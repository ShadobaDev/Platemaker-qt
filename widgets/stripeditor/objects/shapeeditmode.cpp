#include "objects/shapeeditmode.hpp"

#include "objects/object.hpp"
#include "objects/selection.hpp"
#include "objects/selectionchrome.hpp"
#include "bezier.hpp"
#include "geometry.hpp"
#include "shapes.hpp"

#include <QGraphicsScene>
#include <QPolygonF>

#include <algorithm>
#include <functional>
#include <utility>

namespace StripEdit {

namespace {

//! The words of @p c whose curve @p item's picked nodes are (a warp, or the dormant straight one); none when they are
//! a body's, or none are picked.
template <class C>
auto warpPicked(C& c, const Object& item) -> decltype(&c.words.first())
{
    for (auto& w : c.words)
        if (w.id == item.pickedElement())
            return &w;
    return nullptr;
}

}  // namespace

ShapeEditMode::ShapeEditMode(const SelectionModel& selection, QGraphicsScene* scene, Host host, QObject* parent)
    : QObject(parent), m_selection(selection), m_scene(scene), m_host(std::move(host))
{
}

bool ShapeEditMode::canEdit() const
{
    const Object* item = m_host.object(m_selection.primary());
    return item && m_selection.objects().size() == 1 && item->canEditShape();
}

void ShapeEditMode::setOn(bool on)
{
    const QString uid = on && canEdit() ? m_selection.primary() : QString();
    if (uid == m_edited)
        return;
    if (Object* was = m_host.object(m_edited))
        was->setShapeEditing(false);
    m_edited = uid;
    if (Object* item = m_host.object(uid))
        item->setShapeEditing(true);
    emit changed();
}

bool ShapeEditMode::hasPickedNode() const
{
    const Object* item = m_host.object(m_edited);
    return item && item->pickedVertex() >= 0;
}

bool ShapeEditMode::onEditedObject(const QPointF& scenePos, const QTransform& deviceTransform) const
{
    const Object* edited = m_host.object(m_edited);
    if (!edited)
        return false;
    if (const SelectionChrome* chrome = m_host.chrome(); chrome && chrome->hitAt(scenePos).object)
        return true;   // the grips and handles drawn are the edited object's own
    return dynamic_cast<Object*>(m_scene->itemAt(scenePos, deviceTransform)) == edited;
}

void ShapeEditMode::leaveAt(const QPointF& scenePos, const QTransform& deviceTransform)
{
    // A double-click on a handle its owner can put back (a frame's edge, the words' anchor) does that instead.
    if (const SelectionChrome* chrome = m_host.chrome()) {
        const SelectionChrome::Hit hit = chrome->hitAt(scenePos);
        if (hit.object && hit.object->uid() == m_edited && hit.grip == Grip::Shape) {
            const QList<Geometry::Handle> handles = hit.object->shapeHandles();
            if (hit.handle >= 0 && hit.handle < handles.size() && handles.at(hit.handle).reset) {
                Composition next = hit.object->composition();
                if (handles.at(hit.handle).reset(next, handles.at(hit.handle)))
                    m_host.commit(m_edited, next, tr("Reset the words' frame"));
                return;
            }
        }
    }
    auto* other = dynamic_cast<Object*>(m_scene->itemAt(scenePos, deviceTransform));
    if (other && other->uid() != m_edited)
        m_host.select(other->uid());   // a selection of something else ends the editing
    else
        setOn(false);
}

void ShapeEditMode::pickNodesIn(const QRectF& sceneRect, bool add)
{
    Object* edited = m_host.object(m_edited);
    if (!edited)
        return;
    // One element's nodes: the first's inside the band (an outline's, or a warp's).
    QList<int> inside;
    int        owner = 0;
    for (const Geometry::Handle& h : edited->shapeHandles())
        if (h.glyph == Geometry::Handle::Glyph::Node && h.node >= 0 && (owner == 0 || h.element == owner)
            && sceneRect.contains(edited->mapToScene(h.at)) && !inside.contains(h.node)) {
            owner = h.element;
            inside.append(h.node);
        }
    if (inside.isEmpty())
        return;   // a band round no node never happened
    QList<int> picked = add && edited->pickedElement() == owner ? edited->pickedVertices() : QList<int>{};
    for (int i : std::as_const(inside))
        if (!picked.contains(i))
            picked.append(i);
    edited->setPickedVertices(picked, owner);
}

void ShapeEditMode::stepPick(bool clockwise, PickStep how)
{
    Object* edited = m_host.object(m_edited);
    if (!edited || edited->pickedVertices().isEmpty())
        return;
    QPolygonF nodes;   // the picked element's, in index order, as drawn
    const int owner = edited->pickedElement();
    for (const Geometry::Handle& h : edited->shapeHandles())
        if (h.glyph == Geometry::Handle::Glyph::Node && h.element == owner && h.node == nodes.size())
            nodes << edited->mapToScene(h.at);
    const int n = int(nodes.size());
    if (n == 0)
        return;
    // Clockwise on screen (y down) is a positive shoelace: then the next index is the clockwise one.
    qreal area = 0.0;
    for (int i = 0; i < n; ++i)
        area += nodes[i].x() * nodes[(i + 1) % n].y() - nodes[(i + 1) % n].x() * nodes[i].y();
    const int  step   = (area > 0.0) == clockwise ? 1 : n - 1;
    QList<int> picked = edited->pickedVertices();
    int        end    = picked.last();
    for (int i = 0; i < n && picked.contains((end + step) % n); ++i)
        end = (end + step) % n;
    if (how == PickStep::Shrink) {
        if (picked.size() < 2)
            return;   // the last one stays, or the arrows would have nowhere to start from
        picked.removeAll(end);
        // The run's new end is the one picked last, so the next Shrink that way takes it in turn.
        const int before = (end + n - step) % n;
        if (picked.removeAll(before) > 0)
            picked.append(before);
        edited->setPickedVertices(picked, owner);
        return;
    }
    const int next = (end + step) % n;
    if (picked.contains(next))
        return;   // every node is picked
    if (how == PickStep::Extend)
        picked.append(next);
    else
        picked = {next};
    edited->setPickedVertices(picked, owner);
}

std::optional<ShapeNode::Type> ShapeEditMode::pickedNodeType() const
{
    const Object* item = m_host.object(m_edited);
    if (!item)
        return std::nullopt;
    if (const Words* w = warpPicked(item->composition(), *item)) {
        const int i = item->pickedVertex();
        if (i < 0 || i >= w->text.warp.size())
            return std::nullopt;
        return w->text.warp.at(i).type;
    }
    const Body* body = bodyOf(item->composition(), item->pickedBody());
    if (!body || body->shape.kind != ShapeProperties::Kind::Curve)
        return std::nullopt;
    const QList<ShapeNode>& nodes = body->shape.nodes;
    const int               i     = item->pickedVertex();
    if (i < 0 || i >= nodes.size())
        return std::nullopt;
    return nodes.at(i).type;
}

void ShapeEditMode::setPickedNodeType(ShapeNode::Type type)
{
    Object* item = m_host.object(m_edited);
    if (!pickedNodeType() || !item)
        return;
    Composition a = item->composition();
    if (Words* w = warpPicked(a, *item)) {
        const QRectF box = Geometry::warpBox(a, *w);
        for (int i : item->pickedVertices())
            w->text.warp = Bezier::withNodeType(w->text.warp, Geometry::k_warpRules, box, i, type);
        if (w->text.warp == warpPicked(item->composition(), *item)->text.warp)
            return;
        m_host.commit(m_edited, a, tr("Point type"));
        emit changed();
        return;
    }
    Body&        body = *bodyOf(a, item->pickedBody());   // pickedNodeType() found a Curve's node
    const QRectF rect = Geometry::balloonRect(*a.balloon, body);
    for (int i : item->pickedVertices())
        body.shape = Geometry::withNodeType(body.shape, rect, i, type);
    if (body.shape == bodyOf(item->composition(), item->pickedBody())->shape)
        return;
    m_host.commit(m_edited, a, tr("Point type"));
    emit changed();   // the menu's tick, and the handles drawn
}

bool ShapeEditMode::canGrowPickedHandles() const
{
    const Object* item = m_host.object(m_edited);
    if (!pickedNodeType() || !item)
        return false;
    if (const Words* w = warpPicked(item->composition(), *item)) {
        for (int i : item->pickedVertices())
            if (Bezier::withHandlesGrown(w->text.warp, Geometry::k_warpRules, i) != w->text.warp)
                return true;
        return false;
    }
    const ShapeProperties& shape = bodyOf(item->composition(), item->pickedBody())->shape;   // pickedNodeType() found a Curve's node
    for (int i : item->pickedVertices())
        if (!(Geometry::withHandlesGrown(shape, i) == shape))
            return true;
    return false;
}

void ShapeEditMode::growPickedHandles()
{
    Object* item = m_host.object(m_edited);
    if (!canGrowPickedHandles() || !item)
        return;
    Composition a = item->composition();
    if (Words* w = warpPicked(a, *item)) {
        for (int i : item->pickedVertices())
            w->text.warp = Bezier::withHandlesGrown(w->text.warp, Geometry::k_warpRules, i);
        m_host.commit(m_edited, a, tr("Add handles"));
        emit changed();
        return;
    }
    Body&       body = *bodyOf(a, item->pickedBody());
    for (int i : item->pickedVertices())
        body.shape = Geometry::withHandlesGrown(body.shape, i);
    m_host.commit(m_edited, a, tr("Add handles"));
    emit changed();   // the menu's entry, and the handles drawn
}

bool ShapeEditMode::deletePickedNodes()
{
    Object* edited = m_host.object(m_edited);
    if (!edited || edited->pickedVertex() < 0)
        return false;
    Composition a      = edited->composition();
    QList<int>  doomed = edited->pickedVertices();
    std::sort(doomed.begin(), doomed.end(), std::greater<int>());
    if (Words* w = warpPicked(a, *edited)) {
        for (int i : std::as_const(doomed))
            w->text.warp = Bezier::withNodeRemoved(w->text.warp, Geometry::k_warpRules, i);
        if (w->text.warp == warpPicked(edited->composition(), *edited)->text.warp)
            return true;   // two is the fewest
        edited->setPickedVertex(-1);
        m_host.commit(m_edited, a, tr("Delete point"));
        emit changed();
        return true;
    }
    Body* body = bodyOf(a, edited->pickedBody());
    if (!body)
        return true;
    for (int i : std::as_const(doomed))
        body->shape = Geometry::withVertexRemoved(body->shape, i);
    if (body->shape == bodyOf(edited->composition(), edited->pickedBody())->shape)
        return true;
    edited->setPickedVertex(-1);
    m_host.commit(m_edited, a, tr("Delete point"));
    emit changed();
    return true;
}

bool ShapeEditMode::canConvertToCurve() const
{
    const Object* item = m_host.object(m_selection.primary());
    const Body*   body = item ? bodyOf(item->composition(), item->pickedBody()) : nullptr;
    return body && m_selection.objects().size() == 1 && body->shape.kind != ShapeProperties::Kind::Curve;
}

void ShapeEditMode::convertToCurve()
{
    if (!canConvertToCurve())
        return;
    const QString uid  = m_selection.primary();
    Composition   a    = m_host.object(uid)->composition();
    Body&         body = *bodyOf(a, m_host.object(uid)->pickedBody());
    const QRectF  rect = Geometry::balloonRect(*a.balloon, body);
    body.shape         = Geometry::curveOf(Geometry::outlineOf(body, rect, a.seed), rect);
    m_host.commit(uid, a, tr("Convert to curve"));
    setOn(true);
}

}  // namespace StripEdit
