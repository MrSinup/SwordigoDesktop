# 05 — Compiler Pipeline: `.rbsrc` → Lua text → bytecode → `Program{}`

Four stages. Each is independently testable, and stage 3 has a hard constraint that
would derail the project if discovered late (see §5.1).

```
  .rbsrc                    Lua text                 bytecode                asset
┌───────────┐  §4  ┌───────────────────┐  §5  ┌──────────────────┐  §6  ┌──────────────┐
│ Recording │─────▶│ Lua 5.1 source    │─────▶│ Lua 5.1 chunk    │─────▶│ Program{     │
│  (model)  │      │ (canonical emit)  │      │ (32-bit stream)  │      │  String,     │
└───────────┘      └───────────────────┘      └──────────────────┘      │  Bytes }     │
      ▲                                                                  └──────────────┘
      │  §7 verify: compile(text) == chunk ; guest test-load ; reparse
      └──────────────────────────────────────────────────────────────────────────
```

---

## 1. Where the code lives

| Stage | Component | Rationale |
| --- | --- | --- |
| `.rbsrc` parse/write | `ruby::graph::timeline` (`src/ruby/graph/graphy_timeline.cpp`) | it must be usable by the Qt editor directly, with no subprocess |
| Lua text emit | same, `EmitLua()` | pure function of the model — keeps it unit-testable |
| Lua text → chunk | `src/tools/lua51_codegen.{h,cpp}` + `cmake/components/lua51_codegen.cmake` | a **host** build of the vendored Lua 5.1 compiler plus a purpose-built dumper (§5) |
| write-back into `.scl`/`.scene` | `src/tools/filerift_writer.{h,cpp}`, driven by `tools/ruby_cli.cpp` | the surgical writer must live next to the existing byte-exact re-encoder |
| CLI | `ruby_cli rbsrc lua\|compile\|inject\|bind\|diff` | one binary owns both the format and its writer, so the round-trip harness is trivial |

**Decision:** there is exactly **one** emitter and it is C++. A Ruby re-implementation
would be a second source of truth for the same semantics, and the determinism tests in
`04-…` §9 exist specifically to prevent drift. If a Ruby facade is wanted later it must
shell out to `ruby_cli rbsrc` (`04-…` §14.1).

---

## 2. Stage 1/2 boundary — why text is not skipped

The pipeline could go straight from the model to a `Proto` tree and dump bytecode,
skipping text. It deliberately does not, for three reasons:

1. `Program.String` must be written back too. The engine stores both, and every observed
   hook has both. Emitting text is therefore not optional output, it is a required output.
2. The text is the human-auditable artifact. A user who does not trust a compiled blob can
   read the `String` in the asset.
3. Error messages. `luaL_loadbuffer` reports `file:line: message`, and the line numbers are
   only meaningful against the text we generated.

