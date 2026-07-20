cmake_minimum_required(VERSION 3.19)
project(Platemaker VERSION 1.1.0 LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Concurrent Svg)

qt_standard_project_setup()

# qt_standard_project_setup() enables AUTOMOC/AUTOUIC but NOT AUTORCC — turn it on so
# the .qrc below (SVG icons) is compiled into the binary (otherwise :/icons/* is missing).
set(CMAKE_AUTORCC ON)

qt_add_executable(Platemaker
    WIN32 MACOSX_BUNDLE
    # App entry point.
    app/main.cpp
    # App icon
    app/app.rc
    # Qt resources — SVG icons (close button + menu icons), compiled by AUTORCC.
    app/resources.qrc
    # Custom style — icon + text on menu-bar items.
    app/menubaricontextstyle/menubaricontextstyle.cpp app/menubaricontextstyle/menubaricontextstyle.hpp

    # MainWindow — class split across per-concern .cpp (same MainWindow class).
    # Each .ui lives next to its .cpp, so AUTOUIC resolves ui_*.h by default.
    mainwindow/mainwindow.cpp mainwindow/mainwindow.h
    mainwindow/workspace.cpp mainwindow/projects.cpp mainwindow/render.cpp
    mainwindow/renderbatch.cpp
    mainwindow/profiles.cpp mainwindow/templates.cpp mainwindow/about.cpp
    mainwindow/mainwindow.ui

    # Project — class split across per-concern .cpp (same Project class).
    widgets/project/project.cpp widgets/project/project.h
    widgets/project/input.cpp widgets/project/output.cpp
    widgets/project/project.ui

    # Widgets / dialogs — each in its own folder under widgets/ (.cpp/.h/.ui together).
    widgets/imagetile/imagetile.cpp widgets/imagetile/imagetile.h widgets/imagetile/imagetile.ui
    widgets/canvasprofiledialog/canvasprofiledialog.cpp widgets/canvasprofiledialog/canvasprofiledialog.h widgets/canvasprofiledialog/canvasprofiledialog.ui
    widgets/managecanvasprofilesdialog/managecanvasprofilesdialog.cpp widgets/managecanvasprofilesdialog/managecanvasprofilesdialog.h widgets/managecanvasprofilesdialog/managecanvasprofilesdialog.ui
    widgets/manageoutputprofilesdialog/manageoutputprofilesdialog.cpp widgets/manageoutputprofilesdialog/manageoutputprofilesdialog.h widgets/manageoutputprofilesdialog/manageoutputprofilesdialog.ui
    widgets/outputprofiledialog/outputprofiledialog.cpp widgets/outputprofiledialog/outputprofiledialog.h widgets/outputprofiledialog/outputprofiledialog.ui
    widgets/templatesdialog/templatesdialog.cpp widgets/templatesdialog/templatesdialog.h widgets/templatesdialog/templatesdialog.ui
    widgets/outputformatoptionswidget/outputformatoptionswidget.cpp widgets/outputformatoptionswidget/outputformatoptionswidget.h widgets/outputformatoptionswidget/outputformatoptionswidget.ui
    widgets/aboutdialog/aboutdialog.cpp widgets/aboutdialog/aboutdialog.h widgets/aboutdialog/aboutdialog.ui
    widgets/renderworker/renderworker.cpp widgets/renderworker/renderworker.h
)

# Every class/header lives in its own folder; add each to the include path so
# basename includes (#include "canvasprofiledialog.h", "mainwindow.h", …) resolve
# from any source file regardless of folder.
target_include_directories(Platemaker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app/menubaricontextstyle
    ${CMAKE_CURRENT_SOURCE_DIR}/mainwindow
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/project
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/imagetile
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/canvasprofiledialog
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/managecanvasprofilesdialog
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/manageoutputprofilesdialog
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/outputprofiledialog
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/templatesdialog
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/outputformatoptionswidget
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/aboutdialog
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/renderworker
)

