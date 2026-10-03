#ifndef STRIPEDIT_SAMPLING_HPP
#define STRIPEDIT_SAMPLING_HPP

#include <QColor>
#include <QList>
#include <QPointF>

#include <optional>

class QGraphicsLineItem;
class QGraphicsScene;

namespace StripEdit {

class PageSource;
class StripLayout;

/**
 * @brief The colour drawn at @p scenePos — what the eyedropper takes. Nothing when there is no answer yet.
 *
 * **What is drawn is what is picked.** One pixel of the scene, composited: the page through its grade,
 * and every balloon, caption and imported asset over it, each with its own blend mode and opacity — the
 * same pixels the render will produce. Sampling the page pixmap alone was defensible and still wrong:
 * clicking a balloon gave the paper behind it.
 *
 * No colour, rather than a wrong one:
 * - between two pages, because the gutter is not a colour anyone means to pick;
 * - on a page still showing its blurry proxy, which would answer with an average of the colours around
 *   the point. The real pixels are requested instead, so the next press has them;
 * - where nothing is drawn at all.
 *
 * @param seams The seam guides, hidden for the one repaint along with selection chrome: both are the
 *              editor talking rather than the comic.
 */
[[nodiscard]] std::optional<QColor> sampleCanvas(QGraphicsScene& scene, const StripLayout& layout,
                                                 PageSource& pages, const QList<QGraphicsLineItem*>& seams,
                                                 const QPointF& scenePos);

}  // namespace StripEdit

#endif // STRIPEDIT_SAMPLING_HPP
