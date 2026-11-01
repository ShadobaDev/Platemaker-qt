#ifndef PROPERTYGROUPS_HPP
#define PROPERTYGROUPS_HPP

#include <QColor>
#include <QList>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QtGlobal>

#include <type_traits>
#include <utility>

#include "fields.hpp"

/**
 * @file
 * @brief The property groups an ObjectRecord is made of — Shape, Skin, Style, Text, the tails and one tail.
 *
 * Each group is a plain struct with from() / applyTo() / operator==, and the record (objectrecord.hpp) holds
 * one of each. The editor that edits a group lives in `widgets/stripeditor/properties/`, one per group.
 *
 * A group of scalars lists its fields once, in `fields()`: the key each is saved under and the member it
 * is (fields.hpp). Its operator==, its JSON and its `pm:` attributes are all walks over that list, so a
 * new property is one line there.
 */

/**
 * @brief One tail: where it points, how wide it leaves the balloon, and how much it curves.
 *
 * The tip is in balloon coordinates and **may fall outside the balloon** — that is the whole point of a
 * tail, and it is why the record's drawn extent is computed rather than assumed (Geometry::bounds()).
 * Length is not stored: it is the distance from the balloon to the tip, so aiming and lengthening are
 * one gesture.
 */
struct Tail
{
    QPointF tip;                //!< Balloon coordinates; outside the balloon is normal.
    qreal   baseWidth = 34.0;   //!< Width where it emerges from the silhouette, in balloon pixels.
    qreal   bend      = 0.0;    //!< -1..+1 — sideways offset of the curve, as a fraction of its length.

    [[nodiscard]] bool operator==(const Tail& o) const
    {
        return tip == o.tip
            && qFuzzyCompare(baseWidth, o.baseWidth)
            && qFuzzyCompare(1.0 + bend, 1.0 + o.bend);   // +1 so an exact 0.0 compares equal
    }
    [[nodiscard]] bool operator!=(const Tail& o) const { return !(*this == o); }
};

/**
 * @brief Where a balloon's **first** tail points: down and a little left of centre.
 *
 * Which is where a reader expects a speech balloon to be speaking from. It lives beside the tail rather
 * than in whichever editor happens to seed one, because three of them do — the tails editor, the tail
 * list and a fresh placement — and a rule written out once per caller is a rule that drifts. A *later*
 * tail is seeded from the one before it, not from here.
 *
 * The shape tiles deliberately do **not** use this: a thumbnail draws a short tail so that a speaking
 * shape does not come out smaller than the rest of the grid, and that is a drawing tweak rather than a
 * different answer to this question.
 */
[[nodiscard]] inline QPointF firstTailTip(QSize box)
{
    return {box.width() * 0.28, box.height() * 1.25};
}

struct ObjectRecord;

/**
 * @brief What a part of the balloon is painted with (PLAN-L B33): a colour, or a pattern of a second colour
 *        over it — dots, lines or a grid, a halftone. One type for the fill, the line and the shadow.
 *
 * A **sub-group**: a group holds one as a member and lists its fields with keys of its own (fieldsAs()),
 * so the files stay flat (`fill`, `fillKind`, `fillSpacing`…) and the list of a Paint's fields is
 * written once (members()). Drawn alike by the painter's brush and the SVG's pattern, from one cell
 * (Geometry::patternCell()), laid from the box's corner.
 */
struct Paint
{
    /**
     * @brief The pattern, or none. Persisted by name — see patternName(). The table is in styles.cpp.
     */
    enum class Kind { Solid, Dots, Lines, Grid };
    //! The enum's last value. **Adding a pattern moves this**: patternFromName() reads up to it.
    static constexpr Kind k_lastKind = Kind::Grid;

