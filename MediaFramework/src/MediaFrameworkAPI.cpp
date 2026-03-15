// MediaFrameworkAPI.cpp (MediaFramework)
#include "MediaFrameworkAPI.h"
#include "DecoderManager.h"
#include "Globals.h"
#include "SharedMemoryUtils.h"
#include "MediaFrameworkIni.h"
#include "MediaFileResolver.h"
#include "MediaFrameworkMenu.h"
#include "Version.h"

extern std::mutex g_videoMutex;

static MF_Result MediaCommand_Internal(
    MediaInstanceHandle instanceHandle,
    const MediaCommandPacket* commands,
    uint32_t commandCount);

static MF_Result DestroyMediaInstance_Internal(MediaInstanceHandle instanceHandle);

extern "C" MF_Result MF_CreateDecoder(
    const DecoderCreateParams* params,
    DecoderHandle* outDecoder)
{
    if (!params || !outDecoder) {
        return MF_Result::InvalidArgument;
    }
    if (params->size != sizeof(DecoderCreateParams)) {
        return MF_Result::InvalidArgument;
    }

    std::scoped_lock lock(g_videoMutex);

    uint64_t id = g_nextDecoderId.fetch_add(1ULL);
	auto decPtr = std::make_unique<Decoder>();
	Decoder& decoder = *decPtr;
    decoder.id = id;
    
    // Setup shared memory names (consistent for this decoder's lifetime)
    decoder.videoShmName = "Local\\MediaFramework_Decoder_" + std::to_string(id);
    decoder.audioShmName = "Local\\MediaFrameworkAudio_Decoder_" + std::to_string(id);
    
    // Initialize shared memory
	if (!CreateDecoderSharedMemory(decoder, params->decoderComposition)) {
        logger::error("Failed to create shared memory for decoder {}", id);
        return MF_Result::InternalError;
    }
    
    // Launch decoder process (stays warm)
	if (!InitializeDecoder(decoder, params->decoderComposition)) {
        logger::error("Failed to initialize decoder {}", id);
        CleanupDecoderSharedMemory(decoder);
        return MF_Result::InternalError;
    }
    
    uint32_t waited = 0;
    const uint32_t timeoutMs = 5000;
    while (decoder.videoHeader->decoderState == STATE_INITIALIZING) {
        if (waited >= timeoutMs) {
            logger::error("Decoder {} init timeout after {} ms", id, timeoutMs);
            ShutdownDecoder(decoder);
            CleanupDecoderSharedMemory(decoder);
            return MF_Result::InternalError;
        }
        Sleep(20);
        waited += 20;
    }
    decoder.isInitialized.store(true);
	logger::info("Created decoder {} of visualType {} and audioType (warm and ready)", id, 
		static_cast<uint32_t>(params->decoderComposition.visualType), 
		static_cast<uint32_t>(params->decoderComposition.audioType));

	 g_decoders[id] = std::move(decPtr);
    *outDecoder = id;
    return MF_Result::Ok;
}

extern "C" MF_Result MF_DestroyDecoder(DecoderHandle decoder)
{
    std::scoped_lock lock(g_videoMutex);

    Decoder* dec = GetDecoder(decoder);

    if (!dec) {
        return MF_Result::InvalidHandle;
    }
    
    // Stop any playing media
    if (dec->isPlaying.load()) {
        StopDecoderPlayback(*dec);
    }
    
    // Cleanup decoder process and resources
    ShutdownDecoder(*dec);
    CleanupDecoderSharedMemory(*dec);

    // Destroy associated media instances
    std::vector<MediaInstanceHandle> toDestroy;
    for (const auto& mediaInstance : g_mediaInstances) {
        if (decoder == mediaInstance.second.decoderHandle) {
            toDestroy.push_back(mediaInstance.first);
        }
    }
    for (const auto& id : toDestroy) {
        DestroyMediaInstance_Internal(id);
    }
    
    g_decoders.erase(decoder);
    logger::info("Destroyed decoder {}", decoder);
    return MF_Result::Ok;
}

