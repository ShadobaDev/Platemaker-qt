#include "stripviewer.h"
#include "ui_stripviewer.h"
#include "flowlayout.h"
#include "ccpanel.h"
#include "bubblepanel.h"
#include "textpanel.h"

#include <platemaker/core/colour_corrector/colour_corrector.hpp>
#include <platemaker/infrastructure/thumbnail_cache/thumbnail_cache.hpp>

#include <QButtonGroup>
#include <QDebug>
#include <QEvent>
#include <QFutureWatcher>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QListWidget>
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
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <string>
#include <utility>

namespace {

//! LRU caps (in KiB). A scaled page is big — 800×5120 RGBA is ~16 MiB — so these hold only a handful,
//! which is the point: RAM tracks the viewport plus the prefetch margin, not the chapter length.
constexpr int k_pageCacheKiB  = 96 * 1024;   //!< ~6 scaled pages: visible + prefetch, with headroom.
constexpr int k_proxyCacheKiB = 24 * 1024;   //!< Hundreds of 200px-wide proxies.
//! Pages built beyond the viewport on each side. One page is several slices tall, so ±1 already covers
//! a comfortable scroll ahead; a larger margin would multiply a much heavier unit of work.
constexpr int k_prefetchPages = 1;

/**
 * @brief One graphics item that draws every input page as its own image — seam-free.
 *
 * One QGraphicsPixmapItem per page leaves a 1px hairline at each join (QGraphicsView clips and rounds
 * each item's edge independently). Drawing all pages through one item, in one painter pass, tiles them
 * edge-to-edge with no seam at any zoom. The item is a thin view over the StripViewer: it owns no
 * pixels — it asks the viewer for each page's built pixmap (if ready) or its blurry proxy, so the
 * lazy/async machinery lives in one place.
 */
class StripItem : public QGraphicsItem
{
public:
    explicit StripItem(StripViewer *owner) : m_owner(owner) {}

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
    StripViewer *m_owner;
};

} // namespace

StripViewer::StripViewer(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::StripViewer)
{
    m_pageCache.setMaxCost(k_pageCacheKiB);
    m_proxyCache.setMaxCost(k_proxyCacheKiB);
    m_gradedCache.setMaxCost(k_pageCacheKiB); // a graded preview is the same size as the page it came from

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
    connect(m_view->verticalScrollBar(),   &QScrollBar::valueChanged, this, &StripViewer::updateVisiblePages);
    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, &StripViewer::updateVisiblePages);

    connect(ui->buttonZoomOut,   &QToolButton::clicked, this, &StripViewer::zoomOut);
    connect(ui->buttonZoomIn,    &QToolButton::clicked, this, &StripViewer::zoomIn);
    connect(ui->buttonFitWidth,  &QToolButton::clicked, this, &StripViewer::fitWidth);
    connect(ui->buttonZoomReset, &QToolButton::clicked, this, &StripViewer::resetZoom);
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
        m_ccPanel = new CcPanel(ui->toolOptions);                 // Grade
        ui->toolOptions->addWidget(m_ccPanel);
        connect(m_ccPanel, &CcPanel::changed, this, [this](const Platemaker::Models::ColourCorrection& cc) {
            // Live edit: apply it, but do NOT push it back into the panel — the panel is the source
            // here, and re-syncing its widgets mid-drag would fight the slider the user is holding.
            applyGrade(cc);
        });
        connect(m_ccPanel, &CcPanel::committed, this, [this](const Platemaker::Models::ColourCorrection& cc) {
            emit colourCorrectionEdited(cc); // settled: the owner persists it onto the project (undo)
        });
        ui->toolOptions->addWidget(new BubblePanel(ui->toolOptions)); // Bubble
        ui->toolOptions->addWidget(new TextPanel(ui->toolOptions)); // Text

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

void StripViewer::setTool(Tool tool)
{
    m_tool = tool;
    if (auto* b = m_toolGroup->button(static_cast<int>(tool)))
        b->setChecked(true);
    ui->toolOptions->setCurrentIndex(static_cast<int>(tool));
    // Pan == today: hand-drag to pan, and no side panel. Any other tool reveals the panel and frees the
    // left button for tool interaction (drawing/selection lands in later increments).
    const bool pan = (tool == Tool::Pan);
    m_view->setDragMode(pan ? QGraphicsView::ScrollHandDrag : QGraphicsView::NoDrag);
    ui->rightPanel->setVisible(!pan);
}

