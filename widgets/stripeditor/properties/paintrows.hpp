#ifndef STRIPEDIT_PAINTROWS_HPP
#define STRIPEDIT_PAINTROWS_HPP

#include "properties/fieldrows.hpp"
#include "properties/propertygroupeditor.hpp"   // showCombo(), paintColourSwatch(), paintMixedSwatch()
#include "objectrecord.hpp"                      // patternTitle()

#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QPushButton>
#include <QSignalBlocker>

#include <functional>

namespace StripEdit {

namespace PaintRowsDetail {

//! The context the rows' strings are translated in.
constexpr char k_trContext[] = "StripEdit::PaintRows";

#define PM_TR(s) QT_TRANSLATE_NOOP("StripEdit::PaintRows", s)
//! A pattern's numbers, in strip pixels and degrees.
inline const FieldRowSpec k_patternRows[] = {
    {"spacing", PM_TR("Spacing:"), 1,   100, " px", PM_TR("From one dot, or line, to the next."), 1},
    {"size",    PM_TR("Size:"),    0,   100, " px", PM_TR("A dot's width, or a line's. Past the spacing they merge into a solid tone."), 1},
    {"angle",   PM_TR("Angle:"),   -90, 90,  "°",   PM_TR("How far the grid is turned, clockwise.")},
};
#undef PM_TR

}  // namespace PaintRowsDetail

/**
 * @brief The rows that edit one Paint a group holds (PLAN-L B33): its colour, its pattern, the pattern's
 *        colour and its numbers. The same rows in the Fill, the Line and the Effects sections.
 *
 * The panel contract as FieldRows keeps it: *Mixed* while a bound selection disagrees, and *touched*
 * once the artist moves a control, so applyEdited() writes only that. The numbers are a FieldRows over the
 * Paint's own field list. Header-only, a template over the group; it holds no QObject and connects through
 * the owning panel, and must not move once built.
 */
template <class Group>
class PaintRows
{
public:
    /**
     * @param paint       Which Paint of the group these rows edit.
     * @param colourTitle The colour row's label, already translated ("Colour", "Shadow colour").
     * @param values      The panel's own copy of the group, written on every edit.
     * @param edited      Called after an edit is in @p values: the panel emits changed() there.
     * @param committed   Called after a discrete choice (a colour picked): the panel emits committed().
     * @param grounded    A pattern lies over the colour, in a colour of its own (a fill, a line). Without, the
     *                    pattern is the colour's marks on nothing (a shadow), and has no colour row of its own.
     */
    void build(QWidget* owner, QFormLayout* form, Paint Group::* paint, const QString& colourTitle, Group& values,
               const bool& populating, std::function<void()> edited, std::function<void()> committed,
               bool grounded = true)
    {
        using namespace PaintRowsDetail;
        m_form     = form;
        m_grounded = grounded;
        m_paint = paint;
        m_shown = &(values.*paint);
        Paint& p = values.*paint;

        m_colour = new QPushButton(QCoreApplication::translate(k_trContext, "Colour"), owner);
        m_colour->setToolTip(grounded ? QCoreApplication::translate(k_trContext, "The colour; under a pattern, its ground.")
                                      : QCoreApplication::translate(k_trContext, "The colour, or a pattern's marks."));
        form->addRow(colourTitle, m_colour);
        m_kind = new QComboBox(owner);
        for (int i = 0; i <= int(Paint::k_lastKind); ++i)
            m_kind->addItem(patternTitle(static_cast<ObjectRecord::Pattern>(i)), i);
        m_kind->setToolTip(grounded ? QCoreApplication::translate(k_trContext,
                                          "The colour alone, or a halftone of a second colour over it: dots, lines or a grid.")
                                    : QCoreApplication::translate(k_trContext,
                                          "The colour alone, or a halftone of it: dots, lines or a grid, on nothing."));
        form->addRow(QCoreApplication::translate(k_trContext, "Pattern"), m_kind);
        m_second = new QPushButton(QCoreApplication::translate(k_trContext, "Pattern colour"), owner);
        m_second->setToolTip(QCoreApplication::translate(k_trContext, "The pattern's own colour, over the ground."));
        form->addRow(QCoreApplication::translate(k_trContext, "Pattern colour"), m_second);
        m_rows.build(owner, form, "", k_patternRows, k_trContext, p, populating, edited);

        QObject::connect(m_kind, &QComboBox::currentIndexChanged, owner, [this, &p, &populating, edited](int i) {
            if (populating || i < 0)
                return;
            p.kind        = static_cast<Paint::Kind>(m_kind->currentData().toInt());
            m_kindTouched = true;
            m_mixedKind   = false;
            refresh();
            edited();
        });
        // A colour is picked in a dialog, with its transparency: a shadow's is how dark it is.
        const auto pick = [owner, edited, committed](QColor& target, QPushButton* swatch, bool& touched, bool& mixed) {
            const QColor c = QColorDialog::getColor(target, owner, swatch->text(), QColorDialog::ShowAlphaChannel);
            if (!c.isValid())
                return;
            target  = c;
            touched = true;
            mixed   = false;
            paintColourSwatch(swatch, c);
            edited();
            committed();   // a dialog choice is discrete: commit it without waiting on a timer
        };
        QObject::connect(m_colour, &QPushButton::clicked, owner,
                         [this, &p, pick] { pick(p.colour, m_colour, m_colourTouched, m_mixedColour); });
        QObject::connect(m_second, &QPushButton::clicked, owner,
                         [this, &p, pick] { pick(p.second, m_second, m_secondTouched, m_mixedSecond); });
    }

