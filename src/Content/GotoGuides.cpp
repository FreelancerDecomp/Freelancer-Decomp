// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU37 (06f07de0-06f0aa00): the [MetaBehavior] guides - goto_guide (type 5) and
// dock_guide (type 6). See GuidesInternal.h. Lane CNT-AI owns this file.

#include "GuidesInternal.h"
#include "ContentPlayerState.h"   // FindContentPlayerStateByShip, GetValueE4 / GetValueE8
#include "Gcs.h"                  // PickVoiceLine
#include "MissionObject.h"        // FNnPathLeg, FMissionDirectiveList
#include "MissionObjList.h"       // the directive setters
#include "NpcShip.h"              // FContentNpcShip
#include "SpacePaths.h"           // BuildNnPath, BuildPathDirectives, FindEntryRing, FastLength
#include "SystemInfo.h"
#include "common_server.h"        // pub::SpaceObj, pub::AI::lock_maneuvers, pub::GetNicknameId
#include "ContentInternal.h"      // SubtractVectors (FUN_06eeaee0)

#include <cstdint>
#include <vector>

extern unsigned int GGuideLargeShipsClass;   // Guides.cpp, DAT_06fd0644

namespace
{
    Vector GetShipPosition(FContentNpcShip* Ship)
    {
        Vector Position;
        Ship->GetPosition(&Position);                                   // FUN_06eb70f0
        return Position;
    }

    // (z*z + y*y) + x*x of A - B, the order of most TU37 distance checks.
    double DistanceSquaredZyx(Vector const& A, Vector const& B)
    {
        float const X = A.x - B.x;
        float const Y = A.y - B.y;
        float const Z = A.z - B.z;
        return (static_cast<double>(Z) * Z + static_cast<double>(Y) * Y) +
               static_cast<double>(X) * X;
    }

    // The voice line pattern of the goto guide (06f09243-06f092c2, 06f09460-06f094e2,
    // 06f098fc-06f09983): GetVoiceID of the ship, then, in a system, PickVoiceLine(Name, "-")
    // (DAT_06fb6064) and on success (0) a comm line to the target, duration 0.5.
    void SendGuideComm(FContentNpcShip* Ship, unsigned int TargetId, char const* Name)
    {
        unsigned int VoiceId = 0;
        unsigned int const ShipId = Ship->SpaceObjId;
        pub::SpaceObj::GetVoiceID(ShipId, VoiceId);
        FSystemInfo* System = Ship->GetSystem();
        if (System == nullptr)
            return;
        TString<0x40> Line;
        Line.Length = 0;
        Line.Data[0] = '\0';
        if (PickVoiceLine(Name, "-", VoiceId, &Line) != 0)
            return;
        System->SendComm(Ship->SpaceObjId, TargetId, pub::GetNicknameId(Line.Data), 0, 0,
                         nullptr, 0.5f);                                // FUN_06f72930
    }

    // The +0x154 formation-group test of 06f088c9-06f088f5 / 06f0931f-06f0934b /
    // 06f09529-06f0954f: the target is a ship of the system in the ship's group.
    bool IsTargetInShipGroup(FContentNpcShip* Ship, unsigned int TargetId)
    {
        unsigned int const Group = Ship->FormationId;
        FSystemInfo* System = Ship->GetSystem();
        if (System == nullptr)
            return false;
        FContentSpaceObj* Target = System->FindShip(TargetId);          // FUN_06f73930
        if (Target == nullptr)
            return false;
        return static_cast<FContentShipBase*>(Target)->FormationId == Group;
    }

    // The smart replacement of the guide's +0x170 member (FUN_06f0de80), then the list takes
    // one reference (FUN_06f1e390 + AddRef). The caller's ref-holding local (FuncInfo
    // 06fc1efc states 1..3, 5, 6: FUN_06f28e10) releases the creation reference.
    void KeepAndQueue(FGotoGuide* Guide, FObjListDirective* New, FMissionDirectiveList* List)
    {
        AssignDirective(&Guide->Directive.Pointer, New);
        List->Items.push_back(Guide->Directive);
        Guide->Directive->AddRef();
    }
}

// ---------------------------------------------------------------------------------------
// FLineGuide (06fb827c)
// ---------------------------------------------------------------------------------------

FLineGuide::FLineGuide(FMetaBehavior const* Desc)
    : FContentGuide(Desc)
    , Line(Desc->Line)
{
}

// ---------------------------------------------------------------------------------------
// Goto guide (06fb8404)
// ---------------------------------------------------------------------------------------

