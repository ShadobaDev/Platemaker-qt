#include "objectcontroller.h"
#include "objectstatepanel.h"
#include "presetstore.h"
#include "tooloptionspanel.h"
#include "layout.h"
#include "assetobject.h"
#include "bubbleobject.h"
#include "object.h"
#include "artifactpainter.h"
#include "artifactsvg.h"

#include <QFileInfo>
#include <platemaker/core/strip_overlay_compositor/strip_overlay_compositor.hpp>

#include <QAbstractItemModel>
#include <QAction>
#include <QMenu>
#include <QCryptographicHash>
#include <QFileDialog>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGuiApplication>

#include <optional>
#include <QGraphicsView>
#include <QKeySequence>
#include <QTreeWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QSet>
#include <QSvgRenderer>
#include <QTimer>
#include <QTransform>
#include <QWidget>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

namespace StripEdit {

namespace {

//! Shortest drag, in either axis, that counts as drawing a bubble rather than clicking on the strip.
constexpr int k_minPlacementDrag = 24;
//! Scene Z: the strip is 0, seam guides 1, overlays start here (in composite order).
constexpr int k_overlayZBase = 2;
//! How far a duplicate lands from its original, so it is visibly a second bubble and not a mis-click.
constexpr int k_duplicateOffset = 28;
//! Rasterised styled bubbles held before the cache is dropped wholesale. Generous for a chapter,
//! nothing next to the page caches above.
constexpr int k_sharpCacheEntries = 64;

/**
 * @brief Draws an overlay asset that carries no authoring parameters, at its own natural size.
 *
 * Vector assets go through QSvgRenderer explicitly rather than through QPixmap's image plugin: the
 * plugin path depends on qsvg being deployed and gives no control over the size it picks. Raster
 * assets still load the ordinary way, so a hand-supplied PNG keeps working.
 */
QPixmap renderAssetFile(const QString& path)
{
    if (path.isEmpty())
        return {};

    if (!path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive))
        return QPixmap(path);

    QSvgRenderer renderer(path);
    if (!renderer.isValid())
        return {};
    QSize size = renderer.defaultSize();
    if (size.isEmpty())
        return {};

    QImage img(size, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    renderer.render(&p);
    p.end();
    return QPixmap::fromImage(img);
}

} // namespace

ObjectController::ObjectController(QGraphicsScene* scene, QGraphicsView* view, QTreeWidget* list,
                                   ObjectStatePanel* panel, ToolOptionsPanel* defaults,
                                   PresetStore& presets, const Layout& layout,
                                   QWidget* dialogParent, QObject* parent)
    : QObject(parent)
    , m_scene(scene)
    , m_view(view)
    , m_list(list)
    , m_objectState(panel)
    , m_toolOptions(defaults)
    , m_presets(presets)
    , m_layout(layout)
    , m_dialogParent(dialogParent)
{
    connect(m_objectState, &ObjectStatePanel::changed, this,
            [this](const TextArtifact& a) { applyPanelArtifact(a, /*commit=*/false); });
    connect(m_objectState, &ObjectStatePanel::committed, this,
            [this](const TextArtifact& a) { applyPanelArtifact(a, /*commit=*/true); });
    connect(m_objectState, &ObjectStatePanel::deleteRequested, this, &ObjectController::deleteSelectedOverlay);
    connect(m_objectState, &ObjectStatePanel::changedMany, this,
            [this](const QList<TextArtifact>& objects) { applyPanelArtifacts(objects, /*commit=*/false); });
    connect(m_objectState, &ObjectStatePanel::committedMany, this,
            [this](const QList<TextArtifact>& objects) { applyPanelArtifacts(objects, /*commit=*/true); });
    connect(m_objectState, &ObjectStatePanel::fitRequested, this, [this] {
        auto* bubble = qobject_cast<BubbleObject*>(m_overlayItems.value(m_selectedOverlay));
        if (!bubble) return;   // only a bubble has text to fit to
        TextArtifact a = bubble->artifact();
        a.box = fittedBox(a);
        applyPanelArtifact(a, /*commit=*/true);
        m_objectState->setArtifact(a);
    });

    // --- artifact list (right-bottom): composite order, mute toggles, selection ---
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    // Extended: Ctrl adds, Shift takes a run — the two gestures every list in the application uses.
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setHeaderHidden(true);
    m_list->setColumnCount(1);
    // Branch decoration, because the strip nests its pages. Rows start collapsed; what the artist opens
    // stays open, since the rows are updated in place rather than rebuilt.
    m_list->setRootIsDecorated(true);

    // Duplicate / Delete as real QActions: Qt::ActionsContextMenu then builds the list's right-click
    // menu from them for free, and the same objects carry the keyboard shortcuts. They are added to
    // the canvas as well, so they work wherever the bubble was selected — but scoped per widget, so
    // Delete keeps deleting characters while the panel's text box has focus.
    m_actDuplicate = new QAction(tr("Duplicate"), this);
    m_actDuplicate->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    m_actDuplicate->setShortcutContext(Qt::WidgetShortcut);
    connect(m_actDuplicate, &QAction::triggered, this, &ObjectController::duplicateSelectedOverlay);

    m_presetMenu = new QMenu(tr("Apply preset"), dialogParent);
    connect(m_presetMenu, &QMenu::aboutToShow, this, &ObjectController::rebuildPresetMenu);

    m_reanchorMenu = new QMenu(tr("Re-anchor to"), dialogParent);
    connect(m_reanchorMenu, &QMenu::aboutToShow, this, &ObjectController::rebuildReanchorMenu);

    // Blend, at last reachable. Built once; which entry is ticked is decided when it opens, because that
    // is the only moment it can be true.
    m_blendMenu = new QMenu(tr("Blend"), dialogParent);
    using BlendMode = Platemaker::Models::BlendMode;
    const QList<QPair<BlendMode, QString>> blends{
        {BlendMode::Over,     tr("Normal")},
        {BlendMode::Multiply, tr("Multiply")},
        {BlendMode::Screen,   tr("Screen")},
        {BlendMode::Overlay,  tr("Overlay")},
        {BlendMode::Darken,   tr("Darken")},
        {BlendMode::Lighten,  tr("Lighten")},
    };
    for (const auto& [mode, name] : blends) {
        QAction* a = m_blendMenu->addAction(name);
        a->setCheckable(true);
        a->setData(static_cast<int>(mode));
        connect(a, &QAction::triggered, this, [this, mode] { setSelectionBlend(mode); });
    }
    connect(m_blendMenu, &QMenu::aboutToShow, this, [this] {
        // Ticked against the primary: with a set that disagrees, nothing is ticked, which is the same
        // answer ③ gives when it says Mixed.
        std::optional<Platemaker::Models::BlendMode> shared;
        bool agree = true;
        for (const auto& o : m_overlays) {
            const QString uid = QString::fromStdString(o.uid);
            if (!m_selectedOverlays.contains(uid) || m_carriers.contains(uid))
                continue;
            if (!shared)
                shared = o.blend;
            else if (*shared != o.blend)
                agree = false;
        }
        for (QAction* a : m_blendMenu->actions())
            a->setChecked(shared && agree && a->data().toInt() == static_cast<int>(*shared));
    });

    m_actForward = new QAction(tr("Bring forward"), this);
    connect(m_actForward, &QAction::triggered, this, [this] { moveSelectedInStack(true); });
    m_actBackward = new QAction(tr("Send back"), this);
    connect(m_actBackward, &QAction::triggered, this, [this] { moveSelectedInStack(false); });

    m_actDelete = new QAction(tr("Delete"), this);
    m_actDelete->setShortcut(QKeySequence::Delete);
    m_actDelete->setShortcutContext(Qt::WidgetShortcut);
    connect(m_actDelete, &QAction::triggered, this, &ObjectController::deleteSelectedOverlay);

    // Artwork drawn elsewhere — a balloon inked on a tablet, a logo — placed as an overlay like any
    // other. Always available, unlike Duplicate/Delete, because it needs no selection.
    m_actImport = new QAction(tr("Import artwork…"), this);
    connect(m_actImport, &QAction::triggered, this, &ObjectController::importArtwork);

    // The menu is the widgets' own action list (Qt::ActionsContextMenu), so the canvas and the tree
    // cannot drift apart and every entry keeps its shortcut. Sections are separators in that same list:
    // what the object *looks like*, then where it sits in the stack, then what happens to it as a whole.
    const auto separator = [this] {
        auto* a = new QAction(this);
        a->setSeparator(true);
        return a;
    };
    for (QWidget* w : {static_cast<QWidget*>(m_list), static_cast<QWidget*>(m_view)}) {
        w->addAction(m_presetMenu->menuAction());
        w->addAction(m_blendMenu->menuAction());
        w->addAction(separator());
        w->addAction(m_actForward);
        w->addAction(m_actBackward);
        w->addAction(separator());
        w->addAction(m_reanchorMenu->menuAction());
        w->addAction(m_actDuplicate);
        w->addAction(m_actDelete);
        w->addAction(separator());
        w->addAction(m_actImport);
    }
    // Both widgets, not just the list. The actions were added to the canvas from the start — which is why
    // the shortcuts worked there — but without this the canvas had nothing to build a menu *from*, so a
    // right-click on the strip did nothing at all.
    m_list->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_view->setContextMenuPolicy(Qt::ActionsContextMenu);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, [this] {
        // A drag moves a row by taking it out and putting it back, and the taking clears its selection.
        // That is the tree's mechanics, not the artist deselecting, so the selection stands until the
        // move has been committed and re-shown.
        if (m_syncingList || m_rowsMoving) return;
        const auto sel = m_list->selectedItems();
        if (sel.isEmpty()) {
            selectOverlay(QString());
            return;
        }
        // The strip and a page are one of a kind: picking one collapses whatever set was there. Objects
        // and tails gather together — they share a position, which is what a drag acts on.
        QStringList    overlays;
        QList<TailRef> tails;
        for (const QTreeWidgetItem* row : sel) {
            const QString id = row->data(0, Qt::UserRole).toString();
            switch (static_cast<Subject>(row->data(0, k_kindRole).toInt())) {
            case Subject::Strip: selectStrip();  return;
            case Subject::Page:  selectPage(id); return;
            case Subject::Tail:  tails.append(TailRef{id, row->data(0, k_tailRole).toInt()}); break;
            default:             overlays.append(id); break;
            }
        }
        selectSubjects(overlays, tails);
    });
    // Both list handlers are deferred to the next event-loop turn on purpose. Persisting an edit
    // round-trips through the owner and comes back as a re-feed that clears and refills this list —
    // which cannot safely happen inside the list's own itemChanged / rowsMoved emission.
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* row, int) {
        if (m_syncingList || !row) return;
        const QString uid = row->data(0, Qt::UserRole).toString();
        const bool    on  = row->checkState(0) == Qt::Checked;
        QTimer::singleShot(0, this, [this, uid, on] { setOverlayEnabled(uid, on); });
    });
    // Dragging a row changes the composite order, which is what the render draws bottom-to-top. Listened
    // for as both a move and an insert: a list reports a drag as a move, a tree as a removal followed by
    // an insertion. Which one arrives is Qt's business, so neither is relied on; either restarts one
    // timer, and the commit it fires compares the rows with the model and does nothing if they agree.
    m_orderCommit = new QTimer(this);
    m_orderCommit->setSingleShot(true);
    m_orderCommit->setInterval(0);
    connect(m_orderCommit, &QTimer::timeout, this, &ObjectController::commitListOrder);
    const auto orderMayHaveChanged = [this] {
        if (!m_syncingList)
            m_orderCommit->start();
    };
    connect(m_list->model(), &QAbstractItemModel::rowsMoved,    this, orderMayHaveChanged);
    connect(m_list->model(), &QAbstractItemModel::rowsInserted, this, orderMayHaveChanged);
    // Outside a refresh, nothing but a drag removes a row — deleting an object goes through the model
    // and comes back as a refresh.
    connect(m_list->model(), &QAbstractItemModel::rowsAboutToBeRemoved, this, [this] {
        if (m_syncingList)
            return;
        m_rowsMoving = true;
        m_orderCommit->start();   // so the flag is cleared even if no insertion ever follows
    });

    // The scene is the other half of the selection: clicking a bubble on the strip drives the list
    // and the panel, and vice versa.
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
        if (m_syncingList) return;
        // The scene has always multi-selected — items are selectable, so Ctrl+click adds to it. This used
        // to take the first object and drop the rest, which is why the canvas could only ever hold one.
        QStringList picked;
        for (QGraphicsItem* gi : m_scene->selectedItems())
            if (auto* obj = dynamic_cast<Object*>(gi))
                picked.append(obj->uid());
        selectOverlays(picked);
    });
}

