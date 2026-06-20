cmake_minimum_required(VERSION 3.19)
project(Platemaker LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Concurrent)

qt_standard_project_setup()

qt_add_executable(Platemaker
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui

    project.h project.cpp project.ui
    imagetile.h imagetile.cpp imagetile.ui
    manageoutputprofilesdialog.ui managecanvasprofilesdialog.ui outputprofiledialog.ui canvasprofiledialog.ui
    canvasprofiledialog.cpp canvasprofiledialog.h managecanvasprofilesdialog.cpp managecanvasprofilesdialog.h manageoutputprofilesdialog.cpp manageoutputprofilesdialog.h outputprofiledialog.cpp outputprofiledialog.h
    templatesdialog.cpp templatesdialog.h templatesdialog.ui


)

if(1)
    include(FetchContent)

    # ─── platemaker ────────────────────────────────────────────────────────────────
    # Discovery order:
    #   1. PLATEMAKER_DIR cache variable  — explicit local dev package (set once, cached)
    #   2. find_package                   — system install or CMAKE_PREFIX_PATH
    #   3. FetchContent                   — downloads pre-built package from GitHub Releases
    # ───────────────────────────────────────────────────────────────────────────────
    set(PLATEMAKER_VERSION "0.1.1")
    set(PLATEMAKER_DIR "" CACHE PATH "Path to a local platemaker dev package")
    message("Local libplatemaker path: ${PLATEMAKER_DIR}")

    if(PLATEMAKER_DIR)
        list(PREPEND CMAKE_PREFIX_PATH "${PLATEMAKER_DIR}")
        message("Set libplatemaker path: ${PLATEMAKER_DIR}")
    endif()

    find_package(platemaker CONFIG QUIET)

    if(NOT platemaker_FOUND)
        include(FetchContent)

        if(WIN32)
            set(_pm_archive "platemaker-dev-${PLATEMAKER_VERSION}-Windows-AMD64.zip")
        else()
            set(_pm_archive "platemaker-dev-${PLATEMAKER_VERSION}-Linux-x86_64.tar.gz")
        endif()

        FetchContent_Declare(
            platemaker_pkg
            URL "https://github.com/ShadobaDev/PlateMaker/releases/download/v${PLATEMAKER_VERSION}/${_pm_archive}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(platemaker_pkg)
        FetchContent_GetProperties(platemaker_pkg SOURCE_DIR _pm_src)
        file(GLOB _pm_dir LIST_DIRECTORIES true "${_pm_src}/platemaker-dev-*")
        list(GET _pm_dir 0 _pm_dir)
        list(PREPEND CMAKE_PREFIX_PATH "${_pm_dir}")
        find_package(platemaker CONFIG REQUIRED)
    endif()

    target_link_libraries(${PROJECT_NAME} PRIVATE Platemaker::platemaker)

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
install(SCRIPT ${deploy_script})
