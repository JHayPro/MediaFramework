// SharedMemoryUtils.h (MediaFramework)
#pragma once
#include "Globals.h"
#include "PCH.h"

bool CreateDecoderSharedMemory(Decoder& decoder, DecoderComposition decoderComposition);
void CleanupDecoderSharedMemory(Decoder& decoder);
