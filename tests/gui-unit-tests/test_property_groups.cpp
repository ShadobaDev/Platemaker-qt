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

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "geometry.hpp"
#include "recordsvg.hpp"
#include "shapes.hpp"
#include "styles.hpp"
#include "properties/propertygroup.hpp"
#include "objectrecord.hpp"

namespace {

//! A record with **every** property moved off its default, so an accidental write is visible.
ObjectRecord loadedRecord()
{
    ObjectRecord a;
    a.shape.kind       = ObjectRecord::Shape::Cloud;
    a.shape.cornerRadius = 33.0;
    a.shape.nodes      = {{0.1, 0.2}, {0.9, 0.15}, {0.55, 0.95}};
    a.shape.lobes      = 9;
    a.shape.lobeSize   = 33;
    a.shape.edge             = ObjectRecord::Edge::Zigzag;
    a.shape.edgePitch        = 12.5;
    a.shape.edgeDepth        = 4.5;
    a.shape.edgeIrregularity = 25;
    a.box              = QSize(321, 123);

    Tail t;
    t.tip              = QPointF(17, 42);
    t.baseWidth        = 29.0;
    t.bend             = 0.4;
    a.tails.items      = {t};

    a.style.line              = ObjectRecord::Line::Flash;
    a.texture.kind              = ObjectRecord::Texture::Rough;
    a.texture.roughFrequency    = 0.05;
    a.texture.roughOctaves      = 4;
    a.texture.roughDisplacement = 7.5;
    a.texture.roughBlur         = 1.25;
    a.style.flashOuterPitch        = 3.5;
    a.style.flashInnerPitch        = 4.5;
    a.style.flashOuterLength       = 21.0;
    a.style.flashInnerLength       = 12.5;
    a.style.flashOuterBase         = 2.5;
    a.style.flashInnerBase         = 1.5;
    a.style.flashOuterPitchJitter  = 11;
    a.style.flashInnerPitchJitter  = 12;
    a.style.flashOuterLengthJitter = 13;
    a.style.flashInnerLengthJitter = 14;
    a.style.flashCoreStretch       = 15;
    a.style.brushStrokes           = 5;
    a.style.brushOverlap           = 33;
    a.style.brushTaper             = 17;
    a.style.brushSwell             = 61;
    a.style.brushDrift             = 2.75;
    a.styleSeed        = 0xC0FFEEu;

    a.text.body        = QStringLiteral("Nie wiem, co sie dzieje");
    a.text.family      = QStringLiteral("Comic Neue");
    a.text.pixelSize   = 44;
    a.text.bold        = true;
    a.text.align       = Qt::AlignRight;
    a.text.colour      = QColor(1, 2, 3);
    a.text.lineSpacing = 85;
    a.text.padding     = -13;   // negative on purpose: an impact balloon's lettering may leave it

    a.skin.fill.colour        = QColor(4, 5, 6);
    a.style.stroke.colour     = QColor(7, 8, 9);
    a.style.stroke.kind       = ObjectRecord::Pattern::Lines;
    a.style.stroke.second     = QColor(16, 17, 18);
    a.style.stroke.spacing    = 5.5;
    a.style.stroke.size       = 1.5;
    a.style.stroke.angle      = -20;
    a.style.strokeWidth = 11;
    a.skin.fill.kind    = ObjectRecord::Pattern::Dots;
    a.skin.fill.second  = QColor(10, 11, 12);
    a.skin.fill.spacing = 7.5;
    a.skin.fill.size     = 2.5;
    a.skin.fill.angle   = 30;

    a.effects.shadow         = true;
    a.effects.shadowX        = -4.5;
    a.effects.shadowY        = 7.5;
    a.effects.shadowSoftness = 2.5;
    a.effects.shadowFill.colour   = QColor(13, 14, 15, 99);
    a.effects.shadowFill.kind     = ObjectRecord::Pattern::Grid;
    a.effects.shadowFill.second   = QColor(19, 20, 21, 77);
    a.effects.shadowFill.spacing  = 8.5;
    a.effects.shadowFill.size     = 2.0;
    a.effects.shadowFill.angle    = 15;
    return a;
}

// ---------------------------------------------------------------------------
// What this file knows each group to hold. A field added to a group, and not here, stops the build.
//
// Each mirror lists the fields expectUntouched() checks, in the group's own order, so the two structs
// are laid out by the same rules on every compiler and differ in size only when the group has grown.
// A new field has to go into: its group's fields() list (which gives it equality, the JSON and the
// `pm:*` attributes), loadedRecord(), expectUntouched() — and the mirror.
//
// ponytail: a size check. A field small enough to fit into existing padding (a bool after
// StyleProperties::kind) passes unnoticed; counting members needs C++20, or reflection.
// ---------------------------------------------------------------------------

struct ShapeMirror {
    ShapeProperties::Kind kind; qreal cornerRadius; QList<ShapeNode> nodes; int lobes, lobeSize;
    ShapeProperties::Edge edge; qreal edgePitch, edgeDepth; int edgeIrregularity;
};
static_assert(sizeof(ShapeProperties) == sizeof(ShapeMirror),
              "ShapeProperties grew a field: place it (see above), then mirror it");

struct PaintMirror { QColor colour; Paint::Kind kind; QColor second; qreal spacing, size; int angle; };
static_assert(sizeof(Paint) == sizeof(PaintMirror), "Paint grew a field: place it (see above), then mirror it");

struct SkinMirror { Paint fill; };
static_assert(sizeof(SkinProperties) == sizeof(SkinMirror),
              "SkinProperties grew a field: place it (see above), then mirror it");

struct TextureMirror {
    TextureProperties::Kind kind; qreal roughFrequency; int roughOctaves; qreal roughDisplacement, roughBlur;
};
static_assert(sizeof(TextureProperties) == sizeof(TextureMirror),
              "TextureProperties grew a field: place it (see above), then mirror it");

struct StyleMirror {
    StyleProperties::Line line; Paint stroke; int strokeWidth;
    qreal flashOuterPitch, flashInnerPitch, flashOuterLength, flashInnerLength, flashOuterBase, flashInnerBase;
    int flashOuterPitchJitter, flashInnerPitchJitter, flashOuterLengthJitter, flashInnerLengthJitter;
    int flashCoreStretch;
    int brushStrokes, brushOverlap, brushTaper, brushSwell; qreal brushDrift;
};
static_assert(sizeof(StyleProperties) == sizeof(StyleMirror),
              "StyleProperties grew a field: place it (see above), then mirror it");

struct TextMirror { QString body, family; int pixelSize; bool bold; int align; QColor colour; int lineSpacing, padding; };
static_assert(sizeof(TextProperties) == sizeof(TextMirror),
              "TextProperties grew a field: place it (see above), then mirror it");

struct TailMirror { QPointF tip; qreal baseWidth, bend; };
static_assert(sizeof(Tail) == sizeof(TailMirror),
              "Tail grew a field: place it (see above), then mirror it");

struct TailsMirror { QList<Tail> items; };
static_assert(sizeof(TailsProperties) == sizeof(TailsMirror),
              "TailsProperties grew a field: place it (see above), then mirror it");

struct EffectMirror { bool shadow; qreal shadowX, shadowY, shadowSoftness; Paint shadowFill; };
static_assert(sizeof(EffectProperties) == sizeof(EffectMirror),
              "EffectProperties grew a field: place it (see above), then mirror it");

struct RecordMirror {
    ShapeProperties shape; QString artwork; QSize box; TailsProperties tails; StyleProperties style;
    TextureProperties texture; EffectProperties effects; quint32 styleSeed; TextProperties text;
    SkinProperties skin;
};
static_assert(sizeof(ObjectRecord) == sizeof(RecordMirror),
              "ObjectRecord grew a field: place it (see above), then mirror it");


//! Every field of a Paint the same, listed one by one for the reason expectUntouched() gives.
void expectSamePaint(const Paint& after, const Paint& before, const char* which)
{
    EXPECT_EQ(after.colour, before.colour) << which;
    EXPECT_EQ(after.kind, before.kind) << which;
    EXPECT_EQ(after.second, before.second) << which;
    EXPECT_DOUBLE_EQ(after.spacing, before.spacing) << which;
    EXPECT_DOUBLE_EQ(after.size, before.size) << which;
    EXPECT_EQ(after.angle, before.angle) << which;
}

//! Which group a test is applying, so everything else can be checked as untouched.
enum class Group { Shape, Skin, Style, Texture, Effects, Text, Tails, Nothing };

/**
 * @brief Asserts that every property outside \p applied is byte-identical.
 *
 * Listed property by property rather than through the groups' own operator==, on purpose: equality is
 * itself something that can be forgotten when a property is added, and a test leaning on it would
 * weaken silently at exactly the moment it is most needed. **Adding a property to ObjectRecord breaks the
 * build above until someone has decided which group owns it**, and then this function is the next
 * place it goes.
 */
void expectUntouched(Group applied, const ObjectRecord& before, const ObjectRecord& after)
{
    if (applied != Group::Shape) {
        EXPECT_EQ(after.shape.kind, before.shape.kind);
        EXPECT_DOUBLE_EQ(after.shape.cornerRadius, before.shape.cornerRadius);
        EXPECT_TRUE(Fields::same(after.shape.nodes, before.shape.nodes));
        EXPECT_EQ(after.shape.lobes,    before.shape.lobes);
        EXPECT_EQ(after.shape.lobeSize, before.shape.lobeSize);
        EXPECT_EQ(after.shape.edge, before.shape.edge);
        EXPECT_DOUBLE_EQ(after.shape.edgePitch, before.shape.edgePitch);
        EXPECT_DOUBLE_EQ(after.shape.edgeDepth, before.shape.edgeDepth);
        EXPECT_EQ(after.shape.edgeIrregularity, before.shape.edgeIrregularity);
    }

    if (applied != Group::Skin) {
        expectSamePaint(after.skin.fill, before.skin.fill, "fill");
    }
    if (applied != Group::Style) {
        EXPECT_EQ(after.style.line,        before.style.line);
        expectSamePaint(after.style.stroke, before.style.stroke, "stroke");
        EXPECT_EQ(after.style.strokeWidth, before.style.strokeWidth);
        EXPECT_DOUBLE_EQ(after.style.flashOuterPitch,  before.style.flashOuterPitch);
        EXPECT_DOUBLE_EQ(after.style.flashInnerPitch,  before.style.flashInnerPitch);
        EXPECT_DOUBLE_EQ(after.style.flashOuterLength, before.style.flashOuterLength);
        EXPECT_DOUBLE_EQ(after.style.flashInnerLength, before.style.flashInnerLength);
        EXPECT_DOUBLE_EQ(after.style.flashOuterBase,   before.style.flashOuterBase);
        EXPECT_DOUBLE_EQ(after.style.flashInnerBase,   before.style.flashInnerBase);
        EXPECT_EQ(after.style.flashOuterPitchJitter,  before.style.flashOuterPitchJitter);
        EXPECT_EQ(after.style.flashInnerPitchJitter,  before.style.flashInnerPitchJitter);
        EXPECT_EQ(after.style.flashOuterLengthJitter, before.style.flashOuterLengthJitter);
        EXPECT_EQ(after.style.flashInnerLengthJitter, before.style.flashInnerLengthJitter);
        EXPECT_EQ(after.style.flashCoreStretch,       before.style.flashCoreStretch);
        EXPECT_EQ(after.style.brushStrokes,           before.style.brushStrokes);
        EXPECT_EQ(after.style.brushOverlap,           before.style.brushOverlap);
        EXPECT_EQ(after.style.brushTaper,             before.style.brushTaper);
        EXPECT_EQ(after.style.brushSwell,             before.style.brushSwell);
        EXPECT_DOUBLE_EQ(after.style.brushDrift,      before.style.brushDrift);
    }
    if (applied != Group::Texture) {
        EXPECT_EQ(after.texture.kind, before.texture.kind);
        EXPECT_DOUBLE_EQ(after.texture.roughFrequency,    before.texture.roughFrequency);
        EXPECT_EQ(after.texture.roughOctaves,             before.texture.roughOctaves);
        EXPECT_DOUBLE_EQ(after.texture.roughDisplacement, before.texture.roughDisplacement);
        EXPECT_DOUBLE_EQ(after.texture.roughBlur,         before.texture.roughBlur);
    }
    if (applied != Group::Effects) {
        EXPECT_EQ(after.effects.shadow, before.effects.shadow);
        EXPECT_DOUBLE_EQ(after.effects.shadowX,        before.effects.shadowX);
        EXPECT_DOUBLE_EQ(after.effects.shadowY,        before.effects.shadowY);
        EXPECT_DOUBLE_EQ(after.effects.shadowSoftness, before.effects.shadowSoftness);
        expectSamePaint(after.effects.shadowFill, before.effects.shadowFill, "shadow");
    }
    if (applied != Group::Text) {
        EXPECT_EQ(after.text.body,      before.text.body);
        EXPECT_EQ(after.text.family,    before.text.family);
        EXPECT_EQ(after.text.pixelSize, before.text.pixelSize);
        EXPECT_EQ(after.text.bold,      before.text.bold);
        EXPECT_EQ(after.text.align,     before.text.align);
        EXPECT_EQ(after.text.colour,    before.text.colour);
        EXPECT_EQ(after.text.lineSpacing, before.text.lineSpacing);
        EXPECT_EQ(after.text.padding,     before.text.padding);
    }
    if (applied != Group::Tails) {
        ASSERT_EQ(after.tails.items.size(), before.tails.items.size());
        for (int i = 0; i < after.tails.items.size(); ++i)
            EXPECT_EQ(after.tails.items.at(i), before.tails.items.at(i));
    }

    // Owned by no group, so never written by an editor and never skipped here. The box belongs to the
    // object rather than to the record's look; the style seed is per balloon and set once at
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
    const ObjectRecord before = loadedRecord();
    ObjectRecord       after  = before;

    SkinProperties s;
    s.fill.colour = QColor(200, 100, 50);
    s.fill.size   = 1.5;
    s.applyTo(after);

    EXPECT_EQ(after.skin, s);
    EXPECT_EQ(SkinProperties::from(after), s);
    expectUntouched(Group::Skin, before, after);
}

TEST(PropertyGroupOwnership, Shape)
{
    const ObjectRecord before = loadedRecord();
    ObjectRecord       after  = before;

    ShapeProperties s;
    s.kind         = ObjectRecord::Shape::Polygon;
    s.cornerRadius = 12.0;
    s.nodes        = {{0, 0}, {1, 0.5}, {0, 1}};
    s.applyTo(after);

    EXPECT_EQ(after.shape, s);
    EXPECT_EQ(ShapeProperties::from(after), s);
    expectUntouched(Group::Shape, before, after);
}

TEST(PropertyGroupOwnership, Style)
{
    const ObjectRecord before = loadedRecord();
    ObjectRecord       after  = before;

    StyleProperties s;
    s.line          = ObjectRecord::Line::Brush;
    s.stroke.colour = QColor(10, 220, 30);
    s.stroke.kind   = ObjectRecord::Pattern::Lines;
    s.strokeWidth   = 37;
    s.applyTo(after);

    EXPECT_EQ(after.style, s);
    EXPECT_EQ(StyleProperties::from(after), s);
    expectUntouched(Group::Style, before, after);
}

TEST(PropertyGroupOwnership, Texture)
{
    const ObjectRecord before = loadedRecord();
    ObjectRecord       after  = before;

    TextureProperties t;
    t.kind      = ObjectRecord::Texture::Rough;
    t.roughBlur = 0.25;
    t.applyTo(after);

    EXPECT_EQ(after.texture, t);
    EXPECT_EQ(TextureProperties::from(after), t);
    expectUntouched(Group::Texture, before, after);
}

//! The seed is part of the style and deliberately not part of its group — see StyleProperties.
TEST(PropertyGroupOwnership, StyleLeavesTheSeedAlone)
{
    ObjectRecord a = loadedRecord();
    const quint32 seed = a.styleSeed;

    StyleProperties s;
    s.line = ObjectRecord::Line::Flash;
    s.applyTo(a);
    TextureProperties t;
    t.kind = ObjectRecord::Texture::Rough;
    t.applyTo(a);

    EXPECT_EQ(a.styleSeed, seed);
}

TEST(PropertyGroupOwnership, Effects)
{
    const ObjectRecord before = loadedRecord();
    ObjectRecord       after  = before;

    EffectProperties e;
    e.shadowX           = 3.0;
    e.shadowFill.colour = QColor(1, 1, 1, 200);
    e.applyTo(after);

    EXPECT_EQ(after.effects, e);
    EXPECT_EQ(EffectProperties::from(after), e);
    expectUntouched(Group::Effects, before, after);
}

TEST(PropertyGroupOwnership, Text)
{
    const ObjectRecord before = loadedRecord();
    ObjectRecord       after  = before;

    TextProperties s;
    s.body      = QStringLiteral("Co tu sie odprawia");
    s.family    = QStringLiteral("Bangers");
    s.pixelSize = 19;
    s.bold      = false;
    s.align     = Qt::AlignLeft;
    s.colour    = QColor(90, 80, 70);
    s.lineSpacing = 140;
    s.padding     = 21;
    s.applyTo(after);

    EXPECT_EQ(after.text, s);
    EXPECT_EQ(TextProperties::from(after), s);
    expectUntouched(Group::Text, before, after);
}

TEST(PropertyGroupOwnership, Tails)
{
    const ObjectRecord before = loadedRecord();
    ObjectRecord       after  = before;

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
    ObjectRecord before = loadedRecord();
    Tail first = before.tails.items.first();
    Tail last  = first;
    last.tip       = QPointF(250, -40);
    last.baseWidth = 8.0;
    last.bend      = 0.9;
    Tail middle = first;
    middle.tip  = QPointF(90, 200);
    before.tails.items = {first, middle, last};
    ObjectRecord after = before;

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
    const ObjectRecord before = loadedRecord();   // one tail
    ObjectRecord       after  = before;

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
    ObjectRecord a = loadedRecord();
    a.text.colour  = QColor(123, 45, 67);

    SkinProperties s = SkinProperties::from(a);
    s.fill.colour = QColor(123, 45, 67);
    s.applyTo(a);

    EXPECT_EQ(a.skin.fill.colour,   QColor(123, 45, 67));
    EXPECT_EQ(a.text.colour, QColor(123, 45, 67));

    TextProperties t = TextProperties::from(a);
    t.colour = QColor(9, 9, 9);
    t.applyTo(a);

    EXPECT_EQ(a.skin.fill.colour,   QColor(123, 45, 67));   // still the balloon's
    EXPECT_EQ(a.text.colour, QColor(9, 9, 9));
}

// ---------------------------------------------------------------------------
// Persistence — the structs changed, the file format did not
// ---------------------------------------------------------------------------

TEST(PropertyGroupPersistence, KeepsTheFlatKeys)
{
    const ObjectRecord a = loadedRecord();
    const QJsonObject  j = a.toJson();

    EXPECT_EQ(j.value(QStringLiteral("strokeWidth")).toInt(),  a.style.strokeWidth);
    EXPECT_EQ(j.value(QStringLiteral("fill")).toString(),      a.skin.fill.colour.name(QColor::HexArgb));
    EXPECT_EQ(j.value(QStringLiteral("stroke")).toString(),    a.style.stroke.colour.name(QColor::HexArgb));
    EXPECT_EQ(j.value(QStringLiteral("fillKind")).toString(),  QStringLiteral("dots"));
    EXPECT_EQ(j.value(QStringLiteral("text")).toString(),      a.text.body);
    EXPECT_EQ(j.value(QStringLiteral("fontFamily")).toString(),a.text.family);
    EXPECT_EQ(j.value(QStringLiteral("fontSize")).toInt(),     a.text.pixelSize);
    EXPECT_EQ(j.value(QStringLiteral("align")).toInt(),        a.text.align);
    EXPECT_EQ(j.value(QStringLiteral("lineSpacing")).toInt(),  a.text.lineSpacing);
    EXPECT_EQ(j.value(QStringLiteral("padding")).toInt(),      a.text.padding);
    EXPECT_EQ(j.value(QStringLiteral("texture")).toString(),   QStringLiteral("rough"));

    // No grouping crept into the file: a record saved before the groups existed still loads.
    for (const char* nested : {"skin", "shape", "style", "tails"})
        EXPECT_FALSE(j.value(QLatin1String(nested)).isObject()) << nested;
}

TEST(PropertyGroupPersistence, RoundTrips)
{
    ObjectRecord a = loadedRecord();
    // A Curve's node with handles among a Polygon's plain ones, so both forms are saved and read.
    a.shape.nodes.insert(1, ShapeNode({0.5, 0.1}, {0.05, -0.1}, {-0.02, 0.2}, ShapeNode::Type::Smooth));
    const ObjectRecord b = ObjectRecord::fromJson(a.toJson());

    EXPECT_EQ(b.shape, a.shape);
    EXPECT_EQ(b.skin,  a.skin);
    EXPECT_EQ(b.style, a.style);
    EXPECT_EQ(b.text,  a.text);
    EXPECT_EQ(b.tails, a.tails);
    EXPECT_EQ(b, a);
}

/**
 * @brief **Every** shape and every line style comes back from both files it is saved to — the undo
 *        snapshot's JSON and the overlay's SVG.
 *
 * Both read a kind back by its name — the JSON's `shape` and the SVG's `pm:shape` go through the same
 * shapeFromName() — and a reader that stops short of the last kind loads whatever lies past it as the
 * fallback: a balloon appended after Banner came back from every file as a speech balloon. RoundTrips
 * above tries one shape, which is how that went unseen.
 *
 * The kinds go through the name both readers share. The templates are written to the SVG as well, with
 * no lettering: writing lettering measures it, which needs the QGuiApplication this target does not
 * have (see the import tests below).
 */
TEST(PropertyGroupPersistence, EveryShapeAndStyleRoundTrips)
{
    QList<ObjectRecord::Shape> shapes{ObjectRecord::Shape::None};
    shapes += shapeOrder();
    for (ObjectRecord::Shape s : shapes) {
        EXPECT_EQ(shapeFromName(QString::fromLatin1(shapeName(s))), s) << shapeName(s);
        ObjectRecord a   = loadedRecord();
        a.shape.kind = s;
        EXPECT_EQ(ObjectRecord::fromJson(a.toJson()), a) << shapeName(s);
    }

    // Every template, from the SVG too (with no lettering, which would need a font): a placed one must
    // read back as itself, or its tile stops being current once the file is reopened.
    for (const Geometry::ShapeTemplate& t : Geometry::shapeTemplates()) {
        ObjectRecord a = loadedRecord();
        a.shape        = t.shape;
        a.text.body.clear();
        bool ok = false;
        EXPECT_EQ(Svg::read(Svg::write(a), &ok).shape, t.shape) << t.id;
        EXPECT_EQ(ObjectRecord::fromJson(a.toJson()), a) << t.id;
    }

    for (int i = 0; i <= int(StyleProperties::k_lastLine); ++i) {
        const auto l = static_cast<ObjectRecord::Line>(i);
        EXPECT_EQ(lineFromName(QString::fromLatin1(lineName(l))), l) << lineName(l);
        ObjectRecord a = loadedRecord();
        a.style.line   = l;
        EXPECT_EQ(ObjectRecord::fromJson(a.toJson()), a) << lineName(l);
    }
    for (int i = 0; i <= int(TextureProperties::k_lastKind); ++i) {
        const auto t = static_cast<ObjectRecord::Texture>(i);
        EXPECT_EQ(textureFromName(QString::fromLatin1(textureName(t))), t) << textureName(t);
        ObjectRecord a = loadedRecord();
        a.texture.kind = t;
        EXPECT_EQ(ObjectRecord::fromJson(a.toJson()), a) << textureName(t);
    }
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
    ObjectRecord a = loadedRecord();
    a.shape.kind   = ObjectRecord::Shape::Box;
    ASSERT_FALSE(a.tails.items.isEmpty());
    ASSERT_TRUE(a.hasTail());

    const TailsProperties kept = a.tails;

    a.shape.kind = ObjectRecord::Shape::None;   // what the conversion does, and all it does
    EXPECT_FALSE(a.hasSilhouette());            // the one structural question, and its whole answer
    EXPECT_FALSE(a.hasTail());                  // no balloon, so nothing for a tail to leave
    EXPECT_EQ(a.tails, kept);                   // ...but the record still has them

    a.shape.kind = ObjectRecord::Shape::Ellipse;
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
    EXPECT_FALSE(shapeOrder().contains(ObjectRecord::Shape::None));
    // Every enumerated shape except None, and each of them once.
    EXPECT_EQ(shapeOrder().size(), int(ShapeProperties::k_lastKind));
    for (ObjectRecord::Shape s : shapeOrder()) {
        EXPECT_FALSE(shapeTitle(s).isEmpty()) << shapeName(s);
        EXPECT_EQ(shapeOrder().count(s), 1) << shapeName(s);
    }
    // Named all the same, because a kind still has to be spelled somewhere.
    EXPECT_FALSE(shapeTitle(ObjectRecord::Shape::None).isEmpty());
}

/**
 * @brief Every shape has its own entry in the shape table (shapes.cpp), in the enum's order, with a
 *        name, a title, an outline and a text area — except `None`, which has no outline to draw. So
 *        has every line (strokes for each but Clean) and every texture (a filter's reach for each but
 *        None), in styles.cpp.
 *
 * The tables are indexed by the enum, so an entry out of order would give a shape another's geometry;
 * and an appended shape with no entry would be drawn as the Box shapeInfo() falls back to. Every
 * template is a balloon, named once, found by templateOf() as itself, and every kind has one.
 */
TEST(Conversion, EveryShapeAndStyleHasATableEntry)
{
    for (int i = 0; i <= int(StyleProperties::k_lastLine); ++i) {
        const auto                kind = static_cast<ObjectRecord::Line>(i);
        const Geometry::LineInfo& info = Geometry::lineInfo(kind);
        EXPECT_EQ(info.kind, kind) << "line entry " << i << " is out of the enum's order";
        EXPECT_NE(info.name, nullptr) << i;
        EXPECT_NE(info.title, nullptr) << i;
        // Every line but Clean has strokes of its own, and strokes reserve room.
        EXPECT_EQ(info.ink == nullptr, kind == ObjectRecord::Line::Clean) << lineName(kind);
        EXPECT_EQ(info.ink == nullptr, info.inkReach == nullptr) << lineName(kind);
    }
    for (int i = 0; i <= int(TextureProperties::k_lastKind); ++i) {
        const auto                   kind = static_cast<ObjectRecord::Texture>(i);
        const Geometry::TextureInfo& info = Geometry::textureInfo(kind);
        EXPECT_EQ(info.kind, kind) << "texture entry " << i << " is out of the enum's order";
        EXPECT_NE(info.name, nullptr) << i;
        EXPECT_NE(info.title, nullptr) << i;
        // Every texture but None is a filter, and a filter reserves room.
        EXPECT_EQ(info.reach == nullptr, kind == ObjectRecord::Texture::None) << textureName(kind);
    }

    for (int i = 0; i <= int(ShapeProperties::k_lastKind); ++i) {
        const auto                 kind = static_cast<ObjectRecord::Shape>(i);
        const Geometry::ShapeInfo& info = Geometry::shapeInfo(kind);
        EXPECT_EQ(info.kind, kind) << "entry " << i << " is out of the enum's order";
        EXPECT_NE(info.name, nullptr) << i;
        EXPECT_NE(info.title, nullptr) << i;
        EXPECT_NE(info.safeArea, nullptr) << i;
        EXPECT_EQ(info.outline == nullptr, kind == ObjectRecord::Shape::None) << shapeName(kind);
    }

    QList<ObjectRecord::Shape> templated;
    for (const Geometry::ShapeTemplate& t : Geometry::shapeTemplates()) {
        EXPECT_NE(t.shape.kind, ObjectRecord::Shape::None) << t.id;
        EXPECT_NE(t.title, nullptr) << t.id;
        EXPECT_EQ(Geometry::templateOf(t.shape), &t) << t.id << " equals an earlier template";
        EXPECT_EQ(&Geometry::shapeTemplate(t.id), &t) << t.id << " is not the only one of its id";
        templated << t.shape.kind;
    }
    for (ObjectRecord::Shape s : shapeOrder())
        EXPECT_TRUE(templated.contains(s)) << shapeName(s) << " has no template, so no tile";
}

/**
 * @brief A flash and a brush draw no solid outline, stay inside the room reserved for them, and are the
 *        same strokes for the same seed and others for another (PLAN-L P7).
 *
 * The bounds are the buffer and the viewBox, so a stroke past them is clipped off in the render. The
 * strokes are random, so several seeds, at the largest jitter, swell and drift, on shapes with corners
 * and a tail.
 */
TEST(Style, ALinesStrokesStayInsideTheirBounds)
{
    for (const char* id : {"ellipse", "caption", "speech"}) {
        ObjectRecord a;
        a.shape = Geometry::shapeTemplate(id).shape;
        a.box   = QSize(280, 160);
        const ObjectRecord::Shape shape = a.shape.kind;
        if (qstrcmp(id, "speech") == 0) {
            Tail t;
            t.tip = QPointF(60, 230);
            a.tails.items = {t};
        }
        a.style.flashOuterPitchJitter  = 100;
        a.style.flashOuterLengthJitter = 100;
        a.style.brushSwell             = 100;
        a.style.brushDrift             = 6.0;
        a.style.strokeWidth             = 9;
        for (ObjectRecord::Line line : {ObjectRecord::Line::Flash, ObjectRecord::Line::Brush}) {
            a.style.line = line;
            EXPECT_FALSE(Geometry::hasOutline(a));
            EXPECT_FALSE(Geometry::hasFilter(a));

            for (int stretch : {StyleProperties::k_autoStretch, 0, 100}) {
                for (quint32 seed : {1u, 2u, 0xC0FFEEu, 0xFFFFFFFFu}) {
                    a.style.flashCoreStretch = stretch;
                    a.styleSeed              = seed;
                    const QPainterPath ink = Geometry::ink(a, Geometry::silhouette(a));
                    ASSERT_FALSE(ink.isEmpty()) << id << " " << lineName(line);
                    EXPECT_TRUE(Geometry::bounds(a).contains(ink.boundingRect()))
                        << id << " (" << shapeName(shape) << ") " << lineName(line) << " stretch " << stretch
                        << " seed " << seed;
                }
            }

            a.styleSeed             = 7;
            const QPainterPath once = Geometry::ink(a, Geometry::silhouette(a));
            EXPECT_TRUE(once == Geometry::ink(a, Geometry::silhouette(a))) << id << " " << lineName(line);
            a.styleSeed = 8;
            EXPECT_FALSE(once == Geometry::ink(a, Geometry::silhouette(a))) << id << " " << lineName(line);
        }
    }
}

//! A flash left on Auto points its strokes exactly as the shape's own stretch would (PLAN-S A18, A19).
TEST(Style, AutoStretchIsTheShapes)
{
    for (int i = 0; i <= int(ShapeProperties::k_lastKind); ++i) {
        ObjectRecord a;
        a.shape.kind = static_cast<ObjectRecord::Shape>(i);
        a.box        = QSize(300, 140);
        a.style.line = ObjectRecord::Line::Flash;
        a.styleSeed  = 7;
        const int own = Geometry::shapeInfo(a.shape.kind).flashStretch;
        EXPECT_GE(own, 0) << shapeName(a.shape.kind);
        EXPECT_LE(own, 100) << shapeName(a.shape.kind);

        ASSERT_EQ(a.style.flashCoreStretch, StyleProperties::k_autoStretch);   // the default is Auto
        const QPainterPath autoInk = Geometry::ink(a, Geometry::silhouette(a));
        a.style.flashCoreStretch   = own;
        EXPECT_TRUE(autoInk == Geometry::ink(a, Geometry::silhouette(a))) << shapeName(a.shape.kind);
    }
}

/**
 * @brief A tail leaves a concave outline from the crossing nearest its tip (PLAN-L B12).
 *
 * A box with a notch cut down into its top: the line from the centre to a tip up and to the left runs
 * through the notch, back into the left prong, and out of the prong's top. The tail is seen to leave
 * the prong, so it must start there, and the notch it passed over stays empty. A search outwards from the
 * centre stopped at the notch's floor and drew the tail across the gap.
 */
TEST(Tails, AConcaveOutlineIsLeftNearestTheTip)
{
    ObjectRecord a;
    a.shape.kind         = ObjectRecord::Shape::Polygon;
    a.shape.cornerRadius = 0.0;
    a.shape.nodes = {{0, 0}, {0.3, 0}, {0.3, 0.4}, {0.7, 0.4}, {0.7, 0}, {1, 0}, {1, 1}, {0, 1}};
    a.box              = QSize(200, 200);
    a.style.strokeWidth = 0;   // the balloon is then the box, and the nodes are these pixels
    Tail t;
    t.tip         = QPointF(-40, -200);
    a.tails.items = {t};

    ASSERT_TRUE(Geometry::tailsDrawn(a).value(0));
    const QPointF inTheNotch(72, 40);   // on the line, between the notch's floor and the prong
    ASSERT_FALSE(Geometry::outlineOf(a, Geometry::balloonRect(a)).contains(inTheNotch));
    EXPECT_FALSE(Geometry::silhouette(a).contains(inTheNotch));
    EXPECT_TRUE(Geometry::silhouette(a).contains(QPointF(16, -80)));   // ...and the tail is there, on its line
}

/**
 * @brief A Polygon's corner radius rounds as a Box's does, and never past half an edge.
 *
 * A square Polygon and a Box of the same radius are the same outline: a press in a corner falls outside
 * both, and one just inside the arc inside both. A radius past what the edges allow stops at half the
 * shorter one, so the outline stays in its box.
 */
TEST(Shapes, APolygonsCornersRoundAsABoxsDo)
{
    ObjectRecord box;
    box.shape.kind         = ObjectRecord::Shape::Box;
    box.shape.cornerRadius = 30.0;
    box.box                = QSize(200, 100);
    box.style.strokeWidth   = 0;
    ObjectRecord square = box;
    square.shape.kind   = ObjectRecord::Shape::Polygon;
    square.shape.nodes  = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    const QRectF       body = Geometry::balloonRect(box);
    const QPainterPath a    = Geometry::outlineOf(box, body);
    const QPainterPath b    = Geometry::outlineOf(square, body);
    for (const QPointF& p : {QPointF(3, 3), QPointF(12, 12), QPointF(197, 97), QPointF(100, 1), QPointF(1, 50)})
        EXPECT_EQ(a.contains(p), b.contains(p)) << p.x() << "," << p.y();
    EXPECT_FALSE(b.contains(QPointF(3, 3)));   // the corner is cut, not bulged

    square.shape.cornerRadius = 1000.0;
    const QRectF drawn = Geometry::outlineOf(square, body).boundingRect();
    EXPECT_TRUE(body.adjusted(-0.01, -0.01, 0.01, 0.01).contains(drawn));
}

/**
 * @brief Shape editing's handles (PLAN-L P3, P4, P8): one dragged onto where it already is changes
 *        nothing, and a Polygon's nodes are added in the middle of an edge and removed down to three,
 *        never fewer.
 *
 * The drag is the inverse of where a handle is drawn; a pair that disagreed would make a handle jump
 * the moment it is pressed. Every template, plain and with a zigzag edge (whose depth arrow any kind
 * offers, an Ellipse too), with its radius at 0 and off it, and with each node picked in turn, so a
 * Curve's tangent handles are dragged too (a Smooth or Symmetric node's other handle is put back where
 * its type says, which must be where it was).
 */
TEST(Shapes, AHandleDraggedOntoItselfChangesNothing)
{
    const QRectF body(5, 5, 270, 150);
    const qreal  clearance = 12.0;
    for (const Geometry::ShapeTemplate& t : Geometry::shapeTemplates()) {
        for (ObjectRecord::Edge edge : {ObjectRecord::Edge::None, ObjectRecord::Edge::Zigzag}) {
            ShapeProperties shape = t.shape;
            shape.edge            = edge;
            if (!Geometry::hasShapeHandles(shape))
                continue;
            for (qreal radius : {0.0, 20.0}) {
                shape.cornerRadius = radius;
                for (int picked = -1; picked < int(shape.nodes.size()); ++picked) {
                    for (const Geometry::ShapeHandle& h : Geometry::shapeHandles(shape, body, clearance, picked)) {
                        if (h.role == Geometry::ShapeHandle::Role::AddVertex)
                            continue;   // a press adds a node; it has nothing to drag
                        const ShapeProperties dragged = Geometry::dragShapeHandle(shape, body, h, h.at, clearance);
                        EXPECT_TRUE(dragged == shape) << t.id << " edge " << edgeName(edge) << " radius " << radius
                                                      << " picked " << picked << " handle " << int(h.role) << ":"
                                                      << h.index;
                    }
                }
            }
        }
    }

    ShapeProperties diamond = Geometry::shapeTemplate("diamond").shape;
    const ShapeProperties five = Geometry::withVertexAdded(diamond, 0);
    ASSERT_EQ(five.nodes.size(), 5);
    const QPointF middle = (diamond.nodes.at(0).at + diamond.nodes.at(1).at) / 2.0;
    EXPECT_NEAR(five.nodes.at(1).at.x(), middle.x(), 1e-9);
    EXPECT_NEAR(five.nodes.at(1).at.y(), middle.y(), 1e-9);
    diamond = Geometry::withVertexRemoved(diamond, 0);
    EXPECT_EQ(diamond.nodes.size(), 3);
    EXPECT_TRUE(Geometry::withVertexRemoved(diamond, 0) == diamond);   // three is the fewest
}

/**
 * @brief An edge goes in from the outline (PLAN-L P8): the balloon keeps its bounds, the lettering's room
 *        gives up the depth, a tile is still current under it, and the same seed lays the same teeth.
 */
TEST(Shapes, AnEdgeGoesInAndTheLetteringGivesUpItsDepth)
{
    for (const char* id : {"speech", "ellipse", "shout", "scroll", "thought"}) {
        ObjectRecord plain;
        plain.shape = Geometry::shapeTemplate(id).shape;
        plain.box   = QSize(300, 160);
        ObjectRecord edged           = plain;
        edged.shape.edge             = ObjectRecord::Edge::Zigzag;
        edged.shape.edgeDepth        = 9.0;
        edged.shape.edgeIrregularity = 50;
        edged.styleSeed              = 7;

        const QRectF body = Geometry::balloonRect(plain);
        const QRectF bare = Geometry::outlineOf(plain, body).boundingRect();
        EXPECT_TRUE(bare.adjusted(-0.01, -0.01, 0.01, 0.01).contains(Geometry::outlineOf(edged, body).boundingRect()))
            << id;
        const QRectF a = Geometry::textArea(plain);
        const QRectF b = Geometry::textArea(edged);
        EXPECT_NEAR(b.width(), a.width() - 2 * 9.0, 1e-6) << id;
        EXPECT_NEAR(b.height(), a.height() - 2 * 9.0, 1e-6) << id;
        EXPECT_EQ(Geometry::templateOf(edged.shape), &Geometry::shapeTemplate(id)) << id;

        const QPainterPath once = Geometry::outlineOf(edged, body);
        EXPECT_TRUE(once == Geometry::outlineOf(edged, body)) << id;
        edged.styleSeed = 8;
        EXPECT_FALSE(once == Geometry::outlineOf(edged, body)) << id;
    }
}

/**
 * @brief A shadow is the silhouette moved, inside the room reserved for it, and only a soft one is a filter
 *        (PLAN-L P9): a hard one is drawn as it is edited, a soft one by the library.
 */
TEST(Effects, AShadowStaysInItsBoundsAndOnlyASoftOneIsAFilter)
{
    ObjectRecord a;
    a.shape = Geometry::shapeTemplate("speech").shape;
    Tail t;
    t.tip         = QPointF(60, 230);
    a.tails.items = {t};
    EXPECT_TRUE(Geometry::shadowOf(a, Geometry::silhouette(a)).isEmpty());

    a.effects.shadow = true;
    for (const QPointF offset : {QPointF(12, 9), QPointF(-20, -3), QPointF(0, 30)}) {
        for (qreal softness : {0.0, 4.0}) {
            a.effects.shadowX        = offset.x();
            a.effects.shadowY        = offset.y();
            a.effects.shadowSoftness = softness;
            const QPainterPath silhouette = Geometry::silhouette(a);
            const QPainterPath shadow     = Geometry::shadowOf(a, silhouette);
            EXPECT_TRUE(shadow == silhouette.translated(offset));
            // The blur reaches three deviations past the moved shape.
            const qreal  spread = 3.0 * softness;
            const QRectF reach  = shadow.boundingRect().adjusted(-spread, -spread, spread, spread);
            EXPECT_TRUE(Geometry::bounds(a).contains(reach)) << offset.x() << "," << offset.y() << " soft " << softness;
            EXPECT_EQ(Geometry::hasFilter(a), softness > 0.0);
        }
    }
}

namespace {

/**
 * How many of a grid of points over @p body one outline holds and the other does not. The grid is offset
 * off whole pixels, so no point sits on an edge both outlines share, where rounding could go either way.
 */
int pointsInOnlyOne(const QPainterPath& a, const QPainterPath& b, const QRectF& body)
{
    constexpr int n      = 60;
    int           differ = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            const QPointF p(body.left() + (i + 0.37) * body.width() / n, body.top() + (j + 0.61) * body.height() / n);
            differ += a.contains(p) != b.contains(p) ? 1 : 0;
        }
    return differ;
}

}  // namespace

