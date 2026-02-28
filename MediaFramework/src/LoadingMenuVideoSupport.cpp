// LoadingMenuVideoSupport.cpp (MediaFramework)
#include "LoadingMenuVideoSupport.h"
#include "D3DUtils.h"
#include "SharedMemoryUtils.h"
#include "Globals.h"

namespace LoadingMenuVideo
{
    LoadingMenuState g_loadingMenuState;
    std::mutex g_loadingMenuMutex;
  
    // Hook targets
    using AdvanceMovie_t = void (*)(RE::IMenu* menu, float a_deltaTime, std::uint32_t a_currentTime);
    AdvanceMovie_t OriginalAdvanceMovie = nullptr;
  
    using AppendShaderFXInfos_t = void (*)(RE::BSGFxShaderFXTarget*, RE::BSTArray<RE::UIShaderFXInfo>& colorFXInfos, RE::BSTArray<RE::UIShaderFXInfo>& backgroundFXInfos);
    AppendShaderFXInfos_t OriginalAppendShaderFXInfos = nullptr;
    // Copied from MediaFrameworkMenu.cpp (static for use here)
    static RE::NiRect<float> CalculateUIRect(const MediaInstance& instance)
    {
        constexpr float UI_WIDTH = 1920.0f;
        constexpr float UI_HEIGHT = 1080.0f;
       
        if (instance.renderMode == RenderMode::Fullscreen) {
            return RE::NiRect<float>(0.0f, 0.0f, UI_WIDTH, UI_HEIGHT);
        }
       
        // Normalized coords → logical UI coords
        float left = instance.renderX * UI_WIDTH;
        float top = instance.renderY * UI_HEIGHT;
        float right = (instance.renderX + instance.renderW) * UI_WIDTH;
        float bottom = (instance.renderY + instance.renderH) * UI_HEIGHT;
       
        return RE::NiRect<float>(left, top, right, bottom);
    }
    static RE::UIShaderFXInfo CreateQuadForInstance(const MediaInstance& instance)
    {
        RE::UIShaderFXInfo info{};
        // Position in UI space (1920x1080)
        auto rect = CalculateUIRect(instance);
        info.renderQuad.left = rect.left;
        info.renderQuad.top = rect.top;
        info.renderQuad.right = rect.right;
        info.renderQuad.bottom = rect.bottom;
        // Shader configuration
        info.shaderFX.backgroundQuad.left = 0.0f;
        info.shaderFX.backgroundQuad.top = 0.0f;
        info.shaderFX.backgroundQuad.right = 1.0f;
        info.shaderFX.backgroundQuad.bottom = 1.0f;
        // No tint, full brightness
        info.shaderFX.backgroundColor = RE::NiColorA(1.0f, 1.0f, 1.0f, 1.0f);
        info.shaderFX.colorMultipliers = RE::NiColorA(1.0f, 1.0f, 1.0f, 1.0f);
        info.shaderFX.colorBrightness = 1.0f;
        using Flags = RE::UIShaderColors::Flags;
        info.shaderFX.enabledStates = Flags::kBackgroundQuad;
        return info;
    }
  
    // ==============================================
    // Engine Texture Management (Adapted from MediaFrameworkMenu::EnsureEngineTexture)
    // ==============================================
  
    bool EnsureEngineTexture(MediaInstance& instance, ID3D11Device* device)
    {
        // === ORIGINAL CODE KEPT EXACTLY AS-IS UNTIL HELPER ===
        if (!device) {
            logger::error("EnsureEngineTexture: null device");
            return false;
        }

        const uint32_t width = instance.mediaWidth;
        const uint32_t height = instance.mediaHeight;

        if (g_loadingMenuState.bsTexture && g_loadingMenuState.textureReady &&
            g_loadingMenuState.currentInstanceHandle == instance.id &&
            g_loadingMenuState.bsTexture->header.width == width &&
            g_loadingMenuState.bsTexture->header.height == height) {
            return true;
        }
        
        CleanupEngineTexture();

        // === NEW: use helper (exact same creation as original) ===
        g_loadingMenuState.helper.textureName = "LoadingMenuVideo";
        g_loadingMenuState.helper.holderPath  = "root1.FilterHolder_mc.Menu_mc";

        if (!g_loadingMenuState.helper.Ensure(device, width, height)) {
            return false;
        }

        // Sync legacy state (zero behavior change)
        g_loadingMenuState.bsTexture = g_loadingMenuState.helper.bsTexture;
        g_loadingMenuState.niTexture = g_loadingMenuState.helper.niTexture;
        g_loadingMenuState.engineTexture = g_loadingMenuState.helper.engineTexture;
        g_loadingMenuState.textureReady = true;
        g_loadingMenuState.currentInstanceHandle = instance.id;

        // Original Invoke is now inside helper, but we kept the call site for safety
        if (g_loadingMenuState.loadingMenuPtr && g_loadingMenuState.loadingMenuPtr->uiMovie) {
            g_loadingMenuState.helper.BindAndNotify(g_loadingMenuState.loadingMenuPtr->uiMovie.get());
        }

        return true;
    }
  
