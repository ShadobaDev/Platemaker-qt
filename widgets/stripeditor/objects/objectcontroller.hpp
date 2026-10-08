#ifndef STRIPEDIT_OBJECTCONTROLLER_HPP
#define STRIPEDIT_OBJECTCONTROLLER_HPP

#include <QHash>
#include <QSet>
#include <QIcon>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

#include "recordpainter.hpp"   // Painter::Part: which part of an object a colour lands on
#include "toolrail/cursors.hpp"
#include "objects/object.hpp"   // recordFor()/isParametric() ask the object itself
#include "properties/propertygroup.hpp"   // PropertyGroup: which group the menu hands over
#include "objectrecord.hpp"

#include <platemaker/models/project_item.hpp>

#include <vector>

class QGraphicsScene;
class QGraphicsView;
class QTimer;
class QTransform;
class QWidget;

namespace StripEdit {

class PresetStore;
class StripLayout;
class Object;

/**
 * @brief Everything the author *places* on the strip: the objects, the list, the selection, the drag.
 *
 * One sentence, no "and": it owns the overlay set and keeps three views of it in agreement — the scene
 * items, the composite-order list, and the properties panel showing whichever one is selected. The
 * editor above it owns the canvas and the tools, and tells it what TOOL OPTIONS says the next object
 * is (setToolDefaults()); a Create tool's drag is Placement's (placement.hpp), which asks this class for
 * the object it describes.
 *
 * It **owns no persistence**. Every mutation is announced on one of the four signals below and only
 * becomes real when the owner writes it and feeds the new state back through setSource() — which is
 * why a new object's uid does not exist until the round trip completes (see \c m_selectNewOverlay).
 *
 * The strip's scene *is* the strip at 1:1, so an overlay's scene position is its library placement plus
 * its anchor page's top — no coordinate mapping layer, and the preview lands exactly where the render
 * will put it.
 *
 * **What lives where** — the .cpp's sections, in order:
 * - *The constructor* wires OBJECT STATE's panel and the scene's selection.
 * - *Pointer and selection* — what a press lands on (pointerTargetAt(), objectAt()), selecting the
 *   strip, a page or a tail, and an object's own press and drag.
 * - *The feed* — setSource() takes the owner's overlays and records; syncItems() turns them into scene
 *   items, and is **the one place an Object subclass is chosen**.
 * - *Geometry back out* — a settled move or resize, written as a placement and announced.
 * - The rows of the OBJECT STACK are not here: ObjectStack (objectstack/) draws them from what this
 *   holds and reports back a selection, a mute or a new order (setStackOrder()).
 * - *Selection, and the properties panel it binds* — selectSubjects() decides what OBJECT STATE shows.
 * - *Operations on the selection* — colour, presets, artwork, re-anchor, delete, *Apply from tool
 *   options ▸*, *Convert to ▸*, blend, stacking, duplicate; each ends in applyRecords() or a signal.
 *   The object menu (ObjectMenu, objectstack/) names them; OBJECT STATE and the canvas call some too.
 * - *A new object* — requestRecord() / requestArtwork(): asked for here, created by the owner,
 *   selected when the feed brings it back. The drag that describes one is Placement's.
 *
 * Adding a kind of object reaches syncItems() and every isParametric() / `isArtwork()` decision in
 * here; adding a shape reaches none of it. See docs/SPECIFICATION.md, "Extending the strip editor".
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
     * @brief One tail, as a selection holds it: which balloon, and which of its tails.
     */
    struct TailRef
    {
        QString uid;        //!< The balloon's uid.
        int     index = 0;  //!< Which of its tails is selected.
        /** 
         * @brief Compares two tail references for equality. 
         * Two tail references are equal if they refer to the same balloon and the same tail index.
         * @param o The other tail reference to compare against.
         * @return True if both tail references are equal, false otherwise. 
         */
        [[nodiscard]] bool operator==(const TailRef& o) const { return uid == o.uid && index == o.index; }
    };

