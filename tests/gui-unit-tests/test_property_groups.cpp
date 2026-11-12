/**
 * \file
 * \brief Property-group ownership: applying a group writes that group and nothing else.
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

#include <algorithm>
#include <optional>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "edgewalk.hpp"
#include "geometry.hpp"
#include "recordsvg.hpp"
#include "shapes.hpp"
#include "styles.hpp"
#include "properties/propertygroup.hpp"
#include "objectparts.hpp"
#include "objectrecord.hpp"

namespace {

//! A record with **every** property moved off its default, so an accidental write is visible.
ObjectRecord loadedRecord()
{
    ObjectRecord a;
    shapeOf(a).kind       = ObjectRecord::Shape::Cloud;
    shapeOf(a).cornerRadius = 33.0;
    shapeOf(a).nodes      = {{0.1, 0.2}, {0.9, 0.15}, {0.55, 0.95}};
    shapeOf(a).lobes      = 9;
    shapeOf(a).lobeSize   = 33;
    shapeOf(a).bow             = 12;
    shapeOf(a).bowIrregularity = 37;
    shapeOf(a).edge             = ObjectRecord::Edge::Zigzag;
    shapeOf(a).edgePitch        = 12.5;
    shapeOf(a).edgeDepth        = 4.5;
    shapeOf(a).edgeIrregularity = 25;
    boxOf(a)              = QSize(321, 123);

    Tail t;
    t.tip              = QPointF(17, 42);
    t.baseWidth        = 29.0;
    t.bend             = 0.4;
    t.look.circles     = 4;      // Plain still: the numbers are carried, the render is the plain tail's
    t.look.pitch       = 7.5;
    t.look.offset      = -3.25;
    t.look.tipWidth    = 45;     // Sharp still: carried, unused
    t.look.jog         = 35;     // no breaks: carried, unused
    t.look.clockwise   = false;
    tailsOf(a).items      = {t};
    tailsOf(a).nextLook.style   = TailLook::Style::Circles;
    tailsOf(a).nextLook.circles = 5;
    tailsOf(a).nextLook.pitch   = 2.5;
    tailsOf(a).nextLook.offset  = 4.0;
    tailsOf(a).nextLook.tip      = TailLook::Tip::Round;
    tailsOf(a).nextLook.tipWidth = 60;
    tailsOf(a).nextLook.breaks   = 2;
    tailsOf(a).nextLook.jog      = 15;

    styleOf(a).line              = ObjectRecord::Line::Flash;
    textureOf(a).kind              = ObjectRecord::Texture::Rough;
    textureOf(a).roughFrequency    = 0.05;
    textureOf(a).roughOctaves      = 4;
    textureOf(a).roughDisplacement = 7.5;
    textureOf(a).roughBlur         = 1.25;
    textureOf(a).roughFill         = true;   // not the default; the parts are a golden case of their own
    styleOf(a).flashOuterPitch        = 3.5;
    styleOf(a).flashInnerPitch        = 4.5;
    styleOf(a).flashOuterLength       = 21.0;
    styleOf(a).flashInnerLength       = 12.5;
    styleOf(a).flashOuterBase         = 2.5;
    styleOf(a).flashInnerBase         = 1.5;
    styleOf(a).flashOuterPitchJitter  = 11;
    styleOf(a).flashInnerPitchJitter  = 12;
    styleOf(a).flashOuterLengthJitter = 13;
    styleOf(a).flashInnerLengthJitter = 14;
    styleOf(a).flashCoreStretch       = 15;
    styleOf(a).brushStrokes           = 5;
    styleOf(a).brushOverlap           = 33;
    styleOf(a).brushTaper             = 17;
    styleOf(a).brushSwell             = 61;
    styleOf(a).brushDrift             = 2.75;
    styleOf(a).drybrushCore           = 31;
    styleOf(a).drybrushBristles       = 9;
    styleOf(a).drybrushDryness        = 42;
    styleOf(a).dashLength             = 9.5;
    styleOf(a).dashGap                = 4.25;
    styleOf(a).multiLines             = 3;
    styleOf(a).sketchLength           = 33.5;
    styleOf(a).sketchLayers           = 3;
    styleOf(a).sketchStiffness        = 47;
    styleOf(a).sketchSpread           = 1.25;
    styleOf(a).sketchEntry            = 7;
    styleOf(a).sketchExit             = 52;
    styleOf(a).sketchCore             = 31;
    styleOf(a).sketchStrokeWidth      = 44;
    styleOf(a).sketchClockwise        = false;
    styleOf(a).sketchBlots            = 23;
    styleOf(a).dryInkGrain            = 0.25;
    styleOf(a).dryInkWear             = 55;
    styleOf(a).dryInkCore             = 15;
    styleOf(a).multiGap              = 6.5;
    styleOf(a).multiWidth            = 1.75;
    seedOf(a)        = 0xC0FFEEu;
    originOf(a)     = {QStringLiteral("uid-loaded"), QStringLiteral("Loaded")};

    textOf(a).body        = QStringLiteral("Nie wiem, co sie dzieje");
    textOf(a).family      = QStringLiteral("Comic Neue");
    textOf(a).pixelSize   = 44;
    textOf(a).bold        = true;
    textOf(a).align       = Qt::AlignRight;
    textOf(a).fill.colour = QColor(1, 2, 3);
    textOf(a).fill.kind   = ObjectRecord::Pattern::Lines;
    textOf(a).fill.second = QColor(30, 31, 32);
    textOf(a).fill.spacing = 3.5;
    textOf(a).fill.size   = 1.75;
    textOf(a).fill.angle  = 15;
    textOf(a).lineSpacing = 85;
    textOf(a).paddingSide = -13;   // negative on purpose: an impact balloon's lettering may leave it
    textOf(a).paddingTop  = 17;    // and not the side's, so the two are told apart
    textOf(a).outline.colour  = QColor(21, 22, 23);
    textOf(a).outline.kind    = ObjectRecord::Pattern::Dots;
    textOf(a).outline.second  = QColor(24, 25, 26);
    textOf(a).outline.spacing = 4.5;
    textOf(a).outline.size    = 1.25;
    textOf(a).outline.angle   = -35;
    textOf(a).outlineWidth    = 2.5;
    textOf(a).shadow.on         = true;
    textOf(a).shadow.spread     = 3.5;
    textOf(a).shadow.x          = 1.5;
    textOf(a).shadow.y          = -2.25;
    textOf(a).shadow.softness   = 4.0;
    textOf(a).shadowFill.colour = QColor(27, 28, 29, 150);
    textOf(a).shadowFill.kind   = ObjectRecord::Pattern::Grid;
    textOf(a).shadowFill.spacing = 6.5;

    skinOf(a).fill.colour        = QColor(4, 5, 6);
    styleOf(a).stroke.colour     = QColor(7, 8, 9);
    styleOf(a).stroke.kind       = ObjectRecord::Pattern::Lines;
    styleOf(a).stroke.second     = QColor(16, 17, 18);
    styleOf(a).stroke.spacing    = 5.5;
    styleOf(a).stroke.size       = 1.5;
    styleOf(a).stroke.angle      = -20;
    styleOf(a).strokeWidth = 11;
    skinOf(a).fill.kind    = ObjectRecord::Pattern::Dots;
    skinOf(a).fill.second  = QColor(10, 11, 12);
    skinOf(a).fill.spacing = 7.5;
    skinOf(a).fill.size     = 2.5;
    skinOf(a).fill.angle   = 30;

    effectsOf(a).shadow.on         = true;
    effectsOf(a).shadow.spread   = 0.0;   // the hard shadow first drawn, so the golden's older cases stay as they were
    effectsOf(a).shadow.x        = -4.5;
    effectsOf(a).shadow.y        = 7.5;
    effectsOf(a).shadow.softness = 2.5;
    effectsOf(a).shadowFill.colour   = QColor(13, 14, 15, 99);
    effectsOf(a).shadowFill.kind     = ObjectRecord::Pattern::Grid;
    effectsOf(a).shadowFill.spacing  = 8.5;
    effectsOf(a).shadowFill.size     = 2.0;
    effectsOf(a).shadowFill.angle    = 15;
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
    ShapeProperties::Kind kind; qreal cornerRadius; QList<ShapeNode> nodes; int lobes, lobeSize, bow, bowIrregularity;
    ShapeProperties::Edge edge; qreal edgePitch, edgeDepth; int edgeIrregularity;
};
static_assert(sizeof(ShapeProperties) == sizeof(ShapeMirror),
              "ShapeProperties grew a field: place it (see above), then mirror it");

struct PaintMirror {
    QColor colour; Paint::Kind kind; QColor second; qreal spacing, size; int angle;
    qreal fadeLeftFrom, fadeLeftTo, fadeRightFrom, fadeRightTo, fadeTopFrom, fadeTopTo, fadeBottomFrom, fadeBottomTo;
    qreal fadeCentreX, fadeCentreY; bool fadeReversed, fadeRound;
};
static_assert(sizeof(Paint) == sizeof(PaintMirror), "Paint grew a field: place it (see above), then mirror it");

struct SkinMirror { Paint fill; };
static_assert(sizeof(SkinProperties) == sizeof(SkinMirror),
              "SkinProperties grew a field: place it (see above), then mirror it");

struct TextureMirror {
    TextureProperties::Kind kind; qreal roughFrequency; int roughOctaves; qreal roughDisplacement, roughBlur;
    bool roughFill;
};
static_assert(sizeof(TextureProperties) == sizeof(TextureMirror),
              "TextureProperties grew a field: place it (see above), then mirror it");

struct StyleMirror {
    StyleProperties::Line line; Paint stroke; int strokeWidth;
    qreal flashOuterPitch, flashInnerPitch, flashOuterLength, flashInnerLength, flashOuterBase, flashInnerBase;
    int flashOuterPitchJitter, flashInnerPitchJitter, flashOuterLengthJitter, flashInnerLengthJitter;
    int flashCoreStretch;
    int brushStrokes, brushOverlap, brushTaper, brushSwell; qreal brushDrift;
    int drybrushCore, drybrushBristles, drybrushDryness;
    qreal dashLength, dashGap;
    int multiLines; qreal multiGap, multiWidth;
    qreal sketchLength; int sketchLayers, sketchStiffness; qreal sketchSpread; int sketchEntry, sketchExit;
    int sketchCore, sketchStrokeWidth; bool sketchClockwise; int sketchBlots;
    qreal dryInkGrain; int dryInkWear, dryInkCore;
};
static_assert(sizeof(StyleProperties) == sizeof(StyleMirror),
              "StyleProperties grew a field: place it (see above), then mirror it");

struct TextMirror { QString body, family; int pixelSize; bool bold; int align; Paint fill; int lineSpacing, paddingSide, paddingTop;
                    Paint outline; qreal outlineWidth; Shadow shadow; Paint shadowFill; };
static_assert(sizeof(TextProperties) == sizeof(TextMirror),
              "TextProperties grew a field: place it (see above), then mirror it");

struct TailLookMirror { TailLook::Style style; int circles; qreal pitch, offset; TailLook::Tip tip; int tipWidth, breaks, jog; bool clockwise; };
static_assert(sizeof(TailLook) == sizeof(TailLookMirror),
              "TailLook grew a field: place it (see above), then mirror it");

struct TailMirror { QPointF tip; qreal baseWidth, bend; TailLook look; };
static_assert(sizeof(Tail) == sizeof(TailMirror),
              "Tail grew a field: place it (see above), then mirror it");

struct TailsMirror { QList<Tail> items; TailLook nextLook; };
static_assert(sizeof(TailsProperties) == sizeof(TailsMirror),
              "TailsProperties grew a field: place it (see above), then mirror it");

struct ShadowMirror { bool on; qreal spread, x, y, softness; };
static_assert(sizeof(Shadow) == sizeof(ShadowMirror),
              "Shadow grew a field: place it (see above), then mirror it");

struct EffectMirror { Shadow shadow; Paint shadowFill; };
static_assert(sizeof(EffectProperties) == sizeof(EffectMirror),
              "EffectProperties grew a field: place it (see above), then mirror it");

struct RecordMirror {
    ShapeProperties shape; QString artwork; QSize box; TailsProperties tails; StyleProperties style;
    TextureProperties texture; EffectProperties effects; quint32 styleSeed; TextProperties text;
    SkinProperties skin; PresetOrigin presetOrigin;
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
    EXPECT_DOUBLE_EQ(after.fadeLeftFrom, before.fadeLeftFrom) << which;
    EXPECT_DOUBLE_EQ(after.fadeLeftTo, before.fadeLeftTo) << which;
    EXPECT_DOUBLE_EQ(after.fadeRightFrom, before.fadeRightFrom) << which;
    EXPECT_DOUBLE_EQ(after.fadeRightTo, before.fadeRightTo) << which;
    EXPECT_DOUBLE_EQ(after.fadeTopFrom, before.fadeTopFrom) << which;
    EXPECT_DOUBLE_EQ(after.fadeTopTo, before.fadeTopTo) << which;
    EXPECT_DOUBLE_EQ(after.fadeBottomFrom, before.fadeBottomFrom) << which;
    EXPECT_DOUBLE_EQ(after.fadeBottomTo, before.fadeBottomTo) << which;
    EXPECT_DOUBLE_EQ(after.fadeCentreX, before.fadeCentreX) << which;
    EXPECT_DOUBLE_EQ(after.fadeCentreY, before.fadeCentreY) << which;
    EXPECT_EQ(after.fadeReversed, before.fadeReversed) << which;
    EXPECT_EQ(after.fadeRound, before.fadeRound) << which;
}

//! Which group a test is applying, so everything else can be checked as untouched.
enum class Group { Shape, Skin, Style, Texture, Effects, Text, Tails, Nothing };

/**
 * \brief Asserts that every property outside \p applied is byte-identical.
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
        EXPECT_EQ(shapeOf(after).kind, shapeOf(before).kind);
        EXPECT_DOUBLE_EQ(shapeOf(after).cornerRadius, shapeOf(before).cornerRadius);
        EXPECT_TRUE(Fields::same(shapeOf(after).nodes, shapeOf(before).nodes));
        EXPECT_EQ(shapeOf(after).lobes,    shapeOf(before).lobes);
        EXPECT_EQ(shapeOf(after).lobeSize, shapeOf(before).lobeSize);
        EXPECT_EQ(shapeOf(after).bow,             shapeOf(before).bow);
        EXPECT_EQ(shapeOf(after).bowIrregularity, shapeOf(before).bowIrregularity);
        EXPECT_EQ(shapeOf(after).edge, shapeOf(before).edge);
        EXPECT_DOUBLE_EQ(shapeOf(after).edgePitch, shapeOf(before).edgePitch);
        EXPECT_DOUBLE_EQ(shapeOf(after).edgeDepth, shapeOf(before).edgeDepth);
        EXPECT_EQ(shapeOf(after).edgeIrregularity, shapeOf(before).edgeIrregularity);
    }

    if (applied != Group::Skin) {
        expectSamePaint(skinOf(after).fill, skinOf(before).fill, "fill");
    }
    if (applied != Group::Style) {
        EXPECT_EQ(styleOf(after).line,        styleOf(before).line);
        expectSamePaint(styleOf(after).stroke, styleOf(before).stroke, "stroke");
        EXPECT_EQ(styleOf(after).strokeWidth, styleOf(before).strokeWidth);
        EXPECT_DOUBLE_EQ(styleOf(after).flashOuterPitch,  styleOf(before).flashOuterPitch);
        EXPECT_DOUBLE_EQ(styleOf(after).flashInnerPitch,  styleOf(before).flashInnerPitch);
        EXPECT_DOUBLE_EQ(styleOf(after).flashOuterLength, styleOf(before).flashOuterLength);
        EXPECT_DOUBLE_EQ(styleOf(after).flashInnerLength, styleOf(before).flashInnerLength);
        EXPECT_DOUBLE_EQ(styleOf(after).flashOuterBase,   styleOf(before).flashOuterBase);
        EXPECT_DOUBLE_EQ(styleOf(after).flashInnerBase,   styleOf(before).flashInnerBase);
        EXPECT_EQ(styleOf(after).flashOuterPitchJitter,  styleOf(before).flashOuterPitchJitter);
        EXPECT_EQ(styleOf(after).flashInnerPitchJitter,  styleOf(before).flashInnerPitchJitter);
        EXPECT_EQ(styleOf(after).flashOuterLengthJitter, styleOf(before).flashOuterLengthJitter);
        EXPECT_EQ(styleOf(after).flashInnerLengthJitter, styleOf(before).flashInnerLengthJitter);
        EXPECT_EQ(styleOf(after).flashCoreStretch,       styleOf(before).flashCoreStretch);
        EXPECT_EQ(styleOf(after).brushStrokes,           styleOf(before).brushStrokes);
        EXPECT_EQ(styleOf(after).brushOverlap,           styleOf(before).brushOverlap);
        EXPECT_EQ(styleOf(after).brushTaper,             styleOf(before).brushTaper);
        EXPECT_EQ(styleOf(after).brushSwell,             styleOf(before).brushSwell);
        EXPECT_DOUBLE_EQ(styleOf(after).brushDrift,      styleOf(before).brushDrift);
        EXPECT_EQ(styleOf(after).drybrushCore,           styleOf(before).drybrushCore);
        EXPECT_EQ(styleOf(after).drybrushBristles,       styleOf(before).drybrushBristles);
        EXPECT_EQ(styleOf(after).drybrushDryness,        styleOf(before).drybrushDryness);
        EXPECT_DOUBLE_EQ(styleOf(after).dashLength,      styleOf(before).dashLength);
        EXPECT_DOUBLE_EQ(styleOf(after).dashGap,         styleOf(before).dashGap);
        EXPECT_EQ(styleOf(after).multiLines,             styleOf(before).multiLines);
        EXPECT_DOUBLE_EQ(styleOf(after).sketchLength,    styleOf(before).sketchLength);
        EXPECT_EQ(styleOf(after).sketchLayers,           styleOf(before).sketchLayers);
        EXPECT_EQ(styleOf(after).sketchStiffness,        styleOf(before).sketchStiffness);
        EXPECT_DOUBLE_EQ(styleOf(after).sketchSpread,    styleOf(before).sketchSpread);
        EXPECT_EQ(styleOf(after).sketchEntry,            styleOf(before).sketchEntry);
        EXPECT_EQ(styleOf(after).sketchExit,             styleOf(before).sketchExit);
        EXPECT_EQ(styleOf(after).sketchCore,             styleOf(before).sketchCore);
        EXPECT_EQ(styleOf(after).sketchStrokeWidth,      styleOf(before).sketchStrokeWidth);
        EXPECT_EQ(styleOf(after).sketchClockwise,        styleOf(before).sketchClockwise);
        EXPECT_EQ(styleOf(after).sketchBlots,            styleOf(before).sketchBlots);
        EXPECT_DOUBLE_EQ(styleOf(after).dryInkGrain,     styleOf(before).dryInkGrain);
        EXPECT_EQ(styleOf(after).dryInkWear,             styleOf(before).dryInkWear);
        EXPECT_EQ(styleOf(after).dryInkCore,             styleOf(before).dryInkCore);
        EXPECT_DOUBLE_EQ(styleOf(after).multiGap,       styleOf(before).multiGap);
        EXPECT_DOUBLE_EQ(styleOf(after).multiWidth,     styleOf(before).multiWidth);
    }
    if (applied != Group::Texture) {
        EXPECT_EQ(textureOf(after).kind, textureOf(before).kind);
        EXPECT_DOUBLE_EQ(textureOf(after).roughFrequency,    textureOf(before).roughFrequency);
        EXPECT_EQ(textureOf(after).roughOctaves,             textureOf(before).roughOctaves);
        EXPECT_DOUBLE_EQ(textureOf(after).roughDisplacement, textureOf(before).roughDisplacement);
        EXPECT_DOUBLE_EQ(textureOf(after).roughBlur,         textureOf(before).roughBlur);
        EXPECT_EQ(textureOf(after).roughFill,                textureOf(before).roughFill);
    }
    if (applied != Group::Effects) {
        EXPECT_EQ(effectsOf(after).shadow.on, effectsOf(before).shadow.on);
        EXPECT_DOUBLE_EQ(effectsOf(after).shadow.spread,   effectsOf(before).shadow.spread);
        EXPECT_DOUBLE_EQ(effectsOf(after).shadow.x,        effectsOf(before).shadow.x);
        EXPECT_DOUBLE_EQ(effectsOf(after).shadow.y,        effectsOf(before).shadow.y);
        EXPECT_DOUBLE_EQ(effectsOf(after).shadow.softness, effectsOf(before).shadow.softness);
        expectSamePaint(effectsOf(after).shadowFill, effectsOf(before).shadowFill, "shadow");
    }
    if (applied != Group::Text) {
        EXPECT_EQ(textOf(after).body,      textOf(before).body);
        EXPECT_EQ(textOf(after).family,    textOf(before).family);
        EXPECT_EQ(textOf(after).pixelSize, textOf(before).pixelSize);
        EXPECT_EQ(textOf(after).bold,      textOf(before).bold);
        EXPECT_EQ(textOf(after).align,     textOf(before).align);
        expectSamePaint(textOf(after).fill, textOf(before).fill, "text");
        EXPECT_EQ(textOf(after).lineSpacing, textOf(before).lineSpacing);
        EXPECT_EQ(textOf(after).paddingSide, textOf(before).paddingSide);
        EXPECT_EQ(textOf(after).paddingTop,  textOf(before).paddingTop);
        expectSamePaint(textOf(after).outline, textOf(before).outline, "text outline");
        EXPECT_DOUBLE_EQ(textOf(after).outlineWidth, textOf(before).outlineWidth);
        EXPECT_EQ(textOf(after).shadow, textOf(before).shadow);
        expectSamePaint(textOf(after).shadowFill, textOf(before).shadowFill, "text shadow");
    }
    if (applied != Group::Tails) {
        ASSERT_EQ(tailsOf(after).items.size(), tailsOf(before).items.size());
        for (int i = 0; i < tailsOf(after).items.size(); ++i)
            EXPECT_EQ(tailsOf(after).items.at(i), tailsOf(before).items.at(i));
    }

    // Owned by no group, so never written by an editor and never skipped here. The box belongs to the
    // object rather than to the record's look; the style seed is per balloon and set once at
    // placement, which is why a preset must not carry it; the preset origin is set only by applying one.
    EXPECT_EQ(boxOf(after),          boxOf(before));
    EXPECT_EQ(seedOf(after),    seedOf(before));
    EXPECT_EQ(originOf(after), originOf(before));
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

    EXPECT_EQ(skinOf(after), s);
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

    EXPECT_EQ(shapeOf(after), s);
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

    EXPECT_EQ(styleOf(after), s);
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

    EXPECT_EQ(textureOf(after), t);
    EXPECT_EQ(TextureProperties::from(after), t);
    expectUntouched(Group::Texture, before, after);
}

//! The seed is part of the style and deliberately not part of its group — see StyleProperties.
TEST(PropertyGroupOwnership, StyleLeavesTheSeedAlone)
{
    ObjectRecord a = loadedRecord();
    const quint32 seed = seedOf(a);

    StyleProperties s;
    s.line = ObjectRecord::Line::Flash;
    s.applyTo(a);
    TextureProperties t;
    t.kind = ObjectRecord::Texture::Rough;
    t.applyTo(a);

    EXPECT_EQ(seedOf(a), seed);
}

TEST(PropertyGroupOwnership, Effects)
{
    const ObjectRecord before = loadedRecord();
    ObjectRecord       after  = before;

    EffectProperties e;
    e.shadow.x           = 3.0;
    e.shadowFill.colour = QColor(1, 1, 1, 200);
    e.applyTo(after);

    EXPECT_EQ(effectsOf(after), e);
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
    s.fill.colour = QColor(90, 80, 70);
    s.fill.kind   = ObjectRecord::Pattern::Dots;
    s.lineSpacing = 140;
    s.paddingSide = 21;
    s.paddingTop  = -4;
    s.outline.colour = QColor(60, 50, 40);
    s.outline.kind   = ObjectRecord::Pattern::Lines;
    s.outlineWidth   = 3.5;
    s.shadow.on      = true;
    s.shadow.x       = -6.0;
    s.shadowFill.colour = QColor(9, 8, 7, 100);
    s.applyTo(after);

    EXPECT_EQ(textOf(after), s);
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

    EXPECT_EQ(tailsOf(after), s);
    EXPECT_EQ(TailsProperties::from(after), s);
    expectUntouched(Group::Tails, before, after);
}

/**
 * \brief One tail is a group of its own: editing tail 2 leaves tail 1 and tail 3 as they were.
 *
 * The reason tails became objects. When one editor wrote a width and bend onto every tail, a balloon with
 * two differently shaped tails lost the difference the next time anything else about it was edited.
 */
