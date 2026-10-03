#ifndef STRIPEDIT_STRIPITEM_HPP
#define STRIPEDIT_STRIPITEM_HPP

#include <QGraphicsItem>

class QWidget;

namespace StripEdit {

class PageSource;
class StripLayout;

/**
 * @brief One graphics item that draws every input page as its own image — seam-free.
 *
 * One QGraphicsPixmapItem per page leaves a 1px hairline at each join (QGraphicsView clips and rounds
 * each item's edge independently). Drawing all pages through one item, in one painter pass, tiles them
 * edge-to-edge with no seam at any zoom. The item owns no pixels: it reads where each page goes from
 * the StripLayout and what to draw there from the PageSource — the built page if it is ready, its
 * blurry proxy if not — so the lazy/async machinery lives in one place.
 */
class StripItem : public QGraphicsItem
{
public:
    /**
     * @param layout    Where each page sits on the strip.
     * @param pages     The pixels: built, proxy and graded.
     * @param palette   Whose palette a page with no pixels yet is filled from — the editor's.
     */
    StripItem(const StripLayout& layout, const PageSource& pages, const QWidget* palette);

    QRectF boundingRect() const override;   //!< The whole strip, in scene coordinates.
    void   paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    const StripLayout& m_layout;
    const PageSource&  m_pages;
    const QWidget*     m_palette;
};

}  // namespace StripEdit

#endif // STRIPEDIT_STRIPITEM_HPP
