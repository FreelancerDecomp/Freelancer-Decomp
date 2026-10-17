// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "RandomMissionRuntime.h"

#include "ContentMessageRouter.h"   // GMessageRouter (DAT_06fcf254)
#include "FactionProps.h"           // GetFactionNames, FFactionNames
#include "NpcShips.h"               // GetSecondNameValues & co (TU42 name readers)
#include "RandomEncounters.h"       // FRmKillableSolar, FRmCriticalLoot
#include "RandomMissionGen.h"       // GetRmZoneAtPosition
#include "RandomMissionOffer.h"
#include "SystemInfo.h"             // FSystemInfo::SystemId
#include "common_id.h"
#include "common_ini.h"
#include "common_net.h"
#include "common_server.h"

#include <string.h>

// =======================================================================================
// TU61 message types (static initialisers LAB_06f842a0 ... LAB_06f84520, 0x20 apart, in
// this order). Retail increments the exported generator itself; the port calls the exported
// Common function that does the same (StateMachineMessage::GenerateUniqueMessageType).
// =======================================================================================

unsigned int const GRmMsgStartMachine = StateMachineMessage::GenerateUniqueMessageType();      // 06f842a0
unsigned int const GRmMsgMachineStarted = StateMachineMessage::GenerateUniqueMessageType();    // 06f842c0
unsigned int const GRmMsgMachineSucceeded = StateMachineMessage::GenerateUniqueMessageType();  // 06f842e0
unsigned int const GRmMsgMachineFailed = StateMachineMessage::GenerateUniqueMessageType();     // 06f84300
unsigned int const GRmMsgRefreshObjectives = StateMachineMessage::GenerateUniqueMessageType(); // 06f84320
unsigned int const GRmMsgType06fd3ba8 = StateMachineMessage::GenerateUniqueMessageType();      // 06f84340
unsigned int const GRmMsgType06fd3ba4 = StateMachineMessage::GenerateUniqueMessageType();      // 06f84360
unsigned int const GRmMsgType06fd3ba0 = StateMachineMessage::GenerateUniqueMessageType();      // 06f84380
unsigned int const GRmMsgType06fd3b9c = StateMachineMessage::GenerateUniqueMessageType();      // 06f843a0
unsigned int const GRmMsgType06fd3b98 = StateMachineMessage::GenerateUniqueMessageType();      // 06f843c0
unsigned int const GRmMsgType06fd3b94 = StateMachineMessage::GenerateUniqueMessageType();      // 06f843e0
unsigned int const GRmMsgType06fd3b90 = StateMachineMessage::GenerateUniqueMessageType();      // 06f84400
unsigned int const GRmMsgType06fd3b8c = StateMachineMessage::GenerateUniqueMessageType();      // 06f84420
unsigned int const GRmMsgType06fd3b88 = StateMachineMessage::GenerateUniqueMessageType();      // 06f84440
unsigned int const GRmMsgType06fd3b84 = StateMachineMessage::GenerateUniqueMessageType();      // 06f84460
unsigned int const GRmMsgType06fd3b80 = StateMachineMessage::GenerateUniqueMessageType();      // 06f84480
unsigned int const GRmMsgType06fd3b7c = StateMachineMessage::GenerateUniqueMessageType();      // 06f844a0
unsigned int const GRmMsgType06fd3b78 = StateMachineMessage::GenerateUniqueMessageType();      // 06f844c0
unsigned int const GRmMsgType06fd3b74 = StateMachineMessage::GenerateUniqueMessageType();      // 06f844e0
unsigned int const GRmMsgPlayerLeft = StateMachineMessage::GenerateUniqueMessageType();        // 06f84500
unsigned int const GRmMsgType06fd3b6c = StateMachineMessage::GenerateUniqueMessageType();      // 06f84520

// FUN_06f86d90 (RET 4): StateMachineMessage(DAT_06fd3bb0), +0x1c = Reason, vtable 06fbc81c.
FRmMachineFailedMessage::FRmMachineFailedMessage(int InReason)
    : StateMachineMessage(GRmMsgMachineFailed)
    , Reason(InReason)
{
}

