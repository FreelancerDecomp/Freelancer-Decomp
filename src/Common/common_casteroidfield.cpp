// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CmnAsteroid::CAsteroidField -- split out of common_casteroid.cpp (retail cluster
// 0x062a3270-0x062a6100). Declared in common_casteroid.h.
#include "common_casteroid.h"
#include "common_ini.h"
#include "common_id.h"
#include "common_archetype.h"
#include "dacom.h"
#include "common_iface.h" // IObjInspect
#include "common_cobjdb.h"   // IObjDB::GetNext / GetZoneDb
#include "common_universe.h" // Universe::ISystem (CmnAsteroid::Update)

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <tuple>
#include <utility>

namespace CmnAsteroid
{

namespace
{
    // .data 063eb7fc / 063eb7fd: byte flags initialised to 1 and never written by any
    // instruction of the module; they gate populate_asteroids' grid and look-ahead
    // candidates (062a5801 / 062a5922). 063eb7f0 (GAsteroidObserverScan) stays in
    // common_casteroid.cpp with CAsteroidSystem::update.
    bool GAsteroidGridSearch = true;
    bool GAsteroidAheadSearch = true;
}

// ============================================================================
// CAsteroidField implementation
// ============================================================================

CAsteroidField::CAsteroidField()
    : RefCount(0)
    , Zone(nullptr)
    , Changes(nullptr)
    , ExclusionMgr(nullptr)
    , SystemId(0)
    , CubeSize(0xfa)
    , FillDist(1000)
    , EmptyFreq(0.0f)
{
    // .text:062a4660-062a4799: ArchList (+0x00), RefCount (+0x10) 0, cube map (+0x14),
    // Zone / Changes / ExclusionMgr (+0x28 / +0x2c / +0x30) 0, LootZones (+0x34), CubeSize 250,
    // FillDist 1000, EmptyFreq 0, then the per-axis rotation presets {0, PI/2, PI, 3*PI/2}
    // (0, 0x3fc90fdb, 0x40490fdb, 0x4096cbe4) on all three axes. Retail leaves SystemId
    // (+0x40) uninitialised (create_field sets it); the port starts it at 0.
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Rotations.Angles[Axis][0] = 0.0f;
        Rotations.Angles[Axis][1] = 1.5707963f;
        Rotations.Angles[Axis][2] = 3.14159265f;
        Rotations.Angles[Axis][3] = 4.71238898f;
    }
}

CAsteroidField::~CAsteroidField()
{
    // .text:062a47a0-062a4919, in retail order: every cube erased (FUN_062a7040 over the
    // whole map, each ~CAsteroidCube releasing its asteroids), every LootZones node erased,
    // flush_changes (frees the cached marker list), ExclusionMgr destroyed and freed when
    // set; then the member destructors (LootZones, Cubes, ArchList).
    Cubes.clear();
    LootZones.clear();
    flush_changes();
    if (ExclusionMgr != nullptr)
        delete ExclusionMgr;
}

