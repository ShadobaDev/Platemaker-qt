// Where an overlay sits on the strip: scene points to the fractions a workspace stores, and back. Pure maths
// over a made-up chapter, so tested without a scene.

#include <gtest/gtest.h>

#include "objects/striplayout.hpp"

using StripEdit::StripLayout;

namespace {

constexpr qreal k_tolerance = 1e-9;   //!< In fractions and pixels alike: the maths is exact but for rounding.

//! Two pages 400 px wide: "one" 300 px tall, then "two" 500 px tall from y 300.
StripLayout chapter()
{
    std::vector<Platemaker::Core::PagePreviewGeometry> geometry(2);
    std::vector<Platemaker::Models::InputFile>         inputs(2);
    geometry[0].width  = 400;
    geometry[0].height = 300;
    geometry[1].width  = 400;
    geometry[1].height = 500;
    inputs[0].uid = "one";
    inputs[1].uid = "two";
    StripLayout layout;
    layout.build(geometry, inputs);
    return layout;
}

} // namespace

TEST(StripLayout, AnOverlayIsAnchoredToThePageUnderItAndReadsBackWhereItWas)
{
    const StripLayout layout = chapter();
    const auto        at     = layout.anchoredAt(QPointF(100, 340), 200);
    ASSERT_TRUE(at.has_value());
    EXPECT_EQ(at->anchor, "two");
    EXPECT_NEAR(at->xFrac, 0.25, k_tolerance);
    EXPECT_NEAR(at->yFrac, 0.1, k_tolerance);   // 40 px below page two's top
    EXPECT_NEAR(at->wFrac, 0.5, k_tolerance);

    Platemaker::Models::StripOverlay o;
    o.anchorInputUid = at->anchor.toStdString();
    o.xFrac          = at->xFrac;
    o.yFrac          = at->yFrac;
    o.wFrac          = at->wFrac;
    const QPointF back = layout.scenePosOf(o);
    EXPECT_NEAR(back.x(), 100, k_tolerance);
    EXPECT_NEAR(back.y(), 340, k_tolerance);
}

TEST(StripLayout, ThePageCanBeChosenByAnotherPointAndKeptWhenMoving)
{
    const StripLayout layout = chapter();
    // A balloon whose tail reaches up onto page one: the page is its box's, the offset its drawing's.
    const auto at = layout.anchoredAt(QPointF(0, 280), 100, 320.0);
    ASSERT_TRUE(at.has_value());
    EXPECT_EQ(at->anchor, "two");
    EXPECT_NEAR(at->yFrac, -0.05, k_tolerance);

    // Kept on its page while it moves; with no page of that uid, measured from the strip's top.
    EXPECT_NEAR(layout.anchoredOn("one", QPointF(0, 340), 100).yFrac, 0.85, k_tolerance);
    EXPECT_NEAR(layout.anchoredOn("gone", QPointF(0, 340), 100).yFrac, 0.85, k_tolerance);
    EXPECT_FALSE(StripLayout().anchoredAt(QPointF(0, 0), 10).has_value()) << "no layout, no anchor";
}

TEST(StripLayout, APicturesScaleAndPercentComeFromItsWidthFraction)
{
    const StripLayout                layout = chapter();
    Platemaker::Models::StripOverlay o;
    EXPECT_NEAR(layout.scaleOf(o, 50), 1.0, k_tolerance) << "no width recorded: its own size";
    o.wFrac = 0.5;   // 200 px
    EXPECT_NEAR(layout.scaleOf(o, 50), 4.0, k_tolerance);
    EXPECT_NEAR(layout.percentOf(o.wFrac, 50), 400.0, k_tolerance);
    EXPECT_NEAR(layout.percentOf(1.0, 50), 800.0, k_tolerance);   // the strip's width
    EXPECT_NEAR(layout.fraction(layout.pixels(0.3)), 0.3, k_tolerance);
}
