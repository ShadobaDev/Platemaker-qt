#include "objectcontroller.h"
#include "bubblepanel.h"
#include "layout.h"
#include "overlayitem.h"
#include "artifactpainter.h"
#include "artifactsvg.h"

#include <platemaker/core/strip_overlay_compositor/strip_overlay_compositor.hpp>

#include <QAbstractItemModel>
#include <QAction>
#include <QCryptographicHash>
#include <QFileDialog>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeySequence>
#include <QListWidget>
#include <QListWidgetItem>
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

ObjectController::ObjectController(QGraphicsScene* scene, QGraphicsView* view, QListWidget* list,
                                   BubblePanel* panel, const Layout& layout, QWidget* dialogParent,
                                   QObject* parent)
    : QObject(parent)
    , m_scene(scene)
    , m_view(view)
    , m_list(list)
    , m_bubblePanel(panel)
    , m_layout(layout)
    , m_dialogParent(dialogParent)
{
    connect(m_bubblePanel, &BubblePanel::changed, this,
            [this](const TextArtifact& a) { applyPanelArtifact(a, /*commit=*/false); });
    connect(m_bubblePanel, &BubblePanel::committed, this,
            [this](const TextArtifact& a) { applyPanelArtifact(a, /*commit=*/true); });
    connect(m_bubblePanel, &BubblePanel::deleteRequested, this, &ObjectController::deleteSelectedOverlay);
    connect(m_bubblePanel, &BubblePanel::fitRequested, this, [this] {
        OverlayItem* item = m_overlayItems.value(m_selectedOverlay);
        if (!item) return;
        TextArtifact a = item->artifact();
        a.box = fittedBox(a);
        applyPanelArtifact(a, /*commit=*/true);
        m_bubblePanel->setArtifact(a);
    });

    // --- artifact list (right-bottom): composite order, mute toggles, selection ---
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);

    // Duplicate / Delete as real QActions: Qt::ActionsContextMenu then builds the list's right-click
    // menu from them for free, and the same objects carry the keyboard shortcuts. They are added to
    // the canvas as well, so they work wherever the bubble was selected — but scoped per widget, so
    // Delete keeps deleting characters while the panel's text box has focus.
    m_actDuplicate = new QAction(tr("Duplicate"), this);
    m_actDuplicate->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    m_actDuplicate->setShortcutContext(Qt::WidgetShortcut);
    connect(m_actDuplicate, &QAction::triggered, this, &ObjectController::duplicateSelectedOverlay);

    m_actDelete = new QAction(tr("Delete"), this);
    m_actDelete->setShortcut(QKeySequence::Delete);
    m_actDelete->setShortcutContext(Qt::WidgetShortcut);
    connect(m_actDelete, &QAction::triggered, this, &ObjectController::deleteSelectedOverlay);

    // Artwork drawn elsewhere — a balloon inked on a tablet, a logo — placed as an overlay like any
    // other. Always available, unlike Duplicate/Delete, because it needs no selection.
    m_actImport = new QAction(tr("Import artwork…"), this);
    connect(m_actImport, &QAction::triggered, this, &ObjectController::importArtwork);

    for (QWidget* w : {static_cast<QWidget*>(m_list), static_cast<QWidget*>(m_view)}) {
        w->addAction(m_actDuplicate);
        w->addAction(m_actDelete);
        w->addAction(m_actImport);
    }
    m_list->setContextMenuPolicy(Qt::ActionsContextMenu);
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        if (m_syncingList) return;
        const auto sel = m_list->selectedItems();
        selectOverlay(sel.isEmpty() ? QString() : sel.first()->data(Qt::UserRole).toString());
    });
    // Both list handlers are deferred to the next event-loop turn on purpose. Persisting an edit
    // round-trips through the owner and comes back as a re-feed that clears and refills this list —
    // which cannot safely happen inside the list's own itemChanged / rowsMoved emission.
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* row) {
        if (m_syncingList || !row) return;
        const QString uid = row->data(Qt::UserRole).toString();
        const bool    on  = row->checkState() == Qt::Checked;
        QTimer::singleShot(0, this, [this, uid, on] { setOverlayEnabled(uid, on); });
    });
    // Dragging a row changes the composite order, which is what the render draws bottom-to-top.
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        if (m_syncingList) return;
        QTimer::singleShot(0, this, [this] { commitListOrder(); });
    });

    // The scene is the other half of the selection: clicking a bubble on the strip drives the list
    // and the panel, and vice versa.
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
        if (m_syncingList) return;
        const auto picked = m_scene->selectedItems();
        for (QGraphicsItem* gi : picked)
            if (auto* oi = dynamic_cast<OverlayItem*>(gi)) { selectOverlay(oi->uid()); return; }
        selectOverlay(QString());
    });
}

