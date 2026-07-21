#pragma once

#include <cstdint>

namespace RE
{
	class TESObjectREFR;
	class TESEffectShader;
}

// Owns the "which reference is currently outlined" state and applies/removes the
// edge effect shader. Uses only the game's native EffectShader path, so it plays
// nicely with Community Shaders / ENB with no render-pipeline hooking.
class HighlightManager
{
public:
	static HighlightManager* GetSingleton();

	// Look up the EFSH record from ItemOutline.esp. Returns false if not found
	// (mod misinstalled) -> caller should disable the feature.
	bool LoadForms();

	// Highlight a_ref (clearing any previous target first). Passing nullptr just
	// clears the current highlight.
	void SetTarget(RE::TESObjectREFR* a_ref);

	// Remove the highlight from whatever is currently highlighted.
	void Clear();

private:
	HighlightManager() = default;

	void ApplyShader(RE::TESObjectREFR* a_ref);
	void StopShaderOn(RE::TESObjectREFR* a_ref);

	RE::TESEffectShader* _shader{ nullptr };

	// Handle (not raw pointer) to the currently highlighted ref, so we never
	// dereference a freed object across frames. Default-constructed == invalid.
	RE::ObjectRefHandle _current;
};
