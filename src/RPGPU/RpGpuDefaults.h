// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// RP8's [RenderPipeline] keys and defaults, which RPGPU reads the same way (docs/re/rpgpu.md).
// The ids are the pipeline-state ids, D3DRENDERSTATETYPE and D3DTEXTURESTAGESTATETYPE values;
// the names are the ini keys. Same rows as RP8's kPipelineProperties, kRenderStateProperties and
// kStageStateProperties (src/RP8/RP8.cpp).

struct FRpGpuStateInit
{
    int Id;
    const char* Key;
    unsigned long Default;
};

// Pipeline state ids 0..13. Id 6 is RP8's blend-stage limit, set by Startup and not read from
// the ini.
inline constexpr FRpGpuStateInit kRpGpuPipelineStates[] = {
    {0x0, "TEXTURE", 0x1},
    {0x1, "LIGHTING", 0x1},
    {0x2, "TEXTURE_LOD", 0x1},
    {0x3, "TEXTURE_ALLOW_8BIT", 0x0},
    {0x4, "TEXTURE_ALLOW_32BIT", 0x1},
    {0x5, "TEXTURE_ALLOW_DXT", 0x1},
    {0x7, "VIEWSPACE_LIGHTS", 0x1},
    {0x8, "CLEAR_COLOR", 0x0},
    {0x9, "CLEAR_ZDEPTH", 0x3F800000},
    {0xA, "CLEAR_STENCIL", 0xFFFFFFFF},
    {0xB, "STATE_CACHE", 0x1},
    {0xC, "BROKEN_MOD2X", 0x0},
    {0xD, "BROKEN_FLIP", 0x0},
};

inline constexpr FRpGpuStateInit kRpGpuRenderStates[] = {
    {0x7, "ZENABLE", 0x1},
    {0x8, "FILLMODE", 0x3},
    {0x9, "SHADEMODE", 0x2},
    {0xA, "LINEPATTERN", 0x0},
    {0xE, "ZWRITEENABLE", 0x1},
    {0xF, "ALPHATESTENABLE", 0x0},
    {0x10, "LASTPIXEL", 0x1},
    {0x13, "SRCBLEND", 0x2},
    {0x14, "DESTBLEND", 0x1},
    {0x16, "CULLMODE", 0x1},
    {0x17, "ZFUNC", 0x2},
    {0x18, "ALPHAREF", 0x0},
    {0x19, "ALPHAFUNC", 0x8},
    {0x1A, "DITHERENABLE", 0x0},
    {0x1B, "ALPHABLENDENABLE", 0x0},
    {0x1C, "FOGENABLE", 0x0},
    {0x1D, "SPECULARENABLE", 0x0},
    {0x1E, "ZVISIBLE", 0x1},
    {0x22, "FOGCOLOR", 0x0},
    {0x23, "FOGTABLEMODE", 0x1},
    {0x24, "FOGSTART", 0x0},
    {0x25, "FOGEND", 0x0},
    {0x26, "FOGDENSITY", 0x0},
    {0x28, "EDGEANTIALIAS", 0x0},
    {0x2F, "ZBIAS", 0x0},
    {0x30, "RANGEFOGENABLE", 0x0},
    {0x34, "STENCILENABLE", 0x0},
    {0x35, "STENCILFAIL", 0x1},
    {0x36, "STENCILZFAIL", 0x1},
    {0x37, "STENCILPASS", 0x1},
    {0x38, "STENCILFUNC", 0x8},
    {0x39, "STENCILREF", 0xFFFFFFFF},
    {0x3A, "STENCILMASK", 0xFFFFFFFF},
    {0x3B, "STENCILWRITEMASK", 0xFFFFFFFF},
    {0x3C, "TEXTUREFACTOR", 0x0},
    {0x80, "WRAP0", 0x0},
    {0x81, "WRAP1", 0x0},
    {0x82, "WRAP2", 0x0},
    {0x83, "WRAP3", 0x0},
    {0x84, "WRAP4", 0x0},
    {0x85, "WRAP5", 0x0},
    {0x86, "WRAP6", 0x0},
    {0x87, "WRAP7", 0x0},
    {0x88, "CLIPPING", 0x1},
    {0x89, "LIGHTING", 0x0},
    {0x8B, "AMBIENT", 0x0},
    {0x8C, "FOGVERTEXMODE", 0x0},
    {0x8D, "COLORVERTEX", 0x0},
    {0x8E, "LOCALVIEWER", 0x0},
    {0x8F, "NORMALIZENORMALS", 0x0},
    {0x91, "DIFFUSEMATERIALSOURCE", 0x1},
    {0x92, "SPECULARMATERIALSOURCE", 0x2},
    {0x93, "AMBIENTMATERIALSOURCE", 0x2},
    {0x94, "EMISSIVEMATERIALSOURCE", 0x0},
    {0x97, "VERTEXBLEND", 0x0},
    {0x98, "CLIPPLANEENABLE", 0x0},
    {0x99, "SOFTWAREVERTEXPROCESSING", 0x0},
    {0x9A, "POINTSIZE", 0x3F800000},
    {0x9B, "POINTSIZE_MIN", 0x3F800000},
    {0x9C, "POINTSPRITEENABLE", 0x0},
    {0x9D, "POINTSCALEENABLE", 0x0},
    {0x9E, "POINTSCALE_A", 0x3F800000},
    {0x9F, "POINTSCALE_B", 0x0},
    {0xA0, "POINTSCALE_C", 0x0},
    {0xA1, "MULTISAMPLEANTIALIAS", 0x1},
    {0xA2, "MULTISAMPLEMASK", 0xFFFFFFFF},
    {0xA3, "PATCHEDGESTYLE", 0x0},
    {0xA4, "PATCHSEGMENTS", 0x0},
    {0xA5, "DEBUGMONITORTOKEN", 0x0},
    {0xA6, "POINTSIZE_MAX", 0x3F800000},
    {0xA7, "INDEXEDVERTEXBLENDENABLE", 0x0},
    {0xA8, "COLORWRITEENABLE", 0xF},
    {0xAA, "TWEENFACTOR", 0x0},
    {0xAB, "BLENDOP", 0x1},
    {0xAC, "POSITIONORDER", 0x1},
    {0xAD, "NORMALORDER", 0x1},
};

