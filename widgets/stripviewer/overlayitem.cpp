#include "overlayitem.h"

#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QPainter>
#include <QPen>
#include <QStyleOptionGraphicsItem>

#include <utility>

namespace {

//! Smallest box the author can resize to — below this the grips overlap and it stops being draggable.
constexpr int k_minBox = 40;
//! Grip size in *screen* pixels, converted to scene units against the current zoom (see gripSpan()).
constexpr qreal k_gripScreenPx = 9.0;
//! Bounds on that conversion, so boundingRect() can reserve a fixed margin and stay valid at any zoom.
constexpr qreal k_gripMin = 6.0;
constexpr qreal k_gripMax = 18.0;
//! Room reserved around the box for grips — half of k_gripMax, rounded up.
constexpr qreal k_gripMargin = 10.0;

QPainter::CompositionMode compositionFor(Platemaker::Models::BlendMode b)
{
    using B = Platemaker::Models::BlendMode;
    switch (b) {
    case B::Multiply: return QPainter::CompositionMode_Multiply;
    case B::Screen:   return QPainter::CompositionMode_Screen;
    case B::Overlay:  return QPainter::CompositionMode_Overlay;
    case B::Darken:   return QPainter::CompositionMode_Darken;
    case B::Lighten:  return QPainter::CompositionMode_Lighten;
    case B::Over:     break;
    }
    return QPainter::CompositionMode_SourceOver;
}

} // namespace

OverlayItem::OverlayItem(QString uid, TextArtifact artifact, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_uid(std::move(uid))
    , m_artifact(std::move(artifact))
{
    // Selectable but not ItemIsMovable: a press can land on a resize or tail grip, and letting the base
    // class move the item as well would drag the bubble while the author is resizing it. Movement is
    // handled here, in one place, for whichever grip the press actually hit.
    setFlag(ItemIsSelectable, true);
    setAcceptHoverEvents(true);
}

void OverlayItem::setArtifact(const TextArtifact& a)
{
    if (a.box != m_artifact.box)
        prepareGeometryChange();
    m_artifact = a;
    update();
}

void OverlayItem::setBlend(Platemaker::Models::BlendMode blend)
{
    if (m_blend == blend)
        return;
    m_blend = blend;
    update();
}

void OverlayItem::setFallbackPixmap(const QPixmap& pm)
{
    if (m_fallback.cacheKey() == pm.cacheKey())
        return;
    m_fallback = pm;
    update();
}

void OverlayItem::setOrphaned(bool orphaned)
{
    if (m_orphaned == orphaned)
        return;
    m_orphaned = orphaned;
    setFlag(ItemIsSelectable, !orphaned);
    setAcceptHoverEvents(!orphaned);
    update();
}

QRectF OverlayItem::boundingRect() const
{
    return QRectF(0, 0, m_artifact.box.width(), m_artifact.box.height())
        .adjusted(-k_gripMargin, -k_gripMargin, k_gripMargin, k_gripMargin);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void OverlayItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*)
{
    painter->save();
    if (m_orphaned)
        painter->setOpacity(0.35);   // present but not rendering — see the artifact list's "re-anchor"

    painter->setCompositionMode(compositionFor(m_blend));
    if (m_fallback.isNull())
        paintArtifact(*painter, m_artifact);
    else
        painter->drawPixmap(QPointF(0, 0), m_fallback);
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);

    if (option->state & QStyle::State_Selected) {
        const QRectF box(0, 0, m_artifact.box.width(), m_artifact.box.height());

        // Selection chrome in the palette's highlight colour, cosmetic so it stays 1px at any zoom
        // (no hardcoded colours — the app is themed).
        QPen pen(option->palette.color(QPalette::Highlight));
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(box);

        pen.setStyle(Qt::SolidLine);
        painter->setPen(pen);
        painter->setBrush(option->palette.color(QPalette::Base));
        for (const Grip g : {Grip::TopLeft, Grip::TopRight, Grip::BottomLeft, Grip::BottomRight})
            painter->drawRect(gripRect(g));

        if (m_artifact.hasTail()) {
            painter->setBrush(option->palette.color(QPalette::Highlight));
            painter->drawEllipse(gripRect(Grip::Tail));
        }
    }
    painter->restore();
}

// ---------------------------------------------------------------------------
// Grips
// ---------------------------------------------------------------------------

namespace {
//! Grip size in scene units for the current zoom, so it stays roughly constant on screen.
qreal gripSpan(const QGraphicsItem* item)
{
    qreal scale = 1.0;
    if (const QGraphicsScene* s = item->scene(); s && !s->views().isEmpty())
        scale = s->views().first()->transform().m11();
    if (scale <= 0.0)
        scale = 1.0;
    return qBound(k_gripMin, k_gripScreenPx / scale, k_gripMax);
}
} // namespace

QPointF OverlayItem::tailPoint() const
{
    return QPointF(m_artifact.tail.x(), m_artifact.tail.y());
}

