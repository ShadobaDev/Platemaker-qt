#include "mainwindow.h"
#include "menubaricontextstyle.hpp"

#include <QApplication>
#include <QSettings>
#include <QStyleHints>
#include <QIcon>

// Injected by CMake (target_compile_definitions); fallback keeps main.cpp buildable.
#ifndef PLATEMAKER_GUI_VERSION
#define PLATEMAKER_GUI_VERSION "unknown"
#endif

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // The app is a dark-only design (every dialog hardcodes dark stylesheets: #1e1e1e / #2d2d2d
    // backgrounds, #e0e0e0 text). Left to the OS theme, the plain widgets follow a light OS setting
    // while that light-grey text stays put — unreadable. Force the dark colour scheme so the native
    // style renders dark regardless of the OS setting, and we keep the platform's own look (on Windows
    // the windows11 style: lighter-grey rounded controls, the accent left-bar on selected rows).
    // Requires Qt 6.8+ (QStyleHints::setColorScheme); the CMake minimum is pinned accordingly.
    a.styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    // Restore icon+text on top-level menu-bar items (QMenuBar shows only one otherwise). The proxy
    // wraps the platform's default style, so the native look is preserved.
    a.setStyle(new MenuBarIconTextStyle);

    // App identity + storage backend for QSettings. With IniFormat, settings
    // land in a real file under the OS app-config dir on every platform:
    //   Windows: %APPDATA%/Platemaker/Platemaker.ini
    //   Linux:   ~/.config/Platemaker/Platemaker.ini
    // After this, a default-constructed QSettings everywhere resolves here.
    QCoreApplication::setOrganizationName("Platemaker");
    QCoreApplication::setApplicationName("Platemaker");
    QCoreApplication::setApplicationVersion(QStringLiteral(PLATEMAKER_GUI_VERSION));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    a.setWindowIcon(QIcon("icons/icon-red.ico"));
    MainWindow w;
    w.show();
    return QApplication::exec();
}
