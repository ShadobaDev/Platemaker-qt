#ifndef STRIPEDIT_SEETHROUGH_HPP
#define STRIPEDIT_SEETHROUGH_HPP

#include <QGraphicsPathItem>

namespace StripEdit {

/**
 * \brief A path drawn over the strip that a press, a hover and the hit test all go through: it has no shape. Its
 *        bounds are its path's, said here: left to the base, a pen with a width would take them from the empty shape,
 *        and nothing would be drawn. So its pen, if any, is a hairline.
 *
 * What the editor draws over the objects without being one: the mask brush's ring and stroke, the dimming round a
 * selected mask.
 */
class SeeThrough : public QGraphicsPathItem
{
public:
    using QGraphicsPathItem::QGraphicsPathItem;
    [[nodiscard]] QPainterPath shape() const override { return {}; }
    [[nodiscard]] QRectF       boundingRect() const override { return path().controlPointRect(); }
};

}  // namespace StripEdit

#endif // STRIPEDIT_SEETHROUGH_HPP
