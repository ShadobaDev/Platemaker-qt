#include "canvas/stripitem.hpp"

#include "canvas/pagesource.hpp"
#include "objects/striplayout.hpp"

#include <QPainter>
#include <QPalette>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

namespace StripEdit {

StripItem::StripItem(const StripLayout& layout, const PageSource& pages, const QWidget* palette)
    : m_layout(layout)
    , m_pages(pages)
    , m_palette(palette)
{
}

QRectF StripItem::boundingRect() const
{
    const QSize s = m_layout.stripSize();
    return QRectF(0, 0, s.width(), s.height());
}

void StripItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*)
{
    // Smooth the pixmap interior, but turn OFF edge antialiasing: with AA on, each drawPixmap
    // coverage-antialiases the destination rect's edges at fractional zoom, so the boundary row
    // between two pages is only partially covered and the background hairlines through — that is
    // the "1px frame". AA off makes adjacent pages tile with hard edges, each device row owned by
    // exactly one page, no bleed. (Local to this item — the view keeps AA for text/seam lines.)
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF exposed = option->exposedRect;
    for (int i = 0; i < m_layout.pageCount(); ++i) {
        const QRectF r = m_layout.pageRect(i);
        if (!r.intersects(exposed))
            continue;

        if (m_pages.gradeActive()) {
            const QPixmap graded = m_pages.gradedOf(i);
            if (!graded.isNull()) {
                painter->drawPixmap(r.topLeft(), graded); // live grade preview
                continue;
            }
            // not graded yet → briefly show the ungraded page below while the grade is produced
        }

        const QPixmap page = m_pages.pageOf(i);
        if (!page.isNull()) {
            painter->drawPixmap(r.topLeft(), page);     // sharp, at the strip's native scale
            continue;
        }
        const QPixmap proxy = m_pages.proxyOf(i);
        if (!proxy.isNull())
            painter->drawPixmap(r, proxy, QRectF(proxy.rect())); // blurry proxy, scaled into the page rect
        else
            painter->fillRect(r, m_palette->palette().color(QPalette::Base)); // brief neutral placeholder
    }
}

}  // namespace StripEdit
