// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_server_controller.h"

// Content.dll — shared Server controller interface (docs/re/content.md).
// Not a DACOM component: no RegisterComponent, no 1.11_* IID.

struct IDLL
{
    IDLL();
    IDLL(IDLL const& Other);
    IDLL& operator=(IDLL const& Other);

    // Slot 0 — message handler (retail thiscall, RET 8). Slot 1 — dtor.
    virtual void HandleMessage(pub::Controller::EMessage MessageId, void const* Parms) = 0;
    virtual ~IDLL();
};

// Story's one-slot secondary facet, passed to mission controllers by their
// CreateParms. Retail Story+4 uses vtable 06fb4270 -> LAB_06ea6280.
struct IStoryMissionEvents
{
    virtual void OnMissionEvent(unsigned int EventKind, unsigned int Chapter,
                                unsigned int ControllerId) = 0;
};

// FUN_06ea6ba0 builds this polymorphic parameter object on the stack.
// FUN_06eb75a0 reads PlayerId at +4 and StoryEvents at +8 before returning.
struct FStoryMissionCreateParms final : pub::Controller::CreateParms
{
    unsigned int PlayerId;
    IStoryMissionEvents* StoryEvents;
};

extern "C" IDLL* __cdecl CreateInstance(unsigned int ControllerId, char const* Name,
                                        void const* Parms);
extern "C" void __cdecl DestroyInstance(IDLL* Instance);
