// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "win3d_render.h"
#include "win3d_immediate.h"

#include "DALib.h"
#include "globals.h"
#include "render/IRenderPipeline.h"
#include "space_state.h"
#include "common_misc.h"

#include <string.h>

#include <windows.h>

// The depth cache and the glEnable/glDisable translations live in win3d_immediate.cpp (the
// port of OpenGL.cpp). This file used to carry a second, null-guarded copy of glDisable
// (ApplyWin3DRenderLayer) plus a port-only first-frame bootstrap (PrepareWin3DRenderPass,
// ApplyWin3DViewport, ApplyWin3DSceneDepthState, BootstrapWin3DFirstFrame) that no retail
// function corresponds to and nothing called any more. All of it is gone; the glDisable call
// sites use Win3DDisable directly.

void ClearWin3DBuffers(unsigned int Bits)
{
    // FUN_004215e0, 0x004215e0..0x0042161f. No null test: 0x004215f9 and 0x0042160d load
    // DALib::Pipeline and call through its vtable directly.
    IRenderPipeline* const Pipe = DALib::Pipeline;

    // 0x004215e7 / 0x004215f1: retail tests AH, not the whole word -- `TEST AH,0x40` is bit
    // 0x4000 and `TEST AH,0x01` is bit 0x0100. Nothing outside those two bits can affect the
    // result, so the masks below are the whole of the translation.
    unsigned long ClearFlags = (Bits & kWin3DClearColour) != 0 ? 1u : 0u;
    if ((Bits & kWin3DClearDepth) != 0)
        ClearFlags |= 2u;

    // 0x0042160a: depth range is reset to the full [0, 1] on every clear, before the clear
    // itself -- retail pushes 0.0f and 1.0f as raw dwords 0x00000000 / 0x3f800000.
    Pipe->SetDepthRange(0.0f, 1.0f);
    // 0x0042161b: the pipeline is re-read from DALib::Pipeline for the second call.
    DALib::Pipeline->ClearBuffers(ClearFlags, nullptr);
}

void ClearWin3DBuffersToColor(unsigned char const* Rgba)
{
    // FUN_00564f90 (cdecl, one pointer to four bytes R, G, B, A): each byte FILD, FMUL double
    // 1/255 (DAT_005c89c8), FSTP float, then FUN_004214d0(R, G, B, A), tail-jumping into
    // FUN_004215e0(0x4100) -- set clear colour, clear colour+depth.
    double const Scale = 1.0 / 255.0;
    Win3DSetPipelineClearColor(static_cast<float>(Rgba[0] * Scale), static_cast<float>(Rgba[1] * Scale),
                               static_cast<float>(Rgba[2] * Scale), static_cast<float>(Rgba[3] * Scale));
    ClearWin3DBuffers(kWin3DClearColour | kWin3DClearDepth);
}

void Win3DApplyCamera(ICamera* Camera)
{
    // FUN_00422950, 00422950..00422a65. No null checks: retail dereferences both the camera and
    // DALib::Pipeline unconditionally. Identity locals at [ESP+0xc..0x38]: 1.0 at +0xc/+0x1c/+0x2c
    // = elements 0/4/8, everything else 0; SetWorld gets EBX, the slot-0x40 pointer itself.
    float const ZFar = Camera->GetZFar();         // slot 0x24
    float const ZNear = Camera->GetZNear();       // slot 0x20
    float const Aspect = Camera->GetAspect();     // slot 0x28
    float const FovY = Camera->GetFovYDeg();      // slot 0x1c
    Win3DSetPerspective(FovY, Aspect, ZNear, ZFar); // FUN_00422820

    float const* const View12 = Camera->GetViewTransform(); // slot 0x40
    memcpy(GWin3DModelview, View12, sizeof(float) * 12);    // REP MOVSD 0xc -> DAT_006648dc

    IRenderPipeline* const Pipe = DALib::Pipeline;
    int bViewspace = 1;                           // 004229a6: the out value is seeded with 1
    Pipe->GetPipelineState(7, &bViewspace);
    if (bViewspace != 0)
    {
        Pipe->SetModelview(GWin3DModelview);      // +0x7c
        return;
    }

    // 004229de..00422a5c: identity view (+0x74), then the camera's view as the world (+0x6c).
    float Identity[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                          0.0f, 0.0f, 0.0f};
    Pipe->SetView(Identity);
    Pipe->SetWorld(View12);
}