void CAsteroidField::load(INI_Reader& Reader, int CubeSizeParm)
{
    // Common.dll.c 72325-72821 (raw 0x44940). The 2nd parameter is NOT a flags bitmask
    // despite the header's previous placeholder name -- raw disasm (Common.dll.txt
    // 130120-130121: "MOV [ESI+0x44],EAX; MOV [ESI+0xc],EAX") is byte-for-byte identical
    // to CAsteroidField::set_cube_size's own body (Common.dll.c 71017-71024, listing
    // 0x433a0), confirming the caller passes this field's cube size here.
    CubeSize = CubeSizeParm;
    ArchCubeSize = CubeSizeParm;

    static const float kDegToRad = 0.017453292f;

    if (!Reader.read_header())
        return;

    do
    {
        if (Reader.is_header("Cube"))
        {
            if (Reader.read_value())
            {
                do
                {
                    // 062a49b3..062a4a5a: value i (the unrounded x87 result) FMUL float
                    // DAT_0639d210 (0.017453292f), FSTP float into Angles[axis][i], i = 0..3.
                    if (Reader.is_value("xaxis_rotation"))
                    {
                        for (unsigned Index = 0; Index < 4; ++Index)
                            Rotations.Angles[0][Index] = (float)(Reader.value_num(Index) * kDegToRad);
                    }
                    else if (Reader.is_value("yaxis_rotation"))
                    {
                        for (unsigned Index = 0; Index < 4; ++Index)
                            Rotations.Angles[1][Index] = (float)(Reader.value_num(Index) * kDegToRad);
                    }
                    else if (Reader.is_value("zaxis_rotation"))
                    {
                        for (unsigned Index = 0; Index < 4; ++Index)
                            Rotations.Angles[2][Index] = (float)(Reader.value_num(Index) * kDegToRad);
                    }
                    else if (Reader.is_value("asteroid"))
                    {
                        // .text:062a4a73..062a4e18. GetAsteroid(GetAsteroidID(CreateID(value 0)))
                        // is used without a null test: Root::load (vtable +0x0c), then the
                        // companion's slot +0x20 with 0 when a companion exists.
                        ID_String NameId;
                        NameId.Id = CreateID(Reader.get_value_string(0));
                        Archetype::Asteroid* const Arch =
                            Archetype::GetAsteroid(Archetype::GetAsteroidID(NameId));
                        Arch->load();
                        if (Arch->Companion != nullptr)
                            Archetype::CliObjLoad(Arch->Companion, 0);

                        // 062a4ab0..062a4b2a: a default entry (Arch 0, identity) is appended
                        // and the archetype stored into the new node (see the entry's comment
                        // in common_casteroid.h for the release that balances Root::load).
                        ArchList.push_back(AsteroidCubeArchEntry());
                        AsteroidCubeArchEntry& Entry = ArchList.back();
                        Entry.Arch = Arch;

                        // 062a4b26..062a4c2a: values 1 and 2 are stored as floats, value 3
                        // stays on the x87 stack; each is clamped to [-1, 1] (below -1 ->
                        // -1: TEST AH,0x5 / JP not taken only for C0; above 1 -> 1: TEST
                        // AH,0x41 / JNZ keeps C0|C3; unordered keeps the value) and value 3 is
                        // stored as a float. Each is then multiplied by FILD [this+0x0c] *
                        // 0.5 (double DAT_0639df88) kept unrounded on the stack, FSTP float.
                        float ExtentX = Reader.get_value_float(1);
                        float ExtentY = Reader.get_value_float(2);
                        double ExtentZ = Reader.value_num(3);
                        if (ExtentX < -1.0f)
                            ExtentX = -1.0f;
                        else if (ExtentX > 1.0f)
                            ExtentX = 1.0f;
                        if (ExtentY < -1.0f)
                            ExtentY = -1.0f;
                        else if (ExtentY > 1.0f)
                            ExtentY = 1.0f;
                        if (ExtentZ < -1.0)
                            ExtentZ = -1.0;
                        else if (ExtentZ > 1.0)
                            ExtentZ = 1.0;
                        double const HalfCube = (double)ArchCubeSize * 0.5;
                        Entry.Extent.x = (float)(HalfCube * ExtentX);
                        Entry.Extent.y = (float)(HalfCube * ExtentY);
                        Entry.Extent.z = (float)(HalfCube * (float)ExtentZ);

                        // 062a4c2d..062a4cd2: values 4/5/6 must all be present (checked in
                        // that order); they are read in the order 5, 4, 6 and each rounded to
                        // a float. Otherwise the error is reported and all three are 0.
                        float AngleX = 0.0f;
                        float AngleY = 0.0f;
                        float AngleZ = 0.0f;
                        if (Reader.is_value_empty(4) || Reader.is_value_empty(5) ||
                            Reader.is_value_empty(6))
                        {
                            FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                                  "E:\\FL\\Scratch\\Source\\Common\\CAsteroidSystem.cpp", 0x167,
                                  "invalid rotation specified for an asteroid");
                        }
                        else
                        {
                            AngleY = Reader.get_value_float(5);
                            AngleX = Reader.get_value_float(4);
                            AngleZ = Reader.get_value_float(6);
                        }

                        // 062a4cda..062a4dcc: identity, then three row rotations. For each
                        // angle R = angle * DAT_0639d210 (unrounded); C = float(cos R) is
                        // stored, S = sin R stays unrounded on the stack; every new element is
                        // computed from the current A/B on the stack and stored as a float.
                        Matrix& EntryOrient = Entry.Orient;
                        for (int Row = 0; Row < 3; ++Row)
                            for (int Col = 0; Col < 3; ++Col)
                                EntryOrient.E[Row][Col] = (Row == Col) ? 1.0f : 0.0f;
                        {
                            // Rows 1 and 2 by AngleX (062a4d14..062a4d39).
                            double const R = (double)AngleX * kDegToRad;
                            double const C = static_cast<float>(std::cos(static_cast<double>(R)));
                            double const S = std::sin(static_cast<double>(R));
                            for (int Col = 0; Col < 3; ++Col)
                            {
                                double const A = EntryOrient.E[1][Col];
                                double const B = EntryOrient.E[2][Col];
                                EntryOrient.E[1][Col] = (float)(C * A - S * B);
                                EntryOrient.E[2][Col] = (float)(A * S + C * B);
                            }
                        }
                        {
                            // Rows 0 and 2 by AngleY (062a4d60..062a4d87).
                            double const R = (double)AngleY * kDegToRad;
                            double const C = static_cast<float>(std::cos(static_cast<double>(R)));
                            double const S = std::sin(static_cast<double>(R));
                            for (int Col = 0; Col < 3; ++Col)
                            {
                                double const A = EntryOrient.E[0][Col];
                                double const B = EntryOrient.E[2][Col];
                                EntryOrient.E[0][Col] = (float)(C * A + S * B);
                                EntryOrient.E[2][Col] = (float)(C * B - A * S);
                            }
                        }
                        {
                            // Rows 0 and 1 by AngleZ (062a4da7..062a4dcc).
                            double const R = (double)AngleZ * kDegToRad;
                            double const C = static_cast<float>(std::cos(static_cast<double>(R)));
                            double const S = std::sin(static_cast<double>(R));
                            for (int Col = 0; Col < 3; ++Col)
                            {
                                double const A = EntryOrient.E[0][Col];
                                double const B = EntryOrient.E[1][Col];
                                EntryOrient.E[0][Col] = (float)(C * A - S * B);
                                EntryOrient.E[1][Col] = (float)(A * S + C * B);
                            }
                        }

                        // 062a4dd4..062a4e18: flags cleared, then value 7 compared byte-wise
                        // with "mine" (5 bytes) and "instant_kill" (13 bytes) incl. the NUL.
                        Entry.bMine = false;
                        Entry.bInstantKill = false;
                        if (!Reader.is_value_empty(7))
                        {
                            char const* Flag = Reader.get_value_string(7);
                            if (std::strcmp(Flag, "mine") == 0)
                                Entry.bMine = true;
                            else if (std::strcmp(Flag, "instant_kill") == 0)
                                Entry.bInstantKill = true;
                        }
                    }
                } while (Reader.read_value());
            }
        }
        else if (Reader.is_header("Exclusion Zones"))
        {
            // Common.dll.c 72600-72611. Lazily allocate the field's exclusion-zone manager
            // the first time an [Exclusion Zones] block is seen.
            if (ExclusionMgr == nullptr)
                ExclusionMgr = new Exclusion::AsteroidExclusionZoneMgr();

            if (Reader.read_value())
            {
                do
                {
                    if (Reader.is_value("exclusion"))
                    {
                        // Common.dll.c 72615-72629. Retail calls the CONST 2-arg
                        // Universe::get_zone(SystemId, Name) overload here (a distinct call
                        // target from get_non_const_zone, used below by the unrelated
                        // LootableZone "zone" key) -- const_cast matches
                        // AsteroidExclusionZoneMgr::push_back's non-const IZone* parameter;
                        // Universe::GZones itself stores non-const IZone* internally (see
                        // common_universe.cpp), so this is not UB, just API constness.
                        char const* Name = Reader.get_value_string(0);
                        Universe::IZone const* Found = Universe::get_zone(SystemId, Name);
                        if (Found == nullptr)
                        {
                            FDUMP(0x100001,
                                  "%s(%d) : *** ERROR: AsteroidField linked to missing Exclusion Zone (%s) in file named (%s)",
                                  "E:\\FL\\Scratch\\Source\\Common\\CAsteroidSystem.cpp", 0x18c, Name,
                                  Reader.get_file_name());
                        }
                        else
                        {
                            ExclusionMgr->push_back(Found, Zone);
                        }
                    }
                    else if (Reader.is_value("exclude_billboards"))
                    {
                        // 062a4f0b..062a4f27: ftol of the unrounded value; > 0 -> back()+0x08 = 1.
                        int const Count = (int)Reader.value_num(0);
                        if (Count > 0)
                            ExclusionMgr->back()->bExcludeBillboards = 1;
                    }
                    else if (Reader.is_value("billboard_count"))
                    {
                        // 062a4f40..062a4f6c: ftol of the unrounded value; > 0 -> back()+0x10 =
                        // float(Count) (FILD / FSTP float).
                        int const Count = (int)Reader.value_num(0);
                        if (Count > 0)
                            ExclusionMgr->back()->BillboardCount = (float)Count;
                    }
                    else if (Reader.is_value("exclude_dynamic_asteroids"))
                    {
                        // 062a4f81..062a4fac: ftol of the unrounded value > 0, then
                        // back()->zone(+0x00)->vtbl[0x18] GetShape() (0626c6b0, [+0x3c]) of 1
                        // (sphere) or 2 (ellipsoid) -> back()+0x09 = 1. No null tests.
                        int const Count = (int)Reader.value_num(0);
                        if (Count > 0)
                        {
                            Exclusion::AsteroidExclusionZone* const Back = ExclusionMgr->back();
                            unsigned const Shape = Back->ZonePtr->GetShape();
                            if (Shape == Universe::ZoneShapeSphere || Shape == Universe::ZoneShapeEllipsoid)
                                Back->bExcludeDynamicAsteroids = 1;
                        }
                    }
                    else if (Reader.is_value("empty_cube_frequency"))
                    {
                        // 062a4fc0..062a4fe6: FST float of the value, then the UNROUNDED value
                        // FCOMP 0.0f; TEST AH,0x1 / JNZ skips when C0 is set (below 0 or
                        // unordered); otherwise back()+0x0c = the float.
                        double const Freq = Reader.value_num(0);
                        if (Freq >= 0.0)
                            ExclusionMgr->back()->EmptyCubeFrequency = (float)Freq;
                    }
                } while (Reader.read_value());
            }
        }
        else if (Reader.is_header("LootableZone"))
        {
            // .text:062a5011..062a5278. The entry is allocated BEFORE the value loop
            // (operator new(0x28), inline ctor 062a5030..062a5067: four ID_String::clear, the
            // four counts 1, asteroid difficulty 100, dynamic difficulty 1) and written in
            // place; the same block is handed to the zone below (slot +0x8c frees the previous
            // one with free(), hence malloc here). When no zone takes it, it is leaked, as in
            // retail. A failed allocation skips the ctor (062a502e) and the later stores
            // crash, as in retail.
            LootableZoneEntry* const Entry = (LootableZoneEntry*)std::malloc(sizeof(LootableZoneEntry));
            if (Entry != nullptr)
            {
                Entry->AsteroidLootContainerId.Id = 0;
                Entry->DynamicLootContainerId.Id = 0;
                Entry->AsteroidLootCommodityId.Id = 0;
                Entry->DynamicLootCommodityId.Id = 0;
                Entry->AsteroidLootCountMin = 1;
                Entry->AsteroidLootCountMax = 1;
                Entry->DynamicLootCountMin = 1;
                Entry->DynamicLootCountMax = 1;
                Entry->AsteroidLootDifficulty = 100;
                Entry->DynamicLootDifficulty = 1;
            }

            // BL (062a507b): set only by "dynamic_loot_container" / "dynamic_loot_commodity".
            bool bHasDynamicLoot = false;
            // [ESP+0x18] (062a507d): the "zone" key's zone.
            Universe::IZone* NamedZone = nullptr;

            if (Reader.read_value())
            {
                do
                {
                    if (Reader.is_value("dynamic_loot_container"))
                    {
                        // 062a50a4..062a50b5: entry+0x04 = CreateID(value 0), BL = 1.
                        Entry->DynamicLootContainerId.Id = CreateID(Reader.get_value_string(0));
                        bHasDynamicLoot = true;
                    }
                    else if (Reader.is_value("dynamic_loot_commodity"))
                    {
                        // 062a50cc..062a50dd: entry+0x0c = CreateID(value 0), BL = 1.
                        Entry->DynamicLootCommodityId.Id = CreateID(Reader.get_value_string(0));
                        bHasDynamicLoot = true;
                    }
                    else if (Reader.is_value("zone"))
                    {
                        // 062a50f4..062a510a: Universe::get_non_const_zone(this->SystemId
                        // (+0x40), value 0); a later "zone" line overwrites it, null included.
                        NamedZone = Universe::get_non_const_zone(SystemId, Reader.get_value_string(0));
                    }
                    else if (Reader.is_value("dynamic_loot_count"))
                    {
                        // 062a5123..062a515c: ftol of the unrounded values 0 and 1, each
                        // raised to 1, then min lowered to max when max < min; +0x18 / +0x1c.
                        int Min = (int)Reader.value_num(0);
                        int Max = (int)Reader.value_num(1);
                        if (Min < 1)
                            Min = 1;
                        if (Max < 1)
                            Max = 1;
                        if (Max < Min)
                            Min = Max;
                        Entry->DynamicLootCountMin = Min;
                        Entry->DynamicLootCountMax = Max;
                    }
                    else if (Reader.is_value("dynamic_loot_difficulty"))
                    {
                        // 062a5171..062a5196: ftol of the unrounded value, clamped to
                        // [1, 100], into +0x24.
                        int Difficulty = (int)Reader.value_num(0);
                        if (Difficulty < 1)
                            Difficulty = 1;
                        else if (Difficulty > 100)
                            Difficulty = 100;
                        Entry->DynamicLootDifficulty = Difficulty;
                    }
                    // else: "asteroid_loot_container"/"asteroid_loot_commodity"/"asteroid_
                    // loot_count"/"asteroid_loot_difficulty" (Common.dll.c 72765-72773,
                    // Common.dll.txt 130799-130817) -- recognized-but-provably-dead in this
                    // exact retail build (see LootableZoneEntry's own comment: four bare
                    // is_value() calls, no get_value_*/store for any of them). No behavior to
                    // port here; INI_Reader::read_value()'s own key-not-matched path doesn't
                    // desync the parser (same as any other unrecognized value line), so this
                    // port doesn't need to special-case them at all.
                } while (Reader.read_value());

                if (bHasDynamicLoot)
                {
                    if (NamedZone != nullptr)
                    {
                        // 062a51f4..062a5235: the named zone takes the entry (slot +0x8c,
                        // ZONE::SetLootableInfo frees its previous one) and is appended to LootZones.
                        NamedZone->SetLootableInfo(Entry);
                        LootZones.push_back(NamedZone);
                    }
                    else if (Zone != nullptr)
                    {
                        // 062a523a..062a524e: the field's own zone (+0x28) takes it; no list.
                        Zone->SetLootableInfo(Entry);
                    }
                    else
                    {
                        // 062a5250..062a5275.
                        FDUMP(0x100001,
                              "%s(%d) : *** ERROR: loot zone information attached to asteroid field (%s) that has no zone",
                              "E:\\FL\\Scratch\\Source\\Common\\CAsteroidSystem.cpp", 0x246,
                              Reader.get_file_name());
                    }
                }
            }
        }
        // else: any other, truly unrecognized header is left unrecognized.
        // INI_Reader::read_header() safely skips over any unconsumed value lines to reach
        // the next header on its own (common_ini.cpp read_header, both the bini and
        // plain-text paths), so simply not matching a header here does not desync the
        // parser.
    } while (Reader.read_header());
}

