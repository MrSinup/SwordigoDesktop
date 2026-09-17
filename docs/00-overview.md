# 00 — Overview: Graphy Upgrade + Visual Timeline Scripter

Status: **specification / implementation plan**. No production code written yet.
Audience: whoever implements this. Read this file first; every other file assumes it.

---

## 1. What we are building, in my own words

SwordigoDesktop is a native Linux reimplementation of the 2013 mobile game *Swordigo*.
It does not re-implement the game's logic from scratch — it loads the shipping
`libswordigo.so` (ARM64) into a custom ELF loader and executes it on a swappable
ARM64 translation backend (Dynarmic / Unicorn), with an in-process JNI bridge and an
SDL/OpenGL host. On top of that it runs **SRE** — a "Swordigo Runtime Engine" `so`
(`libsre13.so` / `libsre12.so`) that is `dlopen`ed into the guest process and whose
entry points the host trampoline-patches into the game. SRE's most important job here
is to **replace and wrap the game's Lua 5.1 entry points**: `sre_ProgramState_Execute`,
`sre_ProgramState_Resume`, `sre_lua_call_safe`, `sre_lua_resume_safe`. The game's own
Lua VM (a stock Lua 5.1, vendored in-tree at `src/sre/base/lua/src`, LUA_VERSION_NUM 501)
is what actually runs every script the game ships.

The debug overlay is **SwordfareGUI** (`src/platform/swordfare_gui.cpp`, ImGui-driven),
and it already hosts a live Lua REPL ("Raijin") that evaluates commands inside the
running game.

**Graphy** is a separate, Qt-based, custom node-graph backend under `src/ruby/graph/`
(`graphy.{h,cpp}` data model + `graphy_canvas.{h,cpp}` `QWidget` renderer). It currently
renders *one* kind of graph: the ECS structural graph. Two Python converters feed it
Graphy-JSON — `tools/scl_to_graph.py` (for `.scl` template libraries) and
`tools/scene_to_graph.py` (for `.scene` placed levels). A standalone host,
`src/tools/scl_graph_viewer.cpp`, displays those JSON files.

So we are **not** writing a node editor from scratch. We are:

1. **Fixing** Graphy's layout engine (four confirmed, reproducible bugs — see §4).
2. **Formalising** Graphy into *two backends sharing one canvas*:
   - **SCL backend** — one node per `Object` in an `ObjectTemplate`, sub-nodes per
     `Component`, wires = the integer `Identifier` cross-references inside fields.
     Non-spatial: a template in isolation has no world transform.
   - **Scene backend** — `Object`s placed at real `Position`/`Depth`, each
     referencing a template (`TemplateName`) or defined inline. **This backend hosts
     the scripting tool.**
3. **Building the scripting UX**, which is *deliberately not* a blueprint/logic graph.
   It is a **visual timeline / motion editor**:
   - Every **hook slot** on a component (there are 21 of them; §3) gets a button.
   - Clicking it spawns a **ghost/proxy** of the object's real model into an isolated
     preview canvas, seeded at the object's real scene transform.
   - The user **scrubs and drags the ghost** — position, rotation, scale — drops
     **keyframes** on a timeline track, marks **loop regions**, and drops discrete
     **action markers** (a function call + arguments) at timestamps.
   - A **Ruby process** converts this recording into an intermediate format,
     **`.rbsrc`**, which is deliberately *richer than Lua*: it retains the keyframe
     list `(time, property, value, easing)`, loop markers, action markers
     `(fn, args, time)`, and **provenance** (source object + hook + template). Because
     provenance and structure are retained, `.rbsrc` is **re-openable and re-editable**
     rather than a one-way export.
   - A **compiler** walks `.rbsrc` chronologically and emits Lua 5.1 text. Keyframe
     runs become `for`/`Program.Wait` tick loops with interpolated values — matching
     the real `blackhole` pattern found in `rlsw.scl`. Action markers become gated
     calls at the right timestamp — matching the real `item1` pattern in
     `town_shop.scene`. The Lua text is compiled to bytecode with `luac5.1` and **both**
     `String` (source) and `Bytes` (chunk) are written back into the target `Program{}`.
   - **Right-click injection**: right-click an object node → pick a hook slot →
     "Assign `.rbsrc`" → browse compiled scripts → recompile against *that* object's
     `self` context and write into its hook block. One `.rbsrc` is reusable across
     objects because `self` is bound at compile time, not at authoring time.
   - **Live injection**: push freshly-compiled bytecode into the *running* game's
     `SceneObject` via the SRE interpreter, without a scene reload.