    QColor colour{255, 255, 255};   //!< The colour; under a pattern, its ground.
    Kind   kind = Kind::Solid;
    QColor second{20, 20, 20};      //!< The pattern's own colour, over the ground.
    /**
     * @name A pattern's cell: a square grid, turned. Read only while `kind` is not Solid. In balloon pixels
     * and degrees, so it is the same on screen and in the output.
     * @{
     */
    qreal  spacing = 6.0;   //!< From one dot, or line, to the next.
    qreal  size    = 3.0;   //!< A dot's diameter, or a line's width. Past the spacing they merge.
    int    angle   = 45;    //!< How far the grid is turned, clockwise.
    /** @} */
    /**
     * @name A fade (PLAN-L P13c, P13d): a cross in the balloon's rectangle, at `fadeCentreX`, `fadeCentreY`
     * (% of its width and height, the middle by default), an arm towards each side. Along an arm the
     * pattern is whole up to `…From` and gone past `…To`, in % of the way from the centre to that side (100
     * is the side); the ground stays. The two directions combine as a distance, so fading towards two
     * neighbouring sides rounds the corner between them, and all four make a round fade. A side whose
     * `…From` is 100 does not fade. `fadeRound` lays the fade on ellipses instead: the starts on one, the
     * ends on another, so all four arms make an ellipse and not a rounded rectangle. `fadeReversed` turns
     * the tone over: gone at the centre, whole past the ends. Read only while `kind` is not Solid, and only
     * where a group lists these fields (the fill). Set on the strip, by the cross in shape editing, all but
     * the shape and the reversal (the panel's).
     * @{
     */
    qreal fadeLeftFrom   = 100.0;
    qreal fadeLeftTo     = 100.0;
    qreal fadeRightFrom  = 100.0;
    qreal fadeRightTo    = 100.0;
    qreal fadeTopFrom    = 100.0;
    qreal fadeTopTo      = 100.0;
    qreal fadeBottomFrom = 100.0;
    qreal fadeBottomTo   = 100.0;
    qreal fadeCentreX    = 50.0;
    qreal fadeCentreY    = 50.0;
    bool  fadeReversed   = false;
    bool  fadeRound      = false;
    /** @} */

    //! How many fields members() lists: the length of a key table.
    static constexpr int k_fieldCount = 18;

    Paint() = default;
    Paint(QColor colour, QColor second) : colour(colour), second(second) {}

    //! Each field with its place in a key table: the one list both fields() and fieldsAs() walk.
    template <class V>
    static void members(V&& v)
    {
        v(0, &Paint::colour);
        v(1, &Paint::kind);
        v(2, &Paint::second);
        v(3, &Paint::spacing);
        v(4, &Paint::size);
        v(5, &Paint::angle);
        v(6, &Paint::fadeLeftFrom);
        v(7, &Paint::fadeLeftTo);
        v(8, &Paint::fadeRightFrom);
        v(9, &Paint::fadeRightTo);
        v(10, &Paint::fadeTopFrom);
        v(11, &Paint::fadeTopTo);
        v(12, &Paint::fadeBottomFrom);
        v(13, &Paint::fadeBottomTo);
        v(14, &Paint::fadeCentreX);
        v(15, &Paint::fadeCentreY);
        v(16, &Paint::fadeReversed);
        v(17, &Paint::fadeRound);
    }

    //! The fields under plain keys: what a Paint's own editor (PaintRows) walks.
    template <class V>
    static void fields(V&& v)
    {
        // The cell's numbers share a prefix, which is what PaintRows' rows are built from.
        static constexpr const char* k_keys[] = {
            "colour",        "kind",        "second",         "cellSpacing",  "cellSize",
            "cellAngle",     "fadeLeftFrom", "fadeLeftTo",    "fadeRightFrom", "fadeRightTo",
            "fadeTopFrom",   "fadeTopTo",   "fadeBottomFrom", "fadeBottomTo", "fadeCentreX",
            "fadeCentreY",   "fadeReversed", "fadeRound"};
        members([&](int i, auto member) { v(k_keys[i], member); });
    }

    /**
     * @brief The fields of the Paint @p group holds as @p paint, under @p keys (in members()' order): what
     *        that group's own fields() lists, so its files keep their flat keys.
     *
     * A null key leaves that field out: the place does not use it (a shadow's pattern has no colour of its
     * own, only the fill fades), so it is neither saved, compared nor offered (PaintRows asks the list).
     */
    template <class V, class Group>
    static void fieldsAs(V&& v, Paint Group::* paint, const char* const (&keys)[k_fieldCount])
    {
        members([&](int i, auto member) {
            using T = std::remove_reference_t<decltype(std::declval<Paint&>().*member)>;
            if (keys[i])
                v(keys[i], Fields::Nested<Group, Paint, T>{paint, member});
        });
    }

