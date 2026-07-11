// OffscreenRenderer.h (MediaFramework)
#pragma once
#include "Globals.h"
#include "D3DUtils.h"
#include "Renderer.h"

using Microsoft::WRL::ComPtr;

class OffscreenRenderer
{
public:
    OffscreenRenderer() = default;
    ~OffscreenRenderer();

    // Creates or resizes the offscreen render target
    bool Ensure(ID3D11Device* device, uint32_t width, uint32_t height);

    // Renders the MediaInstance into the offscreen texture using the custom HLSL shader
    // overrideTargetAspect: pass > 0 to force a specific aspect (e.g. 16.0f/9.0f for LoadingMenu)
    bool Render(ID3D11DeviceContext* ctx, MediaInstance& instance, float overrideTargetAspect = 0.0f);

    ID3D11Texture2D* GetTexture() const { return offscreenTexture.Get(); }
    ID3D11ShaderResourceView* GetSRV() const { return offscreenSRV.Get(); }

    void Cleanup();

private:
    ComPtr<ID3D11Texture2D>          offscreenTexture;
    ComPtr<ID3D11RenderTargetView>   offscreenRTV;
    ComPtr<ID3D11ShaderResourceView> offscreenSRV;

    uint32_t width = 0;
    uint32_t height = 0;
};