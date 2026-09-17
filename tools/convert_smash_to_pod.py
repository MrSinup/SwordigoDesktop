#!/usr/bin/env python3
"""
convert_to_pod.py — Convert Smash Royale characters to Swordigo POD + Anim PODs + PVR textures.

Usage:
    python3 convert_to_pod.py [character_name] [output_directory] [--scale FACTOR]

Examples:
    python3 convert_to_pod.py mario /home/quantumcreeper/test --scale 50
    python3 convert_to_pod.py wario /home/quantumcreeper/test --scale 50
    python3 convert_to_pod.py bowser /home/quantumcreeper/test --scale 50
    python3 convert_to_pod.py all /home/quantumcreeper/test --scale 50
"""

import sys
import os
import glob
import json
import struct
import subprocess
import tempfile
import argparse

SMASH_ROOT = "/home/quantumcreeper/smario/smashroyale.io"
PORTABLE_DIR = os.path.join(SMASH_ROOT, "art/resumption-20260907/release-optimization/opponent512-batch")
RUBY_BIN = "/home/quantumcreeper/SwordigoDesktop/bin/ruby"
DEFAULT_OUT = "/home/quantumcreeper/test"
DEFAULT_SCALE = 50.0  # 1.48m * 50 = ~74 units (Swordigo Hero Ash scale is 74.2u)

def inject_motions_into_glb(glb_path, motions_path, output_glb_path):
    with open(glb_path, 'rb') as f:
        magic, version, total_len = struct.unpack('<4sII', f.read(12))
        if magic != b'glTF' or version != 2:
            raise ValueError(f"Invalid glTF 2.0 file: {glb_path}")
        
        json_len, json_type = struct.unpack('<II', f.read(8))
        if json_type != 0x4E4F534A:
            raise ValueError(f"Expected JSON chunk in {glb_path}")
        gltf = json.loads(f.read(json_len).decode('utf-8'))
        
        bin_len, bin_type = struct.unpack('<II', f.read(8))
        if bin_type != 0x004E4942:
            raise ValueError(f"Expected BIN chunk in {glb_path}")
        bin_data = bytearray(f.read(bin_len))
        
    with open(motions_path, 'r', encoding='utf-8') as f:
        motions = json.load(f)
        
    node_name_to_idx = {node.get('name'): i for i, node in enumerate(gltf.get('nodes', [])) if 'name' in node}
    
    if 'animations' not in gltf:
        gltf['animations'] = []
    if 'accessors' not in gltf:
        gltf['accessors'] = []
    if 'bufferViews' not in gltf:
        gltf['bufferViews'] = []
    if 'buffers' not in gltf or not gltf['buffers']:
        gltf['buffers'] = [{'byteLength': len(bin_data)}]
        
    def append_data(data_bytes):
        pad = (4 - (len(bin_data) % 4)) % 4
        bin_data.extend(b'\x00' * pad)
        offset = len(bin_data)
        bin_data.extend(data_bytes)
        
        bv_idx = len(gltf['bufferViews'])
        gltf['bufferViews'].append({
            'buffer': 0,
            'byteOffset': offset,
            'byteLength': len(data_bytes)
        })
        return bv_idx

    clips = motions.get('clips', [])
    for clip in clips:
        clip_name = clip.get('name', 'anim')
        tracks = clip.get('tracks', [])
        
        channels = []
        samplers = []
        
        for track in tracks:
            full_name = track.get('name', '')
            if '.' not in full_name:
                continue
            bone_name, prop = full_name.rsplit('.', 1)
            if bone_name not in node_name_to_idx:
                continue
            node_idx = node_name_to_idx[bone_name]
            
            if prop == 'position':
                target_path = 'translation'
                val_type = 'VEC3'
                comps = 3
            elif prop == 'quaternion':
                target_path = 'rotation'
                val_type = 'VEC4'
                comps = 4
            elif prop == 'scale':
                target_path = 'scale'
                val_type = 'VEC3'
                comps = 3
            else:
                continue
                
            times = track.get('times', [])
            values = track.get('values', [])
            if not times or not values or len(values) != len(times) * comps:
                continue
                
            times_bytes = struct.pack(f'<{len(times)}f', *times)
            bv_times = append_data(times_bytes)
            acc_times_idx = len(gltf['accessors'])
            gltf['accessors'].append({
                'bufferView': bv_times,
                'byteOffset': 0,
                'componentType': 5126, # FLOAT
                'count': len(times),
                'type': 'SCALAR',
                'min': [min(times)],
                'max': [max(times)]
            })
            
            vals_bytes = struct.pack(f'<{len(values)}f', *values)
            bv_vals = append_data(vals_bytes)
            acc_vals_idx = len(gltf['accessors'])
            gltf['accessors'].append({
                'bufferView': bv_vals,
                'byteOffset': 0,
                'componentType': 5126, # FLOAT
                'count': len(times),
                'type': val_type
            })
            
            sampler_idx = len(samplers)
            samplers.append({
                'input': acc_times_idx,
                'interpolation': 'LINEAR',
                'output': acc_vals_idx
            })
            
            channels.append({
                'sampler': sampler_idx,
                'target': {
                    'node': node_idx,
                    'path': target_path
                }
            })
            
        if channels:
            gltf['animations'].append({
                'name': clip_name,
                'channels': channels,
                'samplers': samplers
            })
            
    gltf['buffers'][0]['byteLength'] = len(bin_data)
    
    json_bytes = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
    json_pad = (4 - (len(json_bytes) % 4)) % 4
    json_bytes += b' ' * json_pad
    
    bin_pad = (4 - (len(bin_data) % 4)) % 4
    bin_data.extend(b'\x00' * bin_pad)
    
    new_total_len = 12 + 8 + len(json_bytes) + 8 + len(bin_data)
    
    os.makedirs(os.path.dirname(os.path.abspath(output_glb_path)), exist_ok=True)
    with open(output_glb_path, 'wb') as f:
        f.write(struct.pack('<4sII', b'glTF', 2, new_total_len))
        f.write(struct.pack('<II', len(json_bytes), 0x4E4F534A))
        f.write(json_bytes)
        f.write(struct.pack('<II', len(bin_data), 0x004E4942))
        f.write(bin_data)
        
    return len(gltf['animations'])

