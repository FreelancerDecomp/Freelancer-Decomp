// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TU57 (06f70dd0-06f71fc0): trigger state machines and the machine group of the TU61
// random-mission runtime (docs/re/content.md, TU map, lane CNT-RM). Declarations in
// RandomMissionRuntime.h.

#include "ContentStaticIds.h"       // TU57's 16 static-init thunks 06f70dd0-06f70fb0

#include "RandomMissionRuntime.h"

#include "BehaviorCmds.h"           // FContentShipBase (FContentPlayerShip's base)
#include "ContentInternal.h"        // GetContentGameTime, GetContentTimeDistance
#include "ContentMessageRouter.h"   // GMessageRouter (DAT_06fcf254)
#include "ContentPlayerState.h"     // FContentPlayerState, FContentPlayerShip
#include "NpcBase.h"                // FContentSpaceObj::GetPosition
#include "SystemInfo.h"             // FSystemInfo::SystemId
#include "common_misc.h"            // SMM_TIMEOUT
#include "common_net.h"

#include <algorithm>

// LAB_06f70fd0: StateMachineMessage::GenerateUniqueMessageType inlined.
unsigned int const GRmMsgTriggerFired = StateMachineMessage::GenerateUniqueMessageType();

namespace
{
    // The (DeliveryState == -1 || == get_state()) test of 06f715f5-06f7160a.
    bool RmTriggerIsForCurrentState(StateMachine const* Machine, StateMachineMessage const* Message)
    {
        return Message->DeliveryState == -1 || Machine->get_state() == Message->DeliveryState;
    }

    // The inline squared distance of 06f71455-06f7147a / 06f717aa-06f717cd: the deltas
    // (position - centre) in x, y, z order, then (dz * dz + dx * dx) + dy * dy on the FPU.
    double RmTriggerDistanceSq(FContentPlayerState const* Player, Vector const& Center)
    {
        Vector Position;
        Player->ShipObject->GetPosition(&Position);
        double const Dx = static_cast<double>(Position.x) - static_cast<double>(Center.x);
        double const Dy = static_cast<double>(Position.y) - static_cast<double>(Center.y);
        double const Dz = static_cast<double>(Position.z) - static_cast<double>(Center.z);
        return Dz * Dz + Dx * Dx + Dy * Dy;
    }
}

// =======================================================================================
// FRmTriggerFiredMessage
// =======================================================================================

// Inline at 06f71283-06f712a5: StateMachineMessage(DAT_06fd3a18), then the three fields.
FRmTriggerFiredMessage::FRmTriggerFiredMessage(unsigned int InCookie, bool bInFinal,
                                               unsigned int InActivator)
    : StateMachineMessage(GRmMsgTriggerFired)
    , Cookie(InCookie)
    , bFinal(bInFinal)
    , Activator(InActivator)
{
}

// FUN_06f71840.
StateMachineMessage* FRmTriggerFiredMessage::clone() const
{
    return new FRmTriggerFiredMessage(*this);
}

// =======================================================================================
// FRmTrigger
// =======================================================================================

// FUN_06f70ff0 (RET 0x10). +0x30 is not written (only read after Fire stores it).
FRmTrigger::FRmTrigger(unsigned int InCookie, unsigned int InOwnerId, bool bInOneShot,
                       float InRearmDelay)
    : StateMachineMessageHandler(GMessageRouter)
    , OwnerId(InOwnerId)
    , bOneShot(bInOneShot)
    , RearmDelay(InRearmDelay)
    , Cookie(InCookie)
{
}

// LAB_06f71090 (RET 8): the state value picks the slot; the slot's result is dropped.
bool FRmTrigger::Slot2(SMEventData const* Event, int const& State)
{
    if (State < 0)
        return false;
    if (State == 0)
        OnIdle(Event, State);
    else if (State == 1)
        OnArmed(Event, State);
    else if (State == 2)
        OnFired(Event, State);
    else if (State == 3)
        OnRearmWait(Event, State);
    else if (State == 4)
        OnDone(Event, State);
    return false;
}

// FUN_06f71080 (RET 4; ICF with every other +0x20 setter).
void FRmTrigger::SetActivator(unsigned int InActivator)
{
    Activator = InActivator;
}

// LAB_06f71100 (RET 8): state 0 and an enter event (type < 4, == 1) -> state 1.
bool FRmTrigger::OnIdle(SMEventData const* Event, int const& State)
{
    if (State == 0 && Event->get_event_type() < 4 && Event->get_event_type() == 1)
    {
        set_state(1);
        return true;
    }
    return false;
}

// LAB_06f71140 (RET 8): state 2 and an enter event -> Fire (slot 10); one-shot -> state 4;
// otherwise RearmDelay > 0 -> state 3, else state 1 (FCOMP 0.0, TEST AH,0x41 + JNZ: less,
// equal or unordered -> state 1).
bool FRmTrigger::OnFired(SMEventData const* Event, int const& State)
{
    if (State != 2 || Event->get_event_type() >= 4 || Event->get_event_type() != 1)
        return false;
    Fire();
    if (bOneShot)
    {
        set_state(4);
        return true;
    }
    if (RearmDelay > 0.0f)
        set_state(3);
    else
        set_state(1);
    return true;
}

