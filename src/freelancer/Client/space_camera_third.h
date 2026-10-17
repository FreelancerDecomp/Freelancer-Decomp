// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail third-person camera: the object at in-flight-controller + 0x1b0, vtable .rdata
// 0x005dbb14, size 0x218 (the gap to the turret camera at + 0x3c8).
//
// The first twenty slots are the shared base camera interface (space_camera.h); past them the
// vtable is HUDCamera's (.rdata 0x005dc08c) with four of its own:
//
//   +0x48  0x0051b700   shared with HUDCamera
//   +0x4c  0x0051b3f0   shared with HUDCamera
//   +0x50  0x0051c8b0   shared with HUDCamera
//   +0x54  0x005187c0   returns the literal "ThirdPerson"                  <- own, ported
//   +0x58  0x00518af0   attach                                              <- own, ported
//   +0x5c  0x0051cb90   shared with HUDCamera
//   +0x60  0x005172f0   shared with HUDCamera
//   +0x64  0x00518ad0   register event 0x3f, then HUDCamera's activate      <- own, ported
//   +0x68  0x0051b0b0   the derived cameras' shared deactivate
//   +0x6c  0x00518e40   update                                              <- own, ported
//   +0x70  0x0051ad20   JMP 0x0051da40 (HUDCamera's own 0x70)
//   +0x74  0x0051da70   shared with HUDCamera
//   +0x78  0x0051b250   the derived cameras' shared scene draw
//   +0x7c  0x00517320   shared with HUDCamera

#include "space_camera_game.h"

#include <stddef.h>

enum EThirdPersonCameraSlot
{
    kTpCamSlotGetName = 0x54,
    kTpCamSlotAttach  = 0x58,
    kTpCamSlotUpdate  = 0x6c
};

// Field names below are the port's; the OFFSETS are retail's, each traced to the ctor
// (0x00518600), the attach (0x00518af0) or the solver (FUN_00518f20).
struct FThirdPersonCamera : FHudCamera
{
    char const* GetName() override;
    void Attach(void* Object) override;
    bool Activate(int const* ViewRect4) override;
    void Deactivate() override;
    void Update(float DeltaSeconds) override;
    void DrawScene(unsigned LayerFlags, unsigned Param2) override;
    bool OnEvent(unsigned MsgId) override;

    // +0x000 .. +0x100. Retail's ctor at 0x00518600 chains straight into FUN_0051ca80, so
    // everything below +0x100 -- including the object watcher at +0xc0 and the velocity at
    // +0xdc -- belongs to the two intermediate classes, not to this one.

    // Written from the "HpCockpit" hardpoint when FindHardpoint_PS finds it (0x00518c9f), and
    // from a hardcoded fallback when it does not: position (0, 0, -4.0f) and identity. The
    // solver adds the position to the look offset; the orientation is never read back.
    float HardpointPos[3];              // +0x100
    float HardpointOrient[9];           // +0x10c

    float TranslationCache[3];          // +0x130  the placement FUN_00518f20 produced; it is
                                        //         also what the output transform's translation
                                        //         is copied from
    unsigned char bCameraLevelEnabled;  // +0x13c  gates the auto-level branch; ctor sets 1
    unsigned char Pad13D[3];

    // The look offset vector {+0x140, +0x144, +0x148}: {0, camera_offset[0], camera_offset[1]}
    // from the ship companion (ctor default {0, 7, 60}).
    float Field140;                     // +0x140
    float OffsetX;                      // +0x144  ICliObj camera_offset[0]
    float OffsetY;                      // +0x148  ICliObj camera_offset[1]
    // A second offset vector added to it; zeroed by the ctor and on every attach, never
    // written otherwise.
    float Field14C;                     // +0x14c
    float Field150;                     // +0x150
    float Field154;                     // +0x154
    float ResetOrient[9];               // +0x158  the camera orientation as of the last solve;
                                        //         seeded from the target on a reset

