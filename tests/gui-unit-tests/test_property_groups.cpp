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
#include <cstring>
#include <optional>
#include <type_traits>

#include <QFile>
#include <QPainter>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "chain.hpp"
#include "edgewalk.hpp"
#include "geometry.hpp"
#include "svg.hpp"
#include "shapes.hpp"
#include "styles.hpp"
#include "properties/propertygroup.hpp"
#include "objectparts.hpp"
#include "names.hpp"
#include "composition.hpp"
#include "painter.hpp"
#include "renderlist.hpp"

namespace {

//! A balloon with **every** property moved off its default, so an accidental write is visible.
Composition loadedBalloon()
{
    Composition a = balloonObject();
    shapeOf(a).kind       = ShapeProperties::Kind::Cloud;
    shapeOf(a).corner.radius = 33.0;
    shapeOf(a).nodes      = {{0.1, 0.2}, {0.9, 0.15}, {0.55, 0.95}};
    shapeOf(a).cloud.lobes      = 9;
    shapeOf(a).cloud.lobeSize   = 33;
    shapeOf(a).bow.depth             = 12;
    shapeOf(a).bow.irregularity = 37;
    shapeOf(a).edge             = ShapeProperties::Edge::Zigzag;
    shapeOf(a).teeth.pitch        = 12.5;
    shapeOf(a).teeth.depth        = 4.5;
    shapeOf(a).teeth.irregularity = 25;
    boxOf(a)              = QSize(321, 123);

    Tail t;
    t.tip                              = QPointF(17, 42);
    t.baseWidth                        = 29.0;
    t.bend                             = 0.4;
    t.look.circles.count               = 4;      // Plain still: the numbers are carried, the render is the plain tail's
    t.look.circles.pitch               = 7.5;
    t.look.circles.offset              = -3.25;
    t.look.end.width                   = 45;     // Sharp still: carried, unused
    t.look.breaks.jog                  = 35;     // no breaks: carried, unused
    t.look.breaks.clockwise            = false;
    setTails(a, {t});
    nextTailOf(a).style          = TailLook::Style::Circles;
    nextTailOf(a).circles.count  = 5;
    nextTailOf(a).circles.pitch  = 2.5;
    nextTailOf(a).circles.offset = 4.0;
    nextTailOf(a).tip            = TailLook::Tip::Round;
    nextTailOf(a).end.width      = 60;
    nextTailOf(a).breaks.count   = 2;
    nextTailOf(a).breaks.jog     = 15;

    styleOf(a).line              = StyleProperties::Line::Flash;
    textureOf(a).kind              = TextureProperties::Kind::Rough;
    textureOf(a).rough.frequency    = 0.05;
    textureOf(a).rough.octaves      = 4;
    textureOf(a).rough.displacement = 7.5;
    textureOf(a).rough.blur         = 1.25;
    textureOf(a).rough.fill         = true;   // not the default; the parts are a golden case of their own
    styleOf(a).flash.outerPitch        = 3.5;
    styleOf(a).flash.innerPitch        = 4.5;
    styleOf(a).flash.outerLength       = 21.0;
    styleOf(a).flash.innerLength       = 12.5;
    styleOf(a).flash.outerBase         = 2.5;
    styleOf(a).flash.innerBase         = 1.5;
    styleOf(a).flash.outerPitchJitter  = 11;
    styleOf(a).flash.innerPitchJitter  = 12;
    styleOf(a).flash.outerLengthJitter = 13;
    styleOf(a).flash.innerLengthJitter = 14;
    styleOf(a).flash.coreStretch       = 15;
    styleOf(a).brush.strokes           = 5;
    styleOf(a).brush.overlap           = 33;
    styleOf(a).brush.taper             = 17;
    styleOf(a).brush.swell             = 61;
    styleOf(a).brush.drift             = 2.75;
    styleOf(a).drybrush.core           = 31;
    styleOf(a).drybrush.bristles       = 9;
    styleOf(a).drybrush.dryness        = 42;
    styleOf(a).dash.length             = 9.5;
    styleOf(a).dash.gap                = 4.25;
    styleOf(a).multi.lines             = 3;
    styleOf(a).sketch.length           = 33.5;
    styleOf(a).sketch.layers           = 3;
    styleOf(a).sketch.stiffness        = 47;
    styleOf(a).sketch.spread           = 1.25;
    styleOf(a).sketch.entry            = 7;
    styleOf(a).sketch.exit             = 52;
    styleOf(a).sketch.core             = 31;
    styleOf(a).sketch.strokeWidth      = 44;
    styleOf(a).sketch.clockwise        = false;
    styleOf(a).sketch.blots            = 23;
    styleOf(a).dryInk.grain            = 0.25;
    styleOf(a).dryInk.wear             = 55;
    styleOf(a).dryInk.core             = 15;
    styleOf(a).multi.gap              = 6.5;
    styleOf(a).multi.width            = 1.75;
    seedOf(a)        = 0xC0FFEEu;
    originOf(a)     = {QStringLiteral("uid-loaded"), QStringLiteral("Loaded")};

    textOf(a).body        = QStringLiteral("Nie wiem, co sie dzieje");
    textOf(a).family      = QStringLiteral("Comic Neue");
    textOf(a).pixelSize   = 44;
    textOf(a).bold        = true;
    textOf(a).align       = Qt::AlignRight;
    textOf(a).valign      = Qt::AlignBottom;
    textOf(a).fill.colour = QColor(1, 2, 3);
    textOf(a).fill.kind   = Paint::Kind::Lines;
    textOf(a).fill.second = QColor(30, 31, 32);
    textOf(a).fill.spacing = 3.5;
    textOf(a).fill.size   = 1.75;
    textOf(a).fill.angle  = 15;
    textOf(a).lineSpacing = 85;
    textOf(a).letterSpacing = -35;
    textOf(a).kerning       = false;
    textOf(a).vertical      = true;
    textOf(a).columnsLeft   = true;
    textOf(a).warp          = {ShapeNode(0.0, 0.25), ShapeNode({0.5, 0.0}, {-0.125, 0.0}, {0.125, 0.0},
                                                                  ShapeNode::Type::Symmetric), ShapeNode(1.0, 0.75)};
    textOf(a).outline.colour  = QColor(21, 22, 23);
    textOf(a).outline.kind    = Paint::Kind::Dots;
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
    textOf(a).shadowFill.kind   = Paint::Kind::Grid;
    textOf(a).shadowFill.spacing = 6.5;

    skinOf(a).fill.colour        = QColor(4, 5, 6);
    styleOf(a).stroke.colour     = QColor(7, 8, 9);
    styleOf(a).stroke.kind       = Paint::Kind::Lines;
    styleOf(a).stroke.second     = QColor(16, 17, 18);
    styleOf(a).stroke.spacing    = 5.5;
    styleOf(a).stroke.size       = 1.5;
    styleOf(a).stroke.angle      = -20;
    styleOf(a).strokeWidth = 11;
    skinOf(a).fill.kind    = Paint::Kind::Dots;
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
    effectsOf(a).shadowFill.kind     = Paint::Kind::Grid;
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
// `pm:*` attributes), loadedBalloon(), expectUntouched() — and the mirror.
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

// italic and runs are set in RoundTrips, not in loadedBalloon(): the golden cases are built from it, and a lettering
// with neither is saved as it always was.
struct TextMirror { QString body, family; int pixelSize; bool bold, italic; int align, valign; Paint fill; int lineSpacing;
                    int letterSpacing; bool kerning; bool vertical; bool columnsLeft; QList<ShapeNode> warp; Paint outline; qreal outlineWidth; Shadow shadow; Paint shadowFill; QList<TextRun> runs; };
static_assert(sizeof(TextProperties) == sizeof(TextMirror),
              "TextProperties grew a field: place it (see above), then mirror it");

struct TailLookMirror {   // its sections as they are laid out: a field added to one changes their size too
    TailLook::Style style; struct { int count; qreal pitch, offset; } circles;
    TailLook::Tip tip; struct { int width; } end; struct { int count, jog; bool clockwise; } breaks;
};
static_assert(sizeof(TailLook) == sizeof(TailLookMirror),
              "TailLook grew a field: place it (see above), then mirror it");

struct TailMirror { QPointF tip; qreal baseWidth, bend; TailLook look; QPointF anchor; };
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

// The composition's elements, which hold the groups: a field of their own is checked by expectUntouched() too.
struct BalloonStyleMirror { StyleProperties line; SkinProperties fill; TextureProperties texture; EffectProperties effects;
                            TailLook nextLook; };
static_assert(sizeof(BalloonStyle) == sizeof(BalloonStyleMirror),
              "BalloonStyle grew a field: place it (see above), then mirror it");
struct BodyMirror { int id; Place place; QSize size; ShapeProperties shape; };
static_assert(sizeof(Body) == sizeof(BodyMirror), "Body grew a field: place it (see above), then mirror it");
struct BalloonMirror { int id; BalloonStyle style; QList<Body> bodies; QList<BalloonTail> tails; };
static_assert(sizeof(Balloon) == sizeof(BalloonMirror), "Balloon grew a field: place it (see above), then mirror it");
struct WordsMirror { int id; int seat; Place place; QSize box; TextProperties text; Frame frame; };
static_assert(sizeof(Words) == sizeof(WordsMirror), "Words grew a field: place it (see above), then mirror it");
struct MaskMirror { Body region; qreal feather; std::optional<QLineF> fade; Paint::Kind transition; qreal spacing;
                    int angle; bool keepInside; };
static_assert(sizeof(Mask) == sizeof(MaskMirror), "Mask grew a field: place it (see above), then mirror it");
struct CompositionMirror { std::optional<Balloon> balloon; std::optional<Picture> picture; QList<Words> words;
                           std::optional<Mask> mask; quint32 seed; PresetOrigin preset; int nextId; };
static_assert(sizeof(Composition) == sizeof(CompositionMirror),
              "Composition grew a field: place it (see above), then mirror it");


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
 * weaken silently at exactly the moment it is most needed. **Adding a property to Composition breaks the
 * build above until someone has decided which group owns it**, and then this function is the next
 * place it goes.
 */
void expectUntouched(Group applied, const Composition& before, const Composition& after)
{
    if (applied != Group::Shape) {
        EXPECT_EQ(shapeOf(after).kind, shapeOf(before).kind);
        EXPECT_DOUBLE_EQ(shapeOf(after).corner.radius, shapeOf(before).corner.radius);
        EXPECT_TRUE(Fields::same(shapeOf(after).nodes, shapeOf(before).nodes));
        EXPECT_EQ(shapeOf(after).cloud.lobes,    shapeOf(before).cloud.lobes);
        EXPECT_EQ(shapeOf(after).cloud.lobeSize, shapeOf(before).cloud.lobeSize);
        EXPECT_EQ(shapeOf(after).bow.depth,             shapeOf(before).bow.depth);
        EXPECT_EQ(shapeOf(after).bow.irregularity, shapeOf(before).bow.irregularity);
        EXPECT_EQ(shapeOf(after).edge, shapeOf(before).edge);
        EXPECT_DOUBLE_EQ(shapeOf(after).teeth.pitch, shapeOf(before).teeth.pitch);
        EXPECT_DOUBLE_EQ(shapeOf(after).teeth.depth, shapeOf(before).teeth.depth);
        EXPECT_EQ(shapeOf(after).teeth.irregularity, shapeOf(before).teeth.irregularity);
    }

    if (applied != Group::Skin) {
        expectSamePaint(skinOf(after).fill, skinOf(before).fill, "fill");
    }
    if (applied != Group::Style) {
        EXPECT_EQ(styleOf(after).line,        styleOf(before).line);
        expectSamePaint(styleOf(after).stroke, styleOf(before).stroke, "stroke");
        EXPECT_EQ(styleOf(after).strokeWidth, styleOf(before).strokeWidth);
        EXPECT_DOUBLE_EQ(styleOf(after).flash.outerPitch,  styleOf(before).flash.outerPitch);
        EXPECT_DOUBLE_EQ(styleOf(after).flash.innerPitch,  styleOf(before).flash.innerPitch);
        EXPECT_DOUBLE_EQ(styleOf(after).flash.outerLength, styleOf(before).flash.outerLength);
        EXPECT_DOUBLE_EQ(styleOf(after).flash.innerLength, styleOf(before).flash.innerLength);
        EXPECT_DOUBLE_EQ(styleOf(after).flash.outerBase,   styleOf(before).flash.outerBase);
        EXPECT_DOUBLE_EQ(styleOf(after).flash.innerBase,   styleOf(before).flash.innerBase);
        EXPECT_EQ(styleOf(after).flash.outerPitchJitter,  styleOf(before).flash.outerPitchJitter);
        EXPECT_EQ(styleOf(after).flash.innerPitchJitter,  styleOf(before).flash.innerPitchJitter);
        EXPECT_EQ(styleOf(after).flash.outerLengthJitter, styleOf(before).flash.outerLengthJitter);
        EXPECT_EQ(styleOf(after).flash.innerLengthJitter, styleOf(before).flash.innerLengthJitter);
        EXPECT_EQ(styleOf(after).flash.coreStretch,       styleOf(before).flash.coreStretch);
        EXPECT_EQ(styleOf(after).brush.strokes,           styleOf(before).brush.strokes);
        EXPECT_EQ(styleOf(after).brush.overlap,           styleOf(before).brush.overlap);
        EXPECT_EQ(styleOf(after).brush.taper,             styleOf(before).brush.taper);
        EXPECT_EQ(styleOf(after).brush.swell,             styleOf(before).brush.swell);
        EXPECT_DOUBLE_EQ(styleOf(after).brush.drift,      styleOf(before).brush.drift);
        EXPECT_EQ(styleOf(after).drybrush.core,           styleOf(before).drybrush.core);
        EXPECT_EQ(styleOf(after).drybrush.bristles,       styleOf(before).drybrush.bristles);
        EXPECT_EQ(styleOf(after).drybrush.dryness,        styleOf(before).drybrush.dryness);
        EXPECT_DOUBLE_EQ(styleOf(after).dash.length,      styleOf(before).dash.length);
        EXPECT_DOUBLE_EQ(styleOf(after).dash.gap,         styleOf(before).dash.gap);
        EXPECT_EQ(styleOf(after).multi.lines,             styleOf(before).multi.lines);
        EXPECT_DOUBLE_EQ(styleOf(after).sketch.length,    styleOf(before).sketch.length);
        EXPECT_EQ(styleOf(after).sketch.layers,           styleOf(before).sketch.layers);
        EXPECT_EQ(styleOf(after).sketch.stiffness,        styleOf(before).sketch.stiffness);
        EXPECT_DOUBLE_EQ(styleOf(after).sketch.spread,    styleOf(before).sketch.spread);
        EXPECT_EQ(styleOf(after).sketch.entry,            styleOf(before).sketch.entry);
        EXPECT_EQ(styleOf(after).sketch.exit,             styleOf(before).sketch.exit);
        EXPECT_EQ(styleOf(after).sketch.core,             styleOf(before).sketch.core);
        EXPECT_EQ(styleOf(after).sketch.strokeWidth,      styleOf(before).sketch.strokeWidth);
        EXPECT_EQ(styleOf(after).sketch.clockwise,        styleOf(before).sketch.clockwise);
        EXPECT_EQ(styleOf(after).sketch.blots,            styleOf(before).sketch.blots);
        EXPECT_DOUBLE_EQ(styleOf(after).dryInk.grain,     styleOf(before).dryInk.grain);
        EXPECT_EQ(styleOf(after).dryInk.wear,             styleOf(before).dryInk.wear);
        EXPECT_EQ(styleOf(after).dryInk.core,             styleOf(before).dryInk.core);
        EXPECT_DOUBLE_EQ(styleOf(after).multi.gap,       styleOf(before).multi.gap);
        EXPECT_DOUBLE_EQ(styleOf(after).multi.width,     styleOf(before).multi.width);
    }
    if (applied != Group::Texture) {
        EXPECT_EQ(textureOf(after).kind, textureOf(before).kind);
        EXPECT_DOUBLE_EQ(textureOf(after).rough.frequency,    textureOf(before).rough.frequency);
        EXPECT_EQ(textureOf(after).rough.octaves,             textureOf(before).rough.octaves);
        EXPECT_DOUBLE_EQ(textureOf(after).rough.displacement, textureOf(before).rough.displacement);
        EXPECT_DOUBLE_EQ(textureOf(after).rough.blur,         textureOf(before).rough.blur);
        EXPECT_EQ(textureOf(after).rough.fill,                textureOf(before).rough.fill);
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
        EXPECT_EQ(textOf(after).valign,    textOf(before).valign);
        expectSamePaint(textOf(after).fill, textOf(before).fill, "text");
        EXPECT_EQ(textOf(after).lineSpacing, textOf(before).lineSpacing);
        expectSamePaint(textOf(after).outline, textOf(before).outline, "text outline");
        EXPECT_DOUBLE_EQ(textOf(after).outlineWidth, textOf(before).outlineWidth);
        EXPECT_EQ(textOf(after).shadow, textOf(before).shadow);
        expectSamePaint(textOf(after).shadowFill, textOf(before).shadowFill, "text shadow");
    }
    if (applied != Group::Tails) {
        ASSERT_EQ(tailsOf(after).size(), tailsOf(before).size());
        for (int i = 0; i < tailsOf(after).size(); ++i)
            EXPECT_EQ(tailsOf(after).at(i), tailsOf(before).at(i));   // its id and its root too
        EXPECT_EQ(after.nextId, before.nextId);   // only a tail added takes one
    }

    // Owned by no group, so never written by an editor and never skipped here. The size belongs to the
    // object rather than to its look; the seed is per balloon and set once at placement, which is why a
    // preset must not carry it; the preset origin is set only by applying one; the elements' ids, places
    // and seats are the composition's own.
    EXPECT_EQ(boxOf(after),             boxOf(before));
    EXPECT_EQ(seedOf(after),            seedOf(before));
    EXPECT_EQ(originOf(after),          originOf(before));
    EXPECT_EQ(after.balloon->id,        before.balloon->id);
    EXPECT_EQ(bodyOf(after)->id,        bodyOf(before)->id);
    EXPECT_EQ(bodyOf(after)->place,     bodyOf(before)->place);
    EXPECT_EQ(wordsOf(after)->id,       wordsOf(before)->id);
    EXPECT_EQ(wordsOf(after)->seat,     wordsOf(before)->seat);
    EXPECT_EQ(wordsOf(after)->place,    wordsOf(before)->place);
    EXPECT_EQ(wordsOf(after)->box,      wordsOf(before)->box);
    EXPECT_EQ(after.picture.has_value(), before.picture.has_value());
    EXPECT_EQ(after.mask, before.mask);
}

} // namespace

// ---------------------------------------------------------------------------
// One test per group: it writes what it owns, and nothing else.
// ---------------------------------------------------------------------------

TEST(PropertyGroupOwnership, Skin)
{
    const Composition before = loadedBalloon();
    Composition       after  = before;

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
    const Composition before = loadedBalloon();
    Composition       after  = before;

    ShapeProperties s;
    s.kind         = ShapeProperties::Kind::Polygon;
    s.corner.radius = 12.0;
    s.nodes        = {{0, 0}, {1, 0.5}, {0, 1}};
    s.applyTo(after);

    EXPECT_EQ(shapeOf(after), s);
    EXPECT_EQ(ShapeProperties::from(after), s);
    expectUntouched(Group::Shape, before, after);
}

TEST(PropertyGroupOwnership, Style)
{
    const Composition before = loadedBalloon();
    Composition       after  = before;

    StyleProperties s;
    s.line          = StyleProperties::Line::Brush;
    s.stroke.colour = QColor(10, 220, 30);
    s.stroke.kind   = Paint::Kind::Lines;
    s.strokeWidth   = 37;
    s.applyTo(after);

    EXPECT_EQ(styleOf(after), s);
    EXPECT_EQ(StyleProperties::from(after), s);
    expectUntouched(Group::Style, before, after);
}

TEST(PropertyGroupOwnership, Texture)
{
    const Composition before = loadedBalloon();
    Composition       after  = before;

    TextureProperties t;
    t.kind      = TextureProperties::Kind::Rough;
    t.rough.blur = 0.25;
    t.applyTo(after);

    EXPECT_EQ(textureOf(after), t);
    EXPECT_EQ(TextureProperties::from(after), t);
    expectUntouched(Group::Texture, before, after);
}

//! The seed is part of the style and deliberately not part of its group — see StyleProperties.
TEST(PropertyGroupOwnership, StyleLeavesTheSeedAlone)
{
    Composition a = loadedBalloon();
    const quint32 seed = seedOf(a);

    StyleProperties s;
    s.line = StyleProperties::Line::Flash;
    s.applyTo(a);
    TextureProperties t;
    t.kind = TextureProperties::Kind::Rough;
    t.applyTo(a);

    EXPECT_EQ(seedOf(a), seed);
}

TEST(PropertyGroupOwnership, Effects)
{
    const Composition before = loadedBalloon();
    Composition       after  = before;

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
    const Composition before = loadedBalloon();
    Composition       after  = before;

    TextProperties s;
    s.body      = QStringLiteral("Co tu sie odprawia");
    s.family    = QStringLiteral("Bangers");
    s.pixelSize = 19;
    s.bold      = false;
    s.align     = Qt::AlignLeft;
    s.valign    = Qt::AlignTop;
    s.fill.colour = QColor(90, 80, 70);
    s.fill.kind   = Paint::Kind::Dots;
    s.lineSpacing = 140;
    s.outline.colour = QColor(60, 50, 40);
    s.outline.kind   = Paint::Kind::Lines;
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
    const Composition before = loadedBalloon();
    Composition       after  = before;

    Tail one;
    one.tip       = QPointF(-5, 300);
    one.baseWidth = 12.0;
    one.bend      = -0.3;
    TailsProperties s;
    s.items = {one, one};
    s.applyTo(after);

    EXPECT_EQ(TailsProperties::from(after), s);
    EXPECT_EQ(tailsOf(after).first().id, tailsOf(before).first().id) << "a tail kept by its place keeps its id";
    EXPECT_EQ(tailsOf(after).last().root, bodyOf(after)->id) << "a new one leaves the body";
    EXPECT_GT(after.nextId, tailsOf(after).last().id);
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
    Composition before = loadedBalloon();
    Tail first = tailsOf(before).first().tail;
    Tail last  = first;
    last.tip       = QPointF(250, -40);
    last.baseWidth = 8.0;
    last.bend      = 0.9;
    Tail middle = first;
    middle.tip  = QPointF(90, 200);
    setTails(before, {first, middle, last});
    Composition after = before;

    TailProperties edited = TailProperties::from(before, 1);
    edited.tail.baseWidth = 77.0;
    edited.tail.bend      = -0.6;
    edited.applyTo(after);

    ASSERT_EQ(tailsOf(after).size(), 3);
    EXPECT_EQ(tailsOf(after).at(0).tail, tailsOf(before).at(0).tail);
    EXPECT_EQ(tailsOf(after).at(1).tail, edited.tail);
    EXPECT_EQ(tailsOf(after).at(2).tail, tailsOf(before).at(2).tail);
    EXPECT_EQ(TailProperties::from(after, 1), edited);
    EXPECT_EQ(nextTailOf(after), nextTailOf(before));   // a width and a bend are not a look picked
    expectUntouched(Group::Tails, before, after);   // and nothing outside the tails

    // Its look picked: the balloon's next tail takes it, whichever was picked last.
    TailProperties looked = TailProperties::from(after, 2);
    looked.tail.look.style         = TailLook::Style::Circles;
    looked.tail.look.circles.count = 6;
    looked.applyTo(after);
    EXPECT_EQ(nextTailOf(after), looked.tail.look);
    TailProperties plain = TailProperties::from(after, 0);
    plain.tail.look.style         = TailLook::Style::Plain;
    plain.tail.look.circles.pitch = 1.5;
    plain.applyTo(after);
    EXPECT_EQ(nextTailOf(after), plain.tail.look);
}

//! A tail selected before an undo removed it has nowhere to go. Writing it must not bring it back.
TEST(PropertyGroupOwnership, OneTailOutOfRangeWritesNothing)
{
    const Composition before = loadedBalloon();   // one tail
    Composition       after  = before;

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
    Composition a = loadedBalloon();
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
// Persistence — the content: a group an object, a section an object in it
// ---------------------------------------------------------------------------

namespace {

//! What @p a comes back as from its content: its JSON, read.

//! An SVG of ours carrying @p c, its content element with @p attributes, under the namespace @p ns. Built by hand
//! rather than through Svg::write(): what is under test is the reader.
QByteArray svgWith(const Composition& c, const QByteArray& ns = Svg::k_pmNamespace, const QByteArray& attributes = {})
{
    const QString json = QString::fromUtf8(QJsonDocument(toJson(c)).toJson(QJsonDocument::Compact));
    return QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:pm=\"") + ns
         + "\" width=\"10\" height=\"10\"><pm:content" + attributes + ">" + json.toHtmlEscaped().toUtf8()
         + "</pm:content><g/></svg>";
}
Composition throughContent(const Composition& a)
{
    return compositionFromJson(toJson(a));
}

} // namespace

/**
 * \brief The content (toJson() of the composition) is an element an object, a group an object in it and a section
 *        an object inside its group, under the fields' keys; a section's number is left out while it is its
 *        default, and every other field is written whatever its value.
 */
TEST(PropertyGroupPersistence, TheContentIsAGroupAnObject)
{
    const Composition a       = loadedBalloon();
    const QJsonObject  j       = toJson(a);
    const QJsonObject  balloon = j.value(QStringLiteral("balloon")).toObject();
    const QJsonObject  body    = balloon.value(QStringLiteral("bodies")).toArray().first().toObject();
    const QJsonObject  words   = j.value(QStringLiteral("words")).toArray().first().toObject();
    const QJsonObject  style   = balloon.value(QStringLiteral("style")).toObject();
    const auto group = [&](const char* name) { return style.value(QLatin1String(name)).toObject(); };
    const QJsonObject text = words.value(QStringLiteral("text")).toObject();

    EXPECT_EQ(body.value(QStringLiteral("size")).toArray(), (QJsonArray{boxOf(a).width(), boxOf(a).height()}));
    EXPECT_EQ(body.value(QStringLiteral("shape")).toObject().value(QStringLiteral("shape")).toString(),
              QStringLiteral("cloud"));
    EXPECT_EQ(words.value(QStringLiteral("seat")).toInt(), body.value(QStringLiteral("id")).toInt());
    EXPECT_FALSE(words.contains(QStringLiteral("box"))) << "words in a body wrap in its size";
    EXPECT_EQ(group("line").value(QStringLiteral("strokeWidth")).toInt(), styleOf(a).strokeWidth);
    EXPECT_EQ(group("line").value(QStringLiteral("stroke")).toString(), styleOf(a).stroke.colour.name(QColor::HexArgb));
    EXPECT_EQ(group("fill").value(QStringLiteral("fill")).toString(), skinOf(a).fill.colour.name(QColor::HexArgb));
    EXPECT_EQ(group("fill").value(QStringLiteral("fillKind")).toString(), QStringLiteral("dots"));
    EXPECT_EQ(text.value(QStringLiteral("text")).toString(), textOf(a).body);
    EXPECT_EQ(text.value(QStringLiteral("fontSize")).toInt(), textOf(a).pixelSize);
    EXPECT_FALSE(text.contains(QStringLiteral("italic"))) << "written only when set";
    EXPECT_FALSE(text.contains(QStringLiteral("runs"))) << "written only when there are some";
    EXPECT_EQ(group("texture").value(QStringLiteral("texture")).toString(), QStringLiteral("rough"));

    // A section is an object in its group; a number of it set off its default is there, under its key.
    EXPECT_DOUBLE_EQ(group("line").value(QStringLiteral("flash")).toObject()
                         .value(QStringLiteral("flashOuterPitch")).toDouble(), styleOf(a).flash.outerPitch);
    EXPECT_DOUBLE_EQ(group("texture").value(QStringLiteral("rough")).toObject()
                         .value(QStringLiteral("roughFrequency")).toDouble(), textureOf(a).rough.frequency);
    const QJsonObject tail = balloon.value(QStringLiteral("tails")).toArray().first().toObject();
    EXPECT_EQ(tail.value(QStringLiteral("tip")).toArray(),
              (QJsonArray{tailsOf(a).first().tail.tip.x(), tailsOf(a).first().tail.tip.y()}));
    EXPECT_EQ(tail.value(QStringLiteral("root")).toInt(), body.value(QStringLiteral("id")).toInt());
    EXPECT_EQ(group("nextTail").value(QStringLiteral("circles")).toObject()
                  .value(QStringLiteral("circles")).toInt(), nextTailOf(a).circles.count);

    // At its defaults, a section is left out whole; a kind, a colour, a width are written all the same.
    const QJsonObject plain = toJson(balloonObject());
    const QJsonObject line  = plain.value(QStringLiteral("balloon")).toObject().value(QStringLiteral("style")).toObject()
                                  .value(QStringLiteral("line")).toObject();
    for (const char* section : {"flash", "brush", "drybrush", "dash", "multi", "sketch", "dryInk"})
        EXPECT_FALSE(line.contains(QLatin1String(section))) << section;
    EXPECT_FALSE(plain.value(QStringLiteral("balloon")).toObject().value(QStringLiteral("bodies")).toArray().first()
                     .toObject().value(QStringLiteral("shape")).toObject().contains(QStringLiteral("corner")));
    EXPECT_EQ(line.value(QStringLiteral("line")).toString(), QStringLiteral("clean"));
    EXPECT_EQ(line.value(QStringLiteral("strokeWidth")).toInt(), StyleProperties().strokeWidth);
    EXPECT_TRUE(compositionFromJson(plain) == balloonObject()) << "a number left out reads as its default";

    // A text object's words have their own box and place; it has no balloon.
    const Composition caption = withoutBalloon(a);
    const QJsonObject t       = toJson(caption);
    const QJsonObject tw    = t.value(QStringLiteral("words")).toArray().first().toObject();
    EXPECT_FALSE(t.contains(QStringLiteral("balloon")));
    EXPECT_FALSE(tw.contains(QStringLiteral("seat")));
    EXPECT_EQ(tw.value(QStringLiteral("box")).toArray(), (QJsonArray{boxOf(a).width(), boxOf(a).height()}));
}

/**
 * \brief A file's content is read with care: a balloon with no body is no balloon, a picture beside a balloon is
 *        not read, and the next id is never one in use.
 */
TEST(PropertyGroupPersistence, AContentThatBreaksTheRulesIsReadWithin)
{
    QJsonObject j = toJson(loadedBalloon());
    j.insert(QStringLiteral("picture"), QJsonObject{{QStringLiteral("id"), 9}, {QStringLiteral("file"), QStringLiteral("x.png")}});
    j.insert(QStringLiteral("nextId"), 1);
    const Composition c = compositionFromJson(j);
    EXPECT_TRUE(c.balloon);
    EXPECT_FALSE(c.picture);
    EXPECT_GT(c.nextId, c.words.first().id);
    EXPECT_GT(c.nextId, c.balloon->tails.first().id);

    QJsonObject balloon = j.value(QStringLiteral("balloon")).toObject();
    balloon.insert(QStringLiteral("bodies"), QJsonArray{});
    j.insert(QStringLiteral("balloon"), balloon);
    EXPECT_FALSE(compositionFromJson(j).balloon);
}

TEST(PropertyGroupPersistence, RoundTrips)
{
    Composition a = loadedBalloon();
    // A Curve's node with handles among a Polygon's plain ones, so both forms are saved and read.
    shapeOf(a).nodes.insert(1, ShapeNode({0.5, 0.1}, {0.05, -0.1}, {-0.02, 0.2}, ShapeNode::Type::Smooth));
    // Italic, and runs overriding each field a run has, an italic one among them the words' own opposite.
    textOf(a).italic = true;
    TextRun other;
    other.from      = 4;
    other.length    = 4;
    other.family    = QStringLiteral("Bangers");
    other.pixelSize = 60;
    other.bold      = false;
    other.fill      = Paint{QColor(200, 10, 10), QColor(255, 255, 255)};
    other.fill->kind = Paint::Kind::Dots;
    other.letterSpacing = 120;
    TextRun upright;
    upright.from   = 10;
    upright.length = 3;
    upright.italic = false;
    textOf(a).runs = {other, upright};
    // A mask with every field away from its default.
    Mask mask;
    mask.region.id    = a.nextId++;
    mask.region.place = Place{QPointF(120.5, 40.0)};
    mask.region.size  = QSize(90, 70);
    mask.region.shape = Geometry::shapeTemplate("ellipse").shape;
    mask.feather      = 3.5;
    mask.fade         = QLineF(0.1, 0.2, 0.9, 0.8);
    mask.transition   = Paint::Kind::Grid;
    mask.spacing      = 11.5;
    mask.angle        = 30;
    mask.keepInside   = true;
    a.mask            = mask;
    const Composition b = throughContent(a);
    EXPECT_EQ(textOf(b).runs, textOf(a).runs);

    EXPECT_EQ(shapeOf(b), shapeOf(a));
    EXPECT_EQ(skinOf(b),  skinOf(a));
    EXPECT_EQ(styleOf(b), styleOf(a));
    EXPECT_EQ(textOf(b),  textOf(a));
    EXPECT_EQ(tailsOf(b), tailsOf(a));
    EXPECT_TRUE(b == a);
}

/**
 * \brief Runs are kept tidy: an overlap goes to the earlier run, a run past the body is cut there, a value the words
 *        have already is no override, a run that overrides nothing goes, two neighbours alike are one, and one
 *        over the whole text is the words' own lettering.
 */
TEST(PropertyGroupPersistence, RunsAreKeptTidy)
{
    TextProperties t;
    t.body = QStringLiteral("abcdefghij");
    const auto run = [](int from, int length) {
        TextRun r;
        r.from   = from;
        r.length = length;
        return r;
    };
    TextRun bold = run(0, 4);
    bold.bold    = true;
    TextRun more = run(2, 4);   // over the first's last two letters, then two of its own
    more.bold    = true;
    TextRun same = run(6, 1);
    same.pixelSize     = t.pixelSize;       // the words' own size: no override
    same.letterSpacing = t.letterSpacing;   // nor the words' own spacing
    TextRun past = run(7, 5);
    past.italic  = true;
    t.runs       = {bold, more, same, past};
    tidyRuns(t);

    TextRun boldTidy = run(0, 6);
    boldTidy.bold    = true;
    TextRun pastTidy = run(7, 3);
    pastTidy.italic  = true;
    EXPECT_EQ(t.runs, (QList<TextRun>{boldTidy, pastTidy}));

    // One run over the whole text is the words' own lettering.
    TextRun whole       = run(0, 10);
    whole.pixelSize     = 48;
    whole.letterSpacing = -60;
    t.runs              = {whole};
    tidyRuns(t);
    EXPECT_TRUE(t.runs.isEmpty());
    EXPECT_EQ(t.pixelSize, 48);
    EXPECT_EQ(t.letterSpacing, -60);
}

/**
 * \brief **Every** shape and every line style comes back from what it is saved as — the content and the
 *        overlay's SVG.
 *
 * Both read a kind back by its name — the JSON's `shape` and the SVG's `pm:shape` go through the same
 * shapeFromName() — and a reader that stops short of the last kind loads whatever lies past it as the
 * fallback: a balloon appended after Banner came back from every file as a speech balloon. RoundTrips
 * above tries one shape, which is how that went unseen.
 *
 * The kinds go through the name both readers share. The templates are written to the SVG as well, with
 * no lettering: its outlines are a font's, which differs from one machine to another.
 */
TEST(PropertyGroupPersistence, EveryShapeAndStyleRoundTrips)
{
    QList<ShapeProperties::Kind> shapes{ShapeProperties::Kind::None};
    shapes += shapeOrder();
    for (ShapeProperties::Kind s : shapes) {
        EXPECT_EQ(shapeFromName(QString::fromLatin1(shapeName(s))), s) << shapeName(s);
        // None is a text object, which has no balloon to give a shape.
        Composition a = s == ShapeProperties::Kind::None ? withoutBalloon(loadedBalloon()) : loadedBalloon();
        if (a.balloon)
            shapeOf(a).kind = s;
        EXPECT_TRUE(throughContent(a) == a) << shapeName(s);
    }

    // Every template, from the SVG too (with no lettering, which would need a font): a placed one must
    // read back as itself, or its tile stops being current once the file is reopened.
    for (const Geometry::ShapeTemplate& t : Geometry::shapeTemplates()) {
        Composition a = loadedBalloon();
        shapeOf(a)        = t.shape;
        textOf(a).body.clear();
        bool ok = false;
        EXPECT_EQ(shapeOf(Svg::read(Svg::write(a), &ok)), t.shape) << t.id;
        EXPECT_TRUE(throughContent(a) == a) << t.id;
    }

    for (int i = 0; i <= int(StyleProperties::k_lastLine); ++i) {
        const auto l = static_cast<StyleProperties::Line>(i);
        EXPECT_EQ(lineFromName(QString::fromLatin1(lineName(l))), l) << lineName(l);
        Composition a = loadedBalloon();
        styleOf(a).line   = l;
        EXPECT_TRUE(throughContent(a) == a) << lineName(l);
    }
    for (int i = 0; i <= int(TextureProperties::k_lastKind); ++i) {
        const auto t = static_cast<TextureProperties::Kind>(i);
        EXPECT_EQ(textureFromName(QString::fromLatin1(textureName(t))), t) << textureName(t);
        Composition a = loadedBalloon();
        textureOf(a).kind = t;
        EXPECT_TRUE(throughContent(a) == a) << textureName(t);
    }
}

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------

/**
 * \brief What *Convert to ▸* does: to text, the balloon goes with its style and its tails, and the words keep the
 *        size and the place its body had; back to a balloon, one body of the words' size in the look asked for,
 *        the words seated in it, and no tail.
 */
TEST(Conversion, ToTextTheBalloonGoesAndBackItIsANewOne)
{
    const Composition a = loadedBalloon();
    ASSERT_FALSE(tailsOf(a).isEmpty());

    const Composition text = withoutBalloon(a);
    EXPECT_FALSE(text.balloon);
    ASSERT_EQ(text.words.size(), 1);
    EXPECT_EQ(text.words.first().seat, 0);
    EXPECT_EQ(text.words.first().box, boxOf(a));
    EXPECT_EQ(text.words.first().place, bodyOf(a)->place);
    EXPECT_EQ(textOf(text), textOf(a));
    EXPECT_TRUE(withoutBalloon(text) == text);   // text already: nothing to do

    BalloonLook look;
    look.shape.kind             = ShapeProperties::Kind::Ellipse;
    look.style.line.strokeWidth = 9;
    const Composition back = withBalloon(text, look);
    ASSERT_TRUE(back.balloon);
    EXPECT_TRUE(tailsOf(back).isEmpty());
    EXPECT_EQ(shapeOf(back), look.shape);
    EXPECT_TRUE(back.balloon->style == look.style);
    EXPECT_EQ(boxOf(back), boxOf(a));
    EXPECT_EQ(wordsOf(back)->seat, bodyOf(back)->id);
    EXPECT_EQ(textOf(back), textOf(a));
    EXPECT_GT(back.nextId, bodyOf(back)->id);
    EXPECT_TRUE(withBalloon(back, {}) == back);   // a background already: nothing to do
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
    EXPECT_FALSE(shapeOrder().contains(ShapeProperties::Kind::None));
    // Every enumerated shape except None, and each of them once.
    EXPECT_EQ(shapeOrder().size(), int(ShapeProperties::k_lastKind));
    for (ShapeProperties::Kind s : shapeOrder()) {
        EXPECT_FALSE(shapeTitle(s).isEmpty()) << shapeName(s);
        EXPECT_EQ(shapeOrder().count(s), 1) << shapeName(s);
    }
    // Named all the same, because a kind still has to be spelled somewhere.
    EXPECT_FALSE(shapeTitle(ShapeProperties::Kind::None).isEmpty());
}

//! Whether @p Group lists a section named @p name (a kind's table entry names it so).
template <class Group>
bool hasSection(const char* name)
{
    bool found = false;
    Group::sections([&](auto member) {
        using Sub = std::remove_reference_t<decltype(Group().*member)>;
        found     = found || std::strcmp(Sub::section().name, name) == 0;
    });
    return found;
}

//! Every field of the section @p Sub has a row, and a bool's names both its entries (a combo) or neither (a check box).
template <class Sub>
void expectEveryFieldHasARow()
{
    const Fields::Section& s = Sub::section();
    Sub::fields([&](const char* key, auto field) {
        using T = std::remove_reference_t<decltype(Sub().*field)>;
        const Fields::RowSpec* row = nullptr;
        for (int r = 0; r < s.rowCount; ++r)
            if (std::strcmp(s.rows[r].key, key) == 0)
                row = &s.rows[r];
        if (!row) {
            ADD_FAILURE() << s.name << " has no row for " << key;
            return;
        }
        if constexpr (std::is_same_v<T, bool>)
            EXPECT_EQ(row->on == nullptr, row->off == nullptr) << key;
        if (row->needs) {   // the number it follows is a row of the same section
            bool found = false;
            for (int r = 0; r < s.rowCount; ++r)
                found = found || std::strcmp(s.rows[r].key, row->needs) == 0;
            EXPECT_TRUE(found) << key << " needs " << row->needs;
        }
    });
}

//! expectEveryFieldHasARow() for every section of @p Group.
template <class Group>
void expectEverySectionHasRows()
{
    Group::sections([](auto member) {
        expectEveryFieldHasARow<std::remove_reference_t<decltype(Group().*member)>>();
    });
}

/**
 * \brief Every shape has its own entry in the shape table (shapes.cpp), in the enum's order, with a
 *        name, a title, an outline and a text area — except `None`, which has no outline to draw. So
 *        has every line (strokes for each but Clean) and every texture (a filter's reach for each but
 *        None), in styles.cpp. The sections each entry names are its group's, and every number in them
 *        has a panel row.
 *
 * The tables are indexed by the enum, so an entry out of order would give a shape another's geometry;
 * and an appended shape with no entry would be drawn as the Box shapeInfo() falls back to. Every
 * template is a balloon, named once, found by templateOf() as itself, and every kind has one.
 */
TEST(Conversion, EveryShapeAndStyleHasATableEntry)
{
    for (int i = 0; i <= int(StyleProperties::k_lastLine); ++i) {
        const auto                kind = static_cast<StyleProperties::Line>(i);
        const Geometry::LineInfo& info = Geometry::lineInfo(kind);
        EXPECT_EQ(info.kind, kind) << "line entry " << i << " is out of the enum's order";
        EXPECT_NE(info.name, nullptr) << i;
        EXPECT_NE(info.title, nullptr) << i;
        // Every line but the solid outline (Clean, and Dry ink: that outline under a filter) has strokes of
        // its own, and strokes reserve room.
        EXPECT_EQ(info.ink == nullptr, kind == StyleProperties::Line::Clean || kind == StyleProperties::Line::DryInk)
            << lineName(kind);
        EXPECT_EQ(info.ink == nullptr, info.inkReach == nullptr) << lineName(kind);
        // The sections it reads are StyleProperties' own: the Line panel finds their rows by name.
        for (const char* name : info.sections)
            EXPECT_TRUE(!name || hasSection<StyleProperties>(name)) << lineName(kind) << " reads " << name;
    }
    expectEverySectionHasRows<StyleProperties>();
    for (int i = 0; i <= int(TextureProperties::k_lastKind); ++i) {
        const auto                   kind = static_cast<TextureProperties::Kind>(i);
        const Geometry::TextureInfo& info = Geometry::textureInfo(kind);
        EXPECT_EQ(info.kind, kind) << "texture entry " << i << " is out of the enum's order";
        EXPECT_NE(info.name, nullptr) << i;
        EXPECT_NE(info.title, nullptr) << i;
        // Every texture but None is a filter, and a filter reserves room.
        EXPECT_EQ(info.reach == nullptr, kind == TextureProperties::Kind::None) << textureName(kind);
        for (const char* name : info.sections)
            EXPECT_TRUE(!name || hasSection<TextureProperties>(name)) << textureName(kind) << " reads " << name;
    }
    expectEverySectionHasRows<TextureProperties>();

    for (int i = 0; i <= int(ShapeProperties::k_lastKind); ++i) {
        const auto                 kind = static_cast<ShapeProperties::Kind>(i);
        const Geometry::ShapeInfo& info = Geometry::shapeInfo(kind);
        EXPECT_EQ(info.kind, kind) << "entry " << i << " is out of the enum's order";
        EXPECT_NE(info.name, nullptr) << i;
        EXPECT_NE(info.title, nullptr) << i;
        EXPECT_EQ(info.outline == nullptr, kind == ShapeProperties::Kind::None) << shapeName(kind);
        for (const char* name : info.sections)
            EXPECT_TRUE(!name || hasSection<ShapeProperties>(name)) << shapeName(kind) << " reads " << name;
    }
    expectEverySectionHasRows<ShapeProperties>();
    expectEveryFieldHasARow<EdgeParams>();   // the edge's, under its own combo

    // A tail's styles and tips, likewise (styles.cpp); only a tipped style offers a tip.
    for (int i = 0; i <= int(TailLook::k_lastStyle); ++i) {
        const auto                     kind = static_cast<TailLook::Style>(i);
        const Geometry::TailStyleInfo& info = Geometry::tailStyleInfo(kind);
        EXPECT_EQ(info.kind, kind) << "tail style entry " << i << " is out of the enum's order";
        EXPECT_NE(info.name, nullptr) << i;
        EXPECT_NE(info.title, nullptr) << i;
        for (const char* name : info.sections)
            EXPECT_TRUE(!name || hasSection<TailLook>(name)) << info.name << " reads " << name;
    }
    for (int i = 0; i <= int(TailLook::k_lastTip); ++i) {
        const auto                   kind = static_cast<TailLook::Tip>(i);
        const Geometry::TailTipInfo& info = Geometry::tailTipInfo(kind);
        EXPECT_EQ(info.kind, kind) << "tail tip entry " << i << " is out of the enum's order";
        EXPECT_NE(info.name, nullptr) << i;
        EXPECT_NE(info.title, nullptr) << i;
        for (const char* name : info.sections)
            EXPECT_TRUE(!name || hasSection<TailLook>(name)) << info.name << " reads " << name;
    }
    expectEverySectionHasRows<TailLook>();

    QList<ShapeProperties::Kind> templated;
    for (const Geometry::ShapeTemplate& t : Geometry::shapeTemplates()) {
        EXPECT_NE(t.shape.kind, ShapeProperties::Kind::None) << t.id;
        EXPECT_NE(t.title, nullptr) << t.id;
        EXPECT_EQ(Geometry::templateOf(t.shape), &t) << t.id << " equals an earlier template";
        EXPECT_EQ(&Geometry::shapeTemplate(t.id), &t) << t.id << " is not the only one of its id";
        templated << t.shape.kind;
    }
    for (ShapeProperties::Kind s : shapeOrder())
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
    s.kind         = ShapeProperties::Kind::Polygon;
    s.corner.radius = 0.0;
    s.nodes        = {ShapeNode(0, 0), ShapeNode(1, 0), ShapeNode(1, 1), ShapeNode(0, 1)};
    s.bow.depth          = 20;
    const QRectF body(0, 0, 200, 100);
    const auto   outline = Geometry::shapeInfo(s.kind).outline;
    const QPainterPath bowed = outline(s, body);
    // The top edge, 200 px, bows 40 in at its middle; the left one, 100 px, 20.
    EXPECT_FALSE(bowed.contains(QPointF(100, 40 - k_margin)));
    EXPECT_TRUE(bowed.contains(QPointF(100, 40 + k_margin)));
    EXPECT_FALSE(bowed.contains(QPointF(20 - k_margin, 50)));
    EXPECT_TRUE(bowed.contains(QPointF(20 + k_margin, 50)));
    EXPECT_TRUE(body.contains(bowed.boundingRect()));

    s.bow.irregularity = 80;
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
    s.kind  = ShapeProperties::Kind::Curve;
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
    Composition a = loadedBalloon();
    shapeOf(a)        = Geometry::shapeTemplate("speech").shape;
    textOf(a).body.clear();   // no lettering, which would need a font
    textureOf(a).kind           = TextureProperties::Kind::Rough;
    textureOf(a).rough.frequency = 0.035;
    const QByteArray svg = Svg::write(a);
    EXPECT_TRUE(QString::fromUtf8(svg).contains(QStringLiteral("baseFrequency=\"0.035\"")));
    bool ok = false;
    EXPECT_NEAR(textureOf(Svg::read(svg, &ok)).rough.frequency, 0.035, k_tolerance);
    EXPECT_TRUE(ok);
}

/**
 * \brief Dry ink hangs its filter on the outline alone: the fill before it, plain; after it, the outline's
 *        middle, unworn. Under Rough in parts, its group lies inside the line's roughened one.
 */
TEST(Style, DryInkEatsTheOutlineAlone)
{
    Composition a = loadedBalloon();
    shapeOf(a)        = Geometry::shapeTemplate("speech").shape;
    textOf(a).body.clear();   // no lettering, which would need a font
    effectsOf(a).shadow.on = false;
    textureOf(a).kind      = TextureProperties::Kind::None;
    styleOf(a).line        = StyleProperties::Line::DryInk;
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

    styleOf(a).dryInk.core = 0;   // no middle kept: the one stroke, worn
    EXPECT_EQ(QString::fromUtf8(Svg::write(a)).count(QStringLiteral("stroke-width=")),
              svg.count(QStringLiteral("stroke-width=")) - 1);

    textureOf(a).kind      = TextureProperties::Kind::Rough;
    textureOf(a).rough.fill = false;
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
        Composition a = balloonObject();
        shapeOf(a) = Geometry::shapeTemplate(id).shape;
        boxOf(a)   = QSize(280, 160);
        const ShapeProperties::Kind shape = shapeOf(a).kind;
        if (qstrcmp(id, "speech") == 0) {
            Tail t;
            t.tip = QPointF(60, 230);
            setTails(a, {t});
        }
        styleOf(a).flash.outerPitchJitter  = 100;
        styleOf(a).flash.outerLengthJitter = 100;
        styleOf(a).brush.swell             = 100;
        styleOf(a).brush.drift             = 6.0;
        styleOf(a).strokeWidth             = 9;
        for (StyleProperties::Line line : {StyleProperties::Line::Flash, StyleProperties::Line::Brush,
                                        StyleProperties::Line::Drybrush, StyleProperties::Line::Dashed,
                                        StyleProperties::Line::Multi, StyleProperties::Line::Sketch,
                                        StyleProperties::Line::Explosion}) {
            styleOf(a).line = line;
            EXPECT_FALSE(Geometry::hasOutline(*a.balloon));
            EXPECT_FALSE(Geometry::hasFilter(a));

            for (int stretch : {FlashParams::k_autoStretch, 0, 100}) {
                for (quint32 seed : {1u, 2u, 0xC0FFEEu, 0xFFFFFFFFu}) {
                    styleOf(a).flash.coreStretch = stretch;
                    seedOf(a)              = seed;
                    const QPainterPath ink = Geometry::ink(*a.balloon, a.seed, Geometry::silhouette(*a.balloon, a.seed));
                    ASSERT_FALSE(ink.isEmpty()) << id << " " << lineName(line);
                    EXPECT_TRUE(Geometry::bounds(a).contains(ink.boundingRect()))
                        << id << " (" << shapeName(shape) << ") " << lineName(line) << " stretch " << stretch
                        << " seed " << seed;
                }
            }

            seedOf(a)             = 7;
            const QPainterPath once = Geometry::ink(*a.balloon, a.seed, Geometry::silhouette(*a.balloon, a.seed));
            EXPECT_TRUE(once == Geometry::ink(*a.balloon, a.seed, Geometry::silhouette(*a.balloon, a.seed))) << id << " " << lineName(line);
            seedOf(a) = 8;
            if (line != StyleProperties::Line::Dashed && line != StyleProperties::Line::Multi)   // nothing random in them
                EXPECT_FALSE(once == Geometry::ink(*a.balloon, a.seed, Geometry::silhouette(*a.balloon, a.seed))) << id << " " << lineName(line);
        }
    }
}

