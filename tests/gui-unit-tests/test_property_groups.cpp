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

#include "artifactsvg.hpp"
#include "propertygroup.hpp"
#include "artifact.hpp"

namespace {

//! An artifact with **every** property moved off its default, so an accidental write is visible.
Artifact loadedArtifact()
{
    Artifact a;
    a.shape.kind       = Artifact::Shape::Thought;
    a.box              = QSize(321, 123);

    Tail t;
    t.tip              = QPointF(17, 42);
    t.baseWidth        = 29.0;
    t.bend             = 0.4;
    a.tails.items      = {t};

    a.style.kind       = Artifact::Style::Ink;
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
enum class Group { Shape, Skin, Style, Text, Tails, Nothing };

/**
 * @brief Asserts that every property outside \p applied is byte-identical.
 *
 * Listed property by property rather than through the groups' own operator==, on purpose: equality is
 * itself something that can be forgotten when a property is added, and a test leaning on it would
 * weaken silently at exactly the moment it is most needed. **Adding a property to Artifact should
 * break this function until someone has decided which group owns it.**
 */
void expectUntouched(Group applied, const Artifact& before, const Artifact& after)
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
    const Artifact before = loadedArtifact();
    Artifact       after  = before;

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
    const Artifact before = loadedArtifact();
    Artifact       after  = before;

    ShapeProperties s;
    s.kind = Artifact::Shape::Banner;
    s.applyTo(after);

    EXPECT_EQ(after.shape, s);
    EXPECT_EQ(ShapeProperties::from(after), s);
    expectUntouched(Group::Shape, before, after);
}

TEST(PropertyGroupOwnership, Style)
{
    const Artifact before = loadedArtifact();
    Artifact       after  = before;

    StyleProperties s;
    s.kind   = Artifact::Style::Marker;
    s.amount = 0.25;
    s.applyTo(after);

    EXPECT_EQ(after.style, s);
    EXPECT_EQ(StyleProperties::from(after), s);
    expectUntouched(Group::Style, before, after);
}

//! The seed is part of the style and deliberately not part of its group — see StyleProperties.
TEST(PropertyGroupOwnership, StyleLeavesTheSeedAlone)
{
    Artifact a = loadedArtifact();
    const quint32 seed = a.styleSeed;

    StyleProperties s;
    s.kind   = Artifact::Style::Marker;
    s.applyTo(a);

    EXPECT_EQ(a.styleSeed, seed);
}

TEST(PropertyGroupOwnership, Text)
{
    const Artifact before = loadedArtifact();
    Artifact       after  = before;

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
    const Artifact before = loadedArtifact();
    Artifact       after  = before;

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
 * @brief One tail is a group of its own: editing tail 2 leaves tail 1 and tail 3 as they were.
 *
 * The reason tails became objects. When one editor wrote a width and bend onto every tail, a balloon with
 * two differently shaped tails lost the difference the next time anything else about it was edited.
 */
TEST(PropertyGroupOwnership, OneTail)
{
    Artifact before = loadedArtifact();
    Tail first = before.tails.items.first();
    Tail last  = first;
    last.tip       = QPointF(250, -40);
    last.baseWidth = 8.0;
    last.bend      = 0.9;
    Tail middle = first;
    middle.tip  = QPointF(90, 200);
    before.tails.items = {first, middle, last};
    Artifact after = before;

    TailProperties edited = TailProperties::from(before, 1);
    edited.tail.baseWidth = 77.0;
    edited.tail.bend      = -0.6;
    edited.applyTo(after);

    ASSERT_EQ(after.tails.items.size(), 3);
    EXPECT_EQ(after.tails.items.at(0), before.tails.items.at(0));
    EXPECT_EQ(after.tails.items.at(1), edited.tail);
    EXPECT_EQ(after.tails.items.at(2), before.tails.items.at(2));
    EXPECT_EQ(TailProperties::from(after, 1), edited);
    expectUntouched(Group::Tails, before, after);   // and nothing outside the tails
}

//! A tail selected before an undo removed it has nowhere to go. Writing it must not bring it back.
TEST(PropertyGroupOwnership, OneTailOutOfRangeWritesNothing)
{
    const Artifact before = loadedArtifact();   // one tail
    Artifact       after  = before;

    TailProperties gone;
    gone.index      = 3;
    gone.tail.bend  = 0.5;
    gone.applyTo(after);

    expectUntouched(Group::Nothing, before, after);   // not even the tails
}

/**
 * @brief The two colour groups are next door to each other and must stay there.
 *
 * Editing a balloon's frame does not edit its lettering, which is why the colour tool has two swatches
 * and not three. Both set to the same colour, so a group writing across the boundary cannot pass.
 */
TEST(PropertyGroupOwnership, SkinAndTextColoursAreSeparate)
{
    Artifact a = loadedArtifact();
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
    const Artifact a = loadedArtifact();
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
    const Artifact a = loadedArtifact();
    const Artifact b = artifactFromJson(artifactToJson(a));

    EXPECT_EQ(b.shape, a.shape);
    EXPECT_EQ(b.skin,  a.skin);
    EXPECT_EQ(b.style, a.style);
    EXPECT_EQ(b.text,  a.text);
    EXPECT_EQ(b.tails, a.tails);
    EXPECT_EQ(b, a);
}

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------

/**
 * @brief What *Convert to ▸* promises: a kind the record cannot draw hides properties, never destroys
 *        them, so converting back brings them with it.
 *
 * The promise is made in the menu's own message ("converting back brings them back"), and everything
 * behind it is this one rule — the tails stay in the record and the *kind* decides whether they count.
 * A painter taught to draw tails without a balloon, or a conversion taught to clear them, would break
 * the promise silently; this is what would fail first.
 */
TEST(Conversion, AKindHidesTailsRatherThanDestroyingThem)
{
    Artifact a = loadedArtifact();
    a.shape.kind   = Artifact::Shape::Speech;
    ASSERT_FALSE(a.tails.items.isEmpty());
    ASSERT_TRUE(a.hasTail());

    const TailsProperties kept = a.tails;

    a.shape.kind = Artifact::Shape::None;   // what the conversion does, and all it does
    EXPECT_FALSE(a.hasSilhouette());            // the one structural question, and its whole answer
    EXPECT_FALSE(a.hasTail());                  // no balloon, so nothing for a tail to leave
    EXPECT_EQ(a.tails, kept);                   // ...but the record still has them

    a.shape.kind = Artifact::Shape::Caption;
    EXPECT_TRUE(a.hasSilhouette());
    EXPECT_TRUE(a.hasTail());
    EXPECT_EQ(a.tails, kept);
}

/**
 * @brief The picker offers every silhouette exactly once — and offers "no balloon" never.
 *
 * `Shape::None` is a *kind*, not a silhouette: it is the absence of one, and an object without a
 * silhouette has no fill, no outline, no line style and nothing for a tail to leave from. A picker that
 * offered it would let a property control change what the object is, which is *Convert to ▸*'s job.
 */
TEST(Conversion, ThePickerOffersEverySilhouetteAndNoKind)
{
    EXPECT_FALSE(shapeOrder().contains(Artifact::Shape::None));
    // Every enumerated shape except None, and each of them once.
    EXPECT_EQ(shapeOrder().size(), int(Artifact::Shape::Banner));
    for (Artifact::Shape s : shapeOrder()) {
        EXPECT_FALSE(shapeTitle(s).isEmpty()) << shapeName(s);
        EXPECT_EQ(shapeOrder().count(s), 1) << shapeName(s);
    }
    // Named all the same, because a kind still has to be spelled somewhere.
    EXPECT_FALSE(shapeTitle(Artifact::Shape::None).isEmpty());
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

/**
 * @brief What an import decides by: **our own file carries a recipe, and nobody else's does**.
 *
 * A bubble's SVG is an ordinary drawing plus the parameters it was drawn from, in a namespace no
 * renderer looks at. That is what lets one come home as a re-typable balloon instead of being filed as
 * a picture of itself — and what stops a foreign drawing being mistaken for one, since our ten
 * silhouettes cannot express somebody else's paths.
 *
 * The match is on the **namespace URI**, not on the `pm:` prefix, which in XML is only a local
 * shorthand: an SVG binding that prefix to any other namespace is not ours, whatever it looks like.
 */
TEST(Import, OnlyAFileCarryingTheRecipeIsAdopted)
{
    // Built by hand rather than through artifactToSvg(): writing one measures its lettering, and
    // measuring text needs a QGuiApplication this target deliberately does not have. What is under test
    // is the reader's rule, and the rule is which namespace an attribute is in.
    const QByteArray ours =
        QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:pm=\"") + k_pmNamespace
        + "\" width=\"10\" height=\"10\"><g pm:shape=\"thought\" pm:box=\"300,200\""
          " pm:tails=\"40,260,30,0.2\" pm:text=\"Hello\"/></svg>";

    bool               ok   = false;
    const Artifact back = artifactFromSvg(ours, &ok);
    EXPECT_TRUE(ok);
    EXPECT_EQ(back.shape.kind, Artifact::Shape::Thought);
    EXPECT_EQ(back.box, QSize(300, 200));
    EXPECT_EQ(back.tails.items.size(), 1);
    EXPECT_EQ(back.text.body, QStringLiteral("Hello"));

    // The prefix is a local shorthand and carries no meaning: the same file with a different prefix,
    // bound to the same namespace, is still ours.
    QByteArray renamed = ours;
    renamed.replace("pm:", "x:").replace("xmlns:x=", "xmlns:x=");
    renamed.replace("xmlns:pm=", "xmlns:x=");
    ok = false;
    artifactFromSvg(renamed, &ok);
    EXPECT_TRUE(ok);

    // A drawing with no recipe: read, and declined.
    const QByteArray foreign =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">"
        "<rect width=\"10\" height=\"10\"/></svg>";
    ok = true;
    artifactFromSvg(foreign, &ok);
    EXPECT_FALSE(ok);

    // The same attributes under somebody else's namespace are somebody else's attributes.
    const QByteArray impostor =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:pm=\"https://example.invalid/ns/1\""
        " width=\"10\" height=\"10\"><g pm:shape=\"speech\" pm:box=\"10,10\"/></svg>";
    ok = true;
    artifactFromSvg(impostor, &ok);
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// The third kind
// ---------------------------------------------------------------------------

/**
 * @brief A record that names a picture **is** that picture, and none of our geometry applies to it.
 *
 * One record type carries all three kinds — lettering, a balloon, and somebody else's drawing — because
 * a second map keyed by the same uid would be a second channel carrying the same object, and the two
 * would drift. What keeps that honest is this: the moment `artwork` is set, the questions about our own
 * geometry answer *no*, whatever the other fields happen to hold.
 */
TEST(Kinds, ArtworkHasNoGeometryOfOurs)
{
    Artifact a = loadedArtifact();
    a.shape.kind   = Artifact::Shape::Speech;
    ASSERT_TRUE(a.hasSilhouette());
    ASSERT_TRUE(a.hasTail());

    a.artwork = QStringLiteral("art-0123456789abcdef.png");
    EXPECT_TRUE(a.isArtwork());
    EXPECT_FALSE(a.hasSilhouette());   // the drawing is the picture, not a shape of ours
    EXPECT_FALSE(a.hasTail());         // and nothing for a tail to leave from

    a.artwork.clear();
    EXPECT_FALSE(a.isArtwork());
    EXPECT_TRUE(a.hasSilhouette());    // ...and nothing was destroyed to say so
}

//! The kind survives being saved, like every other property — a picture that loaded as a balloon would
//! be the E6a bug arriving by a different road.
TEST(Kinds, ArtworkSurvivesTheSnapshot)
{
    Artifact a = loadedArtifact();
    a.artwork      = QStringLiteral("art-0123456789abcdef.png");

    const Artifact back = artifactFromJson(artifactToJson(a));
    EXPECT_EQ(back.artwork, a.artwork);
    EXPECT_TRUE(back.isArtwork());
    EXPECT_EQ(back, a);

    // A snapshot written before the field existed loads as what it was: something we draw.
    QJsonObject older = artifactToJson(a);
    older.remove(QStringLiteral("artwork"));
    EXPECT_FALSE(artifactFromJson(older).isArtwork());
}

/**
 * @brief A wrapper is written only when it can draw its picture, and reads back as one.
 *
 * The library hands the renderer a buffer with no base path (`vips_svgload_buffer` → librsvg), so a
 * relative `href` has nothing to resolve against: the picture is embedded or the file is useless. A
 * wrapper without it would render as the lettering alone, floating over nothing — worse than no file,
 * because it would look deliberate.
 */
TEST(Import, AWrapperWithoutItsPictureIsNotWritten)
{
    Artifact a;
    a.artwork   = QStringLiteral("art-0123456789abcdef.png");
    a.box       = QSize(200, 200);
    a.text.body = QStringLiteral("KRAK!");

    EXPECT_TRUE(artifactToSvg(a).isEmpty());                                   // no bytes at all
    EXPECT_TRUE(artifactToSvg(a, QByteArray("nonsense"), QString()).isEmpty()); // ...and no media type
}

//! A wrapper says which picture it wraps, so re-importing one brings the object back whole.
TEST(Import, AWrapperNamesItsPicture)
{
    const QByteArray wrapper =
        QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:pm=\"") + k_pmNamespace
        + "\" width=\"10\" height=\"10\"><g pm:shape=\"none\""
          " pm:artwork=\"art-0123456789abcdef.png\" pm:box=\"200,200\" pm:text=\"KRAK!\"/></svg>";

    bool               ok = false;
    const Artifact a  = artifactFromSvg(wrapper, &ok);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(a.isArtwork());
    EXPECT_FALSE(a.hasSilhouette());
    EXPECT_EQ(a.artwork, QStringLiteral("art-0123456789abcdef.png"));
    EXPECT_EQ(a.text.body, QStringLiteral("KRAK!"));
}

// ---------------------------------------------------------------------------------------------------
// Which groups a record carries. The rule ③ shows a subject by, and the rule its object menu offers a
// selection by — written out twice in the panel before it had a name, and the two copies had drifted.
// ---------------------------------------------------------------------------------------------------

using StripEdit::carriesGroup;
using StripEdit::PropertyGroup;

//! Every kind is lettered — the one group a picture and a balloon genuinely share.
TEST(Groups, TextIsCarriedByEveryKind)
{
    Artifact balloon;
    balloon.shape.kind = Artifact::Shape::Speech;

    Artifact text;
    text.shape.kind = Artifact::Shape::None;

    Artifact picture;
    picture.artwork = QStringLiteral("art-0123456789abcdef.png");

    for (const Artifact& a : {balloon, text, picture})
        EXPECT_TRUE(carriesGroup(a, PropertyGroup::Text));
}

//! Fill, line style, shape and tails all need a silhouette to sit on.
TEST(Groups, TheRestNeedASilhouette)
{
    Artifact balloon;
    balloon.shape.kind = Artifact::Shape::Speech;
    ASSERT_TRUE(balloon.hasSilhouette());

    for (auto g : {PropertyGroup::Shape, PropertyGroup::Skin, PropertyGroup::Style, PropertyGroup::Tail})
        EXPECT_TRUE(carriesGroup(balloon, g)) << "balloon, group " << int(g);

    Artifact text;
    text.shape.kind = Artifact::Shape::None;
    for (auto g : {PropertyGroup::Shape, PropertyGroup::Skin, PropertyGroup::Style, PropertyGroup::Tail})
        EXPECT_FALSE(carriesGroup(text, g)) << "shapeless, group " << int(g);
}

//! A picture carries nothing of ours to shape, fill or roughen — **whatever its shape field says**.
TEST(Groups, APictureCarriesOnlyItsLettering)
{
    Artifact picture;
    picture.artwork    = QStringLiteral("art-0123456789abcdef.png");
    picture.shape.kind = Artifact::Shape::Speech;   // a stale value; isArtwork() outranks it

    EXPECT_TRUE(carriesGroup(picture, PropertyGroup::Text));
    for (auto g : {PropertyGroup::Shape, PropertyGroup::Skin, PropertyGroup::Style, PropertyGroup::Tail})
        EXPECT_FALSE(carriesGroup(picture, g)) << "picture, group " << int(g);
}

//! A tail is not a record, so no record carries its group — nor the three that never grew an editor.
TEST(Groups, NoRecordCarriesATailsOwnGroup)
{
    const Artifact a = loadedArtifact();
    ASSERT_TRUE(a.hasSilhouette());
    ASSERT_FALSE(a.tails.items.isEmpty());

    EXPECT_FALSE(carriesGroup(a, PropertyGroup::TailItem));
    EXPECT_FALSE(carriesGroup(a, PropertyGroup::Placement));
    EXPECT_FALSE(carriesGroup(a, PropertyGroup::Size));
    EXPECT_FALSE(carriesGroup(a, PropertyGroup::Compositing));
}
