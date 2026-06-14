#ifndef CANVASPROFILEDIALOG_H
#define CANVASPROFILEDIALOG_H

#include <QDialog>
#include <QColor>
#include <platemaker/models/canvas_profile.hpp>

namespace Ui {
class CanvasProfileDialog;
}

class CanvasProfileDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CanvasProfileDialog(QWidget *parent = nullptr);

    // Call before exec() to populate fields when editing an existing profile.
    // Leave uncalled (or call with default-constructed profile) for a new profile.
    void setProfile(const Platemaker::Models::CanvasProfile &profile);

    // Call after exec() == Accepted to retrieve the filled-in profile.
    Platemaker::Models::CanvasProfile profile() const;

    ~CanvasProfileDialog() override;

private slots:
    void onSaveClicked();
    void onPickVisualColour();
    void onPickBgColour();
    void onDimensionsChanged();
    void onSafeAreaModeToggled(bool checked);

private:
    void applyColourToButton(QPushButton *button, const QColor &colour);
    void updatePreview();
    void updateSizeInfoLabel();

    Ui::CanvasProfileDialog *ui;

    QColor m_visualColour;
    QColor m_bgColour;
    bool   m_safeAreaMode = false;
};

#endif // CANVASPROFILEDIALOG_H
