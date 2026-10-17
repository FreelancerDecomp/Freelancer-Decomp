// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU53: the path and route helpers (SpacePaths.h). Lane CNT-AI owns this file.

#include "SpacePaths.h"

#include "BaseDB.h"               // FindContentBaseInfo, FContentBase::FindFaction
#include "BehaviorCmds.h"         // FContentShipBase (formation group +0x154)
#include "ContentInternal.h"      // SubtractVectors (FUN_06eeaee0)
#include "ContentPlayerState.h"   // FindContentPlayerStateByShip (FUN_06f49bc0)
#include "DockQueues.h"           // GetSpaceMathEngine (DAT_06fd1c70, shared with DockQueues)
#include "Encounters.h"           // SetMatrixIdentity (FUN_06efc7a0)
#include "MissionObject.h"        // FNnPathLeg, FMissionDirectiveList
#include "MissionObjList.h"       // CreateObjListDirective, FObjListDock, FObjListGotoVec
#include "Populators.h"           // SpacePopGetSolarDetection (FUN_06f5a7e0)
#include "SpaceSupport.h"         // GSpaceSupportGuidedShips
#include "SystemInfo.h"
#include "common_server.h"
#include "x86math.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace
{
    // The inlined subtraction whose three differences are passed as floats to the Vector
    // (x, y, z) ctor FUN_06eb7810 (06f5f633, 06f5f65c, 06f5fa9e, 06f605de, 06f6065b). The
    // CALL sites of FUN_06eeaee0 use the shared SubtractVectors (ContentInternal.h).
    Vector Subtract(Vector const& A, Vector const& B)
    {
        Vector Out;
        Out.x = A.x - B.x;
        Out.y = A.y - B.y;
        Out.z = A.z - B.z;
        return Out;
    }

    // FUN_06f5f4f0 (thiscall on the vector): z*z + y*y, then + x*x, on the x87 stack.
    double LengthSquaredZyx(Vector const& V)
    {
        return (static_cast<double>(V.z) * V.z + static_cast<double>(V.y) * V.y) +
               static_cast<double>(V.x) * V.x;
    }

    // The bit guess of the 0x5f3759df estimate: taken from the float of the x87 value
    // (`FST float [tmp]; MOV reg,[tmp]; SAR reg,1; SUB magic,reg`).
    float SqrtGuess(double LengthSquared)
    {
        float const Stored = static_cast<float>(LengthSquared);
        int32_t Bits;
        memcpy(&Bits, &Stored, sizeof(Bits));
        Bits = 0x5f3759df - (Bits >> 1);
        float Guess;
        memcpy(&Guess, &Bits, sizeof(Guess));
        return Guess;
    }

    // The cost form of FUN_06f5f930: ((LengthSquared / Divisor) * (1.5 - y * y *
    // LengthSquared * 0.5)) * y (06f5fa34-06f5fa54, 06f5fad4-06f5faf2, 06f5fb31-06f5fb56).
    double FastLengthOver(double LengthSquared, float Divisor)
    {
        float const Guess = SqrtGuess(LengthSquared);
        double const Step = 1.5 - (static_cast<double>(Guess) * Guess * LengthSquared) * 0.5;
        return ((LengthSquared / Divisor) * Step) * Guess;
    }

    // DAT_06fd1ac0 (16 x 0xc; built once, guard byte DAT_06fd1ab9): the unit vectors
    // (cos(i * pi / 8), 0, sin(i * pi / 8)) of FUN_06f61430. The float DAT_06fbbc44 is pi / 8.
    Vector GRingDirections[16];
    bool bRingDirectionsBuilt = false;

    // The empty ring of FUN_06f62120 / FUN_06f62290 / FUN_06f62440 / FUN_06f67050: id 0,
    // identity rotation, zero translation, reputation -1. Retail leaves the position of the
    // first three unset (it is only read when a ring is found); the port zeroes it.
    void InitEmptyRing(FTradeLaneRing* Ring)
    {
        Ring->ObjectId = 0;
        Ring->Position.x = 0.0f;
        Ring->Position.y = 0.0f;
        Ring->Position.z = 0.0f;
        SetMatrixIdentity(&Ring->Placement.Rotation);
        Ring->Placement.Translation.X = 0.0f;
        Ring->Placement.Translation.Y = 0.0f;
        Ring->Placement.Translation.Z = 0.0f;
        Ring->Reputation = 0xffffffffu;
    }

    // FUN_06f62750 (cdecl): *Out = the rings of the first lane of System that has a ring with
    // RingId; Out is untouched when no lane has it. Each lane's ring vector is copied into a
    // local (06f627c3-06f627dc, EH state 0 = funclet 06fafc80 on [EBP-0x1c]) and the copy is
    // searched; a miss frees it (06f6280b), a hit assigns it to *Out (FUN_06f68ac0) and frees it.
    void GetLaneRingsOf(FSystemInfo* System, unsigned int RingId, std::vector<FTradeLaneRing>* Out)
    {
        std::vector<FTradeLane>* Lanes = System->GetTradeLanes();
        for (std::vector<FTradeLane>::iterator Lane = Lanes->begin(); Lane != Lanes->end(); ++Lane)
        {
            std::vector<FTradeLaneRing> Rings(Lane->Rings);
            for (std::vector<FTradeLaneRing>::iterator Ring = Rings.begin(); Ring != Rings.end();
                 ++Ring)
            {
                if (Ring->ObjectId == RingId)
                {
                    *Out = Rings;                                         // FUN_06f68ac0
                    return;
                }
            }
        }
    }

    // FUN_06f62880 (cdecl): *Out = the ring *RingId of its lane (untouched when not found).
    void FindRing(FSystemInfo* System, unsigned int const* RingId, FTradeLaneRing* Out)
    {
        std::vector<FTradeLaneRing> Rings;
        GetLaneRingsOf(System, *RingId, &Rings);
        for (std::vector<FTradeLaneRing>::iterator Ring = Rings.begin(); Ring != Rings.end(); ++Ring)
        {
            if (Ring->ObjectId == *RingId)
            {
                *Out = *Ring;
                return;
            }
        }
    }

    // FUN_06f62290 (cdecl): the lane end ring (first or last ring of a lane) nearest to
    // Position. First ring: (x*x + z*z) + y*y kept on the x87 stack; last ring:
    // (z*z + y*y) + x*x stored as a float. A ring wins while no ring was taken (Best -1) or
    // when strictly nearer.
    void FindNearestLaneEnd(FTradeLaneRing* Out, FSystemInfo* System, Vector const* Position)
    {
        FTradeLaneRing BestRing;
        InitEmptyRing(&BestRing);
        float Best = -1.0f;

        std::vector<FTradeLane>* Lanes = System->GetTradeLanes();
        for (std::vector<FTradeLane>::iterator Lane = Lanes->begin(); Lane != Lanes->end(); ++Lane)
        {
            FTradeLaneRing const& First = Lane->Rings.front();
            FTradeLaneRing const& Last = Lane->Rings.back();

            double const Dx = static_cast<double>(First.Position.x) - Position->x;
            double const Dy = static_cast<double>(First.Position.y) - Position->y;
            double const Dz = static_cast<double>(First.Position.z) - Position->z;
            double const FirstDistance = (Dx * Dx + Dz * Dz) + Dy * Dy;

            // 06f62352-06f62380: the differences stay on the x87 stack (no float store of the
            // components); only the sum is stored as a float.
            double const Lx = static_cast<double>(Last.Position.x) - Position->x;
            double const Ly = static_cast<double>(Last.Position.y) - Position->y;
            double const Lz = static_cast<double>(Last.Position.z) - Position->z;
            float const LastDistance = static_cast<float>((Lz * Lz + Ly * Ly) + Lx * Lx);

            // 06f6238a-06f623b7: TEST AH,0x44 / JNP -> Best == -1; else TEST AH,5 / JP ->
            // taken unless FirstDistance < Best.
            if (Best == -1.0f || FirstDistance < Best)
            {
                Best = static_cast<float>(FirstDistance);
                BestRing = First;
            }
            // 06f623bb-06f623dd: TEST AH,5 / JP -> taken unless LastDistance < Best.
            if (LastDistance < Best)
            {
                BestRing = Last;
                Best = LastDistance;
            }
        }
        *Out = BestRing;
    }

    // FUN_06f62120 (cdecl): over every lane, the entry ring FindEntryRing(Position, lane, no
    // target) gives; the one nearest to Position ((z*z + x*x) + y*y) wins, first taken while
    // Best is -1, then only when strictly nearer.
    void FindNearestRing(FTradeLaneRing* Out, FSystemInfo* System, Vector const* Position)
    {
        FTradeLaneRing BestRing;
        InitEmptyRing(&BestRing);
        float Best = -1.0f;

        std::vector<FTradeLane>* Lanes = System->GetTradeLanes();
        for (std::vector<FTradeLane>::iterator Lane = Lanes->begin(); Lane != Lanes->end(); ++Lane)
        {
            FTradeLaneRing Ring;
            FindEntryRing(&Ring, Position, &Lane->Rings, nullptr);
            double const Dx = static_cast<double>(Ring.Position.x) - Position->x;
            double const Dy = static_cast<double>(Ring.Position.y) - Position->y;
            double const Dz = static_cast<double>(Ring.Position.z) - Position->z;
            double const Distance = (Dz * Dz + Dx * Dx) + Dy * Dy;
            // 06f62203-06f6221d: Best == -1 (TEST AH,0x44 / JNP), else Distance < Best.
            if (Best == -1.0f || Distance < Best)
            {
                Best = static_cast<float>(Distance);
                BestRing = Ring;
            }
        }
        *Out = BestRing;
    }

    // The planner local of FUN_06f5ea40 (ESP+0x74; built inline: FUN_06f2cb90 on +0,
    // FUN_06eb79a0 on +0xc; torn down by FUN_06f5ff50).
    struct FNnPathPlanner
    {
        // +0x00: candidate routes, each a chain of lane legs (node 0x14, FUN_06f2cb90).
        std::list<std::list<FNnPathLeg>> Routes;
        // +0x0c: the system's lanes as kind 0 legs, dropped once a route ends on them.
        std::list<FNnPathLeg> Lanes;
        float DirectCost = 0.0f;         // +0x18 |From - To| / 300 (not read afterwards)
        Vector Start;                    // +0x1c From
        Vector End;                      // +0x28 To

        bool FindExitRing(FSystemInfo* System, FTradeLaneRing const* StartRing,
                          Vector const* Target, FTradeLaneRing* Out);
        void PickBestRoute(FSystemInfo* System, FTradeLaneRing* Current);
        void RemoveLaneEndingAt(FTradeLaneRing const* Ring);
        void ExtendRoutes(FTradeLaneRing const* Current);
        void CopyRouteEndingAt(FTradeLaneRing const* Ring, std::list<FNnPathLeg>* Out,
                               bool bErase);
        double GetRouteCost(FSystemInfo* System, std::list<FNnPathLeg> const* Legs);
    };

    // FUN_06f5f760 (thiscall, RET 0x10; `this` unused): the end of StartRing's lane to leave
    // it by. The other end when StartRing is one of them, else the end nearer to Target (the
    // last ring on a tie). No lane: Out gets id 0, position 0 and reputation -1; false.
    bool FNnPathPlanner::FindExitRing(FSystemInfo* System, FTradeLaneRing const* StartRing,
                                      Vector const* Target, FTradeLaneRing* Out)
    {
        std::vector<FTradeLaneRing> Rings;
        GetLaneRingsOf(System, StartRing->ObjectId, &Rings);
        if (Rings.empty())
        {
            Out->Position.x = 0.0f;
            Out->Position.y = 0.0f;
            Out->Position.z = 0.0f;
            Out->Reputation = 0xffffffffu;
            Out->ObjectId = 0;
            return false;
        }

        FTradeLaneRing const First = Rings.front();
        FTradeLaneRing const* Chosen = &Rings.back();
        if (First.ObjectId != StartRing->ObjectId)
        {
            if (Chosen->ObjectId == StartRing->ObjectId)
            {
                Chosen = &First;
            }
            else
            {
                // 06f5f888-06f5f8ed: (z*z + y*y) + x*x of each end, both on the x87 stack;
                // TEST AH,0x41 / JNZ keeps the last ring unless LastDistance > FirstDistance
                // (unordered keeps it too).
                double const Fx = static_cast<double>(First.Position.x) - Target->x;
                double const Fy = static_cast<double>(First.Position.y) - Target->y;
                double const Fz = static_cast<double>(First.Position.z) - Target->z;
                double const Lx = static_cast<double>(Chosen->Position.x) - Target->x;
                double const Ly = static_cast<double>(Chosen->Position.y) - Target->y;
                float const LzStored = static_cast<float>(
                    static_cast<double>(Chosen->Position.z) - Target->z);
                double const FirstDistance = (Fz * Fz + Fy * Fy) + Fx * Fx;
                double const LastDistance =
                    (static_cast<double>(LzStored) * LzStored + Ly * Ly) + Lx * Lx;
                if (LastDistance > FirstDistance)
                    Chosen = &First;
            }
        }
        *Out = *Chosen;
        return true;
    }

    // FUN_06f5f5c0 (thiscall, RET 8): the route of least summed cost wins (first taken while
    // Best is negative); *Current = the ring its last leg ends on (FUN_06f62880). Per leg:
    // (|B - End|^2 + |B - Start|^2) + (|A - End|^2 + |A - Start|^2), summed as a float.
    void FNnPathPlanner::PickBestRoute(FSystemInfo* System, FTradeLaneRing* Current)
    {
        float Best = -1.0f;
        for (std::list<std::list<FNnPathLeg>>::iterator Route = Routes.begin();
             Route != Routes.end(); ++Route)
        {
            float Sum = 0.0f;
            for (std::list<FNnPathLeg>::iterator Leg = Route->begin(); Leg != Route->end(); ++Leg)
            {
                Vector const AStart = SubtractVectors(Leg->PointA, Start); // 06f5f60e
                Vector const AEnd = Subtract(Leg->PointA, End);            // FUN_06eb7810
                Vector const BStart = Subtract(Leg->PointB, Start);        // FUN_06eb7810
                double const Bx = static_cast<double>(Leg->PointB.x) - End.x;
                double const By = static_cast<double>(Leg->PointB.y) - End.y;
                double const Bz = static_cast<double>(Leg->PointB.z) - End.z;
                double const BEnd = (Bz * Bz + By * By) + Bx * Bx;
                double const P = BEnd + LengthSquaredZyx(BStart);
                double const Q = LengthSquaredZyx(AEnd);
                double const R = LengthSquaredZyx(AStart);
                Sum = static_cast<float>(P + (Q + R) + Sum);
            }
            // 06f5f6ff-06f5f71d: Sum < Best (JNP), else Best < 0 (TEST AH,5 / JP skips).
            if (Sum < Best || Best < 0.0f)
            {
                Best = Sum;
                FindRing(System, &Route->back().ObjectB, Current);         // FUN_06f62880
            }
        }
    }

    // FUN_06f5f570 (thiscall, RET 4): erase the first lane leg whose ObjectB is *Ring's id.
    void FNnPathPlanner::RemoveLaneEndingAt(FTradeLaneRing const* Ring)
    {
        for (std::list<FNnPathLeg>::iterator Lane = Lanes.begin(); Lane != Lanes.end(); ++Lane)
        {
            if (Lane->ObjectB == Ring->ObjectId)
            {
                Lanes.erase(Lane);
                return;
            }
        }
    }

    // FUN_06f5fb90 (thiscall, RET 4): the route ending on *Current (last leg's ObjectB) is
    // taken out of Routes (empty base when none); each lane with an end within 11000 of
    // Current's position gives a new route = base + that lane, oriented away from Current
    // (a lane whose far end is near is reversed: kind 0, ends and rings swapped).
    void FNnPathPlanner::ExtendRoutes(FTradeLaneRing const* Current)
    {
        std::list<FNnPathLeg> Base;                                        // FUN_06f93280
        for (std::list<std::list<FNnPathLeg>>::iterator Route = Routes.begin();
             Route != Routes.end(); ++Route)
        {
            if (Route->back().ObjectB == Current->ObjectId)
            {
                Base = *Route;                                             // FUN_06f0ddb0
                Routes.erase(Route);                                       // FUN_06f68f70
                break;
            }
        }

        for (std::list<FNnPathLeg>::iterator Lane = Lanes.begin(); Lane != Lanes.end(); ++Lane)
        {
            // 06f5fc44-06f5fc81: TEST AH,0x41 / JP skips when greater (or unordered).
            Vector const ToA = SubtractVectors(Lane->PointA, Current->Position);   // 06f5fc52
            double const DistanceA = (static_cast<double>(ToA.z) * ToA.z +
                                      static_cast<double>(ToA.y) * ToA.y) +
                                     static_cast<double>(ToA.x) * ToA.x;
            if (DistanceA <= 1.21e8)
            {
                std::list<FNnPathLeg> Route(Base);                         // FUN_06f0dd10
                Route.push_back(*Lane);                                    // FUN_06f69780
                Routes.push_back(Route);                                   // FUN_06f697d0
                continue;
            }
            Vector const ToB = SubtractVectors(Lane->PointB, Current->Position);   // 06f5fce9
            double const DistanceB = (static_cast<double>(ToB.z) * ToB.z +
                                      static_cast<double>(ToB.y) * ToB.y) +
                                     static_cast<double>(ToB.x) * ToB.x;
            if (DistanceB <= 1.21e8)
            {
                std::list<FNnPathLeg> Route(Base);
                FNnPathLeg Reversed;
                Reversed.Kind = 0;
                Reversed.PointA = Lane->PointB;
                Reversed.PointB = Lane->PointA;
                Reversed.ObjectA = Lane->ObjectB;
                Reversed.ObjectB = Lane->ObjectA;
                Route.push_back(Reversed);
                Routes.push_back(Route);
            }
        }
    }

    // FUN_06f5fe50 (thiscall, RET 0xc): *Out = the first route ending on *Ring (unless Out is
    // that route); with bErase the route is dropped from Routes.
    void FNnPathPlanner::CopyRouteEndingAt(FTradeLaneRing const* Ring, std::list<FNnPathLeg>* Out,
                                           bool bErase)
    {
        for (std::list<std::list<FNnPathLeg>>::iterator Route = Routes.begin();
             Route != Routes.end(); ++Route)
        {
            if (Route->back().ObjectB != Ring->ObjectId)
                continue;
            if (Out != &*Route)
                *Out = *Route;
            if (bErase)
                Routes.erase(Route);
            return;
        }
    }

    // FUN_06f5f930 (thiscall, RET 8): travel time of Legs. The first leg adds Start to its
    // entry ring (FindEntryRing on its lane towards End) at 300 per second; then each leg its
    // length, free flight (kind 1) at 300, lanes at 2500 (FastLengthOver).
    double FNnPathPlanner::GetRouteCost(FSystemInfo* System, std::list<FNnPathLeg> const* Legs)
    {
        float Total = 0.0f;
        for (std::list<FNnPathLeg>::const_iterator Leg = Legs->begin(); Leg != Legs->end(); ++Leg)
        {
            if (Leg == Legs->begin())
            {
                std::vector<FTradeLaneRing> Rings;                         // FUN_06eac620
                GetLaneRingsOf(System, Leg->ObjectA, &Rings);
                if (!Rings.empty())
                {
                    FTradeLaneRing Entry;
                    FindEntryRing(&Entry, &Start, &Rings, &End);
                    Vector const ToEntry = SubtractVectors(Start, Entry.Position);  // 06f5f9fb
                    double const LengthSquared = LengthSquaredZyx(ToEntry);
                    Total = static_cast<float>(FastLengthOver(LengthSquared, 300.0f) + Total);
                }
            }
            if (Leg->Kind == 1)
            {
                Vector const Along = Subtract(Leg->PointA, Leg->PointB);   // FUN_06eb7810
                Total = static_cast<float>(FastLengthOver(LengthSquaredZyx(Along), 300.0f) + Total);
            }
            else
            {
                double const Dx = static_cast<double>(Leg->PointA.x) - Leg->PointB.x;
                double const Dy = static_cast<double>(Leg->PointA.y) - Leg->PointB.y;
                double const Dz = static_cast<double>(Leg->PointA.z) - Leg->PointB.z;
                double const LengthSquared = (Dz * Dz + Dy * Dy) + Dx * Dx;
                Total = static_cast<float>(FastLengthOver(LengthSquared, 2500.0f) + Total);
            }
        }
        return Total;
    }

    // FUN_06f5ea20 (thiscall): Kind 1, ObjectA / ObjectB 0 (the points are the caller's).
    FNnPathLeg MakeFreeLeg(Vector const& PointA, Vector const& PointB)
    {
        FNnPathLeg Leg;
        Leg.Kind = 1;
        Leg.PointA = PointA;
        Leg.PointB = PointB;
        Leg.ObjectA = 0;
        Leg.ObjectB = 0;
        return Leg;
    }
}

