#include "MediaLoadScreensAPI.h"
#include "Events.h"

void MediaLoadScreensPapyrus::test(std::monostate){
	return;
}


bool MediaLoadScreensPapyrus::QueueMediaFileOrFolder(std::monostate, RE::BSScript::structure_wrapper<"MediaLoadScreens", "MediaLoadScreensOptions"> options, std::string path, int version, int priority)
{
	Sleep(8000);
	auto fullPath = parentMediaPath / path;
	if (path.empty()) {
		logger::warn("QueueMediaFile: empty path");
		return false;
	}

	MediaDescriptor descs[128]{};
	uint32_t count = 0;

	if (MF_DiscoverMedia(fullPath.string().c_str(), descs, 128, &count) != MF_Result::Ok || count == 0) {
		logger::warn("MF_DiscoverMedia was unsuccessful for {}", fullPath.string());
	}

	auto getBool = [&](std::string_view name, bool def) -> bool {
		return options.find<bool>(name).value_or(def);
	};

	bool persistentPerInstance = getBool("persistentPerInstance", false);
	bool persistentCrossInstance = getBool("persistentCrossInstance", false);

	std::lock_guard<std::mutex> lock(g_mediaQueueMutex);
	for (uint32_t i = 0; i < count; ++i) 
		mediaQueue[priority].emplace_back(LoadScreenMedia{descs[i], LoadScreenMediaArgs{persistentPerInstance , persistentCrossInstance}});

	return true;
}

extern "C" bool QueueMediaFolderALR(const char* folderPath, int priority)
{
	if (!folderPath || std::strlen(folderPath) == 0) {
		return false;
	}

	return true;
}
