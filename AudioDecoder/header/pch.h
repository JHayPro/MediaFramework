#pragma once

#include <memory>
#include <optional>
#include <atomic>
#include <xaudio2.h>
#include <x3daudio.h>

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
#include <queue>
#include <mutex>
#include <iomanip>  // for std::put_time
#include <time.h>

#pragma comment(lib, "xaudio2.lib")

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/error.h> // For av_strerror
#include <libswresample/swresample.h>
#include <libavutil\opt.h>
}
