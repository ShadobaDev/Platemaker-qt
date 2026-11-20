// Lettering laid out in a font: its outlines (Geometry::textOutline()) and its fills (Geometry::textFills()).
// The font is the application's default, whatever the machine has, so each test asks what holds in any font.

#include <gtest/gtest.h>

#include "objectparts.hpp"
#include "geometry.hpp"

namespace {

//! Two lines of one I each, the second a tenth of a line under the first: over it, so the block's middle
//! lies in both.
Composition overlappingIs()
{
    Composition a         = textObject(QSize(300, 300));
    textOf(a).body        = QStringLiteral("I\nI");
    textOf(a).pixelSize   = 60;
    textOf(a).lineSpacing = 10;
    return a;
}

} // namespace

TEST(Lettering, OverlappingLettersPaintOnce)
{
    const Composition  a       = overlappingIs();
    const QPainterPath letters = Geometry::textOutline(a, *wordsOf(a));
    ASSERT_FALSE(letters.isEmpty());
    EXPECT_TRUE(letters.contains(letters.boundingRect().center())) << "the overlap is cut out";
}

TEST(Lettering, OverlappingLettersInARunPaintOnce)
{
    // A run sets its letters fragment by fragment, and a run's fill gathers them apart: both must paint the
    // overlap once, as the words' own letters do.
    Composition a = overlappingIs();
    TextRun     run;
    run.from      = 0;
    run.length    = int(textOf(a).body.size());
    run.pixelSize = 61;
    run.fill      = Paint{QColor(200, 0, 0), QColor(255, 255, 255)};
    textOf(a).runs = {run};

    const QPainterPath letters = Geometry::textOutline(a, *wordsOf(a));
    ASSERT_FALSE(letters.isEmpty());
    EXPECT_TRUE(letters.contains(letters.boundingRect().center())) << "the overlap is cut out of the outline";

    const Geometry::TextFills fills = Geometry::textFills(a, *wordsOf(a));
    ASSERT_TRUE(fills.contains(0));
    const QPainterPath& filled = fills.value(0);
    EXPECT_TRUE(filled.contains(filled.boundingRect().center())) << "the overlap is cut out of the run's fill";
}