void StripViewer::applyGrade(const Platemaker::Models::ColourCorrection& cc)
{
    // The owner re-feeds this viewer on every project edit, and the panel persists a settled drag while
    // still emitting live values, so the same grade arrives here repeatedly. Re-grading for it would
    // double the work of a slider drag. processingConfigSignature() is the library's own fingerprint of
    // this exact config — reuse it as the equality test rather than hand-rolling a field compare. (It is
    // empty for any disabled grade, which is right: nothing is graded and the load path is the same one.)
    const std::string sig = Platemaker::Models::processingConfigSignature(cc, {});
    if (sig == m_ccSignature)
        return;
    m_ccSignature = sig;

    // The grade is a point operation on already-built pixels and never changes how a page is read, so
    // no grade edit can invalidate a built page — re-grading the resident ones is always enough.
    m_cc = cc;
    refreshGradePreview();
}

void StripViewer::setColourCorrection(const Platemaker::Models::ColourCorrection& cc)
{
    applyGrade(cc);
    if (m_ccPanel)
        m_ccPanel->setColourCorrection(cc);   // the project is the source here — show it in the panel
}

bool StripViewer::gradeActive() const
{
    // Independent of the active tool: the strip's pixels are the ungraded input, so the project's grade
    // is what the strip is *supposed* to look like — switching to Pan must not reveal an ungraded strip.
    if (!m_cc.enabled)
        return false;
    // A neutral grade leaves the pixels unchanged — nothing to preview.
    return !(m_cc.brightness == 0.0 && m_cc.contrast == 1.0 && m_cc.saturation == 1.0
             && !Platemaker::Models::hasAnyCurve(m_cc.curves));
}

QPixmap StripViewer::gradedOf(int index) const
{
    const QPixmap* p = m_gradedCache.object(index);
    return p ? *p : QPixmap();
}

void StripViewer::produceGraded(int index)
{
    if (!gradeActive() || m_gradedCache.object(index))
        return;
    const QPixmap* src = m_pageCache.object(index); // grade from the resident (ungraded) page
    if (!src)
        return;

    // A page excluded from the grade renders ungraded — the same rule the render applies, and the
    // reason the preview's unit of work is the page: an output slice can straddle an excluded and an
    // included page, so on that feed the exclusion could not be honoured at display time at all.
    const auto& ex  = m_cc.excludedInputUids;
    const auto& uid = m_inputs[static_cast<std::size_t>(m_inputIndex.at(index))].uid;
    if (std::find(ex.begin(), ex.end(), uid) != ex.end())
        return;

    QImage img = src->toImage().convertToFormat(QImage::Format_RGBA8888);
    try {
        // ponytail: grades the whole page (~4 Mpx at 800×5120) even though the viewport shows a
        // fraction of it. Simple and cache-friendly — one grade per page, none while scrolling within
        // it. If a slider drag feels heavy, grade only the visible band: rows are contiguous in an
        // interleaved RGBA buffer, so applyToRgba(bits + top*w*4, w, rows, cc) is already legal.
        Platemaker::Core::ColourCorrector{}.applyToRgba(img.bits(), img.width(), img.height(), m_cc);
    } catch (const std::exception& e) {
        qWarning() << "StripViewer: grade preview failed for page" << index << "—" << e.what();
        return; // leave it ungraded (paint falls back to the built page)
    } catch (...) {
        qWarning() << "StripViewer: grade preview failed for page" << index;
        return;
    }
    const QPixmap g = QPixmap::fromImage(img);
    m_gradedCache.insert(index, new QPixmap(g), qMax(1, (g.width() * g.height() * 4) / 1024));
    if (m_item)
        m_item->update(pageRect(index));
}

void StripViewer::refreshGradePreview()
{
    m_gradedCache.clear();     // the grade changed → previous previews are stale
    if (m_item)
        m_item->update();
    updateVisiblePages();      // re-grade what's on screen (produceGraded runs for visible pages)
}