// FUN_06f86de0: operator new(0x20) (StateMachineMessage::operator new); on success the copy
// ctor and +0x1c; a failed new returns null.
StateMachineMessage* FRmMachineFailedMessage::clone() const
{
    return new FRmMachineFailedMessage(*this);
}

// FUN_06f86e40 (RET 4): StateMachineMessage(DAT_06fd3b70), +0x1c = PlayerId, vtable 06fbc824.
FRmPlayerLeftMessage::FRmPlayerLeftMessage(unsigned int InPlayerId)
    : StateMachineMessage(GRmMsgPlayerLeft)
    , PlayerId(InPlayerId)
{
}

// FUN_06f86e70: as FUN_06f86de0.
StateMachineMessage* FRmPlayerLeftMessage::clone() const
{
    return new FRmPlayerLeftMessage(*this);
}

// =======================================================================================
// .data name tables shared by TU61 and TU63
// =======================================================================================

FRmNamedValue const GRmTextParamNames[15] = {
    {"FSTRING_PARAM_NONE", 0},
    {"OFFER_GROUP", 1},
    {"HOSTILE_GROUP", 2},
    {"BIG_SOLAR", 3},
    {"OTHER_SOLAR", 4},
    {"CRITICAL_LOOT", 5},
    {"TARGET_FULL_NAME", 6},
    {"REWARD_MONEY", 7},
    {"FACTION_REWARD", 8},
    {"FACTION_HIT", 9},
    {"TIME_TO_RETURN_TO_BATTLE", 10},
    {"MISSION_DIFFICULTY", 11},
    {"OFFER_BASE", 12},
    {"TARGET_ZONE", 13},
    {"TARGET_SYSTEM", 14},
};

FRmNamedValue const GRmCommTriggerNames[17] = {
    {"NONE", 0},
    {"IN_SPACE_IN_SYSTEM", 1},
    {"IN_SPACE_NOT_IN_SYSTEM", 2},
    {"LEFT_COMBAT_AREA", 11},
    {"REENTERED_COMBAT_AREA", 12},
    {"GLOBAL_SUCCESS", 13},
    {"GLOBAL_FAIL_COMBAT_RANGE", 14},
    {"GLOBAL_FAIL_DESTROYED_CRITICAL_LOOT", 15},
    {"GLOBAL_FAIL_OTHER", 16},
    {"PLAYER_IN_PRE_BATTLE_SPAWN_RANGE", 3},
    {"SUCCESS_KILLED_ALL_GOING_TO_MAIN", 4},
    {"SUCCESS_NOT_KILLED_ALL_GOING_TO_MAIN", 5},
    {"PLAYER_IN_MAIN_BATTLE_SOLAR_SPAWN_RANGE", 6},
    {"PLAYER_IN_MAIN_BATTLE_SHIP_SPAWN_RANGE", 7},
    {"FIRST_FRIENDLY_SPAWN", 8},
    {"FIRST_BIG_SHIP_SPAWN", 10},
    {"FIRST_DEFENSIVE_SHIP_SPAWN", 9},
};

FRmNamedValue const GRmCommTargetNames[12] = {
    {"NONE", 0},
    {"BASE", 1},
    {"BIG_SOLAR", 2},
    {"BIG_SHIP", 3},
    {"DEFENSIVE_SHIP", 4},
    {"FRIENDLY_SHIP", 5},
    {"PLAYER", 6},
    {"ALL_PLAYERS", 7},
    {"PLAYERS_IN_RANGE", 8},
    {"PLAYERS_OUT_OF_RANGE", 9},
    {"PLAYERS_IN_SYSTEM", 10},
    {"PLAYERS_OUT_OF_SYSTEM", 11},
};

// =======================================================================================
// TU61 -- text with parameters (vtable 06fbc700)
// =======================================================================================

// Inline ctor at every owner (e.g. FUN_06f9efb0 +0x30/+0x60, FUN_06f99560): FmtStr zeroed
// (FUN_06eac680 shape), StrId 0, empty vector.
FRmParamText::FRmParamText()
    : StrId(0)
{
}

// FUN_06f85550: frees the vector, then FmtStr::~FmtStr on +0x04.
FRmParamText::~FRmParamText()
{
}