    void UpdateEngineTexture(MediaInstance& instance, ID3D11DeviceContext* ctx)
    {
		g_loadingMenuState.helper.Update(ctx, instance.mediaTexture.Get());
    }
  
    void CleanupEngineTexture()
    {
        // std::scoped_lock lock(g_loadingMenuMutex);
      
        g_loadingMenuState.helper.Cleanup();
        g_loadingMenuState.externalTexture.ReleaseTexture(); // Cleanup texture binding
        //g_loadingMenuState.niTexture.reset();
        if (g_loadingMenuState.bsTexture) {
            // Engine owns bsTexture, so don't delete, just release ref if needed
            g_loadingMenuState.bsTexture = nullptr;
        }
        g_loadingMenuState.engineTexture.Reset();
        g_loadingMenuState.textureReady = false;
        g_loadingMenuState.currentInstanceHandle = 0;
      
        logger::debug("LoadingMenuVideo: Engine texture cleaned up");
    }
  
    // ==============================================
    // Helper Functions
    // ==============================================
  
    bool ShouldRenderLoadingVideo()
    {
        //std::scoped_lock lock(g_videoMutex);
      
        // Check if we have an active loading video instance
        for (auto& [handle, instance] : g_mediaInstances) {
			if (instance.renderStage == RenderPipelineStage::MenuHook && instance.hookedMenu == HookedMenuName::LoadingMenu && instance.isActive.load()) {
                return true;
            }
        }
        return false;
    }
  
    MediaInstance* GetActiveLoadingVideoInstance()
    {
        std::scoped_lock lock(g_videoMutex);
      
        for (auto& [handle, instance] : g_mediaInstances) {
			if (instance.renderStage == RenderPipelineStage::MenuHook && instance.hookedMenu == HookedMenuName::LoadingMenu && instance.isActive.load()) {
                return &instance;
            }
        }
        return nullptr;
    }
  
    // ==============================================
    // Hooked Functions
    // ==============================================
  
    void HookedAdvanceMovie(RE::IMenu* menu, float a_deltaTime, std::uint32_t a_currentTime)
    {
        // Check if this is LoadingMenu
        bool isLoadingMenu = true; // Since hook is on LoadingMenu-specific impl

		static uint64_t s_lastInstanceHandle = 0;

		MediaInstance* instance = GetActiveLoadingVideoInstance();
		if (instance && instance->id != s_lastInstanceHandle) {
			CleanupEngineTexture();  // kill everything from previous load
			s_lastInstanceHandle = instance->id;
			logger::info("LoadingMenuVideo: New instance detected ({}), cleaned old engine texture", instance->id);
		}
       
        if (isLoadingMenu && ShouldRenderLoadingVideo()) {
            try {
                std::scoped_lock lock(g_loadingMenuMutex);
              
                // Update state
                g_loadingMenuState.loadingMenuPtr = menu;
                //g_loadingMenuState.currentMenuMovie = menu->uiMovie.get(); // For identifying in AppendShaderFXInfos
              
                // Get active loading video instance
                MediaInstance* instance = GetActiveLoadingVideoInstance();
				if (instance && instance->isActive.load()) {
					auto renderer = RE::BSGraphics::RendererData::GetSingleton();
					if (!renderer) {
						logger::error("LoadingMenuVideo: Failed to get renderer");
						OriginalAdvanceMovie(menu, a_deltaTime, a_currentTime);
						return;
					}
					ID3D11Device* device = renderer->device;
					ComPtr<ID3D11DeviceContext> ctx;
					device->GetImmediateContext(ctx.GetAddressOf());

					if (instance->mediaComposition.visualType == VisualType::Image) {
						if (EnsureImageTexture(device, instance->mediaPath, *instance)) {
							if (EnsureEngineTexture(*instance, device)) {
								UpdateEngineTexture(*instance, ctx.Get());
							}
						}
					} else {
						auto decoderIt = g_decoders.find(instance->decoderHandle);
						if (decoderIt != g_decoders.end()) {
							Decoder& decoder = *decoderIt->second;
                      
							if (decoder.isPlaying.load() && decoder.isInitialized.load()) {
								if (device && ctx) {
									if (!decoder.videoHeader) {
										logger::error("Decoder does not have a video header {}", decoder.id);
									} else {
										// Ensure decoder texture exists
										if (decoder.videoHeader && EnsureTexture(device, decoder, *instance)) {
											// Update decoder texture from shared memory
											UpdateTextureFromShared(ctx.Get(), decoder, *instance);

											// Ensure engine texture exists and is registered with SWF
											if (EnsureEngineTexture(*instance, device)) {
												// Copy decoder texture to engine texture
												UpdateEngineTexture(*instance, ctx.Get());
											}
										}
									}
								}
							}
						}
                    }
                }
            } catch (const std::exception& e) {
                logger::error("LoadingMenuVideo: Exception in HookedAdvanceMovie: {}", e.what());
            }
        }
      
        // Call original
        OriginalAdvanceMovie(menu, a_deltaTime, a_currentTime);
    }
  
