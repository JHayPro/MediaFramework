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

add_executable(${PROJECT_NAME})

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "header/*.h")

target_sources(
    ${PROJECT_NAME}
    PRIVATE
        ${SOURCES}
        ${HEADERS}
        ${CMAKE_CURRENT_BINARY_DIR}/version.rc
        ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
)

target_sources(${PROJECT_NAME} PRIVATE ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/header
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${FFMPEG_LIBRARIES}
)

target_precompile_headers(${PROJECT_NAME} PRIVATE header/PCH.h)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})

source_group("header" FILES ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<LOWER_CASE:$<CONFIG>>/MediaFramework_Decoders"
)

set_from_environment(Fallout4Path)
if (DEFINED Fallout4Path)
    set(DECODER_PATH $ENV{Fallout4Path}/Data/F4SE/Plugins/MediaFramework_Decoders)

    add_custom_command(
        TARGET ${PROJECT_NAME} 
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
            ${DECODER_PATH}
        COMMENT "Copying ${PROJECT_NAME}.exe + .pdb to MediaFramework_Decoders folder"
    )
else ()
	message(
		WARNING
		"Fallout4Path environment variable not set — skipping post-build copy to game directory."
	)
endif()