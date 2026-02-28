// Hooks.cpp (MediaFramework)
#include "Hooks.h"
#include "Globals.h"
#include "Renderer.h"

constexpr size_t PRESENT_VTABLE_INDEX = 8;  // IDXGISwapChain::Present index

/** @brief Type for D3D11CreateDeviceAndSwapChain. */
using D3D11CreateDeviceAndSwapChainFn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
static D3D11CreateDeviceAndSwapChainFn g_originalCreateDeviceAndSwapChain = nullptr;

/** @brief Hooked D3D11CreateDeviceAndSwapChain to install Present hook. */
static HRESULT WINAPI HookedCreateDeviceAndSwapChain(
	IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE driverType, HMODULE software, UINT flags,
	const D3D_FEATURE_LEVEL* pFeatureLevels, UINT featureLevels, UINT sdkVersion,
	const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc, IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext)
{
	HRESULT hr = g_originalCreateDeviceAndSwapChain(pAdapter, driverType, software, flags, pFeatureLevels, featureLevels, sdkVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);
	if (FAILED(hr)) {
		return hr;
	}

	if (ppSwapChain && *ppSwapChain && !g_resources.presentPtr) {
		void** vtbl = *reinterpret_cast<void***>(*ppSwapChain);
		void* presentAddr = vtbl[PRESENT_VTABLE_INDEX];
		if (presentAddr) {
			if (MH_CreateHook(presentAddr, HookedPresent, reinterpret_cast<void**>(&g_resources.originalPresent)) == MH_OK) {
				if (MH_EnableHook(presentAddr) == MH_OK) {
					g_resources.presentPtr = presentAddr;
					logger::info("MediaFramework: Present hooked");
#ifdef _DEBUG
					logger::debug("Present hook installed at address: {}", presentAddr);
#endif
				} else {
					logger::error("MediaFramework: MH_EnableHook(Present) failed");
				}
			} else {
				logger::error("MediaFramework: MH_CreateHook(Present) failed");
			}
		}
	}

	return hr;
}

/** @brief Installs the D3D hook. */
bool InstallD3DHook()
{
	if (MH_Initialize() != MH_OK) {
		logger::error("MinHook init failed");
		return false;
	}

	HMODULE d3d11Module = GetModuleHandleA("d3d11.dll");
	if (!d3d11Module) {
		d3d11Module = LoadLibraryA("d3d11.dll");
	}
	if (!d3d11Module) {
		logger::error("Failed to load d3d11.dll");
		return false;
	}

	void* target = reinterpret_cast<void*>(GetProcAddress(d3d11Module, "D3D11CreateDeviceAndSwapChain"));
	if (!target) {
		logger::error("Failed to find D3D11CreateDeviceAndSwapChain");
		return false;
	}

	if (MH_CreateHook(target, HookedCreateDeviceAndSwapChain, reinterpret_cast<void**>(&g_originalCreateDeviceAndSwapChain)) != MH_OK) {
		logger::error("MH_CreateHook(CreateDeviceAndSwapChain) failed");
		return false;
	}

	if (MH_EnableHook(target) != MH_OK) {
		logger::error("MH_EnableHook(CreateDeviceAndSwapChain) failed");
		return false;
	}

	logger::info("MediaFramework: Hooked D3D11CreateDeviceAndSwapChain");
	return true;
}
 
 
 
