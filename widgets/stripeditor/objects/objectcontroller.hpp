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

#include "geometry.hpp"   // Geometry::Part: which part of an object a colour lands on
#include "toolrail/cursors.hpp"
#include "objects/object.hpp"   // compositionOf()/isParametric() ask the object itself
#include "properties/propertygroup.hpp"   // PropertyGroup: which group the menu hands over
#include "composition.hpp"
#include "objects/selection.hpp"
#include "objects/document.hpp"
#include "objects/panelbinder.hpp"
#include "objects/shapeeditmode.hpp"

#include <platemaker/models/project_item.hpp>

#include <vector>

class QGraphicsPathItem;
class QGraphicsScene;
class QGraphicsView;
class QTimer;
class QTransform;
class QWidget;

namespace StripEdit {

class PresetStore;
struct BubblePreset;
class StripLayout;
class Object;
class SelectionChrome;

/**
 * \brief Everything the author *places* on the strip: the objects, the list, the selection, the drag.
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
 * - *The feed* — setSource() takes the owner's overlays and compositions; syncItems() turns them into scene
 *   items, and is **the one place an Object subclass is chosen**.
 * - *Geometry back out* — a settled move or resize, written as a placement and announced.
 * - The rows of the OBJECT STACK are not here: ObjectStack (objectstack/) draws them from what this
 *   holds and reports back a selection, a mute or a new order (setStackOrder()).
 * - *Selection, and the properties panel it binds* — selectSubjects() decides what OBJECT STATE shows.
 * - *Operations on the selection* — colour, presets, artwork, re-anchor, delete, *Convert to ▸*, blend,
 *   stacking, duplicate; each ends in applyCompositions() or a signal.
 *   The object menu (ObjectMenu, objectstack/) names them; OBJECT STATE and the canvas call some too.
 * - *A new object* — requestComposition() / requestPicture(): asked for here, created by the owner,
 *   selected when the feed brings it back. The drag that describes one is Placement's.
 *
 * Adding a kind of object reaches syncItems() and every isParametric() / `picture` decision in
 * here; adding a shape reaches none of it. See docs/SPECIFICATION.md, "Extending the strip editor".
 */
class ObjectController : public QObject
{
    Q_OBJECT

public:
    /**
     * \brief Wires itself to the collaborators it drives; it owns none of them.
     *
     * \param scene        Where the objects are drawn (the editor's canvas scene).
     * \param view         Needed for hit-testing and for "where is the author looking" on import.
     * \param layout       Page geometry, owned by the editor; every placement question is asked of it.
     */
    ObjectController(QGraphicsScene* scene, QGraphicsView* view, PresetStore& presets,
                     const StripLayout& layout,
                     QObject* parent = nullptr);

    /** 
     * \brief Adopts the owner's complete state after an edit round-trips back. 
     * \param overlays  The new overlay set, in composite order.
     * \param compositions The new compositions of the overlays, keyed by uid.
     */
    void setSource(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                   const Composition::Map& compositions);

    /**
     * \brief Select one of @p uids — the first that still exists — when the next feed arrives.
     *
     * How an undone or redone step says *what* it changed, the way the dock it lands in says *where*.
     * A list rather than a uid because one step can touch several objects, and because the objects a
     * step touched are not necessarily still there: undoing a placement, or redoing a delete, leaves
     * nothing to select and the selection is cleared instead of left pointing at a ghost.
     *
     * One-shot, and armed immediately before the feed it applies to.
     * 
     * \param uids  The uids to select after the next feed. The first one that still exists will be selected.
     */
    void selectAfterFeed(const QStringList& uids) { m_selectAfterFeed = uids; }

    /**
     * \brief Selects the strip itself — what every page sits in, and what a grade is applied to.
     */
    void selectStrip();
    /**
     * \brief Selects page @p inputUid of the strip.
     * \param inputUid  The uid of the page to select. If the page does not exist, the selection is cleared.
     */
    void selectPage(const QString& inputUid);
    /**
     * \brief Selects tail @p index of bubble @p uid — or the bubble, when it has no tail at that position.
     * \param uid    The uid of the bubble whose tail is to be selected.
     * \param index  The index of the tail to select. If the bubble has no tail at that index, the bubble itself is selected instead.
     */
    void selectTail(const QString& uid, int index);
    //! Selects words @p id of object @p uid, one of its several, the Text section theirs; the object when they are gone.
    void selectWords(const QString& uid, int id);
    //! Selects body @p id of balloon @p uid, one bubble of several, the Shape section its own; the balloon when it is gone.
    void selectBody(const QString& uid, int id);
    //! Selects object @p uid's own mask, its id @p id, the Shape and Mask sections its own; the object when it is gone.
    void selectMask(const QString& uid, int id);
    //! Whether Esc has something to leave: words, a bubble or a mask picked on its own, or one mask object selected.
    [[nodiscard]] bool canLeaveElement() const;
    //! Esc: from words, a bubble or a mask picked on its own back to their object, from a mask object to nothing
    //! selected; whether there was one to leave.
    bool leaveElement();
    /**
     * \brief Scrolls the strip to a subject: an object or a tail centred in the view, a page with its top
     *        at the view's top. The strip, or a subject that is gone, moves nothing.
     * \param subject  What @p id names.
     * \param id       The object's uid (for a tail, its balloon's), or the page's input uid.
     * \param tail     The tail's element id, for Subject::Tail.
     */
    void reveal(Subject subject, const QString& id, int tail = -1);

