#ifndef PROPERTYGROUPS_HPP
#define PROPERTYGROUPS_HPP

#include <QColor>
#include <QList>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <type_traits>
#include <utility>

#include "fields.hpp"

/**
 * \file
 * \brief The property groups an ObjectRecord is made of — Shape, Skin, Style, Text, the tails and one tail.
 *
 * Each group is a plain struct with from() / applyTo() / operator==, and the record (objectrecord.hpp) holds
 * one of each. The editor that edits a group lives in `widgets/stripeditor/properties/`, one per group.
 *
 * A group of scalars lists its fields once, in `fields()`: the key each is saved under and the member it
 * is (fields.hpp). Its operator==, its JSON and its `pm:` attributes are all walks over that list, so a
 * new property is one line there.
 */

/**
 * \brief One tail: where it points, how wide it leaves the balloon, and how much it curves.
 *
 * The tip is in balloon coordinates and **may fall outside the balloon** — that is the whole point of a
 * tail, and it is why the record's drawn extent is computed rather than assumed (Geometry::bounds()).
 * Length is not stored: it is the distance from the balloon to the tip, so aiming and lengthening are
 * one gesture.
 */
/**
 * \brief How a tail is drawn: one taper (**Plain**), or a chain of ellipses shrinking towards its tip
 *        (**Circles**), a thought's.
 *
 * A Plain tail ends as its `tip` says: in a point (**Sharp**), a half circle (**Round**) whose far point is
 * the tail's tip, or a straight end (**Cut**) through the tip, square to the tail there. A round or cut end
 * is `tipWidth` per cent of the tail's base width, so it follows a resize as the base does.
 *
 * A Plain tail may be **broken**: its middle runs in straight lines through `breaks` points, evenly spaced
 * along its bent course and moved across it by `jog` per cent of its length, to either side in turn, the
 * first a clockwise turn (to its right as it runs from the balloon) unless `clockwise` says otherwise, so
 * it zigzags in sharp turns on its way while its base and its tip stay where they are.
 *
 * The ellipses lie on the plain tail's own curve, bend and all, from where it leaves the balloon (moved by
 * `offset`) to the tip, the last one ending there. Across, the first is the tail's width and the last
 * `k_lastCircle` of it; along, they share what the gaps leave of the way in the same proportion. So the
 * tip stays on the chain however it is dragged, and a short tail's ellipses come out flat, a long one's
 * tall. Each style's and tip's numbers are a section (CirclesParams…), kept whatever the style, so switching
 * back finds them as they were; the tables in styles.cpp list which a style and a tip read.
 */
//! A Circles tail's chain (TailLook). In balloon pixels.
struct CirclesParams
{
    int   count  = 3;     //!< How many ellipses make the tail.
    qreal pitch  = 6.0;   //!< The gap between two of them along the tail; less than 0 overlaps.
    qreal offset = 0.0;   //!< How far into the balloon the first starts: less than 0 leaves a gap.

    template <class V>
    static void fields(V&& v)
    {
        v("circles", &CirclesParams::count);
        v("pitch",   &CirclesParams::pitch);
        v("offset",  &CirclesParams::offset);
    }
    [[nodiscard]] static const Fields::Section& section();
    [[nodiscard]] bool operator==(const CirclesParams& o) const { return Fields::equal(*this, o); }
};

//! A Round or Cut tip's end (TailLook).
struct EndParams
{
    int width = 30;   //!< In per cent of the tail's base width, 0..100.

    template <class V>
    static void fields(V&& v)
    {
        v("tipWidth", &EndParams::width);
    }
    [[nodiscard]] static const Fields::Section& section();
    [[nodiscard]] bool operator==(const EndParams& o) const { return Fields::equal(*this, o); }
};

//! A Plain tail's breaks, its sharp turns (TailLook).
struct BreakParams
{
    //! The most breaks a tail takes.
    static constexpr int k_maxBreaks = 3;
    //! The furthest a break is moved across the tail, in per cent of its length.
    static constexpr int k_maxJog = 50;

    int  count     = 0;      //!< How many sharp turns the tail's middle takes, 0..k_maxBreaks.
    int  jog       = 10;     //!< How far across each is moved, in per cent of the length, 0..k_maxJog.
    bool clockwise = true;   //!< Whether the first turn is clockwise, to the tail's right.

    template <class V>
    static void fields(V&& v)
    {
        v("breaks",    &BreakParams::count);
        v("jog",       &BreakParams::jog);
        v("clockwise", &BreakParams::clockwise);
    }
    [[nodiscard]] static const Fields::Section& section();
    [[nodiscard]] bool operator==(const BreakParams& o) const { return Fields::equal(*this, o); }
};

struct TailLook
{
    //! Persisted by name (TailStyleInfo, TailTipInfo in styles.hpp); adding one moves its `k_last…`.
    enum class Style { Plain, Circles };
    static constexpr Style k_lastStyle = Style::Circles;
    enum class Tip { Sharp, Round, Cut };
    static constexpr Tip k_lastTip = Tip::Cut;
    //! The last ellipse's width, against the first's.
    static constexpr qreal k_lastCircle = 0.4;

