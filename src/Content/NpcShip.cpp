// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU43 (06f33300-06f34ef0): the NPC ship (see NpcShip.h), and TU53's NPC ship
// event list DAT_06fd1bd8 (06f683e0-06f6895f).

#include "NpcShip.h"
#include "BaseDB.h"               // FContentBase::BaseId
#include "ContentInternal.h"      // GSpaceSupportControllerId, GContentNextUniqueId, GetContentGameTime
#include "ContentPlayerState.h"   // FContentPlayerStateBase
#include "DockQueues.h"           // RelocateShip (TU53 FUN_06f67740)
#include "FactionProps.h"         // GetFactionProps (TU48 FUN_06f50650)
#include "Gcs.h"                  // PickVoiceLine (TU25 FUN_06eeb010)
#include "Guides.h"               // FContentGuide, FGuideDesc, CreateGuide
#include "MissionObjList.h"       // CreateObjListDirective, the TU40 directive classes
#include "NpcShips.h"             // FNpcShipArch, FindNpcShipArch, GNpcShipArchs
#include "ObjListDirective.h"
#include "PilotPersonalities.h"   // GetPilotPersonalityWithJob
#include "Populators.h"           // GSpacePopControllerId
#include "RandomMissionDB.h"      // GetShipMissionProperties
#include "SystemInfo.h"
#include "ContentStaticIds.h"     // TU43's 16 static-init thunks 06f33300-06f334e0
#include "common_newchar.h"       // pub::StateGraph::get_state_graph
#include "common_server.h"
#include "common_server_controller.h"

#include <cstring>

namespace
{
    // FUN_06f34100's read: the dword at a fixed byte offset of the directive, the offset
    // chosen by its GetType() code alone (no cast in retail). MissionObjList.h pins every
    // class's member at these offsets with static_asserts.
    unsigned int ReadDirectiveDword(FObjListDirective const* Directive, size_t Offset)
    {
        unsigned int Value;
        memcpy(&Value, reinterpret_cast<unsigned char const*>(Directive) + Offset,
               sizeof(Value));
        return Value;
    }

    // Message 0x100e (ship_in_sights_event) to SpaceSupport.
    struct FNpcShipSightMsg
    {
        unsigned int ShipId;     // +0x00
        unsigned int ObjectId;   // +0x04
        unsigned int SystemId;   // +0x08
    };

    // Message 0x1002 (combat_drift_event) to SpaceSupport; 0x100a (left the system) to the
    // owning controller.
    struct FNpcShipSystemMsg
    {
        unsigned int ShipId;     // +0x00
        unsigned int SystemId;   // +0x04
    };

    // Message 0x1011 (tether_object_event) to the owning controller.
    struct FNpcShipTetherMsg
    {
        unsigned int ObjectId1;  // +0x00
        unsigned int ObjectId2;  // +0x04
        unsigned int Field_0x08; // +0x08 always 0
        unsigned char Field_0x0c;// +0x0c always 0
    };

    // Message 0x1008 (lead_object_event) to the owning controller.
    struct FNpcShipLeadMsg
    {
        unsigned int ObjectId1;  // +0x00
        unsigned int ObjectId2;  // +0x04
        unsigned int Progress;   // +0x08
        float Value;             // +0x0c
        Vector Position;         // +0x10
    };

    // Message 0x1006 (comm lines used up) to the owning controller.
    struct FNpcShipCommDoneMsg
    {
        unsigned int ShipId;     // +0x00
        unsigned int Field_0x04; // +0x04 a field of the first queued directive, or 0
        unsigned int Field_0x08; // +0x08 always 1
    };

    // Message 0x1004 (request_flee_destination) to SpaceSupport.
    struct FNpcShipFleeMsg
    {
        unsigned int ShipId;     // +0x00
        unsigned int Reason;     // +0x04
        unsigned int SystemId;   // +0x08
    };
}

// ---------------------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------------------

// FUN_06f33500 (06f33500-06f33698): TU20 parameters (FUN_06eb7260); +0x44 = 0, +0x48 /
// +0x4c = -1; +0x50 vector (FUN_06ead6a0); +0x60 list (FUN_06f1d4c0); +0x6c/+0x70/+0x74 = 0;
// byte +0x78 = 0; +0x7c = 0; +0x80 vector (+0x84..+0x8c = 0); +0xb4..+0xbc = 0; +0xc0 = -1;
// +0xc4..+0xd0 = 0; +0xf4..+0xfc = 0; +0x100 ID_String::clear; +0x104 = -1.0f
// (DAT_06fb956c), +0x108 = 24.0f, +0x10c = 500.0f, +0x110 = -1, +0x114 = 0; +0x118 list;
// Personality ctor; +0x4fc = 2, +0x500 = 0; +0x90..+0xb0 = the identity.
FContentNpcShipParams::FContentNpcShipParams()
{
    Pilot.clear();
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    CommCostume.PartIds[0] = 0;
    CommCostume.PartIds[1] = 0;
    CommCostume.PartIds[2] = 0;
    CommCostume.PartIds[3] = 0;
    CommCostume.AccessoryCount = 0;
    Orientation = Matrix(1.0f, 0.0f, 0.0f,
                         0.0f, 1.0f, 0.0f,
                         0.0f, 0.0f, 1.0f);
}

// TU58 FUN_06f732e0 06f73309-06f7349e: FUN_06f73680 on the TU50 block; +0x78, +0x7c; the
// cargo vector copied (FUN_06ed2700); the matrix (9 dwords), the position, +0xc0; the costume
// (0xd dwords), +0xf8; +0xfc through FUN_06f21fd0 (copy + AddRef slot 0 when non-null);
// +0x100..+0x114; the +0x118 list (FUN_06f0deb0); the personality (0xf6 dwords); +0x4fc, +0x500.
FContentNpcShipParams::FContentNpcShipParams(FContentNpcShipParams const& Other)
    : FContentShipParams(Other)
    , Field_0x78(Other.Field_0x78)
    , Loadout(Other.Loadout)
    , Cargo(Other.Cargo)
    , Orientation(Other.Orientation)
    , Position(Other.Position)
    , StateGraph(Other.StateGraph)
    , CommCostume(Other.CommCostume)
    , Voice(Other.Voice)
    , GuideDesc(Other.GuideDesc)
    , Pilot(Other.Pilot)
    , Field_0x104(Other.Field_0x104)
    , Field_0x108(Other.Field_0x108)
    , Field_0x10c(Other.Field_0x10c)
    , Field_0x110(Other.Field_0x110)
    , GuideTargetId(Other.GuideTargetId)
    , Field_0x118(Other.Field_0x118)
    , Personality(Other.Personality)
    , CreationType(Other.CreationType)
    , Level(Other.Level)
{
}

