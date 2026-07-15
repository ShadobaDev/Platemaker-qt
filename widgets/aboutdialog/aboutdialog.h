#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>

namespace Ui { class AboutDialog; }

/**
 * @brief Read-only "About" dialog: version, authors, and a (mock) manual.
 *
 * A single tabbed dialog opened from the Help / Authors / Version menu actions;
 * each action selects the matching starting tab via the Tab constructor argument.
 * All content is static (filled in the constructor) — nothing here mutates state.
 */
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    /** @brief Which tab the dialog opens on. */
    enum class Tab { About = 0, Authors = 1, Manual = 2 };

    /**
     * @brief Builds the dialog and selects the initial tab.
     * @param initial Tab to show first (About/Authors/Manual).
     * @param parent  Owning widget, if any.
     */
    explicit AboutDialog(Tab initial = Tab::About, QWidget* parent = nullptr);
    ~AboutDialog() override;

private:
    Ui::AboutDialog* ui;   //!< Qt Designer-generated UI (ui_aboutdialog.h).
};

#endif // ABOUTDIALOG_H
