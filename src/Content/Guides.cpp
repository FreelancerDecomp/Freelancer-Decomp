// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU37 (06f05d00-06f0e000): the NPC guides. Lane CNT-AI owns this file.

#include "Guides.h"
#include "GuidesInternal.h"
#include "BaseDB.h"               // FindContentBaseInfo (FGuideDesc::FindTargetBase)
#include "BehaviorCmds.h"         // FContentShipBase directive list
#include "DockQueues.h"           // GetShipDockingTarget, CollectFormationMembers
#include "MissionObject.h"        // FNnPathLeg, FMissionDirectiveList
#include "MissionObjList.h"       // CreateObjListDirective and the directive setters
#include "NpcShip.h"              // FContentNpcShip
#include "SpacePaths.h"           // BuildNnPath, BuildPathDirectives, the wander helpers
#include "SystemInfo.h"
#include "ContentStaticIds.h"     // TU37's 16 static-init thunks 06f05d00-06f05ee0
#include "ContentInternal.h"      // SubtractVectors (FUN_06eeaee0)
#include "common_id.h"            // CreateID
#include "common_ini.h"
#include "common_server.h"        // pub::GetNicknameId, pub::SpaceObj, pub::Zone

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

// TU37 globals written by its static initializer table (06fcda18..06fcda64).
// LAB_06f07450: DAT_06fd0648 = DAT_06fb8100 * DAT_06fb8100 (10000 squared).
float GGuideLaneEntryRangeSquared = 10000.0f * 10000.0f;
// LAB_06f084b0: DAT_06fd0644 = CreateID("LARGE_SHIPS") (goto guide state 3).
unsigned int GGuideLargeShipsClass = CreateID("LARGE_SHIPS");
// LAB_06f08910 / LAB_06f08920: DAT_06fd0640 = DAT_06fb80ec (-0.6f), DAT_06fd063c =
// DAT_06fb80f4 (0.6f). The attitude window of the patrol scanner's targets
// (FCargoScanRoute::FindScanTarget, 06f0c8e7 / 06f0c8ed; PatrolGuides.cpp).
float GPatrolScanMinAttitude = -0.6f;
float GPatrolScanMaxAttitude = 0.6f;
// DAT_06fce590 (.data, 150.0f, one reader: the wander guide 06f0b457, no writer).
float GWanderObstacleRange = 150.0f;

namespace
{
    // FUN_06f0bb80 (cdecl): *Out = A + B.
    Vector Add(Vector const& A, Vector const& B)
    {
        Vector Out;
        Out.x = A.x + B.x;
        Out.y = A.y + B.y;
        Out.z = A.z + B.z;
        return Out;
    }

    Vector GetShipPosition(FContentNpcShip* Ship)
    {
        Vector Position;
        Ship->GetPosition(&Position);                                   // FUN_06eb70f0
        return Position;
    }
}

// FUN_06f0de80 (06f0de80-06f0dead).
void AssignDirective(FObjListDirective** Slot, FObjListDirective* New)
{
    if (*Slot == New)
        return;
    if (*Slot != nullptr)
    {
        (*Slot)->Release();
        *Slot = nullptr;
    }
    *Slot = New;
    if (New != nullptr)
        New->AddRef();
}

// FUN_06f57740 + FUN_06f577d0 + FUN_06f57c00 on the guide's ship.
void IssueOnlyDirective(FContentNpcShip* Ship, FObjListDirective* Directive)
{
    Ship->CancelDirectives();
    Ship->AddDirective(Directive);
    Ship->RestartDirectives();
}

// FUN_06f07a80 (06f07a80-06f07b51): REPE CMPSB over each literal including its NUL, i.e.
// strcmp == 0; the last compare (against the empty string DAT_06fb572c) returns SETNZ.
int __cdecl ParseGuideBehavior(char const* Name)
{
    if (strcmp(Name, "trade") == 0)
        return 2;
    if (strcmp(Name, "patrol_path") == 0)
        return 3;
    if (strcmp(Name, "nothing") == 0)
        return 1;
    if (strcmp(Name, "wander") == 0)
        return 4;
    if (strcmp(Name, "goto_guide") == 0)
        return 5;
    if (strcmp(Name, "dock_guide") == 0)
        return 6;
    if (strcmp(Name, "patrol_guide") == 0)
        return 7;
    return Name[0] != '\0' ? 1 : 0;
}

// ---------------------------------------------------------------------------------------
// [MetaBehavior]
// ---------------------------------------------------------------------------------------

// FUN_06f07b60 (06f07b60-06f07c0b): head (FUN_06f06530 inline: RefCount 1, Type 5, Ship 0,
// Line empty), +0x114 = 0, the leg list (+0x118 allocator byte, +0x11c head, +0x120 size 0),
// +0x13c = 0, TargetName empty, +0x174 = 0, +0x175 = 0, +0x176 = 1, vtable 06fb82d8. The
// positions +0x124 / +0x130 are not written.
FMbGotoGuide::FMbGotoGuide()
    : FMetaBehavior(5, nullptr)
{
    TargetName.Length = 0;
    TargetName.Data[0] = '\0';
}

// FUN_06f06840 (06f06840-06f068c2; called by the sdd 06f07c10): every leg node erased
// (FUN_06ecfe80), the head freed, then the base vptr.
FMbGotoGuide::~FMbGotoGuide() = default;

