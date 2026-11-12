#include "renderlist.hpp"

#include "geometry.hpp"
#include "styles.hpp"

namespace Render {

namespace {

//! The ids a balloon's definitions are written under; the lettering's are its own, beside them.
const QString k_textureId    = QStringLiteral("pm-style");
const QString k_outlineId    = QStringLiteral("pm-dryink");
const QString k_fillId       = QStringLiteral("pm-fill");
const QString k_strokeId     = QStringLiteral("pm-stroke");
const QString k_shadowId     = QStringLiteral("pm-shadow");
const QString k_shadowFillId = QStringLiteral("pm-shadow-fill");
const QString k_roughFillId  = QStringLiteral("pm-rough-fill");
const QString k_fillAreaId   = QStringLiteral("pm-fill-area");
const QString k_textFillId       = QStringLiteral("pm-text-fill");
const QString k_textOutlineId    = QStringLiteral("pm-text-outline");
const QString k_textShadowId     = QStringLiteral("pm-text-shadow");
const QString k_textShadowFillId = QStringLiteral("pm-text-shadow-fill");

//! A soft shadow's region: wider than a displacement's, as a blur spreads past the shape on every side.
const QString k_blurRegion = QStringLiteral("x=\"-50%\" y=\"-50%\" width=\"200%\" height=\"200%\"");

//! @p list gains @p p's pattern as @p id, unless it is solid and has none.
void addPattern(List& list, const Paint& p, const QString& id)
{
    if (p.kind != Paint::Kind::Solid)
        list.defs.append({id, p, std::nullopt});
}

//! @p list gains shadow @p s's blur as @p id, if it is soft: a hard one, a fading halftone or none have none.
void addBlur(List& list, const Shadow& s, const Paint& fill, const QString& id)
{
    if (!Geometry::blursShadow(s, fill))
        return;
    Filter f;
    f.region = k_blurRegion;
    f.primitives.append({QStringLiteral("feGaussianBlur"), {{QStringLiteral("stdDeviation"), number(s.softness)}}, {}});
    list.defs.append({id, std::nullopt, f});
}

/**
 * \brief The Paint shadow @p s, drawn as @p paths, is painted with, and the opacity of the group it is in:
 *        shadowPaint() for one that does not grow, shadowLayerPaint() for one that does.
 */
Paint shadowPaintFor(const Shadow& s, const Paint& fill, const Geometry::ShadowPaths& paths, qreal* opacity)
{
    if (paths.grow > 0.0)
        return Geometry::shadowLayerPaint(s, fill, opacity);
    if (opacity)
        *opacity = 1.0;
    return Geometry::shadowPaint(s, fill);
}

/**
 * \brief Shadow @p s, drawn as @p paths (Geometry::shadowOf()): the balloon's and the letters' alike. One that
 *        does not grow is its path (a fading one its marks); one that does is a layer, given the shadow's alpha
 *        once, of its body and a round stroke `2 × grow` wide over it, both opaque, so they do not darken where
 *        they overlap. Blurred by @p blurId, patterned by @p fillId (their definitions).
 */
QList<Item> shadowItems(const Shadow& s, const Paint& fill, const Geometry::ShadowPaths& paths, const QString& fillId,
                        const QString& blurId)
{
    if (paths.isEmpty())
        return {};
    qreal         opacity = 1.0;
    const Paint   p       = shadowPaintFor(s, fill, paths, &opacity);
    const QString blur    = Geometry::blursShadow(s, fill) ? blurId : QString();
    Item body;
    body.path = paths.body;
    body.fill = Ink{p, fillId};
    if (paths.grow <= 0.0) {
        body.marks  = Geometry::fadesShadow(s, fill);
        body.filter = blur;
        return {body};
    }
    Item band;
    band.path        = paths.body;
    band.stroke      = Ink{p, fillId};
    band.strokeWidth = 2.0 * paths.grow;
    band.roundCap    = true;
    Item layer;
    layer.kind     = Item::Kind::Group;
    layer.opacity  = opacity;
    layer.layer    = true;
    layer.filter   = blur;
    layer.children = {body, band};
    return {layer};
}

//! An outline of @p width along @p path, in @p ink, filled with nothing.
Item outline(const QPainterPath& path, const Ink& ink, qreal width)
{
    Item i;
    i.path        = path;
    i.stroke      = ink;
    i.strokeWidth = width;
    return i;
}

} // namespace

QString number(qreal v, int decimals)
{
    QString s = QString::number(v, 'f', decimals);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    return s == QLatin1String("-0") ? QStringLiteral("0") : s;
}

Ink colourInk(const QColor& colour)
{
    Paint p;
    p.kind   = Paint::Kind::Solid;
    p.colour = colour;
    return Ink{p, QString()};
}

List balloon(const ObjectRecord& a, const QPainterPath& silhouette)
{
    List out;
    if (a.isArtwork())
        return out;

    const Geometry::TextureInfo& texture = Geometry::textureInfo(a.texture.kind);
    const Geometry::LineInfo&    line    = Geometry::lineInfo(a.style.line);
    const Geometry::ShadowPaths  shadow  = Geometry::shadowOf(a, silhouette);

    // The definitions, before what refers to them, as SVG requires.
    if (texture.filter)
        out.defs.append({k_textureId, std::nullopt, texture.filter(a.texture, a.styleSeed)});
    if (line.outlineFilter)
        out.defs.append({k_outlineId, std::nullopt, line.outlineFilter(a.style, a.styleSeed)});
    if (!Geometry::fades(a.skin.fill))
        addPattern(out, a.skin.fill, k_fillId);
    addPattern(out, a.style.stroke, k_strokeId);
    addBlur(out, a.effects.shadow, a.effects.shadowFill, k_shadowId);
    if (a.effects.shadow.on)
        addPattern(out, shadowPaintFor(a.effects.shadow, a.effects.shadowFill, shadow, nullptr), k_shadowFillId);

    if (silhouette.isEmpty())
        return out;

    const QString textureId  = texture.filter ? k_textureId : QString();
    const Ink     fillInk{a.skin.fill, k_fillId};
    const Ink     strokeInk{a.style.stroke, k_strokeId};
    const bool    outlined   = a.style.strokeWidth > 0 && Geometry::hasOutline(a);
    const bool    worn       = line.outlineFilter != nullptr;
    const QPainterPath ink   = Geometry::ink(a, silhouette);
    const QPainterPath tone  = Geometry::fillTone(a, silhouette);
    const QList<Item>  shade = shadowItems(a.effects.shadow, a.effects.shadowFill, shadow, k_shadowFillId, k_shadowId);

    // A worn outline: the outline under its line's filter, then its middle unworn over it, so a hole never
    // opens the fill. A texture's filter, on a group round it, goes over both.
    const auto wornOutline = [&] {
        Item filtered;
        filtered.kind     = Item::Kind::Group;
        filtered.filter   = k_outlineId;
        filtered.children = {outline(silhouette, strokeInk, a.style.strokeWidth)};
        QList<Item> items{filtered};
        const qreal core = a.style.strokeWidth * (line.unworn ? line.unworn(a.style) : 0.0);
        if (core > 0.0) {
            Item middle         = outline(silhouette, strokeInk, core);
            middle.filteredOnly = true;
            items.append(middle);
        }
        return items;
    };
    Item inkItem;
    inkItem.path = ink;
    inkItem.fill = strokeInk;

    // **Rough, in parts.** The filter moves every pixel it is given, so over the whole balloon a fill's
    // pattern wobbles with the outline. Unless the artist asks for that (`rough.fill`), the balloon is drawn in
    // three layers sharing one filter, which stay in register because the noise is a function of where a pixel
    // is, not of which element it belongs to: the shadow, displaced; the fill, not displaced but masked by the
    // displaced silhouette, and drawn past it by the texture's reach so it meets the moved edge; then the
    // line and its strokes, displaced. Each displaced layer is one more turbulence pass.
    if (texture.filter && !a.texture.rough.fill) {
        if (!shade.isEmpty()) {
            Item layer;
            layer.kind     = Item::Kind::Group;
            layer.filter   = k_textureId;
            layer.children = shade;
            out.items.append(layer);
        }
        Item masked;
        masked.kind       = Item::Kind::Group;
        masked.mask       = silhouette;
        masked.maskId     = k_roughFillId;
        masked.maskFilter = k_textureId;
        const qreal reach = texture.reach(a.texture);
        if (tone.isEmpty()) {
            Item fill;
            fill.path  = silhouette;
            fill.fill  = fillInk;
            fill.bleed = reach;
            masked.children.append(fill);
        } else {
            // A fading fill: the ground, then its marks; the mask is their edge.
            Item ground;
            ground.path  = silhouette;
            ground.fill  = colourInk(a.skin.fill.colour);
            ground.bleed = reach;
            Item marks;
            marks.path    = tone;
            marks.fill    = colourInk(a.skin.fill.second);
            marks.nonzero = true;
            marks.marks   = true;
            masked.children = {ground, marks};
        }
        out.items.append(masked);

        if (outlined || !ink.isEmpty()) {
            Item lines;
            lines.kind   = Item::Kind::Group;
            lines.filter = k_textureId;
            if (outlined && worn)
                lines.children += wornOutline();
            else if (outlined)
                lines.children.append(outline(silhouette, strokeInk, a.style.strokeWidth));
            if (!ink.isEmpty())
                lines.children.append(inkItem);
            out.items.append(lines);
        }
        return out;
    }

    Item group;
    group.kind   = Item::Kind::Group;
    group.filter = textureId;
    // The shadow first, under the balloon and inside its group, so a texture roughens it alike. The moved
    // outline itself, not a transform: a transform would carry its pattern along with it, where the painter
    // lays every pattern from the box's corner.
    group.children += shade;
    Item edge;   // the outline, drawn with the fill or after a fading fill's marks
    edge.path = silhouette;
    if (outlined && !worn) {
        edge.stroke      = strokeInk;
        edge.strokeWidth = a.style.strokeWidth;
    }
    if (tone.isEmpty()) {
        edge.fill = fillInk;
        group.children.append(edge);
    } else {
        // A fading fill: the ground, then its marks clipped to the balloon, then the line over both.
        Item ground;
        ground.path = silhouette;
        ground.fill = colourInk(a.skin.fill.colour);
        Item marks;
        marks.path    = tone;
        marks.fill    = colourInk(a.skin.fill.second);
        marks.nonzero = true;
        marks.marks   = true;
        marks.clip    = silhouette;
        marks.clipId  = k_fillAreaId;
        group.children += {ground, marks, edge};
    }
    if (outlined && worn)
        group.children += wornOutline();
    // A line's own strokes, in the line's paint, over the fill and under the words.
    if (!ink.isEmpty())
        group.children.append(inkItem);
    out.items.append(group);
    return out;
}

List lettering(const ObjectRecord& a, const QPainterPath& text, const Geometry::ShadowPaths& shadow)
{
    List out;
    if (text.isEmpty())
        return out;
    const TextProperties& t = a.text;
    addPattern(out, t.fill, k_textFillId);
    if (t.outlineWidth > 0.0)
        addPattern(out, t.outline, k_textOutlineId);
    if (t.shadow.on) {
        addBlur(out, t.shadow, t.shadowFill, k_textShadowId);
        addPattern(out, shadowPaintFor(t.shadow, t.shadowFill, shadow, nullptr), k_textShadowFillId);
    }

    // The shadow first, under the outline and the letters, as the balloon's is.
    out.items += shadowItems(t.shadow, t.shadowFill, shadow, k_textShadowFillId, k_textShadowId);
    if (t.outlineWidth > 0.0) {
        Item edge     = outline(text, Ink{t.outline, k_textOutlineId}, 2.0 * t.outlineWidth);
        edge.roundCap = true;
        out.items.append(edge);
    }
    Item letters;
    letters.path = text;
    letters.fill = Ink{t.fill, k_textFillId};
    out.items.append(letters);
    return out;
}

}  // namespace Render
