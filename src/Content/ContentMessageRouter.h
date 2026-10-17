// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU36 (06f056d0-06f05d00): the StateMachine message router that every
// Content MessageHandler / StateMachineMessageHandler is bound to. GameSupport creates
// the single instance (FUN_06efeb10: operator new(0x24) + FUN_06f058d0) and deletes it
// in its dtor (FUN_06efebb0: DAT_06fcf254->vtbl[+0x10](1)).

#include "common_net.h"
#include "common_server_controller.h"

#include <list>

class FContentMessageRouter;

// Retail DAT_06fcf254: the live router, or null.
extern FContentMessageRouter* GMessageRouter;

// Retail abstract base: vtable 06fb80d4 holds only the four IMessageRouter slots (all
// purecall). Its ctor (inlined at the top of FUN_06f058d0) publishes the object in
// DAT_06fcf254; its dtor FUN_06f05cf0 (also run from the ctor's unwind funclets
// 06facd16 / 06facd52) clears it.
class FContentMessageRouterBase : public IMessageRouter
{
protected:
    FContentMessageRouterBase();
    ~FContentMessageRouterBase();
};

// Retail object: 0x24 bytes, vtable 06fb80bc (6 slots).
//   +0x04 pending delayed messages (VC6 list: proxy +4, head +8, size +0xc), ordered by
//         delivery time
//   +0x10 timer id (DAT_06fce76c++ at construction)
//   +0x14 last handler id handed out
//   +0x18 registered handlers (VC6 list: proxy +0x18, head +0x1c, size +0x20)
class FContentMessageRouter : public FContentMessageRouterBase
{
public:
    // FUN_06f058d0.
    FContentMessageRouter();

    // Slot 0, LAB_06f05a70 (RET 0x14).
    void RouteMessage(StateMachineMessage* Msg, float Delay, unsigned int Target,
                      unsigned int Sender, int Scope) override;
    // Slot 1, FUN_06f05c00 (RET 4).
    unsigned int RegisterHandler(MessageHandler* Handler) override;
    // Slot 2, FUN_06f05c50 (RET 4).
    void UnregisterHandler(MessageHandler* Handler) override;
    // Slot 3, FUN_06f05ca0 (RET 4).
    MessageHandler* LookupHandlerById(unsigned int Id) override;
    // Slot 4, FUN_06f05970 scalar deleting dtor -> FUN_06f05990.
    virtual ~FContentMessageRouter();
    // Slot 5, LAB_06f05b90 (RET 4): SpaceSupport forwards its timer message here
    // (06f5d74f), for the timers RouteMessage sets on DAT_06fd1c74.
    virtual void OnTimerExpired(pub::Controller::TimerExpired const* Timer);

private:
    std::list<StateMachineMessage*> PendingMessages;   // +0x04
    unsigned int TimerId;                               // +0x10
    unsigned int LastHandlerId;                         // +0x14
    std::list<MessageHandler*> Handlers;                // +0x18
};