// FUN_06f07ef0 (06f07ef0-06f08114). TargetName = the description's (FUN_06ea9f40 clamp 0x30),
// Path = its legs (FUN_06f0ddb0), bRestart / bNodeEndSent 0, State 1. With a ship: TargetId =
// FindObjectIdByName on the ship's system (the first call does not test the system), again on
// a non-null system, whose path is then built (range 1000, flags 0x19) when Path is empty;
// Timer / NagCount 0 and the ship joins GSpaceSupportGuidedShips.
FGotoGuide::FGotoGuide(FMbGotoGuide const* Desc)
    : FLineGuide(Desc)
    , FirstPosition(Desc->FirstPosition)
    , Destination(Desc->Destination)
    , Range(Desc->Range)
    , bCruise(Desc->Field_0x174)
    , bNoCruise(Desc->Field_0x175)
    , Field_0x16e(Desc->Field_0x176)
    , Priority(Desc->Priority)
    , Path(Desc->Legs)
{
    TargetName = Desc->TargetName.Data;
    if (Ship == nullptr)
        return;
    TargetId = Ship->GetSystem()->FindObjectIdByName(&TargetName, Ship->ControllerId);
    FSystemInfo* System = Ship->GetSystem();
    if (System != nullptr)
    {
        TargetId = System->FindObjectIdByName(&TargetName, Ship->ControllerId);
        if (Path.empty())
        {
            float const PathRange = 1000.0f;
            Vector const ShipPosition = GetShipPosition(Ship);
            BuildNnPath(System, &ShipPosition, &Destination, &Path, 0x19, &PathRange, 0);
        }
    }
    Timer = 0.0f;
    NagCount = 0;
    RegisterGuidedShip(Ship);                                           // FUN_06f60d60
}

// FUN_06f08140 body: member-wise, the directive AddRef'ed, the lists copied.
FGotoGuide::FGotoGuide(FGotoGuide const& Other)
    : FLineGuide(Other)
    , FirstPosition(Other.FirstPosition)
    , Destination(Other.Destination)
    , Range(Other.Range)
    , TargetName(Other.TargetName)
    , TargetId(Other.TargetId)
    , bCruise(Other.bCruise)
    , bNoCruise(Other.bNoCruise)
    , Field_0x16e(Other.Field_0x16e)
    , Directive(Other.Directive)
    , State(Other.State)
    , PrevState(Other.PrevState)
    , Priority(Other.Priority)
    , Path(Other.Path)
    , Waypoint(Other.Waypoint)
    , Timer(Other.Timer)
    , NagCount(Other.NagCount)
    , CombatIds(Other.CombatIds)
    , bRestart(Other.bRestart)
    , bNodeEndSent(Other.bNodeEndSent)
{
}

// FUN_06f08390 (06f08390-06f084a0): lock_maneuvers(TargetId, false), the ship leaves
// GSpaceSupportGuidedShips (FUN_06f60da0); then the members as the unwind map FuncInfo
// 06fc1e58 orders them: CombatIds (+0x1a0), Path (+0x180), the +0x170 directive handle
// (FUN_06f28e10), the base.
FGotoGuide::~FGotoGuide()
{
    pub::AI::lock_maneuvers(TargetId, false);
    UnregisterGuidedShip(Ship);
}

// FUN_06f08140 (06f08140-06f08380).
FContentGuide* FGotoGuide::Clone()
{
    return new FGotoGuide(*this);
}

void FGotoGuide::SetState(int NewState)
{
    if (State == NewState)
        return;
    PrevState = State;
    State = NewState;
    bRestart = true;
}

FNnPathLeg FGotoGuide::GetFirstLeg() const
{
    if (Path.empty())
    {
        FNnPathLeg Empty = {};
        return Empty;
    }
    return Path.front();
}

// LAB_06f087e0.
void FGotoGuide::Reset()
{
    State = 1;
    PrevState = 0;
}

// 06ea1490 (RET 8): nothing.
void FGotoGuide::OnDirectivesDone(int ResultWord11c, int ResultWord118)
{
    (void)ResultWord11c;
    (void)ResultWord118;
}

// LAB_06f07de0 (06f07de0-06f07eae).
void FGotoGuide::SetShip(FContentNpcShip* InShip)
{
    bool bWasNull = false;
    if (Ship != nullptr)
        UnregisterGuidedShip(Ship);
    else
        bWasNull = true;
    Ship = InShip;
    if (InShip == nullptr)
        return;
    if (bWasNull)
        RefreshTarget();
    FSystemInfo* System = Ship->GetSystem();
    if (System != nullptr)
    {
        Path.clear();
        float const PathRange = 1000.0f;
        Vector const ShipPosition = GetShipPosition(Ship);
        BuildNnPath(System, &ShipPosition, &Destination, &Path, 0x19, &PathRange, 0);
    }
    RegisterGuidedShip(Ship);
}