    [[nodiscard]] bool operator==(const Paint& o) const { return Fields::equal(*this, o); }
    [[nodiscard]] bool operator!=(const Paint& o) const { return !(*this == o); }
};

/**
 * @brief One node of a free outline, a Polygon's or a Curve's, **relative to the balloon**: 0..1 across
 *        its width and its height, so resizing the box scales the shape (PLAN-L §3.4).
 *
 * A Polygon reads only the point. A Curve also reads the two handles, kept as offsets from the point so
 * a node dragged takes its handles along; a zero handle is none, and a segment with none at either end
 * is straight. The type says how a drag of one handle moves the other (PLAN-L B21).
 */
struct ShapeNode
{
    enum class Type {
        Corner,      //!< The handles are independent.
        Smooth,      //!< The handles stay on one line through the point.
        Symmetric,   //!< On one line, and as long as each other.
    };

    QPointF at;
    QPointF in;    //!< Towards the handle the segment from the previous node arrives along.
    QPointF out;   //!< Towards the handle the segment to the next node leaves along.
    Type    type = Type::Corner;

    ShapeNode() = default;
    ShapeNode(qreal x, qreal y) : at(x, y) {}
    ShapeNode(QPointF at, QPointF in, QPointF out, Type type = Type::Corner)
        : at(at), in(in), out(out), type(type) {}

    //! A Polygon's vertex: a corner with no handles, saved as its point alone.
    [[nodiscard]] bool plain() const { return type == Type::Corner && in.isNull() && out.isNull(); }

    //! Persisted, in the JSON and the SVG. **Never changes** once saved.
    [[nodiscard]] static const char* typeName(Type t)
    {
        switch (t) {
        case Type::Smooth:    return "smooth";
        case Type::Symmetric: return "symmetric";
        case Type::Corner:    break;
        }
        return "corner";
    }
    //! An unknown name reads as a corner, which leaves both handles where they are.
    [[nodiscard]] static Type typeFromName(QStringView name)
    {
        for (Type t : {Type::Smooth, Type::Symmetric})
            if (name == QLatin1String(typeName(t)))
                return t;
        return Type::Corner;
    }

    [[nodiscard]] bool operator==(const ShapeNode& o) const
    {
        return Fields::same(at, o.at) && Fields::same(in, o.in) && Fields::same(out, o.out) && type == o.type;
    }
    [[nodiscard]] bool operator!=(const ShapeNode& o) const { return !(*this == o); }
};

/**
 * @brief The balloon's own surface: what it is filled with. The line drawn round it is the line's
 *        (StyleProperties), with its colour and width (PLAN-L B32).
 *
 * The first **property group** — a named slice of an object's state with exactly one editor
 * responsible for it. Groups exist so that two editors can never write the same field: an editor
 * writes through applyTo() and applyTo() assigns one member, so "this editor touched something that
 * was not its own" stops being expressible rather than being something to remember.
 *
 * The text's colour is deliberately **not** here. Editing a balloon's surface does not edit its
 * lettering, so the text's colour belongs with the text.
 *
 * Persisted as flat keys, as every group is.
 */
struct SkinProperties
{
    //! What the balloon is filled with: white by default, a pattern's dots dark over it.
    Paint fill{QColor(255, 255, 255), QColor(20, 20, 20)};

    //! The fields, with the keys they are saved under (fields.hpp).
    template <class V>
    static void fields(V&& v)
    {
        static constexpr const char* k_fill[] = {
            "fill",              "fillKind",            "fillSecond",         "fillSpacing",
            "fillSize",          "fillAngle",           "fillFadeLeftFrom",   "fillFadeLeftTo",
            "fillFadeRightFrom", "fillFadeRightTo",     "fillFadeTopFrom",    "fillFadeTopTo",
            "fillFadeBottomFrom", "fillFadeBottomTo",   "fillFadeCentreX",    "fillFadeCentreY",
            "fillFadeReversed",  "fillFadeRound"};
        Paint::fieldsAs(v, &SkinProperties::fill, k_fill);
    }

    //! Reads this group out of \p a.
    [[nodiscard]] static SkinProperties from(const ObjectRecord& a);

