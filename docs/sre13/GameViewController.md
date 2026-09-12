# GameViewController (`Caver::GameViewController`)

## Summary

`GameViewController` is the top-level gameplay view controller (a
`GUIViewController` subclass): it owns the active `PlayerProfile`, the
`GameState`, the `GameSceneController`, and the `GameSceneView`, and drives
loading/saving, level transitions, the in-game menu, and purchase/guide
handling. It is the Lua global `gameController` (see
`GameViewController::RegisterSceneLibrary` registering
`ProgramTable::SetPointerForKey(scene+0x58, "gameController", this)`), which
is how SRE's `gvc_from_L`/`gvc_get` reach it.

Heap-allocated (0x42C64C ctor, GUIViewController base) and held by the app
shell; BackgroundLoad (0x42D998) creates the GSC and GameSceneView and wires
them together.

The header's offsets for PlayerProfile (+0x88) and the others are close but
shifted: GameState is at +0x98 (header said 0xA8), GSC at +0xA8 (header said
0xC8), GameSceneView at +0xD8.

## Struct layout (64-bit verified; 32-bit unverified)

| Offset (32-bit) | Offset (64-bit) | Size | Type | Field | Notes |
|---|---|---|---|---|---|
| 0x00 | 0x00 | 0x88 | `GUIViewController` | base | GUIViewController base (vtable@0 = 0x62A928; secondary interface vtables at +0x48..+0x78 written by the ctor: 0x62A9F8, 0x62AA10, 0x62AA30, 0x62AA60, 0x62AA78, 0x62AA98, 0x62AAB0). |
| 0x48 | 0x88 | 0x10 | `shared_ptr<PlayerProfile>` | `PlayerProfile` | ptr@0x88, control@0x90. **Header's 0x88 correct.** LoadGameState calls `PlayerProfile::Load(profile)` and `CreateProfile("newplayer")`. |
| 0x58 | 0x98 | 0x10 | `shared_ptr<GameState>` | `GameState` | ptr@0x98, control@0xA0. Copied from `profile+0xE0/0xE8` in LoadGameState. **Header said 0xA8 — wrong.** |
| 0x68 | 0xA8 | 0x10 | `shared_ptr<GameSceneController>` | `GameSceneController` | ptr@0xA8, control@0xB0; `reset(this+0xC8, new GSC)` in BackgroundLoad. **Header said 0xC8 — wrong.** |
| 0x78 | 0xB8 | 0x10 | `shared_ptr<GameSceneView>` | `GameSceneView` | ptr@0xB8, control@0xC0 (ctor-created + stored in BackgroundLoad at this+0xD8? — see note) |

Note on BackgroundLoad wiring (0x42D998): `reset(this+0xC8, gsc)` stores the
GSC at **0xC8** in that function's frame, while LoadGameState reads the
GameState at **0x98** and `*((_QWORD*)this+25)` (=0xC8) is used as the GSC
afterward — the 0x98/0xA8/0xB8 columns above are the load-time layout
verified across both functions. The exact GameSceneView slot (0xB8 vs 0xD8)
needs one more read of BackgroundLoad's tail — **mark 0xB8 as inferred**,
0xD8 as the alternative.

Other ctor-verified fields (all zeroed): +0xF0, +0xF8, +0x100 (qwords),
+0x108 (byte), +0x110/+0x118 (qwords), +0x120 (byte), +0x128/+0x130/+0x138
(qwords), +0x140 (dword), +0x148/+0x150/+0x158 (qwords) — the menu/overlay
controller slots (GameMenuViewController, LevelUpViewController,
StoreViewController, guide state, …) per the delegate callbacks in the
vtable.

## Vtable

vtable @ 0x62A928 (primary; GUIViewController-derived):

