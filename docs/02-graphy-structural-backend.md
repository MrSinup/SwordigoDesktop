# 02 — Graphy Structural Backend (SCL)

Scope: the **non-spatial** graph. One node per `SceneObject` inside an `ObjectTemplate`,
sub-nodes per `Component`, wires for integer `Identifier` cross-references. No world
transform, no timeline. The Scene backend (`03-…`) reuses everything here.

---

## 1. Responsibilities and boundaries

**In scope**

- Parse a decoded `.scl` (both dialects, `01-…` §7) into a `GraphDocument`.
- Build nodes: `TemplateNode` → `ObjectNode` → `ComponentNode` (one per `Component` block).
- Discover every cross-reference edge from fields, resolve it, and emit a typed wire.
- Quarantine unresolvable/ambiguous references instead of silently dropping them.
- Place nodes deterministically (comment frame per template, columns by class family).
- Report per-node *and* per-graph diagnostics (unresolved refs, duplicate identifiers,
  components with unknown `ClassName`, missing payload slots).

**Out of scope (Scene backend owns it)**

- World `Position`/`Depth` pan/zoom.
- Hook buttons, ghosts, timeline, `.rbsrc` binding.
- Live injection.

**Non-goals**

- **No value-flow / logic graph.** There are no exec pins, no branch nodes, no
  type-promotion. Every wire is a typed identity edge between two components (or a
  component and an object). Rationale in `00-…` §6 "deliberately rejected".
- **No re-serialisation of untouched data.** The SCL backend is read-only on the asset;
  only `05-…`'s surgical `Program{}` writer ever mutates bytes.

---

## 2. Current state (what exists, precisely)

| Artifact | Lines | Role | Verdict |
| --- | --- | --- | --- |
| `src/ruby/graph/graphy.h` | 285 | `Pin`, `Connection`, `Node`, `Graph`, `CommentFrame`, `NodeFlags`, `PinType` (14 types), `ConnectionResponse` | **Keep, extend.** Good bones; wrong concurrency of authority. |
| `src/ruby/graph/graphy.cpp` | 892 | JSON (de)serialisation, `can_connect`, cycle detection, Kahn topo sort, `create_demo_graph()` | Keep algorithms; replace the hand-written demo graph. |
| `src/ruby/graph/graphy_canvas.cpp` | 1254 | `QWidget` painter: grid, comments, wires, nodes, pins, pills, minimap, marquee, slice | **Rewrite layout + pin/pill drawing.** See `07-…`. |
| `tools/scl_to_graph.py` | 433 | `.scl` → Graphy JSON | **Supersede.** Loses data (see §2.1). |
| `tools/scene_to_graph.py` | 718 | `.scene` → Graphy JSON | **Supersede.** Same losses. |
| `src/tools/scl_graph_viewer.cpp` | 124 | standalone `QMainWindow` host | Extend into the dual-backend host. |

### 2.1 What the Python converters actually get wrong (measured)

1. `REF_FIELDS` has **13** entries; the schema has **129** reference-shaped fields over
   **54** classes. 116 edges are invisible.
2. `payload = child; break` takes only the **first** payload block. Real components have
   up to **3** (`BoneControlledCollisionShape` → `BoneControlledCollisionShapeComponent`
   + `CollisionShapeComponent` + `ShapeComponent`), and 6 monster controller families
   always carry `MonsterControllerComponent` **plus** a specific one.
3. `comp_ref_inputs[ref_key] = (target_id, pin_in)` is a **dict keyed by field name**, so
   `hiro.scl`'s three `SwingComponentId : 6/8/15` collapse to one wire.
4. `all_comp_nodes[(ident, comp_id)]` is used as the resolution table, but `ident` is the
   **object** identifier — correct — yet the *uniqueness* assumption is undocumented and
   unenforced. `hiro.scl` reuses `Identifier 101` for three different components in three
   different objects, which is fine, but the same happens for `1` and `120`.
5. `subtitle: f"Tpl: {tpl} · (Z: {pz:.1f})"` on an *SCL* node renders a `Z` that does not
   exist for a template in isolation (bug 3 in `00-…` §4 is a *Scene*-backend symptom but
   the string is authored in the shared helper).
6. `Asset: {name_val}` name + `default_value: name_val` → the duplicate-tag bug.
7. `PIN_BOOLEAN` is assigned from text `true`/`false`, which never occurs in the wire
   format (`01-…` §4).

