// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll state machines: SMEventData and its events, StateMachineMessage / SMMChangeState,
// StateMachine, MessageHandler, StateMachineMessageHandler and StateMachineGroup.
// Split out of common_net.cpp.

#include "common_net.h"
#include "common_misc.h" // SMM_CHANGE_STATE (SMMChangeState's message-type id)

#include <cstdlib>
#include <cstring>
#include <new>

// ===========================================================================
// SMEventData
// ===========================================================================

SMEventData::SMEventData(STATE_MACHINE_EVENT_TYPE Type)
    : EventType(Type)
{
}

STATE_MACHINE_EVENT_TYPE SMEventData::get_event_type() const
{
    // 0x062d1ad0 (ICF-shared): MOV EAX,[this+4]; RET.
    return EventType;
}

bool SMEventData::is_sm_event() const
{
    // 0x06333060: CALL [vtbl+0] (get_event_type), then SETL on `< 4`.
    return get_event_type() < 4;
}

bool SMEventData::is_user_event() const
{
    // 0x06333070: CALL [vtbl+0] (get_event_type), then SETGE on `>= 4`.
    return get_event_type() >= 4;
}

// ===========================================================================
// SMControllerEvent
// ===========================================================================

SMControllerEvent::SMControllerEvent(int Id, const void* Data)
    : SMEventData(SMET_Controller)
    , ControllerId(Id)
    , DataPtr(Data)
{
}

// ===========================================================================
// SMMessage
// ===========================================================================

SMMessage::SMMessage(StateMachineMessage* Msg)
    : SMEventData(SMET_UserEvent)
    , Message(Msg)
{
}

// ===========================================================================
// StateMachineMessage
// ===========================================================================

unsigned int StateMachineMessage::s_unique_message_type_generator = 0;

StateMachineMessage::StateMachineMessage()
    : MessageType(0)
    , Sender(0)
    , Receiver(0)
    , DeliveryState(-1)
    , DeliveryTime(0.0f)
    , bDelivered(0)
{
}

StateMachineMessage::StateMachineMessage(unsigned int Type)
    : MessageType(Type)
    , Sender(0)
    , Receiver(0)
    , DeliveryState(-1)
    , DeliveryTime(0.0f)
    , bDelivered(0)
{
}

StateMachineMessage::StateMachineMessage(float Time, unsigned int Type, unsigned int InSender,
                                         unsigned int InReceiver, int State)
    : MessageType(Type)
    , Sender(InSender)
    , Receiver(InReceiver)
    , DeliveryState(State)
    , DeliveryTime(Time)
    , bDelivered(0)
{
    // Common.dll.txt 0xd32f0. See the class-comment on this overload's declaration
    // (common_net.h) for the bug this fixes: field order was previously shifted by one and a
    // nonexistent "Delivered" bool param was invented. Retail hardcodes bDelivered=0 here.
}

StateMachineMessage::StateMachineMessage(const StateMachineMessage& Other)
    : MessageType(Other.MessageType)
    , Sender(Other.Sender)
    , Receiver(Other.Receiver)
    , DeliveryState(Other.DeliveryState)
    , DeliveryTime(Other.DeliveryTime)
    , bDelivered(Other.bDelivered)
{
}

StateMachineMessage::~StateMachineMessage()
{
}

StateMachineMessage* StateMachineMessage::clone() const
{
    // 0xd2ef0: `new StateMachineMessage(*this)` -- the class operator new (062fd560, JMP
    // malloc) inlined as malloc(0x1c), the VC6 new-expression null check (06332f00), then
    // the inlined copy ctor (field copies, vtable store at 06332f26).
    return new StateMachineMessage(*this);
}

void StateMachineMessage::set_delivery_options(float Time, unsigned int InSender,
                                              unsigned int InReceiver, int State)
{
    DeliveryTime  = Time;
    Sender        = InSender;
    Receiver      = InReceiver;
    DeliveryState = State;
    bDelivered    = 0;
}

void* StateMachineMessage::operator new(size_t Size) noexcept
{
    // 0x062fd560: JMP msvcrt malloc
    return malloc(Size);
}

void StateMachineMessage::operator delete(void* Ptr)
{
    // 0x062fd570: JMP msvcrt free
    free(Ptr);
}

unsigned int StateMachineMessage::get_message_type() const
{
    // 0x062d1ad0: MOV EAX,[this+4]
    return MessageType;
}

