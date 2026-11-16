// The lettering: the words set in their font and turned into outlines, the frame they are laid in and where
// the block sits in it, and fitting a box to them. The frame is the words' own (Words::frame): automatic edges
// reach their seat's inner box: a body's box less the stroke and the edge's teeth, the same for every shape, or a
// picture's whole box.

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
#include <QTextFragment>
#include <QTextLayout>
#include <QTextOption>
#include <QtMath>

#include <algorithm>
#include <optional>

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
 *  The narrowest text area an arrow can drag it to, in balloon pixels: an edge stops this short of the
 *  opposite one, so the area never turns inside out (dragTextAreaHandle()).
 */
constexpr qreal k_minTextSpan   = 2.0;
/**
 *  The character format property a run's letters carry (layOutText()): the run's index in TextProperties::runs.
 */
constexpr int   k_runProperty   = QTextFormat::UserProperty;

/**
 * \brief Where an automatic edge of @p w stops, and what a set one is measured in from: their body's box less the
 *        stroke and the edge's teeth (which go in from the outline), or the picture's whole box. None with no seat.
 */
std::optional<QRectF> seatBox(const Composition& c, const Words& w)
{
    if (const Body* body = seatOf(c, w)) {
        const qreal inset = c.balloon->style.line.strokeWidth / 2.0 + edgeInset(body->shape);
        return balloonRect(*c.balloon, *body).adjusted(inset, inset, -inset, -inset);
    }
    if (seated(c, w))
        return QRectF(c.picture->place.pos, QSizeF(c.picture->size));
    return std::nullopt;
}

QRectF textSafeArea(const Composition& c, const Words& w)
{
    const std::optional<QRectF> seat = seatBox(c, w);
    if (!seat)
        return QRectF(w.place.pos, QSizeF(w.box));
    const Frame& f = w.frame;
    return seat->adjusted(f.left.value_or(0.0), f.top.value_or(0.0), -f.right.value_or(0.0), -f.bottom.value_or(0.0));
}

//! The point of @p w's frame the block is aligned to: a side or the middle across, the top, middle or bottom
//! up and down.
QPointF alignedPoint(const Composition& c, const Words& w)
{
    const QRectF frame = textSafeArea(c, w);
    const int    h     = w.text.align & Qt::AlignHorizontal_Mask;
    const int    v     = w.text.valign & Qt::AlignVertical_Mask;
    return {(h & Qt::AlignLeft) ? frame.left() : (h & Qt::AlignRight) ? frame.right() : frame.center().x(),
            (v & Qt::AlignTop) ? frame.top() : (v & Qt::AlignBottom) ? frame.bottom() : frame.center().y()};
}

//! The block laid in @p w's frame, as blockRect() says: aligned to it on both axes.
QRectF placedBlock(const Composition& c, const Words& w, const QSizeF& block)
{
    const QPointF p = alignedPoint(c, w);
    const int     h = w.text.align & Qt::AlignHorizontal_Mask;
    const int     v = w.text.valign & Qt::AlignVertical_Mask;
    // The share of the block before the aligned point: none from a left or top, all of it to a right or bottom.
    const auto before = [](bool start, bool end) { return start ? 0.0 : end ? 1.0 : 0.5; };
    return QRectF(QPointF(p.x() - before(h & Qt::AlignLeft, h & Qt::AlignRight) * block.width(),
                          p.y() - before(v & Qt::AlignTop, v & Qt::AlignBottom) * block.height()),
                  block);
}

/**
 * \brief The laid-out text, ready to draw or measure.
 *
 * **Never wrapped.** The lines are the author's line breaks, exactly — lettering is broken by hand, and
 * a long word grows its box rather than being split. @p width only places each line according to the
 * alignment; a line wider than it overflows, it is not re-broken.
 */
//! A family set as a QFont is: only when installed (layOutText()).
void setFamily(QFont& f, const QString& family)
{
    if (!family.isEmpty() && QFontDatabase::hasFamily(family))
        f.setFamily(family);
}

