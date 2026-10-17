// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU64 (06fa75c0-06fa9000): the zone enumerator that fills SystemInfo's zones.
// The rest of TU64 is shared STL instantiations (ported as std containers at their use
// site) and the import thunks 06fa8f00.. (CommReferrable slots of the SystemInfo vtable).
// Lane CNT-POP (docs/re/content.md, TU map).

#include "SystemInfo.h"

#include "ContentInternal.h"
#include "ContentStaticIds.h"   // TU64 static-init thunks 06fa75c0-06fa77a0
#include "common_ini.h"
#include "common_server.h"

#include <cstring>

namespace
{
    // DAT_06fd3de0: this TU's math engine, created on first use (FUN_06fa8fa0).
    I3DMathEngine* GZoneMath = nullptr;

    I3DMathEngine* GetZoneMath()
    {
        if (GZoneMath == nullptr)
            GZoneMath = ContentAcquire3DMathEngine();
        return GZoneMath;
    }
}

// FUN_06fa7ff0 (06fa7ff0-06fa810f, RET 8).
bool FSystemZone::GetPathEnds(Vector* OutFirst, Vector* OutSecond)
{
    Vector Size;
    pub::Zone::GetSize(ZoneId, Size);
    Vector const Position = pub::Zone::GetPos(ZoneId);
    Matrix Orientation;
    pub::Zone::GetOrientation(ZoneId, Orientation);

    // 06fa801f-06fa806f: the zone transform (REP MOVSD x9 + position) and (0, y/2, 0);
    // 0.5f = DAT_06fb605c.
    FTransform Placement;
    for (int Row = 0; Row < 3; ++Row)
    {
        for (int Column = 0; Column < 3; ++Column)
            Placement.Rotation.D[Row][Column] = Orientation.E[Row][Column];
    }
    Placement.Translation.X = Position.x;
    Placement.Translation.Y = Position.y;
    Placement.Translation.Z = Position.z;

    FVector Offset;
    Offset.X = 0.0f;
    Offset.Y = Size.y * 0.5f;
    Offset.Z = 0.0f;
    FVector Transformed;
    GetZoneMath()->TransformXform(Transformed, Placement, Offset);     // slot +0x28
    OutFirst->x = Transformed.X;
    OutFirst->y = Transformed.Y;
    OutFirst->z = Transformed.Z;

    // 06fa8091-06fa8107: (0, y * -0.5f, 0), DAT_06fbd6c4.
    Offset.X = 0.0f;
    Offset.Y = Size.y * -0.5f;
    Offset.Z = 0.0f;
    GetZoneMath()->TransformXform(Transformed, Placement, Offset);
    OutSecond->x = Transformed.X;
    OutSecond->y = Transformed.Y;
    OutSecond->z = Transformed.Z;
    return true;
}

// FUN_06fa8120 (06fa8120-06fa8127, RET 4).
void FSystemZone::SetPath(FPatrolPath* InPath)
{
    Path = InPath;
}

// FUN_06fa8130 (06fa8130-06fa8133).
FPatrolPath* FSystemZone::GetPath()
{
    return Path;
}

// FUN_06fa7fb0 (06fa7fb0-06fa7fc6): the result code of GetPopulation is not read.
weighted_vector<unsigned int> const* FSystemZone::GetPopulationFactions()
{
    weighted_vector<unsigned int> const* Factions;
    pub::Zone::GetPopulation(ZoneId, Factions);
    return Factions;
}

// FUN_06fa7fd0 (06fa7fd0-06fa7fe1).
float FSystemZone::GetTradelaneAttack() const
{
    if (bUnpopulated)
        return 0.0f;
    return Population.TradelaneAttack;
}

// FUN_06fa7e40 (06fa7e40-06fa7faa, RET 4). 06fa7e49-06fa7e79: +0x00..+0x0c, then the bytes
// +0x11, +0x10, +0x12, +0x13; +0x14 (VignetteType) is skipped. 06fa7e7c-06fa7f9e: the
// population block -- scalars, the list assign (node reuse, FUN_06f305b0 element copy), the
// encounter vector assign (FUN_06fa8140) and the scalars after it.
FSystemZone& FSystemZone::operator=(FSystemZone const& Other)
{
    ZoneId = Other.ZoneId;
    Nickname = Other.Nickname;
    System = Other.System;
    Path = Other.Path;
    bTradelane = Other.bTradelane;
    bUnpopulated = Other.bUnpopulated;
    bLawful = Other.bLawful;
    bUnlawful = Other.bUnlawful;
    Population = Other.Population;
    return *this;
}

// FUN_06fa77c0 (06fa77c0-06fa797a, RET 8).
void FSystemZoneEnumerator::Slot00(unsigned int const* ZoneId, INI_Reader* Reader)
{
    FSystemZone Zone;
    Zone.ZoneId = *ZoneId;
    Zone.Nickname.clear();
    Zone.System = System;

    FZonePathLabel PathLabel;
    PathLabel.Label.clear();

    if (ParseSystemZone(&Zone, Reader, &PathLabel, bLoadEncounters))
    {
        Zones->insert(Zones->end(), 1, Zone);                             // FUN_06fa8750
        if (PathLabels != nullptr && PathLabel.ZoneId != 0)
            PathLabels->insert(PathLabels->end(), 1, PathLabel);          // FUN_06fa83f0
    }
}