    // --- read and driven by the OBJECT STACK (objectstack/objectstack.hpp), which this class does not know ---
    [[nodiscard]] const std::vector<Platemaker::Models::StripOverlay>& overlays() const { return m_document.overlays(); }   //!< Composite order.
    //! The scene object for overlay @p uid, or nullptr before the strip has a layout to place it on.
    [[nodiscard]] const Object* object(const QString& uid) const { return m_overlayItems.value(uid); }
    //! Balloons selected only because one of their tails is: not subjects, and their own row stays plain.
    [[nodiscard]] const QStringList& carriers() const { return m_selection.carriers(); }
    /**
     * \brief The tails in the selection, in the order they were picked.
     */
    [[nodiscard]] const QList<ElementRef>& selectedTails() const { return m_selection.elements(); }
    //! The words picked on their own (Subject::Words); none otherwise.
    [[nodiscard]] ElementRef selectedWords() const { return m_selection.words(); }
    //! The body picked on its own (Subject::Body); none otherwise.
    [[nodiscard]] ElementRef selectedBody() const { return m_selection.body(); }
    //! The mask picked on its own (Subject::Mask); none otherwise.
    [[nodiscard]] ElementRef selectedMask() const { return m_selection.mask(); }
    void selectOverlay(const QString& uid);   //!< Selects one in the scene and the stack, and loads the panel.
    /**
     * \brief Selects @p uids and @p tails together — the general form, of which everything else is a case.
     *
     * A selection may hold objects and tails at once, because *position* is the role they share: a tail of
     * one balloon and the body of another can be dragged as one thing. What they do **not** share is
     * anything OBJECT STATE could edit, so a mixed selection shows no property sections at all.
     *
     * One tail on its own stays a selected tail: its balloon selected on the canvas so the
     * handles exist, the handle drawn hollow, and OBJECT STATE showing that tail.
     */
    void selectSubjects(const QStringList& uids, const QList<ElementRef>& tails);
    void setOverlayEnabled(const QString& uid, bool on);  //!< A row's mute checkbox.
    /**
     * \brief Adopts @p bottomUp — overlay uids read from the back to the front — as the composite order.
     *
     * Ids that are not overlays (the strip's row) are skipped. An order that has lost or gained an
     * overlay is refused and the rows are put back; an unchanged order only restores the selection the
     * drag's take-and-insert dropped.
     */
    void setStackOrder(const QStringList& bottomUp);

