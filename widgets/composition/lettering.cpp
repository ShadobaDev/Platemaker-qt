// The lettering: the words set in their font and turned into outlines, the rectangle they may occupy,
// and fitting a box to them or them to a box. Where a shape leaves room for words is its own business
// (shapes.cpp); the stroke and the author's padding are taken off here, the same for every shape.

#include "geometry.hpp"
#include "shapes.hpp"
#include "styles.hpp"   // shadowOf(): the lettering's shadow is the balloon's kind

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextOption>
#include <QtMath>

namespace Geometry {

namespace {

/**
 *  Rounds of fitting one axis and then the other in Geometry::fitBoxFor(); two or three settle it.
 */
constexpr int   k_fitPasses     = 6;
/**
 *  How far the lettering may exceed its text area before it counts as not fitting — half a pixel, so a
 *  box rounded to whole pixels is not reported as overflowing by its own rounding.
 */
constexpr qreal k_fitSlack      = 0.5;
/**
 *  Largest side a fit will consider — far past any page; it only stops a search that cannot succeed.
 */
constexpr int   k_maxFitSize    = 1 << 16;
/**
 *  Floor for a fit, matching the resize grips' own minimum — a short line must not collapse the shape.
 */
constexpr int   k_minFitSize    = 40;
/**
 *  Half the narrowest text area an arrow can drag it to, in balloon pixels: its edges stop this short of
 *  the middle, so the area never turns inside out (dragTextAreaHandle()).
 */
constexpr qreal k_minTextHalf   = 1.0;

/**
 * \brief The rectangle text may occupy — the shape's own text area (ShapeInfo::safeArea), inset so
 *        letters do not touch the stroke, and by the author's paddings: the side one left and right, the
 *        top one at the top and, mirrored, at the bottom, which the block is centred between.
 */
QRectF textSafeArea(const Composition& c, const Words& w)
{
    const Body* seat = seatOf(c, w);
    if (!seat)
        return QRectF(w.place.pos, QSizeF(w.box));

    // The author's paddings (TextProperties), on top of the stroke. Negative widens the area past the
    // outline — an impact balloon's lettering bursting out of it.
    // An edge's teeth go in from the outline, so the area gives up their depth as well.
    const Balloon& b      = *c.balloon;
    const qreal    inset  = b.style.line.strokeWidth + edgeInset(seat->shape);
    const qreal    side   = inset + w.text.paddingSide;
    const qreal    top    = inset + w.text.paddingTop;
    const QRectF   usable = shapeInfo(seat->shape.kind).safeArea(seat->shape, balloonRect(b, *seat));
    return usable.adjusted(side, top, -side, -top);
}

/**
 * \brief The laid-out text, ready to draw or measure.
 *
 * **Never wrapped.** The lines are the author's line breaks, exactly — lettering is broken by hand, and
 * a long word grows its box rather than being split. @p width only places each line according to the
 * alignment; a line wider than it overflows, it is not re-broken.
 */
void layOutText(QTextDocument& doc, const TextProperties& t, qreal width)
{
    // A family that is not installed is not handed to QFont: QFont would substitute through the platform's
    // own table (on Windows, "MS Shell Dlg 2" → Tahoma), which is a different stand-in from the one an
    // empty family gets. Leaving it unset gives both the application's default font, so "no font chosen"
    // and "the chosen font is missing" look the same, and Geometry::fontFallback() can name it.
    QFont f;
    if (!t.family.isEmpty() && QFontDatabase::hasFamily(t.family))
        f.setFamily(t.family);
    f.setPixelSize(qMax(1, t.pixelSize));
    f.setBold(t.bold);

    QTextOption opt;
    opt.setAlignment(static_cast<Qt::Alignment>(t.align));
    opt.setWrapMode(QTextOption::NoWrap);

    doc.setDefaultFont(f);
    doc.setDefaultTextOption(opt);
    doc.setDocumentMargin(0);
    doc.setPlainText(t.body);

    // The font's own pitch is left exactly as it is at 100 %: a proportional height of 100 is not the
    // same arithmetic as the default single height, and the default is what every bubble so far used.
    if (t.lineSpacing != 100) {
        QTextBlockFormat pitch;
        pitch.setLineHeight(qMax(1, t.lineSpacing), QTextBlockFormat::ProportionalHeight);
        QTextCursor all(&doc);
        all.select(QTextCursor::Document);
        all.mergeBlockFormat(pitch);
    }
    doc.setTextWidth(qMax(qreal(1), width));
}

} // namespace

QString fontFallback(const Words& w)
{
    const TextProperties& t = w.text;
    if (t.body.isEmpty() || t.family.isEmpty() || QFontDatabase::hasFamily(t.family))
        return {};
    // The stand-in layOutText() uses: the application's default font, never the platform's substitution.
    return defaultFamily();
}

QString defaultFamily()
{
    return QFontInfo(QFont()).family();
}

QPainterPath textOutline(const Composition& c, const Words& w)
{
    if (w.text.body.isEmpty())
        return {};

    const QRectF safe = textSafeArea(c, w);
    if (safe.width() <= 1 || safe.height() <= 1)
        return {};

    QTextDocument doc;
    layOutText(doc, w.text, safe.width());

    // Vertically centred in the safe area — a bubble's text sits in the middle of the balloon. A block
    // taller than the area stops at its top, the top padding, and goes on downwards.
    const qreal   h = doc.size().height();
    const QPointF origin(safe.left(), safe.top() + qMax(qreal(0), (safe.height() - h) / 2.0));

    // The document does the wrapping — unchanged — and then each laid-out line is converted to glyph
    // outlines at the position the layout gave it. Going through the layout rather than re-wrapping by
    // hand is what keeps this identical to what the document would have painted; going to *outlines* is
    // what lets the same geometry be written into an SVG the library can rasterise without a font.
    const QFont  f = doc.defaultFont();
    QPainterPath out;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        const QTextLayout* lay = b.layout();
        if (!lay)
            continue;
        const QString blockText = b.text();
        for (int i = 0; i < lay->lineCount(); ++i) {
            const QTextLine line = lay->lineAt(i);
            const QString   s    = blockText.mid(line.textStart(), line.textLength());
            if (s.trimmed().isEmpty())
                continue;
            // naturalTextRect(), *not* position(): Qt stores every line at the layout's left edge and
            // applies the alignment offset inside QTextLine::draw(). Nothing here goes through draw(),
            // so reading position() silently left-aligns every bubble whatever the setting says.
            // naturalTextRect() is the accessor that has the offset already folded in.
            const QPointF p = lay->position() + line.naturalTextRect().topLeft();
            // addText places by the text *baseline*, which is the line's top plus its ascent.
            out.addText(origin.x() + p.x(), origin.y() + p.y() + line.ascent(), f, s);
        }
    }
    if (out.isEmpty())
        return out;

