// pod_writer.cpp — PowerVR POD model serializer implementation
// Mirrors the chunk/tag grammar read by pod_loader.cpp (PODLoader.as).

#include "pod_writer.h"
#include "pod_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace av {

// ─── Tag Constants (must match pod_loader.cpp) ─────────────────────────
namespace {
constexpr uint32_t kEndTagMask = 0x80000000u;

constexpr uint32_t eFormatVersion               = 1000;
constexpr uint32_t eScene                       = 1001;

constexpr uint32_t eSceneClearColour            = 2000;
constexpr uint32_t eSceneAmbientColour          = 2001;
constexpr uint32_t eSceneNumCameras             = 2002;
constexpr uint32_t eSceneNumLights              = 2003;
constexpr uint32_t eSceneNumMeshes              = 2004;
constexpr uint32_t eSceneNumNodes               = 2005;
constexpr uint32_t eSceneNumMeshNodes           = 2006;
constexpr uint32_t eSceneNumTextures            = 2007;
constexpr uint32_t eSceneNumMaterials           = 2008;
constexpr uint32_t eSceneNumFrames              = 2009;
constexpr uint32_t eSceneMesh                   = 2012;
constexpr uint32_t eSceneNode                   = 2013;
constexpr uint32_t eSceneTexture                = 2014;
constexpr uint32_t eSceneMaterial               = 2015;
constexpr uint32_t eSceneFlags                  = 2016;
constexpr uint32_t eSceneFPS                    = 2017;

constexpr uint32_t eMaterialName                 = 3000;
constexpr uint32_t eMaterialDiffuseTextureIndex  = 3001;
constexpr uint32_t eMaterialOpacity              = 3002;
constexpr uint32_t eMaterialAmbient              = 3003;
constexpr uint32_t eMaterialDiffuse              = 3004;
constexpr uint32_t eMaterialSpecular             = 3005;
constexpr uint32_t eMaterialShininess            = 3006;
// The remaining material tags the *runtime* reader consumes, at the struct
// offsets it stores them to (sub_580AB8, cases 0xBC1..0xBD2 = 3009..3026):
//
//   3009..3017  v+12 .. v+44     nine auxiliary texture slots
//   3018..3023  v+112 .. v+132   material flags / blend equation
//   3024, 3025  v+136, v+152     two 16-byte blocks
//   3026        v+168            one flag
//
// A stock export always writes all of them (see resources/rock1.POD).  Ours
// previously omitted them, which left the nine texture slots at their calloc'd
// 0 — a *valid* texture index — instead of the sentinel -1 stock uses for
// "this material has no texture of that kind".
constexpr uint32_t eMaterialTexSlotBase          = 3009;
constexpr uint32_t eMaterialFlagBase             = 3018;
constexpr uint32_t eMaterialBlockA               = 3024;
constexpr uint32_t eMaterialBlockB               = 3025;
constexpr uint32_t eMaterialTailFlag             = 3026;
// Values taken verbatim from the game's own resources/rock1.POD.
constexpr uint32_t kMaterialTexSlotNone          = 0xFFFFFFFFu;
constexpr uint32_t kMaterialBlendEquationAdd     = 0x8006u; // GL_FUNC_ADD

constexpr uint32_t eTextureFilename              = 4000;

constexpr uint32_t eNodeIndex                    = 5000;
constexpr uint32_t eNodeName                     = 5001;
constexpr uint32_t eNodeMaterialIndex            = 5002;
constexpr uint32_t eNodeParentIndex              = 5003;
constexpr uint32_t eNodePosition                 = 5004;
constexpr uint32_t eNodeRotation                 = 5005;
constexpr uint32_t eNodeScale                    = 5006;
constexpr uint32_t eNodeAnimationPosition        = 5007;
constexpr uint32_t eNodeAnimationRotation        = 5008;
constexpr uint32_t eNodeAnimationScale           = 5009;
// 5010 eNodeMatrix is a NO-OP in the runtime reader: its handler is
// `case 0x1392u: goto LABEL_202;`, which skips the 64-byte payload and stores
// nothing at all.  It must never be written.  Use eNodeAnimationMatrix (5011),
// which the reader stores at node+88 and GetWorldMatrixNoCache consumes
// verbatim as the node's local matrix.
constexpr uint32_t eNodeMatrixUnsupported        = 5010;
constexpr uint32_t eNodeAnimationMatrix           = 5011;
constexpr uint32_t eNodeAnimationFlags            = 5012;
constexpr uint32_t eNodeAnimationPositionIndex   = 5013;
constexpr uint32_t eNodeAnimationRotationIndex   = 5014;
constexpr uint32_t eNodeAnimationScaleIndex      = 5015;
constexpr uint32_t eNodeAnimationMatrixIndex     = 5016;

constexpr uint32_t eMeshNumVertices              = 6000;
constexpr uint32_t eMeshNumFaces                 = 6001;
constexpr uint32_t eMeshNumUVWChannels            = 6002;
constexpr uint32_t eMeshVertexIndexList          = 6003;
constexpr uint32_t eMeshStripLengths             = 6004;
constexpr uint32_t eMeshNumStrips                = 6005;
constexpr uint32_t eMeshVertexList               = 6006;
constexpr uint32_t eMeshNormalList               = 6007;
constexpr uint32_t eMeshTangentList              = 6008;
constexpr uint32_t eMeshBinormalList             = 6009;
constexpr uint32_t eMeshUVWList                  = 6010;
constexpr uint32_t eMeshColorList                = 6011;
constexpr uint32_t eMeshBoneIndexList            = 6012;
constexpr uint32_t eMeshBoneWeightList            = 6013;
constexpr uint32_t eMeshInterleavedData          = 6014;
constexpr uint32_t eMeshBoneBatchIndexList       = 6015;
constexpr uint32_t eMeshNumBoneIndicesPerBatch   = 6016;
constexpr uint32_t eMeshBoneOffsetPerBatch       = 6017;
constexpr uint32_t eMeshMaxNumBonesPerBatch      = 6018;
constexpr uint32_t eMeshNumBoneBatches           = 6019;
constexpr uint32_t eMeshUnpackMatrix             = 6020;

// Tags the *runtime* reader understands inside a mesh, in the order a stock
// PVRGeoPOD export writes them (verified against
// PVRShaman/Example/POD/OGL/*.pod and the game's own resources):
//
//   6000, 6001, 6002, 6005, [6014], 6018, 6019, [6015, 6016, 6017],
//   6020, 6003, 6006, 6007, 6008, 6009, 6010, 6011, [6012, 6013]
//
// 6008/6009/6011 are always present even when empty (n == 0).  The reader
// (`sub_58A504`, tag 9001) stores n, and Caver::PODLoader::CreateMesh skips
// any stream whose n is 0, so an absent tag and an empty tag are behaviourally
// identical — but writing them keeps our output byte-diffable against stock.

constexpr uint32_t eBlockDataType                = 9000;
constexpr uint32_t eBlockNumComponents           = 9001;
constexpr uint32_t eBlockStride                  = 9002;
constexpr uint32_t eBlockData                    = 9003;

// 1002/1003 sit between the version block and the scene block in every stock
// export.  Neither is read by libswordigo (both are unknown tags, so the
// reader's default arm skips them), but they are part of what a stock file
// looks like, so we emit the same shape.
constexpr uint32_t eExporterOptions             = 1002;
constexpr uint32_t eExporterProvenance          = 1003;
constexpr uint32_t eSceneUserData               = 2010;
} // namespace

