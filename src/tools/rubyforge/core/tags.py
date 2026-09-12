"""rubyforge.core.tags — PowerVR POD chunk tag constants.

Single authoritative table of the tag-length-data chunk grammar read by
`src/tools/pod_loader.cpp` and emitted by `src/tools/pod_writer.cpp` (the
golden C++ references). The block IDs mirror the PowerVR SDK
`EPODIdentifiers.as` / `PODLoader.as` used by Swordigo.

Everything here maps 1:1 to the C++ ``constexpr`` tables so the pure-Python
reader/writer stay byte-compatible with the native pipeline.
"""

from __future__ import annotations

# Bit that marks a chunk as a "closing tag". A container block (e.g. the
# scene) is opened with tag ``X`` and closed with ``X | END_TAG`` (length 0).
END_TAG = 0x80000000

# ─── Top-level identifiers ────────────────────────────────────────────
eFormatVersion            = 1000
eScene                    = 1001
# Stock Swordigo PODs also carry two optional metadata-options string blocks
# before the scene ("bFixedPoint=0\\nbFlipTextureV=0..."). They are opaque to
# the chunk parser (skipped) but preserved on the IR by the reader.
eLoaderOptions            = 1002
eLoaderOptions2           = 1003

# ─── Scene parameters ─────────────────────────────────────────────────
eSceneClearColour         = 2000
eSceneAmbientColour       = 2001
eSceneNumCameras          = 2002
eSceneNumLights           = 2003
eSceneNumMeshes           = 2004
eSceneNumNodes            = 2005
eSceneNumMeshNodes        = 2006
eSceneNumTextures         = 2007
eSceneNumMaterials        = 2008
eSceneNumFrames           = 2009
eSceneMesh                = 2012
eSceneNode                = 2013
eSceneTexture             = 2014
eSceneMaterial            = 2015
eSceneFlags               = 2016
eSceneFPS                 = 2017

# ─── Material properties ──────────────────────────────────────────────
eMaterialName                = 3000
eMaterialDiffuseTextureIndex = 3001
eMaterialOpacity             = 3002
eMaterialAmbient             = 3003
eMaterialDiffuse             = 3004
eMaterialSpecular            = 3005
eMaterialShininess           = 3006

# ─── Texture properties ───────────────────────────────────────────────
eTextureFilename             = 4000

# ─── Node properties ──────────────────────────────────────────────────
eNodeIndex                   = 5000
eNodeName                    = 5001
eNodeMaterialIndex           = 5002
eNodeParentIndex             = 5003
eNodePosition                = 5004
eNodeRotation                = 5005
eNodeScale                   = 5006
eNodeAnimationPosition       = 5007
eNodeAnimationRotation       = 5008
eNodeAnimationScale          = 5009
eNodeMatrix                  = 5010
eNodeAnimationMatrix         = 5011
eNodeAnimationFlags          = 5012
eNodeAnimationPositionIndex  = 5013
eNodeAnimationRotationIndex  = 5014
eNodeAnimationScaleIndex     = 5015
eNodeAnimationMatrixIndex    = 5016

# ─── Mesh properties ──────────────────────────────────────────────────
eMeshNumVertices            = 6000
eMeshNumFaces               = 6001
eMeshNumUVWChannels         = 6002
eMeshVertexIndexList        = 6003
eMeshStripLengthList        = 6004
eMeshNumStrips              = 6005
eMeshVertexList             = 6006
eMeshNormalList             = 6007
eMeshTangentList            = 6008
eMeshBinormalList           = 6009
eMeshUVWList                = 6010
eMeshVertexColourList       = 6011
eMeshBoneIndexList          = 6012
eMeshBoneWeightList         = 6013
eMeshInteravedDataList      = 6014          # note: stock misspelling preserved
eMeshBoneBatchIndexList     = 6015
eMeshNumBoneIndicesPerBatch = 6016
eMeshBoneOffsetPerBatch     = 6017
eMeshMaxNumBonesPerBatch    = 6018
eMeshNumBoneBatches         = 6019
eMeshUnpackMatrix           = 6020
eMeshType                   = 6021
eMeshAdjacencyIndexList     = 6022

# ─── Vertex block fields ──────────────────────────────────────────────
eBlockDataType              = 9000
eBlockNumComponents         = 9001
eBlockStride                = 9002
eBlockData                  = 9003

# Vertex data types (DataType 9000). From jPOD.py / the PowerVR SDK.
#  1 Float, 2 Int, 3 UnsignedShort, 4 RGBA, 5 ARGB, 6 D3DCOLOR, 7 UBYTE4,
#  8 DEC3N, 9 Fixed16_16, 10 UnsignedByte, 11 Short, 12 ShortNorm,
#  13 Byte, 14 ByteNorm, 15 UnsignedByteNorm, 16 UnsignedShortNorm,
#  17 UnsignedInt, 18 ABGR, 19 HalfFloat.
DT_FLOAT            = 1
DT_INT              = 2
DT_UNSIGNED_SHORT   = 3
DT_UNSIGNED_BYTE    = 10
DT_SHORT            = 11
DT_SHORT_NORM       = 12
DT_BYTE             = 13
DT_BYTE_NORM        = 14
DT_UNSIGNED_BYTE_NORM = 15
DT_UNSIGNED_SHORT_NORM = 16
DT_UNSIGNED_INT     = 17