extern "C" MF_Result MF_CreateMediaInstance(
    DecoderHandle decoderHandle,
    const MediaCreateParams* createParams,
    const MediaCommandPacket* initialCommands,
    uint32_t initialCommandCount,
    MediaInstanceHandle* outInstance)
{
    if (!createParams || !createParams->mediaPath || !outInstance) {
        return MF_Result::InvalidArgument;
    }
    if (createParams->size != sizeof(MediaCreateParams)) {
        return MF_Result::InvalidArgument;
    }

    std::scoped_lock lock(g_videoMutex);

	//if (createParams->mediaType == )
    Decoder* decoder = GetDecoder(decoderHandle);
    if (!decoder) {
        return MF_Result::InvalidHandle;
    }
    
    if (!decoder->isInitialized.load()) {
        logger::error("Decoder {} not initialized", decoderHandle);
        return MF_Result::InternalError;
    }

    uint64_t id = g_nextMediaInstanceId.fetch_add(1ULL);
    MediaInstance& instance = g_mediaInstances[id];
    instance.id = id;
    instance.decoderHandle = decoderHandle;
    instance.mediaPath = createParams->mediaPath;
	instance.mediaComposition = createParams->mediaComposition;
    instance.finishedCallback = nullptr;
    instance.callbackUserData = nullptr;

    logger::debug("Created media instance {} for decoder {} path: {}", id, decoderHandle, createParams->mediaPath);

    // Process initial commands
    if (initialCommandCount > 0 && initialCommands) {
        MF_Result cmdRes = MediaCommand_Internal(id, initialCommands, initialCommandCount);
        if (cmdRes != MF_Result::Ok) {
            g_mediaInstances.erase(id);
            return cmdRes;
        }
    }

    *outInstance = id;
    return MF_Result::Ok;
}

extern "C" MF_Result MF_MediaCommand(
    MediaInstanceHandle instanceHandle,
    const MediaCommandPacket* commands,
    uint32_t commandCount)
{
    std::scoped_lock lock(g_videoMutex);
    return MediaCommand_Internal(instanceHandle, commands, commandCount);
}

static MF_Result MediaCommand_Internal(
    MediaInstanceHandle instanceHandle,
    const MediaCommandPacket* commands,
    uint32_t commandCount)
{
    MediaInstance* instance = GetMediaInstance(instanceHandle);
    if (!instance) {
        return MF_Result::InvalidHandle;
    }
    
    Decoder* decoder = GetDecoder(instance->decoderHandle);
	if (!decoder && instance->mediaComposition.visualType != VisualType::Image) {
        logger::error("Video instance {} has invalid decoder handle", instanceHandle);
        return MF_Result::InvalidHandle;
    }

    for (uint32_t i = 0; i < commandCount; ++i) {
        const MediaCommandPacket& cmd = commands[i];
        switch (cmd.type) {
            case MediaCommandType::Play: {
                // Play this instance on its decoder
				if (instance->mediaComposition.visualType == VisualType::Image) {
					instance->vbDirty = true;
					instance->isActive.store(true);
				} else if (!PlayVideoOnDecoder(*decoder, *instance)) {
					return MF_Result::InternalError;
				}
                break;
            }
            case MediaCommandType::Pause: {
                // TODO: Implement pause
                logger::warn("Pause command TODO for instance {}", instanceHandle);
                break;
            }
            case MediaCommandType::Stop: {
				if (instance->mediaComposition.visualType == VisualType::Image) {
					instance->isActive.store(false);
				} else if (!StopDecoderPlayback(*decoder)) {
                    return MF_Result::InternalError;
                }
                break;
            }
            case MediaCommandType::Seek: {
                const SeekParams& seek = cmd.params.seek;
                if (seek.size != sizeof(SeekParams)) {
                    return MF_Result::InvalidArgument;
                }
                // TODO: Implement seek
                logger::warn("Seek command TODO for instance {} to {}", instanceHandle, seek.timeSeconds);
                break;
            }
            case MediaCommandType::SetVolume: {
                const VolumeParams& vol = cmd.params.volume;
                if (vol.size != sizeof(VolumeParams)) {
                    return MF_Result::InvalidArgument;
                }
                if (!SetDecoderVolume(*decoder, vol.volume)) {
                    return MF_Result::InternalError;
                }
                break;
            }
            case MediaCommandType::SetFade: {
                const FadeParams& fade = cmd.params.fade;
                if (fade.size != sizeof(FadeParams)) {
                    return MF_Result::InvalidArgument;
                }
                // TODO: Implement
                logger::warn("SetFade command TODO for instance {}", instanceHandle);
                break;
            }
			case MediaCommandType::SetRenderMode: {
				const RenderModeParams& render = cmd.params.renderMode;
				if (render.size != sizeof(RenderModeParams)) {
					return MF_Result::InvalidArgument;
				}
				instance->renderMode = render.mode;
				instance->renderStage = render.stage;
				instance->renderX = render.x;
				instance->renderY = render.y;
				instance->renderW = render.w;
				instance->renderH = render.h;
				if (decoder->currentInstanceHandle == instanceHandle) {
					instance->vbDirty = true;
				}
				logger::debug("Set render mode={} stage={} for instance {} at ({},{},{},{})",
					static_cast<std::uint32_t>(render.mode), static_cast<std::uint32_t>(render.stage),
					instanceHandle, render.x, render.y, render.w, render.h);
				break;
			}
            case MediaCommandType::RegisterCallback: {
                const CallbackParams& cb = cmd.params.callback;
                if (cb.size != sizeof(CallbackParams)) {
                    return MF_Result::InvalidArgument;
                }
                if (cb.type == CallbackType::VideoFinished) {
                    instance->finishedCallback = cb.func;
                    instance->callbackUserData = cb.userData;
                } else {
                    return MF_Result::Unsupported;
                }
                break;
            }
            default:
                return MF_Result::Unsupported;
        }
    }

    return MF_Result::Ok;
}

