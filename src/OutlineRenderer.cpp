#include "PCH.h"

#include "OutlineRenderer.h"

#include "HighlightManager.h"
#include "Settings.h"

#include <d3dcompiler.h>

#include <cstring>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace
{
	// Flat-color hull. Position only: we inflate by scaling the geometry about its
	// own centre on the CPU (baked into worldViewProj), so no normals are needed and
	// the input layout stays trivial - Skyrim packs normals in a format that would
	// otherwise have to be decoded per mesh type.
	constexpr auto kShaderSource = R"(
cbuffer OutlineCB : register(b0)
{
    // row_major so the matrix is used exactly as we upload it - HLSL packs
    // constant-buffer matrices column-major by default, which would silently
    // transpose ours and put the hull somewhere off-screen.
    row_major float4x4 gWorldViewProj;
    float4             gColor;
};

struct VSIn  { float3 pos : POSITION; };
struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(VSIn input)
{
    VSOut o;
    o.pos = mul(float4(input.pos, 1.0f), gWorldViewProj);
    return o;
}

float4 PSMain(VSOut input) : SV_Target
{
    return gColor;
}
)";

	using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
	PresentFn g_originalPresent = nullptr;

	HRESULT WINAPI HookedPresent(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags)
	{
		OutlineRenderer::GetSingleton()->OnPresent();
		return g_originalPresent(a_swapChain, a_syncInterval, a_flags);
	}

	// 4x4 multiply, row-vector convention (v * M), matching Skyrim's worldToCam.
	void MatMul(const float a_a[4][4], const float a_b[4][4], float a_out[4][4])
	{
		for (int r = 0; r < 4; ++r) {
			for (int c = 0; c < 4; ++c) {
				a_out[r][c] = a_a[r][0] * a_b[0][c] +
				              a_a[r][1] * a_b[1][c] +
				              a_a[r][2] * a_b[2][c] +
				              a_a[r][3] * a_b[3][c];
			}
		}
	}
}

OutlineRenderer* OutlineRenderer::GetSingleton()
{
	static OutlineRenderer singleton;
	return &singleton;
}

void OutlineRenderer::InstallHook()
{
	if (_hookInstalled) {
		return;
	}

	auto* window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
	if (!window || !window->swapChain) {
		logger::error("No swap chain available - cannot install Present hook.");
		return;
	}

	auto* swapChain = reinterpret_cast<IDXGISwapChain*>(window->swapChain);

	// IDXGISwapChain vtable: 0-2 IUnknown, 3-6 IDXGIObject, 7 GetDevice, 8 Present.
	void** vtable = *reinterpret_cast<void***>(swapChain);
	constexpr std::size_t kPresentIndex = 8;

	DWORD oldProtect = 0;
	if (!VirtualProtect(&vtable[kPresentIndex], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
		logger::error("VirtualProtect failed for Present vtable entry.");
		return;
	}

	g_originalPresent = reinterpret_cast<PresentFn>(vtable[kPresentIndex]);
	vtable[kPresentIndex] = reinterpret_cast<void*>(&HookedPresent);

	VirtualProtect(&vtable[kPresentIndex], sizeof(void*), oldProtect, &oldProtect);

	_hookInstalled = true;
	logger::info("Present hook installed (original at {}).", reinterpret_cast<void*>(g_originalPresent));
}

bool OutlineRenderer::EnsureResources(ID3D11Device* a_device)
{
	if (_resourcesReady) {
		return true;
	}
	if (_resourceInitFailed) {
		return false;  // don't spam retries every frame
	}

	_device = a_device;

	// --- Compile shaders --------------------------------------------------
	ID3DBlob* vsBlob = nullptr;
	ID3DBlob* psBlob = nullptr;
	ID3DBlob* errBlob = nullptr;

	const UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;

	HRESULT hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), "ItemOutline", nullptr,
		nullptr, "VSMain", "vs_5_0", flags, 0, &vsBlob, &errBlob);
	if (FAILED(hr)) {
		logger::error("VS compile failed (0x{:08X}): {}", static_cast<std::uint32_t>(hr),
			errBlob ? static_cast<const char*>(errBlob->GetBufferPointer()) : "no message");
		if (errBlob) errBlob->Release();
		_resourceInitFailed = true;
		return false;
	}
	if (errBlob) { errBlob->Release(); errBlob = nullptr; }

	hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), "ItemOutline", nullptr,
		nullptr, "PSMain", "ps_5_0", flags, 0, &psBlob, &errBlob);
	if (FAILED(hr)) {
		logger::error("PS compile failed (0x{:08X}): {}", static_cast<std::uint32_t>(hr),
			errBlob ? static_cast<const char*>(errBlob->GetBufferPointer()) : "no message");
		if (errBlob) errBlob->Release();
		vsBlob->Release();
		_resourceInitFailed = true;
		return false;
	}
	if (errBlob) { errBlob->Release(); errBlob = nullptr; }

	_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &_vs);
	_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &_ps);

	// --- Input layout: POSITION only, stride comes from the mesh's VertexDesc ----
	const D3D11_INPUT_ELEMENT_DESC layout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	hr = _device->CreateInputLayout(layout, 1, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &_inputLayout);
	if (FAILED(hr)) {
		logger::error("CreateInputLayout failed (0x{:08X}).", static_cast<std::uint32_t>(hr));
	}

	vsBlob->Release();
	psBlob->Release();

	// --- Constant buffer --------------------------------------------------
	D3D11_BUFFER_DESC cbd{};
	cbd.ByteWidth = sizeof(ConstantBuffer);
	cbd.Usage = D3D11_USAGE_DYNAMIC;
	cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	_device->CreateBuffer(&cbd, nullptr, &_cbuffer);

	// --- States -----------------------------------------------------------
	// Cull FRONT: this is what turns an inflated copy into an outline band. The
	// game's material system cannot express this, which is why the hull has to be
	// drawn by us rather than attached to the scene graph.
	D3D11_RASTERIZER_DESC rd{};
	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_FRONT;
	rd.FrontCounterClockwise = FALSE;
	rd.DepthClipEnable = TRUE;
	_device->CreateRasterizerState(&rd, &_rasterCullFront);

	// Depth test on (so walls occlude the outline), depth write off (so we never
	// corrupt the depth buffer other passes may still read).
	D3D11_DEPTH_STENCIL_DESC dsd{};
	dsd.DepthEnable = TRUE;
	dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	dsd.StencilEnable = FALSE;
	_device->CreateDepthStencilState(&dsd, &_depthTestNoWrite);

	D3D11_BLEND_DESC bd{};
	bd.RenderTarget[0].BlendEnable = FALSE;
	bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	_device->CreateBlendState(&bd, &_blendOpaque);

	_resourcesReady = _vs && _ps && _inputLayout && _cbuffer &&
	                  _rasterCullFront && _depthTestNoWrite && _blendOpaque;
	if (!_resourcesReady) {
		logger::error("Failed to create one or more D3D resources; outline disabled.");
		_resourceInitFailed = true;
	} else {
		logger::info("D3D resources created.");
	}
	return _resourcesReady;
}

