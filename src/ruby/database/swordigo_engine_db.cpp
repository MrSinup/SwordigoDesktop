// ============================================================================
// swordigo_engine_db.cpp — Comprehensive Swordigo Engine Knowledge Base
// ============================================================================

#include "swordigo_engine_db.h"
#include <algorithm>
#include <sstream>

namespace ruby::database {

SwordigoEngineDB& SwordigoEngineDB::instance() {
    static SwordigoEngineDB s_instance;
    return s_instance;
}

SwordigoEngineDB::SwordigoEngineDB() {
    init_enums();
    init_components();
    init_lua_apis();
}

void SwordigoEngineDB::init_enums() {
    // 1. DamageType
    {
        EnumMeta e;
        e.name = "DamageType";
        e.description = "Primary damage classification evaluated by entity armor and resistance attributes.";
        e.values = {
            { 0, "PHYSICAL", "Standard melee and projectile impacts (reduced by PhysicalResistance)." },
            { 1, "MAGIC", "Spell effects like Magic Bolt and dimensional magic (reduced by MagicResistance)." },
            { 2, "FIRE", "Fire breath, lava pools, and burning hazards." },
            { 3, "COLD", "Ice cavern hazards and chilling monster attacks." },
            { 4, "PURE", "True damage ignoring armor and defense barriers." }
        };
        m_enums.push_back(e);
        m_enum_map[e.name] = m_enums.size() - 1;
    }

    // 2. SpecialDamageType
    {
        EnumMeta e;
        e.name = "SpecialDamageType";
        e.description = "Secondary status ailments and kinetic impulses applied alongside damage.";
        e.values = {
            { 0, "NONE", "Standard damage without extra status conditions." },
            { 1, "KNOCKBACK", "Applies high velocity impulse away from damage center." },
            { 2, "STUN", "Briefly disables monster AI and movement cycles." },
            { 3, "FREEZE", "Freezes target solid in a block of ice." },
            { 4, "BURN", "Damage-over-time burning effect." },
            { 5, "INSTANT_KILL", "Bypasses health pool to trigger immediate death animation." }
        };
        m_enums.push_back(e);
        m_enum_map[e.name] = m_enums.size() - 1;
    }

    // 3. HEALTHTYPE
    {
        EnumMeta e;
        e.name = "HEALTHTYPE";
        e.description = "Faction and visual style of the entity's health bar overlay.";
        e.values = {
            { 0, "HEALTH_TYPE_ENEMY", "Hostile enemy (displays red health bar above entity when damaged)." },
            { 1, "HEALTH_TYPE_FRIENDLY", "Friendly NPC or pet (no aggressive target lock)." }
        };
        m_enums.push_back(e);
        m_enum_map[e.name] = m_enums.size() - 1;
    }

    // 4. SpecialType (CollisionShapeComponent)
    {
        EnumMeta e;
        e.name = "SpecialType";
        e.description = "Special collision interaction behavior in Caver's collision pipeline.";
        e.values = {
            { 0, "SPECIAL_TYPE_NONE", "Normal solid collision geometry." },
            { 1, "SPECIAL_TYPE_PICKUP", "Item/powerup pickup trigger (auto-collected on overlap)." },
            { 2, "SPECIAL_TYPE_PORTAL", "Level portal warp transition trigger." },
            { 3, "SPECIAL_TYPE_COLLECTABLE", "Soul shard or coin collectible." },
            { 4, "SPECIAL_TYPE_USE", "Interactive object triggered via the Use action button." },
            { 5, "SPECIAL_TYPE_BLOCKS_DAMAGE", "Shield or obstacle that absorbs incoming damage rays." },
            { 6, "SPECIAL_TYPE_GRABBABLE", "Object that hero can grab, carry, or throw (e.g. pots, vases)." },
            { 7, "SPECIAL_TYPE_PUSHABLE", "Physics block that can be pushed across the floor." }
        };
        m_enums.push_back(e);
        m_enum_map[e.name] = m_enums.size() - 1;
    }

    // 5. MeshType (GroundMeshGeneratorComponent)
    {
        EnumMeta e;
        e.name = "MeshType";
        e.description = "Surface topology generator mode for procedural ground terrain.";
        e.values = {
            { 0, "MESH_TYPE_PLAIN", "Flat angular polygonal terrain extrusion." },
            { 1, "MESH_TYPE_ROUNDED_HAT", "Organic curved terrain cap with smooth rounded corners." }
        };
        m_enums.push_back(e);
        m_enum_map[e.name] = m_enums.size() - 1;
    }

    // 6. PassDirection
    {
        EnumMeta e;
        e.name = "PassDirection";
        e.description = "Allowed transit direction through a world portal.";
        e.values = {
            { 0, "FORWARDS_AND_BACKWARDS", "Bidirectional travel between connected maps." },
            { 1, "FORWARDS_ONLY", "One-way drop or passage forward." },
            { 2, "BACKWARDS_ONLY", "One-way return path." }
        };
        m_enums.push_back(e);
        m_enum_map[e.name] = m_enums.size() - 1;
    }

    // 7. ItemType
    {
        EnumMeta e;
        e.name = "ItemType";
        e.description = "Category of inventory item in GameData.";
        e.values = {
            { 1, "CONSUMABLE", "Potions and temporary health restoratives." },
            { 2, "WEAPON", "Equippable swords and daggers (modifies damage stats)." },
            { 3, "ARMOR", "Equippable protective armor and cloaks." },
            { 4, "TRINKET", "Magic amulets and passive stat gems." },
            { 5, "QUEST", "Plot keys, dimensional emblems, and story artifacts." }
        };
        m_enums.push_back(e);
        m_enum_map[e.name] = m_enums.size() - 1;
    }

    // 8. MapNodeType
    {
        EnumMeta e;
        e.name = "MapNodeType";
        e.description = "World map fast-travel and milestone node type.";
        e.values = {
            { 0, "TYPE_DEFAULT", "Standard level region on map." },
            { 1, "TYPE_TOWN", "Safe village or settlement hub with merchants." },
            { 2, "TYPE_WAYPOINT", "Fast-travel portal waypoint." },
            { 3, "TYPE_BOSS", "Boss battle arena milestone." }
        };
        m_enums.push_back(e);
        m_enum_map[e.name] = m_enums.size() - 1;
    }
}

void SwordigoEngineDB::init_components() {
    auto add_comp = [this](ComponentMeta c) {
        m_components.push_back(c);
        size_t idx = m_components.size() - 1;
        m_class_map[c.class_name] = idx;
        m_tag_map[c.tag_name] = idx;
    };

    // ── Model ──────────────────────────────────────────────────────────────
    {
        ComponentMeta c;
        c.class_name = "Model";
        c.tag_name = "ModelComponent";
        c.category = "Rendering";
        c.summary = "Binds and renders a 3D PowerVR POD mesh model with transforms, lighting, and materials.";
        c.description =
            "The ModelComponent attaches 3D geometry from resources/ (or level assets) to a SceneObject. "
            "It controls spatial orientation (X/Y rotation), origin offset, diffuse modulation, and shatter colors "
            "used when monsters disintegrate on death.";
        c.fields = {
            { "Name", "string", "bat_red", "Base filename of the 3D model asset (without .pod extension).", "" },
            { "YRotation", "float", "0.0", "Yaw rotation angle in radians around the Y axis.", "" },
            { "XRotation", "float", "0.0", "Pitch rotation angle in radians around the X axis.", "" },
            { "EmissionFactor", "float", "0.0", "Self-illumination factor (0.0 = unlit, 1.0 = fully emissive).", "" },
            { "Origin", "Vector3", "{0, 0, 0}", "Pivot point offset relative to object root.", "" },
            { "DiffuseColor", "FloatColor", "{1, 1, 1, 1}", "RGBA diffuse color tint multiplied with texture.", "" },
            { "ShatterColor", "FloatColor", "{0, 0, 0, 1}", "Color tint applied to shard fragments upon death.", "" },
            { "Transparent", "bool", "0", "Enables alpha blending mode for translucent materials.", "" }
        };
        c.example_scl =
            "Component{\n"
            "    ClassName : 'Model'\n"
            "    Identifier : 101\n"
            "    ModelComponent{\n"
            "        Name : 'bat_red'\n"
            "        YRotation : 1.57079637\n"
            "        EmissionFactor : 0.7\n"
            "        DiffuseColor{ R : 1, G : 1, B : 1, A : 1 }\n"
            "    }\n"
            "}";
        add_comp(c);
    }

    // ── Damage ─────────────────────────────────────────────────────────────
    {
        ComponentMeta c;
        c.class_name = "Damage";
        c.tag_name = "DamageComponent";
        c.category = "Combat";
        c.summary = "Defines damage output, scaling factors, and blockability for attacks and hazards.";
        c.description =
            "DamageComponent specifies how an attack, monster contact hitbox, or projectile affects targets. "
            "It configures min/max damage rolls, physical vs. magical scaling, and whether shields can block it.";
        c.fields = {
            { "MinDamage", "int", "1", "Minimum base damage inflicted per hit.", "" },
            { "MaxDamage", "int", "1", "Maximum base damage inflicted per hit.", "" },
            { "DamageType", "enum:DamageType", "0", "Primary damage type (Physical, Magic, Fire, Cold, Pure).", "DamageType" },
            { "SpecialDamageType", "enum:SpecialDamageType", "0", "Status effect (None, Knockback, Stun, Freeze, Burn).", "SpecialDamageType" },
            { "PhysicalDamageFactor", "float", "1.0", "Multiplier applied to physical character attack stat.", "" },
            { "MagicDamageFactor", "float", "0.0", "Multiplier applied to spell power / magic stat.", "" },
            { "IgnoreTargetImmunity", "bool", "0", "When enabled, pierces invincible frames or shields.", "" },
            { "CanBeBlocked", "bool", "1", "Whether a hero's raised shield can negate this attack.", "" },
            { "StandAlone", "bool", "0", "Damage entity operates independently of parent object lifecycle.", "" }
        };
        c.example_scl =
            "Component{\n"
            "    ClassName : 'Damage'\n"
            "    Identifier : 111\n"
            "    DamageComponent{\n"
            "        MinDamage : 12\n"
            "        MaxDamage : 18\n"
            "        DamageType : 0\n"
            "        CanBeBlocked : 1\n"
            "    }\n"
            "}";
        add_comp(c);
    }

    // ── Health ─────────────────────────────────────────────────────────────
    {
        ComponentMeta c;
        c.class_name = "Health";
        c.tag_name = "HealthComponent";
        c.category = "Combat";
        c.summary = "Manages hit points, damage mitigation, and health bar HUD positioning.";
        c.description =
            "HealthComponent tracks an entity's current and maximum hit points. When HP reaches zero, "
            "it notifies the entity controller to trigger death animation, item drops, and Lua callbacks.";
        c.fields = {
            { "MaxHealth", "int", "10", "Total hit points for this entity.", "" },
            { "HEALTHTYPE", "enum:HEALTHTYPE", "0", "Faction type: 0 = Enemy (red bar), 1 = Friendly.", "HEALTHTYPE" },
            { "BarOffset", "Vector3", "{0, 50, 0}", "3D offset in world units to anchor the overhead health bar.", "" }
        };
        c.example_scl =
            "Component{\n"
            "    ClassName : 'Health'\n"
            "    Identifier : 113\n"
            "    HealthComponent{\n"
            "        MaxHealth : 45\n"
            "        HEALTHTYPE : 0\n"
            "        BarOffset{ X : 0, Y : 60, Z : 0 }\n"
            "    }\n"
            "}";
        add_comp(c);
    }

    // ── MonsterEntity ──────────────────────────────────────────────────────
    {
        ComponentMeta c;
        c.class_name = "MonsterEntity";
        c.tag_name = "MonsterEntityComponent";
        c.category = "AI";
        c.summary = "Core monster AI controller with experience awards and lifecycle event hooks.";
        c.description =
            "MonsterEntityComponent provides base behavior for enemies: facing direction, physics state, "
            "experience points upon defeat, and Lua event hooks invoked on hurt or death.";
        c.fields = {
            { "GivesExperience", "bool", "1", "Awards player XP when defeated.", "" },
            { "DefaultDeathAnimation", "bool", "1", "Plays standard dissolve/shatter death sequence.", "" },
            { "OnKill", "Program", "$ $end", "Lua script executed when the monster is defeated.", "" },
            { "OnHurt", "Program", "$ $end", "Lua script executed whenever the monster receives damage.", "" }
        };
        c.lua_hooks = {
            { "OnKill", "local self, killer = ...;", "Invoked when HP reaches 0." },
            { "OnHurt", "local self, damage, attacker = ...;", "Invoked whenever damage is taken." }
        };
        c.example_scl =
            "Component{\n"
            "    ClassName : 'MonsterEntity'\n"
            "    Identifier : 107\n"
            "    MonsterEntityComponent{\n"
            "        GivesExperience : 1\n"
            "        OnKill{\n"
            "            String : $\n"
            "                local self = ...;\n"
            "                Game.SpawnItem('coin_gold', self:position());\n"
            "            $end\n"
            "        }\n"
            "    }\n"
            "}";
        add_comp(c);
    }

    // ── CollisionShape ─────────────────────────────────────────────────────
    {
        ComponentMeta c;
        c.class_name = "CollisionShape";
        c.tag_name = "CollisionShapeComponent";
        c.category = "Physics";
        c.summary = "Defines 2D/3D collision volumes (boxes, circles, polygons) and overlap triggers.";
        c.description =
            "CollisionShapeComponent provides spatial bounds for collision detection, attack hitboxes, "
            "and interactive trigger zones. It executes Lua scripts on overlap enter and exit.";
        c.fields = {
            { "Enabled", "bool", "1", "Whether collision checks are active.", "" },
            { "IsGround", "bool", "0", "Treat shape as walkable solid floor surface.", "" },
            { "Collides", "bool", "1", "Blocks other physics entities.", "" },
            { "ReceivesDamage", "bool", "0", "Can be struck by weapon and spell attacks.", "" },
            { "InflictsDamage", "bool", "0", "Deals damage on contact to opposing entities.", "" },
            { "MinDepth", "float", "-15.0", "Minimum Z depth boundary for collision testing.", "" },
            { "MaxDepth", "float", "15.0", "Maximum Z depth boundary for collision testing.", "" },
            { "SpecialType", "enum:SpecialType", "0", "Interaction role (Pickup, Portal, Collectable, Use, etc.).", "SpecialType" },
            { "Friction", "float", "0.5", "Surface friction coefficient for sliding physics.", "" },
            { "OnCollide", "Program", "$ $end", "Lua script executed on collision entry.", "" },
            { "OnCollisionEnd", "Program", "$ $end", "Lua script executed when collision ends.", "" },
            { "OnReceiveDamage", "Program", "$ $end", "Lua script executed when struck by damage.", "" }
        };
        c.lua_hooks = {
            { "OnCollide", "local self, target = ...;", "Fired on first touch." },
            { "OnCollisionEnd", "local self, target = ...;", "Fired on separation." },
            { "OnReceiveDamage", "local self, damage, attacker = ...;", "Fired when damaged." }
        };
        c.example_scl =
            "Component{\n"
            "    ClassName : 'CollisionShape'\n"
            "    Identifier : 101\n"
            "    ShapeComponent{\n"
            "        Rectangle{ X : -50, Y : -300, Width : 100, Height : 650 }\n"
            "    }\n"
            "    CollisionShapeComponent{\n"
            "        OnCollide{\n"
            "            String : $\n"
            "                local self, target = ...;\n"
            "                if target:identifier() == 'hero' then Camera.ResetFocus(); end\n"
            "            $end\n"
            "        }\n"
            "    }\n"
            "}";
        add_comp(c);
    }

    // ── DoorController ─────────────────────────────────────────────────────
    {
        ComponentMeta c;
        c.class_name = "DoorController";
        c.tag_name = "DoorControllerComponent";
        c.category = "Logic";
        c.summary = "Controls animated dungeon doors, lock states, keys, and room transitions.";
        c.description =
            "DoorControllerComponent manages door mechanisms. It handles open/close animations, "
            "locking logic, key requirements, and triggers connected level transitions.";
        c.fields = {
            { "Open", "bool", "0", "Initial state of the door (open or shut).", "" },
            { "KeyName", "string", "", "Required inventory key item to unlock.", "" },
            { "AutoClose", "bool", "0", "Closes automatically after player passes through.", "" },
            { "RequiresUpdate", "bool", "1", "Runs per-frame state machine update.", "" }
        };
        c.example_scl =
            "Component{\n"
            "    ClassName : 'DoorController'\n"
            "    Identifier : 105\n"
            "    DoorControllerComponent{\n"
            "        Open : 0\n"
            "        KeyName : 'brass_key'\n"
            "    }\n"
            "}";
        add_comp(c);
    }

    // ── ElevatorController ─────────────────────────────────────────────────
    {
        ComponentMeta c;
        c.class_name = "ElevatorController";
        c.tag_name = "ElevatorControllerComponent";
        c.category = "Logic";
        c.summary = "Moves platforms and elevators along waypoint paths or linear oscillations.";
        c.description =
            "ElevatorControllerComponent drives moving platforms, lifts, and swinging obstacles. "
            "It updates position along predefined waypoints and carries player physics seamlessly.";
        c.fields = {
            { "Speed", "float", "2.0", "Platform traversal speed in world units per second.", "" },
            { "WaitTime", "float", "1.0", "Pause time in seconds at each end of the movement path.", "" },
            { "Activated", "bool", "1", "Starts moving immediately on level load.", "" }
        };
        add_comp(c);
    }

    // ── KeyframeAnimation ──────────────────────────────────────────────────
    {
        ComponentMeta c;
        c.class_name = "KeyframeAnimation";
        c.tag_name = "KeyframeAnimationComponent";
        c.category = "Animation";
        c.summary = "Plays skeletal bone animations on an attached 3D ModelComponent.";
        c.description =
            "Controls animation clip playback from the POD model file, including speed scaling, "
            "looping modes, and frame synchronization.";
        c.fields = {
            { "ModelId", "int", "101", "Identifier of the target ModelComponent.", "" },
            { "Name", "string", "bat_fly", "Name of the animation track.", "" },
            { "Repeating", "bool", "1", "Loops animation continuously.", "" },
            { "SpeedMultiplier", "float", "1.0", "Playback speed multiplier.", "" },
            { "Running", "bool", "1", "Active state upon level load.", "" }
        };
        add_comp(c);
    }

    // ── CharController ─────────────────────────────────────────────────────
    {
        ComponentMeta c;
        c.class_name = "CharController";
        c.tag_name = "CharControllerComponent";
        c.category = "Physics";
        c.summary = "2.5D character movement physics, jumping, ground raycasting, and velocities.";
        c.description =
            "CharControllerComponent implements the platforming physics for the hero and humanoid monsters, "
            "handling acceleration, maximum speed, jump impulse, wall friction, and gravity curves.";
        c.fields = {
            { "MaxSpeed", "float", "8.0", "Maximum horizontal run velocity.", "" },
            { "JumpVelocity", "float", "14.0", "Initial upward velocity impulse on jump.", "" },
            { "AirAcceleration", "float", "20.0", "Horizontal control responsive factor while in mid-air.", "" }
        };
        add_comp(c);
    }

    // ── Light ──────────────────────────────────────────────────────────────
    {
        ComponentMeta c;
        c.class_name = "Light";
        c.tag_name = "LightComponent";
        c.category = "Rendering";
        c.summary = "Dynamic point light source with attenuation radius and color pulsing.";
        c.description =
            "LightComponent creates dynamic point lights that illuminate nearby meshes and characters in real time.";
        c.fields = {
            { "Radius", "float", "150.0", "Effective light reach radius.", "" },
            { "Color", "FloatColor", "{1, 0.9, 0.7, 1}", "Light emission tint.", "" },
            { "Intensity", "float", "1.0", "Brightness multiplier.", "" }
        };
        add_comp(c);
    }

    // ── ParticleEmitter ────────────────────────────────────────────────────
    {
        ComponentMeta c;
        c.class_name = "ParticleEmitter";
        c.tag_name = "ParticleEmitterComponent";
        c.category = "FX";
        c.summary = "Configures particle bursts, fountains, smoke, and magical sparkle effects.";
        c.description =
            "ParticleEmitterComponent spawns animated particle sprites with randomized velocities, "
            "color gradients, lifetimes, and gravity scaling.";
        c.fields = {
            { "ParticleType", "string", "fire_sparks", "Particle preset configuration name.", "" },
            { "Rate", "float", "30.0", "Particles emitted per second.", "" },
            { "Active", "bool", "1", "Emits particles continuously.", "" }
        };
        add_comp(c);
    }

    // ── Portal ─────────────────────────────────────────────────────────────
    {
        ComponentMeta c;
        c.class_name = "Portal";
        c.tag_name = "PortalComponent";
        c.category = "World";
        c.summary = "Level warp threshold connecting different level scenes.";
        c.description =
            "PortalComponent links this trigger zone to a destination scene file and target spawn point identifier.";
        c.fields = {
            { "DestinationLevel", "string", "caves_01", "Target .scene file to load.", "" },
            { "DestinationSpawnPoint", "string", "spawn_entrance", "Target SpawnPointComponent identifier.", "" }
        };
        add_comp(c);
    }

    // ── SoundEffect ────────────────────────────────────────────────────────
    {
        ComponentMeta c;
        c.class_name = "SoundEffect";
        c.tag_name = "SoundEffectComponent";
        c.category = "Audio";
        c.summary = "Spatial audio emitter triggered by events or proximity.";
        c.description =
            "SoundEffectComponent plays localized audio with distance falloff based on player camera position.";
        c.fields = {
            { "SoundName", "string", "door_open", "Audio asset filename in sounds/ library.", "" },
            { "Volume", "float", "1.0", "Playback volume (0.0 to 1.0).", "" },
            { "MinInterval", "float", "0.2", "Cooldown between re-triggers.", "" }
        };
        add_comp(c);
    }

    // ── GroundMeshGenerator ────────────────────────────────────────────────
    {
        ComponentMeta c;
        c.class_name = "GroundMeshGenerator";
        c.tag_name = "GroundMeshGeneratorComponent";
        c.category = "World";
        c.summary = "Generates 3D ground geometry from a 2D GroundPolygon boundary.";
        c.description =
            "GroundMeshGeneratorComponent converts a 2D contour polygon into a textured 3D ground block "
            "with front faces, surface caps, and bevels.";
        c.fields = {
            { "GroundPolygonId", "int", "101", "Identifier of the source GroundPolygonComponent.", "" },
            { "TargetMeshId", "int", "102", "Identifier of the output GroundMeshComponent.", "" },
            { "MeshType", "enum:MeshType", "0", "Extrusion style (0: Plain, 1: Rounded Hat).", "MeshType" },
            { "SurfaceWidth", "float", "12.0", "Thickness of the top walkable surface cap.", "" },
            { "HatHeight", "float", "8.0", "Height curve for rounded hat corners.", "" }
        };
        add_comp(c);
    }

    // ── ItemDrop ───────────────────────────────────────────────────────────
    {
        ComponentMeta c;
        c.class_name = "ItemDrop";
        c.tag_name = "ItemDropComponent";
        c.category = "Combat";
        c.summary = "Controls loot dropped when this entity or breakable container is destroyed.";
        c.description =
            "ItemDropComponent rolls against drop probability tables to spawn soul shards, heart restoratives, "
            "or inventory items upon death.";
        c.fields = {
            { "DropChance", "float", "0.75", "Probability (0.0 to 1.0) of spawning an item.", "" },
            { "ItemTemplate", "string", "pickup_heart", "Template identifier of item to spawn.", "" }
        };
        add_comp(c);
    }
}

void SwordigoEngineDB::init_lua_apis() {
    auto add_mod = [this](LuaModuleMeta m) {
        m_lua_modules.push_back(m);
        m_module_map[m.module_name] = m_lua_modules.size() - 1;
    };

    // ── AnimationController ────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "AnimationController";
        m.description = "Controls skeletal animation playback, transitions, and completion timings.";
        m.functions = {
            { "BlendToAnimation", "AnimationController.BlendToAnimation(obj, animation, time)", "Blend 'obj' smoothly from the current animation to 'animation' over 'time' seconds.", "void", { {"obj", "SceneObject"}, {"animation", "number animation ID"}, {"time", "number (optional duration in seconds)"} } },
            { "TimeToCompletion", "AnimationController.TimeToCompletion(obj, animation_identifier)", "*Not yet documented*", "number", { {"obj", "SceneObject"}, {"animation_identifier", "number animation ID"} } }
        };
        add_mod(m);
    }

