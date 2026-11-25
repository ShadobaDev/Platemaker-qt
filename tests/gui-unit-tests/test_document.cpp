// The strip editor's objects as the project holds them, and the operations on them: each says whether it
// changed anything. QtCore and the library's types only, so tested without a scene.

#include <gtest/gtest.h>

#include "objects/document.hpp"
#include "shapes.hpp"
#include "objectparts.hpp"

#include <QPainterPath>

#include <array>
using StripEdit::Document;
using Platemaker::Models::BlendMode;

namespace {

constexpr double k_tolerance = 1e-12;   //!< Fractions are copied, not computed.

//! Three overlays, back to front: "a", "b", "c", all on page "p1".
Document three()
{
    std::vector<Document::Overlay> list(3);
    const char* ids[] = {"a", "b", "c"};
    for (int i = 0; i < 3; ++i) {
        list[size_t(i)].uid            = ids[i];
        list[size_t(i)].anchorInputUid = "p1";
    }
    Document d;
    d.reset(list, {});
    return d;
}

QStringList order(const Document& d)
{
    QStringList out;
    for (const Document::Overlay& o : d.overlays())
        out << QString::fromStdString(o.uid);
    return out;
}

} // namespace

TEST(Document, TheStackOrderMovesOnePlaceAndNotPastItsEnds)
{
    Document d = three();
    EXPECT_TRUE(d.move("a", true));
    EXPECT_EQ(order(d), (QStringList{"b", "a", "c"}));
    EXPECT_FALSE(d.move("c", true)) << "already in front";
    EXPECT_FALSE(d.move("b", false)) << "already at the back";
    EXPECT_FALSE(d.move("gone", true));

    EXPECT_EQ(d.setOrder({"c", "a", "b"}), Document::Reorder::Done);
    EXPECT_EQ(order(d), (QStringList{"c", "a", "b"}));
    EXPECT_EQ(d.setOrder({"strip", "c", "a", "b"}), Document::Reorder::Unchanged) << "a row that is no overlay drops out";
    EXPECT_EQ(d.setOrder({"c", "a"}), Document::Reorder::Refused) << "an order that lost one";
    EXPECT_EQ(order(d), (QStringList{"c", "a", "b"}));
}

TEST(Document, BlendIsSharedOrMixedAndSetOnlyWhereItDiffers)
{
    Document d = three();
    EXPECT_EQ(d.blendOf({"a", "b"}), BlendMode::Over);
    EXPECT_EQ(d.setBlend({"a"}, BlendMode::Multiply), QStringList{"a"});
    EXPECT_FALSE(d.blendOf({"a", "b"}).has_value()) << "mixed";
    EXPECT_EQ(d.setBlend({"a", "b"}, BlendMode::Multiply), QStringList{"b"}) << "a has it already";
    EXPECT_FALSE(d.blendOf({}).has_value());
}

TEST(Document, AnOverlaysPlaceAndPropertiesChangeOnlyWhenTheyDiffer)
{
    Document d = three();
    EXPECT_FALSE(d.setEnabled("a", true)) << "shown already";
    EXPECT_TRUE(d.setEnabled("a", false));
    EXPECT_FALSE(d.overlay("a")->enabled);

    EXPECT_FALSE(d.setAnchor("b", "p1"));
    EXPECT_TRUE(d.setAnchor("b", "p2"));
    EXPECT_EQ(d.overlay("b")->anchorInputUid, "p2");

    ASSERT_TRUE(d.setPlace("c", {"p3", 0.1, 0.2, 0.3}));
    EXPECT_EQ(d.overlay("c")->anchorInputUid, "p3");
    EXPECT_NEAR(d.overlay("c")->xFrac, 0.1, k_tolerance);
    EXPECT_NEAR(d.overlay("c")->yFrac, 0.2, k_tolerance);
    EXPECT_NEAR(d.overlay("c")->wFrac, 0.3, k_tolerance);
    EXPECT_FALSE(d.setPlace("gone", {}));

    d.remove({"a", "c"});
    EXPECT_EQ(order(d), QStringList{"b"});
    EXPECT_EQ(d.overlay("a"), nullptr);
}

