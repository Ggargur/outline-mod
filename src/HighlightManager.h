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

	// Build the edge effect shader at runtime (no ESP required) from the values in
	// Settings. Returns false if the form could not be created -> disable feature.
	bool Init();

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