TEST(PropertyGroupOwnership, OneTail)
{
    ObjectRecord before = loadedRecord();
    Tail first = tailsOf(before).items.first();
    Tail last  = first;
    last.tip       = QPointF(250, -40);
    last.baseWidth = 8.0;
    last.bend      = 0.9;
    Tail middle = first;
    middle.tip  = QPointF(90, 200);
    tailsOf(before).items = {first, middle, last};
    ObjectRecord after = before;

    TailProperties edited = TailProperties::from(before, 1);
    edited.tail.baseWidth = 77.0;
    edited.tail.bend      = -0.6;
    edited.applyTo(after);

    ASSERT_EQ(tailsOf(after).items.size(), 3);
    EXPECT_EQ(tailsOf(after).items.at(0), tailsOf(before).items.at(0));
    EXPECT_EQ(tailsOf(after).items.at(1), edited.tail);
    EXPECT_EQ(tailsOf(after).items.at(2), tailsOf(before).items.at(2));
    EXPECT_EQ(TailProperties::from(after, 1), edited);
    EXPECT_EQ(tailsOf(after).nextLook, tailsOf(before).nextLook);   // a width and a bend are not a look picked
    expectUntouched(Group::Tails, before, after);   // and nothing outside the tails

    // Its look picked: the balloon's next tail takes it, whichever was picked last.
    TailProperties looked = TailProperties::from(after, 2);
    looked.tail.look.style   = TailLook::Style::Circles;
    looked.tail.look.circles = 6;
    looked.applyTo(after);
    EXPECT_EQ(tailsOf(after).nextLook, looked.tail.look);
    TailProperties plain = TailProperties::from(after, 0);
    plain.tail.look.style = TailLook::Style::Plain;
    plain.tail.look.pitch = 1.5;
    plain.applyTo(after);
    EXPECT_EQ(tailsOf(after).nextLook, plain.tail.look);
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
 * \brief The two colour groups are next door to each other and must stay there.
 *
 * Editing a balloon's frame does not edit its lettering, which is why the colour tool has two swatches
 * and not three. Both set to the same colour, so a group writing across the boundary cannot pass.
 */
TEST(PropertyGroupOwnership, SkinAndTextColoursAreSeparate)
{
    ObjectRecord a = loadedRecord();
    textOf(a).fill.colour = QColor(123, 45, 67);

    SkinProperties s = SkinProperties::from(a);
    s.fill.colour = QColor(123, 45, 67);
    s.applyTo(a);

    EXPECT_EQ(skinOf(a).fill.colour,   QColor(123, 45, 67));
    EXPECT_EQ(textOf(a).fill.colour, QColor(123, 45, 67));

    TextProperties t = TextProperties::from(a);
    t.fill.colour = QColor(9, 9, 9);
    t.applyTo(a);

    EXPECT_EQ(skinOf(a).fill.colour,   QColor(123, 45, 67));   // still the balloon's
    EXPECT_EQ(textOf(a).fill.colour, QColor(9, 9, 9));
}

// ---------------------------------------------------------------------------
// Persistence — the structs changed, the file format did not
// ---------------------------------------------------------------------------

TEST(PropertyGroupPersistence, KeepsTheFlatKeys)
{
    const ObjectRecord a = loadedRecord();
    const QJsonObject  j = a.toJson();

    EXPECT_EQ(j.value(QStringLiteral("strokeWidth")).toInt(),  styleOf(a).strokeWidth);
    EXPECT_EQ(j.value(QStringLiteral("fill")).toString(),      skinOf(a).fill.colour.name(QColor::HexArgb));
    EXPECT_EQ(j.value(QStringLiteral("stroke")).toString(),    styleOf(a).stroke.colour.name(QColor::HexArgb));
    EXPECT_EQ(j.value(QStringLiteral("fillKind")).toString(),  QStringLiteral("dots"));
    EXPECT_EQ(j.value(QStringLiteral("text")).toString(),      textOf(a).body);
    EXPECT_EQ(j.value(QStringLiteral("fontFamily")).toString(),textOf(a).family);
    EXPECT_EQ(j.value(QStringLiteral("fontSize")).toInt(),     textOf(a).pixelSize);
    EXPECT_EQ(j.value(QStringLiteral("align")).toInt(),        textOf(a).align);
    EXPECT_EQ(j.value(QStringLiteral("lineSpacing")).toInt(),  textOf(a).lineSpacing);
    EXPECT_EQ(j.value(QStringLiteral("paddingSide")).toInt(),  textOf(a).paddingSide);
    EXPECT_EQ(j.value(QStringLiteral("paddingTop")).toInt(),   textOf(a).paddingTop);
    EXPECT_DOUBLE_EQ(j.value(QStringLiteral("textOutlineWidth")).toDouble(), textOf(a).outlineWidth);
    EXPECT_EQ(j.value(QStringLiteral("textOutlineKind")).toString(), QStringLiteral("dots"));
    EXPECT_DOUBLE_EQ(j.value(QStringLiteral("textShadowY")).toDouble(), textOf(a).shadow.y);
    EXPECT_EQ(j.value(QStringLiteral("textShadowKind")).toString(), QStringLiteral("grid"));
    EXPECT_FALSE(j.contains(QStringLiteral("textShadowSecond")));   // a shadow's pattern has no colour of its own
    EXPECT_EQ(j.value(QStringLiteral("texture")).toString(),   QStringLiteral("rough"));

    // No grouping crept into the file: a record saved before the groups existed still loads.
    for (const char* nested : {"skin", "shape", "style", "tails"})
        EXPECT_FALSE(j.value(QLatin1String(nested)).isObject()) << nested;
}

TEST(PropertyGroupPersistence, RoundTrips)
{
    ObjectRecord a = loadedRecord();
    // A Curve's node with handles among a Polygon's plain ones, so both forms are saved and read.
    shapeOf(a).nodes.insert(1, ShapeNode({0.5, 0.1}, {0.05, -0.1}, {-0.02, 0.2}, ShapeNode::Type::Smooth));
    const ObjectRecord b = ObjectRecord::fromJson(a.toJson());

    EXPECT_EQ(shapeOf(b), shapeOf(a));
    EXPECT_EQ(skinOf(b),  skinOf(a));
    EXPECT_EQ(styleOf(b), styleOf(a));
    EXPECT_EQ(textOf(b),  textOf(a));
    EXPECT_EQ(tailsOf(b), tailsOf(a));
    EXPECT_EQ(b, a);
}

/**
 * \brief **Every** shape and every line style comes back from both files it is saved to — the undo
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
        shapeOf(a).kind = s;
        EXPECT_EQ(ObjectRecord::fromJson(a.toJson()), a) << shapeName(s);
    }

    // Every template, from the SVG too (with no lettering, which would need a font): a placed one must
    // read back as itself, or its tile stops being current once the file is reopened.
    for (const Geometry::ShapeTemplate& t : Geometry::shapeTemplates()) {
        ObjectRecord a = loadedRecord();
        shapeOf(a)        = t.shape;
        textOf(a).body.clear();
        bool ok = false;
        EXPECT_EQ(shapeOf(Svg::read(Svg::write(a), &ok)), t.shape) << t.id;
        EXPECT_EQ(ObjectRecord::fromJson(a.toJson()), a) << t.id;
    }

    for (int i = 0; i <= int(StyleProperties::k_lastLine); ++i) {
        const auto l = static_cast<ObjectRecord::Line>(i);
        EXPECT_EQ(lineFromName(QString::fromLatin1(lineName(l))), l) << lineName(l);
        ObjectRecord a = loadedRecord();
        styleOf(a).line   = l;
        EXPECT_EQ(ObjectRecord::fromJson(a.toJson()), a) << lineName(l);
    }
    for (int i = 0; i <= int(TextureProperties::k_lastKind); ++i) {
        const auto t = static_cast<ObjectRecord::Texture>(i);
        EXPECT_EQ(textureFromName(QString::fromLatin1(textureName(t))), t) << textureName(t);
        ObjectRecord a = loadedRecord();
        textureOf(a).kind = t;
        EXPECT_EQ(ObjectRecord::fromJson(a.toJson()), a) << textureName(t);
    }
}

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------

/**
 * \brief What *Convert to ▸* promises: a kind the record cannot draw hides properties, never destroys
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
    shapeOf(a).kind   = ObjectRecord::Shape::Box;
    ASSERT_FALSE(tailsOf(a).items.isEmpty());
    ASSERT_TRUE(a.hasTail());

    const TailsProperties kept = tailsOf(a);

    shapeOf(a).kind = ObjectRecord::Shape::None;   // what the conversion does, and all it does
    EXPECT_FALSE(a.hasSilhouette());            // the one structural question, and its whole answer
    EXPECT_FALSE(a.hasTail());                  // no balloon, so nothing for a tail to leave
    EXPECT_EQ(tailsOf(a), kept);                   // ...but the record still has them

    shapeOf(a).kind = ObjectRecord::Shape::Ellipse;
    EXPECT_TRUE(a.hasSilhouette());
    EXPECT_TRUE(a.hasTail());
    EXPECT_EQ(tailsOf(a), kept);
}

/**
 * \brief The picker offers every silhouette exactly once — and offers "no balloon" never.
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
 * \brief Every shape has its own entry in the shape table (shapes.cpp), in the enum's order, with a
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
        // Every line but the solid outline (Clean, and Dry ink: that outline under a filter) has strokes of
        // its own, and strokes reserve room.
        EXPECT_EQ(info.ink == nullptr, kind == ObjectRecord::Line::Clean || kind == ObjectRecord::Line::DryInk)
            << lineName(kind);
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
 * \brief A Polygon's bowed edges lie their bow in from their chords, its points stay in the box, and an
 *        irregular bow is dealt from the nodes: the same every time, and not the even one.
 */
TEST(Shape, APolygonsEdgesBowIn)
{
    constexpr qreal k_margin = 2.0;   // either side of where the bowed edge's middle lies, in pixels
    ShapeProperties s;
    s.kind         = ObjectRecord::Shape::Polygon;
    s.cornerRadius = 0.0;
    s.nodes        = {ShapeNode(0, 0), ShapeNode(1, 0), ShapeNode(1, 1), ShapeNode(0, 1)};
    s.bow          = 20;
    const QRectF body(0, 0, 200, 100);
    const auto   outline = Geometry::shapeInfo(s.kind).outline;
    const QPainterPath bowed = outline(s, body);
    // The top edge, 200 px, bows 40 in at its middle; the left one, 100 px, 20.
    EXPECT_FALSE(bowed.contains(QPointF(100, 40 - k_margin)));
    EXPECT_TRUE(bowed.contains(QPointF(100, 40 + k_margin)));
    EXPECT_FALSE(bowed.contains(QPointF(20 - k_margin, 50)));
    EXPECT_TRUE(bowed.contains(QPointF(20 + k_margin, 50)));
    EXPECT_TRUE(body.contains(bowed.boundingRect()));

    s.bowIrregularity = 80;
    const QPainterPath uneven = outline(s, body);
    EXPECT_TRUE(uneven == outline(s, body));
    EXPECT_FALSE(uneven == bowed);
}

/**
 * \brief Picked nodes move together, by pixels of the body, with their handles; the others stay.
 */
TEST(Shape, PickedNodesMoveTogether)
{
    constexpr qreal k_tolerance = 1e-9;
    ShapeProperties s;
    s.kind  = ObjectRecord::Shape::Curve;
    s.nodes = {ShapeNode(0, 0), ShapeNode(1, 0), ShapeNode(1, 1), ShapeNode(0, 1)};
    s.nodes[1].in = QPointF(-0.1, 0.0);
    const QRectF          body(0, 0, 200, 100);
    const ShapeProperties moved = Geometry::withNodesMoved(s, body, {1, 2}, QPointF(40, 0));
    EXPECT_NEAR(moved.nodes[1].at.x(), 1.2, k_tolerance);   // 40 px of 200
    EXPECT_NEAR(moved.nodes[2].at.x(), 1.2, k_tolerance);
    EXPECT_NEAR(moved.nodes[1].in.x(), -0.1, k_tolerance);  // the handle is an offset: it comes along
    EXPECT_NEAR(moved.nodes[0].at.x(), 0.0, k_tolerance);
    EXPECT_NEAR(moved.nodes[3].at.x(), 0.0, k_tolerance);
}

/**
 * \brief A grain with a third decimal reaches the filter as it is, and comes back from the file as it was.
 */
TEST(Texture, AFineGrainIsWrittenAsItIs)
{
    constexpr qreal k_tolerance = 1e-9;
    ObjectRecord a = loadedRecord();
    shapeOf(a)        = Geometry::shapeTemplate("speech").shape;
    textOf(a).body.clear();   // no lettering, which would need a font
    textureOf(a).kind           = ObjectRecord::Texture::Rough;
    textureOf(a).roughFrequency = 0.035;
    const QByteArray svg = Svg::write(a);
    EXPECT_TRUE(QString::fromUtf8(svg).contains(QStringLiteral("baseFrequency=\"0.035\"")));
    bool ok = false;
    EXPECT_NEAR(textureOf(Svg::read(svg, &ok)).roughFrequency, 0.035, k_tolerance);
    EXPECT_TRUE(ok);
}

/**
 * \brief Dry ink hangs its filter on the outline alone: the fill before it, plain; after it, the outline's
 *        middle, unworn. Under Rough in parts, its group lies inside the line's roughened one.
 */
TEST(Style, DryInkEatsTheOutlineAlone)
{
    ObjectRecord a = loadedRecord();
    shapeOf(a)        = Geometry::shapeTemplate("speech").shape;
    textOf(a).body.clear();   // no lettering, which would need a font
    effectsOf(a).shadow.on = false;
    textureOf(a).kind      = ObjectRecord::Texture::None;
    styleOf(a).line        = ObjectRecord::Line::DryInk;
    const QString svg      = QString::fromUtf8(Svg::write(a));
    const QString use      = QStringLiteral("filter=\"url(#pm-dryink)\"");
    const auto    filtered = svg.indexOf(use);
    ASSERT_GE(filtered, 0);
    EXPECT_EQ(svg.count(use), 1);
    EXPECT_TRUE(svg.contains(QStringLiteral("<feFuncA type=\"linear\"")));
    EXPECT_LT(svg.indexOf(QStringLiteral("fill-rule=")), filtered);                   // the fill, plain
    EXPECT_GT(svg.indexOf(QStringLiteral("stroke-width=")), filtered);                // the outline, worn
    EXPECT_GT(svg.lastIndexOf(QStringLiteral("stroke-width=")), svg.indexOf(QStringLiteral("</g>"), filtered));
    EXPECT_TRUE(Geometry::hasFilter(a));

    styleOf(a).dryInkCore = 0;   // no middle kept: the one stroke, worn
    EXPECT_EQ(QString::fromUtf8(Svg::write(a)).count(QStringLiteral("stroke-width=")),
              svg.count(QStringLiteral("stroke-width=")) - 1);

    textureOf(a).kind      = ObjectRecord::Texture::Rough;
    textureOf(a).roughFill = false;
    const QString rough = QString::fromUtf8(Svg::write(a));
    EXPECT_GT(rough.indexOf(use), rough.lastIndexOf(QStringLiteral("filter=\"url(#pm-style)\"")));
}

/**
 * \brief A regular polygon has its points, a star twice as many, both fill the unit box; irregularity is
 *        dealt from its seed: the same for the same seed, another for another, and not the regular one.
 */
TEST(Shape, RegularNodesMakeAPolygonOrAStar)
{
    constexpr qreal k_tolerance = 1e-4;   // a node is saved to four decimals
    const auto fillsTheBox = [&](const QList<ShapeNode>& nodes) {
        qreal left = 1.0, top = 1.0, right = 0.0, bottom = 0.0;
        for (const ShapeNode& n : nodes) {
            left   = qMin(left, n.at.x());
            top    = qMin(top, n.at.y());
            right  = qMax(right, n.at.x());
            bottom = qMax(bottom, n.at.y());
        }
        return std::abs(left) < k_tolerance && std::abs(top) < k_tolerance && std::abs(right - 1.0) < k_tolerance
            && std::abs(bottom - 1.0) < k_tolerance;
    };
    const QList<ShapeNode> pentagon = Geometry::regularNodes(5, 1.0, 0, 0.0, 1);
    EXPECT_EQ(pentagon.size(), 5);
    EXPECT_TRUE(fillsTheBox(pentagon));
    EXPECT_NEAR(pentagon.first().at.x(), 0.5, k_tolerance);   // the first point on top, in the middle
    EXPECT_NEAR(pentagon.first().at.y(), 0.0, k_tolerance);

    const QList<ShapeNode> star = Geometry::regularNodes(12, 0.6, 0, 0.0, 1);
    EXPECT_EQ(star.size(), 24);
    EXPECT_TRUE(fillsTheBox(star));

    const QList<ShapeNode> rough = Geometry::regularNodes(12, 0.6, 0, 0.7, 7);
    EXPECT_TRUE(fillsTheBox(rough));
    EXPECT_TRUE(Fields::same(rough, Geometry::regularNodes(12, 0.6, 0, 0.7, 7)));
    EXPECT_FALSE(Fields::same(rough, Geometry::regularNodes(12, 0.6, 0, 0.7, 8)));
    EXPECT_FALSE(Fields::same(rough, star));
}

/**
 * \brief A flash and a brush draw no solid outline, stay inside the room reserved for them, and are the
 *        same strokes for the same seed and others for another.
 *
 * The bounds are the buffer and the viewBox, so a stroke past them is clipped off in the render. The
 * strokes are random, so several seeds, at the largest jitter, swell and drift, on shapes with corners
 * and a tail.
 */
TEST(Style, ALinesStrokesStayInsideTheirBounds)
{
    for (const char* id : {"ellipse", "caption", "speech"}) {
        ObjectRecord a;
        shapeOf(a) = Geometry::shapeTemplate(id).shape;
        boxOf(a)   = QSize(280, 160);
        const ObjectRecord::Shape shape = shapeOf(a).kind;
        if (qstrcmp(id, "speech") == 0) {
            Tail t;
            t.tip = QPointF(60, 230);
            tailsOf(a).items = {t};
        }
        styleOf(a).flashOuterPitchJitter  = 100;
        styleOf(a).flashOuterLengthJitter = 100;
        styleOf(a).brushSwell             = 100;
        styleOf(a).brushDrift             = 6.0;
        styleOf(a).strokeWidth             = 9;
        for (ObjectRecord::Line line : {ObjectRecord::Line::Flash, ObjectRecord::Line::Brush,
                                        ObjectRecord::Line::Drybrush, ObjectRecord::Line::Dashed,
                                        ObjectRecord::Line::Multi, ObjectRecord::Line::Sketch,
                                        ObjectRecord::Line::Explosion}) {
            styleOf(a).line = line;
            EXPECT_FALSE(Geometry::hasOutline(a));
            EXPECT_FALSE(Geometry::hasFilter(a));

            for (int stretch : {StyleProperties::k_autoStretch, 0, 100}) {
                for (quint32 seed : {1u, 2u, 0xC0FFEEu, 0xFFFFFFFFu}) {
                    styleOf(a).flashCoreStretch = stretch;
                    seedOf(a)              = seed;
                    const QPainterPath ink = Geometry::ink(a, Geometry::silhouette(a));
                    ASSERT_FALSE(ink.isEmpty()) << id << " " << lineName(line);
                    EXPECT_TRUE(Geometry::bounds(a).contains(ink.boundingRect()))
                        << id << " (" << shapeName(shape) << ") " << lineName(line) << " stretch " << stretch
                        << " seed " << seed;
                }
            }

            seedOf(a)             = 7;
            const QPainterPath once = Geometry::ink(a, Geometry::silhouette(a));
            EXPECT_TRUE(once == Geometry::ink(a, Geometry::silhouette(a))) << id << " " << lineName(line);
            seedOf(a) = 8;
            if (line != ObjectRecord::Line::Dashed && line != ObjectRecord::Line::Multi)   // nothing random in them
                EXPECT_FALSE(once == Geometry::ink(a, Geometry::silhouette(a))) << id << " " << lineName(line);
        }
    }
}

/**
 * \brief A dashed line puts a whole number of dashes round each lap, the nearest to its length over a
 *        dash and a gap, and a gap of 0 is one whole line.
 */
TEST(Style, ADashedLineFitsWholeDashesRoundEachLap)
{
    for (const char* id : {"ellipse", "caption", "shout"}) {
        ObjectRecord a;
        shapeOf(a)           = Geometry::shapeTemplate(id).shape;
        boxOf(a)             = QSize(280, 160);
        tailsOf(a).items.clear();
        styleOf(a).line      = ObjectRecord::Line::Dashed;
        styleOf(a).dashLength = 12.0;
        styleOf(a).dashGap    = 8.0;
        const QPainterPath             silhouette = Geometry::silhouette(a);
        const QList<Geometry::OutlineRing> edges   = Geometry::outlineModel(a, silhouette).rings;
        ASSERT_EQ(edges.size(), 1) << id;
        const int dashes = qRound(edges.first().walk.length() / (styleOf(a).dashLength + styleOf(a).dashGap));
        EXPECT_EQ(Geometry::ink(a, silhouette).toSubpathPolygons().size(), dashes) << id;

        styleOf(a).dashGap = 0.0;
        EXPECT_EQ(Geometry::ink(a, silhouette).toSubpathPolygons().size(), 1) << id;
    }
}

/**
 * \brief A multi-line is the outline and lines inside it: measured across a caption's straight left side,
 *        ink on the outline, none in a gap, ink in each inner line, none past the last.
 */
TEST(Style, AMultiLineDrawsItsLinesInside)
{
    ObjectRecord a;
    shapeOf(a) = Geometry::shapeTemplate("caption").shape;
    boxOf(a)   = QSize(280, 160);
    tailsOf(a).items.clear();
    styleOf(a).line        = ObjectRecord::Line::Multi;
    styleOf(a).strokeWidth = 4;
    styleOf(a).multiGap   = 6.0;
    styleOf(a).multiWidth = 3.0;
    const QPainterPath silhouette = Geometry::silhouette(a);
    const QPainterPath ink        = Geometry::ink(a, silhouette);
    const QRectF       r          = silhouette.boundingRect();
    // x in from the silhouette's left edge, at its middle height, where a caption's side is straight
    const auto in = [&](qreal x) { return ink.contains(QPointF(r.left() + x, r.center().y())); };
    EXPECT_TRUE(in(0.5));                      // on the outline
    EXPECT_FALSE(in(2.0 + 3.0));               // in the gap
    EXPECT_TRUE(in(2.0 + 6.0 + 1.5));          // the inner line's middle
    EXPECT_FALSE(in(2.0 + 6.0 + 3.0 + 2.0));   // inside, past it

    styleOf(a).multiLines = 3;
    const QPainterPath three = Geometry::ink(a, silhouette);
    const auto in3 = [&](qreal x) { return three.contains(QPointF(r.left() + x, r.center().y())); };
    EXPECT_TRUE(in3(2.0 + 6.0 + 1.5));                 // the second line, as before
    EXPECT_FALSE(in3(2.0 + 6.0 + 3.0 + 3.0));          // the second gap
    EXPECT_TRUE(in3(2.0 + 6.0 + 3.0 + 6.0 + 1.5));     // the third line's middle
    EXPECT_FALSE(in3(2.0 + 2 * (6.0 + 3.0) + 2.0));    // inside, past it
}

/**
 * \brief A sketch lays as many strokes round the lap as put its layers over any place, one piece each; its
 *        outline adds the stroked outline's two rings, and closes the fill off whatever the strokes do.
 */
TEST(Style, ASketchLaysItsLayersOfStrokes)
{
    ObjectRecord a;
    shapeOf(a) = Geometry::shapeTemplate("ellipse").shape;
    boxOf(a)   = QSize(280, 160);
    tailsOf(a).items.clear();
    styleOf(a).line         = ObjectRecord::Line::Sketch;
    styleOf(a).sketchLength = 40.0;
    styleOf(a).sketchCore   = 0;
    const QPainterPath              silhouette = Geometry::silhouette(a);
    const QList<Geometry::OutlineRing> edges   = Geometry::outlineModel(a, silhouette).rings;
    ASSERT_EQ(edges.size(), 1);
    for (int layers : {1, 3}) {
        styleOf(a).sketchLayers = layers;
        EXPECT_EQ(Geometry::ink(a, silhouette).toSubpathPolygons().size(),
                  qRound(layers * edges.first().walk.length() / styleOf(a).sketchLength)) << layers;
    }

    // Blots add their spots and short strokes, a stream of their own: the strokes stay as they were.
    styleOf(a).sketchLayers = 1;
    const QList<QPolygonF> strokes = Geometry::ink(a, silhouette).toSubpathPolygons();
    styleOf(a).sketchBlots = 100;
    const QList<QPolygonF> blotted = Geometry::ink(a, silhouette).toSubpathPolygons();
    ASSERT_GT(blotted.size(), strokes.size());
    EXPECT_TRUE(blotted.mid(0, strokes.size()) == strokes);
    styleOf(a).sketchBlots = 0;

    // A stroke a lap long at most: longer, it is as many as one a lap long.
    styleOf(a).sketchLength = 100000.0;
    EXPECT_EQ(Geometry::ink(a, silhouette).toSubpathPolygons().size(), 1);

    // With no strokes to speak of, the outline alone still rings the whole silhouette.
    styleOf(a).sketchCore        = 25;
    styleOf(a).sketchStrokeWidth = 0;
    styleOf(a).strokeWidth       = 8;
    const QPainterPath ink = Geometry::ink(a, silhouette);
    const QRectF       r   = silhouette.boundingRect();
    for (const QPointF& p : {QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()),
                             QPointF(r.center().x(), r.top()), QPointF(r.center().x(), r.bottom())})
        EXPECT_TRUE(ink.contains(p)) << p.x() << "," << p.y();
}