The single sentence version: **we are turning a static ECS-graph viewer into an
animation-sequencer-style scripting environment whose output is real Lua bytecode that
can be hot-patched into the live game.**

---

## 2. Terminology (use these words consistently)

| Term | Meaning |
| --- | --- |
| **FileRift** | The decoded text markup dialect for Swordigo's protobuf assets. Not a standard engine format. |
| **`.scl`** | *Scene Component Library* — a library of reusable object templates. Root: `ObjectLibrary`. |
| **`.scene`** | A placed level. Root: `Scene`. |
| **Object / `SceneObject`** | An entity: `Identifier`, optional `TemplateName`, transform, components. |
| **Component** | A typed struct on an object: `ClassName` + integer `Identifier` local to that object. |
| **Hook** | A `Program`-valued field on a component or object (`OnCollide`, `OnLoad`, …). 21 exist. |
| **Backend** | One of Graphy's two graph producers (SCL / Scene). Both feed one renderer. |
| **`.rbsrc`** | *Ruby Script Source* — our intermediate recording format. Re-openable. |
| **Ghost / proxy** | The live, draggable, non-authoritative preview instance of an object's model. |
| **Action marker** | A timestamped function call with arguments (`fn, args, time`). |
| **Loop region** | `[t0, t1)` span that the compiled Lua must repeat. |
| **Injection** | Writing compiled bytecode into a `Program{}` block (offline) or into a live object (online). |

---

## 3. Ground truth recovered during research (correcting the brief)

The task brief was written from memory and is **partly wrong** in ways that would
have sent an implementer down the wrong path. All corrections below are measured, not
guessed — I decoded the real assets with `bin/ruby_cli` and derived the schema from
`libswordigo.so` symbols with the project's own `tools/extract_component_schema.py`.

### 3.1 There are **two** FileRift text dialects in this tree

- **Current** (what `bin/ruby_cli` v5.8.6 emits, and what Graphy must consume):
  a `## FileRift decoded Swordigo file type: <type>` banner, **single-quoted** strings,
  **no trailing commas**, no per-block `##` annotations.
- **Legacy** (present in the committed `decoded_rln/*.scl` corpus): **double-quoted**
  strings, **trailing commas**, and per-block comment annotations (`## SceneObject`,
  `## Component`, `## CharControllerComponent`).

`tools/scl_to_graph.py` and `tools/scene_to_graph.py` only tolerate the legacy shape
in places (they strip one layer of quotes and accept both, but the trailing commas and
`##` labels are not modelled). See `01-filerift-format-spec.md` §7.

### 3.2 The brief's hook list is incomplete and partly fictional

The brief lists `OnLoad, OnCollide, OnHurt, OnKill, OnCollect, OnCollisionEnd, OnTouch,
OnItemGet` plus "a generic `ProgramComponent`/`Program`". Measured reality from the
1.4.13 schema — **21 hook slots across 17 owner classes**:

`Scene.OnLoad(5)`, `SceneObject.OnLoad(10)`, `SceneObjectGroup.OnLoad(4)`,
`ObjectLibrary.Program(5)`, `PropertiesComponent.OnLoad(1)`,
`CollisionShapeComponent.OnCollide(9)/OnCollisionEnd(10)/OnReceiveDamage(12)`,
`GroundPolygonComponent.OnCollide(6)`, `CollectableItemComponent.OnCollect(3)`,
`MonsterEntityComponent.OnKill(1)/OnHurt(2)`, `HeroEntityComponent.OnItemGet(1)`,
`EntityActionComponent.OnActivate(1)`, `PressureTriggerComponent.OnPress(2)/OnRelease(3)`,
`TouchableComponent.OnTouch(2)`, `AttackComponent.OnAttack(11)`,
`BreakableObjectComponent.OnBreak(4)`, `SpellComponent.OnCast(1)`,
`ProgramComponent.Program(2)`.

