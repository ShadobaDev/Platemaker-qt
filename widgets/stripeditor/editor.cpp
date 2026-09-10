#include "editor.h"
#include "ui_editor.h"
#include "flowlayout.h"
#include "gradepanel.h"
#include "pagesource.h"
#include "bubblepanel.h"
#include "overlayitem.h"
#include "artifactpainter.h"
#include "artifactsvg.h"

#include <platemaker/core/strip_overlay_compositor/strip_overlay_compositor.hpp>

#include <QButtonGroup>
#include <QDebug>
#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QAction>
#include <QGraphicsRectItem>
#include <QKeyEvent>
#include <QKeySequence>
#include <QFileDialog>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMouseEvent>
#include <QCryptographicHash>
#include <QPainter>
#include <QTimer>
#include <QPen>
#include <QScrollBar>
#include <QSvgRenderer>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyleOptionGraphicsItem>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

namespace StripEdit {

namespace {

//! Pages built beyond the viewport on each side. One page is several slices tall, so ±1 already covers
//! a comfortable scroll ahead; a larger margin would multiply a much heavier unit of work.
constexpr int k_prefetchPages = 1;

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

/**
 * @brief One graphics item that draws every input page as its own image — seam-free.
 *
 * One QGraphicsPixmapItem per page leaves a 1px hairline at each join (QGraphicsView clips and rounds
 * each item's edge independently). Drawing all pages through one item, in one painter pass, tiles them
 * edge-to-edge with no seam at any zoom. The item is a thin view over the Editor: it owns no
 * pixels — it asks the viewer for each page's built pixmap (if ready) or its blurry proxy, so the
 * lazy/async machinery lives in one place.
 */
class StripItem : public QGraphicsItem
{
public:
    explicit StripItem(Editor *owner) : m_owner(owner) {}

    QRectF boundingRect() const override
    {
        const QSize s = m_owner->stripSize();
        return QRectF(0, 0, s.width(), s.height());
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *) override
    {
        // Smooth the pixmap interior, but turn OFF edge antialiasing: with AA on, each drawPixmap
        // coverage-antialiases the destination rect's edges at fractional zoom, so the boundary row
        // between two pages is only partially covered and the background hairlines through — that is
        // the "1px frame". AA off makes adjacent pages tile with hard edges, each device row owned by
        // exactly one page, no bleed. (Local to this item — the view keeps AA for text/seam lines.)
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QRectF exposed = option->exposedRect;
        for (int i = 0; i < m_owner->pageCount(); ++i) {
            const QRectF r = m_owner->pageRect(i);
            if (!r.intersects(exposed))
                continue;

            if (m_owner->gradeActive()) {
                const QPixmap graded = m_owner->gradedOf(i);
                if (!graded.isNull()) {
                    painter->drawPixmap(r.topLeft(), graded); // live grade preview
                    continue;
                }
                // not graded yet → briefly show the ungraded page below while the grade is produced
            }

            const QPixmap page = m_owner->pageOf(i);
            if (!page.isNull()) {
                painter->drawPixmap(r.topLeft(), page);     // sharp, at the strip's native scale
                continue;
            }
            const QPixmap proxy = m_owner->proxyOf(i);
            if (!proxy.isNull())
                painter->drawPixmap(r, proxy, QRectF(proxy.rect())); // blurry proxy, scaled into the page rect
            else
                painter->fillRect(r, m_owner->palette().color(QPalette::Base)); // brief neutral placeholder
        }
    }

private:
    Editor *m_owner;
};

} // namespace

Editor::Editor(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Editor)
{
    // Page memory: the feed, the proxy/sharp tiers and the graded previews. It reads m_layout, which
    // this widget owns and rebuilds, so it takes it by reference.
    m_pages = new PageSource(m_layout, this);
    connect(m_pages, &PageSource::pageReady, this, [this](int index) {
        if (m_item)
            m_item->update(pageRect(index));
    });

    // Toolbar buttons + the graphics view come from the Designer form; the runtime wiring is here.
    ui->setupUi(this);
    m_view      = ui->graphicsView;
    m_zoomLabel = ui->labelZoom;

    m_scene = new QGraphicsScene(this);
    m_view->setScene(m_scene);
    // Hand-drag to pan the big canvas; scrollbars + wheel cover the rest. No hardcoded background —
    // the strip sits on the themed palette (see the "inherit, don't hardcode colours" rule).
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_view->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    m_view->viewport()->installEventFilter(this);   // Ctrl+wheel zoom
    // Build the pages that scroll into view (plus a prefetch margin).
    connect(m_view->verticalScrollBar(),   &QScrollBar::valueChanged, this, &Editor::updateVisiblePages);
    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, &Editor::updateVisiblePages);

