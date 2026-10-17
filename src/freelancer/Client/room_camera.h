// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The room camera: the ICamera every RoomObject owns at +0x500 (FUN_00556be0), and the one the
// Thorn camera pool (gf_script.h, DAT_00668b00) pushes the active script camera into.
//
// Retail class tree, three vtables stamped in turn by the ctor FUN_00556280:
//   .rdata 0x005e1354  abstract base (FUN_005564a0 / dtor FUN_00556bd0): slots 0..4 and 16 are
//                      _purecall (LAB_005b8064), slot 17 LAB_00556a60 its deleting dtor;
//   .rdata 0x005e1304  intermediate class: QI/AddRef/Release filled in, slots 3/4 still pure,
//                      slot 17 FUN_005585e0;
//   .rdata 0x005e12a4  this class, 22 slots.
// The common ICamera prefix has 20 methods. Retail Room's tail differs after slot16;
// its position/orientation/transform operations use the named typed members below.
// This derived class's appended virtual methods do not reproduce the retail tail indices.

#include "common_misc.h"

struct Rect;
struct ViewRect;

class FRoomCamera : public ICamera
{
public:
    // FUN_00556280 body after operator_new(0xbc): the three vtable stamps, RefCount = 1,
    // FUN_004215b0 identity orientation, zero position, FUN_004b5150 identity view, then
    // FUN_005564b0(&Viewport) -- the ViewRect the ctor built with Rect_to_ViewRect first.
    explicit FRoomCamera(ViewRect const& Viewport);
    // FUN_00556bd0 -- only restamps the base vtable; no member teardown.
    virtual ~FRoomCamera();

