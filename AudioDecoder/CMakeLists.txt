project(
    AudioDecoder
    VERSION 0.1.0
    LANGUAGES CXX
)

cmake_policy(SET CMP0144 NEW)

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

find_package(FFMPEG REQUIRED)

add_executable(AudioDecoder)

file(GLOB SOURCES "src/*.cpp")
file(GLOB HEADERS "header/*.h")

target_sources(
    AudioDecoder
    PRIVATE
        ${SOURCES}
        ${HEADERS}
        ${CMAKE_CURRENT_BINARY_DIR}/version.rc
        ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
)

target_sources(AudioDecoder PRIVATE ${SOURCES} ${HEADERS})

target_include_directories(AudioDecoder PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/header
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(AudioDecoder PRIVATE
    ${FFMPEG_LIBRARIES}
)

if(MSVC)
    target_compile_options(AudioDecoder PRIVATE "$<$<CONFIG:Release>:/MD>")
endif()

target_precompile_headers(AudioDecoder PRIVATE header/PCH.h)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})

source_group("header" FILES ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h)