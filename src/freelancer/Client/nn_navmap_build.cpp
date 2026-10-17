// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// NeuroNetNavMap -- building the system view: its items, trade lanes, zones and the player /
// waypoint markers. See nn_navmap.h. Retail Client\Interface\NeuroNet\NavMap.cpp.

#include "nn_navmap.h"
#include "character_select_equip.h" // GetPlayerStarSystemIdRetail, GetPlayerVisitFlagsRetail
#include "client_cliobj.h"          // Archetype::CliSolarShapeName (companion +0x60)
#include "nn_ui_pulse.h"            // G_NnPaletteArgb_679ba4
#include "common_archetype.h"       // Archetype::GetSolar, Solar
#include "common_id.h"              // CreateID
#include "common_universe.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{
    // DAT_005d3d38 / DAT_005ca23c.
    float const kNavMapRadToDeg = 57.2957763671875f; // 0x42652ee0
    float const kNavMapFullTurn = 360.0f;
    // 0x004917cb -- the system view's map extents.
    float const kNavMapSystemExtent = 112000.0f;
    // 0x00491af9 -- a sun's alpha.
    float const kNavMapSunAlpha = 0.99f; // 0x3f7d70a4
    // 0x0049156b -- the waypoint markers' scale.
    float const kNavMapWaypointScale = 0.6f; // 0x3f19999a

    // The 0x118-byte [Object] record, its four TStrings emptied the way every NavMap.cpp
    // caller does before ISystem slot 16 (length 0, first char 0).
    void ClearSystemObject(Universe::FSystemObject* Object)
    {
        Object->Nickname.Length = 0;
        Object->Nickname.Data[0] = '\0';
        Object->Reputation.Length = 0;
        Object->Reputation.Data[0] = '\0';
        Object->NextRing.Length = 0;
        Object->NextRing.Data[0] = '\0';
        Object->PrevRing.Length = 0;
        Object->PrevRing.Data[0] = '\0';
    }
}

bool NeuroNetNavMap::IsSkippedSystem(Universe::ISystem const* System) const
{
    // Inlined in retail (e.g. 0x0049187b..0x00491892, 0x00491b44..0x00491b5b): the system's id
    // against the four SkipSystemIds.
    unsigned const Id = System->GetId();
    for (int Index = 0; Index < 4; ++Index)
    {
        if (SkipSystemIds[Index] == Id)
            return true;
    }
    return false;
}

