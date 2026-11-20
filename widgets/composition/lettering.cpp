// The lettering: the words set in their font and turned into outlines, the frame they are laid in and where
// the block sits in it, and fitting a box to them. The frame is the words' own (Words::frame): automatic edges
// reach their seat's inner box: a body's box less the stroke and the edge's teeth, the same for every shape, or a
// picture's whole box.

#include "geometry.hpp"
#include "bezier.hpp"
#include "shapes.hpp"
#include "styles.hpp"   // shadowOf(): the lettering's shadow is the balloon's kind

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextBoundaryFinder>
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
 *  What TextProperties::letterSpacing is counted in: thousandths of an em.
 */
constexpr qreal k_perEm         = 1000.0;
/**
 *  What TextProperties::lineSpacing is counted in: per cent of the font's own.
 */
constexpr qreal k_perCent       = 100.0;
/**
 *  Points a curved segment of a warp is walked by (Bezier::polyline()): its length, and the way it turns, to well
 *  under a pixel on any block.
 */
constexpr int   k_warpSamples   = 64;
/**
 *  The longest a straight stretch of a letter's outline is bent as, in strip pixels: a longer one is cut into pieces
 *  this long first, or it would stay straight across the bend.
 */
constexpr qreal k_warpStep      = 1.0;
//! A warp's curve (TextProperties::warp): an open path of two nodes at least.
constexpr Bezier::Rules k_warpRules{2, false};

//! @p font given @p spacing (TextProperties::letterSpacing) at its own size: pixels between letters.
void setSpacing(QFont& font, int spacing)
{
    font.setLetterSpacing(QFont::AbsoluteSpacing, spacing * font.pixelSize() / k_perEm);
}

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

//! The share of a span before the point it is aligned to: none from a start, all of it to an end, half to a middle.
qreal shareBefore(bool start, bool end)
{
    return start ? 0.0 : end ? 1.0 : 0.5;
}

//! The block laid in @p w's frame, as blockRect() says: aligned to it on both axes.
QRectF placedBlock(const Composition& c, const Words& w, const QSizeF& block)
{
    const QPointF p = alignedPoint(c, w);
    const int     h = w.text.align & Qt::AlignHorizontal_Mask;
    const int     v = w.text.valign & Qt::AlignVertical_Mask;
    return QRectF(QPointF(p.x() - shareBefore(h & Qt::AlignLeft, h & Qt::AlignRight) * block.width(),
                          p.y() - shareBefore(v & Qt::AlignTop, v & Qt::AlignBottom) * block.height()),
                  block);
}

//! A family set as a QFont is: only when installed (layOutText()).
void setFamily(QFont& f, const QString& family)
{
    if (!family.isEmpty() && QFontDatabase::hasFamily(family))
        f.setFamily(family);
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
    setFamily(f, t.family);
    f.setPixelSize(qMax(1, t.pixelSize));
    f.setBold(t.bold);
    f.setItalic(t.italic);
    setSpacing(f, t.letterSpacing);
    f.setKerning(t.kerning);

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
            font.setKerning(f.kerning());   // the words' alone: a run does not set it
        }
        if (r.pixelSize)
            font.setPixelSize(qMax(1, *r.pixelSize));
        if (r.bold)
            font.setBold(*r.bold);
        if (r.italic)
            font.setItalic(*r.italic);
        setSpacing(font, r.letterSpacing.value_or(t.letterSpacing));   // of the run's own size
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
 * \brief The letter spacing after @p line's last letter: Qt adds it after every letter, the last too, so a line is
 *        that much wider than its letters. The spacing of the font that letter is set in (a run's or the words'
 *        @p words); 0 for a line with no letter.
 */
qreal trailingSpacing(const QTextBlock& b, const QTextLine& line, const QFont& words)
{
    const QString text = b.text();
    int           last = line.textStart() + line.textLength() - 1;
    while (last >= line.textStart() && text.at(last).isSpace())
        --last;   // trailing spaces are not in the line's natural width
    if (last < line.textStart())
        return 0.0;
    for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        const int           from     = fragment.position() - b.position();
        if (last >= from && last < from + fragment.length()) {
            const QTextCharFormat format = fragment.charFormat();
            return (format.hasProperty(k_runProperty) ? format.font() : words).letterSpacing();
        }
    }
    return words.letterSpacing();
}

