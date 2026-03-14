// PCH.h (VideoOverlay)
#pragma once
#define WIN32_LEAN_AND_MEAN

#pragma warning(push)
#include "F4SE/F4SE.h"
#include "RE/Fallout.h"

#include <fstream>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <wrl.h>

#include "MediaFrameworkAPI.h"

#pragma warning(pop)

#define DLLEXPORT __declspec(dllexport)

namespace logger = F4SE::log;

using namespace std::literals;

using Microsoft::WRL::ComPtr;