// FUN_06f07ec0 (06f07ec0-06f07eeb).
void FGotoGuide::RefreshTarget()
{
    if (Ship == nullptr)
        return;
    TargetId = Ship->GetSystem()->FindObjectIdByName(&TargetName, Ship->ControllerId);
}

// LAB_06f084d0 (06f084d0-06f087ad). The first leg is copied once; the loop runs on PrevState
// while Path is not empty and nothing was found (states other than 2, 3, 5, 6, 9 fall
// through with nothing found; a state that does not change repeats, as retail).
bool FGotoGuide::RequestFleeDestination(pub::AI::ContentCallback::FleeDestinationType& OutType,
                                        Vector& OutPosition, unsigned int& OutParam,
                                        float& OutFloat)
{
    (void)OutParam;
    bool bFound = false;
    if (Path.empty())
        return bFound;
    FNnPathLeg const Leg = Path.front();
    do
    {
        if (bFound)
            break;
        switch (PrevState)
        {
        case 2:
        {
            // FCOMP 1500 / TEST AH,5 / JP: taken (the leg start is the destination) when the
            // length is 1500 or more, or NaN.
            Vector const ShipPosition = GetShipPosition(Ship);
            if (!(FastLength(DistanceSquaredZyx(ShipPosition, Leg.PointA)) < 1500.0f))
            {
                OutType = static_cast<pub::AI::ContentCallback::FleeDestinationType>(3);
                OutPosition = Leg.PointA;
                OutFloat = 1500.0f;                                     // DAT_06fb8108
                bFound = true;
            }
            else
            {
                SetState(6);
            }
            break;
        }
        case 3:
        case 5:
            OutType = static_cast<pub::AI::ContentCallback::FleeDestinationType>(3);
            OutPosition = Leg.PointB;
            OutFloat = 1500.0f;
            bFound = true;
            break;
        case 6:
        {
            Vector const ShipPosition = GetShipPosition(Ship);
            if (!(FastLength(DistanceSquaredZyx(ShipPosition, Leg.PointB)) < 1500.0f))
            {
                OutType = static_cast<pub::AI::ContentCallback::FleeDestinationType>(3);
                OutPosition = Leg.PointB;
                OutFloat = 1500.0f;
                bFound = true;
            }
            else
            {
                SetState(9);
            }
            break;
        }
        case 9:
            Path.pop_front();                                           // FUN_06ecfe80
            if (!Path.empty())
                SetState(2);
            else
                SetState(10);
            break;
        default:
            bFound = false;
            break;
        }
    } while (!Path.empty());
    return bFound;
}

// FUN_06f08800 (06f08800-06f0890b, RET 8).
bool FGotoGuide::IsTargetOnPath(int Kind, unsigned int ObjectId)
{
    if (Kind != 7)
        return IsTargetInShipGroup(Ship, TargetId);

    FNnPathLeg const Leg = GetFirstLeg();
    if (Leg.Kind != 0)
        return false;
    FSystemInfo* System = Ship->GetSystem();
    if (System == nullptr)
        return false;
    std::vector<FTradeLaneRing> Rings;
    if (!System->GetTradeLaneRingsByRing(&ObjectId, &Rings))            // FUN_06f77bd0
        return false;
    for (std::vector<FTradeLaneRing>::iterator Ring = Rings.begin(); Ring != Rings.end(); ++Ring)
    {
        if (Ring->ObjectId == Leg.ObjectA || Ring->ObjectId == Leg.ObjectB)
            return true;
    }
    return false;
}

