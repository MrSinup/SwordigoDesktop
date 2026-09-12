# Caver Core — backend correction, SCL runtime & real scene player

> Companion to `OBJECTSTUDIO_AND_RBSRC.md`. Status: **design position for review.**
>
> Decompilation read for this note (per-function evidence files under
> `OpenSwordigo/arm64_12/functions/Caver/…` and the symbol/callgraph tables):
>
> | Area | Functions confirmed to exist |
> |---|---|
> | `Scene` | `RegisterLibrary`, `FinishLoad`, `Process`, `AddObject`/`RemoveObject`, `AddGroup`/`RemoveGroup`, `MakeUniqueObjectIdentifier`, `ObjectWithIdentifier`, `RegisterNewComponent`, `ActivateObject`, `NewProgramStateForProgram`, `Update`, `DrawModels`, `DrawGroundMeshes` |
> | `SceneObject` | `InitWithTemplate`, `AddComponent`, `GetAllComponents`, `Templatize`, `Clone`, `AddChildObject`, `setLocalAABB`, `WorldPointFromLocalPoint`, `RegisterForWorldBoundsUpdate` |
> | `ObjectLibrary` | `LoadFromFile`, `LoadFromProtobufMessage`, `ImportLibrary`, `RemoveImportedLibrary`, `IsLibraryImported`, `LibraryWithName`, `LibrarySearchPath`, `FindLibrariesDirectoryFromParentDirectories`, `TemplateForName`, `GetAllTemplates`, `AddTexture`/`ContainsTexture`, `AddProgram`, `LoadAllPrograms`, `Process`, `RemoveTemplate` |
> | `GameSceneController` | `InitWithScene`, `CreateHeroObjectAt`, `AddHeroObjectToScene`, `SpawnHeroAt`, `UpdateTarget`, `HandleFall`, `ApplyLevelUp`, `RegisterTreasure(Collection)`, `EquipItem`, `ConsumeItem`, `CanCastSkill`, `BeginCasting` |
> | `ProgramState` | `RegisterLibrary(name, LibFunction*)`, `Update`; `Program::{LoadFromProtobufMessage, LoadIntoState, ExecuteString, InitWithString}` |

---

## 1. Verdict

**Backend first is right. The name is wrong.** The thing to build is not a "scene
player" — it is a **Caver Core**: the object/component/program runtime the game
itself is built on. A scene player, the editor's preview, the SCL runtime, and
ObjectStudio are all *clients* of that core.

This ordering matters because it decides where the assumptions live. Our current
`src/tools/scene_player.cpp` is the cautionary tale: it declares

```cpp
enum class AiKind { None, Walker, Bat, Charger, Bouncer, Static, Archer };
```

and moves objects with hand-written heuristics. That hardcoded AI is not a bug in
the player — it is a *symptom* of having skipped the layer underneath. In vanilla
there is no "AiKind": there is a component update graph where
`ChargingMonsterControllerComponent`, `WalkingMonsterControllerComponent`,
`BatMonsterControllerComponent`, … each own an `Update(float)` and drive
`EntityControllerComponent` / `AnimationControllerComponent` /
`PhysicsObjectComponent`. **Real AI comes free once components tick.** Writing a
better player on top of the stub would just hardcode the AI in a nicer place.

So: delete the heuristic, build the core.

---

## 2. The finding that settles the SCL-runtime argument

`SceneObject::InitWithTemplate(template)` is *the* object-creation primitive. Its
caller list is the game's spawn surface: `BreakableObjectComponent::Break`,
`HealthComponent::Update` (death drops), `ItemDropComponent::CreateItemObjects`,
`MagicBoltComponent::Explode`, `MagicBombComponent::Cast`,
`MagicExplosionComponent::Update`, `MagicHookshotComponent::CreateBlast`,
`ProjectileControllerComponent::CreateBlast`, `SkillComponent::CreateSpellObject`,
`MonsterDeathControllerComponent::KillMonster`, `SkieletonMonsterControllerComponent::CreateWeapons`,
`SwingableWeaponControllerComponent::CreateWeapon`, `SpawnHeroAt`, `Clone`,
`Templatize`, and `Scene::LoadFromFile`.

The body (0x46F51C) is exactly the resolution algorithm:

- it takes the template's component list,
- for each template component, if the **instance** already has a component with
  the same key, it is left alone; otherwise a fresh component is manufactured via
  the component factory's vtable and pushed through `SceneObject::AddComponent`,
  marked as inherited,
