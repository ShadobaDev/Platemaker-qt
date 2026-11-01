#ifndef STRIPEDIT_PAINTROWS_HPP
#define STRIPEDIT_PAINTROWS_HPP

#include "properties/fieldrows.hpp"
#include "properties/propertygroupeditor.hpp"   // showCombo(), paintColourSwatch(), paintMixedSwatch()
#include "objectrecord.hpp"                      // patternTitle()

#include <QCheckBox>
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
    {"cellSpacing", PM_TR("Spacing:"), 1,   100, " px", PM_TR("From one dot, or line, to the next."), 1},
    {"cellSize",    PM_TR("Size:"),    0,   100, " px", PM_TR("A dot's width, or a line's. Past the spacing they merge into a solid tone."), 1},
    {"cellAngle",   PM_TR("Angle:"),   -90, 90,  "°",   PM_TR("How far the grid is turned, clockwise.")},
};
#undef PM_TR

}  // namespace PaintRowsDetail

/**
 * @brief The rows that edit one Paint a group holds (PLAN-L B33): its colour, its pattern, the pattern's
 *        colour and its numbers. The same rows in the Fill, the Line and the Effects sections.
 *
 * The panel contract as FieldRows keeps it: *Mixed* while a bound selection disagrees, and *touched*
 * once the artist moves a control, so applyEdited() writes only that. The numbers are a FieldRows over the
 * Paint's own field list (its `cell…` fields). **Which rows a place offers is its group's field list**: a
 * part the group does not list (Paint::fieldsAs() with a null key) is not used there and has no row — a
 * shadow's pattern has no colour of its own. A fill's fade is set by its cross on the strip, in shape
 * editing (Geometry::fadeHandles()); here it has only its shape and the check box that turns it over. Header-only, a
 * template over the group; it holds no QObject and connects through the owning panel, and must not move
 * once built.
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
     */
    void build(QWidget* owner, QFormLayout* form, Paint Group::* paint, const QString& colourTitle, Group& values,
               const bool& populating, std::function<void()> edited, std::function<void()> committed)
    {
        using namespace PaintRowsDetail;
        m_form     = form;
        m_paint    = paint;
        m_grounded = uses(&Paint::second);
        const bool grounded = m_grounded;
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
        m_rows.build(owner, form, "cell", k_patternRows, k_trContext, p, populating, edited);
        if (uses(&Paint::fadeRound)) {
            m_round = new QComboBox(owner);
            m_round->addItem(QCoreApplication::translate(k_trContext, "Rectangle"), false);
            m_round->addItem(QCoreApplication::translate(k_trContext, "Ellipse"), true);
            m_round->setToolTip(QCoreApplication::translate(k_trContext,
                "The fade's shape, set by the cross on the balloon in shape editing: each arm fading along "
                "its own side, or ellipses through the arms' dots."));
            form->addRow(QCoreApplication::translate(k_trContext, "Fade"), m_round);
            QObject::connect(m_round, &QComboBox::currentIndexChanged, owner, [this, &p, &populating, edited](int i) {
                if (populating || i < 0)
                    return;
                p.fadeRound    = m_round->currentData().toBool();
                m_roundTouched = true;
                m_mixedRound   = false;
                edited();
            });
        }
        if (uses(&Paint::fadeReversed)) {
            m_reversed = new QCheckBox(QCoreApplication::translate(k_trContext, "From the centre"), owner);
            m_reversed->setToolTip(QCoreApplication::translate(k_trContext,
                "Turns the fade over: the pattern is gone at the cross's centre and whole past its ends. "
                "The cross is set on the balloon, in shape editing."));
            form->addRow(QString(), m_reversed);
            QObject::connect(m_reversed, &QCheckBox::toggled, owner, [this, &p, &populating, edited, committed](bool on) {
                if (populating)
                    return;
                m_reversed->setTristate(false);   // they have taken a position
                p.fadeReversed    = on;
                m_reversedTouched = true;
                m_mixedReversed   = false;
                edited();
                committed();   // a tick is discrete, like a dialog's choice
            });
        }

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
        m_mixedColour = m_mixedKind = m_mixedSecond = m_mixedReversed = m_mixedRound = false;
        m_colourTouched = m_kindTouched = m_secondTouched = m_reversedTouched = m_roundTouched = false;
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
        m_mixedReversed = m_mixedReversed || a.fadeReversed != b.fadeReversed;
        m_mixedRound    = m_mixedRound || a.fadeRound != b.fadeRound;
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
        if (m_round)
            showCombo(m_round, m_mixedRound, m_round->findData(p.fadeRound));
        if (m_reversed)
            showCheck(m_reversed, m_mixedReversed, p.fadeReversed);
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
        if (m_reversedTouched) t.fadeReversed = v.fadeReversed;
        if (m_roundTouched)    t.fadeRound    = v.fadeRound;
        m_rows.applyEdited(v, t);
    }

private:
    //! Whether the group lists @p field of this Paint: a place offers only the parts it uses.
    template <class T>
    [[nodiscard]] bool uses(T Paint::* field) const
    {
        bool used = false;
        Group::fields([&](const char*, auto member) {
            if constexpr (Fields::isNested<decltype(member)>) {
                if constexpr (std::is_same_v<decltype(member.outer), Paint Group::*>
                              && std::is_same_v<decltype(member.inner), T Paint::*>)
                    used = used || (member.outer == m_paint && member.inner == field);
            }
        });
        return used;
    }

    void refresh()
    {
        // A selection that disagrees shows the rows at least one of them has.
        const bool patterned = m_mixedKind || m_shown->kind != Paint::Kind::Solid;
        m_form->setRowVisible(m_colour, m_visible);
        m_form->setRowVisible(m_kind, m_visible);
        m_form->setRowVisible(m_second, m_visible && patterned && m_grounded);
        m_rows.setVisible(m_form, m_visible && patterned);
        if (m_round)
            m_form->setRowVisible(m_round, m_visible && patterned);
        if (m_reversed)
            m_form->setRowVisible(m_reversed, m_visible && patterned);
    }

    QFormLayout*     m_form   = nullptr;
    Paint Group::*   m_paint  = nullptr;
    const Paint*     m_shown  = nullptr;   //!< The panel's copy, whose kind decides which rows show.
    QPushButton*     m_colour = nullptr;
    QComboBox*       m_kind   = nullptr;
    QPushButton*     m_second = nullptr;
    QComboBox*       m_round    = nullptr;   //!< Only where the group lists the fade (the fill).
    QCheckBox*       m_reversed = nullptr;   //!< ...likewise.
    FieldRows<Paint> m_rows;               //!< Spacing, size and angle.
    bool m_visible       = true;
    bool m_grounded      = true;   //!< A pattern lies over the colour, in the pattern colour (not a shadow's).
    bool m_mixedColour   = false;
    bool m_mixedKind     = false;
    bool m_mixedSecond   = false;
    bool m_colourTouched = false;
    bool m_kindTouched   = false;
    bool m_secondTouched = false;
    bool m_mixedReversed   = false;
    bool m_reversedTouched = false;
    bool m_mixedRound      = false;
    bool m_roundTouched    = false;
};

}  // namespace StripEdit

#endif // STRIPEDIT_PAINTROWS_HPP
