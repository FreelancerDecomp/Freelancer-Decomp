// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// RP9's private copies of the two Direct3D 8 structures that leave the pipeline as data:
// IRenderPipeline::GetDeviceInfo hands out the 0x520-byte adapter record in RP8's D3D8 shape
// (D3DADAPTER_IDENTIFIER8 + D3DDISPLAYMODE + D3DCAPS8 + four dwords), and the MGSDB scanner
// reads its "Caps" keys at D3DCAPS8 offsets. d3d8types.h cannot be included next to
// d3d9types.h, so the layouts are re-declared here from the DirectX 8.1 SDK
// (dx8/include/d3d8types.h line 1583, dx8/include/d3d8caps.h line 22; both under #pragma pack(4))
// and pinned with static_asserts. They are filled from D3DADAPTER_IDENTIFIER9 / D3DCAPS9.

#include "render/d3d8_min.h"

#include <stddef.h>

#pragma pack(push, 4)

// D3DADAPTER_IDENTIFIER8 (MAX_DEVICE_IDENTIFIER_STRING = 512 in both SDKs).
struct FRp9Identifier8
{
    char Driver[512];
    char Description[512];
    LARGE_INTEGER DriverVersion;
    DWORD VendorId;
    DWORD DeviceId;
    DWORD SubSysId;
    DWORD Revision;
    GUID DeviceIdentifier;
    DWORD WHQLLevel;
};

// D3DCAPS8. Member names are the SDK's, so code written against D3DCAPS8 reads unchanged.
struct FRp9Caps8
{
    D3DDEVTYPE DeviceType;
    UINT AdapterOrdinal;
    DWORD Caps;
    DWORD Caps2;
    DWORD Caps3;
    DWORD PresentationIntervals;
    DWORD CursorCaps;
    DWORD DevCaps;
    DWORD PrimitiveMiscCaps;
    DWORD RasterCaps;
    DWORD ZCmpCaps;
    DWORD SrcBlendCaps;
    DWORD DestBlendCaps;
    DWORD AlphaCmpCaps;
    DWORD ShadeCaps;
    DWORD TextureCaps;
    DWORD TextureFilterCaps;
    DWORD CubeTextureFilterCaps;
    DWORD VolumeTextureFilterCaps;
    DWORD TextureAddressCaps;
    DWORD VolumeTextureAddressCaps;
    DWORD LineCaps;
    DWORD MaxTextureWidth;
    DWORD MaxTextureHeight;
    DWORD MaxVolumeExtent;
    DWORD MaxTextureRepeat;
    DWORD MaxTextureAspectRatio;
    DWORD MaxAnisotropy;
    float MaxVertexW;
    float GuardBandLeft;
    float GuardBandTop;
    float GuardBandRight;
    float GuardBandBottom;
    float ExtentsAdjust;
    DWORD StencilCaps;
    DWORD FVFCaps;
    DWORD TextureOpCaps;
    DWORD MaxTextureBlendStages;
    DWORD MaxSimultaneousTextures;
    DWORD VertexProcessingCaps;
    DWORD MaxActiveLights;
    DWORD MaxUserClipPlanes;
    DWORD MaxVertexBlendMatrices;
    DWORD MaxVertexBlendMatrixIndex;
    float MaxPointSize;
    DWORD MaxPrimitiveCount;
    DWORD MaxVertexIndex;
    DWORD MaxStreams;
    DWORD MaxStreamStride;
    DWORD VertexShaderVersion;
    DWORD MaxVertexShaderConst;
    DWORD PixelShaderVersion;
    float MaxPixelShaderValue;
};

#pragma pack(pop)

static_assert(sizeof(FRp9Identifier8) == 0x42C, "D3DADAPTER_IDENTIFIER8 is 0x42C bytes");
static_assert(offsetof(FRp9Identifier8, Description) == 0x200, "Identifier8 Description");
static_assert(offsetof(FRp9Identifier8, DriverVersion) == 0x400, "Identifier8 DriverVersion");
static_assert(offsetof(FRp9Identifier8, VendorId) == 0x408, "Identifier8 VendorId");
static_assert(offsetof(FRp9Identifier8, DeviceId) == 0x40C, "Identifier8 DeviceId");
static_assert(offsetof(FRp9Identifier8, SubSysId) == 0x410, "Identifier8 SubSysId");
static_assert(offsetof(FRp9Identifier8, Revision) == 0x414, "Identifier8 Revision");
static_assert(offsetof(FRp9Identifier8, DeviceIdentifier) == 0x418, "Identifier8 DeviceIdentifier");
static_assert(offsetof(FRp9Identifier8, WHQLLevel) == 0x428, "Identifier8 WHQLLevel");

static_assert(sizeof(FRp9Caps8) == 0xD4, "D3DCAPS8 is 0xD4 bytes");
static_assert(offsetof(FRp9Caps8, Caps) == 0x08, "Caps8 Caps");
static_assert(offsetof(FRp9Caps8, Caps2) == 0x0C, "Caps8 Caps2");
static_assert(offsetof(FRp9Caps8, DevCaps) == 0x1C, "Caps8 DevCaps");
static_assert(offsetof(FRp9Caps8, RasterCaps) == 0x24, "Caps8 RasterCaps");
static_assert(offsetof(FRp9Caps8, TextureCaps) == 0x3C, "Caps8 TextureCaps");
static_assert(offsetof(FRp9Caps8, TextureFilterCaps) == 0x40, "Caps8 TextureFilterCaps");
static_assert(offsetof(FRp9Caps8, LineCaps) == 0x54, "Caps8 LineCaps");
static_assert(offsetof(FRp9Caps8, MaxTextureWidth) == 0x58, "Caps8 MaxTextureWidth");
static_assert(offsetof(FRp9Caps8, MaxTextureHeight) == 0x5C, "Caps8 MaxTextureHeight");
static_assert(offsetof(FRp9Caps8, MaxAnisotropy) == 0x6C, "Caps8 MaxAnisotropy");
static_assert(offsetof(FRp9Caps8, StencilCaps) == 0x88, "Caps8 StencilCaps");
static_assert(offsetof(FRp9Caps8, MaxTextureBlendStages) == 0x94, "Caps8 MaxTextureBlendStages");
static_assert(offsetof(FRp9Caps8, VertexProcessingCaps) == 0x9C, "Caps8 VertexProcessingCaps");
static_assert(offsetof(FRp9Caps8, MaxPrimitiveCount) == 0xB4, "Caps8 MaxPrimitiveCount");
static_assert(offsetof(FRp9Caps8, PixelShaderVersion) == 0xCC, "Caps8 PixelShaderVersion");
static_assert(offsetof(FRp9Caps8, MaxPixelShaderValue) == 0xD0, "Caps8 MaxPixelShaderValue");

// D3DCAPS9 starts with the D3DCAPS8 member list unchanged (PixelShader1xMaxValue is the renamed
// MaxPixelShaderValue); Rp9Caps8FromCaps9 copies that prefix and then rewrites the bits whose
// meaning moved (see RP9.cpp).
static_assert(offsetof(D3DCAPS9, RasterCaps) == 0x24, "D3DCAPS9 prefix RasterCaps");
static_assert(offsetof(D3DCAPS9, MaxTextureBlendStages) == 0x94, "D3DCAPS9 prefix stages");
static_assert(offsetof(D3DCAPS9, PixelShader1xMaxValue) == 0xD0, "D3DCAPS9 prefix end");
