/**
 * @file
 * @brief Property-group ownership: applying a group writes that group and nothing else.
 *
 * These exist for a specific reason. The strip editor has shipped the same class of bug three times —
 * code writing a field that was not its own, because the invariant lived in a comment rather than in a
 * type. Property groups make that unstateable, and these tests are what keeps it unstateable as groups
 * are added.
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
    a.shape.kind       = TextArtifact::Shape::Thought;
    a.box              = QSize(321, 123);

    Tail t;
    t.tip              = QPointF(17, 42);
    t.baseWidth        = 29.0;
    t.bend             = 0.4;
    a.tails.items      = {t};

    a.style.kind       = TextArtifact::Style::Ink;
    a.style.amount     = 1.7;
    a.styleSeed        = 0xC0FFEEu;

    a.text.body        = QStringLiteral("Nie wiem, co sie dzieje");
    a.text.family      = QStringLiteral("Comic Neue");
    a.text.pixelSize   = 44;
    a.text.bold        = true;
    a.text.align       = Qt::AlignRight;
    a.text.colour      = QColor(1, 2, 3);

    a.skin.fill        = QColor(4, 5, 6);
    a.skin.stroke      = QColor(7, 8, 9);
    a.skin.strokeWidth = 11;
    return a;
}

//! Which group a test is applying, so everything else can be checked as untouched.
enum class Group { Shape, Skin, Style, Text, Tails };

/**
 * @brief Asserts that every property outside \p applied is byte-identical.
 *
 * Listed property by property rather than through the groups' own operator==, on purpose: equality is
 * itself something that can be forgotten when a property is added, and a test leaning on it would
 * weaken silently at exactly the moment it is most needed. **Adding a property to TextArtifact should
 * break this function until someone has decided which group owns it.**
 */
void expectUntouched(Group applied, const TextArtifact& before, const TextArtifact& after)
{
    if (applied != Group::Shape) {
        EXPECT_EQ(after.shape.kind, before.shape.kind);
    }

    if (applied != Group::Skin) {
        EXPECT_EQ(after.skin.fill,        before.skin.fill);
        EXPECT_EQ(after.skin.stroke,      before.skin.stroke);
        EXPECT_EQ(after.skin.strokeWidth, before.skin.strokeWidth);
    }
    if (applied != Group::Style) {
        EXPECT_EQ(after.style.kind, before.style.kind);
        EXPECT_DOUBLE_EQ(after.style.amount, before.style.amount);
    }
    if (applied != Group::Text) {
        EXPECT_EQ(after.text.body,      before.text.body);
        EXPECT_EQ(after.text.family,    before.text.family);
        EXPECT_EQ(after.text.pixelSize, before.text.pixelSize);
        EXPECT_EQ(after.text.bold,      before.text.bold);
        EXPECT_EQ(after.text.align,     before.text.align);
        EXPECT_EQ(after.text.colour,    before.text.colour);
    }
    if (applied != Group::Tails) {
        ASSERT_EQ(after.tails.items.size(), before.tails.items.size());
        for (int i = 0; i < after.tails.items.size(); ++i)
            EXPECT_EQ(after.tails.items.at(i), before.tails.items.at(i));
    }

    // Owned by no group, so never written by an editor and never skipped here. The box belongs to the
    // object rather than to the artifact's look; the style seed is per balloon and set once at
    // placement, which is why a preset must not carry it.
    EXPECT_EQ(after.box,       before.box);
    EXPECT_EQ(after.styleSeed, before.styleSeed);
}

} // namespace

// ---------------------------------------------------------------------------
// One test per group: it writes what it owns, and nothing else.
// ---------------------------------------------------------------------------

TEST(PropertyGroupOwnership, Skin)
{
    const TextArtifact before = loadedArtifact();
    TextArtifact       after  = before;

    SkinProperties s;
    s.fill        = QColor(200, 100, 50);
    s.stroke      = QColor(10, 220, 30);
    s.strokeWidth = 37;
    s.applyTo(after);

    EXPECT_EQ(after.skin, s);
    EXPECT_EQ(SkinProperties::from(after), s);
    expectUntouched(Group::Skin, before, after);
}

TEST(PropertyGroupOwnership, Shape)
{
    const TextArtifact before = loadedArtifact();
    TextArtifact       after  = before;

    ShapeProperties s;
    s.kind = TextArtifact::Shape::Banner;
    s.applyTo(after);

    EXPECT_EQ(after.shape, s);
    EXPECT_EQ(ShapeProperties::from(after), s);
    expectUntouched(Group::Shape, before, after);
}

TEST(PropertyGroupOwnership, Style)
{
    const TextArtifact before = loadedArtifact();
    TextArtifact       after  = before;

    StyleProperties s;
    s.kind   = TextArtifact::Style::Marker;
    s.amount = 0.25;
    s.applyTo(after);

    EXPECT_EQ(after.style, s);
    EXPECT_EQ(StyleProperties::from(after), s);
    expectUntouched(Group::Style, before, after);
}

