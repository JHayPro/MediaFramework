// Events.h (MediaLoadscreen)
#pragma once
#include "PCH.h"

static DecoderHandle g_decoderHandle = -1;
static std::atomic<bool> g_menuOpen{ false };
static std::thread g_checkThread;

extern std::filesystem::path parentMediaPath;
extern std::filesystem::path parentIniPath;

struct LoadScreenMediaArgs {
	bool persistent = false;
	bool persistentCrossInstance = false;

	LoadScreenMediaArgs() = default;
	LoadScreenMediaArgs(bool persistent, bool persistentCrossInstance) : persistent(persistent), persistentCrossInstance(persistentCrossInstance) {};
};

struct LoadScreenMedia {

	MediaDescriptor mediaDescriptor;
	LoadScreenMediaArgs args;

	LoadScreenMedia() = default;

	LoadScreenMedia(MediaDescriptor mediaDescriptor, LoadScreenMediaArgs args) : mediaDescriptor(mediaDescriptor), args(args) {};
};

extern std::array<std::vector<LoadScreenMedia>, 5> mediaQueue;
extern std::mutex g_mediaQueueMutex;

extern std::random_device rd;

void MessageHandler(F4SE::MessagingInterface::Message* const msg);