/**
 * \brief @p doc's block: as wide as its widest line less the spacing after that line's last letter, and as tall as
 *        down to its last line's foot.
 *
 * Qt puts the room a line spacing adds under each line (ProportionalHeight: the letters at the top of a taller line),
 * the last one too; but line spacing is room *between* lines, and under the last it would only lift the letters off
 * the middle of their frame and leave Fit to text a band to spare. Below 100 % the last line reaches past the
 * document's height: the block reaches with it, so it is not cut.
 */
QSizeF blockOf(const QTextDocument& doc)
{
    const QSizeF size    = doc.size();   // first: the document lays its lines out only when asked
    qreal        widest  = -1.0;
    qreal        spacing = 0.0;
    qreal        foot    = size.height();   // no line: the document's own
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
        if (const QTextLayout* lay = b.layout())
            for (int i = 0; i < lay->lineCount(); ++i) {
                const QTextLine line = lay->lineAt(i);
                if (line.naturalTextWidth() > widest) {
                    widest  = line.naturalTextWidth();
                    spacing = trailingSpacing(b, line, doc.defaultFont());
                }
                foot = lay->position().y() + line.y() + line.height();   // the last line read is the last
            }
    return {doc.idealWidth() - spacing, foot};
}

/**
 * \brief How far @p line's letters move across from where Qt put them, so the line is aligned by its letters in a
 *        block @p width wide: Qt aligned it in the layout's own width, the spacing after its last letter included.
 *
 * Qt's own place is kept, and only the difference made good: Qt does not centre a line by its natural width alone (a
 * run's line can sit off it), and with no letter spacing the letters are where they always were.
 */
qreal alignedShift(const QTextBlock& b, const QTextLine& line, const QTextDocument& doc, int align, qreal width)
{
    const int   h      = align & Qt::AlignHorizontal_Mask;
    const qreal before = (h & Qt::AlignRight) ? 1.0 : (h & Qt::AlignHCenter) ? 0.5 : 0.0;
    return before * (width - doc.textWidth() + trailingSpacing(b, line, doc.defaultFont()));
}

//! Letters set in one font from one baseline point: a piece of laid-out lettering.
struct Piece
{
    QFont   font;
    QString text;
    QPointF at;         //!< The baseline's start, in the block's coordinates (its top left at 0, 0).
    int     run = -1;   //!< The run (an index in TextProperties::runs) the letters are in, or -1 for the words'.
};

//! Lettering laid out: what letters() draws and textBlockSize() measures, so the two never disagree.
struct Layout
{
    QList<Piece> pieces;
    QSizeF       block;   //!< What the frame aligns (blockOf()).
};

/**
 * \brief How tall @p metrics' font sets a capital: its H's outline, measured, as a face's own cap height may be
 *        missing or wrong (one comic face says 0.7 em, and sets its H 1.12 em tall).
 */
qreal capitalHeight(const QFontMetricsF& metrics)
{
    return metrics.tightBoundingRect(QStringLiteral("H")).height();
}

/**
 * \brief @p t set down in columns (TextProperties::vertical): each line typed a column, the first on the left (or on
 *        the right, TextProperties::columnsLeft), each letter upright in its own font (a run's or the words').
 *
 * Qt lays text across only, so the columns are laid here; the document still says which font each letter is in. A
 * letter's cell is an em of its font tall, the letter spacing between two cells (after each but the last, as across);
 * in it the letter sits centred across the column by its advance, and up and down so its font's capitals are centred
 * in the cell: every letter of a font on one baseline, a descender below it.
 * A space is a cell of nothing, an empty line a column of none. A column is as thick as a line of its letters would be
 * tall (the words' font for an empty one), and the columns lineSpacing per cent of that apart, middle to middle: at
 * 100 % they touch, as lines do. Each column is placed up and down in the block by valign.
 */
