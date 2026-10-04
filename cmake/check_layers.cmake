# check_layers.cmake — the strip editor's dependency rules, checked on every build.
#
# Run by the `check_layers` target, which Platemaker depends on, and by the `strip_editor_layers` test:
#   cmake -D ROOT=<widgets/stripeditor> -P cmake/check_layers.cmake
#
# Each folder under widgets/stripeditor is a region of the screen (or the objects and property editors
# under them), and includes name their folder ("objects/object.hpp"). So a dependency is one #include
# line, and these rules are text matches on it. A violation prints `file:line: error: …` and stops the
# build. ponytail: a text check on #include lines, not a dependency graph; enough while includes are
# folder-qualified, which the single include root enforces (an unqualified one does not compile).

cmake_minimum_required(VERSION 3.25)   # a -P script sets no policies of its own; IN_LIST needs them

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "check_layers.cmake requires -D ROOT=<widgets/stripeditor>")
endif()

set(_regions canvas objects objectstack objectstate properties tooloptions toolrail)

# What each region may include besides itself. The root (editor.*) is where the regions meet, so it may
# include everything and nothing may include it; the root's other files (presetstore, scrolledpage) are
# shared by all. The table is the diagram in the strip-editor plan, read as layers:
#   toolrail, tooloptions, objectstate -> properties;   objectstack, canvas -> objects -> properties.
set(_may_canvas      objects properties)
set(_may_objects     properties)
set(_may_objectstack objects properties)
set(_may_objectstate properties)
set(_may_properties  "")
set(_may_tooloptions properties)
set(_may_toolrail    properties)

# The exceptions: one header each, never a whole region, so a second include from the same region still
# fails. "region>header", then why.
set(_exceptions
    # OBJECT STATE describes the strip and its pages; their geometry is the objects'.
    "objectstate>objects/striplayout.hpp"
    # The tool table and the cursor rule are what the canvas routes a press by.
    "canvas>toolrail/toolregistry.hpp"
    "canvas>toolrail/cursors.hpp"
    # PointerTarget: what an object says is under the pointer, which the cursor rule takes.
    "objects>toolrail/cursors.hpp"
    # Every tool in the table must have a TOOL OPTIONS page; the stack asserts it.
    "tooloptions>toolrail/toolregistry.hpp"
    # loadArtwork(): the Artwork tool's preview must read a file exactly as the placed object does.
    "tooloptions>objects/artworkobject.hpp"
    # The object menu's Fill and Outline spend the rail's colour pair (read, never written).
    "objectstack>toolrail/colourpair.hpp"
)

set(_violations 0)
set(_used "")
file(GLOB_RECURSE _files RELATIVE "${ROOT}" "${ROOT}/*.cpp" "${ROOT}/*.hpp" "${ROOT}/*.h")
foreach(_file IN LISTS _files)
    if(NOT _file MATCHES "^([^/]+)/")
        continue()   # the root itself
    endif()
    set(_region "${CMAKE_MATCH_1}")

    file(READ "${ROOT}/${_file}" _text)
    # One list element per line. ';' would split a line and '[' ']' stop CMake splitting at all.
    string(REGEX REPLACE "[];[]" "_" _text "${_text}")
    string(REPLACE "\n" ";" _lines "${_text}")
    set(_n 0)
    foreach(_line IN LISTS _lines)
        math(EXPR _n "${_n} + 1")
        if(NOT _line MATCHES "^[ \t]*#[ \t]*include[ \t]*\"([^\"]+)\"")
            continue()
        endif()
        set(_inc "${CMAKE_MATCH_1}")

        set(_bad "")
        if(_inc MATCHES "^([^/]+)/")
            set(_target "${CMAKE_MATCH_1}")
            if(_target IN_LIST _regions AND NOT _target STREQUAL _region
               AND NOT _target IN_LIST _may_${_region})
                if("${_region}>${_inc}" IN_LIST _exceptions)
                    list(APPEND _used "${_region}>${_inc}")
                else()
                    set(_bad "${_region}/ may not include ${_target}/")
                endif()
            endif()
        elseif(_inc STREQUAL "editor.hpp" OR _inc STREQUAL "ui_editor.h")
            set(_bad "${_region}/ may not include the editor; the regions meet there, not in each other")
        endif()

        if(_bad)
            message(NOTICE "${ROOT}/${_file}:${_n}: error: ${_bad} (#include \"${_inc}\")")
            math(EXPR _violations "${_violations} + 1")
        endif()
    endforeach()
endforeach()

# An exception nothing needs any more is one the next reader would believe.
foreach(_e IN LISTS _exceptions)
    if(NOT _e IN_LIST _used)
        message(NOTICE "check_layers.cmake: warning: exception \"${_e}\" is no longer used; remove it")
    endif()
endforeach()

if(_violations GREATER 0)
    message(FATAL_ERROR "${_violations} strip-editor layer violation(s); the rules are in cmake/check_layers.cmake")
endif()
