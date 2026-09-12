# Ruby MCP Server — Swordigo internals for AI agents

A **Model Context Protocol** server embedded in the Ruby SDK, 100% native C++17,
zero new dependencies. It lets AI agents (Claude Desktop, Cline, Continue,
Cursor, any MCP client) query and decode Swordigo assets — `.scl` scripts,
`.scene` scenes, `.POD` models, textures — through the same battle-tested
modules the editor itself uses (`filerift`, `scene_loader`, `pod_loader`,
`pvr_loader`).

| File | Role |
|---|---|
| `src/tools/ruby_mcp.h` | Public API (RunStdioServer / RunHttpServer / HandleLine / ToolListText / DefaultRootDir) |
| `src/tools/ruby_mcp.cpp` | JSON engine + JSON-RPC 2.0 + MCP state machine + 17 tools + **HTTP transport** |
| `src/tools/asset_viewer.cpp` | `--mcp-server` / `--mcp-http-server` flags + **Help → MCP Console** tester |
| `src/tools/ruby_cli.cpp` | `ruby_cli mcp [DIR]` / `mcp-http [PORT] [DIR]` subcommands |

---

## 1 · Feasibility — is this viable in pure C++?

**Yes — and it is the *right* amount of work.** MCP for a *local, tool-heavy*
server is just **JSON-RPC 2.0 over stdio**: one JSON message per line on stdin,
responses on stdout. No HTTP, no sockets, no WebSocket, no SDK. A compliant
server needs only four request types to be useful: `initialize`, `tools/list`,
`tools/call`, plus optional `resources/*` and `prompts/*`.

What that costs in pure C++:

| Building block | Effort | Status |
|---|---|---|
| JSON parser + writer (strict UTF-8, escaping) | ~350 lines | ✅ in `ruby_mcp.cpp` (self-contained) |
| JSON-RPC 2.0 dispatch (ids, errors, notifications, batches) | ~120 lines | ✅ |
| MCP protocol (initialize, capabilities, tools/resources/prompts) | ~200 lines | ✅ |
| Tool implementations | ~700 lines, but **~0 novel logic** | ✅ — every tool calls existing modules |

The key insight: **all the hard domain logic already existed** as C++ modules
linked into `bin/ruby` *and* `bin/ruby_cli`:

- `libfilerift.so` — `filerift::decode_protobuf(bytes, "scl"/"scene")`, `extract_lua_generic`
- `libswpod.so` — `av::scene_load`, `av::scene_list_templates`, `av::scene_program_source`, `av::pod_load`
- `src/tools/scene_entity.cpp` / `scene_physics.cpp` / `scene_collision.cpp` — `entity_parse`, `anim_bindings`, `physics_parse`, `collision_parse`
- `libswcore.so` — `pvr_decode_to_rgba`, zlib

So the MCP layer is a **thin protocol adapter** over verified code — no reverse
engineering, no reimplementation, no drift from what the editor sees.

### The two real engineering gotchas (both solved)

1. **Protocol purity of stdout.** Scene/tool libraries print debug logs
   (`[scene_loader] …`, filerift notices) to stdout. In stdio-MCP, stdout *is*
   the wire. Solution: a `StdoutGuard` RAII object that `dup2`s fd 1 → fd 2 for
   the duration of every tool call **and flushes C++ stdio buffers before
   restoring fd 1** (plain `dup2` is not enough — userspace buffering would
   flush the stray bytes after the restore). `bin/ruby --mcp-server` also
   unbuffers stdout (`setvbuf _IONBF`).
2. **Valid UTF-8 on the wire.** Decoded `.scl`/`.scene` markup contains raw
   high bytes and control bytes. The JSON writer validates every multi-byte
   sequence and maps invalid bytes to U+FFFD, so every emitted message is
   strictly valid UTF-8 (verified: full 18-message batch parses 100%).

### Verified protocol compliance

A 18-request integration test drives the server through `bin/ruby_cli mcp`
and asserts on raw stdout: **pure JSON lines, initialize handshake, ping,
tools/list (17 tools), 12 tool calls against real assets, resources/list,
resources/read, prompts/list, prompts/get, error paths** — all pass. Same
server runs in the GUI binary via `bin/ruby --mcp-server` (verified 2/2).

### Deliberate scope decisions

- **Read-only tools.** Nothing writes files, nothing executes shell commands.
  Agents can *understand* Swordigo internals but cannot corrupt assets.