namespace
{
    // Shared by CAsteroidField::create_cube_id and ::closest_cube_pos (Common.dll.c
    // 0x43270 / 0x45ee0 -- both snap a position onto the field's cube grid identically,
    // one then hashes the result, the other returns it directly).
    // FCOMP 0.0f; TEST AH,0x41; JNZ -> FSUB 0.1f (DAT_0639d20c = 0x3dcccccd) for less,
    // equal and unordered, FADD 0.1f for greater; the sum stays on the x87 stack and goes
    // straight to ftol (64-bit truncation, low 32 bits kept).
    // Takes double: populate_asteroids nudges one coordinate that is still unrounded on the
    // x87 stack (062a59ba); a float argument converts exactly.
    inline int TruncWithEpsilon(double V)
    {
        double const Nudged = (V > 0.0) ? V + (double)0.1f : V - (double)0.1f;
        return (int)(long long)Nudged;
    }
}

unsigned long CAsteroidField::create_cube_id(Vector const& Pos)
{
    // Common.dll.c 0x43270. HalfSize = trunc((CubeSize+1)*0.5) (ftol truncates toward
    // zero; CubeSize is always positive here so this is just CubeSize/2 rounded down for
    // odd sizes). Each axis is nudged by +/-0.1 (DAT_0639d20c) before truncating -- a
    // fp-imprecision guard at exact grid boundaries, NOT a round-to-nearest -- then
    // snapped onto the grid via closest_cube_coord before hashing with compute_cube_id.
    int const HalfSize = (int)(long long)(((double)CubeSize + 1.0) * 0.5);

    Vector Snapped;
    Snapped.x = (float)closest_cube_coord(TruncWithEpsilon(Pos.x), CubeSize, HalfSize);
    Snapped.y = (float)closest_cube_coord(TruncWithEpsilon(Pos.y), CubeSize, HalfSize);
    Snapped.z = (float)closest_cube_coord(TruncWithEpsilon(Pos.z), CubeSize, HalfSize);
    return compute_cube_id(Snapped);
}