    /**
     * @brief Writes this group into \p a — **and nothing else**.
     *
     * The single write path for everything that edits a balloon's surface. Guarded by
     * `SkinPropertiesOwnership` in the unit tests, which applies a group to a randomised record and
     * asserts that every property outside it is unchanged.
     */
    void applyTo(ObjectRecord& a) const;

    [[nodiscard]] bool operator==(const SkinProperties& o) const { return Fields::equal(*this, o); }
    [[nodiscard]] bool operator!=(const SkinProperties& o) const { return !(*this == o); }
};

/**
 * @brief Which silhouette is drawn behind the text, and its parameters.
 *
 * A kind is a **family** — a way of describing an outline — and its parameters say which one. The
 * ready-made outlines the picker offers (a speech balloon, a caption box) are **templates**: values of
 * this group (shapes.hpp). A parameter the current kind does not read is carried unchanged.
 */
struct ShapeProperties
{
    /**
     * @brief The silhouette drawn behind the text. `None` is the Text tool: letters with no balloon.
     *
     * Persisted by name, not by number (see shapeName()), so the order here is free. The strip editor
     * is unreleased, so a kind may still be renamed or merged (PLAN-L B3): a file naming one that is gone
     * loads as a Box.
     */
    enum class Kind { None, Box, Ellipse, Polygon, Cloud, Curve };

    //! The enum's last value. **Adding a shape moves this** — it is how the unit tests know a shape
    //! exists that the picker (shapeOrder()) does not offer, and which therefore would not load either.
    static constexpr Kind k_lastKind = Kind::Curve;

    /**
     * @brief What is done to the outline's edge (PLAN-L B14, M11): pushed in along it in a pattern, any
     * family alike, before the tails are united into it so a tail stays smooth (B26). The table is in
     * shapes.cpp; persisted by name (edgeName()).
     */
    enum class Edge { None, Zigzag };
    //! The enum's last value. **Adding an edge moves this**: edgeFromName() reads up to it.
    static constexpr Edge k_lastEdge = Edge::Zigzag;

    //! Decimal places a node is saved with in the SVG; a template's nodes are rounded to them, so a
    //! placed template reads back as itself. 4 is a fifth of a pixel on a 2000 px balloon.
    static constexpr int k_nodeDecimals = 4;

    Kind  kind         = Kind::Box;
    /**
     * A Box's or a Polygon's corner radius, in balloon pixels. A Box caps it at half its shorter side, a
     * Polygon each corner at half its shorter edge. The default is the speech balloon's (shapes.cpp), so
     * a default record is the speech balloon it always was.
     */
    qreal cornerRadius = 45.0;
    /**
     * A Polygon's or a Curve's nodes, in order (ShapeNode), relative to the balloon so a template fits
     * any size. Fewer than three draw the box's own rectangle.
     */
    QList<ShapeNode> nodes;
    //! A Cloud's lobes, the circles around its edge. The defaults are the thought balloon's.
    int lobes    = 11;
    //! A lobe's radius, in % of the balloon's shorter half-axis.
    int lobeSize = 40;

    /**
     * @name The edge's modulation: its kind, and how it is laid along the outline. Every family takes
     * them, and a template does not set them: picking a tile keeps the edge. Read only while `edge` is
     * not None. The teeth go **in** from the outline, so the balloon keeps its bounds and its text area
     * gives up the depth.
     * @{
     */
    Edge  edge             = Edge::None;
    qreal edgePitch        = 16.0;   //!< One tooth every this many balloon pixels along the edge.
    qreal edgeDepth        = 6.0;    //!< How far in a tooth's valley reaches, in balloon pixels.
    int   edgeIrregularity = 0;      //!< How much each tooth's depth and place vary, in %.
    /** @} */

    //! The fields, with the keys they are saved under (fields.hpp).
    template <class V>
    static void fields(V&& v)
    {
        v("shape",        &ShapeProperties::kind);
        v("cornerRadius", &ShapeProperties::cornerRadius);
        v("nodes",        &ShapeProperties::nodes);
        v("lobes",        &ShapeProperties::lobes);
        v("lobeSize",     &ShapeProperties::lobeSize);
        v("edge",             &ShapeProperties::edge);
        v("edgePitch",        &ShapeProperties::edgePitch);
        v("edgeDepth",        &ShapeProperties::edgeDepth);
        v("edgeIrregularity", &ShapeProperties::edgeIrregularity);
    }