void layOutText(QTextDocument& doc, const TextProperties& t, qreal width)
{
    // A family that is not installed is not handed to QFont: QFont would substitute through the platform's
    // own table (on Windows, "MS Shell Dlg 2" → Tahoma), which is a different stand-in from the one an
    // empty family gets. Leaving it unset gives both the application's default font, so "no font chosen"
    // and "the chosen font is missing" look the same, and Geometry::fontFallback() can name it.
    QFont f;
    setFamily(f, t.family);
    f.setPixelSize(qMax(1, t.pixelSize));
    f.setBold(t.bold);
    f.setItalic(t.italic);

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

    // Each run's letters in their own font, whole (what it leaves out the words'), and marked with the run.
    for (qsizetype i = 0; i < t.runs.size(); ++i) {
        const TextRun& r    = t.runs[i];
        QFont          font = f;
        if (r.family) {
            font = QFont();   // an empty or missing family is the default, as the words' is
            setFamily(font, *r.family);
            font.setPixelSize(f.pixelSize());
            font.setBold(f.bold());
            font.setItalic(f.italic());
        }
        if (r.pixelSize)
            font.setPixelSize(qMax(1, *r.pixelSize));
        if (r.bold)
            font.setBold(*r.bold);
        if (r.italic)
            font.setItalic(*r.italic);
        QTextCharFormat format;
        format.setFont(font);
        format.setProperty(k_runProperty, int(i));
        QTextCursor run(&doc);
        run.setPosition(r.from);
        run.setPosition(r.from + r.length, QTextCursor::KeepAnchor);
        run.mergeCharFormat(format);
    }
    doc.setTextWidth(qMax(qreal(1), width));
}

} // namespace

QString fontFallback(const Words& w)
{
    const TextProperties& t = w.text;
    if (t.body.isEmpty())
        return {};
    const auto missing = [](const QString& family) { return !family.isEmpty() && !QFontDatabase::hasFamily(family); };
    const bool any     = missing(t.family) || std::any_of(t.runs.cbegin(), t.runs.cend(), [&](const TextRun& r) {
                         return r.family && missing(*r.family);
                     });
    // The stand-in layOutText() uses: the application's default font, never the platform's substitution.
    return any ? defaultFamily() : QString();
}

QString defaultFamily()
{
    return QFontInfo(QFont()).family();
}

namespace {

/**
 * \brief @p w's letters as outlines, clipped as textOutline() says; with @p fills, the same letters also by the fill
 *        they wear: a run's index for a run that sets one, -1 for the words' own.
 */
QPainterPath letters(const Composition& c, const Words& w, TextFills* fills)
{
    if (w.text.body.isEmpty())
        return {};

    const QRectF safe = textSafeArea(c, w);
    if (safe.width() <= 1 || safe.height() <= 1)
        return {};

    // Laid out as wide as its widest line, so each line is aligned within the block, and the block placed by the
    // alignment in its frame (placedBlock()).
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
    // Runs set some letters in other fonts: each fragment of a line in its own, placed where the layout put it.
    if (!w.text.runs.isEmpty()) {
        for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
            const QTextLayout* lay = b.layout();
            if (!lay)
                continue;
            const QString blockText = b.text();
            for (int i = 0; i < lay->lineCount(); ++i) {
                const QTextLine line  = lay->lineAt(i);
                const int       start = line.textStart();
                const int       end   = start + line.textLength();
                for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
                    const QTextFragment fragment = it.fragment();
                    const int           from     = qMax(start, fragment.position() - b.position());
                    const int           to       = qMin(end, fragment.position() - b.position() + fragment.length());
                    const QString       s        = blockText.mid(from, to - from);
                    if (to <= from || s.trimmed().isEmpty())
                        continue;
                    const QTextCharFormat format = fragment.charFormat();
                    const bool            inRun  = format.hasProperty(k_runProperty);
                    const QFont           font   = inRun ? format.font() : f;
                    // cursorToX() has the alignment's offset folded in, as naturalTextRect() has.
                    const QPointF at(origin.x() + lay->position().x() + line.cursorToX(from),
                                     origin.y() + lay->position().y() + line.y() + line.ascent());
                    QPainterPath piece;
                    piece.addText(at, font, s);
                    out.addPath(piece);
                    if (fills) {
                        const int run = inRun ? format.property(k_runProperty).toInt() : -1;
                        (*fills)[run >= 0 && w.text.runs.at(run).fill ? run : -1].addPath(piece);
                    }
                }
            }
        }
    }
    for (QTextBlock b = doc.begin(); b.isValid() && w.text.runs.isEmpty(); b = b.next()) {
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

    // The frame is the clip, so an overlong line cannot bleed past its sides. Not up and down: lines the frame
    // has no room for go on past it, away from the side they are aligned to, never cut, and the red text area
    // says so. A text object keeps its box as it was.
    // Intersecting keeps that in vector form, so it survives into the SVG.
    QRectF open = safe;
    if (seated(c, w)) {
        const QRectF drawn = out.boundingRect();
        open.setTop(qMin(safe.top(), drawn.top()));
        open.setBottom(qMax(safe.bottom(), drawn.bottom()));
    }
    QPainterPath clip;
    clip.addRect(open);
    if (fills)
        for (QPainterPath& piece : *fills)
            piece = piece.intersected(clip);
    return out.intersected(clip);
}

} // namespace

