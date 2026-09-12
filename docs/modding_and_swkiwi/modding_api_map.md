# SRE Modding API — Full Hook-to-API Mapping

## Summary
**34 active hooks + 2 new hooks = 36 hooks → 47+ public APIs**

| Category | Hooks | APIs Exposed | Moddable Things |
|----------|-------|-------------|-----------------|
| 🎵 Music | 7 | 8 | Replace tracks, custom music, fade effects, volume |
| 🌄 Background | 3 | 6 | Custom sky, parallax layers, animated/GIF backgrounds |
| 🗺️ Scene | 1 (new) | 4 | Custom scenes, scene replacement, scene events |
| 🚪 Portal | 1 (new) | 3 | Custom portals, teleport API, world map |
| ❤️ HUD/Stats | 1 | 8 | Player stats, HUD mods, stat modifiers |
| 🖼️ GUI Stack | 8 | 6 | Custom UI elements, theme colors, menu mods |
| 💀 Death | 1 | 2 | Custom death screen, respawn behavior |
| ⌨️ Text Input | 4 | 2 | Custom text handlers, input validation |
| 📜 Lua | 4 | 5 | Custom scripts, mod loader, console commands |
| 📋 Menu | 2 | 3 | Custom menu entries, options screen |
| 🔊 Audio | (via Music) | 2 | Volume control, audio effects |
| 📁 VFS | 1 (disabled) | 3 | File redirection, asset replacement |
| **Total** | **36** | **47+** | |

---

## Detailed API per Hook

### 🎵 MUSIC (7 hooks → 8 APIs)

| Hook | Offset | Public API | mod.json Config |
|------|--------|-----------|-----------------|
| `PlayMusicWithName` | 0x4811a0 | **`music.replace`** — Replace any track | `{"replace": {"forest": "custom.mp3"}}` |
| `PlayMusicWithName` | 0x4811a0 | **`music.play`** — Play custom music on demand | `{"play": "my_track.mp3"}` |
| `MusicPlayer_FadeIn` | 0x4814a8 | **`music.fadeIn`** — Custom fade-in duration | `{"fadeIn": 2.0}` |
| `MusicPlayer_FadeOut` | 0x4815d8 | **`music.fadeOut`** — Custom fade-out duration | `{"fadeOut": 1.5}` |
| `MusicPlayer_Update` | 0x482090 | **`music.crossfade`** — Crossfade between tracks | `{"crossfade": true}` |
| `AudioSystem_SetMusicVolume` | 0x47f5f0 | **`music.volume`** — Override volume | `{"volume": 0.8}` |
| `MusicPlayer_SetEnabled` | 0x481e88 | **`music.enable`** — Enable/disable music system | `{"enabled": true}` |
| `MusicPlayer_SetSuspended` | 0x481fc0 | **`music.background`** — Control background audio | `{"suspendOnPause": false}` |

### 🌄 BACKGROUND (3 hooks → 6 APIs)

| Hook | Offset | Public API | mod.json Config |
|------|--------|-----------|-----------------|
| `BackgroundComponent_Draw` | 0x21ded4 | **`background.replace`** — Replace sky/parallax | `{"replace": {"plains": "custom_sky.png"}}` |
| `BackgroundComponent_Draw` | 0x21ded4 | **`background.layers`** — Multi-layer parallax | `{"layers": ["far.png", "mid.png", "near.png"]}` |
| `BackgroundComponent_Draw` | 0x21ded4 | **`background.animated`** — Animated/GIF backgrounds | `{"animated": "sky_anim.gif"}` |
| `RotatingBackgroundComponent_Draw` | 0x2b6760 | **`background.rotation`** — Custom rotation speed/axis | `{"rotation": {"speed": 0.5}}` |
| `RotatingBackgroundComponent_Update` | 0x2b66f8 | **`background.timeOfDay`** — Day/night cycle | `{"timeOfDay": {"cycle": 300}}` |
| All background hooks | - | **`background.color`** — Tint/color overlay | `{"tint": [0.8, 0.9, 1.0]}` |

### 🗺️ SCENE (1 new hook → 4 APIs)

| Hook | Offset | Public API | mod.json Config |
|------|--------|-----------|-----------------|
| `Scene::LoadFromFile` | 0x563478 | **`scene.replace`** — Replace any vanilla scene | `{"replace": {"plains1": "mod_plains.scene"}}` |
| `Scene::LoadFromFile` | 0x563478 | **`scene.add`** — Add custom scenes | `{"add": ["custom_dungeon.scene"]}` |
| `Scene::LoadFromFile` | 0x563478 | **`scene.onLoad`** — Callback when scene loads | Lua: `Mod.onSceneLoad(name, fn)` |
| `Scene::LoadFromFile` | 0x563478 | **`scene.list`** — Query available scenes | Lua: `Mod.getSceneList()` |

### 🚪 PORTAL (1 new hook → 3 APIs)

| Hook | Offset | Public API | mod.json Config |
|------|--------|-----------|-----------------|
| `PortalComponent::Enter` | 0x32FA80 | **`portal.redirect`** — Redirect portal destination | `{"redirect": {"plains1→caves": "custom_cave"}}` |
| `PortalComponent::Enter` | 0x32FA80 | **`portal.onEnter`** — Callback on portal entry | Lua: `Mod.onPortalEnter(fn)` |
| `HandleDidEnterPortalGameEvent` | 0x458A00 | **`portal.teleport`** — Teleport to any scene | Lua: `Mod.teleport("scene", "spawn")` |

### ❤️ HUD / PLAYER STATS (1 hook → 8 APIs)

