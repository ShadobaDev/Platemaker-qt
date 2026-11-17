#ifndef STRIPEDIT_FIELDROWS_HPP
#define STRIPEDIT_FIELDROWS_HPP

#include "properties/propertygroupeditor.hpp"   // showSpin(), restoreSpin(), showCombo(), showCheck()
#include "fields.hpp"                           // Fields::RowSpec, Fields::Section; Fields::same()

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QLabel>

#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>
#include <vector>

namespace StripEdit {

/**
 * \brief One row per field of a group's field list, for the fields whose key starts with a prefix: a spin
 *        for a number; for a bool, a combo if its spec names its two entries, else a check box.
 *
 * The rows a parameterised style or shape shows: built by walking `Group::fields()`, so a field added to
 * the list gets its row with no hand-written widget, and labelled from a spec table (Fields::RowSpec). A
 * number with the prefix and no spec stops a debug build; a bool without one gets no row (its panel shows
 * it otherwise).
 *
 * Each row keeps the panel contract on its own: *Mixed* while a bound selection disagrees about it
 * (showSpin(), showCombo()), and *touched* once the artist moves it, so applyEdited() writes only what was
 * edited. Header-only, as a template over the group; it holds no QObject of its own and connects through the
 * owning panel. It must not move once built: the connections hold a pointer to it.
 */
template <class Group>
class FieldRows
{
public:
    //! A spin for @p key, of a subclass the panel needs (e.g. one with an Auto value), or nullptr.
    using SpinFactory = std::function<QSpinBox*(const char* key)>;

