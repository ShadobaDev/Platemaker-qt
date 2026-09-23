#include "objectstatepanel.hpp"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include "badge.hpp"
#include "collapsiblesection.hpp"
#include "presetstore.hpp"
#include "shapeeditor.hpp"
#include "skineditor.hpp"
#include "styleeditor.hpp"
#include "taileditor.hpp"
#include "taillisteditor.hpp"
#include "texteditor.hpp"

namespace StripEdit {

namespace {

constexpr double k_minPercent = 1.0;      //!< Below this a picture is a dot nobody can grab back.
constexpr double k_maxPercent = 2000.0;   //!< A sound effect ten times the strip's width is still work.


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

ObjectStatePanel::ObjectStatePanel(PresetStore& presets, QWidget* parent)
    : QWidget(parent)
    , m_presets(presets)
    , m_groups(this)
{
    auto* lay = new QVBoxLayout(this);

    // Names the subject. The whole answer to "what am I editing", which the artist should never have to
    // work out from which controls happen to be on screen. After it, what the selection currently looks
    // like — a chip, because it reports and does not offer.
    m_subject = new QLabel(this);
    m_subject->setTextFormat(Qt::PlainText);
    QFont subjectFont = m_subject->font();
    subjectFont.setBold(true);
    m_subject->setFont(subjectFont);
    m_header = new QHBoxLayout;
    m_header->addWidget(m_subject);
    m_header->addStretch(1);
    lay->addLayout(m_header);

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

    // Under the sections and above the actions: what the object *is* composited as. It is not one of
    // the groups — it belongs to the overlay, not to the record — so it sits outside them, and it is
    // the only row a picture and a balloon both have.
    // A picture's size, in its own terms. Above blend because it is the bigger question about a
    // picture, and both are the overlay's rather than the drawing's — which is why neither is a group.
    m_scaleRow      = new QWidget(this);
    auto* scaleForm = new QFormLayout(m_scaleRow);
    scaleForm->setContentsMargins(0, 0, 0, 0);
    m_scale = new QDoubleSpinBox(m_scaleRow);
    m_scale->setRange(k_minPercent, k_maxPercent);
    m_scale->setDecimals(1);
    m_scale->setSuffix(tr(" %"));
    m_scale->setSingleStep(5.0);
    m_scale->setToolTip(tr("How big the picture is drawn, as a percentage of its own pixels. 100% is "
                           "one image pixel per strip pixel — the size it was drawn at."));
    scaleForm->addRow(tr("Size"), m_scale);
    lay->addWidget(m_scaleRow);
    // Live while it moves, settled when it settles — the contract every control in ③ follows.
    connect(m_scale, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (!m_populating)
            emit scaleChanged(v);
    });
    connect(m_scale, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!m_populating)
            emit scaleCommitted(m_scale->value());
    });

    m_blend = new BlendEditor(this);
    lay->addWidget(m_blend);
    connect(m_blend, &BlendEditor::blendPicked, this, &ObjectStatePanel::blendPicked);

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

void ObjectStatePanel::setArtifact(const Artifact& a)
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

    // A picture names itself by its file: one balloon is much like another, but which picture this is
    // is the only thing that tells it from the next one.
    m_subject->setText(m_artifact.isArtwork()      ? m_artifact.artwork
                       : m_artifact.hasSilhouette() ? tr("Bubble")
                                                    : tr("Text"));
    m_subject->setVisible(true);
    refreshLook();
    m_emptyHint->setText(m_emptyText);
    m_emptyHint->setVisible(false);
    m_actions->setVisible(true);
    m_fitButton->setVisible(!m_artifact.isArtwork());   // a picture's box is the picture's, not its words'
    m_deleteButton->setText(tr("Delete"));
    applyKindVisibility();
}

void ObjectStatePanel::setMixedSubjects(int count)
{
    m_subjects.clear();
    m_hasArtifact    = false;
    m_selectionCount = count;
    m_tailIndex      = -1;
    m_commitTimer->stop();

    m_subject->setText(tr("%n objects", "", count));
    m_subject->setVisible(true);
    m_emptyHint->setText(tr("A balloon and a tail have only their position in common — drag to "
                            "move them together."));
    m_emptyHint->setVisible(true);
    m_actions->setVisible(true);
    m_fitButton->setVisible(false);
    m_scaleRow->setVisible(false);
    m_deleteButton->setText(tr("Delete"));
    for (auto it = m_sections.cbegin(); it != m_sections.cend(); ++it)
        it.value()->setVisible(false);
    refreshLook();
}

