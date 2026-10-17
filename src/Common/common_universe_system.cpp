// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_universe.h"
#include "common_universe_internal.h"

#include "common_archetype.h"
#include "common_cobjdb.h"
#include "common_csys.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h" // Geometry::Sphere / Frustum, SphereCull
#include "common_net.h"  // CommReferrable (SYSTEM's second base)
#include "common_reputation.h" // Reputation::Vibe static store (FreeStatic / Begin / EndStaticLoad)
#include "common_zone.h"   // ZONE, the concrete IZone

#include "DALib.h"
#include "dacom.h"
#include "readfile.h"

#include <list>
#include <map>
#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <vector>

// ============================================================================
// SYSTEM -- Universe::ISystem's implementation (vftable .rdata:063a5144)
// ============================================================================

namespace CmnUniverse
{
    // The zone slots SYSTEM calls go through Universe::IZone's vtable (common_universe.h):
    //   zone vt+0x10 GetPos, vt+0x1c GetProperties, vt+0x30 GetRadius,
    //   vt+0x34 InZone (SYSTEM slots 10-12 always pass Margin 0), vt+0x54 HasPhysicalEffect.

    // 0x60ad78ec, the starting "best radius" of slots 10-12.
    float const kNoZoneRadius = 1.0e20f;

    // FUN_06337250 .text:06337376-06337441 (the stack SYSTEM's construction).
    SYSTEM::SYSTEM()
        : Spatial(nullptr)
        , SpaceMusicId(0)
    {
        NicknameId = 0;         // ID_String::clear (+0x4c)
        ((CacheString*)&Nickname)->clear(); // CacheString::clear (+0x50)
        ((CacheString*)&File)->clear();     // CacheString::clear (+0x70)
        StridName = 0;          // +0x68
        IdsInfo = 0;            // +0x6c
        Visit = 0;              // +0x64
        NavMapScale = 1.0f;     // +0x90 = 0x3f800000
        // +0x74 (pos) is not written by retail's construction; LoadUniverseIni gives the stack
        // SYSTEM the previous block's position (FUniverseLoadCarry).
    }

    // FUN_0633aee0: FreeSpatial the zone tree, then the zone list and the connection vector
    // are released (their std:: destructors here).
    SYSTEM::~SYSTEM()
    {
        if (Spatial != nullptr)
        {
            FreeSpatial(Spatial);
            Spatial = nullptr;
        }
    }

    // Slot 0, 062969c0 (ICF-folded getter): MOV EAX,[ECX+0x4c].
    unsigned SYSTEM::GetId() const
    {
        return NicknameId;
    }

    // Slot 1, 0633afa0: ADD ECX,4; JMP CommReferrable::GetMsgIdPrefix.
    TString<64> const& SYSTEM::GetMsgIdPrefix() const
    {
        return CommReferrable::GetMsgIdPrefix();
    }

    // Slot 2, 063390f0 (ICF-folded getter): MOV EAX,[ECX+0x50].
    char const* SYSTEM::GetNickname() const
    {
        return Nickname;
    }

    // Slot 3, 0633aff0: MOV EAX,[ECX+0x68].
    int SYSTEM::GetStridName() const
    {
        return StridName;
    }

    // Slot 4, 0633b000: MOV EAX,[ECX+0x6c].
    int SYSTEM::GetIdsInfo() const
    {
        return IdsInfo;
    }

    // Slot 5, 0629ba60 (ICF-folded getter): MOV EAX,[ECX+0x70].
    char const* SYSTEM::GetFile() const
    {
        return File;
    }

    // Slot 6, 0633b020: LEA EAX,[ECX+0x80].
    std::list<Universe::FSystemZone> const* SYSTEM::GetZones() const
    {
        return &Zones;
    }

    ISpatialPartition* SYSTEM::BuildZoneTree() const
    {
        if (Spatial == nullptr)
        {
            Spatial = NewSpatial("BSP");
            std::list<Universe::FSystemZone>::const_iterator It = Zones.begin();
            std::list<Universe::FSystemZone>::const_iterator End = Zones.end();
            for (; It != End; ++It)
            {
                Universe::IZone const* Zone = It->Zone;
                FSpatialEntry Entry;
                Entry.Radius = Zone->GetRadius();               // zone vt+0x30 first
                Entry.Position = Zone->GetPos();                // then zone vt+0x10
                Entry.Object = const_cast<Universe::IZone*>(Zone);
                SpatialAddObject(Spatial, &Entry);              // spatial vt+0x10
            }
        }
        return Spatial;
    }

