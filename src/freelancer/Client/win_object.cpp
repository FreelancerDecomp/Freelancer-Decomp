// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "win_object.h"

#include "win3d_engine_submit.h"

#include "DALib.h"
#include "render/IRenderPipeline.h"

WinObject::WinObject()
{
    // FUN_004c9060: the Win3D part (inlined), the holder (FUN_00420d40) with five 1.0f and
    // anim -1, the tint zeroed and bits 0..2 of +0xa4 cleared.
    ClientMeshHolderConstruct(&MeshHolder);
    MeshHolder.UserFloats[4] = 1.0f;
    MeshHolder.UserFloats[3] = 1.0f;
    MeshHolder.UserFloats[2] = 1.0f;
    MeshHolder.UserFloats[1] = 1.0f;
    MeshHolder.UserFloats[0] = 1.0f;
    MeshHolder.AnimOrArchIndex = -1;
    TintRgba[3] = 0;
    TintRgba[2] = 0;
    TintRgba[1] = 0;
    TintRgba[0] = 0;
    DrawFlags = static_cast<unsigned char>(DrawFlags & 0xf8);
}

WinObject::~WinObject()
{
    // FUN_004c9230: nothing but the member dtor FUN_0059ef20 (0x004c9259, ~FClientMeshHolder:
    // close the animation script, release a bound instance); the Win3D dtor runs as the base.
}

void WinObject::SetPosition(float const* NewPosition)
{
    // FUN_004c91a0: copy to +0x60, then tail-jump into FUN_00420fd0 on the holder with the
    // same argument (Engine SetInstancePosition; no UpdateInstance).
    Position[0] = NewPosition[0];
    Position[1] = NewPosition[1];
    Position[2] = NewPosition[2];
    ClientMeshHolderSetPosition(&MeshHolder, NewPosition);
}

void WinObject::Destroy()
{
    // FUN_004c9150.
    if (ClientMeshHolderHasInstance(&MeshHolder))
        ClientUiMeshHolderRelease(&MeshHolder);
    Flags = static_cast<unsigned char>(Flags & 0xfc);
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        Child->Destroy();
    Win3DUnregister(this);
}

void WinObject::Update()
{
    // FUN_004c91d0: the Win3D child walk (FUN_005a17b0 inlined), then FUN_00421050 on the
    // holder with this+0x3c -- Engine SetInstanceMatrix (+0x9c) from the orientation and
    // position, which are contiguous there.
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        if ((Child->Flags & kWin3DActive) != 0)
            Child->Update();
        else
            Child->UpdateInactive();
        Child = Next;
    }
    DALib::Engine->SetInstanceMatrix(MeshHolder.EngineInstance, Orientation);
}

bool WinObject::LoadMeshAndCreate(Win3D* Parent, char const* MeshPath,
                                  char const* MaterialPath)
{
    // FUN_0059f1c0.
    if (!ClientMeshLoadFromPath(&MeshHolder, MeshPath, MaterialPath, false))
        return false;
    return Create(Parent, nullptr);
}

void WinObject::Draw()
{
    // FUN_0059f120.
    if ((Flags & kWin3DVisible) == 0)
        return;
    Win3DDrawChildren(this);

    // Plain pass unless bit1: render states 0x17 = 8 and 0xe = 1, then FUN_0059efa0.
    if ((DrawFlags & 2) == 0)
    {
        DALib::Pipeline->SetRenderState(D3DRS_ZFUNC, 8);
        DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 1);
        SubmitWin3DEngineInstance(&MeshHolder, Win3DGetCamera(), 0, 1.0f, 0);
    }

    // Tinted pass when bit0: FUN_00434630 with front culling, the +0xa5 tint and lighting
    // unless bit1.
    if ((DrawFlags & 1) != 0)
    {
        unsigned char const bLighting = static_cast<unsigned char>((~DrawFlags >> 1) & 1);
        SubmitWin3DTintedEngineInstanceWrapped(Win3DGetCamera(), MeshHolder.EngineInstance, 1,
                                               TintRgba, bLighting, 1.0f, 0);
    }
}
