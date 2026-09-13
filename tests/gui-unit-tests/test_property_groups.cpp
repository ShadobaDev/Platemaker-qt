/**
 * @file
 * @brief Property-group ownership: applying a group writes that group and nothing else.
 *
 * The first automated test in this repository, and it exists for a specific reason. The strip editor
 * has shipped the same class of bug three times — code writing a field that was not its own, because
 * the invariant lived in a comment rather than in a type. Property groups make that unstateable, and
 * these tests are what keeps it unstateable as groups are added.
 *
 * No Qt Creator, no GUI, no event loop: a group's applyTo() is a pure function from properties to
 * properties, so this is an ordinary console test.
 */

#include <gtest/gtest.h>

#include <QJsonObject>

#include "textartifact.h"

namespace {

//! An artifact with **every** property moved off its default, so an accidental write is visible.
TextArtifact loadedArtifact()
{
    TextArtifact a;
    a.shape            = TextArtifact::Shape::Thought;
    a.box              = QSize(321, 123);

    Tail t;
    t.tip              = QPointF(17, 42);
    t.baseWidth        = 29.0;
    t.bend             = 0.4;
    a.tails            = {t};

    a.style            = TextArtifact::Style::Ink;
    a.styleAmount      = 1.7;
    a.styleSeed        = 0xC0FFEEu;

    a.text             = QStringLiteral("Nie wiem, co sie dzieje");
    a.fontFamily       = QStringLiteral("Comic Neue");
    a.fontPixelSize    = 44;
    a.bold             = true;
    a.align            = Qt::AlignRight;
    a.textColour       = QColor(1, 2, 3);

    a.skin.fill        = QColor(4, 5, 6);
    a.skin.stroke      = QColor(7, 8, 9);
    a.skin.strokeWidth = 11;
    return a;
}

//! A skin sharing no value with the one above, so "it was written" cannot pass by coincidence.
SkinProperties otherSkin()
{
    SkinProperties s;
    s.fill        = QColor(200, 100, 50);
    s.stroke      = QColor(10, 220, 30);
    s.strokeWidth = 37;
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// Skin
// ---------------------------------------------------------------------------

TEST(SkinPropertiesOwnership, WritesTheThreePropertiesItOwns)
{
    TextArtifact       a = loadedArtifact();
    const SkinProperties s = otherSkin();

    s.applyTo(a);

    EXPECT_EQ(a.skin.fill,        s.fill);
    EXPECT_EQ(a.skin.stroke,      s.stroke);
    EXPECT_EQ(a.skin.strokeWidth, s.strokeWidth);
}

/**
 * @brief The guard this whole file exists for.
 *
 * Listed property by property rather than through operator==, on purpose: equality is itself something
 * that can be forgotten when a field is added, and a test that leans on it would weaken silently at
 * exactly the moment it is most needed. Adding a property to TextArtifact should break this test until
 * someone has decided which group owns it.
 */
TEST(SkinPropertiesOwnership, TouchesNothingElse)
{
    const TextArtifact before = loadedArtifact();
    TextArtifact       after  = before;

    otherSkin().applyTo(after);

    EXPECT_EQ(after.shape,         before.shape);
    EXPECT_EQ(after.box,           before.box);
    ASSERT_EQ(after.tails.size(),  before.tails.size());
    EXPECT_EQ(after.tails.first(), before.tails.first());
    EXPECT_EQ(after.style,         before.style);
    EXPECT_DOUBLE_EQ(after.styleAmount, before.styleAmount);
    EXPECT_EQ(after.styleSeed,     before.styleSeed);
    EXPECT_EQ(after.text,          before.text);
    EXPECT_EQ(after.fontFamily,    before.fontFamily);
    EXPECT_EQ(after.fontPixelSize, before.fontPixelSize);
    EXPECT_EQ(after.bold,          before.bold);
    EXPECT_EQ(after.align,         before.align);
    EXPECT_EQ(after.textColour,    before.textColour);
}

//! The text's colour belongs to the text, not to the balloon's surface — so Skin must not carry it.
TEST(SkinPropertiesOwnership, LeavesTheTextColourAlone)
{
    TextArtifact a = loadedArtifact();
    a.textColour   = QColor(123, 45, 67);

    SkinProperties s = otherSkin();
    s.fill  = QColor(123, 45, 67);   // the same colour, in the group next door
    s.applyTo(a);

    EXPECT_EQ(a.skin.fill,   QColor(123, 45, 67));
    EXPECT_EQ(a.textColour,  QColor(123, 45, 67));
}

TEST(SkinPropertiesOwnership, ReadsBackWhatItWrote)
{
    TextArtifact         a = loadedArtifact();
    const SkinProperties s = otherSkin();

    s.applyTo(a);

    EXPECT_EQ(SkinProperties::from(a), s);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

/**
 * @brief The struct changed; the file format did not.
 *
 * Grouping the three properties is a C++ change, and existing overlays must keep loading. The keys stay
 * flat and at the top level, exactly where they were before the group existed.
 */
TEST(SkinPropertiesPersistence, KeepsTheFlatKeys)
{
    const TextArtifact a = loadedArtifact();
    const QJsonObject  j = artifactToJson(a);

    EXPECT_EQ(j.value(QStringLiteral("strokeWidth")).toInt(), a.skin.strokeWidth);
    EXPECT_EQ(j.value(QStringLiteral("fill")).toString(),   a.skin.fill.name(QColor::HexArgb));
    EXPECT_EQ(j.value(QStringLiteral("stroke")).toString(), a.skin.stroke.name(QColor::HexArgb));
    EXPECT_FALSE(j.contains(QStringLiteral("skin")));   // no nesting crept into the file
}

TEST(SkinPropertiesPersistence, RoundTrips)
{
    const TextArtifact a = loadedArtifact();
    const TextArtifact b = artifactFromJson(artifactToJson(a));

    EXPECT_EQ(b.skin, a.skin);
    EXPECT_EQ(b, a);
}
