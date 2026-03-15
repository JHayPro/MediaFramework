// Main.cpp (MediaFramework)
#include "D3DUtils.h"
#include "DecoderManager.h"
#include "Hooks.h"
#include "SharedMemoryUtils.h"
#include "MediaFrameworkAPI.h"
#include "MediaFrameworkMenu.h"
#include "LoadingMenuVideoSupport.h"

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
F4SE_EXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = Version::PROJECT.data();
	a_info->version = Version::MAJOR;

#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		return false;
	}

	*path /= fmt::format(FMT_STRING("{}.log"), Version::PROJECT);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef NDEBUG
	log->set_level(spdlog::level::trace);
#else
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::warn);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%g(%#): [%^%l%$] %v"s);

	logger::info(FMT_STRING("=== {} v{}.{}.{} ==="), Version::PROJECT, Version::MAJOR, Version::MINOR, Version::PATCH);

	if (a_f4se->IsEditor()) {
		logger::critical("loaded in editor");
		return false;
	}

	const auto ver = a_f4se->RuntimeVersion();
	if ((REL::Module::IsF4() && ver < F4SE::RUNTIME_1_10_163) || REL::Module::IsVR()) {
		logger::critical("unsupported runtime v{}, isVR{}", ver.string(), REL::Module::IsVR());
		return false;
	}

	logger::info("{}: Query successful", Version::PROJECT);

	F4SE::AllocTrampoline(32 * 8);
	return true;
}

/** @brief F4SE plugin load entry point. */
F4SE_EXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* const a_f4se)
{
	g_jobHandle = CreateJobObject(NULL, NULL);
	if (g_jobHandle) {
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo = {};
		jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(g_jobHandle, JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo));
	}
	else {
		logger::error("Failed to create job object");
	}

	F4SE::Init(a_f4se, true);
	const auto messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(MessageHandler)) {
		logger::critical("Failed to register message listener");
		return false;
	}

    logger::info("MediaFramework loaded - installing hooks");
    if (!InstallD3DHook()) {
        return false;
    }

	if (!LoadingMenuVideo::InstallHooks()) {
		logger::error("Failed to install LoadingMenuVideo hooks");
		return false;
	}

    auto renderer = RE::BSGraphics::RendererData::GetSingleton();
    if (renderer && renderer->device) {
		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(renderer->device);
        if (!CompileShadersAndInputLayout(device) || !CreateRenderStates(device)) {
            logger::error("Failed to initialize shaders or render states");
            return false;
        }
    }

    std::atexit(UnloadResources);

    return true;
}

F4SE_EXPORT constinit auto F4SEPlugin_Version = []() noexcept {
	F4SE::PluginVersionData data{};

	data.AuthorName(Version::AUTHOR);
	data.PluginName(Version::PROJECT);
	data.PluginVersion(Version::VERSION);

	data.UsesAddressLibrary(true);
	data.IsLayoutDependent(true);
	data.UsesSigScanning(false);
	data.HasNoStructUse(false);

	data.CompatibleVersions({ F4SE::RUNTIME_1_10_163 });

	return data;
}();