    // ── Camera ─────────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Camera";
        m.description = "Controls 2.5D camera focus, cinematic tracking, smoothing, and screen rumble.";
        m.functions = {
            { "Rumble", "Camera.Rumble()", "Make the screen shake.", "void", {} },
            { "FocusAtShape", "Camera.FocusAtShape(obj, rect)", "Focus the camera at 'obj' position, making sure 'rect' is visible on the screen.", "void", { {"obj", "SceneObject"}, {"rect", "Rectangle (optional)"} } },
            { "FocusAtPoint", "Camera.FocusAtPoint(point, rect)", "Focus the camera at 'point', making sure 'rect' is visible on the screen.", "void", { {"point", "Vector3"}, {"rect", "Rectangle (optional)"} } },
            { "JumpToFocus", "Camera.JumpToFocus()", "Make the camera immediately move to the currently focused position, without any smoothing.", "void", {} },
            { "ResetFocus", "Camera.ResetFocus()", "Make the camera go back to following Hiro.", "void", {} },
            { "IsPointVisible", "Camera.IsPointVisible(point)", "Test if 'point' is visible on the screen.", "boolean", { {"point", "Vector3"} } },
            { "FollowShape", "Camera.FollowShape(obj, rect)", "Make the camera keep focusing on 'obj' as it moves, making sure 'rect' is visible on the screen.", "void", { {"obj", "SceneObject"}, {"rect", "Rectangle (optional)"} } },
            { "IsObjectVisible", "Camera.IsObjectVisible(obj)", "*Not yet documented*", "boolean", { {"obj", "SceneObject"} } },
            { "FollowObject", "Camera.FollowObject(obj)", "*Not yet documented*", "void", { {"obj", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── Character ──────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Character";
        m.description = "Player progression, quest state, story flags, coin balances, and inventory items.";
        m.functions = {
            { "AddItem", "Character.AddItem(itemname)", "Add an item from the inventory, unless it is a unique item.", "void", { {"itemname", "string"} } },
            { "RemoveItem", "Character.RemoveItem(itemname)", "Remove an item, if it is in the inventory.", "void", { {"itemname", "string"} } },
            { "HasItem", "Character.HasItem(itemname)", "Test if an item is in the inventory.", "boolean", { {"itemname", "string"} } },
            { "ItemCount", "Character.ItemCount(itemname)", "Get the number of items in the inventory.", "number", { {"itemname", "string"} } },
            { "AddSkill", "Character.AddSkill(skillname)", "Add a magic spell.", "void", { {"skillname", "string"} } },
            { "HasSkill", "Character.HasSkill(skillname)", "Test if Hiro has a magic spell.", "boolean", { {"skillname", "string"} } },
            { "SetNumCoins", "Character.SetNumCoins(count)", "Set the number of coins Hiro has to 'count'.", "void", { {"count", "number"} } },
            { "NumCoins", "Character.NumCoins()", "Get the number of coins Hiro has.", "number", {} },
            { "HasQuest", "Character.HasQuest(questname)", "Test if a quest has been given.", "boolean", { {"questname", "string"} } },
            { "IsQuestInProgress", "Character.IsQuestInProgress(questname)", "Test if a quest has been given, but has not been completed.", "boolean", { {"questname", "string"} } },
            { "IsQuestCompleted", "Character.IsQuestCompleted(questname)", "Test if a quest has been completed.", "boolean", { {"questname", "string"} } },
            { "AddQuest", "Character.AddQuest(questname)", "Give a quest.", "void", { {"questname", "string"} } },
            { "SetQuestCompleted", "Character.SetQuestCompleted(questname)", "Mark that a quest has been completed.", "void", { {"questname", "string"} } },
            { "AddFlag", "Character.AddFlag(flagname)", "Add a flag to the save.", "void", { {"flagname", "string"} } },
            { "HasFlag", "Character.HasFlag(flagname)", "Test if a flag has been set in this save.", "boolean", { {"flagname", "string"} } },
            { "AddSceneFlag", "Character.AddSceneFlag(sceneflagname)", "Add a flag that applies only to the current level.", "void", { {"sceneflagname", "string"} } },
            { "SetSceneFlag", "Character.SetSceneFlag(sceneflagname)", "Add a flag that applies only to the current level.", "void", { {"sceneflagname", "string"} } },
            { "HasSceneFlag", "Character.HasSceneFlag(sceneflagname)", "Test if a flag has been set in this scene.", "boolean", { {"sceneflagname", "string"} } },
            { "RegisterTreasure", "Character.RegisterTreasure(obj)", "*Not yet documented*", "void", { {"obj", "SceneObject"} } },
            { "RegisterTreasureCollection", "Character.RegisterTreasureCollection(obj)", "*Not yet documented*", "void", { {"obj", "SceneObject"} } },
            { "AddQuestText", "Character.AddQuestText(questname, text)", "Add 'text' to the quest log for quest 'questname'.", "void", { {"questname", "string"}, {"text", "string"} } }
        };
        add_mod(m);
    }

    // ── CharController ─────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "CharController";
        m.description = "Controls Hiro character actions, weapon visibility, and carrying/dropping objects.";
        m.functions = {
            { "SetWeaponsHidden", "CharController.SetWeaponsHidden(char, hidden)", "Hide Hiro's weapon.", "void", { {"char", "SceneObject"}, {"hidden", "boolean"} } },
            { "PickupObject", "CharController.PickupObject(char, obj, x)", "Make Hiro pick up 'obj'. The purpose of 'x' is currently unknown.", "void", { {"char", "SceneObject"}, {"obj", "SceneObject"}, {"x", "boolean"} } },
            { "DropObject", "CharController.DropObject(char, x)", "Make Hiro drop 'obj'. The purpose of 'x' is currently unknown.", "void", { {"char", "SceneObject"}, {"x", "boolean (optional)"} } },
            { "CarriedObject", "CharController.CarriedObject(char)", "Get the SceneObject that Hiro is carrying.", "SceneObject", { {"char", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── SnappingMonsterController ──────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "SnappingMonsterController";
        m.description = "Controls snapping monster and carniplant attack actions.";
        m.functions = {
            { "Attack", "SnappingMonsterController.Attack(obj)", "Make a Snapper/Carniplant attack.", "void", { {"obj", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── CollectableItem ────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "CollectableItem";
        m.description = "Handles collectible items, pickup requirements, and persistent collection flags.";
        m.functions = {
            { "ItemName", "CollectableItem.ItemName(obj)", "Get the Name for a CollectableItem.", "string", { {"obj", "SceneObject"} } },
            { "ItemTitle", "CollectableItem.ItemTitle(obj)", "Get the Title for a CollectableItem.", "string", { {"obj", "SceneObject"} } },
            { "RequiresPickup", "CollectableItem.RequiresPickup(obj)", "Test if a CollectableItem should trigger a cutscene when picked up.", "boolean", { {"obj", "SceneObject"} } },
            { "IsItemCollected", "CollectableItem.IsItemCollected(obj)", "Test if a CollectableItem has been picked up.", "boolean", { {"obj", "SceneObject"} } },
            { "SetItemIdentifier", "CollectableItem.SetItemIdentifier(obj, ident)", "Set the identifier of a CollectableItem.", "void", { {"obj", "SceneObject"}, {"ident", "string"} } },
            { "ItemIdentifier", "CollectableItem.ItemIdentifier(obj)", "*Not yet documented*", "string", { {"obj", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── CollisionShape ─────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "CollisionShape";
        m.description = "Enables or disables individual collision shapes and triggers on SceneObjects.";
        m.functions = {
            { "SetEnabled", "CollisionShape.SetEnabled(obj, collision_shape_identifier, enabled)", "Set 'collision_shape_identifier' to 'enabled' for 'obj'.", "void", { {"obj", "SceneObject"}, {"collision_shape_identifier", "number"}, {"enabled", "boolean (optional)"} } },
            { "DisableAll", "CollisionShape.DisableAll(obj)", "Disable all CollisionShapes for 'obj'.", "void", { {"obj", "SceneObject"} } },
            { "IsEnabled", "CollisionShape.IsEnabled(obj, collision_shape_identifier)", "Test if 'collision_shape_identifier' is enabled for 'obj'.", "boolean", { {"obj", "SceneObject"}, {"collision_shape_identifier", "number"} } }
        };
        add_mod(m);
    }

    // ── Damage ─────────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Damage";
        m.description = "Toggles damage components and active strike state.";
        m.functions = {
            { "SetActive", "Damage.SetActive(obj, active)", "Enable or disable damage for obj.", "void", { {"obj", "SceneObject"}, {"active", "boolean"} } },
            { "IsActive", "Damage.IsActive(obj)", "*Not yet documented*", "boolean", { {"obj", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── DoorController ─────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "DoorController";
        m.description = "Script interface for dungeon doors, gates, and portcullises.";
        m.functions = {
            { "Open", "DoorController.Open(obj)", "Open a door.", "void", { {"obj", "SceneObject"} } },
            { "Close", "DoorController.Close(obj)", "Close a door.", "void", { {"obj", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── ElevatorController ─────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "ElevatorController";
        m.description = "Elevator and moving platform controller modes.";
        m.functions = {
            { "SetMode", "ElevatorController.SetMode(elevator, mode)", "*Not yet documented*", "void", { {"elevator", "SceneObject"}, {"mode", "string"} } }
        };
        add_mod(m);
    }

    // ── Entity ─────────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Entity";
        m.description = "Physics grounding, facing direction, and physics simulation toggling for SceneObjects.";
        m.functions = {
            { "IsOnGround", "Entity.IsOnGround(obj)", "Test if 'obj' is touching a GroundMesh.", "boolean", { {"obj", "SceneObject"} } },
            { "SetFacingDirection", "Entity.SetFacingDirection(obj, direction)", "Make an entity face left or right: -1 for left, 1 for right.", "void", { {"obj", "SceneObject"}, {"direction", "number"} } },
            { "GetFacingDirection", "Entity.GetFacingDirection(obj)", "Get the facing direction of 'obj'.", "number", { {"obj", "SceneObject"} } },
            { "SetPhysicsEnabled", "Entity.SetPhysicsEnabled(obj, enabled)", "Enable or disable gravity and physics for 'obj'.", "void", { {"obj", "SceneObject"}, {"enabled", "boolean"} } },
            { "GetPreviousGroundPosition", "Entity.GetPreviousGroundPosition(obj)", "Get previous ground position of an entity.", "Vector3", { {"obj", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── EntityController ───────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "EntityController";
        m.description = "Monster/NPC AI controller, movement speeds, actions, facing direction, and roaming bounds.";
        m.functions = {
            { "Target", "EntityController.Target(obj)", "Get the SceneObject that 'obj' is targeting.", "SceneObject", { {"obj", "SceneObject"} } },
            { "SetTarget", "EntityController.SetTarget(obj, target)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"target", "SceneObject"} } },
            { "Acceleration", "EntityController.Acceleration(obj)", "Get the acceleration (how fast an entity speeds up when beginning to move) for 'obj'.", "number", { {"obj", "SceneObject"} } },
            { "DefaultAcceleration", "EntityController.DefaultAcceleration(obj)", "*Not yet documented*", "number", { {"obj", "SceneObject"} } },
            { "IsIdle", "EntityController.IsIdle(obj)", "Test if 'obj' is idle (not walking or attacking).", "boolean", { {"obj", "SceneObject"} } },
            { "IdleTime", "EntityController.IdleTime(obj)", "*Not yet documented*", "number", { {"obj", "SceneObject"} } },
            { "PerformAction", "EntityController.PerformAction(obj, action)", "Make 'obj' perform 'action' (an EntityAction).", "void", { {"obj", "SceneObject"}, {"action", "number"} } },
            { "CancelAction", "EntityController.CancelAction(obj)", "*Not yet documented*; likely cancels the current PerformAction", "void", { {"obj", "SceneObject"} } },
            { "IsActionCancelled", "EntityController.IsActionCancelled(obj)", "*Not yet documented*", "boolean", { {"obj", "SceneObject"} } },
            { "SetFacingDirection", "EntityController.SetFacingDirection(obj, direction, time)", "Make an entity face left or right: -1 for left, 1 for right.", "void", { {"obj", "SceneObject"}, {"direction", "number"}, {"time", "number (optional)"} } },
            { "FacingDirection", "EntityController.FacingDirection(obj)", "*Not yet documented*", "number", { {"obj", "SceneObject"} } },
            { "SetAcceleration", "EntityController.SetAcceleration(obj, acceleration, x)", "Set the acceleration (how fast an entity speeds up when beginning to move) for 'obj'.", "void", { {"obj", "SceneObject"}, {"acceleration", "number"}, {"x", "boolean (optional)"} } },
            { "SetMoveSpeed", "EntityController.SetMoveSpeed(obj, move_speed, x)", "Set the max speed for 'obj' to 'move_speed'.", "void", { {"obj", "SceneObject"}, {"move_speed", "number"}, {"x", "boolean (optional)"} } },
            { "MoveSpeed", "EntityController.MoveSpeed(obj)", "*Not yet documented*", "number", { {"obj", "SceneObject"} } },
            { "DefaultMoveSpeed", "EntityController.DefaultMoveSpeed(obj)", "*Not yet documented*", "number", { {"obj", "SceneObject"} } },
            { "SetDefaultMoveSpeed", "EntityController.SetDefaultMoveSpeed(obj, move_speed)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"move_speed", "number"} } },
            { "MoveDirection", "EntityController.MoveDirection(obj)", "*Not yet documented*", "Vector3", { {"obj", "SceneObject"} } },
            { "SetMoveDirection", "EntityController.SetMoveDirection(obj, direction)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"direction", "Vector3"} } },
            { "SetMoveAnimation", "EntityController.SetMoveAnimation(obj, animation_identifier)", "Set the animation that should play when 'obj' moves to 'animation_identifier'.", "void", { {"obj", "SceneObject"}, {"animation_identifier", "number"} } },
            { "DefaultMoveAnimation", "EntityController.DefaultMoveAnimation(obj)", "*Not yet documented*", "number", { {"obj", "SceneObject"} } },
            { "SetMovementBehavior", "EntityController.SetMovementBehavior(obj, behaviour)", "Set MovementBehavior for 'obj' to 'behaviour'. Options: ('none' | 'follow' | 'fight')", "void", { {"obj", "SceneObject"}, {"behaviour", "string"} } },
            { "MovementBehavior", "EntityController.MovementBehavior(obj)", "*Not yet documented*", "string", { {"obj", "SceneObject"} } },
            { "StartSwing", "EntityController.StartSwing(obj, animation_identifier, time)", "Run animation 'animation_identifier' for 'obj' over 'time' seconds.", "void", { {"obj", "SceneObject"}, {"animation_identifier", "number"}, {"time", "number"} } },
            { "RoamBounds", "EntityController.RoamBounds(obj)", "*Not yet documented*", "Rectangle", { {"obj", "SceneObject"} } },
            { "HasRoamBounds", "EntityController.HasRoamBounds(obj)", "*Not yet documented*", "boolean", { {"obj", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── Game ───────────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Game";
        m.description = "Global gameplay runtime: notifications, camera fades, portals, shop popups, cinematic mode, and music.";
        m.functions = {
            { "ShowNotification", "Game.ShowNotification(notification)", "Show a notification on the screen.", "void", { {"notification", "string | number"} } },
            { "Flash", "Game.Flash()", "Flash the screen.", "void", {} },
            { "FadeOut", "Game.FadeOut(time)", "Fade the screen to black over 'time' seconds.", "void", { {"time", "number (optional)"} } },
            { "FadeIn", "Game.FadeIn(time)", "Fade the screen from black to colour over 'time' seconds.", "void", { {"time", "number (optional)"} } },
            { "SetCinematicMode", "Game.SetCinematicMode(enabled, animate)", "Turn cinematic mode on or off. If 'animate' is true, black borders slide smoothly.", "void", { {"enabled", "boolean"}, {"animate", "boolean"} } },
            { "EnterPortal", "Game.EnterPortal(scenename, portalname)", "Teleport to a 'scenename' and spawn in at 'portalname'.", "void", { {"scenename", "string"}, {"portalname", "string"} } },
            { "IncCounter", "Game.IncCounter(counter)", "Add one to the value of a counter. Counters store achievement status.", "void", { {"counter", "string"} } },
            { "SetDefaultMusicName", "Game.SetDefaultMusicName(musicname)", "*Not yet documented*", "void", { {"musicname", "string"} } },
            { "DefaultMusicName", "Game.DefaultMusicName()", "*Not yet documented*", "string", {} },
            { "TitleForItem", "Game.TitleForItem(itemname)", "Get the Title for the item that matches 'itemname'.", "string", { {"itemname", "string"} } },
            { "CurrentLevelName", "Game.CurrentLevelName()", "Get the name for the current level.", "string", {} },
            { "IsItemUnique", "Game.IsItemUnique(itemname)", "*Not yet documented*", "boolean", { {"itemname", "string"} } },
            { "ShowItemBuyPopup", "Game.ShowItemBuyPopup(itemname, itemprice)", "*Not yet documented*", "void", { {"itemname", "string"}, {"itemprice", "number (optional)"} } },
            { "ShowItemInfoPopup", "Game.ShowItemInfoPopup(itemname)", "*Not yet documented*", "void", { {"itemname", "string"} } },
            { "HideItemInfoPopup", "Game.HideItemInfoPopup()", "*Not yet documented*", "void", {} },
            { "GotoCredits", "Game.GotoCredits()", "*Not yet documented*; likely shows the end-of-game credits screen", "void", {} }
        };
        add_mod(m);
    }

    // ── GameController ─────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "GameController";
        m.description = "Global game controller update loop, falling handling, hero spawning, and view reset.";
        m.functions = {
            { "HandleFall", "GameController.HandleFall(obj)", "*Not yet documented*", "void", { {"obj", "SceneObject"} } },
            { "ResetView", "GameController.ResetView()", "*Not yet documented*", "void", {} },
            { "ApplyLevelUp", "GameController.ApplyLevelUp(obj)", "*Not yet documented*", "void", { {"obj", "SceneObject"} } },
            { "ShowMenu", "GameController.ShowMenu()", "*Not yet documented*", "void", {} },
            { "GameControlButtonUp", "GameController.GameControlButtonUp(button)", "*Not yet documented*", "void", { {"button", "number"} } },
            { "GameControlButtonDown", "GameController.GameControlButtonDown(button)", "*Not yet documented*", "void", { {"button", "number"} } },
            { "UnequipArmor", "GameController.UnequipArmor()", "*Not yet documented*", "void", {} },
            { "UpdateTarget", "GameController.UpdateTarget()", "*Not yet documented*", "void", {} },
            { "Update", "GameController.Update(dt)", "*Not yet documented*; likely the per-frame update tick", "void", { {"dt", "number"} } },
            { "SaveGameState", "GameController.SaveGameState()", "*Not yet documented*", "void", {} },
            { "SpawnHeroAt", "GameController.SpawnHeroAt(position)", "*Not yet documented*", "void", { {"position", "Vector3"} } }
        };
        add_mod(m);
    }

    // ── Health ─────────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Health";
        m.description = "Entity health, hero mana, hit immunity timers, and damage status.";
        m.functions = {
            { "CurrentManaPercent", "Health.CurrentManaPercent()", "Get the character's current mana as a percentage.", "number", {} },
            { "CurrentMana", "Health.CurrentMana()", "Get the character's current mana as an absolute value.", "number", {} },
            { "CurrentHealth", "Health.CurrentHealth(obj)", "Get the current health (in half-hearts) for 'obj'.", "number", { {"obj", "SceneObject"} } },
            { "CurrentPercent", "Health.CurrentPercent(obj)", "Get the current health (in percentage) for 'obj'.", "number", { {"obj", "SceneObject"} } },
            { "SetImmunityTime", "Health.SetImmunityTime(obj, time)", "Make 'obj' immune for 'time' seconds.", "void", { {"obj", "SceneObject"}, {"time", "number"} } },
            { "SetCurrentHealth", "Health.SetCurrentHealth(obj, health)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"health", "number"} } },
            { "SetCurrentMana", "Health.SetCurrentMana(obj, mana)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"mana", "number"} } },
            { "MaxHealth", "Health.MaxHealth(obj)", "*Not yet documented*", "number", { {"obj", "SceneObject"} } },
            { "MaxMana", "Health.MaxMana(obj)", "*Not yet documented*", "number", { {"obj", "SceneObject"} } },
            { "HasTakenDamage", "Health.HasTakenDamage(obj)", "*Not yet documented*", "boolean", { {"obj", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── ItemDrop ───────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "ItemDrop";
        m.description = "Chest item drops, dropped loot tracking, and item collection status.";
        m.functions = {
            { "Trigger", "ItemDrop.Trigger(obj)", "Make a chest drop all its items.", "void", { {"obj", "SceneObject"} } },
            { "NumItems", "ItemDrop.NumItems(obj)", "Get the number of items in a chest.", "number", { {"obj", "SceneObject"} } },
            { "SetItemIdentifier", "ItemDrop.SetItemIdentifier(obj, itemnum, identifier)", "Set the identifier of a chest drop item.", "void", { {"obj", "SceneObject"}, {"itemnum", "number"}, {"identifier", "string"} } },
            { "ItemIdentifier", "ItemDrop.ItemIdentifier(obj, itemnum)", "Get the identifier of a chest drop item.", "string", { {"obj", "SceneObject"}, {"itemnum", "number"} } },
            { "AllItemsCollected", "ItemDrop.AllItemsCollected(obj)", "Test if all the items dropped from a chest have been picked up.", "boolean", { {"obj", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── KeyframeAnimation ──────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "KeyframeAnimation";
        m.description = "Keyframe skeletal animation playback, time scrubbing, and completion querying.";
        m.functions = {
            { "SetRunning", "KeyframeAnimation.SetRunning(obj, animation_identifier, running)", "Start or stop an animation for 'obj'.", "void", { {"obj", "SceneObject"}, {"animation_identifier", "number"}, {"running", "boolean"} } },
            { "SetCurrentTime", "KeyframeAnimation.SetCurrentTime(obj, animation_identifier, time)", "Jump to a point in an animation.", "void", { {"obj", "SceneObject"}, {"animation_identifier", "number"}, {"time", "number"} } },
            { "TimeToCompletion", "KeyframeAnimation.TimeToCompletion(obj, animation_identifier)", "Get the number of seconds until an animation finishes.", "number", { {"obj", "SceneObject"}, {"animation_identifier", "number"} } },
            { "TimeToFrame", "KeyframeAnimation.TimeToFrame(obj, animation_identifier, framenum)", "Get the number of seconds from 'framenum' to the end of 'animation_identifier' for 'obj'.", "number", { {"obj", "SceneObject"}, {"animation_identifier", "number"}, {"framenum", "number"} } },
            { "Duration", "KeyframeAnimation.Duration(obj, animation_identifier)", "*Not yet documented*", "number", { {"obj", "SceneObject"}, {"animation_identifier", "number"} } },
            { "SetReversed", "KeyframeAnimation.SetReversed(obj, animation_identifier, reversed)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"animation_identifier", "number"}, {"reversed", "boolean"} } }
        };
        add_mod(m);
    }

    // ── Light ──────────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Light";
        m.description = "Dynamic scene light overlays and brightness controls.";
        m.functions = {
            { "SetOverlayIntensity", "Light.SetOverlayIntensity(obj, intensity)", "Set the brightness of all lights for 'obj'.", "void", { {"obj", "SceneObject"}, {"intensity", "number"} } },
            { "OverlayIntensity", "Light.OverlayIntensity(obj)", "Get the brightness of all lights for 'obj'.", "number", { {"obj", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── Math ───────────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Math";
        m.description = "Swordigo math functions: random numbers and trigonometry.";
        m.functions = {
            { "RandomInt", "Math.RandomInt(min, max)", "Get a random whole number between 'min' and 'max'.", "number", { {"min", "number"}, {"max", "number"} } },
            { "RandomFloat", "Math.RandomFloat(min, max)", "Get a random float between 'min' and 'max'.", "number", { {"min", "number"}, {"max", "number"} } },
            { "Abs", "Math.Abs(x)", "Get the absolute of 'x'.", "number", { {"x", "number"} } },
            { "Sin", "Math.Sin(x)", "*Not yet documented*", "number", { {"x", "number"} } }
        };
        add_mod(m);
    }

    // ── ModelTransformController ───────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "ModelTransformController";
        m.description = "Model rotation speed, angles, rotation axes, and origin pivots.";
        m.functions = {
            { "SetRotationSpeed", "ModelTransformController.SetRotationSpeed(obj, speed)", "Rotate 'obj' at 'speed' degrees per second.", "void", { {"obj", "SceneObject"}, {"speed", "number"} } },
            { "SetRotationAngle", "ModelTransformController.SetRotationAngle(obj, angle)", "Rotate 'obj' to 'angle' degrees.", "void", { {"obj", "SceneObject"}, {"angle", "number"} } },
            { "SetRotationAxis", "ModelTransformController.SetRotationAxis(obj, axis)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"axis", "Vector3"} } },
            { "SetOrigin", "ModelTransformController.SetOrigin(obj, origin)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"origin", "Vector3"} } }
        };
        add_mod(m);
    }

    // ── MusicPlayer ─────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "MusicPlayer";
        m.description = "Background music track playback and volume fading.";
        m.functions = {
            { "PlayMusic", "MusicPlayer.PlayMusic(musicname, x, y)", "Play 'musicname'. The purpose of x and y is currently unknown.", "void", { {"musicname", "string"}, {"x", "boolean (optional)"}, {"y", "boolean (optional)"} } },
            { "FadeIn", "MusicPlayer.FadeIn(time)", "*Not yet documented*", "void", { {"time", "number (optional)"} } },
            { "FadeOut", "MusicPlayer.FadeOut(time)", "*Not yet documented*", "void", { {"time", "number (optional)"} } }
        };
        add_mod(m);
    }

    // ── ObjectLinkController ───────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "ObjectLinkController";
        m.description = "Bone attachment and parent-child scene object linking.";
        m.functions = {
            { "LinkToBone", "ObjectLinkController.LinkToBone(child_obj, parent_obj, bonename)", "Make 'child_obj' follow one of 'parent_obj''s bones.", "void", { {"child_obj", "SceneObject"}, {"parent_obj", "SceneObject"}, {"bonename", "string"} } },
            { "LinkToObject", "ObjectLinkController.LinkToObject(child_obj, parent_obj)", "*Not yet documented*", "void", { {"child_obj", "SceneObject"}, {"parent_obj", "SceneObject"} } },
            { "SetWorldOffset", "ObjectLinkController.SetWorldOffset(obj, offset)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"offset", "Vector3"} } },
            { "SetLocalOffset", "ObjectLinkController.SetLocalOffset(obj, offset)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"offset", "Vector3"} } }
        };
        add_mod(m);
    }

    // ── ParticleEmitter ────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "ParticleEmitter";
        m.description = "Particle system emitter parameters, origins, and color properties.";
        m.functions = {
            { "SetOriginOffset", "ParticleEmitter.SetOriginOffset(obj, emitter_identifier, offset)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"emitter_identifier", "string | number"}, {"offset", "Vector3"} } },
            { "SetParameterAtIndex", "ParticleEmitter.SetParameterAtIndex(obj, emitter_identifier, parameter, value)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"emitter_identifier", "number"}, {"parameter", "number"}, {"value", "number"} } },
            { "ParameterAtIndex", "ParticleEmitter.ParameterAtIndex(obj, emitter_identifier, parameter)", "*Not yet documented*", "number", { {"obj", "SceneObject"}, {"emitter_identifier", "number"}, {"parameter", "number"} } },
            { "SetColor", "ParticleEmitter.SetColor(obj, emitter_identifier, color)", "*Not yet documented*; likely rgb via Vector3", "void", { {"obj", "SceneObject"}, {"emitter_identifier", "number"}, {"color", "Vector3"} } },
            { "Test", "ParticleEmitter.Test()", "*Not yet documented*; debug/dev-only function", "void", {} }
        };
        add_mod(m);
    }

    // ── PhysicsObject ──────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "PhysicsObject";
        m.description = "Rigid body physics simulation. (Note: Controls simulation dynamics and does NOT alter collision geometry; use CollisionShape for collisions).";
        m.functions = {
            { "SetEnabled", "PhysicsObject.SetEnabled(obj, enabled)", "Enable or disable the PhysicsObject component for 'obj'. (Note: does not alter collision geometry.)", "void", { {"obj", "SceneObject"}, {"enabled", "boolean"} } },
            { "IsEnabled", "PhysicsObject.IsEnabled(obj)", "Test if the PhysicsObject component is enabled for 'obj'.", "boolean", { {"obj", "SceneObject"} } },
            { "SetGravityDirection", "PhysicsObject.SetGravityDirection(obj, direction)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"direction", "Vector3"} } },
            { "SetGravityMagnitude", "PhysicsObject.SetGravityMagnitude(obj, magnitude)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"magnitude", "number"} } },
            { "SetDecelerationForce", "PhysicsObject.SetDecelerationForce(obj, force)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"force", "number"} } },
            { "OriginalPosition", "PhysicsObject.OriginalPosition(obj)", "Get the position 'obj' was spawned at.", "Vector3", { {"obj", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── Portal ─────────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Portal";
        m.description = "Level transitions and portal trigger activation.";
        m.functions = {
            { "Activate", "Portal.Activate(obj)", "*Not yet documented*", "void", { {"obj", "SceneObject"} } },
            { "Deactivate", "Portal.Deactivate(obj)", "*Not yet documented*", "void", { {"obj", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── Program ────────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Program";
        m.description = "Script coroutines, delays, and program sequence execution.";
        m.functions = {
            { "Wait", "Program.Wait(time)", "Pause the execution of this program for 'time' seconds.", "void", { {"time", "number"} } },
            { "Print", "Program.Print(message)", "Prints message to engine log.", "void", { {"message", "string"} } },
            { "Execute", "Program.Execute(obj, program_identifier)", "Execute 'program_identifier' for 'obj'.", "void", { {"obj", "SceneObject"}, {"program_identifier", "number (optional)"} } },
            { "SetKeepActive", "Program.SetKeepActive(active)", "*Not yet documented*", "void", { {"active", "boolean"} } },
            { "IsRunning", "Program.IsRunning(obj, program_identifier)", "*Not yet documented*", "boolean", { {"obj", "SceneObject"}, {"program_identifier", "number (optional)"} } }
        };
        add_mod(m);
    }

    // ── Properties ─────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Properties";
        m.description = "Dynamic key-value properties attached to SceneObjects.";
        m.functions = {
            { "GetProperty", "Properties.GetProperty(obj, propertyname)", "*Not yet documented*", "string", { {"obj", "SceneObject"}, {"propertyname", "string"} } },
            { "SetProperty", "Properties.SetProperty(obj, propertyname, value)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"propertyname", "string"}, {"value", "string"} } }
        };
        add_mod(m);
    }

    // ── Rectangle ──────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Rectangle";
        m.description = "2D rectangle bounding box construction.";
        m.functions = {
            { "New", "Rectangle.New(x, y, width, height)", "Make a new Rectangle with the given dimensions.", "Rectangle", { {"x", "number"}, {"y", "number"}, {"width", "number"}, {"height", "number"} } }
        };
        add_mod(m);
    }

    // ── Scene ──────────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Scene";
        m.description = "Scene graph manipulation: instantiating templates, finding objects, lighting overrides, and pausing.";
        m.functions = {
            { "AddObject", "Scene.AddObject(obj)", "Realize a cloned SceneObject instance. Use this function after SceneObject:clone().", "SceneObject", { {"obj", "SceneObject"} } },
            { "CreateObject", "Scene.CreateObject(template, identifier, parent, x)", "Create a SceneObject instance using 'template', and set its identifier to 'identifier'.", "SceneObject", { {"template", "string"}, {"identifier", "string (optional)"}, {"parent", "SceneObject (optional)"}, {"x", "boolean (optional)"} } },
            { "Find", "Scene.Find(identifier)", "Get the SceneObject which matches 'identifier'.", "SceneObject", { {"identifier", "string | number"} } },
            { "OverrideLights", "Scene.OverrideLights(red, green, blue)", "Set the light colour for the entire scene. All numbers should be in range 0.0-1.0.", "void", { {"red", "number"}, {"green", "number"}, {"blue", "number"} } },
            { "ResetLights", "Scene.ResetLights()", "Set the light colour for the scene to (1, 1, 1).", "void", {} },
            { "SetGroupHidden", "Scene.SetGroupHidden(groupname, hidden)", "Set the hidden status for all objects in 'groupname' to 'hidden'.", "void", { {"groupname", "string"}, {"hidden", "boolean"} } },
            { "SetPaused", "Scene.SetPaused(paused)", "Pause or unpause the scene.", "void", { {"paused", "boolean"} } }
        };
        add_mod(m);
    }

    // ── Skill ──────────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Skill";
        m.description = "Player magic casting origin, execution, and cancellation.";
        m.functions = {
            { "BeginCasting", "Skill.BeginCasting(obj, casted_obj_identifier, position)", "Make 'obj' start casting an object with the object starting at 'position'.", "SceneObject", { {"obj", "SceneObject"}, {"casted_obj_identifier", "string"}, {"position", "Vector3"} } },
            { "FinishCasting", "Skill.FinishCasting(obj)", "Make 'obj' finish the spell casting animation.", "void", { {"obj", "SceneObject"} } },
            { "CancelCasting", "Skill.CancelCasting(obj)", "Make 'obj' finish spell casting and cancel casting the object it was about to cast.", "void", { {"obj", "SceneObject"} } },
            { "Origin", "Skill.Origin(obj)", "*Not yet documented*", "Vector3", { {"obj", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── Spell ──────────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Spell";
        m.description = "Active spell queries: caster entity and casting origin.";
        m.functions = {
            { "CasterObject", "Spell.CasterObject(self)", "Find what object is casting self.", "SceneObject", { {"self", "SceneObject"} } },
            { "CastPoint", "Spell.CastPoint(self)", "Find where self is being casted from.", "Vector3", { {"self", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── SoundLibrary ────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "SoundLibrary";
        m.description = "Sound effect audio catalog playback.";
        m.functions = {
            { "PlayEffect", "SoundLibrary.PlayEffect(effectname)", "Play a sound effect.", "void", { {"effectname", "string"} } }
        };
        add_mod(m);
    }

    // ── TextBubble ─────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "TextBubble";
        m.description = "Floating speech bubble text, tap-to-continue progression, and touch controls.";
        m.functions = {
            { "ShowText", "TextBubble.ShowText(bubble_obj, text, max_width)", "Change the text shown in 'bubble_obj' to 'text'.", "void", { {"bubble_obj", "SceneObject"}, {"text", "string"}, {"max_width", "number (optional)"} } },
            { "SetTouchHandlingEnabled", "TextBubble.SetTouchHandlingEnabled(bubble_obj, enabled)", "Enable or disable touch handling for 'bubble_obj'.", "void", { {"bubble_obj", "SceneObject"}, {"enabled", "boolean"} } },
            { "IsTextFinished", "TextBubble.IsTextFinished(bubble_obj)", "Test if 'bubble_obj' has been tapped to skip the text.", "void", { {"bubble_obj", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── Touchable ──────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Touchable";
        m.description = "Touch input hit radius for interactive world objects.";
        m.functions = {
            { "SetTouchRadius", "Touchable.SetTouchRadius(obj, radius)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"radius", "number"} } }
        };
        add_mod(m);
    }

    // ── OverlayText ────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "OverlayText";
        m.description = "UI overlay text display.";
        m.functions = {
            { "SetText", "OverlayText.SetText(obj, text)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"text", "string"} } }
        };
        add_mod(m);
    }

    // ── TransformController ────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "TransformController";
        m.description = "Smooth procedural translation, scaling, and rotation transitions.";
        m.functions = {
            { "SetOrigin", "TransformController.SetOrigin(obj, origin)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"origin", "Vector3"} } },
            { "TranslateTo", "TransformController.TranslateTo(obj, translation, time)", "Move 'obj' to position 'translation' over 'time' seconds.", "void", { {"obj", "SceneObject"}, {"translation", "Vector3"}, {"time", "number"} } },
            { "TranslateBy", "TransformController.TranslateBy(obj, translation, time)", "Move 'obj' by 'translation' relative to its current position over 'time' seconds.", "void", { {"obj", "SceneObject"}, {"translation", "Vector3"}, {"time", "number"} } },
            { "RotateBy", "TransformController.RotateBy(obj, rotation, time)", "Rotate 'obj' by 'rotation' relative to its current rotation over 'time' seconds.", "void", { {"obj", "SceneObject"}, {"rotation", "number"}, {"time", "number"} } },
            { "RotateTo", "TransformController.RotateTo(obj, rotation, time)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"rotation", "number"}, {"time", "number"} } },
            { "ScaleTo", "TransformController.ScaleTo(obj, scale, time)", "Set the size of 'obj' to scale over 'time' seconds.", "void", { {"obj", "SceneObject"}, {"scale", "number"}, {"time", "number"} } },
            { "ScaleBy", "TransformController.ScaleBy(obj, scale, time)", "Adjust the size of 'obj' by scale over 'time' seconds.", "void", { {"obj", "SceneObject"}, {"scale", "number"}, {"time", "number"} } }
        };
        add_mod(m);
    }

    // ── Vector3 ────────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Vector3";
        m.description = "3D vector math utility: coordinates, length, and normalization.";
        m.functions = {
            { "New", "Vector3.New(x, y, z)", "Make a new Vector3.", "Vector3", { {"x", "number"}, {"y", "number (optional)"}, {"z", "number (optional)"} } },
            { "x", "Vector3.x(self)", "Get x component.", "number", { {"self", "Vector3"} } },
            { "y", "Vector3.y(self)", "Get y component.", "number", { {"self", "Vector3"} } },
            { "z", "Vector3.z(self)", "Get z component.", "number", { {"self", "Vector3"} } },
            { "normalized", "Vector3.normalized(self)", "Get the normal of a Vector3.", "Vector3", { {"self", "Vector3"} } },
            { "length", "Vector3.length(self)", "Get the length of a Vector3.", "number", { {"self", "Vector3"} } },
            { "FromAngle", "Vector3.FromAngle(angle)", "*Not yet documented*", "Vector3", { {"angle", "number"} } }
        };
        add_mod(m);
    }

    // ── SceneObject ────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "SceneObject";
        m.description = "Instance methods available on SceneObject handles (e.g. self, target, or created objects).";
        m.functions = {
            { "addComponent", "SceneObject:addComponent(component_class)", "Add a component to the object.", "void", { {"component_class", "string"} } },
            { "clone", "SceneObject:clone()", "Clone the object, return the clone. You can realize the clone using Scene.AddObject.", "SceneObject", {} },
            { "destroy", "SceneObject:destroy()", "Delete the object.", "void", {} },
            { "identifier", "SceneObject:identifier()", "Get the identifier of the object.", "string", {} },
            { "position", "SceneObject:position()", "Get the position of the object.", "Vector3", {} },
            { "depth", "SceneObject:depth()", "Get the depth (z position) of the object.", "number", {} },
            { "rotation", "SceneObject:rotation()", "Get the rotation of the object.", "number", {} },
            { "scaling", "SceneObject:scaling()", "Get the scaling of the object.", "number", {} },
            { "velocity", "SceneObject:velocity()", "Get the velocity of the object.", "Vector3", {} },
            { "setHidden", "SceneObject:setHidden(hidden)", "*Not yet documented*", "void", { {"hidden", "boolean"} } },
            { "setPosition", "SceneObject:setPosition(position)", "*Not yet documented*", "void", { {"position", "Vector3"} } },
            { "setRotation", "SceneObject:setRotation(rotation)", "*Not yet documented*", "void", { {"rotation", "number"} } },
            { "setScaling", "SceneObject:setScaling(scaling)", "*Not yet documented*", "void", { {"scaling", "number"} } },
            { "setVelocity", "SceneObject:setVelocity(velocity)", "*Not yet documented*", "void", { {"velocity", "Vector3"} } },
            { "setAlwaysActive", "SceneObject:setAlwaysActive(always_active)", "Prevent the object from being unloaded when it is off screen.", "void", { {"always_active", "boolean"} } },
            { "setParentObject", "SceneObject:setParentObject(parent)", "*Not yet documented*", "void", { {"parent", "SceneObject"} } }
        };
        add_mod(m);
    }

    // ── Global ─────────────────────────────────────────────────────────────
    {
        LuaModuleMeta m;
        m.module_name = "Global";
        m.description = "Global engine runtime functions defined directly in _G.";
        m.functions = {
            { "SetDimensionModeEnabled", "SetDimensionModeEnabled(enabled)", "Travel to or from the dark dimension!", "void", { {"enabled", "boolean"} } },
            { "GenerateDimensionMonsters", "GenerateDimensionMonsters(position, max_count)", "*Not yet documented*", "void", { {"position", "Vector3"}, {"max_count", "number"} } },
            { "DirectionToTargetFromPosition", "DirectionToTargetFromPosition(target, position)", "*Not yet documented*", "number", { {"target", "Vector3"}, {"position", "Vector3"} } },
            { "CreateShopItem", "CreateShopItem(obj, itemname, itemprice, item_identifier)", "Try to make a shop item given 'itemname' and 'itemprice'. Returns true if successful, false if already bought.", "boolean", { {"obj", "SceneObject"}, {"itemname", "string (optional)"}, {"itemprice", "number (optional)"}, {"item_identifier", "string (optional)"} } },
            { "DestroyShopItem", "DestroyShopItem(obj)", "*Not yet documented*", "void", { {"obj", "SceneObject"} } },
            { "ShowShopItemInfo", "ShowShopItemInfo(obj)", "*Not yet documented*", "void", { {"obj", "SceneObject"} } },
            { "HideShopItemInfo", "HideShopItemInfo(obj)", "*Not yet documented*", "void", { {"obj", "SceneObject"} } },
            { "EntityActionWait", "EntityActionWait(obj, time)", "*Not yet documented*", "void", { {"obj", "SceneObject"}, {"time", "number"} } },
            { "ShowTextBubble", "ShowTextBubble(identifier, position, text, max_width)", "Show a text bubble displaying 'text'.", "SceneObject", { {"identifier", "string"}, {"position", "Vector3"}, {"text", "string"}, {"max_width", "number (optional)"} } },
            { "ShowTextBubbles", "ShowTextBubbles(identifier, position, handle_touches, text_array, max_width)", "Show a text bubble for every string in 'text_array'.", "SceneObject", { {"identifier", "string"}, {"position", "Vector3"}, {"handle_touches", "boolean"}, {"text_array", "table"}, {"max_width", "number (optional)"} } },
            { "ShowQuestBubbles", "ShowQuestBubbles(quest_name, identifier, position, handle_touches, text_array, max_width)", "Show a text bubble for every string in 'text_array', and add text to quest log.", "SceneObject", { {"quest_name", "string"}, {"identifier", "string"}, {"position", "Vector3"}, {"handle_touches", "boolean"}, {"text_array", "table"}, {"max_width", "number (optional)"} } },
            { "HideTextBubble", "HideTextBubble(bubble_name)", "Hide a TextBubble.", "void", { {"bubble_name", "string"} } },
            { "ChestOnLoad", "ChestOnLoad(self)", "Utility function for when a chest is loaded.", "void", { {"self", "SceneObject"} } },
            { "ChestOnUse", "ChestOnUse(self)", "Utility function for when a chest is used.", "void", { {"self", "SceneObject"} } },
            { "AssembleQuestTrigger", "AssembleQuestTrigger(master)", "Trigger the quest to assemble the mageblade.", "void", { {"master", "SceneObject"} } },
            { "FocusAtPointAndWait", "FocusAtPointAndWait(point)", "*Not yet documented*", "void", { {"point", "Vector3"} } },
            { "ObjectAppear", "ObjectAppear(obj)", "*Not yet documented*; new object animation/appear effect", "void", { {"obj", "SceneObject"} } },
            { "LowerPillairs", "LowerPillairs(name)", "*Not yet documented*; found only in dump, purpose unknown", "void", { {"name", "string"} } }
        };
        add_mod(m);
    }
}

const ComponentMeta* SwordigoEngineDB::find_component(const std::string& name) const {
    auto cit = m_class_map.find(name);
    if (cit != m_class_map.end()) return &m_components[cit->second];
    auto tit = m_tag_map.find(name);
    if (tit != m_tag_map.end()) return &m_components[tit->second];
    return nullptr;
}

const std::vector<ComponentMeta>& SwordigoEngineDB::all_components() const {
    return m_components;
}

std::vector<std::string> SwordigoEngineDB::get_all_component_class_names() const {
    std::vector<std::string> names;
    names.reserve(m_components.size());
    for (const auto& c : m_components) names.push_back(c.class_name);
    return names;
}

const EnumMeta* SwordigoEngineDB::find_enum(const std::string& name) const {
    auto it = m_enum_map.find(name);
    return (it != m_enum_map.end()) ? &m_enums[it->second] : nullptr;
}

const std::vector<EnumMeta>& SwordigoEngineDB::all_enums() const {
    return m_enums;
}

const LuaModuleMeta* SwordigoEngineDB::find_lua_module(const std::string& module_name) const {
    auto it = m_module_map.find(module_name);
    return (it != m_module_map.end()) ? &m_lua_modules[it->second] : nullptr;
}

const LuaFunctionMeta* SwordigoEngineDB::find_lua_function(const std::string& module_name, const std::string& func_name) const {
    const auto* mod = find_lua_module(module_name);
    if (!mod) return nullptr;
    for (const auto& fn : mod->functions) {
        if (fn.name == func_name) return &fn;
    }
    return nullptr;
}

const std::vector<LuaModuleMeta>& SwordigoEngineDB::all_lua_modules() const {
    return m_lua_modules;
}

std::string SwordigoEngineDB::get_component_html_doc(const std::string& class_name) const {
    const auto* comp = find_component(class_name);
    if (!comp) return "";

    std::ostringstream ss;
    ss << "<div style='font-family: -apple-system, Segoe UI, sans-serif; padding: 6px; color: #abb2bf; font-size: 13px; line-height: 1.4;'>"
       << "<div style='border-bottom: 1px solid #3e4451; padding-bottom: 4px; margin-bottom: 8px;'>"
       << "<span style='color: #61afef; font-weight: bold; font-size: 15px;'>Component: " << comp->class_name << "</span>"
       << " <span style='color: #98c379; font-size: 11px; background: #282c34; padding: 2px 6px; border-radius: 3px;'>[" << comp->category << "]</span>"
       << "<div style='color: #e5c07b; font-size: 12px; margin-top: 2px;'>Protobuf Tag: <code>" << comp->tag_name << "</code></div>"
       << "</div>"
       << "<p style='margin: 0 0 8px 0; color: #dcdfe4;'>" << comp->description << "</p>";

    if (!comp->fields.empty()) {
        ss << "<div style='font-weight: bold; color: #e5c07b; margin-top: 8px; margin-bottom: 4px;'>Fields & Parameters:</div>"
           << "<table style='border-collapse: collapse; width: 100%; font-size: 12px;'>"
           << "<tr style='color: #5c6370; text-align: left;'><th style='padding: 2px 6px;'>Field</th><th style='padding: 2px 6px;'>Type</th><th style='padding: 2px 6px;'>Default</th><th style='padding: 2px 6px;'>Description</th></tr>";
        for (const auto& f : comp->fields) {
            ss << "<tr style='border-top: 1px solid #282c34;'>"
               << "<td style='padding: 3px 6px; color: #e06c75; font-weight: bold;'><code>" << f.name << "</code></td>"
               << "<td style='padding: 3px 6px; color: #d19a66;'><code>" << f.type << "</code></td>"
               << "<td style='padding: 3px 6px; color: #5c6370;'><code>" << f.default_val << "</code></td>"
               << "<td style='padding: 3px 6px; color: #abb2bf;'>" << f.description;
            if (!f.enum_name.empty()) {
                const auto* en = find_enum(f.enum_name);
                if (en) {
                    ss << "<br/><span style='color: #61afef; font-size: 11px;'>Values: ";
                    for (size_t i = 0; i < en->values.size(); ++i) {
                        if (i > 0) ss << ", ";
                        ss << en->values[i].value << "=" << en->values[i].name;
                    }
                    ss << "</span>";
                }
            }
            ss << "</td></tr>";
        }
        ss << "</table>";
    }

    if (!comp->lua_hooks.empty()) {
        ss << "<div style='font-weight: bold; color: #98c379; margin-top: 10px; margin-bottom: 4px;'>Lua Event Callbacks:</div>";
        for (const auto& hook : comp->lua_hooks) {
            ss << "<div style='background: #21252b; padding: 4px 8px; border-radius: 4px; margin-bottom: 4px;'>"
               << "<span style='color: #61afef; font-weight: bold;'>" << hook.event_name << "</span>"
               << " <code style='color: #e5c07b;'>{ String : $ " << hook.signature << " $end }</code>"
               << "<div style='color: #5c6370; font-size: 11px; margin-top: 2px;'>" << hook.description << "</div>"
               << "</div>";
        }
    }

    if (!comp->example_scl.empty()) {
        ss << "<div style='font-weight: bold; color: #5c6370; margin-top: 10px; margin-bottom: 4px;'>Blueprint Snippet:</div>"
           << "<pre style='background: #1e1e1e; padding: 6px; border-radius: 4px; color: #abb2bf; font-size: 11px; margin: 0;'>"
           << comp->example_scl << "</pre>";
    }

    ss << "</div>";
    return ss.str();
}

std::string SwordigoEngineDB::get_field_html_doc(const std::string& class_name, const std::string& field_name) const {
    const auto* comp = find_component(class_name);
    if (!comp) return "";

    for (const auto& f : comp->fields) {
        if (f.name == field_name) {
            std::ostringstream ss;
            ss << "<div style='font-family: -apple-system, Segoe UI, sans-serif; padding: 6px; color: #abb2bf; font-size: 13px;'>"
               << "<span style='color: #e06c75; font-weight: bold; font-size: 14px;'>" << comp->class_name << "::" << f.name << "</span>"
               << " <span style='color: #d19a66;'>(" << f.type << ")</span>"
               << "<p style='margin: 4px 0 6px 0; color: #dcdfe4;'>" << f.description << "</p>"
               << "<div style='color: #5c6370; font-size: 11px;'>Default value: <code>" << f.default_val << "</code></div>";

            if (!f.enum_name.empty()) {
                const auto* en = find_enum(f.enum_name);
                if (en) {
                    ss << "<div style='margin-top: 6px; font-weight: bold; color: #61afef;'>Valid Enum Options:</div><ul style='margin: 2px 0; padding-left: 18px;'>";
                    for (const auto& ev : en->values) {
                        ss << "<li><code>" << ev.value << "</code> (<b>" << ev.name << "</b>) — " << ev.description << "</li>";
                    }
                    ss << "</ul>";
                }
            }
            ss << "</div>";
            return ss.str();
        }
    }
    return "";
}

std::string SwordigoEngineDB::get_enum_html_doc(const std::string& enum_name) const {
    const auto* en = find_enum(enum_name);
    if (!en) return "";

    std::ostringstream ss;
    ss << "<div style='font-family: -apple-system, Segoe UI, sans-serif; padding: 6px; color: #abb2bf; font-size: 13px;'>"
       << "<span style='color: #61afef; font-weight: bold; font-size: 14px;'>Enum: " << en->name << "</span>"
       << "<p style='margin: 4px 0 6px 0; color: #dcdfe4;'>" << en->description << "</p>"
       << "<ul style='margin: 2px 0; padding-left: 18px;'>";
    for (const auto& ev : en->values) {
        ss << "<li><code>" << ev.value << "</code>: <b style='color: #e5c07b;'>" << ev.name << "</b> — " << ev.description << "</li>";
    }
    ss << "</ul></div>";
    return ss.str();
}

std::string SwordigoEngineDB::get_lua_function_html_doc(const std::string& module_name, const std::string& func_name) const {
    const auto* fn = find_lua_function(module_name, func_name);
    if (!fn) return "";

    std::ostringstream ss;
    ss << "<div style='font-family: -apple-system, Segoe UI, sans-serif; padding: 6px; color: #abb2bf; font-size: 13px;'>"
       << "<div style='color: #61afef; font-weight: bold; font-size: 14px;'><code>" << fn->signature << "</code></div>"
       << "<p style='margin: 4px 0 6px 0; color: #dcdfe4;'>" << fn->description << "</p>";
    if (!fn->params.empty()) {
        ss << "<div style='font-weight: bold; color: #e5c07b; margin-top: 4px;'>Parameters:</div><ul style='margin: 2px 0; padding-left: 18px;'>";
        for (const auto& p : fn->params) {
            ss << "<li><code>" << p.first << "</code> — " << p.second << "</li>";
        }
        ss << "</ul>";
    }
    ss << "<div style='color: #5c6370; margin-top: 4px;'>Returns: <code>" << fn->return_type << "</code></div>"
       << "</div>";
    return ss.str();
}

} // namespace ruby::database
