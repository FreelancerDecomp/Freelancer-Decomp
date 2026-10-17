// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client\Space\SpaceTerrainRenderComp.cpp, the asteroid-field billboard half: the rock
// sprites a CliAsteroid field draws beyond its real asteroid cubes (FCliAsteroid +0x198 flag,
// +0x19C block, see cli_asteroid.h), and the vertex-buffer batch they are drawn through.
//
// Retail call path: CliAsteroid tick FUN_005225d0 -> (Flag198, ASTEROID_EXT > 1, [EFFECTS],
// FUN_0054e040) -> FUN_00522700 ambient pass -> FUN_0054e900(camera, field ExclusionMgr, 2).
//
// The batch is one static object, DAT_006797d0 (static init LAB_0054dad0 = FUN_0054daf0(0x142),
// atexit LAB_0054db50 -> FUN_0054db70). It is an immediate-mode writer over
// IVertexBufferManager::LockVertexMemory: three {pointer, stride} cursors into the locked
// vertices, a current colour and texture coordinate, and a two-vertex save area for the
// loop-closing and quad-splitting modes (flags +0x58 bits 0..2).

#include "cli_asteroid.h"
#include "render/IRenderPipeline.h"

struct FSpaceCamera;
struct IVertexBufferManager;

namespace Exclusion
{
    class AsteroidExclusionZoneMgr;
}

// A {write pointer, stride} pair inside the batch (+0x6c / +0x74 / +0x7c). The retail dtor
// zeroes both fields: FUN_0054db70 0x0054dbbc..0x0054dbce clears them in reverse member order,
// which is what three member destructors produce.
struct FBillboardVertexCursor
{
    explicit FBillboardVertexCursor(int InStride);
    ~FBillboardVertexCursor();

    // FUN_00552b10 (thiscall, RET 4): Ptr += Stride. The argument is never read.
    void Advance(int Unused);

    unsigned char* Ptr; // +0x00
    int Stride;         // +0x04
};

// One saved vertex, 0x18 bytes (batch +0x28 and +0x40).
struct FBillboardSavedVertex
{
    float Pos[3];       // +0x00
    unsigned Color;     // +0x0c
    float U;            // +0x10
    float V;            // +0x14
};

static_assert(sizeof(FBillboardSavedVertex) == 0x18, "saved vertex 0x18");

// DAT_006797d0, 0x84 bytes.
class FAsteroidBillboardBatch
{
public:
    // FUN_0054daf0 (thiscall, RET 4).
    explicit FAsteroidBillboardBatch(unsigned InFvf);
    // FUN_0054db70.
    ~FAsteroidBillboardBatch();

    // FUN_0054f960 (thiscall, RET 8): release the held pipeline (+0x0c) and vertex-buffer
    // manager (+0x10), then hold and AddRef the new ones that are not null.
    void SetInterfaces(IRenderPipeline* InPipeline, IVertexBufferManager* InVbMgr);
    // FUN_0054f9c0: flags bits 0..2, the counters, colour -1, UV 0, primitive 0x7fffffff.
    void Reset();
    // FUN_0054f830 (thiscall, RET 8): the three cursors from the locked vertex pointer. The
    // strides come from the batch's own lock FVF (+0x18); the offsets from InFvf.
    void BindCursors(unsigned char* Base, unsigned InFvf);
    // FUN_0054fd30 (thiscall, RET 0xc): one vertex at the current colour and UV.
    void Vertex(float X, float Y, float Z);

    unsigned Prim;                  // +0x00 0x7fffffff when idle, 4 (triangle list) in use
    int Count;                      // +0x04 vertices written
    unsigned Fvf;                   // +0x08 ctor argument, 0x142
    IRenderPipeline* Pipeline;      // +0x0c AddRef'd
    IVertexBufferManager* VbMgr;    // +0x10 AddRef'd
    FVertexMemoryLock Lock;         // +0x14 borrowed listing and locked vertices
    int LockedCount;                // +0x24
    FBillboardSavedVertex Saved[2]; // +0x28
    unsigned char Flags;            // +0x58 bit 0 save-first, bit 1 close-loop, bit 2 quads
    unsigned char Pad59[3];
    unsigned QuadCounter;           // +0x5c
    float U;                        // +0x60
    float V;                        // +0x64
    unsigned Color;                 // +0x68 D3DCOLOR
    FBillboardVertexCursor PosCursor;   // +0x6c
    FBillboardVertexCursor ColorCursor; // +0x74
    FBillboardVertexCursor UvCursor;    // +0x7c

private:
    FAsteroidBillboardBatch(FAsteroidBillboardBatch const&) = delete;
    FAsteroidBillboardBatch& operator=(FAsteroidBillboardBatch const&) = delete;
};

// DAT_006797d0.
extern FAsteroidBillboardBatch G_AsteroidBillboardBatch;

// FUN_0054f800 (cdecl): SetRenderState for each {state, value} pair up to a zero state.
void BillboardApplyRenderStates(IRenderPipeline* Pipeline, unsigned const* Table);

// FUN_0054e130 (thiscall on the billboard block, RET 0x24): the ambient colour (D3DRS_AMBIENT *
// ambient_intensity / 255 * tinted ambient * color_shift) into all five outputs, then for up
// to MaxLights directional (type 3) lights within 100 of Pos, each light's ambient share and
// its tinted diffuse colour weighted by how far the light faces each of the five directions
// Forward + Right, Forward - Right, Forward + Up, Forward - Up and Forward; each output
// clamped to 1.0 per component.
void BillboardComputeLighting(FAsteroidBillboardBlock* Bb, float const* Pos,
                              float const* Forward, float const* Up, float const* Right,
                              float* OutCenter, float* OutCorner0, float* OutCorner1,
                              float* OutCorner2, float* OutCorner3);

// FUN_0054f9f0 (thiscall on the billboard block, RET 0x14; the block is not read): one rock as
// a single triangle into G_AsteroidBillboardBatch -- base P -/+ A, apex P + B, A and B the
// rock-size-scaled Right / Up (swapped by bSwapAxes, B negated by bNegateApex), the base
// vertices in CornerColor with a corner-UV pair picked by the flip bits, the apex in
// CenterColor at UV (0.5, 0.5), all at the rock's alpha.
void BillboardEmitRock(FAsteroidBillboardBlock* Bb, FAsteroidBillboardRock* Rock,
                       float const* Right, float const* Up, float const* CornerColor,
                       float const* CenterColor);

// FUN_0054e900 (thiscall on the billboard block, RET 0xc). Caller FUN_005225d0 passes the
// field's ExclusionMgr (+0xdc) and ExcludeType 2 (billboards).
void BillboardDraw(FAsteroidBillboardBlock* Bb, FSpaceCamera* Camera,
                   Exclusion::AsteroidExclusionZoneMgr* Excl, int ExcludeType);