// FUN_06f84540 (RET 8): StrId = int value Index; every following value is looked up in
// GRmTextParamNames (DACOM stricmp, null names skipped) and its value appended. A name not
// in the table appends the previous lookup's value; for the first value that is retail's
// uninitialised stack slot [ESP+0x10] -- TODO: no defined value exists, the port uses 0
// (FSTRING_PARAM_NONE).
void FRmParamText::Parse(INI_Reader* Reader, unsigned int Index)
{
    StrId = Reader->get_value_int(Index);
    unsigned int ValueIndex = Index + 1;
    int Param = 0;
    while (!Reader->is_value_empty(ValueIndex))
    {
        char const* Name = Reader->get_value_string(ValueIndex);
        ++ValueIndex;
        for (int Row = 0; Row < 15; ++Row)
        {
            if (GRmTextParamNames[Row].Name != nullptr &&
                _stricmp(Name, GRmTextParamNames[Row].Name) == 0)
            {
                Param = GRmTextParamNames[Row].Value;
                break;
            }
        }
        Params.insert(Params.end(), Param);
    }
}

// LAB_06f84740 (06f84740-06f8478b, RET 4). Text's mad-lib id (+0x08) already set -> nothing
// is rebuilt; otherwise begin_mad_lib(StrId), Source slot 0 (GetTextParam) per parameter
// (+0x24..+0x28) into Text, end_mad_lib. Returns &Text either way.
FmtStr* FRmParamText::Build(IRmTextParamSource* Source)
{
    if (Text.MadLibId == 0)
    {
        Text.begin_mad_lib(static_cast<unsigned int>(StrId));
        for (std::vector<int>::iterator Param = Params.begin(); Param != Params.end(); ++Param)
            Source->GetTextParam(*Param, &Text);
        Text.end_mad_lib();
    }
    return &Text;
}

// LAB_06f85430: StrId == 0.
bool FRmParamText::IsEmpty() const
{
    return StrId == 0;
}

// =======================================================================================
// TU61 -- the random-mission offer (RandomMissionOffer.h)
// =======================================================================================

unsigned int GRmShipGroupSerial = 0;                            // DAT_06fd3c08

// FUN_06f89400 (RET 4). The members' defaults are the ctor's stores; +0x2c..+0x38 are not
// written by retail (see the header).
FRmOffer::FRmOffer(int InMachineType)
    : MachineType(InMachineType)
{
}

// FUN_06f883e0: FailureText (+0x88: vector freed, FmtStr), RewardText (+0x58), Text (+0x3c),
// then the +0x04 vptr back to 06fb43f8.
FRmOffer::~FRmOffer()
{
}

// FUN_06f89550 (RET 8): switch (Param - 1), bound 0xd.
bool FRmOffer::GetTextParam(int Param, FmtStr* Out)
{
    switch (Param)
    {
    case 1:     // OFFER_GROUP: append_rep_group(&+0x24)
    {
        unsigned int const Group = static_cast<unsigned int>(OfferGroup);
        Out->append_rep_group(Group);
        return true;
    }
    case 2:     // HOSTILE_GROUP: append_rep_group(&+0x28)
        Out->append_rep_group(HostileGroup);
        return true;
    case 7:     // REWARD_MONEY: append_int(+0x30)
        Out->append_int(static_cast<unsigned int>(Reward));
        return true;
    case 8:     // FACTION_REWARD: ftol(+0x38 * 100.0f DAT_06fb8574), x87 product (exact in double)
        Out->append_int(static_cast<unsigned int>(
            static_cast<int>(static_cast<double>(RepReward) * 100.0)));
        return true;
    case 9:     // FACTION_HIT: ftol(+0x34 * 100.0f)
        Out->append_int(static_cast<unsigned int>(
            static_cast<int>(static_cast<double>(RepHit) * 100.0)));
        return true;
    case 11:    // MISSION_DIFFICULTY: ftol(+0x2c)
        Out->append_int(static_cast<unsigned int>(static_cast<int>(Difficulty)));
        return true;
    case 12:    // OFFER_BASE: append_base(&+0x20)
        Out->append_base(OfferBase);
        return true;
    case 14:    // TARGET_SYSTEM: pub::System::GetName(System->SystemId, &id), append_string(id)
    {
        unsigned int NameId = static_cast<unsigned int>(Param);    // the argument slot
        pub::System::GetName(System->SystemId, NameId);
        Out->append_string(NameId);
        return true;
    }
    default:    // 3, 4, 5, 6, 10, 13 and everything outside 1..14
        return false;
    }
}

