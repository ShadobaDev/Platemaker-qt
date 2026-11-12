/**
 * \file outline.hpp
 * \brief The outline a line is drawn round, described once: its rings walked by arc length, their corners,
 *        the ends the tails report, the parts it is the union of and what covers each.
 *
 * A line takes this model, not the record (styles.hpp): what is a corner, where a tail ends and how, what lies
 * under another part, is decided here and read by every line alike. What an element knows about itself it
 * reports (a tail: its Round or Cut end); what is a fact of the path alone (a corner) is measured here, once.
 * Built by Geometry::outlineModel() (geometry.hpp).
 */

#ifndef OUTLINE_HPP
#define OUTLINE_HPP

#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QtGlobal>

#include <vector>

#include "edgewalk.hpp"

namespace Geometry {

//! A place where a ring turns sharply: where along it, how far it turns, and the edge's way either side.
struct OutlineCorner
{
    qreal   at   = 0.0;
    qreal   turn = 0.0;   //!< Radians, signed as the walk goes.
    QPointF in;           //!< The way the edge runs into it (a unit vector), read a little before it.
    QPointF out;          //!< The way it runs on, read a little after it.
};

/**
 * \brief A tail's end that is not a point, as the tail reports it: the stretch of the ring from one of its
 *        corners round to the other, `span` long the way the walk goes from `from`.
 */
struct OutlineEnd
{
    qreal from   = 0.0;
    qreal span   = 0.0;
    qreal radius = 0.0;    //!< A round end's radius; for a cut one, half its width.
    bool  round  = false;  //!< A half circle (no corner on it), else a straight cut between two corners.
};

//! The tail's end where it is drawn, in box coordinates: its two corners and its far point.
struct EndPoints
{
    QPointF    from, tip, to;
    OutlineEnd end;          //!< Its radius and kind; where it lies on a ring is filled in by placing it.
};

//! One closed edge of the outline.
struct OutlineRing
{
    EdgeWalk                   walk;
    bool                       clockwise = false;   //!< On screen (y down): a positive shoelace.
    std::vector<OutlineCorner> corners;             //!< In walk order; none inside a round end.
    std::vector<OutlineEnd>    ends;
    /**
     * Whether the edge is under another part, every `k_coverStep` along it; empty on the joined outline,
     * which nothing covers.
     */
    std::vector<char>          covered;

    explicit OutlineRing(const QPolygonF& polygon);

    //! @p at along the ring is under another part (coveredAt() of the joined outline is always false).
    [[nodiscard]] bool coveredAt(qreal at) const;
    //! @p at, any distance along the ring, brought into [0, length).
    [[nodiscard]] qreal wrap(qreal at) const;
    //! @p at lies on @p end.
    [[nodiscard]] bool within(const OutlineEnd& end, qreal at) const;
};

//! One part the outline is the union of: the balloon, or one tail, with its own edge whole.
struct OutlinePart
{
    QPainterPath       path;
    QList<OutlineRing> rings;
};

struct OutlineModel
{
    QPainterPath       joined;   //!< The silhouette: the parts as one path.
    QList<OutlineRing> rings;    //!< Its closed edges, the empty ones left out.
    /** The balloon first, then each tail drawn, each walked round its own edge; for a line that follows them. */
    QList<OutlinePart> parts;
    QRectF             frame;    //!< The record's box at the origin: what radiating strokes are laid from.
    /** How far the shape stretches a radiating line's core along its long axis on Auto, in per cent. */
    int                coreStretch = 0;
};

/**
 * \brief The rings of @p path, the empty ones left out; each part of @p parts but @p own marks where it
 *        covers them (none for an @p own of -1), and each of @p ends lying on one of them is placed on it.
 */
[[nodiscard]] QList<OutlineRing> ringsOf(const QPainterPath& path, const QList<QPainterPath>& parts, qsizetype own,
                                         const QList<EndPoints>& ends);

}  // namespace Geometry

#endif // OUTLINE_HPP
