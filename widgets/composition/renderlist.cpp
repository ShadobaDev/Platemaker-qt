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

List balloon(const Balloon& b, quint32 seed, const QPainterPath& silhouette)
{
    List out;

    const Geometry::TextureInfo& texture = Geometry::textureInfo(b.style.texture.kind);
    const Geometry::LineInfo&    line    = Geometry::lineInfo(b.style.line.line);
    const Geometry::ShadowPaths  shadow  = Geometry::shadowOf(b, silhouette);

    // The definitions, before what refers to them, as SVG requires.
    if (texture.filter)
        out.defs.append({k_textureId, std::nullopt, texture.filter(b.style.texture, seed)});
    if (line.outlineFilter)
        out.defs.append({k_outlineId, std::nullopt, line.outlineFilter(b.style.line, seed)});
    if (!Geometry::fades(b.style.fill.fill))
        addPattern(out, b.style.fill.fill, k_fillId);
    addPattern(out, b.style.line.stroke, k_strokeId);
    addBlur(out, b.style.effects.shadow, b.style.effects.shadowFill, k_shadowId);
    if (b.style.effects.shadow.on)
        addPattern(out, shadowPaintFor(b.style.effects.shadow, b.style.effects.shadowFill, shadow, nullptr), k_shadowFillId);

    if (silhouette.isEmpty())
        return out;

    const QString textureId  = texture.filter ? k_textureId : QString();
    const Ink     fillInk{b.style.fill.fill, k_fillId};
    const Ink     strokeInk{b.style.line.stroke, k_strokeId};
    const bool    outlined   = b.style.line.strokeWidth > 0 && Geometry::hasOutline(b);
    const bool    worn       = line.outlineFilter != nullptr;
    const QPainterPath ink   = Geometry::ink(b, seed, silhouette);
    const QPainterPath tone  = Geometry::fillTone(b, silhouette);
    const QList<Item>  shade = shadowItems(b.style.effects.shadow, b.style.effects.shadowFill, shadow, k_shadowFillId, k_shadowId);

    // A worn outline: the outline under its line's filter, then its middle unworn over it, so a hole never
    // opens the fill. A texture's filter, on a group round it, goes over both.
    const auto wornOutline = [&] {
        Item filtered;
        filtered.kind     = Item::Kind::Group;
        filtered.filter   = k_outlineId;
        filtered.children = {outline(silhouette, strokeInk, b.style.line.strokeWidth)};
        QList<Item> items{filtered};
        const qreal core = b.style.line.strokeWidth * (line.unworn ? line.unworn(b.style.line) : 0.0);
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
    // In pieces, as marks are, where its strokes allow it and its paint is a colour (marks are filled in one).
    inkItem.marks = line.apart && b.style.line.stroke.kind == Paint::Kind::Solid;

    // **Rough, in parts.** The filter moves every pixel it is given, so over the whole balloon a fill's
    // pattern wobbles with the outline. Unless the artist asks for that (`rough.fill`), the balloon is drawn in
    // three layers sharing one filter, which stay in register because the noise is a function of where a pixel
    // is, not of which element it belongs to: the shadow, displaced; the fill, not displaced but masked by the
    // displaced silhouette, and drawn past it by the texture's reach so it meets the moved edge; then the
    // line and its strokes, displaced. Each displaced layer is one more turbulence pass.
    if (texture.filter && !b.style.texture.rough.fill) {
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
        const qreal reach = texture.reach(b.style.texture);
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
            ground.fill  = colourInk(b.style.fill.fill.colour);
            ground.bleed = reach;
            Item marks;
            marks.path    = tone;
            marks.fill    = colourInk(b.style.fill.fill.second);
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
                lines.children.append(outline(silhouette, strokeInk, b.style.line.strokeWidth));
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
        edge.strokeWidth = b.style.line.strokeWidth;
    }
    if (tone.isEmpty()) {
        edge.fill = fillInk;
        group.children.append(edge);
    } else {
        // A fading fill: the ground, then its marks clipped to the balloon, then the line over both.
        Item ground;
        ground.path = silhouette;
        ground.fill = colourInk(b.style.fill.fill.colour);
        Item marks;
        marks.path    = tone;
        marks.fill    = colourInk(b.style.fill.fill.second);
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

namespace {

//! @p base for the words at @p index: the first words' as it is, the n-th's with `-n`, so the ids of one file differ.
QString wordsId(const QString& base, qsizetype index)
{
    return index == 0 ? base : base + QLatin1Char('-') + QString::number(index + 1);
}

//! The id of run @p run's fill in the words whose fill is @p fillId: `-r` and the run's place, from 1.
QString runFillId(const QString& fillId, int run)
{
    return fillId + QStringLiteral("-r") + QString::number(run + 1);
}

/**
 * \brief @p w's lettering (lettering() of a composition), its ids those of the words at @p index; @p fills, when not
 *        empty, the letters by the fill they wear (Geometry::textFills()).
 */
List wordsLettering(const Words& w, const QPainterPath& text, const Geometry::TextFills& fills,
                    const Geometry::ShadowPaths& shadow, qsizetype index)
{
    List out;
    if (text.isEmpty())
        return out;
    const TextProperties& t          = w.text;
    const QString         fillId     = wordsId(k_textFillId, index);
    const QString         outlineId  = wordsId(k_textOutlineId, index);
    const QString         shadowId   = wordsId(k_textShadowId, index);
    const QString         shadowFill = wordsId(k_textShadowFillId, index);
    addPattern(out, t.fill, fillId);
    if (t.outlineWidth > 0.0)
        addPattern(out, t.outline, outlineId);
    if (t.shadow.on) {
        addBlur(out, t.shadow, t.shadowFill, shadowId);
        addPattern(out, shadowPaintFor(t.shadow, t.shadowFill, shadow, nullptr), shadowFill);
    }

    // The shadow first, under the outline and the letters, as the balloon's is.
    out.items += shadowItems(t.shadow, t.shadowFill, shadow, shadowFill, shadowId);
    if (t.outlineWidth > 0.0) {
        Item edge     = outline(text, Ink{t.outline, outlineId}, 2.0 * t.outlineWidth);
        edge.roundCap = true;
        out.items.append(edge);
    }
    if (fills.isEmpty()) {
        Item letters;
        letters.path = text;
        letters.fill = Ink{t.fill, fillId};
        out.items.append(letters);
        return out;
    }
    // Letters of another fill are a path of their own, under a pattern of their own.
    for (auto it = fills.cbegin(); it != fills.cend(); ++it) {
        if (it.value().isEmpty())
            continue;
        const bool    own   = it.key() < 0 || it.key() >= t.runs.size() || !t.runs.at(it.key()).fill;
        const Paint&  paint = own ? t.fill : *t.runs.at(it.key()).fill;
        const QString id    = own ? fillId : runFillId(fillId, it.key());
        if (!own)
            addPattern(out, paint, id);
        Item letters;
        letters.path = it.value();
        letters.fill = Ink{paint, id};
        out.items.append(letters);
    }
    return out;
}

} // namespace

List lettering(const Composition& c, const QList<QPainterPath>& texts, const QList<QMap<int, QPainterPath>>& fills)
{
    List out;
    for (qsizetype i = 0; i < c.words.size() && i < texts.size(); ++i) {
        const List one = wordsLettering(c.words[i], texts[i], i < fills.size() ? fills[i] : Geometry::TextFills(),
                                        Geometry::textShadowOf(c.words[i], texts[i]), i);
        out.defs += one.defs;
        out.items += one.items;
    }
    return out;
}

}  // namespace Render
