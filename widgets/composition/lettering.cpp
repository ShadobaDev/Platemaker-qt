// The lettering: the words set in their font and turned into outlines, the frame they are laid in and where
// the block sits in it, and fitting a box to them. The frame is the words' own (Words::frame): automatic edges
// reach the body's box less the stroke and the edge's teeth, the same for every shape.

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
 * \brief Where an automatic edge of words seated in @p seat stops: the body's box less the stroke and the edge's
 *        teeth (which go in from the outline). The anchor is measured on it too.
 */
QRectF innerBox(const Balloon& b, const Body& seat)
{
    const qreal inset = b.style.line.strokeWidth / 2.0 + edgeInset(seat.shape);
    return balloonRect(b, seat).adjusted(inset, inset, -inset, -inset);
}

//! The point @p w are placed by: their anchor in their seat, or the middle of their own box.
QPointF anchorOf(const Composition& c, const Words& w)
{
    const Body* seat = seatOf(c, w);
    if (!seat)
        return QRectF(w.place.pos, QSizeF(w.box)).center();
    const QRectF inner = innerBox(*c.balloon, *seat);
    return inner.topLeft() + QPointF(w.anchor.x() * inner.width(), w.anchor.y() * inner.height());
}

QRectF textSafeArea(const Composition& c, const Words& w)
{
    const Body* seat = seatOf(c, w);
    if (!seat)
        return QRectF(w.place.pos, QSizeF(w.box));
    const QRectF  inner = innerBox(*c.balloon, *seat);
    const QPointF a     = anchorOf(c, w);
    const Frame&  f     = w.frame;
    return QRectF(QPointF(f.left ? a.x() - *f.left : inner.left(), f.top ? a.y() - *f.top : inner.top()),
                  QPointF(f.right ? a.x() + *f.right : inner.right(), f.bottom ? a.y() + *f.bottom : inner.bottom()));
}