// FUN_06f5f510 (thiscall on the vector) and the inline copies: see SpacePaths.h.
double __cdecl FastLength(double LengthSquared)
{
    float const Guess = SqrtGuess(LengthSquared);
    double const Step = 1.5 - (static_cast<double>(Guess) * Guess * LengthSquared) * 0.5;
    return (Step * Guess) * LengthSquared;
}

// FUN_06f60d60 (06f60d60-06f60d9d).
void __cdecl RegisterGuidedShip(FContentNpcShip* Ship)
{
    GSpaceSupportGuidedShips.push_back(Ship);
}

// FUN_06f60da0 (06f60da0-06f60dc3).
void __cdecl UnregisterGuidedShip(FContentNpcShip* Ship)
{
    for (std::list<FContentNpcShip*>::iterator It = GSpaceSupportGuidedShips.begin();
         It != GSpaceSupportGuidedShips.end(); ++It)
    {
        if (*It == Ship)
        {
            *It = nullptr;
            return;
        }
    }
}

// FUN_06f60e10 (06f60e10-06f60fb1).
bool __cdecl IsSegmentClear(Vector const* From, Vector const* To, FSystemInfo* System)
{
    if (SpacePopGetSolarDetection() == 0)                                  // FUN_06f5a7e0
        return true;

    // 06f60e27-06f60e67: the difference; the length from (x*x + z*z) + y*y.
    double const Dx = static_cast<double>(To->x) - From->x;                // kept on the stack
    float const Dy = To->y - From->y;
    float const Dz = To->z - From->z;
    double const Length = std::sqrt((Dx * Dx + static_cast<double>(Dz) * Dz) +
                                    static_cast<double>(Dy) * Dy);
    // 06f60e6b-06f60e94: (600 + 400) against Length * 0.5 (TEST AH,5 / JP): below it the
    // radius is 2000, else Length.
    float ScanRadius = static_cast<float>(Length);
    if (Length * 0.5 < 1000.0)
        ScanRadius = 2000.0f;

    struct FScanCenter
    {
        Vector Position;
        unsigned int SystemId;
    } Center;
    Center.Position.x = static_cast<float>(Dx * 0.5 + From->x);
    Center.Position.y = static_cast<float>(Dy * 0.5f + From->y);
    Center.Position.z = static_cast<float>(Dz * 0.5f + From->z);
    Center.SystemId = System->SystemId;

    unsigned int Objects[0x20];
    unsigned int Count = 0;
    pub::System::ScanObjects(Center.SystemId, Objects, 0x20, Center.Position, ScanRadius, 0xfdff,
                             Count);
    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        float Radius = 0.0f;
        Vector ObjectCenter;
        pub::SpaceObj::GetRadius(Objects[Index], Radius, ObjectCenter);
        unsigned int Type = 0;
        pub::SpaceObj::GetType(Objects[Index], Type);
        double Reach = Radius;
        if ((Type & 3) != 0)
            Reach = (200.0 + 400.0) + Radius;
        float const ReachSquared = static_cast<float>(Reach * Reach);
        Vector Closest;
        // 06f60f79-06f60f8a: TEST AH,5 / JNP -> blocked when the distance is below the reach.
        if (DistanceSquaredToSegment(&ObjectCenter, From, To, &Closest) < ReachSquared)
            return false;
    }
    return true;
}