StripViewer::~StripViewer()
{
    delete ui;
}

void StripViewer::setPreviewSource(const std::vector<Platemaker::Models::InputFile>&     inputs,
                                   const Platemaker::Models::OutputProfile&              outProfile,
                                   const std::vector<Platemaker::Models::CanvasProfile>& canvasProfiles,
                                   const std::vector<std::string>&                       canvasProfileIds,
                                   const QString&                                        cacheDir)
{
    // Everything that can move a page on the strip, and nothing else. The owner refreshes this viewer on
    // any project edit — a settled slider drag included — and a rebuild drops every built page, so an
    // unconditional rebuild would re-fetch the visible pages after each of them. Deliberately excluded:
    // per-input render bookkeeping (status, sha, timestamps), which the render stamps without any page
    // moving. A page edited on disk therefore does not refresh by itself; re-opening the viewer or
    // rendering picks it up.
    QStringList parts;
    parts << QString::fromStdString(Platemaker::Models::outputProfileSignature(outProfile))
          << cacheDir;
    for (const auto& id : canvasProfileIds)
        parts << QString::fromStdString(id);
    for (const auto& cp : canvasProfiles)
        parts << QStringLiteral("%1:%2").arg(QString::fromStdString(cp.id),
                                             QString::fromStdString(
                                                 Platemaker::Models::canvasRenderFingerprint(cp)));
    for (const auto& in : inputs)
        parts << QStringLiteral("%1:%2").arg(
                     QString::fromStdString(in.filePath),
                     in.status == Platemaker::Models::FileStatus::Missing ? QStringLiteral("x")
                                                                         : QStringLiteral("."));
    const QString sig = parts.join(QStringLiteral("/"));

    if (sig == m_feedSignature && !m_pagePaths.isEmpty())
        return;                 // same strip — keep the built pages

    m_feedSignature    = sig;
    m_inputs           = inputs;
    m_outProfile       = outProfile;
    m_canvasProfiles   = canvasProfiles;
    m_canvasProfileIds = canvasProfileIds;
    m_cacheDir         = cacheDir;
    rebuildScene();
}

QRectF StripViewer::pageRect(int index) const
{
    return QRectF(0, m_pageTops.at(index),
                  m_pageSizes.at(index).width(), m_pageSizes.at(index).height());
}

QPixmap StripViewer::pageOf(int index) const
{
    const QPixmap *p = m_pageCache.object(index);
    return p ? *p : QPixmap();
}

QPixmap StripViewer::proxyOf(int index) const
{
    const QPixmap *p = m_proxyCache.object(index);
    return p ? *p : QPixmap();
}

void StripViewer::resetDecodeState()
{
    ++m_generation;             // in-flight results from before now are ignored on arrival
    m_pageCache.clear();
    m_proxyCache.clear();
    m_gradedCache.clear();
    m_pageInFlight.clear();
    m_proxyInFlight.clear();
}

void StripViewer::rebuildScene()
{
    m_scene->clear();           // deletes every item (incl. the StripItem); the tracked pointers are now stale
    m_item = nullptr;
    m_seamItems.clear();
    m_inputIndex.clear();
    m_pagePaths.clear();
    m_pageTops.clear();
    m_pageSizes.clear();
    m_stripWidth  = 0;
    m_stripHeight = 0;
    resetDecodeState();

    // Ask the library where each page lands. This reads headers and decodes nothing, and the numbers
    // come from the same page-domain code a render uses — so the strip laid out here is the strip a
    // render would build, before any render exists.
    std::vector<Platemaker::Core::PagePreviewGeometry> layout;
    try {
        layout = Platemaker::Core::ProcessingPipeline::previewLayout(
            m_inputs, m_outProfile, m_canvasProfiles, m_canvasProfileIds);
    } catch (const std::exception& e) {
        qWarning() << "StripViewer: preview layout failed —" << e.what();
    }

    int y = 0;
    for (int i = 0; i < static_cast<int>(layout.size()); ++i) {
        const auto& g = layout[static_cast<std::size_t>(i)];
        // A page the render would skip contributes nothing to the strip. Dropping it here is what keeps
        // every page below it at the offset the render will give it.
        if (!g.readable || g.width <= 0 || g.height <= 0)
            continue;
        m_inputIndex.append(i);
        m_pagePaths.append(QString::fromStdString(g.sourceFilePath));
        m_pageTops.append(y);
        m_pageSizes.append(QSize(g.width, g.height));
        y            += g.height;
        m_stripWidth  = qMax(m_stripWidth, g.width);
    }
    m_stripHeight = y;

    if (m_pagePaths.isEmpty()) {
        showEmptyState();
        return;
    }

    m_scene->setSceneRect(0, 0, m_stripWidth, m_stripHeight);
    // One item draws every page as its own image — no per-item seam. It pulls pixels lazily from us.
    m_item = new StripItem(this);
    m_scene->addItem(m_item);
    addSeamItems();

    // Default view: 100%. Re-applied on resize until the user zooms.
    m_pendingFit = true;
    applyDefaultZoom();
    updateVisiblePages();       // start building what's on screen
}