QPainterPath textOutline(const Composition& c, const Words& w)
{
    return letters(c, w, nullptr);
}

TextFills textFills(const Composition& c, const Words& w)
{
    const bool filled = std::any_of(w.text.runs.cbegin(), w.text.runs.cend(), [](const TextRun& r) { return bool(r.fill); });
    if (!filled)
        return {};
    TextFills out;
    letters(c, w, &out);
    return out;
}

QList<TextFills> textFillsOf(const Composition& c)
{
    QList<TextFills> out;
    for (const Words& w : c.words)
        out.append(textFills(c, w));
    return out;
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

QList<QPainterPath> textOutlines(const Composition& c)
{
    QList<QPainterPath> out;
    for (const Words& w : c.words)
        out.append(textOutline(c, w));
    return out;
}

QSizeF textBlockSize(const Words& w)
{
    const TextProperties& t = w.text;
    if (t.body.isEmpty())
        return {};
    // Never wrapped, so the block is the same whatever box it sits in: the widest line by the lines.
    QTextDocument doc;
    layOutText(doc, t, 1);
    return QSizeF(doc.idealWidth(), doc.size().height());
}

Blocks textBlocks(const Composition& c)
{
    Blocks out;
    for (const Words& w : c.words)
        out.append(textBlockSize(w));
    return out;
}

QRectF blockRect(const Composition& c, const Words& w, const QSizeF& block)
{
    return placedBlock(c, w, block);
}

namespace {

//! Whether @p block, placed for @p w, lies in their frame across (@p horizontal) or up and down. True when empty.
bool holdsOne(const Composition& c, const Words& w, const QSizeF& block, bool horizontal)
{
    if (block.isEmpty())
        return true;
    const QRectF frame = textSafeArea(c, w);
    const QRectF r     = placedBlock(c, w, block);
    // The slack once for the size, and once for each side the block may round past.
    return horizontal ? r.width() <= frame.width() + k_fitSlack && r.left() >= frame.left() - k_fitSlack
                            && r.right() <= frame.right() + k_fitSlack
                      : r.height() <= frame.height() + k_fitSlack && r.top() >= frame.top() - k_fitSlack
                            && r.bottom() <= frame.bottom() + k_fitSlack;
}

//! Whether every words of @p c holds its block of @p blocks (by index) across (@p horizontal) or up and down.
bool holds(const Composition& c, const Blocks& blocks, bool horizontal)
{
    for (qsizetype i = 0; i < c.words.size() && i < blocks.size(); ++i)
        if (!holdsOne(c, c.words[i], blocks[i], horizontal))
            return false;
    return true;
}

//! Whether @p block, placed for @p w, lies inside the outline of their body; true with no outline or no block.
bool liesInOutlineOne(const Composition& c, const Words& w, const QSizeF& block)
{
    const Body* seat = seatOf(c, w);
    if (block.isEmpty() || !seat)
        return true;
    const auto outline = shapeInfo(seat->shape.kind).outline;
    if (!outline)
        return true;   // no outline to lie in: a text object's own box
    // The bare outline, the block kept the stroke and the teeth away from it: grown by them, its corners round.
    const qreal  inset = c.balloon->style.line.strokeWidth / 2.0 + edgeInset(seat->shape);
    const QRectF r     = placedBlock(c, w, block).adjusted(k_fitSlack, k_fitSlack, -k_fitSlack, -k_fitSlack);
    QPainterPath kept;
    kept.addRoundedRect(r.adjusted(-inset, -inset, inset, inset), inset, inset);
    return outline(seat->shape, balloonRect(*c.balloon, *seat)).contains(kept);
}

} // namespace

bool fits(const Composition& c, const Blocks& blocks)
{
    return holds(c, blocks, true) && holds(c, blocks, false);
}

bool textOverflows(const Composition& c, const Words& w)
{
    const QSizeF block = textBlockSize(w);
    return !holdsOne(c, w, block, true) || !holdsOne(c, w, block, false);
}

bool liesInOutline(const Composition& c, const Blocks& blocks)
{
    for (qsizetype i = 0; i < c.words.size() && i < blocks.size(); ++i)
        if (!liesInOutlineOne(c, c.words[i], blocks[i]))
            return false;
    return true;
}

QSize fitBoxFor(const Composition& c, const Blocks& blocks)
{
    QSizeF most;   // the largest block on each axis: where the search for a side that holds them starts
    for (const QSizeF& b : blocks)
        if (!b.isEmpty())
            most = most.expandedTo(b);
    if (most.isEmpty())
        return sizeOf(c);

    // The smallest box side, on one axis, whose frame holds the placed block on that axis — the other side
    // held where it is. Every edge keeps its distance from the side, so a binary search is exact; an
    // axis whose set edges are too close for any side keeps the side it has.
    const auto smallest = [&blocks, &most](Composition probe, bool horizontal) {
        const int  was  = horizontal ? sizeOf(probe).width() : sizeOf(probe).height();
        const auto room = [&](int side) {
            QSize s = sizeOf(probe);
            if (horizontal)
                s.setWidth(side);
            else
                s.setHeight(side);
            resize(probe, s);
            return holds(probe, blocks, horizontal);
        };
        const qreal need = horizontal ? most.width() : most.height();
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

    // Held by the frame, the block may still cross a curved outline (an ellipse's corners, a box's rounded ones)
    // or one its set edges reach past: then the box grows, its proportions kept, until the block lies inside it.
    const QSize framed = sizeOf(probe);
    if (liesInOutline(probe, blocks) || framed.isEmpty())
        return framed;
    const auto inside = [&](int width) {
        resize(probe, QSize(width, qCeil(qreal(framed.height()) * width / framed.width())));
        return liesInOutline(probe, blocks);
    };
    int lo = framed.width();
    int hi = lo * 2;
    while (!inside(hi) && hi < k_maxFitSize)
        hi *= 2;
    if (!inside(hi))
        return framed;   // no size will do: the frame's fit is the best there is
    while (hi - lo > 1) {
        const int mid = (lo + hi) / 2;
        (inside(mid) ? hi : lo) = mid;
    }
    inside(hi);

    // Grown in proportion, one side may have room to spare: each side on its own, down to the smallest that still
    // holds the block in the frame and inside the outline, the other held where it is.
    const auto tighten = [&](bool horizontal) {
        const QSize at   = sizeOf(probe);
        const auto  room = [&](int side) {
            resize(probe, horizontal ? QSize(side, at.height()) : QSize(at.width(), side));
            return fits(probe, blocks) && liesInOutline(probe, blocks);
        };
        int low  = k_minFitSize;
        int high = horizontal ? at.width() : at.height();
        if (room(low))
            return;   // the floor holds it, and room() left it there
        while (high - low > 1) {
            const int mid = (low + high) / 2;
            (room(mid) ? high : low) = mid;
        }
        room(high);
    };
    tighten(true);
    tighten(false);
    return sizeOf(probe);
}

QSize fittedBox(const Composition& c)
{
    return fitBoxFor(c, textBlocks(c));
}

namespace {

/**
 * \brief The frame @p was (in the composition's coordinates) of words whose block lies at @p block (none for
 *        empty words), measured again from the seat @p seat, in the same coordinates: an edge past the seat stops at it, and the block keeps its place in it.
 *
 * Along an axis the frame is first the old one, reaching the block where the block overflows it and stopped by the
 * seat; then cut down so the block's alignment puts it where it is (its side on the block's, its middle the block's).
 * An edge on the seat's own is automatic.
 */
Frame frameKeeping(const QRectF& was, const std::optional<QRectF>& block, int align, int valign,
                   const QRectF& seat)
{
    const auto axis = [&](qreal lo, qreal hi, std::optional<qreal> blockLo, std::optional<qreal> blockHi, bool start,
                          bool end, qreal seatLo, qreal seatHi) {
        if (blockLo) {
            lo = qMin(lo, *blockLo);
            hi = qMax(hi, *blockHi);
        }
        lo = qMax(lo, seatLo);
        hi = qMin(hi, seatHi);
        if (blockLo) {
            if (start)
                lo = *blockLo;
            else if (end)
                hi = *blockHi;
            else {
                const qreal mid  = (*blockLo + *blockHi) / 2.0;
                const qreal half = qMin(mid - lo, hi - mid);
                lo               = mid - half;
                hi               = mid + half;
            }
        }
        const auto inset = [](qreal d) { return d > k_fitSlack ? std::optional<qreal>(d) : std::nullopt; };
        return std::pair{inset(lo - seatLo), inset(seatHi - hi)};
    };
    const int h = align & Qt::AlignHorizontal_Mask;
    const int v = valign & Qt::AlignVertical_Mask;
    const auto [left, right] =
        axis(was.left(), was.right(), block ? std::optional(block->left()) : std::nullopt,
             block ? std::optional(block->right()) : std::nullopt, h & Qt::AlignLeft, h & Qt::AlignRight, seat.left(),
             seat.right());
    const auto [top, bottom] =
        axis(was.top(), was.bottom(), block ? std::optional(block->top()) : std::nullopt,
             block ? std::optional(block->bottom()) : std::nullopt, v & Qt::AlignTop, v & Qt::AlignBottom, seat.top(),
             seat.bottom());
    return Frame{left, top, right, bottom};
}

} // namespace

Composition fitToBlocks(const Composition& c, const Blocks& blocks)
{
    const Body* body = bodyOf(c);
    if (!body) {
        Composition out = c;
        resize(out, fitBoxFor(c, blocks));
        return out;
    }

    // The blocks as they lie now, and the rectangle they lie in together.
    QList<std::optional<QRectF>> placed;
    QRectF                       all;
    for (qsizetype i = 0; i < c.words.size(); ++i) {
        const Words& w     = c.words[i];
        const QSizeF block = i < blocks.size() ? blocks[i] : QSizeF();
        if (seatOf(c, w) != body || block.isEmpty()) {
            placed.append(std::nullopt);
            continue;
        }
        placed.append(placedBlock(c, w, block));
        all = all.isNull() ? *placed.last() : all.united(*placed.last());
    }
    if (all.isNull())
        return c;

    // A body of @p size whose seat (inner box) has its middle on the blocks', every frame measured from that seat.
    const qreal inset = c.balloon->style.line.strokeWidth / 2.0 + edgeInset(body->shape);
    const auto  sized = [&](const QSize& size) {
        Composition out = c;
        resize(out, size);
        const QSizeF inner(size.width() - 2 * inset, size.height() - 2 * inset);
        const QRectF seat(all.center() - QPointF(inner.width(), inner.height()) / 2.0, inner);
        for (qsizetype i = 0; i < c.words.size(); ++i) {
            const Words& w = c.words[i];
            if (seatOf(c, w) == body)
                out.words[i].frame = frameKeeping(textSafeArea(c, w), placed[i], w.text.align,
                                                  w.text.valign, seat);
        }
        return out;
    };
    const auto side = [&](qreal span) { return qMax(k_minFitSize, qCeil(span + 2 * inset)); };
    const QSize framed(side(all.width()), side(all.height()));
    Composition probe = sized(framed);
    if (liesInOutline(probe, blocks))
        return probe;

    // A curved outline (an ellipse's corners, a box's rounded ones) cuts a block: the body grows, its proportions
    // kept, until none does.
    const auto inside = [&](int width) {
        probe = sized(QSize(width, qCeil(qreal(framed.height()) * width / framed.width())));
        return liesInOutline(probe, blocks);
    };
    int lo = framed.width();
    int hi = lo * 2;
    while (!inside(hi) && hi < k_maxFitSize)
        hi *= 2;
    if (!inside(hi))
        return sized(framed);   // no size will do: the blocks' own rectangle is the best there is
    while (hi - lo > 1) {
        const int mid = (lo + hi) / 2;
        (inside(mid) ? hi : lo) = mid;
    }
    inside(hi);
    return probe;
}

QList<Handle> textAreaHandles(const Composition& c, const Words& w)
{
    if (!seated(c, w))
        return {};
    const QRectF  area = textSafeArea(c, w);
    const QPointF a    = area.center();
    const auto arrow = [](TextAreaPart part, int index, const QPointF& at, const QPointF& axis) {
        Handle h;
        h.part  = int(part);
        h.index = index;
        h.at    = at;
        h.axis  = axis;
        h.glyph = Handle::Glyph::Arrow;
        return h;
    };
    Handle anchor;
    anchor.part  = int(TextAreaPart::Anchor);
    anchor.at    = a;
    anchor.glyph = Handle::Glyph::Anchor;
    // The middle first: where a narrow frame puts an arrow over it, a press there takes the middle.
    return {anchor,
            arrow(TextAreaPart::Side, 0, QPointF(area.left(), a.y()), QPointF(1, 0)),
            arrow(TextAreaPart::Side, 1, QPointF(area.right(), a.y()), QPointF(-1, 0)),
            arrow(TextAreaPart::Top, 0, QPointF(a.x(), area.top()), QPointF(0, 1)),
            arrow(TextAreaPart::Bottom, 0, QPointF(a.x(), area.bottom()), QPointF(0, -1))};
}

Words dragTextAreaHandle(const Composition& c, const Words& w, const Handle& handle, const QPointF& to)
{
    const std::optional<QRectF> seat = seatBox(c, w);
    if (!seat)
        return w;
    Words        out   = w;
    Frame&       f     = out.frame;
    const QRectF inner = *seat;
    const QRectF area  = textSafeArea(c, w);
    // Whole pixels: what the file says is what the author placed, not the pointer's last fraction.
    const auto whole = [](qreal inset) { return qreal(qRound(inset)); };
    switch (partOf<TextAreaPart>(handle)) {
    case TextAreaPart::Side:
        if (handle.index == 0)
            f.left = whole(qMin(to.x(), area.right() - k_minTextSpan) - inner.left());
        else
            f.right = whole(inner.right() - qMax(to.x(), area.left() + k_minTextSpan));
        break;
    case TextAreaPart::Top:
        f.top = whole(qMin(to.y(), area.bottom() - k_minTextSpan) - inner.top());
        break;
    case TextAreaPart::Bottom:
        f.bottom = whole(inner.bottom() - qMax(to.y(), area.top() + k_minTextSpan));
        break;
    case TextAreaPart::Anchor: {
        // The whole frame, by whole pixels, its middle kept within the seat; the edges of an axis it moves along
        // are set by it.
        const QPointF mid(std::clamp(to.x(), inner.left(), inner.right()), std::clamp(to.y(), inner.top(), inner.bottom()));
        const int     dx = qRound(mid.x() - area.center().x());
        const int     dy = qRound(mid.y() - area.center().y());
        if (dx != 0) {
            f.left  = f.left.value_or(0.0) + dx;
            f.right = f.right.value_or(0.0) - dx;
        }
        if (dy != 0) {
            f.top    = f.top.value_or(0.0) + dy;
            f.bottom = f.bottom.value_or(0.0) - dy;
        }
        break;
    }
    }
    return out;
}

QPointF textAnchor(const Composition& c)
{
    const Words* w = wordsOf(c);
    return w ? alignedPoint(c, *w) : QPointF();
}

}  // namespace Geometry
