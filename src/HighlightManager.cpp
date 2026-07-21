#include "PCH.h"

#include "HighlightManager.h"

#include "Settings.h"

#include <algorithm>

namespace
{
	// The EFSH record lives in ItemOutline.esp. The local form ID below MUST match
	// the FormID of the effect shader you create in the Creation Kit / xEdit.
	// (For an ESL-flagged plugin the last three hex digits are what matter; the
	// game/LookupForm resolves the load-order prefix automatically.)
	constexpr RE::FormID kShaderLocalFormID = 0x800;
	constexpr auto kPluginName = "ItemOutline.esp"sv;

	std::uint8_t ToByte(float a_channel)
	{
		return static_cast<std::uint8_t>(std::clamp(a_channel, 0.0f, 1.0f) * 255.0f + 0.5f);
	}
}

HighlightManager* HighlightManager::GetSingleton()
{
	static HighlightManager singleton;
	return &singleton;
}

bool HighlightManager::LoadForms()
{
	auto* handler = RE::TESDataHandler::GetSingleton();
	if (!handler) {
		return false;
	}

	_shader = handler->LookupForm<RE::TESEffectShader>(kShaderLocalFormID, kPluginName);
	if (!_shader) {
		logger::error("Effect shader {:X} not found in {}.", kShaderLocalFormID, kPluginName);
		return false;
	}

	// Optional runtime tint. Writing to the shared form means every application
	// uses this color; done once here so the crosshair path stays cheap.
	const auto* settings = Settings::GetSingleton();
	if (settings->overrideColor) {
		_shader->data.edgeColor.red = ToByte(settings->colorR);
		_shader->data.edgeColor.green = ToByte(settings->colorG);
		_shader->data.edgeColor.blue = ToByte(settings->colorB);
	}

	logger::info("Loaded effect shader {:08X}.", _shader->GetFormID());
	return true;
}

void HighlightManager::SetTarget(RE::TESObjectREFR* a_ref)
{
	const auto newHandle = a_ref ? a_ref->CreateRefHandle() : RE::ObjectRefHandle{};

	// Same target as last frame's event -> nothing to do.
	if (newHandle == _current) {
		return;
	}

	Clear();

	if (a_ref) {
		ApplyShader(a_ref);
		_current = newHandle;
	}
}

void HighlightManager::Clear()
{
	if (auto ref = _current.get()) {
		StopShaderOn(ref.get());
	}
	_current = RE::ObjectRefHandle{};
}

void HighlightManager::ApplyShader(RE::TESObjectREFR* a_ref)
{
	if (!_shader || !a_ref) {
		return;
	}
	const auto duration = Settings::GetSingleton()->shaderDuration;
	a_ref->ApplyEffectShader(_shader, duration);
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

	processLists->ForEachMagicTempEffect([&](RE::BSTempEffect& a_tempEffect) {
		if (auto* shaderEffect = skyrim_cast<RE::ShaderReferenceEffect*>(&a_tempEffect)) {
			if (shaderEffect->effectData == _shader && shaderEffect->target == handle) {
				shaderEffect->finished = true;  // flags it for removal next update
			}
		}
		return RE::BSContainer::ForEachResult::kContinue;
	});
}
