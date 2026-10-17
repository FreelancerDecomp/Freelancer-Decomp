// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MissionObject.h"

// TU38 static IDs 06f0e000..06f0e1e0, then "default" 06f0e200.
#include "ContentStaticIds.h"
#include "ContentDefaultId.h"

#include "ContentInternal.h"    // GContentNextUniqueId (DAT_06fce76c)
#include "ContentPlayerState.h"
#include "Guides.h"             // FContentGuide (mission ship slot +0x68)
#include "MissionDialogs.h"     // TU28 GMissionDialogs / FMissionDialog ([Dialog])
#include "MissionObjList.h"     // LoadObjList (TU40 FUN_06f25260), FObjListDirective
#include "PilotPersonalities.h" // GetPilotPersonality (FUN_06f3e020)
#include "SpacePaths.h"         // BuildNnPath (TU53 FUN_06f5ea40)
#include "DockQueues.h"         // PlaceFormationBlocks (TU53 FUN_06f66c80)
#include "Encounters.h"         // FindFormation (TU31 FUN_06efd1a0)
#include "FactionProps.h"       // GetFactionProps (TU48 FUN_06f50650)
#include "NpcNames.h"           // TU42 NPC names (FUN_06f319b0 / 06f319d0 / 06f31910 / 06f31d40)
#include "RandomMissionGen.h"   // FindVoiceProperties (FUN_06fa6e50)
#include "RandomEncounters.h"   // TU31 FRandEncParams / FRandEncPopulator and their factories
#include "SystemConnections.h"  // TU41 FUN_06f2a240 / FUN_06f2a190 / FUN_06f29c10
#include "SystemInfo.h"         // FSystemInfo::SystemId (+0x48)
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h"        // SUBOBJ_ID_NONE
#include "common_newchar.h"     // pub::StateGraph::get_state_graph
#include "common_server.h"
#include "common_server_controller.h"
#include "msvcrt_compat.h"
#include "server_cobject.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <string.h>

// TU38 mission object base (see MissionObject.h). Content.dll.txt addresses.

namespace
{
    // LAB_06f0e240: DAT_06fd0694 = 0, then the TString<0x30> assignment of "Player"
    // (length clamp 0x2f, MOVSD/MOVSB copy, terminator, length into DAT_06fd0690).
    TString<0x30> MakePlayerLabel()
    {
        TString<0x30> Label;
        Label.Data[0] = '\0';
        Label = "Player";
        return Label;
    }
}

// LAB_06f0e220: DAT_06fd06c4 = CreateID("LOCAL_OBJECTIVE"). Read by 06f10a3a, 06f10ab0,
// 06f13b1f, 06f13f12.
extern unsigned int const GIdLocalObjective = CreateID("LOCAL_OBJECTIVE");
// LAB_06f0e240: DAT_06fd0690/DAT_06fd0694 TString<0x30> "Player". Read by 06f12169, 06f125a7,
// 06f129cb, 06f12a08.
extern TString<0x30> const GPlayerLabel = MakePlayerLabel();
// LAB_06f0e290: DAT_06fd068c = CreateID("Mission_Random_Encounter"). Read by 06f120a3,
// 06f121e3, 06f12616, 06f131f4, 06f13c9b, 06f18149.
extern unsigned int const GIdMissionRandomEncounter = CreateID("Mission_Random_Encounter");

// FUN_06f683b0 (06f683b0-06f683d9, cdecl): GetType writes into the first argument's stack
// slot; zero result and a type outside 0x200 -> 1, else 0. Param 2 is never read.
bool __cdecl IsSpaceObjTypeSet(unsigned int ObjId, FSystemInfo* Unused)
{
    (void)Unused;
    unsigned int Type = ObjId;
    if (pub::SpaceObj::GetType(ObjId, Type) != 0)
        return false;
    return (Type & 0xfdff) != 0;
}

// FUN_06f0e3e0 (06f0e3e0-06f0e769, RET 8). Retail order: +4 = *param_1, +8/+0xc/+0x10 = 0,
// +0x18/+0x19 = 0, the +0x1c list, +0x30 = 0, +0x34 = -1.0f, +0x38 ID_String::clear, the +0x3c
// map, the +0x50/+0x68 FmtStr, +0x80 later, +0x84/+0x88 = 0, the containers +0x8c..+0x198, then
// vtable 06fb85d8, +0x14 = 0.0333f (0x3d088889), +0x84 = 0, +0x1ac = 3,
// pub::Controller::SetHeartbeatInterval(+4, 0.0333f), +0x81 = 0, +0x80 = 7.
FMissionObject::FMissionObject(unsigned int const* InControllerId, void const* Parms)
    : ControllerId(*InControllerId)
    , PlayerState(nullptr)
    , PlayerId(0)
    , Reward(0)
    , HeartbeatInterval(0.0333333351f)
    , bTriggerSavePending(0)
    , bPendingDestroy(0)
    , bJumpPending(0)
    , JumpTimer(-1.0f)
    , Field_0x80(7)
    , bDestroyed(0)
    , Field_0x84(0.0f)
    , Field_0x88(0)
    , JumpDelay(3)
{
    (void)Parms;
    Field_0x38.clear();
    pub::Controller::SetHeartbeatInterval(ControllerId, HeartbeatInterval);
}

// FUN_06f0e770 -> FUN_06f0ed00. The vtable is reset to 06fb85d8 first, so the slot calls below
// are this class's bodies. The container and FmtStr teardown that follows (06f0ede3-) is done
// by the members' destructors.
FMissionObject::~FMissionObject()
{
    // 06f0ed28-06f0ed53: +0x98 erased (FUN_06ed0470 begin..end).
    RandEncs.clear();
    // 06f0ed58-06f0edaf: while the first +0x3c record is non-null, slot 4 of slot 3 of its id.
    while (!Players.empty() && Players.begin()->second != nullptr)
        FMissionObject::RemovePlayer(FMissionObject::FindPlayer(Players.begin()->second->PlayerId));
    // 06f0edb1-06f0ede1: a jumper with an NPC and a state other than 0 (out of every system)
    // is deleted (vslot 0 with 1) and both dwords cleared.
    std::list<FMissionJumper>::iterator It = Jumpers.begin();
    for (; It != Jumpers.end(); ++It)
    {
        if (It->Npc == nullptr || It->State == 0)
            continue;
        delete It->Npc;
        It->Npc = nullptr;
        It->State = 0;
    }
}

// FUN_06f0f100 (06f0f100-06f0f141, RET 8): a destroyed mission (+0x81) ignores every
// message; otherwise, unless +0x19 is already set, slot 21 (vtbl[+0x54]) handles it.
// If +0x19 is set before or after that, pub::Controller::Destroy(+4) and +0x81 = 1.
void FMissionObject::HandleMessage(pub::Controller::EMessage MessageId, void const* MessageParms)
{
    if (bDestroyed != 0)
        return;
    if (bPendingDestroy == 0)
    {
        HandleMissionMessage(MessageId, MessageParms);
        if (bPendingDestroy == 0)
            return;
    }
    pub::Controller::Destroy(ControllerId);
    bDestroyed = 1;
}

// LAB_06ea1450: XOR EAX,EAX; RET.
int FMissionObject::Slot07() { return 0; }
int FMissionObject::Slot08() { return 0; }
int FMissionObject::Slot09() { return 0; }
int FMissionObject::Slot10() { return 0; }

namespace
{
    // The position triple of a NavMarker (+8..+0x13 of FmtStr::NavMarker).
    Vector GetMarkerPosition(FmtStr::NavMarker const& Marker)
    {
        Vector Position;
        std::memcpy(&Position.x, &Marker.Data[2], sizeof(float));
        std::memcpy(&Position.y, &Marker.Data[3], sizeof(float));
        std::memcpy(&Position.z, &Marker.Data[4], sizeof(float));
        return Position;
    }

    // The type 5 objective fields slot 11 writes (06f1bc89-06f1bcb0 / 06f1bd4d-06f1bd77 /
    // 06f1bdc1-06f1bde8): +0x38 reputation, +0x3c the connection's system, marker +0x40 the
    // current system, +0x44 = 1, +0x48 the point.
    void FillPathObjective(FNNObjectiveType5* Objective, int RepInstance, unsigned int GotoSystemId,
                           unsigned int CurrentSystemId, Vector const& Point)
    {
        Objective->RepInstance = RepInstance;
        Objective->SystemId = GotoSystemId;
        std::memcpy(&Objective->Marker.Data[2], &Point.x, sizeof(float));
        std::memcpy(&Objective->Marker.Data[3], &Point.y, sizeof(float));
        std::memcpy(&Objective->Marker.Data[4], &Point.z, sizeof(float));
        Objective->Marker.Data[1] = 1;
        Objective->Marker.Data[0] = CurrentSystemId;
    }

    // One axis of the slot 11 "leg unchanged" test (06f1bada-06f1bb3a): (Point - Marker)^2 against
    // 1.0f (DAT_06fb5810) in x87 precision; FCOMP + TEST AH,0x41 + JP goes on (greater or
    // unordered), so only "<=" counts as close.
    bool IsAxisClose(float Point, float Marker)
    {
        double const Delta = static_cast<double>(Point) - static_cast<double>(Marker);
        return Delta * Delta <= 1.0;
    }
}

// LAB_06f1b9f0 (06f1b9f0-06f1be13, thiscall RET 0x2c, returns AL).
// 06f1b9fd-06f1ba22: First = the first List node whose slot 4 is 5, or end.
// Legs empty (06f1ba30): First == end -> false (06f1ba34, AL = BL = 0); else the run of type 5
// nodes from First is deleted (FUN_06f1a610 + List vslot 3 per node, 06f1ba40-06f1ba9d; the null
// test on the run end at 06f1ba54 cannot fire, the end is the list head) -> true.
// Legs non-empty: when First is a type 5 node, the first leg's point (kind 1 -> PointB, else
// PointA; 06f1bac4-06f1bad7) is compared with First's marker position; all three axes close ->
// false with the list untouched (06f1bb3a). Otherwise the type 5 run is deleted (06f1bb40-
// 06f1bb8b). Ins = *Where, or List's first node (06f1bb91-06f1bba6), read after the erase.
// GotoSystemId = System->SystemId, replaced by FUN_06f2a240(System, Target) when Target is set
// and that succeeds (06f1bbaa-06f1bbd8). Per leg (06f1bbf0-06f1bdf8): a new type 5 objective of
// this mission inserted before Ins (FUN_06f19cd0 + FUN_06f1e390); kind 0 -> Type/Ids = the
// first leg ? FirstType/FirstIds : LegType/LegIds, rep of ObjectA (GetRep, -1 default), PointA,
// then a second type 5 objective inserted before Ins (FUN_06f93790 + FUN_06eb6740 inline) with
// LegType/LegIds, rep of ObjectB, PointB; kind 1 -> TargetType/TargetIds, rep of Target (only
// when Target is set), PointB; any other kind keeps the default objective. -> true.
bool FMissionObject::BuildPathObjectives(FSystemInfo* System, std::list<FNnPathLeg>* Legs,
                                         FMissionObjectiveList* List, int FirstIds, int LegIds,
                                         int TargetIds, unsigned int TargetObjectId, int FirstType,
                                         int LegType, int TargetType,
                                         std::list<FMissionObjective*>::iterator const* Where)
{
    std::list<FMissionObjective*>& Nodes = List->Objectives;
    std::list<FMissionObjective*>::iterator First = Nodes.begin();
    for (; First != Nodes.end(); ++First)
    {
        if ((*First)->GetType() == 5)
            break;
    }

    if (Legs->size() == 0)
    {
        if (First == Nodes.end())
            return false;
        std::list<FMissionObjective*>::iterator RunEnd = First;
        while (RunEnd != Nodes.end() && (*RunEnd)->GetType() == 5)
            ++RunEnd;
        List->EraseRange(First, RunEnd);
        return true;
    }

    if (First != Nodes.end() && (*First)->GetType() == 5)
    {
        FNnPathLeg const& FirstLeg = Legs->front();
        Vector const& Point = FirstLeg.Kind == 1 ? FirstLeg.PointB : FirstLeg.PointA;
        Vector const MarkerPosition =
            GetMarkerPosition(static_cast<FNNObjectiveType5*>(*First)->Marker);
        if (IsAxisClose(Point.x, MarkerPosition.x) && IsAxisClose(Point.y, MarkerPosition.y) &&
            IsAxisClose(Point.z, MarkerPosition.z))
        {
            return false;
        }
        std::list<FMissionObjective*>::iterator RunEnd = First;
        while (RunEnd != Nodes.end() && (*RunEnd)->GetType() == 5)
            ++RunEnd;
        List->EraseRange(First, RunEnd);
    }

    std::list<FMissionObjective*>::iterator Ins = Where != nullptr ? *Where : Nodes.begin();

    unsigned int GotoSystemId = System->SystemId;
    if (TargetObjectId != 0)
    {
        if (GetConnectionGotoSystem(System->SystemId, TargetObjectId, &GotoSystemId) != 0)
            GotoSystemId = System->SystemId;
    }

    std::list<FNnPathLeg>::iterator LegIt = Legs->begin();
    for (; LegIt != Legs->end(); ++LegIt)
    {
        FMissionObjective* Created = nullptr;
        if (CreateMissionObjective(5, this, &Created))
            Nodes.insert(Ins, Created);
        FNNObjectiveType5* Objective = static_cast<FNNObjectiveType5*>(Created);

        if (LegIt->Kind == 0)
        {
            if (LegIt == Legs->begin())
            {
                Objective->Objective.Type = static_cast<unsigned int>(FirstType);
                Objective->Ids = FirstIds;
            }
            else
            {
                Objective->Objective.Type = static_cast<unsigned int>(LegType);
                Objective->Ids = LegIds;
            }
            int RepA = -1;
            pub::SpaceObj::GetRep(LegIt->ObjectA, RepA);
            FillPathObjective(Objective, RepA, GotoSystemId, System->SystemId, LegIt->PointA);

            int RepB = -1;
            pub::SpaceObj::GetRep(LegIt->ObjectB, RepB);
            FMissionObjective* CreatedB = nullptr;
            if (CreateMissionObjective(5, this, &CreatedB))
                Nodes.insert(Ins, CreatedB);
            FNNObjectiveType5* ObjectiveB = static_cast<FNNObjectiveType5*>(CreatedB);
            ObjectiveB->Objective.Type = static_cast<unsigned int>(LegType);
            ObjectiveB->Ids = LegIds;
            FillPathObjective(ObjectiveB, RepB, GotoSystemId, System->SystemId, LegIt->PointB);
        }
        else if (LegIt->Kind == 1)
        {
            int Rep = -1;
            if (TargetObjectId != 0)
                pub::SpaceObj::GetRep(TargetObjectId, Rep);
            Objective->Ids = TargetIds;
            Objective->Objective.Type = static_cast<unsigned int>(TargetType);
            FillPathObjective(Objective, Rep, GotoSystemId, System->SystemId, LegIt->PointB);
        }
    }
    return true;
}

// LAB_06ea1460: MOV AL,1; RET.
bool FMissionObject::Slot12() { return true; }
// LAB_06ea1470: XOR AL,AL; RET 4.
bool FMissionObject::Slot13(unsigned int Arg) { (void)Arg; return false; }
bool FMissionObject::Slot14() { return true; }
// LAB_06ea1480: XOR AL,AL; RET.
bool FMissionObject::Slot15() { return false; }
bool FMissionObject::Slot16(unsigned int Arg) { (void)Arg; return false; }
bool FMissionObject::Slot17() { return true; }

// ---------------------------------------------------------------------------------------
// FMissionObjective (base, vtable 06fb8ccc)
// ---------------------------------------------------------------------------------------

// FUN_06f19f90 (base part, 06f19f90-06f1a00e).
FMissionObjective::FMissionObjective(FMissionObject* InMission)
    : bHidden(0)
    , Ids(0)
    , Index(-1)
    , Mission(InMission)
    , PlayerState(nullptr)
{
    Nickname.clear();
    // The inline MissionObjective ctor (FUN_06f1a5d0 shape, 06f19fb6-06f19fea) sets Type 1 and
    // zeroes the FmtStr; after the vtable store Type = 4.
    Objective.Type = 4;
    if (Mission != nullptr)
        PlayerState = Mission->PlayerState;
}

// Base part of the slot 5 clones (06f1a044-06f1a07b in FUN_06f1a010, identical in the other
// seven): byte +4, dwords +8..+0x1c, then FmtStr::FmtStr(const FmtStr&) on +0x20.
FMissionObjective::FMissionObjective(FMissionObjective const& Other)
    : bHidden(Other.bHidden)
    , Nickname(Other.Nickname)
    , Ids(Other.Ids)
    , Index(Other.Index)
    , Mission(Other.Mission)
    , PlayerState(Other.PlayerState)
    , Objective(Other.Objective)
{
}

// FUN_06f1a5c0: vtable reset, ~FmtStr at +0x20 (member destructor).
FMissionObjective::~FMissionObjective()
{
}

// FUN_06f1a980 (06f1a980-06f1a9ba, RET 4).
bool FMissionObjective::IsSame(FMissionObjective* Other)
{
    int OtherType = Other->GetType();
    if (GetType() != OtherType)
        return false;
    return Objective.Description == Other->Objective.Description;
}

// FUN_06f1d430 (06f1d430-06f1d451, cdecl): null or empty -> 0; CMP '0' / JL -> '-' test;
// CMP '9' / JLE -> 1; else '-' -> 1. Every objective parser inlines the same test (e.g.
// 06f1aa4a-06f1aa5e) on its TString copy.
bool __cdecl IsNumberText(char const* Text)
{
    if (Text == nullptr)
        return false;
    char const Lead = Text[0];
    if (Lead == '\0')
        return false;
    if (Lead >= '0' && Lead <= '9')
        return true;
    return Lead == '-';
}

namespace
{

    // FUN_06ea9f40 copy of value Index into a TString<0x40> (null -> empty), as the parsers do
    // before the number test.
    void CopyValue(TString<0x40>& Out, INI_Reader* Reader, unsigned int ValueIndex)
    {
        Out.Length = 0;
        Out.Data[0] = '\0';
        Out = Reader->get_value_string(ValueIndex);
    }

    // FmtStr::destroy followed by REP STOSD of the six dwords (e.g. 06f1aac0-06f1aacf).
    void DestroyAndZero(FmtStr& Text)
    {
        Text.destroy();
        std::memset(&Text, 0, sizeof(Text));
    }

    // Slot 1 of types 5 and 6 (LAB_06f1b030, RET 4). The position is read 7, 8, 6 and stored
    // +0x48 = value 8, +0x4c = value 7, +0x50 = value 6 (06f1b06c-06f1b0b5, raw bytes checked),
    // the reverse of the x, y, z order slot 2 writes; retail reloads such a save swapped.
    void ParseType56(FMissionObjective& Objective, INI_Reader* Reader, int& RepInstance,
                     unsigned int& SystemId, FmtStr::NavMarker& Marker)
    {
        Objective.Ids = Reader->get_value_int(1);
        SystemId = static_cast<unsigned int>(Reader->get_value_int(2));
        RepInstance = Reader->get_value_int(3);
        Marker.Data[1] = static_cast<unsigned int>(Reader->get_value_int(4));
        Marker.Data[0] = static_cast<unsigned int>(Reader->get_value_int(5));
        float const Value7 = Reader->get_value_float(7);
        float const Value8 = Reader->get_value_float(8);
        float const Value6 = Reader->get_value_float(6);
        std::memcpy(&Marker.Data[2], &Value8, sizeof(float));
        std::memcpy(&Marker.Data[3], &Value7, sizeof(float));
        std::memcpy(&Marker.Data[4], &Value6, sizeof(float));
    }

    // Slot 2 of types 5 and 6 (LAB_06f1b0d0, RET 4).
    void SaveType56(FMissionObjective const& Objective, FILE* File, int RepInstance,
                    unsigned int SystemId, FmtStr::NavMarker const& Marker)
    {
        fprintf(File, "%d, ", Objective.Ids);
        fprintf(File, "%u, ", SystemId);
        fprintf(File, "%u, ", static_cast<unsigned int>(RepInstance));
        fprintf(File, "%d, ", static_cast<int>(Marker.Data[1]));
        fprintf(File, "%u, ", Marker.Data[0]);
        float X;
        float Y;
        float Z;
        std::memcpy(&X, &Marker.Data[2], sizeof(float));
        std::memcpy(&Y, &Marker.Data[3], sizeof(float));
        std::memcpy(&Z, &Marker.Data[4], sizeof(float));
        fprintf(File, "%f, %f, %f ", static_cast<double>(X), static_cast<double>(Y),
                static_cast<double>(Z));
        fprintf(File, "END");
    }

    // Slot 3 of types 5 and 6 (LAB_06f1b150): system, then the rep instance when the mission's
    // player state exists, this objective's cached state has a ship and +0x38 != -1, then the
    // nav marker.
    void BuildType56(FMissionObjective& Objective, int const& RepInstance,
                     unsigned int const& SystemId, FmtStr::NavMarker const& Marker)
    {
        FmtStr& Text = Objective.Objective.Description;
        Text.destroy();
        DestroyAndZero(Text);
        Text.begin_mad_lib(static_cast<unsigned int>(Objective.Ids));
        Text.append_system(SystemId);
        if (FindContentPlayerState(Objective.Mission->PlayerId) != nullptr &&
            Objective.PlayerState->ShipObject != nullptr && RepInstance != -1)
            Text.append_rep_instance(RepInstance);
        Text.append_nav_marker(Marker);
        Text.end_mad_lib();
    }

    // Slots 1 of navmarker/rep_inst (06f1ab0d-06f1abe3 / 06f1ad43-06f1ae2b): value 1 is a
    // system id number or a system nickname (pub::GetSystemID on the copied text).
    void ParseSystemValue(unsigned int& SystemId, INI_Reader* Reader)
    {
        TString<0x40> Text;
        CopyValue(Text, Reader, 1);
        if (IsNumberText(Text.Data))
            SystemId = static_cast<unsigned int>(Reader->get_value_int(1));
        else
            pub::GetSystemID(SystemId, Text.Data);
    }

    // Values 4, 5, 6 read 5, 4, 6 and stored x = 4, y = 5, z = 6 (06f1ab52-06f1ab9d).
    void ParsePosition(unsigned int* Out, INI_Reader* Reader)
    {
        float const Value5 = Reader->get_value_float(5);
        float const Value4 = Reader->get_value_float(4);
        float const Value6 = Reader->get_value_float(6);
        std::memcpy(&Out[0], &Value4, sizeof(float));
        std::memcpy(&Out[1], &Value5, sizeof(float));
        std::memcpy(&Out[2], &Value6, sizeof(float));
    }

    void SavePosition(FILE* File, unsigned int const* In, char const* Format)
    {
        float X;
        float Y;
        float Z;
        std::memcpy(&X, &In[0], sizeof(float));
        std::memcpy(&Y, &In[1], sizeof(float));
        std::memcpy(&Z, &In[2], sizeof(float));
        fprintf(File, Format, static_cast<double>(X), static_cast<double>(Y),
                static_cast<double>(Z));
    }
}

// ---------------------------------------------------------------------------------------
// Type 1 "base" (vtable 06fb8d20)
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0 case 1 (06f19d04-06f19d32).
FNNObjectiveBase::FNNObjectiveBase(FMissionObject* InMission)
    : FMissionObjective(InMission)
{
}

// LAB_06f1a9c0 (RET 4): value 1 is a base id number or a base nickname (pub::GetBaseID on the
// copied text); value 2 is the ids.
void FNNObjectiveBase::Parse(INI_Reader* Reader)
{
    TString<0x40> Text;
    CopyValue(Text, Reader, 1);
    if (IsNumberText(Text.Data))
        BaseId = static_cast<unsigned int>(Reader->get_value_int(1));
    else
        pub::GetBaseID(BaseId, Text.Data);
    Ids = Reader->get_value_int(2);
}

// LAB_06f1aa80 (RET 4): "%u, " (06fb872c) base, "%d" (06fb8dc0) ids.
void FNNObjectiveBase::Save(FILE* File)
{
    fprintf(File, "%u, ", BaseId);
    fprintf(File, "%d", Ids);
}

// FUN_06f1aab0: destroy twice, zero, begin_mad_lib(ids), append_base(+0x38), end_mad_lib.
bool FNNObjectiveBase::BuildDescription()
{
    FmtStr& Text = Objective.Description;
    Text.destroy();
    DestroyAndZero(Text);
    Text.begin_mad_lib(static_cast<unsigned int>(Ids));
    Text.append_base(BaseId);
    Text.end_mad_lib();
    return true;
}

// LAB_06ee9e10: MOV EAX,1.
int FNNObjectiveBase::GetType() { return 1; }

// FUN_06f1a010: new(0x3c), base copy, +0x38.
FMissionObjective* FNNObjectiveBase::Clone() { return new FNNObjectiveBase(*this); }

