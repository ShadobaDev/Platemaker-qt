#ifndef COMPOSITION_HPP
#define COMPOSITION_HPP

#include <QHash>
#include <QJsonObject>
#include <QLineF>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QtGlobal>

#include <optional>

#include "propertygroups.hpp"   // the groups the elements are made of

/**
 * @file
 * \brief What one object on the strip is made of: a background (a balloon or a picture, or none) and the words
 *        set over it.
 *
 * A **typed tree**, two levels deep: the composition holds its elements, and a balloon holds its bodies and
 * tails. What an object is, is which elements it has: a balloon is `{balloon, words}`, a text object
 * `{words}`, an artwork `{picture, words}`. Each element carries all its own properties; nothing points at a
 * shared look. The balloon owns its line, fill, texture and shadow, so every body and tail of it, unioned
 * and stroked once, wears one style by structure.
 *
 * Coordinates are strip-scale pixels in the composition's frame; what is placed by hand today starts at its
 * origin.
 */

/**
 * \brief The preset last applied to an object: where its look came from, as a hint. Nothing follows the
 *        preset once it is copied in — an edit to the object, or to the preset, leaves this as it was.
 */
struct PresetOrigin
{
    QString uid;    //!< The preset's (BubblePreset::uid); empty for none.
    QString name;   //!< Its name when applied: what is shown when the preset is not among the artist's.

    [[nodiscard]] bool isEmpty() const { return uid.isEmpty(); }
    [[nodiscard]] bool operator==(const PresetOrigin& o) const { return uid == o.uid && name == o.name; }
};

//! Where an element sits in its composition's frame. Only the position is edited; rotation and scale are kept.
struct Place
{
    QPointF pos;
    qreal   rotation = 0.0;   //!< Degrees, clockwise.
    qreal   scale    = 1.0;

    [[nodiscard]] bool operator==(const Place& o) const
    {
        return pos == o.pos && qFuzzyCompare(1.0 + rotation, 1.0 + o.rotation) && qFuzzyCompare(scale, o.scale);
    }
    [[nodiscard]] bool operator!=(const Place& o) const { return !(*this == o); }
};

//! One body of a balloon: its place, its size (what the words in it wrap in) and its own shape.
struct Body
{
    int             id = 0;
    Place           place;
    QSize           size;
    ShapeProperties shape;

    [[nodiscard]] bool operator==(const Body& o) const
    {
        return id == o.id && place == o.place && size == o.size && shape == o.shape;
    }
    [[nodiscard]] bool operator!=(const Body& o) const { return !(*this == o); }
};

/**
 * \brief One tail of a balloon, leaving from the body \c root: to its tip, or, as a **bridge**, to the body \c to.
 *
 * A bridge joins two bodies of one balloon into a chain (chain.hpp); it keeps a tail's width, bend and breaks,
 * and its tip is not read: it runs from its tail's anchor in \c root to \c toAnchor in \c to.
 */
struct BalloonTail
{
    int     id       = 0;
    int     root     = 0;              //!< The id of the body it leaves from.
    Tail    tail;                      //!< Its tip (in the composition's frame), anchor, width, bend and look.
    int     to       = 0;              //!< A bridge's far body; 0 for a tail.
    QPointF toAnchor = Tail::k_centre;  //!< A bridge's anchor in its far body (Tail::anchor); not read on a tail.

    [[nodiscard]] bool isBridge() const { return to != 0; }

    [[nodiscard]] bool operator==(const BalloonTail& o) const
    {
        return id == o.id && root == o.root && tail == o.tail && to == o.to && toAnchor == o.toAnchor;
    }
    [[nodiscard]] bool operator!=(const BalloonTail& o) const { return !(*this == o); }
};

//! How a balloon is drawn, whatever its shape: one value, so a preset carries it whole.
struct BalloonStyle
{
    StyleProperties   line;
    SkinProperties    fill;
    TextureProperties texture;
    EffectProperties  effects;
    TailLook          nextLook;   //!< The look a tail added to it takes (TailsProperties::nextLook).

