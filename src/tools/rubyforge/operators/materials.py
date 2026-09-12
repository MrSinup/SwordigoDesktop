# rubyforge.operators.materials — Stage 4: viewport material node builder.
#
# Builds the bpy side of the contract described by
# rubyforge.core.materials.swordigo_material_spec(): a GLES 1.1
# fixed-function look (diffuse texture, alpha clip, zero specular).

from __future__ import annotations

from ..core.materials import sanitize_texture_name, swordigo_material_spec

try:
    import bpy
    HAS_BPY = True
except Exception:  # pragma: no cover
    bpy = None
    HAS_BPY = False


def apply_material_spec(bmat, spec):
    """Wire a Principled node group to mirror the mobile fixed-function look."""
    bmat.diffuse_color = (*spec["diffuse"], 1.0)
    try:
        bmat.use_nodes = True
    except Exception:
        return
    tree = bmat.node_tree
    bsdf = None
    for node in tree.nodes:
        if node.type == "BSDF_PRINCIPLED":
            bsdf = node
            break
    if bsdf is None:
        bsdf = tree.nodes.new("ShaderNodeBsdfPrincipled")
        out = tree.nodes.get("Material Output") or tree.nodes.new("ShaderNodeOutputMaterial")
        tree.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])

    bsdf.inputs["Specular"].default_value = spec["specular"]
    bsdf.inputs["Roughness"].default_value = spec["roughness"]
    if "Metallic" in bsdf.inputs:
        bsdf.inputs["Metallic"].default_value = spec["metallic"]
    bsdf.inputs["Base Color"].default_value = (*spec["diffuse"], 1.0)

    tex_path = spec.get("texture_path")
    if tex_path and os.path.isfile(tex_path):
        tex = tree.nodes.get("SwordigoDiffuse")
        if tex is None:
            tex = tree.nodes.new("ShaderNodeTexImage")
            tex.name = "SwordigoDiffuse"
            tex.label = "Swordigo Diffuse"
            tex.location = (-400, 0)
        image = bpy.data.images.load(tex_path, check_existing=True)
        tex.image = image
        tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        if "Alpha" in bsdf.inputs:
            tree.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])

    # Alpha-test parity: CLIP blend (or EEVEE Next hashed) at 0.5.
    try:
        bmat.blend_method = spec["blend_mode"]
        bmat.alpha_threshold = spec["alpha_clip"]
    except Exception:
        pass


def material_for_pod(pod_material, search_dirs):
    """Create (or reuse) a bpy material from a POD material + texture dirs."""
    from ..core.materials import resolve_texture_path
    tex_name = ""
    idx = pod_material.diffuse_texture_index
    if 0 <= idx:
        # The caller passes the model's texture list via search context;
        # here we only get the name through the closure-free API below.
        tex_name = ""
    spec = swordigo_material_spec(pod_material.name, pod_material.diffuse,
                                  pod_material.opacity, None)
    bmat = bpy.data.materials.new(spec["name"])
    apply_material_spec(bmat, spec)
    return bmat


def material_with_texture(pod_material, texture_name, search_dirs):
    """Material + resolved texture path (Stage 4 import path)."""
    from ..core.materials import resolve_texture_path
    tex_path = resolve_texture_path(sanitize_texture_name(texture_name), search_dirs)
    spec = swordigo_material_spec(pod_material.name, pod_material.diffuse,
                                  pod_material.opacity, tex_path)
    bmat = bpy.data.materials.new(spec["name"])
    apply_material_spec(bmat, spec)
    return bmat, tex_path


import os  # noqa: E402  (kept last: only needed by apply_material_spec)
