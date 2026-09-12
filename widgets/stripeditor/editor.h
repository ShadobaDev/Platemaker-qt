#ifndef STRIPEDIT_EDITOR_H
#define STRIPEDIT_EDITOR_H

#include <QWidget>
#include <QList>
#include <QPixmap>
#include <QRectF>
#include <QSize>
#include <QString>

#include "layout.h"
#include "pagesource.h"
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
namespace Ui { class Editor; }

namespace StripEdit {

class GradePanel;
class BubblePanel;
class ObjectController;

/**
 * @brief Continuous "infinite strip" editor for a project — the authoring surface for the optional
 *        processing steps (colour grade now, text/bubble overlays next).
 *
 * ## The strip is built from the INPUTS, not from the rendered output
 * The viewer stacks the project's *input pages*, each put through the library's page domain
 * (EXIF-upright → canvas-profile margin crop → scale to the output's target width) by
 * `ProcessingPipeline::layoutPagesFromHeaders` / `decodePageToRgba`. It never reads the committed
 * output slices.
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
 *  - **Layout** comes from `layoutPagesFromHeaders` — a header read per page, no pixels decoded.
 *  - **Proxy tier:** the input page's thumbnail from the lib ThumbnailCache the Input tab already warms
 *    (reused, not reinvented) — drawn instantly so a page is never blank.
 *  - **Sharp tier:** the page is built through the real page domain on a worker thread, only for pages
 *    in view plus a prefetch margin, and kept in a memory-capped LRU cache. Off-screen pages are
 *    evicted, so RAM tracks the viewport, not the chapter length.
 */
class Editor : public QWidget
{
    Q_OBJECT

public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;

    /**
     * @brief Feeds the project's input pages and rebuilds the strip.
     *
     * Lays the strip out through `ProcessingPipeline::layoutPagesFromHeaders`, which reads each page's header
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
    [[nodiscard]] int    pageCount() const { return m_layout.pageCount(); }             //!< Number of drawable pages.
    [[nodiscard]] QRectF pageRect(int index) const { return m_layout.pageRect(index); } //!< Scene rect of page \p index.
    [[nodiscard]] QSize  stripSize() const { return m_layout.stripSize(); }             //!< Whole-strip size (item boundingRect).
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
     * `ProjectItem::addOverlay()`, which also hashes the asset and dedups identical content. Sending
     * the intent instead of a half-built record keeps that inventory the library's.
     *
     * @param artifact       Authoring record for the new bubble (its box is the placement rectangle).
     * @param x,y            Top-left, relative to the anchor page's top edge.
     * @param anchorInputUid The page it was drawn on.
     */
    void artifactCreated(const TextArtifact& artifact, double xFrac, double yFrac, double wFrac,
                         const QString& anchorInputUid);

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

    /**
     * @brief The author picked artwork to bring in — the owner copies it and registers it.
     *
     * Placement is decided here, because only the viewer knows which page is in front of the author;
     * everything after that is the owner's, exactly as it is for a drawn bubble.
     */
    void artworkImportRequested(const QString& sourceFile, double xFrac, double yFrac, double wFrac,
                                const QString& anchorInputUid);

protected:
    //! Ctrl+wheel over the view zooms; a plain wheel keeps the view's native vertical scroll.
    bool eventFilter(QObject *watched, QEvent *event) override;

    //! While the default zoom is still pending, re-applies it as the viewport gets its real size; also
    //! re-evaluates which pages to build.
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildScene();        //!< Lays the feed out via layoutPagesFromHeaders (header reads only) into one lazy StripItem.
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

    //! Adopts a new grade and re-grades the resident pages. Does not touch the Grade panel.
    void applyGrade(const Platemaker::Models::ColourCorrection& cc);

    //! True while a tool that authors overlays is active (Bubble or Text).
    //! Page and anchor geometry is asked of \c m_layout instead — see Layout.
    [[nodiscard]] bool    artifactToolActive() const;

    //! Grade state changed: drop the graded cache and re-grade what's visible.
    void refreshGradePreview();

    Ui::Editor *ui          = nullptr;  //!< Designer form (toolbar buttons + graphics view).
    QGraphicsView  *m_view       = nullptr;  //!< == ui->graphicsView (cached).
    QGraphicsScene *m_scene      = nullptr;
    QGraphicsItem  *m_item       = nullptr;  //!< The single StripItem drawing all pages (owned by the scene).
    QLabel         *m_zoomLabel  = nullptr;  //!< == ui->labelZoom (cached).

    //! Where every drawable page landed (those the render would skip are dropped). Indices into this
    //! key every cache below, and every overlay placement question is asked of it.
    Layout         m_layout;
    QList<QGraphicsLineItem*> m_seamItems; //!< Slice-cut guide lines (owned by the scene).
    double m_zoom        = 1.0;              //!< Absolute zoom factor.
    bool   m_pendingFit  = false;            //!< Re-apply the default zoom on resize until the user zooms.

    //! Where a page's pixels come from: the feed, the proxy/sharp/graded tiers and their caches.
    //! Declared after m_layout because it holds a reference to it.
    PageSource* m_pages = nullptr;

    // --- editor shell: the tool rail's flowing buttons are built in the ctor (a flow layout can't live in
    // a .ui); the splitters, canvas, tool-options stack and artifact list all come from editor.ui ---
    QButtonGroup   *m_toolGroup = nullptr;   //!< Exclusive group of the left rail's tool buttons (id == Tool).
    GradePanel        *m_gradePanel   = nullptr;   //!< The Grade tool-options page (colour-correction controls).
    Tool            m_tool      = Tool::Pan; //!< Current tool.

    // --- the objects on the strip ---
    BubblePanel* m_bubblePanel = nullptr;   //!< Shared tool-options page for both the Bubble and Text tools.
    //! Owns the overlay set, the scene items, the list and the selection. Declared after m_layout,
    //! which it holds by reference.
    ObjectController* m_objects = nullptr;
};

}  // namespace StripEdit

#endif // STRIPEDIT_EDITOR_H