| Hook | Offset | Public API | Read/Write |
|------|--------|-----------|------------|
| `GameSceneView_Update` | 0x34ed2c | **`player.hp`** — Current health | Read |
| `GameSceneView_Update` | 0x34ed2c | **`player.maxHp`** — Max health | Read |
| `GameSceneView_Update` | 0x34ed2c | **`player.mana`** — Current mana | Read |
| `GameSceneView_Update` | 0x34ed2c | **`player.coins`** — Coin count | Read |
| `GameSceneView_Update` | 0x34ed2c | **`player.xp`** — Experience points | Read |
| `GameSceneView_Update` | 0x34ed2c | **`player.level`** — Experience level | Read |
| `GameSceneView_Update` | 0x34ed2c | **`player.attack`** — Attack level | Read |
| `GameSceneView_Update` | 0x34ed2c | **`player.position`** — GameState pointer (advanced) | Read |

### 🖼️ GUI STACK (8 hooks → 6 APIs)

| Hook | Offset | Public API | mod.json Config |
|------|--------|-----------|-----------------|
| `GUIWindow_DrawRect` | 0x4a28bc | **`gui.menuDetect`** — Know when menus are open | Read-only flag |
| `GUIButton_DrawRect` | 0x49565c | **`gui.buttonStyle`** — Custom button colors/shapes | `{"buttons": {"color": "#ff5566"}}` |
| `GUILabel_DrawRect` | 0x497aa0 | **`gui.labelStyle`** — Custom label fonts/colors | `{"labels": {"color": "#ffffff"}}` |
| `GUISlider_DrawRect` | 0x49cd40 | **`gui.sliderStyle`** — Custom slider appearance | `{"sliders": {"color": "#44aaff"}}` |
| `GUIAlertView_DrawRect` | 0x491b54 | **`gui.alertStyle`** — Custom alert dialog appearance | `{"alerts": {"bg": "#333333"}}` |
| All GUI hooks | - | **`gui.theme`** — Complete UI theme override | `{"theme": "dark"}` |

### 💀 DEATH SYSTEM (1 hook → 2 APIs)

| Hook | Offset | Public API | mod.json Config |
|------|--------|-----------|-----------------|
| `GameOverVC_ShowAdMaybe` | 0x347efc | **`death.behavior`** — Custom death handling | `{"death": "instant_respawn"}` |
| `GameOverVC_ShowAdMaybe` | 0x347efc | **`death.onDeath`** — Death callback | Lua: `Mod.onPlayerDeath(fn)` |

### ⌨️ TEXT INPUT (4 hooks → 2 APIs)

| Hook | Offset | Public API |
|------|--------|-----------|
| `StartTextInput*` + `StopTextInput*` | 0x4792ac, 0x4793dc | **`input.textField`** — Custom text input handling |
| `textInputDidChange` + `textInputDidFinish` | 0x4790dc, 0x479290 | **`input.onTextChange`** — Text input callbacks |

### 📜 LUA SCRIPTING (4 hooks → 5 APIs)

| Hook | Offset | Public API |
|------|--------|-----------|
| `ProgramState_Execute` | dynamic | **`lua.execute`** — Run custom Lua before each pcall |
| `ProgramState_Resume` | dynamic | **`lua.onResume`** — Hook coroutine resumption |
| `lua_call_safe` | late | **`lua.safeCall`** — Protected function calls |
| `lua_resume_safe` | late | **`lua.safeResume`** — Protected coroutine resume |
| Mini API (existing) | - | **`lua.modLoader`** — Load mod .lua files via Mini.* table |

### 📋 MENU (2 hooks → 3 APIs)

| Hook | Offset | Public API | mod.json Config |
|------|--------|-----------|-----------------|
| `NewMenuView_DrawRect` | 0x42bae4 | **`menu.mainMenu`** — Custom main menu elements | `{"mainMenu": {"addButton": "Mods"}}` |
| `MainMenuVC_DidOpenShop` | 0x36f394 | **`menu.options`** — Custom options screen | `{"options": {"addTab": "Mod Settings"}}` |
| Both | - | **`menu.onOpen`** — Callback when menu opens | Lua: `Mod.onMenuOpen(fn)` |

### 📁 VFS / FILE SYSTEM (1 disabled hook → 3 APIs)

| Hook | Offset | Public API | mod.json Config |
|------|--------|-----------|-----------------|
| `FileExistsAtPath` | 0x5B429C | **`vfs.redirect`** — Redirect file lookups | `{"redirect": {"old.png": "new.png"}}` |
| `FileExistsAtPath` | 0x5B429C | **`vfs.overlay`** — Overlay mod files on vanilla | `{"overlay": "mods/my_mod/assets/"}` |
| `NewByteBufferFromFile` | 0x58904C | **`vfs.intercept`** — Intercept file reads | Advanced Lua API |

---

## API Access Levels

| Level | Access | Example |
|-------|--------|---------|
| **JSON** | Config-only, no code needed | `music.replace`, `background.replace`, `scene.replace` |
| **Lua** | Script-based, callbacks | `scene.onLoad`, `portal.onEnter`, `death.onDeath` |
| **Advanced** | C API for SRE plugin devs | `vfs.intercept`, `gui.theme` |

## Priority for Implementation

### Phase 1 — Easy wins (JSON-only, no new hooks needed)
1. `music.replace` — swap music tracks
2. `background.replace` — swap background textures
3. `scene.replace` — swap scene files (needs Scene::LoadFromFile hook)
4. `gui.theme` — color overrides

### Phase 2 — Lua callbacks (extend Mini API)
5. `scene.onLoad` — scene load events
6. `portal.onEnter` — portal events
7. `death.onDeath` — death events
8. `player.*` — stat queries in Lua

### Phase 3 — Advanced
9. `vfs.overlay` — full file system overlay
10. `background.layers` — multi-layer parallax
11. `menu.options` — custom options entries
12. `portal.teleport` — programmatic scene changes