    // --- driven by the OBJECT MENU (objectstack/objectmenu.hpp), which this class does not know either ---
    [[nodiscard]] const QString& selectedOverlay() const { return m_selection.primary(); }   //!< The primary.
    //! The balloon TOOL OPTIONS would give an object now, its shape whole — what *Convert to ▸ Balloon* arrives at.
    [[nodiscard]] BalloonLook toolBalloon() const;
    //! Its shape: what the menu asks convertSelectionTo() for.
    [[nodiscard]] ShapeProperties toolBalloonShape() const;
    //! Places @p file on the page in the middle of the view, at a width that grows with the chapter.
    void importPictureFile(const QString& file);
    void duplicateSelectedOverlay();   //!< Copies the selected bubble a little down and right.
    /**
     * \brief Makes every selected object the **kind** @p shape stands for, as one history step.
     *
     * \param shape Of kind `None` for lettering with no balloon; any silhouette for a balloon, and that
     *              whole shape, parameters and nodes included, is what a converted object arrives at, in the
     *              style TOOL OPTIONS shows (toolBalloon()).
     *
     * **What passes through is decided by the kind, not by the silhouette.** An object that already has
     * a balloon is untouched by *Convert to ▸ Balloon* even if it wears a different one — re-shaping it
     * would be this menu doing the shape picker's job on an object the artist only had along for the
     * ride. Changing *which* balloon a selection wears is the Shape section's, or *Apply preset ▸*.
     *
     * Passing through includes keeping its place in the stack, which nothing here reorders, so
     * converting a mixed set is not two acts: select two texts and a balloon, convert to Balloon, and
     * the balloon is simply not in the diff.
     *
     * What an object is, is what it has: to text, the balloon goes, with its style and its tails; converted
     * back, it is the balloon TOOL OPTIONS shows, with no tail. Undo brings the old one back.
     */
    void convertSelectionTo(const ShapeProperties& shape);
    /**
     * \brief Moves the selected object one place towards the front (@p forward) or the back.
     *
     * The stack in the list **is** the composite order, and dragging a row has always said so; this says
     * the same thing without a drag, which is what you want when the object is on the canvas and its row
     * is somewhere off-screen. One object only: moving several needs a rule about their order among
     * themselves, and inventing one to avoid greying a menu entry is the wrong trade.
     * 
     * \param forward  True to move the object towards the front, false to move it towards the back.
     */
    void moveSelectedInStack(bool forward);
    /**
     * \brief Selects the overlay with @p uid, or clears the selection if it does not exist.
     * \param uid  The uid of the overlay to select. If the overlay does not exist, the selection is cleared.
     */
    [[nodiscard]] Subject        subject() const { return m_selection.subject(); }
    /**
     * \brief Whether @p uid is an object **we** author — one whose drawing we generate from its composition.
     *
     * **The one place this is decided.** It was `m_compositions.contains(uid)` written out at eight call
     * sites, and that was the missing abstraction behind two shipped bugs: a default balloon loaded into
     * the panel for a piece of imported artwork, and that default then stored back over the artwork. A
     * third had survived until this refactor — see the panel binding in updateActionStates().
     *
     * False therefore means *imported artwork*: a picture somebody else drew, which we place, move,
     * mute, re-anchor and render, but cannot re-type.
     *
     * **The document answers**: an object we draw has a composition there, and a picture has one only once
     * it was lettered, with its picture in it.
     * 
     * \param uid  The uid of the overlay to check.
     * \return True if the overlay is an object we author, false if it is imported artwork or does not exist.
     */
    [[nodiscard]] bool isParametric(const QString& uid) const
    {
        const std::optional<Composition> c = m_document.composition(uid);
        return c && !c->picture;
    }

    /**
     * \brief The composition of @p uid — **the document's, which is where one lives**.
     *
     * A picture never lettered has none there: its object's description of itself answers (this picture, at
     * its own pixels, with empty words over it), which is what the file says too.
     *
     * \param uid  The uid of the overlay whose composition is requested.
     * \return Its composition, or an empty one if it does not exist.
     */
    [[nodiscard]] Composition compositionOf(const QString& uid) const
    {
        if (std::optional<Composition> c = m_document.composition(uid))
            return *c;
        if (const Object* item = m_overlayItems.value(uid))
            return item->composition();
        return {};
    }

    /**
     * \brief Everything selected, in the order it was picked. The last is the primary — see selectOverlays().
     */
    [[nodiscard]] const QStringList& selectedOverlays() const { return m_selection.objects(); }
    [[nodiscard]] const QString& selectedPage() const { return m_selection.page(); }   //!< When subject() is Page.
    //! When subject() is Tail: the tail's index among its balloon's tails; -1 otherwise.
    [[nodiscard]] int            selectedTail() const;

    /**
     * \brief Paints @p colour onto whatever is drawn at @p scenePos.
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
     * \param scenePos  The pointer's position in the scene, which is the strip at 1:1.
     * \param deviceTransform  The view's transform, for hit-testing the scene position against the objects' silhouettes.
     * \param colour  The colour to paint.
     * \return Whether anything was painted.
     */
    bool applyColourAt(const QPointF& scenePos, const QTransform& deviceTransform, const QColor& colour);

    /**
     * \brief Gives @p colour to the @p role of every selected object that has it, as one history step.
     *
     * The same rule the colour tool paints by, reached from the menu instead of the canvas: a fill lands
     * on everything with a silhouette, the lettering's colour on everything, and an object that has no
     * such role is left alone rather than being given one.
     * 
     * \param colour  The colour to apply.
     * \param role    The part of the object to apply the colour to.
     */
    void applyColourToSelection(const QColor& colour, Geometry::Part role);

    /**
     * \brief Restyles every selected object we draw with preset \p index, as one history step, keeping
     *        what each says, its size and where its tails point. A text object stays text: the preset's
     *        shape is not given to an object that has none. A picture is left alone.
     * \param index  The store's index of the preset.
     */
    void applyPresetToSelection(int index);
    //! How many selected objects applyPresetToSelection() would restyle.
    [[nodiscard]] int presetTargetCount() const;
    //! Whether those objects include a balloon (@p bubble) and a text object (@p text): which presets apply.
    void presetTargetKinds(bool& bubble, bool& text) const;
    //! *Reset to default*: the selected objects' look set to the default of their kind, their origin cleared,
    //! as one history step — applyPresetToSelection() with the default, which is no preset.
    void resetSelectionLook();

