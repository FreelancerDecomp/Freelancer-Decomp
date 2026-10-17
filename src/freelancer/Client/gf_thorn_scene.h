// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Thorn_Scene (RTTI .?AUThorn_Scene@@) -- the object a SCENE entity is bound to
// (operator new(0x78) in FUN_0044d630, ctor FUN_00454390, identity vtable 0x005cdfec).
//
// A scene carries the ambient colour of the room (+0x6c, the first three dwords of the entity's
// 0x3c-byte tail) and answers Thorn's fog and reverb facets. Neither facet looks at the object:
// fog reads and writes the room fog state DAT_0066d2a8 (G_GfFog, gf_script.h), reverb goes
// straight to DALib::Sound's EAX preview. Its identity table is Thorn_Component's defaults plus
// two stdcall slots of its own, 23 / 24 (the ambient colour set / get).
//
// The spawn (FUN_0044ed90, entity type 9) remembers the last scene it made and hands it to every
// spawned object through identity slot 19; RoomObject's ambient override (FUN_00559590) reads the
// colour at +0x6c of the scene it was given.
//
// NAME. The C++ name is the RTTI name, Thorn_Scene, with the F prefix. It is an exe-only class
// (its RTTI and vtable live in freelancer.exe) and must not be confused with thorn.dll's own
// scene, FThornScene (src/Thorn/ThornScene.h) -- nothing crosses between the two.

#include "gf_thorn_component.h"

struct FThorn_Scene : FThornComponent
{
    // FUN_00454390 (thiscall, RET 0xc).
    FThorn_Scene(Csys const& ScriptFrame, FGfScript* InOwner, FThornEntitySnap const* Desc);

    // IFogControl (0x005cdec0; none of them reads `this`)
    GENRESULT DACOM_API SetOn(unsigned char On) override;                         // 3  LAB_00454450
    GENRESULT DACOM_API GetOn(unsigned char* OutOn) override;                     // 4  LAB_00454480
    GENRESULT DACOM_API SetColor(int const* Rgb3) override;                       // 5  LAB_00454500
    GENRESULT DACOM_API GetColor(int* OutRgb3) override;                          // 6  LAB_00454530
    GENRESULT DACOM_API SetRange(float Start, float End, float Density) override; // 7  LAB_00454570
    GENRESULT DACOM_API GetRange(float* OutStart, float* OutEnd,
                                 float* OutDensity) override;                     // 8  LAB_004545a0
    GENRESULT DACOM_API SetTableMode(unsigned char Table, unsigned int Mode) override; // 9 LAB_004545d0
    GENRESULT DACOM_API GetTableMode(unsigned char* OutTable,
                                     unsigned int* OutMode) override;             // 10 LAB_00454600
    GENRESULT DACOM_API SetPacked(unsigned char const* Packed32) override;        // 11 FUN_00454640
    GENRESULT DACOM_API GetPacked(unsigned char* Out32) override;                 // 12 FUN_00454690
    GENRESULT DACOM_API SetRangeValue(const float* Start, const float* End,
                                      const float* Density) override; // modern 13 +0x34

    // IReverbControl (0x005cdeac)
    GENRESULT DACOM_API SetReverb(unsigned int Environment, float Volume, float Decay,
                                  float Damping) override;                        // 3  LAB_00454790
    GENRESULT DACOM_API GetReverb(float* OutEnvironment, float* OutVolume, float* OutDecay,
                                  float* OutDamping) override;                    // 4  LAB_004547c0

    // identity tail
    void* GetEngineInstance() override;                                           // 17 FUN_00450870

    // The scene's own identity slots (stdcall, `this` on the stack). No caller of either is
    // known in Freelancer.exe; the RoomObject reads +0x6c directly.
    virtual GENRESULT DACOM_API SetAmbient(float const* Rgb3);                    // 23 LAB_00453ec0
    virtual GENRESULT DACOM_API GetAmbient(float* OutRgb3);                       // 24 LAB_00454770

    float Ambient[3]; // +0x6c
};
