// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Thorn_Component (RTTI .?AVThorn_Component@@, E:\FL\Scratch\Source\Client\Gf\ScriptBase.cpp) --
// the base of every object a Thorn script entity is bound to in freelancer.exe. Thirteen classes
// derive from it (Camera, CameraReference, Compound, Deformable, Light, LightReference, Marker,
// MarkerReference, Psys, PlayerEnginePsys, Scene, Sound, PlayerEngineSound, SoundReference),
// each in its own gf_thorn_*.h.
//
// LAYOUT. Nine facets, one vptr each, in the order of the QueryInterface table at 0x005cca20
// (thorn_controls.h lists it). The identity vtable (facet +0x00, ICompoundControl) carries the
// object's own thiscall virtuals after ICompoundControl's thirteen slots: 13 the scalar deleting
// destructor, 14..22 below. Every facet's IDAComponent slots are the shared LAB_004500d0 /
// LAB_00450180 / LAB_00450190; the eight other facets reach them through the compiler's
// this-adjustor thunks 0x004501c0..0x00450330 (`SUB [ESP+4],N; JMP`), which C++ multiple
// inheritance produces for us.
//
// CONSTRUCTION. The Gf script spawn FUN_0044ed90 and its factories FUN_0044d630..FUN_0044e3c0
// (gf_script_spawn.cpp) new the concrete class per entity type.

#include "common_csys.h"
#include "thorn_controls.h"
#include "thorn_iface.h"

struct FGfScript;
struct ICamera;

static_assert(sizeof(void*) == 4, "the facet offsets of the QI table (0x005cca20) are x86 offsets");

// The "not implemented" answer of every default slot (LAB_00454190, LAB_0044fcc0..LAB_0044fcf0:
// MOV EAX,0xfffffff9). dacom.h has no name for it.
static const GENRESULT kThornSlotNotImplemented = static_cast<GENRESULT>(0xFFFFFFF9);