- then a second pass reconciles components whose "override" flag is set,
- finally `SetInstanceScaling(this, template_scaling)` applies the library scale.

Which means: **`Scene.CreateObject("firespray", "fspr", self)` in a Lua program is
a template lookup plus `InitWithTemplate`.** Runtime object creation goes through
the ObjectLibrary. Therefore:

> **A library manager is not an editor convenience — it is a gameplay
> requirement.** If Ruby re-parses `.scl`s ad hoc per scene and has no persistent
> registry of loaded libraries, then `Scene.CreateObject`, item drops, spell
> spawns, weapon creation and hero spawn are all either broken or faked. That is a
> much stronger reason to build the SCL runtime than "editing is slow".

We already have the *edit-time* half of this (`scl_load_templates`,
`scene_materialize_object_template`, `scene_override_inherited_component`,
`scene_apply_clean_template`), which mirrors the two-pass override logic. What is
missing is the *runtime* half.

---

## 3. Caver Core — the runtime facade

One module (`av::caver` or `sp::core`), no rendering, no UI, deterministic. It
exists so the editor preview, the player, the CLI and the tests all share one
simulation.

```
CaverCore
├── LibraryManager        // ObjectLibrary registry + import graph   (§4)
├── Scene                 // groups, objects, identifiers, activation
│   ├── groups   : name → SceneObjectGroup
│   ├── objects  : name → SceneObject            (MakeUniqueObjectIdentifier,
│   │                                             ObjectWithIdentifier)
│   ├── bounds   / collision shapes (registered for world-bounds update)
│   └── pending  : objects waiting for activation
├── ComponentRegistry     // class name → factory + schema + Update slot (§5)
├── ProgramHost           // ProgramState per program, ticked clock  (§6)
└── Hero                  // GameSceneController-equivalent            (§6)
```

Explicitly modelled vanilla operations: `AddObject`/`RemoveObject`,
`AddGroup`/`RemoveGroup`, `RegisterNewComponent`, `ActivateObject`,
`NewProgramStateForProgram`, `FinishLoad`, `Update(float)`.

The renderer keeps consuming `SceneData` + per-object play state, as it does
today; the core produces that state instead of heuristics producing it.

---

## 4. LibraryManager (the SCL runtime)

### 4.1 Model

```
LibraryManager
├── libraries : name → ObjectLibrary          // loaded, keyed by name
├── by_path   : path → {name, mtime, hash}    // reparse only on change
├── imports   : name → [name]                 // ImportedLibrary DAG
├── search_paths : [dir]                      // LibrarySearchPath parity
└── generation : u64                          // bumped on any content change
```

Mirrors the vanilla API 1:1: `load(path)`, `load_from_bytes(bytes, name)`,
`library_with_name(name)`, `import_library(name)`, `remove_imported_library(name)`,
`is_library_imported(name)`, `template_for_name(name, required)`,
`get_all_templates()`, `load_all_programs()`, `process(dt)`.

Two distinctions the current code blurs and must not:

1. **`Scene::RegisterLibrary` is not library loading.** Its real body (0x462DAC)
   registers Lua bindings named `"scene"` and `"Scene"` into `ProgramState`. That
   is the *API registration*, done once — exactly what `scene_lua.cpp`'s
   `register_api()` already does. Keep the two concepts named apart.
2. **`ObjectLibrary::LoadAllPrograms` is a real step.** Libraries expose programs
   (field 5 `Program`, plus handler programs inside templates). Loading a library
   should build its program table, not just its templates.

### 4.2 Hot swap — "edit an SCL, it replaces the old one in the runtime"

The user's instinct is correct and matches how the game behaves when a template is
re-assigned. Design:

```
edit .scl on disk
  → reparse ONLY that file                (mtime+hash gate)
  → LibraryManager::replace(name, new_lib) // bumps generation
  → for each live SceneObject whose template resolves from that library:
        re-run the InitWithTemplate two-pass reconciliation
        • untouched components  → refreshed from the new template
        • overridden/local ones → win (they are the instance's own)
  → programs re-bound by name; running ProgramStates for removed programs stop
  → mark dirty; never reload the whole scene
```

The single semantics decision this forces — and I'd like your call on it — is
**what happens to already-spawned objects when their template changes**:

