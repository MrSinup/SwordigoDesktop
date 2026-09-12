# `.scmap` World-Map Editor — Feasibility Report

**Author:** Buffy (SwordigoDesktop Ruby SDK) · **Date:** 2026-08-10
**Scope:** What `.scmap` really is, its exact binary structure, the game's runtime API, and how far a visual map editor can go **without breaking vanilla (non-SRE) Swordigo compatibility**.

---

## 1. TL;DR — The headline

1. **`.scmap` is NOT a tile map.** It is the **world-travel node graph** — the overworld map you see in-game (zones, nodes, portals, "Teleport To"). The actual level geometry lives in `.scene` files (which Ruby already edits).
2. **We already have lossless read/write.** Verified byte-identical round-trip through filerift: `6399 bytes → 25.6 KB markup → 6399 bytes (byte_identical=YES)` on `test.scmap`.
3. **A full visual node-graph editor is 100% vanilla-compatible** — every editable field maps 1:1 to a protobuf field, and re-encode is byte-exact. No SRE required for the editor itself.
4. **The one subtlety:** node **positions are computed by the game at load time** (`RecursivelySetNodePositions`, step 54.0 units along 8 compass directions). The vanilla file stores **no positions at all** (0 `Position` fields in all 110 nodes). So the editor must either (a) replicate the algorithm for preview, or (b) keep manual layouts in a **sidecar** file. Writing `Position` into the proto is *probably* respected by `MapNode::LoadFromProtobufMessage` but then overwritten by the layout pass — needs a live-game/SRE test before we rely on it.
5. **All UI art is already extracted** — the exact in-game map sprites (`ui_map_guide_node/start/end/edge/icon_*`) and zone backgrounds (`grove_bg`, `grasslands_bg`, `caves_bg`, `forest_bg`, `graveyard_bg`, …) live in `src/assets/icons/` and `src/assets/`.

**Bottom line:** a "Super Mario Maker–style" visual overworld editor is **fully feasible with zero vanilla risk** in the markup → edit → recode pipeline. The only thing that cannot be vanilla-written is manual free-form node placement (use a sidecar), because the vanilla engine re-derives positions from the portal graph.

---

## 2. What the game actually does with `.scmap`

From `OpenSwordigo/resources/ida_decompiled/functions/Caver/` (IDA Pro evidence headers):

| Class | Role |
|---|---|
| `Caver::Map` | Runtime world map: `LoadFromProtobufMessage`, `SaveToProtobufMessage`, `FindPath`, `NodeForName`, `UnflagAllNodes`, `RecursivelySetNodePositions` |
| `Caver::MapZone` | Area grouping: `LoadFrom/SaveToProtobufMessage` |
| `Caver::MapNode` | One travel stop: `LoadFrom/SaveToProtobufMessage`, `AddPortal`, `HasPortalTo`, `ExperienceLevel`, `Title`, `MusicName` |
| `Caver::MapView` | The in-game map screen: `Init(Map, GameState, bool, bool)`, `Update`, `LayoutSubviews`, `SelectNode`, `SelectNodeAtIndex`, `ClearSelection`, `SetNodePressed`, `NearestNodeInDirection`, `RecursivelySetVisibleNodes`, `HideFarNodes`, `UpdateDefaultNodeVisibility`, `UpdateNodes`, `UpdateNodesSprite`, `UpdateSelectionSprite`, `UpdatePathsSprite`, `AddPathSprite`, `AddPathToSprite`, `AddPathSegmentToSprite`, `UpdateTargetPath`, `AnimateLocationMarkerFromNodeToNode`, `AddNodeIconsToSprite`, `LocationMarkerFrameForMapNode`, `TouchBegan/Moved/Ended/Cancelled` |
| `Caver::MapViewNode` | Per-node UI element: `InitWithMapNode` (sets a **16.0f** scale constant — node sprite size) |
| `Caver::MapMenuPage` | The full-screen page: `InitWithGameState`, `UpdateTitle`, `CenterAtNode`, `ShowTravelToButtonFromNode` (**"Teleport To"** button), `MapViewDidTapSelectedNode`, `MapViewDidTapEmptySpace`, `UpdateMapViewBounds`, `ButtonPressed`, `Dismiss` |

