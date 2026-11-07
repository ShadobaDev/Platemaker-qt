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
    a.shape            = shape;
    a.text.paddingSide = padding;
    a.text.paddingTop  = padding;
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
    ObjectRecord big   = withShape(ObjectRecord::Shape::Cloud);
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

    // Each in its own direction: the side padding widens the box only, the top one (mirrored
    // at the bottom) heightens it only.
    ObjectRecord side       = withShape(ObjectRecord::Shape::Box, 0);
    side.text.paddingSide   = 20;
    ObjectRecord top        = withShape(ObjectRecord::Shape::Box, 0);
    top.text.paddingTop     = 20;
    const QSize  wide       = Geometry::fitBoxFor(side, block);
    const QSize  tall       = Geometry::fitBoxFor(top, block);
    EXPECT_EQ(wide.width(), roomy.width());
    EXPECT_EQ(wide.height(), tight.height());
    EXPECT_EQ(tall.width(), tight.width());
    EXPECT_EQ(tall.height(), roomy.height());
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

TEST(TextFit, FixPaddingLowersEachUntilTheLetteringFits)
{
    const QSizeF block(200, 60);

    // The box Fit gives at padding 0: at padding 8 the lettering overflows both ways, and 0 is the most
    // either can keep.
    ObjectRecord a = withShape(ObjectRecord::Shape::Box, 0);
    a.box          = Geometry::fitBoxFor(a, block);
    a.text.paddingSide = a.text.paddingTop = 8;
    ASSERT_FALSE(Geometry::fits(a, block));
    TextProperties fixed = Geometry::paddingsToFit(a, block);
    EXPECT_EQ(fixed.paddingSide, 0);
    EXPECT_EQ(fixed.paddingTop, 0);

    // Each in its own direction only: too wide lowers the side and leaves the top alone...
    ObjectRecord wide = a;
    wide.box.setHeight(a.box.height() + 100);
    fixed = Geometry::paddingsToFit(wide, block);
    EXPECT_EQ(fixed.paddingSide, 0);
    EXPECT_EQ(fixed.paddingTop, 8);
    // ...and too tall lowers the top and leaves the side alone.
    ObjectRecord tall = a;
    tall.box.setWidth(a.box.width() + 100);
    fixed = Geometry::paddingsToFit(tall, block);
    EXPECT_EQ(fixed.paddingSide, 8);
    EXPECT_EQ(fixed.paddingTop, 0);

    // A balloon smaller than its lettering: the paddings go below zero, past the outline, each to the
    // largest that holds.
    ObjectRecord burst = withShape(ObjectRecord::Shape::Box, -20);
    burst.box          = Geometry::fitBoxFor(burst, block);
    burst.text.paddingSide = burst.text.paddingTop = 8;
    fixed = Geometry::paddingsToFit(burst, block);
    EXPECT_EQ(fixed.paddingSide, -20);
    burst.text = fixed;
    EXPECT_TRUE(Geometry::fits(burst, block));
    ObjectRecord over = burst;
    ++over.text.paddingSide;
    EXPECT_FALSE(Geometry::fits(over, block));   // and it is the largest that does
    over = burst;
    ++over.text.paddingTop;
    EXPECT_FALSE(Geometry::fits(over, block));
}

TEST(TextFit, FixPaddingLeavesWhatAlreadyFitsAlone)
{
    ObjectRecord roomy = withShape(ObjectRecord::Shape::Ellipse, 8);
    roomy.box          = QSize(900, 600);
    EXPECT_EQ(Geometry::paddingsToFit(roomy, QSizeF(200, 36)), roomy.text);   // never raised to take up the room

    ObjectRecord text = withShape(ObjectRecord::Shape::None, 8);
    text.box          = QSize(50, 40);
    EXPECT_EQ(Geometry::paddingsToFit(text, QSizeF(200, 36)), text.text);     // no outline, no padding to give
}

/**
 * \brief The text area's arrows sit on its left, right and top edges, and dragging one moves
 *        that edge with the pointer: the side padding for either side, the top padding for the top.
 */
