// MediaFrameworkMenu.h (MediaFramework)
#pragma once
#include "PCH.h"
#include "Globals.h"

/**
 * @brief Custom ShaderFX target that renders video quads
 * This replaces the need for manual quad population
 */
class MediaFrameworkShaderFXTarget : public RE::BSGFxShaderFXTarget
{
public:
    using RE::BSGFxShaderFXTarget::BSGFxShaderFXTarget;
    
    // Override to add video quads
    void AppendShaderFXInfos(
        RE::BSTArray<RE::UIShaderFXInfo>& a_colorFXInfos,
        RE::BSTArray<RE::UIShaderFXInfo>& a_backgroundFXInfos) override;

private:
	RE::NiRect<float> CalculateUIRect(const MediaInstance& instance) const;
    RE::UIShaderFXInfo CreateQuadForInstance(const MediaInstance& instance) const;
};

/**
 * @brief Menu that manages video rendering via ShaderFX
 */
class MediaFrameworkMenu : public RE::GameMenuBase
{
public:
    MediaFrameworkMenu();
    ~MediaFrameworkMenu() override;

    RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override;

    static bool IsPreUIEnabled();
    static constexpr const char* MENU_NAME = "MediaFrameworkMenu";
    
    // Public for Renderer.cpp access
    bool EnsureEngineTexture(MediaInstance& instance, ID3D11Device* device);
    void UpdateEngineTexture(MediaInstance& instance, ID3D11DeviceContext* ctx);
    
private:
    void CreateMovieAndShaderTarget(EngineTextureCache& cache);
    bool BindTextureToScaleform(EngineTextureCache& cache);
    
    std::unique_ptr<MediaFrameworkShaderFXTarget> m_shaderTarget;
};