    void HookedAppendShaderFXInfos(RE::BSGFxShaderFXTarget* target, RE::BSTArray<RE::UIShaderFXInfo>& colorFXInfos, RE::BSTArray<RE::UIShaderFXInfo>& backgroundFXInfos)
    {
		//TODO: REALLY NEED THIS TO NOT HOOK WHEN NOT IN LOAD SCREEN
        // Call original first
        OriginalAppendShaderFXInfos(target, colorFXInfos, backgroundFXInfos);
      
        // Check if this is the loading menu's target and we should render
        bool isLoadingMenuTarget = false;
        {
            std::scoped_lock lock(g_loadingMenuMutex);
           // isLoadingMenuTarget = (target->uiMovie.get() == g_loadingMenuState.currentMenuMovie);
			isLoadingMenuTarget = true; // Don't think this is even false, guess we'll see
        }
      
        if (isLoadingMenuTarget && ShouldRenderLoadingVideo()) {
            try {
                MediaInstance* instance = GetActiveLoadingVideoInstance();
                if (!instance) {
                    return;
                }
              
                std::scoped_lock lock(g_videoMutex);
                auto decoderIt = g_decoders.find(instance->decoderHandle);
                if (decoderIt == g_decoders.end()) {
                    return;
                }
              
                Decoder& decoder = *decoderIt->second;
                if (!decoder.isPlaying.load() || !decoder.isInitialized.load()) {
                    return;
                }
              
				if (!instance->srv || !instance->quadVB || !instance->mediaTexture) {
                    return;
                }
              
                RE::UIShaderFXInfo quadInfo = CreateQuadForInstance(*instance);
                backgroundFXInfos.push_back(quadInfo);
              
                logger::trace("LoadingMenuVideo: Appended ShaderFX info for decoder {}", decoder.id);
              
            } catch (const std::exception& e) {
                logger::error("LoadingMenuVideo: Exception in HookedAppendShaderFXInfos: {}", e.what());
            }
        }
    }
  
    // ==============================================
    // Hook Installation
    // ==============================================
  
    bool InstallHooks()
    {
        logger::info("LoadingMenuVideo: Installing hooks...");
      
        // PSEUDO: Get LoadingMenu::AdvanceMovie address (or GameMenuBase::AdvanceMovie if shared)
        // e.g., via pattern scan: "48 8B C4 48 89 58 08 48 89 70 10" or RelocAddr(0x123456)
		uintptr_t advanceMovieAddr = REL::ID(618896).address();
      
        if (!advanceMovieAddr) {
            logger::error("LoadingMenuVideo: Failed to find AdvanceMovie");
            return false;
        }
      
        // Create hook for AdvanceMovie
        if (MH_CreateHook(reinterpret_cast<void*>(advanceMovieAddr),
                         reinterpret_cast<void*>(&HookedAdvanceMovie),
                         reinterpret_cast<void**>(&OriginalAdvanceMovie)) != MH_OK) {
            logger::error("LoadingMenuVideo: Failed to create AdvanceMovie hook");
            return false;
        }
      
        if (MH_EnableHook(reinterpret_cast<void*>(advanceMovieAddr)) != MH_OK) {
            logger::error("LoadingMenuVideo: Failed to enable AdvanceMovie hook");
            return false;
        }
      
        logger::info("LoadingMenuVideo: AdvanceMovie hooked at 0x{:X}", advanceMovieAddr);
      
        // PSEUDO: Get BSGFxShaderFXTarget::AppendShaderFXInfos address
        // e.g., from vtable offset (assuming index 0x10 or pattern scan)
		uintptr_t appendShaderFXAddr = REL::ID(544646).address();
      
        if (!appendShaderFXAddr) {
            logger::error("LoadingMenuVideo: Failed to find AppendShaderFXInfos");
            return false;
        }
      
        // Create hook for AppendShaderFXInfos
        if (MH_CreateHook(reinterpret_cast<void*>(appendShaderFXAddr),
                         reinterpret_cast<void*>(&HookedAppendShaderFXInfos),
                         reinterpret_cast<void**>(&OriginalAppendShaderFXInfos)) != MH_OK) {
            logger::error("LoadingMenuVideo: Failed to create AppendShaderFXInfos hook");
            return false;
        }
      
        if (MH_EnableHook(reinterpret_cast<void*>(appendShaderFXAddr)) != MH_OK) {
            logger::error("LoadingMenuVideo: Failed to enable AppendShaderFXInfos hook");
            return false;
        }
      
        logger::info("LoadingMenuVideo: AppendShaderFXInfos hooked at 0x{:X}", appendShaderFXAddr);
      
        logger::info("LoadingMenuVideo: All hooks installed successfully");
        return true;
    }
}