/**
 * @brief A Curve keeps its outline through the edits that should not change it (PLAN-L P4): *Convert to
 *        Curve* from a Box, an Ellipse, a rounded Polygon and a Cloud, in a node per piece; a segment split by its "+"; and a node made
 *        Corner. A node made Symmetric gets two handles on one line, as long as each other.
 */
TEST(Shapes, ACurveKeepsItsOutlineThroughItsEdits)
{
    const QRectF body(5, 5, 270, 150);
    ShapeProperties rounded = Geometry::shapeTemplate("shout").shape;
    rounded.cornerRadius    = 6.0;
    for (const ShapeProperties& from : {Geometry::shapeTemplate("speech").shape, Geometry::shapeTemplate("ellipse").shape,
                                        rounded, Geometry::shapeTemplate("thought").shape}) {
        const QPainterPath    before = Geometry::shapeInfo(from.kind).outline(from, body);
        const ShapeProperties curve  = Geometry::curveOf(before, body);
        EXPECT_EQ(curve.kind, ObjectRecord::Shape::Curve);
        EXPECT_EQ(pointsInOnlyOne(before, Geometry::shapeInfo(curve.kind).outline(curve, body), body), 0)
            << shapeName(from.kind);
        // A node per cubic the path has, not a polygon: a cloud's lobe is a few arc pieces.
        if (from.kind == ObjectRecord::Shape::Cloud)
            EXPECT_LE(curve.nodes.size(), 4 * from.lobes);
    }
    // An ellipse's four quarters meet smoothly, each node's handles as long as each other.
    const ShapeProperties round = Geometry::curveOf(
        Geometry::shapeInfo(ObjectRecord::Shape::Ellipse).outline(Geometry::shapeTemplate("ellipse").shape, body), body);
    ASSERT_EQ(round.nodes.size(), 4);
    for (const ShapeNode& node : round.nodes)
        EXPECT_EQ(node.type, ShapeNode::Type::Symmetric);

    const auto            outline = Geometry::shapeInfo(ObjectRecord::Shape::Curve).outline;
    const ShapeProperties scroll  = Geometry::shapeTemplate("scroll").shape;
    for (int edge = 0; edge < scroll.nodes.size(); ++edge) {
        const ShapeProperties split = Geometry::withVertexAdded(scroll, edge);
        ASSERT_EQ(split.nodes.size(), scroll.nodes.size() + 1);
        EXPECT_EQ(pointsInOnlyOne(outline(scroll, body), outline(split, body), body), 0) << "edge " << edge;
    }
    const ShapeProperties corner = Geometry::withNodeType(scroll, body, 2, ShapeNode::Type::Corner);
    EXPECT_EQ(pointsInOnlyOne(outline(scroll, body), outline(corner, body), body), 0);

    // Node 0 is a corner whose handles point two ways; Symmetric puts them on one line, of one length.
    const ShapeNode node = Geometry::withNodeType(scroll, body, 0, ShapeNode::Type::Symmetric).nodes.at(0);
    const QPointF   in(node.in.x() * body.width(), node.in.y() * body.height());
    const QPointF   out(node.out.x() * body.width(), node.out.y() * body.height());
    EXPECT_NEAR(in.x() + out.x(), 0.0, 1e-9);
    EXPECT_NEAR(in.y() + out.y(), 0.0, 1e-9);
    EXPECT_EQ(node.type, ShapeNode::Type::Symmetric);
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
    // Built by hand rather than through Svg::write(): writing one measures its lettering, and
    // measuring text needs a QGuiApplication this target deliberately does not have. What is under test
    // is the reader's rule, and the rule is which namespace an attribute is in.
    const QByteArray ours =
        QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:pm=\"") + Svg::k_pmNamespace
        + "\" width=\"10\" height=\"10\"><g pm:shape=\"cloud\" pm:box=\"300,200\""
          " pm:tails=\"40,260,30,0.2\" pm:text=\"Hello\" pm:padding=\"-6\" pm:lineSpacing=\"80\"/></svg>";

    bool               ok   = false;
    const ObjectRecord back = Svg::read(ours, &ok);
    EXPECT_TRUE(ok);
    EXPECT_EQ(back.shape.kind, ObjectRecord::Shape::Cloud);
    EXPECT_EQ(back.box, QSize(300, 200));
    EXPECT_EQ(back.tails.items.size(), 1);
    EXPECT_EQ(back.text.body, QStringLiteral("Hello"));
    EXPECT_EQ(back.text.padding, -6);
    EXPECT_EQ(back.text.lineSpacing, 80);

    // The prefix is a local shorthand and carries no meaning: the same file with a different prefix,
    // bound to the same namespace, is still ours.
    QByteArray renamed = ours;
    renamed.replace("pm:", "x:").replace("xmlns:x=", "xmlns:x=");
    renamed.replace("xmlns:pm=", "xmlns:x=");
    ok = false;
    Svg::read(renamed, &ok);
    EXPECT_TRUE(ok);

    // A drawing with no recipe: read, and declined.
    const QByteArray foreign =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">"
        "<rect width=\"10\" height=\"10\"/></svg>";
    ok = true;
    Svg::read(foreign, &ok);
    EXPECT_FALSE(ok);

    // The same attributes under somebody else's namespace are somebody else's attributes.
    const QByteArray impostor =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:pm=\"https://example.invalid/ns/1\""
        " width=\"10\" height=\"10\"><g pm:shape=\"speech\" pm:box=\"10,10\"/></svg>";
    ok = true;
    Svg::read(impostor, &ok);
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
    ObjectRecord a = loadedRecord();
    a.shape.kind   = ObjectRecord::Shape::Box;
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
//! be a default balloon written over imported artwork, arriving by a different road.
TEST(Kinds, ArtworkSurvivesTheSnapshot)
{
    ObjectRecord a = loadedRecord();
    a.artwork      = QStringLiteral("art-0123456789abcdef.png");

    const ObjectRecord back = ObjectRecord::fromJson(a.toJson());
    EXPECT_EQ(back.artwork, a.artwork);
    EXPECT_TRUE(back.isArtwork());
    EXPECT_EQ(back, a);

    // A snapshot written before the field existed loads as what it was: something we draw.
    QJsonObject older = a.toJson();
    older.remove(QStringLiteral("artwork"));
    EXPECT_FALSE(ObjectRecord::fromJson(older).isArtwork());
}

