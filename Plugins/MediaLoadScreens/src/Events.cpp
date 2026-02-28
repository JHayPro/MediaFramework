// Events.cpp (MediaLoadscreen)
#include "Events.h"
#include "PCH.h"
#include <chrono>
#include <filesystem>
#include <random>
#include <thread>

namespace fs = std::filesystem;

static std::string GetMediaFolderPath()
{
    char dllPath[MAX_PATH]{};
    HMODULE hModule = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(GetMediaFolderPath),
                           &hModule))
    {
        GetModuleFileNameA(hModule, dllPath, MAX_PATH);
    }
    else
    {
        logger::error("MediaLoadscreen: Failed to get DLL module handle");
        return "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Fallout 4\\Data\\F4SE\\Plugins\\ALR-V_Videos";
    }

    fs::path dllFile(dllPath);
    return (dllFile.parent_path() / "ALR-V_Videos").string();
}

/** @brief Event sink for loading menu open/close. */
class LoadingMenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	static void Dummy() {}
	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& ev, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
	{
		std::string menuName = ev.menuName.c_str();
		if (menuName == "LoadingMenu") {
			if (ev.opening) {
				DecoderCreateParams decParams = { sizeof(DecoderCreateParams), DecoderComposition{ DecoderVisualType::Video, AudioType::Enabled } };
				if (MF_CreateDecoder(&decParams, &g_decoderHandle) != MF_Result::Ok) {
					logger::error("MediaLoadscreen: Failed to create decoder");
					return RE::BSEventNotifyControl::kContinue;
				}

				g_menuOpen.store(true);
				if (g_checkThread.joinable()) {
					g_checkThread.join();
				}
				g_checkThread = std::thread([]() {
					const std::string folder = GetMediaFolderPath();
					MediaDescriptor descs[256]{};
					uint32_t count = 0;

					if (MF_DiscoverMedia(folder.c_str(), descs, 256, &count) != MF_Result::Ok || count == 0) {
						logger::warn("MediaLoadscreen: No media found via discovery in {}", folder);
					}
					// Random selection
					std::random_device rd;
					std::mt19937 gen(rd());
					std::uniform_int_distribution<uint32_t> dist(0, count - 1);
					MediaDescriptor selectedMediaDescriptor{};
					selectedMediaDescriptor = descs[dist(gen)];

					
					MediaCommandPacket commands[16];
					uint32_t commandCount = 0;

					MF_Result result = MF_ParseMediaINI(
						"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Fallout 4\\Data\\F4SE\\Plugins\\MediaLoadscreen.ini",  // Child defaults (can be NULL)
						selectedMediaDescriptor.iniPath,                                                                           // File-specific INI
						commands,
						16, 
						&commandCount);

					MediaCreateParams createParams{ sizeof(MediaCreateParams), selectedMediaDescriptor.primaryPath, selectedMediaDescriptor.mediaComposition };


					while (g_menuOpen.load()) {
						VideoQueryResult videoQueryResult;
						if (MF_QueryVideo(g_instanceHandle, VideoQueryType::InstanceValid, &videoQueryResult) == MF_Result::Ok && !videoQueryResult.boolValue) {

							if (MF_CreateMediaInstance(g_decoderHandle, &createParams, commands, commandCount, &g_instanceHandle) != MF_Result::Ok) {
								logger::error("MediaLoadscreen: Failed to recreate video instance");
								return;
							}

						} else if (MF_QueryVideo(g_instanceHandle, VideoQueryType::IsPlaying, &videoQueryResult) == MF_Result::Ok && !videoQueryResult.boolValue) {
							logger::info("MediaLoadscreen: Video instance {} ended during loading, restarting", g_instanceHandle);
							MF_DestroyMediaInstance(g_instanceHandle);
							// Play from -1
							//PlayParams playParams{ -1, repeatSetting::None, 1.0f };
							//VideoCommandPacket cmd{ VideoCommandType::Play, &playParams };
							//MF_VideoCommand(g_instanceHandle, &cmd, 1);
						}
						std::this_thread::sleep_for(std::chrono::milliseconds(20));
					}
				});
				//            // Get last frame
				//            VideoQueryResult frameRes;
				//            int64_t lastFrame = -1;
				//if (MF_QueryVideo(g_instanceHandle, VideoQueryType::CurrentTime, &frameRes) == MF_Result::Ok) {
				//	lastFrame = frameRes.floatValue;
				//            }
			} else {
				g_menuOpen.store(false);
				if (g_checkThread.joinable()) {
					g_checkThread.join();
				}
				MF_DestroyDecoder(g_decoderHandle);
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}
};

/** @brief Handles F4SE messages. */
void MessageHandler(F4SE::MessagingInterface::Message* const msg)
{
	if (msg->type == F4SE::MessagingInterface::kGameDataReady) {
		auto ui = RE::UI::GetSingleton();
		if (ui) {
			static LoadingMenuSink sink;
			ui->RegisterSink<RE::MenuOpenCloseEvent>(&sink);
			logger::info("MediaLoadscreen: Registered LoadingMenu sink");
		} else {
			logger::warn("MediaLoadscreen: UI singleton not available in kGameDataReady");
		}
	}
}
