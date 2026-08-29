#include "profilepickerdialog.h"
#include "ui_profilepickerdialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontMetrics>
#include <QListWidgetItem>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QWidget>

namespace {

// Paints each list row as two lines (name / summary) with rounded "chip" badges after the text.
// Reads the row data straight from the dialog's row list (index == row), so the badges stay structured
// (text + colour) instead of baked into a string. Only text/badges are custom-painted; the base style
// still draws the background, selection and check indicator, so checkboxes and selection are unchanged.
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

        if (!m_rows || index.row() < 0 || index.row() >= m_rows->size())
            return;
        const ProfilePickerDialog::Row& r = m_rows->at(index.row());

        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, nullptr);

        const QFont       baseFont = opt.font;
        const QFontMetrics fm(baseFont);
        const int lineH   = fm.height();
        const int lineGap = 4;
        const int totalH  = lineH * 2 + lineGap;
        int       y       = textRect.top() + qMax(0, (textRect.height() - totalH) / 2);

        const QColor nameColour = opt.palette.color(
            (opt.state & QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text);
        const QColor dimColour  = opt.palette.color(QPalette::PlaceholderText);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        drawLine(painter, baseFont, r.title,   nameColour, r.titleBadges,
                 textRect.left(), y, lineH, textRect.right());
        y += lineH + lineGap;
        drawLine(painter, baseFont, r.summary, dimColour,  r.summaryBadges,
                 textRect.left(), y, lineH, textRect.right());
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const int lineH = QFontMetrics(opt.font).height();
        return QSize(0, lineH * 2 + 4 /*line gap*/ + 12 /*padding*/);
    }

private:
    // Slightly smaller, bold font for badges, robust to point- vs pixel-sized base fonts.
    static QFont badgeFontFor(const QFont& base)
    {
        QFont f = base;
        f.setBold(true);
        if (base.pointSizeF() > 0)
            f.setPointSizeF(base.pointSizeF() * 0.85);
        else if (base.pixelSize() > 0)
            f.setPixelSize(qMax(1, static_cast<int>(base.pixelSize() * 0.85)));
        return f;
    }

    static void drawLine(QPainter* painter, const QFont& baseFont, const QString& text,
                         const QColor& textColour, const QList<ProfilePickerDialog::Badge>& badges,
                         int x, int y, int lineH, int right)
    {
        const QFontMetrics fm(baseFont);
        painter->setFont(baseFont);
        painter->setPen(textColour);
        painter->drawText(QRect(x, y, right - x, lineH), Qt::AlignVCenter | Qt::AlignLeft, text);

        int cx = x + fm.horizontalAdvance(text);

        const QFont        badgeFont = badgeFontFor(baseFont);
        const QFontMetrics bfm(badgeFont);
        const int hpad   = 7;                   // horizontal padding inside a chip
        const int bh     = bfm.height() + 2;    // chip height (smaller than the line)
        const int radius = 5;                   // corner radius
        const int gap    = 8;                   // space before each chip (so chips never touch)

        for (const ProfilePickerDialog::Badge& b : badges) {
            const int bw = bfm.horizontalAdvance(b.text) + 2 * hpad;
            cx += gap;
            if (cx + bw > right) break;          // don't overflow the item
            const QRectF chip(cx, y + (lineH - bh) / 2.0, bw, bh);
            painter->setPen(QPen(b.colour.darker(150), 1));   // border: same hue, darker
            painter->setBrush(b.colour);
            painter->drawRoundedRect(chip, radius, radius);
            painter->setFont(badgeFont);
            painter->setPen(QColor(0x11, 0x11, 0x11));        // dark text on the light chip
            painter->drawText(chip, Qt::AlignCenter, b.text);
            cx += bw;
        }
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