// FUN_06f08930 (06f08930-06f08a74): for a lane first leg, the entry ring of that lane nearest
// the target (FindEntryRing, no side test); within 3000 of the target (3000 * 3000 compared
// on the x87 stack, TEST AH,0x41 / JNZ) it becomes the Waypoint.
bool FGotoGuide::FindLaneEntryNearTarget()
{
    FNnPathLeg const Leg = GetFirstLeg();
    if (Leg.Kind != 0)
        return false;
    Vector TargetPosition;
    Matrix Orientation;
    pub::SpaceObj::GetLocation(TargetId, TargetPosition, Orientation);
    FSystemInfo* System = Ship->GetSystem();
    if (System == nullptr)
        return false;
    std::vector<FTradeLaneRing> Rings;
    if (!System->GetTradeLaneRingsByRing(&Leg.ObjectA, &Rings))
        return false;
    FTradeLaneRing Entry;
    FindEntryRing(&Entry, &TargetPosition, &Rings, nullptr);            // FUN_06f62440
    float const Dx = TargetPosition.x - Entry.Position.x;
    float const Dy = TargetPosition.y - Entry.Position.y;
    float const Dz = TargetPosition.z - Entry.Position.z;
    double const LengthSquared =
        (static_cast<double>(Dz) * Dz + static_cast<double>(Dy) * Dy) + static_cast<double>(Dx) * Dx;
    if (!(3000.0 * 3000.0 > LengthSquared))
        return false;
    Waypoint = Entry.Position;
    return true;
}

// FUN_06f08a80 (06f08a80-06f08c6c): the first leg point (A of every leg but the first, then
// B) within 1500 of the target becomes the Waypoint. The second test (the ship-target
// distance squared not below 0, TEST AH,1 / JZ) only fails for NaN.
bool FGotoGuide::FindLegPointNearTarget()
{
    if (Path.empty())
        return false;
    Vector TargetPosition;
    Matrix Orientation;
    pub::SpaceObj::GetLocation(TargetId, TargetPosition, Orientation);
    double const Limit = 1500.0 * 1500.0;                               // DAT_06fb810c squared
    for (std::list<FNnPathLeg>::iterator Leg = Path.begin(); Leg != Path.end(); ++Leg)
    {
        // FCOMPP (1500^2 against the distance) / TEST AH,0x41 / JNZ: near when 1500^2 is
        // greater; then FCOMP 0 / TEST AH,1 / JZ: taken when the distance is not below 0 and
        // not NaN.
        if (Leg != Path.begin() && Limit > DistanceSquaredZyx(TargetPosition, Leg->PointA))
        {
            Vector const ShipPosition = GetShipPosition(Ship);
            if (DistanceSquaredZyx(ShipPosition, TargetPosition) >= 0.0)
            {
                Waypoint = Leg->PointA;
                return true;
            }
        }
        if (Limit > DistanceSquaredZyx(TargetPosition, Leg->PointB))
        {
            Vector const ShipPosition = GetShipPosition(Ship);
            if (DistanceSquaredZyx(ShipPosition, TargetPosition) >= 0.0)
            {
                Waypoint = Leg->PointB;
                return true;
            }
        }
    }
    return false;
}

