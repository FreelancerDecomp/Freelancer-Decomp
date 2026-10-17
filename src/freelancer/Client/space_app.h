// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail SpaceApp -- the in-flight client state, one static object at .data 0x00678b98 (handed
// out by FUN_0054bd30). RTTI COL at .rdata 0x005e0550 -> .?AVSpaceApp@@, bases IEnvironment,
// IInputEvent (both on the primary vptr, .rdata 0x005e0554, 15 slots) and Event::Handler at +4
// (vptr .rdata 0x005e0548, one slot: LAB_0054a580).
//
// Size 0xbac: the docking camera at +0xa9c is 0x104 bytes and the controller's own words sit at
// +0xba0..+0xbab. Built by the static initializer LAB_00548f00 (FUN_00549300 on DAT_00678b98,
// then atexit(LAB_00548f20) -> FUN_00548f30). Every offset below is a store in the ctor
// 0x00549300 or in the listed slot.
//
// The eight cameras are the retail classes at their retail offsets; FUN_00549e70 picks among
// them, FUN_00549bc0 installs one into +0xba0.

#include "client_environment.h"
#include "event_handler.h"
#include "space_camera_first.h"
#include "space_camera_third.h"
#include "space_camera_turret.h"
#include "space_camera_rear.h"
#include "space_camera_death.h"
#include "space_camera_script.h"
#include "space_camera_static.h"
#include "space_camera_docking.h"

#include <stddef.h>

// The Event::Handler face at +4. Its one slot is LAB_0054a580 (thiscall, RET 4) with `this` = the
// face, so every controller field it touches is read at face-relative offsets (+0xba0 there is
// the controller's +0xba4, and so on).
class FSpaceAppEventFace : public EventHandler
{
public:
    FSpaceAppEventFace()
    {
    }

    // FUN_00576010 on +4 -- the last step of FUN_00548f30.
    ~FSpaceAppEventFace()
    {
    }

    // LAB_0054a580.
    bool OnEvent(unsigned MsgId) override;
};

// The primary vftable .rdata 0x005e0554 (COL 0x005ef678), fifteen slots -- raw PE dwords; the
// dword after slot 14 is the next vtable's COL 0x005ef664:
//   0 LAB_0054a0e0  1 0x004b50e0 (XOR AL,AL / RET 0xc)  2 LAB_0054a210  3 LAB_0054a2e0
//   4 LAB_0054a450  5 0x00401e90 (RET)  6 0x0053e4a0 (RET 4)  7 0x004b5010 (XOR EAX,EAX / RET)
//   8 0x00401e90  9 FUN_0054ab20  10 0x00401e90  11 LAB_0054a540  12 FUN_0054ab40
//   13 FUN_0054aec0  14 LAB_0054b000.
struct SpaceApp : public IEnvironment
{
    // FUN_00549300 / FUN_00548f30.
    SpaceApp();
    ~SpaceApp();

    bool Slot00(unsigned A, unsigned Message, unsigned B, unsigned C) override;
    bool Slot01(unsigned A, unsigned B, unsigned C) override;
    bool Slot02(unsigned A, unsigned B, unsigned long Packed) override;
    bool Slot03(unsigned A, unsigned Message, unsigned B, unsigned long Packed) override;
    bool Slot04(unsigned A, unsigned B, unsigned C) override;
    void Slot05() override;
    void Slot06(unsigned A) override;
    int GetEnvironmentType() override;
    void PreInit() override;
    void Shutdown() override;          // FUN_0054ab20 -- ShutdownSpaceState (space_state.cpp)
    void Slot10() override;
    void Resize(int Width, int Height) override; // LAB_0054a540
    bool Init(int Param) override;     // FUN_0054ab40 -- InitSpaceState
    void Destroy() override;           // FUN_0054aec0 -- DestroySpaceState
    int Update(float DeltaSeconds) override;     // LAB_0054b000 -- UpdateSpaceState