// FUN_06f60fc0 (06f60fc0-06f61190).
bool __cdecl FindObstacleCenter(unsigned int SystemId, Vector Center, float Range,
                                Vector* OutCenter)
{
    unsigned int Objects[0x20];
    unsigned int Count = 0;
    float const ScanRadius = static_cast<float>(((400.0 + 600.0) + Range) * 0.5);
    pub::System::ScanObjects(SystemId, Objects, 0x20, Center, ScanRadius, 0xfdff, Count);

    Vector Obstacles[0x20];
    int ObstacleCount = 0;
    float const HalfRange = static_cast<float>(Range * 0.5);
    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        float Radius = 0.0f;
        Vector ObjectCenter;
        pub::SpaceObj::GetRadius(Objects[Index], Radius, ObjectCenter);
        unsigned int Type = 0;
        pub::SpaceObj::GetType(Objects[Index], Type);
        double Reach = Radius;
        if ((Type & 3) != 0)
            Reach = 400.0 + Radius;
        float const Limit = static_cast<float>(HalfRange + Reach);

        double const Dx = static_cast<double>(Center.x) - ObjectCenter.x;
        double const Dy = static_cast<double>(Center.y) - ObjectCenter.y;
        double const Dz = static_cast<double>(Center.z) - ObjectCenter.z;
        double const DistanceSquared = (Dz * Dz + Dy * Dy) + Dx * Dx;
        // 06f610b8-06f610cd: TEST AH,0x41 / JNZ skips unless Limit^2 > distance^2.
        if (static_cast<double>(Limit) * Limit > DistanceSquared)
        {
            Obstacles[ObstacleCount] = ObjectCenter;
            ++ObstacleCount;
        }
    }

    OutCenter->x = 0.0f;
    OutCenter->y = 0.0f;
    OutCenter->z = 0.0f;
    if (ObstacleCount > 0)
    {
        float const Inverse = static_cast<float>(1.0 / ObstacleCount);
        double SumX = 0.0;
        double SumY = 0.0;
        double SumZ = 0.0;
        for (int Index = 0; Index < ObstacleCount; ++Index)
        {
            float const PartY = Inverse * Obstacles[Index].y;
            float const PartZ = Inverse * Obstacles[Index].z;
            SumX += static_cast<double>(Inverse) * Obstacles[Index].x;
            SumY += PartY;
            SumZ += PartZ;
        }
        OutCenter->x = static_cast<float>(SumX);
        OutCenter->z = static_cast<float>(SumZ);
        OutCenter->y = static_cast<float>(SumY);
    }
    return ObstacleCount > 0;
}

