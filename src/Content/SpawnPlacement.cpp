// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU53 (SpaceSupport's range, 06f61350-06f661f0): the placement of an encounter
// spawn -- where a formation built by TU29 FUN_06ef4eb0 (FEncounterParameters::SpawnFormation)
// appears: out of a trade lane (1), launched from a dock (2..6), cruising in (7) or buzzing the
// player (8). PlaceSpawnFormation (FUN_06f65f10) picks one of the formation's arrival methods
// at random until one places the blocks and fills the FSpawnPlan (Guides.h).
// Lane CNT-GS (docs/re/content.md, TU map).
//
// x87: retail keeps intermediate values in extended precision and rounds only on FSTP dword;
// the port uses double for the unrounded values and float where retail stores a float.

#include "SpaceSupport.h"

#include "ContentInternal.h"        // SubtractVectors (06eeaee0), FastReciprocalSqrt (06f5aeb0)
#include "ContentPlayerState.h"     // FContentPlayerState, FContentPlayerShip (TU46)
#include "ContentStaticIds.h"       // TU53's static ids (can_use_berths / *_moors)
#include "DockQueues.h"             // GetSpaceMathEngine (DAT_06fd1c70)
#include "Guides.h"                 // FGuideDescBase, FSpawnPlan
#include "MissionObjList.h"         // CreateObjListDirective, FObjListGotoVec, FObjListInstantTradelane
#include "NpcShip.h"                // FContentNpcShipParams (TU43)
#include "PatrolPaths.h"            // FPatrolPath, FPatrolPathSegment
#include "Populators.h"             // FSpawnRequest, FSpacePopExclusionSphere, SpacePopGetSolarDetection
#include "SpacePaths.h"             // IsSegmentClear (FUN_06f60e10)
#include "SystemInfo.h"             // FSystemInfo, FSystemZone, FTradeLaneRing, DistanceSquaredToSegment
#include "common_misc.h"            // random_inside_cylinder
#include "common_server.h"
#include "x86math.h"                // FVector, FMatrix

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace
{
    // .rdata 06fbbb80: the ship reputation passed to the dock target picks (none).
    int const GNoShipRep = -1;
    // .rdata 06fbbb70: "no object" -- the excluded id of a first pick.
    unsigned int const GNoObjectId = 0;
    // .rdata 06fbbb7c: every object type (the path target picks of FUN_06f63cc0).
    unsigned int const GAnyObjectType = 0xffffffffu;
    // DAT_06fd0eb0 (static init LAB_06f62940 = 5000): how far from the spawn point a trade
    // lane ring must be to become the entry ring (FUN_06f62950, compared as an unsigned int64).
    unsigned int GTradeLaneEntryDistance = 5000;
    // DAT_06fbbc40: the clearance added to an object's squared radius (FUN_06f61350).
    float const GObjectClearanceSq = 90000.0f;
    // DAT_06fbbc54: the per-axis squared tolerance of "the walk reached a segment end".
    float const GSegmentEndToleranceSq = 0.01f;
    // DAT_06fbbc98: 3500^2, the dock-launch distance limit of FUN_06f65900.
    float const GDockLaunchDistanceSq = 12250000.0f;
    // DAT_06fb4448: 2^-15 (rand() to [0, 1)).
    float const GRandScale = 1.0f / 32768.0f;

    bool const GTrue = true;
    bool const GFalse = false;

    FVector ToFVector(Vector const& Source)
    {
        FVector Out;
        Out.X = Source.x;
        Out.Y = Source.y;
        Out.Z = Source.z;
        return Out;
    }

    FMatrix ToFMatrix(Matrix const& Source)
    {
        FMatrix Out;
        std::memcpy(&Out, &Source, sizeof(Out));
        return Out;
    }

    // FUN_06f0bb50 (thiscall): sqrt((x^2 + y^2) + z^2) on the x87 stack.
    double VectorLength(Vector const& V)
    {
        return std::sqrt((static_cast<double>(V.x) * V.x + static_cast<double>(V.y) * V.y) +
                         static_cast<double>(V.z) * V.z);
    }

    // The inline form (no CALL): the components subtracted in place and stored through the
    // Vector(x, y, z) ctor FUN_06eb7810. The CALL sites of FUN_06eeaee0 use SubtractVectors.
    Vector Subtract(Vector const& A, Vector const& B)
    {
        Vector Out;
        Out.x = A.x - B.x;
        Out.y = A.y - B.y;
        Out.z = A.z - B.z;
        return Out;
    }

    // FUN_06f0bb80 (cdecl): *Out = A + B, each component stored as a float.
    Vector Add(Vector const& A, Vector const& B)
    {
        Vector Out;
        Out.x = A.x + B.x;
        Out.y = A.y + B.y;
        Out.z = A.z + B.z;
        return Out;
    }

    // FUN_06f60760 (cdecl): *Out = V * Scale, each product stored as a float.
    Vector Scale(Vector const& V, float Factor)
    {
        Vector Out;
        Out.x = Factor * V.x;
        Out.y = Factor * V.y;
        Out.z = Factor * V.z;
        return Out;
    }

    // std::find over a class list (FUN_06eb5f30 against the vector's end).
    bool HasClass(std::vector<unsigned int> const& Classes, unsigned int ClassId)
    {
        return std::find(Classes.begin(), Classes.end(), ClassId) != Classes.end();
    }

    // The "(rand() * Count) >> 15" index idiom (IMUL, CDQ, AND EDX,0x7fff, ADD, SAR 0xf).
    int RandIndex(int Count)
    {
        return (rand() * Count) / 32768;
    }

    // The "(rand() << 1) >> 15" coin of FUN_06f61800 / FUN_06f62ab0: 0 or 1.
    int RandCoin()
    {
        return (rand() * 2) / 32768;
    }

    // The exclusion spheres of the other players' ships (2500 around each ship that is not
    // the request's), appended in player order. Inline in FUN_06f64800 (06f64873-06f648e8),
    // FUN_06f64fc0 (06f6506e-06f650dc) and FUN_06f65900 (06f65b4b-06f65bbc).
    void AddOtherPlayerSpheres(FSystemInfo* System, FSpawnRequest const* Request,
                               std::vector<FSpacePopExclusionSphere>* Spheres)
    {
        std::vector<FContentPlayerState*>* Players = System->GetPlayers();          // FUN_06f78ee0
        for (std::vector<FContentPlayerState*>::iterator Player = Players->begin();
             Player != Players->end(); ++Player)
        {
            FContentPlayerShip* Ship = (*Player)->ShipObject;
            if (Ship == nullptr || Ship->SpaceObjId == Request->PlayerShip->SpaceObjId)
                continue;
            Vector Position;
            Ship->GetPosition(&Position);                                           // FUN_06eb70f0
            FSpacePopExclusionSphere const Sphere = {Position.x, Position.y, Position.z, 2500.0f};
            Spheres->push_back(Sphere);                                             // FUN_06f695a0
        }
    }

    // The spawner's directive insert: the new directive goes in front of the leader block's
    // list (FUN_06f1e390 at FUN_06f1df30's begin, or the same insert inlined), AddRef'ed
    // through the holder's pointer; the holder releases the creating reference.
    void AddLeaderDirective(FContentNpcShipParams* Leader, FObjListDirective* Directive)
    {
        Leader->Directives.Items.push_front(Directive);
        Directive->AddRef();
    }

    // FUN_06f61350 (06f61350-06f6142a, cdecl): false when an object of the system (a scan of
    // 300 around Position, type 0x7f0000, 64 at most) is nearer than its radius plus 300
    // (squared radius + 90000 above the squared distance).
    bool IsClearOfObjects(Vector const* Position, FSystemInfo* System)
    {
        unsigned int const SystemId = System->SystemId;
        unsigned int Ids[0x40];
        unsigned int Count;
        pub::System::ScanObjects(SystemId, Ids, 0x40, *Position, 300.0f, 0x7f0000, Count);
        for (unsigned int Index = 0; Index < Count; ++Index)
        {
            float Radius;
            Vector Center;
            if (pub::SpaceObj::GetRadius(Ids[Index], Radius, Center) != 0)
                continue;
            float const RadiusSq = Radius * Radius;                                 // FSTP dword
            double const Dx = static_cast<double>(Position->x) - Center.x;
            double const Dy = static_cast<double>(Position->y) - Center.y;
            double const Dz = static_cast<double>(Position->z) - Center.z;
            double const DistanceSq = (Dz * Dz + Dx * Dx) + Dy * Dy;
            // TEST AH,0x41 / JE: neither C0 nor C3 -- the padded radius is above the distance.
            if (static_cast<double>(RadiusSq) + GObjectClearanceSq > DistanceSq)
                return false;
        }
        return true;
    }

    // FUN_06f61570 (06f61570-06f616c4, cdecl): Out[256] = Center + *Radius * the unit
    // directions of a 16 x 16 polar grid (polar angle i * pi/16, azimuth j * pi/8; entry
    // j * 16 + i). The table is a function-local static (guard DAT_06fd1ab8 with the atexit
    // dtor FUN_06f616d0, an empty body) built once (flag DAT_06fd0eb4).
    void GetSpherePoints(Vector const* Center, float const* Radius, Vector* Out)
    {
        static Vector Directions[256];                                              // DAT_06fd0eb8
        static bool bDirectionsBuilt = false;                                       // DAT_06fd0eb4
        if (!bDirectionsBuilt)
        {
            for (int Polar = 0; Polar < 16; ++Polar)
            {
                double const PolarAngle = Polar * static_cast<double>(0.19634954631328583f); // DAT_06fbbc50
                double const SinPolar = std::sin(PolarAngle);
                float const CosPolar = static_cast<float>(std::cos(PolarAngle));
                for (int Azimuth = 0; Azimuth < 16; ++Azimuth)
                {
                    double const AzimuthAngle = Azimuth * 0.39269909262657166;       // DAT_06fbbc48
                    Vector& Direction = Directions[Azimuth * 16 + Polar];
                    Direction.x = static_cast<float>(std::cos(AzimuthAngle) * SinPolar);
                    Direction.y = static_cast<float>(std::sin(AzimuthAngle) * SinPolar);
                    Direction.z = CosPolar;
                }
            }
            bDirectionsBuilt = true;
        }
        for (int Index = 0; Index < 256; ++Index)
        {
            double const R = *Radius;
            // x is added unrounded; y and z are stored as floats first (06f61665-06f616b2).
            float const Y = static_cast<float>(R * Directions[Index].y);
            float const Z = static_cast<float>(R * Directions[Index].z);
            Out[Index].x = static_cast<float>(R * Directions[Index].x + Center->x);
            Out[Index].y = Y + Center->y;
            Out[Index].z = Z + Center->z;
        }
    }

    // TEST AH,0x41 / JP of FUN_06f61800: every axis of Point is within the squared tolerance
    // of End (C0 or C3 on each compare).
    bool IsAtSegmentEnd(Vector const& Point, Vector const& End)
    {
        double const Dx = static_cast<double>(Point.x) - End.x;
        double const Dy = static_cast<double>(Point.y) - End.y;
        double const Dz = static_cast<double>(Point.z) - End.z;
        return Dx * Dx <= GSegmentEndToleranceSq && Dy * Dy <= GSegmentEndToleranceSq &&
               Dz * Dz <= GSegmentEndToleranceSq;
    }

    // FUN_06f61800 (06f61800-06f61cab, cdecl, 12 args): a spawn point on the segment
    // SegmentStart-SegmentEnd. The walk starts at the segment point nearest to Position and
    // steps 200 towards one end chosen at random (up to 15 points); the second and third pass
    // walk the other way. A point must be inside every InsideSpheres sphere, outside every
    // OutsideSpheres sphere, within MaxZoneDistance of every NearZones zone (unless it is -1),
    // outside every AvoidZones zone, have no object within 200 (solar detection only) and be
    // clear of objects. *OutTarget = the end walked towards, *OutDirection = -1 / 1 (the
    // second pass flips both).
    bool FindSegmentSpawnPoint(std::vector<FSpacePopExclusionSphere> const* OutsideSpheres,
                               std::vector<FSpacePopExclusionSphere> const* InsideSpheres,
                               std::vector<FSystemZone*> const* AvoidZones,
                               std::vector<FSystemZone*> const* NearZones, float MaxZoneDistance,
                               Vector const* Position, Vector const* SegmentStart,
                               Vector const* SegmentEnd, Vector* OutPosition, Vector* OutTarget,
                               int* OutDirection, FSystemInfo* System)
    {
        Vector Closest;
        DistanceSquaredToSegment(Position, SegmentStart, SegmentEnd, &Closest);    // FUN_06f611a0
        Vector const Delta = Subtract(*SegmentEnd, *SegmentStart);                  // FUN_06eb7810
        double const InvLength = 1.0 / std::sqrt(
            (static_cast<double>(Delta.x) * Delta.x + static_cast<double>(Delta.y) * Delta.y) +
            static_cast<double>(Delta.z) * Delta.z);                                // DAT_06fb5810
        Vector Step;
        Step.x = static_cast<float>(Delta.x * InvLength);
        Step.y = static_cast<float>(Delta.y * InvLength);
        Step.z = static_cast<float>(Delta.z * InvLength);

        Vector Far;
        if (RandCoin() != 0)
        {
            Step = Scale(Step, 200.0f);                                             // DAT_06fb8578
            *OutDirection = -1;
            *OutTarget = *SegmentStart;
            Far = *SegmentEnd;
        }
        else
        {
            Step = Scale(Step, -200.0f);                                            // DAT_06fbbc58
            *OutDirection = 1;
            *OutTarget = *SegmentEnd;
            Far = *SegmentStart;
        }

        bool bFound = false;
        for (int Pass = 0; Pass <= 2 && !bFound; ++Pass)
        {
            if (Pass == 1)
            {
                *OutTarget = Far;
                Step = Scale(Step, -1.0f);                                          // DAT_06fb5e84
                *OutDirection = -*OutDirection;
            }
            for (int StepIndex = 0; StepIndex < 15; ++StepIndex)
            {
                Vector Offset;                                                      // FUN_06eb7810
                Offset.x = static_cast<float>(static_cast<double>(Step.x) * StepIndex);
                Offset.y = static_cast<float>(static_cast<double>(Step.y) * StepIndex);
                Offset.z = static_cast<float>(static_cast<double>(Step.z) * StepIndex);
                *OutPosition = Add(Offset, Closest);

                Vector OnSegment;
                DistanceSquaredToSegment(OutPosition, SegmentStart, SegmentEnd, &OnSegment);
                // 06f61a86-06f61b2c: the walk left the segment at an end -> next pass.
                if (IsAtSegmentEnd(OnSegment, *SegmentStart) || IsAtSegmentEnd(OnSegment, *SegmentEnd))
                    break;

                if (!TestExclusionSpheres(&OutPosition->x, InsideSpheres, &GTrue, &GTrue))
                    continue;
                if (!TestExclusionSpheres(&OutPosition->x, OutsideSpheres, &GTrue, &GFalse))
                    continue;

                bool bZoneFailed = false;
                if (NearZones != nullptr)
                {
                    for (std::vector<FSystemZone*>::const_iterator Zone = NearZones->begin();
                         Zone != NearZones->end(); ++Zone)
                    {
                        // TEST AH,0x44 / JNP: -1 turns the distance test off.
                        if (MaxZoneDistance == -1.0f)
                            continue;
                        // TEST AH,0x41 / JE: fails when the distance is above the limit.
                        if (pub::Zone::GetDistance((*Zone)->ZoneId, *OutPosition) > MaxZoneDistance)
                        {
                            bZoneFailed = true;
                            break;
                        }
                    }
                }
                if (bZoneFailed)
                    continue;
                if (AvoidZones != nullptr)
                {
                    for (std::vector<FSystemZone*>::const_iterator Zone = AvoidZones->begin();
                         Zone != AvoidZones->end(); ++Zone)
                    {
                        // TEST AH,0x41 / JNP against 1.0 (DAT_06fb70c0): inside or on the zone.
                        if (pub::Zone::ClassifyPoint((*Zone)->ZoneId, *OutPosition) <= 1.0)
                        {
                            bZoneFailed = true;
                            break;
                        }
                    }
                }
                if (bZoneFailed)
                    continue;

                if (SpacePopGetSolarDetection())                                    // FUN_06f5a7e0
                {
                    unsigned int const SystemId = System->SystemId;
                    unsigned int Ids[0x20];
                    unsigned int Count;
                    pub::System::ScanObjects(SystemId, Ids, 0x20, *OutPosition, 200.0f, 0x7ffdff,
                                             Count);
                    if (Count != 0)
                        continue;
                }
                if (IsClearOfObjects(OutPosition, System))                           // FUN_06f61350
                {
                    bFound = true;
                    break;
                }
            }
        }
        return bFound;
    }

    // FUN_06f61cb0 (06f61cb0-06f62116, cdecl, 17 args): a spawn point on the sphere of
    // Radius around Center (GetSpherePoints) whose height is within CylinderHeight / 2 of
    // LookFrom's, inside every InsideSpheres sphere and outside every OutsideSpheres sphere;
    // the candidates are drawn at random. Each draw must be within MaxZoneDistance of every
    // NearZones zone (unless -1); then two attempts look for *OutFrom = LookFrom + a random
    // point of the cylinder (CylinderRadius, CylinderHeight) at the candidate's height with no
    // object within 200 of it (solar detection only; up to four draws), a clear segment to the
    // candidate and a candidate clear of objects. AvoidZones is queried (ClassifyPoint up to the
    // first zone the point is in) but its result is not used.
    bool FindSphereSpawnPoint(std::vector<FSpacePopExclusionSphere> const* OutsideSpheres,
                              std::vector<FSpacePopExclusionSphere> const* InsideSpheres,
                              std::vector<FSystemZone*> const* AvoidZones,
                              std::vector<FSystemZone*> const* NearZones, float MaxZoneDistance,
                              Vector Center, float Radius, Vector LookFrom, float CylinderRadius,
                              float CylinderHeight, Vector* OutPosition, Vector* OutFrom,
                              FSystemInfo* System)
    {
        Vector Points[256];
        GetSpherePoints(&Center, &Radius, Points);                                  // FUN_06f61570
        std::vector<std::pair<unsigned int, unsigned int>> Candidates;
        Candidates.reserve(256);                                                    // 06f61d02-06f61d73
        double const HalfHeight = CylinderHeight * 0.5;                             // DAT_06fb74e0
        double const MaxHeight = LookFrom.y + HalfHeight;
        for (unsigned int Polar = 0; Polar < 16; ++Polar)
        {
            for (unsigned int Azimuth = 0; Azimuth < 16; ++Azimuth)
            {
                *OutPosition = Points[Polar * 16 + Azimuth];
                // 06f61dcc-06f61df1: TEST AH,0x41 / JE, then (LookFrom.y - half) FCOMPP.
                bool bFits = true;
                if (OutPosition->y > MaxHeight)
                    bFits = false;
                else if (LookFrom.y - HalfHeight > OutPosition->y)
                    bFits = false;
                if (!TestExclusionSpheres(&OutPosition->x, InsideSpheres, &GTrue, &GTrue))
                    bFits = false;
                if (TestExclusionSpheres(&OutPosition->x, OutsideSpheres, &GTrue, &GFalse) && bFits)
                    Candidates.push_back(std::make_pair(Polar, Azimuth));            // FUN_06f96770
            }
        }

        bool bFound = false;
        while (!Candidates.empty() && !bFound)
        {
            // 06f61ec5-06f61f01: floor(rand() * 2^-15 * (count - 1) + 0.5), FTOL.
            double const Scaled = std::floor(
                static_cast<double>(rand()) * GRandScale *
                    static_cast<double>(static_cast<long long>(Candidates.size() - 1)) +
                0.5);
            size_t const Pick = static_cast<size_t>(static_cast<int>(Scaled));
            *OutPosition = Points[Candidates[Pick].first * 16 + Candidates[Pick].second];
            Candidates.erase(Candidates.begin() + static_cast<std::ptrdiff_t>(Pick));

            bool bZoneFailed = false;
            if (NearZones != nullptr)
            {
                for (std::vector<FSystemZone*>::const_iterator Zone = NearZones->begin();
                     Zone != NearZones->end(); ++Zone)
                {
                    if (MaxZoneDistance == -1.0f)
                        continue;
                    if (pub::Zone::GetDistance((*Zone)->ZoneId, *OutPosition) > MaxZoneDistance)
                    {
                        bZoneFailed = true;
                        break;
                    }
                }
            }
            if (bZoneFailed)
                continue;
            if (AvoidZones != nullptr)
            {
                // 06f61fb2-06f61fe9: both exits of this loop continue at 06f61feb.
                for (std::vector<FSystemZone*>::const_iterator Zone = AvoidZones->begin();
                     Zone != AvoidZones->end(); ++Zone)
                {
                    if (pub::Zone::ClassifyPoint((*Zone)->ZoneId, *OutPosition) <= 1.0)
                        break;
                }
            }

            for (int Attempt = 1; Attempt < 3 && !bFound; ++Attempt)
            {
                unsigned int Tries = 0;
                bool bScanClear;
                do
                {
                    Vector const Offset = random_inside_cylinder(CylinderRadius, CylinderHeight);
                    OutFrom->x = LookFrom.x + Offset.x;
                    OutFrom->y = LookFrom.y + Offset.y;
                    OutFrom->z = LookFrom.z + Offset.z;
                    OutFrom->y = OutPosition->y;                                    // 06f62053
                    ++Tries;
                    bScanClear = true;
                    if (SpacePopGetSolarDetection())                                // FUN_06f5a7e0
                    {
                        unsigned int const SystemId = System->SystemId;
                        unsigned int Ids[0x20];
                        unsigned int Count;
                        pub::System::ScanObjects(SystemId, Ids, 0x20, *OutFrom, 200.0f, 0x7ffdff,
                                                 Count);
                        if (Count != 0)
                            bScanClear = false;
                    }
                } while (!bScanClear && Tries <= 3);
                // 06f620cd-06f620ef: a fourth draw fails even when its scan is clear.
                if (bScanClear && Tries <= 3 && IsSegmentClear(OutFrom, OutPosition, System) &&
                    IsClearOfObjects(OutPosition, System))
                {
                    bFound = true;
                }
            }
        }
        return bFound;
    }

    // FUN_06f62950 (06f62950-06f62aa7, cdecl, recursive): from ring *Index along *Direction,
    // the first ring that has a ring behind it (Index - Direction) and lies farther than 5000
    // from *Position (distance by the reciprocal square root estimate); -1 / -1 and false when
    // the walk leaves the lane.
    bool FindLaneEntryRings(std::vector<FTradeLaneRing> const* Rings, int const* Index,
                            int const* Direction, Vector const* Position, int* OutRing,
                            int* OutPrevRing)
    {
        unsigned int const Count = static_cast<unsigned int>(Rings->size());
        int const Current = *Index;
        if (static_cast<unsigned int>(Current) >= Count)
        {
            *OutRing = -1;
            *OutPrevRing = -1;
            return false;
        }
        Vector const RingPosition = (*Rings)[static_cast<size_t>(Current)].Position;
        bool bCheck = false;
        if (*Direction == 1 && Current != 0)
            bCheck = true;
        else if (*Direction == -1 && static_cast<unsigned int>(Current) < Count - 1)
            bCheck = true;
        if (bCheck)
        {
            double const Dx = static_cast<double>(RingPosition.x) - Position->x;
            double const Dy = static_cast<double>(RingPosition.y) - Position->y;
            double const Dz = static_cast<double>(RingPosition.z) - Position->z;
            double const LengthSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
            float const LengthSqF = static_cast<float>(LengthSq);                  // FST dword
            int Bits;
            std::memcpy(&Bits, &LengthSqF, sizeof(Bits));
            Bits = 0x5f3759df - (Bits >> 1);
            float Guess;
            std::memcpy(&Guess, &Bits, sizeof(Guess));
            double const Length =
                ((1.5f - static_cast<double>(Guess) * Guess * LengthSq * 0.5f) * Guess) * LengthSq;
            // FILD qword DAT_06fd0eb0 / FCOMPP, TEST AH,5 / JP: found when 5000 is below it.
            if (static_cast<double>(GTradeLaneEntryDistance) < Length)
            {
                *OutRing = Current;
                *OutPrevRing = Current - *Direction;
                return true;
            }
        }
        int const Next = Current + *Direction;
        return FindLaneEntryRings(Rings, &Next, Direction, Position, OutRing, OutPrevRing);
    }

    // FUN_06f62ab0 (06f62ab0-06f62f4d, cdecl): the entry rings of one lane for the request
    // (FindLaneEntryRings from the ring nearest to the request's position -- the scan stops
    // when the distances start to grow --, towards the lane's far end, or a random way from a
    // middle ring). The result also depends on the lane end the ships come from: when a
    // player's ship is within 4000 of it, sees it (4000, 70 degrees) or has a +0x124 object
    // within 4000 of it, the result is !bMiddle, otherwise bMiddle (the request is not the
    // heartbeat population at the ship, or the ship is in a lane zone and the nearest ring is
    // not an end ring).
    bool PickLaneEntry(FSystemInfo* System, std::vector<FTradeLaneRing> const* Rings,
                       FSpawnRequest const* Request, bool const* bInLaneZone, int* OutRing,
                       int* OutPrevRing)
    {
        Vector const& Position = Request->Placement.Position;
        int Nearest = -1;
        float NearestDistanceSq = -1.0f;
        int Index = 0;
        for (std::vector<FTradeLaneRing>::const_iterator Ring = Rings->begin(); Ring != Rings->end();
             ++Ring, ++Index)
        {
            double const Dx = static_cast<double>(Position.x) - Ring->Position.x;
            double const Dy = static_cast<double>(Position.y) - Ring->Position.y;
            double const Dz = static_cast<double>(Position.z) - Ring->Position.z;
            double const DistanceSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
            // 06f62b2a-06f62b59: the first ring, or a nearer one, is taken; a farther one ends
            // the scan; an equal one goes on.
            if (NearestDistanceSq == -1.0f || static_cast<double>(NearestDistanceSq) > DistanceSq)
            {
                Nearest = Index;
                NearestDistanceSq = static_cast<float>(DistanceSq);
            }
            else if (static_cast<double>(NearestDistanceSq) < DistanceSq)
            {
                break;
            }
        }

        int const LastRing = static_cast<int>(Rings->size()) - 1;
        // 06f62b7e-06f62bbf: kept in the Request argument's slot.
        bool bMiddle;
        if (Request->ZoneList != 0)
            bMiddle = true;
        else if (!*bInLaneZone || Nearest == 0 || Nearest == LastRing)
            bMiddle = false;
        else
            bMiddle = true;

        Vector End;
        if (Nearest == 0)
        {
            int Start = 0;
            int Direction = 1;
            if (!FindLaneEntryRings(Rings, &Start, &Direction, &Position, OutRing, OutPrevRing))
                return false;
            End = Rings->front().Position;
        }
        else if (Nearest == LastRing)
        {
            int Start = Nearest;
            int Direction = -1;
            if (!FindLaneEntryRings(Rings, &Start, &Direction, &Position, OutRing, OutPrevRing))
                return false;
            End = Rings->back().Position;
        }
        else
        {
            int FirstDirection;
            int SecondDirection;
            if (RandCoin() == 1)
            {
                FirstDirection = 1;
                SecondDirection = -1;
            }
            else
            {
                FirstDirection = -1;
                SecondDirection = 1;
            }
            int Start = Nearest;
            if (!FindLaneEntryRings(Rings, &Start, &FirstDirection, &Position, OutRing, OutPrevRing))
            {
                int SecondStart = Nearest;
                if (!FindLaneEntryRings(Rings, &SecondStart, &SecondDirection, &Position, OutRing,
                                        OutPrevRing))
                {
                    return false;
                }
            }
            // 06f62dec-06f62e02: SETG on prev > ring -> +1: the last ring, else the first.
            End = *OutPrevRing > *OutRing ? Rings->back().Position : Rings->front().Position;
        }

        float Radius = 4000.0f;
        float NearestPlayerSq;
        if (System->IsPlayerShipNear(&End, &Radius, &NearestPlayerSq))              // FUN_06f72dd0
            return !bMiddle;
        float MaxDistance = 4000.0f;
        float ConeAngle = 70.0f;
        if (System->IsSeenByPlayerShip(&End, &MaxDistance, &ConeAngle))             // FUN_06f730c0
            return !bMiddle;
        float ObjectRadius = 4000.0f;
        if (System->IsNearPlayerField124Object(&End, &ObjectRadius))                // FUN_06f73130
            return !bMiddle;
        return bMiddle;
    }

    // The ring-list pick of FUN_06f62f50 (06f63096-06f630d7 / 06f63143-06f63180): the only
    // list, or (rand() * count) >> 15.
    std::vector<std::vector<FTradeLaneRing>>::iterator
    PickLane(std::vector<std::vector<FTradeLaneRing>>* Lanes)
    {
        int const Count = static_cast<int>(Lanes->size());
        int const Pick = Count - 1 == 0 ? 0 : RandIndex(Count);
        return Lanes->begin() + Pick;
    }

    // FUN_06f62f50 (06f62f50-06f637da, cdecl): the trade-lane arrival point. The ring lists
    // of the lanes within 5000 of the request (in a lane zone of the heartbeat population at
    // the ship: only lanes whose first ring is in that zone) are tried in random order; a
    // lane's entry rings come from PickLaneEntry. The exit point is *Offset beyond the entry
    // ring along its z axis (or behind it, when the lane runs the other way) -- rejected when a
    // ship travelling that way is within 15000 of it or, near a lane end, one left less than 7 s
    // ago. On success *OutRingId / *OutNextRingId are the entry ring and the one before it,
    // *OutPosition is 255 above the exit point, *OutOrientation the ring's orientation (turned
    // around for the reverse direction). *OutRingId stays 0 when no lane works.
    void PickTradeLaneArrival(FSystemInfo* System, FSystemZone* Zone, FSpawnRequest const* Request,
                              unsigned int* OutRingId, unsigned int* OutNextRingId,
                              Vector* OutPosition, Matrix* OutOrientation, float const* Offset)
    {
        std::vector<std::vector<FTradeLaneRing>> Lanes;                             // FUN_06ead6a0
        *OutRingId = 0;
        bool bInLaneZone;
        if (Zone != nullptr && Zone->bTradelane && Request->ZoneList == 0)
        {
            bInLaneZone = true;
            System->FindTradeLaneRingListsNear(&Request->Placement.Position, 5000.0f, &Lanes); // FUN_06f76970
            std::vector<std::vector<FTradeLaneRing>>::iterator Lane = Lanes.begin();
            while (Lane != Lanes.end())
            {
                // TEST AH,0x41 / JNZ against 1.0: the first ring in or on the zone keeps it.
                if (!(pub::Zone::ClassifyPoint(Zone->ZoneId, Lane->front().Position) > 1.0f))
                    ++Lane;
                else
                    Lane = Lanes.erase(Lane);                                       // FUN_06f69830
            }
        }
        else
        {
            bInLaneZone = false;
            System->FindTradeLaneRingListsNear(&Request->Placement.Position, 5000.0f, &Lanes);
        }
        if (Lanes.empty())
            return;                                                                 // 06f63058

        for (;;)
        {
            // 06f63070: an emptied list clears both ring ids.
            if (Lanes.empty())
            {
                *OutNextRingId = 0;
                *OutRingId = 0;
                return;
            }
            std::vector<std::vector<FTradeLaneRing>>::iterator Lane = PickLane(&Lanes);
            int RingIndex;
            int PrevRingIndex;
            while (!PickLaneEntry(System, &*Lane, Request, &bInLaneZone, &RingIndex, &PrevRingIndex))
            {
                Lanes.erase(Lane);                                                  // FUN_06f69830
                if (Lanes.empty())
                {
                    *OutNextRingId = 0;
                    *OutRingId = 0;
                    return;
                }
                Lane = PickLane(&Lanes);
            }

            std::vector<FTradeLaneRing> const& Rings = *Lane;
            Vector RingPosition;
            Matrix RingOrientation;
            pub::SpaceObj::GetLocation(Rings[static_cast<size_t>(RingIndex)].ObjectId, RingPosition,
                                       RingOrientation);
            *OutRingId = Rings[static_cast<size_t>(RingIndex)].ObjectId;
            Vector PrevPosition;
            Matrix PrevOrientation;
            pub::SpaceObj::GetLocation(Rings[static_cast<size_t>(PrevRingIndex)].ObjectId,
                                       PrevPosition, PrevOrientation);
            Vector const LaneDirection = SubtractVectors(PrevPosition, RingPosition); // 06f63230
            *OutNextRingId = Rings[static_cast<size_t>(PrevRingIndex)].ObjectId;

            // 06f63255-06f632b8: the entry ring is next to a lane end.
            bool bNearLaneEnd = false;
            if (PrevRingIndex < RingIndex)
            {
                bNearLaneEnd = RingIndex == 1 || RingIndex == 2;
            }
            else if (PrevRingIndex > RingIndex)
            {
                int const Count = static_cast<int>(Rings.size());
                bNearLaneEnd = RingIndex == Count - 2 || RingIndex == Count - 3;
            }

            FMatrix const RingMatrix = ToFMatrix(RingOrientation);
            double const Dot = (static_cast<double>(LaneDirection.x) * RingOrientation.E[0][2] +
                                static_cast<double>(LaneDirection.y) * RingOrientation.E[1][2]) +
                               static_cast<double>(LaneDirection.z) * RingOrientation.E[2][2];
            // TEST AH,5 / JP: falls through only when the dot product is below 0.
            bool const bForward = Dot < 0.0;
            int const Direction = bForward ? 1 : -1;

            FVector Local;
            Local.X = 0.0f;
            Local.Y = 0.0f;
            Local.Z = bForward ? -*Offset : *Offset;
            FVector Rotated;
            GetSpaceMathEngine()->TransformMatrix(Rotated, RingMatrix, Local);      // slot +0x2c
            Vector ExitPoint;                                                       // FUN_06eb7810
            ExitPoint.x = Rotated.X + RingPosition.x;
            ExitPoint.y = Rotated.Y + RingPosition.y;
            ExitPoint.z = Rotated.Z + RingPosition.z;

            int LeaveDirection = Direction;
            float const LeaveTime =
                static_cast<float>(System->GetTimeSinceLaneLeave(&LeaveDirection, OutRingId)); // FUN_06f78070
            float ShipDistance = 0.0f;
            int ShipDirection = Direction;
            bool bReject = false;
            // 06f633fe-06f63413: TEST AH,0x41 / JNZ against 15000 (DAT_06fbbc60): a ship within
            // 15000 either way.
            if (System->GetLaneShipDistance(&ShipDirection, OutRingId, &ExitPoint, &ShipDistance) && // FUN_06f77e70
                !(std::fabs(static_cast<double>(ShipDistance)) > 15000.0))
            {
                bReject = true;
            }
            // 06f63415-06f63441: near a lane end, a ship left less than 7 s ago (TEST AH,1 /
            // JE against 7.0 accepts, TEST AH,5 / JNP against 0 accepts a negative time).
            else if (bNearLaneEnd && !(LeaveTime >= 7.0f) && !(LeaveTime < 0.0f))
            {
                bReject = true;
            }
            if (bReject)
            {
                Lanes.erase(Lane);                                                  // FUN_06f6a410 / FUN_06f637e0 + FUN_06f699e0
                continue;
            }

            FVector Lift;
            Lift.X = 0.0f;
            if (bForward)
            {
                Lift.Y = 255.0f;
                Lift.Z = -*Offset;
                *OutOrientation = RingOrientation;
            }
            else
            {
                // 06f636b2-06f63757: the ring's orientation with its first and third columns
                // negated; the lift is taken from the ring's own orientation.
                *OutOrientation = RingOrientation;
                for (int Row = 0; Row < 3; ++Row)
                {
                    OutOrientation->E[Row][0] = -RingOrientation.E[Row][0];
                    OutOrientation->E[Row][2] = -RingOrientation.E[Row][2];
                }
                Lift.Y = -255.0f;
                Lift.Z = *Offset;
            }
            FVector Lifted;
            GetSpaceMathEngine()->TransformMatrix(Lifted, RingMatrix, Lift);
            OutPosition->x = Lifted.X + RingPosition.x;
            OutPosition->y = Lifted.Y + RingPosition.y;
            OutPosition->z = Lifted.Z + RingPosition.z;
            return;
        }
    }

    // The formation's orientation from its forward vector, columns right, up, forward (the
    // shared tail of 06f64d7e-06f64e5f and 06f65619-06f65702): Up = Forward x Right (each
    // product stored as a float), normalized by FUN_06f0bb50.
    Matrix BuildColumnMatrix(Vector const& Right, Vector const& Forward, Vector* OutUpRaw)
    {
        Vector Up;
        Up.x = static_cast<float>(static_cast<double>(Right.z) * Forward.y -
                                  static_cast<double>(Right.y) * Forward.z);
        Up.y = static_cast<float>(static_cast<double>(Forward.z) * Right.x -
                                  static_cast<double>(Right.z) * Forward.x);
        Up.z = static_cast<float>(static_cast<double>(Right.y) * Forward.x -
                                  static_cast<double>(Forward.y) * Right.x);
        *OutUpRaw = Up;
        double const InvUp = 1.0 / VectorLength(Up);                                // DAT_06fb5810
        Matrix Out;
        Out.E[0][0] = Right.x;
        Out.E[0][1] = static_cast<float>(Up.x * InvUp);
        Out.E[0][2] = Forward.x;
        Out.E[1][0] = Right.y;
        Out.E[1][1] = static_cast<float>(Up.y * InvUp);
        Out.E[1][2] = Forward.y;
        Out.E[2][0] = Right.z;
        Out.E[2][1] = static_cast<float>(Up.z * InvUp);
        Out.E[2][2] = Forward.z;
        return Out;
    }

    // Every block of the formation gets Orientation (REP MOVSD 9 per node).
    void SetBlocksOrientation(std::list<FContentNpcShipParams>* Blocks, Matrix const& Orientation)
    {
        for (std::list<FContentNpcShipParams>::iterator Block = Blocks->begin();
             Block != Blocks->end(); ++Block)
        {
            Block->Orientation = Orientation;
        }
    }

    // FUN_06f65d20 (06f65d20-06f65f02, cdecl): arrival method 1, out of a trade lane.
    // PickTradeLaneArrival with the offset 0 for the zones ahead of the ship (request zone
    // list 2), else 3750; the entry ring must be outside every exclusion sphere of the request.
    // Plan kind 1 with both rings, the leader at the arrival point and orientation, and an
    // InstantTradelane directive (0x14) in front of the leader's list.
    bool PlaceOnTradeLane(FSpawnRequest const* Request, FSystemInfo* System, FSystemZone* Zone,
                          FSpawnPlan* Plan, std::list<FContentNpcShipParams>* Blocks)
    {
        float const Offset = Request->ZoneList == 2 ? 0.0f : 3750.0f;               // 0x456a6000
        unsigned int RingId;
        unsigned int NextRingId;
        Vector Position;
        Matrix Orientation;
        PickTradeLaneArrival(System, Zone, Request, &RingId, &NextRingId, &Position, &Orientation,
                             &Offset);                                              // FUN_06f62f50
        if (RingId == 0)
            return false;

        Vector RingPosition;
        Matrix RingOrientation;
        pub::SpaceObj::GetLocation(RingId, RingPosition, RingOrientation);         // 06f65da8
        // 06f65dae-06f65dd6: outside every sphere (bAll 1, bInside 0).
        if (!TestExclusionSpheres(&RingPosition.x, &Request->ExclusionSpheres, &GTrue, &GFalse))
            return false;

        Plan->RingId = RingId;
        Plan->NextRingId = NextRingId;
        Plan->Kind = 1;
        FContentNpcShipParams& Leader = Blocks->front();
        Leader.Position = Position;
        Leader.Orientation = Orientation;

        // The holder in the first argument's slot (EH state 0).
        TRefHandle<FObjListDirective> Directive;
        CreateObjListDirective(0x14, &Directive.Pointer);                           // FUN_06f24870
        // 06f65e65-06f65e7a: the offset's float bits go to the op's +0x14.
        unsigned int OffsetBits;
        std::memcpy(&OffsetBits, &Offset, sizeof(OffsetBits));
        static_cast<FObjListInstantTradelane*>(Directive.Pointer)->SetLane(&RingId, OffsetBits,
                                                                           &NextRingId); // FUN_06f27870
        AddLeaderDirective(&Leader, Directive);
        return true;
    }

    // FUN_06f65900 (06f65900-06f65d14, cdecl): arrival methods 2..6, launched from a dock.
    // Only a formation of one ship. The object types come from Methods[3..6] (docking ring,
    // jump gate, station, capital; an arrival drops rings and gates). Ships that can use
    // moorings look within 4000 of the request and must not be nearer than 3500 (unless an
    // arrival: not farther); other ships look within 2700 (GEncounterPlacementRange) and must
    // be within it -- ships without berths only from rings / gates / stations with type bits
    // 0xc40; arrivals of ships without moorings never launch. Three picks (PickDockTarget,
    // excluding the previous pick); each launcher must be outside the request's spheres and
    // 2500 around the other players' ships. Plan kind 2 with the launcher in DockTarget.
    bool PlaceAtDock(FSpawnRequest const* Request, FSystemInfo* System, FSystemZone* Zone,
                     std::vector<unsigned char> const* Methods, FSpawnPlan* Plan,
                     unsigned int Faction, std::list<FContentNpcShipParams>* Blocks)
    {
        (void)Zone;
        if (Blocks->size() != 1)                                                    // 06f65920
            return false;

        unsigned char const* Method = Methods->data();
        unsigned int Mask = (Method[5] != 0 ? 0x100u : 0u) | (Method[6] != 0 ? 0x200000u : 0u) |
                            (Method[4] != 0 ? 0x40u : 0u) | (Method[3] != 0 ? 0x20u : 0u);
        FContentNpcShipParams const& Block = Blocks->front();
        if (Request->bArrival)
            Mask &= 0xfffff39fu;                                                    // 06f6599c

        bool bTry = true;                                                           // arg slot 7
        bool bNear = false;                                                         // arg slot 4
        float Range = 0.0f;
        std::vector<unsigned int> const& Classes = Block.ClassIds;
        if (HasClass(Classes, GIdCanUseSmallMoors) || HasClass(Classes, GIdCanUseMediumMoors) ||
            HasClass(Classes, GIdCanUseLargeMoors))                                 // DAT_06fd1c38/34/30
        {
            Range = 4000.0f;                                                        // 0x457a0000
            bNear = false;
        }
        else if (HasClass(Classes, GIdCanUseBerths))                                // DAT_06fd1c3c
        {
            if (Request->bArrival)
            {
                bTry = false;
            }
            else
            {
                Range = GEncounterPlacementRange;                                   // DAT_06fcf2f4
                bNear = true;
            }
        }
        else if (Request->bArrival || (Mask & 0xc40) == 0)
        {
            bTry = false;
        }
        else
        {
            Range = GEncounterPlacementRange;
            bNear = true;
        }

        Plan->DockTarget = 0;
        std::vector<FSpacePopExclusionSphere> Spheres;
        bool bFound = false;
        for (int Try = 0; Try < 3; ++Try)
        {
            if (bFound || !bTry)
                break;
            Plan->DockTarget = System->PickDockTarget(&Block.ClassIds, &GNoShipRep, &Faction,
                                                      &Plan->DockTarget,
                                                      &Request->Placement.Position, Range,
                                                      &Mask);                       // FUN_06f724f0
            if ((Plan->DockTarget & 0x3fffffff) == 0)
                continue;
            Vector Position;
            Matrix Orientation;
            pub::SpaceObj::GetLocation(Plan->DockTarget, Position, Orientation);   // 06f65aff
            // 06f65b05-06f65bbc: the spheres are gathered once (the request's, then the other
            // players' ships).
            if (Spheres.empty())
            {
                for (std::vector<FSpacePopExclusionSphere>::const_iterator Sphere =
                         Request->ExclusionSpheres.begin();
                     Sphere != Request->ExclusionSpheres.end(); ++Sphere)
                {
                    Spheres.push_back(*Sphere);                                     // FUN_06f69d50
                }
                AddOtherPlayerSpheres(System, Request, &Spheres);
            }
            if (!TestExclusionSpheres(&Position.x, &Spheres, &GTrue, &GFalse))       // FUN_06f616e0
                continue;

            Vector const& From = Request->Placement.Position;
            double const Dx = static_cast<double>(Position.x) - From.x;
            double const Dy = static_cast<double>(Position.y) - From.y;
            double const Dz = static_cast<double>(Position.z) - From.z;
            double const DistanceSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
            if (bNear)
            {
                // 06f65bfd-06f65c3a: TEST AH,5 / JNP -- fails when range^2 is below it.
                if (static_cast<double>(GEncounterPlacementRange) * GEncounterPlacementRange <
                    DistanceSq)
                    continue;
            }
            else if (!Request->bArrival)
            {
                // 06f65c49-06f65c7e: fails when it is below 3500^2.
                if (DistanceSq < GDockLaunchDistanceSq)
                    continue;
            }
            else
            {
                // 06f65c84-06f65cb9: TEST AH,0x41 / JE -- fails when it is above 3500^2.
                if (DistanceSq > GDockLaunchDistanceSq)
                    continue;
            }
            Plan->Kind = 2;
            bFound = true;
        }
        return bFound;
    }

    // FUN_06f64800 (06f64800-06f64fb7, cdecl): arrival method 7, cruising in. Exclusion
    // spheres: 2500 around the other players' ships, the request's, and one around the
    // request's position. A patrol-path leader (guide type 3; the description is not
    // null-tested) spawns on a random segment of the zone's path (FindSegmentSpawnPoint, sphere
    // 200 for arrivals, 2500 else; plan Path / PathTargetPoint / PathDirection); every other
    // formation on a sphere around the request (arrival: radius 100 + rand * 1775, sphere 100;
    // else around the position in 5 s, radius 2500 + rand * 50, looking from the position in
    // 7 s, sphere 2500) through FindSphereSpawnPoint (cylinder 2000 x 500). Plan kind 7; every
    // block faces the destination; the leader gets a GotoVec (500 on a path, else 100).
    bool PlaceCruising(FSpawnRequest const* Request, FSystemInfo* System, FSystemZone* Zone,
                       double const* ZoneCreationDistance, FSpawnPlan* Plan,
                       std::list<FContentNpcShipParams>* Blocks)
    {
        std::vector<FSpacePopExclusionSphere> Spheres;                              // local_70
        std::vector<FSystemZone*> Zones;                                            // local_60
        if (Zone != nullptr)
            Zones.push_back(Zone);                                                  // FUN_06eb5770
        AddOtherPlayerSpheres(System, Request, &Spheres);
        for (std::vector<FSpacePopExclusionSphere>::const_iterator Sphere =
                 Request->ExclusionSpheres.begin();
             Sphere != Request->ExclusionSpheres.end(); ++Sphere)
        {
            Spheres.push_back(*Sphere);                                             // FUN_06f695a0
        }

        Vector const& RequestPosition = Request->Placement.Position;
        Vector SpawnPosition;                                                       // local_78
        Vector Destination;                                                         // local_6c
        bool bFound;
        bool const bPathLeader = Blocks->front().GuideDesc->Type == 3;
        if (bPathLeader)
        {
            FPatrolPath* Path = Zone->GetPath();                                    // FUN_06fa8130
            FSpacePopExclusionSphere const Around = {
                RequestPosition.x, RequestPosition.y, RequestPosition.z,
                Request->bArrival ? 200.0f : 2500.0f};                              // 0x43480000 / 0x451c4000
            Spheres.push_back(Around);
            std::vector<FPatrolPathSegment> Segments;
            Path->GetZoneSegments(Zone->ZoneId, &Segments);                         // FUN_06f37ea0
            // 06f649b1-06f649df: the segment is not range-checked (an empty list is read).
            FPatrolPathSegment const* Segment =
                Segments.data() + RandIndex(static_cast<int>(Segments.size()));
            bFound = FindSegmentSpawnPoint(&Spheres, nullptr, nullptr, &Zones,
                                           static_cast<float>(*ZoneCreationDistance),
                                           &RequestPosition,
                                           &Path->Points[static_cast<size_t>(Segment->FirstPoint)],
                                           &Path->Points[static_cast<size_t>(Segment->SecondPoint)],
                                           &SpawnPosition, &Destination, &Plan->PathDirection,
                                           System);                                 // FUN_06f61800
            if (bFound)
            {
                Plan->Path = Path;
                Plan->PathTargetPoint =
                    Plan->PathDirection == 1 ? Segment->SecondPoint : Segment->FirstPoint;
            }
        }
        else
        {
            Vector Center;
            Vector LookFrom;
            float Radius;
            if (Request->bArrival)
            {
                // 06f64a85-06f64af2: rand * 2^-15 * 1775 + 100 (DAT_06fbbc80 / DAT_06fbbc78).
                Radius = static_cast<float>(static_cast<double>(rand()) * GRandScale * 1775.0 + 100.0);
                Center = RequestPosition;
                LookFrom = RequestPosition;
                FSpacePopExclusionSphere const Around = {
                    RequestPosition.x, RequestPosition.y, RequestPosition.z, 100.0f};
                Spheres.push_back(Around);
            }
            else
            {
                // 06f64afc-06f64bdf: rand * 2^-15 * 50 + 2500 (DAT_06fbbc70 / DAT_06fbbc68).
                Radius = static_cast<float>(static_cast<double>(rand()) * GRandScale * 50.0 + 2500.0);
                Center = Add(RequestPosition, Scale(Request->Velocity, 5.0f));      // FUN_06f60760 / FUN_06f0bb80
                Vector Ahead;                                                       // FUN_06eb7810
                Ahead.x = Request->Velocity.x * 7.0f;                               // DAT_06fbbc5c
                Ahead.y = Request->Velocity.y * 7.0f;
                Ahead.z = Request->Velocity.z * 7.0f;
                LookFrom = Add(RequestPosition, Ahead);
                FSpacePopExclusionSphere const Around = {
                    RequestPosition.x, RequestPosition.y, RequestPosition.z, 2500.0f};
                Spheres.push_back(Around);                                          // FUN_06f69d50
            }
            bFound = FindSphereSpawnPoint(&Spheres, nullptr, nullptr, &Zones,
                                          static_cast<float>(*ZoneCreationDistance), Center, Radius,
                                          LookFrom, 2000.0f, 500.0f, &SpawnPosition, &Destination,
                                          System);                                  // FUN_06f61cb0
        }
        if (!bFound)
            return false;

        Plan->Position = SpawnPosition;
        Plan->Kind = 7;
        Plan->Destination = Destination;
        Blocks->front().Position = SpawnPosition;

        // 06f64cc3-06f64d1c: forward = (spawn - destination) normalized, the squares summed
        // x, z, y.
        Vector const Delta = SubtractVectors(SpawnPosition, Destination);           // 06f64cd2
        double const InvLength = 1.0 / std::sqrt(
            (static_cast<double>(Delta.x) * Delta.x + static_cast<double>(Delta.z) * Delta.z) +
            static_cast<double>(Delta.y) * Delta.y);
        Vector Forward;
        Forward.x = static_cast<float>(Delta.x * InvLength);
        Forward.y = static_cast<float>(Delta.y * InvLength);
        Forward.z = static_cast<float>(Delta.z * InvLength);
        // 06f64d1e-06f64d7e: right = (0,1,0) x forward, unrounded, scaled by the float
        // reciprocal of its length (DAT_06fb444c = 0).
        double const Y0 = static_cast<double>(Forward.y) * 0.0f;
        double const RightX = Forward.z - Y0;
        double const RightY = static_cast<double>(Forward.x) * 0.0f - static_cast<double>(Forward.z) * 0.0f;
        double const RightZ = Y0 - Forward.x;
        float const InvRight = static_cast<float>(
            1.0 / std::sqrt((RightX * RightX + RightY * RightY) + RightZ * RightZ));
        Vector Right;
        Right.x = static_cast<float>(RightX * InvRight);
        Right.y = static_cast<float>(RightY * InvRight);
        Right.z = static_cast<float>(RightZ * InvRight);
        Vector UpRaw;
        Matrix const Orientation = BuildColumnMatrix(Right, Forward, &UpRaw);
        SetBlocksOrientation(Blocks, Orientation);

        // 06f64e85-06f64f4a: the leader's GotoVec to the destination (EH state 3 holder).
        TRefHandle<FObjListDirective> Directive;
        CreateObjListDirective(5, &Directive.Pointer);                              // FUN_06f24870
        FObjListGotoVec* Goto = static_cast<FObjListGotoVec*>(Directive.Pointer);
        Goto->Op.Priority = 1;                                                      // [dir+0x114]
        // The guide type is read again from the leader's description (not null-tested).
        float const Range = Blocks->front().GuideDesc->Type == 3 ? 500.0f : 100.0f;
        Goto->SetDestination(&Destination, Range, -1.0f, 0, false, false);          // FUN_06f26460
        Goto->Op.Field_58 = 1;                                                      // [dir+0x164]
        AddLeaderDirective(&Blocks->front(), Directive);
        return true;
    }

    // FUN_06f64fc0 (06f64fc0-06f658cb, cdecl): arrival method 8, buzzing the player. Exclusion
    // spheres: the request's, then 2500 around the other players' ships, and (not arrivals)
    // 2500 around the request's position. A ship slower than 5 gets a point on a sphere of
    // radius 2500 + rand * 50 around it; a moving one a point on a 45 sphere 150 above its
    // path (2500 + speed * 2.5 ahead), looking from 5000 behind it -- or, when that direction
    // is straight up / down, the stationary case around the ship looking from 5000 behind.
    // FindSphereSpawnPoint (cylinder 50 x 50). Plan kind 8; every block faces the destination,
    // the leader is moved 50 along the up axis and gets a GotoVec (401) to the destination.
    bool PlaceBuzzing(FSpawnRequest const* Request, FSystemInfo* System, FSystemZone* Zone,
                      double const* ZoneCreationDistance, FSpawnPlan* Plan,
                      std::list<FContentNpcShipParams>* Blocks)
    {
        Vector Velocity = Request->Velocity;                                        // local_c0
        std::vector<FSpacePopExclusionSphere> Spheres;                              // local_5c
        std::vector<FSystemZone*> Zones;                                            // local_80
        Zones.push_back(Zone);                                                      // FUN_06eb5770, not null-tested
        for (std::vector<FSpacePopExclusionSphere>::const_iterator Sphere =
                 Request->ExclusionSpheres.begin();
             Sphere != Request->ExclusionSpheres.end(); ++Sphere)
        {
            Spheres.push_back(*Sphere);                                             // FUN_06f695a0
        }
        AddOtherPlayerSpheres(System, Request, &Spheres);

        Vector const& RequestPosition = Request->Placement.Position;
        // 06f650de-06f6511c: |v| = |v|^2 * FUN_06f5aeb0(|v|^2); TEST AH,0x41 / JP against 5.
        float const SpeedSq = static_cast<float>(
            (static_cast<double>(Velocity.x) * Velocity.x + static_cast<double>(Velocity.y) * Velocity.y) +
            static_cast<double>(Velocity.z) * Velocity.z);
        double const Speed = FastReciprocalSqrt(SpeedSq) * SpeedSq;                 // 06f65105
        Vector Center;
        Vector LookFrom;
        float Radius;
        // JP is taken when neither C0 nor C3 is set or the compare is unordered: slow only
        // when the speed is at most 5.
        if (Speed <= 5.0)                                                           // DAT_06fb6280
        {
            Center = RequestPosition;
            LookFrom = RequestPosition;
            Radius = static_cast<float>(static_cast<double>(rand()) * GRandScale * 50.0 + 2500.0);
        }
        else
        {
            // 06f6516e-06f6519b: the direction of travel.
            double const InvSpeed = 1.0 / VectorLength(Velocity);
            Vector Direction;
            Direction.x = static_cast<float>(Velocity.x * InvSpeed);
            Direction.y = static_cast<float>(Velocity.y * InvSpeed);
            Direction.z = static_cast<float>(Velocity.z * InvSpeed);
            // 06f6519d-06f651e1: 2500 + speed * 2.5 ahead (DAT_06fbbc90 / DAT_06fbbc68).
            float const SpeedSq2 = static_cast<float>(
                (static_cast<double>(Request->Velocity.x) * Request->Velocity.x +
                 static_cast<double>(Request->Velocity.y) * Request->Velocity.y) +
                static_cast<double>(Request->Velocity.z) * Request->Velocity.z);
            float const AheadDistance = static_cast<float>(
                FastReciprocalSqrt(SpeedSq2) * SpeedSq2 * 2.5 + 2500.0);           // 06f651c4
            Vector const Ahead = Add(RequestPosition, Scale(Direction, AheadDistance));
            Center = Ahead;                                                         // local_8c
            Vector const Behind =
                SubtractVectors(RequestPosition, Scale(Direction, 5000.0f));        // 06f65241, 0x459c4000
            LookFrom = Behind;
            // 06f65246-06f652e4: the direction from behind to ahead, and right = (0,1,0) x it.
            Vector const Track = SubtractVectors(Ahead, Behind);                    // 06f6526d
            double const InvTrack = 1.0 / VectorLength(Track);
            Vector TrackDirection;
            TrackDirection.x = static_cast<float>(Track.x * InvTrack);
            TrackDirection.y = static_cast<float>(Track.y * InvTrack);
            TrackDirection.z = static_cast<float>(Track.z * InvTrack);
            double const Y0 = static_cast<double>(TrackDirection.y) * 0.0f;
            Vector Right;                                                           // FUN_06eb7810
            Right.x = static_cast<float>(TrackDirection.z - Y0);
            Right.y = static_cast<float>(static_cast<double>(TrackDirection.x) * 0.0f -
                                         static_cast<double>(TrackDirection.z) * 0.0f);
            Right.z = static_cast<float>(Y0 - TrackDirection.x);
            // 06f652e9-06f6531a: TEST AH,0x44 / JP against 0.0 (DAT_06fbbc88) per component.
            if (Right.x == 0.0 && Right.y == 0.0 && Right.z == 0.0)
            {
                Center = RequestPosition;
                Radius = static_cast<float>(static_cast<double>(rand()) * GRandScale * 50.0 + 2500.0);
            }
            else
            {
                double const InvRight = 1.0 / VectorLength(Right);
                Right.x = static_cast<float>(Right.x * InvRight);
                Right.y = static_cast<float>(Right.y * InvRight);
                Right.z = static_cast<float>(Right.z * InvRight);
                Vector Up;                                                          // FUN_06eb7810
                Up.x = static_cast<float>(static_cast<double>(TrackDirection.y) * Right.z -
                                          static_cast<double>(TrackDirection.z) * Right.y);
                Up.y = static_cast<float>(static_cast<double>(TrackDirection.z) * Right.x -
                                          static_cast<double>(TrackDirection.x) * Right.z);
                Up.z = static_cast<float>(static_cast<double>(TrackDirection.x) * Right.y -
                                          static_cast<double>(TrackDirection.y) * Right.x);
                double const InvUp = 1.0 / VectorLength(Up);
                Up.x = static_cast<float>(Up.x * InvUp);
                Up.y = static_cast<float>(Up.y * InvUp);
                Up.z = static_cast<float>(Up.z * InvUp);
                Vector const Raise = Scale(Up, 150.0f);                             // 0x43160000
                Center.x = Center.x + Raise.x;                                      // FUN_06f658d0
                Center.y = Center.y + Raise.y;
                Center.z = Center.z + Raise.z;
                Radius = 45.0f;                                                     // 0x42340000
            }
        }
        if (!Request->bArrival)
        {
            FSpacePopExclusionSphere const Around = {
                RequestPosition.x, RequestPosition.y, RequestPosition.z, 2500.0f};
            Spheres.push_back(Around);                                              // FUN_06f695a0
        }

        Vector SpawnPosition;                                                       // local_88
        Vector Destination;                                                         // local_94
        if (!FindSphereSpawnPoint(&Spheres, nullptr, nullptr, &Zones,
                                  static_cast<float>(*ZoneCreationDistance), Center, Radius,
                                  LookFrom, 50.0f, 50.0f, &SpawnPosition, &Destination, System))
        {
            return false;                                                           // FUN_06f61cb0
        }

        Plan->Position = SpawnPosition;
        Plan->Kind = 8;
        Plan->Destination = Destination;
        FContentNpcShipParams& Leader = Blocks->front();
        Leader.Position = SpawnPosition;

        // 06f65559-06f655e5: forward = (spawn - destination) normalized by FUN_06f0bb50, right =
        // (0,1,0) x forward stored as floats, then normalized by FUN_06f0bb50.
        Vector const Delta = SubtractVectors(SpawnPosition, Destination);           // 06f6556e
        double const InvLength = 1.0 / VectorLength(Delta);
        Vector Forward;
        Forward.x = static_cast<float>(Delta.x * InvLength);
        Forward.y = static_cast<float>(Delta.y * InvLength);
        Forward.z = static_cast<float>(Delta.z * InvLength);
        double const Y0 = static_cast<double>(Forward.y) * 0.0f;
        Vector Right;
        Right.x = static_cast<float>(Forward.z - Y0);
        Right.y = static_cast<float>(static_cast<double>(Forward.x) * 0.0f -
                                     static_cast<double>(Forward.z) * 0.0f);
        Right.z = static_cast<float>(Y0 - Forward.x);
        double const InvRight = 1.0 / VectorLength(Right);
        Right.x = static_cast<float>(Right.x * InvRight);
        Right.y = static_cast<float>(Right.y * InvRight);
        Right.z = static_cast<float>(Right.z * InvRight);
        Vector UpRaw;
        Matrix const Orientation = BuildColumnMatrix(Right, Forward, &UpRaw);
        SetBlocksOrientation(Blocks, Orientation);

        // 06f65721-06f6578b: the leader moves 50 along the (renormalized) up axis.
        double const InvUp = 1.0 / VectorLength(UpRaw);
        Vector Up;
        Up.x = static_cast<float>(UpRaw.x * InvUp);
        Up.y = static_cast<float>(UpRaw.y * InvUp);
        Up.z = static_cast<float>(UpRaw.z * InvUp);
        Vector const Lift = Scale(Up, 50.0f);                                       // 0x42480000
        Leader.Position.x = Leader.Position.x + Lift.x;                             // FUN_06f658d0
        Leader.Position.y = Leader.Position.y + Lift.y;
        Leader.Position.z = Leader.Position.z + Lift.z;

        // 06f65790-06f6583e: the GotoVec (EH state 2 holder; FUN_06f28e10 clears it first).
        TRefHandle<FObjListDirective> Directive;
        CreateObjListDirective(5, &Directive.Pointer);                              // FUN_06f24870
        FObjListGotoVec* Goto = static_cast<FObjListGotoVec*>(Directive.Pointer);
        Goto->SetDestination(&Destination, 401.0f, -1.0f, 0, false, false);         // 0x43c88000
        Goto->Op.Priority = 1;                                                      // [dir+0x114]
        Goto->Op.Field_58 = 1;                                                      // [dir+0x164]
        AddLeaderDirective(&Leader, Directive);
        return true;
    }
}

