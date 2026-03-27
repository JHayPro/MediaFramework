#include "MediaLoadScreensAPI.h"

bool MediaLoadScreensPapyrus::QueueMediaFile(std::monostate, std::string filePath, int priority, bool persistent)
{
	return false;
}

bool MediaLoadScreensPapyrus::QueueMediaFolder(std::monostate, std::string folderPath, int priority, bool persistent)
{
	return false;
}

extern "C" bool QueueMediaFolderALR(const char* filePath, int priority)
{
	return false;
}