//! The block laid in @p w's frame, as blockRect() says, from the anchor and the alignment.
QRectF placedBlock(const Composition& c, const Words& w, const QSizeF& block)
{
    const QRectF  frame = textSafeArea(c, w);
    const QPointF a     = anchorOf(c, w);
    const int     align = w.text.align & Qt::AlignHorizontal_Mask;
    const qreal   x     = (align & Qt::AlignLeft)  ? frame.left()
                        : (align & Qt::AlignRight) ? frame.right() - block.width()
                                                   : a.x() - block.width() / 2.0;
    // A block taller than its frame stops at the frame's top and goes on downwards.
    const qreal y = block.height() > frame.height() ? frame.top() : a.y() - block.height() / 2.0;
    return QRectF(QPointF(x, y), block);
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

    // Laid out as wide as its widest line, so each line is aligned within the block, and the block placed by the
    // anchor and the alignment (placedBlock()).
    QTextDocument doc;
    layOutText(doc, w.text, 1);
    layOutText(doc, w.text, doc.idealWidth());
    const QPointF origin = placedBlock(c, w, doc.size()).topLeft();

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

    // The frame is the clip, so an overlong line cannot bleed past it: on the sides and at the top. Not at the
    // bottom: lines the balloon has no room for go on out of it, never cut,
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

QRectF blockRect(const Composition& c, const Words& w, const QSizeF& block)
{
    return placedBlock(c, w, block);
}

namespace {

//! Whether @p block, placed for @p c's first words, lies in their frame across (@p horizontal) or up and down.
bool holds(const Composition& c, const QSizeF& block, bool horizontal)
{
    const Words* w = wordsOf(c);
    if (!w)
        return true;
    const QRectF frame = textSafeArea(c, *w);
    const QRectF r     = placedBlock(c, *w, block);
    // The slack once for the size, and once for each side the block may round past.
    return horizontal ? r.width() <= frame.width() + k_fitSlack && r.left() >= frame.left() - k_fitSlack
                            && r.right() <= frame.right() + k_fitSlack
                      : r.height() <= frame.height() + k_fitSlack && r.top() >= frame.top() - k_fitSlack
                            && r.bottom() <= frame.bottom() + k_fitSlack;
}

} // namespace

bool fits(const Composition& c, const QSizeF& block)
{
    if (block.isEmpty())
        return true;
    return holds(c, block, true) && holds(c, block, false);
}

bool textOverflows(const Composition& c)
{
    return !fits(c, textBlockSize(c));
}

QSize fitBoxFor(const Composition& c, const QSizeF& block)
{
    if (block.isEmpty())
        return sizeOf(c);

    // The smallest box side, on one axis, whose frame holds the placed block on that axis — the other side
    // held where it is. An automatic edge and the anchor move with the side, so a binary search is exact; an
    // axis whose set edges are too close for any side keeps the side it has.
    const auto smallest = [&block](Composition probe, bool horizontal) {
        const int  was  = horizontal ? sizeOf(probe).width() : sizeOf(probe).height();
        const auto room = [&](int side) {
            QSize s = sizeOf(probe);
            if (horizontal)
                s.setWidth(side);
            else
                s.setHeight(side);
            resize(probe, s);
            return holds(probe, block, horizontal);
        };
        const qreal need = horizontal ? block.width() : block.height();
        int         hi   = qMax(k_minFitSize, qCeil(need) * 2);
        while (!room(hi) && hi < k_maxFitSize)
            hi *= 2;
        if (!room(hi))
            return was;
        int lo = k_minFitSize;
        if (room(lo))
            return lo;   // the floor already holds it
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
        resize(probe, QSize(smallest(probe, true), before.height()));
        resize(probe, QSize(sizeOf(probe).width(), smallest(probe, false)));
        if (sizeOf(probe) == before)
            break;
    }
    return sizeOf(probe);
}

QSize fittedBox(const Composition& c)
{
    return fitBoxFor(c, textBlockSize(c));
}

QList<Handle> textAreaHandles(const Composition& c)
{
    const Words* w = wordsOf(c);
    if (!c.balloon || !w || !seatOf(c, *w))
        return {};
    const QRectF  area = textSafeArea(c, *w);
    const QPointF a    = anchorOf(c, *w);
    const auto arrow = [](TextAreaPart part, int index, const QPointF& at, const QPointF& axis) {
        Handle h;
        h.part  = int(part);
        h.index = index;
        h.at    = at;
        h.axis  = axis;
        h.glyph = Handle::Glyph::Arrow;
        return h;
    };
    return {arrow(TextAreaPart::Side, 0, QPointF(area.left(), a.y()), QPointF(1, 0)),
            arrow(TextAreaPart::Side, 1, QPointF(area.right(), a.y()), QPointF(-1, 0)),
            arrow(TextAreaPart::Top, 0, QPointF(a.x(), area.top()), QPointF(0, 1))};
}

Words dragTextAreaHandle(const Composition& c, const Handle& handle, const QPointF& to)
{
    const Words* w = wordsOf(c);
    if (!w)
        return {};
    Words         out = *w;
    const QPointF a   = anchorOf(c, *w);
    // Whole pixels: what the file says is what the author placed, not the pointer's last fraction.
    const auto from = [](qreal distance) { return qreal(qMax(qRound(distance), qCeil(k_minTextHalf))); };
    if (partOf<TextAreaPart>(handle) == TextAreaPart::Side) {
        if (handle.index == 0)
            out.frame.left = from(a.x() - to.x());
        else
            out.frame.right = from(to.x() - a.x());
    } else if (partOf<TextAreaPart>(handle) == TextAreaPart::Top) {
        out.frame.top = from(a.y() - to.y());
    }
    return out;
}

QPointF textAnchorFor(const Composition& c, const QSizeF& block)
{
    const Words* w = wordsOf(c);
    if (!w)
        return {};
    const QRectF area  = textSafeArea(c, *w);
    const int    align = w->text.align & Qt::AlignHorizontal_Mask;
    const qreal  x     = (align & Qt::AlignLeft)  ? area.left()
                       : (align & Qt::AlignRight) ? area.right()
                                                  : anchorOf(c, *w).x();
    return {x, placedBlock(c, *w, block).center().y()};
}

QPointF textAnchor(const Composition& c)
{
    return textAnchorFor(c, textBlockSize(c));
}

}  // namespace Geometry
