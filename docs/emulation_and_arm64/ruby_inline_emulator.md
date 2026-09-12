# Ruby GG Inline Mini-Emulator ("Engine Pod") — Feasibility & Design

**Status: implemented (Phase 1) + validated live.** Ruby GG (Qt6) boots a real
Swordigo **1.4.13 ARM64** session (engine `libswordigo.so` + `libsre13.so`) in a
dock widget at low resolution — no separate SDL window. Frames stream over
shared memory and render like a video player; the widget is zoomable, pausable
and muteable; input is captured the moment the cursor is over the emulator.

```
┌─ ruby_gg (Qt6) ─────────────────────────────────────────────────────────┐
│ [📱][📄][📂][💾]… left rail       (Run 1.4.13, New, Open, Save, …)    │
│ ┌─────────────┬───────────────────────────────┬────────────────────────┐ │
│ │ asset/outln │       3D viewport / IDE       │ inspector              │ │
│ │ (left docks)│      (shrinks when docks       │ engine preview  16:9  │ │
│ │             │       are widened)             │   [▶]⏸🔊 720p▼        │ │
│ └─────────────┴───────────────────────────────┴────────────────────────┘ │
└───────────────────────────────────────────────────────────────────────────┘
        ▲ shared memory (3×RGBA ring, /dev/shm)        ▲ stdin PodMsg stream
┌───────┴─────────────────────────────────────────────────────────────────────┐
│ child: bin/swordfare --pod-preview shm:WxH --lib engine/v1.4.13/arm64-v8a/… │
│        hidden SDL/GL window → game renders → glReadPixels → row flip → ring │
└──────────────────────────────────────────────────────────────────────────────┘
```

## 1. Problem

- Swordfare's emulator host is an **8335-line monolithic SDL app** — one window,
  one GL context, one event loop, ~200 process-global pieces of state
  (`load_and_boot_arm64()` alone is ~2,400 lines).
- Ruby GG is a separate Qt6 process with its own UI; it cannot "just call" the
  monolith without recompiling every subsystem into it (Dynarmic, ELF loader,
  JNI bridges, ImGui…) and cannot host a second native GL window inside a dock
  portably (Wayland forbids foreign-window embedding; SDL↔Qt GL-context sharing
  is fragile and platform-specific).

## 2. Options evaluated

| Option | Portability | Risk | Reuse of existing pipeline |
|---|---|---|---|
| **A. Child process pod + shm frames** (chosen) | X11/Wayland/Windows | low | 100% — the whole boot/SRE13 path runs unchanged |
| B. In-process SDL window embedded via native handle | broken on Wayland, subclassing issues | high | partial |
| C. In-process library: recompile all subsystems into ruby_gg | good | very high (symbol conflicts with Qt, months of refactor) | needs extraction |
| D. Offscreen FBO + texture share into QOpenGLWidget | good | medium-high (cross-context sharing) | partial |

**A wins for stability/velocity**: a guest crash or emulator bug can only kill
the *child*, never Ruby GG; no Qt/GL context sharing; ruby stays fully
responsive while the engine runs at its own 60 fps pace.

## 3. IPC protocol (`src/platform/pod_ipc.{h,cpp}`)

- **Frames:** one POSIX shared-memory object, fixed header + **3-slot RGBA ring**
  with per-slot generation/ack counters (child = sole writer, ruby = sole
  reader, lock-free, no tearing, frames dropped not queued).
  Header: magic `"SWP1"`, width/height/stride/bytes, `state`, `fps`, `seq_total`,
  per-slot `gen[3]/ack[3]`.
- **Controls:** length-prefixed `pod::PodMsg` stream on the child's **stdin**
  (a dedicated reader thread pushes into a queue the game loop drains):
  pause/freeze, mute toggle, quit, key (SDL keycode+scancode+mods), mouse
  button/move (normalized 0..1), wheel, touch, UTF-8 text.
- ruby cleans up `/dev/shm` (`shm_unlink_name`) after the child exits, even if
  the child crashed (kill -9 never runs destructors).

## 4. Swordfare side (`--pod-preview <shm>:<W>x<H>`)

Small, additive changes in `src/main.cpp` + `Display::init(…, hidden)`:

1. Window is created **hidden** (`SDL_WINDOW_HIDDEN`) and re-sized so the
   *drawable* (back buffer) lands on the requested resolution after HiDPI
   scaling — measured pixel density was 2.0 on this Wayland laptop, so a
   960×540 request correctly yields a 960×540 frame, not 1920×1080.
2. Host overlays that would contaminate the captured frame are skipped:
   ImGui/Swordfare GUI overlay, mod-tools overlay, SRT overlay, debug/HUD text,
   loading screen. The captured pixels are exactly what a visible window would
   show.