// FUN_06f07c30 (06f07c30-06f07dda). The float values are read 3, 2, 1 and 6, 5, 4 (the order
// does not matter: get_value_float has no side effect on the reader's position).
void FMbGotoGuide::Parse(INI_Reader* Reader)
{
    TargetName = Reader->get_value_string(0);
    FirstPosition.z = Reader->get_value_float(3);
    FirstPosition.y = Reader->get_value_float(2);
    FirstPosition.x = Reader->get_value_float(1);
    Destination.z = Reader->get_value_float(6);
    Destination.y = Reader->get_value_float(5);
    Destination.x = Reader->get_value_float(4);
    Range = static_cast<float>(Reader->get_value_int(7));             // FILD / FSTP float
    if (!Reader->is_value_empty(8))
        Field_0x176 = Reader->get_value_bool(8) ? 1 : 0;
    if (Reader->is_value_empty(9))
        return;
    if (_stricmp(Reader->get_value_string(9), "NORMAL") == 0)
    {
        Priority = 1;
        return;
    }
    if (_stricmp(Reader->get_value_string(9), "ALWAYS_EXECUTE") == 0)
        Priority = 0;
}

// FUN_06f0a030 (06f0a030-06f0a0ea). Values 2, 1, 3 are read in that order (no side effect).
// Destination (+0x130) is left as the ctor left it (not written).
void FMbDockGuide::Parse(INI_Reader* Reader)
{
    TargetName = Reader->get_value_string(0);                         // FUN_06ea9f40 clamp
    float const Y = Reader->get_value_float(2);
    float const X = Reader->get_value_float(1);
    float const Z = Reader->get_value_float(3);
    FirstPosition.x = X;
    FirstPosition.y = Y;
    FirstPosition.z = Z;
    DockTargetId = pub::GetNicknameId(Reader->get_value_string(4));
    Range = Reader->get_value_float(5);
}

// FUN_06f065e0 (06f065e0-06f0683c). The new object gets an extra local reference (smart
// pointer) and one for the list (the node copy AddRefs); both local references are released
// afterwards, so the list holds the one reference. The Type is written a second time after the
// ctor (5 / 6); Line = get_line_ptr() (FUN_06f0dfa0 for the goto guide, the inlined
// FUN_06ea9f40 copy for the dock guide: same clamp), then Parse (slot +0x0c).
void __cdecl LoadMetaBehavior(INI_Reader* Reader, std::list<FMetaBehavior*>* List,
                              unsigned int* OutNickname)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            *OutNickname = CreateID(Reader->get_value_string());
            continue;
        }
        if (Reader->is_value("system"))
            continue;

        FMetaBehavior* Created = nullptr;
        int Type = 0;
        if (_stricmp(Reader->get_name_ptr(), "MB_GOTOGUIDE") == 0)
        {
            Created = new FMbGotoGuide();
            Type = 5;
        }
        else if (_stricmp(Reader->get_name_ptr(), "MB_DOCKGUIDE") == 0)
        {
            Created = new FMbDockGuide();
            Type = 6;
        }
        else
        {
            continue;
        }
        // The local handle (EH states 2 / 4 of 06f065e0) takes a reference of its own.
        TRefHandle<FMetaBehavior> Behavior;
        Behavior = Created;
        Behavior->AddRef();
        Behavior->Type = Type;
        Behavior->Line = Reader->get_line_ptr();
        Behavior->Parse(Reader);
        List->push_back(Behavior);                                     // FUN_06f1e390
        Behavior->AddRef();
        Created->Release();
    }
}

// ---------------------------------------------------------------------------------------
// CreateGuide
// ---------------------------------------------------------------------------------------

// FUN_06f05f00 (06f05f00-06f0616d). Switch on Desc->Type - 1 (0..5): 1 nothing (0x28),
// 2 trade (0x34, FUN_06f06fe0), 3 patrol_path (0x34, FUN_06f0aa40), 4 wander (0x3c),
// 5 goto (0x1b0, FUN_06f07ef0), 6 dock (0x1b8, FUN_06f0a270); anything else gives null.
// Tail (06f06124-06f0615f): *InOut != new -> the old one released and cleared, *InOut = new,
// new AddRef'ed; then (also when equal) *InOut released once more -> the fresh guide ends at
// RefCount 1 held by *InOut. Returns *InOut != null. Type 3 (06f06004-06f06030): new 0x34,
// FUN_06f0aa40 (FPatrolGuide, PatrolGuides.cpp).
bool __cdecl CreateGuide(FGuideDescBase* Desc, FContentGuide** InOutGuide)
{
    FContentGuide* Guide = nullptr;
    switch (Desc->Type)
    {
    case 1:
        Guide = new FNothingGuide(static_cast<FGuideDesc const*>(Desc));
        break;
    case 2:
        Guide = new FTradeGuide(static_cast<FGuideDesc const*>(Desc));
        break;
    case 3:
        Guide = new FPatrolGuide(static_cast<FGuideDesc const*>(Desc));
        break;
    case 4:
        Guide = new FWanderGuide(static_cast<FGuideDesc const*>(Desc));
        break;
    case 5:
        Guide = new FGotoGuide(static_cast<FMbGotoGuide const*>(Desc));
        break;
    case 6:
        Guide = new FDockGuide(static_cast<FMbDockGuide const*>(Desc));
        break;
    default:
        break;
    }

    if (*InOutGuide != Guide)
    {
        if (*InOutGuide != nullptr)
        {
            (*InOutGuide)->Release();
            *InOutGuide = nullptr;
        }
        *InOutGuide = Guide;
        if (Guide == nullptr)
            return false;
        Guide->AddRef();
    }
    if (*InOutGuide != nullptr)
        (*InOutGuide)->Release();
    return *InOutGuide != nullptr;
}

