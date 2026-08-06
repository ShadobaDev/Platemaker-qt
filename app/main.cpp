#include "mainwindow.h"
#include "menubaricontextstyle.hpp"

#include <QApplication>
#include <QSettings>
#include <QStyleHints>
#include <QIcon>
#include <QDebug>

#include <cstdlib>
#include <exception>

// Injected by CMake (target_compile_definitions); fallback keeps main.cpp buildable.
#ifndef PLATEMAKER_GUI_VERSION
#define PLATEMAKER_GUI_VERSION "unknown"
#endif

int main(int argc, char *argv[])
{
    // Log the in-flight C++ exception on a terminate() — an uncaught exception escaping a slot / the
    // event loop, a noexcept violation, or a pure-virtual call — so it lands in the Qt log / debugger
    // output instead of a silent abort. Cheap C++-side hygiene (see docs/TODO.md); it does NOT catch a
    // hardware fault such as a segfault (that is an OS signal / SEH, not a C++ exception).
    std::set_terminate([] {
        if (std::exception_ptr e = std::current_exception()) {
            try { std::rethrow_exception(e); }
            catch (const std::exception& ex) { qCritical("Fatal: unhandled exception: %s", ex.what()); }
            catch (...)                       { qCritical("Fatal: unhandled non-standard exception."); }
        } else {
            qCritical("Fatal: terminate() called with no active exception.");
        }
        std::abort();
    });

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
    // Load from the compiled Qt resource, not a filesystem path: a relative path resolves against
    // the working directory, so it silently yielded a null icon whenever the app was launched from
    // anywhere but the install folder (the About dialog's icon band exposed the failure). The .ico
    // carries all sizes, so the taskbar/window and the 64px About pixmap both come from one asset.
    a.setWindowIcon(QIcon(QStringLiteral(":/icons/app")));
    MainWindow w;
    w.show();
    return QApplication::exec();
}