extern "C" MF_Result MF_DestroyMediaInstance(MediaInstanceHandle instanceHandle)
{
    std::scoped_lock lock(g_videoMutex);
    return DestroyMediaInstance_Internal(instanceHandle);
}

static MF_Result DestroyMediaInstance_Internal(MediaInstanceHandle instanceHandle)
{
    MediaInstance* instance = GetMediaInstance(instanceHandle);
    if (!instance) {
        return MF_Result::InvalidHandle;
    }
    
    // If this instance is currently playing, stop it
    Decoder* decoder = GetDecoder(instance->decoderHandle);
    if (decoder && decoder->currentInstanceHandle == instanceHandle) {
        StopDecoderPlayback(*decoder);
    }
    
    g_mediaInstances.erase(instanceHandle);
    logger::debug("Destroyed video instance {}", instanceHandle);
    return MF_Result::Ok;
}

extern "C" MF_Result MF_OpenRenderMenu()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) {
        logger::error("MF_OpenRenderMenu: UI singleton unavailable");
        return MF_Result::InternalError;
    } 
    
    std::scoped_lock lock(g_videoMutex);
    
    // Increment refcount
    int prev = g_preUIMenuRefCount.fetch_add(1, std::memory_order_release);
    logger::info("MF_OpenRenderMenu: refcount {} -> {}", prev, prev + 1);
    
    // On 0->1 transition, show menu
    if (prev == 0) {
        // Register menu if not already registered
        // Note: Replaced IsMenuRegistered with check on menuCreators (CommonLibF4 uses BSTHashMap)
        // RE::BSFixedString menuName("MediaFrameworkMenu");
        //if (ui->menuCreators.count(menuName) == 0) {
        //    logger::warn("MediaFrameworkMenu not registered, registering now");
        //    ui->Register("MediaFrameworkMenu", [](RE::BSFixedString) -> RE::IMenu* {
        //        return new MediaFrameworkMenu();
        //    });
        //}
        
        // Show menu (will call ProcessMessage with kShow)
        // Note: RE::UIMessage is a struct, use aggregate initialization instead of ctor
        // RE::UIMessage msg{ menuName, RE::UI_MESSAGE_TYPE::kShow };
		RE::UIMessageQueue::GetSingleton()->AddMessage(MediaFrameworkMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
        
        logger::info("MediaFrameworkMenu shown (first open)");
    }
    
    return MF_Result::Ok;
}

extern "C" MF_Result MF_CloseRenderMenu()
{
    auto ui = RE::UI::GetSingleton();
    if (!ui) {
        logger::error("MF_CloseRenderMenu: UI singleton unavailable");
        return MF_Result::InternalError;
    }
    
    std::scoped_lock lock(g_videoMutex);
    
    // Check for underflow
    int current = g_preUIMenuRefCount.load(std::memory_order_acquire);
    if (current <= 0) {
        logger::error("MF_CloseRenderMenu: refcount already zero!");
        return MF_Result::InternalError;
    }
    
    // Decrement refcount
    int prev = g_preUIMenuRefCount.fetch_sub(1, std::memory_order_release);
    logger::info("MF_CloseRenderMenu: refcount {} -> {}", prev, prev - 1);
    
    // On 1->0 transition, hide menu
    if (prev == 1) {
		RE::BSFixedString menuName(MediaFrameworkMenu::MENU_NAME);
        //RE::UIMessage msg{ menuName, RE::UI_MESSAGE_TYPE::kHide };
		RE::UIMessageQueue::GetSingleton()->AddMessage(MediaFrameworkMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide);
        
        logger::info("MediaFrameworkMenu hidden (last close)");
    }
    
    return MF_Result::Ok;
}

