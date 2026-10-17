// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TU61 -- the random-mission runtime machine and the runtime slots of the offer components
// (docs/re/content.md, TU map, lane CNT-RM). Declarations in RandomMissionMachine.h,
// RandomMissionOffer.h and RandomMissionRuntime.h.

#include "ContentStaticIds.h"       // TU61's 16 static-init thunks 06f84060..06f84240
#include "ContentDefaultId.h"       // TU61 LAB_06f84260: CreateID("default") -> DAT_06fd3bc4

#include "RandomMissionMachine.h"

#include "ContentInternal.h"        // SubtractVectors, SpacePopRemoveExclusionSphere
#include "ContentMessageRouter.h"   // GMessageRouter (DAT_06fcf254)
#include "ContentPlayerState.h"     // FContentPlayerState
#include "DockQueues.h"             // PlaceFormationBlocks
#include "Encounters.h"             // FindFormation
#include "Gcs.h"                    // MakeVoiceLineName
#include "Guides.h"                 // FGuideDesc
#include "MissionObject.h"          // CreateMissionObjective, FNNObjectiveType7
#include "MissionObjList.h"         // CreateObjListDirective, FObjListStayInRange, FObjListGotoVec
#include "NpcBase.h"                // FContentSpaceObj::GetPosition
#include "NpcNames.h"               // FNpcNameRequest, BuildNpcName, Create*Reputation
#include "NpcShip.h"                // FContentNpcShip, ApplyNpcShipArchById, ApplyNpcShipFaction
#include "RefHandle.h"              // TRefHandle
#include "RandomEncounters.h"       // FRmKillableSolar, FRmCriticalLoot
#include "RandomMissionObject.h"    // FRandomMissionObject
#include "SpacePaths.h"             // BuildNnPath
#include "SystemInfo.h"             // FSystemInfo, FindSystemInfo
#include "common_csys.h"            // Transform (pub::SpaceObj::GetEmptyPos)
#include "common_misc.h"            // SUBOBJ_ID_NONE
#include "common_newchar.h"         // pub::StateGraph::get_state_graph
#include "common_pub.h"             // SetZoneBehaviorParams, SetPersonalityParams
#include "common_server.h"
#include "server_cobject.h"         // SolarInfo, LootInfo, CreateSolar, CreateLoot

#include <cstdlib>
#include <cstring>

// =======================================================================================
// FRmObjective (vtable 06fbc6d8)
// =======================================================================================

// FUN_06f85440 (06f85440-06f85542): operator new(0x48); the member copy with the vtable store
// 06fbc700 then 06fbc6d8; a failed new returns null.
FRmObjective* FRmObjective::Clone() const
{
    return new FRmObjective(*this);
}

// LAB_06ee9e10 (ICF): MOV EAX,1; RET.
int FRmObjective::GetType() const
{
    return 1;
}

// LAB_06f85590 (06f85590-06f855df, RET 8). The factory's out-parameter is the argument slot of
// InMission (it starts as InMission and is overwritten; type 7 always builds).
FMissionObjective* FRmObjective::CreateMissionObjective(FMissionObject* InMission,
                                                        FRmRuntimeMachine* Machine)
{
    FMissionObjective* Created = nullptr;
    ::CreateMissionObjective(7, InMission, &Created);
    Created->Objective.Type = static_cast<unsigned int>(ObjectiveType);
    IRmTextParamSource* Source = nullptr;
    if (Machine != nullptr)
        Source = Machine;
    static_cast<FNNObjectiveType7*>(Created)->Text = *Build(Source);
    Created->bHidden = 0;
    return Created;
}

// LAB_06ea1470 (ICF: XOR AL,AL; RET 4).
bool FRmObjective::GetPosition(Vector* /*Out*/)
{
    return false;
}

// LAB_06f85580: RET 0xc.
void FRmObjective::SetPosition(Vector /*InPosition*/)
{
}

// LAB_06ea1470 (ICF).
bool FRmObjective::GetSystemId(unsigned int* /*Out*/)
{
    return false;
}

// =======================================================================================
// FRmObjectiveType2 (vtable 06fbc710)
// =======================================================================================

// FUN_06f855f0 (06f855f0-06f8578d, RET 4). The vector assignment (06f8565f-06f85776) is the
// VC6 vector operator= inlined.
FRmObjectiveType2::FRmObjectiveType2(FRmParamText const* Source)
{
    StrId = Source->StrId;
    if (&Params != &Source->Params)
        Params = Source->Params;
}

// LAB_06f860a0 (06f860a0-06f860f7, RET 4).
FmtStr* FRmObjectiveType2::Build(IRmTextParamSource* Source)
{
    if (Text.MadLibId == 0)
    {
        Text.begin_mad_lib(static_cast<unsigned int>(StrId));
        for (std::vector<int>::iterator Param = Params.begin(); Param != Params.end(); ++Param)
            Source->GetTextParam(*Param, &Text);
        Text.end_mad_lib();
        FmtStr::NavMarker Marker;
        std::memcpy(&Marker.Data[0], &MarkerSystemId, sizeof(unsigned int));
        std::memcpy(&Marker.Data[1], &MarkerValue, sizeof(unsigned int));
        std::memcpy(&Marker.Data[2], &MarkerPosition.x, sizeof(float));
        std::memcpy(&Marker.Data[3], &MarkerPosition.y, sizeof(float));
        std::memcpy(&Marker.Data[4], &MarkerPosition.z, sizeof(float));
        Text.append_nav_marker(Marker);
    }
    return &Text;
}

// FUN_06f85790 (06f85790-06f858a5): operator new(0x5c), the member copy (vtable stores 06fbc700,
// 06fbc6d8, 06fbc710); null when new fails.
FRmObjective* FRmObjectiveType2::Clone() const
{
    return new FRmObjectiveType2(*this);
}

// LAB_06ee9660 (ICF): MOV EAX,2; RET.
int FRmObjectiveType2::GetType() const
{
    return 2;
}

// LAB_06f86060 (RET 4).
bool FRmObjectiveType2::GetPosition(Vector* Out)
{
    *Out = MarkerPosition;
    return true;
}

// LAB_06f86080 (RET 0xc).
void FRmObjectiveType2::SetPosition(Vector InPosition)
{
    MarkerPosition = InPosition;
}

// LAB_06f858b0 (RET 4).
bool FRmObjectiveType2::GetSystemId(unsigned int* Out)
{
    *Out = MarkerSystemId;
    return true;
}

// LAB_06f86030 (RET 0x14).
void FRmObjectiveType2::SetMarker(unsigned int InSystemId, int InValue, Vector InPosition)
{
    MarkerSystemId = InSystemId;
    MarkerValue = InValue;
    MarkerPosition = InPosition;
}

// =======================================================================================
// FRmObjectiveType3 (vtable 06fbc73c)
// =======================================================================================

// FUN_06f858c0 (06f858c0-06f85a91, RET 4): the base part inline (FmtStr, StrId 0, the
// parameter vector, +0x30 = 0x3a, +0x34 = 0, +0x38 = Vector(0, 0, 0), +0x44 = 0), the id
// vector, then StrId and Params of Source as FUN_06f855f0.
FRmObjectiveType3::FRmObjectiveType3(FRmParamText const* Source)
{
    StrId = Source->StrId;
    if (&Params != &Source->Params)
        Params = Source->Params;
}

// FUN_06f85f60 (06f85f60-06f86029).
FRmObjectiveType3::~FRmObjectiveType3()
{
    for (std::vector<unsigned int>::iterator Id = Ids.begin(); Id != Ids.end(); ++Id)
    {
        for (std::vector<FContentPlayerState*>::iterator Player = Players->begin();
             Player != Players->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, *Id, 0);
    }
}

// FUN_06f85aa0 (06f85aa0-06f85c11): operator new(0x70), the member copy (+0x6c included).
FRmObjective* FRmObjectiveType3::Clone() const
{
    return new FRmObjectiveType3(*this);
}

// LAB_06f5e540 (ICF): MOV EAX,3; RET.
int FRmObjectiveType3::GetType() const
{
    return 3;
}

// LAB_06f85d90 (06f85d90-06f85f5d, RET 0x18). The text: FmtStr::destroy, then the six dwords
// zeroed (STOSD x6, the inline FmtStr ctor) -- the next Build rebuilds it.
void FRmObjectiveType3::SetTargets(std::vector<unsigned int> const* InIds,
                                   std::vector<FContentPlayerState*>* InPlayers,
                                   unsigned int InSystemId, Vector InPosition)
{
    MarkerPosition = InPosition;
    MarkerSystemId = InSystemId;
    MarkerValue = 0;
    Players = InPlayers;
    Text.destroy();
    Text = FmtStr();
    for (std::vector<unsigned int>::iterator Id = Ids.begin(); Id != Ids.end(); ++Id)
    {
        for (std::vector<FContentPlayerState*>::iterator Player = InPlayers->begin();
             Player != InPlayers->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, *Id, 0);
    }
    if (&Ids != InIds)
        Ids = *InIds;
    for (std::vector<unsigned int>::const_iterator Id = InIds->begin(); Id != InIds->end(); ++Id)
    {
        for (std::vector<FContentPlayerState*>::iterator Player = InPlayers->begin();
             Player != InPlayers->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, *Id, 1);
    }
}

// LAB_06f85c40 (06f85c40-06f85d83, RET 0x1c). The local vector is reserved to the record
// count first (FUN_06eb6b30 / FUN_06eed270).
void FRmObjectiveType3::SetTargetRecords(FRmRecordList* Records,
                                         std::vector<FContentPlayerState*>* InPlayers,
                                         unsigned int Mask, unsigned int InSystemId,
                                         Vector InPosition)
{
    std::vector<unsigned int> Found;
    Found.reserve(Records->Records.size());
    for (std::list<FRmSpawnRecord*>::iterator It = Records->Records.begin();
         It != Records->Records.end(); ++It)
    {
        FRmSpawnRecord* const Record = *It;
        if ((Record->Flags & Mask) != 0 && Record->State == 2)
            Found.push_back(Record->ObjectId);
    }
    SetTargets(&Found, InPlayers, InSystemId, InPosition);
}

// =======================================================================================
// FRmObjectiveType4 (vtable 06fbc770)
// =======================================================================================

namespace
{
    // The "away from the origin" test of 06f8612f-06f8617b / 06f861c0-06f86203 /
    // 06f8627d-06f862bd: per coordinate FLD, FMUL ST1, FCOMP 1.0 (DAT_06fb5810), TEST AH,0x41
    // + JP: greater or unordered is away (the z test's JNP jumps on less or equal).
    bool RmIsAwayFromOrigin(Vector const& At)
    {
        double const X = At.x;
        double const Y = At.y;
        double const Z = At.z;
        return !(X * X <= 1.0) || !(Y * Y <= 1.0) || !(Z * Z <= 1.0);
    }
}

// LAB_06f86230 (06f86230-06f862dd, RET 4).
FmtStr* FRmObjectiveType4::Build(IRmTextParamSource* Source)
{
    if (Text.MadLibId == 0)
    {
        Text.begin_mad_lib(static_cast<unsigned int>(StrId));
        for (std::vector<int>::iterator Param = Params.begin(); Param != Params.end(); ++Param)
            Source->GetTextParam(*Param, &Text);
        Text.end_mad_lib();
        if (RmIsAwayFromOrigin(MarkerPosition))
        {
            FmtStr::NavMarker Marker;
            std::memcpy(&Marker.Data[0], &MarkerSystemId, sizeof(unsigned int));
            std::memcpy(&Marker.Data[1], &MarkerValue, sizeof(unsigned int));
            std::memcpy(&Marker.Data[2], &MarkerPosition.x, sizeof(float));
            std::memcpy(&Marker.Data[3], &MarkerPosition.y, sizeof(float));
            std::memcpy(&Marker.Data[4], &MarkerPosition.z, sizeof(float));
            Text.append_nav_marker(Marker);
        }
        Text.append_int(static_cast<unsigned int>(Seconds));
    }
    return &Text;
}

// LAB_06f25080 (ICF): MOV EAX,4; RET.
int FRmObjectiveType4::GetType() const
{
    return 4;
}