    Style         style = Style::Plain;
    CirclesParams circles;
    Tip           tip   = Tip::Sharp;   //!< How a Plain tail ends.
    EndParams     end;
    BreakParams   breaks;

    //! The sections, in the order the Tail panel shows them.
    template <class V>
    static void sections(V&& v)
    {
        v(&TailLook::end);
        v(&TailLook::breaks);
        v(&TailLook::circles);
    }

    //! The fields, with the keys they are known by (fields.hpp). A file carries them as toText() writes them.
    template <class V>
    static void fields(V&& v)
    {
        v("style", &TailLook::style);
        Fields::nest(v, &TailLook::circles);
        v("tip",   &TailLook::tip);
        Fields::nest(v, &TailLook::end);
        Fields::nest(v, &TailLook::breaks);
    }

    //! As text, "plain,3,6,0,sharp,30,0,10,1": the style's name, the numbers, the tip's name and its width,
    //! the breaks, the jog and whether the first turn is clockwise. How a file carries it.
    [[nodiscard]] QString toText() const;
    //! Out of the fields toText() writes, from @p first on; what is missing or unknown keeps its default.
    [[nodiscard]] static TailLook fromFields(const QStringList& fields, qsizetype first = 0);

    [[nodiscard]] bool operator==(const TailLook& o) const { return Fields::equal(*this, o); }
    [[nodiscard]] bool operator!=(const TailLook& o) const { return !(*this == o); }
};

struct Tail
{
    QPointF  tip;                //!< Balloon coordinates; outside the balloon is normal.
    qreal    baseWidth = 34.0;   //!< Width where it emerges from the silhouette, in balloon pixels.
    qreal    bend      = 0.0;    //!< -1..+1 — sideways offset of the curve, as a fraction of its length.
    TailLook look;               //!< Plain, or a chain of ellipses.

    [[nodiscard]] bool operator==(const Tail& o) const
    {
        return tip == o.tip
            && qFuzzyCompare(baseWidth, o.baseWidth)
            && qFuzzyCompare(1.0 + bend, 1.0 + o.bend)   // +1 so an exact 0.0 compares equal
            && look == o.look;
    }
    [[nodiscard]] bool operator!=(const Tail& o) const { return !(*this == o); }
};

/**
 * \brief Where a balloon's **first** tail points: down and a little left of centre.
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
 * \brief What a part of the balloon is painted with: a colour, or a pattern of a second colour
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
     * \brief The pattern, or none. Persisted by name — see patternName(). The table is in styles.cpp.
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
     * @name A fade: a cross in the balloon's rectangle, at `fadeCentreX`, `fadeCentreY`
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
     * \brief The fields of the Paint @p group holds as @p paint, under @p keys (in members()' order): what
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
 * \brief A shadow: what casts it (a balloon's silhouette, or the letters with their outline) grown by the
 *        spread, moved by the offset and filled with a Paint the group holds beside it.
 *
 * A **sub-group**, like Paint: the balloon's effects and the lettering each hold one, with keys of their own
 * (fieldsAs()). Its Paint is not inside it but the group's own sibling member, since a field list nests one
 * level only. Hard at softness 0, drawn as it is on the strip; above it, blurred by the library (an SVG
 * filter), or, as a halftone, its marks fading as geometry (Geometry::shadowOf()). By default it is
 * centred: a rim on every side.
 */
struct Shadow
{
    bool  on       = false;
    qreal spread   = 6.0;   //!< How far it grows past what casts it, on every side, in strip pixels.
    qreal x        = 0.0;   //!< How far right, in strip pixels; negative is left.
    qreal y        = 0.0;   //!< How far down; negative is up.
    qreal softness = 0.0;   //!< The blur's deviation, in strip pixels; 0 is a hard edge.

    //! How many fields members() lists: the length of a key table.
    static constexpr int k_fieldCount = 5;

    //! Each field with its place in a key table: the one list both fields() and fieldsAs() walk.
    template <class V>
    static void members(V&& v)
    {
        v(0, &Shadow::on);
        v(1, &Shadow::spread);
        v(2, &Shadow::x);
        v(3, &Shadow::y);
        v(4, &Shadow::softness);
    }

    //! The fields under plain keys: what a shadow's own editor (ShadowRows) walks.
    template <class V>
    static void fields(V&& v)
    {
        static constexpr const char* k_keys[] = {"on", "spread", "x", "y", "softness"};
        members([&](int i, auto member) { v(k_keys[i], member); });
    }

    //! The fields of the Shadow @p group holds as @p shadow, under @p keys (in members()' order).
    template <class V, class Group>
    static void fieldsAs(V&& v, Shadow Group::* shadow, const char* const (&keys)[k_fieldCount])
    {
        members([&](int i, auto member) {
            using T = std::remove_reference_t<decltype(std::declval<Shadow&>().*member)>;
            v(keys[i], Fields::Nested<Group, Shadow, T>{shadow, member});
        });
    }

    [[nodiscard]] bool operator==(const Shadow& o) const { return Fields::equal(*this, o); }
    [[nodiscard]] bool operator!=(const Shadow& o) const { return !(*this == o); }
};

