#ifndef LICENCEDIALOG_H
#define LICENCEDIALOG_H

#include <QDialog>

namespace Ui { class LicenceDialog; }

/**
 * @brief Modal viewer for a single third-party licence text.
 *
 * Opened from the About dialog when a licence identifier is clicked. The licence texts are shipped
 * beside the executable in credits/licenses/ (see the product SBOM); this dialog reads the file for
 * the requested component and shows it read-only, or a short notice pointing at the SBOM when the
 * text is not bundled for that identifier.
 */
class LicenceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LicenceDialog(QWidget* parent = nullptr);
    ~LicenceDialog() override;

    /**
     * @brief Show the licence text for one component.
     * @param componentLabel Heading, e.g. "libvips — LGPL-2.1-or-later".
     * @param licenceFilePath Absolute path to the licence text file (may not exist).
     */
    void showLicence(const QString& componentLabel, const QString& licenceFilePath);

private:
    Ui::LicenceDialog* ui;   //!< Qt Designer-generated UI (ui_licencedialog.h).
};

#endif // LICENCEDIALOG_H