// LAB_06f861c0 (06f861c0-06f86220, RET 4).
bool FRmObjectiveType4::GetPosition(Vector* Out)
{
    if (!RmIsAwayFromOrigin(MarkerPosition))
        return false;
    *Out = MarkerPosition;
    return true;
}

// LAB_06f86120 (06f86120-06f861b7, RET 0x18).
void FRmObjectiveType4::SetCountdown(int InSeconds, unsigned int InSystemId, int InValue,
                                     Vector InPosition)
{
    Text.destroy();
    Text = FmtStr();
    if (RmIsAwayFromOrigin(InPosition))
    {
        MarkerSystemId = InSystemId;
        MarkerValue = InValue;
        MarkerPosition = InPosition;
    }
    Seconds = InSeconds;
}

// =======================================================================================
// FRmRuntimeMachine helpers
// =======================================================================================

// FUN_06f8a570: JMP [mission vtable +0x74] (slot 29).
std::vector<FContentPlayerState*>* FRmRuntimeMachine::GetMissionPlayers()
{
    return Mission->GetPlayerStates();
}

// FUN_06f8a580: JMP [mission vtable +0x78] (slot 30).
std::map<unsigned int, FContentPlayerState*>* FRmRuntimeMachine::GetMissionPlayerMap()
{
    return Mission->GetPlayerStateMap();
}

// =======================================================================================
// FRmConversationMessage (vtable 06fbc6d0)
// =======================================================================================

// 06f84f66-06f84f92 (inline in FUN_06f84f10): the base first, then the Clone of Source.
FRmConversationMessage::FRmConversationMessage(FRmConversationBase* Source)
    : StateMachineMessage(GRmMsgType06fd3b74)
    , Conversation(Source->Clone())
{
}

// FUN_06f87dc0: the conversation deleted through its slot 0 when set.
FRmConversationMessage::~FRmConversationMessage()
{
    if (Conversation != nullptr)
        delete Conversation;
}

// FUN_06f87e10: StateMachineMessage::operator new(0x20) and the shallow copy (exported base
// copy ctor, +0x1c, vptr; EH state 0), state -1 at 06f87e65, then +0x1c = its Clone (slot 5,
// 06f87e6d). No null test: a failed new reads +0x1c of null.
StateMachineMessage* FRmConversationMessage::clone() const
{
    FRmConversationMessage* const Copy = new FRmConversationMessage(*this);
    Copy->Conversation = Copy->Conversation->Clone();
    return Copy;
}

// =======================================================================================
// FRmOfferComponent slots 5-7 (vtable 06fbc6ac) and the slot 7 overrides
// =======================================================================================

// FUN_06f84f10 (06f84f10-06f8500f, RET 0xc). Per conversation whose Trigger (slot 8) is
// Trigger: slot 6 (Conversation, Machine, Player); then the message with a slot 5 clone goes
// through this handler's send_delayed_message after the conversation's delay (slot 10) to
// Machine's handler id; the message's dtor deletes the clone.
void FRmOfferComponent::SendConversations(int Trigger, FRmRuntimeMachine* Machine,
                                          FContentPlayerState* Player)
{
    for (std::list<FRmConversation>::iterator It = Conversations.begin();
         It != Conversations.end(); ++It)
    {
        FRmConversation* Conversation = &*It;
        if (Conversation->GetTrigger() != Trigger)
            continue;
        PrepareConversation(Conversation, Machine, Player);
        FRmConversationMessage Message(Conversation);
        unsigned int const Target = static_cast<MessageHandler*>(Machine)->get_id();
        send_delayed_message(&Message, Conversation->GetDelay(), Target);
    }
}

// LAB_06f85020 (06f85020-06f852a5, RET 0xc). MarkReady (slot 9); per line: the texts and the
// players cleared, slot 7 for the listeners (bSpeaker 1) and then the speaker... in retail
// order: slot 7 with 1 first, then with 0; no voice yet -> pub::SpaceObj::GetVoiceID of the
// speaker; the voice line name of the line's text (MakeVoiceLineName(Text, "-", VoiceId))
// appended as a nickname id; +0x28 = 0x4a39, Field_0x8c = 1.
void FRmOfferComponent::PrepareConversation(FRmConversation* Conversation,
                                            FRmRuntimeMachine* Machine,
                                            FContentPlayerState* Player)
{
    Conversation->MarkReady();
    for (std::vector<FGcsLine*>::iterator It = Conversation->Lines.begin();
         It != Conversation->Lines.end(); ++It)
    {
        FRmLine* Line = static_cast<FRmLine*>(*It);
        Line->TextIds.clear();
        Line->PlayerIds.clear();
        ResolveLine(true, Line, Machine, Player);
        ResolveLine(false, Line, Machine, Player);
        if (Line->VoiceId == 0)
            pub::SpaceObj::GetVoiceID(Line->Speaker, Line->VoiceId);
        TString<0x40> const Name = MakeVoiceLineName(Line->Text.Data, "-", Line->VoiceId);
        unsigned int const TextId = pub::GetNicknameId(Name.Data);
        Line->TextIds.push_back(TextId);
        Line->Field_0x28 = 0x4a39;
        Line->Field_0x8c = 1;
    }
}

// LAB_06f852b0 (RET 0x10): Machine's slot 11 with the same arguments.
bool FRmOfferComponent::ResolveLine(bool bSpeaker, FRmLine* Line, FRmRuntimeMachine* Machine,
                                    FContentPlayerState* Player)
{
    return Machine->ResolveLine(bSpeaker, Line, Player);
}

namespace
{
    // The ship and solar slot 7 bodies (LAB_06f88df0 / LAB_06f88af0) differ in two places:
    // the range 8 test (ship: squared distance < 1e8, TEST AH,0x5 + JP; solar: <= 1e8, TEST
    // AH,0x41 + JP) and the bSpeaker argument passed to the machine without Player (ship 1,
    // solar 0).
    bool RmResolveRangeLine(bool bSpeaker, FRmLine* Line, FRmRuntimeMachine* Machine,
                            FContentPlayerState* Player, bool bSolar)
    {
        if (bSpeaker)
            return Machine->ResolveLine(true, Line, Player);
        int const Kind = Line->TargetKind;
        if (Kind != 8 && Kind != 9)
            return Machine->ResolveLine(false, Line, Player);
        if (Player == nullptr)
            return Machine->ResolveLine(!bSolar, Line, nullptr);

        std::vector<FContentPlayerState*>* Players = Machine->GetMissionPlayers();
        // FUN_06eb6b30 (capacity) < the player count -> reallocate to exactly that count.
        Line->PlayerIds.reserve(Players->size());
        Vector Center;
        Player->ShipObject->GetPosition(&Center);
        for (std::vector<FContentPlayerState*>::iterator It = Players->begin();
             It != Players->end(); ++It)
        {
            FContentPlayerState* const Other = *It;
            if (Other->ShipObject == nullptr)
                continue;
            Vector Position;
            Other->ShipObject->GetPosition(&Position);
            if (Kind == 9)
            {
                // 06f88f13-06f88f65: FUN_06eb7810(x, y, z) of the float differences, then
                // (z*z + y*y) + x*x against 1e8 (DAT_06fbc2ac): TEST AH,0x41 + JNZ skips
                // less, equal or unordered -- the player is kept when farther.
                float const Dx = Position.x - Center.x;
                float const Dy = Position.y - Center.y;
                float const Dz = Position.z - Center.z;
                double const DistanceSq = static_cast<double>(Dz) * Dz +
                                          static_cast<double>(Dy) * Dy +
                                          static_cast<double>(Dx) * Dx;
                if (DistanceSq > 100000000.0)
                    Line->PlayerIds.push_back(Other->PlayerId);
            }
            else
            {
                // 06f89063-06f890b3: SubtractVectors (FUN_06eeaee0), (z*z + y*y) + x*x.
                Vector const Delta = SubtractVectors(Position, Center);
                double const DistanceSq = static_cast<double>(Delta.z) * Delta.z +
                                          static_cast<double>(Delta.y) * Delta.y +
                                          static_cast<double>(Delta.x) * Delta.x;
                bool const bInRange = bSolar ? DistanceSq <= 100000000.0
                                             : DistanceSq < 100000000.0;
                if (bInRange)
                    Line->PlayerIds.push_back(Other->PlayerId);
            }
        }
        return true;
    }
}

// LAB_06f88df0 (06f88df0-06f890e3, RET 0x10).
bool FRmShipComponent::ResolveLine(bool bSpeaker, FRmLine* Line, FRmRuntimeMachine* Machine,
                                   FContentPlayerState* Player)
{
    return RmResolveRangeLine(bSpeaker, Line, Machine, Player, false);
}

// LAB_06f88af0 (06f88af0-06f88de3, RET 0x10).
bool FRmSolarComponent::ResolveLine(bool bSpeaker, FRmLine* Line, FRmRuntimeMachine* Machine,
                                    FContentPlayerState* Player)
{
    return RmResolveRangeLine(bSpeaker, Line, Machine, Player, true);
}

// =======================================================================================
// Sub-machine messages
// =======================================================================================

// Inline at 06f86a5a-06f86a6e (StateMachineMessage(Type), +0x1c, vtable 06fbc804).
FRmSolarMessage::FRmSolarMessage(unsigned int Type, unsigned int InObjectId)
    : StateMachineMessage(Type)
    , ObjectId(InObjectId)
{
}

// FUN_06f87cb0: operator new(0x20), the copy ctor, +0x1c.
StateMachineMessage* FRmSolarMessage::clone() const
{
    return new FRmSolarMessage(*this);
}

// FUN_06f86c80 (RET 0xc).
FRmLootMessage::FRmLootMessage(unsigned int Type, unsigned int InObjectId, unsigned int InValue)
    : StateMachineMessage(Type)
    , ObjectId(InObjectId)
    , Value(InValue)
{
}

// FUN_06f86cb0: operator new(0x24), the copy ctor, +0x1c / +0x20.
StateMachineMessage* FRmLootMessage::clone() const
{
    return new FRmLootMessage(*this);
}

// Inline at 06f87612-06f87645.
FRmLootPlayerMessage::FRmLootPlayerMessage(unsigned int Type, unsigned int InObjectId,
                                           unsigned int InValue0, unsigned int InValue1)
    : StateMachineMessage(Type)
    , ObjectId(InObjectId)
    , Value0(InValue0)
    , Value1(InValue1)
{
}

// FUN_06f86d20: operator new(0x28), the copy ctor, +0x1c..+0x24 (the 06fbc80c store first,
// then 06fbc814: an inlined base copy).
StateMachineMessage* FRmLootPlayerMessage::clone() const
{
    return new FRmLootPlayerMessage(*this);
}

// Inline at 06f87f05-06f87f1d.
FRmShipMessage::FRmShipMessage(unsigned int Type, unsigned int InGroup, unsigned int InObjectId)
    : StateMachineMessage(Type)
    , Group(InGroup)
    , ObjectId(InObjectId)
{
}

// FUN_06f87c40: operator new(0x24), the copy ctor, +0x1c / +0x20.
StateMachineMessage* FRmShipMessage::clone() const
{
    return new FRmShipMessage(*this);
}

// FUN_06f87d10 (RET 4): StateMachineMessage(DAT_06fd3b78), +0x1c.
FRmCookieMessage::FRmCookieMessage(unsigned int InCookie)
    : StateMachineMessage(GRmMsgType06fd3b78)
    , Cookie(InCookie)
{
}

// FUN_06f87d40: operator new(0x20), the copy ctor, +0x1c.
StateMachineMessage* FRmCookieMessage::clone() const
{
    return new FRmCookieMessage(*this);
}

// =======================================================================================
// Records
// =======================================================================================

// FUN_06f866a0: +4 = 1, the empty name, +0x3c = 2, +0x40 = 0, +0x44 = -2.0 (DAT_06fbc2e8),
// +0x48 / +0x4c = 0, the empty loot vector, vtable 06fbc7c8.
FRmSpawnRecord::FRmSpawnRecord()
{
    Name.Length = 0;
    Name.Data[0] = '\0';
}

// FUN_06f86700 / FUN_06f87bf0: the loot vector's elements (their label lists) and buffer.
FRmSpawnRecord::~FRmSpawnRecord()
{
}

