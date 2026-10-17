// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Event::Handler (RTTI .?AVHandler@Event@@, Client\Interface\KeyMap.cpp) -- the face a
// class inherits to receive KeyMap command ids through the DAT_0067c3a4 registry
// (hud_command_dispatch.h). Base vftable 0x005cbed8 has a single slot (_purecall): the event
// callback, thiscall RET 4, which FUN_00576410 calls as vt[0](MsgId) and stops at the first
// true. The destructor is not virtual: FUN_00576010 is called directly by every derived dtor
// (e.g. TypeSelect 0x0056624f, UIPage::MasterWin, the main command handler atexit 005b2550).
//
// The face sits wherever the derived class puts it (TypeSelect +0x7c, MasterWin +0x80); the
// registry stores the face's address, so AsHudCmdHandler() is `this` of the face.

#include "hud_command_dispatch.h"

class EventHandler
{
public:
    // slot 0 -- handle MsgId; true consumes it.
    virtual bool OnEvent(unsigned MsgId) = 0;

    FHudCmdHandler* AsHudCmdHandler()
    {
        return this;
    }

    // FUN_005760d0 (thiscall, RET 4) -- add (MsgId, this) to the registry.
    void RegisterEvent(unsigned MsgId)
    {
        HudRegisterCommandHandler(AsHudCmdHandler(), MsgId);
    }

    // FUN_005760f0 (thiscall, RET 4) -- drop this handler's entries for MsgId.
    void UnregisterEvent(unsigned MsgId)
    {
        HudUnregisterCommand(AsHudCmdHandler(), MsgId);
    }

    // FUN_00576190 (thiscall) -- drop every entry of this handler.
    void UnregisterAllEvents()
    {
        HudUnregisterCommandHandler(AsHudCmdHandler());
    }

protected:
    EventHandler()
    {
    }

    // FUN_00576010 -- vftable back to 0x005cbed8, FUN_00577c30(this), erase every registration
    // (freeing an emptied tree), FUN_005762f0(this, 0).
    ~EventHandler()
    {
        HudDestroyEventHandler(AsHudCmdHandler());
    }

private:
    EventHandler(EventHandler const&) = delete;
    EventHandler& operator=(EventHandler const&) = delete;
};