    float TargetOrientB[9];             // +0x17c  second copy of the target's orientation
    float LevelAxis[3];                 // +0x1a0  reset to (0, 0, -1)
    float PrevAngularRate;              // +0x1ac  the last frame's turn rate (rad/s)
    float TargetOrientA[9];             // +0x1b0  first copy of the target's orientation

    float AngularAcceleration;          // +0x1d4  ICliObj +0x70
    float AngularSlerpMultiplier;       // +0x1d8  ICliObj +0x74
    float HorizontalTurnAngle;          // +0x1dc  ICliObj +0x78 (radians)
    float VerticalTurnUpAngle;          // +0x1e0  ICliObj +0x7c (radians)
    float VerticalTurnDownAngle;        // +0x1e4  ICliObj +0x80 (radians)
    float TurnLookAheadSlerpAmount;     // +0x1e8  ICliObj +0x84

    // The speed tracker FUN_00518f20 runs every frame. None of these come from the ini.
    float PrevSpeed;                    // +0x1ec  last frame's speed, for the acceleration
    float LookAheadMultiplier;          // +0x1f0  ctor 0.05f
    float AccelerationLimit;            // +0x1f4  ctor 40.0f; scaled by the slack
    float SmoothedSpeed;                // +0x1f8  what the camera actually follows
    float SpeedSlack;                   // +0x1fc  integrated (speed - smoothed), never negative
    float TurnRateScale;                // +0x200  ctor 2.0f

    float UpAxis[3];                    // +0x204  the up the camera levels towards
    float Field210;                     // +0x210  zeroed by the ctor, never read
    unsigned char bInitialised;         // +0x214  set on every exit from the attach; the next
                                        //         update skips its own solve and clears it
    unsigned char Pad215[3];
};

// The turret camera sits at controller + 0x3c8 and this one at + 0x1b0, so the class is
// exactly 0x218 bytes; every field offset below is checked against the listing. That the
// inherited part lands exactly on +0x100 is an independent check on the whole hierarchy:
// 0xc0 (FSpaceCamera) + 0x38 (FGameCamera) + 0x8 (FHudCamera) = 0x100.
static_assert(sizeof(FThirdPersonCamera) == 0x218, "FThirdPersonCamera must be 0x218");
static_assert(offsetof(FThirdPersonCamera, HardpointPos) == 0x100, "TP +0x100");
static_assert(offsetof(FThirdPersonCamera, HardpointOrient) == 0x10c, "TP +0x10c");
static_assert(offsetof(FThirdPersonCamera, TranslationCache) == 0x130, "TP +0x130");
static_assert(offsetof(FThirdPersonCamera, Field140) == 0x140, "TP +0x140");
static_assert(offsetof(FThirdPersonCamera, OffsetX) == 0x144, "TP +0x144");
static_assert(offsetof(FThirdPersonCamera, OffsetY) == 0x148, "TP +0x148");
static_assert(offsetof(FThirdPersonCamera, Field14C) == 0x14c, "TP +0x14c");
static_assert(offsetof(FThirdPersonCamera, Field154) == 0x154, "TP +0x154");
static_assert(offsetof(FThirdPersonCamera, TargetOrientB) == 0x17c, "TP +0x17c");
static_assert(offsetof(FThirdPersonCamera, PrevAngularRate) == 0x1ac, "TP +0x1ac");
static_assert(offsetof(FThirdPersonCamera, TargetOrientA) == 0x1b0, "TP +0x1b0");
static_assert(offsetof(FThirdPersonCamera, AngularAcceleration) == 0x1d4, "TP +0x1d4");
static_assert(offsetof(FThirdPersonCamera, TurnLookAheadSlerpAmount) == 0x1e8, "TP +0x1e8");
static_assert(offsetof(FThirdPersonCamera, UpAxis) == 0x204, "TP +0x204");
static_assert(offsetof(FThirdPersonCamera, Field210) == 0x210, "TP +0x210");
static_assert(offsetof(FThirdPersonCamera, bInitialised) == 0x214, "TP +0x214");

