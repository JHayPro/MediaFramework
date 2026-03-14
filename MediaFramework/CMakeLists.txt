project(
    MediaFramework
    VERSION 0.1.0
    LANGUAGES CXX
)

find_package(directxtex CONFIG REQUIRED HINTS "${VCPKG_ROOT}/installed/x64-windows-static-md")
find_package(minhook CONFIG REQUIRED HINTS "${VCPKG_ROOT}/installed/x64-windows-static-md")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
    @ONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
    @ONLY
)

file(GLOB SOURCES "src/*.cpp")
file(GLOB HEADERS "header/*.h" "include/*.h")

add_library(MediaFramework SHARED)

target_sources(
    MediaFramework
    PRIVATE
        ${SOURCES}
        ${HEADERS}
        ${CMAKE_CURRENT_BINARY_DIR}/version.rc
        ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
)

target_include_directories(
    MediaFramework
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/header
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(MediaFramework PRIVATE CommonLibF4 Microsoft::DirectXTex minhook::minhook)

target_precompile_headers(MediaFramework PRIVATE header/PCH.h)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})

source_group("header" FILES ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h)