// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Thorn_Light and Thorn_LightReference (RTTI .?AUThorn_Light@@ / .?AUThorn_LightReference@@,
// E:\FL\Scratch\Source\Client\GF\ScriptLight.cpp) -- the objects LIGHT entities are bound to.
//
// A Thorn_Light (operator new(0xa4) in FUN_0044e260, ctor FUN_00452fa0, identity vtable
// 0x005cd524) owns an engine light wrapper (FUN_00556fd0, gf_script.h) and a slot of the script
// light registry DAT_0066d130 / DAT_0066d230, which the room draw enumerates (FUN_0044b450).
// A Thorn_LightReference (operator new(0x70) in FUN_0044e2d0, ctor FUN_00453480, identity vtable
// 0x005cd734) looks up a created light of the same name in the running scripts (FUN_00456790)
// and forwards to it.

#include "common_csys.h"
#include "gf_thorn_component.h"

struct FGfLightWrapper;

struct FThornLight : FThornComponent
{
    // FUN_00452fa0 (thiscall, RET 0xc).
    FThornLight(Csys const& ScriptFrame, FGfScript* InOwner, FThornEntitySnap const* Desc);

    // ICompoundControl (0x005cd550 / 0x005cd554)
    GENRESULT DACOM_API SetTransform(float const* Transform12) override; // 11 LAB_004531e0
    GENRESULT DACOM_API GetTransform(float* OutTransform12) override;    // 12 LAB_004532d0

    // IGeoTransformControl (0x005cd4c4..0x005cd4d0)
    GENRESULT DACOM_API SetPosition(float* Position) override;           // 3  LAB_00453130
    GENRESULT DACOM_API SetOrientation(float* Orient9) override;         // 4  LAB_00453150
    GENRESULT DACOM_API GetPosition(float* OutPosition) override;        // 5  LAB_00453280
    GENRESULT DACOM_API GetOrientation(float* OutOrient9) override;      // 6  LAB_004532b0

    // ID3DLightControl (0x005cd464..0x005cd478)
    GENRESULT DACOM_API SetOn(int On) override;                          // 3  LAB_00453310
    GENRESULT DACOM_API SetLight(void const* D3dLight8) override;        // 4  LAB_004533d0
    GENRESULT DACOM_API SetDirection(float* Dir3) override;              // 5  LAB_00453330
    GENRESULT DACOM_API GetOn(int* OutOn) override;                      // 6  LAB_004533f0
    GENRESULT DACOM_API GetLight(void* OutD3dLight8) override;           // 7  LAB_00453450
    GENRESULT DACOM_API GetDirection(float* OutDir3) override;           // 8  LAB_00453420

    // identity tail
    void Stop() override;                                                // 14 LAB_00453100
    void* GetEngineInstance() override;                                  // 17 FUN_00450870

    FGfLightWrapper* Wrapper; // +0x6c FUN_00556fd0's wrapper
    int RegistryIndex;        // +0x70 slot in DAT_0066d130 / DAT_0066d230, -1 when none
    Matrix Orient;            // +0x74 the last orientation Thorn set (script frame)
    Vector Direction;         // +0x98 the light direction in Orient's frame (props +0x0c)
};

struct FThornLightReference : FThornComponent
{
    // FUN_00453480 (thiscall, RET 0xc).
    FThornLightReference(Csys const& ScriptFrame, FGfScript* InOwner,
                         FThornEntitySnap const* Desc);

    // ICompoundControl
    GENRESULT DACOM_API SetTransform(float const* Transform12) override; // 11 LAB_00457c00
    GENRESULT DACOM_API GetTransform(float* OutTransform12) override;    // 12 LAB_00457c20

    // IGeoTransformControl
    GENRESULT DACOM_API SetPosition(float* Position) override;           // 3  LAB_00457bc0
    GENRESULT DACOM_API SetOrientation(float* Orient9) override;         // 4  LAB_00457be0
    GENRESULT DACOM_API GetPosition(float* OutPosition) override;        // 5  LAB_00453560
    GENRESULT DACOM_API GetOrientation(float* OutOrient9) override;      // 6  LAB_00453580

    // ID3DLightControl
    GENRESULT DACOM_API SetOn(int On) override;                          // 3  LAB_004535a0
    GENRESULT DACOM_API SetLight(void const* D3dLight8) override;        // 4  LAB_004535e0
    GENRESULT DACOM_API SetDirection(float* Dir3) override;              // 5  LAB_004535c0
    GENRESULT DACOM_API GetOn(int* OutOn) override;                      // 6  LAB_00453600
    GENRESULT DACOM_API GetLight(void* OutD3dLight8) override;           // 7  LAB_00453640
    GENRESULT DACOM_API GetDirection(float* OutDir3) override;           // 8  LAB_00453620

    // identity tail
    void Stop() override;                                                // 14 LAB_00450cd0
    void* GetEngineInstance() override;                                  // 17 LAB_00453550

    FThornComponent* Target; // +0x6c the created light of that name, or null
};

// The reference forwarders shared (ICF) by Thorn_LightReference, Thorn_MarkerReference and
// Thorn_SoundReference: a null target answers -1 without a message.
GENRESULT ThornReferenceSetPosition(FThornComponent* Target, float* Position);       // 00457bc0
GENRESULT ThornReferenceSetOrientation(FThornComponent* Target, float* Orient9);     // 00457be0
GENRESULT ThornReferenceGetPosition(FThornComponent* Target, float* OutPosition);    // 00453560
GENRESULT ThornReferenceGetOrientation(FThornComponent* Target, float* OutOrient9);  // 00453580
GENRESULT ThornReferenceSetTransform(FThornComponent* Target, float const* Xform12); // 00457c00
GENRESULT ThornReferenceGetTransform(FThornComponent* Target, float* OutXform12);    // 00457c20
void* ThornReferenceGetEngineInstance(FThornComponent* Target);                      // 00453550