/**
 * @brief A wrapper is written only when it can draw its picture, and reads back as one.
 *
 * The library hands the renderer a buffer with no base path (`vips_svgload_buffer` → librsvg), so a
 * relative `href` has nothing to resolve against: the picture is embedded or the file is useless. A
 * wrapper without it would render as the lettering alone, floating over nothing — worse than no file,
 * because it would look deliberate.
 */
TEST(Import, AFileBakedInAStandInSaysWhich)
{
    // pm:fontFamily is the font asked for and stays so; pm:fontFallback is what the outlines were set in
    // while it was missing. The heal at open reads only the second — by namespace, like the recipe.
    const QByteArray standIn =
        QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:pm=\"") + Svg::k_pmNamespace
        + "\" width=\"10\" height=\"10\"><g pm:shape=\"speech\" pm:box=\"10,10\" pm:text=\"Hi\""
          " pm:fontFamily=\"Segoe Print\" pm:fontFallback=\"Segoe UI\"/></svg>";
    EXPECT_EQ(Svg::bakedFontFallback(standIn), QStringLiteral("Segoe UI"));
    EXPECT_EQ(Svg::read(standIn).text.family, QStringLiteral("Segoe Print"));

    QByteArray ownFont = standIn;
    ownFont.replace(" pm:fontFallback=\"Segoe UI\"", "");
    EXPECT_TRUE(Svg::bakedFontFallback(ownFont).isEmpty());

    // Somebody else's namespace says nothing about our files.
    QByteArray impostor = standIn;
    impostor.replace(Svg::k_pmNamespace, "https://example.invalid/ns/1");
    EXPECT_TRUE(Svg::bakedFontFallback(impostor).isEmpty());
}

