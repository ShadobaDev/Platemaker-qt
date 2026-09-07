#include "overlayitem.h"
#include "artifactpainter.h"

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
    refreshBounds();
}

void OverlayItem::refreshBounds()
{
    // Resolve once, here, and keep the paths: paint() must not rebuild an eleven-circle union or re-lay
    // a text document on every scroll and selection change.
    m_silhouette = m_fallback.isNull() ? artifactSilhouette(m_artifact) : QPainterPath();
    m_textPath   = m_fallback.isNull() ? artifactTextOutline(m_artifact) : QPainterPath();

    const QRectF next = m_fallback.isNull()
        ? artifactBoundsOf(m_artifact, m_silhouette, m_textPath)
        : QRectF(QPointF(0, 0), QSizeF(m_fallback.size()));
    if (next == m_bounds)
        return;
    prepareGeometryChange();
    m_bounds = next;
}

void OverlayItem::setArtifact(const TextArtifact& a)
{
    m_artifact = a;
    // A tail can reach outside the balloon, so the drawn extent moves for more reasons than a resize:
    // aiming one, bending it, or adding a second all change what this item covers.
    refreshBounds();
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
    refreshBounds();
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
    // The artifact's own extent, not its balloon: local (0,0) is still the balloon's top-left, so a tail
    // pointing up or left gives this rect a negative origin. Sharing artifactBounds() with the rasteriser
    // and the SVG writer is what keeps all three describing the same rectangle.
    return contentBounds()
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
        paintArtifactPaths(*painter, m_artifact, m_silhouette, m_textPath);
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

        painter->setBrush(option->palette.color(QPalette::Highlight));
        for (int i = 0; i < m_artifact.tails.size(); ++i)
            painter->drawEllipse(tailGripRect(i));
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
    default:                return {};
    }
    return QRectF(c.x() - s / 2, c.y() - s / 2, s, s);
}

QRectF OverlayItem::tailGripRect(int i) const
{
    if (i < 0 || i >= m_artifact.tails.size())
        return {};
    const qreal   s = gripSpan(this);
    const QPointF c = m_artifact.tails.at(i).tip;
    return QRectF(c.x() - s / 2, c.y() - s / 2, s, s);
}

OverlayItem::Grip OverlayItem::gripAt(const QPointF& local, int* tailIndex) const
{
    if (tailIndex)
        *tailIndex = -1;
    if (!isSelected())
        return Grip::Body;   // grips only exist on the selected item; a press elsewhere is a move

    // Tails first: a tip can sit near a corner, and aiming is the more specific intent there.
    for (int i = 0; i < m_artifact.tails.size(); ++i) {
        if (tailGripRect(i).contains(local)) {
            if (tailIndex)
                *tailIndex = i;
            return Grip::Tail;
        }
    }
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

    m_active        = gripAt(e->pos(), &m_activeTail);
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

    if (m_active == Grip::Tail && m_activeTail >= 0 && m_activeTail < m_artifact.tails.size()) {
        // Unclamped on purpose: the artwork's extent is computed from where its tails actually point, so
        // a tip well outside the balloon makes the artifact bigger rather than being cropped away.
        m_artifact.tails[m_activeTail].tip = e->pos();
        refreshBounds();
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
        // Keep every tail pointing the same relative way as the balloon changes size, and scale its
        // width with it — otherwise a tail keeps its absolute thickness and swamps a shrinking bubble.
        if (!m_artifact.box.isEmpty()) {
            const qreal sx = double(newBox.width())  / m_artifact.box.width();
            const qreal sy = double(newBox.height()) / m_artifact.box.height();
            for (Tail& t : m_artifact.tails) {
                t.tip = QPointF(t.tip.x() * sx, t.tip.y() * sy);
                t.baseWidth *= (sx + sy) / 2.0;
            }
        }
        m_artifact.box = newBox;
        refreshBounds();
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