// FUN_06f61430 (06f61430-06f61556). The table is built on the first call (the atexit entry
// of the guard DAT_06fd1b80, FUN_06f61560, is an empty function).
void __cdecl PickPointsAround(Vector const* Center, float const* MinRange, float MaxRange,
                              Vector* OutPoints)
{
    if (!bRingDirectionsBuilt)
    {
        for (int Index = 0; Index < 16; ++Index)
        {
            double const Angle = static_cast<double>(Index) * 0.3926990926265717;
            GRingDirections[Index].x = static_cast<float>(std::cos(Angle));
            GRingDirections[Index].y = 0.0f;
            GRingDirections[Index].z = static_cast<float>(std::sin(Angle));
        }
        bRingDirectionsBuilt = true;
    }

    for (int Index = 0; Index < 16; ++Index)
    {
        float const Low = *MinRange;
        double const Range = static_cast<double>(rand()) * 3.0517578125e-05 *
                             (static_cast<double>(MaxRange) - Low) + Low;
        float const OffsetY = static_cast<float>(Range * GRingDirections[Index].y);
        float const OffsetZ = static_cast<float>(Range * GRingDirections[Index].z);
        OutPoints[Index].x = static_cast<float>(Range * GRingDirections[Index].x + Center->x);
        OutPoints[Index].y = OffsetY + Center->y;
        OutPoints[Index].z = OffsetZ + Center->z;
    }
}

