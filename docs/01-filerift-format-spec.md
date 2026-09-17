# 01 — FileRift Format Specification (`.scl` / `.scene`)

Derived by decoding real shipping assets with the project's own native decoder and
cross-checking the field map against the game binary's own symbol table. Nothing here is
transcribed from another engine's format.

## 0. Provenance of every claim in this document

| Claim | How it was established |
| --- | --- |
| Lexical shape, quoting, banner, heredoc | `bin/ruby_cli -d de_in` on `hiro.scl`, `town_shop.scene`, `credits.scene` copied from `~/.local/share/swordigo-desktop/assets/resources/` |
| Field names + tag numbers + per-class membership | `python3 tools/extract_component_schema.py libswordigo.so --json` → 133 classes / 624 fields (dumped to `.scratch/graphy_recon/component_schema.json`) |
| Component class → extension slot (100…560) | `tools/extract_component_schema.py libswordigo.so --components` → 81 component extensions |
| Legacy dialect differences | committed corpus `decoded_rln/*.scl` (`hiro.scl`, `rlsw.scl`) |
| Root flattening | `hiro.scl:788` (`ImportedLibrary` at column 0) and `town_shop.scene:2058` (`ObjectLibrary{` nested) |

Re-run all of the above with:

```bash
bin/ruby_cli -d <dir-of-binary-assets>          # -> ./de_out/*.scl|.scene
python3 tools/extract_component_schema.py libswordigo.so --json > schema.json
```

**Critically: the on-disk game assets are binary protobuf-lite. The text grammar below is
FileRift's representation.** Do not assume it matches Unreal/Unity/Godot scene formats;
it does not, and every structural oddity in §5 is Swordigo-specific.

---

## 1. File kinds

| Extension | Root message | Root emission style | Purpose |
| --- | --- | --- | --- |
| `.scl` | `ObjectLibrary` | **flattened** — root fields at column 0, no wrapping braces | reusable object templates |
| `.scene` | `Scene` | **flattened** — `Object{…}` blocks at column 0, with a nested `ObjectLibrary{…}`, `Bounds{…}`, `Group{…}` | a placed level |
| `.gdata`, `.fr`, `.gopt`, `.gstate`, `.gplayer`, `.scmap`, `.sounds`, `.atlas`, `.fnt` | various `GameData` sub-messages | per-kind | other subsystems (out of scope here, but the CLI handles them) |

Filenames are the identity: `hiro.scl` → `ObjectLibrary.Name = 'hiro'`;
`town_shop.scene` → `Scene` whose nested `ObjectLibrary.Name = 'town_shop'`.

---

## 2. The banner line

Every file emitted by the current decoder starts with exactly one comment line and one
blank line:

```
## FileRift decoded Swordigo file type: scl
<blank>
```

For `.scene` the type token is `scene`. This banner is **the only `##` comment in the
new dialect** (verified: `grep -c '##' town_shop.scene` → `1`). The legacy dialect uses
`##` for per-block type annotations instead (§7). The decoder's re-encoder derives the
type token from the *output extension*, so the banner must survive a round-trip.

---

## 3. Lexical grammar

```
file            := banner? ( statement )* EOF
banner          := "##" SP "FileRift decoded Swordigo file type:" SP typeid NL NL

statement       := block | assignment

block           := ident ( SP )? "{" NL ( statement )* indent? "}" NL
assignment      := key SP? ":" SP? value NL
assignment_nc   := key SP value NL                     ; colon-less, tolerated on read

key             := ident
ident           := [A-Za-z_][A-Za-z0-9_]*
value           := string_lit | number | heredoc | bytes_lit | enumident

string_lit      := "'" ( [^'] | "\\'" )* "'"           ; current dialect
                 | '"' ( [^"] | '\\"' )* '"'           ; legacy dialect, also accepted
number          := "-"? digit+ ( "." digit+ )? ( ("e"|"E") ("+"|"-")? digit+ )?
heredoc         := "$" NL (any-char)* NL "$end"
bytes_lit       := "'" ( "\\x" hex hex | "\\\\" | "\\'" | [^'\\] )* "'"

indent          := 4 spaces (canonical; never semantically significant)
NL              := "\n"
```