//! The seed is part of the style and deliberately not part of its group — see StyleProperties.
TEST(PropertyGroupOwnership, StyleLeavesTheSeedAlone)
{
    TextArtifact a = loadedArtifact();
    const quint32 seed = a.styleSeed;

    StyleProperties s;
    s.kind   = TextArtifact::Style::Marker;
    s.applyTo(a);

    EXPECT_EQ(a.styleSeed, seed);
}

TEST(PropertyGroupOwnership, Text)
{
    const TextArtifact before = loadedArtifact();
    TextArtifact       after  = before;

    TextProperties s;
    s.body      = QStringLiteral("Co tu sie odprawia");
    s.family    = QStringLiteral("Bangers");
    s.pixelSize = 19;
    s.bold      = false;
    s.align     = Qt::AlignLeft;
    s.colour    = QColor(90, 80, 70);
    s.applyTo(after);

    EXPECT_EQ(after.text, s);
    EXPECT_EQ(TextProperties::from(after), s);
    expectUntouched(Group::Text, before, after);
}

TEST(PropertyGroupOwnership, Tails)
{
    const TextArtifact before = loadedArtifact();
    TextArtifact       after  = before;

    Tail one;
    one.tip       = QPointF(-5, 300);
    one.baseWidth = 12.0;
    one.bend      = -0.3;
    TailsProperties s;
    s.items = {one, one};
    s.applyTo(after);

    EXPECT_EQ(after.tails, s);
    EXPECT_EQ(TailsProperties::from(after), s);
    expectUntouched(Group::Tails, before, after);
}

/**
 * @brief The two colour groups are next door to each other and must stay there.
 *
 * Editing a balloon's frame does not edit its lettering, which is why the colour tool has two swatches
 * and not three. Both set to the same colour, so a group writing across the boundary cannot pass.
 */
TEST(PropertyGroupOwnership, SkinAndTextColoursAreSeparate)
{
    TextArtifact a = loadedArtifact();
    a.text.colour  = QColor(123, 45, 67);

    SkinProperties s = SkinProperties::from(a);
    s.fill = QColor(123, 45, 67);
    s.applyTo(a);

    EXPECT_EQ(a.skin.fill,   QColor(123, 45, 67));
    EXPECT_EQ(a.text.colour, QColor(123, 45, 67));

    TextProperties t = TextProperties::from(a);
    t.colour = QColor(9, 9, 9);
    t.applyTo(a);

    EXPECT_EQ(a.skin.fill,   QColor(123, 45, 67));   // still the balloon's
    EXPECT_EQ(a.text.colour, QColor(9, 9, 9));
}

// ---------------------------------------------------------------------------
// Persistence — the structs changed, the file format did not
// ---------------------------------------------------------------------------

TEST(PropertyGroupPersistence, KeepsTheFlatKeys)
{
    const TextArtifact a = loadedArtifact();
    const QJsonObject  j = artifactToJson(a);

    EXPECT_EQ(j.value(QStringLiteral("strokeWidth")).toInt(),  a.skin.strokeWidth);
    EXPECT_EQ(j.value(QStringLiteral("fill")).toString(),      a.skin.fill.name(QColor::HexArgb));
    EXPECT_EQ(j.value(QStringLiteral("stroke")).toString(),    a.skin.stroke.name(QColor::HexArgb));
    EXPECT_EQ(j.value(QStringLiteral("text")).toString(),      a.text.body);
    EXPECT_EQ(j.value(QStringLiteral("fontFamily")).toString(),a.text.family);
    EXPECT_EQ(j.value(QStringLiteral("fontSize")).toInt(),     a.text.pixelSize);
    EXPECT_EQ(j.value(QStringLiteral("align")).toInt(),        a.text.align);
    EXPECT_EQ(j.value(QStringLiteral("styleAmount")).toDouble(), a.style.amount);

    // No grouping crept into the file: an artifact saved before the groups existed still loads.
    for (const char* nested : {"skin", "shape", "style", "tails"})
        EXPECT_FALSE(j.value(QLatin1String(nested)).isObject()) << nested;
}

TEST(PropertyGroupPersistence, RoundTrips)
{
    const TextArtifact a = loadedArtifact();
    const TextArtifact b = artifactFromJson(artifactToJson(a));

    EXPECT_EQ(b.shape, a.shape);
    EXPECT_EQ(b.skin,  a.skin);
    EXPECT_EQ(b.style, a.style);
    EXPECT_EQ(b.text,  a.text);
    EXPECT_EQ(b.tails, a.tails);
    EXPECT_EQ(b, a);
}
