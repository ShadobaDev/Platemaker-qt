#ifndef STRIPEDIT_SHADOWROWS_HPP
#define STRIPEDIT_SHADOWROWS_HPP

#include "properties/fieldrows.hpp"
#include "properties/paintrows.hpp"
#include "properties/propertygroupeditor.hpp"   // showCheck()

#include <QCheckBox>
#include <QCoreApplication>
#include <QFormLayout>

#include <functional>

namespace StripEdit {

namespace ShadowRowsDetail {

//! The context the rows' strings are translated in.
constexpr char k_trContext[] = "StripEdit::ShadowRows";

#define PM_TR(s) QT_TRANSLATE_NOOP("StripEdit::ShadowRows", s)
//! A shadow's numbers, in strip pixels.
inline const FieldRowSpec k_rows[] = {
    {"spread",   PM_TR("Spread:"),        0,    50,  " px",
     PM_TR("How far the shadow grows past what casts it on every side; with no offset, a rim all round."), 1},
    {"x",        PM_TR("Offset across:"), -100, 100, " px", PM_TR("How far right the shadow falls; negative is left."), 1},
    {"y",        PM_TR("Offset down:"),   -100, 100, " px", PM_TR("How far down the shadow falls; negative is up."), 1},
    {"softness", PM_TR("Softness:"),      0,    50,  " px",
     PM_TR("How blurred its edge is. 0 is a hard shadow, drawn as you edit; above it the library draws the "
           "balloon, exactly as it will be baked. A halftone shadow fades instead: its dots or lines thin out "
           "over this width, drawn as you edit."), 1},
};
#undef PM_TR

}  // namespace ShadowRowsDetail

/**
 * \brief The rows that edit one Shadow a group holds, and the Paint it holds beside it: a check box, the
 *        numbers while it is on, and the paint. The same rows for the balloon's shadow and the letters'.
 *
 * The panel contract as PaintRows keeps it: *Mixed* while a bound selection disagrees, and *touched* once
 * the artist moves a control, so applyEdited() writes only that. The numbers are a FieldRows over the
 * Shadow's own field list. Header-only, a template over the group; it holds no QObject and connects
 * through the owning panel, and must not move once built.
 */
template <class Group>
class ShadowRows
{
public:
    /**
     * \param shadow     Which Shadow of the group these rows edit.
     * \param fill       ...and which Paint fills it.
     * \param title      The check box's row label, already translated ("Shadow").
     * \param tip        The check box's tooltip, already translated: what casts the shadow.
     * \param values     The panel's own copy of the group, written on every edit.
     * \param edited     Called after an edit is in @p values: the panel emits changed() there.
     * \param committed  Called after a discrete choice (a tick, a colour): the panel emits committed().
     */
    void build(QWidget* owner, QFormLayout* form, Shadow Group::* shadow, Paint Group::* fill,
               const QString& title, const QString& tip, Group& values, const bool& populating,
               std::function<void()> edited, std::function<void()> committed)
    {
        using namespace ShadowRowsDetail;
        m_form   = form;
        m_member = shadow;
        m_shown  = &(values.*shadow);
        Shadow& s = values.*shadow;

        m_on = new QCheckBox(QCoreApplication::translate(k_trContext, "Drop a shadow"), owner);
        m_on->setToolTip(tip);
        form->addRow(title, m_on);
        m_rows.build(owner, form, "", k_rows, k_trContext, s, populating, edited);
        // Its paint, whose transparency is how dark it is; or a halftone of it, with no ground under.
        m_paint.build(owner, form, fill, QCoreApplication::translate(k_trContext, "Shadow colour"), values,
                      populating, edited, committed);

        QObject::connect(m_on, &QCheckBox::toggled, owner, [this, &s, &populating, edited, committed](bool on) {
            if (populating)
                return;
            m_on->setTristate(false);   // they have taken a position
            s.on      = on;
            m_touched = true;
            m_mixed   = false;
            refresh();
            edited();
            committed();   // a tick is discrete, like a dialog's choice
        });
    }

    //! Starts a bind: nothing mixed, nothing touched.
    void reset()
    {
        m_mixed = m_touched = false;
        m_rows.reset();
        m_paint.reset();
    }

    //! Marks every control on which @p subject disagrees with @p shown as *Mixed*.
    void compare(const Group& subject, const Group& shown)
    {
        m_mixed = m_mixed || (subject.*m_member).on != (shown.*m_member).on;
        m_rows.compare(subject.*m_member, shown.*m_member);
        m_paint.compare(subject, shown);
    }

    //! Shows @p values' shadow, or *Mixed*. Call it inside the panel's populating guard.
    void show(const Group& values)
    {
        showCheck(m_on, m_mixed, (values.*m_member).on);
        m_rows.show(values.*m_member);
        m_paint.show(values);
        refresh();
    }

    //! Copies into @p target the controls the artist has touched, from @p values.
    void applyEdited(const Group& values, Group& target) const
    {
        if (m_touched)
            (target.*m_member).on = (values.*m_member).on;
        m_rows.applyEdited(values.*m_member, target.*m_member);
        m_paint.applyEdited(values, target);
    }

    //! Shows or hides the shadow's every row, the tick included.
    void setShown(bool shown)
    {
        m_allowed = shown;
        refresh();
    }

private:
    void refresh()
    {
        // A selection that disagrees shows the rows at least one of them has.
        const bool on = m_allowed && (m_mixed || m_shown->on);
        m_form->setRowVisible(m_on, m_allowed);
        m_rows.setVisible(m_form, on);
        m_paint.setVisible(on);
    }

    QFormLayout*      m_form   = nullptr;
    Shadow Group::*   m_member = nullptr;
    const Shadow*     m_shown  = nullptr;   //!< The panel's copy, whose tick decides which rows show.
    QCheckBox*        m_on     = nullptr;
    FieldRows<Shadow> m_rows;               //!< Spread, offset, softness.
    PaintRows<Group>  m_paint;              //!< Its paint.
    bool m_mixed   = false;
    bool m_touched = false;
    bool m_allowed = true;   //!< setShown().
};

}  // namespace StripEdit

#endif // STRIPEDIT_SHADOWROWS_HPP
