# Device Capability and Graphics Quality Investigation Report

This document reports on the focused reverse-engineering investigation into whether Swordigo contains any automatic graphics-quality classification system.

VERDICT:
NO DEVICE QUALITY SYSTEM FOUND

---

### 1. Most Suspicious Finding

The game engine contains **no automatic device tiering or hardware capability checks**. It does not call `glGetString` for `GL_VENDOR` or `GL_RENDERER` to parse GPU names, nor does it query JNI/Android device info (like `Build.MODEL` or `DisplayMetrics`). 

Instead, graphics quality (which enables/disables stencil shadow volumes, dynamic lighting, glows, and shaders) is driven entirely by user-level preferences stored in the host preferences file (read via the JNI bridge method `getBooleanFromSP`). 

If the host environment does not set the default preference value for `"high_details"` to `"true"`, the preference defaults to `false` and the engine falls back to Low Details rendering mode.

---

### 2. Device/Graphics Classification Pipeline

```text
Host Preferences (prefs.ini)
  ↓ (default: "false" if missing)
JNI bridge: getBooleanFromSP("high_details")
  ↓ (returns 0 or 1)
GameOptions (constructed and loaded at boot)
  ↓ (stored at offset 0x50 in GameOptions instance)
Engine Components / Scene rendering
  ↓ (reads GameOptions::sharedOptions()->high_details)
High Details Rendering (Shading, shadow volumes, glows enabled)
```

---

### 3. OpenGL Capability Queries

| Query | Function | Result Storage | Threshold/Use | Graphics Effect |
| :--- | :--- | :--- | :--- | :--- |
| `GL_EXTENSIONS` (`0x1F03`) | `CPVRTglesExt::LoadExtensions` | `this` fields | Checks for `GL_OES_framebuffer_object` etc. | Used only to resolve GLES 1.1 extension pointers. |
| `GL_TEXTURE_BINDING_2D` (`0x8069`) | `Texture::Load` | `local_2c` | Saves bound texture state | Restores state after texture operations. |
| `GL_ARRAY_BUFFER_BINDING` (`0x8894`) | `VertexArrayObject::Draw` | `local_70` | Saves bound buffer state | Restores state after draw. |
| `GL_VIEWPORT` (`0xd31`) | `Scene::Draw` | `this + 0x250` | Viewport dimensions | Restores viewport. |

No queries exist for `GL_MAX_TEXTURE_SIZE` or other limits to scale down graphics quality.

---

### 4. Android/JNI Device Queries

| Java API | Native Function | Failure Default | Used By |
| :--- | :--- | :--- | :--- |
| `Native.getBooleanFromSP` | JNI Static Method | `false` | Loading detail/graphics settings from SP |
| `Native.getIntFromSP` | JNI Static Method | `0` | Loading audio volume, options, age checks |
| `Native.getPlatformConsentState` | JNI Static Method | `3` (OBTAINED) | Verifying compliance/gate |

No calls exist into `android/os/Build`, `ActivityManager`, `DisplayMetrics`, or memory checks.

---

### 5. Graphics Feature Flags

All feature flags are fields inside `GameOptions`, which is instantiated as a singleton at boot.

| Address/Offset | Temporary Name | Default | Writers | Readers | Effect |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `GameOptions + 0x50` | `high_details` | `0` | `LoadFromFile` / `defaultOptions` | Scene, ShadowVolumeComponent, GlowComponent | Enables shadow volumes, shading, and glows. |
| `GameOptions + 0x51` | `options_field_51` | `0` | `LoadFromFile` / `defaultOptions` | UI views / controls | Unknown controls layout flag. |
| `GameOptions + 0x52` | `options_field_52` | `0` | `LoadFromFile` / `defaultOptions` | UI views / controls | Unknown controls layout flag. |

---

### 6. `_2x` Asset Selection

Swordigo does not perform dynamic high-res/low-res asset suffix mapping (like `_2x` or `@2x`) in the Android ARM64 binary. Assets are loaded directly from the archive pack as-is. There is no code path checking screen dimensions or density to append `_2x` to file paths.

---

### 7. Fallback/Failure Behavior

Since the JNI bridge `pref_get` defaults to `"false"` for any missing key, any setup without a pre-populated `prefs.ini` causes the game to query `getBooleanFromSP("high_details")` &rarr; returns `0` &rarr; sets `GameOptions::high_details = 0` (Low Details). This silently disables all visual effect passes, glow capturing, and shadow volumes.

---

### 8. ARM64 vs ARMv7 Difference

Both binary ports are identical in their graphics quality logic: they both delegate the detail setting to the host environment preferences via `getBooleanFromSP`. No platform or arch-specific graphic quality downgrades exist.

---

### 9. Best Runtime Values to Trace

* `getBooleanFromSP("high_details")` (returned value)
* `getIntFromSP("detailLevel")` (returned value)
* `glGetString(GL_EXTENSIONS)` (to verify GLES extension list passed to guest)

---

### 10. Fastest Experiment

Force `pref_get("high_details", ...)` to always return `"true"` in `jni_bridge_arm64.cpp`. If glows/shadows are restored, this proves the preference fallback was the only blocker.
