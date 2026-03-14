project(
    MediaLoadScreens
    VERSION 0.1.0
    LANGUAGES CXX
)

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

add_library(MediaLoadScreens SHARED)

target_sources(
    MediaLoadScreens
    PRIVATE
        ${SOURCES}
        ${HEADERS}
        ${CMAKE_CURRENT_BINARY_DIR}/version.rc
        ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
)

target_include_directories(
    MediaLoadScreens
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/header
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../MediaFramework/include
)

target_link_libraries(MediaLoadScreens PRIVATE MediaFramework CommonLibF4)

target_precompile_headers(MediaLoadScreens PRIVATE header/PCH.h)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})

source_group("header" FILES ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h)