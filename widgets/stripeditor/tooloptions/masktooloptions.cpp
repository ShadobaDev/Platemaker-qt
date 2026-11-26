#include "tooloptions/masktooloptions.hpp"

#include "segmented.hpp"

#include <QFormLayout>
#include <QLabel>
#include <QSettings>
#include <QSpinBox>

namespace StripEdit {

namespace {

const auto k_subtractKey  = QStringLiteral("stripEditor/maskTools/subtract");
const auto k_sizeKey      = QStringLiteral("stripEditor/maskBrush/size");
const auto k_ellipseKey   = QStringLiteral("stripEditor/maskShape/ellipse");
const auto k_polygonalKey = QStringLiteral("stripEditor/maskLasso/polygonal");

constexpr int k_defaultSizePx = 40;     //!< About a letter's height on a page.
constexpr int k_maxSizePx     = 1000;   //!< A panel's width, about: past it a mask is drawn by its nodes.

}  // namespace

MaskToolOptions::MaskToolOptions(QWidget* parent)
    : QWidget(parent)
{
    auto* form = m_form = new QFormLayout(this);

    m_add      = segment(tr("Add"), tr("What you draw is added to the selected mask: it hides more."));
    m_subtract = segment(tr("Subtract"), tr("What you draw is taken from the selected mask: it hides less."));
    form->addRow(tr("Mode"), segmented(this, new QButtonGroup(this), {m_add, m_subtract}));

    m_size = new QSpinBox(this);
    m_size->setRange(1, k_maxSizePx);
    m_size->setSuffix(tr(" px"));
    m_size->setToolTip(tr("The brush's diameter on the strip, whatever the zoom."));
    form->addRow(tr("Size"), m_size);

    QToolButton* rectangle = segment(tr("Rectangle"), tr("Drag a box. Shift: a square; Alt: from its centre."));
    m_ellipse = segment(tr("Ellipse"), tr("Drag the box round an ellipse. Shift: a circle; Alt: from its centre."));
    m_shape   = segmented(this, new QButtonGroup(this), {rectangle, m_ellipse});
    form->addRow(tr("Shape"), m_shape);

    QToolButton* freehand = segment(tr("Freehand"), tr("Drag round the area: letting go closes it."));
    m_polygonal = segment(tr("Polygonal"),
                          tr("Click each corner; a double click, Enter or a click on the first corner closes it. "
                             "Backspace takes a corner back, Esc drops it, Shift keeps an edge at 45°."));
    m_lasso = segmented(this, new QButtonGroup(this), {freehand, m_polygonal});
    form->addRow(tr("Lasso"), m_lasso);

    m_why = new QLabel(tr("Select a mask to paint: click one on the strip, or pick it in the object stack."), this);
    m_why->setWordWrap(true);
    form->addRow(m_why);
    showTarget(false);

    const QSettings settings;
    (settings.value(k_subtractKey, false).toBool() ? m_subtract : m_add)->setChecked(true);
    m_size->setValue(settings.value(k_sizeKey, k_defaultSizePx).toInt());
    (settings.value(k_ellipseKey, false).toBool() ? m_ellipse : rectangle)->setChecked(true);
    (settings.value(k_polygonalKey, false).toBool() ? m_polygonal : freehand)->setChecked(true);
    setTool(m_tool);

    // Each pair's second button is its setting; toggled fires for either click, as the pair is exclusive.
    const auto remember = [this](QToolButton* second, const QString& key) {
        connect(second, &QToolButton::toggled, this, [this, key](bool on) {
            QSettings().setValue(key, on);
            emit changed();
        });
    };
    remember(m_subtract, k_subtractKey);
    remember(m_ellipse, k_ellipseKey);
    remember(m_polygonal, k_polygonalKey);
    connect(m_size, &QSpinBox::valueChanged, this, [this](int px) {
        QSettings().setValue(k_sizeKey, px);
        emit changed();
    });
}

void MaskToolOptions::setTool(MaskTool tool)
{
    m_tool = tool;
    m_form->setRowVisible(m_size, tool == MaskTool::Brush);
    m_form->setRowVisible(m_shape, tool == MaskTool::Shape);
    m_form->setRowVisible(m_lasso, tool == MaskTool::Lasso);
}

bool MaskToolOptions::adds() const
{
    return m_add->isChecked();
}

int MaskToolOptions::size() const
{
    return m_size->value();
}

MaskStroke MaskToolOptions::stroke() const
{
    switch (m_tool) {
    case MaskTool::Brush:
        return MaskStroke::Brush;
    case MaskTool::Shape:
        return m_ellipse->isChecked() ? MaskStroke::Ellipse : MaskStroke::Rectangle;
    case MaskTool::Lasso:
        return m_polygonal->isChecked() ? MaskStroke::Polygon : MaskStroke::Freehand;
    }
    return MaskStroke::Brush;
}

void MaskToolOptions::showTarget(bool paints)
{
    m_why->setVisible(!paints);
}

}  // namespace StripEdit