PointerTarget ObjectController::pointerTargetAt(const QPointF&     scenePos,
                                                const QTransform& deviceTransform) const
{
    auto* obj = dynamic_cast<Object*>(m_scene->itemAt(scenePos, deviceTransform));
    if (!obj)
        return PointerTarget::BareStrip;
    if (obj->isOrphaned())
        return PointerTarget::Orphan;

    switch (obj->gripAtScene(scenePos)) {
    case Object::Grip::TopLeft:
    case Object::Grip::BottomRight: return PointerTarget::ResizeFDiag;
    case Object::Grip::TopRight:
    case Object::Grip::BottomLeft:  return PointerTarget::ResizeBDiag;
    case Object::Grip::Handle:      return PointerTarget::TailHandle;
    case Object::Grip::Body:
    case Object::Grip::None:        break;
    }
    return PointerTarget::Object;
}

bool ObjectController::objectAt(const QPointF& scenePos, const QTransform& deviceTransform) const
{
    return dynamic_cast<Object*>(m_scene->itemAt(scenePos, deviceTransform)) != nullptr;
}

void ObjectController::reselect()
{
    if (m_subject == Subject::Tail) {
        selectTail(m_selectedOverlay, m_selectedTail);   // falls back to the bubble if the tail is gone
        return;
    }
    if (m_subject == Subject::Strip || m_subject == Subject::Page) {
        // A page can go between feeds — an input removed — and a selection must not outlive its row.
        if (subjectRow())
            selectSubject(m_subject, m_selectedPage);
        else
            selectOverlay(QString());
        return;
    }
    if (!m_selectedOverlays.isEmpty())
        selectSubjects(m_selectedOverlays, m_selectedTails);   // both kinds; whatever is gone drops out
}

void ObjectController::selectStrip()
{
    selectSubject(Subject::Strip, QString());
}

void ObjectController::selectPage(const QString& inputUid)
{
    selectSubject(Subject::Page, inputUid);
}

void ObjectController::selectTail(const QString& uid, int index)
{
    const auto* bubble = qobject_cast<BubbleObject*>(m_overlayItems.value(uid));
    if (!bubble || index < 0 || index >= bubble->artifact().tails.items.size()) {
        selectOverlay(uid);   // nothing at that position: the bubble is what is left to hold
        return;
    }
    selectSubjects({uid}, {TailRef{uid, index}});
}


QTreeWidgetItem* ObjectController::tailRow(const QString& uid, int index) const
{
    for (int r = 0; r < m_list->topLevelItemCount(); ++r) {
        QTreeWidgetItem* top = m_list->topLevelItem(r);
        if (top->data(0, k_kindRole).toInt() == static_cast<int>(Subject::Overlay)
            && top->data(0, Qt::UserRole).toString() == uid)
            return (index >= 0 && index < top->childCount()) ? top->child(index) : nullptr;
    }
    return nullptr;
}

void ObjectController::onObjectPressed(const QString& uid, int handle)
{
    const bool add = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;

    if (handle >= 0) {
        if (add) {
            // Ctrl gathers, here as everywhere else. The scene has already toggled the balloon's own
            // selection by the time this arrives; re-stating the whole selection puts that right.
            QList<TailRef> tails = m_selectedTails;
            const TailRef  t{uid, handle};
            if (tails.contains(t))
                tails.removeAll(t);
            else
                tails.append(t);
            QStringList objects = m_selectedOverlays;
            objects.removeAll(uid);   // the balloon comes back as the tail's carrier, not as a subject
            selectSubjects(objects, tails);
        } else {
            selectTail(uid, handle);
        }
        beginDrag(uid, handle);
        return;
    }
    // A press on the balloon itself while one of its tails is selected is a press on the balloon: the
    // scene reports no change, because the balloon was selected all along.
    if (m_subject == Subject::Tail && uid == m_selectedOverlay && !add)
        selectOverlay(uid);
    beginDrag(uid, handle);
}

