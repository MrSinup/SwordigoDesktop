# ObjectStudio & rbsrc — design plan

> Status: **proposal for review**. Nothing here is implemented beyond the SCL
> loader layer (`scl_load_templates` / `add` / `rename` / `remove` / `update` /
> `save`) and the Program bytecode fix from M0.
>
> Inputs read for this plan:
> - 17 real `.scl` files decoded with `ruby_cli -d` from
>   `~/.local/share/swordigo-desktop/assets/resources/` (51 ship in total).
> - `src/tools/filerift.cpp` (native schema + `@compile`), `src/tools/scene_loader.*`
>   (ObjectLibrary I/O), `src/tools/template_sources.cpp`, `src/tools/scene_lua.cpp`
>   (live Lua host), `src/ruby/panels/inspector_panel.cpp`.
> - The original Python FileRift 5.8.5 (`lib/block_formats.py`, `decode.py`,
>   `recode.py`, `util.py`) for the canonical tag→name table and marker fields.
> - `docs/MASTER_TODO_RUBY_GG_PARITY.md` (§2.4), `docs/misc/scene_component_library_notes.md`.

---

## 0. Thesis

An `.scl` is not a scene. It is an **object archetype library** — a bag of named
prefabs, each one a `SceneObject` (transform + LocalAabb) with an ordered,
id-addressed component graph and embedded Lua programs. It has no geometry of
its own (a `Model` names a `.pod`), no obvious visual form, and the FileRift text
flattens the graph into indentation. That is why nobody can read them.

So the tool is **not a viewer**. It is an **archetype IDE**: one semantic model,
many projections, and — critically — the ability to see the thing *run*.

The name follows the domain: `.scl` = `ObjectLibrary`, the unit is an object
archetype. **ObjectStudio**. (`SclStudio` describes the file format; `ObjectStudio`
describes the work.)

---

## 1. Ground truth (measured, not assumed)

### 1.1 Shape

```
ObjectLibrary                          // the .scl
├── Name : 'crypt'                     // field 1
├── Template {                         // field 2, repeated
│     Object {                         // SceneObject
│       Identifier : 'crypt_torch'     //   field 2  — the archetype's name
│       Component { … } × N            //   field 3  — ordered, typed
│       Position / Depth / Rotation / Scaling / LocalAabb / Hidden
│     }
│     Scaling : 1                      // field 3 of Template — library scale
│   }
├── ImportedLibrary : 'x'              // field 3, repeated (cross-library refs)
└── Texture : { … }                    // field 4, repeated
```

Corpus (17 files): **63 distinct component classes**. `monsters.scl` alone is 57
templates / 804 components / 91 Lua chunks. Nothing is a one-off — this is a
real content library.

### 1.2 The component is the atom

```
Component {
  ClassName  : 'FireEmitter'   // field 1 (string)
  Identifier : 105             // field 2 (object-local int handle)
  Label      : …               // field 3 (string, usually empty)
  ParentComponentIdentifier: … // field 4
  <payload field, e.g. 0x7ea = field 253 LEN> { FireEmitterComponent{…} }
}
```

**Every cross-component reference is another component's `Identifier`** on the
same object: `ParticleEmitterId`, `LightId`, `ModelId`, `AnimationControllerId`,
`BoundsShapeId`, `AttackAreaId`, `RoamAreaId`, … (≈40 such fields across the
schema). That single fact is what makes the graph well-defined and renderable.

### 1.3 Lua is double-stored, and the engine runs `Bytes`

`Program { String(0x0a), Bytes(0x12), Name(0x1a) }`. Across the corpus: 178
source chunks, 176 compiled chunks, **0 empty** — and every `Bytes` starts with
the Lua 5.1 header `\x1bLuaQ\x00\x01\x04\x04\x04\x08\x00`. Two source-only
chunks exist (`monsters.scl` 93 vs 91) — an edge case to handle, not assume away.