TEST(Import, AWrapperWithoutItsPictureIsNotWritten)
{
    ObjectRecord a;
    a.artwork   = QStringLiteral("art-0123456789abcdef.png");
    a.box       = QSize(200, 200);
    a.text.body = QStringLiteral("KRAK!");

    EXPECT_TRUE(Svg::write(a).isEmpty());                                   // no bytes at all
    EXPECT_TRUE(Svg::write(a, QByteArray("nonsense"), QString()).isEmpty()); // ...and no media type
}

//! A wrapper says which picture it wraps, so re-importing one brings the object back whole.
TEST(Import, AWrapperNamesItsPicture)
{
    const QByteArray wrapper =
        QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:pm=\"") + Svg::k_pmNamespace
        + "\" width=\"10\" height=\"10\"><g pm:shape=\"none\""
          " pm:artwork=\"art-0123456789abcdef.png\" pm:box=\"200,200\" pm:text=\"KRAK!\"/></svg>";

    bool               ok = false;
    const ObjectRecord a  = Svg::read(wrapper, &ok);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(a.isArtwork());
    EXPECT_FALSE(a.hasSilhouette());
    EXPECT_EQ(a.artwork, QStringLiteral("art-0123456789abcdef.png"));
    EXPECT_EQ(a.text.body, QStringLiteral("KRAK!"));
}

