// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Thorn_Psys and Thorn_PlayerEnginePsys (RTTI .?AUThorn_Psys@@ / .?AUThorn_PlayerEnginePsys@@,
// E:\FL\Scratch\Source\Client\GF\ScriptPsys.cpp) -- the objects PSYS entities are bound to.
//
// A Thorn_Psys (operator new(0xac) in FUN_0044e340, ctor FUN_00453d70, identity vtable
// 0x005cdc2c) plays one [VisEffect] through a LightsCamera particle holder (lights_camera.h):
// Thorn starts it (IPSysControl slot 3), ticks it with the frame the entity sits at, and ends it;
// the room draw hands it the camera (identity slot 18, through FUN_0044f970).
// A Thorn_PlayerEnginePsys (operator new(0x70) in FUN_0044e3c0, ctor FUN_004540c0, identity
// vtable 0x005cde3c) drives the Gf ObjectShip's engine effects instead ("PlayerShipEngines").

#include "common_csys.h"
#include "gf_thorn_component.h"
#include "lights_camera.h"

struct FVisEffectArch;

struct FThornPsys : FThornComponent
{
    // FUN_00453d70 (thiscall, RET 0xc).
    FThornPsys(Csys const& ScriptFrame, FGfScript* InOwner, FThornEntitySnap const* Desc);

    // FUN_00453e60 (thiscall, RET 4) -- the [VisEffect] named EffectName into +0x74, AddRef'd,
    // or an FDUMP. Caller: the spawn FUN_0044e340 (0x0044e3a6) with the entity's template.
    void BindVisEffect(char const* EffectName);

    // ICompoundControl (0x005cdc58 / 0x005cdc5c)
    GENRESULT DACOM_API SetTransform(float const* Transform12) override; // 11 LAB_00453f00
    GENRESULT DACOM_API GetTransform(float* OutTransform12) override;    // 12 LAB_00453f60

    // IGeoTransformControl (0x005cdbcc..0x005cdbd8)
    GENRESULT DACOM_API SetPosition(float* Position) override;           // 3  LAB_00453ec0
    GENRESULT DACOM_API SetOrientation(float* Orient9) override;         // 4  LAB_00453ee0
    GENRESULT DACOM_API GetPosition(float* OutPosition) override;        // 5  LAB_00454770
    GENRESULT DACOM_API GetOrientation(float* OutOrient9) override;      // 6  LAB_00453f40

    // IPSysControl (0x005cdb48..0x005cdb58)
    GENRESULT DACOM_API Start() override;                                // 3  LAB_00453fb0
    GENRESULT DACOM_API StopEffect() override;                           // 4  LAB_00453fd0
    GENRESULT DACOM_API Tick(int DeltaMs) override;                      // 5  LAB_00453ff0
    GENRESULT DACOM_API SetSparam(float Value) override;                 // 6  LAB_00454070
    GENRESULT DACOM_API GetSparam(float* OutValue) override;             // 7  LAB_004540a0

    // identity tail
    void Stop() override;                                                // 14 FUN_00453e30
    void* GetEngineInstance() override;                                  // 17 FUN_00450870
    void DrawEffects(ICamera* Camera) override;                          // 18 LAB_00453e50

    FLightsCameraParticles Particles; // +0x6c the live effect and its VisEffect reference
    FVisEffectArch* Vis;              // +0x74 the bound [VisEffect], own reference
    Csys Local;                       // +0x78 the entity frame Thorn set (no script-frame
                                      //       transform: position +0x78, orientation +0x84)
    float Sparam;                     // +0xa8
};

struct FThornPlayerEnginePsys : FThornComponent
{
    // FUN_004540c0 (thiscall, RET 0xc) -- base ctor, +0x6c = 0.
    FThornPlayerEnginePsys(Csys const& ScriptFrame, FGfScript* InOwner,
                           FThornEntitySnap const* Desc);

    // IPSysControl (0x005cdd58..0x005cdd68; slot 5 stays LAB_00454190)
    GENRESULT DACOM_API Start() override;                                // 3  LAB_00454150
    GENRESULT DACOM_API StopEffect() override;                           // 4  LAB_00454170
    GENRESULT DACOM_API SetSparam(float Value) override;                 // 6  LAB_004541a0
    GENRESULT DACOM_API GetSparam(float* OutValue) override;             // 7  LAB_004541d0

    // identity tail
    void Stop() override;                                                // 14 LAB_00454130
    void* GetEngineInstance() override;                                  // 17 LAB_00454140

    float Sparam; // +0x6c
};