void StateMachineMessage::set_message_type(unsigned int Type)
{
    // 0x06309b30: [this+4] = arg
    MessageType = Type;
}

unsigned int StateMachineMessage::get_sender() const
{
    // 0x062a66a0: MOV EAX,[this+8]
    return Sender;
}

void StateMachineMessage::set_sender(unsigned int InSender)
{
    // 0x0631d940: [this+8] = arg
    Sender = InSender;
}

unsigned int StateMachineMessage::get_receiver() const
{
    // 0x0629fb90: MOV EAX,[this+0xc]
    return Receiver;
}

void StateMachineMessage::set_receiver(unsigned int InReceiver)
{
    // 0x06332f40: [this+0xc] = arg
    Receiver = InReceiver;
}

int StateMachineMessage::get_delivery_state() const
{
    // 0x06263a70: MOV EAX,[this+0x10]
    return DeliveryState;
}

void StateMachineMessage::set_delivery_state(int State)
{
    // 0x06332f50: [this+0x10] = arg
    DeliveryState = State;
}

float StateMachineMessage::get_delivery_time() const
{
    // 0x06332f60: FLD [this+0x14]
    return DeliveryTime;
}

void StateMachineMessage::set_delivery_time(float Time)
{
    // 0x0633bad0: [this+0x14] = arg (raw dword copy)
    DeliveryTime = Time;
}

bool StateMachineMessage::is_delivered() const
{
    // 0x06263bc0: MOV AL,byte [this+0x18]
    return bDelivered != 0;
}

void StateMachineMessage::set_delivered(bool bInDelivered)
{
    // 0x06263bd0: byte [this+0x18] = arg
    bDelivered = bInDelivered ? 1 : 0;
}

unsigned int StateMachineMessage::GenerateUniqueMessageType()
{
    s_unique_message_type_generator++;
    return s_unique_message_type_generator;
}

// ===========================================================================
// SMMChangeState
// ===========================================================================

SMMChangeState::SMMChangeState(int NewStateVal)
    : StateMachineMessage()
    , NewState(NewStateVal)
{
    // 0x06332fa6: retail LOADS the global .data slot (`MOV ECX,[.data:SMM_CHANGE_STATE]`)
    // and stores it into +0x04 -- it does not embed a literal here. Previously this file
    // kept its own `static const unsigned kSmmChangeStateType = 1` copy, which happened to
    // match but would silently drift from SMM_CHANGE_STATE (see common_misc.cpp, where the
    // id is now handed out by GenerateUniqueMessageType at static-init time exactly as in
    // retail). Reading the real global instead keeps the two in lockstep.
    MessageType = SMM_CHANGE_STATE;
}

StateMachineMessage* SMMChangeState::clone() const
{
    // 0xd2fd0: `new SMMChangeState(*this)` -- inherited StateMachineMessage::operator new
    // inlined as malloc(0x20), null check (06332fe0), then the inlined copy ctor (base
    // fields, NewState, SMMChangeState vtable).
    return new SMMChangeState(*this);
}

// ===========================================================================
// StateMachine
// ===========================================================================

StateMachine::StateMachine()
    : CurrentState(0)
    , PendingState(0)
    , bPendingChange(0)
    , bProcessing(0)
{
}

StateMachine::~StateMachine()
{
}

int StateMachine::get_state() const
{
    // 0x062d1ad0 (ICF-shared): MOV EAX,[this+4]
    return CurrentState;
}

void StateMachine::start()
{
    // Retail 0xd3230: builds a temporary SMEventData{EventType=SMET_Tick} on the
    // stack and calls this->vtable[1](&event) — i.e. this->Process(&event).
    // (Confirmed via StateMachine's vtable dump: slot0=dtor thunk, slot1=process,
    // slot2=pure-virtual purecall stub, slot3=handle_state_change.)
    SMEventData TickEvent(SMET_Tick);
    process(&TickEvent);
}

void StateMachine::reset()
{
    CurrentState = 0;
    PendingState = 0;
}

void StateMachine::set_state(int NewState)
{
    if (NewState != CurrentState)
    {
        PendingState = NewState;
        bPendingChange = 1;
    }
}

// .rdata:063a4978 -- the -1 "any state" value process() offers slot 2 when the current state
// did not take the event.
static const int GStateMachineAnyState = -1;