// FUN_06f62440 (06f62440-06f62740).
void __cdecl FindEntryRing(FTradeLaneRing* Out, Vector const* Position,
                           std::vector<FTradeLaneRing> const* Rings, Vector const* Target)
{
    FTradeLaneRing Nearest;
    InitEmptyRing(&Nearest);
    float NearestDistance = -1.0f;
    FTradeLaneRing Crossing;
    InitEmptyRing(&Crossing);
    float CrossingDistance = -1.0f;

    for (std::vector<FTradeLaneRing>::const_iterator Ring = Rings->begin(); Ring != Rings->end();
         ++Ring)
    {
        Vector const Diff = Subtract(Ring->Position, *Position);          // FUN_06eb7810
        double const WideDistance = LengthSquaredZyx(Diff);
        float const Distance = static_cast<float>(WideDistance);         // FST local_f8
        // 06f6254d-06f6256b: the x87 value < Nearest (JNP), else Nearest == -1 (TEST AH,0x44).
        if (WideDistance < NearestDistance || NearestDistance == -1.0f)
        {
            Nearest = *Ring;
            NearestDistance = Distance;
        }
        if (Target == nullptr)
            continue;

        // 06f62591-06f625d1: the ring's -z axis through its rotation (slot 11,
        // TransformMatrix), normalised with 1 / sqrt((x*x + y*y) + z*z).
        FVector const Back = {0.0f, 0.0f, -1.0f};
        FVector Axis;
        GetSpaceMathEngine()->TransformMatrix(Axis, Ring->Placement.Rotation, Back);
        double const Inverse = 1.0 / std::sqrt((static_cast<double>(Axis.X) * Axis.X +
                                                static_cast<double>(Axis.Y) * Axis.Y) +
                                               static_cast<double>(Axis.Z) * Axis.Z);
        double const Nx = Axis.X * Inverse;
        double const Ny = Inverse * Axis.Y;
        double const Nz = Inverse * Axis.Z;
        // 06f62622-06f62676: the plane's offset, then the signed distances of Target and
        // Position; the ring counts when their product is below 0 (TEST AH,5 / JP skips).
        double const Offset = -((Ring->Position.z * Nz + Ring->Position.x * Nx) +
                                Ring->Position.y * Ny);
        double const TargetSide = ((Nz * Target->z + Ny * Target->y) + Nx * Target->x) + Offset;
        double const PositionSide =
            ((Nz * Position->z + Ny * Position->y) + Nx * Position->x) + Offset;
        if (!(TargetSide * PositionSide < 0.0))
            continue;
        // 06f62678-06f62696: Distance < Crossing (JNP), else Crossing == -1.
        if (Distance < CrossingDistance || CrossingDistance == -1.0f)
        {
            Crossing = *Ring;
            CrossingDistance = Distance;
        }
    }

    // 06f626d0-06f626f8: the crossing ring replaces the nearest one when a target was given
    // and its id is set.
    if (Target != nullptr && (Crossing.ObjectId & 0x3fffffffu) != 0)
        Nearest = Crossing;
    *Out = Nearest;
}

