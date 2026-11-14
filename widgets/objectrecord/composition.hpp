#ifndef COMPOSITION_HPP
#define COMPOSITION_HPP

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QtGlobal>

#include <optional>

#include "objectrecord.hpp"     // PresetOrigin, and the record the conversion reads and writes
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
 * Coordinates are strip-scale pixels in the composition's frame, the frame the record's box and tail tips
 * already use (the first body at the origin).
 */

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

//! The background the words sit on: one style for all its parts, and the parts.
struct Balloon
{
    int               id = 0;
    StyleProperties   line;
    SkinProperties    fill;
    TextureProperties texture;
    EffectProperties  effects;
    TailLook          nextLook;   //!< The look a tail added to it takes (TailsProperties::nextLook).
    QList<Body>        bodies;
    QList<BalloonTail> tails;

    [[nodiscard]] bool operator==(const Balloon& o) const
    {
        return id == o.id && line == o.line && fill == o.fill && texture == o.texture && effects == o.effects
            && nextLook == o.nextLook && bodies == o.bodies && tails == o.tails;
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

    [[nodiscard]] bool operator==(const Composition& o) const
    {
        return balloon == o.balloon && picture == o.picture && words == o.words && seed == o.seed
            && preset == o.preset && nextId == o.nextId;
    }
    [[nodiscard]] bool operator!=(const Composition& o) const { return !(*this == o); }
};

/**
 * @name The way between a record and a composition, while both exist. A record that is a balloon is one body;
 *       a text object or an artwork keeps no balloon, so the groups such a record carries unread are not
 *       carried over (they come back as their defaults).
 * @{
 */
[[nodiscard]] Composition compositionOf(const ObjectRecord& a);
[[nodiscard]] ObjectRecord recordOf(const Composition& c);
/** @} */

/**
 * \brief The composition as its content: the JSON the workspace keeps with the overlay and the SVG copies.
 *
 * `{balloon: {line, fill, texture, effects, nextTail, bodies, tails}, picture, words, seed, preset, nextId}`, each
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

#endif // COMPOSITION_HPP
