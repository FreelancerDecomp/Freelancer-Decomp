// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TU61 -- the vignette runtime machine's state machine (FRmVignetteMachine::Slot2, FUN_06f8c460,
// 5106 insns) and its helpers FUN_06f8bc70..FUN_06f8c3c0 / FUN_06f90b40 / FUN_06f90b70, plus the
// two TU61 trigger classes it builds (vtables 06fbca40 / 06fbca84). Content.md section 7, lane
// CNT-RM. Declarations in RandomMissionMachine.h.
//
// FUN_06f8c460 is one function in retail; the port splits it by state (Handle*), each piece
// keeps its listing range. The objective keys the machine uses (the generator's
// FRmObjectiveMap keys): 1 the target ship marker, 2 the main battle marker, 3 the target
// component's "going to main" marker (shown as key 2), 4 / 5 the big / other solars, 6 / 7 the
// defensive / big ships, 8 the friendly group, 9 the "return to the combat area" countdown,
// 0xa / 0xb the critical loot, 0xc the dock of the offer base.

#include "RandomMissionMachine.h"

#include "BaseDB.h"                 // FContentBase, FindContentBaseInfo
#include "BehaviorCmds.h"           // FContentShipBase directives
#include "ContentInternal.h"        // GetContentGameTime, GetContentTimeDistance, SpacePop*
#include "ContentPlayerState.h"     // FContentPlayerState, FindContentPlayerState*
#include "DockQueues.h"             // RemoveFromFormation
#include "Gcs.h"                    // GGcsConversationManager
#include "MissionObjList.h"         // CreateObjListDirective, FObjListStayInRange, FObjListGotoVec
#include "NpcShip.h"                // FContentNpcShip
#include "RandomMissionObject.h"    // FRandomMissionObject
#include "RefHandle.h"              // TRefHandle
#include "SystemInfo.h"             // FSystemInfo::SystemId
#include "common_misc.h"            // SMM_TIMEOUT
#include "common_server.h"
#include "server_cobject.h"         // pub::SpaceObj::GetHealth
#include "server_gameobject.h"      // FSubObjDamageEvent (controller message 0xd)

#include <cstring>

// LAB_06f84280 (TU61 static init): FLD 3500.0 (DAT_06fbc2c8), FADD 6000.0 (DAT_06fbc2b4), FSTP.
float GRmVignetteOuterRadius = 3500.0f + 6000.0f;

namespace
{
    // DAT_06fd3c0c (.bss, 0): the cookie counter of FUN_06f8c460's function-local statics
    // (guard bytes DAT_06fd3b68 / DAT_06fd3b44); every cookie is the counter's value the first
    // time its code is reached, post-incremented.
    unsigned int GRmVignetteNextCookie = 0;

    unsigned int RmNextVignetteCookie()
    {
        return GRmVignetteNextCookie++;
    }

    // 06f8c481-06f8c4d6: the three statics initialised on every entry of FUN_06f8c460, in this
    // order (guard bits 1, 2, 4 of DAT_06fd3b68).
    struct FRmVignetteEntryCookies
    {
        unsigned int LeaveArea;     // DAT_06fd3b64: FRmProximityLeaveTrigger of the combat area
        unsigned int ReenterArea;   // DAT_06fd3b60: FRmProximityTrigger back into the area
        unsigned int Countdown;     // DAT_06fd3b5c: FRmTimerTrigger of the countdown
    };

    FRmVignetteEntryCookies const& RmVignetteEntryCookies()
    {
        static FRmVignetteEntryCookies const Cookies = {RmNextVignetteCookie(),
                                                        RmNextVignetteCookie(),
                                                        RmNextVignetteCookie()};
        return Cookies;
    }

    // The message test FUN_06f8c460 repeats for every message: event type 3, the delivery state
    // -1 or the current one (direct field reads or the get_delivery_state import), then the
    // message type.
    StateMachineMessage const* RmMessageOf(StateMachine const* Machine, SMEventData const* Event,
                                           unsigned int Type)
    {
        if (Event->get_event_type() != 3)
            return nullptr;
        StateMachineMessage const* Message = static_cast<SMMessage const*>(Event)->Message;
        if (Message->DeliveryState != -1 && Machine->get_state() != Message->DeliveryState)
            return nullptr;
        if (Message->MessageType != Type)
            return nullptr;
        return Message;
    }

    // A trigger-fired message (DAT_06fd3a18) whose +0x1c is Cookie.
    FRmTriggerFiredMessage const* RmFiredTriggerOf(StateMachine const* Machine,
                                                   SMEventData const* Event, unsigned int Cookie)
    {
        StateMachineMessage const* Message = RmMessageOf(Machine, Event, GRmMsgTriggerFired);
        if (Message == nullptr)
            return nullptr;
        FRmTriggerFiredMessage const* Fired = static_cast<FRmTriggerFiredMessage const*>(Message);
        if (Fired->Cookie != Cookie)
            return nullptr;
        return Fired;
    }

    // The controller message ids (common_server_controller.h), used throughout this file.
    using pub::Controller::EMessage;

    // A controller event (type 4) of message MessageId. The event's int +0x08 (named
    // ControllerId in common_net.h) is the controller message id, cast back after the type test.
    SMControllerEvent const* RmControllerOf(SMEventData const* Event, EMessage MessageId)
    {
        if (Event->get_event_type() != 4)
            return nullptr;
        SMControllerEvent const* Controller = static_cast<SMControllerEvent const*>(Event);
        if (static_cast<EMessage>(Controller->ControllerId) != MessageId)
            return nullptr;
        return Controller;
    }

    // The enter event test (event type < 4 and == 1) inlined in every state.
    bool RmIsEnterEvent(SMEventData const* Event)
    {
        return Event->get_event_type() < 4 && Event->get_event_type() == 1;
    }

    // FUN_06f90b40 (and its inline copies): a group to spawn (+0x00 set) that is not spawned
    // yet (+0x01 clear).
    bool RmHasUnspawnedGroup(std::vector<FRmShipGroup> const* Groups)
    {
        for (std::vector<FRmShipGroup>::const_iterator It = Groups->begin(); It != Groups->end();
             ++It)
        {
            if (It->Field_0x00 != 0 && It->Field_0x01 == 0)
                return true;
        }
        return false;
    }

    // The inline choice of the main battle centre: the solars' when that component is active,
    // else the ships'.
    Vector const* RmMainBattleCenter(FRmVignetteOffer const* Offer)
    {
        if (Offer->Solars.bActive)
            return &Offer->Solars.Position;
        return &Offer->Ships.Position;
    }

    // FUN_06f931c0 / FUN_06f930a0: the objective of Key in a component's map (insert of
    // {Key, null} when missing, as operator[]).
    FRmParamText* RmComponentObjective(FRmOfferComponent* Component, int Key)
    {
        return Component->Objectives.Objectives[Key];
    }
}

// =======================================================================================
// TU61 triggers
// =======================================================================================

FRmShipCountTrigger::FRmShipCountTrigger(unsigned int InCookie, unsigned int InOwnerId,
                                         FRmRecordList* InRecords, int InThreshold)
    : FRmTrigger(InCookie, InOwnerId, true, 1.0f)
    , Records(InRecords)
    , Threshold(InThreshold)
{
}

// LAB_06f92dd0 (06f92dd0-06f92e46, RET 8). JG: a count above Threshold does not fire.
bool FRmShipCountTrigger::OnArmed(SMEventData const* Event, int const& State)
{
    if (State != 1)
        return false;
    if (RmIsEnterEvent(Event))
    {
        if (CountAlive() <= Threshold)
            set_state(2);
        return true;
    }
    if (RmControllerOf(Event, EMessage::ObjectDestroyed) != nullptr)
    {
        if (CountAlive() <= Threshold)
            set_state(2);
        return true;
    }
    return false;
}

// LAB_06f92e50 (06f92e50-06f92e79).
int FRmShipCountTrigger::CountAlive()
{
    int Count = 0;
    for (std::list<FRmSpawnRecord*>::iterator It = Records->Records.begin();
         It != Records->Records.end(); ++It)
    {
        if ((*It)->Field_0x04 && (*It)->State == 2)
            ++Count;
    }
    return Count;
}

FRmShipHealthTrigger::FRmShipHealthTrigger(unsigned int InCookie, unsigned int InOwnerId,
                                           FRmRecordList* InRecords, int InThreshold, int InKind)
    : FRmTrigger(InCookie, InOwnerId, true, 1.0f)
    , Records(InRecords)
    , Threshold(InThreshold)
    , Kind(InKind)
{
}