So: **text is authoritative, bytecode is derived from the text**, and `String`/`Bytes` are
guaranteed consistent by construction (stage 3 must consume stage 2's text verbatim).

---

## 3. Stage 2 inputs

`EmitLua(const RbsrcDocument&, const BindContext&)` needs:

```cpp
struct BindContext {
    QString hook;                 // "CollisionShape.OnCollide"
    QStringList implicit_args;    // {"self","target","normal","groundCollision"}
    int owner_component_id;       // re-resolved, not the recorded value (04-… §3)
    // accessor catalog, §3.2
    const AccessorTable* accessors;
    // how Position{X,Y}+Depth map onto the runtime Vector3 (open question, §9.1)
    AxisMap axis_map = AxisMap{X:0, Y:1, Depth:2};
};
```

### 3.1 Implicit-argument preamble

Derived from the hook (`03-…` §3.2). Emitted exactly as observed:

```lua
local self = ...;                                              -- OnLoad, OnPress, OnActivate, …
local self, target = ...;                                      -- OnCollisionEnd, OnKill, OnHurt
local self, target, normal, groundCollision = ...;             -- OnCollide   (hiro.scl, verbatim)
```

The preamble is part of the **generated text**, so a `.rbsrc` never stores it. That is what
makes a `.rbsrc` portable between hook kinds: change the hook, and the preamble changes.

### 3.2 The accessor catalog — a hard prerequisite

A transform track can be emitted directly, because the object API is observed:
`self:setPosition(...)`, `self:setScaling(...)`, `self:position()`, `self:scaling()`,
`self:destroy()`, `self:identifier()`.

A **field** track cannot, unless we know the accessor for that specific payload field.
Observed forms are inconsistent:

| Observed | Example | Meaning |
| --- | --- | --- |
| instance method | `self:setScaling(v)` | on the object |
| static per-class | `PhysicsObject.SetEnabled(obj, bool)` | takes the target object |
| static per-class with id | `CollisionShape.IsEnabled(self, 107)` | takes object **and** component id |
| table function | `TransformController.ScaleTo(effect, 1, 0.2)` | takes object |
| ad-hoc Lua state | `self.friendly = …`, `self.exper = …` | plain Lua table fields, **not** proto fields |

Therefore `graphy_timeline` consumes a generated **accessor table**, and **a field track may
only be created if an accessor exists**. Refusing to create an unemittable track is the
correct behaviour: emitting plausible-but-wrong Lua into a shipping asset is the worst
possible outcome.

Two mechanical sources for the catalog:

1. **`Mini` namespace** (the SRE's own bindings): scan `src/sre/sre12/sre_mini_api.c` for
   `g_lua_setfield(L, -2, "<Name>")`. This yields **395 unique names, 62 of them setters**:

   ```
   SetAirJumpUsed SetAlignment SetAlpha SetAlwaysActive SetArmorModel SetBackgroundAlpha
   SetBackgroundResource SetBoneScale SetButtonConfined SetCinematicMode SetClickable
   SetCoinLimit SetCoins SetControlsDisabled SetControlsHidden SetCurrentHealth
   SetCurrentMana SetDefaultPlayerModel SetDimensions SetExp SetFollow SetHealth SetHidden
   SetHiddenAll SetImmunityTime SetItemIdentifier SetJumpHeight SetLevel SetLevelAttributes
   SetMana SetModelName SetMovementFacingLock SetObjectSpeed SetOffset
   SetOverlayBackgroundAlpha SetOverlayBackgroundColor SetOverlayCornerRadius
   SetOverlayHidden SetOverlayMovable SetOverlayPinchable SetOverlayPosition SetPadding
   SetPerspectiveProjection SetPosition SetPositionOffset SetRunSpeed SetScaling SetSpeed
   SetStunTime SetText SetTextColor SetTextFont SetTextScale SetTouchHandlingEnabled
   SetTouchRadius SetTrinketColor SetUpVector SetValue SetWalkSpeed SetWeaponColor
   SetWeaponColorForTrinket SetZoom
   ```

2. **The engine's own class tables** (`PhysicsObject`, `Entity`, `Health`,
   `TransformController`, `CollisionShape`, `Scene`, `Character`, `SoundLibrary`, …).
   These are *not* in `sre_mini_api.c`; they are registered by the game. `lua_api_dump.txt`
   in the repo root is **empty (0 lines)**, so the dump has never actually been produced.
   The reliable extraction is a **live console reflection pass** (`06-…` §4.4):

   ```lua
   for k, v in pairs(_G) do
     if type(v) == "table" then
       local names = {}
       for fk, fv in pairs(v) do
         if type(fv) == "function" then names[#names+1] = fk end
       end
       print(k .. ": " .. table.concat(names, ","))
     end
   end
   ```

   Run once per engine ABI inside a running game, cache the output as
   `assets/schema/accessors_1.4.13.json`. This is a 2-minute job that unblocks the whole
   field-track feature, and it produces exactly the artifact `lua_api_dump.txt` was meant
   to be.

   `tools/generate_accessors.py` then merges (1) and (2) into:

   ```json
   { "version": "1.4.13",
     "fields": {
       "CollisionShapeComponent.Enabled":  { "form": "static_with_id",
                                             "table": "CollisionShape",
                                             "fn": "SetEnabled",
                                             "arg_component_id": true },
       "PhysicsObjectComponent.PhysicsEnabled": { "form": "static",
                                              "table": "PhysicsObject",
                                              "fn": "SetEnabled" },
       "HealthComponent.MaxHealth":         { "form": "static",
                                              "table": "Health", "fn": "SetMaxHealth" }
     },
     "unmapped": ["ParticleEmitterComponent.Parameter", "…"] }
   ```

   `"unmapped"` is the honest list of fields the timeline must not offer as tracks.

---

## 4. Stage 2 — keyframe emission

### 4.1 Time partition

```
emit_document(rec, ctx):
  # ---- 1. preamble
  emit "local " .. join(ctx.implicit_args) .. " = ...;"

  # ---- 2. state declarations (04-… §5), emitted before any body
  for each StateEntry s:
      numeric counter:  emit "self." .. s.name .. " = self." .. s.name .. " or " .. s.init .. ";"
      string  flag:     emit "self." .. s.name .. " = self." .. s.name .. " or \"" .. s.init .. "\";"

  # ---- 3. seed the transform so frame 0 is correct even if the loop is skipped
  for each transform track t with a key at t <= 0:
      emit setter(t.kind, value_at(t, 0))

  # ---- 4. partition on times that MUST be exact
  cuts = { 0 } ∪ { duration }
       ∪ { k.time             for each non-muted track, each key }
       ∪ { m.time             for each enabled marker }
       ∪ { r.begin, r.end     for each loop region }
  P = sorted(ascent(cuts))
  P = merge_runs_closer_than(P, min_gap = rec.frame_quantum)   # avoid zero-length segments

  # ---- 5. emit
  covered = set()
  for each loop region r in ascending order:
      emit_loop(r, segments_of(P, r.begin, r.end), rec, ctx)
      covered += [r.begin, r.end]
  for each uncovered segment s in segments_of(P, everything) \ covered:
      emit_segment(s, rec, ctx)
```

`merge_runs_closer_than` is essential: two keyframes 1e-9 apart would otherwise produce a
zero-iteration loop and a division by zero in the interpolation parameter.

### 4.2 Segment emission (the tick loop)

For a segment `[a, b)`:

```
q        = rec.frame_quantum
stride   = rec.wait_stride
frames   = max(1, round((b - a) / q))
```

Emitted Lua:

```lua
for _i = 0, <frames - 1> do
    local _u = <a>                      -- if frames == 1
    -- else: local _u = _i / (<frames - 1>)
    <assignments for every track active on [a,b)>
    <markers whose time falls in [a,b), in time order>
    <wait, see below>
end
```

Wait emission, three cases:

```lua
Program.Wait(<q>)                                   -- stride == 1
if (_i % <stride>) == 0 then Program.Wait(<q*stride>) end   -- stride  > 1  (blackhole)
<nothing>                                           -- stride == 0 (single-shot segment)
```

The `stride > 1` form is deliberately byte-shaped like the shipping `blackhole` loop
(`if (map % 10) == 0 then Program.Wait(0.0001) end`) so the decompiler in §8 can recognise
it. `frame_quantum` for such a recording is the *per-tick* quantum (`1e-5` in `blackhole`),
and `frame_quantum * wait_stride` is the actual wait (`1e-4`).

### 4.3 Interpolation expressions

Given the bracketing keys `k0` (last key with `t <= a`) and `k1` (first key with `t >= b`),
with `dt = k1.time - k0.time`:

| `k0.interp` | emitted expression |
| --- | --- |
| `Step` | `<v0>` |
| `Linear` | `<v0> + (<v1> - <v0>) * _u` |
| `EaseIn` | `<v0> + (<v1> - <v0>) * (_u * _u)` |
| `EaseOut` | `<v0> + (<v1> - <v0>) * (1 - (1 - _u) * (1 - _u))` |
| `EaseInOut` | `_e = (_u < 0.5) and (2 * _u * _u) or (1 - 2 * (1 - _u) * (1 - _u))` then lerp |
| `Cubic` | Hermite: `h00=2u³-3u²+1, h10=u³-2u²+u, h01=-2u³+3u², h11=u³-u²`, `v = h00*v0 + h10*dt*out_slope0 + h01*v1 + h11*dt*in_slope1` |

Note `_e` and `h*` are hoisted to a single assignment per segment where possible, so a
track set of 8 active tracks costs 8 lerps per tick, not 8 closures.

### 4.4 Property setters

| `PropertyKind` | emitted |
| --- | --- |
| `PositionX` / `PositionY` / `Depth` | three tracks compose one call, emitted once per segment: `self:setPosition(Vector3.New(<x>, <y>, <depth>))` (axis order per `BindContext::axis_map`, §9.1) |
| `Rotation` | `self:setRotation(<v>)` (name assumed; §9.2) |
| `Scaling` | `self:setScaling(<v>)` |
| `Visibility` | `self:setHidden(<not v>)` — `SetHidden` is confirmed present (§3.2) |
| `BoolField` / `IntField` / `FloatField` | per accessor catalog: `Table.Fn(self[, id], <v>)` |
| `ParameterIndex` | same, with the index folded into the emitted field name if the accessor takes one |
| `AssetName` | accessor with a string argument (e.g. `Model.SetName(self, "<v>")` if registered) |

If a transform track composes with others, the *composed* setter call is emitted once per
segment and the individual tracks do not each emit their own call. This is why the
partition is global rather than per-track: it is the same reason a film projector has one
shutter, not one per reel.

### 4.5 Loop regions

```lua
while true do                                     -- infinite
    <segments of the region, in order>
end

for _rep = 1, <repeats> do                        -- finite
    <segments of the region, in order>
end
```

A finite region whose end is followed by further segments needs a `break`-free structure,
which `for` gives naturally. An infinite region terminates the function body — anything
after it is dead, and the emitter warns (`RBSRC_W003`).

### 4.6 Marker emission

```lua
<receiver_expr>.<fn>(<args>)                      -- no result
<capture> = <receiver_expr>.<fn>(<args>)          -- capture form
```

with

| `receiver` | `receiver_expr` |
| --- | --- |
| `"self"` | `self` |
| `"global"` | `<fn>` split on `.` → `Lib.Fn` |
| `"global:Character"` | `Character` |
| `"object:<id>"` | `Scene.Find("<id>")` — hoisted to a local at the top of the function on first use |
| `"component:<Class>[<n>]"` | resolved via the object's component table at codegen time |
| `"captured:<name>"` | `<name>` |

`once = true` inside a loop emits a **guard local hoisted above the loop**:

```lua
local _did_marker_3 = false
…
    if not _did_marker_3 then
        _did_marker_3 = true
        CreateShopItem(self, "healingpotion", 50);
    end
```

This is exactly the semantics `ProgramComponent.ExecuteOnce : 1` provides
(`01-…` §5.7), made explicit because a marker can live inside a loop.

---

## 4.7 Worked example A — `item1` (action marker, no motion)

Input: `04-…` §13.1. No tracks, no loops, one marker at `t = 0`, `duration = 0`.

Partition: `cuts = {0, 0} → P = {0}`; the single segment `[0,0)` has `frames = 1`,
`stride = 1`, but there is nothing to interpolate, so the segment collapses.

Emitted:

```lua
local self = ...;

CreateShopItem(self, "healingpotion", 50);
```

Shipping asset (`town_shop.scene`, `ProgramComponent.String`), verbatim:

```
\nlocal self = ...;\r\n\r\nCreateShopItem(self, "healingpotion", 50);\n
```

The difference is the leading blank line and CRLF line endings, which come from the
authoring tool of 2013 and are **not reproduced by the emitter**. Consequence: a
recompiled `item1` is semantically identical but **not byte-identical** to the shipped
`String`. `03-…` criterion B2 is therefore stated as "identical after normalising leading
whitespace and line endings", and a separate `--preserve-line-endings` flag exists if
byte-identity is required for a fixture.

On the bytecode side, byte-identity is *impossible in principle* anyway: the chunk embeds
the source string, so different text ⇒ different chunk. This is worth stating plainly so
nobody chases it.

---

## 4.8 Worked example B — `blackhole` (scrub-to-loop inversion)

Input: `04-…` §13.2.

```
duration 0.007   frame_quantum 1e-5   wait_stride 10
track Scaling: (0.000, 0.01) → (0.007, 7.01), Linear
loop region [0.000, 0.007] infinite
state counter exper: init 700, per_tick -1, gate "> 0"
state flag    friendly: init "neutral"
```

Emission:

```lua
local self = ...;

self.friendly = self.friendly or "neutral";
self.exper = self.exper or 700;
self:setScaling(0.010000);

while true do
    for _i = 0, 699 do
        local _u = _i / 699;
        if self.exper > 0 then
            self.exper = self.exper - 1;
            self:setScaling(0.010000 + (7.010000 - 0.010000) * _u);
        end
        if (_i % 10) == 0 then
            Program.Wait(0.0001);
        end
    end
end
```

Compare with the shipping original (`rlsw.scl`, `blackhole` `Object.OnLoad`, verbatim):

```lua
local self = ...;
self.friendly = self.friendly or "neutral"
self.exper = self.exper or 700
…
local scale = self:scaling()
self:setScaling(0.01)

for map = 1, (1e+200^2) do
    if (map % 10) == 0 then
        Program.Wait(0.0001)
    end
    if self.exper > 0 then
        self.exper = self.exper - 1
        self:setScaling(self:scaling() + (1/100))
    end
end
```

Trajectory equivalence:

| | shipping | compiled |
| --- | --- | --- |
| ticks | unbounded (`1e+200^2`) | unbounded (`while true`) |
| waits | `700/10 = 70` × `1e-4` = **0.007 s** | `700/10 = 70` × `1e-4` = **0.007 s** |
| scale after tick *n* | `0.01 + 0.01n` | `0.01 + 7.0 * n/699` |
| final scale | `0.01 + 700·0.01 = 7.01` | `0.01 + 7.0 = 7.01` |
| **per-tick delta** | constant `+0.01` | `+0.010014…` (varies by one part in 700) |

The last row is the honest limitation: the original is a *discrete accumulator*
(`self:scaling() + 1/100`), the compiled form is a *continuous interpolation*. They agree at
every keyframe endpoint and differ by at most `0.01/699 ≈ 1.4e-5` mid-segment. Two options:

- **(A) Interpolated form** (above). Clean, matches every other track, error ≤ 1.4e-5.
- **(B) Accumulator form**: detect that a `Scaling` track is exactly a uniform delta over
  its span with no easing and emit `self:setScaling(self:scaling() + (0.010000))` — bit-exact
  with the original at every tick.

**Recommendation: (B) as an emitter optimisation with (A) as the general case**, because
`blackhole` is a *fixture* and behavioural equivalence should be exact there. The detector
is: `keys[n+1].v - keys[n].v` constant across all `n`, all `interp == Linear`, and the
track's `state` counter (if any) has `per_tick != 0` whose magnitude matches. Record the
choice in `.rbsrc` as `emit_mode "interpolate" | "accumulate"` so a round-trip is stable
(still need to add this key; tracked in §9.4).

---

## 5. Stage 3 — Lua text → Lua 5.1 chunk

### 5.1 The 32-bit dump stream (critical finding)

The game runs ARM64, but **the shipped chunks are 32-bit-duMPed**. Byte-verified by
decoding `town_shop.scene`'s `item1` `Bytes` field (237 bytes total):

```
header[0..12] = 1b 4c 75 61 | 51 | 00 | 01 | 04 | 04 | 04 | 08 | 00
                 \x1bLua     5.1  fmt  LE  s/int s/size_t s/Instr s/Number integral
                                              ^^^^^^^^ = 4   <-- NOT 8
```

| Evidence | Bytes | Conclusion |
| --- | --- | --- |
| header | `1b 4c 75 61 51 00 01 04 04 04 08 00` | `sizeof(size_t) == 4`, `sizeof(lua_Number) == 8`, little-endian |
| first `Proto` field (source string size) | `40 00 00 00` = 64 | string sizes are **4-byte LE**, and 64 = `strlen(source)+1` for the 63-char source |
| source string | `local self = ...;\r\n\r\nCreateShopItem(self, "healingpotion", 50);` | CRLF preserved inside the chunk |
| constant `CreateShopItem` | `04 | 0f 00 00 00 | "CreateShopItem" 00` | `LUA_TSTRING`=4, then **4-byte** length 15 |
| constant `healingpotion` | `04 | 0e 00 00 00 | "healingpotion" 00` | same, length 14 |
| constant `50.0` | `03 | 00 00 00 00 00 00 49 40` | `LUA_TNUMBER`=3 + 8-byte LE IEEE-754 double |
| debug tail | `05 00 00 00 "self" 00 | 01 00 00 00 | 06 00 00 00 | 00 00 00 00` | locvar `self` [1,6) + trailing `sizeupvalues` word — debug info **is** present |

**Consequence:** a stock host `luac5.1` built on x86-64 emits `sizeof(size_t) == 8` and a
stream whose length prefixes are 8 bytes wide. Such a chunk is **not merely header-incompatible,
it is structurally unreadable** by the engine's loader — patching the header alone does not
help, because every string length and every array size in the dump uses that width.
`lundump.c` in stock Lua 5.1 validates the size fields (`"incompatible precompiled chunk"`).

So: **we cannot use an external `luac` for the chunk, and we cannot use a 64-bit host build
of `ldump.c` either.** (This invalidates the brief's suggestion of "compiled with `luac5.1`
(or an embedded Lua 5.1 compiler)" in its naive form; the embedded compiler is right, the
stock dumper is not usable.)

### 5.2 The solution: `lua51_codegen`

Build a host-side library from the **vendored** sources at `src/sre/base/lua/src` — which
are complete, including `lparser.c`, `lcode.c`, `lstring.c`, `ltable.c`, and `ldump.c`
(verified present) — and then:

1. **Use the vendored parser/compiler as-is** for source → `Proto`:
   `luaL_loadbuffer(L, text, len, chunkname)`. This guarantees identical syntax and
   identical instruction selection to the engine's own Lua.
2. **Replace the dumper.** Write `src/tools/lua51_dump32.cpp`: a port of `ldump.c`
   parameterised on integer width:

   ```cpp
   namespace ruby::lua51 {
   struct DumpConfig {
       int  int_width       = 4;   // sizeof(int)
       int  size_t_width    = 4;   // <-- the whole point
       int  instruction_width = 4;
       int  number_width    = 8;
       bool little_endian   = true;
       bool integral_number = false;   // 0x00: lua_Number is floating point
       bool strip_debug     = false;   // keep, so runtime errors have line numbers
   };
   std::string dump(const Proto* p, DumpConfig cfg = {});
   // inverse, for self-verification (never shipped to the game):
   bool load(const std::string& chunk, Proto* out);
   }
   ```

   Roughly 250 lines: `DumpHeader`, `DumpFunction` (`source`, `linedefined`,
   `lastlinedefined`, `nups`, `numparams`, `is_vararg`, `maxstacksize`, `code`,
   `constants`, `protos`, `debug`), `DumpString`, `DumpConstants`, `DumpDebug`,
   `DumpCode`, `DumpAbsLineInfo`, all with a width-parameterised `put_int`.
3. **Keep debug info** (`strip_debug = false`). The shipping chunk has it, and it is what
   makes a Lua error inside a hot-patched hook report a useful line number. The cost is a
   larger chunk (237 bytes for `item1`), which is irrelevant.
4. **Never use the engine's `LUAC_VERSION`/`LUAC_FORMAT` constants implicitly** — read them
   from a checked-in reference chunk (`testdata/ref_chunk.luac` extracted from `item1`) and
   assert we reproduce the header byte-for-byte.

### 5.3 If a 32-bit host build turns out to be easier

An `-m32` build of the vendored `ldump.c` would also produce a correct stream, at the cost
of requiring i386 multilib on every developer machine and a second toolchain in CI.
`lua51_dump32.cpp` is preferred: it has no external dependency, it is ~250 lines, and it is
directly unit-testable (`dump(load(x)) == x`).

### 5.4 Ground truth: verify in the guest

The only proof that a chunk loads is the engine loading it. The SRE already provides the
path (`06-…`):

```
host -> g_lua_console_buf = <probe Lua that loadstring()s the chunk hex>
     -> g_lua_console_pending = 1
guest -> runs it on the live lua_State, writes g_lua_console_result
```

Probe script (submitted as one console command):

```lua
local hex = "<chunk as hex>"
local s = ""
for i = 1, #hex, 2 do s = s .. string.char(tonumber(hex:sub(i, i+1), 16)) end
local f, err = loadstring(s, "@graphy_probe")
if not f then return "LOAD_FAIL: " .. tostring(err) end
local ok, e = pcall(f)
return ok and "LOAD_OK" or ("RUN_ERR: " .. tostring(e))
```

`05-…` §7 wires this into CI as a *manual/optional* gate (it needs a running game), and the
`lua51_dump32` self-round-trip as the *always-on* gate.

---

## 6. Stage 4 — surgical write-back into `Program{}`

The writer is deliberately **not** a re-serialiser (`01-…` §8 rule 2). It is a byte-range
editor:

```cpp
namespace ruby::filerift {

struct ProgramBlock {
    size_t string_begin, string_end;   // byte offsets of the heredoc body in the file
    size_t bytes_begin,  bytes_end;    // byte offsets of the Bytes literal payload
    QString owner_path;                // "Object[item1].Component[1].ProgramComponent.Program"
    QString field_name;                // "Program" | "OnCollide" | "OnLoad" | …
};

// Locate without parsing the whole model: brace-stack walk + field scan.
bool find_program_block(const std::string& src, const QString& object_ident,
                        const QString& hook, ProgramBlock& out, QString& error);

// Rewrite exactly two ranges, leaving every other byte untouched.
std::string write_program(const std::string& src, const ProgramBlock& block,
                          const std::string& lua_source, const std::string& chunk,
                          bool preserve_line_endings);
}
```

### 6.1 Locating the block

1. Walk the file with a brace stack, recording byte offsets for every block, exactly as
   `01-…` §3.1 requires (nesting is never regexed).
2. Find the top-level `Object` whose direct `Identifier` scalar equals the requested one
   (including any `#N` suffix, `01-…` §6.6).
3. Inside it, find the owner:
   - `hook == "SceneObject.OnLoad"` → the `OnLoad{` block that is a **direct child** of the
     `Object`.
   - otherwise, find the `Component` whose `ClassName` matches
     `BindContext.owner_class` at the requested ordinal, and inside it the `Program`-valued
     block named `hook`'s field.
4. Inside that block, record the `String : $ … $end` body range and the `Bytes : '…'` literal
   range. If either is absent, create it (appending `Bytes` after `String`, or vice versa)
   and record the insertion point instead of a replacement range.

### 6.2 Escaping / formatting rules

- **`String`**: emit `String : $` then the Lua text then `$end`, at the block's existing
  indentation for the `$end` line. Line endings: preserve the file's dominant convention
  (both samples are CRLF inside heredocs); `--preserve-line-endings` forces CRLF.
- **`Bytes`**: single quotes, printable ASCII (0x20–0x7E except `'` and `\`) raw, everything
  else as `\xHH`, `\\` and `\'` escaped. This matches the current decoder exactly
  (`01-…` §6.3), and is verified by the round-trip harness.
- **No reflow, no re-indent** of anything else in the file.

### 6.3 Verification (the harness that must gate every write)

```
1. decode -> d0                                   (bin/ruby_cli -d)
2. find_program_block(d0, obj, hook)              -> must succeed
3. write_program(d0, ..., lua, chunk)             -> d1
4. assert d1 differs from d0 ONLY inside the two recorded ranges
5. recode(d1) -> binary; decode(binary) -> d2
6. assert d2 == d1                                (whole-file fixpoint, 01-§8)
7. assert parse_program(d1, obj, hook).lua == lua
8. assert bytes32_sha(d2, obj, hook) == sha(chunk)
```

`tests/filerift_smoke.cpp` + `bin/filerift_smoke` already exist; this harness extends them.

---

## 7. Determinism, caching, errors

### 7.1 Determinism

`EmitLua` must be a pure function of `(RbsrcDocument, BindContext)`. No timestamps, no
hash-map iteration order in output, no locale-dependent number formatting (**use `%.9g`
with the C locale forced**, and never `QString::number` which respects the locale for
grouping). Emitted float literals must be parseable by Lua 5.1's `strtod`.

Test: emit twice, in two processes, with `LC_ALL=de_DE.UTF-8` in one → identical bytes.

### 7.2 Cache (`04-…` §8)

`cache.lua_source_sha256` lets the UI show `dirty` without compiling. On a cache hit,
`Bytes` can be pushed to the live game without touching the toolchain (`06-…`).

### 7.3 Diagnostics

| Code | Meaning | Severity |
| --- | --- | --- |
| `RBSRC_E001` | duplicate key time within a track | error |
| `RBSRC_E002` | overlapping loop regions | error |
| `RBSRC_E003` | marker references an unknown accessor/function | error |
| `RBSRC_E004` | `once` marker inside a `parameter`-indexed track region (order undefined) | error |
| `RBSRC_W001` | field track whose accessor is absent from the catalog (`"unmapped"`) | warn at author time, **error at compile** |
| `RBSRC_W002` | zero-length segment merged | info |
| `RBSRC_W003` | dead code after an infinite loop region | warn |
| `RBSRC_W004` | `AxisMap` is the unverified default (§9.1) | warn once |
| `RBSRC_W005` | emitted text differs from the existing `String` only in line endings | info |

---

## 8. Stage 1 (reverse) — Lua → `.rbsrc`, i.e. the decompiler

Needed for `03-…` criterion B3 and for "open any existing hook as a recording". This is
recogniser-based, not a general decompiler, and it only claims to succeed on **emitter-shaped**
output (which is the point: our own output is stable under round-trip).

Recognised forms:

| Pattern in `String` | Recognised as |
| --- | --- |
| `for <v> = 1, <big>` / `while true do` + `Program.Wait(<q>)` | infinite loop region, `q` |
| `if (x % N) == 0 then Program.Wait(<q*N>) end` | `wait_stride = N`, `frame_quantum = q` |
| `self:<setter>(<expr>)` with `expr` linear in `_u` (or `i/N`) | a `Linear` key pair |
| `self.<name> = self.<name> or <lit>` | `state` flag / counter `init` |
| `if self.<name> > 0 then self.<name> = self.<name> - 1 then …` | `state` counter `per_tick = -1`, `gate = "> 0"` |
| `self:setScaling(self:scaling() + (<const>))` | uniform-delta `Scaling` track, `emit_mode "accumulate"` |
| bare `Fn(<args>)` / `Lib.Fn(<args>)` at statement level | `ActionMarker` |
| `local <n> = Fn(<args>)` | `ActionMarker` with `capture` |

On any unrecognised statement, the decompiler **fails loudly** with the line number and
preserves the original text in the recording as an `opaque` block, which the emitter
re-emits verbatim. That makes the decompiler *total* (it never loses code) while being
honest about what it understood. `opaque` needs a `.rbsrc` key — see §9.4.

---

## 9. Open questions

1. **Axis mapping `Position{X,Y}` + `Depth` → runtime `Vector3`.** `Position` has only X/Y
   and `Depth` is separate (`01-…` §5.3), but the runtime API is
   `self:position()` / `self:setPosition(Vector3)` and scripts do
   `self:position() + Vector3.New(0, 68, -95)` (a Depth-looking third component).
   The obvious mapping is `Vector3.New(X, Y, Depth)` but it is **not proven**.
   **Resolution is cheap and must be done first:** via the live console (`06-…`),
   for an object with known `Position{X: 919.5, Y: 394.5}` and `Depth: -29.9`, print
   `Scene.Find("elder"):position()`. Whatever comes out is the mapping. Until then the
   compiler emits `RBSRC_W004` and uses `(X, Y, Depth)`.
2. **Is the rotation setter `self:setRotation`?** `Rotation` is present in the asset
   (`SceneObject.Rotation(6)`) but no observed script sets it. Candidates:
   `setRotation`, `setAngle`, `TransformController.RotateTo`. Resolve with the same
   reflection pass as §3.2.
3. **Does the engine's loader actually validate the header sizes?** The shipping chunk
   claims `sizeof(size_t) == 4` while the game is ARM64. Either the engine's Lua was built
   32-bit (impossible for a 64-bit process unless `lua_State` is internal-only — actually
   entirely possible if its Lua uses a 32-bit `size_t`-sized allocator typedef, which some
   mobile forks do), or `lundump.c` was patched to relax the checks. **This matters**: if
   the loader is strict, `lua51_dump32` is mandatory; if it is relaxed, a 64-bit dump might
   work but we still must not emit one, because the *interpreter* reads array sizes with its
   own width and a mismatched stream would corrupt memory. Either way emit 32-bit. Confirming
   which case it is takes one experiment: hand the live console a deliberately 64-bit-dumped
   chunk and read the error string. Do that before writing `lua51_dump32` so the design note
   records the answer.
4. **Two missing `.rbsrc` keys** discovered while writing this document: `emit_mode`
   (`"interpolate" | "accumulate"`, §4.8) and `opaque` blocks (§8). Both must be added to
   `04-…` §3/§4 before implementation; they are the difference between a round-trip that is
   exact and one that is merely close.
5. **`Vector3.New` argument count.** `Vector3.New(0, 20, 0)` (3 args) is observed; a 2-arg
   form may exist. If the runtime position is 3-component, always emit 3.
