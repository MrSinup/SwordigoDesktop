# 08 — Reference Study Notes

Concrete citations from the reference tree at
`/run/media/quantumcreeper/TVPG/Storage/GameEngine-ReferenceForRuby`, mapped to the
SwordigoDesktop component each one informed. Line numbers are from the copies actually
present in that tree.

```
GameEngine-ReferenceForRuby/
├── godot-4.7.2-stable/          <- read for the timeline UX and the node layout engine
├── UnrealEngine-release/        <- read for Sequencer/MovieScene and the graph editor
└── raylib-6.0/                  <- not used (no node/timeline editor surface)
```

**Absent, and worth recording:**

- **Unreal Matinee does not exist in this tree.**
  `find UnrealEngine-release/Engine/Source -iname '*Matinee*'` → nothing; neither
  `InterpTrack.h` nor `UInterpTrackMove` exists. This is a UE5-era branch, where Matinee and
  the whole `UInterpTrack*`/`UInterpGroup*` family were deleted in favour of MovieScene.
  The brief said "and, if present, Matinee" — it is not, so the only Sequencer-era
  references below are MovieScene-based. Where Matinee's concepts survive (key-in-time-only
  curves, `FInterpCurve<T>` per property) they survive as **MovieScene channels**, which is
  what we read instead.
- **Godot's VisualScript does not exist in 4.7.2.** There is no `modules/visual_script`
  directory. The logic-graph scripter was removed from Godot 4. So the brief's
  "if Godot's old VisualScript system is present … note its shortcomings" cannot be answered
  from this tree by reading code. What *can* be said, and is the stronger argument anyway:
  **both reference engines' logic-graph scripters are the parts their own communities
  abandoned** — Unreal's Blueprint type-conversion machinery is the most-filed-against
  subsystem in the engine, and Godot deleted VisualScript outright. We therefore have two
  independent data points that a logic-node scripter is the wrong shape for this problem, and
  the brief's decision to build a *motion editor* instead is well supported.

---

## 1. Unreal — Sequencer / MovieScene

### 1.1 Scrubbing is an explicit, batched transaction

| Citation | What it establishes |
| --- | --- |
| `Editor/Sequencer/Public/ISequencer.h:749` `virtual void OnBeginScrubbing() = 0;` | scrubbing is a first-class *transaction* with an explicit begin |
| `…:750` `virtual void OnEndScrubbing() = 0;` | and an explicit end (one undo entry) |
| `…:455` `virtual FQualifiedFrameTime GetLocalTime() const = 0;` | the playhead is a *frame-quantised* time value, not a float |
| `…:484` `virtual void SetLocalTime(FFrameTime Time, ESnapTimeMode SnapTimeMode = ESnapTimeMode::STM_None, bool bEvaluate = true) = 0;` | ****`bEvaluate` is the crucial parameter**: move the playhead *without* re-evaluating, then evaluate separately |
| `…:487` `virtual void SetLocalTimeDirectly(FFrameTime NewTime, bool bEvaluate = true) = 0;` | bypasses snapping when the caller has already snapped |

**Informed:** `03-…` §4.4. Our ghost drag is a scrub, and the begin/`bEvaluate=false`/end
decomposition is copied deliberately: without it, a 60 Hz mouse drag would re-apply the whole
track set (and re-tick previews) on every mouse-move, and would produce one undo entry per
mouse-move. Concretely, the `apply_tracks_to_ghost(t)` purity requirement in `03-…` §8 exists
to make the `bEvaluate = false` path cheap.

**Original adaptation:** Unreal's `FFrameTime` is an integer frame number with a frame-rate
denominator. We store **seconds** (`double`) and keep frame quantisation as a *display and
codegen* concern (`frame_quantum`), because the target language's time unit is
`Program.Wait(seconds)` — quantising in the model would force a second dequantisation at
codegen and lose the loop cadence (`wait_stride`, `05-…` §4.8).

### 1.2 Right-click injection is a track-editor extension point