// FUN_06f92ed0 (06f92ed0-06f92fe5, RET 8).
bool FRmShipHealthTrigger::OnArmed(SMEventData const* Event, int const& State)
{
    if (State != 1)
        return false;
    if (RmIsEnterEvent(Event))
    {
        int const Lowest = GetLowestHealth();
        if (Lowest != -1 && Lowest <= Threshold)
            set_state(2);
        return true;
    }
    SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::ObjectDamaged);
    if (Controller == nullptr)
        return false;

    // 06f92f38-06f92f71: the record of the damaged object (parms +0x00, read once before the
    // walk), of this trigger's kind.
    FSubObjDamageEvent const* Damage = static_cast<FSubObjDamageEvent const*>(Controller->DataPtr);
    FRmSpawnRecord* Record = nullptr;
    for (std::list<FRmSpawnRecord*>::iterator It = Records->Records.begin();
         It != Records->Records.end(); ++It)
    {
        if ((*It)->ObjectId == Damage->ObjId)
        {
            Record = *It;
            break;
        }
    }
    if (Record == nullptr)
        return true;
    if (static_cast<FRmShipRecord*>(Record)->Kind != Kind)
        return true;

    // 06f92f73-06f92fd0: ftol(current / max * -100.0) (DAT_06fbcab8 = 0xc2c80000) and
    // ftol(RelativeDamage * -100.0) (parms +0x1c = hit points lost / max hit points, Server
    // .text:06ceb5d8-06ceb5f3); their difference below Threshold fires with the record as the
    // activator. Retail keeps both products on the x87 stack (FLD / FDIV / FMUL, no store) and
    // truncates them with _ftol; the doubles below are x87 registers as well (/arch:IA32), so
    // the thread's precision control rounds them the same way, and a store of such a value to
    // a double is exact.
    unsigned int const ObjectId = Record->ObjectId;
    float Current = 0.0f;
    float Max = 0.0f;
    pub::SpaceObj::GetHealth(ObjectId, Current, Max);
    int const Health = static_cast<int>(static_cast<double>(Current) /
                                        static_cast<double>(Max) * -100.0);
    int const DamagePercent =
        static_cast<int>(static_cast<double>(Damage->RelativeDamage) * -100.0);
    if (DamagePercent - Health < Threshold)
    {
        SetActivator(Record->ObjectId);
        set_state(2);
    }
    return true;
}

// LAB_06f92ff0 (06f92ff0-06f93074): ftol(current / max * 100.0) (DAT_06fb8574 = 0x42c80000),
// kept on the x87 stack as in retail (see OnArmed above). The end of the list is re-read every
// iteration (06f93061), as std::list::end() here.
int FRmShipHealthTrigger::GetLowestHealth()
{
    int Lowest = -1;
    for (std::list<FRmSpawnRecord*>::iterator It = Records->Records.begin();
         It != Records->Records.end(); ++It)
    {
        FRmSpawnRecord* const Record = *It;
        if (!Record->Field_0x04 || Record->State != 2)
            continue;
        if (static_cast<FRmShipRecord*>(Record)->Kind != Kind)
            continue;
        unsigned int const ObjectId = Record->ObjectId;
        float Current = 0.0f;
        float Max = 0.0f;
        pub::SpaceObj::GetHealth(ObjectId, Current, Max);
        int const Health = static_cast<int>(static_cast<double>(Current) /
                                            static_cast<double>(Max) * 100.0);
        if (Lowest == -1 || Health < Lowest)
            Lowest = Health;
    }
    return Lowest;
}

// =======================================================================================
// FRmVignetteMachine helpers
// =======================================================================================

// FUN_06f8bc70 (06f8bc70-06f8bd8a). The alive flagged ships are looked up again by id
// (Ships->Records slot 2) before their flags are cleared.
void FRmVignetteMachine::ClearShipTargets()
{
    RemoveObjective(6);
    RemoveObjective(7);
    FRmRecordList Flagged = Ships->Query(0, 2, 2);
    for (std::list<FRmSpawnRecord*>::iterator It = Flagged.Records.begin();
         It != Flagged.Records.end(); ++It)
    {
        FRmSpawnRecord* const Record = Ships->Records.FindById((*It)->ObjectId);
        Record->Flags = 0;
        std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
        for (std::vector<FContentPlayerState*>::iterator Player = Players->begin();
             Player != Players->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, Record->ObjectId, 0);
    }
}

// FUN_06f8bd90 (06f8bd90-06f8beaa).
void FRmVignetteMachine::ClearSolarTargets()
{
    RemoveObjective(4);
    RemoveObjective(5);
    FRmRecordList Flagged = Solars->Query(-1, 2, 2);
    for (std::list<FRmSpawnRecord*>::iterator It = Flagged.Records.begin();
         It != Flagged.Records.end(); ++It)
    {
        FRmSpawnRecord* const Record = Solars->Records.FindById((*It)->ObjectId);
        Record->Flags = 0;
        std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
        for (std::vector<FContentPlayerState*>::iterator Player = Players->begin();
             Player != Players->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, Record->ObjectId, 0);
    }
}

// FUN_06f8beb0 (06f8beb0-06f8c1f7, RET 0xc). Retail has no end test after the group search
// (06f8bee0-06f8bef0); every caller passes the serial of one of Component's groups.
void FRmVignetteMachine::SpawnGroupWithTriggers(unsigned int Serial, FRmShipComponent* Component,
                                                unsigned int Cookie)
{
    std::vector<FRmShipGroup>::iterator Group = Component->Groups.begin();
    while (Group != Component->Groups.end() && Group->Serial != Serial)
        ++Group;
    if (Group->Field_0x01 != 0)
        return;
    if (Group->Field_0x02 != 0)
        ClearShipTargets();

    // 06f8bf0b-06f8bf44: slot 24, the group's records, their objectives.
    SpawnShipGroup(&*Group, Group != Component->Groups.begin());
    FRmRecordList Members = Ships->QueryGroup(Group->Serial);
    MarkGroupTargets(&Members, &Component->Objectives);

    // 06f8bf49-06f8c025: the first-spawn comm sequences. Field_0x40 > 0.0 (FCOMP 0.0, TEST
    // AH,0x41 + JNZ: less, equal or unordered go to the hostile side) is a friendly group.
    if (Group->Field_0x40 > 0.0f)
    {
        if (!bFriendlySpawnSent)
        {
            VignetteOffer->General.SendConversations(RmCommFirstFriendlySpawn, this, nullptr);
            bFriendlySpawnSent = 1;
        }
    }
    else if (!bBigShipSpawnSent || !bDefensiveShipSpawnSent)
    {
        bool bDefensive = false;
        bool bBig = false;
        for (std::list<FRmSpawnRecord*>::iterator It = Members.Records.begin();
             It != Members.Records.end(); ++It)
        {
            int const Kind = static_cast<FRmShipRecord*>(*It)->Kind;
            if (Kind == 1)
                bDefensive = true;
            else if (Kind == 2)
                bBig = true;
        }
        if (!bBigShipSpawnSent && bBig)
        {
            VignetteOffer->General.SendConversations(RmCommFirstBigShipSpawn, this, nullptr);
            bBigShipSpawnSent = 1;
        }
        else if (!bDefensiveShipSpawnSent && bDefensive)
        {
            VignetteOffer->General.SendConversations(RmCommFirstDefensiveShipSpawn, this,
                                                     nullptr);
            bDefensiveShipSpawnSent = 1;
        }
    }

    // 06f8c02c-06f8c193: the next group not spawned yet gets a trigger for "all ships dead"
    // and, by its +0x04 mode, a delayed cookie (2) or a "Field_0x08 ships left" trigger (3).
    std::vector<FRmShipGroup>::iterator Next = Group + 1;
    while (Next != Component->Groups.end() && Next->Field_0x01 != 0)
        ++Next;
    if (Next == Component->Groups.end())
        return;

    FRmShipCountTrigger* AllDead =
        new FRmShipCountTrigger(Cookie, get_id(), &Ships->Records, 0);
    AllDead->SetActivator(Next->Serial);
    AddStateMachine(AllDead);
    if (Next->Field_0x04 == 2)
    {
        FRmCookieMessage Delayed(Next->Serial);
        send_delayed_message_to_me(&Delayed, static_cast<float>(Next->Field_0x08), SMMS_Self, 0);
    }
    else if (Next->Field_0x04 == 3)
    {
        FRmShipCountTrigger* FewLeft =
            new FRmShipCountTrigger(Cookie, get_id(), &Ships->Records, Next->Field_0x08);
        FewLeft->SetActivator(Next->Serial);
        AddStateMachine(FewLeft);
    }
}

// FUN_06f8c200 (06f8c200-06f8c326, RET 4). No null test on the record.
void FRmVignetteMachine::OnFlaggedShipsLost(unsigned int ObjectId)
{
    FRmShipRecord* Record = static_cast<FRmShipRecord*>(Ships->Records.FindById(ObjectId));
    int const Kind = Record->Kind;
    if (Kind == 2)
    {
        FRmRecordList Left = Ships->Query(2, 2, 2);
        if (Left.Records.size() == 0)
            RemoveObjective(7);
    }
    else if (Kind == 1)
    {
        FRmRecordList Left = Ships->Query(1, 2, 2);
        if (Left.Records.size() == 0)
            RemoveObjective(6);
    }
}

// FUN_06f8c330 (06f8c330-06f8c3bd): the inline finds of 7, 4, 6 and 5.
bool FRmVignetteMachine::HasTargetObjectives()
{
    if (FindObjective(7) != ActiveObjectives.end())
        return true;
    if (FindObjective(4) != ActiveObjectives.end())
        return true;
    if (FindObjective(6) != ActiveObjectives.end())
        return true;
    if (FindObjective(5) != ActiveObjectives.end())
        return true;
    return false;
}

