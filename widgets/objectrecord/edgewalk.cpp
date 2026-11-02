#include "edgewalk.hpp"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <utility>

namespace Geometry {

EdgeWalk::EdgeWalk(QPolygonF edge)
    : m_edge(std::move(edge))
{
    if (m_edge.size() < 2)
        return;
    if (m_edge.first() != m_edge.last())
        m_edge << m_edge.first();
    m_at.assign(size_t(m_edge.size()), 0.0);
    for (int i = 1; i < m_edge.size(); ++i)
        m_at[size_t(i)] = m_at[size_t(i - 1)] + QLineF(m_edge[i - 1], m_edge[i]).length();
}

EdgeWalk::Point EdgeWalk::at(qreal s) const
{
    if (m_at.size() < 2)
        return {};
    // Which segment, and how far along it.
    const auto   next = std::upper_bound(m_at.begin(), m_at.end(), s);
    const size_t k    = size_t(std::clamp<std::ptrdiff_t>(next - m_at.begin(), 1,
                                                          std::ptrdiff_t(m_at.size()) - 1)) - 1;
    const qreal   seg = m_at[k + 1] - m_at[k];
    const qreal   t   = seg > 0.0 ? (s - m_at[k]) / seg : 0.0;
    const QPointF a   = m_edge[int(k)];
    const QPointF d   = m_edge[int(k) + 1] - a;
    return {a + d * t, seg > 0.0 ? d / seg : QPointF()};
}

qreal smoothNoise(quint32 seed, qreal x, qint64 period)
{
    // The value at a whole number: the seed and the number hashed together (a 32-bit mixer), as a
    // fraction. A hash, not a generator, so any point can be asked for without walking to it.
    const auto lattice = [seed, period](qint64 k) {
        if (period > 0)
            k = ((k % period) + period) % period;
        quint32 h = seed ^ (quint32(k) * 0x9E3779B1u);
        h ^= h >> 16;
        h *= 0x7FEB352Du;
        h ^= h >> 15;
        h *= 0x846CA68Bu;
        h ^= h >> 16;
        return h / qreal(0xFFFFFFFFu) * 2.0 - 1.0;
    };
    const qreal  whole = std::floor(x);
    const qint64 k     = qint64(whole);
    const qreal  t     = x - whole;
    const qreal  ease  = t * t * (3.0 - 2.0 * t);   // smoothstep: no kink where two spans meet
    return lattice(k) + (lattice(k + 1) - lattice(k)) * ease;
}

QList<EdgeWalk> edgeWalks(const QPainterPath& path)
{
    QList<EdgeWalk> out;
    for (const QPolygonF& edge : path.toSubpathPolygons()) {
        EdgeWalk walk(edge);
        if (walk.length() > 0.0)
            out.append(std::move(walk));
    }
    return out;
}

}  // namespace Geometry