extern "C" MF_Result MF_QueryVideo(
    MediaInstanceHandle instanceHandle,
    VideoQueryType query,
    VideoQueryResult* outResult)
{
    if (!outResult) return MF_Result::InvalidArgument;

    std::scoped_lock lock(g_videoMutex);

    MediaInstance* instance = GetMediaInstance(instanceHandle);
    if (query == VideoQueryType::InstanceValid) {
        if (instance)
            outResult->boolValue = true;
        else
            outResult->boolValue = false;
        return MF_Result::Ok;

    } else if (!instance)
        return MF_Result::InvalidHandle;

    switch (query) {
        case VideoQueryType::IsPlaying: {
			outResult->boolValue = (instance->isActive.load());
            break;
        }
        case VideoQueryType::CurrentTime: {
            // TODO: Implement (map to PTS?)
            outResult->floatValue = 0.0f;
            logger::warn("CurrentTime query TODO");
            break;
        }
        case VideoQueryType::Duration: {
            // TODO: Implement
            outResult->floatValue = 0.0f;
            logger::warn("Duration query TODO");
            break;
        }
        default:
            return MF_Result::Unsupported;
    }

    return MF_Result::Ok;
}

extern "C" MF_Result MF_QueryFramework(
    FrameworkQueryType query,
    FrameworkQueryResult* outResult)
{
    if (!outResult) return MF_Result::InvalidArgument;

    switch (query) {
        case FrameworkQueryType::Framework_Version: {
            outResult->floatValue = Version::VERSION.pack();
            break;
        }
        default:
            return MF_Result::Unsupported;
    }

    return MF_Result::Ok;
}
 
extern "C" MF_Result MF_ParseMediaINI(
    const char* childIniPath,
    const char* fileIniPath,
    MediaCommandPacket* outCommands,
    uint32_t maxCommands,
    uint32_t* outCommandCount)
{
    using namespace MediaFramework::INI;
    // Validate inputs
    if (!fileIniPath || !outCommands || !outCommandCount)
        return MF_Result::InvalidArgument;

    static thread_local ParsedMediaDefinition g_lastParsedDefinition;

    try {
        // Parse INI
        std::string childPath = childIniPath ? childIniPath : "";
        std::string filePath = fileIniPath;
        ParsedMediaDefinition result =
            MediaFrameworkINIParser::ParseMediaDefinition(childPath, filePath);
        if (!result.parseSuccess)
            return MF_Result::ParseError;

        g_lastParsedDefinition = std::move(result);

        // Copy commands
        uint32_t cmdCount = static_cast<uint32_t>(g_lastParsedDefinition.initialCommands.size());
        if (cmdCount > maxCommands)
            cmdCount = maxCommands;
        std::memcpy(outCommands, g_lastParsedDefinition.initialCommands.data(), sizeof(MediaCommandPacket) * cmdCount);
        *outCommandCount = cmdCount;

        return MF_Result::Ok;
    } catch (...) {
        return MF_Result::InternalError;
    }
}

extern "C" const char* MF_GetLastParseError()
{
	return MediaFramework::INI::GetLastParseErrorInternal();
}

extern "C" MF_Result MF_DiscoverMedia(
	const char* path,
	MediaDescriptor* outDescriptors,
	uint32_t maxDescriptors,
	uint32_t* outCount)
{
	if (!path || !outDescriptors || !outCount || maxDescriptors == 0)
		return MF_Result::InvalidArgument;

	try {
		std::filesystem::path fsPath(path);
		MediaFramework::FileResolver::MediaFileResolver resolver;

		auto internal = resolver.Resolve(fsPath);

		uint32_t written = 0;
		for (const auto& i : internal) {
			if (written >= maxDescriptors)
				break;

			auto& d = outDescriptors[written++];
			d.size = sizeof(MediaDescriptor);

			MediaFramework::FileResolver::CopyStringSafe(d.primaryPath, sizeof(d.primaryPath), i.primary.string());
			MediaFramework::FileResolver::CopyStringSafe(d.audioPath, sizeof(d.audioPath), i.audio.empty() ? "" : i.audio.string());
			MediaFramework::FileResolver::CopyStringSafe(d.iniPath, sizeof(d.iniPath), i.ini.empty() ? "" : i.ini.string());

			d.mediaComposition = i.mediaComposition;
		}

		*outCount = written;
		return MF_Result::Ok;
	} catch (const std::exception& e) {
		logger::error("MF_DiscoverMedia exception: {}", e.what());
		*outCount = 0;
		return MF_Result::InternalError;
	} catch (...) {
		logger::error("MF_DiscoverMedia unknown exception");
		*outCount = 0;
		return MF_Result::InternalError;
	}
}