Key cross-links proven from IDA call graphs:
- `MapView::Init` pulls in **`GameState::StateForLevelWithName`**, **`StateProperties::HasFlag`**, **`GameData::QuestForName`**, **`GameState::GetAllQuestsInProgress`** → the map is **save-state driven** (unlocks, quest markers, flags).
- `GameViewController::UpdateGuideTarget` references **`ui_map_guide_icon_quest` / `ui_map_guide_icon_spell` / `ui_map_guide_icon_key`** → guide/quest markers are drawn on the map.
- `MapNode::SaveToProtobufMessage` constructs `Proto::MapNode_Portal` → portals round-trip with the node.

**In-game meaning:** the `.scmap` is the **overworld travel graph**. Each node = a `.scene` level (`LevelName`), each portal = a travel connection, each zone = an area with a title, music, and experience gate. `GameState.CurrentMapNodeName` (field 42) records where the hero is.

---

## 3. Binary format — exact protobuf schema

Decoded with our own filerift (`decode_protobuf(bytes, "scmap")` → schema name `Map`). Field numbers confirmed from both `src/tools/filerift.cpp` (lines ~225, ~359–380) and `src/tools/scene_schemas.cpp` (lines ~599–625).

```
Map                     (filetype "scmap")
├─ 2  Zone (repeated MapZone)

MapZone
├─ 1  Name          (string)   e.g. 'town'
├─ 2  Title         (string)   e.g. 'Cairnwood Village'
├─ 3  Node          (repeated MapNode)
├─ 4  ExperienceLevel (varint) zone unlock gate
└─ 5  Music         (string)   e.g. 'outdoors_light'

MapNode
├─ 1  Position      (Vector2)  ← NEVER present in vanilla test.scmap (computed)
├─ 2  LevelName     (string)   e.g. 'town_part1'  ← the .scene file it opens
├─ 3  Portal        (repeated MapNode_Portal)
├─ 4  Type          (varint enum 0..3, IsValid: < 4)
├─ 5  Hidden        (varint)   dungeon/interior nodes hidden on map
├─ 6  ExperienceLevel (varint)
├─ 7  Music         (string)
├─ 8  HasPortal     (varint)
├─ 9  NumTreasures  (varint)
├─ 10 Title         (string)   displayed name e.g. "Healer's House"
└─ 11 IgnoreInStatistics (varint)

MapNode_Portal
├─ 1  DestinationName (string) ← name of the destination node's LevelName
├─ 2  Direction      (varint enum 1..8, IsValid: (d-1) < 8 → 8 compass dirs)
├─ 3  PassDirection  (varint enum)
└─ 4  IgnoreInNodePositioning (varint) ← excluded from auto-layout
```

**Confirmed enum semantics from real data (`test.scmap`, 14 zones / 110 nodes):**
- **Node Type:** `1` = town/city hub (Cairnwood Village), `2` = mid-zone waypoint hub (plains_part2, forest_part1, grove_part1), `3` = boss/landmark chamber (Chamber of The Mageblade, Lair of Death, Hall of The Dwarven Kings, fire_partBoss, icecastle_partBoss, florennum_jail_boss, worldsend_part9), `0`/absent = plain node.
- **Portal Direction:** alternating `1, 3, 1, 3` along main paths, `5`, `7` on branches → consistent with **8 compass octants** (0° / 90° / …). Exact angle mapping (see §6) needs one live probe, but `FromAngle(θ) = (cos θ, sin θ)` is proven.
- **Hidden:** `1` on interiors (shops, caves, towers, cellars) — they render as icons only when relevant, not as travel stops.
- **`IgnoreInNodePositioning: 1`** appears on 2 portals — the recursion skips them (`if (!portal->flag) { … place … }`).

**Zone/background affinity (from `test.scmap` zone names + `src/assets/` + game resources):**

| Zone (Name) | Title | Music | Background (available!) |
|---|---|---|---|
| `town` | Cairnwood Village | `outdoors_light` | `bg0.png` / `pic0.png` |
| `woods` | Cairnwood Forest | `forest` | `forest_bg.png` |
| `plains` | The Plains | `outdoors_light` | `grasslands_bg.png` |
| `woodkeep` | Forgotten Keep | `dungeon1` | `woodkeep_bg_2x.tex.png` |
| `grove` | Evernight Forest | `forest` | `grove_bg.png` |
| `icecastle` | — | `dungeon1` | `caves_bg.png` |
| `lowergrove` | World's End Keep | `dungeon1` | `graveyard_bg.png` / `wasteland_bg_2x.tex.png` |

---

## 4. The auto-layout algorithm (why positions are computed)

From `Caver::Map::RecursivelySetNodePositions` (0x3BC04C body + call graph):

