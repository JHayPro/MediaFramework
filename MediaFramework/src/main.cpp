// Main.cpp (MediaFramework)
#include "D3DUtils.h"
#include "DecoderManager.h"
#include "Hooks.h"
#include "SharedMemoryUtils.h"
#include "MediaFrameworkAPI.h"
#include "MediaFrameworkMenu.h"
#include "LoadingMenuVideoSupport.h"
#include "Version.h"

HANDLE g_jobHandle = NULL;

/** @brief Unload and clean up resources on DLL exit. */
void UnloadResources()
{
	logger::info("MediaFramework loading");
	std::scoped_lock lock(g_videoMutex);
	// Collect and destroy all video instances first
	std::vector<uint64_t> instanceIds;
	for (const auto& pair : g_mediaInstances) {
		instanceIds.push_back(pair.first);
	}
	for (auto id : instanceIds) {
		MF_DestroyMediaInstance(id);
	}
	// Collect and destroy all decoders
	std::vector<uint64_t> decoderIds;
	for (const auto& pair : g_decoders) {
		decoderIds.push_back(pair.first);
	}
	for (auto id : decoderIds) {
		MF_DestroyDecoder(id);
	}
	g_resources.Reset();
	if (MH_DisableHook(g_resources.presentPtr) != MH_OK) {
		logger::warn("Failed to disable Present hook");
	}
	MH_Uninitialize();

    LoadingMenuVideo::CleanupEngineTexture();

	if (g_jobHandle)
		CloseHandle(g_jobHandle);

	logger::info("MediaFramework unloaded");
}

void MessageHandler(F4SE::MessagingInterface::Message* msg)
{
	 switch (msg->type) {
	 case F4SE::MessagingInterface::kGameDataReady:
	 	{
	 		auto ui = RE::UI::GetSingleton();
	 		if (ui) {
	 			ui->RegisterMenu(
	 				MediaFrameworkMenu::MENU_NAME,
	 				[](const RE::UIMessage&) -> RE::IMenu* { return new MediaFrameworkMenu(); });
	 			logger::info("MediaFrameworkMenu registered successfully in kGameDataReady");
	 		} else {
	 			logger::warn("UI singleton not available in kGameDataReady - registration skipped");
	 		}
	 		break;
	 	}
	 }
}

/** @brief F4SE plugin query entry point. */
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* const a_f4se, F4SE::PluginInfo* const a_info)
{
	auto path = logger::log_directory();
	if (!path) {
		return false;
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);

#ifndef NDEBUG
	auto msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
	std::vector<spdlog::sink_ptr> sinks = { file_sink, msvc_sink };
	auto log = std::make_shared<spdlog::logger>("global log"s, sinks.begin(), sinks.end());
	log->set_level(spdlog::level::trace);
#else
	auto log = std::make_shared<spdlog::logger>("global log"s, file_sink);
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::warn);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%g(%#): [%^%l%$] %v"s);

	logger::info("{} v{}", Version::PROJECT, Version::NAME);

	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = Version::PROJECT.data();
	a_info->version = Version::MAJOR;

	if (a_f4se->IsEditor()) {
		logger::critical("Loaded in editor");
		return false;
	}

	const auto runtimeVersion = a_f4se->RuntimeVersion();
	if (runtimeVersion < F4SE::RUNTIME_1_10_162) {
		logger::critical("Unsupported runtime v{}", runtimeVersion.string());
		return false;
	}

	return true;
}

/** @brief F4SE plugin load entry point. */
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* const a_f4se)
{
    F4SE::Init(a_f4se);

    logger::info("MediaFramework loaded - installing hooks");
    if (!InstallD3DHook()) {
        return false;
    }

    auto renderer = RE::BSGraphics::RendererData::GetSingleton();
    if (renderer && renderer->device) {
        if (!CompileShadersAndInputLayout(renderer->device) || !CreateRenderStates(renderer->device)) {
            logger::error("Failed to initialize shaders or render states");
            return false;
        }
    }

    // Register MediaFrameworkMenu for PreUI rendering
	const auto messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(MessageHandler)) {
		logger::critical("Failed to register message listener");
		return false;
	}

	g_jobHandle = CreateJobObject(NULL, NULL);
	if (g_jobHandle) {
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo = {};
		jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(g_jobHandle, JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo));
	} else {
		logger::error("Failed to create job object");
	}

    // Added: Install LoadingMenu hooks
    if (!LoadingMenuVideo::InstallHooks()) {
        logger::error("Failed to install LoadingMenuVideo hooks");
        return false;
    }

    // Register cleanup on exit for version independence
    std::atexit(UnloadResources);
	//Sleep(12000);
    return true;
}