Vector CAsteroidField::closest_cube_pos(Vector const& Pos)
{
    // Common.dll.c 0x45ee0. Identical per-axis snap to create_cube_id (same
    // HalfSize/epsilon convention, confirmed by CubeSize being read from the same field
    // this port already stores), but returns the snapped Vector instead of hashing it.
    int const HalfSize = (int)(long long)(((double)CubeSize + 1.0) * 0.5);

    Vector Snapped;
    Snapped.x = (float)closest_cube_coord(TruncWithEpsilon(Pos.x), CubeSize, HalfSize);
    Snapped.y = (float)closest_cube_coord(TruncWithEpsilon(Pos.y), CubeSize, HalfSize);
    Snapped.z = (float)closest_cube_coord(TruncWithEpsilon(Pos.z), CubeSize, HalfSize);
    return Snapped;
}

void CAsteroidField::update(float Dt)
{
    // .text:062a52a0-062a52f8. Every cube (tree order) still in CubeStateUnknown (node+0x50 ==
    // cube+0x40 == 0) accumulates Dt into StateTimer (cube+0x00) and becomes CubeStateFar when
    // the timer is above 3.0f (DAT_0639dfd4), CubeStateNear otherwise: FCOMP + TEST AH,0x41 +
    // JNZ takes the Near store (2) for less, equal and unordered; only greater falls through
    // to the Far store (3).
    for (std::map<unsigned long, CAsteroidCube>::iterator It = Cubes.begin(); It != Cubes.end(); ++It)
    {
        CAsteroidCube& Cube = It->second;
        if (Cube.State == CubeStateUnknown)
        {
            Cube.StateTimer += Dt;
            Cube.State = (Cube.StateTimer > 3.0f) ? CubeStateFar : CubeStateNear;
        }
    }
}

