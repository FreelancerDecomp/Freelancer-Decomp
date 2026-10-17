// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Win3D / OpenGL layer helpers: the glClear translation and the camera apply.
// glEnable / glDisable (FUN_00421dd0 / FUN_00421ed0) are Win3DEnable / Win3DDisable in
// win3d_immediate.h.

// FUN_004215e0 -- the glClear translation. Its argument is a GL-style bit set, but retail only
// ever looks at AH, so exactly two bits mean anything and every other bit (including anything
// above bit 15) is ignored:
//
//     0x4000 -> clear the colour buffer      0x0100 -> clear the depth buffer
//
// Those map to D3DCLEAR_TARGET (1) and D3DCLEAR_ZBUFFER (2) for IRenderPipeline::ClearBuffers.
// The call sites pass 0x100 (depth only, e.g. camera slot 0x78), 0x4000 (colour only) and
// 0x4100 (both).
enum EWin3DClearBits
{
    kWin3DClearColour = 0x4000,
    kWin3DClearDepth  = 0x0100
};

void ClearWin3DBuffers(unsigned int Bits);

// FUN_00564f90 (cdecl, one pointer to four 0..255 bytes in R,G,B,A order). Sets the pipeline's
// clear colour (each byte times 1/255) and clears both the colour and depth buffers -- retail
// tail-calls FUN_004215e0(0x4100) after setting the colour, which is exactly ClearWin3DBuffers
// with both bits set.
void ClearWin3DBuffersToColor(unsigned char const* Rgba);

// FUN_00422950 (cdecl, one argument): the camera's perspective through FUN_00422820 (fovY,
// aspect, znear, zfar from slots 0x1c / 0x28 / 0x20 / 0x24), then its packed view (slot 0x40)
// copied into DAT_006648dc; pipeline state 7 is READ -- non-zero sends the copy through
// SetModelview, zero sets an identity view and the slot-0x40 view as the world.
struct ICamera;
void Win3DApplyCamera(ICamera* Camera);