    [[nodiscard]] static ShapeProperties from(const ObjectRecord& a);
    void applyTo(ObjectRecord& a) const;

    [[nodiscard]] bool operator==(const ShapeProperties& o) const { return Fields::equal(*this, o); }
    [[nodiscard]] bool operator!=(const ShapeProperties& o) const { return !(*this == o); }
};

/**
 * @brief What is done to the balloon as it is rasterised (PLAN-L B4): its texture, an SVG filter on the
 *        balloon's group — its silhouette, its line's strokes, its shadow — never on the words.
 *
 * Its own group, and its own section of the panel (B32), apart from the line: the two are independent,
 * so a flash or a brush can be rough.
 */
struct TextureProperties
{
    /**
     * @brief A texture is an SVG filter, so librsvg applies it and Qt cannot: that is the whole reason
     * such a balloon is previewed through the library instead of being drawn locally — an effect only the
     * committed output could show would be an effect nobody could author. None is a true no-op: no filter
     * is emitted at all.
     *
     * Persisted by name — see textureName().
     */
    enum class Kind { None, Rough };
    //! The enum's last value. **Adding a texture moves this**: textureFromName() reads up to it.
    static constexpr Kind k_lastKind = Kind::Rough;

    Kind kind = Kind::None;

    /**
     * @name Rough: turbulence displacing the outline, then a blur (PLAN-L B2). Read only while `kind` is
     * Rough, and carried unchanged otherwise. The defaults are the old Marker's numbers; the Ink look is
     * a built-in preset (Geometry::inkRough()). Lengths in balloon pixels, so the wobble scales with the
     * balloon, not with the output's resolution.
     * @{
     */
    qreal roughFrequency    = 0.03;   //!< feTurbulence's baseFrequency: higher is a finer grain.
    int   roughOctaves      = 3;      //!< How many finer wobbles ride on the first.
    qreal roughDisplacement = 6.0;    //!< How far the outline wanders (feDisplacementMap's scale).
    qreal roughBlur         = 0.0;    //!< The bleed after it (feGaussianBlur's deviation); 0 for none.
    /** @} */

    //! The fields, with the keys they are saved under (fields.hpp).
    template <class V>
    static void fields(V&& v)
    {
        v("texture",           &TextureProperties::kind);
        v("roughFrequency",    &TextureProperties::roughFrequency);
        v("roughOctaves",      &TextureProperties::roughOctaves);
        v("roughDisplacement", &TextureProperties::roughDisplacement);
        v("roughBlur",         &TextureProperties::roughBlur);
    }

    [[nodiscard]] static TextureProperties from(const ObjectRecord& a);
    void applyTo(ObjectRecord& a) const;

    [[nodiscard]] bool operator==(const TextureProperties& o) const { return Fields::equal(*this, o); }
    [[nodiscard]] bool operator!=(const TextureProperties& o) const { return !(*this == o); }
};

/**
 * @brief The line drawn round the balloon: what kind, in what colour, how wide, and each kind's numbers.
 *
 * Clean is a solid outline; Flash and Brush are strokes of their own. The colour and the width are the
 * line's (PLAN-L B32), so a brush's width sits with the brush; the panel's Line section is this group.
 *
 * The **seed is not in here**, deliberately. It is per balloon and set once at placement, so a preset
 * that carried it would give a whole chapter one repeated wobble — and a group's applyTo() writes the
 * whole group, which would do exactly that. It stays a bare field with no editor.
 */
struct StyleProperties
{
    /**
     * @brief How the edge is drawn. Clean is the solid outline; Flash and Brush are strokes of their
     * own, plain geometry, so Qt draws them as it draws a clean balloon. The table is in styles.cpp.
     *
     * Persisted by name — see lineName(). The strip editor is unreleased (PLAN-L B3), so the order is
     * free.
     */
    enum class Line { Clean, Flash, Brush };
    //! The enum's last value. **Adding a line moves this**: lineFromName() reads up to it.
    static constexpr Line k_lastLine = Line::Brush;

    //! `flashCoreStretch`'s "follow the shape" value.
    static constexpr int k_autoStretch = -1;