### 3.1 Golden rules

1. **Indentation is cosmetic.** The decoder writes 4 spaces per level; the parser must
   rebuild nesting from `{` / `}` only. `tools/scl_to_graph.py` / `scene_to_graph.py`
   are already brace-stack parsers — keep that property, never regex the nesting.
2. **`Object{` has no space before the brace; `Component{`, `Position{`, `Template{`,
   `Bounds{`, `Group{`, `ObjectLibrary{` also have none.** But scalars use ` : ` with
   spaces. Do not depend on spacing for tokenisation — `key` terminates at the first `{`
   or `:`.
3. **Strings are single-quoted in the current dialect and always unescaped-looking.**
   The decoder does **not** escape embedded quotes or backslashes in ordinary string
   fields; a value containing `'` would be emitted raw and is a known degenerate case
   (none observed in the corpus — `grep -n "\\\\'" **/*.scl` returns nothing).
   Consequence: a naive `strip("'")` is *usually* right but must be paired with a check
   that the value is quote-balanced, and it must be paired with an escape-decoder for
   the `Bytes` field only.
4. **Trailing commas are legacy-only.** Current output has none. The legacy reader must
   tolerate them.
5. **Repeated keys are lists, order-preserved.** `SwingComponentId` appears 3× on the
   same component; `ImportedLibrary` appears 13× on a library; `Parameter` appears 14×
   on one `ParticleEmitter`. Any model that stores scalars in a `map<string,string>`
   loses data. See §6.4.
6. **`Bytes` values are hundreds of characters long and are a single logical token.**
   Never line-split them. The legacy corpus emits them with C escapes inside
   double quotes (`"\x1bLuaQ\x00..."`); the current dialect emits single-quoted
   escapes (`'\x1bLuaQ\x00...'`). Both decode to the same byte string. See §6.3.

---

## 4. Value encoding rules (measured)

Everything in the decoded output is one of: quoted string, bare number, heredoc, or
escaped-bytes string. **Booleans are integers.** `Hidden : 0`, `Collides : 1`,
`ExecuteOnce : 1`, `TapToEnter : 0`, `Locked : 1`, `LocalSystem : 1`. There is no
`true`/`false` token in the serialised output; the `true`/`false` substrings that appear
in a decoded file are inside Lua source or Lua string literals
(`scene_to_graph.py` is therefore wrong to model `PhysicsEnabled` as `PIN_BOOLEAN`
from a textual `true` — it must be `1`/`0` as `PIN_INT`/`PIN_BYTE`).

**Floats are printed with lossy precision.** `NormalMaxJumpTime : 0.230000004` is the
`float` `0.23f` printed through `%g`-ish formatting of a widened double; `Town_shop`
shows `Depth : 1.72038269`. Enums that are 32-bit floats in the wire format appear as
integers when integral (`Rotation : 0`, `Scaling : 1`, `Parameter : 90.0`). Rule: **do not
round-trip floats through a decimal formatter.** Keep the original textual token verbatim
for any field you are not modifying (see §8).

**Absent vs. zero.** A field is only emitted when it is *present* in the protobuf. So
`Depth : 1.72038269` appears for placed objects but `OnLoad{…}` appears only when a script
exists. The graph backends must treat *absent* and *zero* as different states, because
`DefaultAnimationControllerId` absent ≠ `= 0`.

---

## 5. Structural shapes, file by file

### 5.1 `.scl` — `ObjectLibrary`, flattened

```filerift
## FileRift decoded Swordigo file type: scl

Name : 'hiro'
Template{
    Object{
        Identifier : 'hiro'
        Component{ … }
        …
    }
}
ImportedLibrary : 'magic'
ImportedLibrary : 'rubymath'
```