def convert_character(char_name, out_dir=DEFAULT_OUT, scale=DEFAULT_SCALE):
    char_dir = os.path.join(PORTABLE_DIR, char_name, "portable")
    if not os.path.exists(char_dir):
        matches = glob.glob(os.path.join(SMASH_ROOT, f"**/{char_name}/**/model.glb"), recursive=True)
        if matches:
            char_dir = os.path.dirname(matches[0])
        else:
            print(f"[-] Character '{char_name}' not found!")
            return False

    glb_file = os.path.join(char_dir, "model.glb")
    motions_file = os.path.join(char_dir, "motions.json")
    
    if not os.path.exists(glb_file):
        print(f"[-] model.glb not found in {char_dir}")
        return False
        
    os.makedirs(out_dir, exist_ok=True)
    out_pod = os.path.join(out_dir, f"{char_name}.POD")
    
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_glb = os.path.join(tmp_dir, f"{char_name}_animated.glb")
        
        if os.path.exists(motions_file):
            print(f"[+] Found motions.json for '{char_name}'. Injecting animation tracks...")
            num_clips = inject_motions_into_glb(glb_file, motions_file, tmp_glb)
            print(f"    -> Injected {num_clips} clips into staging GLB")
            target_glb = tmp_glb
        else:
            print(f"[!] No companion motions.json for '{char_name}'. Converting static rig.")
            target_glb = glb_file
            
        cmd = [
            RUBY_BIN,
            "--glb2pod",
            target_glb,
            out_pod,
            "--force",
            "--anim-fps", "24",
            "--scale", str(scale)
        ]
        
        print(f"[+] Running Ruby CLI (scale={scale}x): {' '.join(cmd)}")
        res = subprocess.run(cmd, capture_output=True, text=True)
        print(res.stdout)
        if res.stderr:
            print(res.stderr)
            
        if res.returncode != 0:
            print(f"[-] Ruby conversion failed for {char_name}!")
            return False
            
    print(f"[✓] Successfully exported {char_name} (scale: {scale}x) to {out_dir}\n")
    return True

def list_available_characters():
    if not os.path.exists(PORTABLE_DIR):
        return []
    return sorted([d for d in os.listdir(PORTABLE_DIR) if os.path.isdir(os.path.join(PORTABLE_DIR, d, "portable"))])

def main():
    parser = argparse.ArgumentParser(description="Convert Smash Royale GLB models to Swordigo POD + Anim PODs.")
    parser.add_argument("character", nargs="?", default="mario", help="Character name, 'all', or '--list'")
    parser.add_argument("output_dir", nargs="?", default=DEFAULT_OUT, help=f"Output directory (default: {DEFAULT_OUT})")
    parser.add_argument("--scale", "-s", type=float, default=DEFAULT_SCALE, help=f"Scale multiplier (default: {DEFAULT_SCALE}x for Swordigo Hero ~74u size)")
    parser.add_argument("--list", "-l", action="store_true", help="List all available characters")

    args = parser.parse_args()

    if args.list or args.character.lower() in ("--list", "-l", "list"):
        chars = list_available_characters()
        print(f"Available Smash Royale characters ({len(chars)}):")
        for c in chars:
            print(f"  - {c}")
        sys.exit(0)

    if args.character.lower() == "all":
        chars = list_available_characters()
        print(f"Converting all {len(chars)} characters with scale={args.scale}x to {args.output_dir}...")
        for c in chars:
            try:
                convert_character(c, args.output_dir, scale=args.scale)
            except Exception as e:
                print(f"[-] Error converting {c}: {e}")
    else:
        convert_character(args.character, args.output_dir, scale=args.scale)

if __name__ == "__main__":
    main()