    //                                      +0x000  vptr .rdata 0x005e0554 (IEnvironment)
    FSpaceAppEventFace Events;              // +0x004  .rdata 0x005e0548
    unsigned char      bCameraReady;        // +0x008  DAT_00678ba0 -- 1 at 0x0054ab8e, 0 at 0x0054af8d
    unsigned char      bPlayerLostHandled;  // +0x009  FUN_00471e80 already run for a lost ship
    unsigned char      bLeaveRequested;     // +0x00a  the frame returns 6 while set
    unsigned char      bInitInProgress;     // +0x00b  DAT_00678ba3 (FUN_0054bd40)
    FFirstPersonCamera FirstPerson;         // +0x00c  ctor FUN_00517180
    FThirdPersonCamera ThirdPerson;         // +0x1b0  ctor FUN_00518600
    FTurretCamera      Turret;              // +0x3c8  ctor FUN_0051a210
    FRearViewCamera    RearView;            // +0x500  ctor FUN_0051ad30
    FDeathCamera       Death;               // +0x60c  GameCamera ctor + four floats, vt 0x005e061c
    FScriptCamera      Script;              // +0x760  ctor FUN_0051e100
    FStaticCamera      Static;              // +0x9a4  GameCamera ctor, vt 0x005e0594
    FDockingCamera     Docking;             // +0xa9c  ctor FUN_0051ea20
    FSpaceCamera*      ActiveCamera;        // +0xba0  DAT_00679738
    int                PlayerPreference;    // +0xba4  0 first person, 1 third (ctor 1)
    // +0xba8 = DAT_00679740: raised by FUN_0054bb10 (a script camera was set up, 0x0054bb43) and
    // read by FUN_0054bc90; the frame drops it when the view returns to "player" (0x0054b1fa),
    // and FUN_0054b230 keeps the camera where it is while it is up.
    unsigned char      bScriptCameraLatch;  // +0xba8
    unsigned char      bSpaceReady;         // +0xba9  1 at 0x0054adf3, dropped once the launch ends
    unsigned char      bDockingPending;     // +0xbaa
    unsigned char      bDockingActive;      // +0xbab

private:
    SpaceApp(SpaceApp const&) = delete;
    SpaceApp& operator=(SpaceApp const&) = delete;
};

static_assert(sizeof(void*) != 4 || sizeof(SpaceApp) == 0xbac, "SpaceApp is 0xbac");
static_assert(sizeof(void*) != 4 || offsetof(SpaceApp, Events) == 0x004, "SpaceApp +0x004");
static_assert(sizeof(void*) != 4 || offsetof(SpaceApp, bCameraReady) == 0x008, "SpaceApp +0x008");
static_assert(sizeof(void*) != 4 || offsetof(SpaceApp, bInitInProgress) == 0x00b, "SpaceApp +0x00b");
static_assert(sizeof(void*) != 4 || offsetof(SpaceApp, FirstPerson) == 0x00c, "SpaceApp +0x00c");
static_assert(sizeof(void*) != 4 || offsetof(SpaceApp, ThirdPerson) == 0x1b0, "SpaceApp +0x1b0");
static_assert(sizeof(void*) != 4 || offsetof(SpaceApp, Turret) == 0x3c8, "SpaceApp +0x3c8");
static_assert(sizeof(void*) != 4 || offsetof(SpaceApp, RearView) == 0x500, "SpaceApp +0x500");
static_assert(sizeof(void*) != 4 || offsetof(SpaceApp, Death) == 0x60c, "SpaceApp +0x60c");
static_assert(sizeof(void*) != 4 || offsetof(SpaceApp, Script) == 0x760, "SpaceApp +0x760");
static_assert(sizeof(void*) != 4 || offsetof(SpaceApp, Static) == 0x9a4, "SpaceApp +0x9a4");
static_assert(sizeof(void*) != 4 || offsetof(SpaceApp, Docking) == 0xa9c, "SpaceApp +0xa9c");
static_assert(sizeof(void*) != 4 || offsetof(SpaceApp, ActiveCamera) == 0xba0, "SpaceApp +0xba0");
static_assert(sizeof(void*) != 4 || offsetof(SpaceApp, PlayerPreference) == 0xba4, "SpaceApp +0xba4");
static_assert(sizeof(void*) != 4 || offsetof(SpaceApp, bScriptCameraLatch) == 0xba8, "SpaceApp +0xba8");
static_assert(sizeof(void*) != 4 || offsetof(SpaceApp, bDockingActive) == 0xbab, "SpaceApp +0xbab");

// DAT_00678b98 (space_state.cpp).
extern SpaceApp G_SpaceApp;

// FUN_00549830 (thiscall on the controller, no arguments): read cameras.ini and hand every
// entry of [WinCamera] (the Win3D camera DAT_0067dbf8), [CockpitCamera], [ThirdPersonCamera],
// [TurretCamera], [RearViewCamera] and [DeathCamera] to that camera's slot 0x4c. Caller: the
// space init (0x0054ad86).
void SpaceAppLoadCamerasIni(SpaceApp* App);

// FUN_0054b900 (cdecl, no arguments): the player's ship was destroyed -- arm the death camera
// (+0x60c) from the listener camera's transform and switch to it. Caller: the IClient
// DESTROYOBJECT handler (0x0053bcbc).
void SpaceAppArmDeathCamera();

// FUN_0054bb50 (cdecl, one argument): the static camera's Csys (+0x9a4 +0x04, twelve floats)
// and view-dirty byte, then "static" when the controller is up. Caller: IClient 0x0053e154.
void SetStaticCameraTransform(const float* Csys12);
