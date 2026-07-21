#include "PCH.h"

#include "ItemFilter.h"

#include "Settings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace
{
	bool ContainsInsensitive(std::string_view a_haystack, std::string_view a_needle)
	{
		const auto it = std::search(
			a_haystack.begin(), a_haystack.end(),
			a_needle.begin(), a_needle.end(),
			[](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) ==
				       std::tolower(static_cast<unsigned char>(b));
			});
		return it != a_haystack.end();
	}

	// Inventory-style pickups. A dropped item on the ground is a TESObjectREFR
	// whose base form is one of these, so this list also covers loose loot.
	bool IsInventoryType(RE::FormType a_type)
	{
		switch (a_type) {
		case RE::FormType::Weapon:
		case RE::FormType::Armor:
		case RE::FormType::Ammo:
		case RE::FormType::Book:
		case RE::FormType::Scroll:
		case RE::FormType::Ingredient:
		case RE::FormType::AlchemyItem:  // potions & food
		case RE::FormType::Misc:         // clutter, gold, gems, ingots...
		case RE::FormType::SoulGem:
		case RE::FormType::KeyMaster:    // keys
		case RE::FormType::Apparatus:
			return true;
		default:
			return false;
		}
	}

	// Heuristic ore-vein detection: mineable veins are activators, and vanilla /
	// most mods tag the base activator with a keyword whose editor ID mentions
	// ore/vein/mining. This is opt-in (bHighlightOreVeins) precisely because
	// activator detection is fuzzy and mod-dependent.
	bool LooksLikeOreVein(RE::TESBoundObject* a_base)
	{
		auto* keywords = a_base->As<RE::BGSKeywordForm>();
		if (!keywords) {
			return false;
		}

		static constexpr std::array needles{ "vein"sv, "ore"sv, "mine"sv };

		bool found = false;
		keywords->ForEachKeyword([&](const RE::BGSKeyword* a_kw) {
			if (a_kw) {
				if (const char* id = a_kw->GetFormEditorID(); id && *id) {
					const std::string_view idView{ id };
					for (const auto needle : needles) {
						if (ContainsInsensitive(idView, needle)) {
							found = true;
							return RE::BSContainer::ForEachResult::kStop;
						}
					}
				}
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return found;
	}
}

namespace ItemFilter
{
	bool IsEligible(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return false;
		}

		const auto* settings = Settings::GetSingleton();

		if (settings->onlyLootable) {
			if (a_ref->IsDisabled() || a_ref->IsDeleted() || a_ref->IsMarkedForDeletion()) {
				return false;
			}
		}

		auto* base = a_ref->GetObjectReference();  // TESBoundObject*
		if (!base) {
			return false;
		}

		const auto baseType = base->GetFormType();

		bool matched = false;

		if (settings->highlightInventoryItems && IsInventoryType(baseType)) {
			matched = true;
		} else if (settings->highlightFlora &&
		           (baseType == RE::FormType::Flora || baseType == RE::FormType::Tree)) {
			matched = true;
		} else if (settings->highlightOreVeins &&
		           baseType == RE::FormType::Activator && LooksLikeOreVein(base)) {
			matched = true;
		}

		if (!matched) {
			return false;
		}

		// Optional minimum-value gate for inventory items.
		if (settings->minItemValue > 0) {
			if (auto* valueForm = base->As<RE::TESValueForm>()) {
				if (valueForm->value < settings->minItemValue) {
					return false;
				}
			}
		}

		return true;
	}
}
