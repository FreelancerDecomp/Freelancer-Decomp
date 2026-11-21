// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// DDS header reading for GetImageInfo (RpGpuDds.cpp). Kept apart from the pipeline so the
// texture decoder (x64-client-plan.md, step 4.1) can grow here.

#include <cstdint>

// Fills Info, D3DXIMAGE_INFO's seven dwords (width, height, depth, mip levels, D3DFORMAT,
// resource type, file format), from the DDS file in SrcData. False for anything D3DX would
// reject: not a DDS file, a format Shading does not load this way, or a truncated file.
bool ReadDdsImageInfo(std::uint32_t* Info, unsigned int SrcSize, const void* SrcData);
