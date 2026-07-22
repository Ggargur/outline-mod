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
    // x: +1 for normal depth (0 near), -1 for reversed depth (1 near).
    // y: 0 disables the occlusion test entirely.
    // zw: pixel-coordinate scale, because dynamic resolution can make the depth
    //     texture a different size from the back buffer.
    float4             gDepthParams;
};

// The scene depth buffer. We sample it by pixel coordinate rather than binding it
// as a depth-stencil view, because our own stencil occupies the DSV slot.
Texture2D<float> gSceneDepth : register(t0);

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
    if (gDepthParams.y > 0.5f)
    {
        int2 coord = int2(input.pos.xy * gDepthParams.zw);
        float sceneZ = gSceneDepth.Load(int3(coord, 0));
        // Discard where the scene is in front of this fragment, so walls and other
        // meshes occlude the outline instead of it drawing through everything.
        if ((sceneZ - input.pos.z) * gDepthParams.x < 0.0f)
        {
            discard;
        }
    }
    return gColor;
}
)";

	using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
	PresentFn g_originalPresent = nullptr;

	HRESULT WINAPI HookedPresent(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags)
	{
		// When the pre-UI hook is active it does the drawing instead, so the outline
		// ends up beneath the HUD rather than painted over it.
		if (!Settings::GetSingleton()->preUIHook) {
			OutlineRenderer::GetSingleton()->OnPresent();
		}
		return g_originalPresent(a_swapChain, a_syncInterval, a_flags);
	}

	// GRenderer::BeginFrame - runs immediately before Scaleform draws the UI.
	using BeginFrameFn = void(__fastcall*)(void*);
	BeginFrameFn g_originalBeginFrame = nullptr;

	bool g_loggedBeginFrameFired = false;

	void __fastcall HookedBeginFrame(void* a_self)
	{
		if (!g_loggedBeginFrameFired) {
			g_loggedBeginFrameFired = true;
			logger::info("Pre-UI hook FIRED - this vtable slot is called per frame.");
		}
		OutlineRenderer::GetSingleton()->OnPresent();
		g_originalBeginFrame(a_self);
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

void OutlineRenderer::InstallPreUIHook()
{
	if (_preUIHookInstalled) {
		return;
	}

	auto* manager = RE::BSScaleformManager::GetSingleton();
	if (!manager || !manager->renderer) {
		logger::error("Scaleform renderer not available - pre-UI hook not installed.");
		return;
	}

	// BSScaleformRenderer is not declared in CommonLibSSE-NG, so its exact vtable
	// layout is unverified - slot 4 (GRenderer::BeginFrame) turned out never to be
	// called. The index is configurable so candidates can be probed from the INI
	// instead of one rebuild per guess.
	void** vtable = *reinterpret_cast<void***>(manager->renderer);
	const std::size_t kBeginFrameIndex =
		static_cast<std::size_t>(Settings::GetSingleton()->preUIVtableIndex);

	// Dump the neighbourhood so we can see which slots hold plausible code pointers.
	logger::info("Scaleform renderer vtable @ {}:", static_cast<void*>(vtable));
	for (std::size_t i = 0; i < 20; ++i) {
		logger::info("  [{:02X}] {}", i, vtable[i]);
	}

	DWORD oldProtect = 0;
	if (!VirtualProtect(&vtable[kBeginFrameIndex], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
		logger::error("VirtualProtect failed for GRenderer::BeginFrame.");
		return;
	}

	g_originalBeginFrame = reinterpret_cast<BeginFrameFn>(vtable[kBeginFrameIndex]);
	vtable[kBeginFrameIndex] = reinterpret_cast<void*>(&HookedBeginFrame);

	VirtualProtect(&vtable[kBeginFrameIndex], sizeof(void*), oldProtect, &oldProtect);

	_preUIHookInstalled = true;
	logger::info("Pre-UI hook installed (GRenderer::BeginFrame original at {}).",
		reinterpret_cast<void*>(g_originalBeginFrame));
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

	rd.CullMode = D3D11_CULL_BACK;  // mask pass draws the object normally
	_device->CreateRasterizerState(&rd, &_rasterCullBack);

	// Pass 1 (mask): write stencil=1 wherever the object is, no colour.
	D3D11_DEPTH_STENCIL_DESC maskDesc{};
	maskDesc.DepthEnable = FALSE;
	maskDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	maskDesc.StencilEnable = TRUE;
	maskDesc.StencilReadMask = 0xFF;
	maskDesc.StencilWriteMask = 0xFF;
	maskDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	maskDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
	maskDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
	maskDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
	maskDesc.BackFace = maskDesc.FrontFace;
	_device->CreateDepthStencilState(&maskDesc, &_stencilWrite);

	// Pass 2 (outline): draw only where the mask is NOT set - i.e. the band that the
	// inflated hull adds outside the object's silhouette.
	D3D11_DEPTH_STENCIL_DESC outlineDesc = maskDesc;
	outlineDesc.StencilWriteMask = 0x00;
	outlineDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	outlineDesc.FrontFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;
	outlineDesc.BackFace = outlineDesc.FrontFace;
	_device->CreateDepthStencilState(&outlineDesc, &_stencilTestOutside);

	D3D11_BLEND_DESC bd{};
	bd.RenderTarget[0].BlendEnable = FALSE;
	bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	_device->CreateBlendState(&bd, &_blendOpaque);

	bd.RenderTarget[0].RenderTargetWriteMask = 0;  // mask pass writes stencil only
	_device->CreateBlendState(&bd, &_blendNoColorWrite);

	_resourcesReady = _vs && _ps && _inputLayout && _cbuffer &&
	                  _rasterCullFront && _rasterCullBack &&
	                  _stencilWrite && _stencilTestOutside &&
	                  _blendOpaque && _blendNoColorWrite;
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

	if (!EnsureStencilBuffer()) {
		return;
	}

	// ---- Save everything we are about to clobber, so Community Shaders (and the
	// game) see the pipeline exactly as they left it. Community Shaders renders
	// through a multi-target G-buffer, so saving only slot 0 (as this did at first)
	// left its other targets unbound and produced blocky shadow artefacts. ----
	constexpr UINT kMaxRTVs = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
	ID3D11RenderTargetView* oldRTVs[kMaxRTVs]{};
	ID3D11DepthStencilView* oldDSV = nullptr;
	_context->OMGetRenderTargets(kMaxRTVs, oldRTVs, &oldDSV);

	ID3D11Buffer* oldVB = nullptr;
	UINT oldVBStride = 0;
	UINT oldVBOffset = 0;
	_context->IAGetVertexBuffers(0, 1, &oldVB, &oldVBStride, &oldVBOffset);

	ID3D11Buffer* oldIB = nullptr;
	DXGI_FORMAT oldIBFormat{};
	UINT oldIBOffset = 0;
	_context->IAGetIndexBuffer(&oldIB, &oldIBFormat, &oldIBOffset);

	ID3D11Buffer* oldPSCB = nullptr;
	_context->PSGetConstantBuffers(0, 1, &oldPSCB);

	ID3D11ShaderResourceView* oldPSSRV = nullptr;
	_context->PSGetShaderResources(0, 1, &oldPSSRV);

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
	auto* rtv = reinterpret_cast<ID3D11RenderTargetView*>(frameBuffer.RTV);

	if (rtv) {
		_context->ClearDepthStencilView(_stencilView, D3D11_CLEAR_STENCIL, 1.0f, 0);
		_context->OMSetRenderTargets(1, &rtv, _stencilView);
		_context->IASetInputLayout(_inputLayout);
		_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		_context->VSSetShader(_vs, nullptr, 0);
		_context->PSSetShader(_ps, nullptr, 0);
		_context->VSSetConstantBuffers(0, 1, &_cbuffer);
		// The pixel shader reads gColor from b0 too. Binding it only to the vertex
		// stage left the PS sampling an unbound buffer, i.e. zeros - which is why
		// the first version drew the hull solid black instead of the chosen colour.
		_context->PSSetConstantBuffers(0, 1, &_cbuffer);

		// Scene depth as an SRV, so the pixel shader can reject occluded fragments.
		auto* depthSRV = PickDepthSRV(renderer, _depthScaleX, _depthScaleY);
		_context->PSSetShaderResources(0, 1, &depthSRV);
		_depthAvailable = depthSRV != nullptr;
		if (!_depthAvailable && _frameCounter % 600 == 0) {
			logger::warn("No sampleable depth target - outline will not be occluded.");
		}

		if (debugQuad) {
			_context->OMSetBlendState(_blendOpaque, nullptr, 0xFFFFFFFF);
			DrawDebugQuad();
		} else {
			DrawTarget(target);
		}
	} else if (_frameCounter % 600 == 0) {
		logger::error("Framebuffer RTV is null - nothing to draw into.");
	}

	// ---- Restore ----
	_context->OMSetRenderTargets(kMaxRTVs, oldRTVs, oldDSV);
	_context->OMSetBlendState(oldBlend, oldBlendFactor, oldSampleMask);
	_context->OMSetDepthStencilState(oldDSS, oldStencilRef);
	_context->RSSetState(oldRS);
	_context->IASetInputLayout(oldLayout);
	_context->IASetPrimitiveTopology(oldTopology);
	_context->IASetVertexBuffers(0, 1, &oldVB, &oldVBStride, &oldVBOffset);
	_context->IASetIndexBuffer(oldIB, oldIBFormat, oldIBOffset);
	_context->VSSetShader(oldVS, nullptr, 0);
	_context->PSSetShader(oldPS, nullptr, 0);
	_context->VSSetConstantBuffers(0, 1, &oldVSCB);
	_context->PSSetConstantBuffers(0, 1, &oldPSCB);
	_context->PSSetShaderResources(0, 1, &oldPSSRV);

	// OMGetRenderTargets & friends AddRef their outputs.
	for (auto* view : oldRTVs) {
		if (view) view->Release();
	}
	if (oldDSV) oldDSV->Release();
	if (oldBlend) oldBlend->Release();
	if (oldDSS) oldDSS->Release();
	if (oldRS) oldRS->Release();
	if (oldLayout) oldLayout->Release();
	if (oldVB) oldVB->Release();
	if (oldIB) oldIB->Release();
	if (oldVS) oldVS->Release();
	if (oldPS) oldPS->Release();
	if (oldVSCB) oldVSCB->Release();
	if (oldPSCB) oldPSCB->Release();
	if (oldPSSRV) oldPSSRV->Release();
}

ID3D11ShaderResourceView* OutlineRenderer::PickDepthSRV(void* a_rendererData, float& a_scaleX, float& a_scaleY)
{
	auto* renderer = static_cast<RE::BSGraphics::RendererData*>(a_rendererData);
	a_scaleX = 1.0f;
	a_scaleY = 1.0f;

	// Audit every depth target once: which ones are sampleable, and at what size.
	// kMAIN turned out to have no SRV at all (it is a write-only DSV), so this tells
	// us definitively which target can back the occlusion test.
	if (!_loggedDepthSources) {
		_loggedDepthSources = true;

		// Control sample: the framebuffer target is known-good (we successfully draw
		// into it). If its size also reads 0x0 then we are reading the renderer
		// struct at the wrong offset and the depth audit below means nothing.
		{
			auto& fb = renderer->renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER];
			std::uint32_t w = 0, h = 0;
			if (auto* tex = reinterpret_cast<ID3D11Texture2D*>(fb.texture)) {
				D3D11_TEXTURE2D_DESC td{};
				tex->GetDesc(&td);
				w = td.Width;
				h = td.Height;
			}
			logger::info("CONTROL framebuffer: rtv={} srv={} tex={} size={}x{}",
				fb.RTV ? "yes" : "no", fb.SRV ? "yes" : "no", fb.texture ? "yes" : "no", w, h);
		}

		logger::info("--- depth targets (index: srv, size) ---");
		for (int i = 0; i < RE::RENDER_TARGET_DEPTHSTENCIL::kTOTAL; ++i) {
			auto& entry = renderer->depthStencils[i];
			auto* srv = reinterpret_cast<ID3D11ShaderResourceView*>(entry.depthSRV);
			auto* tex = reinterpret_cast<ID3D11Texture2D*>(entry.texture);
			std::uint32_t w = 0, h = 0;
			if (tex) {
				D3D11_TEXTURE2D_DESC td{};
				tex->GetDesc(&td);
				w = td.Width;
				h = td.Height;
			}
			logger::info("  [{}] srv={} size={}x{}", i, srv ? "yes" : "no", w, h);
		}
	}

	const int configured = Settings::GetSingleton()->depthSource;

	// Resolve once. Keying this off "_depthSourceIndex < 0" meant that when nothing
	// suitable was found the search - and its log line - repeated every single frame.
	if (!_depthSourceResolved) {
		_depthSourceResolved = true;
		if (configured >= 0 && configured < RE::RENDER_TARGET_DEPTHSTENCIL::kTOTAL) {
			_depthSourceIndex = configured;
		} else {
			// Preference order: the copies exist specifically to be sampled.
			const int candidates[] = {
				RE::RENDER_TARGET_DEPTHSTENCIL::kPOST_ZPREPASS_COPY,
				RE::RENDER_TARGET_DEPTHSTENCIL::kMAIN_COPY,
				RE::RENDER_TARGET_DEPTHSTENCIL::kPOST_WATER_COPY,
				RE::RENDER_TARGET_DEPTHSTENCIL::kMAIN,
			};
			for (int candidate : candidates) {
				if (renderer->depthStencils[candidate].depthSRV) {
					_depthSourceIndex = candidate;
					break;
				}
			}
		}
		logger::info("Using depth source index {}.", _depthSourceIndex);
	}

	if (_depthSourceIndex < 0) {
		return nullptr;
	}

	auto& chosen = renderer->depthStencils[_depthSourceIndex];
	auto* srv = reinterpret_cast<ID3D11ShaderResourceView*>(chosen.depthSRV);
	if (!srv) {
		return nullptr;
	}

	// Skyrim's dynamic resolution means the depth texture can be larger than the
	// area actually rendered, so pixel coordinates must be rescaled before sampling.
	if (auto* tex = reinterpret_cast<ID3D11Texture2D*>(chosen.texture)) {
		D3D11_TEXTURE2D_DESC td{};
		tex->GetDesc(&td);
		if (td.Width && td.Height && _stencilWidth && _stencilHeight) {
			a_scaleX = static_cast<float>(td.Width) / static_cast<float>(_stencilWidth);
			a_scaleY = static_cast<float>(td.Height) / static_cast<float>(_stencilHeight);
		}
	}

	return srv;
}

// Our own stencil target, sized to the back buffer. Recreated if the resolution
// changes.
bool OutlineRenderer::EnsureStencilBuffer()
{
	const auto screen = RE::BSGraphics::Renderer::GetScreenSize();
	const auto width = static_cast<std::uint32_t>(screen.width);
	const auto height = static_cast<std::uint32_t>(screen.height);
	if (width == 0 || height == 0) {
		return false;
	}

	if (_stencilView && width == _stencilWidth && height == _stencilHeight) {
		return true;
	}

	if (_stencilView) { _stencilView->Release(); _stencilView = nullptr; }
	if (_stencilTexture) { _stencilTexture->Release(); _stencilTexture = nullptr; }

	D3D11_TEXTURE2D_DESC td{};
	td.Width = width;
	td.Height = height;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	if (FAILED(_device->CreateTexture2D(&td, nullptr, &_stencilTexture))) {
		logger::error("Failed to create stencil texture {}x{}.", width, height);
		return false;
	}
	if (FAILED(_device->CreateDepthStencilView(_stencilTexture, nullptr, &_stencilView))) {
		logger::error("Failed to create stencil view.");
		return false;
	}

	_stencilWidth = width;
	_stencilHeight = height;
	logger::info("Stencil buffer created ({}x{}).", width, height);
	return true;
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

	// Collect the drawable leaves once; both passes iterate the same list.
	std::vector<RE::BSGeometry*> geometries;
	std::vector<RE::NiAVObject*> stack{ root };
	while (!stack.empty()) {
		auto* obj = stack.back();
		stack.pop_back();
		if (!obj) {
			continue;
		}
		if (auto* geometry = obj->AsGeometry()) {
			geometries.push_back(geometry);
			continue;
		}
		if (auto* node = obj->AsNode()) {
			for (auto& child : node->GetChildren()) {
				stack.push_back(child.get());
			}
		}
	}

	if (geometries.empty()) {
		if (_frameCounter % 600 == 0) {
			logger::warn("Target {:08X} has no BSGeometry to draw.", a_ref->GetFormID());
		}
		return;
	}

	const float thickness = Settings::GetSingleton()->outlineThickness;

	// Pass 1 - mask: draw the object at its real size, colour writes off, stamping
	// stencil=1 over its silhouette.
	_context->OMSetBlendState(_blendNoColorWrite, nullptr, 0xFFFFFFFF);
	_context->OMSetDepthStencilState(_stencilWrite, 1);
	_context->RSSetState(_rasterCullBack);
	for (auto* geometry : geometries) {
		DrawGeometry(geometry, viewProj, 0.0f);
	}

	// Pass 2 - outline: draw the inflated hull, but only where the mask is absent,
	// so what survives is exactly the band the inflation added around the silhouette.
	_context->OMSetBlendState(_blendOpaque, nullptr, 0xFFFFFFFF);
	_context->OMSetDepthStencilState(_stencilTestOutside, 1);
	_context->RSSetState(_rasterCullFront);
	for (auto* geometry : geometries) {
		DrawGeometry(geometry, viewProj, thickness);
	}
}

void OutlineRenderer::DrawGeometry(RE::BSGeometry* a_geometry, const Matrix4& a_viewProj, float a_inflate)
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
	const float scale = world.scale * (1.0f + a_inflate);
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
	cb.depthParams[0] = s.reverseDepth ? -1.0f : 1.0f;
	// The mask pass writes no colour, so occlusion only matters for the outline pass.
	cb.depthParams[1] = (s.occlude && _depthAvailable && a_inflate > 0.0f) ? 1.0f : 0.0f;
	cb.depthParams[2] = _depthScaleX;
	cb.depthParams[3] = _depthScaleY;

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