struct FThornComponent : ICompoundControl,
                         ISoundControl,
                         IIKControl,
                         IGeoTransformControl,
                         ICameraControl,
                         ID3DLightControl,
                         IPSysControl,
                         IFogControl,
                         IReverbControl
{
    // FUN_0044fbe0 (thiscall, RET 0xc). EntityIndex -1, RefCount 1, the script's frame copied,
    // Owner, the entity's type, a malloc_strcpy of its name (+0x38), the reference bit (Flags & 1).
    FThornComponent(Csys const& ScriptFrame, FGfScript* InOwner, FThornEntitySnap const* Desc);

    // 13: FUN_0044fd80 (FUN_0044fd10 is the scalar deleting variant) -- malloc_strfree(Name).
    virtual ~FThornComponent();

    // --- IDAComponent, one body for all nine facets ---------------------------------------
    // LAB_004500d0 (RET 0xc) -- the table at 0x005cca20, exact strcmp. A hit AddRefs the facet
    // (slot 1) and hands it out, 0; a miss writes null and answers -3.
    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    // LAB_00450180 -- ++RefCount.
    unsigned long DACOM_API AddRef() override;
    // LAB_00450190 -- see the .cpp: the last release frees the memory WITHOUT the destructor.
    unsigned long DACOM_API Release() override;

    // --- ICompoundControl defaults (Thorn_Component 0x005cc6e0..0x005cc704) ------------------
    GENRESULT DACOM_API StartMotion(void* Desc) override;                 // 3  LAB_00454190
    GENRESULT DACOM_API StopMotion(void* Desc) override;                  // 4  LAB_00454190
    GENRESULT DACOM_API SetFloorHeight(float Height) override;            // 5  LAB_00451370: 0
    GENRESULT DACOM_API AdvanceTime(int DeltaMs) override;                // 6  LAB_00451370: 0
    GENRESULT DACOM_API ConnectHardpoints(void* Desc) override;           // 7  LAB_00454190
    GENRESULT DACOM_API DisconnectHardpoints(void* Desc) override;        // 8  LAB_00454190
    GENRESULT DACOM_API GetHardpointXform(int TargetType, char const* Name,
                                          float* OutXform12) override;    // 9  LAB_0044fcd0
    GENRESULT DACOM_API GetFloorHeight(float* OutHeight) override;        // 10 LAB_0044fcb0
    GENRESULT DACOM_API SetTransform(float const* Transform12) override;  // 11 LAB_00454190
    GENRESULT DACOM_API GetTransform(float* OutTransform12) override;     // 12 LAB_00454190

    // --- ISoundControl defaults (0x005cc6ac..0x005cc6c8) --------------------------------------
    GENRESULT DACOM_API StartSound(void* Desc, int TimeMs) override;      // 3  LAB_0044fce0
    GENRESULT DACOM_API StopSound(void* Desc) override;                   // 4..8 LAB_00454190
    GENRESULT DACOM_API SetAttenuation(float Attenuation) override;
    GENRESULT DACOM_API SetPan(float Pan) override;
    GENRESULT DACOM_API GetAttenuation(float* OutAttenuation) override;
    GENRESULT DACOM_API GetPan(float* OutPan) override;
    GENRESULT DACOM_API SetAudioProps(float const* Props8, int Mask) override; // 9  LAB_0044fce0
    GENRESULT DACOM_API GetAudioProps(float* Props8, int Mask) override;       // 10 LAB_0044fce0

    // --- IIKControl defaults (0x005cc690..0x005cc698) ----------------------------------------
    GENRESULT DACOM_API StartIk(void* Desc, float* Position, float* Orientation) override;  // LAB_0044fcd0
    GENRESULT DACOM_API StopIk(void* Desc) override;                                        // LAB_00454190
    GENRESULT DACOM_API UpdateIk(void* Desc, float* Position, float* Orientation) override; // LAB_0044fcd0

    // --- IGeoTransformControl defaults (all LAB_00454190) ------------------------------------
    GENRESULT DACOM_API SetPosition(float* Position) override;
    GENRESULT DACOM_API SetOrientation(float* Orient9) override;
    GENRESULT DACOM_API GetPosition(float* OutPosition) override;
    GENRESULT DACOM_API GetOrientation(float* OutOrient9) override;

    // --- ICameraControl defaults (all LAB_00454190) ------------------------------------------
    GENRESULT DACOM_API SetFov(const float* Fov) override;
    GENRESULT DACOM_API CameraSlot4(void* Arg) override;
    GENRESULT DACOM_API SetAspect(const float* Aspect) override;
    GENRESULT DACOM_API SetNear(const float* Near) override;
    GENRESULT DACOM_API SetFar(const float* Far) override;
    GENRESULT DACOM_API GetFov(float* OutFov) override;
    GENRESULT DACOM_API CameraSlot9(void* Arg) override;
    GENRESULT DACOM_API GetAspect(float* OutAspect) override;
    GENRESULT DACOM_API GetNear(float* OutNear) override;
    GENRESULT DACOM_API GetFar(float* OutFar) override;

    // --- ID3DLightControl defaults (0x005cc610..0x005cc624, all LAB_00454190) -----------------
    GENRESULT DACOM_API SetOn(int On) override;
    GENRESULT DACOM_API SetLight(void const* D3dLight8) override;
    GENRESULT DACOM_API SetDirection(float* Dir3) override;
    GENRESULT DACOM_API GetOn(int* OutOn) override;
    GENRESULT DACOM_API GetLight(void* OutD3dLight8) override;
    GENRESULT DACOM_API GetDirection(float* OutDir3) override;

    // --- IPSysControl defaults (0x005cc5ec..0x005cc5fc) --------------------------------------
    GENRESULT DACOM_API Start() override;                                 // 3  LAB_0044fcc0
    GENRESULT DACOM_API StopEffect() override;                            // 4  LAB_0044fcc0
    GENRESULT DACOM_API Tick(int DeltaMs) override;                       // 5..7 LAB_00454190
    GENRESULT DACOM_API SetSparam(float Value) override;
    GENRESULT DACOM_API GetSparam(float* OutValue) override;

    // --- IFogControl defaults (0x005cc5b4..0x005cc5d8) ---------------------------------------
    GENRESULT DACOM_API SetOn(unsigned char On) override;                 // 3..6 LAB_00454190
    GENRESULT DACOM_API GetOn(unsigned char* OutOn) override;
    GENRESULT DACOM_API SetColor(int const* Rgb3) override;
    GENRESULT DACOM_API GetColor(int* OutRgb3) override;
    GENRESULT DACOM_API SetRange(float Start, float End, float Density) override; // 7 LAB_0044fcd0
    GENRESULT DACOM_API GetRange(float* OutStart, float* OutEnd,
                                 float* OutDensity) override;                     // 8 LAB_0044fcd0
    GENRESULT DACOM_API SetTableMode(unsigned char Table, unsigned int Mode) override; // 9 LAB_0044fce0
    GENRESULT DACOM_API GetTableMode(unsigned char* OutTable,
                                     unsigned int* OutMode) override;             // 10 LAB_0044fce0
    GENRESULT DACOM_API SetPacked(unsigned char const* Packed32) override; // 11, 12 LAB_00454190
    GENRESULT DACOM_API GetPacked(unsigned char* Out32) override;
    GENRESULT DACOM_API SetRangeValue(const float* Start, const float* End,
                                      const float* Density) override; // modern 13 +0x34

    // --- IReverbControl defaults (0x005cc59c / 0x005cc5a0, LAB_0044fcf0) ----------------------
    GENRESULT DACOM_API SetReverb(unsigned int Environment, float Volume, float Decay,
                                  float Damping) override;
    GENRESULT DACOM_API GetReverb(float* OutEnvironment, float* OutVolume, float* OutDecay,
                                  float* OutDamping) override;

    // --- the identity table's own thiscall virtuals (Thorn_Component 0x005cc70c..0x005cc72c) --
    // 14 (+0x38) -- the script stops: the entity lets go of what it holds. Base: RET.
    virtual void Stop();
    // 15 (+0x3c) -- the script's spawned table. Base: RET 8.
    virtual void SetSpawnTable(FThornComponent** Table, int Count);
    // 16 (+0x40) -- the actor named Name, if this entity is it. Base: XOR AL,AL; RET 0xc.
    virtual bool FindActor(char const* ActorName, void** OutActorField, void** OutActor);
    // 17 (+0x44) -- the engine instance the entity moves. Base: _purecall.
    virtual void* GetEngineInstance() = 0;
    // 18 (+0x48) -- the effects pass (FUN_0044f970 0x0044f98f, per spawned entity). Base: RET 4.
    virtual void DrawEffects(ICamera* Camera);
    // 19 (+0x4c) -- the scene entity whose ambient override the room object takes (RoomObject
    // +0x0c, FUN_00559590). Base: RET 4.
    virtual void SetScene(void* Scene);
    // 20 (+0x50) -- RoomObject +0x10 / +0x11 / +0x12 / +0x14. Base: RET 0x10.
    virtual void SetFlags(unsigned char bVisible, unsigned char bLit, unsigned char bFog,
                          unsigned char bFlag14);
    // 21 (+0x54) -- re-apply the entity's sound state to what plays it. Only Thorn_Sound has a
    // body (LAB_00457630); the Gf fade tick (FUN_00456650 0x0045671c) sends it to every spawned
    // entity of the playing scripts. Base: RET.
    virtual void RefreshSound();
    // 22 (+0x58) -- a background (music / ambience) sound. Only Thorn_Sound has a body
    // (LAB_00456ea0); the spawn skips the initial property writes for one (0x0044f0d5).
    // Base: FUN_0044fd00 (XOR AL,AL; RET).
    virtual bool IsBackgroundSound();

    // FUN_00450070 (thiscall, RET 8) -- a created (not reference) entity of this type and name.
    bool MatchesEntity(int InType, char const* InName) const;

    // FUN_0044fdd0 (thiscall, RET 0x10) -- DALib::Hardpoint slot 6 from the Owner's entity
    // IndexB's hardpoint HpB to entity IndexA's HpA; "Cannot connect" FDUMP on failure.
    void ConnectEntityHardpoints(int IndexA, char const* HpA, int IndexB, char const* HpB);
    // FUN_0044ff30 (thiscall, RET 0x10) -- DALib::Engine slot 48 on the same pair; "Cannot
    // disconnect" FDUMP unless it answers 1.
    void DisconnectEntityHardpoints(int IndexA, char const* HpA, int IndexB, char const* HpB);

    int EntityIndex;          // +0x24 -1 (written by the spawn)
    unsigned long RefCount;   // +0x28 1
    Csys Frame;               // +0x2c the owning script's frame (FGfScript +0x00)
    FGfScript* Owner;         // +0x5c
    int Type;                 // +0x60 kThornEntity* (FThornEntitySnap +0x00)
    char* Name;               // +0x64 malloc'd copy of FThornEntitySnap +0x38
    unsigned char bReference; // +0x68 FThornEntitySnap +0x04 bit 0
};