float CAsteroidField::get_immersion(Vector const& Pos)
{
    // .text:062a53a0-062a53b2: no zone -> 1.0f (DAT_0639a1d0); otherwise a tail jump into the
    // zone's slot 0 (IZoneImmersion::GetImmersion, the immersion ramp) with the same argument.
    if (Zone == nullptr)
        return 1.0f;
    return Zone->GetImmersion(Pos);
}

bool CAsteroidField::near_field(Vector const& Pos)
{
    // .text:062a53c0-062a54d2. No zone -> true.
    if (Zone == nullptr)
        return true;

    // 062a53d6-062a53ee: Margin = float((CubeSize + FillDist) * 1.74f) (FILD, FMUL by
    // DAT_0639dfdc, FSTP to a float local).
    float const Margin = (float)((double)(CubeSize + FillDist) * (double)1.74f);

    // 062a53f2-062a5414: Delta = Pos - zone position (slot +0x10), each component stored as
    // a float.
    Vector const& Center = Zone->GetPos();
    Vector Delta;
    Delta.x = Pos.x - Center.x;
    Delta.y = Pos.y - Center.y;
    Delta.z = Pos.z - Center.z;

    // 062a541a-062a5450: Reach = radius (slot +0x30) + Margin, kept on the FPU stack;
    // DistSq = x*x + z*z + y*y, stored to a float but compared as computed. DistSq above
    // Reach^2 (no C0 / C3 bit) -> false.
    double const Reach = (double)Zone->GetRadius() + (double)Margin;
    double const DistSqWide = (double)Delta.x * Delta.x + (double)Delta.z * Delta.z +
                              (double)Delta.y * Delta.y;
    float const DistSq = (float)DistSqWide;
    if (DistSqWide > Reach * Reach)
        return false;

    // 062a545b-062a54ad: sqrt of the float DistSq, stored as the float Dist but compared
    // unrounded against 1e-4f (DAT_0639dfd8; the JNZ also takes unordered): at or below it
    // the excess is 0.0f; otherwise Dir = Delta * (1.0 / Dist) with Dist reloaded as the
    // float (062a5472), float components, and Excess = Dist - zone extent along Dir
    // (slot +0x40).
    double const DistWide = std::sqrt((double)DistSq);
    float const Dist = (float)DistWide;
    double Excess;
    if (DistWide > (double)0.0001f)
    {
        double const InvDist = 1.0 / (double)Dist;
        Vector Dir;
        Dir.x = (float)(Delta.x * InvDist);
        Dir.y = (float)(Delta.y * InvDist);
        Dir.z = (float)(Delta.z * InvDist);
        Excess = (double)Dist - (double)Zone->GetRadiusAlong(Dir);
    }
    else
    {
        Excess = 0.0;
    }

    // 062a54b5-062a54d2: FCOMP Margin, TEST AH,0x41, JP -> false for greater (no bit) and
    // unordered (both bits); true for less or equal.
    return Excess <= (double)Margin;
}