// ---------------------------------------------------------------------------------------
// Type 2 "navmarker" (vtable 06fb8d04)
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0 case 2 (06f19d37-06f19d69).
FNNObjectiveNavMarker::FNNObjectiveNavMarker(FMissionObject* InMission)
    : FMissionObjective(InMission)
{
}

// LAB_06f1ab00 (RET 4): system +0x38, ids (2), +0x3c (3), position +0x40 (4, 5, 6).
void FNNObjectiveNavMarker::Parse(INI_Reader* Reader)
{
    ParseSystemValue(Marker.Data[0], Reader);
    Ids = Reader->get_value_int(2);
    Marker.Data[1] = static_cast<unsigned int>(Reader->get_value_int(3));
    ParsePosition(&Marker.Data[2], Reader);
}

// LAB_06f1abf0 (RET 4): "%u, " system, "%d, " ids, "%d, " +0x3c, "%f, %f, %f".
void FNNObjectiveNavMarker::Save(FILE* File)
{
    fprintf(File, "%u, ", Marker.Data[0]);
    fprintf(File, "%d, ", Ids);
    fprintf(File, "%d, ", static_cast<int>(Marker.Data[1]));
    SavePosition(File, &Marker.Data[2], "%f, %f, %f");
}

// LAB_06f1ac50: destroy twice, zero, begin_mad_lib(ids), append_nav_marker(+0x38), end.
bool FNNObjectiveNavMarker::BuildDescription()
{
    FmtStr& Text = Objective.Description;
    Text.destroy();
    DestroyAndZero(Text);
    Text.begin_mad_lib(static_cast<unsigned int>(Ids));
    Text.append_nav_marker(Marker);
    Text.end_mad_lib();
    return true;
}

// LAB_06ee9660: MOV EAX,2.
int FNNObjectiveNavMarker::GetType() { return 2; }

// FUN_06f1a0a0: new(0x4c), base copy, 5 dwords from +0x38.
FMissionObjective* FNNObjectiveNavMarker::Clone() { return new FNNObjectiveNavMarker(*this); }

// ---------------------------------------------------------------------------------------
// Type 3 "ids" (vtable 06fb8ce8)
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0 case 3 (06f19d6e-06f19da0).
FNNObjectiveIds::FNNObjectiveIds(FMissionObject* InMission)
    : FMissionObjective(InMission)
{
}

// FUN_06f1aca0 (RET 4): ids = value 1.
void FNNObjectiveIds::Parse(INI_Reader* Reader)
{
    Ids = Reader->get_value_int(1);
}

// LAB_06f1acc0 (RET 4): "%d" ids.
void FNNObjectiveIds::Save(FILE* File)
{
    fprintf(File, "%d", Ids);
}

// LAB_06f1ace0: destroy twice, zero, begin_mad_lib(ids), append_string(ids), end.
bool FNNObjectiveIds::BuildDescription()
{
    FmtStr& Text = Objective.Description;
    Text.destroy();
    DestroyAndZero(Text);
    Text.begin_mad_lib(static_cast<unsigned int>(Ids));
    Text.append_string(static_cast<unsigned int>(Ids));
    Text.end_mad_lib();
    return true;
}

// LAB_06f5e540: MOV EAX,3.
int FNNObjectiveIds::GetType() { return 3; }

// FUN_06f1a140: new(0x38), base copy.
FMissionObjective* FNNObjectiveIds::Clone() { return new FNNObjectiveIds(*this); }

// ---------------------------------------------------------------------------------------
// Type 4 "rep_inst" (vtable 06fb8cb0)
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0 case 4 (06f19da5-06f19e17): the base ctor inlined, then +0x38 = 0 and
// byte +0x3c = 0.
FNNObjectiveRepInst::FNNObjectiveRepInst(FMissionObject* InMission)
    : FMissionObjective(InMission)
{
    Label.Length = 0;
    Label.Data[0] = '\0';
}

// LAB_06f1ad30 (RET 4): system +0x6c, ids (2), +0x70 (3), position +0x74 (4, 5, 6), then the
// label (7): null -> empty, else strlen clamped to 0x2f, copied, terminated (06f1add6-06f1ae63).
void FNNObjectiveRepInst::Parse(INI_Reader* Reader)
{
    ParseSystemValue(Marker.Data[0], Reader);
    Ids = Reader->get_value_int(2);
    Marker.Data[1] = static_cast<unsigned int>(Reader->get_value_int(3));
    ParsePosition(&Marker.Data[2], Reader);
    Label = Reader->get_value_string(7);
}

// LAB_06f1ae70 (RET 4): "%u, " system, "%d, " ids, "%d, " +0x70, "%f, %f, %f, ", "%s" label.
void FNNObjectiveRepInst::Save(FILE* File)
{
    fprintf(File, "%u, ", Marker.Data[0]);
    fprintf(File, "%d, ", Ids);
    fprintf(File, "%d, ", static_cast<int>(Marker.Data[1]));
    SavePosition(File, &Marker.Data[2], "%f, %f, %f, ");
    fprintf(File, "%s", Label.Data);
}

// LAB_06f1aee0: the label's object id (06f1aee7-06f1afac is FUN_06f15b90 inlined: ship, the
// +0xd0 loot loop of FUN_06f15b10, solar), its reputation (-1 when there is no object or
// pub::SpaceObj::GetRep fails), then destroy twice, zero, begin_mad_lib(ids),
// append_rep_instance, append_nav_marker(+0x6c), end.
bool FNNObjectiveRepInst::BuildDescription()
{
    unsigned int ObjId = Mission->GetLabelObjectId(&Label);
    int Rep = -1;
    if ((ObjId & 0x3fffffff) != 0 && pub::SpaceObj::GetRep(ObjId, Rep) != 0)
        Rep = -1;
    FmtStr& Text = Objective.Description;
    Text.destroy();
    DestroyAndZero(Text);
    Text.begin_mad_lib(static_cast<unsigned int>(Ids));
    Text.append_rep_instance(Rep);
    Text.append_nav_marker(Marker);
    Text.end_mad_lib();
    return true;
}

// LAB_06f25080: MOV EAX,4.
int FNNObjectiveRepInst::GetType() { return 4; }

// FUN_06f1a1d0: new(0x80), base copy, 0xd dwords from +0x38, 5 dwords from +0x6c.
FMissionObjective* FNNObjectiveRepInst::Clone() { return new FNNObjectiveRepInst(*this); }

// ---------------------------------------------------------------------------------------
// Types 5 and 6 (vtables 06fb8c94 / 06fb8c78)
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0 case 5 (06f19e1c-06f19e58).
FNNObjectiveType5::FNNObjectiveType5(FMissionObject* InMission)
    : FMissionObjective(InMission)
    , RepInstance(-1)
    , SystemId(0)
{
}

void FNNObjectiveType5::Parse(INI_Reader* Reader) { ParseType56(*this, Reader, RepInstance, SystemId, Marker); }
void FNNObjectiveType5::Save(FILE* File) { SaveType56(*this, File, RepInstance, SystemId, Marker); }
bool FNNObjectiveType5::BuildDescription() { BuildType56(*this, RepInstance, SystemId, Marker); return true; }
// LAB_06ee99a0: MOV EAX,5.
int FNNObjectiveType5::GetType() { return 5; }
// FUN_06f1a280: new(0x54), base copy, +0x38, +0x3c, 5 dwords from +0x40.
FMissionObjective* FNNObjectiveType5::Clone() { return new FNNObjectiveType5(*this); }

// FUN_06f19cd0 case 6 (06f19e5d-06f19e99).
FNNObjectiveType6::FNNObjectiveType6(FMissionObject* InMission)
    : FMissionObjective(InMission)
    , RepInstance(-1)
    , SystemId(0)
{
}

void FNNObjectiveType6::Parse(INI_Reader* Reader) { ParseType56(*this, Reader, RepInstance, SystemId, Marker); }
void FNNObjectiveType6::Save(FILE* File) { SaveType56(*this, File, RepInstance, SystemId, Marker); }
bool FNNObjectiveType6::BuildDescription() { BuildType56(*this, RepInstance, SystemId, Marker); return true; }
// LAB_06f250c0: MOV EAX,6.
int FNNObjectiveType6::GetType() { return 6; }
// FUN_06f1a330: new(0x54), base copy, +0x38, +0x3c, 5 dwords from +0x40.
FMissionObjective* FNNObjectiveType6::Clone() { return new FNNObjectiveType6(*this); }

// ---------------------------------------------------------------------------------------
// Type 7 (vtable 06fb8c5c)
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0 case 7 (06f19e9e-06f19f0f): base ctor inlined, FmtStr inline ctor (FUN_06eac680)
// at +0x38.
FNNObjectiveType7::FNNObjectiveType7(FMissionObject* InMission)
    : FMissionObjective(InMission)
{
}

// FUN_06f1a560 (deleting form 06f1a540): ~FmtStr +0x38, vtable reset, ~FmtStr +0x20.
FNNObjectiveType7::~FNNObjectiveType7()
{
}

// FUN_06ebf6f0: RET 4.
void FNNObjectiveType7::Parse(INI_Reader* Reader) { (void)Reader; }
void FNNObjectiveType7::Save(FILE* File) { (void)File; }

// LAB_06f1b1e0: destroy once, zero, FmtStr::operator=(+0x38).
bool FNNObjectiveType7::BuildDescription()
{
    DestroyAndZero(Objective.Description);
    Objective.Description = Text;
    return true;
}

// LAB_06f250d0: MOV EAX,7.
int FNNObjectiveType7::GetType() { return 7; }

// FUN_06f1a3e0: new(0x50), base copy, FmtStr copy ctor on +0x38.
FMissionObjective* FNNObjectiveType7::Clone() { return new FNNObjectiveType7(*this); }

// ---------------------------------------------------------------------------------------
// Type 8 (vtable 06fb8c40)
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0 case 8 (06f19f11-06f19f3f).
FNNObjectiveType8::FNNObjectiveType8(FMissionObject* InMission)
    : FMissionObjective(InMission)
{
}

// FUN_06ebf6f0: RET 4.
void FNNObjectiveType8::Parse(INI_Reader* Reader) { (void)Reader; }
void FNNObjectiveType8::Save(FILE* File) { (void)File; }

// LAB_06f1b210: destroy once, zero, begin_mad_lib(ids), append_int(+0x38), end.
bool FNNObjectiveType8::BuildDescription()
{
    FmtStr& Description = Objective.Description;
    DestroyAndZero(Description);
    Description.begin_mad_lib(static_cast<unsigned int>(Ids));
    Description.append_int(Value);
    Description.end_mad_lib();
    return true;
}

// LAB_06f1a480: MOV EAX,8.
int FNNObjectiveType8::GetType() { return 8; }

// FUN_06f1a490: new(0x3c), base copy, +0x38.
FMissionObjective* FNNObjectiveType8::Clone() { return new FNNObjectiveType8(*this); }

// ---------------------------------------------------------------------------------------
// Factory, [NNObjective] load/save
// ---------------------------------------------------------------------------------------

// FUN_06f19cd0 (06f19cd0-06f19f62, cdecl). The jump table covers 1..8; any other type skips
// the store (caseD_8 at 06f19f46) and returns whether *Out is already set.
bool __cdecl CreateMissionObjective(int Type, FMissionObject* Mission, FMissionObjective** Out)
{
    switch (Type)
    {
    case 1:
        *Out = new FNNObjectiveBase(Mission);
        break;
    case 2:
        *Out = new FNNObjectiveNavMarker(Mission);
        break;
    case 3:
        *Out = new FNNObjectiveIds(Mission);
        break;
    case 4:
        *Out = new FNNObjectiveRepInst(Mission);
        break;
    case 5:
        *Out = new FNNObjectiveType5(Mission);
        break;
    case 6:
        *Out = new FNNObjectiveType6(Mission);
        break;
    case 7:
        *Out = new FNNObjectiveType7(Mission);
        break;
    case 8:
        *Out = new FNNObjectiveType8(Mission);
        break;
    default:
        break;
    }
    return *Out != nullptr;
}

// FUN_06f1a610 (06f1a610-06f1a62a, cdecl).
bool __cdecl DeleteMissionObjective(FMissionObjective** Objective)
{
    if (*Objective != nullptr)
    {
        delete *Objective;
        *Objective = nullptr;
    }
    return true;
}

// FUN_06f1a630 (06f1a630-06f1a8de, cdecl). Keys:
//   nickname: atoi of value 0, or CreateID of it when that is 0;
//   type: value 0 a number (digit or '-' first) -> that type, 0 skips; else "navmarker" 2,
//         "base" 1, "ids" 3, "rep_inst" 4, anything else skips. The factory result is not
//         tested: slot 1 is called on it (a type outside 1..8 before any valid one calls
//         through null, as retail does);
//   state: a number -> that value; else 10, and "ACTIVE" clears / "HIDDEN" sets hidden;
//   hidden: get_value_bool.
// After the section an objective gets nickname (+8), state (+0x1c) and hidden (+4) and is
// appended to List (inline push_back 06f1a8a5-06f1a8d1).
void __cdecl LoadNNObjective(INI_Reader* Reader, FMissionObject* Mission, FMissionObjectiveList* List)
{
    ID_String NicknameId;
    NicknameId.clear();
    unsigned char bHiddenValue = 0;
    FMissionObjective* Objective = nullptr;
    // Retail never initialises this local ([ESP+0x1c]); every [NNObjective] in the shipped
    // DATA (353 sections) has a state key, so the port's 0 is never observed.
    int State = 0;

    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            // 06f1a689 imports MSVCRT atoi: saved unsigned hash IDs keep all 32 bits.
            // UCRT atoi saturates IDs above INT_MAX and breaks later Act_SetNNObj lookup.
            NicknameId.Id = static_cast<unsigned int>(MsvcrtAtoi(Reader->get_value_string(0)));
            if (NicknameId.Id == 0)
                NicknameId.Id = CreateID(Reader->get_value_string(0));
        }
        else if (Reader->is_value("type"))
        {
            TString<0x40> Text;
            CopyValue(Text, Reader, 0);
            int Type = 0;
            if (IsNumberText(Text.Data))
            {
                Type = Reader->get_value_int(0);
                if (Type == 0)
                    continue;
            }
            else if (_stricmp(Reader->get_value_string(0), "navmarker") == 0)
                Type = 2;
            else if (_stricmp(Reader->get_value_string(0), "base") == 0)
                Type = 1;
            else if (_stricmp(Reader->get_value_string(0), "ids") == 0)
                Type = 3;
            else if (_stricmp(Reader->get_value_string(0), "rep_inst") == 0)
                Type = 4;
            else
                continue;
            CreateMissionObjective(Type, Mission, &Objective);
            Objective->Parse(Reader);
        }
        else if (Reader->is_value("state"))
        {
            TString<0x40> Text;
            CopyValue(Text, Reader, 0);
            if (IsNumberText(Text.Data))
            {
                State = Reader->get_value_int(0);
            }
            else
            {
                State = 10;
                if (_stricmp("ACTIVE", Reader->get_value_string(0)) == 0)
                    bHiddenValue = 0;
                else if (_stricmp("HIDDEN", Reader->get_value_string(0)) == 0)
                    bHiddenValue = 1;
            }
        }
        else if (Reader->is_value("hidden"))
        {
            bHiddenValue = Reader->get_value_bool(0) ? 1 : 0;
        }
    }

    if (Objective == nullptr)
        return;
    Objective->Nickname = NicknameId;
    Objective->Objective.Type = static_cast<unsigned int>(State);
    Objective->bHidden = bHiddenValue;
    List->Objectives.push_back(Objective);
}

// FUN_06f1a8e0 (06f1a8e0-06f1a97c, cdecl).
void __cdecl SaveNNObjectives(FILE* File, FMissionObjectiveList* List)
{
    std::list<FMissionObjective*>::iterator It = List->Objectives.begin();
    for (; It != List->Objectives.end(); ++It)
    {
        FMissionObjective* Objective = *It;
        fprintf(File, "[NNObjective]\n");
        fprintf(File, "nickname = %u\n", Objective->Nickname.Id);
        fprintf(File, "type = %d, ", Objective->GetType());
        Objective->Save(File);
        fprintf(File, "\n");
        fprintf(File, "state = %d\n", static_cast<int>(Objective->Objective.Type));
        fprintf(File, "hidden = %d\n", Objective->bHidden != 0 ? 1 : 0);
        fprintf(File, "\n");
    }
}

// ---------------------------------------------------------------------------------------
// FMissionObjectiveList (vtables 06fb479c / 06fb478c)
// ---------------------------------------------------------------------------------------

// FUN_06f1ca70: empty list (then the derived vtable store by FMissionOwnedObjectiveList's ctor).
FMissionObjectiveList::FMissionObjectiveList()
{
}

// 06f1cf09-06f1cf23 (inside FUN_06f1cee0): FUN_06f1ca70, then FUN_06f1caa0(Other).
FMissionObjectiveList::FMissionObjectiveList(FMissionObjectiveList const& Other)
{
    *this = Other;
}

// 06eb7900: FUN_06f1cb40 (every element deleted), then the nodes and the head freed.
FMissionObjectiveList::~FMissionObjectiveList()
{
    Clear();
}

// FUN_06f1cb40: for every node, delete the element when non-null (vslot 0 with 1), then erase
// the node (vslot 3 = 06eb7850).
void FMissionObjectiveList::Clear()
{
    std::list<FMissionObjective*>::iterator It = Objectives.begin();
    while (It != Objectives.end())
    {
        if (*It != nullptr)
            delete *It;
        It = Objectives.erase(It);
    }
}

// FUN_06f1caa0 (06f1caa0-06f1cb36, RET 4): the FUN_06f1cb40 loop inlined, then an inline
// push_back of Other's elements' slot 5 in order. Returns this.
FMissionObjectiveList& FMissionObjectiveList::operator=(FMissionObjectiveList const& Other)
{
    Clear();
    std::list<FMissionObjective*>::const_iterator It = Other.Objectives.begin();
    for (; It != Other.Objectives.end(); ++It)
        Objectives.push_back((*It)->Clone());
    return *this;
}

// FUN_06f1cb80 (06f1cb80-06f1cbba, RET 8): per node delete when non-null (vslot 0 with 1),
// erase (vslot 3), until Last.
void FMissionObjectiveList::EraseRange(std::list<FMissionObjective*>::iterator First,
                                       std::list<FMissionObjective*>::iterator Last)
{
    while (First != Last)
    {
        if (*First != nullptr)
            delete *First;
        First = Objectives.erase(First);
    }
}

// FUN_06f1cbc0 (06f1cbc0-06f1cc35, RET 0xc): for each of Source, slot 5, clone +0x14 =
// InMission, inline insert before Where and ++size.
void FMissionObjectiveList::InsertClones(std::list<FMissionObjective*>::iterator Where,
                                         FMissionObjectiveList const* Source,
                                         FMissionObject* InMission)
{
    std::list<FMissionObjective*>::const_iterator It = Source->Objectives.begin();
    for (; It != Source->Objectives.end(); ++It)
    {
        FMissionObjective* Copy = (*It)->Clone();
        Copy->Mission = InMission;
        Objectives.insert(Where, Copy);
    }
}

// FUN_06f1cc40 (06f1cc40-06f1cca4, RET 8).
FMissionObjective* FMissionObjectiveList::Add(int Type, FMissionObject* InMission)
{
    FMissionObjective* Created = nullptr;
    CreateMissionObjective(Type, InMission, &Created);
    if (Created != nullptr)
        Objectives.push_back(Created);
    return Created;
}

// FUN_06f1ccb0 (06f1ccb0-06f1ccfc): size 0 -> nothing; else the front value deleted (vslot 0
// with 1) and cleared, the node unlinked and freed, --size.
void FMissionObjectiveList::PopFront()
{
    if (Objectives.empty())
        return;
    if (Objectives.front() != nullptr)
    {
        delete Objectives.front();
        Objectives.front() = nullptr;
    }
    Objectives.pop_front();
}

// FUN_06f1cd00: size 0 -> 0, else tail call of the front value's slot 4.
int FMissionObjectiveList::GetFrontType()
{
    if (Objectives.empty())
        return 0;
    return Objectives.front()->GetType();
}

// FUN_06f1cd20.
FMissionObjective* FMissionObjectiveList::GetFront()
{
    if (Objectives.empty())
        return nullptr;
    return Objectives.front();
}

// ---------------------------------------------------------------------------------------
// FMissionPlayer (vtable 06fb85b8)
// ---------------------------------------------------------------------------------------

// 06f1b77f-06f1b7d8 (inlined in FUN_06f1b6d0).
FMissionPlayer::FMissionPlayer(unsigned int InPlayerId, FMissionObject* InMission)
    : PlayerId(InPlayerId)
    , Mission(InMission)
    , PlayerState(FindContentPlayerState(InPlayerId))
    , Field_0x24(0)
    , Field_0x25(0)
{
    // 06f1b7d8, after the vtable store.
    Objectives.Owner = this;
}

// 06f0e2b0 -> FUN_06f1cd40 (EH state 1): the +0x28 objectives deleted (06f1cd69-06f1cd9f, the
// FUN_06f1cb40 loop through the list's vslot 3), then the member dtors: +0x28 (state 2), +8
// (state 3).
FMissionPlayer::~FMissionPlayer()
{
    SentObjectives.Clear();
}

// FUN_06f1d3d0 (06f1d3d0-06f1d413, RET 4).
void FMissionPlayer::SetField25(bool bValue)
{
    Field_0x25 = bValue ? 1 : 0;
    if (bValue)
        return;
    SentObjectives.Clear();
}

// LAB_06f1d420.
bool FMissionPlayer::GetField25()
{
    return Field_0x25 != 0;
}

// FUN_06f1d110 (06f1d110-06f1d3c0).
void FMissionPlayer::SendObjectives()
{
    // 06f1d12d-06f1d169: a mission, its player state, and the player's live mission either
    // this one or none.
    if (Mission == nullptr)
        return;
    if (FindContentPlayerState(Mission->PlayerId) == nullptr)
        return;
    unsigned int LiveMission = PlayerState->ValueD8;
    if (LiveMission != Mission->ControllerId && LiveMission != 0)
        return;

    // 06f1d16f: mission +0x84 = 0.
    Mission->Field_0x84 = 0.0f;

    // 06f1d178-06f1d1a9: objectives with +4 == 0 whose slot 3 is true.
    unsigned int Count = 0;
    std::list<FMissionObjective*>::iterator It = Objectives.Objectives.begin();
    for (; It != Objectives.Objectives.end(); ++It)
    {
        if ((*It)->bHidden != 0)
            continue;
        if ((*It)->BuildDescription())
            ++Count;
    }

    if (Count == 0)
    {
        // LAB_06f1d38d: no array.
        pub::Player::SetMissionObjectives(PlayerId, Mission->ControllerId, nullptr, 0,
                                          Mission->Field_0x50, Mission->Field_0x80,
                                          Mission->Field_0x68);
        return;
    }

    // 06f1d1b9-06f1d1ee: new[] of Count MissionObjective (0x1c, ctor FUN_06f1a5d0: Type 1, empty
    // FmtStr; EH state 0 only while the elements are built, the array is a raw pointer after
    // that). The fill loop below writes one entry per objective with +4 == 0 without the slot 3
    // test, so retail writes past the array when a visible objective's slot 3 failed above.
    pub::Player::MissionObjective* Sent = new pub::Player::MissionObjective[Count];
    for (unsigned int Slot = 0; Slot < Count; ++Slot)
        Sent[Slot].Type = 1;
    unsigned int Index = 0;
    It = Objectives.Objectives.begin();
    for (; It != Objectives.Objectives.end(); ++It)
    {
        FMissionObjective* Objective = *It;
        if (Objective->bHidden != 0)
        {
            // LAB_06f1d320.
            Objective->Index = -1;
            continue;
        }
        if (Field_0x25 == 0 && (Objective->Objective.Type & 0x10) != 0)
        {
            // 06f1d230-06f1d259: already sent (slot 6 of a kept clone)?
            bool bKnown = false;
            std::list<FMissionObjective*>::iterator KeptIt = SentObjectives.Objectives.begin();
            for (; KeptIt != SentObjectives.Objectives.end(); ++KeptIt)
            {
                if ((*KeptIt)->IsSame(Objective))
                {
                    bKnown = true;
                    break;
                }
            }
            if (bKnown)
            {
                // LAB_06f1d2e1-06f1d31b: clear the first set flag of 0x100, then 0x20 with
                // 0x40, else 0x10.
                unsigned int Type = Objective->Objective.Type;
                if ((Type & 0x100) != 0)
                {
                    Objective->Objective.Type = Type & ~0x100u;
                }
                else
                {
                    if ((Type & 0x20) != 0)
                        Objective->Objective.Type = Type & ~0x20u;
                    Type = Objective->Objective.Type;
                    if ((Type & 0x40) != 0)
                        Objective->Objective.Type = Type & ~0x40u;
                    else
                        Objective->Objective.Type = Type & ~0x10u;
                }
            }
            else
            {
                // 06f1d25b-06f1d288: clear 0x40, keep slot 5's object in +0x28.
                if ((Objective->Objective.Type & 0x40) != 0)
                    Objective->Objective.Type = Objective->Objective.Type & ~0x40u;
                FMissionObjective* Kept = Objective->Clone();
                SentObjectives.Objectives.push_back(Kept);
            }
        }
        // LAB_06f1d28d-06f1d2cf.
        if (Objective->Ids == 1)
            Objective->Objective.Type = (Objective->Objective.Type & 0x80) | 9;
        // 06f1d2a5-06f1d2cf: +0x10 = index, the entry's Type and FmtStr::operator=.
        Objective->Index = static_cast<int>(Index);
        Sent[Index].Type = Objective->Objective.Type;
        Sent[Index].Description = Objective->Objective.Description;
        ++Index;
    }

    // 06f1d332-06f1d372: send with Count (EBX is reloaded from the count after every write,
    // 06f1d2d3), then delete[] the array (FUN_06fa9036 with the element dtor LAB_06f1c980).
    pub::Player::SetMissionObjectives(PlayerId, Mission->ControllerId, Sent, Count,
                                      Mission->Field_0x50, Mission->Field_0x80,
                                      Mission->Field_0x68);
    delete[] Sent;
}