// ---------------------------------------------------------------------------------------------------
// Which groups a record carries. The rule OBJECT STATE shows a subject by, and the rule its object menu
// offers a selection by — written out twice in the panel before it had a name, and the two copies had
// drifted.
// ---------------------------------------------------------------------------------------------------

using StripEdit::carriesGroup;
using StripEdit::PropertyGroup;

//! Every kind is lettered — the one group a picture and a balloon genuinely share.
TEST(Groups, TextIsCarriedByEveryKind)
{
    ObjectRecord balloon;
    balloon.shape.kind = ObjectRecord::Shape::Box;

    ObjectRecord text;
    text.shape.kind = ObjectRecord::Shape::None;

    ObjectRecord picture;
    picture.artwork = QStringLiteral("art-0123456789abcdef.png");

    for (const ObjectRecord& a : {balloon, text, picture})
        EXPECT_TRUE(carriesGroup(a, PropertyGroup::Text));
}

//! Fill, line style, shape and tails all need a silhouette to sit on.
TEST(Groups, TheRestNeedASilhouette)
{
    ObjectRecord balloon;
    balloon.shape.kind = ObjectRecord::Shape::Box;
    ASSERT_TRUE(balloon.hasSilhouette());

    for (auto g : {PropertyGroup::Shape, PropertyGroup::Skin, PropertyGroup::Style, PropertyGroup::Tail})
        EXPECT_TRUE(carriesGroup(balloon, g)) << "balloon, group " << int(g);

    ObjectRecord text;
    text.shape.kind = ObjectRecord::Shape::None;
    for (auto g : {PropertyGroup::Shape, PropertyGroup::Skin, PropertyGroup::Style, PropertyGroup::Tail})
        EXPECT_FALSE(carriesGroup(text, g)) << "shapeless, group " << int(g);
}