void StateMachine::process(const SMEventData* Event)
{
    // 0x063339c0: byte[+0xd]=1; if (!vtbl[+8](Event, &[+4])) vtbl[+8](Event, &063a4978);
    // vtbl[+0xc](); byte[+0xd]=0.
    bProcessing = 1;
    if (!Slot2(Event, CurrentState))
        Slot2(Event, GStateMachineAnyState);
    handle_state_change();
    bProcessing = 0;
}

void StateMachine::handle_state_change()
{
    // 0x06333a00: while the change flag is set, offer slot 2 a stack SMEventData of type 2
    // against the old state, commit PendingState, clear the flag, then offer type 1 against
    // the new state. A slot-2 handler that calls set_state again re-arms the loop.
    while (bPendingChange != 0)
    {
        SMEventData Event(SMET_Message);
        Slot2(&Event, CurrentState);
        CurrentState = PendingState;
        bPendingChange = 0;
        Event.EventType = SMET_Tick;
        Slot2(&Event, CurrentState);
    }
}

// ===========================================================================
// MessageHandler
// ===========================================================================

MessageHandler::MessageHandler(IMessageRouter* InRouter)
    : Router(InRouter)
{
    // 0x063333a0: [+4] = Router; [+8] = Router->vtable[1](this). No null check in retail.
    HandlerId = Router->RegisterHandler(this);
}

MessageHandler::~MessageHandler()
{
    // 0x063333f0: Router->vtable[2](this). No null check in retail.
    Router->UnregisterHandler(this);
}

unsigned int MessageHandler::get_id() const
{
    // 0x062a66a0 (ICF-shared): MOV EAX,[this+8]
    return HandlerId;
}

void MessageHandler::send_immediate_message(StateMachineMessage* Msg, unsigned int Target)
{
    // 0x06333410: Router->vtable[0](Msg, 0, Target, HandlerId, -1)
    Router->RouteMessage(Msg, 0.0f, Target, HandlerId, -1);
}

void MessageHandler::send_delayed_message(StateMachineMessage* Msg, float Delay, unsigned int Target)
{
    // 0x06333430: Router->vtable[0](Msg, Delay, Target, HandlerId, -1)
    Router->RouteMessage(Msg, Delay, Target, HandlerId, -1);
}

void MessageHandler::send_delayed_message_to_me(StateMachineMessage* Msg, float Delay, int Scope)
{
    // 0x06333460: Router->vtable[0](Msg, Delay, HandlerId, HandlerId, Scope)
    Router->RouteMessage(Msg, Delay, HandlerId, HandlerId, Scope);
}

// ===========================================================================
// StateMachineMessageHandler
// ===========================================================================

StateMachineMessageHandler::StateMachineMessageHandler(IMessageRouter* InRouter)
    : StateMachine()
    , MessageHandler(InRouter)
{
    // RE'd from Ghidra @ 0xd3080: StateMachine() default-constructs at offset 0,
    // then MessageHandler(InRouter) constructs at offset +0x10 and registers
    // *that* sub-object pointer (this+0x10) as the handler — MessageHandler's
    // own ctor already does Router->RegisterHandler(this), which is exactly
    // this+0x10 here. No extra fields or manual registration needed.
}

void StateMachineMessageHandler::receive_message(StateMachineMessage* Msg)
{
    // RE'd from Ghidra @ 0xd3480: builds a stack-local SMMessage(EventType=
    // UserEvent) wrapping Msg and calls this->vtable[1](&event) — i.e. the
    // StateMachine sub-object's Process(), reached via ordinary virtual
    // dispatch (StateMachineMessageHandler does not override Process itself;
    // StateMachine, or a derived override such as StateMachineGroup::Process,
    // handles it). Matches retail exactly, including no null check on Msg.
    SMMessage Event(Msg);
    process(&Event);
}

void StateMachineMessageHandler::send_delayed_message_to_me(
    StateMachineMessage* Msg, float Delay, SM_MESSAGE_SCOPE Scope, int TargetId)
{
    // RE'd from Ghidra @ 0xd34b0: dispatches through the inherited MessageHandler's
    // Router/HandlerId (at +0x14/+0x18 via the MessageHandler base). 0x063334b0 has no
    // null check on the router; a scope outside 0..2 does nothing.

    switch (Scope)
    {
    case SMMS_Self:
        // Retail: Router->RouteMessage(Msg, Delay, HandlerId, HandlerId, CurrentState)
        // — the last argument is the StateMachine base's CurrentState (this+4),
        // not a separate field.
        Router->RouteMessage(Msg, Delay, HandlerId, HandlerId, CurrentState);
        break;
    case SMMS_Broadcast:
        Router->RouteMessage(Msg, Delay, HandlerId, HandlerId, -1);
        break;
    case SMMS_Target:
        Router->RouteMessage(Msg, Delay, HandlerId, HandlerId, TargetId);
        break;
    }
}

