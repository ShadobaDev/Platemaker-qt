#include "recordsvg.hpp"

#include "geometry.hpp"
#include "styles.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QPainterPath>
#include <QRectF>
#include <QStringList>
#include <QPointF>
#include <QXmlStreamReader>

#include <cmath>

namespace Svg {

namespace {

//! Decimal places for path coordinates. Two is well under a rendered pixel and keeps files small.
constexpr int k_precision = 2;

QString num(qreal v, int precision = k_precision)
{
    // 'f' then trim, rather than 'g': 'g' switches to scientific notation for small values and SVG has
    // no such syntax, so a coordinate like 1e-05 would silently become an invalid path.
    QString s = QString::number(v, 'f', precision);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    return s == QLatin1String("-0") ? QStringLiteral("0") : s;
}

QString pt(const QPointF& p)
{
    return num(p.x()) + QLatin1Char(' ') + num(p.y());
}

/**
 * @brief A QPainterPath as SVG path data. Qt paths hold only moves, lines and cubics, so this is total.
 *
 * **Every subpath is closed with its own Z**, not just the last one. That matters for stroking rather
 * than filling: SVG fills an open subpath as if it were closed, but strokes it as an open line, so a
 * shape with more than one contour — a united silhouette with a hole, or any glyph with a counter —
 * would come out with a visible gap in its outline.
 */
QString pathData(const QPainterPath& path)
{
    QString d;
    d.reserve(path.elementCount() * 12);
    bool open = false;   // a subpath has been started and not yet closed

    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            if (open)
                d += QLatin1String("Z ");
            d += QLatin1String("M") + pt(e);
            open = true;
            break;
        case QPainterPath::LineToElement:
            d += QLatin1String("L") + pt(e);
            break;
        case QPainterPath::CurveToElement:
            // A cubic is stored as the control point followed by two CurveToData elements; consume all
            // three here so the loop does not emit them as stray commands.
            d += QLatin1String("C") + pt(e);
            if (i + 2 < path.elementCount()) {
                d += QLatin1Char(' ') + pt(path.elementAt(i + 1))
                   + QLatin1Char(' ') + pt(path.elementAt(i + 2));
                i += 2;
            }
            break;
        case QPainterPath::CurveToDataElement:
            break;   // consumed above
        }
        d += QLatin1Char(' ');
    }
    if (open)
        d += QLatin1Char('Z');
    return d.trimmed();
}

//! SVG spells QPainterPath's two fill rules this way.
QString fillRule(const QPainterPath& p)
{
    return p.fillRule() == Qt::WindingFill ? QStringLiteral("nonzero") : QStringLiteral("evenodd");
}

QString esc(const QString& s)
{
    QString o = s;
    o.replace(QLatin1Char('&'),  QLatin1String("&amp;"));    // first, or it double-escapes the rest
    o.replace(QLatin1Char('<'),  QLatin1String("&lt;"));
    o.replace(QLatin1Char('>'),  QLatin1String("&gt;"));
    o.replace(QLatin1Char('"'),  QLatin1String("&quot;"));
    o.replace(QLatin1Char('\''), QLatin1String("&apos;"));
    // A literal newline inside an attribute is normalised to a space by every XML parser, which would
    // silently join a bubble's lines on reload. Encode it.
    o.replace(QLatin1Char('\n'), QLatin1String("&#10;"));
    o.replace(QLatin1Char('\r'), QString());
    return o;
}

//! `fill="#rrggbb"` plus an opacity attribute only when the colour is not fully opaque.
QString paint(const char* attr, const QColor& c)
{
    QString s = QLatin1String(attr) + QLatin1String("=\"") + c.name(QColor::HexRgb) + QLatin1Char('"');
    if (c.alpha() < 255)
        s += QLatin1Char(' ') + QLatin1String(attr) + QLatin1String("-opacity=\"")
           + num(c.alphaF()) + QLatin1Char('"');
    return s;
}

QString attr(const QString& name, const QString& value)
{
    return QLatin1String(" pm:") + name + QLatin1String("=\"") + esc(value) + QLatin1Char('"');
}

QString attr(const QString& name, int value)
{
    return attr(name, QString::number(value));
}

