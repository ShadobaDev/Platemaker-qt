/**
 * \file fields.hpp
 * \brief A property group's fields, listed once: the key each is saved under and the member it is.
 *
 * Every group in propertygroups.hpp has a `fields()` list. What a group is compared by (here), what it
 * is saved as in the undo snapshot's JSON (objectrecord.cpp) and in the overlay's `pm:` attributes
 * (recordsvg.cpp) are each written once, as a walk over that list — so a new property is **one line in
 * its group's list**, and it cannot be saved but not compared, or compared but not saved.
 *
 * A list is a static function taking a visitor, called with each field's key and a pointer to its
 * member: `v("fill", &SkinProperties::fill)`. The key is the persisted name, the same in the JSON and in
 * the SVG, and it may differ from the member's name (`fontFamily` for `TextProperties::family`): the list
 * is where that mapping is written down. **A key never changes** once a file carries it.
 *
 * What is not a group of fields stays written by hand: the box, the tails, the artwork's file name and
 * the style seed (ObjectRecord's own fields). A field may be a list (a Polygon's nodes), with one
 * overload of its own in each walk.
 */

#ifndef FIELDS_HPP
#define FIELDS_HPP

#include <QColor>
#include <QList>
#include <QPointF>
#include <QString>
#include <QtGlobal>

#include <array>
#include <type_traits>
#include <utility>

namespace Fields {

//! Whether two values of a field are the same, as the group's operator== means it.
inline bool same(int a, int b) { return a == b; }
inline bool same(bool a, bool b) { return a == b; }
//! +1 so an exact 0.0 compares equal: qFuzzyCompare() never calls 0 equal to anything.
inline bool same(qreal a, qreal b) { return qFuzzyCompare(1.0 + a, 1.0 + b); }
inline bool same(const QString& a, const QString& b) { return a == b; }
inline bool same(const QColor& a, const QColor& b) { return a == b; }
inline bool same(const QPointF& a, const QPointF& b) { return same(a.x(), b.x()) && same(a.y(), b.y()); }
//! A list of a type of the groups' own (a node), by its operator==, which compares numbers as same() does.
template <class T>
bool same(const QList<T>& a, const QList<T>& b)
{
    return a == b;
}
template <class E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
bool same(E a, E b)
{
    return a == b;
}

/**
 * \brief A field inside a field: member @p inner of the struct a group holds as @p outer (a Paint).
 *
 * C++ cannot make one pointer-to-member of the two, so a group's `fields()` hands this to the visitor
 * where a sub-group's field is, under a key of the group's own (`fillKind` for the fill's Paint's kind).
 * Every walk reaches a field through at(), which takes either.
 */
template <class Group, class Sub, class T>
struct Nested
{
    Sub Group::* outer;
    T Sub::*     inner;
};

//! The field @p member of @p g: a member of the group, or a member of one of its sub-groups.
template <class Group, class T>
T& at(Group& g, T Group::* member) { return g.*member; }
template <class Group, class T>
const T& at(const Group& g, T Group::* member) { return g.*member; }
template <class Group, class Sub, class T>
T& at(Group& g, Nested<Group, Sub, T> n) { return (g.*n.outer).*n.inner; }
template <class Group, class Sub, class T>
const T& at(const Group& g, Nested<Group, Sub, T> n) { return (g.*n.outer).*n.inner; }

//! Whether @p M names a sub-group's field (Nested) rather than the group's own.
template <class M>
inline constexpr bool isNested = false;
template <class Group, class Sub, class T>
inline constexpr bool isNested<Nested<Group, Sub, T>> = true;

/**
 * \brief The fields of the section @p outer of a group, each under the section's own key: what the group's
 *        fields() lists for it, so its files keep their flat keys.
 *
 * A **section** is the numbers one kind reads, or several kinds share (a line's, an edge's): a struct with a
 * fields() list of its own, held by its group as a member, and described for a panel by its section().
 */
template <class V, class Group, class Sub>
void nest(V&& v, Sub Group::* outer)
{
    Sub::fields([&](const char* key, auto inner) {
        using T = std::remove_reference_t<decltype(std::declval<Sub&>().*inner)>;
        v(key, Nested<Group, Sub, T>{outer, inner});
    });
}

//! The context a RowSpec's and a Section's strings are translated in.
constexpr char k_trContext[] = "QObject";

/**
 * \brief What a panel calls a field, and what it offers: a spin's range, or for a choice (a bool) its two
 *        entries. Title, tip and entries are QT_TRANSLATE_NOOP strings in the k_trContext context.
 */
struct RowSpec
{
    const char* key;
    const char* title;
    double      min, max;
    const char* suffix;
    const char* tip;
    //! Places after the point; 0 shows whole numbers (a length in whole pixels), more a decimal spin.
    int         decimals = 0;
    //! What an arrow adds, for a decimal spin; 0 is one unit of the last place. A number whose effect is
    //! in proportion to it wants steps that show at its usual size, and typing keeps every place.
    double      step = 0.0;
    //! A bool's two entries, true first: it is a combo. A bool without them is a check box, `title` its text.
    const char* on  = nullptr;
    const char* off = nullptr;
};

//! A section as a panel shows it: its name (what a kind's table entry lists), a heading, and its rows.
struct Section
{
    const char*    name;
    //! A heading, shown where a kind shows more than one section; nullptr for none (a family's own numbers).
    const char*    title;
    const RowSpec* rows;
    int            rowCount;
};

//! @p rows as a section named @p name, under @p title.
template <size_t N>
constexpr Section sectionOf(const char* name, const char* title, const RowSpec (&rows)[N])
{
    return {name, title, rows, int(N)};
}

//! The sections a kind reads, by Section::name, as its table entry lists them; unused places nullptr.
using SectionNames = std::array<const char*, 2>;

//! Every field of \p a equal to the same field of \p b — a group's operator==.
template <class Group>
bool equal(const Group& a, const Group& b)
{
    bool all = true;
    Group::fields([&](const char*, auto member) { all = all && same(at(a, member), at(b, member)); });
    return all;
}

}  // namespace Fields

#endif // FIELDS_HPP
