// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// The port's own spelling of the Direct3D 8 types the render pipeline interface carries
// (render/IRenderPipeline.h), for modules that cannot include the D3D headers: RPGPU today and
// the Linux / macOS client later (x64-client-plan.md, phase 6). No Windows or D3D header.
//
// Every type has the layout of the D3D type it stands for, so the same bytes cross the
// interface whichever spelling each side was compiled with. IRenderPipeline.h uses these types
// only under RP_PORT_TYPES; every other module keeps seeing the D3D types themselves (it checks
// the equivalence there).

#include <cstddef>
#include <cstdint>

// D3DCOLORVALUE.
struct FRpColorValue
{
    float r;
    float g;
    float b;
    float a;
};

// D3DVECTOR.
struct FRpVector
{
    float x;
    float y;
    float z;
};

// D3DLIGHT8 (0x68 bytes). Type is D3DLIGHTTYPE: 1 point, 2 spot, 3 directional.
struct FRpLight
{
    std::uint32_t Type;
    FRpColorValue Diffuse;
    FRpColorValue Specular;
    FRpColorValue Ambient;
    FRpVector Position;
    FRpVector Direction;
    float Range;
    float Falloff;
    float Attenuation0;
    float Attenuation1;
    float Attenuation2;
    float Theta;
    float Phi;
};

// D3DMATERIAL8 (0x44 bytes).
struct FRpMaterial
{
    FRpColorValue Diffuse;
    FRpColorValue Ambient;
    FRpColorValue Specular;
    FRpColorValue Emissive;
    float Power;
};

// D3DXIMAGE_INFO as D3DX 8.1 and 9 lay it out (0x1C bytes). Format is a D3DFORMAT,
// ResourceType a D3DRESOURCETYPE (3 texture, 4 volume, 5 cube), ImageFileFormat a
// D3DXIMAGE_FILEFORMAT (4 DDS).
struct FRpImageInfo
{
    std::uint32_t Width;
    std::uint32_t Height;
    std::uint32_t Depth;
    std::uint32_t MipLevels;
    std::uint32_t Format;
    std::uint32_t ResourceType;
    std::uint32_t ImageFileFormat;
};

// A render-state id: D3DRENDERSTATETYPE's value, one dword.
using RpRenderStateId = std::uint32_t;

// A window: an opaque handle, the same type as Windows' HWND (struct HWND__*), so a module
// that includes windows.h passes its HWND unchanged.
struct HWND__;
using RpWindowHandle = HWND__*;