    [[nodiscard]] bool operator==(const BalloonStyle& o) const
    {
        return line == o.line && fill == o.fill && texture == o.texture && effects == o.effects
            && nextLook == o.nextLook;
    }
    [[nodiscard]] bool operator!=(const BalloonStyle& o) const { return !(*this == o); }
};

//! A balloon as a preset or the tool options carry it: the shape its bodies take and the style it is drawn in.
struct BalloonLook
{
    ShapeProperties shape;
    BalloonStyle    style;

    [[nodiscard]] bool operator==(const BalloonLook& o) const { return shape == o.shape && style == o.style; }
};

//! The background the words sit on: one style for all its parts, and the parts.
struct Balloon
{
    int                id = 0;
    BalloonStyle       style;
    QList<Body>        bodies;
    QList<BalloonTail> tails;

    [[nodiscard]] bool operator==(const Balloon& o) const
    {
        return id == o.id && style == o.style && bodies == o.bodies && tails == o.tails;
    }
    [[nodiscard]] bool operator!=(const Balloon& o) const { return !(*this == o); }
};

/**
 * \brief Where words sit on their seat: four edges, each **set** — its distance in from the seat's inner box (a
 *        body's box less the stroke and the edge's teeth, a picture's whole box), in its pixels, negative past
 *        it — or **automatic** (none): that box's own edge. The frame keeps its distances from the seat's edges
 *        when the seat changes size.
 */
struct Frame
{
    std::optional<qreal> left, top, right, bottom;

    [[nodiscard]] bool operator==(const Frame& o) const
    {
        const auto same = [](const std::optional<qreal>& a, const std::optional<qreal>& b) {
            return a.has_value() == b.has_value() && (!a || qFuzzyCompare(1.0 + *a, 1.0 + *b));
        };
        return same(left, o.left) && same(top, o.top) && same(right, o.right) && same(bottom, o.bottom);
    }
    [[nodiscard]] bool operator!=(const Frame& o) const { return !(*this == o); }
};

/**
 * \brief Words and how they are set: the text, its lettering, and where they sit.
 *
 * They sit on a seat (\c seat, its id: a body, or the picture), in their \c frame on it; or, with no seat
 * (\c seat 0), in their own \c box at \c place: a caption.
 */
struct Words
{
    int            id   = 0;
    int            seat = 0;
    Place          place;   //!< Read only with no seat.
    QSize          box;     //!< Read only with no seat.
    TextProperties text;
    Frame          frame;   //!< Read only with a seat.

    [[nodiscard]] bool operator==(const Words& o) const
    {
        return id == o.id && seat == o.seat && place == o.place && box == o.box && text == o.text
            && frame == o.frame;
    }
    [[nodiscard]] bool operator!=(const Words& o) const { return !(*this == o); }
};

//! An imported picture: a file name in the workspace's `overlays/`, drawn at \c size.
struct Picture
{
    int     id = 0;
    QString file;
    Place   place;
    QSize   size;

    [[nodiscard]] bool operator==(const Picture& o) const
    {
        return id == o.id && file == o.file && place == o.place && size == o.size;
    }
    [[nodiscard]] bool operator!=(const Picture& o) const { return !(*this == o); }
};

/**
 * \brief A cut out of the object's own drawing: where it hides what the object draws, as a layer mask does.
 *
 * Its \c region is a body's geometry (a place, a size and a shape, outlined by Geometry::outlineOf()), in the
 * composition's frame, so it moves with the object; resize() scales it with the object's box. Inside the region
 * the object is hidden; a \c feather softens the region's edge, and a \c fade lets it hide less and less along a
 * line: wholly at the line's start, not at all past its end.
 */