// ===========================================================================
// StateMachineGroup
// ===========================================================================

StateMachineGroup::StateMachineGroup(IMessageRouter* InRouter)
    : StateMachineMessageHandler(InRouter)
{
    // 0x06333530: the StateMachineMessageHandler base (MessageHandler at +0x10 registers
    // itself with InRouter), then two empty std::lists (operator new(0xc) head node each).
    // The bytes the old port read as "GroupId = 1" / "Field_0x28 = 2" are the lists'
    // allocator bytes, copied from stack slots; the 1 and 2 are the EH state counter.
}

StateMachineGroup::~StateMachineGroup()
{
    // 0x06333610: front to back, delete each machine (null-tested scalar deleting dtor,
    // CALL [vtbl+0] with 1) and erase its node. Then the members are destroyed in reverse
    // order -- the removal list (its iterators are dead, nothing is dereferenced), then the
    // now-empty machine list -- and the MessageHandler base unregisters from the router.
    FMachineList::iterator It = Machines.begin();
    while (It != Machines.end())
    {
        delete *It;
        It = Machines.erase(It);
    }
}

void StateMachineGroup::process(const SMEventData* Event)
{
    // 0x06333840. 06333840-06333877 is StateMachine::process inlined (slot-2 offer with
    // the -1 fallback, the virtual handle_state_change), clearing bProcessing before the
    // first dying-machine pass. 063338c1: the event type comes through the virtual
    // get_event_type(); 3 ends the call. Otherwise every machine processes the event and a
    // second dying-machine pass follows. No null checks on the event or the machines.
    bProcessing = 1;
    if (!Slot2(Event, CurrentState))
        Slot2(Event, GStateMachineAnyState);
    handle_state_change();
    bProcessing = 0;

    remove_dying_machines();

    if (Event->get_event_type() == SMET_UserEvent)
        return;

    // 063338cc: front to back, CALL [vtbl+4] (process) on every machine.
    for (FMachineList::iterator It = Machines.begin(); It != Machines.end(); ++It)
        (*It)->process(Event);

    remove_dying_machines();
}

void StateMachineGroup::add_state_machine(StateMachineMessageHandler* Handler)
{
    // 0x06333730: push_back (node linked before the head), then a stack SMEventData of
    // type 1 through Handler's CALL [vtbl+4] (process). No null test of Handler.
    Machines.push_back(Handler);
    SMEventData TickEvent(SMET_Tick);
    Handler->process(&TickEvent);
}

void StateMachineGroup::remove_state_machine(const StateMachineMessageHandler* Handler)
{
    // 0x063337a0: find the first machine equal to Handler (front to back) and push_back
    // its iterator onto the removal list. Nothing is unlinked or deleted here: that waits
    // for remove_dying_machines, so a marked machine can still run in this tick's pass.
    for (FMachineList::iterator It = Machines.begin(); It != Machines.end(); ++It)
    {
        if (*It == Handler)
        {
            DyingMachines.push_back(It);
            return;
        }
    }
}

void StateMachineGroup::remove_state_machine(unsigned int Id)
{
    // 0x06333800: the router returns the registered MessageHandler sub-object (+0x10);
    // the down-cast's null-preserving -0x10 adjustment is retail's `ADD EAX,-0x10` / `PUSH 0`
    // pair, and both branches make the virtual call (CALL [vtbl+0x18]), null included.
    // No null test of the router.
    MessageHandler* const Registered = Router->LookupHandlerById(Id);
    remove_state_machine(static_cast<const StateMachineMessageHandler*>(Registered));
}

void StateMachineGroup::remove_dying_machines()
{
    // 0x06333950, and the same steps inlined twice in process (063338f0 and 06333874,
    // the latter through std::list::erase calls): front to back over the removal list,
    // delete the marked machine (null-tested scalar deleting dtor), erase its node from the
    // machine list, then erase the removal entry.
    std::list<FMachineList::iterator>::iterator It = DyingMachines.begin();
    while (It != DyingMachines.end())
    {
        delete **It;
        Machines.erase(*It);
        It = DyingMachines.erase(It);
    }
}
