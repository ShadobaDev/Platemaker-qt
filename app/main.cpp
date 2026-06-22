#include "mainwindow.h"

#include <QApplication>
#include <QSettings>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // App identity + storage backend for QSettings. With IniFormat, settings
    // land in a real file under the OS app-config dir on every platform:
    //   Windows: %APPDATA%/Platemaker/Platemaker.ini
    //   Linux:   ~/.config/Platemaker/Platemaker.ini
    // After this, a default-constructed QSettings everywhere resolves here.
    QCoreApplication::setOrganizationName("Platemaker");
    QCoreApplication::setApplicationName("Platemaker");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    a.setWindowIcon(QIcon("icons/icon-red.ico"));
    MainWindow w;
    w.show();
    return QApplication::exec();
}