//! Tails as "x,y,width,bend" groups joined by ";" — one attribute however many there are.
QString tailsToText(const QList<Tail>& tails)
{
    QStringList parts;
    parts.reserve(tails.size());
    for (const Tail& t : tails)
        parts << (num(t.tip.x()) + QLatin1Char(',') + num(t.tip.y()) + QLatin1Char(',')
                  + num(t.baseWidth) + QLatin1Char(',') + num(t.bend));
    return parts.join(QLatin1Char(';'));
}

QList<Tail> tailsFromText(const QString& s)
{
    QList<Tail> tails;
    for (const QString& g : s.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const auto f = g.split(QLatin1Char(','));
        if (f.size() < 2)
            continue;   // a group without at least a tip says nothing
        Tail t;
        t.tip = QPointF(f[0].toDouble(), f[1].toDouble());
        if (f.size() > 2) t.baseWidth = f[2].toDouble();
        if (f.size() > 3) t.bend      = f[3].toDouble();
        tails.append(t);
    }
    return tails;
}

/**
 * @brief The `<defs>` block for \p a's style, and the `filter="..."` to hang on the group.
 *
 * Only primitives librsvg implements. Qt SVG implements neither feTurbulence nor feDisplacementMap, so
 * anything written here is invisible to Qt — which is precisely why a styled bubble is previewed by
 * asking the library to rasterise it (see the strip editor's sharp tier) rather than drawing it locally.
 *
 * baseFrequency is in the filter region's *user space*, and the viewBox is in balloon pixels, so the
 * texture scales with the bubble rather than with the output resolution. A chapter re-profiled to twice
 * the width re-renders the same wobble, larger — not twice as much of it.
 */
QString styleDefs(const ObjectRecord& a, const QString& id)
{
    if (a.style.texture != ObjectRecord::Texture::Rough)
        return {};

    const StyleProperties& s       = a.style;
    const bool             blurred = s.roughBlur > 0.0;

    QString d;
    d += QStringLiteral("  <defs>\n");
    // A generous region: the displacement moves ink outward, and a filter region is a hard clip.
    d += QStringLiteral("    <filter id=\"%1\" x=\"-25%\" y=\"-25%\" width=\"150%\" height=\"150%\">\n")
             .arg(id);
    d += QStringLiteral("      <feTurbulence type=\"fractalNoise\" baseFrequency=\"%1\" numOctaves=\"%2\" "
                        "seed=\"%3\" result=\"pmNoise\"/>\n")
             .arg(num(qMax(s.roughFrequency, 0.0))).arg(qMax(s.roughOctaves, 1)).arg(a.styleSeed % 65536u);
    d += QStringLiteral("      <feDisplacementMap in=\"SourceGraphic\" in2=\"pmNoise\" scale=\"%1\" "
                        "xChannelSelector=\"R\" yChannelSelector=\"G\"%2/>\n")
             .arg(num(qMax(s.roughDisplacement, 0.0)),
                  blurred ? QStringLiteral(" result=\"pmBled\"") : QString());
    if (blurred)
        d += QStringLiteral("      <feGaussianBlur in=\"pmBled\" stdDeviation=\"%1\"/>\n").arg(num(s.roughBlur));
    d += QStringLiteral("    </filter>\n  </defs>\n");
    return d;
}

int intOf(const QStringView& v, int fallback)
{
    bool      ok = false;
    const int n  = v.toInt(&ok);
    return ok ? n : fallback;
}

// ---------------------------------------------------------------------------
// A field's value as a `pm:` attribute, and back — one overload per type a group holds (fields.hpp).
// Reading falls back to the value already there, which for a fresh record is the struct's own default.
// ---------------------------------------------------------------------------

