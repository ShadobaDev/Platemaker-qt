#ifndef STRIPEDIT_OBJECTCONTROLLER_H
#define STRIPEDIT_OBJECTCONTROLLER_H

#include <QHash>
#include <QSet>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringList>

#include "cursors.h"
#include "textartifact.h"

#include <platemaker/models/project_item.hpp>

#include <vector>

class QAction;
class QGraphicsRectItem;
class QGraphicsScene;
class QGraphicsView;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QMenu;
class QTransform;
class QWidget;

namespace StripEdit {

class ObjectStatePanel;
class PresetStore;
class ToolOptionsPanel;
class Layout;
class Object;

/**
 * @brief Everything the author *places* on the strip: the objects, the list, the selection, the drag.
 *
 * One sentence, no "and": it owns the overlay set and keeps three views of it in agreement — the scene
 * items, the composite-order list, and the properties panel showing whichever one is selected. The
 * editor above it owns the canvas and the tools; it hands this class the mouse while a placement drag
 * is in flight, and tells it one thing about the active tool — whether a placement gets a balloon.
 *
 * It **owns no persistence**. Every mutation is announced on one of the four signals below and only
 * becomes real when the owner writes it and feeds the new state back through setSource() — which is
 * why a new object's uid does not exist until the round trip completes (see \c m_selectNewOverlay).
 *
 * The strip's scene *is* the strip at 1:1, so an overlay's scene position is its library placement plus
 * its anchor page's top — no coordinate mapping layer, and the preview lands exactly where the render
 * will put it.
 */
class ObjectController : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief What kind of thing is selected.
     *
     * An overlay keeps its uid in the selection as it always has. The strip and its pages are not
     * overlays — nothing about them is placed, styled or composited — so they are told apart here rather
     * than squeezed into an overlay's uid.
     */
    enum class Subject { None, Overlay, Strip, Page, Tail };

    /**
     * @brief Wires itself to the collaborators it drives; it owns none of them.
     *
     * @param scene        Where the objects are drawn (the editor's canvas scene).
     * @param view         Needed for hit-testing and for "where is the author looking" on import.
     * @param list         The right-bottom list: composite order, mute toggles, selection.
     * @param panel        The selected object's properties — edited, and told what is selected.
     * @param defaults     The tool's own options, read (never written) for what a new object starts as.
     * @param layout       Page geometry, owned by the editor; every placement question is asked of it.
     * @param dialogParent Parent for the file/message dialogs this raises.
     */
    ObjectController(QGraphicsScene* scene, QGraphicsView* view, QTreeWidget* list,
                     ObjectStatePanel* panel, ToolOptionsPanel* defaults, PresetStore& presets,
                     const Layout& layout,
                     QWidget* dialogParent, QObject* parent = nullptr);

    //! Adopts the owner's complete state after an edit round-trips back.
    void setSource(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                   const ArtifactMap& artifacts);

    /**
     * @brief Select one of @p uids — the first that still exists — when the next feed arrives.
     *
     * How an undone or redone step says *what* it changed, the way the dock it lands in says *where*.
     * A list rather than a uid because one step can touch several objects, and because the objects a
     * step touched are not necessarily still there: undoing a placement, or redoing a delete, leaves
     * nothing to select and the selection is cleared instead of left pointing at a ghost.
     *
     * One-shot, and armed immediately before the feed it applies to.
     */
    void selectAfterFeed(const QStringList& uids) { m_selectAfterFeed = uids; }

    //! Selects the strip itself — what every page sits in, and what a grade is applied to.
    void selectStrip();
    //! Selects page @p inputUid of the strip.
    void selectPage(const QString& inputUid);
    //! Selects tail @p index of bubble @p uid — or the bubble, when it has no tail at that position.
    void selectTail(const QString& uid, int index);
    [[nodiscard]] Subject        subject() const { return m_subject; }
    //! Everything selected, in the order it was picked. The last is the primary — see selectOverlays().
    [[nodiscard]] const QStringList& selectedOverlays() const { return m_selectedOverlays; }
    [[nodiscard]] const QString& selectedPage() const { return m_selectedPage; }   //!< When subject() is Page.
    [[nodiscard]] int            selectedTail() const { return m_selectedTail; }   //!< When subject() is Tail.

