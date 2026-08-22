#Requires -Version 5.1
#
# make_installer.ps1 - build the Windows (Inno Setup) installer from a Qt Creator build dir.
#
# Wipes install/ first so stale DLLs from a previous build (e.g. a MinGW build's libplatemaker.dll plus
# libgcc/libstdc++/libwinpthread) never leak into the installer. `cmake --install` only adds/updates
# files, it never removes ones no longer produced, so without this the installer can bundle two
# toolchains' runtimes at once (and an AV scan of it would be misleading).
#
# Usage:
#   ./scripts/make_installer.ps1                 # default: MSVC 2022 Release build dir
#   ./scripts/make_installer.ps1 --build <dir>   # any other Qt Creator build dir
#
# Examples:
#   ./scripts/make_installer.ps1
#   ./scripts/make_installer.ps1 --build build\Desktop_Qt_6_11_2_MSVC2022_64bit_Debug
#   ./scripts/make_installer.ps1 --build build\Desktop_Qt_6_11_1_MinGW_64_bit-Release
#
# The target build dir must already be configured + built. This script only runs the `installer` target
# (install -> windeployqt -> Inno Setup) after cleaning install/.

$ErrorActionPreference = 'Stop'

# Run from the repo root (where CMakeLists.txt / Platemaker.iss live), whatever the caller's cwd.
Set-Location (Join-Path $PSScriptRoot '..')

# Default build dir - the MSVC 2022 Release kit (the shipping toolchain). Override with --build <dir>.
$buildDir = 'build\Desktop_Qt_6_11_2_MSVC2022_64bit_Release'

# Parse --build <dir> (also accepts -build / -Build).
for ($i = 0; $i -lt $args.Count; $i++) {
    if ($args[$i] -match '^(--build|-build|-Build)$') {
        if ($i + 1 -ge $args.Count) { Write-Error 'The --build flag requires a directory argument.'; exit 1 }
        $buildDir = $args[$i + 1]
        $i++
    }
    else {
        Write-Error ('Unknown argument: ' + $args[$i] + '  (usage: make_installer.ps1 [--build <dir>])')
        exit 1
    }
}

if (-not (Test-Path -LiteralPath $buildDir)) {
    Write-Error ('Build dir not found: ' + $buildDir)
    Write-Host  'Configure + build it first (Qt Creator, or: cmake -B <dir> ...  then  cmake --build <dir>).'
    exit 1
}

# Clean install/ so no stale artifacts leak into the installer.
if (Test-Path -LiteralPath 'install') {
    Write-Host 'Cleaning install\ ...'
    Remove-Item -LiteralPath 'install' -Recurse -Force
}

# Build the installer target.
Write-Host ('Building installer from: ' + $buildDir)
cmake --build $buildDir --target installer
if ($LASTEXITCODE -ne 0) { Write-Error ('installer build failed (exit ' + $LASTEXITCODE + ').'); exit $LASTEXITCODE }

Write-Host ''
Write-Host 'Installer in installer-output\:'
if (Test-Path installer-output) {
    Get-ChildItem installer-output -Filter *.exe | ForEach-Object {
        '  {0}  ({1:N1} MB)' -f $_.Name, ($_.Length / 1MB)
    }
}
else {
    Write-Host '  (none - did the installer target run?)'
}