// FUN_06f86820: every non-null record deleted through its slot 0 with 1.
void FRmRecordList::DeleteAll()
{
    for (std::list<FRmSpawnRecord*>::iterator It = Records.begin(); It != Records.end(); ++It)
    {
        if (*It != nullptr)
            delete *It;
    }
}

// FUN_06f86850 (RET 4).
FRmSpawnRecord* FRmRecordList::FindById(unsigned int Id)
{
    if (Id == 0)
        return nullptr;
    for (std::list<FRmSpawnRecord*>::iterator It = Records.begin(); It != Records.end(); ++It)
    {
        if (Id == (*It)->ObjectId)
            return *It;
    }
    return nullptr;
}

// FUN_06f86880 (RET 0x34): the unrolled strcmp of InName.Data against the record's name.
FRmSpawnRecord* FRmRecordList::FindByName(TString<0x30> InName)
{
    for (std::list<FRmSpawnRecord*>::iterator It = Records.begin(); It != Records.end(); ++It)
    {
        if (std::strcmp(InName.Data, (*It)->Name.Data) == 0)
            return *It;
    }
    return nullptr;
}

// =======================================================================================
// Sub-machines
// =======================================================================================

// 06f89b01-06f89b1a (the base part inline in the runtime machine ctor).
FRmSubMachine::FRmSubMachine(unsigned int InOwnerId)
    : StateMachineMessageHandler(GMessageRouter)
    , OwnerId(InOwnerId)
{
}

void FRmSubMachine::SendToOwner(StateMachineMessage* Message)
{
    send_immediate_message(Message, OwnerId);
}

FRmSolarMachine::FRmSolarMachine(unsigned int InOwnerId)
    : FRmSubMachine(InOwnerId)
{
}

// FUN_06f86920.
FRmSolarMachine::~FRmSolarMachine()
{
    Records.DeleteAll();
}

// LAB_06f86a00 (06f86a00-06f86ab7, RET 8). True for every message 0x39 in state 0.
bool FRmSolarMachine::Slot2(SMEventData const* Event, int const& State)
{
    if (State < 0 || State != 0)
        return false;
    SMControllerEvent const* Controller = static_cast<SMControllerEvent const*>(Event);
    // The type 4 event's int +0x08 is the controller message id (FRandomMissionObject::
    // HandleMissionMessage), cast back after the type test.
    if (Event->get_event_type() != 4 ||
        static_cast<pub::Controller::EMessage>(Controller->ControllerId) !=
            pub::Controller::EMessage::ObjectDestroyed)
        return false;
    unsigned int const Id = *static_cast<unsigned int const*>(Controller->DataPtr);
    FRmSpawnRecord* Record = Records.FindById(Id);
    if (Record != nullptr)
    {
        unsigned int const ObjectId = Record->ObjectId;
        Record->State = 3;
        FRmSolarMessage Message(GRmMsgType06fd3b9c, ObjectId);
        send_immediate_message(&Message, OwnerId);
    }
    return true;
}

// FUN_06f86ae0 (06f86ae0-06f86c78, RET 0x10): a local list filled in record order, returned
// as a copy (06f86bbe-06f86c06).
FRmRecordList FRmSolarMachine::Query(int Kind, int InState, unsigned int Mask)
{
    FRmRecordList Result;
    for (std::list<FRmSpawnRecord*>::iterator It = Records.Records.begin();
         It != Records.Records.end(); ++It)
    {
        FRmSolarRecord* const Record = static_cast<FRmSolarRecord*>(*It);
        if (Kind != -1 && Record->SolarType != Kind)
            continue;
        if (InState != 0 && Record->State != InState)
            continue;
        if (Mask != 0 && (Record->Flags & Mask) == 0)
            continue;
        Result.Records.push_back(Record);
    }
    return Result;
}

FRmShipMachine::FRmShipMachine(unsigned int InOwnerId)
    : FRmSubMachine(InOwnerId)
{
}

// FUN_06f87990.
FRmShipMachine::~FRmShipMachine()
{
    Records.DeleteAll();
}

// LAB_06f87e90 (06f87e90-06f880b8, RET 8). After the destroyed message: the record's group;
// every member destroyed (State 3) -> DAT_06fd3ba4; a record with Flags bit 1 and no alive
// member with Flags bit 1 left -> DAT_06fd3ba0. True for every message 0x39 in state 0.
bool FRmShipMachine::Slot2(SMEventData const* Event, int const& State)
{
    if (State < 0 || State != 0)
        return false;
    SMControllerEvent const* Controller = static_cast<SMControllerEvent const*>(Event);
    // The type 4 event's int +0x08 is the controller message id, cast back after the type test.
    if (Event->get_event_type() != 4 ||
        static_cast<pub::Controller::EMessage>(Controller->ControllerId) !=
            pub::Controller::EMessage::ObjectDestroyed)
        return false;
    unsigned int const Id = *static_cast<unsigned int const*>(Controller->DataPtr);
    FRmShipRecord* Record = static_cast<FRmShipRecord*>(Records.FindById(Id));
    if (Record == nullptr)
        return true;

    unsigned int const Group = Record->Group;
    unsigned int const ObjectId = Record->ObjectId;
    Record->State = 3;
    // FuncInfo 06fc627c: the destroyed message is EH state 0 up to the exit (06f88091); the
    // group list (state 1) and the two later messages (states 2 / 3) nest inside it.
    FRmShipMessage Destroyed(GRmMsgType06fd3ba8, Group, ObjectId);
    send_immediate_message(&Destroyed, OwnerId);

    FRmRecordList Members = QueryGroup(Record->Group);
    bool const bFlagged = ((Record->Flags >> 1) & 1) != 0;
    bool bNoFlaggedAlive = true;
    unsigned int DeadCount = 0;
    for (std::list<FRmSpawnRecord*>::iterator It = Members.Records.begin();
         It != Members.Records.end(); ++It)
    {
        int const MemberState = (*It)->State;
        if (MemberState == 3)
            ++DeadCount;
        if (MemberState == 2 && ((*It)->Flags & 2) != 0)
            bNoFlaggedAlive = false;
    }
    if (DeadCount == Members.Records.size())
    {
        FRmShipMessage Message(GRmMsgType06fd3ba4, Record->Group, Record->ObjectId);
        send_immediate_message(&Message, OwnerId);
    }
    if (bFlagged && bNoFlaggedAlive)
    {
        FRmShipMessage Message(GRmMsgType06fd3ba0, Record->Group, Record->ObjectId);
        send_immediate_message(&Message, OwnerId);
    }
    return true;
}

// FUN_06f87a50 (06f87a50-06f87be7, RET 0x10).
FRmRecordList FRmShipMachine::Query(int Kind, int InState, unsigned int Mask)
{
    FRmRecordList Result;
    for (std::list<FRmSpawnRecord*>::iterator It = Records.Records.begin();
         It != Records.Records.end(); ++It)
    {
        FRmShipRecord* const Record = static_cast<FRmShipRecord*>(*It);
        if (Kind != 0 && Record->Kind != Kind)
            continue;
        if (InState != 0 && Record->State != InState)
            continue;
        if (Mask != 0 && (Record->Flags & Mask) == 0)
            continue;
        Result.Records.push_back(Record);
    }
    return Result;
}

// FUN_06f880c0 (06f880c0-06f88239, RET 8).
FRmRecordList FRmShipMachine::QueryGroup(unsigned int Group)
{
    FRmRecordList Result;
    if (Group != 0xffffffffu)
    {
        for (std::list<FRmSpawnRecord*>::iterator It = Records.Records.begin();
             It != Records.Records.end(); ++It)
        {
            FRmShipRecord* const Record = static_cast<FRmShipRecord*>(*It);
            if (Group == Record->Group)
                Result.Records.push_back(Record);
        }
    }
    return Result;
}

FRmLootMachine::FRmLootMachine(unsigned int InOwnerId)
    : FRmSubMachine(InOwnerId)
{
}

// FUN_06f86ef0.
FRmLootMachine::~FRmLootMachine()
{
    Records.DeleteAll();
}

// LAB_06f86fb0 (06f86fb0-06f877b6, RET 8). State 0 only; every handled message answers true.
bool FRmLootMachine::Slot2(SMEventData const* Event, int const& State)
{
    if (State < 0 || State != 0)
        return false;

    // 06f86fe7-06f872ab: message DAT_06fd3b6c {+0x1c loot id, +0x20 ship id} for this state:
    // the loot is taken from its holder and created again next to the ship.
    if (Event->get_event_type() == 3)
    {
        StateMachineMessage const* Message = static_cast<SMMessage const*>(Event)->Message;
        if ((Message->DeliveryState == -1 || Message->DeliveryState == get_state()) &&
            Message->MessageType == GRmMsgType06fd3b6c)
        {
            // The 0x24-byte FRmLootMessage layout: +0x1c the loot, +0x20 the ship.
            FRmLootMessage const* Drop = static_cast<FRmLootMessage const*>(Message);
            unsigned int const* ShipId = &Drop->Value;
            FContentPlayerState* Player = FindContentPlayerStateByShip(ShipId);
            FRmLootRecord* Record = static_cast<FRmLootRecord*>(Records.FindById(Drop->ObjectId));
            pub::Player::RemoveCargo(Record->HolderId, Record->CargoSlot, 1);

            pub::SpaceObj::LootInfo Info;
            Info.SystemID = 0;
            Info.EquipArchID = 0;
            Info.NameId = 0;
            Info.OwnerObjId = 0;
            Info.Velocity = Vector{0.0f, 0.0f, 0.0f};
            Info.Position = Vector{0.0f, 0.0f, 0.0f};
            Info.AngularVelocity = Vector{0.0f, 0.0f, 0.0f};
            std::memset(&Info.Orientation, 0, sizeof(Info.Orientation));
            Info.Orientation.E[0][0] = 1.0f;
            Info.Orientation.E[1][1] = 1.0f;
            Info.Orientation.E[2][2] = 1.0f;
            Info.Units = 0;
            Info.HitPtsScale = 0.0f;
            Info.bCanAiTractor = 1;
            Info.bMission = 0;
            Info.MarkForPlayer = 0;

            Info.SystemID = Player->GetSystemRecord()->SystemId;
            pub::SpaceObj::GetArchetypeID(Record->GoodId, Info.EquipArchID);
            unsigned int const PlayerShip = Player->GetShipId();
            Info.OwnerObjId = PlayerShip;
            Vector Linear;
            Vector Angular;
            pub::SpaceObj::GetMotion(PlayerShip, Linear, Angular);
            Vector ShipPosition;
            Player->ShipObject->GetPosition(&ShipPosition);
            // 06f87148-06f871ef: position = ship + (0, 10, 0) (DAT_06fb4438), velocity = the
            // ship's + (0, 10, 0), the ship's angular velocity, one unit.
            Info.Position = Vector{ShipPosition.x, ShipPosition.y + 10.0f, ShipPosition.z};
            Info.Velocity = Vector{Linear.x, Linear.y + 10.0f, Linear.z};
            Info.AngularVelocity = Angular;
            Info.Units = 1;
            // 06f871fa-06f87235: rand() * 2^-15 (DAT_06fb4448) * 0.5 + 0.5 (double DAT_06fb74e0).
            int const Roll = std::rand();
            float const Scaled = static_cast<float>(Roll) * 3.0517578125e-05f;
            Info.HitPtsScale = static_cast<float>(static_cast<double>(Scaled) * 0.5 + 0.5);
            Info.bCanAiTractor = 0;
            Info.bMission = 1;
            pub::SpaceObj::CreateLoot(Record->ObjectId, Info);

            unsigned int const LootId = Record->ObjectId;
            Record->State = 2;
            Record->Units = static_cast<int>(Info.Units);
            Record->HolderId = 0;
            Record->CargoSlot = SUBOBJ_ID_NONE;
            FRmLootMessage Reply(GRmMsgType06fd3b84, LootId, *ShipId);
            send_immediate_message(&Reply, OwnerId);
            return true;
        }
    }

    using pub::Controller::EMessage;
    SMControllerEvent const* Controller = static_cast<SMControllerEvent const*>(Event);
    // A type 4 event's int +0x08 is the controller message id, cast back after the type test.

    // 06f872b0-06f87419: message 0x39 {id, ..., +0x10 cause} -- the loot is gone.
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Controller->ControllerId) == EMessage::ObjectDestroyed)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        FRmLootRecord* Record = static_cast<FRmLootRecord*>(Records.FindById(Parms[0]));
        if (Record == nullptr)
            return true;
        int const Cause = static_cast<int>(Parms[4]);
        if (Cause == 1 || Cause == 0)
            Record->State = 3;
        else
            Record->State = 4;
        if (Cause == 1)
        {
            FRmLootMessage Message(GRmMsgType06fd3b94, Record->ObjectId, 0);
            send_immediate_message(&Message, OwnerId);
        }
        else if (Cause == 0)
        {
            FRmLootMessage Message(GRmMsgType06fd3b80, Record->ObjectId, 0);
            send_immediate_message(&Message, OwnerId);
        }
        if (Record->Kind == 2)
        {
            bool bNoneLeft = false;
            {
                FRmRecordList Alive = Query(0, 2, 0, 2, 0);
                bNoneLeft = Alive.Records.empty();
            }
            if (bNoneLeft)
            {
                FRmLootMessage Message(GRmMsgType06fd3b8c, Record->ObjectId, Parms[1]);
                send_immediate_message(&Message, OwnerId);
            }
        }
        return true;
    }

    // 06f8741e-06f87593: message 0x18 {id, ship, word slot, good, units} -- picked up.
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Controller->ControllerId) == EMessage::LootAcquired)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        FRmLootRecord* Record = static_cast<FRmLootRecord*>(Records.FindById(Parms[0]));
        if (Record == nullptr)
            return true;
        Record->Units -= static_cast<int>(Parms[4]);
        Record->GoodId = Parms[3];
        Record->CargoSlot = *reinterpret_cast<unsigned short const*>(&Parms[2]);
        unsigned int const* ShipId = &Parms[1];
        Record->HolderId = FindContentPlayerStateByShip(ShipId)->PlayerId;
        if (Record->Units == 0)
        {
            // FuncInfo 06fc6100: the picked-up message is EH state 4 from 06f874ca to the exit,
            // so it outlives the critical-loot test, the query and the 3b8c message (state 5).
            FRmLootMessage PickedUp(GRmMsgType06fd3b90, Record->ObjectId, *ShipId);
            send_immediate_message(&PickedUp, OwnerId);
            if (Record->Kind == 2)
            {
                bool bNoneLeft = false;
                {
                    FRmRecordList Alive = Query(0, 2, 0, 2, 0);
                    bNoneLeft = Alive.Records.empty();
                }
                if (bNoneLeft)
                {
                    FRmLootMessage Message(GRmMsgType06fd3b8c, Record->ObjectId, *ShipId);
                    send_immediate_message(&Message, OwnerId);
                }
            }
        }
        return true;
    }

    // 06f87598-06f876d6: message 0x27 {ship, word 1}: the critical loot the player holds.
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Controller->ControllerId) == EMessage::Msg0x27)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        FContentPlayerState* Player = FindContentPlayerStateByShip(Parms);
        if (Player == nullptr)
            return true;
        FRmRecordList Held = Query(0, 2, 0, 4, Player->PlayerId);
        if (!Held.Records.empty())
        {
            for (std::list<FRmSpawnRecord*>::iterator It = Held.Records.begin();
                 It != Held.Records.end(); ++It)
            {
                FRmLootPlayerMessage Message(GRmMsgType06fd3b88, (*It)->ObjectId, Parms[0],
                                             Parms[1]);
                send_immediate_message(&Message, OwnerId);
            }
        }
        return true;
    }

    // 06f876db-06f8779a: message 0x3d {system}: the first alive critical loot of that system
    // is destroyed.
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Controller->ControllerId) == EMessage::SystemUnloaded)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        FRmRecordList Alive = Query(0, 2, 0, 2, 0);
        if (!Alive.Records.empty())
        {
            FRmSpawnRecord* const First = Alive.Records.front();
            unsigned int System = 0;
            pub::SpaceObj::GetSystem(First->ObjectId, System);
            if (Parms[0] == System)
                pub::SpaceObj::Destroy(First->ObjectId, static_cast<DestroyType>(0));
        }
        return true;
    }
    return false;
}