void ObjectController::beginDrag(const QString& uid, int handle)
{
    // Is this drag the selection's, or this object's alone? Grabbing a body carries the selection when
    // that body is in it; grabbing a tail carries it when *that tail* is — a balloon being selected does
    // not make aiming one of its tails a group gesture.
    const int subjects = static_cast<int>(m_selectedOverlays.size() + m_selectedTails.size());
    m_dragIsGroup = subjects > 1
                 && (handle >= 0 ? m_selectedTails.contains(TailRef{uid, handle})
                                 : m_selectedOverlays.contains(uid));

    m_dragStartPos.clear();
    m_dragStartTips.clear();
    if (!m_dragIsGroup)
        return;

    // Everything is placed from *its own* start plus the drag's delta, never nudged per mouse-move, so a
    // fast drag cannot make the formation drift apart.
    for (const QString& u : std::as_const(m_selectedOverlays)) {
        if (Object* item = m_overlayItems.value(u))
            m_dragStartPos.insert(u, item->pos());
    }
    for (const TailRef& t : std::as_const(m_selectedTails)) {
        if (Object* item = m_overlayItems.value(t.uid))
            m_dragStartTips.append({t, item->handleAt(t.index)});
    }
}

void ObjectController::onObjectDragged(const QString& uid, const QPointF& delta, int handle)
{
    if (!m_dragIsGroup)
        return;   // one object moving itself, which it has already done

    for (auto it = m_dragStartPos.cbegin(); it != m_dragStartPos.cend(); ++it) {
        if (it.key() == uid)
            continue;   // the one under the mouse placed itself
        if (Object* item = m_overlayItems.value(it.key()))
            item->setPos(it.value() + delta);
    }

    for (const auto& [tail, start] : std::as_const(m_dragStartTips)) {
        if (tail.uid == uid && tail.index == handle)
            continue;   // likewise the tail being aimed
        Object* item = m_overlayItems.value(tail.uid);
        if (!item)
            continue;
        // A tip lives in the balloon's own units, and the item may be drawn at a scale, so the scene
        // delta has to be divided back out before it means anything to a tail.
        const qreal k = item->scale() > 0.0 ? item->scale() : 1.0;
        item->moveHandle(tail.index, start + delta / k);
    }
}

void ObjectController::selectSubject(Subject subject, const QString& pageUid)
{
    // Everything overlay-shaped let go of first, through the one function that already knows how: the
    // scene, the tree, the actions that need an object, and the object panel.
    selectOverlay(QString());

    m_subject      = subject;
    m_selectedPage = subject == Subject::Page ? pageUid : QString();

    m_syncingList = true;
    if (QTreeWidgetItem* row = subjectRow())
        row->setSelected(true);
    m_syncingList = false;

    emit subjectChanged(m_subject, m_selectedPage);
}

QTreeWidgetItem* ObjectController::subjectRow() const
{
    for (int r = 0; r < m_list->topLevelItemCount(); ++r) {
        QTreeWidgetItem* top = m_list->topLevelItem(r);
        if (top->data(0, k_kindRole).toInt() != static_cast<int>(Subject::Strip))
            continue;
        if (m_subject == Subject::Strip)
            return top;
        for (int c = 0; c < top->childCount(); ++c)
            if (top->child(c)->data(0, Qt::UserRole).toString() == m_selectedPage)
                return top->child(c);
    }
    return nullptr;
}

void ObjectController::setExcludedPages(const QSet<QString>& inputUids)
{
    if (inputUids == m_excludedPages)
        return;
    m_excludedPages = inputUids;
    refreshList();
}

qreal ObjectController::itemScaleFor(const Platemaker::Models::StripOverlay& o, qreal naturalWidth) const
{
    // wFrac 0 means "the asset's own size", which is exactly a scale of 1 — and it is what an overlay
    // placed before sizes were recorded, or imported and never resized, carries.
    if (o.wFrac <= 0.0 || naturalWidth <= 0.0)
        return 1.0;
    return m_layout.pixels(o.wFrac) / naturalWidth;
}

void ObjectController::setSource(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                                 const ArtifactMap&                                  artifacts)
{
    m_overlays  = overlays;
    m_artifacts = artifacts;
    syncItems();
    refreshList();

    // A bubble we just asked for has arrived with its minted uid. addOverlay() appends, so it is the
    // last one — select it and put the caret in the text box, because the next thing anyone wants to do
    // with a bubble they just drew is type in it.
    if (m_selectNewOverlay && !m_overlays.empty()) {
        m_selectNewOverlay = false;
        selectOverlay(QString::fromStdString(m_overlays.back().uid));
        if (m_objectState)
            m_objectState->focusText();
        return;
    }
    m_selectNewOverlay = false;

    // A history step brought this feed, and these are the objects it touched. Selecting one is how the
    // artist sees *what* was undone; raising its dock only says where to look. The first that survived
    // wins — a step that removed everything it touched leaves nothing to point at.
    if (!m_selectAfterFeed.isEmpty()) {
        QStringList touched;
        touched.swap(m_selectAfterFeed);   // one-shot: consumed by this feed and no other
        for (const QString& uid : touched) {
            if (!m_overlayItems.contains(uid))
                continue;
            selectOverlay(uid);
            // A long strip is exactly where an undone edit sits off-screen, so bring it into view in
            // both places the selection shows. ensureVisible() and scrollToItem() do nothing when the
            // target is already visible, which keeps an undo of what you are looking at perfectly still.
            m_view->ensureVisible(m_overlayItems.value(uid));
            const QList<QTreeWidgetItem*> rows = m_list->selectedItems();
            if (!rows.isEmpty())
                m_list->scrollToItem(rows.first());
            return;
        }
        selectOverlay(QString());
        return;
    }

    // The selection may not have survived the edit (a delete, or an undo that removed it). Re-applying
    // the set drops whatever is gone and keeps the rest, because selectOverlays() filters by what exists.
    bool gone = false;
    for (const QString& uid : std::as_const(m_selectedOverlays))
        gone = gone || !m_overlayItems.contains(uid);
    if (gone) {
        selectSubjects(m_selectedOverlays, m_selectedTails);
        if (m_selectedOverlays.isEmpty())
            return;
    }

    // ponytail: tails are addressed by position, not by id. A feed that changes how many tails a balloon
    // has — an undo, a preset — cannot say which one went, so a tail selection survives it only by moving
    // up to its balloon. A per-tail id would let the selection follow its tail; nothing needs that yet.
    if (m_subject == Subject::Tail) {
        const auto* bubble = qobject_cast<BubbleObject*>(m_overlayItems.value(m_selectedOverlay));
        if (!bubble || bubble->artifact().tails.items.size() != m_selectedTailCount)
            selectOverlay(m_selectedOverlay);
    }
}

QImage ObjectController::sharpRasterFor(const TextArtifact& a)
{
    const QByteArray svg = artifactToSvg(a);
    if (svg.isEmpty())
        return {};

    // Keyed by the document, so the entry cannot outlive what produced it.
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(svg, QCryptographicHash::Sha256).toHex());
    if (const auto it = m_sharpCache.constFind(key); it != m_sharpCache.constEnd())
        return it.value();

    // The render's own rasteriser, at strip scale — the scene is 1:1 with the strip, so what appears
    // here is what the committed slice will carry. From the bytes, not the path: see m_sharpCache.
    const auto raster = Platemaker::Core::StripOverlayCompositor{}.rasterizeSvgRgba(svg.toStdString(), 1.0);
    QImage img;
    if (raster.isValid()) {
        // copy(): the QImage would otherwise reference the vector's buffer, which dies with the call.
        img = QImage(raster.rgba.data(), raster.width, raster.height,
                     raster.width * 4, QImage::Format_RGBA8888).copy();
    }
    // Every settled edit mints a new hash, so a long lettering session would otherwise accumulate one
    // rasterisation per revision. Nothing here is precious — a miss costs one librsvg render.
    if (m_sharpCache.size() > k_sharpCacheEntries)
        m_sharpCache.clear();
    m_sharpCache.insert(key, img);   // cache the failure too, so a broken bubble is not retried per repaint
    return img;
}