float CAsteroidField::near_field_fraction(Vector const& Pos, float Dist)
{
    // .text:062a54e0-062a55f8. No zone -> 1.0f.
    if (Zone == nullptr)
        return 1.0f;

    // 062a54fa-062a5529: Dist kept when above 0.0f (TEST AH,0x41 + JNZ: less, equal and
    // unordered take the default); otherwise float((FillDist + CubeSize) * 1.2f)
    // (DAT_0639dfe0).
    if (!(Dist > 0.0f))
        Dist = (float)((double)(FillDist + CubeSize) * (double)1.2f);

    // 062a552d-062a5551: Delta = Pos - zone position (slot +0x10), float components.
    Vector const& Center = Zone->GetPos();
    Vector Delta;
    Delta.x = Pos.x - Center.x;
    Delta.y = Pos.y - Center.y;
    Delta.z = Pos.z - Center.z;

    // 062a5557-062a558d: Reach = radius (slot +0x30) + Dist; DistSq = x*x + z*z + y*y.
    // DistSq above Reach^2 (no C0 / C3 bit, JZ) -> 0.0f.
    double const Reach = (double)Zone->GetRadius() + (double)Dist;
    double const DistSqWide = (double)Delta.x * Delta.x + (double)Delta.z * Delta.z +
                              (double)Delta.y * Delta.y;
    float const DistSq = (float)DistSqWide;
    if (DistSqWide > Reach * Reach)
        return 0.0f;

    // 062a558f-062a55cc: sqrt of the float DistSq (no small-length test here), stored as the
    // float Len; the reciprocal is taken of the unrounded root still on the FPU stack
    // (FST then FDIVR, no reload); Dir = Delta * (1.0 / root) in float components;
    // Excess = Len (float) - extent along Dir (slot +0x40).
    double const LenWide = std::sqrt((double)DistSq);
    float const Len = (float)LenWide;
    double const InvLen = 1.0 / LenWide;
    Vector Dir;
    Dir.x = (float)(Delta.x * InvLen);
    Dir.y = (float)(Delta.y * InvLen);
    Dir.z = (float)(Delta.z * InvLen);
    double const Excess = (double)Len - (double)Zone->GetRadiusAlong(Dir);

    // 062a55d0-062a55f8: FCOM Dist, TEST AH,0x41, JP -> 0.0f for greater and unordered;
    // otherwise 1.0f - Excess / Dist (left unrounded in ST0; the only consumer, freelancer.exe
    // 0052241f, stores it as a float at once).
    if (!(Excess <= (double)Dist))
        return 0.0f;
    return (float)(1.0 - Excess / (double)Dist);
}

void CAsteroidField::flush_changes()
{
    // .text:062a5600-062a56d4. In-order walk of the cube tree (+0x18): a CubeStateFar cube
    // (node+0x50 == cube+0x40 == 3) is erased through the tree erase helper FUN_062a6c60
    // (its ~CAsteroidCube releases the asteroids); every other cube has its State set to 0
    // (.text:062a5668). Then the cached marker list (+0x2c) is deleted -- the recursive
    // scalar-deleting dtor FUN_062a56e0 frees the whole chain -- and +0x2c is cleared.
    for (std::map<unsigned long, CAsteroidCube>::iterator It = Cubes.begin(); It != Cubes.end();)
    {
        if (It->second.State == CubeStateFar)
        {
            It = Cubes.erase(It);
        }
        else
        {
            It->second.State = CubeStateUnknown;
            ++It;
        }
    }
    if (Changes != nullptr)
        delete Changes;
    Changes = nullptr;
}

asteroid_marker* CAsteroidField::get_changes()
{
    // .text:062a5300-062a5393. A cached list is returned as is. Otherwise every cube in tree
    // order (node+0x10) contributes its CAsteroidCube::get_changes list: the first non-null
    // one becomes the cache head (+0x2c), each later one is appended after the last node of
    // the list so far (walk ->Next from the previous tail, .text:062a533a-062a534a).
    if (Changes != nullptr)
        return Changes;

    asteroid_marker* Tail = nullptr;
    for (std::map<unsigned long, CAsteroidCube>::iterator It = Cubes.begin(); It != Cubes.end(); ++It)
    {
        asteroid_marker* CubeChanges = It->second.get_changes();
        if (CubeChanges == nullptr)
            continue;
        if (Changes == nullptr)
        {
            Changes = CubeChanges;
            Tail = CubeChanges;
        }
        else
        {
            while (Tail->Next != nullptr)
                Tail = Tail->Next;
            Tail->Next = CubeChanges;
        }
    }
    return Changes;
}

CAsteroidCube* CAsteroidField::find_cube(unsigned long CubeIdValue)
{
    // Common.dll.c 0x46100. Retail walks its hand-rolled RB-tree by CubeId and returns
    // the embedded cube (node+0x10) or nullptr; equivalent std::map lookup here.
    std::map<unsigned long, CAsteroidCube>::iterator It = Cubes.find(CubeIdValue);
    if (It == Cubes.end())
        return nullptr;
    return &It->second;
}

