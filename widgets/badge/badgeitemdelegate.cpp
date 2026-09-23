#include "badgeitemdelegate.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QPainter>
#include <QStyle>
#include <QToolTip>

#include <limits>

namespace {

constexpr int k_lineGap    = 4;    //!< Between a row's own line and the dimmed one under it.
constexpr int k_twoLinePad = 12;   //!< The padding a two-line row carries, above and below.

[[nodiscard]] QList<Badge> badgesAt(const QModelIndex& index, int role)
{
    return index.data(role).value<QList<Badge>>();
}

//! How much room a run of chips wants, the gaps between them included — measured by the code that
//! draws them, so the two cannot disagree about it.
[[nodiscard]] int badgesWidth(const QFont& base, const QList<Badge>& badges)
{
    if (badges.isEmpty())
        return 0;
    const QList<QRect> chips =
        layOutBadges(nullptr, base, badges, 0, 0, 0, std::numeric_limits<int>::max());
    return chips.isEmpty() ? 0 : chips.last().right() + 1;
}

/**
 * @brief Lays one line out — the text, then its chips — and draws it when @p painter is given.
 *
 * **The chips win the space.** A label is a preview of something legible elsewhere; a chip is a fact
 * about the object written nowhere else, and `layOutBadges` drops a chip that would cross the edge —
 * silently, which on a long label would mean the row quietly stops reporting. So the text is elided to
 * what is left over, and the chips are always on the row.
 *
 * @return One rect per chip drawn, for the caller that is hit-testing rather than painting.
 */
QList<QRect> layOutLine(QPainter* painter, const QFont& base, const QString& text, const QColor& colour,
                        const QList<Badge>& badges, const QRect& line, int lineH)
{
    const QFontMetrics fm(base);
    const QString      shown =
        fm.elidedText(text, Qt::ElideRight, qMax(0, line.width() - badgesWidth(base, badges)));
    if (painter) {
        painter->setFont(base);
        painter->setPen(colour);
        painter->drawText(line, Qt::AlignVCenter | Qt::AlignLeft, shown);
    }
    return layOutBadges(painter, base, badges, line.left() + fm.horizontalAdvance(shown), line.top(),
                        lineH, line.right());
}

//! The detail behind whichever chip @p pos is in, or empty when it is in none of them.
[[nodiscard]] QString tipAt(const QPoint& pos, const QFont& base, const QString& text,
                            const QList<Badge>& badges, const QRect& line, int lineH)
{
    const QList<QRect> chips = layOutLine(nullptr, base, text, QColor(), badges, line, lineH);
    for (int i = 0; i < chips.size(); ++i)
        if (chips.at(i).contains(pos))
            return badges.at(i).detail;
    return {};
}

} // namespace

BadgeItemDelegate::Lines BadgeItemDelegate::linesFor(const QStyleOptionViewItem& opt,
                                                     const QModelIndex&          index) const
{
    const QWidget* widget = opt.widget;
    QStyle*        style  = widget ? widget->style() : QApplication::style();
    // Whatever the style left after the check indicator and the icon — it is measured from the item's
    // rect rather than from its text, so asking with the text already cleared is safe.
    const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget);
    const int   lineH    = QFontMetrics(opt.font).height();

    Lines l;
    l.two    = !index.data(k_subtitleRole).toString().isEmpty();
    l.height = lineH;
    const int totalH = l.two ? lineH * 2 + k_lineGap : lineH;
    const int top    = textRect.top() + qMax(0, (textRect.height() - totalH) / 2);
    l.line           = QRect(textRect.left(), top, textRect.width(), lineH);
    if (l.two)
        l.subtitle = l.line.translated(0, lineH + k_lineGap);
    return l;
}

void BadgeItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    const QString      subtitle     = index.data(k_subtitleRole).toString();
    const QList<Badge> badges       = badgesAt(index, k_badgesRole);
    const QList<Badge> subBadges    = badgesAt(index, k_subtitleBadgesRole);
    // Nothing of ours on this row — and a row we have nothing to add to is a row the base class should
    // draw, exactly as it would have without this delegate.
    if (badges.isEmpty() && subtitle.isEmpty() && subBadges.isEmpty()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    const QString  title  = opt.text;
    const QWidget* widget = opt.widget;
    QStyle*        style  = widget ? widget->style() : QApplication::style();

    opt.text.clear();                                                      // we paint the text ourselves
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);    // bg + selection + check + icon

    const Lines  lines = linesFor(opt, index);
    // The item's own foreground, which `initStyleOption` has already put in this palette — that is what
    // keeps a greyed row grey once the text stops being the style's to draw.
    const QColor ink   = opt.palette.color((opt.state & QStyle::State_Selected) ? QPalette::HighlightedText
                                                                               : QPalette::Text);
    const QColor dim   = opt.palette.color(QPalette::PlaceholderText);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    layOutLine(painter, opt.font, title, ink, badges, lines.line, lines.height);
    if (lines.two)
        layOutLine(painter, opt.font, subtitle, dim, subBadges, lines.subtitle, lines.height);
    painter->restore();
}

QSize BadgeItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const QSize base = QStyledItemDelegate::sizeHint(option, index);
    if (index.data(k_subtitleRole).toString().isEmpty())
        return base;   // one line: the row is whatever the style asks for, the icon included

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const int lineH = QFontMetrics(opt.font).height();
    // Width nought, not the base's: the second line is a summary that elides, and a hint wide enough to
    // hold it would hand the view a horizontal scrollbar for text the row was never going to show whole.
    return {0, qMax(base.height(), lineH * 2 + k_lineGap + k_twoLinePad)};
}

bool BadgeItemDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view,
                                  const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (event && event->type() == QEvent::ToolTip) {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const Lines lines = linesFor(opt, index);

        QString tip = tipAt(event->pos(), opt.font, opt.text, badgesAt(index, k_badgesRole),
                            lines.line, lines.height);
        if (tip.isEmpty() && lines.two)
            tip = tipAt(event->pos(), opt.font, index.data(k_subtitleRole).toString(),
                        badgesAt(index, k_subtitleBadgesRole), lines.subtitle, lines.height);
        if (!tip.isEmpty()) {
            QToolTip::showText(event->globalPos(), tip, view);
            return true;
        }
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}
