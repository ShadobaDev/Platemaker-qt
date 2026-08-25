#include "mainwindow.h"
#include "menubaricontextstyle.hpp"

#include <QApplication>
#include <QSettings>
#include <QStyleHints>
#include <QOperatingSystemVersion>
#include <QIcon>
#include <QSvgRenderer>
#include <QPalette>
#include <QColor>
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

namespace {
// An explicit dark palette for the Fusion fallback (Windows 10). Fusion's own standard palette is
// light, and QStyleHints::setColorScheme(Dark) does not reliably darken it on the Win10 platform
// theme — so palette-driven text (menu items, list rows with no explicit colour) comes out black on
// the app's hardcoded-dark chrome, readable only when a selection/hover paints the OS accent behind
// it. A palette set directly on the QApplication is not reset by the style, so it holds. Only applied
// on the Fusion path; the native windows11 path keeps its own (already-correct) dark palette. Colours
// mirror the hardcoded dark dialogs (#1e1e1e / #2d2d2d / #e0e0e0) and the app accent (#0078d7), so
// unstyled chrome matches the styled dialogs and hover/selection is the app blue, not the OS accent.
QPalette fusionDarkPalette()
{
    const QColor window (0x2d, 0x2d, 0x2d);
    const QColor base   (0x1e, 0x1e, 0x1e);
    const QColor text   (0xe0, 0xe0, 0xe0);
    const QColor dim    (0x80, 0x80, 0x80);
    const QColor accent (0x00, 0x78, 0xd7);

    QPalette p;
    p.setColor(QPalette::Window,          window);
    p.setColor(QPalette::WindowText,      text);
    p.setColor(QPalette::Base,            base);
    p.setColor(QPalette::AlternateBase,   window);
    p.setColor(QPalette::ToolTipBase,     base);
    p.setColor(QPalette::ToolTipText,     text);
    p.setColor(QPalette::Text,            text);
    p.setColor(QPalette::Button,          window);
    p.setColor(QPalette::ButtonText,      text);
    p.setColor(QPalette::BrightText,      QColor(Qt::white));
    p.setColor(QPalette::Link,            accent);
    p.setColor(QPalette::Highlight,       accent);
    p.setColor(QPalette::HighlightedText, QColor(Qt::white));
    p.setColor(QPalette::PlaceholderText, dim);

    p.setColor(QPalette::Disabled, QPalette::Text,       dim);
    p.setColor(QPalette::Disabled, QPalette::WindowText, dim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, dim);
    return p;
}
} // namespace

int main(int argc, char *argv[])
{
#ifdef _WIN32
    // First thing in main(): must precede any post-main dynamic DLL load (Qt plugins, libvips ops).
    restrictDllSearchPath();
#endif


    // Log the in-flight C++ exception on a terminate() — an uncaught exception escaping a slot / the
    // event loop, a noexcept violation, or a pure-virtual call — so it lands in the Qt log / debugger
    // output instead of a silent abort. Cheap C++-side hygiene; it does NOT catch a
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

    // Anchor Qt6Svg.dll as a load-time dependency of this executable. SVG is reached only at runtime,
    // through Qt's qsvg image / qsvgicon iconengine plugins (the menu-bar icons, the tab close-X, any
    // ":/...svg"), so nothing here references a Qt6Svg symbol: the linker imports only Qt6Core/Gui/Widgets
    // and Qt6Svg.dll would be pulled in lazily, when those plugins first load. But restrictDllSearchPath()
    // above has by then stripped PATH, and Qt6Svg.dll lives in the Qt bin dir (reached via PATH), not the
    // app dir of a non-deployed build — so Windows can no longer find it, the qsvg plugins silently fail to
    // load, and every SVG renders blank. Constructing a QSvgRenderer forces Qt6Svg.dll to be a load-time
    // import, resolved at process start (before the hardening) exactly like the other Qt DLLs, so it is
    // already in memory when the plugins load. Deployed builds co-locate it in the app dir; this also
    // covers the dev run straight from the build directory. (qsvgicon.dll imports Qt6Svg.dll — verified.)
    { QSvgRenderer qtSvgLoadTimeAnchor; Q_UNUSED(qtSvgLoadTimeAnchor); }

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
    // PM_FORCE_FUSION (a runtime *environment* variable, not a CMake option) forces the Fusion path so a
    // Win11 dev box can preview the Win10 look — set it under Qt Creator: Projects > Run > Run Environment.
    // Hidden testing switch; harmless when unset.
    const bool forceFusion = qEnvironmentVariableIsSet("PM_FORCE_FUSION");
    const bool nativeDarkCapable = !forceFusion &&
        QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11;
#else
    const bool nativeDarkCapable = true;   // macOS / Fusion honour the scheme themselves
#endif

    a.styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    // Menu-bar icon+text proxy over the platform style (QMenuBar shows only one otherwise): the native
    // default where it can render dark (windows11), otherwise Fusion (Win10). MenuBarIconTextStyle
    // inherits QProxyStyle's constructors, so the key selects the wrapped base.
    a.setStyle(nativeDarkCapable ? new MenuBarIconTextStyle
                                 : new MenuBarIconTextStyle(QStringLiteral("Fusion")));

    // Fusion's own palette is light and the dark-scheme hint above does not darken it on the Win10
    // platform theme, so palette-driven text (menu items, list rows with no explicit colour) renders
    // black on the app's dark chrome — readable only under a selection/hover that paints the OS accent.
    // A palette set directly on the QApplication is not reset by the style, so force an explicit dark one
    // on the Fusion path. The native windows11 path already renders dark from the scheme, so leave it be.
    if (!nativeDarkCapable)
        a.setPalette(fusionDarkPalette());

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