    /**
     * \brief Moves the selected object onto page @p pageUid, at the same offset from that page's top.
     * \param pageUid  The UID of the page to move the object to.
     */
    void reanchorSelection(const QString& pageUid);

    void syncItems();    //!< Reconciles the scene items with the overlay set, by uid.
    //! Gives every object the mask objects above it that meet it (Object::setCuts()): after a sync and during a drag.
    void refreshCuts();
    /**
     * \brief While a mask is selected (an object's own, picked, or a mask object), the strip round its regions is
     *        dimmed, so which mask is worked on can be seen; otherwise nothing. At the moments refreshCuts() is called,
     *        and when the selection changes.
     */
    void refreshDim();
    //! Removes objects @p uids, their compositions with them, as one history step called @p undoText; nothing selected.
    void removeObjects(const QStringList& uids, const QString& undoText);
    void reselect();     //!< Re-applies the current selection after the scene was rebuilt.

    /**
     * \brief While set, the list <-> scene selection callbacks are ignored: a programmatic rebuild is not a
     * \brief user action, and \c QGraphicsScene::clear() drops the selection on its way through.
     */
    void setSyncing(bool on) { m_syncingList = on; }

    /**
     * \brief The scene is about to delete every item: drop the pointers. Nothing is lost — the objects only
     *        show the document, which keeps an edit previewed but not yet settled too.
     */
    void forgetItems()
    {
        m_overlayItems.clear();
        m_chrome = nullptr;   // the scene's too; syncItems() makes a new one
        m_dim    = nullptr;   // likewise; refreshDim() makes a new one
    }

    /**
     * \brief Re-reads where the selection's grips are. The objects report their own changes; the zoom is
     *        the owner's to report, because the grips are a fixed size on screen.
     */
    void refreshChrome();

    /**
     * \brief Returns true if an existing object sits under \p scenePos.
     * \param scenePos  The position to check.
     * \param deviceTransform  The transform for the device coordinates.
     * \return True if an object is at the specified position.
     */
    [[nodiscard]] bool objectAt(const QPointF& scenePos, const QTransform& deviceTransform) const;

    /**
     * \brief What the pointer is over at @p scenePos, for whoever has to decide a cursor or a gesture.
     * \param scenePos  The position to check.
     * \param deviceTransform  The transform for the device coordinates.
     * \return The target under the pointer.
     */
    [[nodiscard]] PointerTarget pointerTargetAt(const QPointF& scenePos,
                                                const QTransform& deviceTransform) const;

    /**
     * \brief Puts @p file down centred on @p scenePos, **at its own size** — a drop from the TOOL OPTIONS
     * preview.
     *
     * Its own size, deliberately, and not fitted to anything: a sound effect that reaches past the
     * strip's edge is a thing artists want, and a placement that quietly shrank it would be a decision
     * nobody asked for. *Fit to strip width* is one menu entry away for when they did.
     * 
     * \param file  The file to place.
     * \param scenePos  The position to place the file at, in scene coordinates.
     */
    void placePictureAt(const QString& file, const QPointF& scenePos);

    /**
     * \brief Draws the selected artwork at @p percent of its own pixels — 100 is one for one.
     *
     * A percentage rather than a width, because the artist's question is *how much bigger than I drew
     * it*, and because the answer has to survive a re-profile: the stored form is a fraction of the
     * page, so the same percentage means the same thing at 800 or 1600 points wide.
     *
     * \param percent  The percentage to scale the artwork by.
     * \param commit   False while the spin box is moving — the object resizes, the history does not.
     */
    void scaleSelectedPicture(double percent, bool commit = true);

    [[nodiscard]] double selectedPicturePercent() const; //! What that percentage currently is, or 0 when the selection is not one piece of artwork.

    /**
     * \brief Gives every selected object the blend mode @p blend, as one history step.
     *
     * Blend has been in the model, the compositor and this preview since the library shipped it, and
     * nothing could reach it: both creation sites wrote `Over` and no widget offered another. It is a
     * property every object has, so a set takes it the way a set takes a colour.
     * 
     * \param blend  The blend mode to apply to the selection.
     */
    void setSelectionBlend(Platemaker::Models::BlendMode blend);

    /**
     * \brief The mode the whole selection is composited in, or no value when they disagree.
     *
     * One computation for the menu's tick and for OBJECT STATE's row, because they answer the same question
     * and a tick that disagreed with the row beside it would make one of them wrong.
     * 
     * \return The blend mode of the selection, or std::nullopt if they disagree.
     */
    [[nodiscard]] std::optional<Platemaker::Models::BlendMode> selectionBlend() const;

    /**
     * \brief Removes everything selected, as one history step.
     *
     * Public because more than the menu asks for it: OBJECT STATE offers Delete for whichever kind of object
     * it is showing. What it deletes is the selection, which is the only thing any of them mean by it.
     */
    void deleteSelectedOverlay();

