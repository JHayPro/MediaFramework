@echo off
set "EXTERN_DIR=external"

git submodule update --init --recursive

:: Install all vcpkg deps (root json covers everything)
if not exist "vcpkg_installed" (
    vcpkg install --triplet x64-windows-static-md
)

vcpkg install ffmpeg:x64-windows

:: Root build with CommonLibF4 flags
cmake --preset windows-x64
cmake --build --preset build-release