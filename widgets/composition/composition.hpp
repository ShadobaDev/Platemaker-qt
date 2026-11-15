#ifndef COMPOSITION_HPP
#define COMPOSITION_HPP

#include <QHash>
#include <QJsonObject>
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

//! One tail of a balloon, leaving from the body \c root.
struct BalloonTail
{
    int  id   = 0;
    int  root = 0;   //!< The id of the body it leaves from.
    Tail tail;       //!< Its tip (in the composition's frame), width, bend and look.

    [[nodiscard]] bool operator==(const BalloonTail& o) const
    {
        return id == o.id && root == o.root && tail == o.tail;
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
 * \brief Words and how they are set: the text, its lettering and its paddings.
 *
 * They sit in a body (\c seat, its id), whose size they wrap in, or, with no body (\c seat 0), in their own
 * \c box at \c place: a caption, or the words over a picture.
 */
struct Words
{
    int            id   = 0;
    int            seat = 0;
    Place          place;   //!< Read only with no seat.
    QSize          box;     //!< Read only with no seat.
    TextProperties text;

    [[nodiscard]] bool operator==(const Words& o) const
    {
        return id == o.id && seat == o.seat && place == o.place && box == o.box && text == o.text;
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
    quint32                seed   = 0;   //!< The style seed (topUpStyleSeed()).
    PresetOrigin           preset;
    int                    nextId = 1;

    //! One project's compositions, keyed by `StripOverlay::uid`.
    using Map = QHash<QString, Composition>;

    [[nodiscard]] bool operator==(const Composition& o) const
    {
        return balloon == o.balloon && picture == o.picture && words == o.words && seed == o.seed
            && preset == o.preset && nextId == o.nextId;
    }
    [[nodiscard]] bool operator!=(const Composition& o) const { return !(*this == o); }
};

//! The body @p w sits in, or none: words with no seat, or a seat the balloon does not have.
[[nodiscard]] const Body* seatOf(const Composition& c, const Words& w);

/**
 * \brief The words @p c draws and the editor edits: its first. ponytail: one until the chain, whose second words
 *        need render ids of their own (renderlist.cpp's are one set per file), and an editor that picks them.
 */
[[nodiscard]] const Words* wordsOf(const Composition& c);
[[nodiscard]] Words*       wordsOf(Composition& c);

//! The body the editor edits: the first of @p c's balloon; none without a balloon. ponytail: as wordsOf().
[[nodiscard]] const Body* bodyOf(const Composition& c);
[[nodiscard]] Body*       bodyOf(Composition& c);

/**
 * \brief The rectangle the author dragged: the bodies', the picture's, and the box of words with no seat, as
 *        one. Not what is drawn, which a tail may take past it (Geometry::bounds()).
 */
[[nodiscard]] QRectF frameOf(const Composition& c);

/**
 * \brief The size the grips drag: the first body's, or the picture's, or the first words' own box.
 */
[[nodiscard]] QSize sizeOf(const Composition& c);
//! Sets sizeOf(): the first body's size; or the picture's and its words' box; or the words' box.
void resize(Composition& c, const QSize& size);

//! The size an object has before it is drawn on the strip: what a preset and the tool options show.
inline constexpr QSize k_defaultObjectSize{280, 160};

//! Words and nothing else: a text object, its words empty, in a box of @p size at the origin.
[[nodiscard]] Composition textObject(const QSize& size = k_defaultObjectSize);

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
 *        texture, or with an irregular edge on its first body — so it needs a seed, and *Re-roll* has something to
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
 * `{balloon: {id, style, bodies, tails}, picture, words, seed, preset, nextId}`, each
 * group an object and each section an object inside its group (a section's numbers left out while they are their
 * defaults). A body is `{id, place, size, shape}`, a tail `{id, root, tip, width, bend, look}`, words
 * `{id, text, seat}` or, with no seat, `{id, text, place, box}`; a place is `{pos}`, with `rotation` and `scale`
 * only when they are not their defaults.
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