Engine behaviour (from the ARM64 decompilation) is unambiguous: `LoadIntoState`
tests field 2 only and feeds it to `luaL_loadbuffer`; field 1 is authoring
metadata the runtime never compiles. **M0 (already landed)** makes
`av::scene_set_program_source()` regenerate field 2 atomically and drop a stale
chunk on a compile error.

### 1.4 Handlers live on components, not on the object

Each event field is a nested `Program`, on a *specific* component:

| Handler | Owning component |
|---|---|
| `OnKill`, `OnHurt` | `MonsterEntityComponent` |
| `OnActivate` | `EntityActionComponent` |
| `OnCollide`, `OnCollisionEnd`, `OnReceiveDamage` | `CollisionShapeComponent` |
| `OnCollect` | `CollectableItemComponent` |
| `OnCast` | `SpellComponent` |
| `OnAttack` | `AttackComponent` |
| `OnBreak` | `BreakableObjectComponent` |
| `OnPress`, `OnRelease` | `PressureTriggerComponent` |
| `OnTouch` | `TouchableComponent` |
| `OnItemGet` | `HeroEntityComponent` |
| `OnLoad` | `PropertiesComponent`, or the object's own field 10 |
| `Program` | `ProgramComponent` |

Corpus frequencies: `OnKill` ×67, `OnHurt` ×67, `OnActivate` ×50, `OnCollect`
×28, `OnCollide` ×27, `OnLoad` ×12. So "behaviour" is a per-component property —
the studio must show *which* component owns *which* handler.

### 1.5 What already exists in-repo (reuse, don't rebuild)

- `scl_load_templates` → `SclTemplateEntry{name, scaling, raw_object_bytes, object}`.
- `scl_add/rename/remove/update_template`, `scl_save_to_file` — byte-preserving,
  unknown fields survive, untouched libraries round-trip byte-exact.
- `scene_component_fields` / `scene_set_component_field` — schema-driven field
  editing (the Qt Inspector already drives these).
- `scene_lua.*` — a real Lua 5.1 host that already classifies AI-loops vs event
  handlers, runs the shipped scripts against a stubbed engine API, and resolves
  proxies.
- `Viewport3DWidget`, particle engine, dynamic lights, GLSL 330 pipeline.
- `filerift::compile_lua_to_bytecode` (Lua 5.1, in-process) and the native
  `init_schemas()` table (class → tag → name → submessage).

**Conclusion: the semantic layer is mostly assembly. What is missing is one
middle-tier model (`TemplateGraph`) and the IDE on top of it.**

---

## 2. ObjectStudio

### 2.1 Product principles

1. **One archetype, one truth, many lenses.** The document is a semantic
   `TemplateGraph`; the diorama, graph, timeline and text are four projections of
   it. Edit anywhere, re-emit once.
2. **Nothing is lost.** Unknown fields, field order and `Label`s survive;
   untouched libraries are byte-identical. "Save" is safe by default.
3. **Show it running.** A prefab you cannot preview is a text file. The studio
   must model the archetype *and play it*.
4. **Never hand-type an id.** Identifiers are derived by the tool; you name
   components, it wires them.
5. **Learn from shipping content.** Every one of the 51 archetype libraries is a
   tutorial if the tool can decompile it back into an editable form.

### 2.2 The killer workflow (what we are optimising for)

Today a modder opening `magic.scl` sees ~6 000 lines of `Component{ ... }` with
`ParticleEmitterId : 108` and no way to know what 108 is, then hand-edits bytes
through FileRift.

Target:

1. Open the **Object Browser** → 51 libraries, 700+ archetypes.
2. Filter `has:Lua, warnings:any, unused:false` → "here are 12 broken handlers in
   your mod".
3. Click `fire_spirit` → **diorama** with real mesh, light and particles.
4. Press **Play** → the script runs; the sprite glows, particles rise, the
   timeline shows `Program.Wait(4.0)` ticking.
5. Click the emitter node → its `Light →` port flashes; drag a wire to another
   light → `LightId` rewired, no typing.
6. **Fork** → `fire_spirit_blue` → change radius/colour in the schema inspector →
   edit the handler in **rbsrc** → Save.
