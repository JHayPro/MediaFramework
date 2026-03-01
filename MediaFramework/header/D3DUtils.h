// D3DUtils.h (MediaFramework)
#pragma once
#include "PCH.h"
#include "Globals.h"

bool CompileShadersAndInputLayout(ID3D11Device* const device);
bool CreateRenderStates(ID3D11Device* const device);
bool EnsureTexture(ID3D11Device* const device, Decoder& decoder, MediaInstance& instance);
void UpdateTextureFromShared(ID3D11DeviceContext* const ctx, Decoder& decoder, MediaInstance& instance);

/**
 * @brief Ensure image texture is loaded for image-type decoders
 * 
 * Lazy-loads DDS file on first call. Subsequent calls are no-ops if already loaded.
 * 
 * @param device D3D11 device
 * @param decoder Image decoder
 * @param imagePath Path to DDS file
 * @return true if texture is ready, false on load failure
 */
bool EnsureImageTexture(ID3D11Device* device, const std::string& imagePath, MediaInstance& instance);
