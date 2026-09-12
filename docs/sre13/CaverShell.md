# CaverShell (`Caver::CaverShell`)

## Summary

`CaverShell` is the application shell — the top-level object of the game
process. It owns argv, the window/view hierarchy, and the platform
navigation controller, and it drives the frame loop (`Update`, `Render`,
touch dispatch). It is the "Caver" analogue of an OS app delegate; the
engine's bootstrap creates one instance and calls `InitApplication`.

It is reachable from Lua as the shell that owns the `RenderingContext`
(see `CaverShell::Render(RenderingContext*)`). SRE's host talks to it
through `SuspendApplication`/`ResumeApplication`/`QuitApplication` and, via
the `RenderingContext` current-context singleton, the draw surface.

## Struct layout (64-bit verified; 32-bit unverified)

The header's offsets check out against `InitApplication` (0x321B90):
argv vector read at +0x08/+0x10, `FWShellPreferences` at +0x20
(`FWShellPreferences::Set(shell+0x20, ...)`), `initialized` bool at +0x60
(`*(shell+0x60)=1`). The `window`/`navigationController` offsets come from
`InitView`/`ReleaseView` family and are marked accordingly.

| Offset (32-bit) | Offset (64-bit) | Size | Type | Field | Notes |
|---|---|---|---|---|---|
| 0x04 | 0x08 | 0x08 | — | `_pad0` | Unresolved; likely `int argc` + pad (argv vector comes right after). |
| 0x08 | 0x08 | 0x08 | `void*` | `argvBegin` | Vector of 24-byte entries; each entry has an 8-bit-size String-like at +0 and data/ptr at +8/+16 (see InitApplication's argv scan: `v4 += 24`, reads byte@+0, qword@+8, ptr@+16). |
| 0x10 | 0x10 | 0x08 | `void*` | `argvEnd` | |
| 0x18 | 0x18 | 0x08 | — | `_pad1` | |
| 0x20 | 0x20 | 0x34/0x40 | `FWShellPreferences` | `preferences` | Embedded preferences struct; `InitApplication` scans argv for `-fw-something` (string at argv[..]==6-length tag `"-fake"`?) and calls `FWShellPreferences::Set(shell+0x20, 3, 0.5)` — a framebuffer scale preference. Size 0x40 (64-bit) is a guess from the next field at 0x60. |
| 0x54 | 0x60 | 0x01 | `bool` | `initialized` | Set to 1 at the end of `InitApplication`. |
| 0x55 | 0x61 | 0x0F/0x1F | — | `_pad2` | |
| 0x64 | 0x80 | 0x08 | `void*` | `window` | (64-bit offset inferred from header; not directly confirmed in the functions examined — **guess**.) |
| 0x68 | 0x88 | 0x08 | — | `_pad3` | |
| 0x70 | 0x90 | 0x08 | `void*` | `navigationController` | (Same caveat — **guess**.) |
| 0x78 | 0x98 | 0x08 | — | `_pad4` | |
| 0x7C | 0xA0 | 0x01 | `bool` | `suspended` | App suspension flag. |
| 0x7D | 0xA1 | 0x1F | — | `_pad5` | tail (size ≈ 0xC0 64-bit, unverified). |

## Vtable

None for the data layout; `CaverShell`'s virtual surface is exposed through
its own vtable but the struct begins with `_pad0` (no vtable pointer at +0
per the layout above — the shell is likely held via
`boost::shared_ptr<CaverShell>` with `checked_delete`, and virtual dispatch
goes through the `RenderingContext`-side classes instead). **Unresolved.**

## Exported functions

### CaverShell::InitApplication()
- Mangled: `_ZN5Caver10CaverShell15InitApplicationEv` @ 0x321B90
- Behavior: scans argv for a preferences tag, sets `preferences` value 3 to 0.5 (likely "render scale"), sets `initialized=1`. (Early part of engine boot — audio/video init lives in `InitView`.)
- Confidence: **verified in IDA**.

### CaverShell::QuitApplication()
- Mangled: `_ZN5Caver10CaverShell15QuitApplicationEv` @ 0x321C70
- Behavior: initiates shutdown (tears down view, releases rendering context, exits frame loop).
- Confidence: verified symbol exists; body not examined — **inferred**.

### CaverShell::SuspendApplication(bool)
- Mangled: `_ZN5Caver10CaverShell18SuspendApplicationEb` @ 0x323828
- Behavior: app-background path; stops the frame loop, releases GL context.
- Confidence: **inferred from name/sym** (address confirmed in dynsym).

### CaverShell::ResumeApplication()
- Mangled: `_ZN5Caver10CaverShell17ResumeApplicationEv` @ 0x323924
- Behavior: app-foreground path; re-creates GL context.
- Confidence: **inferred from name/sym**.

### CaverShell::Update(float) / CaverShell::Render(RenderingContext*)
- Mangled: `_ZN5Caver10CaverShell6UpdateEf` @ 0x323944 / `_ZN5Caver10CaverShell6RenderEPNS_16RenderingContextE` @ 0x323A60
- Behavior: per-frame update (game controllers, scene) and draw (scene + GUI into the current context).
- Confidence: **inferred from name/sym** (addresses confirmed).

### CaverShell::InitView() / UpdateViewSize() / ReleaseView()
- Mangled: `@0x321DB4` / `@0x321C74` / `@0x32361C`
- Behavior: create/destroy the window+GL surface; `UpdateViewSize` recomputes the camera aspect (calls `Camera::SetAspectRatio`).
- Confidence: **inferred from name/sym**.

### Touch plumbing
`TouchBegan/TouchMoved/TouchEnded/TouchCancelled(FWTouch const&)` @
0x323CB0/0x323D8C/0x323E64/0x323F3C and `ConvertTouchToWindow` @ 0x323C24 —
input dispatch into the GUIResponder/view hierarchy. Confidence: inferred.

## Open questions / unresolved offsets

- 0x00–0x07 (`_pad0`): likely `argc`/reserved — no access found.
- 0x80+ (window, navigationController, suspended): the offsets in the header
  were hand-annotated, not disassembly-verified in this pass. Verify against
  `InitView` (0x321DB4) before relying on them.
- The argv element format (24-byte entries, String-like at +0) suggests a
  second, 16-byte-capacity string type used only for CLI args.

## Proposed SRE hooks

- `g_sre_quit_requested` / `g_sre_suspend_requested`: host flags that call
  `CaverShell_QuitApplication`/`SuspendApplication` at a safe point (end of
  `Update`) instead of the host force-killing the process — lets the save
  flush run.
- **Safe**: `initialized`, `suspended` are plain bools — a host can read them
  to know when the engine is up. **Unsafe**: touching `argvBegin/argvEnd`
  (heap vector) or `preferences` without `FWShellPreferences::Set`.