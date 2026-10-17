// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Client-side per-object VMeshWire/IMesh part cache (retail FUN_00405b10 cluster).
// Builds merged CPU vertex (16-byte: XYZ + tag) and 16-bit index buffers per engine
// instance hierarchy for wireframe/damage-hide consumers.
//
// Retail callers: FUN_004e64f3 ( sole Build site at this+0x3a0 ).

#include "x86math.h"

#include <cstddef>
#include <map>

struct IVMeshWire;
struct IMesh;

// One part record — retail size 0x1c (LAB_00405f80 ctor).
#pragma pack(push, 1)
struct FPartCacheVertex
{
    float X;
    float Y;
    float Z;
    unsigned Tag;
};

struct FPartCacheEntry
{
    unsigned VertexCount;           // +0x00
    FPartCacheVertex* Vertices;     // +0x04
    unsigned IndexCount;            // +0x08
    unsigned short* Indices;        // +0x0C
    void* InstanceId;               // +0x10  (-1 = unset)
    unsigned Tag;                   // +0x14  packed color written into vertex.Tag
    unsigned char bHidden;          // +0x18  set by MarkHidden
    unsigned char bActive;          // +0x19  ctor=1; ActivatePart sets 1
    unsigned char Field1A;          // +0x1A  ctor=0
    unsigned char bSkipWeightTest;  // +0x1B  IMesh path weight cull gate
};
#pragma pack(pop)

static_assert(sizeof(FPartCacheEntry) == 0x1C, "FPartCacheEntry 0x1C");
static_assert(sizeof(FPartCacheVertex) == 0x10, "FPartCacheVertex 0x10");

// Root cache object — retail layout at consumer+0x3a0.
struct FPartCache
{
    FPartCacheEntry* Entries;       // +0x00  points past VC6 array count prefix
    unsigned PartCount;             // +0x04
    unsigned TotalIndices;          // +0x08
    unsigned TotalVertices;         // +0x0C
    unsigned short* MergedIndices;  // +0x10
};

// FUN_004061a0 — byte size of packed FVF texcoord block.
int FvfTexcoordBlockBytes(unsigned Fvf);

// FUN_00406880 — copy 12 dwords (FTransform / instance matrix).
void CopyTransform12(FTransform* Dest, FTransform const* Source);

// FUN_004058b0 — free one entry's vertex/index buffers.
void PartCacheEntryDestroy(FPartCacheEntry* Entry);

// LAB_00405f80 — zero-init one entry.
void PartCacheEntryConstruct(FPartCacheEntry* Entry);

// FUN_004058e0 — destroy all entries + merged IB.
void PartCacheClear(FPartCache* Cache);

// FUN_00405fb0 — VMeshWire path: weld part verts through TransformXform.
void PartCacheBuildFromWire(FPartCacheEntry* Entry, IVMeshWire* Wire, FTransform const* Xform);

// FUN_00406230 — IMesh path: walk groups, weld positions (channel 0x100).
unsigned PartCacheBuildFromMesh(FPartCacheEntry* Entry, IMesh* Mesh, FTransform const* Xform);

// FUN_00406200 — call BuildFromMesh twice; store first return in Entry->IndexCount.
void PartCacheBuildFromMeshTwice(FPartCacheEntry* Entry, IMesh* Mesh, FTransform const* Xform);

// FUN_00405b10 — rebuild whole hierarchy cache for Instance with Tag color.
// Returns true on success (TotalIndices <= 16000); false on invalid instance or overflow.
bool PartCacheBuild(FPartCache* Cache, void* Instance, unsigned Tag);

// FUN_00405930 — mark part (and optional children) hidden.
void PartCacheMarkHidden(FPartCache* Cache, void* Instance, bool bRecurseChildren);

// FUN_004059d0 — clear bActive on non-root parts that are hidden.
void PartCacheClearHiddenActive(FPartCache* Cache);

// FUN_00405a00 — activate part, optionally retag vertices; recurse children if asked.
// Returns true if the instance was found in the cache.
bool PartCacheActivatePart(FPartCache* Cache, void* Instance, bool bRecurseChildren, unsigned Tag);

// FUN_00406510 (thiscall on the cache, RET 8) — draw the active parts as one line list: render
// states DAT_005c7608 (lighting, specular, alpha blend off), stage 0 untextured, DAT_005c7628
// (colour vertex on, diffuse from colour 1); with any vertices, lock TotalVertices of FVF 0x42
// in the VB manager and, per active part, append its indices rebased by the running vertex count
// into MergedIndices and copy its XYZ + tag vertices; texture stage 0 from DAT_005c7650;
// modelview = Camera view (slot 0x40) * Xform when pipeline state 7 is set, else world Xform and
// view; DrawIndexedPrimitiveVb(D3DPT_LINELIST); finally DAT_005c7640 (colour vertex off).
// Caller: HUD_Target's target view 0x004e3e9c (the view camera +0x3b8, the object's transform).
struct ICamera;
void PartCacheDraw(FPartCache* Cache, ICamera* Camera, FTransform const* Xform);

// The instance -> saved LOD map FUN_00402580 fills and FUN_00402720 replays (retail VC6
// std::map<long,int>, 0x18-byte nodes: key +0xc, value +0x10; ctor FUN_004e7f60, destroy
// FUN_004e7e70 / FUN_004e7f20, insert FUN_00402790, iterator ++ FUN_0052afa0).
typedef std::map<void*, int> FRendererLodMap;

// FUN_00402580 (cdecl Instance, PreferredLod, Saved). Instance -1 returns 0. Otherwise: when
// Saved is given, record IRenderer2 Slot9 (current LOD) of Instance, clamp PreferredLod to
// [.., LodCount-1] and set it (Slot8 / Slot10); then for every GetRelatedInstance(Instance, 0)
// child the same, except the child's clamp is LodCount (max index + 1). Returns the largest
// max LOD index seen (0 when none is positive). Callers: HUD_Target_Base 0x004e64c1 (with a
// map) and 0x00539a1e.
int BindRendererLods(void* Instance, int PreferredLod, FRendererLodMap* Saved);

// FUN_00402720 (cdecl UnusedInstance, Saved) -- replay every saved (instance, LOD) pair through
// IRenderer2 Slot8 (LOD count) + Slot10 (set LOD), the LOD clamped to LodCount - 1. The first
// argument is pushed by the caller (0x004e64fe) but never read.
void ApplyRendererDetailQueue(void* UnusedInstance, FRendererLodMap const* Saved);
