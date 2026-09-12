#!/usr/bin/env python3
"""
scl_to_graph.py — Convert FileRift SCL (Swordigo ObjectLibrary markup) to Graphy JSON.

Usage:
    python3 tools/scl_to_graph.py <input.scl> [-o <output.json>]
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

# ── Component Category Color Mappings ────────────────────────────────────────
CATEGORY_MAP = {
    "Model": "Rendering",
    "ParticleEmitter": "FX / Particles",
    "Particle": "FX / Particles",
    "CollisionShape": "Collision",
    "UtilityShape": "Collision",
    "ShapeComponent": "Collision",
    "PhysicsObject": "Physics",
    "TransformController": "Transform",
    "SimpleGlow": "Lighting",
    "Light": "Lighting",
    "Program": "Scripting",
    "Properties": "Properties",
    "DoorController": "Gameplay",
    "KeyframeAnimation": "Animation",
    "AnimationController": "Animation",
    "SoundEffect": "Audio",
    "SpawnPoint": "Level",
}

# ── Reference Field Mapping (Fields that refer to another component's ID) ────
REF_FIELDS = {
    "ParticleId": ("Particle ID", PIN_INT),
    "ModelBindingId": ("Model Binding ID", PIN_INT),
    "ModelId": ("Model ID", PIN_INT),
    "AnimationControllerId": ("Anim Controller ID", PIN_INT),
    "AnimationId": ("Animation ID", PIN_INT),
    "CloseSoundId": ("Close Sound ID", PIN_INT),
    "OpenSoundId": ("Open Sound ID", PIN_INT),
    "ParentComponentIdentifier": ("Parent Component ID", PIN_INT),
    "DefaultAnimationId": ("Default Anim ID", PIN_INT),
}


class SclBlock:
    def __init__(self, name):
        self.name = name
        self.scalars = {}       # key -> single value or list of values
        self.children = []      # list of SclBlock

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


def parse_scl_text(text: str) -> SclBlock:
    """Parses FileRift markup into a tree of SclBlock objects."""
    root = SclBlock("Root")
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

        # Check multiline Lua string start: e.g. "String : $"
        if stripped.endswith("String : $") or ": $" in stripped:
            colon_idx = stripped.find(":")
            key = stripped[:colon_idx].strip()
            # Consume until $end
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

        # Check block close
        if stripped == "}":
            if len(stack) > 1:
                stack.pop()
            i += 1
            continue

        # Check block open: "Template{" or "Object {" or "Component {"
        brace_idx = stripped.find("{")
        if brace_idx != -1 and not stripped.endswith("$"):
            block_name = stripped[:brace_idx].strip()
            new_block = SclBlock(block_name)
            stack[-1].children.append(new_block)
            stack.append(new_block)
            i += 1
            continue

        # Check scalar: "Identifier : 'questvase'" or "Radius : 150"
        colon_idx = stripped.find(":")
        if colon_idx != -1:
            key = stripped[:colon_idx].strip()
            val_raw = stripped[colon_idx + 1:].strip()

            # Clean quotes
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


def convert_scl_to_graph(scl_root: SclBlock) -> dict:
    """Converts the parsed SclBlock tree into a Graphy JSON document."""
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

    templates = scl_root.find_all("Template")
    if not templates:
        objs = scl_root.find_all("Object")
        if objs:
            dummy_tpl = SclBlock("Template")
            dummy_tpl.children = objs
            templates = [dummy_tpl]

    next_node_id = 1
    next_pin_id = 100
    next_conn_id = 500
    next_comment_id = 900

    comment_x = 40.0
    comment_y = 50.0

    for tpl in templates:
        obj = tpl.find_first("Object")
        if not obj:
            continue

        template_id = obj.get("Identifier", "UnnamedTemplate")
        components = obj.find_all("Component")

        cols = {
            "resources": [],
            "emitters": [],
            "physics": [],
            "scripts": []
        }

        comp_registry = {}

        for comp in components:
            cls_name = comp.get("ClassName", "Component")
            comp_id_str = comp.get("Identifier", "0")
            try:
                comp_id = int(comp_id_str)
            except ValueError:
                comp_id = next_node_id

            node_id = next_node_id
            next_node_id += 1

            category = CATEGORY_MAP.get(cls_name, "Generic")

            payload_block = None
            for child in comp.children:
                if child.name.endswith("Component") or child.name in ["ShapeComponent", "Emitter", "Program"]:
                    payload_block = child
                    break

            subtitle_parts = [f"ID: {comp_id}"]
            if payload_block:
                if payload_block.get("Name"):
                    subtitle_parts.append(str(payload_block.get("Name")))
                elif payload_block.get("TextureName"):
                    subtitle_parts.append(str(payload_block.get("TextureName")))

            subtitle = " · ".join(subtitle_parts)

            inputs = []
            outputs = []

            out_pin_id = next_pin_id
            next_pin_id += 1
            outputs.append({
                "id": out_pin_id,
                "node_id": node_id,
                "name": "Component ID",
                "tooltip": f"ID {comp_id}",
                "type": PIN_INT,
                "dir": PIN_DIR_OUTPUT,
                "default_value": str(comp_id)
            })

            ref_inputs = {}
            if payload_block:
                for ref_key, (pin_label, pin_type) in REF_FIELDS.items():
                    vals = payload_block.get_all(ref_key)
                    for val in vals:
                        try:
                            target_ref_id = int(val)
                            if target_ref_id != 0:
                                in_pin_id = next_pin_id
                                next_pin_id += 1
                                inputs.append({
                                    "id": in_pin_id,
                                    "node_id": node_id,
                                    "name": f"{pin_label} [{target_ref_id}]",
                                    "tooltip": f"References Component ID {target_ref_id}",
                                    "type": pin_type,
                                    "dir": PIN_DIR_INPUT,
                                    "default_value": str(target_ref_id)
                                })
                                if ref_key not in ref_inputs:
                                    ref_inputs[ref_key] = []
                                ref_inputs[ref_key].append((target_ref_id, in_pin_id))
                        except ValueError:
                            pass

                prop_keys = [
                    ("TextureName", "Texture", PIN_STRING),
                    ("Size", "Size", PIN_FLOAT),
                    ("MaxParticles", "Max Particles", PIN_INT),
                    ("EmissionFactor", "Emission", PIN_FLOAT),
                    ("PhysicsEnabled", "Physics", PIN_BOOLEAN),
                    ("PulseTime", "Pulse Time", PIN_FLOAT),
                    ("PulseAmount", "Pulse Amount", PIN_FLOAT),
                ]
                for pkey, plabel, ptype in prop_keys:
                    if pkey in payload_block.scalars:
                        pval = str(payload_block.get(pkey))
                        pid = next_pin_id
                        next_pin_id += 1
                        inputs.append({
                            "id": pid,
                            "node_id": node_id,
                            "name": plabel,
                            "tooltip": f"{pkey}: {pval}",
                            "type": ptype,
                            "dir": PIN_DIR_INPUT,
                            "default_value": pval
                        })

            comp_registry[comp_id] = {
                "node_id": node_id,
                "out_pin_id": out_pin_id,
                "ref_inputs": ref_inputs
            }

            node_data = {
                "id": node_id,
                "title": cls_name,
                "subtitle": subtitle,
                "category": category,
                "flags": 0,
                "x": 0.0,
                "y": 0.0,
                "width": 210.0,
                "height": max(80.0, 36.0 + max(len(inputs), len(outputs)) * 24.0),
                "inputs": inputs,
                "outputs": outputs
            }

            if cls_name in ["Particle", "Model"]:
                cols["resources"].append(node_data)
            elif cls_name in ["ParticleEmitter", "CollisionShape", "UtilityShape"]:
                cols["emitters"].append(node_data)
            elif cls_name in ["PhysicsObject", "TransformController", "SimpleGlow"]:
                cols["physics"].append(node_data)
            else:
                cols["scripts"].append(node_data)

            graph["nodes"].append(node_data)

        col_x = comment_x + 30.0
        max_col_h = 0.0

        for col_name in ["resources", "emitters", "physics", "scripts"]:
            node_list = cols[col_name]
            cur_y = comment_y + 60.0
            for nd in node_list:
                nd["x"] = col_x
                nd["y"] = cur_y
                cur_y += nd["height"] + 25.0
            max_col_h = max(max_col_h, cur_y - comment_y)
            if node_list:
                col_x += 270.0

        for comp_id, info in comp_registry.items():
            to_node = info["node_id"]
            for ref_key, targets in info["ref_inputs"].items():
                for (target_comp_id, to_pin) in targets:
                    if target_comp_id in comp_registry:
                        from_node = comp_registry[target_comp_id]["node_id"]
                        from_pin  = comp_registry[target_comp_id]["out_pin_id"]
                        conn_id = next_conn_id
                        next_conn_id += 1
                        graph["connections"].append({
                            "id": conn_id,
                            "from_node": from_node,
                            "from_pin": from_pin,
                            "to_node": to_node,
                            "to_pin": to_pin
                        })

        frame_w = max(400.0, col_x - comment_x + 30.0)
        frame_h = max(300.0, max_col_h + 30.0)

        graph["comments"].append({
            "id": next_comment_id,
            "title": f"SCL Template: {template_id}",
            "x": comment_x,
            "y": comment_y,
            "width": frame_w,
            "height": frame_h,
            "color": "#8c1e3250"
        })
        next_comment_id += 1
        comment_y += frame_h + 50.0

    graph["next_node_id"] = next_node_id
    graph["next_pin_id"] = next_pin_id
    graph["next_conn_id"] = next_conn_id
    graph["next_comment_id"] = next_comment_id

    return graph


def main():
    parser = argparse.ArgumentParser(description="Convert FileRift SCL to Graphy Node Graph JSON")
    parser.add_argument("input_scl", help="Path to input .scl file")
    parser.add_argument("-o", "--output", help="Path to output .json file", default=None)
    args = parser.parse_args()

    if not os.path.exists(args.input_scl):
        print(f"Error: Input file '{args.input_scl}' not found.")
        sys.exit(1)

    with open(args.input_scl, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    root = parse_scl_text(text)
    graph_data = convert_scl_to_graph(root)

    out_path = args.output
    if not out_path:
        base, _ = os.path.splitext(args.input_scl)
        out_path = base + "_graph.json"

    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(graph_data, f, indent=2)

    node_count = len(graph_data["nodes"])
    conn_count = len(graph_data["connections"])
    comm_count = len(graph_data["comments"])
    print(f"Successfully converted '{args.input_scl}' -> '{out_path}'")
    print(f"  Nodes: {node_count}, Connections: {conn_count}, Comment Frames: {comm_count}")


if __name__ == "__main__":
    main()
