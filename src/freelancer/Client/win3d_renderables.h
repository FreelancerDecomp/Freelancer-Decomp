// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The helpers FUN_0059e300 (Win3DDrawAll, win3d.cpp) calls around its draw loop.
//
// The raw-offset renderable registry that used to live here (RegisterRenderable /
// DrawRenderables on void*) is gone: the registry is Win3D's own list DAT_0067dbe4, typed as
// Win3D* in win3d.cpp.

struct IRPVertexBuffer;
struct IRPIndexBuffer;
struct IRPDraw;

// One vertex of the immediate batchers: position and diffuse, FVF 0x42, 0x10 bytes.
struct FImmedVertex
{
    float X;             // +0x00
    float Y;             // +0x04
    float Z;             // +0x08
    unsigned long Color; // +0x0c
};

// The immediate line batchers DAT_0067dd90 (A, the wire controls' lines, drawn through the
// Win3D camera's perspective) and DAT_0067dd68 (B, the HUD's 2D lines -- FUN_005a2970 sets its
// +0x25 byte, so it draws through the camera's ortho box). 0x28 bytes. Static initializers
// LAB_005a28c0 / LAB_005a2900 zero +0x00..+0x18, set Dirty 1 and +0x25 0.
struct FImmedBatcher
{
    IRPVertexBuffer* Vb;         // +0x00  QI "1.11_IRPVertexBuffer" (FUN_005a2df0)
    IRPIndexBuffer* Ib;          // +0x04  QI "1.11_IRPIndexBuffer"
    IRPDraw* Draw;               // +0x08  QI "1.11_IRPDraw"
    FImmedVertex* VertBase;      // +0x0c  operator new(0xc000): 0xc00 vertices
    unsigned short* IndexBase;   // +0x10  operator new(0x3000): 0x1800 indices
    FImmedVertex* VertCursor;    // +0x14
    unsigned short* IndexCursor; // +0x18
    int VertCount;               // +0x1c
    int IndexCount;              // +0x20
    unsigned char Dirty;         // +0x24  set by every flush; FUN_005a29b0 rewinds on it
    unsigned char bOrtho;        // +0x25  flush through FUN_004227d0 instead of FUN_00422950
    unsigned char Pad26[2];
};

extern FImmedBatcher G_ImmedBatchA; // DAT_0067dd90
extern FImmedBatcher G_ImmedBatchB; // DAT_0067dd68

// FUN_005a2a40 (thiscall on a batcher, RET 0x14) -- one thick line A -> B: both batchers are
// flushed and rewound first when 12 more indices or 8 more vertices would not fit, then eight
// vertices (A and B in Color, each also pushed Width to either side along the perpendicular in
// EdgeColor) and twelve indices.
void ImmedBatchAddLine(FImmedBatcher* Batch, float const* A, float const* B,
                       unsigned long Color, unsigned long EdgeColor, float Width);

// FUN_005a2970 -- FUN_005a2df0 on both batchers, then B's +0x25 = 1. Caller: FUN_0059d5a0.
void Win3DImmedBatchCreateAll();
// FUN_005a2990 -- FUN_005a2e80 on both batchers. Caller: FUN_0059d6d0.
void Win3DImmedBatchDestroyAll();

// FUN_005a29b0 -- reset both immediate batchers (DAT_0067dd90 / DAT_0067dd68) whose dirty
// byte is set.
void Win3DImmedBatchReset();

// FUN_005a2a20 -- FUN_005a2ee0 on DAT_0067dd90, then on DAT_0067dd68.
void Win3DImmedBatchFlushAll();

// FUN_005a2940 (cdecl) -- a 2D line from A to B (x, y, z each) into the second immediate batcher
// (DAT_0067dd68) through FUN_005a2a40 (RET 0x14): both batchers are flushed first when 12 more
// indices or 8 more vertices would not fit; then eight vertices -- A and B in Color, each also
// offset Width to either side along the perpendicular (atan2 of B - A plus DAT_005d3e88) in
// EdgeColor -- and twelve indices. Callers: HUD_Targetable_Objects' bracket drawer
// (0x004eb06a..0x004ebc8a) and lead-marker cross (FUN_004f2070).
void Win3DImmedDrawLine(float const* A, float const* B, unsigned long Color,
                        unsigned long EdgeColor, float Width);

// FUN_00559c10 -- ButtonMontage.ini loaded once (FUN_00559db0).
void ButtonMontageEnsure();

// FUN_00559c20 -- flush the "buttons" material batch with RS 0x17 / 0xe saved around it.
void ButtonMontageFlushBatch();

// FUN_00417f10 -- 2D overlay prep and the font-atlas screen-quad flush.
void UiOverlayPrepPipeline();

// FUN_00563af0 -- IME focus overlay pass (defined in client_ime_focus.cpp): with a singleton that
// has input boxes, IMEFocus::Draw(0) under a 2D projection, point filtering, depth func ALWAYS /
// no depth write and no culling, then the "IMEbkground" and "IMEtext" screen-quad lists flushed.
void ImeFocusDrawPass();

// FUN_0045a9f0 -- Tip Draw and other overlay callers: flush particle batch, reset
// camera dirty, end/begin sorted pass, then UI overlay pipeline prep.
void Win3DFlushBeforeOverlayDraw();

// Incremented by scene-object draw stubs (smoke verification).
extern int G_DrawCallCount;
