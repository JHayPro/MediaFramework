// PCH.h (MediaFramework)
#pragma once
#define WIN32_LEAN_AND_MEAN

#pragma warning(push)
#include "F4SE/F4SE.h"
#include "RE/Fallout.h"

#include <fstream>
#include <MinHook.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXTex.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <wrl.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

#pragma warning(pop)

#define DLLEXPORT __declspec(dllexport)

namespace logger = F4SE::log;

using namespace std::literals;

using Microsoft::WRL::ComPtr;
 
 
 