| Citation | What it establishes |
| --- | --- |
| `Editor/Sequencer/Public/ISequencerTrackEditor.h:100` `class ISequencerTrackEditor` | tracks are contributed by *plugins*, not hard-coded |
| `…:141` `virtual void BuildAddTrackMenu(FMenuBuilder& MenuBuilder) = 0;` | adding a track is a context-menu action |
| `…:164` `virtual void BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass) = 0;` | **the menu is built for a specific object binding and its class** |
| `…:192` `virtual TSharedPtr<SWidget> BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params) = 0;` | each track can contribute its own inline editor widget |
| `…:276` `virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> TrackClass) const = 0;` | a track editor declares what it can handle |

**Informed:** `03-…` §3 (hook chips) and `02-…` §7 (`IGraphBackend::context_menu_for`).
`BuildObjectBindingTrackMenu`'s signature is the exact shape of our "right-click an object →
pick a hook slot → assign `.rbsrc`": the menu is parametrised by **the object**, and the
available actions depend on **the object's class**. Our analogue is
`SceneTimelineBackend::context_menu_for(NodeAddress)`, and the available hooks come from the
decoded file plus the generated hook registry (`01-…` §3.2) rather than from a compiled-in
list — which is strictly better than Unreal here, because we can offer *only* hooks that
actually exist on that object.

**Original adaptation:** `SupportsType` becomes unnecessary because our tracks are generated
from the component's own schema (`02-…` §3.2), so there is exactly one track provider and it
is always correct. We keep the *idea* (`03-…` §5.3 "track set offered for a given target")
without the plugin indirection.

### 1.3 Authored data vs compiled runtime artifact

| Citation | What it establishes |
| --- | --- |
| `Runtime/MovieScene/Public/MovieScene.h:517` `UMovieSceneTrack* AddTrack(TSubclassOf<UMovieSceneTrack> TrackClass, const FGuid& ObjectGuid);` | the authored document is a list of tracks bound to object GUIDs |
| `…:580` `TArray<UMovieSceneTrack*> FindTracks(TSubclassOf<UMovieSceneTrack>, const FGuid&, const FName& TrackName) const;` | track identity is (class, object binding, name) |
| `…:706` `const TArray<UMovieSceneTrack*>& GetTracks() const` | authored order is queryable |
| `Runtime/MovieScene/Public/Evaluation/MovieSceneEvaluationTemplate.h:159` `struct FMovieSceneEvaluationTemplate` | **a separate compiled structure exists** |
| `…:230` `const TMap<FMovieSceneTrackIdentifier, FMovieSceneEvaluationTrack>& GetTracks() const;` | compiled tracks are keyed by a *different*, generated identifier |
| `…:277` `TMap<FMovieSceneTrackIdentifier, FMovieSceneEvaluationTrack> Tracks;` | the live, compiled set |
| `…:280` `TMap<FMovieSceneTrackIdentifier, FMovieSceneEvaluationTrack> StaleTracks;` | **the previous compile is retained**, so a rebind can transition instead of hard-cutting |
| `Runtime/MovieScene/Public/Evaluation/MovieSceneEvaluationTemplate.h:112` `struct FMovieSceneEvaluationTemplateSerialNumber` | the compiled artifact carries a serial number, so staleness is detectable |

**Informed:** `05-…` §1 (the four-stage pipeline) and `06-…` §1.5/§2.2. This is the single
most important architectural import in the whole project:

- authored = `.rbsrc` (`04-…`), compiled = `(String, Bytes)`, and the compiled artifact is
  **derived, not edited**.
- `StaleTracks` → our **rollback slot** in `06-…` §2.1, which is what makes
  `Graphy.Rollback` and a safe try-compile-revert loop possible.
- the serial number → `04-…` §8's `cache.lua_source_sha256`, which makes "is this compiled
  artifact still current?" a cheap check rather than a full recompile.
- `FMovieSceneTrackIdentifier` being a *different* namespace from the authored track is
  exactly why our `.rbsrc` stores `target_component_id` **and** resolves by
  `(ClassName, ordinal)` on bind (`03-…` §9): the binding is re-derived, never trusted.

### 1.4 Key-level change events