// ---------------------------------------------------------------------------------------
// Target guide (06fb81d8)
// ---------------------------------------------------------------------------------------

FTargetGuide::FTargetGuide(FGuideDesc const* Desc)
    : FContentGuide(Desc)
{
    Desc->GetTargets(&TargetId1, &TargetId2, &TargetBase1, &TargetBase2);   // FUN_06f062d0
}

// 06ebf720.
void FTargetGuide::Reset()
{
}

// LAB_06f068f0 (06f068f0-06f0691c).
void FTargetGuide::GetTargets(unsigned int* OutId1, unsigned int* OutId2,
                              FContentBase** OutBase1, FContentBase** OutBase2)
{
    *OutId1 = TargetId1;
    *OutId2 = TargetId2;
    if (OutBase1 == nullptr)
        return;
    *OutBase1 = TargetBase1;
    if (OutBase2 == nullptr)
        return;
    *OutBase2 = TargetBase2;
}

// LAB_06f068d0 (06f068d0-06f068ea).
void FTargetGuide::SwapTargets()
{
    unsigned int const Id1 = TargetId1;
    FContentBase* const Base1 = TargetBase1;
    TargetId1 = TargetId2;
    TargetBase1 = TargetBase2;
    TargetId2 = Id1;
    TargetBase2 = Base1;
}

// FUN_06f06a50 (06f06a50-06f06b11): seconds = (rand() * 11) / 32768 (signed division) + 20,
// FILD of the int64 {seconds, 0} into the float argument of SetDelay (FUN_06f27940). The
// directive lives in a ref-holding local (FuncInfo 06fc1ba8 state 0: FUN_06f28e10).
void FTargetGuide::IssueDelayDirective()
{
    TRefHandle<FObjListDirective> Directive;
    CreateObjListDirective(1, &Directive.Pointer);                      // FUN_06f24870
    int const Seconds = (std::rand() * 11) / 32768 + 20;
    FObjListDelay* Delay = static_cast<FObjListDelay*>(Directive.Pointer);
    Delay->SetDelay(static_cast<float>(static_cast<long long>(Seconds)));
    Delay->Op.Priority = 1;                                             // [dir+0x114]
    IssueOnlyDirective(Ship, Directive);
}

// FUN_06f069c0 (06f069c0-06f06a47): factory 0x15 without a setter; ref-holding local
// (FuncInfo 06fc2280 state 0: FUN_06f28e10).
void FTargetGuide::IssueLaunchDirective()
{
    TRefHandle<FObjListDirective> Directive;
    CreateObjListDirective(0x15, &Directive.Pointer);
    static_cast<FObjListLaunch*>(Directive.Pointer)->Op.Priority = 1;
    IssueOnlyDirective(Ship, Directive);
}

// FUN_06f06920 (06f06920-06f069b9): Dock SetTarget(&TargetId2, &DAT_06fb80e4 (0)); ref-holding
// local (FuncInfo 06fc2280 state 0: FUN_06f28e10).
void FTargetGuide::IssueDockDirective()
{
    TRefHandle<FObjListDirective> Directive;
    CreateObjListDirective(0x13, &Directive.Pointer);
    unsigned int const Zero = 0;                                        // DAT_06fb80e4
    FObjListDock* Dock = static_cast<FObjListDock*>(Directive.Pointer);
    Dock->SetTarget(&TargetId2, &Zero);                                 // FUN_06f276f0
    Dock->Op.Priority = 1;
    IssueOnlyDirective(Ship, Directive);
}