// FUN_06ef4bb0 (06ef4bb0-06ef4ce2): the +0x118 nodes erased (FUN_06ecfe80) and the head freed;
// GuideDesc Released (slot 1) and set to 0 when non-null (06ef4c16-06ef4c2d); the +0x84 buffer
// freed; then the TU50 block part (06ef4c54 onwards, FContentShipParams dtor). The port runs
// the same steps as member destructors in reverse declaration order: Field_0x118 (+0x118),
// Pilot, GuideDesc (FGuideDescHandle, FUN_06f28e10), Cargo (+0x80), then the base.
FContentNpcShipParams::~FContentNpcShipParams() = default;

// FUN_06f348f0 (06f348f0-06f34a3b).
void __cdecl ApplyNpcShipArch(FContentNpcShipParams* Params, FNpcShipArch const* Arch,
                              ID_String* Pilot, ID_String const* Job)
{
    if (Arch == nullptr)
        return;

    Params->Loadout = Arch->Loadout;
    Params->ShipArchId = Arch->ShipArchetype;
    GetShipMissionProperties(Arch->ShipArchetype, &Params->ClassIds);

    // 06f3491a-06f34970: an empty nickname takes the arch's (TString assign: null -> empty,
    // else clamped to 0x2f).
    if (Params->Nickname.Length == 0)
        Params->Nickname = Arch->Nickname.get_string();

    Params->StateGraph = Arch->StateGraph;

    // 06f3497b-06f349b5: each npc_class not yet in ClassIds is appended (insert(end, 1, v)).
    for (std::vector<unsigned int>::const_iterator Class = Arch->NpcClasses.begin();
         Class != Arch->NpcClasses.end(); ++Class)
    {
        std::vector<unsigned int>::iterator Found = Params->ClassIds.begin();
        while (Found != Params->ClassIds.end() && *Found != *Class)
            ++Found;
        if (Found == Params->ClassIds.end())
            Params->ClassIds.insert(Params->ClassIds.end(), 1, *Class);
    }

    // 06f349b7-06f349cc: the job id is a local ID_String (cleared, then *Job when given).
    ID_String JobId;
    JobId.clear();
    if (Job != nullptr)
        JobId = *Job;

    if (!Arch->Pilot.IsEmpty())
    {
        Params->Pilot = Arch->Pilot;
        GetPilotPersonalityWithJob(&Params->Pilot.Id, &JobId, &Params->Personality);
    }

    Params->Level = Arch->Level;

    if (Pilot != nullptr && !Pilot->IsEmpty())
    {
        Params->Pilot = *Pilot;
        GetPilotPersonalityWithJob(&Pilot->Id, &JobId, &Params->Personality);
    }
}

// FUN_06f34a40 (06f34a40-06f34a72): the arch lookup result is passed on even when null.
bool __cdecl ApplyNpcShipArchById(FContentNpcShipParams* Params, unsigned int const* ArchId,
                                  ID_String* Pilot, ID_String const* Job)
{
    FNpcShipArch* Arch = FindNpcShipArch(ArchId, &GNpcShipArchs);
    ApplyNpcShipArch(Params, Arch, Pilot, Job);
    return Arch != nullptr;
}

// FUN_06f34a80 (06f34a80-06f34acc). Retail: FP = FUN_06f50650(&FactionId); null -> false;
// FUN_06f4f430(FP, Param2, &Params->CommCostume); FUN_06f4f620(FP, (Param2 != 1) + 1,
// &Params->Voice); true. Both pick results are ignored.
bool __cdecl ApplyNpcShipFaction(unsigned int FactionId, int Param2, FContentNpcShipParams* Params)
{
    FFactionProps* FactionProps = GetFactionProps(&FactionId);
    if (FactionProps == nullptr)
        return false;
    FactionProps->PickSpaceCostume(Param2, &Params->CommCostume);
    FactionProps->PickVoice(static_cast<unsigned int>((Param2 != 1) ? 1 : 0) + 1, &Params->Voice);
    return true;
}

// ---------------------------------------------------------------------------------------
// The ship
// ---------------------------------------------------------------------------------------