/**
 * \brief A dry brush keeps a whole core, and its hairs thin out away from it and as it dries; a tail ends
 *        in a point.
 *
 * Measured every two pixels round the silhouette's edge, at offsets across the line: on the core, in the
 * two lanes next to it and in the two at the line's edges, both sides.
 */
TEST(Style, ADrybrushKeepsItsCoreAndThinsOutwards)
{
    ObjectRecord a;
    shapeOf(a)                  = Geometry::shapeTemplate("ellipse").shape;
    boxOf(a)                    = QSize(280, 160);
    styleOf(a).line             = ObjectRecord::Line::Drybrush;
    styleOf(a).strokeWidth      = 16;   // half 8: a core of 2, four lanes of 1.5 a side
    styleOf(a).drybrushCore     = 25;
    styleOf(a).drybrushBristles = 8;
    seedOf(a)              = 0xC0FFEEu;
    const QPainterPath silhouette = Geometry::silhouette(a);
    const QPolygonF    edge       = silhouette.toFillPolygon();
    const auto inked = [&](int dryness, std::initializer_list<qreal> offsets) {
        styleOf(a).drybrushDryness = dryness;
        const QPainterPath ink  = Geometry::ink(a, silhouette);
        int                hits = 0;
        int                all  = 0;
        for (int i = 0; i + 1 < edge.size(); ++i) {
            const QLineF  side(edge[i], edge[i + 1]);
            const QPointF across = QPointF(-side.dy(), side.dx()) / side.length();
            for (qreal t = 0.0; t < side.length(); t += 2.0)
                for (qreal f : offsets)
                    for (qreal sign : {-1.0, 1.0}) {
                        hits += ink.contains(side.pointAt(t / side.length()) + across * (sign * f)) ? 1 : 0;
                        ++all;
                    }
        }
        return qreal(hits) / all;
    };
    const std::initializer_list<qreal> core{0.0, 1.5};
    const std::initializer_list<qreal> near{2.4, 3.0, 3.6, 4.2, 4.8};
    const std::initializer_list<qreal> far{5.4, 6.0, 6.6, 7.2, 7.8};
    for (int dryness : {0, 50, 100})
        EXPECT_DOUBLE_EQ(inked(dryness, core), 1.0) << dryness;   // the fill is never open
    EXPECT_GT(inked(0, near), 1.5 * inked(0, far));               // sparser away from the core
    EXPECT_GT(inked(0, far), 0.0);
    EXPECT_LT(inked(60, near), 0.7 * inked(0, near));             // and as it dries
    EXPECT_DOUBLE_EQ(inked(100, near), 0.0);                      // bone dry, the core alone
    EXPECT_DOUBLE_EQ(inked(100, far), 0.0);

    // A tail ends in a point: its tip is inked, and beside it, across the tail, nothing is: the hairs
    // close on it and the core is mitred, where a round join or folded hairs would spread it.
    Tail t;
    t.tip         = QPointF(60, 260);
    tailsOf(a).items = {t};
    styleOf(a).drybrushDryness = 0;
    const QLineF  axis(Geometry::balloonRect(a).center(), t.tip);
    const QPointF beside = QPointF(-axis.dy(), axis.dx()) / axis.length() * 4.8;
    for (quint32 seed : {1u, 2u, 0xC0FFEEu}) {
        seedOf(a)            = seed;
        const QPainterPath ink = Geometry::ink(a, Geometry::silhouette(a));
        EXPECT_TRUE(ink.contains(t.tip)) << seed;
        EXPECT_FALSE(ink.contains(t.tip + beside)) << seed;
        EXPECT_FALSE(ink.contains(t.tip - beside)) << seed;
    }

    // The hairs follow each part on its own edge and run on under another only a little: the balloon's
    // outline deep under a wide tail, far from both roots and from the tail's sides, has none.
    {
        ObjectRecord e = a;
        shapeOf(e)        = Geometry::shapeTemplate("ellipse").shape;
        boxOf(e)          = QSize(280, 160);
        Tail wide;
        wide.tip       = QPointF(140, 420);
        wide.baseWidth = 160;
        tailsOf(e).items  = {wide};
        const QRectF  body   = Geometry::balloonRect(e);
        const QPointF bottom(body.center().x(), body.bottom());
        for (quint32 seed = 1; seed <= 8; ++seed) {
            seedOf(e)            = seed;
            const QPainterPath ink = Geometry::ink(e, Geometry::silhouette(e));
            for (qreal off : {-6.0, -3.0, 0.0, 3.0, 6.0})
                EXPECT_FALSE(ink.contains(bottom + QPointF(0, off))) << seed << " at " << off;
        }
    }

    // A concave corner (an L's inside, as a tail's root): each hair runs on to the other side's core and
    // stops there. Outside, the two sides' hairs cross in the angle, so the line halving it is inked here
    // and there; inside, nothing runs on into the fill, past where either side's band is.
    tailsOf(a).items.clear();
    shapeOf(a).kind         = ObjectRecord::Shape::Polygon;
    shapeOf(a).cornerRadius = 0.0;
    shapeOf(a).nodes        = {{0, 0}, {1, 0}, {1, 0.5}, {0.5, 0.5}, {0.5, 1}, {0, 1}};
    boxOf(a)                = QSize(200, 200);
    const QRectF  body   = Geometry::balloonRect(a);
    const QPointF inside = body.topLeft() + QPointF(body.width() / 2, body.height() / 2);
    styleOf(a).drybrushDryness = 0;
    int rootHits = 0;
    for (quint32 seed = 1; seed <= 8; ++seed) {
        seedOf(a)            = seed;
        const QPainterPath ink = Geometry::ink(a, Geometry::silhouette(a));
        for (qreal d : {4.5, 6.0, 7.5, 9.0})
            rootHits += ink.contains(inside + QPointF(d, d) / std::sqrt(2.0)) ? 1 : 0;
    }
    EXPECT_GT(rootHits, 0);
    for (quint32 seed = 1; seed <= 8; ++seed) {
        seedOf(a)            = seed;
        const QPainterPath ink = Geometry::ink(a, Geometry::silhouette(a));
        for (qreal lane : {2.75, 4.25, 5.75, 7.25})   // a hair run on past the corner, either side
            for (const QPointF& off : {QPointF(-4, -lane), QPointF(-lane, -4)})
                EXPECT_FALSE(ink.contains(inside + off)) << seed << " at " << off.x() << "," << off.y();
    }
}