    //! The pages the grade skips, so their rows can say so. Touches the rows only when the set changed.
    void setExcludedPages(const QSet<QString>& inputUids);

    /**
     * @brief Paints @p colour onto whatever is drawn at @p scenePos.
     *
     * The colour tool's canvas face, and the same rule the eyedropper reads by: **what is under the
     * pointer is what changes.** Point at the lettering and the lettering takes it; at the outline and
     * the outline does; anywhere inside the balloon and it is the fill. A point on a balloon's box but
     * outside its silhouette paints nothing, and imported artwork takes nothing — it is pixels somebody
     * else drew. The tool therefore needs no "what am I painting" setting: the picture is the setting.
     *
     * The object it lands on becomes the **selection**, so ③ shows what just changed and the same edit
     * is one Ctrl+Z away. One undo step per press, named after what it painted.
     *
     * @return Whether anything was painted.
     */
    bool applyColourAt(const QPointF& scenePos, const QTransform& deviceTransform, const QColor& colour);

    //! Rebuilds *Apply preset ▸* from the store, so a preset saved a moment ago is already there.
    void rebuildPresetMenu();
    //! Restyles the selected bubble with preset \p index, keeping what it says and where it points.
    void applyPresetToSelection(int index);

    //! Rebuilds *Re-anchor to ▸* from the layout: one entry per page, the current one checked.
    void rebuildReanchorMenu();
    //! Moves the selected object onto page @p pageUid, at the same offset from that page's top.
    void reanchorSelection(const QString& pageUid);

    void syncItems();    //!< Reconciles the scene items with the overlay set, by uid.
    void refreshList();  //!< Rebuilds the list from the overlay set (composite order).
    void reselect();     //!< Re-applies the current selection after the scene was rebuilt.

    //! While set, the list ⇄ scene selection callbacks are ignored: a programmatic rebuild is not a
    //! user action, and \c QGraphicsScene::clear() drops the selection on its way through.
    void setSyncing(bool on) { m_syncingList = on; }
    //! The scene deleted every item it owned; drop the now-dangling pointers without touching them.
    void forgetItems() { m_overlayItems.clear(); }

    // --- placement, driven by the editor's event filter ---
    [[nodiscard]] bool isPlacing() const { return m_placing; }
    //! True when an existing object sits under \p scenePos — the editor leaves that press to the item.
    [[nodiscard]] bool objectAt(const QPointF& scenePos, const QTransform& deviceTransform) const;

    //! What the pointer is over at @p scenePos, for whoever has to decide a cursor or a gesture.
    [[nodiscard]] PointerTarget pointerTargetAt(const QPointF& scenePos,
                                                const QTransform& deviceTransform) const;
    void beginPlacement(const QPointF& scenePos);
    void updatePlacement(const QPointF& scenePos);
    void finishPlacement();

signals:
    //! A bubble was drawn. Creation is the library's — it mints the uid and dedups identical artwork.
    void artifactCreated(const TextArtifact& artifact, double xFrac, double yFrac, double wFrac,
                         const QString& anchorInputUid);
    //! Any other edit, as the complete new state: one channel rather than one signal per gesture.
    void overlaysEdited(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                        const ArtifactMap& artifacts, const QString& undoText);
    //! Artwork drawn elsewhere should be copied into the workspace and registered at this placement.
    void artworkImportRequested(const QString& sourceFile, double xFrac, double yFrac, double wFrac,
                                const QString& anchorInputUid);
    //! The selection moved to @p subject. @p uid is the overlay's uid or the page's input uid, and empty
    //! for the strip and for nothing. Which panel shows that subject is the editor's to decide.
    void subjectChanged(StripEdit::ObjectController::Subject subject, const QString& uid);
private:
    void onOverlayGeometryEdited(const QString& uid); //!< An item settled a move/resize/tail drag.
    void onObjectPressed(const QString& uid, int handle); //!< A press on a tail's handle selects that tail.
    /**
     * @brief How much bigger than its own artwork an overlay is drawn.
     *
     * \c 1.0 whenever the asset was authored at the width the strip is laid out at now, which is every
     * overlay until a chapter is re-profiled. After one, the record's \c wFrac still says how wide the
     * object is *relative to the page*, and this is what turns that back into a scale for the item.
     */
    [[nodiscard]] qreal itemScaleFor(const Platemaker::Models::StripOverlay& o, qreal naturalWidth) const;
    //! The library's rasterisation of \p a, cached by the SVG it emits. Empty if it cannot be produced.
    [[nodiscard]] QImage sharpRasterFor(const TextArtifact& a);
    void selectOverlay(const QString& uid);   //!< Selects one in the scene and the list, and loads the panel.