// FUN_06f8c3c0 (06f8c3c0-06f8c455, RET 8): the player map's lower_bound (FUN_06f2e220) and key
// test; a mapped player with a ship gets IN_SPACE_IN_SYSTEM when SystemId is this machine's
// (slot 8), else IN_SPACE_NOT_IN_SYSTEM.
void FRmVignetteMachine::SendSystemConversations(unsigned int InPlayerId, unsigned int SystemId)
{
    std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
    std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerMap->find(InPlayerId);
    if (It == PlayerMap->end())
        return;
    FContentPlayerState* const Player = It->second;
    if (Player->ShipObject == nullptr)
        return;
    if (SystemId == GetSystemId())
        VignetteOffer->General.SendConversations(RmCommInSpaceInSystem, this, Player);
    else
        VignetteOffer->General.SendConversations(RmCommInSpaceNotInSystem, this, Player);
}

// FUN_06f90b70 (06f90b70-06f90f1d, RET 8). Retail reads Members' first record without an empty
// test (06f90db1-06f90dbd); the spawn always gives the group a record.
void FRmVignetteMachine::MarkGroupTargets(FRmRecordList* Members, FRmObjectiveMap* Map)
{
    bool bMarked = false;

    // 06f90b91-06f90bb5: the first member flagged as a target (Flags bit 1).
    FRmShipRecord* Flagged = nullptr;
    for (std::list<FRmSpawnRecord*>::iterator It = Members->Records.begin();
         It != Members->Records.end(); ++It)
    {
        if (((*It)->Flags & 2) != 0)
        {
            Flagged = static_cast<FRmShipRecord*>(*It);
            break;
        }
    }
    if (Flagged != nullptr)
    {
        // 06f90bb5-06f90bcc: SETNZ (Kind != 1) + 6 -- defensive ships 6, any other kind 7.
        int const Key = (Flagged->Kind != 1 ? 1 : 0) + 6;
        std::vector<FRmObjective*>::iterator Active = FindObjective(Key);
        unsigned int const SystemId = Offer->System->SystemId;
        if (Active == ActiveObjectives.end())
        {
            // 06f90bfc-06f90d00: the component's objective marks the members, then the inline
            // AddObjective (FUN_06ecfc30 push_back on +0x68).
            static_cast<FRmObjectiveType3*>(Map->Objectives[Key])
                ->SetTargetRecords(Members, GetMissionPlayers(), 2, SystemId, Position);
            AddObjective(Key, static_cast<FRmObjective*>(Map->Objectives[Key]));
        }
        else
        {
            // 06f90d0f-06f90da1: the active objective marks every alive flagged ship of the
            // kind, and is rebuilt.
            FRmRecordList SameKind = Ships->Query(Flagged->Kind, 2, 2);
            static_cast<FRmObjectiveType3*>(*Active)
                ->SetTargetRecords(&SameKind, GetMissionPlayers(), 2, SystemId, Position);
            RebuildObjective((*Active)->Key);
        }
        RemoveObjective(8);
        bMarked = true;
    }

    // 06f90db1-06f90f08: a hostile group (first member's attitude < 0.0: FCOMP 0.0, TEST
    // AH,0x5 + JP skips greater, equal and unordered) gets the component's objective 8 when it
    // has one, nothing was marked above and none of 8, 6, 7 is active.
    FRmSpawnRecord* const First = Members->Records.front();
    if (!(First->Attitude < 0.0f))
        return;
    if (Map->Objectives.find(8) == Map->Objectives.end())
        return;
    if (bMarked)
        return;
    if (FindObjective(8) != ActiveObjectives.end())
        return;
    if (FindObjective(6) != ActiveObjectives.end())
        return;
    if (FindObjective(7) != ActiveObjectives.end())
        return;
    AddObjective(8, static_cast<FRmObjective*>(Map->Objectives[8]));
}

// =======================================================================================
// FRmVignetteMachine::Slot2 (FUN_06f8c460)
// =======================================================================================

// FUN_06f8c460 (06f8c460-06f90a32, RET 8).
bool FRmVignetteMachine::Slot2(SMEventData const* Event, int const& State)
{
    RmVignetteEntryCookies();
    if (State < 0)
        return HandleAnyState(Event);

    // 06f8c3bd-06f8d3f7: states 0..2 are the base's slots 13..15, their result dropped.
    if (State == 0)
    {
        OnState0(Event, State);
        return false;
    }
    if (State == 1)
    {
        OnState1(Event, State);
        return false;
    }
    if (State == 2)
    {
        OnState2(Event, State);
        return false;
    }
    if (State == 3)
        return HandleStart(Event);
    if (State == 8)
        return HandlePreBattle(Event);
    if (State == 9)
        return HandleTargetBattle(Event);
    if (State == 10)
        return HandleApproachMain(Event);
    if (State == 11)
        return HandleMainBattle(Event);
    if (State == 12)
        return HandleCriticalLoot(Event);
    if (State == 5)
        return HandleSucceeded(Event);
    if (State == 6)
        return HandleFailed(Event);
    if (State == 7)
        return HandleDone(Event);
    return false;
}

