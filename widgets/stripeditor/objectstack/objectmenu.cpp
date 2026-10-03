#include "objectstack/objectmenu.hpp"

#include "objects/artworkobject.hpp"
#include "objects/object.hpp"
#include "objects/objectcontroller.hpp"
#include "objects/striplayout.hpp"
#include "presetstore.hpp"
#include "properties/blendeditor.hpp"   // blendModes(): the menu and OBJECT STATE's row name the modes from one list
#include "properties/propertygroup.hpp"
#include "toolrail/colourpair.hpp"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsView>
#include <QInputDialog>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>

#include <algorithm>
#include <optional>

namespace StripEdit {

ObjectMenu::ObjectMenu(ObjectController& objects, const StripLayout& layout, PresetStore& presets,
                       QWidget* tree, QGraphicsView* view, QWidget* dialogParent, QObject* parent)
    : QObject(parent)
    , m_objects(objects)
    , m_layout(layout)
    , m_presets(presets)
    , m_tree(tree)
    , m_view(view)
    , m_dialogParent(dialogParent)
{
    // Duplicate / Delete as real QActions: Qt::ActionsContextMenu then builds the list's right-click
    // menu from them for free, and the same objects carry the keyboard shortcuts. They are added to
    // the canvas as well, so they work wherever the bubble was selected — but scoped per widget, so
    // Delete keeps deleting characters while the panel's text box has focus.
    m_actDuplicate = new QAction(tr("Duplicate"), this);
    m_actDuplicate->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    m_actDuplicate->setShortcutContext(Qt::WidgetShortcut);
    connect(m_actDuplicate, &QAction::triggered, &m_objects, &ObjectController::duplicateSelectedOverlay);

    m_presetMenu = new QMenu(tr("Apply preset"), dialogParent);
    connect(m_presetMenu, &QMenu::aboutToShow, this, &ObjectMenu::rebuildPresetMenu);

    m_reanchorMenu = new QMenu(tr("Re-anchor to"), dialogParent);
    connect(m_reanchorMenu, &QMenu::aboutToShow, this, &ObjectMenu::rebuildReanchorMenu);

    // Blend, at last reachable. Built once; which entry is ticked is decided when it opens, because that
    // is the only moment it can be true.
    m_blendMenu = new QMenu(tr("Blend"), dialogParent);
    for (const auto& [mode, name] : blendModes()) {
        QAction* a = m_blendMenu->addAction(name);
        a->setCheckable(true);
        a->setData(static_cast<int>(mode));
        connect(a, &QAction::triggered, this, [this, mode] { m_objects.setSelectionBlend(mode); });
    }
    connect(m_blendMenu, &QMenu::aboutToShow, this, [this] {
        // Ticked from the same answer OBJECT STATE's row shows: with a set that disagrees, nothing is ticked
        // and the row says *Mixed*. One question, one computation.
        const auto shared = m_objects.selectionBlend();
        for (QAction* a : m_blendMenu->actions())
            a->setChecked(shared && a->data().toInt() == static_cast<int>(*shared));
    });

    // The pair, spent from the menu as well as from the canvas — the gesture the bucket offers is easy to
    // miss, and an entry that names it is how it stops being folklore.
    m_actFill = new QAction(tr("Fill with primary colour"), this);
    connect(m_actFill, &QAction::triggered, this, [this] {
        if (m_colours)
            m_objects.applyColourToSelection(m_colours->primary(), Painter::Part::Fill);
    });
    m_actOutline = new QAction(tr("Outline with secondary colour"), this);
    connect(m_actOutline, &QAction::triggered, this, [this] {
        if (m_colours)
            m_objects.applyColourToSelection(m_colours->secondary(), Painter::Part::Outline);
    });

    // One property group at a time, taken from what the tool's options are set to.
    m_groupMenu = new QMenu(tr("Apply from tool options"), dialogParent);
    const QList<QPair<PropertyGroup, QString>> groups{
        {PropertyGroup::Shape, tr("Shape")},
        {PropertyGroup::Skin,  tr("Fill && outline")},
        {PropertyGroup::Style, tr("Line style")},
        {PropertyGroup::Text,  tr("Text style")},
    };
    for (const auto& [group, name] : groups) {
        QAction* a = m_groupMenu->addAction(name);
        connect(a, &QAction::triggered, this, [this, group] { m_objects.applyGroupToSelection(group); });
    }

    m_actSavePreset = new QAction(tr("Save as preset…"), this);
    connect(m_actSavePreset, &QAction::triggered, this, &ObjectMenu::saveSelectionAsPreset);

    // **Convert crosses the one boundary there is.** An object either has a silhouette — to fill, to
    // roughen, to grow tails from — or it has not, and that decides which property groups it carries at
    // all. *Which* silhouette is a property, edited in OBJECT STATE and applied to a set from *Apply from
    // tool options ▸*; it has no business in a menu about kinds. Two entries, therefore, not ten.
    //
    // A menu offers only what applies to every selected object, and here that intersection is either
    // both or neither: every authored object can become either
    // kind, and something that is not an authored object — imported artwork, or a tail — can become
    // nothing, which is what greys the menu.
    m_convertMenu = new QMenu(tr("Convert to"), dialogParent);
    m_actToText   = m_convertMenu->addAction(tr("Text"));
    m_actToText->setCheckable(true);
    connect(m_actToText, &QAction::triggered, this,
            [this] { m_objects.convertSelectionTo(ObjectRecord::Shape::None); });
    m_actToBalloon = m_convertMenu->addAction(tr("Balloon"));
    m_actToBalloon->setCheckable(true);
    // The silhouette it arrives at is the one the tool's options are set to — the same source *Apply
    // from tool options ▸* spends, so there is one answer to "which balloon" and not two.
    connect(m_actToBalloon, &QAction::triggered, this, [this] {
        m_objects.convertSelectionTo(m_objects.toolBalloonShape());
    });
    connect(m_convertMenu, &QMenu::aboutToShow, this, [this] {
        // Ticked only when the selection agrees, exactly as Blend is: with a set that disagrees,
        // nothing is ticked, which is the same answer OBJECT STATE gives when it says Mixed. The tick is on
        // the *kind*, so any silhouette ticks Balloon — the shape tiles say which one.
        std::optional<bool> shaped;
        bool                agree = true;
        for (const QString& uid : std::as_const(m_objects.selectedOverlays())) {
            if (m_objects.carriers().contains(uid) || !m_objects.isParametric(uid))
                continue;
            const bool hasShape = m_objects.recordFor(uid).hasSilhouette();
            if (!shaped)
                shaped = hasShape;
            else if (*shaped != hasShape)
                agree = false;
        }
        m_actToText->setChecked(shaped && agree && !*shaped);
        m_actToBalloon->setChecked(shaped && agree && *shaped);
    });

    m_actForward = new QAction(tr("Bring forward"), this);
    connect(m_actForward, &QAction::triggered, this, [this] { m_objects.moveSelectedInStack(true); });
    m_actBackward = new QAction(tr("Send back"), this);
    connect(m_actBackward, &QAction::triggered, this, [this] { m_objects.moveSelectedInStack(false); });

    // Artwork's own two. They are not on a balloon's menu, because a balloon has no "own size": its
    // drawing is generated at whatever size it is given.
    m_actNaturalSize = new QAction(tr("Original size"), this);
    connect(m_actNaturalSize, &QAction::triggered, this, [this] { m_objects.scaleSelectedArtwork(100.0); });
    m_actFitToStrip = new QAction(tr("Fit to strip width"), this);
    connect(m_actFitToStrip, &QAction::triggered, this, [this] {
        const auto* art = qobject_cast<const ArtworkObject*>(m_objects.object(m_objects.selectedOverlay()));
        const double tw = m_layout.targetWidth();
        if (art && tw > 0 && art->artwork().width() > 0)
            m_objects.scaleSelectedArtwork(tw * 100.0 / art->artwork().width());
    });

    m_actDelete = new QAction(tr("Delete"), this);
    m_actDelete->setShortcut(QKeySequence::Delete);
    m_actDelete->setShortcutContext(Qt::WidgetShortcut);
    connect(m_actDelete, &QAction::triggered, &m_objects, &ObjectController::deleteSelectedOverlay);

    // Artwork drawn elsewhere — a balloon inked on a tablet, a logo — placed as an overlay like any
    // other. Always available, unlike Duplicate/Delete, because it needs no selection.
    m_actImport = new QAction(tr("Import artwork…"), this);
    connect(m_actImport, &QAction::triggered, this, &ObjectMenu::importArtwork);

    // The menu is the widgets' own action list (Qt::ActionsContextMenu), so the canvas and the tree
    // cannot drift apart and every entry keeps its shortcut. Sections are separators in that same list:
    // what the object *looks like*, then where it sits in the stack, then what happens to it as a whole.
    const auto separator = [this] {
        auto* a = new QAction(this);
        a->setSeparator(true);
        return a;
    };
    for (QWidget* w : {static_cast<QWidget*>(m_tree), static_cast<QWidget*>(m_view)}) {
        w->addAction(m_presetMenu->menuAction());
        w->addAction(m_actSavePreset);
        w->addAction(m_groupMenu->menuAction());
        w->addAction(m_actFill);
        w->addAction(m_actOutline);
        w->addAction(m_blendMenu->menuAction());
        w->addAction(separator());
        w->addAction(m_actForward);
        w->addAction(m_actBackward);
        w->addAction(m_actNaturalSize);
        w->addAction(m_actFitToStrip);
        w->addAction(separator());
        w->addAction(m_convertMenu->menuAction());
        w->addAction(m_reanchorMenu->menuAction());
        w->addAction(m_actDuplicate);
        w->addAction(m_actDelete);
        w->addAction(separator());
        w->addAction(m_actImport);
    }
    // Both widgets, not just the list. The actions were added to the canvas from the start — which is why
    // the shortcuts worked there — but without this the canvas had nothing to build a menu *from*, so a
    // right-click on the strip did nothing at all.
    m_tree->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_view->setContextMenuPolicy(Qt::ActionsContextMenu);

    connect(&m_objects, &ObjectController::selectionChanged, this, &ObjectMenu::updateEntries);
}

void ObjectMenu::updateEntries()
{
    // An action that acts on one object stays disabled while several things are selected rather than
    // quietly acting on the primary: a control that does something other than what the panel names is a
    // lie. A tail can only be deleted.
    const QStringList&                      picked      = m_objects.selectedOverlays();
    const QList<ObjectController::TailRef>& pickedTails = m_objects.selectedTails();
    const bool oneObject = picked.size() == 1 && pickedTails.isEmpty();
    if (m_actDuplicate) m_actDuplicate->setEnabled(oneObject);
    if (m_actDelete)    m_actDelete->setEnabled(!picked.isEmpty());
    if (m_presetMenu)   m_presetMenu->menuAction()->setEnabled(oneObject);
    if (m_reanchorMenu) m_reanchorMenu->menuAction()->setEnabled(oneObject);
    // Blend is a property every object has, so a set can take it; the stack order is one object's place
    // among the others, so it is not a thing several can be told at once.
    const bool anyObject = picked.size() > m_objects.carriers().size();
    if (m_blendMenu)   m_blendMenu->menuAction()->setEnabled(anyObject);
    if (m_actForward)  m_actForward->setEnabled(oneObject);
    if (m_actBackward) m_actBackward->setEnabled(oneObject);
    if (m_groupMenu)   m_groupMenu->menuAction()->setEnabled(anyObject);
    if (m_actFill)     m_actFill->setEnabled(anyObject && m_colours);
    if (m_actOutline)  m_actOutline->setEnabled(anyObject && m_colours);
    if (m_actSavePreset) m_actSavePreset->setEnabled(oneObject);

    // **A menu entry that cannot apply is not greyed, it is absent.** Greying says *not now*; these
    // never apply to a picture somebody else drew — it has no preset, no fill and no outline to give
    // it, and nothing to re-type. A balloon's menu is a balloon's.
    //
    // **Every** selected object has to be able to take it, not merely one of them — the same
    // intersection rule *Convert to ▸* follows. Offering *Fill with primary colour* for a
    // selection of a balloon and a picture would be offering to do it to both, and it would quietly
    // do it to one: a menu that acts on part of what is selected is a menu that lied about its
    // subject.
    const bool allParametric = !picked.isEmpty()
        && std::all_of(picked.cbegin(), picked.cend(), [this](const QString& uid) {
               return m_objects.isParametric(uid) || m_objects.carriers().contains(uid);
           });
    for (QAction* a : {m_actSavePreset, m_actFill, m_actOutline})
        if (a)
            a->setVisible(allParametric);
    if (m_presetMenu) m_presetMenu->menuAction()->setVisible(allParametric);
    if (m_groupMenu)  m_groupMenu->menuAction()->setVisible(allParametric);
    // ...and the two that only artwork has.
    const bool artwork = m_objects.selectionIsArtwork();
    if (m_actNaturalSize) m_actNaturalSize->setVisible(artwork);
    if (m_actFitToStrip)  m_actFitToStrip->setVisible(artwork);
    // A kind is something only an authored object has. One that is not — imported artwork, or a tail —
    // can reach nothing, and the intersection of "everything" with "nothing" is what greys this out.
    const bool authoredOnly =
        !picked.isEmpty() && pickedTails.isEmpty()
        && std::all_of(picked.cbegin(), picked.cend(), [this](const QString& uid) {
               return m_objects.isParametric(uid) && !m_objects.carriers().contains(uid);
           });
    if (m_convertMenu) m_convertMenu->menuAction()->setEnabled(authoredOnly);
}

void ObjectMenu::setColourSource(const ColourPair* pair)
{
    m_colours = pair;
    const bool has = pair != nullptr;
    if (m_actFill)    m_actFill->setVisible(has);
    if (m_actOutline) m_actOutline->setVisible(has);
}

void ObjectMenu::rebuildPresetMenu()
{
    m_presetMenu->clear();
    const QList<BubblePreset>& presets = m_presets.presets();
    for (int i = 0; i < presets.size(); ++i) {
        QAction* a = m_presetMenu->addAction(presets.at(i).name);
        connect(a, &QAction::triggered, this, [this, i] { m_objects.applyPresetToSelection(i); });
    }
}

void ObjectMenu::rebuildReanchorMenu()
{
    m_reanchorMenu->clear();

    const auto it = std::find_if(m_objects.overlays().cbegin(), m_objects.overlays().cend(),
                                 [this](const Platemaker::Models::StripOverlay& o) {
                                     return QString::fromStdString(o.uid) == m_objects.selectedOverlay();
                                 });
    if (it == m_objects.overlays().cend())
        return;

    // Listed, never pre-chosen. The only thing a guess could go on is position, and position is exactly
    // what a deletion shifts: page 5 becomes the fourth page, and a guess puts one page's lettering on
    // another page's art. Named the way the object list names pages, so the two can be read together.
    const int current = m_layout.pageForAnchor(QString::fromStdString(it->anchorInputUid));
    for (int i = 0; i < m_layout.pageCount(); ++i) {
        const Page& page = m_layout.page(i);
        QAction* a = m_reanchorMenu->addAction(tr("p.%1 — %2")
                                                   .arg(i + 1, 2, 10, QLatin1Char('0'))
                                                   .arg(QFileInfo(page.sourcePath).fileName()));
        a->setCheckable(true);
        a->setChecked(i == current);
        a->setEnabled(i != current);
        connect(a, &QAction::triggered, this, [this, uid = page.inputUid] { m_objects.reanchorSelection(uid); });
    }
}

void ObjectMenu::saveSelectionAsPreset()
{
    const Object* bubble = m_objects.object(m_objects.selectedOverlay());
    if (!bubble || bubble->record().isArtwork())
        return;   // imported artwork has no look to save

    bool          ok   = false;
    const QString name = QInputDialog::getText(m_dialogParent, tr("Save preset"), tr("Preset name:"),
                                               QLineEdit::Normal, bubble->record().text.body.left(24),
                                               &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;

    // The store decides what a preset carries — the look, never the lettering — so this hands over the
    // whole record and lets the one function that knows the answer do the cutting.
    int existing = -1;
    if (m_presets.save(name, bubble->record(), /*replaceExisting=*/false, &existing) < 0) {
        if (QMessageBox::question(m_dialogParent, tr("Save preset"),
                                  tr("A preset named “%1” already exists. Replace it?").arg(name))
            != QMessageBox::Yes)
            return;
        m_presets.save(name, bubble->record(), /*replaceExisting=*/true);
    }
}

void ObjectMenu::importArtwork()
{
    if (m_layout.isEmpty()) {
        QMessageBox::information(m_dialogParent, tr("Import artwork"),
                                 tr("Add input pages to the project first — artwork is anchored to a "
                                    "page, so there has to be one to put it on."));
        return;
    }

    const QString file = QFileDialog::getOpenFileName(
        m_dialogParent, tr("Import artwork"), QString(),
        tr("Artwork (*.svg *.png *.webp);;All files (*)"));
    if (file.isEmpty())
        return;

    m_objects.importArtworkFile(file);
}

}  // namespace StripEdit