struct Mask
{
    Body  region;                //!< Where it hides: its id is the mask's, as an element of the composition.
    qreal feather = 0.0;         //!< How far the edge is blurred (a Gaussian's deviation), in its pixels; 0 is hard.
    /**
     * The fade, as fractions of the region's box: hidden wholly at its start, not at all at its end, a gradient
     * between. None, the default, hides the region evenly.
     */
    std::optional<QLineF> fade;

    [[nodiscard]] bool operator==(const Mask& o) const
    {
        return region == o.region && qFuzzyCompare(1.0 + feather, 1.0 + o.feather) && fade == o.fade;
    }
    [[nodiscard]] bool operator!=(const Mask& o) const { return !(*this == o); }
};

/**
 * \brief One object on the strip: one overlay, one SVG.
 *
 * At most one background: a balloon or a picture. Element ids are unique across the whole composition and
 * never reused (\c nextId), so an element is addressed by its id wherever it sits.
 */
struct Composition
{
    std::optional<Balloon> balloon;
    std::optional<Picture> picture;
    QList<Words>           words;
    std::optional<Mask>    mask;          //!< What of its drawing it hides; none by default.
    quint32                seed   = 0;   //!< The style seed (topUpStyleSeed()).
    PresetOrigin           preset;
    int                    nextId = 1;

    //! One project's compositions, keyed by `StripOverlay::uid`.
    using Map = QHash<QString, Composition>;

    [[nodiscard]] bool operator==(const Composition& o) const
    {
        return balloon == o.balloon && picture == o.picture && words == o.words && mask == o.mask
            && seed == o.seed && preset == o.preset && nextId == o.nextId;
    }
    [[nodiscard]] bool operator!=(const Composition& o) const { return !(*this == o); }
};

//! The body @p w sits in, or none: words with no seat, on the picture, or on a seat the balloon does not have.
[[nodiscard]] const Body* seatOf(const Composition& c, const Words& w);

//! Whether @p w sit on a seat @p c has: a body (seatOf()) or the picture. Otherwise they keep their own box.
[[nodiscard]] bool seated(const Composition& c, const Words& w);

/**
 * \brief @p t's runs made tidy: in order, none overlapping (the earlier keeps the letters), each within the body,
 *        none overriding a field with the words' own value, none overriding nothing, two neighbours alike merged;
 *        one left over the whole text folded into the words' own lettering.
 */
void tidyRuns(TextProperties& t);

//! Whether any words of @p c has text: what makes a picture a file of ours, and offers the words' handles.
[[nodiscard]] bool lettered(const Composition& c);

/**
 * \brief The words the editor edits: @p c's first. ponytail: one until the editor picks one of several words.
 */
[[nodiscard]] const Words* wordsOf(const Composition& c);
[[nodiscard]] Words*       wordsOf(Composition& c);

//! The body the editor edits: the first of @p c's balloon; none without a balloon. ponytail: as wordsOf().
[[nodiscard]] const Body* bodyOf(const Composition& c);
[[nodiscard]] Body*       bodyOf(Composition& c);
//! @p c's body @p id, its first for 0; none without a balloon or with no such body.
[[nodiscard]] const Body* bodyOf(const Composition& c, int id);
[[nodiscard]] Body*       bodyOf(Composition& c, int id);

/**
 * \brief The rectangle the author dragged: the bodies', the picture's, and the box of words with no seat, as
 *        one. Not what is drawn, which a tail may take past it (Geometry::bounds()).
 */
[[nodiscard]] QRectF frameOf(const Composition& c);

/**
 * \brief The size the grips drag: the first body's, or the picture's, or the first words' own box.
 */
[[nodiscard]] QSize sizeOf(const Composition& c);
//! Sets sizeOf(): the first body's size; or the picture's; and the box of the words with no seat. A mask's
//! region is scaled with it, from its top-left corner, so it keeps its place on the object.
void resize(Composition& c, const QSize& size);

