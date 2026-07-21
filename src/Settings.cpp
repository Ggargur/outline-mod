#include "PCH.h"

#include "Settings.h"

#include <SimpleIni.h>

namespace
{
	constexpr auto kIniPath = L"Data/SKSE/Plugins/ItemOutline.ini";
}

Settings* Settings::GetSingleton()
{
	static Settings singleton;
	return &singleton;
}

void Settings::Load()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	const SI_Error rc = ini.LoadFile(kIniPath);
	if (rc < 0) {
		logger::info("No ItemOutline.ini found - writing defaults.");
	}

	// GetBoolValue / GetLongValue / GetDoubleValue seed the in-memory ini with the
	// default when a key is absent, so SaveFile() below produces a full template.
	highlightInventoryItems = ini.GetBoolValue("Filter", "bHighlightInventoryItems", highlightInventoryItems);
	highlightFlora = ini.GetBoolValue("Filter", "bHighlightFlora", highlightFlora);
	highlightOreVeins = ini.GetBoolValue("Filter", "bHighlightOreVeins", highlightOreVeins);
	onlyLootable = ini.GetBoolValue("Filter", "bOnlyLootable", onlyLootable);
	minItemValue = static_cast<std::int32_t>(ini.GetLongValue("Filter", "iMinItemValue", minItemValue));

	colorR = static_cast<float>(ini.GetDoubleValue("Highlight", "fColorR", colorR));
	colorG = static_cast<float>(ini.GetDoubleValue("Highlight", "fColorG", colorG));
	colorB = static_cast<float>(ini.GetDoubleValue("Highlight", "fColorB", colorB));
	edgeFalloff = static_cast<float>(ini.GetDoubleValue("Highlight", "fEdgeFalloff", edgeFalloff));
	edgeWidth = static_cast<float>(ini.GetDoubleValue("Highlight", "fEdgeWidth", edgeWidth));
	fillAlpha = static_cast<float>(ini.GetDoubleValue("Highlight", "fFillAlpha", fillAlpha));
	shaderDuration = static_cast<float>(ini.GetDoubleValue("Highlight", "fShaderDuration", shaderDuration));

	// Persist so the file always exists with every key documented on disk.
	ini.SetBoolValue("Filter", "bHighlightInventoryItems", highlightInventoryItems);
	ini.SetBoolValue("Filter", "bHighlightFlora", highlightFlora);
	ini.SetBoolValue("Filter", "bHighlightOreVeins", highlightOreVeins);
	ini.SetBoolValue("Filter", "bOnlyLootable", onlyLootable);
	ini.SetLongValue("Filter", "iMinItemValue", minItemValue);
	ini.SetDoubleValue("Highlight", "fColorR", colorR);
	ini.SetDoubleValue("Highlight", "fColorG", colorG);
	ini.SetDoubleValue("Highlight", "fColorB", colorB);
	ini.SetDoubleValue("Highlight", "fEdgeFalloff", edgeFalloff);
	ini.SetDoubleValue("Highlight", "fEdgeWidth", edgeWidth);
	ini.SetDoubleValue("Highlight", "fFillAlpha", fillAlpha);
	ini.SetDoubleValue("Highlight", "fShaderDuration", shaderDuration);
	ini.SaveFile(kIniPath);

	logger::info(
		"Settings: inventory={} flora={} ore={} onlyLootable={} minValue={}",
		highlightInventoryItems, highlightFlora, highlightOreVeins, onlyLootable, minItemValue);
}
