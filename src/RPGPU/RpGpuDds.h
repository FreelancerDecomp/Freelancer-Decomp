// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// DDS header reading for GetImageInfo (RpGpuDds.cpp). Kept apart from the pipeline so the
// texture decoder (x64-client-plan.md, step 4.1) can grow here.

#include "render/rp_types.h"

// Fills Info (D3DXIMAGE_INFO's layout) from the DDS file in SrcData. False for anything D3DX would
// reject: not a DDS file, a format Shading does not load this way, or a truncated file.
bool ReadDdsImageInfo(FRpImageInfo* Info, unsigned int SrcSize, const void* SrcData);