// FUN_06f08c70 (06f08c70-06f099aa, RET 8). Range used by the approach tests (06f08c70-
// 06f08c9f: FLD 1500 / FCOMP Range / TEST AH,5 / JP -> 1500 when 1500 >= Range or NaN): the
// larger of Range and 1500. Every path ends with Start() when a state change asked for a
// restart (FinishEvent).
void FGotoGuide::HandleEvent(pub::Controller::EMessage MsgId, void const* Parms)
{
    using pub::Controller::EMessage;
    float const ApproachRange = 1500.0f < Range ? Range : 1500.0f;
    if (Ship == nullptr)
        return;

    FNnPathLeg Leg = {};
    Leg.Kind = 1;
    if (!Path.empty())
        Leg = Path.front();
    else
        SetState(10);

    double const ApproachSquared = static_cast<double>(ApproachRange) * ApproachRange;
    unsigned int const* Ids = static_cast<unsigned int const*>(Parms);

    switch (MsgId)
    {
    case EMessage::TradelaneExit:
        if (Ids[0] == TargetId)
            pub::AI::lock_maneuvers(TargetId, false);
        // FCOMPP (r^2 against the distance) / TEST AH,5 / JP: skipped when r^2 >= distance
        // or NaN.
        if (Ids[0] == Ship->SpaceObjId && State == 7 &&
            ApproachSquared < DistanceSquaredZyx(GetShipPosition(Ship), Leg.PointB))
            SetState(3);
        break;
    case EMessage::TradelaneEnter:
        if (Ids[0] == TargetId && IsTargetOnPath(7, Ids[1]))
            pub::AI::lock_maneuvers(TargetId, true);
        break;
    case EMessage::TradelaneRequest:
        if (State >= 2 && State <= 5 && Ids[0] == TargetId && IsTargetOnPath(7, Ids[1]))
            SetState(7);
        break;
    case EMessage::FormationRequest:
        if (State == 3 && TargetId == Ids[0] && Ship->SpaceObjId == Ids[1])
        {
            PrevState = 3;
            State = 7;
            bRestart = true;
        }
        break;
    case EMessage::ObjectDestroyed:
        if (State != 4)
            break;
        for (std::list<unsigned int>::iterator It = CombatIds.begin(); It != CombatIds.end(); ++It)
        {
            if (*It == Ids[0])
            {
                CombatIds.erase(It);
                break;
            }
        }
        if (CombatIds.empty())
        {
            Ship->SetField1b1(false);                                   // FUN_06f33fd0
            if (PrevState != State)
            {
                int const Old = State;
                State = PrevState;
                PrevState = Old;
                bRestart = true;
            }
        }
        break;
    case EMessage::CombatState:
    {
        int const Type = static_cast<int>(Ids[0]);
        unsigned int const A = Ids[1];
        unsigned int const B = Ids[2];
        if (State == 4)
        {
            if (Type == 4 || Type == 3)
            {
                for (std::list<unsigned int>::iterator It = CombatIds.begin();
                     It != CombatIds.end(); ++It)
                {
                    if (*It == A)
                    {
                        CombatIds.erase(It);
                        break;
                    }
                }
                if (CombatIds.empty())
                {
                    Ship->SetField1b1(false);
                    if (PrevState != State)
                    {
                        int const Old = State;
                        State = PrevState;
                        PrevState = Old;
                        bRestart = true;
                    }
                }
                break;
            }
            if (Type != 0)
                break;
            if (A != TargetId && A != Ship->SpaceObjId && B != TargetId && B != Ship->SpaceObjId)
                break;
            bool bKnownId = false;
            for (std::list<unsigned int>::iterator It = CombatIds.begin(); It != CombatIds.end(); ++It)
            {
                if (*It == A)
                {
                    bKnownId = true;
                    break;
                }
            }
            if (!bKnownId)
                CombatIds.push_back(A);                                 // FUN_06eb6740
            break;
        }
        if (State != 2 && State != 3 && State != 5 && State != 7)
            break;
        if (Type != 0)
            break;
        if (A != TargetId && A != Ship->SpaceObjId && B != TargetId && B != Ship->SpaceObjId)
            break;
        bool bKnown = false;
        for (std::list<unsigned int>::iterator It = CombatIds.begin(); It != CombatIds.end(); ++It)
        {
            if (*It == A)
            {
                bKnown = true;
                break;
            }
        }
        if (!bKnown)
            CombatIds.push_back(A);
        if (State == 7)
            State = 3;
        Ship->SetField1b1(true);
        SetState(4);
        break;
    }
    case EMessage::Heartbeat:
        HandleTimer(*static_cast<float const*>(Parms), Leg, ApproachSquared);
        break;
    default:
        break;
    }
    FinishEvent();
}

void FGotoGuide::FinishEvent()
{
    if (bRestart)
        Start();
}