    connect(ui->buttonZoomOut,   &QToolButton::clicked, this, &Editor::zoomOut);
    connect(ui->buttonZoomIn,    &QToolButton::clicked, this, &Editor::zoomIn);
    connect(ui->buttonFitWidth,  &QToolButton::clicked, this, &Editor::fitWidth);
    connect(ui->buttonZoomReset, &QToolButton::clicked, this, &Editor::resetZoom);
    connect(ui->buttonSeams, &QToolButton::toggled, this, [this](bool on) {
        for (QGraphicsLineItem *seam : std::as_const(m_seamItems))
            seam->setVisible(on);
    });
    connect(ui->buttonRenderView, &QToolButton::clicked, this, [this] { emit renderAndViewRequested(); });

    // --- Editor shell: the splitters, canvas, tool-options stack and artifact list come from the .ui
    // (editorBody = toolbox | canvas | rightPanel). Here we only fill the toolbox with a flowing grid of
    // square tool tiles (a flow layout can't be expressed in a .ui), give the stack a page per tool, and
    // set the splitter sizing (not a .ui property). Pan (the default) keeps today's behaviour: hand-drag
    // pan and no side panel.
    {
        auto* railLay = new FlowLayout(ui->toolRail, 6, 4, 4); // margin, hSpacing, vSpacing — wraps to fit
        m_toolGroup = new QButtonGroup(this);
        m_toolGroup->setExclusive(true);
        auto addTool = [&](Tool t, const QString& iconPath, const QString& tip) {
            auto* b = new QToolButton(ui->toolRail);
            b->setIcon(QIcon(iconPath));
            b->setIconSize(QSize(26, 26));
            b->setToolTip(tip);
            b->setCheckable(true);
            b->setAutoRaise(true);
            b->setToolButtonStyle(Qt::ToolButtonIconOnly);
            b->setFixedSize(40, 40);       // square tile
            railLay->addWidget(b);
            m_toolGroup->addButton(b, static_cast<int>(t));
        };
        addTool(Tool::Pan,    QStringLiteral(":/icons/tools/pan.svg"),    tr("Pan / select (default)"));
        addTool(Tool::Grade,  QStringLiteral(":/icons/tools/cc.svg"),     tr("Colour correction"));
        addTool(Tool::Bubble, QStringLiteral(":/icons/tools/bubble.svg"), tr("Speech bubble"));
        addTool(Tool::Text,   QStringLiteral(":/icons/tools/text.svg"),   tr("Text"));

        // One tool-options page per tool (index == Tool). Empty scaffolds for now; grade/bubble/text
        // controls arrive in later increments. Pan has no options.
        ui->toolOptions->addWidget(new QWidget(ui->toolOptions)); // Pan
        m_gradePanel = new GradePanel(ui->toolOptions);                 // Grade
        ui->toolOptions->addWidget(m_gradePanel);
        connect(m_gradePanel, &GradePanel::changed, this, [this](const Platemaker::Models::ColourCorrection& cc) {
            // Live edit: apply it, but do NOT push it back into the panel — the panel is the source
            // here, and re-syncing its widgets mid-drag would fight the slider the user is holding.
            applyGrade(cc);
        });
        connect(m_gradePanel, &GradePanel::committed, this, [this](const Platemaker::Models::ColourCorrection& cc) {
            emit colourCorrectionEdited(cc); // settled: the owner persists it onto the project (undo)
        });
        // One panel for Bubble *and* Text: they author the same object (a TextArtifact, with or without
        // a shape), so both rail buttons point at this page and setTool() just hides the shape group.
        m_bubblePanel = new BubblePanel(ui->toolOptions);
        ui->toolOptions->addWidget(m_bubblePanel);
        connect(m_bubblePanel, &BubblePanel::changed, this,
                [this](const TextArtifact& a) { applyPanelArtifact(a, /*commit=*/false); });
        connect(m_bubblePanel, &BubblePanel::committed, this,
                [this](const TextArtifact& a) { applyPanelArtifact(a, /*commit=*/true); });
        connect(m_bubblePanel, &BubblePanel::deleteRequested, this, &Editor::deleteSelectedOverlay);
        connect(m_bubblePanel, &BubblePanel::fitRequested, this, [this] {
            OverlayItem* item = m_overlayItems.value(m_selectedOverlay);
            if (!item) return;
            TextArtifact a = item->artifact();
            a.box = fittedBox(a);
            applyPanelArtifact(a, /*commit=*/true);
            m_bubblePanel->setArtifact(a);
        });

        // Splitter behaviour (not expressible in the .ui): canvas absorbs resize, panels keep their width.
        ui->editorBody->setStretchFactor(0, 0);   // toolbox
        ui->editorBody->setStretchFactor(1, 1);   // canvas
        ui->editorBody->setStretchFactor(2, 0);   // right panel
        ui->editorBody->setSizes({108, 700, 260}); // toolbox wide enough for a 2-tile row by default
        ui->rightPanel->setStretchFactor(0, 3);    // options
        ui->rightPanel->setStretchFactor(1, 2);    // artifacts

        connect(m_toolGroup, &QButtonGroup::idClicked, this, [this](int id) { setTool(static_cast<Tool>(id)); });

        // --- artifact list (right-bottom): composite order, mute toggles, selection ---
        ui->artifactList->setDragDropMode(QAbstractItemView::InternalMove);
        ui->artifactList->setSelectionMode(QAbstractItemView::SingleSelection);

        // Duplicate / Delete as real QActions: Qt::ActionsContextMenu then builds the list's right-click
        // menu from them for free, and the same objects carry the keyboard shortcuts. They are added to
        // the canvas as well, so they work wherever the bubble was selected — but scoped per widget, so
        // Delete keeps deleting characters while the panel's text box has focus.
        m_actDuplicate = new QAction(tr("Duplicate"), this);
        m_actDuplicate->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
        m_actDuplicate->setShortcutContext(Qt::WidgetShortcut);
        connect(m_actDuplicate, &QAction::triggered, this, &Editor::duplicateSelectedOverlay);

        m_actDelete = new QAction(tr("Delete"), this);
        m_actDelete->setShortcut(QKeySequence::Delete);
        m_actDelete->setShortcutContext(Qt::WidgetShortcut);
        connect(m_actDelete, &QAction::triggered, this, &Editor::deleteSelectedOverlay);

        // Artwork drawn elsewhere — a balloon inked on a tablet, a logo — placed as an overlay like any
        // other. Always available, unlike Duplicate/Delete, because it needs no selection.
        m_actImport = new QAction(tr("Import artwork…"), this);
        connect(m_actImport, &QAction::triggered, this, &Editor::importArtwork);

        for (QWidget* w : {static_cast<QWidget*>(ui->artifactList), static_cast<QWidget*>(m_view)}) {
            w->addAction(m_actDuplicate);
            w->addAction(m_actDelete);
            w->addAction(m_actImport);
        }
        ui->artifactList->setContextMenuPolicy(Qt::ActionsContextMenu);
        connect(ui->artifactList, &QListWidget::itemSelectionChanged, this, [this] {
            if (m_syncingList) return;
            const auto sel = ui->artifactList->selectedItems();
            selectOverlay(sel.isEmpty() ? QString() : sel.first()->data(Qt::UserRole).toString());
        });
        // Both list handlers are deferred to the next event-loop turn on purpose. Persisting an edit
        // round-trips through the owner and comes back as a re-feed that clears and refills this list —
        // which cannot safely happen inside the list's own itemChanged / rowsMoved emission.
        connect(ui->artifactList, &QListWidget::itemChanged, this, [this](QListWidgetItem* row) {
            if (m_syncingList || !row) return;
            const QString uid = row->data(Qt::UserRole).toString();
            const bool    on  = row->checkState() == Qt::Checked;
            QTimer::singleShot(0, this, [this, uid, on] { setOverlayEnabled(uid, on); });
        });
        // Dragging a row changes the composite order, which is what the render draws bottom-to-top.
        connect(ui->artifactList->model(), &QAbstractItemModel::rowsMoved, this, [this] {
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

        setTool(Tool::Pan);   // default: today's view, right panel hidden
    }

    showEmptyState();
}

void Editor::setTool(Tool tool)
{
    m_tool = tool;
    if (auto* b = m_toolGroup->button(static_cast<int>(tool)))
        b->setChecked(true);
    // Bubble and Text share one options page (see the ctor) — Text is the same object without a shape.
    ui->toolOptions->setCurrentIndex(
        tool == Tool::Text ? static_cast<int>(Tool::Bubble) : static_cast<int>(tool));
    // Pan == today: hand-drag to pan, and no side panel. Any other tool reveals the panel and frees the
    // left button for tool interaction.
    const bool pan = (tool == Tool::Pan);
    m_view->setDragMode(pan ? QGraphicsView::ScrollHandDrag : QGraphicsView::NoDrag);
    ui->rightPanel->setVisible(!pan);

    if (m_bubblePanel)
        m_bubblePanel->setShapeControlsVisible(tool == Tool::Bubble);

    // The artifact list belongs to whichever tool it is listing. It holds overlays today; under Grade it
    // is meant to hold the per-page exclusions, which are not built yet — so hide it there rather than
    // show bubbles under a tool that cannot edit them.
    ui->artifactList->setVisible(artifactToolActive());

    // Drawing takes over the left button, so panning moves to the middle button / scrollbars while an
    // authoring tool is active — the usual drawing-app trade. The crosshair says so.
    m_view->viewport()->setCursor(artifactToolActive() ? Qt::CrossCursor : Qt::ArrowCursor);

    // Bubbles stay visible under every tool (they are part of what the strip looks like) but are only
    // selectable while a tool that authors them is active.
    syncOverlayItems();
    if (!artifactToolActive())
        selectOverlay(QString());
}

void Editor::applyGrade(const Platemaker::Models::ColourCorrection& cc)
{
    if (m_pages->setColourCorrection(cc))
        refreshGradePreview();
}

void Editor::setColourCorrection(const Platemaker::Models::ColourCorrection& cc)
{
    applyGrade(cc);
    if (m_gradePanel)
        m_gradePanel->setColourCorrection(cc);   // the project is the source here — show it in the panel
}

bool Editor::gradeActive() const  { return m_pages->gradeActive(); }
QPixmap Editor::gradedOf(int index) const { return m_pages->gradedOf(index); }
QPixmap Editor::pageOf(int index) const   { return m_pages->pageOf(index); }
QPixmap Editor::proxyOf(int index) const  { return m_pages->proxyOf(index); }

void Editor::refreshGradePreview()
{
    m_pages->clearGraded();    // the grade changed → previous previews are stale
    if (m_item)
        m_item->update();
    updateVisiblePages();      // re-grade what's on screen (produceGraded runs for visible pages)
}

Editor::~Editor()
{
    delete ui;
}

void Editor::setPreviewSource(const std::vector<Platemaker::Models::InputFile>&     inputs,
                                   const Platemaker::Models::OutputProfile&              outProfile,
                                   const std::vector<Platemaker::Models::CanvasProfile>& canvasProfiles,
                                   const std::vector<std::string>&                       canvasProfileIds,
                                   const QString&                                        cacheDir)
{
    // An unchanged feed keeps the built pages — but an empty layout still needs a rebuild, which is the
    // case on the very first feed and after a failed one.
    if (!m_pages->setFeed(inputs, outProfile, canvasProfiles, canvasProfileIds, cacheDir)
        && !m_layout.isEmpty())
        return;
    rebuildScene();
}

void Editor::rebuildScene()
{
    m_syncingList = true;       // scene->clear() drops the selection; that is not a user action
    m_scene->clear();           // deletes every item (incl. the StripItem); the tracked pointers are now stale
    m_syncingList = false;
    m_item = nullptr;
    m_seamItems.clear();
    m_overlayItems.clear();     // owned by the scene — already deleted, just forget them
    m_layout.clear();
    m_pages->reset();

    // Ask the library where each page lands. This reads headers and decodes nothing, and the numbers
    // come from the same page-domain code a render uses — so the strip laid out here is the strip a
    // render would build, before any render exists.
    // Pages the render would skip are dropped here, which is what keeps every page below them at the
    // offset the render will give it.
    m_layout.build(m_pages->layoutPages(), m_pages->inputs());

    if (m_layout.isEmpty()) {
        showEmptyState();
        return;
    }

    m_scene->setSceneRect(0, 0, m_layout.stripWidth(), m_layout.stripHeight());
    // One item draws every page as its own image — no per-item seam. It pulls pixels lazily from us.
    m_item = new StripItem(this);
    m_scene->addItem(m_item);
    addSeamItems();

    // Overlays are placed against the layout above, so they can only be built once it exists. The
    // scene's selection did not survive clear(), so re-apply it to whatever is still selected.
    syncOverlayItems();
    refreshArtifactList();
    if (!m_selectedOverlay.isEmpty())
        selectOverlay(m_selectedOverlay);

    // Default view: 100%. Re-applied on resize until the user zooms.
    m_pendingFit = true;
    applyDefaultZoom();
    updateVisiblePages();       // start building what's on screen
}

void Editor::addSeamItems()
{
    // Where the output will be cut. Unlike the page joins, these are not visible in the strip itself,
    // and they are exactly what an author needs to see: a bubble that straddles one lands on both
    // slices. The render cuts every sliceHeight from the top of the strip, so that is what is drawn.
    const int step = m_pages->sliceHeight();
    if (step <= 0)
        return;

    // A thin, semi-transparent guide, in the palette's text colour so it reads in both themes without a
    // hardcoded colour.
    QColor c = palette().color(QPalette::WindowText);
    c.setAlpha(90);
    QPen pen(c);
    pen.setCosmetic(true);      // stays 1px regardless of zoom
    for (int top = step; top < m_layout.stripHeight(); top += step) {
        auto *seam = m_scene->addLine(0, top, m_layout.stripWidth(), top, pen);
        seam->setVisible(ui->buttonSeams->isChecked());
        seam->setZValue(1);     // above the strip item
        m_seamItems.append(seam);
    }
}

void Editor::showEmptyState()
{
    m_syncingList = true;
    m_scene->clear();
    m_syncingList = false;
    m_item = nullptr;
    m_seamItems.clear();
    m_overlayItems.clear();
    m_layout.clear();

    auto *text = m_scene->addSimpleText(
        tr("No pages to show.\nAdd input pages to the project to see the strip."));
    text->setBrush(palette().color(QPalette::WindowText));
    const QRectF b = text->boundingRect();
    m_scene->setSceneRect(b.adjusted(-40, -40, 40, 40));
    resetZoom();
}

// ---------------------------------------------------------------------------
// Lazy page build: proxy (blurry, instant) + the real page domain (sharp, async)
// ---------------------------------------------------------------------------

void Editor::updateVisiblePages()
{
    if (m_layout.isEmpty())
        return;

    // The viewport mapped into scene coordinates → which pages intersect it.
    const QRectF vis = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
    int first = -1, last = -1;
    for (int i = 0; i < m_layout.pageCount(); ++i) {
        const Page& p = m_layout.page(i);
        if (p.top + p.size.height() >= vis.top() && p.top <= vis.bottom()) {
            if (first < 0) first = i;
            last = i;
        }
    }
    if (first < 0)
        return;

    first = qMax(0, first - k_prefetchPages);
    last  = qMin(m_layout.pageCount() - 1, last + k_prefetchPages);
    for (int i = first; i <= last; ++i) {
        m_pages->request(i);
        m_pages->produceGraded(i); // grade now if built; otherwise the build watcher grades it on arrival
    }
}

// ---------------------------------------------------------------------------
// Zoom
// ---------------------------------------------------------------------------

void Editor::applyZoom(double z)
{
    m_zoom = qBound(0.02, z, 8.0);
    QTransform t;
    t.scale(m_zoom, m_zoom);
    m_view->setTransform(t);
    m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(m_zoom * 100.0)));
    updateVisiblePages();       // zoom changes how many pages are on screen
}