// FUN_06f1cea0 (06f1cea0-06f1ced4).
void FMissionPlayer::ClearObjectives()
{
    Objectives.Clear();
}

// FUN_06f1cee0 (06f1cee0-06f1d109). Backup (EH state 1) = a copy of the +8 list: base copy
// (FUN_06f1ca70 + FUN_06f1caa0 clones) then its Owner; Temp (state 2) = a new derived list
// (Owner 0); a type 7 objective of the record's mission (FUN_06f19cd0) goes to Temp's end when
// created (FUN_06f1e390) and gets Type 0x89 with no null test. The +8 list = Temp (FUN_06f1cb40,
// clones appended through FUN_06ecfc30, Owner = 0), FUN_06f1d110, the +8 list = Backup (the
// same, Owner restored). Temp is destroyed before Backup; both delete their elements.
void FMissionPlayer::SendPlaceholderObjective()
{
    FMissionOwnedObjectiveList Backup(Objectives);
    FMissionOwnedObjectiveList Temp;

    FMissionObjective* Placeholder = nullptr;
    CreateMissionObjective(7, Mission, &Placeholder);
    if (Placeholder != nullptr)
        Temp.Objectives.push_back(Placeholder);
    Placeholder->Objective.Type = 0x89;

    Objectives = Temp;
    SendObjectives();
    Objectives = Backup;
}

// ---------------------------------------------------------------------------------------
// FMissionObject player slots
// ---------------------------------------------------------------------------------------

// FUN_06f1b6d0 (06f1b6d0-06f1b821, RET 4).
FMissionPlayer* FMissionObject::GetOrCreatePlayer(unsigned int InPlayerId)
{
    // 06f1b6ee-06f1b72d: FUN_06f2e220 find; a hit returns the node value (LAB_06f1b808).
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.find(InPlayerId);
    if (It != Players.end())
        return It->second;
    // 06f1b733-06f1b746: no player state -> 0.
    FContentPlayerState* State = FindContentPlayerState(InPlayerId);
    if (State == nullptr)
        return nullptr;
    // 06f1b74c-06f1b761: in multiplayer the player must be in a system (FUN_06f478c0).
    if (!pub::SinglePlayer() && State->GetSystemRecord() == nullptr)
        return nullptr;
    // 06f1b767-06f1b7ff: new(0x3c), inline ctor, FUN_06f003e0 insert {id, player}.
    FMissionPlayer* Player = new FMissionPlayer(InPlayerId, this);
    Players.insert(std::make_pair(InPlayerId, Player));
    return Player;
}

// FUN_06f1b830 (06f1b830-06f1b881, RET 4).
FMissionPlayer* FMissionObject::FindPlayer(unsigned int InPlayerId)
{
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.find(InPlayerId);
    if (It == Players.end())
        return nullptr;
    return It->second;
}

// FUN_06f1b8c0 (06f1b8c0-06f1b9e2, RET 4). Null -> nothing. Delete the objectives (+8), resend
// the now empty list (FUN_06f1d110), erase every +0x3c entry keyed by the player id (the
// equal_range walk 06f1b909-06f1b9d0; whole-tree clear when the range spans the tree), then
// delete the record (vslot 0 with 1).
void FMissionObject::RemovePlayer(FMissionPlayer* Player)
{
    if (Player == nullptr)
        return;
    Player->Objectives.Clear();
    Player->SendObjectives();
    Players.erase(Player->PlayerId);
    delete Player;
}

// FUN_06f1b8a0 (06f1b8a0-06f1b8b8, RET 4): vslot 4 of vslot 3.
void FMissionObject::RemovePlayerById(unsigned int InPlayerId)
{
    RemovePlayer(FindPlayer(InPlayerId));
}

// LAB_06f1b890: head->left == head -> 0, else its value.
FMissionPlayer* FMissionObject::GetFirstPlayer()
{
    if (Players.empty())
        return nullptr;
    return Players.begin()->second;
}

// FUN_06f0fc40 (06f0fc40-06f0fd90). The +0xd0 loot records are not touched.
// TODO(CNT-AI): the +0x114 clear (FUN_06f1ecc0, 06f0fd17-06f0fd2c) - the [MetaBehavior] map is
// not declared yet.
void FMissionObject::EndMission()
{
    // 06f0fc48-06f0fc66: while slot 6 returns a record, slot 5 with its id.
    FMissionPlayer* Player = GetFirstPlayer();
    while (Player != nullptr)
    {
        RemovePlayerById(Player->PlayerId);
        Player = GetFirstPlayer();
    }
    // 06f0fc68-06f0fc8c: FmtStr::destroy and zero both FmtStr (6 dwords each).
    Field_0x50.destroy();
    std::memset(&Field_0x50, 0, sizeof(Field_0x50));
    Field_0x68.destroy();
    std::memset(&Field_0x68, 0, sizeof(Field_0x68));
    // 06f0fc8e-06f0fca3: +0x184 erase(begin, end) (FUN_06f1f130).
    MissionNpcs.clear();
    // 06f0fca8-06f0fcbd: +0xa4 cleared.
    NpcObjects.clear();
    // 06f0fcc2-06f0fcfb: +0xb8 erased node by node (FUN_06f1de90).
    MissionShips.clear();
    // 06f0fcfd-06f0fd12: +0x100 erase(begin, end) (FUN_06f1ec00).
    ObjLists.clear();
    // 06f0fd17-06f0fd2c: +0x114 erase(begin, end) (FUN_06f1ecc0).
    MetaBehaviors.clear();
    // 06f0fd31-06f0fd56: +0xc4 erased node by node.
    MissionSolars.clear();
    // 06f0fd58-06f0fd83: +0x98 erased node by node.
    RandEncs.clear();
    // 06f0fd86.
    bPendingDestroy = 1;
}

// FUN_06f1ca50 (06f1ca50-06f1ca6b, RET 4).
void FMissionObject::SetPlayer(unsigned int InPlayerId)
{
    PlayerId = InPlayerId;
    PlayerState = FindContentPlayerState(InPlayerId);
}

// DAT_06fce594 (.data, initial 1): the objective resend timer runs (msg 2 clears, 0x36 sets).
static bool GObjectiveTimerRunning = true;

// FUN_06f1b4f0 (06f1b4f0-06f1b600, RET 8). Messages 2 / 0x36 whose parms+4 is this mission's
// player (a real id) stop / restart the timer. Message 0x20 while running: +0x84 += *parms
// (FLD/FADD/FST), and once it is >= 180.0 (DAT_06fb8df8; FCOMP, TEST AH,0x01 + JNZ skips on
// less or unordered) every player record is resent through slot 3 / slot 1 (1) /
// FUN_06f1d110 / slot 1 (0). FUN_06f1d110 zeroes +0x84.
void FMissionObject::UpdateObjectiveTimer(pub::Controller::EMessage MessageId,
                                          void const* MessageParms)
{
    if (MessageId == pub::Controller::EMessage::BaseExit)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
        if (Parms[1] == PlayerId && (PlayerId & 0x3fffffff) != 0)
            GObjectiveTimerRunning = false;
        return;
    }
    if (MessageId == pub::Controller::EMessage::SpaceEnter)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
        if (Parms[1] == PlayerId && (PlayerId & 0x3fffffff) != 0)
            GObjectiveTimerRunning = true;
        return;
    }
    if (MessageId != pub::Controller::EMessage::Heartbeat)
        return;
    if (!GObjectiveTimerRunning)
        return;
    // The compare uses the unrounded x87 sum (FST keeps ST0); a double sum of two floats is
    // exact here.
    double const Sum = static_cast<double>(*static_cast<float const*>(MessageParms)) +
                       static_cast<double>(Field_0x84);
    Field_0x84 = static_cast<float>(Sum);
    if (!(Sum >= 180.0))
        return;
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.begin();
    for (; It != Players.end(); ++It)
    {
        FMissionPlayer* Player = FindPlayer(It->first);
        if (Player == nullptr)
            continue;
        Player->SetField25(true);
        Player->SendObjectives();
        Player->SetField25(false);
    }
}

// FUN_06f1b260 (06f1b260-06f1b2ff, RET 8).
bool FMissionObject::SetObjectiveVisible(unsigned int const* NicknameId, bool bVisible)
{
    bool bFound = false;
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.begin();
    for (; It != Players.end(); ++It)
    {
        FMissionPlayer* Player = It->second;
        if (Player == nullptr)
            continue;
        std::list<FMissionObjective*>::iterator ObjIt = Player->Objectives.Objectives.begin();
        for (; ObjIt != Player->Objectives.Objectives.end(); ++ObjIt)
        {
            if (*NicknameId != (*ObjIt)->Nickname.Id)
                continue;
            (*ObjIt)->bHidden = bVisible ? 0 : 1;
            bFound = true;
            break;
        }
    }
    return bFound;
}

// FUN_06f1b310 (06f1b310-06f1b3c6, RET 8): no early exit, every match of every player.
void FMissionObject::SetObjectiveState(unsigned int const* NicknameId, int State)
{
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.begin();
    for (; It != Players.end(); ++It)
    {
        FMissionPlayer* Player = It->second;
        if (Player == nullptr)
            continue;
        std::list<FMissionObjective*>::iterator ObjIt = Player->Objectives.Objectives.begin();
        for (; ObjIt != Player->Objectives.Objectives.end(); ++ObjIt)
        {
            FMissionObjective* Objective = *ObjIt;
            if (*NicknameId != Objective->Nickname.Id)
                continue;
            Objective->Objective.Type = static_cast<unsigned int>(State);
            // 06f1b351-06f1b35a: CMP index,-1 / JLE skips.
            if (Objective->Index > -1)
                pub::Player::SetMissionObjectiveState(Player->PlayerId, ControllerId,
                                                      Objective->Index,
                                                      static_cast<unsigned int>(State));
        }
    }
}

// FUN_06f1b3d0 (06f1b3d0-06f1b475): no null test on the records; per player the objectives are
// deleted and their nodes erased (vslot 3), then FUN_06f1d110.
void FMissionObject::ClearObjectives()
{
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.begin();
    for (; It != Players.end(); ++It)
    {
        FMissionPlayer* Player = It->second;
        Player->Objectives.Clear();
        Player->SendObjectives();
    }
}

// FUN_06f1b480 (06f1b480-06f1b4ed): null records skipped.
void FMissionObject::HideAllObjectives()
{
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.begin();
    for (; It != Players.end(); ++It)
    {
        FMissionPlayer* Player = It->second;
        if (Player == nullptr)
            continue;
        std::list<FMissionObjective*>::iterator ObjIt = Player->Objectives.Objectives.begin();
        for (; ObjIt != Player->Objectives.Objectives.end(); ++ObjIt)
            (*ObjIt)->bHidden = 1;
    }
}

// FUN_06f1b610 (06f1b610-06f1b66d).
void FMissionObject::RenumberAllObjectives()
{
    std::map<unsigned int, FMissionPlayer*>::iterator It = Players.begin();
    for (; It != Players.end(); ++It)
        RenumberObjectives(It->first);
}

// FUN_06f1b670 (06f1b670-06f1b6c0, RET 4): slot 3, no null test.
void FMissionObject::RenumberObjectives(unsigned int InPlayerId)
{
    FMissionPlayer* Player = FindPlayer(InPlayerId);
    int NextIndex = 0;
    std::list<FMissionObjective*>::iterator ObjIt = Player->Objectives.Objectives.begin();
    for (; ObjIt != Player->Objectives.Objectives.end(); ++ObjIt)
    {
        FMissionObjective* Objective = *ObjIt;
        if (Objective->bHidden != 0)
        {
            Objective->Index = -1;
            continue;
        }
        if (NextIndex != 0)
            Objective->Objective.Type = 10;
        Objective->Index = NextIndex;
        ++NextIndex;
    }
    Player->SendObjectives();
}

// FUN_06ea1490: RET 8.
void FMissionObject::Slot20(unsigned int Arg0, unsigned int Arg1)
{
    (void)Arg0;
    (void)Arg1;
}

// FUN_06f0f150 (06f0f150-06f0fbf3, RET 8). Two switches (raw PE tables: 06f0fbf8 for ids 4..0xa,
// the byte table 06f0fc2c + 06f0fc14 for 0x24..0x36) plus compares; every case ends at the
// common tail 06f0fbb5 (caseD_7), which forwards the message to slot 19 while this is the
// player's live mission.
void FMissionObject::HandleMissionMessage(pub::Controller::EMessage MessageId,
                                          void const* MessageParms)
{
    using pub::Controller::EMessage;
    // 06f0f159-06f0f167: FUN_06f49b00(this+0xc); null -> return (no tail).
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    // 06f0f17e-06f0f18c: ids other than 8 and 9 go to FUN_06f148f0.
    if (MessageId != EMessage::PlayerLogout && MessageId != EMessage::GroupMemberLeft)
        NotifyRandEncs(MessageId, MessageParms);

    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    switch (MessageId)
    {
    case EMessage::PlayerSave:
        // 06f0f2f5-06f0f362 (save, parms = {player id, FILE*}): the mission's player in single
        // player with +0x18 clear -> the snapshot when the player has a ship (state +0x10), then
        // FUN_06f0ff90 and FUN_06f11f70 on the file.
        if (FindContentPlayerState(PlayerId) == nullptr)
            break;
        if (Parms[0] != PlayerId)
            break;
        if (!pub::SinglePlayer())
            break;
        if (bTriggerSavePending != 0)
            break;
        if (PlayerState->ShipObject != nullptr)
            SnapshotMissionObjects();
        {
            FILE* File = static_cast<FILE* const*>(MessageParms)[1];
            SaveMission(File);
            SaveRandEncs(File);
        }
        break;

    case EMessage::PlayerLoad:
    {
        // 06f0f367-06f0f3c4 (load, parms = {player id, INI_Reader*}): the reader's state kept
        // around FUN_06f106e0 (get_state, reset, load, set_state).
        if (FindContentPlayerState(PlayerId) == nullptr)
            break;
        if (Parms[0] != PlayerId)
            break;
        INI_Reader* Reader = static_cast<INI_Reader* const*>(MessageParms)[1];
        INI_Reader::State Saved;
        Reader->get_state(Saved);
        Reader->reset();
        LoadMissionSave(Reader);
        Reader->set_state(Saved);
        break;
    }

    case EMessage::PlayerLaunch:
        // 06f0f3c9-06f0f44d: single player and parms[0] is the player -> every +0x134 name
        // spawned (FUN_06f171e0, no placement), then the list emptied.
        if (!pub::SinglePlayer())
            break;
        if (Parms[0] != PlayerId)
            break;
        {
            std::list<TString<0x30>>::iterator It = RespawnSolars.begin();
            for (; It != RespawnSolars.end(); ++It)
                SpawnMissionSolar(&*It, nullptr);
        }
        RespawnSolars.clear();
        break;

    case EMessage::PlayerLogout:
    case EMessage::GroupMemberLeft:
    {
        // 06f0f209-06f0f2f0 (player gone): slot 5 on the id, the id out of +0xdc.
        RemovePlayerById(Parms[0]);
        std::vector<unsigned int>::iterator Dead =
            std::find(DeadPlayerIds.begin(), DeadPlayerIds.end(), Parms[0]);
        if (Dead != DeadPlayerIds.end())
            DeadPlayerIds.erase(Dead);
        bool bFail = false;
        if (PlayerId == Parms[0])
        {
            // 06f0f27d-06f0f2a5: the mission's own player: slot 15 set -> fail; else slot 18
            // with the first remaining record's player (0 without one).
            if (Slot15())
            {
                bFail = true;
            }
            else
            {
                FMissionPlayer* First = GetFirstPlayer();
                if (First != nullptr)
                    SetPlayer(First->PlayerId);
                else
                    SetPlayer(0);
            }
        }
        else if (FindContentPlayerState(PlayerId) != nullptr && PlayerState->IsGroupMember(Parms[0]) &&
                 Slot16(Parms[0]))
        {
            // 06f0f2a7-06f0f2cf: a group member left and slot 16 says so -> fail.
            bFail = true;
        }
        if (bFail)
        {
            // 06f0f2d5-06f0f2de: slot 24, then slot 18 with 0.
            OnMissionFail();
            SetPlayer(0);
        }
        // 06f0f2e1-06f0f2ec: no player state any more -> +0x19 = 1.
        if (PlayerState == nullptr)
            bPendingDestroy = 1;
        break;
    }

    case EMessage::GroupMemberJoined:
        // 06f0f1b3-06f0f204: a group member of the player joins the mission (FUN_06f48ba0 on
        // the member's state, which retail does not null-test).
        if (FindContentPlayerState(PlayerId) == nullptr)
            break;
        if (!PlayerState->IsGroupMember(Parms[0]))
            break;
        FindContentPlayerState(Parms[0])->JoinMission(&ControllerId, PlayerId, false, 0);
        break;

    case EMessage::Heartbeat:
        // 06f0f452-06f0f45d: FUN_06f14b40(*parms) (the frame time).
        UpdateJumpers(*static_cast<float const*>(MessageParms));
        break;

    case EMessage::AbortMission:
    {
        // 06f0f5b3-06f0f5bf: *parms == +0xc, else the tail (no null test in retail).
        if (Parms[0] != PlayerId)
            break;
        if (FindContentPlayerState(PlayerId) == nullptr)
            break;
        // 06f0f5d9-06f0f5e4: FUN_06f487c0([this+8]) = cached state +0xd8 == +4.
        if (PlayerState->GetLiveMission() != ControllerId)
            break;
        // 06f0f5ea-06f0f5ee: CALL [EDX+0x60] = slot 24.
        OnMissionFail();
        break;
    }

    case EMessage::Msg0x25:
        // 06f0f48b-06f0f49a: FUN_06f149f0(parms).
        OnShipJump(Parms);
        break;

    case EMessage::JumpInComplete:
        // 06f0f49f-06f0f4b1: a pending jump starts its timer.
        if (bJumpPending != 0)
            JumpTimer = 0.0f;
        break;

    case EMessage::Msg0x27:
    {
        // 06f0f4b6-06f0f4eb: the player whose ship is *parms (FUN_06f49bc0) is appended to +0xdc
        // (FUN_06f937c0 insert at the end).
        FContentPlayerState* Dead = FindContentPlayerStateByShip(Parms);
        if (Dead == nullptr)
            break;
        unsigned int DeadId = Dead->PlayerId;
        DeadPlayerIds.push_back(DeadId);
        break;
    }

    case EMessage::SpaceEnter:
    {
        // 06f0f4f0-06f0f5ae: parms[1] leaves +0xdc; for the mission's player in single player,
        // the respawn keeps the hit points unless parms[2] (low 30 bits set) is an object of type
        // 0x20 or 0x100, then FUN_06f145d0 and FUN_06f12c10.
        std::vector<unsigned int>::iterator Dead =
            std::find(DeadPlayerIds.begin(), DeadPlayerIds.end(), Parms[1]);
        if (Dead != DeadPlayerIds.end())
            DeadPlayerIds.erase(Dead);
        if (Parms[1] != PlayerId)
            break;
        if (!pub::SinglePlayer())
            break;
        bool bKeepHitPoints = true;
        if ((Parms[2] & 0x3fffffff) != 0)
        {
            unsigned int Type;
            pub::SpaceObj::GetType(Parms[2], Type);
            if ((Type & 0x20) != 0 || (Type & 0x100) != 0)
                bKeepHitPoints = false;
        }
        RespawnMissionObjects(bKeepHitPoints);
        // 06f0f5a7-06f0f5a9.
        ApplyVibeInfos();
        break;
    }

    case EMessage::SpaceExit:
        // 06f0f5f6-06f0f61e: single player and parms[1] is the mission's player -> the snapshot.
        if (!pub::SinglePlayer())
            break;
        if (Parms[1] != PlayerId)
            break;
        SnapshotMissionObjects();
        break;

    case EMessage::ObjectDestroyed:
    {
        // 06f0f657-06f0f924 (a SpaceObj left space; parms[0] its id, parms[2] the other object
        // of the event, parms[3] a player id).
        // 06f0f657-06f0f691: a live mission NPC's ship slot gets its jumper flag back.
        std::map<unsigned int, FContentNpcShip*>::iterator NpcIt = NpcObjects.find(Parms[0]);
        if (NpcIt != NpcObjects.end() && NpcIt->second != nullptr)
        {
            FMissionShip* Ship = FindMissionShip(&NpcIt->second->Nickname);
            if (Ship != nullptr)
                Ship->bJumper = Ship->bOriginalJumpFlag;
        }
        // 06f0f694-06f0f6b9: the first jumper entry of that SpaceObj is unlinked.
        {
            std::list<FMissionJumper>::iterator It = Jumpers.begin();
            for (; It != Jumpers.end(); ++It)
            {
                if (It->Npc->SpaceObjId == Parms[0])
                {
                    Jumpers.erase(It);
                    break;
                }
            }
        }
        // 06f0f6be-06f0f6c1: out of its random-encounter formation arm.
        RemoveRandEncMember(Parms);
        // 06f0f6c6-06f0f6f4: single player with a system record -> FUN_06f74620.
        {
            FContentPlayerState* State = FindContentPlayerState(PlayerId);
            if (pub::SinglePlayer() && State != nullptr)
            {
                FSystemInfo* System = PlayerState->GetSystemRecord();
                if (System != nullptr)
                    System->RemoveMissionObject(Parms[0]);
            }
        }
        // 06f0f6f9-06f0f7ef: an NPC leaves +0xa4 (deleted when parms[2] & 0x3fffffff is 0);
        // otherwise a [MsnSolar] / [MsnLoot] record with that id gets +4 / +0x38 = 2 and its id
        // cleared (the loot walk only when no solar matched).
        bool const bNoOther = (Parms[2] & 0x3fffffff) == 0;
        NpcIt = NpcObjects.find(Parms[0]);
        if (NpcIt != NpcObjects.end())
        {
            if (bNoOther && NpcIt->second != nullptr)
                delete NpcIt->second;
            NpcObjects.erase(NpcIt);
        }
        else
        {
            bool bSolar = false;
            std::list<FMissionSolar>::iterator SolarIt = MissionSolars.begin();
            for (; SolarIt != MissionSolars.end(); ++SolarIt)
            {
                if (SolarIt->SpaceObjId == Parms[0])
                {
                    SolarIt->bSpawned = 2;
                    SolarIt->SpaceObjId = 0;
                    bSolar = true;
                    break;
                }
            }
            if (!bSolar)
            {
                std::list<FMissionLoot>::iterator LootIt = MissionLoots.begin();
                for (; LootIt != MissionLoots.end(); ++LootIt)
                {
                    if (LootIt->SpaceObjId == Parms[0])
                    {
                        LootIt->bSpawned = 2;
                        LootIt->SpaceObjId = 0;
                        break;
                    }
                }
            }
        }
        // 06f0f7f2-06f0f842: every label entry of that SpaceObj leaves +0xec.
        {
            std::multimap<unsigned int, unsigned int>::iterator It = FormationMembers.begin();
            while (It != FormationMembers.end())
            {
                if (It->second == Parms[0])
                    It = FormationMembers.erase(It);
                else
                    ++It;
            }
        }
        // 06f0f844-06f0f864: nothing more for a player in +0xdc.
        if (std::find(DeadPlayerIds.begin(), DeadPlayerIds.end(), Parms[3]) != DeadPlayerIds.end())
            break;
        if (FindContentPlayerState(PlayerId) == nullptr)
            break;
        // 06f0f87e-06f0f924: the player's own ship (slot 12), a group member (FUN_06f48190 +
        // slot 13) or the player's ship alone in its group (FUN_06f480c0 == 1 + slot 14) -> slot 24.
        unsigned int ShipId = 0;
        if (PlayerState->ShipObject != nullptr)
            ShipId = PlayerState->ShipObject->SpaceObjId;
        if (PlayerId == Parms[3] && ShipId == Parms[0] && Slot12())
        {
            OnMissionFail();
            break;
        }
        if (PlayerState->IsGroupMember(Parms[3]) && Slot13(Parms[3]))
        {
            OnMissionFail();
            break;
        }
        if (PlayerId == Parms[3] && ShipId == Parms[0] && PlayerState->GetGroupSize() == 1 && Slot14())
            OnMissionFail();
        break;
    }

    case EMessage::SystemEnter:
        // 06f0f643-06f0f652: FUN_06f14c10(parms).
        PlaceJumpersAtLaunch(Parms);
        break;

    case EMessage::Msg0x1006:
        // 06f0f929-06f0f93b: parms[1] == 0 -> slot 24.
        if (Parms[1] == 0)
            OnMissionFail();
        break;

    case EMessage::FormationLeaderChanged:
    {
        // 06f0faa1-06f0fbaf: parms[1] is an NPC of this mission (+0x170); every player record
        // with a visible rep_inst objective (slot 4 == 4) whose label resolves (FUN_06f15940,
        // else FUN_06f15b10, else FUN_06f159f0; only with a player ship) to parms[0]'s SpaceObj
        // id renumbers every player (FUN_06f1b670 per +0x3c key) and goes on with the next
        // record.
        FContentSpaceObj const* Object = *static_cast<FContentSpaceObj* const*>(MessageParms);
        FContentNpcShip const* Npc = static_cast<FContentNpcShip* const*>(MessageParms)[1];
        if (Npc->ControllerId != ControllerId)
            break;
        std::map<unsigned int, FMissionPlayer*>::iterator RecordIt = Players.begin();
        for (; RecordIt != Players.end(); ++RecordIt)
        {
            FMissionPlayer* Record = RecordIt->second;
            if (Record == nullptr)
                continue;
            std::list<FMissionObjective*>::iterator ObjIt = Record->Objectives.Objectives.begin();
            for (; ObjIt != Record->Objectives.Objectives.end(); ++ObjIt)
            {
                FMissionObjective* Objective = *ObjIt;
                if (Objective->bHidden != 0)
                    continue;
                if (Objective->GetType() != 4)
                    continue;
                TString<0x30> const* Label = &static_cast<FNNObjectiveRepInst*>(Objective)->Label;
                unsigned int TargetId = 0;
                if (FindContentPlayerState(PlayerId) != nullptr && PlayerState->ShipObject != nullptr)
                {
                    TargetId = GetLabelShipId(Label);
                    if ((TargetId & 0x3fffffff) == 0)
                    {
                        TargetId = GetLootId(Label);
                        if ((TargetId & 0x3fffffff) == 0)
                            TargetId = GetSolarId(Label);
                    }
                }
                if ((TargetId & 0x3fffffff) == 0)
                    continue;
                if (TargetId != Object->SpaceObjId)
                    continue;
                std::map<unsigned int, FMissionPlayer*>::iterator PlayerIt = Players.begin();
                for (; PlayerIt != Players.end(); ++PlayerIt)
                    RenumberObjectives(PlayerIt->first);
                break;
            }
        }
        break;
    }

    case EMessage::NpcShipCreated:
    {
        FContentNpcShip* Npc = *static_cast<FContentNpcShip* const*>(MessageParms);
        // 06f0f955-06f0f967: npc +0x170 == +4.
        if (Npc->ControllerId != ControllerId)
            break;
        // 06f0f96d-06f0f97b.
        if (FindContentPlayerState(PlayerId) == nullptr)
            break;
        // 06f0f981-06f0f9eb: strcmp of each [MarkObj] name with the npc nickname.
        bool bMarkObjHit = false;
        std::list<TString<0x30>>::iterator It = MarkObjNames.begin();
        for (; It != MarkObjNames.end(); ++It)
        {
            if (std::strcmp(Npc->Nickname.Data, It->Data) == 0)
            {
                bMarkObjHit = true;
                break;
            }
        }
        // 06f0f9ed-06f0f9ff: pub::Player::MarkObj(+0xc, npc+4, 1).
        if (bMarkObjHit)
            pub::Player::MarkObj(PlayerId, Npc->SpaceObjId, 1);
        // 06f0fa02-06f0fa0d: FUN_06f15560(this, npc+0xd4).
        FMissionShip* Ship = FindMissionShip(&Npc->Nickname);
        // 06f0fa12-06f0fa45: ship != 0 && byte [ship+0x48] (jumper) != 0 -> FUN_06f1e440(+0x1c,
        // npc).
        if (Ship != nullptr && Ship->bJumper != 0)
        {
            // 06f0fa1f-06f0fa45: {0, 0} appended, then the new node's npc = the npc.
            FMissionJumper Jumper;
            Jumper.Npc = nullptr;
            Jumper.State = 0;
            Jumpers.push_back(Jumper);
            Jumpers.back().Npc = Npc;
        }
        // 06f0fa48-06f0fa4b: FUN_06f14e80(this, parms).
        AttachRandEncNpc(MessageParms);
        // 06f0fa50-06f0fa9a: every formation id on the npc (+0x17c) -> FUN_06f003e0 insert
        // {id, npc+4} into the +0xec multimap.
        std::list<unsigned int>::iterator FormIt = Npc->Field_0x17c.begin();
        for (; FormIt != Npc->Field_0x17c.end(); ++FormIt)
            FormationMembers.insert(std::make_pair(*FormIt, Npc->SpaceObjId));
        break;
    }

    default:
        break;
    }

    // 06f0fbb5-06f0fbe9 (caseD_7, every case): the player's state exists and its live mission
    // (FUN_06f487c0) is this controller -> slot 19 (vtbl[+0x4c]) with the message.
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    if (PlayerState->GetLiveMission() != ControllerId)
        return;
    UpdateObjectiveTimer(MessageId, MessageParms);
}

