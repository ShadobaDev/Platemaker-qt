#ifndef BUBBLEPANEL_H
#define BUBBLEPANEL_H

#include <QWidget>

namespace Ui { class BubblePanel; }

/**
 * @brief Tool-options panel for the strip editor's Bubble tool.
 *
 * Empty scaffold for now — the speech-bubble controls (shape, fill/stroke, tail) land in a later
 * increment. Shown as a page of the strip editor's tool-options stack.
 */
class BubblePanel : public QWidget
{
    Q_OBJECT

public:
    explicit BubblePanel(QWidget* parent = nullptr);
    ~BubblePanel() override;

private:
    Ui::BubblePanel* ui;
};

#endif // BUBBLEPANEL_H