Missing from the brief: `OnReceiveDamage`, `OnCollisionEnd` on `GroundPolygon`,
`OnActivate`, `OnPress`, `OnRelease`, `OnAttack`, `OnBreak`, `OnCast`. The brief's
`OnHurt`/`OnKill`/`OnItemGet`/`OnTouch` do exist, but on classes it did not name
(`MonsterEntityComponent`, `HeroEntityComponent`, `TouchableComponent`). Also note the
brief calls the generic block "`ProgramComponent`/`Program`" — in the schema those are
two different messages: `ProgramComponent` is a *component class* (slot 157) whose
`Program` field (tag 2) holds the script; `Program` itself is the message
`{ String(1), Bytes(2), Name(3) }`.

### 3.3 `Program` blocks are **not** all on `ProgramComponent`

This is the single most important correction for the timeline backend. Hooks are
fields on the *owning component* (`CollisionShapeComponent.OnCollide` is tag 9 of the
collision shape), **and** on `SceneObject` itself (`OnLoad`, tag 10). A timeline editor
that only looks for `ProgramComponent` would miss `SceneObject.OnLoad` and every
component-embedded hook. `town_shop.scene`'s `item1` is a `ProgramComponent`
(`ExecuteOnce : 1`), but `hiro.scl`'s `CollisionShape` `OnCollide` is a plain field on
the collision shape.

### 3.4 `Component.ParentComponentIdentifier` exists and was omitted from the brief

`Component` = `{ ClassName(1), Identifier(2), Label(3), ParentComponentIdentifier(4) }`.
Tag 4 is a real structural parent edge (already used by `scene_to_graph.py`'s
`REF_FIELDS`), and there is an unmapped `Label(3)` string. Also, `Identifier` is
**repeated** on some classes (`CharControllerComponent.SwingComponentId` appears 3× in
`hiro.scl`), so the graph model must handle **multi-edges**, which
`scene_to_graph.py` silently drops by storing `comp_ref_inputs[ref_key] = (...)` in a
dict keyed by field name.

### 3.5 The cross-reference surface is 10× bigger than the current converter assumes

`tools/scene_to_graph.py` hand-maintains `REF_FIELDS` with **13** entries.
`tools/extract_component_schema.py` measures **129 reference-shaped fields across 54
classes** out of **133 classes / 624 fields** total. The plan therefore **generates**
the pin/wire schema from the binary instead of transcribing it. Table in
`02-graphy-structural-backend.md` §4.

### 3.6 `TemplateName` is usually `'Template 1'`, not a real template

In `town_shop.scene`, `Background`, `DirectionalLight`, `iapstoremodel` etc. all carry
`TemplateName : 'Template 1'`, while `item2`/`item3` carry `TemplateName : 'shop_item'`
and `item1` carries **no `TemplateName` at all** (fully inline). The Scene backend
therefore cannot treat `TemplateName` as "resolves to a template object" without a
resolution step; `'Template 1'` is the legacy "everything had a template" artifact.
See `01-…` §6.

### 3.7 `Bytes` is genuinely Lua 5.1, and the off-diagonal detail is the `\r\n`

Decoded `item1` shows `Bytes : '\x1bLuaQ\x00\x01\x04\x04\x04\x08\x00@…'` — standard
Lua 5.1 signature `\x1bLua` + version `0x51`, endianness `0x01`, `sizeof(int)=4`,
`sizeof(size_t)=4`, `sizeof(Instruction)=4`, `sizeof(lua_Number)=8`, integral flag `0`.
And the embedded debug/source-name region contains `local self = ...;\r\n\r\n…` — the
`String` field preserves **CRLF** line endings. Any write-back that normalises line
endings will change the byte length recorded in the chunk header and corrupt the debug
section. `05-compiler-pipeline.md` treats `\r\n` as load-bearing.

