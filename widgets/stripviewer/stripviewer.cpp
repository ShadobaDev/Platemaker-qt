#include "stripviewer.h"

#include <platemaker/infrastructure/thumbnail_cache/thumbnail_cache.hpp>

#include <QAction>
#include <QEvent>
#include <QFutureWatcher>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QScrollBar>
#include <QStyleOptionGraphicsItem>
#include <QToolBar>
#include <QTransform>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>

#include <string>
#include <utility>

namespace {

//! LRU caps (in KiB). Full slices are big (native RGBA); proxies are tiny, so many fit. Both bound RAM
//! to roughly the viewport plus the prefetch margin, independent of chapter length.
constexpr int k_fullCacheKiB  = 96 * 1024;   //!< ~24 native 800×1280 slices — visible + prefetch, with headroom.
constexpr int k_proxyCacheKiB = 24 * 1024;   //!< Hundreds of 200px-wide proxies.
constexpr int k_prefetchSlices = 3;          //!< Slices decoded beyond the viewport on each side.

/**
 * @brief One graphics item that draws every output slice as its own image — seam-free.
 *
 * One QGraphicsPixmapItem per slice leaves a 1px hairline at each page join (QGraphicsView clips and
 * rounds each item's edge independently). Drawing all slices through one item, in one painter pass,
 * tiles them edge-to-edge with no seam at any zoom. The item is a thin view over the StripViewer: it
 * owns no pixels — it asks the viewer for each slice's sharp pixmap (if decoded) or its blurry proxy,
 * so the lazy/async machinery lives in one place.
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
        // between two slices is only partially covered and the background hairlines through — that is
        // the "1px frame". AA off makes adjacent slices tile with hard edges, each device row owned by
        // exactly one slice, no bleed. (Local to this item — the view keeps AA for text/seam lines.)
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QRectF exposed = option->exposedRect;
        for (int i = 0; i < m_owner->sliceCount(); ++i) {
            const QRectF r = m_owner->sliceRect(i);
            if (!r.intersects(exposed))
                continue;

            const QPixmap full = m_owner->fullOf(i);
            if (!full.isNull()) {
                painter->drawPixmap(r.topLeft(), full);     // sharp, native size
                continue;
            }
            const QPixmap proxy = m_owner->proxyOf(i);
            if (!proxy.isNull())
                painter->drawPixmap(r, proxy, QRectF(proxy.rect())); // blurry proxy, scaled into the slice rect
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
{
    m_fullCache.setMaxCost(k_fullCacheKiB);
    m_proxyCache.setMaxCost(k_proxyCacheKiB);

    m_scene = new QGraphicsScene(this);
    m_view  = new QGraphicsView(m_scene, this);
    // Hand-drag to pan the big canvas; scrollbars + wheel cover the rest. No hardcoded background —
    // the strip sits on the themed palette (see the "inherit, don't hardcode colours" rule).
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_view->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    m_view->viewport()->installEventFilter(this);   // Ctrl+wheel zoom
    // Decode the slices that scroll into view (plus a prefetch margin).
    connect(m_view->verticalScrollBar(),   &QScrollBar::valueChanged, this, &StripViewer::updateVisibleSlices);
    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, &StripViewer::updateVisibleSlices);

    // Slim top toolbar: zoom controls + seams toggle + "Render & view". The future colour / bubble
    // tools slot in here (or a side panel) without disturbing the view.
    auto *bar = new QToolBar(this);
    bar->setMovable(false);                              // it lives in a layout, not a QMainWindow area
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);     // the actions have no icons — show their text
    bar->addAction(tr("−"), this, &StripViewer::zoomOut);
    m_zoomLabel = new QLabel(QStringLiteral("100%"), this);
    m_zoomLabel->setMinimumWidth(48);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    bar->addWidget(m_zoomLabel);
    bar->addAction(tr("+"),        this, &StripViewer::zoomIn);
    bar->addAction(tr("Fit width"), this, &StripViewer::fitWidth);
    bar->addAction(tr("100%"),     this, &StripViewer::resetZoom);
    bar->addSeparator();
    m_seamsAction = bar->addAction(tr("Seams"));
    m_seamsAction->setCheckable(true);
    m_seamsAction->setToolTip(tr("Show a guide line at each slice boundary."));
    connect(m_seamsAction, &QAction::toggled, this, [this](bool on) {
        for (QGraphicsLineItem *seam : std::as_const(m_seamItems))
            seam->setVisible(on);
    });
    bar->addSeparator();
    bar->addAction(tr("Render && view"), this, [this] { emit renderAndViewRequested(); });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(bar);
    layout->addWidget(m_view, 1);

    showEmptyState();
}

void StripViewer::setSlices(const QStringList &slicePaths, const QString &cacheDir)
{
    m_slicePaths = slicePaths;
    m_cacheDir   = cacheDir;
    rebuildScene();
}

QRectF StripViewer::sliceRect(int index) const
{
    return QRectF(0, m_sliceTops.at(index),
                  m_sliceSizes.at(index).width(), m_sliceSizes.at(index).height());
}

QPixmap StripViewer::fullOf(int index) const
{
    const QPixmap *p = m_fullCache.object(index);
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
    m_fullCache.clear();
    m_proxyCache.clear();
    m_fullInFlight.clear();
    m_proxyInFlight.clear();
}

void StripViewer::rebuildScene()
{
    m_scene->clear();           // deletes every item (incl. the StripItem); the tracked pointers are now stale
    m_item = nullptr;
    m_seamItems.clear();
    m_paths.clear();
    m_sliceTops.clear();
    m_sliceSizes.clear();
    m_stripWidth  = 0;
    m_stripHeight = 0;
    resetDecodeState();

    // Lay the strip out from each slice's HEADER size only — no pixels are decoded here.
    int y = 0;
    for (const QString &path : std::as_const(m_slicePaths)) {
        const QSize s = QImageReader(path).size();   // header read, not a decode
        if (!s.isValid())       // a slice missing / not yet written / unreadable — skip, keep the rest
            continue;
        m_sliceTops.append(y);
        m_paths.append(path);
        m_sliceSizes.append(s);
        y            += s.height();
        m_stripWidth  = qMax(m_stripWidth, s.width());
    }
    m_stripHeight = y;

    if (m_paths.isEmpty()) {
        showEmptyState();
        return;
    }

    m_scene->setSceneRect(0, 0, m_stripWidth, m_stripHeight);
    // One item draws every slice as its own image — no per-item seam. It pulls pixels lazily from us.
    m_item = new StripItem(this);
    m_scene->addItem(m_item);
    addSeamItems();

    // Default view: the output's native width (several slices visible), shrunk only if the strip is
    // wider than the viewport. Re-applied on resize until the user zooms.
    m_pendingFit = true;
    applyDefaultZoom();
    updateVisibleSlices();      // start decoding what's on screen
}

void StripViewer::addSeamItems()
{
    // A thin, semi-transparent guide at each interior slice boundary (skip the top edge at y==0).
    // Painted in the palette's text colour so it reads in both themes without a hardcoded colour.
    QColor c = palette().color(QPalette::WindowText);
    c.setAlpha(90);
    QPen pen(c);
    pen.setCosmetic(true);      // stays 1px regardless of zoom
    for (int top : std::as_const(m_sliceTops)) {
        if (top == 0)
            continue;
        auto *seam = m_scene->addLine(0, top, m_stripWidth, top, pen);
        seam->setVisible(m_seamsAction->isChecked());
        seam->setZValue(1);     // above the strip item
        m_seamItems.append(seam);
    }
}

void StripViewer::showEmptyState()
{
    m_scene->clear();
    m_item = nullptr;
    m_seamItems.clear();
    m_paths.clear();
    m_sliceTops.clear();
    m_sliceSizes.clear();
    m_stripWidth = m_stripHeight = 0;

    auto *text = m_scene->addSimpleText(
        tr("No rendered output yet.\nRender the project to see the full strip."));
    text->setBrush(palette().color(QPalette::WindowText));
    const QRectF b = text->boundingRect();
    m_scene->setSceneRect(b.adjusted(-40, -40, 40, 40));
    resetZoom();
}

// ---------------------------------------------------------------------------
// Lazy decode: proxy (blurry, instant) + full (sharp, async), for visible + prefetch
// ---------------------------------------------------------------------------

void StripViewer::updateVisibleSlices()
{
    if (m_paths.isEmpty())
        return;

    // The viewport mapped into scene coordinates → which slices intersect it.
    const QRectF vis = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
    int first = -1, last = -1;
    for (int i = 0; i < m_paths.size(); ++i) {
        const int top = m_sliceTops.at(i);
        const int bot = top + m_sliceSizes.at(i).height();
        if (bot >= vis.top() && top <= vis.bottom()) {
            if (first < 0) first = i;
            last = i;
        }
    }
    if (first < 0)
        return;

    first = qMax(0, first - k_prefetchSlices);
    last  = qMin(m_paths.size() - 1, last + k_prefetchSlices);
    for (int i = first; i <= last; ++i)
        requestSlice(i);
}

void StripViewer::requestSlice(int index)
{
    const int gen = m_generation;

    // Sharp tier: decode the full slice at native resolution on a worker thread.
    if (!m_fullCache.object(index) && !m_fullInFlight.contains(index)) {
        m_fullInFlight.insert(index);
        const QString path = m_paths.at(index);
        auto *watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, index, gen] {
            const QImage img = watcher->result();
            watcher->deleteLater();
            if (gen != m_generation)   // superseded by a rebuild — its in-flight set was already cleared
                return;
            m_fullInFlight.remove(index);
            if (img.isNull())
                return;
            const QPixmap pm = QPixmap::fromImage(img);
            m_fullCache.insert(index, new QPixmap(pm),
                               qMax(1, (pm.width() * pm.height() * 4) / 1024));
            if (m_item)
                m_item->update(sliceRect(index));
        });
        watcher->setFuture(QtConcurrent::run([path]() -> QImage {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            return reader.read();
        }));
    }

    // Proxy tier: a small blurry thumbnail, reusing the lib ThumbnailCache the render already warmed.
    if (!m_cacheDir.isEmpty() && !m_proxyCache.object(index) && !m_proxyInFlight.contains(index)) {
        m_proxyInFlight.insert(index);
        const std::string path     = m_paths.at(index).toStdString();
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
                m_item->update(sliceRect(index));
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
    updateVisibleSlices();      // zoom changes how many slices are on screen
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
    if (m_stripWidth <= 0)
        return;
    // Leave room for the vertical scrollbar so the fit doesn't force a horizontal one.
    const int vw = m_view->viewport()->width() - m_view->verticalScrollBar()->width();
    // Never enlarge past 100%: keep the output's native width so several slices read at once; shrink
    // only when the strip is wider than the viewport.
    applyZoom(vw > 0 ? qMin(1.0, static_cast<double>(vw) / static_cast<double>(m_stripWidth)) : 1.0);
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
    updateVisibleSlices();
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
