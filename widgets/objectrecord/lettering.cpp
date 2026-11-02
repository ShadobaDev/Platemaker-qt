// The lettering: the words set in their font and turned into outlines, the rectangle they may occupy,
// and fitting a box to them or them to a box. Where a shape leaves room for words is its own business
// (shapes.cpp); the stroke and the author's padding are taken off here, the same for every shape.

#include "geometry.hpp"
#include "shapes.hpp"

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
 * @brief The rectangle text may occupy — the shape's own text area (ShapeInfo::safeArea), inset so
 *        letters do not touch the stroke, and by the author's paddings: the side one left and right, the
 *        top one at the top and, mirrored, at the bottom, which the block is centred between.
 */
QRectF textSafeArea(const ObjectRecord& a, const QRectF& body)
{
    if (!a.hasSilhouette())
        return QRectF(0, 0, a.box.width(), a.box.height());

    // The author's paddings (TextProperties), on top of the stroke. Negative widens the area past the
    // outline — an impact balloon's lettering bursting out of it.
    // An edge's teeth go in from the outline, so the area gives up their depth as well.
    const qreal  inset  = a.style.strokeWidth + edgeInset(a.shape);
    const qreal  side   = inset + a.text.paddingSide;
    const qreal  top    = inset + a.text.paddingTop;
    const QRectF usable = shapeInfo(a.shape.kind).safeArea(a.shape, body);
    return usable.adjusted(side, top, -side, -top);
}

/**
 * @brief The laid-out text, ready to draw or measure.
 *
 * **Never wrapped.** The lines are the author's line breaks, exactly — lettering is broken by hand, and
 * a long word grows its box rather than being split. @p width only places each line according to the
 * alignment; a line wider than it overflows, it is not re-broken.
 */
void layOutText(QTextDocument& doc, const ObjectRecord& a, qreal width)
{
    // A family that is not installed is not handed to QFont: QFont would substitute through the platform's
    // own table (on Windows, "MS Shell Dlg 2" → Tahoma), which is a different stand-in from the one an
    // empty family gets. Leaving it unset gives both the application's default font, so "no font chosen"
    // and "the chosen font is missing" look the same, and Geometry::fontFallback() can name it.
    QFont f;
    if (!a.text.family.isEmpty() && QFontDatabase::hasFamily(a.text.family))
        f.setFamily(a.text.family);
    f.setPixelSize(qMax(1, a.text.pixelSize));
    f.setBold(a.text.bold);

    QTextOption opt;
    opt.setAlignment(static_cast<Qt::Alignment>(a.text.align));
    opt.setWrapMode(QTextOption::NoWrap);

    doc.setDefaultFont(f);
    doc.setDefaultTextOption(opt);
    doc.setDocumentMargin(0);
    doc.setPlainText(a.text.body);

    // The font's own pitch is left exactly as it is at 100 %: a proportional height of 100 is not the
    // same arithmetic as the default single height, and the default is what every bubble so far used.
    if (a.text.lineSpacing != 100) {
        QTextBlockFormat pitch;
        pitch.setLineHeight(qMax(1, a.text.lineSpacing), QTextBlockFormat::ProportionalHeight);
        QTextCursor all(&doc);
        all.select(QTextCursor::Document);
        all.mergeBlockFormat(pitch);
    }
    doc.setTextWidth(qMax(qreal(1), width));
}

} // namespace

QString fontFallback(const ObjectRecord& a)
{
    if (a.text.body.isEmpty() || a.text.family.isEmpty() || QFontDatabase::hasFamily(a.text.family))
        return {};
    // The stand-in layOutText() uses: the application's default font, never the platform's substitution.
    return defaultFamily();
}

QString defaultFamily()
{
    return QFontInfo(QFont()).family();
}

QPainterPath textOutline(const ObjectRecord& a)
{
    if (a.text.body.isEmpty())
        return {};

    const QRectF body = balloonRect(a);
    const QRectF safe = textSafeArea(a, body);
    if (safe.width() <= 1 || safe.height() <= 1)
        return {};

    QTextDocument doc;
    layOutText(doc, a, safe.width());

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
    // and the red text area says so. A text object and a picture's words keep their box as it was (C10).
    // Intersecting keeps that in vector form, so it survives into the SVG.
    QRectF open = safe;
    if (a.hasSilhouette())
        open.setBottom(qMax(safe.bottom(), out.boundingRect().bottom()));
    QPainterPath clip;
    clip.addRect(open);
    return out.intersected(clip);
}

QRectF textArea(const ObjectRecord& a)
{
    return textSafeArea(a, balloonRect(a));
}

QSizeF textBlockSize(const ObjectRecord& a)
{
    if (a.text.body.isEmpty())
        return {};
    // Never wrapped, so the block is the same whatever box it sits in: the widest line by the lines.
    QTextDocument doc;
    layOutText(doc, a, 1);
    return QSizeF(doc.idealWidth(), doc.size().height());
}

