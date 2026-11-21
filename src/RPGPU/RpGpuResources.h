// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// RPGPU's texture and buffer objects (RpGpuResources.cpp).

#include "x64_low_arena.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// CPU memory for one texture image, on the ordinary heap: LockTexture hands its address out in
// FRpTextureLock::Bits, which is pointer-sized on x64.
struct FRpGpuImage
{
    std::vector<unsigned char> Bits;
    unsigned int Width = 0;
    unsigned int Height = 0;
    unsigned int Pitch = 0;
};

// A texture: its size, RP8 pixel-format number (pfenum) and one image per mip level (per face
// for a cube). Callers keep the handle in 32-bit fields, so on x64 the handle object (not its
// images) is allocated below 2 GB (x64_low_arena.h).
struct FRpGpuTexture
{
    FL_LOW_ARENA_NEW
    FRpGpuTexture() = default;
    ~FRpGpuTexture() = default;
    FRpGpuTexture(const FRpGpuTexture&) = delete;
    FRpGpuTexture& operator=(const FRpGpuTexture&) = delete;

    unsigned int Width = 0;
    unsigned int Height = 0;
    unsigned int Format = 0;
    unsigned int Levels = 0;
    bool bCube = false;
    std::vector<FRpGpuImage> Images; // level, or face for a cube
    // A level changed since the last upload to the backend (or it never went up).
    bool bGpuStale = true;
    // The backend image version of the last upload (0: none). Each upload gets a new one, so draws
    // recorded before a change keep the image they saw.
    std::uint64_t GpuVersion = 0;
};

// A vertex or index buffer in CPU memory. Pool buffers (one per FVF) and the scratch index
// buffer are rings: a lock starts at the cursor and wraps to 0 when it does not fit.
struct FRpGpuBuffer
{
    FL_LOW_ARENA_NEW
    unsigned int Fvf = 0;     // vertex buffers
    unsigned int Stride = 0;  // bytes per vertex or index
    unsigned int Count = 0;   // capacity in vertices or indices
    bool bRing = false;
    bool bLocked = false;
    unsigned int Cursor = 0;  // ring position, in elements
    std::vector<unsigned char> Data;
};