/**
 * \brief A dashed line puts a whole number of dashes round each lap, the nearest to its length over a
 *        dash and a gap, and a gap of 0 is one whole line: a band, its outer edge and its inner one.
 */
TEST(Style, ADashedLineFitsWholeDashesRoundEachLap)
{
    for (const char* id : {"ellipse", "caption", "shout"}) {
        Composition a = balloonObject();
        shapeOf(a)           = Geometry::shapeTemplate(id).shape;
        boxOf(a)             = QSize(280, 160);
        tailsOf(a).clear();
        styleOf(a).line      = StyleProperties::Line::Dashed;
        styleOf(a).dash.length = 12.0;
        styleOf(a).dash.gap    = 8.0;
        const QPainterPath             silhouette = Geometry::silhouette(*a.balloon, a.seed);
        const QList<Geometry::OutlineRing> edges   = Geometry::outlineModel(*a.balloon, a.seed, silhouette).rings;
        ASSERT_EQ(edges.size(), 1) << id;
        const int dashes = qRound(edges.first().walk.length() / (styleOf(a).dash.length + styleOf(a).dash.gap));
        EXPECT_EQ(Geometry::ink(*a.balloon, a.seed, silhouette).toSubpathPolygons().size(), dashes) << id;

        styleOf(a).dash.gap = 0.0;
        EXPECT_EQ(Geometry::ink(*a.balloon, a.seed, silhouette).toSubpathPolygons().size(), 2) << id;
    }
}

