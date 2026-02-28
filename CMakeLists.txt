cmake_minimum_required(VERSION 3.22)
project(Fallout4-MediaFramework CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg integration
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

find_package(directxtex CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)

add_subdirectory(external/CommonLibF4)
add_subdirectory(MediaFramework)  # Core DLL