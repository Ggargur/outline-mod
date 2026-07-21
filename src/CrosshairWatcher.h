#pragma once

// Drives the highlight by polling the crosshair pick every frame (via a hook on
// PlayerCharacter::Update), so the outline tracks the reticle tightly instead of
// lagging behind the game's "sticky" crosshair-ref event.
class CrosshairWatcher
{
public:
	static CrosshairWatcher* GetSingleton();

	// Install the per-frame update hook (idempotent).
	void Install();

	// Read the current crosshair target and update the highlight. Called each
	// frame from the update hook.
	void Poll();

private:
	CrosshairWatcher() = default;
	CrosshairWatcher(const CrosshairWatcher&) = delete;
	CrosshairWatcher& operator=(const CrosshairWatcher&) = delete;

	bool _installed{ false };
};