/**
 * \brief One node of a free outline, a Polygon's or a Curve's, **relative to the balloon**: 0..1 across
 *        its width and its height, so resizing the box scales the shape.
 *
 * A Polygon reads only the point. A Curve also reads the two handles, kept as offsets from the point so
 * a node dragged takes its handles along; a zero handle is none, and a segment with none at either end
 * is straight. The type says how a drag of one handle moves the other.
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
 * \brief The balloon's own surface: what it is filled with. The line drawn round it is the line's
 *        (StyleProperties), with its colour and width.
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
     * \brief Writes this group into \p a — **and nothing else**.
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
 * @name A shape's sections: the numbers each family and the edge read (shapes.cpp), one struct each, held
 * by ShapeProperties whatever its kind. Their rows are their section() (shapes.cpp); a family's table entry
 * lists the sections it reads.
 * @{
 */

/**
 * \brief A corner's radius, in balloon pixels: read by Box and Polygon. A Box caps it at half its shorter
 *        side, a Polygon each corner at half its shorter edge. The default is the speech balloon's
 *        (shapes.cpp), so a default record is the speech balloon it always was.
 */
struct CornerParams
{
    qreal radius = 45.0;

    template <class V>
    static void fields(V&& v)
    {
        v("cornerRadius", &CornerParams::radius);
    }
    [[nodiscard]] static const Fields::Section& section();
    [[nodiscard]] bool operator==(const CornerParams& o) const { return Fields::equal(*this, o); }
};

//! A Cloud's lobes, the circles around its edge. The defaults are the thought balloon's.
struct CloudParams
{
    int lobes    = 11;
    int lobeSize = 40;   //!< A lobe's radius, in % of the balloon's shorter half-axis.

    template <class V>
    static void fields(V&& v)
    {
        v("lobes",    &CloudParams::lobes);
        v("lobeSize", &CloudParams::lobeSize);
    }
    [[nodiscard]] static const Fields::Section& section();
    [[nodiscard]] bool operator==(const CloudParams& o) const { return Fields::equal(*this, o); }
};

/**
 * \brief How far a Polygon's edges bow in: an arc between two points, which stay sharp. In, never out, so
 *        the balloon keeps to its nodes' bounds.
 */
struct BowParams
{
    int depth        = 0;   //!< In % of each edge's length; 0 is straight.
    /**
     * How much each edge's bow varies, deeper or flatter, in %. Dealt from the edge's two nodes, so moving a
     * node deals its two edges again and nothing else; no seed, so a preset carries the same arcs.
     */
    int irregularity = 0;

    template <class V>
    static void fields(V&& v)
    {
        v("bow",             &BowParams::depth);
        v("bowIrregularity", &BowParams::irregularity);
    }
    [[nodiscard]] static const Fields::Section& section();
    [[nodiscard]] bool operator==(const BowParams& o) const { return Fields::equal(*this, o); }
};

/**
 * \brief How an edge's teeth are laid along the outline, whichever edge: every family takes them, and a
 *        template does not set them, so picking a tile keeps them. The teeth go **in** from the outline, so the
 *        balloon keeps its bounds and its text area gives up the depth. In balloon pixels.
 */
struct EdgeParams
{
    qreal pitch        = 16.0;   //!< One tooth every this many pixels along the edge.
    qreal depth        = 6.0;    //!< How far in a tooth's valley reaches.
    int   irregularity = 0;      //!< How much each tooth's depth and place vary, in %.

    template <class V>
    static void fields(V&& v)
    {
        v("edgePitch",        &EdgeParams::pitch);
        v("edgeDepth",        &EdgeParams::depth);
        v("edgeIrregularity", &EdgeParams::irregularity);
    }
    [[nodiscard]] static const Fields::Section& section();
    [[nodiscard]] bool operator==(const EdgeParams& o) const { return Fields::equal(*this, o); }
};
/** @} */

/**
 * \brief Which silhouette is drawn behind the text, and its parameters.
 *
 * A kind is a **family** — a way of describing an outline — and its parameters say which one. The
 * ready-made outlines the picker offers (a speech balloon, a caption box) are **templates**: values of
 * this group (shapes.hpp). A family's numbers are sections (CornerParams…), kept whatever the kind.
 */
struct ShapeProperties
{
    /**
     * \brief The silhouette drawn behind the text. `None` is the Text tool: letters with no balloon.
     *
     * Persisted by name, not by number (see shapeName()), so the order here is free. The strip editor
     * is unreleased, so a kind may still be renamed or merged: a file naming one that is gone
     * loads as a Box.
     */
    enum class Kind { None, Box, Ellipse, Polygon, Cloud, Curve };

    //! The enum's last value. **Adding a shape moves this** — it is how the unit tests know a shape
    //! exists that the picker (shapeOrder()) does not offer, and which therefore would not load either.
    static constexpr Kind k_lastKind = Kind::Curve;

