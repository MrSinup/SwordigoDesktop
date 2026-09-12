# SpriteComponent

## Summary

`Caver::SpriteComponent` renders a 2D sprite on a `SceneObject`: it owns a
`shared_ptr<Sprite>` (allocated in the ctor, 0x58 bytes), a texture-name
string used to re-resolve the texture, and the sprite's UV rectangle cache.
`SetTexture` is the hot path the editor and scripts use to change visuals; the
component then refreshes the object's bounds so the collision grid stays in
sync.

Header: none yet — see `Component.md` for the shared base.

## Struct layout

Verified from the ARM64 v1.4.13 ctor (`0x343EF4`), `LoadFromProtobufMessage`
(`0x3441EC`) and `SetTexture` (`0x344160`). 64-bit ABI.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x68 | 24 | `std::string` | `textureName` | **Verified:** `LoadFromProtobufMessage` copies proto field 1 here (+0x68); `SetTexture` copies the texture's name (tex+0x40) here. |
| 0x80 | 16 | `shared_ptr<Sprite>` | `sprite` | **Verified:** ctor allocates a 0x58-byte `Sprite`, `boost::shared_ptr<Sprite>::reset` at +0x80. `SetTexture` uses `*(sprite)+...` for the UV cache. |
| 0x90 | 16 | `Rectangle` | `spriteUV` | **Verified:** `SetTexture` writes 16 bytes at +0x90 from `sprite+0x08` (the sprite's UV rect) and calls `Component::UpdateObjectBounds`. |
| 0xA0 | 4 | — | (end) | Total ≈ 0xA4. (ctor zeroes OWORDs at 0x68/0x78 and at 0x90-ish via `this+0x91`). |

Note: the ctor's `_OWORD` zero at +0x91 is an IDA misalignment artifact; the
real layout is `textureName` 0x68–0x7F, `sprite` 0x80–0x8F, `spriteUV`
0x90–0x9F.

## Vtable (at 0x61B540)

| Slot | Offset | Target | Notes |
|---|---|---|---|
| 0 | +0x00 | `~SpriteComponent()` (0x344A60) | Incomplete-object dtor. |
| 1 | +0x08 | `~SpriteComponent()` (0x344B3C) | Deleting dtor. |
| 2 | +0x10 | `Clone()` (0x344C14) | — |
| 4 | +0x20 | `LoadFromProtobufMessage` (0x3441EC) | — |
| 5 | +0x28 | `SaveToProtobufMessage` (0x344234) | — |
| 6 | +0x30 | `ShouldSave()` (0x344C80) | — |
| 8 | +0x40 | `Prepare()` (0x3442E4) | — |
| 14–18 | +0x70.. | IBindable accessors (0x344404–0x3447AC) | GetBindings/Value/SetValue; `PerformBindedAction` inherited. |
| 25 | +0xC8 | `Update(float)` — inherited (base) | — |
| 27 | +0xD8 | `HasBounds()` override (0x3449D4?) | Sprite has bounds. |
| 29+ | +0xE8.. | bounds/WorldMatrix/Draw virtuals | `Draw` override at 0x344924. |

## Exported functions

### SpriteComponent::SetTexture(intrusive_ptr<Texture> const&)
- Mangled: `_ZN5Caver15SpriteComponent10SetTextureERKN5boost13intrusive_ptrINS_7TextureEEE` (0x344160).
- Behavior: copies `texture->name` (texture+0x40) into `textureName` (+0x68); calls `Sprite::InitWithTexture(sprite, tex, rect)`; refreshes `spriteUV` (+0x90) from `sprite+0x08`; `Component::UpdateObjectBounds`.
- Confidence: **verified**.

### SpriteComponent::LoadFromProtobufMessage
- Mangled: `_ZN5Caver15SpriteComponent23LoadFromProtobufMessageERKNS_5Proto9ComponentE` (0x3441EC).
- Behavior: base load; copies the proto `textureName` string into +0x68 (the sprite itself is built during `Prepare`).
- Confidence: **verified**.

### SpriteComponent::Draw
- Mangled: `_ZN5Caver15SpriteComponent4DrawEPNS_16RenderingContextERKNS_7Matrix4E` (0x344924).
- Behavior: renders the sprite with the object's world matrix; not read in full this pass.
- Confidence: **inferred** (symbol + callers).

## Open questions

- The exact `Sprite` layout (0x58 bytes; UV rect at +0x08, name at +0x40) is partially known from `SetTexture`/`TextureForName` reads; a full pass is a separate doc.
- `SetTexture`'s rect argument default (zeros) means the UV rect comes from the texture itself.

## Proposed SRE hooks

- `SpriteComponent_SetTexture` (via the resolved symbol) is the sanctioned visual-swap API — call it rather than mutating `sprite`/`textureName` directly.
- `SetValueForBindedProperty` on the "textureName" binding (outlet index from `GetBindings`) is the script-friendly equivalent.
- Reading `textureName` (+0x68) is safe for overlays that want to tag objects by sprite.