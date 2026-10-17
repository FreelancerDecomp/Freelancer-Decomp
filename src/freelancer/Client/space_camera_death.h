// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The death camera -- the shot that takes over when the player's ship is destroyed: it shakes,
// then drifts away from the wreck while the shake decays. Vtable .rdata 0x005e061c, controller
// + 0x60c, and no constructor of its own: the controller calls the FGameCamera ctor, writes four
// floats and swaps the vptr (0x00549371 .. 0x0054939e).
//
// Three slots are its own; the other twelve are FGameCamera's:
//
//   0x58  LAB_00549470  attach: take the object's position and velocity as the drift seed
//   0x60  LAB_00549450  finished: the shake timer has reached exactly zero
//   0x6c  LAB_00549620  update: shake, then drift
//
// The shake is two independent random rotations per frame, one about Y and one about Z, applied
// to a stored orientation rather than to the camera's own -- which is why the class carries a
// nine-float matrix of its own at +0x110. The amplitude falls off as the SQUARE of the time
// left in a two-second window, so the shake dies away quickly and smoothly.

#include "space_camera_game.h"

#include <stddef.h>

struct FDeathCamera : FGameCamera
{
    void Attach(void* Object) override;
    bool IsFinished() override;
    void Update(float DeltaSeconds) override;


    // +0x0f8  the watched object's world position at the moment of attach. Written by the attach
    // slot and, as far as the exe goes, never read again -- kept because retail keeps it.
    float BasePosition[3];

    // +0x104  the direction the camera drifts in: the watched object's velocity, clamped to
    // MaxDriftSpeed. With no object at all this becomes (-1, 0, 0).
    float DriftDirection[3];

    float ShakeOrientation[9];       // +0x110  the matrix the shake rotates and the update copies
    float DriftPosition[3];          // +0x134  the drift accumulator, copied into the world pos
    float DriftSpeed;                // +0x140  controller writes 1.0f
    float ShakeTimer;                // +0x144  counts down; zero means finished
    float ShakeDuration;             // +0x148  controller writes 1.5f
    float DriftDamping;              // +0x14c  controller writes 0.85f
    float MaxDriftSpeed;             // +0x150  controller writes 100.0f
};

static_assert(sizeof(FDeathCamera) == 0x154, "FDeathCamera is 0x154");
static_assert(offsetof(FDeathCamera, BasePosition) == 0x0f8, "DthC +0x0f8");
static_assert(offsetof(FDeathCamera, DriftDirection) == 0x104, "DthC +0x104");
static_assert(offsetof(FDeathCamera, ShakeOrientation) == 0x110, "DthC +0x110");
static_assert(offsetof(FDeathCamera, DriftPosition) == 0x134, "DthC +0x134");
static_assert(offsetof(FDeathCamera, DriftSpeed) == 0x140, "DthC +0x140");
static_assert(offsetof(FDeathCamera, ShakeTimer) == 0x144, "DthC +0x144");
static_assert(offsetof(FDeathCamera, ShakeDuration) == 0x148, "DthC +0x148");
static_assert(offsetof(FDeathCamera, DriftDamping) == 0x14c, "DthC +0x14c");
static_assert(offsetof(FDeathCamera, MaxDriftSpeed) == 0x150, "DthC +0x150");

// The controller's step for this slot: FUN_0051b2f0, then the four literals, then the vptr.
//
// Note what is NOT initialised: the shake matrix at +0x110, the drift accumulator at +0x134 and
// the shake timer at +0x144. The attach slot does not write them either (except on its
// no-object path, and not even then for the matrix). Retail relies on whatever set the death
// sequence up having written them; the port zeroes nothing extra, so an unstarted death camera
// reports itself finished (a zero timer) and stays put.
void DeathCameraConstruct(FDeathCamera* Cam);

// Slot 0x58 (LAB_00549470). Points the watcher at Object and seeds the drift from it.
void DeathCameraAttach(FDeathCamera* Cam, void* Object);

// Slot 0x60 (LAB_00549450). True when the shake timer is EXACTLY zero -- retail's test is an
// equality against 0.0f, not a "<= 0", so a negative timer would report unfinished. The update
// clamps the timer at zero, which is what keeps that from happening.
bool DeathCameraIsFinished(const FDeathCamera* Cam);

// Slot 0x6c (LAB_00549620). Shake, then drift. Does nothing at all once finished.
void DeathCameraUpdate(FDeathCamera* Cam, float DeltaSeconds);

inline FSpaceCamera* SpaceCameraOf(FDeathCamera* Cam) { return Cam; }
