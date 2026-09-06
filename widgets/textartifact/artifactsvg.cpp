#include "artifactsvg.h"

#include "artifactpainter.h"

#include <QFile>
#include <QPainterPath>
#include <QPointF>
#include <QXmlStreamReader>

#include <cmath>

namespace {

//! Decimal places for path coordinates. Two is well under a rendered pixel and keeps files small.
constexpr int k_precision = 2;

QString num(qreal v)
{
    // 'f' then trim, rather than 'g': 'g' switches to scientific notation for small values and SVG has
    // no such syntax, so a coordinate like 1e-05 would silently become an invalid path.
    QString s = QString::number(v, 'f', k_precision);
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

int intOf(const QStringView& v, int fallback)
{
    bool      ok = false;
    const int n  = v.toInt(&ok);
    return ok ? n : fallback;
}

const char* shapeName(TextArtifact::Shape s)
{
    switch (s) {
    case TextArtifact::Shape::None:    return "none";
    case TextArtifact::Shape::Speech:  return "speech";
    case TextArtifact::Shape::Shout:   return "shout";
    case TextArtifact::Shape::Caption: return "caption";
    }
    return "speech";
}

TextArtifact::Shape shapeFrom(const QStringView& v)
{
    if (v == QLatin1String("none"))    return TextArtifact::Shape::None;
    if (v == QLatin1String("shout"))   return TextArtifact::Shape::Shout;
    if (v == QLatin1String("caption")) return TextArtifact::Shape::Caption;
    return TextArtifact::Shape::Speech;
}

} // namespace

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

QByteArray artifactToSvg(const TextArtifact& a)
{
    if (a.box.isEmpty())
        return {};

    const int w = a.box.width();
    const int h = a.box.height();

    QString svg;
    svg += QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:pm=\"%1\"\n"
                          "     width=\"%2\" height=\"%3\" viewBox=\"0 0 %2 %3\">\n")
               .arg(QLatin1String(k_pmNamespace)).arg(w).arg(h);

    // Everything the editor needs to re-solve this bubble, in a namespace no renderer looks at. Losing
    // these leaves a perfectly good drawing that simply cannot be re-typed — the intended degradation.
    svg += QStringLiteral("  <g");
    svg += attr(QStringLiteral("v"), 1);
    svg += attr(QStringLiteral("shape"), QString::fromLatin1(shapeName(a.shape)));
    svg += attr(QStringLiteral("box"), QStringLiteral("%1,%2").arg(w).arg(h));
    svg += attr(QStringLiteral("tail"), QStringLiteral("%1,%2").arg(a.tail.x()).arg(a.tail.y()));
    svg += attr(QStringLiteral("text"), a.text);
    svg += attr(QStringLiteral("fontFamily"), a.fontFamily);
    svg += attr(QStringLiteral("fontSize"), a.fontPixelSize);
    svg += attr(QStringLiteral("bold"), a.bold ? 1 : 0);
    svg += attr(QStringLiteral("align"), a.align);
    svg += attr(QStringLiteral("fill"), a.fill.name(QColor::HexArgb));
    svg += attr(QStringLiteral("stroke"), a.stroke.name(QColor::HexArgb));
    svg += attr(QStringLiteral("textColour"), a.textColour.name(QColor::HexArgb));
    svg += attr(QStringLiteral("strokeWidth"), a.strokeWidth);
    svg += QLatin1String(">\n");

    const QPainterPath silhouette = artifactSilhouette(a);
    if (!silhouette.isEmpty()) {
        svg += QStringLiteral("    <path d=\"%1\" fill-rule=\"%2\" %3")
                   .arg(pathData(silhouette), fillRule(silhouette), paint("fill", a.fill));
        if (a.strokeWidth > 0)
            svg += QStringLiteral(" %1 stroke-width=\"%2\" stroke-linejoin=\"round\"")
                       .arg(paint("stroke", a.stroke)).arg(a.strokeWidth);
        svg += QLatin1String("/>\n");
    }

    const QPainterPath text = artifactTextOutline(a);
    if (!text.isEmpty())
        svg += QStringLiteral("    <path d=\"%1\" fill-rule=\"%2\" %3/>\n")
                   .arg(pathData(text), fillRule(text), paint("fill", a.textColour));

    svg += QLatin1String("  </g>\n</svg>\n");
    return svg.toUtf8();
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

TextArtifact artifactFromSvg(const QByteArray& svg, bool* ok)
{
    if (ok)
        *ok = false;

    TextArtifact a;
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
        a.shape = shapeFrom(at.value(ns, QStringLiteral("shape")));

        const auto box = at.value(ns, QStringLiteral("box")).toString().split(QLatin1Char(','));
        if (box.size() == 2)
            a.box = QSize(intOf(QStringView(box[0]), a.box.width()),
                          intOf(QStringView(box[1]), a.box.height()));

        const auto tail = at.value(ns, QStringLiteral("tail")).toString().split(QLatin1Char(','));
        if (tail.size() == 2)
            a.tail = QPoint(intOf(QStringView(tail[0]), a.tail.x()),
                            intOf(QStringView(tail[1]), a.tail.y()));

        a.text          = at.value(ns, QStringLiteral("text")).toString();
        a.fontFamily    = at.value(ns, QStringLiteral("fontFamily")).toString();
        a.fontPixelSize = intOf(at.value(ns, QStringLiteral("fontSize")), a.fontPixelSize);
        a.bold          = intOf(at.value(ns, QStringLiteral("bold")), 0) != 0;
        a.align         = intOf(at.value(ns, QStringLiteral("align")), a.align);
        a.strokeWidth   = intOf(at.value(ns, QStringLiteral("strokeWidth")), a.strokeWidth);

        const auto colour = [&](const char* name, QColor fallback) {
            const QColor c(at.value(ns, QLatin1String(name)).toString());
            return c.isValid() ? c : fallback;
        };
        a.fill       = colour("fill",       a.fill);
        a.stroke     = colour("stroke",     a.stroke);
        a.textColour = colour("textColour", a.textColour);

        if (ok)
            *ok = true;
        return a;
    }

    return a;   // no pm:* group — a hand-drawn or externally edited asset (ok stays false)
}

ArtifactMap artifactsFromOverlays(const std::vector<Platemaker::Models::StripOverlay>& overlays)
{
    ArtifactMap map;
    for (const auto& o : overlays) {
        if (o.assetPath.empty())
            continue;
        QFile f(QString::fromStdString(o.assetPath));
        if (!f.open(QIODevice::ReadOnly))
            continue;   // missing asset: the overlay stays in the project and shows as unavailable

        bool               ok = false;
        const TextArtifact a  = artifactFromSvg(f.readAll(), &ok);
        if (ok)
            map.insert(QString::fromStdString(o.uid), a);
        // else: a flat asset (hand-drawn, or edited outside Platemaker). Deliberately left out of the
        // map — the caller draws it from the file and does not offer to re-type it.
    }
    return map;
}