QRectF OverlayItem::gripRect(Grip g) const
{
    const qreal  s = gripSpan(this);
    const QRectF box(0, 0, m_artifact.box.width(), m_artifact.box.height());

    QPointF c;
    switch (g) {
    case Grip::TopLeft:     c = box.topLeft();     break;
    case Grip::TopRight:    c = box.topRight();    break;
    case Grip::BottomLeft:  c = box.bottomLeft();  break;
    case Grip::BottomRight: c = box.bottomRight(); break;
    case Grip::Tail:        c = tailPoint();       break;
    default:                return {};
    }
    return QRectF(c.x() - s / 2, c.y() - s / 2, s, s);
}

OverlayItem::Grip OverlayItem::gripAt(const QPointF& local) const
{
    if (!isSelected())
        return Grip::Body;   // grips only exist on the selected item; a press elsewhere is a move

    // Tail first: it can sit near a corner, and aiming is the more specific intent there.
    if (m_artifact.hasTail() && gripRect(Grip::Tail).contains(local))
        return Grip::Tail;
    for (const Grip g : {Grip::TopLeft, Grip::TopRight, Grip::BottomLeft, Grip::BottomRight})
        if (gripRect(g).contains(local))
            return g;
    return Grip::Body;
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

void OverlayItem::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    if (m_orphaned || e->button() != Qt::LeftButton) {
        QGraphicsObject::mousePressEvent(e);
        return;
    }

    // Let the base class run the selection logic (which may select this item), then read the grip —
    // gripAt() depends on being selected, so the order matters.
    QGraphicsObject::mousePressEvent(e);

    m_active        = gripAt(e->pos());
    m_startRect     = QRectF(pos(), QSizeF(m_artifact.box));
    m_startScenePos = e->scenePos();
    m_moved         = false;
    e->accept();
}

void OverlayItem::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    if (m_active == Grip::None) {
        QGraphicsObject::mouseMoveEvent(e);
        return;
    }

    const QPointF delta = e->scenePos() - m_startScenePos;
    m_moved = true;

    if (m_active == Grip::Body) {
        setPos(m_startRect.topLeft() + delta);
        return;
    }

    if (m_active == Grip::Tail) {
        // The tail is stored in box coordinates and clamped to the box: the artifact's bitmap *is* its
        // box, so a tip outside it would simply be cropped away by the rasteriser.
        const QPointF local = e->pos();
        m_artifact.tail = QPoint(qBound(0, qRound(local.x()), m_artifact.box.width()),
                                 qBound(0, qRound(local.y()), m_artifact.box.height()));
        update();
        return;
    }

    // Corner resize: work from the rect at press rather than accumulating per-move deltas, so a fast
    // drag cannot accumulate rounding error.
    QRectF r = m_startRect;
    switch (m_active) {
    case Grip::TopLeft:     r.setTopLeft(r.topLeft() + delta);         break;
    case Grip::TopRight:    r.setTopRight(r.topRight() + delta);       break;
    case Grip::BottomLeft:  r.setBottomLeft(r.bottomLeft() + delta);   break;
    case Grip::BottomRight: r.setBottomRight(r.bottomRight() + delta); break;
    default: break;
    }
    r = r.normalized();
    if (r.width() < k_minBox || r.height() < k_minBox)
        return;   // refuse rather than clamp: clamping makes the box "stick" and jump on the way back

    const QSize newBox(qRound(r.width()), qRound(r.height()));
    if (newBox != m_artifact.box) {
        // Keep the tail pointing the same relative way as the balloon changes size.
        if (m_artifact.tail.y() >= 0 && !m_artifact.box.isEmpty()) {
            m_artifact.tail = QPoint(
                qRound(m_artifact.tail.x() * double(newBox.width())  / m_artifact.box.width()),
                qRound(m_artifact.tail.y() * double(newBox.height()) / m_artifact.box.height()));
        }
        prepareGeometryChange();
        m_artifact.box = newBox;
    }
    setPos(r.topLeft());
    update();
}

void OverlayItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* e)
{
    const bool report = m_moved && m_active != Grip::None;
    m_active = Grip::None;
    m_moved  = false;
    QGraphicsObject::mouseReleaseEvent(e);

    // Only a settled drag is persisted: the owner turns each report into one undo step, and reporting
    // per mouse-move would bury the history under a pixel-by-pixel trail.
    if (report)
        emit geometryEdited(m_uid);
}

void OverlayItem::hoverMoveEvent(QGraphicsSceneHoverEvent* e)
{
    switch (gripAt(e->pos())) {
    case Grip::TopLeft:
    case Grip::BottomRight: setCursor(Qt::SizeFDiagCursor); break;
    case Grip::TopRight:
    case Grip::BottomLeft:  setCursor(Qt::SizeBDiagCursor); break;
    case Grip::Tail:        setCursor(Qt::CrossCursor);     break;
    default:                setCursor(Qt::SizeAllCursor);   break;
    }
    QGraphicsObject::hoverMoveEvent(e);
}
