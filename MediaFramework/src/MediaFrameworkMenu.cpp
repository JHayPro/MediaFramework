// MediaFrameworkMenu.cpp (MediaFramework)
#include "MediaFrameworkMenu.h"
#include "Globals.h"

std::atomic<uint64_t> g_preUIMenuRefCount{ 0 };

// ============================================================================
// MediaFrameworkShaderFXTarget Implementation
// ============================================================================

void MediaFrameworkShaderFXTarget::AppendShaderFXInfos(
    RE::BSTArray<RE::UIShaderFXInfo>& a_colorFXInfos,
    RE::BSTArray<RE::UIShaderFXInfo>& a_backgroundFXInfos)
{
    std::scoped_lock lock(g_videoMutex);

    // Add video quads for all active PreUI instances
    for (auto& [instanceId, instance] : g_mediaInstances) {
        if (instance.renderStage != RenderPipelineStage::PreUI) {
            continue;
        }

        if (!instance.isActive.load() || !instance.engineTexture.niTexture) {
            continue;
        }

		//Don't think this does anything really
        //auto decoderIt = g_decoders.find(instance.decoderHandle);
        //if (decoderIt == g_decoders.end()) {
        //    continue;
        //}

        //Decoder& decoder = *decoderIt->second;
        //if (!decoder.isPlaying.load() || decoder.currentInstanceHandle != instanceId) {
        //    continue;
        //}

		//NOTE: Might be placing the Menu over the button bar
        RE::UIShaderFXInfo quadInfo = CreateQuadForInstance(instance);
        a_backgroundFXInfos.push_back(quadInfo);
    }
    
    // Call base implementation to handle any Scaleform-based quads
    RE::BSGFxShaderFXTarget::AppendShaderFXInfos(a_colorFXInfos, a_backgroundFXInfos);
}

RE::NiRect<float> MediaFrameworkShaderFXTarget::CalculateUIRect(const MediaInstance& instance) const
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

