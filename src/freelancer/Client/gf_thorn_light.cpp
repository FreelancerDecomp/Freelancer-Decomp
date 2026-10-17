// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "gf_thorn_light.h"

#include "dacom.h"
#include "engine_light.h"
#include "gf_script.h"
#include "gf_script_object.h"
#include "x86math.h"

#include <cstdint>
#include <cstring>

namespace
{
    char const* const GScriptLightFile = "E:\\FL\\Scratch\\Source\\Client\\GF\\ScriptLight.cpp";

    GENRESULT const kThornReferenceMissing = static_cast<GENRESULT>(-1);

    // DAT_0066d27c -- this file's 3DMathEngine, acquired (FUN_005b73e0) on first use by each of
    // the three bodies that read it (0x00453168, 0x0045320d, 0x00453352).
    I3DMathEngine* G_ThornLightMath = nullptr;

    I3DMathEngine* ThornLightMath()
    {
        if (G_ThornLightMath == nullptr)
            G_ThornLightMath = Acquire3DMathEngine();
        return G_ThornLightMath;
    }

    FEngineLight* EngineLightOf(FGfLightWrapper* Wrapper)
    {
        return Wrapper->Light;
    }

    // The tail shared by LAB_00453150 / LAB_004531e0 / LAB_00453330: the direction carried into
    // the current orientation (3DMathEngine slot 11, TransformMatrix(out, Orient, Direction)),
    // LookMatrixYup of it, and FUN_00410f60 on the engine light when the wrapper holds one. The
    // wrapper itself is dereferenced without a test (`MOV ECX,[EDX]`).
    void PushLightOrientation(FThornLight& Self)
    {
        FVector Rotated;
        ThornLightMath()->TransformMatrix(Rotated, *reinterpret_cast<FMatrix const*>(&Self.Orient),
                                          *reinterpret_cast<FVector const*>(&Self.Direction));
        Vector Look;
        Look.x = Rotated.X;
        Look.y = Rotated.Y;
        Look.z = Rotated.Z;
        Matrix const LookOrient = LookMatrixYup(Look);
        FEngineLight* const Light = EngineLightOf(Self.Wrapper);
        if (Light != nullptr)
            EngineLightSetOrientation(Light, &LookOrient.E[0][0]);
    }
}

// ---------------------------------------------------------------------------------------
// Thorn_Light
// ---------------------------------------------------------------------------------------