void ObjectController::setAuthoring(bool active, bool textOnly)
{
    m_authoring = active;
    m_textOnly  = textOnly;
    // Objects stay visible under every tool (they are part of what the strip looks like) but are only
    // selectable while a tool that authors them is active, so the flag change has to reach the items.
    syncItems();
    if (!active)
        selectOverlay(QString());
}

bool ObjectController::objectAt(const QPointF& scenePos, const QTransform& deviceTransform) const
{
    return dynamic_cast<OverlayItem*>(m_scene->itemAt(scenePos, deviceTransform)) != nullptr;
}

void ObjectController::reselect()
{
    if (!m_selectedOverlay.isEmpty())
        selectOverlay(m_selectedOverlay);
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
        if (m_bubblePanel)
            m_bubblePanel->focusText();
        return;
    }
    m_selectNewOverlay = false;

    // The selection may not have survived the edit (a delete, or an undo that removed it).
    if (!m_selectedOverlay.isEmpty() && !m_overlayItems.contains(m_selectedOverlay))
        selectOverlay(QString());
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

        // Normally the asset carries the parameters that say what to draw. When it does not — art drawn
        // elsewhere, or a file edited outside Platemaker — fall back to drawing the asset itself: the
        // bubble still shows and still moves, it just cannot be re-typed. That is the intended
        // degradation, and it is what makes an imported shape a first-class overlay rather than an error.
        TextArtifact a = m_artifacts.value(uid);
        QPixmap      fallback;
        OverlayItem* item = m_overlayItems.value(uid);
        if (!m_artifacts.contains(uid)) {
            fallback = renderAssetFile(QString::fromStdString(o.assetPath));
            // The artwork's own size seeds the box, but only once. Re-reading it on every feed would
            // undo a resize the moment it was made — and on a synced drive the file may still be
            // reporting its previous size for a moment after being rewritten.
            if (item)
                a.box = item->artifact().box;
            else if (!fallback.isNull())
                a.box = fallback.size();
        }

        if (!item) {
            item = new OverlayItem(uid, a);
            connect(item, &OverlayItem::geometryEdited, this, &ObjectController::onOverlayGeometryEdited);
            m_scene->addItem(item);
            m_overlayItems.insert(uid, item);
        } else if (item->artifact() != a) {
            item->setArtifact(a);
        }

        // A styled bubble is drawn by the library, because its effect is an SVG filter Qt cannot render.
        // Unstyled ones keep drawing locally: same geometry, no round-trip.
        if (fallback.isNull() && a.style != TextArtifact::Style::Clean)
            item->setSharpRaster(sharpRasterFor(a));
        else
            item->setSharpRaster(QImage());

        item->setFallbackPixmap(fallback);
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
        item->setFlag(QGraphicsItem::ItemIsSelectable, m_authoring && !orphaned);
    }
}

void ObjectController::onOverlayGeometryEdited(const QString& uid)
{
    OverlayItem* item = m_overlayItems.value(uid);
    if (!item)
        return;

    const double tw = m_layout.targetWidth();
    if (tw <= 0)
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

    // A resize or tail drag changed the artifact too — but only for an overlay that *has* one. For a
    // flat asset item->artifact() is a default bubble, and storing it would make the overlay look
    // authored: the next sync would draw a blank balloon where the imported artwork was.
    if (m_artifacts.contains(uid)) {
        m_artifacts.insert(uid, item->artifact());
        if (uid == m_selectedOverlay && m_bubblePanel)
            m_bubblePanel->setArtifact(item->artifact());
    }
    refreshList();
    pushOverlays(tr("Move bubble"));
}

void ObjectController::refreshList()
{
    if (!m_list)
        return;

    m_syncingList = true;
    m_list->clear();
    // The list is a **stack**: row 0 is the front-most object, and a row covers every row below it
    // wherever they overlap. The render draws m_overlays in vector order, so the *last* element is the
    // one on top — which makes the list that vector reversed. Reversing here rather than in the model
    // keeps the library's "composite order == vector order" rule intact and costs one iterator.
    for (auto rit = m_overlays.rbegin(); rit != m_overlays.rend(); ++rit) {
        const auto&   o    = *rit;
        const QString uid  = QString::fromStdString(o.uid);
        const int     page = m_layout.pageForAnchor(QString::fromStdString(o.anchorInputUid));

        const QString label = m_artifacts.contains(uid) ? artifactLabel(m_artifacts.value(uid))
                                                        : tr("(flat asset)");
        const QString prefix = (page >= 0) ? tr("p.%1").arg(page + 1, 2, 10, QLatin1Char('0'))
                                           : tr("orphan");

        auto* row = new QListWidgetItem(QStringLiteral("%1 · %2").arg(prefix, label));
        row->setData(Qt::UserRole, uid);
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        row->setCheckState(o.enabled ? Qt::Checked : Qt::Unchecked);
        if (page < 0) {
            row->setForeground(m_dialogParent->palette().brush(QPalette::Disabled, QPalette::WindowText));
            row->setToolTip(tr("The page this overlay was anchored to is not in the strip. It is kept, "
                               "and comes back when its page does — the render skips it meanwhile."));
        }
        m_list->addItem(row);
        if (uid == m_selectedOverlay)
            row->setSelected(true);
    }
    m_syncingList = false;
}

