// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Thorn_Deformable (RTTI .?AVThorn_Deformable@@, E:\FL\Scratch\Source\Client\Gf\
// ScriptDeformable.cpp) -- the object a DEFORMABLE entity is bound to: a CharacterObject whose
// animations (CharacterInfo scripts through DALib::Deform) and IK aims the script drives.
// operator new(0x9f8) in FUN_0044ded0 (ctor call 0x0044df6c); identity vtable 0x005cd1ec.
//
// Built by FGfScript::SpawnDeformable (FUN_0044ded0 -> FUN_0044e870, gf_script_spawn.cpp) with
// the room's character (FUN_00449cd0 / FUN_00449d50), the sequence's, or a new one from the
// character factory FUN_00435b80 (gf_character_object.h).

#include "gf_thorn_component.h"

struct FCharacterObject;

struct FThornDeformable : FThornComponent
{
    // FUN_00451fd0 (thiscall, RET 0xc).
    FThornDeformable(Csys const& ScriptFrame, FGfScript* InOwner, FThornEntitySnap const* Desc);
    // FUN_004520c0 (FUN_004520a0 is the scalar deleting destructor).
    ~FThornDeformable() override;

    // ICompoundControl (0x005cd1f8..0x005cd21c)
    GENRESULT DACOM_API StartMotion(void* Desc) override;                // 3  LAB_00452240
    GENRESULT DACOM_API StopMotion(void* Desc) override;                 // 4  LAB_004523a0
    GENRESULT DACOM_API SetFloorHeight(float Height) override;           // 5  LAB_00452d00
    GENRESULT DACOM_API AdvanceTime(int DeltaMs) override;               // 6  LAB_00452210
    GENRESULT DACOM_API ConnectHardpoints(void* Desc) override;          // 7  FUN_00452d70
    GENRESULT DACOM_API DisconnectHardpoints(void* Desc) override;       // 8  FUN_00452db0
    GENRESULT DACOM_API GetHardpointXform(int TargetType, char const* Name,
                                          float* OutXform12) override;   // 9  LAB_00452730
    GENRESULT DACOM_API GetFloorHeight(float* OutHeight) override;       // 10 LAB_00452d50
    GENRESULT DACOM_API SetTransform(float const* Transform12) override; // 11 LAB_00452b50
    GENRESULT DACOM_API GetTransform(float* OutTransform12) override;    // 12 LAB_00452c70

    // IIKControl (0x005cd1a8..0x005cd1b0)
    GENRESULT DACOM_API StartIk(void* Desc, float* Position, float* Orientation) override;  // LAB_00452460
    GENRESULT DACOM_API StopIk(void* Desc) override;                                        // FUN_00452a30
    GENRESULT DACOM_API UpdateIk(void* Desc, float* Position, float* Orientation) override; // LAB_00452680

    // IGeoTransformControl (0x005cd188..0x005cd194)
    GENRESULT DACOM_API SetPosition(float* Position) override;           // LAB_00452ab0
    GENRESULT DACOM_API SetOrientation(float* Orient9) override;         // LAB_00452b00
    GENRESULT DACOM_API GetPosition(float* OutPosition) override;        // LAB_00452bd0
    GENRESULT DACOM_API GetOrientation(float* OutOrient9) override;      // LAB_00452c20

    // identity tail
    void Stop() override;                                                // 14 FUN_00452130
    bool FindActor(char const* InActorName, void** OutActorField,
                   void** OutActor) override;                            // 16 LAB_00452df0
    void* GetEngineInstance() override;                                  // 17 LAB_00452200
    void SetScene(void* Scene) override;                                 // 19 LAB_00452ec0
    void SetFlags(unsigned char bVisible, unsigned char bLit, unsigned char bFog,
                  unsigned char bFlag14) override;                       // 20 LAB_00452ed0

    // FUN_00452e40 (thiscall, RET 4) -- the character instance's frame (world). Caller: the
    // spawn FUN_0044ded0 (0x0044dfd8).
    Csys GetCharacterCsys() const;
    // FUN_00452eb0 -- the character's deformable instance. Caller: FUN_0044ded0 (0x0044dffb).
    void* GetDeformInstance() const;

    // One motion (retail slot ctor LAB_00451f50).
    struct FMotionSlot
    {
        FMotionSlot(); // LAB_00451f50: {-1, 0, -1, 0}

        int Handle;           // +0x00 -1; IDeformable slot 7's out handle
        unsigned ScriptId;     // +0x04 0; CreateID of the animation name
        int EventId;           // +0x08 -1
        unsigned char bActive; // +0x0c 0
    };

    // One IK aim (retail slot ctor LAB_00451f70).
    struct FAimSlot
    {
        FAimSlot(); // LAB_00451f70: zeros, Target identity

        void* Handle;          // +0x00 0; IDeformable slot 11's out handle
        void const* Key;        // +0x04 0; FThornStartIkDesc::Key
        Csys Target;            // +0x08 identity; the script-frame target the aim follows
        unsigned char bActive;  // +0x38 0
        unsigned char bStarted; // +0x39 0; start_aim succeeded
    };

    static const int kMotionSlots = 32;
    static const int kAimSlots = 32;

    FCharacterObject* Character;       // +0x6c 0 (written by the spawn)
    char* ActorName;                   // +0x70 0 (malloc'd by the spawn)
    float FloorHeight;                 // +0x74 0
    FMotionSlot Motions[kMotionSlots]; // +0x78
    FAimSlot Aims[kAimSlots];          // +0x278
};