    /**
     * @brief Wires itself to the collaborators it drives; it owns none of them.
     *
     * @param scene        Where the objects are drawn (the editor's canvas scene).
     * @param view         Needed for hit-testing and for "where is the author looking" on import.
     * @param layout       Page geometry, owned by the editor; every placement question is asked of it.
     */
    ObjectController(QGraphicsScene* scene, QGraphicsView* view, PresetStore& presets,
                     const StripLayout& layout,
                     QObject* parent = nullptr);

    /** 
     * @brief Adopts the owner's complete state after an edit round-trips back. 
     * @param overlays  The new overlay set, in composite order.
     * @param records The new records for the overlays, keyed by uid.
     */
    void setSource(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                   const ObjectRecord::Map& records);

    /**
     * @brief Select one of @p uids — the first that still exists — when the next feed arrives.
     *
     * How an undone or redone step says *what* it changed, the way the dock it lands in says *where*.
     * A list rather than a uid because one step can touch several objects, and because the objects a
     * step touched are not necessarily still there: undoing a placement, or redoing a delete, leaves
     * nothing to select and the selection is cleared instead of left pointing at a ghost.
     *
     * One-shot, and armed immediately before the feed it applies to.
     * 
     * @param uids  The uids to select after the next feed. The first one that still exists will be selected.
     */
    void selectAfterFeed(const QStringList& uids) { m_selectAfterFeed = uids; }

    /**
     * @brief Selects the strip itself — what every page sits in, and what a grade is applied to.
     */
    void selectStrip();
    /**
     * @brief Selects page @p inputUid of the strip.
     * @param inputUid  The uid of the page to select. If the page does not exist, the selection is cleared.
     */
    void selectPage(const QString& inputUid);
    /**
     * @brief Selects tail @p index of bubble @p uid — or the bubble, when it has no tail at that position.
     * @param uid    The uid of the bubble whose tail is to be selected.
     * @param index  The index of the tail to select. If the bubble has no tail at that index, the bubble itself is selected instead.
     */
    void selectTail(const QString& uid, int index);

    // --- read and driven by the OBJECT STACK (objectstack/objectstack.hpp), which this class does not know ---
    [[nodiscard]] const std::vector<Platemaker::Models::StripOverlay>& overlays() const { return m_overlays; }   //!< Composite order.
    //! The scene object for overlay @p uid, or nullptr before the strip has a layout to place it on.
    [[nodiscard]] const Object* object(const QString& uid) const { return m_overlayItems.value(uid); }
    //! Balloons selected only because one of their tails is: not subjects, and their own row stays plain.
    [[nodiscard]] const QStringList& carriers() const { return m_carriers; }
    /**
     * @brief The tails in the selection, in the order they were picked.
     */
    [[nodiscard]] const QList<TailRef>& selectedTails() const { return m_selectedTails; }
    void selectOverlay(const QString& uid);   //!< Selects one in the scene and the stack, and loads the panel.
    /**
     * @brief Selects @p uids and @p tails together — the general form, of which everything else is a case.
     *
     * A selection may hold objects and tails at once, because *position* is the role they share: a tail of
     * one balloon and the body of another can be dragged as one thing. What they do **not** share is
     * anything OBJECT STATE could edit, so a mixed selection shows no property sections at all.
     *
     * One tail on its own stays a selected tail: its balloon selected on the canvas so the
     * handles exist, the handle drawn hollow, and OBJECT STATE showing that tail.
     */
    void selectSubjects(const QStringList& uids, const QList<TailRef>& tails);
    void setOverlayEnabled(const QString& uid, bool on);  //!< A row's mute checkbox.
    /**
     * @brief Adopts @p bottomUp — overlay uids read from the back to the front — as the composite order.
     *
     * Ids that are not overlays (the strip's row) are skipped. An order that has lost or gained an
     * overlay is refused and the rows are put back; an unchanged order only restores the selection the
     * drag's take-and-insert dropped.
     */
    void setStackOrder(const QStringList& bottomUp);

