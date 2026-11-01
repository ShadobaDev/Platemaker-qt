/**
 * \file edgewalk.hpp
 * \brief A closed edge walked by arc length: where a point that far along it is, and which way it runs.
 *
 * The walk every style that follows the outline shares (PLAN-L M10): the flash spreads its strokes along
 * it, a brush lays its ribbon on it. Distances along a polygon, not angles, so a wide oval is not crowded
 * at its ends.
 */

#ifndef EDGEWALK_HPP
#define EDGEWALK_HPP

#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>

#include <vector>

namespace Geometry {

class EdgeWalk
{
public:
    //! A place on the edge, and the edge's direction there (a unit vector, the way the edge runs).
    struct Point
    {
        QPointF at;
        QPointF tangent;
    };

    //! Walks @p edge, closed back to its first point if it is not already.
    explicit EdgeWalk(QPolygonF edge);

    //! The edge's perimeter; 0 for one too short to walk (fewer than two points, or all on one spot).
    [[nodiscard]] qreal length() const { return m_at.empty() ? 0.0 : m_at.back(); }

    //! The point @p s along the edge, for @p s in [0, length()].
    [[nodiscard]] Point at(qreal s) const;

private:
    QPolygonF          m_edge;
    std::vector<qreal> m_at;   //!< How far along the edge each of its points is.
};

//! One walk for every closed edge of @p path (QPainterPath::toSubpathPolygons()), the empty ones left out.
[[nodiscard]] QList<EdgeWalk> edgeWalks(const QPainterPath& path);

}  // namespace Geometry

#endif // EDGEWALK_HPP
