// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail turret camera: the object at in-flight-controller + 0x3c8, vtable .rdata 0x005dbcc4,
// ctor 0x0051a210, size 0x138 (the gap to the rear-view camera at + 0x500).
//
// Like the rear-view camera this one is small enough to port end to end: the constructor plants
// every default, and the ini loader reads four keys and nothing else. Its own vtable slots are
// 0x54 (the name), 0x58 (0x0051a6a0, the attach), 0x64 (0x0051a640) and 0x6c (0x0051a730); the
// rest it shares with the other cockpit cameras. Its secondary input vptr is 0x005dbcb8, whose
// single method is 0x0051ac20 -- its own, unlike the rear-view camera which reuses HUDCamera's.

#include "space_camera_game.h"

#include <stddef.h>

struct FTurretCamera : FHudCamera
{
    char const* GetName() override;
    void Attach(void* Object) override;
    bool Activate(int const* ViewRect4) override;
    void Deactivate() override;
    void Update(float DeltaSeconds) override;
    void DrawScene(unsigned LayerFlags, unsigned Param2) override;
    bool OnEvent(unsigned MsgId) override;


    float Tether[3];             // +0x100  [TurretCamera] tether, default (0, 5, 25)
    float Field10C;              // +0x10c  zeroed by the ctor; the current yaw FUN_0051a7d0 turns by
    float Field110;              // +0x110  zeroed; the yaw it eases towards (wrapped by 2*pi)
    float Field114;              // +0x114  zeroed; the current pitch
    float Field118;              // +0x118  zeroed; the pitch it eases towards (+/- pi/3)
    float Field11C;              // +0x11c  zeroed; the yaw key rate (event 0x32 / 0x33)
    float Field120;              // +0x120  zeroed; the pitch key rate (event 0x34 / 0x35)
    float Field124;              // +0x124  0.75f, no ini key reaches it
    float YawRotateSpeed;        // +0x128  [TurretCamera] yaw_rotate_speed,   default 2.0
    float PitchRotateSpeed;      // +0x12c  [TurretCamera] pitch_rotate_speed, default 1.5
    float AccelSpeed;            // +0x130  [TurretCamera] accel_speed,        default 5.0 (0x0051a277)
    float MouseAimTime;          // +0x134  FUN_0051a7d0's mouse-aim clock (frame time summed while
                                 //         mouse flight steers; zeroed when it does not). The
                                 //         retail ctor never touches it.
};

// The rear-view camera sits at controller + 0x500 and this one at + 0x3c8.
static_assert(sizeof(FTurretCamera) == 0x138, "FTurretCamera must be 0x138");
static_assert(offsetof(FTurretCamera, Tether) == 0x100, "TC +0x100");
static_assert(offsetof(FTurretCamera, Field124) == 0x124, "TC +0x124");
static_assert(offsetof(FTurretCamera, YawRotateSpeed) == 0x128, "TC +0x128");
static_assert(offsetof(FTurretCamera, PitchRotateSpeed) == 0x12c, "TC +0x12c");
static_assert(offsetof(FTurretCamera, AccelSpeed) == 0x130, "TC +0x130");

// FUN_0051a210. Chains to the HUDCamera ctor, then plants the defaults; note that it writes the
// tether's Z first (0x41c80000) and only then X and Y, and that +0x124 gets 0.75f while no ini
// key can ever change it.
void TurretCameraConstruct(FTurretCamera* Cam);

// Slot 0x54 (0x0051a2a0): the class name, a plain .rdata literal.
const char* TurretCameraGetName();

// FUN_0051ac20 -- slot 0 of the Turret Event::Handler face (+0xf8, table 0x005dbcb8): blocked
// mouse pick -> false; HUDCamera's FUN_0051d7f0 first; then 0x32/0x33 set +0x11c and 0x34/0x35
// set +0x120 to -/+ +0x124 while the key is down (0 when released), 0x36 zeroes +0x10c..+0x118;
// those five answer true, anything else false.
bool TurretCameraOnEvent(FTurretCamera* Cam, unsigned MsgId);

// Slot 0x64 (FUN_0051a640): register the face for 0x32..0x36, then HUDCamera's activate.
bool TurretCameraActivate(FTurretCamera* Cam, const int* ViewRect4);

// FUN_0051a7d0 (thiscall RET 0xc): the turret view -- the watched ship's orientation turned by
// the eased yaw (+0x10c) and pitch (+0x114), shaken, and the tether (+0x100) through it onto the
// ship's position, into Out12 = {3x3, position}. The targets (+0x110/+0x118) move by the key
// rates (+0x11c/+0x120) or, after 0.25s of left-button / locked mouse flight, by the cursor's
// offset from the surface centre. Answers true (Out12 untouched) when the watcher does not hold
// a client ship; false otherwise. The last argument is passed by both callers and never read.
bool TurretCameraComputeView(FTurretCamera* Cam, float* Out12, float DeltaSeconds, bool bAttach);

// Slot 0x58 (FUN_0051a6a0): zero the four angles, HUDCamera's attach, then the view on an
// identity 3x4 into the world transform.
void TurretCameraAttach(FTurretCamera* Cam, void* Object);

// Slot 0x6c (FUN_0051a730): the view on an identity 3x4 into the world transform, then
// HUDCamera's update.
void TurretCameraUpdate(FTurretCamera* Cam, float DeltaSeconds);

// FUN_0051a2b0, a __thiscall taking the cockpit ini path. Reads [TurretCamera] and warns about
// the obsolete [Turret] section. Returns false when the file could not be opened.
bool TurretCameraLoad(FTurretCamera* Cam, const char* CockpitIniPath);