// =======================================================================================
// FRmRuntimeMachine
// =======================================================================================

float GRmMultiplayerDelay = 30.0f;

namespace
{
    // The x87 squared distance of FUN_06f89e90 / FUN_06f8bb80: the three differences kept in
    // extended precision, summed (x*x + z*z) + y*y.
    double RmDistanceSq(Vector const& A, Vector const& B)
    {
        double const Dx = static_cast<double>(A.x) - static_cast<double>(B.x);
        double const Dy = static_cast<double>(A.y) - static_cast<double>(B.y);
        double const Dz = static_cast<double>(A.z) - static_cast<double>(B.z);
        return (Dx * Dx + Dz * Dz) + Dy * Dy;
    }
}

// FUN_06f89a00 (06f89a00-06f89c96, RET 4). The three sub-machines are added with the
// StateMachineGroup import (not FRmMachineGroup::AddMachine); their owner id is this
// machine's handler id, read before each is built.
FRmRuntimeMachine::FRmRuntimeMachine(FRmOffer* InOffer)
    : FRmMachineGroup(GMessageRouter)
    , Mission(InOffer->Mission)
    , Offer(InOffer)
{
    if (!pub::SinglePlayer())
        GRmMultiplayerDelay = 45.0f;
    Solars = new FRmSolarMachine(get_id());
    StateMachineGroup::add_state_machine(Solars);
    Ships = new FRmShipMachine(get_id());
    StateMachineGroup::add_state_machine(Ships);
    Loot = new FRmLootMachine(get_id());
    StateMachineGroup::add_state_machine(Loot);
}

// FUN_06f89cc0: the members in reverse order (Offer released, the objective vector, the
// objective list with its objectives), then ~FRmMachineGroup.
FRmRuntimeMachine::~FRmRuntimeMachine()
{
}

// LAB_06f8a5c0 (RET 8).
bool FRmRuntimeMachine::Slot2(SMEventData const* Event, int const& State)
{
    if (State < 0)
        return false;
    if (State == 0)
        OnState0(Event, State);
    else if (State == 1)
        OnState1(Event, State);
    else if (State == 2)
        OnState2(Event, State);
    return false;
}

// LAB_06ebf720: RET.
void FRmRuntimeMachine::Shutdown()
{
}

// FUN_06f89dc0 (06f89dc0-06f89e2b).
unsigned int FRmRuntimeMachine::GetSystemId()
{
    if (Objectives.Objectives.empty())
        return Offer->System->SystemId;
    unsigned int SystemId = 0;
    std::vector<FRmObjective*>::iterator It = ActiveObjectives.begin();
    for (; It != ActiveObjectives.end(); ++It)
    {
        if ((*It)->GetSystemId(&SystemId))
            break;
    }
    if (It != ActiveObjectives.end())
        return SystemId;
    return Offer->System->SystemId;
}

// FUN_06f89e30 (RET 0x34): slot 3 of the solar machine's list (+0x20).
FRmSpawnRecord* FRmRuntimeMachine::FindSolarByName(TString<0x30> InName)
{
    return Solars->Records.FindByName(InName);
}

// FUN_06f89e60 (RET 0x34).
FRmSpawnRecord* FRmRuntimeMachine::FindShipByName(TString<0x30> InName)
{
    return Ships->Records.FindByName(InName);
}

// LAB_06f89e90 (06f89e90-06f8a537, RET 0xc). Speaker kinds (FRmCommLine::Speaker - 1, jump
// table 06f8a53c): 1 BASE (the offer's costume and voice, +0x2c = 0x32d7), 2 BIG_SOLAR (the
// first alive solar of type 0), 3 BIG_SHIP (the first ship of kind 2), 4 DEFENSIVE_SHIP (kind 1
// and alive), 5 FRIENDLY_SHIP (attitude > 0.0 and alive). Listener kinds (FRmCommSequence::
// Target - 6, jump table 06f8a550): PLAYER, ALL_PLAYERS, PLAYERS_IN_RANGE /
// PLAYERS_OUT_OF_RANGE (1e8 squared around Player's ship or the mission area), PLAYERS_IN_SYSTEM
// / PLAYERS_OUT_OF_SYSTEM.
bool FRmRuntimeMachine::ResolveLine(bool bSpeaker, FRmLine* Line, FContentPlayerState* Player)
{
    if (bSpeaker)
    {
        switch (Line->SpeakerKind)
        {
        case 1:
            Line->Speaker = 0;
            Line->CostumeId = Offer->McCostume;
            Line->VoiceId = Offer->VoiceId;
            Line->Field_0x2c = 0x32d7;
            return true;
        case 2:
        {
            FRmRecordList Found = Solars->Query(0, 2, 0);
            if (Found.Records.empty())
                return false;
            Line->Speaker = Found.Records.front()->ObjectId;
            return true;
        }
        case 3:
            for (std::list<FRmSpawnRecord*>::iterator It = Ships->Records.Records.begin();
                 It != Ships->Records.Records.end(); ++It)
            {
                if (static_cast<FRmShipRecord*>(*It)->Kind == 2)
                {
                    Line->Speaker = (*It)->ObjectId;
                    return true;
                }
            }
            return false;
        case 4:
            for (std::list<FRmSpawnRecord*>::iterator It = Ships->Records.Records.begin();
                 It != Ships->Records.Records.end(); ++It)
            {
                if (static_cast<FRmShipRecord*>(*It)->Kind == 1 && (*It)->State == 2)
                {
                    Line->Speaker = (*It)->ObjectId;
                    return true;
                }
            }
            return false;
        case 5:
            // 06f89fa3-06f89fb6: FCOMP 0.0 (DAT_06fbc29c), TEST AH,0x41 + JNZ: less, equal or
            // unordered skip.
            for (std::list<FRmSpawnRecord*>::iterator It = Ships->Records.Records.begin();
                 It != Ships->Records.Records.end(); ++It)
            {
                if ((*It)->Attitude > 0.0f && (*It)->State == 2)
                {
                    Line->Speaker = (*It)->ObjectId;
                    return true;
                }
            }
            return false;
        default:
            return false;
        }
    }

    switch (Line->TargetKind)
    {
    case 6:
        Line->PlayerIds.push_back(Player->PlayerId);
        return true;
    case 7:
    {
        std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
        for (std::vector<FContentPlayerState*>::iterator It = Players->begin();
             It != Players->end(); ++It)
            Line->PlayerIds.push_back((*It)->PlayerId);
        return true;
    }
    case 8:
    case 9:
    {
        std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
        Vector Center;
        if (Player != nullptr)
            Player->ShipObject->GetPosition(&Center);
        else
            Center = Position;
        for (std::vector<FContentPlayerState*>::iterator It = Players->begin();
             It != Players->end(); ++It)
        {
            if ((*It)->ShipObject == nullptr)
                continue;
            Vector Other;
            (*It)->ShipObject->GetPosition(&Other);
            double const DistanceSq = RmDistanceSq(Other, Center);
            // 06f8a22e (kind 8): TEST AH,0x41 + JP skips greater only; 06f8a317 (kind 9):
            // TEST AH,0x41 + JNZ skips less, equal and unordered.
            bool const bKeep = Line->TargetKind == 8 ? !(DistanceSq > 100000000.0)
                                                     : DistanceSq > 100000000.0;
            if (bKeep)
                Line->PlayerIds.push_back((*It)->PlayerId);
        }
        return true;
    }
    case 10:
    case 11:
    {
        // 06f8a0e8-06f8a12f / 06f8a150-06f8a197: the player index advances only past a
        // player WITHOUT a ship (ADD EDI,4 at 06f8a129 / 06f8a191 is jumped over by both the
        // match and the push path). Retail therefore never leaves the loop once a mission
        // player has a ship; the port keeps that.
        std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
        std::vector<FContentPlayerState*>::iterator It = Players->begin();
        while (It != Players->end())
        {
            if ((*It)->ShipObject == nullptr)
            {
                ++It;
                continue;
            }
            unsigned int const PlayerSystem = (*It)->GetSystemRecord()->SystemId;
            bool const bSame = PlayerSystem == GetSystemId();
            if (bSame == (Line->TargetKind == 10))
                Line->PlayerIds.push_back((*It)->PlayerId);
        }
        return true;
    }
    default:
        return false;
    }
}

