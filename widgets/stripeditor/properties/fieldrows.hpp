#ifndef STRIPEDIT_FIELDROWS_HPP
#define STRIPEDIT_FIELDROWS_HPP

#include "properties/propertygroupeditor.hpp"   // showSpin(), restoreSpin()

#include <QCoreApplication>
#include <QFormLayout>

#include <cstring>
#include <functional>
#include <type_traits>
#include <vector>

namespace StripEdit {

//! What a panel calls a field, and what its spin offers. Title and tip are QT_TRANSLATE_NOOP strings.
struct FieldRowSpec
{
    const char* key;
    const char* title;
    int         min, max;
    const char* suffix;
    const char* tip;
};

/**
 * @brief One spin per field of a group's field list, for the fields whose key starts with a prefix.
 *
 * The rows a parameterised style or shape shows: built by walking `Group::fields()`, so a field added to
 * the list gets its row with no hand-written widget, and labelled from a spec table the panel owns. A
 * field with the prefix and no spec stops a debug build.
 *
 * Each row keeps the panel contract on its own: *Mixed* while a bound selection disagrees about it
 * (showSpin()), and *touched* once the artist moves it, so applyEdited() writes only what was edited.
 * Header-only, as a template over the group; it holds no QObject of its own and connects through the
 * owning panel. It must not move once built: the connections hold a pointer to it.
 */
template <class Group>
class FieldRows
{
public:
    //! A spin for @p key, of a subclass the panel needs (e.g. one with an Auto value), or nullptr.
    using SpinFactory = std::function<QSpinBox*(const char* key)>;

    /**
     * @brief Adds the rows to @p form, in the field list's order.
     * @param owner      Parents the spins, and is the context of their connections.
     * @param prefix     Which fields get a row.
     * @param specs      Labels, ranges and tooltips, translated in @p context.
     * @param values     The panel's own copy of the group, written on every edit.
     * @param populating The panel's guard: while true, a spin's change is the panel showing a value.
     * @param edited     Called after a row's edit is in @p values: the panel emits changed() there.
     */
    template <size_t N>
    void build(QWidget* owner, QFormLayout* form, const char* prefix, const FieldRowSpec (&specs)[N],
               const char* context, Group& values, const bool& populating, std::function<void()> edited,
               const SpinFactory& makeSpin = {})
    {
        const size_t prefixLength = std::strlen(prefix);
        Group::fields([&](const char* key, auto member) {
            if (std::strncmp(key, prefix, prefixLength) != 0)
                return;
            const FieldRowSpec* spec = nullptr;
            for (const FieldRowSpec& s : specs)
                if (std::strcmp(s.key, key) == 0)
                    spec = &s;
            Q_ASSERT_X(spec, "FieldRows", "a field with the prefix and no row in the spec table");
            if (!spec)
                return;

            QSpinBox* spin = makeSpin ? makeSpin(key) : nullptr;
            if (!spin)
                spin = new QSpinBox(owner);
            spin->setRange(spec->min, spec->max);
            spin->setSuffix(QString::fromLatin1(spec->suffix));
            spin->setToolTip(QCoreApplication::translate(context, spec->tip));
            form->addRow(QCoreApplication::translate(context, spec->title), spin);

            using T = std::remove_reference_t<decltype(values.*member)>;
            Row row;
            row.key  = key;
            row.spin = spin;
            row.min  = spec->min;
            row.get  = [member](const Group& v) { return int(qRound(double(v.*member))); };
            row.set  = [member](Group& v, int x) { v.*member = T(x); };
            m_rows.push_back(row);

            const size_t index = m_rows.size() - 1;
            QObject::connect(spin, &QSpinBox::valueChanged, owner,
                             [this, index, &values, &populating, edited](int v) {
                                 if (populating)
                                     return;
                                 Row& r = m_rows[index];
                                 if (restoreSpin(r.spin, r.min))
                                     r.mixed = false;
                                 r.set(values, v);
                                 r.touched = true;
                                 edited();
                             });
        });
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
            r.mixed = r.mixed || r.get(subject) != r.get(shown);
    }

    //! Shows @p values, or *Mixed*. Call it inside the panel's populating guard.
    void show(const Group& values) const
    {
        for (const Row& r : m_rows)
            showSpin(r.spin, r.mixed, r.get(values), r.min, r.spin->maximum());
    }

    //! Lowers or raises @p key's ceiling, e.g. to what the bound object can show. Call it before show().
    void setMaximum(const char* key, int max) const
    {
        for (const Row& r : m_rows)
            if (std::strcmp(r.key, key) == 0 && r.spin->maximum() != max)
                r.spin->setMaximum(qMax(r.min, max));   // only on a change: setRange() stops a held arrow
    }

    void setVisible(QFormLayout* form, bool visible) const
    {
        for (const Row& r : m_rows)
            form->setRowVisible(r.spin, visible);
    }

    //! Copies into @p target the rows the artist has touched, from @p values.
    void applyEdited(const Group& values, Group& target) const
    {
        for (const Row& r : m_rows)
            if (r.touched)
                r.set(target, r.get(values));
    }

private:
    //! One field's spin, and how to reach the field. Values are whole numbers on screen.
    struct Row
    {
        const char*                      key  = nullptr;
        QSpinBox*                        spin = nullptr;
        int                              min  = 0;
        std::function<int(const Group&)>  get;
        std::function<void(Group&, int)>  set;
        bool                             mixed   = false;
        bool                             touched = false;
    };

    std::vector<Row> m_rows;
};

}  // namespace StripEdit

#endif // STRIPEDIT_FIELDROWS_HPP
