// Events.cpp (MediaLoadscreen)
#include "Events.h"

std::filesystem::path dllParentPath;
std::filesystem::path parentIniPath;

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
					logger::error("Failed to create decoder");
					return RE::BSEventNotifyControl::kContinue;
				}

				g_menuOpen.store(true);
				if (g_checkThread.joinable()) {
					g_checkThread.join();
				}

				DecoderHandle decoderHandle = g_decoderHandle;

				g_checkThread = std::thread([decoderHandle]() {
					const std::string folder = (dllParentPath / "ALR-V_Videos").string();
					MediaDescriptor descs[256]{};
					uint32_t count = 0;

					if (MF_DiscoverMedia(folder.c_str(), descs, 256, &count) != MF_Result::Ok || count == 0) {
						logger::warn("No media found via discovery in {}", folder);
					}

					// Random selection
					std::random_device rd;
					std::mt19937 gen(rd());
					std::uniform_int_distribution<uint32_t> dist(0, count - 1);
					MediaDescriptor selectedMediaDescriptor = descs[dist(gen)];

					MediaCommandPacket commands[16];
					uint32_t commandCount = 0;

					MF_Result result = MF_ParseMediaINI(
						parentIniPath.string().c_str(),
						selectedMediaDescriptor.iniPath,
						commands,
						16,
						&commandCount);

					MediaCreateParams createParams{ sizeof(MediaCreateParams), selectedMediaDescriptor.primaryPath, selectedMediaDescriptor.mediaComposition };

					MediaInstanceHandle instanceHandle = -1;

					while (g_menuOpen.load()) {
						VideoQueryResult videoQueryResult;
						if (MF_QueryVideo(instanceHandle, VideoQueryType::InstanceValid, &videoQueryResult) == MF_Result::Ok && !videoQueryResult.boolValue) {

							if (MF_CreateMediaInstance(decoderHandle, &createParams, commands, commandCount, &instanceHandle) != MF_Result::Ok) {
								logger::error("Failed to recreate video instance");
								return;
							}

						}
						else if (MF_QueryVideo(instanceHandle, VideoQueryType::IsPlaying, &videoQueryResult) == MF_Result::Ok && !videoQueryResult.boolValue) {
							logger::info("Video instance {} ended during loading, restarting", instanceHandle);
							MF_DestroyMediaInstance(instanceHandle);
							instanceHandle = -1;
							// Play from -1
							//PlayParams playParams{ -1, repeatSetting::None, 1.0f };
							//VideoCommandPacket cmd{ VideoCommandType::Play, &playParams };
							//MF_VideoCommand(instanceHandle, &cmd, 1);
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
				}
			else {
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
		}
		else {
			logger::warn("MediaLoadscreen: UI singleton not available in kGameDataReady");
		}
	}
}