// LAB_06f8a590.
bool FRmRuntimeMachine::CanSave()
{
    if (get_state() == 5 || get_state() == 6 || get_state() == 7)
        return false;
    return true;
}

// LAB_06f71100 (ICF with FRmTrigger::OnIdle).
bool FRmRuntimeMachine::OnState0(SMEventData const* Event, int const& State)
{
    if (State == 0 && Event->get_event_type() < 4 && Event->get_event_type() == 1)
    {
        set_state(1);
        return true;
    }
    return false;
}

// LAB_06f8a610 (06f8a610-06f8a669, RET 8).
bool FRmRuntimeMachine::OnState1(SMEventData const* Event, int const& State)
{
    if (State != 1 || Event->get_event_type() != 3)
        return false;
    StateMachineMessage const* Message = static_cast<SMMessage const*>(Event)->Message;
    if (Message->DeliveryState != -1 && get_state() != Message->DeliveryState)
        return false;
    if (Message->MessageType != GRmMsgStartMachine)
        return false;
    set_state(2);
    return true;
}

// LAB_06f8a670 (06f8a670-06f8a6ac, RET 8).
bool FRmRuntimeMachine::OnState2(SMEventData const* Event, int const& State)
{
    if (State == 2 && Event->get_event_type() < 4 && Event->get_event_type() == 1)
    {
        set_state(3);
        return true;
    }
    return false;
}

// FUN_06f8b160 (06f8b160-06f8b24b): over a copy of the mission's players.
void FRmRuntimeMachine::RefreshObjectives()
{
    std::vector<FContentPlayerState*> Players = *GetMissionPlayers();
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end();
         ++It)
    {
        unsigned int const MachineSystem = GetSystemId();
        if ((*It)->GetSystemId() == MachineSystem)
            UpdatePlayerObjectives((*It)->PlayerId);
    }
    bObjectivesChanged = false;
}

// FUN_06f8b250 (06f8b250-06f8b32a): over a copy of the mission's players, mission slot 2.
void FRmRuntimeMachine::SendPlaceholderObjectives()
{
    std::vector<FContentPlayerState*> Players = *GetMissionPlayers();
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end();
         ++It)
        Mission->GetOrCreatePlayer((*It)->PlayerId)->SendPlaceholderObjective();
}

// FUN_06f8b330 (06f8b330-06f8b618, RET 4).
void FRmRuntimeMachine::UpdatePlayerObjectives(unsigned int InPlayerId)
{
    FMissionPlayer* Player = Mission->GetOrCreatePlayer(InPlayerId);
    if (Player == nullptr)
        return;
    std::list<FMissionObjective*>& List = Player->Objectives.Objectives;

    // 06f8b35b-06f8b3d5: the machine's run starts after the first objective of a player
    // without a ship and after the type 5 path objectives; it ends at a type 8 or 5 objective.
    std::list<FMissionObjective*>::iterator First = List.begin();
    bool bReplaced = false;
    // Port-only STL adaptation (CONTENT-OBJECTIVE-END-ITERATOR in deviations.md).
    // Retail 06f8b376 has no end check: MOV EAX,[EAX] follows the circular node's Next
    // pointer. On an empty VC6 list the sentinel points to itself. Modern std::list
    // forbids ++end(), so retain end() here and continue the objective update.
    // Keep this check unless an equivalent empty-list-safe traversal replaces it.
    if (Player->PlayerState->ShipObject == nullptr && First != List.end())
        ++First;
    while (First != List.end() && (*First)->GetType() == 5)
        ++First;
    std::list<FMissionObjective*>::iterator Last = First;
    while (Last != List.end() && (*Last)->GetType() != 8 && (*Last)->GetType() != 5)
        ++Last;

    // 06f8b3d7-06f8b4a2: the run is replaced by clones of the machine's objectives unless it
    // already matches them one by one (the player's objective's IsSame, slot 6).
    bool bSame = true;
    {
        std::list<FMissionObjective*>::iterator Mine = Objectives.Objectives.begin();
        std::list<FMissionObjective*>::iterator Theirs = First;
        for (; Theirs != Last; ++Theirs, ++Mine)
        {
            if (Mine == Objectives.Objectives.end() || !(*Theirs)->IsSame(*Mine))
            {
                bSame = false;
                break;
            }
        }
        if (bSame && Mine != Objectives.Objectives.end())
            bSame = false;
    }
    if (!bSame)
    {
        Player->Objectives.EraseRange(First, Last);
        Player->Objectives.InsertClones(Last, &Objectives, Mission);
        bReplaced = true;
    }

    // 06f8b4a7-06f8b4bf: only a player in this machine's system gets anything sent.
    FContentPlayerState* State = Player->PlayerState;
    if (State->GetSystemRecord()->SystemId != GetSystemId())
        return;

    bool bPathChanged = false;
    if (State->ShipObject != nullptr && State->ShipObject->HasField120())
    {
        // 06f8b4e2-06f8b579: once per launch, a reached first path objective is dropped
        // (squared distance < 1e6, DAT_06fb8570; TEST AH,0x5 + JP skips greater, equal and
        // unordered).
        if (Player->Field_0x24 == 0)
        {
            Player->Field_0x24 = 1;
            if (!List.empty())
            {
                FMissionObjective* Front = Player->Objectives.GetFront();
                if (Front->GetType() == 5)
                {
                    Vector Now;
                    State->GetPosition(&Now);
                    FNNObjectiveType5 const* Leg = static_cast<FNNObjectiveType5 const*>(Front);
                    Vector LegPoint;
                    std::memcpy(&LegPoint.x, &Leg->Marker.Data[2], sizeof(float));
                    std::memcpy(&LegPoint.y, &Leg->Marker.Data[3], sizeof(float));
                    std::memcpy(&LegPoint.z, &Leg->Marker.Data[4], sizeof(float));
                    Vector Delta;
                    Vec3Assign(&Delta, Now.x - LegPoint.x, Now.y - LegPoint.y,
                               Now.z - LegPoint.z);
                    double const DistanceSq = static_cast<double>(Delta.z) * Delta.z +
                                              static_cast<double>(Delta.y) * Delta.y +
                                              static_cast<double>(Delta.x) * Delta.x;
                    if (DistanceSq < 1000000.0)
                    {
                        Player->Objectives.PopFront();
                        bPathChanged = true;
                    }
                }
            }
        }
    }
    else
    {
        // 06f8b580-06f8b5ca: the path objectives (slot 19); a single path objective makes
        // the objective after it carry flags 0x50.
        Player->Field_0x24 = 0;
        UpdatePathObjectives(Player, &bPathChanged);
        if (bPathChanged)
        {
            std::list<FMissionObjective*>::iterator It = List.begin();
            int PathCount = 0;
            while (It != List.end() && (*It)->GetType() == 5)
            {
                ++It;
                ++PathCount;
            }
            if (PathCount == 1 && It != List.end())
                (*It)->Objective.Type |= 0x50;
        }
    }

    // 06f8b5ce-06f8b5ef: outside single player the mission's slot 31.
    bool bCountChanged = false;
    if (!pub::SinglePlayer())
        Mission->UpdatePlayerCountObjective(Player, &bCountChanged);
    if (bReplaced || bPathChanged || bCountChanged)
        Player->SendObjectives();
}

// FUN_06f8b620 (06f8b620-06f8b924, RET 8).
bool FRmRuntimeMachine::UpdatePathObjectives(FMissionPlayer* Player, bool* bChanged)
{
    FMissionObjectiveList* List = &Player->Objectives;
    Vector Target;
    if (!ActiveObjectives.empty() && ActiveObjectives[0]->GetPosition(&Target))
    {
        FRmObjective* const First = ActiveObjectives[0];
        std::list<FNnPathLeg> Legs;
        unsigned int SystemId = GetSystemId();
        FSystemInfo* System = FindSystemInfo(&SystemId);
        FContentPlayerState* State = Player->PlayerState;
        Vector PlayerPosition;
        State->GetPosition(&PlayerPosition);
        // 06f8b6c9-06f8b70c: a player at the origin (every coordinate squared <= 1.0) gets no
        // path (TEST AH,0x41 + JP: greater or unordered builds).
        bool const bPlaced =
            !(static_cast<double>(PlayerPosition.x) * PlayerPosition.x <= 1.0) ||
            !(static_cast<double>(PlayerPosition.y) * PlayerPosition.y <= 1.0) ||
            !(static_cast<double>(PlayerPosition.z) * PlayerPosition.z <= 1.0);
        if (bPlaced)
        {
            float const Range = 0.1f;
            Vector From;
            State->GetPosition(&From);
            BuildNnPath(System, &From, &Target, &Legs, 0x19, &Range, 1);

            int Ids = 0;
            int const MachineType = Offer->MachineType;
            if (MachineType == 1)
                Ids = ((std::rand() * 3) >> 15) + 0x3307;
            else if (MachineType == 2)
                Ids = ((std::rand() * 3) >> 15) + 0x3304;
            else if (MachineType == 3)
                Ids = ((std::rand() * 3) >> 15) + 0x330a;

            bool bWhere = false;
            if (State->ShipObject == nullptr && !List->Objectives.empty())
                bWhere = true;
            std::list<FMissionObjective*>::iterator Where = List->Objectives.begin();

            // 06f8b7d1-06f8b80d: objective key 0xc: the first object found at the target
            // (ScanObjects, 0x20 at most, radius 0, mask 0x600de0). Retail reads the buffer's
            // first word even when nothing was found (stack garbage); the port's buffer is 0.
            unsigned int TargetObjectId = 0;
            if (First->Key == 0xc)
            {
                unsigned int ScanSystem = System->SystemId;
                unsigned int Buffer[0x20] = {};
                unsigned int Count = 0;
                pub::System::ScanObjects(ScanSystem, Buffer, 0x20, Target, 0.0f, 0x600de0, Count);
                TargetObjectId = Buffer[0];
            }
            int const FirstType = State->ShipObject != nullptr ? 0x5a : 0x0a;
            *bChanged = Mission->BuildPathObjectives(System, &Legs, List, Ids, 1, 1,
                                                     TargetObjectId, FirstType, 9, 0xa,
                                                     bWhere ? &Where : nullptr);
        }
        return *bChanged;
    }

    // 06f8b88d-06f8b90b / 06f8b8cd-06f8b90b: no target -> the path objectives at the front are
    // dropped (and reported when there was one).
    if (!List->Objectives.empty())
    {
        if (List->GetFrontType() == 5)
            *bChanged = true;
        while (!List->Objectives.empty() && List->GetFrontType() == 5)
            List->PopFront();
    }
    return *bChanged;
}

// FUN_06f8bac0 (06f8bac0-06f8bb71, RET 4). The vector index stays valid across
// RebuildObjective (erase + insert at the same place, no reallocation).
void FRmRuntimeMachine::OnPlayerJoined(unsigned int InPlayerId)
{
    SetPlayerAttitudes(FindContentPlayerState(InPlayerId));
    for (size_t Index = 0; Index < ActiveObjectives.size(); ++Index)
    {
        FRmObjective* const Objective = ActiveObjectives[Index];
        if (Objective->GetType() != 3)
            continue;
        FRmObjectiveType3* const Targets = static_cast<FRmObjectiveType3*>(Objective);
        unsigned int SystemId = 0;
        Targets->GetSystemId(&SystemId);
        Vector Where;
        Targets->GetPosition(&Where);
        Targets->SetTargets(&Targets->Ids, GetMissionPlayers(), SystemId, Where);
        RebuildObjective(Targets->Key);
    }
    RefreshObjectives();
}

