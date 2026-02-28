// SharedMemoryUtils.cpp (MediaFramework)
#include "SharedMemoryUtils.h"
#include "MediaFrameworkAPI.h"
#include "Globals.h"

bool CreateDecoderSharedMemory(Decoder& decoder, DecoderComposition decoderComposition)
{
    // Create video shared memory
    if (!decoder.videoShmView) {
        decoder.videoShmHandle = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, static_cast<DWORD>(SHM_SIZE), decoder.videoShmName.c_str());
        if (!decoder.videoShmHandle) {
            logger::error("CreateFileMappingA failed for decoder {}: 0x{:X}", decoder.id, GetLastError());
            return false;
        }
        
        decoder.videoShmView = static_cast<uint8_t*>(MapViewOfFile(decoder.videoShmHandle, FILE_MAP_ALL_ACCESS, 0, 0, SHM_SIZE));
        if (!decoder.videoShmView) {
            logger::error("MapViewOfFile failed for decoder {}: 0x{:X}", decoder.id, GetLastError());
            CloseHandle(decoder.videoShmHandle);
            decoder.videoShmHandle = nullptr;
            return false;
        }
        
        logger::debug("Video shared memory mapped for decoder {}", decoder.id);
        
        ZeroMemory(decoder.videoShmView, SHM_SIZE);
        decoder.videoHeader = reinterpret_cast<SharedVideoHeader*>(decoder.videoShmView);
        decoder.videoHeader->readIndex = 0;
        decoder.videoHeader->writeIndex = 1;
    }
    
    // Create audio shared memory
	if (!decoder.audioShmView && decoderComposition.audioType != AudioType::None) {
        decoder.audioShmHandle = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, static_cast<DWORD>(AUDIO_SHM_SIZE), decoder.audioShmName.c_str());
        if (!decoder.audioShmHandle) {
            logger::error("CreateFileMappingA failed for audio decoder {}: 0x{:X}", decoder.id, GetLastError());
            // Continue without audio
        } else {
            decoder.audioShmView = static_cast<uint8_t*>(MapViewOfFile(decoder.audioShmHandle,
                FILE_MAP_ALL_ACCESS, 0, 0, AUDIO_SHM_SIZE));
            if (!decoder.audioShmView) {
                logger::error("MapViewOfFile failed for audio decoder {}: 0x{:X}", decoder.id, GetLastError());
                CloseHandle(decoder.audioShmHandle);
                decoder.audioShmHandle = nullptr;
            } else {
                logger::debug("Audio shared memory mapped for decoder {}", decoder.id);
                ZeroMemory(decoder.audioShmView, AUDIO_SHM_SIZE);
                decoder.audioHeader = reinterpret_cast<SharedAudioControl*>(decoder.audioShmView);
            }
        }
    }
    
    return true;
}

void CleanupDecoderSharedMemory(Decoder& decoder)
{
    // Cleanup video shared memory
    if (decoder.videoShmView) {
        UnmapViewOfFile(decoder.videoShmView);
        decoder.videoShmView = nullptr;
    }
    if (decoder.videoShmHandle) {
        CloseHandle(decoder.videoShmHandle);
        decoder.videoShmHandle = nullptr;
    }
    decoder.videoHeader = nullptr;
    decoder.frameData0 = nullptr;
    decoder.frameData1 = nullptr;
    
    // Cleanup audio shared memory
    if (decoder.audioShmView) {
        UnmapViewOfFile(decoder.audioShmView);
        decoder.audioShmView = nullptr;
    }
    if (decoder.audioShmHandle) {
        CloseHandle(decoder.audioShmHandle);
        decoder.audioShmHandle = nullptr;
    }
    decoder.audioHeader = nullptr;
    
    logger::debug("Shared memory cleaned up for decoder {}", decoder.id);
}