// Stage 0 modulates its texture (COLOROP 4, ALPHAOP 2 = SELECTARG1); the other stages start
// disabled (1 / 1).
inline constexpr FRpGpuStateInit kRpGpuFirstStageOps[] = {
    {0x1, "COLOROP", 0x4},
    {0x4, "ALPHAOP", 0x2},
};
inline constexpr FRpGpuStateInit kRpGpuOtherStageOps[] = {
    {0x1, "COLOROP", 0x1},
    {0x4, "ALPHAOP", 0x1},
};

inline constexpr FRpGpuStateInit kRpGpuStageStates[] = {
    {0x2, "COLORARG1", 0x2},
    {0x3, "COLORARG2", 0x1},
    {0x5, "ALPHAARG1", 0x2},
    {0x6, "ALPHAARG2", 0x1},
    {0x7, "BUMPENVMAT00", 0x0},
    {0x8, "BUMPENVMAT01", 0x0},
    {0x9, "BUMPENVMAT10", 0x0},
    {0xA, "BUMPENVMAT11", 0x0},
    {0xB, "TEXCOORDINDEX", 0x0},
    {0xD, "ADDRESSU", 0x1},
    {0xE, "ADDRESSV", 0x1},
    {0xF, "BORDERCOLOR", 0x0},
    {0x10, "MAGFILTER", 0x2},
    {0x11, "MINFILTER", 0x2},
    {0x12, "MIPFILTER", 0x1},
    {0x13, "MIPMAPLODBIAS", 0x0},
    {0x14, "MAXMIPLEVEL", 0x0},
    {0x15, "MAXANISOTROPY", 0x1},
    {0x16, "BUMPENVLSCALE", 0x0},
    {0x17, "BUMPENVLOFFSET", 0x0},
    {0x18, "TEXTURETRANSFORMFLAGS", 0x0},
    {0x19, "ADDRESSW", 0x1},
    {0x1A, "COLORARG0", 0x1},
    {0x1B, "ALPHAARG0", 0x1},
    {0x1C, "RESULTARG", 0x1},
};
