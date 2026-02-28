@echo off
git submodule update --init --recursive
powershell -File scripts/download_ffmpeg.ps1
vcpkg install
cmake --preset release
cmake --build --preset release