    Line  line = Line::Clean;
    //! The outline's paint, and a flash's or a brush's strokes': near black, a pattern's lines white over it.
    Paint stroke{QColor(20, 20, 20), QColor(255, 255, 255)};
    int   strokeWidth = 5;      //!< The outline's width; a brush's too. 0 draws no line.

    /**
     * @name The flash: narrow triangles ringing the silhouette, outwards and inwards (styles.cpp).
     * Read only while `line` is Flash, and carried unchanged by every other line, so switching back
     * finds them as they were. Lengths in balloon pixels; jitter in % of the pitch or the length.
     * @{
     */
    qreal flashOuterPitch        = 5.0;   //!< One outer stroke every this many pixels along the edge.
    qreal flashInnerPitch        = 7.0;   //!< Likewise, inside.
    qreal flashOuterLength       = 18.0;  //!< How far an outer stroke reaches out from the edge.
    qreal flashInnerLength       = 10.0;  //!< How far an inner stroke reaches in.
    qreal flashOuterBase         = 4.0;   //!< An outer stroke's width where it leaves the edge.
    qreal flashInnerBase         = 3.0;   //!< Likewise, inside.
    int   flashOuterPitchJitter  = 60;    //!< How much each outer stroke's place varies.
    int   flashInnerPitchJitter  = 60;
    int   flashOuterLengthJitter = 50;    //!< How much each outer stroke's length varies.
    int   flashInnerLengthJitter = 50;
    /**
     * What the strokes point away from, in %: 0 is the box's centre; more stretches it into a line
     * along the longer axis, which squares the strokes up with a long straight side. **k_autoStretch**
     * (the default) takes the shape's own value (ShapeInfo::flashStretch) instead, so it follows the
     * shape; a value set by hand stays through any change of shape.
     */
    int   flashCoreStretch       = k_autoStretch;
    /** @} */

    /**
     * @name The brush: a few strokes laid along the edge, each tapering to a point at both ends and
     * swelling and wandering along the way (styles.cpp). Read only while `line` is Brush. Its width is the
     * outline's (`strokeWidth`), not a number of its own. In % of a stroke's share of the
     * edge, of its length, of its width, or in balloon pixels.
     * @{
     */
    int   brushStrokes = 3;     //!< How many strokes go round each edge.
    int   brushOverlap = 20;    //!< How far each runs past where the next begins, in % of its share.
    int   brushTaper   = 30;    //!< How much of a stroke, at each end, narrows to its point, in %.
    int   brushSwell   = 40;    //!< How much a stroke's width varies along it, in %.
    qreal brushDrift   = 1.5;   //!< How far a stroke wanders off the edge, in balloon pixels.
    /** @} */

    //! The fields, with the keys they are saved under (fields.hpp).
    template <class V>
    static void fields(V&& v)
    {
        static constexpr const char* k_stroke[] = {
            "stroke",     "strokeKind",  "strokeSecond", "strokeSpacing", "strokeSize", "strokeAngle",
            nullptr,      nullptr,       nullptr,        nullptr,         nullptr,      nullptr,
            nullptr,      nullptr,       nullptr,        nullptr,         nullptr,      nullptr};   // a line does not fade
        v("line",                   &StyleProperties::line);
        Paint::fieldsAs(v, &StyleProperties::stroke, k_stroke);
        v("strokeWidth",            &StyleProperties::strokeWidth);
        v("flashOuterPitch",        &StyleProperties::flashOuterPitch);
        v("flashInnerPitch",        &StyleProperties::flashInnerPitch);
        v("flashOuterLength",       &StyleProperties::flashOuterLength);
        v("flashInnerLength",       &StyleProperties::flashInnerLength);
        v("flashOuterBase",         &StyleProperties::flashOuterBase);
        v("flashInnerBase",         &StyleProperties::flashInnerBase);
        v("flashOuterPitchJitter",  &StyleProperties::flashOuterPitchJitter);
        v("flashInnerPitchJitter",  &StyleProperties::flashInnerPitchJitter);
        v("flashOuterLengthJitter", &StyleProperties::flashOuterLengthJitter);
        v("flashInnerLengthJitter", &StyleProperties::flashInnerLengthJitter);
        v("flashCoreStretch",       &StyleProperties::flashCoreStretch);
        v("brushStrokes",           &StyleProperties::brushStrokes);
        v("brushOverlap",           &StyleProperties::brushOverlap);
        v("brushTaper",             &StyleProperties::brushTaper);
        v("brushSwell",             &StyleProperties::brushSwell);
        v("brushDrift",             &StyleProperties::brushDrift);
    }