void NeuroNetNavMap::BuildSystemItems()
{
    // FUN_004917c0.
    MapExtentX = kNavMapSystemExtent;
    MapExtentZ = kNavMapSystemExtent;
    LaneSegments.clear(); // FUN_004a2fd0(first, last)
    LaneRings.clear();    // FUN_004a3ab0(last, last, first), then last = first

    Universe::FSystemObject Object;
    ClearSystemObject(&Object);

    // 0x00491868..0x004918a5: the object count (read before the null test, as in retail),
    // none for a null or a skipped system; SystemScale = the system's NavMapScale.
    unsigned Count = static_cast<unsigned>(CurrentSystem->GetObjectCount());
    if (CurrentSystem == nullptr || IsSkippedSystem(CurrentSystem))
        Count = 0;
    SystemScale = CurrentSystem->GetNavMapScale();

    for (unsigned Index = 0; Index < Count; ++Index)
    {
        if (!CurrentSystem->GetSystemObject(Index, &Object))
            continue;
        Archetype::Solar const* const Arch = Archetype::GetSolar(Object.ArchetypeId);
        if (Arch == nullptr)
            continue;

        unsigned Type = Arch->ObjectType;
        if ((Type & 0x80) != 0)
        {
            // 0x00491906..0x004919bb: a trade lane ring is remembered; one in the middle of a
            // lane (both links set) gets no item of its own.
            FNavMapRing Ring;
            Ring.Nickname.Length = 0;
            Ring.Nickname.Data[0] = '\0';
            Ring.NextRing.Length = 0;
            Ring.NextRing.Data[0] = '\0';
            Ring.PrevRing.Length = 0;
            Ring.PrevRing.Data[0] = '\0';
            Ring.Nickname = Object.Nickname.Data;
            Ring.Pos[0] = Object.Pos.x;
            Ring.Pos[1] = Object.Pos.y;
            Ring.Pos[2] = Object.Pos.z;
            Ring.ArchetypeId = Object.ArchetypeId;
            Ring.NextRing = Object.NextRing.Data;
            Ring.PrevRing = Object.PrevRing.Data; // FUN_00417c50(data, 0x20, prev_ring)
            LaneRings.push_back(Ring);            // FUN_004a3020(last, 1, &Ring)
            if (Object.PrevRing.Data[0] != '\0' && Object.NextRing.Data[0] != '\0')
                Type = 0;
        }

        if ((SolarTypeMask & Type) == 0 || Type == 0x20000000)
            continue;

        // 0x004919e9..0x00491a5e: moons and planets face the system centre.
        float Heading = 0.0f;
        Archetype::ICliObj* const Companion = Arch->Companion;
        if ((Type & 3) != 0)
        {
            double const A = -static_cast<double>(Object.Pos.x);
            double const B = -static_cast<double>(Object.Pos.z);
            double const Length = sqrt(B * B + A * A);
            // TEST AH,0x41 + JNE: only "greater than 0" computes a heading.
            if (Length > 0.0)
            {
                double const Inv = 1.0 / Length;
                float const CosAngle = static_cast<float>(Inv * A);
                float const SinAngle = static_cast<float>(Inv * B);
                Heading = static_cast<float>(acos(static_cast<double>(CosAngle)) *
                                             static_cast<double>(kNavMapRadToDeg));
                if (SinAngle > 0.0f)
                    Heading = static_cast<float>(static_cast<double>(kNavMapFullTurn) -
                                                 static_cast<double>(Heading));
            }
        }

        // 0x00491a66..0x00491aed: the item, placed at the scaled position, with the solar's
        // radius, the companion's shape_name mesh, ids_name / ids_info, the heading, the
        // nickname id and the visit byte.
        unsigned const Id = CreateID(Object.Nickname.Data);
        double const Scale = static_cast<double>(SystemScale);
        float const X = static_cast<float>(static_cast<double>(Object.Pos.x) * Scale);
        float const Y = static_cast<float>(static_cast<double>(Object.Pos.y) * Scale);
        float const Z = static_cast<float>(Scale * static_cast<double>(Object.Pos.z));
        FNavMapItem* const Item = AddItem(
            0, static_cast<int>(Type), X, Y, Z, Arch->SolarRadius,
            Archetype::CliSolarShapeName(Companion), Object.IdsName, Object.IdsInfo, Heading, Id,
            Object.Visit);

        // 0x00491af2..0x00491b15 -- the item is used without a null test, as in retail.
        if ((Type & 4) != 0)
            Item->Mesh.UserFloats[4] = kNavMapSunAlpha;
        if (Object.Goto != 0)
            Item->System = Universe::get_system(Object.Goto);
    }

    // 0x00491b33..0x00491b91.
    BuildLaneSegments(); // FUN_004981f0
    if (CurrentSystem == nullptr || IsSkippedSystem(CurrentSystem))
        MapZones.clear(); // the element-wise erase(first, last) at 0x00491b9c..0x00491bce
    else
        BuildMapZones(); // FUN_004a0670
    if (!HasVisitedContent()) // FUN_00498a80
        LaneSegments.clear(); // FUN_004a3a80(last, last, first), then last = first
}

