// Fitting a box to its lettering: the geometry half, which needs no font. The block of lettering is
// given as a size — what Geometry::textBlockSize() would measure — so every shape can be tried without
// the QGuiApplication this target deliberately does not have.

#include <gtest/gtest.h>

#include "objectparts.hpp"
#include "names.hpp"
#include "geometry.hpp"
#include "shapes.hpp"

namespace {

//! The grips' own minimum, which a fit does not go below either.
constexpr int k_floor = 40;

//! A balloon of @p shape, or a text object for `None`.
Composition withShape(const ShapeProperties& shape)
{
    const Composition a = textObject(QSize(500, 500));   // a fit's starting point must not matter
    return shape.kind == ShapeProperties::Kind::None ? a : withBalloon(a, {shape, {}});
}

Composition withShape(ShapeProperties::Kind kind)
{
    ShapeProperties shape;
    shape.kind = kind;
    return withShape(shape);
}

//! Distances and positions in balloon pixels: exact but for rounding.
constexpr qreal k_px = 1e-9;
//! A frame dragged by its middle moves by whole pixels, so it lands within half of one of the pointer.
constexpr qreal k_wholePx = 0.5;

//! Every outline the picker offers, by its template's id, and no balloon at all.
QList<QPair<const char*, ShapeProperties>> everyShape(bool withNone = true)
{
    QList<QPair<const char*, ShapeProperties>> out;
    if (withNone) {
        ShapeProperties none;
        none.kind = ShapeProperties::Kind::None;
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
            Composition a = withShape(shape);
            boxOf(a)          = Geometry::fitBoxFor(a, block);
            EXPECT_TRUE(Geometry::fits(a, block)) << id << " " << block.width() << "x" << block.height();
            EXPECT_TRUE(Geometry::liesInOutline(a, block)) << id << " " << block.width() << "x" << block.height();
        }
    }
}

TEST(TextFit, TheFitIsTight)
{
    // One pixel less on both sides and the block no longer fits its frame or crosses the outline — unless a side
    // is at the floor.
    for (const auto& [id, shape] : everyShape()) {
        for (const QSizeF& block : k_blocks) {
            Composition a = withShape(shape);
            const QSize fit = Geometry::fitBoxFor(a, block);
            if (fit.width() <= k_floor || fit.height() <= k_floor)
                continue;
            boxOf(a) = QSize(fit.width() - 1, fit.height() - 1);
            EXPECT_FALSE(Geometry::fits(a, block) && Geometry::liesInOutline(a, block))
                << id << " " << block.width() << "x" << block.height() << " fit " << fit.width() << "x" << fit.height();
        }
    }
}

/**
 * \brief The frame alone is not the fit where the outline is curved, or where set edges reach past it: the box
 *        grows until the block lies inside the outline itself.
 */
TEST(TextFit, TheFitKeepsTheBlockInsideTheOutline)
{
    const QSizeF block(200, 36);

    // An ellipse's box holds the block in its frame long before its curve does.
    Composition ellipse = withShape(ShapeProperties::Kind::Ellipse);
    boxOf(ellipse)      = Geometry::fitBoxFor(ellipse, block);
    EXPECT_TRUE(Geometry::liesInOutline(ellipse, block));
    EXPECT_GT(boxOf(ellipse).width(), block.width() * 1.2) << "the corners of the block reach the curve";

    // Edges set past the outline (the arrows allow it): the frame holds the block at a size the outline does not,
    // so the fit grows the balloon round the words.
    Composition past           = withShape(ShapeProperties::Kind::Box);
    wordsOf(past)->frame.left  = -49;
    wordsOf(past)->frame.right = -49;
    boxOf(past)                = Geometry::fitBoxFor(past, block);
    EXPECT_TRUE(Geometry::fits(past, block));
    EXPECT_TRUE(Geometry::liesInOutline(past, block));

    // No outline, nothing to lie in: a text object's box is the block's.
    EXPECT_TRUE(Geometry::liesInOutline(withShape(ShapeProperties::Kind::None), block));
}