7. `magic.scl` changed by exactly the bytes of that one template; everything else
   is bit-identical, and the compiled `Bytes` match the source.

That is the revolution: the graph the text format hides becomes the primary UI,
and the compiler guarantees the two Lua stores agree.

### 2.3 Information architecture

A new top-level workspace in `ruby_gg` (**not** the legacy ImGui panel, which
should stay research-only). A document per `.scl`, plus a global index.

```
┌ Libraries ─┬─ Stage: fire_spirit ────────────────┬─ Inspector ──────┐
│ crypt.scl  │ [Diorama] [Graph] [Behaviour] [Text]│ (schema fields)  │
│  torch     │                                     │  Light #103      │
│  brazier   │        (3D preview / node graph /   │   Type   3       │
│  fire      │         behaviour timeline /        │   Radius 250 ────┤
│   spirit ◀ │         FileRift source)            │   Color  #9eef00 │
│ magic.scl  │                                     ├─ Issues ─────────┤
│  …         │                                     │ ⚠ dangling 118   │
└────────────┴─────────────────────────────────────┴──────────────────┘
  ▸ Timeline / Console (when Play is active)
```

Left rail: libraries → archetypes (badges: `Lua n`, `⚠ n`, `used ×N`). Top:
command palette (`⌘K` → "goto archetype", "new from clone", "lint all"). The
**stage tabs are the same archetype in different projections**, never different
tools.

The five lenses from the earlier pass survive, deepened, plus three that the
corpus and the existing infra justify:

**Lens 1 — Archetype table.** Sortable grid across the whole workspace, not one
file: name, library, component chips, `Lua n`, scaling, AABB, inbound refs,
warnings. Queries: has Lua, unreferenced, dangling, near-duplicate.

**Lens 2 — Component graph.** Nodes are components; edges are id-reference
fields. Lint: dangling ids, duplicate ids, orphan components, cycles. This is
the diagram nobody has ever had for Swordigo.

**Lens 3 — 3D diorama.** Assemble a synthetic one-object `SceneData` from
`SclTemplateEntry.object` and hand it to the existing viewport:
`Model`→POD instance, `Light`/`SimpleGlow`→light + billboard,
`ParticleEmitter`/`FireEmitter`→particle engine, `CollisionShape`/`UtilityShape`/
`GroundPolygon`→gizmos, `KeyframeAnimation`/`AnimationController`→animation,
`LocalAabb`→wire box, `Scaling`→folded in. View modes: **solo** (one archetype),
**turntable** (spin/zoom, for hero assets), **swarm** (N clones, for monsters and
projectiles), **dummy** (hero target + AI live).

**Lens 4 — Behaviour.** A per-archetype behaviour card: which component owns
which handler, plus the script. Click a handler → the compiler opens it with
line-accurate diagnostics. **This is where rbsrc plugs in (§3).**

**Lens 5 — Lineage / recipes.** The corpus is full of near-duplicates across
libraries. Because the schema is known, diff *semantically*: "94 % identical to
`torch_cave`; differs in `Light.Radius 250→200`, `SimpleGlow.Color`, and the
`OnKill` script". Fork-from-closest, not from blank.

**Lens 6 — Wiring editor (new).** Not just viewing edges — authoring them. Ports
are derived from the schema (`*Id` fields → typed ports), so dragging
`FireEmitter.Light →` onto a `Light` node writes `LightId` and nothing else.
Untyped references (`ParentComponentIdentifier`) are offered as a fallback.

**Lens 7 — Lint / health (new).** A first-class panel with severities and
quick-fixes:
`dangling-ref` · `duplicate-id` · `orphan-component` · `empty-bytes` (source but
no compiled chunk — the exact class of bug M0 fixed) · `missing-pod` /
`missing-texture` · `emitter-arity` (parameter count vs `Emitter.Type`) ·
`name-collision` across libraries · `zero-scaling` · `unreachable-handler`.