    //! Shape editing: its state, its input on the strip and its operations (the menu and CanvasInput drive it).
    [[nodiscard]] ShapeEditMode&       shapeEdit() { return *m_shapeEdit; }
    [[nodiscard]] const ShapeEditMode& shapeEdit() const { return *m_shapeEdit; }

    /**
     * \brief *Re-roll*: a new style seed for every selected balloon whose style draws on one
     *        (a flash, a brush, a texture), so its strokes or its wobble are dealt again. One history step.
     */
    void rerollSelection();
    //! Whether rerollSelection() has a balloon to re-roll.
    [[nodiscard]] bool canReroll() const;

    /**
     * \brief Checks if exactly one piece of imported artwork is selected — what OBJECT STATE and the menu
     * both ask.
     * \return True if exactly one piece of imported artwork is selected, false otherwise.
     */
    [[nodiscard]] bool selectionIsPicture() const;

    // --- what OBJECT STATE shows, and where what it says goes: wired by the Editor ---
    [[nodiscard]] PanelBinder& panel() { return *m_panel; }
    //! OBJECT STATE's *Fit*: grows or shrinks the selected object's box to its words. Not a picture.
    void fitSelectionToText();
    //! Whether *Add words* applies: one balloon or picture selected (or words of it), its words on a seat.
    [[nodiscard]] bool canAddWords() const;
    //! *Add words*: new words on the selected object (Edits::addWords()), from the picked words; picked, the caret in them.
    void addWords();
    //! Whether *Add bubble* applies: one balloon selected, or a bubble of it, that bubble (or the chain's last) an end.
    [[nodiscard]] bool canAddBubble() const;
    //! Whether the picked bubble can be deleted: one of a chain of more than one (a middle one splits it).
    [[nodiscard]] bool canDeleteSelectedBubble() const;
    /**
     * \brief *Add bubble*: a new bubble after the picked one (or the chain's last), below and to the right, bridged
     *        to it, with empty words in its lettering (Chain::addBody()); picked.
     */
    void addBubble();
    //! Whether *Add mask to object* applies: one object selected, or an element of it, with no mask of its own yet.
    [[nodiscard]] bool canAddMask() const;
    //! *Add mask to object*: a mask of the selected object's own (Edits::addMask()); picked.
    void addMask();
    //! The one mask object selected: whether it is Hued (Mask::hued). None for any other selection.
    [[nodiscard]] std::optional<bool> selectionHued() const;
    //! Shows the one mask object selected Hued or Applied (Mask::hued), one history step; the render is unchanged.
    void setSelectionHued(bool hued);
    //! Whether the mask tools paint: one mask selected, a mask object or an object's own mask picked.
    [[nodiscard]] bool canPaintMask() const;
    /**
     * \brief A mask tool's click with nothing it can paint selected: the topmost mask at @p scenePos selected, a mask
     *        object whose regions cover it or an object's own mask where it hides it (Geometry::maskHidesAt()). Never
     *        an object of any other kind; nothing where there is no mask.
     */
    void pickMaskAt(const QPointF& scenePos);
    /**
     * \brief A stroke of the mask brush on the selected object: @p sceneArea hidden (@p hide) or shown
     *        (Edits::paintMask()), fitted within half a screen pixel; one history step, the mask then picked (the
     *        object, when it was erased to nothing).
     */
    void paintMask(const QPainterPath& sceneArea, bool hide);
    //! Whether *Join* applies: two balloons selected, whole (not as a tail's carrier), each with a chain end.
    [[nodiscard]] bool canJoin() const;
    /**
     * \brief *Join*: the second balloon picked taken into the first where it lies on the strip, bridged between the
     *        chain ends nearest each other (Chain::join()); its overlay removed, the first keeping its own and its look.
     *        One undo step; the joined balloon selected.
     */
    void joinSelection();
    //! Whether a tail's tip is held over a bubble it would join on release (counting, or lit): Esc lets go of it.
    [[nodiscard]] bool joinHeld() const;
    //! Esc while held: that bubble does not join, and counts again only once the tip has left it; whether one was held.
    bool cancelJoin();

    // --- a new object: asked for here, created by the owner, selected when it comes back in the feed ---
    //! Asks the owner for a new authored object; it is selected when the feed brings it back.
    void requestComposition(const Composition& composition, double xFrac, double yFrac, double wFrac,
                       const QString& anchorInputUid);
    //! Asks the owner to import @p file as a new picture; it is selected when the feed brings it back.
    void requestPicture(const QString& file, double xFrac, double yFrac, double wFrac, QSize naturalSize,
                        const QString& anchorInputUid);

