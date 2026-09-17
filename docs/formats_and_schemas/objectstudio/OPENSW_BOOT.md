# opensw — booting Swordigo's own runtime into a level

`./opensw` boots the **recovered** engine (`caver::`) into a real, shipping level
with real hero controls, real collision and the real Lua programs. It is not an
emulator and it is not a `libswordigo` host: there is exactly one engine in this
build and it lives in `src/ruby/caver/`.

```
./opensw --assets assets13                       # level from the save, interactive stop
./opensw --assets assets13 --new                 # fresh hero (newplayer.gstate)
./opensw --assets assets13 --level town_part1    # jump to a specific level
./opensw --assets assets13 --frames 600 --input "0-200:right,201-230:right+jump,231-599:right"
```

## The boot chain (recovered, not invented)

| step | evidence (arm32_13) |
|---|---|
| `PlayerProfile::CreateProfile(id)` | `0x33BD14` — `PathForResourceOfType(id, "gstate")` → `Proto::GameState` |
| `LoadGameStateFromProtobufMessage` | `0x33BDB0` — `("test","scmap")` → `Proto::Map`, `("gamedata","gdata")` → `Proto::GameData`, then `GameState(GameData)` |
| `Filename` / `LocalFilePath` / `ProfileExists` | `0x33C32C` / `0x33BCAC` / `0x33BC5C` — `<id>.gplayer` in `DocumentsDirectoryPath()` |
| `GameSceneController::InitWithScene` | `0x313F1C` — camera `0.34907 rad`, near `1.0`, far `50.0`/`20000.0` |
| `GameSceneController::SpawnHeroAt` | `0x3140B4` — `ObjectWithIdentifier(id)` else `"spawn_default"`, hero pos = object pos + `SpawnPointComponent` offset |
| `GameSceneController::CreateHeroObjectAt` | `0x314258` — `ObjectLibrary::LibraryWithName("hiro")` → `TemplateForName("hiro")`, identifier `"hero"`, `HealthComponent` max = `2 * ExperienceLevel + 4` |
| `GameSceneController::AddHeroObjectToScene` | `0x314470` — `HeroEquipmentManager::Init`, equip weapon/armor, apply trinket to spells |
| `GameSceneController::GameControlButtonDown` | `0x315314` — the button → action map (see `game_control.h`) |
| `GameSceneController::Update` | `0x31468C` |

`newplayer.gstate` is 38 bytes and decodes to exactly what the request asks for —
a normal, zero-progress hero in the town:

```
field 1: (empty CharacterState)   -> level 0, no items
field 3: "town_herohouse"         -> CurrentLevel
field 4: "spawn_default"          -> CurrentSpawnPoint
field 9: "map"                    -> SelectedMenuTab
```

There is no "new game" special case anywhere: reading the template *is* the new
game. Equipment is derived too — `CharacterState::EquippedWeapon()` is
`HighestLevelItemOfType(ItemType::Weapon)` and `EquippedArmor()` is the same with
`ItemType::Armor` (literals `1` and `2`, `0x314568` / `0x314464`), so the hero
always wields the best **owned** item of that type.

## What it actually does on a boot

```
opensw: created profile 'player' from newplayer.gstate
opensw: level 'town_herohouse' (town_herohouse)
opensw: 326 objects, 1535 components, 583 libraries, 782 collision walls
opensw: 68 embedded Lua programs in the level (handlers + loops)
opensw: hero at (3535.2, 407.5, -1.2) via spawn point 'spawn_default'
opensw: hero tuning run=230.0 jump=250.0 maxJumpTime=0.23 gravity=0.0 radius=8.0
opensw: max health: 4
```

Every number is read out of the shipped data: `230`, `250` and `0.23` are
`CharControllerComponent.NormalRunSpeed` / `JumpSpeed` / `NormalMaxJumpTime` on
the `hiro` template, `4` is `2*0+4`, and the 583 libraries are the level's own
embedded `ObjectLibrary` messages plus everything it imports. Holding
`right` for one second moves the hero exactly 230 units — the archetype's speed.

## Architecture

```
src/ruby/caver/game/
  game_control.{h,cpp}             GameControlButton -> PlayerInput
  game_data.{h,cpp}                GameData (gamedata.gdata), GameMap (test.scmap)
  game_state.{h,cpp}               GameState/CharacterState/LevelState/QuestState
  player_profile.{h,cpp}           CreateProfile, LoadGameStateFromProtobuf, PathForResourceOfType
  game_scene_controller.{h,cpp}    InitWithScene/SpawnHeroAt/CreateHeroObjectAt/Update
  opensw_main.cpp                  the executable
```

All protobuf field numbers come from the binary's own
`Caver::Proto::<Class>::k<Field>FieldNumber` globals
(`tools/extract_component_schema.py`), so a new engine version is a re-run, not a
re-transcription.

## What is deliberately not here yet

1. **No renderer.** The plan is to publish `ObjectRenderState` / `VisualItem` into
   Ruby's existing viewport rather than grow a second draw path in this binary.
   Until then `opensw` prints the state it simulates.
2. **The hero's gravity is 0 in the data.** `hiro`'s `PhysicsObjectComponent`
   leaves `GravityMagnitude` unset, so the hero neither falls nor lands through
   the recovered physics; the ground resolve still works (a downward raycast
   against the level's walls), which is why the hero stands and runs. The engine's
   gravity for the hero therefore comes from somewhere else — the next thing to
   find, and `--frames` + the printed `gravity=` make it one run away.
3. **`CharControllerComponent::Update` is the hero's real locomotion.** Only its
   tuned values are consumed so far; the state machine (run/turn/land/hurt/die and
   the weapon controllers) is still in `arm32_13`, not in `caver`.
4. **Menu, save selection, and the pause/map buttons** (7/8/9) are out of scope on
   purpose, per the request.
5. **Portals, doors and cutscene transitions** load through the library registry
   already, but changing level mid-run is not wired into the loop yet.