// LAB_06f06b20 (06f06b20-06f06dd9). The two squared distances are stored as floats and
// compared (TEST AH,5 / JP: the first target wins only when strictly nearer). The directive
// list is built for the nearer target (Range 2500, flags 1) only to read its first directive;
// taking the first target swaps the targets. The list is released afterwards.
bool FTargetGuide::RequestFleeDestination(pub::AI::ContentCallback::FleeDestinationType& OutType,
                                          Vector& OutPosition, unsigned int& OutParam,
                                          float& OutFloat)
{
    if (TargetId1 == 0 || TargetId2 == 0)
        return false;

    Vector Position1;
    Matrix Orientation;
    pub::SpaceObj::GetLocation(TargetId1, Position1, Orientation);
    Vector const Delta1 = SubtractVectors(GetShipPosition(Ship), Position1);   // 06f06b81
    float const Distance1 = static_cast<float>(
        (static_cast<double>(Delta1.z) * Delta1.z + static_cast<double>(Delta1.y) * Delta1.y) +
        static_cast<double>(Delta1.x) * Delta1.x);

    Vector Position2;
    pub::SpaceObj::GetLocation(TargetId2, Position2, Orientation);
    Vector const Delta2 = SubtractVectors(GetShipPosition(Ship), Position2);   // 06f06bd1
    float const Distance2 = static_cast<float>(
        (static_cast<double>(Delta2.z) * Delta2.z + static_cast<double>(Delta2.y) * Delta2.y) +
        static_cast<double>(Delta2.x) * Delta2.x);

    FMissionDirectiveList Directives;                                   // FUN_06f93460
    float const Range = 2500.0f;
    if (Distance1 < Distance2)
    {
        Vector const ShipPosition = GetShipPosition(Ship);
        BuildPathDirectives(Ship->GetSystem(), &ShipPosition, &Position1, &Directives.Items, 1,
                            &Range);
        SwapTargets();
    }
    else
    {
        Vector const ShipPosition = GetShipPosition(Ship);
        BuildPathDirectives(Ship->GetSystem(), &ShipPosition, &Position2, &Directives.Items, 1,
                            &Range);
    }
    if (Directives.Items.empty())
        return false;

    // The front is held in a ref-holding local (06f06c99-06f06cae: AddRef when set; FuncInfo
    // 06fc1c04 state 1, funclet 06faceb8: FUN_06f28e10), and the type 5 / 0x13 branches take a
    // second, typed copy (AddRef 06f06d4d / 06f06ce4, Release 06f06d8a / 06f06d06) before the
    // holder is released (06f06d95 / 06f06d11 / 06f06cd2), then the list.
    TRefHandle<FObjListDirective> First;
    First = Directives.Items.front();
    if (First.Pointer != nullptr)
        First->AddRef();
    int const Type = First->GetType();
    if (Type == 5)
    {
        TRefHandle<FObjListDirective> Typed(First);
        OutType = static_cast<pub::AI::ContentCallback::FleeDestinationType>(3);
        OutPosition = static_cast<FObjListGotoVec*>(Typed.Pointer)->Op.Destination;   // +0x11c
        OutFloat = 2000.0f;
        return true;
    }
    if (Type == 0x13)
    {
        TRefHandle<FObjListDirective> Typed(First);
        OutType = static_cast<pub::AI::ContentCallback::FleeDestinationType>(2);
        OutParam = static_cast<FObjListDock*>(Typed.Pointer)->Op.DockObjectId;       // +0x118
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------
// Type 1: nothing (06fb8188)
// ---------------------------------------------------------------------------------------

// CreateGuide case 1: the Delay directive is built with the directive's own ctor (FUN_06f06e50:
// base, DirectiveDelayOp, vtable 06fb82f8 - the factory's case 1) and assigned through
// FUN_06f0de80, which AddRefs it: the creation reference is never released, so the
// directive keeps one reference more than the guide's (retail behaviour, kept). +0x24 is a
// ref-holding member (CreateGuide's unwind state 3, funclet 06facd9b: FUN_06f28e10 on +0x24).
FNothingGuide::FNothingGuide(FGuideDesc const* Desc)
    : FTargetGuide(Desc)
{
    FObjListDelay* Delay = new FObjListDelay();
    AssignDirective(&DelayDirective.Pointer, Delay);
    static_cast<FObjListDelay*>(DelayDirective.Pointer)->SetDelay(10000.0f);    // 0x461c4000
    static_cast<FObjListDelay*>(DelayDirective.Pointer)->Op.Priority = 1;
}

// Clone body 06f06f46-06f06f77: +0x24 = 0 (EH state 2: the member), then the smart assignment
// of the source's directive (AddRef when set) = the holder's copy.
FNothingGuide::FNothingGuide(FNothingGuide const& Other)
    : FTargetGuide(Other)
    , DelayDirective(Other.DelayDirective)
{
}

// FUN_06f06e00: +0x24 released when set and cleared (the member's destructor).
FNothingGuide::~FNothingGuide() = default;

// FUN_06f06ed0 (06f06ed0-06f06f9e).
FContentGuide* FNothingGuide::Clone()
{
    return new FNothingGuide(*this);
}

// FUN_06f06fa0 (06f06fa0-06f06fbb).
void FNothingGuide::Start()
{
    IssueOnlyDirective(Ship, DelayDirective);
}

// LAB_06f0af30: Start().
void FNothingGuide::OnDirectivesDone(int ResultWord11c, int ResultWord118)
{
    (void)ResultWord11c;
    (void)ResultWord118;
    Start();
}

// ---------------------------------------------------------------------------------------
// Routes (06fb839c / 06fb8384)
// ---------------------------------------------------------------------------------------

// LAB_06f070f0 (RET 8).
int FGuideRoute::HandleEvent(pub::Controller::EMessage MsgId, void const* Parms)
{
    (void)MsgId;
    (void)Parms;
    return GetStatus();
}

// LAB_06f07100 (RET 4).
void FGuideRoute::SetShip(FContentNpcShip* InShip)
{
    Ship = InShip;
}

// LAB_06f07750 (06f07750-06f077a3): +4, +8, +0xc..+0x14, +0x18 copied, +0x1c = 0.
FGuideRoute* FTradeRoute::Clone()
{
    FTradeRoute* Copy = new FTradeRoute(*this);
    Copy->Field_0x1c = 0;
    return Copy;
}

// 06ee9e10: 1.
int FTradeRoute::GetType()
{
    return 1;
}

// LAB_06f07110: State == 4 ? 3 : 1.
int FTradeRoute::GetStatus()
{
    return State == 4 ? 3 : 1;
}

// LAB_06f077b0 (06f077b0-06f0792f, RET 4). The state loop (JA to its own head for State > 4,
// as retail). Each BuildPathDirectives uses flags 1 and &Range.
int FTradeRoute::Update(std::list<FObjListDirective*>* Out)
{
    for (;;)
    {
        switch (State)
        {
        case 0:
        {
            Vector const ShipPosition = GetShipPosition(Ship);
            BuildPathDirectives(Ship->GetSystem(), &ShipPosition, &Center, Out, 1, &Range);
            if (Out->empty())
            {
                State = 4;
                return GetStatus();
            }
            int const Type = Out->front()->GetType();
            if (Type == 5)
                State = 1;
            else
                State = Type == 0x13 ? 2 : 1;
            return GetStatus();
        }
        case 1:
        {
            FObjListDirective** Active = Ship->GetActiveDirectiveSlot();
            Vector From;
            if ((*Active)->GetType() == 5)
                From = static_cast<FObjListGotoVec*>(*Active)->Op.Destination;   // +0x11c
            else
                From = GetShipPosition(Ship);
            BuildPathDirectives(Ship->GetSystem(), &From, &Center, Out, 1, &Range);
            if (Out->empty())
            {
                State = 4;
                return GetStatus();
            }
            int const Type = Out->front()->GetType();
            if (Type == 5)
                State = 1;
            else if (Type == 0x13)
                State = 2;
            return GetStatus();
        }
        case 2:
            State = 3;
            continue;
        case 3:
            if (AddFormationWaitDirective(Out))
                return GetStatus();
            State = 1;
            continue;
        case 4:
            return GetStatus();
        default:
            continue;
        }
    }
}

// FUN_06f07950 (06f07950-06f07a73).
bool FTradeRoute::AddFormationWaitDirective(std::list<FObjListDirective*>* Out)
{
    std::vector<FContentShipBase*> Members;
    CollectFormationMembers(&Members, Ship->FormationId);               // FUN_06f66220
    for (std::vector<FContentShipBase*>::iterator It = Members.begin(); It != Members.end(); ++It)
    {
        if ((*It)->SpaceObjId == Ship->SpaceObjId)
            continue;
        if (!(*It)->HasField120())                                      // FUN_06f57710
            continue;
        // Ref-holding local (FuncInfo 06fc1d5c state 1: FUN_06f28e10; state 0 the vector).
        TRefHandle<FObjListDirective> Directive;
        CreateObjListDirective(1, &Directive.Pointer);
        FObjListDelay* Delay = static_cast<FObjListDelay*>(Directive.Pointer);
        Delay->SetDelay(1.0f);
        Delay->Op.Priority = 1;
        Out->push_back(Directive.Pointer);                              // FUN_06f1e390 + AddRef
        Directive->AddRef();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------
// Type 2: trade (06fb8330)
// ---------------------------------------------------------------------------------------

// FUN_06f06fe0 (06f06fe0-06f070e0). The route (0x20): Ship = Desc->Ship, State 0, Center =
// the GetRadius centre of TargetId2, Range = its radius + DAT_06fb83b4 (3000), +0x1c = 0.
FTradeGuide::FTradeGuide(FGuideDesc const* Desc)
    : FTargetGuide(Desc)
    , TradeGood(Desc->TradeGood)
    , ReturnGood(Desc->ReturnGood)
{
    float Radius;
    Vector RadiusCenter;
    pub::SpaceObj::GetRadius(TargetId2, Radius, RadiusCenter);
    FTradeRoute* NewRoute = new FTradeRoute();
    NewRoute->Ship = Desc->Ship;
    NewRoute->State = 0;
    NewRoute->Center = RadiusCenter;
    NewRoute->Range = Radius + 3000.0f;
    NewRoute->Field_0x1c = 0;
    Route = NewRoute;
}

FTradeGuide::FTradeGuide(FTradeGuide const& Other)
    : FTargetGuide(Other)
    , State(Other.State)
    , TradeGood(Other.TradeGood)
    , ReturnGood(Other.ReturnGood)
{
    Route = Other.Route != nullptr ? static_cast<FTradeRoute*>(Other.Route->Clone()) : nullptr;
}

FTradeGuide::~FTradeGuide()
{
    delete Route;                                                       // FUN_06fa8fe0
}

// FUN_06f07180 (06f07180-06f07233).
FContentGuide* FTradeGuide::Clone()
{
    return new FTradeGuide(*this);
}

// FUN_06f07240 (06f07240-06f07434). Retail also builds an outer local directive list before
// the state loop that nothing fills (head 06f0725f-06f0727e) and tears it down on the way out
// (06f073a4-06f07421; unwind states 0 / 3, funclets 06facf40 FUN_06ecfa90 / 06facf58
// FUN_06f15350); the port holds it as the same local. The State > 3 head jump (JA to the loop
// head) spins as in retail.
void FTradeGuide::Start()
{
    FMissionDirectiveList UnusedDirectives;
    for (;;)
    {
        switch (State)
        {
        case 0:
        {
            FMissionDirectiveList Next;
            if (Route->Update(&Next.Items) == 3)
            {
                State = 1;
                continue;
            }
            Ship->SetDirectives(&Next.Items, 0);                        // FUN_06f57840
            return;
        }
        case 1:
            IssueDockDirective();
            State = 2;
            return;
        case 2:
        {
            unsigned int const ShipId = Ship->SpaceObjId;
            unsigned int DockTarget;
            int DockIndex;
            if (GetShipDockingTarget(&ShipId, &DockTarget, &DockIndex, nullptr))   // FUN_06f6eca0
            {
                IssueDelayDirective();
                State = 3;
                return;
            }
            SwapTargets();
            State = 0;
            continue;
        }
        case 3:
            IssueLaunchDirective();
            SwapTargets();
            State = 0;
            continue;
        default:
            continue;
        }
    }
}

// LAB_06f061a0: +0x24 = 0.
void FTradeGuide::Reset()
{
    State = 0;
}

// LAB_06f0af30: Start().
void FTradeGuide::OnDirectivesDone(int ResultWord11c, int ResultWord118)
{
    (void)ResultWord11c;
    (void)ResultWord118;
    Start();
}

// LAB_06f07120: Ship = InShip, then the route's SetShip (slot 5, tail jump).
void FTradeGuide::SetShip(FContentNpcShip* InShip)
{
    Ship = InShip;
    Route->SetShip(InShip);
}

// LAB_06f07140.
unsigned int FTradeGuide::GetTradeGood()
{
    return TradeGood;
}

// LAB_06f07150.
unsigned int FTradeGuide::GetReturnGood()
{
    return ReturnGood;
}

// LAB_06f076c0 (06f076c0-06f07743): goods and targets swapped; the route aims at the new
// TargetId2: Center = its GetLocation position (the GetRadius centre is fetched and unused),
// Range = its radius + 3000, State 0.
void FTradeGuide::SwapTargets()
{
    unsigned int const Good = TradeGood;
    TradeGood = ReturnGood;
    ReturnGood = Good;
    FTargetGuide::SwapTargets();

    Vector Position;
    Matrix Orientation;
    pub::SpaceObj::GetLocation(TargetId2, Position, Orientation);
    float Radius;
    Vector RadiusCenter;
    pub::SpaceObj::GetRadius(TargetId2, Radius, RadiusCenter);
    Route->Range = Radius + 3000.0f;                                    // DAT_06fb83b4
    Route->Center = Position;
    Route->State = 0;
}

// FUN_06f07470 (06f07470-06f076b1). Legs to TargetId2 (flags 1, range 1500, seventh argument
// 1). No legs, or a free first leg: dock type 2 at TargetId2. A lane first leg: within 10000
// (DAT_06fd0648, TEST AH,0x41 / JNZ = not greater) of its first ring -> type 2 at that ring;
// else type 3 towards it: the unit direction times 10000 (x / y rounded to float after the
// normalisation, z kept on the x87 stack), OutFloat 500.
bool FTradeGuide::RequestFleeDestination(pub::AI::ContentCallback::FleeDestinationType& OutType,
                                         Vector& OutPosition, unsigned int& OutParam,
                                         float& OutFloat)
{
    std::list<FNnPathLeg> Legs;                                         // FUN_06f93640
    Vector const ShipPosition = GetShipPosition(Ship);
    Vector TargetPosition;
    Matrix Orientation;
    pub::SpaceObj::GetLocation(TargetId2, TargetPosition, Orientation);
    float const Range = 1500.0f;
    BuildNnPath(Ship->GetSystem(), &ShipPosition, &TargetPosition, &Legs, 1, &Range, 1);

    if (Legs.empty() || Legs.front().Kind == 1)
    {
        OutType = static_cast<pub::AI::ContentCallback::FleeDestinationType>(2);
        OutParam = TargetId2;
        return true;
    }

    FNnPathLeg const& First = Legs.front();
    Vector RingPosition;
    Matrix RingOrientation;
    pub::SpaceObj::GetLocation(First.ObjectA, RingPosition, RingOrientation);
    double const Dz = static_cast<double>(RingPosition.z) - ShipPosition.z;
    float const Dy = static_cast<float>(static_cast<double>(RingPosition.y) - ShipPosition.y);
    float const Dx = static_cast<float>(static_cast<double>(RingPosition.x) - ShipPosition.x);
    double const LengthSquared =
        (static_cast<double>(Dx) * Dx + static_cast<double>(Dy) * Dy) + Dz * Dz;
    if (!(LengthSquared > GGuideLaneEntryRangeSquared))
    {
        OutType = static_cast<pub::AI::ContentCallback::FleeDestinationType>(2);
        OutParam = First.ObjectA;
        return true;
    }

    float const Inverse = static_cast<float>(1.0 / std::sqrt(LengthSquared));
    float const Nx = static_cast<float>(static_cast<double>(Dx) * Inverse);
    float const Ny = static_cast<float>(static_cast<double>(Dy) * Inverse);
    double const Nz = Dz * Inverse;
    OutPosition.x = static_cast<float>(static_cast<double>(Nx) * 10000.0);
    OutPosition.y = static_cast<float>(static_cast<double>(Ny) * 10000.0);
    OutPosition.z = static_cast<float>(Nz * 10000.0);
    OutType = static_cast<pub::AI::ContentCallback::FleeDestinationType>(3);
    OutFloat = 500.0f;
    return true;
}

// ---------------------------------------------------------------------------------------
// Type 4: wander (06fb8138)
// ---------------------------------------------------------------------------------------

// CreateGuide case 4 (06f06051-06f060c4): +0x24 = 0, +0x28 = 0 (FUN_06f0de80 with null),
// +0x2c = Desc->WanderZone, +0x30 / +0x34 / +0x38 = 1500 / 3000 / 500.
FWanderGuide::FWanderGuide(FGuideDesc const* Desc)
    : FTargetGuide(Desc)
    , Zone(Desc->WanderZone)
{
}

// Clone body: +0x28 copied and AddRef'ed when set (the holder's copy).
FWanderGuide::FWanderGuide(FWanderGuide const& Other)
    : FTargetGuide(Other)
    , State(Other.State)
    , Directive(Other.Directive)
    , Zone(Other.Zone)
    , MinRange(Other.MinRange)
    , MaxRange(Other.MaxRange)
    , Height(Other.Height)
{
}

// FUN_06f0b300: +0x28 released when set and cleared (the member's destructor).
FWanderGuide::~FWanderGuide() = default;

// FUN_06f0b350 (06f0b350-06f0b40f).
FContentGuide* FWanderGuide::Clone()
{
    return new FWanderGuide(*this);
}

// LAB_06f061a0: +0x24 = 0.
void FWanderGuide::Reset()
{
    State = 0;
}

// LAB_06f0af30: Start().
void FWanderGuide::OnDirectivesDone(int ResultWord11c, int ResultWord118)
{
    (void)ResultWord11c;
    (void)ResultWord118;
    Start();
}

// LAB_06f0b410 (06f0b410-06f0bb48).
//   An obstacle near the ship (FindObstacleCenter, range DAT_06fce590): a GotoVec 200 away
//   from its centre (range 100).
//   Else 16 candidate points around (ship x, Height, ship z) (PickPointsAround with
//   MinRange / MaxRange), tried from a random start index, each index once: a point is taken
//   when it lies in the zone (ClassifyPoint < 1.0) or there is no zone, and the segment to it
//   is clear (IsSegmentClear). Found: a GotoVec to it (range 500); a MinRange below 1500 is
//   reset to 1500 / 3000.
//   None found: MinRange > 100 -> a 0.5 s Delay and both ranges halved; else the ranges are
//   reset and, with a zone, a GotoVec towards the zone (its position when within 1000,
//   else 1000 towards it; range 250).
//   Every GotoVec: Field_5A 0, Field_5B 1, Field_58 1, BaseOp Field_08 1. Each new directive
//   lives in a ref-holding local (FuncInfo 06fc2164 states 0 / 2 / 3 / 4: FUN_06f28e10; state
//   1 is the index vector) and replaces +0x28 (FUN_06f0de80 smart assignment); the tail hands
//   +0x28 to the ship as its only directive (also when nothing new was made).
void FWanderGuide::Start()
{
    if (State == 0)
        State = 1;

    Vector const ShipPosition = GetShipPosition(Ship);
    FSystemInfo* System = Ship->GetSystem();
    Vector Obstacle;
    if (FindObstacleCenter(System->SystemId, ShipPosition, GWanderObstacleRange, &Obstacle))
    {
        Vector Away = SubtractVectors(ShipPosition, Obstacle);         // 06f0b4a5
        double const Length = std::sqrt(                                // FUN_06f0bb50
            (static_cast<double>(Away.x) * Away.x + static_cast<double>(Away.y) * Away.y) +
            static_cast<double>(Away.z) * Away.z);
        double const Inverse = 1.0 / Length;
        double const X = Away.x * Inverse;
        Away.y = static_cast<float>(Away.y * Inverse);
        Away.z = static_cast<float>(Away.z * Inverse);
        Away.x = static_cast<float>(X * 200.0);                         // DAT_06fb8578
        Away.y = static_cast<float>(static_cast<double>(Away.y) * 200.0);
        Away.z = static_cast<float>(static_cast<double>(Away.z) * 200.0);

        TRefHandle<FObjListDirective> New;                              // EH state 0
        CreateObjListDirective(5, &New.Pointer);
        Vector const Destination = Add(ShipPosition, Away);             // FUN_06f0bb80
        FObjListGotoVec* Goto = static_cast<FObjListGotoVec*>(New.Pointer);
        Goto->SetDestination(&Destination, 100.0f, -1.0f, 0, false, false);   // FUN_06f26460
        Goto->Op.Field_5A = 0;
        Goto->Op.Field_5B = 1;
        Goto->Op.Field_58 = 1;
        Goto->Op.Priority = 1;
        AssignDirective(&Directive.Pointer, New);
        IssueOnlyDirective(Ship, Directive);
        return;
    }

    Vector Center;
    Center.x = ShipPosition.x;
    Center.y = Height;
    Center.z = ShipPosition.z;
    Vector Points[16];
    PickPointsAround(&Center, &MinRange, MaxRange, Points);             // FUN_06f61430

    std::vector<int> Used;
    Used.reserve(16);
    bool bFound = false;
    Vector Destination = {};
    for (unsigned int Attempt = 0; Attempt < 16; ++Attempt)
    {
        int Index = (std::rand() * 16) / 32768;
        for (;;)
        {
            bool bSeen = false;
            for (std::vector<int>::iterator It = Used.begin(); It != Used.end(); ++It)
            {
                if (*It == (Index & 0xf))
                {
                    bSeen = true;
                    break;
                }
            }
            if (!bSeen)
                break;
            ++Index;
        }
        Index &= 0xf;
        Used.push_back(Index);                                          // FUN_06eb5770

        if (Zone != nullptr && !(pub::Zone::ClassifyPoint(*Zone, Points[Index]) < 1.0f))
            continue;
        if (IsSegmentClear(&ShipPosition, &Points[Index], Ship->GetSystem()))
        {
            bFound = true;
            Destination = Points[Index];
            break;
        }
    }

    if (bFound)
    {
        TRefHandle<FObjListDirective> New;                              // EH state 2
        CreateObjListDirective(5, &New.Pointer);
        FObjListGotoVec* Goto = static_cast<FObjListGotoVec*>(New.Pointer);
        Goto->SetDestination(&Destination, 500.0f, -1.0f, 0, false, false);
        Goto->Op.Field_5A = 0;
        Goto->Op.Field_5B = 1;
        Goto->Op.Field_58 = 1;
        Goto->Op.Priority = 1;
        AssignDirective(&Directive.Pointer, New);
        // FCOMP 1500 / TEST AH,5 / JP: skipped when MinRange >= 1500 or NaN.
        if (MinRange < 1500.0f)
        {
            MaxRange = 3000.0f;
            MinRange = 1500.0f;
        }
    }
    else if (MinRange > 100.0f)                                         // TEST AH,0x41 / JNZ
    {
        TRefHandle<FObjListDirective> New;                              // EH state 3
        CreateObjListDirective(1, &New.Pointer);
        FObjListDelay* Delay = static_cast<FObjListDelay*>(New.Pointer);
        Delay->SetDelay(0.5f);
        Delay->Op.Priority = 1;
        AssignDirective(&Directive.Pointer, New);
        MinRange = static_cast<float>(MinRange * 0.5);                  // DAT_06fb74e0 (double)
        MaxRange = static_cast<float>(MaxRange * 0.5);
    }
    else
    {
        MaxRange = 3000.0f;
        MinRange = 1500.0f;
        if (Zone != nullptr)
        {
            Vector const ZonePosition = pub::Zone::GetPos(*Zone);
            Vector Toward;
            Toward.x = ZonePosition.x - ShipPosition.x;
            Toward.y = ZonePosition.y - ShipPosition.y;
            Toward.z = ZonePosition.z - ShipPosition.z;
            double const LengthSquared =
                (static_cast<double>(Toward.z) * Toward.z + static_cast<double>(Toward.y) * Toward.y) +
                static_cast<double>(Toward.x) * Toward.x;
            Vector Target;
            if (!(LengthSquared > 1000000.0))                           // DAT_06fb8570
            {
                Target = ZonePosition;
            }
            else
            {
                double const Inverse = 1.0 / std::sqrt(LengthSquared);
                double const X = Toward.x * Inverse;
                Toward.y = static_cast<float>(Toward.y * Inverse);
                Toward.z = static_cast<float>(Toward.z * Inverse);
                Toward.x = static_cast<float>(X * 1000.0);              // DAT_06fb856c
                Toward.y = static_cast<float>(static_cast<double>(Toward.y) * 1000.0);
                double const Z = static_cast<double>(Toward.z) * 1000.0;
                Toward.z = static_cast<float>(Z);
                Target.x = static_cast<float>(static_cast<double>(ShipPosition.x) + Toward.x);
                Target.y = static_cast<float>(static_cast<double>(Toward.y) + ShipPosition.y);
                Target.z = static_cast<float>(Z + ShipPosition.z);
            }

            TRefHandle<FObjListDirective> New;                          // EH state 4
            CreateObjListDirective(5, &New.Pointer);
            FObjListGotoVec* Goto = static_cast<FObjListGotoVec*>(New.Pointer);
            Goto->SetDestination(&Target, 250.0f, -1.0f, 0, false, false);
            Goto->Op.Field_5A = 0;
            Goto->Op.Field_5B = 1;
            Goto->Op.Field_58 = 1;
            Goto->Op.Priority = 1;
            AssignDirective(&Directive.Pointer, New);
        }
    }

    IssueOnlyDirective(Ship, Directive);
}

// FUN_06f06260 (06f06260-06f062be, RET 0x10; added by CNT-GS9). The class list is copied
// for each FindTargetBase call (FUN_06eb35b0 into the by-value argument).
void FGuideDesc::SetTargets(unsigned int const* Id1, unsigned int const* Id2,
                            std::vector<unsigned int> const* ClassIds, int const* Rep)
{
    TargetId1 = *Id1;
    TargetId2 = *Id2;
    TargetBase1 = FindTargetBase(Id1, *ClassIds, Rep);
    TargetBase2 = FindTargetBase(Id2, *ClassIds, Rep);
}

// FUN_06f06300 (06f06300-06f06526, RET 0x18; added by CNT-GS9).
FContentBase* FGuideDesc::FindTargetBase(unsigned int const* ObjectId,
                                         std::vector<unsigned int> ClassIds, int const* Rep)
{
    (void)ClassIds;                                         // freed at 06f064fd, never read
    unsigned int BaseId = 0;
    unsigned int Type;
    pub::SpaceObj::GetType(*ObjectId, Type);                                    // 06f0633b
    if ((Type & 0x40) != 0)
    {
        // 06f06350-06f064cf: through a jump object, a random base of the destination system
        // that accepts the ship's affiliation (rejected ones leave the list).
        unsigned int SystemId;
        unsigned int JumpTarget;
        pub::SpaceObj::GetJumpTarget(*ObjectId, SystemId, JumpTarget);          // 06f0635d
        unsigned int Bases[0x14];
        unsigned int Count = 0;
        pub::GetBases(SystemId, Bases, 0x14, Count);                            // 06f0637c
        std::vector<unsigned int> Candidates;
        Candidates.reserve(Count);
        for (unsigned int Index = 0; Index < Count; ++Index)
            Candidates.push_back(Bases[Index]);                                 // FUN_06f937c0
        while (!Candidates.empty() && BaseId == 0)
        {
            int const Pick = (rand() * static_cast<int>(Candidates.size())) / 32768;
            if (IsBaseOfAffiliation(Rep, &Candidates[static_cast<size_t>(Pick)]))  // FUN_06f6cb60
                BaseId = Candidates[static_cast<size_t>(Pick)];
            else
                Candidates.erase(Candidates.begin() + Pick);
        }
    }
    else
    {
        pub::SpaceObj::GetDockingTarget(*ObjectId, BaseId);                     // 06f064ea
    }
    return FindContentBaseInfo(&BaseId);                                        // FUN_06eac0c0
}