    // The paddings are the clip, so an overlong line cannot bleed past the stroke: on the sides and at the
    // top. Not at the bottom: lines the balloon has no room for go on out of it, never cut,
    // and the red text area says so. A text object and a picture's words keep their box as it was.
    // Intersecting keeps that in vector form, so it survives into the SVG.
    QRectF open = safe;
    if (seatOf(c, w))
        open.setBottom(qMax(safe.bottom(), out.boundingRect().bottom()));
    QPainterPath clip;
    clip.addRect(open);
    return out.intersected(clip);
}

ShadowPaths textShadowOf(const Words& w, const QPainterPath& text)
{
    if (text.isEmpty())
        return {};
    // A round band as wide as the outline, then one as wide as the spread, is one as wide as the two.
    Shadow s = w.text.shadow;
    s.spread = qMax(s.spread, 0.0) + qMax(w.text.outlineWidth, 0.0);
    return shadowOf(s, w.text.shadowFill, text);
}

QRectF textArea(const Composition& c, const Words& w)
{
    return textSafeArea(c, w);
}

namespace {

//! The text area of @p c's first words, the ones the editor fits; none without words.
QRectF areaOf(const Composition& c)
{
    const Words* w = wordsOf(c);
    return w ? textSafeArea(c, *w) : QRectF();
}

//! The lettering of @p c's first words; the default with none.
TextProperties letteringOf(const Composition& c)
{
    const Words* w = wordsOf(c);
    return w ? w->text : TextProperties();
}

} // namespace

QSizeF textBlockSize(const Composition& c)
{
    const TextProperties t = letteringOf(c);
    if (t.body.isEmpty())
        return {};
    // Never wrapped, so the block is the same whatever box it sits in: the widest line by the lines.
    QTextDocument doc;
    layOutText(doc, t, 1);
    return QSizeF(doc.idealWidth(), doc.size().height());
}

bool fits(const Composition& c, const QSizeF& block)
{
    if (block.isEmpty())
        return true;
    const QRectF area = areaOf(c);
    return block.width() <= area.width() + k_fitSlack && block.height() <= area.height() + k_fitSlack;
}

bool textOverflows(const Composition& c)
{
    return !fits(c, textBlockSize(c));
}

QSize fitBoxFor(const Composition& c, const QSizeF& block)
{
    if (block.isEmpty())
        return sizeOf(c);

    // The smallest box side, on one axis, whose text area holds @p need on that axis — the other side
    // held where it is. A text area grows with its own side for every shape (a thought balloon's lobes
    // and a scroll's rolls eat into it, but by less than the side grows), so a binary search is exact.
    const auto smallest = [](Composition probe, bool horizontal, qreal need) {
        const auto room = [&](int side) {
            QSize s = sizeOf(probe);
            if (horizontal)
                s.setWidth(side);
            else
                s.setHeight(side);
            resize(probe, s);
            const QRectF area = areaOf(probe);
            return (horizontal ? area.width() : area.height()) + k_fitSlack >= need;
        };
        int hi = qMax(k_minFitSize, qCeil(need) * 2);
        while (!room(hi) && hi < k_maxFitSize)
            hi *= 2;
        int lo = k_minFitSize;
        if (room(lo))
            return lo;   // the floor already holds it — a negative padding can do that
        while (hi - lo > 1) {
            const int mid = (lo + hi) / 2;
            (room(mid) ? hi : lo) = mid;
        }
        return hi;
    };

    // Each axis on its own, then again: in a thought balloon the lobes are sized by the *shorter* side,
    // so settling the height can take a little width back. Two or three rounds settle it; this is
    // headroom.
    Composition probe = c;
    for (int pass = 0; pass < k_fitPasses; ++pass) {
        const QSize before = sizeOf(probe);
        resize(probe, QSize(smallest(probe, true, block.width()), before.height()));
        resize(probe, QSize(sizeOf(probe).width(), smallest(probe, false, block.height())));
        if (sizeOf(probe) == before)
            break;
    }
    return sizeOf(probe);
}