- **(a) Refresh** (vanilla's `InitWithTemplate` behaviour, and what I recommend):
  live objects pick up template changes except where they overrode a component.
  The editor preview stays truthful; this is also what makes templates useful.
- **(b) Snapshot**: objects instantiated before the edit keep their old component
  set until re-spawned. Safer for a *running game session*, but the editor then
  shows wrong objects.
- **(c) Explicit**: refresh on demand (a "re-resolve instances" action + a badge
  on the archetype). Predictable, but manual.

I'd implement **(a) as the default with a per-session toggle to (b)**, because the
override flags already give us the right "local wins" granularity.

The existing 1.3 s template-rescan freeze (`scan_template_sources`) is the same
disease: it walks every root and reparses every `.scl` on refresh. The
LibraryManager's mtime+hash cache removes it structurally.

---

## 5. Component coverage — make "understand every component" finite

"Ruby should understand every component fully" is the right goal but an unbounded
sentence. Turn it into a **matrix with a number**, per the 63 classes in the
corpus:

| Class | Parse | Instantiate | Wire refs | Update | Render | Notes |
|---|---|---|---|---|---|---|
| Model | ✅ | ✅ | – | ✅ | ✅ | |
| Light | ✅ | ✅ | – | ✅ | ✅ | |
| ParticleEmitter | ✅ | ✅ | ✅ | ✅ | ✅ | params table missing |
| FireEmitter | ✅ | ✅ | ✅ | ✅ | ✅ | |
| CollisionShape | ✅ | ⚠ | ✅ | ❌ | ✅ | needs collision solver |
| PhysicsObject | ✅ | ⚠ | – | ❌ | – | needs integrator |
| EntityController | ✅ | ⚠ | ✅ | ❌ | – | drives movement |
| ChargingMonsterController | ✅ | ⚠ | ✅ | ❌ | ✅ | animation only today |
| … | | | | | | |

- **Parse** (all 63, lossless) — largely done via the schema; make it exhaustive
  and regression-tested against the corpus.
- **Instantiate** — must go through `InitWithTemplate` semantics, not ad-hoc.
- **Wire refs** — resolve every `*Id` to a live component instance (the
  `TemplateGraph` from the ObjectStudio plan is the same data).
- **Update** — the actual simulation, in dependency order:
  `Transform`/`ModelTransformController` → `Model`/`AnimationController`/
  `KeyframeAnimation`/`BlendAnimation` → `PhysicsObject`/`CollisionShape`/
  `GroundMesh` → `Entity`/`CharController`/`EntityController` → `*MonsterController`
  → `Health`/`Damage`/`Attack` → `CollectableItem`/`ItemDrop`/`Skill`/`Magic*` →
  `ParticleEmitter`/`Light`/`SimpleGlow` → property/logic components.
- **Render** — already exists.

The honest reality is that ~20 classes carry the simulation; the rest are data or
one-clock behaviours. The dashboard is how we *know* when the backend is done
instead of arguing about it.

---

## 6. Hero and ProgramHost

**Hero is mandatory, not optional.** Scripts assume it:
`EntityController.Target(self)` needs a target, `Scene.Find("hero")` must resolve,
parenting (`Scene.CreateObject(..., self)`) and damage/collision callbacks assume
a player entity. Vanilla path: `GameSceneController::InitWithScene` →
`SpawnHeroAt(name)` / `CreateHeroObjectAt(…)` → `AddHeroObjectToScene`, plus
`HeroEntityComponent` (has `OnItemGet`), `CharControllerComponent`,
`CharAnimControllerComponent`, `HeroEquipmentManager`.

For the core, the hero is just **an archetype instantiated through the same
`InitWithTemplate` path** with a profile-lite (health, mana, coins, level,
equipped item names). No special case in the simulation — only in input and
camera. That keeps it honest and reuses §4.

**ProgramHost** is the existing `scene_lua.cpp` behaviour promoted into the core:
one `ProgramState` per running program, resumed on a shared clock against
`Program.Wait`, with all libraries registered (`"scene"`, `"Scene"`, component
namespaces). The `Program.String` (source) is what we run — the engine only loads
`Bytes`, but for the *editor* the source path is what gives line-accurate errors;
injecting both (M0) keeps the file correct for the real game.

---

## 7. Scene player = thin client

Once §3–§6 exist, the player is: core tick + input + camera + renderer. Explicitly
deleted: `AiKind` and every heuristic in `src/tools/scene_player.cpp`. The one
worth keeping from it is the `PlayObject` state struct the renderer already reads —
the core should *write* it.

"Real Caver semantics" for the player is therefore a checklist, not a vibe:

- objects created via template instantiation, identifiers unique
  (`MakeUniqueObjectIdentifier`), `ObjectWithIdentifier` resolves,
- activation gating (`ActivateObject`, `RegisterObjectWaitingForActivation`) before
  `OnLoad` fires (`FinishLoad`),
- component tick order as in `Scene::Update`,
- world bounds registered and updated for collision-capable components,
- programs ticked on the shared clock with `Wait`,
- groups (`SceneObjectGroup`) for camera/region logic,
- hero present before any program starts.

---

## 8. rbsrc: imports and namespaces

Your `import magic` idea is right, with three rules that keep it honest:

```
import "magic.scl"                    # namespaced: magic.fire_spirit
import "magic.scl" as m               # alias
from "magic.scl" import fire_spirit   # unqualified
import lib "magic"                    # name-resolved via ObjectLibrary search path
import lua "./helpers/vec.rbsrc"      # compile-time inlined helper module
```

1. **Libraries are named, and names come from the file** (`ObjectLibrary.Name` —
   `crypt`, `magic`). Both explicit-path and search-path forms should exist;
   explicit paths win in ambiguity.
2. **Collisions are errors, never silent.** If `import "a"` and `import "b"` both
   define `fire_spirit`, unqualified use is an error until qualified. Lint it.
3. **There is no runtime `require`.** The engine loads whole `Program` chunks; a
   script cannot `require` at runtime. So an rbsrc `import` must be a
   **compile-time** operation: pull the module's declarations, inline the used
   functions into the emitted Lua chunk, dead-code-eliminate the rest, and emit
   the engine API calls. This is a compiler feature, and it is the difference
   between a language that composes and one that pretends to.

Also align the typed stdlib with the engine's own registration shape:
`ProgramState::RegisterLibrary(name, LibFunction*)` is called with `"scene"` and
`"Scene"`, i.e. **the engine already thinks in named libraries of functions** —
so rbsrc namespaces (`Program.`, `Scene.`, `EntityController.`, `Light.`, …)
should be generated from that registration table, not invented.

### 8.1 Visual AI editor

If we build it, it must be a **view over the same rbsrc AST** — a bidirectional
projection, like the diorama is over components. Node graph for flow (events,
waits, spawns, state machines), raw-code node for anything the graph can't express.
Two sources of truth (a graph format *and* rbsrc) will drift within a week; one
AST with two editors won't.

What the graph can express well, from the corpus: `on <event>` entry points,
`Program.Wait` sequencing, `Scene.CreateObject` spawns, loop/keep-active states,
`TransformController` tweens. Everything else falls to a code node.

---

## 9. Suggested order

The ObjectStudio plan's **M1 (`TemplateGraph` + lint)** and this core overlap:
`TemplateGraph` is the static half of `ComponentRegistry`. Build the core so the
editor reads it rather than re-deriving.

| Step | Deliverable | Why first |
|---|---|---|
| **C1** | `LibraryManager` — load/import/cache/hot-swap, keyed by name, mtime+hash | unblocks `Scene.CreateObject`, hero, everything |
| **C2** | `ComponentRegistry` + coverage matrix generator (parse/instantiate/wire/update/render per class, from the corpus) | makes "understand every component" measurable |
| **C3** | Scene load parity: `FinishLoad`, groups, identifiers, activation, `InitWithTemplate` two-pass | the semantics every object depends on |
| **C4** | Hero as an archetype + `ProgramHost` with all libraries registered | scripts stop breaking |
| **C5** | Component `Update` implementation in dependency order | this is where real AI appears |
| **C6** | Scene player as a thin client; delete `AiKind` | last, not first |

C1 and C2 are small and self-contained and can be tested against the 51 files
with no GL, no Qt — a good place to start.

---

## 10. Open questions for you

1. **Hot-swap semantics** (§4.2): refresh live instances by default (a), snapshot
   (b), or explicit (c)?
2. **Hero source**: instantiate the shipped hero archetype/POD straight from the
   assets, or build a synthetic hero that satisfies the API surface? (I lean
   shipped archetype — it is the only way `OnItemGet`/equipment behave.)
3. **Program execution source**: run `String` (line-accurate errors, editor
   preview) while always writing both — agreed?
4. **Simulation scope for "real AI"**: implement the `*MonsterController` family
   faithfully, or start with the generic ones and treat the specialised
   controllers as data-configured? (I lean faithful-but-staged via the C2 matrix.)
5. **rbsrc scope**: script tier first, structure tier second — or both, since the
   structure tier is what makes `Scene.CreateObject` ids correct?