---

## 3. Class registry: `ClassName` → payload block(s) + slot

**81 component extension slots** are recoverable from the binary;
`tools/extract_component_schema.py libswordigo.so --components` prints
`field_number tag class_name count`. **77 `ClassName` tokens** were observed across the
51 shipping `.scl` files. The two sets are *not* the same shape, because one `Component`
can carry several payload blocks.

### 3.1 Observed `ClassName` → payload block set (authoritative for the reader)

Measured by walking every decoded `Component{…}` in 51 `.scl` files
(`.scratch/graphy_census/`):

```
AnimationController            AnimationControllerComponent
Attack                         AttackComponent
BatMonsterController           BatMonsterControllerComponent + MonsterControllerComponent
BlendAnimation                 BlendAnimationComponent
BoneControlledCollisionShape   BoneControlledCollisionShapeComponent + CollisionShapeComponent + ShapeComponent
BouncingMonsterController      BouncingMonsterControllerComponent + MonsterControllerComponent
BreakableObject                BreakableObjectComponent
BushController                 BushControllerComponent
CharAnimController             AnimationControllerComponent + CharAnimControllerComponent
CharController                 CharControllerComponent
ChargingMonsterController      ChargingMonsterControllerComponent + MonsterControllerComponent
CollectableItem                CollectableItemComponent
CollisionShape                 CollisionShapeComponent + ShapeComponent   (ShapeComponent alone: 1 case)
Damage                         DamageComponent
DimensionObject                (none)
DimensionSpell                 SpellComponent
DoorController                 DoorControllerComponent
ElevatorController             ElevatorControllerComponent
Entity                         EntityComponent
EntityAction                   EntityActionComponent
EntityController               EntityControllerComponent
EntityInfo                     EntityInfoComponent
FireBreath                     FireBreathComponent + SpellComponent
FireEmitter                    FireEmitterComponent
GenericMonsterController       GenericMonsterControllerComponent + MonsterControllerComponent
GroundMesh                     GroundMeshComponent
GroundMeshGenerator            GroundMeshGeneratorComponent
GroundPolygon                  GroundPolygonComponent
Health                         HealthComponent
HeroEntity                     EntityComponent + HeroEntityComponent
ItemDrop                       ItemDropComponent
KeyframeAnimation              KeyframeAnimationComponent
Light                          LightComponent
MagicBolt                      MagicBoltComponent + SpellComponent
MagicBomb                      MagicBombComponent + SpellComponent
MagicExplosion                 MagicExplosionComponent
MagicHookshot                  MagicHookshotComponent + SpellComponent
MagicSpellCast                 MagicSpellCastComponent
Model                          ModelComponent
ModelTransformController       ModelTransformControllerComponent
MonsterDeathController         MonsterDeathControllerComponent
MonsterEntity                  EntityComponent + MonsterEntityComponent
ObjectLinkController           ObjectLinkControllerComponent
OrbitController                OrbitControllerComponent
OverlayText                    OverlayTextComponent
Particle                       ParticleComponent
ParticleEmitter                ParticleEmitterComponent
PhysicsObject                  PhysicsObjectComponent
PhysicsPlatform                PhysicsPlatformComponent
Portal                         PortalComponent
PortalEffect                   PortalEffectComponent
PressureTrigger                PressureTriggerComponent
Program                        ProgramComponent
ProjectileController           ProjectileControllerComponent
Properties                     PropertiesComponent
Shadow                         ShadowComponent
ShatterComponent               (none)
ShootingMonsterController      MonsterControllerComponent + ShootingMonsterControllerComponent
SimpleGlow                     SimpleGlowComponent
SkellyMonsterController        MonsterControllerComponent + SkellyMonsterControllerComponent
Skill                          SkillComponent
SnappingMonsterController      MonsterControllerComponent + SnappingMonsterControllerComponent
SoundEffect                    SoundEffectComponent
SpawnPoint                     SpawnPointComponent
StaticMonsterController        MonsterControllerComponent + StaticMonsterControllerComponent
Swing                          SwingComponent
SwingableWeapon                SwingableWeaponComponent
SwingableWeaponController      SwingableWeaponControllerComponent
TextureMapping                 TextureMappingComponent
Touchable                      TouchableComponent
Transform                      (none)
TransformController            (none)
UtilityShape                   ShapeComponent
WalkingMonsterController       MonsterControllerComponent + WalkingMonsterControllerComponent
WaterMesh                      WaterMeshComponent
WeaponGlow                     WeaponGlowComponent
WeaponTrail                    WeaponTrailComponent
```

