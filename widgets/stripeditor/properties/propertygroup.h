#ifndef STRIPEDIT_PROPERTYGROUP_H
#define STRIPEDIT_PROPERTYGROUP_H

#include "textartifact.h"

namespace StripEdit {

/**
 * @brief A named slice of an object's editable state.
 *
 * One editor is responsible for each, and only that editor writes it. `Tail` is a balloon's tails as a
 * collection; `TailItem` is one tail, the group of a selected tail object.
 *
 * **Append only, and nothing is removed.** The first three carry no editor and never have — they were
 * named before it was settled that placement, size and compositing belong to the object rather than to
 * its look. They stay because the value's *number* is persisted: which sections an artist leaves open
 * is remembered under `stripEditor/objectState/expanded/<int>`, so dropping a value would silently
 * shift every section after it onto somebody else's remembered state.
 */
enum class PropertyGroup { Placement, Size, Compositing, Shape, Skin, Style, Text, Tail, TailItem };

/**
 * @brief Whether @p a carries @p g at all — **the one answer to "which sections apply"**.
 *
 * A pure function of a record, which is what lets the rule be tested rather than only looked at. It was
 * written out twice in the panel, once for a single subject and once for a set, and the two had already
 * drifted: the set's copy enumerated the groups it wanted by name, so a group added to one copy was
 * simply missing from the other.
 *
 * The rule itself is §26's structural question and nothing more. **The lettering is the one group every
 * kind carries** — a balloon, a piece of standalone text and an imported picture are all lettered. The
 * rest need a silhouette: something to fill, something to roughen, somewhere for a tail to leave from,
 * and a choice of which silhouette it is. `TailItem` belongs to a *tail*, which is not a record, so no
 * record ever carries it.
 *
 * What a surface does with the answer is the surface's: ③ shows a set the **union** of what its objects
 * carry, while the object menu offers only the **intersection**, because an entry that acts on part of
 * a selection is an entry that lied about its subject.
 */
[[nodiscard]] inline bool carriesGroup(const TextArtifact& a, PropertyGroup g)
{
    switch (g) {
    case PropertyGroup::Text:
        return true;
    case PropertyGroup::Shape:
    case PropertyGroup::Skin:
    case PropertyGroup::Style:
    case PropertyGroup::Tail:
        return a.hasSilhouette();
    case PropertyGroup::TailItem:      // a tail's own group; a record never carries it
    case PropertyGroup::Placement:     // …and these three have no editor at all — see above
    case PropertyGroup::Size:
    case PropertyGroup::Compositing:
        break;
    }
    return false;
}

}  // namespace StripEdit

#endif // STRIPEDIT_PROPERTYGROUP_H