// 06f09168-06f0998a: switch on State - 1 (1..10).
void FGotoGuide::HandleTimer(float DeltaSeconds, FNnPathLeg const& Leg, double ApproachSquared)
{
    switch (State)
    {
    case 1:
        SetState(2);
        break;
    case 2:
    {
        // 06f091a6-06f091f3: (x*x + z*z) + y*y of ship - leg start; FCOMPP r^2 / TEST AH,0x41
        // / JNZ: not within reach when r^2 <= distance or NaN.
        Vector const ShipPosition = GetShipPosition(Ship);
        Vector const Delta = SubtractVectors(ShipPosition, Leg.PointA);   // 06f091c0
        double const DistanceSquared =
            (static_cast<double>(Delta.x) * Delta.x + static_cast<double>(Delta.z) * Delta.z) +
            static_cast<double>(Delta.y) * Delta.y;
        if (!(ApproachSquared > DistanceSquared))
        {
            if (FindLegPointNearTarget())
                SetState(8);
            break;
        }
        if (Leg.Kind == 0)
        {
            FContentPlayerState* Player = FindContentPlayerStateByShip(&TargetId);   // FUN_06f49bc0
            if (Player == nullptr)
                break;
            if (IsTargetOnPath(static_cast<int>(Player->GetValueE4()), Player->GetValueE8()))
            {
                SetState(7);
                break;
            }
            SendGuideComm(Ship, TargetId, "mb_gg_cmd_enter_tl");
            SetState(3);
        }
        else if (Leg.Kind == 1)
        {
            SetState(6);
        }
        break;
    }
    case 3:
    {
        bool bNear = FindLaneEntryNearTarget() || IsTargetInShipGroup(Ship, TargetId);
        if (bNear && !Ship->IsInShipClass(&GGuideLargeShipsClass))      // FUN_06f575d0
        {
            Timer = 0.0f;
            NagCount = 0;
            SetState(5);
        }
        else if (FindLegPointNearTarget())
        {
            Timer = 0.0f;
            NagCount = 0;
            SetState(8);
        }
        else if (!(Timer < 30.0f))                                      // DAT_06fb8104
        {
            if (NagCount >= 5)
            {
                if (Leg.Kind == 0)
                    SetState(7);
                else if (Leg.Kind == 1)
                    SetState(6);
            }
            else
            {
                SendGuideComm(Ship, TargetId, "mb_gg_cmd_get_going");
                ++NagCount;
            }
            Timer = 0.0f;
        }
        Timer = static_cast<float>(static_cast<double>(Timer) + DeltaSeconds);
        break;
    }
    case 5:
    {
        FContentPlayerState* Player = FindContentPlayerStateByShip(&TargetId);
        if (!FindLaneEntryNearTarget() && !IsTargetInShipGroup(Ship, TargetId))
        {
            SetState(3);
            break;
        }
        if (Player != nullptr &&
            IsTargetOnPath(static_cast<int>(Player->GetValueE4()), Player->GetValueE8()))
        {
            SetState(7);
            break;
        }
        if (ApproachSquared > DistanceSquaredZyx(GetShipPosition(Ship), Leg.PointB))
            SetState(9);
        break;
    }
    case 6:
    case 7:
        if (ApproachSquared > DistanceSquaredZyx(GetShipPosition(Ship), Leg.PointB))
            SetState(9);
        break;
    case 8:
        if (ApproachSquared > DistanceSquaredZyx(GetShipPosition(Ship), Waypoint))
        {
            FSystemInfo* System = Ship->GetSystem();
            if (System != nullptr)
            {
                Path.clear();                                           // FUN_06fa8370
                float const PathRange = 1000.0f;
                Vector const ShipPosition = GetShipPosition(Ship);
                BuildNnPath(System, &ShipPosition, &Destination, &Path, 0x19, &PathRange, 0);
            }
            SetState(2);
        }
        break;
    case 9:
    {
        FSystemInfo* System = Ship->GetSystem();
        FContentSpaceObj* Target = System != nullptr ? System->FindShip(TargetId) : nullptr;
        if (FindLegPointNearTarget())
        {
            bNodeEndSent = false;
            SetState(8);
            break;
        }
        if (Target != nullptr)
        {
            Vector ShipPosition;
            Ship->GetPosition(&ShipPosition);
            Vector TargetPosition;
            Target->GetPosition(&TargetPosition);
            // FCOMP 0 / TEST AH,5 / JP: the node is passed only when the distance is below 0
            // (never for a real distance), else the "at node end" line below.
            if (DistanceSquaredZyx(TargetPosition, ShipPosition) < 0.0)
            {
                bNodeEndSent = false;
                Path.pop_front();                                       // FUN_06ecfe80
                if (!Path.empty())
                    SetState(2);
                else
                    SetState(10);
                break;
            }
        }
        if (bNodeEndSent)
            break;
        bNodeEndSent = true;
        SendGuideComm(Ship, TargetId, "mb_gg_cmd_at_node_end");
        break;
    }
    case 10:
        bDone = true;
        break;
    default:
        break;
    }
}