void ObjectController::syncItems()
{
    if (!m_scene)
        return;

    // Drop items whose overlay is gone.
    QSet<QString> live;
    for (const auto& o : m_overlays)
        live.insert(QString::fromStdString(o.uid));
    for (auto it = m_overlayItems.begin(); it != m_overlayItems.end(); ) {
        if (live.contains(it.key())) { ++it; continue; }
        m_scene->removeItem(it.value());
        delete it.value();
        it = m_overlayItems.erase(it);
    }

    if (m_layout.isEmpty())
        return;   // no strip laid out yet — there is nothing to place an overlay against

    int z = k_overlayZBase;
    for (const auto& o : m_overlays) {
        const QString uid  = QString::fromStdString(o.uid);
        const int     page = m_layout.pageForAnchor(QString::fromStdString(o.anchorInputUid));
        const bool    orphaned = page < 0 && !o.anchorInputUid.empty();

        // **The one place the two kinds are told apart**, and it is a choice of constructor rather than
        // a test repeated downstream. An overlay whose asset carries no authoring parameters — art drawn
        // elsewhere, or a file edited outside Platemaker — becomes an AssetObject: it still shows, still
        // moves and still renders, it just cannot be re-typed. That is the intended degradation, and it
        // is what makes imported artwork a first-class object rather than an error.
        const bool parametric = m_artifacts.contains(uid);
        Object*    item       = m_overlayItems.value(uid);

        // An overlay cannot change kind in place; if it somehow has, rebuild rather than mis-draw it.
        if (item && (item->kind() == Object::Kind::Bubble) != parametric) {
            m_scene->removeItem(item);
            delete item;
            m_overlayItems.remove(uid);
            item = nullptr;
        }
        if (!item) {
            item = parametric
                ? static_cast<Object*>(new BubbleObject(uid, m_artifacts.value(uid)))
                : static_cast<Object*>(new AssetObject(uid, renderAssetFile(QString::fromStdString(o.assetPath))));
            connect(item, &Object::geometryEdited, this, &ObjectController::onOverlayGeometryEdited);
            connect(item, &Object::pressed,        this, &ObjectController::onObjectPressed);
            connect(item, &Object::dragging,       this, &ObjectController::onObjectDragged);
            m_scene->addItem(item);
            m_overlayItems.insert(uid, item);
        }

        if (auto* bubble = qobject_cast<BubbleObject*>(item)) {
            const TextArtifact& a = m_artifacts.value(uid);
            if (bubble->artifact() != a)
                bubble->setArtifact(a);
            // A styled bubble is drawn by the library, because its effect is an SVG filter Qt cannot
            // render. Unstyled ones keep drawing locally: same geometry, no round-trip.
            bubble->setSharpRaster(a.style.kind != TextArtifact::Style::Clean ? sharpRasterFor(a) : QImage());
        }

        item->setBlend(o.blend);
        item->setOrphaned(orphaned);
        // The record says how wide the object is relative to the page; the item draws its own artwork at
        // that artwork's own size. The ratio is the item's scale — 1.0 for everything authored at the
        // width the strip is laid out at now, which is every overlay until a chapter is re-profiled.
        const qreal k = itemScaleFor(o, item->contentBounds().width());
        item->setScale(k);
        // The record stores the artwork's top-left; the item is positioned by its balloon's. They differ
        // by the bounds offset whenever a tail reaches above or left of the balloon — and that offset is
        // in the item's own units, so it scales with it.
        item->setPos(m_layout.scenePosOf(o) - item->contentBounds().topLeft() * k);
        item->setVisible(o.enabled);
        item->setZValue(z++);
        // Selectable under **every** tool. It used to depend on an authoring tool being active, which
        // made the object panel depend on it too — and left an object that could be dragged but not
        // selected, because moving it never asked. An unanchored object is the one exception: it is
        // not on the strip, so there is nothing to select it *on*.
        item->setFlag(QGraphicsItem::ItemIsSelectable, !orphaned);
    }
}

void ObjectController::writePlacement(const QString& uid)
{
    Object* item = m_overlayItems.value(uid);
    const double tw = m_layout.targetWidth();
    if (!item || tw <= 0)
        return;   // no strip laid out; there is nothing to measure a fraction against

    // Re-anchor to whichever page the bubble now sits on. Crossing a page boundary is a normal drag,
    // and silently re-homing it is the whole point: the offset stays relative to the artwork under it.
    // Back to the artwork's own top-left, which is what the library composites at — in scene pixels,
    // so the item's own scale applies to the offset as well as to the artwork.
    const qreal   k    = item->scale();
    const QPointF p    = item->pos() + item->contentBounds().topLeft() * k;
    const int     page = m_layout.pageAtSceneY(item->pos().y());
    for (auto& o : m_overlays) {
        if (QString::fromStdString(o.uid) != uid)
            continue;
        // Back into the durable unit. Dividing by the target width here is the *only* place a placement
        // leaves the editor, which is what keeps the stored form independent of the profile in use.
        o.xFrac = p.x() / tw;
        o.wFrac = item->contentBounds().width() * k / tw;
        if (page >= 0) {
            o.anchorInputUid = m_layout.anchorUidForPage(page).toStdString();
            o.yFrac          = (p.y() - m_layout.page(page).top) / tw;
        } else {
            o.yFrac = p.y() / tw;
        }
        break;
    }
}

void ObjectController::onOverlayGeometryEdited(const QString& uid)
{
    Object* item = m_overlayItems.value(uid);
    if (!item)
        return;

    // A drag carries the whole selection (position is the one role everything has), so the record of
    // every object that travelled is rewritten — and the lot becomes **one** history step, because one
    // drag is one thing the artist did. A tail that travelled changes its balloon's extent, so its
    // balloon's placement is rewritten too, and its artifact re-read below.
    const bool        group = m_dragIsGroup && item->reportedDrag();
    const QStringList moved = group ? m_selectedOverlays : QStringList{uid};
    for (const QString& u : moved)
        writePlacement(u);
    if (group) {
        for (const TailRef& t : std::as_const(m_selectedTails)) {
            if (auto* bubble = qobject_cast<BubbleObject*>(m_overlayItems.value(t.uid)))
                m_artifacts.insert(t.uid, bubble->artifact());
        }
    }
    const int travelled = group ? selectedSubjectCount() : 1;
    m_dragIsGroup = false;

    // A resize or tail drag changed the artifact too — and only a bubble has one. This used to test
    // the model for an authoring record and was written wrong once, storing a *default* bubble over
    // imported artwork; asking the object what it is cannot go wrong the same way.
    if (auto* bubble = qobject_cast<BubbleObject*>(item)) {
        m_artifacts.insert(uid, bubble->artifact());
        // Only when the panel is about this one object. A set of several — or one of two kinds — is
        // already showing what it should, and rebinding it to the thing that happened to move would be
        // the panel changing subject on its own.
        if (uid == m_selectedOverlay && m_objectState && m_selectedOverlays.size() == 1) {
            // A tail's drag leaves that tail selected, so the panel shows the tail again, not the balloon.
            if (m_subject == Subject::Tail)
                m_objectState->setTail(bubble->artifact(), m_selectedTail);
            else if (m_selectedTails.isEmpty())
                m_objectState->setArtifact(bubble->artifact());
        }
    }
    refreshList();
    pushOverlays(group ? tr("Move %n objects", "", travelled) : tr("Move bubble"));
}