Layout laidOutDown(const TextProperties& t)
{
    QTextDocument doc;
    layOutText(doc, t, 1);
    const QFont words = doc.defaultFont();

    struct Cell
    {
        QFont   font;
        QString text;
        int     run;
    };
    struct Column
    {
        QList<Cell> cells;
        qreal       length    = 0.0;   //!< Top of the first cell to the foot of the last.
        qreal       thickness = 0.0;
    };
    QList<Column> columns;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        const QString text = b.text();
        QList<QFont>  fontAt(text.size(), words);
        QList<int>    runAt(text.size(), -1);
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment   fragment = it.fragment();
            const QTextCharFormat format   = fragment.charFormat();
            if (!format.hasProperty(k_runProperty))
                continue;
            const int from = fragment.position() - b.position();
            for (int i = from; i < from + fragment.length() && i < text.size(); ++i) {
                fontAt[i] = format.font();
                runAt[i]  = format.property(k_runProperty).toInt();
            }
        }
        Column             column;
        QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, text);
        for (int from = 0, to = graphemes.toNextBoundary(); to > from; from = to, to = graphemes.toNextBoundary()) {
            const QFont& font = fontAt.at(from);
            if (!column.cells.isEmpty())
                column.length += column.cells.constLast().font.letterSpacing();
            column.cells.append({font, text.mid(from, to - from), runAt.at(from)});
            column.length   += font.pixelSize();
            column.thickness = qMax(column.thickness, QFontMetricsF(font).height());
        }
        if (column.cells.isEmpty())
            column.thickness = QFontMetricsF(words).height();
        columns.append(column);
    }

    Layout out;
    // The middle of each column from the block's first side, and the block's width and height.
    QList<qreal> middles;
    qreal        across = 0.0;
    for (qsizetype i = 0; i < columns.size(); ++i) {
        across += i == 0 ? columns[i].thickness / 2.0
                         : (columns[i - 1].thickness + columns[i].thickness) / 2.0 * t.lineSpacing / k_perCent;
        middles.append(across);
        out.block.setHeight(qMax(out.block.height(), columns[i].length));
    }
    out.block.setWidth(columns.isEmpty() ? 0.0 : across + columns.constLast().thickness / 2.0);

    const int   v      = t.valign & Qt::AlignVertical_Mask;
    const qreal before = shareBefore(v & Qt::AlignTop, v & Qt::AlignBottom);
    for (qsizetype i = 0; i < columns.size(); ++i) {
        const Column& column = columns[i];
        const qreal   middle = t.columnsLeft ? out.block.width() - middles[i] : middles[i];
        qreal         top    = before * (out.block.height() - column.length);
        for (const Cell& cell : column.cells) {
            const qreal em = cell.font.pixelSize();
            if (!cell.text.trimmed().isEmpty()) {
                QFont plain = cell.font;   // the advance without the spacing after it
                plain.setLetterSpacing(QFont::AbsoluteSpacing, 0.0);
                const QFontMetricsF metrics(plain);
                out.pieces.append({cell.font, cell.text,
                                   QPointF(middle - metrics.horizontalAdvance(cell.text) / 2.0,
                                           top + (em + capitalHeight(metrics)) / 2.0),
                                   cell.run});
            }
            top += em + cell.font.letterSpacing();
        }
    }
    return out;
}

/**
 * \brief @p t laid out: each line, or with runs each fragment of a line, in its font, where the layout put it and
 *        aligned in the block by its letters (alignedShift()). A line or fragment of spaces alone is no piece.
 *
 * The document does the line breaking (the author's own, never wrapped) and the placing, and each line is read back
 * at the position the layout gave it: what the document would have painted. Laid out a second time as wide as its
 * widest line, so each line is aligned within the block.
 */
Layout laidOut(const TextProperties& t)
{
    if (t.vertical)
        return laidOutDown(t);
    QTextDocument doc;
    layOutText(doc, t, 1);
    layOutText(doc, t, doc.idealWidth());
    Layout out;
    out.block = blockOf(doc);

    const QFont f = doc.defaultFont();
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        const QTextLayout* lay = b.layout();
        if (!lay)
            continue;
        const QString blockText = b.text();
        for (int i = 0; i < lay->lineCount(); ++i) {
            const QTextLine line  = lay->lineAt(i);
            const qreal     shift = alignedShift(b, line, doc, t.align, out.block.width());
            if (t.runs.isEmpty()) {
                const QString s = blockText.mid(line.textStart(), line.textLength());
                if (s.trimmed().isEmpty())
                    continue;
                // naturalTextRect(), *not* position(): Qt stores every line at the layout's left edge and
                // applies the alignment offset inside QTextLine::draw(). Nothing here goes through draw(),
                // so reading position() silently left-aligns every bubble whatever the setting says.
                // naturalTextRect() is the accessor that has the offset already folded in.
                // addText places by the text *baseline*, which is the line's top plus its ascent.
                const QPointF p = lay->position() + line.naturalTextRect().topLeft();
                out.pieces.append({f, s, QPointF(p.x() + shift, p.y() + line.ascent())});
                continue;
            }
            // Runs set some letters in other fonts: each fragment of a line in its own.
            const int start = line.textStart();
            const int end   = start + line.textLength();
            for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                const int           from     = qMax(start, fragment.position() - b.position());
                const int           to       = qMin(end, fragment.position() - b.position() + fragment.length());
                const QString       s        = blockText.mid(from, to - from);
                if (to <= from || s.trimmed().isEmpty())
                    continue;
                const QTextCharFormat format = fragment.charFormat();
                const bool            inRun  = format.hasProperty(k_runProperty);
                // cursorToX() has Qt's alignment folded in, as naturalTextRect() has: the shift takes it out.
                out.pieces.append({inRun ? format.font() : f, s,
                                   QPointF(lay->position().x() + line.cursorToX(from) + shift,
                                           lay->position().y() + line.y() + line.ascent()),
                                   inRun ? format.property(k_runProperty).toInt() : -1});
            }
        }
    }
    return out;
}

