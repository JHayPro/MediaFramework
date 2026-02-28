// LoadingMenuVideoSupport.h (MediaFramework)
#pragma once
#include "PCH.h"
#include "Globals.h"

namespace LoadingMenuVideo
{
    // State tracking for loading menu video
    struct LoadingMenuState
    {
        RE::IMenu* loadingMenuPtr = nullptr;
        RE::BSGFxShaderFXTarget* shaderFXTarget = nullptr;
        
        // Engine texture resources (mirroring MediaFrameworkMenu approach)
		RE::BSScaleformExternalTexture externalTexture;
		RE::NiPointer<RE::NiTexture> niTexture;
		RE::BSGraphics::Texture* bsTexture = nullptr;
        ComPtr<ID3D11Texture2D> engineTexture;
		RE::Scaleform::Ptr<RE::Scaleform::GFx::Movie> currentMenuMovie = nullptr;
        
        ScaleformTextureHelper helper;

        uint64_t currentInstanceHandle = 0;
        bool textureReady = false;
        
        void Reset()
        {
            loadingMenuPtr = nullptr;
            shaderFXTarget = nullptr;
			externalTexture.ReleaseTexture();
            //niTexture.reset();
            //bsTexture.reset();
            engineTexture.Reset();
            currentInstanceHandle = 0;
            textureReady = false;
        }
    };
    
    extern LoadingMenuState g_loadingMenuState;
    extern std::mutex g_loadingMenuMutex;
    
    // Hooks
    bool InstallHooks();
    
    // Engine texture management (similar to MediaFrameworkMenu)
    bool EnsureEngineTexture(MediaInstance& instance, ID3D11Device* device);
    void UpdateEngineTexture(MediaInstance& instance, ID3D11DeviceContext* ctx);
    void CleanupEngineTexture();
    
    // Helper to check if we should render video on loading screen
    bool ShouldRenderLoadingVideo();
    MediaInstance* GetActiveLoadingVideoInstance();
}