    /**
     * @brief Selects exactly @p uids, in the order given. The **last** is the primary.
     *
     * The primary is what a single-subject action acts on and what ③ shows when there is only one —
     * "the thing I just clicked", which is the last one picked. Everything written before the selection
     * was a set still reads selectedOverlay(), which is now that primary.
     */
    void selectOverlays(const QStringList& uids);
    //! Selects the strip or a page: every overlay deselected, that one row selected, the subject reported.
    void selectSubject(Subject subject, const QString& pageUid);
    //! The tree row of the selected strip or page, or nullptr.
    [[nodiscard]] QTreeWidgetItem* subjectRow() const;
    //! The tree row of tail @p index of bubble @p uid, or nullptr.
    [[nodiscard]] QTreeWidgetItem* tailRow(const QString& uid, int index) const;
    void pushOverlays(const QString& undoText); //!< Emits overlaysEdited() with the current state.
    //! Live edit from the panel → item (+persist, as a step named @p undoText or for the subject).
    void applyPanelArtifact(const TextArtifact& a, bool commit, const QString& undoText = QString());
    void deleteSelectedOverlay();
    void deleteSelectedTail();   //!< Takes the selected tail off its balloon, and selects the balloon.
    void importArtwork();           //!< Asks for a file and drops it on the page currently in view.
    void duplicateSelectedOverlay();   //!< Copies the selected bubble a little down and right.
    void setOverlayEnabled(const QString& uid, bool on);  //!< The list's mute checkbox (deferred, see the ctor).
    void commitListOrder();                               //!< Adopts the list's row order as composite order.

    // --- collaborators, not owned ---
    QGraphicsScene* m_scene        = nullptr;
    QGraphicsView*  m_view         = nullptr;
    //! The object stack. A tree, so that objects can nest under the objects they belong to; for now
    //! every row is top level and it behaves exactly as the list it replaced.
    QTreeWidget*    m_list         = nullptr;
    ObjectStatePanel* m_objectState = nullptr;
    ToolOptionsPanel* m_toolOptions = nullptr;  //!< Read for prototype(); never edited from here.
    PresetStore&      m_presets;
    //! *Apply preset ▸* on the selection. Restyling something that exists is a different act from
    //! choosing what the next object will be, so it lives with the object rather than with the tool.
    QMenu*            m_presetMenu = nullptr;
    //! *Re-anchor to ▸* on the selection — the explicit way back for an object whose page is gone, and
    //! the only way to move one that is not on the strip, where there is nothing to drag.
    QMenu*            m_reanchorMenu = nullptr;
    const Layout&   m_layout;
    QWidget*        m_dialogParent = nullptr;

