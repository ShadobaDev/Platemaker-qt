#ifndef STRIPEDIT_EDITOR_H
#define STRIPEDIT_EDITOR_H

#include <QWidget>
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QRectF>
#include <QSize>
#include <QString>

#include "layout.h"
#include "pagesource.h"
#include "textartifact.h"
#include "toolregistry.h"

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
class AdvisoryBar;
class Advisories;

namespace Ui { class Editor; }

namespace StripEdit {

class ColourPair;
class GradePanel;
class ObjectStatePanel;
class StripStatePanel;
class PresetStore;
class ToolOptionsPanel;
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

    /**
     * @brief Selects the tool with registry id @p id: rail button, options page, drag mode, cursor.
     *
     * Everything this does it reads off that tool's row in `tools()`, which is what lets a tool be
     * added without this class hearing about it.
     */
    void setTool(const QString& id);

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
     * @brief Select what an undone or redone step touched, when the feed carrying it arrives.
     *
     * Armed by the owner immediately before that feed, because the objects only become real here when
     * the new state comes back. @p uids is a list: one step can touch several objects, and the first
     * one still standing is the one selected.
     */
    void selectAfterFeed(const QStringList& uids);

    /**
     * @brief Gives this editor its own advisory bar, along the bottom edge, for @p projectUid.
     *
     * It exists because this widget's window is not always the main window: dragged out, the dock is a
     * top level of its own with no status bar, and maximised it covers the one behind it. The chapter's
     * problems would then be invisible in the window where they are worked on.
     *
     * @param registry The application's advisory registry; the bar subscribes and keeps itself current.
     */
    void setAdvisories(Advisories* registry, const QString& projectUid);

    //! Turns that bar off while this editor's window already shows the same advisories elsewhere —
    //! docked, the main window's status bar is saying it. The owner knows; this widget does not.
    void setAdvisoriesActive(bool active);

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

    //! A settled grade edit — the owner (MainWindow) persists it onto the project as one undo step named
    //! @p undoText. Named here, where it is known what was done — an adjustment moved, reset or removed, a
    //! page excluded — rather than guessed afterwards from a before-and-after that cannot tell them apart.
    void colourCorrectionEdited(const Platemaker::Models::ColourCorrection& cc, const QString& undoText);

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
    //! Re-draws the generated rail icons when the theme flips — they are made of palette colours.
    void changeEvent(QEvent* event) override;

    //! While the default zoom is still pending, re-applies it as the viewport gets its real size; also
    //! re-evaluates which pages to build.
    void resizeEvent(QResizeEvent *event) override;

private:
    /**
     * @brief Restores / stores the three splitters' positions across sessions.
     *
     * How wide the tool options are, and how the right column divides between an object's properties
     * and the list of objects, is a working preference — the kind that is infuriating to re-drag every
     * time the editor opens. Application config, not the workspace: it follows the artist, not the comic.
     */
    void restoreSplitterState();
    void storeSplitterState() const;

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
    //! The active tool reads the canvas rather than changing it — the eyedropper.
    [[nodiscard]] bool    isSampling() const;
    //! The active tool spends the colour pair on what it hits — the colour applicator.
    [[nodiscard]] bool    isApplying() const;

    /**
     * @brief Re-decides the viewport cursor for the tool and whatever the pointer is over.
     *
     * Called on hover, after a press is released, when the tool changes, when the zoom changes and after
     * a feed — every moment at which either half of *(tool, target)* can have moved, including the ones
     * where the pointer itself did not.
     *
     * **Nothing else sets the viewport cursor.** The view's drag mode still writes one of its own, and
     * `cursorFor()` answers the same cursor in that state so the two agree rather than take turns.
     */
    void                  updateCursor();
    /**
     * @brief Draws the rail buttons that carry no icon file.
     *
     * Two tools draw their own: one that places a single shape is drawn by the rasteriser that draws
     * that shape, and the colour tool *is* a swatch of the primary colour. Both are made of things that
     * change under the application — the palette, the pair — so they are drawn here rather than once in
     * the constructor, and this runs again whenever either moves.
     */
    void                  refreshGeneratedToolIcons();

    /**
     * @brief Reads the colour at @p scenePos into the pair — the secondary half when @p secondary.
     *
     * Takes what is **drawn**: one composited pixel of the scene — the page through its grade, with every
     * balloon, caption and asset over it — which is the same pixel the render will produce. Selection
     * chrome and the seam guides are left out of that one repaint: they are the editor talking, not the
     * comic. A page still showing its proxy is not sampled — a blurry stand-in would hand back an average
     * of the colours around the point rather than the colour at it — so the page is requested and the
     * press does nothing.
     *
     * @return Whether a colour was taken.
     */
    bool sampleColourAt(const QPointF& scenePos, bool secondary);

    //! Grade state changed: drop the graded cache and re-grade what's visible.
    void refreshGradePreview();

    //! Shows the selected strip or page in ③, with current data — or the object panel for anything else.
    void showSubject();

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
    QButtonGroup   *m_toolGroup = nullptr;   //!< The rail's buttons; a button's id is its row in tools().
    QHash<QString, int> m_toolPage;          //!< Tool id → its page in the options stack.
    ColourPair*     m_colours    = nullptr;  //!< The primary/secondary pair, under the rail. Furniture.
    //! Where the middle-button pan last was, in viewport points; x < 0 when no such pan is in flight.
    QPoint          m_panFrom {-1, -1};
    QPoint          m_pointerPos {-1, -1};   //!< Last hovered viewport point, so the cursor can be
                                             //!< re-decided when the pointer has not moved but the
                                             //!< scene under it has.
    //! The tool tiles' own row in the rail. Held because its minimum height has to follow the flow
    //! layout's wrapping — see `eventFilter()` — or a drag can hide a row of tools.
    QWidget           *m_toolTiles    = nullptr;
    GradePanel        *m_gradePanel   = nullptr;   //!< The Grade tool-options page (colour-correction controls).
    AdvisoryBar       *m_advisoryBar  = nullptr;   //!< Bottom edge of this editor; absent until setAdvisories().
    QString         m_tool;                  //!< The active tool's registry id.

    // --- the objects on the strip ---
    //! Right-top: what the selected object *is*. Inert, and says so, while nothing is selected.
    ObjectStatePanel* m_objectState = nullptr;
    //! The same surface when the strip or one of its pages is selected: what *that* is.
    StripStatePanel*  m_stripState = nullptr;
    //! What the properties stack actually switches between: each panel inside its own scroll area, so a
    //! selection cannot widen the column under the pointer. See `scrolled()` in the .cpp.
    QWidget*          m_objectPage = nullptr;
    QWidget*          m_stripPage  = nullptr;
    /**
     * @brief The grade as this editor last saw it — from the project, or from a live edit in progress.
     *
     * What a page's exclusion is toggled against. Taken from the last thing shown rather than asked of the
     * project, so a toggle can never write back a grade older than the one on screen.
     */
    Platemaker::Models::ColourCorrection m_cc;
    //! Bottom-left, under the tool rail: what the *next* object will be. Never edits anything.
    ToolOptionsPanel* m_toolOptions = nullptr;
    /**
     * @brief The preset library, owned here because more than one thing needs it.
     *
     * The tool's options pick one for the next object; an object's context menu applies one to what is
     * selected. A model reachable only by going through a widget is a model that cannot be reached.
     */
    PresetStore* m_presets = nullptr;
    //! Owns the overlay set, the scene items, the list and the selection. Declared after m_layout,
    //! which it holds by reference.
    ObjectController* m_objects = nullptr;
};

}  // namespace StripEdit

#endif // STRIPEDIT_EDITOR_H
