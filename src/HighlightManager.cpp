#include "PCH.h"

#include "HighlightManager.h"

HighlightManager* HighlightManager::GetSingleton()
{
	static HighlightManager singleton;
	return &singleton;
}

void HighlightManager::SetTarget(RE::TESObjectREFR* a_ref)
{
	const auto newHandle = a_ref ? a_ref->CreateRefHandle() : RE::ObjectRefHandle{};
	if (newHandle == _current) {
		return;
	}

	_current = newHandle;

	if (a_ref) {
		logger::debug("Target {:08X} ({})", a_ref->GetFormID(), a_ref->GetName());
	} else {
		logger::debug("Target cleared");
	}
}

RE::TESObjectREFR* HighlightManager::GetTarget() const
{
	auto ref = _current.get();
	return ref ? ref.get() : nullptr;
}
