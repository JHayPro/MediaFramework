// main.cpp (MediaLoadscreen)
#include "Version.h"
#include "Events.h"

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

	const auto messaging = F4SE::GetMessagingInterface();
	if (!messaging) {
		logger::critical("MediaLoadscreen: Failed to get F4SE messaging interface");
		return false;
	}

	messaging->RegisterListener(MessageHandler);

	logger::info("MediaLoadscreen: Plugin loaded successfully");

	return true;
}

