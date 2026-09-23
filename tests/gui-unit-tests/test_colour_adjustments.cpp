/**
 * @file test_colour_adjustments.cpp
 * @brief The named adjustments are a partition of one `ColourCorrection`.
 *
 * Two panels rely on this mapping: the Grade tool edits one adjustment at a time, and the strip's state
 * lists what is applied and removes one on request. The contract worth pinning is the one *Remove* and
 * *Reset* stand on — taking an adjustment off touches its own fields and nothing else, not the other
 * adjustments and not the page exclusions — and that the per-adjustment rules add up to exactly the
 * library's `isNeutral()`, so the panels can never call a grade applied that the render calls neutral.
 */

#include <gtest/gtest.h>

#include "colouradjustment.hpp"

using Platemaker::Models::ColourCorrection;
using StripEdit::ColourAdjustment;
using StripEdit::allColourAdjustments;
using StripEdit::colourAdjustmentName;
using StripEdit::isColourAdjustmentApplied;
using StripEdit::withoutColourAdjustment;

namespace {

//! A grade with every adjustment applied and one page excluded.
ColourCorrection everythingApplied()
{
    ColourCorrection cc;
    cc.brightness        = -0.5;
    cc.contrast          = 1.5;
    cc.saturation        = 0.25;
    cc.curves.master     = {{0.0, 0.0}, {1.0, 0.5}};
    cc.excludedInputUids = {"file-title"};
    return cc;
}

[[nodiscard]] std::string nameOf(ColourAdjustment a)
{
    return colourAdjustmentName(a).toStdString();
}

}  // namespace

TEST(ColourAdjustments, NeutralAppliesNothing)
{
    const ColourCorrection neutral;
    for (ColourAdjustment a : allColourAdjustments())
        EXPECT_FALSE(isColourAdjustmentApplied(neutral, a)) << nameOf(a);
}

TEST(ColourAdjustments, EachFieldBelongsToExactlyOneAdjustment)
{
    ColourCorrection contrastOnly;
    contrastOnly.contrast = 1.2;
    EXPECT_TRUE(isColourAdjustmentApplied(contrastOnly, ColourAdjustment::BrightnessContrast));
    EXPECT_FALSE(isColourAdjustmentApplied(contrastOnly, ColourAdjustment::Saturation));
    EXPECT_FALSE(isColourAdjustmentApplied(contrastOnly, ColourAdjustment::Curves));

    ColourCorrection saturationOnly;
    saturationOnly.saturation = 0.0;   // greyscale is an adjustment, not an absence of one
    EXPECT_TRUE(isColourAdjustmentApplied(saturationOnly, ColourAdjustment::Saturation));
    EXPECT_FALSE(isColourAdjustmentApplied(saturationOnly, ColourAdjustment::BrightnessContrast));
}

TEST(ColourAdjustments, RemovingOneLeavesTheOthersAndTheExclusions)
{
    for (ColourAdjustment removed : allColourAdjustments()) {
        const ColourCorrection before = everythingApplied();
        const ColourCorrection after  = withoutColourAdjustment(before, removed);

        EXPECT_FALSE(isColourAdjustmentApplied(after, removed)) << nameOf(removed);
        for (ColourAdjustment other : allColourAdjustments()) {
            if (other != removed) {   // braced: EXPECT_TRUE is itself an if/else
                EXPECT_TRUE(isColourAdjustmentApplied(after, other))
                    << "removing " << nameOf(removed) << " also took off " << nameOf(other);
            }
        }
        EXPECT_EQ(after.excludedInputUids, before.excludedInputUids)
            << "removing " << nameOf(removed) << " touched the page exclusions";
    }
}

TEST(ColourAdjustments, AddUpToTheLibrarysNeutral)
{
    // Neither side may call a grade applied that the other calls neutral: the panels decide what to list
    // from these rules, and the render decides what to do from isNeutral().
    ColourCorrection cc = everythingApplied();
    EXPECT_FALSE(Platemaker::Models::isNeutral(cc));
    for (ColourAdjustment a : allColourAdjustments()) {
        cc = withoutColourAdjustment(cc, a);
        bool anyApplied = false;
        for (ColourAdjustment b : allColourAdjustments())
            anyApplied = anyApplied || isColourAdjustmentApplied(cc, b);
        EXPECT_EQ(Platemaker::Models::isNeutral(cc), !anyApplied) << "after removing " << nameOf(a);
    }
    EXPECT_TRUE(Platemaker::Models::isNeutral(cc));
    EXPECT_EQ(cc.excludedInputUids, everythingApplied().excludedInputUids);
}
