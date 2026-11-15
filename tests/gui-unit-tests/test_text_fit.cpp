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
        }
    }
}

TEST(TextFit, TheFitIsTightOnBothAxes)
{
    // One pixel less on either side and the block no longer fits — unless that side is at the floor.
    for (const auto& [id, shape] : everyShape()) {
        for (const QSizeF& block : k_blocks) {
            Composition a = withShape(shape);
            const QSize  fit = Geometry::fitBoxFor(a, block);

            Composition narrower = a;
            boxOf(narrower) = QSize(fit.width() - 1, fit.height());
            if (fit.width() > k_floor)
                EXPECT_FALSE(Geometry::fits(narrower, block)) << id << " width";

            Composition shorter = a;
            boxOf(shorter) = QSize(fit.width(), fit.height() - 1);
            if (fit.height() > k_floor)
                EXPECT_FALSE(Geometry::fits(shorter, block)) << id << " height";
        }
    }
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

    // The block is centred on the anchor, the middle of that box by default.
    const QRectF block = Geometry::blockRect(a, *wordsOf(a), QSizeF(100, 40));
    EXPECT_NEAR(block.center().x(), box.center().x(), k_px);
    EXPECT_NEAR(block.center().y(), box.center().y(), k_px);
}

TEST(TextFit, ASetEdgeIsItsDistanceFromTheAnchorAndTheAnchorKeepsItsPlace)
{
    Composition a = withShape(ShapeProperties::Kind::Box);
    boxOf(a)      = QSize(300, 200);
    Words& w      = *wordsOf(a);
    w.anchor      = QPointF(0.25, 0.5);
    w.frame.left  = 30;
    w.frame.top   = 10;
    const qreal   in    = styleOf(a).strokeWidth / 2.0;
    const QRectF  box   = Geometry::balloonRect(*a.balloon, *bodyOf(a)).adjusted(in, in, -in, -in);
    const QPointF at    = box.topLeft() + QPointF(0.25 * box.width(), 0.5 * box.height());
    const QRectF  area  = Geometry::textArea(a, w);
    EXPECT_NEAR(area.left(), at.x() - 30, k_px);
    EXPECT_NEAR(area.top(), at.y() - 10, k_px);
    EXPECT_NEAR(area.right(), box.right(), k_px) << "the right edge is automatic";

    // A block wider than the set left edge allows does not fit, whatever size the box is: the fit keeps its width.
    EXPECT_FALSE(Geometry::fits(a, QSizeF(80, 10)));
    EXPECT_EQ(Geometry::fitBoxFor(a, QSizeF(80, 10)).width(), boxOf(a).width());

    // The anchor is a fraction of the body: a wider body moves it with it.
    boxOf(a) = QSize(600, 200);
    const QRectF wider = Geometry::balloonRect(*a.balloon, *bodyOf(a)).adjusted(in, in, -in, -in);
    EXPECT_NEAR(Geometry::textArea(a, *wordsOf(a)).left(), wider.left() + 0.25 * wider.width() - 30, k_px);
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
 * \brief The text area's arrows sit on its left and right edges, level with the anchor, and on its top, above it;
 *        dragging one sets that edge where the pointer is, as its distance from the anchor.
 */
TEST(TextFit, TheTextAreaArrowsSetTheirEdges)
{
    using Geometry::TextAreaPart;
    Composition a = withShape(ShapeProperties::Kind::Box);
    boxOf(a)      = QSize(300, 200);
    const QRectF  area = Geometry::textArea(a, *wordsOf(a));
    const QPointF at   = Geometry::blockRect(a, *wordsOf(a), QSizeF()).center();   // the anchor
    const QList<Geometry::Handle> arrows = Geometry::textAreaHandles(a);
    ASSERT_EQ(arrows.size(), 3);
    EXPECT_EQ(Geometry::partOf<TextAreaPart>(arrows[0]), TextAreaPart::Side);
    EXPECT_EQ(arrows[0].at, QPointF(area.left(), at.y()));
    EXPECT_EQ(Geometry::partOf<TextAreaPart>(arrows[1]), TextAreaPart::Side);
    EXPECT_EQ(arrows[1].at, QPointF(area.right(), at.y()));
    EXPECT_EQ(Geometry::partOf<TextAreaPart>(arrows[2]), TextAreaPart::Top);
    EXPECT_EQ(arrows[2].at, QPointF(at.x(), area.top()));

    // Each sets its own edge, in whole pixels from the anchor, inside the balloon or past it; the others stay
    // automatic.
    Words w = Geometry::dragTextAreaHandle(a, arrows[0], QPointF(at.x() - 40, 0));
    ASSERT_TRUE(w.frame.left.has_value());
    EXPECT_NEAR(*w.frame.left, 40.0, k_px);
    EXPECT_FALSE(w.frame.right.has_value());
    w = Geometry::dragTextAreaHandle(a, arrows[1], QPointF(at.x() + 400, 0));   // out past the outline
    EXPECT_NEAR(*w.frame.right, 400.0, k_px);
    w = Geometry::dragTextAreaHandle(a, arrows[2], QPointF(0, at.y() - 25.4));
    EXPECT_NEAR(*w.frame.top, 25.0, k_px);

    // Dragged past the anchor, an edge stops a pixel short of it: the area never turns inside out.
    w = Geometry::dragTextAreaHandle(a, arrows[0], QPointF(at.x() + 50, 0));
    EXPECT_NEAR(*w.frame.left, 1.0, k_px);

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

TEST(TextFit, TheAnchorIsTheAlignedSideAndTheMiddleOfTheBlock)
{
    Composition a   = withShape(ShapeProperties::Kind::None);
    boxOf(a)            = QSize(300, 100);
    const QSizeF blk(120, 40);

    textOf(a).align = Qt::AlignHCenter;
    EXPECT_EQ(Geometry::textAnchorFor(a, blk), QPointF(150, 50));
    textOf(a).align = Qt::AlignLeft;
    EXPECT_EQ(Geometry::textAnchorFor(a, blk), QPointF(0, 50));
    textOf(a).align = Qt::AlignRight;
    EXPECT_EQ(Geometry::textAnchorFor(a, blk), QPointF(300, 50));

    // Taller than its area, the block is pinned to the top, as textOutline() draws it.
    EXPECT_EQ(Geometry::textAnchorFor(a, QSizeF(120, 160)).y(), 80);
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