if(1)
    include(FetchContent)

    # ─── platemaker ────────────────────────────────────────────────────────────────
    # Discovery order:
    #   1. LIBPLATEMAKER_DIR cache variable — explicit local dev package (set once, cached)
    #   2. find_package                     — system install or CMAKE_PREFIX_PATH
    #   3. FetchContent                     — downloads pre-built package from GitHub Releases
    # ───────────────────────────────────────────────────────────────────────────────
    set(LIBPLATEMAKER_VERSION "0.2.1")
    set(LIBPLATEMAKER_DIR "" CACHE PATH "Path to a local platemaker dev package")
    message("Local libplatemaker path: ${LIBPLATEMAKER_DIR}")

    if(LIBPLATEMAKER_DIR)
        list(PREPEND CMAKE_PREFIX_PATH "${LIBPLATEMAKER_DIR}")
        message("Set libplatemaker path: ${LIBPLATEMAKER_DIR}")
    endif()

    # Pinned: this app uses APIs added in libplatemaker 0.2.0 (sanitize/applyProcessingResults
    # signatures). Without a minimum, an older install would configure cleanly and only fail
    # later as confusing compile errors. Same variable drives the download URL below, so the
    # required and fetched versions cannot drift apart.
    find_package(platemaker ${LIBPLATEMAKER_VERSION} CONFIG QUIET)

    if(NOT platemaker_FOUND)
        include(FetchContent)

        if(WIN32)
            set(_pm_archive "platemaker-dev-${LIBPLATEMAKER_VERSION}-windows-mingw-release.zip")
        else()
            set(_pm_archive "platemaker-dev-${LIBPLATEMAKER_VERSION}-linux-x86_64-release.tar.gz")
        endif()

        FetchContent_Declare(
            platemaker_pkg
            URL "https://github.com/ShadobaDev/PlateMaker/releases/download/${LIBPLATEMAKER_VERSION}/${_pm_archive}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(platemaker_pkg)
        FetchContent_GetProperties(platemaker_pkg SOURCE_DIR _pm_src)

        # The release archive extracts flat — LICENSE/bin/include/lib sit directly in
        # the source dir, with the package config at lib/cmake/platemaker/. So the
        # package root *is* _pm_src; there is no platemaker-dev-* subdirectory to glob
        # for. (Globbing for one yielded an empty list, and list(GET) on it aborted the
        # configure with "index 0 out of range" — which read like a download failure.)
        list(PREPEND CMAKE_PREFIX_PATH "${_pm_src}")
        find_package(platemaker ${LIBPLATEMAKER_VERSION} CONFIG REQUIRED)
    endif()

    target_link_libraries(${PROJECT_NAME} PRIVATE Platemaker::platemaker)

    # Version metadata for the About dialog, injected as compile definitions:
    #   PLATEMAKER_GUI_VERSION — this app's project() version
    #   PLATEMAKER_LIB_VERSION — libplatemaker version from find_package's config-version
    #                            file (platemaker_VERSION). Read from CMake, so the GUI
    #                            never depends on a lib header being present/updated.
    # Licences shown in the About dialog. Kept here rather than in the source so a licence
    # change is a one-line edit in the build file instead of a hunt through the UI code.
    #
    # NOTE: the libplatemaker and libvips entries describe *someone else's* code, so they can
    # drift without this file noticing. The lasting fix is for the lib to report its own
    # dependencies and licences — tracked in docs/TODO.md.
    set(PLATEMAKER_GUI_LICENCE  "GPL-3.0")
    set(PLATEMAKER_LIB_LICENCE  "LGPL-3.0-or-later")
    set(PLATEMAKER_QT_LICENCE   "LGPL-3.0")
    set(PLATEMAKER_VIPS_LICENCE "LGPL-2.1-or-later")
    set(PLATEMAKER_JSON_LICENCE "MIT")

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        PLATEMAKER_GUI_VERSION="${PROJECT_VERSION}"
        PLATEMAKER_LIB_VERSION="${platemaker_VERSION}"
        PLATEMAKER_GUI_LICENCE="${PLATEMAKER_GUI_LICENCE}"
        PLATEMAKER_LIB_LICENCE="${PLATEMAKER_LIB_LICENCE}"
        PLATEMAKER_QT_LICENCE="${PLATEMAKER_QT_LICENCE}"
        PLATEMAKER_VIPS_LICENCE="${PLATEMAKER_VIPS_LICENCE}"
        PLATEMAKER_JSON_LICENCE="${PLATEMAKER_JSON_LICENCE}")

    # Windows: copy platemaker.dll and all libvips runtime DLLs next to the executable
    if(WIN32)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<TARGET_FILE_DIR:Platemaker::platemaker>"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            COMMENT "Copying platemaker runtime DLLs"
            VERBATIM
        )
    endif()
