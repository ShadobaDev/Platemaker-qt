// A balloon of several bodies joined by bridges: the chain's rules, each refusal leaving the composition as it
// was, merging two balloons, cutting one in pieces, and a bridge through the content's JSON.

#include <gtest/gtest.h>

#include <QJsonArray>

#include "chain.hpp"
#include "geometry.hpp"

using namespace Chain;

namespace {

//! The offset a second balloon is merged at, and a new body placed at: any distance that tells the two apart.
const QPointF k_apart(300, 40);

//! A balloon of one body with its words, as the Bubble tool places it.
Composition balloon(const QString& words = QStringLiteral("Hey"))
{
    Composition c = withBalloon(textObject(), BalloonLook{});
    c.words.first().text.body   = words;
    c.words.first().text.family = QStringLiteral("Comic Neue");
    return c;
}

int bodyId(const Composition& c, int index) { return c.balloon->bodies.at(index).id; }

//! A chain of three bodies, A–B–C, made by *Add bubble* twice.
Composition chainOfThree()
{
    Composition c = balloon();
    int b = 0, last = 0;
    EXPECT_EQ(addBody(c, bodyId(c, 0), k_apart, &b), ChainError::None);
    EXPECT_EQ(addBody(c, b, k_apart, &last), ChainError::None);
    return c;
}

QList<int> bodyIds(const Composition& c)
{
    QList<int> out;
    for (const Body& b : c.balloon->bodies)
        out << b.id;
    return out;
}

} // namespace

TEST(Chain, TwoBalloonsMergeAsTwoPiecesUntilABridgeJoinsThem)
{
    Composition       a     = balloon(QStringLiteral("first"));
    const Composition other = balloon(QStringLiteral("second"));
    QList<int>        merged;
    ASSERT_EQ(merge(a, other, k_apart, &merged), ChainError::None);

    ASSERT_EQ(a.balloon->bodies.size(), 2);
    ASSERT_EQ(merged.size(), 1);
    EXPECT_EQ(bodyId(a, 1), merged.first());
    EXPECT_NE(bodyId(a, 0), bodyId(a, 1));
    EXPECT_EQ(a.balloon->bodies.at(1).place.pos, other.balloon->bodies.first().place.pos + k_apart);
    // The merged words came along and sit on their own body, under a new id.
    ASSERT_EQ(a.words.size(), 2);
    EXPECT_EQ(a.words.at(1).text.body, QStringLiteral("second"));
    EXPECT_EQ(a.words.at(1).seat, merged.first());
    EXPECT_NE(a.words.at(1).id, a.words.at(0).id);
    EXPECT_GT(a.nextId, merged.first());

    EXPECT_FALSE(joined(*a.balloon, bodyId(a, 0), merged.first()));
    EXPECT_EQ(pieces(a).size(), 2);
    ASSERT_EQ(addBridge(a, bodyId(a, 0), merged.first()), ChainError::None);
    EXPECT_TRUE(joined(*a.balloon, bodyId(a, 0), merged.first()));
    EXPECT_EQ(pieces(a).size(), 1);
}

TEST(Chain, ARefusedEditLeavesTheCompositionAsItWas)
{
    Composition       c      = chainOfThree();
    const Composition before = c;
    const int         a = bodyId(c, 0), b = bodyId(c, 1), last = bodyId(c, 2);
    const int         bridge = c.balloon->tails.first().id;

    EXPECT_EQ(addBridge(c, a, a), ChainError::SameBody);
    EXPECT_EQ(addBridge(c, a, c.nextId + 1), ChainError::NotABody);
    EXPECT_EQ(addBridge(c, a, last), ChainError::ClosesRing);        // A and C are joined through B
    EXPECT_EQ(addBridge(c, b, a), ChainError::NotAChainEnd);         // B has two bridges
    EXPECT_EQ(addBody(c, b, k_apart), ChainError::NotAChainEnd);
    EXPECT_EQ(makeBridge(c, bridge, last), ChainError::NotATail);    // a bridge already
    EXPECT_EQ(makeBridge(c, c.nextId + 1, last), ChainError::NotATail);
    EXPECT_EQ(c, before);

    Composition text = textObject();
    EXPECT_EQ(addBody(text, 1, k_apart), ChainError::NoBalloon);
    EXPECT_EQ(merge(text, balloon(), k_apart), ChainError::NoBalloon);
    EXPECT_EQ(text, textObject());
}

TEST(Chain, AHeldTailBecomesTheBridgeAndKeepsItsLook)
{
    Composition a = balloon();
    BalloonTail tail;
    tail.id             = a.nextId++;
    tail.root           = bodyId(a, 0);
    tail.tail.baseWidth = 20;
    tail.tail.bend      = 0.25;
    a.balloon->tails.append(tail);

    QList<int> merged;
    ASSERT_EQ(merge(a, balloon(), k_apart, &merged), ChainError::None);
    ASSERT_EQ(makeBridge(a, tail.id, merged.first()), ChainError::None);

    ASSERT_EQ(a.balloon->tails.size(), 1);
    const BalloonTail& bridge = a.balloon->tails.first();
    EXPECT_TRUE(bridge.isBridge());
    EXPECT_EQ(bridge.id, tail.id);
    EXPECT_EQ(bridge.to, merged.first());
    EXPECT_EQ(bridge.tail, tail.tail);
    EXPECT_EQ(pieces(a).size(), 1);
}