    [[nodiscard]] static StyleProperties from(const ObjectRecord& a);
    void applyTo(ObjectRecord& a) const;

    [[nodiscard]] bool operator==(const StyleProperties& o) const { return Fields::equal(*this, o); }
    [[nodiscard]] bool operator!=(const StyleProperties& o) const { return !(*this == o); }
};

/**
 * @brief What is drawn about the balloon, beside its outline and its fill (PLAN-L M14, B23): a shadow so far.
 *
 * A group of its own, so applying a look's line does not apply its shadow by accident, and an effect
 * added later (a glow) is a field here. Its editor is laid out in the Style section, beside the line and
 * the texture, so the artist sees one place for how the balloon is drawn.
 */
struct EffectProperties
{
    /**
     * @name The shadow: the whole silhouette, tails and all, grown by the spread, moved by the offset and
     * filled with its paint, under the balloon. Hard at softness 0, drawn as it is on the strip; above it,
     * blurred by the library (an SVG filter), and the balloon is then previewed through the library. By
     * default it is centred: a rim on every side (B36).
     * @{
     */
    bool   shadow         = false;
    qreal  shadowSpread   = 6.0;           //!< How far it grows past the balloon on every side, in balloon pixels.
    qreal  shadowX        = 0.0;           //!< How far right, in balloon pixels; negative is left.
    qreal  shadowY        = 0.0;           //!< How far down; negative is up.
    qreal  shadowSoftness = 0.0;           //!< The blur's deviation, in balloon pixels; 0 is a hard edge.
    //! Its paint: a colour, whose alpha is how dark it is, or a halftone of it.
    Paint  shadowFill{QColor(0, 0, 0, 110), QColor(0, 0, 0)};
    /** @} */

    //! The fields, with the keys they are saved under (fields.hpp).
    template <class V>
    static void fields(V&& v)
    {
        v("shadow",         &EffectProperties::shadow);
        v("shadowSpread",   &EffectProperties::shadowSpread);
        v("shadowX",        &EffectProperties::shadowX);
        v("shadowY",        &EffectProperties::shadowY);
        v("shadowSoftness", &EffectProperties::shadowSoftness);
        // A shadow's pattern is its colour in marks on nothing (no second colour), and it fades by its
        // softness, not by a cross.
        static constexpr const char* k_shadowFill[] = {
            "shadowColour", "shadowKind", nullptr, "shadowSpacing", "shadowSize", "shadowAngle",
            nullptr,        nullptr,      nullptr, nullptr,         nullptr,      nullptr,
            nullptr,        nullptr,      nullptr, nullptr,         nullptr,      nullptr};
        Paint::fieldsAs(v, &EffectProperties::shadowFill, k_shadowFill);
    }

    [[nodiscard]] static EffectProperties from(const ObjectRecord& a);
    void applyTo(ObjectRecord& a) const;

    [[nodiscard]] bool operator==(const EffectProperties& o) const { return Fields::equal(*this, o); }
    [[nodiscard]] bool operator!=(const EffectProperties& o) const { return !(*this == o); }
};

/**
 * @brief What the balloon says, and how it is set.
 *
 * Content and typography in one group because one widget edits both, and because the split that
 * matters is elsewhere: a **preset** carries the typography and never the content — *everything a
 * balloon is, minus everything it says*. The colour is here rather than with the fill and stroke for
 * the same reason the colour tool has only two swatches: editing a frame is not editing its lettering.
 *
 * Shared, unchanged, by whatever has text — a balloon, a caption, and eventually a standalone text
 * object — rather than each kind carrying its own copy of the same six properties.
 */