- **stdio transport only.** Local MCP servers are spawned per-client, so stdio
  is the correct default. (A TCP/WebSocket listener is a trivial future
  extension if you ever want a shared daemon.)
- **Session-per-process.** One MCP client per server process (the stdio
  contract); the GUI's console reuses the same dispatcher in-process.

---

## 2 · Tools

| Tool | What it does |
|---|---|
| `scl_decode` | Decode a `.scl` to readable FileRift markup / Lua (auto-picks the most readable representation) |
| `scene_decode` | Full `.scene` → FileRift markup text |
| `scene_objects` | Structured JSON: every object's transform, mesh, entity/hero/monster data, animation bindings, physics, collision, components |
| `scene_summary` | Counts, bounds, spawn point, waters/lights/shadows, external library references |
| `scene_programs` | Onload Lua programs attached to scene objects |
| `scene_templates` | The add-object palette (embedded + external `.scl` templates) |
| `scene_libraries` | External `.scl` libraries a scene references (+ missing ones) |
| `search` | Folder-wide string search — **decoded-aware** (searches `.scl`/`.scene` as text), with per-file counts + context snippets |
| `search_scl` | String search inside one decoded `.scl` |
| `file_info` | Size, type, and type-specific facts for any asset |
| `pod_info` | `.POD` structure: meshes, nodes, materials, textures, frames, bones, AABB |
| `pod_blocks` | POD block-id histogram (PowerVR numeric tags, e.g. 6006 VertexList, 6014 Interleaved, 6020 MeshUnpackMatrix) |
| `list_files` | Enumerate assets under a directory |
| `texture_info` | `.pvr` / `.tex` / `.png` dims + format (optional full decode) |
| `read_file` | Raw file view (txt/c/cpp/md/json/…): byte-offset pagination, max-lines cap, hex dump, binary detection |
| `list_dir` | Generic `ls`: dir listing with sizes/types/extensions, recursive + name filter |
| `find_files` | Recursive glob/substring filename search (e.g. `*.cpp`, `main`) |

Plus **resources** (`file://` URIs mirroring the asset root; `.scl`/`.scene`
decode to text on read) and **prompts** (`analyze_scene`, `explain_scl`).

---

## 3 · Running it

```bash
# Headless stdio server (Claude Desktop / Cline / Continue spawn this):
bin/ruby_cli mcp [ROOT_DIR]
bin/ruby     --mcp-server [--mcp-root ROOT_DIR]

# HTTP server — for URL-based clients (ChatGPT desktop, web clients):
bin/ruby_cli mcp-http [PORT] [ROOT_DIR]              # default http://127.0.0.1:8765/mcp
bin/ruby     --mcp-http-server [--port N] [--mcp-root ROOT_DIR]

# In-app testing: Help → MCP Console (request editor, response viewer, tool list)
```

The HTTP transport implements MCP **Streamable HTTP** (POST `/mcp` →
`application/json`, GET `/mcp` → SSE channel with heartbeats, `Mcp-Session-Id`
header, CORS for web clients) plus a `/sse` path alias for older SSE clients.
Local-only by default (binds `127.0.0.1`).

Resource root resolution order: `ROOT_DIR` argument → `MCP_ROOT` env →
`~/.local/share/swordigo-desktop/assets` → current directory.

### ChatGPT desktop (Secure MCP Tunnel)

ChatGPT's MCP add-server form only accepts **HTTPS URLs** — a plain
`http://127.0.0.1:8765/mcp` is rejected. The supported way to connect a
*private* server like ours is OpenAI's **Secure MCP Tunnel**: `tunnel-client`
runs on this machine, opens an **outbound-only** HTTPS connection to OpenAI's
control plane, long-polls for queued MCP work, forwards each JSON-RPC request
to our local server (stdio **or** HTTP), and returns the response through the
tunnel. No inbound ports, no public IP, no HTTPS cert needed.

**Step 0 — install `tunnel-client`** (official `openai/tunnel-client`, already
installed to `~/.local/bin` on this machine; update to latest release anytime):

```bash
curl -sL -o /tmp/tc.zip https://github.com/openai/tunnel-client/releases/latest/download/tunnel-client-v0.0.11-linux-amd64.zip
unzip -o -q /tmp/tc.zip -d /tmp/tc && install -m 755 /tmp/tc/tunnel-client ~/.local/bin/tunnel-client
tunnel-client --version
```

**Step 1 — create the tunnel in Platform** (one-time, needs your OpenAI
account): open **platform.openai.com → tunnel settings** (Platform →
Tunnels) → **Create tunnel**. Copy the `tunnel_id` (looks like
`tunnel_0123456789abcdef0123456789abcdef`) and a **runtime API key** (also
issued there). Both go to the same Platform organization that owns the tunnel.

