cmake_minimum_required(VERSION 3.26)

cmake_policy(SET CMP0144 NEW)

find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat swresample
    HINTS "${VCPKG_ROOT}/installed/x64-windows")

add_library(audioDecoder SHARED)

file(GLOB SOURCES "src/*.cpp")
file(GLOB HEADERS "header/*.h" "include/*.h")

target_sources(audioDecoder PRIVATE ${SOURCES} ${HEADERS})

target_include_directories(audioDecoder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/header
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS} 
)

target_link_libraries(audioDecoder PRIVATE
    MediaFramework
    FFmpeg::avcodec
    FFmpeg::avformat
)

if(MSVC)
    target_compile_options(audioDecoder PRIVATE "$<$<CONFIG:Release>:/MD>")
endif()

target_precompile_headers(audioDecoder PRIVATE header/PCH.h)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})