    /**
     * \brief What TOOL OPTIONS says the next object is — read, never written.
     *
     * Two questions, given as functions so that this class needs no tool-options panel: the prototype an
     * object would be placed as, and the balloon one would wear, which *Convert to ▸ Balloon* arrives at
     * (a balloon even while the Text tool is armed). The Editor, which owns both regions, wires them.
     */
    struct ToolDefaults
    {
        std::function<Composition()> prototype;
        std::function<BalloonLook()> balloon;
    };
    void setToolDefaults(ToolDefaults defaults) { m_toolDefaults = std::move(defaults); }

signals:
    /**
     * \brief A bubble was drawn. Creation is the library's — it mints the uid and dedups identical artwork.
     * \param composition  What the new object is.
     * \param xFrac, yFrac, wFrac  The new object's position and size as fractions of the page dimensions.
     * \param anchorInputUid  The UID of the anchor input.
     */
    void compositionCreated(const Composition& composition, double xFrac, double yFrac, double wFrac,
                         const QString& anchorInputUid);
    /**
     * \brief Any other edit, as the complete new state: one channel rather than one signal per gesture.
     * \param overlays  The new overlay set, in composite order.
     * \param compositions  The new compositions of the overlays, keyed by uid.
     * \param undoText  The text to use for the undo step that will be created
     * \param created  The compositions of the overlays with no uid yet, in their order: new ones, made in the same
     *                 step (a balloon split in two), their uids minted by the library.
     */
    void overlaysCommitted(const std::vector<Platemaker::Models::StripOverlay>& overlays,
                        const Composition::Map& compositions, const QString& undoText,
                        const QList<Composition>& created);
    /**
     * \brief Artwork drawn elsewhere should be copied into the workspace and registered here.
     *
     * @p naturalSize is the picture's own pixels, read through the one loader that knows how to ask an
     * SVG its size. It travels because the far end cannot work it out: it sees only the copy it has
     * just written, and guessing there once produced a picture whose size was a fraction times 1000.
     * \param sourceFile  The file to copy into the workspace.
     * \param xFrac, yFrac, wFrac  The new object's position and size as fractions of the page dimensions.
     * \param naturalSize  The picture's own pixel dimensions.
     * \param anchorInputUid  The UID of the anchor input.
     */
    void pictureImportRequested(const QString& sourceFile, double xFrac, double yFrac, double wFrac,
                                QSize naturalSize, const QString& anchorInputUid);
    /**
     * \brief The selection moved to @p subject. @p uid is the overlay's uid or the page's input uid, and empty
     * \param subject  The new selected subject.
     * \param uid      The UID of the selected object, or an empty string if none.
     */
    void subjectChanged(StripEdit::Subject subject, const QString& uid);
    // --- for the OBJECT STACK: what changed, so the rows can follow ---
    void stackChanged();               //!< The objects, their order, labels, looks or pages changed.
    void selectionChanged();           //!< The selected objects and tails changed (rows, menu entries).
    void subjectRowChanged();          //!< The strip or a page became the selection.
    void revealSelectionRequested();   //!< An undo selected something: bring its row into view.

    void textFocusRequested();      //!< A bubble just placed, or its words pressed in shape editing: the caret to its text.

private:
    void onOverlayGeometryCommitted(const QString& uid); //!< An item settled a move/resize/tail drag.
    void writePlacement(const QString& uid, bool rehome = true); //<! Writes where object @p uid now stands back into its composition — placement, width and, if @p rehome, anchor page.
    void onObjectPressed(const QString& uid, int handle); //!< A press on a tail's handle selects that tail.
    void onCompositionEdited(const QString& uid, const Composition& next); //!< A canvas drag's edit: the document first, then the object.
    void onObjectDragged(const QString& uid, const QPointF& delta, int handle); //<! An object reports a live drag; the selection decides what else travels with it.
    void beginDrag(const QString& uid, int handle); //<! Records where everything selected stands, so a group drag can place each from its own start.
    /**
     * \brief The library's rasterisation of \p a, cached by the SVG it emits. Empty if it cannot be produced.
     */
    [[nodiscard]] QImage sharpRasterFor(const Composition& a);

    /**
     * \brief Selects exactly @p uids, in the order given. The **last** is the primary.
     *
     * The primary is what a single-subject action acts on and what OBJECT STATE shows when there is only one
     * — "the thing I just clicked", which is the last one picked. Everything written before the selection was
     * a set still reads selectedOverlay(), which is now that primary.
     */
    void selectOverlays(const QStringList& uids);