**Lens 8 — Play harness / behaviour tests (new, ambitious).** `scene_lua` already
runs the real scripts. Turn that into a *prefab test bench*: Play/Pause/Step, a
tick clock, a probe panel listing every API call and what it resolved to, and
assertions as a first-class artifact:

```
spec fire_spirit
  when dropped   → within 0.2s particles > 0
  when killed    → within 5s Scene.Find("fspr") == nil
```

This turns "does my mod work?" from a boot-the-game question into a unit test.

### 2.4 Data model

```
SclWorkspace
├── libraries : [SclDocument]              // one per .scl, bytes preserved
└── index     : name → archetype ref       // cross-file lookups

SclDocument
├── bytes, path, dirty
├── library_name, imported_libraries[], textures[], other_fields[]
└── archetypes : [Archetype]

Archetype
├── name, scaling
├── object : SceneObject                    // existing parser output
├── graph  : TemplateGraph                  // NEW — the semantic layer
├── programs : [ProgramRef { owner_component, field, string, bytes, rbsrc? }]
└── lint : [Diagnostic]

TemplateGraph
├── nodes : ComponentId → Node { class, id, label, fields[], ports[] }
├── edges : [Edge { from_node, from_field, to_node }]   // resolved *Id refs
└── unresolved : [Edge { from_node, from_field, raw_id }] // → dangling lint
```

`TemplateGraph` is a **view over bytes**, never a re-serialisation: it is built by
decoding, and every mutation goes through `scl_*` / component-field writers that
re-emit only the touched entry. This is the missing layer; everything else reads it.

**Labels as stable slugs (idea to validate).** `Component.Label` (field 3) is
present in the schema and empty in the corpus. Storing a stable slug there
(`light.fire`, `emitter.fume`) would make wiring human-legible and rbsrc
round-trippable. Risk: the engine may surface `Label` in tooling; needs an
engine check before we rely on it. Fallback: keep slugs in the rbsrc sidecar only.

### 2.5 Reuse map

| Concern | Existing code | Verdict |
|---|---|---|
| Library bytes, template CRUD | `scene_loader.cpp` `scl_*` | reuse |
| Field widgets + schema | `scene_component_fields`, `InspectorPanel` | reuse |
| Class/tag/name schema | `filerift::init_schemas`, `filerift_schema.cpp` | reuse + extend |
| Lua 5.1 host + handler detection | `scene_lua.*` | reuse (add probes) |
| 3D preview + particles + lights | `Viewport3DWidget` etc. | reuse via synthetic SceneData |
| Program String↔Bytes | `scene_set_program_source`, `compile_lua_to_bytecode` | reuse |
| Legacy ImGui SCL studio | `asset_viewer.cpp` | **do not reuse** |

### 2.6 Build order

| Milestone | Deliverable | Acceptance |
|---|---|---|
| **M1** | `TemplateGraph` + Lint engine in `swpod`, headless | unit tests on the corpus: graph edges resolve for `crypt_torch`; lint finds a planted dangling id |
| **M2** | Object Browser + Archetype table + Issues panel (Qt) | open all 51 libraries, filter, see warnings; bytes unchanged on close |
| **M3** | Component graph lens + wiring editor | rewire a `FireEmitter.Light` by drag; save; reload shows the new id |
| **M4** | Behaviour lens + real Lua editor (replaces the modal dialog) | edit a handler, compile-on-save, error maps to a line; Play runs it |
| **M5** | 3D diorama (synthetic SceneData) | `crypt_torch` renders with light + flame particles |
| **M6** | Play harness + behaviour specs | `fire_spirit` spec passes/ fails deterministically |
| **M7** | Lineage/semantic diff + fork-from-closest | fork `torch_cave` → diff view shows exactly 3 semantic changes |

---

## 3. rbsrc

### 3.1 Thesis

Two ideas make rbsrc adoptable where a fresh language would not be:

1. **rbsrc ⊃ Lua.** A `.rbsrc` file is Lua 5.1 plus optional sugar. Any existing
   script is valid rbsrc unchanged, and the compiler can be a thin source-to-source
   transform, so error mapping is honest and trust is high.