`ObjectLibrary` schema (authoritative, tag order is the wire order):

| Field | Tag | Notes |
| --- | --- | --- |
| `Name` | 1 | library identity; also the filename stem |
| `Template` | 2 | repeated; each holds one `Object` + an optional `Scaling` |
| `ImportedLibrary` | 3 | repeated string; the dependency edges of the library graph |
| `Texture` | 4 | repeated; embedded texture payloads |
| `Program` | 5 | library-level program slot |

`ObjectTemplate` = `{ Object(1), Scaling(2) }` — note `Scaling` on the *template*, distinct
from `SceneObject.Scaling(7)`.

### 5.2 `.scene` — `Scene`, flattened, with nested library

Tail of `town_shop.scene` (verbatim structure):

```filerift
Object{ … }                       ← 40+ placed objects at column 0
ObjectLibrary{
    Name : 'town_shop'
    Template{
        Object{
            Identifier : 'Template 1'          ← see §6.5
            Position{ X : 0  Y : 0 }
            Depth : 0
            Rotation : 0
            Scaling : 1
            LocalAabb{ X : -10  Y : -10  Width : 20  Height : 20 }
            Hidden : 0
        }
        Scaling : 1
    }
    ImportedLibrary : 'caves_stuff'
    …
}
Bounds{ X : -450  Y : -300  Width : 1650  Height : 1000 }
Group{ Identifier : 'bg_group'   ObjectIdentifier : 'obj2#2'   Locked : 1 }
Group{ Identifier : 'pedastalgroup'
       ObjectIdentifier : 'obj1#7'  ObjectIdentifier : 'obj3#8'  ObjectIdentifier : 'obj1#4' }
```

`Scene` schema: `Object(1)` repeated, `ObjectLibrary(2)` singular,
`Bounds(3)` singular, `Group(4)` repeated, `OnLoad(5)` singular. Note that on the wire
`SceneObject` is field 1 and `ObjectLibrary` is field 2, but the decoder emits **all
`Object` blocks first, then the `ObjectLibrary` block** — i.e. the text order is not the
wire order across repeated-vs-singular siblings. A re-encoder must therefore either
(a) emit in the same grouped order, or (b) prove the encoder is order-insensitive
(it is, for distinct tags). The existing `ruby_cli recode` path already does (b).

### 5.3 `SceneObject`

Authoritative tag map:

| Field | Tag | Text form |
| --- | --- | --- |
| `TemplateName` | 1 | string, **optional** |
| `Identifier` | 2 | string, **required** |
| `Component` | 3 | repeated block |
| `Position` | 4 | nested `Vector2{ X, Y }` |
| `Depth` | 5 | float |
| `Rotation` | 6 | float, radians, Z axis |
| `Scaling` | 7 | float, uniform |
| `LocalAabb` | 8 | nested `{ X, Y, Width, Height }` |
| `Hidden` | 9 | int 0/1 |
| `OnLoad` | 10 | `Program` block |

There is **no** `Position.Z`.** X/Y are world-plane; `Depth` is the separate parallax
axis, and `LocalAabb` is a *rectangle* (`X, Y, Width, Height`) not min/max. The brief
described the transform as "`Position`/`Depth`/`Rotation`/`Scaling`", which is correct,
but it is worth stating that this is a **2.5D** transform: the visual timeline's
"position" is genuinely 2-D plus a depth plane, so the ghost drag is a 2-D drag with a
separate depth slider — not a 3-D gizmo. `SceneObjectGroup.Locked(6)` plus
`Bounds(3)` are what the Scene backend uses to lay out its pan/zoom world view.

### 5.4 `Component`

