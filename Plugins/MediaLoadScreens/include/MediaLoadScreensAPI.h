// MediaLoadScreensAPI.h (MediaLoadScreens)
#pragma once
#include "PCH.h"

#ifdef MEDIALOADSCREENS_EXPORTS
#	define MLS_API __declspec(dllexport)
#else
#	define MLS_API __declspec(dllimport)
#endif

extern "C"
{
	// Hook for backwards compatibility with Legacy ALR
	bool MLS_API QueueMediaFolderALR(const char* filePath, int priority);
}

namespace MediaLoadScreensPapyrus
{
	bool QueueMediaFile(std::monostate, std::string filePath, int priority, bool persistent);
	bool QueueMediaFolder(std::monostate, std::string folderPath, int priority, bool persistent);
}