void ObjectController::refreshList()
{
    if (!m_list)
        return;

    m_syncingList = true;

    // **Updated in place, never cleared and refilled.** The tree is where an object is picked out
    // precisely, and every edit comes back to this controller as a feed — so a tree rebuilt on each feed
    // would lose what the artist had opened, selected or scrolled to at exactly the moment they were
    // using it. Rows are matched by id — an overlay's uid, the strip's fixed id, a page's input uid — and
    // only what changed is touched.
    QHash<QString, QTreeWidgetItem*> unused;
    for (int r = 0; r < m_list->topLevelItemCount(); ++r) {
        QTreeWidgetItem* row = m_list->topLevelItem(r);
        unused.insert(row->data(0, Qt::UserRole).toString(), row);
    }

    // Puts @p row at top-level position @p index, moving it only if it is not already there. A move is a
    // take and an insert, and the view forgets whether a taken row was expanded — so that is carried
    // across by hand, or every reorder would fold up whatever the artist had opened.
    const auto placeTopLevel = [this](QTreeWidgetItem* row, int index) {
        const int at = m_list->indexOfTopLevelItem(row);
        if (at == index)
            return;
        const bool open = at >= 0 && row->isExpanded();
        if (at >= 0)
            m_list->takeTopLevelItem(at);
        m_list->insertTopLevelItem(index, row);
        row->setExpanded(open);
    };

    // The tree is a **stack**: row 0 is the front-most object, and a row covers every row below it
    // wherever they overlap. The render draws m_overlays in vector order, so the *last* element is the
    // one on top — which makes the rows that vector reversed. Reversing here rather than in the model
    // keeps the library's "composite order == vector order" rule intact and costs one iterator.
    int index = 0;
    for (auto rit = m_overlays.rbegin(); rit != m_overlays.rend(); ++rit, ++index) {
        const auto&   o    = *rit;
        const QString uid  = QString::fromStdString(o.uid);
        const int     page = m_layout.pageForAnchor(QString::fromStdString(o.anchorInputUid));

        // The object names itself. The fallback covers the one moment there is no object to ask: rows
        // refreshed before the strip has a layout, where syncItems() has nothing to place anything against.
        const Object* item  = m_overlayItems.value(uid);
        const QString label = item ? item->label()
                                   : (m_artifacts.contains(uid) ? artifactLabel(m_artifacts.value(uid))
                                                                : tr("(imported artwork)"));
        const QString prefix = (page >= 0) ? tr("p.%1").arg(page + 1, 2, 10, QLatin1Char('0'))
                                           : tr("orphan");

        QTreeWidgetItem* row = unused.take(uid);
        if (!row) {
            row = new QTreeWidgetItem;
            row->setData(0, Qt::UserRole, uid);
            row->setData(0, k_kindRole, static_cast<int>(Subject::Overlay));
            // Drops land between rows and never onto one. Nesting is structural — a tail belongs to its
            // balloon — and is not something a drag may create.
            row->setFlags((row->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsDropEnabled);
        }
        placeTopLevel(row, index);

        row->setText(0, QStringLiteral("%1 · %2").arg(prefix, label));
        row->setCheckState(0, o.enabled ? Qt::Checked : Qt::Unchecked);
        // Both set on every pass, not only when unanchored: a row is reused, and one that was greyed must
        // stop being greyed the moment its object is re-anchored.
        if (page < 0) {
            row->setForeground(0, m_dialogParent->palette().brush(QPalette::Disabled, QPalette::WindowText));
            row->setToolTip(0, tr("The page this overlay was anchored to is not in the strip. It is kept, "
                                  "and comes back when its page does — the render skips it meanwhile."));
        } else {
            row->setData(0, Qt::ForegroundRole, QVariant());
            row->setToolTip(0, QString());
        }
        row->setSelected(m_subject == Subject::Overlay && m_selectedOverlays.contains(uid));

        // A bubble's tails, one row each, by position: a row stands for whichever tail is at its index now.
        // Only a bubble has tails, so artwork gets none.
        const int tailCount = m_artifacts.contains(uid)
                                  ? static_cast<int>(m_artifacts.value(uid).tails.items.size()) : 0;
        while (row->childCount() > tailCount)
            delete row->takeChild(row->childCount() - 1);
        for (int t = 0; t < tailCount; ++t) {
            QTreeWidgetItem* tailItem = t < row->childCount() ? row->child(t) : nullptr;
            if (!tailItem) {
                tailItem = new QTreeWidgetItem(row);
                tailItem->setData(0, k_kindRole, static_cast<int>(Subject::Tail));
                tailItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);   // not draggable, not a drop target
            }
            tailItem->setData(0, Qt::UserRole, uid);
            tailItem->setData(0, k_tailRole, t);
            tailItem->setText(0, tr("Tail %1").arg(t + 1));
            tailItem->setSelected(m_selectedTails.contains(TailRef{uid, t}));
        }
    }

    // The strip, pinned last — under every object, because it is what they all sit on. It is not an
    // overlay: nothing can drag it, nothing can be dropped on it, and it has no mute, because a strip
    // that could be hidden would no longer show what renders (Q62). Absent until there is a layout.
    // Rows for overlays that are gone go *before* the strip is placed. Left in until the end they would
    // still occupy positions above it, and the strip would be moved — taken and re-inserted — on every
    // deletion, when all that had changed was a row above it disappearing.
    QTreeWidgetItem* strip = unused.take(k_stripId);
    qDeleteAll(unused);
    unused.clear();
    if (m_layout.isEmpty()) {
        delete strip;
        strip = nullptr;
    }

    if (!m_layout.isEmpty()) {
        if (!strip) {
            strip = new QTreeWidgetItem;
            strip->setData(0, Qt::UserRole, k_stripId);
            strip->setData(0, k_kindRole, static_cast<int>(Subject::Strip));
            strip->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        }
        placeTopLevel(strip, index);
        strip->setText(0, tr("Strip · %n page(s)", "", m_layout.pageCount()));

        // Its pages, in strip order, reconciled the same way — by input uid, in place, so a page the
        // artist had selected is still selected after an edit that did not remove it.
        QHash<QString, QTreeWidgetItem*> oldPages;
        for (int c = 0; c < strip->childCount(); ++c)
            oldPages.insert(strip->child(c)->data(0, Qt::UserRole).toString(), strip->child(c));

        for (int i = 0; i < m_layout.pageCount(); ++i) {
            const Page& page = m_layout.page(i);
            QTreeWidgetItem* row = oldPages.take(page.inputUid);
            if (!row) {
                row = new QTreeWidgetItem;
                row->setData(0, Qt::UserRole, page.inputUid);
                row->setData(0, k_kindRole, static_cast<int>(Subject::Page));
                row->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);   // not draggable, not a drop target
            }
            const int at = strip->indexOfChild(row);
            if (at != i) {
                if (at >= 0)
                    strip->takeChild(at);
                strip->insertChild(i, row);
            }
            const QString name = tr("p.%1 — %2").arg(i + 1, 2, 10, QLatin1Char('0'))
                                                .arg(QFileInfo(page.sourcePath).fileName());
            row->setText(0, m_excludedPages.contains(page.inputUid) ? tr("%1 · excluded").arg(name) : name);
            row->setSelected(m_subject == Subject::Page && page.inputUid == m_selectedPage);
        }
        qDeleteAll(oldPages);   // pages no longer in the strip
        strip->setSelected(m_subject == Subject::Strip);
    }

    m_syncingList = false;
}



void ObjectController::selectOverlay(const QString& uid)
{
    selectOverlays(uid.isEmpty() ? QStringList{} : QStringList{uid});
}

void ObjectController::selectOverlays(const QStringList& uids)
{
    selectSubjects(uids, {});
}

int ObjectController::selectedSubjectCount() const
{
    return static_cast<int>(m_selectedOverlays.size() - m_carriers.size() + m_selectedTails.size());
}