// FUN_06f8b930 (06f8b930-06f8bab6, RET 4). TEST AH,0x44 + JNP skips an attitude equal to
// -2.0 (DAT_06fbc2e8).
void FRmRuntimeMachine::SetPlayerAttitudes(FContentPlayerState* Player)
{
    FRmRecordList ShipList = Ships->Query(0, 2, 0);
    for (std::list<FRmSpawnRecord*>::iterator It = ShipList.Records.begin();
         It != ShipList.Records.end(); ++It)
    {
        FRmSpawnRecord* const Record = *It;
        if (Record->Attitude == -2.0f)
            continue;
        int const PlayerRep = Player->GetReputation();
        int const ShipRep = Record->NpcShip->GetReputation();
        pub::Reputation::SetAttitude(ShipRep, PlayerRep, Record->Attitude);
    }
    FRmRecordList SolarList = Solars->Query(-1, 2, 0);
    for (std::list<FRmSpawnRecord*>::iterator It = SolarList.Records.begin();
         It != SolarList.Records.end(); ++It)
    {
        FRmSolarRecord* const Record = static_cast<FRmSolarRecord*>(*It);
        if (Record->Attitude == -2.0f)
            continue;
        int const PlayerRep = Player->GetReputation();
        pub::Reputation::SetAttitude(Record->Reputation, PlayerRep, Record->Attitude);
    }
}

// FUN_06f92680 (RET 4).
void FRmRuntimeMachine::SpawnSolars(std::list<FRmSolarSpawn>* InSolars)
{
    for (std::list<FRmSolarSpawn>::iterator It = InSolars->begin(); It != InSolars->end(); ++It)
        SpawnSolar(&*It);
}

// FUN_06f926b0 (06f926b0-06f92dcd, RET 4).
bool FRmRuntimeMachine::SpawnSolar(FRmSolarSpawn* Solar)
{
    if (Offer->System == nullptr)
        return false;
    bool bCreated = false;

    // 06f926ee-06f92870: the SolarInfo.
    pub::SpaceObj::SolarInfo Info;
    std::memset(&Info, 0, sizeof(Info));
    Info.Orientation.E[0][0] = 1.0f;
    Info.Orientation.E[1][1] = 1.0f;
    Info.Orientation.E[2][2] = 1.0f;
    Info.VibeGroupId = -1;
    Info.Field_0x80 = 1;
    Info.HitPts = -1;
    Costume SolarCostume;
    FSystemInfo* System = FindSystemInfo(&Solar->DestSys);
    std::strcpy(Info.Nickname, Solar->Nickname.Data);
    Info.Field_0x00 = Mission->ControllerId;
    Info.SolarArchID = Solar->ArchetypeId;
    Info.SystemID = System != nullptr ? System->SystemId : 0;
    Info.Position = Solar->Position;
    Info.HitPts = Solar->Hitpoints;
    std::memcpy(&Info.Orientation, &Solar->Orientation, sizeof(FMatrix));
    Info.LoadoutID = Solar->LoadoutId;
    Info.VoiceId = Solar->VoiceId;
    Info.DockingBaseId = Solar->BaseId;
    if (Solar->Field_0x468 != 0)
    {
        pub::GF::GetCostumeByID(static_cast<int>(Solar->Field_0x468), SolarCostume);
        Info.CostumeHead = SolarCostume.PartIds[0];
        Info.CostumeBody = SolarCostume.PartIds[1];
        Info.CostumeLeftHand = SolarCostume.PartIds[2];
        Info.CostumeRightHand = SolarCostume.PartIds[3];
        for (int Index = 0; Index < 8; ++Index)
            Info.CostumeAccessory[Index] = SolarCostume.AccessoryIds[Index];
        Info.CostumeAccessoryCount = static_cast<int>(SolarCostume.AccessoryCount);
    }
    Info.Field_0x80 = 0;

    // 06f92877-06f9294f: the reputation, named by the killable solar's string (+0x507d0).
    FmtStr Name;
    Name.begin_mad_lib(static_cast<unsigned int>(Solar->Arch->StringId + 0x507d0));
    Name.end_mad_lib();
    {
        FmtStr Empty;
        pub::Reputation::Alloc(Info.VibeGroupId, Name, Empty);
    }

    // 06f92955-06f92b85: the record: the solar's name, its loot, Flags = ModeFlag, the
    // attitude and the solar type.
    FRmSolarRecord Record;
    Record.Name = Solar->Nickname.Data;
    Record.State = 2;
    Record.Loot = Solar->Loot;
    Record.Flags = static_cast<unsigned int>(Solar->ModeFlag);
    Record.Attitude = Solar->Field_0x4c8;
    Record.SolarType = Solar->SolarType;
    Record.Reputation = Info.VibeGroupId;
    Record.Field_0x04 = true;
    pub::Reputation::SetAffiliation(Info.VibeGroupId, Solar->Faction);

    if (pub::SpaceObj::CreateSolar(Record.ObjectId, Info) == 0)
    {
        // 06f92bc8-06f92c2c: SetPersonalityParams with the "NOTHING" graph, +0x10 / +0x14 = 0,
        // the solar's pilot when it has one; SubmitState.
        pub::AI::SetPersonalityParams Op;
        Op.StateGraphId = pub::StateGraph::get_state_graph("NOTHING", pub::StateGraph::LEADER);
        Op.DirectiveCallbackObject = nullptr;
        Op.ContentCallbackObject = nullptr;
        if (Solar->bPilot != 0)
        {
            Op.bSetPersonality = 1;
            Op.Value = Solar->Personality;
        }
        pub::AI::SubmitState(Record.ObjectId, &Op);
        if ((Solar->ModeFlag & 1) != 0)
        {
            pub::SpaceObj::SetInvincible(Record.ObjectId, true, Solar->Field_0x4e0 != 0,
                                         Solar->InvincibleDuration);
        }
        // 06f92c62-06f92cc2: the attitude towards every mission player (FCOMP -2.0, TEST
        // AH,0x44 + JNP skips equal).
        if (!(Record.Attitude == -2.0f))
        {
            std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
            for (std::vector<FContentPlayerState*>::iterator It = Players->begin();
                 It != Players->end(); ++It)
            {
                int const PlayerRep = (*It)->GetReputation();
                pub::Reputation::SetAttitude(Info.VibeGroupId, PlayerRep, Record.Attitude);
            }
        }
        Solars->Records.Records.push_back(new FRmSolarRecord(Record));
        System->AddMissionObject(Record.ObjectId);
        bCreated = true;
    }
    return bCreated;
}

namespace
{
    // FUN_06efc7a0: the 3x3 identity (thiscall on the matrix).
    void RmSetIdentity(Matrix* Out)
    {
        std::memset(Out, 0, sizeof(Matrix));
        Out->E[0][0] = 1.0f;
        Out->E[1][1] = 1.0f;
        Out->E[2][2] = 1.0f;
    }

    // The spawn point search of FUN_06f915a0 (06f916a8-06f918da): GetEmptyPos around the
    // group's position; on success a second search from a point raised by Lift (FUN_06f0bb80,
    // float adds), with Clearance changed to SecondClearance. The second result is not tested.
    void RmFindSpawnPoint(FRmShipGroup const* Group, float StartRadius, float Clearance,
                          float SecondClearance, float Lift,
                          pub::SpaceObj::PosSelectionType Selection, Vector* OutPosition)
    {
        Transform Base;
        RmSetIdentity(&Base.Rotation);
        Base.Translation = Group->Position;
        unsigned int SystemId = Group->System->SystemId;
        if (pub::SpaceObj::GetEmptyPos(SystemId, Base, StartRadius, Clearance, Selection,
                                       *OutPosition) == 0)
            return;
        Clearance = SecondClearance;
        Vector Raised;
        Raised.x = Group->Position.x + 0.0f;
        Raised.y = Group->Position.y + Lift;
        Raised.z = Group->Position.z + 0.0f;
        RmSetIdentity(&Base.Rotation);
        Base.Translation = Raised;
        SystemId = Group->System->SystemId;
        pub::SpaceObj::GetEmptyPos(SystemId, Base, StartRadius, Clearance, Selection,
                                   *OutPosition);
    }

    // 06f9206b-06f92133: the stores into the copied JobStruct (+0x2a0 of the personality,
    // local [ESP+0x154]); AttackPreferences gets four {class, range, flags} rows and the
    // terminator class 12.
    void RmTuneShipJob(pub::AI::Personality::JobStruct* Job)
    {
        float const Range = 2000.0f + 2000.0f;          // FLD DAT_06fbc2cc, FADD ST0,ST0
        int const Kinds[4] = {8, 0xa, 0xb, 0xc};
        int const Flags = 0xf;
        pub::AI::Personality::JobStruct::FAttackPreference* const Prefs = Job->AttackPreferences;
        Job->FleeSceneThreatStyle = 4;                  // +0x3c4
        Job->SceneToughnessThreshold = 4;               // +0x2a4
        Job->Field_00 = 0;                              // +0x2a0
        Prefs[0].ShipClass = 0;                         // +0x2b0
        Prefs[0].Range = Range;                         // +0x2b4
        Prefs[1].Range = Range;                         // +0x2c0
        Prefs[2].Range = Range;                         // +0x2cc
        Prefs[3].Range = Range;                         // +0x2d8
        std::memset(&Job->FleeWhenHullDamagedPercent, 0, sizeof(float));   // +0x3c8 dword 0
        Job->bFleeWhenLeaderFleesStyle = 0;             // +0x3cc
        Job->bFleeNoWeaponsStyle = 0;                   // +0x3cd
        Prefs[0].WeaponFlags = 1;                       // +0x2b8
        Prefs[1].ShipClass = Kinds[0];                  // +0x2bc
        Prefs[1].WeaponFlags = Flags;                   // +0x2c4
        Prefs[2].ShipClass = Kinds[1];                  // +0x2c8
        Prefs[2].WeaponFlags = Flags;                   // +0x2d0
        Prefs[3].ShipClass = Kinds[2];                  // +0x2d4
        Prefs[3].WeaponFlags = Flags;                   // +0x2dc
        Prefs[4].ShipClass = Kinds[3];                  // +0x2e0
    }
}