//! A picture carries nothing of ours to shape, fill or roughen — **whatever its shape field says**.
TEST(Groups, APictureCarriesOnlyItsLettering)
{
    ObjectRecord picture;
    picture.artwork    = QStringLiteral("art-0123456789abcdef.png");
    picture.shape.kind = ObjectRecord::Shape::Box;   // a stale value; isArtwork() outranks it

    EXPECT_TRUE(carriesGroup(picture, PropertyGroup::Text));
    for (auto g : {PropertyGroup::Shape, PropertyGroup::Skin, PropertyGroup::Style, PropertyGroup::Tail})
        EXPECT_FALSE(carriesGroup(picture, g)) << "picture, group " << int(g);
}

//! A tail is not a record, so no record carries its group — nor the three that never grew an editor.
TEST(Groups, NoRecordCarriesATailsOwnGroup)
{
    const ObjectRecord a = loadedRecord();
    ASSERT_TRUE(a.hasSilhouette());
    ASSERT_FALSE(a.tails.items.isEmpty());

    EXPECT_FALSE(carriesGroup(a, PropertyGroup::TailItem));
    EXPECT_FALSE(carriesGroup(a, PropertyGroup::Placement));
    EXPECT_FALSE(carriesGroup(a, PropertyGroup::Size));
    EXPECT_FALSE(carriesGroup(a, PropertyGroup::Compositing));
}

