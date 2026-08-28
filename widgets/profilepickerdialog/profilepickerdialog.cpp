#include "profilepickerdialog.h"
#include "ui_profilepickerdialog.h"

#include <QDialogButtonBox>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QWidget>

ProfilePickerDialog::ProfilePickerDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ProfilePickerDialog)
{
    ui->setupUi(this);

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
    {
        const QSignalBlocker blocker(ui->listWidget);
        ui->listWidget->clear();
        for (const Row &r : m_rows) {
            auto *item = new QListWidgetItem(r.summary.isEmpty() ? r.title
                                                                 : r.title + '\n' + r.summary,
                                             ui->listWidget);
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
