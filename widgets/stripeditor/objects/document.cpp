#include "objects/document.hpp"

#include <QPainterPath>
#include <QPolygonF>
#include <QtGlobal>
#include <QtMath>

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <utility>

namespace StripEdit {

void Document::reset(std::vector<Overlay> overlays, Composition::Map compositions)
{
    m_overlays     = std::move(overlays);
    m_compositions = std::move(compositions);
    // A composition with no overlay is no object.
    for (auto it = m_compositions.begin(); it != m_compositions.end();)
        it = overlay(it.key()) ? std::next(it) : m_compositions.erase(it);
}

std::optional<Composition> Document::composition(const QString& uid) const
{
    const auto it = m_compositions.constFind(uid);
    return it == m_compositions.constEnd() ? std::nullopt : std::optional<Composition>(*it);
}

bool Document::setComposition(const QString& uid, const Composition& c)
{
    if (!overlay(uid))
        return false;
    const auto it = m_compositions.find(uid);
    if (it != m_compositions.end() && *it == c)
        return false;
    m_compositions.insert(uid, c);
    return true;
}

const Document::Overlay* Document::overlay(const QString& uid) const
{
    const std::string id = uid.toStdString();
    const auto        it = std::find_if(m_overlays.cbegin(), m_overlays.cend(), [&](const Overlay& o) { return o.uid == id; });
    return it == m_overlays.cend() ? nullptr : &*it;
}

Document::Overlay* Document::find(const QString& uid)
{
    return const_cast<Overlay*>(std::as_const(*this).overlay(uid));
}

bool Document::setPlace(const QString& uid, const StripLayout::Anchored& at)
{
    Overlay* o = find(uid);
    if (!o)
        return false;
    o->xFrac          = at.xFrac;
    o->yFrac          = at.yFrac;
    o->wFrac          = at.wFrac;
    o->anchorInputUid = at.anchor.toStdString();
    return true;
}

bool Document::setAnchor(const QString& uid, const QString& page)
{
    Overlay* o = find(uid);
    if (!o || QString::fromStdString(o->anchorInputUid) == page)
        return false;
    // Only the anchor changes. Placement is measured from the anchor page's top, so the object lands at the
    // offset it had on its old page — for an unanchored one, where it sat on the page it lost, which is the best
    // starting point there is and one drag from wherever it should be.
    o->anchorInputUid = page.toStdString();
    return true;
}

std::optional<Platemaker::Models::BlendMode> Document::blendOf(const QStringList& uids) const
{
    std::optional<Platemaker::Models::BlendMode> shared;
    for (const Overlay& o : m_overlays) {
        if (!uids.contains(QString::fromStdString(o.uid)))
            continue;
        if (!shared)
            shared = o.blend;
        else if (*shared != o.blend)
            return std::nullopt;   // no one answer — which is an answer, and it is *Mixed*
    }
    return shared;
}

QStringList Document::setBlend(const QStringList& uids, Platemaker::Models::BlendMode blend)
{
    QStringList changed;
    for (Overlay& o : m_overlays) {
        const QString uid = QString::fromStdString(o.uid);
        if (!uids.contains(uid) || o.blend == blend)
            continue;
        o.blend = blend;
        changed << uid;
    }
    return changed;
}

bool Document::setEnabled(const QString& uid, bool on)
{
    Overlay* o = find(uid);
    if (!o || o->enabled == on)
        return false;
    o->enabled = on;
    return true;
}

bool Document::move(const QString& uid, bool forward)
{
    const Overlay* o = overlay(uid);
    if (!o)
        return false;
    // The library draws the vector in order, so the **last** element is the front-most. Forward therefore means
    // later in the vector.
    const auto index  = o - m_overlays.data();
    const auto target = index + (forward ? 1 : -1);
    if (target < 0 || target >= static_cast<std::ptrdiff_t>(m_overlays.size()))
        return false;   // already at the end of the stack
    std::swap(m_overlays[size_t(index)], m_overlays[size_t(target)]);
    return true;
}

Document::Reorder Document::setOrder(const QStringList& bottomUp)
{
    std::unordered_map<std::string, Overlay> byUid;
    for (const Overlay& o : m_overlays)
        byUid.emplace(o.uid, o);

    std::vector<Overlay> reordered;
    reordered.reserve(m_overlays.size());
    for (const QString& uid : bottomUp) {
        const auto it = byUid.find(uid.toStdString());
        if (it != byUid.end())
            reordered.push_back(it->second);
    }
    // A row landed where no overlay row belongs — among the strip's pages, say: an order that has lost
    // someone's bubble is not taken.
    if (reordered.size() != m_overlays.size())
        return Reorder::Refused;
    const bool same = std::equal(reordered.cbegin(), reordered.cend(), m_overlays.cbegin(),
                                 [](const Overlay& a, const Overlay& b) { return a.uid == b.uid; });
    if (same)
        return Reorder::Unchanged;
    m_overlays = std::move(reordered);
    return Reorder::Done;
}

void Document::remove(const QStringList& uids)
{
    m_overlays.erase(std::remove_if(m_overlays.begin(), m_overlays.end(),
                                    [&](const Overlay& o) { return uids.contains(QString::fromStdString(o.uid)); }),
                     m_overlays.end());
    for (const QString& uid : uids)
        m_compositions.remove(uid);
}

namespace Edits {

bool reroll(Composition& c, const std::function<quint32()>& random)
{
    if (!usesSeed(c))
        return false;
    // Never the seed it had: a re-roll that changed nothing would look like a broken button.
    const quint32 was = c.seed;
    while (c.seed == was || c.seed == 0)
        c.seed = random();
    return true;
}

bool paint(Composition& c, Geometry::Part part, const QColor& colour, int words)
{
    switch (part) {
    case Geometry::Part::Text: {
        const auto w = std::find_if(c.words.begin(), c.words.end(), [words](const Words& x) { return x.id == words; });
        Words*     target = w != c.words.end() ? &*w : wordsOf(c);
        if (!target)
            return false;
        target->text.fill.colour = colour;
        return true;
    }
    case Geometry::Part::Outline:
        if (c.balloon)
            c.balloon->style.line.stroke.colour = colour;
        return c.balloon.has_value();
    case Geometry::Part::Fill:
        if (c.balloon)
            c.balloon->style.fill.fill.colour = colour;
        return c.balloon.has_value();
    case Geometry::Part::None:
        break;
    }
    return false;
}

bool convert(Composition& c, bool toBalloon, const BalloonLook& look)
{
    if (c.picture || c.balloon.has_value() == toBalloon)
        return false;
    c = toBalloon ? withBalloon(c, look) : withoutBalloon(c);
    // ...and a size that fits the lettering in its new kind. A text object's box is its words' own size,
    // which a balloon's smaller text area could not hold. Fitted rather than only grown, both ways:
    // converting back gives the words' own box again.
    resize(c, Geometry::fittedBox(c));
    return true;
}

bool fitToText(Composition& c, int body)
{
    if (c.picture)
        return false;
    const Composition was = c;
    c = body != 0 ? Geometry::fitBodyToBlocks(c, body, Geometry::textBlocks(c))
                  : Geometry::fitToBlocks(c, Geometry::textBlocks(c));
    return c != was;
}

bool removeTails(Composition& c, QList<int> indexes)
{
    if (!c.balloon)
        return false;
    // Highest first, or removing tail 1 would renumber tail 2 under our feet.
    std::sort(indexes.begin(), indexes.end(), std::greater<int>());
    bool removed = false;
    for (int i : std::as_const(indexes))
        if (i >= 0 && i < c.balloon->tails.size()) {
            c.balloon->tails.removeAt(i);
            removed = true;
        }
    return removed;
}

int addWords(Composition& c, int from, int seat)
{
    const auto model = std::find_if(c.words.cbegin(), c.words.cend(), [from](const Words& w) { return w.id == from; });
    if (c.words.isEmpty())
        return 0;
    Words added = model != c.words.cend() ? *model : c.words.first();
    if (seat != 0)
        added.seat = seat;
    if (!seated(c, added))
        return 0;
    added.id = c.nextId++;
    added.text.body.clear();
    added.frame     = {};
    added.frame.top = qreal(qRound(Geometry::textArea(c, added).height() / 2.0));
    c.words.append(added);
    return added.id;
}

bool removeWords(Composition& c, int id)
{
    if (c.words.size() <= 1)
        return false;
    return c.words.removeIf([id](const Words& w) { return w.id == id; }) > 0;
}

namespace {

//! *Add mask to object*'s region inside the frame, as a fraction of its width.
constexpr qreal k_newMaskWidth = 1.0 / 3.0;
/**
 * How far *Add mask to object*'s region reaches past the frame's top, bottom and right, as a fraction of the frame's
 * shorter side. Its outline shows what it covers if it is later faded by its Edge: on the frame's own edge that would
 * let the object show back there, past it nothing is drawn.
 */
constexpr qreal k_newMaskOverhang = 0.25;

} // namespace

int addMask(Composition& c)
{
    const QRectF frame = frameOf(c);
    if (c.mask || frame.isEmpty())
        return 0;
    const qreal  inside = qRound(frame.width() * k_newMaskWidth);
    const qreal  past   = qRound(qMin(frame.width(), frame.height()) * k_newMaskOverhang);
    const QRectF region(frame.right() - inside, frame.top() - past, inside + past, frame.height() + 2 * past);
    Mask m;
    m.id = c.nextId++;
    m.regions.append(rectRegion(c.nextId++, region));
    // A soft edge following the outline, as deep as its default (Mask::edgeWidth): wherever it is painted to.
    m.shape = Mask::Fade::Edge;
    c.mask = m;
    return m.id;
}

bool removeMask(Composition& c)
{
    const bool had = c.mask.has_value();
    c.mask.reset();
    return had;
}

namespace {

//! @p contours' outer ones: those inside no other (a hole, and whatever lies in it, is inside its outer one).
QList<QPolygonF> outerContours(const QList<QPolygonF>& contours)
{
    QList<QPolygonF> out;
    for (qsizetype i = 0; i < contours.size(); ++i) {
        bool inside = false;
        for (qsizetype j = 0; j < contours.size() && !inside; ++j)
            inside = j != i && !contours[i].isEmpty() && contours[j].containsPoint(contours[i].first(), Qt::OddEvenFill);
        if (!inside)
            out.append(contours[i]);
    }
    return out;
}

//! A region round @p outline: a box of whole pixels round it, and its nodes a Curve fitted within @p tolerance.
std::optional<Body> regionRound(const QPolygonF& outline, qreal tolerance, int id)
{
    const QRectF bounds = outline.boundingRect();
    const QPoint corner(qFloor(bounds.left()), qFloor(bounds.top()));
    const QSize  size(qMax(1, qCeil(bounds.right()) - corner.x()), qMax(1, qCeil(bounds.bottom()) - corner.y()));
    Body r;
    r.id                  = id;
    r.place.pos           = corner;
    r.size                = size;
    r.shape.kind          = ShapeProperties::Kind::Curve;
    r.shape.corner.radius = 0.0;
    r.shape.nodes         = Bezier::fitted(outline, QRectF(corner, QSizeF(size)), tolerance);
    if (r.shape.nodes.isEmpty())
        return std::nullopt;   // a sliver with no area to speak of
    return r;
}

//! @p p, a fraction of @p from, as the fraction of @p to at the same place.
QPointF sameFraction(const QPointF& p, const QRectF& from, const QRectF& to)
{
    const QPointF at = from.topLeft() + QPointF(p.x() * from.width(), p.y() * from.height());
    return {(at.x() - to.left()) / to.width(), (at.y() - to.top()) / to.height()};
}

} // namespace

bool paintMask(Composition& c, const QPainterPath& area, bool hide, qreal tolerance)
{
    if (area.isEmpty() || tolerance <= 0.0 || (!c.mask && !hide))
        return false;
    Mask         m    = c.mask.value_or(Mask{});
    const bool   grow = hide != m.keepInside;   // with Keep inside, hiding takes from the regions
    const QRectF before = Geometry::maskBox(m);

    // The regions it meets, as one area; the others are kept as they are.
    QPainterPath met;
    QList<Body>  regions;
    for (const Body& r : std::as_const(m.regions)) {
        const QPainterPath outline = Geometry::outlineOf(r, QRectF(r.place.pos, QSizeF(r.size)), c.seed);
        if (outline.intersects(area))
            met = met.united(outline);
        else
            regions.append(r);
    }
    // Wholly inside, it adds nothing, or takes only a hole, which is filled.
    if (met.contains(area) || (!grow && met.isEmpty()))
        return false;

    const QPainterPath result = (grow ? met.united(area) : met.subtracted(area)).simplified();
    if (!c.mask)
        m.id = c.nextId++;
    for (const QPolygonF& piece : outerContours(result.toSubpathPolygons())) {
        if (const std::optional<Body> r = regionRound(piece, tolerance, c.nextId)) {
            regions.append(*r);
            ++c.nextId;
        }
    }
    if (regions.isEmpty()) {
        c.mask.reset();
        return true;
    }
    m.regions = regions;

    // The fade where it was on the object, on the box round the regions as they are now.
    const QRectF after = Geometry::maskBox(m);
    if (!before.isEmpty() && m.fade)
        m.fade = QLineF(sameFraction(m.fade->p1(), before, after), sameFraction(m.fade->p2(), before, after));
    m.tone = Geometry::crossMoved(m.tone, before, after);
    c.mask = m;
    return true;
}

}  // namespace Edits

}  // namespace StripEdit