// FUN_06f336a0 (06f336a0-06f33cea).
FContentNpcShip::FContentNpcShip(FContentNpcShipParams const* Params)
    : FContentShipBase(Params)
    , CreationKind(Params->CreationType)
    , Field_0x1bc(Params->Field_0x104)
    , Field_0x1c0(Params->Field_0x10c)
    , Loadout(Params->Loadout)
    , Field_0x1c8(Params->Field_0x108)
    , Field_0x5a8(Params->Field_0x78)
{
    // 06f336e8-06f337ac: the members above in retail order; both ID_Strings cleared.
    Field_0x1cc.clear();
    EncounterParamsId.clear();

    // 06f337c9: the TU20 base copied the handle raw; the NPC ship takes a reference
    // (released by the dtor). No null check.
    ReputationHandle->AddRef();

    // 06f337cf-06f33860: ShipInfo defaults — ids 0, both velocities 0 (FUN_06eb7810),
    // +0x54/+0x58 0, cargo list ctor, costume parts / count 0 (FUN_06eac5b0), +0xa4 -1,
    // +0xa8/+0xac 0, +0xb0 -1, +0xb4..+0xbc 0, the identity (FUN_06efc7a0).
    pub::SpaceObj::ShipInfo Info;
    Info.ControllerId = 0;
    Info.SystemID = 0;
    Info.ShipArchID = 0;
    Info.Position.x = 0.0f;
    Info.Position.y = 0.0f;
    Info.Position.z = 0.0f;
    Info.AngularVelocity.x = 0.0f;
    Info.AngularVelocity.y = 0.0f;
    Info.AngularVelocity.z = 0.0f;
    Info.Velocity.x = 0.0f;
    Info.Velocity.y = 0.0f;
    Info.Velocity.z = 0.0f;
    Info.Field_0x54 = 0;
    Info.LoadoutID = 0;
    Info.CostumeHead = 0;
    Info.CostumeBody = 0;
    Info.CostumeLeftHand = 0;
    Info.CostumeRightHand = 0;
    for (int Index = 0; Index < 8; ++Index)
        Info.CostumeAccessory[Index] = 0;   // retail leaves these unset (FUN_06eac5b0)
    Info.CostumeAccessoryCount = 0;
    Info.VibeGroupId = -1;
    Info.VoiceId = 0;
    Info.DockingBaseId = 0;
    Info.HitPts = -1;
    Info.Field_0xb4 = 0;
    Info.Field_0xb8 = 0;
    Info.ReputationRank = 0;
    Info.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                              0.0f, 1.0f, 0.0f,
                              0.0f, 0.0f, 1.0f);

    // 06f33865-06f338ea: arch, loadout, hit points, system id (no null check on the
    // system), position, orientation, zero velocities, the owning controller.
    Info.ShipArchID = ShipArchId;
    Info.LoadoutID = Params->Loadout;
    Info.HitPts = Params->HitPoints;
    Info.SystemID = Params->System->SystemId;
    Info.Position = Params->Position;
    Info.Orientation = Params->Orientation;
    Info.ControllerId = ControllerId;

    // 06f33908-06f33921: the docking base id, or 0.
    if (Params->Base != nullptr)
        Info.DockingBaseId = Params->Base->BaseId;
    else
        Info.DockingBaseId = 0;

    // 06f33921-06f339ad: a new CargoDesc per cargo entry, appended to the list (MetaList
    // node reuse through FreeList inlined).
    for (std::vector<pub::SpaceObj::CargoDesc>::const_iterator Cargo = Params->Cargo.begin();
         Cargo != Params->Cargo.end(); ++Cargo)
    {
        pub::SpaceObj::CargoDesc* Copy = new pub::SpaceObj::CargoDesc(*Cargo);
        Info.Cargo.append(Copy);
    }

    // 06f339af-06f33a15: the costume — four parts, then AccessoryCount accessories.
    Info.CostumeHead = Params->CommCostume.PartIds[0];
    Info.CostumeBody = Params->CommCostume.PartIds[1];
    Info.CostumeLeftHand = Params->CommCostume.PartIds[2];
    Info.CostumeRightHand = Params->CommCostume.PartIds[3];
    Info.CostumeAccessoryCount = static_cast<int>(Params->CommCostume.AccessoryCount);
    for (int Index = 0; Index < Info.CostumeAccessoryCount; ++Index)
        Info.CostumeAccessory[Index] = Params->CommCostume.AccessoryIds[Index];

    // 06f33a17-06f33a38: vibe = the parameters' handle reputation (FUN_06f33f80, no null
    // check), voice, rank = level.
    Info.VibeGroupId = Params->ReputationHandle->GetReputation();
    Info.VoiceId = Params->Voice;
    Info.ReputationRank = static_cast<unsigned int>(Params->Level);

    // 06f33a3f-06f33a95: list assignment (FUN_06fa8370 / FUN_06ed0230).
    Field_0x17c = Params->Field_0x118;

    // 06f33a9a-06f33aaf.
    CreationKind = Params->CreationType;
    pub::SpaceObj::Create(SpaceObjId, Info);

    // 06f33ab8-06f33adf: SetPersonalityParams with bHasPersonality = 1 and the parameters'
    // personality.
    pub::AI::SetPersonalityParams Personalize;
    Personalize.bSetPersonality = 1;
    Personalize.Value = Params->Personality;

    // 06f33ae1-06f33b32: Affiliation of the ship's handle (FUN_06eb6bb0) -> faction
    // properties (FUN_06f50650, no null check): ship +0x550 (Personality.JobParams.ScanForCargoCount)
    // = 0, then the archetypes of its scan_for_cargo list (+0x60, 8-byte pairs) into +0x554..
    // while fewer than 16. The personality copy below (06f33b34, 0xf6 dwords) overwrites all
    // of it again.
    unsigned int Affiliation = ReputationHandle->GetAffiliation();
    FFactionProps* FactionProps = GetFactionProps(&Affiliation);
    Personality.JobParams.ScanForCargoCount = 0;
    for (std::vector<FFactionScanCargo>::const_iterator It = FactionProps->ScanForCargo.begin();
         It != FactionProps->ScanForCargo.end(); ++It)
    {
        if (Personality.JobParams.ScanForCargoCount >= 16)
            break;
        Personality.JobParams.ScanForCargo[Personality.JobParams.ScanForCargoCount] = It->Archetype;
        ++Personality.JobParams.ScanForCargoCount;
    }
    Personality = Params->Personality;

    // 06f33b47-06f33b95: StateGraph (-1 -> "NOTHING"), both callbacks, SubmitState.
    int StateGraph = Params->StateGraph;
    if (StateGraph == -1)
        StateGraph = pub::StateGraph::get_state_graph("NOTHING", pub::StateGraph::LEADER);
    Personalize.StateGraphId = StateGraph;
    Personalize.ContentCallbackObject = static_cast<pub::AI::ContentCallback*>(this);
    Personalize.DirectiveCallbackObject = static_cast<pub::AI::DirectiveCallback*>(this);
    pub::AI::SubmitState(SpaceObjId, &Personalize);

    // 06f33b98-06f33bb1: priority 0 (FUN_06f57c90), then the parameters' directives.
    int ZeroPriority = 0;
    SetPriority(&ZeroPriority);
    SetDirectives(&Params->Directives.Items, 0);

    // 06f33bb6-06f33c74: the guide.
    FGuideDescBase* Desc = Params->GuideDesc;
    if (Desc != nullptr)
    {
        if (Desc->Type == 0)
        {
            if (Guide != nullptr)
            {
                Guide->Release();
                Guide = nullptr;
            }
        }
        else
        {
            Desc->Ship = this;
            bool bStart = true;
            if (Params->GuideTargetId != 0)
            {
                // The out-parameter is the stack slot that held the zero priority.
                unsigned int TargetType = 0;
                pub::SpaceObj::GetType(Params->GuideTargetId, TargetType);
                if ((TargetType & 0xc40) == 0)
                    bStart = false;
            }
            if (Guide != nullptr)
            {
                pub::AI::DirectiveCancelOp Cancel;
                SubmitDirective(&Cancel);
                if (Guide != nullptr)
                {
                    Guide->Release();
                    Guide = nullptr;
                }
            }
            // 06f33c51-06f33c74: Type != 0 -> CreateGuide(Desc, &Guide) (FUN_06f05f00); when
            // bStart and the directive list is empty, Guide->Start() (no null check).
            if (Desc->Type != 0)
            {
                CreateGuide(Desc, &Guide.Pointer);
                if (bStart && Directives.Items.empty())
                    Guide->Start();
            }
        }
    }

    // 06f33c77-06f33cbc: no flee destination.
    FleePosition.x = 0.0f;
    FleePosition.y = 0.0f;
    FleePosition.z = 0.0f;
    FleeState = 0;
    FleeObjectId = 0;
    FleeDistance = 0.0f;
}

// FUN_06f34680 (06f34680-06f347fd); deleting dtor 06f33d00. The reputation handle is
// released first (vtbl[+4], no null check, EH state 2); then the members: the +0x17c list
// (state 2 -> 1) and the +0x178 guide handle (state 1 -> 0, Release when set); the TU50 part
// (FUN_06f33d20, inlined in retail) follows as the base dtor. The port runs the two member
// steps as the destructors of Field_0x17c and Guide (declared in that order), as the unwind
// map FuncInfo 06fc3b8c does.
FContentNpcShip::~FContentNpcShip()
{
    ReputationHandle->Release();
}

