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

#include "textartifact.h"

#include <platemaker/core/processing_pipeline/processing_pipeline.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/processing_steps.hpp>
#include <platemaker/models/project_item.hpp>

#include <string>
#include <vector>

class QGraphicsView;
class QGraphicsScene;
class QGraphicsItem;
class QGraphicsLineItem;
class QLabel;
class QEvent;
class QResizeEvent;
class QAction;
class QButtonGroup;
class QGraphicsRectItem;
class QListWidgetItem;
class CcPanel;
class BubblePanel;
class OverlayItem;

namespace Ui { class StripViewer; }

/**
 * @brief Continuous "infinite strip" editor for a project — the authoring surface for the optional
 *        processing steps (colour grade now, text/bubble overlays next).
 *
 * ## The strip is built from the INPUTS, not from the rendered output
 * The viewer stacks the project's *input pages*, each put through the library's page domain
 * (EXIF-upright → canvas-profile margin crop → scale to the output's target width) by
 * `ProcessingPipeline::previewLayout` / `previewPageRgba`. It never reads the committed output slices.
 * Three things follow, and they are the whole reason for the design:
 *  - **It works before the first render.** There is nothing to view otherwise, and a grade has to be
 *    authored before it is baked, not after.
 *  - **The grade is applied relative to the input**, so rendering the project does not change what the
 *    viewer shows. Feeding on committed output meant the render baked the grade in and the preview then
 *    graded it a second time.
 *  - **Per-page exclusions are expressible.** The unit of work here is the page, exactly the unit the
 *    grade's `excludedInputUids` addresses; an output slice can straddle an excluded and an included
 *    page, so on that feed the exclusion has no meaning at display time.
 *
 * Slices are deliberately absent: they are an *output* artifact (files to publish). A viewer draws a
 * continuous strip and hides the joins anyway, so cutting the preview into them would buy nothing. The
 * slice grid still matters to the author — that is what the seam guides draw, at every slice height.
 *
 * ## Rendering: one item, no seams
 * The strip is a *single* graphics item (StripItem, in the .cpp) that draws each page as its own image.
 * One item per page would leave a 1px hairline at every join — QGraphicsView clips and rounds each
 * item's edge independently, so at fractional zoom the boundaries fall between device pixels and the
 * background shows through. Drawing all pages through one item removes that seam at any zoom.
 *
 * ## Memory: proxy + async page build + prefetch
 * A scaled page is far bigger than a slice (~16 MB at 800×5120), and a chapter has many, so pages are
 * brought online lazily:
 *  - **Layout** comes from `previewLayout` — a header read per page, no pixels decoded.
 *  - **Proxy tier:** the input page's thumbnail from the lib ThumbnailCache the Input tab already warms
 *    (reused, not reinvented) — drawn instantly so a page is never blank.
 *  - **Sharp tier:** the page is built through the real page domain on a worker thread, only for pages
 *    in view plus a prefetch margin, and kept in a memory-capped LRU cache. Off-screen pages are
 *    evicted, so RAM tracks the viewport, not the chapter length.
 */
class StripViewer : public QWidget
{
    Q_OBJECT

public:
    explicit StripViewer(QWidget *parent = nullptr);
    ~StripViewer() override;

    /**
     * @brief Feeds the project's input pages and rebuilds the strip.
     *
     * Lays the strip out through `ProcessingPipeline::previewLayout`, which reads each page's header
     * and decodes nothing; pixels are built lazily, per page, off the UI thread. Pages the render would
     * skip (missing or unreadable) are dropped here exactly as the render drops them, so the preview's
     * page offsets match what a render produces.
     *
     * @param inputs           The project's inputs in strip order (`ProjectItem::inputsInOrder()`).
     * @param outProfile       The project's resolved output profile — supplies the target width every
     *                         page is scaled to, and the slice height the seam guides mark.
     * @param canvasProfiles   The workspace's canvas-profile palette (margins).
     * @param canvasProfileIds The profiles linked to this project, in priority order.
     * @param cacheDir         Workspace `.platemaker-cache` for the proxy thumbnails (empty → no
     *                         proxies; pages show a neutral placeholder until their build arrives).
     */
    void setPreviewSource(const std::vector<Platemaker::Models::InputFile>&     inputs,
                          const Platemaker::Models::OutputProfile&              outProfile,
                          const std::vector<Platemaker::Models::CanvasProfile>& canvasProfiles,
                          const std::vector<std::string>&                       canvasProfileIds,
                          const QString&                                        cacheDir);