// FUN_06f915a0 (06f915a0-06f9243a, RET 8). Locals in retail's construction order (EH states 0
// and 1): the block list (FUN_06ef6b80), the record vector; the created-ship list (state 0xb)
// last. The group is marked spawned first. Every ship of the group becomes a block (name,
// arch, faction look, the group's orientation, labels, cargo, priority, hit points, a
// StayInRange 2000 directive and, for the first ship, a GotoVec 500 directive) and a record;
// the blocks are laid out in the group's formation at an empty spot and created in the group's
// system. Ships launched from a dock (DockTarget != 0) get no record here and the result is
// false. Otherwise every created ship's record takes its id and ship, the ship's personality
// gets the job rows below, and the record is kept by the ship machine; the attitudes towards
// the mission players and between ships of different attitude are set.
bool FRmRuntimeMachine::SpawnShipGroup(FRmShipGroup* Group, bool bNotFirst)
{
    bool bSpawned = false;
    std::list<FContentNpcShipParams> Blocks;
    std::vector<FRmShipRecord> Records;
    Group->Field_0x01 = 1;
    if (!Group->Ships.empty())
        Records.reserve(Group->Ships.size());

    // 06f916a8-06f918da: the first group of a component searches the 3D volume (350 / 50, then
    // 250 higher); a later one the YZ plane (2625 / 1000, then 150 higher with clearance 250).
    // Retail does not initialise the point (local_9c4), and need not: GetEmptyPos (Server
    // .text:06cf9bd0) returns non-zero only through caseD_4 (-4: a negative radius or
    // clearance, or a selection type above 3), which these constant arguments never reach;
    // every other exit writes the point and returns 0 (06cfa03d loops until bEmpty).
    Vector SpawnPoint;
    if (!bNotFirst)
        RmFindSpawnPoint(Group, 350.0f, 50.0f, 50.0f, 250.0f,
                         pub::SpaceObj::POS_SEL_3D_VOLUME, &SpawnPoint);
    else
        RmFindSpawnPoint(Group, 2625.0f, 1000.0f, 250.0f, 150.0f,
                         pub::SpaceObj::POS_SEL_YZ_PLANE, &SpawnPoint);

    for (std::vector<FRmShipSpawn>::iterator Ship = Group->Ships.begin();
         Ship != Group->Ships.end(); ++Ship)
    {
        // 06f91900-06f919f1: a default block and a default ship record (Kind 0) appended.
        Blocks.push_back(FContentNpcShipParams());
        Records.push_back(FRmShipRecord());
        FContentNpcShipParams* Block = &Blocks.back();
        FRmShipRecord* Record = &Records.back();

        // 06f919f1-06f91ace: the name -- the offer's target name on the target ship, a random
        // name (gender from the faction, BuildNpcName with a named leader), or the [NPC]
        // block's; the block's old reputation handle released first.
        FNpcNameParams Name;
        if (Ship->TargetName != nullptr)
        {
            Name.First = Ship->TargetName->First;
            Name.Second = Ship->TargetName->Second;
            Name.Affiliation = Ship->TargetName->Affiliation;
            Name.Gender = Ship->TargetName->Gender;
        }
        if (Ship->TargetName != nullptr || Ship->bRandomName != 0)
        {
            if (Ship->TargetName == nullptr)
            {
                PickNpcNameGender(Ship->Npc.Affiliation, &Name);             // FUN_06f319b0
                FNpcNameRequest Request;                                    // FUN_06f31650
                BuildNpcName(Ship->Npc.Affiliation, &Request, &Name, true); // FUN_06f319d0
            }
            if (Block->ReputationHandle != nullptr)
            {
                Block->ReputationHandle->Release();
                Block->ReputationHandle = nullptr;
            }
            CreateNpcNameReputation(&Name, &Block->ReputationHandle);       // FUN_06f31910
        }
        else
        {
            if (Block->ReputationHandle != nullptr)
            {
                Block->ReputationHandle->Release();
                Block->ReputationHandle = nullptr;
            }
            CreateMsnNpcReputation(&Ship->Npc, &Block->ReputationHandle);   // FUN_06f31d40
        }

        // 06f91ace-06f91cc1: nickname, the npc_ship_arch (result not tested), the faction's
        // costume and voice for the name's gender, creation type 1, the group's orientation,
        // the mission's controller, labels, the group's +0x7c, cargo, priority, hit points.
        Block->Nickname = Ship->Nickname.Data;                              // FUN_06ed0160
        ApplyNpcShipArchById(Block, &Ship->Npc.NpcShipArch.Id, nullptr, nullptr);
        ApplyNpcShipFaction(Ship->Npc.Affiliation, Name.Gender, Block);
        Block->CreationType = 1;
        std::memcpy(&Block->Orientation, &Group->Orientation, sizeof(FMatrix));
        Block->ControllerId = Mission->ControllerId;
        Block->Field_0x118 = Ship->LabelIds;
        Block->Field_0x110 = Group->Field_0x7c;
        Block->Cargo.clear();
        for (std::vector<FMissionShipCargo>::iterator Cargo = Ship->Cargo.begin();
             Cargo != Ship->Cargo.end(); ++Cargo)
        {
            pub::SpaceObj::CargoDesc Desc;
            Desc.GoodId = Cargo->GoodId;
            Desc.Count = static_cast<unsigned int>(Cargo->Count);
            Block->Cargo.push_back(Desc);
        }
        Block->Priority = Ship->Priority;
        Block->HitPoints = Ship->HitPoints;

        // 06f91cc1-06f91e13: the record.
        Record->Name = Ship->Nickname.Data;
        Record->State = 2;
        Record->Loot = Ship->Loot;
        Record->Group = Group->Serial;
        Record->Flags = static_cast<unsigned int>(Ship->Field_0xd8);
        Record->Attitude = Group->Field_0x40;
        Record->Kind = Ship->Field_0xdc;
        Record->Field_0x04 = Ship->Npc.Affiliation == Offer->HostileGroup;

        // 06f91e16-06f91f15: StayInRange 2000 (DAT_06fbc2cc) of the group's position; the first
        // ship also GotoVec there (500, -1.0, no cruise flags, Op +8 = 1); each appended to the
        // block's directives with an AddRef, the holders released.
        TRefHandle<FObjListDirective> StayInRange;
        CreateObjListDirective(0xd, &StayInRange.Pointer);
        static_cast<FObjListStayInRange*>(StayInRange.Pointer)->SetSphere(&Group->Position, 2000.0f);
        {
            FObjListDirective* const Added = StayInRange.Pointer;
            Block->Directives.Items.push_back(Added);
            Added->AddRef();
        }
        if (Ship == Group->Ships.begin())
        {
            TRefHandle<FObjListDirective> GotoGroup;
            CreateObjListDirective(5, &GotoGroup.Pointer);
            FObjListGotoVec* const Goto = static_cast<FObjListGotoVec*>(GotoGroup.Pointer);
            Goto->SetDestination(&Group->Position, 500.0f, -1.0f, 0, false, false);
            Goto->Op.Priority = 1;
            FObjListDirective* const Added = GotoGroup.Pointer;
            Block->Directives.Items.push_back(Added);
            Added->AddRef();
        }
    }

    // 06f91f63-06f91fc8: a named formation lays the blocks out at the spawn point (identity
    // rotation); retail does not test FUN_06efd1a0's result here.
    if (!Group->Formation.IsEmpty())
    {
        FTransform Placement;
        std::memset(&Placement.Rotation, 0, sizeof(Placement.Rotation));
        Placement.Rotation.D[0][0] = 1.0f;
        Placement.Rotation.D[1][1] = 1.0f;
        Placement.Rotation.D[2][2] = 1.0f;
        Placement.Translation.X = SpawnPoint.x;
        Placement.Translation.Y = SpawnPoint.y;
        Placement.Translation.Z = SpawnPoint.z;
        PlaceFormationBlocks(&Blocks, FindFormation(&Group->Formation.Id), &Placement);
    }

    // 06f91fcb-06f92038: the dock target -- the solar of the group's name, else its +0x78;
    // TU58 FUN_06f73810 in the group's system.
    unsigned int DockTarget = static_cast<unsigned int>(Group->Field_0x78);
    if (Group->Name.Length != 0)
        DockTarget = Solars->Records.FindByName(Group->Name)->ObjectId;
    std::list<FContentNpcShip*> Created;
    Group->System->CreateNpcShips(&Blocks, DockTarget, &Created);
    if (DockTarget != 0)
        return bSpawned;

    // 06f92040-06f921dc: block i's ship goes with record i (retail walks the created list
    // without an end test). Its personality's job rows are set (SetPersonalityParams, +0x18 = 1,
    // the state graph of the ship) and a copy of the record (FUN_06f92440) is kept by the ship
    // machine.
    std::list<FContentNpcShip*>::iterator Spawned = Created.begin();
    for (std::vector<FRmShipRecord>::iterator Record = Records.begin(); Record != Records.end();
         ++Record, ++Spawned)
    {
        FContentNpcShip* const Npc = *Spawned;
        Record->ObjectId = Npc->SpaceObjId;
        Record->NpcShip = Npc;
        pub::AI::Personality Tuned = *Npc->GetPersonality();
        RmTuneShipJob(&Tuned.JobParams);
        pub::AI::SetPersonalityParams Op;
        Op.bSetPersonality = 1;
        Op.Value = Tuned;
        Op.StateGraphId = pub::AI::get_state_graph_id(Npc->SpaceObjId);    // 06f9216f: Op +0x0c
        pub::AI::SubmitState(Npc->SpaceObjId, &Op);
        Ships->Records.Records.push_back(new FRmShipRecord(*Record));
    }

    // 06f921e2-06f92313: a record with an attitude (not -2.0, DAT_06fbc2e8; TEST AH,0x44 +
    // JNP skips equal) sets it towards every mission player, and -0.9 (DAT_06fbc2b8) both ways
    // with every other alive ship record of a different attitude.
    std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
    for (std::vector<FRmShipRecord>::iterator Record = Records.begin(); Record != Records.end();
         ++Record)
    {
        if (Record->Attitude == -2.0f)
            continue;
        for (std::vector<FContentPlayerState*>::iterator Player = Players->begin();
             Player != Players->end(); ++Player)
        {
            int const PlayerRep = (*Player)->GetReputation();
            int const ShipRep = Record->NpcShip->GetReputation();
            pub::Reputation::SetAttitude(ShipRep, PlayerRep, Record->Attitude);
        }
        for (std::list<FRmSpawnRecord*>::iterator It = Ships->Records.Records.begin();
             It != Ships->Records.Records.end(); ++It)
        {
            FRmSpawnRecord* const Other = *It;
            if (Other->State != 2)
                continue;
            if (Other->ObjectId == Record->ObjectId)
                continue;
            if (Other->Attitude == Record->Attitude)
                continue;
            {
                int const OtherRep = Other->NpcShip->GetReputation();
                int const ShipRep = Record->NpcShip->GetReputation();
                pub::Reputation::SetAttitude(ShipRep, OtherRep, -0.9f);
            }
            {
                int const ShipRep = Record->NpcShip->GetReputation();
                int const OtherRep = Other->NpcShip->GetReputation();
                pub::Reputation::SetAttitude(OtherRep, ShipRep, -0.9f);
            }
        }
    }
    bSpawned = true;
    return bSpawned;
}

// FUN_06f90f60 (06f90f60-06f912ff, RET 4). Only a critical loot (Kind 2) gets a record and a
// message; every loot is created (a relative one only when its parent object exists).
bool FRmRuntimeMachine::SpawnLoot(FRmLootSpawn* InLoot)
{
    FSystemInfo* System = Offer->System;
    if (System == nullptr)
        return false;
    pub::SpaceObj::LootInfo* Info = &InLoot->Info;
    Info->SystemID = System->SystemId;
    if (InLoot->Kind == 2)
        Info->bMission = 1;
    if (InLoot->CriticalLoot != nullptr)
        Info->NameId = static_cast<unsigned int>(InLoot->CriticalLoot->Name + 0x50bb8);
    if (InLoot->bRelPos != 0)
    {
        // 06f90fda-06f910b3: next to the parent: its position + RelPosOffset, its velocities,
        // an identity orientation.
        unsigned int const Parent = Info->OwnerObjId;
        if ((Parent & 0x3fffffff) != 0)
        {
            Vector ParentPosition;
            Matrix ParentOrientation;
            pub::SpaceObj::GetLocation(Parent, ParentPosition, ParentOrientation);
            Vector Linear;
            Vector Angular;
            pub::SpaceObj::GetMotion(Parent, Linear, Angular);
            Info->Position.x = ParentPosition.x + InLoot->RelPosOffset.x;
            Info->Position.y = ParentPosition.y + InLoot->RelPosOffset.y;
            Info->Position.z = ParentPosition.z + InLoot->RelPosOffset.z;
            Info->Velocity = Linear;
            Info->AngularVelocity = Angular;
            std::memset(&Info->Orientation, 0, sizeof(Info->Orientation));
            Info->Orientation.E[0][0] = 1.0f;
            Info->Orientation.E[1][1] = 1.0f;
            Info->Orientation.E[2][2] = 1.0f;
            pub::SpaceObj::CreateLoot(InLoot->SpaceObjId, *Info);
        }
    }
    else
    {
        pub::SpaceObj::CreateLoot(InLoot->SpaceObjId, *Info);
    }

    // 06f910c6-06f9119e: the record.
    FRmLootRecord Record;
    Record.ObjectId = InLoot->SpaceObjId;
    Record.ParentId = Info->OwnerObjId;
    Record.Units = static_cast<int>(Info->Units);
    Record.Field_0x6c = InLoot->Field_0x100;
    Record.Kind = InLoot->Kind;
    Record.HolderId = 0;
    Record.CargoSlot = SUBOBJ_ID_NONE;
    Record.GoodId = 0;
    if (InLoot->Kind == 2)
    {
        Record.Flags = 2;
        if (InLoot->Field_0x100 != 1)
            pub::SpaceObj::SetInvincible2(Record.ObjectId, false, true, 1.0f);
        Loot->Records.Records.push_back(new FRmLootRecord(Record));
        FRmLootMessage Message(GRmMsgType06fd3b98, Record.ObjectId, 0);
        Loot->SendToOwner(&Message);
    }
    return true;
}

// FUN_06f90f20 (06f90f20-06f90f4f, RET 4).
void FRmRuntimeMachine::SpawnRecordLoot(FRmSpawnRecord* Record)
{
    for (std::vector<FRmLootSpawn>::iterator It = Record->Loot.begin(); It != Record->Loot.end();
         ++It)
    {
        It->Info.OwnerObjId = Record->ObjectId;
        SpawnLoot(&*It);
    }
}