// FUN_06f099f0 (06f099f0-06f09ffe). The first leg is read before the tests (retail reads
// the head's first node even when Path is empty; GetFirstLeg gives the zeroed leg then).
// Only runs when a restart was requested and the ship is not in its +0x120 state; the
// directives built for the state (possibly none) replace the ship's list with priority 0.
void FGotoGuide::Start()
{
    FNnPathLeg const Leg = GetFirstLeg();
    if (!bRestart)
        return;
    if (Ship->HasField120())                                            // FUN_06f57710
        return;
    bRestart = false;

    FMissionDirectiveList Directives;                                   // FUN_06f93460
    switch (State)
    {
    case 2:
    {
        TRefHandle<FObjListDirective> New;
        CreateObjListDirective(8, &New.Pointer);
        FObjListGuide* Guide = static_cast<FObjListGuide*>(New.Pointer);
        Guide->SetGuide(&TargetId, &FirstPosition, &Leg.PointA, 500.0f, bNoCruise != 0,
                        bCruise != 0, Field_0x16e != 0, false);         // FUN_06f26770
        Guide->Op.Priority = (uint32_t)Priority;
        KeepAndQueue(this, New, &Directives);
        break;
    }
    case 3:
        Ship->CancelDirectives();                                       // FUN_06f57740
        break;
    case 5:
    {
        TRefHandle<FObjListDirective> New;
        CreateObjListDirective(8, &New.Pointer);
        FObjListGuide* Guide = static_cast<FObjListGuide*>(New.Pointer);
        Guide->SetGuide(&TargetId, &FirstPosition, &Waypoint, 500.0f, bNoCruise != 0,
                        bCruise != 0, Field_0x16e != 0, false);
        KeepAndQueue(this, New, &Directives);
        break;
    }
    case 6:
    {
        TRefHandle<FObjListDirective> New;
        CreateObjListDirective(8, &New.Pointer);
        FObjListGuide* Guide = static_cast<FObjListGuide*>(New.Pointer);
        Guide->SetGuide(&TargetId, &FirstPosition, &Leg.PointB, 500.0f, bNoCruise != 0,
                        bCruise != 0, Field_0x16e != 0, true);
        Guide->Op.Priority = (uint32_t)Priority;
        KeepAndQueue(this, New, &Directives);
        break;
    }
    case 7:
    {
        FSystemInfo* System = Ship->GetSystem();
        if (System == nullptr)
            break;
        std::vector<FTradeLaneRing> Rings;
        System->GetTradeLaneRingsByRing(&Leg.ObjectB, &Rings);         // result not tested
        Vector const ShipPosition = GetShipPosition(Ship);
        FTradeLaneRing Entry;
        FindEntryRing(&Entry, &ShipPosition, &Rings, nullptr);
        TRefHandle<FObjListDirective> New;
        CreateObjListDirective(0x13, &New.Pointer);
        static_cast<FObjListDock*>(New.Pointer)->SetTarget(&Entry.ObjectId, &Leg.ObjectB);   // FUN_06f276f0
        KeepAndQueue(this, New, &Directives);
        break;
    }
    case 8:
    {
        FSystemInfo* System = Ship->GetSystem();
        if (System == nullptr)
            break;
        Vector const ShipPosition = GetShipPosition(Ship);
        BuildPathDirectives(System, &ShipPosition, &Waypoint, &Directives.Items, 0x19, &Range);
        break;
    }
    case 10:
    {
        TRefHandle<FObjListDirective> New;
        CreateObjListDirective(8, &New.Pointer);
        FObjListGuide* Guide = static_cast<FObjListGuide*>(New.Pointer);
        Guide->SetGuide(&TargetId, &FirstPosition, &Destination, Range, bNoCruise != 0,
                        bCruise != 0, Field_0x16e != 0, false);
        Guide->Op.Priority = (uint32_t)Priority;
        KeepAndQueue(this, New, &Directives);
        break;
    }
    default:
        break;
    }
    Ship->SetDirectives(&Directives.Items, 0);                          // FUN_06f57840
}

// ---------------------------------------------------------------------------------------
// Dock guide (06fb8490)
// ---------------------------------------------------------------------------------------

// FUN_06f0a270 (06f0a270-06f0a3a0): the goto guide, then DockTargetId, the two flags 0; the
// target id refreshed with a ship; Destination = the dock target's position; with a ship in a
// system the path is rebuilt to it (range 1000, flags 0x19).
FDockGuide::FDockGuide(FMbDockGuide const* Desc)
    : FGotoGuide(Desc)
    , DockTargetId(Desc->DockTargetId)
{
    if (Ship != nullptr)
        TargetId = Ship->GetSystem()->FindObjectIdByName(&TargetName, Ship->ControllerId);
    Matrix Orientation;
    pub::SpaceObj::GetLocation(DockTargetId, Destination, Orientation);
    if (Ship == nullptr)
        return;
    FSystemInfo* System = Ship->GetSystem();
    if (System == nullptr)
        return;
    Path.clear();
    float const PathRange = 1000.0f;
    Vector const ShipPosition = GetShipPosition(Ship);
    BuildNnPath(System, &ShipPosition, &Destination, &Path, 0x19, &PathRange, 0);
}

// FUN_06f0a3b0 body: the goto part copied, DockTargetId and bDockRestart copied, bDocking 0;
// the target id refreshed with a ship, Destination = the dock target's position.
FDockGuide::FDockGuide(FDockGuide const& Other)
    : FGotoGuide(Other)
    , DockTargetId(Other.DockTargetId)
    , bDockRestart(Other.bDockRestart)
    , bDocking(false)
{
    if (Ship != nullptr)
        TargetId = Ship->GetSystem()->FindObjectIdByName(&TargetName, Ship->ControllerId);
    Matrix Orientation;
    pub::SpaceObj::GetLocation(DockTargetId, Destination, Orientation);
}

