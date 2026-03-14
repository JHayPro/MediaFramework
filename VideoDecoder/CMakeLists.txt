cmake_minimum_required(VERSION 3.26)

cmake_policy(SET CMP0144 NEW)

find_package(FFMPEG REQUIRED)

add_executable(VideoDecoder)

file(GLOB SOURCES "src/*.cpp")
file(GLOB HEADERS "header/*.h" "include/*.h")

target_sources(VideoDecoder PRIVATE ${SOURCES} ${HEADERS})

target_include_directories(VideoDecoder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/header
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(VideoDecoder PRIVATE
    ${FFMPEG_LIBRARIES}
)

if(MSVC)
    target_compile_options(VideoDecoder PRIVATE "$<$<CONFIG:Release>:/MD>")
endif()

target_precompile_headers(VideoDecoder PRIVATE header/PCH.h)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})