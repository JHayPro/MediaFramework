// ImageLoader.cpp (MediaFramework)
#include "ImageLoader.h"

bool LoadDDSImage(ID3D11Device* device, const std::string& ddsPath, MediaInstance& instance)
{
    if (!device) {
        logger::error("LoadDDSImage: null device");
        return false;
    }

    try {
        // Convert UTF-8 path to wstring for DirectXTex
		std::wstring wpath;
		int requiredSize = MultiByteToWideChar(CP_UTF8, 0, ddsPath.c_str(), -1, nullptr, 0);
		if (requiredSize > 0) {
			wpath.resize(requiredSize - 1);  // Exclude null terminator from size
			if (MultiByteToWideChar(CP_UTF8, 0, ddsPath.c_str(), -1, &wpath[0], requiredSize) == 0) {
				logger::error("Path wstring conversion: Handle conversion error");
			}
		} else {
			logger::error("Path wstring conversion: Handle size query error");
		}

        // Check if file exists
        if (!std::filesystem::exists(wpath)) {
            logger::error("LoadDDSImage: file not found: {}", ddsPath);
            return false;
        }

        // Determine loader based on extension (case-insensitive)
        std::filesystem::path fsPath(wpath);
        std::wstring wext = fsPath.extension().wstring();
        std::string lowerExt;
        lowerExt.resize(wext.length());
        std::transform(wext.begin(), wext.end(), lowerExt.begin(),
            [](wchar_t c) -> char {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            });

        DirectX::ScratchImage image;
        DirectX::TexMetadata metadata;
        HRESULT hr;

        if (lowerExt == ".dds") {
            // Original DDS path - exactly the same as before
            hr = DirectX::LoadFromDDSFile(
                wpath.c_str(),
                DirectX::DDS_FLAGS_NONE,
                &metadata,
                image
            );

            if (FAILED(hr)) {
                logger::error("LoadDDSImage: LoadFromDDSFile failed for {}: 0x{:X}", ddsPath, static_cast<uint32_t>(hr));
                return false;
            }
        } else {
            // PNG, JPG, JPEG, BMP, TIFF, etc. via WIC
            hr = DirectX::LoadFromWICFile(
                wpath.c_str(),
                DirectX::WIC_FLAGS_NONE,
                &metadata,
                image
            );

            if (FAILED(hr)) {
                logger::error("LoadDDSImage: LoadFromWICFile failed for {}: 0x{:X}", ddsPath, static_cast<uint32_t>(hr));
                return false;
            }
        }

		if (DirectX::IsCompressed(metadata.format)) {
			DirectX::ScratchImage outImage;
			HRESULT hr = DirectX::Decompress(
				image.GetImages(),
				image.GetImageCount(),
				metadata,
				DXGI_FORMAT_B8G8R8A8_UNORM,
				outImage);

			if (FAILED(hr)) {
				logger::error("LoadDDSImage: Decompress failed for {} (BC format {}): 0x{:X}",
					ddsPath, static_cast<int>(metadata.format), static_cast<uint32_t>(hr));
				return false;
			}
            // Fixed move order (original had a subtle bug)
			metadata = outImage.GetMetadata();
			image = std::move(outImage);
		} 

		if (metadata.format != DXGI_FORMAT_B8G8R8A8_UNORM && instance.renderStage != RenderPipelineStage::TextureSwap) {
			DirectX::ScratchImage outImage;
			HRESULT hr = DirectX::Convert(
				image.GetImages(), image.GetImageCount(), metadata,
				DXGI_FORMAT_B8G8R8A8_UNORM,
				DirectX::TEX_FILTER_DEFAULT | DirectX::TEX_FILTER_SRGB,
				DirectX::TEX_THRESHOLD_DEFAULT,
				outImage);

			if (FAILED(hr)) {
				logger::error("LoadDDSImage: Convert failed for {}: 0x{:X}", ddsPath, static_cast<uint32_t>(hr));
				return false;
			}

			metadata = outImage.GetMetadata();
			image = std::move(outImage);
		}

        // Validate format (unchanged)
        if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D) {
            logger::error("LoadDDSImage: only 2D textures supported: {}", ddsPath);
            return false;
        }

        //logger::debug("LoadDDSImage: loaded DDS {}x{}, {} mips, format {}",
        //    metadata.width, metadata.height, metadata.mipLevels, static_cast<int>(metadata.format));

        // TODO: Generate mipmaps if needed for texture replacement feature
        // This is currently skipped - DDS files should pre-bake mips if needed
        // if (metadata.mipLevels == 1 && wantMipmaps) {
        //     DirectX::ScratchImage mipChain;
        //     hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), 
        //                                    metadata, DirectX::TEX_FILTER_DEFAULT, 0, mipChain);
        //     if (SUCCEEDED(hr)) {
        //         image = std::move(mipChain);
        //     }
        // }
        
		// BSTextures for videos expect only 1 Mip Level, no real point in coordinating this right now
		if (instance.renderStage != RenderPipelineStage::TextureSwap)
			metadata.mipLevels = 1;

		// Create D3D11 texture from loaded image
        ComPtr<ID3D11Resource> resource;
        hr = DirectX::CreateTexture(
            device,
            image.GetImages(),
            image.GetImageCount(),
            metadata,
            resource.GetAddressOf()
        );

        if (FAILED(hr)) {
            logger::error("LoadDDSImage: CreateTexture failed for {}: 0x{:X}", ddsPath, static_cast<uint32_t>(hr));
            return false;
        }

        // QueryInterface to ID3D11Texture2D
        ComPtr<ID3D11Texture2D> texture2D;
        hr = resource.As(&texture2D);
        if (FAILED(hr)) {
            logger::error("LoadDDSImage: failed to get ID3D11Texture2D interface: 0x{:X}", static_cast<uint32_t>(hr));
            return false;
        }

        // Get texture description for dimensions
        D3D11_TEXTURE2D_DESC desc;
        texture2D->GetDesc(&desc);

        // Create shader resource view with full mip chain
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;

        ComPtr<ID3D11ShaderResourceView> srv;
        hr = device->CreateShaderResourceView(texture2D.Get(), &srvDesc, srv.GetAddressOf());
        if (FAILED(hr)) {
            logger::error("LoadDDSImage: CreateShaderResourceView failed for {}: 0x{:X}", ddsPath, static_cast<uint32_t>(hr));
            return false;
        }

        // Store results in instance
		instance.mediaTexture = std::move(texture2D);
		instance.srv = std::move(srv);
		instance.mediaWidth = desc.Width;
		instance.mediaHeight = desc.Height;

        logger::info("LoadDDSImage: successfully loaded {} ({}x{})", ddsPath, desc.Width, desc.Height);
        return true;

    } catch (const std::exception& e) {
        logger::error("LoadDDSImage: exception loading {}: {}", ddsPath, e.what());
        return false;
    } catch (...) {
        logger::error("LoadDDSImage: unknown exception loading {}", ddsPath);
        return false;
    }
}