endif()

target_link_libraries(Platemaker
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Concurrent
        Qt::Svg   # pulls in the qsvg image plugin (SVG icons) — windeployqt deploys it
)

install(TARGETS Platemaker
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET Platemaker
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})   # windeployqt — deploys Qt's DLLs (incl. its older MinGW runtime)

# MUST come AFTER windeployqt above. Installs platemaker.dll + all its MSYS2 dependency
# DLLs (libvips, libuhdr, glib, …) AND the MSYS2 runtime (libwinpthread-1, libgcc_s_seh-1,
# libstdc++-6). windeployqt does NOT deploy these (they aren't Qt), and copying them last
# makes the newer MSYS2 runtime overwrite Qt's older copy — resolving libuhdr-1.dll's
# missing pthread_cond_timedwait64 import when running outside Qt Creator.
if(WIN32)
    install(DIRECTORY "$<TARGET_FILE_DIR:Platemaker::platemaker>/"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        FILES_MATCHING PATTERN "*.dll")
endif()

# ─── Install platemaker's own runtime DLLs LAST ────────────────────────────────
# windeployqt (above) ships its own copies of libgcc_s_seh-1.dll / libwinpthread-1.dll
# / libstdc++-6.dll from Qt's bundled MinGW toolchain. platemaker/libuhdr were built
# with a different MinGW (MSYS2 mingw64) whose ABI is incompatible (missing symbols
# like pthread_cond_timedwait64). These install() calls MUST run after
# install(SCRIPT ${deploy_script}) above so they win the "last one wins" file copy,
# overwriting windeployqt's incompatible runtime DLLs with the matching ones.
if(WIN32)
    # All of platemaker's own runtime DLLs (libvips, libuhdr, etc.) — same folder
    # the POST_BUILD step already copies from.
    install(DIRECTORY "$<TARGET_FILE_DIR:Platemaker::platemaker>/"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        FILES_MATCHING PATTERN "*.dll"
    )

    # platemaker/libuhdr were built with MSYS2 MinGW64 — its compiler runtime DLLs
    # (winpthreads/libgcc) are not part of the platemaker package itself, but
    # Platemaker.exe needs an ABI-matching copy of them at install time, or it will
    # fail to load libuhdr-1.dll with "punktu wejścia procedury pthread_cond_timedwait64".
    set(PLATEMAKER_MSYS2_MINGW64_BIN "C:/msys64/mingw64/bin" CACHE PATH
        "bin/ dir of the MSYS2 MinGW64 toolchain platemaker/libuhdr were built with")
    if(EXISTS "${PLATEMAKER_MSYS2_MINGW64_BIN}/libwinpthread-1.dll")
        install(FILES
            "${PLATEMAKER_MSYS2_MINGW64_BIN}/libwinpthread-1.dll"
            "${PLATEMAKER_MSYS2_MINGW64_BIN}/libgcc_s_seh-1.dll"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    else()
        message(WARNING "MSYS2 MinGW64 runtime not found at ${PLATEMAKER_MSYS2_MINGW64_BIN} — "
                         "Platemaker.exe may fail to load libuhdr-1.dll on machines without MSYS2.")
    endif()
endif()

# --- Inno Setup installer target -------------------------------------------
if(WIN32)
    find_program(ISCC_EXECUTABLE ISCC
        PATHS
            "C:/Program Files (x86)/Inno Setup 6"
            "C:/Program Files/Inno Setup 6"
            "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6"
        DOC "Inno Setup compiler (ISCC.exe)"
    )

    set(INSTALLER_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/install"
        CACHE PATH "Staging prefix used by cmake --install before Inno Setup packaging")

    if(ISCC_EXECUTABLE)
        add_custom_target(installer
            DEPENDS Platemaker
            COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
                    --prefix "${INSTALLER_INSTALL_PREFIX}"
            COMMAND ${ISCC_EXECUTABLE}
                    "/DMyAppVersion=${PROJECT_VERSION}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/Platemaker.iss"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Installing and packaging Platemaker ${PROJECT_VERSION}..."
            VERBATIM
        )
        message(STATUS "Inno Setup found: ${ISCC_EXECUTABLE} -- run: cmake --build . --target installer")
    else()
        message(STATUS "Inno Setup not found -- install via: winget install JRSoftware.InnoSetup")
    endif()
endif()

