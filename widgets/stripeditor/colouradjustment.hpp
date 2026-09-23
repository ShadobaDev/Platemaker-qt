#ifndef STRIPEDIT_COLOURADJUSTMENT_HPP
#define STRIPEDIT_COLOURADJUSTMENT_HPP

#include <QList>
#include <QString>

#include <platemaker/models/colour_correction.hpp>

namespace StripEdit {

/**
 * @brief One colour adjustment, in an image editor's sense — a named part of the grade.
 *
 * The library holds one `ColourCorrection` with a fixed set of fields. An image editor's colour menu names the same
 * operations one at a time, and that is how an artist thinks of a grade: *I brightened it, then pulled the
 * saturation down*. This is the mapping between the two, in one place, because two panels speak it — the
 * Grade tool, which edits one adjustment at a time, and the strip's state, which lists what is applied.
 *
 * Declared in the order the library applies them, which is the order that decides the result.
 */
enum class ColourAdjustment { Curves, BrightnessContrast, Saturation };

//! Every adjustment, in the library's order.
[[nodiscard]] QList<ColourAdjustment> allColourAdjustments();

//! The name an image editor gives it — for a list entry, a heading or an undo step.
[[nodiscard]] QString colourAdjustmentName(ColourAdjustment a);

//! Whether it can be edited here. Curves are read by the library and have no editor yet: they can be listed
//! and removed, not opened.
[[nodiscard]] bool isColourAdjustmentEditable(ColourAdjustment a);

//! Whether @p cc applies it — any of its fields away from neutral. The per-adjustment half of `isNeutral()`.
[[nodiscard]] bool isColourAdjustmentApplied(const Platemaker::Models::ColourCorrection& cc, ColourAdjustment a);

//! Its values in @p cc as a short phrase — "brightness −0.54, contrast 1.56". Empty for curves.
[[nodiscard]] QString colourAdjustmentValues(const Platemaker::Models::ColourCorrection& cc, ColourAdjustment a);

/**
 * @brief @p cc with this adjustment's fields back at neutral, and nothing else touched.
 *
 * The other adjustments stay, and so do the page exclusions. Removing an adjustment and resetting it are the
 * same act: a neutral field is an adjustment that is not there.
 */
[[nodiscard]] Platemaker::Models::ColourCorrection withoutColourAdjustment(
    Platemaker::Models::ColourCorrection cc, ColourAdjustment a);

}  // namespace StripEdit

#endif // STRIPEDIT_COLOURADJUSTMENT_HPP