void ObjectController::selectSubjects(const QStringList& uids, const QList<TailRef>& tails)
{
    QStringList picked;
    for (const QString& uid : uids) {
        if (m_overlayItems.contains(uid) && !picked.contains(uid))
            picked.append(uid);
    }

    // A tail outlives neither its balloon nor a change in how many tails that balloon has.
    QList<TailRef> pickedTails;
    QStringList    carriers;
    for (const TailRef& t : tails) {
        const auto* bubble = qobject_cast<BubbleObject*>(m_overlayItems.value(t.uid));
        if (bubble && t.index >= 0 && t.index < bubble->artifact().tails.items.size()
            && !pickedTails.contains(t)) {
            pickedTails.append(t);
            if (!picked.contains(t.uid)) {
                // Its balloon carries the handles a tail is aimed by, so it has to be selected on the
                // canvas — but it is not a subject. Nobody picked it, so it is not counted, not deleted,
                // and its own row stays unhighlighted.
                picked.append(t.uid);
                carriers.append(t.uid);
            }
        }
    }

    m_selectedOverlays = picked;
    m_selectedTails    = pickedTails;
    m_carriers         = carriers;
    m_selectedOverlay  = picked.isEmpty() ? QString() : picked.last();
    m_selectedPage.clear();

    // One tail, on its own balloon, is the subject T5c built: the handle drawn hollow and ③ showing that
    // tail. Anything else with a tail in it is a set whose only common property is where it sits.
    const bool singleTail = pickedTails.size() == 1 && picked.size() == 1
                         && picked.first() == pickedTails.first().uid;
    const auto* primary = qobject_cast<BubbleObject*>(m_overlayItems.value(m_selectedOverlay));
    m_selectedTail      = singleTail ? pickedTails.first().index : -1;
    m_selectedTailCount = primary ? static_cast<int>(primary->artifact().tails.items.size()) : 0;
    m_subject           = picked.isEmpty() ? Subject::None
                        : singleTail       ? Subject::Tail
                                           : Subject::Overlay;

    m_syncingList = true;
    for (auto it = m_overlayItems.begin(); it != m_overlayItems.end(); ++it) {
        it.value()->setSelected(picked.contains(it.key()));
        it.value()->setFocusedHandle(-1);
    }
    // ponytail: one focused handle per balloon. Select two tails of the same balloon and only the last
    // is drawn hollow; both still move. Per-handle marking would need the chrome to carry a set.
    for (const TailRef& t : pickedTails) {
        if (Object* item = m_overlayItems.value(t.uid))
            item->setFocusedHandle(t.index);
    }

    m_list->clearSelection();
    for (int r = 0; r < m_list->topLevelItemCount(); ++r) {
        QTreeWidgetItem* row = m_list->topLevelItem(r);
        if (row->data(0, k_kindRole).toInt() != static_cast<int>(Subject::Overlay))
            continue;
        const QString uid = row->data(0, Qt::UserRole).toString();
        // A tail's row stands for the tail; its balloon's row is only selected when the balloon itself is.
        row->setSelected(picked.contains(uid) && !carriers.contains(uid));
        for (int c = 0; c < row->childCount(); ++c)
            row->child(c)->setSelected(pickedTails.contains(TailRef{uid, c}));
    }
    m_syncingList = false;

    // An action that acts on one object stays disabled while several things are selected rather than
    // quietly acting on the primary: a control that does something other than what the panel names is a
    // lie. A tail can only be deleted.
    const bool oneObject = picked.size() == 1 && pickedTails.isEmpty();
    if (m_actDuplicate) m_actDuplicate->setEnabled(oneObject);
    if (m_actDelete)    m_actDelete->setEnabled(!picked.isEmpty());
    if (m_presetMenu)   m_presetMenu->menuAction()->setEnabled(oneObject);
    if (m_reanchorMenu) m_reanchorMenu->menuAction()->setEnabled(oneObject);
    // Blend is a property every object has, so a set can take it; the stack order is one object's place
    // among the others, so it is not a thing several can be told at once.
    const bool anyObject = picked.size() > m_carriers.size();
    if (m_blendMenu)   m_blendMenu->menuAction()->setEnabled(anyObject);
    if (m_actForward)  m_actForward->setEnabled(oneObject);
    if (m_actBackward) m_actBackward->setEnabled(oneObject);

    if (m_objectState) {
        if (picked.isEmpty()) {
            m_objectState->clearSelection();
        } else if (singleTail) {
            m_objectState->setTail(primary->artifact(), m_selectedTail);
        } else if (!pickedTails.isEmpty()) {
            m_objectState->setMixedSubjects(selectedSubjectCount());
        } else if (oneObject) {
            m_objectState->setArtifact(m_artifacts.value(m_selectedOverlay));
        } else {
            QList<TextArtifact> subjects;
            subjects.reserve(picked.size());
            for (const QString& uid : picked)
                subjects.append(m_artifacts.value(uid));
            m_objectState->setArtifacts(subjects);
        }
    }
    emit subjectChanged(m_subject, m_selectedOverlay);
}




void ObjectController::rebuildPresetMenu()
{
    m_presetMenu->clear();
    const QList<BubblePreset>& presets = m_presets.presets();
    for (int i = 0; i < presets.size(); ++i) {
        QAction* a = m_presetMenu->addAction(presets.at(i).name);
        connect(a, &QAction::triggered, this, [this, i] { applyPresetToSelection(i); });
    }
}

bool ObjectController::applyColourAt(const QPointF& scenePos, const QTransform& deviceTransform,
                                     const QColor& colour)
{
    auto* bubble = qobject_cast<BubbleObject*>(
        dynamic_cast<Object*>(m_scene->itemAt(scenePos, deviceTransform)));
    if (!bubble || bubble->isOrphaned() || !colour.isValid())
        return false;   // imported artwork has no colour of its own, and an orphan is not on the strip

    // A few screen pixels of forgiveness, in the object's own units: a thin letter and a hairline
    // outline are unhittable at 100% zoom otherwise, and at 400% the same slack would swallow the fill.
    qreal onScreen = m_view ? m_view->transform().m11() : 1.0;
    onScreen *= bubble->scale();
    if (onScreen <= 0.0)
        onScreen = 1.0;
    const qreal slack = k_pickSlackPx / onScreen;

    TextArtifact       a    = bubble->artifact();
    QString            step;
    const ArtifactPart part = artifactPartAt(a, bubble->mapFromScene(scenePos), slack);
    switch (part) {
    case ArtifactPart::Text:
        a.text.colour = colour;
        step          = tr("Apply text colour");
        break;
    case ArtifactPart::Outline:
        a.skin.stroke = colour;
        step          = tr("Apply outline colour");
        break;
    case ArtifactPart::Fill:
        a.skin.fill = colour;
        step        = tr("Apply fill colour");
        break;
    case ArtifactPart::None:
        return false;   // the transparent corner of the box is not the balloon
    }

    // Pouring onto something that is part of a selection paints the **whole** selection, each object in
    // the role it has: a fill lands on everything with a silhouette, the lettering's colour on everything.
    // An object outside the selection is a fresh subject, and painting it selects it as any click does.
    if (m_selectedOverlays.size() > 1 && m_selectedOverlays.contains(bubble->uid())) {
        QList<TextArtifact> next;
        next.reserve(m_selectedOverlays.size());
        for (const QString& uid : std::as_const(m_selectedOverlays)) {
            TextArtifact each = m_artifacts.value(uid);
            const bool   shaped = each.shape.kind != TextArtifact::Shape::None;
            switch (part) {
            case ArtifactPart::Text:    each.text.colour = colour; break;
            case ArtifactPart::Outline: if (shaped) each.skin.stroke = colour; break;
            case ArtifactPart::Fill:    if (shaped) each.skin.fill   = colour; break;
            case ArtifactPart::None:    break;
            }
            next.append(each);
        }
        applyPanelArtifacts(next, /*commit=*/true, step);
        if (m_objectState)
            m_objectState->setArtifacts(next);
        return true;
    }

    selectOverlay(bubble->uid());
    m_objectState->setArtifact(a);
    applyPanelArtifact(a, /*commit=*/true, step);
    return true;
}

void ObjectController::applyPresetToSelection(int index)
{
    auto* bubble = qobject_cast<BubbleObject*>(m_overlayItems.value(m_selectedOverlay));
    if (!bubble || index < 0 || index >= m_presets.presets().size())
        return;
    // keepShape is false: the shape section is on screen beside this menu, so a preset changing the
    // shape is visible and reversible — unlike the tool options under the Text tool, where it is not.
    const TextArtifact a =
        PresetStore::applied(m_presets.presets().at(index), bubble->artifact(), /*keepShape=*/false);
    m_objectState->setArtifact(a);
    applyPanelArtifact(a, /*commit=*/true);
}