// FUN_06f8ade0 (06f8ade0-06f8ae5f, RET 8).
void FRmRuntimeMachine::AddObjective(int Key, FRmObjective* Objective)
{
    Objective->Key = Key;
    FMissionObjective* Built = Objective->CreateMissionObjective(Mission, this);
    Built->BuildDescription();
    ActiveObjectives.push_back(Objective);
    Objectives.Objectives.push_back(Built);
    bObjectivesChanged = true;
    RefreshObjectives();
}

// The 06f8ae80-06f8aee3 walk: the Objectives node at the index of Where, one further when a
// type 5 objective is anywhere in the list (the scan stops at the first one).
std::list<FMissionObjective*>::iterator FRmRuntimeMachine::ObjectiveListPosition(
    std::vector<FRmObjective*>::iterator Where)
{
    int Offset = 0;
    for (std::list<FMissionObjective*>::iterator It = Objectives.Objectives.begin();
         It != Objectives.Objectives.end(); ++It)
    {
        if ((*It)->GetType() == 5)
        {
            Offset = 1;
            break;
        }
    }
    int Steps = static_cast<int>(Where - ActiveObjectives.begin()) + Offset;
    std::list<FMissionObjective*>::iterator Node = Objectives.Objectives.begin();
    while (Steps > 0)
    {
        --Steps;
        ++Node;
    }
    return Node;
}

// FUN_06f8ae70 (06f8ae70-06f8af30, RET 0xc). The list position is taken before the objective
// is built (the type 5 scan comes first in retail).
void FRmRuntimeMachine::InsertObjective(int Key, FRmObjective* Objective,
                                        std::vector<FRmObjective*>::iterator Where)
{
    Objective->Key = Key;
    std::list<FMissionObjective*>::iterator Node = ObjectiveListPosition(Where);
    FMissionObjective* Built = Objective->CreateMissionObjective(Mission, this);
    Built->BuildDescription();
    ActiveObjectives.insert(Where, Objective);
    Objectives.Objectives.insert(Node, Built);
    bObjectivesChanged = true;
    RefreshObjectives();
}

// FUN_06f8af40 (06f8af40-06f8b020, RET 4).
bool FRmRuntimeMachine::RebuildObjective(int Key)
{
    std::vector<FRmObjective*>::iterator It = FindObjective(Key);
    if (It == ActiveObjectives.end())
        return false;
    std::list<FMissionObjective*>::iterator Node = ObjectiveListPosition(It);
    FRmObjective* const Objective = *It;
    size_t const Index = static_cast<size_t>(It - ActiveObjectives.begin());
    ActiveObjectives.erase(It);
    std::list<FMissionObjective*>::iterator Next = Node;
    ++Next;
    Objectives.EraseRange(Node, Next);
    InsertObjective(Key, Objective, ActiveObjectives.begin() + Index);
    return true;
}

// FUN_06f8b030 (06f8b030-06f8b107, RET 4).
bool FRmRuntimeMachine::RemoveObjective(int Key)
{
    std::vector<FRmObjective*>::iterator It = FindObjective(Key);
    if (It == ActiveObjectives.end())
        return false;
    std::list<FMissionObjective*>::iterator Node = ObjectiveListPosition(It);
    ActiveObjectives.erase(It);
    std::list<FMissionObjective*>::iterator Next = Node;
    ++Next;
    Objectives.EraseRange(Node, Next);
    bObjectivesChanged = true;
    RefreshObjectives();
    return true;
}

// FUN_06f8b110 (RET 4).
std::vector<FRmObjective*>::iterator FRmRuntimeMachine::FindObjective(int Key)
{
    std::vector<FRmObjective*>::iterator It = ActiveObjectives.begin();
    for (; It != ActiveObjectives.end(); ++It)
    {
        if ((*It)->Key == Key)
            break;
    }
    return It;
}

// =======================================================================================
// FRmVignetteMachine
// =======================================================================================

FRmVignetteMachine::FRmVignetteMachine(FRmVignetteOffer* InOffer)
    : FRmRuntimeMachine(InOffer)
    , VignetteOffer(InOffer)
{
}

// FUN_06f8a6d0: +0xb0 released, then the base.
FRmVignetteMachine::~FRmVignetteMachine()
{
}

// FUN_06f8b140 (RET 8): this is the +0x54 sub-object; +0x5c from it is +0xb0.
bool FRmVignetteMachine::GetTextParam(int Param, FmtStr* Out)
{
    return VignetteOffer->GetTextParam(Param, Out);
}

// LAB_06f8a820 (06f8a820-06f8ab0c).
void FRmVignetteMachine::Shutdown()
{
    // 06f8a841-06f8a8b7: the population exclusion spheres (10000.0, DAT_06fbc2ec) of the
    // target ships and of the solars or the ships.
    if (VignetteOffer->TargetShips.bActive)
    {
        Vector const& Center = VignetteOffer->TargetShips.Position;
        SpacePopRemoveExclusionSphere(Center.x, Center.y, Center.z, 10000.0f);
    }
    {
        Vector const& Center = VignetteOffer->Solars.bActive ? VignetteOffer->Solars.Position
                                                              : VignetteOffer->Ships.Position;
        SpacePopRemoveExclusionSphere(Center.x, Center.y, Center.z, 10000.0f);
    }

    // 06f8a8ba-06f8a96b: every alive ship gets a zone behaviour (mode 2) and +0x1bc = 24.0;
    // one that is not a formation wingman also a type 4 guide.
    FRmRecordList ShipList = Ships->Query(0, 2, 0);
    pub::AI::SetZoneBehaviorParams ZoneParams;
    ZoneParams.InvertMode = 2;
    for (std::list<FRmSpawnRecord*>::iterator It = ShipList.Records.begin();
         It != ShipList.Records.end(); ++It)
    {
        FContentNpcShip* const Ship = (*It)->NpcShip;
        Ship->Field_0x1bc = 24.0f;
        pub::AI::SubmitState(Ship->SpaceObjId, &ZoneParams);
        if (Ship->FormationSlot == 0 || Ship->FormationId == 0xffffffffu)
        {
            FGuideDesc Desc(4, Ship);
            Ship->SetGuideFromDesc(&Desc, true);
        }
    }

    // 06f8a96d-06f8a9ae: the alive solars flagged in the offer's system record.
    FRmRecordList SolarList = Solars->Query(-1, 2, 0);
    for (std::list<FRmSpawnRecord*>::iterator It = SolarList.Records.begin();
         It != SolarList.Records.end(); ++It)
        VignetteOffer->System->SetObjectFlag((*It)->ObjectId, true);

    // 06f8a9b0-06f8aa07: the alive loot: a critical one destroyed, any other made vulnerable.
    FRmRecordList LootList = Loot->Query(0, 0, 0, 2, 0);
    for (std::list<FRmSpawnRecord*>::iterator It = LootList.Records.begin();
         It != LootList.Records.end(); ++It)
    {
        FRmLootRecord* const Record = static_cast<FRmLootRecord*>(*It);
        if (Record->Kind == 2)
            pub::SpaceObj::Destroy(Record->ObjectId, static_cast<DestroyType>(0));
        else
            pub::SpaceObj::SetInvincible(Record->ObjectId, false, true, 0.0f);
    }

    // 06f8aa0b-06f8aa1b: slot 28 with the mission's players.
    UnmarkObjects(GetMissionPlayers());
}

// LAB_06f8bb80 (06f8bb80-06f8bc6e). The reward text is slot 2 of the offer's RewardText with
// this machine as the parameter source. No empty test on the players.
void FRmVignetteMachine::OnSucceeded()
{
    FmtStr* Text = VignetteOffer->RewardText.Build(this);
    std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
    for (std::vector<FContentPlayerState*>::iterator It = Players->begin(); It != Players->end();
         ++It)
    {
        FContentPlayerState* const Player = *It;
        // 06f8bbc9-06f8bc16: FCOMPP of 1e8 (10000.0 * 10000.0) against the squared distance,
        // TEST AH,0x41 + JNZ: only strictly closer players.
        bool bNear = false;
        if (Player->ShipObject != nullptr)
        {
            Vector Where;
            Player->GetPosition(&Where);
            double const Limit = 10000.0 * 10000.0;
            bNear = RmDistanceSq(Where, Position) < Limit;
        }
        if (bNear)
        {
            Player->SetFlags(4);
            Player->SetCountdown(120.0f);
        }
        else
        {
            Player->SetFlags(0);
        }
    }
    unsigned int const FirstPlayer = (*Players->begin())->PlayerId;
    pub::Player::DisplayMissionMessage(FirstPlayer, *Text, static_cast<MissionMessageType>(3),
                                       true);
}

// FUN_06f8ab10 (06f8ab10-06f8add9, RET 4).
void FRmVignetteMachine::UnmarkObjects(std::vector<FContentPlayerState*> const* Players)
{
    FRmRecordList ShipList = Ships->Query(0, 2, 0);
    for (std::list<FRmSpawnRecord*>::iterator It = ShipList.Records.begin();
         It != ShipList.Records.end(); ++It)
    {
        FContentNpcShip* const Ship = (*It)->NpcShip;
        for (std::vector<FContentPlayerState*>::const_iterator Player = Players->begin();
             Player != Players->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, Ship->SpaceObjId, 0);
    }
    FRmRecordList SolarList = Solars->Query(-1, 2, 0);
    for (std::list<FRmSpawnRecord*>::iterator It = SolarList.Records.begin();
         It != SolarList.Records.end(); ++It)
    {
        for (std::vector<FContentPlayerState*>::const_iterator Player = Players->begin();
             Player != Players->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, (*It)->ObjectId, 0);
    }
    FRmRecordList LootList = Loot->Query(0, 0, 0, 2, 0);
    for (std::list<FRmSpawnRecord*>::iterator It = LootList.Records.begin();
         It != LootList.Records.end(); ++It)
    {
        for (std::vector<FContentPlayerState*>::const_iterator Player = Players->begin();
             Player != Players->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, (*It)->ObjectId, 0);
    }
    // 06f8ac6c-06f8ace1: the held critical loot (State 4) leaves its holder's cargo. The query
    // temporary (EH state 3) is assigned into the loot list above (FUN_06f93490 at 06f8ac96,
    // FuncInfo 06fc6698 has no fourth list), which is then walked.
    LootList.Records = Loot->Query(0, 0, 0, 4, 0).Records;
    for (std::list<FRmSpawnRecord*>::iterator It = LootList.Records.begin();
         It != LootList.Records.end(); ++It)
    {
        FRmLootRecord* const Record = static_cast<FRmLootRecord*>(*It);
        pub::Player::RemoveCargo(Record->HolderId, Record->CargoSlot, 1);
    }
}

// FUN_06f8c460 (the vignette machine's slot 2) and its helpers: RandomMissionVignette.cpp.

// FUN_06f890f0 (06f890f0-06f8919f, cdecl).
bool __cdecl CreateRmRuntimeMachine(FRmOffer* Offer, FRmRuntimeMachine** Out)
{
    FRmRuntimeMachine* Machine = nullptr;
    if (Offer->MachineType > 0 && Offer->MachineType <= 3)
        Machine = new FRmVignetteMachine(static_cast<FRmVignetteOffer*>(Offer));
    *Out = Machine;
    return Machine != nullptr;
}

// FUN_06f877c0 (06f877c0-06f87966, RET 0x18).
FRmRecordList FRmLootMachine::Query(int Field6c, int Kind, unsigned int Field60, int InState,
                                    unsigned int InHolderId)
{
    FRmRecordList Result;
    for (std::list<FRmSpawnRecord*>::iterator It = Records.Records.begin();
         It != Records.Records.end(); ++It)
    {
        FRmLootRecord* const Record = static_cast<FRmLootRecord*>(*It);
        if (Field60 != 0 && Record->ParentId != Field60)
            continue;
        if (InHolderId != 0 && Record->HolderId != InHolderId)
            continue;
        if (Field6c != 0 && Record->Field_0x6c != Field6c)
            continue;
        if (Kind != 0 && Record->Kind != Kind)
            continue;
        if (InState != 0 && Record->State != InState)
            continue;
        Result.Records.push_back(Record);
    }
    return Result;
}