TEST(TextFit, TheStartingBoxDoesNotMatter)
{
    Composition big   = withShape(ShapeProperties::Kind::Cloud);
    Composition small = big;
    boxOf(big)   = QSize(900, 50);
    boxOf(small) = QSize(41, 700);
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

TEST(TextFit, AnAutomaticEdgeReachesTheBoxLessTheStroke)
{
    Composition a = withShape(ShapeProperties::Kind::Box);
    boxOf(a)      = QSize(300, 200);
    const qreal  in   = styleOf(a).strokeWidth / 2.0;
    const QRectF area = Geometry::textArea(a, *wordsOf(a));
    const QRectF box  = Geometry::balloonRect(*a.balloon, *bodyOf(a)).adjusted(in, in, -in, -in);
    EXPECT_NEAR(area.left(), box.left(), k_px);
    EXPECT_NEAR(area.top(), box.top(), k_px);
    EXPECT_NEAR(area.right(), box.right(), k_px);
    EXPECT_NEAR(area.bottom(), box.bottom(), k_px);

    // The block is centred in its frame by default.
    const QRectF block = Geometry::blockRect(a, *wordsOf(a), QSizeF(100, 40));
    EXPECT_NEAR(block.center().x(), box.center().x(), k_px);
    EXPECT_NEAR(block.center().y(), box.center().y(), k_px);
}

TEST(TextFit, ASetEdgeIsItsInsetFromTheBodyAndKeepsItWhenTheBodyGrows)
{
    Composition a = withShape(ShapeProperties::Kind::Box);
    boxOf(a)      = QSize(300, 200);
    Words& w      = *wordsOf(a);
    w.frame.left  = 30;
    w.frame.top   = 10;
    w.frame.right = -15;   // past the outline
    const qreal  in   = styleOf(a).strokeWidth / 2.0;
    const QRectF box  = Geometry::balloonRect(*a.balloon, *bodyOf(a)).adjusted(in, in, -in, -in);
    const QRectF area = Geometry::textArea(a, w);
    EXPECT_NEAR(area.left(), box.left() + 30, k_px);
    EXPECT_NEAR(area.top(), box.top() + 10, k_px);
    EXPECT_NEAR(area.right(), box.right() + 15, k_px);
    EXPECT_NEAR(area.bottom(), box.bottom(), k_px) << "the bottom edge is automatic";

    // A block wider than the frame does not fit; the fit grows the body until the frame, its insets kept, holds it.
    const QSizeF wide(400, 10);
    EXPECT_FALSE(Geometry::fits(a, wide));
    Composition fitted = a;
    boxOf(fitted)      = Geometry::fitBoxFor(a, wide);
    EXPECT_TRUE(Geometry::fits(fitted, wide));

    // A wider body keeps the frame's distances from its edges.
    boxOf(a)           = QSize(600, 200);
    const QRectF wider = Geometry::balloonRect(*a.balloon, *bodyOf(a)).adjusted(in, in, -in, -in);
    EXPECT_NEAR(Geometry::textArea(a, *wordsOf(a)).left(), wider.left() + 30, k_px);
    EXPECT_NEAR(Geometry::textArea(a, *wordsOf(a)).right(), wider.right() + 15, k_px);
}

TEST(TextFit, AWordsBoxIsItsFrameWithoutABalloon)
{
    // A text object's text area is its box: the fit is the block itself, rounded up, above the floor.
    EXPECT_EQ(Geometry::fitBoxFor(withShape(ShapeProperties::Kind::None), QSizeF(200, 36)), QSize(200, k_floor));
}

TEST(TextFit, NoTextKeepsTheBox)
{
    const Composition a = withShape(ShapeProperties::Kind::Ellipse);
    EXPECT_EQ(Geometry::fitBoxFor(a, QSizeF()), boxOf(a));
    EXPECT_TRUE(Geometry::fits(a, QSizeF()));
}

/**
 * \brief The frame's middle comes first, so a press where an arrow lies over it takes the middle. The arrows sit on
 *        the frame's left and right edges, level with it, and on its top and bottom, above and below it; dragging
 *        one sets that edge where the pointer is, and the middle follows. Dragging the middle moves the frame.
 */
TEST(TextFit, TheTextAreaArrowsSetTheirEdges)
{
    using Geometry::TextAreaPart;
    Composition a = withShape(ShapeProperties::Kind::Box);
    boxOf(a)      = QSize(300, 200);
    const QRectF area = Geometry::textArea(a, *wordsOf(a));
    const QList<Geometry::Handle> arrows = Geometry::textAreaHandles(a);
    ASSERT_EQ(arrows.size(), 5);
    EXPECT_EQ(Geometry::partOf<TextAreaPart>(arrows[0]), TextAreaPart::Anchor);
    const QPointF at = arrows[0].at;
    EXPECT_NEAR(at.x(), area.center().x(), k_px) << "the middle of the body by default";
    EXPECT_NEAR(at.y(), area.center().y(), k_px);
    EXPECT_EQ(Geometry::partOf<TextAreaPart>(arrows[1]), TextAreaPart::Side);
    EXPECT_EQ(arrows[1].at, QPointF(area.left(), at.y()));
    EXPECT_EQ(Geometry::partOf<TextAreaPart>(arrows[2]), TextAreaPart::Side);
    EXPECT_EQ(arrows[2].at, QPointF(area.right(), at.y()));
    EXPECT_EQ(Geometry::partOf<TextAreaPart>(arrows[3]), TextAreaPart::Top);
    EXPECT_EQ(arrows[3].at, QPointF(at.x(), area.top()));
    EXPECT_EQ(Geometry::partOf<TextAreaPart>(arrows[4]), TextAreaPart::Bottom);
    EXPECT_EQ(arrows[4].at, QPointF(at.x(), area.bottom()));

    // Each sets its own edge where the pointer is, in whole pixels in from the body's box (every edge is automatic
    // here, so that box is the frame), inside the balloon or past it; the others stay automatic.
    const QRectF box = area;
    Words w = Geometry::dragTextAreaHandle(a, arrows[1], QPointF(box.left() + 40.3, 0));
    ASSERT_TRUE(w.frame.left.has_value());
    EXPECT_NEAR(*w.frame.left, 40.0, k_px);
    EXPECT_FALSE(w.frame.right.has_value());
    w = Geometry::dragTextAreaHandle(a, arrows[2], QPointF(box.right() + 30, 0));   // out past the outline
    EXPECT_NEAR(*w.frame.right, -30.0, k_px);
    w = Geometry::dragTextAreaHandle(a, arrows[3], QPointF(0, box.top() + 25.4));
    EXPECT_NEAR(*w.frame.top, 25.0, k_px);
    w = Geometry::dragTextAreaHandle(a, arrows[4], QPointF(0, box.bottom() - 12));
    EXPECT_NEAR(*w.frame.bottom, 12.0, k_px);

    // The middle follows the edges: it is the frame's middle whatever was dragged.
    Composition moved = a;
    *wordsOf(moved)   = w;
    EXPECT_EQ(Geometry::textAreaHandles(moved)[0].at, Geometry::textArea(moved, w).center());

    // The middle moves the whole frame, by whole pixels, its size kept; never past the body.
    w = Geometry::dragTextAreaHandle(a, arrows[0], at + QPointF(-50, 30));
    EXPECT_NEAR(*w.frame.left, -50.0, k_px);
    EXPECT_NEAR(*w.frame.right, 50.0, k_px);
    EXPECT_NEAR(*w.frame.top, 30.0, k_px);
    EXPECT_NEAR(*w.frame.bottom, -30.0, k_px);
    w = Geometry::dragTextAreaHandle(a, arrows[0], box.bottomRight() + QPointF(500, 500));
    *wordsOf(moved) = w;
    EXPECT_NEAR(Geometry::textArea(moved, w).center().x(), box.right(), k_wholePx);
    EXPECT_NEAR(Geometry::textArea(moved, w).center().y(), box.bottom(), k_wholePx);
    w = Geometry::dragTextAreaHandle(a, arrows[0], at + QPointF(0, 30));
    EXPECT_FALSE(w.frame.left.has_value()) << "an axis it did not move along keeps its edges";

    // Dragged past the opposite edge, an edge stops two pixels short of it: the area never turns inside out.
    w = Geometry::dragTextAreaHandle(a, arrows[1], QPointF(box.right() + 50, 0));
    *wordsOf(moved) = w;
    EXPECT_NEAR(Geometry::textArea(moved, w).width(), 2.0, k_wholePx);

    // No balloon: the words keep their own box, and offer no arrows.
    EXPECT_TRUE(Geometry::textAreaHandles(withShape(ShapeProperties::Kind::None)).isEmpty());
}

/**
 * \brief The letters' outline is drawn outside them, so the composition's bounds grow by its width
 *        round the lettering, and not at all without one.
 */
TEST(TextFit, TheLettersOutlineGrowsTheBounds)
{
    Composition a = withShape(ShapeProperties::Kind::None);
    boxOf(a)          = QSize(100, 60);
    QPainterPath letters;                    // standing in for the glyphs, which need a font to lay out
    letters.addRect(QRectF(-20, 10, 140, 30));   // wider than the box, so the lettering sets the bounds
    const QRectF bare = Geometry::boundsOf(a, {}, letters);
    textOf(a).outlineWidth = 6.0;
    const QRectF outlined = Geometry::boundsOf(a, {}, letters);
    EXPECT_DOUBLE_EQ(outlined.left(), bare.left() - 6.0);
    EXPECT_DOUBLE_EQ(outlined.right(), bare.right() + 6.0);
    textOf(a).outlineWidth = 0.0;
    EXPECT_EQ(Geometry::boundsOf(a, {}, letters), bare);

    // Their shadow reaches past the outline: by its spread and its offset, on every side.
    textOf(a).outlineWidth = 6.0;
    textOf(a).shadow.on    = true;
    textOf(a).shadow.spread = 2.0;
    textOf(a).shadow.y      = 3.0;
    const QRectF shadowed = Geometry::boundsOf(a, {}, letters);
    EXPECT_DOUBLE_EQ(shadowed.left(), bare.left() - 6.0 - 2.0 - 3.0);
}

/**
 * \brief The letters' shadow is cast by them with their outline: grown by the outline's width and the
 *        spread together, then moved; nothing without a shadow or without words.
 */
TEST(TextFit, TheLettersShadowIsCastWithTheirOutline)
{
    Composition a = withShape(ShapeProperties::Kind::None);
    QPainterPath letters;
    letters.addRect(QRectF(10, 20, 100, 30));
    EXPECT_TRUE(Geometry::textShadowOf(*wordsOf(a), letters).isEmpty());   // off by default

    textOf(a).shadow.on     = true;
    textOf(a).shadow.spread = 2.0;
    textOf(a).shadow.x      = 5.0;
    textOf(a).outlineWidth  = 3.0;
    const Geometry::ShadowPaths cast = Geometry::textShadowOf(*wordsOf(a), letters);
    EXPECT_TRUE(cast.body == letters.translated(5.0, 0.0));   // moved right by 5...
    EXPECT_DOUBLE_EQ(cast.grow, 3.0 + 2.0);                    // ...and grown by the outline and the spread
    EXPECT_TRUE(Geometry::textShadowOf(*wordsOf(a), QPainterPath()).isEmpty());
}

/**
 * \brief The block is aligned in its frame on both axes, and one larger than the frame goes past it away from
 *        the side it is aligned to; the point that stays put when the box changes is the aligned one.
 */
TEST(TextFit, TheBlockIsAlignedInItsFrameOnBothAxes)
{
    Composition a = withShape(ShapeProperties::Kind::None);
    boxOf(a)      = QSize(300, 100);
    const QSizeF blk(120, 40);
    const auto   placed = [&](int h, int v, const QSizeF& size) {
        textOf(a).align  = h;
        textOf(a).valign = v;
        return Geometry::blockRect(a, *wordsOf(a), size);
    };

    EXPECT_EQ(placed(Qt::AlignHCenter, Qt::AlignVCenter, blk), QRectF(90, 30, 120, 40));
    EXPECT_EQ(Geometry::textAnchor(a), QPointF(150, 50));
    EXPECT_EQ(placed(Qt::AlignLeft, Qt::AlignTop, blk), QRectF(0, 0, 120, 40));
    EXPECT_EQ(Geometry::textAnchor(a), QPointF(0, 0));
    EXPECT_EQ(placed(Qt::AlignRight, Qt::AlignBottom, blk), QRectF(180, 60, 120, 40));
    EXPECT_EQ(Geometry::textAnchor(a), QPointF(300, 100));

    // Taller than the frame: down from a top, up from a bottom, both ways from the middle.
    const QSizeF tall(120, 160);
    EXPECT_NEAR(placed(Qt::AlignLeft, Qt::AlignTop, tall).top(), 0.0, k_px);
    EXPECT_NEAR(placed(Qt::AlignLeft, Qt::AlignBottom, tall).bottom(), 100.0, k_px);
    EXPECT_NEAR(placed(Qt::AlignLeft, Qt::AlignVCenter, tall).center().y(), 50.0, k_px);
}

TEST(TextFit, ConvertingFitsTheLetteringInEveryShape)
{
    // The TODO this closes: a text object's box is its words' own size, and converted to a balloon
    // without a new box the smaller text area clipped them. Convert fits the box to the new kind.
    const QSizeF block(180, 72);
    Composition text = withShape(ShapeProperties::Kind::None);
    boxOf(text)          = Geometry::fitBoxFor(text, block);
    for (const auto& [id, shape] : everyShape(/*withNone=*/false)) {
        Composition balloon = withBalloon(text, {shape, {}});
        EXPECT_FALSE(Geometry::fits(balloon, block)) << id << " (the old behaviour clipped)";
        boxOf(balloon) = Geometry::fitBoxFor(balloon, block);
        EXPECT_TRUE(Geometry::fits(balloon, block)) << id;
    }
}