Notes that matter for the node model:

- **`ClassName` is not derivable from the payload block name and vice versa.** `UtilityShape`
  → `ShapeComponent`; `CollisionShape` → `CollisionShapeComponent` + `ShapeComponent`;
  `HeroEntity` → `EntityComponent` + `HeroEntityComponent`. So the node's title comes from
  `ClassName`, and the *pins* come from the payload block(s), and the mapping must be a
  table, not a string transform.
- **Four `ClassName` tokens have no payload block at all**: `Transform` (10 instances),
  `TransformController` (48), `DimensionObject` (4), `ShatterComponent` (1). Types
  `Transform` and `TransformController` are the two most common in that group, which
  suggests their configuration lives in a payload message the decoder does not yet know
  (`TransformController` present in `swordigo_symbols.txt`?). **Open question — see §8.**
- **Multi-block components branch the graph**: e.g. `CollisionShape` should present
  `ShapeComponent`'s `Rectangle/Circle/Polygon` as a *shape* sub-node and
  `CollisionShapeComponent` as the *collision* sub-node, with an internal
  `shape → collision` edge. This is the honest structure and it is what the current
  converter destroys.

### 3.2 Slot map (generated, not transcribed)

`tools/extract_component_schema.py libswordigo.so --components` emits
`field_number tag class_name count`, e.g.

```
100  802   SpriteComponent
101  810   ModelComponent
102  818   KeyframeAnimationComponent
110  882   GroundPolygonComponent
120  962   ShapeComponent
121  970   CollisionShapeComponent
149  1194  AnimationControllerComponent
151  1210  CharControllerComponent
157  1258  ProgramComponent
251  2010  ParticleComponent
302  2418  MonsterControllerComponent
400  3202  SwingableWeaponComponent
500  4002  PortalComponent
```

Full list = 81 rows. **The implementation must consume this, not a hand-written table.**
Add `tools/generate_graphy_schema.py` producing `src/ruby/graph/graphy_schema.h`:

```cpp
// generated by tools/generate_graphy_schema.py from component_schema.json
namespace ruby::graph::schema {
struct Payload { int slot; const char* message; };
struct ClassEntry { const char* class_name; Slice<Payload> payloads; };
struct FieldEntry { const char* field; int tag; PinType type; RefScope scope; };

inline constexpr Payload kCollisionShapePayloads[] = {
    {121, "CollisionShapeComponent"}, {120, "ShapeComponent"},
};
inline constexpr ClassEntry kClasses[] = {
    {"CollisionShape", make_slice(kCollisionShapePayloads)}, …
};
}
```

Regenerating must be a CI step whose diff is reviewed: "what changed between engine
versions" then becomes a diff of this header, which is the honest answer to that question
(the same reasoning `tools/scan_scl_components.py` already articulates in its header
comment).

---

## 4. Typed-pin schema

### 4.1 `PinType` reuse

Graphy already has a 14-value `PinType` enum with Unreal palette colours
(`graphy.h`). Keep it. Map Swordigo field types onto it:

| Wire type | `PinType` | Colour | Examples |
| --- | --- | --- | --- |
| reference to a `Component` | `Int` | `#03C46E` teal | `ModelId`, `ParticleId`, `SwingComponentId` |
| reference to a `ShapeComponent` | `Byte` | `#003628` dark green | `RoamAreaId`, `AttackAreaId`, `TriggerShapeId`, `ElevationShapeId`, `BoundsShapeId` |
| reference to a `Program` (hook) | `Delegate` | `#FF1414` red | `OnCollide`, `OnLoad`, `Program` |
| reference to an asset by name | `String` | `#FF00A8` magenta | `TextureName`, `Name`, `ItemName`, `DestinationSceneName`, `SpawnPointName` |
| transform | `Vector3` / `Rotator` | `#FF9704` / `#5A74FF` | `Position`, `Scaling`, `Rotation`, `Depth` |
| `Vector2` (world plane) | `Vector3` | `#FF9704` | `Position{X,Y}`, `LocalAabb` |
| `FloatColor` | `Color` | `#0088FF` | `BaseColor`, `LightComponent.Color`, `DiffuseColor` |
| cross-object / group edge | `Object` | `#0066E8` | `SceneObjectGroup.ObjectIdentifier`, `Scene.Find(...)` |
| enum / flag int | `Byte` | `#003628` | `Collides`, `ReceivesDamage`, `DamageType`, `Type`, `Trigger` |
| plain scalar | `Float` / `Int` / `Boolean` | | `NormalRunSpeed`, `MaxHealth`, `ExecuteOnce` |

