#ifndef STRIPEDIT_SCROLLEDPAGE_HPP
#define STRIPEDIT_SCROLLEDPAGE_HPP

#include <QFrame>
#include <QScrollArea>
#include <QStackedWidget>

namespace StripEdit {

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
    auto* area = new QScrollArea(host);
    area->setFrameShape(QFrame::NoFrame);   // the panel already sits in a framed column
    area->setWidgetResizable(true);
    area->setWidget(page);
    return area;
}

}  // namespace StripEdit

#endif // STRIPEDIT_SCROLLEDPAGE_HPP
