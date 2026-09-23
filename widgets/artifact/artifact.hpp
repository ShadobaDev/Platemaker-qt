#ifndef ARTIFACT_HPP
#define ARTIFACT_HPP

#include <QColor>
#include <QHash>
#include <QList>
#include <QStringView>
#include <QtGlobal>
#include <QPointF>
#include <QJsonObject>
#include <QPoint>
#include <QSize>
#include <QString>

/**
 * @brief What a bubble *is* — the editable source a strip overlay is rendered from.
 *
 * The library rasterises artwork and deliberately never grows a text engine, so everything about a
 * bubble's content lives here, on the GUI side. This struct is the *working* form; the SVG in
 * `<workspace>/overlays/` is the stored one, and it carries these same values in a private namespace
 * so a bubble can be re-solved from the file it renders from (see artifactsvg.hpp).
 *
 * Coordinates are **strip-scale pixels**, the same scale the render composites at, so the scene preview
 * and the baked output are the same geometry by construction (see artifactpainter.hpp).
 *
 * One struct serves both rail tools: the Text tool is this with `shape == Shape::None`. Two entry
 * points, one object — so a caption can grow a balloon later without changing type, and there is one
 * rasteriser, one schema and one list.
 */
/**
 * @brief One tail: where it points, how wide it leaves the balloon, and how much it curves.
 *
 * The tip is in balloon coordinates and **may fall outside the balloon** — that is the whole point of a
 * tail, and it is why the artifact's drawn extent is computed rather than assumed (artifactBounds()).
 * Length is not stored: it is the distance from the balloon to the tip, so aiming and lengthening are
 * one gesture.
 */
struct Tail
{
    QPointF tip;                //!< Balloon coordinates; outside the balloon is normal.
    qreal   baseWidth = 34.0;   //!< Width where it emerges from the silhouette, in balloon pixels.
    qreal   bend      = 0.0;    //!< -1..+1 — sideways offset of the curve, as a fraction of its length.

    [[nodiscard]] bool operator==(const Tail& o) const
    {
        return tip == o.tip
            && qFuzzyCompare(baseWidth, o.baseWidth)
            && qFuzzyCompare(1.0 + bend, 1.0 + o.bend);   // +1 so an exact 0.0 compares equal
    }
    [[nodiscard]] bool operator!=(const Tail& o) const { return !(*this == o); }
};

/**
 * @brief Where a balloon's **first** tail points: down and a little left of centre.
 *
 * Which is where a reader expects a speech balloon to be speaking from. It lives beside the tail rather
 * than in whichever editor happens to seed one, because three of them do — the tails editor, the tail
 * list and a fresh placement — and a rule written out once per caller is a rule that drifts. A *later*
 * tail is seeded from the one before it, not from here.
 *
 * The shape tiles deliberately do **not** use this: a thumbnail draws a short tail so that a speaking
 * shape does not come out smaller than the rest of the grid, and that is a drawing tweak rather than a
 * different answer to this question.
 */
[[nodiscard]] inline QPointF firstTailTip(QSize box)
{
    return {box.width() * 0.28, box.height() * 1.25};
}

struct Artifact;

/**
 * @brief The balloon's own surface: what it is filled with, and the line drawn around it.
 *
 * The first **property group** — a named slice of an object's state with exactly one editor
 * responsible for it. Groups exist so that two editors can never write the same field: an editor
 * writes through applyTo() and applyTo() assigns one member, so "this editor touched something that
 * was not its own" stops being expressible rather than being something to remember.
 *
 * The text's colour is deliberately **not** here. Editing a balloon's frame does not edit its
 * lettering, so the colour tool's two swatches are the fill and the stroke, and the text's colour
 * belongs with the text.
 *
 * Persisted as three flat keys, exactly as before — the struct changed, the file format did not.
 */
struct SkinProperties
{
    QColor fill{255, 255, 255};
    QColor stroke{20, 20, 20};
    int    strokeWidth = 5;

    //! Reads this group out of \p a.
    [[nodiscard]] static SkinProperties from(const Artifact& a);

    /**
     * @brief Writes this group into \p a — **and nothing else**.
     *
     * The single write path for everything that edits a balloon's surface. Guarded by
     * `SkinPropertiesOwnership` in the unit tests, which applies a group to a randomised artifact and
     * asserts that every property outside it is unchanged.
     */
    void applyTo(Artifact& a) const;

    [[nodiscard]] bool operator==(const SkinProperties& o) const
    {
        return fill == o.fill && stroke == o.stroke && strokeWidth == o.strokeWidth;
    }
    [[nodiscard]] bool operator!=(const SkinProperties& o) const { return !(*this == o); }
};