// =======================================================================================
// TU61 comm sequence lines and conversations (06f84790-06f84d60)
// =======================================================================================

// FUN_06f84790 (06f84790-06f847b8): FUN_06ed6180(0), +0x40 / +0x44 / +0x48 = 0, +0x4c = 0
// (the empty text), +0x8c = 0, vtable 06fbc648.
FRmLine::FRmLine()
    : FGcsPlayerListLine(nullptr)
    , SpeakerKind(0)
    , TargetKind(0)
    , Field_0x8c(0)
{
    Text.Length = 0;
    Text.Data[0] = '\0';
}

// FUN_06f849c0 (06f849c0-06f849e0): FUN_06ed6710 (called directly) true and +0x8c non-zero
// -> 1, else 0.
bool FRmLine::CanPlay()
{
    if (FGcsPlayerListLine::CanPlay() && Field_0x8c != 0)
        return true;
    return false;
}

// FUN_06f84820 (06f84820-06f849b2): new 0x90 and a member-wise copy -- the FGcsLine part
// (the text ids copied), vptr 06fb6268, the player ids copied, vptr 06fb623c, +0x40, +0x44,
// the text (0x11 dwords), +0x8c, vptr 06fbc648. A failed new returns null.
FGcsCloneableLine* FRmLine::Clone()
{
    return new FRmLine(*this);
}

// FUN_06f84a20 (06f84a20-06f84bd6): a local vector gets each line's slot 5 copy appended
// (FUN_06f937c0 insert(end, 1, &copy)); the inlined vector assign then makes it Lines (the
// old buffer freed, the old pointers not deleted), and the local buffer is freed.
void FRmConversationBase::CloneLines()
{
    std::vector<FGcsLine*> Copies;
    for (std::vector<FGcsLine*>::iterator It = Lines.begin(); It != Lines.end(); ++It)
    {
        FGcsLine* const Copy = static_cast<FGcsCloneableLine*>(*It)->Clone();
        Copies.insert(Copies.end(), 1, Copy);
    }
    Lines = Copies;
}

// FUN_06f84c00 (06f84c00-06f84c31): +0x18 zero -> false; a line whose slot 1 fails ->
// false; otherwise true.
bool FRmConversation::IsValid()
{
    if (!bReady)
        return false;
    for (std::vector<FGcsLine*>::iterator It = Lines.begin(); It != Lines.end(); ++It)
    {
        if (!(*It)->CanPlay())
            return false;
    }
    return true;
}

// FUN_06f84c40 (06f84c40-06f84d15): new 0x24, member copy (+0x04 byte, the line pointers,
// vptr 06fbc690, +0x14, +0x18, +0x1c, +0x20, vptr 06fbc660), then the copy's slot 6
// (06f84d00).
FRmConversationBase* FRmConversation::Clone()
{
    FRmConversation* const Copy = new FRmConversation(*this);
    Copy->CloneLines();
    return Copy;
}

// FUN_06f71080 (RET 4; one ICF body for every class storing its argument at +0x20).
void FRmConversation::SetTrigger(int InTrigger)
{
    Trigger = InTrigger;
}

// LAB_06f84d30.
int FRmConversation::GetTrigger()
{
    return Trigger;
}

// LAB_06f84d40.
void FRmConversation::MarkReady()
{
    bReady = true;
}

// LAB_06f84d50 (FLD float: the value goes out on the FPU stack).
float FRmConversation::GetDelay()
{
    return Delay;
}

// LAB_06f84d60 (RET 4).
void FRmConversation::SetDelay(float InDelay)
{
    Delay = InDelay;
}

// FUN_06f89230: every held objective is deleted through its vtable slot 0 (with 1) and its
// slot cleared (06f89270-06f89281); then the tree nodes and the head/nil nodes are freed.
FRmObjectiveMap::~FRmObjectiveMap()
{
    for (std::map<int, FRmParamText*>::iterator It = Objectives.begin(); It != Objectives.end();
         ++It)
    {
        if (It->second != nullptr)
        {
            delete It->second;
            It->second = nullptr;
        }
    }
}

