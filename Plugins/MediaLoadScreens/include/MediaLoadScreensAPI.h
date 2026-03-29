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
	bool MLS_API QueueMediaFolderALR(const char* folderPath, int priority);
}

namespace MediaLoadScreensPapyrus
{
	bool QueueMediaFileOrFolder(std::monostate, RE::BSScript::structure_wrapper<"MediaLoadScreens", "MediaLoadScreensOptions"> options, std::string path, int version, int priority);
	void test(std::monostate);
}