// FUN_06f711c0 (RET 8): state 3 and the 0x20 heartbeat (controller event) -> once the time
// since LastFireTime (FUN_06efec20 against FUN_06efec10) exceeds RearmDelay (as double;
// TEST AH,0x41 + JNZ: less, equal or unordered keep waiting) -> state 1. True for every
// heartbeat in state 3.
bool FRmTrigger::OnRearmWait(SMEventData const* Event, int const& State)
{
    // A type 4 event's int +0x08 is the controller message id, cast back after the type test.
    if (State != 3 || Event->get_event_type() != 4 ||
        static_cast<pub::Controller::EMessage>(
            static_cast<SMControllerEvent const*>(Event)->ControllerId) !=
            pub::Controller::EMessage::Heartbeat)
        return false;
    double const Now = GetContentGameTime();
    double const Delay = static_cast<double>(RearmDelay);
    if (GetContentTimeDistance(&Now, &LastFireTime) > Delay)
        set_state(1);
    return true;
}

// LAB_06f9a6f0 (ICF: XOR AL,AL; RET 8).
bool FRmTrigger::OnDone(SMEventData const* /*Event*/, int const& /*State*/)
{
    return false;
}

// LAB_06f71240: LastFireTime = the game time (FST); the message's final flag is bOneShot
// unless the time is the -1.0 sentinel (FCOMP DAT_06fb78f0, TEST AH,0x44 + JNP: equal ->
// 0); sent to OwnerId through the MessageHandler side's send_delayed_message with 0.01 s.
void FRmTrigger::Fire()
{
    LastFireTime = GetContentGameTime();
    bool bFinal = false;
    if (LastFireTime != -1.0 && bOneShot)
        bFinal = true;
    FRmTriggerFiredMessage Message(Cookie, bFinal, Activator);
    send_delayed_message(&Message, 0.01f, OwnerId);
}

// =======================================================================================
// FRmProximityTrigger
// =======================================================================================

// FUN_06f712e0 (RET 0x18).
FRmProximityTrigger::FRmProximityTrigger(unsigned int InCookie, unsigned int InOwnerId,
                                         unsigned int InSystemId, Vector const* InCenter,
                                         float InRadius,
                                         std::vector<FContentPlayerState*> const* InPlayers)
    : FRmTrigger(InCookie, InOwnerId, true, 1.0f)
    , Center(*InCenter)
    , RadiusSq(InRadius * InRadius)
    , SystemId(InSystemId)
    , Players(InPlayers)
{
}

// LAB_06f713f0 (RET 8): state 1 and the 0x20 heartbeat. The first player with a ship in
// SystemId (system record +0x48, not null-tested) whose squared distance is <= RadiusSq
// (TEST AH,0x41 + JNP: less or equal) becomes the activator (slot 4, its ship id) and the
// state goes to 2. True for every heartbeat in state 1.
bool FRmProximityTrigger::OnArmed(SMEventData const* Event, int const& State)
{
    // A type 4 event's int +0x08 is the controller message id, cast back after the type test.
    if (State != 1 || Event->get_event_type() != 4 ||
        static_cast<pub::Controller::EMessage>(
            static_cast<SMControllerEvent const*>(Event)->ControllerId) !=
            pub::Controller::EMessage::Heartbeat)
        return false;
    for (std::vector<FContentPlayerState*>::const_iterator It = Players->begin();
         It != Players->end(); ++It)
    {
        FContentPlayerState* const Player = *It;
        if (Player->ShipObject == nullptr || Player->GetSystemRecord()->SystemId != SystemId)
            continue;
        if (RmTriggerDistanceSq(Player, Center) <= static_cast<double>(RadiusSq))
        {
            SetActivator(Player->GetShipId());
            set_state(2);
            return true;
        }
    }
    return true;
}

// =======================================================================================
// FRmTimerTrigger
// =======================================================================================

// FUN_06f714d0 (RET 0xc).
FRmTimerTrigger::FRmTimerTrigger(unsigned int InCookie, unsigned int InOwnerId, float InDelay)
    : FRmTrigger(InCookie, InOwnerId, true, 1.0f)
    , Delay(InDelay)
{
}

// LAB_06f71560 (RET 8): state 1. Enter event (type < 4, == 1): an SMM_TIMEOUT message to
// itself after Delay (send_delayed_message_to_me, scope 1, target 0) -> true. Message
// event (type 3) carrying that timeout for this state -> state 2, true. Otherwise false.
bool FRmTimerTrigger::OnArmed(SMEventData const* Event, int const& State)
{
    if (State != 1)
        return false;
    if (Event->get_event_type() < 4 && Event->get_event_type() == 1)
    {
        StateMachineMessage Timeout(SMM_TIMEOUT);
        send_delayed_message_to_me(&Timeout, Delay, SMMS_Self, 0);
        return true;
    }
    if (Event->get_event_type() != 3)
        return false;
    StateMachineMessage const* Message = static_cast<SMMessage const*>(Event)->Message;
    if (RmTriggerIsForCurrentState(this, Message) && Message->MessageType == SMM_TIMEOUT)
    {
        set_state(2);
        return true;
    }
    return false;
}