// FUN_06f65f10 (06f65f10-06f661f0, cdecl).
bool __cdecl PlaceSpawnFormation(FSpawnRequest const* Request, FSystemInfo* System,
                                 FSystemZone* Zone, double const* ZoneCreationDistance,
                                 std::vector<unsigned char> Arrival, FSpawnPlan* Plan,
                                 unsigned int const* Faction,
                                 std::list<FContentNpcShipParams>* Blocks)
{
    std::vector<unsigned char> Methods(Arrival);                                    // FUN_06f69570
    if (Request->bArrival)
    {
        // 06f65f8a-06f65fd2: arrivals may cruise in; unless the leader is a patrol-path
        // leader they may also buzz the player, but never come out of a trade lane.
        Methods[7] = 1;
        FGuideDescBase const* Desc = Blocks->front().GuideDesc;                     // FUN_06f1df30
        if (Desc == nullptr || Desc->Type != 3)
        {
            Methods[8] = 1;
            Methods[1] = 0;
        }
    }

    while (Plan->Kind == 0)
    {
        // 06f65ff0-06f6600c: no method left -> not placed.
        size_t Index = 0;
        while (Index < Methods.size() && Methods[Index] == 0)
            ++Index;
        if (Index >= Methods.size())
            return false;
        // 06f6600e-06f66056: a random start, then the next set method (wrapping).
        int const Count = static_cast<int>(Methods.size());
        unsigned int Pick = static_cast<unsigned int>((rand() * Count) / 32768);
        while (Methods[Pick % static_cast<unsigned int>(Count)] == 0)
            ++Pick;
        switch (Pick % static_cast<unsigned int>(Count))
        {
        case 1:
            PlaceOnTradeLane(Request, System, Zone, Plan, Blocks);                  // FUN_06f65d20
            Methods[1] = 0;
            break;
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
            PlaceAtDock(Request, System, Zone, &Methods, Plan, *Faction, Blocks);   // FUN_06f65900
            Methods[6] = 0;
            Methods[5] = 0;
            Methods[4] = 0;
            Methods[3] = 0;
            Methods[2] = 0;
            break;
        case 7:
            PlaceCruising(Request, System, Zone, ZoneCreationDistance, Plan, Blocks); // FUN_06f64800
            Methods[7] = 0;
            break;
        case 8:
            PlaceBuzzing(Request, System, Zone, ZoneCreationDistance, Plan, Blocks); // FUN_06f64fc0
            Methods[8] = 0;
            break;
        default:
            // Index 0 (never set by the arrival parser) or above 8: nothing; retail loops on.
            break;
        }
    }
    return Plan->Kind != 0;
}

