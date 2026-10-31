// Fitting a box to its lettering: the geometry half, which needs no font. The block of lettering is
// given as a size — what Geometry::textBlockSize() would measure — so every shape can be tried without
// the QGuiApplication this target deliberately does not have.

#include <gtest/gtest.h>

#include "objectrecord.hpp"
#include "geometry.hpp"
#include "shapes.hpp"

namespace {

//! The grips' own minimum, which a fit does not go below either.
constexpr int k_floor = 40;

ObjectRecord withShape(const ShapeProperties& shape, int padding = 8)
{
    ObjectRecord a;
    a.shape        = shape;
    a.text.padding = padding;
    a.box          = QSize(500, 500);   // a fit's starting point must not matter
    return a;
}

ObjectRecord withShape(ObjectRecord::Shape kind, int padding = 8)
{
    ShapeProperties shape;
    shape.kind = kind;
    return withShape(shape, padding);
}

//! Every outline the picker offers, by its template's id, and no balloon at all.
QList<QPair<const char*, ShapeProperties>> everyShape(bool withNone = true)
{
    QList<QPair<const char*, ShapeProperties>> out;
    if (withNone) {
        ShapeProperties none;
        none.kind = ObjectRecord::Shape::None;
        out.append({"none", none});
    }
    for (const Geometry::ShapeTemplate& t : Geometry::shapeTemplates())
        out.append({t.id, t.shape});
    return out;
}

//! Blocks of lettering: one wide line, a tall narrow stack, a single short word.
const QList<QSizeF> k_blocks{QSizeF(200, 36), QSizeF(80, 150), QSizeF(12, 20)};

} // namespace

TEST(TextFit, TheFittedBoxHoldsTheBlockInEveryShape)
{
    for (const auto& [id, shape] : everyShape()) {
        for (const QSizeF& block : k_blocks) {
            ObjectRecord a = withShape(shape);
            a.box          = Geometry::fitBoxFor(a, block);
            EXPECT_TRUE(Geometry::fits(a, block)) << id << " " << block.width() << "x" << block.height();
        }
    }
}

TEST(TextFit, TheFitIsTightOnBothAxes)
{
    // One pixel less on either side and the block no longer fits — unless that side is at the floor.
    for (const auto& [id, shape] : everyShape()) {
        for (const QSizeF& block : k_blocks) {
            ObjectRecord a = withShape(shape);
            const QSize  fit = Geometry::fitBoxFor(a, block);

            ObjectRecord narrower = a;
            narrower.box = QSize(fit.width() - 1, fit.height());
            if (fit.width() > k_floor)
                EXPECT_FALSE(Geometry::fits(narrower, block)) << id << " width";

            ObjectRecord shorter = a;
            shorter.box = QSize(fit.width(), fit.height() - 1);
            if (fit.height() > k_floor)
                EXPECT_FALSE(Geometry::fits(shorter, block)) << id << " height";
        }
    }
}

TEST(TextFit, TheStartingBoxDoesNotMatter)
{
    ObjectRecord big   = withShape(ObjectRecord::Shape::Thought);
    ObjectRecord small = big;
    big.box   = QSize(900, 50);
    small.box = QSize(41, 700);
    EXPECT_EQ(Geometry::fitBoxFor(big, QSizeF(200, 36)), Geometry::fitBoxFor(small, QSizeF(200, 36)));
}

TEST(TextFit, NeitherSideGoesBelowTheFloor)
{
    for (const auto& [id, shape] : everyShape()) {
        const QSize fit = Geometry::fitBoxFor(withShape(shape), QSizeF(3, 3));
        EXPECT_GE(fit.width(), k_floor) << id;
        EXPECT_GE(fit.height(), k_floor) << id;
    }
}

TEST(TextFit, PaddingGrowsTheBoxAndANegativeOneShrinksIt)
{
    const QSizeF block(200, 60);
    const QSize  tight = Geometry::fitBoxFor(withShape(ObjectRecord::Shape::Box, 0), block);
    const QSize  roomy = Geometry::fitBoxFor(withShape(ObjectRecord::Shape::Box, 20), block);
    const QSize  burst = Geometry::fitBoxFor(withShape(ObjectRecord::Shape::Box, -20), block);
    EXPECT_GT(roomy.width(), tight.width());
    EXPECT_GT(roomy.height(), tight.height());
    EXPECT_LT(burst.width(), tight.width());
    // Negative enough, the lettering is wider than its own balloon — an impact balloon's look.
    EXPECT_LT(burst.width(), block.width());
}