// FUN_06f0fda0 (06f0fda0-06f0fe9f). No player state -> slot 22 only. Else the reward (+0x10)
// is split by the group size (unsigned DIV) and paid through FUN_06f48050, every record's
// objectives are deleted and resent (FUN_06f1b3d0's loop inlined, no null test), slot 22 runs,
// and FUN_06f48a20(+4, 2, 1) ends the mission for the whole group.
void FMissionObject::OnMissionSucceed()
{
    FContentPlayerState* State = FindContentPlayerState(PlayerId);
    if (State == nullptr)
    {
        EndMission();
        return;
    }
    unsigned int const GroupSize = State->GetGroupSize();
    State->RewardGroup(static_cast<int>(Reward / GroupSize));
    ClearObjectives();
    EndMission();
    State->EndLiveMission(ControllerId, 2, true);
}

// FUN_06f0fea0 (06f0fea0-06f0ff89): every record's objectives deleted and resent (inlined
// FUN_06f1b3d0 loop), then slot 22; when the player state exists and its live mission
// (FUN_06f487c0) is this controller, FUN_06f48a20(+4, 3, 1) after it.
void FMissionObject::OnMissionFail()
{
    ClearObjectives();
    FContentPlayerState* State = FindContentPlayerState(PlayerId);
    if (State != nullptr && State->GetLiveMission() == ControllerId)
    {
        EndMission();
        State->EndLiveMission(ControllerId, 3, true);
        return;
    }
    EndMission();
}

// FUN_06eef520: OR EAX,-1; RET 4.
int FMissionObject::MissionStateFromName(char const* Name)
{
    (void)Name;
    return -1;
}

// LAB_06ea14a0: MOV EAX,"NOT_IMPLEMENTED" (.rdata:06fb384c); RET 4.
char const* FMissionObject::MissionStateToName(int State)
{
    (void)State;
    return "NOT_IMPLEMENTED";
}

// FUN_06ebf6f0: RET 4.
void FMissionObject::Slot28(void const* Parms)
{
    (void)Parms;
}

// FUN_06f15940 (06f15940-06f159b9, RET 4).
unsigned int FMissionObject::GetLabelShipId(TString<0x30> const* Label)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return 0;
    if (PlayerState->ShipObject == nullptr)
        return 0;
    // 06f15969: stricmp(label chars, "Player") (.rdata:06fb4708).
    if (_stricmp(Label->Data, "Player") == 0)
    {
        if (FindContentPlayerState(PlayerId) == nullptr)
            return 0;
        if (PlayerState->ShipObject == nullptr)
            return 0;
        return PlayerState->ShipObject->SpaceObjId;
    }
    FContentNpcShip* Npc = FindNpcByName(Label);
    if (Npc == nullptr)
        return 0;
    return Npc->SpaceObjId;
}

// FUN_06f15710 (06f15710-06f15789, RET 4): with a live player ship, the first +0xa4 entry
// (in key order) whose nickname matches (stricmp); a null value or no match falls back to
// FUN_06f15840. Without a player ship straight to FUN_06f15840.
FContentNpcShip* FMissionObject::FindNpcByName(TString<0x30> const* Label)
{
    if (FindContentPlayerState(PlayerId) != nullptr && PlayerState->ShipObject != nullptr)
    {
        std::map<unsigned int, FContentNpcShip*>::iterator It = NpcObjects.begin();
        for (; It != NpcObjects.end(); ++It)
        {
            if (_stricmp(It->second->Nickname.Data, Label->Data) != 0)
                continue;
            if (It->second != nullptr)
                return It->second;
            break;
        }
    }
    return FindFormationNpc(Label);
}

// FUN_06f15840 (06f15840-06f1592e, RET 4): +0xb8 records named Label (stricmp of the record
// chars, node+0xc); for each ship slot the first +0xa4 NPC with that nickname (stricmp of npc
// +0xd8 and slot +8); it is returned when +0x14c is null or +0x154 is -1, else the next slot is
// tried. Nothing found -> FUN_06f15490.
FContentNpcShip* FMissionObject::FindFormationNpc(TString<0x30> const* Label)
{
    std::list<FMissionShipGroup>::iterator GroupIt = MissionShips.begin();
    for (; GroupIt != MissionShips.end(); ++GroupIt)
    {
        if (_stricmp(GroupIt->Nickname.Data, Label->Data) != 0)
            continue;
        std::vector<FMissionShip>::iterator SlotIt = GroupIt->Ships.begin();
        for (; SlotIt != GroupIt->Ships.end(); ++SlotIt)
        {
            std::map<unsigned int, FContentNpcShip*>::iterator It = NpcObjects.begin();
            for (; It != NpcObjects.end(); ++It)
            {
                if (_stricmp(It->second->Nickname.Data, SlotIt->Nickname.Data) == 0)
                    break;
            }
            if (It == NpcObjects.end())
                continue;
            FContentNpcShip* Npc = It->second;
            if (Npc == nullptr)
                continue;
            // 06f158dd-06f158ee: +0x14c zero or +0x154 == -1 returns the ship.
            if (Npc->FormationSlot == 0 || Npc->FormationId == 0xffffffffu)
                return Npc;
        }
    }
    return FindRandEncFormationNpc(Label);
}

// FUN_06f15490 (06f15490-06f15554, RET 4): every random-encounter formation arm whose
// ID_String names Label (stricmp); for each NPC id in the arm the first +0xa4 NPC with that
// +4 (walk in key order), returned under the same +0x14c/+0x154 test. Else 0.
FContentNpcShip* FMissionObject::FindRandEncFormationNpc(TString<0x30> const* Label)
{
    std::list<FMissionRandEnc>::iterator EncIt = RandEncs.begin();
    for (; EncIt != RandEncs.end(); ++EncIt)
    {
        std::list<FMissionRandEncArm>::iterator ArmIt = EncIt->Arms.begin();
        for (; ArmIt != EncIt->Arms.end(); ++ArmIt)
        {
            if (_stricmp(ArmIt->Name.get_string(), Label->Data) != 0)
                continue;
            std::list<unsigned int>::iterator IdIt = ArmIt->MemberIds.begin();
            for (; IdIt != ArmIt->MemberIds.end(); ++IdIt)
            {
                std::map<unsigned int, FContentNpcShip*>::iterator It = NpcObjects.begin();
                for (; It != NpcObjects.end(); ++It)
                {
                    if (It->second->SpaceObjId == *IdIt)
                        break;
                }
                if (It == NpcObjects.end())
                    continue;
                FContentNpcShip* Npc = It->second;
                if (Npc == nullptr)
                    continue;
                // 06f15519-06f1552a.
                if (Npc->FormationSlot == 0 || Npc->FormationId == 0xffffffffu)
                    return Npc;
            }
        }
    }
    return nullptr;
}

// FUN_06f15b90 (06f15b90-06f15c6a, RET 4). The loot loop at 06f15bd7-06f15c45 is
// FUN_06f15b10 inlined.
unsigned int FMissionObject::GetLabelObjectId(TString<0x30> const* Label)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return 0;
    if (PlayerState->ShipObject == nullptr)
        return 0;
    unsigned int ShipId = GetLabelShipId(Label);
    if ((ShipId & 0x3fffffff) != 0)
        return ShipId;
    unsigned int LootId = GetLootId(Label);
    if ((LootId & 0x3fffffff) != 0)
        return LootId;
    return GetSolarId(Label);
}

// FUN_06f15b10 (06f15b10-06f15b88, RET 4): inline strcmp of each +0xd0 record's nickname chars
// (node+0xc) with the label chars; the first match returns its +0x34 (node+0x3c).
unsigned int FMissionObject::GetLootId(TString<0x30> const* Label)
{
    std::list<FMissionLoot>::iterator It = MissionLoots.begin();
    for (; It != MissionLoots.end(); ++It)
    {
        if (std::strcmp(It->Nickname.Data, Label->Data) == 0)
            return It->SpaceObjId;
    }
    return 0;
}

// FUN_06f159f0 (06f159f0-06f15ad9, RET 4).
unsigned int FMissionObject::GetSolarId(TString<0x30> const* Label)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return 0;
    if (PlayerState->ShipObject == nullptr)
        return 0;
    // 06f15a23-06f15a95: inline strcmp against each +0xc4 solar nickname (node+0x3f4).
    unsigned int SolarId = 0;
    std::list<FMissionSolar>::iterator It = MissionSolars.begin();
    for (; It != MissionSolars.end(); ++It)
    {
        if (std::strcmp(It->Nickname.Data, Label->Data) == 0)
        {
            SolarId = It->SpaceObjId;
            break;
        }
    }
    if ((SolarId & 0x3fffffff) != 0)
        return SolarId;
    // 06f15a9f-06f15ac6: a universe object of that nickname in the player's system.
    unsigned int NicknameId = pub::GetNicknameId(Label->Data);
    if (IsSpaceObjTypeSet(NicknameId, PlayerState->GetSystemRecord()))
        return NicknameId;
    return 0;
}

// FUN_06f156b0 (06f156b0-06f15701, RET 4): FUN_06fa7200 lower_bound on +0xa4 by *Id; end or
// a greater key -> 0, else the node value (+0x10).
FContentNpcShip* FMissionObject::FindNpcById(unsigned int const* Id)
{
    std::map<unsigned int, FContentNpcShip*>::iterator It = NpcObjects.lower_bound(*Id);
    if (It == NpcObjects.end() || *Id < It->first)
        return nullptr;
    return It->second;
}

// FUN_06f15ae0 (06f15ae0-06f15b0a, RET 4): the first +0xd0 record whose +0x34 (node+0x3c)
// equals *SpaceObjId, as the value pointer (node+8); null at the end.
FMissionLoot* FMissionObject::FindLootById(unsigned int const* SpaceObjId)
{
    unsigned int const Id = *SpaceObjId;
    std::list<FMissionLoot>::iterator It = MissionLoots.begin();
    for (; It != MissionLoots.end(); ++It)
    {
        if (It->SpaceObjId == Id)
            return &*It;
    }
    return nullptr;
}

// FUN_06f15560 (06f15560-06f155e5, RET 4): each +0xb8 record's ship slots [node+0xf4,
// node+0xf8) (stride 0x88), DACOM stricmp(slot+8, nickname chars); the first match is returned
// (06f155b8 re-tests the slot pointer, never null).
FMissionShip* FMissionObject::FindMissionShip(TString<0x30> const* Nickname)
{
    std::list<FMissionShipGroup>::iterator GroupIt = MissionShips.begin();
    for (; GroupIt != MissionShips.end(); ++GroupIt)
    {
        std::vector<FMissionShip>::iterator ShipIt = GroupIt->Ships.begin();
        for (; ShipIt != GroupIt->Ships.end(); ++ShipIt)
        {
            if (_stricmp(ShipIt->Nickname.Data, Nickname->Data) == 0)
                return &*ShipIt;
        }
    }
    return nullptr;
}

// FUN_06f155f0 (06f155f0-06f156a2, RET 4). A ship slot found by FUN_06f15560: lower_bound of
// its +0x38 id in +0x184 (FUN_06fa7200; end or a greater key -> 0), else the node's value +0x10
// (node+0x20, the [NPC] space_costume). No ship: the +0xc4 solars in order, DACOM stricmp of
// the record nickname chars (node+0x3f4) with the nickname chars; a match returns record +0x478.
Costume* FMissionObject::GetMissionCostume(TString<0x30> const* Nickname)
{
    FMissionShip* Ship = FindMissionShip(Nickname);
    if (Ship != nullptr)
    {
        std::map<unsigned int, FMsnNpc>::iterator NpcIt = MissionNpcs.lower_bound(Ship->NpcId.Id);
        if (NpcIt == MissionNpcs.end() || Ship->NpcId.Id < NpcIt->first)
            return nullptr;
        return &NpcIt->second.Appearance;
    }
    std::list<FMissionSolar>::iterator SolarIt = MissionSolars.begin();
    for (; SolarIt != MissionSolars.end(); ++SolarIt)
    {
        if (_stricmp(SolarIt->Nickname.Data, Nickname->Data) == 0)
            return &SolarIt->SpaceCostume;
    }
    return nullptr;
}

// FUN_06f159c0 (06f159c0-06f159e8, RET 4): the first +0xc4 node whose value+0 equals *ObjId,
// as the value pointer; end -> 0.
FMissionSolar* FMissionObject::FindSolarById(unsigned int const* ObjId)
{
    unsigned int const Id = *ObjId;
    std::list<FMissionSolar>::iterator It = MissionSolars.begin();
    for (; It != MissionSolars.end(); ++It)
    {
        if (It->SpaceObjId == Id)
            return &*It;
    }
    return nullptr;
}

// FUN_06f15c70 (06f15c70-06f15de1, RET 8). The local TString starts empty; each source is
// copied with FUN_06ea9f40 (clamp 0x2f) or the same inline copy for "Player".
TString<0x30> FMissionObject::GetMissionObjectName(unsigned int const* ObjId)
{
    TString<0x30> Name;
    Name.Length = 0;
    Name.Data[0] = '\0';
    // 06f15c7c-06f15cd2: FUN_06f7b0f0 find on +0xa4; a non-null NPC gives its nickname (+0xd8).
    std::map<unsigned int, FContentNpcShip*>::iterator NpcIt = NpcObjects.find(*ObjId);
    if (NpcIt != NpcObjects.end() && NpcIt->second != nullptr)
    {
        Name = NpcIt->second->Nickname.Data;
        return Name;
    }
    // 06f15cd7-06f15cee / 06f15d37-06f15d61: the first +0xc4 record with SpaceObjId == id.
    std::list<FMissionSolar>::iterator SolarIt = MissionSolars.begin();
    for (; SolarIt != MissionSolars.end(); ++SolarIt)
    {
        if (SolarIt->SpaceObjId == *ObjId)
        {
            Name = SolarIt->Nickname.Data;
            return Name;
        }
    }
    // 06f15cf0-06f15d35: no player state -> empty; the player's ship (cached state +0x10,
    // ship +4) -> "Player".
    if (FindContentPlayerState(PlayerId) == nullptr)
        return Name;
    if (PlayerState->ShipObject != nullptr && *ObjId == PlayerState->ShipObject->SpaceObjId)
    {
        Name = "Player";
        return Name;
    }
    // 06f15d63-06f15d94: the first +0xd0 record with SpaceObjId (node+0x3c) == id.
    std::list<FMissionLoot>::iterator LootIt = MissionLoots.begin();
    for (; LootIt != MissionLoots.end(); ++LootIt)
    {
        if (LootIt->SpaceObjId == *ObjId)
        {
            Name = LootIt->Nickname.Data;
            return Name;
        }
    }
    return Name;
}

// FUN_06f199f0 (06f199f0-06f19b74, RET 0x3c).
bool FMissionObject::MarkMissionObject(unsigned int ObjId, int Mode, TString<0x30> Name)
{
    bool bResult = true;
    // 06f199f6-06f19a0a: FUN_06f49b00(+0xc) null -> 1.
    if (FindContentPlayerState(PlayerId) == nullptr)
        return bResult;
    // 06f19a10-06f19a2e: MarkObj(cached state +0xc, id, mode); a nonzero result -> 0.
    if (pub::Player::MarkObj(PlayerState->PlayerId, ObjId, Mode) != 0)
        bResult = false;
    if (Mode == 0)
    {
        // 06f19a3b-06f19aab: the first +0x140 name equal to Name (strcmp) is unlinked.
        std::list<TString<0x30>>::iterator It = MarkObjNames.begin();
        for (; It != MarkObjNames.end(); ++It)
        {
            if (std::strcmp(It->Data, Name.Data) == 0)
                break;
        }
        if (It != MarkObjNames.end())
            MarkObjNames.erase(It);
        return bResult;
    }
    if (Mode == 1)
    {
        // 06f19ac6-06f19b5d: absent -> the name (0xd dwords) appended.
        std::list<TString<0x30>>::iterator It = MarkObjNames.begin();
        for (; It != MarkObjNames.end(); ++It)
        {
            if (std::strcmp(It->Data, Name.Data) == 0)
                break;
        }
        if (It == MarkObjNames.end())
            MarkObjNames.push_back(Name);
        return bResult;
    }
    // LAB_06f19b6d.
    return false;
}

// FUN_06f14980 (06f14980-06f149ec, RET 4): inline strcmp of Nickname's chars with each entry's
// npc +0xd8; the node value address is returned.
FMissionJumper* FMissionObject::FindJumperNpc(TString<0x30> const* Nickname)
{
    std::list<FMissionJumper>::iterator It = Jumpers.begin();
    for (; It != Jumpers.end(); ++It)
    {
        if (std::strcmp(Nickname->Data, It->Npc->Nickname.Data) == 0)
            return &*It;
    }
    return nullptr;
}

// FUN_06f14e30 (06f14e30-06f14e77, RET 4): unlink and free the first entry whose npc +4 equals
// *ObjId, size - 1.
void FMissionObject::RemoveJumperNpc(unsigned int const* ObjId)
{
    std::list<FMissionJumper>::iterator It = Jumpers.begin();
    for (; It != Jumpers.end(); ++It)
    {
        if (It->Npc->SpaceObjId == *ObjId)
        {
            Jumpers.erase(It);
            return;
        }
    }
}

// FUN_06f149f0 (06f149f0-06f14b2f, RET 4).
void FMissionObject::OnShipJump(unsigned int const* ShipId)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    FContentPlayerShip* PlayerShip = PlayerState->ShipObject;
    if (PlayerShip == nullptr)
        return;
    if (*ShipId == PlayerShip->SpaceObjId)
    {
        // 06f14a23-06f14a71: the player's target gate (+0x174) gives the destination; a failed
        // query does nothing. Every state-0 jumper: state 2, then out of its system
        // (FUN_06f34800(0, 0, 0)).
        if (pub::SpaceObj::GetJumpTarget(PlayerShip->TargetId, JumpSystemId, JumpGateId) != 0)
            return;
        std::list<FMissionJumper>::iterator It = Jumpers.begin();
        for (; It != Jumpers.end(); ++It)
        {
            if (It->State != 0)
                continue;
            It->State = 2;
            It->Npc->Relocate(nullptr, nullptr, nullptr);
        }
        return;
    }
    // 06f14a7a-06f14b26: a live mission NPC (FUN_06f7b0f0 on +0xa4, null value -> nothing): the
    // first jumper with its nickname (strcmp) gets state 1 and leaves its system.
    std::map<unsigned int, FContentNpcShip*>::iterator Found = NpcObjects.find(*ShipId);
    if (Found == NpcObjects.end() || Found->second == nullptr)
        return;
    std::list<FMissionJumper>::iterator It = Jumpers.begin();
    for (; It != Jumpers.end(); ++It)
    {
        if (std::strcmp(Found->second->Nickname.Data, It->Npc->Nickname.Data) != 0)
            continue;
        It->State = 1;
        It->Npc->Relocate(nullptr, nullptr, nullptr);
        return;
    }
}