| Citation | What it establishes |
| --- | --- |
| `Runtime/MovieScene/Public/Channels/MovieSceneChannel.h` `struct FKeyAddOrDeleteEventItem { int32 Index; FFrameNumber Frame; }` | adding/deleting a key broadcasts a structured event |
| `…` `struct FKeyMoveEventItem { int32 Index; FFrameNumber Frame; int32 NewIndex; FFrameNumber NewFrame; }` | moving a key broadcasts *both* positions, so listeners can re-sort without re-reading |

**Informed:** `03-…` §9 (`AddKeyCommand`, `MoveKeysCommand`). Our undo commands carry the same
information (old and new time/index), which is what lets the `QUndoStack` entries be exact
and lets the key list maintain its sorted invariant without a rescan.

---

## 2. Unreal — Graph editor (structural graph)

### 2.1 Pins are real widgets, so the node's size is computed, not declared

| Citation | What it establishes |
| --- | --- |
| `Editor/GraphEditor/Public/SGraphNode.h:217` `virtual void CreatePinWidgets();` | pins are *created as widgets* |
| `…:468` `TArray< TSharedRef<SGraphPin> > InputPins;` | inputs are widgets in an array |
| `…:470` `TArray< TSharedRef<SGraphPin> > OutputPins;` | likewise outputs |
| `…:476` `TSharedPtr<SVerticalBox> LeftNodeBox;` | inputs live in a **vertical box** |
| `…:478` `TSharedPtr<SVerticalBox> RightNodeBox;` | outputs likewise |
| `…:360` `virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox) {}` | extension point *inside* the flow layout |
| `…:280` `virtual FSlateRect GetTitleRect() const;` | the title's rect is **queried from layout**, not hard-coded |
| `…:214` `virtual void UpdateGraphNode();` | geometry is (re)built in a dedicated pass, not during paint |
| `…:157` `virtual void MoveTo(...)` / `159 GetDesiredSizeForMarquee2f()` | node size is a *desired size* the panel asks for |

**Informed:** `07-…` §6 (the `LayoutEngine`) and the fix rules L1–L4. This is the strongest
possible confirmation that Graphy's four bugs share one root cause: **Unreal has no
`headerHeight = 32`; it has a vertical box, and the title rect is whatever the layout
computed.** `UpdateGraphNode()` being a separate pass from painting is also the precedent for
fix B6 (`07-…` §5) — layout belongs in a layout pass, not in `paintEvent`.

### 2.2 Panel and node containment

| Citation | What it establishes |
| --- | --- |
| `Editor/GraphEditor/Public/SGraphPanel.h:97` `class SGraphPanel : public SNodePanel, public FGCObject` | the canvas is a *panel* over a generic node-panel; graph semantics are layered on top |
| `…:231-236` the `SNodePanel interface` comment block | the base class owns pan/zoom/marquee; the derived class owns graph semantics |
| `…:269` `bool IsNodeTitleVisible(const class UEdGraphNode* Node, bool bRequestRename);` | visibility queries are panel-level, and are used to decide whether to draw a compact node |
| `Editor/GraphEditor/Public/SNodePanel.h` | exists; the zoom/pan/marquee/hit-test layer |
| `Editor/GraphEditor/Public/SGraphNodeKnot.h` | reroute knots (Graphy already implements these) |
| `Editor/GraphEditor/Public/SGraphNodeComment.h` | comment frames that carry nodes (Graphy already implements these) |

**Informed:** `02-…` §7 (`IGraphBackend` + `GraphDocument`). The `SGraphPanel : SNodePanel`
split is the argument for keeping `GraphyCanvas` as a dumb painter and moving all semantics
into the backend — the same decomposition that lets us host two very different backends on
one canvas. `IsNodeTitleVisible` informed the minimap/HUD decision in `07-…` §4: at low zoom
the node should degrade gracefully rather than keep drawing unreadable text.

### 2.3 Wire geometry and hover hit-testing

