# MediaFramework
API for flexible multimedia functionality. Primary usage is performing engine level texture swaps to mimic video playback. Includes audio decoder for audio playback. Pre-alpha build.

## Building the Project


```powershell
git clone https://github.com/JHayPro/MediaFramework
cd MediaFramework
./scripts/setup.bat  
```

Alternative:
```powershell
git clone https://github.com/JHayPro/MediaFramework
cd MediaFramework
git submodule update --init --recursive

if (-not (Test-Path "vcpkg_installed")) {
    vcpkg install --triplet x64-windows-static-md
}

cmake --preset windows-x64
cmake --build --preset build-release
```