/**
 * @brief Which silhouette is drawn behind the text.
 *
 * One property, and still a group: it has an owner, an editor and an ownership test like every other,
 * and a group with one property today is a group with room tomorrow.
 */
struct ShapeProperties
{
    /**
     * @brief The silhouette drawn behind the text. `None` is the Text tool: letters with no balloon.
     *
     * **Append only.** These are persisted by name, not by number (see shapeName()), so the order here
     * is free — but an existing value must keep its name or every saved bubble using it silently becomes
     * a speech balloon on the next load.
     */
    enum class Kind { None, Speech, Shout, Caption, Ellipse, Diamond, Trapezoid, Thought, Scroll, Banner };

    Kind kind = Kind::Speech;

    [[nodiscard]] static ShapeProperties from(const Artifact& a);
    void applyTo(Artifact& a) const;

    [[nodiscard]] bool operator==(const ShapeProperties& o) const { return kind == o.kind; }
    [[nodiscard]] bool operator!=(const ShapeProperties& o) const { return !(*this == o); }
};

/**
 * @brief How the outline is drawn, as opposed to what it is.
 *
 * The **seed is not in here**, deliberately. It is per balloon and set once at placement, so a preset
 * that carried it would give a whole chapter one repeated wobble — and a group's applyTo() writes the
 * whole group, which would do exactly that. It stays a bare field with no editor.
 */
struct StyleProperties
{
    /**
     * @brief Every value but `Clean` is an SVG filter, so the artwork stays the same geometry and the
     * effect happens at rasterise time — which means librsvg applies it and Qt cannot. That is the
     * whole reason a styled bubble is previewed through the library instead of being drawn locally: an
     * effect only the committed output could show would be an effect nobody could author.
     *
     * **Append only**, and persisted by name — see styleName().
     */
    enum class Kind { Clean, Marker, Ink };

    Kind  kind   = Kind::Clean;  //!< Clean is a true no-op: no filter is emitted at all.
    qreal amount = 1.0;          //!< Scales the effect, 0..2. 1.0 is the preset's own strength.

    [[nodiscard]] static StyleProperties from(const Artifact& a);
    void applyTo(Artifact& a) const;

    [[nodiscard]] bool operator==(const StyleProperties& o) const
    {
        return kind == o.kind && qFuzzyCompare(1.0 + amount, 1.0 + o.amount);
    }
    [[nodiscard]] bool operator!=(const StyleProperties& o) const { return !(*this == o); }
};

/**
 * @brief What the balloon says, and how it is set.
 *
 * Content and typography in one group because one widget edits both, and because the split that
 * matters is elsewhere: a **preset** carries the typography and never the content — *everything a
 * balloon is, minus everything it says*. The colour is here rather than with the fill and stroke for
 * the same reason the colour tool has only two swatches: editing a frame is not editing its lettering.
 *
 * Shared, unchanged, by whatever has text — a balloon, a caption, and eventually a standalone text
 * object — rather than each kind carrying its own copy of the same six properties.
 */
struct TextProperties
{
    QString body;                     //!< The lettering itself.
    QString family;                   //!< Empty = the application's default family.
    int     pixelSize = 30;           //!< Strip-scale pixels, so it means the same thing in the output.
    bool    bold      = false;
    int     align     = Qt::AlignHCenter;   //!< Horizontal alignment of the wrapped text.
    QColor  colour{20, 20, 20};

    [[nodiscard]] static TextProperties from(const Artifact& a);
    void applyTo(Artifact& a) const;

    [[nodiscard]] bool operator==(const TextProperties& o) const
    {
        return body == o.body && family == o.family && pixelSize == o.pixelSize && bold == o.bold
            && align == o.align && colour == o.colour;
    }
    [[nodiscard]] bool operator!=(const TextProperties& o) const { return !(*this == o); }
};

/**
 * @brief Every tail on the balloon.
 *
 * The whole list is one group today because a tail is not yet addressable on its own: there is no way
 * to say *which* tail, which is why one can be added and never removed. When tails become objects in
 * their own right this splits into a group per tail, and the list stops being a property at all.
 */
struct TailsProperties
{
    QList<Tail> items;   //!< Empty = no tail. More than one = one sound, several speakers.

    [[nodiscard]] static TailsProperties from(const Artifact& a);
    void applyTo(Artifact& a) const;

    [[nodiscard]] bool operator==(const TailsProperties& o) const { return items == o.items; }
    [[nodiscard]] bool operator!=(const TailsProperties& o) const { return !(*this == o); }
};

