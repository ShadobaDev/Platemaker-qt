#include "profilepickerdialog.h"
#include "ui_profilepickerdialog.h"

#include "badge.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QListWidgetItem>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolTip>
#include <QWidget>

namespace {

// Paints each list row as two lines (name / summary) with rounded chips after the text. Reads the row
// data straight from the dialog's row list (index == row), so the badges stay structured instead of
// baked into a string. Only text/badges are custom-painted; the base style still draws the background,
// selection and check indicator, so checkboxes and selection are unchanged. The chips themselves are
// `widgets/badge/`'s, shared with the status bar.
class BadgeItemDelegate : public QStyledItemDelegate
{
public:
    explicit BadgeItemDelegate(const QList<ProfilePickerDialog::Row>* rows, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_rows(rows) {}

protected:
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        QStyle* style = QApplication::style();
        opt.text.clear();                                                     // we paint the text ourselves
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, nullptr);  // bg + selection + checkbox

        const ProfilePickerDialog::Row* r = rowFor(index);
        if (!r)
            return;
        const Lines lines = linesFor(opt);

        const QColor nameColour = opt.palette.color(
            (opt.state & QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text);
        const QColor dimColour  = opt.palette.color(QPalette::PlaceholderText);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        drawLine(painter, opt.font, r->title,   nameColour, r->titleBadges,   lines.title,   lines.height);
        drawLine(painter, opt.font, r->summary, dimColour,  r->summaryBadges, lines.summary, lines.height);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const int lineH = QFontMetrics(opt.font).height();
        return QSize(0, lineH * 2 + k_lineGap + 12 /*padding*/);
    }

    /**
     * @brief Answers the tooltip of whichever chip is under the cursor.
     *
     * A painted badge is not a widget, so there is nothing for `setToolTip()` to attach to and the view
     * has to be asked instead — this is the hook for that. The chip rectangles come from the same
     * `layOutBadges()` call `paint()` makes, with a null painter, so the chip that answers cannot drift
     * apart from the chip on screen.
     */
    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option,
                   const QModelIndex& index) override
    {
        const ProfilePickerDialog::Row* r = rowFor(index);
        if (event && event->type() == QEvent::ToolTip && r) {
            QStyleOptionViewItem opt = option;
            initStyleOption(&opt, index);
            const Lines lines = linesFor(opt);

            QString tip = tipAt(event->pos(), opt.font, r->title, r->titleBadges,
                                lines.title, lines.height);
            if (tip.isEmpty())
                tip = tipAt(event->pos(), opt.font, r->summary, r->summaryBadges,
                            lines.summary, lines.height);
            if (!tip.isEmpty()) {
                QToolTip::showText(event->globalPos(), tip, view);
                return true;
            }
        }
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }

private:
    static constexpr int k_lineGap = 4;   //!< Between the title line and the summary line.

    //! Where the two lines of one row sit. Computed once and used by both painting and hit-testing,
    //! because a tooltip that disagrees with the pixels is worse than no tooltip.
    struct Lines {
        QRect title;
        QRect summary;
        int   height = 0;
    };

    [[nodiscard]] const ProfilePickerDialog::Row* rowFor(const QModelIndex& index) const
    {
        if (!m_rows || index.row() < 0 || index.row() >= m_rows->size())
            return nullptr;
        return &m_rows->at(index.row());
    }

    [[nodiscard]] static Lines linesFor(const QStyleOptionViewItem& opt)
    {
        const QRect textRect =
            QApplication::style()->subElementRect(QStyle::SE_ItemViewItemText, &opt, nullptr);
        const int lineH  = QFontMetrics(opt.font).height();
        const int totalH = lineH * 2 + k_lineGap;
        const int top    = textRect.top() + qMax(0, (textRect.height() - totalH) / 2);

        Lines l;
        l.height  = lineH;
        l.title   = QRect(textRect.left(), top, textRect.width(), lineH);
        l.summary = l.title.translated(0, lineH + k_lineGap);
        return l;
    }

    //! Where this line's chips begin: after the text it follows.
    [[nodiscard]] static int badgeLeft(const QFont& base, const QString& text, const QRect& line)
    {
        return line.left() + QFontMetrics(base).horizontalAdvance(text);
    }

    static void drawLine(QPainter* painter, const QFont& base, const QString& text,
                         const QColor& textColour, const QList<Badge>& badges,
                         const QRect& line, int lineH)
    {
        painter->setFont(base);
        painter->setPen(textColour);
        painter->drawText(line, Qt::AlignVCenter | Qt::AlignLeft, text);
        layOutBadges(painter, base, badges, badgeLeft(base, text, line), line.top(), lineH,
                     line.right());
    }