    /**
     * \brief What is done to the outline's edge: pushed in along it in a pattern, any
     * family alike, before the tails are united into it so a tail stays smooth. The table is in
     * shapes.cpp; persisted by name (edgeName()).
     */
    enum class Edge { None, Zigzag, Wave, Scallop, Spike };
    //! The enum's last value. **Adding an edge moves this**: edgeFromName() reads up to it.
    static constexpr Edge k_lastEdge = Edge::Spike;

    //! Decimal places a node is saved with in the SVG; a template's nodes are rounded to them, so a
    //! placed template reads back as itself. 4 is a fifth of a pixel on a 2000 px balloon.
    static constexpr int k_nodeDecimals = 4;

    Kind         kind = Kind::Box;
    CornerParams corner;
    /**
     * A Polygon's or a Curve's nodes, in order (ShapeNode), relative to the balloon so a template fits
     * any size. Fewer than three draw the box's own rectangle.
     */
    QList<ShapeNode> nodes;
    CloudParams  cloud;
    BowParams    bow;
    //! The edge laid over whichever outline, and its teeth; read only while `edge` is not None.
    Edge         edge = Edge::None;
    EdgeParams   teeth;

    //! The families' sections, in the order the Shape panel shows them; the edge's is apart, under its own combo.
    template <class V>
    static void sections(V&& v)
    {
        v(&ShapeProperties::corner);
        v(&ShapeProperties::cloud);
        v(&ShapeProperties::bow);
    }

    //! The fields, with the keys they are saved under (fields.hpp).
    template <class V>
    static void fields(V&& v)
    {
        v("shape", &ShapeProperties::kind);
        Fields::nest(v, &ShapeProperties::corner);
        v("nodes", &ShapeProperties::nodes);
        Fields::nest(v, &ShapeProperties::cloud);
        Fields::nest(v, &ShapeProperties::bow);
        v("edge",  &ShapeProperties::edge);
        Fields::nest(v, &ShapeProperties::teeth);
    }

    [[nodiscard]] static ShapeProperties from(const ObjectRecord& a);
    void applyTo(ObjectRecord& a) const;

    [[nodiscard]] bool operator==(const ShapeProperties& o) const { return Fields::equal(*this, o); }
    [[nodiscard]] bool operator!=(const ShapeProperties& o) const { return !(*this == o); }
};

/**
 * \brief Rough, a texture: turbulence displacing the outline, then a blur. The defaults are the old Marker's
 *        numbers; the Ink look is a built-in preset (Geometry::inkRough()). Lengths in balloon pixels, so the
 *        wobble scales with the balloon, not with the output's resolution.
 */
struct RoughParams
{
    qreal frequency    = 0.03;   //!< feTurbulence's baseFrequency: higher is a finer grain.
    int   octaves      = 3;      //!< How many finer wobbles ride on the first.
    qreal displacement = 6.0;    //!< How far the outline wanders (feDisplacementMap's scale).
    qreal blur         = 0.0;    //!< The bleed after it (feGaussianBlur's deviation); 0 for none.
    //! Whether the fill's paint wobbles too. Off, only the outline and the fill's edge do: a pattern stays
    //! on its grid, laid from the box's corner as everywhere else.
    bool  fill         = false;

    template <class V>
    static void fields(V&& v)
    {
        v("roughFrequency",    &RoughParams::frequency);
        v("roughOctaves",      &RoughParams::octaves);
        v("roughDisplacement", &RoughParams::displacement);
        v("roughBlur",         &RoughParams::blur);
        v("roughFill",         &RoughParams::fill);
    }
    [[nodiscard]] static const Fields::Section& section();
    [[nodiscard]] bool operator==(const RoughParams& o) const { return Fields::equal(*this, o); }
};

/**
 * \brief What is done to the balloon as it is rasterised: its texture, an SVG filter on the
 *        balloon's group — its silhouette, its line's strokes, its shadow — never on the words.
 *
 * Its own group, and its own section of the panel, apart from the line: the two are independent,
 * so a flash or a brush can be rough.
 */
struct TextureProperties
{
    /**
     * \brief A texture is an SVG filter, so librsvg applies it and Qt cannot: that is the whole reason
     * such a balloon is previewed through the library instead of being drawn locally — an effect only the
     * committed output could show would be an effect nobody could author. None is a true no-op: no filter
     * is emitted at all.
     *
     * Persisted by name — see textureName().
     */
    enum class Kind { None, Rough };
    //! The enum's last value. **Adding a texture moves this**: textureFromName() reads up to it.
    static constexpr Kind k_lastKind = Kind::Rough;

    Kind        kind = Kind::None;
    RoughParams rough;

    //! The sections, in the order the Texture panel shows them.
    template <class V>
    static void sections(V&& v)
    {
        v(&TextureProperties::rough);
    }

    //! The fields, with the keys they are saved under (fields.hpp).
    template <class V>
    static void fields(V&& v)
    {
        v("texture", &TextureProperties::kind);
        sections([&](auto section) { Fields::nest(v, section); });
    }

    [[nodiscard]] static TextureProperties from(const ObjectRecord& a);
    void applyTo(ObjectRecord& a) const;