TEST(Edits, EachSaysWhetherItChangedTheObject)
{
    namespace E = StripEdit::Edits;

    // A re-roll deals a new seed, never the old one nor 0, to what draws from one; plain ink has nothing to deal.
    Composition rough     = balloonObject();
    styleOf(rough).line   = StyleProperties::Line::Brush;
    rough.seed            = 7;
    const quint32 dealt[] = {7, 0, 9};
    int           next    = 0;
    EXPECT_TRUE(E::reroll(rough, [&] { return dealt[next++]; }));
    EXPECT_EQ(rough.seed, 9u);
    Composition plain = balloonObject();
    EXPECT_FALSE(E::reroll(plain, [] { return 1u; }));

    // Paint lands on a part the object has; a text object has no line.
    Composition text = textObject();
    EXPECT_TRUE(E::paint(text, Geometry::Part::Text, Qt::red));
    EXPECT_EQ(textOf(text).fill.colour, QColor(Qt::red));
    EXPECT_FALSE(E::paint(text, Geometry::Part::Outline, Qt::red));

    // Converting is to the other kind only; the tails go with the balloon.
    Composition balloon = balloonObject();
    setTails(balloon, {Tail{}});
    EXPECT_FALSE(E::convert(balloon, true, {}));
    EXPECT_TRUE(E::convert(balloon, false, {}));
    EXPECT_FALSE(balloon.balloon.has_value());
    EXPECT_TRUE(E::convert(balloon, true, {}));
    EXPECT_TRUE(tailsOf(balloon).isEmpty());
    Composition picture = pictureObject("p.png");
    EXPECT_FALSE(E::convert(picture, true, {})) << "a picture is no balloon";
    EXPECT_FALSE(E::fitToText(picture)) << "a picture's box is its own pixels";

    // Tails go highest first, whatever order they are named in; none there, nothing done.
    Composition three = balloonObject();
    Tail        a, b, c;
    a.baseWidth = 1;
    b.baseWidth = 2;
    c.baseWidth = 3;
    setTails(three, {a, b, c});
    EXPECT_TRUE(E::removeTails(three, {0, 2}));
    ASSERT_EQ(tailsOf(three).size(), 1);
    EXPECT_NEAR(tailsOf(three).first().tail.baseWidth, 2.0, k_tolerance);
    EXPECT_FALSE(E::removeTails(three, {5}));
}

/**
 * \brief Words added to a balloon sit in the lower half of its body, lettered as the words they came from, with no
 *        text; a picture takes them too, a text object does not; the last words cannot be removed.
 */
TEST(Edits, WordsAreAddedInTheLowerHalfAndTheLastOneStays)
{
    namespace E = StripEdit::Edits;
    constexpr qreal k_px = 1e-9;

    Composition balloon      = balloonObject();
    textOf(balloon).body     = QStringLiteral("Hi");
    textOf(balloon).bold     = true;
    const int   first        = wordsOf(balloon)->id;
    const int   added        = E::addWords(balloon, first);
    ASSERT_NE(added, 0);
    ASSERT_EQ(balloon.words.size(), 2);
    const Words& second      = balloon.words.last();
    EXPECT_EQ(second.id, added);
    EXPECT_EQ(second.seat, balloon.words.first().seat);
    EXPECT_TRUE(second.text.body.isEmpty());
    EXPECT_TRUE(second.text.bold) << "the lettering of the words it came from";
    const QRectF whole = Geometry::textArea(balloon, balloon.words.first());
    const QRectF lower = Geometry::textArea(balloon, second);
    EXPECT_NEAR(lower.bottom(), whole.bottom(), k_px);
    EXPECT_NEAR(lower.top(), whole.top() + qRound(whole.height() / 2.0), k_px);

    // A colour poured on words lands on those words.
    EXPECT_TRUE(E::paint(balloon, Geometry::Part::Text, Qt::red, added));
    EXPECT_EQ(balloon.words.last().text.fill.colour, QColor(Qt::red));
    EXPECT_NE(balloon.words.first().text.fill.colour, QColor(Qt::red));

    EXPECT_TRUE(E::removeWords(balloon, added));
    EXPECT_FALSE(E::removeWords(balloon, first)) << "the last words stay";
    EXPECT_FALSE(E::removeWords(balloon, added)) << "gone already";

    Composition picture = pictureObject("p.png");
    EXPECT_NE(E::addWords(picture, 0), 0) << "a picture's words sit on it";
    Composition text = textObject();
    EXPECT_EQ(E::addWords(text, 0), 0) << "a text object is its one words";
    EXPECT_EQ(text.words.size(), 1);
}

/**
 * \brief *Add mask to object* covers the right third of the frame and reaches past it, hidden from the frame's right
 *        edge and shown at its left, under an id of its own; an object has one mask, and removing it takes it away.
 */