| Citation | What it establishes |
| --- | --- |
| `Editor/GraphEditor/Public/ConnectionDrawingPolicy.h:102` `class FConnectionDrawingPolicy` | wire drawing is a policy object, separable from the panel |
| `…:174` `static UE_API float MakeSplineReparamTable(P0, P0Tangent, P1, P1Tangent, FInterpCurve<float>& OutReparamTable);` | **a reparameterisation table is built so `t` is uniform in arc length** |
| `…:177/180` `virtual void DrawSplineWithArrow(...)` | arrow is drawn as part of the spline pass |
| `…:188` `virtual void DrawConnection(int32 LayerId, const FVector2f& Start, const FVector2f& End, const FConnectionParams& Params)` | connections are drawn into an explicit **layer** |
| `…:242` `void SetSliceLine(const FMarqueeRect& InLine);` | the wire-slicing laser (Graphy already implements this) |
| `…:191` `void SetHoveredPins(const TSet<FEdGraphPinReference>&, const TArray<TSharedPtr<SGraphPin>>&, double HoverTime);` | hovered pins are *set*, not derived per-frame |
| `…:161` comment: "If a (valid) slice line has been set, we'll intersect any drawn splines via `DrawConnection()` with that line" | slicing is implemented at the *drawing policy* level |
| `Private/ConnectionDrawingPolicy.cpp:136` `DrawSplineWithArrow(...)`, `:315` `DrawConnection(...)`, `:425` `DrawPreviewConnector(...)`, `:483` `DetermineWiringStyle(...)`, `:489` `DetermineLinkGeometry(...)`, `:507` `Draw(...)`, `:583` `BuildPinToPinWidgetMap(...)`, `:595` `DrawPinGeometries(...)` | the full pass structure: build pin geometry map → determine geometry/style → draw |
| `Editor/GraphEditor/Public/GraphEditorSettings.h:105` `float SplineHoverTolerance;` | hover tolerance is a **setting**, not a magic number in the hit test |

**Informed:** `07-…` §5 B8. Graphy approximates wire hit-testing with 24 uniform `t` samples
against a 7px tolerance. `MakeSplineReparamTable` is the reference for why that is wrong:
uniform `t` on a cubic is **not** uniform in arc length, so the effective hover tolerance
varies along the wire. The fix uses `QPainterPathStroker` (exact) plus a single
`kSplineHitTolerance` constant, i.e. the same shape as `SplineHoverTolerance`.

**Original adaptation:** Unreal's `FConnectionDrawingPolicy` is a class hierarchy with
per-schema subclasses (`BlueprintConnectionDrawingPolicy` etc.). We do **not** need that:
we have exactly one wire geometry, so a single free function plus a `WireStyle` struct is
correct. Copying the policy-object pattern would be architecture tourism.

---

## 3. Godot — AnimationPlayer / AnimationTrackEditor (the timeline)

### 3.1 The keyframe data model

| Citation | What it establishes |
| --- | --- |
| `scene/resources/animation.h:60` `enum InterpolationType : uint8_t { INTERPOLATION_NEAREST, INTERPOLATION_LINEAR, INTERPOLATION_CUBIC, INTERPOLATION_LINEAR_ANGLE, INTERPOLATION_CUBIC_ANGLE }` | the interpolation enum, including angle-aware variants |
| `…:68` `enum UpdateMode { UPDATE_CONTINUOUS, UPDATE_DISCRETE, UPDATE_CAPTURE }` | **a track can be continuous or discrete** — the origin of our `Interp::Step` |
| `…:74` `enum LoopMode { LOOP_NONE, LOOP_LINEAR, LOOP_PINGPONG }` | track-level extrapolation; the origin of our `Extrapolation` |
| `…:81` `enum LoopedFlag { LOOPED_FLAG_NONE, LOOPED_FLAG_END, LOOPED_FLAG_START }` | the comment explains it: *"used in Animation to 'process the keys at both ends correct'"* — a real-world acknowledgement that loop boundaries need special key handling |
| `…:107` `struct Track { TrackType type; InterpolationType interpolation; bool loop_wrap; NodePath path; StringName concatenated_path; bool imported; bool enabled; }` | a track carries its own interp/loop/enabled state |
| `…:126` `struct Key { real_t transition = 1.0; double time = 0.0; }` + `…:133 template<typename T> struct TKey : public Key { T value; }` | keys are `(time, value)` with a transition weight |
| `…:144/152/160` `PositionTrack{ LocalVector<TKey<Vector3>> positions; }` `RotationTrack{ TKey<Quaternion> }` `ScaleTrack{ TKey<Vector3> }` | per-domain typed key arrays |
| `…:168` `BlendShapeTrack{ LocalVector<TKey<float>> }` | a scalar track, which is what our `FloatField` is |
| `…:176` `ValueTrack{ UpdateMode update_mode; LocalVector<TKey<Variant>> values; }` | the generic per-property track, which is what our `FloatField`/`BoolField`/`AssetName` are |
| `…:187` `struct MethodKey : public Key { StringName method; Vector<Vector> params; }` / `MethodTrack{ LocalVector<MethodKey> methods; }` | **a call with arguments at a time — the exact precedent for our action markers** |
| `…:247` `struct MarkerKey { double time; StringName name; }` | **Godot 4.7's named marker** |
| `…:255` `LocalVector<MarkerKey> marker_names; // time -> name` | markers are stored as a time-ordered vector |
| `…:256-257` `HashMap<StringName,double> marker_times; HashMap<StringName,Color> marker_colors;` | marker names are a separate lookup namespace with presentation data |
| `…:259` `LocalVector<Track*> tracks;` | the document is a flat track list |
| `…:268` `int _marker_insert(double p_time, LocalVector<MarkerKey>& p_keys, const MarkerKey& p_value);` | markers are inserted into a **sorted** list, like keys |