Purely for correctness of colour/affordance, but worth stating: `Boolean` should only be
used where the field is semantically boolean *and* the on-disk encoding is 0/1; the pill
must render `1`/`0`, not `true`/`false` (`01-…` §4).

### 4.2 The reference-field set (129 fields / 54 classes)

Derived by matching `(Id|Ids|Identifier)$` against the binary-derived schema. Fields are
listed `Name[tag]`. Ordered by slot.

```
AnimationControllerComponent       ModelId[1], DefaultAnimationId[2]
AttackComponent                    AnimationId[1], CollisionShapeId[2], AttackAreaId[3], SoundEffectId[4]
BatMonsterControllerComponent      FlyAnimationId[1], FlapSoundId[2]
BlendAnimationComponent            Animation1Id[1], Animation2Id[2]
BoneControlledCollisionShapeComponent  ControllingModelId[1]
BouncingMonsterControllerComponent JumpAnimationId[1], FallAnimationId[2]
BushControllerComponent            WobbleAnimationId[1], WobbleSoundId[2], CutSoundId[3]
CharAnimControllerComponent        StandAnimationId[4], WalkAnimationId[5], JumpAnimationId[6],
                                   FallAnimationId[7], CastAnimationId[8], AirJumpAnimationId[9]
CharControllerComponent            DefaultAnimationControllerId[1], RightWeaponControllerId[2],
                                   LeftWeaponControllerId[6], EntityId[7], SwingComponentId[8],
                                   LiftAnimationControllerId[9], LiftAnimationId[10], DropAnimationId[11],
                                   ThrowAnimationId[12], HurtAnimationId[13], DieAnimationId[14],
                                   PushAnimationId[15], JumpSoundId[18], AirJumpSoundId[19],
                                   JumpLandSoundId[20]
ChargingMonsterControllerComponent WalkAnimationId[1], ChargeAnimationId[2], RunAnimationId[3]
CollectableItemComponent           Identifier[4]  (item-definition id, NOT a component ref — see §4.3)
Component                          Identifier[2], ParentComponentIdentifier[4]
DoorControllerComponent            AnimationControllerId[1], AnimationId[2], CloseSoundId[5], OpenSoundId[6]
EditorViewState                    SelectedObjectIdentifier[1], InspectedObjectIdentifier[2],
                                   InspectedTemplateIdentifier[3]      (editor-only; never in assets)
ElevatorControllerComponent        ElevationShapeId[1]
EntityControllerComponent          EntityId[1], AnimationControllerId[2], DefaultMoveAnimationId[3],
                                   RoamAreaId[4]
FireBreathComponent                ParticleEmitterId[1], SwooshSoundId[2]
FireEmitterComponent               ParticleEmitterId[1], LightId[3]
GenericMonsterControllerComponent  WalkAnimationId[1]
GroundMeshGeneratorComponent       GroundPolygonId[1], TargetMeshId[2], FrontTextureMappingId[3],
                                   SurfaceTextureMappingId[4]
GuideTarget / _LevelObject        ObjectIdentifier[4]/[2], CarryObjectIdentifier[5]
ItemDropComponent (+_ItemDropEntry) ItemIdentifier[2]
Item / PlayerProfile / GameState / GUIViewLayout   Identifier[...]  (data-model ids, not scene refs)
KeyframeAnimationComponent         ModelId[1]
LeapingMonsterControllerComponent  WalkAnimationId[1], LeapAttackId[2]
MagicBoltComponent                 ParticleEmitterId[1], SwooshSoundId[2], HitSoundId[3]
MagicExplosionComponent            ParticleEmitterId[1], SoundId[2]
MagicHookshotComponent             ParticleEmitterId[1], SwooshSoundId[2], HitSoundId[3], GroundHitSoundId[5]
MagicSpellCastComponent            ParticleEmitterId[1], SoundEffectId[2]
ModelTransformControllerComponent  ModelId[1]
MonsterControllerComponent         AnimationControllerId[2], EntityId[3], RoamAreaId[4]
MonsterDeathControllerComponent    ParticleEmitterId[1]
ObjectLinkControllerComponent      TargetObjectIdentifier[1], TargetBoneIdentifier[2]
ParticleEmitterComponent           ParticleId[2], ModelBindingId[3], ParentEmitterId[5]
ParticleObjectComponent            ModelId[1]
PortalComponent                    TriggerShapeId[4]
PortalEffectComponent              PolygonId[1], TextureMappingId[2]
SceneObject                        Identifier[2]   (component-scope root)
SceneObjectGroup                   Identifier[1], ObjectIdentifier[2]
ShootingMonsterControllerComponent WalkAnimationId[1], ShootAnimationId[2]
SkellyMonsterControllerComponent   CharControllerId[1], AttackAreaId[2]
SkillComponent                     CastFinishAnimationId[1]
SnappingMonsterControllerComponent StandAnimationId[1], AttackAnimationId[2], BlendAnimationId[3],
                                   AttackAreaId[4], AttackSoundId[5]
StaticMonsterControllerComponent   AnimationId[1], SoundId[2]
SwingComponent                     AnimationId[1]
SwingableWeaponComponent           ModelId[1], TrailId[2], ImpactParticleEmitterId[4], SwingSoundId[5],
                                   DamageImpactSoundId[6], GlowTrailId[7], CollisionShapeId[8], GlowId[9]
SwingableWeaponControllerComponent ControllingModelId[1]
WalkingMonsterControllerComponent  WalkAnimationId[1]
WaterMeshComponent                 BoundsShapeId[1], TextureMappingId[2]
WeaponGlowComponent                ParticleEmitterId[1]
```

