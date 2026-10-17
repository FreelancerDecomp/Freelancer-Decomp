// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Thorn_Compound (RTTI .?AVThorn_Compound@@, E:\FL\Scratch\Source\Client\Gf\ScriptCompound.cpp)
// -- the object a COMPOUND entity is bound to: one engine instance (a room prop's, or the
// player's ship), motions played on it through DALib::Anim, hardpoint connections, and its
// transform relative to the script frame with a fixed offset. operator new(0x190) at 0x0044de3b
// (FUN_0044dd80); identity vtable 0x005ccf6c.
//
// Built by FGfScript::SpawnCompound (FUN_0044dd80 -> FUN_0044e430, gf_script_spawn.cpp).

#include "common_goods.h" // HardpointSummary
#include "gf_thorn_component.h"

struct FRoomObject;

struct FThornCompound : FThornComponent
{
    // FUN_004510f0 (thiscall, RET 0x10).
    FThornCompound(Csys const& ScriptFrame, Csys const& InOffset, FGfScript* InOwner,
                   FThornEntitySnap const* Desc);
    // FUN_00451240 (FUN_00451220 is the scalar deleting destructor).
    ~FThornCompound() override;

    // ICompoundControl
    GENRESULT DACOM_API StartMotion(void* Desc) override;                // 3  FUN_004513b0
    GENRESULT DACOM_API StopMotion(void* Desc) override;                 // 4  FUN_00451450
    GENRESULT DACOM_API ConnectHardpoints(void* Desc) override;          // 7  FUN_00451e50
    GENRESULT DACOM_API DisconnectHardpoints(void* Desc) override;       // 8  FUN_00451e90
    GENRESULT DACOM_API GetHardpointXform(int TargetType, char const* Name,
                                          float* OutXform12) override;   // 9  LAB_00451b90
    GENRESULT DACOM_API SetTransform(float const* Transform12) override; // 11 LAB_00451660
    GENRESULT DACOM_API GetTransform(float* OutTransform12) override;    // 12 LAB_00451a00

    // IGeoTransformControl (0x005ccf0c..0x005ccf18)
    GENRESULT DACOM_API SetPosition(float* Position) override;           // 3  LAB_004514f0
    GENRESULT DACOM_API SetOrientation(float* Orient9) override;         // 4  LAB_004515b0
    GENRESULT DACOM_API GetPosition(float* OutPosition) override;        // 5  LAB_004517b0
    GENRESULT DACOM_API GetOrientation(float* OutOrient9) override;      // 6  LAB_00451930

    // identity tail
    void Stop() override;                                                // 14 FUN_00451290
    void* GetEngineInstance() override;                                  // 17 LAB_00451360
    void SetScene(void* Scene) override;                                 // 19 LAB_00451ed0
    void SetFlags(unsigned char bVisible, unsigned char bLit, unsigned char bFog,
                  unsigned char bFlag14) override;                       // 20 LAB_00451ef0

    // FUN_00451330 (thiscall, RET 4) -- the instance, and its hardpoints re-analyzed. The spawn
    // FUN_0044dd80 calls it (0x0044de67).
    void SetInstance(long InInstance);

    // FUN_00451380 -- every motion slot back to {-1, -1, 0}.
    void ResetMotionSlots();

    static const int kMotionSlots = 16;

    FRoomObject* Object;               // +0x6c the room object owning Instance, or null
    long Instance;                     // +0x70 -1
    void* AnimScript;                  // +0x74 IAnimation2 script, -1 = none
    int Category;                      // +0x78 -1 (written by the spawn; TODO meaning)
    unsigned char bUseScene;           // +0x7c not written by the ctor (the spawn sets it)
    Csys Offset;                       // +0x80 the entity's offset from its instance
    Csys Local;                        // +0xb0 identity; the last frame the script set
    unsigned long Playback[kMotionSlots]; // +0xe0 IAnimation2 playback ids, -1
    int EventId[kMotionSlots];         // +0x120 the START_MOTION's unique id, -1
    unsigned char bPlaying[kMotionSlots]; // +0x160
    HardpointSummary Hardpoints;       // +0x170 of Instance
};
