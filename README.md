# ItemOutline

Highlights collectable items with an **edge glow** when the crosshair is over them,
and removes it when you look away. Built for **Skyrim Anniversary Edition (1.6.x)**.

**Community Shaders / ENB compatible by design:** it never hooks the D3D11 render
pipeline. It applies the game's native `TESEffectShader` (edge effect) to the
reference under the crosshair, so whatever lighting/post-processing stack you run
(Community Shaders, ENB, or vanilla) renders it through the normal path — no pass
ordering or render-state conflicts.

**No ESP, no Creation Kit needed:** the effect shader is built entirely in code at
runtime from `ItemOutline.ini`, using a small fill texture shipped with the mod. The
artifact is a ready-to-install mod (DLL + INI + texture).

## How it works

1. On `kDataLoaded`, read `ItemOutline.ini` and **create** the edge `TESEffectShader`
   at runtime from those values (see [`HighlightManager::Init`](src/HighlightManager.cpp)).
2. Subscribe to `SKSE::CrosshairRefEvent` (fires when the crosshair target changes).
3. For each new target, [`ItemFilter`](src/ItemFilter.cpp) checks the base form type
   against the enabled categories.
4. [`HighlightManager`](src/HighlightManager.cpp) applies the effect shader to the
   new target and stops it on the previous one (by scanning the active temp-effect
   list and setting `finished = true`).

| File | Role |
|------|------|
| [src/main.cpp](src/main.cpp) | SKSE entry point, logging, lifecycle |
| [src/CrosshairWatcher.cpp](src/CrosshairWatcher.cpp) | `CrosshairRefEvent` sink |
| [src/ItemFilter.cpp](src/ItemFilter.cpp) | Eligibility by base form type |
| [src/HighlightManager.cpp](src/HighlightManager.cpp) | Apply/remove effect shader |
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

The effect shader is created in code (see
[`HighlightManager::Init`](src/HighlightManager.cpp)) — there is **no ESP** and nothing
to author in the Creation Kit. Its look is driven by the `[Highlight]` keys in the INI
(edge color, edge fall-off/width, fill alpha). The fill texture ships with the mod at
`textures/ItemOutline/edge_gradient.dds`.

> This produces a Fresnel rim-glow around the item — the practical "outline" for an
> effect-shader approach. A crisp vector-style contour would require a screen-space
> render pass and is intentionally out of scope (see the plan).

## Installing

The CI **ItemOutline-Mod** artifact is already laid out for install. Drop its contents
into `Data/` (or install the folder as a mod in MO2/Vortex):

```
SKSE/Plugins/ItemOutline.dll
SKSE/Plugins/ItemOutline.ini
textures/ItemOutline/edge_gradient.dds
```

## Configuration

Edit `Data/SKSE/Plugins/ItemOutline.ini` — see
[data/SKSE/Plugins/ItemOutline.ini](data/SKSE/Plugins/ItemOutline.ini) for every key
(which categories to highlight, minimum item value, outline color, edge fall-off/width,
fill alpha, shader lifetime).

## Testing in-game

1. Install the mod (DLL + INI + texture) alongside SKSE + Address Library, with
   Community Shaders active.
2. Load a save; open `Documents/My Games/Skyrim Special Edition/SKSE/ItemOutline.log`
   and confirm "ItemOutline ready" and "Runtime effect shader created".
3. Point the crosshair at: a weapon/potion on the ground, an item on a table, a plant,
   and (if enabled) an ore vein → the edge glow appears **only** on those.
4. Move the crosshair away → the glow disappears immediately (no stuck effect).
5. Confirm containers/chests and NPCs/corpses are **not** highlighted.
6. Toggle Community Shaders and ENB on/off → outline still renders, no crash, no
   visual conflict. Sweep the crosshair quickly across several items to confirm
   effects don't accumulate.
7. Change the color/category settings in the INI and reconfirm.
