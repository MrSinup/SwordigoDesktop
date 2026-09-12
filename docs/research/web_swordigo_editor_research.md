# Web Swordigo Editor (szkot.xyz/SwordigoEditor) — Feature Research & Ruby GG Comparison

**Author:** SRE / Ruby GG tooling pass
**Date:** 2026-09-09
**Source:** `https://szkot.xyz/SwordigoEditor/` (v1.2.2), fully downloaded via `wget -r` and beautified.

## 1. What was downloaded

```
/tmp/szkot_editor/
├── szkot.xyz/SwordigoEditor/
│   ├── index.html                 (Vite SPA shell: menubar, assets panel, viewport, objects/props explorer)
│   ├── favicon.ico
│   └── assets/
│       ├── index-u_qz_Wej.js      (raw 838 KB bundle)
│       ├── index_beautified.js    (beautified 1.2 MB / 31,819 lines — the actual editor)
│       ├── index-CoXoKe-t.css
│       └── editor.main-B_-AdIX2.js (raw 3.8 MB — bundled Monaco editor)
└── editor.main.js / editor.main.beautified.js  (Monaco only; not game logic)
```

Beautification was done with a small brace/string-aware node script (`{`, `}`, `;` newline expansion
with string/template/regex protection). The app itself is a **vanilla-JS + three.js r166** scene
editor with a **Monaco-based Lua/decoded-view editor**, no framework (no React/Vue).

---

## 2. Editor architecture at a glance

| Concern | Implementation | Location (beautified) |
|---|---|---|
| Protobuf wire decode → editable tree | Hand-rolled generic wire-tree parser (`lM`, `Ph`, `uM`, `qs`) | ~19800–20160 |
| Protobuf encode | `rr` + `fM` (BigInt varints, float32 via DataView) | ~20051–20084 |
| Field-name schema | One giant `Ft.types` table (~every game message) | ~20340–23730 |
| Document model + undo/redo | `class is` (extends event emitter), **snapshot = full re-encode of the file** | ~23980–24310 |
| Scene graph / 3D viewport | `pS` (three.js WebGLRenderer + OrbitControls-like custom input), `Kl` scene builder | ~27420–29480 |
| Ground mesh generation | `GS` / `HS` / `Jd` / `Np` (vertex-pushing triangulator + ear clipping `BS`) | ~29380–29940 |
| POD (`.pod`) model parser | `Ya` / `yS` / `bS` / `MS` — full AB.POD.2.0 block reader | ~27610–27940 |
| Textures | `to` / `Cp` / `Tp` / `wS` — PVR v2 (RGBA8888, ETC1), `.tex` gzip (8 pixel formats), PNG/WebP | ~28140–28430 |
| GLB → POD converter | Separate lazy chunk `glbToPod-*.js` | `import("./glbToPod-D9pcRzhd.js")` |
| Assets | Full **APK / ZIP reader** (`ke`) with categories (Scenes/Models/Textures/Sounds/Libraries/Fonts/Atlases) | ~19500+, `Lw` panel |
| Lua editing | Monaco, plus a **hand-written full Swordigo Lua API `---@meta`** for LSP | 24805–25700 |
| Audio | `Gr` — native `<audio>` element playback of WAV (browser codec) | 31166 |
| Save/export | `cs()` write-back into the in-memory APK; **APK repack + re-sign** (`nS`), ZIP export | 31267, File menu |

---

## 3. What the web editor does (feature inventory)

### 3.1 File / asset handling
- **Drag-and-drop** an `.apk`/`.zip`/`.scene`/`.pod`/`.glb`/texture/audio file into the browser window → decodes it (hint text in the index.html viewport).
- Loads the whole **APK as a virtual filesystem** (`ke`) and lists assets by category with emoji icons:
  `Scenes 🌐, Models 🧊, Textures 🎨, Sounds 🕪, Libraries 📓, Fonts 🔤, Atlases 🧩, Other 📄`.
- Asset context menu: Open / Edit / **Add to Scene** (for models) / Play (sounds) / Import (Replace) /
  Export / **Export to glb** / **Decode to Clipboard** (protobuf → decoded text) / Copy / Rename / Delete.