### 3.8 Matinee does not exist in the reference tree

`find …/UnrealEngine-release/Engine/Source -iname '*Matinee*'` returns nothing, and
neither `InterpTrack.h` nor `UInterpTrackMove` exists. This reference is UE5-era;
Matinee and `UInterpTrack*` were deleted. The brief said "and, if present, Matinee" —
it is **not present**, so `08-reference-study-notes.md` documents the Sequencer and
MovieScene replacements only, and notes where legacy Matinee concepts (`FInterpTrack`,
key-in-time-only curves) survive conceptually inside `MovieScene` channels.

---

## 4. The four Graphy bugs — root causes proven, not guessed

Source of truth: `src/ruby/graph/graphy_canvas.cpp`, and the real graph JSON
`town_elderhouse_graph.json` (85 nodes, 60 connections, 36 comment frames) which **is**
the graph in the reported screenshot (`Tpl: npc_oldman · (Z: -29.9)`, `Asset: npc_elder`
both appear verbatim).

I reproduced Graphy's geometry offline
(`.scratch/graphy_recon/graphy_layout_probe.py`) against that file:

| # | Symptom (screenshot) | Root cause | Measured |
| --- | --- | --- | --- |
| 1 | Subtitle collides with title / first pin row | `header_h` is a **constant 32** in `update_node_layout()`; title is `VAlignCenter` over all 32px, subtitle is `VAlignCenter` inside `header_rect.adjusted(12, 14, -12, 0)` — i.e. an ad-hoc +14px nudge inside the *same* band, minus a reserved line | **85/85 nodes** overlap; title band `[y, y+32)` and subtitle band `[y+14, y+32)` share 18px |
| 2 | Numeric pills truncated mid-character, no ellipsis | `pill_w` is a hard-coded `48.0f` and text is drawn `Qt::AlignCenter` (clips, never elides). The pin-name label rect `[x+10, x+w*0.5-4)` also runs *under* the pill at `[x+w*0.40, x+w*0.40+48)` | **88 pills** exceed 48px; `'919.5, 394.5, -29.9'` needs 93px and renders as `5, 394.5, -2`; **117** label/pill rect collisions |
| 3 | `Asset: npc_elder` immediately followed by a duplicate `npc_elder` tag | `scene_to_graph.py` puts the value in the pin **name** *and* in `default_value`, so `draw_nodes()` renders the same string twice | `'Asset: npc_elder'` → name `Asset: npc_elder`, pill `npc_elder` showing `npc_elde` (clipped) |
| 4 | Stray histogram/equalizer widget bottom-right of the canvas | It is `draw_minimap()`, step 7 of `paintEvent`, drawn **after `p.restore()`** in screen space, always when `m_show_minimap`, with no `setClipRect` and no docking. Each node is a raw `p.drawRect(...)`, so ~85 node rects read as a bar chart | 85 `drawRect` calls per frame in a 170×115 box |

The layout rule that is actually wrong is: **the node's vertical flow does not account
for its own text content.** `update_node_layout()` computes height from *pin count*
only (`32 + rows*22 + 8`) and then `set_size(max(json_height, calc_h))`; the JSON
heights from `scene_to_graph.py` are a *different* formula (`44 + rows*24`), so 13 nodes
carry a >8px dead band. Two competing layout authorities and a fixed header. Fixes are
layout-engine rules, in `07-graphy-bugfixes.md`.

---

## 5. Ambiguities I could not resolve without you

Flagging these now because they change the spec. Each has a recommended default so work
is not blocked.

