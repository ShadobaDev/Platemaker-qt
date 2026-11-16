/**
 * \file objectparts.hpp
 * \brief How a test makes an object and reaches its parts: its balloon's shape, size, style and tails, its
 *        words, its seed, its picture and its preset origin.
 *
 * The tests read and write an object's parts through these and never through where the composition keeps them,
 * so a change to how an object is stored is a change here, not in every test. Each returns a reference to the
 * part, so `styleOf(a).line = …` writes the object and `shapeOf(a).kind` reads it. A balloon's part is asked of
 * an object with a balloon, the words' of one with words, the picture's of a picture.
 */

#ifndef TEST_OBJECTPARTS_HPP
#define TEST_OBJECTPARTS_HPP

#include "composition.hpp"

//! A balloon of the default look and size, its words empty: what an object is with no preset picked.
inline Composition balloonObject() { return withBalloon(textObject(), {}); }

//! A picture of @p file at the default object size, its words empty (pictureObject()).
inline Composition pictureObject(const QString& file) { return pictureObject(file, k_defaultObjectSize); }

inline ShapeProperties&         shapeOf(Composition& c) { return bodyOf(c)->shape; }
inline const ShapeProperties&   shapeOf(const Composition& c) { return bodyOf(c)->shape; }
//! The size the grips drag: the balloon's body, or the words' own box.
inline QSize&                   boxOf(Composition& c) { return bodyOf(c) ? bodyOf(c)->size : wordsOf(c)->box; }
inline const QSize&             boxOf(const Composition& c) { return bodyOf(c) ? bodyOf(c)->size : wordsOf(c)->box; }
inline TextProperties&          textOf(Composition& c) { return wordsOf(c)->text; }
inline const TextProperties&    textOf(const Composition& c) { return wordsOf(c)->text; }
inline QList<BalloonTail>&       tailsOf(Composition& c) { return c.balloon->tails; }
inline const QList<BalloonTail>& tailsOf(const Composition& c) { return c.balloon->tails; }
inline TailLook&                nextTailOf(Composition& c) { return c.balloon->style.nextLook; }
inline const TailLook&          nextTailOf(const Composition& c) { return c.balloon->style.nextLook; }
inline StyleProperties&         styleOf(Composition& c) { return c.balloon->style.line; }
inline const StyleProperties&   styleOf(const Composition& c) { return c.balloon->style.line; }
inline SkinProperties&          skinOf(Composition& c) { return c.balloon->style.fill; }
inline const SkinProperties&    skinOf(const Composition& c) { return c.balloon->style.fill; }
inline TextureProperties&       textureOf(Composition& c) { return c.balloon->style.texture; }
inline const TextureProperties& textureOf(const Composition& c) { return c.balloon->style.texture; }
inline EffectProperties&        effectsOf(Composition& c) { return c.balloon->style.effects; }
inline const EffectProperties&  effectsOf(const Composition& c) { return c.balloon->style.effects; }
inline quint32&                 seedOf(Composition& c) { return c.seed; }
inline const quint32&           seedOf(const Composition& c) { return c.seed; }
inline QString&                 pictureFileOf(Composition& c) { return c.picture->file; }
inline const QString&           pictureFileOf(const Composition& c) { return c.picture->file; }
inline PresetOrigin&            originOf(Composition& c) { return c.preset; }
inline const PresetOrigin&      originOf(const Composition& c) { return c.preset; }

//! Gives @p c's balloon @p tails, through the tails' own group: a tail kept by its place, a new one from the body.
inline void setTails(Composition& c, const QList<Tail>& tails)
{
    TailsProperties p = TailsProperties::from(c);
    p.items           = tails;
    p.applyTo(c);
}

#endif // TEST_OBJECTPARTS_HPP
