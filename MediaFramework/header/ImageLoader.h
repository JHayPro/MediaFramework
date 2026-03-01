// ImageLoader.h (MediaFramework)
// DDS image loading using DirectXTex
#pragma once
#include "PCH.h"
#include "Globals.h"

/**
 * @brief Load a DDS file into decoder's static texture resources
 * 
 * Loads the DDS file, creates D3D11 texture with all mip levels,
 * and stores the result in decoder.staticTexture/staticSRV.
 * Also updates decoder.textureWidth/textureHeight.
 * 
 * @param device D3D11 device for texture creation
 * @param ddsPath Path to DDS file (relative to game root)
 * @param decoder Decoder to store loaded texture in
 * @return true on success, false on failure (logs error)
 * 
 * @note Thread-safe when called with different decoders
 * @note Does NOT generate mipmaps - uses existing mips in DDS file
 */
bool LoadDDSImage(ID3D11Device* device, const std::string& ddsPath, MediaInstance& instance);