| Field | Tag | Text form |
| --- | --- | --- |
| `ClassName` | 1 | string — the class token, e.g. `'CollisionShape'` |
| `Identifier` | 2 | int — **local to the owning object** |
| `Label` | 3 | string — optional human label (currently unused by Graphy) |
| `ParentComponentIdentifier` | 4 | int — **structural parent edge** |
| `<PayloadBlock>` | ≥100 | see §5.5 |

`ClassName` is **not** the payload block's name. `ClassName : 'CollisionShape'` is followed
by **two** payload blocks, `ShapeComponent{…}` and `CollisionShapeComponent{…}`;
`ClassName : 'Background'` → `BackgroundComponent{…}`; `ClassName : 'Particle'` →
`ParticleComponent{…}`; `ClassName : 'Program'` → `ProgramComponent{…}`. The payload block
is identified by its **extension slot**, not by string-matching `ClassName + "Component"`.
The 81 valid `(slot, block-name)` pairs are produced by
`bin/ruby_cli`-compatible `tools/extract_component_schema.py --components`; see
`02-graphy-structural-backend.md` §3 for the generated table.

A component may therefore be a **multi-block** component (`ShapeComponent` +
`CollisionShapeComponent`), which the current Graphy converter flattens by taking
`children[0]` only:

```python
payload = None
for child in comp.children:
    if child.name.endswith("Component") or child.name in ["ShapeComponent","Program"]:
        payload = child
        break          # <-- drops CollisionShapeComponent entirely
```

That is a real data-loss bug in the current converters, independent of the four visual
bugs.

### 5.5 Worked real example — `hiro.scl`, component 1

```filerift
Component{
    ClassName : 'CharController'
    Identifier : 1
    CharControllerComponent{
        DefaultAnimationControllerId : 117
        RightWeaponControllerId : 3
        NormalRunSpeed : 230
        JumpSpeed : 250
        NormalMaxJumpTime : 0.230000004
        LeftWeaponControllerId : 0
        EntityId : 5
        SwingComponentId : 8
        SwingComponentId : 15
        SwingComponentId : 6
        LiftAnimationControllerId : 120
        …
        JumpLandSoundId : 21
    }
}
```

Three things to notice: (a) `SwingComponentId` is **repeated** — three outgoing edges from
one field name; (b) `EntityId : 5` is a *cross-object* reference shape (an entity id, not
a component id), so the auto-wire resolver needs to know which refs are object-scoped;
(c) the field order in the text is **not** tag order (tag 7 `EntityId` is printed after
tags 2/6) — it is the order the protobuf serializer walked the fields, so a re-encoder
must preserve observed order per instance rather than sorting by tag.

### 5.6 `Program` — the hook payload

`Program` = `{ String(1), Bytes(2), Name(3) }`, emitted as:

```filerift
OnCollide{
    String : $
local self, target, normal, groundCollision = ...;
…
$end
    Bytes : '\x1bLuaQ\x00\x01\x04\x04\x04\x08\x00@\x00\x00\x00local self = ...;\r\n…'
}
```

Rules:

- **Both** `String` and `Bytes` are expected to be present and consistent. `String` is the
  authoritative source; `Bytes` is what the VM loads.
- The heredoc opener is exactly `String : $` and the terminator is a line whose stripped
  content is `$end`. The *body is verbatim* including `\r` and blank lines. In
  `town_shop.scene`, `item1`'s body is
  `\nlocal self = ...;\r\n\r\nCreateShopItem(self, "healingpotion", 50);\n`
  — a `\n` after `$`, then CRLF line endings from the original authoring tool.
- `Bytes` is a Lua 5.1 chunk: `1B 4C 75 61 51` = `\x1bLuaQ`, then
  `00 01 04 04 04 08 00` = endianness 1, `sizeof(int)=4`, `sizeof(size_t)=4`,
  `sizeof(Instruction)=4`, `sizeof(lua_Number)=8`, integral flag 0. **The source name and
  line-info sections embed the `String` text, including the CRLF**, so the byte length
  recorded in the chunk depends on exact line endings. This is why the compiler pipeline
  must be byte-exact (`05-compiler-pipeline.md` §6).
