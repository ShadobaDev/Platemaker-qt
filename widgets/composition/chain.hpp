/**
 * \file chain.hpp
 * \brief A balloon of several bodies, joined by bridges: the rules a chain keeps and the edits that keep them.
 *
 * Bodies are a balloon's bubbles and bridges its joints (BalloonTail::to). The bodies of a balloon form **one
 * chain**: every two are joined through bridges, a body has at most two of them, and no ring is closed. Each
 * edit here either keeps that, or refuses with a ChainError and leaves the composition as it was. What breaks
 * the chain in two (a bridge or a middle body removed) does not refuse: it hands back the pieces.
 */

#ifndef CHAIN_HPP
#define CHAIN_HPP

#include <QList>
#include <QPointF>

#include "composition.hpp"

namespace Chain {

//! The most bridges one body has: a chain reads from one end to the other, so nothing branches.
inline constexpr int k_maxBridgesPerBody = 2;

//! Why an edit was refused; `None` when it was made.
enum class ChainError {
    None,
    NoBalloon,     //!< The composition has no balloon.
    NotABody,      //!< An end is not a body of the balloon.
    NotATail,      //!< The id is no tail of the balloon, or it is a bridge already.
    SameBody,      //!< Both ends are one body.
    NotAChainEnd,  //!< A body has its k_maxBridgesPerBody already.
    ClosesRing,    //!< The two bodies are joined already: a second bridge would close a ring.
};

//! How many bridges @p b has on body @p body.
[[nodiscard]] int bridgesOn(const Balloon& b, int body);

//! Whether @p body is an end of its chain, so a bridge may still be added to it: a lone body is one.
[[nodiscard]] bool chainEnd(const Balloon& b, int body);

//! Whether bodies @p from and @p to are joined, through any number of bridges.
[[nodiscard]] bool joined(const Balloon& b, int from, int to);

/**
 * \brief @p b's bodies in reading order: from the first body that ends its chain along its bridges, then each
 *        other chain the same way (a balloon is one chain; a merge not yet bridged is two).
 */
[[nodiscard]] QList<int> order(const Balloon& b);

//! The bridge between bodies @p a and @p c, its id; 0 for none.
[[nodiscard]] int bridgeBetween(const Balloon& b, int a, int c);

//! Body @p body of @p c moved @p by, with the tails leaving it; its words sit on it, its bridges stretch.
void moveBody(Composition& c, int body, const QPointF& by);

/**
 * \brief Body @p body of @p c given the box @p to: its place and size. The tips of the tails leaving it stay where
 *        they point, their base width following the size as a balloon's do; its words sit on it.
 */
void resizeBody(Composition& c, int body, const QRectF& to);

//! Whether a bridge from @p from to @p to may be added to @p b, and why not.
[[nodiscard]] ChainError canBridge(const Balloon& b, int from, int to);

//! A new bridge from body @p from to body @p to, in the look the balloon gives a new tail.
ChainError addBridge(Composition& c, int from, int to);

//! Tail @p tail made a bridge from its body to body @p to: what the held tail of a join becomes.
ChainError makeBridge(Composition& c, int tail, int to);

/**
 * \brief A new body after @p from, its shape and size, at @p from's place moved @p by, with a bridge to it and
 *        empty words seated in it, in the lettering of @p from's first words: *Add bubble*.
 *
 * Its id in @p added. Refused as a bridge from @p from would be.
 */
ChainError addBody(Composition& c, int from, const QPointF& by, int* added = nullptr);

/**
 * \brief @p other's balloon and words taken into @p c, moved by @p by (from @p other's frame into @p c's).
 *
 * Their ids are renumbered from `c.nextId`, the words kept on their bodies; @p c's style, seed and preset stay.
 * The two balloons are not bridged yet: two pieces until addBridge() or makeBridge() joins them. @p c unchanged
 * unless both have a balloon. The new ids of @p other's bodies, in order, in @p bodies.
 */
ChainError merge(Composition& c, const Composition& other, const QPointF& by, QList<int>* bodies = nullptr);

/**
 * \brief @p c cut into its chains: one composition per set of bodies the bridges join, each with its tails and
 *        the words seated on it. The first holds the first body, and the words with no seat.
 *
 * Each keeps the style, the seed, the preset and `nextId`, so no id is given twice. One piece, @p c as it is,
 * when it is one chain or has no balloon.
 */
[[nodiscard]] QList<Composition> pieces(const Composition& c);

//! @p c without bridge @p bridge, in its pieces(); @p c alone when it has no such bridge.
[[nodiscard]] QList<Composition> withoutBridge(const Composition& c, int bridge);

/**
 * \brief @p c without body @p body, its tails, its bridges and the words seated on it, in its pieces().
 *
 * @p c alone when it has no such body, or when it is the last one: removing that is deleting the object.
 */
[[nodiscard]] QList<Composition> withoutBody(const Composition& c, int body);

}  // namespace Chain

#endif // CHAIN_HPP