    // Slots 0..2 -- LAB_00558600 / LAB_005586b0 / LAB_005586c0.
    GENRESULT DACOM_API QueryInterface(char const* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    // Slots 3..16.
    float const* DACOM_API GetPosition() override;          // LAB_00556a80  this + 0x80
    float const* DACOM_API GetOrientation() override;       // LAB_00556a90  this + 0x5c
    int const* DACOM_API GetViewport() override;            // LAB_0040f380  this + 0x04
    float DACOM_API GetHalfFovXDeg() override;              // LAB_00556630  +0x14
    float DACOM_API GetFovYDeg() override;                  // LAB_00556640  +0x18
    float DACOM_API GetZNear() override;                    // LAB_00556650  +0x1c
    float DACOM_API GetZFar() override;                     // LAB_00556660  +0x20
    float DACOM_API GetAspect() override;                   // LAB_00556670  +0x2c
    double DACOM_API GetXScaleOverNear() override;          // LAB_00556680  +0x38 / +0x1c
    double DACOM_API GetYScaleOverNear() override;          // LAB_00556690  +0x3c / +0x1c
    int DACOM_API Project(float* OutX, float* OutY, float* OutZ,
                          float const* World) override;     // LAB_005568b0
    void DACOM_API Unproject(float* OutWorld3, float ScreenX,
                             float ScreenY) override;       // LAB_00556950
    int DACOM_API ClassifySphere(float const* Center,
                                 float Radius) override;    // LAB_00556780
    float const* DACOM_API GetViewTransform() override;     // LAB_00556aa0

    // Complete the common prefix; the two legacy placeholders are local extensions.
    int DACOM_API ProjectVec(float* Out3, float const* World) override;
    void GetVelocity(float* Out3) override;
    int SetProperty(char const* Key, char const* Value) override;
    virtual void DACOM_API Slot20();
    virtual void DACOM_API Slot21();

    // Retail slot 19 (FUN_00556b70): copy three floats to +0x80.
    void SetPosition(float const* Pos3);
    // Retail slot 20 (FUN_00556b90): copy nine floats to +0x5c.
    void SetOrientation(float const* Orient9);
    // Retail slot 21 (LAB_00556bb0): copy twelve floats -- orientation then position -- to +0x5c.
    void SetTransform(float const* Transform12);

    // FUN_005564b0 -- non-virtual init from a ViewRect (null selects 0,0,639,479).
    void InitFromViewRect(int const* ViewRect4);
    // FUN_005566a0 -- rebuild the X side plane and XScale from TanHalfFovX.
    void RebuildPlaneX();
    // FUN_00556710 -- rebuild the Y side plane and YScale from TanHalfFovY.
    void RebuildPlaneY();

    int Viewport[4];             // +0x04  ViewRect: left, top, right, bottom
    float HalfFovXDeg;           // +0x14
    float HalfFovYDeg;           // +0x18
    float ZNear;                 // +0x1c
    float ZFar;                  // +0x20
    float TanHalfFovX;           // +0x24
    float TanHalfFovY;           // +0x28
    float Aspect;                // +0x2c
    float HalfWidth;             // +0x30
    float HalfHeight;            // +0x34
    float XScale;                // +0x38
    float YScale;                // +0x3c
    float PlaneX[3];             // +0x40
    float PlaneY[3];             // +0x4c
    unsigned long RefCount;      // +0x58
    float Orient[9];             // +0x5c  row-major; followed by Pos -- together an FTransform
    float Pos[3];                // +0x80
    float View[12];              // +0x8c  GetViewTransform's output
};

// FUN_00556280 -- cdecl (Transform12, Rect by value, HalfFovX, ZNear, ZFar). Transform12 is
// position (3) then orientation (9), as 556be0 lays it out on its stack.
FRoomCamera* RoomCameraConstruct(float const* PosOrient12, Rect const& Viewport,
                                 float HalfFovXDeg, float ZNear, float ZFar);

// FUN_00556be0 -- origin, identity, Rect {0, 0, DAT_00610850 (640), DAT_00610854 (480)},
// 35 / 0.25 / 1e6.
FRoomCamera* RoomCameraCreate();

// FUN_00556ca0 -- slot 17 with 1 (delete) on a non-null camera, bypassing the ref count.
void RoomCameraDestroy(FRoomCamera* Camera);

// FUN_00556cb0 / 556cc0 / 556cd0 -- slots 19 / 20 / 21.
void RoomCameraSetPosition(FRoomCamera* Camera, float const* Pos3);
void RoomCameraSetOrientation(FRoomCamera* Camera, float const* Orient9);
void RoomCameraSetTransform(FRoomCamera* Camera, float const* Transform12);

// FUN_00556ce0 -- set the horizontal half-angle; the aspect comes from the live viewport.
void RoomCameraSetHalfFovX(FRoomCamera* Camera, float HalfFovXDeg);
// Typed raw-word transport for Thorn control callbacks.
void RoomCameraSetHalfFovXValue(FRoomCamera* Camera, const float* HalfFovXDeg);
// FUN_00556e00 -- store ZNear, then rebuild both planes from the stored half-angles.
void RoomCameraSetZNear(FRoomCamera* Camera, float ZNear);
// Typed raw-word transport for Thorn control callbacks.
void RoomCameraSetZNearValue(FRoomCamera* Camera, const float* ZNear);
// FUN_00556f10 -- store ZFar.
void RoomCameraSetZFar(FRoomCamera* Camera, float ZFar);
// Typed raw-word transport for Thorn control callbacks.
void RoomCameraSetZFarValue(FRoomCamera* Camera, const float* ZFar);

// FUN_0041a390 (thiscall on a float[3]) -- scale by 1 / |v|, no zero test.
void RoomVec3Normalize(float* V);

// DAT_006799bc -- the lazily acquired 3DMathEngine this TU (0x00556xxx) shares: every use
// tests it and fills it from FUN_005b73e0 when null -- FRoomCamera::Project LAB_005568b0
// (0x005568c1), FRoomCamera::GetViewTransform LAB_00556aa0 (0x00556aa0) and the light factory
// FUN_00556fd0 (0x00557094). A raw scan finds no other reference; nothing releases it.
struct I3DMathEngine;
extern I3DMathEngine* G_LightsCameraMath;
I3DMathEngine* LightsCameraMath();