### 4.3 Three reference *kinds* — and why the taxonomy is required

The list above is not homogeneous. Blindly wiring all 129 would create false edges.
Three classes:

**(a) Intra-object component edge.** The overwhelmingly common case. The value is a
`Component.Identifier` **within the same `SceneObject`**. Proof: in `hiro.scl`, all 34
non-zero reference values resolve inside their own object, including
`EntityId : 5` → component 5 = `HeroEntity`. Also in `hiro.scl`, `Identifier 101` is used
by three *different* components (`Model`, and two more) — so the table **must** be keyed
`(object_identifier, component_identifier)`, never by identifier alone.

**(b) Named-asset edge.** `*SoundId`, `LightId`(sometimes), `ModelBindingId`,
`TextureMappingId` partially: some of these index into a name table
(`SoundEffectComponent.Name : 'door_close'`) rather than a sibling component. Detected
because the value has no sibling with that identifier in the object. Render as a
**dangling named edge** with the value shown, not as an error.

**(c) Extra-scene / data-model id.** `ItemDropComponent.ItemIdentifier`,
`CollectableItemComponent.Identifier`, `PlayerProfile.Identifier`, `GuideTarget.
ObjectIdentifier`, `*EntityId` on `SceneObjectGroup`. These live in a different namespace
(item table, profile, world map). Render as a **typed literal badge**, never a wire.
`EditorViewState` is editor-only and must be excluded outright.

**The classifier cannot come from the field name alone.** Algorithm:

```
classify_ref(owner_class, field, value, object_components):
    if owner_class in EDITOR_ONLY:                     return DataLiteral
    if field in KNOWN_ASSET_REF:                       return NamedAsset
    if field in KNOWN_DATA_MODEL_REF:                  return DataLiteral
    if value in object_components.by_identifier:       return ComponentEdge
    if value in scene_index.all_components:            return ExtraObjectEdge  # dangling -> dashed
    if field.endswith("SoundId"):                      return NamedAsset
    return Ambiguous                                     # quarantine, show badge
```

`KNOWN_ASSET_REF` / `KNOWN_DATA_MODEL_REF` are small curated overlays on top of the
generated schema, and they are the **only** hand-maintained part. Everything else is
derived. This is a deliberate inversion of the current design, where the hand-maintained
list is the *entire* model.

---

## 5. Auto-wire resolution algorithm

Two passes over the object, O(n·k).