// =======================================================================================
// FRmProximityLeaveTrigger
// =======================================================================================

// FUN_06f71660 (RET 0x18).
FRmProximityLeaveTrigger::FRmProximityLeaveTrigger(
    unsigned int InCookie, unsigned int InOwnerId, unsigned int InSystemId,
    Vector const* InCenter, float InRadius, std::vector<FContentPlayerState*> const* InPlayers)
    : FRmTrigger(InCookie, InOwnerId, true, 1.0f)
    , Center(*InCenter)
    , RadiusSq(InRadius * InRadius)
    , SystemId(InSystemId)
    , Players(InPlayers)
{
}

// LAB_06f71740 (RET 8): state 1 and the 0x20 heartbeat. A player ship of SystemId within
// RadiusSq (FCOM, TEST AH,0x41 + JNP: less or equal) -> true at once, nothing changes.
// Otherwise every such ship closer than DAT_06fb781c (1e10; FCOMP, TEST AH,0x05 + JP: only
// "less" falls through) is remembered, the last one becomes the activator (slot 4) when
// there is one, and the state goes to 2.
bool FRmProximityLeaveTrigger::OnArmed(SMEventData const* Event, int const& State)
{
    // A type 4 event's int +0x08 is the controller message id, cast back after the type test.
    if (State != 1 || Event->get_event_type() != 4 ||
        static_cast<pub::Controller::EMessage>(
            static_cast<SMControllerEvent const*>(Event)->ControllerId) !=
            pub::Controller::EMessage::Heartbeat)
        return false;
    FContentPlayerState* Last = nullptr;
    for (std::vector<FContentPlayerState*>::const_iterator It = Players->begin();
         It != Players->end(); ++It)
    {
        FContentPlayerState* const Player = *It;
        if (Player->ShipObject == nullptr || Player->GetSystemRecord()->SystemId != SystemId)
            continue;
        double const DistanceSq = RmTriggerDistanceSq(Player, Center);
        if (DistanceSq <= static_cast<double>(RadiusSq))
            return true;
        if (DistanceSq < 1.0e10)
            Last = Player;
    }
    if (Last != nullptr)
        SetActivator(Last->GetShipId());
    set_state(2);
    return true;
}

// =======================================================================================
// FRmMachineGroup
// =======================================================================================

// FUN_06f718b0 (RET 4).
FRmMachineGroup::FRmMachineGroup(IMessageRouter* InRouter)
    : StateMachineGroup(InRouter)
{
}

// FUN_06f71920: the front id is removed (slot 5 of the group's own table) and erased until
// each vector is empty; the buffers are freed; ~StateMachineGroup.
FRmMachineGroup::~FRmMachineGroup()
{
    while (!Machines.empty())
    {
        remove_state_machine(Machines.front());
        Machines.erase(Machines.begin());
    }
    while (!StateMachines.empty())
    {
        remove_state_machine(StateMachines.front());
        StateMachines.erase(StateMachines.begin());
    }
}

// FUN_06f71bd0 (RET 4): slot 4 add_state_machine, then vector insert(end, 1, &id).
void FRmMachineGroup::AddStateMachine(StateMachineMessageHandler* Machine)
{
    add_state_machine(Machine);
    unsigned int const Id = static_cast<MessageHandler*>(Machine)->get_id();
    StateMachines.insert(StateMachines.end(), 1, Id);
}

// FUN_06f71da0 (RET 4).
void FRmMachineGroup::AddMachine(StateMachineMessageHandler* Machine)
{
    add_state_machine(Machine);
    unsigned int const Id = static_cast<MessageHandler*>(Machine)->get_id();
    Machines.insert(Machines.end(), 1, Id);
}

// FUN_06f71af0 (RET 4).
void FRmMachineGroup::RemoveMachine(unsigned int Id)
{
    remove_state_machine(Id);
    std::vector<unsigned int>::iterator const NewEnd =
        std::remove(StateMachines.begin(), StateMachines.end(), Id);
    if (NewEnd != StateMachines.end())
    {
        StateMachines.erase(NewEnd, StateMachines.end());
        return;
    }
    Machines.erase(std::remove(Machines.begin(), Machines.end(), Id), Machines.end());
}

// LAB_06f71a30. The events are stack SMEventData (Content's copy of the table 06fb7620).
void FRmMachineGroup::handle_state_change()
{
    while (bPendingChange)
    {
        SMEventData Exit(static_cast<STATE_MACHINE_EVENT_TYPE>(2));
        Slot2(&Exit, CurrentState);
        while (!StateMachines.empty())
        {
            remove_state_machine(StateMachines.front());
            StateMachines.erase(StateMachines.begin());
        }
        CurrentState = PendingState;
        bPendingChange = 0;
        SMEventData Enter(static_cast<STATE_MACHINE_EVENT_TYPE>(1));
        Slot2(&Enter, CurrentState);
    }
}
