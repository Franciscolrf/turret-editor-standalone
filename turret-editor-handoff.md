# ARK Turret Editor — Engineering Handoff

## Goal

Add a standalone bulk turret-settings editor for ARK: Survival Evolved, Microsoft Store/UWP build v962.2. The user wants to scan friendly turrets within a configurable radius measured in foundations (for example, 10 or 20 foundations), filter by turret type, and apply Range and Targeting options.

One foundation is treated as 400 cm (4 m). The editor scans the local world for friendly Auto, Heavy, Tek, and Plant turrets within the selected radius.

## Project layout and build

- Turret logic and UI: `src/paidini-source/Manual Map Ark Base/Ark base/Client/Client Manager/Modules/Misc/TurretEditor.h`
- Isolated ImGui host, input and hooks: `src/paidini-source/Manual Map Ark Base/Ark base/Client/TurretEditorStandalone.cpp`
- Isolated build script: `src/paidini-source/Manual Map Ark Base/build_turret_standalone.bat`
- Use the matching `injector.exe` and `paidini.dll` from `src/paidini-source/Manual Map Ark Base/Ark base/Injector/Turret Editor Standalone/`.
- The full project depends on Past Owl, which was not included. The standalone build uses vendored ImGui and does not require Past Owl.

The latest DLL compiled successfully. SHA256: `CB6C4934B28243C5561D77542B097ADFCE051D228F01212F32FDD3A062F64CBA`. This latest build has not yet been tested in game after the most recent Targeting-label correction.

## Implemented

- Standalone ImGui editor with radius, filters for Auto / Heavy / Tek / Plant, Range and Targeting controls, Apply, Learn, Forget, and diagnostic dump.
- Radius detection and friendly-turret counting work in game.
- Cursor/input handling was corrected; the editor GUI opens and accepts input.
- Learned action indices persist in `turret_editor.ini`. Schema 3 adds separate learned radial-menu routes for Range and Targeting; schema 2 INIs still load their saved action indices.
- No warning/alarm option is exposed.
- Detailed event tracing records PRE/POST for `BPActivatedMultiUseSelection`, `ClientDoMultiUse`, `BPClientDoMultiUse`, `ServerMultiUse`, and `BPTryMultiUse`. Entries include correlated IDs, timestamps, parameters, target classes, raw parameter bytes, and Range/AI values. `BPTryMultiUse` logs its return byte and post-handler values.
- Apply replays the learned menu navigation asynchronously: it sends one navigation step, waits 350 ms on later game ticks, then sends `ServerMultiUse` and the final client event. This avoids dispatching the whole path in one frame.
- If a setting remains unchanged after one attempt and a 1.2-second replication window, the editor skips it instead of repeatedly reopening/replaying the wheel.
- The UI Targeting labels were reordered to match the observed enum: `0 All`, `1 Survivors + tames`, `2 Survivors only`, `3 Wild only`, `4 Survivors + mounted`, `5 Tamed only`. The learned numeric actions do not need to be relearned. A previously saved selection of value 3 will now display as `Wild only`; choose `Survivors only` again to select value 2.

## What the logs established

Manual turret changes follow this observed path:

`BPActivatedMultiUseSelection → ClientDoMultiUse (menu navigation) → BPClientDoMultiUse → BPActivatedMultiUseSelection → ServerMultiUse → BPTryMultiUse → ClientDoMultiUse (completion) → BPClientDoMultiUse`

The direct Apply path initially failed because it only replayed the final action. Capturing and replaying menu navigation made Range changes work on a Heavy and later on a Tek. The successful local handler traces show `BPTryMultiUse retByte=1` and the updated property value.

The Targeting mismatch was a UI-to-enum mapping error. The old UI sent enum value 3 for “Survivors only”; the game applied `AISetting=3`, which the user observed as “Only Wild Creatures.” The corrected UI now assigns “Survivors only” to enum value 2 and its learned action (index `88758` in the current INI).

In a test with four Tek turrets in a 14-foundation scan radius, only the Tek about 1.7 m away produced a local `BPTryMultiUse` accepted result. Attempts for the other Teks, about 11–18 m away, called `ServerMultiUse` but did not produce the same local handler trace or change the reflected value. This strongly suggests ARK is enforcing its normal multi-use eligibility/range rules. A client-side radius scan finds candidate actors; it does not grant server authority or bypass the game's validation. A dedicated server's internal `BPTryMultiUse` may not be observable from the client, so confirm results through replicated property changes as well.

The radial wheel appears because the replay calls `BPActivatedMultiUseSelection`, which is part of the game's own interaction flow. The current build reduces repeated attempts, but the wheel may still briefly appear for each candidate action. Removing that visual side effect without breaking the accepted game path has not been solved.

## Known limitations / open questions

- True remote bulk modification across all turrets in a large radius is not confirmed. The latest logs suggest ordinary server interaction checks reject some candidates. Do not claim that a `called=1` log means the server accepted the action; only a handler/replication result establishes success.
- The radius remains in place and Apply still iterates its candidate turrets. The game may reject turrets that are outside valid interaction conditions. A client-only module cannot force an authoritative server to accept rejected requests.
- Whether a server-side/admin-supported implementation can provide the requested large-radius behavior is not established.
- The radial wheel can still appear during an attempted change.
- The latest corrected Targeting enum labels need an in-game check.

## Test files

- Log: `%USERPROFILE%\AppData\Local\Packages\StudioWildcard.4558480580BB9_1w2mm55455e38\AC\paidini\debug.log`
- Persistence: `%USERPROFILE%\AppData\Local\Packages\StudioWildcard.4558480580BB9_1w2mm55455e38\AC\paidini\turret_editor.ini`

## Suggested next test

Use the latest DLL and its matching loader. Test one nearby Heavy or Tek first. Select a different Range and verify the property changes. Then select `Survivors only` and verify the reflected `AISetting` becomes 2 and the in-game label matches. After that, test several Teks at different distances inside the chosen foundation radius and compare `ServerMultiUse`, `BPTryMultiUse`, the return byte, and post-change properties. Keep the full `debug.log`; the current PRE/POST traces are intended to show exactly where each candidate is accepted or rejected.

Learned actions and menu routes are saved in the INI; do not delete it or relearn every option unless the UI reports missing route/action data.