// FUN_06f84d70: MessageHandler(DAT_06fcf254) (it registers with the router), +0x0c = 0, the
// empty list (0x2c-byte head node), the empty objective map (FUN_06f891a0 inline), vtables
// 06fbc6cc at +0x1c and 06fbc6ac.
FRmOfferComponent::FRmOfferComponent()
    : MessageHandler(GMessageRouter)
{
}

// FUN_06f84e60: the objective map (FUN_06f89230), the list (each element's slot 0 with 0,
// node freed), the head node, ~MessageHandler.
FRmOfferComponent::~FRmOfferComponent()
{
}

// FUN_06ebf6f0 (RET 4): no body.
void FRmOfferComponent::receive_message(StateMachineMessage* /*Msg*/)
{
}

// FUN_06f88500: every group's ship vector (FUN_06f93370: slot 0 with 0 per ship, stride
// 0xf0), the group buffer, then the component (objective map, list, MessageHandler).
FRmShipComponent::~FRmShipComponent()
{
}

// FUN_06f88500 through vtable 06fbc8dc (ICF with the ship component's).
FRmTargetShipComponent::~FRmTargetShipComponent()
{
}

// FUN_06f88620: the solar list (FUN_06f93d10 per node, the head freed), then the objective
// map, the event list and ~MessageHandler.
FRmSolarComponent::~FRmSolarComponent()
{
}

// FUN_06f862e0: the FMissionShip template (vtable 06fb60a8 first), FUN_06f31430 on +0x88,
// +0xd4 = 0, +0xd8 = 2, +0xdc = 1, the loot vector empty, vtable 06fbc7a0.
FRmShipSpawn::FRmShipSpawn()
{
}

// FUN_06f86420: the loot vector (each loot's label list freed), then the FMissionShip dtor
// (vtable 06fb60a8, guide released, cargo, directives, labels).
FRmShipSpawn::~FRmShipSpawn()
{
}

// FUN_06f865e0: +0 = 1, +1/+2 = 0, +4 = 0, +8 = -1, +0xc = 0, identity 3x3 at +0x10, +0x34
// zero, +0x40 = -2.0f (DAT_06fbc2e8), +0x44/+0x48 = 0 (empty name), +0x78 = 0, +0x7c = -1,
// +0x80 = CreateID("transport_liberty"), the ship vector empty, +0x94 = ++DAT_06fd3c08.
FRmShipGroup::FRmShipGroup()
{
    Orientation.D[0][0] = 1.0f;
    Orientation.D[0][1] = 0.0f;
    Orientation.D[0][2] = 0.0f;
    Orientation.D[1][0] = 0.0f;
    Orientation.D[1][1] = 1.0f;
    Orientation.D[1][2] = 0.0f;
    Orientation.D[2][0] = 0.0f;
    Orientation.D[2][1] = 0.0f;
    Orientation.D[2][2] = 1.0f;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Name.Length = 0;
    Name.Data[0] = '\0';
    Formation.Id = CreateID("transport_liberty");
    ++GRmShipGroupSerial;
    Serial = GRmShipGroupSerial;
}

// FUN_06f88240 (RET 4): FRmOffer(InMachineType), then +0xb8 the general component (inline:
// MessageHandler, +0xc = 0, list FUN_06f93640, map FUN_06f1f4f0), +0xec the target ship
// component (FUN_06f84d70, vector FUN_06eac620, +0x50 = 0, +0x54 = -1, +0xc = 0), +0x144 the
// solar component (inline base, FUN_06f891a0, list FUN_06f93430), +0x190 the ship component
// (FUN_06f84d70, vector), +0x1e0/+0x1f8 FmtStr (FUN_06eac680), +0x210 = 0, +0x214 = -1;
// vtables 06fbc898 / 06fbc88c last. Unwind states 1..0xc follow this member order.
FRmVignetteOffer::FRmVignetteOffer(int InMachineType)
    : FRmOffer(InMachineType)
{
}

// FUN_06f88710: vtables 06fbc898 / 06fbc88c, FUN_06f88940, then TargetName (+0x1f8, +0x1e0),
// Ships, Solars, TargetShips, General (member dtors inlined), then ~FRmOffer.
FRmVignetteOffer::~FRmVignetteOffer()
{
    ReleaseTargetName();
}

