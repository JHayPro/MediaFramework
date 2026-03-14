// PCH.h (VideoOverlay)
#pragma once
#define WIN32_LEAN_AND_MEAN

#pragma warning(push)
#include "F4SE/F4SE.h"
#include "RE/Fallout.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#pragma warning(pop)

#include <fstream>
#include <wrl.h>
#include "MediaFrameworkAPI.h"

#define DLLEXPORT __declspec(dllexport)

namespace logger = F4SE::log;

using namespace std::literals;

using Microsoft::WRL::ComPtr;