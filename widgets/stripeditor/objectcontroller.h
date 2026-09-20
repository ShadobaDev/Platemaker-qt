#ifndef STRIPEDIT_OBJECTCONTROLLER_H
#define STRIPEDIT_OBJECTCONTROLLER_H

#include <QHash>
#include <QSet>
#include <QIcon>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringList>

#include "artifactpainter.h"   // ArtifactPart: which part of an object a colour lands on
#include "cursors.h"
#include "propertygroupeditor.h"   // PropertyGroup: which group the menu hands over
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
class ColourPair;
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

    //! One tail, as a selection holds it: which balloon, and which of its tails.
    struct TailRef
    {
        QString uid;
        int     index = 0;
        [[nodiscard]] bool operator==(const TailRef& o) const { return uid == o.uid && index == o.index; }
    };

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
    /**
     * @brief Whether @p uid is an object **we** author — one whose drawing we generate from a record.
     *
     * **The one place this is decided.** It was `m_artifacts.contains(uid)` written out at eight call
     * sites, and the review counted that as the missing abstraction behind two shipped bugs (E6a,
     * E6a.1): a default balloon loaded into the panel for a piece of imported artwork, and that default
     * then stored back over the artwork. A third had survived until this refactor — see the panel
     * binding in updateActionStates().
     *
     * False therefore means *imported artwork*: a picture somebody else drew, which we place, move,
     * mute, re-anchor and render, but cannot re-type. When artwork gains lettering of its own it will
     * have a record too, and this test will have to become something else — **in one place instead of
     * eight**, which is the whole point of it having a name.
     */
    [[nodiscard]] bool isParametric(const QString& uid) const { return m_artifacts.contains(uid); }

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

    /**
     * @brief Where the menu's colour entries read from — the tool column's pair. Never written to.
     *
     * The pair is furniture (§5.2): the tools that spend it hold a reference rather than a colour of
     * their own, and so does this menu. Without one, the two colour entries stay hidden.
     */
    void setColourSource(const ColourPair* pair);

    /**
     * @brief Gives @p colour to the @p role of every selected object that has it, as one history step.
     *
     * The same rule the colour tool paints by, reached from the menu instead of the canvas: a fill lands
     * on everything with a silhouette, the lettering's colour on everything, and an object that has no
     * such role is left alone rather than being given one.
     */
    void applyColourToSelection(const QColor& colour, ArtifactPart role);

    /**
     * @brief Copies one property group from the tool's options onto every selected object.
     *
     * This is where the *style applicator* went (§23.8). As a rail tool it would have to carry a current
     * style, which a stateless tool may not; as a menu entry it carries nothing — the value is whatever
     * ④ is set to, which is the panel that already holds "what the next object will be".
     */
    void applyGroupToSelection(PropertyGroup group);

    //! Saves the selected object's look as a named preset — everything a preset carries, and no lettering.
    void saveSelectionAsPreset();

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

    /**
     * @brief Something happened that the artist should be told once — not a state they can fix.
     *
     * **An event, deliberately not a badge.** A badge is derived from state, so whoever raised it
     * re-evaluates and clears it; *"converting hid 2 tails"* has already happened and nothing can make
     * it stop being true. Events belong in the status bar's temporary message area, which is what its
     * left side is for, while the advisories keep the right (§9.3).
     */
    void noted(const QString& text);
