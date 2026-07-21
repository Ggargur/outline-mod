#pragma once

// Listens for SKSE::CrosshairRefEvent (fired whenever the object under the
// crosshair changes) and drives the HighlightManager accordingly.
class CrosshairWatcher : public RE::BSTEventSink<SKSE::CrosshairRefEvent>
{
public:
	static CrosshairWatcher* GetSingleton();

	// Idempotent: attaches this sink to the crosshair-ref event source.
	void Register();

	RE::BSEventNotifyControl ProcessEvent(
		const SKSE::CrosshairRefEvent* a_event,
		RE::BSTEventSource<SKSE::CrosshairRefEvent>* a_source) override;

private:
	CrosshairWatcher() = default;
	CrosshairWatcher(const CrosshairWatcher&) = delete;
	CrosshairWatcher(CrosshairWatcher&&) = delete;
	CrosshairWatcher& operator=(const CrosshairWatcher&) = delete;
	CrosshairWatcher& operator=(CrosshairWatcher&&) = delete;

	bool _registered{ false };
};
