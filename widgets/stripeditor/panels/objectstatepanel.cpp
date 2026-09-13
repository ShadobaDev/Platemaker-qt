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
#include "tailseditor.h"
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
    return g == PropertyGroup::Text || g == PropertyGroup::Skin;
}

QString sectionTitle(PropertyGroup g)
{
    switch (g) {
    case PropertyGroup::Shape: return ObjectStatePanel::tr("Shape");
    case PropertyGroup::Skin:  return ObjectStatePanel::tr("Fill && outline");
    case PropertyGroup::Style: return ObjectStatePanel::tr("Line style");
    case PropertyGroup::Text:  return ObjectStatePanel::tr("Text");
    case PropertyGroup::Tail:  return ObjectStatePanel::tr("Tails");
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

    m_emptyHint = new QLabel(tr("Select an object on the strip to edit it."), this);
    m_emptyHint->setWordWrap(true);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setEnabled(false);      // reads as inactive without a hardcoded colour
    lay->addWidget(m_emptyHint);

    // One section per group, in a fixed order, so a group present on two consecutive selections stays
    // where it was and the panel does not shuffle under the cursor.
    for (PropertyGroupEditor* e : m_groups.all()) {
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
    actionRow->addWidget(fitBtn);
    actionRow->addWidget(delBtn);
    lay->addWidget(m_actions);
    lay->addStretch(1);

    m_commitTimer = new QTimer(this);
    m_commitTimer->setSingleShot(true);
    m_commitTimer->setInterval(k_commitDebounceMs);
    connect(m_commitTimer, &QTimer::timeout, this, [this] {
        if (m_hasSelection)
            emit committed(m_artifact);
    });

    // A group editor reports the same two things whatever it edits, so five editors map onto the two
    // signals this panel emits, once.
    for (PropertyGroupEditor* e : m_groups.all()) {
        connect(e, &PropertyGroupEditor::edited,    this, [this] { onControlChanged(); });
        connect(e, &PropertyGroupEditor::committed, this, [this] {
            if (m_hasSelection)
                emit committed(m_artifact);
        });
    }
    // Picking a shape gives you the shape its tile shows, tail and all — connected before the collector
    // above runs, because Qt calls slots in connection order and the tails editor has to hear first.
    connect(m_groups.shape(), &ShapeEditor::edited, this, [this] {
        m_groups.tails()->shapeChanged(m_groups.shape()->values().kind);
    });

    connect(fitBtn, &QPushButton::clicked, this, [this] { if (m_hasSelection) emit fitRequested(); });
    connect(delBtn, &QPushButton::clicked, this, [this] { if (m_hasSelection) emit deleteRequested(); });

    clearSelection();
}

void ObjectStatePanel::setArtifact(const TextArtifact& a)
{
    m_artifact     = a;
    m_hasSelection = true;

    m_populating = true;
    m_groups.bind(m_artifact);
    m_populating = false;

    m_subject->setText(m_artifact.shape.kind == TextArtifact::Shape::None ? tr("Text") : tr("Bubble"));
    m_subject->setVisible(true);
    m_emptyHint->setVisible(false);
    m_actions->setVisible(true);
    applyKindVisibility();
}

void ObjectStatePanel::clearSelection()
{
    m_hasSelection = false;
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
    // A shapeless object has no silhouette, so there is nothing to fill, nothing to roughen and nothing
    // for a tail to grow from. Shape stays: it is how a caption grows a balloon, until a conversion tool
    // takes that over.
    const bool hasShape = m_artifact.shape.kind != TextArtifact::Shape::None;
    for (auto it = m_sections.cbegin(); it != m_sections.cend(); ++it) {
        const auto g = static_cast<PropertyGroup>(it.key());
        const bool applies = (g == PropertyGroup::Shape || g == PropertyGroup::Text) ? true : hasShape;
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

    m_groups.collect(m_artifact);

    // A shape change can add or remove whole sections — the object is still the same object, so this is
    // the one moment the panel is allowed to re-lay itself out.
    applyKindVisibility();
    m_subject->setText(m_artifact.shape.kind == TextArtifact::Shape::None ? tr("Text") : tr("Bubble"));

    if (!m_hasSelection)
        return;

    emit changed(m_artifact);
    m_commitTimer->start();
}

}  // namespace StripEdit