// FUN_06f14b40 (06f14b40-06f14c06, RET 4). The x87 test at 06f14b5e-06f14b6c: FILD JumpDelay,
// FCOMP JumpTimer, TEST AH,0x41 + JP taken when neither C0 nor C3 is set = JumpDelay >
// JumpTimer -> add the frame time; otherwise (not greater) the jumpers come in.
void FMissionObject::UpdateJumpers(float DeltaTime)
{
    if (bJumpPending == 0)
        return;
    if (JumpTimer == -1.0f)
        return;
    if (static_cast<float>(JumpDelay) > JumpTimer)
    {
        JumpTimer = JumpTimer + DeltaTime;
        return;
    }
    // 06f14b72-06f14be2: every state-2 jumper jumps in at JumpGateId of JumpSystemId
    // (FUN_06f738c0), loses its directives (FUN_06f57740) and guide (FUN_06f34b70(0, 0, 0)),
    // slot 28 gets {npc id, system id}, state 0.
    std::list<FMissionJumper>::iterator It = Jumpers.begin();
    for (; It != Jumpers.end(); ++It)
    {
        if (It->State != 2)
            continue;
        FSystemInfo* JumpSystem = FindSystemInfo(&JumpSystemId);
        JumpSystem->AddJumpingInSpaceObj(It->Npc, JumpGateId);
        It->Npc->CancelDirectives();
        It->Npc->SetGuide(nullptr, false, false);
        unsigned int Arrival[2];
        Arrival[0] = It->Npc->SpaceObjId;
        Arrival[1] = JumpSystem->SystemId;
        Slot28(Arrival);
        It->State = 0;
    }
    // 06f14be6-06f14bea.
    bJumpPending = 0;
    JumpTimer = -1.0f;
}

// FUN_06f14c10 (06f14c10-06f14e2d, RET 4). The placement starts as the identity (FUN_06efc7a0,
// translation 0). Without a player state retail leaves the selection kind and the orientation
// unset (stack garbage); the port starts them at 0 / identity.
void FMissionObject::PlaceJumpersAtLaunch(unsigned int const* Parms)
{
    std::list<FMissionJumper>::iterator It = Jumpers.begin();
    for (; It != Jumpers.end(); ++It)
    {
        if (It->State != 1)
        {
            // 06f14e0f-06f14e14.
            if (It->State == 2)
                bJumpPending = 1;
            continue;
        }
        // 06f14c3c-06f14d3f: the anchor by the launching object's type (0x40 jump gate / hole:
        // 700 (DAT_06fb8ac8) above it; 0x800: at it) or the player's ship (selection 3).
        unsigned int Type = 0;
        pub::SpaceObj::GetType(Parms[3], Type);
        Transform Placement;
        Placement.Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        Placement.Translation.x = 0.0f;
        Placement.Translation.y = 0.0f;
        Placement.Translation.z = 0.0f;
        Vector Position;
        Position.x = 0.0f;
        Position.y = 0.0f;
        Position.z = 0.0f;
        Matrix Orientation(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        int Kind = 0;
        bool bAnchor = true;
        if ((Type & 0x40) != 0)
        {
            pub::SpaceObj::GetLocation(Parms[3], Position, Orientation);
            Position.z = Position.z + 700.0f;
            Kind = 0;
        }
        else if ((Type & 0x800) != 0)
        {
            pub::SpaceObj::GetLocation(Parms[3], Position, Orientation);
            Kind = 0;
        }
        else if (FindContentPlayerState(PlayerId) != nullptr)
        {
            pub::SpaceObj::GetLocation(PlayerState->GetShipId(), Position, Orientation);
            Kind = 3;
        }
        else
        {
            bAnchor = false;
        }
        if (bAnchor)
        {
            Placement.Translation = Position;
            Placement.Rotation = Orientation;
        }
        // 06f14d42-06f14dbd: GetEmptyPos from the placement, the object's radius x1.25
        // (DAT_06fb8ac4) as the start, the NPC's radius as the clearance; the radius calls'
        // centres are written into the position / a scratch vector. A failure leaves the
        // jumper in state 1.
        float LaunchRadius = 0.0f;
        pub::SpaceObj::GetRadius(Parms[3], LaunchRadius, Position);
        float NpcRadius = 0.0f;
        unsigned int NpcId = It->Npc->SpaceObjId;
        Vector NpcCentre;
        pub::SpaceObj::GetRadius(NpcId, NpcRadius, NpcCentre);
        float const StartRadius = LaunchRadius * 1.25f;
        pub::SpaceObj::PosSelectionType const Selection =
            static_cast<pub::SpaceObj::PosSelectionType>(Kind);
        if (pub::SpaceObj::GetEmptyPos(Parms[0], Placement, StartRadius, NpcRadius, Selection,
                                       Position) != 0)
            continue;
        // 06f14dbf-06f14e06: into the system (FUN_06f75f30 / FUN_06f34800), directives cancelled
        // (FUN_06f57740), slot 28 (+0x70) with {npc id, system id, object id}, state 0.
        It->Npc->Relocate(FindSystemInfo(&Parms[0]), &Position, &Orientation);
        It->Npc->CancelDirectives();
        unsigned int Arrival[3];
        Arrival[0] = It->Npc->SpaceObjId;
        Arrival[1] = Parms[0];
        Arrival[2] = Parms[3];
        Slot28(Arrival);
        It->State = 0;
    }
}

// FUN_06f145d0 (06f145d0-06f1476f, RET 4).
void FMissionObject::RespawnMissionObjects(bool bKeepHitPoints)
{
    // 06f145d6-06f14627: every +0x128 record, its slots' hit points -1 unless kept, then
    // FUN_06f15e20.
    std::list<FMissionShipGroup>::iterator GroupIt = RespawnShips.begin();
    for (; GroupIt != RespawnShips.end(); ++GroupIt)
    {
        if (!bKeepHitPoints)
        {
            std::vector<FMissionShip>::iterator SlotIt = GroupIt->Ships.begin();
            for (; SlotIt != GroupIt->Ships.end(); ++SlotIt)
                SlotIt->HitPoints = -1;
        }
        SpawnMissionShipGroup(&*GroupIt);
    }
    // 06f14629-06f1467b: every +0x8c formation through the first [MsnRandEnc] (+0x98) whose
    // NicknameId matches: its Params' FUN_06efba30 (dock target 0).
    std::list<FMissionRandEncRespawn>::iterator RespawnIt = RandEncRespawns.begin();
    for (; RespawnIt != RandEncRespawns.end(); ++RespawnIt)
    {
        std::list<FMissionRandEnc>::iterator EncIt = RandEncs.begin();
        for (; EncIt != RandEncs.end(); ++EncIt)
        {
            if (RespawnIt->EncounterId.Id != EncIt->NicknameId.Id)
                continue;
            EncIt->Params->SpawnFormation(&RespawnIt->Placement, RespawnIt->Count,
                                          &RespawnIt->Directives.Items, &RespawnIt->Choices,
                                          &RespawnIt->Names, RespawnIt->Priority, 0);
            break;
        }
    }
    // 06f1467d-06f146de: the first slot of every +0x128 record, when its NPC exists
    // (FUN_06f15710 on the slot nickname): its guide (FUN_06f34b70(guide, 1, 1)) or else its
    // directives at the NPC's priority (FUN_06f57840(&slot +0x58, FUN_06f57c80)).
    for (GroupIt = RespawnShips.begin(); GroupIt != RespawnShips.end(); ++GroupIt)
    {
        if (GroupIt->Ships.empty())
            continue;
        FMissionShip& First = GroupIt->Ships.front();
        FContentNpcShip* Npc = FindNpcByName(&First.Nickname);
        if (Npc == nullptr)
            continue;
        if (First.Guide.Pointer != nullptr)
            Npc->SetGuide(First.Guide.Pointer, true, true);
        else
            Npc->SetDirectives(&First.Directives.Items, Npc->GetPriority());
    }
    // 06f146e0-06f14769: both lists emptied (value dtors FUN_06f227e0 / FUN_06f229c0).
    RespawnShips.clear();
    RandEncRespawns.clear();
}

// FUN_06f148f0 (06f148f0-06f14971, RET 8): with the player's ship, every enabled encounter
// whose populator accepts the message (slot 3 +0xc on (Params, MessageId, parms)) has NpcCount
// zeroed, its Params spawned (slot 4 +0x10 with 1) and is disabled.
void FMissionObject::NotifyRandEncs(pub::Controller::EMessage MessageId, void const* MessageParms)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    if (PlayerState->ShipObject == nullptr)
        return;
    std::list<FMissionRandEnc>::iterator It = RandEncs.begin();
    for (; It != RandEncs.end(); ++It)
    {
        if (It->bEnabled == 0)
            continue;
        if (!It->Populator->Accept(It->Params, MessageId, MessageParms))
            continue;
        It->NpcCount = 0;
        It->Params->Spawn(true);
        It->bEnabled = 0;
    }
}

// FUN_06f153a0 (06f153a0-06f1548a, RET 4). Per record the arm walk stops at the first arm that
// held the id (erased member; an arm left without members unlinked and destroyed,
// FUN_06f227c0); then a record whose arm list is empty is cleared again (FUN_06f1dcd0) and its
// "%s_frm%d" counter (+0x2c) reset.
void FMissionObject::RemoveRandEncMember(unsigned int const* ObjId)
{
    std::list<FMissionRandEnc>::iterator EncIt = RandEncs.begin();
    for (; EncIt != RandEncs.end(); ++EncIt)
    {
        std::list<FMissionRandEncArm>::iterator ArmIt = EncIt->Arms.begin();
        for (; ArmIt != EncIt->Arms.end(); ++ArmIt)
        {
            std::list<unsigned int>::iterator MemberIt =
                std::find(ArmIt->MemberIds.begin(), ArmIt->MemberIds.end(), *ObjId);
            if (MemberIt == ArmIt->MemberIds.end())
                continue;
            ArmIt->MemberIds.erase(MemberIt);
            if (ArmIt->MemberIds.empty())
                EncIt->Arms.erase(ArmIt);
            break;
        }
        if (EncIt->Arms.empty())
        {
            EncIt->Arms.clear();
            EncIt->FormationIndex = 0;
        }
    }
}

// 06f17ee9-06f17f55. Params / Populator: the handles start null.
FMissionRandEnc::FMissionRandEnc()
    : bEnabled(0)
    , FormationIndex(0)
    , NpcCount(0)
{
    Nickname.Length = 0;
    Nickname.Data[0] = '\0';
    NicknameId.clear();
}

// FUN_06f21c50 (06f21c50-06f21d3b) and FUN_06ed3710 (06ed3710-06ed37c6) are the implicit copy
// constructor and destructor (MissionObject.h): the TRefHandle members AddRef on copy and
// Release on destruction in retail's member order.

// FUN_06f14e80 (06f14e80-06f15327, RET 4). Ported through LAB_06f15020: the encounter whose
// nickname id (+0x28) is in the npc's formation-id list (+0x17c) gets it; npc +0x1bc = 24.0f
// (DAT_06fb8588); an arm with the same +0x154 takes the npc id, else a new arm "%s_frm%d" is
// appended and the encounter's formation counter (+0x2c) grows; then the npc counter (+0x44)
// grows. Once it equals the Params' ShipsSpawned (+0x34) the encounter is complete: with an
// arrival object (Params +0x8c) its ships form a new formation picked by weight (the tail
// 06f15027-06f15324 below), then message 0x100c (Cnd_EncLaunched) and +0x44 = 0.
void FMissionObject::AttachRandEncNpc(void const* MessageParms)
{
    FContentNpcShip* Npc = *static_cast<FContentNpcShip* const*>(MessageParms);
    std::list<FMissionRandEnc>::iterator EncIt = RandEncs.begin();
    for (; EncIt != RandEncs.end(); ++EncIt)
    {
        bool bMember = false;
        std::list<unsigned int>::iterator IdIt = Npc->Field_0x17c.begin();
        for (; IdIt != Npc->Field_0x17c.end(); ++IdIt)
        {
            if (*IdIt == EncIt->NicknameId.Id)
            {
                bMember = true;
                break;
            }
        }
        if (bMember)
            break;
    }
    if (EncIt == RandEncs.end())
        return;

    // LAB_06f14f15.
    Npc->Field_0x1bc = 24.0f;
    std::list<FMissionRandEncArm>::iterator ArmIt = EncIt->Arms.begin();
    for (; ArmIt != EncIt->Arms.end(); ++ArmIt)
    {
        if (ArmIt->FormationRef == Npc->FormationId)
            break;
    }
    if (ArmIt != EncIt->Arms.end())
    {
        // LAB_06f1507d: the arm's id onto the npc list, the npc id into the arm.
        Npc->Field_0x17c.push_back(ArmIt->Id);
        ArmIt->MemberIds.push_back(Npc->SpaceObjId);
    }
    else
    {
        // 06f14f45-06f1501b.
        char FormationName[0x40];
        sprintf(FormationName, "%s_frm%d", EncIt->Nickname.Data, EncIt->FormationIndex);
        ID_String NameA;
        ID_String NameB;
        NameA.clear();
        NameB.clear();
        FMissionRandEncArm Arm;
        NameA.Id = CreateID(FormationName);
        NameB.Id = CreateID(FormationName);
        Arm.Name = NameA;
        Arm.FormationRef = Npc->FormationId;
        Arm.Id = NameB.Id;
        Arm.MemberIds.push_back(Npc->SpaceObjId);
        Npc->Field_0x17c.push_back(NameB.Id);
        EncIt->Arms.push_back(Arm);
        EncIt->FormationIndex = EncIt->FormationIndex + 1;
    }
    // LAB_06f15020-06f15031: one more ship; not all of the encounter's ships yet -> done.
    EncIt->NpcCount = EncIt->NpcCount + 1;
    if (static_cast<unsigned int>(EncIt->Params->ShipsSpawned) != EncIt->NpcCount)
        return;

    // 06f15037-06f15041: only with an arrival object the ships regroup.
    if ((EncIt->Params->DockTarget & 0x3fffffff) != 0)
    {
        // 06f15047-06f15162: every arm's ships (+0xec equal_range on the arm id; each looked
        // up in +0xa4, a miss giving null) into a local list.
        std::list<FContentShipBase*> Members;                              // FUN_06f93460
        std::list<FMissionRandEncArm>::iterator Arm = EncIt->Arms.begin();
        for (; Arm != EncIt->Arms.end(); ++Arm)
        {
            std::multimap<unsigned int, unsigned int>::iterator Member =
                FormationMembers.lower_bound(Arm->Id);                     // FUN_06fa7200
            for (; Member != FormationMembers.upper_bound(Arm->Id); ++Member)  // FUN_06ed1670
            {
                std::map<unsigned int, FContentNpcShip*>::iterator Object =
                    NpcObjects.find(Member->second);                       // FUN_06f7b0f0
                FContentNpcShip* Ship = Object == NpcObjects.end() ? nullptr : Object->second;
                Members.push_back(Ship);                                   // FUN_06f1e390
            }
        }

        if (Members.empty())
        {
            // LAB_06f15261: FUN_06f67090 on the new NPC.
            RemoveFromFormation(Npc);
        }
        else
        {
            // 06f15174-06f151b7: the weights (value +0x24) summed as floats.
            std::list<FRandEncFormation>& Formations = EncIt->Params->Formations;
            float Total = 0.0f;
            std::list<FRandEncFormation>::iterator Pack = Formations.begin();
            for (; Pack != Formations.end(); ++Pack)
                Total = static_cast<float>(static_cast<double>(Total) + Pack->Weight);
            // 06f151b9-06f151c8: FCOMP 0.0f, TEST AH,0x44 / JNP taken on equal -> nothing
            // picked. 06f151ce-06f15243: Roll = (float)(rand() * 2^-15 (DAT_06fb4448) * total);
            // the first pack whose running (float) sum reaches the roll (TEST AH,0x41 / JNP
            // taken on Roll <= sum). Nothing picked -> retail reads the list head's value slot.
            FRandEncFormation* Picked = nullptr;
            if (Total != 0.0f)
            {
                float const Roll = static_cast<float>(static_cast<double>(rand()) *
                                                      (1.0f / 32768.0f) * Total);
                float Sum = 0.0f;
                for (Pack = Formations.begin(); Pack != Formations.end(); ++Pack)
                {
                    Sum = static_cast<float>(static_cast<double>(Sum) + Pack->Weight);
                    if (Roll <= Sum)
                    {
                        Picked = &*Pack;
                        break;
                    }
                }
            }
            // 06f15245-06f15258: FUN_06f66ec0(&list, pack value +0, 0).
            // TODO(CNT-MS-OBJ): with nothing picked retail passes the list sentinel's value slot
            // (an indeterminate FFormation*); the port passes null.
            FormFormation(&Members, Picked != nullptr ? Picked->Formation : nullptr, false);
        }
        // 06f1526d-06f1527c: the local list freed.
    }

    // LAB_06f15281-06f15324: {the encounter's nickname id} as message 0x100c (Cnd_EncLaunched)
    // to this controller, then the ship count starts again.
    std::list<unsigned int> Launched;
    Launched.push_back(EncIt->NicknameId.Id);
    pub::Controller::_SendMessage(ControllerId, pub::Controller::EMessage::EncounterLaunched, &Launched);
    EncIt->NpcCount = 0;
}

// FUN_06f19c70 (06f19c70-06f19c97, RET 4): walk the +0x46c list for node+8 == *LabelId.
bool FMissionSolar::HasLabel(unsigned int const* LabelId) const
{
    std::list<unsigned int>::const_iterator It = LabelIds.begin();
    for (; It != LabelIds.end(); ++It)
    {
        if (*It == *LabelId)
            return true;
    }
    return false;
}

// FUN_06f19ca0 (06f19ca0-06f19cc7, RET 4): walk the +0xec list for node+8 == *LabelId.
bool FMissionLoot::HasLabel(unsigned int const* LabelId) const
{
    std::list<unsigned int>::const_iterator It = LabelIds.begin();
    for (; It != LabelIds.end(); ++It)
    {
        if (*It == *LabelId)
            return true;
    }
    return false;
}

// FUN_06f19b80 (06f19b80-06f19c6a, RET 8). All three lookups run first: FUN_06f7b0f0 find on
// +0xa4 (end -> no NPC, else the node value), the first +0xc4 solar with value+0 == ObjId, the
// first +0xd0 loot with value+0x34 == ObjId. Then, in order: the NPC's +0x17c id list
// (06f19bf9-06f19c10), the solar's +0x46c list (06f19c16-06f19c47, FUN_06f19c70 inlined with
// FUN_06f68e30 as the iterator step), the loot row through FUN_06f19ca0 (06f19c4d).
bool FMissionObject::HasObjectLabel(unsigned int ObjId, unsigned int const* LabelId)
{
    std::map<unsigned int, FContentNpcShip*>::iterator NpcIt = NpcObjects.find(ObjId);
    FContentNpcShip* Npc = NpcIt == NpcObjects.end() ? nullptr : NpcIt->second;

    FMissionSolar* Solar = nullptr;
    std::list<FMissionSolar>::iterator SolarIt = MissionSolars.begin();
    for (; SolarIt != MissionSolars.end(); ++SolarIt)
    {
        if (SolarIt->SpaceObjId == ObjId)
        {
            Solar = &*SolarIt;
            break;
        }
    }

    FMissionLoot* Loot = nullptr;
    std::list<FMissionLoot>::iterator LootIt = MissionLoots.begin();
    for (; LootIt != MissionLoots.end(); ++LootIt)
    {
        if (LootIt->SpaceObjId == ObjId)
        {
            Loot = &*LootIt;
            break;
        }
    }

    if (Npc != nullptr)
    {
        std::list<unsigned int>::iterator IdIt = Npc->Field_0x17c.begin();
        for (; IdIt != Npc->Field_0x17c.end(); ++IdIt)
        {
            if (*IdIt == *LabelId)
                return true;
        }
    }
    if (Solar != nullptr && Solar->HasLabel(LabelId))
        return true;
    if (Loot != nullptr && Loot->HasLabel(LabelId))
        return true;
    return false;
}

// FUN_06f15df0 (06f15df0-06f15e19, RET 4): linear search of [+0xe0, +0xe4).
bool FMissionObject::IsPlayerDead(unsigned int InPlayerId)
{
    std::vector<unsigned int>::iterator It = DeadPlayerIds.begin();
    for (; It != DeadPlayerIds.end(); ++It)
    {
        if (*It == InPlayerId)
            break;
    }
    return It != DeadPlayerIds.end();
}

// DAT_06fd070c: TU38's 3DMathEngine, created on first use (FUN_06fa8fa0) by the orientation
// keys of FUN_06f172f0 / FUN_06f18eb0 and by FUN_06f15e20 (06f16346-06f16383).
static I3DMathEngine* GMissionMathEngine = nullptr;

namespace
{
    // The 9 dwords of a rotation copied as one block (REP MOVSD 9).
    void CopyRotation(FMatrix* Dest, Matrix const* Source)
    {
        std::memcpy(Dest, Source, sizeof(FMatrix));
    }
}

