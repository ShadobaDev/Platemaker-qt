// The outline model's rings: their corners measured, what covers them, and the tails' ends placed on them.

#include "outline.hpp"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace Geometry {

namespace {

//! The turn at a vertex, in degrees, from which it is a corner: a line does not follow it round.
constexpr qreal k_cornerTurn  = 35.0;
//! How far either side of a vertex its turn is measured, in balloon pixels: a flattened curve's own
//! small steps are not corners, one corner split by a sliver of an edge still is.
constexpr qreal k_cornerProbe = 1.5;
//! How often along a part's edge it is asked whether another part covers it, in balloon pixels.
constexpr qreal k_coverStep   = 2.0;
/**
 *  How far a point a tail reports may lie off the ring it is on, in balloon pixels: the union of the parts
 *  flattens their curves, moving the edge by about half a pixel.
 */
constexpr qreal k_featureSlack = 1.0;

//! The corners of @p edge, one per turn: of vertices closer than the probe, the sharpest.
std::vector<OutlineCorner> cornersOf(const EdgeWalk& edge)
{
    const qreal length = edge.length();
    const auto  wrap   = [length](qreal at) { return at - std::floor(at / length) * length; };
    std::vector<OutlineCorner> out;
    const std::vector<qreal>& at = edge.corners();
    for (size_t i = 0; i + 1 < at.size(); ++i) {   // the last is the first again
        const QPointF t1   = edge.at(wrap(at[i] - k_cornerProbe)).tangent;
        const QPointF t2   = edge.at(wrap(at[i] + k_cornerProbe)).tangent;
        const qreal   turn = std::atan2(t1.x() * t2.y() - t1.y() * t2.x(), QPointF::dotProduct(t1, t2));
        if (std::abs(turn) < qDegreesToRadians(k_cornerTurn))
            continue;
        if (!out.empty() && at[i] - out.back().at < 2.0 * k_cornerProbe) {
            if (std::abs(turn) > std::abs(out.back().turn))
                out.back() = {at[i], turn, t1, t2};
            continue;
        }
        out.push_back({at[i], turn, t1, t2});
    }
    if (out.size() > 1 && out.front().at + length - out.back().at < 2.0 * k_cornerProbe) {
        if (std::abs(out.back().turn) > std::abs(out.front().turn))
            out.front() = out.back();
        out.pop_back();
    }
    return out;
}

/**
 * \brief Places @p e on @p ring if its three points lie on it: from whichever corner the walk reaches first,
 *        round through the tip to the other. A round end takes the corners measured on it away.
 */
bool place(OutlineRing& ring, const EndPoints& e)
{
    const EdgeWalk::Place from = ring.walk.locate(e.from);
    const EdgeWalk::Place tip  = ring.walk.locate(e.tip);
    const EdgeWalk::Place to   = ring.walk.locate(e.to);
    if (qMax(from.off, qMax(tip.off, to.off)) > k_featureSlack)
        return false;
    OutlineEnd end = e.end;
    if (ring.wrap(tip.at - from.at) <= ring.wrap(to.at - from.at)) {
        end.from = from.at;
        end.span = ring.wrap(to.at - from.at);
    } else {
        end.from = to.at;
        end.span = ring.wrap(from.at - to.at);
    }
    ring.ends.push_back(end);
    if (end.round) {
        OutlineEnd near = end;   // a corner where the half circle meets the sides is the flattening's too
        near.from -= k_featureSlack;
        near.span += 2.0 * k_featureSlack;
        ring.corners.erase(std::remove_if(ring.corners.begin(), ring.corners.end(),
                                          [&](const OutlineCorner& c) { return ring.within(near, c.at); }),
                           ring.corners.end());
    }
    return true;
}

}  // namespace

OutlineRing::OutlineRing(const QPolygonF& polygon)
    : walk(polygon)
    , clockwise(shoelace(polygon) > 0.0)
    , corners(cornersOf(walk))
{
}

qreal OutlineRing::wrap(qreal at) const
{
    const qreal length = walk.length();
    return at - std::floor(at / length) * length;
}

bool OutlineRing::within(const OutlineEnd& end, qreal at) const
{
    return wrap(at - end.from) <= end.span;
}

bool OutlineRing::coveredAt(qreal at) const
{
    if (covered.empty())
        return false;
    const qsizetype marks = qsizetype(covered.size());
    return covered[size_t(qRound(wrap(at) / walk.length() * marks) % marks)] != 0;
}

QList<OutlineRing> ringsOf(const QPainterPath& path, const QList<QPainterPath>& parts, qsizetype own,
                           const QList<EndPoints>& ends)
{
    QList<OutlineRing> out;
    for (const QPolygonF& polygon : path.toSubpathPolygons()) {
        OutlineRing ring(polygon);
        const qreal length = ring.walk.length();
        if (length <= 0.0)
            continue;
        if (own >= 0) {
            const int marks = qMax(1, qCeil(length / k_coverStep));
            ring.covered.assign(size_t(marks), 0);
            for (int k = 0; k < marks; ++k) {
                const QPointF p = ring.walk.at(length * k / marks).at;
                for (qsizetype other = 0; other < parts.size() && !ring.covered[size_t(k)]; ++other)
                    ring.covered[size_t(k)] = other != own && parts[other].contains(p);
            }
        }
        out.append(std::move(ring));
    }
    for (const EndPoints& e : ends)
        for (OutlineRing& ring : out)
            if (place(ring, e))
                break;
    return out;
}

}  // namespace Geometry