// 06ea1460: MOV AL,1; RET.
bool FContentNpcShip::Slot04()
{
    return true;
}

// 06f33e70: LEA EAX,[ECX+0x1d0]; RET.
pub::AI::Personality* FContentNpcShip::GetPersonality()
{
    return &Personality;
}

// LAB_06f341f0 (06f341f0-06f3423b). Retail: FP = FUN_06f50650(&Affiliation of +8) (no null
// check); Out[n++] = FP+0x60 element key while n < MaxCount; returns n. The +0x60 list is
// the scan_for_cargo pairs (FFactionProps::ScanForCargo), the key their archetype.
int FContentNpcShip::Slot18(unsigned int* Out, int MaxCount)
{
    unsigned int Affiliation = ReputationHandle->GetAffiliation();
    FFactionProps* FactionProps = GetFactionProps(&Affiliation);
    int Count = 0;
    for (std::vector<FFactionScanCargo>::const_iterator It = FactionProps->ScanForCargo.begin();
         It != FactionProps->ScanForCargo.end(); ++It)
    {
        if (Count >= MaxCount)
            break;
        Out[Count] = It->Archetype;
        ++Count;
    }
    return Count;
}

// FUN_06f33f90 (06f33f90-06f33fc4): the TU50 handling first (FUN_06f57820), then, when the
// directive list is empty, the guide's slot +0x18 with the same arguments.
void FContentNpcShip::OnDirectiveComplete(int ResultWord11c, int ResultWord118)
{
    FContentShipBase::OnDirectiveComplete(ResultWord11c, ResultWord118);
    if (HasNoDirectives() && Guide != nullptr)
        Guide->OnDirectivesDone(ResultWord11c, ResultWord118);
}

// LAB_06f34250 (06f34250-06f343f7, RET 0x10).
//   +0x1b1 set, the event list's last tether status equal to Status, or no system: return
//   (no message). Otherwise the ship's voice (GetVoiceID); Status 1 / 2: comm line
//   "bc_enter_near_teth" / "bc_enter_far_teth" and StartTimer(1.0); Status 3:
//   "bc_exit_far_teth", SetCommLine and QueueTetherEvent(ObjectId2, 3, Position). A line
//   id with (id & 0x3fffffff) goes out through SendComm(ship, ObjectId2, id, 0, 0, 0, 0.5);
//   then message 0x1011 {ObjectId1, ObjectId2, 0, 0} to the owning controller.
void FContentNpcShip::tether_object_event(uint ObjectId1, uint ObjectId2, TetherStatusType Status,
                                          Vector const& Position)
{
    if (Field_0x1b1)
        return;
    if (GNpcShipEvents.LastTetherStatus == static_cast<unsigned int>(Status))
        return;
    FSystemInfo* Sys = GetSystem();
    if (Sys == nullptr)
        return;

    unsigned int LineId = 0;
    unsigned int ShipId = SpaceObjId;
    unsigned int Voice = 0;
    pub::SpaceObj::GetVoiceID(ShipId, Voice);

    // Each line: FUN_06eeb010(name, "" (DAT_06fb572c), Voice, &Line) == 0 -> LineId =
    // GetNicknameId(Line) (06f342e1-06f34303); on failure LineId stays 0.
    TString<0x40> Line;
    Line.Length = 0;
    Line.Data[0] = '\0';
    switch (static_cast<int>(Status))
    {
    case 1:     // "bc_enter_near_teth" (PTR 06fce5a0)
        if (PickVoiceLine("bc_enter_near_teth", "", Voice, &Line) == 0)
            LineId = pub::GetNicknameId(Line.Data);
        GNpcShipEvents.StartTimer(this, 1.0f);
        break;
    case 2:     // "bc_enter_far_teth" (PTR 06fce5a4)
        if (PickVoiceLine("bc_enter_far_teth", "", Voice, &Line) == 0)
            LineId = pub::GetNicknameId(Line.Data);
        GNpcShipEvents.StartTimer(this, 1.0f);
        break;
    case 3:     // "bc_exit_far_teth" (PTR 06fce5a8)
        if (PickVoiceLine("bc_exit_far_teth", "", Voice, &Line) == 0)
            LineId = pub::GetNicknameId(Line.Data);
        GNpcShipEvents.SetCommLine(this, LineId);
        GNpcShipEvents.QueueTetherEvent(this, ObjectId2, 3, &Position);
        break;
    default:
        break;
    }

    if ((LineId & 0x3fffffff) != 0)
        Sys->SendComm(SpaceObjId, ObjectId2, LineId, 0, 0, nullptr, 0.5f);

    FNpcShipTetherMsg Msg;
    Msg.ObjectId1 = ObjectId1;
    Msg.ObjectId2 = ObjectId2;
    Msg.Field_0x08 = 0;
    Msg.Field_0x0c = 0;
    unsigned int Controller = ControllerId;
    pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::TetherObject, &Msg);
}