    /**
     * \brief Adds the rows to @p form, in the field list's order.
     * \param owner      Parents the spins, and is the context of their connections.
     * \param prefix     Which fields get a row.
     * \param specs      Labels, ranges and tooltips (@p count of them), translated in @p context.
     * \param values     The panel's own copy of the group, written on every edit.
     * \param populating The panel's guard: while true, a spin's change is the panel showing a value.
     * \param edited     Called after a row's edit is in @p values: the panel emits changed() there.
     */
    void build(QWidget* owner, QFormLayout* form, const char* prefix, const Fields::RowSpec* specs, int count,
               const char* context, Group& values, const bool& populating, std::function<void()> edited,
               const SpinFactory& makeSpin = {})
    {
        const size_t prefixLength = std::strlen(prefix);
        Group::fields([&](const char* key, auto member) {
            using T = std::remove_cv_t<std::remove_reference_t<decltype(Fields::at(values, member))>>;
            // A row holds a number or a bool: a list (a Polygon's nodes) is never a row, and is not compiled as
            // one; nor is an enum (e.g. the edge's kind), which its panel shows as a combo of its own.
            // A sub-group's field (a Paint's) is its own editor's (PaintRows), whatever its key.
            if constexpr (!Fields::isNested<decltype(member)> && std::is_arithmetic_v<T>) {
                if (std::strncmp(key, prefix, prefixLength) != 0)
                    return;
                const Fields::RowSpec* spec = nullptr;
                for (int i = 0; i < count; ++i)
                    if (std::strcmp(specs[i].key, key) == 0)
                        spec = &specs[i];
                if constexpr (std::is_same_v<T, bool>) {
                    if (!spec)
                        return;   // its panel's own
                } else {
                    Q_ASSERT_X(spec, "FieldRows", "a field with the prefix and no row in the spec table");
                    if (!spec)
                        return;
                }

                Row row;
                row.key   = key;
                row.needs = spec->needs;
                row.min   = spec->min;
                if constexpr (std::is_same_v<T, bool>) {
                    if (spec->on) {
                        row.choice = new QComboBox(owner);
                        row.choice->addItem(QCoreApplication::translate(context, spec->on), true);
                        row.choice->addItem(QCoreApplication::translate(context, spec->off), false);
                        row.box = row.choice;
                    } else {
                        row.check = new QCheckBox(QCoreApplication::translate(context, spec->title), owner);
                        row.box   = row.check;
                    }
                    row.get = [member](const Group& v) { return Fields::at(v, member) ? 1.0 : 0.0; };
                    row.set = [member](Group& v, double x) { Fields::at(v, member) = x != 0.0; };
                } else {
                    if (spec->decimals > 0) {
                        row.decimal = new QDoubleSpinBox(owner);
                        row.decimal->setDecimals(spec->decimals);
                        row.decimal->setSingleStep(spec->step > 0.0 ? spec->step : std::pow(10.0, -spec->decimals));
                        row.decimal->setRange(spec->min, spec->max);
                        row.decimal->setSuffix(QString::fromUtf8(spec->suffix));   // a suffix may be "°"
                        row.box = row.decimal;
                        row.get = [member](const Group& v) { return double(Fields::at(v, member)); };
                    } else {
                        row.whole = makeSpin ? makeSpin(key) : nullptr;
                        if (!row.whole)
                            row.whole = new QSpinBox(owner);
                        row.whole->setRange(int(spec->min), int(spec->max));
                        row.whole->setSuffix(QString::fromUtf8(spec->suffix));
                        row.box = row.whole;
                        row.get = [member](const Group& v) { return double(qRound(double(Fields::at(v, member)))); };
                    }
                    // Into the member's own type: an integer field rounds what it is given.
                    row.set = [member](Group& v, double x) {
                        if constexpr (std::is_floating_point_v<T>)
                            Fields::at(v, member) = T(x);
                        else
                            Fields::at(v, member) = T(qRound(x));
                    };
                }
                row.box->setToolTip(QCoreApplication::translate(context, spec->tip));
                // A check box says its title itself.
                form->addRow(row.check ? QString() : QCoreApplication::translate(context, spec->title), row.box);
                if (QWidget* label = form->labelForField(row.box))   // the name says what the tip explains
                    label->setToolTip(row.box->toolTip());
                m_rows.push_back(row);

                const size_t index  = m_rows.size() - 1;
                const auto   onEdit = [this, index, &values, &populating, edited](double v) {
                    if (populating)
                        return;
                    Row& r = m_rows[index];
                    if (r.check)
                        r.check->setTristate(false);   // they have taken a position
                    if (r.whole ? restoreSpin(r.whole, int(r.min)) : r.decimal ? restoreSpin(r.decimal, r.min) : true)
                        r.mixed = false;
                    r.set(values, v);
                    r.touched = true;
                    edited();
                };
                if (row.choice)
                    QObject::connect(row.choice, &QComboBox::currentIndexChanged, owner,
                                     [onEdit, choice = row.choice](int i) {
                                         if (i >= 0)
                                             onEdit(choice->itemData(i).toBool() ? 1.0 : 0.0);
                                     });
                else if (row.check)
                    QObject::connect(row.check, &QCheckBox::checkStateChanged, owner, [onEdit](Qt::CheckState s) {
                        if (s != Qt::PartiallyChecked)   // Mixed is only ever shown, never picked
                            onEdit(s == Qt::Checked ? 1.0 : 0.0);
                    });
                else if (row.whole)
                    QObject::connect(row.whole, &QSpinBox::valueChanged, owner, [onEdit](int v) { onEdit(v); });
                else
                    QObject::connect(row.decimal, &QDoubleSpinBox::valueChanged, owner, onEdit);
            }
        });
    }

    //! build() over a spec table.
    template <size_t N>
    void build(QWidget* owner, QFormLayout* form, const char* prefix, const Fields::RowSpec (&specs)[N],
               const char* context, Group& values, const bool& populating, std::function<void()> edited,
               const SpinFactory& makeSpin = {})
    {
        build(owner, form, prefix, specs, int(N), context, values, populating, std::move(edited), makeSpin);
    }

    //! Starts a bind: nothing mixed, nothing touched.
    void reset()
    {
        for (Row& r : m_rows)
            r.mixed = r.touched = false;
    }

    //! Marks every row on which @p subject disagrees with @p shown as *Mixed*.
    void compare(const Group& subject, const Group& shown)
    {
        for (Row& r : m_rows)
            r.mixed = r.mixed || !Fields::same(r.get(subject), r.get(shown));
    }