- `Program.Name(3)` is absent in every observed sample; do not rely on it.
- The heredoc body is **not** indented by the decoder even though the surrounding block is
  at 16 spaces. So the `$end` terminator arrives at 16 spaces of indent while the body
  lines are at column 0. A parser must accept `$end` with arbitrary leading whitespace
  (both `scl_to_graph.py` and `scene_to_graph.py` already use `$end in stripped`).

### 5.7 `ObjectLibrary.Program` and `ProgramComponent`

Two "generic program" carriers exist and they are **different**:

- `ProgramComponent` = `{ ExecuteOnce(1), Program(2), Enabled(3), Trigger(4) }`,
  reached via `ClassName : 'Program'`. This is `item1` in `town_shop.scene`
  (`ExecuteOnce : 1`, `Enabled : 1`, `Trigger : 10`). This is the "one action at t=0,
  no motion" pattern.
- `ObjectLibrary.Program(5)` — a library-level program (schema-present; not observed in
  the sample `.scl` files we decoded).

`Trigger : 10` is an enum whose meaning is not recovered from the schema (candidate: a
trigger mode such as "on enter / on interact"). **Open question — flagged in §9.**

---

## 6. Edge cases (all verified against the corpus)

### 6.1 Quote style

Current dialect: `'…'`. Legacy: `"…"`. Both must be accepted; new writes always use `'…'`.
Do **not** strip a single quote pair blindly — first check the string is balanced, because
Lua bodies inside `$…$end` are not quoted at all, and `Bytes` uses C escapes.

### 6.2 Colon-less assignment

The project's own spec (`src/ruby/docs/filerift_format_specification.md`) claims
`Scaling 1.5` and `Position { X: 120.0, Y: 45.0 }` (inline, comma-separated) are valid.
**Neither form appears in any decoded shipping asset**, and `ruby_cli` never emits them.
`grep -nE "\{[^}]*,[^}]*\}"` over the decoded samples returns **zero** matches. Treat the
existing spec's §3.2/§3.3 as aspirational, not authoritative. The reader *may* tolerate
colon-less assignment defensively, but the writer must never emit it.

### 6.3 `Bytes` escape decoding

Two escaping conventions in the corpus for the same field:

```
current: Bytes : '\x1bLuaQ\x00\x01\x04\x04\x04\x08\x00@\x00\x00\x00local self = ...;\r\n\r\nCreateShopItem(...)\x00\x00\x00\x00…'
legacy:  Bytes : "\x1bLuaQ\x00\x01\x04\x04\x04\x08\x00\x00\x00\x00\x00\x00\x00\x00\x02\n*\x00\x00\x00%\x00\x00\x01F…"
```

Decoding is byte-wise: `\xHH` → byte, `\r`, `\n`, `\t`, `\\`, `\'`/`\"` → the char.
Note the contained literal `local self = ...;` (printable ASCII is emitted raw) and the
`\r\n` inside. Bytes lengths observed up to ~2 KB; do not assume a small field.

### 6.4 Repeated fields, in order

Repeated fields observed: `Component(3)` on `SceneObject`, `SwingComponentId`,
`ParticleId` (3× on `blackhole`), `ModelBindingId`, `Parameter` (14× per emitter),
`ImportedLibrary` (13× on `town_shop`), `ObjectIdentifier` (3× on `pedastalgroup`),
`Texture`/`Subtexture`, `BoneIndices`/`BoneWeights`, `Component` nested inside
`ObjectLibrary.Template.Object`, and `KeyframeAnimation…`-style key arrays.