**Informed:** `03-…` §5.1 (`Track`, `Key`, `ActionMarker`, `LoopRegion`) and `04-…` §4.1.
The direct mappings: `Animation::Track` → our `Track`; `TKey<T>` → our `Key`;
`MethodKey{method, params}` → our `ActionMarker{fn, args}`; `MarkerKey{time, name}` → a
`ActionMarker` with no args, or a bare marker; `UpdateMode::UPDATE_DISCRETE` → `Interp::Step`;
`LoopMode` → our `Extrapolation`.

**Original adaptations (all three are deliberate divergences):**

1. **Godot conflates track extrapolation with loop behaviour** (`LoopMode` is on the track and
   there is also a separate marker list). We keep **`Extrapolation` on the track** and
   **`LoopRegion` in its own section** (`04-…` §7) because our loop regions are
   *compile-time* constructs that emit `for`/`Program.Wait` (`03-…` §11.3), not runtime
   playback settings. Merging them would make it impossible to express "this track holds its
   last value, but this span repeats".
2. **`Key.transition` is dropped.** It is a weight for blending between tracks, and we have
   no track blending.
3. **`Angle` interpolation variants are dropped** (`INTERPOLATION_*_ANGLE`) — our `Rotation`
   is a single scalar radians field, not a quaternion, so shortest-arc interpolation is a
   plain lerp.

### 3.2 The editor's UX shell

