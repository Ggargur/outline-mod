#pragma once

#include <cstdint>

// Runtime configuration, read once at kDataLoaded from
//   Data/SKSE/Plugins/ItemOutline.ini
// Missing keys fall back to the defaults below (and the file is (re)written so
// users get a documented template on first run).
class Settings
{
public:
	static Settings* GetSingleton();

	void Load();

	// --- Filter: which categories get highlighted -------------------------
	bool highlightInventoryItems = true;  // weapons, armor, potions, ingredients, misc, gold...
	bool highlightFlora = true;           // harvestable plants / trees
	bool highlightOreVeins = false;       // mineable activators (keyword-gated, opt-in)
	bool onlyLootable = true;             // skip disabled / deleted / non-takable refs

	// Skip items whose base gold value is below this (0 = no minimum).
	std::int32_t minItemValue = 0;

	// --- Appearance -------------------------------------------------------
	// If true, tint the applied shader with the RGB below instead of the color
	// baked into the EFSH record. If false, the ESP's own edge color is used.
	bool overrideColor = false;
	float colorR = 1.0f;
	float colorG = 0.85f;
	float colorB = 0.20f;

	// Effect shader lifetime, in seconds, used when applying. -1 == infinite
	// (removed explicitly when the crosshair leaves). Kept configurable so users
	// can switch to the finite-duration/auto-expire behaviour if desired.
	float shaderDuration = -1.0f;

private:
	Settings() = default;
};
