/**
 * \file objectparts.hpp
 * \brief How a test reaches the parts of an object: its shape, box, words, tails, look, seed, picture and
 *        preset origin.
 *
 * The tests read and write an object's parts through these and never through where the object keeps them,
 * so a change to how an object is stored is a change here, not in every test. Each returns a reference to
 * the part, so `styleOf(a).line = …` writes the object and `shapeOf(a).kind` reads it.
 */

#ifndef TEST_OBJECTPARTS_HPP
#define TEST_OBJECTPARTS_HPP

#include "objectrecord.hpp"

inline ShapeProperties&         shapeOf(ObjectRecord& a) { return a.shape; }
inline const ShapeProperties&   shapeOf(const ObjectRecord& a) { return a.shape; }
inline QSize&                   boxOf(ObjectRecord& a) { return a.box; }
inline const QSize&             boxOf(const ObjectRecord& a) { return a.box; }
inline TextProperties&          textOf(ObjectRecord& a) { return a.text; }
inline const TextProperties&    textOf(const ObjectRecord& a) { return a.text; }
inline TailsProperties&         tailsOf(ObjectRecord& a) { return a.tails; }
inline const TailsProperties&   tailsOf(const ObjectRecord& a) { return a.tails; }
inline StyleProperties&         styleOf(ObjectRecord& a) { return a.style; }
inline const StyleProperties&   styleOf(const ObjectRecord& a) { return a.style; }
inline SkinProperties&          skinOf(ObjectRecord& a) { return a.skin; }
inline const SkinProperties&    skinOf(const ObjectRecord& a) { return a.skin; }
inline TextureProperties&       textureOf(ObjectRecord& a) { return a.texture; }
inline const TextureProperties& textureOf(const ObjectRecord& a) { return a.texture; }
inline EffectProperties&        effectsOf(ObjectRecord& a) { return a.effects; }
inline const EffectProperties&  effectsOf(const ObjectRecord& a) { return a.effects; }
inline quint32&                 seedOf(ObjectRecord& a) { return a.styleSeed; }
inline const quint32&           seedOf(const ObjectRecord& a) { return a.styleSeed; }
inline QString&                 artworkOf(ObjectRecord& a) { return a.artwork; }
inline const QString&           artworkOf(const ObjectRecord& a) { return a.artwork; }
inline PresetOrigin&            originOf(ObjectRecord& a) { return a.presetOrigin; }
inline const PresetOrigin&      originOf(const ObjectRecord& a) { return a.presetOrigin; }

#endif // TEST_OBJECTPARTS_HPP
