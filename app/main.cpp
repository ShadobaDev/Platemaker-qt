#include "mainwindow.h"
#include "menubaricontextstyle.hpp"

#include <QApplication>
#include <QSettings>
#include <QStyleHints>
#include <QOperatingSystemVersion>
#include <QIcon>
#include <QDebug>

#include <cstdlib>
#include <exception>

#ifdef _WIN32
#include <windows.h>

// LOAD_LIBRARY_SEARCH_* live in libloaderapi.h when _WIN32_WINNT >= 0x0602; define them defensively so
// the call compiles regardless of the MinGW headers' target-version defaults.
#ifndef LOAD_LIBRARY_SEARCH_APPLICATION_DIR
#define LOAD_LIBRARY_SEARCH_APPLICATION_DIR 0x00000200
#endif
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

// Defence-in-depth for an unsigned, DLL-heavy app: strip the current working directory and PATH from
// the default DLL search, leaving only the application dir (our whole bundled closure is co-located
// there — see CMakeLists) and System32. This closes DLL search-order hijacking / planting for every
// DLL loaded *after* this point (Qt plugins, libvips operation DLLs at render time); the exe's static
// imports are already resolved before main() runs, so startup linkage is unaffected. It does NOT stop
// hook-based injection (that would need ProcessExtensionPointDisablePolicy, deliberately deferred — see
// docs/SPECIFICATION.md) and does NOT affect SmartScreen. Resolved dynamically so this degrades to a
// no-op on pre-Windows-8 hosts instead of failing to load, and sidesteps MinGW header quirks.
static void restrictDllSearchPath()
{
    using SetDefaultDllDirectories_t = BOOL(WINAPI *)(DWORD);
    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    const auto setDefaultDllDirectories = kernel32
        ? reinterpret_cast<SetDefaultDllDirectories_t>(
              reinterpret_cast<void *>(GetProcAddress(kernel32, "SetDefaultDllDirectories")))
        : nullptr;

    if (!setDefaultDllDirectories) {
        qWarning("DLL hardening: SetDefaultDllDirectories unavailable (pre-Win8?); search path unchanged.");
        return;
    }
    if (setDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32))
        qDebug("DLL hardening: default search path restricted to application dir + System32.");
    else
        qWarning("DLL hardening: SetDefaultDllDirectories failed (GetLastError=%lu).", GetLastError());
}
#endif // _WIN32

// Injected by CMake (target_compile_definitions); fallback keeps main.cpp buildable.
#ifndef PLATEMAKER_GUI_VERSION
#define PLATEMAKER_GUI_VERSION "unknown"
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
    // First thing in main(): must precede any post-main dynamic DLL load (Qt plugins, libvips ops).
    restrictDllSearchPath();
#endif


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
    //
    // Windows 10's native (windowsvista) style honours no dark scheme — its Win32 controls have no dark
    // theme — so on Win10 the hint below only tints the palette while native-drawn controls, combo
    // popups and menus stay light, an unreadable mix against the dark dialogs. Fusion is fully
    // palette-driven and renders a consistent dark on any Windows version, so fall back to it there.
    // Windows 11 keeps its native windows11 style (stock look), which honours the dark scheme itself.
#ifdef _WIN32
    const bool nativeDarkCapable =
        QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11;
#else
    const bool nativeDarkCapable = true;   // macOS / Fusion honour the scheme themselves
#endif

    a.styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    // Restore icon+text on top-level menu-bar items (QMenuBar shows only one otherwise). The proxy
    // wraps a base style, so the platform look is preserved: the native default where it can render dark,
    // otherwise Fusion (Win10). MenuBarIconTextStyle inherits QProxyStyle's constructors, so the key
    // selects the wrapped base.
    a.setStyle(nativeDarkCapable ? new MenuBarIconTextStyle
                                 : new MenuBarIconTextStyle(QStringLiteral("Fusion")));

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
