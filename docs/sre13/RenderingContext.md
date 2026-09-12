# RenderingContext

## Summary

`Caver::RenderingContext` is the engine's **OpenGL ES state wrapper**. One
context exists per GL thread/rendering pass; a process-global
`qword_651860` holds the "current" context (get/set via the static
`CurrentContext()`/`SetCurrentContext()`). It bundles the GL viewport,
clear color/depth, blend state, color/alpha, the current matrix, the bound
program, and the embedded `BasicRenderingPrograms` registry. The engine is a
single-context game (the ctor immediately issues `glBlendFunc(GL_ONE,
GL_ONE_MINUS_SRC_ALPHA)`), so the context is mostly a tidy state cache.

Header: `src/sre/sre13/caver/RenderingContext.h`.

## Struct layout

Verified from the ARM64 v1.4.13 ctor (`0x5151EC`). 64-bit ABI. The header's
field names (`programsBegin/programsEnd`, `viewport`, `clearColor[4]`,
`blendingEnabled`, ...) are **unverified guesses** — the ctor only reveals the
offsets below; field-to-slot mapping for the GL-state setters
(`SetViewport`/`SetClearColor`/`SetMatrix`/...) was not cross-checked in this
pass and should be treated as inferred.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 4 | `RenderingAPI` (enum) | `api` | Ctor arg stored as a DWORD. |
| 0x04 | 4 | — | (unknown) | Not written by ctor. |
| 0x08 | 8 | `void*` | (list head) | `= &this[0x10]` — self-referential; part of a registry/list the context is linked into (e.g. `BasicRenderingPrograms` bookkeeping). |
| 0x10 | 8 | `void*` | — | Zeroed. |
| 0x18 | 8 | `void*` | — | Zeroed. |
| 0x20 | 16 | — | (matrix?/rect) | Zeroed OWORD pair (0x20, 0x30). Likely the current matrix or viewport storage; **inferred**. |
| 0x30 | 16 | — | — | Zeroed. |
| 0x40 | 8 | `double` | `= 1.0, 1.0` | `FMOV V0.2S, #1.0` → two floats `{1.0f, 1.0f}` at 0x40. Possibly default color. |
| 0x48 | 4 | — | (unknown) | Zeroed DWORD. |
| 0x4C | 1 | `bool` | — | Zeroed. |
| 0x50 | 4 | `int` | `= 1` | Enabled flag (blending? texturing?). |
| 0x54 | 8 | `void*` | — | Zeroed. |
| 0x5C | 1 | `bool` | — | Zeroed. |
| 0x5D | 2 | `uint16` | `= 1` | WORD `1` at 0x5D–0x5E. |
| 0x5F | 4 | `int` | `= -1` | Program handle sentinel (`-1` = none bound?). |
| 0x64 | 4 | `float` | `= 1.0f` | Alpha? |
| 0x68 | 1 | `bool` | — | Zeroed. |
| 0x6C–0xEB | — | — | (unknown) | Unmapped in this pass. |
| 0xEC | 4 | `int` | — | Zeroed. |
| 0x118 | 4 | `int` | — | Zeroed. |
| 0x120 | 8 | `void*` | — | Zeroed. |

The `BasicRenderingPrograms::AddToContext(this, api)` call in the ctor means the
program registry is embedded in (or attached to) the context; its exact offset
wasn't isolated (it may share the 0x20–0x30 region or live later in the object).

## Exported functions

### RenderingContext::CurrentContext() / SetCurrentContext(RenderingContext*)
- Mangled: `_ZN5Caver16RenderingContext14CurrentContextEv` (0x5151D4) /
  `_ZN5Caver16RenderingContext17SetCurrentContextEPS0_` (0x5151E0).
- Behavior: read/write the process-global `qword_651860`. This is the hook the
  game's draw code uses to find its GL state.
- Confidence: **verified**.

### RenderingContext::RenderingContext(RenderingAPI)
- Mangled: `_ZN5Caver16RenderingContextC2ENS_12RenderingAPIE` (0x5151EC).
- Behavior: initializes the fields above, registers the basic programs, and
  issues the default blend func.
- Confidence: **verified**.

### GL-state setters / draw helpers
`SetViewport(Rectangle const&)` (0x515360), `SetClearColor(Color const&)`
(0x515384), `SetClearDepth(float)` (0x5153C0), `Clear(bool,bool,bool)`
(0x5153C4), `SetProjectionMatrix(Matrix4 const&)` (0x5153EC),
`SetMatrix(Matrix4 const&)` (0x5154B0), `SetIdentityMatrix()` (0x51554C),
`SetColor`/`SetAlpha` (0x515584/0x5155D8), `WhiteTexture()` (0x5157BC),
`SetBlendFunc`/`SetDefaultBlendFunc` (0x515904/0x515938),
`SetBlendingEnabled(bool)` (0x515970), `SetTexturingEnabled(bool)` (0x515998),
`SetLightingEnabled(bool)` (0x5159C8), `SetCullFaceEnabled(bool)` (0x5159F8),
`SetDepthTestEnabled(bool)` (0x515A20), `SetDepthWriteEnabled(bool)` (0x515A48),
`ProgramForIdentifier(unsigned int)` (0x515B98), `UseProgram(unsigned int)`
(0x515C74), `BindTexture(Texture*)` (0x515FB0),
`SetEnabledVertexAttribArrays` (0x515F40/0x516014), `PrepareForDrawing()`
(0x5162DC), `SetUniformVariable` (0x5162D4), `SetVertexAttribPointer`
(0x5162D0), `DrawArrays(int,int,int)` (0x516344), `DrawElements(...)` (0x5163CC),
plus `StartBackgroundLoading()`/`FinishBackgroundLoading()` (0x515340/0x515344).
All are thin state caches that flush to GL; **verified** as existing symbols,
bodies not individually reconstructed.

## Open questions

- The header's `Matrix4 matrix` (0x40/0x50 guess) and `viewport` (0x18/0x20
  guess) offsets do not match the ctor's writes — the real layout is likely
  {api@0, list@8, viewport@0x20, matrix@0x40...}; needs one pass over
  `SetViewport`/`SetProjectionMatrix`/`SetMatrix` bodies to pin down.
- `CurrentContext` global address `qword_651860` (in .bss) is the hook target;
  SRE must read it after the first frame (it's null until a context exists).

## Proposed SRE hooks

- `RenderingContext_CurrentContext` (already in the header) is the gate for any
  GL-level instrumentation: overlay drawing, screenshot capture, or shader
  injection should resolve the current context once per frame.
- A screenshot capability is best built as a new exported helper that runs on
  the GL thread (call `glReadPixels` after `PrepareForDrawing`), not by reaching
  into the context's internals.
- Exposing `SetBlendingEnabled`/`SetClearColor` is safe (pure state setters) and
  useful for overlay tools; avoid touching `UseProgram`/program handles from a
  foreign thread — the GL state is single-threaded.