    /**
     * \brief How many things the artist actually picked — a balloon carrying someone's selected tail is not one.
     */
    [[nodiscard]] int selectedSubjectCount() const;
    /**
     * \brief Selects the strip or a page: every overlay deselected, that one row selected, the subject reported.
     */
    void selectSubject(Subject subject, const QString& pageUid);
    /**
     * \brief Which file an asset item draws: the imported picture, which is not always the overlay's own.
     */
    [[nodiscard]] QString pictureFor(const Platemaker::Models::StripOverlay& o) const;
    /**
     * \brief Live edit from the panel → item (+persist, as a step named @p undoText or for the subject).
     */
    void pushOverlays(const QString& undoText); //!< Emits overlaysCommitted() with the current state.
    /**
     * \brief Writes @p compositions (compositions) onto the objects @p uids names — **the one write path**.
     *
     * There were three: one for a single balloon, one for several, and one for a picture. They differed
     * in a `qobject_cast` and in what the history step was called, and the two that only accepted a
     * balloon dropped a picture silently — so the bucket, applied to a balloon and a lettered picture
     * together, coloured one of them and told the panel it had coloured both.
     *
     * **Pairing is stated, not inferred.** The old multi-object path matched its list to
     * `m_selection.objects()` by position and refused when the two lengths disagreed, which is what made a
     * mixed selection uneditable. Here the caller says which object each composition is for.
     *
     * \param uids     The uids of the objects to apply the compositions to.
     * \param compositions  The compositions to apply to the objects.
     * \param commit False while a control is being dragged or typed in: the objects repaint and nothing
     *               reaches the history. The panel's own debounce decides when an edit has settled.
     * \param undoText  The text to use for the undo step that will be created. If empty, a default text will be used.
     */
    void applyCompositions(const QStringList& uids, const QList<Composition>& compositions, bool commit,
                      const QString& undoText = QString());

    /**
     * \brief One object, by uid — the same path, for the callers that act on the primary selection.
     * \param uid  The uid of the object to apply the composition to.
     * \param composition  The composition to apply to the object.
     * \param commit  False while a control is being dragged or typed in: the object
     */
    void applyComposition(const QString& uid, const Composition& composition, bool commit,
                     const QString& undoText = QString());

    //! @p preset's look onto every selected object we draw, as one history step named @p step.
    void applyLookToSelection(const BubblePreset& preset, const QString& step);

public:
    //! Makes @p origin the one selected object's origin, as one history step: what *Save as preset…* does to
    //! the object it saved from.
    void setSelectionOrigin(const PresetOrigin& origin);

private:

    /**
     * \brief When @p next's box differs from what @p item holds — *Fit*, *Convert* — moves the tail tips
     *        back so they keep pointing where they did.
     * \return How far the item has to move, in its own units, for the lettering to stay where it is on
     *         the strip; null when the box did not change.
     */
    [[nodiscard]] QPointF settleBox(const Object* item, Composition& next) const;

    void deleteSelectedTail();
    //! Delete with words picked: those words, never the last (Edits::removeWords()); the object is what is left to hold.
    void deleteSelectedWords();
    //! Delete with a bubble picked: that bubble, its words and tails; a middle one splits the chain (applyPieces()).
    void deleteSelectedBubble();
    //! Delete with a mask picked: the mask (Edits::removeMask()); its object is what is left to hold.
    void deleteSelectedMask();
    //! The two balloons @p keep and @p other as @p joined, in @p keep: @p other's overlay removed, one undo step.
    void commitJoin(const QString& keep, const QString& other, const Composition& joined);
    /**
     * \brief The bubble tail @p tail of balloon @p uid is held over: of the topmost other balloon under its tip;
     *        empty for none, or for a bridge. Whether the two may join there is Chain::canJoinByTail()'s.
     */
    [[nodiscard]] ElementRef joinTargetAt(const QString& uid, int tail) const;
    //! A tail's tip moved: a new bubble under it starts the hold's count; none, or another, drops the last.
    void aimJoin(const QString& uid, int tail);
    //! Lights the held bubble, red or grey as Chain::canJoinByTail() says, or puts it out.
    void setJoinLit(bool on);
    //! Forgets the hold: the count stopped, the light out, no target.
    void dropJoin();
    //! The release over a lit bubble: the held tail made the bridge to it (Chain::makeBridge()); whether it joined.
    bool joinByTail();
    /**
     * \brief Object @p uid as @p pieces, one commit labelled @p undoText: the first stays the object, held; each
     *        other becomes a new overlay over it, where it stood on the strip, on the same page, in a copy of its look.
     */
    void applyPieces(const QString& uid, const QList<Composition>& pieces, const QString& undoText);
    //! Whether @p ref's object still has those words.
    [[nodiscard]] bool hasWords(const ElementRef& ref) const;
    //! Whether @p ref's object still has that body.
    [[nodiscard]] bool hasBody(const ElementRef& ref) const;
    //! Whether @p ref's object still has a mask, that region its.
    [[nodiscard]] bool hasMask(const ElementRef& ref) const;

