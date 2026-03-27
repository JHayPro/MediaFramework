// main.cpp (MediaLoadscreen)
#include "MediaLoadScreensAPI.h"
#include "Events.h"

std::filesystem::path GetCurrentDLLPath() {
	HMODULE hm = NULL;
	// Use the address of this current function to find the containing DLL
	if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&GetCurrentDLLPath, &hm)) {
		wchar_t path[MAX_PATH];
		GetModuleFileNameW(hm, path, MAX_PATH);
		return std::filesystem::path(path);
	}
	return "";
}

bool F4SEAPI Register(RE::BSScript::IVirtualMachine* a_vm)
{
	if (!a_vm) {
		return false;
	}

	const auto obj = "MediaLoadscreens"sv;
	a_vm->BindNativeMethod(obj, "QueueMediaFile"sv, MediaLoadScreensPapyrus::QueueMediaFile, std::nullopt, false);
	a_vm->BindNativeMethod(obj, "QueueMediaFolder"sv, MediaLoadScreensPapyrus::QueueMediaFolder, std::nullopt, false);

	logger::info("Papyrus functions registered");
	return true;
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
	F4SE::Init(a_f4se);

	const auto messaging = F4SE::GetMessagingInterface();
	if (!messaging) {
		logger::critical("Failed to get F4SE messaging interface");
		return false;
	}

	const auto papyrus = F4SE::GetPapyrusInterface();
	if (!papyrus) {
		logger::critical("Failed to get F4SE papyrus interface");
		return false;
	}
	papyrus->Register(Register);

	messaging->RegisterListener(MessageHandler);

	auto fullPath = GetCurrentDLLPath();
	if (!std::filesystem::exists(fullPath)) {
		logger::critical("Failed to get plugin path");
		return false;
	}
	dllParentPath = fullPath.parent_path();
	parentIniPath = dllParentPath.string() + "\\" + Version::PROJECT.data() + ".ini";

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