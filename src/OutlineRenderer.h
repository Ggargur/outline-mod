#pragma once

#include <d3d11.h>

#include <cstdint>

namespace RE
{
	class BSGeometry;
	class TESObjectREFR;
}

// Draws the outline as an "inverted hull": the target's own geometry, re-issued
// with our shaders, inflated slightly and rasterized with FRONT faces culled, so
// only the back faces show - which appear as a band around the silhouette.
//
// Runs from an IDXGISwapChain::Present hook (i.e. after the game's and Community
// Shaders' post-processing), and saves/restores every D3D state it touches so it
// cannot disturb CS.
class OutlineRenderer
{
public:
	static OutlineRenderer* GetSingleton();

	// Patch IDXGISwapChain::Present. Safe to call more than once.
	void InstallHook();

	// Called from the Present hook, once per frame.
	void OnPresent();

private:
	OutlineRenderer() = default;
	OutlineRenderer(const OutlineRenderer&) = delete;
	OutlineRenderer& operator=(const OutlineRenderer&) = delete;

	// Row-major 4x4, laid out for HLSL (already transposed on upload).
	struct Matrix4
	{
		float m[4][4];
	};

	struct ConstantBuffer
	{
		float worldViewProj[4][4];
		float color[4];
	};

	bool EnsureResources(ID3D11Device* a_device);
	void DrawTarget(RE::TESObjectREFR* a_ref);
	void DrawGeometry(RE::BSGeometry* a_geometry, const Matrix4& a_viewProj);
	void DrawDebugQuad();

	// --- D3D objects (created once) ---
	ID3D11Device* _device{ nullptr };
	ID3D11DeviceContext* _context{ nullptr };
	ID3D11VertexShader* _vs{ nullptr };
	ID3D11PixelShader* _ps{ nullptr };
	ID3D11InputLayout* _inputLayout{ nullptr };
	ID3D11Buffer* _cbuffer{ nullptr };
	ID3D11RasterizerState* _rasterCullFront{ nullptr };
	ID3D11DepthStencilState* _depthTestNoWrite{ nullptr };
	ID3D11BlendState* _blendOpaque{ nullptr };

	// Debug-only quad (validates the hook/pipeline independently of geometry+matrices)
	ID3D11Buffer* _debugQuadVB{ nullptr };

	bool _hookInstalled{ false };
	bool _resourcesReady{ false };
	bool _resourceInitFailed{ false };
	bool _loggedFirstDraw{ false };
	std::uint32_t _frameCounter{ 0 };
};
