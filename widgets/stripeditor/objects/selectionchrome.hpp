#ifndef STRIPEDIT_SELECTIONCHROME_HPP
#define STRIPEDIT_SELECTIONCHROME_HPP

#include <QGraphicsItem>
#include <QPointer>
#include <QRectF>

#include <map>
#include <memory>

#include "objects/object.hpp"

class QGraphicsView;
class QSvgRenderer;

namespace StripEdit {

/**
 * @brief The selection's box, its eight grips and its tail handles — one item, above every object.
 *
 * They used to be drawn by each object in its own paint(), which put them at that object's place in
 * the stack: a balloon lower in the stack had its grips painted over, and its tail handle taken, by
 * whatever sat above it — a tip covered by another bubble could be neither seen nor grabbed. Drawn
 * here, above everything, they are always on top, as in GIMP, Inkscape or Figma.
 *
 * The item draws and hit-tests; it does not resize or aim anything. A press on a grip hands the drag to
 * the object that owns it (Object::beginDrag()), which keeps the one implementation of what a drag does.
 * Its shape is the grips and handles alone, so a press anywhere else falls through to the objects.
 *
 * Drawn in scene coordinates, at a fixed size on screen: the grips of a picture drawn at another scale
 * are the same size as a balloon's, which they were not while each object drew its own.
 *
 * While an object's shape is edited, the outline is traced instead of the box and its own handles take
 * the grips' place (Object::setShapeEditing()); a press on one is handed over as Grip::Shape.
 *
 * It also says when a selected object's lettering does not fit: the text area is outlined in red, the
 * way a layout program marks overset text. The lettering is clipped there as before; the outline is
 * what tells the author it is.
 */
class SelectionChrome : public QGraphicsItem
{
public:
    //! What a press on the chrome landed on.
    struct Hit
    {
        Object* object = nullptr;   //!< Null: nothing of the chrome is there.
        Grip    grip   = Grip::None;
        int     handle = -1;        //!< For Grip::Handle, which tail.
    };

    explicit SelectionChrome(const QGraphicsView* view);

    /**
     * @brief Re-reads the selection and where it is. Called whenever an object reports chromeChanged(),
     *        the selection changes, or the zoom does (the grips are a fixed size on screen).
     */
    void refresh();

    //! What of the chrome is under @p scenePos — the topmost selected object's grip or handle.
    [[nodiscard]] Hit hitAt(const QPointF& scenePos) const;

    QRectF       boundingRect() const override { return m_bounds; }
    QPainterPath shape() const override;
    void         paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

private:
    //! The selected objects the chrome is drawn for, topmost first.
    [[nodiscard]] QList<Object*> subjects() const;
    //! The grip size in scene units at the current zoom.
    [[nodiscard]] qreal span() const;
    //! The rectangle @p grip of @p o occupies, in scene units; empty when that grip is not offered.
    [[nodiscard]] QRectF gripRect(const Object* o, Grip grip, qreal s) const;
    //! The square a tail handle occupies, in scene units.
    [[nodiscard]] QRectF handleRect(const Object* o, int index, qreal s) const;
    //! @p o's outline handles while its shape is edited, in its item coordinates; none otherwise.
    [[nodiscard]] static QList<Geometry::ShapeHandle> shapeHandles(const Object* o);
    //! @p o's text area in scene units when its lettering does not fit it; empty when it does.
    [[nodiscard]] static QRectF overflowRect(const Object* o);
    //! The radius handle's arrow in @p fill, edged in @p edge, made once per pair.
    QSvgRenderer& radiusArrow(const QColor& fill, const QColor& edge);

    const QGraphicsView* m_view;
    QRectF               m_bounds;
    QList<QRectF>        m_overflows;   //!< Text areas the lettering spills out of, in scene units.
    QPointer<Object>     m_target;   //!< The object a press handed its drag to, until the release.
    //! The radius arrows made so far, by colour: two per theme, the plain one and the pressed one.
    std::map<QString, std::unique_ptr<QSvgRenderer>> m_arrows;
};

}  // namespace StripEdit

#endif // STRIPEDIT_SELECTIONCHROME_HPP