    // --- driven by the OBJECT MENU (objectstack/objectmenu.hpp), which this class does not know either ---
    [[nodiscard]] const QString& selectedOverlay() const { return m_selectedOverlay; }   //!< The primary.
    //! The silhouette TOOL OPTIONS would give a balloon now — what *Convert to ▸ Balloon* arrives at.
    [[nodiscard]] ObjectRecord::Shape toolBalloonShape() const;
    //! Places @p file on the page in the middle of the view, at a width that grows with the chapter.
    void importArtworkFile(const QString& file);
    void duplicateSelectedOverlay();   //!< Copies the selected bubble a little down and right.
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
     * Passing through includes keeping its place in the stack, which nothing here reorders, so
     * converting a mixed set is not two acts: select two texts and a balloon, convert to Balloon, and
     * the balloon is simply not in the diff.
     *
     * The carry-over needs no per-pair code. Every kind is the same record, so a conversion writes one
     * property and what the new kind cannot draw is **not drawn rather than destroyed**: a converted
     * balloon's tails are still in its record and come back if it is converted back. What stopped being
     * drawn is said once, in the status bar, because it is an event and not a condition.
     * 
     * @param kind  The kind to convert the selection to.
     */
    void convertSelectionTo(ObjectRecord::Shape kind);
    /**
     * @brief Moves the selected object one place towards the front (@p forward) or the back.
     *
     * The stack in the list **is** the composite order, and dragging a row has always said so; this says
     * the same thing without a drag, which is what you want when the object is on the canvas and its row
     * is somewhere off-screen. One object only: moving several needs a rule about their order among
     * themselves, and inventing one to avoid greying a menu entry is the wrong trade.
     * 
     * @param forward  True to move the object towards the front, false to move it towards the back.
     */
    void moveSelectedInStack(bool forward);
    /**
     * @brief Selects the overlay with @p uid, or clears the selection if it does not exist.
     * @param uid  The uid of the overlay to select. If the overlay does not exist, the selection is cleared.
     */
    [[nodiscard]] Subject        subject() const { return m_subject; }
    /**
     * @brief Whether @p uid is an object **we** author — one whose drawing we generate from a record.
     *
     * **The one place this is decided.** It was `m_records.contains(uid)` written out at eight call
     * sites, and that was the missing abstraction behind two shipped bugs: a default balloon loaded into
     * the panel for a piece of imported artwork, and that default then stored back over the artwork. A
     * third had survived until this refactor — see the panel binding in updateActionStates().
     *
     * False therefore means *imported artwork*: a picture somebody else drew, which we place, move,
     * mute, re-anchor and render, but cannot re-type.
     *
     * **The object answers when there is one**, because it is the one that knows: its record is kept
     * describing what it actually is. The feed's records are consulted only for a uid whose object has
     * not been built yet, and a uid neither of them holds is not ours to author.
     * 
     * @param uid  The uid of the overlay to check.
     * @return True if the overlay is an object we author, false if it is imported artwork or does not exist.
     */
    [[nodiscard]] bool isParametric(const QString& uid) const
    {
        if (const Object* item = m_overlayItems.value(uid))
            return !item->record().isArtwork();
        const auto it = m_feedRecords.constFind(uid);
        return it != m_feedRecords.constEnd() && !it->isArtwork();
    }

    /**
     * @brief The record for @p uid — **from the object, which is where one lives**.
     *
     * Every read of an authoring record goes through here. It used to be `m_records.value(uid)`, and
     * that returns a **default speech balloon** for a uid the map does not hold: one value standing for
     * both "a plain speech balloon" and "no record at all", which is the mechanism behind four shipped
     * defects. An object always has a record describing what it is, so asking one cannot go wrong; the
     * feed's map is consulted only before the object exists.
     * 
     * @param uid  The uid of the overlay whose record is requested.
     * @return The record for the overlay, or a default speech balloon if it does not exist.
     */
    [[nodiscard]] ObjectRecord recordFor(const QString& uid) const
    {
        if (const Object* item = m_overlayItems.value(uid))
            return item->record();
        return m_feedRecords.value(uid);
    }

    /**
     * @brief Every object's record, as the owner should store it — **built from the objects**.
     *
     * What goes out on overlaysCommitted(). Derived rather than maintained: the map used to be written by
     * hand at ten sites beside the object that had just been given the same record, and a write path
     * that updated one of the two left the other describing something that is not on screen.
     * 
     * @return A map of every overlay's uid to its record, built from the objects.
     */
    [[nodiscard]] ObjectRecord::Map currentRecords() const;

