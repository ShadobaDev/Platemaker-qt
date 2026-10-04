
# Build the installer: MSVC 2022 Release (the shipping toolchain).
.\scripts\make_installer.ps1

# ...from any other build dir (Debug, MinGW, etc.) via --build
.\scripts\make_installer.ps1 --build build\Desktop_Qt_6_11_2_MSVC2022_64bit_Debug
.\scripts\make_installer.ps1 --build build\Desktop_Qt_6_11_1_MinGW_64_bit-Release
.\scripts\make_installer.ps1 --build .\build\Desktop_Qt_6_11_1_MinGW_64_bit-Debug

# Build the installer (raw cmake — does NOT clean install/, may bundle stale DLLs from a prior build)
cmake --build .\build\Desktop_Qt_6_11_2_MSVC2022_64bit_Release\ --target installer
cmake --build .\build\Desktop_Qt_6_11_1_MinGW_64_bit-Debug\ --target installer

# Build the portable ZIP (no Inno Setup needed): MSVC 2022 Release by default, cleans install/ first.
# Output: installer-output\Platemaker-<ver>-portable.zip + its -portable-SHA256SUMS.txt
.\scripts\make_portable.ps1
.\scripts\make_portable.ps1 --build build\Desktop_Qt_6_11_2_MSVC2022_64bit_Debug

# Build the portable ZIP (raw cmake — does NOT clean install/, may bundle stale DLLs from a prior build)
cmake --build .\build\Desktop_Qt_6_11_2_MSVC2022_64bit_Release\ --target portable

# Install / deploy to a prefix (no installer)
cmake --install .\build\Desktop_Qt_6_11_2_MSVC2022_64bit_Release --prefix D:\Users\Shadoba\Dev\Platemaker-qt\Platemaker\install
