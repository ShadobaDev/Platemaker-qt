#include "tooloptions/bubbletooloptions.hpp"

#include <algorithm>

#include <QAction>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QToolButton>
#include <QFrame>
#include <QVBoxLayout>

#include "presets/presetsdialog.hpp"
#include "presetstore.hpp"
#include "properties/shapeeditor.hpp"
#include "properties/skineditor.hpp"
#include "properties/effectseditor.hpp"
#include "properties/textureeditor.hpp"
#include "properties/styleeditor.hpp"
#include "properties/nexttaileditor.hpp"
#include "properties/texteditor.hpp"

namespace StripEdit {

BubbleToolOptions::BubbleToolOptions(PresetStore& presets, QWidget* parent)
    : QWidget(parent)
    , m_presets(presets)
    , m_groups(this)
{
    auto* lay = new QVBoxLayout(this);

    // --- Presets ------------------------------------------------------------------------------------
    // Above the controls it fills, and outside the Shape group, because a preset also carries the font
    // and the colours the Text tool uses.
    m_presetCombo = new QComboBox(this);
    // **The picker shows what the next object is made from:** a preset, or *No preset* — the default. A
    // row changed below is laid over it and the picker keeps its name; picking again starts over from
    // it. A preset is a quick way to set many fields at once, not a promise.
    m_presetCombo->setPlaceholderText(tr("Pick a preset…"));   // only after the picked one was deleted
    m_presetCombo->setToolTip(tr("What the next object is made from. A preset is saved from an object, "
                                 "with Save as preset… on its menu."));
    m_presetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_presetCombo->setIconSize(QSize(k_bubbleThumbW, k_bubbleThumbH));

    auto* presetMore = new QToolButton(this);
    presetMore->setText(QStringLiteral("⋯"));
    presetMore->setPopupMode(QToolButton::InstantPopup);
    auto* presetMenu = new QMenu(presetMore);
    m_presetDelete     = presetMenu->addAction(tr("Delete preset"));
    QAction* manage    = presetMenu->addAction(tr("Manage presets…"));
    connect(manage, &QAction::triggered, this, [this] { PresetsDialog(m_presets, m_pickedName, this).exec(); });
    // Importing and exporting a pack move a file between machines and people — the menu bar's business
    // (Tools), where every other import and export is, rather than a panel's.
    presetMore->setMenu(presetMenu);

    auto* presetRow = new QHBoxLayout;

    presetRow->addWidget(m_presetCombo, 1);
    presetRow->addWidget(presetMore);
    lay->addLayout(presetRow);

    // --- Shape (Bubble tool only) -------------------------------------------------------------------
    m_shapeGroup = new QGroupBox(tr("Shape"), this);
    auto* shapeLay = new QVBoxLayout(m_shapeGroup);
    shapeLay->addWidget(m_groups.shape());
    // The shape's parameters are on a widget of their own, for the panel to lay out: under the tiles.
    shapeLay->addWidget(m_groups.shape()->parameters());
    m_tails = new NextTailEditor(this);
    shapeLay->addWidget(m_tails);
    // Fill, line, texture, effects: OBJECT STATE's sections, in its order, ruled apart here
    // since this page has no sections of its own.
    for (QWidget* part : {static_cast<QWidget*>(m_groups.skin()), static_cast<QWidget*>(m_groups.style()),
                          static_cast<QWidget*>(m_groups.texture()), static_cast<QWidget*>(m_groups.effects())}) {
        auto* rule = new QFrame(m_shapeGroup);
        rule->setFrameShape(QFrame::HLine);
        rule->setFrameShadow(QFrame::Sunken);
        shapeLay->addWidget(rule);
        shapeLay->addWidget(part);
        m_ruleAbove.insert(part, rule);
    }
    lay->addWidget(m_shapeGroup);

    // --- Text (both tools) --------------------------------------------------------------------------
    m_textGroup = new QGroupBox(tr("Text"), this);
    auto* textLay = new QVBoxLayout(m_textGroup);
    textLay->addWidget(m_groups.text());
    lay->addWidget(m_textGroup);
    lay->addStretch(1);

    // Nothing here acts on an object, because there is no object yet: no line to type.
    m_groups.text()->setContentVisible(false);

    // --- Wiring -------------------------------------------------------------------------------------
    // The one cross-group rule, connected first so it runs first: picking a shape gives you the shape
    // its tile shows, tail and all, and the tails editor has to hear about it before the change is
    // collected. Qt runs slots in connection order, which is the whole reason this line is up here.
    connect(m_groups.shape(), &ShapeEditor::picked, this, [this] {
        m_tails->shapeChanged(m_groups.shape()->values());
    });
    for (PropertyGroupEditor* e : m_groups.all())
        connect(e, &PropertyGroupEditor::changed, this, [this] { onControlChanged(); });
    connect(m_tails, &PropertyGroupEditor::changed, this, [this] { onControlChanged(); });

    // activated(), not currentIndexChanged(): only a human picking an entry applies a preset, so
    // rebuilding the list never restyles anything, and re-picking the current entry re-applies it.
    connect(m_presetCombo, &QComboBox::activated, this, [this](int i) { applyPreset(i); });
    connect(m_presetDelete, &QAction::triggered,   this, [this] { onDeletePreset(); });
    connect(&m_presets, &PresetStore::changed, this, [this] { refreshPresetCombo(); });

    refreshPresetCombo();
    syncFromModel();
}

ShapeProperties BubbleToolOptions::balloonShape() const
{
    ObjectRecord picked;
    m_groups.shape()->applyTo(picked);
    return picked.hasSilhouette() ? picked.shape : ShapeProperties();
}

void BubbleToolOptions::setToolShape(std::optional<ObjectRecord::Shape> shape)
{
    m_toolShape = shape;
}

void BubbleToolOptions::showRows(const QList<Row>& rows)
{
    // An editor shows when any of its rows is on the map, and then only those rows.
    const auto show = [&rows](PropertyGroupEditor* e) {
        e->showOnly(rows);
        const QList<Row> own = e->rows();
        const bool any = std::any_of(own.cbegin(), own.cend(), [&rows](Row r) { return rows.contains(r); });
        e->setVisible(any);
        return any;
    };
    bool shapeBox = false;
    for (PropertyGroupEditor* e : {static_cast<PropertyGroupEditor*>(m_groups.shape()),
                                   static_cast<PropertyGroupEditor*>(m_tails)})
        shapeBox = show(e) || shapeBox;
    for (PropertyGroupEditor* e : {static_cast<PropertyGroupEditor*>(m_groups.skin()),
                                   static_cast<PropertyGroupEditor*>(m_groups.style()),
                                   static_cast<PropertyGroupEditor*>(m_groups.texture()),
                                   static_cast<PropertyGroupEditor*>(m_groups.effects())}) {
        const bool shown = show(e);
        // A rule parts two shown things; the first shown part in the box needs none above it.
        m_ruleAbove.value(e)->setVisible(shown && shapeBox);
        shapeBox = shown || shapeBox;
    }
    m_shapeGroup->setVisible(shapeBox);
    m_textGroup->setVisible(show(m_groups.text()));
}

ObjectRecord BubbleToolOptions::prototype() const
{
    ObjectRecord a;
    // Shape first: the tails editor reads it, because a shapeless record has nothing to grow a tail
    // from. Everything else is order-independent by construction — no two groups touch a property.
    // The whole group, so whatever a shape grows beyond its kind (a radius, a polygon's nodes) arrives
    // with it: the tiles' balloon, which is never None. Then the tool's own kind over it, if it has one.
    a.shape = balloonShape();
    if (m_toolShape)
        a.shape.kind = *m_toolShape;
    m_groups.skin()->applyTo(a);
    m_groups.style()->applyTo(a);
    m_groups.texture()->applyTo(a);
    m_groups.effects()->applyTo(a);
    m_groups.text()->applyTo(a);

    // A placement takes the look and not the line. The same distinction a preset makes — everything a
    // balloon is, minus everything it says — and the same reason: the words belong to one balloon.
    a.text.body.clear();
    // …and to one balloon's aim: applyToNew() makes a first tail rather than copying anyone else's.
    m_tails->applyToNew(a);

    // A fresh seed per bubble, so a page of marker balloons does not wear one repeated wobble —
    // by the same rule every other writer follows, rather than a second one that also seeded balloons
    // with no style to wobble.
    topUpStyleSeed(a);
    return a;
}

// ---------------------------------------------------------------------------

void BubbleToolOptions::onControlChanged()
{
    if (m_populating)
        return;
    m_groups.collect(m_record, m_tails);   // laid over the picked preset, which the picker still names
    // Nothing is emitted: these values describe an object that does not exist yet, so there is nothing
    // to preview and nothing to persist until one is placed.
}

void BubbleToolOptions::syncFromModel()
{
    m_populating = true;
    m_groups.bind(m_record);
    m_tails->bindOne(m_record);
    m_populating = false;
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

void BubbleToolOptions::refreshPresetCombo()
{
    const QSignalBlocker block(m_presetCombo);
    m_presetCombo->clear();
    // Entry 0 is the absence of a preset; entry i + 1 is the store's preset i. No separator: it would be
    // an entry too, and shift every index after it.
    m_presetCombo->addItem(tr("No preset"));
    const QList<BubblePreset>& presets = m_presets.presets();
    int picked = m_pickedName.isEmpty() ? 0 : -1;
    for (int i = 0; i < presets.size(); ++i) {
        const BubblePreset& p = presets.at(i);
        m_presetCombo->addItem(QIcon(bubbleThumbnail(p.record.shape, p.record.skin.fill.colour,
                                                     p.record.style.stroke.colour, p.record.text.fill.colour)),
                               p.name);
        if (p.name == m_pickedName)
            picked = i + 1;
    }
    m_presetCombo->setCurrentIndex(picked);   // -1, and the placeholder, when the picked one is gone
    m_presetDelete->setEnabled(picked > 0 && m_presets.isCustom(picked - 1));
}

void BubbleToolOptions::applyPreset(int entry)
{
    // *No preset* is the default, applied the way a preset is: the same function, with nothing in it but
    // `ObjectRecord`'s own values.
    const int index = entry - 1;
    if (index >= m_presets.presets().size())
        return;
    const BubblePreset base = index >= 0 ? m_presets.presets().at(index) : BubblePreset{QString(), ObjectRecord()};
    m_pickedName = base.name;
    m_record     = PresetStore::applied(base, m_record, /*keepShape=*/m_toolShape.has_value());
    syncFromModel();
    refreshPresetCombo();
}

void BubbleToolOptions::onDeletePreset()
{
    const int i = m_presetCombo->currentIndex() - 1;   // the picked one, and only the artist's own
    if (!m_presets.isCustom(i))
        return;
    if (QMessageBox::question(this, tr("Delete preset"),
                              tr("Delete the preset “%1”?").arg(m_presets.presets().at(i).name))
        != QMessageBox::Yes)
        return;
    m_presets.remove(i);
    // The values stay as they are; they are simply nobody's preset now, and the picker says so.
    refreshPresetCombo();
}

}  // namespace StripEdit
