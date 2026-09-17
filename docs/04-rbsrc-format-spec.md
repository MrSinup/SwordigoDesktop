# 04 — `.rbsrc` Intermediate Format Specification

`.rbsrc` is Graphy's **recording** — the thing the timeline produces and re-opens.
It is *not* Lua and *not* a graph: it is a superset of what Lua can express, plus the
provenance needed to re-open, re-edit and re-target it.

---

## 1. Why an intermediate format at all

### 1.1 What Lua 5.1 cannot express (and therefore loses)

| Timeline concept | Why Lua loses it |
| --- | --- |
| `(time, property, value, easing)` | Lua has only the *compiled* `for`/`Program.Wait` loop. Easing identity, original key times, and key values that were overwritten by interpolation are gone. |
| Loop region `[t0, t1)` | Only visible as the loop's bound expression, entangled with unrelated control flow. |
| Action marker `(fn, args, t)` | Only visible as a call site; the "at time t, as a marker" intent is gone. |
| Provenance (which object/hook/template) | Not present at all. |
| Muted / disabled tracks, comments | No representation. |
| Data-bounded loop state (`blackhole`'s `exper`) | Present but unstructured. |

### 1.2 Requirements

1. **Lossless and re-openable.** Load → edit → save must not drop anything the editor can
   edit, including fields the user did not touch.
2. **Deterministic.** The canonical writer must be a pure function of the model, so two
   saves of an unchanged model are byte-identical (makes `.rbsrc` diff-friendly and makes
   `A3`/`B5`-style tests possible).
3. **Human-editable.** A user must be able to hand-fix a keyframe in a text editor without
   a host application. (This is the deciding argument against a binary format and against
   a JSON blob with floats printed to 17 digits.)
4. **Versioned and migratable.** A `.rbsrc` written by an older Graphy must load, with a
   recorded migration, never silently.
5. **Provenance-bearing but target-agnostic.** It records the object/hook it was recorded
   against, *and* it must compile against a different target object
   (`03-…` §9, criterion B7).
6. **Verifiable.** It carries hashes sufficient to detect that the *asset* changed under it.

### 1.3 Why the surface looks Ruby-ish

The brief calls for "a separate process (written in Ruby)" and names the format
`.rbsrc` = *Ruby script source*. Given the ambiguity flagged in `00-…` §5.1, the format is
specified as **Ruby-syntax-shaped, block-structured text**:
`name "value"`, `key { … }`, comments with `#`. That surface is:
- trivially parseable by C++ (a brace-stack reader, same shape as `01-…`'s FileRift
  reader, so one reader implementation pattern serves both),
- *evaluable* by a real Ruby interpreter later if the project wants a genuine Ruby
  producer, without changing the on-disk format,
- and diff-friendly.

The canonical writer lives in C++ (`tools/ruby_cli.cpp` subcommand, §8). A Ruby facade is
optional and must not become a dependency.

---

## 2. Canonical grammar

```
rbsrc            := "rbsrc" SP version NL NL ( section )*
version          := digit+ "." digit+
section          := "recording" block | "tracks" block | "state" block
                  | "markers" block | "loops" block | "cache" block
block            := "{" NL ( entry )* "}"
entry            := capture | named_block | comment | NL
capture          := key SP value NL
named_block      := ident SP string_lit SP block
comment          := "#" (any)* NL

key              := [a-z_][a-z0-9_]*
ident            := [A-Za-z_][A-Za-z0-9_.]*
value            := number | integer | string_lit | "true" | "false"
string_lit       := '"' ( [^"\\] | "\\" any )* '"'
number           := "-"? digit+ "." digit+ ( ("e"|"E") ("+"|"-")? digit+ )?
integer          := "-"? digit+
```

Rules:

- **Exactly one blank line between top-level sections.** Within a block, one entry per
  line, indented two spaces per level.
- **Keys are `snake_case`; identifiers (track targets, function names) preserve case.**
- Strings use **double** quotes with `\"`, `\\`, `\n`, `\t` escapes. (Deliberately the
  opposite of FileRift's single quotes, so a `.rbsrc` string can never be confused with a
  FileRift field when eyeballing a diff. This is convention, not necessity — but the two
  formats coexist on disk and the distinction is worth forcing.)
- **Floats are printed with `%.9g`.** Nine significant digits is chosen because every
  shipping float in the assets was originally a 32-bit `float`, whose full precision is
  recoverable within 9 significant decimal digits (`01-…` §4). More digits would produce
  noise diffs; fewer would corrupt values.
- **Order of entries is fixed by this specification** (each block below lists its required
  order). The reader must accept any order; the writer must emit canonical order.
- **Comments are preserved.** Each block may carry comments; they are attached to the
  following entry and re-emitted verbatim. (A format that eats your comments is not
  hand-editable.)

---

## 3. Section: `recording` (provenance & metadata)

Required, exactly one, first.

```rbsrc
rbsrc 1.0

recording {
  format_note "Graphy scene timeline recording"
  object "item1"                    # scene identifier, including any #N suffix
  hook "ProgramComponent.Program"   # "<OwnerClass>.<Field>"
  owner_class "Program"
  owner_id 1                        # Component.Identifier at record time (may not resolve on rebind)
  template "Template 1"
  source_asset "town_shop.scene"
  source_asset_sha256 "3f2a…"        # staleness detection (01-§4 rule 5)
  duration 0.000000000
  frame_quantum 0.016666667
  default_interp "Linear"
  created_by "Graphy 0.4.0"
  created_at "2026-09-13T13:40:00Z"
  modified_at "2026-09-13T14:02:11Z"
}
```

| Field | Required | Semantics |
| --- | --- | --- |
| `format_note` | no | free text, informational |
| `object` | yes | the `SceneObject.Identifier` the recording was made against, verbatim including `#N` (`01-…` §6.6) |
| `hook` | yes | `"<OwnerClass>.<Field>"` — one of the 21 slots (`01-…` §3.2). For `SceneObject.OnLoad` use `"SceneObject.OnLoad"`; for the generic carrier `"ProgramComponent.Program"` |
| `owner_class` | yes | the `ClassName` token of the owning component (`"Program"`, `"CollisionShape"`) or `"SceneObject"` |
| `owner_id` | yes | the component `Identifier` at record time. **Advisory**: used to seed the UI, never trusted for binding |
| `template` | no | resolved template name, or the placeholder string, or absent |
| `source_asset` | yes | filename the recording was read from |
| `source_asset_sha256` | no | hex digest; when present and mismatched → chip state `dirty` (`03-…` §3.1) |
| `duration` | yes | seconds; the end of the last key/marker/loop, cached for display |
| `frame_quantum` | yes | codegen tick size in seconds; default `1/60` |
| `wait_stride` | no | emit `Program.Wait(frame_quantum * wait_stride)` every `wait_stride`-th tick instead of every tick; default `1`. Required to reproduce scripts that guard the wait with a modulus (`rlsw.scl`'s `blackhole` does `if (map % 10) == 0 then Program.Wait(0.0001) end`). Getting this wrong changes not just timing but the object's per-frame time-slice cost (`05-…` §4.8). |
| `default_interp` | yes | applied to newly created keys |
| `created_by`, `created_at`, `modified_at` | no | informational only; **excluded from the determinism test** (§7) |

**Binding rule.** A rebind to a different object re-resolves the target by
`(owner_class, ordinal-of-that-class-within-the-object)`, never by `owner_id`
(`03-…` criterion B7). `owner_id` exists solely so the UI can highlight the original.

---

## 4. Section: `tracks`

Required. May be empty (`tracks {\n}`), meaning "no motion".

```rbsrc
tracks {
  # track <PropertyKind> ["<field>"] on component <ClassName> [ordinal]
  track Scaling on "Model" 0 {
    extrapolation "Clamp"
    quantization 0.000000000
    muted false
    key { t 0.000000000  v 0.010000000  interp "Linear"  in_slope 0.000000000  out_slope 0.000000000  broken false }
    key { t 0.700000000  v 7.010000000  interp "Linear"  in_slope 0.000000000  out_slope 0.000000000  broken false }
  }

  track FloatField "EmissionFactor" on "Model" 0 {
    extrapolation "Clamp"
    key { t 0.000000000  v 0.000000000  interp "EaseInOut" }
    key { t 0.300000000  v 1.000000000  interp "EaseInOut" }
  }

  track BoolField "Enabled" on "CollisionShape" 2 {
    extrapolation "Step"
    key { t 0.000000000  v true }
    key { t 1.250000000  v false }
  }

  track AssetName "Name" on "Model" 0 {
    extrapolation "Clamp"
    key { t 0.000000000  v "npc_elder" }
    key { t 2.000000000  v "npc_elder_hurt" }
  }
}
```

### 4.1 `PropertyKind` enumeration (normative — must match the C++ enum exactly)

| Token | Value type | Drives |
| --- | --- | --- |
| `PositionX` | Float | `SceneObject.Position.X` |
| `PositionY` | Float | `SceneObject.Position.Y` |
| `Depth` | Float | `SceneObject.Depth` |
| `Rotation` | Float (radians) | `SceneObject.Rotation` |
| `Scaling` | Float | `SceneObject.Scaling` |
| `Visibility` | Bool | `SceneObject.Hidden` (inverted: visible = `!Hidden`) |
| `BoolField` | Bool | a named `0/1` field on the target payload |
| `IntField` | Int | a named integer field |
| `FloatField` | Float | a named float field |
| `ParameterIndex` | Float | an element of a repeated float field (`ParticleEmitter.Parameter[n]`) |
| `AssetName` | String | a named `TextureName`/`Name`/`ItemName` field |
| `Visibility` extra | — | see above |

`ParameterIndex` is required because `ParticleEmitter.Parameter` is a **repeated** float
with up to 14 elements on one emitter (`01-…` §6.4); plain `FloatField` would silently
edit index 0. Serialised as:

```rbsrc
  track ParameterIndex "Parameter[7]" on "ParticleEmitter" 0 {
    key { t 0.000000000  v 0.350000000  interp "Linear" }
  }
```

### 4.2 `Interp` enumeration

`"Step"`, `"Linear"`, `"Cubic"`, `"EaseIn"`, `"EaseOut"`, `"EaseInOut"`.

### 4.3 `Extrapolation`

`"Clamp"`, `"Loop"`, `"PingPong"`. Note: this is the *track-level* curve extrapolation
(what happens outside `[first_key, last_key]`), which is **not** the same thing as a loop
region (§5). Keeping them as separate concepts is deliberate — Godot conflates them with
`Animation::LoopMode` (`scene/resources/animation.h:74`), and the confusion shows.

### 4.4 Key invariants (validated on load)

1. `keys` sorted strictly ascending by `t`; duplicate `t` (within `1e-9`) is a load error.
2. `t >= 0`.
3. For `BoolField`/`Visibility`, `v` is `true`/`false`.
4. For `IntField`, `v` has no fractional part.
5. `on "<ClassName>" <ordinal>` must resolve in the target object at bind time; if it does
   not, the recording loads but is marked unbindable, with the missing class reported.

## 5. Section: `state` — data-bounded loops

Optional, exactly one. Required to represent `blackhole`'s `exper` counter faithfully
(`03-…` §7.3).

```rbsrc
state {
  counter "exper" {
    init 700.000000000
    per_tick -1.000000000
    gate "> 0"                    # "<op> <number>" with op in { ">", ">=", "<", "<=", "==" }
    reset_on_activate true
  }
  flag "friendly" {
    init "neutral"                # string-valued
  }
}
```

Compiles to (matching the shipping `blackhole` bytecode's structure):

```lua
self.friendly = self.friendly or "neutral"
self.exper = self.exper or 700
…
if self.exper > 0 then
    self.exper = self.exper - 1
    …
end
```

A `counter` is the only construct in `.rbsrc` that is inherently
non-visual. It is explicitly typed and gated so the compiler cannot invent semantics.

---

## 6. Section: `markers`

Optional. Order = chronological.

```rbsrc
markers {
  marker {
    t 0.000000000
    fn "CreateShopItem"
    receiver "self"
    once true
    enabled true
    comment "sell a healing potion for 50"
    arg String "healingpotion"
    arg Number 50.000000000
  }

  marker {
    t 0.850000000
    fn "SoundLibrary.PlayEffect"
    receiver "global"
    once false
    arg String "portal_found"
  }

  marker {
    t 1.500000000
    fn "TransformController.ScaleTo"
    receiver "object:portal_effect_1"
    arg Number 0.001000000
    arg Number 0.300000000
  }
}
```

### 6.1 `receiver` grammar (normative)

| Form | Meaning | Compiles to |
| --- | --- | --- |
| `"self"` | the hook's own object | `Fn(self, …)` |
| `"global"` | a global library table (`SoundLibrary`, `Character`, `Scene`, `Math`) | `Lib.Fn(…)` |
| `"global:Character"` | explicit global when `fn` is unqualified | `Character.Fn(…)` |
| `"object:<identifier>"` | another scene object, resolved by identifier (including `#N`) | `Scene.Find("<id>"):…` or a captured local |
| `"component:<ClassName>[<ordinal>]"` | a component on the same object | resolved at codegen via the object's component table |
| `"captured:<name>"` | a local captured by an earlier marker | `<name>` |

`captured` exists because real scripts do exactly this:
`local effect = Scene.CreateObject(...)` then several calls on `effect`
(see `rlsw.scl`'s portal `OnCollide`). Without a capture mechanism the marker model
cannot express assignment, and the user would be forced back into text.

A marker that produces a capture declares it:

```rbsrc
  marker {
    t 0.010000000
    fn "Scene.CreateObject"
    receiver "global:Scene"
    capture "effect"
    arg String "portal_effect"
    arg String "portal_effect_"      # a string prefix; see §6.2
    arg SelfRef
  }
```

### 6.2 `arg` grammar (normative)

| Form | Meaning |
| --- | --- |
| `arg Number <float>` | numeric literal |
| `arg String "<text>"` | string literal, escaped per §2 |
| `arg Bool true\|false` | boolean |
| `arg SelfRef` | the hook's `self` |
| `arg ObjectRef "<identifier>"` | another object resolved from the hook's object's root |
| `arg ComponentRef "<ClassName>" <ordinal>` | a component on the same object |
| `arg Concat <n> …` | the following `n` args concatenated with `..` (for `"portal_effect_" .. self:identifier()`); the second element may be `SelfRefCall "identifier"` |

`Concat` exists because `rlsw.scl` contains
`Scene.CreateObject("portal_effect", "portal_effect_" .. self:identifier(), self)`.
Removing the ability to express it would make the format *strictly weaker* than the
shipping scripts, which violates §1.2 requirement 1.

---

## 7. Section: `loops`

Optional, exactly one, may contain zero or more regions.

```rbsrc
loops {
  region {
    begin 0.000000000
    end 0.700000000
    infinite true
    repeats 0
    label "grow"
  }
  region {
    begin 1.200000000
    end 1.800000000
    infinite false
    repeats 3
  }
}
```

Regions must not overlap; the reader rejects overlap as an error and truncates on write.

---

## 8. Section: `cache` (optional, non-authoritative)

A cache of the last compilation, so the UI can show "compiled / dirty" cheaply and so a
bytecode-only injection path needs no toolchain.

```rbsrc
cache {
  lua_source_sha256 "9c1b…"
  lua_bytecode_sha256 "40de…"
  compiler "luac5.1-vendored-2011"
  compiled_at "2026-09-13T14:02:11Z"
  bytecode_len 412
}
```

**Rules:** the cache is *advisory*. On load, the editor verifies
`sha256(emit_lua(model)) == lua_source_sha256`; on mismatch the cache is discarded and the
chip becomes `dirty`. The cache is **excluded from the determinism test** (below) and
should be omitted when writing a `.rbsrc` into version control. The bytecode itself is
never stored in `.rbsrc` — it belongs in the asset (`05-…`) — the cache holds only its
digest and length.

---

## 9. Determinism and the canonical writer

Canonical writer rules, all testable:

1. Sections emitted in the order `recording`, `tracks`, `state`, `markers`, `loops`,
   `cache` — with a blank line between.
2. Within `tracks`, order is **insertion order of the first track per `(kind, field,
   class, ordinal)`, then stable**. Never sorted by name (insertion order preserves
   authoring intent and makes diffs minimal).
3. `keys` sorted by `t` ascending.
4. Floats `%.9g`; integers plain; `bool` as `true`/`false`.
5. `created_at`/`modified_at`/`compiled_at` are excluded from the determinism comparison
   (they are timestamps). Everything else must match byte-for-byte.
6. Comment preservation: comments are re-emitted attached to their following entry.

Test: `serialize(parse(serialize(m))) == serialize(m)` for every fixture, and
`parse(serialize(m)) == m` modulo the timestamp fields.

---

## 10. CLI surface

Extend `tools/ruby_cli.cpp` (the project's native FileRift tool) so `.rbsrc` handling lives
next to the encoder that owns write-back fidelity:

```
rbsrc new      <out.rbsrc> --object ID --hook HOOK --class CLASS
rbsrc check    <file.rbsrc>                 # parse + validate invariants, exit 0/2
rbsrc fmt      <file.rbsrc> [--write]       # canonicalise (does NOT touch timestamps)
rbsrc show     <file.rbsrc>                 # human summary: track/marker/loop counts
rbsrc lua      <file.rbsrc> [-o out.lua]    # emit Lua text  (05-… §3)
rbsrc compile  <file.rbsrc> -o out.luac     # emit bytecode  (05-… §5)
rbsrc inject   <file.rbsrc> --into FILE.scl|FILE.scene --object ID --hook HOOK
                                            # surgical write-back (05-… §6)
rbsrc diff     <a.rbsrc> <b.rbsrc>          # semantic diff (ignores formatting)
rbsrc bind     <file.rbsrc> --to-scene F --object ID --hook HOOK
                                            # emit Lua for a different target, no mutation
```

`rbsrc diff` is the tool that makes the format's re-editability real: it compares the
*model*, not the text, so a reformat never shows as a change.

---

## 11. Versioning & migration

- The first line is `rbsrc <major>.<minor>`. Reader accepts any `major == 1`.
- **Minor bump** = additive, old readers must ignore unknown sections/keys (forward
  compatibility by skipping unknown blocks).
- **Major bump** = breaking; the reader refuses and points at the migration command.
- Every migration is a named, tested function:

```cpp
struct Migration { int from_major, from_minor; int to_major, to_minor;
                   QString name; bool (*apply)(QJsonObject&); };
// registry, ordered. `apply` operates on the neutral JSON model, not the text.
```

Migrations must be *reversible in principle*: the reader keeps unknown keys in a
`preserved` map and re-emits them on write, so opening and saving an old file with a new
Graphy cannot destroy fields the old version had.

Version roadmap:

| Version | Change |
| --- | --- |
| 1.0 | this document |
| 1.x (planned) | `state.flag` string values; `Concat` extended to n terms; per-key `comment` |
| 2.0 (speculative) | nested/compound tracks (a key whose value is a small struct), needed if particle emitters ever need multi-parameter envelopes |

---

## 12. Neutral model and interchange form

The canonical text is the interchange format. Internally the reader/writer operate on a
neutral model that is 1:1 with the text (so no information is lost in a round-trip) and
that maps directly onto `ruby::graph::timeline::Recording` (`03-…` §5.1):

```cpp
namespace ruby::graph::timeline {

struct RbsrcHeader {
    int major = 1, minor = 0;
    QString format_note;
    // §3 provenance, verbatim
    QString object, hook, owner_class, template_name, source_asset;
    int owner_id = 0;
    QString source_asset_sha256;
    Seconds duration = 0.0, frame_quantum = 1.0/60.0;
    Interp default_interp = Interp::Linear;
    QString created_by, created_at, modified_at;
    QHash<QString, QString> preserved;      // unknown-at-read keys, re-emitted
};

struct RbsrcDocument {
    RbsrcHeader header;
    std::vector<Track> tracks;              // 03-§5.1
    std::vector<LoopRegion> loops;
    std::vector<ActionMarker> markers;
    std::vector<StateEntry> state;
    std::optional<CacheBlock> cache;
};

RbsrcDocument parse(QStringView text);
QString        write(const RbsrcDocument&);   // canonical, deterministic (§7)
}
```

There is deliberately **no JSON form on disk**. JSON was considered and rejected: floats
serialise to 17 digits by default (defeating §1.2 requirement 3), key order is not
defined for maps (defeating determinism), and comments are impossible (defeating
hand-editability). A JSON view is fine as a *transient* for the `rbsrc diff` tool and for
the migration functions.

---

## 13. Worked fixtures (must ship in `tests/rbsrc/`)

### 11.1 `item1__program.rbsrc` — action marker only

```rbsrc
rbsrc 1.0

recording {
  object "item1"
  hook "ProgramComponent.Program"
  owner_class "Program"
  owner_id 1
  template ""
  source_asset "town_shop.scene"
  duration 0.000000000
  frame_quantum 0.016666667
  default_interp "Linear"
}

tracks {
}

state {
}

markers {
  marker {
    t 0.000000000
    fn "CreateShopItem"
    receiver "self"
    once true
    enabled true
    arg String "healingpotion"
    arg Number 50.000000000
  }
}

loops {
}
```

This must compile to `05-…` §4.7, i.e. the shipping `item1` body.

### 11.2 `blackhole__onload.rbsrc` — keyframe run + loop + data-bound counter

```rbsrc
rbsrc 1.0

recording {
  object "blackhole"
  hook "SceneObject.OnLoad"
  owner_class "SceneObject"
  owner_id 0
  source_asset "rlsw.scl"
  duration 0.007000000
  frame_quantum 0.000010000
  wait_stride 10
  default_interp "Linear"
}

tracks {
  track Scaling on "SceneObject" 0 {
    extrapolation "Clamp"
    key { t 0.000000000  v 0.010000000  interp "Linear" }
    key { t 0.007000000  v 7.010000000  interp "Linear" }
  }
}

state {
  counter "exper" {
    init 700.000000000
    per_tick -1.000000000
    gate "> 0"
  }
  flag "friendly" {
    init "neutral"
  }
}

markers {
}

loops {
  region {
    begin 0.000000000
    end 0.007000000
    infinite true
    label "grow"
  }
}
```

`frame_quantum = 1e-5` with `wait_stride = 10` gives 700 loop iterations and 70
`Program.Wait(0.0001)` calls — exactly the shipping cadence (`03-…` §7.2).

Round-trip test for this fixture is `03-…` criterion B3 and `05-…` §8.

---

## 14. Open questions

1. **Does the Ruby process materialise?** If a real Ruby producer is wanted, it evaluates
   the §2 grammar as Ruby method calls. That requires making the grammar valid Ruby
   (`recording do … end` rather than `recording { … }` with bare `key value` lines — bare
   `object "item1"` *is* valid Ruby, a method call with one string argument; `track
   Scaling on "Model" 0 { … }` is **not**). Options: (a) keep the format non-Ruby and drop
   the "Ruby" naming claim, (b) make the grammar strictly Ruby-valid, adding `do…end`
   blocks and no-arg-call keywords. **Recommendation: (a)**, and treat `.rbsrc` as an
   initialism for "Ruby-realm script source", since format consistency with FileRift's
   reader pattern is worth more than Ruby evaluability. Needs your call.
2. **`capture` scope.** Should captures be per-recording or per-loop-iteration (i.e. does
   a captured local persist across a loop region)? `rlsw.scl`'s portal script assigns
   before a loop, so per-recording is sufficient for observed data; per-iteration is
   strictly more expressive. Recommendation: per-recording, with a validator that rejects
   use-before-assignment.
3. **Should `duration` be derived or authoritative?** It is currently a cached value with
   a validator that it equals `max(t)` over keys/markers/loop ends. Deriving is simpler and
   removes a way to be wrong; caching helps the chip render without a full parse. Keep the
   cache + validator.
