#ifndef BADGEITEMDELEGATE_HPP
#define BADGEITEMDELEGATE_HPP

#include <QStyledItemDelegate>

#include "badge.hpp"

/**
 * @brief Paints a view's row as its own text followed by chips — what the row *is*, then what is *true*
 *        of it.
 *
 * Extracted from the profile picker, where it was a file-local class, because the strip editor's object
 * list wants the same row: a name somebody chose, and a few facts nobody typed. The one change the move
 * demanded is where the chips come from — **the model, not the host's own row list**. Indexing a flat
 * vector by `index.row()` is an assumption only a list can keep; a tree's rows are not a vector, and a
 * delegate that needs one cannot serve both.
 *
 * Only the text and the chips are custom-painted: the style still draws the background, the selection,
 * the check indicator and the icon. A row with no chips and no subtitle is handed straight back to
 * `QStyledItemDelegate`, so setting this delegate on an existing view changes nothing until the model
 * has something to report.
 */
class BadgeItemDelegate : public QStyledItemDelegate
{
public:
    /**
     * @name Model roles
     *
     * Far above `Qt::UserRole`, deliberately. Every view that uses this delegate keeps its own data in
     * the first few user roles — the strip editor's tree alone uses `UserRole + 1` and `+ 2` — so a
     * delegate that claimed one of those would collide with the very host it was written to serve.
     */
    ///@{
    static constexpr int k_badgesRole         = Qt::UserRole + 900;  //!< `QList<Badge>`, after the text.
    static constexpr int k_subtitleRole       = Qt::UserRole + 901;  //!< A dimmed second line; empty → one line.
    static constexpr int k_subtitleBadgesRole = Qt::UserRole + 902;  //!< `QList<Badge>`, after that line.
    ///@}

    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void  paint(QPainter* painter, const QStyleOptionViewItem& option,
                const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    /**
     * @brief Answers the tooltip of whichever chip is under the cursor.
     *
     * A painted badge is not a widget, so there is nothing for `setToolTip()` to attach to and the view
     * has to be asked instead — this is the hook for that. The chip rectangles come from the same call
     * `paint()` makes, with a null painter, so the chip that answers cannot drift apart from the chip on
     * screen. Off a chip, the row's own tooltip is left to the base class.
     */
    bool  helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option,
                    const QModelIndex& index) override;

private:
    //! Where the one or two lines of a row sit. Computed once and used by both painting and hit-testing,
    //! because a tooltip that disagrees with the pixels is worse than no tooltip.
    struct Lines {
        QRect line;
        QRect subtitle;
        int   height = 0;
        bool  two    = false;
    };

    [[nodiscard]] Lines linesFor(const QStyleOptionViewItem& opt, const QModelIndex& index) const;
};

#endif // BADGEITEMDELEGATE_HPP