QString attrValue(int v) { return QString::number(v); }
QString attrValue(bool v) { return v ? QStringLiteral("1") : QStringLiteral("0"); }
QString attrValue(qreal v) { return num(v); }
QString attrValue(const QString& v) { return v; }
QString attrValue(const QColor& v) { return v.name(QColor::HexArgb); }
QString attrValue(ObjectRecord::Shape v) { return QString::fromLatin1(shapeName(v)); }
QString attrValue(ObjectRecord::Line v) { return QString::fromLatin1(lineName(v)); }
QString attrValue(ObjectRecord::Texture v) { return QString::fromLatin1(textureName(v)); }
//! Nodes joined by ";", as the tails are: "x,y", or "x,y,type,inX,inY,outX,outY" for a Curve's node with
//! handles.
QString attrValue(const QList<ShapeNode>& v)
{
    const auto  pair = [](const QPointF& p) {
        return num(p.x(), ShapeProperties::k_nodeDecimals) + QLatin1Char(',') + num(p.y(), ShapeProperties::k_nodeDecimals);
    };
    QStringList parts;
    parts.reserve(v.size());
    for (const ShapeNode& n : v)
        parts << (n.plain() ? pair(n.at)
                            : QStringList{pair(n.at), QLatin1String(ShapeNode::typeName(n.type)), pair(n.in), pair(n.out)}
                                  .join(QLatin1Char(',')));
    return parts.join(QLatin1Char(';'));
}

void fromAttr(QStringView s, int& v) { v = intOf(s, v); }
void fromAttr(QStringView s, bool& v) { v = intOf(s, v ? 1 : 0) != 0; }
void fromAttr(QStringView s, qreal& v)
{
    bool        ok = false;
    const qreal d  = s.toDouble(&ok);
    if (ok)
        v = d;
}
void fromAttr(QStringView s, QString& v) { v = s.toString(); }
void fromAttr(QStringView s, QColor& v)
{
    const QColor c(s.toString());
    if (c.isValid())
        v = c;
}
// An unknown or missing name reads as the reader's own fallback (Box, Clean, None).
void fromAttr(QStringView s, ObjectRecord::Shape& v) { v = shapeFromName(s); }
void fromAttr(QStringView s, ObjectRecord::Line& v) { v = lineFromName(s); }
void fromAttr(QStringView s, ObjectRecord::Texture& v) { v = textureFromName(s); }
void fromAttr(QStringView s, QList<ShapeNode>& v)
{
    v.clear();
    for (QStringView g : s.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const auto f = g.split(QLatin1Char(','));
        if (f.size() == 2)
            v.append(ShapeNode(f[0].toDouble(), f[1].toDouble()));
        else if (f.size() == 7)
            v.append(ShapeNode({f[0].toDouble(), f[1].toDouble()}, {f[3].toDouble(), f[4].toDouble()},
                               {f[5].toDouble(), f[6].toDouble()}, ShapeNode::typeFromName(f[2])));
    }
}

//! Every field of \p g as a `pm:` attribute, under its key.
template <class Group>
QString groupAttrs(const Group& g)
{
    QString out;
    Group::fields([&](const char* key, auto member) { out += attr(QLatin1String(key), attrValue(g.*member)); });
    return out;
}

//! Every field of \p g out of the `pm:` attributes in \p at; a missing one leaves its field as it is.
template <class Group>
void readGroup(Group& g, const QXmlStreamAttributes& at, const QString& ns)
{
    Group::fields([&](const char* key, auto member) {
        const QString name = QLatin1String(key);
        if (at.hasAttribute(ns, name))
            fromAttr(at.value(ns, name), g.*member);
    });
}

} // namespace

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

namespace {

//! The family a file records: the record's own, or — for an empty one, "the default" — the default's name
//! on the machine that drew it. The outlines were set in that font, and a file that says so is one another
//! machine can recognise as missing it, rather than silently re-set in its own default.
QString fileFontFamily(const ObjectRecord& a)
{
    return a.text.family.isEmpty() ? Geometry::defaultFamily() : a.text.family;
}

/**
 * @brief The lettering's `pm:` attributes, with the family a file records (fileFontFamily()), and then
 *        `pm:fontFallback` when the words were set in a stand-in.
 *
 * The family asked for is kept whatever happened; the fallback says what the outlines were actually set
 * in when that family was not installed, so the file can be re-set once it is (MainWindow's heal).
 */
QString letteringAttrs(const ObjectRecord& a)
{
    TextProperties text = a.text;
    text.family         = fileFontFamily(a);
    QString out         = groupAttrs(text);
    if (const QString fallback = Geometry::fontFallback(a); !fallback.isEmpty())
        out += attr(QStringLiteral("fontFallback"), fallback);
    return out;
}

} // namespace

