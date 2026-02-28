set(FFMPEG_ROOT ${PROJECT_SOURCE_DIR}/../external/FFmpeg)
find_package(FFmpeg REQUIRED PATHS ${FFMPEG_ROOT})
add_executable(VideoDecoder src/main.cpp)
target_link_libraries(VideoDecoder PRIVATE avcodec avformat avutil swresample)  # FFmpeg libs