TEST(TextFit, PaddingDoesNothingWithoutABalloon)
{
    const QSizeF block(200, 36);
    EXPECT_EQ(Geometry::fitBoxFor(withShape(ObjectRecord::Shape::None, 0), block),
              Geometry::fitBoxFor(withShape(ObjectRecord::Shape::None, 50), block));
    // A text object's text area is its box: the fit is the block itself, rounded up, above the floor.
    EXPECT_EQ(Geometry::fitBoxFor(withShape(ObjectRecord::Shape::None), block), QSize(200, k_floor));
}

TEST(TextFit, NoTextKeepsTheBox)
{
    const ObjectRecord a = withShape(ObjectRecord::Shape::Ellipse);
    EXPECT_EQ(Geometry::fitBoxFor(a, QSizeF()), a.box);
    EXPECT_TRUE(Geometry::fits(a, QSizeF()));
}

TEST(TextFit, FixPaddingLowersItUntilTheLetteringFits)
{
    const QSizeF block(200, 60);

    // The box Fit gives at padding 0: at padding 8 the lettering overflows, and 0 is the most it can keep.
    ObjectRecord a = withShape(ObjectRecord::Shape::Box, 0);
    a.box          = Geometry::fitBoxFor(a, block);
    a.text.padding = 8;
    ASSERT_FALSE(Geometry::fits(a, block));
    EXPECT_EQ(Geometry::paddingToFit(a, block), 0);

    // A balloon smaller than its lettering: the padding goes below zero, past the outline.
    ObjectRecord burst = withShape(ObjectRecord::Shape::Box, -20);
    burst.box          = Geometry::fitBoxFor(burst, block);
    burst.text.padding = 8;
    const int fixed    = Geometry::paddingToFit(burst, block);
    EXPECT_EQ(fixed, -20);
    burst.text.padding = fixed;
    EXPECT_TRUE(Geometry::fits(burst, block));
    burst.text.padding = fixed + 1;
    EXPECT_FALSE(Geometry::fits(burst, block));   // and it is the largest that does
}

TEST(TextFit, FixPaddingLeavesWhatAlreadyFitsAlone)
{
    ObjectRecord roomy = withShape(ObjectRecord::Shape::Ellipse, 8);
    roomy.box          = QSize(900, 600);
    EXPECT_EQ(Geometry::paddingToFit(roomy, QSizeF(200, 36)), 8);   // never raised to take up the room

    ObjectRecord text = withShape(ObjectRecord::Shape::None, 8);
    text.box          = QSize(50, 40);
    EXPECT_EQ(Geometry::paddingToFit(text, QSizeF(200, 36)), 8);    // no outline, no padding to give
}

TEST(TextFit, TheAnchorIsTheAlignedSideAndTheMiddleOfTheBlock)
{
    ObjectRecord a   = withShape(ObjectRecord::Shape::None);
    a.box            = QSize(300, 100);
    const QSizeF blk(120, 40);

    a.text.align = Qt::AlignHCenter;
    EXPECT_EQ(Geometry::textAnchorFor(a, blk), QPointF(150, 50));
    a.text.align = Qt::AlignLeft;
    EXPECT_EQ(Geometry::textAnchorFor(a, blk), QPointF(0, 50));
    a.text.align = Qt::AlignRight;
    EXPECT_EQ(Geometry::textAnchorFor(a, blk), QPointF(300, 50));

    // Taller than its area, the block is pinned to the top, as textOutline() draws it.
    EXPECT_EQ(Geometry::textAnchorFor(a, QSizeF(120, 160)).y(), 80);
}

TEST(TextFit, ConvertingFitsTheLetteringInEveryShape)
{
    // The TODO this closes: a text object's box is its words' own size, and converted to a balloon
    // without a new box the smaller text area clipped them. Convert fits the box to the new kind.
    const QSizeF block(180, 72);
    ObjectRecord text = withShape(ObjectRecord::Shape::None);
    text.box          = Geometry::fitBoxFor(text, block);
    for (const auto& [id, shape] : everyShape(/*withNone=*/false)) {
        ObjectRecord balloon = text;
        balloon.shape        = shape;
        EXPECT_FALSE(Geometry::fits(balloon, block)) << id << " (the old behaviour clipped)";
        balloon.box = Geometry::fitBoxFor(balloon, block);
        EXPECT_TRUE(Geometry::fits(balloon, block)) << id;
    }
}