QByteArray write(const ObjectRecord& a, const QByteArray& picture, const QString& mime)
{
    if (a.box.isEmpty())
        return {};

    // **A picture with words over it.** The geometry is the picture's own box — no tails to reach past
    // it — so the viewBox is the box, and the lettering is laid out in it exactly as a shapeless
    // object's is (textSafeArea() gives the whole box when there is no silhouette of ours).
    if (a.isArtwork()) {
        if (picture.isEmpty() || mime.isEmpty())
            return {};

        QString svg;
        svg += QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" "
                              "xmlns:xlink=\"http://www.w3.org/1999/xlink\" xmlns:pm=\"%1\"\n"
                              "     width=\"%2\" height=\"%3\" viewBox=\"0 0 %2 %3\">\n")
                   .arg(QLatin1String(k_pmNamespace))
                   .arg(num(a.box.width()), num(a.box.height()));

        // The same parameter group every file of ours carries, so this one is re-typable in the same
        // way — and so an import can tell it is ours (see read).
        svg += QStringLiteral("  <g");
        svg += groupAttrs(a.shape);
        svg += attr(QStringLiteral("artwork"), a.artwork);
        svg += attr(QStringLiteral("box"),
                    QStringLiteral("%1,%2").arg(a.box.width()).arg(a.box.height()));
        svg += letteringAttrs(a);
        svg += QLatin1String(">\n");

        svg += QStringLiteral("    <image x=\"0\" y=\"0\" width=\"%1\" height=\"%2\" "
                              "xlink:href=\"data:%3;base64,%4\"/>\n")
                   .arg(num(a.box.width()), num(a.box.height()), mime,
                        QString::fromLatin1(picture.toBase64()));

        const QPainterPath text = Geometry::textOutline(a);
        if (!text.isEmpty())
            svg += QStringLiteral("    <path d=\"%1\" fill-rule=\"%2\" %3/>\n")
                       .arg(pathData(text), fillRule(text), paint("fill", a.text.colour));

        svg += QLatin1String("  </g>\n</svg>\n");
        return svg.toUtf8();
    }

    // The viewBox is the record's *drawn* extent, not its balloon: a tail may reach above or left of
    // the balloon, and SVG takes a negative viewBox origin natively — so the file itself carries the
    // offset and the placement stays a plain top-left. width/height are that extent, so the library
    // rasterises it 1:1 at scale 1.0.
    const QRectF bounds = Geometry::bounds(a);
    if (bounds.isEmpty())
        return {};

    QString svg;
    svg += QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:pm=\"%1\"\n"
                          "     width=\"%2\" height=\"%3\" viewBox=\"%4 %5 %2 %3\">\n")
               .arg(QLatin1String(k_pmNamespace))
               .arg(num(bounds.width()), num(bounds.height()),
                    num(bounds.left()),  num(bounds.top()));

    // The filter is defined before the group that references it, as SVG requires.
    const QString filterId = QStringLiteral("pm-style");
    svg += styleDefs(a, filterId);

    // Everything the editor needs to re-solve this bubble, in a namespace no renderer looks at. Losing
    // these leaves a perfectly good drawing that simply cannot be re-typed — the intended degradation.
    svg += QStringLiteral("  <g");
    svg += groupAttrs(a.shape);
    svg += attr(QStringLiteral("box"),
                QStringLiteral("%1,%2").arg(a.box.width()).arg(a.box.height()));
    svg += attr(QStringLiteral("tails"), tailsToText(a.tails.items));
    svg += letteringAttrs(a);
    svg += groupAttrs(a.skin);
    svg += groupAttrs(a.style);
    svg += attr(QStringLiteral("styleSeed"), QString::number(a.styleSeed));
    svg += QLatin1String(">\n");

    // Two groups (PLAN-L M16): the balloon — its silhouette and its line's strokes — and the lettering.
    // The texture's filter hangs on the balloon's: a displacement on the lettering would drag the words
    // about with the outline, and the balloon is what should look hand-drawn, not the words in it. The
    // lettering's own group is where a text style will hang.
    const QPainterPath silhouette = Geometry::silhouette(a);
    if (!silhouette.isEmpty()) {
        svg += QLatin1String("    <g");
        if (Geometry::hasFilter(a))
            svg += QStringLiteral(" filter=\"url(#%1)\"").arg(filterId);
        svg += QLatin1String(">\n");
        svg += QStringLiteral("      <path d=\"%1\" fill-rule=\"%2\" %3")
                   .arg(pathData(silhouette), fillRule(silhouette), paint("fill", a.skin.fill));
        if (a.skin.strokeWidth > 0 && Geometry::hasOutline(a))
            svg += QStringLiteral(" %1 stroke-width=\"%2\" stroke-linejoin=\"round\"")
                       .arg(paint("stroke", a.skin.stroke)).arg(a.skin.strokeWidth);
        svg += QLatin1String("/>\n");

        // A line's own strokes, in the stroke colour, over the fill and under the words.
        const QPainterPath ink = Geometry::ink(a, silhouette);
        if (!ink.isEmpty())
            svg += QStringLiteral("      <path d=\"%1\" fill-rule=\"%2\" %3/>\n")
                       .arg(pathData(ink), fillRule(ink), paint("fill", a.skin.stroke));
        svg += QLatin1String("    </g>\n");
    }

    const QPainterPath text = Geometry::textOutline(a);
    if (!text.isEmpty())
        svg += QStringLiteral("    <g>\n      <path d=\"%1\" fill-rule=\"%2\" %3/>\n    </g>\n")
                   .arg(pathData(text), fillRule(text), paint("fill", a.text.colour));

    svg += QLatin1String("  </g>\n</svg>\n");
    return svg.toUtf8();
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