    /**
     * @brief Everything selected, in the order it was picked. The last is the primary — see selectOverlays().
     */
    [[nodiscard]] const QStringList& selectedOverlays() const { return m_selectedOverlays; }
    [[nodiscard]] const QString& selectedPage() const { return m_selectedPage; }   //!< When subject() is Page.
    [[nodiscard]] int            selectedTail() const { return m_selectedTail; }   //!< When subject() is Tail.

    /**
     * @brief Paints @p colour onto whatever is drawn at @p scenePos.
     *
     * The colour tool's canvas face, and the same rule the eyedropper reads by: **what is under the
     * pointer is what changes.** Point at the lettering and the lettering takes it; at the outline and
     * the outline does; anywhere inside the balloon and it is the fill. A point on a balloon's box but
     * outside its silhouette paints nothing, and imported artwork takes nothing — it is pixels somebody
     * else drew. The tool therefore needs no "what am I painting" setting: the picture is the setting.
     *
     * The object it lands on becomes the **selection**, so OBJECT STATE shows what just changed and the same
     * edit is one Ctrl+Z away. One undo step per press, named after what it painted.
     *
     * @param scenePos  The pointer's position in the scene, which is the strip at 1:1.
     * @param deviceTransform  The view's transform, for hit-testing the scene position against the objects' silhouettes.
     * @param colour  The colour to paint.
     * @return Whether anything was painted.
     */
    bool applyColourAt(const QPointF& scenePos, const QTransform& deviceTransform, const QColor& colour);

    /**
     * @brief Gives @p colour to the @p role of every selected object that has it, as one history step.
     *
     * The same rule the colour tool paints by, reached from the menu instead of the canvas: a fill lands
     * on everything with a silhouette, the lettering's colour on everything, and an object that has no
     * such role is left alone rather than being given one.
     * 
     * @param colour  The colour to apply.
     * @param role    The part of the object to apply the colour to.
     */
    void applyColourToSelection(const QColor& colour, Painter::Part role);

    /**
     * @brief Copies one property group from the tool's options onto every selected object.
     *
     * This is where the *style applicator* went. As a rail tool it would have to carry a current
     * style, which a stateless tool may not; as a menu entry it carries nothing — the value is whatever
     * TOOL OPTIONS is set to, which is the panel that already holds "what the next object will be".
     */
    void applyGroupToSelection(PropertyGroup group);

    /**
     * @brief Restyles the selected bubble with preset \p index, keeping what it says and where it points.
     * @param index  The index of the preset to apply, as it appears in the *Apply preset ▸* menu.
     */
    void applyPresetToSelection(int index);

    /**
     * @brief Moves the selected object onto page @p pageUid, at the same offset from that page's top.
     * @param pageUid  The UID of the page to move the object to.
     */
    void reanchorSelection(const QString& pageUid);

    void syncItems();    //!< Reconciles the scene items with the overlay set, by uid.
    void reselect();     //!< Re-applies the current selection after the scene was rebuilt.

    /**
     * @brief While set, the list <-> scene selection callbacks are ignored: a programmatic rebuild is not a
     * @brief user action, and \c QGraphicsScene::clear() drops the selection on its way through.
     */
    void setSyncing(bool on) { m_syncingList = on; }

    /**
     * @brief The scene is about to delete every item; harvest their records and drop the pointers.
     *
     * Call it **before** `QGraphicsScene::clear()`: it reads every object. Their records are harvested
     * into the feed's first, because the objects are where a record lives between feeds — an edit
     * previewed but not yet settled exists only on the object, and rebuilding the scene from a feed that
     * never heard about it would quietly undo it.
     */
    void forgetItems()
    {
        m_feedRecords = currentRecords();
        m_overlayItems.clear();
    }

    /**
     * @brief Returns true if an existing object sits under \p scenePos.
     * @param scenePos  The position to check.
     * @param deviceTransform  The transform for the device coordinates.
     * @return True if an object is at the specified position.
     */
    [[nodiscard]] bool objectAt(const QPointF& scenePos, const QTransform& deviceTransform) const;