void ObjectController::applyPanelArtifacts(const QList<TextArtifact>& objects, bool commit,
                                           const QString& undoText)
{
    if (objects.size() != m_selectedOverlays.size())
        return;   // the panel is answering about a selection that has since changed

    for (int i = 0; i < objects.size(); ++i) {
        const QString& uid = m_selectedOverlays.at(i);
        if (auto* bubble = qobject_cast<BubbleObject*>(m_overlayItems.value(uid))) {
            bubble->setArtifact(objects.at(i));
            m_artifacts.insert(uid, objects.at(i));
        }
    }
    if (!commit)
        return;   // live preview only, exactly as the single-object path does

    // Each object keeps its own width fraction: a colour does not change how much room the artwork takes,
    // but a stroke does, and the render draws the asset at wFrac of the page.
    if (const double tw = m_layout.targetWidth(); tw > 0) {
        for (auto& o : m_overlays) {
            const QString uid = QString::fromStdString(o.uid);
            if (auto* item = m_overlayItems.value(uid); item && m_selectedOverlays.contains(uid))
                o.wFrac = item->contentBounds().width() * item->scale() / tw;
        }
    }

    refreshList();
    pushOverlays(!undoText.isEmpty() ? undoText
                                     : tr("Edit %n objects", "", static_cast<int>(objects.size())));
}

void ObjectController::applyPanelArtifact(const TextArtifact& a, bool commit, const QString& undoText)
{
    auto* item = qobject_cast<BubbleObject*>(m_overlayItems.value(m_selectedOverlay));
    if (!item)
        return;   // the panel authors bubbles; imported artwork has no parameters to apply

    item->setArtifact(a);
    m_artifacts.insert(m_selectedOverlay, a);

    // Live edits repaint only. Persisting every keystroke would write a PNG and push an undo step per
    // character; the panel debounces and tells us when it has settled.
    if (!commit)
        return;

    // Typing a longer line, or restyling, changes how much room the artwork takes — and the render
    // draws the asset at wFrac of the page, not at whatever size the SVG happens to come out. Without
    // this the re-emitted artwork would be squeezed back into the old width.
    if (const double tw = m_layout.targetWidth(); tw > 0) {
        const qreal k = item->scale();
        for (auto& o : m_overlays) {
            if (QString::fromStdString(o.uid) != m_selectedOverlay)
                continue;
            o.wFrac = item->contentBounds().width() * k / tw;
            break;
        }
    }

    refreshList();
    pushOverlays(!undoText.isEmpty()           ? undoText
                 : m_subject == Subject::Tail  ? tr("Edit tail")
                                               : tr("Edit bubble"));
}

void ObjectController::importArtwork()
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

    // Onto whatever the author is looking at: the middle of the viewport, resolved to the page under
    // it. Dropping it at the top of the chapter would mean scrolling back to find what you just added.
    const QRectF  view   = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
    const QPointF centre = view.center();
    const int     page   = m_layout.pageAtSceneY(centre.y());
    if (page < 0)
        return;

    const double tw = m_layout.targetWidth();
    if (tw <= 0)
        return;

    // Record the artwork's own width as a fraction straight away rather than leaving it at 0 ("natural
    // size"). Both draw identically today; the difference shows on the next re-profile, where a logo
    // that knows its width relative to the page grows with the chapter and a "natural size" one does
    // not. Growing with the chapter is what anyone placing artwork on a page meant.
    const qreal natural = renderAssetFile(file).width();

    m_selectNewOverlay = true;
    emit artworkImportRequested(file, centre.x() / tw,
                                (centre.y() - m_layout.page(page).top) / tw,
                                natural > 0 ? natural / tw : 0.0,
                                m_layout.anchorUidForPage(page));
}

void ObjectController::rebuildReanchorMenu()
{
    m_reanchorMenu->clear();

    const auto it = std::find_if(m_overlays.cbegin(), m_overlays.cend(),
                                 [this](const Platemaker::Models::StripOverlay& o) {
                                     return QString::fromStdString(o.uid) == m_selectedOverlay;
                                 });
    if (it == m_overlays.cend())
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
        connect(a, &QAction::triggered, this, [this, uid = page.inputUid] { reanchorSelection(uid); });
    }
}

void ObjectController::reanchorSelection(const QString& pageUid)
{
    for (auto& o : m_overlays) {
        if (QString::fromStdString(o.uid) != m_selectedOverlay)
            continue;
        if (QString::fromStdString(o.anchorInputUid) == pageUid)
            return;
        // Only the anchor changes. Placement is measured from the anchor page's top, so the object lands
        // at the offset it had on its old page — for an unanchored one, where it sat on the page it lost,
        // which is the best starting point there is and one drag from wherever it should be.
        o.anchorInputUid = pageUid.toStdString();
        syncItems();
        refreshList();
        reselect();
        pushOverlays(tr("Re-anchor"));
        return;
    }
}

void ObjectController::deleteSelectedTail()
{
    auto* bubble = qobject_cast<BubbleObject*>(m_overlayItems.value(m_selectedOverlay));
    if (!bubble)
        return;
    TextArtifact a = bubble->artifact();
    if (m_selectedTail < 0 || m_selectedTail >= a.tails.items.size())
        return;
    a.tails.items.removeAt(m_selectedTail);

    // The balloon is what is left to hold once its tail is gone. Selected before the edit goes out, so the
    // feed that comes back finds a balloon selected, not a tail that no longer exists.
    const QString uid = m_selectedOverlay;
    selectOverlay(uid);
    applyPanelArtifact(a, /*commit=*/true, tr("Delete tail"));
    if (m_objectState)
        m_objectState->setArtifact(a);
}

void ObjectController::deleteSelectedOverlay()
{
    if (m_subject == Subject::Tail) {
        deleteSelectedTail();
        return;
    }
    if (m_selectedOverlays.isEmpty())
        return;

    // A selection of two kinds is deleted as one: the objects go, and the tails of the balloons that
    // stay go with them. Highest index first, or removing tail 1 would renumber tail 2 under our feet.
    if (!m_selectedTails.isEmpty()) {
        QStringList doomedObjects = m_selectedOverlays;
        for (const QString& carrier : std::as_const(m_carriers))
            doomedObjects.removeAll(carrier);   // it only lent a tail; nobody asked for the balloon

        // A balloon the artist *did* pick goes whole, so its own selected tails need no separate removal.
        QHash<QString, QList<int>> byBubble;
        for (const TailRef& t : std::as_const(m_selectedTails)) {
            if (!doomedObjects.contains(t.uid))
                byBubble[t.uid].append(t.index);
        }
        const int subjects = selectedSubjectCount();

        for (auto it = byBubble.begin(); it != byBubble.end(); ++it) {
            auto* bubble = qobject_cast<BubbleObject*>(m_overlayItems.value(it.key()));
            if (!bubble)
                continue;
            TextArtifact a = bubble->artifact();
            QList<int>   indexes = it.value();
            std::sort(indexes.begin(), indexes.end(), std::greater<int>());
            for (int i : std::as_const(indexes)) {
                if (i >= 0 && i < a.tails.items.size())
                    a.tails.items.removeAt(i);
            }
            bubble->setArtifact(a);
            m_artifacts.insert(it.key(), a);
            writePlacement(it.key());
        }

        m_overlays.erase(std::remove_if(m_overlays.begin(), m_overlays.end(),
                                        [&](const Platemaker::Models::StripOverlay& o) {
                                            return doomedObjects.contains(QString::fromStdString(o.uid));
                                        }),
                         m_overlays.end());
        for (const QString& uid : std::as_const(doomedObjects))
            m_artifacts.remove(uid);

        selectOverlay(QString());
        syncItems();
        refreshList();
        pushOverlays(tr("Delete %n objects", "", subjects));
        return;
    }

    // One history step for the gesture, not one per object: the artist deleted a selection, and that is
    // what they will expect one Ctrl+Z to bring back.
    const QStringList doomed = m_selectedOverlays;
    m_overlays.erase(std::remove_if(m_overlays.begin(), m_overlays.end(),
                                    [&](const Platemaker::Models::StripOverlay& o) {
                                        return doomed.contains(QString::fromStdString(o.uid));
                                    }),
                     m_overlays.end());
    for (const QString& uid : doomed)
        m_artifacts.remove(uid);

    selectOverlay(QString());
    syncItems();
    refreshList();
    pushOverlays(doomed.size() == 1 ? tr("Delete bubble")
                                    : tr("Delete %n objects", "", static_cast<int>(doomed.size())));
}



void ObjectController::setSelectionBlend(Platemaker::Models::BlendMode blend)
{
    int changed = 0;
    for (auto& o : m_overlays) {
        const QString uid = QString::fromStdString(o.uid);
        if (!m_selectedOverlays.contains(uid) || m_carriers.contains(uid) || o.blend == blend)
            continue;
        o.blend = blend;
        ++changed;
        if (Object* item = m_overlayItems.value(uid))
            item->setBlend(blend);
    }
    if (changed == 0)
        return;   // picking the mode it already has is not an edit, and not a history step

    refreshList();
    pushOverlays(changed == 1 ? tr("Set blend") : tr("Set blend on %n objects", "", changed));
}

