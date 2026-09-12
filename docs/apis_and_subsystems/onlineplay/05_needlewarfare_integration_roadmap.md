# NeedleWarfare II X SwordigoDesktop
## Integration Roadmap & Verification Plan (with Host ImGui Bridge)

> **Credits**: NeedleWarfare II (NW2) multiplayer mod architecture created by **dukinja**.

---

## Executive Summary
This document defines the phased implementation roadmap and testing plan for integrating **NeedleWarfare II** online multiplayer (by **dukinja**) into `SwordigoDesktop` (SRE), including the **Host ImGui Bridge Stubbing Strategy**.

---

## 1. Phased Integration Roadmap

```
Phase 1: RakNet Source Integration & Build Setup
 ├── Copy RakNet 4.x sources into src/sre/raknet/
 ├── Add build rules to Makefile (libswd_net.so)
 └── Verify clean compilation without warnings

Phase 2: Lua RakNet & Binary Pack Binding Registration
 ├── Port raknet_lua.cpp and pack_lua.cpp into SRE
 ├── Register RakNet and pack namespaces in Lua VM
 └── Test loopback UDP socket creation in SRE Lua console

Phase 3: ImGui Guest Stubbing & Host Bridge Implementation
 ├── Replace guest imgui.cpp with SRE Host ImGui Bridge Stubs
 ├── Forward guest ImGui.Begin(), ImGui.Text(), ImGui.Button() calls directly to Host C++ ImGui
 └── Verify zero guest ImGui bloat & native 144+ FPS rendering

Phase 4: Remote Hero Spawning & Interpolation
 ├── Implement remote player ghost instantiation
 ├── Add Hermite spline position/velocity interpolation
 └── Test 2-player state synchronization over local network

Phase 5: Combat Sync & PvP Mechanics
 ├── Sync sword attack hitboxes & damage events
 ├── Sync spell casting (Fireball, Magic Bomb, Dimensional Rift)
 └── Sync portal level transitions between host and clients

Phase 6: User Interface & Server Browser
 ├── Add native main menu "MULTIPLAYER" GUI button
 └── Add host server creation & IP connection dialog
```

---

## 2. Verification & Testing Strategy

### Automated Loopback Unit Tests
1. **Socket Bind Test**: Execute `peer:Startup(32, {port=12345}, 1)` and verify `RAKNET_STARTED`.
2. **Local Loopback Connection Test**: Instantiate two `RakPeer` handles in Lua, connect `127.0.0.1:12345`, and verify packet exchange.
3. **Pack/Unpack Roundtrip Test**: Verify binary encoding of floats, shorts, and bytes via `pack.pack` and `pack.unpack`.
4. **Host ImGui Forwarding Test**: Invoke `ImGui.Begin("NeedleWarfare NW2")` from guest Lua and verify window renders natively via Host's Vulkan/OpenGL ImGui context.

### Multi-Instance Integration Testing
- Launch two concurrent instances of `bin/swordigo_boot` on desktop.
- Instance 1 hosts server on port 12345 (`Mini.HostGame()`).
- Instance 2 connects to `127.0.0.1:12345` (`Mini.ConnectGame("127.0.0.1")`).
- Verify remote player ghost appears in scene and moves smoothly in real-time with zero single-player behavior regression!