1. **Where does the Ruby process live?** The brief says "a separate process (written in
   Ruby)", but **there is no Ruby-language runtime in this project at all**. *Ruby* is
   the project's own editor suite: `bin/ruby` (Godot-based), `bin/ruby_cli` (headless
   FileRift CLI), and *Ruby GG* (`src/ruby/`, Qt6, `bin/ruby_gg`) — the name has
   nothing to do with the Ruby language. (Bare `ruby` resolves to `/usr/bin/ruby`, a
   root-installed 1 MB launcher that links `libswcore.so` and therefore only runs from
   inside the project tree. That is a packaging artifact, not a broken build product:
   every executable under `bin/` carries a valid RUNPATH and `ldd`s clean.)
   **Default taken:** the
   `.rbsrc` producer/compiler is a **standalone `ruby_cli` subcommand** reading/writing
   `.rbsrc`, implemented in C++ alongside `tools/ruby_cli.cpp`, with a thin Ruby-syntax
   *facade* only if a real interpreter is wanted later. Rationale: it keeps the
   compiler inside the same binary that already owns the byte-exact
   `.scene`/`.scl` encoder, so write-back fidelity is provable with the existing
   `recode` round-trip. See `05-compiler-pipeline.md` §2.
2. **Which SRE ABI is the target?** `src/sre/` contains `sre12` and `sre13`
   (game 1.4.12 and 1.4.13). `sre13` is strictly newer and has the live console with
   the `Program.Wait` shadow. **Default taken:** design against **sre13**, with the
   sre12 differences called out where they matter (`06-…` §7).
3. **Is a `.rbsrc` recorded against a *template* or an *instance*?** `town_shop.scene`
   shows both inline objects and `TemplateName` references. **Default taken:** a
   `.rbsrc` is recorded against an **instance id + hook**, and its *compiled* Lua is
   bound to whatever `self` the target provides — which is exactly what makes it
   reusable. Provenance records both the concrete instance and (if any) the template.
4. **Do we need `luac5.1` externally, or can we compile in-process?** No `luac5.1` is
   guaranteed installed; the vendored Lua 5.1 at `src/sre/base/lua/src` contains the
   full compiler (`lcode.c`, `lparser.c`, `ldump.c`). **Default taken:** link a
   `lua51_codegen` static lib from the vendored sources and call `luaL_loadbuffer` +
   `lua_dump`-equivalent directly, eliminating the external toolchain dependency
   entirely. Details in `05-…` §5.
5. **Scope of "live injection" for v1.** Full hot-swap of a *running* coroutine
   mid-`Wait` is genuinely hard (the SRE's `ProgramState` owns a `coroutine` pointer at
   `+0x08`). **Default taken:** v1 does **object-level hot rebind** — cancel the
   object's `ProgramState`, install the new closure, re-`Execute` from the current
   local time — and explicitly does *not* attempt instruction-level transplant of a
   suspended coroutine. `06-…` §5 states the limitation.

---

## 6. Reference study: what we take, and what we deliberately reject

Full citations in `08-reference-study-notes.md`. Summary:

**Taken from Unreal Sequencer / MovieScene:**
- The **authored-vs-compiled split**. `UMovieScene` is the authored track list;
  `FMovieSceneEvaluationTemplate` (with `Tracks` *and* `StaleTracks`,
  `MovieSceneEvaluationTemplate.h:159,277,280`) is the compiled runtime artifact.
  Our `UMovieScene` is `.rbsrc`; our `FMovieSceneEvaluationTemplate` is the
  `(Lua text, bytecode)` pair plus a `StaleTracks` analogue — the previously compiled
  blob, kept so a live object can be rebind-transitioned instead of hard-cut.
- **Scrubbing is an explicit transaction.** `ISequencer::OnBeginScrubbing()` /
  `OnEndScrubbing()` (`ISequencer.h:749,750`) and
  `SetLocalTime(FFrameTime, ESnapTimeMode, bool bEvaluate)` (`:484`) — our ghost drag
  is a scrub, and `evaluate=false` batches are how we avoid re-running the game loop
  per mouse-move.
- **Right-click = track-editor extension point.** `ISequencerTrackEditor::
  BuildObjectBindingTrackMenu(FMenuBuilder&, TArray<FGuid>, const UClass*)`
  (`ISequencerTrackEditor.h:164`) is exactly our "right-click object → pick hook →
  assign `.rbsrc`" flow, including the object-binding argument.

