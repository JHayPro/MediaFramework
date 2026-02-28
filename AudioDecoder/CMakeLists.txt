set(FFMPEG_ROOT ${PROJECT_SOURCE_DIR}/../external/FFmpeg)
find_package(FFmpeg REQUIRED PATHS ${FFMPEG_ROOT})
add_executable(AudioDecoder src/main.cpp)
target_link_libraries(AudioDecoder PRIVATE avcodec avformat avutil swresample)  # FFmpeg libs