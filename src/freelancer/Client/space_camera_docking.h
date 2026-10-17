// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The docking camera -- the fixed exterior shot that watches the player's ship fly into a dock
// ring or a base mooring point. Vtable .rdata 0x005dc2d4, ctor FUN_0051ea20, controller + 0xa9c.
//
// It is the only camera in the hierarchy that never moves and yet updates every frame: it holds
// its position and re-aims at the player's ship, which is exactly what a docking shot looks
// like. Four slots are its own:
//
//   0x58  FUN_0051eaa0  attach:     runs two subsystem calls first, then the usual watcher swap
//   0x64  LAB_0051ea50  activate:   base activate, bars in, clear the fade countdown
//   0x68  FUN_0051ea80  deactivate: bars out, clear the countdown
//   0x6c  LAB_0051eae0  update:     re-aim at the ship, and run the fade countdown
//
// Its own fields, all three written by the ctor at 0x0051ea2a..0x0051ea36:
//
//   +0xf8   bFadePending   whether the docking fade-out has been armed
//   +0xfc   TotalSeconds   how long the docking sequence lasts
//   +0x100  ElapsedSeconds how much of it has gone by
//
// The countdown is what makes the screen fade to black just as the ship reaches the airlock: the
// update fades out once the REMAINING time drops below the fade's own duration, so the fade
// finishes exactly as the sequence does.

#include "space_camera_game.h"

struct FDockingCamera : FGameCamera
{
    void Attach(void* Object) override;
    bool IsFinished() override;
    bool Activate(int const* ViewRect4) override;
    void Deactivate() override;
    void Update(float DeltaSeconds) override;

    unsigned char bFadePending;      // +0x0f8  ctor writes 0
    unsigned char Pad0F9[3];
    float         TotalSeconds;      // +0x0fc  ctor writes 0
    float         ElapsedSeconds;    // +0x100  ctor writes 0
};

static_assert(sizeof(FDockingCamera) == 0x104, "FDockingCamera is 0x104");
static_assert(offsetof(FDockingCamera, bFadePending) == 0x0f8, "DC +0x0f8");
static_assert(offsetof(FDockingCamera, TotalSeconds) == 0x0fc, "DC +0x0fc");
static_assert(offsetof(FDockingCamera, ElapsedSeconds) == 0x100, "DC +0x100");

// FUN_0051ea20.
void DockingCameraConstruct(FDockingCamera* Cam);

// Slot 0x58 (FUN_0051eaa0).
void DockingCameraAttach(FDockingCamera* Cam, void* Object);

// Slot 0x64 (LAB_0051ea50).
bool DockingCameraActivate(FDockingCamera* Cam, const int* ViewRect4);

// Slot 0x68 (FUN_0051ea80).
void DockingCameraDeactivate(FDockingCamera* Cam);

// Slot 0x6c (LAB_0051eae0). Re-aims at the player ship and advances the fade countdown.
void DockingCameraUpdate(FDockingCamera* Cam, float DeltaSeconds);

// FUN_0051ebd0 -- a non-virtual method, called from FUN_0054bc0a. Arms the countdown: the
// sequence is Seconds long, and the elapsed counter starts at 0.1 rather than at zero (the
// literal 0x3dcccccd), so the first tenth of a second is spent before the camera ever runs.
void DockingCameraBeginSequence(FDockingCamera* Cam, float Seconds);

inline FSpaceCamera* SpaceCameraOf(FDockingCamera* Cam) { return Cam; }