//! The size an object has before it is drawn on the strip: what a preset and the tool options show.
inline constexpr QSize k_defaultObjectSize{280, 160};

//! Words and nothing else: a text object, its words empty, in a box of @p size at the origin.
[[nodiscard]] Composition textObject(const QSize& size = k_defaultObjectSize);

//! A picture of @p file at @p size (its own pixels), with empty words seated on it.
[[nodiscard]] Composition pictureObject(const QString& file, const QSize& size);

/**
 * \brief @p c given a balloon of @p look: one body of its shape where its first words' own box was, of its size,
 *        the words seated in it, and no tail. A composition with a background already comes back as it is.
 */
[[nodiscard]] Composition withBalloon(Composition c, const BalloonLook& look);

/**
 * \brief @p c with no balloon: the words that sat in a body in their own box, where that body was and of its
 *        size. The balloon's style and tails go with it (*Convert to text*).
 */
[[nodiscard]] Composition withoutBalloon(Composition c);

/**
 * \brief Whether @p c draws anything at random from its seed: a balloon whose line is not the solid one, with a
 *        texture, or with an irregular edge on any body — so it needs a seed, and *Re-roll* has something to
 *        deal again.
 */
[[nodiscard]] bool usesSeed(const Composition& c);

/**
 * \brief Gives @p c a seed if it uses one (usesSeed()) and has none — **the one place that mints one**.
 *
 * The seed belongs to no property group, precisely so that no editor and no preset can copy it: a page of
 * marker balloons all wearing the same wobble is the failure it exists to prevent. Idempotent: a composition
 * that already has a seed keeps it, so re-styling never re-rolls the wobble.
 */
void topUpStyleSeed(Composition& c);

/**
 *  How much of the lettering names an object in a list. Long enough to tell two lines apart, short
 *  enough to leave room for what else a row carries.
 */
inline constexpr int k_labelChars = 32;

/**
 * \brief What to call @p c in the object stack: the first line of its words, or, with none, what it is in
 *        parentheses (its balloon's shape, or text).
 */
[[nodiscard]] QString objectLabel(const Composition& c);

/**
 * \brief The composition as its content: the JSON the workspace keeps with the overlay and the SVG copies.
 *
 * `{balloon: {id, style, bodies, tails}, picture, words, mask, seed, preset, nextId}`, each
 * group an object and each section an object inside its group (a section's numbers left out while they are their
 * defaults). A body is `{id, place, size, shape}`, a tail `{id, root, tip, width, bend, look}` and a bridge `to`
 * too, words
 * `{id, text, seat, frame}` or, with no seat, `{id, text, place, box}`; a place is `{pos}`, with `rotation` and `scale`
 * only when they are not their defaults. A mask is `{id, place, size, shape}`, with `feather` and `fade`
 * (`[x1, y1, x2, y2]`) only when it has them.
 */
[[nodiscard]] QJsonObject toJson(const Composition& c);
/**
 * \brief The composition @p j describes (toJson()); what it leaves out is its default. A balloon with no body is
 *        not read, nor a picture beside a balloon; `nextId` is never below an id in use.
 */
[[nodiscard]] Composition compositionFromJson(const QJsonObject& j);

/**
 * @name The parts a preset is made of, in the content's form (toJson()): a balloon's style `{line, fill, texture,
 *       effects, nextTail}`, a shape, a lettering. What a reader does not find is its default.
 * @{
 */
[[nodiscard]] QJsonObject     toJson(const BalloonStyle& s);
[[nodiscard]] BalloonStyle    balloonStyleFromJson(const QJsonObject& j);
[[nodiscard]] QJsonObject     toJson(const ShapeProperties& s);
[[nodiscard]] ShapeProperties shapeFromJson(const QJsonObject& j);
[[nodiscard]] QJsonObject     toJson(const TextProperties& t);
[[nodiscard]] TextProperties  textFromJson(const QJsonObject& j);
/** @} */

#endif // COMPOSITION_HPP