// The Direct3D 8 render-state ids RP8 reads from the ini (render/rp8_semantics.h), by their
// D3DRS_ names.
namespace RpRs
{
    inline constexpr RpRenderStateId kZENABLE = 0x7;
    inline constexpr RpRenderStateId kFILLMODE = 0x8;
    inline constexpr RpRenderStateId kSHADEMODE = 0x9;
    inline constexpr RpRenderStateId kLINEPATTERN = 0xA;
    inline constexpr RpRenderStateId kZWRITEENABLE = 0xE;
    inline constexpr RpRenderStateId kALPHATESTENABLE = 0xF;
    inline constexpr RpRenderStateId kLASTPIXEL = 0x10;
    inline constexpr RpRenderStateId kSRCBLEND = 0x13;
    inline constexpr RpRenderStateId kDESTBLEND = 0x14;
    inline constexpr RpRenderStateId kCULLMODE = 0x16;
    inline constexpr RpRenderStateId kZFUNC = 0x17;
    inline constexpr RpRenderStateId kALPHAREF = 0x18;
    inline constexpr RpRenderStateId kALPHAFUNC = 0x19;
    inline constexpr RpRenderStateId kDITHERENABLE = 0x1A;
    inline constexpr RpRenderStateId kALPHABLENDENABLE = 0x1B;
    inline constexpr RpRenderStateId kFOGENABLE = 0x1C;
    inline constexpr RpRenderStateId kSPECULARENABLE = 0x1D;
    inline constexpr RpRenderStateId kZVISIBLE = 0x1E;
    inline constexpr RpRenderStateId kFOGCOLOR = 0x22;
    inline constexpr RpRenderStateId kFOGTABLEMODE = 0x23;
    inline constexpr RpRenderStateId kFOGSTART = 0x24;
    inline constexpr RpRenderStateId kFOGEND = 0x25;
    inline constexpr RpRenderStateId kFOGDENSITY = 0x26;
    inline constexpr RpRenderStateId kEDGEANTIALIAS = 0x28;
    inline constexpr RpRenderStateId kZBIAS = 0x2F;
    inline constexpr RpRenderStateId kRANGEFOGENABLE = 0x30;
    inline constexpr RpRenderStateId kSTENCILENABLE = 0x34;
    inline constexpr RpRenderStateId kSTENCILFAIL = 0x35;
    inline constexpr RpRenderStateId kSTENCILZFAIL = 0x36;
    inline constexpr RpRenderStateId kSTENCILPASS = 0x37;
    inline constexpr RpRenderStateId kSTENCILFUNC = 0x38;
    inline constexpr RpRenderStateId kSTENCILREF = 0x39;
    inline constexpr RpRenderStateId kSTENCILMASK = 0x3A;
    inline constexpr RpRenderStateId kSTENCILWRITEMASK = 0x3B;
    inline constexpr RpRenderStateId kTEXTUREFACTOR = 0x3C;
    inline constexpr RpRenderStateId kWRAP0 = 0x80;
    inline constexpr RpRenderStateId kWRAP1 = 0x81;
    inline constexpr RpRenderStateId kWRAP2 = 0x82;
    inline constexpr RpRenderStateId kWRAP3 = 0x83;
    inline constexpr RpRenderStateId kWRAP4 = 0x84;
    inline constexpr RpRenderStateId kWRAP5 = 0x85;
    inline constexpr RpRenderStateId kWRAP6 = 0x86;
    inline constexpr RpRenderStateId kWRAP7 = 0x87;
    inline constexpr RpRenderStateId kCLIPPING = 0x88;
    inline constexpr RpRenderStateId kLIGHTING = 0x89;
    inline constexpr RpRenderStateId kAMBIENT = 0x8B;
    inline constexpr RpRenderStateId kFOGVERTEXMODE = 0x8C;
    inline constexpr RpRenderStateId kCOLORVERTEX = 0x8D;
    inline constexpr RpRenderStateId kLOCALVIEWER = 0x8E;
    inline constexpr RpRenderStateId kNORMALIZENORMALS = 0x8F;
    inline constexpr RpRenderStateId kDIFFUSEMATERIALSOURCE = 0x91;
    inline constexpr RpRenderStateId kSPECULARMATERIALSOURCE = 0x92;
    inline constexpr RpRenderStateId kAMBIENTMATERIALSOURCE = 0x93;
    inline constexpr RpRenderStateId kEMISSIVEMATERIALSOURCE = 0x94;
    inline constexpr RpRenderStateId kVERTEXBLEND = 0x97;
    inline constexpr RpRenderStateId kCLIPPLANEENABLE = 0x98;
    inline constexpr RpRenderStateId kSOFTWAREVERTEXPROCESSING = 0x99;
    inline constexpr RpRenderStateId kPOINTSIZE = 0x9A;
    inline constexpr RpRenderStateId kPOINTSIZE_MIN = 0x9B;
    inline constexpr RpRenderStateId kPOINTSPRITEENABLE = 0x9C;
    inline constexpr RpRenderStateId kPOINTSCALEENABLE = 0x9D;
    inline constexpr RpRenderStateId kPOINTSCALE_A = 0x9E;
    inline constexpr RpRenderStateId kPOINTSCALE_B = 0x9F;
    inline constexpr RpRenderStateId kPOINTSCALE_C = 0xA0;
    inline constexpr RpRenderStateId kMULTISAMPLEANTIALIAS = 0xA1;
    inline constexpr RpRenderStateId kMULTISAMPLEMASK = 0xA2;
    inline constexpr RpRenderStateId kPATCHEDGESTYLE = 0xA3;
    inline constexpr RpRenderStateId kPATCHSEGMENTS = 0xA4;
    inline constexpr RpRenderStateId kDEBUGMONITORTOKEN = 0xA5;
    inline constexpr RpRenderStateId kPOINTSIZE_MAX = 0xA6;
    inline constexpr RpRenderStateId kINDEXEDVERTEXBLENDENABLE = 0xA7;
    inline constexpr RpRenderStateId kCOLORWRITEENABLE = 0xA8;
    inline constexpr RpRenderStateId kTWEENFACTOR = 0xAA;
    inline constexpr RpRenderStateId kBLENDOP = 0xAB;
    inline constexpr RpRenderStateId kPOSITIONORDER = 0xAC;
    inline constexpr RpRenderStateId kNORMALORDER = 0xAD;
}

// The sizes are the same on every platform: no field is a pointer.
static_assert(sizeof(FRpLight) == 0x68, "FRpLight is D3DLIGHT8's 0x68 bytes");
static_assert(offsetof(FRpLight, Position) == 0x34, "FRpLight: Position at +0x34");
static_assert(offsetof(FRpLight, Range) == 0x4C, "FRpLight: Range at +0x4C");
static_assert(offsetof(FRpLight, Phi) == 0x64, "FRpLight: Phi at +0x64");
static_assert(sizeof(FRpMaterial) == 0x44, "FRpMaterial is D3DMATERIAL8's 0x44 bytes");
static_assert(offsetof(FRpMaterial, Power) == 0x40, "FRpMaterial: Power at +0x40");
static_assert(sizeof(FRpImageInfo) == 0x1C, "FRpImageInfo is D3DXIMAGE_INFO's seven dwords");