    //! Shows @p values, or *Mixed*. Call it inside the panel's populating guard.
    void show(const Group& values) const
    {
        for (const Row& r : m_rows) {
            if (r.choice)
                showCombo(r.choice, r.mixed, r.choice->findData(r.get(values) != 0.0));
            else if (r.check)
                showCheck(r.check, r.mixed, r.get(values) != 0.0);
            else if (r.whole)
                showSpin(r.whole, r.mixed, qRound(r.get(values)), int(r.min), r.whole->maximum());
            else
                showSpin(r.decimal, r.mixed, r.get(values), r.min, r.decimal->maximum());
        }
    }

    //! Lowers or raises @p key's ceiling, e.g. to what the bound object can show. Call it before show().
    void setMaximum(const char* key, int max) const
    {
        for (const Row& r : m_rows)
            if (std::strcmp(r.key, key) == 0 && r.whole && r.whole->maximum() != max)
                r.whole->setMaximum(qMax(int(r.min), max));   // only on a change: setRange() stops a held arrow
    }

    void setVisible(QFormLayout* form, bool visible) const
    {
        for (const Row& r : m_rows)
            form->setRowVisible(r.box, visible);
    }

    //! Hides the rows whose spec `needs` a number that is 0 in @p values, and not *Mixed*. After setVisible().
    void hideUnneeded(QFormLayout* form, const Group& values) const
    {
        for (const Row& r : m_rows)
            if (r.needs)
                for (const Row& n : m_rows)
                    if (std::strcmp(n.key, r.needs) == 0 && !n.mixed && Fields::same(n.get(values), 0.0))
                        form->setRowVisible(r.box, false);
    }

    //! Shows or hides the rows whose key holds @p part, leaving the others as they are.
    void setRowsVisible(QFormLayout* form, const char* part, bool visible) const
    {
        for (const Row& r : m_rows)
            if (std::strstr(r.key, part))
                form->setRowVisible(r.box, visible);
    }

    //! Copies into @p target the rows the artist has touched, from @p values.
    void applyEdited(const Group& values, Group& target) const
    {
        for (const Row& r : m_rows)
            if (r.touched)
                r.set(target, r.get(values));
    }

private:
    //! One field's row, and how to reach the field: whole numbers on screen, with decimals, a choice or a check.
    struct Row
    {
        const char*                         key     = nullptr;
        const char*                         needs   = nullptr;   //!< Fields::RowSpec::needs
        QWidget*                            box     = nullptr;   //!< whichever of the four it is
        QSpinBox*                           whole   = nullptr;
        QDoubleSpinBox*                     decimal = nullptr;
        QComboBox*                          choice  = nullptr;
        QCheckBox*                          check   = nullptr;
        double                              min     = 0.0;
        std::function<double(const Group&)> get;     //!< As the row shows it (rounded for a whole one; 1 or 0).
        std::function<void(Group&, double)> set;
        bool                                mixed   = false;
        bool                                touched = false;
    };

    std::vector<Row> m_rows;
};

/**
 * \brief The rows of every section of a group (`Group::sections()`, fields.hpp), each under its heading if it
 *        has one: what a panel shows for the kinds of a thing whose numbers are sections (a line's, a shape's,
 *        a texture's).
 *
 * Built once, in the order the group lists its sections; showSections() then shows those a kind's table
 * entry names. The panel contract is FieldRows', section by section. A row that `needs` another
 * (Fields::RowSpec) follows it: the panel calls showSections() again after an edit.
 */
template <class Group>
class SectionRows
{
public:
    using SpinFactory = typename FieldRows<Group>::SpinFactory;

