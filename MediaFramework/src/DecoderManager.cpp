// DecoderManager.cpp (MediaFramework)
#include "D3DUtils.h"
#include "DecoderManager.h"
#include "Globals.h"
#include "SharedMemoryUtils.h"

inline uint32_t GetTickCountMilliseconds()
{
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

bool InitializeDecoder(Decoder& decoder, DecoderComposition decoderComposition)
{
    char dllPath[MAX_PATH];
    
    HMODULE hModule = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&InitializeDecoder), &hModule)) {
        logger::error("Failed to get module handle for decoder init: 0x{:X}", GetLastError());
        return false;
    }
    
    GetModuleFileNameA(hModule, dllPath, MAX_PATH);
    std::string dir = dllPath;
    dir = dir.substr(0, dir.find_last_of("\\/"));
    
    // Launch video decoder process (warm mode - waits for commands)
    std::string videoExePath = dir + "\\MediaFramework_Decoders\\VideoDecoder.exe";
    std::string videoCmdLine = "\"" + videoExePath + "\" " + decoder.videoShmName;
    
    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessA(nullptr, const_cast<LPSTR>(videoCmdLine.c_str()), nullptr, nullptr,
            FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &startupInfo, &processInfo)) {
        logger::error("Failed to launch VideoDecoder.exe for decoder {}: 0x{:X}",
            decoder.id, GetLastError());
        return false;
    }
	decoder.videoProcess = processInfo.hProcess;

	if (g_jobHandle) {
		AssignProcessToJobObject(g_jobHandle, decoder.videoProcess);
	}
    
    CloseHandle(processInfo.hThread);
    
    logger::info("Video decoder process launched for decoder {} with PID: {} (warm mode)",
        decoder.id, GetProcessId(processInfo.hProcess));
    
    // Launch audio decoder process (if needed)
	if (decoderComposition.audioType != AudioType::None) {
        std::string audioExePath = dir + "\\MediaFramework_Decoders\\AudioDecoder.exe";
        std::string audioCmdLine = "\"" + audioExePath + "\" " + decoder.audioShmName;
        
        STARTUPINFOA audioStartupInfo{};
        audioStartupInfo.cb = sizeof(audioStartupInfo);
        
        PROCESS_INFORMATION audioProcessInfo{};
        if (!CreateProcessA(nullptr, const_cast<LPSTR>(audioCmdLine.c_str()), nullptr, nullptr,
                FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &audioStartupInfo, &audioProcessInfo)) {
            logger::error("Failed to launch AudioDecoder.exe for decoder {}: 0x{:X}",
                decoder.id, GetLastError());
            // Continue without audio
        } else {
			decoder.audioProcess = audioProcessInfo.hProcess;
			if (g_jobHandle) {
				AssignProcessToJobObject(g_jobHandle, decoder.audioProcess);
			}
            CloseHandle(audioProcessInfo.hThread);
            logger::info("Audio decoder process launched for decoder {} with PID: {} (warm mode)",
                decoder.id, GetProcessId(audioProcessInfo.hProcess));
        }
    }
    
    if (decoder.videoHeader) {
        decoder.videoHeader->shouldExit = 0;
        decoder.videoHeader->command = CMD_NONE;
        decoder.videoHeader->commandAck = 0;
        decoder.lastVideoHeartbeat = GetTickCountMilliseconds();
    }
    
    if (decoder.audioHeader) {
        decoder.audioHeader->shouldExit = 0;
        decoder.lastAudioHeartbeat = GetTickCountMilliseconds();
    }
    
    // Wait a moment for decoder to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    return true;
}

void ShutdownDecoder(Decoder& decoder)
{
    // Signal decoders to exit
    if (decoder.videoHeader) {
        decoder.videoHeader->shouldExit = 1;
    }
    if (decoder.audioHeader) {
        decoder.audioHeader->shouldExit = 1;
    }
    
    // Wait for video decoder
	if (decoder.videoProcess) {
		DWORD waitResult = WaitForSingleObject(decoder.videoProcess, 5000);
        if (waitResult == WAIT_TIMEOUT) {
            logger::warn("Video decoder for decoder {} did not exit within timeout; terminating", decoder.id);
			TerminateProcess(decoder.videoProcess, 0);
        }
		CloseHandle(decoder.videoProcess);
		decoder.videoProcess = nullptr;
    }
    
    // Wait for audio decoder
    if (decoder.audioProcess) {
        DWORD waitResult = WaitForSingleObject(decoder.audioProcess, 5000);
        if (waitResult == WAIT_TIMEOUT) {
            logger::warn("Audio decoder for decoder {} did not exit within timeout; terminating", decoder.id);
            TerminateProcess(decoder.audioProcess, 0);
        }
        CloseHandle(decoder.audioProcess);
        decoder.audioProcess = nullptr;
    }
    
    logger::info("Decoder {} shut down", decoder.id);
}