void OutlineRenderer::OnPresent()
{
	++_frameCounter;

	auto* renderer = RE::BSGraphics::Renderer::GetRendererData();
	if (!renderer || !renderer->forwarder || !renderer->context) {
		return;
	}

	auto* device = reinterpret_cast<ID3D11Device*>(renderer->forwarder);
	_context = reinterpret_cast<ID3D11DeviceContext*>(renderer->context);

	if (!EnsureResources(device)) {
		return;
	}

	auto* target = HighlightManager::GetSingleton()->GetTarget();
	const bool debugQuad = Settings::GetSingleton()->debugTestQuad;
	if (!target && !debugQuad) {
		return;
	}

	// ---- Save everything we are about to clobber, so Community Shaders (and the
	// game) see the pipeline exactly as they left it. ----
	ID3D11RenderTargetView* oldRTV = nullptr;
	ID3D11DepthStencilView* oldDSV = nullptr;
	_context->OMGetRenderTargets(1, &oldRTV, &oldDSV);

	ID3D11BlendState* oldBlend = nullptr;
	FLOAT oldBlendFactor[4]{};
	UINT oldSampleMask = 0;
	_context->OMGetBlendState(&oldBlend, oldBlendFactor, &oldSampleMask);

	ID3D11DepthStencilState* oldDSS = nullptr;
	UINT oldStencilRef = 0;
	_context->OMGetDepthStencilState(&oldDSS, &oldStencilRef);

	ID3D11RasterizerState* oldRS = nullptr;
	_context->RSGetState(&oldRS);

	ID3D11InputLayout* oldLayout = nullptr;
	_context->IAGetInputLayout(&oldLayout);

	D3D11_PRIMITIVE_TOPOLOGY oldTopology{};
	_context->IAGetPrimitiveTopology(&oldTopology);

	ID3D11VertexShader* oldVS = nullptr;
	ID3D11PixelShader* oldPS = nullptr;
	_context->VSGetShader(&oldVS, nullptr, nullptr);
	_context->PSGetShader(&oldPS, nullptr, nullptr);

	ID3D11Buffer* oldVSCB = nullptr;
	_context->VSGetConstantBuffers(0, 1, &oldVSCB);

	// ---- Bind our pipeline ----
	// Draw into the final framebuffer (post-processing already ran), testing against
	// the main depth buffer through a READ-ONLY view so we never write depth.
	auto& frameBuffer = renderer->renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];
	auto& mainDepth = renderer->depthStencils[RE::RENDER_TARGET_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

	auto* rtv = reinterpret_cast<ID3D11RenderTargetView*>(frameBuffer.RTV);
	auto* dsvReadOnly = reinterpret_cast<ID3D11DepthStencilView*>(mainDepth.readOnlyViews[0]);

	if (rtv) {
		_context->OMSetRenderTargets(1, &rtv, dsvReadOnly);
		_context->OMSetBlendState(_blendOpaque, nullptr, 0xFFFFFFFF);
		_context->OMSetDepthStencilState(_depthTestNoWrite, 0);
		_context->RSSetState(_rasterCullFront);
		_context->IASetInputLayout(_inputLayout);
		_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		_context->VSSetShader(_vs, nullptr, 0);
		_context->PSSetShader(_ps, nullptr, 0);
		_context->VSSetConstantBuffers(0, 1, &_cbuffer);

		if (debugQuad) {
			DrawDebugQuad();
		} else {
			DrawTarget(target);
		}
	} else if (_frameCounter % 600 == 0) {
		logger::error("Framebuffer RTV is null - nothing to draw into.");
	}

	// ---- Restore ----
	_context->OMSetRenderTargets(1, &oldRTV, oldDSV);
	_context->OMSetBlendState(oldBlend, oldBlendFactor, oldSampleMask);
	_context->OMSetDepthStencilState(oldDSS, oldStencilRef);
	_context->RSSetState(oldRS);
	_context->IASetInputLayout(oldLayout);
	_context->IASetPrimitiveTopology(oldTopology);
	_context->VSSetShader(oldVS, nullptr, 0);
	_context->PSSetShader(oldPS, nullptr, 0);
	_context->VSSetConstantBuffers(0, 1, &oldVSCB);

	// OMGetRenderTargets & friends AddRef their outputs.
	if (oldRTV) oldRTV->Release();
	if (oldDSV) oldDSV->Release();
	if (oldBlend) oldBlend->Release();
	if (oldDSS) oldDSS->Release();
	if (oldRS) oldRS->Release();
	if (oldLayout) oldLayout->Release();
	if (oldVS) oldVS->Release();
	if (oldPS) oldPS->Release();
	if (oldVSCB) oldVSCB->Release();
}