// FUN_06f6cb60 (06f6cb60-06f6cb8e).
bool __cdecl IsBaseOfAffiliation(int const* Reputation, unsigned int const* BaseId)
{
    unsigned int Affiliation = 0;
    pub::Reputation::GetAffiliation(*Reputation, Affiliation);
    FContentBase* Base = FindContentBaseInfo(BaseId);                      // FUN_06eac0c0
    return Base->FindFaction(Affiliation) != nullptr;                      // FUN_06eab770
}

// FUN_06f5ea40 (06f5ea40-06f5f4e2).
bool __cdecl BuildNnPath(FSystemInfo* System, Vector const* From, Vector const* To,
                         std::list<FNnPathLeg>* Legs, int Flags, float const* Range,
                         int Unused7)
{
    (void)Flags;
    (void)Unused7;
    bool bResult = true;                                                   // local_21e

    // 06f5ea6d-06f5ea7c: the lane end nearest to the destination (FUN_06f62290).
    FTradeLaneRing EndRing;
    FindNearestLaneEnd(&EndRing, System, To);

    FNnPathPlanner Planner;
    Planner.Start = *From;
    Planner.End = *To;
    Planner.DirectCost = static_cast<float>(
        FastLength(LengthSquaredZyx(SubtractVectors(*From, *To))) / 300.0f);  // 06f5eb00

    // 06f5eb2e-06f5ebf6: every lane as a kind 0 leg between its end rings (FUN_06f5ea20 then
    // Kind 0; FUN_06f2fb70 is the ring count).
    std::vector<FTradeLane>* Lanes = System->GetTradeLanes();
    for (std::vector<FTradeLane>::iterator Lane = Lanes->begin(); Lane != Lanes->end(); ++Lane)
    {
        FNnPathLeg Leg = MakeFreeLeg(Lane->StartPosition, Lane->EndPosition);
        Leg.Kind = 0;
        Leg.ObjectA = Lane->Rings.empty() ? 0 : Lane->Rings.front().ObjectId;
        Leg.ObjectB = Lane->Rings.empty() ? 0 : Lane->Rings.back().ObjectId;
        Planner.Lanes.push_back(Leg);                                      // FUN_06f68e70
    }

    if (Planner.Routes.empty())
    {
        // 06f5ec0a-06f5ec5d: the ring nearest to the start and the end of its lane to leave by.
        FTradeLaneRing StartRing;
        FindNearestRing(&StartRing, System, &Planner.Start);              // FUN_06f62120
        FTradeLaneRing ExitRing;
        InitEmptyRing(&ExitRing);                                          // FUN_06f67050
        if (!Planner.FindExitRing(System, &StartRing, &Planner.End, &ExitRing))
        {
            // 06f5f071-06f5f12c: no lane: straight to the destination unless it is in range.
            Legs->clear();
            // TEST AH,5 / JP: no path unless Range^2 < distance^2.
            Vector const Diff = SubtractVectors(*From, *To);             // 06f5f0a1
            double const DistanceSquared = LengthSquaredZyx(Diff);
            double const RangeValue = *Range;
            if (!(RangeValue * RangeValue < DistanceSquared))
                return false;
            Legs->push_back(MakeFreeLeg(*From, *To));                      // FUN_06f69780
            return bResult;
        }

        // 06f5ec63-06f5edfe: the start lane (the one the exit ring belongs to) becomes the
        // first route, oriented towards the exit; EntryPoint is where that lane is joined.
        // Retail leaves EntryPoint unset when no lane matches (never: the exit ring is a lane
        // end); the port starts it at 0.
        Vector EntryPoint;
        EntryPoint.x = 0.0f;
        EntryPoint.y = 0.0f;
        EntryPoint.z = 0.0f;
        for (std::list<FNnPathLeg>::iterator Lane = Planner.Lanes.begin();
             Lane != Planner.Lanes.end(); ++Lane)
        {
            if (ExitRing.ObjectId == Lane->ObjectA)
            {
                EntryPoint = Lane->PointB;
                std::list<FNnPathLeg> Route;
                FNnPathLeg Reversed = MakeFreeLeg(Lane->PointB, Lane->PointA);
                Reversed.Kind = 0;
                Reversed.ObjectA = Lane->ObjectB;
                Reversed.ObjectB = Lane->ObjectA;
                Route.push_back(Reversed);
                Planner.Routes.push_back(Route);                           // FUN_06f68f20
                Planner.Lanes.erase(Lane);
                break;
            }
            if (ExitRing.ObjectId == Lane->ObjectB)
            {
                EntryPoint = Lane->PointA;
                std::list<FNnPathLeg> Route;
                Route.push_back(*Lane);
                Planner.Routes.push_back(Route);
                Planner.Lanes.erase(Lane);
                break;
            }
        }

        // 06f5ee03-06f5efdf: every lane with an end within 11000 of EntryPoint starts a route
        // too, oriented away from it.
        std::list<FNnPathLeg>::iterator Lane = Planner.Lanes.begin();
        while (Lane != Planner.Lanes.end())
        {
            if (LengthSquaredZyx(SubtractVectors(EntryPoint, Lane->PointA)) < 1.21e8)   // 06f5ee41
            {
                std::list<FNnPathLeg> Route;
                Route.push_back(*Lane);
                Planner.Routes.push_back(Route);
                Lane = Planner.Lanes.erase(Lane);
                continue;
            }
            if (LengthSquaredZyx(SubtractVectors(EntryPoint, Lane->PointB)) < 1.21e8)   // 06f5eee8
            {
                std::list<FNnPathLeg> Route;
                FNnPathLeg Reversed = MakeFreeLeg(Lane->PointB, Lane->PointA);
                Reversed.Kind = 0;
                Reversed.ObjectA = Lane->ObjectB;
                Reversed.ObjectB = Lane->ObjectA;
                Route.push_back(Reversed);
                Planner.Routes.push_back(Route);
                Lane = Planner.Lanes.erase(Lane);
                continue;
            }
            ++Lane;
        }
    }

    // 06f5efe5-06f5f148: the ring the best route ends on; its lane leg leaves the pool. The
    // ring starts as id 0 with the identity transform (FUN_06f143d0); retail leaves its
    // position and reputation unset, the port zeroes them.
    FTradeLaneRing Current;
    InitEmptyRing(&Current);
    Planner.PickBestRoute(System, &Current);
    for (std::list<FNnPathLeg>::iterator Lane = Planner.Lanes.begin();
         Lane != Planner.Lanes.end(); ++Lane)
    {
        if (Lane->ObjectB == Current.ObjectId)
        {
            Planner.Lanes.erase(Lane);
            break;
        }
    }

    // 06f5f14d-06f5f254: extend until the destination's lane end is reached, the routes run
    // out, or the current ring is within 11000 of that end (TEST AH,0x41 / JP: <= counts).
    Vector ToEnd = SubtractVectors(Current.Position, EndRing.Position);   // 06f5f167
    bool bClose = LengthSquaredZyx(ToEnd) <= 1.21e8;
    while (Current.ObjectId != EndRing.ObjectId && !Planner.Routes.empty() && !bClose)
    {
        Planner.ExtendRoutes(&Current);
        Planner.PickBestRoute(System, &Current);
        Planner.RemoveLaneEndingAt(&Current);
        ToEnd = SubtractVectors(Current.Position, EndRing.Position);   // 06f5f210
        bClose = LengthSquaredZyx(ToEnd) <= 1.21e8;
    }

    // 06f5f25a-06f5f2cc: the chosen route, then free flight from its last ring.
    Planner.CopyRouteEndingAt(&Current, Legs, false);
    Legs->push_back(MakeFreeLeg(Current.Position, *To));

    // 06f5f2cc-06f5f315: the lanes must beat flying straight (TEST AH,0x41 / JNZ keeps the
    // route unless Cost > Direct; unordered keeps it too).
    float const Cost = static_cast<float>(Planner.GetRouteCost(System, Legs));
    double const Direct = FastLength(LengthSquaredZyx(SubtractVectors(*From, *To))) / 300.0f;   // 06f5f2ec
    if (Cost > Direct)
    {
        // 06f5f31b-06f5f39c: straight flight, unless the destination is within *Range
        // (TEST AH,5 / JP: no path unless Range^2 < distance^2).
        Legs->clear();                                                     // FUN_06ecfec0
        double const DistanceSquared = LengthSquaredZyx(SubtractVectors(*From, *To));   // 06f5f32a
        double const RangeValue = *Range;
        if (!(RangeValue * RangeValue < DistanceSquared))
            return false;
        Legs->push_back(MakeFreeLeg(*From, *To));
        return bResult;
    }

    // 06f5f3a6-06f5f49e: the first lane is joined at its ring nearest to From that lies
    // between From and the lane's far end; a lane joined at its exit is dropped (and an
    // emptied path becomes the straight flight).
    std::list<FNnPathLeg>::iterator First = Legs->begin();
    if (First != Legs->end())
    {
        std::vector<FTradeLaneRing> Rings;                                 // FUN_06eac620
        GetLaneRingsOf(System, First->ObjectA, &Rings);
        FTradeLaneRing Entry;
        FindEntryRing(&Entry, From, &Rings, &First->PointB);
        First->ObjectA = Entry.ObjectId;
        First->PointA = Entry.Position;
        if (First->ObjectA == First->ObjectB)
        {
            Legs->pop_front();                                             // FUN_06f68e40
            if (Legs->empty())
                Legs->push_back(MakeFreeLeg(*From, *To));
        }
    }
    return bResult;
}

