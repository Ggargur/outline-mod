# ItemOutline

Highlights collectable items with an **outline** when the crosshair is over them,
and removes it when you look away. Built for **Skyrim Anniversary Edition (1.6.x)**.

**No ESP, no Creation Kit needed:** everything is built in code at runtime from
`ItemOutline.ini`. The artifact is a ready-to-install mod (DLL + INI).

## How it works

The outline is an **inverted hull**: the target's own geometry, re-issued through our
own D3D11 shaders, inflated slightly and rasterized with front faces culled, so only
the back faces survive — a band around the silhouette. A stencil mask pass carves out
the object's interior so only the band remains.

1. On `kDataLoaded`, read `ItemOutline.ini`.
2. Hook `PlayerCharacter::Update` and **poll `CrosshairPickData` every frame** so the
   highlight tracks the reticle tightly (no lag from the game's sticky crosshair-ref event).
3. For the current target, [`ItemFilter`](src/ItemFilter.cpp) checks the base form type
   against the enabled categories.
4. [`OutlineRenderer`](src/OutlineRenderer.cpp) draws the hull once per frame, from
   **`GRenderer::BeginDisplay`** on the live Scaleform renderer — the first UI draw of
   the frame. Drawing there is what puts the outline *under* the HUD and what keeps the
   scene depth buffer available, so the outline can be **occluded** by walls and other
   meshes. `IDXGISwapChain::Present` is also hooked, as a frame delimiter and as a
   fallback that draws (over the UI) on any frame where no menu rendered.

Every D3D state the renderer touches is saved and restored, so Community Shaders and
ENB see the pipeline exactly as they left it.

| File | Role |
|------|------|
| [src/main.cpp](src/main.cpp) | SKSE entry point, logging, lifecycle |
| [src/CrosshairWatcher.cpp](src/CrosshairWatcher.cpp) | Per-frame crosshair polling (Update hook) |
| [src/ItemFilter.cpp](src/ItemFilter.cpp) | Eligibility by base form type |
| [src/HighlightManager.cpp](src/HighlightManager.cpp) | Holds the current target |
| [src/OutlineRenderer.cpp](src/OutlineRenderer.cpp) | Render hooks + the inverted-hull pass |
| [src/Settings.cpp](src/Settings.cpp) | INI parsing (SimpleIni) |

## Requirements (runtime)

- Skyrim SE **AE 1.6.x**
- [SKSE64](https://skse.silverlock.org/) (AE build)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- Community Shaders and/or ENB are **optional** — the mod works with or without them.

No ESP/plugin slot is used and no master files are required.

## Building (Windows)

Requires the **MSVC toolchain** (Visual Studio 2022 Build Tools, "Desktop C++"),
**Ninja**, **CMake ≥ 3.21**, **git**, and **[vcpkg](https://github.com/microsoft/vcpkg)**
with the `VCPKG_ROOT` environment variable set (vcpkg provides CommonLibSSE-NG's
dependencies — spdlog, rapidcsv — via the manifest `vcpkg.json`). Run the commands
from a **Developer Command Prompt / Developer PowerShell for VS** so `cl.exe` is on PATH.

```powershell
# 1. Get CommonLibSSE-NG as a submodule (SimpleIni is fetched automatically by CMake)
git submodule add https://github.com/CharmedBaryon/CommonLibSSE-NG extern/CommonLibSSE-NG
git submodule update --init --recursive

# 2. Configure + build (Ninja, single-config Release x64). vcpkg installs deps on first run.
cmake --preset windows-release
cmake --build build
```

The output is `build/ItemOutline.dll`.

> Don't have Windows at all? Push the repo to GitHub and let the included
> [CI workflow](.github/workflows/build.yml) build the DLL for you on a Windows
> runner — download it from the run's **Artifacts**.

To auto-copy the DLL into a mod staging folder during development, configure with:

```powershell
cmake --preset windows-release -DITEMOUTLINE_DEPLOY_DIR="C:/Path/To/mods/ItemOutline/SKSE/Plugins"
```

> CommonLibSSE-NG supplies `Skyrim.h`/`SKSE.h`. Editing on a machine without the
> submodule checked out will show "file not found" IntelliSense errors — those
> resolve once the submodule is present and CMake has configured.

Everything is built in code — there is **no ESP** and nothing to author in the Creation
Kit. The look is driven by the `[Highlight]` keys in the INI (outline color, thickness,
occlusion).

## Installing

The CI **ItemOutline-Mod** artifact is already laid out for install. Drop its contents
into `Data/` (or install the folder as a mod in MO2/Vortex):

```
SKSE/Plugins/ItemOutline.dll
SKSE/Plugins/ItemOutline.ini
```

## Configuration

Edit `Data/SKSE/Plugins/ItemOutline.ini` — see
[data/SKSE/Plugins/ItemOutline.ini](data/SKSE/Plugins/ItemOutline.ini) for every key
(which categories to highlight, minimum item value, outline color and thickness,
occlusion, and the render-path/debug toggles).

## Testing in-game

1. Install the mod (DLL + INI) alongside SKSE + Address Library, with Community
   Shaders active.
2. Load a save; open `Documents/My Games/Skyrim Special Edition/SKSE/ItemOutline.log`
   and confirm "ItemOutline ready", "Pre-UI hook installed", "Drawing pre-UI", the
   chosen depth-source index and the auto-detected depth sense.
3. Point the crosshair at: a weapon/potion on the ground, an item on a table, a plant,
   and (if enabled) an ore vein → the outline appears **only** on those.
4. Move the crosshair away → the outline disappears immediately.
5. Confirm containers/chests and NPCs/corpses are **not** highlighted.
6. **Under the UI:** with an item outlined, open the inventory or map — the outline must
   sit *behind* the menu panels, not over them.
7. **Occlusion:** step behind a wall or column so the item is hidden → the outline
   disappears, and returns when it is in sight again. If it shows up *only* through
   walls, the auto-detect got the depth sense backwards — set `iReverseDepth = 1`.
8. Toggle Community Shaders and ENB on/off → outline still renders, no crash, no
   shadow/G-buffer artefacts, no menu flicker.
9. Change the color/category settings in the INI and reconfirm.