TEST(Edits, AMaskCoversTheRightThirdAndThereIsOne)
{
    namespace E = StripEdit::Edits;
    Composition  a     = balloonObject();
    const QRectF frame = frameOf(a);
    const int    next  = a.nextId;
    const int    id    = E::addMask(a);
    ASSERT_EQ(id, next);
    EXPECT_GT(a.nextId, id) << "its id is not handed out again";
    ASSERT_TRUE(a.mask);
    ASSERT_EQ(a.mask->regions.size(), 1);
    const QRectF region(a.mask->regions[0].place.pos, QSizeF(a.mask->regions[0].size));
    const qreal  inside = qRound(frame.width() / 3.0);
    EXPECT_EQ(region.left(), frame.right() - inside);
    EXPECT_GT(region.right(), frame.right()) << "past the frame, where an Edge fade shows nothing back";
    EXPECT_LT(region.top(), frame.top());
    EXPECT_GT(region.bottom(), frame.bottom());
    ASSERT_TRUE(a.mask->fade);
    EXPECT_EQ(a.mask->shape, Mask::Fade::Linear);
    constexpr qreal k_px = 1e-9;
    const QLineF    fade = *Geometry::maskFade(*a.mask);
    EXPECT_NEAR(fade.x1(), frame.right(), k_px) << "hidden wholly from the frame's right edge";
    EXPECT_NEAR(fade.x2(), region.left(), k_px) << "shown at the region's left";

    EXPECT_EQ(E::addMask(a), 0) << "one mask";
    EXPECT_TRUE(E::removeMask(a));
    EXPECT_FALSE(a.mask);
    EXPECT_FALSE(E::removeMask(a));

    Composition picture = pictureObject("p.png");
    EXPECT_NE(E::addMask(picture), 0) << "a picture is masked as a balloon is";
}

namespace {

constexpr qreal k_fit = 0.5;    //!< How near a stroke's regions keep to it, in the object's pixels.
constexpr qreal k_dab = 20.0;   //!< A dab's radius.

//! A press of the brush at @p at, @p radius round.
QPainterPath dab(const QPointF& at, qreal radius = k_dab)
{
    QPainterPath p;
    p.addEllipse(at, radius, radius);
    return p;
}

//! A rectangle of the brush or the eraser.
QPainterPath bar(const QRectF& r)
{
    QPainterPath p;
    p.addRect(r);
    return p;
}

//! Whether @p c's mask has a region over @p at.
bool coveredAt(const Composition& c, const QPointF& at)
{
    return c.mask && Geometry::maskOutline(*c.mask, c.seed).contains(at);
}

} // namespace

/**
 * \brief The mask brush hides and the eraser shows, in as few regions as lie apart: a stroke apart is a region of its
 *        own, one that meets two joins them, an eraser cutting one through leaves two, a hole is filled, and erased to
 *        nothing the mask goes. With Keep inside it is the other way round. The fade stays where it was.
 */