// LAB_06f34400 (06f34400-06f34640, RET 0x14).
//   +0x1b1 set, the last lead progress equal to Progress, or no system: straight to the
//   message. Otherwise the voice, then by Progress (jump table 06f34644):
//     1 "bc_goto_pos", 2 "bc_goto_flat" (skipped when the last progress is 7): line lookup,
//       StartTimer(1.0);
//     3 "bc_goto_neg", 6 "bc_follow_pos_app_off", 7 "bc_follow_flat" (skipped when the last
//       progress is 2), 8 "bc_follow_neg": line lookup, SetCommLine, QueueLeadEvent;
//     4, 5: StartTimer(1.0) and straight to the message;
//   then SendComm as in tether_object_event, and message 0x1008 {ObjectId1, ObjectId2,
//   Progress, Value, Position} to the owning controller.
void FContentNpcShip::lead_object_event(uint ObjectId1, uint ObjectId2, LeadProgressType Progress,
                                        float Value, Vector const& Position)
{
    unsigned int const ProgressValue = static_cast<unsigned int>(Progress);
    if (!Field_0x1b1 && GNpcShipEvents.LastLeadProgress != ProgressValue)
    {
        FSystemInfo* Sys = GetSystem();
        if (Sys != nullptr)
        {
            unsigned int LineId = 0;
            unsigned int ShipId = SpaceObjId;
            unsigned int Voice = 0;
            pub::SpaceObj::GetVoiceID(ShipId, Voice);

            // Each line as in tether_object_event: FUN_06eeb010(name, "", Voice, &Line) == 0
            // -> LineId = GetNicknameId(Line).
            TString<0x40> Line;
            Line.Length = 0;
            Line.Data[0] = '\0';
            bool bSendComm = true;
            switch (static_cast<int>(ProgressValue))
            {
            case 1:     // "bc_goto_pos"
                if (PickVoiceLine("bc_goto_pos", "", Voice, &Line) == 0)
                    LineId = pub::GetNicknameId(Line.Data);
                GNpcShipEvents.StartTimer(this, 1.0f);
                break;
            case 2:     // "bc_goto_flat"
                if (GNpcShipEvents.LastLeadProgress == 7)
                    break;
                if (PickVoiceLine("bc_goto_flat", "", Voice, &Line) == 0)
                    LineId = pub::GetNicknameId(Line.Data);
                GNpcShipEvents.StartTimer(this, 1.0f);
                break;
            case 3:     // "bc_goto_neg"
            case 6:     // "bc_follow_pos_app_off"
            case 8:     // "bc_follow_neg"
            {
                char const* const Name = ProgressValue == 3 ? "bc_goto_neg" :
                                         ProgressValue == 6 ? "bc_follow_pos_app_off" :
                                                              "bc_follow_neg";
                if (PickVoiceLine(Name, "", Voice, &Line) == 0)
                    LineId = pub::GetNicknameId(Line.Data);
                GNpcShipEvents.SetCommLine(this, LineId);
                GNpcShipEvents.QueueLeadEvent(this, ObjectId2, ProgressValue, Value, &Position);
                break;
            }
            case 7:     // "bc_follow_flat"
                if (GNpcShipEvents.LastLeadProgress == 2)
                    break;
                if (PickVoiceLine("bc_follow_flat", "", Voice, &Line) == 0)
                    LineId = pub::GetNicknameId(Line.Data);
                GNpcShipEvents.SetCommLine(this, LineId);
                GNpcShipEvents.QueueLeadEvent(this, ObjectId2, ProgressValue, Value, &Position);
                break;
            case 4:
            case 5:
                GNpcShipEvents.StartTimer(this, 1.0f);
                bSendComm = false;
                break;
            default:
                break;
            }

            if (bSendComm && (LineId & 0x3fffffff) != 0)
                Sys->SendComm(SpaceObjId, ObjectId2, LineId, 0, 0, nullptr, 0.5f);
        }
    }

    FNpcShipLeadMsg Msg;
    Msg.ObjectId1 = ObjectId1;
    Msg.ObjectId2 = ObjectId2;
    Msg.Progress = ProgressValue;
    Msg.Value = Value;
    Msg.Position = Position;
    unsigned int Controller = ControllerId;
    pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::LeadObject, &Msg);
}

// LAB_06f34000 (06f34000-06f3403e, RET 4): message 0x100e {ship, ObjectId, system id} to
// SpaceSupport (DAT_06fd1c74).
void FContentNpcShip::ship_in_sights_event(uint ObjectId)
{
    FNpcShipSightMsg Msg;
    Msg.ShipId = SpaceObjId;
    Msg.ObjectId = ObjectId;
    Msg.SystemId = GetSystem()->SystemId;
    pub::Controller::_SendMessage(GSpaceSupportControllerId, pub::Controller::EMessage::ShipInSights,
                                  &Msg);
}

// LAB_06f34ce0 (06f34ce0-06f34eec, RET 0x18). By FleeState: 3 -> OutType 2, 2 -> OutType 1
// (OutParam = FleeObjectId, OutFloat = FleeDistance); 1 -> OutType 3 (OutPosition =
// FleePosition, OutFloat = FleeDistance); 0 -> the guide's slot +0x30; when it answers, a
// goto directive (type 5) to the destination becomes the active directive. Every path ends
// with message 0x1004 {ship, Reason, system id} to SpaceSupport. ObjectId is not read.
bool FContentNpcShip::request_flee_destination(uint ObjectId, FleeReason Reason,
                                               FleeDestinationType& OutType,
                                               Vector& OutPosition, uint& OutParam,
                                               float& OutFloat)
{
    (void)ObjectId;
    bool bResult = false;
    switch (FleeState)
    {
    case 0:
        if (Guide != nullptr)
        {
            bResult = Guide->RequestFleeDestination(OutType, OutPosition, OutParam, OutFloat);
            if (bResult)
            {
                // 06f34dbd-06f34e17: kind 1 / 2 -> the object's position (GetLocation);
                // kind 3 -> OutPosition; other kinds leave it unset in retail.
                Vector Destination;
                Destination.x = 0.0f;
                Destination.y = 0.0f;
                Destination.z = 0.0f;
                int Kind = static_cast<int>(OutType);
                if (Kind > 0)
                {
                    if (Kind <= 2)
                    {
                        Matrix Orientation;
                        pub::SpaceObj::GetLocation(OutParam, Destination, Orientation);
                    }
                    else if (Kind == 3)
                    {
                        Destination = OutPosition;
                    }
                }
                // 06f34e1b-06f34e95: a ref-holding local (the ObjectId parameter slot reused,
                // EH state 0 of FuncInfo LAB_06faebf8, released by FUN_06f28e10 on unwind) =
                // FUN_06f24870(5) (a GotoVec); FUN_06f26460(&Destination, OutFloat, -1.0f
                // DAT_06fb9570, 0, 0, 0) on it (no null check); then ActiveDirective = it
                // (unless the same: the old one released, the new one AddRef'd) and the
                // local released.
                TRefHandle<FObjListDirective> Directive;
                CreateObjListDirective(5, &Directive.Pointer);
                static_cast<FObjListGotoVec*>(Directive.Pointer)
                    ->SetDestination(&Destination, OutFloat, -1.0f, 0, false, false);
                if (ActiveDirective != Directive.Pointer)
                {
                    if (ActiveDirective != nullptr)
                    {
                        ActiveDirective->Release();
                        ActiveDirective = nullptr;
                    }
                    ActiveDirective = Directive.Pointer;
                    if (ActiveDirective != nullptr)
                        ActiveDirective->AddRef();
                }
            }
        }
        break;
    case 1:
        OutType = static_cast<FleeDestinationType>(3);
        OutPosition = FleePosition;
        OutFloat = FleeDistance;
        bResult = true;
        break;
    case 2:
        OutType = static_cast<FleeDestinationType>(1);
        OutParam = FleeObjectId;
        OutFloat = FleeDistance;
        bResult = true;
        break;
    case 3:
        OutType = static_cast<FleeDestinationType>(2);
        OutParam = FleeObjectId;
        OutFloat = FleeDistance;
        bResult = true;
        break;
    default:
        break;
    }

    FNpcShipFleeMsg Msg;
    Msg.ShipId = SpaceObjId;
    Msg.Reason = static_cast<unsigned int>(Reason);
    Msg.SystemId = GetSystem()->SystemId;
    pub::Controller::_SendMessage(GSpaceSupportControllerId, pub::Controller::EMessage::FleeRequest,
                                  &Msg);
    return bResult;
}