//! Whether @p t is bent along a curve: a warp of two nodes at least.
bool isWarped(const TextProperties& t)
{
    return t.warp.size() >= k_warpRules.minNodes;
}

qreal dot(const QPointF& a, const QPointF& b)
{
    return a.x() * b.x() + a.y() * b.y();
}

//! @p v turned a quarter: the normal on its left as the screen shows it (y down), as long as it.
QPointF normalOf(const QPointF& v)
{
    return {-v.y(), v.x()};
}

QPointF unit(const QPointF& v)
{
    const qreal l = std::hypot(v.x(), v.y());
    return l > 0.0 ? v / l : QPointF();
}

/**
 * \brief A flat block bent along a warp's curve: the block's middle line, across for lines and down for columns,
 *        laid along the curve by length (scaled to the curve's), each point kept as far from it along the curve's
 *        normal as it was from the line. Past the curve's ends, it runs on straight.
 */
class Warp
{
public:
    Warp(const QList<ShapeNode>& curve, const QSizeF& flat, bool vertical)
        : m_start(vertical ? QPointF(flat.width() / 2.0, 0.0) : QPointF(0.0, flat.height() / 2.0))
        , m_along(vertical ? QPointF(0.0, 1.0) : QPointF(1.0, 0.0))
        , m_reference(vertical ? flat.height() : flat.width())
    {
        // The curve's points, none twice in a row: each step has a length and a way.
        for (const QPointF& p : Bezier::polyline(curve, k_warpRules, QRectF(QPointF(), flat), k_warpSamples))
            if (m_points.isEmpty() || p != m_points.last())
                m_points.append(p);
        m_lengths.append(0.0);
        for (qsizetype i = 1; i < m_points.size(); ++i) {
            const QPointF step = m_points[i] - m_points[i - 1];
            m_lengths.append(m_lengths.last() + std::hypot(step.x(), step.y()));
        }
        // A point's way is its two steps' together, so the normal turns smoothly through it; an end's is its step's.
        for (qsizetype i = 0; i < m_points.size(); ++i) {
            const QPointF before = i > 0 ? unit(m_points[i] - m_points[i - 1]) : QPointF();
            const QPointF after  = i + 1 < m_points.size() ? unit(m_points[i + 1] - m_points[i]) : QPointF();
            const QPointF way    = unit(before + after);
            m_ways.append(way.isNull() ? (after.isNull() ? before : after) : way);   // a hairpin: one step's
        }
    }

    //! Whether there is anything to bend along: a curve with length, a block with a middle line.
    [[nodiscard]] bool valid() const { return m_reference > 0.0 && m_points.size() >= 2; }

    //! @p p of the flat block, bent.
    [[nodiscard]] QPointF map(const QPointF& p) const
    {
        const QPointF from   = p - m_start;
        const qreal   at     = dot(from, m_along) / m_reference * m_lengths.last();
        const qreal   beside = dot(from, normalOf(m_along));
        // The step @p at falls in; before the first or past the last, the end step, run on straight.
        const auto    it     = std::upper_bound(m_lengths.cbegin() + 1, m_lengths.cend() - 1, at);
        const auto    i      = qsizetype(it - m_lengths.cbegin()) - 1;
        const qreal   share  = (at - m_lengths[i]) / (m_lengths[i + 1] - m_lengths[i]);
        const QPointF point  = m_points[i] + (m_points[i + 1] - m_points[i]) * share;
        const qreal   within = qBound(0.0, share, 1.0);
        const QPointF way    = unit(m_ways[i] * (1.0 - within) + m_ways[i + 1] * within);
        return point + normalOf(way) * beside;
    }

private:
    QPointF        m_start;       //!< Where the middle line starts in the flat block.
    QPointF        m_along;       //!< Its way.
    qreal          m_reference;   //!< Its length.
    QList<QPointF> m_points;      //!< The curve, as points.
    QList<qreal>   m_lengths;     //!< How far along the curve each point is.
    QList<QPointF> m_ways;        //!< Which way the curve runs at each point.
};