/**
 * @brief One tail of a balloon — the group a selected tail is edited through.
 *
 * A tail is an object of its own, and this is its state. It is addressed by **position** in the balloon's
 * list rather than by an id: undo restores whole states and holds no tail by number, so an index is enough,
 * and a selection that outlives a change in the number of tails simply moves up to its balloon.
 *
 * applyTo() writes that one tail and leaves the others exactly as they were; an index the balloon no longer
 * has writes nothing, rather than resurrecting a tail that was deleted.
 */
struct TailProperties
{
    int  index = 0;     //<! Which tail in the balloon's list is being edited. Out of range = no tail.
    Tail tail;          //<! The tail's own state, which applyTo() writes into the balloon at `index`.

    /**
     * @brief Reads the tail at \p index out of \p a, or an empty tail if the index is out of range.
     * @param a The balloon to read from.
     * @param index The tail to read, 0..n-1. Out of range = no tail, and applyTo() will write nothing.
     * @return The tail at \p index, or an empty tail if the index is out of range.
     * @see applyTo()
     */
    [[nodiscard]] static TailProperties from(const Artifact& a, int index);
    
    /**
     * @brief Writes the tail at \p index into \p a, or does nothing if the index is out of range.
     * @param a The balloon to write to.
     * @see from()
     */
    void applyTo(Artifact& a) const;

    /**
     * @brief Compares two tail properties for equality.
     * @param o The other tail properties to compare with.
     * @return True if the tail properties are equal, false otherwise.
     */
    [[nodiscard]] bool operator==(const TailProperties& o) const { return index == o.index && tail == o.tail; }

    /**
     * @brief Compares two tail properties for inequality.
     * @param o The other tail properties to compare with.
     * @return True if the tail properties are not equal, false otherwise.
     */
    [[nodiscard]] bool operator!=(const TailProperties& o) const { return !(*this == o); }
};

struct Artifact
{
    //! The enums live with the groups that own them; these keep every existing spelling working.
    using Shape = ShapeProperties::Kind;
    using Style = StyleProperties::Kind;

    ShapeProperties shape;  //<! What silhouette is drawn behind the text, if any.

    /**
     * @brief The imported picture this object **is** — a file name in the workspace's `overlays/`.
     *
     * Empty for an object we draw ourselves. Non-empty and the drawing is somebody else's: our
     * silhouette, our line style and our tails have nothing to act on, and what is left that we can
     * still do is put lettering over it (V5b) and decide how big it is drawn.
     *
     * **This is the third kind, and it lives in the same record on purpose.** A second map keyed by the
     * same uid would be a second channel carrying the same object, and the two would drift; a record
     * that says what it is keeps one. A *file name* rather than a path so the workspace stays portable,
     * and a name inside `overlays/` rather than the import's source so it stays self-contained — the
     * source may be anywhere, or gone.
     */
    QString artwork;

    /**
     * @brief The **balloon** — what the author drags, and what text wraps inside.
     *
     * Not the artifact's drawn extent: a tail may reach well outside it, so the size of the rendered
     * artwork comes from artifactBounds(). Until tails could point anywhere these were one rectangle,
     * with a fixed fraction of the height reserved at the bottom for the tail to live in.
     */
    QSize box{280, 160};

    TailsProperties tails;
    StyleProperties style;

    /**
     * @brief Seeds the filter's noise, so two bubbles do not wear identical wobble.
     *
     * Stored rather than derived, because it must survive an edit: re-rolling it on every keystroke
     * would make the outline crawl while you type. Set once, when the bubble is placed.
     */
    quint32 styleSeed   = 0;

    TextProperties text;
    SkinProperties skin;   //!< Fill, stroke and stroke width — see SkinProperties.

    //! True when a tail should be drawn. A shapeless artifact has nothing to grow a tail from.
    /**
     * @brief Whether this object has a balloon behind its lettering — **the one structural question**.
     *
     * It decides which property groups the object carries at all: a fill and an outline to paint, a
     * line style to roughen them with, and something for a tail to leave from. Everything else about
     * `shape` — speech, thought, caption, banner — is one property with one editor, which is why
     * *which* silhouette is picked in a panel and *whether there is one* is `Convert to ▸`.
     *
     * Written out by hand in twenty places before it had a name, in both polarities.
     */
    //! Somebody else drew this one: the drawing is `artwork`, and none of our geometry applies to it.
    [[nodiscard]] bool isArtwork() const { return !artwork.isEmpty(); }

