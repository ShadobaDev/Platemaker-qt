#ifndef CCPANEL_H
#define CCPANEL_H

#include <QWidget>

namespace Ui { class CcPanel; }

/**
 * @brief Tool-options panel for the strip editor's Colour-correction (Grade) tool.
 *
 * Empty scaffold for now — the grade controls (brightness / contrast / saturation, a curve editor, the
 * ICC toggle and the per-page exclusion list) land in a later increment. Shown as a page of the strip
 * editor's tool-options stack.
 */
class CcPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CcPanel(QWidget* parent = nullptr);
    ~CcPanel() override;

private:
    Ui::CcPanel* ui;
};

#endif // CCPANEL_H