3. `pod_publish_frame()` runs **right before SwapWindow**: `glReadPixels` of the
   whole back buffer (RGBA), row-flip (GL is bottom-up), publish to the ring,
   update `fps` once a second.
4. Control drain at the top of the ARM64 loop handles pause (full freeze of
   update+draw, 12 ms sleep — not just the host-side `g_game_paused`),
   mute (existing `GUI_MUSIC_MUTE` path → SRE volume hook), quit, and event
   synthesis via `SDL_PushEvent`, so **every existing input mapper
   (keyboard-state-driven touch buttons, keyboard events, text input) is reused
   unchanged**.
5. Crash handler UI is disabled in pod mode (ruby owns failure UX).

## 5. Ruby GG side (`src/ruby/emulator/`)

- **`workspace_detect.{h,cpp}`** — auto-detects a valid asset workspace:
  scans a root for `<root>/resources`, `<root>/assets*/resources` and nested
  instance folders, validates with real boot markers (`menu.scene/Resources`,
  `game_common_atlas_2x.atlas`, title/font textures…). Used when Boot ▶ is
  pressed and when a project folder is opened.
- **`engine_pod.{h,cpp}`** — spawns `bin/swordfare` with
  `--lib <…>/engine/v1.4.13/arm64-v8a/libswordigo.so` + `--sre` (libsre13 is
  loaded by that path automatically) + the detected `--assets` dir; attaches to
  the shm ring with retry; pulls newest frames on a 16 ms timer; deep-copies
  into a `QImage`; sends control messages; mirrors session state
  (booting/running/paused/crashed).
- **`preview_view.{h,cpp}`** — video-player widget: smooth letterboxed paint,
  zoom (Ctrl+wheel / −/+ / Fit, 25–600 %), pan at zoom, **hover-based keyboard
  capture** through an application event filter (routes WASD the instant the
  cursor is over the emulator; text fields/dropdowns/modal dialogs are never
  hijacked). Mouse clicks/moves are forwarded as normalized engine coords so a
  click inside the frame = a tap in the game.
- **`engine_preview_panel.{h,cpp}`** — the dock content: status row, the view,
  and the "player controls" under it — **▶ Boot, ⏸ Pause (freeze), 🔊/🔇 Mute,
  resolution 960×540 / 720p / 900p / 1080p (restart to apply), zoom −/+ / Fit,
  Assets…, 📷 snapshot, Log**. Min size 360×240 so it can never be squeezed into
  a portrait sliver.
- **Main window** — `Engine Preview` docked on the right below the Inspector;
  **left vertical rail** (Android-Studio style) holds the painted **phone ▶
  button** plus New / Open Project / Save / Frame / Tools / Docs. The former
  top toolbar was removed — the top panel stays free. The central widget's
  minimums were lowered so widening the left docks shrinks the **3D viewport**
  instead of crushing the right rail.

## 6. Validation (live, on this machine — 1.4.13 + libsre13 + assets13)

| Test | Result |
|---|---|
| `swordfare_boot` / `ruby_gg` builds | clean |
| Pod boot to Running | ~3 s, state=2 in shm |
| Frame ring 480×270 & 640×360 & 960×540 (density 2) | drawable == requested |
| Frames flowing 60 fps, real pixels | `avg_byte=38.2`, seq grows |
| **Pause → shm state 3** | ✔ |
| **Resume → state 2** | ✔ |
| **Mute toggle** | ✔ (no crash, log confirms) |
| Key + text events | ✔ (no crash; `w` keydown sent) |
| **Quit → clean rc=0**, shm state 0 | ✔ |
| ctest suite | 5/5 pass |

## 7. Cost & limits

- Readback: one `glReadPixels` + flip + copy per frame
  (1280×720 ≈ 3.7 MB/frame ≈ 220 MB/s at 60 fps — trivial on any desktop GPU/CPU;
  use 960×540 or 720p presets for weaker hardware).
- Saves/controls config are shared with the standalone swordfare
  (`~/.local/share/swordigo-desktop/save`, `controls_arm64.ini`) — intended.
- The child runs full-speed (60 fps, own pacing) regardless of the dock's
  refresh; ruby repaints at up to 60 Hz and simply shows the newest frame.

## 8. Roadmap (Phase 2+, parked)

- In-process/no-copy pipeline via `QOpenGLWidget` + cross-context texture (skip
  readback) once a stable shared-context path is needed.
- `kMsgVolume` per-channel volume control; screenshot stream / recording.
- Multi-touch forwarding from Qt touch events; gamepad pass-through.
- Auto-resume of a killed pod with the same session flags; session logging to a
  file next to the console panel.
- Windows: the pod protocol compiles (shm mapped via `CreateFileMapping` is
  stubbed for the producer path) — frame shm + stdin controls need the Windows
  producer half filled in if ruby-win64 must run pods locally.
