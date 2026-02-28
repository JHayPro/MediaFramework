// D3DUtils.cpp (MediaFramework)
#include "D3DUtils.h"
#include "ImageLoader.h"

bool CompileShadersAndInputLayout(ID3D11Device* device)
{
	if (!device) {
		return false;
	}

	ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
	bool compiledVS = false;
	bool compiledPS = false;

	// Try external HLSL first
	constexpr auto shaderPath = L"Data\\Shaders\\VideoQuad.hlsl";
	if (GetFileAttributesW(shaderPath) != INVALID_FILE_ATTRIBUTES) {
		HRESULT hr = D3DCompileFromFile(shaderPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
			"VSMain", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errBlob.GetAddressOf());
		if (SUCCEEDED(hr)) { 
			compiledVS = true;
			logger::debug("Compiled VS from external file");
		} else if (errBlob) {
			logger::error("VS compile error: {}", static_cast<const char*>(errBlob->GetBufferPointer()));
		}
	}

	// Fallback to embedded
	if (!compiledVS) {
		HRESULT hr = D3DCompile(kEmbeddedHLSL, strlen(kEmbeddedHLSL), nullptr, nullptr, nullptr,
			"VSMain", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errBlob.GetAddressOf());
		if (FAILED(hr)) {
			if (errBlob) {
				logger::error("Embedded VS error: {}", static_cast<const char*>(errBlob->GetBufferPointer()));
			}
			return false;
		} 
		logger::debug("Compiled VS from embedded HLSL");
	}

	// Pixel shader (same pattern)
	if (GetFileAttributesW(shaderPath) != INVALID_FILE_ATTRIBUTES) {
		HRESULT hr = D3DCompileFromFile(shaderPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
			"PSMain", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errBlob.GetAddressOf());
		if (SUCCEEDED(hr)) {
			compiledPS = true;
			logger::debug("Compiled PS from external file");
		} else if (errBlob) {
			logger::error("PS compile error: {}", static_cast<const char*>(errBlob->GetBufferPointer()));
		}
	}

	if (!compiledPS) {
		HRESULT hr = D3DCompile(kEmbeddedHLSL, strlen(kEmbeddedHLSL), nullptr, nullptr, nullptr,
			"PSMain", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errBlob.GetAddressOf());
		if (FAILED(hr)) {
			if (errBlob) {
				logger::error("Embedded PS error: {}", static_cast<const char*>(errBlob->GetBufferPointer()));
			}
			return false;
		}
		logger::debug("Compiled PS from embedded HLSL");
	}

	// Create shaders
	if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
			nullptr, g_resources.vs.GetAddressOf()))) {
		logger::error("CreateVertexShader failed");
		return false;
	}

	if (FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
			nullptr, g_resources.ps.GetAddressOf()))) {
		logger::error("CreatePixelShader failed");
		return false;
	}

	// Input layout
	constexpr D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	if (FAILED(device->CreateInputLayout(layoutDesc, _countof(layoutDesc),
			vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
			g_resources.layout.GetAddressOf()))) {
		logger::error("CreateInputLayout failed");
		return false;
	}

	return true;
}

bool CreateRenderStates(ID3D11Device* device)
{
    if (!device) {
        return false;
    }

    // Blend state
    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    if (FAILED(device->CreateBlendState(&blendDesc, g_resources.blend.GetAddressOf()))) {
        logger::error("CreateBlendState failed");
        return false;
    }

    // Rasterizer
    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable = TRUE;

    if (FAILED(device->CreateRasterizerState(&rasterDesc, g_resources.raster.GetAddressOf()))) {
        logger::error("CreateRasterizerState failed");
        return false;
    }

    // Depth stencil
    D3D11_DEPTH_STENCIL_DESC depthDesc{};
    depthDesc.DepthEnable = FALSE;
    depthDesc.StencilEnable = FALSE;

    if (FAILED(device->CreateDepthStencilState(&depthDesc, g_resources.depth.GetAddressOf()))) {
        logger::error("CreateDepthStencilState failed");
        return false;
    }

    // Sampler
    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    if (FAILED(device->CreateSamplerState(&samplerDesc, g_resources.sampler.GetAddressOf()))) {
        logger::error("CreateSamplerState failed");
        return false;
    }

    return true;
}