    std::vector<Platemaker::Models::StripOverlay> m_overlays;   //!< The project's overlays, in composite order.
    ArtifactMap                                   m_artifacts;  //!< Their authoring records, keyed by overlay uid.
    QHash<QString, Object*>                       m_overlayItems; //!< Live scene objects, keyed by overlay uid.
    /**
     * @brief Library rasterisations of styled bubbles, keyed by the SVG document itself.
     *
     * Keyed by the bytes rather than by the overlay's stored hash, and rendered from those same bytes
     * rather than from the file — because the file is the wrong thing to ask. A workspace can live on a
     * synced drive, where reading a file back immediately after writing it may still return the previous
     * content; keying and rendering off the buffer in hand makes the preview show what is being edited,
     * and leaves the file to matter only when a render reads it.
     *
     * Only styled bubbles are in here — an unstyled one is the same geometry either way, so rasterising
     * it would buy nothing.
     * ponytail: rasterised synchronously, on the UI thread. One bubble per settled edit is a few ms;
     * opening a chapter with dozens of styled bubbles is the case that would want QtConcurrent.
     */
    QHash<QString, QImage>                        m_sharpCache;
    QString            m_selectedOverlay;                   //!< The primary: last of m_selectedOverlays.
    QStringList        m_selectedOverlays;                  //!< Everything selected, in pick order.
    Subject            m_subject = Subject::None;           //!< What the selection is.
    QString            m_selectedPage;                      //!< Input uid of the selected page, when a page is.
    int                m_selectedTail      = -1;            //!< Index of the selected tail, when a tail is.
    int                m_selectedTailCount = 0;             //!< How many tails its bubble had when it was selected.
    QSet<QString>      m_excludedPages;                     //!< Pages the grade skips — said on their rows.
    //! Which kind of thing a tree row stands for, beside its id in Qt::UserRole.
    //! How far off a hairline outline or a thin letter a press may land and still count, in screen px.
    static constexpr qreal k_pickSlackPx = 3.0;

    static constexpr int k_kindRole = Qt::UserRole + 1;
    //! A tail row's position in its bubble's list, beside the bubble's uid in Qt::UserRole.
    static constexpr int k_tailRole = Qt::UserRole + 2;
    //! The strip row's id. Overlay uids are minted as "ovl-…" and page ids are input uids, so it is free.
    static inline const QString k_stripId = QStringLiteral("strip");
    // Duplicate / Delete, shared by the artifact list's context menu and its keyboard shortcuts, and
    // reachable from the canvas too — the two places a bubble is ever selected.
    QAction*           m_actDuplicate    = nullptr;
    QAction*           m_actDelete       = nullptr;
    QAction*           m_actImport       = nullptr;   //!< Bring in artwork drawn outside Platemaker.
    QGraphicsRectItem* m_placementRubber = nullptr;         //!< Rubber band while a new bubble is drawn.
    QPointF            m_placementOrigin;                   //!< Where that drag started, in scene coordinates.
    bool               m_placing         = false;
    bool               m_syncingList     = false;           //!< Guards the list ⇄ scene selection round-trip.
    //! Coalesces a drag in the tree into one commit. A tree moves a row by taking it out and inserting it
    //! again, so one gesture can arrive as more than one model signal; they all restart this, and it
    //! fires once, after the drop has finished.
    QTimer*            m_orderCommit     = nullptr;
    //! Set from the moment a drag starts taking a row out until the commit above has run. Taking a row
    //! out drops its selection, and without this the tree would report a deselection nobody asked for.
    bool               m_rowsMoving      = false;
    //! Set when this controller asked for a new bubble; the uid only exists after the owner mints it, so
    //! the selection has to wait for the feed to come back.
    bool               m_selectNewOverlay = false;
    //! Set when a history step is about to arrive: the objects it touched, to select on that feed. A
    //! different question from m_selectNewOverlay — that one means "whatever was appended", because
    //! there was no uid to name yet; this one names them.
    QStringList        m_selectAfterFeed;
};

}  // namespace StripEdit

#endif // STRIPEDIT_OBJECTCONTROLLER_H