    //! The editor tools on the left rail. Pan = plain viewing (hand-drag, no side panel); the others reveal
    //! the right panel. Bubble/Text gain real controls in a later increment.
    enum class Tool { Pan, Grade, Bubble, Text };

    //! Selects the active tool: checks its rail button, swaps the options page, sets the drag mode and
    //! shows/hides the right panel.
    void setTool(Tool tool);

    /**
     * @brief Feeds the project's text/bubble overlays and their authoring records.
     *
     * Overlays are placed in the **page domain**: each carries the uid of the input page it rides on
     * (`StripOverlay::anchorInputUid`) plus an offset from that page's top, and this viewer resolves the
     * pair against the strip it just laid out — the same arithmetic `ProcessingPipeline::run()` does, so
     * the preview cannot disagree with the render about where a bubble lands. An overlay whose anchor
     * page is not in the strip is shown greyed and marked orphaned rather than dropped, because the
     * project still holds it and it returns the moment its page does.
     *
     * Items are reconciled by uid rather than rebuilt, so a re-feed after an edit keeps the selection
     * and does not interrupt an interaction.
     */
    void setOverlaySource(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                          const ArtifactMap&                                  artifacts);

    /**
     * @brief Feeds the project's colour grade to the Grade panel and the live preview.
     *
     * The strip's pixels are ungraded by construction, so this always previews cleanly — before a
     * render and after one alike. No grade edit ever re-reads a file: the colour step does not change
     * how a page is read, so the resident pixels stay a valid baseline for every grade tried on them.
     */
    void setColourCorrection(const Platemaker::Models::ColourCorrection& cc);

    // --- read by StripItem (the single painting item) ---
    [[nodiscard]] int    pageCount() const { return m_pagePaths.size(); }             //!< Number of drawable pages.
    [[nodiscard]] QRectF pageRect(int index) const;                                   //!< Scene rect of page \p index.
    [[nodiscard]] QSize  stripSize() const { return {m_stripWidth, m_stripHeight}; }  //!< Whole-strip size (item boundingRect).
    [[nodiscard]] QPixmap pageOf(int index) const;   //!< Built (ungraded) page if cached, else a null pixmap.
    [[nodiscard]] QPixmap proxyOf(int index) const;  //!< Blurry proxy thumbnail if cached, else a null pixmap.
    [[nodiscard]] bool    gradeActive() const;       //!< True when the live grade preview should be shown.
    [[nodiscard]] QPixmap gradedOf(int index) const; //!< Graded preview of page \p index if cached, else null.

signals:
    //! The "Render & view" button — asks the owner (MainWindow) to (re)render this project. The strip
    //! shows the same pixels before and after, so this is about producing the output files, not the view.
    void renderAndViewRequested();

    //! A settled grade edit in the CC panel — the owner (MainWindow) persists it onto the project (undo).
    void colourCorrectionEdited(const Platemaker::Models::ColourCorrection& cc);

    /**
     * @brief A new bubble was drawn — the owner rasterises it and registers it with the library.
     *
     * Creation is the one thing this viewer cannot finish on its own: the uid is minted by
     * `ProjectItem::addOverlay()`, which also hashes the bitmap and dedups identical content. Sending
     * the intent instead of a half-built record keeps that inventory the library's.
     *
     * @param artifact       Authoring record for the new bubble (its box is the placement rectangle).
     * @param x,y            Top-left, relative to the anchor page's top edge.
     * @param anchorInputUid The page it was drawn on.
     */
    void artifactCreated(const TextArtifact& artifact, int x, int y, const QString& anchorInputUid);

    /**
     * @brief Every other overlay edit, as the complete new state: move, restyle, delete, reorder, mute.
     *
     * One channel rather than one signal per gesture — the uids already exist, so the owner only has to
     * store what it is given (re-rasterising the artifacts whose bitmaps no longer match) and push one
     * undo step labelled @p undoText.
     */
    void overlaysEdited(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                        const ArtifactMap&                                  artifacts,
                        const QString&                                      undoText);

protected:
    //! Ctrl+wheel over the view zooms; a plain wheel keeps the view's native vertical scroll.
    bool eventFilter(QObject *watched, QEvent *event) override;