void ObjectController::moveSelectedInStack(bool forward)
{
    if (m_selectedOverlays.size() != 1 || !m_carriers.isEmpty())
        return;
    const QString uid = m_selectedOverlays.first();

    const auto at = std::find_if(m_overlays.begin(), m_overlays.end(),
                                 [&](const Platemaker::Models::StripOverlay& o) {
                                     return QString::fromStdString(o.uid) == uid;
                                 });
    if (at == m_overlays.end())
        return;

    // The library draws the vector in order, so the **last** element is the front-most — which is why the
    // list shows this reversed (see refreshList). Forward therefore means later in the vector.
    const auto index  = static_cast<qsizetype>(std::distance(m_overlays.begin(), at));
    const auto target = index + (forward ? 1 : -1);
    if (target < 0 || target >= static_cast<qsizetype>(m_overlays.size()))
        return;   // already at the end of the stack; nothing to report and nothing to undo

    std::swap(m_overlays[static_cast<size_t>(index)], m_overlays[static_cast<size_t>(target)]);
    syncItems();    // z-values come from the vector's order
    refreshList();
    pushOverlays(forward ? tr("Bring forward") : tr("Send back"));
}

void ObjectController::setOverlayEnabled(const QString& uid, bool on)
{
    for (auto& o : m_overlays) {
        if (QString::fromStdString(o.uid) != uid || o.enabled == on)
            continue;
        o.enabled = on;
        syncItems();
        pushOverlays(on ? tr("Show overlay") : tr("Hide overlay"));
        return;
    }
}

void ObjectController::commitListOrder()
{
    // First, before any return: a drag that ends in an early exit must not leave the tree ignoring
    // every selection made after it.
    m_rowsMoving = false;

    // The list is the composite order reversed (see refreshList): row 0 is the front-most object and
    // the render draws last-on-top, so the vector is the rows read bottom-up.
    std::unordered_map<std::string, Platemaker::Models::StripOverlay> byUid;
    for (const auto& o : m_overlays)
        byUid.emplace(o.uid, o);

    std::vector<Platemaker::Models::StripOverlay> reordered;
    reordered.reserve(m_overlays.size());
    for (int r = m_list->topLevelItemCount() - 1; r >= 0; --r) {
        const auto it = byUid.find(
            m_list->topLevelItem(r)->data(0, Qt::UserRole).toString().toStdString());
        if (it != byUid.end())
            reordered.push_back(it->second);
    }
    if (reordered.size() != m_overlays.size()) {
        // A row landed where no overlay row belongs — among the strip's pages, say. Put the rows back as
        // the model has them rather than commit an order that has lost someone's bubble.
        refreshList();
        return;
    }

    // Nothing moved — a signal that looked like a drag was not one. Committing anyway would cost a round
    // trip through the owner for a history step that records nothing.
    const bool sameOrder = std::equal(reordered.begin(), reordered.end(), m_overlays.begin(),
                                      [](const Platemaker::Models::StripOverlay& a,
                                         const Platemaker::Models::StripOverlay& b) {
                                          return a.uid == b.uid;
                                      });
    if (sameOrder) {
        reselect();   // put back what the drag's take-and-insert unselected
        return;
    }

    m_overlays = std::move(reordered);
    syncItems();
    pushOverlays(tr("Reorder overlays"));
    reselect();
}

void ObjectController::duplicateSelectedOverlay()
{
    Object* item = m_overlayItems.value(m_selectedOverlay);
    if (!item)
        return;

    for (const auto& o : m_overlays) {
        if (QString::fromStdString(o.uid) != m_selectedOverlay)
            continue;
        // Routed through the creation channel, not copied into the list here: the library mints the new
        // uid and, because the artwork is byte-identical, its inventory dedups it onto the same file.
        // Offset within the same page, so a duplicate stays on the artwork its original was talking to.
        // The offset is a pixel nudge, so it becomes a fraction like everything else — a duplicate has
        // to land the same distance away whatever width the chapter is being laid out at.
        const double tw  = m_layout.targetWidth();
        const double off = tw > 0 ? k_duplicateOffset / tw : 0.0;
        m_selectNewOverlay = true;
        if (auto* bubble = qobject_cast<BubbleObject*>(item)) {
            emit artifactCreated(bubble->artifact(), o.xFrac + off, o.yFrac + off, o.wFrac,
                                 QString::fromStdString(o.anchorInputUid));
        } else {
            // Imported artwork has no authoring record to re-emit, so the copy goes through the import
            // channel instead: the library hashes the same bytes and dedups the new placement onto the
            // file that is already there. Routing it through creation would have written an SVG of a
            // *default* bubble — which is what it did before this was two types.
            emit artworkImportRequested(QString::fromStdString(o.assetPath),
                                        o.xFrac + off, o.yFrac + off, o.wFrac,
                                        QString::fromStdString(o.anchorInputUid));
        }
        return;
    }
}

void ObjectController::pushOverlays(const QString& undoText)
{
    emit overlaysEdited(m_overlays, m_artifacts, undoText);
}

// --- placing a new bubble ---------------------------------------------------

void ObjectController::beginPlacement(const QPointF& scenePos)
{
    m_placing         = true;
    m_placementOrigin = scenePos;

    QColor c = m_dialogParent->palette().color(QPalette::Highlight);
    QPen pen(c);
    pen.setCosmetic(true);
    pen.setStyle(Qt::DashLine);
    c.setAlpha(40);

    m_placementRubber = m_scene->addRect(QRectF(scenePos, QSizeF(0, 0)), pen, c);
    m_placementRubber->setZValue(1000);   // above everything while it is being drawn
}

void ObjectController::updatePlacement(const QPointF& scenePos)
{
    if (m_placementRubber)
        m_placementRubber->setRect(QRectF(m_placementOrigin, scenePos).normalized());
}

void ObjectController::finishPlacement()
{
    QRectF r = m_placementRubber ? m_placementRubber->rect() : QRectF();
    if (m_placementRubber) {
        m_scene->removeItem(m_placementRubber);
        delete m_placementRubber;
        m_placementRubber = nullptr;
    }
    m_placing = false;

    if (m_layout.isEmpty() || !m_toolOptions)
        return;

    // Only a drag creates a bubble. Letting a bare click create one made every click on the artwork a
    // placement — including the click that just deselects the bubble you finished — so the canvas
    // quietly filled up with empty balloons. A click now means what it means everywhere else: deselect.
    if (r.width() < k_minPlacementDrag || r.height() < k_minPlacementDrag) {
        selectOverlay(QString());
        return;
    }

    const int page = m_layout.pageAtSceneY(r.top());
    if (page < 0)
        return;

    // Whatever the active tool places — shape included: the panel is the tool's side of the question,
    // and this controller knows nothing about which tool is armed.
    TextArtifact a = m_toolOptions->prototype();
    a.box = r.size().toSize();
    // The prototype's tail was placed against the panel's nominal box; re-aim it at the one just drawn,
    // just below the balloon, which is where a reader expects a new bubble to be speaking from.
    for (Tail& t : a.tails.items)
        t.tip = QPointF(a.box.width() * 0.28, a.box.height() * 1.25);

    // Creation is the library's: it mints the uid, hashes the asset and dedups identical content, so
    // the owner finishes this and feeds the result back — where it gets selected (see setOverlaySource).
    const double tw = m_layout.targetWidth();
    if (tw <= 0)
        return;

    m_selectNewOverlay = true;
    // The drag was in strip pixels at the width the editor is laid out at, and the SVG about to be
    // written is in those same pixels — so the artwork's own width *is* this fraction of the page, and
    // the bubble comes back at scale 1.
    const QRectF  bounds = artifactBounds(a);
    const QPointF origin = r.topLeft() + bounds.topLeft();
    emit artifactCreated(a, origin.x() / tw, (origin.y() - m_layout.page(page).top) / tw,
                         bounds.width() / tw,
                         m_layout.anchorUidForPage(page));
}

}  // namespace StripEdit
