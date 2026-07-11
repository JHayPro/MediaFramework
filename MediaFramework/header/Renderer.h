// Renderer.h (MediaFramework)
#pragma once
#include "PCH.h"
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

// Replace DrawVideoQuad signature with:
void RenderVideosAtStage(RenderPipelineStage stage, ID3D11DeviceContext* ctx);

// Keep existing HookedPresent signature
HRESULT __stdcall HookedPresent(IDXGISwapChain* const swapChain, const UINT syncInterval, const UINT flags);