TEST(Chain, AnAddedBubbleIsBridgedAndGetsEmptyWordsInItsNeighboursLettering)
{
    const Composition c = chainOfThree();
    ASSERT_EQ(c.balloon->bodies.size(), 3);
    EXPECT_EQ(bridgesOn(*c.balloon, bodyId(c, 0)), 1);
    EXPECT_EQ(bridgesOn(*c.balloon, bodyId(c, 1)), 2);
    EXPECT_TRUE(chainEnd(*c.balloon, bodyId(c, 2)));
    EXPECT_EQ(c.balloon->bodies.at(1).place.pos, c.balloon->bodies.at(0).place.pos + k_apart);
    EXPECT_EQ(c.balloon->bodies.at(1).shape, c.balloon->bodies.at(0).shape);

    ASSERT_EQ(c.words.size(), 3);
    EXPECT_EQ(c.words.at(1).seat, bodyId(c, 1));
    EXPECT_TRUE(c.words.at(1).text.body.isEmpty());
    EXPECT_EQ(c.words.at(1).text.family, QStringLiteral("Comic Neue"));
}

TEST(Chain, RemovingAMiddleBubbleOrABridgeSplitsTheChain)
{
    Composition c = chainOfThree();
    Words caption;   // words with no seat stay with the first piece
    caption.id = c.nextId++;
    c.words.append(caption);
    const int a = bodyId(c, 0), b = bodyId(c, 1), last = bodyId(c, 2);

    const QList<Composition> split = withoutBody(c, b);
    ASSERT_EQ(split.size(), 2);
    EXPECT_EQ(bodyIds(split.at(0)), QList<int>{a});
    EXPECT_EQ(bodyIds(split.at(1)), QList<int>{last});
    EXPECT_TRUE(split.at(0).balloon->tails.isEmpty());
    EXPECT_TRUE(split.at(1).balloon->tails.isEmpty());
    ASSERT_EQ(split.at(0).words.size(), 2);
    EXPECT_EQ(split.at(0).words.at(1).id, caption.id);
    ASSERT_EQ(split.at(1).words.size(), 1);
    EXPECT_EQ(split.at(1).words.first().seat, last);
    for (const Composition& p : split) {
        EXPECT_EQ(p.nextId, c.nextId);
        EXPECT_EQ(p.balloon->style, c.balloon->style);
    }

    const QList<Composition> cut = withoutBridge(c, c.balloon->tails.first().id);   // the bridge A–B
    ASSERT_EQ(cut.size(), 2);
    EXPECT_EQ(bodyIds(cut.at(0)), QList<int>{a});
    EXPECT_EQ(bodyIds(cut.at(1)), (QList<int>{b, last}));
    EXPECT_EQ(cut.at(1).balloon->tails.size(), 1);

    const Composition end = withoutBody(c, last).first();   // an end: one piece, shorter
    EXPECT_EQ(bodyIds(end), (QList<int>{a, b}));
    EXPECT_EQ(withoutBody(c, c.nextId + 1).size(), 1);   // no such body
    EXPECT_EQ(withoutBody(balloon(), bodyId(balloon(), 0)).first(), balloon());   // the last body stays
}

TEST(Chain, ABridgeIsSavedWithItsFarBodyAndATailWithout)
{
    const Composition c = chainOfThree();
    EXPECT_EQ(compositionFromJson(toJson(c)), c);

    Composition withTail = balloon();
    BalloonTail tail;
    tail.id   = withTail.nextId++;
    tail.root = bodyId(withTail, 0);
    withTail.balloon->tails.append(tail);
    const QJsonObject saved = toJson(withTail).value(QStringLiteral("balloon")).toObject()
                                  .value(QStringLiteral("tails")).toArray().first().toObject();
    EXPECT_FALSE(saved.contains(QStringLiteral("to")));
}

TEST(Chain, ABridgeDrawsTwoBodiesAsOneOutline)
{
    // No lettering: measuring it needs the QGuiApplication this target does not have.
    Composition a = balloon(QString());
    QList<int>  merged;
    ASSERT_EQ(merge(a, balloon(QString()), k_apart, &merged), ChainError::None);
    EXPECT_EQ(Geometry::silhouette(*a.balloon, a.seed).toSubpathPolygons().size(), 2);   // two pieces, apart

    ASSERT_EQ(addBridge(a, bodyId(a, 0), merged.first()), ChainError::None);
    const QPainterPath joined = Geometry::silhouette(*a.balloon, a.seed);
    EXPECT_EQ(joined.toSubpathPolygons().size(), 1);   // one ring round both: no edge inside the joint
    const QRectF second(a.balloon->bodies.at(1).place.pos, QSizeF(a.balloon->bodies.at(1).size));
    EXPECT_TRUE(Geometry::bounds(a).contains(second));
    EXPECT_EQ(Geometry::tailsDrawn(*a.balloon, a.seed), QList<bool>{true});

    a.balloon->tails.first().tail.baseWidth = 0.0;   // a bridge of no width draws none
    EXPECT_EQ(Geometry::silhouette(*a.balloon, a.seed).toSubpathPolygons().size(), 2);
}
