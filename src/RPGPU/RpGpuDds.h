// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// DDS header reading for GetImageInfo (RpGpuDds.cpp). Kept apart from the pipeline so the
// texture decoder (x64-client-plan.md, step 4.1) can grow here.

#include "render/rp_types.h"

// Fills Info (D3DXIMAGE_INFO's layout) from the DDS file in SrcData. False for anything D3DX would
// reject: not a DDS file, a format Shading does not load this way, or a truncated file.
bool ReadDdsImageInfo(FRpImageInfo* Info, unsigned int SrcSize, const void* SrcData);

// A DDS cube map, as LoadCubemap takes it: the edge length, the levels in the file, the RP8
// pixel format (pfenum) and where each face's levels start in the file (faces +X, -X, +Y, -Y,
// +Z, -Z, each with its whole chain, as DDS stores them). False for anything that is not a
// square cube map in a format RPGPU keeps (P8 is not).
struct FDdsCube
{
    unsigned int Edge = 0;
    unsigned int Levels = 0;
    unsigned int Format = 0;
    unsigned int Offset[6][16] = {}; // bytes from the start of the file
    unsigned int Bytes[16] = {};     // one face's bytes at each level
};
bool ReadDdsCube(FDdsCube* Out, unsigned int SrcSize, const void* SrcData);