// 06f8c4e8-06f8d3ba: the messages of every state.
bool FRmVignetteMachine::HandleAnyState(SMEventData const* Event)
{
    FRmVignetteEntryCookies const& Cookies = RmVignetteEntryCookies();

    // 06f8c4e8-06f8c518: controller message 0x20 -> slot 16.
    if (RmControllerOf(Event, EMessage::Heartbeat) != nullptr)
    {
        RefreshObjectives();
        return true;
    }

    // 06f8c51b-06f8c584: a comm sequence conversation (DAT_06fd3b74) goes to the GCS manager
    // (DAT_06fcf1b0) when valid (slot 2).
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgType06fd3b74))
    {
        FRmConversationBase* const Conversation =
            static_cast<FRmConversationMessage const*>(Message)->Conversation;
        if (Conversation->IsValid())
            GGcsConversationManager->Add(Conversation);
        return true;
    }

    // 06f8c587-06f8c5f8: controller messages 0x36 {system, player} and 0x3b {system, ..,
    // player}.
    if (SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::SpaceEnter))
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        SendSystemConversations(Parms[1], Parms[0]);
        return true;
    }
    if (SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::SystemEnter))
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        SendSystemConversations(Parms[2], Parms[0]);
        return true;
    }

    // 06f8c5fb-06f8c661: controller message 0xa {player} of a mapped player -> slot 20.
    if (SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::GroupMemberJoined))
    {
        unsigned int const* PlayerId = static_cast<unsigned int const*>(Controller->DataPtr);
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        if (PlayerMap->find(*PlayerId) == PlayerMap->end())
            return true;
        OnPlayerJoined(*PlayerId);
        return true;
    }

    // 06f8c664-06f8c733: a player left (DAT_06fd3b70): slot 28 with a one-player vector.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgPlayerLeft))
    {
        unsigned int const PlayerId = static_cast<FRmPlayerLeftMessage const*>(Message)->PlayerId;
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerMap->find(PlayerId);
        if (It == PlayerMap->end())
            return true;
        std::vector<FContentPlayerState*> Leaving;
        Leaving.insert(Leaving.begin(), It->second);
        UnmarkObjects(&Leaving);
        return true;
    }

    // 06f8c736-06f8c799: controller message 5 {player} of a mapped player -> slot 21.
    if (SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::PlayerLoad))
    {
        unsigned int const* PlayerId = static_cast<unsigned int const*>(Controller->DataPtr);
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerMap->find(*PlayerId);
        if (It == PlayerMap->end())
            return true;
        SetPlayerAttitudes(It->second);
        return true;
    }

    // 06f8c79c-06f8c84b: controller message 0x27 {ship, base} (docked): in single player,
    // once the area was reached, a mapped player's dock fails the mission (13).
    if (SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::Msg0x27))
    {
        if (!pub::SinglePlayer())
            return true;
        if (!bAreaReached)
            return true;
        FContentPlayerState* Player =
            FindContentPlayerStateByShip(static_cast<unsigned int const*>(Controller->DataPtr));
        if (Player == nullptr)
            return true;
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        if (PlayerMap->find(Player->PlayerId) == PlayerMap->end())
            return true;
        FailReason = 13;
        set_state(6);
        return true;
    }

    // 06f8c84e-06f8cb2d: the critical loot was spawned (DAT_06fd3b98): the solar / ship target
    // objectives go away, state 12, and the loot's objective (0xa or 0xb by its +0x6c) marks
    // it -- the active one rebuilt, else the offer's added.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgType06fd3b98))
    {
        unsigned int const LootId = static_cast<FRmLootMessage const*>(Message)->ObjectId;
        FRmLootRecord* Record = static_cast<FRmLootRecord*>(Loot->Records.FindById(LootId));
        if (Record->Kind != 2)
            return true;
        ClearShipTargets();
        ClearSolarTargets();
        set_state(12);
        int Key = 0;
        if (Record->Field_0x6c == 1)
            Key = 0xa;
        else if (Record->Field_0x6c == 2 || Record->Field_0x6c == 3)
            Key = 0xb;
        else
            return true;

        Vector Where;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(Record->ObjectId, Where, Orientation);
        unsigned int SystemId = 0;
        pub::SpaceObj::GetSystem(Record->ObjectId, SystemId);
        std::vector<FRmObjective*>::iterator Active = FindObjective(Key);
        if (Active != ActiveObjectives.end())
        {
            FRmRecordList Critical = Loot->Query(Record->Field_0x6c, 2, 0, 2, 0);
            static_cast<FRmObjectiveType3*>(*Active)
                ->SetTargetRecords(&Critical, Mission->GetPlayerStates(), 2, SystemId, Where);
            RebuildObjective((*Active)->Key);
        }
        else
        {
            FRmRecordList Critical;
            Critical.Records.push_back(Record);
            static_cast<FRmObjectiveType3*>(RmComponentObjective(&VignetteOffer->General, Key))
                ->SetTargetRecords(&Critical, Mission->GetPlayerStates(), 2, SystemId, Where);
            AddObjective(Key, static_cast<FRmObjective*>(
                                  RmComponentObjective(&VignetteOffer->General, Key)));
        }
        return true;
    }

    // 06f8cb30-06f8cc5c: controller message 0x39 {.., player +0xc, position +0x14} (a player's
    // ship destroyed) of a mapped player inside 1e8 squared of the area (DAT_06fbc2f4; TEST
    // AH,0x5 + JP skips greater, equal and unordered): the mission's slot 32 (player count
    // decremented) at 0 fails it -- GLOBAL_FAIL_OTHER, reason 2 (single player) or 3.
    if (SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::ObjectDestroyed))
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerMap->find(Parms[3]);
        if (It == PlayerMap->end())
            return true;
        Vector const* Killed = reinterpret_cast<Vector const*>(&Parms[5]);
        Vector const Delta = SubtractVectors(*Killed, Position);
        // 06f8cb97-06f8cbb4: (x*x + z*z) + y*y on the x87 stack (FADDP twice), in that order.
        double const DistanceSq = (static_cast<double>(Delta.x) * Delta.x +
                                   static_cast<double>(Delta.z) * Delta.z) +
                                  static_cast<double>(Delta.y) * Delta.y;
        if (!(DistanceSq < 100000000.0))
            return true;
        if (Mission->DecrementPlayerCount() > 0)
            return true;
        VignetteOffer->General.SendConversations(RmCommGlobalFailOther, this, It->second);
        FailReason = pub::SinglePlayer() ? 2 : 3;
        set_state(6);
        return true;
    }

    // 06f8cc5f-06f8cf06: the players left the combat area: LEFT_COMBAT_AREA, the countdown
    // objective 9, a trigger back into the area (AreaRadius - 250), the countdown timer and the
    // 5 s tick DAT_06fd3b7c.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, Cookies.LeaveArea))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        LeftAreaTime = static_cast<float>(GetContentGameTime());
        VignetteOffer->General.SendConversations(
            RmCommLeftCombatArea, this, FindContentPlayerStateByShip(&Fired->Activator));
        FRmObjectiveType4* Countdown = new FRmObjectiveType4();
        Countdown->ObjectiveType = 0xda;
        Countdown->StrId = RmComponentObjective(&VignetteOffer->General, 9)->StrId;
        unsigned int const SystemId = VignetteOffer->System->SystemId;
        Countdown->SetCountdown(static_cast<int>(GRmMultiplayerDelay), SystemId, 0x44d,
                                Position);
        InsertObjective(9, Countdown, ActiveObjectives.begin());
        AddMachine(new FRmProximityTrigger(Cookies.ReenterArea, get_id(),
                                           VignetteOffer->System->SystemId, &Position,
                                           AreaRadius - 250.0f, GetMissionPlayers()));
        AddMachine(new FRmTimerTrigger(Cookies.Countdown, get_id(), GRmMultiplayerDelay));
        StateMachineMessage Tick(GRmMsgType06fd3b7c);
        send_delayed_message_to_me(&Tick, 5.0f, SMMS_Broadcast, 0);
        return true;
    }

    // 06f8cf09-06f8d087: back in the area: REENTERED_COMBAT_AREA, slot 17, the countdown
    // objective removed and deleted, a new leave trigger (AreaRadius + 250).
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, Cookies.ReenterArea))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        VignetteOffer->General.SendConversations(
            RmCommReenteredCombatArea, this, FindContentPlayerStateByShip(&Fired->Activator));
        SendPlaceholderObjectives();
        std::vector<FRmObjective*>::iterator Active = FindObjective(9);
        if (Active != ActiveObjectives.end())
        {
            FRmObjective* const Countdown = *Active;
            RemoveObjective(9);
            if (Countdown != nullptr)
                delete Countdown;
        }
        AddStateMachine(new FRmProximityLeaveTrigger(Cookies.LeaveArea, get_id(),
                                                     VignetteOffer->System->SystemId, &Position,
                                                     AreaRadius + 250.0f, GetMissionPlayers()));
        LeftAreaTime = -1.0f;
        return true;
    }

    // 06f8d08a-06f8d1be: the countdown ran out: when (GRmMultiplayerDelay - 1.0) <= the time
    // since the players left (FCOMPP, TEST AH,0x41 + JP skips greater and unordered):
    // GLOBAL_FAIL_COMBAT_RANGE, reason 0xb (single player) or 0xc.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, Cookies.Countdown))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        if (LeftAreaTime == -1.0f)
            return true;
        double const Now = GetContentGameTime();
        double const Left = LeftAreaTime;
        double const Elapsed = GetContentTimeDistance(&Left, &Now);
        double const Limit = static_cast<double>(GRmMultiplayerDelay) - 1.0;
        if (Limit > Elapsed || Limit != Limit || Elapsed != Elapsed)
            return true;
        VignetteOffer->General.SendConversations(
            RmCommGlobalFailCombatRange, this, FindContentPlayerStateByShip(&Fired->Activator));
        FailReason = pub::SinglePlayer() ? 0xb : 0xc;
        set_state(6);
        LeftAreaTime = -1.0f;
        return true;
    }

    // 06f8d1c1-06f8d348: the 5 s tick (DAT_06fd3b7c): the countdown objective is taken out and,
    // while time is left (FCOM 0.0, TEST AH,0x41 + JNZ: less, equal or unordered stop), put
    // back at the front with the remaining seconds; the tick is sent again. Retail reads the
    // objective without an end test (06f8d263): the tick only runs while it is active.
    if (RmMessageOf(this, Event, GRmMsgType06fd3b7c) != nullptr)
    {
        if (LeftAreaTime == -1.0f)
            return true;
        double const Now = GetContentGameTime();
        double const Left = LeftAreaTime;
        int const Elapsed = static_cast<int>(GetContentTimeDistance(&Left, &Now));
        FRmObjective* const Countdown = *FindObjective(9);
        RemoveObjective(9);
        double const Remaining = static_cast<double>(GRmMultiplayerDelay) -
                                 static_cast<double>(Elapsed);
        if (!(Remaining > 0.0))
            return true;
        unsigned int const SystemId = VignetteOffer->System->SystemId;
        static_cast<FRmObjectiveType4*>(Countdown)
            ->SetCountdown(static_cast<int>(Remaining), SystemId, 0x44d, Position);
        InsertObjective(9, Countdown, ActiveObjectives.begin());
        StateMachineMessage Tick(GRmMsgType06fd3b7c);
        send_delayed_message_to_me(&Tick, 5.0f, SMMS_Broadcast, 0);
        return true;
    }

    // 06f8d34b-06f8d3ba: a ship destroyed (DAT_06fd3ba8): its loot (slot 26).
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgType06fd3ba8))
    {
        unsigned int const ObjectId = static_cast<FRmShipMessage const*>(Message)->ObjectId;
        SpawnRecordLoot(Ships->Records.FindById(ObjectId));
        return true;
    }
    return false;
}

// State 3 (06f8d3fc-06f8d6ea): on enter the mission hears the machine started; the target
// ships' marker (state 8) or the main battle's (state 10); AreaRadius 3500; the system comm
// sequences for every player with a ship.
bool FRmVignetteMachine::HandleStart(SMEventData const* Event)
{
    if (!RmIsEnterEvent(Event))
        return false;
    StateMachineMessage Started(GRmMsgMachineStarted);
    send_immediate_message(&Started, static_cast<MessageHandler*>(Mission)->get_id());

    FRmVignetteOffer* const Vignette = VignetteOffer.Get();
    if (Vignette->TargetShips.bActive)
    {
        unsigned int const SystemId = Vignette->System->SystemId;
        static_cast<FRmObjectiveType2*>(RmComponentObjective(&Vignette->General, 1))
            ->SetMarker(SystemId, 0x44d, Vignette->TargetShips.Position);
        AddObjective(1, static_cast<FRmObjective*>(RmComponentObjective(&Vignette->General, 1)));
        set_state(8);
    }
    else
    {
        Vector const* Center = RmMainBattleCenter(Vignette);
        unsigned int const SystemId = Vignette->System->SystemId;
        static_cast<FRmObjectiveType2*>(RmComponentObjective(&Vignette->General, 2))
            ->SetMarker(SystemId, 0x44d, *Center);
        AddObjective(2, static_cast<FRmObjective*>(RmComponentObjective(&Vignette->General, 2)));
        set_state(10);
    }
    AreaRadius = 3500.0f;
    std::vector<FContentPlayerState*>* Players = Mission->GetPlayerStates();
    for (std::vector<FContentPlayerState*>::iterator It = Players->begin(); It != Players->end();
         ++It)
    {
        if ((*It)->ShipObject == nullptr)
            continue;
        SendSystemConversations((*It)->PlayerId, (*It)->GetSystemRecord()->SystemId);
    }
    return true;
}

