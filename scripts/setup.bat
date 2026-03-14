@echo off

git submodule update --init --recursive

if not exist "vcpkg_installed" (
    vcpkg install --triplet x64-windows-static-md-ffmpeg-dynamic --overlay-triplets=triplets
)

:: Root build with CommonLibF4 flags
cmake --preset windows-x64
cmake --build --preset build-release