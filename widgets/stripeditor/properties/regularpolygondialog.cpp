#include "properties/regularpolygondialog.hpp"
#include "ui_regularpolygondialog.h"

#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>

#include "shapes.hpp"   // Geometry::regularNodes()

namespace StripEdit {

RegularPolygonDialog::RegularPolygonDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::RegularPolygonDialog)
    , m_seed(QRandomGenerator::global()->generate())
{
    ui->setupUi(this);
    const auto moved = [this] { emit nodesChanged(nodes()); };
    for (QSpinBox* spin : {ui->spinPoints, ui->spinInner, ui->spinTurn, ui->spinIrregularity})
        connect(spin, &QSpinBox::valueChanged, this, moved);
    connect(ui->pushButtonReroll, &QPushButton::clicked, this, [this, moved] {
        m_seed = QRandomGenerator::global()->generate();
        moved();
    });
    // Nothing to deal again while nothing varies.
    const auto irregular = [this] { ui->pushButtonReroll->setEnabled(ui->spinIrregularity->value() > 0); };
    connect(ui->spinIrregularity, &QSpinBox::valueChanged, this, irregular);
    irregular();
}

RegularPolygonDialog::~RegularPolygonDialog()
{
    delete ui;
}

QList<ShapeNode> RegularPolygonDialog::nodes() const
{
    return Geometry::regularNodes(ui->spinPoints->value(), ui->spinInner->value() / 100.0, ui->spinTurn->value(),
                                  ui->spinIrregularity->value() / 100.0, m_seed);
}

}  // namespace StripEdit