/**
 * \brief A dashed line ends every tail inked, whatever its length and its tip: a dash is anchored on the tip, or on the
 *        middle of a Cut or a Round end, so no gap falls there.
 */
TEST(Style, ADashedLineEndsEveryTailInked)
{
    constexpr qreal k_back  = 1.0;   // how far back from the tip, along the tail, the probe stands
    constexpr int   k_steps = 12;    // tail lengths tried, a pixel apart: the dashes fall differently on each
    Composition a = balloonObject();
    shapeOf(a)             = Geometry::shapeTemplate("ellipse").shape;
    boxOf(a)               = QSize(280, 160);
    styleOf(a).line        = StyleProperties::Line::Dashed;
    styleOf(a).strokeWidth = 4;
    styleOf(a).dash.length = 12.0;
    styleOf(a).dash.gap    = 10.0;
    Tail t;
    t.baseWidth      = 40.0;
    t.look.end.width = 50;
    for (TailLook::Tip tip : {TailLook::Tip::Sharp, TailLook::Tip::Cut, TailLook::Tip::Round}) {
        t.look.tip = tip;
        for (int k = 0; k < k_steps; ++k) {
            t.tip = QPointF(140, 400 + k);
            setTails(a, {t});
            const QPainterPath ink = Geometry::ink(*a.balloon, a.seed, Geometry::silhouette(*a.balloon, a.seed));
            EXPECT_TRUE(ink.contains(t.tip - QPointF(0.0, k_back))) << int(tip) << " at " << t.tip.y();
        }
    }
}

/**
 * \brief Where a Circles tail's first circle only just meets its bubble, the outline turns almost back on itself at
 *        the pinch: a brush's or a dash's corner there is bevelled, not mitred into a spike across the gap.
 *
 * The author's chain of three ellipses with a Circles tail from two of them, where both lines spiked. Its outline has
 * no convex corner (ellipses, and the concave ones where parts meet), so a ribbon about it lies within half its width
 * of it everywhere, give or take the flattening: a spike across a gap does not.
 */
TEST(Style, ALineDoesNotSpikeWhereATailOnlyJustTouches)
{
    constexpr qreal k_flattening = 1.0;   // how far a flattened outline may be from a true one
    Composition a = compositionFromJson(QJsonDocument::fromJson(R"({"balloon":{"bodies":[
        {"id":2,"place":{"pos":[113.63,20.82]},"shape":{"edge":"none","nodes":[],"shape":"ellipse"},"size":[116,97]},
        {"id":4,"place":{"pos":[180.9,78.6]},"shape":{"edge":"none","nodes":[],"shape":"ellipse"},"size":[112,82]},
        {"id":8,"place":{"pos":[275.57,24.46]},"shape":{"edge":"none","nodes":[],"shape":"ellipse"},"size":[112,103]}],
        "id":1,"tails":[
        {"bend":0,"id":5,"look":{"style":"circles","tip":"sharp"},"root":4,"tip":[230.16,267.55],"width":31},
        {"bend":0,"id":7,"look":{"style":"plain","tip":"sharp"},"root":2,"tip":[0,0],"to":4,"width":34},
        {"bend":0,"id":10,"look":{"style":"plain","tip":"sharp"},"root":4,"tip":[0,0],"to":8,"width":34},
        {"bend":0,"id":13,"look":{"style":"circles","tip":"sharp"},"root":8,"tip":[357.9,277.43],"width":34}],
        "style":{"line":{"line":"dashed","strokeWidth":5}}},"nextId":14,"seed":0,"words":[]})").object());
    ASSERT_TRUE(a.balloon);
    styleOf(a).brush.drift = 0.0;
    styleOf(a).brush.swell = 0;
    const qreal         half       = styleOf(a).strokeWidth / 2.0;
    const QPainterPath  silhouette = Geometry::silhouette(*a.balloon, a.seed);
    QPainterPathStroker reach;
    reach.setWidth(2.0 * (half + k_flattening));
    const QPainterPath allowed = reach.createStroke(silhouette).united(silhouette);
    for (StyleProperties::Line line : {StyleProperties::Line::Brush, StyleProperties::Line::Dashed}) {
        styleOf(a).line   = line;
        const QRectF spike = Geometry::ink(*a.balloon, a.seed, silhouette).subtracted(allowed).boundingRect();
        EXPECT_LT(qMax(spike.width(), spike.height()), k_flattening) << lineName(line) << " " << spike.width() << "x"
                                                                    << spike.height();
    }
}

