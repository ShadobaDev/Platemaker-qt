#Requires -Version 5.1
#
# make_portable.ps1 - build the portable Windows ZIP from a Qt Creator build dir.
#
# Wipes install/ first so stale DLLs from a previous build never leak into the ZIP (same reason as
# make_installer.ps1 - `cmake --install` adds/updates but never removes). The `portable` target then
# flattens install/ into a single Platemaker-<ver>/ folder (exe at the root) and zips it.
#
# Usage:
#   ./scripts/make_portable.ps1                 # default: MSVC 2022 Release build dir
#   ./scripts/make_portable.ps1 --build <dir>   # any other Qt Creator build dir
#
# The target build dir must already be configured + built.

$ErrorActionPreference = 'Stop'

# Run from the repo root (where CMakeLists.txt lives), whatever the caller's cwd.
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
        Write-Error ('Unknown argument: ' + $args[$i] + '  (usage: make_portable.ps1 [--build <dir>])')
        exit 1
    }
}

if (-not (Test-Path -LiteralPath $buildDir)) {
    Write-Error ('Build dir not found: ' + $buildDir)
    Write-Host  'Configure + build it first (Qt Creator, or: cmake -B <dir> ...  then  cmake --build <dir>).'
    exit 1
}

# Clean install/ so no stale artifacts leak into the ZIP.
if (Test-Path -LiteralPath 'install') {
    Write-Host 'Cleaning install\ ...'
    Remove-Item -LiteralPath 'install' -Recurse -Force
}

# Build the portable target.
Write-Host ('Building portable ZIP from: ' + $buildDir)
cmake --build $buildDir --target portable
if ($LASTEXITCODE -ne 0) { Write-Error ('portable build failed (exit ' + $LASTEXITCODE + ').'); exit $LASTEXITCODE }

Write-Host ''
Write-Host 'Portable ZIP in installer-output\:'
if (Test-Path installer-output) {
    Get-ChildItem installer-output -Filter *-portable.zip | ForEach-Object {
        '  {0}  ({1:N1} MB)' -f $_.Name, ($_.Length / 1MB)
    }
}
else {
    Write-Host '  (none - did the portable target run?)'
}
