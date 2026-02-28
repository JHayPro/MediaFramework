// ScaleformTextureHelper.cpp (MediaFramework)'
#include "ScaleformTextureHelper.h"

bool ScaleformTextureHelper::Ensure(ID3D11Device* device, uint32_t w, uint32_t h)
{
    if (!device) {
        logger::error("ScaleformTextureHelper::Ensure: null device");
        return false;
    }

    // Reuse if already correct size + ready (exact same logic as both original places)
    if (bsTexture && textureReady &&
        width == w && height == h) {
        return true;
    }

    Cleanup(); // exact same as original CleanupEngineTexture

    if (!CreateBSGraphicsTexture(device, w, h)) {
        return false;
    }

    if (!CreateNiWrapper()) {
        Cleanup();
        return false;
    }

    // Set the name the SWF will use for img://
    if (!textureName.empty()) {
        niTexture->name = textureName.c_str();
    } else {
        niTexture->name = "MediaFrameworkVideo"; // safe fallback
    }

    // Bind via BSScaleformExternalTexture (exact code from both places)
    externalTexture.texturePath = niTexture->name;
    externalTexture.renderTarget = 1;
    externalTexture.SetTexture(niTexture.get());

    if (!externalTexture.gamebryoTexture) {
        logger::error("ScaleformTextureHelper: SetTexture failed - gamebryoTexture is null!");
        Cleanup();
        return false;
    }

    // Notify SWF if we have a movie and holderPath (this was the delicate part in both)
    if (!holderPath.empty()) {
        // We don't have the movie here - caller will call BindAndNotify manually if needed
        // (this keeps the original delicate Invoke logic in the original classes)
    }

    engineTexture = reinterpret_cast<ID3D11Texture2D*>(bsTexture->texture);
    width = w;
    height = h;
    textureReady = true;

    logger::info("ScaleformTextureHelper: Created {}x{} texture named '{}'", w, h, textureName);
    return true;
}

void ScaleformTextureHelper::Update(ID3D11DeviceContext* ctx, ID3D11Texture2D* sourceTexture)
{
    if (!ctx || !engineTexture || !sourceTexture) {
        return;
    }
    ctx->CopyResource(engineTexture.Get(), sourceTexture);
}

void ScaleformTextureHelper::Cleanup()
{
    externalTexture.ReleaseTexture();
    //niTexture.reset();
    if (bsTexture) {
        bsTexture = nullptr; // engine owns it, never delete
    }
    engineTexture.Reset();
    textureReady = false;
    width = 0;
    height = 0;
}

bool ScaleformTextureHelper::CreateBSGraphicsTexture(ID3D11Device* device, uint32_t w, uint32_t h)
{
    RE::BSGraphics::TextureHeader header{};
    header.width = w;
    header.height = h;
    header.mipCount = 1;
    header.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    header.flags = 0;
    header.tilemode = 0;

    bsTexture = RE::BSGraphics::CreateTexture(header, false);
    if (!bsTexture) {
        logger::error("ScaleformTextureHelper: Failed to create BSGraphics::Texture");
        return false;
    }

    // Black init (exact same in both original files)
    const uint32_t pixelCount = w * h;
    std::vector<uint32_t> blackPixels(pixelCount, 0xFF000000);

    RE::BSGraphics::LoadTextureData(
        bsTexture,
        reinterpret_cast<char*>(blackPixels.data()),
        pixelCount * sizeof(uint32_t),
        0
    );

    if (!bsTexture->srv || !bsTexture->texture) {
        logger::error("ScaleformTextureHelper: LoadTextureData failed");
        return false;
    }
    return true;
}

bool ScaleformTextureHelper::CreateNiWrapper()
{
	if (!niTexture) {
		RE::BSTSmartPointer<RE::BSResource::Stream> emptyStream;
		emptyStream.reset(new RE::BSResource::Archive2::ReaderStream());
		emptyStream->totalSize = 0;
		emptyStream->flags = 0;

		niTexture = RE::NiPointer<RE::NiTexture>(
			RE::NiTexture::Create(emptyStream, "PAIndicatorLineMask_d.DDS", false, false, false));

		if (!niTexture) {
			logger::error("ScaleformTextureHelper: Failed to create NiTexture wrapper");
			return false;
		}
	}

    niTexture->rendererTexture = bsTexture;
    return true;
}

bool ScaleformTextureHelper::BindAndNotify(RE::Scaleform::GFx::Movie* movie)
{
    if (!movie || holderPath.empty()) return false;

    RE::Scaleform::GFx::Value holder;
    if (movie->GetVariable(&holder, holderPath.c_str())) {
        RE::Scaleform::GFx::Value result;
        bool success = holder.Invoke("onVideoTextureReady", &result, nullptr, 0);
        if (success) {
            logger::info("ScaleformTextureHelper: Notified {}->onVideoTextureReady", holderPath);
            return true;
        } else {
            logger::error("ScaleformTextureHelper: Invoke failed on {}", holderPath);
        }
    }
    return false;
}