bool PlayVideoOnDecoder(Decoder& decoder, MediaInstance& instance)
{
    if (!decoder.videoHeader) {
        logger::error("Decoder {} has no video header", decoder.id);
        return false;
    }
    
    // Stop any currently playing video (wait for it to finish)
    if (decoder.isPlaying.load()) {
        logger::info("Stopping current video on decoder {} before switching", decoder.id);
        StopDecoderPlayback(decoder);
        
        // Brief pause to ensure clean stop
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Prepare video play command
    strncpy_s(decoder.videoHeader->videoPath, sizeof(decoder.videoHeader->videoPath),
        instance.mediaPath.c_str(), _TRUNCATE);
    decoder.videoHeader->seekFrame = instance.startFrame;
    decoder.videoHeader->loopEnabled = instance.loop ? 1 : 0;
    decoder.videoHeader->targetFPS = 0.0f;  // Use native FPS
    
    // Clear previous state
    decoder.videoHeader->isReady = 0;
    decoder.videoHeader->frameIndex = 0;
    decoder.videoHeader->absoluteFrame = instance.startFrame >= 0 ? instance.startFrame : 0;
    decoder.videoHeader->commandAck = 0;
    decoder.lastFrameIndex = -1;
    
    // Send video play command
    decoder.videoHeader->command = CMD_PLAY;
    
    // Wait for video command acknowledgment (with timeout)
    auto startTime = std::chrono::steady_clock::now();
    constexpr auto timeout = std::chrono::milliseconds(500);
    
    while (decoder.videoHeader->commandAck == 0) {
        if (std::chrono::steady_clock::now() - startTime > timeout) {
            logger::error("Timeout waiting for video play command acknowledgment on decoder {}", decoder.id);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    logger::info("Decoder {}: Playing video '{}' from frame {} (loop={})",
        decoder.id, instance.mediaPath, instance.startFrame, instance.loop);
    
    // Mark this instance as active
    decoder.currentInstanceHandle = instance.id;
	instance.vbDirty = true;
    instance.isActive.store(true);
    decoder.isPlaying.store(true);
    
    // Send audio play command if audio decoder present
    if (decoder.audioHeader && decoder.audioProcess) {
		strncpy_s(decoder.audioHeader->audioPath, sizeof(decoder.audioHeader->audioPath),
            instance.mediaPath.c_str(), _TRUNCATE);
        decoder.audioHeader->loopEnabled = instance.loop ? 1 : 0;
        decoder.audioHeader->volume = std::clamp(instance.volume, 0.0f, 1.0f);
        decoder.audioHeader->commandAck = 0;
        decoder.audioHeader->command = CMD_PLAY;
        
        // Wait for audio command acknowledgment
        startTime = std::chrono::steady_clock::now();
        while (decoder.audioHeader->commandAck == 0) {
            if (std::chrono::steady_clock::now() - startTime > timeout) {
                logger::warn("Timeout waiting for audio play command acknowledgment on decoder {}", decoder.id);
                break;  // Continue without audio
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        logger::info("Audio playback started for decoder {}", decoder.id);
    }
    
    decoder.lastVideoHeartbeat = GetTickCountMilliseconds();
    if (decoder.audioHeader) {
        decoder.lastAudioHeartbeat = GetTickCountMilliseconds();
    }
    
    logger::info("Started playback on decoder {} for instance {}", decoder.id, instance.id);
    return true;
}

bool StopDecoderPlayback(Decoder& decoder)
{
    if (!decoder.isPlaying.load()) {
        return true;  // Already stopped
    }
    
    if (!decoder.videoHeader) {
        logger::error("Decoder {} has no video header", decoder.id);
        return false;
    }
    
    // Send video stop command
    decoder.videoHeader->commandAck = 0;
    decoder.videoHeader->command = CMD_STOP;
    
    // Wait for video acknowledgment
    auto startTime = std::chrono::steady_clock::now();
    constexpr auto timeout = std::chrono::milliseconds(500);
    
    while (decoder.videoHeader->commandAck == 0) {
        if (std::chrono::steady_clock::now() - startTime > timeout) {
            logger::warn("Timeout waiting for video stop command acknowledgment on decoder {}", decoder.id);
            break;  // Continue anyway
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Send audio stop command if audio decoder present
    if (decoder.audioHeader && decoder.audioProcess) {
        decoder.audioHeader->commandAck = 0;
        decoder.audioHeader->command = CMD_STOP;
        
        // Wait for audio acknowledgment
        startTime = std::chrono::steady_clock::now();
        while (decoder.audioHeader->commandAck == 0) {
            if (std::chrono::steady_clock::now() - startTime > timeout) {
                logger::warn("Timeout waiting for audio stop command acknowledgment on decoder {}", decoder.id);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        logger::info("Audio playback stopped for decoder {}", decoder.id);
    }
    
    decoder.videoHeader->isReady = 0;
    
    // Clear texture
    auto renderer = RE::BSGraphics::RendererData::GetSingleton();
    if (renderer && renderer->device) {
		// ===== IMAGE PATH =====
		// Find which instance is using this decoder to get the path
		auto instIt = g_mediaInstances.find(decoder.currentInstanceHandle);
		if (instIt == g_mediaInstances.end()) {
			logger::info("could not find instance for decoder {}", decoder.id);
			return false;
		}
		MediaInstance& instance = instIt->second;

        ComPtr<ID3D11DeviceContext> ctx;
        renderer->device->GetImmediateContext(ctx.GetAddressOf());
        if (ctx) {
			ClearTextureToBlack(ctx.Get(), instance);
        }
    }
    
    // Call callback if registered
    if (decoder.currentInstanceHandle != 0) {
        auto it = g_mediaInstances.find(decoder.currentInstanceHandle);
        if (it != g_mediaInstances.end() && it->second.finishedCallback) {
            it->second.finishedCallback(decoder.currentInstanceHandle, it->second.callbackUserData);
        }
        
        // Mark instance as inactive
        if (it != g_mediaInstances.end()) {
            it->second.isActive.store(false);
        }
    }
    
    decoder.currentInstanceHandle = 0;
    decoder.isPlaying.store(false);
    
    logger::info("Stopped playback on decoder {}", decoder.id);
    return true;
}

bool SetDecoderVolume(Decoder& decoder, float volume)
{
    if (!decoder.audioHeader) {
        return false;
    }
    
    decoder.audioHeader->volume = std::clamp(volume, 0.0f, 1.0f);
    logger::debug("Set volume to {} for decoder {}", volume, decoder.id);
    return true;
}


//TODO: Does not seem to be used lmao, should readd
void CheckDecoderHealth(Decoder& decoder)
{
    if (!decoder.isInitialized.load()) {
        return;  // Not initialized, nothing to check
    }
    
    uint32_t currentTime = GetTickCountMilliseconds();
    
    // Check video decoder health
    if (decoder.videoHeader) {
        uint32_t lastBeat = decoder.videoHeader->lastHeartbeat;
        uint32_t timeSinceLastBeat = currentTime - lastBeat;
        
        if (timeSinceLastBeat > HEARTBEAT_TIMEOUT_MS) {
            logger::warn("Video decoder {} appears frozen ({}ms since last beat)",
                decoder.id, timeSinceLastBeat);
            
            if (decoder.videoProcess) {
                DWORD exitCode = STILL_ACTIVE;
				if (GetExitCodeProcess(decoder.videoProcess, &exitCode)) {
                    if (exitCode != STILL_ACTIVE) {
                        logger::error("Video decoder crashed with exit code: {}", exitCode);
                        
                        if (decoder.videoHeader->errorMessage[0]) {
                            logger::error("Decoder error: {}", decoder.videoHeader->errorMessage);
                        }
                        
                        // Mark as not initialized to prevent further use
                        decoder.isInitialized.store(false);
                        decoder.isPlaying.store(false);
                        
                        logger::error("Decoder {} is no longer functional", decoder.id);
                    }
                }
            }
        }
    }
    
    // Check audio decoder health
    if (decoder.audioHeader && decoder.audioProcess) {
        uint32_t lastBeat = decoder.audioHeader->lastHeartbeat;
        uint32_t timeSinceLastBeat = currentTime - lastBeat;
        
        if (timeSinceLastBeat > HEARTBEAT_TIMEOUT_MS) {
            logger::warn("Audio decoder {} appears frozen ({}ms since last beat)",
                decoder.id, timeSinceLastBeat);
            
            DWORD exitCode = STILL_ACTIVE;
            if (GetExitCodeProcess(decoder.audioProcess, &exitCode)) {
                if (exitCode != STILL_ACTIVE) {
                    logger::error("Audio decoder crashed with exit code: {}", exitCode);
                    
                    if (decoder.audioHeader->errorMessage[0]) {
                        logger::error("Audio error: {}", decoder.audioHeader->errorMessage);
                    }
                    
                    // Continue without audio
                    CloseHandle(decoder.audioProcess);
                    decoder.audioProcess = nullptr;
                    logger::warn("Continuing decoder {} without audio", decoder.id);
                }
            }
        }
    }
}

void ClearTextureToBlack(ID3D11DeviceContext* ctx, MediaInstance& instance)
{
	if (!ctx || !instance.mediaTexture) {
        return;
    }
    
    D3D11_MAPPED_SUBRESOURCE mapped{};
	HRESULT hr = ctx->Map(instance.mediaTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
		logger::error("Map failed in ClearTextureToBlack for instance {}: 0x{:X}", instance.id, hr);
        return;
    }

    memset(mapped.pData, 0, static_cast<size_t>(mapped.RowPitch) * instance.mediaHeight);
	ctx->Unmap(instance.mediaTexture.Get(), 0);
    
    logger::debug("Texture cleared to black for instance {}", instance.id);
}