bool fits(const ObjectRecord& a, const QSizeF& block)
{
    if (block.isEmpty())
        return true;
    const QRectF area = textArea(a);
    return block.width() <= area.width() + k_fitSlack && block.height() <= area.height() + k_fitSlack;
}

bool textOverflows(const ObjectRecord& a)
{
    return !fits(a, textBlockSize(a));
}

QSize fitBoxFor(const ObjectRecord& a, const QSizeF& block)
{
    if (block.isEmpty())
        return a.box;

    // The smallest box side, on one axis, whose text area holds @p need on that axis — the other side
    // held where it is. A text area grows with its own side for every shape (a thought balloon's lobes
    // and a scroll's rolls eat into it, but by less than the side grows), so a binary search is exact.
    const auto smallest = [](ObjectRecord probe, bool horizontal, qreal need) {
        const auto room = [&](int side) {
            if (horizontal)
                probe.box.setWidth(side);
            else
                probe.box.setHeight(side);
            const QRectF area = textArea(probe);
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
    ObjectRecord probe = a;
    for (int pass = 0; pass < k_fitPasses; ++pass) {
        const QSize before = probe.box;
        probe.box.setWidth(smallest(probe, true, block.width()));
        probe.box.setHeight(smallest(probe, false, block.height()));
        if (probe.box == before)
            break;
    }
    return probe.box;
}

QSize fittedBox(const ObjectRecord& a)
{
    return fitBoxFor(a, textBlockSize(a));
}

TextProperties paddingsToFit(const ObjectRecord& a, const QSizeF& block)
{
    TextProperties out = a.text;
    if (!a.hasSilhouette() || block.isEmpty())
        return out;   // no outline, no padding to give

    // Each padding gives way in its own direction only: the side one is all that narrows the
    // area, the top one (mirrored at the bottom) all that shortens it. Within one direction the area
    // shrinks as the padding grows, on every shape, so the largest padding that still holds the block is
    // a binary search below the current one. A fix only ever lowers a padding.
    const auto lowered = [&](int TextProperties::* padding, bool across) {
        ObjectRecord probe = a;
        const qreal  need  = across ? block.width() : block.height();
        const auto   holds = [&](int value) {
            probe.text.*padding = value;
            const QRectF area   = textArea(probe);
            return (across ? area.width() : area.height()) + k_fitSlack >= need;
        };
        int hi = a.text.*padding;
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

TextProperties fittingPaddings(const ObjectRecord& a)
{
    return paddingsToFit(a, textBlockSize(a));
}

QList<ShapeHandle> textAreaHandles(const ObjectRecord& a)
{
    if (!a.hasSilhouette())
        return {};
    const QRectF  area = textArea(a);
    const QPointF mid  = area.center();
    return {{ShapeHandle::Role::TextSide, 0, QPointF(area.left(), mid.y()), QPointF(1, 0)},
            {ShapeHandle::Role::TextSide, 1, QPointF(area.right(), mid.y()), QPointF(-1, 0)},
            {ShapeHandle::Role::TextTop, 0, QPointF(mid.x(), area.top()), QPointF(0, 1)}};
}

TextProperties dragTextAreaHandle(const ObjectRecord& a, const ShapeHandle& handle, const QPointF& to)
{
    // How far the edge moves inwards, up to its share of the area less its own minimum: the side padding
    // is one value and the top one mirrored, so either edge meets its partner at the middle.
    TextProperties out   = a.text;
    const QRectF   area  = textArea(a);
    const auto     inBy  = [](qreal moved, qreal extent) {
        return qRound(qMin(moved, qMax(qreal(0), extent / 2.0 - k_minTextHalf)));
    };
    if (handle.role == ShapeHandle::Role::TextSide) {
        const qreal moved = handle.index == 0 ? to.x() - area.left() : area.right() - to.x();
        out.paddingSide   = a.text.paddingSide + inBy(moved, area.width());
    } else if (handle.role == ShapeHandle::Role::TextTop) {
        out.paddingTop = a.text.paddingTop + inBy(to.y() - area.top(), area.height());
    }
    return out;
}

QPointF textAnchorFor(const ObjectRecord& a, const QSizeF& block)
{
    // The same placement textOutline() gives the block: each line aligned within the text area, the
    // block centred in it — or pinned to its top when it is taller than the area.
    const QRectF area  = textArea(a);
    const int    align = a.text.align & Qt::AlignHorizontal_Mask;
    const qreal  x     = (align & Qt::AlignLeft)  ? area.left()
                       : (align & Qt::AlignRight) ? area.right()
                                                  : area.center().x();
    const qreal  y     = area.top() + qMax(qreal(0), (area.height() - block.height()) / 2.0)
                       + block.height() / 2.0;
    return {x, y};
}

QPointF textAnchor(const ObjectRecord& a)
{
    return textAnchorFor(a, textBlockSize(a));
}

}  // namespace Geometry
