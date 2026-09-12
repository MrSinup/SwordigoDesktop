"""rubyforge.core.materials — Stage 4: Swordigo fixed-function materials.

Caver's mobile renderer is GLES 1.1 fixed-function: a diffuse texture
modulated by a diffuse colour, alpha from the material/texture with alpha-
test cut-outs for foliage and hair. No PBR. The viewport node built by the
Blender side reproduces exactly that (research report §12):

  * diffuse texture -> Base Colour
  * texture alpha   -> Alpha, blend mode CLIP (threshold 0.5)
  * Specular = 0, Roughness = 1, Metallic = 0

The pure helpers here are Blender-free so they can be unit-tested: the
material "spec" dict is the contract between them and the bpy node builder.
"""

from __future__ import annotations

import os
from typing import Dict, List, Optional, Sequence

__all__ = [
    "TEXTURE_EXTENSIONS",
    "sanitize_texture_name",
    "resolve_texture_path",
    "is_power_of_two",
    "texture_pot_warning",
    "swordigo_material_spec",
]

TEXTURE_EXTENSIONS = (".png", ".pvr", ".jpg", ".jpeg", ".tga", ".bmp")


def sanitize_texture_name(name: str) -> str:
    """Strip any directory component from a POD TexName (tag 4000).

    Texture filenames read from the asset are untrusted input; this is the
    path-traversal guard (research report §27).
    """
    name = (name or "").replace("\\\\", "/").strip()
    base = os.path.basename(name)
    base = base.replace("\x00", "").strip()
    # Reject anything that still looks like a traversal attempt.
    if ".." in base or base in ("", ".", "/"):
        return ""
    return base


def resolve_texture_path(tex_name: str,
                         search_dirs: Sequence[str]) -> Optional[str]:
    """Find a texture on disk for a POD texture filename.

    Tries the exact sanitized name in every search dir, then the same stem
    with each known texture extension, then a case-insensitive directory
    scan. Returns the first existing path or None.
    """
    base = sanitize_texture_name(tex_name)
    if not base:
        return None
    stem, ext = os.path.splitext(base)
    for d in search_dirs:
        if not d:
            continue
        exact = os.path.join(d, base)
        if os.path.isfile(exact):
            return exact
        if ext:
            continue  # an extension was already present; stem probing is noise
        for candidate_ext in TEXTURE_EXTENSIONS:
            cand = os.path.join(d, base + candidate_ext)
            if os.path.isfile(cand):
                return cand
        # Case-insensitive fallback (stock assets mix .POD/.pod style case).
        try:
            for entry in os.listdir(d):
                if entry.lower() == base.lower() and os.path.isfile(os.path.join(d, entry)):
                    return os.path.join(d, entry)
                entry_stem, entry_ext = os.path.splitext(entry)
                if entry_ext.lower() in TEXTURE_EXTENSIONS and entry_stem.lower() == stem.lower():
                    return os.path.join(d, entry)
        except OSError:
            continue
    return None


def is_power_of_two(n: int) -> bool:
    return n > 0 and (n & (n - 1)) == 0


def texture_pot_warning(width: int, height: int) -> Optional[str]:
    """Warning text for non-power-of-two textures (mobile GPU constraint)."""
    bad = []
    if not is_power_of_two(width):
        bad.append("width %d" % width)
    if not is_power_of_two(height):
        bad.append("height %d" % height)
    if not bad:
        return None
    return "texture is not power-of-two (%s)" % ", ".join(bad)


def swordigo_material_spec(name: str,
                           diffuse: Sequence[float] = (1.0, 1.0, 1.0),
                           opacity: float = 1.0,
                           texture_path: Optional[str] = None) -> Dict:
    """The viewport-material contract consumed by the bpy node builder."""
    return {
        "name": name or "Default",
        "diffuse": [float(diffuse[0]), float(diffuse[1]), float(diffuse[2])],
        "opacity": float(opacity) if opacity > 0.0 else 1.0,
        "texture_path": texture_path,
        "blend_mode": "CLIP",       # GLES 1.1 alpha-test parity
        "alpha_clip": 0.5,
        "specular": 0.0,
        "roughness": 1.0,
        "metallic": 0.0,
    }