    //! While the default zoom is still pending, re-applies it as the viewport gets its real size; also
    //! re-evaluates which pages to build.
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildScene();        //!< Lays the feed out via previewLayout (header reads only) into one lazy StripItem.
    void showEmptyState();      //!< Clears the scene and shows the "no pages yet" hint.
    void addSeamItems();        //!< Adds a guide line at each slice cut (every sliceHeight down the strip).
    void applyZoom(double z);   //!< Sets the absolute zoom factor (clamped) and updates the % label.
    void applyDefaultZoom();    //!< 100%.
    void userZoom(double z);    //!< A user-initiated zoom: applies it and ends the pending default-zoom follow.
    void zoomIn();
    void zoomOut();
    void resetZoom();           //!< 100%.
    void fitWidth();            //!< Scales so the whole strip width fits the viewport (may enlarge past 100%).

    //! Requests a build of every page in view plus a prefetch margin. Called on scroll / zoom / resize.
    void updateVisiblePages();
    //! Kicks off the async proxy + page build for one page (no-op if already cached / in flight).
    void requestPage(int index);
    //! Discards all cached/in-flight pages and bumps the generation so stale results are ignored.
    void resetDecodeState();

    //! Adopts a new grade and re-grades the resident pages. Does not touch the CC panel.
    void applyGrade(const Platemaker::Models::ColourCorrection& cc);

    // --- overlays (text & bubbles) ---
    void onOverlayGeometryEdited(const QString& uid); //!< An item settled a move/resize/tail drag.
    void syncOverlayItems();        //!< Reconciles the scene items with m_overlays, by uid.
    void refreshArtifactList();     //!< Rebuilds the right-bottom list from m_overlays (composite order).
    void selectOverlay(const QString& uid);   //!< Selects one in the scene and the list, and loads the panel.
    void pushOverlays(const QString& undoText); //!< Emits overlaysEdited() with the current state.
    void applyPanelArtifact(const TextArtifact& a, bool commit); //!< Live edit from the panel → item (+persist).
    void deleteSelectedOverlay();
    void duplicateSelectedOverlay();   //!< Copies the selected bubble a little down and right.
    void setOverlayEnabled(const QString& uid, bool on);  //!< The list's mute checkbox (deferred, see the ctor).
    void commitListOrder();                               //!< Adopts the list's row order as composite order.
    void beginPlacement(const QPointF& scenePos);   //!< Bubble/Text tool: start the placement rubber band.
    void updatePlacement(const QPointF& scenePos);
    void finishPlacement();                          //!< Emits artifactCreated() for the drawn rectangle.

    //! Index of the drawable page containing strip-Y \p y, or -1 when it falls outside every page.
    [[nodiscard]] int     pageAtSceneY(qreal y) const;
    //! Input uid of drawable page \p page (empty when out of range).
    [[nodiscard]] QString anchorUidForPage(int page) const;
    //! Drawable page carrying input uid \p uid, or -1 — which is what makes an overlay an orphan.
    [[nodiscard]] int     pageForAnchor(const QString& uid) const;
    //! Scene position of \p o, resolving its page anchor against the current layout.
    [[nodiscard]] QPointF scenePosOf(const Platemaker::Models::StripOverlay& o) const;
    //! True while a tool that authors overlays is active (Bubble or Text).
    [[nodiscard]] bool    artifactToolActive() const;

    //! Grade the built page \p index into the graded-preview cache (no-op if grade inactive / not built).
    void produceGraded(int index);
    //! Grade state changed: drop the graded cache and re-grade what's visible.
    void refreshGradePreview();

    Ui::StripViewer *ui          = nullptr;  //!< Designer form (toolbar buttons + graphics view).
    QGraphicsView  *m_view       = nullptr;  //!< == ui->graphicsView (cached).
    QGraphicsScene *m_scene      = nullptr;
    QGraphicsItem  *m_item       = nullptr;  //!< The single StripItem drawing all pages (owned by the scene).
    QLabel         *m_zoomLabel  = nullptr;  //!< == ui->labelZoom (cached).