// FUN_06f15e20 (06f15e20-06f1698f, RET 4). EH states 1..0xa: the parameter list (RAII), the
// block template (inlined dtor 06f15ecd-06f15f40), the two FmtStr of the name.
void FMissionObject::SpawnMissionShipGroup(FMissionShipGroup* Group)
{
    // 06f15e50-06f15e60: the placement starts as the record's orientation and position.
    FTransform Placement;
    Placement.Rotation = Group->Orientation;
    Placement.Translation.X = Group->Position.x;
    Placement.Translation.Y = Group->Position.y;
    Placement.Translation.Z = Group->Position.z;
    // [ESP+0x12]: set when the record's formation is unknown; nothing is created then.
    bool bNoFormation = false;
    std::list<FContentNpcShipParams> Blocks;                              // FUN_06ef6b80

    std::vector<FMissionShip>::iterator Ship = Group->Ships.begin();
    for (; Ship != Group->Ships.end(); ++Ship)
    {
        // 06f15ea2-06f15f40: a default block (FUN_06f33500) appended (FUN_06efc860).
        Blocks.push_back(FContentNpcShipParams());
        FContentNpcShipParams* Params = &Blocks.back();

        // 06f15f45-06f15f7a: the name block (two inline FmtStr ctors, Gender 0, Affiliation -1).
        FNpcNameParams Name;

        // 06f15f85-06f15fb5: the ship's "npc" block (+0x184, FUN_06f7b0f0 find).
        std::map<unsigned int, FMsnNpc>::iterator NpcIt = MissionNpcs.find(Ship->NpcId.Id);
        if (NpcIt == MissionNpcs.end())
        {
            // 06f167e4-06f16818: no such [NPC]: the new block is dropped again (the size test
            // is retail's).
            if (!Blocks.empty())
                Blocks.pop_back();
            continue;
        }
        FMsnNpc* Npc = &NpcIt->second;

        // 06f15fbb-06f160ab: the reputation handle of the block.
        if (Ship->bRandomName != 0)
        {
            // 06f15fd1-06f16048: the gender from the [NPC] voice's roles (1, then 2), else
            // from the faction (FUN_06f319b0).
            if (Npc->Voice == 0)
            {
                PickNpcNameGender(Npc->Affiliation, &Name);
            }
            else
            {
                FVoiceProperties* Voice = FindVoiceProperties(&Npc->Voice);  // FUN_06fa6e50
                if (Voice == nullptr)
                    PickNpcNameGender(Npc->Affiliation, &Name);
                else if (Voice->SupportsRoles(1))                             // FUN_06fa6e30
                    Name.Gender = 1;
                else if (Voice->SupportsRoles(2))
                    Name.Gender = 2;
                else
                    PickNpcNameGender(Npc->Affiliation, &Name);
            }
            // 06f1604b-06f16089: a default request (FUN_06f31650), the name (FUN_06f319d0 with
            // bNamedLeader 0), the old handle released, a new one from the name (FUN_06f31910).
            FNpcNameRequest Request;
            BuildNpcName(Npc->Affiliation, &Request, &Name, false);
            if (Params->ReputationHandle != nullptr)
            {
                Params->ReputationHandle->Release();
                Params->ReputationHandle = nullptr;
            }
            CreateNpcNameReputation(&Name, &Params->ReputationHandle);
        }
        else
        {
            // 06f16096-06f160ab: the old handle released, a new one from the [NPC] block
            // (FUN_06f31d40).
            if (Params->ReputationHandle != nullptr)
            {
                Params->ReputationHandle->Release();
                Params->ReputationHandle = nullptr;
            }
            CreateMsnNpcReputation(Npc, &Params->ReputationHandle);
        }

        // 06f160b0-06f160d5: the block's nickname (FUN_06ea9f40, max 0x30).
        Params->Nickname = Ship->Nickname.Data;

        // 06f160d8-06f16203: the npc_ship_arch from the mission's own table (+0x198,
        // FUN_06f32480 + FUN_06f348f0), else from npcships.ini (FUN_06f34a40).
        FNpcShipArch* Arch = FindNpcShipArch(&Npc->NpcShipArch.Id, &NpcShipArchs);
        if (Arch != nullptr)
        {
            ApplyNpcShipArch(Params, Arch, nullptr, nullptr);
        }
        else if (!ApplyNpcShipArchById(Params, &Npc->NpcShipArch.Id, nullptr, nullptr))
        {
            // 06f16209-06f1622a: unknown arch - the block is erased (FUN_06f1f4b0) and the
            // ship skipped.
            Blocks.pop_back();
            continue;
        }

        // 06f16105-06f16155: voice and space costume of the [NPC] block (the costume's four
        // parts, the count, and that many accessories).
        Params->Voice = Npc->Voice;
        Params->CommCostume.PartIds[0] = Npc->Appearance.PartIds[0];
        Params->CommCostume.PartIds[1] = Npc->Appearance.PartIds[1];
        Params->CommCostume.PartIds[2] = Npc->Appearance.PartIds[2];
        Params->CommCostume.PartIds[3] = Npc->Appearance.PartIds[3];
        Params->CommCostume.AccessoryCount = Npc->Appearance.AccessoryCount;
        for (int Index = 0; Index < static_cast<int>(Params->CommCostume.AccessoryCount); ++Index)
            Params->CommCostume.AccessoryIds[Index] = Npc->Appearance.AccessoryIds[Index];

        // 06f16155-06f161ad: no head or no voice -> the faction's picks (FUN_06f50650; costume
        // FUN_06f4f430, voice FUN_06f4f620) with the name's gender.
        if ((Npc->Appearance.PartIds[0] & 0x3fffffff) == 0 || (Npc->Voice & 0x3fffffff) == 0)
        {
            FFactionProps* Faction = GetFactionProps(&Npc->Affiliation);
            if (Faction != nullptr)
            {
                if ((Npc->Appearance.PartIds[0] & 0x3fffffff) == 0)
                    Faction->PickSpaceCostume(Name.Gender, &Params->CommCostume);
                if ((Npc->Voice & 0x3fffffff) == 0)
                    Faction->PickVoice(static_cast<unsigned int>(Name.Gender), &Params->Voice);
            }
        }

        // 06f161b2-06f161da: the ship's placement, identity and zero.
        FTransform Local;
        std::memset(&Local, 0, sizeof(Local));
        Local.Rotation.D[0][0] = 1.0f;                                    // FUN_06efc7a0
        Local.Rotation.D[1][1] = 1.0f;
        Local.Rotation.D[2][2] = 1.0f;
        if (Group->bRelPos == 0)
        {
            // 06f161e4-06f161f2: the record's orientation and position.
            Local.Rotation = Group->Orientation;
            Local.Translation.X = Group->Position.x;
            Local.Translation.Y = Group->Position.y;
            Local.Translation.Z = Group->Position.z;
        }
        else
        {
            // 06f1622f-06f16315: the anchor - the player's ship ("Player", stricmp) or the NPC
            // named by rel_pos (FUN_06f15710); identity and zero when there is none.
            FTransform Anchor;
            std::memset(&Anchor, 0, sizeof(Anchor));
            Anchor.Rotation.D[0][0] = 1.0f;
            Anchor.Rotation.D[1][1] = 1.0f;
            Anchor.Rotation.D[2][2] = 1.0f;
            FContentSpaceObj* AnchorObject = nullptr;
            if (_stricmp(Group->RelPosObj.Data, "Player") == 0)
            {
                if (FindContentPlayerState(PlayerId) != nullptr)
                    AnchorObject = PlayerState->ShipObject;
            }
            else
            {
                AnchorObject = FindNpcByName(&Group->RelPosObj);
            }
            if (AnchorObject != nullptr)
            {
                // FUN_06eb70f0 / FUN_06eb7130.
                Vector AnchorPosition;
                AnchorObject->GetPosition(&AnchorPosition);
                Anchor.Translation.X = AnchorPosition.x;
                Anchor.Translation.Y = AnchorPosition.y;
                Anchor.Translation.Z = AnchorPosition.z;
                Matrix AnchorOrientation;
                AnchorObject->GetOrientation(&AnchorOrientation);
                CopyRotation(&Anchor.Rotation, &AnchorOrientation);
            }

            if (Group->bOrientation == 0)
            {
                // 06f16321-06f16341: FUN_06f243d0 - placed at the heading / range and facing
                // away from the anchor.
                PlaceFacingAnchor(&Anchor, &Group->RelPosHeading, &Group->RelPosRange, &Local);
            }
            else
            {
                // 06f16343-06f163bc: the position only (DAT_06fd070c slot +0x28 on
                // (cos h * r, 0, -(sin h * r))); the rotation stays identity.
                FVector Offset;
                Offset.X = static_cast<float>(std::cos(static_cast<double>(Group->RelPosHeading)) *
                                              Group->RelPosRange);
                Offset.Y = 0.0f;
                Offset.Z = static_cast<float>(-(std::sin(static_cast<double>(Group->RelPosHeading)) *
                                                Group->RelPosRange));
                if (GMissionMathEngine == nullptr)
                    GMissionMathEngine = ContentAcquire3DMathEngine();
                FVector Position;
                GMissionMathEngine->TransformXform(Position, Anchor, Offset);
                Local.Translation = Position;
            }
        }

        // 06f163c0-06f1645b: in the player's system, a ship with a positive radius (FCOMP
        // 0.0f DAT_06fb444c, TEST AH,0x41 / JNZ: fall-through is greater) is moved to an empty
        // spot when its position is taken (IsPosEmpty; GetEmptyPos 3D volume, the radius as
        // start and clearance).
        if (FindContentPlayerState(PlayerId) != nullptr)
        {
            FSystemInfo* PlayerSystem = PlayerState->GetSystemRecord();
            if (PlayerSystem != nullptr && Ship->Radius > 0.0f)
            {
                unsigned int SystemId = PlayerSystem->SystemId;
                Vector Position;
                Position.x = Local.Translation.X;
                Position.y = Local.Translation.Y;
                Position.z = Local.Translation.Z;
                bool bEmpty = true;
                pub::SpaceObj::IsPosEmpty(SystemId, Position, Ship->Radius, bEmpty);
                if (!bEmpty)
                {
                    Transform Base;
                    std::memcpy(&Base.Rotation, &Local.Rotation, sizeof(FMatrix));
                    Base.Translation = Position;
                    pub::SpaceObj::PosSelectionType const Selection = pub::SpaceObj::POS_SEL_3D_VOLUME;
                    pub::SpaceObj::GetEmptyPos(SystemId, Base, Ship->Radius, Ship->Radius, Selection,
                                               Position);
                    Local.Translation.X = Position.x;
                    Local.Translation.Y = Position.y;
                    Local.Translation.Z = Position.z;
                }
            }
        }

        // 06f1645e-06f1649d: the first ship's placement becomes the formation's.
        if (Ship == Group->Ships.begin())
            Placement = Local;

        // 06f1649f-06f164de: position, orientation, creation type 0, the mission's controller.
        Params->Position.x = Local.Translation.X;
        Params->Position.y = Local.Translation.Y;
        Params->Position.z = Local.Translation.Z;
        std::memcpy(&Params->Orientation, &Local.Rotation, sizeof(FMatrix));
        Params->CreationType = 0;
        Params->ControllerId = ControllerId;
        // 06f164d6-06f16521: the labels (list assignment FUN_06fa8370 / FUN_06ed0230).
        Params->Field_0x118 = Ship->LabelIds;
        // 06f16526-06f16542: the record's +0xe0.
        Params->Field_0x110 = Group->Field_0xe0;
        // 06f16548-06f16639: the cargo (the vector assignment inlined; same 8-byte pairs).
        Params->Cargo.clear();
        std::vector<FMissionShipCargo>::iterator Cargo = Ship->Cargo.begin();
        for (; Cargo != Ship->Cargo.end(); ++Cargo)
        {
            pub::SpaceObj::CargoDesc Desc;
            Desc.GoodId = Cargo->GoodId;
            Desc.Count = static_cast<unsigned int>(Cargo->Count);
            Params->Cargo.push_back(Desc);
        }
        // 06f1663c-06f16650.
        Params->Priority = Ship->Priority;
        Params->HitPoints = Ship->HitPoints;

        // 06f16653-06f16662: the first mission ship slot of that nickname takes this slot's
        // jumper flag (FUN_06f15560).
        FMissionShip* Named = FindMissionShip(&Params->Nickname);
        if (Named != nullptr)
            Named->bJumper = Ship->bJumper;

        // 06f16665-06f16719: "init_objectives" -> the [ObjList]'s directives (+0x100): the old
        // ones released and erased, the list assigned (FUN_06f93490), each AddRef'ed -- the
        // FMissionDirectiveList assignment (FUN_06f20230) inlined.
        std::map<unsigned int, FMissionDirectiveList>::iterator ObjList =
            ObjLists.find(Ship->InitObjectives.Id);
        if (ObjList != ObjLists.end())
            Params->Directives = ObjList->second;

        // 06f1671d-06f167e2: "init_metabehavior" -> the [MetaBehavior] (+0x114, lower_bound +
        // key test): the directives released and erased, and the list's first object becomes
        // the block's guide description (the old one released, the new one AddRef'ed). Retail
        // reads the first node's value without an empty test.
        std::map<unsigned int, FMissionMetaBehaviorList>::iterator Meta =
            MetaBehaviors.find(Ship->InitMetaBehavior.Id);
        if (Meta != MetaBehaviors.end())
        {
            Params->Directives.Clear();                                         // FUN_06ed2a00
            FGuideDescBase* Desc = Meta->second.Items.front();
            if (Params->GuideDesc != Desc)
            {
                if (Params->GuideDesc != nullptr)
                {
                    Params->GuideDesc->Release();
                    Params->GuideDesc = nullptr;
                }
                Params->GuideDesc = Desc;
                if (Desc != nullptr)
                    Desc->AddRef();
            }
        }
        // 06f1681c-06f16842: ~FmtStr x2 (Name).
    }

    // 06f16866-06f168ad: a named formation lays the blocks out around the placement
    // (FUN_06efd1a0 twice, FUN_06f66c80); an unknown one stops the creation.
    if (!Group->Formation.IsEmpty())
    {
        if (FindFormation(&Group->Formation.Id) != nullptr)
            PlaceFormationBlocks(&Blocks, FindFormation(&Group->Formation.Id), &Placement);
        else
            bNoFormation = true;
    }

    // 06f168b2-06f168d4: the dock target - the record's +0xdc, or the "arrival_obj" solar
    // (FUN_06f159f0).
    unsigned int DockTarget = Group->ArrivalObjId;
    if (Group->ArrivalObj.Length != 0)
        DockTarget = GetSolarId(&Group->ArrivalObj);

    // 06f168d6-06f1691d: TU58 FUN_06f73810 in the record's system, else in the player's.
    if (Group->System != nullptr)
    {
        if (!bNoFormation)
            Group->System->CreateNpcShips(&Blocks, DockTarget, nullptr);
    }
    else if (FindContentPlayerState(PlayerId) != nullptr)
    {
        FSystemInfo* PlayerSystem = PlayerState->GetSystemRecord();
        if (PlayerSystem != nullptr && !bNoFormation)
            PlayerSystem->CreateNpcShips(&Blocks, DockTarget, nullptr);
    }
    // 06f16922-06f1696f: the blocks destroyed (FUN_06ef8790 per node) and the head freed.
}

// FUN_06f169a0 (06f169a0-06f16f93, RET 4). EH state 0: the empty FmtStr of the reputation name.
bool FMissionObject::SpawnMissionSolarRecord(FMissionSolar* Solar)
{
    // 06f169c3-06f169ed.
    if (FindContentPlayerState(PlayerId) == nullptr)
        return false;
    FSystemInfo* System = PlayerState->GetSystemRecord();
    if (System == nullptr)
        return false;

    // 06f169f3-06f16a5e: the SolarInfo template - ids and position 0, identity orientation
    // (FUN_06efc7a0), loadout 0, costume ctor FUN_06eac5b0, vibe -1, voice and base 0, +0x80 = 1,
    // hit points -1, empty nickname, +0xc8 / +0xcc = 0.
    pub::SpaceObj::SolarInfo Info;
    std::memset(&Info, 0, sizeof(Info));
    Info.Orientation.E[0][0] = 1.0f;
    Info.Orientation.E[1][1] = 1.0f;
    Info.Orientation.E[2][2] = 1.0f;
    Info.VibeGroupId = -1;
    Info.Field_0x80 = 1;
    Info.HitPts = -1;

    // 06f16a63-06f16aa5: the nickname copied (strcpy loop), the record's id = its nickname id
    // (pub::GetNicknameId), the mission's controller, the archetype, the player's system.
    FSystemInfo* PlayerSystem = PlayerState->GetSystemRecord();
    std::strcpy(Info.Nickname, Solar->Nickname.Data);
    Solar->SpaceObjId = pub::GetNicknameId(Info.Nickname);
    Info.Field_0x00 = ControllerId;
    Info.SolarArchID = Solar->ArchetypeId;
    Info.SystemID = PlayerSystem != nullptr ? PlayerSystem->SystemId : 0;

    // 06f16ac7-06f16b58: a solar with a positive radius (FCOMP 0.0f, TEST AH,0x41 / JNZ:
    // fall-through is greater) whose record position is taken moves to an empty spot (IsPosEmpty
    // on the record's position; GetEmptyPos 3D volume from the record's transform, the radius as
    // start and clearance).
    // Retail's gender slot below (the argument of FUN_06f4f7c0 at 06f16cdd) is the stack dword
    // [ESP+0x14] that holds System (06f169e9); the GetEmptyPos branch stores its
    // PosSelectionType 3 into the same dword (06f16b4a). FUN_06f4f7c0 overwrites it only when the
    // faction has voices, so otherwise FUN_06f4f430 compares this leftover (a heap pointer or 3)
    // with the costume genders (0/1/2) and never matches.
    int GenderSlot = static_cast<int>(reinterpret_cast<uintptr_t>(System));
    Vector Position = Solar->Position;
    if (PlayerSystem != nullptr && Solar->Radius > 0.0f)
    {
        unsigned int const SystemId = System->SystemId;
        bool bEmpty = true;
        pub::SpaceObj::IsPosEmpty(SystemId, Solar->Position, Solar->Radius, bEmpty);
        if (!bEmpty)
        {
            Transform Base;
            std::memcpy(&Base.Rotation, &Solar->Orientation, sizeof(FMatrix));
            Base.Translation = Solar->Position;
            pub::SpaceObj::PosSelectionType const Selection = pub::SpaceObj::POS_SEL_3D_VOLUME;
            GenderSlot = static_cast<int>(Selection);
            pub::SpaceObj::GetEmptyPos(SystemId, Base, Solar->Radius, Solar->Radius, Selection,
                                       Position);
        }
    }

    // 06f16b5b-06f16c26: the record's values.
    Info.Position = Position;
    Info.LoadoutID = Solar->LoadoutId;
    Info.VibeGroupId = static_cast<int>(Solar->Field_0x458);
    std::memcpy(&Info.Orientation, &Solar->Orientation, sizeof(FMatrix));
    Info.DockingBaseId = Solar->BaseId;
    Info.Field_0x80 = Solar->Visit;
    Info.CostumeHead = Solar->SpaceCostume.PartIds[0];
    Info.CostumeBody = Solar->SpaceCostume.PartIds[1];
    Info.CostumeLeftHand = Solar->SpaceCostume.PartIds[2];
    Info.CostumeRightHand = Solar->SpaceCostume.PartIds[3];
    Info.CostumeAccessoryCount = static_cast<int>(Solar->SpaceCostume.AccessoryCount);
    for (int Index = 0; Index < Info.CostumeAccessoryCount; ++Index)
        Info.CostumeAccessory[Index] = Solar->SpaceCostume.AccessoryIds[Index];
    Info.VoiceId = Solar->VoiceId;

    // 06f16c28-06f16cb0: the reputation - Alloc(vibe, the record's name, an empty FmtStr), then
    // its affiliation is the record's faction.
    {
        FmtStr Empty;
        pub::Reputation::Alloc(Info.VibeGroupId, Solar->Name, Empty);
    }
    pub::Reputation::SetAffiliation(Info.VibeGroupId, Solar->Faction);

    // 06f16cb6-06f16d20: no head or no voice -> the faction's picks (FUN_06f50650, gender
    // FUN_06f4f7c0, costume FUN_06f4f430, voice FUN_06f4f620). The gender starts as GenderSlot
    // (see above); PickGender writes it whenever the faction has voices.
    if ((Solar->SpaceCostume.PartIds[0] & 0x3fffffff) == 0 || (Solar->VoiceId & 0x3fffffff) == 0)
    {
        FFactionProps* Faction = GetFactionProps(&Solar->Faction);
        if (Faction != nullptr)
        {
            int Gender = GenderSlot;
            Faction->PickGender(&Gender);
            if ((Solar->SpaceCostume.PartIds[0] & 0x3fffffff) == 0)
            {
                // Retail passes &Info+0x40, which is laid out as a Costume; the pick writes it
                // only on success, so the copy round trip leaves Info as it was otherwise.
                Costume InfoCostume;
                InfoCostume.PartIds[0] = Info.CostumeHead;
                InfoCostume.PartIds[1] = Info.CostumeBody;
                InfoCostume.PartIds[2] = Info.CostumeLeftHand;
                InfoCostume.PartIds[3] = Info.CostumeRightHand;
                for (int Index = 0; Index < 8; ++Index)
                    InfoCostume.AccessoryIds[Index] = Info.CostumeAccessory[Index];
                InfoCostume.AccessoryCount = static_cast<unsigned int>(Info.CostumeAccessoryCount);
                Faction->PickSpaceCostume(Gender, &InfoCostume);
                Info.CostumeHead = InfoCostume.PartIds[0];
                Info.CostumeBody = InfoCostume.PartIds[1];
                Info.CostumeLeftHand = InfoCostume.PartIds[2];
                Info.CostumeRightHand = InfoCostume.PartIds[3];
                Info.CostumeAccessoryCount = static_cast<int>(InfoCostume.AccessoryCount);
                for (int Index = 0; Index < 8; ++Index)
                    Info.CostumeAccessory[Index] = InfoCostume.AccessoryIds[Index];
            }
            if ((Solar->VoiceId & 0x3fffffff) == 0)
                Faction->PickVoice(static_cast<unsigned int>(Gender), &Info.VoiceId);
        }
    }

    // 06f16d25-06f16d36.
    if (pub::SpaceObj::CreateSolar(Solar->SpaceObjId, Info) != 0)
        return false;

    // 06f16d3c-06f16da2: SetPersonalityParams with the "NOTHING" state graph, +0x10 / +0x14 = 0,
    // the record's pilot when it has one; SubmitState.
    pub::AI::SetPersonalityParams Op;
    Op.StateGraphId = pub::StateGraph::get_state_graph("NOTHING", pub::StateGraph::LEADER);
    Op.DirectiveCallbackObject = nullptr;
    Op.ContentCallbackObject = nullptr;
    if (Solar->bPilot != 0)
    {
        Op.bSetPersonality = 1;
        Op.Value = Solar->Personality;
    }
    pub::AI::SubmitState(Solar->SpaceObjId, &Op);
    // 06f16da2-06f16da8.
    Solar->bSpawned = 1;

    // 06f16daf-06f16de9: every label -> {label, id} into +0xec (FUN_06f003e0).
    std::list<unsigned int>::iterator Label = Solar->LabelIds.begin();
    for (; Label != Solar->LabelIds.end(); ++Label)
        FormationMembers.insert(std::make_pair(*Label, Solar->SpaceObjId));

    // 06f16deb-06f16e6c: [Invulnerables] rows of this nickname (strcmp).
    std::list<FMissionInvulnerable>::iterator InvIt = Invulnerables.begin();
    for (; InvIt != Invulnerables.end(); ++InvIt)
    {
        if (std::strcmp(Solar->Nickname.Data, InvIt->Nickname.Data) != 0)
            continue;
        pub::SpaceObj::SetInvincible(Solar->SpaceObjId, true, InvIt->bParam3 != 0, InvIt->Duration);
    }

    // 06f16e6e-06f16eee: [Decloaked] rows of this nickname -> ActivateEquipment {Type 2, 0}.
    std::list<TString<0x30>>::iterator DecIt = DecloakedNames.begin();
    for (; DecIt != DecloakedNames.end(); ++DecIt)
    {
        if (std::strcmp(Solar->Nickname.Data, DecIt->Data) != 0)
            continue;
        pub::SpaceObj::EQInfo EquipInfo;
        std::memset(&EquipInfo, 0, sizeof(EquipInfo));
        EquipInfo.Type = 2;
        EquipInfo.EquipId = 0;
        pub::SpaceObj::ActivateEquipment(Solar->SpaceObjId, &EquipInfo);
    }

    // 06f16ef0-06f16f65: the first [MarkObj] name equal to the nickname -> pub::Player::MarkObj
    // (player, id, 1).
    std::list<TString<0x30>>::iterator MarkIt = MarkObjNames.begin();
    for (; MarkIt != MarkObjNames.end(); ++MarkIt)
    {
        if (std::strcmp(MarkIt->Data, Solar->Nickname.Data) == 0)
            break;
    }
    if (MarkIt != MarkObjNames.end())
        pub::Player::MarkObj(PlayerId, Solar->SpaceObjId, 1);

    // 06f16f68-06f16f75: the system record keeps the id (FUN_06f743e0).
    PlayerSystem->AddMissionObject(Solar->SpaceObjId);
    return true;
}

// FUN_06f16fa0 (06f16fa0-06f1719d, RET 4).
void FMissionObject::SpawnMissionLoot(FMissionLoot* Loot)
{
    // 06f16fa9-06f16fcb.
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    if (PlayerState->GetSystemRecord() == nullptr)
        return;

    // 06f16fd1-06f16ffa: the player's system, +0x60 = 0, +0x61 = the record's +0xfa.
    Loot->Info.SystemID = PlayerState->GetSystemRecord()->SystemId;
    Loot->Info.bCanAiTractor = 0;
    Loot->Info.bMission = Loot->bCannotJettison;

    int Result;
    if (Loot->bRelPos != 0)
    {
        // 06f17006-06f1705d: the anchor - with a player ship, a ship label (FUN_06f15940), a
        // loot (FUN_06f15b10) or a solar (FUN_06f159f0); none -> nothing.
        unsigned int Anchor = 0;
        if (FindContentPlayerState(PlayerId) != nullptr && PlayerState->ShipObject != nullptr)
        {
            Anchor = GetLabelShipId(&Loot->RelPosObj);
            if ((Anchor & 0x3fffffff) == 0)
            {
                Anchor = GetLootId(&Loot->RelPosObj);
                if ((Anchor & 0x3fffffff) == 0)
                    Anchor = GetSolarId(&Loot->RelPosObj);
            }
        }
        if ((Anchor & 0x3fffffff) == 0)
            return;
        // 06f17063-06f170ab: GetLocation fails -> nothing; else the position is the anchor's
        // plus the offset (FUN_06f0bb80, float adds).
        Vector AnchorPosition;
        Matrix AnchorOrientation;
        if (pub::SpaceObj::GetLocation(Anchor, AnchorPosition, AnchorOrientation) != 0)
            return;
        Loot->Info.Position.x = AnchorPosition.x + Loot->RelPosOffset.x;
        Loot->Info.Position.y = AnchorPosition.y + Loot->RelPosOffset.y;
        Loot->Info.Position.z = AnchorPosition.z + Loot->RelPosOffset.z;
        Result = pub::SpaceObj::CreateLoot(Loot->SpaceObjId, Loot->Info);
    }
    else
    {
        // 06f170c3-06f170ce.
        Result = pub::SpaceObj::CreateLoot(Loot->SpaceObjId, Loot->Info);
    }
    if (Result != 0)
        return;

    // 06f170d9-06f1711f: created; every label -> {label, id} into +0xec (FUN_06f003e0).
    Loot->bSpawned = 1;
    std::list<unsigned int>::iterator Label = Loot->LabelIds.begin();
    for (; Label != Loot->LabelIds.end(); ++Label)
        FormationMembers.insert(std::make_pair(*Label, Loot->SpaceObjId));

    // 06f1711f-06f17194: [Invulnerables] rows of this nickname (strcmp).
    std::list<FMissionInvulnerable>::iterator InvIt = Invulnerables.begin();
    for (; InvIt != Invulnerables.end(); ++InvIt)
    {
        if (std::strcmp(Loot->Nickname.Data, InvIt->Nickname.Data) != 0)
            continue;
        pub::SpaceObj::SetInvincible(Loot->SpaceObjId, true, InvIt->bParam3 != 0, InvIt->Duration);
    }
}