void OutlineRenderer::DrawTarget(RE::TESObjectREFR* a_ref)
{
	auto* root = a_ref ? a_ref->Get3D() : nullptr;
	if (!root) {
		return;
	}

	auto* camera = RE::Main::WorldRootCamera();
	if (!camera) {
		return;
	}

	// worldToCam is row-major applied to a column vector; transpose it so we can
	// keep the whole chain in row-vector convention (matching the HLSL above).
	const auto& w2c = camera->GetRuntimeData().worldToCam;
	Matrix4 viewProj{};
	for (int r = 0; r < 4; ++r) {
		for (int c = 0; c < 4; ++c) {
			viewProj.m[r][c] = w2c[c][r];
		}
	}

	if (!_loggedFirstDraw) {
		_loggedFirstDraw = true;
		logger::info("First outline draw. viewProj[0]=({:.3f},{:.3f},{:.3f},{:.3f}) [3]=({:.3f},{:.3f},{:.3f},{:.3f})",
			viewProj.m[0][0], viewProj.m[0][1], viewProj.m[0][2], viewProj.m[0][3],
			viewProj.m[3][0], viewProj.m[3][1], viewProj.m[3][2], viewProj.m[3][3]);
	}

	int drawn = 0;
	// Iterative DFS over the ref's 3D; BSGeometry nodes are the leaves we draw.
	std::vector<RE::NiAVObject*> stack{ root };
	while (!stack.empty()) {
		auto* obj = stack.back();
		stack.pop_back();
		if (!obj) {
			continue;
		}
		if (auto* geometry = obj->AsGeometry()) {
			DrawGeometry(geometry, viewProj);
			++drawn;
			continue;
		}
		if (auto* node = obj->AsNode()) {
			for (auto& child : node->GetChildren()) {
				stack.push_back(child.get());
			}
		}
	}

	if (drawn == 0 && _frameCounter % 600 == 0) {
		logger::warn("Target {:08X} has no BSGeometry to draw.", a_ref->GetFormID());
	}
}

