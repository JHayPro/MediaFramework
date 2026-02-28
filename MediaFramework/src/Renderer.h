// Renderer.h (MediaFramework)
#pragma once
#include "PCH.h"
#include "Globals.h"

// Replace DrawVideoQuad signature with:
void RenderVideosAtStage(RenderPipelineStage stage, ID3D11DeviceContext* ctx);

// Keep existing HookedPresent signature
HRESULT __stdcall HookedPresent(IDXGISwapChain* const swapChain, const UINT syncInterval, const UINT flags);
 
 
 