/**
 * \brief A brush's strokes wander off the edge, but never so far that it is left bare: the fill would show past the
 *        ink where a stroke swells thin or narrows to its end. Every point of an ellipse's edge lies under the ink,
 *        whatever the seed, with the default brush.
 */
TEST(Style, ABrushNeverLeavesTheEdgeBare)
{
    constexpr qreal   k_probe = 1.0;   // the edge sampled this far apart
    constexpr quint32 k_seeds = 12;
    Composition a = balloonObject();
    shapeOf(a)             = Geometry::shapeTemplate("ellipse").shape;
    boxOf(a)               = QSize(280, 160);
    tailsOf(a).clear();
    styleOf(a).line        = StyleProperties::Line::Brush;
    styleOf(a).brush       = BrushParams{};
    styleOf(a).strokeWidth = 5;
    for (quint32 seed = 1; seed <= k_seeds; ++seed) {
        seedOf(a)                     = seed;
        const QPainterPath silhouette = Geometry::silhouette(*a.balloon, a.seed);
        const QPainterPath ink        = Geometry::ink(*a.balloon, a.seed, silhouette);
        int                bare       = 0;
        for (const QPolygonF& ring : silhouette.toSubpathPolygons())
            for (int i = 0; i + 1 < ring.size(); ++i) {
                const qreal len = QLineF(ring[i], ring[i + 1]).length();
                for (qreal s = 0.0; s < len; s += k_probe)
                    bare += ink.contains(ring[i] + (ring[i + 1] - ring[i]) * (s / len)) ? 0 : 1;
            }
        EXPECT_EQ(bare, 0) << "seed " << seed;
    }
}