void NeuroNetNavMap::AddPlayerMarker()
{
    // FUN_00491410.
    float Pos[3];
    float Heading;
    PlayerMapPosition(Pos, &Heading); // FUN_00491be0
    PlayerItem = AddItem(2, 0, Pos[0], Pos[1], Pos[2], ShipMarkerSize, "NNM_SM_INFO_POSITION",
                         0x219a, 0, Heading, 0, 0);
    // 0x00491473 -- no null test, as in retail.
    PlayerItem->PulsePhase = -1.0f;
    if (ViewMode != 0 && CurrentSystem->GetId() != GetPlayerStarSystemIdRetail())
        PlayerItem->bShown = 0;

    // 0x004914a3..0x0049159e: the ten waypoint markers, tinted with the pulse palette
    // colour (the FUN_004a1af0 sequence inline) and scaled to 0.6.
    for (int Index = 0; Index < 10; ++Index)
    {
        char Path[0x200];
        sprintf(Path, "Interface\\NeuroNet\\NavMap\\NewNavMap\\waypoint%d.3db", Index);
        FClientMeshHolder* const Holder = &DigitMeshes[Index];
        ClientMeshLoadFromPath(Holder, Path, 0, true); // FUN_0059ec80(path, 0, 1)
        ClientMeshHolderSetColor(Holder,
                                 reinterpret_cast<unsigned char const*>(&G_NnPaletteArgb_679ba4));
        NavMapScaleMeshRotation(Holder, kNavMapWaypointScale);
    }
}

void NeuroNetNavMap::BuildLaneSegments()
{
    // FUN_004981f0.
    int const Count = static_cast<int>(LaneRings.size());
    for (int Chain = 0; Chain < Count; ++Chain)
    {
        // 0x00498240..0x00498288: a lane's first ring (no prev_ring, a next_ring) starts a
        // chain; it is marked used by emptying its nickname.
        FNavMapRing& First = LaneRings[Chain];
        if (First.PrevRing.Data[0] != '\0' || First.NextRing.Data[0] == '\0')
            continue;
        First.Nickname.Data[0] = '\0';
        TString<32> Name = First.NextRing; // REP MOVSD 9 dwords
        float Pos[3] = {First.Pos[0], First.Pos[1], First.Pos[2]};

        for (;;)
        {
            // 0x00498290..0x004982c5: the unused ring named Name (DACOM stricmp). Retail
            // starts the scan over, without end, when there is none.
            FNavMapRing* Next = nullptr;
            while (Next == nullptr)
            {
                for (int Index = 0; Index < Count; ++Index)
                {
                    FNavMapRing& Ring = LaneRings[Index];
                    if (Ring.Nickname.Data[0] == '\0')
                        continue;
                    if (_stricmp(Name.Data, Ring.Nickname.Data) == 0)
                    {
                        Next = &Ring;
                        break;
                    }
                }
            }

            // 0x004982c7..0x0049836b: one segment, both ends scaled by SystemScale.
            FNavMapSegment Segment;
            double const EndScale = static_cast<double>(SystemScale);
            float const EndX = static_cast<float>(static_cast<double>(Next->Pos[0]) * EndScale);
            float const EndY = static_cast<float>(static_cast<double>(Next->Pos[1]) * EndScale);
            float const EndZ = static_cast<float>(static_cast<double>(Next->Pos[2]) * EndScale);
            double const StartScale = static_cast<double>(SystemScale);
            Segment.Start[0] = static_cast<float>(static_cast<double>(Pos[0]) * StartScale);
            Segment.Start[1] = static_cast<float>(static_cast<double>(Pos[1]) * StartScale);
            Segment.Start[2] = static_cast<float>(static_cast<double>(Pos[2]) * StartScale);
            Segment.End[0] = EndX;
            Segment.End[1] = EndY;
            Segment.End[2] = EndZ;
            LaneSegments.push_back(Segment); // FUN_004a3680(last, 1, &Segment)

            // 0x00498370..0x004983d5: the ring is used; follow its next_ring.
            Next->Nickname.Data[0] = '\0';
            Pos[0] = Next->Pos[0];
            Pos[1] = Next->Pos[1];
            Pos[2] = Next->Pos[2];
            if (Next->NextRing.Data[0] == '\0')
                break;
            Name = Next->NextRing.Data; // strlen, clamped to 0x1f, copied, NUL
        }
    }

    // 0x004983f4..0x00498460: the rings are dropped (CMP EBX,EBX: an erase of the whole
    // range, last = first).
    LaneRings.clear();
}

