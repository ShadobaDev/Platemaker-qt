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
    test_property_groups.cpp
    test_colour_adjustments.cpp
    ${CMAKE_SOURCE_DIR}/widgets/textartifact/textartifact.cpp
    ${CMAKE_SOURCE_DIR}/widgets/stripeditor/colouradjustment.cpp
)

target_include_directories(platemaker-gui-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/widgets/textartifact
    ${CMAKE_SOURCE_DIR}/widgets/stripeditor
    # The colour adjustments read libplatemaker's ColourCorrection, which is header-only. Its include path
    # and not its target: linking the target would make every test run need the library's DLL on PATH,
    # including the one gtest_discover_tests starts at build time.
    $<TARGET_PROPERTY:Platemaker::platemaker,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(platemaker-gui-tests PRIVATE
    Qt6::Gui
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(platemaker-gui-tests)