**Because the decoder emits repeated scalars as consecutive identical keys, the reader
must append, never overwrite.** The current Python converters do this correctly for
scalars (`scalars[key]` becomes a list) but the *graph* layer then drops the multiplicity
by keying refs on the field name (`comp_ref_inputs[ref_key] = …`), so `hiro`'s three
`SwingComponentId`s produce one wire. `02-…` §5.4 fixes this.

### 6.5 `TemplateName` is frequently a placeholder

Verified in `town_shop.scene`:

- `item1` — **no `TemplateName` at all** (fully inline definition).
- `item2`, `item3` — `TemplateName : 'shop_item'` (real template reference).
- `Background`, `DirectionalLight`, `iapstoremodel`, … — `TemplateName : 'Template 1'`,
  which resolves to the dummy `Object{ Identifier : 'Template 1' }` inside the scene's own
  nested `ObjectLibrary` (a minimum bounding object with `LocalAabb` 20×20).
- `new_level2_graph.json` shows the same pattern: every node's subtitle is
  `Tpl: SceneObject · (Z: 0.0)`.

So `'Template 1'` is the legacy "every object has a template" artefact, not a meaningful
reference. **The Scene backend must classify a `TemplateName` as
`placeholder | resolved | absent`** rather than assuming it points at a real archetype.
`item1` (absent) and `item2` (resolved) are the two authoring styles the brief asks us to
support, and both are in this one file.

### 6.6 Object identifier suffixing

`town_shop.scene` contains `obj1`, `obj1#2`, `obj1#3` … `obj1#7`, plus `obj3#8`. The `#N`
suffix is a duplicate-name disambiguator applied by the *scene authoring tool*, and
`Group.ObjectIdentifier` references use it verbatim (`'obj1#7'`). Any identifier-to-node
map must be keyed on the full string including `#N`; do not normalise.

### 6.7 `Scene.Find("…")` is a string reference, not an integer one

The Lua bodies contain `Scene.Find("elder")`, `Scene.Find('elder')` (both quote styles
inside scripts) and `self:identifier() .. "_bubble"` constructions. `scene_to_graph.py`
already regexes `Scene\.Find\(["']([a-zA-Z0-9_#]+)["']\)`. Because identifiers can be
runtime-built (`"portal_effect_" .. self:identifier()`), these regex edges are
**heuristic** and must be labelled as such in the graph (dashed wire), never presented as
authoritative. This distinction matters for the Scene backend: it is a *scripted* edge,
not a data edge.

### 6.8 Long heredocs and `$end` inside Lua

No instance of the literal `$end` inside Lua string data was observed, but
`credits.scene` contains `local self =...;` (no space before `=`) and 60+ line scripts.
The heredoc scanner must:
- detect the opener on `String : $` **only** (not on `Bytes`),
- scan for a line whose `strip()` equals `$end`, and
- never re-indent or re-wrap the body.

### 6.9 Objects with zero components

`new_level2_graph.json` shows nodes `obj3 [None]` with no payload at all, and
`town_shop.scene`'s `Group` members are pure markers. The backends must render a node
with no components and no wires without collapsing its height to the header.

---

## 7. Legacy dialect — complete diff list

| Aspect | Current (`ruby_cli` v5.8.6) | Legacy (`decoded_rln/*.scl`) |
| --- | --- | --- |
| Banner | `## FileRift decoded Swordigo file type: <t>` + blank line | none |
| String quoting | `'single'` | `"double"` |
| Trailing commas | none | every scalar ends with `,` |
| Block annotations | none | `Component{  ## Component`, `Object{  ## SceneObject`, payload blocks annotated with their message type (`## CharControllerComponent`, `## Vector3`, `## FloatColor`, `## Program`, `## ParticleEmitter`) |
| Float formatting | `230`, `0.230000004` | `230.0`, `0.23000000417232513` (full double expansion) |
| Spaces in braces | `Identifier : 'hiro'` | `Identifier : "hiro",` (same, but with comma) |