- Category menu: Import Files…, **Export All (zip of category)**, Paste.
- File menu: **Import Files**, **Export as APK** (repacks the loaded APK and **signs it** →
  `_signed.apk` ready to install), **Export as ZIP** (whole APK contents).

### 3.2 Scene document model (the core, most instructive part)
Everything is a **typed protobuf wire tree**. Key insight for Ruby GG: they did **not** parse into
native C++-like structs; they keep the raw wire tree with a name/type schema and mutate the tree,
then **re-encode on save**. This is exactly what Ruby GG's `scene_loader`/FileRift already does
structurally, but the web editor pushes it further:

- **Schema-driven UI**: every field renders an editor automatically from `Ft.types` (i32 → text input,
  varint → text/checkbox/dropdown, strings → text, `Program` → "Edit Lua" button with Monaco).
- **Snapshot undo/redo**: `snapshot()` re-encodes the whole file to a `Uint8Array` and pushes it on
  an undo stack (cap 100 entries). `undo()` decodes the popped snapshot and diffs object-by-object to
  emit minimal `structureChanged` events. No command pattern needed — the entire editor state is the
  encoded file, so undo is trivially correct (at the cost of memory; scenes are small).
- **Template (SCL library) support**: object field 1 names a template; `loadLibraries()` merges
  `*.msaddon.scl` + scene-local libraries, `objects()` virtualizes inherited components
  (local components override library ones by component ID), and `unlinkTemplate()` materializes
  the template components into the object.
- **Add Component** with auto-fresh IDs and **default field scaffolding** (`aa()` recursively builds
  a default value for Vector2/Vector3/Color/Rectangle fields).
- Copy/paste works at **both granularities**: whole objects (deep-cloned, fresh identifier) and
  **single components** (pasted into a selected object with a fresh component ID).
- Ground polygon editing **regenerates the GroundMesh from the polygon in-browser** on commit
  (`qS` + `GS`) — the same algorithm the game runs, including min/max depth and LocalAABB
  recomputation. Ruby GG ports this via `boulder` + `av::scene_load_bytes` (already done in the
  in-scene mesh editor).

### 3.3 3D viewport (three.js)
- Perspective camera, 60° FOV; **RMB-hold = fly mode** (pointer-lock, WASD + E/Q/Shift, scroll =
  fly speed); **scroll without RMB = dolly** toward the view direction.
- 200×100 **grid** helper, toggleable; **Frame Scene** / F-key frames the selected object.
- **TransformControls** (three.js `TransformControls`): Move (T) / Rotate (R) / Resize (S) gizmos;
  `attachGizmo` supports per-axis show flags.
- **Click-pick** via raycast (drag-distance threshold 4px distinguishes click vs drag); picks the
  object id stored in `userData.objectId` up the parent chain.
- **Game-light engine** (`dS`): parses Light components (ambient/directional/point/overlay,
  flicker via FireEmitter link), drives three.js lights + a custom vertex-color **veil overlay**
  ("Render Lights" menu toggle).
- **Background plane**: scene Background component is drawn as a huge textured quad that tracks the
  camera and clamps to its bounds (parallax by object z).
- **Water mesh** animator, **particle emitters** (5 emitter types ported from the game's
  ParticleEmitterComponent semantics) — all gated behind "Render Effects (WIP)".
- **Shape regions / collision debug**: Box/Circle/Polygon shapes rendered as translucent volumes +
  edge outlines, toggleable ("Show Controllers").
- Per-type **wireframe markers**: objects with no renderable components get a colored wireframe box
  (color by component type), `SpawnPoint 🚩/Portal 🌀/Light 💡/Sound 🔊` colors.
- View menu: Render Lights, Render Effects (WIP), Show Background, Show Controllers, Show Hidden,
  Gizmo mode radio, Frame Scene, Toggle Grid, Fullscreen (F11).

### 3.4 Object editing
- Objects panel (tree) + Properties panel (schema-generated editors), both with **search filters**.
- Context menus: Focus in Viewer, Rename, Duplicate, Add Component, Unlink Template, Copy,
  Paste Object/Component, Delete, Play Sound (if SoundEffect component).