    [[nodiscard]] bool operator==(const TextureProperties& o) const { return Fields::equal(*this, o); }
    [[nodiscard]] bool operator!=(const TextureProperties& o) const { return !(*this == o); }
};

/**
 * @name A line's sections: the numbers each kind of line reads (styles.cpp), one struct each, held by
 * StyleProperties whatever its line, so switching back finds them as they were. Their rows are their
 * section() (styles.cpp); a kind's table entry lists the sections it reads.
 * @{
 */

/**
 * \brief The flash: narrow triangles ringing the silhouette, outwards and inwards. Read by Flash and
 *        Explosion. Lengths in balloon pixels; jitter in % of the pitch or the length.
 */
struct FlashParams
{
    //! `coreStretch`'s "follow the shape" value.
    static constexpr int k_autoStretch = -1;

    qreal outerPitch        = 5.0;   //!< One outer stroke every this many pixels along the edge.
    qreal innerPitch        = 7.0;   //!< Likewise, inside.
    qreal outerLength       = 18.0;  //!< How far an outer stroke reaches out from the edge.
    qreal innerLength       = 10.0;  //!< How far an inner stroke reaches in.
    qreal outerBase         = 4.0;   //!< An outer stroke's width where it leaves the edge.
    qreal innerBase         = 3.0;   //!< Likewise, inside.
    int   outerPitchJitter  = 60;    //!< How much each outer stroke's place varies.
    int   innerPitchJitter  = 60;
    int   outerLengthJitter = 50;    //!< How much each outer stroke's length varies.
    int   innerLengthJitter = 50;
    /**
     * What the strokes point away from, in %: 0 is the box's centre; more stretches it into a line
     * along the longer axis, which squares the strokes up with a long straight side. **k_autoStretch**
     * (the default) takes the shape's own value (ShapeInfo::flashStretch) instead, so it follows the
     * shape; a value set by hand stays through any change of shape.
     */
    int   coreStretch       = k_autoStretch;

    template <class V>
    static void fields(V&& v)
    {
        v("flashOuterPitch",        &FlashParams::outerPitch);
        v("flashInnerPitch",        &FlashParams::innerPitch);
        v("flashOuterLength",       &FlashParams::outerLength);
        v("flashInnerLength",       &FlashParams::innerLength);
        v("flashOuterBase",         &FlashParams::outerBase);
        v("flashInnerBase",         &FlashParams::innerBase);
        v("flashOuterPitchJitter",  &FlashParams::outerPitchJitter);
        v("flashInnerPitchJitter",  &FlashParams::innerPitchJitter);
        v("flashOuterLengthJitter", &FlashParams::outerLengthJitter);
        v("flashInnerLengthJitter", &FlashParams::innerLengthJitter);
        v("flashCoreStretch",       &FlashParams::coreStretch);
    }
    [[nodiscard]] static const Fields::Section& section();
    [[nodiscard]] bool operator==(const FlashParams& o) const { return Fields::equal(*this, o); }
};

/**
 * \brief The brush: a few strokes laid along the edge, each tapering to a point at both ends and swelling
 *        and wandering along the way. Its width is the outline's (`strokeWidth`). In % of a stroke's share
 *        of the edge, of its length, of its width, or in balloon pixels.
 */
struct BrushParams
{
    int   strokes = 3;     //!< How many strokes go round each edge.
    int   overlap = 20;    //!< How far each runs past where the next begins, in % of its share.
    int   taper   = 30;    //!< How much of a stroke, at each end, narrows to its point, in %.
    int   swell   = 40;    //!< How much a stroke's width varies along it, in %.
    qreal drift   = 1.5;   //!< How far a stroke wanders off the edge, in balloon pixels.

    template <class V>
    static void fields(V&& v)
    {
        v("brushStrokes", &BrushParams::strokes);
        v("brushOverlap", &BrushParams::overlap);
        v("brushTaper",   &BrushParams::taper);
        v("brushSwell",   &BrushParams::swell);
        v("brushDrift",   &BrushParams::drift);
    }
    [[nodiscard]] static const Fields::Section& section();
    [[nodiscard]] bool operator==(const BrushParams& o) const { return Fields::equal(*this, o); }
};

/**
 * \brief The dry brush: a thin whole core along the edge, and hairs either side of it that thin out away
 *        from it. Read by Drybrush and Explosion. Its width is the outline's: `core` of it the core, the rest
 *        shared among the hairs' lanes.
 */
struct DrybrushParams
{
    int core     = 20;   //!< The whole thin line in the middle, in % of the width.
    int bristles = 12;   //!< How many hairs lie across the line, half each side of the core.
    int dryness  = 20;   //!< How much of the hairs by the core are gaps, in %: 100 leaves the core alone.

    template <class V>
    static void fields(V&& v)
    {
        v("drybrushCore",     &DrybrushParams::core);
        v("drybrushBristles", &DrybrushParams::bristles);
        v("drybrushDryness",  &DrybrushParams::dryness);
    }
    [[nodiscard]] static const Fields::Section& section();
    [[nodiscard]] bool operator==(const DrybrushParams& o) const { return Fields::equal(*this, o); }
};