| Citation | What it establishes |
| --- | --- |
| `editor/animation/animation_track_editor.h:606` `class AnimationTrackEditor : public VBoxContainer` | the panel is a vertical box: toolbar, timeline, track rows, curve area |
| `…:188` `class AnimationTimelineEdit : public Range` | **the ruler is a `Range`**, so the playhead is an integer position with min/max/step tracking |
| `…:290` `class AnimationMarkerEdit : public Control` | the marker lane is a dedicated control |
| `…:423` `class AnimationTrackEdit : public Control` | one row per track |
| `…:574` `class AnimationTrackEditGroup : public Control` | rows are grouped (by node/property path) |
| `…:61/98` `AnimationTrackKeyEdit` / `AnimationMultiTrackKeyEdit` (both `: public Object`) | **the key inspector is a reflected object with `_get_property_list()`**, so editing a key opens in the standard property inspector |
| `…:136/164` `AnimationMarkerKeyEdit` / `AnimationMultiMarkerKeyEdit` | markers get the same treatment |
| `…:1024` `class AnimationTrackKeyEditEditor : public EditorProperty` + `…:1034 struct KeyDataCache` | an inline editor property with a cache, so a key inspector is cheap |
| `…:1050` `class AnimationMarkerKeyEditEditor : public EditorProperty` | likewise for markers |
| `…:702` `struct InsertData { Animation::TrackType type; NodePath path; int track_idx; float time = FLT_MAX; Variant value; String query; bool advance; }` | inserting a key is a **queued, confirmable** operation (`insert_data` is a `List<InsertData>`, `_query_insert`, `_confirm_insert_list`) |
| `…:722` `struct TrackIndices { int normal, reset; }` | adding a track may add a companion track in a separate "reset" animation |
| `…:760` `struct SelectedKey { int track = 0; int key = 0; bool operator<(...) }` | selection identity is `(track, key)` with an ordering |
| `…:766` `struct KeyInfo { float pos = 0; }` | selection carries the key's position, so group drag has an offset |
| `…:770` `RBMap<SelectedKey, KeyInfo> selection;` | the selection is a map, not a list |
| `…:773-777` `moving_selection_offset`, `_move_selection_begin/_commit/_cancel` (and `is_moving_selection()` at `…:401` on the public side) | **a bisected move transaction**, which is exactly our scrub transaction |
| `…:866` `struct TrackClipboard`, `…:885 struct KeyClipboard { struct Key {…} }` | both tracks and keys are copyable |
| `…:2906` `float AnimationTrackEditor::…` / `editor/animation/animation_track_editor_plugins.{h,cpp}` | per-track-type edit widgets are contributed by plugins (`AnimationTrackEditPlugin`) |
| `editor/animation/animation_bezier_editor.{h,cpp}` `AnimationBezierTrackEdit` | a separate curve editor with draggable tangent handles |
| `editor/animation/animation_blend_space_{1d,2d}_editor.cpp`, `animation_blend_tree_editor_plugin.cpp`, `animation_state_machine_editor.cpp`, `animation_library_editor.cpp`, `animation_tree_editor_plugin.cpp` | the wider animation-editor family — **not used**; noted only to show that Godot's answer to "different animation semantics" is *different editors*, not one universal one |
| `scene/animation/animation_player.{h,cpp}`, `animation_mixer.{h,cpp}`, `animation_tree.*`, `animation_node_state_machine.*`, `animation_blend_tree.*` | runtime playback — **not used** (we compile to Lua, we do not play back in-editor) |

**Informed:** `03-…` §6 (the panel layout diagram), §5.2 (invariants), §9 (undo).
Directly imported:

- the **VBox shell** (toolbar → ruler (`Range`) → pannable track rows → optional curve area);
- **`SelectedKey{track,key}` + `KeyInfo{pos}`** as the selection model, verbatim, because it
  is the minimal correct model for group drag;
- the **`_move_selection_begin/_move/_commit/_cancel`** transaction shape, which is the same
  shape as our scrub (`03-…` §4.4);
- **key and marker inspectors as reflected inspectors** — we get this free by using
  `Q_PROPERTY` + a `QFormLayout` editor, and it is why `.rbsrc` keys are a plain struct;
- **`InsertData`'s queued insert** is the precedent for our "add track" flow, where the user
  picks a property and it may need confirmation.

**Original adaptations:**

1. **We reuse `src/ruby/panels/animation_control_bar.{h,cpp}` instead of writing a
   `TimelineEdit : Range` from scratch.** That widget already exposes
   `set_frame_count(int)`, `set_frame(float)`, `frameChanged(float)`, `playingChanged(bool)`
   and its own header comment says it is "deliberately independent from the viewport so it
   can be docked, floated, or reused by future scene and animation editors". This is that
   future editor. Rewriting it would discard working code and the stated intent.
2. **No reset/animation-companion track** (`TrackIndices{normal, reset}`) — we have one
   compiled blob per hook, so there is nowhere to put a companion.
3. **No per-track-type plugin indirection** — instead, tracks are generated from the target's
   schema (`03-…` §5.3), so there is one track renderer that switches on `PropertyKind`.
4. **Markers carry typed arguments** (`04-…` §6.2), richer than `MarkerKey{time,name}`,
   because ours must compile to a call, including string concatenation and object receivers.

---

## 4. Godot — GraphEdit / GraphNode (structural graph, layout fix)

