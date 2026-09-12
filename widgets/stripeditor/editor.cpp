#include "editor.h"
#include "ui_editor.h"
#include "flowlayout.h"
#include "gradepanel.h"
#include "objectcontroller.h"
#include "pagesource.h"
#include "bubblepanel.h"

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
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QScrollBar>
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

        // Everything placed on the strip. It drives the scene, the list and the panel; it owns no
        // persistence, so every edit leaves through one of its four signals and comes back as a re-feed.
        m_objects = new ObjectController(m_scene, m_view, ui->artifactList, m_bubblePanel,
                                         m_layout, this, this);
        connect(m_objects, &ObjectController::artifactCreated,        this, &Editor::artifactCreated);
        connect(m_objects, &ObjectController::overlaysEdited,         this, &Editor::overlaysEdited);
        connect(m_objects, &ObjectController::artworkImportRequested, this, &Editor::artworkImportRequested);
        // Splitter behaviour (not expressible in the .ui): canvas absorbs resize, panels keep their width.
        ui->editorBody->setStretchFactor(0, 0);   // toolbox
        ui->editorBody->setStretchFactor(1, 1);   // canvas
        ui->editorBody->setStretchFactor(2, 0);   // right panel
        ui->editorBody->setSizes({108, 700, 260}); // toolbox wide enough for a 2-tile row by default
        ui->rightPanel->setStretchFactor(0, 3);    // options
        ui->rightPanel->setStretchFactor(1, 2);    // artifacts

        connect(m_toolGroup, &QButtonGroup::idClicked, this, [this](int id) { setTool(static_cast<Tool>(id)); });

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
    m_objects->setAuthoring(artifactToolActive(), tool == Tool::Text);
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
    m_objects->setSyncing(true);  // scene->clear() drops the selection; that is not a user action
    m_scene->clear();           // deletes every item (incl. the StripItem); the tracked pointers are now stale
    m_objects->setSyncing(false);
    m_item = nullptr;
    m_seamItems.clear();
    m_objects->forgetItems();   // owned by the scene — already deleted, just forget them
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
    m_objects->syncItems();
    m_objects->refreshList();
    m_objects->reselect();

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
    m_objects->setSyncing(true);
    m_scene->clear();
    m_objects->setSyncing(false);
    m_item = nullptr;
    m_seamItems.clear();
    m_objects->forgetItems();
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
                if (!m_objects->objectAt(scenePos, m_view->transform())) {
                    m_objects->beginPlacement(scenePos);
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseMove && m_objects->isPlacing()) {
            auto* me = static_cast<QMouseEvent*>(event);
            m_objects->updatePlacement(m_view->mapToScene(me->position().toPoint()));
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && m_objects->isPlacing()) {
            m_objects->finishPlacement();
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

void Editor::setOverlaySource(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                              const ArtifactMap&                                  artifacts)
{
    m_objects->setSource(overlays, artifacts);
}

bool Editor::artifactToolActive() const
{
    return m_tool == Tool::Bubble || m_tool == Tool::Text;
}

}  // namespace StripEdit
