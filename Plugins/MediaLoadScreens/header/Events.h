// Events.h (MediaLoadscreen)
#pragma once
#include "PCH.h"

static DecoderHandle g_decoderHandle = -1;
static std::atomic<bool> g_menuOpen{ false };
static std::thread g_checkThread;

extern std::filesystem::path dllParentPath;
extern std::filesystem::path parentIniPath;

extern std::unordered_map<int, std::vector<std::filesystem::path>> mediaPathQueue;

void MessageHandler(F4SE::MessagingInterface::Message* const msg);