// FUN_06f60020 (06f60020-06f601eb). The unwind map (FuncInfo 06fc4af4) holds the legs (state
// 0) and one ref-holding local per branch (state 1 the dock directive, state 2 the goto
// directive; FUN_06f28e10 releases it): each directive is made by the factory (FUN_06f24870)
// into its holder (the dock branch first releases the empty holder, 06f60128), gets its
// setter and BaseOp Field_08 = 1 ([dir+0x114]), the list takes a copy (insert at the end,
// FUN_06f1e390) and AddRefs it (slot 0), and the holder is released at the end of the branch.
// Retail leaves the leg count in EAX (06f601a3); no caller reads it.
void __cdecl BuildPathDirectives(FSystemInfo* System, Vector const* From, Vector const* To,
                                 std::list<FObjListDirective*>* Directives, int Flags,
                                 float const* Range)
{
    std::list<FNnPathLeg> Legs;                                            // FUN_06f93280
    BuildNnPath(System, From, To, &Legs, Flags, Range, 0);
    for (std::list<FNnPathLeg>::iterator Leg = Legs.begin(); Leg != Legs.end(); ++Leg)
    {
        if (Leg->Kind == 0)
        {
            TRefHandle<FObjListDirective> Directive;                      // EH state 1
            CreateObjListDirective(0x13, &Directive.Pointer);
            FObjListDock* Dock = static_cast<FObjListDock*>(Directive.Pointer);
            Dock->SetTarget(&Leg->ObjectA, &Leg->ObjectB);                 // FUN_06f276f0
            Dock->Op.Priority = 1;
            Directives->push_back(Directive.Pointer);
            Directive->AddRef();
        }
        else if (Leg->Kind == 1)
        {
            TRefHandle<FObjListDirective> Directive;                      // EH state 2
            CreateObjListDirective(5, &Directive.Pointer);
            FObjListGotoVec* Goto = static_cast<FObjListGotoVec*>(Directive.Pointer);
            Goto->SetDestination(&Leg->PointB, *Range, -1.0f, 0, false, false);   // FUN_06f26460
            Goto->Op.Priority = 1;
            Directives->push_back(Directive.Pointer);
            Directive->AddRef();
        }
    }
}