namespace {

/**
 * \brief @p item painted as the strip editor paints it (Painter::play(): marks stamped or in pieces, a stroke
 *        in pieces) looks as @p whole draws it in one go: alike on average, and few pixels far apart, at a zoom.
 */
void expectPaintedAsWhole(const Render::Item& item, const std::function<void(QPainter&)>& whole)
{
    constexpr qreal k_zoom      = 2.0;
    constexpr qreal k_meanAlpha = 4.0;     // of 255, over the whole picture
    constexpr int   k_farAlpha  = 64;      // a pixel this far apart counts...
    constexpr qreal k_farShare  = 0.005;   // ...and so many of them may be
    const qreal  half    = item.strokeWidth / 2.0;
    const QRectF area    = item.path.boundingRect().adjusted(-half, -half, half, half);
    const auto   picture = [&](const std::function<void(QPainter&)>& draw) {
        QImage img((area.size() * k_zoom).toSize() + QSize(2, 2), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter painter(&img);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(k_zoom, k_zoom);
        painter.translate(-area.topLeft());
        draw(painter);
        return img;
    };
    const QImage painted = picture([&](QPainter& painter) { Painter::play(painter, {item}); });
    const QImage path    = picture(whole);

    qint64 sum = 0;
    int    far = 0;
    for (int y = 0; y < path.height(); ++y)
        for (int x = 0; x < path.width(); ++x) {
            const int d = std::abs(qAlpha(path.pixel(x, y)) - qAlpha(painted.pixel(x, y)));
            sum += d;
            far += d > k_farAlpha ? 1 : 0;
        }
    const qreal pixels = qreal(path.width()) * path.height();
    EXPECT_LT(sum / pixels, k_meanAlpha);
    EXPECT_LT(far / pixels, k_farShare);
}

//! @p marks, as a marks item in black, painted as the same path filled whole by the nonzero rule.
void expectPaintedAsItsPath(const QPainterPath& marks)
{
    Render::Item item;
    item.path    = marks;
    item.fill    = Render::colourInk(Qt::black);
    item.nonzero = true;
    item.marks   = true;
    QPainterPath whole = marks;
    whole.setFillRule(Qt::WindingFill);
    expectPaintedAsWhole(item, [&](QPainter& painter) { painter.fillPath(whole, Qt::black); });
}

}  // namespace

/**
 * \brief A fading halftone's marks paint as their path: dots (stamped) merging on a turned grid, and a grid's
 *        bars, whose crossings stay inked.
 */
TEST(Style, AFadingHalftonePaintsAsItsPath)
{
    Paint p;
    p.colour  = Qt::white;
    p.second  = Qt::black;
    p.size    = 7.0;   // over the spacing: the darkest dots merge
    p.spacing = 6.0;
    p.angle   = 45.0;
    // A large area, and one small enough that its marks are filled in one piece, crossings and all.
    for (const QRectF& area : {QRectF(0, 0, 240, 120), QRectF(0, 0, 30, 20)})
        for (Paint::Kind kind : {Paint::Kind::Dots, Paint::Kind::Grid}) {
            SCOPED_TRACE(QStringLiteral("%1 wide, kind %2").arg(area.width()).arg(int(kind)).toStdString());
            p.kind          = kind;
            const auto fade = [&](const QPointF& at) { return at.x() / area.width(); };
            expectPaintedAsItsPath(Geometry::halftoneMarks(p, area, fade, 3.0));
        }
}

/**
 * \brief Lettering's outline, stroked round many glyphs (in pieces on screen), paints as one stroke of it does,
 *        in a transparent colour too (the pieces overlap where letters' outlines meet).
 */
TEST(Style, ALetteringOutlinePaintsAsOneStroke)
{
    Composition a = balloonObject();
    tailsOf(a).clear();
    boxOf(a)                 = QSize(400, 300);
    textOf(a).outlineWidth   = 1.5;
    textOf(a).body           = QStringLiteral("Many letters, outlined, in pieces round every one of their contours.");
    const QPainterPath text  = Geometry::textOutlines(a).value(0);
    ASSERT_GT(text.toSubpathPolygons().size(), 20);
    for (const QColor& colour : {QColor(Qt::black), QColor(0, 0, 0, 128)}) {
        SCOPED_TRACE(colour.alpha());
        Render::Item item;
        item.path        = text;
        item.stroke      = Render::colourInk(colour);
        item.strokeWidth = 2.0 * textOf(a).outlineWidth;
        item.roundCap    = true;
        expectPaintedAsWhole(item, [&](QPainter& painter) {
            QPen pen(colour, item.strokeWidth);
            pen.setJoinStyle(Qt::RoundJoin);
            pen.setCapStyle(Qt::RoundCap);
            painter.strokePath(text, pen);
        });
    }
}

/**
 * \brief A line drawn as strokes apart paints as its path, in pieces. The others have a band with a hole: a dry
 *        brush's core (Explosion's too), a sketch's strokes, a dash closed round a ring, a multi-line's lines.
 */
TEST(Style, ALineOfStrokesApartPaintsAsItsPath)
{
    using Line    = StyleProperties::Line;
    Composition a = balloonObject();
    shapeOf(a)    = Geometry::shapeTemplate("ellipse").shape;
    boxOf(a)      = QSize(280, 160);
    for (Line line : {Line::Flash, Line::Brush, Line::Drybrush, Line::Sketch, Line::Explosion, Line::Dashed,
                      Line::Multi}) {
        SCOPED_TRACE(int(line));
        styleOf(a).line               = line;
        const QPainterPath silhouette = Geometry::silhouette(*a.balloon, a.seed);
        const QPainterPath ink        = Geometry::ink(*a.balloon, a.seed, silhouette);
        ASSERT_FALSE(ink.isEmpty());
        const bool apart = Geometry::lineInfo(line).apart;
        EXPECT_EQ(apart, line == Line::Flash || line == Line::Brush);
        if (apart)
            expectPaintedAsItsPath(ink);
    }
}

/**
 * \brief A multi-line is the outline and lines inside it: measured across a caption's straight left side,
 *        ink on the outline, none in a gap, ink in each inner line, none past the last.
 */
TEST(Style, AMultiLineDrawsItsLinesInside)
{
    Composition a = balloonObject();
    shapeOf(a) = Geometry::shapeTemplate("caption").shape;
    boxOf(a)   = QSize(280, 160);
    tailsOf(a).clear();
    styleOf(a).line        = StyleProperties::Line::Multi;
    styleOf(a).strokeWidth = 4;
    styleOf(a).multi.gap   = 6.0;
    styleOf(a).multi.width = 3.0;
    const QPainterPath silhouette = Geometry::silhouette(*a.balloon, a.seed);
    const QPainterPath ink        = Geometry::ink(*a.balloon, a.seed, silhouette);
    const QRectF       r          = silhouette.boundingRect();
    // x in from the silhouette's left edge, at its middle height, where a caption's side is straight
    const auto in = [&](qreal x) { return ink.contains(QPointF(r.left() + x, r.center().y())); };
    EXPECT_TRUE(in(0.5));                      // on the outline
    EXPECT_FALSE(in(2.0 + 3.0));               // in the gap
    EXPECT_TRUE(in(2.0 + 6.0 + 1.5));          // the inner line's middle
    EXPECT_FALSE(in(2.0 + 6.0 + 3.0 + 2.0));   // inside, past it

    styleOf(a).multi.lines = 3;
    const QPainterPath three = Geometry::ink(*a.balloon, a.seed, silhouette);
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
    Composition a = balloonObject();
    shapeOf(a) = Geometry::shapeTemplate("ellipse").shape;
    boxOf(a)   = QSize(280, 160);
    tailsOf(a).clear();
    styleOf(a).line         = StyleProperties::Line::Sketch;
    styleOf(a).sketch.length = 40.0;
    styleOf(a).sketch.core   = 0;
    const QPainterPath              silhouette = Geometry::silhouette(*a.balloon, a.seed);
    const QList<Geometry::OutlineRing> edges   = Geometry::outlineModel(*a.balloon, a.seed, silhouette).rings;
    ASSERT_EQ(edges.size(), 1);
    for (int layers : {1, 3}) {
        styleOf(a).sketch.layers = layers;
        EXPECT_EQ(Geometry::ink(*a.balloon, a.seed, silhouette).toSubpathPolygons().size(),
                  qRound(layers * edges.first().walk.length() / styleOf(a).sketch.length)) << layers;
    }

    // Blots add their spots and short strokes, a stream of their own: the strokes stay as they were.
    styleOf(a).sketch.layers = 1;
    const QList<QPolygonF> strokes = Geometry::ink(*a.balloon, a.seed, silhouette).toSubpathPolygons();
    styleOf(a).sketch.blots = 100;
    const QList<QPolygonF> blotted = Geometry::ink(*a.balloon, a.seed, silhouette).toSubpathPolygons();
    ASSERT_GT(blotted.size(), strokes.size());
    EXPECT_TRUE(blotted.mid(0, strokes.size()) == strokes);
    styleOf(a).sketch.blots = 0;

    // A stroke a lap long at most: longer, it is as many as one a lap long.
    styleOf(a).sketch.length = 100000.0;
    EXPECT_EQ(Geometry::ink(*a.balloon, a.seed, silhouette).toSubpathPolygons().size(), 1);

    // With no strokes to speak of, the outline alone still rings the whole silhouette.
    styleOf(a).sketch.core        = 25;
    styleOf(a).sketch.strokeWidth = 0;
    styleOf(a).strokeWidth       = 8;
    const QPainterPath ink = Geometry::ink(*a.balloon, a.seed, silhouette);
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
    Composition a = balloonObject();
    shapeOf(a)                  = Geometry::shapeTemplate("ellipse").shape;
    boxOf(a)                    = QSize(280, 160);
    styleOf(a).line             = StyleProperties::Line::Drybrush;
    styleOf(a).strokeWidth      = 16;   // half 8: a core of 2, four lanes of 1.5 a side
    styleOf(a).drybrush.core     = 25;
    styleOf(a).drybrush.bristles = 8;
    seedOf(a)              = 0xC0FFEEu;
    const QPainterPath silhouette = Geometry::silhouette(*a.balloon, a.seed);
    const QPolygonF    edge       = silhouette.toFillPolygon();
    const auto inked = [&](int dryness, std::initializer_list<qreal> offsets) {
        styleOf(a).drybrush.dryness = dryness;
        const QPainterPath ink  = Geometry::ink(*a.balloon, a.seed, silhouette);
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
    setTails(a, {t});
    styleOf(a).drybrush.dryness = 0;
    const QLineF  axis(Geometry::balloonRect(*a.balloon, *bodyOf(a)).center(), t.tip);
    const QPointF beside = QPointF(-axis.dy(), axis.dx()) / axis.length() * 4.8;
    for (quint32 seed : {1u, 2u, 0xC0FFEEu}) {
        seedOf(a)            = seed;
        const QPainterPath ink = Geometry::ink(*a.balloon, a.seed, Geometry::silhouette(*a.balloon, a.seed));
        EXPECT_TRUE(ink.contains(t.tip)) << seed;
        EXPECT_FALSE(ink.contains(t.tip + beside)) << seed;
        EXPECT_FALSE(ink.contains(t.tip - beside)) << seed;
    }

    // The hairs follow each part on its own edge and run on under another only a little: the balloon's
    // outline deep under a wide tail, far from both roots and from the tail's sides, has none.
    {
        Composition e = a;
        shapeOf(e)        = Geometry::shapeTemplate("ellipse").shape;
        boxOf(e)          = QSize(280, 160);
        Tail wide;
        wide.tip       = QPointF(140, 420);
        wide.baseWidth = 160;
        setTails(e, {wide});
        const QRectF  body   = Geometry::balloonRect(*e.balloon, *bodyOf(e));
        const QPointF bottom(body.center().x(), body.bottom());
        for (quint32 seed = 1; seed <= 8; ++seed) {
            seedOf(e)            = seed;
            const QPainterPath ink = Geometry::ink(*e.balloon, e.seed, Geometry::silhouette(*e.balloon, e.seed));
            for (qreal off : {-6.0, -3.0, 0.0, 3.0, 6.0})
                EXPECT_FALSE(ink.contains(bottom + QPointF(0, off))) << seed << " at " << off;
        }
    }

    // A concave corner (an L's inside, as a tail's root): each hair runs on to the other side's core and
    // stops there. Outside, the two sides' hairs cross in the angle, so the line halving it is inked here
    // and there; inside, nothing runs on into the fill, past where either side's band is.
    tailsOf(a).clear();
    shapeOf(a).kind         = ShapeProperties::Kind::Polygon;
    shapeOf(a).corner.radius = 0.0;
    shapeOf(a).nodes        = {{0, 0}, {1, 0}, {1, 0.5}, {0.5, 0.5}, {0.5, 1}, {0, 1}};
    boxOf(a)                = QSize(200, 200);
    const QRectF  body   = Geometry::balloonRect(*a.balloon, *bodyOf(a));
    const QPointF inside = body.topLeft() + QPointF(body.width() / 2, body.height() / 2);
    styleOf(a).drybrush.dryness = 0;
    int rootHits = 0;
    for (quint32 seed = 1; seed <= 8; ++seed) {
        seedOf(a)            = seed;
        const QPainterPath ink = Geometry::ink(*a.balloon, a.seed, Geometry::silhouette(*a.balloon, a.seed));
        for (qreal d : {4.5, 6.0, 7.5, 9.0})
            rootHits += ink.contains(inside + QPointF(d, d) / std::sqrt(2.0)) ? 1 : 0;
    }
    EXPECT_GT(rootHits, 0);
    for (quint32 seed = 1; seed <= 8; ++seed) {
        seedOf(a)            = seed;
        const QPainterPath ink = Geometry::ink(*a.balloon, a.seed, Geometry::silhouette(*a.balloon, a.seed));
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
    Composition a = balloonObject();
    shapeOf(a)             = Geometry::shapeTemplate("ellipse").shape;
    boxOf(a)               = QSize(280, 160);
    styleOf(a).strokeWidth = 0;
    Tail t;
    t.tip                 = QPointF(140, 400);
    t.baseWidth           = 40.0;
    t.look.style          = TailLook::Style::Circles;
    t.look.circles.count  = 3;
    t.look.circles.pitch  = 10.0;
    t.look.circles.offset = -8.0;   // a gap below the balloon
    setTails(a, {t});
    const QRectF body   = Geometry::balloonRect(*a.balloon, *bodyOf(a));

    QList<QPolygonF> rings = Geometry::silhouette(*a.balloon, a.seed).toSubpathPolygons();
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
    EXPECT_NEAR(rings[2].boundingRect().top() - first.bottom(), t.look.circles.pitch, 0.5);
    EXPECT_NEAR(last.top() - rings[2].boundingRect().bottom(), t.look.circles.pitch, 0.5);

    // Sunk into the balloon, the first one melts into it.
    tailsOf(a).first().tail.look.circles.offset = 8.0;
    EXPECT_EQ(Geometry::silhouette(*a.balloon, a.seed).toSubpathPolygons().size(), 3);
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
    Composition a = balloonObject();
    shapeOf(a)             = Geometry::shapeTemplate("ellipse").shape;
    boxOf(a)               = QSize(280, 160);
    styleOf(a).strokeWidth = 0;
    Tail t;
    t.tip            = QPointF(140, 400);
    t.baseWidth      = 40.0;
    t.look.end.width = 50;
    const qreal end  = t.baseWidth / 2.0 * t.look.end.width / 100.0;   // either side of the middle
    setTails(a, {t});
    const auto at    = [&](TailLook::Tip tip, QPointF p) {
        tailsOf(a).first().tail.look.tip = tip;
        return Geometry::silhouette(*a.balloon, a.seed).contains(p);
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
    // a Round end half a circle long with no corner on it, a Cut one straight across, a Sharp one a point at the tip.
    constexpr qreal k_flattening = 1.0;   // how far a flattened end's length may be from a true one's
    constexpr qreal k_same       = 1e-9;
    for (TailLook::Tip tip : {TailLook::Tip::Sharp, TailLook::Tip::Round, TailLook::Tip::Cut}) {
        tailsOf(a).first().tail.look.tip = tip;
        const Geometry::OutlineModel m = Geometry::outlineModel(*a.balloon, a.seed, Geometry::silhouette(*a.balloon, a.seed));
        ASSERT_EQ(m.parts.size(), 2);
        ASSERT_EQ(m.rings.size(), 1);
        for (const Geometry::OutlineRing& ring : {m.parts[1].rings.first(), m.rings.first()}) {
            ASSERT_EQ(ring.ends.size(), 1u);
            if (tip == TailLook::Tip::Sharp) {
                const Geometry::OutlineEnd& e = ring.ends.front();
                EXPECT_TRUE(e.sharp);
                EXPECT_FALSE(e.round);
                EXPECT_EQ(e.span, 0.0);
                const QPointF at = ring.walk.at(ring.wrap(e.middle())).at;
                EXPECT_NEAR(at.x(), t.tip.x(), k_flattening);
                EXPECT_NEAR(at.y(), t.tip.y(), k_flattening);
                continue;
            }
            const Geometry::OutlineEnd& e     = ring.ends.front();
            const bool                  round = tip == TailLook::Tip::Round;
            EXPECT_EQ(e.round, round);
            EXPECT_NEAR(e.radius, end, k_same);
            EXPECT_NEAR(e.span, round ? M_PI * end : 2.0 * end, k_flattening);
            for (const Geometry::OutlineCorner& c : ring.corners)
                EXPECT_FALSE(round && ring.within(e, c.at)) << c.at;
        }
    }
    tailsOf(a).first().tail.look.tip = TailLook::Tip::Sharp;

    // A Circles tail has no tip: the same chain whatever it says.
    tailsOf(a).first().tail.look.style = TailLook::Style::Circles;
    tailsOf(a).first().tail.look.tip   = TailLook::Tip::Sharp;
    const QPainterPath chain         = Geometry::silhouette(*a.balloon, a.seed);
    tailsOf(a).first().tail.look.tip = TailLook::Tip::Cut;
    EXPECT_TRUE(chain == Geometry::silhouette(*a.balloon, a.seed));
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
    Composition a = balloonObject();
    shapeOf(a)             = Geometry::shapeTemplate("ellipse").shape;
    boxOf(a)               = QSize(280, 160);
    styleOf(a).strokeWidth = 0;
    Tail t;
    t.tip               = QPointF(140, 460);
    t.baseWidth         = 30.0;
    t.look.breaks.count = 2;
    t.look.breaks.jog   = 20;
    setTails(a, {t});
    const QPainterPath straight = [&] {
        Composition plain             = a;
        tailsOf(plain).first().tail.look.breaks.count = 0;
        return Geometry::silhouette(*plain.balloon, plain.seed);
    }();
    const QPainterPath broken = Geometry::silhouette(*a.balloon, a.seed);
    EXPECT_FALSE(broken == straight);

    // The tail's straight line runs from inside the balloon to the tip; a third and two thirds of the way
    // its breaks stand a fifth of its length to either side, where the straight tail is not.
    const QRectF  body  = Geometry::balloonRect(*a.balloon, *bodyOf(a));
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
    tailsOf(a).first().tail.look.breaks.clockwise = false;
    EXPECT_TRUE(zigzags(Geometry::silhouette(*a.balloon, a.seed), 1.0));
    // A Circles tail takes no breaks.
    tailsOf(a).first().tail.look.style = TailLook::Style::Circles;
    const QPainterPath chain         = Geometry::silhouette(*a.balloon, a.seed);
    tailsOf(a).first().tail.look.breaks.count = 0;
    EXPECT_TRUE(chain == Geometry::silhouette(*a.balloon, a.seed));
}

//! A flash left on Auto points its strokes exactly as the shape's own stretch would.
TEST(Style, AutoStretchIsTheShapes)
{
    for (int i = 0; i <= int(ShapeProperties::k_lastKind); ++i) {
        Composition a = balloonObject();
        shapeOf(a).kind = static_cast<ShapeProperties::Kind>(i);
        boxOf(a)        = QSize(300, 140);
        styleOf(a).line = StyleProperties::Line::Flash;
        seedOf(a)  = 7;
        const int own = Geometry::shapeInfo(shapeOf(a).kind).flashStretch;
        EXPECT_GE(own, 0) << shapeName(shapeOf(a).kind);
        EXPECT_LE(own, 100) << shapeName(shapeOf(a).kind);

        ASSERT_EQ(styleOf(a).flash.coreStretch, FlashParams::k_autoStretch);   // the default is Auto
        const QPainterPath autoInk = Geometry::ink(*a.balloon, a.seed, Geometry::silhouette(*a.balloon, a.seed));
        styleOf(a).flash.coreStretch   = own;
        EXPECT_TRUE(autoInk == Geometry::ink(*a.balloon, a.seed, Geometry::silhouette(*a.balloon, a.seed))) << shapeName(shapeOf(a).kind);
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
    Composition a = balloonObject();
    shapeOf(a).kind         = ShapeProperties::Kind::Polygon;
    shapeOf(a).corner.radius = 0.0;
    shapeOf(a).nodes = {{0, 0}, {0.3, 0}, {0.3, 0.4}, {0.7, 0.4}, {0.7, 0}, {1, 0}, {1, 1}, {0, 1}};
    boxOf(a)              = QSize(200, 200);
    styleOf(a).strokeWidth = 0;   // the balloon is then the box, and the nodes are these pixels
    Tail t;
    t.tip         = QPointF(-40, -200);
    setTails(a, {t});

    ASSERT_TRUE(Geometry::tailsDrawn(*a.balloon, a.seed).value(0));
    const QPointF inTheNotch(72, 40);   // on the line, between the notch's floor and the prong
    ASSERT_FALSE(Geometry::outlineOf(*bodyOf(a), Geometry::balloonRect(*a.balloon, *bodyOf(a)), a.seed).contains(inTheNotch));
    EXPECT_FALSE(Geometry::silhouette(*a.balloon, a.seed).contains(inTheNotch));
    EXPECT_TRUE(Geometry::silhouette(*a.balloon, a.seed).contains(QPointF(16, -80)));   // ...and the tail is there, on its line
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
    Composition box = balloonObject();
    shapeOf(box).kind         = ShapeProperties::Kind::Box;
    shapeOf(box).corner.radius = 30.0;
    boxOf(box)                = QSize(200, 100);
    styleOf(box).strokeWidth   = 0;
    Composition square = box;
    shapeOf(square).kind   = ShapeProperties::Kind::Polygon;
    shapeOf(square).nodes  = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    const QRectF       body = Geometry::balloonRect(*box.balloon, *bodyOf(box));
    const QPainterPath a    = Geometry::outlineOf(*bodyOf(box), body, box.seed);
    const QPainterPath b    = Geometry::outlineOf(*bodyOf(square), body, square.seed);
    for (const QPointF& p : {QPointF(3, 3), QPointF(12, 12), QPointF(197, 97), QPointF(100, 1), QPointF(1, 50)})
        EXPECT_EQ(a.contains(p), b.contains(p)) << p.x() << "," << p.y();
    EXPECT_FALSE(b.contains(QPointF(3, 3)));   // the corner is cut, not bulged

    shapeOf(square).corner.radius = 1000.0;
    const QRectF drawn = Geometry::outlineOf(*bodyOf(square), body, square.seed).boundingRect();
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
        for (ShapeProperties::Edge edge : {ShapeProperties::Edge::None, ShapeProperties::Edge::Zigzag}) {
            ShapeProperties shape = t.shape;
            shape.edge            = edge;
            if (!Geometry::hasShapeHandles(shape))
                continue;
            for (qreal radius : {0.0, 20.0}) {
                shape.corner.radius = radius;
                for (int picked = -1; picked < int(shape.nodes.size()); ++picked) {
                    for (const Geometry::Handle& h : Geometry::shapeHandles(shape, body, clearance, picked)) {
                        if (Geometry::partOf<Geometry::ShapePart>(h) == Geometry::ShapePart::AddVertex)
                            continue;   // a press adds a node; it has nothing to drag
                        const ShapeProperties dragged = Geometry::dragShapeHandle(shape, body, h, h.at, clearance);
                        EXPECT_TRUE(dragged == shape) << t.id << " edge " << edgeName(edge) << " radius " << radius
                                                      << " picked " << picked << " handle " << h.part << ":"
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
    for (int e = int(ShapeProperties::Edge::Zigzag); e <= int(ShapeProperties::k_lastEdge); ++e)
    for (const char* id : {"speech", "ellipse", "shout", "scroll", "thought"}) {
        const auto edge = static_cast<ShapeProperties::Edge>(e);
        Composition plain = balloonObject();
        shapeOf(plain) = Geometry::shapeTemplate(id).shape;
        boxOf(plain)   = QSize(300, 160);
        Composition edged           = plain;
        shapeOf(edged).edge             = edge;
        shapeOf(edged).teeth.depth        = 9.0;
        shapeOf(edged).teeth.irregularity = 50;
        seedOf(edged)              = 7;

        const QRectF body = Geometry::balloonRect(*plain.balloon, *bodyOf(plain));
        const QRectF bare = Geometry::outlineOf(*bodyOf(plain), body, plain.seed).boundingRect();
        EXPECT_TRUE(bare.adjusted(-0.01, -0.01, 0.01, 0.01).contains(Geometry::outlineOf(*bodyOf(edged), body, edged.seed).boundingRect()))
            << id << " " << edgeName(edge);
        const QRectF a = Geometry::textArea(plain, *wordsOf(plain));
        const QRectF b = Geometry::textArea(edged, *wordsOf(edged));
        EXPECT_NEAR(b.width(), a.width() - 2 * 9.0, 1e-6) << id << " " << edgeName(edge);
        EXPECT_NEAR(b.height(), a.height() - 2 * 9.0, 1e-6) << id << " " << edgeName(edge);
        EXPECT_EQ(Geometry::templateOf(shapeOf(edged)), &Geometry::shapeTemplate(id)) << id << " " << edgeName(edge);

        const QPainterPath once = Geometry::outlineOf(*bodyOf(edged), body, edged.seed);
        EXPECT_TRUE(once == Geometry::outlineOf(*bodyOf(edged), body, edged.seed)) << id << " " << edgeName(edge);
        seedOf(edged) = 8;
        EXPECT_FALSE(once == Geometry::outlineOf(*bodyOf(edged), body, edged.seed)) << id << " " << edgeName(edge);
    }
}

/**
 * \brief A shadow is the silhouette moved, and grown by a band as wide as its spread drawn over it (no union
 *        of the two), inside the room reserved for it; only a soft one is a filter: a hard one is drawn as
 *        it is edited, a soft one by the library.
 */
TEST(Effects, AShadowStaysInItsBoundsAndOnlyASoftOneIsAFilter)
{
    Composition a = balloonObject();
    shapeOf(a) = Geometry::shapeTemplate("speech").shape;
    Tail t;
    t.tip         = QPointF(60, 230);
    setTails(a, {t});
    EXPECT_TRUE(Geometry::shadowOf(effectsOf(a).shadow, effectsOf(a).shadowFill, Geometry::silhouette(*a.balloon, a.seed)).isEmpty());

    effectsOf(a).shadow.on = true;
    for (const QPointF offset : {QPointF(12, 9), QPointF(-20, -3), QPointF(0, 30), QPointF(0, 0)}) {
        for (qreal grow : {0.0, 6.0, 15.0}) {
            for (qreal softness : {0.0, 4.0}) {
                effectsOf(a).shadow.spread   = grow;
                effectsOf(a).shadow.x        = offset.x();
                effectsOf(a).shadow.y        = offset.y();
                effectsOf(a).shadow.softness = softness;
                const QPainterPath          silhouette = Geometry::silhouette(*a.balloon, a.seed);
                const Geometry::ShadowPaths shadow     = Geometry::shadowOf(effectsOf(a).shadow, effectsOf(a).shadowFill, silhouette);
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
    Composition a = balloonObject();
    shapeOf(a)                      = Geometry::shapeTemplate("speech").shape;
    effectsOf(a).shadow.on             = true;
    effectsOf(a).shadow.spread       = 0.0;
    effectsOf(a).shadow.x            = 0.0;
    effectsOf(a).shadow.y            = 0.0;
    effectsOf(a).shadowFill.colour  = QColor(10, 20, 30, 200);
    effectsOf(a).shadowFill.spacing = 8.0;
    effectsOf(a).shadowFill.size    = 4.0;
    effectsOf(a).shadowFill.angle   = 0;
    const QPainterPath silhouette = Geometry::silhouette(*a.balloon, a.seed);

    for (int k = 1; k <= int(Paint::k_lastKind); ++k) {
        effectsOf(a).shadowFill.kind = static_cast<Paint::Kind>(k);
        // Hard: the moved shape, filled with the pattern's marks in the shadow's colour, and no ground.
        effectsOf(a).shadow.softness = 0.0;
        EXPECT_FALSE(Geometry::fadesShadow(effectsOf(a).shadow, effectsOf(a).shadowFill));
        EXPECT_TRUE(Geometry::shadowOf(effectsOf(a).shadow, effectsOf(a).shadowFill, silhouette).body == silhouette);
        const Paint hard = Geometry::shadowPaint(effectsOf(a).shadow, effectsOf(a).shadowFill);
        EXPECT_EQ(hard.kind, effectsOf(a).shadowFill.kind);
        EXPECT_EQ(hard.colour.alpha(), 0) << "a halftone shadow has no ground";
        EXPECT_EQ(hard.second, effectsOf(a).shadowFill.colour);

        // Soft: its marks, filled in the colour; no filter, and within the blur's reach.
        effectsOf(a).shadow.softness = 4.0;
        EXPECT_TRUE(Geometry::fadesShadow(effectsOf(a).shadow, effectsOf(a).shadowFill));
        EXPECT_FALSE(Geometry::hasFilter(a));
        EXPECT_EQ(Geometry::shadowPaint(effectsOf(a).shadow, effectsOf(a).shadowFill).kind, Paint::Kind::Solid);
        EXPECT_EQ(Geometry::shadowPaint(effectsOf(a).shadow, effectsOf(a).shadowFill).colour, effectsOf(a).shadowFill.colour);
        const QPainterPath marks = Geometry::shadowOf(effectsOf(a).shadow, effectsOf(a).shadowFill, silhouette).body;
        ASSERT_FALSE(marks.isEmpty()) << k;
        EXPECT_DOUBLE_EQ(Geometry::shadowOf(effectsOf(a).shadow, effectsOf(a).shadowFill, silhouette).grow, 0.0);   // marks are grown already
        const qreal  reach = 3.0 * effectsOf(a).shadow.softness;
        const QRectF outer = silhouette.boundingRect().adjusted(-reach, -reach, reach, reach);
        EXPECT_TRUE(outer.contains(marks.boundingRect())) << k;
        EXPECT_TRUE(Geometry::bounds(a).contains(marks.boundingRect())) << k;
    }

    // Deep inside, a dot is the pattern's own; well outside, there is none.
    effectsOf(a).shadowFill.kind = Paint::Kind::Dots;
    const QPainterPath marks  = Geometry::shadowOf(effectsOf(a).shadow, effectsOf(a).shadowFill, silhouette).body;
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
    const QPainterPath spread  = Geometry::shadowOf(effectsOf(a).shadow, effectsOf(a).shadowFill, silhouette).body;
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
    Composition a = balloonObject();
    shapeOf(a)           = Geometry::shapeTemplate("caption").shape;
    boxOf(a)             = QSize(400, 200);
    styleOf(a).strokeWidth = 0;   // the balloon's rectangle is the box
    Paint& p          = skinOf(a).fill;
    p.kind            = Paint::Kind::Dots;
    p.spacing         = 8.0;
    p.size            = 6.0;
    p.angle           = 0;
    const QRectF       body       = Geometry::balloonRect(*a.balloon, *bodyOf(a));
    const QPainterPath silhouette = Geometry::silhouette(*a.balloon, a.seed);
    ASSERT_EQ(body, QRectF(0, 0, 400, 200));
    EXPECT_FALSE(Geometry::fades(p)) << "every arm on its side: no fade";
    EXPECT_TRUE(Geometry::fillTone(*a.balloon, silhouette).isEmpty());

    // Down only, from half way to the side.
    p.fadeBottomFrom = 50.0;
    EXPECT_TRUE(Geometry::fades(p));
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(200, 10)), 1.0);    // above: untouched
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(200, 140)), 1.0);   // before its start
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(200, 175)), 0.5);   // half way along
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(390, 175)), 0.5);   // the same across
    EXPECT_DOUBLE_EQ(Geometry::fadeTone(p, body, QPointF(200, 200)), 0.0);   // at the side

    const QPainterPath marks = Geometry::fillTone(*a.balloon, silhouette);
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
    const QList<Geometry::Handle> cross = Geometry::fadeHandles(still, body);
    ASSERT_EQ(cross.size(), 9);
    EXPECT_EQ(Geometry::partOf<Geometry::FadePart>(cross.at(0)), Geometry::FadePart::From);
    EXPECT_EQ(Geometry::partOf<Geometry::FadePart>(cross.at(4)), Geometry::FadePart::To);
    EXPECT_EQ(Geometry::partOf<Geometry::FadePart>(cross.at(8)), Geometry::FadePart::Centre);
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
    const QList<Geometry::Handle> moved = Geometry::fadeHandles(dragged, body);
    EXPECT_EQ(moved.at(0).at, QPointF(0, 200));     // the left start, on its side
    EXPECT_EQ(moved.at(2).at, QPointF(100, 0));     // the top start
    EXPECT_EQ(moved.at(3).at, QPointF(100, 200));   // the bottom arm has no length left
    EXPECT_EQ(Geometry::dragFadeHandle(dragged, body, moved.at(3), QPointF(100, 150)), dragged)
        << "an arm with no length does not drag";

    // Only the fill lists the fade, and the shadow's pattern no colour of its own: neither is saved there.
    Composition b = a;
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
    rounded.corner.radius    = 6.0;
    for (const ShapeProperties& from : {Geometry::shapeTemplate("speech").shape, Geometry::shapeTemplate("ellipse").shape,
                                        rounded, Geometry::shapeTemplate("thought").shape}) {
        const QPainterPath    before = Geometry::shapeInfo(from.kind).outline(from, body);
        const ShapeProperties curve  = Geometry::curveOf(before, body);
        EXPECT_EQ(curve.kind, ShapeProperties::Kind::Curve);
        EXPECT_EQ(pointsInOnlyOne(before, Geometry::shapeInfo(curve.kind).outline(curve, body), body), 0)
            << shapeName(from.kind);
        // A node per cubic the path has, not a polygon: a cloud's lobe is a few arc pieces.
        if (from.kind == ShapeProperties::Kind::Cloud)
            EXPECT_LE(curve.nodes.size(), 4 * from.cloud.lobes);
    }
    // An ellipse's four quarters meet smoothly, each node's handles as long as each other.
    const ShapeProperties round = Geometry::curveOf(
        Geometry::shapeInfo(ShapeProperties::Kind::Ellipse).outline(Geometry::shapeTemplate("ellipse").shape, body), body);
    ASSERT_EQ(round.nodes.size(), 4);
    for (const ShapeNode& node : round.nodes)
        EXPECT_EQ(node.type, ShapeNode::Type::Symmetric);

    const auto            outline = Geometry::shapeInfo(ShapeProperties::Kind::Curve).outline;
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
        if (t.shape.kind == ShapeProperties::Kind::Polygon || t.shape.kind == ShapeProperties::Kind::Curve) {
            fills(t.shape, t.id);
        } else if (t.shape.kind == ShapeProperties::Kind::Cloud) {
            const QRectF e = Geometry::outlineExtent(Geometry::curveOf(outline(t.shape, body), body), body);
            EXPECT_TRUE(QRectF(0, 0, 1, 1).adjusted(-k_onEdge, -k_onEdge, k_onEdge, k_onEdge).contains(e)) << t.id;
        } else if (outline && t.shape.kind != ShapeProperties::Kind::None) {
            fills(Geometry::curveOf(outline(t.shape, body), body), t.id);
        }
    }
}

/**
 * \brief A picked Curve node offers both its handles, the ones it lacks where they would grow from, and
 *        *Add handles* makes those real without changing the outline. *Convert to Curve* of a speech
 *        balloon gives each arc end one handle, on the arc's side; a node added on a straight side has none.
 */
/**
 * \brief A composition's handles each name their element and carry their owner's drag: the outline's change the
 *        body, the fade's the fill, the arrows the words, and nothing else; a "+" adds its node on the press.
 */
TEST(Handles, EachBelongsToItsElementAndItsOwnerTakesTheDrag)
{
    Composition c       = balloonObject();
    shapeOf(c)          = Geometry::shapeTemplate("diamond").shape;
    skinOf(c).fill.kind = Paint::Kind::Lines;
    textOf(c).body      = QStringLiteral("Hi");
    const Geometry::HandleContext none;
    const QList<Geometry::Handle> all = Geometry::handlesOf(c, none);

    // The outline's first, then the fade's cross (nine), then the text area's anchor and four arrows.
    constexpr qsizetype k_cross  = 9;
    constexpr qsizetype k_arrows = 5;   // the anchor and four edges
    ASSERT_GT(all.size(), k_cross + k_arrows);
    for (qsizetype i = 0; i < all.size(); ++i) {
        const int owner = i >= all.size() - k_arrows           ? wordsOf(c)->id
                        : i >= all.size() - k_arrows - k_cross ? c.balloon->id
                                                               : bodyOf(c)->id;
        EXPECT_EQ(all.at(i).element, owner) << "handle " << i;
        EXPECT_TRUE(all.at(i).drag) << "handle " << i;
    }

    const auto dragged = [&](const Geometry::Handle& h, const QPointF& by) {
        Composition next = c;
        h.drag(next, h, h.at + by, none);
        return next;
    };
    const auto first = [&](Geometry::Handle::Glyph glyph) {
        return *std::find_if(all.cbegin(), all.cend(), [&](const Geometry::Handle& h) { return h.glyph == glyph; });
    };

    const Composition node = dragged(first(Geometry::Handle::Glyph::Node), QPointF(-30, 0));
    EXPECT_FALSE(shapeOf(node) == shapeOf(c));
    EXPECT_TRUE(skinOf(node) == skinOf(c));
    EXPECT_TRUE(textOf(node) == textOf(c));

    const Composition fade = dragged(first(Geometry::Handle::Glyph::FadeCentre), QPointF(20, 10));
    EXPECT_FALSE(skinOf(fade) == skinOf(c));
    EXPECT_TRUE(shapeOf(fade) == shapeOf(c));

    const Geometry::Handle left  = all.at(all.size() - k_arrows + 1);
    const Composition      words = dragged(left, QPointF(10, 0));
    EXPECT_FALSE(*wordsOf(words) == *wordsOf(c));   // its frame

    // A double-click puts an edge back to automatic, and on the middle centres the frame in the body.
    ASSERT_TRUE(left.reset);
    Composition edited = words;
    EXPECT_TRUE(left.reset(edited, left));
    EXPECT_FALSE(wordsOf(edited)->frame.left.has_value());
    EXPECT_FALSE(left.reset(edited, left)) << "automatic already: nothing to put back";
    const Geometry::Handle anchor = all.at(all.size() - k_arrows);
    ASSERT_EQ(anchor.glyph, Geometry::Handle::Glyph::Anchor);
    wordsOf(edited)->frame.left  = 10;
    wordsOf(edited)->frame.right = 40;
    EXPECT_TRUE(anchor.reset(edited, anchor)) << "centred, its size kept";
    constexpr qreal k_tolerance = 1e-9;   // whole pixels, shared out
    EXPECT_NEAR(wordsOf(edited)->frame.left.value_or(0.0), 25.0, k_tolerance);
    EXPECT_NEAR(wordsOf(edited)->frame.right.value_or(0.0), 25.0, k_tolerance);
    EXPECT_FALSE(wordsOf(edited)->frame.top.has_value()) << "an axis with both edges automatic is centred already";
    EXPECT_FALSE(anchor.reset(edited, anchor));
    EXPECT_TRUE(shapeOf(words) == shapeOf(c));
    EXPECT_TRUE(skinOf(words) == skinOf(c));

    // A "+" adds its node when pressed, and becomes that node's handle for the drag that follows.
    Geometry::Handle plus    = first(Geometry::Handle::Glyph::Plus);
    Composition      pressed = c;
    ASSERT_TRUE(plus.press);
    EXPECT_TRUE(plus.press(pressed, plus));
    EXPECT_GT(shapeOf(pressed).nodes.size(), shapeOf(c).nodes.size());
    EXPECT_EQ(plus.glyph, Geometry::Handle::Glyph::Node);
    EXPECT_EQ(plus.node, plus.index);
    EXPECT_FALSE(plus.press);
}

TEST(Shapes, APickedCurveNodeOffersBothHandlesAndGrowsTheMissingOnes)
{
    const QRectF          body(5, 5, 270, 150);
    const ShapeProperties speech  = Geometry::shapeTemplate("speech").shape;
    const ShapeProperties curve   = Geometry::curveOf(Geometry::shapeInfo(speech.kind).outline(speech, body), body);
    const auto            outline = Geometry::shapeInfo(ShapeProperties::Kind::Curve).outline;
    constexpr qreal       k_samePlace = 1e-9;   //!< Two handles at one point, in balloon pixels

    const auto handleAt = [&](const ShapeProperties& s, int picked, Geometry::ShapePart part) {
        for (const Geometry::Handle& h : Geometry::shapeHandles(s, body, 0.0, picked))
            if (Geometry::partOf<Geometry::ShapePart>(h) == part && h.index == picked)
                return std::optional<QPointF>(h.at);
        return std::optional<QPointF>();
    };

    int straight = -1;   // a segment with no handle at either end: one of the box's sides
    for (int i = 0; i < curve.nodes.size(); ++i) {
        EXPECT_TRUE(handleAt(curve, i, Geometry::ShapePart::InHandle)) << "node " << i;
        EXPECT_TRUE(handleAt(curve, i, Geometry::ShapePart::OutHandle)) << "node " << i;
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
    for (const auto role : {Geometry::ShapePart::InHandle, Geometry::ShapePart::OutHandle}) {
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

TEST(Import, OnlyAFileCarryingTheRecipeIsAdopted)
{
    Composition c   = balloonObject();
    shapeOf(c).kind = ShapeProperties::Kind::Cloud;
    boxOf(c)        = QSize(300, 200);
    Tail t;
    t.tip = QPointF(40, 260);
    setTails(c, {t});
    textOf(c).body        = QStringLiteral("Hello");
    wordsOf(c)->frame.left  = 25;   // where the words sit: a set edge, saved and read back
    textOf(c).lineSpacing = 80;
    const QByteArray ours = svgWith(c);

    bool              ok   = false;
    const Composition back = Svg::read(ours, &ok);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(back == c);
    EXPECT_EQ(tailsOf(back).size(), 1);
    ASSERT_TRUE(wordsOf(back)->frame.left.has_value());
    EXPECT_NEAR(*wordsOf(back)->frame.left, 25.0, 1e-9);
    EXPECT_FALSE(wordsOf(back)->frame.right.has_value()) << "an automatic edge stays automatic";

    // The prefix is a local shorthand and carries no meaning: the same file with a different prefix,
    // bound to the same namespace, is still ours.
    QByteArray renamed = ours;
    renamed.replace("pm:", "x:");
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

    // The same content under somebody else's namespace is somebody else's.
    ok = true;
    (void)Svg::read(svgWith(c, "https://example.invalid/ns/1"), &ok);
    EXPECT_FALSE(ok);
}

/**
 * \brief A file of ours carries its content as the text of `<pm:content>`; a project's compositions come from its
 *        overlays' content, and from the file only for an overlay that has none. What is not a composition's
 *        content — not JSON, or an object with no words — is not ours.
 */
TEST(Import, TheContentIsReadFromTheOverlayThenFromItsFile)
{
    Composition cloud = balloonObject();
    shapeOf(cloud).kind = ShapeProperties::Kind::Cloud;
    boxOf(cloud)        = QSize(300, 200);
    textOf(cloud).body  = QStringLiteral("a < b");
    bool              ok   = false;
    const Composition back = Svg::read(svgWith(cloud), &ok);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(back == cloud);
    EXPECT_EQ(textOf(back).body, QStringLiteral("a < b"));

    const QByteArray header =
        QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:pm=\"") + Svg::k_pmNamespace + "\" width=\"10\" height=\"10\">";
    ok = true;
    (void)Svg::read(header + "<pm:content>not json</pm:content></svg>", &ok);
    EXPECT_FALSE(ok);
    ok = true;
    (void)Svg::read(header + "<pm:content>{\"box\":[300,200],\"shape\":{\"shape\":\"cloud\"}}</pm:content></svg>", &ok);
    EXPECT_FALSE(ok) << "an object with no words is no composition's";

    // The overlay's own content first; a file only for an overlay without one; neither, no composition.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("old.svg"));
    {
        Composition ellipse   = balloonObject();
        shapeOf(ellipse).kind = ShapeProperties::Kind::Ellipse;
        QFile f(file);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(svgWith(ellipse));
    }
    std::vector<Platemaker::Models::StripOverlay> overlays(3);
    overlays[0].uid       = "ovl-content";
    Composition named     = back;
    textOf(named).family  = QStringLiteral("Comic Neue");   // a family of its own: an empty one asks the system
    overlays[0].content   = Svg::contentOf(named).toStdString();
    overlays[0].assetPath = file.toStdString();   // a file that says otherwise is not read
    overlays[1].uid       = "ovl-file";
    overlays[1].assetPath = file.toStdString();
    overlays[2].uid       = "ovl-neither";
    const Composition::Map compositions = Svg::readOverlays(overlays);
    ASSERT_EQ(compositions.size(), 2);
    EXPECT_EQ(shapeOf(compositions.value(QStringLiteral("ovl-content"))).kind, ShapeProperties::Kind::Cloud);
    EXPECT_EQ(shapeOf(compositions.value(QStringLiteral("ovl-file"))).kind, ShapeProperties::Kind::Ellipse);
    EXPECT_FALSE(compositions.contains(QStringLiteral("ovl-neither")));
}

// ---------------------------------------------------------------------------
// The third kind
// ---------------------------------------------------------------------------


//! A picture survives being saved, like every other element — a picture that loaded as a balloon would be a
//! default balloon written over imported artwork. One beside a balloon is not read
//! (AContentThatBreaksTheRulesIsReadWithin).
TEST(Kinds, APictureSurvivesItsContent)
{
    Composition a  = pictureObject(QStringLiteral("art-0123456789abcdef.png"), QSize(200, 100));
    textOf(a).body = QStringLiteral("KRAK!");

    const Composition back = throughContent(a);
    EXPECT_TRUE(back == a);
    ASSERT_TRUE(back.picture);
    EXPECT_FALSE(back.balloon);
    EXPECT_EQ(pictureFileOf(back), pictureFileOf(a));
}

TEST(Import, AFileBakedInAStandInSaysWhich)
{
    // The content's family is the font asked for and stays so; pm:fontFallback is what the outlines were set in
    // while it was missing. The heal at open reads only the second — by namespace, like the content.
    Composition c    = balloonObject();
    textOf(c).body   = QStringLiteral("Hi");
    textOf(c).family = QStringLiteral("Segoe Print");
    const QByteArray standIn = svgWith(c, Svg::k_pmNamespace, " pm:fontFallback=\"Segoe UI\"");
    EXPECT_EQ(Svg::bakedFontFallback(standIn), QStringLiteral("Segoe UI"));
    EXPECT_EQ(textOf(Svg::read(standIn)).family, QStringLiteral("Segoe Print"));

    EXPECT_TRUE(Svg::bakedFontFallback(svgWith(c)).isEmpty());

    // Somebody else's namespace says nothing about our files.
    QByteArray impostor = standIn;
    impostor.replace(Svg::k_pmNamespace, "https://example.invalid/ns/1");
    EXPECT_TRUE(Svg::bakedFontFallback(impostor).isEmpty());
}

TEST(Import, AWrapperWithoutItsPictureIsNotWritten)
{
    Composition a  = pictureObject(QStringLiteral("art-0123456789abcdef.png"), QSize(200, 200));
    textOf(a).body = QStringLiteral("KRAK!");

    EXPECT_TRUE(Svg::write(a).isEmpty());                                   // no bytes at all
    EXPECT_TRUE(Svg::write(a, QByteArray("nonsense"), QString()).isEmpty()); // ...and no media type
}

//! A wrapper says which picture it wraps, so re-importing one brings the object back whole.
TEST(Import, AWrapperNamesItsPicture)
{
    Composition picture  = pictureObject(QStringLiteral("art-0123456789abcdef.png"), QSize(200, 200));
    textOf(picture).body = QStringLiteral("KRAK!");

    bool              ok = false;
    const Composition a  = Svg::read(svgWith(picture), &ok);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(a.picture);
    EXPECT_FALSE(a.balloon);
    EXPECT_EQ(pictureFileOf(a), QStringLiteral("art-0123456789abcdef.png"));
    EXPECT_EQ(textOf(a).body, QStringLiteral("KRAK!"));
}

// ---------------------------------------------------------------------------------------------------
// Which groups an object carries. The rule OBJECT STATE shows a subject by, and the rule its object menu
// offers a selection by — written out twice in the panel before it had a name, and the two copies had
// drifted.
// ---------------------------------------------------------------------------------------------------

using StripEdit::carriesGroup;
using StripEdit::PropertyGroup;

//! Every kind is lettered — the one group a picture and a balloon genuinely share.
TEST(Groups, TextIsCarriedByEveryKind)
{
    for (const Composition& a : {balloonObject(), textObject(), pictureObject(QStringLiteral("art-0123456789abcdef.png"))})
        EXPECT_TRUE(carriesGroup(a, PropertyGroup::Text));
}

//! Fill, line style, shape and tails all need a balloon to sit on.
TEST(Groups, TheRestNeedASilhouette)
{
    const Composition balloon = balloonObject();
    for (auto g : {PropertyGroup::Shape, PropertyGroup::Skin, PropertyGroup::Style, PropertyGroup::Tail})
        EXPECT_TRUE(carriesGroup(balloon, g)) << "balloon, group " << int(g);

    const Composition text = textObject();
    for (auto g : {PropertyGroup::Shape, PropertyGroup::Skin, PropertyGroup::Style, PropertyGroup::Tail})
        EXPECT_FALSE(carriesGroup(text, g)) << "text, group " << int(g);
}

//! A picture carries nothing of ours to shape, fill or roughen.
TEST(Groups, APictureCarriesOnlyItsLettering)
{
    const Composition picture = pictureObject(QStringLiteral("art-0123456789abcdef.png"));
    EXPECT_TRUE(carriesGroup(picture, PropertyGroup::Text));
    for (auto g : {PropertyGroup::Shape, PropertyGroup::Skin, PropertyGroup::Style, PropertyGroup::Tail})
        EXPECT_FALSE(carriesGroup(picture, g)) << "picture, group " << int(g);
}

//! A tail is not an object, so no object carries its group — nor the three that never grew an editor.
TEST(Groups, NoObjectCarriesATailsOwnGroup)
{
    const Composition a = loadedBalloon();
    ASSERT_FALSE(tailsOf(a).isEmpty());

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
void bump(Paint::Kind& v)
{
    v = static_cast<Paint::Kind>((int(v) + 1) % (int(Paint::k_lastKind) + 1));
}
void bump(ShapeProperties::Edge& v)
{
    v = static_cast<ShapeProperties::Edge>((int(v) + 1) % (int(ShapeProperties::k_lastEdge) + 1));
}
void bump(ShapeProperties::Kind& v)
{
    v = static_cast<ShapeProperties::Kind>((int(v) + 1) % (int(ShapeProperties::k_lastKind) + 1));
}
void bump(StyleProperties::Line& v)
{
    v = static_cast<StyleProperties::Line>((int(v) + 1) % (int(StyleProperties::k_lastLine) + 1));
}
void bump(TextureProperties::Kind& v)
{
    v = static_cast<TextureProperties::Kind>((int(v) + 1) % (int(TextureProperties::k_lastKind) + 1));
}

/**
 * \brief For every field of the group @p part gives: changing it alone makes the group unequal, and the
 *        changed object comes back the same from the JSON and from the SVG.
 *
 * The SVG is written with no lettering, whose outlines are a font's and differ from one machine to another;
 * the body's own `pm:text` is read by the import test.
 */
template <class Part>
void expectEveryFieldCounts(Part part)
{
    using Group = std::remove_reference_t<decltype(part(std::declval<Composition&>()))>;
    Group::fields([&](const char* key, auto member) {
        Composition a = loadedBalloon();
        textOf(a).body.clear();
        Composition b = a;
        bump(Fields::at(part(b), member));

        EXPECT_FALSE(part(a) == part(b)) << key << ": not compared";
        EXPECT_TRUE(compositionFromJson(toJson(b)) == b) << key << ": not in the JSON";
        if (textOf(b).body.isEmpty()) {   // writing words measures them, which needs a font
            bool ok = false;
            EXPECT_TRUE(Svg::read(Svg::write(b), &ok) == b) << key << ": not in the SVG";
            EXPECT_TRUE(ok) << key;
        }
    });
}

} // namespace

TEST(Fields, EveryFieldIsComparedAndSaved)
{
    expectEveryFieldCounts([](Composition& c) -> ShapeProperties& { return shapeOf(c); });
    expectEveryFieldCounts([](Composition& c) -> TextProperties& { return textOf(c); });
    expectEveryFieldCounts([](Composition& c) -> SkinProperties& { return skinOf(c); });
    expectEveryFieldCounts([](Composition& c) -> StyleProperties& { return styleOf(c); });
    expectEveryFieldCounts([](Composition& c) -> TextureProperties& { return textureOf(c); });
    expectEveryFieldCounts([](Composition& c) -> EffectProperties& { return effectsOf(c); });
}

//! Every key a group is saved under is its own: two fields sharing one would overwrite each other.
TEST(Fields, EveryKeyIsUnique)
{
    QStringList keys;
    const auto  collect = [&](const char* key, auto) { keys << QString::fromLatin1(key); };
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
    return QStringLiteral(PLATEMAKER_GOLDEN_DIR "/compositions.txt");
}

//! One golden case: what it is called in the file, and the object.
struct GoldenCase
{
    QString     title;
    Composition object;
};

/**
 * \brief Everything a composition is written as, for every template (and no shape) × look × with and without
 *        a tail. A case is named by its template's id and its look: clean, marker and ink (Rough at the
 *        Marker's and the Ink's numbers), flash, a rough flash, and brush.
 *
 * The balloon's SVG, its text area and its undo-snapshot JSON, one section per case. The lettering is
 * left empty and its family named, so nothing here measures a font: a font's outlines differ from one
 * machine to another, and the text area covers what the lettering is fitted to.
 */
QList<GoldenCase> goldenCases()
{
    ShapeProperties none = shapeOf(loadedBalloon());
    none.kind            = ShapeProperties::Kind::None;
    QList<QPair<QString, ShapeProperties>> shapes{{QStringLiteral("none"), none}};
    for (const Geometry::ShapeTemplate& t : Geometry::shapeTemplates())
        shapes.append({QString::fromLatin1(t.id), t.shape});
    // The other edges on the speech balloon, each with some irregularity.
    for (int e = int(ShapeProperties::Edge::Wave); e <= int(ShapeProperties::k_lastEdge); ++e) {
        ShapeProperties edged  = Geometry::shapeTemplate("speech").shape;
        edged.edge             = static_cast<ShapeProperties::Edge>(e);
        edged.teeth.pitch        = 24.0;
        edged.teeth.depth        = 8.0;
        edged.teeth.irregularity = 30;
        shapes.append({QStringLiteral("speech ") + QLatin1String(edgeName(edged.edge)), edged});
    }
    // A zigzag edge on one template of each kind that draws a line or a curve.
    for (const char* id : {"speech", "ellipse", "shout", "scroll"}) {
        ShapeProperties zigzag = Geometry::shapeTemplate(id).shape;
        zigzag.edge            = ShapeProperties::Edge::Zigzag;
        shapes.append({QString::fromLatin1(id) + QStringLiteral(" zigzag"), zigzag});
    }

    // The looks: a line over the loaded composition's flash and brush numbers, and a texture, Rough at a
    // look's own numbers (the Marker's are the struct's defaults).
    struct Look
    {
        QString           name;
        StyleProperties   style;
        TextureProperties texture;
    };
    StyleProperties clean = styleOf(loadedBalloon());
    clean.line            = StyleProperties::Line::Clean;
    clean.stroke.kind     = Paint::Kind::Solid;   // the patterns have cases of their own, below
    StyleProperties flash = clean;
    flash.line            = StyleProperties::Line::Flash;
    StyleProperties brush = clean;
    brush.line            = StyleProperties::Line::Brush;
    TextureProperties untextured = textureOf(loadedBalloon());   // its Rough numbers carried, unused
    untextured.kind              = TextureProperties::Kind::None;
    TextureProperties marker;
    marker.kind = TextureProperties::Kind::Rough;
    const QList<Look> looks{
        {QStringLiteral("clean"), clean, untextured},
        {QStringLiteral("marker"), clean, marker},
        {QStringLiteral("ink"), clean, Geometry::inkRough(untextured)},
        {QStringLiteral("flash"), flash, untextured},
        {QStringLiteral("rough flash"), flash, marker},
        {QStringLiteral("brush"), brush, untextured},
    };

    QList<GoldenCase> cases;
    const auto write = [&cases](const QString& name, const QString& look, bool tail, Composition a) {
        textOf(a).body.clear();
        originOf(a) = {};   // written only when there is one; Presets.TheOriginTravelsWithTheObject has it
        if (!tail && a.balloon)
            tailsOf(a).clear();
        cases.append({QStringLiteral("%1 / %2 / %3")
                           .arg(name, look, tail ? QStringLiteral("tail") : QStringLiteral("no tail")),
                       a});
    };
    for (const auto& [name, shape] : std::as_const(shapes)) {
        for (const Look& look : looks) {
            for (bool tail : {false, true}) {
                Composition a  = loadedBalloon();
                styleOf(a)         = look.style;
                textureOf(a)       = look.texture;
                skinOf(a).fill.kind = Paint::Kind::Solid;   // the fills and the shadow have cases of
                effectsOf(a).shadow.on = false;                       // their own, below
                if (shape.kind == ShapeProperties::Kind::None)
                    a = withoutBalloon(a);   // the text object: its words, and nothing of a balloon
                else
                    shapeOf(a) = shape;
                write(name, look.name, tail, a);
            }
        }
    }
    // A dry brush on two templates, a white line on a black balloon with a pointed tail, at the dry
    // brush's own numbers.
    for (const char* id : {"speech", "shout"}) {
        Composition a        = loadedBalloon();
        shapeOf(a)               = Geometry::shapeTemplate(id).shape;
        styleOf(a)               = looks.first().style;
        styleOf(a).line          = StyleProperties::Line::Drybrush;
        styleOf(a).stroke.colour = Qt::white;
        styleOf(a).strokeWidth   = 14;
        styleOf(a).drybrush.core     = StyleProperties().drybrush.core;
        styleOf(a).drybrush.bristles = StyleProperties().drybrush.bristles;
        styleOf(a).drybrush.dryness  = StyleProperties().drybrush.dryness;
        textureOf(a)             = looks.first().texture;
        skinOf(a).fill.kind      = Paint::Kind::Solid;
        skinOf(a).fill.colour    = Qt::black;
        effectsOf(a).shadow.on      = false;
        tailsOf(a).first().tail.tip = QPointF(90, 230);
        write(QString::fromLatin1(id), QStringLiteral("drybrush"), true, a);
    }
    // A dashed line, a whisper, on two templates with a tail, at its own numbers.
    for (const char* id : {"ellipse", "caption"}) {
        Composition a      = loadedBalloon();
        shapeOf(a)             = Geometry::shapeTemplate(id).shape;
        styleOf(a)             = looks.first().style;
        styleOf(a).line        = StyleProperties::Line::Dashed;
        styleOf(a).dash.length  = StyleProperties().dash.length;
        styleOf(a).dash.gap     = StyleProperties().dash.gap;
        textureOf(a)           = looks.first().texture;
        skinOf(a).fill.kind    = Paint::Kind::Solid;
        effectsOf(a).shadow.on = false;
        write(QString::fromLatin1(id), QStringLiteral("dashed"), true, a);
    }
    // A multi-line, three lines, on the speech balloon with its tail, at its other numbers' defaults.
    {
        Composition a      = loadedBalloon();
        shapeOf(a)             = Geometry::shapeTemplate("speech").shape;
        styleOf(a)             = looks.first().style;
        styleOf(a).line        = StyleProperties::Line::Multi;
        styleOf(a).multi.lines  = 3;
        styleOf(a).multi.gap   = StyleProperties().multi.gap;
        styleOf(a).multi.width = StyleProperties().multi.width;
        textureOf(a)           = looks.first().texture;
        skinOf(a).fill.kind    = Paint::Kind::Solid;
        effectsOf(a).shadow.on = false;
        write(QStringLiteral("speech"), QStringLiteral("multi"), true, a);
    }
    // A sketch, on two templates with a tail, at its own numbers and a pen's width.
    for (const char* id : {"ellipse", "shout"}) {
        Composition a          = loadedBalloon();
        shapeOf(a)                 = Geometry::shapeTemplate(id).shape;
        styleOf(a)                 = looks.first().style;
        styleOf(a).line            = StyleProperties::Line::Sketch;
        styleOf(a).strokeWidth     = 3;
        styleOf(a).sketch.length    = StyleProperties().sketch.length;
        styleOf(a).sketch.layers    = StyleProperties().sketch.layers;
        styleOf(a).sketch.stiffness = StyleProperties().sketch.stiffness;
        styleOf(a).sketch.spread    = StyleProperties().sketch.spread;
        styleOf(a).sketch.entry     = StyleProperties().sketch.entry;
        styleOf(a).sketch.exit      = StyleProperties().sketch.exit;
        styleOf(a).sketch.core        = StyleProperties().sketch.core;
        styleOf(a).sketch.strokeWidth = StyleProperties().sketch.strokeWidth;
        styleOf(a).sketch.clockwise   = StyleProperties().sketch.clockwise;
        styleOf(a).sketch.blots       = StyleProperties().sketch.blots;
        textureOf(a)               = looks.first().texture;
        skinOf(a).fill.kind        = Paint::Kind::Solid;
        effectsOf(a).shadow.on     = false;
        write(QString::fromLatin1(id), QStringLiteral("sketch"), true, a);
    }
    // The burst with concave edges, as the author's reference: a rough rectangle of points, long and short,
    // joined by arcs bowing in, of uneven depth, in a clean even line.
    {
        Composition a          = loadedBalloon();
        shapeOf(a)                 = Geometry::shapeTemplate("shout").shape;
        shapeOf(a).nodes           = {ShapeNode(0, 0.1),  ShapeNode(0.3, 0),    ShapeNode(0.55, 0.08), ShapeNode(1, 0),
                                   ShapeNode(0.95, 0.5), ShapeNode(1, 1),   ShapeNode(0.6, 0.92), ShapeNode(0.25, 1),
                                   ShapeNode(0, 0.85), ShapeNode(0.07, 0.45)};
        shapeOf(a).corner.radius    = 0.0;
        shapeOf(a).bow.depth             = 15;
        shapeOf(a).bow.irregularity = 60;
        styleOf(a)                 = looks.first().style;
        styleOf(a).strokeWidth     = 4;
        textureOf(a)               = looks.first().texture;
        skinOf(a).fill.kind        = Paint::Kind::Solid;
        effectsOf(a).shadow.on     = false;
        write(QStringLiteral("shout"), QStringLiteral("bowed"), true, a);
    }
    // The spilling ink, as the author's reference: long loose strokes side by side, and blots.
    {
        Composition a            = loadedBalloon();
        shapeOf(a)                   = Geometry::shapeTemplate("ellipse").shape;
        styleOf(a)                   = looks.first().style;
        styleOf(a).line              = StyleProperties::Line::Sketch;
        styleOf(a).strokeWidth       = 3;
        styleOf(a).sketch.length      = 300.0;
        styleOf(a).sketch.layers      = 3;
        styleOf(a).sketch.stiffness   = 0;
        styleOf(a).sketch.spread      = 7.0;
        styleOf(a).sketch.entry       = 20;
        styleOf(a).sketch.exit        = 60;
        styleOf(a).sketch.core        = 0;
        styleOf(a).sketch.strokeWidth = 100;
        styleOf(a).sketch.clockwise   = true;
        styleOf(a).sketch.blots       = 60;
        textureOf(a)                 = looks.first().texture;
        skinOf(a).fill.kind          = Paint::Kind::Solid;
        effectsOf(a).shadow.on       = false;
        write(QStringLiteral("ellipse"), QStringLiteral("spilling ink"), true, a);
    }
    // An explosion, as the author's reference: a white dry-brush ring on a black ellipse, in a dense halo of
    // fine spikes, outward only, at the lines' own numbers.
    {
        Composition a                 = loadedBalloon();
        shapeOf(a)                        = Geometry::shapeTemplate("ellipse").shape;
        styleOf(a)                        = looks.first().style;
        styleOf(a).line                   = StyleProperties::Line::Explosion;
        styleOf(a).stroke.colour          = Qt::white;
        styleOf(a).strokeWidth            = 10;
        styleOf(a).drybrush.core           = StyleProperties().drybrush.core;
        styleOf(a).drybrush.bristles       = StyleProperties().drybrush.bristles;
        styleOf(a).drybrush.dryness        = StyleProperties().drybrush.dryness;
        styleOf(a).flash.outerPitch        = StyleProperties().flash.outerPitch;
        styleOf(a).flash.outerLength       = StyleProperties().flash.outerLength;
        styleOf(a).flash.outerBase         = StyleProperties().flash.outerBase;
        styleOf(a).flash.outerPitchJitter  = StyleProperties().flash.outerPitchJitter;
        styleOf(a).flash.outerLengthJitter = StyleProperties().flash.outerLengthJitter;
        styleOf(a).flash.coreStretch       = StyleProperties().flash.coreStretch;
        textureOf(a)                      = looks.first().texture;
        skinOf(a).fill.kind               = Paint::Kind::Solid;
        skinOf(a).fill.colour             = Qt::black;
        effectsOf(a).shadow.on            = false;
        write(QStringLiteral("ellipse"), QStringLiteral("explosion"), false, a);
    }
    // A Circles tail: on a clean speech balloon, apart from it; and the author's reference, a
    // white dry brush on a black ellipse, the chain sunk into it.
    for (bool reference : {false, true}) {
        Composition a   = loadedBalloon();
        shapeOf(a)          = Geometry::shapeTemplate(reference ? "ellipse" : "speech").shape;
        styleOf(a)          = looks.first().style;
        textureOf(a)        = looks.first().texture;
        skinOf(a).fill.kind = Paint::Kind::Solid;
        effectsOf(a).shadow.on = false;
        Tail& t          = tailsOf(a).first().tail;
        t.tip                 = QPointF(250, 230);
        t.bend                = 0.2;
        t.look.style          = TailLook::Style::Circles;
        t.look.circles.count  = 3;
        t.look.circles.pitch  = 5.0;
        t.look.circles.offset = reference ? 6.0 : -6.0;
        if (reference) {
            styleOf(a).line             = StyleProperties::Line::Drybrush;
            styleOf(a).stroke.colour    = Qt::white;
            styleOf(a).strokeWidth      = 10;
            styleOf(a).drybrush.core     = StyleProperties().drybrush.core;
            styleOf(a).drybrush.bristles = StyleProperties().drybrush.bristles;
            styleOf(a).drybrush.dryness  = StyleProperties().drybrush.dryness;
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
            Composition a      = loadedBalloon();
            shapeOf(a)             = Geometry::shapeTemplate("speech").shape;
            styleOf(a)             = StyleProperties();
            styleOf(a).line        = rough ? StyleProperties::Line::Clean : static_cast<StyleProperties::Line>(l);
            styleOf(a).strokeWidth = 14;
            textureOf(a)           = rough ? marker : looks.first().texture;
            skinOf(a).fill.kind    = Paint::Kind::Solid;
            effectsOf(a).shadow.on = false;
            Tail& tail          = tailsOf(a).first().tail;
            tail.tip            = QPointF(220, 260);
            tail.baseWidth      = 60.0;
            tail.bend           = 0.15;
            tail.look.tip       = tip;
            tail.look.end.width = 15;
            write(QStringLiteral("speech"),
                  QStringLiteral("tip %1, %2")
                      .arg(tip == TailLook::Tip::Round ? QStringLiteral("round") : QStringLiteral("cut"),
                           rough ? QStringLiteral("rough") : QString::fromLatin1(lineName(styleOf(a).line))),
                  true, a);
        }
    }
    // A broken tail, two breaks, with each tip, under the lines that treat a turn their own way.
    for (TailLook::Tip tip : {TailLook::Tip::Sharp, TailLook::Tip::Round, TailLook::Tip::Cut}) {
        for (StyleProperties::Line line : {StyleProperties::Line::Clean, StyleProperties::Line::Drybrush,
                                        StyleProperties::Line::Sketch, StyleProperties::Line::Multi,
                                        StyleProperties::Line::Flash}) {
            Composition a      = loadedBalloon();
            shapeOf(a)             = Geometry::shapeTemplate("speech").shape;
            styleOf(a)             = StyleProperties();
            styleOf(a).line        = line;
            styleOf(a).strokeWidth = 8;
            textureOf(a)           = looks.first().texture;
            skinOf(a).fill.kind    = Paint::Kind::Solid;
            effectsOf(a).shadow.on = false;
            Tail& tail          = tailsOf(a).first().tail;
            tail.tip                   = QPointF(240, 300);
            tail.baseWidth             = 50.0;
            tail.bend                  = 0.1;
            tail.look.tip              = tip;
            tail.look.end.width        = 30;
            tail.look.breaks.count     = 2;
            tail.look.breaks.jog       = 15;
            tail.look.breaks.clockwise = true;
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
        Composition a      = loadedBalloon();
        shapeOf(a)             = Geometry::shapeTemplate("speech").shape;
        styleOf(a)             = StyleProperties();
        styleOf(a).strokeWidth = 8;
        textureOf(a)           = looks.first().texture;
        skinOf(a).fill.kind    = Paint::Kind::Solid;
        effectsOf(a).shadow.on = false;
        Tail& tail          = tailsOf(a).first().tail;
        tail.tip                   = QPointF(240, 300);
        tail.baseWidth             = 50.0;
        tail.bend                  = 0.1;
        tail.look.breaks.count     = 2;
        tail.look.breaks.jog       = 15;
        tail.look.breaks.clockwise = false;
        write(QStringLiteral("speech"), QStringLiteral("broken counter-clockwise, clean"), true, a);
    }
    // A screentone fill on two templates, clean.
    for (const char* id : {"speech", "shout"}) {
        for (bool tail : {false, true}) {
            Composition a = loadedBalloon();
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
            Composition a           = loadedBalloon();
            shapeOf(a)                  = Geometry::shapeTemplate(id).shape;
            styleOf(a)                  = looks.first().style;
            textureOf(a)                = looks.first().texture;
            skinOf(a).fill.kind          = Paint::Kind::Solid;
            effectsOf(a).shadow.softness  = softness;
            effectsOf(a).shadowFill.kind = Paint::Kind::Solid;
            write(QString::fromLatin1(id), softness > 0.0 ? QStringLiteral("soft shadow") : QStringLiteral("shadow"),
                  true, a);
        }
    }
    // A centred shadow, hard and soft: the default spread and no offset.
    for (qreal softness : {0.0, 2.5}) {
        Composition a            = loadedBalloon();
        shapeOf(a)                   = Geometry::shapeTemplate("speech").shape;
        styleOf(a)                   = looks.first().style;
        textureOf(a)                 = looks.first().texture;
        skinOf(a).fill.kind          = Paint::Kind::Solid;
        skinOf(a).fill.colour        = Qt::white;
        styleOf(a).strokeWidth       = StyleProperties().strokeWidth;   // a thick line would hide the rim
        effectsOf(a).shadow.spread    = EffectProperties().shadow.spread;
        effectsOf(a).shadow.x         = 0.0;
        effectsOf(a).shadow.y         = 0.0;
        effectsOf(a).shadow.softness  = softness;
        effectsOf(a).shadowFill.kind = Paint::Kind::Solid;
        write(QStringLiteral("speech"), softness > 0.0 ? QStringLiteral("soft centred shadow")
                                                       : QStringLiteral("centred shadow"), true, a);
    }
    // Every pattern in every place it paints: the fill, a thick line, and a shadow.
    for (int k = 1; k <= int(Paint::k_lastKind); ++k) {
        const auto pattern = static_cast<Paint::Kind>(k);
        Composition a     = loadedBalloon();
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
        const auto   pattern = static_cast<Paint::Kind>(k);
        Composition a       = loadedBalloon();
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
        if (pattern != Paint::Kind::Dots) {
            p.fadeTopFrom = 30.0;
            p.fadeTopTo   = 90.0;
        }
        if (pattern == Paint::Kind::Grid) {
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
        if (pattern == Paint::Kind::Grid) {
            p.fadeRound = true;
            write(QStringLiteral("speech"), QStringLiteral("fading grid, reversed, round"), true, a);
        }
    }
    // Rough in parts: the fill not displaced, masked by the displaced silhouette; a pattern and a fade.
    for (const bool fading : {false, true}) {
        Composition a       = loadedBalloon();
        shapeOf(a)              = Geometry::shapeTemplate("speech").shape;
        textureOf(a).rough.fill  = false;
        skinOf(a).fill.kind     = Paint::Kind::Dots;
        skinOf(a).fill.fadeLeftFrom = fading ? 40.0 : skinOf(a).fill.fadeLeftFrom;
        write(QStringLiteral("speech"), fading ? QStringLiteral("rough in parts, fading dots")
                                               : QStringLiteral("rough in parts, dots"), true, a);
    }
    // Dry ink: the outline eaten into but for its middle, the fill and the shadow whole; and under Rough, in
    // parts, which roughens it as a whole.
    for (const bool rough : {false, true}) {
        Composition a        = loadedBalloon();
        shapeOf(a)               = Geometry::shapeTemplate("speech").shape;
        styleOf(a).line          = StyleProperties::Line::DryInk;
        styleOf(a).stroke.kind   = Paint::Kind::Solid;
        styleOf(a).strokeWidth   = 9;
        styleOf(a).dryInk.grain   = StyleProperties().dryInk.grain;
        styleOf(a).dryInk.wear    = StyleProperties().dryInk.wear;
        styleOf(a).dryInk.core    = StyleProperties().dryInk.core;
        textureOf(a).kind        = rough ? TextureProperties::Kind::Rough : TextureProperties::Kind::None;
        textureOf(a).rough.fill   = false;
        effectsOf(a).shadow.on   = !rough;   // a dark shadow would show through the holes and hide them
        skinOf(a).fill.kind      = Paint::Kind::Solid;
        write(QStringLiteral("speech"), rough ? QStringLiteral("dry ink, rough") : QStringLiteral("dry ink"), true, a);
    }
    // A halftone shadow, hard: the pattern's marks on nothing (the cases above fade, soft).
    {
        Composition a            = loadedBalloon();
        shapeOf(a)                   = Geometry::shapeTemplate("speech").shape;
        styleOf(a)                   = looks.first().style;
        textureOf(a)                 = looks.first().texture;
        skinOf(a).fill.kind          = Paint::Kind::Solid;
        effectsOf(a).shadow.softness  = 0.0;
        effectsOf(a).shadowFill.kind = Paint::Kind::Dots;
        write(QStringLiteral("speech"), QStringLiteral("hard halftone shadow"), true, a);
    }
    // Two speech balloons joined by a bridge, the first's tail made it: apart, and overlapping (the bridge only
    // crosses the overlap), under the lines that walk the joined outline their own way; and broken.
    const QPointF apart(380, 60);
    const QPointF overlapping(240, 70);
    const auto chained = [&](const QPointF& by, StyleProperties::Line line, int breaks) {
        Composition a          = loadedBalloon();
        shapeOf(a)             = Geometry::shapeTemplate("speech").shape;
        styleOf(a)             = StyleProperties();
        styleOf(a).line        = line;
        styleOf(a).strokeWidth = 8;
        textureOf(a)           = looks.first().texture;
        skinOf(a).fill.kind    = Paint::Kind::Solid;
        effectsOf(a).shadow.on = false;
        Tail& tail             = tailsOf(a).first().tail;
        tail.baseWidth         = 40.0;
        tail.bend              = 0.2;
        tail.look.breaks.count = breaks;
        tail.look.breaks.jog   = 15;
        Composition other      = a;
        other.balloon->tails.clear();
        QList<int> merged;
        EXPECT_EQ(Chain::merge(a, other, by, &merged), Chain::ChainError::None);
        EXPECT_EQ(Chain::makeBridge(a, a.balloon->tails.first().id, merged.first()), Chain::ChainError::None);
        for (Words& w : a.words)
            w.text.body.clear();
        return a;
    };
    for (StyleProperties::Line line : {StyleProperties::Line::Clean, StyleProperties::Line::Drybrush,
                                    StyleProperties::Line::Multi, StyleProperties::Line::Flash}) {
        write(QStringLiteral("speech chain"), QStringLiteral("apart, %1").arg(QLatin1String(lineName(line))), true,
              chained(apart, line, 0));
        write(QStringLiteral("speech chain"), QStringLiteral("overlapping, %1").arg(QLatin1String(lineName(line))),
              true, chained(overlapping, line, 0));
    }
    for (StyleProperties::Line line : {StyleProperties::Line::Clean, StyleProperties::Line::Flash})
        write(QStringLiteral("speech chain"), QStringLiteral("apart broken, %1").arg(QLatin1String(lineName(line))),
              true, chained(apart, line, 2));
    // Anchors: a tail leaving straight down near the left end of a wide box, and a bridge between the two bodies'
    // lower parts.
    {
        Composition a     = loadedBalloon();
        shapeOf(a)        = Geometry::shapeTemplate("speech").shape;
        boxOf(a)          = QSize(420, 120);
        Tail& tail        = tailsOf(a).first().tail;
        tail.anchor       = QPointF(0.15, 0.5);
        tail.tip          = QPointF(63, 220);
        write(QStringLiteral("speech"), QStringLiteral("anchored tail, wide"), true, a);
    }
    {
        Composition  a      = chained(apart, StyleProperties::Line::Clean, 0);
        BalloonTail& bridge = a.balloon->tails.first();
        bridge.tail.anchor  = QPointF(0.5, 0.85);
        bridge.toAnchor     = QPointF(0.3, 0.85);
        write(QStringLiteral("speech chain"), QStringLiteral("anchored, clean"), true, a);
    }
    return cases;
}

QString goldenCompositions()
{
    QString out;
    for (const GoldenCase& c : goldenCases()) {
        const QRectF area = Geometry::textArea(c.object, *wordsOf(c.object));
        out += QStringLiteral("=== %1 ===\n").arg(c.title);
        out += QString::fromUtf8(Svg::write(c.object));
        out += QStringLiteral("\ntextArea: %1 %2 %3 %4\n")
                   .arg(area.x(), 0, 'f', 3).arg(area.y(), 0, 'f', 3)
                   .arg(area.width(), 0, 'f', 3).arg(area.height(), 0, 'f', 3);
        out += QStringLiteral("json: ")
             + QString::fromUtf8(QJsonDocument(toJson(c.object)).toJson(QJsonDocument::Compact))
             + u'\n';
    }
    return out;
}

} // namespace

/**
 * \brief What a composition is written as does not change unless it is meant to — **byte for byte**.
 *
 * The guard for refactoring the geometry and the persistence: moving a shape's path, a style's
 * filter or a property's key to another file must leave every file written exactly as it was. A change
 * that is meant is accepted by regenerating the golden file and reviewing its diff: run the tests with
 * `PLATEMAKER_UPDATE_GOLDEN=1` in the environment.
 */
TEST(Golden, EveryShapeAndStyleIsWrittenAsBefore)
{
    const QString actual = goldenCompositions();
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

//! Every golden case comes back as itself from its content and from its SVG: what is saved is all there is.
TEST(Composition, EveryGoldenCaseComesBackFromWhatIsSaved)
{
    for (const GoldenCase& g : goldenCases()) {
        SCOPED_TRACE(g.title.toStdString());
        EXPECT_TRUE(compositionFromJson(toJson(g.object)) == g.object);
        bool ok = false;
        EXPECT_TRUE(Svg::read(Svg::write(g.object), &ok) == g.object);
        EXPECT_TRUE(ok);
    }
}

// ---------------------------------------------------------------------------
// An object's own mask: what it hides, how it moves, how it is written.
// ---------------------------------------------------------------------------

namespace {

//! A balloon of the default size with a box mask over the middle of its right half.
Composition maskedBalloon()
{
    Composition a = balloonObject();
    tailsOf(a).clear();
    Mask m;
    m.region.id    = a.nextId++;
    m.region.place = Place{QPointF(160.0, 40.0)};
    m.region.size  = QSize(80, 80);
    m.region.shape.corner.radius = 0.0;   // a plain rectangle, so a point's coverage is the fade's alone
    a.mask         = m;
    return a;
}

//! @p img's alpha at @p at, a point in the composition's frame of an image Painter::render() drew of @p c.
int alphaAt(const QImage& img, const Composition& c, const QPointF& at)
{
    const QPoint px = (at - Geometry::bounds(c).topLeft()).toPoint();
    return qAlpha(img.pixel(px));
}

}  // namespace

/**
 * \brief A mask hides what the object draws inside its region and nothing outside it; with a fade, less and less
 *        along the fade's line, as the SVG's gradient does.
 */
TEST(Mask, HidesWhatItCoversAndFadesAlongItsLine)
{
    constexpr int k_alphaTolerance = 4;   // of 255: antialiasing and the gradient's rounding
    Composition   plain = maskedBalloon();
    plain.mask.reset();
    const QImage  whole = Painter::render(plain);

    Composition  a   = maskedBalloon();
    const QImage hard = Painter::render(a);
    const QPointF inside(200.0, 80.0), outside(80.0, 80.0);
    ASSERT_EQ(alphaAt(whole, plain, inside), 255);   // the fill is opaque there
    EXPECT_EQ(alphaAt(hard, a, inside), 0);
    EXPECT_EQ(alphaAt(hard, a, outside), alphaAt(whole, plain, outside));

    // Kept inside: what the region covers shows, the rest of the object goes.
    a.mask->keepInside = true;
    const QImage kept  = Painter::render(a);
    EXPECT_EQ(alphaAt(kept, a, inside), 255);
    EXPECT_EQ(alphaAt(kept, a, outside), 0);
    a.mask->keepInside = false;

    // Left to right across the region: hidden wholly at its left edge, not at all at its right.
    a.mask->fade = QLineF(0.0, 0.5, 1.0, 0.5);
    const QImage faded = Painter::render(a);
    for (const qreal along : {0.25, 0.5, 0.75}) {
        SCOPED_TRACE(along);
        const QPointF at(160.0 + 80.0 * along, 80.0);
        EXPECT_NEAR(alphaAt(faded, a, at), 255.0 * along, k_alphaTolerance);
    }

    // Through a pattern, what is left breaks into its marks: hidden at the fade's start, whole at its end, and on
    // the way, over a whole number of cells, about as much left as the smooth fade leaves.
    constexpr int   k_spacing      = 8;
    constexpr qreal k_meanTolerance = 0.1;   // of the whole: a halftone's area against its tone, sampled
    for (const Paint::Kind pattern : {Paint::Kind::Dots, Paint::Kind::Lines, Paint::Kind::Grid}) {
        SCOPED_TRACE(int(pattern));
        a.mask->transition = pattern;
        a.mask->spacing    = k_spacing;
        a.mask->angle      = 0;
        const QImage marked = Painter::render(a);
        EXPECT_EQ(alphaAt(marked, a, QPointF(160.5, 80.0)), 0);
        EXPECT_EQ(alphaAt(marked, a, QPointF(239.5, 80.0)), 255);
        // The middle half of the fade: a mean tone of a half.
        qreal sum = 0.0;
        int   n   = 0;
        for (int x = 180; x < 220; ++x)
            for (int y = 64; y < 96; ++y, ++n)
                sum += alphaAt(marked, a, QPointF(x + 0.5, y + 0.5)) / 255.0;
        EXPECT_NEAR(sum / n, 0.5, k_meanTolerance);
    }
}

/**
 * \brief A click picks the mask where it hides its object, as the eye sees it: half hidden or more, and something
 *        drawn there; not past the fade's half way, not outside the region, not where the object draws nothing.
 */
TEST(Mask, IsPickedWhereItHides)
{
    Composition a = maskedBalloon();
    EXPECT_TRUE(Geometry::maskHidesAt(a, QPointF(200.0, 80.0)));
    EXPECT_FALSE(Geometry::maskHidesAt(a, QPointF(80.0, 80.0))) << "outside the region";

    a.mask->fade = QLineF(0.0, 0.5, 1.0, 0.5);   // hidden at the region's left, shown at its right
    EXPECT_TRUE(Geometry::maskHidesAt(a, QPointF(170.0, 80.0)));
    EXPECT_FALSE(Geometry::maskHidesAt(a, QPointF(230.0, 80.0))) << "mostly shown there";

    a.mask->fade.reset();
    a.mask->region.place = Place{QPointF(-100.0, -100.0)};   // over nothing the balloon draws
    a.mask->region.size  = QSize(50, 50);
    EXPECT_FALSE(Geometry::maskHidesAt(a, QPointF(-75.0, -75.0)));
    // Kept inside, the other way round: outside the region it hides, inside it does not.
    a.mask->region.place = Place{QPointF(160.0, 40.0)};
    a.mask->region.size  = QSize(80, 80);
    a.mask->keepInside   = true;
    EXPECT_FALSE(Geometry::maskHidesAt(a, QPointF(200.0, 80.0)));
    EXPECT_TRUE(Geometry::maskHidesAt(a, QPointF(80.0, 80.0)));
    a.mask.reset();
    EXPECT_FALSE(Geometry::maskHidesAt(a, QPointF(200.0, 80.0)));
}

//! \brief A mask keeps its place on the object as the object is resized: scaled from the box's corner with it.
TEST(Mask, ScalesWithTheObjectsBox)
{
    Composition   a      = maskedBalloon();
    const QPointF corner = bodyOf(a)->place.pos;
    resize(a, QSize(560, 80));   // twice as wide, half as tall
    EXPECT_EQ(a.mask->region.place.pos, corner + QPointF((160.0 - corner.x()) * 2.0, (40.0 - corner.y()) * 0.5));
    EXPECT_EQ(a.mask->region.size, QSize(160, 40));
}

/**
 * \brief The mask picked is edited by its own handles alone, and the Shape section reaches its region: its outline's
 *        handles change its shape and never the balloon's, its fade's two ends move the fade, a "+" adds a node.
 */
TEST(Mask, IsEditedByItsOwnHandlesAndTheShapeSection)
{
    Composition a  = maskedBalloon();
    a.mask->fade   = QLineF(0.0, 0.5, 1.0, 0.5);
    const int   id = a.mask->region.id;
    Geometry::HandleContext context;
    context.body = id;
    const QList<Geometry::Handle> all = Geometry::handlesOf(a, context);
    ASSERT_FALSE(all.isEmpty());
    for (const Geometry::Handle& h : all) {
        EXPECT_EQ(h.element, id);
        EXPECT_TRUE(h.drag);
    }

    const auto glyphed = [&](Geometry::Handle::Glyph glyph) {
        return std::find_if(all.cbegin(), all.cend(), [&](const Geometry::Handle& h) { return h.glyph == glyph; });
    };
    const auto end = glyphed(Geometry::Handle::Glyph::FadeEnd);
    ASSERT_NE(end, all.cend());
    const QRectF region(a.mask->region.place.pos, QSizeF(a.mask->region.size));
    EXPECT_EQ(end->at, QPointF(region.right(), region.center().y()));
    Composition faded = a;
    end->drag(faded, *end, region.center(), context);
    constexpr qreal k_fraction = 1e-9;
    EXPECT_NEAR(faded.mask->fade->x2(), 0.5, k_fraction);
    EXPECT_NEAR(faded.mask->fade->x1(), 0.0, k_fraction) << "the start stays";

    const auto radius = std::find_if(all.cbegin(), all.cend(), [](const Geometry::Handle& h) {
        return h.part == int(Geometry::ShapePart::Radius) && h.glyph != Geometry::Handle::Glyph::FadeStart
            && h.glyph != Geometry::Handle::Glyph::FadeEnd;
    });
    ASSERT_NE(radius, all.cend());
    Composition rounded = a;
    radius->drag(rounded, *radius, radius->at + QPointF(20.0, 0.0), context);
    EXPECT_FALSE(rounded.mask->region.shape == a.mask->region.shape);
    EXPECT_TRUE(shapeOf(rounded) == shapeOf(a)) << "the balloon's own shape is not what was edited";

    ShapeProperties ellipse = Geometry::shapeTemplate("ellipse").shape;
    ellipse.applyTo(a, id);
    EXPECT_TRUE(a.mask->region.shape == ellipse);
    EXPECT_TRUE(ShapeProperties::from(a, id) == ellipse);
    EXPECT_FALSE(shapeOf(a) == ellipse);

    // A polygon's "+" adds its node to the region.
    a.mask->region.shape = Geometry::shapeTemplate("diamond").shape;
    QList<Geometry::Handle> polygon = Geometry::handlesOf(a, context);
    const auto plus = std::find_if(polygon.begin(), polygon.end(),
                                   [](const Geometry::Handle& h) { return h.glyph == Geometry::Handle::Glyph::Plus; });
    ASSERT_NE(plus, polygon.end());
    ASSERT_TRUE(plus->press);
    const qsizetype nodes = a.mask->region.shape.nodes.size();
    EXPECT_TRUE(plus->press(a, *plus));
    EXPECT_EQ(a.mask->region.shape.nodes.size(), nodes + 1);
}

/**
 * \brief A mask is written as the SVG's: the content group masked, the region black in a white mask, a fade a
 *        gradient, a feather a blur; and it reads back. Only a feather makes the object one only the library draws.
 */
TEST(Mask, IsWrittenAsTheSvgsMask)
{
    Composition a = maskedBalloon();
    QByteArray  svg = Svg::write(a);
    EXPECT_TRUE(svg.contains("<g mask=\"url(#pm-mask)\">"));
    EXPECT_TRUE(svg.contains("<mask id=\"pm-mask\""));
    EXPECT_FALSE(svg.contains("linearGradient"));
    EXPECT_FALSE(svg.contains("feGaussianBlur stdDeviation=\"3.5\""));
    EXPECT_FALSE(Geometry::hasFilter(a));

    a.mask->fade    = QLineF(0.0, 0.5, 1.0, 0.5);
    a.mask->feather = 3.5;
    svg             = Svg::write(a);
    EXPECT_TRUE(svg.contains("<linearGradient id=\"pm-mask-fade\""));
    EXPECT_TRUE(svg.contains("feGaussianBlur stdDeviation=\"3.5\""));
    EXPECT_TRUE(Geometry::hasFilter(a));
    bool ok = false;
    EXPECT_EQ(Svg::read(svg, &ok).mask, a.mask);   // the rest is read as RoundTrips reads it
    EXPECT_TRUE(ok);

    // Kept inside, black and white swap: the whole mask black, the gradient from white.
    a.mask->keepInside = true;
    svg                = Svg::write(a);
    EXPECT_TRUE(svg.contains("fill=\"black\"/><path"));
    EXPECT_TRUE(svg.contains("<stop offset=\"0\" stop-color=\"white\"/>"));
    a.mask->keepInside = false;

    // A patterned transition: the region black up to the fade's end, the marks white over it, all blurred.
    a.mask->transition = Paint::Kind::Dots;
    svg                = Svg::write(a);
    EXPECT_FALSE(svg.contains("<linearGradient id=\"pm-mask-fade\""));
    EXPECT_TRUE(svg.contains("<g filter=\"url(#pm-mask-feather)\"><path"));
    EXPECT_TRUE(svg.contains("fill-rule=\"nonzero\" fill=\"white\"/></g>"));
}
