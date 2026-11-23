// The strip editor's objects as the project holds them, and the operations on them: each says whether it
// changed anything. QtCore and the library's types only, so tested without a scene.

#include <gtest/gtest.h>

#include "objects/document.hpp"
#include "shapes.hpp"
#include "objectparts.hpp"

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
 * \brief *Add mask* covers the right third of the frame and reaches past it, hidden from the frame's right edge
 *        and shown at its left, under an id of its own; an object has one mask, and removing it takes it away.
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
    EXPECT_GT(region.right(), frame.right()) << "past the frame, where a feather shows nothing back";
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