namespace
{
    // The dock target picks of FUN_06f63cc0: the leader's classes, no ship reputation, the
    // leader's affiliation (FUN_06eb6bb0, fetched for each pick) and the type mask 0x600d60.
    unsigned int PickLeaderDockTarget(FSystemInfo* System, FContentNpcShipParams* Leader,
                                      unsigned int const* ExcludeId, Vector const* Position,
                                      float Range)
    {
        unsigned int const Mask = 0x600d60;
        unsigned int const Affiliation = Leader->ReputationHandle->GetAffiliation();
        return System->PickDockTarget(&Leader->ClassIds, &GNoShipRep, &Affiliation, ExcludeId,
                                      Position, Range, &Mask);                  // FUN_06f724f0
    }

    // The point-index filter of FUN_06f63cc0 (06f63dd8-06f63e2f, 06f64526-06f64561): drops
    // the indices outside [First, Last] (JL / JG, the raw values: -1 is not remapped).
    void KeepPointsInRange(std::vector<int>* Points, int First, int Last)
    {
        std::vector<int>::iterator It = Points->begin();
        while (It != Points->end())
        {
            if (*It < First || *It > Last)
                It = Points->erase(It);
            else
                ++It;
        }
    }
}

// FUN_06f63cc0 (06f63cc0-06f647fc, cdecl).
void __cdecl SetLeaderGuideTargets(FSystemInfo* System, FSpawnPlan* Plan,
                                   FContentNpcShipParams* Leader)
{
    FGuideDesc* Desc = static_cast<FGuideDesc*>(Leader->GuideDesc.Pointer);
    bool const bWander = Desc->Type == 4;                                           // kept in arg 3's slot
    Vector Origin;                                                                  // local_28, zeroed per pick
    Origin.x = 0.0f;
    Origin.y = 0.0f;
    Origin.z = 0.0f;

    if (Plan->Kind == 2)
    {
        // 06f63d08-06f63f06: launched from a dock -- the launcher is the first target.
        unsigned int DockTarget = Plan->DockTarget;
        unsigned int Target2 = 0;
        if (bWander)
        {
            Target2 = DockTarget;
        }
        else if (Plan->Path != nullptr)
        {
            FPatrolPath* Path = Plan->Path;
            int First = -1;
            int Last = -1;
            if ((Path->Usage & 0x10) != 0)
            {
                if (Plan->PathDirection == 1)
                {
                    First = Plan->PathTargetPoint;
                }
                else
                {
                    First = 0;
                    Last = Plan->PathTargetPoint;
                }
            }
            unsigned int const Affiliation = Leader->ReputationHandle->GetAffiliation();
            Target2 = System->PickPathDockTarget(&Leader->ClassIds, &GNoShipRep, &Affiliation,
                                                 &DockTarget, Path, First, Last, 0.0f,
                                                 &GAnyObjectType);                  // FUN_06f725c0
            std::vector<int> Points;
            Path->GetObjectPointIndices(&Target2, &Points);                         // FUN_06f37d30
            KeepPointsInRange(&Points, First, Last);
            // 06f63e40-06f63e62: not range-checked (an empty list is read).
            Plan->PathPoint = Points.data()[RandIndex(static_cast<int>(Points.size()))];
            if (Target2 == 0)
                Target2 = DockTarget;
        }
        else
        {
            Target2 = PickLeaderDockTarget(System, Leader, &DockTarget, &Origin, -1.0f);
        }
        int const Rep = Leader->ReputationHandle->GetReputation();                  // FUN_06f33f80
        Desc->SetTargets(&DockTarget, &Target2, &Leader->ClassIds, &Rep);           // FUN_06f06260
        return;
    }

    if (Plan->Kind == 1)
    {
        // 06f63f14-06f64430: out of a trade lane.
        unsigned int Target2 = 0;                                                   // [esp+0x7c]
        unsigned int Target1 = 0;                                                   // [esp+0x14]
        FTradeLane* Lane;
        System->FindTradeLaneByRing(&Plan->RingId, &Lane);                          // FUN_06f77e00
        int const Order = System->GetTradeLaneRingOrder(&Plan->RingId, &Plan->NextRingId); // FUN_06f77b40
        Vector Anchor;                                                              // local_40
        if (Order == -1)
        {
            Anchor = Lane->StartPosition;
            Target2 = PickLeaderDockTarget(System, Leader, &GNoObjectId, &Lane->EndPosition,
                                           20000.0f);                               // 0x469c4000
        }
        else if (Order == 0)
        {
            return;                                                                 // 06f647ea
        }
        else if (Order == 1)
        {
            Anchor = Lane->EndPosition;
            Target2 = PickLeaderDockTarget(System, Leader, &GNoObjectId, &Lane->StartPosition,
                                           20000.0f);
        }

        if (bWander)
        {
            if (Target2 == 0)
                Target2 = PickLeaderDockTarget(System, Leader, &GNoObjectId, &Origin, -1.0f);
            Target1 = Target2;
        }
        else
        {
            std::vector<FTradeLane*> Near;
            System->FindTradeLanesNear(&Anchor, 20000.0f, &Near);                  // FUN_06f767d0
            bool const bOneLane = Near.size() == 1;
            // 06f640dc-06f64109: TEST AH,5 / JNP -- below 0.5, or the only lane.
            if (static_cast<double>(rand()) * GRandScale < 0.5 || bOneLane)
                Target1 = PickLeaderDockTarget(System, Leader, &Target2, &Anchor, 20000.0f);
            std::vector<FTradeLane*>::iterator Own = std::find(Near.begin(), Near.end(), Lane);
            if (Own != Near.end())
                Near.erase(Own);                                                    // FUN_06f69a10
            while (Target1 == 0)
            {
                if (Near.empty())
                {
                    Target1 = PickLeaderDockTarget(System, Leader, &Target2, &Origin, -1.0f);
                    break;
                }
                int const Pick = RandIndex(static_cast<int>(Near.size()));
                FTradeLane* Other = Near[static_cast<size_t>(Pick)];
                Vector const ToStart = Subtract(Other->StartPosition, Anchor);     // FUN_06eb7810
                Vector const ToEnd = Subtract(Other->EndPosition, Anchor);
                double const StartSq = (static_cast<double>(ToStart.z) * ToStart.z +
                                        static_cast<double>(ToStart.y) * ToStart.y) +
                                       static_cast<double>(ToStart.x) * ToStart.x;
                double const EndSq = (static_cast<double>(ToEnd.z) * ToEnd.z +
                                      static_cast<double>(ToEnd.y) * ToEnd.y) +
                                     static_cast<double>(ToEnd.x) * ToEnd.x;
                // 06f6426b-06f64272: TEST AH,0x41 / JNZ -- the end only when it is farther.
                Vector const* FarEnd = EndSq > StartSq ? &Other->EndPosition : &Other->StartPosition;
                Target1 = PickLeaderDockTarget(System, Leader, &Target2, FarEnd, 20000.0f);
                Near.erase(Near.begin() + Pick);                                    // FUN_06f6a640
            }
            if (Target2 == 0)
                Target2 = PickLeaderDockTarget(System, Leader, &Target1, &Origin, -1.0f);
        }
        int const Rep = Leader->ReputationHandle->GetReputation();                  // FUN_06f33f80
        Desc->SetTargets(&Target2, &Target1, &Leader->ClassIds, &Rep);              // FUN_06f06260
        return;
    }

    // 06f64435-06f647e1: cruising, buzzing, or not placed.
    unsigned int Target1 = 0;                                                       // [esp+0x10]
    unsigned int Target2 = 0;                                                       // [esp+0x18]
    FPatrolPath* Path = Plan->Path;
    if (Path != nullptr)
    {
        int const LastPoint = static_cast<int>(Path->Points.size()) - 1;
        int First;
        int Last;
        if ((Path->Usage & 0x10) != 0)
        {
            if (Plan->PathDirection == 1)
            {
                First = Plan->PathTargetPoint;
                Last = LastPoint;
            }
            else
            {
                First = 0;
                Last = Plan->PathTargetPoint;
            }
        }
        else
        {
            First = 0;
            Last = LastPoint;
        }
        unsigned int const Affiliation = Leader->ReputationHandle->GetAffiliation();
        Target1 = System->PickPathDockTarget(&Leader->ClassIds, &GNoShipRep, &Affiliation,
                                             &GNoObjectId, Path, First, Last, 0.0f,
                                             &GAnyObjectType);                      // FUN_06f725c0
        std::vector<int> Points;
        Path->GetObjectPointIndices(&Target1, &Points);                             // FUN_06f37d30
        KeepPointsInRange(&Points, First, Last);
        Plan->PathPoint = Points.data()[RandIndex(static_cast<int>(Points.size()))];

        // 06f64595-06f64611: the second target's point range.
        int SecondFirst;
        int SecondLast;
        if (!Path->bLoop)
        {
            if ((Path->Usage & 0x10) != 0)
            {
                if (Plan->PathDirection == 1)
                {
                    SecondFirst = 0;
                    SecondLast = Plan->PathTargetPoint;
                }
                else
                {
                    SecondFirst = Plan->PathTargetPoint;
                    SecondLast = LastPoint;
                }
            }
            else
            {
                SecondFirst = 0;
                SecondLast = LastPoint;
            }
        }
        else
        {
            SecondFirst = Plan->PathTargetPoint;
            SecondLast = Plan->PathPoint;
        }
        unsigned int const SecondAffiliation = Leader->ReputationHandle->GetAffiliation();
        Target2 = System->PickPathDockTarget(&Leader->ClassIds, &GNoShipRep, &SecondAffiliation,
                                             &Target1, Path, SecondFirst, SecondLast, 5000.0f,
                                             &GAnyObjectType);                      // 0x459c4000
        if (Target2 == 0)
            Target2 = Target1;
    }
    else
    {
        // 06f6467e-06f647a1: TEST AH,5 / JP -- the first target near the leader below 0.5.
        if (static_cast<double>(rand()) * GRandScale < 0.5)
            Target1 = PickLeaderDockTarget(System, Leader, &GNoObjectId, &Leader->Position, 20000.0f);
        if (Target1 == 0)
            Target1 = PickLeaderDockTarget(System, Leader, &GNoObjectId, &Origin, -1.0f);
        Target2 = PickLeaderDockTarget(System, Leader, &Target1, &Origin, -1.0f);
    }
    if (bWander)
    {
        if (Target2 == 0)
            Target2 = Target1;
        else if (Target1 == 0)
            Target1 = Target2;
    }
    int const Rep = Leader->ReputationHandle->GetReputation();                      // FUN_06f33f80
    Desc->SetTargets(&Target2, &Target1, &Leader->ClassIds, &Rep);                  // FUN_06f06260
}