// FUN_00452fa0. After the base ctor and the vptrs: stack copies of the snap's position (+0x08),
// orientation (+0x14) and the whole 0x24-dword light props (+0x44); FUN_00556fd0(&pos,
// &props+0x28 (the D3DLIGHT8), &props+0x0c, snap+0x40 light group) into +0x6c
// (0x0045304c..0x004530b1); +0x74 = the orientation, +0x98 = props+0x0c; then the first null
// registry slot (0x004530b6 loop, 0x40 slots) takes the wrapper and the enable byte
// props+0x20 != 0, and its index goes to +0x70 -- or -1 when all are taken.
FThornLight::FThornLight(Csys const& ScriptFrame, FGfScript* InOwner, FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Wrapper(nullptr),
      RegistryIndex(-1)
{
    float const* const Props = reinterpret_cast<float const*>(&Desc->Light);
    Wrapper = GfLightWrapperCreate(Desc->Pos, ThornLightPropsD3d(Props),
                                   ThornLightPropsTarget(Props), Desc->LtGrp);
    memcpy(&Orient, Desc->Orient, sizeof(Orient));
    Direction.x = Desc->Light.Direction[0];
    Direction.y = Desc->Light.Direction[1];
    Direction.z = Desc->Light.Direction[2];
    RegistryIndex = GfLightRegistryAdd(Wrapper, Desc->Light.On != 0);
}

// LAB_004531e0 (identity, RET 8). FUN_00410ef0 with Transform+0x24 when the wrapper holds a
// light (no script-frame transform), +0x74 = the rotation, then the direction push.
GENRESULT DACOM_API FThornLight::SetTransform(float const* Transform12)
{
    FEngineLight* const Light = EngineLightOf(Wrapper);
    if (Light != nullptr)
        EngineLightSetPosition(Light, Transform12 + 9);
    memcpy(&Orient, Transform12, sizeof(Orient));
    PushLightOrientation(*this);
    return GR_OK;
}

// LAB_004532d0 (identity, RET 8). Out+0x24 = the engine light's position (+0x98) when there is
// a light, Out+0 = +0x74.
GENRESULT DACOM_API FThornLight::GetTransform(float* OutTransform12)
{
    FEngineLight* const Light = EngineLightOf(Wrapper);
    if (Light != nullptr)
    {
        OutTransform12[9] = Light->Position[0];
        OutTransform12[10] = Light->Position[1];
        OutTransform12[11] = Light->Position[2];
    }
    memcpy(OutTransform12, &Orient, sizeof(Orient));
    return GR_OK;
}

// LAB_00453130 (geo facet, RET 8). FUN_00410ef0(Position) on the light when there is one.
GENRESULT DACOM_API FThornLight::SetPosition(float* Position)
{
    FEngineLight* const Light = EngineLightOf(Wrapper);
    if (Light != nullptr)
        EngineLightSetPosition(Light, Position);
    return GR_OK;
}

// LAB_00453150 (geo facet, RET 8). +0x74 = Orient9 (facet +0x68), then the direction push.
GENRESULT DACOM_API FThornLight::SetOrientation(float* Orient9)
{
    memcpy(&Orient, Orient9, sizeof(Orient));
    PushLightOrientation(*this);
    return GR_OK;
}

// LAB_00453280 (geo facet, RET 8). The light's +0x98 when there is one; otherwise nothing is
// written.
GENRESULT DACOM_API FThornLight::GetPosition(float* OutPosition)
{
    FEngineLight* const Light = EngineLightOf(Wrapper);
    if (Light != nullptr)
    {
        OutPosition[0] = Light->Position[0];
        OutPosition[1] = Light->Position[1];
        OutPosition[2] = Light->Position[2];
    }
    return GR_OK;
}

// LAB_004532b0 (geo facet, RET 8). REP MOVSD 9 from +0x74.
GENRESULT DACOM_API FThornLight::GetOrientation(float* OutOrient9)
{
    memcpy(OutOrient9, &Orient, sizeof(Orient));
    return GR_OK;
}

// LAB_00453310 (D3D-light facet, RET 8). Facet +0x5c = +0x70: when the index is not -1, the
// registry enable byte = On != 0.
GENRESULT DACOM_API FThornLight::SetOn(int On)
{
    if (RegistryIndex != -1)
        GfLightRegistrySetEnabled(RegistryIndex, On != 0);
    return GR_OK;
}

// LAB_004533d0 (D3D-light facet, RET 8). FUN_00411120(D3dLight8) on the light when there is one.
GENRESULT DACOM_API FThornLight::SetLight(void const* D3dLight8)
{
    FEngineLight* const Light = EngineLightOf(Wrapper);
    if (Light != nullptr)
        EngineLightSetLight(Light, static_cast<FD3dLight8 const*>(D3dLight8));
    return GR_OK;
}

// LAB_00453330 (D3D-light facet, RET 8). +0x98 = Dir3 (facet +0x84), then the direction push.
GENRESULT DACOM_API FThornLight::SetDirection(float* Dir3)
{
    Direction.x = Dir3[0];
    Direction.y = Dir3[1];
    Direction.z = Dir3[2];
    PushLightOrientation(*this);
    return GR_OK;
}

// LAB_004533f0 (D3D-light facet, RET 8). *OutOn = 0, then the enable byte (MOVZX) when the
// index is not -1.
GENRESULT DACOM_API FThornLight::GetOn(int* OutOn)
{
    *OutOn = 0;
    if (RegistryIndex != -1)
        *OutOn = GfLightRegistryIsEnabled(RegistryIndex) ? 1 : 0;
    return GR_OK;
}

// LAB_00453450 (D3D-light facet, RET 8). 0x1a dwords from the light's +0x0c (its D3DLIGHT8) when
// there is a light.
GENRESULT DACOM_API FThornLight::GetLight(void* OutD3dLight8)
{
    FEngineLight* const Light = EngineLightOf(Wrapper);
    if (Light != nullptr)
        memcpy(OutD3dLight8, &Light->D3d, sizeof(FD3dLight8));
    return GR_OK;
}

// LAB_00453420 (D3D-light facet, RET 8). +0x98.
GENRESULT DACOM_API FThornLight::GetDirection(float* OutDir3)
{
    OutDir3[0] = Direction.x;
    OutDir3[1] = Direction.y;
    OutDir3[2] = Direction.z;
    return GR_OK;
}

// LAB_00453100. The registry slot +0x70 and its enable byte cleared (no -1 test), +0x70 = -1,
// FUN_00557130(&+0x6c).
void FThornLight::Stop()
{
    GfLightRegistryRemove(RegistryIndex);
    RegistryIndex = -1;
    GfLightWrapperDestroy(&Wrapper);
}

// FUN_00450870.
void* FThornLight::GetEngineInstance()
{
    return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
}

// ---------------------------------------------------------------------------------------
// The reference forwarders (ICF-shared by the light, marker and sound references)
// ---------------------------------------------------------------------------------------

GENRESULT ThornReferenceSetPosition(FThornComponent* Target, float* Position)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->SetPosition(Position);
}

