#include "PCH.h"

#include "HighlightManager.h"

#include "Settings.h"

#include <algorithm>

namespace
{
	// Fill texture shipped with the mod (Data/Textures/ItemOutline/edge_gradient.dds).
	// Paths are relative to Data\Textures\.
	constexpr auto kFillTexture = "ItemOutline\\edge_gradient.dds"sv;

	// Direct3D9 blend/op constants (the enums are only forward-declared in CommonLib).
	constexpr auto kBlendSrcAlpha = static_cast<RE::D3DBLEND>(5);   // D3DBLEND_SRCALPHA
	constexpr auto kBlendOne = static_cast<RE::D3DBLEND>(2);        // D3DBLEND_ONE
	constexpr auto kBlendOpAdd = static_cast<RE::D3DBLENDOP>(1);    // D3DBLENDOP_ADD

	std::uint8_t ToByte(float a_channel)
	{
		return static_cast<std::uint8_t>(std::clamp(a_channel, 0.0f, 1.0f) * 255.0f + 0.5f);
	}

	RE::Color MakeColor(const Settings& a_s, std::uint8_t a_alpha)
	{
		RE::Color c;
		c.red = ToByte(a_s.colorR);
		c.green = ToByte(a_s.colorG);
		c.blue = ToByte(a_s.colorB);
		c.alpha = a_alpha;
		return c;
	}
}

HighlightManager* HighlightManager::GetSingleton()
{
	static HighlightManager singleton;
	return &singleton;
}

bool HighlightManager::Init()
{
	auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESEffectShader>();
	if (!factory) {
		logger::error("No effect-shader form factory available.");
		return false;
	}

	_shader = static_cast<RE::TESEffectShader*>(factory->Create());
	if (!_shader) {
		logger::error("Failed to create runtime effect shader form.");
		return false;
	}

	const auto& s = *Settings::GetSingleton();
	auto& d = _shader->data;

	const auto edge = MakeColor(s, 255);
	const auto fill = MakeColor(s, ToByte(s.fillAlpha));

	// Membrane (fill) shader: additive so it glows over the item without darkening it.
	d.membraneShaderSourceBlendMode = kBlendSrcAlpha;
	d.membraneShaderDestBlendMode = kBlendOne;
	d.membraneShaderBlendOperation = kBlendOpAdd;
	d.membraneShaderZTestFunction = RE::D3DCMPFUNC::kAlways;

	// Interior fill: keep it subtle (driven by fillAlpha), instant on, no fade.
	d.fillTextureEffectColorKey1 = fill;
	d.fillTextureEffectColorKey2 = fill;
	d.fillTextureEffectColorKey3 = fill;
	d.fillTextureEffectColorKeyScaleTimeColorKey1Scale = 1.0f;
	d.fillTextureEffectColorKeyScaleTimeColorKey2Scale = 1.0f;
	d.fillTextureEffectColorKeyScaleTimeColorKey3Scale = 1.0f;
	d.fillTextureEffectFullAlphaRatio = std::clamp(s.fillAlpha, 0.0f, 1.0f);
	d.fillTextureEffectPersistentAlphaRatio = std::clamp(s.fillAlpha, 0.0f, 1.0f);
	// No fade: the effect must snap on/off with the crosshair, not linger.
	d.fillTextureEffectAlphaFadeInTime = 0.0f;
	d.fillTextureEffectFullAlphaTime = 0.0f;
	d.fillTextureEffectAlphaFadeOutTime = 0.0f;
	d.textureCountU = 1.0f;
	d.textureCountV = 1.0f;
	d.fillTextureEffectTextureScaleU = 1.0f;
	d.fillTextureEffectTextureScaleV = 1.0f;

	// Edge (rim glow): the actual "outline".
	d.edgeColor = edge;
	d.edgeEffectColor = edge;
	d.edgeEffectFallOff = s.edgeFalloff;
	d.edgeWidthAlphaUnits = s.edgeWidth;
	d.edgeEffectFullAlphaRatio = 1.0f;
	d.edgeEffectPersistentAlphaRatio = 1.0f;
	d.edgeEffectFullAlphaTime = 1.0f;
	d.colorScale = 1.0f;

	// No particles.
	d.flags.set(RE::EffectShaderData::Flags::kDisableParticleShader);

	_shader->fillTexture.textureName = kFillTexture;

	logger::info("Runtime effect shader created (form {:08X}).", _shader->GetFormID());
	return true;
}

void HighlightManager::SetTarget(RE::TESObjectREFR* a_ref)
{
	const auto newHandle = a_ref ? a_ref->CreateRefHandle() : RE::ObjectRefHandle{};

	// Same target as last event -> nothing to do.
	if (newHandle == _current) {
		return;
	}

	Clear();

	if (a_ref) {
		ApplyShader(a_ref);
		_current = newHandle;
		logger::info("APPLY {:08X} ({})", a_ref->GetFormID(), a_ref->GetName());
	}
}

void HighlightManager::Clear()
{
	if (auto ref = _current.get()) {
		logger::info("CLEAR {:08X} ({})", ref->GetFormID(), ref->GetName());
		StopShaderOn(ref.get());
	}
	_current = RE::ObjectRefHandle{};
}

void HighlightManager::ApplyShader(RE::TESObjectREFR* a_ref)
{
	if (!_shader || !a_ref) {
		return;
	}
	a_ref->ApplyEffectShader(_shader, Settings::GetSingleton()->shaderDuration);
}

// End every instance of OUR shader currently playing on a_ref. Scanning the
// active temp-effect list (rather than caching the ShaderReferenceEffect*) is
// safe against the effect being torn down on its own (e.g. cell unload).
void HighlightManager::StopShaderOn(RE::TESObjectREFR* a_ref)
{
	if (!a_ref) {
		return;
	}
	auto* processLists = RE::ProcessLists::GetSingleton();
	if (!processLists) {
		return;
	}

	const auto handle = a_ref->CreateRefHandle();

	int matched = 0;
	int shaderEffectsSeen = 0;
	processLists->ForEachMagicTempEffect([&](RE::BSTempEffect* a_tempEffect) {
		if (auto* shaderEffect = skyrim_cast<RE::ShaderReferenceEffect*>(a_tempEffect)) {
			++shaderEffectsSeen;
			if (shaderEffect->effectData == _shader && shaderEffect->target == handle) {
				// finished=true only *schedules* removal - the manager culls the
				// effect on a later cycle, during which it keeps rendering (that is
				// the lingering). Everything below is a plain field write (safe here,
				// unlike Detach()/vfuncs which mutate the scene graph and crash when
				// called during this locked iteration):
				//  - clear kVisible so it stops drawing this frame,
				//  - force age past lifetime so Update() returns false -> culled next tick,
				//  - finished as a backstop for cleanup.
				shaderEffect->flags.reset(RE::ShaderReferenceEffect::Flag::kVisible);
				shaderEffect->lifetime = 0.0f;
				shaderEffect->age = 1.0f;
				shaderEffect->finished = true;
				++matched;
			}
		}
		return RE::BSContainer::ForEachResult::kContinue;
	});

	logger::info("StopShaderOn {:08X}: finished {}/{} shader effect(s)",
		a_ref->GetFormID(), matched, shaderEffectsSeen);
}