bool EnsureTexture(ID3D11Device* device, Decoder& decoder, MediaInstance& instance)
{
	if (!decoder.videoHeader) {
		return false;
	}

	if (instance.mediaTexture && instance.mediaWidth && instance.mediaHeight) {
		return true;
	}

	instance.mediaWidth = (instance.mediaComposition.visualType == VisualType::Image) ? instance.mediaWidth : decoder.videoHeader->width;
	instance.mediaHeight = (instance.mediaComposition.visualType == VisualType::Image) ? instance.mediaHeight : decoder.videoHeader->height;

	instance.srv.Reset();
	instance.mediaTexture.Reset();

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = instance.mediaWidth;
	desc.Height = instance.mediaHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	ComPtr<ID3D11Texture2D> newTexture;
	if (FAILED(device->CreateTexture2D(&desc, nullptr, newTexture.GetAddressOf()))) {
		logger::error("CreateTexture2D failed for instance {}", instance.id);
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	ComPtr<ID3D11ShaderResourceView> newSrv;
	if (FAILED(device->CreateShaderResourceView(newTexture.Get(), &srvDesc, newSrv.GetAddressOf()))) {
		logger::error("CreateShaderResourceView failed for decoder {}", decoder.id);
		return false;
	}

	instance.mediaTexture = std::move(newTexture);
	instance.srv = std::move(newSrv);
	decoder.lastFrameIndex = -1;

	// Update buffer pointers
	if (decoder.videoHeader->dataSize > 0) {
		decoder.frameData0 = decoder.videoShmView + sizeof(SharedVideoHeader);
		decoder.frameData1 = decoder.frameData0 + decoder.videoHeader->dataSize;
	}

	logger::debug("Texture created for decoder {}: {}x{}", decoder.id,
		instance.mediaWidth, instance.mediaHeight);
	return true;
}

void UpdateTextureFromShared(ID3D11DeviceContext* ctx, Decoder& decoder, MediaInstance& instance)
{
	// CRITICAL: Validate ALL pointers before dereferencing
	if (!ctx) {
		return;
	}

	if (!instance.mediaTexture) {
		return;
	}

	if (!decoder.videoHeader) {
		logger::warn("UpdateTextureFromShared: decoder {} has null header", decoder.id);
		return;
	}

	if (!decoder.frameData0 || !decoder.frameData1) {
		logger::warn("UpdateTextureFromShared: decoder {} has null frame data", decoder.id);
		return;
	}

	// Read volatile values once to avoid TOCTOU issues
	const uint32_t ready = decoder.videoHeader->isReady;
	const int64_t idx = decoder.videoHeader->frameIndex;

	if (ready == 0 || idx == decoder.lastFrameIndex) {
		return;
	}

	if (!decoder.videoHeader) return; // Re-check after reads

	const uint32_t currentReadIndex = decoder.videoHeader->readIndex;
	uint8_t* src = (currentReadIndex == 0) ? decoder.frameData0 : decoder.frameData1;

	D3D11_MAPPED_SUBRESOURCE mapped{};
	HRESULT hr = ctx->Map(instance.mediaTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (FAILED(hr)) {
		// Don't spam errors - this can fail during device loss
		static uint32_t errorCount = 0;
		if (++errorCount % 60 == 1) {  // Log once per ~60 frames
			logger::error("Map failed for decoder {}: 0x{:X}", decoder.id, hr);
		}
		return;
	}

	if (!decoder.videoHeader) { ctx->Unmap(instance.mediaTexture.Get(), 0); return; }

	const uint32_t rowBytes = decoder.videoHeader->width * 4;
	uint8_t* dst = static_cast<uint8_t*>(mapped.pData);

	if (mapped.RowPitch == rowBytes) {
		memcpy(dst, src, static_cast<size_t>(rowBytes) * decoder.videoHeader->height);
	} else {
		for (uint32_t y = 0; y < decoder.videoHeader->height; ++y) {
			memcpy(dst + y * mapped.RowPitch, src + y * rowBytes, rowBytes);
		}
	}

	ctx->Unmap(instance.mediaTexture.Get(), 0);
	if (decoder.videoHeader) { // Check before write
		decoder.videoHeader->isReady = 0;
	}
	decoder.lastFrameIndex = idx;
}

bool EnsureImageTexture(ID3D11Device* device, const std::string& imagePath, MediaInstance& instance)
{
	if (!device) {
		logger::error("EnsureImageTexture: null device");
		return false;
	}

	// If already loaded, we're done
	if (instance.mediaTexture && instance.srv) {
		return true;
	}

	// Lazy load the DDS file
	if (!LoadDDSImage(device, imagePath, instance)) {
		logger::error("EnsureImageTexture: failed to load DDS image: {}", imagePath);
		return false;
	}

	logger::info("EnsureImageTexture: loaded {}x{} from {}", 
		instance.mediaWidth, instance.mediaHeight, imagePath);
	return true;
}
