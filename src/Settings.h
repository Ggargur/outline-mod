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

	// --- Appearance (drives the runtime-built effect shader) --------------
	// Outline / edge color, 0.0-1.0 per channel.
	float colorR = 1.0f;
	float colorG = 0.85f;
	float colorB = 0.20f;

	// Inverted-hull inflation, as a fraction of the object's size. 0.03 = 3% bigger.
	// Bigger = thicker outline band.
	float outlineThickness = 0.03f;

	// Hide the outline behind walls and other meshes by testing the scene depth.
	bool occlude = true;

	// Sense of the depth buffer. -1 = auto-detect from the camera's own projection
	// (recommended). 0 = normal (0 at the near plane), 1 = reversed. Only set this if
	// occlusion comes out inverted - outline visible ONLY through walls, or vanishing.
	std::int32_t reverseDepth = -1;

	// Which depth target to sample. -1 = pick automatically (recommended). The log
	// lists every target and whether it is sampleable, so a specific index can be
	// forced here without rebuilding.
	std::int32_t depthSource = -1;

	// Draw the outline just before the UI (hooking GRenderer::BeginDisplay on the live
	// Scaleform renderer) instead of at Present. This is what puts the outline
	// underneath the HUD and what makes the scene depth buffer usable for occlusion.
	// Turning it off falls back to drawing at Present, over the UI.
	bool preUIHook = true;

	// Draw a fixed coloured quad in the top-left instead of the outline. Isolates
	// "Present hook / render target / shaders are fine" from "geometry or matrix is
	// wrong" in a single test.
	bool debugTestQuad = false;

	// Rim tightness: higher = thinner rim hugging the silhouette.
	float edgeFalloff = 2.5f;
	// Edge width in alpha units (game-space thickness of the glow band).
	float edgeWidth = 10.0f;
	// Interior fill visibility, 0.0 (invisible interior, pure outline) .. 1.0.
	float fillAlpha = 0.10f;

	// Effect shader lifetime, in seconds, used when applying. -1 == infinite
	// (removed explicitly when the crosshair leaves).
	float shaderDuration = -1.0f;

private:
	Settings() = default;
};