// LAB_06f34ad0 (06f34ad0-06f34ae4, RET 4): forwarded to the guide (tail JMP [vtbl+0x2c]).
void FContentNpcShip::scan_state_change(bool bScanning)
{
    if (Guide != nullptr)
        Guide->OnScanStateChange(bScanning);
}

// LAB_06f34090 (06f34090-06f340f0, RET 0xc): block {Type, Param1, Param2, system id};
// byte +0x11c = (Type == 0); message 0x1001 to controller 0.
void FContentNpcShip::combat_state_notification(CSNType Type, uint Param1, uint Param2)
{
    FSpaceObjCombatStateMsg Msg;
    Msg.Type = static_cast<unsigned int>(Type);
    Msg.Param1 = Param1;
    Msg.Param2 = Param2;
    Msg.SystemId = GetSystem()->SystemId;
    bInCombat = (Type == 0) ? 1 : 0;
    unsigned int Controller = 0;
    pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::CombatState, &Msg);
}

// LAB_06f34050 (06f34050-06f34086): message 0x1002 {ship, system id} to SpaceSupport.
void FContentNpcShip::combat_drift_event()
{
    FNpcShipSystemMsg Msg;
    Msg.ShipId = SpaceObjId;
    Msg.SystemId = GetSystem()->SystemId;
    pub::Controller::_SendMessage(GSpaceSupportControllerId, pub::Controller::EMessage::CombatDrift,
                                  &Msg);
}

// LAB_06f34cb0 (06f34cb0-06f34cd3): +0x1b4 0 -> 2, 2 -> 0, 1 -> 1, else 3.
pub::AI::ContentCallback::CreationType FContentNpcShip::get_creation_type()
{
    if (CreationKind == 0)
        return CreationType2;
    if (CreationKind == 2)
        return CreationType0;
    return CreationKind == 1 ? CreationType1 : CreationType3;
}

// FUN_06f33e80 (06f33e80-06f33e93).
bool FContentNpcShip::IsSpacePopShip() const
{
    return ControllerId == GSpacePopControllerId;
}

// FUN_06f33ea0 (06f33ea0-06f33ed2): +0x188 = 1; +0x190 = *Position; +0x18c = *Distance.
void FContentNpcShip::SetFleePosition(Vector const* Position, float const* Distance)
{
    FleeState = 1;
    FleePosition = *Position;
    FleeDistance = *Distance;
}

// FUN_06f33ee0 (06f33ee0-06f33f17): +0x19c = *ObjectId; +0x18c = *Distance; +0x188 =
// bType3 ? 3 : 2.
void FContentNpcShip::SetFleeObject(unsigned int const* ObjectId, float const* Distance, bool bType3)
{
    FleeObjectId = *ObjectId;
    FleeDistance = *Distance;
    FleeState = bType3 ? 3 : 2;
}

// FUN_06f33f20 (06f33f20-06f33f39): +0x1a8 = FUN_06efec10() (FSTP double); +0x1a0 = Player.
void FContentNpcShip::SetLastHailPlayer(FContentPlayerState* Player)
{
    LastHailTime = GetContentGameTime();
    LastHailPlayer = Player;
}

// FUN_06f33f40 (06f33f40-06f33f78): +0x190 = 0 vector, +0x188 = 0, +0x19c = 0, +0x18c = 0.
void FContentNpcShip::ClearFlee()
{
    FleePosition.x = 0.0f;
    FleePosition.y = 0.0f;
    FleePosition.z = 0.0f;
    FleeState = 0;
    FleeObjectId = 0;
    FleeDistance = 0.0f;
}

// FUN_06f33fd0 (06f33fd0-06f33fec): byte +0x1b1 = Value; non-zero -> JMP FUN_06f68600
// (ECX = DAT_06fd1bd8, the ship as its argument).
void FContentNpcShip::SetField1b1(bool Value)
{
    Field_0x1b1 = Value;
    if (Value)
        GNpcShipEvents.ClearTimer(this);
}

// FUN_06f33ff0: MOV AL,[ECX+0x1b1]; RET.
bool FContentNpcShip::GetField1b1() const
{
    return Field_0x1b1;
}

// FUN_06f34100 (06f34100-06f341e1). --CommLinesLeft; > 0 -> done. Otherwise block {ship,
// 0, 1}; when the directive list is not empty, its FIRST directive's GetType() picks the
// second dword: 3/4/5 -> directive +0x17c, 7 -> +0x10c, 8/9 -> +0x1c8; message 0x1006 to
// the owning controller. Retail reads the raw offsets by type code, with no cast: GetType 5
// is reported by GotoVec and also by the Guide / GuideShip directives (their vtables share
// 06ee99a0), whose +0x17c is GotoParams.Field_64 (float bits); no directive reports 8 or 9.
// The offsets are pinned in MissionObjList.h.
void FContentNpcShip::OnCommLineDone()
{
    --CommLinesLeft;
    if (CommLinesLeft > 0)
        return;

    FNpcShipCommDoneMsg Msg;
    Msg.ShipId = SpaceObjId;
    Msg.Field_0x04 = 0;
    Msg.Field_0x08 = 1;
    if (!Directives.Items.empty())
    {
        FObjListDirective* First = Directives.Items.front();
        int DirectiveType = First->GetType();
        if (DirectiveType == 3 || DirectiveType == 4 || DirectiveType == 5)
            Msg.Field_0x04 = ReadDirectiveDword(First, 0x17c);
        else if (DirectiveType == 7)
            Msg.Field_0x04 = ReadDirectiveDword(First, 0x10c);
        else if (DirectiveType == 8 || DirectiveType == 9)
            Msg.Field_0x04 = ReadDirectiveDword(First, 0x1c8);
    }
    unsigned int Controller = ControllerId;
    pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::Msg0x1006, &Msg);
}