void OutlineRenderer::DrawGeometry(RE::BSGeometry* a_geometry, const Matrix4& a_viewProj)
{
	auto* triShape = a_geometry->AsTriShape();
	if (!triShape) {
		return;  // skip particles/lines/etc.
	}

	auto* rendererData = a_geometry->GetGeometryRuntimeData().rendererData;
	if (!rendererData || !rendererData->vertexBuffer || !rendererData->indexBuffer) {
		return;
	}

	const auto& counts = triShape->GetTrishapeRuntimeData();
	const UINT indexCount = static_cast<UINT>(counts.triangleCount) * 3u;
	if (indexCount == 0) {
		return;
	}

	auto& vertexDesc = a_geometry->GetGeometryRuntimeData().vertexDesc;
	const UINT stride = vertexDesc.GetSize();
	const UINT positionOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::Attribute::VA_POSITION);
	if (stride == 0) {
		return;
	}

	// World transform, converted to row-vector convention, with the inflation baked
	// into the scale. Scaling about the node origin is a coarser hull than pushing
	// along normals, but it needs POSITION only - no per-mesh normal decoding.
	const auto& world = a_geometry->world;
	const float scale = world.scale * (1.0f + Settings::GetSingleton()->outlineThickness);
	const auto& rot = world.rotate;

	Matrix4 worldMatrix{};
	for (int c = 0; c < 3; ++c) {
		worldMatrix.m[0][c] = rot.entry[c][0] * scale;
		worldMatrix.m[1][c] = rot.entry[c][1] * scale;
		worldMatrix.m[2][c] = rot.entry[c][2] * scale;
	}
	worldMatrix.m[3][0] = world.translate.x;
	worldMatrix.m[3][1] = world.translate.y;
	worldMatrix.m[3][2] = world.translate.z;
	worldMatrix.m[3][3] = 1.0f;

	ConstantBuffer cb{};
	MatMul(worldMatrix.m, a_viewProj.m, cb.worldViewProj);
	const auto& s = *Settings::GetSingleton();
	cb.color[0] = s.colorR;
	cb.color[1] = s.colorG;
	cb.color[2] = s.colorB;
	cb.color[3] = 1.0f;

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(_context->Map(_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		return;
	}
	std::memcpy(mapped.pData, &cb, sizeof(cb));
	_context->Unmap(_cbuffer, 0);

	auto* vertexBuffer = reinterpret_cast<ID3D11Buffer*>(rendererData->vertexBuffer);
	auto* indexBuffer = reinterpret_cast<ID3D11Buffer*>(rendererData->indexBuffer);

	_context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &positionOffset);
	_context->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R16_UINT, 0);
	_context->DrawIndexed(indexCount, 0, 0);
}

// Validates the hook + render target + shaders without depending on the camera
// matrix or mesh buffers: if this shows up but the outline doesn't, the bug is in
// the geometry/matrix path, not the plumbing.
void OutlineRenderer::DrawDebugQuad()
{
	if (!_debugQuadVB) {
		// Clip-space triangle strip in the top-left corner.
		const float verts[] = {
			-0.9f, 0.9f, 0.5f,
			-0.6f, 0.9f, 0.5f,
			-0.9f, 0.6f, 0.5f,
			-0.6f, 0.6f, 0.5f,
		};
		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = sizeof(verts);
		bd.Usage = D3D11_USAGE_IMMUTABLE;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA init{};
		init.pSysMem = verts;
		_device->CreateBuffer(&bd, &init, &_debugQuadVB);
		logger::info("Debug quad buffer created.");
	}
	if (!_debugQuadVB) {
		return;
	}

	// Identity matrix -> the vertices above are already in clip space.
	ConstantBuffer cb{};
	cb.worldViewProj[0][0] = 1.0f;
	cb.worldViewProj[1][1] = 1.0f;
	cb.worldViewProj[2][2] = 1.0f;
	cb.worldViewProj[3][3] = 1.0f;
	const auto& s = *Settings::GetSingleton();
	cb.color[0] = s.colorR; cb.color[1] = s.colorG; cb.color[2] = s.colorB; cb.color[3] = 1.0f;

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (SUCCEEDED(_context->Map(_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		std::memcpy(mapped.pData, &cb, sizeof(cb));
		_context->Unmap(_cbuffer, 0);
	}

	UINT stride = sizeof(float) * 3;
	UINT offset = 0;
	_context->IASetVertexBuffers(0, 1, &_debugQuadVB, &stride, &offset);
	_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	// No depth/cull interference for the debug quad.
	_context->RSSetState(nullptr);
	_context->OMSetDepthStencilState(nullptr, 0);
	_context->Draw(4, 0);

	if (!_loggedFirstDraw) {
		_loggedFirstDraw = true;
		logger::info("Debug quad drawn.");
	}
}
