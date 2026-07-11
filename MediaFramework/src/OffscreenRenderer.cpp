// OffscreenRenderer.h (MediaFramework)
#include "OffscreenRenderer.h"

OffscreenRenderer::~OffscreenRenderer()
{
    Cleanup();
}

bool OffscreenRenderer::Ensure(ID3D11Device* device, uint32_t w, uint32_t h)
{
    if (!device || w == 0 || h == 0)
        return false;

    if (offscreenTexture && width == w && height == h)
        return true; // Already correct size

    Cleanup();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(device->CreateTexture2D(&desc, nullptr, offscreenTexture.GetAddressOf())))
    {
        logger::error("OffscreenRenderer: Failed to create offscreen texture");
        return false;
    }

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = desc.Format;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

    if (FAILED(device->CreateRenderTargetView(offscreenTexture.Get(), &rtvDesc, offscreenRTV.GetAddressOf())))
    {
        logger::error("OffscreenRenderer: Failed to create RTV");
        Cleanup();
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    if (FAILED(device->CreateShaderResourceView(offscreenTexture.Get(), &srvDesc, offscreenSRV.GetAddressOf())))
    {
        logger::error("OffscreenRenderer: Failed to create SRV");
        Cleanup();
        return false;
    }

    width = w;
    height = h;

    logger::debug("OffscreenRenderer: Created {}x{} offscreen target", w, h);
    return true;
}

bool OffscreenRenderer::Render(ID3D11DeviceContext* ctx, MediaInstance& instance, float overrideTargetAspect)
{
    if (!ctx || !offscreenRTV || !instance.srv)
    {
        logger::error("VideoOffscreenRenderer::Render - Invalid state");
        return false;
    }

    SavedStates states;
    states.Save(ctx);

    // Set render target + viewport
    ID3D11RenderTargetView* rtv = offscreenRTV.Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(width);
    vp.Height = static_cast<float>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);

    // Clear to black
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    ctx->ClearRenderTargetView(offscreenRTV.Get(), clearColor);

    // === Build constant buffer data ===
    float mediaAspect = (instance.mediaHeight > 0)
        ? static_cast<float>(instance.mediaWidth) / static_cast<float>(instance.mediaHeight)
        : 1.0f;

    float targetAspect = (overrideTargetAspect > 0.0f)
        ? overrideTargetAspect
        : (static_cast<float>(width) / static_cast<float>(height));

    auto now = GetTickCountMilliseconds();
    float currentPlaybackTime = static_cast<float>(now - instance.startTime) / 1000.0f;

    struct alignas(16) CBData {
        float     mediaAspect;
        float     targetAspect;
        uint32_t  scaleMode;
        float     fadeInSeconds;
        float     fadeOutSeconds;
        float     fadeColor[4];
        float     currentTime;
        float     duration;
        uint32_t  pad[5];
    };

    CBData cb{};
    cb.mediaAspect = mediaAspect;
    cb.targetAspect = targetAspect;
    cb.scaleMode = static_cast<uint32_t>(instance.scaleMode);
    cb.fadeInSeconds = instance.fadeParams.fadeInSeconds;
    cb.fadeOutSeconds = instance.fadeParams.fadeOutSeconds;
    memcpy(cb.fadeColor, instance.fadeParams.color, sizeof(cb.fadeColor));
    cb.currentTime = currentPlaybackTime;
    cb.duration = 10000.0f;

    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(ctx->Map(g_resources.videoCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &cb, sizeof(cb));
        ctx->Unmap(g_resources.videoCB.Get(), 0);
    }

    // Set shaders and resources
    ctx->VSSetShader(g_resources.vs.Get(), nullptr, 0);
    ctx->PSSetShader(g_resources.ps.Get(), nullptr, 0);
    ctx->IASetInputLayout(g_resources.layout.Get());

    ID3D11Buffer* cbs[] = { g_resources.videoCB.Get() };
    ctx->VSSetConstantBuffers(0, 1, cbs);
    ctx->PSSetConstantBuffers(0, 1, cbs);

    ID3D11ShaderResourceView* srvs[] = { instance.srv.Get() };
    ctx->PSSetShaderResources(0, 1, srvs);

    ID3D11SamplerState* samps[] = { g_resources.sampler.Get() };
    ctx->PSSetSamplers(0, 1, samps);

    // === Vertex Buffer Handling ===
    ID3D11Buffer* vertexBufferToUse = instance.quadVB.Get();

    // If no quadVB exists (common in LoadingMenu), create a fullscreen one
    ComPtr<ID3D11Buffer> fullscreenVB;
    if (!vertexBufferToUse)
    {
        // Create a simple fullscreen quad (NDC space)
        struct Vertex {
            float pos[4];
            float uv[2];
        };

        Vertex verts[6] = {
            { {-1.0f,  1.0f, 0.0f, 1.0f}, {0.0f, 0.0f} },
            { { 1.0f,  1.0f, 0.0f, 1.0f}, {1.0f, 0.0f} },
            { {-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 1.0f} },
            { {-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 1.0f} },
            { { 1.0f,  1.0f, 0.0f, 1.0f}, {1.0f, 0.0f} },
            { { 1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, 1.0f} },
        };

        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.ByteWidth = sizeof(verts);
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA initData{};
        initData.pSysMem = verts;

        ComPtr<ID3D11Device> device;
        ctx->GetDevice(device.GetAddressOf());

        if (FAILED(device->CreateBuffer(&bd, &initData, fullscreenVB.GetAddressOf())))
        {
            logger::error("VideoOffscreenRenderer: Failed to create fullscreen quad");
            states.Restore(ctx);
            return false;
        }

        vertexBufferToUse = fullscreenVB.Get();
    }

    constexpr UINT stride = sizeof(float) * 6; // pos + uv
    constexpr UINT offset = 0;
    ID3D11Buffer* vbs[] = { vertexBufferToUse };
    ctx->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ctx->Draw(6, 0);

    states.Restore(ctx);
    return true;
}

void OffscreenRenderer::Cleanup()
{
    offscreenSRV.Reset();
    offscreenRTV.Reset();
    offscreenTexture.Reset();
    width = 0;
    height = 0;
}