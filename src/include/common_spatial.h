// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_ini.h" // Vector

// Common.dll spatial partitions: NewSpatial / FreeSpatial and the two concrete partitions
// behind them, BspPartition ("BSP") and LinearPartition ("Linear"). The implementation is
// src/Common/common_spatial.cpp.

namespace Geometry { struct Sphere; struct Frustum; } // common_misc.h

// The 0x14-byte entry the partitions store and Add (+0x10) / AddArray (+0x14) take. SYSTEM's
// zone-tree build fills one per listed zone as {zone, zone->Pos, zone radius}
// (.text:0633b061-0633b0a6).
struct FSpatialEntry
{
    void* Object;    // +0x00
    Vector Position; // +0x04
    float Radius;    // +0x10
};
static_assert(sizeof(FSpatialEntry) == 0x14, "FSpatialEntry size is 0x14");

// The result buffer the queries write into. Every retail caller builds it on the stack as
// {0x800, 0, items} (SYSTEM slots 8-12, Server.dll pub::System::InZones .text:06cfb640/06cfb760).
// The queries append while Count < Capacity and never reset Count.
struct FSpatialQueryResult
{
    unsigned Capacity;  // +0x00
    unsigned Count;     // +0x04
    void* Items[0x800]; // +0x08
};

// The segment the ray query (+0x20) takes: BspPartition reads the origin at +0x00, the
// direction at +0x0c and the length at +0x18 (FUN_063310c0, FUN_063311e0).
struct FSpatialRay
{
    Vector Origin;    // +0x00
    Vector Direction; // +0x0c
    float Length;     // +0x18
};
static_assert(sizeof(FSpatialRay) == 0x1c, "FSpatialRay size is 0x1c");

// RTTI .?AUISpatialPartition@@ -- the base of BspPartition (vftable .rdata:063a4920) and
// LinearPartition (.rdata:063a48ec). Ten slots; retail has no symbols for them, so the names
// below describe what both implementations do. +0x04 and +0x24 have no caller identified
// and keep slot names.
struct ISpatialPartition
{
    virtual ~ISpatialPartition() {}                                                          // +0x00
    virtual int Slot04() = 0;                                                                // +0x04 both: return 0
    virtual void Clear() = 0;                                                                // +0x08
    virtual void Remove(void* Object) = 0;                                                   // +0x0c
    virtual void Add(FSpatialEntry const* Entry) = 0;                                        // +0x10
    virtual void AddArray(FSpatialEntry const* Entries, unsigned Count) = 0;                 // +0x14
    virtual int QueryFrustum(Geometry::Frustum const* Frustum, FSpatialQueryResult* Result) = 0; // +0x18
    virtual int QuerySphere(Geometry::Sphere const* Sphere, FSpatialQueryResult* Result) = 0;    // +0x1c
    virtual int QueryRay(FSpatialRay const* Ray, FSpatialQueryResult* Result) = 0;           // +0x20
    virtual void Slot24(unsigned long Out[6]) = 0;                                           // +0x24 both: zero 6 dwords
};

// Call helpers kept for the existing callers (SYSTEM zone tree, client object DB, HUD).
inline void SpatialAddObject(ISpatialPartition* Spatial, FSpatialEntry const* Entry)
{
    Spatial->Add(Entry);
}

inline int SpatialFill(ISpatialPartition* Spatial, Geometry::Sphere const* Query, FSpatialQueryResult* Result)
{
    return Spatial->QuerySphere(Query, Result);
}

inline int SpatialFillFrustum(ISpatialPartition* Spatial, Geometry::Frustum const* Query,
                              FSpatialQueryResult* Result)
{
    return Spatial->QueryFrustum(Query, Result);
}

// ?FreeSpatial@@YAXPAUISpatialPartition@@@Z (.text:06331940) / ?NewSpatial@@YAPAUISpatialPartition@@PBD@Z
// (.text:0632e9f0).
COMMON_API void __cdecl FreeSpatial(ISpatialPartition* Spatial);
COMMON_API ISpatialPartition* __cdecl NewSpatial(char const* Type);
