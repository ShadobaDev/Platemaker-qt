cmake_minimum_required(VERSION 3.25)

# ---------------------------------------------------------------------------
# GoogleTest-based unit tests for the GUI's property model.
#
# Deliberately narrow: this target links Qt6::Gui and nothing else, so anything it can test is free
# of QWidget by construction. That is the point rather than an accident — a property group's
# applyTo() is a pure function from properties to properties, and keeping it testable that way is
# what keeps the model out of the widgets.
#
#   cmake -S . -B build/tests -DPLATEMAKER_GUI_BUILD_TESTS=ON
#   cmake --build build/tests --target platemaker-gui-tests
#   ctest --test-dir build/tests
#
# GTest is discovered the same way libplatemaker does it: a system install first, FetchContent
# otherwise.
# ---------------------------------------------------------------------------

find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    set(gtest_force_shared_crt ON  CACHE BOOL "" FORCE)   # MSVC: match our /MD build
    set(BUILD_GMOCK            OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST          OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        GTest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(GTest)
endif()

add_executable(platemaker-gui-tests
    main.cpp
    test_property_groups.cpp
    test_colour_adjustments.cpp
    test_presets.cpp
    test_workspace_folder.cpp
    test_workspace_lock.cpp
    test_resize.cpp
    test_selection.cpp
    test_striplayout.cpp
    test_panelbinder.cpp
    test_document.cpp
    test_text_fit.cpp
    test_lettering.cpp
    test_history.cpp
    test_chain.cpp
    test_bezier.cpp
    ${CMAKE_SOURCE_DIR}/widgets/composition/names.cpp
    ${CMAKE_SOURCE_DIR}/widgets/composition/composition.cpp
    ${CMAKE_SOURCE_DIR}/widgets/composition/chain.cpp
    # The SVG a bubble is written to, and the geometry it is written from: both Qt6::Gui only, so the
    # rule above still holds — and the import's premise is worth a test of its own.
    ${CMAKE_SOURCE_DIR}/widgets/composition/svg.cpp
    ${CMAKE_SOURCE_DIR}/widgets/composition/shapes.cpp
    ${CMAKE_SOURCE_DIR}/widgets/composition/bezier.cpp
    ${CMAKE_SOURCE_DIR}/widgets/composition/styles.cpp
    ${CMAKE_SOURCE_DIR}/widgets/composition/edgewalk.cpp
    ${CMAKE_SOURCE_DIR}/widgets/composition/outline.cpp
    ${CMAKE_SOURCE_DIR}/widgets/composition/silhouette.cpp
    ${CMAKE_SOURCE_DIR}/widgets/composition/lettering.cpp
    ${CMAKE_SOURCE_DIR}/widgets/composition/handles.cpp
    ${CMAKE_SOURCE_DIR}/widgets/composition/painter.cpp
    ${CMAKE_SOURCE_DIR}/widgets/composition/renderlist.cpp
    ${CMAKE_SOURCE_DIR}/widgets/stripeditor/properties/colouradjustment.cpp
    # The preset store is QtCore and a QObject — no widget in it — so the rule above still holds.
    ${CMAKE_SOURCE_DIR}/widgets/stripeditor/presetstore.cpp
    # The area a mask tool draws: a pure function over points.
    ${CMAKE_SOURCE_DIR}/widgets/stripeditor/objects/maskarea.cpp
    # The eight grips' resize maths: a pure function over rectangles.
    ${CMAKE_SOURCE_DIR}/widgets/stripeditor/objects/resize.cpp
    # What is selected: QtCore only.
    ${CMAKE_SOURCE_DIR}/widgets/stripeditor/objects/selection.cpp
    # What OBJECT STATE is bound to: QtCore, and compositions.
    ${CMAKE_SOURCE_DIR}/widgets/stripeditor/objects/panelbinder.cpp
    # The objects as the project holds them: the library's types, none of its code.
    ${CMAKE_SOURCE_DIR}/widgets/stripeditor/objects/document.cpp
    # Where an overlay sits on the strip: the library's types, none of its code.
    ${CMAKE_SOURCE_DIR}/widgets/stripeditor/objects/striplayout.cpp
    # The one-workspace-per-folder rule: QtCore and the file system, nothing else.
    ${CMAKE_SOURCE_DIR}/widgets/workspacefolder/workspacefolder.cpp
    ${CMAKE_SOURCE_DIR}/widgets/workspacefolder/workspacelock.cpp
    ${CMAKE_SOURCE_DIR}/widgets/workspacefolder/fontfiles.cpp
    # The undo history: QUndoStack is QtGui's, nothing else.
    ${CMAKE_SOURCE_DIR}/widgets/history/history.cpp
)

target_include_directories(platemaker-gui-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/widgets/composition
    # One root, as in the app: includes name their folder, e.g. "properties/propertygroup.hpp" for
    # carriesGroup(), which is header-only and QWidget-free so the rule can be tested.
    ${CMAKE_SOURCE_DIR}/widgets/stripeditor
    ${CMAKE_SOURCE_DIR}/widgets/workspacefolder
    ${CMAKE_SOURCE_DIR}/widgets/history
    # The colour adjustments read libplatemaker's ColourCorrection, which is header-only. Its include path
    # and not its target: linking the target would make every test run need the library's DLL on PATH,
    # including the one gtest_discover_tests starts at build time.
    $<TARGET_PROPERTY:Platemaker::platemaker,INTERFACE_INCLUDE_DIRECTORIES>
)

# The golden files a test compares against, in the source tree so a regenerated one shows in git diff.
target_compile_definitions(platemaker-gui-tests PRIVATE
    PLATEMAKER_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

target_link_libraries(platemaker-gui-tests PRIVATE
    Qt6::Gui
    GTest::gtest
)
if(WIN32)
    target_link_libraries(platemaker-gui-tests PRIVATE dwrite)
endif()

include(GoogleTest)
gtest_discover_tests(platemaker-gui-tests)