```
resolve_object(obj: ObjectNode, scene_index: SceneIndex) -> WireResult:

  # pass 1 — index
  by_ident: Map<int, ComponentNode>          # within THIS object only
  for c in obj.components:
      if c.identifier in by_ident:
          dupes.append(c)                     # asset bug; keep both, mark ambiguous
      by_ident[c.identifier] = c

  # pass 2 — resolve
  for c in obj.components:
      for (field, values) in c.payload_fields.where(is_reference_shaped(field)):
          for v in values:                    # repeated -> multiple wires, order-preserved
              kind = classify_ref(c.class_name, field, v, by_ident)
              switch kind:
                ComponentEdge:
                    target = by_ident[v]
                    emit Wire(from=target.output_pin,
                              to=c.input_pin(field),
                              type=pin_type_for(field),
                              multiplicity=index_of(v))     # keeps 3x SwingComponentId
                ExtraObjectEdge:
                    emit DanglingWire(target_hint=v, dashed=True, reason="not in object")
                NamedAsset:
                    emit Badge(c, label=humanise(field), value=asset_name_of(v))
                DataLiteral:
                    emit Badge(c, label=humanise(field), value=v, style=literal)
                Ambiguous:
                    emit Badge(c, label=humanise(field), value=v, style=warn)
                    diagnostics.warn(c, field, v)

  # structural edge, always first
  for c in obj.components:
      if c.parent_component_identifier != 0:
          emit Wire(from=by_ident[c.parent_component_identifier].structural_pin,
                    to=c.structural_pin, type=Delegate)   # Component.ParentComponentIdentifier[4]
```

### 5.1 Invariants

- **Resolution is object-scoped.** A reference never escapes its `SceneObject`. Any
  cross-object edge must come from an explicit source: a `Scene.Find("...")` string in a
  Lua body, or a `SceneObjectGroup.ObjectIdentifier`, or a `Portal( DestinationSceneName,
  SpawnPointName )`. Each of those gets its own wire kind and its own colour.
- **Multiplicity is preserved.** `hiro`'s `SwingComponentId : 8, 15, 6` produces three
  wires. The pin must therefore be repeatable: the `Pin` struct gains
  `int multi_index` (0-based ordinal within its field) and the UI groups them under one
  labelled pin stack.
- **Ambiguity is surfaced, never silently dropped.** The current converters silently
  drop anything they cannot resolve; the graph must show a badge.
- **Deterministic output.** Sort referenced pins by `(tag, multi_index)`; sort nodes by
  `(object ordinal, component identifier)`. Two runs on the same file must produce
  byte-identical `GraphDocument` JSON — this is what makes the migration test in §9
  possible.

### 5.2 Diagnostic set

| Code | Meaning | Severity |
| --- | --- | --- |
| `SCL001` | duplicate `Component.Identifier` within one object | warn |
| `SCL002` | reference value has no target in the object | warn (dangling wire) |
| `SCL003` | `ClassName` not present in the generated registry | error (node still renders) |
| `SCL004` | component has zero payload blocks | info |
| `SCL005` | `TemplateName` has no matching template | warn |
| `SCL006` | `Bytes` chunk signature is not `\x1bLuaQ…` | error |
| `SCL007` | hook `String` present but `Bytes` absent (or vice versa) | warn |
| `SCL008` | oscillation detected (`a → b → a` via structural edges) | error, refuses auto-layout |

---

## 6. Node and wire presentation

### 6.1 Node structure (three levels)

```
┌─ TemplateNode "hiro.scl"  (CommentFrame, not a node) ─────────────────┐
│  ┌─ ObjectNode  `hiro`        subtitle: Tpl — · 44 components        │
│  │     in:  ⟨Structural: ParentObject⟩                               │
│  │     out: ⟨Object Ref⟩  ⟨Component Chain⟩                          │
│  └───────────────────────────────────────────────────────────────────┘
│  ┌─ ComponentNode  `CollisionShape  [ID: 2]`  …  ─────────────────┐   │
│  │     in:  ⟨ControllingModel [101]⟩ ⟨Parent Component [1]⟩       │   │
│  │     out: ⟨ID 2⟩                                                │   │
│  │     hook: [OnCollide] [OnCollisionEnd] [OnReceiveDamage]        │   │
│  └────────────────────────────────────────────────────────────────┘   │
└───────────────────────────────────────────────────────────────────────┘
```

- **`ObjectNode`** is the only node with an object-scoped name. Title = `Identifier`;
  subtitle = `[TemplateName | "inline"] · Nn components · ID space 1..129`.