TEST(Edits, TheMaskBrushHidesAndTheEraserShowsInRegionsApart)
{
    namespace E = StripEdit::Edits;
    Composition a = balloonObject();
    EXPECT_FALSE(E::paintMask(a, dab({50, 50}), false, k_fit)) << "nothing is hidden to show";
    ASSERT_TRUE(E::paintMask(a, dab({50, 50}), true, k_fit)) << "the first stroke makes the mask";
    ASSERT_TRUE(a.mask);
    ASSERT_EQ(a.mask->regions.size(), 1);
    EXPECT_EQ(a.mask->regions[0].shape.kind, ShapeProperties::Kind::Curve);
    EXPECT_TRUE(coveredAt(a, {50, 50}));
    EXPECT_FALSE(coveredAt(a, {50 + k_dab + 1, 50}));
    EXPECT_FALSE(E::paintMask(a, dab({50, 50}, k_dab / 2), true, k_fit)) << "hidden already";

    const Body first = a.mask->regions[0];
    ASSERT_TRUE(E::paintMask(a, dab({200, 50}), true, k_fit));
    ASSERT_EQ(a.mask->regions.size(), 2) << "a stroke apart is a region of its own";
    EXPECT_EQ(a.mask->regions[0], first) << "a region the stroke does not meet keeps its nodes";

    ASSERT_TRUE(E::paintMask(a, bar({50, 45, 150, 10}), true, k_fit));
    EXPECT_EQ(a.mask->regions.size(), 1) << "a stroke meeting both joins them";
    EXPECT_TRUE(coveredAt(a, {125, 50}));

    ASSERT_TRUE(E::paintMask(a, bar({115, 0, 20, 100}), false, k_fit));
    EXPECT_EQ(a.mask->regions.size(), 2) << "an eraser cutting it through leaves two";
    EXPECT_FALSE(coveredAt(a, {125, 50}));
    EXPECT_FALSE(E::paintMask(a, dab({50, 50}, k_dab / 4), false, k_fit)) << "a hole is filled: it takes nothing";
    EXPECT_TRUE(coveredAt(a, {50, 50}));

    ASSERT_TRUE(E::paintMask(a, bar({0, 0, 300, 100}), false, k_fit));
    EXPECT_FALSE(a.mask) << "erased to nothing, the mask goes";

    ASSERT_TRUE(E::paintMask(a, dab({50, 50}).subtracted(dab({50, 50}, k_dab / 2)), true, k_fit));
    ASSERT_EQ(a.mask->regions.size(), 1) << "a ring painted round";
    EXPECT_TRUE(coveredAt(a, {50, 50})) << "the hole in it filled";

    // Keep inside: the regions are what shows, so the brush takes from them and the eraser adds.
    Composition k = balloonObject();
    ASSERT_TRUE(E::paintMask(k, dab({50, 50}), true, k_fit));
    k.mask->keepInside = true;
    ASSERT_TRUE(E::paintMask(k, dab({50 + k_dab, 50}, k_dab / 2), true, k_fit));
    EXPECT_FALSE(coveredAt(k, {50 + k_dab - 2, 50})) << "the brush hides: it takes from what shows";
    ASSERT_TRUE(E::paintMask(k, dab({200, 50}), false, k_fit));
    EXPECT_EQ(k.mask->regions.size(), 2) << "the eraser shows: it adds";

    // The fade stays on the object where it was, though the box round the regions grows.
    Composition f = balloonObject();
    ASSERT_TRUE(E::paintMask(f, dab({50, 50}), true, k_fit));
    f.mask->shape = Mask::Fade::Linear;
    f.mask->fade  = QLineF(0.0, 0.5, 1.0, 0.5);
    const QLineF line = *Geometry::maskFade(*f.mask);
    ASSERT_TRUE(E::paintMask(f, dab({80, 50}), true, k_fit));
    constexpr qreal k_px = 1e-9;
    const QLineF    moved = *Geometry::maskFade(*f.mask);
    EXPECT_NEAR(moved.x1(), line.x1(), k_px);
    EXPECT_NEAR(moved.y1(), line.y1(), k_px);
    EXPECT_NEAR(moved.x2(), line.x2(), k_px);
    EXPECT_NEAR(moved.y2(), line.y2(), k_px);

    // The cross's centre and its right arm, in pixels. Saved in tenths of a per cent, of a box and an arm a couple of
    // hundred pixels long: that rounding moves them a fraction of a pixel.
    constexpr qreal k_roundedPx = 0.25;
    const auto cross = [](const Mask& m) {
        const QRectF  box = Geometry::maskBox(m);
        const Paint&  t   = m.tone;
        const QPointF c   = box.topLeft() + QPointF(box.width() * t.fadeCentreX, box.height() * t.fadeCentreY) / 100.0;
        const qreal   arm = box.right() - c.x();
        return std::array<qreal, 4>{c.x(), c.y(), c.x() + arm * t.fadeRightFrom / 100.0,
                                    c.x() + arm * t.fadeRightTo / 100.0};
    };
    f.mask->shape              = Mask::Fade::Rectangle;
    f.mask->tone.fadeCentreX   = 40.0;
    f.mask->tone.fadeRightFrom = 20.0;
    f.mask->tone.fadeRightTo   = 80.0;
    const auto was = cross(*f.mask);
    ASSERT_TRUE(E::paintMask(f, dab({140, 50}), true, k_fit));
    const auto now = cross(*f.mask);
    for (size_t i = 0; i < was.size(); ++i)
        EXPECT_NEAR(now[i], was[i], k_roundedPx) << "at " << i;
    EXPECT_NEAR(f.mask->tone.fadeLeftFrom, 100.0, k_px) << "a side that does not fade still does not";
}

TEST(Document, ItHoldsTheCompositionsOfTheOverlaysItHoldsAndNothingElse)
{
    Document          d = three();
    Composition::Map  fed;
    fed.insert("a", balloonObject());
    fed.insert("orphan", balloonObject());   // no overlay: no object
    std::vector<Document::Overlay> list = d.overlays();
    d.reset(list, fed);
    EXPECT_TRUE(d.composition("a").has_value());
    EXPECT_FALSE(d.composition("b").has_value()) << "a picture never lettered has none";
    EXPECT_FALSE(d.composition("orphan").has_value());
    EXPECT_EQ(d.compositions().size(), 1);

    Composition said    = *d.composition("a");
    textOf(said).body   = QStringLiteral("said");
    EXPECT_TRUE(d.setComposition("a", said));
    EXPECT_FALSE(d.setComposition("a", said)) << "the same again is no change";
    EXPECT_EQ(textOf(*d.composition("a")).body, QStringLiteral("said"));
    EXPECT_FALSE(d.setComposition("gone", said)) << "no overlay, no composition";

    d.remove({"a"});
    EXPECT_FALSE(d.composition("a").has_value());
    EXPECT_TRUE(d.compositions().isEmpty());
}