    /**
     * @brief What the pointer is over at @p scenePos, for whoever has to decide a cursor or a gesture.
     * @param scenePos  The position to check.
     * @param deviceTransform  The transform for the device coordinates.
     * @return The target under the pointer.
     */
    [[nodiscard]] PointerTarget pointerTargetAt(const QPointF& scenePos,
                                                const QTransform& deviceTransform) const;

    /**
     * @brief Puts @p file down centred on @p scenePos, **at its own size** — a drop from the TOOL OPTIONS
     * preview.
     *
     * Its own size, deliberately, and not fitted to anything: a sound effect that reaches past the
     * strip's edge is a thing artists want, and a placement that quietly shrank it would be a decision
     * nobody asked for. *Fit to strip width* is one menu entry away for when they did.
     * 
     * @param file  The file to place.
     * @param scenePos  The position to place the file at, in scene coordinates.
     */
    void placeArtworkAt(const QString& file, const QPointF& scenePos);

    /**
     * @brief Draws the selected artwork at @p percent of its own pixels — 100 is one for one.
     *
     * A percentage rather than a width, because the artist's question is *how much bigger than I drew
     * it*, and because the answer has to survive a re-profile: the stored form is a fraction of the
     * page, so the same percentage means the same thing at 800 or 1600 points wide.
     *
     * @param percent  The percentage to scale the artwork by.
     * @param commit   False while the spin box is moving — the object resizes, the history does not.
     */
    void scaleSelectedArtwork(double percent, bool commit = true);

    [[nodiscard]] double selectedArtworkPercent() const; //! What that percentage currently is, or 0 when the selection is not one piece of artwork.

    /**
     * @brief Gives every selected object the blend mode @p blend, as one history step.
     *
     * Blend has been in the model, the compositor and this preview since the library shipped it, and
     * nothing could reach it: both creation sites wrote `Over` and no widget offered another. It is a
     * property every object has, so a set takes it the way a set takes a colour.
     * 
     * @param blend  The blend mode to apply to the selection.
     */
    void setSelectionBlend(Platemaker::Models::BlendMode blend);

    /**
     * @brief The mode the whole selection is composited in, or no value when they disagree.
     *
     * One computation for the menu's tick and for OBJECT STATE's row, because they answer the same question
     * and a tick that disagreed with the row beside it would make one of them wrong.
     * 
     * @return The blend mode of the selection, or std::nullopt if they disagree.
     */
    [[nodiscard]] std::optional<Platemaker::Models::BlendMode> selectionBlend() const;

    /**
     * @brief Removes everything selected, as one history step.
     *
     * Public because more than the menu asks for it: OBJECT STATE offers Delete for whichever kind of object
     * it is showing. What it deletes is the selection, which is the only thing any of them mean by it.
     */
    void deleteSelectedOverlay();

    /**
     * @brief Checks if exactly one piece of imported artwork is selected — what OBJECT STATE and the menu
     * both ask.
     * @return True if exactly one piece of imported artwork is selected, false otherwise.
     */
    [[nodiscard]] bool selectionIsArtwork() const;

    // --- what OBJECT STATE says: its edits come in here, wired by the Editor ---
    /**
     * @brief What OBJECT STATE just said, written to the objects OBJECT STATE was bound to.
     *
     * The panel answers with records and no uids, because it was handed records and no uids. Which
     * objects those were is remembered in \c m_panelSubjects at the moment it was bound, so a selection
     * that has changed since cannot make this write the right records onto the wrong objects.
     * 
     * @param records  The records to apply to the objects.
     * @param commit  False while a control is being dragged or typed in: the objects
     */
    void applyPanelRecords(const QList<ObjectRecord>& records, bool commit);
    //! OBJECT STATE's *Fit*: grows or shrinks the selected object's box to its words. Not a picture.
    void fitSelectionToText();

