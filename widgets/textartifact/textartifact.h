#ifndef TEXTARTIFACT_H
#define TEXTARTIFACT_H

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
 * so a bubble can be re-solved from the file it renders from (see artifactsvg.h).
 *
 * Coordinates are **strip-scale pixels**, the same scale the render composites at, so the scene preview
 * and the baked output are the same geometry by construction (see artifactpainter.h).
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

struct TextArtifact;

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
    [[nodiscard]] static SkinProperties from(const TextArtifact& a);

    /**
     * @brief Writes this group into \p a — **and nothing else**.
     *
     * The single write path for everything that edits a balloon's surface. Guarded by
     * `SkinPropertiesOwnership` in the unit tests, which applies a group to a randomised artifact and
     * asserts that every property outside it is unchanged.
     */
    void applyTo(TextArtifact& a) const;

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

    [[nodiscard]] static ShapeProperties from(const TextArtifact& a);
    void applyTo(TextArtifact& a) const;

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

    [[nodiscard]] static StyleProperties from(const TextArtifact& a);
    void applyTo(TextArtifact& a) const;

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

    [[nodiscard]] static TextProperties from(const TextArtifact& a);
    void applyTo(TextArtifact& a) const;

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

    [[nodiscard]] static TailsProperties from(const TextArtifact& a);
    void applyTo(TextArtifact& a) const;

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
    int  index = 0;
    Tail tail;

    [[nodiscard]] static TailProperties from(const TextArtifact& a, int index);
    void applyTo(TextArtifact& a) const;

    [[nodiscard]] bool operator==(const TailProperties& o) const { return index == o.index && tail == o.tail; }
    [[nodiscard]] bool operator!=(const TailProperties& o) const { return !(*this == o); }
};

struct TextArtifact
{
    //! The enums live with the groups that own them; these keep every existing spelling working.
    using Shape = ShapeProperties::Kind;
    using Style = StyleProperties::Kind;

    ShapeProperties shape;

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
    [[nodiscard]] bool hasSilhouette() const { return shape.kind != Shape::None; }

    [[nodiscard]] bool hasTail() const { return hasSilhouette() && !tails.items.isEmpty(); }

    [[nodiscard]] bool operator==(const TextArtifact& o) const;
    [[nodiscard]] bool operator!=(const TextArtifact& o) const { return !(*this == o); }
};

//! Authoring records for one project's overlays, keyed by `StripOverlay::uid`.
using ArtifactMap = QHash<QString, TextArtifact>;

/**
 * @brief The persisted name of \p s, and the way back.
 *
 * One mapping, used by both persistence paths — the undo snapshot's JSON and the SVG's `pm:shape`. It
 * used to be a positional array in one and a switch in the other, which meant appending a shape was
 * a silent out-of-bounds read on one side and a compiler error on neither.
 */
[[nodiscard]] const char* shapeName(TextArtifact::Shape s);

/**
 * @brief What to call \p s on screen, translated — as opposed to shapeName(), which is what it is
 *        called in a file.
 *
 * The two are deliberately separate: a persisted name may never change, and a displayed one must be
 * free to. It lives here rather than in the shape picker because the picker is no longer the only
 * thing that names a shape — *Convert to ▸* does too, and two lists would drift.
 */
[[nodiscard]] QString shapeTitle(TextArtifact::Shape s);

/**
 * @brief Every **silhouette**, in the order the pickers offer them. Not the enum's order, which is
 *        append-only and therefore historical.
 *
 * `Shape::None` is deliberately **not** in it. It is not a silhouette to choose between; it is the
 * absence of one, which is a different *kind* of object — no fill, no outline, no line style, nothing
 * for a tail to leave from. Choosing it belongs to *Convert to ▸*, and a picker that offered it would
 * let a property control change what the object is.
 */
[[nodiscard]] const QList<TextArtifact::Shape>& shapeOrder();
//! Parses \p name; anything unrecognised falls back to Speech, so an unknown shape still draws.
[[nodiscard]] TextArtifact::Shape shapeFromName(QStringView name);

//! The persisted name of \p s, and the way back — same contract as shapeName().
[[nodiscard]] const char* styleName(TextArtifact::Style s);
[[nodiscard]] TextArtifact::Style styleFromName(QStringView name);

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

[[nodiscard]] QJsonObject artifactToJson(const TextArtifact& a);
[[nodiscard]] TextArtifact artifactFromJson(const QJsonObject& j);

//! A whole map, keyed by overlay uid — the shape the undo snapshot stores.
[[nodiscard]] QJsonObject artifactsToJsonObject(const ArtifactMap& m);
[[nodiscard]] ArtifactMap artifactsFromJsonObject(const QJsonObject& j);

/**
 * @brief Every open project's authoring records, keyed by project uid.
 *
 * A **cache**, not a store: the records live in the overlays' own SVG files (see artifactsvg.h), and
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

#endif // TEXTARTIFACT_H
