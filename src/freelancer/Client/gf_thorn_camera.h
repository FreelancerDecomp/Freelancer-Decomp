// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Thorn_Camera and Thorn_CameraReference (RTTI .?AUThorn_Camera@@ / .?AUThorn_CameraReference@@,
// E:\FL\Scratch\Source\Client\Gf\ScriptCamera.cpp) -- the objects CAMERA and MONITOR entities
// are bound to.
//
// A Thorn_Camera (operator new(0x70), ctor FUN_004507a0, identity vtable 0x005ccbbc) owns one
// slot of the camera pool DAT_00668b00 (G_GfCameraPool, gf_script.h): its geo and camera facets
// write the slot, and push into the slot's room camera only while the slot is the active one.
// A Thorn_CameraReference (operator new(0x70), ctor FUN_00450c60, identity vtable 0x005ccd5c)
// holds another script's camera at +0x6c and forwards every facet call to it.

#include "gf_thorn_component.h"

class FRoomCamera;

struct FThornCamera : FThornComponent
{
    // FUN_004507a0 (thiscall, RET 0xc) -- base ctor, then +0x6c = -1 (no pool slot yet).
    FThornCamera(Csys const& ScriptFrame, FGfScript* InOwner, FThornEntitySnap const* Desc);

    // FUN_00450830 (thiscall, RET 0xc) -- G_GfCameraPool.Allocate(Priority, Camera,
    // bCanBeActive) into +0x6c. Callers: the spawn factories FUN_0044d9b0 (0x0044da1f, 1),
    // FUN_0044dba0 (0x0044dc02, 0), FUN_0044dc20 (0x0044dced, 0).
    void Register(FRoomCamera* Camera, int Priority, unsigned char bCanBeActive);

    // ICompoundControl (0x005ccbe8 / 0x005ccbec)
    GENRESULT DACOM_API SetTransform(float const* Transform12) override; // 11 LAB_00450940
    GENRESULT DACOM_API GetTransform(float* OutTransform12) override;    // 12 LAB_00450aa0

    // IGeoTransformControl (0x005ccb5c..0x005ccb68)
    GENRESULT DACOM_API SetPosition(float* Position) override;           // 3  LAB_00450880
    GENRESULT DACOM_API SetOrientation(float* Orient9) override;         // 4  LAB_004508e0
    GENRESULT DACOM_API GetPosition(float* OutPosition) override;        // 5  LAB_004509f0
    GENRESULT DACOM_API GetOrientation(float* OutOrient9) override;      // 6  LAB_00450a50

    // ICameraControl (0x005ccb24..0x005ccb48; slots 4, 5, 9, 10 stay LAB_00454190)
    GENRESULT DACOM_API SetFov(const float* Fov) override;                      // 3  LAB_00450b40
    GENRESULT DACOM_API SetNear(const float* Near) override;                    // 6  LAB_00450b80
    GENRESULT DACOM_API SetFar(const float* Far) override;                      // 7  LAB_00450bc0
    GENRESULT DACOM_API GetFov(float* OutFov) override;                  // 8  LAB_00450c00
    GENRESULT DACOM_API GetNear(float* OutNear) override;                // 11 LAB_00450c20
    GENRESULT DACOM_API GetFar(float* OutFar) override;                  // 12 LAB_00450c40

    // identity tail
    void Stop() override;                                                // 14 LAB_00450860
    void* GetEngineInstance() override;                                  // 17 FUN_00450870

    int PoolSlot; // +0x6c index into G_GfCameraPool, -1 until Register
};

struct FThornCameraReference : FThornComponent
{
    // FUN_00450c60 (thiscall, RET 0xc) -- base ctor, then +0x6c = 0. The spawn FUN_0044dc20
    // writes the target afterwards (0x0044dc84 / 0x0044dd62).
    FThornCameraReference(Csys const& ScriptFrame, FGfScript* InOwner,
                          FThornEntitySnap const* Desc);

    // ICompoundControl
    GENRESULT DACOM_API SetTransform(float const* Transform12) override; // 11 LAB_00450dc0
    GENRESULT DACOM_API GetTransform(float* OutTransform12) override;    // 12 LAB_00450ea0

    // IGeoTransformControl
    GENRESULT DACOM_API SetPosition(float* Position) override;           // 3  LAB_00450d20
    GENRESULT DACOM_API SetOrientation(float* Orient9) override;         // 4  LAB_00450d70
    GENRESULT DACOM_API GetPosition(float* OutPosition) override;        // 5  LAB_00450e00
    GENRESULT DACOM_API GetOrientation(float* OutOrient9) override;      // 6  LAB_00450e50

    // ICameraControl
    GENRESULT DACOM_API SetFov(const float* Fov) override;                      // 3  LAB_00450ee0
    GENRESULT DACOM_API SetNear(const float* Near) override;                    // 6  LAB_00450f30
    GENRESULT DACOM_API SetFar(const float* Far) override;                      // 7  LAB_00450f80
    GENRESULT DACOM_API GetFov(float* OutFov) override;                  // 8  LAB_00450fd0
    GENRESULT DACOM_API GetNear(float* OutNear) override;                // 11 LAB_00451020
    GENRESULT DACOM_API GetFar(float* OutFar) override;                  // 12 LAB_00451070

    // identity tail
    void Stop() override;                                                // 14 LAB_00450cd0
    void* GetEngineInstance() override;                                  // 17 LAB_00450ce0

    FThornComponent* Target; // +0x6c the referenced camera, or null
};
