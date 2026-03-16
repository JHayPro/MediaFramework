// Events.h (MediaLoadscreen)
#pragma once
#include "PCH.h"

// Global handles
static DecoderHandle g_decoderHandle = -1;
static MediaInstanceHandle g_instanceHandle = -1;

static std::atomic<bool> g_menuOpen{ false };
static std::thread g_checkThread;
static std::filesystem::path dllParentPath;
static std::filesystem::path parentIniPath;

void MessageHandler(F4SE::MessagingInterface::Message* const msg);

