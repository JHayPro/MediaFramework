// Events.cpp (MediaLoadscreen)
#include "Events.h"

std::filesystem::path parentMediaPath;
std::filesystem::path parentIniPath;
std::array<std::vector<LoadScreenMedia>, 5> mediaQueue;
std::mutex g_mediaQueueMutex;

std::random_device rd;
std::mt19937 gen(rd());

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
					LoadScreenMedia selectedLoadScreenMedia;

					for (int priority = 4; priority >= 0; --priority) {
						if (!mediaQueue[priority].empty()) {
							std::uniform_int_distribution<> dis(0, mediaQueue[priority].size() - 1);
							int index = dis(gen);
							selectedLoadScreenMedia = mediaQueue[priority][index];
							if (!selectedLoadScreenMedia.args.persistent) {
								std::swap(mediaQueue[priority][index], mediaQueue[priority].back());
								mediaQueue[priority].pop_back();
							}
							break;
						}
					}
					MediaDescriptor selectedMediaDescriptor = selectedLoadScreenMedia.mediaDescriptor;

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
			logger::info("Registered LoadingMenu sink");
		}
		else {
			logger::warn("UI singleton not available in kGameDataReady");
		}
	}
}