    [[nodiscard]] bool hasSilhouette() const { return !isArtwork() && shape.kind != Shape::None; }

    [[nodiscard]] bool hasTail() const { return hasSilhouette() && !tails.items.isEmpty(); }

    [[nodiscard]] bool operator==(const Artifact& o) const;
    [[nodiscard]] bool operator!=(const Artifact& o) const { return !(*this == o); }
};

/**
 * @brief Gives \p a a style seed if it is styled and has none — **the one place that mints one**.
 *
 * The seed belongs to no property group, precisely so that no editor and no preset can copy it: a page
 * of marker balloons all wearing the same wobble is the failure it exists to prevent. That makes *who
 * mints it* a question with exactly one right answer, and it is here — four callers had each written
 * the rule out, and one of them had already diverged into minting a seed for balloons that have no
 * style to wobble.
 *
 * Idempotent: a record that already has a seed keeps it, so re-styling never re-rolls the wobble.
 */
void topUpStyleSeed(Artifact& a);

//! Authoring records for one project's overlays, keyed by `StripOverlay::uid`.
using ArtifactMap = QHash<QString, Artifact>;

/**
 * @brief The persisted name of \p s, and the way back.
 *
 * One mapping, used by both persistence paths — the undo snapshot's JSON and the SVG's `pm:shape`. It
 * used to be a positional array in one and a switch in the other, which meant appending a shape was
 * a silent out-of-bounds read on one side and a compiler error on neither.
 */
[[nodiscard]] const char* shapeName(Artifact::Shape s);

/**
 * @brief What to call \p s on screen, translated — as opposed to shapeName(), which is what it is
 *        called in a file.
 *
 * The two are deliberately separate: a persisted name may never change, and a displayed one must be
 * free to. It lives here rather than in the shape picker because the picker is no longer the only
 * thing that names a shape — *Convert to ▸* does too, and two lists would drift.
 */
[[nodiscard]] QString shapeTitle(Artifact::Shape s);

/**
 * @brief Every **silhouette**, in the order the pickers offer them. Not the enum's order, which is
 *        append-only and therefore historical.
 *
 * `Shape::None` is deliberately **not** in it. It is not a silhouette to choose between; it is the
 * absence of one, which is a different *kind* of object — no fill, no outline, no line style, nothing
 * for a tail to leave from. Choosing it belongs to *Convert to ▸*, and a picker that offered it would
 * let a property control change what the object is.
 */
[[nodiscard]] const QList<Artifact::Shape>& shapeOrder();
//! Parses \p name; anything unrecognised falls back to Speech, so an unknown shape still draws.
[[nodiscard]] Artifact::Shape shapeFromName(QStringView name);

//! The persisted name of \p s, and the way back — same contract as shapeName().
[[nodiscard]] const char* styleName(Artifact::Style s);
[[nodiscard]] Artifact::Style styleFromName(QStringView name);

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

[[nodiscard]] QJsonObject artifactToJson(const Artifact& a);
[[nodiscard]] Artifact artifactFromJson(const QJsonObject& j);

//! A whole map, keyed by overlay uid — the shape the undo snapshot stores.
[[nodiscard]] QJsonObject artifactsToJsonObject(const ArtifactMap& m);
[[nodiscard]] ArtifactMap artifactsFromJsonObject(const QJsonObject& j);

/**
 * @brief Every open project's authoring records, keyed by project uid.
 *
 * A **cache**, not a store: the records live in the overlays' own SVG files (see artifactsvg.hpp), and
 * this holds the parsed form so a populate() does not re-read and re-parse the whole chapter. It is
 * repopulated from disk when a workspace is opened and written through whenever the editor commits.
 *
 * There is deliberately nothing to save here. An authoring sidecar would be a second copy of what the
 * asset already carries — one more file to keep in step, and one more thing to lose separately from the
 * artwork it describes.
 */
class ArtifactStore
{
public:
    //! `<workspace dir>/overlays` — where the SVG assets live; created on demand by ensureDir().
    [[nodiscard]] static QString overlaysDir(const QString& workspacePath);
    //! Creates the overlays directory if missing. Returns its path, or empty if it cannot be created.
    [[nodiscard]] static QString ensureOverlaysDir(const QString& workspacePath);

    void clear() { m_byProject.clear(); }

    [[nodiscard]] ArtifactMap        artifacts(const QString& projectUid) const;
    void                             setArtifacts(const QString& projectUid, ArtifactMap map);

private:
    QHash<QString, ArtifactMap> m_byProject;   //!< project uid → (overlay uid → artifact)
};

#endif // ARTIFACT_HPP
