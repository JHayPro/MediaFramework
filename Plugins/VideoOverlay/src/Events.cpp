// Events.cpp (VideoOverlay) - Updated to match new MediaLoadscreen API style
#include "PCH.h"
#include "Events.h"
#include <thread>
#include <chrono>
#include <filesystem>
#include <string>

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
        logger::error("VideoOverlay: Failed to get DLL module handle");
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
                // Stop video when loading starts
                g_videoEnabled.store(false);
                if (g_checkThread.joinable()) {
                    g_checkThread.join();
                }

                // Destroy instance if it is still valid
                VideoQueryResult videoQueryResult;
                if (MF_QueryVideo(g_instanceHandle, VideoQueryType::InstanceValid, &videoQueryResult) == MF_Result::Ok &&
                    videoQueryResult.boolValue) {
                    MF_DestroyMediaInstance(g_instanceHandle);
                }

                MF_DestroyDecoder(g_decoderHandle);
            } else {
                // Start video after loading completes (new API style)
                DecoderCreateParams decParams = { sizeof(DecoderCreateParams), DecoderComposition{ DecoderVisualType::Video, AudioType::Enabled } };
                if (MF_CreateDecoder(&decParams, &g_decoderHandle) != MF_Result::Ok) {
                    logger::error("VideoOverlay: Failed to create decoder");
                    return RE::BSEventNotifyControl::kContinue;
                }

                g_videoEnabled.store(true);
                if (g_checkThread.joinable()) {
                    g_checkThread.join();
                }

                g_checkThread = std::thread([]() {
                    const std::string folder = GetMediaFolderPath();
					MF_OpenRenderMenu(); 

                    MediaDescriptor descs[256]{};
                    uint32_t count = 0;

                    if (MF_DiscoverMedia(folder.c_str(), descs, 256, &count) != MF_Result::Ok || count == 0) {
                        logger::warn("VideoOverlay: No media found via discovery in {}", folder);
                        return;
                    }

                    // Select test0.mp4 specifically (fallback to first if missing)
                    MediaDescriptor selectedMediaDescriptor{};
					// Random selection
					std::random_device rd;
					std::mt19937 gen(rd());
					std::uniform_int_distribution<uint32_t> dist(0, count - 1);
					selectedMediaDescriptor = descs[dist(gen)];

                    MediaCreateParams createParams{ sizeof(MediaCreateParams),
                                                    selectedMediaDescriptor.primaryPath,
                                                    selectedMediaDescriptor.mediaComposition };

                    // Manual render + play commands (exact same behaviour as old code, no RenderMenu calls needed in new API)
                    MediaCommandPacket renderCmd{};
                    renderCmd.type = MediaCommandType::SetRenderMode;
                    renderCmd.params.renderMode.size = sizeof(RenderModeParams);
                    renderCmd.params.renderMode.mode = RenderMode::Window;
                    renderCmd.params.renderMode.x = 0.0f;
                    renderCmd.params.renderMode.y = 0.5f;
                    renderCmd.params.renderMode.w = 0.5f;
                    renderCmd.params.renderMode.h = 0.5f;
                    renderCmd.params.renderMode.stage = RenderPipelineStage::PreUI;

                    MediaCommandPacket playCmd{};
                    playCmd.type = MediaCommandType::Play;

                    MediaCommandPacket commands[2] = { renderCmd, playCmd };
                    uint32_t commandCount = 2;

                    while (g_videoEnabled.load()) {
                        VideoQueryResult videoQueryResult;

                        if (MF_QueryVideo(g_instanceHandle, VideoQueryType::InstanceValid, &videoQueryResult) == MF_Result::Ok &&
                            !videoQueryResult.boolValue) {

                            if (MF_CreateMediaInstance(g_decoderHandle, &createParams, commands, commandCount, &g_instanceHandle) != MF_Result::Ok) {
                                logger::error("VideoOverlay: Failed to recreate video instance");
                                return;
                            }

                        } else if (MF_QueryVideo(g_instanceHandle, VideoQueryType::IsPlaying, &videoQueryResult) == MF_Result::Ok &&
                                   !videoQueryResult.boolValue) {
                            logger::info("VideoOverlay: Video instance {} ended during gameplay, restarting", g_instanceHandle);
                            MF_DestroyMediaInstance(g_instanceHandle);
                        }

                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    }
                });
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
            logger::info("VideoOverlay: Registered LoadingMenu sink");
        } else {
            logger::warn("VideoOverlay: UI singleton not available in kGameDataReady");
        }
    }
}