**Step 2 — point the tunnel at our server.** Two options, both fully tested:

*HTTP transport* — run the Ruby MCP HTTP server in one terminal, then init a
profile pointing at it:

```bash
# Terminal A
cd /home/quantumcreeper/SwordigoDesktop && ./bin/ruby_cli mcp-http 8765

# Terminal B
export CONTROL_PLANE_API_KEY="sk-..."   # runtime key from Step 1
tunnel-client init \
  --sample sample_mcp_http_local \
  --profile ruby-swordigo \
  --tunnel-id tunnel_0123456789abcdef0123456789abcdef \
  --mcp-server-url http://127.0.0.1:8765/mcp
```

*stdio transport (simpler — no extra server)* — the tunnel can spawn our CLI
as a child process directly:

```bash
export CONTROL_PLANE_API_KEY="sk-..."
tunnel-client init \
  --sample sample_mcp_stdio_local \
  --profile ruby-swordigo \
  --tunnel-id tunnel_0123456789abcdef0123456789abcdef \
  --mcp-command "/home/quantumcreeper/SwordigoDesktop/bin/ruby_cli mcp"
```

**Step 3 — validate and run:**

```bash
tunnel-client doctor --profile ruby-swordigo --explain
# verify the local UI shows healthy+ready, then keep it running:
tunnel-client run --profile ruby-swordigo
# health: curl http://127.0.0.1:8080/healthz  ·  admin UI: http://127.0.0.1:8080/ui
```

**Step 4 — connect from ChatGPT:** enable **developer mode** (Settings →
Security and login; Enterprise/Edu workspaces must have it granted by an
admin), then open **ChatGPT → Plugins → + (create developer-mode app) →
Connection: Tunnel**. Pick the tunnel when ChatGPT lists it (or paste the
`tunnel_id`). No URL is entered — the tunnel endpoint is OpenAI-hosted and
HTTPS by definition. All 17 tools then appear in the app's tool picker.

> Notes: `tunnel-client` needs only outbound HTTPS to `api.openai.com:443`.
> Tunnel permissions (Tunnels Read/Manage) are granted by the Platform
> organization owner; role changes can take ~30 min to propagate. The admin
> UI is loopback-only by default.

### Other ChatGPT-like clients that DO accept plain http URLs

For clients that still allow local HTTP (web-based MCP testers, Cursor,
Cline), the Streamable HTTP transport stays available:

```bash
bin/ruby_cli mcp-http 8765        # http://127.0.0.1:8765/mcp
```

### Client configs (stdio)

**Claude Desktop** (`claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "ruby-swordigo": {
      "command": "/home/quantumcreeper/SwordigoDesktop/bin/ruby_cli",
      "args": ["mcp"]
    }
  }
}
```

**Cline / Continue / Cursor** — same `command`/`args` shape. To point at a
custom asset folder add the dir as a positional arg (`["mcp",
"/path/to/assets"]`) or set the `MCP_ROOT` env var.

### Quick smoke test (no client needed)

```bash
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05"}}' \
  | ./bin/ruby_cli mcp
```

---

## 4 · Architecture notes

- `mcp::HandleLine(line, out)` is the single protocol entry — used by both the
  stdio loop and the in-app console, so behavior is identical everywhere.
- Tools are a table of `{name, description, JSON-schema, handler}`; adding a
  tool is ~15 lines.
- Scene loads happen per-call (no caching) — correct for agent workloads,
  fast enough (a 93-object scene parses in tens of ms).
- Decoded text is capped at 8 MiB per tool result; searches cap files at
  64 MiB and results at 100 files by default (all tunable via tool args).

## 5 · Future extensions (easy wins)

- **`scene_edit`** tools (move/duplicate/delete objects, set fields) reusing
  `scene_workspace` + `scene_save` — opt-in behind a flag, since they write.
- **`ground_mesh`** tools (subdivide/split/extrude via `scene_workspace`) for
  procedural level-gen agents.
- **`texture_edit`** tools (the Image Editor's CPU-side pixel ops are already
  in `asset_viewer.cpp` — extract into a module).
- **`lua_run`** — evaluate a Lua snippet against a scene's scripts via
  `scene_lua`.
- TCP/WebSocket transport + multiple sessions if you want a shared daemon.
- `resources/subscribe` change notifications (file mtime polling).