/**
 * \brief The dashed line, a whisper: the edge cut into dashes as wide as the outline, a whole number of
 *        them to each lap, so the gap is stretched or squeezed a little to close it. In balloon pixels.
 */
struct DashParams
{
    qreal length = 12.0;   //!< How long a dash is along the edge.
    qreal gap    = 8.0;    //!< The gap after it, before the lap is fitted; 0 draws a whole line.

    template <class V>
    static void fields(V&& v)
    {
        v("dashLength", &DashParams::length);
        v("dashGap",    &DashParams::gap);
    }
    [[nodiscard]] static const Fields::Section& section();
    [[nodiscard]] bool operator==(const DashParams& o) const { return Fields::equal(*this, o); }
};

//! The multi-line: the outline, and lines inside it following the silhouette, tails included. In balloon pixels.
struct MultiParams
{
    int   lines = 2;     //!< How many lines, the outline among them.
    qreal gap   = 4.0;   //!< The space between one line and the next.
    qreal width = 3.0;   //!< Each inner line's width; the outline's is its own.

    template <class V>
    static void fields(V&& v)
    {
        v("multiLines", &MultiParams::lines);
        v("multiGap",   &MultiParams::gap);
        v("multiWidth", &MultiParams::width);
    }
    [[nodiscard]] static const Fields::Section& section();
    [[nodiscard]] bool operator==(const MultiParams& o) const { return Fields::equal(*this, o); }
};

/**
 * \brief The sketch: a thin whole outline, and many short pen strokes over it, all drawn the same way
 *        round, each straighter than the curve it lies on, landing with a short taper and lifting off with a
 *        long one, as a g-pen's pressure does. The defaults are the author's, tuned by eye on a speech
 *        balloon at a 5 px line.
 */
struct SketchParams
{
    qreal length      = 72.0;   //!< How long a stroke is, in balloon pixels, a lap at most; each varies about it.
    int   layers      = 6;      //!< How many strokes lie over any place of the outline.
    int   stiffness   = 30;     //!< How straight a stroke stays, in %: 0 follows the outline, 100 a straight line.
    qreal spread      = 4.5;    //!< How far in or out a stroke lies from the outline, in balloon pixels.
    int   entry       = 20;     //!< How much of a stroke, at its start, the pen lands over, in %.
    int   exit        = 100;    //!< How much of a stroke, at its end, the pen lifts off over, in %.
    int   core        = 25;     //!< The whole outline under the strokes, in % of the line's width.
    int   strokeWidth = 50;     //!< A stroke's width, in % of the line's.
    bool  clockwise   = true;   //!< Which way round, on screen, the pen goes: where it lands and lifts.
    int   blots       = 0;      //!< How many ink blots, spots and short strokes, lie along the line, in %.

    template <class V>
    static void fields(V&& v)
    {
        v("sketchLength",      &SketchParams::length);
        v("sketchLayers",      &SketchParams::layers);
        v("sketchStiffness",   &SketchParams::stiffness);
        v("sketchSpread",      &SketchParams::spread);
        v("sketchEntry",       &SketchParams::entry);
        v("sketchExit",        &SketchParams::exit);
        v("sketchCore",        &SketchParams::core);
        v("sketchStrokeWidth", &SketchParams::strokeWidth);
        v("sketchClockwise",   &SketchParams::clockwise);
        v("sketchBlots",       &SketchParams::blots);
    }
    [[nodiscard]] static const Fields::Section& section();
    [[nodiscard]] bool operator==(const SketchParams& o) const { return Fields::equal(*this, o); }
};

/**
 * \brief Dry ink: the outline eaten by the paper, a broken print — noise thresholded into holes in it, but
 *        for its middle, kept whole so the fill stays closed off. The one line that is a filter, not geometry
 *        (dryInkFilter() in styles.cpp), so the library previews it; a texture still goes over it.
 */
struct DryInkParams
{
    qreal grain = 0.15;   //!< The noise's baseFrequency: higher is finer specks.
    int   wear  = 35;     //!< How much of the outline is eaten, in %.
    int   core  = 25;     //!< Its middle kept whole, in % of the line's width.

    template <class V>
    static void fields(V&& v)
    {
        v("dryInkGrain", &DryInkParams::grain);
        v("dryInkWear",  &DryInkParams::wear);
        v("dryInkCore",  &DryInkParams::core);
    }
    [[nodiscard]] static const Fields::Section& section();
    [[nodiscard]] bool operator==(const DryInkParams& o) const { return Fields::equal(*this, o); }
};
/** @} */

/**
 * \brief The line drawn round the balloon: what kind, in what colour, how wide, and each kind's numbers.
 *
 * Clean is a solid outline; the others are strokes of their own or a filter. The colour and the width are the
 * line's, so a brush's width sits with the brush; the panel's Line section is this group. Each kind's numbers
 * are a section (FlashParams…), kept for every kind whatever the line.
 *
 * The **seed is not in here**, deliberately. It is per balloon and set once at placement, so a preset
 * that carried it would give a whole chapter one repeated wobble — and a group's applyTo() writes the
 * whole group, which would do exactly that. It stays a bare field with no editor.
 */
