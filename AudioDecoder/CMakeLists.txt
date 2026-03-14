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

set_target_properties(AudioDecoder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<LOWER_CASE:$<CONFIG>>/MediaFramework_Decoders"
)

set_from_environment(Fallout4Path)
if (DEFINED Fallout4Path)
    set(DECODER_PATH $ENV{Fallout4Path}/Data/F4SE/Plugins/MediaFramework_Decoders)

    add_custom_command(
        TARGET AudioDecoder 
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            $<TARGET_FILE_DIR:AudioDecoder>
            ${DECODER_PATH}
    )
else ()
	message(
		WARNING
		"Variable Fallout4Path is not defined. Skipping post-build copy command."
	)
endif()