GENRESULT ThornReferenceSetOrientation(FThornComponent* Target, float* Orient9)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->SetOrientation(Orient9);
}

GENRESULT ThornReferenceGetPosition(FThornComponent* Target, float* OutPosition)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->GetPosition(OutPosition);
}

GENRESULT ThornReferenceGetOrientation(FThornComponent* Target, float* OutOrient9)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->GetOrientation(OutOrient9);
}

GENRESULT ThornReferenceSetTransform(FThornComponent* Target, float const* Xform12)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->SetTransform(Xform12);
}

GENRESULT ThornReferenceGetTransform(FThornComponent* Target, float* OutXform12)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->GetTransform(OutXform12);
}

void* ThornReferenceGetEngineInstance(FThornComponent* Target)
{
    if (Target == nullptr)
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    return Target->GetEngineInstance();
}

// ---------------------------------------------------------------------------------------
// Thorn_LightReference
// ---------------------------------------------------------------------------------------

// FUN_00453480. Base ctor, vptrs, then +0x6c = FUN_00456790(DAT_0066d2d0, 5, Desc->Name) --
// the snap's name, not the malloc'd copy (0x004534f2); none found: FDUMP(0x100002, "invalid
// light reference", ScriptLight.cpp, 0x177, Desc->Name).
FThornLightReference::FThornLightReference(Csys const& ScriptFrame, FGfScript* InOwner,
                                           FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Target(nullptr)
{
    Target = G_GfScriptManager.FindCreatedEntity(kThornEntityLight, Desc->Name);
    if (Target == nullptr)
        FDUMP(0x100002, "%s(%d) : *** WARNING: invalid light reference to name %s",
              GScriptLightFile, 0x177, Desc->Name);
}

// LAB_00457c00 / LAB_00457c20 -- identity 11 / 12 of the target, -1 without one.
GENRESULT DACOM_API FThornLightReference::SetTransform(float const* Transform12)
{
    return ThornReferenceSetTransform(Target, Transform12);
}

GENRESULT DACOM_API FThornLightReference::GetTransform(float* OutTransform12)
{
    return ThornReferenceGetTransform(Target, OutTransform12);
}

// LAB_00457bc0 / LAB_00457be0 / LAB_00453560 / LAB_00453580 -- geo 3..6 of the target.
GENRESULT DACOM_API FThornLightReference::SetPosition(float* Position)
{
    return ThornReferenceSetPosition(Target, Position);
}

GENRESULT DACOM_API FThornLightReference::SetOrientation(float* Orient9)
{
    return ThornReferenceSetOrientation(Target, Orient9);
}

GENRESULT DACOM_API FThornLightReference::GetPosition(float* OutPosition)
{
    return ThornReferenceGetPosition(Target, OutPosition);
}

GENRESULT DACOM_API FThornLightReference::GetOrientation(float* OutOrient9)
{
    return ThornReferenceGetOrientation(Target, OutOrient9);
}

// LAB_004535a0 .. LAB_00453640 -- facet +0x58 (= +0x6c) non-null: the target's D3D-light facet
// (+0x14), same slot; otherwise -1.
GENRESULT DACOM_API FThornLightReference::SetOn(int On)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return static_cast<ID3DLightControl*>(Target)->SetOn(On);
}

GENRESULT DACOM_API FThornLightReference::SetLight(void const* D3dLight8)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->SetLight(D3dLight8);
}

GENRESULT DACOM_API FThornLightReference::SetDirection(float* Dir3)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->SetDirection(Dir3);
}

GENRESULT DACOM_API FThornLightReference::GetOn(int* OutOn)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->GetOn(OutOn);
}

GENRESULT DACOM_API FThornLightReference::GetLight(void* OutD3dLight8)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->GetLight(OutD3dLight8);
}

GENRESULT DACOM_API FThornLightReference::GetDirection(float* OutDir3)
{
    if (Target == nullptr)
        return kThornReferenceMissing;
    return Target->GetDirection(OutDir3);
}

// LAB_00450cd0.
void FThornLightReference::Stop()
{
    Target = nullptr;
}

// LAB_00453550 -- the target's identity slot 17, or -1.
void* FThornLightReference::GetEngineInstance()
{
    return ThornReferenceGetEngineInstance(Target);
}
