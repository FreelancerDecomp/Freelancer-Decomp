// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU36 (06f056d0-06f05d00): the StateMachine message router. See
// ContentMessageRouter.h.

#include "ContentMessageRouter.h"
#include "ContentInternal.h"
#include "ContentStaticIds.h" // TU36 static IDs 06f056d0..06f058b0 (DAT_06fd0638..DAT_06fd05fc)

FContentMessageRouter* GMessageRouter = nullptr;

FContentMessageRouterBase::FContentMessageRouterBase()
{
    // 06f058ed-06f058f8: base vptr 06fb80d4, then DAT_06fcf254 = this.
    GMessageRouter = static_cast<FContentMessageRouter*>(this);
}

// FUN_06f05cf0: DAT_06fcf254 = 0.
FContentMessageRouterBase::~FContentMessageRouterBase()
{
    GMessageRouter = nullptr;
}

// FUN_06f058d0 (06f058d0-06f05968): both lists get an empty sentinel, TimerId takes the
// next value of DAT_06fce76c (post-increment), LastHandlerId = 0; vptr 06fb80bc last.
FContentMessageRouter::FContentMessageRouter()
    : TimerId(GContentNextUniqueId++)
    , LastHandlerId(0)
{
}

// FUN_06f05990 (06f05990-06f05a6f): delete every pending message through its virtual
// dtor (vtbl[0](1)), then the handler list is cleared and freed, then the message list;
// the base dtor clears DAT_06fcf254. The handlers themselves are not deleted.
FContentMessageRouter::~FContentMessageRouter()
{
    std::list<StateMachineMessage*>::iterator It = PendingMessages.begin();
    for (; It != PendingMessages.end(); ++It)
    {
        StateMachineMessage* Pending = *It;
        if (Pending != nullptr)
            delete Pending;
    }
}

// LAB_06f05a70 (06f05a70-06f05b85).
// Delay <= 0.0f (DAT_06fb444c; FCOMP + TEST AH,0x41 + JP: parity odd = less/equal falls
// through): deliver now. Otherwise (greater or unordered) queue a clone and arm a timer.
void FContentMessageRouter::RouteMessage(StateMachineMessage* Msg, float Delay,
                                         unsigned int Target, unsigned int Sender, int Scope)
{
    if (Delay <= 0.0f)
    {
        // 06f05a88-06f05ac5: set_delivery_options(0, Sender, Target, Scope), then
        // this->vtbl[+0xc](Msg->Receiver); on a hit bDelivered = 1 and
        // handler->vtbl[+4](Msg) (receive_message).
        Msg->set_delivery_options(0.0f, Sender, Target, Scope);
        MessageHandler* Handler = LookupHandlerById(Msg->Receiver);
        if (Handler == nullptr)
            return;
        Msg->bDelivered = 1;
        Handler->receive_message(Msg);
        return;
    }

    // 06f05acd-06f05add: FUN_06efec10() (double) + Delay, stored as float.
    float DeliveryTime = static_cast<float>(GetContentGameTime() + Delay);

    // 06f05ad6-06f05afc: first queued message whose time is greater (FCOMP + TEST
    // AH,0x41 + JZ = greater) -- the insert position; the sentinel if none.
    std::list<StateMachineMessage*>::iterator Where = PendingMessages.begin();
    for (; Where != PendingMessages.end(); ++Where)
    {
        if ((*Where)->DeliveryTime > DeliveryTime)
            break;
    }

    // 06f05afe-06f05b4e: Msg->vtbl[+4]() (clone), set_delivery_options on the clone,
    // insert before Where, then ++size (+0xc).
    StateMachineMessage* Copy = Msg->clone();
    Copy->set_delivery_options(DeliveryTime, Sender, Target, Scope);
    PendingMessages.insert(Where, Copy);

    // 06f05b50-06f05b75: pub::SetTimer(&DAT_06fd1c74, {TimerId, 0}, Delay).
    pub::Controller::TimerExpired Timer;
    Timer.Param1 = TimerId;
    Timer.Param2 = 0;
    pub::SetTimer(GSpaceSupportControllerId, Timer, Delay);
}

// FUN_06f05c00 (06f05c00-06f05c42): append, ++size (+0x20), return ++LastHandlerId.
unsigned int FContentMessageRouter::RegisterHandler(MessageHandler* Handler)
{
    Handlers.push_back(Handler);
    ++LastHandlerId;
    return LastHandlerId;
}

// FUN_06f05c50 (06f05c50-06f05c8f): erase the first node holding Handler, if any.
void FContentMessageRouter::UnregisterHandler(MessageHandler* Handler)
{
    std::list<MessageHandler*>::iterator It = Handlers.begin();
    for (; It != Handlers.end(); ++It)
    {
        if (*It == Handler)
        {
            Handlers.erase(It);
            return;
        }
    }
}

// FUN_06f05ca0 (06f05ca0-06f05ce0): first handler whose MessageHandler::get_id() == Id,
// else 0.
MessageHandler* FContentMessageRouter::LookupHandlerById(unsigned int Id)
{
    std::list<MessageHandler*>::iterator It = Handlers.begin();
    for (; It != Handlers.end(); ++It)
    {
        if ((*It)->get_id() == Id)
            return *It;
    }
    return nullptr;
}

// LAB_06f05b90 (06f05b90-06f05bf2). Only the timer armed by this router
// (Timer->Param1 == TimerId) is handled: the earliest queued message is delivered like
// an immediate one, deleted through its virtual dtor, and its node erased (--size).
// Retail takes the first node without an emptiness test: every queued message armed
// exactly one timer.
void FContentMessageRouter::OnTimerExpired(pub::Controller::TimerExpired const* Timer)
{
    if (Timer->Param1 != TimerId)
        return;

    std::list<StateMachineMessage*>::iterator First = PendingMessages.begin();
    StateMachineMessage* Msg = *First;
    MessageHandler* Handler = LookupHandlerById(Msg->Receiver);
    if (Handler != nullptr)
    {
        Msg->bDelivered = 1;
        Handler->receive_message(Msg);
    }
    if (Msg != nullptr)
        delete Msg;
    PendingMessages.erase(First);
}