/**
 * \brief A Circles tail is a chain of ellipses on the tail's curve, shrinking from its width to the tip,
 *        `pitch` apart, the first `offset` from the balloon and the last ending at the tip.
 *
 * A straight tail down from an ellipse's bottom, so each ellipse's extent across is its width and along
 * is down the page.
 */
TEST(Tails, ACirclesTailIsAChainShrinkingToItsTip)
{
    ObjectRecord a;
    shapeOf(a)             = Geometry::shapeTemplate("ellipse").shape;
    boxOf(a)               = QSize(280, 160);
    styleOf(a).strokeWidth = 0;
    Tail t;
    t.tip               = QPointF(140, 400);
    t.baseWidth         = 40.0;
    t.look.style        = TailLook::Style::Circles;
    t.look.circles      = 3;
    t.look.pitch        = 10.0;
    t.look.offset       = -8.0;   // a gap below the balloon
    tailsOf(a).items       = {t};
    const QRectF body   = Geometry::balloonRect(a);

    QList<QPolygonF> rings = Geometry::silhouette(a).toSubpathPolygons();
    ASSERT_EQ(rings.size(), 4);   // the balloon, and three ellipses apart from it and from each other
    std::sort(rings.begin(), rings.end(),
              [](const QPolygonF& l, const QPolygonF& r) { return l.boundingRect().top() < r.boundingRect().top(); });
    const QRectF first = rings[1].boundingRect();
    const QRectF last  = rings[3].boundingRect();
    EXPECT_NEAR(first.width(), t.baseWidth, 0.5);
    EXPECT_NEAR(last.width(), t.baseWidth * TailLook::k_lastCircle, 0.5);
    EXPECT_GT(first.width(), rings[2].boundingRect().width());
    EXPECT_GT(rings[2].boundingRect().width(), last.width());
    EXPECT_NEAR(first.top(), body.bottom() + 8.0, 0.5);
    EXPECT_NEAR(last.bottom(), t.tip.y(), 0.5);
    EXPECT_NEAR(rings[2].boundingRect().top() - first.bottom(), t.look.pitch, 0.5);
    EXPECT_NEAR(last.top() - rings[2].boundingRect().bottom(), t.look.pitch, 0.5);

    // Sunk into the balloon, the first one melts into it.
    tailsOf(a).items.first().look.offset = 8.0;
    EXPECT_EQ(Geometry::silhouette(a).toSubpathPolygons().size(), 3);
}

/**
 * \brief A Plain tail ends as its tip says: in a point, cut straight across through the tip, or in a half
 *        circle whose far point is the tip, the end `tipWidth` per cent of the base wide.
 *
 * A straight tail down from an ellipse's bottom, so the end runs across the page at the tip's height.
 */
TEST(Tails, ATipEndsAsItSays)
{
    constexpr qreal k_inside = 0.5;   // how far inside an edge a probe stands, in balloon pixels
    ObjectRecord a;
    shapeOf(a)             = Geometry::shapeTemplate("ellipse").shape;
    boxOf(a)               = QSize(280, 160);
    styleOf(a).strokeWidth = 0;
    Tail t;
    t.tip            = QPointF(140, 400);
    t.baseWidth      = 40.0;
    t.look.tipWidth  = 50;
    const qreal end  = t.baseWidth / 2.0 * t.look.tipWidth / 100.0;   // either side of the middle
    tailsOf(a).items    = {t};
    const auto at    = [&](TailLook::Tip tip, QPointF p) {
        tailsOf(a).items.first().look.tip = tip;
        return Geometry::silhouette(a).contains(p);
    };

    // Sharp: the sides meet at the tip, so just above it the tail is far narrower than the end would be.
    EXPECT_FALSE(at(TailLook::Tip::Sharp, t.tip + QPointF(end - k_inside, -k_inside)));

    // Cut: as wide as the end right at the tip, and nothing past it.
    EXPECT_TRUE(at(TailLook::Tip::Cut, t.tip + QPointF(end - k_inside, -k_inside)));
    EXPECT_TRUE(at(TailLook::Tip::Cut, t.tip + QPointF(-(end - k_inside), -k_inside)));
    EXPECT_FALSE(at(TailLook::Tip::Cut, t.tip + QPointF(end + k_inside, -k_inside)));
    EXPECT_FALSE(at(TailLook::Tip::Cut, t.tip + QPointF(0, k_inside)));

    // Round: the tip is the far point of the half circle, its corners rounded off, its centre `end` above.
    EXPECT_TRUE(at(TailLook::Tip::Round, t.tip + QPointF(0, -k_inside)));
    EXPECT_FALSE(at(TailLook::Tip::Round, t.tip + QPointF(0, k_inside)));
    EXPECT_FALSE(at(TailLook::Tip::Round, t.tip + QPointF(end - k_inside, -k_inside)));
    EXPECT_TRUE(at(TailLook::Tip::Round, t.tip + QPointF(end - k_inside, -end)));

    // The outline the lines take hears of the end from the tail, on the tail's own edge and on the joined one:
    // a Round end half a circle long with no corner on it, a Cut one straight across, a Sharp one none.
    constexpr qreal k_flattening = 1.0;   // how far a flattened end's length may be from a true one's
    constexpr qreal k_same       = 1e-9;
    for (TailLook::Tip tip : {TailLook::Tip::Sharp, TailLook::Tip::Round, TailLook::Tip::Cut}) {
        tailsOf(a).items.first().look.tip = tip;
        const Geometry::OutlineModel m = Geometry::outlineModel(a, Geometry::silhouette(a));
        ASSERT_EQ(m.parts.size(), 2);
        ASSERT_EQ(m.rings.size(), 1);
        for (const Geometry::OutlineRing& ring : {m.parts[1].rings.first(), m.rings.first()}) {
            if (tip == TailLook::Tip::Sharp) {
                EXPECT_TRUE(ring.ends.empty());
                continue;
            }
            ASSERT_EQ(ring.ends.size(), 1u);
            const Geometry::OutlineEnd& e     = ring.ends.front();
            const bool                  round = tip == TailLook::Tip::Round;
            EXPECT_EQ(e.round, round);
            EXPECT_NEAR(e.radius, end, k_same);
            EXPECT_NEAR(e.span, round ? M_PI * end : 2.0 * end, k_flattening);
            for (const Geometry::OutlineCorner& c : ring.corners)
                EXPECT_FALSE(round && ring.within(e, c.at)) << c.at;
        }
    }
    tailsOf(a).items.first().look.tip = TailLook::Tip::Sharp;

    // A Circles tail has no tip: the same chain whatever it says.
    tailsOf(a).items.first().look.style = TailLook::Style::Circles;
    tailsOf(a).items.first().look.tip   = TailLook::Tip::Sharp;
    const QPainterPath chain         = Geometry::silhouette(a);
    tailsOf(a).items.first().look.tip   = TailLook::Tip::Cut;
    EXPECT_TRUE(chain == Geometry::silhouette(a));
}

