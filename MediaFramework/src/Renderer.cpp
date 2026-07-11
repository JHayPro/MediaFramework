// Renderer.cpp (MediaFramework)
#include "Renderer.h"
#include "D3DUtils.h"
#include "DecoderManager.h"
#include "SharedMemoryUtils.h"
#include "MediaFrameworkMenu.h"
#include "Globals.h"

struct SavedStates
{
	ComPtr<ID3D11BlendState> blend;
	float blendFactor[4]{};
	UINT mask{};
	ComPtr<ID3D11RasterizerState> raster;
	ComPtr<ID3D11DepthStencilState> depth;
	UINT stencil{};
	ComPtr<ID3D11ShaderResourceView> srv;
	ComPtr<ID3D11SamplerState> sampler;
	ComPtr<ID3D11VertexShader> vs;
	ComPtr<ID3D11PixelShader> ps;
	ComPtr<ID3D11InputLayout> layout;
	ID3D11Buffer* vb{};
	UINT stride{};
	UINT offset{};
	D3D11_PRIMITIVE_TOPOLOGY topo{};

	void Save(ID3D11DeviceContext* ctx)
	{
		ctx->OMGetBlendState(blend.GetAddressOf(), blendFactor, &mask);
		ctx->RSGetState(raster.GetAddressOf());
		ctx->OMGetDepthStencilState(depth.GetAddressOf(), &stencil);
		ctx->PSGetShaderResources(0, 1, srv.GetAddressOf());
		ctx->PSGetSamplers(0, 1, sampler.GetAddressOf());
		ctx->VSGetShader(vs.GetAddressOf(), nullptr, nullptr);
		ctx->PSGetShader(ps.GetAddressOf(), nullptr, nullptr);
		ctx->IAGetInputLayout(layout.GetAddressOf());
		ctx->IAGetVertexBuffers(0, 1, &vb, &stride, &offset);
		ctx->IAGetPrimitiveTopology(&topo);
	}

	void Restore(ID3D11DeviceContext* ctx)
	{
		ctx->OMSetBlendState(blend.Get(), blendFactor, mask);
		ctx->RSSetState(raster.Get());
		ctx->OMSetDepthStencilState(depth.Get(), stencil);
		ctx->PSSetShaderResources(0, 1, srv.GetAddressOf());
		ctx->PSSetSamplers(0, 1, sampler.GetAddressOf());
		ctx->VSSetShader(vs.Get(), nullptr, 0);
		ctx->PSSetShader(ps.Get(), nullptr, 0);
		ctx->IASetInputLayout(layout.Get());
		ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
		ctx->IASetPrimitiveTopology(topo);
		if (vb)
			vb->Release();
	}
};

/**
 * @brief Render videos at TextureSwap/Post stages ONLY
 * PreUI is handled by ShaderFX pipeline in MediaFrameworkMenu
 */