2. **Structure is part of the language.** Scripts are only half of an archetype;
   the other half is a component graph full of hand-typed ids. rbsrc can describe
   *that* too — and then the ids are compiler-generated, never typed.

So rbsrc has two tiers that share one grammar: **behaviour** (compiles to Lua →
`Program.String` + `Program.Bytes`) and **structure** (compiles to components →
the template's component list + derived `*Id` refs).

### 3.2 Behaviour tier

```lua
-- fire_spirit.rbsrc
on load(self)
  Program.Wait(2.0)
  local spray = Scene.CreateObject("firespray", "fspr", self)
  spray:setRotation(self:rotation())
  TransformController.ScaleTo(spray, 0.01, 0)
  spray:setHidden(true)
  while true do
    spray:setHidden(false)
    Program.Wait(4.0)
  end
end

on kill(self)
  SoundLibrary.PlayEffect("enemy_die")
end
```

- `on <event>(self)` is the only new construct. It maps to the owning component's
  `Program` field via the §1.4 table, so the modder never has to know that
  `OnKill` lives on `MonsterEntityComponent`.
- Bodies are Lua 5.1, type-checked against a generated engine API (below).
- Everything else in the file that is plain Lua is a normal chunk.

**Typed stdlib.** Generate a prelude from `register_api()` in `scene_lua.cpp` +
`swordigo_engine_db`, keyed by component namespace, with types inferred from the
corpus + decompilation:

```lua
namespace Program          Wait(seconds: number)        SetKeepActive(bool)
namespace Scene            CreateObject(template: string, id: string, parent: Entity?): Entity
                           Find(id: string): Entity?
namespace EntityController Target(self: Entity): Entity?  SetMoveSpeed(self, n)
namespace Light            SetIntensity(self, n)          SetColor(self, r,g,b,a)
namespace CollisionShape   SetEnabled(self, bool)
…
```

This gives hover docs, real arity errors, and completion on the exact calls the
shipped game uses.

**Component handles (sugar).** `self:CollisionShape.enabled = false` compiles to
`CollisionShape.SetEnabled(self, false)`; `self:Light("fire").intensity = 2`
resolves by `Label`/`Identifier` when an entity has several of a type.
*Open question:* how the engine API disambiguates multi-component entities — the
corpus must be read for the canonical pattern before we commit to the sugar.

### 3.3 Structure tier (the revolution)

```lua
archetype torch_cave
  scaling 1

  model  "torch"        as body
  glow   as halo        { color #3b5a33, size 27, pulse 0.07/0.8 }
  light  as fire        { type 3, color #9eef00, intensity 1.5, radius 250 }

  emitter as fume
    light     -> fire
    particles -> particle.fume      -- ParticleId
    max       -> 40

  fire as flame                     -- FireEmitterComponent
    light   -> fire                 -- LightId
    emitter -> fume                 -- ParticleEmitterId
end
```

The compiler:
1. assigns each declaration a stable slug and a fresh `Identifier`
   (honouring the `101+` conventions where the corpus uses them),
2. emits each `Component{ ClassName, Identifier, Label=slug, payload }`,
3. resolves every `->` into the correct `*Id` field for that component class
   (from the schema),
4. runs the behaviour tier for any `on …` blocks and writes `String` + `Bytes`,
5. re-emits the library through the byte-preserving `scl_*` path.

Because ids are derived, the whole class of "I typed 108 but the emitter is 109"
bugs disappears. This is the part that would actually change how Swordigo mods
are made.

### 3.4 Decompiler — the adoption hook

`ruby_cli rbsrc decompile magic.scl` → editable `.rbsrc`. Every shipped archetype
(700+) becomes a readable, forkable example. Titles: "want a floating lantern?
here is `crypt_torch` in 20 lines instead of 300".

Round-trip policy:
- **lossless for the modelled subset** — structure + programs + known fields;
- **preserved for the rest** — unknown fields stay in the bytes and are carried by
  the `scl_*` writers;
- **explicit for anything it cannot express** — emit `raw { 0x7ea: <hex> }` blocks
  rather than silently dropping data.

### 3.5 Where the rbsrc source lives (decide together)

| Option | Pro | Con |
|---|---|---|
| `.rbsrc` **sidecar** next to the `.scl` | shipped `.scl` stays clean & portable; studio project owns the richer source | lost if only the `.scl` is shared |
| Program **`Name`** field (3) | travels with the file | abuses a real field; visible to other tools |
| synthetic Program field (like `514 @compile`) | travels with the file; protobuf ignores unknowns | new convention; must confirm engine tolerance |

Recommendation: **sidecar by default**, with `String` always holding the
*generated Lua* (so the file is self-describing and any Lua tool still reads it),
and revisit an embedded field only if we confirm the engine ignores it.

### 3.6 Compiler pipeline

```
.rbsrc
  → lex/parse (Lua-superset grammar)
  → resolve   (slugs → component ids; stdlib symbols; handler → owning component)
  → typecheck (warnings by default, --strict to fail)
  → emit Lua   (structure-independent; event bodies verbatim + prelude)
  → compile    → luaL_loadbuffer + lua_dump → Program.Bytes
  → emit graph (components, ids, resolved *Id wires)
  → ObjectLibrary bytes (scl_* byte-preserving writers)
  → diagnostics (source spans mapped back to .rbsrc)
```

A source map from generated Lua lines back to rbsrc spans means a preview crash
points at the line the modder actually wrote.

### 3.7 Tooling

```
ruby_cli rbsrc build   x.rbsrc -o x.scl          # structure + behaviour
ruby_cli rbsrc script  x.rbsrc -o x.lua          # behaviour only
ruby_cli rbsrc decompile magic.scl -o magic.rbsrc
ruby_cli rbsrc check   --strict src/             # CI
ruby_cli rbsrc fmt     x.rbsrc
```

Plus LSP endpoints (hover, complete, diagnose) consumed by the ObjectStudio code
pane, and a headless `check` for mod CI.

### 3.8 Testing

Golden files: `input.rbsrc` → expected `Program.String`, `Program.Bytes`,
component bytes, and emitted ids. Plus behaviour tests through the `scene_lua`
harness (spec §2.3/Lens 8) so a compile that "looks right" is also *demonstrated*.

---

## 4. How the two fit together

```
              .scl bytes
                  │  scl_load_templates
                  ▼
   ┌────── TemplateGraph ──────┐        ← M1 (the missing middle tier)
   │ nodes · edges · programs  │
   └────┬──────────┬───────────┘
        │          │
   Lenses 1–8   rbsrc ⇄ Lua
   (Qt IDE)     compiler
        │          │
        └──► Program.String + Bytes ◄── compile_lua_to_bytecode (M0, done)
                    │
              scl_* byte-preserving writers
                    │
                 .scl bytes
```

rbsrc is not a separate tool: it is the **L3 text front-end of the studio**, and
the studio is the thing that owns archetypes. That is why they should ship
together.

---

## 5. Open questions to settle together

1. **rbsrc source storage** — sidecar vs Program `Name` vs synthetic field (§3.5).
2. **Component slugs** — is `Component.Label` safe to use for stable names, or
   engine-visible? (§2.4)
3. **Multi-component disambiguation** — read the corpus for the canonical way a
   script obtains a specific component of a type (§3.2).
4. **Emitter parameter names** — build the `Type → [Parameter]` name table
   (correlate arity with the decompiled particle code) so Lens 7's `emitter-arity`
   and the inspector can label slots.
5. **`String` semantics** — should ObjectStudio always write generated Lua into
   `String` (self-describing), even when rbsrc is the source of truth?
6. **Cross-library imports** — how `ImportedLibrary` resolution order affects
   the global archetype index and "used by N scenes".

## 6. Suggested first step

Everything above rests on `TemplateGraph`. It is pure data, headless, and
unit-testable against the 51-file corpus — no Qt, no GL. Build **M1** first: it is
the load-bearing wall, and it immediately powers the Lint engine and the graph
lens regardless of which UI shell we choose.
