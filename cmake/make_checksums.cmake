# make_checksums.cmake — write a coreutils-style SHA-256 sum for the built installer.
#
# Run at build time by the `installer` target (see CMakeLists.txt):
#   cmake -D INSTALLER_FILE=<setup.exe> -D OUTPUT=<SHA256SUMS.txt> -P cmake/make_checksums.cmake
#
# The "<hash>  <name>" format (two spaces, filename only — not a path) is exactly what
# `sha256sum -c SHA256SUMS.txt` expects, so users can verify with coreutils; Windows users compare
# against `Get-FileHash`. Upload the resulting file as a GitHub Release asset next to the installer.

if(NOT DEFINED INSTALLER_FILE OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "make_checksums.cmake requires -D INSTALLER_FILE=... -D OUTPUT=...")
endif()

if(NOT EXISTS "${INSTALLER_FILE}")
    message(FATAL_ERROR "Installer not found (did Inno Setup run?): ${INSTALLER_FILE}")
endif()

file(SHA256 "${INSTALLER_FILE}" _hash)
get_filename_component(_name "${INSTALLER_FILE}" NAME)
file(WRITE "${OUTPUT}" "${_hash}  ${_name}\n")
message(STATUS "SHA256SUMS.txt written: ${_hash}  ${_name}")