RE::UIShaderFXInfo MediaFrameworkShaderFXTarget::CreateQuadForInstance(const MediaInstance& instance) const
{
    RE::UIShaderFXInfo info{};

    // Position in UI space (1920x1080)
    auto rect = CalculateUIRect(instance);
    info.renderQuad.left = rect.left;
    info.renderQuad.top = rect.top;
    info.renderQuad.right = rect.right;
    info.renderQuad.bottom = rect.bottom;

    // Shader configuration
    // TODO: Once texture binding works, verify these values
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

// ============================================================================
// MediaFrameworkMenu Implementation
// ============================================================================

MediaFrameworkMenu::MediaFrameworkMenu()
{
    using Flags = RE::UI_MENU_FLAGS;
    
    menuFlags = static_cast<Flags>(0);
    menuFlags |= Flags::kAlwaysOpen;
    menuFlags |= Flags::kRequiresUpdate;
    menuFlags |= Flags::kCustomRendering;
    
    inputContext = RE::UserEvents::INPUT_CONTEXT_ID::kNone;
	depthPriority = RE::UI_DEPTH_PRIORITY::kBook;

    logger::info("MediaFrameworkMenu constructed (ShaderFX, depth=kScope)");
}

MediaFrameworkMenu::~MediaFrameworkMenu()
{
    // Remove shader target from array before destruction
    if (m_shaderTarget) {
        auto it = std::find(shaderFXObjects.begin(), shaderFXObjects.end(), m_shaderTarget.get());
        if (it != shaderFXObjects.end()) {
            shaderFXObjects.erase(it);
        }
    }
}

RE::UI_MESSAGE_RESULTS MediaFrameworkMenu::ProcessMessage(RE::UIMessage& a_message)
{
    if (a_message.type == RE::UI_MESSAGE_TYPE::kShow) {
        uint64_t prev = g_preUIMenuRefCount.fetch_add(1, std::memory_order_release);
        logger::info("MediaFrameworkMenu shown (refcount: {} -> {})", prev, prev + 1);
    } 
    else if (a_message.type == RE::UI_MESSAGE_TYPE::kHide) {
        uint64_t prev = g_preUIMenuRefCount.fetch_sub(1, std::memory_order_release);
        logger::info("MediaFrameworkMenu hidden (refcount: {} -> {})", prev, prev - 1);
        
        if (prev == 0) {
            logger::error("MediaFrameworkMenu refcount underflow!");
        }
    }

    return RE::GameMenuBase::ProcessMessage(a_message);
}

void MediaFrameworkMenu::CreateMovieAndShaderTarget(EngineTextureCache& cache)
{
    // Load SWF movie
    RE::Scaleform::GFx::Movie::ScaleModeType scaleMode = RE::Scaleform::GFx::Movie::ScaleModeType::kShowAll;
    float bgAlpha = 0.0f;  // Transparent

    bool loaded = RE::BSScaleformManager::GetSingleton()->LoadMovie(
        *this, this->uiMovie, cache.menuName, cache.menuObjPath, scaleMode, bgAlpha);

    if (!loaded || !this->uiMovie) {
        logger::error("Failed to load movie: {}", cache.menuName);
        return;
    }

    logger::info("Loaded SWF: {} with root: {}", cache.menuName, cache.menuObjPath);

    // Create shader target from movie
    m_shaderTarget = std::make_unique<MediaFrameworkShaderFXTarget>(*this->uiMovie, cache.menuObjPath);
    this->shaderFXObjects.push_back(m_shaderTarget.get());
    
    logger::info("Created MediaFrameworkShaderFXTarget for movie");
}

bool MediaFrameworkMenu::BindTextureToScaleform(EngineTextureCache& cache)
{
    if (!this->uiMovie) {
        logger::error("Cannot bind texture - no movie loaded");
        return false;
    }

    // Step 1: Set the texture path (this is what img:// will reference)
	cache.externalTexture.texturePath = cache.niTexture->name;
    
    // Step 2: Set render target flag (1 = external texture, based on your struct)
    cache.externalTexture.renderTarget = 1;
    
    // Step 3: Bind the NiTexture - this makes it available to img://MediaFrameworkVideo
    cache.externalTexture.SetTexture(cache.niTexture.get());
    
    logger::info("Bound NiTexture to BSScaleformExternalTexture:");
    logger::info("  - Path: {}", cache.externalTexture.texturePath.c_str());
    logger::info("  - Texture: {:X}", (uintptr_t)cache.externalTexture.gamebryoTexture.get());
    
    // Step 4: Verify the texture was set
    if (!cache.externalTexture.gamebryoTexture) {
        logger::error("SetTexture failed - gamebryoTexture is null!");
        return false;
    }
    
    // Step 5: Notify SWF that texture is ready
	RE::Scaleform::GFx::Value holder;
	if (this->uiMovie->GetVariable(&holder, cache.menuObjPath)) {

		auto holderType = holder.GetType();
		logger::info("Holder type: {}", static_cast<int>(holderType));

		RE::Scaleform::GFx::Value result;
		bool bResult = holder.Invoke("onVideoTextureReady", &result);
		if (bResult) {
			logger::info("Notified SWF MediaFrameworkHolder that texture is ready");
		} else {
			logger::error("Invoke failed on MediaFrameworkHolder.onVideoTextureReady");
			return false;
		}
	} else {
		logger::error("Could not find _root.MediaFrameworkHolder");
		return false;
	}
    
    return true;
}

bool MediaFrameworkMenu::EnsureEngineTexture(MediaInstance& instance, ID3D11Device* device)
{
    auto& cache = instance.engineTexture;
    
    if (cache.bsTexture && !cache.needsRecreate &&
        cache.width == instance.mediaWidth && 
        cache.height == instance.mediaHeight) {
        return true;
    }

    if (!instance.mediaTexture) {
        logger::error("Source texture not available for instance {}", instance.id);
        return false;
    }
    
    cache.Reset();

    cache.menuName = MediaFrameworkMenu::MENU_NAME;
    cache.menuObjPath = "_root.MediaFrameworkMenuInstance";
    cache.bsMenuName = cache.menuName;

    if (!this->uiMovie) {
        CreateMovieAndShaderTarget(cache);
        if (!this->uiMovie) {
            logger::error("Failed to create movie for instance {}", instance.id);
            return false;
        }
    }
    
    cache.helper.textureName = "MediaFrameworkVideo";
    cache.helper.holderPath  = cache.menuObjPath;   // for future onVideoTextureReady

    if (!cache.helper.Ensure(device, instance.mediaWidth, instance.mediaHeight)) {
        return false;
    }

    // Sync back to legacy members so nothing breaks
    cache.bsTexture = cache.helper.bsTexture;
    cache.niTexture = cache.helper.niTexture;
    cache.width     = cache.helper.width;
    cache.height    = cache.helper.height;
    cache.needsRecreate = false;
    if (!BindTextureToScaleform(cache)) {
        logger::error("Failed to bind texture to scaleform for instance {}", instance.id);
        return false;
    }

    return true;
}

void MediaFrameworkMenu::UpdateEngineTexture(MediaInstance& instance, ID3D11DeviceContext* ctx)
{
	if (!ctx || !instance.mediaTexture || !instance.engineTexture.bsTexture) {
		return;
	}

	instance.engineTexture.helper.Update(ctx, instance.mediaTexture.Get());
}

bool MediaFrameworkMenu::IsPreUIEnabled()
{
    return g_preUIMenuRefCount.load(std::memory_order_acquire) > 0;
}