**Implication for the docs' worked examples:** `decoded_rln/*.scl` is the *better* source
for understanding wire semantics (the `##` annotations literally name each protobuf
message), while `bin/ruby_cli` output is the *better* source for the grammar we must
emit. `05-compiler-pipeline.md` quotes the legacy file for the `blackhole` semantics and
the current dialect for the write-back target.

---

## 8. Round-trip / re-encode contract (normative)

This is the contract every writer in this project must satisfy.

1. **`decode` is total and lossless enough to re-encode.** `bin/ruby_cli` already proves
   this for whole files: `decode(x) → recode → decode` is a fixpoint on the corpus
   (`de_in` → `de_out` → `re_out`). Any new writer must preserve that property for the
   subset of fields it touches.
2. **Untouched fields must be byte-identical.** The `.scl`/`.scene` writer must be a
   *surgical* editor: it rewrites only the `Program{String, Bytes}` block it owns and
   copies every other byte of the enclosing object verbatim. Do not re-serialise the
   whole file from a parsed tree — float formatting (§4) and repeated-field order (§6.4)
   are lossy through a naive model.
3. **Never normalise line endings.** The heredoc body and the chunk both contain `\r\n`
   in shipping data.
4. **Absent stays absent.** Removing a field you set to zero is a behaviour change
   (`DefaultAnimationControllerId = 0` vs absent).
5. **Encode `Bytes` with `\xHH` for non-printable bytes and raw ASCII otherwise**, inside
   single quotes, matching the current dialect exactly. Do not add `\r`/`\n` as literal
   escapes if the current decoder emits `\r`/`\n` escapes — match it, then verify with
   `ruby_cli recode` that the decoded text is identical.
6. **Field order within a block is preserved as observed**, not sorted by tag. The one
   exception is a newly *inserted* field, which is appended at the end of its block
   (which is what the protobuf path does for a newly-set tag anyway).

### Verification harness (to be written as part of the implementation)

```
for f in <corpus>:
    d0 = decode(f)
    r  = recode(d0)                 # markup -> binary
    d1 = decode(r)
    assert d0 == d1
    s  = inject_program(d0, …)      # our surgical writer
    assert decode(recode(s)) == inject_program(d1, …)
```

`tests/filerift_smoke.cpp` and `bin/filerift_smoke` already exist as a starting point.

---

## 9. Open questions / ambiguities

1. **`ProgramComponent.Trigger : 10`** — enum values unknown. Used by `item1`/`item2`/
   `item3`/`itemcheck`. Needs either an IDA pass on the enum or an empirical test.
   Blocks the "when does an action marker fire" semantics for `ProgramComponent` hooks
   (`03-…` §7 assumes it is an enter/exit trigger and gates on it conservatively).
2. **`Component.Label(3)`** — never observed non-empty. Unclear whether it is
   author-visible. Until resolved, Graphy shows `ClassName` and `Identifier` only.
3. **Object-scoped vs component-scoped references.** `CharControllerComponent.EntityId`
   and `MonsterControllerComponent.EntityId` look like *SceneObjectGroup/entity* ids
   while `SwingComponentId` is a *component* id. The schema gives no scope annotation, so
   `02-…` §5.5 derives scope from the field-name family (`*EntityId*` → object scope,
   otherwise component scope) and quarantines ambiguous cases in the UI.
4. **`Scene.OnLoad(5)` vs `SceneObject.OnLoad(10)`** — both exist, and
   `town_shop.scene` does not exercise `Scene.OnLoad`. Whether the scene-level hook can
   see objects that are not yet loaded is unknown; the timeline backend treats it as
   "before objects" and warns.
5. **`.scmap` / `MapNode_Portal`** — `PortalComponent.DestinationSceneName` +
   `SpawnPointName` are plain strings, but there is a separate world-map structure
   (`map_loader.cpp`, `map_editor.cpp`) with node/zone/portal indices. The Scene backend
   should offer a "follow portal" jump that uses the map, but that is out of scope for
   v1.