// State 8 (06f8d6ed-06f8d9b2): the target ships' pre-battle.
bool FRmVignetteMachine::HandlePreBattle(SMEventData const* Event)
{
    static unsigned int const CookieTargetFar = RmNextVignetteCookie();     // DAT_06fd3b58
    static unsigned int const CookieTargetNear = RmNextVignetteCookie();    // DAT_06fd3b54

    // 06f8d726-06f8d875: the area moves to the target; triggers at 2500 (DAT_06fbc2c4) and
    // 10000 (DAT_06fbc2ec).
    if (RmIsEnterEvent(Event))
    {
        Position = VignetteOffer->TargetShips.Position;
        AddStateMachine(new FRmProximityTrigger(CookieTargetNear, get_id(),
                                                VignetteOffer->System->SystemId,
                                                &VignetteOffer->TargetShips.Position, 2500.0f,
                                                GetMissionPlayers()));
        AddStateMachine(new FRmProximityTrigger(CookieTargetFar, get_id(),
                                                VignetteOffer->System->SystemId,
                                                &VignetteOffer->TargetShips.Position, 10000.0f,
                                                GetMissionPlayers()));
        return true;
    }

    // 06f8d878-06f8d8f3 (+ 06f8efb2): a player within 10000: the population is kept out of the
    // target's 10000 sphere; the area is reached.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieTargetFar))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        Vector const& Target = VignetteOffer->TargetShips.Position;
        SpacePopAddExclusionSphere(Target.x, Target.y, Target.z, 10000.0f);
        bAreaReached = 1;
        return true;
    }

    // 06f8d8f8-06f8d9b2: a player within 2500: PLAYER_IN_PRE_BATTLE_SPAWN_RANGE, the marker
    // objective 1 goes, state 9.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieTargetNear))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        VignetteOffer->General.SendConversations(
            RmCommPreBattleSpawnRange, this, FindContentPlayerStateByShip(&Fired->Activator));
        RemoveObjective(1);
        set_state(9);
        return true;
    }
    return false;
}

// State 9 (06f8d9b5-06f8e5b6): the fight with the target ships.
bool FRmVignetteMachine::HandleTargetBattle(SMEventData const* Event)
{
    static unsigned int const CookieNextGroup = RmNextVignetteCookie();     // DAT_06fd3b50
    static unsigned int const CookieTargetDone = RmNextVignetteCookie();    // DAT_06fd3b4c
    FRmVignetteEntryCookies const& Cookies = RmVignetteEntryCookies();
    FRmVignetteOffer* const Vignette = VignetteOffer.Get();

    // 06f8d9f0-06f8dd9a: the leave trigger (AreaRadius + 250); the first target group spawns,
    // the delayed ones (+0x04 == 1) get their cookie after +0x08 seconds; the end condition of
    // the target fight by the component's +0x50 (1 timer, 2 ship count, 3 ship health).
    if (RmIsEnterEvent(Event))
    {
        AddStateMachine(new FRmProximityLeaveTrigger(Cookies.LeaveArea, get_id(),
                                                     Vignette->System->SystemId,
                                                     &Vignette->TargetShips.Position,
                                                     AreaRadius + 250.0f, GetMissionPlayers()));
        std::vector<FRmShipGroup>& Groups = Vignette->TargetShips.Groups;
        if (!Groups.empty())
        {
            SpawnGroupWithTriggers(Groups.front().Serial, &Vignette->TargetShips, CookieNextGroup);
            for (std::vector<FRmShipGroup>::iterator It = VignetteOffer->TargetShips.Groups.begin() + 1;
                 It != VignetteOffer->TargetShips.Groups.end(); ++It)
            {
                if (It->Field_0x04 != 1)
                    continue;
                FRmCookieMessage Delayed(It->Serial);
                send_delayed_message_to_me(&Delayed, static_cast<float>(It->Field_0x08),
                                           SMMS_Self, 0);
            }
        }
        int const Mode = VignetteOffer->TargetShips.Field_0x50;
        if (Mode == 1)
        {
            AddStateMachine(new FRmTimerTrigger(
                CookieTargetDone, get_id(),
                static_cast<float>(VignetteOffer->TargetShips.Field_0x54)));
        }
        else if (Mode == 2)
        {
            AddStateMachine(new FRmShipCountTrigger(CookieTargetDone, get_id(), &Ships->Records,
                                                    VignetteOffer->TargetShips.Field_0x54));
        }
        else if (Mode == 3)
        {
            AddStateMachine(new FRmShipHealthTrigger(CookieTargetDone, get_id(),
                                                     &Ships->Records,
                                                     VignetteOffer->TargetShips.Field_0x54, 2));
        }
        return true;
    }

    // 06f8dd9d-06f8e052: the target fight is over once every target group spawned: with no
    // ship alive SUCCESS_KILLED_ALL_GOING_TO_MAIN (or straight to state 5 when the main ships
    // have nothing to spawn), else SUCCESS_NOT_KILLED_ALL_GOING_TO_MAIN; the target
    // component's objective 3 marks the main battle (key 2, at the front); state 10.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieTargetDone))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        if (RmHasUnspawnedGroup(&VignetteOffer->TargetShips.Groups))
            return true;
        // 06f8de2f-06f8de3d: Query(0, 2, 2) is built and kept to the end unread.
        FRmRecordList Flagged = Ships->Query(0, 2, 2);
        bool bNoneAlive = false;
        {
            FRmRecordList Alive = Ships->Query(0, 2, 0);
            bNoneAlive = Alive.Records.size() == 0;
        }
        if (bNoneAlive)
        {
            if (!RmHasUnspawnedGroup(&VignetteOffer->Ships.Groups))
            {
                set_state(5);
                return true;
            }
            VignetteOffer->General.SendConversations(RmCommKilledAllGoingToMain, this, nullptr);
        }
        else
        {
            VignetteOffer->General.SendConversations(RmCommNotKilledAllGoingToMain, this,
                                                     nullptr);
        }
        Vector const* Center = RmMainBattleCenter(VignetteOffer.Get());
        unsigned int const SystemId = VignetteOffer->System->SystemId;
        static_cast<FRmObjectiveType2*>(RmComponentObjective(&VignetteOffer->TargetShips, 3))
            ->SetMarker(SystemId, 0x44d, *Center);
        InsertObjective(2,
                        static_cast<FRmObjective*>(
                            RmComponentObjective(&VignetteOffer->TargetShips, 3)),
                        ActiveObjectives.begin());
        set_state(10);
        return true;
    }

    // 06f8e055-06f8e15c: the next target group, unless every group spawned and no target
    // objective (7, 4, 6, 5) is left.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieNextGroup))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        if (!RmHasUnspawnedGroup(&VignetteOffer->TargetShips.Groups))
        {
            if (FindObjective(7) == ActiveObjectives.end() &&
                FindObjective(4) == ActiveObjectives.end() &&
                FindObjective(6) == ActiveObjectives.end() &&
                FindObjective(5) == ActiveObjectives.end())
                return true;
        }
        SpawnGroupWithTriggers(Fired->Activator, &VignetteOffer->TargetShips, CookieNextGroup);
        return true;
    }

    // 06f8e15f-06f8e1c9: a delayed target group (DAT_06fd3b78).
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgType06fd3b78))
    {
        SpawnGroupWithTriggers(static_cast<FRmCookieMessage const*>(Message)->Cookie,
                               &VignetteOffer->TargetShips, CookieNextGroup);
        return true;
    }

    // 06f8e1cc-06f8e200: a whole group destroyed (DAT_06fd3ba4) is taken.
    if (RmMessageOf(this, Event, GRmMsgType06fd3ba4) != nullptr)
        return true;

    // 06f8e206-06f8e411: the flagged ships of a group died (DAT_06fd3ba0): once every target
    // group spawned and at most the countdown is active, the countdown goes, and the main
    // battle starts (KILLED_ALL, objective 3 as 2, state 10) or, with no main ships to spawn,
    // state 5.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgType06fd3ba0))
    {
        OnFlaggedShipsLost(static_cast<FRmShipMessage const*>(Message)->ObjectId);
        if (RmHasUnspawnedGroup(&VignetteOffer->TargetShips.Groups))
            return true;
        if (ActiveObjectives.size() != 0)
        {
            if (ActiveObjectives.size() != 1)
                return true;
            if (FindObjective(9) == ActiveObjectives.end())
                return true;
        }
        if (ActiveObjectives.size() == 1)
        {
            RemoveObjective(9);
            LeftAreaTime = -1.0f;
        }
        if (!RmHasUnspawnedGroup(&VignetteOffer->Ships.Groups))
        {
            set_state(5);
            return true;
        }
        VignetteOffer->General.SendConversations(RmCommKilledAllGoingToMain, this, nullptr);
        AddObjective(2, static_cast<FRmObjective*>(
                            RmComponentObjective(&VignetteOffer->TargetShips, 3)));
        set_state(10);
        return true;
    }

    // 06f8e414-06f8e5b6: the exit to state 10 (PendingState): AreaRadius grows to
    // GRmVignetteOuterRadius and every alive ship leaves its formation for the main battle:
    // StayInRange 2000 (DAT_06fbc2cc) of the centre, then GotoVec there (750, -1.0, cruise).
    if (Event->get_event_type() < 4 && Event->get_event_type() == 2)
    {
        if (PendingState != 10)
            return true;
        AreaRadius = GRmVignetteOuterRadius;
        TRefHandle<FObjListDirective> StayInRange;
        CreateObjListDirective(0xd, &StayInRange.Pointer);
        static_cast<FObjListStayInRange*>(StayInRange.Pointer)
            ->SetSphere(RmMainBattleCenter(VignetteOffer.Get()), 2000.0f);
        TRefHandle<FObjListDirective> GotoCenter;
        CreateObjListDirective(5, &GotoCenter.Pointer);
        static_cast<FObjListGotoVec*>(GotoCenter.Pointer)
            ->SetDestination(RmMainBattleCenter(VignetteOffer.Get()), 750.0f, -1.0f, 0, false, true);
        FRmRecordList Alive = Ships->Query(0, 2, 0);
        for (std::list<FRmSpawnRecord*>::iterator It = Alive.Records.begin();
             It != Alive.Records.end(); ++It)
        {
            FContentNpcShip* const Ship = (*It)->NpcShip;
            RemoveFromFormation(Ship);
            Ship->CancelDirectives();
            Ship->AddDirective(StayInRange.Pointer);
            Ship->AddDirective(GotoCenter.Pointer);
            Ship->RestartDirectives();
        }
        return true;
    }
    return false;
}