```
RecursivelySetNodePositions(node):
  for portal in node.portals:
    if portal.IgnoreInNodePositioning: skip
    dest = node_by_name[portal.DestinationName]
    if dest not yet positioned:
        dest.position = ROUND( node.position + FromAngle(angle) * 54.0f )
        recurse(dest)
```

- `FromAngle(θ)` = `(cos θ, sin θ)` (proven, 0x4B2B60).
- Spacing constant **54.0f** (`0x42580000`).
- `Rounded()` (integer rounding) — positions end up integral.
- The recursion starts from a root node and fans out **along portal directions**, so the vanilla map renders as a **stair-step path graph** — exactly the `AddPathSprite / UpdateTargetPath / AnimateLocationMarkerFromNodeToNode` visuals.

**Consequence for the editor:**
- To preview the map **exactly as vanilla**, implement this same recursion (portals as edges, directions as octants, 54px spacing). That's a pure function — trivial in C++.
- For **free-form layout**, store override positions in a **sidecar** (`.swmap` / `.json`), or write `MapNode.Position` and verify in a live game first (§6 risk table).

---

## 5. What Ruby already has (integration surface)

| Capability | Status |
|---|---|
| `decode_protobuf(bytes, "scmap")` → full markup | ✅ `filerift.cpp:1053` (`filetype "scmap" → "Map"`) |
| `recode_markup(text, "scmap")` → binary | ✅ byte-identical verified |
| CLI batch decode/recode | ✅ `bin/ruby_cli` (`filetype_for_path` includes `scmap`) |
| `MapZone`/`MapNode`/`MapNode_Portal` schemas | ✅ `scene_schemas.cpp:599–625` |
| `.scmap` in asset browser | ✅ `asset_viewer.cpp` (recognized ext) |
| Scene editor (the *content* of each node) | ✅ existing Ruby scene editor |
| MCP server (for AI-driven map editing) | ✅ `bin/ruby_cli mcp` — add a `map_decode` tool (mirrors `scene_decode`) |
| Guide sprites + zone backgrounds | ✅ `src/assets/icons/ui_map_guide_*.png`, `src/assets/*_bg.png` |

**Missing** (to build): a `map_loader.h/.cpp` (structured `MapData` mirroring the proto), a `map_view` ImGui canvas (reusing scene-viewport conventions), and a `Map → scene` linker (double-click node → open `LevelName.scene`).

---

## 6. Compatibility risk table (vanilla, non-SRE first)

| Operation | Vanilla risk | Why |
|---|---|---|
| Decode → edit → **recode without byte drift** | 🟢 **Zero** | Verified byte-identical on the real file |
| Edit all string/int/enum fields (Title, Music, Type, Hidden, Treasure, XP gates, portal Direction/Pass) | 🟢 **Zero** | Same protobuf wire format; the game parses them identically |
| Add/remove **zones, nodes, portals** | 🟢 **Zero** | The engine fully reconstructs state from the graph (`FindPath`, `NodeForName`, portal walks) |
| Auto-layout preview (replicate recursion) | 🟢 **Zero** | Editor-only; file untouched |
| Manual positions in **sidecar** | 🟢 **Zero** | Vanilla never sees the file |
| Writing `MapNode.Position` into the proto | 🟡 **Untested** | `MapNode::LoadFromProtobufMessage` does read `Position`, but the loader then runs `RecursivelySetNodePositions` — likely overwrites. Must verify with a live game/SRE hook before shipping |
| Free-form layout with zero file changes + guaranteed same render | 🟡 **Needs test** | Only relevant if you want the *vanilla game* to show your manual layout; sidecar doesn't reach vanilla |
| New node types / directions beyond enums (Type ≥ 4, Direction ≥ 9) | 🔴 **Breaks** | `IsValid` bounds-checked (`Type < 4`, `(Direction-1) < 8`); invalid values are rejected by the game |
| Modifying `.scene` files through the map editor | 🟢 **Zero** (by design) | Scene editor already byte-safe; map editor only *links* to scenes |

**Guiding rule:** every editable field in the editor must stay inside the documented schema, and saves must always go through `recode_markup` (byte-exact). Any editor-only data (manual layout, comments, bookmarks) goes to a sidecar file with the same stem + `.swmap.json`.

---

## 7. The editor — full feature plan (phased)

### Phase A — Foundation (pure tooling, no GUI)
1. `src/tools/map_loader.h/.cpp` — structured `MapData { zones, nodes, portals }` with load/save via filerift (byte-exact).
2. Auto-layout: exact port of `RecursivelySetNodePositions` (54.0, octant angles, `IgnoreInNodePositioning`).
3. `FindPath` (BFS) port for travel-path preview.
4. MCP tools: `map_decode`, `map_summary`, `map_search` (search nodes/zones by string across any `.scmap`).