// FUN_06f88940 (06f88940-06f88ac7). Nothing when neither name part has a mad lib (+0x1e4 and
// +0x1fc zero). Otherwise the target ship groups (+0x124..+0x128), then -- when no ship there
// has +0xd4 set -- the ship groups (+0x1c8..+0x1cc) are searched for the first ship with
// +0xd4 != 0; its group's +0x01 is read. A found ship in a group with +0x01 set keeps the
// name; in every other case the faction's name tables (GetFactionNames(&Affiliation), not
// null-tested) get the parts back at once: the pilot pair when GetSecondNameValues gives two
// non-zero values, else the large ship name GetSecondNameThirdValue gives (when non-zero);
// then always the formation designation (GetFirstNameValue, GetFirstNameLastValue).
void FRmVignetteOffer::ReleaseTargetName()
{
    if (TargetName.First.MadLibId == 0 && TargetName.Second.MadLibId == 0)
        return;

    bool bFound = false;
    unsigned char bKeep = 0;
    std::vector<FRmShipGroup> const* const GroupLists[2] = {&TargetShips.Groups, &Ships.Groups};
    for (int ListIndex = 0; ListIndex < 2 && !bFound; ++ListIndex)
    {
        std::vector<FRmShipGroup> const* const Groups = GroupLists[ListIndex];
        for (std::vector<FRmShipGroup>::const_iterator Group = Groups->begin();
             Group != Groups->end() && !bFound; ++Group)
        {
            for (std::vector<FRmShipSpawn>::const_iterator Ship = Group->Ships.begin();
                 Ship != Group->Ships.end(); ++Ship)
            {
                if (Ship->TargetName != nullptr)
                {
                    bKeep = Group->Field_0x01;
                    bFound = true;
                    break;
                }
            }
        }
    }
    if (bFound && bKeep != 0)
        return;

    FFactionNames* const Names = GetFactionNames(&TargetName.Affiliation);
    int PilotFirst = 0;
    int PilotLast = 0;
    GetSecondNameValues(&TargetName.First, &TargetName.Second, &PilotFirst, &PilotLast);
    if (PilotFirst != 0 && PilotLast != 0)
    {
        Names->ReleasePilotName(PilotFirst, PilotLast, true);
    }
    else
    {
        int LargeShipName = 0;
        GetSecondNameThirdValue(&TargetName.First, &TargetName.Second, &LargeShipName);
        if (LargeShipName != 0)
            Names->ReleaseLargeShipName(LargeShipName, true);
    }

    int Desig = 0;
    GetFirstNameValue(&TargetName.First, &TargetName.Second, &Desig);
    int DesigNumber = -1;
    GetFirstNameLastValue(&TargetName.First, &TargetName.Second, &DesigNumber);
    Names->ReleaseFormationDesig(Desig, DesigNumber, true);
}

// FUN_06f89650 (06f89650-06f896e1).
void FRmVignetteOffer::CountCriticalLoot(std::vector<FRmShipGroup> const* Groups,
                                         FRmCriticalLoot const** OutLoot,
                                         int* InOutCount) const
{
    for (std::vector<FRmShipGroup>::const_iterator Group = Groups->begin();
         Group != Groups->end(); ++Group)
    {
        for (std::vector<FRmShipSpawn>::const_iterator Ship = Group->Ships.begin();
             Ship != Group->Ships.end(); ++Ship)
        {
            for (std::vector<FRmLootSpawn>::const_iterator Loot = Ship->Loot.begin();
                 Loot != Ship->Loot.end(); ++Loot)
            {
                if (Loot->Kind != 2)
                    continue;
                *OutLoot = Loot->CriticalLoot;
                *InOutCount += Loot->Info.Units;
                if (*InOutCount > 1)
                    break;
            }
        }
    }
}

