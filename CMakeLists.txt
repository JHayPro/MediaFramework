cmake_minimum_required(VERSION 3.26)

project(MediaFramework
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(external/CommonLibF4)
set_target_properties(CommonLibF4 PROPERTIES FOLDER "CommonLibF4")
set_target_properties(AddressLibDecoder PROPERTIES FOLDER "CommonLibF4")
set_target_properties(AddressLibGen PROPERTIES FOLDER "CommonLibF4")
set_target_properties(RTTIDump PROPERTIES FOLDER "CommonLibF4")

add_subdirectory(MediaFramework)

add_subdirectory(AudioDecoder)
add_subdirectory(VideoDecoder)
set_target_properties(AudioDecoder  PROPERTIES FOLDER "Decoders")
set_target_properties(VideoDecoder  PROPERTIES FOLDER "Decoders")

add_subdirectory(Plugins/DynamicTextures)
add_subdirectory(Plugins/MediaLoadScreens)
add_subdirectory(Plugins/VideoOverlay)
set_target_properties(DynamicTextures   PROPERTIES FOLDER "Plugins")
set_target_properties(MediaLoadScreens  PROPERTIES FOLDER "Plugins")
set_target_properties(VideoOverlay      PROPERTIES FOLDER "Plugins")