TEST(TextFit, TheTextAreaArrowsDragThePaddings)
{
    using Role         = Geometry::ShapeHandle::Role;
    ObjectRecord a     = withShape(ObjectRecord::Shape::Box);
    a.box              = QSize(300, 200);
    const QRectF area  = Geometry::textArea(a);
    const QList<Geometry::ShapeHandle> arrows = Geometry::textAreaHandles(a);
    ASSERT_EQ(arrows.size(), 3);
    EXPECT_EQ(arrows[0].role, Role::TextSide);
    EXPECT_EQ(arrows[0].at, QPointF(area.left(), area.center().y()));
    EXPECT_EQ(arrows[1].role, Role::TextSide);
    EXPECT_EQ(arrows[1].at, QPointF(area.right(), area.center().y()));
    EXPECT_EQ(arrows[2].role, Role::TextTop);
    EXPECT_EQ(arrows[2].at, QPointF(area.center().x(), area.top()));

    // Each edge follows the pointer; the other padding stays.
    TextProperties t = Geometry::dragTextAreaHandle(a, arrows[0], arrows[0].at + QPointF(10, 40));
    EXPECT_EQ(t.paddingSide, a.text.paddingSide + 10);
    EXPECT_EQ(t.paddingTop, a.text.paddingTop);
    t = Geometry::dragTextAreaHandle(a, arrows[1], arrows[1].at + QPointF(5, 0));   // out past the outline
    EXPECT_EQ(t.paddingSide, a.text.paddingSide - 5);
    t = Geometry::dragTextAreaHandle(a, arrows[2], arrows[2].at + QPointF(-30, 7));
    EXPECT_EQ(t.paddingTop, a.text.paddingTop + 7);
    EXPECT_EQ(t.paddingSide, a.text.paddingSide);

    // Dragged past the middle, an edge stops short of it: the area never turns inside out.
    ObjectRecord thin = a;
    thin.text         = Geometry::dragTextAreaHandle(a, arrows[0], QPointF(area.right() + 50, 0));
    EXPECT_GT(Geometry::textArea(thin).width(), 0.0);
    thin.text         = Geometry::dragTextAreaHandle(a, arrows[2], QPointF(0, area.bottom() + 50));
    EXPECT_GT(Geometry::textArea(thin).height(), 0.0);

    // No balloon, no paddings, no arrows.
    EXPECT_TRUE(Geometry::textAreaHandles(withShape(ObjectRecord::Shape::None)).isEmpty());
}

/**
 * \brief The letters' outline is drawn outside them, so the record's bounds grow by its width
 *        round the lettering, and not at all without one.
 */
TEST(TextFit, TheLettersOutlineGrowsTheBounds)
{
    ObjectRecord a = withShape(ObjectRecord::Shape::None);
    a.box          = QSize(100, 60);
    QPainterPath letters;                    // standing in for the glyphs, which need a font to lay out
    letters.addRect(QRectF(-20, 10, 140, 30));   // wider than the box, so the lettering sets the bounds
    const QRectF bare = Geometry::boundsOf(a, {}, letters);
    a.text.outlineWidth = 6.0;
    const QRectF outlined = Geometry::boundsOf(a, {}, letters);
    EXPECT_DOUBLE_EQ(outlined.left(), bare.left() - 6.0);
    EXPECT_DOUBLE_EQ(outlined.right(), bare.right() + 6.0);
    a.text.outlineWidth = 0.0;
    EXPECT_EQ(Geometry::boundsOf(a, {}, letters), bare);

    // Their shadow reaches past the outline: by its spread and its offset, on every side.
    a.text.outlineWidth = 6.0;
    a.text.shadow.on    = true;
    a.text.shadow.spread = 2.0;
    a.text.shadow.y      = 3.0;
    const QRectF shadowed = Geometry::boundsOf(a, {}, letters);
    EXPECT_DOUBLE_EQ(shadowed.left(), bare.left() - 6.0 - 2.0 - 3.0);
}

/**
 * \brief The letters' shadow is cast by them with their outline: grown by the outline's width and the
 *        spread together, then moved; nothing without a shadow or without words.
 */
TEST(TextFit, TheLettersShadowIsCastWithTheirOutline)
{
    ObjectRecord a = withShape(ObjectRecord::Shape::None);
    QPainterPath letters;
    letters.addRect(QRectF(10, 20, 100, 30));
    EXPECT_TRUE(Geometry::textShadowOf(a, letters).isEmpty());   // off by default

    a.text.shadow.on     = true;
    a.text.shadow.spread = 2.0;
    a.text.shadow.x      = 5.0;
    a.text.outlineWidth  = 3.0;
    const Geometry::ShadowPaths cast = Geometry::textShadowOf(a, letters);
    EXPECT_TRUE(cast.body == letters.translated(5.0, 0.0));   // moved right by 5...
    EXPECT_DOUBLE_EQ(cast.grow, 3.0 + 2.0);                    // ...and grown by the outline and the spread
    EXPECT_TRUE(Geometry::textShadowOf(a, QPainterPath()).isEmpty());
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