// State 10 (06f8e5b9-06f8f0f3): on the way to the main battle.
bool FRmVignetteMachine::HandleApproachMain(SMEventData const* Event)
{
    static unsigned int const CookieMainShips = RmNextVignetteCookie();     // DAT_06fd3b48
    static unsigned int const CookieMainFar = RmNextVignetteCookie();       // DAT_06fd3b40
    static unsigned int const CookieMainNear = RmNextVignetteCookie();      // DAT_06fd3b3c
    FRmVignetteEntryCookies const& Cookies = RmVignetteEntryCookies();

    // 06f8e612-06f8e91e: the objectives still at the old centre (every |delta|^2 <= 1.0;
    // TEST AH,0x41 + JP skips greater and unordered) move to the main battle's; the area moves;
    // the leave trigger when there was a target; triggers at 7500 (DAT_06fbc2b0) and 10000.
    if (RmIsEnterEvent(Event))
    {
        Vector const* CenterPtr = RmMainBattleCenter(VignetteOffer.Get());
        Vector const Center = *CenterPtr;
        Vector const Old = Position;
        for (std::vector<FRmObjective*>::iterator It = ActiveObjectives.begin();
             It != ActiveObjectives.end(); ++It)
        {
            Vector At;
            if (!(*It)->GetPosition(&At))
                continue;
            double const Dx = static_cast<double>(At.x) - static_cast<double>(Old.x);
            if (!(Dx * Dx <= 1.0))
                continue;
            double const Dy = static_cast<double>(At.y) - static_cast<double>(Old.y);
            if (!(Dy * Dy <= 1.0))
                continue;
            double const Dz = static_cast<double>(At.z) - static_cast<double>(Old.z);
            if (!(Dz * Dz <= 1.0))
                continue;
            (*It)->SetPosition(Center);
        }
        Position = Center;
        if (VignetteOffer->TargetShips.bActive)
        {
            AddStateMachine(new FRmProximityLeaveTrigger(
                Cookies.LeaveArea, get_id(), VignetteOffer->System->SystemId,
                RmMainBattleCenter(VignetteOffer.Get()), AreaRadius + 250.0f, GetMissionPlayers()));
        }
        AddStateMachine(new FRmProximityTrigger(CookieMainShips, get_id(),
                                                VignetteOffer->System->SystemId,
                                                RmMainBattleCenter(VignetteOffer.Get()), 7500.0f,
                                                GetMissionPlayers()));
        AddStateMachine(new FRmProximityTrigger(CookieMainFar, get_id(),
                                                VignetteOffer->System->SystemId,
                                                RmMainBattleCenter(VignetteOffer.Get()), 10000.0f,
                                                GetMissionPlayers()));
        return true;
    }

    // 06f8e921-06f8ea7a: the flagged ships of a group died (DAT_06fd3ba0): with every main
    // group spawned and no target, loot or dock objective left, state 5.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgType06fd3ba0))
    {
        OnFlaggedShipsLost(static_cast<FRmShipMessage const*>(Message)->ObjectId);
        if (RmHasUnspawnedGroup(&VignetteOffer->Ships.Groups))
            return true;
        if (FindObjective(7) != ActiveObjectives.end() ||
            FindObjective(4) != ActiveObjectives.end() ||
            FindObjective(6) != ActiveObjectives.end() ||
            FindObjective(5) != ActiveObjectives.end())
            return true;
        if (FindObjective(0xa) != ActiveObjectives.end() ||
            FindObjective(0xb) != ActiveObjectives.end() ||
            FindObjective(0xc) != ActiveObjectives.end() ||
            FindObjective(0xd) != ActiveObjectives.end())
            return true;
        set_state(5);
        return true;
    }

    // 06f8ea7d-06f8ed76: a solar destroyed (DAT_06fd3b9c): the last flagged solar of its type
    // takes objective 4 (type 0) or 5 (type 1); with no flagged solar left the lone objective 8
    // goes; then, once nothing else is left, the countdown goes and state 5.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgType06fd3b9c))
    {
        unsigned int const SolarId = static_cast<FRmSolarMessage const*>(Message)->ObjectId;
        FRmSolarRecord* Record = static_cast<FRmSolarRecord*>(Solars->Records.FindById(SolarId));
        size_t TypeLeft = 0;
        {
            FRmRecordList Left = Solars->Query(Record->SolarType, 2, 2);
            TypeLeft = Left.Records.size();
        }
        if (TypeLeft != 0)
            return true;
        if (Record->SolarType == 0)
            RemoveObjective(4);
        else if (Record->SolarType == 1)
            RemoveObjective(5);
        size_t AnyLeft = 0;
        {
            FRmRecordList Left = Solars->Query(-1, 2, 2);
            AnyLeft = Left.Records.size();
        }
        if (AnyLeft != 0)
            return true;
        if (ActiveObjectives.size() == 1 && FindObjective(8) != ActiveObjectives.end())
            RemoveObjective(8);
        if (RmHasUnspawnedGroup(&VignetteOffer->Ships.Groups))
            return true;
        if (FindObjective(0xa) != ActiveObjectives.end() ||
            FindObjective(0xb) != ActiveObjectives.end() ||
            FindObjective(0xc) != ActiveObjectives.end() ||
            FindObjective(0xd) != ActiveObjectives.end())
            return true;
        // 06f8ec7b-06f8ecb0: the key 7 search through the FUN_06f90a40 getter.
        std::vector<FRmObjective*>::iterator Big = ActiveObjectives.begin();
        while (Big != ActiveObjectives.end() && (*Big)->Key != 7)
            ++Big;
        if (Big != ActiveObjectives.end())
            return true;
        if (FindObjective(4) != ActiveObjectives.end() ||
            FindObjective(6) != ActiveObjectives.end() ||
            FindObjective(5) != ActiveObjectives.end())
            return true;
        if (FindObjective(9) != ActiveObjectives.end())
        {
            RemoveObjective(9);
            LeftAreaTime = -1.0f;
        }
        set_state(5);
        return true;
    }

    // 06f8ed7b-06f8eee6: a player within 7500: PLAYER_IN_MAIN_BATTLE_SOLAR_SPAWN_RANGE, the
    // solars spawn (slot 22), a trigger at 2500 for the ships.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieMainShips))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        VignetteOffer->General.SendConversations(RmCommMainBattleSolarSpawnRange, this,
                                                 FindContentPlayerStateByShip(&Fired->Activator));
        SpawnSolars(&VignetteOffer->Solars.Solars);
        AddStateMachine(new FRmProximityTrigger(CookieMainNear, get_id(),
                                                VignetteOffer->System->SystemId,
                                                RmMainBattleCenter(VignetteOffer.Get()), 2500.0f,
                                                GetMissionPlayers()));
        return true;
    }

    // 06f8eeeb-06f8efd5: a player within 10000: the exclusion sphere; the area is reached.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieMainFar))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        Vector const* Center = RmMainBattleCenter(VignetteOffer.Get());
        SpacePopAddExclusionSphere(Center->x, Center->y, Center->z, 10000.0f);
        bAreaReached = 1;
        return true;
    }

    // 06f8efd8-06f8f0b3: a player within 2500: PLAYER_IN_MAIN_BATTLE_SHIP_SPAWN_RANGE, state 11.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieMainNear))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        VignetteOffer->General.SendConversations(RmCommMainBattleShipSpawnRange, this,
                                                 FindContentPlayerStateByShip(&Fired->Activator));
        set_state(11);
        return true;
    }

    // 06f8f0b6-06f8f0f3: the exit drops the main battle marker (key 2; PUSH EAX with the event
    // type 2).
    if (Event->is_sm_event() && Event->get_event_type() == 2)
    {
        RemoveObjective(2);
        return true;
    }
    return false;
}