void CAsteroidField::populate_asteroids_internal(Vector const& Pos, Vector const& Candidate, float DistSqThreshold)
{
    // .text:062a5ab0-062a5d4f (no Ghidra .c).
    // 1. ExclusionMgr (+0x30), when set: is_excluded(Candidate, EOT_CmnAsteroid, &Value)
    //    (slot +0x4); its value is the zone's "empty_cube_frequency"
    //    (AsteroidExclusionZone::EmptyCubeFrequency) and, when >= 0, replaces EmptyFreq for
    //    CAsteroidCube::init below.
    // 2. compute_cube_id(Candidate); bail if 0 or if step 1 reported the cube excluded.
    // 3. find_cube(cubeId) (FUN_06301640, the tree find).
    // 4. If found: skip if its State is CubeStateActive or CubeStateNear. Otherwise
    //    Delta = Pos - Candidate (FUN_06288800, cdecl(out,a,b) float subtract) and, if
    //    |Delta|^2 < DistSqThreshold, mark it CubeStateNear and zero StateTimer (+0x00).
    // 5. If not found: the same Delta and test, then the Zone (+0x28) slot 0 immersion
    //    test, then the new cube is inserted and initialised (details at each step below).
    // .text:062a5ad7-062a5b1c. BL starts at 1 and only an ExclusionMgr call can clear it
    // (NEG/SBB/INC: BL = (AL == 0)). The override is taken when OutDist is neither below
    // 0.0f (DAT_06399408) nor NaN (FCOMP; TEST AH,0x1; JNZ skips when C0 is set).
    float FreqOverride = EmptyFreq;
    bool bNotExcluded = true;
    if (ExclusionMgr != nullptr)
    {
        float OutDist = -1.0f;
        bNotExcluded = !ExclusionMgr->is_excluded(Candidate, Exclusion::EOT_CmnAsteroid, &OutDist);
        if (bNotExcluded && OutDist >= 0.0f)
            FreqOverride = OutDist;
    }

    // .text:062a5b20-062a5b37: the cube id is computed before either bail-out is tested.
    unsigned long const CubeIdValue = compute_cube_id(Candidate);
    if (CubeIdValue == 0)
        return;
    if (!bNotExcluded)
        return;

    CAsteroidCube* Existing = find_cube(CubeIdValue);
    if (Existing != nullptr)
    {
        // .text:062a5b5c-062a5bcb.
        if (Existing->State == CubeStateActive || Existing->State == CubeStateNear)
            return;

        // FUN_06288800 stores each component of Pos - Candidate as a float; the sum is
        // kept on the FPU stack in the order (z*z + y*y) + x*x.
        Vector Delta;
        Delta.x = Pos.x - Candidate.x;
        Delta.y = Pos.y - Candidate.y;
        Delta.z = Pos.z - Candidate.z;
        double DistSq = (double)Delta.z * Delta.z + (double)Delta.y * Delta.y;
        DistSq += (double)Delta.x * Delta.x;
        // FCOMP; TEST AH,0x5; JP bails unless the sum is strictly below the threshold
        // (equal, greater and NaN all leave the cube alone).
        if (DistSq < (double)DistSqThreshold)
        {
            Existing->State = CubeStateNear;
            Existing->StateTimer = 0.0f;
        }
        return;
    }

    // .text:062a5bd0-062a5c27: the same float components (inline FSUB/FSTP dword, packed by
    // FUN_0628b030) and the same (z*z + y*y) + x*x sum and strict-less test.
    Vector Delta;
    Delta.x = Pos.x - Candidate.x;
    Delta.y = Pos.y - Candidate.y;
    Delta.z = Pos.z - Candidate.z;
    double DistSq = (double)Delta.z * Delta.z + (double)Delta.y * Delta.y;
    DistSq += (double)Delta.x * Delta.x;
    if (!(DistSq < (double)DistSqThreshold))
        return;

    // .text:062a5c2d-062a5c40: Zone (+0x28) slot 0 (IZoneImmersion::GetImmersion) on Candidate,
    // called with no null test. FCOMP 0.0f; TEST AH,0x44; JNP bails only when the result
    // equals 0.0f -- below, above and NaN all go on to build the cube.
    if (Zone->GetImmersion(Candidate) == 0.0f)
        return;

    // .text:062a5c46-062a5d18: retail builds a temporary cube with the inlined
    // CAsteroidField*-taking ctor (Owner = this, State 0, empty asteroid list), copy-
    // constructs it into a (CubeId, cube) pair (??0CAsteroidCube@@QAE@ABV01@@Z 062a3070),
    // inserts the pair through FUN_062a6a50, destroys the pair, and runs the inlined
    // ~CAsteroidCube of the temporary (FUN_0632ca20 / FUN_062b9490 are the folded list
    // helpers; the list is empty, so nothing is released). Building the cube in place in
    // the map leaves the same node contents.
    CAsteroidCube& NewCube = Cubes.emplace(std::piecewise_construct,
        std::forward_as_tuple(CubeIdValue), std::forward_as_tuple(this)).first->second;
    // .text:062a5d21-062a5d47: State 1, Size = CubeSize (+0x44), SystemId (+0x40), then
    // init(Candidate, CubeId, this, &Rotations (+0x50), FreqOverride).
    NewCube.State = CubeStateActive;
    NewCube.Size = CubeSize;
    NewCube.SystemId = SystemId;
    NewCube.init(Candidate, CubeIdValue, this, Rotations, FreqOverride);
}

