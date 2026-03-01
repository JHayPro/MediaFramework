// DecoderManager.h (MediaFramework)
#pragma once
#include "PCH.h"
#include "Globals.h"

// Decoder lifecycle
bool InitializeDecoder(Decoder& decoder, DecoderComposition decoderComposition);
void ShutdownDecoder(Decoder& decoder);
void CheckDecoderHealth(Decoder& decoder);

// Playback control
bool PlayVideoOnDecoder(Decoder& decoder, MediaInstance& instance);
bool StopDecoderPlayback(Decoder& decoder);
bool SetDecoderVolume(Decoder& decoder, float volume);

// Utility
void ClearTextureToBlack(ID3D11DeviceContext* ctx, MediaInstance& instance);

