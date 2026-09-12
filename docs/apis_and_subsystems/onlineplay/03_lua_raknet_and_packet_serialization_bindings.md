# NeedleWarfare II X SwordigoDesktop
## Lua RakNet & Binary Packet Serialization Bindings

> **Credits**: NeedleWarfare II (NW2) multiplayer mod architecture created by **dukinja**.

---

## Executive Summary
This document specifies the Lua API bindings for RakNet UDP networking (`RakNet.*`) and binary packet serialization (`pack.*` / `lpack`), ported from `libneedlewarfare` by **dukinja**.

---

## 1. `RakNet.*` Lua API Specification

### Static Constructor & Instance Access
```lua
local peer = RakNet.GetInstance() -- Returns singleton RakPeerInterface userdata handle
```

### Peer Member Methods (`RakNet.Peer`)

| Lua Method | Arguments | Return Type | Description |
| :--- | :--- | :--- | :--- |
| `peer:Startup(maxConn, socketDesc, threadPriority)` | `(int, table, int)` | `string` | Initializes UDP socket on specified port (e.g. 12345) |
| `peer:Connect(host, port, password)` | `(string, int, string)` | `string` | Initiates non-blocking UDP connection to remote host |
| `peer:Shutdown(blockDuration)` | `(int)` | `nil` | Gracefully closes all active connections and releases sockets |
| `peer:Send(bitstream, priority, reliability, channel, target_guid, broadcast)` | `(BitStream, string, string, int, string, bool)` | `number` | Transmits a serialized packet over network |
| `peer:Receive()` | `()` | `Packet` or `nil` | Polls for incoming network packets from queue |
| `peer:DeallocatePacket(packet)` | `(Packet)` | `nil` | Releases packet memory back to pool |
| `peer:GetGuidFromSystemAddress(sys_addr)` | `(string)` | `string` | Converts IP/Port to unique RakNet GUID string |

---

## 2. Binary Packet Serialization (`pack.*`)

To minimize network bandwidth over UDP, `libneedlewarfare` uses `lpack` byte packing:

```lua
-- Encoding a player state packet into binary format
local packet_data = pack.pack("bfffhh", 
    ID_PLAYER_SYNC,   -- b = byte (Packet ID)
    pos_x, pos_y, pos_z, -- f = float (3D Coordinates)
    anim_state, health  -- h = short (Anim ID, Health HP)
)

-- Transmitting over RakNet
local bs = RakNet.BitStream()
bs:Write(packet_data)
peer:Send(bs, "HIGH", "UNRELIABLE_SEQUENCED", 0, UNASSIGNED_GUID, true)

-- Decoding an incoming packet
local id, px, py, pz, anim, hp = pack.unpack(received_bytes, "bfffhh")
```

---

## 3. Custom Network Message Identifier Enums

NeedleWarfare II defines custom network message IDs starting above `ID_USER_PACKET_ENUM` (212):

```cpp
enum SwordigoNetworkMessages {
    ID_SWORDIGO_PLAYER_SYNC      = ID_USER_PACKET_ENUM + 1, // 213: Pos, Vel, Anim
    ID_SWORDIGO_SPELL_CAST        = ID_USER_PACKET_ENUM + 2, // 214: Fireball, Magic, Bomb
    ID_SWORDIGO_DAMAGE_EVENT      = ID_USER_PACKET_ENUM + 3, // 215: PvP Damage hit
    ID_SWORDIGO_CHAT_MESSAGE      = ID_USER_PACKET_ENUM + 4, // 216: In-game chat
    ID_SWORDIGO_PORTAL_WARP       = ID_USER_PACKET_ENUM + 5, // 217: Synchronized level load
    ID_SWORDIGO_SPAWN_MONSTER     = ID_USER_PACKET_ENUM + 6  // 218: Host monster spawn sync
};
```
