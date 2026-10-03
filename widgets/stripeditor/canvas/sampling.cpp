#include "canvas/sampling.hpp"

#include "canvas/pagesource.hpp"
#include "objects/object.hpp"
#include "objects/striplayout.hpp"

#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>

namespace StripEdit {

std::optional<QColor> sampleCanvas(QGraphicsScene& scene, const StripLayout& layout, PageSource& pages,
                                   const QList<QGraphicsLineItem*>& seams, const QPointF& scenePos)
{
    if (layout.isEmpty())
        return std::nullopt;
    const int page = layout.pageAtSceneY(scenePos.y());
    if (page < 0 || !layout.pageRect(page).contains(scenePos))
        return std::nullopt;   // the gutter between two pages is not a colour anyone means to pick

    // A page still showing its blurry proxy is not sampled: a stand-in would answer with an average of
    // the colours around the point rather than the colour at it. Ask for the real pixels instead.
    if (pages.gradedOf(page).isNull() && pages.pageOf(page).isNull()) {
        pages.request(page);
        return std::nullopt;
    }

    // Two things in the scene are the editor talking rather than the comic, and they are hidden for the
    // one repaint: selection chrome, and the seam guides.
    QList<QGraphicsLineItem*> hiddenSeams;
    for (QGraphicsLineItem* seam : seams) {
        if (seam->isVisible()) {
            seam->setVisible(false);
            hiddenSeams.append(seam);
        }
    }
    Object::setChromeVisible(false);

    QImage pixel(1, 1, QImage::Format_ARGB32);
    pixel.fill(Qt::transparent);
    {
        QPainter p(&pixel);
        scene.render(&p, QRectF(0, 0, 1, 1),                       // the pixel under the cursor,
                     QRectF(scenePos - QPointF(0.5, 0.5), QSizeF(1, 1)),   // not the one past it
                     Qt::IgnoreAspectRatio);
    }

    Object::setChromeVisible(true);
    for (QGraphicsLineItem* seam : std::as_const(hiddenSeams))
        seam->setVisible(true);

    const QColor picked = pixel.pixelColor(0, 0);
    if (picked.alpha() == 0)
        return std::nullopt;   // nothing was drawn there after all
    return picked;
}

}  // namespace StripEdit