void StripViewer::addSeamItems()
{
    // Where the output will be cut. Unlike the page joins, these are not visible in the strip itself,
    // and they are exactly what an author needs to see: a bubble that straddles one lands on both
    // slices. The render cuts every sliceHeight from the top of the strip, so that is what is drawn.
    const int step = m_outProfile.sliceHeight;
    if (step <= 0)
        return;

    // A thin, semi-transparent guide, in the palette's text colour so it reads in both themes without a
    // hardcoded colour.
    QColor c = palette().color(QPalette::WindowText);
    c.setAlpha(90);
    QPen pen(c);
    pen.setCosmetic(true);      // stays 1px regardless of zoom
    for (int top = step; top < m_stripHeight; top += step) {
        auto *seam = m_scene->addLine(0, top, m_stripWidth, top, pen);
        seam->setVisible(ui->buttonSeams->isChecked());
        seam->setZValue(1);     // above the strip item
        m_seamItems.append(seam);
    }
}

void StripViewer::showEmptyState()
{
    m_scene->clear();
    m_item = nullptr;
    m_seamItems.clear();
    m_inputIndex.clear();
    m_pagePaths.clear();
    m_pageTops.clear();
    m_pageSizes.clear();
    m_stripWidth = m_stripHeight = 0;

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

void StripViewer::updateVisiblePages()
{
    if (m_pagePaths.isEmpty())
        return;

    // The viewport mapped into scene coordinates → which pages intersect it.
    const QRectF vis = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
    int first = -1, last = -1;
    for (int i = 0; i < m_pagePaths.size(); ++i) {
        const int top = m_pageTops.at(i);
        const int bot = top + m_pageSizes.at(i).height();
        if (bot >= vis.top() && top <= vis.bottom()) {
            if (first < 0) first = i;
            last = i;
        }
    }
    if (first < 0)
        return;

    first = qMax(0, first - k_prefetchPages);
    last  = qMin(m_pagePaths.size() - 1, last + k_prefetchPages);
    for (int i = first; i <= last; ++i) {
        requestPage(i);
        produceGraded(i); // grade now if already built; otherwise the build watcher grades it on arrival
    }
}

void StripViewer::requestPage(int index)
{
    const int gen = m_generation;

    // Sharp tier: put the input page through the library's page domain on a worker thread. This is the
    // same code the render runs, so the pixels here are the pixels the render will produce — ungraded,
    // because the grade is a point op we apply to the result and re-apply on every slider move.
    if (!m_pageCache.object(index) && !m_pageInFlight.contains(index)) {
        m_pageInFlight.insert(index);
        // Copies, because the worker outlives this call and the workspace can change under it.
        const auto  input   = m_inputs[static_cast<std::size_t>(m_inputIndex.at(index))];
        const auto  outProf = m_outProfile;
        const auto  profs   = m_canvasProfiles;
        const auto  ids     = m_canvasProfileIds;
        const QSize size    = m_pageSizes.at(index);

        auto *watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, index, gen] {
            const QImage img = watcher->result();
            watcher->deleteLater();
            if (gen != m_generation)   // superseded by a rebuild — its in-flight set was already cleared
                return;
            m_pageInFlight.remove(index);
            if (img.isNull())
                return;
            const QPixmap pm = QPixmap::fromImage(img);
            m_pageCache.insert(index, new QPixmap(pm),
                               qMax(1, (pm.width() * pm.height() * 4) / 1024));
            produceGraded(index); // grade the freshly-built page if the grade is on
            if (m_item)
                m_item->update(pageRect(index));
        });
        watcher->setFuture(QtConcurrent::run(
            [input, outProf, profs, ids, size]() -> QImage {
                QImage img(size, QImage::Format_RGBA8888);
                // previewPageRgba writes tightly packed RGBA8888. Format_RGBA8888 is 4 bytes per pixel,
                // so a scanline is always 4-byte aligned and Qt adds no padding — but assert rather than
                // assume, because a padded scanline would shear the image.
                if (img.bytesPerLine() != size.width() * 4)
                    return {};
                try {
                    Platemaker::Core::ProcessingPipeline::previewPageRgba(
                        input, outProf, profs, ids, img.bits(), size.width(), size.height());
                } catch (...) {
                    return {};   // the page stays on its proxy; the layout already knows its size
                }
                return img;
            }));
    }

    // Proxy tier: the input page's thumbnail, reusing the lib ThumbnailCache the Input tab already warms
    // for exactly these files. Aspect-wrong for a margin-cropped page, but it is a placeholder that gets
    // replaced the moment the real page arrives.
    if (!m_cacheDir.isEmpty() && !m_proxyCache.object(index) && !m_proxyInFlight.contains(index)) {
        m_proxyInFlight.insert(index);
        const std::string path     = m_pagePaths.at(index).toStdString();
        const std::string cacheDir = m_cacheDir.toStdString();
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, index, gen] {
            const QString thumbPath = watcher->result();
            watcher->deleteLater();
            if (gen != m_generation)   // superseded by a rebuild — its in-flight set was already cleared
                return;
            m_proxyInFlight.remove(index);
            if (thumbPath.isEmpty())
                return;
            const QPixmap pm(thumbPath);
            if (pm.isNull())
                return;
            m_proxyCache.insert(index, new QPixmap(pm),
                                qMax(1, (pm.width() * pm.height() * 4) / 1024));
            if (m_item)
                m_item->update(pageRect(index));
        });
        watcher->setFuture(QtConcurrent::run([path, cacheDir]() -> QString {
            try {
                Platemaker::Infrastructure::ThumbnailCache cache(cacheDir);
                return QString::fromStdString(cache.getOrGenerate(path));
            } catch (...) {
                return {};
            }
        }));
    }
}

