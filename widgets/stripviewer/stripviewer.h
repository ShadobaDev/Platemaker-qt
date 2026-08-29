#ifndef STRIPVIEWER_H
#define STRIPVIEWER_H

#include <QWidget>
#include <QCache>
#include <QList>
#include <QPixmap>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>

class QGraphicsView;
class QGraphicsScene;
class QGraphicsItem;
class QGraphicsLineItem;
class QLabel;
class QEvent;
class QResizeEvent;

namespace Ui { class StripViewer; }

/**
 * @brief Continuous "infinite strip" view of a project's rendered output slices.
 *
 * Stacks the project's output slices back into the single vertical strip they were cut from, with
 * zoom / fit-width / vertical scroll. The viewer is deliberately a window onto *lib-rendered* pixels
 * (the render is the source of truth) — it never re-derives the image itself. Today it is fed the
 * committed output slices (setSlices); the same surface will later be fed a preview render and grow
 * colour-correction / text-bubble tools on top of the QGraphicsScene.
 *
 * ## Rendering: one item, no seams
 * The strip is a *single* graphics item (StripItem, in the .cpp) that draws each output as its own
 * image. One item per slice would leave a 1px hairline at every page join — QGraphicsView clips and
 * rounds each item's edge independently, so at fractional zoom the boundaries fall between device
 * pixels and the background shows through. Drawing all slices through one item removes that seam at any
 * zoom while still showing the outputs as separate images.
 *
 * ## Memory: proxy + async native decode + prefetch (the "B-minimal" strategy)
 * Decoding every slice up front would cost ~4 MB of RAM per 800×1280 slice — a 100-page chapter would
 * be ~400 MB for pixels only a few of which are on screen. Instead:
 *  - **Layout** is computed from each slice's *header* size only (no decode).
 *  - **Proxy tier:** a small blurry thumbnail per slice, from the lib ThumbnailCache the render already
 *    warms (reused, not reinvented) — drawn instantly so a slice is never blank.
 *  - **Sharp tier:** the full slice is decoded at native resolution on a worker thread (QtConcurrent),
 *    only for slices in view plus a prefetch margin, and kept in a memory-capped LRU cache. When it
 *    arrives it replaces the proxy. Off-screen slices are evicted, so RAM tracks the viewport, not the
 *    chapter length. (Decoding at *display* resolution to also shrink with zoom is a later step.)
 */
class StripViewer : public QWidget
{
    Q_OBJECT

public:
    explicit StripViewer(QWidget *parent = nullptr);
    ~StripViewer() override;

    /**
     * @brief Feeds the ordered rendered slice image paths (in strip order) and rebuilds the strip.
     *
     * An empty list — or a project whose slices are all unreadable — shows the "render first" empty
     * state. Only header sizes are read here; pixels are decoded lazily, per-slice, off the UI thread.
     * @param slicePaths Absolute paths of the output slices, top-to-bottom.
     * @param cacheDir   Workspace .platemaker-cache dir for the proxy thumbnails (empty → no proxies,
     *                   slices show a neutral placeholder until their sharp decode arrives).
     */
    void setSlices(const QStringList &slicePaths, const QString &cacheDir);

    // --- read by StripItem (the single painting item) ---
    [[nodiscard]] int    sliceCount() const { return m_paths.size(); }         //!< Number of drawable slices.
    [[nodiscard]] QRectF sliceRect(int index) const;                           //!< Scene rect of slice \p index.
    [[nodiscard]] QSize  stripSize() const { return {m_stripWidth, m_stripHeight}; } //!< Whole-strip size (item boundingRect).
    [[nodiscard]] QPixmap fullOf(int index) const;   //!< Decoded native slice if cached, else a null pixmap.
    [[nodiscard]] QPixmap proxyOf(int index) const;  //!< Blurry proxy thumbnail if cached, else a null pixmap.

signals:
    //! The "Render & view" button — asks the owner (MainWindow) to (re)render this project; the render's
    //! finish hook then refreshes this viewer. Outputs are cheap/regenerable, so this is safe to offer.
    void renderAndViewRequested();

protected:
    //! Ctrl+wheel over the view zooms; a plain wheel keeps the view's native vertical scroll.
    bool eventFilter(QObject *watched, QEvent *event) override;

    //! While the default zoom is still pending, re-applies it as the viewport gets its real size; also
    //! re-evaluates which slices to decode.
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildScene();        //!< Lays out m_slicePaths (header sizes only) into one lazy StripItem.
    void showEmptyState();      //!< Clears the scene and shows the "no output yet" hint.
    void addSeamItems();        //!< Adds a thin guide line at each slice boundary (toggled by ui->buttonSeams).
    void applyZoom(double z);   //!< Sets the absolute zoom factor (clamped) and updates the % label.
    void applyDefaultZoom();    //!< Native width, shrunk only if the strip is wider than the viewport (never enlarged).
    void userZoom(double z);    //!< A user-initiated zoom: applies it and ends the pending default-zoom follow.
    void zoomIn();
    void zoomOut();
    void resetZoom();           //!< 100%.
    void fitWidth();            //!< Scales so the whole strip width fits the viewport (may enlarge past 100%).

    //! Requests decode of every slice in view plus a prefetch margin. Called on scroll / zoom / resize.
    void updateVisibleSlices();
    //! Kicks off the async proxy + full decode for one slice (no-op if already cached / in flight).
    void requestSlice(int index);
    //! Discards all cached/in-flight decodes and bumps the generation so stale results are ignored.
    void resetDecodeState();

    Ui::StripViewer *ui          = nullptr;  //!< Designer form (toolbar buttons + graphics view).
    QGraphicsView  *m_view       = nullptr;  //!< == ui->graphicsView (cached).
    QGraphicsScene *m_scene      = nullptr;
    QGraphicsItem  *m_item       = nullptr;  //!< The single StripItem drawing all slices (owned by the scene).
    QLabel         *m_zoomLabel  = nullptr;  //!< == ui->labelZoom (cached).

    QStringList  m_slicePaths;               //!< Raw feed as given to setSlices (may include unreadable entries).
    QString      m_cacheDir;                 //!< Proxy-thumbnail cache dir (the workspace .platemaker-cache).

    // Filtered, drawable slices (unreadable entries dropped) — indices here key every cache/in-flight set.
    QStringList  m_paths;                    //!< Absolute path of each drawable slice.
    QList<int>   m_sliceTops;                //!< Cumulative Y offset per drawable slice (also: lookup/bubble anchor).
    QList<QSize> m_sliceSizes;               //!< Native size per drawable slice (from the header).
    QList<QGraphicsLineItem*> m_seamItems;   //!< Boundary guide lines (owned by the scene).
    int    m_stripWidth  = 0;                //!< Widest slice = strip width.
    int    m_stripHeight = 0;                //!< Sum of slice heights.
    double m_zoom        = 1.0;              //!< Absolute zoom factor.
    bool   m_pendingFit  = false;            //!< Re-apply the default zoom on resize until the user zooms.

    // --- async decode state ---
    QCache<int, QPixmap> m_fullCache;        //!< Decoded native slices, LRU-evicted under a byte cap.
    QCache<int, QPixmap> m_proxyCache;       //!< Blurry proxy thumbnails, LRU-evicted under a byte cap.
    QSet<int>            m_fullInFlight;      //!< Slice indices whose full decode is running.
    QSet<int>            m_proxyInFlight;     //!< Slice indices whose proxy load is running.
    int                  m_generation = 0;   //!< Bumped on every rebuild; async results from an older gen are dropped.
};

#endif // STRIPVIEWER_H
