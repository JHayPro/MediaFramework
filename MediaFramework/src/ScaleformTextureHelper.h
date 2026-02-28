// ScaleformTextureHelper.h (MediaFramework)
#pragma once
#include "PCH.h"

struct ScaleformTextureHelper
{
    // All members that were duplicated between MediaFrameworkMenu and LoadingMenuVideoSupport
    RE::BSGraphics::Texture* bsTexture = nullptr;
    RE::NiPointer<RE::NiTexture> niTexture;
    RE::BSScaleformExternalTexture externalTexture;
    ComPtr<ID3D11Texture2D> engineTexture;   // used by MenuHook path
    uint32_t width = 0;
    uint32_t height = 0;
    bool textureReady = false;

    // Caller-configurable (this is the ONLY new thing)
    std::string textureName;   // what img:// will use
    std::string holderPath;    // path to the object that has onVideoTextureReady()

    bool Ensure(ID3D11Device* device, uint32_t w, uint32_t h);
    void Update(ID3D11DeviceContext* ctx, ID3D11Texture2D* sourceTexture);
    void Cleanup();

	bool BindAndNotify(RE::Scaleform::GFx::Movie* movie);  // only if holderPath is set

private:
    bool CreateBSGraphicsTexture(ID3D11Device* device, uint32_t w, uint32_t h);
    bool CreateNiWrapper();
};