    [[nodiscard]] static QString tipAt(const QPoint& pos, const QFont& base, const QString& text,
                                       const QList<Badge>& badges, const QRect& line, int lineH)
    {
        const QList<QRect> chips = layOutBadges(nullptr, base, badges,
                                                badgeLeft(base, text, line), line.top(), lineH,
                                                line.right());
        for (int i = 0; i < chips.size(); ++i)
            if (chips.at(i).contains(pos))
                return badges.at(i).detail;
        return {};
    }

    const QList<ProfilePickerDialog::Row>* m_rows;
};

} // namespace

ProfilePickerDialog::ProfilePickerDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ProfilePickerDialog)
{
    ui->setupUi(this);
    ui->listWidget->setItemDelegate(new BadgeItemDelegate(&m_rows, ui->listWidget));

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &ProfilePickerDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &ProfilePickerDialog::reject);
    connect(ui->buttonSelectAll,  &QPushButton::clicked, this, &ProfilePickerDialog::onSelectAll);
    connect(ui->buttonSelectNone, &QPushButton::clicked, this, &ProfilePickerDialog::onSelectNone);
    connect(ui->listWidget, &QListWidget::currentRowChanged, this, &ProfilePickerDialog::onCurrentRowChanged);
    connect(ui->listWidget, &QListWidget::itemChanged, this, &ProfilePickerDialog::updateConfirmEnabled);

    ui->labelIntro->hide();
    updateConfirmEnabled();
}

ProfilePickerDialog::~ProfilePickerDialog()
{
    delete ui;
}

void ProfilePickerDialog::setIntro(const QString &text)
{
    ui->labelIntro->setText(text);
    ui->labelIntro->setVisible(!text.isEmpty());
}

void ProfilePickerDialog::setConfirmText(const QString &text)
{
    if (QPushButton *ok = ui->buttonBox->button(QDialogButtonBox::Ok))
        ok->setText(text);
}

void ProfilePickerDialog::setRows(const QList<Row> &rows, bool checkAllByDefault)
{
    m_rows = rows;

    // Rebuild the inspection stack: page 0 is a blank placeholder (no/invalid selection), then one page
    // per row. The dialog owns each row's details widget from here — addWidget reparents it.
    while (ui->stackDetails->count() > 0) {
        QWidget *w = ui->stackDetails->widget(0);
        ui->stackDetails->removeWidget(w);
        w->deleteLater();
    }
    ui->stackDetails->addWidget(new QWidget(ui->stackDetails));

    // Populate the list with signals blocked so the per-item itemChanged storm doesn't churn state.
    // The delegate paints the title/summary/badges; the item text (plain title) only feeds keyboard
    // type-search.
    {
        const QSignalBlocker blocker(ui->listWidget);
        ui->listWidget->clear();
        for (const Row &r : m_rows) {
            auto *item = new QListWidgetItem(r.title, ui->listWidget);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(checkAllByDefault ? Qt::Checked : Qt::Unchecked);
            ui->stackDetails->addWidget(r.details ? r.details : new QWidget(ui->stackDetails));
        }
    }

    if (!m_rows.isEmpty())
        ui->listWidget->setCurrentRow(0);
    else
        ui->stackDetails->setCurrentIndex(0);

    updateConfirmEnabled();
}

QList<int> ProfilePickerDialog::checkedIndices() const
{
    QList<int> out;
    for (int i = 0; i < ui->listWidget->count(); ++i)
        if (ui->listWidget->item(i)->checkState() == Qt::Checked)
            out.append(i);
    return out;
}

void ProfilePickerDialog::onCurrentRowChanged(int row)
{
    // Page 0 is the blank placeholder; row r maps to page r + 1.
    ui->stackDetails->setCurrentIndex(row < 0 ? 0 : row + 1);
}

void ProfilePickerDialog::onSelectAll()
{
    for (int i = 0; i < ui->listWidget->count(); ++i)
        ui->listWidget->item(i)->setCheckState(Qt::Checked);
}

void ProfilePickerDialog::onSelectNone()
{
    for (int i = 0; i < ui->listWidget->count(); ++i)
        ui->listWidget->item(i)->setCheckState(Qt::Unchecked);
}

void ProfilePickerDialog::updateConfirmEnabled()
{
    if (QPushButton *ok = ui->buttonBox->button(QDialogButtonBox::Ok))
        ok->setEnabled(!checkedIndices().isEmpty());
}