/**
 * \brief A broken tail runs in straight lines through its breaks, evenly spaced along it and moved across it
 *        by the jog, to its right first (a clockwise first turn) and then its left, its base and tip where
 *        they were.
 *
 * A straight tail down from an ellipse's bottom: across is along x, and the right of a tail running down
 * the page is -x.
 */
TEST(Tails, ABrokenTailZigzagsThroughItsBreaks)
{
    constexpr qreal k_tipTolerance = 0.5;   // balloon pixels
    ObjectRecord a;
    shapeOf(a)             = Geometry::shapeTemplate("ellipse").shape;
    boxOf(a)               = QSize(280, 160);
    styleOf(a).strokeWidth = 0;
    Tail t;
    t.tip            = QPointF(140, 460);
    t.baseWidth      = 30.0;
    t.look.breaks    = 2;
    t.look.jog       = 20;
    tailsOf(a).items    = {t};
    const QPainterPath straight = [&] {
        ObjectRecord plain             = a;
        tailsOf(plain).items.first().look.breaks = 0;
        return Geometry::silhouette(plain);
    }();
    const QPainterPath broken = Geometry::silhouette(a);
    EXPECT_FALSE(broken == straight);

    // The tail's straight line runs from inside the balloon to the tip; a third and two thirds of the way
    // its breaks stand a fifth of its length to either side, where the straight tail is not.
    const QRectF  body  = Geometry::balloonRect(a);
    const QPointF from  = body.center();
    EXPECT_NEAR(broken.boundingRect().bottom(), t.tip.y(), k_tipTolerance);   // the tip stays the lowest point
    // Whether @p path's breaks stand where a first turn to @p side (-1 the tail's right, -x) puts them.
    const auto zigzags = [&](const QPainterPath& path, qreal side) {
        for (qreal base = from.y(); base < body.bottom(); base += 1.0) {
            // Where the spine starts is inside the balloon: a start for which both breaks are in the tail.
            const qreal   len = t.tip.y() - base;
            const QPointF b1(t.tip.x() + side * 0.2 * len, base + len / 3.0);
            const QPointF b2(t.tip.x() - side * 0.2 * len, base + 2.0 * len / 3.0);
            if (path.contains(b1) && path.contains(b2) && !straight.contains(b1) && !straight.contains(b2))
                return true;
        }
        return false;
    };
    EXPECT_TRUE(zigzags(broken, -1.0));
    EXPECT_FALSE(zigzags(broken, 1.0));

    // A counter-clockwise first turn mirrors it: right becomes left.
    tailsOf(a).items.first().look.clockwise = false;
    EXPECT_TRUE(zigzags(Geometry::silhouette(a), 1.0));
    // A Circles tail takes no breaks.
    tailsOf(a).items.first().look.style = TailLook::Style::Circles;
    const QPainterPath chain         = Geometry::silhouette(a);
    tailsOf(a).items.first().look.breaks = 0;
    EXPECT_TRUE(chain == Geometry::silhouette(a));
}

//! A flash left on Auto points its strokes exactly as the shape's own stretch would.
TEST(Style, AutoStretchIsTheShapes)
{
    for (int i = 0; i <= int(ShapeProperties::k_lastKind); ++i) {
        ObjectRecord a;
        shapeOf(a).kind = static_cast<ObjectRecord::Shape>(i);
        boxOf(a)        = QSize(300, 140);
        styleOf(a).line = ObjectRecord::Line::Flash;
        seedOf(a)  = 7;
        const int own = Geometry::shapeInfo(shapeOf(a).kind).flashStretch;
        EXPECT_GE(own, 0) << shapeName(shapeOf(a).kind);
        EXPECT_LE(own, 100) << shapeName(shapeOf(a).kind);

        ASSERT_EQ(styleOf(a).flashCoreStretch, StyleProperties::k_autoStretch);   // the default is Auto
        const QPainterPath autoInk = Geometry::ink(a, Geometry::silhouette(a));
        styleOf(a).flashCoreStretch   = own;
        EXPECT_TRUE(autoInk == Geometry::ink(a, Geometry::silhouette(a))) << shapeName(shapeOf(a).kind);
    }
}

/**
 * \brief A tail leaves a concave outline from the crossing nearest its tip.
 *
 * A box with a notch cut down into its top: the line from the centre to a tip up and to the left runs
 * through the notch, back into the left prong, and out of the prong's top. The tail is seen to leave
 * the prong, so it must start there, and the notch it passed over stays empty. A search outwards from the
 * centre stopped at the notch's floor and drew the tail across the gap.
 */
TEST(Tails, AConcaveOutlineIsLeftNearestTheTip)
{
    ObjectRecord a;
    shapeOf(a).kind         = ObjectRecord::Shape::Polygon;
    shapeOf(a).cornerRadius = 0.0;
    shapeOf(a).nodes = {{0, 0}, {0.3, 0}, {0.3, 0.4}, {0.7, 0.4}, {0.7, 0}, {1, 0}, {1, 1}, {0, 1}};
    boxOf(a)              = QSize(200, 200);
    styleOf(a).strokeWidth = 0;   // the balloon is then the box, and the nodes are these pixels
    Tail t;
    t.tip         = QPointF(-40, -200);
    tailsOf(a).items = {t};

    ASSERT_TRUE(Geometry::tailsDrawn(a).value(0));
    const QPointF inTheNotch(72, 40);   // on the line, between the notch's floor and the prong
    ASSERT_FALSE(Geometry::outlineOf(a, Geometry::balloonRect(a)).contains(inTheNotch));
    EXPECT_FALSE(Geometry::silhouette(a).contains(inTheNotch));
    EXPECT_TRUE(Geometry::silhouette(a).contains(QPointF(16, -80)));   // ...and the tail is there, on its line
}

/**
 * \brief A Polygon's corner radius rounds as a Box's does, and never past half an edge.
 *
 * A square Polygon and a Box of the same radius are the same outline: a press in a corner falls outside
 * both, and one just inside the arc inside both. A radius past what the edges allow stops at half the
 * shorter one, so the outline stays in its box.
 */
TEST(Shapes, APolygonsCornersRoundAsABoxsDo)
{
    ObjectRecord box;
    shapeOf(box).kind         = ObjectRecord::Shape::Box;
    shapeOf(box).cornerRadius = 30.0;
    boxOf(box)                = QSize(200, 100);
    styleOf(box).strokeWidth   = 0;
    ObjectRecord square = box;
    shapeOf(square).kind   = ObjectRecord::Shape::Polygon;
    shapeOf(square).nodes  = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    const QRectF       body = Geometry::balloonRect(box);
    const QPainterPath a    = Geometry::outlineOf(box, body);
    const QPainterPath b    = Geometry::outlineOf(square, body);
    for (const QPointF& p : {QPointF(3, 3), QPointF(12, 12), QPointF(197, 97), QPointF(100, 1), QPointF(1, 50)})
        EXPECT_EQ(a.contains(p), b.contains(p)) << p.x() << "," << p.y();
    EXPECT_FALSE(b.contains(QPointF(3, 3)));   // the corner is cut, not bulged

    shapeOf(square).cornerRadius = 1000.0;
    const QRectF drawn = Geometry::outlineOf(square, body).boundingRect();
    EXPECT_TRUE(body.adjusted(-0.01, -0.01, 0.01, 0.01).contains(drawn));
}

/**
 * \brief Shape editing's handles: one dragged onto where it already is changes
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
 * \brief An edge goes in from the outline: the balloon keeps its bounds, the lettering's room
 *        gives up the depth, a tile is still current under it, and the same seed lays the same teeth.
 */
TEST(Shapes, AnEdgeGoesInAndTheLetteringGivesUpItsDepth)
{
    for (int e = int(ObjectRecord::Edge::Zigzag); e <= int(ShapeProperties::k_lastEdge); ++e)
    for (const char* id : {"speech", "ellipse", "shout", "scroll", "thought"}) {
        const auto edge = static_cast<ObjectRecord::Edge>(e);
        ObjectRecord plain;
        shapeOf(plain) = Geometry::shapeTemplate(id).shape;
        boxOf(plain)   = QSize(300, 160);
        ObjectRecord edged           = plain;
        shapeOf(edged).edge             = edge;
        shapeOf(edged).edgeDepth        = 9.0;
        shapeOf(edged).edgeIrregularity = 50;
        seedOf(edged)              = 7;

        const QRectF body = Geometry::balloonRect(plain);
        const QRectF bare = Geometry::outlineOf(plain, body).boundingRect();
        EXPECT_TRUE(bare.adjusted(-0.01, -0.01, 0.01, 0.01).contains(Geometry::outlineOf(edged, body).boundingRect()))
            << id << " " << edgeName(edge);
        const QRectF a = Geometry::textArea(plain);
        const QRectF b = Geometry::textArea(edged);
        EXPECT_NEAR(b.width(), a.width() - 2 * 9.0, 1e-6) << id << " " << edgeName(edge);
        EXPECT_NEAR(b.height(), a.height() - 2 * 9.0, 1e-6) << id << " " << edgeName(edge);
        EXPECT_EQ(Geometry::templateOf(shapeOf(edged)), &Geometry::shapeTemplate(id)) << id << " " << edgeName(edge);

        const QPainterPath once = Geometry::outlineOf(edged, body);
        EXPECT_TRUE(once == Geometry::outlineOf(edged, body)) << id << " " << edgeName(edge);
        seedOf(edged) = 8;
        EXPECT_FALSE(once == Geometry::outlineOf(edged, body)) << id << " " << edgeName(edge);
    }
}

/**
 * \brief A shadow is the silhouette moved, and grown by a band as wide as its spread drawn over it (no union
 *        of the two), inside the room reserved for it; only a soft one is a filter: a hard one is drawn as
 *        it is edited, a soft one by the library.
 */
TEST(Effects, AShadowStaysInItsBoundsAndOnlyASoftOneIsAFilter)
{
    ObjectRecord a;
    shapeOf(a) = Geometry::shapeTemplate("speech").shape;
    Tail t;
    t.tip         = QPointF(60, 230);
    tailsOf(a).items = {t};
    EXPECT_TRUE(Geometry::shadowOf(a, Geometry::silhouette(a)).isEmpty());

    effectsOf(a).shadow.on = true;
    for (const QPointF offset : {QPointF(12, 9), QPointF(-20, -3), QPointF(0, 30), QPointF(0, 0)}) {
        for (qreal grow : {0.0, 6.0, 15.0}) {
            for (qreal softness : {0.0, 4.0}) {
                effectsOf(a).shadow.spread   = grow;
                effectsOf(a).shadow.x        = offset.x();
                effectsOf(a).shadow.y        = offset.y();
                effectsOf(a).shadow.softness = softness;
                const QPainterPath          silhouette = Geometry::silhouette(a);
                const Geometry::ShadowPaths shadow     = Geometry::shadowOf(a, silhouette);
                // The moved balloon, grown on every side by a band as wide as the spread.
                EXPECT_TRUE(shadow.body == silhouette.translated(offset));
                EXPECT_DOUBLE_EQ(shadow.grow, grow);
                // The band and the blur reach that far, and three deviations, past the moved shape.
                const qreal  out   = grow + 3.0 * softness;
                const QRectF reach = shadow.body.boundingRect().adjusted(-out, -out, out, out);
                EXPECT_TRUE(Geometry::bounds(a).contains(reach))
                    << offset.x() << "," << offset.y() << " spread " << grow << " soft " << softness;
                EXPECT_EQ(Geometry::hasFilter(a), softness > 0.0);
            }
        }
    }
}

/**
 * \brief A halftone shadow is the shadow's colour in marks on nothing, not marks over the shadow; a soft
 *        one fades as geometry, as a blur would, and is no filter.
 */
TEST(Effects, AHalftoneShadowReplacesTheShadowAndFadesOverItsSoftness)
{
    ObjectRecord a;
    shapeOf(a)                      = Geometry::shapeTemplate("speech").shape;
    effectsOf(a).shadow.on             = true;
    effectsOf(a).shadow.spread       = 0.0;
    effectsOf(a).shadow.x            = 0.0;
    effectsOf(a).shadow.y            = 0.0;
    effectsOf(a).shadowFill.colour  = QColor(10, 20, 30, 200);
    effectsOf(a).shadowFill.spacing = 8.0;
    effectsOf(a).shadowFill.size    = 4.0;
    effectsOf(a).shadowFill.angle   = 0;
    const QPainterPath silhouette = Geometry::silhouette(a);

    for (int k = 1; k <= int(Paint::k_lastKind); ++k) {
        effectsOf(a).shadowFill.kind = static_cast<Paint::Kind>(k);
        // Hard: the moved shape, filled with the pattern's marks in the shadow's colour, and no ground.
        effectsOf(a).shadow.softness = 0.0;
        EXPECT_FALSE(Geometry::fadesShadow(a));
        EXPECT_TRUE(Geometry::shadowOf(a, silhouette).body == silhouette);
        const Paint hard = Geometry::shadowPaint(a);
        EXPECT_EQ(hard.kind, effectsOf(a).shadowFill.kind);
        EXPECT_EQ(hard.colour.alpha(), 0) << "a halftone shadow has no ground";
        EXPECT_EQ(hard.second, effectsOf(a).shadowFill.colour);

        // Soft: its marks, filled in the colour; no filter, and within the blur's reach.
        effectsOf(a).shadow.softness = 4.0;
        EXPECT_TRUE(Geometry::fadesShadow(a));
        EXPECT_FALSE(Geometry::hasFilter(a));
        EXPECT_EQ(Geometry::shadowPaint(a).kind, Paint::Kind::Solid);
        EXPECT_EQ(Geometry::shadowPaint(a).colour, effectsOf(a).shadowFill.colour);
        const QPainterPath marks = Geometry::shadowOf(a, silhouette).body;
        ASSERT_FALSE(marks.isEmpty()) << k;
        EXPECT_DOUBLE_EQ(Geometry::shadowOf(a, silhouette).grow, 0.0);   // marks are grown already
        const qreal  reach = 3.0 * effectsOf(a).shadow.softness;
        const QRectF outer = silhouette.boundingRect().adjusted(-reach, -reach, reach, reach);
        EXPECT_TRUE(outer.contains(marks.boundingRect())) << k;
        EXPECT_TRUE(Geometry::bounds(a).contains(marks.boundingRect())) << k;
    }

    // Deep inside, a dot is the pattern's own; well outside, there is none.
    effectsOf(a).shadowFill.kind = Paint::Kind::Dots;
    const QPainterPath marks  = Geometry::shadowOf(a, silhouette).body;
    const qreal        side   = effectsOf(a).shadowFill.spacing;
    const QPointF      middle = silhouette.boundingRect().center();
    const QPointF      dot((std::floor(middle.x() / side) + 0.5) * side, (std::floor(middle.y() / side) + 0.5) * side);
    EXPECT_TRUE(marks.contains(dot + QPointF(0.9 * effectsOf(a).shadowFill.size / 2.0, 0)));
    EXPECT_FALSE(marks.contains(dot + QPointF(1.1 * effectsOf(a).shadowFill.size / 2.0, 0)));
    const QRectF body = silhouette.boundingRect();
    EXPECT_FALSE(marks.intersects(QRectF(body.left() - 20, body.top(), 8, body.height())));

    // Spread, it fades about the grown edge, measured from the balloon's with no union: marks reach out
    // past the balloon by about the spread, and there, 20 px out, are some.
    effectsOf(a).shadow.spread    = 20.0;
    const QPainterPath spread  = Geometry::shadowOf(a, silhouette).body;
    EXPECT_LT(spread.boundingRect().left(), body.left() - 15.0);
    EXPECT_TRUE(spread.intersects(QRectF(body.left() - 20, body.top(), 8, body.height())));
}