- **`ComponentNode`** title = `ClassName`; subtitle = `ID: <n>` plus at most one asset
  name; badge row for `Label` if present.
- **Multi-block components** get a `SubBlockNode` per payload block
  (`ShapeComponent`, `MonsterControllerComponent`, …), auto-laid-out inside the component
  node as a **vertical child region** (Godot `GraphNode` slot model, `08-…`), not as
  separate top-level nodes. This keeps a 44-component object navigable.
- **Hook affordance** is a chip in the component body, one per hook field present on that
  payload. It is *enabled* only in the Scene backend; in the SCL backend it is a
  read-only indicator. The chip is the entry point of `03-…` §4.

### 6.2 Pin naming rule (fixes the duplicate-tag bug at the source)

One rule, applied at graph-build time:

> A pin carries **either** a label **or** a value, never both in the same glyph.
> If the value is short and the label is redundant with it, the label *is* the value.

Concretely, replace `scene_to_graph.py`'s

```python
{"name": f"Asset: {name_val}", "tooltip": …, "default_value": name_val}
```

with

```python
{"name": "Asset", "value": name_val, "show_value": True, "pin_type": PIN_STRING}
```

so `draw_nodes()` renders `Asset  npc_elder` once. Same for `Pos`: the label becomes
`Pos` and the value pill becomes the *only* place the numbers appear, with width computed
from `QFontMetrics::horizontalAdvance()` and an explicit `Qt::ElideMiddle` fallback —
never a fixed 48px box (`07-…` §3).

### 6.3 Wire kinds and styling

| Kind | Source | Style | Colour |
| --- | --- | --- | --- |
| `ComponentEdge` | resolved integer id in-object | solid, 2.2px, arrow on exec-less | type colour from §4.1 |
| `StructuralEdge` | `Component.ParentComponentIdentifier` | solid, 3px, square cap | `Delegate` white/grey |
| `ContainmentEdge` | object → its components | thin, dashed 4/4 | `#4D4A4A` Wildcard grey |
| `ScriptEdge` | `Scene.Find("x")` heuristic | dashed 6/4, hover shows the Lua line | `Object` blue |
| `GroupEdge` | `SceneObjectGroup.ObjectIdentifier` | dotted | `Object` blue, dimmed |
| `DanglingEdge` | unresolved ref | dashed, red halo, no arrowhead | `#FF1414` |
| `AssetRef` | name reference | **not a wire** — badge | `String` magenta |

Rendering rules for wires are in `07-…` §4 (reparameterised cubic for uniform hit-testing,
hover halo, 24-step polyline replaced).

### 6.4 Layout

Deterministic, no physics:

- One `CommentFrame` per `Template` (SCL) or per object-cluster (Scene). Frame colour from
  the class family of the busiest component.
- Inside a frame: `ObjectNode` in column 0; component nodes in columns 1…4 grouped by
  *class family* (Resources / Emitters / Physics / Scripting / Gameplay), each column a
  vertical stack with `gap = 24`, matching `scl_to_graph.py`'s existing column intent but
  with measured heights instead of the `36 + rows*24` guess.
- Node height comes from the **new layout engine**, not the JSON (`07-…` §2). The graph
  document therefore stops carrying `height` at all; it carries only `x`/`y` hints.
- After automatic placement, run a **collision-avoiding pass** modelled on
  `GraphEditArranger::arrange_nodes()` (`scene/gui/graph_edit_arranger.h:39,62`) so
  long-label nodes never sit under a sibling column. Purely local, O(n log n): sort by
  column, sweep downward, push down on overlap, then relax.

---

## 7. Backend interface (shared canvas)

The canvas must stop knowing what it draws. Introduce the seam:

```cpp
namespace ruby::graph {

// Everything the canvas needs; produced by either backend.
struct GraphDocument {
    std::shared_ptr<Graph> graph;
    std::vector<Diagnostic> diagnostics;
    DocumentKind kind;                 // Scl | Scene
    // Scene-only, empty for Scl:
    std::shared_ptr<class TimelineDocument> timeline;
    std::shared_ptr<class SceneWorldIndex> world;
};

class IGraphBackend {
public:
    virtual ~IGraphBackend() = default;
    virtual GraphDocument build(const BackendSource& src, BuildOptions) = 0;
    virtual void rebuild_incremental(GraphDocument&, const ChangeSet&) = 0;
    // node hit-test -> what the object is, for context menus
    virtual NodeAddress address_of(int node_id) const = 0;   // {object, component, hook?}
    virtual QMenu* context_menu_for(NodeAddress, QWidget* parent) = 0;
};

class SclBackend : public IGraphBackend { … };
class SceneTimelineBackend : public IGraphBackend { … };   // 03-…

}
```

