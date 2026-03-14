cmake_minimum_required(VERSION 3.26)

project(MediaFramework
    VERSION 0.1.0
    LANGUAGES CXX
)

# Enforce modern C++
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Common warnings
if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- Dependencies ----

# CommonLibF4
add_subdirectory(external/CommonLibF4)

# # Core framework
add_subdirectory(MediaFramework)
if (MSVC)
    target_compile_options(MediaFramework PRIVATE "$<$<CONFIG:Release>:/MD>")
endif()

# # Decoders
add_subdirectory(AudioDecoder)
add_subdirectory(VideoDecoder)

# # Plugins
add_subdirectory(Plugins/DynamicTextures)
add_subdirectory(Plugins/MediaLoadScreens)
add_subdirectory(Plugins/VideoOverlay)