    // --- the feed: everything the lib needs to put one input page through the page domain ---
    std::vector<Platemaker::Models::InputFile>     m_inputs;           //!< Project inputs in strip order.
    Platemaker::Models::OutputProfile              m_outProfile;       //!< Target width + slice height.
    std::vector<Platemaker::Models::CanvasProfile> m_canvasProfiles;   //!< Workspace palette (margins).
    std::vector<std::string>                       m_canvasProfileIds; //!< Profiles linked to this project.
    QString                                        m_cacheDir;         //!< Proxy-thumbnail cache dir.
    QString                                        m_feedSignature;    //!< Fingerprint of the feed above — a re-feed that matches it keeps the built pages.

    // Drawable pages (those the render would also skip are dropped) — indices here key every cache.
    QList<int>          m_inputIndex;   //!< Index into m_inputs for each drawable page.
    QStringList         m_pagePaths;    //!< Source path of each drawable page (proxy lookup + diagnostics).
    QList<int>          m_pageTops;     //!< Cumulative Y offset per drawable page (also: overlay anchor).
    QList<QSize>        m_pageSizes;    //!< Scaled size per page, from previewLayout.
    QList<QGraphicsLineItem*> m_seamItems; //!< Slice-cut guide lines (owned by the scene).
    int    m_stripWidth  = 0;                //!< Widest page = strip width.
    int    m_stripHeight = 0;                //!< Sum of page heights.
    double m_zoom        = 1.0;              //!< Absolute zoom factor.
    bool   m_pendingFit  = false;            //!< Re-apply the default zoom on resize until the user zooms.

    // --- async build state ---
    QCache<int, QPixmap> m_pageCache;        //!< Built (ungraded) pages, LRU-evicted under a byte cap.
    QCache<int, QPixmap> m_proxyCache;       //!< Blurry proxy thumbnails, LRU-evicted under a byte cap.
    QSet<int>            m_pageInFlight;     //!< Page indices whose build is running.
    QSet<int>            m_proxyInFlight;    //!< Page indices whose proxy load is running.
    int                  m_generation = 0;   //!< Bumped on every rebuild; async results from an older gen are dropped.

    // --- editor shell: the tool rail's flowing buttons are built in the ctor (a flow layout can't live in
    // a .ui); the splitters, canvas, tool-options stack and artifact list all come from stripviewer.ui ---
    QButtonGroup   *m_toolGroup = nullptr;   //!< Exclusive group of the left rail's tool buttons (id == Tool).
    CcPanel        *m_ccPanel   = nullptr;   //!< The Grade tool-options page (colour-correction controls).
    Tool            m_tool      = Tool::Pan; //!< Current tool.

    // --- colour correction ---
    Platemaker::Models::ColourCorrection m_cc;            //!< Current grade (from the project / the panel).
    std::string                          m_ccSignature;   //!< Fingerprint of m_cc — a grade that matches it is ignored.
    QCache<int, QPixmap>                 m_gradedCache;   //!< Graded preview of visible pages; cleared on grade change.

    // --- text & bubbles ---
    BubblePanel* m_bubblePanel = nullptr;   //!< Shared tool-options page for both the Bubble and Text tools.
    std::vector<Platemaker::Models::StripOverlay> m_overlays;   //!< The project's overlays, in composite order.
    ArtifactMap                                   m_artifacts;  //!< Their authoring records, keyed by overlay uid.
    QHash<QString, OverlayItem*>                  m_overlayItems; //!< Live scene items, keyed by overlay uid.
    QString            m_selectedOverlay;                   //!< uid of the selected overlay, empty for none.
    // Duplicate / Delete, shared by the artifact list's context menu and its keyboard shortcuts, and
    // reachable from the canvas too — the two places a bubble is ever selected.
    QAction*           m_actDuplicate    = nullptr;
    QAction*           m_actDelete       = nullptr;
    QGraphicsRectItem* m_placementRubber = nullptr;         //!< Rubber band while a new bubble is drawn.
    QPointF            m_placementOrigin;                   //!< Where that drag started, in scene coordinates.
    bool               m_placing         = false;
    bool               m_syncingList     = false;           //!< Guards the list ⇄ scene selection round-trip.
    //! Set when this viewer asked for a new bubble; the uid only exists after the owner mints it, so the
    //! selection has to wait for the feed to come back.
    bool               m_selectNewOverlay = false;
};

#endif // STRIPVIEWER_H