// ─── Byte emitter ──────────────────────────────────────────────────────
namespace {

class Sink {
public:
    void u32(uint32_t v) {
        buf_.push_back(static_cast<uint8_t>(v));
        buf_.push_back(static_cast<uint8_t>(v >> 8));
        buf_.push_back(static_cast<uint8_t>(v >> 16));
        buf_.push_back(static_cast<uint8_t>(v >> 24));
    }
    void f32(float v) { std::memcpy(tmp_, &v, 4); u32(tmp_[0] | (tmp_[1] << 8) | (tmp_[2] << 16) | (tmp_[3] << 24)); }
    void bytes(const void* data, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        buf_.insert(buf_.end(), p, p + n);
    }
    void floats(const std::vector<float>& v) { if (!v.empty()) bytes(v.data(), v.size() * sizeof(float)); }
    void u32s(const std::vector<uint32_t>& v) { if (!v.empty()) bytes(v.data(), v.size() * sizeof(uint32_t)); }

    // ── Block grammar ───────────────────────────────────────────────────
    //
    // EVERY block, leaf or container, is delimited by an explicit close pair:
    //
    //     <tag:u32> <len:u32> <len payload bytes> <tag|0x80000000> <0:u32>
    //
    // That is what every stock writer emits — vanilla Swordigo assets
    // (resources/rock1.POD, resources/hiro.POD), the PVRGeoPOD 2021 R2 sample
    // exports under PVRShaman/Example/POD and the PVRGeoPOD Example tree all
    // carry the close pair on *leaves* too, not only on containers.  The
    // runtime happens to tolerate its absence (an unknown tag simply has its
    // declared length skipped), but a file without it is not a stock POD and
    // any stricter reader desynchronises, so we write it everywhere.
    void end_tag(uint32_t tag) { u32(tag | kEndTagMask); u32(0); }
    void begin(uint32_t tag) {
        u32(tag);
        u32(0); // container blocks carry a zero length field
    }
    void finish(uint32_t tag) { end_tag(tag); }