| Citation | What it establishes |
| --- | --- |
| `scene/gui/graph_node.h:42` `struct Slot { bool enable_left; int type_left; Color color_left; … bool enable_right; int type_right; … }` | a slot is a typed, coloured port with independent left/right configuration |
| `…:58` `struct PortCache { Vector2 pos; int slot_index; int type; Color color; }` | **port positions are cached**, not recomputed per paint |
| `…:65` `struct _MinSizeCache { int min_size; int max_size; bool will_stretch; int final_size; }` | minimum size is computed and cached per slot |
| `scene/gui/graph_node.h` `HBoxContainer *titlebar_hbox; Label *title_label;` | **the title is a real container + label**, so its height is measured |
| `… theme_cache.separation`, `… theme_cache.port_h_offset` | spacing is themed, not hard-coded |
| `scene/gui/graph_node.cpp:1000` `Size2 GraphNode::_get_minimum_size(bool p_use_desired_sizes) const` | minimum size is a function of measured children and `separation` |
| `…:1006` `Size2 minsize = (p_use_desired_sizes ? titlebar_hbox->get_bound_desired_size() : titlebar_hbox->get_minimum_size()) + sb_titlebar->get_minimum_size();` | **the node's minimum height starts from the measured titlebar** |
| `…:1024` `minsize.height += separation;` | each extra row adds separation |
| `…:1041-1046` `void GraphNode::_port_pos_update() { int edgeofs = theme_cache.port_h_offset; int separation = theme_cache.separation; … int vertical_ofs = titlebar_hbox->get_size().height + theme_cache.titlebar->get_minimum_size().height + theme_cache.panel->get_margin(SIDE_TOP);` | **port Y starts after the *measured* titlebar height plus the panel's top margin** |
| `…:1082` `vertical_ofs += size.height + separation;` | and accumulates per slot |
| `…:653-703` `int port_h_offset = theme_cache.port_h_offset; Rect2 titlebar_rect(Point2(), titlebar_hbox->get_size() + sb_titlebar->get_minimum_size()); … draw_port(slot_index, Point2i(port_h_offset, slot_y), true, slot.color_left); … draw_port(slot_index, Point2i(get_size().x - port_h_offset, slot_y), false, slot.color_right);` | ports are drawn at *computed* slot Y positions, symmetric left/right |
| `…:163-173` `Size2 titlebar_size = Size2(new_size.width, titlebar_hbox->get_size().height); fit_child_in_rect(titlebar_hbox, titlebar_rect); …` | the titlebar is **resized by the layout system** |
| `scene/gui/graph_edit.cpp:1526` `PackedVector2Array GraphEdit::get_connection_line(const Vector2 &p_from, const Vector2 &p_to) const` | wire geometry is a function of the two port positions |
| `…:1528` `if (GDVIRTUAL_CALL(_get_connection_line, p_from, p_to, ret))` | it is overridable, so wire shape is a policy |
| `…:1561/1584` `Vector<Vector2> points = get_connection_line(conn->_cache.from_pos * zoom, conn->_cache.to_pos * zoom);` | **wires are drawn from the cached port positions**, not recomputed |
| `…:2906` `float GraphEdit::get_connection_lines_curvature() const` / `…:2923` `float GraphEdit::get_connection_lines_thickness() const` | curvature and thickness are settings |
| `scene/gui/graph_edit_arranger.h:39` `class GraphEditArranger : public RefCounted` / `…:62` `void arrange_nodes();` | **a collision-avoiding auto-layout pass exists separately from the graph** |
| `scene/gui/graph_frame.{h,cpp}`, `graph_element.{h,cpp}` | comment/frame containers and the shared element base |

**Informed:** `07-…` §6 whole. `_port_pos_update()`'s
`vertical_ofs = titlebar_hbox->get_size().height + theme_cache.titlebar->get_minimum_size().height + theme_cache.panel->get_margin(SIDE_TOP)`
is the exact three-term computation Graphy is missing: it has `header_h = 32.0f` instead.
Graphy's subtitle collision (bug 1) is impossible in Godot **by construction**, because
`titlebar_hbox->get_size().height` cannot be smaller than the label inside it.

Also imported:

- `PortCache` → `Pin::canvas_x/canvas_y` already exists in `graphy.h`, but Graphy overwrites
  it every `paintEvent` (`07-…` B6). Godot caches and invalidates; we will too.
- `_MinSizeCache` → `NodeLayout` cached per node id (`07-…` §6).
- `GraphEditArranger::arrange_nodes()` → `LayoutEngine::arrange_collision_free()`
  (`02-…` §6.4), needed because Scene-backend nodes are placed at world coordinates and will
  overlap.