void Editor::userZoom(double z)
{
    m_pendingFit = false;   // the user has taken control — stop re-fitting on resize
    applyZoom(z);
}

void Editor::zoomIn()  { userZoom(m_zoom * 1.25); }
void Editor::zoomOut() { userZoom(m_zoom / 1.25); }
void Editor::resetZoom() { userZoom(1.0); }

void Editor::applyDefaultZoom()
{
    // Default view is native size — 100% — always. (The former "shrink to fit the viewport width" default
    // computed a tiny zoom while the splitter had not yet given the canvas its real width, so the strip
    // opened at ~3%. 100% is what's wanted regardless; "Fit width" stays available on demand.)
    applyZoom(1.0);
}

void Editor::fitWidth()
{
    if (m_layout.stripWidth() <= 0)
        return;
    // Explicit user fit — unlike the default, this MAY enlarge past 100% to fill the width.
    const int vw = m_view->viewport()->width() - m_view->verticalScrollBar()->width();
    if (vw > 0)
        userZoom(static_cast<double>(vw) / static_cast<double>(m_layout.stripWidth()));
}

void Editor::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_pendingFit)
        applyDefaultZoom();     // settle the default zoom as the viewport gets its real size
    updateVisiblePages();
}

bool Editor::eventFilter(QObject *watched, QEvent *event)
{
    // Bubble / Text: the left button draws a new bubble on empty strip. A press that lands on an
    // existing overlay is left alone, so the item's own move/resize handling still runs.
    if (watched == m_view->viewport() && artifactToolActive()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                const QPointF scenePos = m_view->mapToScene(me->position().toPoint());
                if (!dynamic_cast<OverlayItem*>(m_scene->itemAt(scenePos, m_view->transform()))) {
                    beginPlacement(scenePos);
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseMove && m_placing) {
            auto* me = static_cast<QMouseEvent*>(event);
            updatePlacement(m_view->mapToScene(me->position().toPoint()));
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && m_placing) {
            finishPlacement();
            return true;
        }
    }

    if (watched == m_view->viewport() && event->type() == QEvent::Wheel) {
        auto *we = static_cast<QWheelEvent *>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            const int d = we->angleDelta().y();
            if (d > 0)
                zoomIn();
            else if (d < 0)
                zoomOut();
            return true;        // consumed — plain wheel still scrolls vertically
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Text & bubbles
//
// The strip's scene *is* the strip at 1:1, so an overlay's scene position is its library placement
// plus its anchor page's top — no coordinate mapping layer, and the preview lands exactly where the
// render will put it.
// ---------------------------------------------------------------------------

bool Editor::artifactToolActive() const
{
    return m_tool == Tool::Bubble || m_tool == Tool::Text;
}

void Editor::setOverlaySource(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                                   const ArtifactMap&                                  artifacts)
{
    m_overlays  = overlays;
    m_artifacts = artifacts;
    syncOverlayItems();
    refreshArtifactList();

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

QImage Editor::sharpRasterFor(const TextArtifact& a)
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

void Editor::syncOverlayItems()
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
            connect(item, &OverlayItem::geometryEdited, this, &Editor::onOverlayGeometryEdited);
            connect(item, &OverlayItem::artworkResized,  this, &Editor::onArtworkResized);
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
        // The record stores the artwork's top-left; the item is positioned by its balloon's. They differ
        // by the bounds offset whenever a tail reaches above or left of the balloon.
        item->setPos(m_layout.scenePosOf(o) - item->contentBounds().topLeft());
        item->setVisible(o.enabled);
        item->setZValue(z++);
        item->setFlag(QGraphicsItem::ItemIsSelectable, artifactToolActive() && !orphaned);
    }
}

void Editor::onOverlayGeometryEdited(const QString& uid)
{
    OverlayItem* item = m_overlayItems.value(uid);
    if (!item)
        return;

    // Re-anchor to whichever page the bubble now sits on. Crossing a page boundary is a normal drag,
    // and silently re-homing it is the whole point: the offset stays relative to the artwork under it.
    // Back to the artwork's own top-left, which is what the library composites at.
    const QPointF p    = item->pos() + item->contentBounds().topLeft();
    const int     page = m_layout.pageAtSceneY(item->pos().y());
    for (auto& o : m_overlays) {
        if (QString::fromStdString(o.uid) != uid)
            continue;
        o.x = qRound(p.x());
        if (page >= 0) {
            o.anchorInputUid = m_layout.anchorUidForPage(page).toStdString();
            o.y              = qRound(p.y()) - m_layout.page(page).top;
        } else {
            o.y = qRound(p.y());
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
    refreshArtifactList();
    pushOverlays(tr("Move bubble"));
}

void Editor::onArtworkResized(const QString& uid, QSize size)
{
    // Straight through: the size belongs in the artwork itself, and only the owner writes files.
    emit artworkResizeRequested(uid, size);
}

void Editor::refreshArtifactList()
{
    if (!ui->artifactList)
        return;

    m_syncingList = true;
    ui->artifactList->clear();
    for (const auto& o : m_overlays) {
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
            row->setForeground(palette().brush(QPalette::Disabled, QPalette::WindowText));
            row->setToolTip(tr("The page this overlay was anchored to is not in the strip. It is kept, "
                               "and comes back when its page does — the render skips it meanwhile."));
        }
        ui->artifactList->addItem(row);
        if (uid == m_selectedOverlay)
            row->setSelected(true);
    }
    m_syncingList = false;
}

void Editor::selectOverlay(const QString& uid)
{
    m_selectedOverlay = uid;

    m_syncingList = true;
    for (auto it = m_overlayItems.begin(); it != m_overlayItems.end(); ++it)
        it.value()->setSelected(it.key() == uid);
    for (int r = 0; r < ui->artifactList->count(); ++r) {
        QListWidgetItem* row = ui->artifactList->item(r);
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

void Editor::applyPanelArtifact(const TextArtifact& a, bool commit)
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
    refreshArtifactList();
    pushOverlays(tr("Edit bubble"));
}

void Editor::importArtwork()
{
    if (m_layout.isEmpty()) {
        QMessageBox::information(this, tr("Import artwork"),
                                 tr("Add input pages to the project first — artwork is anchored to a "
                                    "page, so there has to be one to put it on."));
        return;
    }

    const QString file = QFileDialog::getOpenFileName(
        this, tr("Import artwork"), QString(),
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

    m_selectNewOverlay = true;
    emit artworkImportRequested(file, qRound(centre.x()), qRound(centre.y()) - m_layout.page(page).top,
                                m_layout.anchorUidForPage(page));
}

void Editor::deleteSelectedOverlay()
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
    syncOverlayItems();
    refreshArtifactList();
    pushOverlays(tr("Delete bubble"));
}

void Editor::setOverlayEnabled(const QString& uid, bool on)
{
    for (auto& o : m_overlays) {
        if (QString::fromStdString(o.uid) != uid || o.enabled == on)
            continue;
        o.enabled = on;
        syncOverlayItems();
        pushOverlays(on ? tr("Show overlay") : tr("Hide overlay"));
        return;
    }
}

void Editor::commitListOrder()
{
    // The list's row order is the composite order: the render draws overlays in vector order, so the
    // last row is the one on top.
    std::unordered_map<std::string, Platemaker::Models::StripOverlay> byUid;
    for (const auto& o : m_overlays)
        byUid.emplace(o.uid, o);

    std::vector<Platemaker::Models::StripOverlay> reordered;
    reordered.reserve(m_overlays.size());
    for (int r = 0; r < ui->artifactList->count(); ++r) {
        const auto it = byUid.find(ui->artifactList->item(r)->data(Qt::UserRole).toString().toStdString());
        if (it != byUid.end())
            reordered.push_back(it->second);
    }
    if (reordered.size() != m_overlays.size())
        return;   // defensive: a partial rebuild would silently drop someone's bubble

    m_overlays = std::move(reordered);
    syncOverlayItems();
    pushOverlays(tr("Reorder overlays"));
}

void Editor::duplicateSelectedOverlay()
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
        m_selectNewOverlay = true;
        emit artifactCreated(item->artifact(), o.x + k_duplicateOffset, o.y + k_duplicateOffset,
                             QString::fromStdString(o.anchorInputUid));
        return;
    }
}

void Editor::pushOverlays(const QString& undoText)
{
    emit overlaysEdited(m_overlays, m_artifacts, undoText);
}

// --- placing a new bubble ---------------------------------------------------

void Editor::beginPlacement(const QPointF& scenePos)
{
    m_placing         = true;
    m_placementOrigin = scenePos;

    QColor c = palette().color(QPalette::Highlight);
    QPen pen(c);
    pen.setCosmetic(true);
    pen.setStyle(Qt::DashLine);
    c.setAlpha(40);

    m_placementRubber = m_scene->addRect(QRectF(scenePos, QSizeF(0, 0)), pen, c);
    m_placementRubber->setZValue(1000);   // above everything while it is being drawn
}

void Editor::updatePlacement(const QPointF& scenePos)
{
    if (m_placementRubber)
        m_placementRubber->setRect(QRectF(m_placementOrigin, scenePos).normalized());
}

void Editor::finishPlacement()
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
    if (m_tool == Tool::Text)
        a.shape = TextArtifact::Shape::None;   // the Text tool is this object without a balloon
    a.box = r.size().toSize();
    // The prototype's tail was placed against the panel's nominal box; re-aim it at the one just drawn,
    // just below the balloon, which is where a reader expects a new bubble to be speaking from.
    for (Tail& t : a.tails)
        t.tip = QPointF(a.box.width() * 0.28, a.box.height() * 1.25);

    // Creation is the library's: it mints the uid, hashes the asset and dedups identical content, so
    // the owner finishes this and feeds the result back — where it gets selected (see setOverlaySource).
    m_selectNewOverlay = true;
    const QPointF origin = r.topLeft() + artifactBounds(a).topLeft();
    emit artifactCreated(a, qRound(origin.x()), qRound(origin.y()) - m_layout.page(page).top,
                         m_layout.anchorUidForPage(page));
}

}  // namespace StripEdit
