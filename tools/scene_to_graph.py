#!/usr/bin/env python3
"""
scene_to_graph.py — Convert FileRift decoded Swordigo .scene files to Graphy Node Graph JSON.

Features:
- Full entity and component extraction (Objects, Components, Transforms, AABBs).
- Inter-component dependency wiring:
    * GroundPolygon -> GroundMeshGenerator
    * TextureMapping -> GroundMeshGenerator
    * GroundMeshGenerator -> GroundMesh
    * GroundPolygon -> CollisionShape (ParentComponentIdentifier)
    * CollisionShape -> Portal (TriggerShapeId)
    * Model -> KeyframeAnimation & AnimationController
    * AnimationController & SoundEffect -> DoorController
- Inter-entity semantic wiring:
    * Lua OnCollide script -> Scene.Find("elder") entity targets
    * Portal -> SpawnPoint matching
- Clean hierarchical column layout with Unreal-style Comment Frames per entity/group.
- Scene overview node displaying Bounds and ObjectLibrary imported packages.
"""

import sys
import os
import re
import json
import argparse

# ── PinType enum values matching ruby::graph::PinType ────────────────────────
PIN_EXEC       = 0
PIN_BOOLEAN    = 1
PIN_BYTE       = 2
PIN_INT        = 3
PIN_FLOAT      = 4
PIN_STRING     = 5
PIN_TEXT       = 6
PIN_VECTOR3    = 7
PIN_ROTATOR    = 8
PIN_TRANSFORM  = 9
PIN_COLOR      = 10
PIN_OBJECT     = 11
PIN_DELEGATE   = 12
PIN_WILDCARD   = 13

PIN_DIR_INPUT  = 0
PIN_DIR_OUTPUT = 1

# ── Category Palette ─────────────────────────────────────────────────────────
CATEGORY_MAP = {
    "Entity": "Level / Entity",
    "Model": "Rendering",
    "Background": "Environment",
    "Light": "Lighting",
    "ParticleEmitter": "FX / Particles",
    "Particle": "FX / Particles",
    "SimpleGlow": "Lighting",
    "CollisionShape": "Collision",
    "GroundPolygon": "Terrain & Polygon",
    "GroundMesh": "Terrain & Mesh",
    "GroundMeshGenerator": "Terrain Generator",
    "TextureMapping": "Materials & UV",
    "Portal": "Gameplay / Portals",
    "SpawnPoint": "Level / Spawns",
    "DoorController": "Gameplay / Doors",
    "KeyframeAnimation": "Animation",
    "AnimationController": "Animation",
    "SoundEffect": "Audio",
    "OrbitController": "Transforms",
    "Program": "Scripting",
    "OnCollideScript": "Scripting / Lua",
}

# ── Reference Field Mapping (Fields that refer to another component's ID) ────
REF_FIELDS = {
    "GroundPolygonId": ("Ground Polygon", PIN_INT),
    "TargetMeshId": ("Target Mesh", PIN_INT),
    "FrontTextureMappingId": ("Front Texture", PIN_INT),
    "SurfaceTextureMappingId": ("Surface Texture", PIN_INT),
    "ParentComponentIdentifier": ("Parent Polygon", PIN_INT),
    "TriggerShapeId": ("Trigger Shape", PIN_INT),
    "ModelId": ("Model ID", PIN_INT),
    "AnimationControllerId": ("Anim Controller", PIN_INT),
    "AnimationId": ("Animation Clip", PIN_INT),
    "CloseSoundId": ("Close Sound", PIN_INT),
    "OpenSoundId": ("Open Sound", PIN_INT),
    "ParticleId": ("Particle ID", PIN_INT),
    "ModelBindingId": ("Model Binding", PIN_INT),
}


class SceneBlock:
    def __init__(self, name):
        self.name = name
        self.scalars = {}       # key -> val or list of vals
        self.children = []      # list of SceneBlock

    def get(self, key, default=None):
        v = self.scalars.get(key, default)
        if isinstance(v, list) and len(v) > 0:
            return v[0]
        return v

    def get_all(self, key):
        v = self.scalars.get(key, [])
        if isinstance(v, list):
            return v
        return [v]

    def find_first(self, block_name):
        for c in self.children:
            if c.name == block_name:
                return c
        return None

    def find_all(self, block_name):
        return [c for c in self.children if c.name == block_name]