// ---------------------------------------------------------------------------
// Field lists
// ---------------------------------------------------------------------------

namespace {

//! A different value of the same field — for each type a group holds.
void bump(int& v) { v += 7; }
void bump(bool& v) { v = !v; }
void bump(qreal& v) { v += 0.25; }
void bump(QString& v) { v += QStringLiteral("x"); }
void bump(QColor& v) { v = QColor(v.red() ^ 0x55, v.green() ^ 0x55, v.blue() ^ 0x55, v.alpha()); }
void bump(QList<ShapeNode>& v) { v.append(ShapeNode(0.25, 0.75)); }
void bump(ObjectRecord::Pattern& v)
{
    v = static_cast<ObjectRecord::Pattern>((int(v) + 1) % (int(Paint::k_lastKind) + 1));
}
void bump(ObjectRecord::Edge& v)
{
    v = static_cast<ObjectRecord::Edge>((int(v) + 1) % (int(ShapeProperties::k_lastEdge) + 1));
}
void bump(ObjectRecord::Shape& v)
{
    v = static_cast<ObjectRecord::Shape>((int(v) + 1) % (int(ShapeProperties::k_lastKind) + 1));
}
void bump(ObjectRecord::Line& v)
{
    v = static_cast<ObjectRecord::Line>((int(v) + 1) % (int(StyleProperties::k_lastLine) + 1));
}
void bump(ObjectRecord::Texture& v)
{
    v = static_cast<ObjectRecord::Texture>((int(v) + 1) % (int(TextureProperties::k_lastKind) + 1));
}

/**
 * @brief For every field of the group at \p group: changing it alone makes the group unequal, and the
 *        changed record comes back the same from the JSON and from the SVG.
 *
 * The SVG is written with no lettering, because writing lettering measures a font, which needs the
 * QGuiApplication this target does not have; the body's own `pm:text` is read by the import test.
 */
template <class Group>
void expectEveryFieldCounts(Group ObjectRecord::*group)
{
    Group::fields([&](const char* key, auto member) {
        ObjectRecord a = loadedRecord();
        a.text.body.clear();
        ObjectRecord b = a;
        bump(Fields::at(b.*group, member));

        EXPECT_FALSE(a.*group == b.*group) << key << ": not compared";
        EXPECT_EQ(ObjectRecord::fromJson(b.toJson()), b) << key << ": not in the JSON";
        if (b.text.body.isEmpty()) {
            bool ok = false;
            EXPECT_EQ(Svg::read(Svg::write(b), &ok), b) << key << ": not in the SVG";
            EXPECT_TRUE(ok) << key;
        }
    });
}

} // namespace

TEST(Fields, EveryFieldIsComparedAndSaved)
{
    expectEveryFieldCounts(&ObjectRecord::shape);
    expectEveryFieldCounts(&ObjectRecord::text);
    expectEveryFieldCounts(&ObjectRecord::skin);
    expectEveryFieldCounts(&ObjectRecord::style);
    expectEveryFieldCounts(&ObjectRecord::texture);
    expectEveryFieldCounts(&ObjectRecord::effects);
}

//! Every key a record is saved under is its own: two fields sharing one would overwrite each other.
TEST(Fields, EveryKeyIsUnique)
{
    // The record's own, written by hand, and then every group's list.
    QStringList keys{QStringLiteral("artwork"), QStringLiteral("w"), QStringLiteral("h"),
                     QStringLiteral("box"), QStringLiteral("tails"), QStringLiteral("styleSeed"),
                     QStringLiteral("fontFallback")};
    const auto collect = [&](const char* key, auto) { keys << QString::fromLatin1(key); };
    ShapeProperties::fields(collect);
    TextProperties::fields(collect);
    SkinProperties::fields(collect);
    StyleProperties::fields(collect);
    for (const QString& k : keys)
        EXPECT_EQ(keys.count(k), 1) << k.toStdString();
}