void CAsteroidField::populate_asteroids(Vector const& Pos, Vector const& Dir)
{
    // .text:062a5710-062a5aa8. Called from CAsteroidSystem::update with Pos = an observer's
    // position and Dir = its negated forward axis. Two pieces, gated by the never-written
    // .data bytes DAT_063eb7fc / DAT_063eb7fd (both 1): the 2x2x2 grid cubes around Pos,
    // then one cube ahead of Pos along Dir.
    // .text:062a5718-062a580b: Pos snapped onto the grid exactly as closest_cube_pos does
    // (inlined; computed before the flag test, pure). 062a580f-062a581d: the grid threshold
    // is FILD CubeSize * 0.99 (DAT_0639dfe8, a double), squared on the x87 stack and stored
    // once as a float.
    Vector const Grid = closest_cube_pos(Pos);
    double const ScaledSize = (double)CubeSize * 0.99;
    float const DistSqThresholdGrid = (float)(ScaledSize * ScaledSize);

    if (GAsteroidGridSearch)                                     // DAT_063eb7fc
    {
        // 062a5829-062a5872: per axis FCOMP Pos.axis against Grid.axis; TEST AH,0x41; JNZ
        // (less, equal, unordered) -> -CubeSize, greater -> +CubeSize (an int).
        int const OffsetX = (Pos.x > Grid.x) ? CubeSize : -CubeSize;
        int const OffsetY = (Pos.y > Grid.y) ? CubeSize : -CubeSize;
        int const OffsetZ = (Pos.z > Grid.z) ? CubeSize : -CubeSize;

        // 062a5874-062a591c: z outer, y middle, x inner; a set index adds FILD offset to the
        // grid float (FADD float, FSTP float).
        for (int Cz = 0; Cz < 2; ++Cz)
        {
            for (int Cy = 0; Cy < 2; ++Cy)
            {
                for (int Cx = 0; Cx < 2; ++Cx)
                {
                    Vector Candidate;
                    Candidate.x = (Cx != 0) ? (float)((double)OffsetX + Grid.x) : Grid.x;
                    Candidate.y = (Cy != 0) ? (float)((double)OffsetY + Grid.y) : Grid.y;
                    Candidate.z = (Cz != 0) ? (float)((double)OffsetZ + Grid.z) : Grid.z;
                    populate_asteroids_internal(Pos, Candidate, DistSqThresholdGrid);
                }
            }
        }
    }

    if (GAsteroidAheadSearch)                                    // DAT_063eb7fd
    {
        // 062a592f-062a598e: C = FILD CubeSize; x = float((C*Dir.x)*1.25 + Pos.x) with
        // nothing rounded before the store; y: C*Dir.y is stored as a float, then *1.25 +
        // Pos.y stays on the x87 stack; z: C*Dir.z and *1.25 are each stored as floats, then
        // + Pos.z stored as a float. 1.25f is DAT_0639dfe4.
        double const C = (double)CubeSize;
        float const AheadX = (float)(C * Dir.x * 1.25 + Pos.x);
        float const ScaledY = (float)(C * Dir.y);
        double const AheadYWide = (double)ScaledY * 1.25 + Pos.y;
        float const ScaledZ = (float)(C * Dir.z);
        float const ScaledZ125 = (float)((double)ScaledZ * 1.25);
        float const AheadZ = (float)((double)ScaledZ125 + Pos.z);

        // 062a5992-062a5a75: nudge + ftol per axis (y from the unrounded value, FCOM at
        // 062a59ba), HalfSize = ftol((C + 1.0f) * 0.5f), inlined closest_cube_coord, FILD ->
        // FSTP float.
        int const SnapX = TruncWithEpsilon(AheadX);
        int const SnapY = TruncWithEpsilon(AheadYWide);
        int const SnapZ = TruncWithEpsilon(AheadZ);
        int const HalfSize = (int)(long long)((C + 1.0) * 0.5);
        Vector Candidate;
        Candidate.x = (float)closest_cube_coord(SnapX, CubeSize, HalfSize);
        Candidate.y = (float)closest_cube_coord(SnapY, CubeSize, HalfSize);
        Candidate.z = (float)closest_cube_coord(SnapZ, CubeSize, HalfSize);

        // 062a5a7d: the threshold is the immediate 0x49742400 = 1000000.0f.
        populate_asteroids_internal(Pos, Candidate, 1000000.0f);
    }
}

int CAsteroidField::get_cube_size()
{
    // Common.dll.c 0x433b0.
    return CubeSize;
}

void CAsteroidField::set_system(unsigned SystemIdValue)
{
    // .text:062a33e0: MOV [this+0x40],arg; RET 4.
    SystemId = SystemIdValue;
}

AxisRotations& CAsteroidField::get_axis_rotations()
{
    // .text:0629d990: LEA EAX,[this+0x50]; RET.
    return Rotations;
}

Exclusion::AsteroidExclusionZoneMgr* CAsteroidField::get_exclusion_mgr()
{
    // .text:063275a0: MOV EAX,[this+0x30]; RET.
    return ExclusionMgr;
}

Universe::IZone const* CAsteroidField::get_lootable_zone(Vector const& Pos) const
{
    // .text:062a5e80. Walks the [LootableZone] list at +0x38 in insertion order; slot 0x00 of
    // each zone is the containment test, compared against 0.0f (DAT_06399408) with
    // FCOMP/FNSTSW/TEST AH,0x44/JP -- the jump is taken when the result is not equal to 0.
    for (std::list<Universe::IZone*>::const_iterator It = LootZones.begin();
         It != LootZones.end(); ++It)
    {
        if ((*It)->GetImmersion(Pos) != 0.0f)
            return *It;
    }

    // .text:062a5eae-062a5eb8: Zone->vtable[0x7c] is .text:06262dd0 (MOV EAX,[this+0x84]),
    // the owned loot entry installed by slot 0x8c. Retail dereferences Zone unconditionally.
    if (Zone->GetLootableInfo() != nullptr)
        return Zone;
    return nullptr;
}

void CAsteroidField::set_cube_size(int Size)
{
    // .text:062a33a0-062a33aa: [this+0x44] = Size, [this+0x0c] = Size.
    CubeSize = Size;
    ArchCubeSize = Size;
}

void CAsteroidField::set_fill_dist(int Dist)
{
    // Common.dll.c 0x433c0.
    FillDist = Dist;
}

void CAsteroidField::set_empty_freq(float Freq)
{
    // Common.dll.c 0x433d0.
    EmptyFreq = Freq;
}

void CAsteroidField::set_zone(Universe::IZone const* InZone)
{
    // Common.dll.c 0xdbae0 (ICF-folded with CECargo::SetCount): store at this+0x28.
    Zone = const_cast<Universe::IZone*>(InZone);
}

int CAsteroidField::AddRef()
{
    // Common.dll.c 0x45e40.
    return ++RefCount;
}

int CAsteroidField::Release()
{
    // Common.dll.c 0x45e50.
    int NewCount = --RefCount;
    if (NewCount == 0)
        delete this;
    return NewCount;
}

}
