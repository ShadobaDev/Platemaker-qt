# make_portable.cmake — flatten the staged install/ tree into a portable Platemaker-<ver>/ folder.
#
# Run at build time by the `portable` target (see CMakeLists.txt):
#   cmake -D SRC=<staged install prefix> -D VERSION=<x.y.z> -D DEST=<output dir> -P cmake/make_portable.cmake
#
# The staged tree nests the exe under bin/ (with qt.conf's Prefix = ..). A portable unzip should reach the
# exe with no digging, so this lifts bin/* to the folder root (exe + DLLs + credits/ together), keeps
# plugins/ and translations/ as sibling subfolders, and rewrites qt.conf to Prefix = . accordingly. The
# `portable` target then zips the resulting Platemaker-<ver>/ folder with `cmake -E tar`.

if(NOT DEFINED SRC OR NOT DEFINED VERSION OR NOT DEFINED DEST)
    message(FATAL_ERROR "make_portable.cmake requires -D SRC=... -D VERSION=... -D DEST=...")
endif()

if(NOT EXISTS "${SRC}/bin/Platemaker.exe")
    message(FATAL_ERROR "Staged build not found (run cmake --install first): ${SRC}/bin/Platemaker.exe")
endif()

set(_stage "${DEST}/Platemaker-${VERSION}")
file(REMOVE_RECURSE "${_stage}")          # wipe any previous flatten so stale files never leak into the zip
file(MAKE_DIRECTORY "${_stage}")

# bin/* → folder root (exe, ~50 DLLs, credits/, qt.conf). Trailing slash copies the CONTENTS of bin/.
file(COPY "${SRC}/bin/" DESTINATION "${_stage}")
# plugins/ and translations/ stay as subfolders — Qt resolves them under the prefix below.
file(COPY "${SRC}/plugins" "${SRC}/translations" DESTINATION "${_stage}")
# Portable layout: the exe sits WITH its DLLs at the root, so Qt's prefix is the folder itself, not '..'.
file(WRITE "${_stage}/qt.conf" "[Paths]\nPrefix = .\n")

message(STATUS "Portable tree staged: ${_stage}")
