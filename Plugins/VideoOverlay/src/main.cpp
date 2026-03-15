// main.cpp (VideoOverlay)
#include "Version.h"
#include "Events.h"

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
    F4SE::Init(a_f4se);

    const auto messaging = F4SE::GetMessagingInterface();
    if (!messaging) {
        logger::critical("Failed to get F4SE messaging interface");
        return false;
    }

    messaging->RegisterListener(MessageHandler);

    logger::info("Plugin loaded successfully");

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
