#pragma once

namespace RE
{
	class TESObjectREFR;
}

// Decides whether a reference under the crosshair should be highlighted, based
// on its base-form type and the user's Settings. Pure/read-only.
namespace ItemFilter
{
	bool IsEligible(RE::TESObjectREFR* a_ref);
}