private:
    void onOverlayGeometryEdited(const QString& uid); //!< An item settled a move/resize/tail drag.
    //! Writes where object @p uid now stands back into its record — placement, width and anchor page.
    void writePlacement(const QString& uid);
    void onObjectPressed(const QString& uid, int handle); //!< A press on a tail's handle selects that tail.
    //! An object reports a live drag; the selection decides what else travels with it.
    void onObjectDragged(const QString& uid, const QPointF& delta, int handle);
    //! Records where everything selected stands, so a group drag can place each from its own start.
    void beginDrag(const QString& uid, int handle);
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

    /**
     * @brief Selects @p uids and @p tails together — the general form, of which everything else is a case.
     *
     * A selection may hold objects and tails at once, because *position* is the role they share: a tail of
     * one balloon and the body of another can be dragged as one thing. What they do **not** share is
     * anything ③ could edit, so a mixed selection shows no property sections at all.
     *
     * One tail on its own stays the subject it was in T5c: its balloon selected on the canvas so the
     * handles exist, the handle drawn hollow, and ③ showing that tail.
     */
    void selectSubjects(const QStringList& uids, const QList<TailRef>& tails);

    //! The tails in the selection, in the order they were picked.
    [[nodiscard]] const QList<TailRef>& selectedTails() const { return m_selectedTails; }

    //! How many things the artist actually picked — a balloon carrying someone's selected tail is not one.
    [[nodiscard]] int selectedSubjectCount() const;
    //! Selects the strip or a page: every overlay deselected, that one row selected, the subject reported.
    void selectSubject(Subject subject, const QString& pageUid);
    //! The tree row of the selected strip or page, or nullptr.
    [[nodiscard]] QTreeWidgetItem* subjectRow() const;
    //! The glyph a row wears — the object drawn small, cached until the look it is made of changes.
    [[nodiscard]] QIcon rowGlyph(const QString& uid);
    //! The tree row of tail @p index of bubble @p uid, or nullptr.
    [[nodiscard]] QTreeWidgetItem* tailRow(const QString& uid, int index) const;
    void pushOverlays(const QString& undoText); //!< Emits overlaysEdited() with the current state.
    //! Live edit from the panel → item (+persist, as a step named @p undoText or for the subject).
    void applyPanelArtifact(const TextArtifact& a, bool commit, const QString& undoText = QString());
    //! The same, for the whole selection: @p objects are in m_selectedOverlays order.
    void applyPanelArtifacts(const QList<TextArtifact>& objects, bool commit,
                             const QString& undoText = QString());
    void deleteSelectedOverlay();
    void deleteSelectedTail();   //!< Takes the selected tail off its balloon, and selects the balloon.
    void importArtwork();           //!< Asks for a file and drops it on the page currently in view.
    void duplicateSelectedOverlay();   //!< Copies the selected bubble a little down and right.
    void setOverlayEnabled(const QString& uid, bool on);  //!< The list's mute checkbox (deferred, see the ctor).

    /**
     * @brief Gives every selected object the blend mode @p blend, as one history step.
     *
     * Blend has been in the model, the compositor and this preview since the library shipped it, and
     * nothing could reach it: both creation sites wrote `Over` and no widget offered another. It is a
     * property every object has, so a set takes it the way a set takes a colour.
     */
    void setSelectionBlend(Platemaker::Models::BlendMode blend);

    /**
     * @brief Makes every selected object the **kind** @p kind stands for, as one history step.
     *
     * @param kind `None` for lettering with no balloon; any silhouette for a balloon, and that is the
     *             silhouette a converted object arrives at.
     *
     * **What passes through is decided by the kind, not by the silhouette.** An object that already has
     * a balloon is untouched by *Convert to ▸ Balloon* even if it wears a different one — re-shaping it
     * would be this menu doing the shape picker's job on an object the artist only had along for the
     * ride. Changing *which* balloon a selection wears is `applyGroupToSelection(PropertyGroup::Shape)`.
     *
     * Passing through includes keeping its place in the stack, which nothing here reorders (Q46), so
     * converting a mixed set is not two acts: select two texts and a balloon, convert to Balloon, and
     * the balloon is simply not in the diff.
     *
     * The carry-over needs no per-pair code. Every kind is the same record, so a conversion writes one
     * property and what the new kind cannot draw is **not drawn rather than destroyed**: a converted
     * balloon's tails are still in its record and come back if it is converted back. What stopped being
     * drawn is said once, in the status bar, because it is an event and not a condition (§9.3).
     */
    void convertSelectionTo(TextArtifact::Shape kind);

    /**
     * @brief Moves the selected object one place towards the front (@p forward) or the back.
     *
     * The stack in the list **is** the composite order, and dragging a row has always said so; this says
     * the same thing without a drag, which is what you want when the object is on the canvas and its row
     * is somewhere off-screen. One object only: moving several needs a rule about their order among
     * themselves, and inventing one to avoid greying a menu entry is the wrong trade.
     */
    void moveSelectedInStack(bool forward);
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
    QList<TailRef>     m_selectedTails;                     //!< Tails in the selection, in pick order.
    //! Uids in m_selectedOverlays that are there only to **carry** a selected tail — its handles exist
    //! only while its balloon is selected. They are not subjects: they are not counted, not deleted, and
    //! their row in the tree is not highlighted.
    QStringList        m_carriers;

    QHash<QString, QPair<QString, QIcon>> m_glyphs;   //!< uid → (what the glyph is made of, the glyph).
    QIcon                                 m_tailGlyph;  //!< One drawing; every tail row wears it.
    QIcon                                 m_pageGlyph;  //!< Likewise for a page…
    QIcon                                 m_stripGlyph; //!< …and for the strip itself.

    // --- a drag in flight: where everything stood when it started ---
    QHash<QString, QPointF>         m_dragStartPos;   //!< Object uid → its position at the press.
    QList<QPair<TailRef, QPointF>>  m_dragStartTips;  //!< Tail → its tip, in its balloon's own units.
    bool                            m_dragIsGroup = false;
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
    QAction*           m_actForward      = nullptr;   //!< Bring forward — one place up the stack.
    QAction*           m_actBackward     = nullptr;   //!< Send back.
    QMenu*             m_blendMenu       = nullptr;   //!< The six blend modes, checkable, on the selection.
    QMenu*             m_convertMenu     = nullptr;   //!< The two kinds the selection can be made into.
    QAction*           m_actToText       = nullptr;   //!< Convert to ▸ Text: no silhouette at all.
    QAction*           m_actToBalloon    = nullptr;   //!< Convert to ▸ Balloon: the tiles' silhouette.
    QMenu*             m_groupMenu       = nullptr;   //!< *Apply this group ▸*, from the tool's options.
    QAction*           m_actFill         = nullptr;   //!< Fill with the primary colour.
    QAction*           m_actOutline      = nullptr;   //!< Outline with the secondary colour.
    QAction*           m_actSavePreset   = nullptr;
    const ColourPair*  m_colours         = nullptr;   //!< The pair the two colour entries spend.
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
