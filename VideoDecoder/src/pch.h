#pragma once
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <windows.h>
#include <fstream>
#include <filesystem> // For file existence check
#include <cassert>
#include <shlobj.h> // For SHGetKnownFolderPath
#include <map>
#include <memory>
#include <optional>
#include <atomic>
#include <format>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/error.h> // For av_strerror
}