static_assert(offsetof(FThirdPersonCamera, bCameraLevelEnabled) == 0x13c, "TP +0x13c");
static_assert(offsetof(FThirdPersonCamera, ResetOrient) == 0x158, "TP +0x158");
static_assert(offsetof(FThirdPersonCamera, LevelAxis) == 0x1a0, "TP +0x1a0");
static_assert(offsetof(FThirdPersonCamera, PrevSpeed) == 0x1ec, "TP +0x1ec");
static_assert(offsetof(FThirdPersonCamera, LookAheadMultiplier) == 0x1f0, "TP +0x1f0");
static_assert(offsetof(FThirdPersonCamera, AccelerationLimit) == 0x1f4, "TP +0x1f4");
static_assert(offsetof(FThirdPersonCamera, SmoothedSpeed) == 0x1f8, "TP +0x1f8");
static_assert(offsetof(FThirdPersonCamera, SpeedSlack) == 0x1fc, "TP +0x1fc");
static_assert(offsetof(FThirdPersonCamera, TurnRateScale) == 0x200, "TP +0x200");


// FUN_005187d0. Opens the cockpit ini and validates it; returns false when the file is missing
// or the aspect ratio is off, which is exactly when retail emits its two FDUMP warnings.
bool ThirdPersonCameraLoad(FThirdPersonCamera* Cam, const char* CockpitIniPath);

// Slot 0x54 (0x005187c0): the class name, a plain .rdata literal.
const char* ThirdPersonCameraGetName();

// FUN_0051a190 -- slot 0 of the ThirdPerson Event::Handler face (+0xf8, table 0x005dbb0c):
// blocked mouse pick -> false; HUDCamera's FUN_0051d7f0 first; event 0x3f flips +0x13c and hands
// it to the watched ship's IBehaviorManager::update_level_camera, answering true.
bool ThirdPersonCameraOnEvent(FThirdPersonCamera* Cam, unsigned MsgId);

// FUN_00518600: HUDCamera's ctor, the two vptrs, then this class's defaults (see the .cpp).
void ThirdPersonCameraConstruct(FThirdPersonCamera* Cam);

// Slot 0x64 (FUN_00518ad0, thiscall RET 4): register the face for event 0x3f, then HUDCamera's
// activate (FUN_0051cad0) -- its answer.
bool ThirdPersonCameraActivate(FThirdPersonCamera* Cam, const int* ViewRect4);

// Slot 0x58 (LAB_00518af0, thiscall RET 4): HUDCamera's attach, the ship companion's camera
// block, the target's orientation copies and world transform, the HpCockpit hardpoint (or the
// fallback), then a reset solve from an identity view into the world transform and +0x130.
// With nothing watched it only warns (Cockpit.cpp 0x3c9). Either way +0x214 ends up 1.
void ThirdPersonCameraAttach(FThirdPersonCamera* Cam, void* Object);

// Slot 0x6c (LAB_00518e40, thiscall RET 4): unless the attach just ran (+0x214), solve from an
// identity view into the world transform (warning at Cockpit.cpp 0x3e9 when there is no ship);
// then HUDCamera's update, and +0x214 cleared.
void ThirdPersonCameraUpdate(FThirdPersonCamera* Cam, float DeltaSeconds);

// FUN_0051a140 (cdecl): Out9 = A9 * B9 through DAT_00675220's I3DMathEngine::MulMatrix into a
// local, copied out; returns Out9. Callers: 0x00519b34 (the solver below) and 0x0055c5c9.
float* CockpitMulMatrix(float* Out9, const float* A9, const float* B9);

// FUN_00518f20 (thiscall RET 0xc): the chase-camera solver. Out12 = {3x3, position}; false (Out12
// untouched) when the watcher does not hold a client ship. bReset seeds the speed tracker, the
// level axis and the orientation history from the ship.
bool ThirdPersonCameraSolve(FThirdPersonCamera* Cam, float* Out12, float DeltaSeconds, bool bReset);
