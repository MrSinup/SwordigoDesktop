# NeedleWarfare II X SwordigoDesktop
## Multiplayer Player Sync & Hermite Interpolation Plan

> **Credits**: NeedleWarfare II (NW2) multiplayer mod architecture created by **dukinja**.

---

## Executive Summary
This document specifies the real-time state synchronization, remote player ghost instantiation, and movement interpolation algorithms for NeedleWarfare II multiplayer in SwordigoDesktop.

---

## 1. Multi-Player Network Topology

NeedleWarfare II supports both **Peer-to-Peer (P2P)** mesh networking and **Host/Server-Client** topology:

```
                          ┌──────────────────────────┐
                          │   Host / Server Instance │
                          │   (Relays Packets &      │
                          │    Syncs Monsters)       │
                          └─────────────┬────────────┘
                                        │
             ┌──────────────────────────┴──────────────────────────┐
             ▼                                                     ▼
┌───────────────────────────┐                         ┌───────────────────────────┐
│     Client Instance #1    │ ◄─── Direct UDP Sync ───►   │     Client Instance #2    │
│    (Remote Hero Ghost)    │                         │    (Remote Hero Ghost)    │
└───────────────────────────┘                         └───────────────────────────┘
```

---

## 2. Remote Hero Ghost Instantiation

When a new player connects over RakNet:
1. SRE spawns a remote `SceneObject` using `Mini.RecreateHero()` or spawning a dummy character mesh (`textures/hero_remote.pvr`).
2. The remote hero object is assigned a unique `RakNetGUID` string key.
3. Collisions between remote player ghosts are configured based on PvP settings (ghost clipping or PvP hurtbox triggers).

---

## 3. Position & Rotation Interpolation (Hermite Spline)

Because network packets arrive at ~20 Hz over UDP while the game renders at 60–144 FPS, direct position updates cause visual jittering. SRE applies **Hermite Spline Interpolation** to smooth remote player movements:

$$\mathbf{P}(t) = (2t^3 - 3t^2 + 1)\mathbf{P}_0 + (t^3 - 2t^2 + t)\mathbf{V}_0 + (-2t^3 + 3t^2)\mathbf{P}_1 + (t^3 - t^2)\mathbf{V}_1$$

Where:
* $\mathbf{P}_0, \mathbf{V}_0$ = Last received position and velocity vector.
* $\mathbf{P}_1, \mathbf{V}_1$ = Target incoming position and velocity vector.
* $t$ = Normalized interpolation factor ($0.0 \le t \le 1.0$).

---

## 4. State Packet Payload Layout (24 Bytes)

```
Offset  Type     Field          Description
──────  ───────  ─────────────  ─────────────────────────────────
0x00    uint8    packet_id      ID_SWORDIGO_PLAYER_SYNC (213)
0x01    uint8    sequence_num   Rolling sequence counter
0x02    float32  pos_x          World X coordinate
0x06    float32  pos_y          World Y coordinate
0x0A    float32  pos_z          World Z coordinate
0x0E    float16  vel_x          Half-precision X velocity
0x10    float16  vel_y          Half-precision Y velocity
0x12    uint8    anim_state     Running / Jumping / Swinging / Hurt
0x13    uint8    facing_dir     0 = Left, 1 = Right
0x14    uint8    current_hp     Current Health points
0x15    uint8    equipped_item  Sword ID / Armor ID
0x16    uint16   flags          PvP active, shield up, spell casting
```