// ---------------------------------------------------------------------------
// Zoom
// ---------------------------------------------------------------------------

void StripViewer::applyZoom(double z)
{
    m_zoom = qBound(0.02, z, 8.0);
    QTransform t;
    t.scale(m_zoom, m_zoom);
    m_view->setTransform(t);
    m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(m_zoom * 100.0)));
    updateVisiblePages();       // zoom changes how many pages are on screen
}

void StripViewer::userZoom(double z)
{
    m_pendingFit = false;   // the user has taken control — stop re-fitting on resize
    applyZoom(z);
}

void StripViewer::zoomIn()  { userZoom(m_zoom * 1.25); }
void StripViewer::zoomOut() { userZoom(m_zoom / 1.25); }
void StripViewer::resetZoom() { userZoom(1.0); }

void StripViewer::applyDefaultZoom()
{
    // Default view is native size — 100% — always. (The former "shrink to fit the viewport width" default
    // computed a tiny zoom while the splitter had not yet given the canvas its real width, so the strip
    // opened at ~3%. 100% is what's wanted regardless; "Fit width" stays available on demand.)
    applyZoom(1.0);
}

void StripViewer::fitWidth()
{
    if (m_stripWidth <= 0)
        return;
    // Explicit user fit — unlike the default, this MAY enlarge past 100% to fill the width.
    const int vw = m_view->viewport()->width() - m_view->verticalScrollBar()->width();
    if (vw > 0)
        userZoom(static_cast<double>(vw) / static_cast<double>(m_stripWidth));
}

void StripViewer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_pendingFit)
        applyDefaultZoom();     // settle the default zoom as the viewport gets its real size
    updateVisiblePages();
}

bool StripViewer::eventFilter(QObject *watched, QEvent *event)
{
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