    // --- a new object: asked for here, created by the owner, selected when it comes back in the feed ---
    //! Asks the owner for a new authored object; it is selected when the feed brings it back.
    void requestRecord(const ObjectRecord& record, double xFrac, double yFrac, double wFrac,
                       const QString& anchorInputUid);
    //! Asks the owner to import @p file as a new picture; it is selected when the feed brings it back.
    void requestArtwork(const QString& file, double xFrac, double yFrac, double wFrac, QSize naturalSize,
                        const QString& anchorInputUid);

    /**
     * @brief What TOOL OPTIONS says the next object is — read, never written.
     *
     * Two questions, given as functions so that this class needs no tool-options panel: the prototype a
     * balloon would be placed as, which *Apply from tool options ▸* copies a group from, and the
     * silhouette a balloon would wear, which *Convert to ▸ Balloon* arrives at (never `None`, even while
     * the Text tool is armed). The Editor, which owns both regions, wires them.
     */
    struct ToolDefaults
    {
        std::function<ObjectRecord()>        prototype;
        std::function<ObjectRecord::Shape()> balloonShape;
    };
    void setToolDefaults(ToolDefaults defaults) { m_toolDefaults = std::move(defaults); }

signals:
    /**
     * @brief A bubble was drawn. Creation is the library's — it mints the uid and dedups identical artwork.
     * @param record  The record for the new object.
     * @param xFrac, yFrac, wFrac  The new object's position and size as fractions of the page dimensions.
     * @param anchorInputUid  The UID of the anchor input.
     */
    void recordCreated(const ObjectRecord& record, double xFrac, double yFrac, double wFrac,
                         const QString& anchorInputUid);
    /**
     * @brief Any other edit, as the complete new state: one channel rather than one signal per gesture.
     * @param overlays  The new overlay set, in composite order.
     * @param records  The new records for the overlays, keyed by uid.
     * @param undoText  The text to use for the undo step that will be created
     */
    void overlaysCommitted(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                        const ObjectRecord::Map& records, const QString& undoText);
    /**
     * @brief Artwork drawn elsewhere should be copied into the workspace and registered here.
     *
     * @p naturalSize is the picture's own pixels, read through the one loader that knows how to ask an
     * SVG its size. It travels because the far end cannot work it out: it sees only the copy it has
     * just written, and guessing there once produced a record whose box was a fraction times 1000.
     * @param sourceFile  The file to copy into the workspace.
     * @param xFrac, yFrac, wFrac  The new object's position and size as fractions of the page dimensions.
     * @param naturalSize  The picture's own pixel dimensions.
     * @param anchorInputUid  The UID of the anchor input.
     */
    void artworkImportRequested(const QString& sourceFile, double xFrac, double yFrac, double wFrac,
                                QSize naturalSize, const QString& anchorInputUid);
    /**
     * @brief The selection moved to @p subject. @p uid is the overlay's uid or the page's input uid, and empty
     * @param subject  The new selected subject.
     * @param uid      The UID of the selected object, or an empty string if none.
     */
    void subjectChanged(StripEdit::ObjectController::Subject subject, const QString& uid);
    // --- for the OBJECT STACK: what changed, so the rows can follow ---
    void stackChanged();               //!< The objects, their order, labels, looks or pages changed.
    void selectionChanged();           //!< The selected objects and tails changed (rows, menu entries).
    void subjectRowChanged();          //!< The strip or a page became the selection.
    void revealSelectionRequested();   //!< An undo selected something: bring its row into view.

    // --- for OBJECT STATE: what it is bound to now. The Editor wires these; this class has no panel. ---
    void boundToRecord(const ObjectRecord& record);            //!< One object.
    void boundToTail(const ObjectRecord& record, int index);   //!< One tail, of the balloon @p record.
    void boundToRecords(const QList<ObjectRecord>& records);   //!< Several objects, of any kinds.
    void boundToMixed(int count);   //!< Objects and tails together: nothing editable in common.
    void boundToNothing();          //!< Nothing is selected.
    //! The selection's blend mode, none when they disagree; @p applies is false for a tail or nothing.
    void blendBound(std::optional<Platemaker::Models::BlendMode> blend, bool applies);
    //! One picture's scale in percent; no value for anything else.
    void artworkScaleBound(std::optional<double> percent);
    void textFocusRequested();      //!< A bubble just placed: put the caret in its text.

