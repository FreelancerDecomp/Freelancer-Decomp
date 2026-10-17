// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Growable XYZ|DIFFUSE|TEX1 (FVF 0x142, stride 0x18) vertex + 16-bit index batch.
// Retail: FUN_0046eab0 / FUN_0046ecd0 / FUN_0046ebc0 / FUN_004fc9a0 / FUN_004fca50 /
// FUN_004fcb40, object layout at DAT_00674c7c (Effect.cpp static).
//
// Distinct from screen_quad_state (DAT_00616498 FlashLight/HUD path) and from
// win3d_immediate (fixed 0x3000 OpenGL emulator). Do not merge.

struct IRenderPipeline;

// One CPU vertex — matches retail stride 0x18 / FVF 0x142.
struct FEffectVertex
{
    float X;              // +0x00
    float Y;              // +0x04
    float Z;              // +0x08
    unsigned char B;      // +0x0C  D3DCOLOR byte order B,G,R,A
    unsigned char G;      // +0x0D
    unsigned char R;      // +0x0E
    unsigned char A;      // +0x0F
    float U;              // +0x10
    float V;              // +0x14
};

static_assert(sizeof(FEffectVertex) == 0x18, "Effect vertex stride 0x18");

// Retail object starting at DAT_00674c7c. Methods take this; Effect.cpp uses the
// global G_EffectVertexBatch.
struct FEffectVertexBatch
{
    unsigned Type;                // +0x00  LAB_004f9360 writes 1
    FEffectVertex* Vertices;      // +0x04  DAT_00674c80
    int VertCount;                // +0x08  DAT_00674c84
    unsigned Fvf;                 // +0x0C  DAT_00674c88 = 0x142
    unsigned short* Indices;      // +0x10  DAT_00674c8c
    int IndexCount;               // +0x14  DAT_00674c90
    IRenderPipeline* Pipeline;  // +0x18  DAT_00674c94 (AddRef'd during flush)
    int Growth;                   // +0x1C  DAT_00674c98 = 0x80
    unsigned VertCapacity;        // +0x20  DAT_00674c9c
    unsigned IndexCapacity;       // +0x24  DAT_00674ca0
    unsigned char Flag28;         // +0x28  DAT_00674ca4
    unsigned char bStripMode;     // +0x29  DAT_00674ca5
    unsigned char Pad2A;
    unsigned char Pad2B;
    unsigned StripCounter;        // +0x2C  DAT_00674ca8
    unsigned char ColorR;         // +0x30  DAT_00674cac  (written to vert +0xE)
    unsigned char ColorG;         // +0x31  DAT_00674cad  (vert +0xD)
    unsigned char ColorB;         // +0x32  DAT_00674cae  (vert +0xC)
    unsigned char ColorA;         // +0x33  DAT_00674caf
    float U;                      // +0x34  DAT_00674cb0
    float V;                      // +0x38  DAT_00674cb4
};

static_assert(sizeof(FEffectVertexBatch) == 0x3C, "batch object 0x3C");

// DAT_00674c7c — Effect.cpp frame batch for beams / cross-billboards.
extern FEffectVertexBatch G_EffectVertexBatch;

// DAT_00674c78 — once-flag for atexit dtor LAB_004f93f0.
extern unsigned char G_EffectVertexBatchDtorOnce;

// DAT_00674f50 — lazy I3DMathEngine* cache for Effect/Beam TransformXform (+0x28).
struct I3DMathEngine;
extern I3DMathEngine* G_EffectMathEngine;

// LAB_004f9360 + atexit LAB_004f93f0.
void EffectVertexBatchStaticInit();
void EffectVertexBatchStaticShutdown();

// FUN_0046e9c0 — the batch's destructor: release Pipeline (+0x18) and free both buffers.
// Retail callers: the texture panels' atexit LAB_0050a020 (0x0050a039, DAT_00675018 -- the port's
// FPanelVertexBatcher in texture_panels.cpp, whose +0x04/+0x10/+0x18 are asserted to line up;
// G_PanelBatcherAtExit calls this on it) and
// the unwind funclet of the chat background draw FUN_0045e470 (0x005ba383; chat_text_input.cpp
// FChatBgBatch's destructor). The effect batch's own atexit LAB_004f93f0 inlines the same body.
void EffectVertexBatchDestroy(FEffectVertexBatch* Batch);

// FUN_0046ecd0 — grow Vertices when VertCapacity <= VertCount (step Growth, stride 0x18).
void EffectVertexBatchGrowVertices(FEffectVertexBatch* Batch);

// FUN_0046ebc0 — zero VertCount/IndexCount/strip state; ensure index capacity >= 1.
void EffectVertexBatchReset(FEffectVertexBatch* Batch);

// FUN_0046eab0 — append one vertex from current Color/UV; returns new index (count-1).
int EffectVertexBatchAppend(FEffectVertexBatch* Batch, float X, float Y, float Z);

// FUN_004fcb40 — ensure IndexCapacity >= IndexCount + Needed (growth Growth*3).
void EffectVertexBatchEnsureIndices(FEffectVertexBatch* Batch, int Needed);

// FUN_004fc9a0 — push one triangle (3 indices); grows as needed.
void EffectVertexBatchPushTriangle(FEffectVertexBatch* Batch, unsigned short I0,
                                   unsigned short I1, unsigned short I2);

// FUN_004fca50 — push quad as two triangles (I0,I1,I2) + (I0,I2,I3).
void EffectVertexBatchPushQuad(FEffectVertexBatch* Batch, unsigned short I0,
                               unsigned short I1, unsigned short I2, unsigned short I3);

// FUN_004fc950 — colour bytes = low byte of ftol(component * 255.0), alpha 0xff.
void EffectVertexBatchSetColor(FEffectVertexBatch* Batch, float R, float G, float B);

// FUN_004f9de0 (cdecl: camera) — RS/TSS tables (DAT_005d9a04/9a3c/99dc/99f4), FUN_00425640
// texture, the camera's view (vt+0x40) as the modelview (+0x7c), DrawIndexedPrimitiveUp(4, FVF,
// …) of G_EffectVertexBatch, restore.
struct ICamera;
void EffectVertexBatchFlush(ICamera* Cam);