void ObjectController::selectOverlay(const QString& uid)
{
    m_selectedOverlay = uid;

    m_syncingList = true;
    for (auto it = m_overlayItems.begin(); it != m_overlayItems.end(); ++it)
        it.value()->setSelected(it.key() == uid);
    for (int r = 0; r < m_list->count(); ++r) {
        QListWidgetItem* row = m_list->item(r);
        row->setSelected(row->data(Qt::UserRole).toString() == uid);
    }
    m_syncingList = false;

    const bool has = !uid.isEmpty() && m_overlayItems.contains(uid);
    if (m_actDuplicate) m_actDuplicate->setEnabled(has);
    if (m_actDelete)    m_actDelete->setEnabled(has);

    if (!m_bubblePanel)
        return;
    // Only a bubble this editor authored can be edited here. A flat asset — imported artwork, or a file
    // whose parameters were lost — has no record, and item->artifact() would hand the panel a *default*
    // bubble: typing into it would quietly replace the artwork with a blank balloon.
    OverlayItem* item = m_overlayItems.value(uid);
    if (item && m_artifacts.contains(uid))
        m_bubblePanel->setArtifact(item->artifact());
    else
        m_bubblePanel->clearSelection();
}

void ObjectController::applyPanelArtifact(const TextArtifact& a, bool commit)
{
    OverlayItem* item = m_overlayItems.value(m_selectedOverlay);
    if (!item)
        return;

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
    pushOverlays(tr("Edit bubble"));
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

void ObjectController::deleteSelectedOverlay()
{
    if (m_selectedOverlay.isEmpty())
        return;
    const QString uid = m_selectedOverlay;

    m_overlays.erase(std::remove_if(m_overlays.begin(), m_overlays.end(),
                                    [&](const Platemaker::Models::StripOverlay& o) {
                                        return QString::fromStdString(o.uid) == uid;
                                    }),
                     m_overlays.end());
    m_artifacts.remove(uid);

    selectOverlay(QString());
    syncItems();
    refreshList();
    pushOverlays(tr("Delete bubble"));
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
    // The list is the composite order reversed (see refreshList): row 0 is the front-most object and
    // the render draws last-on-top, so the vector is the rows read bottom-up.
    std::unordered_map<std::string, Platemaker::Models::StripOverlay> byUid;
    for (const auto& o : m_overlays)
        byUid.emplace(o.uid, o);

    std::vector<Platemaker::Models::StripOverlay> reordered;
    reordered.reserve(m_overlays.size());
    for (int r = m_list->count() - 1; r >= 0; --r) {
        const auto it = byUid.find(m_list->item(r)->data(Qt::UserRole).toString().toStdString());
        if (it != byUid.end())
            reordered.push_back(it->second);
    }
    if (reordered.size() != m_overlays.size())
        return;   // defensive: a partial rebuild would silently drop someone's bubble

    m_overlays = std::move(reordered);
    syncItems();
    pushOverlays(tr("Reorder overlays"));
}

void ObjectController::duplicateSelectedOverlay()
{
    OverlayItem* item = m_overlayItems.value(m_selectedOverlay);
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
        emit artifactCreated(item->artifact(), o.xFrac + off, o.yFrac + off, o.wFrac,
                             QString::fromStdString(o.anchorInputUid));
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

    if (m_layout.isEmpty() || !m_bubblePanel)
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

    TextArtifact a = m_bubblePanel->prototype();
    if (m_textOnly)
        a.shape = TextArtifact::Shape::None;   // the Text tool is this object without a balloon
    a.box = r.size().toSize();
    // The prototype's tail was placed against the panel's nominal box; re-aim it at the one just drawn,
    // just below the balloon, which is where a reader expects a new bubble to be speaking from.
    for (Tail& t : a.tails)
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