// State 11 (06f8f0f6-06f8f895): the main battle.
bool FRmVignetteMachine::HandleMainBattle(SMEventData const* Event)
{
    static unsigned int const CookieNextGroup = RmNextVignetteCookie();     // DAT_06fd3b38
    FRmVignetteEntryCookies const& Cookies = RmVignetteEntryCookies();

    // 06f8f120-06f8f4d6: AreaRadius 3500; the flagged solars of type 1 (objective 5) and 0
    // (objective 4) are marked; the first main group spawns unless delayed, every delayed group
    // gets its cookie; the leave trigger.
    if (Event->is_sm_event() && Event->get_event_type() == 1)
    {
        AreaRadius = 3500.0f;
        FRmRecordList Others = Solars->Query(1, 2, 2);
        if (Others.Records.size() != 0)
        {
            static_cast<FRmObjectiveType3*>(RmComponentObjective(&VignetteOffer->Solars, 5))
                ->SetTargetRecords(&Others, GetMissionPlayers(), 2,
                                   VignetteOffer->System->SystemId, Position);
            AddObjective(5, static_cast<FRmObjective*>(
                                RmComponentObjective(&VignetteOffer->Solars, 5)));
        }
        FRmRecordList Big = Solars->Query(0, 2, 2);
        if (Big.Records.size() != 0)
        {
            static_cast<FRmObjectiveType3*>(RmComponentObjective(&VignetteOffer->Solars, 4))
                ->SetTargetRecords(&Big, GetMissionPlayers(), 2,
                                   VignetteOffer->System->SystemId, Position);
            AddObjective(4, static_cast<FRmObjective*>(
                                RmComponentObjective(&VignetteOffer->Solars, 4)));
        }
        if (!VignetteOffer->Ships.Groups.empty())
        {
            std::vector<FRmShipGroup>::iterator First = VignetteOffer->Ships.Groups.begin();
            if (First->Field_0x04 != 1)
                SpawnGroupWithTriggers(First->Serial, &VignetteOffer->Ships, CookieNextGroup);
            for (std::vector<FRmShipGroup>::iterator It = First;
                 It != VignetteOffer->Ships.Groups.end(); ++It)
            {
                if (It->Field_0x04 != 1)
                    continue;
                FRmCookieMessage Delayed(It->Serial);
                send_delayed_message_to_me(&Delayed, static_cast<float>(It->Field_0x08),
                                           SMMS_Self, 0);
            }
        }
        AddStateMachine(new FRmProximityLeaveTrigger(
            Cookies.LeaveArea, get_id(), VignetteOffer->System->SystemId,
            RmMainBattleCenter(VignetteOffer.Get()), AreaRadius + 250.0f, GetMissionPlayers()));
        return true;
    }

    // 06f8f4d9-06f8f5c0: the next main group (FUN_06f90b40 / FUN_06f8c330 gate).
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieNextGroup))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        if (!RmHasUnspawnedGroup(&VignetteOffer->Ships.Groups) && !HasTargetObjectives())
            return true;
        SpawnGroupWithTriggers(Fired->Activator, &VignetteOffer->Ships, CookieNextGroup);
        return true;
    }

    // 06f8f5c3-06f8f654: a delayed main group (DAT_06fd3b78).
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgType06fd3b78))
    {
        SpawnGroupWithTriggers(static_cast<FRmCookieMessage const*>(Message)->Cookie,
                               &VignetteOffer->Ships, CookieNextGroup);
        return true;
    }

    // The common end of 06f8f6e0-06f8f71b / 06f8f845-06f8f895 (06f8f87c, 06f8f884, 06f8ed3d,
    // 06f8ed50): with no objective or only the countdown, state 5 (the countdown removed).
    struct FTail
    {
        static bool Run(FRmVignetteMachine* Machine)
        {
            if (!Machine->ActiveObjectives.empty())
            {
                if (Machine->ActiveObjectives.size() != 1)
                    return true;
                if (Machine->FindObjective(9) == Machine->ActiveObjectives.end())
                    return true;
            }
            if (Machine->ActiveObjectives.size() == 1)
            {
                Machine->RemoveObjective(9);
                Machine->LeftAreaTime = -1.0f;
            }
            Machine->set_state(5);
            return true;
        }
    };

    // 06f8f657-06f8f71d: the flagged ships of a group died (DAT_06fd3ba0).
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgType06fd3ba0))
    {
        OnFlaggedShipsLost(static_cast<FRmShipMessage const*>(Message)->ObjectId);
        if (RmHasUnspawnedGroup(&VignetteOffer->Ships.Groups))
            return true;
        return FTail::Run(this);
    }

    // 06f8f720-06f8f895: a solar destroyed (DAT_06fd3b9c).
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgType06fd3b9c))
    {
        unsigned int const SolarId = static_cast<FRmSolarMessage const*>(Message)->ObjectId;
        FRmSolarRecord* Record = static_cast<FRmSolarRecord*>(Solars->Records.FindById(SolarId));
        bool bTypeGone = false;
        {
            FRmRecordList Left = Solars->Query(Record->SolarType, 2, 2);
            bTypeGone = Left.Records.empty();
        }
        if (!bTypeGone)
            return true;
        if (Record->SolarType == 0)
            RemoveObjective(4);
        else if (Record->SolarType == 1)
            RemoveObjective(5);
        if (ActiveObjectives.size() == 1 && FindObjective(8) != ActiveObjectives.end())
            RemoveObjective(8);
        if (RmHasUnspawnedGroup(&VignetteOffer->Ships.Groups))
            return true;
        return FTail::Run(this);
    }
    return false;
}

