# Swordigo Caver Engine: Enums and Constants Specification

## 1. Overview
All enumeration types in the Caver Engine are strongly typed 32-bit signed or unsigned integers in C++ and stored as protobuf `varint` values in FileRift assets. This document specifies the exact numerical values, textual identifiers, and runtime behaviors.

---

## 2. Enumeration Catalog

### 2.1 `DamageType`
Specifies elemental and physical damage categories used by `DamageComponent`, `AttackComponent`, and `HealthComponent`.

| Value | Identifier | Description |
| :---: | :--- | :--- |
| `0` | `PHYSICAL` | Standard kinetic/melee strike (sword swings, monster collisions, physical projectiles). Reduced by physical armor defense. |
| `1` | `MAGIC` | Magic bolt, magic bomb explosion, or spell projectile. Triggers magical particle bursts and bypasses physical shielding. |
| `2` | `FIRE` | Thermal / incendiary damage (fire traps, fire breath, dragonkin projectiles). Triggers fire particle bursts and burning animations. |
| `3` | `COLD` | Frost / ice elemental damage (ice castle spikes, frost monsters). Triggers freeze slow effects. |
| `4` | `PURE` | Absolute damage bypassing all defenses and resistance calculations (used for bottomless pits and insta-kill hazard zones). |

---

### 2.2 `SpecialDamageType`
Secondary status effects applied alongside base damage.

| Value | Identifier | Description |
| :---: | :--- | :--- |
| `0` | `NONE` | Standard damage with no supplementary status effect. |
| `1` | `KNOCKBACK` | Applies kinetic directional impulse pushing the victim away from the damage origin. |
| `2` | `STUN` | Temporarily interrupts the victim's animation state and disables AI actions for a duration. |

---

### 2.3 `HEALTHTYPE`
Entity damage vulnerability and life simulation mode used by `HealthComponent`.

| Value | Identifier | Description |
| :---: | :--- | :--- |
| `0` | `NORMAL` | Standard entity: takes damage from valid opposing factions, displays health bar, dies when health reaches zero. |
| `1` | `INVULNERABLE` | Indestructible entity: registers collisions and triggers callbacks, but hitpoints cannot be depleted. |
| `2` | `GHOST` | Intangible entity: ignores standard damage checks and passes through physical attack hitboxes. |

---

### 2.4 `SpecialType`
Used for special combat calculations, bonuses, and critical hits.

| Value | Identifier | Description |
| :---: | :--- | :--- |
| `0` | `NONE` | Standard attack calculation. |
| `1` | `CRITICAL` | Enhanced damage calculation with critical visual/audio effect and screen camera rumble. |

---

### 2.5 `MeshType`
Geometry classification for procedural and static terrain meshes.

| Value | Identifier | Description |
| :---: | :--- | :--- |
| `0` | `COLLISION` | Invisible physical solid geometry used exclusively for collision detection. |
| `1` | `VISUAL` | Visible textured geometry that does not participate in physical collision testing. |
| `2` | `DYNAMIC` | Combined visual and physical mesh updated procedurally at runtime (e.g. collapsing platforms). |

---

### 2.6 `PassDirection`
One-way platform passage classification used by `PhysicsPlatformComponent`.

| Value | Identifier | Description |
| :---: | :--- | :--- |
| `0` | `NONE` | Solid impassable obstacle in all directions. |
| `1` | `TOP_DOWN` | Entities can jump through from below and stand on top. |
| `2` | `BOTTOM_UP` | Entities can fall through from above and block from below. |
| `3` | `BIDIRECTIONAL` | Permeable trigger surface detecting intersection without physical obstruction. |

---

### 2.7 `ItemType`
Inventory item classification used in player profiles and game data files (`.gdata`).

| Value | Identifier | Description |
| :---: | :--- | :--- |
| `0` | `WEAPON` | Primary melee equipment (e.g. Brass Sword, Broadsword, The Mageblade). |
| `1` | `ARMOR` | Defensive vestment reducing incoming physical damage. |
| `2` | `TRINKET` | Passive enhancement artifact (e.g. regenerative trinket, coin doubler). |
| `3` | `QUEST` | Key items, portal stones, dungeon keys, and story artifacts. |
| `4` | `CONSUMABLE` | Single-use restoratives or reagents (healing potions, mana replenishment). |

---

### 2.8 `MapNodeType`
World map zone and travel junction points.

| Value | Identifier | Description |
| :---: | :--- | :--- |
| `0` | `STANDARD` | Waypoint connector on the world map. |
| `1` | `PORTAL` | Fast-travel magical dimensional rift portal. |
| `2` | `TOWN` | Safe settlement area containing NPCs and healers (e.g. Florennum, Cairnwood). |
| `3` | `DUNGEON` | Hostile dungeon entrance or underground labyrinth. |