    /**
     * @brief Something happened that the artist should be told once — not a state they can fix.
     *
     * **An event, deliberately not a badge.** A badge is derived from state, so whoever raised it
     * re-evaluates and clears it; *"converting hid 2 tails"* has already happened and nothing can make
     * it stop being true. Events belong in the status bar's temporary message area, which is what its
     * left side is for, while the advisories keep the right.
     * 
     * @param text  The message to show the artist.
     */
    void noted(const QString& text);
private:
    void onOverlayGeometryCommitted(const QString& uid); //!< An item settled a move/resize/tail drag.
    void writePlacement(const QString& uid); //<! Writes where object @p uid now stands back into its record — placement, width and anchor page.
    void onObjectPressed(const QString& uid, int handle); //!< A press on a tail's handle selects that tail.
    void onObjectDragged(const QString& uid, const QPointF& delta, int handle); //<! An object reports a live drag; the selection decides what else travels with it.
    void beginDrag(const QString& uid, int handle); //<! Records where everything selected stands, so a group drag can place each from its own start.
    /**
     * @brief How much bigger than its own artwork an overlay is drawn.
     *
     * \c 1.0 whenever the asset was authored at the width the strip is laid out at now, which is every
     * overlay until a chapter is re-profiled. After one, the record's \c wFrac still says how wide the
     * object is *relative to the page*, and this is what turns that back into a scale for the item.
     * 
     * @param o  The overlay to compute the scale for.
     * @param naturalWidth  The overlay's own width in pixels, as read from the SVG or the imported picture.
     * @return The scale factor to apply to the overlay's item
     */
    [[nodiscard]] qreal itemScaleFor(const Platemaker::Models::StripOverlay& o, qreal naturalWidth) const;
    /**
     * @brief The library's rasterisation of \p a, cached by the SVG it emits. Empty if it cannot be produced.
     * @param a  The record to rasterise.
     * @return The rasterised image of the record, or an empty QImage if it cannot be produced.
     */
    [[nodiscard]] QImage sharpRasterFor(const ObjectRecord& a);

    /**
     * @brief Selects exactly @p uids, in the order given. The **last** is the primary.
     *
     * The primary is what a single-subject action acts on and what OBJECT STATE shows when there is only one
     * — "the thing I just clicked", which is the last one picked. Everything written before the selection was
     * a set still reads selectedOverlay(), which is now that primary.
     */
    void selectOverlays(const QStringList& uids);



    /**
     * @brief How many things the artist actually picked — a balloon carrying someone's selected tail is not one.
     */
    [[nodiscard]] int selectedSubjectCount() const;
    /**
     * @brief Selects the strip or a page: every overlay deselected, that one row selected, the subject reported.
     */
    void selectSubject(Subject subject, const QString& pageUid);
    /**
     * @brief Which file an asset item draws: the imported picture, which is not always the overlay's own.
     */
    [[nodiscard]] QString pictureFor(const Platemaker::Models::StripOverlay& o) const;
    /**
     * @brief Live edit from the panel -> item (+persist, as a step named @p undoText or for the subject).
     */
    void pushOverlays(const QString& undoText); //!< Emits overlaysCommitted() with the current state.
    /**
     * @brief Writes @p records onto the objects @p uids names — **the one write path**.
     *
     * There were three: one for a single balloon, one for several, and one for a picture. They differed
     * in a `qobject_cast` and in what the history step was called, and the two that only accepted a
     * balloon dropped a picture silently — so the bucket, applied to a balloon and a lettered picture
     * together, coloured one of them and told the panel it had coloured both.
     *
     * **Pairing is stated, not inferred.** The old multi-object path matched its list to
     * `m_selectedOverlays` by position and refused when the two lengths disagreed, which is what made a
     * mixed selection uneditable. Here the caller says which object each record is for.
     *
     * @param uids     The uids of the objects to apply the records to.
     * @param records  The records to apply to the objects.
     * @param commit False while a control is being dragged or typed in: the objects repaint and nothing
     *               reaches the history. The panel's own debounce decides when an edit has settled.
     * @param undoText  The text to use for the undo step that will be created. If empty, a default text will be used.
     */
    void applyRecords(const QStringList& uids, const QList<ObjectRecord>& records, bool commit,
                      const QString& undoText = QString());