ObjectRecord read(const QByteArray& svg, bool* ok)
{
    if (ok)
        *ok = false;

    ObjectRecord a;
    QXmlStreamReader xml(svg);
    const QString ns = QLatin1String(k_pmNamespace);

    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;

        const QXmlStreamAttributes at = xml.attributes();
        if (!at.hasAttribute(ns, QStringLiteral("shape")))
            continue;   // not the parameter-carrying group — keep looking

        // Every field falls back to the struct's own default, so a file written by an older build, or
        // hand-edited, loads as a usable bubble rather than a blank one.
        readGroup(a.shape, at, ns);
        readGroup(a.text, at, ns);
        readGroup(a.skin, at, ns);
        readGroup(a.style, at, ns);

        // A wrapper says which picture it is wrapping; a balloon carries no such attribute and stays
        // an object we draw.
        a.artwork = at.value(ns, QStringLiteral("artwork")).toString();

        const auto box = at.value(ns, QStringLiteral("box")).toString().split(QLatin1Char(','));
        if (box.size() == 2)
            a.box = QSize(intOf(QStringView(box[0]), a.box.width()),
                          intOf(QStringView(box[1]), a.box.height()));

        a.tails.items = tailsFromText(at.value(ns, QStringLiteral("tails")).toString());

        bool       seedOk = false;
        const auto seed   = at.value(ns, QStringLiteral("styleSeed")).toULongLong(&seedOk);
        if (seedOk)
            a.styleSeed = quint32(seed);

        if (ok)
            *ok = true;
        return a;
    }

    return a;   // no pm:* group — a hand-drawn or externally edited asset (ok stays false)
}

ObjectRecord::Map readOverlays(const std::vector<Platemaker::Models::StripOverlay>& overlays)
{
    ObjectRecord::Map map;
    for (const auto& o : overlays) {
        if (o.assetPath.empty())
            continue;
        QFile f(QString::fromStdString(o.assetPath));
        if (!f.open(QIODevice::ReadOnly))
            continue;   // missing asset: the overlay stays in the project and shows as unavailable

        bool               ok = false;
        const ObjectRecord a  = read(f.readAll(), &ok);
        if (ok)
            map.insert(QString::fromStdString(o.uid), a);
        // else: a flat asset (hand-drawn, or edited outside Platemaker). Deliberately left out of the
        // map — the caller draws it from the file and does not offer to re-type it.
    }
    return map;
}

// ---------------------------------------------------------------------------------------------------
// Writing an overlay file (moved from Project so a workspace can be repaired with no project open)
// ---------------------------------------------------------------------------------------------------