struct StyleProperties
{
    /**
     * \brief How the edge is drawn. Clean is the solid outline; Flash, Brush, Drybrush, Dashed, Multi and Sketch are strokes
     * of their own, plain geometry, so Qt draws them as it draws a clean balloon. The table is in styles.cpp.
     *
     * Persisted by name — see lineName(). The strip editor is unreleased, so the order is
     * free.
     */
    enum class Line { Clean, Flash, Brush, Drybrush, Dashed, Multi, Sketch, DryInk, Explosion };
    //! The enum's last value. **Adding a line moves this**: lineFromName() reads up to it.
    static constexpr Line k_lastLine = Line::Explosion;

    Line  line = Line::Clean;
    //! The outline's paint, and a flash's or a brush's strokes': near black, a pattern's lines white over it.
    Paint stroke{QColor(20, 20, 20), QColor(255, 255, 255)};
    int   strokeWidth = 5;      //!< The outline's width; a brush's too. 0 draws no line.

    FlashParams    flash;
    BrushParams    brush;
    DrybrushParams drybrush;
    DashParams     dash;
    MultiParams    multi;
    SketchParams   sketch;
    DryInkParams   dryInk;

    //! The sections, in the order their fields are listed (and a panel shows them).
    template <class V>
    static void sections(V&& v)
    {
        v(&StyleProperties::flash);
        v(&StyleProperties::brush);
        v(&StyleProperties::drybrush);
        v(&StyleProperties::dash);
        v(&StyleProperties::multi);
        v(&StyleProperties::sketch);
        v(&StyleProperties::dryInk);
    }

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
        sections([&](auto section) { Fields::nest(v, section); });
    }

    [[nodiscard]] static StyleProperties from(const ObjectRecord& a);
    void applyTo(ObjectRecord& a) const;

    [[nodiscard]] bool operator==(const StyleProperties& o) const { return Fields::equal(*this, o); }
    [[nodiscard]] bool operator!=(const StyleProperties& o) const { return !(*this == o); }
};

/**
 * \brief What is drawn about the balloon, beside its outline and its fill: a shadow so far.
 *
 * A group of its own, so applying a look's line does not apply its shadow by accident, and an effect
 * added later (a glow) is a field here. Its editor is laid out in the Style section, beside the line and
 * the texture, so the artist sees one place for how the balloon is drawn.
 */
struct EffectProperties
{
    /**
     * @name The shadow: the whole silhouette, tails and all, grown, moved and filled under the balloon
     * (Shadow). Blurred, the balloon is previewed through the library.
     * @{
     */
    Shadow shadow;
    //! Its paint: a colour, whose alpha is how dark it is, or a halftone of it.
    Paint  shadowFill{QColor(0, 0, 0, 110), QColor(0, 0, 0)};
    /** @} */

    //! The fields, with the keys they are saved under (fields.hpp).
    template <class V>
    static void fields(V&& v)
    {
        static constexpr const char* k_shadow[] = {"shadow", "shadowSpread", "shadowX", "shadowY", "shadowSoftness"};
        Shadow::fieldsAs(v, &EffectProperties::shadow, k_shadow);
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
 * \brief What the balloon says, and how it is set.
 *
 * Content and typography in one group because one widget edits both, and because the split that
 * matters is elsewhere: a **preset** carries the typography and never the content — *everything a
 * balloon is, minus everything it says*. The letters' paint is here rather than with the balloon's fill
 * and stroke for the same reason the colour tool has only two swatches: editing a frame is not editing
 * its lettering.
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
    //! The letters' paint: a colour, or a halftone of a second colour over it (dots, lines, a grid).
    Paint   fill{QColor(20, 20, 20), QColor(255, 255, 255)};
    //! Line pitch as a percentage of the font's own — for faces that set their lines too far apart.
    int     lineSpacing = 100;
    /**
     * @name How far the lettering keeps from the balloon's outline, in strip pixels, on top of the stroke:
     *       an inset of the shape's own text area.
     *
     * The lettering's, not the balloon's: it is how the words sit, and it is set beside the rest of how
     * they are set. **Only a balloon uses them**: a text object has no outline to keep away from, so the
     * values are carried and ignored, and the editor does not offer them there. **May be negative**: the
     * text area then reaches past the outline, which an impact balloon with lettering bursting out of it
     * needs. 8 is the inset every balloon had before it was a property.
     *
     * The words start centred, and each line grows the block up and down alike until its top meets the top
     * padding; from there it grows only downwards, out of the balloon, and is never cut there
     * (Geometry::textOutline()). So the top padding is mirrored at the bottom for the centring and the fit.
     * @{
     */
    int     paddingSide = 8;   //!< Left and right, one value.
    int     paddingTop  = 8;   //!< The top, which the block stops at.
    /** @} */
    /**
     * @name The outline: a band round each letter, outside it, so the letterform keeps the
     * weight its typeface gives it — the glyphs stroked at twice the width, then filled over. Painted
     * with a colour, or a halftone of a second over it. In strip pixels; 0 is none, the default.
     * @{
     */
    Paint   outline;
    qreal   outlineWidth = 0.0;
    /** @} */
    /**
     * @name The shadow: the letters with their outline, grown, moved and filled under them (Shadow), off by
     * default. Solid with no offset it reads as a second outline, soft with no offset as a glow.
     * @{
     */
    Shadow  shadow;
    Paint   shadowFill{QColor(0, 0, 0, 110), QColor(0, 0, 0)};
    /** @} */

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
        v("paddingSide", &TextProperties::paddingSide);
        v("paddingTop",  &TextProperties::paddingTop);
        // The colour keeps the key it had as a plain colour; a pattern does not fade here.
        static constexpr const char* k_fill[] = {
            "textColour", "textKind", "textSecond", "textSpacing", "textSize", "textAngle",
            nullptr,      nullptr,    nullptr,      nullptr,       nullptr,    nullptr,
            nullptr,      nullptr,    nullptr,      nullptr,       nullptr,    nullptr};
        Paint::fieldsAs(v, &TextProperties::fill, k_fill);
        static constexpr const char* k_outline[] = {
            "textOutline", "textOutlineKind", "textOutlineSecond", "textOutlineSpacing", "textOutlineSize",
            "textOutlineAngle",
            nullptr,       nullptr,           nullptr,             nullptr,              nullptr,
            nullptr,       nullptr,           nullptr,             nullptr,              nullptr,
            nullptr,       nullptr};   // an outline does not fade
        Paint::fieldsAs(v, &TextProperties::outline, k_outline);
        v("textOutlineWidth", &TextProperties::outlineWidth);
        static constexpr const char* k_shadow[] = {"textShadow", "textShadowSpread", "textShadowX", "textShadowY",
                                                   "textShadowSoftness"};
        Shadow::fieldsAs(v, &TextProperties::shadow, k_shadow);
        // As the balloon's: its pattern is its colour in marks on nothing, and it fades by its softness.
        static constexpr const char* k_shadowFill[] = {
            "textShadowColour", "textShadowKind", nullptr, "textShadowSpacing", "textShadowSize",
            "textShadowAngle",
            nullptr,            nullptr,          nullptr, nullptr,             nullptr,
            nullptr,            nullptr,          nullptr, nullptr,             nullptr,
            nullptr,            nullptr};
        Paint::fieldsAs(v, &TextProperties::shadowFill, k_shadowFill);
    }