- **Component-level copy/paste** (web editor's standout feature vs Ruby GG today).
- `scaleObjectData()` — on gizmo scale, it **scales the shape/rectangle/collision data inside the
  components too**, not just the transform (Ruby GG's gizmo currently only writes the transform).
- Model component: reads POD rotation offsets (fields 2/4), tint color (field 8), and **CenterPoint**
  correction from the POD node hierarchy.
- Gizmo drag → `setTransformSilent` (no event) → `notifyEdited`/`syncObjectSignature` on release,
  so the scene graph only rebuilds when signature (FNV hash of encoded bytes) changes.

### 3.5 Binary decoders (browser-side, no native deps)
- **POD**: `AB.POD.2.0` header, block reader `Mp` with the end-of-list sentinel bit, per-block typed
  accessors; node hierarchy with `CenterPoint` compensation; interleaved vertex streams
  (positions/normals/UVs, indices, bone indices/weights, bone batches).
- **Textures**: PVR v2 (`RGBA8888` + **ETC1 software decode** in `wS` — a full ETC1 block
  decompressor in JS!), gzipped `.tex` (8 pixel formats incl. 4444/5551/565/luminance/alpha),
  PNG/WebP via `createImageBitmap`.
- **Scene/other protobuf files** via the generic wire tree (`.scene`, `.scl`, `.gdata`, `.gstate`,
  `.gopt`, `.gplayer`, `.scmap`, `.sounds`, `.fnt`, `.atlas`).
- **GLB → POD converter** in a lazy-loaded chunk (imports `.glb`/`.gltf` into the APK as POD with
  textures).

### 3.6 Lua scripting
- Monaco editor with a **complete hand-written Lua API meta file** (`---@class`, `---@param`,
  `---@return` for every game API: `Camera`, `Character`, `CharController`, `Game`, `Scene`,
  `Spell`, `Health`, `Damage`, `Mini`, `AnimationController`, `ItemDrop`, `KeyframeAnimation`,
  `Light`, `PhysicsObject`, `SnappingMonsterController`, … — see section 6).
- "Edit Lua" opens any `Program`-typed field in Monaco with full LSP (hover, autocomplete,
  diagnostics) and writes back on save. Read-only view for inherited (library) components.
- Also a **decoded-text editor**: double-click any `.scene/.scl/...` asset → human-readable decoded
  text view with a **validate-on-save** that re-parses the text and shows the error before writing.

### 3.7 Keyboard shortcuts
`Ctrl+Z` undo, `Ctrl+Y`/`Ctrl+Shift+Z` redo, `Ctrl+C` copy, `Ctrl+V` paste, `Delete` delete,
`F11` fullscreen, `T/R/S` gizmo mode, `F` frame object, RMB+WASD fly.

---

## 4. What the web editor does BETTER than Ruby GG (and what to port)

> **Port status (refreshed 2026-09, task 0.0).** Rows 1, 2, 3, 4, 8 and 10 —
> the six "high" items — have all landed in Ruby GG. The ⛔/✅ marks in the
> "Ruby GG today" column are the ground truth; the surrounding parity audit is
> `docs/parity_ruby_gg_vs_ruby_imgui.md` and the live work list is
> `docs/MASTER_TODO_RUBY_GG_PARITY.md`.

| # | Web editor capability | Ruby GG today | Port value / effort |
|---|---|---|---|
| 1 | **Component-level copy/paste** (copy one component, paste onto another object with fresh ID) | ✅ **Ported** — `av::scene_paste_component` (fresh type id), `InspectorPanel` Copy/Paste + static clipboard, signal wired in `ruby_main_window` (master task 1.1) | Done |
| 2 | **Snapshot-encode undo/redo on the scene file** (undo of *any* edit, incl. mesh edits) | ✅ **Ported** — `Viewport3DWidget::push_scene_snapshot_undo(before, label)` on the per-scene `QUndoStack`; every mutating op goes through it, mesh commit included (master task 1.2) | Done |
| 3 | **Schema-driven property editors** (auto UI for every protobuf field, incl. unknown fields, with per-field docs) | ✅ **Ported** — `InspectorPanel` renders each `SceneComponentField` from the `filerift_schema` table (float/int/string/Program→"Edit Lua"/nested group; unknown → raw hex with field number) (master task 2.1) | Done |
| 4 | **Scale gizmo also scales shape/collision/rectangle payloads** (`scaleObjectData`) | ✅ **Ported** — `av::scene_scale_object_payload` (radius uses `max(sx,sy)`, dominant-axis ratio for models), called from the gizmo scale-end path (master task 1.3) | Done |
| 5 | **APK import + repack + re-sign in-editor** | 🟡 **Partial** — import + session registry + root rewiring landed (`apk::Session`); repack/export and the v1 signer are master tasks 4.1b/c | Medium / medium |
| 6 | **In-browser POD/ETC1/PVR decoders** (no native deps) | Ruby GG uses native `pod_load`/SDL_image — faster, more formats | Low (already superior) |
| 7 | **Ground polygon → ground mesh regeneration on commit** | Ruby GG now does this (boulder + `scene_load_bytes`) | Already ported ✓ |
| 8 | **Library/template inheritance UI** (Add Object from SCL template, Unlink Template, inherited components shown dimmed) | ✅ **Ported** — `TemplateInspectorPanel`: retarget combo, `[local]` vs dimmed `[inherited]` tree, Override / Unlink & Materialize / Reset (master task 2.4b). The `.scl` *file* studio (2.4a) is still open | Done (UI); file studio remains |
| 9 | **Game light engine preview + veil overlay** | Ruby GG has lighting rig preview | Partial (web uses custom overlay shader) |
| 10 | **Water + particle effect preview toggles** | ✅ **Ported** — `[FX]` toolbar toggle + View menu: animated water, portal vortex, 5-type particle engine with torch emitters (master task 3.1) | Done |
| 11 | **"Add to Scene" from model asset** (drops a Model object with computed LocalAABB) | ✅ **Ported** — `TemplatePalettePanel` → `add_model_object` measures POD bounds via `scene_build_local_aabb` (master task 2.3) | Done |
| 12 | **Decoded-text editor with validate-on-save for any protobuf asset** | 🟡 Ruby GG FileRift has binary→text; decode/encode round-trip validation exists for scene saves but is not generalized to every protobuf kind | Medium / low — task **4.3** |
| 13 | **FNV-signature dirty tracking** (only rebuild scene graph when bytes change) | Ruby GG rebuilds on structure change events | Low (perf nicety) |

### The single most instructive idea
The web editor's **"document = encoded bytes, undo = snapshot of bytes"** model is a simpler, more
robust design than Ruby GG's event-spaghetti for mesh edits. It sidesteps exactly the class of bug
fixed in this session (stale caches, revert-on-save, dirty-flag races): there is one source of
truth, and every mutation funnels through `snapshot() → mutate tree → notifyEdited()`.

---

## 5. Key snippets (annotated)

### 5.1 Protobuf wire-tree decode (generic, schema-driven)
```js
// index_beautified.js ~19800
function Ze(i) { return i.tag >> 3 }                       // field number from tag
class lM {                                                 // varint reader
    constructor(e) { this.buf = e; this.pos = 0 }
    varint() {
        let e = 0n, t = 0n;
        for (;;) {
            if (this.pos >= this.buf.length) throw new Error("varint past end");
            const n = this.buf[this.pos++];
            if (e |= BigInt(n & 127) << t, !(n & 128)) return e;
            if (t += 7n, t > 70n) throw new Error("varint too long");
        }
    }
    bytes(e) { ... }
}
function Ph(i, e, t, n, s) {                                // walk wire tree
    // wire 0 → varint, wire 1 → i64, wire 5 → i32 (float32 LE),
    // wire 2 → if schema says type: recurse as msg; else guess:
    //   try parse as msg (round-trip check dM), else latin1 str vs bytes (wd heuristic)
}
```

### 5.2 Snapshot undo/redo (the model to steal)
```js
// ~23980
class is extends ja {                                     // Document
    snapshot() {
        const t = this.encode(),                          // full re-encode to Uint8Array
              n = this.undoStack[this.undoStack.length - 1];
        n instanceof Uint8Array && EM(n, t)               // skip no-op snapshots
            || (this.undoStack.push(t),
                this.undoStack.length > is.MAX_HISTORY && this.undoStack.shift(),
                this.redoStack.length = 0);
    }
    undo() {
        const t = this.root;
        for (; this.undoStack.length;) {
            const n = this.decodeEntry(this.undoStack.pop());   // decode snapshot
            if (!jl(t, n))                                     // deep-equal? skip no-ops
                return this.redoStack.push(t),
                       this.applyRoot(t, n), !0;               // apply + diff + emit
        }
        return !1;
    }
    applyRoot(t, n) {
        const s = t.filter(a => Ze(a) === 1 && a.kind === "msg");  // old objects
        this.root = n; this.loadLibraries();
        let r; const o = this.objectFields();
        if (s.length === o.length) {                        // diff old vs new objects
            r = [];
            for (let a = 0; a < o.length; a++)
                jl(s[a].children ?? [], o[a].children ?? []) || r.push("obj" + a);
        }
        this.emit("structureChanged", { changedIds: r });   // minimal rebuild hints
    }
}
```

### 5.3 Component copy/paste (Ruby GG gap #1)
```js
// ~24300
pasteComponent(t, n) {                                      // t=target object, n=component
    this.snapshot();
    const s = Os([n])[0],                                   // deep clone (Os = deep clone tree)
          r = this.freshComponentId(t),                     // max existing ID + 1
          o = Oe(s.children, 2);                             // component ID field (2)
    o && o.kind === "varint" ? o.varint = BigInt(r)
                             : s.children = [Bn(2, r), ...s.children ?? []];
    (t.field).children ?? (t.field.children = []);
    t.field.children.push(s);
    this.emit("structureChanged", { changedIds: [t.id] });
}
```

### 5.4 Scale gizmo that also scales embedded data (Ruby GG gap #4)
```js
// ~24360 — called on gizmo scale end
scaleObjectData(t, n, s, r) {
    const o = a => (a?.kind) === "msg" && (                 // scale a Rectangle (4 floats)
        this.setF32Child(a, 1, he(a.children, 1) * n),
        this.setF32Child(a, 2, he(a.children, 2) * s),
        this.setF32Child(a, 3, he(a.children, 3) * n),
        this.setF32Child(a, 4, he(a.children, 4) * s));
    o(Oe(t.field.children, 8));                             // object LocalAABB
    for (const a of kt(t.field.children, 3)) {              // each component
        const c = Oe(a.children, _t.SHAPE);                 //   Shape → rect + circle + polygon pts
        if (c?.kind === "msg") { o(Oe(c.children, 1)); ... }
        const l = Oe(a.children, _t.COLLISION_SHAPE);       //   CollisionShape → radius fields 6/7
        if (l?.kind === "msg") { ... this.setF32Child(l, 6, he(l.children, 6) * r); ... }
    }
}
```

### 5.5 Ground polygon editor with live mesh regeneration (already ported to Ruby GG)
```js
// ~29630 Dp class: pointer-drag handles on the polygon plane
onMove: e => {
    ... this.raycaster.ray.intersectPlane(this.dragPlane, t);
    this.moved || (this.doc.snapshot(), this.moved = !0);   // undo point on first move
    const n = this.objectGroup.worldToLocal(t.clone());
    this.verts[this.dragging] = { x: n.x, y: n.y };
    this.handles[this.dragging].position.set(n.x, n.y, 0);
    this.rebuildOutline();
},
commit() {                                                  // on release
    ... write verts back to GroundPolygon + mapped Shape components;
    if (n && t) {                                           // regenerate GroundMesh from polygon
        const d = qS(n, s, this.verts, VS(t), nf(h), nf(u));  // GS = game's ground mesh algo
        rf(t, 4, d.minDepth); rf(t, 5, d.maxDepth); a = d.localAabb;
    }
    c && $S(e, c);                                          // update object LocalAABB
    this.doc.notifyEdited(this.objId);
}
```

### 5.6 Lua API meta (embedded LSP surface)
```lua
---@meta
---@diagnostic disable undefined-global
---@class SceneObject
SceneObject = {}
Mini = {}; AnimationController = {}; Camera = {}
Character = {}; CharController = {}; SnappingMonsterController = {}
CollectableItem = {}; CollisionShape = {}; Entity = {}
EntityController = {}; Game = {}; Health = {}; Damage = {}
Spell = {}; Program = {}; Scene = {}; ItemDrop = {}
KeyframeAnimation = {}; Light = {}
-- e.g.
function Camera.Rumble() end
function Camera.FocusAtShape(obj, rect) end
function Scene.AddObject(obj) end
function Scene.CreateObject(template, identifier, parent, x) end
function SceneObject:addComponent(component_class) end
function SceneObject:clone() end
function SceneObject:destroy() end
function SceneObject:identifier() end
function SceneObject:position() end
```
(Ruby GG's Script IDE has syntax highlighting; adding this meta file as an LSP/autocomplete
source would give the same hover/autocomplete experience.)

### 5.7 POD block reader header
```js
// ~27613
const mS = "AB.POD.2.0", gS = 2147483648;                    // end-of-list sentinel bit
const Ge = { version: 1000, scene: 1001, sceneNumMeshNodes: 2006, sceneNumFrames: 2009,
  sceneMesh: 2012, sceneNode: 2013, sceneTexture: 2014, sceneMaterial: 2015, sceneFPS: 2017,
  matName: 3000, matDiffuseTexIdx: 3001, matOpacity: 3002, matDiffuse: 3004, texFilename: 4000,
  nodeIndex: 5000, nodeName: 5001, nodeMaterialIndex: 5002, nodeParentIndex: 5003,
  nodePosition: 5004, nodeRotation: 5005, nodeScale: 5006, nodeAnimPosition: 5007, ...,
  meshNumVertices: 6000, meshNumFaces: 6001, meshNumUVWChannels: 6002, meshVertexIndexList: 6003,
  meshVertexList: 6006, meshNormalList: 6007, meshUVWList: 6010, meshBoneIndexList: 6012,
  meshBoneWeightList: 6013, meshInterleavedDataList: 6014, meshBoneBatchIndexList: 6015,
  meshNumBoneIndicesPerBatch: 6016, meshBoneOffsetPerBatch: 6017, meshMaxNumBonesPerBatch: 6018,
  meshNumBoneBatches: 6019, blockDataType: 9000, blockNumComponents: 9001, blockStride: 9002,
  blockData: 9003 };
```
This tag table **exactly matches the FileRift POD format** used by Ruby GG's `pod_load`
(verify against `src/tools/av_renderer.h`'s `swk::pod` readers) — a good cross-check for the
native POD writer/reader.

### 5.8 Texture decoders in JS
- `Ep`/`Tp` — PVR v2 header (`version==52`, magic `0x21525650`), pixel formats `18` (RGBA8888) and
  `54` (**ETC1**, with a full software block decoder `wS` — 4×4 blocks, RGB444/555 base colors,
  intensity tables).
- `Cp` — gzipped `.tex`: header `{format:u32,width:u32,height:u32}` + raw pixels; 8 formats:
  `1` RGBA8888, `2` RGBA4444, `3` RGBA5551, `4` RGB888, `5` RGB565, `6` LUMINANCE8,
  `7` ALPHA8, `8` LUMINANCE_ALPHA88.
- `AS` — candidate-name search exactly like Ruby GG's `texture_candidates`:
  `[name, base_2x.pvr, base.pvr, base_2x.tex.png, base.tex.png, base_2x.png, base.png]`.

---

## 6. Feature-by-feature comparison table

| Feature | Web editor (szkot.xyz) | Ruby GG | Notes |
|---|---|---|---|
| Scene file open/save | ✅ APK/ZIP + loose, byte-exact write-back | ✅ loose + FileRift structured save | Ruby GG adds structured (readable) save |
| Undo/redo | ✅ full-file snapshot (100 deep) | ⚠️ partial (text QUndoStack + mesh session snapshot) | **Port candidate** |
| Object copy/paste | ✅ object + **component** | ✅ object | component-level missing |
| Duplicate | ✅ | ✅ | |
| Delete | ✅ (with confirm) | ✅ (with confirm) | |
| Rename | ✅ | ✅ | |
| Templates (SCL) | ✅ inherit + unlink + dimmed inherited | ⚠️ template-aware load, no UI | port candidate |
| Add Component from list | ✅ | ✅ (subset?) | web covers all ~50 classes |
| Property editors | ✅ schema-generated, per-field docs, unknown-field support | ✅ typed editors, narrower | |
| Lua editing | ✅ Monaco + full API meta LSP | ✅ QScintilla-style IDE, no API LSP | add meta |
| Ground polygon edit | ✅ drag handles + live mesh regen | ✅ (in-scene mesh editor, this session) | ported ✓ |
| Scale gizmo scales data | ✅ (`scaleObjectData`) | ⚠️ transform only | **port candidate** |
| Game lights preview | ✅ custom veil overlay | ✅ lighting rig | |
| Water animation | ✅ | ⚠️ static | |
| Particle preview | ✅ (5 emitter types, WIP) | ❌ | |
| Collision debug volumes | ✅ translucent + outlines | ✅ (ShapeComponent debug) | |
| Background parallax plane | ✅ | ✅ | |
| Model preview (POD) | ✅ | ✅ | Ruby GG native & faster |
| ETC1/PVR decode | ✅ JS | ✅ native (SDL_image) | |
| GLB import → POD | ✅ | ⚠️ partial | |
| APK repack + sign | ✅ in-browser | ⚠️ external tools | |
| Audio preview | ✅ `<audio>` WAV (browser) | ✅ SDL3 WAV/MP3/OGG (better) | Ruby GG better |
| Multi-doc tabs | ✅ Monaco-style doc tabs | ✅ QTabWidget docs | |
| Fullscreen | ✅ F11 | ✅ | |

---

## 7. Recommended ports to Ruby GG (priority order)

1. **Component-level copy/paste** (`pasteComponent`, `freshComponentId`, deep-clone `Os`) —
   small, high value; wire into the Scene Hierarchy context menu and `Ctrl+C`/`Ctrl+V`.
2. **Scale-gizmo payload scaling** (`scaleObjectData`) — fixes a real correctness gap where
   shape/collision geometry does not follow the transform.
3. **Snapshot-based undo for scene edits** — unify gizmo/property/mesh edits under one undo stack;
   this is the robust fix for the stale-cache/revert classes of bugs hit this session.
4. **Lua API `---@meta` file** — drop the web editor's (or `jni_bridge`-derived) meta into the
   Script IDE for autocomplete/hover of the full game API.
5. **Schema-driven property panel** — port the `Ft.types`-style field table (already largely
   present in `docs/sre13` + FileRift) so every field gets a correct editor with docs.
6. **ETC1/PVR decode fallbacks** — only if a target platform lacks SDL_image support.

## 8. Risks / caveats
- The web editor is labeled **"early testing prototype … could even corrupt your files"** — do not
  treat its schema as authoritative over FileRift/`scene_loader`; treat it as a cross-check.
- Its **`SceneObject` mapping differs slightly** from the recovered headers in field numbering
  (e.g. it treats `Position` as a `Vector2` + separate `Depth`, while `SceneObject.h` uses
  `pos_x/pos_y/pos_z` floats) — verify against the game's real write path before adopting.
- Snapshot undo is O(file size) per edit; fine for scenes, wasteful for `.gdata`/profiles — cap the
  stack (theirs is 100) and skip no-op snapshots (they do).
- ETC1 software decode is slow in JS; in C++ use the GPU (already done via SDL_image/GL).

## 9. Artifacts kept for reference
- Raw bundle: `/tmp/szkot_editor/szkot.xyz/SwordigoEditor/assets/index-u_qz_Wej.js`
- Beautified: `/tmp/szkot_editor/szkot.xyz/SwordigoEditor/assets/index_beautified.js` (31,819 lines)
- Monaco bundle: `/tmp/szkot_editor/editor.main.js` (3.8 MB, editor only)
- Page shell: `/tmp/szkot_editor/szkot.xyz/SwordigoEditor/index.html`