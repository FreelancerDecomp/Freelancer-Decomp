// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail first-person (cockpit) camera: the object at in-flight-controller + 0x00c, vtable
// .rdata 0x005db944, ctor 0x00517180, size 0x1a4 (the gap to the third-person camera at + 0x1b0).
//
// This is the only one of the four cockpit cameras that owns a mesh, and the only one whose
// slot 0x50 is not the shared 0x0051c8b0 (it uses 0x00401e90, a stub). Its own slots are
// 0x54 (0x00517330, the name), 0x58 (0x005178e0, the attach), 0x64 (0x0051b0a0),
// 0x6c (0x00517ba0) and 0x78 (0x00518420); the rest it shares with its siblings.
//
// Its ini loader FUN_005173c0 is the one the in-flight init calls FIRST (0x0054ad2d), and all
// four loaders are handed the same cockpit path.

#include "space_camera_game.h"

#include "client_mesh_load.h"   // FMeshHolder (FUN_00420d40)

#include <stddef.h>

// The three (position, orientation) pairs the ctor plants, at their retail offsets. The loader
// overwrites the middle one from the engine's cockpit hardpoint; the other two are written by the
// attach (LAB_005178e0) and the per-frame update (LAB_00517ba0).
struct FFirstPersonCamera : FHudCamera
{
    void ScaleEnvironmentEffects() override;
    char const* GetName() override;
    void Attach(void* Object) override;
    void Deactivate() override;
    void Update(float DeltaSeconds) override;
    void DrawScene(unsigned LayerFlags, unsigned Param2) override;
    bool OnEvent(unsigned MsgId) override;


    float PositionA[3];              // +0x100  zeroed by the ctor
    float OrientationA[9];           // +0x10c  identity
    float HeadPosition[3];           // +0x130  zeroed; loader fills it from IEngine slot 0x90
    float HeadOrientation[9];        // +0x13c  identity; loader fills it from IEngine slot 0x98
    float PositionC[3];              // +0x160  zeroed
    float OrientationC[9];           // +0x16c  identity

    // FUN_00420d40 builds the cockpit mesh holder in place here (0x005171a7); FUN_00420dd0
    // loads it from the mesh and material-library names the loader collected, and its +0x04
    // (+0x194 here) is the compound instance CompoundInstanceAnalyze walks to find the
    // cockpit's camera hardpoint.
    FMeshHolder Mesh;                // +0x190

    unsigned char bFlag1A0;          // +0x1a0  ctor sets 1
    unsigned char Pad1A1[3];
};

// The third-person camera sits at controller + 0x1b0 and this one at + 0x00c.
static_assert(sizeof(FFirstPersonCamera) == 0x1a4, "FFirstPersonCamera must be 0x1a4");
static_assert(offsetof(FFirstPersonCamera, PositionA) == 0x100, "FP +0x100");
static_assert(offsetof(FFirstPersonCamera, OrientationA) == 0x10c, "FP +0x10c");
static_assert(offsetof(FFirstPersonCamera, HeadPosition) == 0x130, "FP +0x130");
static_assert(offsetof(FFirstPersonCamera, HeadOrientation) == 0x13c, "FP +0x13c");
static_assert(offsetof(FFirstPersonCamera, PositionC) == 0x160, "FP +0x160");
static_assert(offsetof(FFirstPersonCamera, OrientationC) == 0x16c, "FP +0x16c");
static_assert(offsetof(FFirstPersonCamera, Mesh) == 0x190, "FP +0x190");
static_assert(offsetof(FFirstPersonCamera, bFlag1A0) == 0x1a0, "FP +0x1a0");

// [Cockpit] head_turn, which retail keeps in two globals rather than on the camera --
// DAT_006135a0 and DAT_006135a4 (.data 30.0f / 25.0f; written by the loader, read by nothing).
extern float G_CockpitHeadTurnX;    // DAT_006135a0
extern float G_CockpitHeadTurnY;    // DAT_006135a4

// DAT_00675220: Cockpit.cpp's lazily-filled I3DMathEngine cache (FUN_005b73e0 on first use),
// read by the first-person attach/update and by the rear-view update LAB_0051b0d0.
struct I3DMathEngine;
extern I3DMathEngine* G_FirstPersonMathEngine;

// FUN_00517180. Chains to the HUDCamera ctor, builds the mesh helper, then plants three
// identity transforms and clears the two HUDCamera flag bytes at +0xfc and +0xfd.
void FirstPersonCameraConstruct(FFirstPersonCamera* Cam);

// Slot 0x54 (0x00517330): the class name, a plain .rdata literal.
const char* FirstPersonCameraGetName();

// Slot 0x58 (LAB_005178e0): HUDCamera attach (FUN_0051cbb0), then -- with an object watched --
// the camera takes the object's transform, the "HpCockpit" hardpoint (FindHardpoint_PS; missing
// -> (0,0,-4) + identity) into +0x100/+0x10c, and the eye point object + R*hp + (R*hpR)*head
// into +0x160 and the world transform. Sets +0x1a0.
void FirstPersonCameraAttach(FFirstPersonCamera* Cam, void* Object);

// Slot 0x6c (LAB_00517ba0): with a watched client ship, place the cockpit mesh at the ship's
// cockpit hardpoint, derive the head's acceleration lag (DAT_0067522c) and roll from the ship's
// local velocity / angular velocity, set the camera's world transform (rigid while the "cockpit"
// perf option's applied value is under 0.999) and shake it (FUN_0051dbd0); then HUDCamera's
// update (FUN_0051d070) and +0x1a0 cleared.
void FirstPersonCameraUpdate(FFirstPersonCamera* Cam, float DeltaSeconds);

// Slot 0x78 (LAB_00518420, thiscall RET 8): with an object watched, the scene pass
// (FUN_0051c200), a depth clear (always when the "cockpit" option is applied above 0.999,
// otherwise only with the HUD option), FUN_0051c970, the cockpit mesh pass (near / far 0.05 /
// 500, the cockpit lighting or the jump ambient, the mesh drawn in depth range [0.75, 1]),
// HUDCamera's draw (FUN_0051daa0) and FUN_0051c9e0.
void FirstPersonCameraSceneDraw(FFirstPersonCamera* Cam, unsigned LayerFlags, unsigned Param2);

// FUN_005183e0 -- slot 0 of the FirstPerson Event::Handler face (+0xf8, table 0x005db93c):
// blocked mouse pick (FUN_0045a740) -> false, otherwise HUDCamera's FUN_0051d7f0.
bool FirstPersonCameraOnEvent(FFirstPersonCamera* Cam, unsigned MsgId);

// FUN_00518410 (thiscall): `ADD ECX,0x190 / JMP FUN_00420f70` -- release the cockpit mesh
// holder (destroy the engine instance, drop both material-cache slots). The space teardown
// FUN_0054aec0 calls it on controller + 0x00c (0x0054aef7).
void FirstPersonCameraReleaseMesh(FFirstPersonCamera* Cam);

// FUN_005173c0, a __thiscall taking the cockpit ini path. Reads [Cockpit] (mesh and
// material_library into 0x104-byte stack strings, head_turn into two globals), warns about the
// obsolete [Camera] header, loads the cockpit mesh, takes the head transform from its "cam"
// part and finishes with the same 4:3 sanity check the third-person loader runs. Returns
// nothing: a missing file and an odd aspect are FDUMP warnings only.
void FirstPersonCameraLoad(FFirstPersonCamera* Cam, const char* CockpitIniPath);