- `get_connection_lines_curvature()`/`thickness()` → a `WireStyle` struct instead of the
  `offset = max(35, |dx|*0.5)` spread across `make_spline_path`.

**Original adaptation:** Godot's slots are **index-addressed** (`set_slot(index, …)`,
`get_input_port_position(idx)`), which works when the node's content is authored in the
editor. Ours is **generated from decoded data**, so a slot index is unstable across
re-decodes. We therefore key rows by `(pin field tag, multi_index)` — the stable identity
from `02-…` §5.1 — and derive the visual index from that, rather than the reverse.

---

## 5. Summary: what each reference gave us

| SwordigoDesktop artifact | Primary reference | Secondary |
| --- | --- | --- |
| `02-…` §7 `IGraphBackend` / `GraphDocument` seam | Unreal `SGraphPanel : SNodePanel` (`SGraphPanel.h:97`) | Godot `GraphEdit`/`GraphNode` split |
| `02-…` §5 auto-wire resolution | **original** (no engine has integer-FK edges in a decoded format) | Unreal pin typing for the wire palette |
| `02-…` §6.4 collision-avoiding layout | Godot `GraphEditArranger::arrange_nodes()` | Unreal `SGraphNode::GetDesiredSize*` |
| `03-…` §4.4 scrub transaction | Unreal `ISequencer::OnBeginScrubbing`/`SetLocalTime(_,_,false)` | Godot `_move_selection_begin/_commit` |
| `03-…` §3 hook chips / right-click assign | Unreal `ISequencerTrackEditor::BuildObjectBindingTrackMenu` | — |
| `03-…` §5 keyframe/marker model | Godot `Animation::Track/TKey/MethodKey/MarkerKey` | Unreal `FMovieSceneChannel` events |
| `03-…` §6 panel layout | Godot `AnimationTrackEditor`/`AnimationTimelineEdit`/`AnimationTrackEdit` | existing `AnimationControlBar` |
| `03-…` §7 action markers | Godot `MethodKey`+`MarkerKey` | observed `CreateShopItem` calls |
| `03-…` §7.3 `state` counters | **original** | — |
| `03-…` §8 ghost trails | **original** | — |
| `04-…` re-openable recording | Unreal authored-vs-compiled split | Godot `.tres` resource model (rejected as too heavy) |
| `05-…` §1 derived-artifact pipeline | Unreal `FMovieSceneEvaluationTemplate` | — |
| `05-…` §4 tick-loop inversion | **original** | vs Godot `UPDATE_CONTINUOUS` sampling |
| `06-…` §1.5 rebind via engine APIs | Unreal evaluation-template rebuild | existing SRE console/mailbox |
| `07-…` L1–L4 layout rules | Godot `GraphNode::_get_minimum_size`/`_port_pos_update` | Unreal `SVerticalBox` pin layout |
| `07-…` B8 wire hit-testing | Unreal `MakeSplineReparamTable` + `SplineHoverTolerance` | Godot `_cache.from_pos/to_pos` |
| `07-…` §4 minimap | **original** | Godot's minimap is a separate `Minimap` node, which we do not have |

### The three genuine gaps in both references

Worth stating explicitly, because these are where the design had to be invented rather than
adapted (`00-…` §6):

1. **Neither engine has a graph whose edges are *discovered* from integer foreign keys inside
   a decoded binary asset.** Unreal's graph edges are user-drawn value/exec flow; Godot's are
   signal connections. Our resolver (`02-…` §5) has no precedent.
2. **Neither engine compiles an animation into a *script*.** They evaluate at runtime. The
   keyframe-run → `for`/`Program.Wait` inversion (`05-…` §4.8) and the `state` counter block
   (`03-…` §7.3) are original, and they exist *because* the target is a coroutine language.
3. **Neither engine hot-patches a live VM through a guest-memory mailbox into a translated
   ARM64 process.** `06-…`'s transport and phasing are specific to SwordigoDesktop's
   architecture; the closest analogue (Unreal's live compile of a Blueprint, or
   `MovieScene`'s template rebuild) is conceptually similar but architecturally unrelated.