// FUN_06f68270 (06f68270-06f682b5).
bool __cdecl IsSameShipOrFormation(FContentShipBase* Ship, unsigned int OtherId)
{
    FContentShipBase* Other =
        static_cast<FContentShipBase*>(Ship->GetSystem()->FindShip(OtherId));    // FUN_06f73930
    if (Other == nullptr)
        return false;
    if (Other->FormationId != 0xffffffffu && Other->FormationId == Ship->FormationId)
        return true;
    return Ship->SpaceObjId == Other->SpaceObjId;                          // NEG/SBB/INC
}

namespace
{
    // 06f67dcc-06f67e62 / 06f67e8f-06f67f59: the ships of Object's formation group
    // (FUN_06f66220 after room for 5 is reserved, FUN_06eed270), or Object alone when it flies
    // in no group (+0x154 == -1, appended by FUN_06eb5770 / FUN_06f937c0).
    void CollectAttitudeMembers(std::vector<FContentShipBase*>* Out, FContentShipBase* Object)
    {
        if (Object->FormationId == 0xffffffffu)
        {
            Out->push_back(Object);
            return;
        }
        Out->reserve(5);
        CollectFormationMembers(Out, Object->FormationId);
    }
}

// FUN_06f67d80 (06f67d80-06f68267). Slot 1 of a member (FContentSpaceObj::Slot04) tells an NPC
// ship (true) from a player ship; an NPC's reputation is its handle's (FUN_06f33f80), a player
// ship's pub::SpaceObj::GetRep (not checked inside the loops). SetAttitude(A, B) sets A's
// attitude towards B. Neither FindShip result is null-checked (retail).
void __cdecl SetFormationAttitudes(FSystemInfo* System, unsigned int ShipId, unsigned int OtherId,
                                   bool bFormations, bool bMutual, float Attitude)
{
    if (!bFormations)
    {
        // 06f681d7-06f68257: the two objects only, never from a player's ship.
        int ShipRep;
        int OtherRep;
        if (pub::SpaceObj::GetRep(ShipId, ShipRep) != 0)
            return;
        if (pub::SpaceObj::GetRep(OtherId, OtherRep) != 0)
            return;
        if (FindContentPlayerStateByShip(&ShipId) == nullptr)            // FUN_06f49bc0
            pub::Reputation::SetAttitude(ShipRep, OtherRep, Attitude);
        if (bMutual && FindContentPlayerStateByShip(&OtherId) == nullptr)
            pub::Reputation::SetAttitude(OtherRep, ShipRep, Attitude);
        return;
    }

    std::vector<FContentShipBase*> ShipMembers;
    CollectAttitudeMembers(&ShipMembers,
                           static_cast<FContentShipBase*>(System->FindShip(ShipId)));

    // 06f67e65-06f67e81: OtherId is a ship when its type has a bit of 0x7f0000. Retail
    // ignores GetType's result (the type stays unset on a failure); the port starts it at 0.
    unsigned int OtherType = 0;
    pub::SpaceObj::GetType(OtherId, OtherType);
    if ((OtherType & 0x7f0000u) == 0)
    {
        // 06f680f2-06f681c4: every member of the ship's group and the object's reputation.
        int OtherRep;
        if (pub::SpaceObj::GetRep(OtherId, OtherRep) != 0)
            return;
        for (std::vector<FContentShipBase*>::iterator Member = ShipMembers.begin();
             Member != ShipMembers.end(); ++Member)
        {
            if ((*Member)->Slot04())
            {
                int MemberRep = (*Member)->ReputationHandle->GetReputation();
                pub::Reputation::SetAttitude(MemberRep, OtherRep, Attitude);
                if (bMutual)
                {
                    int MemberRepAgain = (*Member)->ReputationHandle->GetReputation();
                    pub::Reputation::SetAttitude(OtherRep, MemberRepAgain, Attitude);
                }
            }
            else if (bMutual)
            {
                int MemberRep;
                pub::SpaceObj::GetRep((*Member)->SpaceObjId, MemberRep);
                pub::Reputation::SetAttitude(OtherRep, MemberRep, Attitude);
            }
        }
        return;
    }

    std::vector<FContentShipBase*> OtherMembers;
    CollectAttitudeMembers(&OtherMembers,
                           static_cast<FContentShipBase*>(System->FindShip(OtherId)));

    // 06f67f5c-06f680bf.
    for (std::vector<FContentShipBase*>::iterator Member = ShipMembers.begin();
         Member != ShipMembers.end(); ++Member)
    {
        if ((*Member)->Slot04())
        {
            for (std::vector<FContentShipBase*>::iterator Target = OtherMembers.begin();
                 Target != OtherMembers.end(); ++Target)
            {
                if ((*Target)->Slot04())
                {
                    int TargetRep = (*Target)->ReputationHandle->GetReputation();
                    int MemberRep = (*Member)->ReputationHandle->GetReputation();
                    pub::Reputation::SetAttitude(MemberRep, TargetRep, Attitude);
                    if (bMutual)
                    {
                        int MemberRepAgain = (*Member)->ReputationHandle->GetReputation();
                        int TargetRepAgain = (*Target)->ReputationHandle->GetReputation();
                        pub::Reputation::SetAttitude(TargetRepAgain, MemberRepAgain, Attitude);
                    }
                }
                else
                {
                    // 06f67fff-06f68030: towards a player's ship, one way only.
                    int TargetRep;
                    pub::SpaceObj::GetRep((*Target)->SpaceObjId, TargetRep);
                    int MemberRep = (*Member)->ReputationHandle->GetReputation();
                    pub::Reputation::SetAttitude(MemberRep, TargetRep, Attitude);
                }
            }
        }
        else if (bMutual)
        {
            // 06f68046-06f680b4: a player's ship: only the NPC targets towards it.
            for (std::vector<FContentShipBase*>::iterator Target = OtherMembers.begin();
                 Target != OtherMembers.end(); ++Target)
            {
                if (!(*Target)->Slot04())
                    continue;
                int MemberRep;
                pub::SpaceObj::GetRep((*Member)->SpaceObjId, MemberRep);
                int TargetRep = (*Target)->ReputationHandle->GetReputation();
                pub::Reputation::SetAttitude(TargetRep, MemberRep, Attitude);
            }
        }
    }
}