void RenderVideosAtStage(RenderPipelineStage stage, ID3D11DeviceContext* ctx)
{
    if (!ctx) {
        return;
    }
    
    // PreUI should never reach here
	if (stage == RenderPipelineStage::PreUI || stage == RenderPipelineStage::MenuHook) {
        logger::error("PreUI/LoadingMenu stages should not use direct D3D rendering!");
        return;
    }
    
    // Direct D3D11 rendering for TextureSwap and Post stages
	for (auto& [instanceId, instance] : g_mediaInstances) {
		if (instance.renderStage != stage || !instance.isActive.load()) {
			continue;
		}

		// Dont think this is needed?
		//if (instance.mediaComposition.visualType == VisualType::Video){
		//	auto decoderIt = g_decoders.find(instance.decoderHandle);
		//	if (decoderIt == g_decoders.end()) {
		//		continue;
		//	}

		//	Decoder& decoder = *decoderIt->second;

		//	if (!decoder.isPlaying.load()) {
		//		continue;
		//	}

		//	if (decoder.currentInstanceHandle != instanceId) {
		//		continue;
		//	}
		//}

        ID3D11ShaderResourceView* srv = nullptr;
		srv = instance.srv.Get();
		if (!srv) {
			continue;
		}

        // Get dimensions (unified location)
        if (instance.mediaWidth == 0 || instance.mediaHeight == 0) {
            continue;
        }
        
        // Update VB if needed
        if (instance.vbDirty || !instance.quadVB) {
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = sizeof(Vertex) * 6;
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            ComPtr<ID3D11Device> device;
            ctx->GetDevice(device.GetAddressOf());
            if (device && !instance.quadVB) {
                if (FAILED(device->CreateBuffer(&bd, nullptr, instance.quadVB.GetAddressOf()))) {
                    logger::error("Failed to create VB for instance {}", instance.id);
                    continue;
                }
            }

            D3D11_MAPPED_SUBRESOURCE msr{};
            if (SUCCEEDED(ctx->Map(instance.quadVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &msr))) {
                Vertex* verts = static_cast<Vertex*>(msr.pData);

                float left   = -1.0f;
                float right  =  1.0f;
                float top    =  1.0f;
                float bottom = -1.0f;

                if (instance.renderMode == RenderMode::Window) {
                    left   =  2.0f * instance.renderX - 1.0f;
                    right  =  2.0f * (instance.renderX + instance.renderW) - 1.0f;
                    top    =  1.0f - 2.0f * instance.renderY;
                    bottom =  1.0f - 2.0f * (instance.renderY + instance.renderH);
                }

                // Always full target rectangle + UV 0-1.
                // All Fit / Fill / Stretch logic lives in the shader now.
                verts[0] = { {left,  top,    0.f, 1.f}, {0.f, 0.f} };
                verts[1] = { {right, top,    0.f, 1.f}, {1.f, 0.f} };
                verts[2] = { {left,  bottom, 0.f, 1.f}, {0.f, 1.f} };
                verts[3] = verts[2];
                verts[4] = verts[1];
                verts[5] = { {right, bottom, 0.f, 1.f}, {1.f, 1.f} };

                ctx->Unmap(instance.quadVB.Get(), 0);
                instance.vbDirty = false;
            }
        }
        
		if (!instance.srv || !instance.quadVB || !instance.mediaTexture) {
            continue;
        }
        
        if (!g_resources.vs || !g_resources.ps || !g_resources.layout) {
            continue;
        }
        
        SavedStates states;
        states.Save(ctx);
        
        constexpr float clearBlend[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ctx->OMSetBlendState(g_resources.blend.Get(), clearBlend, 0xFFFFFFFF);
        ctx->RSSetState(g_resources.raster.Get());
        ctx->OMSetDepthStencilState(g_resources.depth.Get(), 0);
        ctx->VSSetShader(g_resources.vs.Get(), nullptr, 0);
        ctx->PSSetShader(g_resources.ps.Get(), nullptr, 0);
        ctx->IASetInputLayout(g_resources.layout.Get());
        
        constexpr UINT stride = sizeof(Vertex);
        constexpr UINT offset = 0;
		ID3D11Buffer* vbs[] = { instance.quadVB.Get() };
        ctx->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        
        ID3D11ShaderResourceView* srvs[] = { instance.srv.Get() };
        ctx->PSSetShaderResources(0, 1, srvs);
        
        ID3D11SamplerState* samps[] = { g_resources.sampler.Get() };
        ctx->PSSetSamplers(0, 1, samps);
        
        if (g_resources.videoCB)
        {
            // Current viewport (for correct pixel aspect)
            D3D11_VIEWPORT vp{};
            UINT num = 1;
            ctx->RSGetViewports(&num, &vp);
            const float screenW = (vp.Width  > 0.f) ? vp.Width  : 1.f;
            const float screenH = (vp.Height > 0.f) ? vp.Height : 1.f;

            // Target rect in NDC
            float left = -1.f, right = 1.f, top = 1.f, bottom = -1.f;
            if (instance.renderMode == RenderMode::Window) {
                left   =  2.f * instance.renderX - 1.f;
                right  =  2.f * (instance.renderX + instance.renderW) - 1.f;
                top    =  1.f - 2.f * instance.renderY;
                bottom =  1.f - 2.f * (instance.renderY + instance.renderH);
            }

            const float ndcW = right - left;
            const float ndcH = top - bottom;
            const float targetAspect = (ndcW > 0.f && ndcH > 0.f)
                ? (ndcW / ndcH) * (screenW / screenH)
                : 1.f;

            const float mediaAspect = (instance.mediaHeight > 0)
                ? static_cast<float>(instance.mediaWidth) / static_cast<float>(instance.mediaHeight)
                : 1.f;

            struct alignas(16) CBData {
                float     mediaAspect;
                float     targetAspect;
                uint32_t  scaleMode;
                float     fadeInSeconds;
                float     fadeColor[4];
                float     fadeOutSeconds;
                float     currentTime;
                float     duration;
                uint32_t  pad;
            };

            auto now = GetTickCountMilliseconds();
            float currentPlaybackTime = static_cast<float>(now - instance.startTime) / 1000;

            CBData cb = {
                mediaAspect,
                targetAspect,
                static_cast<uint32_t>(instance.scaleMode),

                instance.fadeParams.fadeInSeconds,
                {
                    instance.fadeParams.color[0],
                    instance.fadeParams.color[1],
                    instance.fadeParams.color[2],
                    instance.fadeParams.color[3]
                },
                instance.fadeParams.fadeOutSeconds,
                currentPlaybackTime,
                instance.duration,
                0
            };

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(ctx->Map(g_resources.videoCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                memcpy(mapped.pData, &cb, sizeof(cb));
                ctx->Unmap(g_resources.videoCB.Get(), 0);
            }

            ID3D11Buffer* cbs[] = { g_resources.videoCB.Get() };
            ctx->VSSetConstantBuffers(0, 1, cbs);
            ctx->PSSetConstantBuffers(0, 1, cbs);
        }

        ctx->Draw(6, 0);
        
        ID3D11ShaderResourceView* nullSrv[1] = { nullptr };
        ctx->PSSetShaderResources(0, 1, nullSrv);
        
        states.Restore(ctx);
    }
}

HRESULT __stdcall HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
{
    try {
        ComPtr<ID3D11Device> device;
        if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device),
                reinterpret_cast<void**>(device.GetAddressOf())))) {
            return g_resources.originalPresent(swapChain, syncInterval, flags);
        }

        ComPtr<ID3D11DeviceContext> ctx;
        device->GetImmediateContext(ctx.GetAddressOf());
        if (!ctx) {
            return g_resources.originalPresent(swapChain, syncInterval, flags);
        }

        std::scoped_lock lock(g_videoMutex);

        if (!g_resources.vs || !g_resources.ps) {
            CompileShadersAndInputLayout(device.Get());
            CreateRenderStates(device.Get());
        }

        // Get menu instance for PreUI texture management
        MediaFrameworkMenu* menu = nullptr;
        if (MediaFrameworkMenu::IsPreUIEnabled()) {
            auto ui = RE::UI::GetSingleton();
            if (ui) {
                auto menuPtr = ui->GetMenu(MediaFrameworkMenu::MENU_NAME);
                if (menuPtr) {
                    menu = static_cast<MediaFrameworkMenu*>(menuPtr.get());
                }
            }
        }

        // Update ALL decoder textures
		for (auto& [id, instance] : g_mediaInstances) {
			Decoder* decoder;
			if (instance.mediaComposition.visualType == VisualType::Video) {
				decoder = GetDecoder(instance.decoderHandle);
				if (!decoder->isPlaying.load() || !decoder->isInitialized.load()) {
					continue;
				} 
			} else {
				decoder = NULL;
			}
				

			// Branch on media type
			if (instance.mediaComposition.visualType == VisualType::Image) {
				// Lazy load image texture (only loads once)
				if (!EnsureImageTexture(device.Get(), instance.mediaPath, instance)) {
					logger::warn("Failed to ensure image texture for instance {}", instance.id);
					continue;
				}
            
				// Handle PreUI stage
				if (instance.renderStage == RenderPipelineStage::PreUI) {
					if (menu) {
						if (menu->EnsureEngineTexture(instance, device.Get())) {
							menu->UpdateEngineTexture(instance, ctx.Get());
						}
					}
				}
				// MenuHook handled in LoadingMenu hooks
			} else if (decoder != NULL) {
				if (!decoder->videoHeader) {
					logger::error("Decoder does not have a video header {}", decoder->id);
					continue;
				}

				if (decoder->videoHeader && EnsureTexture(device.Get(), *decoder, instance)) {
					// Update decoder texture from shared memory
					UpdateTextureFromShared(ctx.Get(), *decoder, instance);
                
					// Find which instance is using this decoder
					auto instIt = g_mediaInstances.find(decoder->currentInstanceHandle);
					if (instIt != g_mediaInstances.end()) {
						MediaInstance& instance = instIt->second;
                    
						// Handle PreUI instance (existing code)
						if (instance.renderStage == RenderPipelineStage::PreUI) {
							if (menu) {
								if (menu->EnsureEngineTexture(instance, device.Get())) {
									menu->UpdateEngineTexture(instance, ctx.Get());
								}
							}
						}
						// Handle LoadingScreen instance
						else if (instance.renderStage == RenderPipelineStage::MenuHook) {
							// LoadingScreen texture updates handled in LoadingMenu::AdvanceMovie hook
						}
					}
				}
			}
		}

        // Render TextureSwap and Post stages via direct D3D11
        RenderVideosAtStage(RenderPipelineStage::TextureSwap, ctx.Get());
        RenderVideosAtStage(RenderPipelineStage::Post, ctx.Get());
        
        // PreUI renders via ShaderFX: UI::AdvanceMenus → menu->CacheShaderFXQuadsForRenderer_Impl
        // LoadingScreen renders via ShaderFX: LoadingMenu::AdvanceMovie → BSGFxShaderFXTarget::AppendShaderFXInfos

    } catch (const std::exception& e) {
        logger::error("Exception in HookedPresent: {}", e.what());
    } catch (...) {
        logger::error("Unknown exception in HookedPresent");
    }

    return g_resources.originalPresent(swapChain, syncInterval, flags);
}