def parse_scene_text(text: str) -> SceneBlock:
    """Parses FileRift scene markup into a recursive SceneBlock tree."""
    root = SceneBlock("Root")
    stack = [root]

    lines = text.splitlines()
    i = 0
    n = len(lines)

    while i < n:
        line = lines[i]
        stripped = line.strip()

        if not stripped or stripped.startswith("#"):
            i += 1
            continue

        # Multiline Lua string: e.g. "String : $"
        if stripped.endswith("String : $") or ": $" in stripped:
            colon_idx = stripped.find(":")
            key = stripped[:colon_idx].strip()
            lua_lines = []
            i += 1
            while i < n:
                cur = lines[i]
                if cur.strip() == "$end" or "$end" in cur:
                    break
                lua_lines.append(cur)
                i += 1
            lua_code = "\n".join(lua_lines)
            stack[-1].scalars[key] = lua_code
            i += 1
            continue

        # Ignore huge raw binary byte dumps like Bytes : '...' or VertexData : '...'
        if stripped.startswith("Bytes :") or stripped.startswith("VertexData :") or stripped.startswith("IndexData :"):
            colon_idx = stripped.find(":")
            key = stripped[:colon_idx].strip()
            stack[-1].scalars[key] = "<binary data>"
            i += 1
            continue

        # Block close
        if stripped == "}":
            if len(stack) > 1:
                stack.pop()
            i += 1
            continue

        # Block open: "Object{" or "Component {" or "Position{"
        brace_idx = stripped.find("{")
        if brace_idx != -1 and not stripped.endswith("$"):
            block_name = stripped[:brace_idx].strip()
            new_block = SceneBlock(block_name)
            stack[-1].children.append(new_block)
            stack.append(new_block)
            i += 1
            continue

        # Key-Value scalar: "Identifier : 'elder'" or "Depth : 1.72"
        colon_idx = stripped.find(":")
        if colon_idx != -1:
            key = stripped[:colon_idx].strip()
            val_raw = stripped[colon_idx + 1:].strip()

            # Strip outer quotes
            if (val_raw.startswith("'") and val_raw.endswith("'")) or \
               (val_raw.startswith('"') and val_raw.endswith('"')):
                val = val_raw[1:-1]
            else:
                val = val_raw

            if key in stack[-1].scalars:
                existing = stack[-1].scalars[key]
                if isinstance(existing, list):
                    existing.append(val)
                else:
                    stack[-1].scalars[key] = [existing, val]
            else:
                stack[-1].scalars[key] = val
            i += 1
            continue

        i += 1

    return root


def classify_entity(identifier: str, template: str, components: list) -> tuple:
    """Returns (group_name, category_color) based on entity characteristics."""
    comp_names = [c.get("ClassName", "") for c in components]
    ident_lower = identifier.lower()

    if "background" in ident_lower or "background" in comp_names:
        return "Environment", "#1e3a5f"
    if "light" in ident_lower or "Light" in comp_names:
        return "Lighting", "#5f4b1e"
    if "spawn" in ident_lower or "SpawnPoint" in comp_names:
        return "Spawns & Travel", "#1e5f38"
    if "portal" in comp_names or "portal" in ident_lower:
        return "Portals & Exits", "#2d1e5f"
    if "elder" in ident_lower or "npc" in template.lower() or "npc" in ident_lower:
        return "NPCs & Dialogue", "#5f1e4b"
    if "trigger" in ident_lower:
        return "Triggers & Quests", "#5f1e1e"
    if "torch" in ident_lower:
        return "Lighting Props", "#5a431c"
    if "groundmesh" in [c.lower() for c in comp_names] or "groundpolygon" in [c.lower() for c in comp_names]:
        return "World Geometry & Terrain", "#1e4d35"
    return "Entities & Props", "#283542"