/**
 * \brief @p path bent by @p warp: its curves flattened and its edges cut to k_warpStep, each point mapped.
 *
 * ponytail: polygons, not Béziers, so a bent letter is many short edges in the SVG; map the curves' control points
 * if the files grow too large.
 */
QPainterPath bentPath(const QPainterPath& path, const Warp& warp)
{
    QPainterPath out;
    out.setFillRule(Qt::WindingFill);
    for (const QPolygonF& ring : path.toSubpathPolygons()) {
        QPolygonF bent;
        for (qsizetype i = 0; i < ring.size(); ++i) {
            if (i > 0) {
                const QPointF a    = ring[i - 1], b = ring[i];
                const int     cuts = qCeil(std::hypot(b.x() - a.x(), b.y() - a.y()) / k_warpStep);
                for (int k = 1; k < cuts; ++k)
                    bent.append(warp.map(a + (b - a) * (qreal(k) / cuts)));
            }
            bent.append(warp.map(ring[i]));
        }
        out.addPolygon(bent);
        out.closeSubpath();
    }
    return out;
}

//! A piece's letters as outlines, and the run they are in (Piece::run).
struct Outline
{
    QPainterPath path;
    int          run = -1;
};

/**
 * \brief The box a block laid out @p flat takes bent along @p t's warp: the flat block's own rectangle, bent and
 *        measured, so a straight warp keeps it as it was and the frame, the fit and the overflow follow the bend.
 */
QRectF bentBox(const TextProperties& t, const QSizeF& flat)
{
    const Warp warp(t.warp, flat, t.vertical);
    if (!warp.valid())
        return QRectF(QPointF(), flat);
    QPainterPath box;
    box.addRect(QRectF(QPointF(), flat));
    return bentPath(box, warp).boundingRect();
}

//! Lettering bent along its warp: the outlines in the bent block's coordinates (its top left at 0, 0), and its size.
struct Bent
{
    QList<Outline> outlines;
    QSizeF         block;
};

//! @p layout of @p t bent along its warp (Warp), moved so its bent box (bentBox()) starts at 0, 0.
Bent bent(const TextProperties& t, const Layout& layout)
{
    const Warp   warp(t.warp, layout.block, t.vertical);
    const QRectF box = bentBox(t, layout.block);
    Bent         out;
    out.block = box.size();
    for (const Piece& p : layout.pieces) {
        QPainterPath piece;
        piece.addText(p.at, p.font, p.text);
        Outline o{warp.valid() ? bentPath(piece, warp) : piece, p.run};
        o.path.translate(-box.topLeft());
        out.outlines.append(o);
    }
    return out;
}

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

    // The block placed by the alignment in its frame (placedBlock()), and each piece turned into outlines there:
    // going to *outlines* is what lets the same geometry be written into an SVG the library can rasterise without a
    // font. Bent, the outlines are made in the block first and the bent block is placed.
    const Layout   layout = laidOut(w.text);
    QList<Outline> outlines;
    if (isWarped(w.text)) {
        const Bent    b      = bent(w.text, layout);
        const QPointF origin = placedBlock(c, w, b.block).topLeft();
        for (Outline o : b.outlines) {
            o.path.translate(origin);
            outlines.append(o);
        }
    } else {
        const QPointF origin = placedBlock(c, w, layout.block).topLeft();
        for (const Piece& p : layout.pieces) {
            QPainterPath piece;
            piece.addText(origin + p.at, p.font, p.text);
            outlines.append({piece, p.run});
        }
    }
    // Non-zero, as addText() leaves a path it starts: where two letters overlap (a tight face, lines set close)
    // they paint once. addPath() keeps the receiver's rule, so the paths gathered from pieces are set to it too.
    QPainterPath out;
    out.setFillRule(Qt::WindingFill);
    for (const Outline& o : std::as_const(outlines)) {
        out.addPath(o.path);
        if (fills) {
            QPainterPath& filled = (*fills)[o.run >= 0 && w.text.runs.at(o.run).fill ? o.run : -1];
            filled.setFillRule(Qt::WindingFill);
            filled.addPath(o.path);
        }
    }
    if (out.isEmpty())
        return out;

    // The frame is the clip, so an overlong line cannot bleed past its sides. Not up and down: lines the frame
    // has no room for go on past it, away from the side they are aligned to, never cut, and the red text area
    // says so. Columns turn it: an overlong column is cut at the top and bottom, and columns go on past the
    // sides. A text object keeps its box as it was.
    // Intersecting keeps that in vector form, so it survives into the SVG.
    QRectF open = safe;
    if (seated(c, w)) {
        const QRectF drawn = out.boundingRect();
        if (w.text.vertical) {
            open.setLeft(qMin(safe.left(), drawn.left()));
            open.setRight(qMax(safe.right(), drawn.right()));
        } else {
            open.setTop(qMin(safe.top(), drawn.top()));
            open.setBottom(qMax(safe.bottom(), drawn.bottom()));
        }
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
    // Never wrapped, so the block is the same whatever box it sits in: the widest line's letters by the lines, or
    // that block bent.
    const QSizeF flat = laidOut(t).block;
    return isWarped(t) ? bentBox(t, flat).size() : flat;
}

