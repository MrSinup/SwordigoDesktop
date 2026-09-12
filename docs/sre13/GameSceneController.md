# GameSceneController (`Caver::GameSceneController`)

## Summary

`GameSceneController` is the gameplay controller: it owns the active
`GameState` link, the embedded `CameraController`, the hero `SceneObject`,
the hero's component pointers (CharController/Entity/Health/Mana), the
`HeroEquipmentManager`, and the per-frame gameplay update (hero follow-cam,
level-up logic, target/monster facing, casting, HUD sync). It is
heap-allocated once per level by `GameViewController::BackgroundLoad`
(`operator new(0x188)`, 0x42D998) and held in `GameViewController` at +0xC8
(shared_ptr). It is the object bound to the Lua global `gameController`.

The header's layout is **significantly wrong** — it placed
`CharacterState*` at +0x10, `Scene*` at +0x20, `hero` at +0xB8, and the
component pointers at +0xC0..+0xD0. The real layout (below) comes from the
ctor (0x4253B4), `InitWithScene` (0x425624), `BackgroundLoad` (0x42D998),
`Update` (0x4265E8), `CreateHeroObjectAt` (0x425AE8), and `ApplyLevelUp`
(0x427080).

## Struct layout (64-bit verified; 32-bit unverified)

| Offset (32-bit) | Offset (64-bit) | Size | Type | Field | Notes |
|---|---|---|---|---|---|
| 0x00 | 0x00 | 0x08 | `void*` | `vtable` | = 0x62A5E8 (vtable of `IGameControlDelegate` interface, which is the primary vtable — slot 0 = GameControlButtonDown, slot 1 = GameControlButtonUp). |
| 0x04 | 0x08 | 0x08 | `GameState*` | `GameState` | shared_ptr ptr. **Header calls this `CharacterState*` — wrong.** Set by BackgroundLoad: `gsc+8 = gvc->GameState`. The GSC reads level/XP through it at `GameState+0xA0/0x9C`. |
| 0x0C | 0x10 | 0x08 | `void*` | `GameStateRef` | shared_ptr control. |
| 0x14 | 0x18 | 0x08 | `LevelState*` | `currentLevelState` | Set by BackgroundLoad: `gsc+0x18 = GameState::StateForLevelWithName(gs, currentLevelName, 1)` — **the header's `_pad1` region**. |
| 0x1C | 0x20 | 0x08 | `Scene*` | `Scene` | shared_ptr ptr, set by `InitWithScene`. **Header guessed 0x20 — correct!** |
| 0x24 | 0x28 | 0x08 | `void*` | `SceneRef` | shared_ptr control. |
| 0x2C | 0x30 | 0x08 | `void*` | `gameOverlay/UI` | Zeroed in ctor; later holds a controller with `ExperienceBar*` at +0x210 (Update: `*(*(this+0x30)+0x210)` = ExperienceBar). Possibly the GameOverlayViewController. |
| 0x38 | 0x38 | 0xA0 | `CameraController` | `cameraController` | **Embedded** (ctor runs `CameraController(this+0x38)`). Absolute `camera` ptr = 0x38+0x58 = **0x90**; `cameraOffset` = 0x3C; `lerpFactor` = 0x54; `zoom` = 0x64. |
| 0x98 | 0xD8 | 0x08 | `SceneObject*` | `hero` | Intrusive ref. **Header says 0xB8 — wrong (that's inside the CameraController).** Created by `CreateHeroObjectAt`/`AddHeroObjectToScene`. |
| 0xA0 | 0xE0 | 0x08 | `CharControllerComponent*` | `CharControllerComponent` | (Update: `*(_QWORD*)this+28`) — has input state at +0x240 (0x576). |
| 0xA8 | 0xE8 | 0x08 | `EntityComponent*` | `EntityComponent` | Facing int at +0x68 (0x104); MonsterEntityComponent collection checked against it. |
| 0xB0 | 0xF0 | 0x08 | `HealthComponent*` | `HealthComponent` | HP ints at +0x70/+0x74/+0x78, regen float at +0x8C. |
| 0xB8 | 0xF8 | 0x08 | `ManaComponent*` | `ManaComponent` | Mana ints at +0x68/+0x6C. |
| 0xC0 | 0x100 | 0x88 | `HeroEquipmentManager` | `heroEquipment` | **Embedded** (ctor runs `HeroEquipmentManager(this+0x100)`). |
| 0x108 | 0x148 | 0x04 | `float` | `heroFacing` | Camera lead direction (±1 or smoothed); used as `facing·90` x-lead in the follow-cam. |
| 0x118 | 0x158 | 0x08 | `void*` | `castingSkill` | shared_ptr<Skill> ptr while casting. |
| 0x120 | 0x160 | 0x08 | `void*` | `castingSkillRef` | control. |
| 0x128 | 0x168 | 0x04 | `float` | `castingTimer` | Counts down; on ≤0 → `FinishCasting`, release skill, restore stats. |
| 0x130 | 0x170 | 0x08 | `SceneObject*` | `targetObject` | Current monster target (Position.x at +0x80 compared against hero). |
| 0x138 | 0x178 | 0x01 | `bool` | `levelUpInProgress` | Set when a level-up event fires; blocks follow-cam & HUD. |
| 0x13C | 0x17C | 0x04 | `float` | `levelUpTimer` | 0.2 → counts down; on expiry checks `XP ≥ XPReq(level+1)` and fires the level-up event. |
| 0x140 | 0x180 | 0x01 | `bool` | `updatesDisabled` | While set, Update skips hero-follow/HUD/input sync (scene transitions, menus). |
| 0x144 | 0x184 | 0x04 | `int` | (spare) | Zeroed in ctor. Unresolved. |

Total size 0x188 (verified: `operator new(0x188)`).

## Vtable

Primary vtable @ 0x62A5E8 (IGameControlDelegate interface):

| Slot | Offset | Target | Signature |
|---|---|---|---|
| 0 | +0x00 | 0x427D70 | `GameSceneController::GameControlButtonDown(GameControlButton)` |
| 1 | +0x08 | 0x428E8C | `GameSceneController::GameControlButtonUp(GameControlButton)` |

The RTTI anchor `_ZTIN5Caver20IGameControlDelegateE` follows. The full GSC
class vtable (dtor/Update/etc.) is elsewhere in .data.rel.ro (the class
inherits GUIViewController-side interfaces); the dtor is at 0x439888 (weak).

## Exported functions

- `Update(float)` @ 0x4265E8 — the per-frame gameplay driver. **Verified in
  IDA + disasm.** Order: (1) ensure hero exists (`Scene::ObjectWithIdentifier("hero")`,
  else `AddHeroObjectToScene`); (2) monster-facing/target logic against
  `targetObject`; (3) level-up timer → `CharacterState` XP/level check
  (reads `GameState+0x9C/0xA0`!) → `ExperienceBar::UpdateExperience` +
  level-up `GameEvent` + sound; (4) hero-follow camera: computes
  `currentPos/targetPos` from hero position ± visible-area clamps, writes
  them **directly into the embedded CameraController** (+0x48..0x60 abs);
  (5) casting timer; (6) HUD sync (`CharacterState+0x90` = HP, +0x94 = Mana
  from HealthComponent/ManaComponent); (7) `CameraController::Update(this+0x38, dt)`;
  (8) scene vtable `Update`.
- `InitWithScene(shared_ptr<Scene> const&)` @ 0x425624 — stores Scene
  (+0x20), sets perspective projection (0.34907, 1.0, 50, 20000), writes the
  device-dependent camera offset (+0x3C), lerpFactor ≈1.0, zoom 0.8,
  registers the camera program library, orients BackgroundComponents toward
  the camera offset.
- `SpawnHeroAt(std::string const& id)` @ 0x425890 — spawns the hero at the
  named spawn point.
- `CreateHeroObjectAt(Vector3 const&, int, bool)` @ 0x425AE8 — builds the
  hero SceneObject + components and stores them at +0xD8/+0xE0/+0xE8/+0xF0/+0xF8.
- `AddHeroObjectToScene()` @ 0x426004.
- `UpdateTarget()` @ 0x426DAC — re-acquires `targetObject`.
- `EquipItem(shared_ptr<Item> const&)` @ 0x4262DC; `UnequipArmor()` @ 0x426F8C;
  `BeginCasting(shared_ptr<Skill> const&)` @ 0x4281FC; `CanCastSkill(shared_ptr<Skill> const&)` @ 0x429034;
  `ConsumeItem(shared_ptr<Item> const&)` @ 0x426E6C; `ApplyLevelUp(int,int,int)` @ 0x427080
  (writes `GameState+0xA4/0xA8/0xAC` upgrade counters and Health/Mana stats,
  applies HeroEquipmentManager::ApplyLevelUp);
  `HandleFall()` @ 0x426E5C; `HandleGameEvent(GameEvent const&)` @ 0x427160;
  `GameControlButtonDown/Up` @ 0x427D70/0x428E8C;
  `ManaCostForSkill` @ 0x428EC4; `RegisterTreasure*` @ 0x429168/0x429180.

## Open questions / unresolved offsets

- 0x30 (GameOverlay UI pointer), 0x184 (spare dword), and the full
  `HeroEquipmentManager` layout (0x100+) remain partially mapped.
- 32-bit offsets unverified.

## Proposed SRE hooks

- **Hero teleport (the big one)**: hero Position is at `gsc->hero + 0x80`
  (Vector3 x/y/z). A `sre_hero_set_position(x,y,z)` accessor should write it
  AND call `SceneObject::UpdateBounds` + clear the physics velocity — but the
  safest path is to reuse the engine's own spawn:
  `GameSceneController_SpawnHeroAt(gsc, spawnId)` (exported, verified) with a
  dynamically-built level `String`. Prefer SpawnHeroAt for scripted teleports.
- **Live stat injection**: see CharacterState.md — SRE already publishes
  `g_sre_player_*` from this struct's hierarchy (sre_scene_update.c).
- **Cast injection**: `BeginCasting`/`CanCastSkill` are exported and safe to
  call with a shared_ptr<Skill>; SRE could add a `g_sre_skill_cast_request`
  consumed in the Update hook.
- **Level transition**: `GameViewController::GotoLevel` (see
  GameViewController.md) is the clean path — do not tear down the GSC
  yourself.
- **Unsafe to expose raw**: `castingSkill` shared_ptr slots, `hero`
  intrusive ref, `Scene` shared_ptr — always go through the exported
  functions for writes. Reading `hero->Position` for telemetry (as SRE does)
  is safe.