    // A complete leaf written in one call: tag, length, payload, close pair.
    void field(uint32_t tag, const void* payload, size_t n) {
        u32(tag);
        u32(static_cast<uint32_t>(n));
        if (n) bytes(payload, n);
        end_tag(tag);
    }
    void field_u32(uint32_t tag, uint32_t v) {
        uint8_t b[4] = {static_cast<uint8_t>(v),        static_cast<uint8_t>(v >> 8),
                        static_cast<uint8_t>(v >> 16),  static_cast<uint8_t>(v >> 24)};
        field(tag, b, 4);
    }
    void field_f32(uint32_t tag, float v) {
        uint32_t w;
        std::memcpy(&w, &v, 4);
        field_u32(tag, w);
    }
    void field_floats(uint32_t tag, const std::vector<float>& v) {
        field(tag, v.empty() ? nullptr : v.data(), v.size() * sizeof(float));
    }
    void field_u32s(uint32_t tag, const std::vector<uint32_t>& v) {
        field(tag, v.empty() ? nullptr : v.data(), v.size() * sizeof(uint32_t));
    }
    // Strings are NUL-terminated *and* the length includes that NUL, exactly
    // as the SDK writes them (e.g. rock1.POD's mat.name has len = 13 for
    // "01 - Default" + '\0').
    void field_str(uint32_t tag, const std::string& s) {
        u32(tag);
        u32(static_cast<uint32_t>(s.size() + 1));
        if (!s.empty()) bytes(s.data(), s.size());
        buf_.push_back(0);
        end_tag(tag);
    }

    const std::vector<uint8_t>& data() const { return buf_; }

