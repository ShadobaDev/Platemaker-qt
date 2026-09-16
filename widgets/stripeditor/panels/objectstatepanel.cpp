#include "objectstatepanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include "collapsiblesection.h"
#include "shapeeditor.h"
#include "skineditor.h"
#include "styleeditor.h"
#include "taileditor.h"
#include "taillisteditor.h"
#include "texteditor.h"

namespace StripEdit {

namespace {

constexpr int k_commitDebounceMs = 300; //!< Coalesce typing into one undo step this long after it stops.

//! Where an expanded section is remembered — a working preference, so it follows the artist.
QString expansionKey(PropertyGroup g)
{
    return QStringLiteral("stripEditor/objectState/expanded/%1").arg(int(g));
}

//! Open on a first run: what is edited most, and what is looked at most.
bool expandedByDefault(PropertyGroup g)
{
    return g == PropertyGroup::Text || g == PropertyGroup::Skin || g == PropertyGroup::TailItem;
}

QString sectionTitle(PropertyGroup g)
{
    switch (g) {
    case PropertyGroup::Shape: return ObjectStatePanel::tr("Shape");
    case PropertyGroup::Skin:  return ObjectStatePanel::tr("Fill && outline");
    case PropertyGroup::Style: return ObjectStatePanel::tr("Line style");
    case PropertyGroup::Text:  return ObjectStatePanel::tr("Text");
    case PropertyGroup::Tail:  return ObjectStatePanel::tr("Tails");
    case PropertyGroup::TailItem: return ObjectStatePanel::tr("Tail");
    default: break;
    }
    return {};
}

} // namespace

ObjectStatePanel::ObjectStatePanel(QWidget* parent)
    : QWidget(parent)
    , m_groups(this)
{
    auto* lay = new QVBoxLayout(this);

    // Names the subject. The whole answer to "what am I editing", which the artist should never have to
    // work out from which controls happen to be on screen.
    m_subject = new QLabel(this);
    m_subject->setTextFormat(Qt::PlainText);
    QFont subjectFont = m_subject->font();
    subjectFont.setBold(true);
    m_subject->setFont(subjectFont);
    lay->addWidget(m_subject);

    m_emptyText = tr("Select an object on the strip to edit it.");
    m_emptyHint = new QLabel(m_emptyText, this);
    m_emptyHint->setWordWrap(true);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setEnabled(false);      // reads as inactive without a hardcoded colour
    lay->addWidget(m_emptyHint);

    // The look's four, then a balloon's tails as a list, then one tail — the last shown only when a tail
    // is the subject.
    m_tailList = new TailListEditor(this);
    m_tail     = new TailEditor(this);
    QList<PropertyGroupEditor*> editors = m_groups.all();
    editors << m_tailList << m_tail;

    // One section per group, in a fixed order, so a group present on two consecutive selections stays
    // where it was and the panel does not shuffle under the cursor.
    for (PropertyGroupEditor* e : editors) {
        auto* section = new CollapsibleSection(sectionTitle(e->group()), this);
        section->setContent(e);
        lay->addWidget(section);
        m_sections.insert(int(e->group()), section);

        connect(section, &CollapsibleSection::expandedChanged, this, [g = e->group()](bool on) {
            QSettings().setValue(expansionKey(g), on);
        });
    }
    restoreExpansion();

    // Fit and Delete act on the selection, so they live with the selection.
    m_actions = new QWidget(this);
    auto* actionRow = new QHBoxLayout(m_actions);
    actionRow->setContentsMargins(0, 0, 0, 0);
    auto* fitBtn = new QPushButton(tr("Fit to text"), m_actions);
    fitBtn->setToolTip(tr("Size the bubble to the line it holds."));
    auto* delBtn = new QPushButton(tr("Delete"), m_actions);
    m_fitButton    = fitBtn;
    m_deleteButton = delBtn;
    actionRow->addWidget(fitBtn);
    actionRow->addWidget(delBtn);
    lay->addWidget(m_actions);
    lay->addStretch(1);

    m_commitTimer = new QTimer(this);
    m_commitTimer->setSingleShot(true);
    m_commitTimer->setInterval(k_commitDebounceMs);
    connect(m_commitTimer, &QTimer::timeout, this, [this] {
        if (!m_subjects.isEmpty())
            emit committedMany(m_subjects);
        else if (m_hasArtifact)
            emit committed(m_artifact);
    });

    // A group editor reports the same two things whatever it edits, so every editor maps onto the two
    // signals this panel emits, once.
    for (PropertyGroupEditor* e : editors) {
        connect(e, &PropertyGroupEditor::edited,    this, [this] { onControlChanged(); });
        connect(e, &PropertyGroupEditor::committed, this, [this] {
            if (!m_subjects.isEmpty())
                emit committedMany(m_subjects);
            else if (m_hasArtifact)
                emit committed(m_artifact);
        });
    }
    // Picking a shape gives you the shape its tile shows, tail and all — connected before the collector
    // above runs, because Qt calls slots in connection order and the tails editor has to hear first.
    connect(m_groups.shape(), &ShapeEditor::edited, this, [this] {
        m_tailList->shapeChanged(m_groups.shape()->values().kind);
    });

    connect(fitBtn, &QPushButton::clicked, this, [this] { if (m_hasArtifact) emit fitRequested(); });
    // Delete asks how *many* are selected, not whether one artifact is bound: with several selected there
    // is nothing to bind and still everything to remove.
    connect(delBtn, &QPushButton::clicked, this, [this] { if (m_selectionCount > 0) emit deleteRequested(); });

    clearSelection();
}

void ObjectStatePanel::setArtifact(const TextArtifact& a)
{
    m_artifact      = a;
    m_tailIndex     = -1;
    m_hasArtifact   = true;
    m_selectionCount = 1;

    m_populating = true;
    m_subjects.clear();
    m_groups.bind(m_artifact);
    m_tailList->bindOne(m_artifact);
    m_populating = false;

    m_subject->setText(m_artifact.shape.kind == TextArtifact::Shape::None ? tr("Text") : tr("Bubble"));
    m_subject->setVisible(true);
    m_emptyHint->setVisible(false);
    m_emptyHint->setText(m_emptyText);
    m_actions->setVisible(true);
    m_fitButton->setVisible(true);
    m_deleteButton->setText(tr("Delete"));
    applyKindVisibility();
}

void ObjectStatePanel::setArtifacts(const QList<TextArtifact>& objects)
{
    m_subjects       = objects;
    m_hasArtifact    = false;   // no single artifact, so the single-subject signals stay quiet
    m_selectionCount = static_cast<int>(objects.size());
    m_tailIndex      = -1;
    m_commitTimer->stop();

    // A role is a property several kinds share, so each group is bound to the objects that have it: the
    // lettering's colour every object has, a fill only something with a silhouette does. Binding a
    // shapeless object into the skin group would have it vote "Mixed" with values it never uses.
    PropertyGroupEditor::Subjects all;
    PropertyGroupEditor::Subjects shaped;
    all.reserve(objects.size());
    for (const TextArtifact& a : objects) {
        all.append(&a);
        if (a.shape.kind != TextArtifact::Shape::None)
            shaped.append(&a);
    }

    m_populating = true;
    m_groups.skin()->bind(shaped);
    m_groups.style()->bind(shaped);   // a line style roughens an outline; a caption has none to roughen
    m_groups.text()->bind(all);
    m_populating = false;

    m_subject->setText(tr("%n objects", "", m_selectionCount));
    m_subject->setVisible(true);
    m_emptyHint->setVisible(false);
    m_actions->setVisible(true);
    m_fitButton->setVisible(false);   // one box cannot be fitted to several texts
    m_deleteButton->setText(tr("Delete"));

    // The union: a section is here when at least one of them carries that group. Skin needs a silhouette
    // to sit on; the lettering's colour every object has.
    const bool anyShape = !shaped.isEmpty();
    // Shape and the tails are **absent** for a set on purpose. Giving several objects one shape is a
    // conversion, which is its own act with its own menu; adding a tail to five balloons is five
    // objects, not one property.
    for (auto it = m_sections.cbegin(); it != m_sections.cend(); ++it) {
        const auto g = static_cast<PropertyGroup>(it.key());
        const bool applies = g == PropertyGroup::Text
                          || ((g == PropertyGroup::Skin || g == PropertyGroup::Style) && anyShape);
        it.value()->setVisible(applies);
    }
}

void ObjectStatePanel::setTail(const TextArtifact& a, int index)
{
    m_artifact      = a;
    m_tailIndex     = index;
    m_hasArtifact   = true;
    m_selectionCount = 1;

    m_subjects.clear();
    m_populating = true;
    m_tail->setIndex(index);
    m_tail->bindOne(m_artifact);
    m_populating = false;

    m_subject->setText(tr("Tail %1").arg(index + 1));
    m_subject->setVisible(true);
    m_emptyHint->setVisible(false);
    m_actions->setVisible(true);
    m_fitButton->setVisible(false);   // a tail holds no text to fit
    m_deleteButton->setText(tr("Delete tail"));
    applyKindVisibility();
}

void ObjectStatePanel::clearSelection()
{
    m_hasArtifact    = false;
    m_selectionCount = 0;
    m_tailIndex      = -1;
    m_subjects.clear();
    m_commitTimer->stop();

    // Gone, not greyed. There is no object, so there are no properties — and a greyed control would
    // promise one that arriving later would not deliver.
    m_subject->setVisible(false);
    m_emptyHint->setVisible(true);
    m_actions->setVisible(false);
    for (CollapsibleSection* s : std::as_const(m_sections))
        s->setVisible(false);
}

void ObjectStatePanel::focusText()
{
    m_groups.text()->focusContent();
}

void ObjectStatePanel::applyKindVisibility()
{
    // A selected tail is a subject of its own with one section, and the balloon's groups stay the balloon's.
    // Otherwise: a shapeless object has no silhouette, so there is nothing to fill, nothing to roughen and
    // nothing for a tail to grow from. Shape stays: it is how a caption grows a balloon, until a conversion
    // tool takes that over.
    const bool tailSubject = m_tailIndex >= 0;
    const bool hasShape    = m_artifact.shape.kind != TextArtifact::Shape::None;
    for (auto it = m_sections.cbegin(); it != m_sections.cend(); ++it) {
        const auto g = static_cast<PropertyGroup>(it.key());
        bool applies = false;
        if (g == PropertyGroup::TailItem)
            applies = tailSubject;
        else if (!tailSubject)
            applies = (g == PropertyGroup::Shape || g == PropertyGroup::Text) ? true : hasShape;
        it.value()->setVisible(applies);
    }
}


void ObjectStatePanel::restoreExpansion()
{
    const QSettings st;
    for (auto it = m_sections.cbegin(); it != m_sections.cend(); ++it) {
        const auto g = static_cast<PropertyGroup>(it.key());
        it.value()->setExpanded(st.value(expansionKey(g), expandedByDefault(g)).toBool());
    }
}

void ObjectStatePanel::onControlChanged()
{
    if (m_populating)
        return;

    if (!m_subjects.isEmpty()) {
        // A set: every object takes what the artist touched and keeps everything else of its own.
        for (TextArtifact& a : m_subjects) {
            if (a.shape.kind != TextArtifact::Shape::None) {
                m_groups.skin()->applyEditedTo(a);   // a caption with no balloon has no fill to take
                m_groups.style()->applyEditedTo(a);
            }
            m_groups.text()->applyEditedTo(a);
        }
        emit changedMany(m_subjects);
        m_commitTimer->start();
        return;
    }

    if (m_tailIndex >= 0) {
        m_tail->applyTo(m_artifact);   // that tail, and nothing else about the balloon
    } else {
        m_groups.collect(m_artifact, m_tailList);

        // A shape change can add or remove whole sections — the object is still the same object, so this
        // is the one moment the panel is allowed to re-lay itself out.
        applyKindVisibility();
        m_subject->setText(m_artifact.shape.kind == TextArtifact::Shape::None ? tr("Text") : tr("Bubble"));
    }

    if (!m_hasArtifact)
        return;

    emit changed(m_artifact);
    m_commitTimer->start();
}


}  // namespace StripEdit