// State 12 (06f8f89a-06f9059a): the critical loot.
bool FRmVignetteMachine::HandleCriticalLoot(SMEventData const* Event)
{
    // 06f8f8a3-06f8f8d1: the enter event and controller message 0x27 are taken (the "any
    // state" dock failure does not apply here).
    if (Event->is_sm_event() && Event->get_event_type() == 1)
        return true;
    if (RmControllerOf(Event, EMessage::Msg0x27) != nullptr)
        return true;

    // 06f8f8d7-06f8fa82: a mapped player with a ship left (DAT_06fd3b70): the critical loot he
    // holds (state 4) is dropped next to his ship (DAT_06fd3b6c to the loot machine). False:
    // the "any state" handler runs as well.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgPlayerLeft))
    {
        unsigned int const PlayerId = static_cast<FRmPlayerLeftMessage const*>(Message)->PlayerId;
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerMap->find(PlayerId);
        if (It == PlayerMap->end())
            return false;
        if (!RmPlayerHasShip(It->second))
            return false;
        FRmRecordList Held = Loot->Query(0, 2, 0, 4, PlayerId);
        if (!Held.Records.empty())
        {
            FRmLootRecord* const Record = static_cast<FRmLootRecord*>(Held.Records.front());
            FindContentPlayerState(Record->HolderId);
            unsigned int const ShipId = FindContentPlayerState(Record->HolderId)->GetShipId();
            FRmLootMessage Drop(GRmMsgType06fd3b6c, Record->ObjectId, ShipId);
            send_immediate_message(&Drop, static_cast<MessageHandler*>(Loot)->get_id());
        }
        return false;
    }

    // The DAT_06fd3b6c drop of the loot record's holder (06f8fa0c-06f8fa5f and its copies).
    struct FDrop
    {
        static void Send(FRmVignetteMachine* Machine, FRmLootRecord* Record)
        {
            unsigned int const ShipId = FindContentPlayerState(Record->HolderId)->GetShipId();
            FRmLootMessage Drop(GRmMsgType06fd3b6c, Record->ObjectId, ShipId);
            Machine->send_immediate_message(&Drop,
                                            static_cast<MessageHandler*>(Machine->Loot)->get_id());
        }
    };

    // 06f8fa85-06f8fec6: the loot picked up (DAT_06fd3b90 {loot, ship}). Mode 1: dropped again.
    // Mode 2: a mission player has it -> objective 0xb goes, state 5. Mode 3: a mission player
    // has it -> objective 0xb goes, the dock of the offer base becomes objective 0xc. A
    // non-mission picker gets it dropped again.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgType06fd3b90))
    {
        FRmLootMessage const* Picked = static_cast<FRmLootMessage const*>(Message);
        FRmLootRecord* Record = static_cast<FRmLootRecord*>(Loot->Records.FindById(Picked->ObjectId));
        int const Mode = Record->Field_0x6c;
        if (Mode == 1)
        {
            FDrop::Send(this, Record);
            return true;
        }
        if (Mode != 2 && Mode != 3)
            return true;
        FContentPlayerState* Picker = FindContentPlayerStateByShip(&Picked->Value);
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        if (PlayerMap->find(Picker->PlayerId) == PlayerMap->end())
        {
            FDrop::Send(this, Record);
            return true;
        }
        RemoveObjective(0xb);
        if (Mode == 2)
        {
            set_state(5);
            return true;
        }

        // 06f8fb6f-06f8fcce: the dock object of the offer base (FUN_06eac4d0, its out
        // position being the buffer the holder's ship position was read into).
        FContentPlayerState* Holder = FindContentPlayerState(Record->HolderId);
        FContentBase* Base = FindContentBaseInfo(&VignetteOffer->OfferBase);
        Vector ShipPosition;
        Holder->ShipObject->GetPosition(&ShipPosition);
        unsigned int const DockId = Base->FindDockObject(&ShipPosition);
        Vector Where;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(DockId, Where, Orientation);
        std::vector<unsigned int> Dock;
        Dock.push_back(DockId);
        static_cast<FRmObjectiveType3*>(RmComponentObjective(&VignetteOffer->General, 0xc))
            ->SetTargets(&Dock, GetMissionPlayers(), Base->GetSystemInfo()->SystemId, Where);
        AddObjective(0xc, static_cast<FRmObjective*>(
                              RmComponentObjective(&VignetteOffer->General, 0xc)));
        return true;
    }

    // 06f8fec9-06f9024b: the loot created again (DAT_06fd3b84): mode 3 -> objectives 0xc and
    // 0xb go, the offer's objective 0xb marks the loot; modes 1 / 2 -> the active objective 0xa
    // / 0xb marks every alive critical loot of the mode and is rebuilt (no end test, 06f901c0).
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgType06fd3b84))
    {
        unsigned int const LootId = static_cast<FRmLootMessage const*>(Message)->ObjectId;
        FRmLootRecord* Record = static_cast<FRmLootRecord*>(Loot->Records.FindById(LootId));
        int const Mode = Record->Field_0x6c;
        if (Mode == 3)
        {
            RemoveObjective(0xc);
            RemoveObjective(0xb);
            Vector Where;
            Matrix Orientation;
            pub::SpaceObj::GetLocation(Record->ObjectId, Where, Orientation);
            unsigned int SystemId = 0;
            pub::SpaceObj::GetSystem(Record->ObjectId, SystemId);
            FRmRecordList Single;
            Single.Records.push_back(Record);
            static_cast<FRmObjectiveType3*>(RmComponentObjective(&VignetteOffer->General, 0xb))
                ->SetTargetRecords(&Single, GetMissionPlayers(), 2, SystemId, Where);
            AddObjective(0xb, static_cast<FRmObjective*>(
                                  RmComponentObjective(&VignetteOffer->General, 0xb)));
            return true;
        }
        if (Mode != 1 && Mode != 2)
            return true;
        Vector Where;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(Record->ObjectId, Where, Orientation);
        unsigned int SystemId = 0;
        pub::SpaceObj::GetSystem(Record->ObjectId, SystemId);
        std::vector<FRmObjective*>::iterator Active = FindObjective(Mode == 1 ? 0xa : 0xb);
        FRmRecordList Critical = Loot->Query(Record->Field_0x6c, 2, 0, 2, 0);
        FRmObjective* const Objective = *Active;
        static_cast<FRmObjectiveType3*>(Objective)
            ->SetTargetRecords(&Critical, GetMissionPlayers(), 2, SystemId, Where);
        RebuildObjective(Objective->Key);
        return true;
    }

    // 06f9024e-06f9031c: the loot destroyed by a player (DAT_06fd3b94 {loot, ship}): mode 1 ->
    // objective 0xa goes, state 5; modes 2 / 3 -> objective 0xb goes,
    // GLOBAL_FAIL_DESTROYED_CRITICAL_LOOT, reason 4 / 5, state 6.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgType06fd3b94))
    {
        FRmLootMessage const* Destroyed = static_cast<FRmLootMessage const*>(Message);
        FRmLootRecord* Record =
            static_cast<FRmLootRecord*>(Loot->Records.FindById(Destroyed->ObjectId));
        int const Mode = Record->Field_0x6c;
        if (Mode == 1)
        {
            RemoveObjective(0xa);
            set_state(5);
            return true;
        }
        if (Mode != 2 && Mode != 3)
            return true;
        RemoveObjective(0xb);
        VignetteOffer->General.SendConversations(RmCommGlobalFailDestroyedCriticalLoot, this,
                                                 FindContentPlayerStateByShip(&Destroyed->Value));
        FailReason = Mode == 2 ? 4 : 5;
        set_state(6);
        return true;
    }

    // 06f9037e-06f90420: the loot destroyed otherwise (DAT_06fd3b80): reason 6 / 7 / 8 by
    // mode (unchanged for another mode), state 6.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgType06fd3b80))
    {
        unsigned int const LootId = static_cast<FRmLootMessage const*>(Message)->ObjectId;
        FRmLootRecord* Record = static_cast<FRmLootRecord*>(Loot->Records.FindById(LootId));
        int const Mode = Record->Field_0x6c;
        if (Mode == 1)
            FailReason = 6;
        else if (Mode == 2)
            FailReason = 7;
        else if (Mode == 3)
            FailReason = 8;
        set_state(6);
        return true;
    }

    // 06f90423-06f904ee: controller message 0x39 of a mapped player (parms +0xc) holding the
    // first critical loot (state 4): reason 9, state 6. False either way. Retail reads the
    // first record without an empty test: in state 12 a critical loot record exists.
    if (SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::ObjectDestroyed))
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        if (PlayerMap->find(Parms[3]) == PlayerMap->end())
            return false;
        FRmRecordList Critical = Loot->Query(0, 2, 0, 0, 0);
        FRmLootRecord* const First = static_cast<FRmLootRecord*>(Critical.Records.front());
        if (First->State == 4 && First->HolderId == Parms[3])
        {
            FailReason = 9;
            set_state(6);
        }
        return false;
    }

    // 06f904f1-06f9059a: the holder docked (DAT_06fd3b88 {loot, ship, base}): mode 3 at the
    // offer base -> objective 0xc goes, state 5; elsewhere reason 10, state 6.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgType06fd3b88))
    {
        FRmLootPlayerMessage const* Docked = static_cast<FRmLootPlayerMessage const*>(Message);
        FRmLootRecord* Record =
            static_cast<FRmLootRecord*>(Loot->Records.FindById(Docked->ObjectId));
        if (Record->Field_0x6c != 3)
            return true;
        if (Docked->Value1 == VignetteOffer->OfferBase)
        {
            RemoveObjective(0xc);
            set_state(5);
            return true;
        }
        FailReason = 10;
        set_state(6);
        return true;
    }
    return false;
}

// State 5 (06f9059f-06f90770): succeeded. GLOBAL_SUCCESS and a 5 s timeout; on the timeout
// slot 27, DAT_06fd3bb4 to the mission after 0.001 s, state 7.
bool FRmVignetteMachine::HandleSucceeded(SMEventData const* Event)
{
    FRmVignetteEntryCookies const& Cookies = RmVignetteEntryCookies();
    if (Event->is_sm_event() && Event->get_event_type() == 1)
    {
        VignetteOffer->General.SendConversations(RmCommGlobalSuccess, this, nullptr);
        StateMachineMessage Timeout(SMM_TIMEOUT);
        send_delayed_message_to_me(&Timeout, 5.0f, SMMS_Self, 0);
        return true;
    }
    if (RmMessageOf(this, Event, SMM_TIMEOUT) != nullptr)
    {
        OnSucceeded();
        StateMachineMessage Done(GRmMsgMachineSucceeded);
        send_delayed_message(&Done, 0.001f, static_cast<MessageHandler*>(Mission)->get_id());
        set_state(7);
        return true;
    }
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, Cookies.ReenterArea))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        return true;
    }
    return false;
}

// State 6 (06f90773-06f9095b): failed. A 5 s timeout; on it FRmMachineFailedMessage(FailReason)
// to the mission after 0.001 s, state 7.
bool FRmVignetteMachine::HandleFailed(SMEventData const* Event)
{
    FRmVignetteEntryCookies const& Cookies = RmVignetteEntryCookies();
    if (Event->is_sm_event() && Event->get_event_type() == 1)
    {
        StateMachineMessage Timeout(SMM_TIMEOUT);
        send_delayed_message_to_me(&Timeout, 5.0f, SMMS_Self, 0);
        return true;
    }
    if (RmMessageOf(this, Event, SMM_TIMEOUT) != nullptr)
    {
        FRmMachineFailedMessage Failed(FailReason);
        send_delayed_message(&Failed, 0.001f, static_cast<MessageHandler*>(Mission)->get_id());
        set_state(7);
        return true;
    }
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, Cookies.ReenterArea))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        return true;
    }
    return false;
}

// State 7 (06f9095e-06f90a32): done. Slot 7 on enter.
bool FRmVignetteMachine::HandleDone(SMEventData const* Event)
{
    FRmVignetteEntryCookies const& Cookies = RmVignetteEntryCookies();
    if (Event->is_sm_event() && Event->get_event_type() == 1)
    {
        Shutdown();
        return true;
    }
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, Cookies.ReenterArea))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        return true;
    }
    return false;
}