// ---------------------------------------------------------------------------
// Golden files
// ---------------------------------------------------------------------------

namespace {

//! Where the golden file lives, in the source tree, so a regenerated one shows up in `git diff`.
QString goldenPath()
{
    return QStringLiteral(PLATEMAKER_GOLDEN_DIR "/records.txt");
}

/**
 * @brief Everything a record is written as, for every template (and no shape) × look × with and without
 *        a tail. A case is named by its template's id and its look: clean, marker and ink (Rough at the
 *        Marker's and the Ink's numbers), flash, a rough flash, and brush.
 *
 * The balloon's SVG, its text area and its undo-snapshot JSON, one section per case. The lettering is
 * left empty and its family named, so nothing here measures a font: writing lettering needs the
 * QGuiApplication this target does not have, and the text area covers what the lettering is fitted to.
 */
QString goldenRecords()
{
    ShapeProperties none = loadedRecord().shape;
    none.kind            = ObjectRecord::Shape::None;
    QList<QPair<QString, ShapeProperties>> shapes{{QStringLiteral("none"), none}};
    for (const Geometry::ShapeTemplate& t : Geometry::shapeTemplates())
        shapes.append({QString::fromLatin1(t.id), t.shape});
    // A zigzag edge on one template of each kind that draws a line or a curve (PLAN-L P8).
    for (const char* id : {"speech", "ellipse", "shout", "scroll"}) {
        ShapeProperties zigzag = Geometry::shapeTemplate(id).shape;
        zigzag.edge            = ObjectRecord::Edge::Zigzag;
        shapes.append({QString::fromLatin1(id) + QStringLiteral(" zigzag"), zigzag});
    }

    // The looks: a line over the loaded record's flash and brush numbers, and a texture, Rough at a
    // look's own numbers (the Marker's are the struct's defaults).
    struct Look
    {
        QString           name;
        StyleProperties   style;
        TextureProperties texture;
    };
    StyleProperties clean = loadedRecord().style;
    clean.line            = ObjectRecord::Line::Clean;
    clean.stroke.kind     = ObjectRecord::Pattern::Solid;   // the patterns have cases of their own, below
    StyleProperties flash = clean;
    flash.line            = ObjectRecord::Line::Flash;
    StyleProperties brush = clean;
    brush.line            = ObjectRecord::Line::Brush;
    TextureProperties untextured = loadedRecord().texture;   // its Rough numbers carried, unused
    untextured.kind              = ObjectRecord::Texture::None;
    TextureProperties marker;
    marker.kind = ObjectRecord::Texture::Rough;
    const QList<Look> looks{
        {QStringLiteral("clean"), clean, untextured},
        {QStringLiteral("marker"), clean, marker},
        {QStringLiteral("ink"), clean, Geometry::inkRough(untextured)},
        {QStringLiteral("flash"), flash, untextured},
        {QStringLiteral("rough flash"), flash, marker},
        {QStringLiteral("brush"), brush, untextured},
    };

    QString    out;
    const auto write = [&out](const QString& name, const QString& look, bool tail, ObjectRecord a) {
        a.text.body.clear();
        if (!tail)
            a.tails.items.clear();
        const QRectF area = Geometry::textArea(a);
        out += QStringLiteral("=== %1 / %2 / %3 ===\n")
                   .arg(name, look, tail ? QStringLiteral("tail") : QStringLiteral("no tail"));
        out += QString::fromUtf8(Svg::write(a));
        out += QStringLiteral("\ntextArea: %1 %2 %3 %4\n")
                   .arg(area.x(), 0, 'f', 3).arg(area.y(), 0, 'f', 3)
                   .arg(area.width(), 0, 'f', 3).arg(area.height(), 0, 'f', 3);
        out += QStringLiteral("json: ")
             + QString::fromUtf8(QJsonDocument(a.toJson()).toJson(QJsonDocument::Compact)) + u'\n';
    };
    for (const auto& [name, shape] : std::as_const(shapes)) {
        for (const Look& look : looks) {
            for (bool tail : {false, true}) {
                ObjectRecord a  = loadedRecord();
                a.shape         = shape;
                a.style         = look.style;
                a.texture       = look.texture;
                a.skin.fill.kind = ObjectRecord::Pattern::Solid;   // the fills and the shadow have cases of
                a.effects.shadow = false;                       // their own, below
                write(name, look.name, tail, a);
            }
        }
    }
    // A screentone fill on two templates, clean (PLAN-L P9).
    for (const char* id : {"speech", "shout"}) {
        for (bool tail : {false, true}) {
            ObjectRecord a = loadedRecord();
            a.shape        = Geometry::shapeTemplate(id).shape;
            a.style        = looks.first().style;
            a.texture      = looks.first().texture;
            a.effects.shadow = false;
            write(QString::fromLatin1(id), QStringLiteral("screentone"), tail, a);
        }
    }
    // A shadow, hard and soft, on two templates (PLAN-L P9).
    for (const char* id : {"speech", "shout"}) {
        for (qreal softness : {0.0, 2.5}) {
            ObjectRecord a           = loadedRecord();
            a.shape                  = Geometry::shapeTemplate(id).shape;
            a.style                  = looks.first().style;
            a.texture                = looks.first().texture;
            a.skin.fill.kind          = ObjectRecord::Pattern::Solid;
            a.effects.shadowSoftness  = softness;
            a.effects.shadowFill.kind = ObjectRecord::Pattern::Solid;
            write(QString::fromLatin1(id), softness > 0.0 ? QStringLiteral("soft shadow") : QStringLiteral("shadow"),
                  true, a);
        }
    }
    // Every pattern in every place it paints (PLAN-L P11): the fill, a thick line, and a shadow.
    for (int k = 1; k <= int(Paint::k_lastKind); ++k) {
        const auto pattern = static_cast<ObjectRecord::Pattern>(k);
        ObjectRecord a     = loadedRecord();
        a.shape            = Geometry::shapeTemplate("speech").shape;
        a.style            = looks.first().style;
        a.texture          = looks.first().texture;
        a.skin.fill.kind   = pattern;
        a.style.stroke     = a.skin.fill;
        a.style.stroke.colour = QColor(7, 8, 9);
        a.style.stroke.second = QColor(240, 240, 240);
        a.style.strokeWidth   = 14;
        a.effects.shadowFill.kind = pattern;
        write(QStringLiteral("speech"), QStringLiteral("pattern ") + QLatin1String(patternName(pattern)), true, a);
    }
    return out;
}

} // namespace

/**
 * @brief What a record is written as does not change unless it is meant to — **byte for byte**.
 *
 * The guard for refactoring the geometry and the persistence (PLAN-S): moving a shape's path, a style's
 * filter or a property's key to another file must leave every file written exactly as it was. A change
 * that is meant is accepted by regenerating the golden file and reviewing its diff: run the tests with
 * `PLATEMAKER_UPDATE_GOLDEN=1` in the environment.
 */
TEST(Golden, EveryShapeAndStyleIsWrittenAsBefore)
{
    const QString actual = goldenRecords();
    QFile         file(goldenPath());

    if (qEnvironmentVariableIsSet("PLATEMAKER_UPDATE_GOLDEN")) {
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate)) << goldenPath().toStdString();
        file.write(actual.toUtf8());
        GTEST_SKIP() << "golden file written: " << goldenPath().toStdString();
    }

    ASSERT_TRUE(file.open(QIODevice::ReadOnly))
        << goldenPath().toStdString() << " is missing: run once with PLATEMAKER_UPDATE_GOLDEN=1";
    const QStringList want = QString::fromUtf8(file.readAll()).split(u'\n');
    const QStringList got  = actual.split(u'\n');

    // The first differing line and the case it is in, rather than two 400 KB strings.
    QString section;
    for (qsizetype i = 0; i < qMax(want.size(), got.size()); ++i) {
        const QString w = i < want.size() ? want[i] : QStringLiteral("<end of file>");
        const QString g = i < got.size() ? got[i] : QStringLiteral("<end of output>");
        if (w.startsWith(QStringLiteral("=== ")))
            section = w;
        ASSERT_EQ(g.toStdString(), w.toStdString())
            << "first difference at line " << i + 1 << ", in " << section.toStdString();
    }
}