void NeuroNetNavMap::BuildMapZones()
{
    // FUN_004a0670.
    TerrainZoneCount = 0;
    TerrainRecheckTimer = 1.0f; // DAT_005d2ecc
    MapZones.clear();   // FUN_005995c0(first, last)

    // 0x004a06b0..0x004a0778: three passes over the zone list; each records where it
    // started in MapZones.
    int PassStart[3];
    for (int Pass = 0; Pass < 3; ++Pass)
    {
        PassStart[Pass] = static_cast<int>(MapZones.size());
        std::list<Universe::FSystemZone> const* const Zones = CurrentSystem->GetZones();
        for (std::list<Universe::FSystemZone>::const_iterator It = Zones->begin();
             It != Zones->end(); ++It)
        {
            // Retail copies the 0xc0c record to the stack and reads the zone from the copy.
            Universe::IZone const* const Zone = It->Zone;
            unsigned long const Props = Zone->GetProperties();
            bool bAdd = false;
            if (Pass == 0)
                bAdd = (Props & 0x8000) != 0 && (Props & 0x30000) == 0;
            else if (Pass == 1)
                bAdd = (Props & 0x38000) == 0 && (Props & 0x7fc0) != 0;
            else
                bAdd = (Props & 0x30000) != 0;
            if (bAdd)
                MapZones.push_back(Zone); // FUN_00537260(last, 1, &zone)
        }
    }

    // 0x004a077e..0x004a07ed: the second pass's zones bubble-sorted by (properties & 0x7fc0),
    // ascending, until a sweep swaps nothing.
    int const First = PassStart[1];
    int const Last = PassStart[2] - 1;
    for (;;)
    {
        if (First >= Last)
            break;
        bool bSorted = true;
        for (int Index = First; Index < Last; ++Index)
        {
            Universe::IZone const* const Right = MapZones[Index + 1];
            Universe::IZone const* const Left = MapZones[Index];
            unsigned long const RightKey = Right->GetProperties() & 0x7fc0;
            unsigned long const LeftKey = Left->GetProperties() & 0x7fc0;
            if (LeftKey > RightKey)
            {
                MapZones[Index] = Right;
                MapZones[Index + 1] = Left;
                bSorted = false;
            }
        }
        if (bSorted)
            break;
    }
}

bool NeuroNetNavMap::HasVisitedContent()
{
    // FUN_00498a80.
    Universe::FSystemObject Object;
    ClearSystemObject(&Object);

    // 0x00498ac2..0x00498b3a: an object that is neither a lane ring nor a sun (0x84) -- or
    // has no solar archetype -- and is visited (the visit map's flags OR its own, bit 0).
    unsigned const Count = static_cast<unsigned>(CurrentSystem->GetObjectCount());
    for (unsigned Index = 0; Index < Count; ++Index)
    {
        if (!CurrentSystem->GetSystemObject(Index, &Object))
            continue;
        Archetype::Solar const* const Arch = Archetype::GetSolar(Object.ArchetypeId);
        if (Arch != nullptr && (Arch->ObjectType & 0x84) != 0)
            continue;
        unsigned char Visit = 0;
        GetPlayerVisitFlagsRetail(CreateID(Object.Nickname.Data), &Visit, 0, 0); // 0x00498b14
        if (((Visit | Object.Visit) & 1) != 0)
            return true;
    }

    // 0x00498b3c..0x00498baa: a visited map zone (the visit map OR the zone's own byte,
    // IZone slot 32).
    int const ZoneCount = static_cast<int>(MapZones.size());
    for (int Index = 0; Index < ZoneCount; ++Index)
    {
        Universe::IZone const* const Zone = MapZones[Index];
        unsigned char Visit = 0;
        GetPlayerVisitFlagsRetail(Zone->GetId(), &Visit, 1, 0); // 0x00498b82
        if (((Visit | Zone->GetVisit()) & 1) != 0)
            return true;
    }
    return false;
}
