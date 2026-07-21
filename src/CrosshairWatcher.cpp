#include "PCH.h"

#include "CrosshairWatcher.h"

#include "HighlightManager.h"
#include "ItemFilter.h"

namespace
{
	// Per-frame hook: PlayerCharacter::Update is vtable index 0xAD (SE/AE). We call
	// the original, then sample the crosshair pick.
	struct PlayerUpdateHook
	{
		static void thunk(RE::PlayerCharacter* a_player, float a_delta)
		{
			func(a_player, a_delta);
			CrosshairWatcher::GetSingleton()->Poll();
		}

		static inline REL::Relocation<decltype(thunk)> func;
		static constexpr std::size_t idx = 0xAD;
	};
}

CrosshairWatcher* CrosshairWatcher::GetSingleton()
{
	static CrosshairWatcher singleton;
	return &singleton;
}

void CrosshairWatcher::Install()
{
	if (_installed) {
		return;
	}

	REL::Relocation<std::uintptr_t> vtbl{ RE::PlayerCharacter::VTABLE[0] };
	PlayerUpdateHook::func = vtbl.write_vfunc(PlayerUpdateHook::idx, PlayerUpdateHook::thunk);
	_installed = true;

	logger::info("Installed PlayerCharacter::Update hook for crosshair polling.");
}

void CrosshairWatcher::Poll()
{
	RE::TESObjectREFR* ref = nullptr;
	if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
		ref = pick->target.get().get();  // ObjectRefHandle -> NiPointer -> raw
	}

	auto* manager = HighlightManager::GetSingleton();
	manager->SetTarget(ItemFilter::IsEligible(ref) ? ref : nullptr);
}
