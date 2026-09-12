# Throndigo — Server / Host (Multiplayer) API

The mod turns the hero `OnLoad` (in `hiro.scl`) into a UDP multiplayer
**client** that streams its own state to a room server and renders other
players. The transport is LuaSocket's `broken_socket` UDP module, which
lives in **libmini.so** (the SwMini loader) or is provided natively by
SRE's Lua host.

## Configuration

```lua
local ip   = "192.168.0.100"
local port = 13337
local now  = os.time()

udp = broken_socket.udp()
udp:setpeername(tostring(ip), port)  -- connected UDP socket
udp:settimeout(0)                    -- non-blocking
```

- `192.168.0.100:13337/UDP` is the hard-coded **target server**.
- All I/O is non-blocking (`settimeout(0)`), so every receive may return
  `nil, "timeout"`.

## Heartbeat / server detection (`host_on`)

```lua
function host_on()
    local success, err = udp:send(0 .. "-" .. 0 .. "-".. 0 .. "-" ..
                                  0 .. "-" .. 0 .. "-" .. 0 .. "-".. 0)
    if not success then return false, err end
    local data, recv_err = udp:receive()
    if data then return true else return false, err end
end
```

Sends the "empty player" token (seven zero fields) and reports the server
online if anything at all comes back. The main loop runs `mp()` instead
of `host_on()` (heartbeat is handled as a side effect of every `mp()`
send).

## State broadcast (`mp`)

Every ~50 ms (`Program.Wait(0.05)`), the client sends its own state:

```lua
hx       = tostring(hero:position():x())
hy       = tostring(hero:position():y())
vx       = tostring(hero:velocity():x())
vy       = tostring(hero:velocity():y())
fac      = tostring(Entity.GetFacingDirection(hero))
anim     = tostring(hero.curranim)
z        = tostring(hero:position():z())
rotation = rotate or 0                       -- ModelTransformController angle

udp:send(hx.."-"..hy.."-"..vx.."-"..vy.."-"..fac.."-"..anim.."-"..z.."-"..rotation)
```

**Wire format (per player, dash-separated, sent to server):**

```
x-y-vx-vy-fac-anim-z-rotation
```

Then it immediately receives:

```lua
local data, err = udp:receive()
```

- `data == "no_players"` → server reachable, no remote players.
- `data` parses as the `;`-separated list → `remotePlayers` updated,
  `server_online = true`, `last_server_heartbeat = os.time()`.
- else (incl. `"timeout"`) → `server_online = false`.

## Receive format (`parsePlayerData`)

Server reply is `;`-delimited player records, each `,`-delimited:

```lua
for entry in data:gmatch("([^;]+)") do
    local x, y, vx, vy, fac, anim, z, rotation =
        entry:match("([^,]+),([^,]+),([^,]+),([^,]+),([^,]+),([^,]+),([^,]+),([^,]+)")
    table.insert(result, {
        x = tonumber(x), y = tonumber(y),
        vx = tonumber(vx), vy = tonumber(vy),
        fac = tonumber(fac),
        anim = anim,
        z = tonumber(z),
        rotation = tonumber(rotation),
    })
end
```

**Reply wire format (per player, comma-separated, joined with `;`):**

```
x,y,vx,vy,fac,anim,z,rotation
```

## Remote player lifecycle

`remotePlayers` (parsed) and `spawnedPlayers` (live objects) are the two
global tables. For every parsed player:

```lua
local ident = "np_" .. i
active[ident] = true
if not Scene.Find(ident) then
    np = Scene.CreateObject("dummy", ident)   -- handle.scl template
    spawnedPlayers[ident] = np
    np.lastUpdate = os.time()
    np.rotation = p.rotation
else
    np = Scene.Find(ident)
    np:setPosition(Vector3.New(p.x, p.y, p.z))
    np:setVelocity(Vector3.New(p.vx, p.vy))
    Entity.SetFacingDirection(np, p.fac)
    ModelTransformController.SetRotationAngle(np, p.rotation or 0)
    np.curranim = p.anim                -- drives handle.scl animation
end
```

Stale players are culled if `os.time() - lastUpdate > 1` s:

```lua
for ident, np in pairs(spawnedPlayers) do
    if now - lastUpdate > 1 then
        np:destroy(); spawnedPlayers[ident] = nil
    end
end
```

### The `dummy` template (handle.scl)

`Scene.CreateObject("dummy", ident)` spawns the remote avatar: a
`hiro` Model + `ModelTransformController`, Skill, `AnimationController`
(self-update off), `MonsterEntity`, `EntityController`, `CollisionShape`,
and a `SwingableWeaponController` with **`WeaponTemplateName =
'dummy_thorn'`**. Its `OnLoad` wires `curranim` to the AnimationController
and uses `EntityController.StartSwing` so remote players visibly attack.

## Status HUD

A click-insensitive overlay button reports online state:

```lua
local button = OverlayController.NewButton("Server Online\nfalse", 0.2, 0.1, 0.3, 0.2)
button:setAlwaysActive(true)
button:setClickable(false)
button:setBackgroundAlpha(0)
button:setTextScale(0.6)
```

Updated every loop: `button:setText("Server Online \n" ..
tostring(server_online))`.

## Server expectations (from the client's point of view)

1. UDP socket listening on port **13337**.
2. Each incoming datagram interpreted as one player's
   `x-y-vx-vy-fac-anim-z-rotation` dash frame.
3. Reply is either the literal string `no_players` or `;`-joined
   `x,y,vx,vy,fac,anim,z,rotation` records covering **all** clients
   (including the sender).
4. No per-player addressing / authentication — the server resolves
   clients by their UDP source address/IP.

## Reference room server (repo)

`MultiSW-Server-master/` is a TypeScript Node service (v0.0.1
"indev", client-less) using `dgram.createSocket("udp4")`:

- Listens on **port 69420** (differs from the client's 13337).
- Handles JSON `{x, y}` messages (differs from the dash-separated frame
  format above).
- Keeps a `Database` Map keyed by client IP, broadcasts updated state to
  connected peers.

> Port/protocol mismatch: the workspace server is a different/older
> protocol. The in-game client targets `192.168.0.100:13337` with the
> dash-format frames documented here. A fully matching server is not in
> the repo.

## SRE support requirements

For this to run under SRE the Lua host must expose (all already present
in `src/sre/sre_mini_api.c`):

- `broken_socket` (LuaSocket `socket.core`) with `udp/setpeername/
  settimeout/send/receive/close`
- `os.time()` (stdlib), `OverlayController.NewButton` (+ setText,
  setClickable, setBackgroundAlpha, setTextScale, setAlwaysActive,
  setPosition/makeMovable)
- `Scene.CreateObject/Find`, `Vector3.New`, `hero:position()/
  :velocity()`, `Entity.GetFacingDirection/SetFacingDirection`,
  `ModelTransformController.SetRotationAngle`, `hero.curranim`
- UDP syscalls (`sendto`/`recvfrom`) must reach the host network stack,
  not the emulated socket layer.