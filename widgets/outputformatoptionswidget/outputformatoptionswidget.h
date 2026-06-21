#ifndef OUTPUTFORMATOPTIONSWIDGET_H
#define OUTPUTFORMATOPTIONSWIDGET_H

#include <QWidget>

#include <platemaker/models/output_profile.hpp>

namespace Ui { class OutputFormatOptionsWidget; }

/**
 * \brief Reusable editor for an OutputProfile's image format + per-format encoding
 *        options (PNG / JPEG / WebP).
 *
 * A format dropdown reveals exactly one options group. The single source of truth
 * for the model↔widget mapping (incl. the JPEG subsampling enum↔index), embedded in
 * both the project Output tab and OutputProfileDialog.
 */
class OutputFormatOptionsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit OutputFormatOptionsWidget(QWidget* parent = nullptr);
    ~OutputFormatOptionsWidget() override;

    // Load the format + all option structs from \p profile (does not emit edited()).
    void setFromProfile(const Platemaker::Models::OutputProfile& profile);
    // Write the format + all option structs into \p profile (name/slicing untouched).
    void applyToProfile(Platemaker::Models::OutputProfile& profile) const;

signals:
    void edited();   // any control changed by the user

private slots:
    void onFormatChanged();
    void onAnyOptionChanged();

private:
    void updateVisibility();

    Ui::OutputFormatOptionsWidget* ui;
    bool m_loading = false;   // suppresses edited() while setFromProfile() runs
};

#endif // OUTPUTFORMATOPTIONSWIDGET_H