// FUN_06f171e0 (06f171e0-06f172df, RET 8).
bool FMissionObject::SpawnMissionSolar(TString<0x30> const* Name, FTransform const* Placement)
{
    bool bResult = false;
    std::list<FMissionSolar>::iterator Solar = MissionSolars.begin();
    for (; Solar != MissionSolars.end(); ++Solar)
    {
        // 06f17210-06f17227: every record of that nickname (stricmp), not only the first.
        if (_stricmp(Solar->Nickname.Data, Name->Data) != 0)
            continue;
        // 06f1722d-06f17289: the record's transform kept; a given placement replaces it.
        Vector const SavedPosition = Solar->Position;
        FMatrix const SavedOrientation = Solar->Orientation;
        if (Placement != nullptr)
        {
            Solar->Position.x = Placement->Translation.X;
            Solar->Position.y = Placement->Translation.Y;
            Solar->Position.z = Placement->Translation.Z;
            Solar->Orientation = Placement->Rotation;
        }
        // 06f1728b-06f172c0: FUN_06f169a0, then the record's transform restored.
        bResult = SpawnMissionSolarRecord(&*Solar);
        Solar->Position = SavedPosition;
        Solar->Orientation = SavedOrientation;
    }
    return bResult;
}

// FUN_06f171a0 (06f171a0-06f171d4): node+0xc (record +4) = 0 for every +0xc4 node, then
// FUN_06f478c0 on +8; a record -> JMP FUN_06f746f0 on it.
void FMissionObject::DestroyMissionObjects()
{
    std::list<FMissionSolar>::iterator It = MissionSolars.begin();
    for (; It != MissionSolars.end(); ++It)
        It->bSpawned = 0;
    FSystemInfo* System = PlayerState->GetSystemRecord();
    if (System != nullptr)
        System->DestroyMissionObjects();
}

// FUN_06f14780 prefix (06f14780-06f148e0): FUN_06f003e0 insert {npc+4, npc} into +0xa4,
// then [Invulnerables] matches -> SetInvincible(npc+4, true, bool, float), then
// [Decloaked] matches -> ActivateEquipment(npc+4, {Type 2, flag 0}).
void FMissionObject::RegisterNpc(void const* MessageParms)
{
    FContentNpcShip* Npc = *static_cast<FContentNpcShip* const*>(MessageParms);
    unsigned int NpcId = Npc->SpaceObjId;
    NpcObjects.emplace(NpcId, Npc);

    std::list<FMissionInvulnerable>::iterator InvIt = Invulnerables.begin();
    for (; InvIt != Invulnerables.end(); ++InvIt)
    {
        if (std::strcmp(Npc->Nickname.Data, InvIt->Nickname.Data) != 0)
            continue;
        pub::SpaceObj::SetInvincible(NpcId, true, InvIt->bParam3 != 0, InvIt->Duration);
    }

    std::list<TString<0x30>>::iterator DecIt = DecloakedNames.begin();
    for (; DecIt != DecloakedNames.end(); ++DecIt)
    {
        if (std::strcmp(Npc->Nickname.Data, DecIt->Data) != 0)
            continue;
        pub::SpaceObj::EQInfo EquipInfo;
        std::memset(&EquipInfo, 0, sizeof(EquipInfo));
        EquipInfo.Type = 2;
        EquipInfo.EquipId = 0;
        pub::SpaceObj::ActivateEquipment(NpcId, &EquipInfo);
    }
}

// ---------------------------------------------------------------------------------------
// LoadMission (slot 25, FUN_06f172f0) and its records
// ---------------------------------------------------------------------------------------

// GMissionMathEngine (DAT_06fd070c) is defined above SpawnMissionShipGroup.

namespace
{
    // FmtStr::destroy then REP STOSD of the 6 dwords (06f1732a-06f17350).
    void ResetMissionFmtStr(FmtStr& Text)
    {
        Text.destroy();
        Text.Head = nullptr;
        Text.MadLibId = 0;
        for (int Index = 0; Index < FmtStr::KindCount; ++Index)
            Text.ValueIndex[Index] = 0;
    }

    // The orientation keys of FUN_06f172f0 (06f1764e-06f17728, 06f17b00-06f17bf6) and of
    // FUN_06f18eb0: values 2, 1, 0, 3 are read, the quaternion (W v0, X v1, Y v2, Z v3) is scaled
    // by 1.0f / FUN_06f18be0 (the length, x87 sum W^2 + X^2 + Y^2 + Z^2 then FSQRT) with each
    // product stored as float, and turned into a matrix by DAT_06fd070c's QuaternionToMatrix.
    void ReadMissionOrientation(INI_Reader* Reader, FMatrix* OutMatrix)
    {
        float const Y = Reader->get_value_float(2);
        float const X = Reader->get_value_float(1);
        float const W = Reader->get_value_float(0);
        float const Z = Reader->get_value_float(3);
        double const Length = std::sqrt(static_cast<double>(W) * W + static_cast<double>(X) * X +
                                        static_cast<double>(Y) * Y + static_cast<double>(Z) * Z);
        double const Inverse = 1.0 / Length;
        FQuaternion Quat;
        Quat.W = static_cast<float>(W * Inverse);
        Quat.X = static_cast<float>(X * Inverse);
        Quat.Y = static_cast<float>(Y * Inverse);
        Quat.Z = static_cast<float>(Z * Inverse);
        if (GMissionMathEngine == nullptr)
            GMissionMathEngine = ContentAcquire3DMathEngine();
        GMissionMathEngine->QuaternionToMatrix(*OutMatrix, Quat);
    }

    // [MsnLoot] values (06f17461-06f17872), written into the list's new last record.
    void LoadMissionLootValues(INI_Reader* Reader, FMissionLoot* Loot)
    {
        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                // 06f17483-06f174c4: FUN_06ed0160 (TString assignment), CreateID of the copy,
                // FUN_06f1e390 push_back into +0xec.
                Loot->Nickname = Reader->get_value_string(0);
                Loot->LabelIds.push_back(CreateID(Loot->Nickname.Data));
            }
            else if (Reader->is_value("archetype"))
            {
                // 06f174df-06f17532: a TString<0x40> copy (FUN_06ea9f40, null -> empty), then
                // pub::GetEquipmentID(+0x40, chars).
                TString<0x40> Name;
                Name = Reader->get_value_string(0);
                pub::GetEquipmentID(Loot->Info.EquipArchID, Name.Data);
            }
            else if (Reader->is_value("string_id"))
            {
                Loot->Info.NameId = static_cast<unsigned int>(Reader->get_value_int(0));
            }
            else if (Reader->is_value("position"))
            {
                Loot->Info.Position.x = Reader->get_value_float(0);
                Loot->Info.Position.y = Reader->get_value_float(1);
                Loot->Info.Position.z = Reader->get_value_float(2);
            }
            else if (Reader->is_value("rel_pos_obj"))
            {
                // 06f175ad-06f175f0: null -> empty; FUN_06ea9f40 max 0x30; +0xe4 = 1 either way.
                Loot->RelPosObj = Reader->get_value_string(0);
                Loot->bRelPos = 1;
            }
            else if (Reader->is_value("rel_pos_offset"))
            {
                Loot->RelPosOffset.x = Reader->get_value_float(0);
                Loot->RelPosOffset.y = Reader->get_value_float(1);
                Loot->RelPosOffset.z = Reader->get_value_float(2);
            }
            else if (Reader->is_value("orientation"))
            {
                // 06f1764e-06f17728: the matrix is copied to +0x64 (REP MOVSD 9).
                FMatrix Rotation;
                ReadMissionOrientation(Reader, &Rotation);
                std::memcpy(&Loot->Info.Orientation, &Rotation, sizeof(Rotation));
            }
            else if (Reader->is_value("velocity"))
            {
                Loot->Info.Velocity.x = Reader->get_value_float(0);
                Loot->Info.Velocity.y = Reader->get_value_float(1);
                Loot->Info.Velocity.z = Reader->get_value_float(2);
            }
            else if (Reader->is_value("angular_velocity"))
            {
                Loot->Info.AngularVelocity.x = Reader->get_value_float(0);
                Loot->Info.AngularVelocity.y = Reader->get_value_float(1);
                Loot->Info.AngularVelocity.z = Reader->get_value_float(2);
            }
            else if (Reader->is_value("equip_amount"))
            {
                Loot->Info.Units = static_cast<unsigned int>(Reader->get_value_int(0));
            }
            else if (Reader->is_value("health"))
            {
                Loot->Info.HitPtsScale = Reader->get_value_float(0);
            }
            else if (Reader->is_value("label"))
            {
                Loot->LabelIds.push_back(CreateID(Reader->get_value_string(0)));
            }
            else if (Reader->is_value("can_jettison"))
            {
                Loot->bCannotJettison = Reader->get_value_bool(0) ? 0 : 1;
            }
        }
    }

    // The [MsnSolar] orientation key (06f17afe-06f17bf9) inlines its own normalisation: values
    // 2, 1, 0, 3; value 2 is rounded to float first (FSTP float [ESP+0x10c]); the x87 sum runs
    // W^2 + Z^2 + Y^2 + X^2, FSQRT, 1.0f / length stored as float, and each component times that
    // float is stored as float (W v0, X v1, Y v2, Z v3). QuaternionToMatrix into a local, copied
    // to +0x41c.
    void ReadMissionSolarOrientation(INI_Reader* Reader, FMatrix* OutMatrix)
    {
        float const Y = Reader->get_value_float(2);
        float const X = Reader->get_value_float(1);
        float const W = Reader->get_value_float(0);
        float const Z = Reader->get_value_float(3);
        double const Sum = static_cast<double>(W) * W + static_cast<double>(Z) * Z +
                           static_cast<double>(Y) * Y + static_cast<double>(X) * X;
        float const Inverse = static_cast<float>(1.0 / std::sqrt(Sum));
        FQuaternion Quat;
        Quat.W = static_cast<float>(static_cast<double>(W) * Inverse);
        Quat.X = static_cast<float>(static_cast<double>(Inverse) * X);
        Quat.Y = static_cast<float>(static_cast<double>(Inverse) * Y);
        Quat.Z = static_cast<float>(static_cast<double>(Inverse) * Z);
        if (GMissionMathEngine == nullptr)
            GMissionMathEngine = ContentAcquire3DMathEngine();
        GMissionMathEngine->QuaternionToMatrix(*OutMatrix, Quat);
    }

    // [MsnSolar] values (06f179fa-06f17e9f). The nickname, faction, archetype, loadout, base and
    // label keys read the value with the no-index get_value_string (IAT 06fb3054).
    void LoadMissionSolarValues(INI_Reader* Reader, FMissionSolar* Solar)
    {
        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                // 06f17a1b-06f17a7f: null -> empty, FUN_06ea9f40 max 0x30; CreateID of the
                // copy pushed onto +0x46c.
                Solar->Nickname = Reader->get_value_string();
                Solar->LabelIds.push_back(CreateID(Solar->Nickname.Data));
            }
            else if (Reader->is_value("position"))
            {
                // 06f17a9a-06f17ade: values 1, 0, 2 read; stored x = v0, y = v1, z = v2.
                float const Y = Reader->get_value_float(1);
                float const X = Reader->get_value_float(0);
                float const Z = Reader->get_value_float(2);
                Solar->Position.x = X;
                Solar->Position.y = Y;
                Solar->Position.z = Z;
            }
            else if (Reader->is_value("orientation"))
            {
                ReadMissionSolarOrientation(Reader, &Solar->Orientation);
            }
            else if (Reader->is_value("faction"))
            {
                pub::Reputation::GetReputationGroup(Solar->Faction, Reader->get_value_string());
            }
            else if (Reader->is_value("string_id"))
            {
                Solar->Name.begin_mad_lib(static_cast<unsigned int>(Reader->get_value_int(0)));
                Solar->Name.end_mad_lib();
            }
            else if (Reader->is_value("pilot"))
            {
                // 06f17c73-06f17ca5: +0x3e0 = 1 only when FUN_06f3e020 returns exactly 1.
                unsigned int PilotId = CreateID(Reader->get_value_string(0));
                if (GetPilotPersonality(&PilotId, &Solar->Personality))
                    Solar->bPilot = 1;
            }
            else if (Reader->is_value("archetype"))
            {
                Solar->ArchetypeId = pub::GetNicknameId(Reader->get_value_string());
            }
            else if (Reader->is_value("loadout"))
            {
                pub::GetLoadoutID(Solar->LoadoutId, Reader->get_value_string());
            }
            else if (Reader->is_value("system"))
            {
                pub::GetSystemID(Solar->SystemId, Reader->get_value_string(0));
            }
            else if (Reader->is_value("base"))
            {
                pub::GetBaseID(Solar->BaseId, Reader->get_value_string());
            }
            else if (Reader->is_value("voice"))
            {
                pub::GetVoiceID(Solar->VoiceId, Reader->get_value_string(0));
            }
            else if (Reader->is_value("radius"))
            {
                Solar->Radius = Reader->get_value_float(0);
            }
            else if (Reader->is_value("label"))
            {
                Solar->LabelIds.push_back(CreateID(Reader->get_value_string()));
            }
            else if (Reader->is_value("costume"))
            {
                ParseSpaceCostume(&Solar->SpaceCostume, Reader);
            }
            else if (Reader->is_value("visit"))
            {
                // 06f17e39-06f17e8e: DACOM stricmp against a stack copy of "unknowable".
                if (_stricmp(Reader->get_value_string(0), "unknowable") == 0)
                    Solar->Visit = 0x80;
            }
        }
    }

    // [Mission] values (06f18832-06f18aff).
    void LoadMissionHeaderValues(INI_Reader* Reader, FMissionObject* Mission)
    {
        while (Reader->read_value())
        {
            if (Reader->is_value("mission_title"))
            {
                Mission->Field_0x50.begin_mad_lib(static_cast<unsigned int>(Reader->get_value_int(0)));
                Mission->Field_0x50.end_mad_lib();
            }
            else if (Reader->is_value("mission_offer"))
            {
                Mission->Field_0x68.begin_mad_lib(static_cast<unsigned int>(Reader->get_value_int(0)));
                Mission->Field_0x68.end_mad_lib();
            }
            else if (Reader->is_value("reward"))
            {
                Mission->Reward = static_cast<unsigned int>(Reader->get_value_int(0));
            }
            else if (Reader->is_value("npc_ship_file"))
            {
                // 06f188cf-06f18aee: a local INI_Reader (EH state 0x13), FUN_06f24280 (result
                // not tested), FUN_06f32510 into +0x198, close, then the reader's destructor.
                INI_Reader NpcShipReader;
                OpenContentDataIni(Reader->get_value_string(0), &NpcShipReader);
                LoadNpcShipArchs(&NpcShipReader, &Mission->NpcShipArchs);
                NpcShipReader.close();
            }
        }
    }

    // FUN_06f18eb0 (06f18eb0-06f199e0, RET 0xc): the values of one [MsnShip]/[MsnFormation]
    // section into Group, the new last record of Groups (+0xb8). The template ship slot goes to
    // the end of Group->Ships first (FUN_06f1da40, 06f18f9a-06f18fc1) and Current (local +0x10)
    // is that last slot; "formation" and "ship" drop it (Current = null). The keys that write
    // through Current have no null test (retail neither).
    void LoadMissionShipValues(INI_Reader* Reader, FMissionShipGroup* Group,
                               std::list<FMissionShipGroup>* Groups)
    {
        Group->Ships.push_back(FMissionShip());
        FMissionShip* Current = &Group->Ships.back();
        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                // 06f1908d-06f190d3: FUN_06ed0160 of the no-index value into the record, then of
                // the record chars into the slot; CreateID of the value pushed onto slot +0x3c
                // (FUN_06f1e390).
                Group->Nickname = Reader->get_value_string();
                Current->Nickname = Group->Nickname.Data;
                unsigned int const NicknameId = CreateID(Reader->get_value_string());
                Current->LabelIds.push_back(NicknameId);
            }
            else if (Reader->is_value("npc"))
            {
                Current->NpcId.Id = CreateID(Reader->get_value_string());
            }
            else if (Reader->is_value("random_name"))
            {
                Current->bRandomName = Reader->get_value_bool(0) ? 1 : 0;
            }
            else if (Reader->is_value("label"))
            {
                Current->LabelIds.push_back(CreateID(Reader->get_value_string()));
            }
            else if (Reader->is_value("init_objectives"))
            {
                // 06f19175-06f191a2: only while +0x64 is empty.
                if (Current->InitMetaBehavior.IsEmpty())
                    Current->InitObjectives.Id = CreateID(Reader->get_value_string(0));
            }
            else if (Reader->is_value("init_metabehavior"))
            {
                // 06f191b7-06f191e4: only while +0x54 is empty.
                if (Current->InitObjectives.IsEmpty())
                    Current->InitMetaBehavior.Id = CreateID(Reader->get_value_string(0));
            }
            else if (Reader->is_value("jumper"))
            {
                unsigned char const bJumper = Reader->get_value_bool(0) ? 1 : 0;
                Current->bJumper = bJumper;
                Current->bOriginalJumpFlag = bJumper;
            }
            else if (Reader->is_value("cargo"))
            {
                // 06f1921f-06f1926b: a failed pub::GetGoodID skips the key; the count is
                // ftol(value 1); FUN_06f96770 inserts one element at the end.
                unsigned int GoodId;
                if (pub::GetGoodID(GoodId, Reader->get_value_string(0)) == 0)
                {
                    FMissionShipCargo Entry;
                    Entry.GoodId = GoodId;
                    Entry.Count = static_cast<int>(Reader->get_value_float(1));
                    Current->Cargo.insert(Current->Cargo.end(), 1, Entry);
                }
            }
            else if (Reader->is_value("hit_points"))
            {
                int const HitPoints = Reader->get_value_int(0);
                Current->HitPoints = HitPoints;
                Current->Field_0x80 = HitPoints;
            }
            else if (Reader->is_value("radius"))
            {
                Current->Radius = Reader->get_value_float(0);
            }
            else if (Reader->is_value("system"))
            {
                // 06f192cd-06f19301: pub::GetSystemID, then FUN_06f75f30; only a found record is
                // stored.
                unsigned int SystemId;
                if (pub::GetSystemID(SystemId, Reader->get_value_string(0)) == 0)
                {
                    FSystemInfo* System = FindSystemInfo(&SystemId);
                    if (System != nullptr)
                        Group->System = System;
                }
            }
            else if (Reader->is_value("position"))
            {
                // 06f19316-06f19348: values 1, 0, 2 read; x = v0, y = v1, z = v2.
                float const Y = Reader->get_value_float(1);
                float const X = Reader->get_value_float(0);
                float const Z = Reader->get_value_float(2);
                Group->Position.x = X;
                Group->Position.y = Y;
                Group->Position.z = Z;
            }
            else if (Reader->is_value("rel_pos"))
            {
                // 06f1935d-06f193b9: +0xa4 = 1; value 0 times DAT_06fb6060 (0x3c8efa35, pi/180)
                // into +0x68; value 1 into +0x6c (null -> empty, FUN_06ea9f40 max 0x30); value 2
                // into +0xa0 unless it is empty.
                Group->bRelPos = 1;
                Group->RelPosHeading = Reader->get_value_float(0) * 0.0174532925f;
                Group->RelPosObj = Reader->get_value_string(1);
                if (!Reader->is_value_empty(2))
                    Group->RelPosRange = Reader->get_value_float(2);
            }
            else if (Reader->is_value("orientation"))
            {
                // 06f193d5-06f194a9: +0xa5 = 1; the normalisation is the [MsnSolar] one (values
                // 2, 1, 0, 3; value 2 rounded to float; W^2 + Z^2 + Y^2 + X^2); the matrix is
                // copied to +0x38 (REP MOVSD 9).
                Group->bOrientation = 1;
                FMatrix Rotation;
                ReadMissionSolarOrientation(Reader, &Rotation);
                std::memcpy(&Group->Orientation, &Rotation, sizeof(Rotation));
            }
            else if (Reader->is_value("arrival_obj"))
            {
                // 06f194bf-06f19539: every slot's +0x84 = 0 (dword store), value 0 into +0xa8
                // (null -> empty, max 0x30), value 1 into +0xe0 unless it is empty.
                std::vector<FMissionShip>::iterator ShipIt = Group->Ships.begin();
                for (; ShipIt != Group->Ships.end(); ++ShipIt)
                    ShipIt->Radius = 0.0f;
                Group->ArrivalObj = Reader->get_value_string(0);
                if (!Reader->is_value_empty(1))
                    Group->Field_0xe0 = Reader->get_value_int(1);
            }
            else if (Reader->is_value("formation"))
            {
                // 06f19551-06f195b0: with a Current slot the last slot is erased (FUN_06ed2860
                // copies nothing, FUN_06ed03d0 destroys it) and Current = null; then +0xe4.
                if (Current != nullptr)
                {
                    Group->Ships.pop_back();
                    Current = nullptr;
                }
                Group->Formation.Id = CreateID(Reader->get_value_string(0));
            }
            else if (Reader->is_value("ship"))
            {
                // 06f195cc-06f1962b: with a Current slot the last slot is erased (FUN_06ed34e0
                // copy-down loop, FUN_06ed03d0) and Current = null.
                if (Current != nullptr)
                {
                    Group->Ships.pop_back();
                    Current = nullptr;
                }
                // 06f19633-06f1967b: value 0 into a local TString<0x30> (null -> empty).
                TString<0x30> ShipName;
                ShipName = Reader->get_value_string(0);
                // 06f1967b-06f196f7: the first record of Groups whose chars (node+0xc) equal the
                // name (inline strcmp) ...
                std::list<FMissionShipGroup>::iterator It = Groups->begin();
                for (; It != Groups->end(); ++It)
                {
                    if (std::strcmp(ShipName.Data, It->Nickname.Data) == 0)
                        break;
                }
                if (It == Groups->end())
                    continue;
                // 06f196fc-06f19911: ... gives a copy of its first slot (node+0xf4) to the end of
                // Group->Ships: a local copy (inline copy ctor, EH states 9-0xd), inserted with
                // FUN_06ed12c0 (count 1) and destroyed (states 0x11/0xf/0x12/0xe) before the
                // record goes; 06f1994d-06f199b5: that record is unlinked, its slots destroyed
                // (FUN_06ed03d0), the node freed, size - 1.
                {
                    FMissionShip const Copy(It->Ships.front());
                    Group->Ships.insert(Group->Ships.end(), 1, Copy);
                }
                Groups->erase(It);
            }
        }
    }
}

// 06f17379-06f17403.
FMissionLoot::FMissionLoot()
    : SpaceObjId(0)
    , bSpawned(0)
    , bRelPos(0)
    , Field_0xe8(0)
    , SubObjId(SUBOBJ_ID_NONE)
    , bCannotJettison(0)
{
    Nickname.Length = 0;
    Nickname.Data[0] = '\0';
    // FUN_06f18c10: every dword 0 except the matrix diagonal (1.0f at +0x28/+0x38/+0x48 of the
    // LootInfo) and +0x60 = 1; +0x62/+0x63 are not written.
    std::memset(&Info, 0, sizeof(Info));
    Info.Orientation.E[0][0] = 1.0f;
    Info.Orientation.E[1][1] = 1.0f;
    Info.Orientation.E[2][2] = 1.0f;
    Info.bCanAiTractor = 1;
    Info.bMission = 0;
    RelPosObj.Length = 0;
    RelPosObj.Data[0] = '\0';
    RelPosOffset.x = 0.0f;
    RelPosOffset.y = 0.0f;
    RelPosOffset.z = 0.0f;
}

// 06f178b6-06f17984 (offsets after the PUSH at 06f17906 are one dword lower in the listing).
FMissionSolar::FMissionSolar()
    : SpaceObjId(0)
    , bSpawned(0)
    , bPilot(0)
    , Radius(150.0f)
    , ArchetypeId(0)
    , SystemId(0)
    , LoadoutId(0)
    , Field_0x458(-1)
    , Faction(0xffffffffu)
    , BaseId(0)
    , VoiceId(0)
    , Field_0x468(0)
    , Visit(0)
{
    Nickname.Length = 0;
    Nickname.Data[0] = '\0';
    // FUN_06efc7a0: identity 3x3.
    std::memset(&Orientation, 0, sizeof(Orientation));
    Orientation.D[0][0] = 1.0f;
    Orientation.D[1][1] = 1.0f;
    Orientation.D[2][2] = 1.0f;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    // FUN_06eac5b0: the four part ids and the accessory count (the accessories stay as they are).
    SpaceCostume.PartIds[0] = 0;
    SpaceCostume.PartIds[1] = 0;
    SpaceCostume.PartIds[2] = 0;
    SpaceCostume.PartIds[3] = 0;
    SpaceCostume.AccessoryCount = 0;
}

// FMissionDirectiveList's bodies (FUN_06f20230 copy, FUN_06ed2a00 Clear) are in
// ObjListDirective.h.

// FUN_06f20230 (the same body as the directive list copy): copy the nodes, AddRef each.
FMissionMetaBehaviorList::FMissionMetaBehaviorList(FMissionMetaBehaviorList const& Other)
    : Items(Other.Items)
{
    std::list<FMetaBehavior*>::iterator It = Items.begin();
    for (; It != Items.end(); ++It)
        (*It)->AddRef();
}

// 06f18560-06f1859b (and the +0x114 node value dtor in FUN_06f1e120): Release (slot 1) on
// every element, erase (FUN_06ecfec0), free the head (FUN_06f15350).
FMissionMetaBehaviorList::~FMissionMetaBehaviorList()
{
    std::list<FMetaBehavior*>::iterator It = Items.begin();
    for (; It != Items.end(); ++It)
        (*It)->Release();
    Items.clear();
}

