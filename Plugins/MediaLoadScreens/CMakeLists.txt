project(
    MediaLoadScreens
    VERSION 0.1.0
    LANGUAGES CXX
)

# Generate Version.h from template
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
    @ONLY
)

# Generate version.rc for Windows DLL resource (version info in file properties)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
    @ONLY
)

# Glob sources and headers
file(GLOB SOURCES "src/*.cpp")
file(GLOB HEADERS "header/*.h" "include/*.h")

# Add the library
add_library(MediaLoadScreens SHARED)

# Add sources and headers to the target (headers for IDE visibility)
target_sources(
    MediaLoadScreens
    PRIVATE
        ${SOURCES}
        ${HEADERS}
        ${CMAKE_CURRENT_BINARY_DIR}/version.rc  # Embed the resource file
        ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h  # For IDE visibility
)

# Include directories (PRIVATE if internal; change to PUBLIC if needed for child mods)
target_include_directories(
    MediaLoadScreens
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/include  # For generated Version.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src      # For sources (if any includes are relative)
        ${CMAKE_CURRENT_SOURCE_DIR}/header   # For headers in header/ (fixes C1083)
        ${CMAKE_CURRENT_SOURCE_DIR}/include  # For public headers in include/
        ${CMAKE_CURRENT_SOURCE_DIR}/../../MediaFramework/include
)

# Link dependencies
target_link_libraries(MediaLoadScreens PRIVATE MediaFramework CommonLibF4)

# Precompiled headers
target_precompile_headers(MediaLoadScreens PRIVATE header/PCH.h)

# Organize into VS filters (folders in Solution Explorer) - mirrors directory structure
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})

# Optional: Add generated Version.h to a "Generated" filter for clarity
source_group("Generated" FILES ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h)