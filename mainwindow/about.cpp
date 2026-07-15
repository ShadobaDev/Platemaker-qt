#include "mainwindow.h"
#include "aboutdialog.h"

// About menu — Version / Authors / Help(manual) all open the one AboutDialog on
// the matching tab. Read-only, so nothing here touches the workspace.

namespace {
void showAboutDialog(QWidget* parent, AboutDialog::Tab tab)
{
    AboutDialog dlg(tab, parent);
    dlg.exec();
}
} // namespace

void MainWindow::onShowVersion()  { showAboutDialog(this, AboutDialog::Tab::About); }
void MainWindow::onShowAuthors()  { showAboutDialog(this, AboutDialog::Tab::Authors); }
void MainWindow::onShowHelp()     { showAboutDialog(this, AboutDialog::Tab::Manual); }