/**
 * @brief The media type of @p file, from its suffix — what a data URI has to declare.
 * @param file The file to determine the media type for.
 * @return The media type as a string.
 */
QString pictureMime(const QString& file)
{
    const QString ext = QFileInfo(file).suffix().toLower();
    if (ext == QLatin1String("png"))  return QStringLiteral("image/png");
    if (ext == QLatin1String("webp")) return QStringLiteral("image/webp");
    if (ext == QLatin1String("svg"))  return QStringLiteral("image/svg+xml");
    if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg"))
        return QStringLiteral("image/jpeg");
    return {};
}

/**
 * @brief Writes \p a into the workspace's overlays/ directory as an SVG.
 *
 * The file the library composites **is** the authoring record: the artwork every renderer can draw,
 * plus the editor's parameters in a namespace renderers ignore. There is no separate sidecar and no
 * intermediate raster.
 *
 * @param reusePath The file this overlay already owns, overwritten in place. Empty for a new overlay,
 *                  which is then named by content hash — that name is also what makes two identical
 *                  bubbles share one file, the same dedup the library's inventory performs.
 *
 * A bubble keeps **one** file for its lifetime. Naming every revision by its content instead would
 * leave one file per settled edit — a dozen in a single lettering session — and the reason to do that
 * has gone: the undo snapshot carries the full authoring record, so a previous rendering is regenerated
 * from it (see Project::rewriteOverlayAssets()) rather than recovered from a file kept alive for it.
 *
 * @return The asset's absolute path, or empty when it could not be written.
 */
QString writeFile(const QString& overlaysDir, const ObjectRecord& a, const QString& reusePath)
{
    // **A picture with nothing written on it is its own file.** There is nothing of ours to draw, so
    // generating one would be drawing our geometry over somebody's artwork — the mistake that once
    // stored a default balloon over an imported picture, in the one place that could still make it.
    // Its overlay points straight at the imported picture.
    if (a.isArtwork()) {
        const QString picture = overlaysDir + QLatin1Char('/') + a.artwork;
        if (a.text.body.isEmpty())
            return picture;

        // Lettered, so there *is* something of ours: a wrapper that embeds the picture and draws the
        // words over it. Written to a file of its own and **never over the picture** — `reusePath` is
        // deliberately ignored here, because for an artwork overlay that path may be the picture
        // itself, and the picture is the one thing in the workspace we did not make.
        QFile in(picture);
        if (!in.open(QIODevice::ReadOnly))
            return {};
        const QByteArray bytes = in.readAll();
        const QByteArray svg   = write(a, bytes, pictureMime(a.artwork));
        if (svg.isEmpty())
            return {};

        const QString sha = QString::fromLatin1(
            QCryptographicHash::hash(svg, QCryptographicHash::Sha256).toHex()).left(16);
        const QString path = overlaysDir + QStringLiteral("/ovl-") + sha + QStringLiteral(".svg");
        if (QFile::exists(path))
            return path;
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return {};
        out.write(svg);
        return out.error() == QFile::NoError ? path : QString{};
    }

    const QByteArray svg = write(a);
    if (svg.isEmpty() || overlaysDir.isEmpty())
        return {};

    QString path = reusePath;
    if (path.isEmpty()) {
        // Hashed from the bytes about to be written, so the name matches what the library will hash
        // when it inventories the file — no scratch file needed, unlike the raster path's encode-then-hash.
        const QString sha = QString::fromLatin1(
            QCryptographicHash::hash(svg, QCryptographicHash::Sha256).toHex()).left(16);
        path = overlaysDir + QStringLiteral("/ovl-") + sha + QStringLiteral(".svg");
        if (QFile::exists(path))
            return path;               // same content, already stored
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    f.write(svg);
    return f.error() == QFile::NoError ? path : QString{};
}

QString bakedFontFallback(const QByteArray& svg)
{
    QXmlStreamReader xml(svg);
    const QString    ns = QLatin1String(k_pmNamespace);
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        const QXmlStreamAttributes at = xml.attributes();
        if (at.hasAttribute(ns, QStringLiteral("shape")))   // the parameter group — the only place it goes
            return at.value(ns, QStringLiteral("fontFallback")).toString();
    }
    return {};
}

} // namespace Svg