/**
 * \brief A fill fades by a cross in the balloon: along each arm, whole up to its start and gone past its
 *        end; one arm is a straight fade, all four a round one. The centre moves, and the fade
 *        turns over; on ellipses, all four make an ellipse, and one arm is still straight.
 */
TEST(Paint, AFadingFillThinsOutAlongItsCross)
{
    ObjectRecord a;
    shapeOf(a)           = Geometry::shapeTemplate("caption").shape;
    boxOf(a)             = QSize(400, 200);
    styleOf(a).strokeWidth = 0;   // the balloon's rectangle is the box
    Paint& p          = skinOf(a).fill;
    p.kind            = Paint::Kind::Dots;
    p.spacing         = 8.0;
    p.size            = 6.0;
    p.angle           = 0;
    const QRectF       body       = Geometry::balloonRect(a);
    const QPainterPath silhouette = Geometry::silhouette(a);
    ASSERT_EQ(body, QRectF(0, 0, 400, 200));
    EXPECT_FALSE(Geometry::fades(p)) << "every arm on its side: no fade";
    EXPECT_TRUE(Geometry::fillTone(a, silhouette).isEmpty());

    // Down only, from half way to the side.
    p.fadeBottomFrom = 50.0;
    EXPECT_TRUE(Geometry::fades(p));
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(200, 10)), 1.0);    // above: untouched
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(200, 140)), 1.0);   // before its start
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(200, 175)), 0.5);   // half way along
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(390, 175)), 0.5);   // the same across
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(200, 200)), 0.0);   // at the side

    const QPainterPath marks = Geometry::fillTone(a, silhouette);
    ASSERT_FALSE(marks.isEmpty());
    const auto dotAt = [&](int i, int j) { return QPointF((i + 0.5) * p.spacing, (j + 0.5) * p.spacing); };
    EXPECT_TRUE(marks.contains(dotAt(25, 2) + QPointF(0.9 * p.size / 2.0, 0)));   // whole, up top
    EXPECT_TRUE(marks.contains(dotAt(25, 21)));                                   // y 172: smaller
    EXPECT_FALSE(marks.contains(dotAt(25, 21) + QPointF(0.9 * p.size / 2.0, 0)));
    EXPECT_FALSE(marks.intersects(QRectF(0, 199, 400, 1)));

    // All four from the centre: round, gone at the sides and in the corners.
    p.fadeLeftFrom = p.fadeRightFrom = p.fadeTopFrom = p.fadeBottomFrom = 0.0;
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(200, 100)), 1.0);
    EXPECT_NEAR(Geometry::fadeTone(p, body, QPointF(320, 100)), 0.4, 1e-9);   // 0.6 of the way right
    EXPECT_NEAR(Geometry::fadeTone(p, body, QPointF(200 + 0.6 * 200, 100 + 0.8 * 100)), 0.0, 1e-9);
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(0, 0)), 0.0);
    // Turned over: gone at the centre, whole past the ends.
    p.fadeReversed = true;
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(200, 100)), 0.0);
    EXPECT_NEAR(Geometry::fadeTone(p, body, QPointF(320, 100)), 0.6, 1e-9);
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(0, 0)), 1.0);
    p.fadeReversed = false;

    // On ellipses: from half way, a point at 0.45 of the way on both axes is inside the rectangle's whole
    // middle, but past the starts' ellipse (0.45·√2 of the way, the ends' at 1).
    p.fadeLeftFrom = p.fadeRightFrom = p.fadeTopFrom = p.fadeBottomFrom = 50.0;
    const QPointF inner(200 + 0.45 * 200, 100 + 0.45 * 100);
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, inner), 1.0);
    p.fadeRound = true;
    EXPECT_NEAR(Geometry::fadeTone(p, body, inner), 1.0 - (0.45 * std::sqrt(2.0) - 0.5) / 0.5, 1e-9);
    EXPECT_NEAR(Geometry::fadeTone(p, body, QPointF(350, 100)), 0.5, 1e-9);   // on an axis, as before
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(200, 100)), 1.0);
    // One arm alone: the others do not fade, so it is the same straight fade.
    p.fadeLeftFrom = p.fadeRightFrom = p.fadeTopFrom = 100.0;
    EXPECT_NEAR(Geometry::fadeTone(p, body, QPointF(390, 175)), 0.5, 1e-9);
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(5, 140)), 1.0);
    p.fadeRound = false;
    p.fadeLeftFrom = p.fadeRightFrom = p.fadeTopFrom = p.fadeBottomFrom = 0.0;

    // The centre moved to a quarter across: each arm is still measured to its own side, so the left one is
    // shorter than the right.
    p.fadeCentreX = 25.0;
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(100, 100)), 1.0);
    EXPECT_NEAR(Geometry::fadeTone(p, body, QPointF(50, 100)), 0.5, 1e-9);    // half of 100 left
    EXPECT_NEAR(Geometry::fadeTone(p, body, QPointF(250, 100)), 0.5, 1e-9);   // half of 300 right
    p.fadeCentreX = 50.0;

    // The cross: a start and an end on each arm, every one on its side for a fill that does not fade, then
    // the centre.
    Paint still;
    still.kind = Paint::Kind::Lines;
    const QList<Geometry::ShapeHandle> cross = Geometry::fadeHandles(still, body);
    ASSERT_EQ(cross.size(), 9);
    EXPECT_EQ(cross.at(0).role, Geometry::ShapeHandle::Role::FadeFrom);
    EXPECT_EQ(cross.at(4).role, Geometry::ShapeHandle::Role::FadeTo);
    EXPECT_EQ(cross.at(8).role, Geometry::ShapeHandle::Role::FadeCentre);
    EXPECT_EQ(cross.at(8).at, QPointF(200, 100));
    EXPECT_EQ(cross.at(3).at, QPointF(200, 200));   // the bottom arm's start, on its side
    EXPECT_TRUE(Geometry::fadeHandles(Paint(), body).isEmpty()) << "a solid fill has no cross";
    // Pulling the bottom start in begins a fade; pushing it past its end takes the end along, and the
    // end pulled in past the start takes the start.
    Paint dragged = Geometry::dragFadeHandle(still, body, cross.at(3), QPointF(200, 150));
    EXPECT_DOUBLE_EQ(dragged.fadeBottomFrom, 50.0);
    EXPECT_DOUBLE_EQ(dragged.fadeBottomTo, 100.0);
    dragged = Geometry::dragFadeHandle(dragged, body, cross.at(7), QPointF(260, 120));   // across is ignored
    EXPECT_DOUBLE_EQ(dragged.fadeBottomTo, 20.0);
    EXPECT_DOUBLE_EQ(dragged.fadeBottomFrom, 20.0);
    dragged = Geometry::dragFadeHandle(dragged, body, cross.at(3), QPointF(200, 400));   // past the side
    EXPECT_DOUBLE_EQ(dragged.fadeBottomFrom, 100.0);
    EXPECT_DOUBLE_EQ(dragged.fadeBottomTo, 100.0);
    // The centre goes anywhere in the rectangle, no further; the arms keep their per cents, so at rest
    // they still sit on their sides.
    dragged.fadeBottomFrom = 50.0;
    dragged = Geometry::dragFadeHandle(dragged, body, cross.at(8), QPointF(100, 250));
    EXPECT_DOUBLE_EQ(dragged.fadeCentreX, 25.0);
    EXPECT_DOUBLE_EQ(dragged.fadeCentreY, 100.0);
    EXPECT_DOUBLE_EQ(dragged.fadeBottomFrom, 50.0);
    const QList<Geometry::ShapeHandle> moved = Geometry::fadeHandles(dragged, body);
    EXPECT_EQ(moved.at(0).at, QPointF(0, 200));     // the left start, on its side
    EXPECT_EQ(moved.at(2).at, QPointF(100, 0));     // the top start
    EXPECT_EQ(moved.at(3).at, QPointF(100, 200));   // the bottom arm has no length left
    EXPECT_EQ(Geometry::dragFadeHandle(dragged, body, moved.at(3), QPointF(100, 150)), dragged)
        << "an arm with no length does not drag";

    // Only the fill lists the fade, and the shadow's pattern no colour of its own: neither is saved there.
    ObjectRecord b = a;
    styleOf(b).stroke.fadeBottomFrom = 10.0;
    effectsOf(b).shadowFill.second   = QColor(1, 2, 3);
    EXPECT_TRUE(styleOf(b) == styleOf(a));
    EXPECT_TRUE(effectsOf(b) == effectsOf(a));
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
 * \brief A Curve keeps its outline through the edits that should not change it: *Convert to
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

/**
 * \brief Every Polygon and Curve template fills its box edge to edge, and so does every template made a
 *        Curve but a cloud. Dragging a node fits the box to the outline (Geometry::outlineExtent()), so one
 *        that stopped short of an edge would jump at the first drag. A cloud's lobes stop short of its box
 *        (by about 1.6 % of the width at the left), so a cloud made a Curve has its box tightened to them
 *        at the first drag of a node: inside the box, never past it.
 */
TEST(Shapes, EveryNodedOutlineFillsItsBox)
{
    constexpr qreal k_onEdge = 1e-3;   //!< On the box's edge, as a fraction of it: a curve's extreme is found numerically
    const QRectF    body(5, 5, 270, 150);
    const auto      fills = [&](const ShapeProperties& s, const char* what) {
        const QRectF e = Geometry::outlineExtent(s, body);
        EXPECT_NEAR(e.left(), 0.0, k_onEdge) << what;
        EXPECT_NEAR(e.top(), 0.0, k_onEdge) << what;
        EXPECT_NEAR(e.right(), 1.0, k_onEdge) << what;
        EXPECT_NEAR(e.bottom(), 1.0, k_onEdge) << what;
    };
    for (const Geometry::ShapeTemplate& t : Geometry::shapeTemplates()) {
        const auto outline = Geometry::shapeInfo(t.shape.kind).outline;
        if (t.shape.kind == ObjectRecord::Shape::Polygon || t.shape.kind == ObjectRecord::Shape::Curve) {
            fills(t.shape, t.id);
        } else if (t.shape.kind == ObjectRecord::Shape::Cloud) {
            const QRectF e = Geometry::outlineExtent(Geometry::curveOf(outline(t.shape, body), body), body);
            EXPECT_TRUE(QRectF(0, 0, 1, 1).adjusted(-k_onEdge, -k_onEdge, k_onEdge, k_onEdge).contains(e)) << t.id;
        } else if (outline && t.shape.kind != ObjectRecord::Shape::None) {
            fills(Geometry::curveOf(outline(t.shape, body), body), t.id);
        }
    }
}

/**
 * \brief A picked Curve node offers both its handles, the ones it lacks where they would grow from, and
 *        *Add handles* makes those real without changing the outline. *Convert to Curve* of a speech
 *        balloon gives each arc end one handle, on the arc's side; a node added on a straight side has none.
 */