// LAB_06f896f0 (RET 8). The base offer's answer first (FUN_06f89550); otherwise switch
// (Param - 3), bound 0xa:
//   3 BIG_SOLAR: the first solar (+0x188 list) of type 0 -> append_installation(its
//     KillableSolars entry's string id); none -> false.
//   4 OTHER_SOLAR: the solars of type 1 or 2; one -> its string id, more (the scan stops at
//     the second) -> the last seen one's string id + 1; none -> false.
//   5 CRITICAL_LOOT: count 0; CountCriticalLoot over the target groups (+0x120), then -- while
//     the count is at most 1 -- over the ship groups (+0x1c4), then over the solars' loot
//     (Kind 2 adds its units and takes its record; a solar's scan stops past 1); count 0 ->
//     false, else append_loot(record->Name).
//   6 TARGET_FULL_NAME: GetSecondNameValues on the target name; both non-zero -> both
//     appended as strings. Else GetSecondNameThirdValue; zero -> false; otherwise
//     append_string(the faction's large_ship_desig) and append_string(the second value the
//     first reader left).
//   13 TARGET_ZONE: the position of the active component -- target ships (+0xf8), else ships
//     (+0x19c), else solars -- through GetRmZoneAtPosition(System, &pos), append_zone_id.
//   7..12: false.
bool FRmVignetteOffer::GetTextParam(int Param, FmtStr* Out)
{
    if (FRmOffer::GetTextParam(Param, Out))
        return true;

    switch (Param)
    {
    case 3:
    {
        for (std::list<FRmSolarSpawn>::const_iterator Solar = Solars.Solars.begin();
             Solar != Solars.Solars.end(); ++Solar)
        {
            if (Solar->SolarType == 0)
            {
                Out->append_installation(Solar->Arch->StringId);
                return true;
            }
        }
        return false;
    }
    case 4:
    {
        int Count = 0;
        FRmKillableSolar const* Arch = nullptr;
        for (std::list<FRmSolarSpawn>::const_iterator Solar = Solars.Solars.begin();
             Solar != Solars.Solars.end(); ++Solar)
        {
            if (Solar->SolarType == 1 || Solar->SolarType == 2)
            {
                Arch = Solar->Arch;
                ++Count;
                if (Count > 1)
                {
                    Out->append_installation(Arch->StringId + 1);
                    return true;
                }
            }
        }
        if (Count == 0)
            return false;
        Out->append_installation(Arch->StringId);
        return true;
    }
    case 5:
    {
        FRmCriticalLoot const* Loot = nullptr;
        int Count = 0;
        CountCriticalLoot(&TargetShips.Groups, &Loot, &Count);
        if (Count <= 1)
        {
            CountCriticalLoot(&Ships.Groups, &Loot, &Count);
            if (Count <= 1)
            {
                for (std::list<FRmSolarSpawn>::const_iterator Solar = Solars.Solars.begin();
                     Solar != Solars.Solars.end(); ++Solar)
                {
                    for (std::vector<FRmLootSpawn>::const_iterator It = Solar->Loot.begin();
                         It != Solar->Loot.end(); ++It)
                    {
                        if (It->Kind != 2)
                            continue;
                        Count += It->Info.Units;
                        Loot = It->CriticalLoot;
                        if (Count > 1)
                            break;
                    }
                }
            }
        }
        if (Count == 0)
            return false;
        Out->append_loot(Loot->Name);
        return true;
    }
    case 6:
    {
        int First = 0;
        int Second = 0;
        GetSecondNameValues(&TargetName.First, &TargetName.Second, &First, &Second);
        if (First != 0 && Second != 0)
        {
            Out->append_string(static_cast<unsigned int>(First));
            Out->append_string(static_cast<unsigned int>(Second));
            return true;
        }
        int LargeShipName = 0;
        GetSecondNameThirdValue(&TargetName.First, &TargetName.Second, &LargeShipName);
        if (LargeShipName == 0)
            return false;
        FFactionNames* const Names = GetFactionNames(&TargetName.Affiliation);
        Out->append_string(static_cast<unsigned int>(Names->GetLargeShipDesig()));
        Out->append_string(static_cast<unsigned int>(Second));
        return true;
    }
    case 13:
    {
        Vector Position;
        if (TargetShips.bActive)
            Position = TargetShips.Position;
        else if (Ships.bActive)
            Position = Ships.Position;
        else
            Position = Solars.Position;
        unsigned int const Zone = GetRmZoneAtPosition(System, &Position);
        Out->append_zone_id(Zone);
        return true;
    }
    default:
        return false;
    }
}
