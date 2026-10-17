// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The "static" in-flight camera -- the one a cutscene parks somewhere and leaves. Its vtable is
// .rdata 0x005e0594 and it sits at controller + 0x9a4.
//
// It is the smallest class in the whole hierarchy: the controller ctor builds it by calling the
// FGameCamera ctor (FUN_0051b2f0) and then overwriting the vptr (0x005493c6), with NOT ONE field
// of its own. That is why it is exactly 0xf8 bytes -- controller + 0xa9c, where the docking
// camera starts, is 0xf8 past it. So the class is pure behaviour: three overridden slots and
// nothing else.
//
//   0x64  LAB_0051e920  activate:   base activate, bars in, tell the ship the view is cinematic
//   0x68  LAB_0051e980  deactivate: the mirror image, bars out
//   0x74  LAB_0051e9d0  event:      event 15 hands the view back to the player
//
// Everything else, including the per-frame update, is FGameCamera's -- which is the point: a
// static camera does not move.

#include "space_camera_game.h"

struct FStaticCamera : FGameCamera
{
    bool Activate(int const* ViewRect4) override;
    void Deactivate() override;
    int HandleCommand74(void* Command) override;

};

static_assert(sizeof(FStaticCamera) == 0x0f8, "FStaticCamera adds nothing to FGameCamera");

// The controller ctor's step for this slot: FUN_0051b2f0 then the vptr, nothing more.
void StaticCameraConstruct(FStaticCamera* Cam);

// Slot 0x64 (LAB_0051e920). Takes the viewport, exactly like every other activate slot.
bool StaticCameraActivate(FStaticCamera* Cam, const int* ViewRect4);

// Slot 0x68 (LAB_0051e980).
void StaticCameraDeactivate(FStaticCamera* Cam);

// Slot 0x74 (LAB_0051e9d0). Retail's argument is a pointer whose first dword is an event code;
// the only code this camera reacts to is 15, and it reacts by asking for the "player" mode.
// Always returns 0.
int StaticCameraOnEvent(FStaticCamera* Cam, const int* Event);

// The event code above, named because it is the only thing that gets the view back.
enum { kStaticCameraReleaseEvent = 15 };

inline FSpaceCamera* SpaceCameraOf(FStaticCamera* Cam) { return Cam; }