// FUN_06f0a3b0 (06f0a3b0-06f0a5cb).
FContentGuide* FDockGuide::Clone()
{
    return new FDockGuide(*this);
}

// LAB_06f0a5d0 (06f0a5d0-06f0a61d).
void FDockGuide::RefreshTarget()
{
    if (Ship != nullptr)
        TargetId = Ship->GetSystem()->FindObjectIdByName(&TargetName, Ship->ControllerId);
    Matrix Orientation;
    pub::SpaceObj::GetLocation(DockTargetId, Destination, Orientation);
}

// LAB_06f0a620 (06f0a620-06f0a7f7, RET 8). Messages 0x11 / 0x16 / 0x20 / 0x27 (byte table
// 06f0a810); the others, and those four when their state test fails, go to the goto guide's
// handler unless the docking part has started. Ends with Start() when bDockRestart is set.
void FDockGuide::HandleEvent(pub::Controller::EMessage MsgId, void const* Parms)
{
    using pub::Controller::EMessage;
    unsigned int const* Ids = static_cast<unsigned int const*>(Parms);
    bool bForward = false;
    switch (MsgId)
    {
    case EMessage::Heartbeat:
        if (State == 10)
        {
            bDocking = true;
            bDone = false;
            PrevState = 10;
            State = 12;
            bDockRestart = true;
        }
        else if (State == 13)
        {
            bDone = true;
        }
        else
        {
            bForward = true;
        }
        break;
    case EMessage::Msg0x27:
    {
        if (State != 12)
        {
            bForward = true;
            break;
        }
        if (Ids[0] != Ship->SpaceObjId)
            break;
        unsigned int Type;
        if (pub::SpaceObj::GetType(DockTargetId, Type) != 0)
            break;
        unsigned int DockingTarget = 0;
        pub::SpaceObj::GetDockingTarget(DockTargetId, DockingTarget);
        bool bArrived = false;
        if ((Type & 0x100) != 0 && (Ids[1] & 0x3fffffff) != 0 && DockingTarget == Ids[1])
            bArrived = true;
        else if ((Type & 0x20) != 0 && DockTargetId == Ids[2])
            bArrived = true;
        if (bArrived && State != 13)
        {
            PrevState = State;
            State = 13;
            bDockRestart = true;
        }
        break;
    }
    case EMessage::DockComplete:
    {
        if (State != 12)
        {
            bForward = true;
            break;
        }
        if (Ids[0] != Ship->SpaceObjId)
            break;
        unsigned int Type;
        if (pub::SpaceObj::GetType(DockTargetId, Type) != 0)
            break;
        if ((Type & 0xc40) == 0)
            break;
        if (DockTargetId != Ids[1])
            break;
        if (State != 13)
        {
            PrevState = State;
            State = 13;
            bDockRestart = true;
        }
        break;
    }
    case EMessage::TradelaneEnter:
        if (State != 12)
        {
            bForward = true;
            break;
        }
        if (Ids[0] == Ship->SpaceObjId && Ids[1] == DockTargetId)
        {
            PrevState = 12;
            State = 13;
            bDockRestart = true;
        }
        break;
    default:
        bForward = true;
        break;
    }

    if (bForward && !bDocking)
        FGotoGuide::HandleEvent(MsgId, Parms);                          // FUN_06f08c70
    if (bDockRestart)
        Start();
}

// LAB_06f0a830 (06f0a830-06f0aa00). Before docking the goto guide's Start runs first
// (FUN_06f099f0); then, when a dock restart is pending and the ship is not in its +0x120
// state, state 12 hands the ship one Dock directive at DockTargetId (FUN_06f276f0 with
// DAT_06fb80e4 = 0), and the pending flag is cleared.
void FDockGuide::Start()
{
    if (!bDocking)
        FGotoGuide::Start();
    if (!bDockRestart)
        return;
    if (Ship->HasField120())
        return;
    if (State == 12)
    {
        FMissionDirectiveList Directives;                               // FUN_06f93790 head
        // Ref-holding local (FuncInfo 06fc1fd4 state 1: FUN_06f28e10).
        TRefHandle<FObjListDirective> New;
        CreateObjListDirective(0x13, &New.Pointer);
        unsigned int const Zero = 0;
        static_cast<FObjListDock*>(New.Pointer)->SetTarget(&DockTargetId, &Zero);
        KeepAndQueue(this, New, &Directives);
        Ship->SetDirectives(&Directives.Items, 0);
    }
    bDockRestart = false;
}