    // Slot 7, 0633b030: build the zone tree on first use and return it.
    ISpatialPartition* SYSTEM::GetZoneTree() const
    {
        return BuildZoneTree();
    }

    // Slot 8, 0633b0c0 (RET 4): property flags at a sphere. Zones whose flags carry any of
    // 0x7fc0 are exclusive -- only the smallest-radius one of them counts (the first one sets
    // it, a later one replaces it only with a strictly smaller radius); all other zones'
    // flags are OR-ed together. The result is that OR plus the chosen exclusive zone's flags.
    unsigned SYSTEM::GetSphereProperties(Geometry::Sphere const* Query) const
    {
        ISpatialPartition* Tree = BuildZoneTree();
        FSpatialQueryResult Result;
        Result.Capacity = 0x800;
        Result.Count = 0;
        SpatialFill(Tree, Query, &Result);

        unsigned Combined = 0;                     // [ESP+0x18]
        unsigned ExclusiveFlags = 0;               // EBP
        float ExclusiveRadius = 0.0f;              // [ESP+0x10], only read once EBP != 0
        unsigned Index = 0;
        for (; Index < Result.Count; ++Index)
        {
            Universe::IZone const* Zone = (Universe::IZone const*)Result.Items[Index];
            if (SphereCull(*Query, Zone))
                continue;
            unsigned Flags = Zone->GetProperties();  // zone vt+0x1c
            if ((Flags & 0x7fc0) == 0)
            {
                Combined |= Flags;
                continue;
            }
            if (ExclusiveFlags == 0)
            {
                ExclusiveFlags = Flags;
                ExclusiveRadius = Zone->GetRadius();
            }
            else if (Zone->GetRadius() < ExclusiveRadius)
            {
                ExclusiveFlags = Flags;
                ExclusiveRadius = Zone->GetRadius();
            }
        }
        return Combined | ExclusiveFlags;
    }

    // Slot 9, 0633b230 (RET 4): OR of the property flags of every zone the sphere touches.
    unsigned SYSTEM::GetNestedProperties(Geometry::Sphere const* Query) const
    {
        ISpatialPartition* Tree = BuildZoneTree();
        FSpatialQueryResult Result;
        Result.Capacity = 0x800;
        Result.Count = 0;
        SpatialFill(Tree, Query, &Result);

        unsigned Combined = 0;
        unsigned Index = 0;
        for (; Index < Result.Count; ++Index)
        {
            Universe::IZone const* Zone = (Universe::IZone const*)Result.Items[Index];
            if (!SphereCull(*Query, Zone))
                Combined |= Zone->GetProperties();
        }
        return Combined;
    }

    // Slot 10, 0633b350 (RET 4): the smallest-radius zone containing Point. The query sphere
    // has radius 0.1f (0x3dcccccd); a null tree or a zero Fill result gives null.
    Universe::IZone const* SYSTEM::GetSmallestZoneAt(Vector const* Point) const
    {
        BuildZoneTree();
        if (Spatial == nullptr)
            return nullptr;

        Geometry::Sphere Query;
        Query.Center = *Point;
        Query.Radius = 0.1f;
        FSpatialQueryResult Result;
        Result.Capacity = 0x800;
        Result.Count = 0;
        if (SpatialFill(Spatial, &Query, &Result) == 0)
            return nullptr;

        Universe::IZone const* Best = nullptr;
        float BestRadius = kNoZoneRadius;
        unsigned Index = 0;
        for (; Index < Result.Count; ++Index)
        {
            Universe::IZone const* Zone = (Universe::IZone const*)Result.Items[Index];
            if (!Zone->InZone(*Point, 0.0f))
                continue;
            float Radius = Zone->GetRadius();
            if (Radius < BestRadius)
            {
                BestRadius = Radius;
                Best = Zone;
            }
        }
        return Best;
    }

    // Slot 11, 0633b4b0 (RET 4): as slot 10, restricted to zones whose slot 0x54 holds; the
    // radius test comes before the containment test.
    Universe::IZone const* SYSTEM::GetSmallestPhysicalZoneAt(Vector const* Point) const
    {
        Universe::IZone const* Best = nullptr;
        float BestRadius = kNoZoneRadius;
        BuildZoneTree();
        if (Spatial == nullptr)
            return Best;

        Geometry::Sphere Query;
        Query.Center = *Point;
        Query.Radius = 0.1f;
        FSpatialQueryResult Result;
        Result.Capacity = 0x800;
        Result.Count = 0;
        if (SpatialFill(Spatial, &Query, &Result) == 0)
            return Best;

        unsigned Index = 0;
        for (; Index < Result.Count; ++Index)
        {
            Universe::IZone const* Zone = (Universe::IZone const*)Result.Items[Index];
            if (!Zone->HasPhysicalEffect())
                continue;
            float Radius = Zone->GetRadius();
            if (!(Radius < BestRadius))
                continue;
            if (Zone->InZone(*Point, 0.0f))
            {
                Best = Zone;
                BestRadius = Radius;
            }
        }
        return Best;
    }