void ObjectStatePanel::setArtifacts(const QList<Artifact>& objects)
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
    for (const Artifact& a : objects) {
        all.append(&a);
        if (a.hasSilhouette())
            shaped.append(&a);
    }

    m_populating = true;
    m_groups.skin()->bind(shaped);
    m_groups.style()->bind(shaped);   // a line style roughens an outline; a caption has none to roughen
    m_groups.text()->bind(all);
    m_populating = false;

    m_subject->setText(tr("%n objects", "", m_selectionCount));
    m_subject->setVisible(true);
    refreshLook();
    m_emptyHint->setVisible(false);
    m_actions->setVisible(true);
    m_fitButton->setVisible(false);   // one box cannot be fitted to several texts
    m_scaleRow->setVisible(false);    // one spin box cannot be several pictures' sizes
    m_deleteButton->setText(tr("Delete"));

    // **The union**: a section is here when at least one of them carries that group — carriesGroup()
    // answers for each, as it does for a single subject.
    //
    // Shape and the tails are **absent** for a set on purpose, and that is a policy of this surface
    // rather than something about the records: giving several objects one shape is a conversion, which
    // is its own act with its own menu, and adding a tail to five balloons is five objects, not one
    // property.
    for (auto it = m_sections.cbegin(); it != m_sections.cend(); ++it) {
        const auto g = static_cast<PropertyGroup>(it.key());
        const bool forASet = g != PropertyGroup::Shape && g != PropertyGroup::Tail;
        it.value()->setVisible(forASet
                               && std::any_of(objects.cbegin(), objects.cend(),
                                              [g](const Artifact& a) { return carriesGroup(a, g); }));
    }
}

void ObjectStatePanel::setTail(const Artifact& a, int index)
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
    refreshLook();
    m_emptyHint->setVisible(false);
    m_actions->setVisible(true);
    m_fitButton->setVisible(false);   // a tail holds no text to fit
    m_scaleRow->setVisible(false);
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
    refreshLook();
    m_subject->setVisible(false);
    m_emptyHint->setVisible(true);
    m_actions->setVisible(false);
    m_scaleRow->setVisible(false);
    for (CollapsibleSection* s : std::as_const(m_sections))
        s->setVisible(false);
}

void ObjectStatePanel::focusText()
{
    m_groups.text()->focusContent();
}

void ObjectStatePanel::applyKindVisibility()
{
    // A selected tail is a subject of its own with one section, and the balloon's groups stay the
    // balloon's. Otherwise: a shapeless object has no silhouette, so there is nothing to fill, nothing
    // to roughen, nothing for a tail to grow from — **and no silhouette to choose between**, which is
    // why Shape goes with them. Shape answers *which* balloon; whether there is a balloon at all is a
    // kind, and kinds are changed by *Convert to ▸*. The text is the only group every kind carries.
    //
    // The editors are still bound and still written back (`PropertyGroupSet::collect()` writes every
    // group), so a hidden Shape editor holds the object's own `None` and hands it straight back — which
    // is exactly what must happen: a panel may not convert an object by being edited.
    const bool tailSubject = m_tailIndex >= 0;
    for (auto it = m_sections.cbegin(); it != m_sections.cend(); ++it) {
        const auto g = static_cast<PropertyGroup>(it.key());
        // A selected tail is a subject of its own with exactly one section; otherwise the record
        // answers, through the same rule a set is measured by.
        it.value()->setVisible(tailSubject ? g == PropertyGroup::TailItem
                                           : carriesGroup(m_artifact, g));
    }
}


void ObjectStatePanel::setArtworkScale(std::optional<double> percent)
{
    m_scaleRow->setVisible(percent.has_value());
    if (!percent)
        return;
    m_populating = true;
    m_scale->setValue(qBound(k_minPercent, *percent, k_maxPercent));
    m_populating = false;
}

void ObjectStatePanel::setSelectionBlend(std::optional<Platemaker::Models::BlendMode> blend,
                                        bool applies)
{
    m_blend->setVisible(applies);
    if (applies)
        m_blend->setBlend(blend);
}

void ObjectStatePanel::refreshLook()
{
    // Rebuilt rather than relabelled: a painted chip carries its text, its colours and its tooltip
    // together, and a setter for each would be three ways to leave two of them disagreeing.
    delete m_lookChip;
    m_lookChip = nullptr;

    QString text;
    QString detail;
    if (m_tailIndex >= 0 || m_selectionCount == 0 || (m_hasArtifact && m_artifact.isArtwork())) {
        // A tail has no look of its own, nothing selected has nothing to report, and a picture's look
        // is whoever drew it — a preset covers a shape, a fill and a line style it does not have.
    } else if (m_hasArtifact) {
        text   = m_presets.lookLabel(m_artifact);
        detail = m_presets.matching(m_artifact) >= 0
                     ? tr("Every property a preset covers still matches “%1”.").arg(text)
                     : tr("The look of no preset: something a preset covers has been changed since.");
    } else if (!m_subjects.isEmpty()) {
        const QString first = m_presets.lookLabel(m_subjects.first());
        const bool    agree = std::all_of(m_subjects.cbegin(), m_subjects.cend(),
                                          [this, &first](const Artifact& a) {
                                              return m_presets.lookLabel(a) == first;
                                          });
        text   = agree ? first : tr("Mixed");
        detail = agree ? tr("Every object in the selection has this look.")
                       : tr("The selected objects do not share one look.");
    }

    if (text.isEmpty())
        return;
    m_lookChip = makeBadge(toneBadge(BadgeTone::Neutral, text, detail, palette()), this);
    m_header->insertWidget(1, m_lookChip);   // after the subject, before the stretch
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
        for (Artifact& a : m_subjects) {
            if (a.hasSilhouette()) {
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
        m_subject->setText(m_artifact.hasSilhouette() ? tr("Bubble") : tr("Text"));
    }

    // One changed property and it is no longer that preset (Q40). Computed, so it flips back by itself
    // if the artist edits the value to an exact match again.
    refreshLook();

    if (!m_hasArtifact)
        return;

    emit changed(m_artifact);
    m_commitTimer->start();
}


}  // namespace StripEdit