    /**
     * @brief One object, by uid — the same path, for the callers that act on the primary selection.
     * @param uid  The uid of the object to apply the record to.
     * @param record  The record to apply to the object.
     * @param commit  False while a control is being dragged or typed in: the object
     */
    void applyRecord(const QString& uid, const ObjectRecord& record, bool commit,
                     const QString& undoText = QString());

    void deleteSelectedTail();   //!< Takes the selected tail off its balloon, and selects the balloon.




    // --- collaborators, not owned ---
    QGraphicsScene* m_scene        = nullptr;   //!< The scene that draws the strip and its overlays.
    QGraphicsView*  m_view         = nullptr;   //!< The view that shows the scene, and whose transform is used for hit-testing. 
    ToolDefaults       m_toolDefaults;                //!< What TOOL OPTIONS says the next object is.
    PresetStore&      m_presets;                //!< The store of named presets, which the menu reads from and the save action writes to.
    const StripLayout&   m_layout;                   //!< The layout that owns the strip, for page names and sizes.

    std::vector<Platemaker::Models::StripOverlay> m_overlays;   //!< The project's overlays, in composite order.
    /**
     * @brief The records the last feed brought — **a seed, not the state**.
     *
     * Read in two places only: to build an object that does not exist yet, and to answer for a uid that
     * has no object. Everything else asks the object, through recordFor(). It is deliberately not kept
     * in step with edits, because the objects already are; keeping a second copy in step by hand is
     * what this member used to be for, and what it stopped being.
     */
    ObjectRecord::Map                                   m_feedRecords;
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
    QHash<QString, QImage>  m_sharpCache;
    QString            m_selectedOverlay;                   //!< The primary: last of m_selectedOverlays.
    QStringList        m_selectedOverlays;                  //!< Everything selected, in pick order.
    /**
     * @brief The objects OBJECT STATE is currently bound to, in the order its records are in.
     *
     * Not the same thing as the selection, and that is the point: the panel answers about what it was
     * shown, which may no longer be what is selected. Empty whenever the panel is showing something
     * that cannot be edited — nothing, a mixed set, a tail's balloon — so a stray signal writes nothing.
     */
    QStringList        m_panelSubjects;
    QList<TailRef>     m_selectedTails;                     //!< Tails in the selection, in pick order.
    //! Uids in m_selectedOverlays that are there only to **carry** a selected tail — its handles exist
    //! only while its balloon is selected. They are not subjects: they are not counted, not deleted, and
    //! their row in the tree is not highlighted.
    QStringList        m_carriers;

    // --- a drag in flight: where everything stood when it started ---
    QHash<QString, QPointF>         m_dragStartPos;   //!< Object uid -> its position at the press.
    QList<QPair<TailRef, QPointF>>  m_dragStartTips;  //!< Tail -> its tip, in its balloon's own units.
    bool                            m_dragIsGroup = false;
    Subject            m_subject = Subject::None;           //!< What the selection is.
    QString            m_selectedPage;                      //!< Input uid of the selected page, when a page is.
    int                m_selectedTail      = -1;            //!< Index of the selected tail, when a tail is.
    int                m_selectedTailCount = 0;             //!< How many tails its bubble had when it was selected.
    //! How far off a hairline outline or a thin letter a press may land and still count, in screen px.
    static constexpr qreal k_pickSlackPx = 3.0;

    bool               m_syncingList     = false;           //!< Guards the scene's selection round-trip.
    /**
     * @brief Set when this controller asked for a new bubble; the uid only exists after the owner mints it, so
     *        the selection has to wait for the feed to come back.
     */
    bool               m_selectNewOverlay = false;
    /**
     * @brief Set when a history step is about to arrive: the objects it touched, to select on that feed. A
     *        different question from m_selectNewOverlay — that one means "whatever was appended", because
     *        there was no uid to name yet; this one names them.
     */
    QStringList        m_selectAfterFeed;
};

}  // namespace StripEdit

#endif // STRIPEDIT_OBJECTCONTROLLER_HPP