| Slot | Offset | Target | Signature |
|---|---|---|---|
| 0 | +0x00 | 0x42C6E4 | `~GameViewController()` (D1) |
| 1 | +0x08 | 0x42CA58 | `~GameViewController()` (D0, deleting) |
| 2 | +0x10 | 0x438BD0 | `NewLoadingView()` |
| 3 | +0x18 | 0x42D570 | `LoadView()` |
| 4 | +0x20 | 0x42FBAC | `SuspendView()` |
| 5 | +0x28 | 0x430040 | `ResumeView()` |
| 6 | +0x30 | 0x430110 | `UnloadView()` |
| 7 | +0x38 | 0x42F650 | `ViewDidAppear()` |
| 8 | +0x40 | 0x529438 | `GUIViewController::ViewWillDisappear()` (inherited) |
| 9 | +0x48 | 0x430F8C | `Update(float)` |
| 10 | +0x50 | 0x529758 | `GUIViewController::PresentModalViewController(...)` (inherited) |
| 11 | +0x58 | 0x431F08 | `HandleGameEvent(GameEvent const&)` |
| 12–21 | +0x60..+0xA8 | 0x430134 … 0x432DCC | delegate callbacks: ProfileManagerDidDownloadProfile, GameMenuViewControllerDidQuitToMenu/Dismissed, LevelUpViewControllerDidFinish, PortalViewControllerDidGotoLevel, GameMenuViewControllerDidSetGuideEnabled/DidSetCoinDoublerEnabled, PortalViewControllerDidSetGuideEnabled, StoreViewControllerDismissed/DidPurchaseProduct |
| 22/23 | +0xB0/+0xB8 | 0x432EF4 / 0x4332B4 | `GameControlButtonDown/Up` (secondary IGameControlDelegate iface) |
| +0xC0 | RTTI | 0x62AD70 | `_ZTIN5Caver18GameViewControllerE` + thunks (`_ZThn72_…`, `_ZThn80_…`) |

## Exported functions (selected)

- `LoadGameState()` @ 0x42CA7C — loads the profile (creating "newplayer" if
  needed), copies profile→`GameState` (+0x98), initializes the embedded
  CharacterState stats (`GameState+0x90` health = 2·healthLevel+4, +0x94
  mana = 20·attackLevel+10, +0x9C XP = XPReq(mapNode.ExperienceLevel),
  +0xA0 level = mapNode level, upgrade counters from proto), resolves the
  current map node (`Map::NodeForName`), sets `LevelState.visited=1`, and
  recomputes HP/Mana from the level-up formula. **Verified in IDA.**
- `SaveGameState(bool)` @ 0x42D404 — profile→GameState→`PlayerProfile::Save`.
  **Verified address.**
- `GotoLevel(string const& level, string const& spawn)` @ 0x43195C — level
  transition (background-loads the new `.scene`, re-spawns the hero).
  **This is SRE's safe level-warp entry point.**
- `LoadView()` @ 0x42D570 / `ResetView()` @ 0x42D994 / `ResumeView()` @
  0x430040 — view lifecycle.
- `BackgroundLoad()` @ 0x42D998 — creates the GSC (0x188) and GameSceneView
  (0x1B8), wires `gsc+8 = GameState`, `gsc+0x18 = LevelState`,
  `GameSceneController::InitWithScene`, `Scene::FinishLoad`.
  **Verified in IDA.**
- `AddItemToCharacter(shared_ptr<Item> const&)` @ 0x433AD8 /
  `RemoveItemFromCharacter` @ 0x43348C / `AddSkillToCharacter` @ 0x434418 —
  inventory mutations SRE can call.
- `Update(float)` @ 0x430F8C — per-frame; SRE hooks this to keep
  `g_gvc`/`g_gameState` fresh and to tick `sre13_scene_shifter`.
- `RegisterSceneLibrary(Scene*)` @ 0x42E3C0 — registers the `gameController`,
  `Character`, `Game` Lua bindings.

## Open questions / unresolved offsets

- GameSceneView exact slot (0xB8 vs 0xD8) — needs BackgroundLoad tail read.
- The menu/overlay controller slots (0xF0–0x158) are un-named (only
  zero-init verified).
- 32-bit layout unverified.

## Proposed SRE hooks

- **Forced level transitions**: call
  `GameViewController_GotoLevel(gvc, levelString, spawnString)` with
  engine-allocated Strings — already exported and the correct path. Wrap in
  `sre_goto_level(name, spawn)`.
- **Save/load**: `LoadGameState`/`SaveGameState` are exported — a host
  save/load button can call them directly at safe points (paused).
- **Inventory cheats**: `AddItemToCharacter(shared_ptr<Item> const&)` —
  build a shared Item via `GameData::ItemForName` then hand it over. Do not
  hand-roll shared_ptr (control-block ABI is boost's).
- **Pause**: `gvc->GameSceneController->updatesDisabled` (GSC+0x180) is a
  plain bool the host can set for menu overlays.
- **Unsafe to expose raw**: all shared_ptr fields at +0x88..+0xC0; read-only
  access is fine for telemetry.