// FUN_06f34800 (06f34800-06f348de, RET 0xc).
void FContentNpcShip::Relocate(FSystemInfo* NewSystem, Vector const* Position,
                               Matrix const* Orientation)
{
    if (System != nullptr)
        System->RemoveSpaceObj(SpaceObjId, false);

    // 06f34819-06f34865: the new system's id, or 0; leaving every system reports message
    // 0x100a {ship, old system id} to the owning controller.
    unsigned int NewSystemId = 0;
    if (NewSystem != nullptr)
    {
        NewSystemId = NewSystem->SystemId;
    }
    else if (System != nullptr)
    {
        FNpcShipSystemMsg Msg;
        Msg.ShipId = SpaceObjId;
        Msg.SystemId = System->SystemId;
        unsigned int Controller = ControllerId;
        pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::NpcSystemExit, &Msg);
    }

    // 06f34868-06f348ba: Position / Orientation copied into locals when given (retail
    // leaves them unset otherwise; the port starts them at zero / identity -- never read:
    // the only callers passing null, TU38 06f14a67 / 06f14b26, also pass no system, and
    // pub::SpaceObj::Relocate ignores them for system 0, Server 06cfa459), then
    // FUN_06f67740(&ship id, &NewSystemId, &pos, &orient) — TU53 RelocateShip:
    // pub::SpaceObj::Relocate plus SpaceSupport's dock queues.
    Vector LocalPosition;
    LocalPosition.x = 0.0f;
    LocalPosition.y = 0.0f;
    LocalPosition.z = 0.0f;
    Matrix LocalOrientation(1.0f, 0.0f, 0.0f,
                            0.0f, 1.0f, 0.0f,
                            0.0f, 0.0f, 1.0f);
    if (Position != nullptr)
        LocalPosition = *Position;
    if (Orientation != nullptr)
        LocalOrientation = *Orientation;
    unsigned int ShipId = SpaceObjId;
    RelocateShip(&ShipId, &NewSystemId, &LocalPosition, &LocalOrientation);

    // 06f348bd-06f348d9.
    if (NewSystem != nullptr)
        NewSystem->AddSpaceObj(this);
    else
        System = nullptr;
}

// FUN_06f34af0 (06f34af0-06f34b61, RET 8).
void FContentNpcShip::SetGuideFromDesc(FGuideDescBase* Desc, bool bStart)
{
    if (Guide != nullptr)
    {
        pub::AI::DirectiveCancelOp Cancel;
        SubmitDirective(&Cancel);
        if (Guide != nullptr)
        {
            Guide->Release();
            Guide = nullptr;
        }
    }
    // 06f34b2e-06f34b59: Desc->Type != 0 -> CreateGuide(Desc, &Guide) (FUN_06f05f00);
    // bStart and no directives -> Guide->Start() (no null check).
    if (Desc->Type == 0)
        return;
    CreateGuide(Desc, &Guide.Pointer);
    if (bStart && Directives.Items.empty())
        Guide->Start();
}

// FUN_06f34b70 (06f34b70-06f34c2a, RET 0xc).
void FContentNpcShip::SetGuide(FContentGuide* NewGuide, bool bStart, bool bReset)
{
    if (NewGuide != nullptr)
    {
        if (Guide != NewGuide)
        {
            if (Guide != nullptr)
            {
                Guide->Release();
                Guide = nullptr;
            }
            Guide = NewGuide;
            NewGuide->AddRef();
        }
        Guide->SetShip(this);
        if (bReset)
            Guide->Reset();
        if (bStart && Directives.Items.empty())
            Guide->Start();
        return;
    }

    if (Guide != nullptr)
    {
        pub::AI::DirectiveCancelOp Cancel;
        SubmitDirective(&Cancel);
    }
    if (Guide != nullptr)
    {
        Guide->Release();
        Guide = nullptr;
    }
}

// FUN_06f34c30 (06f34c30-06f34c75, RET 8).
void FContentNpcShip::ForwardToGuide(pub::Controller::EMessage MsgId, void const* Parms)
{
    if (Guide == nullptr)
        return;
    Guide->HandleEvent(MsgId, Parms);
    if (Guide->IsDone() && Guide != nullptr)
    {
        Guide->Release();
        Guide = nullptr;
    }
}

// FUN_06f34c80 (06f34c80-06f34c91): tail JMP [vtbl+0x28].
int FContentNpcShip::GetGuideType() const
{
    if (Guide == nullptr)
        return 0;
    return Guide->GetType();
}

// FUN_06f34ca0.
FContentGuide* FContentNpcShip::GetGuide() const
{
    return Guide;
}

// ---------------------------------------------------------------------------------------
// TU53 event list (DAT_06fd1bd8)
// ---------------------------------------------------------------------------------------

// _initterm LAB_06f5ca20 -> FUN_06f5ca60 (empty list; +0xc..+0x14 are .bss zero), atexit
// FUN_06f5ca40 -> FUN_06f15350 (guarded by bit 0 of DAT_06fd1bd4).
FNpcShipEventList GNpcShipEvents;

namespace
{
    // The loops of 06f68570..06f68840: first record whose ship has the same space object id.
    std::list<FNpcShipEvent>::iterator FindShipEvent(std::list<FNpcShipEvent>& Events,
                                                     FContentNpcShip const* Ship)
    {
        std::list<FNpcShipEvent>::iterator It = Events.begin();
        while (It != Events.end() && It->Ship->SpaceObjId != Ship->SpaceObjId)
            ++It;
        return It;
    }
}

// FUN_06f683e0 (06f683e0-06f6856d, RET 8).
//   0x3e: the record whose TimerId == Parms[0] (first match only): Kind 0 -> the ship's
//     SetField1b1(false); 1 -> LastLeadProgress = 0, lead_object_event(ship, ObjectId,
//     LeadProgress, Value, Position); 2 -> LastFollowStatus = 0, follow_event(ship, ObjectId,
//     FollowStatus, Value); 3 -> LastTetherStatus = 0, tether_object_event(ship, ObjectId,
//     TetherStatus, Position); other kinds: nothing. TimerId = 0 for kinds 0..3.
//   0x39: erase the first record whose ship id == Parms[0].
//   0xb: the first record with ship id == Parms[2], CommLineId == Parms[3] and Parms[4] == 0
//     -> the ship's OnCommLineDone. Parms[4] of the 0xb that the GCS manager sends itself
//     (FUN_06ee8c50) is an unwritten stack word in retail; the port writes 0 there
//     (FGcsConversationManager::Update, Gcs.cpp), so for those messages this test always passes.
void FNpcShipEventList::HandleMessage(pub::Controller::EMessage MsgId, void const* Parms)
{
    unsigned int const* Words = static_cast<unsigned int const*>(Parms);
    if (MsgId == pub::Controller::EMessage::CommComplete)
    {
        for (std::list<FNpcShipEvent>::iterator It = Events.begin(); It != Events.end(); ++It)
        {
            if (It->Ship->SpaceObjId == Words[2] && It->CommLineId == Words[3] && Words[4] == 0)
            {
                It->Ship->OnCommLineDone();
                return;
            }
        }
        return;
    }
    if (MsgId == pub::Controller::EMessage::ObjectDestroyed)
    {
        for (std::list<FNpcShipEvent>::iterator It = Events.begin(); It != Events.end(); ++It)
        {
            if (It->Ship->SpaceObjId == Words[0])
            {
                Events.erase(It);
                return;
            }
        }
        return;
    }
    if (MsgId != pub::Controller::EMessage::TimerExpired)
        return;

    for (std::list<FNpcShipEvent>::iterator It = Events.begin(); It != Events.end(); ++It)
    {
        if (It->TimerId != Words[0])
            continue;
        FContentNpcShip* Ship = It->Ship;
        switch (It->Kind)
        {
        case 0:
            Ship->SetField1b1(false);
            It->TimerId = 0;
            break;
        case 1:
            LastLeadProgress = 0;
            Ship->lead_object_event(Ship->SpaceObjId, It->ObjectId,
                                    static_cast<pub::AI::ContentCallback::LeadProgressType>(It->LeadProgress),
                                    It->Value, It->Position);
            It->TimerId = 0;
            break;
        case 2:
            LastFollowStatus = 0;
            Ship->follow_event(Ship->SpaceObjId, It->ObjectId,
                               static_cast<pub::AI::ContentCallback::FollowStatusType>(It->FollowStatus),
                               It->Value);
            It->TimerId = 0;
            break;
        case 3:
            LastTetherStatus = 0;
            Ship->tether_object_event(Ship->SpaceObjId, It->ObjectId,
                                      static_cast<pub::AI::ContentCallback::TetherStatusType>(It->TetherStatus),
                                      It->Position);
            It->TimerId = 0;
            break;
        default:
            break;
        }
        return;
    }
}