struct TextProperties
{
    QString body;                     //!< The lettering itself.
    QString family;                   //!< Empty = the application's default family.
    int     pixelSize = 30;           //!< Strip-scale pixels, so it means the same thing in the output.
    bool    bold      = false;
    int     align     = Qt::AlignHCenter;   //!< Horizontal alignment of each line.
    QColor  colour{20, 20, 20};
    //! Line pitch as a percentage of the font's own — for faces that set their lines too far apart.
    int     lineSpacing = 100;
    /**
     * @brief How far the lettering keeps from the balloon's outline, in strip pixels, on top of the
     *        stroke — an inset of the shape's own text area.
     *
     * The lettering's, not the balloon's: it is how the words sit, and it is set beside the rest of how
     * they are set. **Only a balloon uses it** — a text object has no outline to keep away from, so
     * the value is carried and ignored, and the editor does not offer it there. **May be negative**:
     * the text area then reaches past the outline, which an impact balloon with lettering bursting out
     * of it needs. 8 is the inset every balloon had before it was a property.
     */
    int     padding = 8;

    //! The fields, with the keys they are saved under (fields.hpp). The keys predate the members'
    //! names, which is why three of them differ.
    template <class V>
    static void fields(V&& v)
    {
        v("text",        &TextProperties::body);
        v("fontFamily",  &TextProperties::family);
        v("fontSize",    &TextProperties::pixelSize);
        v("bold",        &TextProperties::bold);
        v("align",       &TextProperties::align);
        v("lineSpacing", &TextProperties::lineSpacing);
        v("padding",     &TextProperties::padding);
        v("textColour",  &TextProperties::colour);
    }

    [[nodiscard]] static TextProperties from(const ObjectRecord& a);
    void applyTo(ObjectRecord& a) const;

    [[nodiscard]] bool operator==(const TextProperties& o) const { return Fields::equal(*this, o); }
    [[nodiscard]] bool operator!=(const TextProperties& o) const { return !(*this == o); }
};

/**
 * @brief Every tail on the balloon.
 *
 * The whole list is one group today because a tail is not yet addressable on its own: there is no way
 * to say *which* tail, which is why one can be added and never removed. When tails become objects in
 * their own right this splits into a group per tail, and the list stops being a property at all.
 */
struct TailsProperties
{
    QList<Tail> items;   //!< Empty = no tail. More than one = one sound, several speakers.

    [[nodiscard]] static TailsProperties from(const ObjectRecord& a);
    void applyTo(ObjectRecord& a) const;

    [[nodiscard]] bool operator==(const TailsProperties& o) const { return items == o.items; }
    [[nodiscard]] bool operator!=(const TailsProperties& o) const { return !(*this == o); }
};

/**
 * @brief One tail of a balloon — the group a selected tail is edited through.
 *
 * A tail is an object of its own, and this is its state. It is addressed by **position** in the balloon's
 * list rather than by an id: undo restores whole states and holds no tail by number, so an index is enough,
 * and a selection that outlives a change in the number of tails simply moves up to its balloon.
 *
 * applyTo() writes that one tail and leaves the others exactly as they were; an index the balloon no longer
 * has writes nothing, rather than resurrecting a tail that was deleted.
 */
struct TailProperties
{
    int  index = 0;     //<! Which tail in the balloon's list is being edited. Out of range = no tail.
    Tail tail;          //<! The tail's own state, which applyTo() writes into the balloon at `index`.

    /**
     * @brief Reads the tail at \p index out of \p a, or an empty tail if the index is out of range.
     * @param a The balloon to read from.
     * @param index The tail to read, 0..n-1. Out of range = no tail, and applyTo() will write nothing.
     * @return The tail at \p index, or an empty tail if the index is out of range.
     * @see applyTo()
     */
    [[nodiscard]] static TailProperties from(const ObjectRecord& a, int index);
    
    /**
     * @brief Writes the tail at \p index into \p a, or does nothing if the index is out of range.
     * @param a The balloon to write to.
     * @see from()
     */
    void applyTo(ObjectRecord& a) const;

    /**
     * @brief Compares two tail properties for equality.
     * @param o The other tail properties to compare with.
     * @return True if the tail properties are equal, false otherwise.
     */
    [[nodiscard]] bool operator==(const TailProperties& o) const { return index == o.index && tail == o.tail; }

    /**
     * @brief Compares two tail properties for inequality.
     * @param o The other tail properties to compare with.
     * @return True if the tail properties are not equal, false otherwise.
     */
    [[nodiscard]] bool operator!=(const TailProperties& o) const { return !(*this == o); }
};

#endif // PROPERTYGROUPS_HPP