    //! Adds a heading and the rows for each section, in the group's order. As FieldRows::build().
    void build(QWidget* owner, QFormLayout* form, Group& values, const bool& populating,
               const std::function<void()>& edited, const SpinFactory& makeSpin = {})
    {
        Group::sections([&](auto member) {
            using Sub = std::remove_reference_t<decltype(values.*member)>;
            auto part = std::make_unique<Part<Sub>>();
            part->member  = member;
            part->section = &Sub::section();
            if (part->section->title) {
                part->heading = new QLabel(QCoreApplication::translate(Fields::k_trContext, part->section->title), owner);
                QFont font = part->heading->font();
                font.setBold(true);
                part->heading->setFont(font);
                form->addRow(part->heading);
            }
            part->rows.build(owner, form, "", part->section->rows, part->section->rowCount, Fields::k_trContext,
                             values.*member, populating, edited, makeSpin);
            m_parts.push_back(std::move(part));
        });
    }

    void reset()
    {
        for (auto& p : m_parts)
            p->reset();
    }

    void compare(const Group& subject, const Group& shown)
    {
        for (auto& p : m_parts)
            p->compare(subject, shown);
    }

    void show(const Group& values) const
    {
        for (const auto& p : m_parts)
            p->show(values);
    }

    void applyEdited(const Group& values, Group& target) const
    {
        for (const auto& p : m_parts)
            p->applyEdited(values, target);
    }

    //! FieldRows::setMaximum(), in whichever section has @p key.
    void setMaximum(const char* key, int max) const
    {
        for (const auto& p : m_parts)
            p->setMaximum(key, max);
    }

    /**
     * \brief Shows the sections named in @p names, every section when @p all (a selection that disagrees
     *        about the kind), and none when not @p visible. Of the named ones, the rows whose key holds
     *        @p hides are not shown, nor those that need a number that is 0 in @p values. A heading shows over
     *        each only where more than one is shown.
     */
    void showSections(QFormLayout* form, const Group& values, bool visible, bool all,
                      const Fields::SectionNames& names, const char* hides = nullptr) const
    {
        const auto named = [&](const Fields::Section& s) {
            for (const char* n : names)
                if (n && std::strcmp(n, s.name) == 0)
                    return true;
            return false;
        };
        int shown = 0;
        for (const auto& p : m_parts)
            shown += visible && (all || named(*p->section));
        for (const auto& p : m_parts) {
            const bool on = visible && (all || named(*p->section));
            p->setVisible(form, on, values);
            if (on && !all && hides)
                p->hideRows(form, hides);
            if (p->heading)
                form->setRowVisible(p->heading, on && shown > 1 && !all);
        }
    }

private:
    struct Base
    {
        virtual ~Base() = default;
        virtual void reset()                                                 = 0;
        virtual void compare(const Group& subject, const Group& shown)      = 0;
        virtual void show(const Group& values) const                        = 0;
        virtual void applyEdited(const Group& values, Group& target) const  = 0;
        virtual void setVisible(QFormLayout* form, bool visible, const Group& values) const = 0;
        virtual void hideRows(QFormLayout* form, const char* part) const    = 0;
        virtual void setMaximum(const char* key, int max) const             = 0;

        const Fields::Section* section = nullptr;
        QLabel*                heading = nullptr;   //!< nullptr for a section with no title
    };

    template <class Sub>
    struct Part : Base
    {
        Sub Group::*   member = nullptr;
        FieldRows<Sub> rows;

        void reset() override { rows.reset(); }
        void compare(const Group& subject, const Group& shown) override { rows.compare(subject.*member, shown.*member); }
        void show(const Group& values) const override { rows.show(values.*member); }
        void applyEdited(const Group& values, Group& target) const override
        {
            rows.applyEdited(values.*member, target.*member);
        }
        void setVisible(QFormLayout* form, bool visible, const Group& values) const override
        {
            rows.setVisible(form, visible);
            if (visible)
                rows.hideUnneeded(form, values.*member);
        }
        void hideRows(QFormLayout* form, const char* part) const override { rows.setRowsVisible(form, part, false); }
        void setMaximum(const char* key, int max) const override { rows.setMaximum(key, max); }
    };

    std::vector<std::unique_ptr<Base>> m_parts;
};

}  // namespace StripEdit

#endif // STRIPEDIT_FIELDROWS_HPP
