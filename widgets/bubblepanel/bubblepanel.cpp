#include "bubblepanel.h"
#include "ui_bubblepanel.h"

BubblePanel::BubblePanel(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::BubblePanel)
{
    ui->setupUi(this);
}

BubblePanel::~BubblePanel()
{
    delete ui;
}
