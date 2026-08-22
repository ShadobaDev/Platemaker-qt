cmake_minimum_required(VERSION 3.25)
project(Platemaker VERSION 1.4.3 LANGUAGES CXX)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Concurrent Svg)

qt_standard_project_setup()

# qt_standard_project_setup() enables AUTOMOC/AUTOUIC but NOT AUTORCC — turn it on so
# the .qrc below (SVG icons) is compiled into the binary (otherwise :/icons/* is missing).
set(CMAKE_AUTORCC ON)

# Embed Windows version metadata (product name / version / company / description shown by Explorer's
# Details tab and Process Explorer), generated from the project version so it stays in sync. app.rc
# keeps the icon; this .rc adds VERSIONINFO only. Compiled by the RC tool alongside app.rc.
configure_file(app/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/version.rc" @ONLY)

qt_add_executable(Platemaker
    WIN32 MACOSX_BUNDLE
    # App entry point.
    app/main.cpp
    # App icon
    app/app.rc
    # Windows version metadata (generated from PROJECT_VERSION — see app/version.rc.in)
    "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
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
    widgets/project/projectsnapshotcommand.cpp widgets/project/projectsnapshotcommand.h
    widgets/project/workspacesnapshotcommand.cpp widgets/project/workspacesnapshotcommand.h
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
    widgets/licencedialog/licencedialog.cpp widgets/licencedialog/licencedialog.h widgets/licencedialog/licencedialog.ui
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
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets/licencedialog
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
    set(LIBPLATEMAKER_VERSION "0.5.1")
    set(LIBPLATEMAKER_DIR "" CACHE PATH "Path to a local platemaker dev package")
    message("Local libplatemaker path: ${LIBPLATEMAKER_DIR}")

    if(LIBPLATEMAKER_DIR)
        list(PREPEND CMAKE_PREFIX_PATH "${LIBPLATEMAKER_DIR}")
        message("Set libplatemaker path: ${LIBPLATEMAKER_DIR}")
    endif()

    # Pinned: the profile-editing widgets now go through Infrastructure::WorkspaceEditor and the
    # read-only Workspace::canvasProfiles()/outputProfiles() accessors, both added in libplatemaker
    # 0.3.0 (which made the profile vectors private). Without a minimum, an older install would
    # configure cleanly and only fail later as confusing compile errors. Same variable drives the
    # download URL below, so the required and fetched versions cannot drift apart.
    #
    # Bumped to 0.5.0 for the 1.4.2 release — a hard floor, not just a preference: the GUI now calls
    # ProcessingPipeline::run() with the trailing thumbnailCacheDir argument (the render output contract,
    # lib SPECIFICATION §7.0), which does not exist before 0.5.0, so an older lib fails to compile. 0.5.0
    # also carries the atomic output publish + OutputLocked that fix the re-render write failures.
    find_package(platemaker ${LIBPLATEMAKER_VERSION} CONFIG QUIET)

    if(NOT platemaker_FOUND)
        include(FetchContent)

        # Pick the dev package matching the compiler this GUI is being built with — the lib ships a
        # per-toolchain package (…-windows-msvc-… vs …-windows-mingw-…) because MSVC and MinGW are
        # ABI-incompatible (the package config's own guard rejects a mismatch). MSVC is true for
        # MSVC/clang-cl, false for MinGW/GCC.
        if(WIN32)
            if(MSVC)
                set(_pm_archive "platemaker-dev-${LIBPLATEMAKER_VERSION}-windows-msvc-release.zip")
            else()
                set(_pm_archive "platemaker-dev-${LIBPLATEMAKER_VERSION}-windows-mingw-release.zip")
            endif()
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

    # About-dialog metadata injected as compile definitions — only the two facts this app owns:
    #   PLATEMAKER_GUI_VERSION — this app's project() version
    #   PLATEMAKER_GUI_LICENCE — the GUI's own licence
    #   PLATEMAKER_QT_LICENCE  — Qt's licence (Qt is linked directly, so the GUI legitimately knows it)
    # Keeping them here means a licence change is a one-line build-file edit, not a code hunt.
    #
    # libplatemaker, libvips and nlohmann/json are deliberately NOT here: the About dialog reads
    # their versions and licences from the lib at runtime (buildInfo() / linkedComponents()), so the
    # GUI never asserts facts about code it does not own that could silently go stale.
    # SPDX identifiers (they key the licence-text files the About dialog opens).
    set(PLATEMAKER_GUI_LICENCE  "GPL-3.0-only")
    set(PLATEMAKER_QT_LICENCE   "LGPL-3.0-only")

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        PLATEMAKER_GUI_VERSION="${PROJECT_VERSION}"
        PLATEMAKER_GUI_LICENCE="${PLATEMAKER_GUI_LICENCE}"
        PLATEMAKER_QT_LICENCE="${PLATEMAKER_QT_LICENCE}")

    # -----------------------------------------------------------------------------
    # Product SBOM + licence texts (credits/)
    #
    # The lib ships its own SPDX SBOM (libplatemaker + libvips + nlohmann/json) and the licence
    # texts, located via platemaker_CREDITS_DIR from its package config. Here we MERGE that into a
    # product-level SBOM: keep the lib's packages verbatim (so the GUI never re-declares code it does
    # not own), append the two components the GUI itself links — Qt and Platemaker — and rewrite the
    # relationship graph with Platemaker at the root. This mirrors what Yocto's image-level SBOM does
    # (merge the component documents into one), done here with CMake's own string(JSON) — no extra
    # tool. The result is the machine-readable inventory (CRA / commercial consumers) that ships
    # beside the licence texts the LGPL requires us to distribute.
    # -----------------------------------------------------------------------------
    if(NOT DEFINED platemaker_CREDITS_DIR OR NOT EXISTS "${platemaker_CREDITS_DIR}/sbom.spdx.json")
        message(FATAL_ERROR "libplatemaker package has no credits/sbom.spdx.json "
            "(platemaker_CREDITS_DIR='${platemaker_CREDITS_DIR}'). Update the lib package to >= 0.2.2.")
    endif()

    set(_pm_app_credits "${CMAKE_BINARY_DIR}/credits")
    file(REMOVE_RECURSE "${_pm_app_credits}")
    file(MAKE_DIRECTORY "${_pm_app_credits}/licenses")

    # Licence texts: take the lib's set, add Platemaker's own (GPL-3.0). Qt (LGPL-3.0-only) shares the
    # LGPL-3.0 body the lib already ships (LGPL-3.0-or-later.txt), so the viewer maps it there — no
    # duplicate file.
    file(COPY "${platemaker_CREDITS_DIR}/licenses/" DESTINATION "${_pm_app_credits}/licenses")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
                   "${_pm_app_credits}/licenses/GPL-3.0-only.txt" COPYONLY)

    # The lib's Windows package also ships a human THIRD-PARTY-NOTICES.txt (the full bundled DLL graph:
    # glib, libpng, libimagequant, …). Carry it into the product credits so it reaches the installed app
    # and the About dialog's "full notices" link. (Absent on a Linux/system-libvips lib package.)
    if(EXISTS "${platemaker_CREDITS_DIR}/THIRD-PARTY-NOTICES.txt")
        file(COPY "${platemaker_CREDITS_DIR}/THIRD-PARTY-NOTICES.txt" DESTINATION "${_pm_app_credits}")
    endif()

    # Merge the SBOM with string(JSON): keep the lib's packages[], append Qt + Platemaker, rewrite the
    # relationships and the document identity to describe the product.
    file(READ "${platemaker_CREDITS_DIR}/sbom.spdx.json" _pm_sbom)
    string(JSON _pm_npkg LENGTH "${_pm_sbom}" packages)

    set(_pm_qt_pkg "{
      \"SPDXID\": \"SPDXRef-Package-Qt\",
      \"name\": \"Qt\",
      \"versionInfo\": \"${Qt6_VERSION}\",
      \"supplier\": \"Organization: The Qt Company\",
      \"downloadLocation\": \"https://github.com/qt/qtbase\",
      \"filesAnalyzed\": false,
      \"licenseConcluded\": \"LGPL-3.0-only\",
      \"licenseDeclared\": \"LGPL-3.0-only\",
      \"copyrightText\": \"NOASSERTION\",
      \"externalRefs\": [ { \"referenceCategory\": \"PACKAGE-MANAGER\", \"referenceType\": \"purl\", \"referenceLocator\": \"pkg:github/qt/qtbase@${Qt6_VERSION}\" } ]
    }")
    set(_pm_app_pkg "{
      \"SPDXID\": \"SPDXRef-Package-Platemaker\",
      \"name\": \"Platemaker\",
      \"versionInfo\": \"${PROJECT_VERSION}\",
      \"supplier\": \"Organization: ShadobaDev\",
      \"downloadLocation\": \"https://github.com/ShadobaDev/Platemaker-qt\",
      \"filesAnalyzed\": false,
      \"licenseConcluded\": \"GPL-3.0-only\",
      \"licenseDeclared\": \"GPL-3.0-only\",
      \"copyrightText\": \"Copyright (c) 2026 ShadobaDev\",
      \"externalRefs\": [ { \"referenceCategory\": \"PACKAGE-MANAGER\", \"referenceType\": \"purl\", \"referenceLocator\": \"pkg:github/ShadobaDev/Platemaker-qt@${PROJECT_VERSION}\" } ]
    }")
    string(JSON _pm_sbom SET "${_pm_sbom}" packages "${_pm_npkg}" "${_pm_qt_pkg}")
    math(EXPR _pm_npkg1 "${_pm_npkg}+1")
    string(JSON _pm_sbom SET "${_pm_sbom}" packages "${_pm_npkg1}" "${_pm_app_pkg}")

    # Product relationships: KEEP the lib's graph intact — libplatemaker → libvips / nlohmann AND the
    # whole bundled-DLL closure (libvips → glib, libpng, … plus the compiler-runtime edges) that the lib
    # SBOM now carries — then re-root the document on Platemaker and add the two edges the GUI owns.
    # (An earlier version REPLACED the array wholesale, which dropped every bundled-DLL edge and left the
    # packages as orphans — so this must augment, not overwrite.)
    string(JSON _pm_sbom SET "${_pm_sbom}" relationships 0 relatedSpdxElement
           "\"SPDXRef-Package-Platemaker\"")   # relationships[0] is DOCUMENT DESCRIBES … → Platemaker now
    string(JSON _pm_nrel LENGTH "${_pm_sbom}" relationships)
    string(JSON _pm_sbom SET "${_pm_sbom}" relationships "${_pm_nrel}"
           "{ \"spdxElementId\": \"SPDXRef-Package-Platemaker\", \"relationshipType\": \"DEPENDS_ON\", \"relatedSpdxElement\": \"SPDXRef-Package-libplatemaker\" }")
    math(EXPR _pm_nrel1 "${_pm_nrel}+1")
    string(JSON _pm_sbom SET "${_pm_sbom}" relationships "${_pm_nrel1}"
           "{ \"spdxElementId\": \"SPDXRef-Package-Platemaker\", \"relationshipType\": \"DEPENDS_ON\", \"relatedSpdxElement\": \"SPDXRef-Package-Qt\" }")

    string(TIMESTAMP _pm_sbom_stamp "%Y%m%d%H%M%S" UTC)
    string(JSON _pm_sbom SET "${_pm_sbom}" name "\"Platemaker-${PROJECT_VERSION}\"")
    string(JSON _pm_sbom SET "${_pm_sbom}" documentNamespace
           "\"https://github.com/ShadobaDev/Platemaker-qt/spdx/Platemaker-${PROJECT_VERSION}-${_pm_sbom_stamp}\"")
    file(WRITE "${_pm_app_credits}/sbom.spdx.json" "${_pm_sbom}")

    # Ship credits/ beside the executable (installed) and copy it into the build tree so the in-app
    # viewer works when running from Qt Creator without installing.
    install(DIRECTORY "${_pm_app_credits}/" DESTINATION "${CMAKE_INSTALL_BINDIR}/credits")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_pm_app_credits}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/credits"
        COMMENT "Copying credits/ (SBOM + licences) next to the executable"
        VERBATIM
    )

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

    # MinGW only: platemaker/libuhdr were built with MSYS2 MinGW64 — its compiler runtime DLLs
    # (winpthreads/libgcc) are not part of the platemaker package itself, but Platemaker.exe needs an
    # ABI-matching copy of them at install time, or it fails to load libuhdr-1.dll with
    # "punktu wejścia procedury pthread_cond_timedwait64". The MSVC build has no MinGW runtime (its C++
    # runtime is the MSVC redist, deployed by windeployqt), so this whole step is skipped there.
    if(MINGW)
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
        # Paths of the artifact Inno Setup emits (OutputDir / OutputBaseFilename in Platemaker.iss)
        # and the checksum file we write beside it. Keep in sync with Platemaker.iss if those change.
        set(_pm_installer_exe
            "${CMAKE_CURRENT_SOURCE_DIR}/installer-output/Platemaker-${PROJECT_VERSION}-Setup.exe")
        set(_pm_checksums
            "${CMAKE_CURRENT_SOURCE_DIR}/installer-output/Platemaker-${PROJECT_VERSION}-SHA256SUMS.txt")

        add_custom_target(installer
            DEPENDS Platemaker
            COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
                    --prefix "${INSTALLER_INSTALL_PREFIX}"
            COMMAND ${ISCC_EXECUTABLE}
                    "/DMyAppVersion=${PROJECT_VERSION}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/Platemaker.iss"
            # Emit SHA256SUMS.txt next to the installer (sha256sum format) for release verification.
            COMMAND ${CMAKE_COMMAND}
                    "-DINSTALLER_FILE=${_pm_installer_exe}"
                    "-DOUTPUT=${_pm_checksums}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/make_checksums.cmake"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Installing, packaging and checksumming Platemaker ${PROJECT_VERSION}..."
            VERBATIM
        )
        message(STATUS "Inno Setup found: ${ISCC_EXECUTABLE} -- run: cmake --build . --target installer")
    else()
        message(STATUS "Inno Setup not found -- install via: winget install JRSoftware.InnoSetup")
    endif()
endif()

