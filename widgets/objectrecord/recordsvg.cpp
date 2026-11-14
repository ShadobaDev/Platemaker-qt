#include "recordsvg.hpp"

#include "composition.hpp"
#include "geometry.hpp"
#include "renderlist.hpp"
#include "styles.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainterPath>
#include <QRectF>
#include <QStringList>
#include <QPointF>
#include <QXmlStreamReader>

#include <cmath>

namespace Svg {

namespace {

//! Decimal places for a value the record is read back from: Render::k_valueDecimals.
constexpr int k_valuePrecision = Render::k_valueDecimals;

QString num(qreal v, int precision = Render::k_coordinateDecimals)
{
    return Render::number(v, precision);
}

QString pt(const QPointF& p)
{
    return num(p.x()) + QLatin1Char(' ') + num(p.y());
}

/**
 * \brief A QPainterPath as SVG path data. Qt paths hold only moves, lines and cubics, so this is total.
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

// ---------------------------------------------------------------------------
// The old `pm:` attributes, as files written before the content carried them: read only, by read(), until
// the first release, which no such file can reach.
// ---------------------------------------------------------------------------

//! Tails as "x,y,width,bend,look…" groups joined by ";", as the old `pm:tails` carried them.
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
        t.look = TailLook::fromFields(f, 4);
        tails.append(t);
    }
    return tails;
}

/**
 * \brief The `<defs>` block for @p p's pattern, or nothing for a solid Paint.
 *
 * In the user space of the whole drawing (`userSpaceOnUse`), which is the box's, so the grid is laid from
 * the box's corner as the painter's brush is (Painter::paintBrush()), from the same cell.
 */
QString patternDefs(const Paint& p, const QString& id)
{
    if (p.kind == Paint::Kind::Solid)
        return {};
    const Geometry::PatternCell cell = Geometry::patternCell(p);
    QString d;
    d += QStringLiteral("  <defs>\n");
    d += QStringLiteral("    <pattern id=\"%1\" patternUnits=\"userSpaceOnUse\" width=\"%2\" height=\"%2\" "
                        "patternTransform=\"rotate(%3)\">\n")
             .arg(id, num(cell.size), num(cell.angle));
    d += QStringLiteral("      <rect width=\"%1\" height=\"%1\" %2/>\n").arg(num(cell.size), paint("fill", p.colour));
    d += QStringLiteral("      <path d=\"%1\" fill-rule=\"nonzero\" %2/>\n")
             .arg(pathData(cell.marks), paint("fill", p.second));
    d += QStringLiteral("    </pattern>\n  </defs>\n");
    return d;
}

//! @p attr (`fill` or `stroke`) painted with @p p: its colour, or the pattern defined as @p id.
QString paintAttr(const char* attr, const Paint& p, const QString& id)
{
    if (p.kind == Paint::Kind::Solid)
        return paint(attr, p.colour);
    return QStringLiteral("%1=\"url(#%2)\"").arg(QLatin1String(attr), id);
}

int intOf(const QStringView& v, int fallback)
{
    bool      ok = false;
    const int n  = v.toInt(&ok);
    return ok ? n : fallback;
}

// A field's old `pm:` attribute, read back: one overload per type a group holds (fields.hpp), falling back to
// the value already there, which for a fresh record is the struct's own default.

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
void fromAttr(QStringView s, ObjectRecord::Edge& v) { v = edgeFromName(s); }
void fromAttr(QStringView s, ObjectRecord::Pattern& v) { v = patternFromName(s); }
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

//! Every field of \p g out of the `pm:` attributes in \p at; a missing one leaves its field as it is.
template <class Group>
void readGroup(Group& g, const QXmlStreamAttributes& at, const QString& ns)
{
    Group::fields([&](const char* key, auto member) {
        const QString name = QLatin1String(key);
        if (at.hasAttribute(ns, name))
            fromAttr(at.value(ns, name), Fields::at(g, member));
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
 * \brief The content's copy in the file, `<pm:content>` (its text the record's content, contentOf()), with
 *        `pm:fontFallback` when the words were set in a stand-in.
 *
 * The family asked for is kept whatever happened; the fallback says what the outlines were actually set
 * in when that family was not installed, so the file can be re-set once it is (MainWindow's heal). It says
 * what this file's drawing is, not what the bubble is, so it is the file's, not the content's.
 */
QString contentElement(const ObjectRecord& a)
{
    QString out = QStringLiteral("  <pm:content");
    if (const QString fallback = Geometry::fontFallback(a); !fallback.isEmpty())
        out += QStringLiteral(" pm:fontFallback=\"") + esc(fallback) + QLatin1Char('"');
    // Element text, not an attribute: only what would end it or start markup is escaped, so the JSON reads.
    QString text = contentOf(a);
    text.replace(QLatin1Char('&'), QLatin1String("&amp;"));   // first, or it double-escapes the rest
    text.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    text.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    return out + QLatin1Char('>') + text + QStringLiteral("</pm:content>\n");
}

/**
 * \brief @p ink as the value of @p attr (`fill` or `stroke`): its pattern, or a colour.
 */
QString inkAttr(const char* attr, const Render::Ink& ink)
{
    return ink.patternId.isEmpty() ? paint(attr, ink.paint.colour) : paintAttr(attr, ink.paint, ink.patternId);
}

//! One filter primitive on its line, its children (a transfer function) inside it.
QString primitive(const Render::Primitive& p)
{
    QString s = QLatin1Char('<') + p.tag;
    for (const auto& [name, value] : p.attrs)
        s += QLatin1Char(' ') + name + QLatin1String("=\"") + value + QLatin1Char('"');
    if (p.children.isEmpty())
        return s + QLatin1String("/>");
    s += QLatin1Char('>');
    for (const Render::Primitive& c : p.children)
        s += primitive(c);
    return s + QLatin1String("</") + p.tag + QLatin1Char('>');
}

//! @p list's definitions, each a `<defs>` block of its own, in order.
QString defsOf(const Render::List& list)
{
    QString d;
    for (const Render::Def& def : list.defs) {
        if (def.pattern) {
            d += patternDefs(*def.pattern, def.id);
        } else if (def.filter) {
            d += QStringLiteral("  <defs>\n    <filter id=\"%1\" %2>\n").arg(def.id, def.filter->region);
            for (const Render::Primitive& p : def.filter->primitives)
                d += QLatin1String("      ") + primitive(p) + QLatin1Char('\n');
            d += QStringLiteral("    </filter>\n  </defs>\n");
        }
    }
    return d;
}

QString itemsOf(const QList<Render::Item>& items, const QString& indent);

//! One item at @p indent: a path (a clip it needs first), a group (a mask it needs first) or a picture.
QString itemOf(const Render::Item& i, const QString& indent)
{
    QString out;
    switch (i.kind) {
    case Render::Item::Kind::Image:
        return indent + QStringLiteral("<image x=\"0\" y=\"0\" width=\"%1\" height=\"%2\" "
                                       "xlink:href=\"data:%3;base64,%4\"/>\n")
                            .arg(num(i.size.width()), num(i.size.height()), i.mime,
                                 QString::fromLatin1(i.picture.toBase64()));
    case Render::Item::Kind::Group:
        if (!i.mask.isEmpty())
            out += indent + QStringLiteral("<mask id=\"%1\" x=\"-25%\" y=\"-25%\" width=\"150%\" height=\"150%\">"
                                           "<path d=\"%2\" fill-rule=\"%3\" fill=\"white\" filter=\"url(#%4)\"/></mask>\n")
                                .arg(i.maskId, pathData(i.mask), fillRule(i.mask), i.maskFilter);
        out += indent + QLatin1String("<g");
        if (i.opacity < 1.0)
            out += QStringLiteral(" opacity=\"%1\"").arg(num(i.opacity));
        if (!i.filter.isEmpty())
            out += QStringLiteral(" filter=\"url(#%1)\"").arg(i.filter);
        if (!i.mask.isEmpty())
            out += QStringLiteral(" mask=\"url(#%1)\"").arg(i.maskId);
        out += QLatin1String(">\n") + itemsOf(i.children, indent + QLatin1String("  ")) + indent
             + QLatin1String("</g>\n");
        return out;
    case Render::Item::Kind::Path:
        break;
    }
    if (!i.clip.isEmpty())
        out += indent + QStringLiteral("<clipPath id=\"%1\"><path d=\"%2\" clip-rule=\"%3\"/></clipPath>\n")
                            .arg(i.clipId, pathData(i.clip), fillRule(i.clip));
    out += indent + QStringLiteral("<path d=\"%1\"").arg(pathData(i.path));
    if (i.fill)
        out += QStringLiteral(" fill-rule=\"%1\" %2")
                   .arg(i.nonzero ? QStringLiteral("nonzero") : fillRule(i.path), inkAttr("fill", *i.fill));
    else
        out += QLatin1String(" fill=\"none\"");
    if (i.stroke)
        out += QStringLiteral(" %1 stroke-width=\"%2\" stroke-linejoin=\"round\"%3")
                   .arg(inkAttr("stroke", *i.stroke), num(i.strokeWidth),
                        i.roundCap ? QStringLiteral(" stroke-linecap=\"round\"") : QString());
    else if (i.bleed > 0.0 && i.fill)
        out += QStringLiteral(" %1 stroke-width=\"%2\" stroke-linejoin=\"round\"")
                   .arg(inkAttr("stroke", *i.fill), num(2.0 * i.bleed));
    if (!i.filter.isEmpty())
        out += QStringLiteral(" filter=\"url(#%1)\"").arg(i.filter);
    if (!i.clipId.isEmpty())
        out += QStringLiteral(" clip-path=\"url(#%1)\"").arg(i.clipId);
    return out + QLatin1String("/>\n");
}

QString itemsOf(const QList<Render::Item>& items, const QString& indent)
{
    QString out;
    for (const Render::Item& i : items)
        out += itemOf(i, indent);
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
        const QPainterPath text  = Geometry::textOutline(a);
        const Render::List words = Render::lettering(a, text, Geometry::textShadowOf(a, text));

        QString svg;
        svg += QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" "
                              "xmlns:xlink=\"http://www.w3.org/1999/xlink\" xmlns:pm=\"%1\"\n"
                              "     width=\"%2\" height=\"%3\" viewBox=\"0 0 %2 %3\">\n")
                   .arg(QLatin1String(k_pmNamespace))
                   .arg(num(a.box.width()), num(a.box.height()));
        svg += defsOf(words);

        // The same content every file of ours carries, so an import can tell it is ours (see read).
        svg += contentElement(a);
        svg += QStringLiteral("  <g>\n");

        Render::Item image;
        image.kind    = Render::Item::Kind::Image;
        image.picture = picture;
        image.mime    = mime;
        image.size    = QSizeF(a.box);
        svg += itemOf(image, QStringLiteral("    "));
        svg += itemsOf(words.items, QStringLiteral("    "));

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

    // The balloon and the words, as the painter draws them (renderlist.hpp); their definitions first.
    const QPainterPath silhouette = Geometry::silhouette(a);
    const QPainterPath text       = Geometry::textOutline(a);
    const Render::List balloon    = Render::balloon(a, silhouette);
    const Render::List words      = Render::lettering(a, text, Geometry::textShadowOf(a, text));
    svg += defsOf(balloon) + defsOf(words);

    // A copy of the bubble's content, in a namespace no renderer looks at: what *Import* reads to make this
    // file a bubble again in another workspace. Losing it leaves a perfectly good drawing that simply cannot be
    // re-typed — the intended degradation.
    svg += contentElement(a);
    svg += QStringLiteral("  <g>\n");

    // Two groups: the balloon, and the lettering. The texture's filter hangs on the balloon's: a displacement
    // on the lettering would drag the words about with the outline.
    svg += itemsOf(balloon.items, QStringLiteral("    "));
    if (!words.items.isEmpty())
        svg += QLatin1String("    <g>\n") + itemsOf(words.items, QStringLiteral("      ")) + QLatin1String("    </g>\n");

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

        // The content's copy (contentElement()).
        if (xml.namespaceUri() == ns && xml.name() == QLatin1String("content"))
            return fromContent(xml.readElementText().toStdString(), ok);

        // A file written before the content: its old attributes (until the first release; see above).
        const QXmlStreamAttributes at = xml.attributes();
        if (!at.hasAttribute(ns, QStringLiteral("shape")))
            continue;   // not the parameter-carrying group — keep looking

        // Every field falls back to the struct's own default, so a file written by an older build, or
        // hand-edited, loads as a usable bubble rather than a blank one.
        readGroup(a.shape, at, ns);
        readGroup(a.text, at, ns);
        readGroup(a.skin, at, ns);
        readGroup(a.style, at, ns);
        readGroup(a.texture, at, ns);
        readGroup(a.effects, at, ns);

        // A wrapper says which picture it is wrapping; a balloon carries no such attribute and stays
        // an object we draw.
        a.artwork = at.value(ns, QStringLiteral("artwork")).toString();

        const auto box = at.value(ns, QStringLiteral("box")).toString().split(QLatin1Char(','));
        if (box.size() == 2)
            a.box = QSize(intOf(QStringView(box[0]), a.box.width()),
                          intOf(QStringView(box[1]), a.box.height()));

        a.tails.items    = tailsFromText(at.value(ns, QStringLiteral("tails")).toString());
        a.tails.nextLook = TailLook::fromFields(at.value(ns, QStringLiteral("nextTail")).toString().split(QLatin1Char(',')));

        bool       seedOk = false;
        const auto seed   = at.value(ns, QStringLiteral("styleSeed")).toULongLong(&seedOk);
        if (seedOk)
            a.styleSeed = quint32(seed);
        a.presetOrigin = {at.value(ns, QStringLiteral("presetUid")).toString(),
                          at.value(ns, QStringLiteral("presetName")).toString()};

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
        if (bool ok = false; !o.content.empty()) {
            const ObjectRecord a = fromContent(o.content, &ok);
            if (ok) {
                map.insert(QString::fromStdString(o.uid), a);
                continue;
            }
        }
        // No content: an overlay saved before it was kept with the overlay, read from its own file once.
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
 * \brief The media type of @p file, from its suffix — what a data URI has to declare.
 * \param file The file to determine the media type for.
 * \return The media type as a string.
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
 * \brief Writes \p a into the workspace's overlays/ directory as an SVG.
 *
 * The file the library composites **is** the authoring record: the artwork every renderer can draw,
 * plus the editor's parameters in a namespace renderers ignore. There is no separate sidecar and no
 * intermediate raster.
 *
 * \param reusePath The file this overlay already owns, overwritten in place. Empty for a new overlay,
 *                  which is then named by content hash — that name is also what makes two identical
 *                  bubbles share one file, the same dedup the library's inventory performs.
 *
 * A bubble keeps **one** file for its lifetime. Naming every revision by its content instead would
 * leave one file per settled edit — a dozen in a single lettering session — and the reason to do that
 * has gone: the undo snapshot carries the full authoring record, so a previous rendering is regenerated
 * from it (see Project::rewriteOverlayAssets()) rather than recovered from a file kept alive for it.
 *
 * \return The asset's absolute path, or empty when it could not be written.
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
        // The content's element, or an old file's parameter group: the only places it goes.
        if ((xml.namespaceUri() == ns && xml.name() == QLatin1String("content")) || at.hasAttribute(ns, QStringLiteral("shape")))
            return at.value(ns, QStringLiteral("fontFallback")).toString();
    }
    return {};
}

QString contentOf(const ObjectRecord& a)
{
    ObjectRecord stored = a;
    stored.text.family  = fileFontFamily(a);
    return QString::fromUtf8(QJsonDocument(toJson(compositionOf(stored))).toJson(QJsonDocument::Compact));
}

ObjectRecord fromContent(const std::string& content, bool* ok)
{
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(content));
    if (ok)
        *ok = doc.isObject();
    if (!doc.isObject())
        return {};
    // A content saved before it was a composition's has no words: the record's own form, read until the first
    // release, as the old attributes are.
    const QJsonObject j = doc.object();
    return j.contains(QStringLiteral("words")) ? recordOf(compositionFromJson(j)) : ObjectRecord::fromJson(j);
}

} // namespace Svg