`GraphyCanvas` keeps: pan/zoom, grid, comments, wires, marquee, slice, minimap (docked —
`07-…` §4), and adds one indirection: `NodeAddress`. `NodeFlags` gains
`HookChip`, `Badge`, `SubBlock`. The Scene backend contributes additional paint passes
through a `IOverlayRenderer` the canvas calls between wires and nodes.

**Why this shape:** it is the smallest seam that lets the timeline backend own ghosts,
tracks and preview while reusing every line of Graphy's painter. It also matches the
reference architecture (Unreal separates `SGraphPanel` from the schema/`ISequencerTrackEditor`,
`08-…` §3).

---

## 8. Open questions

1. **`Transform` / `TransformController` have no payload block** in 58 observed instances,
   yet exist as `ClassName` tokens. Either the decoder is missing a payload message
   (likely — check `swordigo_symbols.txt` for a `TransformControllerComponent` and whether
   `extract_component_schema.py` maps a slot for it) or they are genuinely empty markers.
   Until resolved, render them as payload-less nodes with an `SCL004` info diagnostic.
2. **`UtilityShape` → `ShapeComponent`** but there is no `UtilityShapeComponent` slot. Is
   `UtilityShape` a `ClassName` alias for `Shape`? Only 36 instances, all alone. Needs an
   IDA/behaviour check.
3. **`ShatterComponent`** appears once as a `ClassName` and is absent from the 81-slot
   registry. Possible decoder forward-compat artefact.
4. **Which `*SoundId` are component refs vs name-table indices?** `hiro` resolves
   `JumpLandSoundId : 21` to `SoundEffect` id 21, so at least some are component refs whose
   *target* holds the name. The classifier in §4.3 prefers component resolution first,
   which is correct for the corpus, but the ambiguity should be logged so a wrong guess is
   visible.
5. **`ParticleEmitterId` vs `ParticleId`** — both exist; `ParticleId` points at `Particle`
   components (id 3,4,123 in `hiro`), `ParticleEmitterId` at `ParticleEmitter` components
   (`blackhole` has 120). Not a problem for wiring, but the two names must not be
   normalised to one label in the UI.

---

## 9. Migration plan and acceptance tests

**Order**

1. Add `graphy_layout.{h,cpp}` + fix bug 1–3 (`07-…`). Land independently: all existing
   graphs get correct layout with no backend change. Regression: `bin/scl_studio_test`,
   `bin/scl_graph_viewer` on the four committed `*_graph.json` files.
2. Generate `graphy_schema.h`. Add a unit test asserting 81 slots and that
   `CollisionShape → {CollisionShapeComponent, ShapeComponent}`.
3. Implement `SclBackend` in C++ reading the decoded text directly. Emit
   `GraphDocument` JSON in the same schema as today so the canvas needs no change yet.
4. **Golden migration test**: for each `decoded_rln/*.scl`, run `scl_to_graph.py` and
   `SclBackend`, and assert the new graph is a **superset** — every old node/wire present,
   plus the newly discovered edges. Assert the resolved-edge count equals the expected
   number computed independently by a test-side reference implementation.
5. Delete `REF_FIELDS` from the Python converters and route the viewer through
   `SclBackend`.

**Acceptance criteria**

| # | Criterion |
| --- | --- |
| A1 | `hiro.scl` produces 3 wires for `SwingComponentId` (6, 8, 15), not 1 |
| A2 | `hiro.scl` resolves `EntityId : 5` → the `HeroEntity` component node |
| A3 | `hiro.scl`'s three distinct components sharing `Identifier 101` produce zero cross-object wires |
| A4 | `CollisionShape` nodes expose both `ShapeComponent` and `CollisionShapeComponent` data |
| A5 | Every `ClassName` in the 51-file corpus resolves to a registry entry or emits `SCL003` |
| A6 | Two builds of the same file produce byte-identical `GraphDocument` JSON |
| A7 | Rendered output contains no truncated pill and no overlapping label/pill (`07-…` probe) |