    //! Starts a bind: nothing mixed, nothing touched.
    void reset()
    {
        m_mixedColour = m_mixedKind = m_mixedSecond = false;
        m_colourTouched = m_kindTouched = m_secondTouched = false;
        m_rows.reset();
    }

    //! Marks every control on which @p subject disagrees with @p shown as *Mixed*.
    void compare(const Group& subject, const Group& shown)
    {
        const Paint& a = subject.*m_paint;
        const Paint& b = shown.*m_paint;
        m_mixedColour  = m_mixedColour || a.colour != b.colour;
        m_mixedKind    = m_mixedKind || a.kind != b.kind;
        m_mixedSecond  = m_mixedSecond || a.second != b.second;
        m_rows.compare(a, b);
    }

    //! Shows @p values' Paint, or *Mixed*. Call it inside the panel's populating guard.
    void show(const Group& values)
    {
        const Paint& p = values.*m_paint;
        if (m_mixedColour)
            paintMixedSwatch(m_colour, m_colour->palette());
        else
            paintColourSwatch(m_colour, p.colour);
        showCombo(m_kind, m_mixedKind, m_kind->findData(int(p.kind)));
        if (m_mixedSecond)
            paintMixedSwatch(m_second, m_second->palette());
        else
            paintColourSwatch(m_second, p.second);
        m_rows.show(p);
        refresh();
    }

    //! Shows or hides every row; the pattern's own rows show only for a pattern.
    void setVisible(bool shown)
    {
        m_visible = shown;
        refresh();
    }

    //! Copies into @p target the controls the artist has touched, from @p values.
    void applyEdited(const Group& values, Group& target) const
    {
        const Paint& v = values.*m_paint;
        Paint&       t = target.*m_paint;
        if (m_colourTouched) t.colour = v.colour;
        if (m_kindTouched)   t.kind   = v.kind;
        if (m_secondTouched) t.second = v.second;
        m_rows.applyEdited(v, t);
    }

private:
    void refresh()
    {
        // A selection that disagrees shows the rows at least one of them has.
        const bool patterned = m_mixedKind || m_shown->kind != Paint::Kind::Solid;
        m_form->setRowVisible(m_colour, m_visible);
        m_form->setRowVisible(m_kind, m_visible);
        m_form->setRowVisible(m_second, m_visible && patterned && m_grounded);
        m_rows.setVisible(m_form, m_visible && patterned);
    }

    QFormLayout*     m_form   = nullptr;
    Paint Group::*   m_paint  = nullptr;
    const Paint*     m_shown  = nullptr;   //!< The panel's copy, whose kind decides which rows show.
    QPushButton*     m_colour = nullptr;
    QComboBox*       m_kind   = nullptr;
    QPushButton*     m_second = nullptr;
    FieldRows<Paint> m_rows;               //!< Spacing, size and angle.
    bool m_visible       = true;
    bool m_grounded      = true;   //!< A pattern lies over the colour, in the pattern colour.
    bool m_mixedColour   = false;
    bool m_mixedKind     = false;
    bool m_mixedSecond   = false;
    bool m_colourTouched = false;
    bool m_kindTouched   = false;
    bool m_secondTouched = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_PAINTROWS_HPP