**Taken from Godot `AnimationPlayer` / `AnimationTrackEditor`:**
- The concrete keyframe encoding: `Animation::Track{ type, interpolation, loop_wrap,
  path, … }`, `TKey<T>{ transition, time, value }`, `MarkerKey{ double time;
  StringName name; }` (`scene/resources/animation.h:107,127,168,247`). Godot 4.7
  animation **markers** are a near-exact precedent for our **action markers**, and
  `AnimationTrackEditor::_fetch_value_track_options()` is the precedent for per-track
  interpolation/loop choice.
- The UX shell: `AnimationTimelineEdit : Range`, `AnimationMarkerEdit : Control`,
  `AnimationTrackEdit`, `AnimationTrackEditGroup`, and `SelectedKey`/`KeyInfo`
  bookkeeping (`editor/animation/animation_track_editor.h:188,290,423,760,766`) — a
  dockable VBox of track rows over a shared timeline ruler, which is precisely the
  panel layout Graphy's Scene backend needs.

**Taken from Godot `GraphNode` / `GraphEdit` (and `GraphEditArranger`):**
- The **layout fix**. `GraphNode::_get_minimum_size(bool p_use_desired_sizes)` and
  `_port_pos_update()` compute port positions from a *measured* titlebar widget:
  `vertical_ofs = titlebar_hbox->get_size().height + theme_cache.titlebar->
  get_minimum_size().height + theme_cache.panel->get_margin(SIDE_TOP)`
  (`scene/gui/graph_node.cpp:1000,1041-1046`). The title is a real `HBoxContainer` +
  `Label`, not absolutely-placed text. That is the fix for bugs 1–3.
- `GraphEditArranger::arrange_nodes()` (`scene/gui/graph_edit_arranger.h:39,62`) is the
  precedent for collision-avoiding auto-layout, which the Scene backend needs because
  objects are placed at world coordinates and will overlap.
- `GraphEdit::get_connection_line()` + `_cache.from_pos/_cache.to_pos` and the
  `get_connection_lines_curvature()/thickness()` settings
  (`scene/gui/graph_edit.cpp:1526,2906,2923`) are the precedent for our
  curved-wire geometry and for **caching port screen positions** instead of
  recomputing them inside `paintEvent` (Graphy currently mutates node layout during
  painting — see `07-…` §5).

**Taken from Unreal `SGraphPanel` / `SGraphNode` / `ConnectionDrawingPolicy`:**
- Pins are **real widgets** in a vertical box: `SGraphNode` holds
  `TArray<TSharedRef<SGraphPin>> InputPins/OutputPins` and
  `TSharedPtr<SVerticalBox> LeftNodeBox/RightNodeBox`
  (`SGraphNode.h:468,470,476,478`), built by `CreatePinWidgets()`, with extension hooks
  `CreateBelowPinControls(TSharedPtr<SVerticalBox>)` (`:360`) and `GetTitleRect()`
  (`:280`). Same conclusion as Godot: stop absolutely positioning, let a flow layout own
  the geometry.
- `FConnectionDrawingPolicy::{DrawConnection, DrawSplineWithArrow,
  MakeSplineReparamTable, DetermineWiringStyle, DetermineLinkGeometry}`
  (`ConnectionDrawingPolicy.h:174-189`; `.cpp:136,315,483,489,507`) plus
  `GraphEditorSettings::SplineHoverTolerance` (`GraphEditorSettings.h:105`). The
  reparameterisation table is the correct way to make wire hit-testing uniform along
  the curve, which Graphy's fixed 24-step polyline approximation gets wrong.

**Deliberately rejected:**
- Godot's `VisualScript` (a logic-node scripter) — removed from Godot 4, so not even
  present in `godot-4.7.2-stable` (no `modules/visual_script`). The brief asks us to note
  its clunkiness as a cautionary tale; the stronger evidence for *not* building a logic
  graph is that both reference engines' own logic-graph systems are the most-hated part
  of each editor, and our target users want motion, not control flow.
