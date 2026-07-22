#pragma once

namespace RE
{
	class TESObjectREFR;
}

// Holds which reference is currently outlined. The actual drawing is done by
// OutlineRenderer at Present time - this is just the shared state between the
// crosshair polling (game thread) and the render hook.
//
// Nothing here touches the game's effect-shader system any more: that system only
// lets you *request* removal, and the manager culls the effect ~1-2s later while it
// keeps rendering, which is what caused the lingering outlines.
class HighlightManager
{
public:
	static HighlightManager* GetSingleton();

	// Called every frame from the crosshair poll. Passing nullptr clears it.
	void SetTarget(RE::TESObjectREFR* a_ref);

	// Resolved target, or nullptr. Safe to call from the render hook.
	RE::TESObjectREFR* GetTarget() const;

private:
	HighlightManager() = default;

	RE::ObjectRefHandle _current;
};