QSize fittedBox(const Composition& c)
{
    return fitBoxFor(c, textBlockSize(c));
}

TextProperties paddingsToFit(const Composition& c, const QSizeF& block)
{
    const TextProperties text = letteringOf(c);
    TextProperties       out  = text;
    if (!c.balloon || !wordsOf(c) || block.isEmpty())
        return out;   // no outline, no padding to give

    // Each padding gives way in its own direction only: the side one is all that narrows the
    // area, the top one (mirrored at the bottom) all that shortens it. Within one direction the area
    // shrinks as the padding grows, on every shape, so the largest padding that still holds the block is
    // a binary search below the current one. A fix only ever lowers a padding.
    const auto lowered = [&](int TextProperties::* padding, bool across) {
        Composition probe = c;
        const qreal need  = across ? block.width() : block.height();
        const auto  holds = [&](int value) {
            wordsOf(probe)->text.*padding = value;
            const QRectF area             = areaOf(probe);
            return (across ? area.width() : area.height()) + k_fitSlack >= need;
        };
        int hi = text.*padding;
        if (holds(hi))
            return hi;                    // nothing to fix in this direction
        int lo = hi - k_maxFitSize;       // far below anything; the area outgrows any block there
        if (!holds(lo))
            return lo;
        while (hi - lo > 1) {
            const int mid = lo + (hi - lo) / 2;
            (holds(mid) ? lo : hi) = mid;
        }
        return lo;
    };
    out.paddingSide = lowered(&TextProperties::paddingSide, true);
    out.paddingTop  = lowered(&TextProperties::paddingTop, false);
    return out;
}

TextProperties fittingPaddings(const Composition& c)
{
    return paddingsToFit(c, textBlockSize(c));
}

QList<Handle> textAreaHandles(const Composition& c)
{
    if (!c.balloon || !wordsOf(c))
        return {};
    const QRectF  area = areaOf(c);
    const QPointF mid  = area.center();
    const auto arrow = [](TextAreaPart part, int index, const QPointF& at, const QPointF& axis) {
        Handle h;
        h.part  = int(part);
        h.index = index;
        h.at    = at;
        h.axis  = axis;
        h.glyph = Handle::Glyph::Arrow;
        return h;
    };
    return {arrow(TextAreaPart::Side, 0, QPointF(area.left(), mid.y()), QPointF(1, 0)),
            arrow(TextAreaPart::Side, 1, QPointF(area.right(), mid.y()), QPointF(-1, 0)),
            arrow(TextAreaPart::Top, 0, QPointF(mid.x(), area.top()), QPointF(0, 1))};
}

TextProperties dragTextAreaHandle(const Composition& c, const Handle& handle, const QPointF& to)
{
    // How far the edge moves inwards, up to its share of the area less its own minimum: the side padding
    // is one value and the top one mirrored, so either edge meets its partner at the middle.
    const TextProperties text = letteringOf(c);
    TextProperties       out  = text;
    const QRectF         area = areaOf(c);
    const auto           inBy = [](qreal moved, qreal extent) {
        return qRound(qMin(moved, qMax(qreal(0), extent / 2.0 - k_minTextHalf)));
    };
    if (partOf<TextAreaPart>(handle) == TextAreaPart::Side) {
        const qreal moved = handle.index == 0 ? to.x() - area.left() : area.right() - to.x();
        out.paddingSide   = text.paddingSide + inBy(moved, area.width());
    } else if (partOf<TextAreaPart>(handle) == TextAreaPart::Top) {
        out.paddingTop = text.paddingTop + inBy(to.y() - area.top(), area.height());
    }
    return out;
}

QPointF textAnchorFor(const Composition& c, const QSizeF& block)
{
    // The same placement textOutline() gives the block: each line aligned within the text area, the
    // block centred in it — or pinned to its top when it is taller than the area.
    const QRectF area  = areaOf(c);
    const int    align = letteringOf(c).align & Qt::AlignHorizontal_Mask;
    const qreal  x     = (align & Qt::AlignLeft)  ? area.left()
                       : (align & Qt::AlignRight) ? area.right()
                                                  : area.center().x();
    const qreal  y     = area.top() + qMax(qreal(0), (area.height() - block.height()) / 2.0)
                       + block.height() / 2.0;
    return {x, y};
}

QPointF textAnchor(const Composition& c)
{
    return textAnchorFor(c, textBlockSize(c));
}

}  // namespace Geometry