    //! A press in body @p id of @p uid's balloon, already selected: picks it, unless the same press picked words.
    void onBodyPressed(const QString& uid, int id);
    //! Whether the press being handled picked words (onObjectPressed()), which a bubble then leaves alone.
    bool m_pressPickedWords = false;
    //! The selection just made, shown: the items selected, their tails and words marked, the panel bound.
    void showSelection();

    //! Tail @p index of @p uid's balloon, as a selection holds it: by its element id (0 when there is none there).
    [[nodiscard]] ElementRef tailRef(const QString& uid, int index) const;
    //! Where tail @p ref is among its balloon's tails; -1 when it is gone.
    [[nodiscard]] int tailIndex(const ElementRef& ref) const;   //!< Takes the selected tail off its balloon, and selects the balloon.




    // --- collaborators, not owned ---
    QGraphicsScene* m_scene        = nullptr;   //!< The scene that draws the strip and its overlays.
    QGraphicsView*  m_view         = nullptr;   //!< The view that shows the scene, and whose transform is used for hit-testing. 
    ToolDefaults       m_toolDefaults;                //!< What TOOL OPTIONS says the next object is.
    PresetStore&      m_presets;                //!< The store of named presets, which the menu reads from and the save action writes to.
    const StripLayout&   m_layout;                   //!< The layout that owns the strip, for page names and sizes.

    Document                                      m_document;   //!< The objects: their overlays and compositions.
    QHash<QString, Object*>                       m_overlayItems; //!< Live scene objects, keyed by overlay uid.
    SelectionChrome*                              m_chrome = nullptr; //!< The selection's grips, above every object; owned by the scene.
    QGraphicsPathItem*                            m_dim    = nullptr; //!< The strip dimmed round a selected mask (refreshDim()); the scene's.
    /**
     * \brief Library rasterisations of bubbles with a filter style, keyed by the SVG document itself.
     *
     * Keyed by the bytes rather than by the overlay's stored hash, and rendered from those same bytes
     * rather than from the file — because the file is the wrong thing to ask. A workspace can live on a
     * synced drive, where reading a file back immediately after writing it may still return the previous
     * content; keying and rendering off the buffer in hand makes the preview show what is being edited,
     * and leaves the file to matter only when a render reads it.
     *
     * Only bubbles with a filter (Geometry::hasFilter()) are in here — any other is the same geometry
     * either way, a flash included, so rasterising it would buy nothing.
     * ponytail: rasterised synchronously, on the UI thread. One bubble per settled edit is a few ms;
     * opening a chapter with dozens of filtered bubbles is the case that would want QtConcurrent.
     */
    QHash<QString, QImage>  m_sharpCache;
    SelectionModel     m_selection;   //!< What is selected; this class draws and binds what it says.
    PanelBinder*       m_panel = nullptr;   //!< What OBJECT STATE shows; a child of this object.
    ShapeEditMode*     m_shapeEdit = nullptr;   //!< Shape editing; a child of this object.

    // --- a drag in flight: where everything stood when it started ---
    QHash<QString, QPointF>         m_dragStartPos;   //!< Object uid → its position at the press.
    QList<QPair<ElementRef, QPointF>> m_dragStartTips;  //!< Tail → its tip, in its balloon's own units.
    bool                            m_dragIsGroup = false;

    // --- the hold-to-join gesture: a tail's tip held over another balloon's bubble, then released ---
    QString    m_joinFrom;             //!< The balloon whose tail is aimed.
    int        m_joinTail = -1;        //!< That tail, by index.
    ElementRef m_joinAt;               //!< The bubble under its tip (joinTargetAt()); empty for none.
    QTimer*    m_joinHold = nullptr;   //!< Runs while the tip is held there; lights the bubble when it fires.
    bool       m_joinLit  = false;     //!< The bubble is lit: releasing now joins, where the two may join.
    //! How far off a hairline outline or a thin letter a press may land and still count, in screen px.
    static constexpr qreal k_pickSlackPx = 3.0;
    //! How near a mask brush stroke's regions keep to it, in screen px: closer than the eye can tell.
    static constexpr qreal k_maskFitPx = 0.5;
    //! How many screen pixels one of @p item's own pixels is, at the zoom shown.
    [[nodiscard]] qreal screenScale(const Object& item) const;

    bool               m_syncingList     = false;           //!< Guards the scene's selection round-trip.
    /**
     * \brief Set when this controller asked for a new bubble; the uid only exists after the owner mints it, so
     *        the selection has to wait for the feed to come back.
     */
    bool               m_selectNewOverlay = false;
    /**
     * \brief Set when a history step is about to arrive: the objects it touched, to select on that feed. A
     *        different question from m_selectNewOverlay — that one means "whatever was appended", because
     *        there was no uid to name yet; this one names them.
     */
    QStringList        m_selectAfterFeed;
};

}  // namespace StripEdit

#endif // STRIPEDIT_OBJECTCONTROLLER_HPP