namespace
{
    // FUN_06eb5770 after a linear find: append Value unless Indices already holds it.
    void AddUniqueIndex(std::vector<int>* Indices, int Value)
    {
        for (std::vector<int>::iterator It = Indices->begin(); It != Indices->end(); ++It)
        {
            if (*It == Value)
                return;
        }
        Indices->insert(Indices->end(), Value);
    }
}

// FUN_06fa7980 (06fa7980-06fa7e2f, RET 0xc).
bool ParseSystemZone(FSystemZone* Zone, INI_Reader* Reader, FZonePathLabel* PathLabel,
                     bool bLoadEncounters)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            // 06fa79ba-06fa7a6d
            Zone->Nickname.Id = CreateID(Reader->get_value_string());
            if (strstr(Reader->get_value_string(), "Tradelane") != nullptr)
            {
                ZoneShape Shape;
                pub::Zone::GetShape(Zone->ZoneId, Shape);
                if (Shape == 3)
                {
                    Vector Size;
                    pub::Zone::GetSize(Zone->ZoneId, Size);
                    // DAT_06fb4438 = 10.0f; TEST AH,5 + JP: taken (skip) unless less.
                    // The product stays on the x87 stack (not rounded to float).
                    if (static_cast<double>(Size.x) * 10.0 < static_cast<double>(Size.z) &&
                        static_cast<double>(Size.y) * 10.0 < static_cast<double>(Size.z))
                        Zone->bTradelane = true;
                }
            }
            else if (strstr(Reader->get_value_string(), "vignette") != nullptr)
            {
                Zone->bLawful = true;
            }
        }
        else if (Reader->is_value("path_label"))
        {
            // 06fa7a81-06fa7b94: label, then unique path indices from value 1 on.
            PathLabel->ZoneId = Zone->ZoneId;
            PathLabel->Label.Id = CreateID(Reader->get_value_string(0));
            AddUniqueIndex(&PathLabel->Indices, Reader->get_value_int(1));
            unsigned int Index = 2;
            while (!Reader->is_value_empty(Index))
            {
                AddUniqueIndex(&PathLabel->Indices, Reader->get_value_int(Index));
                ++Index;
            }
        }
        else if (Reader->is_value("usage"))
        {
            // 06fa7bf0-06fa7c5a
            unsigned int Index = 0;
            while (!Reader->is_value_empty(Index))
            {
                char const* Usage = Reader->get_value_string(Index);
                if (_stricmp(Usage, "trade") == 0)
                    PathLabel->Usage |= 0x10;
                else if (_stricmp(Usage, "patrol") == 0)
                    PathLabel->Usage |= 0x1;
                ++Index;
            }
        }
        else if (Reader->is_value("mission_eligible"))
        {
            PathLabel->bMissionEligible = Reader->get_value_bool(0);
        }
        else if (Reader->is_value("faction"))
        {
            // 06fa7c89: recognised and ignored.
        }
        else if (Reader->is_value("density"))
        {
            Zone->Population.Density = Reader->get_value_int(0);
        }
        else if (Reader->is_value("repop_time"))
        {
            // 06fa7cc3-06fa7cdd: TEST AH,0x41 + JP -- a value <= 0 becomes 1.0 (NaN stays).
            Zone->Population.RepopTime = Reader->get_value_float(0);
            if (Zone->Population.RepopTime <= 0.0f)
                Zone->Population.RepopTime = 1.0f;
        }
        else if (Reader->is_value("mission_type"))
        {
            // 06fa7cf6-06fa7d6f: bUnlawful is not reset first.
            Zone->bLawful = false;
            unsigned int Index = 0;
            while (!Reader->is_value_empty(Index))
            {
                if (_stricmp(Reader->get_value_string(Index), "unlawful") == 0)
                    Zone->bUnlawful = true;
                else if (_stricmp(Reader->get_value_string(Index), "lawful") == 0)
                    Zone->bLawful = true;
                ++Index;
            }
            if (!Zone->bUnlawful && !Zone->bLawful)
                Zone->bLawful = true;
        }
        else if (Reader->is_value("vignette_type"))
        {
            if (_stricmp(Reader->get_value_string(0), "field") == 0)
                Zone->VignetteType = 2;
            else if (_stricmp(Reader->get_value_string(0), "exclusion") == 0)
                Zone->VignetteType = 4;
            else
                Zone->VignetteType = 1;
        }
        else if (bLoadEncounters)
        {
            // 06fa7dde-06fa7dee: the return value is not used.
            Zone->Population.ParseValue(Reader, Zone->System, Zone);
        }
    }

    // 06fa7bb0-06fa7dfa
    if (bLoadEncounters)
        Zone->bUnpopulated = !Zone->Population.bHasPopulationKeys && PathLabel->ZoneId == 0;

    // 06fa7dfd-06fa7e2f
    if (Zone->bUnpopulated && PathLabel->ZoneId == 0 && !Zone->bLawful && !Zone->bUnlawful)
        return false;
    return true;
}