- Unreal's Blueprint **type-promotion/conversion** machinery. Graphy has a
  `MakeWithConversion` response and `can_connect_types()`, but the ECS cross-reference
  graph is a *typed identity* graph (every ref is `int → Component`), not a value-flow
  graph. Wiring by coercion would be actively wrong. See `02-…` §5.

**Genuinely original (not copied from either engine), and why:**
1. **Wire = resolved integer identity, not a user-drawn value edge.** Neither engine has
   a graph whose edges are *discovered* from integer foreign keys inside a decoded
   binary format. `02-…` §5 defines the resolution algorithm (scope-limited two-pass
   `Identifier` binding with ambiguity quarantine).
2. **`.rbsrc` as a first-class, re-openable recording with provenance.** Unreal
   serialises authored data to `.uasset` and Godot to `.tres`; neither keeps a
   *re-editable* intermediate that is simultaneously (a) richer than the target
   language and (b) round-trip-stable with the shipping binary. `04-…` specifies it.
3. **Compiling keyframes into a `Program.Wait` tick loop.** This is a direct consequence
   of *this* engine's script model: `blackhole` in `rlsw.scl` already does
   `for map = 1, N do … Program.Wait(0.0001) … end` while growing `self:scaling()`.
   Instead of sampling a curve at N frames, we invert it: choose `N` and `dt` from the
   loop budget and emit an interpolated loop. `05-…` §4 works both real examples.
4. **Live rebind through the SRE console mailbox.** The `(buf, pending, status, result)`
   guest-memory mailbox in `SwordfareGUI::update_console_backend()` +
   `sre13_console_frame_tick()` is an unusual capability (a hot Lua path into a
   *running* translated ARM64 process). `06-…` extends it with a bytecode-load command.

---

## 7. File map: current state vs. what this plan adds

```
src/ruby/graph/graphy.h                 [modify] pin/node/wire model, add backend + track types
src/ruby/graph/graphy.cpp               [modify] schema-driven can_connect, multi-edge support
src/ruby/graph/graphy_canvas.h/.cpp      [modify] layout engine rewrite; split renderers
src/ruby/graph/graphy_layout.h/.cpp      [ NEW  ] content-measured flow layout (float, not textHeight*0.5)
src/ruby/graph/graphy_schema.h           [ NEW  ] generated from component_schema.json
src/ruby/graph/graphy_luainject.h/.cpp   [ NEW  ] Program{} read/write, bytecode round-trip
src/ruby/graph/graphy_timeline.h/.cpp    [ NEW  ] keyframes, loop regions, action markers
src/ruby/graph/graphy_ghost.h/.cpp       [ NEW  ] ghost preview host inside Viewport3DWidget
src/ruby/panels/timeline_panel.h/.cpp    [ NEW  ] track rows + ruler (q.v. AnimationControlBar)
src/tools/scl_graph_viewer.cpp           [modify] SCL backend mode + right-click hook menu
tools/ruby_cli.cpp                       [modify] `rbsrc compile|decompile|inject` subcommands
tools/extract_component_schema.py        [ reuse ] regenerate graphy_schema
tools/generate_graphy_schema.py          [ NEW  ] schema JSON -> graphy_schema.h
docs/*                                   [ NEW  ] this plan
```

`tools/scene_to_graph.py` and `tools/scl_to_graph.py` are **superseded**: their graph
generation moves into the SCL/Scene backends so there is exactly one authority for node
semantics. Keep them only as golden-output comparators for the migration test.

---

## 8. Read order

1. `01-filerift-format-spec.md` — the data we consume and must write back byte-exactly.
2. `02-graphy-structural-backend.md` — SCL backend, pin schema, auto-wire resolution.
3. `03-graphy-scene-timeline-backend.md` — the scripting UX (the point of the project).
4. `04-rbsrc-format-spec.md` — the intermediate format.
5. `05-compiler-pipeline.md` — `.rbsrc` → Lua → bytecode → `Program{}`.
6. `06-live-injection-sre-hook.md` — hot-patching the running game.
7. `07-graphy-bugfixes.md` — the four bugs, as layout-engine rules.
8. `08-reference-study-notes.md` — every citation, mapped to a component.