// FUN_06f68570 (06f68570-06f685f9, RET 8): an existing record gets CommLineId; otherwise a
// new record {Ship, CommLineId, TimerId 0} is appended (retail leaves +0xc..+0x2c unset).
void FNpcShipEventList::SetCommLine(FContentNpcShip* Ship, unsigned int CommLineId)
{
    std::list<FNpcShipEvent>::iterator Found = FindShipEvent(Events, Ship);
    if (Found != Events.end())
    {
        Found->CommLineId = CommLineId;
        return;
    }
    FNpcShipEvent Event = FNpcShipEvent();
    Event.Ship = Ship;
    Event.CommLineId = CommLineId;
    Event.TimerId = 0;
    Events.push_back(Event);
}

// FUN_06f68600 (06f68600-06f6862b, RET 4): the ship's record, if any, forgets its timer.
void FNpcShipEventList::ClearTimer(FContentNpcShip* Ship)
{
    std::list<FNpcShipEvent>::iterator Found = FindShipEvent(Events, Ship);
    if (Found != Events.end())
        Found->TimerId = 0;
}

// FUN_06f68630 (06f68630-06f68701, RET 8). Id = DAT_06fce76c++; Ship->SetField1b1(true);
// Ship +0x1b8 = 5; an existing record gets TimerId = Id, Kind = 0; otherwise {Ship, 0, Id,
// Kind 0} is appended; +0xc/+0x10/+0x14 = 0; pub::SetTimer(&SpaceSupport, {Id, 0}, Delay).
void FNpcShipEventList::StartTimer(FContentNpcShip* Ship, float Delay)
{
    unsigned int Id = GContentNextUniqueId++;
    Ship->SetField1b1(true);
    Ship->CommLinesLeft = 5;

    std::list<FNpcShipEvent>::iterator Found = FindShipEvent(Events, Ship);
    if (Found != Events.end())
    {
        Found->TimerId = Id;
        Found->Kind = 0;
    }
    else
    {
        FNpcShipEvent Event = FNpcShipEvent();
        Event.Ship = Ship;
        Event.CommLineId = 0;
        Event.TimerId = Id;
        Event.Kind = 0;
        Events.push_back(Event);
    }

    pub::Controller::TimerExpired Timer;
    Timer.Param1 = Id;
    Timer.Param2 = 0;
    LastLeadProgress = 0;
    LastFollowStatus = 0;
    LastTetherStatus = 0;
    pub::SetTimer(GSpaceSupportControllerId, Timer, Delay);
}

// FUN_06f68710 (06f68710-06f68807, RET 0x14). Id = DAT_06fce76c++; an existing record gets
// ObjectId, Kind 1, TimerId Id, LeadProgress, Value, Position; otherwise a new record {Ship,
// 0, Id, ObjectId, Progress, -, -, Value, Position, Kind 1} is appended. Then
// SetTimer({Id, 0}, 30.0) and LastLeadProgress = Progress.
void FNpcShipEventList::QueueLeadEvent(FContentNpcShip* Ship, unsigned int ObjectId,
                                       unsigned int Progress, float Value, Vector const* Position)
{
    unsigned int Id = GContentNextUniqueId++;
    std::list<FNpcShipEvent>::iterator Found = FindShipEvent(Events, Ship);
    if (Found != Events.end())
    {
        Found->ObjectId = ObjectId;
        Found->Kind = 1;
        Found->TimerId = Id;
        Found->LeadProgress = Progress;
        Found->Value = Value;
        Found->Position = *Position;
    }
    else
    {
        FNpcShipEvent Event = FNpcShipEvent();
        Event.Ship = Ship;
        Event.CommLineId = 0;
        Event.TimerId = Id;
        Event.ObjectId = ObjectId;
        Event.LeadProgress = Progress;
        Event.Value = Value;
        Event.Position = *Position;
        Event.Kind = 1;
        Events.push_back(Event);
    }

    pub::Controller::TimerExpired Timer;
    Timer.Param1 = Id;
    Timer.Param2 = 0;
    pub::SetTimer(GSpaceSupportControllerId, Timer, 30.0f);
    LastLeadProgress = Progress;
}

// FUN_06f68840 (06f68840-06f6892b, RET 0x10). As QueueLeadEvent with Kind 3, TetherStatus,
// and LastTetherStatus = Status.
void FNpcShipEventList::QueueTetherEvent(FContentNpcShip* Ship, unsigned int ObjectId,
                                         unsigned int Status, Vector const* Position)
{
    unsigned int Id = GContentNextUniqueId++;
    std::list<FNpcShipEvent>::iterator Found = FindShipEvent(Events, Ship);
    if (Found != Events.end())
    {
        Found->Kind = 3;
        Found->ObjectId = ObjectId;
        Found->TimerId = Id;
        Found->TetherStatus = Status;
        Found->Position = *Position;
    }
    else
    {
        FNpcShipEvent Event = FNpcShipEvent();
        Event.Ship = Ship;
        Event.CommLineId = 0;
        Event.TimerId = Id;
        Event.ObjectId = ObjectId;
        Event.TetherStatus = Status;
        Event.Position = *Position;
        Event.Kind = 3;
        Events.push_back(Event);
    }

    pub::Controller::TimerExpired Timer;
    Timer.Param1 = Id;
    Timer.Param2 = 0;
    pub::SetTimer(GSpaceSupportControllerId, Timer, 30.0f);
    LastTetherStatus = Status;
}