    // Free-form helpers used only for payloads assembled inline.

private:
    std::vector<uint8_t> buf_;
    uint8_t tmp_[4];
};

void write_vertex_block(Sink& out, uint32_t block_id, const std::vector<float>& data, int components) {
    if (data.empty() || components <= 0) return;
    out.begin(block_id);
    out.field_u32(eBlockDataType, 1);                                  // float
    out.field_u32(eBlockNumComponents, static_cast<uint32_t>(components));
    out.field_u32(eBlockStride, static_cast<uint32_t>(components * 4));
    out.field_floats(eBlockData, data);
    out.finish(block_id);
}

void write_bone_index_block(Sink& out, const std::vector<float>& data, int components) {
    if (data.empty() || components <= 0) return;
    std::vector<uint32_t> ints;
    ints.reserve(data.size());
    for (float f : data) ints.push_back(static_cast<uint32_t>(static_cast<int32_t>(std::lround(f))));
    out.begin(eMeshBoneIndexList);
    out.field_u32(eBlockDataType, 2);                                  // int
    out.field_u32(eBlockNumComponents, static_cast<uint32_t>(components));
    out.field_u32(eBlockStride, static_cast<uint32_t>(components * 4));
    out.field_u32s(eBlockData, ints);
    out.finish(eMeshBoneIndexList);
}

void write_index_block(Sink& out, const std::vector<uint32_t>& indices) {
    if (indices.empty()) return;
    // Index-width preservation (pod_master/03 §5, 09 §5): stock meshes store
    // indices as UNSIGNED_SHORT (eType 3, 2 bytes). That is only valid when
    // every index fits in a u16 — blindly truncating (idx & 0xFFFF) corrupts
    // any mesh with >65535 vertices. Pick the width by the max index value:
    //   all < 65536  -> UNSIGNED_SHORT (matches stock, 2 bytes)
    //   otherwise    -> UNSIGNED_INT   (eType 5? no — POD uses UINT via type 5
    //                    is ARGB; the index widener uses type 3=USHORT or the
    //                    4-byte path with type 2 (INT)/UINT). We emit 4-byte
    //                    UINT using data-type-size 4, component 1.
    bool needs_u32 = false;
    for (uint32_t idx : indices) { if (idx >= 65536u) { needs_u32 = true; break; } }

    out.begin(eMeshVertexIndexList);
    if (!needs_u32) {
        out.field_u32(eBlockDataType, 3);   // UNSIGNED_SHORT
        out.field_u32(eBlockNumComponents, 1);
        out.field_u32(eBlockStride, 2);
        std::vector<uint8_t> raw;
        raw.reserve(indices.size() * 2);
        for (uint32_t idx : indices) {
            raw.push_back(static_cast<uint8_t>(idx & 0xFF));
            raw.push_back(static_cast<uint8_t>((idx >> 8) & 0xFF));
        }
        out.field(eBlockData, raw.data(), raw.size());
    } else {
        // >65535 vertices: keep full 32-bit indices (type 2 = INT, size 4 —
        // PVRTModelPODDataTypeSize(2)==4, matching how stock stores 32-bit
        // integer streams; the loader reads size-4 elements).
        out.field_u32(eBlockDataType, 2);   // INT (32-bit)
        out.field_u32(eBlockNumComponents, 1);
        out.field_u32(eBlockStride, 4);
        out.field_u32s(eBlockData, indices);
    }
    out.finish(eMeshVertexIndexList);
}

// A CPODData block whose stream is legitimately absent.  Stock exports always
// emit 6008/6009/6011 with n == 0 when the source had no such channel, and —
// this is the part that is easy to get wrong — they carry NO 9003 data tag at
// all in that case (see rock1.POD's TANGENT/BINORMAL/COLORS blocks: only 9000,
// 9001 and 9002 are present).
void write_empty_attr_block(Sink& out, uint32_t block_id, uint32_t elem_type) {
    out.begin(block_id);
    out.field_u32(eBlockDataType, elem_type);
    out.field_u32(eBlockNumComponents, 0);
    out.field_u32(eBlockStride, 0);
    out.finish(block_id);
}

// ─── Bone-batch (bone-id) table ────────────────────────────────────────
//
// A mesh's BONEIDX stream is NEVER a list of skeleton indices.  The runtime
// resolves a vertex's bone with a TWO-LEVEL lookup in
// Caver::PODLoader::CreateMesh (0x4E4E74):
//
//   1. BONEIDX[vertex]                ->  slot into pnBoneBatchBoneIdx (6015)
//   2. pnBoneBatchBoneIdx[batch][slot] ->  POD *NODE INDEX* of the bone
//   3. that node index                ->  Skeleton bone index, which
//      Caver::PODLoader::CreateSkeleton (0x4E42F0) builds as the ascending
//      node-index rank of every node whose name starts with "Bone"/"Control"
//      plus every ancestor of such a node.
//
// Level 2 lives in a std::map keyed by the node index; CreateMesh fills it from
// the batch table.  The table itself is mandatory: the remap loop's bound is
// `**(unsigned int **)(pMesh + 248)`  — i.e. `pnBoneIdxBatch[0]` (tag 6016) —
// read with no null check, for every mesh whose BONEIDX stream is non-empty.
// A skinned mesh that omits 6015/6016 is therefore a null dereference inside
// the shipped loader, which is what left the converted characters rendering as
// an empty silhouette.
//
// `nMaxNumBonesPerBatch` (6018, pMesh + 264) is the table CAPACITY: CreateMesh
// allocates 4 * that many bytes before indexing with a BONEIDX value.

bool node_is_skeleton_bone(const std::string& name) {
    return name.rfind("Bone", 0) == 0 || name.rfind("Control", 0) == 0;
}

// Replicates CreateSkeleton's marking: a node is part of the skeleton when it
// is named Bone*/Control* or is an ANCESTOR of such a node (walking parents up).
std::vector<int> skeleton_node_order(const PODModel& model) {
    const int n = static_cast<int>(model.nodes.size());
    std::vector<char> marked(static_cast<size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        if (!node_is_skeleton_bone(model.nodes[static_cast<size_t>(i)].name)) continue;
        int j = i;
        for (int guard = 0; j >= 0 && j < n && guard < n + 1; ++guard) {
            if (marked[static_cast<size_t>(j)]) break;
            marked[static_cast<size_t>(j)] = 1;
            j = model.nodes[static_cast<size_t>(j)].parent_index;
        }
    }
    std::vector<int> order;
    for (int i = 0; i < n; ++i)
        if (marked[static_cast<size_t>(i)]) order.push_back(i);
    return order;
}

void write_mesh(Sink& out, const PODModel& model, const PODMesh& m) {
    out.begin(eSceneMesh);
    out.field_u32(eMeshNumVertices, static_cast<uint32_t>(m.num_vertices));
    out.field_u32(eMeshNumFaces, static_cast<uint32_t>(m.num_faces));
    out.field_u32(eMeshNumUVWChannels, static_cast<uint32_t>(m.uvs.empty() ? 0 : 1));
    // Triangle lists, never strips: the reader's PVRTModelPODCountIndices()
    // returns nNumFaces * 3 when nNumStrips == 0, which is what the runtime
    // sizes its index-buffer memcpy from.  6005 being absent would also read
    // as 0 (the mesh struct is calloc'd) but stock writes it explicitly.
    out.field_u32(eMeshNumStrips, 0);

    // 6018/6019 go out for EVERY mesh — a static mesh carries 0/0, exactly as
    // stock PVRGeoPOD does (verified against resources/rock1.POD).  The batch
    // index / count / offset arrays only exist when the mesh is skinned.
    const bool skinned = m.bones_per_vertex > 0 && !m.bone_indices.empty();
    std::vector<uint32_t> batch_ids;
    int max_slot = 0;
    for (float f : m.bone_indices)
        max_slot = std::max(max_slot, static_cast<int>(std::lround(f)));
    if (skinned) {
        if (m.has_bone_batches && !m.bone_batches.indices.empty()) {
            batch_ids = m.bone_batches.indices;
        } else {
            // No table supplied: synthesise one from the skeleton marking rule
            // so the loader can never walk a null pointer.  Slot i is taken to
            // be skeleton bone i (ascending marked-node order), which is the
            // convention every importer in this tree uses.
            const std::vector<int> order = skeleton_node_order(model);
            const size_t slots = std::max<size_t>(order.size(), static_cast<size_t>(max_slot) + 1);
            batch_ids.reserve(slots);
            for (size_t i = 0; i < slots; ++i) {
                if (i < order.size()) {
                    batch_ids.push_back(static_cast<uint32_t>(order[i]));
                } else if (!order.empty()) {
                    batch_ids.push_back(static_cast<uint32_t>(order.back()));
                } else {
                    batch_ids.push_back(0u);
                }
            }
            std::fprintf(stderr,
                         "[POD] mesh is skinned but carried no bone-batch table; "
                         "synthesised %zu slot(s) from the node names\n", slots);
        }
        // 6018 is the table CAPACITY the loader allocates before indexing it
        // with a raw BONEIDX value: it must cover the largest slot present.
        if (static_cast<int>(batch_ids.size()) <= max_slot) {
            std::fprintf(stderr,
                         "[POD] bone-batch table has %zu slot(s) but a vertex "
                         "references slot %d; padding the table\n",
                         batch_ids.size(), max_slot);
            batch_ids.resize(static_cast<size_t>(max_slot) + 1,
                             batch_ids.empty() ? 0u : batch_ids.back());
        }
    }

    const uint32_t max_bones = static_cast<uint32_t>(skinned ? batch_ids.size() : 0);
    const uint32_t n_batches = static_cast<uint32_t>(skinned ? 1 : 0);
    out.field_u32(eMeshMaxNumBonesPerBatch, max_bones);
    out.field_u32(eMeshNumBoneBatches, n_batches);
    if (skinned) {
        out.field_u32s(eMeshBoneBatchIndexList, batch_ids);
        // One count per batch, then one offset per batch — both single-element
        // arrays because we always synthesise exactly one batch.
        out.field_u32(eMeshNumBoneIndicesPerBatch, static_cast<uint32_t>(batch_ids.size()));
        out.field_u32(eMeshBoneOffsetPerBatch, 0u);
    }

    // 6020 pfUnpackMatrix @ +276: the runtime stores it but never reads it,
    // and our streams are already pre-baked, so identity is the correct value.
    // Stock always writes it; emitting it here keeps the mesh byte-layout
    // identical to a PVRGeoPOD export.
    {
        static const std::vector<float> kIdentity = {
            1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
        out.field_floats(eMeshUnpackMatrix,
                         m.has_unpack_matrix
                             ? std::vector<float>(m.unpack_matrix, m.unpack_matrix + 16)
                             : kIdentity);
    }

    write_index_block(out, m.indices);
    write_vertex_block(out, eMeshVertexList, m.positions, 3);
    if (m.normals.empty()) write_empty_attr_block(out, eMeshNormalList, 1);
    else                   write_vertex_block(out, eMeshNormalList, m.normals, 3);
    write_empty_attr_block(out, eMeshTangentList, 1);
    write_empty_attr_block(out, eMeshBinormalList, 1);
    if (m.uvs.empty()) write_empty_attr_block(out, eMeshUVWList, 1);
    else               write_vertex_block(out, eMeshUVWList, m.uvs, 2);
    write_empty_attr_block(out, eMeshColorList, 5 /*argb*/);
    if (m.bones_per_vertex > 0) {
        write_bone_index_block(out, m.bone_indices, m.bones_per_vertex);
        write_vertex_block(out, eMeshBoneWeightList, m.bone_weights, m.bones_per_vertex);
    } else {
        // Stock always carries the skinned channels, empty when the mesh is
        // rigid — rock1.POD's BONEIDX is eType 2 (int) with n == 0.
        write_empty_attr_block(out, eMeshBoneIndexList, 2);
        write_empty_attr_block(out, eMeshBoneWeightList, 1);
    }
    out.finish(eSceneMesh);
}

// ─── Node transform helpers ────────────────────────────────────────────
//
// Confirmed against the shipped libswordigo.so. The runtime reads a node's
// transform from *three* arrays and nowhere else:
//
//   node+40  pfAnimPosition  5007   stride 3 floats per key
//   node+56  pfAnimRotation  5008   stride 4 floats per key
//   node+72  pfAnimScale     5009   stride **7** floats per key
//             (GetTranslationMatrix @0x584D1C: `v10 = 3*frame; v11 = v10+3`;
//              GetScalingMatrix     @0x584B04: `v10 = 7*frame; v11 = v10+7`)
//
// A node that carried none of 5004..5009 leaves all three NULL and its world
// matrix is then assembled from uninitialised storage — which is exactly the
// "flat sheet" failure.  Stock PVRGeoPOD never emits 5004/5005/5006; it puts
// the static transform in 5007/5008/5009 as a ONE-key array, and the reader's
// node-close handler then sets nAnimFlags |= 1|2|4 so the same channels are
// also the animation path.  This writer does the same.
//
// 5011 (pfAnimMatrix, node+88) is consumed verbatim by GetWorldMatrixNoCache
// and is used only when a matrix cannot be expressed as translate/rotate/scale.
// 5010 is a NO-OP (`case 0x1392u: goto LABEL_202`) and is never written.

float v3len(const float v[3]) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void v3cross(float out[3], const float a[3], const float b[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// The runtime's PVRTMatrixRotationQuaternionF (0x58CF68) is the TRANSPOSE of
// the textbook column-major quaternion->matrix, so a quaternion read back by
// the game is the textbook quaternion of the transposed rotation.  Emitting q
// recovered from R^T therefore reproduces exactly the rotation the runtime
// will build.
void quat_from_col_major_transposed(const float R[16], float q[4]) {
    // S = R^T, laid out column-major so the textbook extraction applies.
    float S[16];
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            S[c * 4 + r] = R[r * 4 + c];
    const float tr = S[0] + S[5] + S[10];
    if (tr > 0.0f) {
        float s = std::sqrt(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * s;
        q[0] = (S[9] - S[6]) / s;
        q[1] = (S[2] - S[8]) / s;
        q[2] = (S[4] - S[1]) / s;
    } else if (S[0] > S[5] && S[0] > S[10]) {
        float s = std::sqrt(1.0f + S[0] - S[5] - S[10]) * 2.0f;
        q[3] = (S[9] - S[6]) / s;
        q[0] = 0.25f * s;
        q[1] = (S[1] + S[4]) / s;
        q[2] = (S[2] + S[8]) / s;
    } else if (S[5] > S[10]) {
        float s = std::sqrt(1.0f + S[5] - S[0] - S[10]) * 2.0f;
        q[3] = (S[2] - S[8]) / s;
        q[0] = (S[1] + S[4]) / s;
        q[1] = 0.25f * s;
        q[2] = (S[6] + S[9]) / s;
    } else {
        float s = std::sqrt(1.0f + S[10] - S[0] - S[5]) * 2.0f;
        q[3] = (S[4] - S[1]) / s;
        q[0] = (S[2] + S[8]) / s;
        q[1] = (S[6] + S[9]) / s;
        q[2] = 0.25f * s;
    }
}

// PVRTMatrixRotationQuaternionF, transcribed from the decompilation.
void quat_to_pvrt_matrix(const float q[4], float f[16]) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    f[0]  = 1.0f - 2.0f * (y * y + z * z);
    f[1]  = 2.0f * (x * y - z * w);
    f[2]  = 2.0f * (x * z + y * w);
    f[3]  = 0.0f;
    f[4]  = 2.0f * (x * y + z * w);
    f[5]  = 1.0f - 2.0f * (x * x + z * z);
    f[6]  = 2.0f * (y * z - x * w);
    f[7]  = 0.0f;
    f[8]  = 2.0f * (x * z - y * w);
    f[9]  = 2.0f * (y * z + x * w);
    f[10] = 1.0f - 2.0f * (x * x + y * y);
    f[11] = 0.0f;
    f[12] = f[13] = f[14] = 0.0f;
    f[15] = 1.0f;
}

// Decomposes a column-major 4x4 into the runtime's node TRS.  Returns false
// when the matrix is not a pure translate/rotate/scale — negative determinant
// (mirror), shear, or a degenerate axis — in which case the caller writes 5011
// and lets GetWorldMatrixNoCache consume the matrix verbatim.
bool decompose_node_trs(const float m[16], float pos[3], float quat[4], float scale[3]) {
    const float cx[3] = {m[0], m[1], m[2]};
    const float cy[3] = {m[4], m[5], m[6]};
    const float cz[3] = {m[8], m[9], m[10]};
    const float sx = v3len(cx), sy = v3len(cy), sz = v3len(cz);
    if (sx < 1e-6f || sy < 1e-6f || sz < 1e-6f) return false;

    float n[3];
    v3cross(n, cx, cy);
    const float det = n[0] * cz[0] + n[1] * cz[1] + n[2] * cz[2];
    if (det <= 1e-9f) return false; // mirrored or singular: keep the 4x4

    float R[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (int i = 0; i < 3; ++i) {
        R[i]      = cx[i] / sx;
        R[4 + i]  = cy[i] / sy;
        R[8 + i]  = cz[i] / sz;
    }

    quat_from_col_major_transposed(R, quat);
    // Normalise: the extraction is sensitive to accumulated float error.
    float ql = std::sqrt(quat[0] * quat[0] + quat[1] * quat[1] +
                         quat[2] * quat[2] + quat[3] * quat[3]);
    if (ql < 1e-6f) return false;
    quat[0] /= ql; quat[1] /= ql; quat[2] /= ql; quat[3] /= ql;

    pos[0] = m[12]; pos[1] = m[13]; pos[2] = m[14];
    scale[0] = sx; scale[1] = sy; scale[2] = sz;

    // Round-trip through the runtime's own maths.  Anything that survives this
    // exactly is emitted as TRS; everything else keeps its 4x4.
    float Rq[16];
    quat_to_pvrt_matrix(quat, Rq);
    const float scl[3] = {sx, sy, sz};
    float worst = 0.0f;
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 4; ++r)
            worst = std::max(worst, std::fabs(Rq[c * 4 + r] * scl[c] - m[c * 4 + r]));
    return worst <= 1e-4f;
}

void write_node(Sink& out, const PODNode& n) {
    out.begin(eSceneNode);
    out.field_u32(eNodeIndex, static_cast<uint32_t>(n.object_index));
    out.field_str(eNodeName, n.name.empty() ? "Node" : n.name);
    out.field_u32(eNodeMaterialIndex, static_cast<uint32_t>(n.material_index));
    out.field_u32(eNodeParentIndex, static_cast<uint32_t>(n.parent_index));
    out.field_u32(eNodeAnimationFlags, n.anim_flags);

    // Resolve the three channels the runtime actually reads.  A matrix node
    // is decomposed into TRS so it lands in the SAME tags every stock POD
    // uses; a 4x4 that cannot be decomposed stays a raw 5011.
    float st_pos[3] = {n.translation[0], n.translation[1], n.translation[2]};
    float st_rot[4] = {n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]};
    float st_scl[3] = {n.scale[0], n.scale[1], n.scale[2]};
    bool  raw_matrix = false;
    if (n.anim_matrix.empty() && n.has_matrix) {
        if (decompose_node_trs(n.matrix, st_pos, st_rot, st_scl)) {
            // expressible as TRS: prefer the stock form
        } else {
            raw_matrix = true;
        }
    }

    // 5007/5008/5009 are written UNCONDITIONALLY, one key when the node has no
    // keyframes of its own.  The reader's node-close handler sets
    // nAnimFlags |= 1|2|4 the moment one of them is present, and that flag is
    // what makes GetTranslationMatrix / GetScalingMatrix / the rotation slerp
    // index the arrays by frame.  Omitting them (or using 5004/5005/5006, which
    // the close handler also accepts but which never set the flags) leaves
    // every node frozen at whatever the arrays happen to hold.
    //
    // Note the scale stride: GetScalingMatrix walks `7 * frame`, so a scale key
    // is SEVEN floats — the stock exporter's 28-byte 5009 payload is exactly
    // one such key.  The extra four are the scale-orientation quaternion and
    // are zero in every stock file.
    if (!raw_matrix) {
        if (!n.anim_translation.empty()) {
            out.field_floats(eNodeAnimationPosition, n.anim_translation);
        } else {
            out.field_floats(eNodeAnimationPosition, {st_pos[0], st_pos[1], st_pos[2]});
        }
        if (!n.anim_rotation.empty()) {
            out.field_floats(eNodeAnimationRotation, n.anim_rotation);
        } else {
            out.field_floats(eNodeAnimationRotation, {st_rot[0], st_rot[1], st_rot[2], st_rot[3]});
        }
        if (!n.anim_scale.empty()) {
            // Canonical scale channel: 7 floats per key [sx,sy,sz,0,0,0,0].
            // GetScalingMatrix indexes `&pfAnimScale[7 * frame]`, so the stride
            // MUST be 7; a legacy 3-float/key array is expanded, anything else
            // is passed through unchanged (best effort).
            std::vector<float> scale_out;
            if (n.anim_scale.size() % 7 == 0) {
                scale_out = n.anim_scale;
            } else if (n.anim_scale.size() % 3 == 0) {
                const size_t keys = n.anim_scale.size() / 3;
                scale_out.reserve(keys * 7);
                for (size_t k = 0; k < keys; ++k) {
                    scale_out.push_back(n.anim_scale[k * 3 + 0]);
                    scale_out.push_back(n.anim_scale[k * 3 + 1]);
                    scale_out.push_back(n.anim_scale[k * 3 + 2]);
                    scale_out.push_back(0.0f);
                    scale_out.push_back(0.0f);
                    scale_out.push_back(0.0f);
                    scale_out.push_back(0.0f);
                }
            } else {
                scale_out = n.anim_scale;
            }
            out.field_floats(eNodeAnimationScale, scale_out);
        } else {
            out.field_floats(eNodeAnimationScale,
                             {st_scl[0], st_scl[1], st_scl[2], 0.0f, 0.0f, 0.0f, 0.0f});
        }
    }

    if (raw_matrix) {
        out.field_floats(eNodeAnimationMatrix, std::vector<float>(n.matrix, n.matrix + 16));
    } else if (!n.anim_matrix.empty()) {
        out.field_floats(eNodeAnimationMatrix, n.anim_matrix);
    }

    if (!n.anim_translation_idx.empty()) out.field_u32s(eNodeAnimationPositionIndex, n.anim_translation_idx);
    if (!n.anim_rotation_idx.empty())    out.field_u32s(eNodeAnimationRotationIndex, n.anim_rotation_idx);
    if (!n.anim_scale_idx.empty())       out.field_u32s(eNodeAnimationScaleIndex, n.anim_scale_idx);
    if (!n.anim_matrix_idx.empty())      out.field_u32s(eNodeAnimationMatrixIndex, n.anim_matrix_idx);

    out.finish(eSceneNode);
}

void write_texture(Sink& out, const std::string& name) {
    out.begin(eSceneTexture);
    out.field_str(eTextureFilename, name);   // stock always emits it, empty if unnamed
    out.finish(eSceneTexture);
}

void write_material(Sink& out, const PODMaterial& mat) {
    out.begin(eSceneMaterial);
    out.field_u32(eMaterialTailFlag, 0);
    out.field_str(eMaterialName, mat.name.empty() ? "Default" : mat.name);
    out.field_u32(eMaterialDiffuseTextureIndex, static_cast<uint32_t>(mat.diffuse_texture_index));
    // Nine auxiliary texture slots, all "none" unless the material names one.
    for (int i = 0; i < 9; ++i) out.field_u32(eMaterialTexSlotBase + static_cast<uint32_t>(i),
                                              kMaterialTexSlotNone);
    out.field_f32(eMaterialOpacity, mat.opacity > 0.0f ? mat.opacity : 1.0f);
    out.field_floats(eMaterialAmbient, {0.2f, 0.2f, 0.2f});
    out.field_floats(eMaterialDiffuse, {mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]});
    out.field_floats(eMaterialSpecular, {0.0f, 0.0f, 0.0f});
    out.field_f32(eMaterialShininess, 0.0f);
    // Lighting on, ambient on, two cleared switches, additive blend equation.
    out.field_u32(eMaterialFlagBase + 0, 1);
    out.field_u32(eMaterialFlagBase + 1, 1);
    out.field_u32(eMaterialFlagBase + 2, 0);
    out.field_u32(eMaterialFlagBase + 3, 0);
    out.field_u32(eMaterialFlagBase + 4, kMaterialBlendEquationAdd);
    out.field_u32(eMaterialFlagBase + 5, kMaterialBlendEquationAdd);
    out.field_floats(eMaterialBlockA, {0.0f, 0.0f, 0.0f, 0.0f});
    out.field_floats(eMaterialBlockB, {0.0f, 0.0f, 0.0f, 0.0f});
    out.finish(eSceneMaterial);
}

} // namespace

bool pod_write(const PODModel& model, const std::string& path, std::string* err) {
    Sink out;

    // Top-level version block.
    out.field(eFormatVersion, "AB.POD.2.0", 11);

    // 1002 exporter options / 1003 provenance.  Bytes copied verbatim from a
    // stock export (rock1.POD / hiro.POD) so our files are indistinguishable
    // from PVRGeoPOD output to anything that inspects them.  The options
    // string is a true statement about this writer: indexed, non-interleaved,
    // no unpack matrix, no vertex colour, UVW channel 0 enabled.
    static const char kExporterOptions[] =
        "bFixedPoint=0\nbFlipTextureV=0\nbIndexed=1\nbUnpackMatrix=0\nbInterleaved=0\n"
        "bSortVtx=0\nbTangentSpace=0\ncS=2\ndwBoneLimit=100\nePrimType=0\neTriSort=0\n"
        "exportBoneGeometry=0\nexportControllers=0\nexportGeom=1\nexportMatrices=0\n"
        "exportMaterials=1\nexportNormals=1\nexportSkin=1\nexportVertexColor=0\n"
        "fTangentSpaceVtxSplit=0.000000e+000\nbAlignData=1\nuiPadDataTo=4\n"
        "sVcOptPos.eType=1\nsVcOptPos.nEnable=135\nsVcOptNor.eType=1\nsVcOptNor.nEnable=135\n"
        "sVcOptCol.eType=5\nsVcOptCol.nEnable=15\nsVcOptBoneInd.eType=2\nsVcOptBoneInd.nEnable=15\n"
        "sVcOptBoneWt.eType=1\nsVcOptBoneWt.nEnable=15\nsVcOptUVW[0].eType=1\n"
        "sVcOptUVW[0].nEnable=131\nstaticFrame=0\n";
    out.field(eExporterOptions, kExporterOptions, sizeof(kExporterOptions));
    static const char kProvenance[] = "PVRGeoPOD-compatible writer (SwordigoDesktop)\0";
    out.field(eExporterProvenance, kProvenance, sizeof(kProvenance));

    // Scene block.  Child order follows the stock writer exactly: materials,
    // meshes, nodes, textures (verified on rock1.POD and hiro.POD).
    out.begin(eScene);
    out.field_floats(eSceneClearColour, {0.0f, 0.0f, 0.0f});
    out.field_floats(eSceneAmbientColour, {0.2f, 0.2f, 0.2f});
    out.field_u32(eSceneNumCameras, 0);
    out.field_u32(eSceneNumLights, 0);
    out.field_u32(eSceneNumMeshes, static_cast<uint32_t>(model.meshes.size()));
    out.field_u32(eSceneNumNodes, static_cast<uint32_t>(model.nodes.size()));
    out.field_u32(eSceneNumMeshNodes, static_cast<uint32_t>(std::max(0, model.num_mesh_nodes)));
    out.field_u32(eSceneNumTextures, static_cast<uint32_t>(model.texture_filenames.size()));
    out.field_u32(eSceneNumMaterials, static_cast<uint32_t>(model.materials.size()));
    out.field_u32(eSceneNumFrames, static_cast<uint32_t>(std::max(0, model.num_frames)));
    out.field_u32(eSceneFPS, static_cast<uint32_t>(model.fps));
    out.field_u32(eSceneFlags, 0);

    for (const auto& m : model.materials)          write_material(out, m);
    for (const auto& m : model.meshes)             write_mesh(out, model, m);
    for (const auto& n : model.nodes)              write_node(out, n);
    for (const auto& t : model.texture_filenames)  write_texture(out, t);
    out.finish(eScene);

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        if (err) *err = "cannot open output file: " + path;
        return false;
    }
    f.write(reinterpret_cast<const char*>(out.data().data()), static_cast<std::streamsize>(out.data().size()));
    if (!f) {
        if (err) *err = "write failed for " + path;
        return false;
    }
    return true;
}

} // namespace av
