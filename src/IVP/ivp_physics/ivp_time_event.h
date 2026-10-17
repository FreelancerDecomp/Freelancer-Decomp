// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstddef>
#include <cstdint>

// ============================================================================
// IVP_Time_Event -- the base of everything the environment's time manager
// (IVP_Sim_Unit_Manager, env+0x04) keeps in its event pool. Library name
// (Ipion); layout and slot read off the listing (cmn-phy5, 2026-10-08):
//   +0x00 vptr, slot 0 = simulate_time_event(IVP_Environment*), RET 4.
//         The time manager dispatches it as `MOV EAX,[ESI]; PUSH EBX; CALL
//         [EAX]` after removing the event from the pool (063606d0 :06360784-
//         :06360789, 063607c0 :0636087c-:06360888).
//   +0x04 the event's pool slot, 0xffff when not queued: the time manager
//         stores 0xffff there right after the removal (:06360724, :06360881)
//         and FUN_063609c0 stores the Insert result there (:063609e5).
// There is no virtual destructor: the PSI event's vtable 063a574c has one slot,
// and the time manager's destructor frees queued events with a bare operator
// delete (FUN_06360960). Derived classes that need one (IVP_Collision) declare
// it themselves.
// ============================================================================

class IVP_Environment;

class IVP_Time_Event
{
public:
    int32_t EventQueueHandle; // +0x04

    virtual void simulate_time_event(IVP_Environment* Environment) = 0;

protected:
    IVP_Time_Event() {}
    ~IVP_Time_Event() {}
};
