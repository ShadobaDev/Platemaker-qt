#ifndef STRIPEDIT_SCROLLEDPAGE_HPP
#define STRIPEDIT_SCROLLEDPAGE_HPP

#include <QFrame>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyle>

namespace StripEdit {

/**
 * @brief A scroll area whose page is as wide with the vertical bar as without it.
 *
 * The bar's width is kept free while the bar is hidden, so its showing up does not narrow the page: the
 * shape tiles reflow with the width, and a section opening far below them pushed the last tile onto a
 * row of its own. A style whose bars float over the content (transient) has no width to keep.
 */
class SteadyScrollArea : public QScrollArea
{
public:
    explicit SteadyScrollArea(QWidget* parent)
        : QScrollArea(parent)
    {
        connect(verticalScrollBar(), &QScrollBar::rangeChanged, this,
                [this](int, int max) { keepBarWidth(max > 0); });
        keepBarWidth(false);
    }

private:
    void keepBarWidth(bool barShown)
    {
        const bool transient = style()->styleHint(QStyle::SH_ScrollBar_Transient, nullptr, this);
        setViewportMargins(0, 0, barShown || transient ? 0 : verticalScrollBar()->sizeHint().width(), 0);
    }
};

/**
 * @brief Wraps @p page in a scroll area to prevent the column from resizing when the panel's contents change.
 *
 * **A panel may not decide how wide its column is.** A stacked widget's minimum is its pages' minimum,
 *  and a splitter may never take a child below that — so the right column grew the moment something was
 *  selected and the properties appeared (measured: 18 points empty, 174 with one object's controls, and
 *  more with the real panel). Every row in the list then slid sideways, out from under the pointer that
 *  had just come down on a mute checkbox, and the click landed on the row instead. Inside a scroll area
 *  the column's minimum is the scroll area's own — constant — and a panel too big for the column scrolls
 *  rather than shoving it. That is also what lets the object list keep its third of the height when the
 *  properties are long.
 *
 * Shared by both stacks a column holds — TOOL OPTIONS and OBJECT STATE — which is why it lives here.
 *
 * @param page The widget to wrap.
 * @param host The stacked widget that will contain the scroll area.
 * @return A pointer to the created scroll area.
 */
[[nodiscard]] inline QScrollArea* scrolled(QWidget* page, QStackedWidget* host)
{
    auto* area = new SteadyScrollArea(host);
    area->setFrameShape(QFrame::NoFrame);   // the panel already sits in a framed column
    area->setWidgetResizable(true);
    area->setWidget(page);
    return area;
}

}  // namespace StripEdit

#endif // STRIPEDIT_SCROLLEDPAGE_HPP