TEST(Shapes, APickedCurveNodeOffersBothHandlesAndGrowsTheMissingOnes)
{
    const QRectF          body(5, 5, 270, 150);
    const ShapeProperties speech  = Geometry::shapeTemplate("speech").shape;
    const ShapeProperties curve   = Geometry::curveOf(Geometry::shapeInfo(speech.kind).outline(speech, body), body);
    const auto            outline = Geometry::shapeInfo(ObjectRecord::Shape::Curve).outline;
    constexpr qreal       k_samePlace = 1e-9;   //!< Two handles at one point, in balloon pixels

    const auto handleAt = [&](const ShapeProperties& s, int picked, Geometry::ShapeHandle::Role role) {
        for (const Geometry::ShapeHandle& h : Geometry::shapeHandles(s, body, 0.0, picked))
            if (h.role == role && h.index == picked)
                return std::optional<QPointF>(h.at);
        return std::optional<QPointF>();
    };

    int straight = -1;   // a segment with no handle at either end: one of the box's sides
    for (int i = 0; i < curve.nodes.size(); ++i) {
        EXPECT_TRUE(handleAt(curve, i, Geometry::ShapeHandle::Role::InHandle)) << "node " << i;
        EXPECT_TRUE(handleAt(curve, i, Geometry::ShapeHandle::Role::OutHandle)) << "node " << i;
        if (curve.nodes.at(i).out.isNull() && curve.nodes.at((i + 1) % curve.nodes.size()).in.isNull())
            straight = i;
    }
    ASSERT_GE(straight, 0);

    // A node added on that side has neither handle; both are offered, and grown, a third of the way along
    // the side, so the outline is the same and the offered handles are where the grown ones are.
    const ShapeProperties split = Geometry::withVertexAdded(curve, straight);
    const int             added = straight + 1;
    ASSERT_TRUE(split.nodes.at(added).in.isNull() && split.nodes.at(added).out.isNull());
    const ShapeProperties grown = Geometry::withHandlesGrown(split, added);
    EXPECT_FALSE(grown.nodes.at(added).in.isNull());
    EXPECT_FALSE(grown.nodes.at(added).out.isNull());
    EXPECT_EQ(pointsInOnlyOne(outline(split, body), outline(grown, body), body), 0);
    for (const auto role : {Geometry::ShapeHandle::Role::InHandle, Geometry::ShapeHandle::Role::OutHandle}) {
        const QPointF offered = *handleAt(split, added, role);
        const QPointF real    = *handleAt(grown, added, role);
        EXPECT_NEAR(offered.x(), real.x(), k_samePlace);
        EXPECT_NEAR(offered.y(), real.y(), k_samePlace);
    }
    // Nothing to grow once it has both, and nothing on the arc's side of an arc end.
    EXPECT_EQ(Geometry::withHandlesGrown(grown, added), grown);
    EXPECT_EQ(Geometry::withHandlesGrown(curve, straight).nodes.at(straight).in, curve.nodes.at(straight).in);
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

/**
 * \brief What an import decides by: **our own file carries a recipe, and nobody else's does**.
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
          " pm:tails=\"40,260,30,0.2\" pm:text=\"Hello\" pm:paddingSide=\"-6\" pm:paddingTop=\"3\" pm:lineSpacing=\"80\"/></svg>";

    bool               ok   = false;
    const ObjectRecord back = Svg::read(ours, &ok);
    EXPECT_TRUE(ok);
    EXPECT_EQ(shapeOf(back).kind, ObjectRecord::Shape::Cloud);
    EXPECT_EQ(boxOf(back), QSize(300, 200));
    EXPECT_EQ(tailsOf(back).items.size(), 1);
    EXPECT_EQ(textOf(back).body, QStringLiteral("Hello"));
    EXPECT_EQ(textOf(back).paddingSide, -6);
    EXPECT_EQ(textOf(back).paddingTop, 3);
    EXPECT_EQ(textOf(back).lineSpacing, 80);

    // The prefix is a local shorthand and carries no meaning: the same file with a different prefix,
    // bound to the same namespace, is still ours.
    QByteArray renamed = ours;
    renamed.replace("pm:", "x:").replace("xmlns:x=", "xmlns:x=");
    renamed.replace("xmlns:pm=", "xmlns:x=");
    ok = false;
    (void)Svg::read(renamed, &ok);
    EXPECT_TRUE(ok);

    // A drawing with no recipe: read, and declined.
    const QByteArray foreign =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">"
        "<rect width=\"10\" height=\"10\"/></svg>";
    ok = true;
    (void)Svg::read(foreign, &ok);
    EXPECT_FALSE(ok);

    // The same attributes under somebody else's namespace are somebody else's attributes.
    const QByteArray impostor =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:pm=\"https://example.invalid/ns/1\""
        " width=\"10\" height=\"10\"><g pm:shape=\"speech\" pm:box=\"10,10\"/></svg>";
    ok = true;
    (void)Svg::read(impostor, &ok);
    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// The third kind
// ---------------------------------------------------------------------------

/**
 * \brief A record that names a picture **is** that picture, and none of our geometry applies to it.
 *
 * One record type carries all three kinds — lettering, a balloon, and somebody else's drawing — because
 * a second map keyed by the same uid would be a second channel carrying the same object, and the two
 * would drift. What keeps that honest is this: the moment `artwork` is set, the questions about our own
 * geometry answer *no*, whatever the other fields happen to hold.
 */
TEST(Kinds, ArtworkHasNoGeometryOfOurs)
{
    ObjectRecord a = loadedRecord();
    shapeOf(a).kind   = ObjectRecord::Shape::Box;
    ASSERT_TRUE(a.hasSilhouette());
    ASSERT_TRUE(a.hasTail());

    artworkOf(a) = QStringLiteral("art-0123456789abcdef.png");
    EXPECT_TRUE(a.isArtwork());
    EXPECT_FALSE(a.hasSilhouette());   // the drawing is the picture, not a shape of ours
    EXPECT_FALSE(a.hasTail());         // and nothing for a tail to leave from

    artworkOf(a).clear();
    EXPECT_FALSE(a.isArtwork());
    EXPECT_TRUE(a.hasSilhouette());    // ...and nothing was destroyed to say so
}

//! The kind survives being saved, like every other property — a picture that loaded as a balloon would
//! be a default balloon written over imported artwork, arriving by a different road.
TEST(Kinds, ArtworkSurvivesTheSnapshot)
{
    ObjectRecord a = loadedRecord();
    artworkOf(a)      = QStringLiteral("art-0123456789abcdef.png");

    const ObjectRecord back = ObjectRecord::fromJson(a.toJson());
    EXPECT_EQ(artworkOf(back), artworkOf(a));
    EXPECT_TRUE(back.isArtwork());
    EXPECT_EQ(back, a);

    // A snapshot written before the field existed loads as what it was: something we draw.
    QJsonObject older = a.toJson();
    older.remove(QStringLiteral("artwork"));
    EXPECT_FALSE(ObjectRecord::fromJson(older).isArtwork());
}

/**
 * \brief A wrapper is written only when it can draw its picture, and reads back as one.
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
    EXPECT_EQ(textOf(Svg::read(standIn)).family, QStringLiteral("Segoe Print"));

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
    artworkOf(a)   = QStringLiteral("art-0123456789abcdef.png");
    boxOf(a)       = QSize(200, 200);
    textOf(a).body = QStringLiteral("KRAK!");

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
    EXPECT_EQ(artworkOf(a), QStringLiteral("art-0123456789abcdef.png"));
    EXPECT_EQ(textOf(a).body, QStringLiteral("KRAK!"));
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
    shapeOf(balloon).kind = ObjectRecord::Shape::Box;

    ObjectRecord text;
    shapeOf(text).kind = ObjectRecord::Shape::None;

    ObjectRecord picture;
    artworkOf(picture) = QStringLiteral("art-0123456789abcdef.png");

    for (const ObjectRecord& a : {balloon, text, picture})
        EXPECT_TRUE(carriesGroup(a, PropertyGroup::Text));
}

//! Fill, line style, shape and tails all need a silhouette to sit on.
TEST(Groups, TheRestNeedASilhouette)
{
    ObjectRecord balloon;
    shapeOf(balloon).kind = ObjectRecord::Shape::Box;
    ASSERT_TRUE(balloon.hasSilhouette());

    for (auto g : {PropertyGroup::Shape, PropertyGroup::Skin, PropertyGroup::Style, PropertyGroup::Tail})
        EXPECT_TRUE(carriesGroup(balloon, g)) << "balloon, group " << int(g);

    ObjectRecord text;
    shapeOf(text).kind = ObjectRecord::Shape::None;
    for (auto g : {PropertyGroup::Shape, PropertyGroup::Skin, PropertyGroup::Style, PropertyGroup::Tail})
        EXPECT_FALSE(carriesGroup(text, g)) << "shapeless, group " << int(g);
}

//! A picture carries nothing of ours to shape, fill or roughen — **whatever its shape field says**.
TEST(Groups, APictureCarriesOnlyItsLettering)
{
    ObjectRecord picture;
    artworkOf(picture)    = QStringLiteral("art-0123456789abcdef.png");
    shapeOf(picture).kind = ObjectRecord::Shape::Box;   // a stale value; isArtwork() outranks it

    EXPECT_TRUE(carriesGroup(picture, PropertyGroup::Text));
    for (auto g : {PropertyGroup::Shape, PropertyGroup::Skin, PropertyGroup::Style, PropertyGroup::Tail})
        EXPECT_FALSE(carriesGroup(picture, g)) << "picture, group " << int(g);
}

//! A tail is not a record, so no record carries its group — nor the three that never grew an editor.
TEST(Groups, NoRecordCarriesATailsOwnGroup)
{
    const ObjectRecord a = loadedRecord();
    ASSERT_TRUE(a.hasSilhouette());
    ASSERT_FALSE(tailsOf(a).items.isEmpty());

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
 * \brief For every field of the group at \p group: changing it alone makes the group unequal, and the
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
        textOf(a).body.clear();
        ObjectRecord b = a;
        bump(Fields::at(b.*group, member));

        EXPECT_FALSE(a.*group == b.*group) << key << ": not compared";
        EXPECT_EQ(ObjectRecord::fromJson(b.toJson()), b) << key << ": not in the JSON";
        if (textOf(b).body.isEmpty()) {
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
 * \brief Everything a record is written as, for every template (and no shape) × look × with and without
 *        a tail. A case is named by its template's id and its look: clean, marker and ink (Rough at the
 *        Marker's and the Ink's numbers), flash, a rough flash, and brush.
 *
 * The balloon's SVG, its text area and its undo-snapshot JSON, one section per case. The lettering is
 * left empty and its family named, so nothing here measures a font: writing lettering needs the
 * QGuiApplication this target does not have, and the text area covers what the lettering is fitted to.
 */
QString goldenRecords()
{
    ShapeProperties none = shapeOf(loadedRecord());
    none.kind            = ObjectRecord::Shape::None;
    QList<QPair<QString, ShapeProperties>> shapes{{QStringLiteral("none"), none}};
    for (const Geometry::ShapeTemplate& t : Geometry::shapeTemplates())
        shapes.append({QString::fromLatin1(t.id), t.shape});
    // The other edges on the speech balloon, each with some irregularity.
    for (int e = int(ObjectRecord::Edge::Wave); e <= int(ShapeProperties::k_lastEdge); ++e) {
        ShapeProperties edged  = Geometry::shapeTemplate("speech").shape;
        edged.edge             = static_cast<ObjectRecord::Edge>(e);
        edged.edgePitch        = 24.0;
        edged.edgeDepth        = 8.0;
        edged.edgeIrregularity = 30;
        shapes.append({QStringLiteral("speech ") + QLatin1String(edgeName(edged.edge)), edged});
    }
    // A zigzag edge on one template of each kind that draws a line or a curve.
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
    StyleProperties clean = styleOf(loadedRecord());
    clean.line            = ObjectRecord::Line::Clean;
    clean.stroke.kind     = ObjectRecord::Pattern::Solid;   // the patterns have cases of their own, below
    StyleProperties flash = clean;
    flash.line            = ObjectRecord::Line::Flash;
    StyleProperties brush = clean;
    brush.line            = ObjectRecord::Line::Brush;
    TextureProperties untextured = textureOf(loadedRecord());   // its Rough numbers carried, unused
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
        textOf(a).body.clear();
        originOf(a) = {};   // written only when there is one; Presets.TheOriginTravelsWithTheObject has it
        if (!tail)
            tailsOf(a).items.clear();
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
                shapeOf(a)         = shape;
                styleOf(a)         = look.style;
                textureOf(a)       = look.texture;
                skinOf(a).fill.kind = ObjectRecord::Pattern::Solid;   // the fills and the shadow have cases of
                effectsOf(a).shadow.on = false;                       // their own, below
                write(name, look.name, tail, a);
            }
        }
    }
    // A dry brush on two templates, a white line on a black balloon with a pointed tail, at the dry
    // brush's own numbers.
    for (const char* id : {"speech", "shout"}) {
        ObjectRecord a        = loadedRecord();
        shapeOf(a)               = Geometry::shapeTemplate(id).shape;
        styleOf(a)               = looks.first().style;
        styleOf(a).line          = ObjectRecord::Line::Drybrush;
        styleOf(a).stroke.colour = Qt::white;
        styleOf(a).strokeWidth   = 14;
        styleOf(a).drybrushCore     = StyleProperties().drybrushCore;
        styleOf(a).drybrushBristles = StyleProperties().drybrushBristles;
        styleOf(a).drybrushDryness  = StyleProperties().drybrushDryness;
        textureOf(a)             = looks.first().texture;
        skinOf(a).fill.kind      = ObjectRecord::Pattern::Solid;
        skinOf(a).fill.colour    = Qt::black;
        effectsOf(a).shadow.on      = false;
        tailsOf(a).items.first().tip = QPointF(90, 230);
        write(QString::fromLatin1(id), QStringLiteral("drybrush"), true, a);
    }
    // A dashed line, a whisper, on two templates with a tail, at its own numbers.
    for (const char* id : {"ellipse", "caption"}) {
        ObjectRecord a      = loadedRecord();
        shapeOf(a)             = Geometry::shapeTemplate(id).shape;
        styleOf(a)             = looks.first().style;
        styleOf(a).line        = ObjectRecord::Line::Dashed;
        styleOf(a).dashLength  = StyleProperties().dashLength;
        styleOf(a).dashGap     = StyleProperties().dashGap;
        textureOf(a)           = looks.first().texture;
        skinOf(a).fill.kind    = ObjectRecord::Pattern::Solid;
        effectsOf(a).shadow.on = false;
        write(QString::fromLatin1(id), QStringLiteral("dashed"), true, a);
    }
    // A multi-line, three lines, on the speech balloon with its tail, at its other numbers' defaults.
    {
        ObjectRecord a      = loadedRecord();
        shapeOf(a)             = Geometry::shapeTemplate("speech").shape;
        styleOf(a)             = looks.first().style;
        styleOf(a).line        = ObjectRecord::Line::Multi;
        styleOf(a).multiLines  = 3;
        styleOf(a).multiGap   = StyleProperties().multiGap;
        styleOf(a).multiWidth = StyleProperties().multiWidth;
        textureOf(a)           = looks.first().texture;
        skinOf(a).fill.kind    = ObjectRecord::Pattern::Solid;
        effectsOf(a).shadow.on = false;
        write(QStringLiteral("speech"), QStringLiteral("multi"), true, a);
    }
    // A sketch, on two templates with a tail, at its own numbers and a pen's width.
    for (const char* id : {"ellipse", "shout"}) {
        ObjectRecord a          = loadedRecord();
        shapeOf(a)                 = Geometry::shapeTemplate(id).shape;
        styleOf(a)                 = looks.first().style;
        styleOf(a).line            = ObjectRecord::Line::Sketch;
        styleOf(a).strokeWidth     = 3;
        styleOf(a).sketchLength    = StyleProperties().sketchLength;
        styleOf(a).sketchLayers    = StyleProperties().sketchLayers;
        styleOf(a).sketchStiffness = StyleProperties().sketchStiffness;
        styleOf(a).sketchSpread    = StyleProperties().sketchSpread;
        styleOf(a).sketchEntry     = StyleProperties().sketchEntry;
        styleOf(a).sketchExit      = StyleProperties().sketchExit;
        styleOf(a).sketchCore        = StyleProperties().sketchCore;
        styleOf(a).sketchStrokeWidth = StyleProperties().sketchStrokeWidth;
        styleOf(a).sketchClockwise   = StyleProperties().sketchClockwise;
        styleOf(a).sketchBlots       = StyleProperties().sketchBlots;
        textureOf(a)               = looks.first().texture;
        skinOf(a).fill.kind        = ObjectRecord::Pattern::Solid;
        effectsOf(a).shadow.on     = false;
        write(QString::fromLatin1(id), QStringLiteral("sketch"), true, a);
    }
    // The burst with concave edges, as the author's reference: a rough rectangle of points, long and short,
    // joined by arcs bowing in, of uneven depth, in a clean even line.
    {
        ObjectRecord a          = loadedRecord();
        shapeOf(a)                 = Geometry::shapeTemplate("shout").shape;
        shapeOf(a).nodes           = {ShapeNode(0, 0.1),  ShapeNode(0.3, 0),    ShapeNode(0.55, 0.08), ShapeNode(1, 0),
                                   ShapeNode(0.95, 0.5), ShapeNode(1, 1),   ShapeNode(0.6, 0.92), ShapeNode(0.25, 1),
                                   ShapeNode(0, 0.85), ShapeNode(0.07, 0.45)};
        shapeOf(a).cornerRadius    = 0.0;
        shapeOf(a).bow             = 15;
        shapeOf(a).bowIrregularity = 60;
        styleOf(a)                 = looks.first().style;
        styleOf(a).strokeWidth     = 4;
        textureOf(a)               = looks.first().texture;
        skinOf(a).fill.kind        = ObjectRecord::Pattern::Solid;
        effectsOf(a).shadow.on     = false;
        write(QStringLiteral("shout"), QStringLiteral("bowed"), true, a);
    }
    // The spilling ink, as the author's reference: long loose strokes side by side, and blots.
    {
        ObjectRecord a            = loadedRecord();
        shapeOf(a)                   = Geometry::shapeTemplate("ellipse").shape;
        styleOf(a)                   = looks.first().style;
        styleOf(a).line              = ObjectRecord::Line::Sketch;
        styleOf(a).strokeWidth       = 3;
        styleOf(a).sketchLength      = 300.0;
        styleOf(a).sketchLayers      = 3;
        styleOf(a).sketchStiffness   = 0;
        styleOf(a).sketchSpread      = 7.0;
        styleOf(a).sketchEntry       = 20;
        styleOf(a).sketchExit        = 60;
        styleOf(a).sketchCore        = 0;
        styleOf(a).sketchStrokeWidth = 100;
        styleOf(a).sketchClockwise   = true;
        styleOf(a).sketchBlots       = 60;
        textureOf(a)                 = looks.first().texture;
        skinOf(a).fill.kind          = ObjectRecord::Pattern::Solid;
        effectsOf(a).shadow.on       = false;
        write(QStringLiteral("ellipse"), QStringLiteral("spilling ink"), true, a);
    }
    // An explosion, as the author's reference: a white dry-brush ring on a black ellipse, in a dense halo of
    // fine spikes, outward only, at the lines' own numbers.
    {
        ObjectRecord a                 = loadedRecord();
        shapeOf(a)                        = Geometry::shapeTemplate("ellipse").shape;
        styleOf(a)                        = looks.first().style;
        styleOf(a).line                   = ObjectRecord::Line::Explosion;
        styleOf(a).stroke.colour          = Qt::white;
        styleOf(a).strokeWidth            = 10;
        styleOf(a).drybrushCore           = StyleProperties().drybrushCore;
        styleOf(a).drybrushBristles       = StyleProperties().drybrushBristles;
        styleOf(a).drybrushDryness        = StyleProperties().drybrushDryness;
        styleOf(a).flashOuterPitch        = StyleProperties().flashOuterPitch;
        styleOf(a).flashOuterLength       = StyleProperties().flashOuterLength;
        styleOf(a).flashOuterBase         = StyleProperties().flashOuterBase;
        styleOf(a).flashOuterPitchJitter  = StyleProperties().flashOuterPitchJitter;
        styleOf(a).flashOuterLengthJitter = StyleProperties().flashOuterLengthJitter;
        styleOf(a).flashCoreStretch       = StyleProperties().flashCoreStretch;
        textureOf(a)                      = looks.first().texture;
        skinOf(a).fill.kind               = ObjectRecord::Pattern::Solid;
        skinOf(a).fill.colour             = Qt::black;
        effectsOf(a).shadow.on            = false;
        write(QStringLiteral("ellipse"), QStringLiteral("explosion"), false, a);
    }
    // A Circles tail: on a clean speech balloon, apart from it; and the author's reference, a
    // white dry brush on a black ellipse, the chain sunk into it.
    for (bool reference : {false, true}) {
        ObjectRecord a   = loadedRecord();
        shapeOf(a)          = Geometry::shapeTemplate(reference ? "ellipse" : "speech").shape;
        styleOf(a)          = looks.first().style;
        textureOf(a)        = looks.first().texture;
        skinOf(a).fill.kind = ObjectRecord::Pattern::Solid;
        effectsOf(a).shadow.on = false;
        Tail& t          = tailsOf(a).items.first();
        t.tip            = QPointF(250, 230);
        t.bend           = 0.2;
        t.look.style     = TailLook::Style::Circles;
        t.look.circles   = 3;
        t.look.pitch     = 5.0;
        t.look.offset    = reference ? 6.0 : -6.0;
        if (reference) {
            styleOf(a).line             = ObjectRecord::Line::Drybrush;
            styleOf(a).stroke.colour    = Qt::white;
            styleOf(a).strokeWidth      = 10;
            styleOf(a).drybrushCore     = StyleProperties().drybrushCore;
            styleOf(a).drybrushBristles = StyleProperties().drybrushBristles;
            styleOf(a).drybrushDryness  = StyleProperties().drybrushDryness;
            skinOf(a).fill.colour       = Qt::black;
        }
        write(QString::fromLatin1(reference ? "ellipse" : "speech"),
              reference ? QStringLiteral("circles, drybrush") : QStringLiteral("circles"), true, a);
    }
    // A round and a cut tip, bent a little, under every line and under the Rough texture, each line at its
    // own numbers but a thick one: the end's radius under the line's half-width, as the author's case.
    for (TailLook::Tip tip : {TailLook::Tip::Round, TailLook::Tip::Cut}) {
        for (int l = 0; l <= int(StyleProperties::k_lastLine) + 1; ++l) {
            const bool   rough  = l > int(StyleProperties::k_lastLine);
            ObjectRecord a      = loadedRecord();
            shapeOf(a)             = Geometry::shapeTemplate("speech").shape;
            styleOf(a)             = StyleProperties();
            styleOf(a).line        = rough ? ObjectRecord::Line::Clean : static_cast<ObjectRecord::Line>(l);
            styleOf(a).strokeWidth = 14;
            textureOf(a)           = rough ? marker : looks.first().texture;
            skinOf(a).fill.kind    = ObjectRecord::Pattern::Solid;
            effectsOf(a).shadow.on = false;
            Tail& tail          = tailsOf(a).items.first();
            tail.tip            = QPointF(220, 260);
            tail.baseWidth      = 60.0;
            tail.bend           = 0.15;
            tail.look.tip       = tip;
            tail.look.tipWidth  = 15;
            write(QStringLiteral("speech"),
                  QStringLiteral("tip %1, %2")
                      .arg(tip == TailLook::Tip::Round ? QStringLiteral("round") : QStringLiteral("cut"),
                           rough ? QStringLiteral("rough") : QString::fromLatin1(lineName(styleOf(a).line))),
                  true, a);
        }
    }
    // A broken tail, two breaks, with each tip, under the lines that treat a turn their own way.
    for (TailLook::Tip tip : {TailLook::Tip::Sharp, TailLook::Tip::Round, TailLook::Tip::Cut}) {
        for (ObjectRecord::Line line : {ObjectRecord::Line::Clean, ObjectRecord::Line::Drybrush,
                                        ObjectRecord::Line::Sketch, ObjectRecord::Line::Multi,
                                        ObjectRecord::Line::Flash}) {
            ObjectRecord a      = loadedRecord();
            shapeOf(a)             = Geometry::shapeTemplate("speech").shape;
            styleOf(a)             = StyleProperties();
            styleOf(a).line        = line;
            styleOf(a).strokeWidth = 8;
            textureOf(a)           = looks.first().texture;
            skinOf(a).fill.kind    = ObjectRecord::Pattern::Solid;
            effectsOf(a).shadow.on = false;
            Tail& tail          = tailsOf(a).items.first();
            tail.tip            = QPointF(240, 300);
            tail.baseWidth      = 50.0;
            tail.bend           = 0.1;
            tail.look.tip       = tip;
            tail.look.tipWidth  = 30;
            tail.look.breaks    = 2;
            tail.look.jog       = 15;
            tail.look.clockwise = true;
            write(QStringLiteral("speech"),
                  QStringLiteral("broken %1, %2")
                      .arg(QString::fromLatin1(tip == TailLook::Tip::Sharp   ? "sharp"
                                               : tip == TailLook::Tip::Round ? "round"
                                                                             : "cut"),
                           QString::fromLatin1(lineName(line))),
                  true, a);
        }
    }
    // The same broken tail mirrored: its first turn counter-clockwise.
    {
        ObjectRecord a      = loadedRecord();
        shapeOf(a)             = Geometry::shapeTemplate("speech").shape;
        styleOf(a)             = StyleProperties();
        styleOf(a).strokeWidth = 8;
        textureOf(a)           = looks.first().texture;
        skinOf(a).fill.kind    = ObjectRecord::Pattern::Solid;
        effectsOf(a).shadow.on = false;
        Tail& tail          = tailsOf(a).items.first();
        tail.tip            = QPointF(240, 300);
        tail.baseWidth      = 50.0;
        tail.bend           = 0.1;
        tail.look.breaks    = 2;
        tail.look.jog       = 15;
        tail.look.clockwise = false;
        write(QStringLiteral("speech"), QStringLiteral("broken counter-clockwise, clean"), true, a);
    }
    // A screentone fill on two templates, clean.
    for (const char* id : {"speech", "shout"}) {
        for (bool tail : {false, true}) {
            ObjectRecord a = loadedRecord();
            shapeOf(a)        = Geometry::shapeTemplate(id).shape;
            styleOf(a)        = looks.first().style;
            textureOf(a)      = looks.first().texture;
            effectsOf(a).shadow.on = false;
            write(QString::fromLatin1(id), QStringLiteral("screentone"), tail, a);
        }
    }
    // A shadow, hard and soft, on two templates.
    for (const char* id : {"speech", "shout"}) {
        for (qreal softness : {0.0, 2.5}) {
            ObjectRecord a           = loadedRecord();
            shapeOf(a)                  = Geometry::shapeTemplate(id).shape;
            styleOf(a)                  = looks.first().style;
            textureOf(a)                = looks.first().texture;
            skinOf(a).fill.kind          = ObjectRecord::Pattern::Solid;
            effectsOf(a).shadow.softness  = softness;
            effectsOf(a).shadowFill.kind = ObjectRecord::Pattern::Solid;
            write(QString::fromLatin1(id), softness > 0.0 ? QStringLiteral("soft shadow") : QStringLiteral("shadow"),
                  true, a);
        }
    }
    // A centred shadow, hard and soft: the default spread and no offset.
    for (qreal softness : {0.0, 2.5}) {
        ObjectRecord a            = loadedRecord();
        shapeOf(a)                   = Geometry::shapeTemplate("speech").shape;
        styleOf(a)                   = looks.first().style;
        textureOf(a)                 = looks.first().texture;
        skinOf(a).fill.kind          = ObjectRecord::Pattern::Solid;
        skinOf(a).fill.colour        = Qt::white;
        styleOf(a).strokeWidth       = StyleProperties().strokeWidth;   // a thick line would hide the rim
        effectsOf(a).shadow.spread    = EffectProperties().shadow.spread;
        effectsOf(a).shadow.x         = 0.0;
        effectsOf(a).shadow.y         = 0.0;
        effectsOf(a).shadow.softness  = softness;
        effectsOf(a).shadowFill.kind = ObjectRecord::Pattern::Solid;
        write(QStringLiteral("speech"), softness > 0.0 ? QStringLiteral("soft centred shadow")
                                                       : QStringLiteral("centred shadow"), true, a);
    }
    // Every pattern in every place it paints: the fill, a thick line, and a shadow.
    for (int k = 1; k <= int(Paint::k_lastKind); ++k) {
        const auto pattern = static_cast<ObjectRecord::Pattern>(k);
        ObjectRecord a     = loadedRecord();
        shapeOf(a)            = Geometry::shapeTemplate("speech").shape;
        styleOf(a)            = looks.first().style;
        textureOf(a)          = looks.first().texture;
        skinOf(a).fill.kind   = pattern;
        styleOf(a).stroke     = skinOf(a).fill;
        styleOf(a).stroke.colour = QColor(7, 8, 9);
        styleOf(a).stroke.second = QColor(240, 240, 240);
        styleOf(a).strokeWidth   = 14;
        effectsOf(a).shadowFill.kind = pattern;
        write(QStringLiteral("speech"), QStringLiteral("pattern ") + QLatin1String(patternName(pattern)), true, a);
    }
    // A fill that fades by its cross, one way per pattern: dots down, lines up and down,
    // a grid round from the middle.
    for (int k = 1; k <= int(Paint::k_lastKind); ++k) {
        const auto   pattern = static_cast<ObjectRecord::Pattern>(k);
        ObjectRecord a       = loadedRecord();
        shapeOf(a)              = Geometry::shapeTemplate("speech").shape;
        styleOf(a)              = looks.first().style;
        textureOf(a)            = looks.first().texture;
        effectsOf(a).shadow.on     = false;
        Paint& p             = skinOf(a).fill;
        p.kind               = pattern;
        p.colour             = QColor(250, 250, 250);
        p.second             = QColor(30, 30, 200, 200);
        p.fadeBottomFrom     = 30.0;
        p.fadeBottomTo       = 90.0;
        if (pattern != ObjectRecord::Pattern::Dots) {
            p.fadeTopFrom = 30.0;
            p.fadeTopTo   = 90.0;
        }
        if (pattern == ObjectRecord::Pattern::Grid) {
            p.fadeLeftFrom = p.fadeRightFrom = 20.0;
            p.fadeLeftTo = p.fadeRightTo = 100.0;
        }
        write(QStringLiteral("speech"), QStringLiteral("fading ") + QLatin1String(patternName(pattern)), true, a);
        // ...and the same with its cross moved up and left, then turned over.
        p.fadeCentreX = 30.0;
        p.fadeCentreY = 35.0;
        write(QStringLiteral("speech"), QStringLiteral("fading ") + QLatin1String(patternName(pattern)) + QStringLiteral(", moved"),
              true, a);
        p.fadeReversed = true;
        write(QStringLiteral("speech"), QStringLiteral("fading ") + QLatin1String(patternName(pattern)) + QStringLiteral(", reversed"),
              true, a);
        // ...and on ellipses, where all four arms fade: the grid.
        if (pattern == ObjectRecord::Pattern::Grid) {
            p.fadeRound = true;
            write(QStringLiteral("speech"), QStringLiteral("fading grid, reversed, round"), true, a);
        }
    }
    // Rough in parts: the fill not displaced, masked by the displaced silhouette; a pattern and a fade.
    for (const bool fading : {false, true}) {
        ObjectRecord a       = loadedRecord();
        shapeOf(a)              = Geometry::shapeTemplate("speech").shape;
        textureOf(a).roughFill  = false;
        skinOf(a).fill.kind     = ObjectRecord::Pattern::Dots;
        skinOf(a).fill.fadeLeftFrom = fading ? 40.0 : skinOf(a).fill.fadeLeftFrom;
        write(QStringLiteral("speech"), fading ? QStringLiteral("rough in parts, fading dots")
                                               : QStringLiteral("rough in parts, dots"), true, a);
    }
    // Dry ink: the outline eaten into but for its middle, the fill and the shadow whole; and under Rough, in
    // parts, which roughens it as a whole.
    for (const bool rough : {false, true}) {
        ObjectRecord a        = loadedRecord();
        shapeOf(a)               = Geometry::shapeTemplate("speech").shape;
        styleOf(a).line          = ObjectRecord::Line::DryInk;
        styleOf(a).stroke.kind   = ObjectRecord::Pattern::Solid;
        styleOf(a).strokeWidth   = 9;
        styleOf(a).dryInkGrain   = StyleProperties().dryInkGrain;
        styleOf(a).dryInkWear    = StyleProperties().dryInkWear;
        styleOf(a).dryInkCore    = StyleProperties().dryInkCore;
        textureOf(a).kind        = rough ? ObjectRecord::Texture::Rough : ObjectRecord::Texture::None;
        textureOf(a).roughFill   = false;
        effectsOf(a).shadow.on   = !rough;   // a dark shadow would show through the holes and hide them
        skinOf(a).fill.kind      = ObjectRecord::Pattern::Solid;
        write(QStringLiteral("speech"), rough ? QStringLiteral("dry ink, rough") : QStringLiteral("dry ink"), true, a);
    }
    // A halftone shadow, hard: the pattern's marks on nothing (the cases above fade, soft).
    {
        ObjectRecord a            = loadedRecord();
        shapeOf(a)                   = Geometry::shapeTemplate("speech").shape;
        styleOf(a)                   = looks.first().style;
        textureOf(a)                 = looks.first().texture;
        skinOf(a).fill.kind          = ObjectRecord::Pattern::Solid;
        effectsOf(a).shadow.softness  = 0.0;
        effectsOf(a).shadowFill.kind = ObjectRecord::Pattern::Dots;
        write(QStringLiteral("speech"), QStringLiteral("hard halftone shadow"), true, a);
    }
    return out;
}

} // namespace

/**
 * \brief What a record is written as does not change unless it is meant to — **byte for byte**.
 *
 * The guard for refactoring the geometry and the persistence: moving a shape's path, a style's
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