def convert_scene_to_graph(scene_root: SceneBlock, scene_name: str = "Scene") -> dict:
    """Converts the parsed SceneBlock tree into a comprehensive Graphy JSON document."""
    graph = {
        "version": 1,
        "next_node_id": 1,
        "next_pin_id": 100,
        "next_conn_id": 500,
        "next_comment_id": 900,
        "nodes": [],
        "connections": [],
        "comments": []
    }

    next_node_id = 1
    next_pin_id = 100
    next_conn_id = 500
    next_comment_id = 900

    # 1. Global Scene Overview Node
    bounds_blk = scene_root.find_first("Bounds")
    lib_blk = scene_root.find_first("ObjectLibrary")

    bounds_str = "Default"
    if bounds_blk:
        bx = bounds_blk.get("X", "0")
        by = bounds_blk.get("Y", "0")
        bw = bounds_blk.get("Width", "0")
        bh = bounds_blk.get("Height", "0")
        bounds_str = f"X: {bx} Y: {by} [{bw}x{bh}]"

    imported_libs = []
    if lib_blk:
        imported_libs = lib_blk.get_all("ImportedLibrary")

    overview_id = next_node_id
    next_node_id += 1

    ov_inputs = []
    ov_outputs = []

    ov_pin_bounds = next_pin_id
    next_pin_id += 1
    ov_inputs.append({
        "id": ov_pin_bounds,
        "node_id": overview_id,
        "name": "Level Bounds",
        "tooltip": bounds_str,
        "type": PIN_VECTOR3,
        "dir": PIN_DIR_INPUT,
        "default_value": bounds_str
    })

    for lib in imported_libs:
        pid = next_pin_id
        next_pin_id += 1
        ov_outputs.append({
            "id": pid,
            "node_id": overview_id,
            "name": f"Library: {lib}",
            "tooltip": f"Imported archetype package '{lib}.scl'",
            "type": PIN_OBJECT,
            "dir": PIN_DIR_OUTPUT,
            "default_value": lib
        })

    graph["nodes"].append({
        "id": overview_id,
        "title": f"Scene: {scene_name}",
        "subtitle": f"Bounds: {bounds_str}",
        "category": "Level / Environment",
        "flags": 0,
        "x": 60.0,
        "y": 60.0,
        "width": 260.0,
        "height": max(100.0, 44.0 + max(len(ov_inputs), len(ov_outputs)) * 24.0),
        "inputs": ov_inputs,
        "outputs": ov_outputs
    })

    # 2. Objects / Entities & Attached Components
    objects = scene_root.find_all("Object")

    entity_nodes = {}       # ident -> node_id
    entity_ref_pins = {}    # ident -> pin_id
    all_comp_nodes = {}     # (entity_ident, comp_id) -> dict(node_id, out_pin, in_pins)
    lua_script_nodes = []   # list of (node_id, script_text, in_pin_dict)

    grouped_objects = {}

    for obj in objects:
        ident = obj.get("Identifier", "UnnamedObject")
        tpl = obj.get("TemplateName", "None")
        components = obj.find_all("Component")

        group_name, frame_color = classify_entity(ident, tpl, components)
        if group_name not in grouped_objects:
            grouped_objects[group_name] = []
        grouped_objects[group_name].append((obj, frame_color))

    start_x = 400.0
    start_y = 60.0
    group_spacing_x = 750.0

    current_group_x = start_x

    for group_name, obj_list in grouped_objects.items():
        current_y = start_y

        for obj, frame_color in obj_list:
            ident = obj.get("Identifier", "UnnamedObject")
            tpl = obj.get("TemplateName", "None")
            pos_blk = obj.find_first("Position")

            px = float(pos_blk.get("X", 0.0)) if pos_blk else 0.0
            py = float(pos_blk.get("Y", 0.0)) if pos_blk else 0.0
            pz = float(obj.get("Depth", 0.0))
            rot = float(obj.get("Rotation", 0.0))
            scale = float(obj.get("Scaling", 1.0))

            # 2a. Entity Root Node
            entity_id = next_node_id
            next_node_id += 1
            entity_nodes[ident] = entity_id

            entity_inputs = []
            entity_outputs = []

            p_trans_in = next_pin_id
            next_pin_id += 1
            entity_inputs.append({
                "id": p_trans_in,
                "node_id": entity_id,
                "name": f"Pos ({px:.0f}, {py:.0f})",
                "tooltip": f"Position X: {px:.2f}, Y: {py:.2f}, Depth: {pz:.2f}",
                "type": PIN_VECTOR3,
                "dir": PIN_DIR_INPUT,
                "default_value": f"{px:.1f}, {py:.1f}, {pz:.1f}"
            })

            p_scale_in = next_pin_id
            next_pin_id += 1
            entity_inputs.append({
                "id": p_scale_in,
                "node_id": entity_id,
                "name": f"Scale {scale:.1f} · Rot {rot:.2f}",
                "tooltip": f"Scale: {scale}, Rotation: {rot} rad",
                "type": PIN_ROTATOR,
                "dir": PIN_DIR_INPUT,
                "default_value": f"Scale={scale}"
            })

            p_ent_ref = next_pin_id
            next_pin_id += 1
            entity_ref_pins[ident] = p_ent_ref
            entity_outputs.append({
                "id": p_ent_ref,
                "node_id": entity_id,
                "name": "Entity Ref",
                "tooltip": f"Reference handle for '{ident}'",
                "type": PIN_OBJECT,
                "dir": PIN_DIR_OUTPUT,
                "default_value": ident
            })

            p_comp_flow = next_pin_id
            next_pin_id += 1
            entity_outputs.append({
                "id": p_comp_flow,
                "node_id": entity_id,
                "name": "Components",
                "tooltip": "Component hierarchy chain",
                "type": PIN_DELEGATE,
                "dir": PIN_DIR_OUTPUT,
                "default_value": ""
            })

            entity_node = {
                "id": entity_id,
                "title": f"Entity: {ident}",
                "subtitle": f"Tpl: {tpl} · (Z: {pz:.1f})",
                "category": CATEGORY_MAP.get("Entity", "Level / Entity"),
                "flags": 0,
                "x": current_group_x + 30.0,
                "y": current_y + 40.0,
                "width": 240.0,
                "height": max(90.0, 44.0 + max(len(entity_inputs), len(entity_outputs)) * 24.0),
                "inputs": entity_inputs,
                "outputs": entity_outputs
            }
            graph["nodes"].append(entity_node)

            # 2b. Component Nodes for this Entity
            components = obj.find_all("Component")
            comp_nodes = []
            comp_y = current_y + 40.0

            for comp in components:
                cls_name = comp.get("ClassName", "Component")
                comp_id_str = comp.get("Identifier", "0")
                try:
                    comp_id = int(comp_id_str)
                except ValueError:
                    comp_id = 0

                cnode_id = next_node_id
                next_node_id += 1

                payload = None
                for child in comp.children:
                    if child.name.endswith("Component") or child.name in ["ShapeComponent", "Program"]:
                        payload = child
                        break

                c_inputs = []
                c_outputs = []

                p_parent = next_pin_id
                next_pin_id += 1
                c_inputs.append({
                    "id": p_parent,
                    "node_id": cnode_id,
                    "name": "Owner Entity",
                    "tooltip": f"Attached to {ident}",
                    "type": PIN_DELEGATE,
                    "dir": PIN_DIR_INPUT,
                    "default_value": ""
                })

                conn_id = next_conn_id
                next_conn_id += 1
                graph["connections"].append({
                    "id": conn_id,
                    "from_node": entity_id,
                    "from_pin": p_comp_flow,
                    "to_node": cnode_id,
                    "to_pin": p_parent
                })

                p_cid = next_pin_id
                next_pin_id += 1
                c_outputs.append({
                    "id": p_cid,
                    "node_id": cnode_id,
                    "name": f"ID [{comp_id}]",
                    "tooltip": f"Component ID {comp_id}",
                    "type": PIN_INT,
                    "dir": PIN_DIR_OUTPUT,
                    "default_value": str(comp_id)
                })

                comp_ref_inputs = {}
                check_targets = [comp]
                if payload:
                    check_targets.append(payload)

                for tgt in check_targets:
                    for ref_key, (ref_label, pin_type) in REF_FIELDS.items():
                        vals = tgt.get_all(ref_key)
                        for val in vals:
                            try:
                                target_id = int(val)
                                if target_id != 0:
                                    pin_in = next_pin_id
                                    next_pin_id += 1
                                    c_inputs.append({
                                        "id": pin_in,
                                        "node_id": cnode_id,
                                        "name": f"{ref_label} [{target_id}]",
                                        "tooltip": f"References Component {target_id}",
                                        "type": pin_type,
                                        "dir": PIN_DIR_INPUT,
                                        "default_value": str(target_id)
                                    })
                                    comp_ref_inputs[ref_key] = (target_id, pin_in)
                            except ValueError:
                                pass

                subtitle_parts = [f"ID: {comp_id}"]
                if payload:
                    if payload.get("Name"):
                        name_val = payload.get("Name")
                        subtitle_parts.append(name_val)
                        pid = next_pin_id
                        next_pin_id += 1
                        c_inputs.append({
                            "id": pid,
                            "node_id": cnode_id,
                            "name": f"Asset: {name_val}",
                            "tooltip": f"Asset Name: {name_val}",
                            "type": PIN_STRING,
                            "dir": PIN_DIR_INPUT,
                            "default_value": name_val
                        })

                    if payload.get("Intensity"):
                        intens = payload.get("Intensity")
                        pid = next_pin_id
                        next_pin_id += 1
                        c_inputs.append({
                            "id": pid,
                            "node_id": cnode_id,
                            "name": f"Intensity: {float(intens):.2f}",
                            "tooltip": f"Light intensity",
                            "type": PIN_FLOAT,
                            "dir": PIN_DIR_INPUT,
                            "default_value": str(intens)
                        })

                    if payload.get("DestinationSceneName"):
                        dest = payload.get("DestinationSceneName")
                        subtitle_parts.append(f"-> {dest}")
                        pid = next_pin_id
                        next_pin_id += 1
                        c_outputs.append({
                            "id": pid,
                            "node_id": cnode_id,
                            "name": f"Target: {dest}",
                            "tooltip": f"Destination level '{dest}.scene'",
                            "type": PIN_STRING,
                            "dir": PIN_DIR_OUTPUT,
                            "default_value": dest
                        })

                    if payload.get("TextureName"):
                        tex = payload.get("TextureName")
                        subtitle_parts.append(tex)
                        pid = next_pin_id
                        next_pin_id += 1
                        c_inputs.append({
                            "id": pid,
                            "node_id": cnode_id,
                            "name": f"Tex: {tex}",
                            "tooltip": f"Texture asset: {tex}",
                            "type": PIN_STRING,
                            "dir": PIN_DIR_INPUT,
                            "default_value": tex
                        })

                    on_collide = payload.find_first("OnCollide")
                    if on_collide and on_collide.get("String"):
                        lua_code = on_collide.get("String")
                        pid = next_pin_id
                        next_pin_id += 1
                        c_outputs.append({
                            "id": pid,
                            "node_id": cnode_id,
                            "name": "OnCollide Event",
                            "tooltip": "Lua Collision Script Callback",
                            "type": PIN_EXEC,
                            "dir": PIN_DIR_OUTPUT,
                            "default_value": ""
                        })
                        lua_script_nodes.append((cnode_id, lua_code, {}))

                all_comp_nodes[(ident, comp_id)] = {
                    "node_id": cnode_id,
                    "out_pin": p_cid,
                    "ref_inputs": comp_ref_inputs
                }

                category = CATEGORY_MAP.get(cls_name, "Components")
                cnode_data = {
                    "id": cnode_id,
                    "title": cls_name,
                    "subtitle": " · ".join(subtitle_parts),
                    "category": category,
                    "flags": 0,
                    "x": current_group_x + 310.0,
                    "y": comp_y,
                    "width": 230.0,
                    "height": max(80.0, 44.0 + max(len(c_inputs), len(c_outputs)) * 24.0),
                    "inputs": c_inputs,
                    "outputs": c_outputs
                }
                graph["nodes"].append(cnode_data)
                comp_nodes.append(cnode_data)
                comp_y += cnode_data["height"] + 20.0

            frame_h = max(160.0, comp_y - current_y + 10.0)
            if not components:
                frame_h = 150.0

            graph["comments"].append({
                "id": next_comment_id,
                "title": f"Object: {ident} [{tpl}]",
                "x": current_group_x,
                "y": current_y,
                "width": 580.0 if components else 320.0,
                "height": frame_h,
                "color": frame_color
            })
            next_comment_id += 1

            current_y += frame_h + 30.0

        current_group_x += group_spacing_x

    # 3. Wire Component Dependencies inside each Entity
    for (ident, comp_id), info in all_comp_nodes.items():
        to_node = info["node_id"]
        for ref_key, (target_id, to_pin) in info["ref_inputs"].items():
            if (ident, target_id) in all_comp_nodes:
                from_info = all_comp_nodes[(ident, target_id)]
                conn_id = next_conn_id
                next_conn_id += 1
                graph["connections"].append({
                    "id": conn_id,
                    "from_node": from_info["node_id"],
                    "from_pin": from_info["out_pin"],
                    "to_node": to_node,
                    "to_pin": to_pin
                })

    # 4. Wire Inter-Entity Lua Script References (Scene.Find("name"))
    find_pattern = re.compile(r'Scene\.Find\(["\']([a-zA-Z0-9_#]+)["\']\)')

    for cnode_id, lua_code, _ in lua_script_nodes:
        found_targets = find_pattern.findall(lua_code)
        for target_name in set(found_targets):
            if target_name in entity_ref_pins:
                target_pin_id = next_pin_id
                next_pin_id += 1

                for nd in graph["nodes"]:
                    if nd["id"] == cnode_id:
                        nd["inputs"].append({
                            "id": target_pin_id,
                            "node_id": cnode_id,
                            "name": f"Find: {target_name}",
                            "tooltip": f"Target entity '{target_name}' retrieved via Scene.Find()",
                            "type": PIN_OBJECT,
                            "dir": PIN_DIR_INPUT,
                            "default_value": target_name
                        })
                        nd["height"] = max(nd["height"], 44.0 + len(nd["inputs"]) * 24.0)
                        break

                conn_id = next_conn_id
                next_conn_id += 1
                graph["connections"].append({
                    "id": conn_id,
                    "from_node": entity_nodes[target_name],
                    "from_pin": entity_ref_pins[target_name],
                    "to_node": cnode_id,
                    "to_pin": target_pin_id
                })

    graph["next_node_id"] = next_node_id
    graph["next_pin_id"] = next_pin_id
    graph["next_conn_id"] = next_conn_id
    graph["next_comment_id"] = next_comment_id

    return graph


def main():
    parser = argparse.ArgumentParser(description="Convert FileRift .scene to Graphy Node Graph JSON")
    parser.add_argument("input_scene", help="Path to input .scene file")
    parser.add_argument("-o", "--output", help="Path to output .json file", default=None)
    args = parser.parse_args()

    if not os.path.exists(args.input_scene):
        print(f"Error: Input file '{args.input_scene}' not found.")
        sys.exit(1)

    with open(args.input_scene, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    scene_name = os.path.splitext(os.path.basename(args.input_scene))[0]
    root = parse_scene_text(text)
    graph_data = convert_scene_to_graph(root, scene_name=scene_name)

    out_path = args.output
    if not out_path:
        base, _ = os.path.splitext(args.input_scene)
        out_path = base + "_graph.json"

    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(graph_data, f, indent=2)

    node_count = len(graph_data["nodes"])
    conn_count = len(graph_data["connections"])
    comm_count = len(graph_data["comments"])
    print(f"Successfully converted '{args.input_scene}' -> '{out_path}'")
    print(f"  Nodes: {node_count}, Connections: {conn_count}, Comment Frames: {comm_count}")


if __name__ == "__main__":
    main()
