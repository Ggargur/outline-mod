#pragma once

#include <d3d11.h>

#include <cstdint>

namespace RE
{
	class BSGeometry;
	class NiPoint3;
	class TESObjectREFR;
}

// Draws the outline as an "inverted hull": the target's own geometry, re-issued
// with our shaders, inflated slightly and rasterized with FRONT faces culled, so
// only the back faces show - which appear as a band around the silhouette.
//
// Runs from GRenderer::BeginDisplay - the first Scaleform draw of the frame, after the
// game's and Community Shaders' post-processing but before the UI - so the outline sits
// under the HUD and the scene depth buffer is still available to occlude against.
// IDXGISwapChain::Present is hooked too, as a frame delimiter and as the fallback for
// frames where no menu rendered. Every D3D state it touches is saved and restored so it
// cannot disturb CS.
class OutlineRenderer
{
public:
	static OutlineRenderer* GetSingleton();

	// Patch IDXGISwapChain::Present. Safe to call more than once. Always installed:
	// it delimits the frame and is the fallback path when the pre-UI hook is absent.
	void InstallHook();

	// Patch GRenderer::BeginDisplay on the live Scaleform renderer, which runs just
	// before each movie is drawn. Lets the outline sit under the HUD, and catches the
	// frame at a point where the scene depth buffer still holds this frame's scene.
	// The vtable comes from a live object, so no version-specific address is needed.
	void InstallPreUIHook();

	// First Scaleform draw of the frame: this is the "under the UI" path.
	void OnPreUIDraw();

	// Present: draws only if the pre-UI path did not run this frame, then resets the
	// per-frame guard.
	void OnFrameEnd();

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
		float depthParams[4];  // x: depth sign, y: occlusion on/off
	};

	// The actual per-frame render, shared by both entry points.
	void Draw();

	bool EnsureResources(ID3D11Device* a_device);
	bool EnsureStencilBuffer();

	// Works out whether the scene depth buffer has 0 or 1 at the near plane, by
	// projecting two points that share a screen position at different distances
	// through the very matrix the game rendered the scene with.
	void ResolveDepthSense(const Matrix4& a_viewProj, const RE::NiPoint3& a_worldPos);

	// Finds a depth target that is actually readable from a shader: kPOST_ZPREPASS_COPY
	// is the one the rest of the ecosystem samples, with kMAIN as a last resort.
	ID3D11ShaderResourceView* PickDepthSRV(void* a_rendererData, float& a_scaleX, float& a_scaleY);
	void DrawTarget(RE::TESObjectREFR* a_ref);
	void DrawGeometry(RE::BSGeometry* a_geometry, const Matrix4& a_viewProj, float a_inflate);
	void DrawDebugQuad();

	// --- D3D objects (created once) ---
	ID3D11Device* _device{ nullptr };
	ID3D11DeviceContext* _context{ nullptr };
	ID3D11VertexShader* _vs{ nullptr };
	ID3D11PixelShader* _ps{ nullptr };
	ID3D11InputLayout* _inputLayout{ nullptr };
	ID3D11Buffer* _cbuffer{ nullptr };
	ID3D11RasterizerState* _rasterCullFront{ nullptr };
	ID3D11RasterizerState* _rasterCullBack{ nullptr };
	ID3D11BlendState* _blendOpaque{ nullptr };
	ID3D11BlendState* _blendNoColorWrite{ nullptr };

	// Our own stencil buffer. The game's depth buffer is not reliable at Present
	// time (it left the hull unmasked, drawing a solid blob), so the silhouette mask
	// is built here instead, fully under our control.
	ID3D11Texture2D* _stencilTexture{ nullptr };
	ID3D11DepthStencilView* _stencilView{ nullptr };
	ID3D11DepthStencilState* _stencilWrite{ nullptr };   // pass 1: mark the silhouette
	ID3D11DepthStencilState* _stencilTestOutside{ nullptr };  // pass 2: draw only outside it
	std::uint32_t _stencilWidth{ 0 };
	std::uint32_t _stencilHeight{ 0 };

	// Debug-only quad (validates the hook/pipeline independently of geometry+matrices)
	ID3D11Buffer* _debugQuadVB{ nullptr };

	bool _hookInstalled{ false };
	bool _preUIHookInstalled{ false };
	bool _resourcesReady{ false };
	bool _resourceInitFailed{ false };
	bool _loggedFirstDraw{ false };
	bool _loggedDepthSources{ false };
	bool _depthSourceResolved{ false };
	bool _depthAvailable{ false };

	// Set once the Scaleform hook has drawn this frame, so the Present fallback does
	// not draw the outline a second time.
	bool _uiDrawnThisFrame{ false };
	bool _loggedDrawPath{ false };

	// +1 if the depth buffer has 0 at the near plane, -1 if reversed, 0 = indeterminate
	// (which disables the occlusion test). Recomputed every frame.
	float _depthSign{ 0.0f };
	bool _loggedDepthSense{ false };

	float _depthScaleX{ 1.0f };
	float _depthScaleY{ 1.0f };
	int _depthSourceIndex{ -1 };  // resolved once; -1 = not chosen yet
	std::uint32_t _frameCounter{ 0 };
};
