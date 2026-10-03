#ifndef STRIPEDIT_OBJECTSTATESTACK_HPP
#define STRIPEDIT_OBJECTSTATESTACK_HPP

#include <QObject>

#include <platemaker/models/colour_correction.hpp>

class QStackedWidget;
class QWidget;

namespace StripEdit {

class ObjectState;
class StripLayout;
class StripState;

/**
 * @brief OBJECT STATE: which panel describes the selection, and what it says about the strip.
 *
 * Two panels share the column. ObjectState describes placed objects — one, several, or a tail — and
 * StripState describes the strip or one of its pages, which are selected too but are not overlays, so
 * they get the same surface with different contents rather than an object panel full of sections that
 * never apply. Which of the two shows is decided by the selection; the Editor asks, and this answers.
 */
class ObjectStateStack : public QObject
{
public:
    //! Adds both panels to @p stack, the form's OBJECT STATE stack, which also parents this.
    ObjectStateStack(QStackedWidget* stack, ObjectState* objects, StripState* strip);

    //! The object panel — for one object, several, or a tail. It binds itself to the selection.
    void showObject();

    //! The strip: how many pages, how many the grade skips, and the grade itself.
    void showStrip(const StripLayout& layout, const Platemaker::Models::ColourCorrection& cc);

    //! Page @p index of @p layout: what it is and whether the grade skips it.
    void showPage(const StripLayout& layout, int index, const Platemaker::Models::ColourCorrection& cc);

private:
    QStackedWidget* m_stack      = nullptr;
    StripState*     m_strip      = nullptr;
    QWidget*        m_objectPage = nullptr;   //!< ObjectState, wrapped to scroll.
    QWidget*        m_stripPage  = nullptr;   //!< StripState, wrapped to scroll.
};

}  // namespace StripEdit

#endif // STRIPEDIT_OBJECTSTATESTACK_HPP
