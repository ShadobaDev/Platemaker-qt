#include "layout.h"

namespace StripEdit {

namespace {
//! Returned for an out-of-range page, so every accessor can stay total (see page()).
const Page& nullPage()
{
    static const Page k_none;
    return k_none;
}
} // namespace

void Layout::build(const std::vector<Platemaker::Core::PagePreviewGeometry>& geometry,
                        const std::vector<Platemaker::Models::InputFile>&         inputs)
{
    clear();

    int y = 0;
    for (int i = 0; i < static_cast<int>(geometry.size()); ++i) {
        const auto& g = geometry[static_cast<std::size_t>(i)];
        // A page the render would skip contributes nothing to the strip. Dropping it here is what keeps
        // every page below it at the offset the render will give it.
        if (!g.readable || g.width <= 0 || g.height <= 0)
            continue;

        Page p;
        p.inputIndex = i;
        p.sourcePath = QString::fromStdString(g.sourceFilePath);
        if (i >= 0 && i < static_cast<int>(inputs.size()))
            p.inputUid = QString::fromStdString(inputs[static_cast<std::size_t>(i)].uid);
        p.top  = y;
        p.size = QSize(g.width, g.height);
        m_pages.append(p);

        y      += g.height;
        m_width = qMax(m_width, g.width);
    }
    m_height = y;
}

void Layout::clear()
{
    m_pages.clear();
    m_width  = 0;
    m_height = 0;
}

const Page& Layout::page(int i) const
{
    return (i >= 0 && i < m_pages.size()) ? m_pages.at(i) : nullPage();
}

QRectF Layout::pageRect(int i) const
{
    const Page& p = page(i);
    return QRectF(0, p.top, p.size.width(), p.size.height());
}

int Layout::pageAtSceneY(qreal y) const
{
    if (m_pages.isEmpty())
        return -1;
    for (int i = m_pages.size() - 1; i >= 0; --i)
        if (y >= m_pages.at(i).top)
            return i;
    return 0;   // above the first page: clamp, never fall through to an absolute placement
}

QString Layout::anchorUidForPage(int page) const
{
    return this->page(page).inputUid;
}

int Layout::pageForAnchor(const QString& uid) const
{
    if (uid.isEmpty())
        return -1;
    for (int p = 0; p < m_pages.size(); ++p)
        if (m_pages.at(p).inputUid == uid)
            return p;
    return -1;
}

QPointF Layout::scenePosOf(const Platemaker::Models::StripOverlay& o) const
{
    const int page = pageForAnchor(QString::fromStdString(o.anchorInputUid));
    const int top  = (page >= 0) ? m_pages.at(page).top : 0;
    return QPointF(o.x, top + o.y);
}

}  // namespace StripEdit
