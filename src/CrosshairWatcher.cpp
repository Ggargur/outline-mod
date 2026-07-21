#include "PCH.h"

#include "CrosshairWatcher.h"

#include "HighlightManager.h"
#include "ItemFilter.h"

CrosshairWatcher* CrosshairWatcher::GetSingleton()
{
	static CrosshairWatcher singleton;
	return &singleton;
}

void CrosshairWatcher::Register()
{
	if (_registered) {
		return;
	}
	if (auto* source = SKSE::GetCrosshairRefEventSource()) {
		source->AddEventSink(this);
		_registered = true;
		logger::info("Registered crosshair ref event sink.");
	} else {
		logger::error("Could not get crosshair ref event source.");
	}
}

RE::BSEventNotifyControl CrosshairWatcher::ProcessEvent(
	const SKSE::CrosshairRefEvent* a_event,
	RE::BSTEventSource<SKSE::CrosshairRefEvent>*)
{
	auto* manager = HighlightManager::GetSingleton();

	// crosshairRef is null when the crosshair leaves all references.
	RE::TESObjectREFR* ref = a_event ? a_event->crosshairRef.get() : nullptr;

	manager->SetTarget(ItemFilter::IsEligible(ref) ? ref : nullptr);

	return RE::BSEventNotifyControl::kContinue;
}
