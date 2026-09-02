#include "ccpanel.h"
#include "ui_ccpanel.h"

CcPanel::CcPanel(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::CcPanel)
{
    ui->setupUi(this);
}

CcPanel::~CcPanel()
{
    delete ui;
}