    [[nodiscard]] static TextProperties from(const ObjectRecord& a);
    void applyTo(ObjectRecord& a) const;

    [[nodiscard]] bool operator==(const TextProperties& o) const { return Fields::equal(*this, o); }
    [[nodiscard]] bool operator!=(const TextProperties& o) const { return !(*this == o); }
};

/**
 * \brief Every tail on the balloon.
 *
 * The whole list is one group today because a tail is not yet addressable on its own: there is no way
 * to say *which* tail, which is why one can be added and never removed. When tails become objects in
 * their own right this splits into a group per tail, and the list stops being a property at all.
 */
struct TailsProperties
{
    QList<Tail> items;   //!< Empty = no tail. More than one = one sound, several speakers.
    /**
     * The look last picked for any of the balloon's tails (TailProperties::applyTo()), which a tail added
     * to it takes: pick Circles on one, and the next is a chain too; pick Plain on any, and it is plain.
     */
    TailLook    nextLook;

    [[nodiscard]] static TailsProperties from(const ObjectRecord& a);
    void applyTo(ObjectRecord& a) const;

    [[nodiscard]] bool operator==(const TailsProperties& o) const
    {
        return items == o.items && nextLook == o.nextLook;
    }
    [[nodiscard]] bool operator!=(const TailsProperties& o) const { return !(*this == o); }
};

/**
 * \brief One tail of a balloon — the group a selected tail is edited through.
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
     * \brief Reads the tail at \p index out of \p a, or an empty tail if the index is out of range.
     * \param a The balloon to read from.
     * \param index The tail to read, 0..n-1. Out of range = no tail, and applyTo() will write nothing.
     * \return The tail at \p index, or an empty tail if the index is out of range.
     * @see applyTo()
     */
    [[nodiscard]] static TailProperties from(const ObjectRecord& a, int index);
    
    /**
     * \brief Writes the tail at \p index into \p a, or does nothing if the index is out of range.
     *
     * A tail whose look this changes also makes it the balloon's `nextLook`, the one a tail added later
     * takes; editing only its width or bend leaves that as it was.
     *
     * \param a The balloon to write to.
     * @see from()
     */
    void applyTo(ObjectRecord& a) const;

    /**
     * \brief Compares two tail properties for equality.
     * \param o The other tail properties to compare with.
     * \return True if the tail properties are equal, false otherwise.
     */
    [[nodiscard]] bool operator==(const TailProperties& o) const { return index == o.index && tail == o.tail; }

    /**
     * \brief Compares two tail properties for inequality.
     * \param o The other tail properties to compare with.
     * \return True if the tail properties are not equal, false otherwise.
     */
    [[nodiscard]] bool operator!=(const TailProperties& o) const { return !(*this == o); }
};

#endif // PROPERTYGROUPS_HPP