### Phase B — Visual map editor (Ruby new tab, like the scene editor)
5. **Canvas**: pan/zoom ImGui canvas; draw zone backgrounds (`grove_bg.png` etc. by zone name) as the backdrop.
6. **Nodes**: render with real `ui_map_guide_node.png` sprite (16.0f scale); boss nodes (`Type 3`) get boss styling; hidden nodes dimmed.
7. **Portals**: draw path segments like the game's `ui_map_guide_edge.png` (stair-step along octants), with the animated "location marker" concept.
8. **Editing**: select/move/add/delete nodes & portals; node inspector (all 11 MapNode fields); zone inspector (5 fields); portal inspector (4 fields); drag a portal endpoint onto another node to re-link (dynamic linking, as requested).
9. **Scene sync**: double-click a node → load `LevelName.scene` in the scene editor; reverse-link (from scene editor, "attach to map node").
10. **Validation panel**: orphan nodes, portals to missing levels, unreachable nodes, XP-gate mismatch, treasure counts, duplicate names.

### Phase C — Game-accurate preview
11. Live preview replicating `MapView` semantics: `UpdateDefaultNodeVisibility`, `HideFarNodes`, `NearestNodeInDirection`, quest/spell/key icon overlays (`ui_map_guide_icon_*`) from GameState/quest data.
12. "Travel To" simulation using `FindPath` + marker animation (`AnimateLocationMarkerFromNodeToNode`).

### Phase D — Advanced (still vanilla-safe)
13. Layout sidecar (`.swmap.json`): free-form positions, per-node notes, layout auto-save, "re-layout from algorithm" button.
14. PNG export of the whole map; diff/merge between two `.scmap`s.
15. SRE hook (optional): live-test a modded `.scmap` inside the real game to validate byte-level behavior of any ambiguous field (esp. `Position`).

---

## 8. "How far can we take it?" — honest ceiling

**Fully achievable, vanilla-identical:**
- The entire overworld graph editor (nodes/portals/zones, all metadata, byte-exact save).
- The exact vanilla map rendering (algorithm + real art assets) as a preview.
- Scene↔map two-way linking, validation, FindPath, quest-marker overlays.

**Not achievable without touching vanilla behavior:**
- Free-form node positions that *vanilla* displays (vanilla recomputes positions; only sidecar or a live-game test can extend this).
- New node types/directions beyond the enums (hard-coded `IsValid` bounds in the game).
- Any tilemap semantics — **Swordigo has no tilemaps**; `.scmap` is purely the travel graph, `.scene` files are the geometry. A "tile map editor" would actually be a scene editor feature (ground-mesh generator), which Ruby already has.

**The killer feature this unlocks:** a **world-flow design tool** — you can visually restructure the entire game's overworld (which levels connect to which, gating, treasure counts, boss placement, music themes) with zero byte risk, then instantly open any node's scene to edit its content. Combined with Ruby's existing scene editor + ground-mesh generator, that's a complete vanilla-compatible Swordigo level-design suite.

---

## 9. Evidence appendix

- `test.scmap` decode: 14 zones, 110 nodes, 0 `Position` fields; zones `town/woods/plains/woodkeep/grove/…/lowergrove`.
- Byte-identical round-trip probe: `orig=6399 text=25625 enc=6399 BYTE_IDENTICAL=YES`.
- IDA evidence: `Caver::MapView::Init(Map, GameState, bool, bool)` + `UpdateTargetPath` + `ShowTravelToButtonFromNode("Teleport To")`; `Caver::Map::RecursivelySetNodePositions` (54.0f, `FromAngle`, `Rounded`, skip-flag at portal+16); `Vector2::FromAngle = (cos,sin)`; `MapNode_Type_IsValid < 4`; `MapNode_Portal_Direction_IsValid (d-1)<8`; `MapViewNode::InitWithMapNode` scale 16.0f; `GameViewController::UpdateGuideTarget` uses `ui_map_guide_icon_quest/spell/key`.
- Schemas: `src/tools/filerift.cpp:225,359–380`; `src/tools/scene_schemas.cpp:599–625`.
- Art: `src/assets/icons/ui_map_guide_{node,start,end,end_noarrow,edge,icon_key,icon_quest,icon_spell}.png`, `src/assets/{grove,grasslands,caves,forest,graveyard,bg0,pic0}.png`; game-res `*_bg_2x.tex.png` variants.