    // Slot 12, 0633b620 (RET 8): as slot 11 with the filter `PropertyFlags & Mask` and a
    // query sphere of radius 1.0f.
    Universe::IZone const* SYSTEM::GetSmallestZoneWithPropertiesAt(Vector const* Point, unsigned Mask) const
    {
        Universe::IZone const* Best = nullptr;
        float BestRadius = kNoZoneRadius;
        BuildZoneTree();
        if (Spatial == nullptr)
            return Best;

        Geometry::Sphere Query;
        Query.Center = *Point;
        Query.Radius = 1.0f;
        FSpatialQueryResult Result;
        Result.Capacity = 0x800;
        Result.Count = 0;
        if (SpatialFill(Spatial, &Query, &Result) == 0)
            return Best;

        unsigned Index = 0;
        for (; Index < Result.Count; ++Index)
        {
            Universe::IZone const* Zone = (Universe::IZone const*)Result.Items[Index];
            if ((Mask & Zone->GetProperties()) == 0)
                continue;
            float Radius = Zone->GetRadius();
            if (!(Radius < BestRadius))
                continue;
            if (Zone->InZone(*Point, 0.0f))
            {
                Best = Zone;
                BestRadius = Radius;
            }
        }
        return Best;
    }

    // Slot 13, 0633b790: begin null -> 0, else (end - begin) >> 5.
    int SYSTEM::GetConnectionCount() const
    {
        return (int)Connections.size();
    }

    // Slot 14, 0633b7b0 (RET 8): copy connection [Index] (8 dwords) when it exists. Retail
    // returns false on every path (XOR AL,AL at .text:0633b7de).
    bool SYSTEM::GetConnection(unsigned Index, Universe::FSystemConnection* Out) const
    {
        if (Index < Connections.size())
            *Out = Connections[Index];
        return false;
    }

    // Slot 15, 0633b7f0: reload the shared object pool from this system's file unless it
    // already holds it, then return the pool's record count.
    int SYSTEM::GetObjectCount() const
    {
        if (this != GSystemObjectPoolOwner)
        {
            LoadSystemObjectPool(File);
            GSystemObjectPoolOwner = this;
        }
        return (int)GSystemObjectPool.size();
    }

    // Slot 16, 0633b840 (RET 8): bounds-check Index against slot 15 (called through the
    // vtable, so it reloads the pool when needed) and the pool, then copy the record field by
    // field; the TString fields go through TString assignment (strlen, clamped to N-1).
    bool SYSTEM::GetSystemObject(unsigned Index, Universe::FSystemObject* Out) const
    {
        if (Index >= (unsigned)GetObjectCount())
            return false;
        if (Index >= GSystemObjectPool.size())
            return false;

        Universe::FSystemObject const& Src = GSystemObjectPool[Index];
        Out->Nickname = Src.Nickname.Data;
        Out->Pos = Src.Pos;
        Out->ArchetypeId = Src.ArchetypeId;
        Out->IdsName = Src.IdsName;
        Out->IdsInfo = Src.IdsInfo;
        Out->Reputation = Src.Reputation.Data;
        Out->DockWith = Src.DockWith;
        Out->Goto = Src.Goto;
        Out->BaseId = Src.BaseId;
        Out->NextRing = Src.NextRing.Data;
        Out->PrevRing = Src.PrevRing.Data;
        Out->Visit = Src.Visit;
        return true;
    }

    // Slot 17, 0633b010: LEA EAX,[ECX+0x74].
    Vector const* SYSTEM::GetPos() const
    {
        return &Pos;
    }

    // Slot 18, 0633afb0: MOV AL,[ECX+0x64].
    unsigned char SYSTEM::GetVisit() const
    {
        return Visit;
    }

    // Slot 19, 0633afe0 (RET 4): MOV [ECX+0x64],AL.
    void SYSTEM::SetVisit(unsigned char NewVisit)
    {
        Visit = NewVisit;
    }

    // Slot 20, 0633afc0: FLD [ECX+0x90].
    float SYSTEM::GetNavMapScale() const
    {
        return NavMapScale;
    }

    // Slot 21, 0633afd0: MOV EAX,[ECX+0x94].
    unsigned SYSTEM::GetSpaceMusic() const
    {
        return SpaceMusicId;
    }
}

