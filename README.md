# ARK Turret Editor (standalone source)

An isolated ImGui-based turret settings editor for ARK: Survival Evolved, Microsoft Store/UWP build v962.2. This repository contains only the standalone editor, the source needed to build it, and its third-party dependencies. It does not include the original Past Owl project or its other paidini features.

## Build

Requirements: Windows 10/11, Visual Studio 2022 with the **Desktop development with C++** workload and Windows SDK.

Run `build.bat` from a Developer Command Prompt or double-click it. Outputs go to `dist/`; intermediate object files go to `build/`. The script builds the standalone DLL and the matching loader. No prebuilt binaries are committed.

## Runtime notes

- The scan radius is expressed in foundations; the implementation uses 400 cm (4 m) per foundation.
- Turret filters: Auto, Heavy, Tek, and Plant.
- Editable options: Range and Targeting.
- The editor learns action/menu routes from observed manual interactions and persists learned data in the game's local paidini settings directory.
- A successful local scan does not mean every remote interaction will be accepted by the game/server. Test only where you have authorization; server-side interaction validation remains authoritative.
- Latest handoff, including current known issues and test evidence: [`turret-editor-handoff.md`](turret-editor-handoff.md).

## Files

- `src/Client Manager/Modules/Misc/TurretEditor.h` — editor implementation.
- `src/TurretEditorStandalone.cpp` — minimal ImGui/DX11 host and hooks.
- `injector.cpp` — matching loader source.
- `build.bat` — isolated build script.

## Third-party code

Dear ImGui and MinHook sources are included under `src/ImGui` and `src/MinHook`; retain their respective license files. Their licenses do not grant rights to ARK or other game assets. See `THIRD_PARTY_NOTICES.md`.

## Status

The editor UI, cursor/input handling, radius scan, and nearby Range changes were observed working in game. A Targeting enum-label correction is present in the latest build but still needs in-game verification. See the handoff for limitations and detailed evidence.