QList<ShapeNode> straightWarp(bool vertical)
{
    if (vertical)
        return {ShapeNode(0.5, 0.0), ShapeNode(0.5, 1.0)};
    return {ShapeNode(0.0, 0.5), ShapeNode(1.0, 0.5)};
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

namespace {

/**
 * \brief @p c's body @p id fitted to the blocks of its own words (fitToBlocks()); @p inPlace moves it too, so its
 *        seat's middle is on the blocks' (a bubble of a chain), where a lone balloon's body keeps the origin.
 */
Composition fitBody(const Composition& c, int id, const Blocks& blocks, bool inPlace)
{
    const Body* body = bodyOf(c, id);
    if (!body)
        return c;

    // The blocks as they lie now, and the rectangle they lie in together; the other bodies' words left out, so
    // their outlines are not this body's to hold.
    QList<std::optional<QRectF>> placed;
    QRectF                       all;
    Blocks                       own;
    for (qsizetype i = 0; i < c.words.size(); ++i) {
        const Words& w     = c.words[i];
        const QSizeF block = i < blocks.size() ? blocks[i] : QSizeF();
        if (seatOf(c, w) != body || block.isEmpty()) {
            placed.append(std::nullopt);
            own.append(QSizeF());
            continue;
        }
        own.append(block);
        placed.append(placedBlock(c, w, block));
        all = all.isNull() ? *placed.last() : all.united(*placed.last());
    }
    if (all.isNull())
        return c;

    // A body of @p size whose seat (inner box) has its middle on the blocks', every frame measured from that seat.
    const qreal inset = c.balloon->style.line.strokeWidth / 2.0 + edgeInset(body->shape);
    const auto  sized = [&](const QSize& size) {
        Composition out    = c;
        Body*       fitted = bodyOf(out, id);
        fitted->size       = size;
        if (inPlace)
            fitted->place.pos = all.center() - QPointF(size.width(), size.height()) / 2.0;
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
    if (liesInOutline(probe, own))
        return probe;

    // A curved outline (an ellipse's corners, a box's rounded ones) cuts a block: the body grows, its proportions
    // kept, until none does.
    const auto inside = [&](int width) {
        probe = sized(QSize(width, qCeil(qreal(framed.height()) * width / framed.width())));
        return liesInOutline(probe, own);
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

} // namespace

Composition fitToBlocks(const Composition& c, const Blocks& blocks)
{
    if (!bodyOf(c)) {
        Composition out = c;
        resize(out, fitBoxFor(c, blocks));
        return out;
    }
    if (c.balloon->bodies.size() == 1)
        return fitBody(c, c.balloon->bodies.first().id, blocks, false);
    Composition out = c;   // a chain: each bubble to its own words, where they lie
    for (const Body& b : c.balloon->bodies)
        out = fitBody(out, b.id, blocks, true);
    return out;
}

Composition fitBodyToBlocks(const Composition& c, int body, const Blocks& blocks)
{
    return fitBody(c, body, blocks, c.balloon && c.balloon->bodies.size() > 1);
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
