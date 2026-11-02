/**
 * \file edgewalk.hpp
 * \brief A closed edge walked by arc length: where a point that far along it is, and which way it runs;
 *        and the smooth noise that varies what follows it.
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
#include <QtGlobal>

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

    //! How far along the edge each of its points is, from 0 to length() (the closing point, the first again).
    [[nodiscard]] const std::vector<qreal>& corners() const { return m_at; }

private:
    QPolygonF          m_edge;
    std::vector<qreal> m_at;   //!< How far along the edge each of its points is.
};

//! One walk for every closed edge of @p path (QPainterPath::toSubpathPolygons()), the empty ones left out.
[[nodiscard]] QList<EdgeWalk> edgeWalks(const QPainterPath& path);

/**
 * @brief Smooth seeded noise along a line, in [-1, 1]: a random value at every whole @p x, eased from one
 *        to the next. The same @p seed gives the same curve, so whatever follows it stays put while the
 *        balloon is edited.
 *
 * With a @p period (whole numbers) the values repeat every @p period, so noise laid round a loop that
 * many units long closes on itself; 0 never repeats.
 */
[[nodiscard]] qreal smoothNoise(quint32 seed, qreal x, qint64 period = 0);

}  // namespace Geometry

#endif // EDGEWALK_HPP
