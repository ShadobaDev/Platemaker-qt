#ifndef TEXTPANEL_H
#define TEXTPANEL_H

#include <QWidget>

namespace Ui { class TextPanel; }

/**
 * @brief Tool-options panel for the strip editor's Text tool.
 *
 * Empty scaffold for now — the text controls (font, size, colour, alignment) land in a later increment.
 * Shown as a page of the strip editor's tool-options stack.
 */
class TextPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TextPanel(QWidget* parent = nullptr);
    ~TextPanel() override;

private:
    Ui::TextPanel* ui;
};

#endif // TEXTPANEL_H