// FUN_06f18eb0 06f18ed5-06f18f93.
FMissionShip::FMissionShip()
    : bJumper(0)
    , bOriginalJumpFlag(0)
    , Priority(0)
    , bRandomName(0)
    , HitPoints(-1)
    , Field_0x80(-1)
    , Radius(7.0f)
{
    Nickname.Length = 0;
    Nickname.Data[0] = '\0';
    NpcId.clear();
    InitObjectives.clear();
    InitMetaBehavior.clear();
}

// FUN_06ed2a60 (06ed2a60-06ed2bf7): +4 (0xd dwords), +0x38, the +0x3c list (FUN_06ed0230 insert
// of Other's range), +0x48/+0x49/+0x4c/+0x50/+0x54, the +0x58 list (FUN_06f20230), +0x64, +0x68
// handle with AddRef (slot 0) when set, the +0x6c vector (new of the size, element copy),
// +0x7c/+0x80/+0x84, vtable 06fb60a8. Unwind states (FuncInfo 06fbf610): 1 +0x3c, 3 +0x58,
// 4 +0x68 (FUN_06f28e10) - the member order below.
FMissionShip::FMissionShip(FMissionShip const& Other)
    : Nickname(Other.Nickname)
    , NpcId(Other.NpcId)
    , LabelIds(Other.LabelIds)
    , bJumper(Other.bJumper)
    , bOriginalJumpFlag(Other.bOriginalJumpFlag)
    , Priority(Other.Priority)
    , bRandomName(Other.bRandomName)
    , InitObjectives(Other.InitObjectives)
    , Directives(Other.Directives)
    , InitMetaBehavior(Other.InitMetaBehavior)
    , Guide(Other.Guide)
    , Cargo(Other.Cargo)
    , HitPoints(Other.HitPoints)
    , Field_0x80(Other.Field_0x80)
    , Radius(Other.Radius)
{
}

// FUN_06ed34e0 (06ed34e0-, RET 4): the nickname (TString assignment of Other's chars), +0x38,
// the +0x3c list assigned (skipped on self), +0x48..+0x54, the +0x58 directives, +0x64, the +0x68
// guide (old Release, new AddRef), the +0x6c vector, +0x7c..+0x84.
FMissionShip& FMissionShip::operator=(FMissionShip const& Other)
{
    Nickname = Other.Nickname.Data;
    NpcId = Other.NpcId;
    if (&LabelIds != &Other.LabelIds)
        LabelIds = Other.LabelIds;
    bJumper = Other.bJumper;
    bOriginalJumpFlag = Other.bOriginalJumpFlag;
    Priority = Other.Priority;
    bRandomName = Other.bRandomName;
    InitObjectives = Other.InitObjectives;
    Directives = Other.Directives;
    InitMetaBehavior = Other.InitMetaBehavior;
    // 06ed3656-06ed3675: the old guide released (slot 1) and cleared, then Other's stored and
    // AddRef'ed (slot 0); no same-pointer test. The handle's raw stores do not touch the count.
    if (Guide.Pointer != nullptr)
    {
        Guide.Pointer->Release();
        Guide = nullptr;
    }
    Guide = Other.Guide.Pointer;
    if (Guide.Pointer != nullptr)
        Guide.Pointer->AddRef();
    // 06ed3677: FUN_06ef8910 (vector assignment).
    Cargo = Other.Cargo;
    HitPoints = Other.HitPoints;
    Field_0x80 = Other.Field_0x80;
    Radius = Other.Radius;
    return *this;
}

// FUN_06ed2c20: vtable 06fb60a8, the body releases +0x68 (slot 1) when set and clears it
// (06ed2c48-06ed2c5e); then the members (FuncInfo 06fbf654): the +0x6c buffer freed, the +0x68
// handle (FUN_06f28e10 inlined 06ed2c73-06ed2c87, null by now), the +0x58 directives released
// and erased, the +0x3c nodes erased and freed.
FMissionShip::~FMissionShip()
{
    if (Guide.Pointer != nullptr)
    {
        Guide.Pointer->Release();
        Guide = nullptr;
    }
}

// FUN_06f0e2d0 (06f0e2d0-06f0e3d6). The matrix and the position are written twice by the ctor
// (06f0e2ef-06f0e31a and 06f0e383-06f0e3a4), with the same values.
FMissionShipGroup::FMissionShipGroup()
    : System(nullptr)
    , RelPosHeading(0.0f)
    , RelPosRange(2500.0f)
    , bRelPos(0)
    , bOrientation(0)
    , ArrivalObjId(0)
    , Field_0xe0(-1)
{
    Nickname.Length = 0;
    Nickname.Data[0] = '\0';
    std::memset(&Orientation, 0, sizeof(Orientation));
    Orientation.D[0][0] = 1.0f;
    Orientation.D[1][1] = 1.0f;
    Orientation.D[2][2] = 1.0f;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    RelPosObj.Length = 0;
    RelPosObj.Data[0] = '\0';
    ArrivalObj.Length = 0;
    ArrivalObj.Data[0] = '\0';
    Formation.clear();
    Formation.Id = CreateID("transport_liberty");
}

// FUN_06f172f0 (06f172f0-06f1789e, RET 4). reset, both FmtStr (+0x50, +0x68) destroyed and
// zeroed, then every header in order: MsnLoot (06f17364), MsnSolar (06f178a1), MsnShip and
// MsnFormation (06f17eaa/06f17ebf -> LAB_06f18b3b), MsnRandEnc (06f17ed4), ObjList (06f18230),
// MetaBehavior (06f183ef), Dialog (06f185a5), Mission (06f1881d), NNObjective (06f18b0a); any
// other header is skipped.
void FMissionObject::LoadMission(INI_Reader* Reader)
{
    Reader->reset();
    ResetMissionFmtStr(Field_0x50);
    ResetMissionFmtStr(Field_0x68);

    while (Reader->read_header())
    {
        if (Reader->is_header("MsnLoot"))
        {
            // 06f17379-06f1744f: the template goes to the end of +0xd0 (FUN_06f1e910), the keys
            // write that last record.
            MissionLoots.push_back(FMissionLoot());
            LoadMissionLootValues(Reader, &MissionLoots.back());
        }
        else if (Reader->is_header("MsnSolar"))
        {
            // 06f178b6-06f179f6: the template goes to the end of +0xc4 (FUN_06f1e830), the keys
            // write that last record.
            MissionSolars.push_back(FMissionSolar());
            LoadMissionSolarValues(Reader, &MissionSolars.back());
        }
        else if (Reader->is_header("MsnShip") || Reader->is_header("MsnFormation"))
        {
            // LAB_06f18b3b-06f18bcf: the FUN_06f0e2d0 template goes to the end of +0xb8
            // (FUN_06f1e750), then FUN_06f18eb0(Reader, &last record, &list).
            MissionShips.push_back(FMissionShipGroup());
            LoadMissionShipValues(Reader, &MissionShips.back(), &MissionShips);
        }
        else if (Reader->is_header("MsnRandEnc"))
        {
            // 06f17ee9-06f1822b: the stack record, filled by the section's values, copied to
            // the end of +0x98 (FUN_06f1e670 + FUN_06f21c50); the record's own references are
            // then released (06f181d3-06f1820d) and its arms freed (FUN_06ed37d0).
            FMissionRandEnc Record;
            while (Reader->read_value())
            {
                if (Reader->is_value("nickname"))
                {
                    // 06f17f83-06f17fca: FUN_06ea9f40 (max 0x20), CreateID of the value.
                    Record.Nickname = Reader->get_value_string(0);
                    Record.NicknameId.Id = CreateID(Reader->get_value_string(0));
                }
                else if (Reader->is_value("encounter_type"))
                {
                    // 06f17fe9-06f180d6: "TL_Attack" / "SP_Attack" (stricmp) replace both objects
                    // (the old ones Released) and the populator reads the rest of the section
                    // into the parameters (slot 4 +0x10). Any other type keeps the objects.
                    char const* Type = Reader->get_value_string(0);
                    char const* ParamsName = nullptr;
                    char const* PopulatorName = nullptr;
                    if (_stricmp(Type, "TL_Attack") == 0)
                    {
                        ParamsName = "TLParams";
                        PopulatorName = "TLPzr";
                    }
                    else if (_stricmp(Type, "SP_Attack") == 0)
                    {
                        ParamsName = "SPParams";
                        PopulatorName = "SPPzr";
                    }
                    if (ParamsName != nullptr)
                    {
                        if (Record.Params != nullptr)
                        {
                            Record.Params->Release();
                            Record.Params = nullptr;
                        }
                        CreateRandEncParams(ParamsName, &Record.Params.Pointer);   // FUN_06efb830
                        if (Record.Populator != nullptr)
                        {
                            Record.Populator->Release();
                            Record.Populator = nullptr;
                        }
                        CreateRandEncPopulator(PopulatorName, &Record.Populator.Pointer);  // FUN_06efb980
                        Record.Populator->Parse(Reader, Record.Params);
                    }
                    // 06f180d9-06f18179: the parameters' labels get the encounter's id and
                    // "Mission_Random_Encounter" (DAT_06fd068c); the mission's controller and
                    // player state go to +0xc / +0x10. Retail does this for every type and
                    // does not test the object.
                    Record.Params->Labels.push_back(Record.NicknameId.Id);
                    Record.Params->Labels.push_back(GIdMissionRandomEncounter);
                    Record.Params->ControllerId = ControllerId;
                    Record.Params->Player = PlayerState;
                }
            }
            RandEncs.push_back(Record);
        }
        else if (Reader->is_header("ObjList"))
        {
            // 06f18245-06f183ea: a local list and ID_String (cleared), FUN_06f25260 fills them,
            // the pair {id, list copy} goes into +0x100 (FUN_06f1e9f0, unique: a duplicate id
            // keeps the first list); the pair and the local list then release their references.
            FMissionDirectiveList Directives;
            ID_String Nickname;
            Nickname.clear();
            LoadObjList(Reader, &Directives.Items, &Nickname.Id);
            ObjLists.insert(std::map<unsigned int, FMissionDirectiveList>::value_type(Nickname.Id,
                                                                                    Directives));
        }
        else if (Reader->is_header("MetaBehavior"))
        {
            // 06f18404-06f185a0: a local list and ID_String (cleared), FUN_06f065e0 fills them,
            // the pair {id, list copy} goes into +0x114 (FUN_06f1e9f0, unique: a duplicate id
            // keeps the first list); the pair and the local list then release their references.
            FMissionMetaBehaviorList Objects;
            ID_String Nickname;
            Nickname.clear();
            LoadMetaBehavior(Reader, &Objects.Items, &Nickname.Id);
            MetaBehaviors.insert(
                std::map<unsigned int, FMissionMetaBehaviorList>::value_type(Nickname.Id, Objects));
        }
        else if (Reader->is_header("Dialog"))
        {
            // 06f185bc-06f18818: the first value is read without a test and must be "nickname"
            // (else the section is skipped); a local dialog (the mission's controller, empty
            // speakers, no system) is filled by FUN_06ef2380 and a copy inserted into
            // DAT_06fcf260 under CreateID(nickname) (FUN_06f1f2a0, unique: a duplicate id keeps
            // the first dialog); the pair and the local are then destroyed.
            Reader->read_value();
            if (Reader->is_value("nickname"))
            {
                unsigned int DialogId = CreateID(Reader->get_value_string(0));
                FMissionDialog Dialog;
                Dialog.ControllerId = ControllerId;
                Dialog.System = nullptr;
                Dialog.Load(Reader);
                GMissionDialogs.insert(
                    std::map<unsigned int, FMissionDialog>::value_type(DialogId, Dialog));
            }
        }
        else if (Reader->is_header("Mission"))
        {
            LoadMissionHeaderValues(Reader, this);
        }
        else if (Reader->is_header("NNObjective"))
        {
            // 06f18b1d-06f18b33: slot 6 (+0x18), its +8 list, FUN_06f1a630.
            LoadNNObjective(Reader, this, &GetFirstPlayer()->Objectives);
        }
    }
}

// ---------------------------------------------------------------------------------------
// FMissionNnPath (mission +0x21c)
// ---------------------------------------------------------------------------------------

namespace
{
    // The timer block FUN_06f1be20 inlines five times (06f1be6b-06f1be9d, 06f1bfb0-06f1bfe2,
    // 06f1c026-06f1c058, 06f1c270-06f1c29a, 06f1c357-06f1c389): TimerId = DAT_06fce76c++, then
    // pub::SetTimer(&mission +4, {TimerId, 0}, 1.0f).
    void StartNnPathTimer(FMissionNnPath* Path)
    {
        unsigned int const Id = GContentNextUniqueId;
        GContentNextUniqueId = Id + 1;
        Path->TimerId = Id;
        pub::Controller::TimerExpired Timer;
        Timer.Param1 = Id;
        Timer.Param2 = 0;
        pub::SetTimer(Path->Mission->ControllerId, Timer, 1.0f);
    }
}

// FUN_06f1be20 (06f1be20-06f1c39c, RET 8): returns +0x78 on every path. Inactive (+0x74 == 0)
// -> nothing. The two resolution blocks (06f1c07f-06f1c120 and 06f1c1d1-06f1c25b) are
// FUN_06f15b90 inlined (player state, ship, FUN_06f15940, the loot walk, FUN_06f159f0).
// T1 (06f1c25e) = Rebuild, SendObjectives(false, true), new timer; T2 (06f1c345) = Rebuild,
// SendObjectives(false, false), new timer.
int FMissionNnPath::HandleMessage(pub::Controller::EMessage MessageId, void const* MessageParms)
{
    using pub::Controller::EMessage;
    if (bActive == 0)
        return State;

    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    FMissionObject* Owner = Mission;
    bool bLaunched = false;
    switch (MessageId)
    {
    case EMessage::BaseExit:
        // 06f1bf25-06f1bf58.
        if (State == 3 && Owner->PlayerId == Parms[1] && (Owner->PlayerId & 0x3fffffff) != 0)
            State = 4;
        return State;

    case EMessage::TradelaneEnter:
        // 06f1beba-06f1bf18: docked with the player's ship.
        if (State != 3 || FindContentPlayerState(Owner->PlayerId) == nullptr)
            return State;
        if (Parms[0] != Owner->PlayerState->GetShipId() || (Parms[0] & 0x3fffffff) == 0)
            return State;
        State = 4;
        SendObjectives(true, false);
        return State;

    case EMessage::TradelaneExit:
        // 06f1bf64-06f1bff3: undocked.
        if (State != 4 || FindContentPlayerState(Owner->PlayerId) == nullptr)
            return State;
        if (Parms[0] != Owner->PlayerState->GetShipId() || (Parms[0] & 0x3fffffff) == 0)
            return State;
        StartNnPathTimer(this);
        State = 3;
        return State;

    case EMessage::Heartbeat:
        // 06f1be61-06f1beae: first heartbeat.
        if (State != 1)
            return State;
        StartNnPathTimer(this);
        State = 2;
        return State;

    case EMessage::SpaceEnter:
        // 06f1bfff-06f1c123.
        if (Owner->PlayerId != Parms[1] || (Owner->PlayerId & 0x3fffffff) == 0)
            return State;
        if (State == 4)
        {
            StartNnPathTimer(this);
            State = 3;
        }
        if (Owner->PlayerState->GetSystemId() == SystemId)
            TargetObjectId = Owner->GetLabelObjectId(&Label);
        bLaunched = true;
        break;

    case EMessage::SpaceExit:
        // 06f1c128-06f1c15a.
        if (Owner->PlayerId == Parms[1] && (Owner->PlayerId & 0x3fffffff) != 0)
            TargetObjectId = 0;
        return State;

    case EMessage::SystemExit:
        // 06f1c29f-06f1c307: FUN_06f1cb40 loop inlined, the legs erased, objectives resent.
        Objectives.Clear();
        Legs.clear();
        SendObjectives(false, false);
        return State;

    case EMessage::SystemEnter:
        // 06f1c17c-06f1c25b: system entered.
        if (Parms[2] != Owner->PlayerId || FindContentPlayerState(Owner->PlayerId) == nullptr)
            return State;
        if (Parms[0] != SystemId)
        {
            TargetObjectId = 0;
            return State;
        }
        if (Label.Length != 0)
            TargetObjectId = Owner->GetLabelObjectId(&Label);
        bLaunched = true;
        break;

    case EMessage::TimerExpired:
        // 06f1c30a-06f1c31b: this path's timer, not while docked.
        if (Parms[0] != TimerId || State == 4)
            return State;
        break;

    case EMessage::PlayerMissionChanged:
        // 06f1c31d-06f1c343.
        if (Parms[1] != Owner->PlayerId)
            return State;
        if (Parms[0] != Owner->ControllerId && (Parms[0] & 0x3fffffff) != 0)
            return State;
        break;

    default:
        return State;
    }

    Rebuild();
    SendObjectives(false, bLaunched);
    StartNnPathTimer(this);
    return State;
}

// FUN_06f1c3f0 (06f1c3f0-06f1c42a, RET 4): the float fields go out as doubles (FLD/FSTP double).
void FMissionNnPath::Save(FILE* File)
{
    fprintf(File, "nnpath = %u, %u, %f, %f, %f, %s, %u\n", TravelIds, TargetIds,
            static_cast<double>(Position.x), static_cast<double>(Position.y),
            static_cast<double>(Position.z), Label.Data, SystemId);
}

// FUN_06f1c430 (06f1c430-06f1c52c, RET 8): values 0/1 ints, floats read in the order 4, 3, 2
// into z, y, x; value 5 (when not empty) is the label (TString assignment, null -> empty);
// value 6 the system; then State = 1, bActive = 1, Mission = InMission.
void FMissionNnPath::Load(INI_Reader* Reader, FMissionObject* InMission)
{
    TravelIds = Reader->get_value_int(0);
    TargetIds = Reader->get_value_int(1);
    float const Z = Reader->get_value_float(4);
    float const Y = Reader->get_value_float(3);
    float const X = Reader->get_value_float(2);
    Position.x = X;
    Position.y = Y;
    Position.z = Z;
    if (!Reader->is_value_empty(5))
        Label = Reader->get_value_string(5);
    SystemId = static_cast<unsigned int>(Reader->get_value_int(6));
    State = 1;
    bActive = 1;
    Mission = InMission;
}

// FUN_06f1c530 (06f1c530-06f1c7b7). No player state or no system record -> nothing.
// Target (EBP) = 0. With a ship in the path's system: a label resolves TargetObjectId once
// (06f1c5ab-06f1c5f6 = FUN_06f15b90 inlined, GetRadius into +0x24 when found), then Target =
// TargetObjectId and GetLocation(Target) into +0x14 (called even for 0); Destination = +0x14.
// With a ship elsewhere, or without a ship outside the path's system: Target = FUN_06f29c10
// (gate path, then any path) and Destination = FUN_06f2a190 of it. Without a ship in the path's
// system: Destination = +0x14. Then the objectives are deleted (FUN_06f1cb40 inlined), the legs
// erased, and TU53 FUN_06f5ea40(System, &player position, &Destination, &Legs, 0x19,
// &(+0x24 + 200.0f), 0) builds the legs. Ids = TravelIds; one leg or none in the path's system
// -> Ids = TargetIds and, the first time, bArrived = 1 and State = 5. Mission slot 11 with
// (System, &Legs, this, TravelIds, Ids, Ids, Target, 8, 9, 8, null).
void FMissionNnPath::Rebuild()
{
    FMissionObject* Owner = Mission;
    if (FindContentPlayerState(Owner->PlayerId) == nullptr)
        return;
    FSystemInfo* System = Owner->PlayerState->GetSystemRecord();
    if (System == nullptr)
        return;

    unsigned int Target = 0;
    // Retail's stack slot is left as it was when FUN_06f2a190 finds no connection.
    Vector Destination = {0.0f, 0.0f, 0.0f};
    bool const bHasShip = Owner->PlayerState->ShipObject != nullptr;
    if (System->SystemId == SystemId)
    {
        if (bHasShip && Label.Length != 0)
        {
            if ((TargetObjectId & 0x3fffffff) == 0)
            {
                TargetObjectId = Owner->GetLabelObjectId(&Label);
                if ((TargetObjectId & 0x3fffffff) != 0)
                {
                    Vector Center;
                    pub::SpaceObj::GetRadius(TargetObjectId, TargetRadius, Center);
                }
            }
            Target = TargetObjectId;
            Matrix Orientation;
            pub::SpaceObj::GetLocation(Target, Position, Orientation);
        }
        Destination = Position;
    }
    else
    {
        Target = GetNextHopConnectionObject(System->SystemId, SystemId, 1);
        if ((Target & 0x3fffffff) == 0)
            Target = GetNextHopConnectionObject(System->SystemId, SystemId, 0);
        GetConnectionPosition(System->SystemId, Target, &Destination);
    }

    Objectives.Clear();
    Legs.clear();
    float const Radius = TargetRadius + 200.0f;
    // 06f1c721-06f1c756: FUN_06f47af0 (the player's position, its returned pointer passed on),
    // then TU53 FUN_06f5ea40 (result not read).
    Vector PlayerPosition;
    BuildNnPath(System, Owner->PlayerState->GetPosition(&PlayerPosition), &Destination, &Legs,
                0x19, &Radius, 0);

    int Ids = TravelIds;
    if (Legs.size() <= 1 && System->SystemId == SystemId)
    {
        Ids = TargetIds;
        if (bArrived == 0)
        {
            bArrived = 1;
            State = 5;
        }
    }
    Owner->BuildPathObjectives(System, &Legs, &Objectives, TravelIds, Ids, Ids, Target, 8, 9, 8,
                               nullptr);
}

// FUN_06f1c7c0 (06f1c7c0-06f1c979, RET 8). Count = size, less one with bSkipFirst; Count <= 0
// sends nothing. new[] Count MissionObjective (FUN_06f1a5d0: Type 1, empty FmtStr). Every
// objective (the first skipped with bSkipFirst) whose slot 3 succeeds gets a Type - the first
// one sent 0x3a when State is 2 or 5, 0x1a when bLaunched, else 0xa (both of the first two set
// State = 3), the later ones 9 - and is copied into the next array slot. The array goes to
// pub::Player::SetMissionObjectives with Count (not the number filled), then delete[].
void FMissionNnPath::SendObjectives(bool bSkipFirst, bool bLaunched)
{
    int const Count = static_cast<int>(Objectives.Objectives.size()) - (bSkipFirst ? 1 : 0);
    if (Count <= 0)
        return;

    std::vector<pub::Player::MissionObjective> Sent(static_cast<size_t>(Count));
    for (int Slot = 0; Slot < Count; ++Slot)
        Sent[static_cast<size_t>(Slot)].Type = 1;

    int Index = 0;
    std::list<FMissionObjective*>::iterator It = Objectives.Objectives.begin();
    for (; It != Objectives.Objectives.end(); ++It)
    {
        if (bSkipFirst && It == Objectives.Objectives.begin())
            continue;
        FMissionObjective* Objective = *It;
        if (!Objective->BuildDescription())
            continue;
        if (State == 2 || State == 5)
        {
            Objective->Objective.Type = Index != 0 ? 9u : 0x3au;
            State = 3;
        }
        else if (bLaunched)
        {
            Objective->Objective.Type = Index != 0 ? 9u : 0x1au;
            State = 3;
        }
        else
        {
            Objective->Objective.Type = Index != 0 ? 9u : 0xau;
        }
        Sent[static_cast<size_t>(Index)].Type = Objective->Objective.Type;
        Sent[static_cast<size_t>(Index)].Description = Objective->Objective.Description;
        ++Index;
    }

    pub::Player::SetMissionObjectives(Mission->PlayerId, Mission->ControllerId, Sent.data(),
                                      static_cast<unsigned int>(Count), Mission->Field_0x50,
                                      Mission->Field_0x80, Mission->Field_0x68);
}

// FUN_06f1c990 (06f1c990-06f1ca49): +0x74 = 0; every objective deleted (FUN_06f1a610 on a copy
// of the node value) and its node erased through vslot 3 (the FUN_06f1cb40 body, inlined); the
// +0x5c list emptied (FUN_06fa8370 begin..end); +0x20, +0x24 and the label cleared (the label
// twice: 06f1c9e5 and 06f1c9ee); +0x68 = +0x6c = 1; the position zeroed; +0x70, +0x78, +0x84,
// +0x7c, +0x80 = 0. +0x10 is not touched.
void FMissionNnPath::Reset()
{
    bActive = 0;
    Objectives.Clear();
    Legs.clear();
    TargetObjectId = 0;
    Label = static_cast<char const*>(nullptr);
    TargetRadius = 0.0f;
    Label = static_cast<char const*>(nullptr);
    TravelIds = 1;
    TargetIds = 1;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    SystemId = 0;
    State = 0;
    bArrived = 0;
    TimerId = 0;
    Mission = nullptr;
}
