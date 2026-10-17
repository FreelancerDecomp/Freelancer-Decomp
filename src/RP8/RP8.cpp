// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "RP8.h"
#include "Mgsdb.h"
#include "readfile.h"
#include "x86math.h"
#include "msvcrt_compat.h"

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ddraw.h>
#include <map>
#include <new>
#include <string>
#include <utility>
#include <vector>

// IProfileParser conveniences RP8 wants on top of the raw interface: a zeroed buffer and a
// "did it actually write anything" answer. They used to be inline helpers in
// render/IProfileParser.h that dispatched by vtable slot number (5/6/7/8); the interface is
// typed now, so these are just RP8's own calling conventions.
static bool ParserGetString(IProfileParser* Parser, void* Section, const char* Key, char* Buf,
                            unsigned int BufSize)
{
    if (Parser == nullptr || Section == nullptr || Key == nullptr || Buf == nullptr || BufSize == 0)
        return false;
    memset(Buf, 0, BufSize);
    Parser->GetString(Section, Key, Buf, BufSize);
    return Buf[0] != '\0';
}

static void* ParserOpenSection(IProfileParser* Parser, const char* Name, unsigned int Flags)
{
    if (Parser == nullptr || Name == nullptr)
        return nullptr;
    return Parser->OpenSection(Name, Flags);
}

static void ParserCloseSection(IProfileParser* Parser, void* Section)
{
    if (Parser == nullptr || Section == nullptr)
        return;
    Parser->CloseSection(Section);
}

// Retail RP8 FUN_06d047df ("populate_format_catalog", rp8.dll.c:1875) walks a section
// line-by-line: pass an incrementing zero-based Index until the call returns 0 (no more lines).
static bool ParserEnumLine(IProfileParser* Parser, void* Section, int Index, char* Buf,
                           unsigned int BufSize)
{
    if (Parser == nullptr || Section == nullptr || Buf == nullptr || BufSize == 0)
        return false;
    return Parser->ReadLine(Section, Index, Buf, BufSize) != 0;
}

#pragma comment(lib, "dacom.lib")
#pragma comment(lib, "ddraw.lib")
#pragma comment(lib, "d3d8.lib")
#pragma comment(lib, "version.lib")

#include "d3dx8tex.h"

namespace
{
    const int kFdumpError = 0x100001;
    const int kFdumpWarning = 0x100002;
    const int kFdumpNotice = 0x100003;
    // Retail pushes one literal per kind: "%s(%d) : WARNING:General:%s" (184 sites),
    // "... NOTICE ..." (24), "... ERROR ..." (1); the %s-substituted kind below yields the
    // same text.
    const char kFdumpFmt[] = "%s(%d) : %s:General:%s";
    // __FILE__ strings pushed by the retail FDUMP sites (read from rp8.dll .data).
    const char kSrcRpDx8[] =
        "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\RenderPipeline\\DX8\\rp_dx8.cpp";
    const char kSrcDx8Texture[] =
        "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\RenderPipeline\\DX8\\dx8_texture.h";
    const char kSrcDx8ShaderInl[] =
        "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\RenderPipeline\\DX8\\dx8_shader_inl.h";
    const char kSrcDx8IndexBuffer[] =
        "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\RenderPipeline\\DX8\\dx8_index_buffer.h";
    const char kSrcDx8VertexBuffer[] =
        "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\RenderPipeline\\DX8\\dx8_vertex_buffer.h";
    const char kSrcStateCache[] =
        "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\RenderPipeline\\StateCache.cpp";
    const char kSrcPixel[] =
        "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\RenderPipeline\\RPUL\\Pixel.cpp";

    // One map insertion of FUN_06d01c68 / FUN_06d19530 / FUN_06d176e0 / FUN_06d19c30: the
    // arguments of FUN_06d175f0 / FUN_06d176a0 (id, name, default, value 0, enabled), in the
    // order the listing inserts them. Names are the rp8.dll .data strings.
    struct FRp8PropertyInit
    {
        int Id;
        const char* Name;
        unsigned long Default;
        unsigned char bEnabled;
    };

    // FUN_06d01c68 (06d01d07..06d02032), map object+0x278.
    const FRp8PropertyInit kPipelineProperties[] = {
        {0x0, "TEXTURE", 0x1, 1},
        {0x1, "LIGHTING", 0x1, 1},
        {0x7, "VIEWSPACE_LIGHTS", 0x1, 1},
        {0xB, "STATE_CACHE", 0x1, 1},
        {0x8, "CLEAR_COLOR", 0x0, 1},
        {0x9, "CLEAR_ZDEPTH", 0x3F800000, 1},
        {0xA, "CLEAR_STENCIL", 0xFFFFFFFF, 1},
        {0x2, "TEXTURE_LOD", 0x1, 1},
        {0x3, "TEXTURE_ALLOW_8BIT", 0x0, 1},
        {0x4, "TEXTURE_ALLOW_32BIT", 0x1, 1},
        {0x5, "TEXTURE_ALLOW_DXT", 0x1, 1},
        {0x6, "BROKEN_MULTITEXTURE", 0x1, 1},
        {0xC, "BROKEN_MOD2X", 0x0, 1},
        {0xD, "BROKEN_FLIP", 0x0, 1},
    };

    // FUN_06d19530, map object+0x28C: disabled until startup finds the key in the INI.
    const FRp8PropertyInit kAbilityProperties[] = {
        {0x4, "DEVICE_GEOMETRY", 0x0, 0},
        {0x5, "TEXTURE_SQUARE_ONLY", 0x0, 0},
        {0x6, "TEXTURE_MAX_WIDTH", 0x0, 0},
        {0x7, "TEXTURE_MAX_HEIGHT", 0x0, 0},
        {0x8, "TEXTURE_CUBEMAPS", 0x0, 0},
        {0x9, "DEPTH_BIAS", 0x0, 0},
        {0xA, "FOG_VERTEX", 0x0, 0},
        {0xB, "FOG_PIXEL", 0x0, 0},
        {0xC, "FOG_RANGE", 0x0, 0},
        {0xD, "FOG_W", 0x0, 0},
        {0xE, "RASTER_ANTIALIASEDGES", 0x0, 0},
        {0xF, "DEVICE_SUPPORT_LEVEL", 0x0, 0},
        {0x10, "TEXTURE_TRILINEAR", 0x0, 0},
        {0x11, "DEVICE_BAD_MODE", 0x0, 0},
        {0x12, "DEVICE_BAD_4444", 0x0, 0},
    };

    // FUN_06d176e0, map object+0x2A0 (D3DRENDERSTATETYPE ids).
    const FRp8PropertyInit kRenderStateProperties[] = {
        {0x7, "ZENABLE", 0x1, 1},
        {0x8, "FILLMODE", 0x3, 1},
        {0x9, "SHADEMODE", 0x2, 1},
        {0xA, "LINEPATTERN", 0x0, 1},
        {0xE, "ZWRITEENABLE", 0x1, 1},
        {0xF, "ALPHATESTENABLE", 0x0, 1},
        {0x10, "LASTPIXEL", 0x1, 1},
        {0x13, "SRCBLEND", 0x2, 1},
        {0x14, "DESTBLEND", 0x1, 1},
        {0x16, "CULLMODE", 0x1, 1},
        {0x17, "ZFUNC", 0x2, 1},
        {0x18, "ALPHAREF", 0x0, 1},
        {0x19, "ALPHAFUNC", 0x8, 1},
        {0x1A, "DITHERENABLE", 0x0, 1},
        {0x1B, "ALPHABLENDENABLE", 0x0, 1},
        {0x1C, "FOGENABLE", 0x0, 1},
        {0x1D, "SPECULARENABLE", 0x0, 1},
        {0x1E, "ZVISIBLE", 0x1, 1},
        {0x22, "FOGCOLOR", 0x0, 1},
        {0x23, "FOGTABLEMODE", 0x1, 1},
        {0x24, "FOGSTART", 0x0, 1},
        {0x25, "FOGEND", 0x0, 1},
        {0x26, "FOGDENSITY", 0x0, 1},
        {0x28, "EDGEANTIALIAS", 0x0, 1},
        {0x2F, "ZBIAS", 0x0, 1},
        {0x30, "RANGEFOGENABLE", 0x0, 1},
        {0x34, "STENCILENABLE", 0x0, 1},
        {0x35, "STENCILFAIL", 0x1, 1},
        {0x36, "STENCILZFAIL", 0x1, 1},
        {0x37, "STENCILPASS", 0x1, 1},
        {0x38, "STENCILFUNC", 0x8, 1},
        {0x39, "STENCILREF", 0xFFFFFFFF, 1},
        {0x3A, "STENCILMASK", 0xFFFFFFFF, 1},
        {0x3B, "STENCILWRITEMASK", 0xFFFFFFFF, 1},
        {0x3C, "TEXTUREFACTOR", 0x0, 1},
        {0x80, "WRAP0", 0x0, 1},
        {0x81, "WRAP1", 0x0, 1},
        {0x82, "WRAP2", 0x0, 1},
        {0x83, "WRAP3", 0x0, 1},
        {0x84, "WRAP4", 0x0, 1},
        {0x85, "WRAP5", 0x0, 1},
        {0x86, "WRAP6", 0x0, 1},
        {0x87, "WRAP7", 0x0, 1},
        {0x88, "CLIPPING", 0x1, 1},
        {0x89, "LIGHTING", 0x0, 1},
        {0x8B, "AMBIENT", 0x0, 1},
        {0x8C, "FOGVERTEXMODE", 0x0, 1},
        {0x8D, "COLORVERTEX", 0x0, 1},
        {0x8E, "LOCALVIEWER", 0x0, 1},
        {0x8F, "NORMALIZENORMALS", 0x0, 1},
        {0x91, "DIFFUSEMATERIALSOURCE", 0x1, 1},
        {0x92, "SPECULARMATERIALSOURCE", 0x2, 1},
        {0x93, "AMBIENTMATERIALSOURCE", 0x2, 1},
        {0x94, "EMISSIVEMATERIALSOURCE", 0x0, 1},
        {0x97, "VERTEXBLEND", 0x0, 1},
        {0x98, "CLIPPLANEENABLE", 0x0, 1},
        {0x99, "SOFTWAREVERTEXPROCESSING", 0x0, 1},
        {0x9A, "POINTSIZE", 0x3F800000, 1},
        {0x9B, "POINTSIZE_MIN", 0x3F800000, 1},
        {0x9C, "POINTSPRITEENABLE", 0x0, 1},
        {0x9D, "POINTSCALEENABLE", 0x0, 1},
        {0x9E, "POINTSCALE_A", 0x3F800000, 1},
        {0x9F, "POINTSCALE_B", 0x0, 1},
        {0xA0, "POINTSCALE_C", 0x0, 1},
        {0xA1, "MULTISAMPLEANTIALIAS", 0x1, 1},
        {0xA2, "MULTISAMPLEMASK", 0xFFFFFFFF, 1},
        {0xA3, "PATCHEDGESTYLE", 0x0, 1},
        {0xA4, "PATCHSEGMENTS", 0x0, 1},
        {0xA5, "DEBUGMONITORTOKEN", 0x0, 1},
        {0xA6, "POINTSIZE_MAX", 0x3F800000, 1},
        {0xA7, "INDEXEDVERTEXBLENDENABLE", 0x0, 1},
        {0xA8, "COLORWRITEENABLE", 0xF, 1},
        {0xAA, "TWEENFACTOR", 0x0, 1},
        {0xAB, "BLENDOP", 0x1, 1},
        {0xAC, "POSITIONORDER", 0x1, 1},
        {0xAD, "NORMALORDER", 0x1, 1},
    };

    // FUN_06d19c30(map, bFirstStage): 06d19c47 JZ -- a non-zero flag inserts COLOROP 4 /
    // ALPHAOP 2 (MODULATE / SELECTARG1), zero inserts 1 / 1 (DISABLE); the rest is shared.
    const FRp8PropertyInit kStageOpsFirstStage[] = {
        {0x1, "COLOROP", 0x4, 1},
        {0x4, "ALPHAOP", 0x2, 1},
    };
    const FRp8PropertyInit kStageOpsOtherStage[] = {
        {0x1, "COLOROP", 0x1, 1},
        {0x4, "ALPHAOP", 0x1, 1},
    };
    const FRp8PropertyInit kStageStateProperties[] = {
        {0x2, "COLORARG1", 0x2, 1},
        {0x3, "COLORARG2", 0x1, 1},
        {0x5, "ALPHAARG1", 0x2, 1},
        {0x6, "ALPHAARG2", 0x1, 1},
        {0x7, "BUMPENVMAT00", 0x0, 1},
        {0x8, "BUMPENVMAT01", 0x0, 1},
        {0x9, "BUMPENVMAT10", 0x0, 1},
        {0xA, "BUMPENVMAT11", 0x0, 1},
        {0xB, "TEXCOORDINDEX", 0x0, 1},
        {0xD, "ADDRESSU", 0x1, 1},
        {0xE, "ADDRESSV", 0x1, 1},
        {0xF, "BORDERCOLOR", 0x0, 1},
        {0x10, "MAGFILTER", 0x2, 1},
        {0x11, "MINFILTER", 0x2, 1},
        {0x12, "MIPFILTER", 0x1, 1},
        {0x13, "MIPMAPLODBIAS", 0x0, 1},
        {0x14, "MAXMIPLEVEL", 0x0, 1},
        {0x15, "MAXANISOTROPY", 0x1, 1},
        {0x16, "BUMPENVLSCALE", 0x0, 1},
        {0x17, "BUMPENVLOFFSET", 0x0, 1},
        {0x18, "TEXTURETRANSFORMFLAGS", 0x0, 1},
        {0x19, "ADDRESSW", 0x1, 1},
        {0x1A, "COLORARG0", 0x1, 1},
        {0x1B, "ALPHAARG0", 0x1, 1},
        {0x1C, "RESULTARG", 0x1, 1},
    };

    // FUN_06d16ff0 (clear) followed by one insertion per row (FUN_06d15f80 operator[] +
    // FUN_06d17630 assignment, or FUN_06d16b90 insert into the just-cleared map -- same result).
    void FillPropertyMap(FRp8PropertyMap* Map, const FRp8PropertyInit* Rows, unsigned int Count,
                         bool bClear)
    {
        if (bClear)
            Map->clear();
        for (unsigned int Index = 0; Index < Count; ++Index)
        {
            FRp8Property Property;
            Property.Name = Rows[Index].Name;
            Property.Id = Rows[Index].Id;
            Property.Default = Rows[Index].Default;
            Property.Value = 0;
            Property.bEnabled = Rows[Index].bEnabled;
            (*Map)[Rows[Index].Id] = Property;
        }
    }

    // FUN_06d19c30.
    void FillStageStateMap(FRp8PropertyMap* Map, bool bFirstStage)
    {
        Map->clear();
        if (bFirstStage)
            FillPropertyMap(Map, kStageOpsFirstStage,
                            sizeof(kStageOpsFirstStage) / sizeof(kStageOpsFirstStage[0]), false);
        else
            FillPropertyMap(Map, kStageOpsOtherStage,
                            sizeof(kStageOpsOtherStage) / sizeof(kStageOpsOtherStage[0]), false);
        FillPropertyMap(Map, kStageStateProperties,
                        sizeof(kStageStateProperties) / sizeof(kStageStateProperties[0]), false);
    }

    // DAT_06d73580 / DAT_06d739a0 / DAT_06d73dc0: the three static 0x400-byte buffers startup and
    // FUN_06d16930 read INI strings into (string keys / numeric keys / the ability-key probe).
    char G_ProfileStringBuf[0x400];
    char G_ProfileValueBuf[0x400];
    char G_ProfileProbeBuf[0x400];

    // Startup's smart-pointer idiom (FUN_06d15f20 assign + AddRef, FUN_06d167a0 Release): take
    // the held parser, or ask DACOM for one when none is held. False = the QI failed.
    bool AcquireStartupParser(ICOManager* Dacom, IProfileParser* Held, IProfileParser** Out)
    {
        *Out = nullptr;
        if (Held != nullptr)
        {
            *Out = Held;
            Held->AddRef();
            return true;
        }
        return Dacom->QueryInterface(Iids::ProfileParser, (void**)(Out)) == GR_OK;
    }

    void ReleaseStartupParser(IProfileParser** Parser)
    {
        if (*Parser != nullptr)
        {
            (*Parser)->Release();
            *Parser = nullptr;
        }
    }

    unsigned long ReadProfileNumber(ICOManager* Dacom, IProfileParser* Held, const char* Section,
                                    const char* Key, unsigned long Default, unsigned long* Out)
    {
        // Retail's local_c is uninitialized and only read back when the IProfileParser QI fails
        // (then *Out is returned unwritten); the port starts it at 0 instead of reading garbage.
        unsigned long Local = 0;
        if (Out == nullptr)
            Out = &Local;
        IProfileParser* Parser = nullptr;
        if (AcquireStartupParser(Dacom, Held, &Parser))
        {
            *Out = Default;
            void* Handle = Parser->OpenSection(Section, 3);
            if (Handle != nullptr)
            {
                if (Parser->GetString(Handle, Key, G_ProfileValueBuf, 0x400) != 0)
                {
                    const size_t Span =
                        strspn(G_ProfileValueBuf, "0123456789truefalsynoxTRUEFALSYNOX+-");
                    if (Span != 0)
                        G_ProfileValueBuf[Span] = '\0';
                    if (_stricmp(G_ProfileValueBuf, "true") == 0)
                        *Out = 1;
                    else if (_stricmp(G_ProfileValueBuf, "yes") == 0)
                        *Out = 1;
                    else if (_stricmp(G_ProfileValueBuf, "false") == 0)
                        *Out = 0;
                    else if (_stricmp(G_ProfileValueBuf, "no") == 0)
                        *Out = 0;
                    else if (strchr(G_ProfileValueBuf, 'x') != nullptr ||
                             strchr(G_ProfileValueBuf, 'X') != nullptr)
                        *Out = strtoul(G_ProfileValueBuf, nullptr, 16);
                    else
                        // 06d16b09 also runs strspn(buf, "0123456789+-") and drops the result.
                        *Out = (unsigned long)(MsvcrtAtoi(G_ProfileValueBuf));
                }
                Parser->CloseSection(Handle);
            }
        }
        const unsigned long Result = *Out;
        ReleaseStartupParser(&Parser);
        return Result;
    }

    // Startup's inlined string reads (DeviceId, DeviceType, MGSDB): once a parser is held, Out
    // gets Default (strncpy 0x80), then the key's text (GetString into DAT_06d73580, strncpy
    // 0x80) when present.
    void ReadProfileString(ICOManager* Dacom, IProfileParser* Held, const char* Section,
                           const char* Key, const char* Default, char* Out)
    {
        IProfileParser* Parser = nullptr;
        if (AcquireStartupParser(Dacom, Held, &Parser))
        {
            strncpy(Out, Default, 0x80);
            void* Handle = Parser->OpenSection(Section, 3);
            if (Handle != nullptr)
            {
                if (Parser->GetString(Handle, Key, G_ProfileStringBuf, 0x400) != 0)
                    strncpy(Out, G_ProfileStringBuf, 0x80);
                Parser->CloseSection(Handle);
            }
        }
        ReleaseStartupParser(&Parser);
    }

    struct FZFormatRow
    {
        D3DFORMAT Format;
        int ZBits;
        int StencilBits;
    };

    const FZFormatRow kZTable[] = {
        {D3DFMT_D16, 0x10, 0}, {D3DFMT_D32, 0x20, 0},
        {D3DFMT_D15S1, 0x0F, 1}, {D3DFMT_D24S8, 0x18, 8},
        {D3DFMT_D24X8, 0x18, 0}, {D3DFMT_D24X4S4, 0x18, 4},
    };

    // FUN_06d15b0d / FUN_06d15b45: walk DAT_06d64040 (rows {fmt, z bits, stencil bits}, ended
    // by fmt 0) for the given depth format; its z / stencil bits, 0 when absent.
    int DepthFormatZBits(D3DFORMAT Format)
    {
        for (unsigned int Index = 0; Index < sizeof(kZTable) / sizeof(kZTable[0]); ++Index)
        {
            if (kZTable[Index].Format == Format)
                return kZTable[Index].ZBits;
        }
        return 0;
    }
    int DepthFormatStencilBits(D3DFORMAT Format)
    {
        for (unsigned int Index = 0; Index < sizeof(kZTable) / sizeof(kZTable[0]); ++Index)
        {
            if (kZTable[Index].Format == Format)
                return kZTable[Index].StencilBits;
        }
        return 0;
    }

    // DAT_06d6ffa8 — 22 dword (FUN_06d5cc20). Numeric ids only; no PF_* names.
    // Values at idx 2/7 are 0x14/0x15 (also valid indices 20/21 = DAA4/DAA8).
    const unsigned int kPfToD3d[22] = {
        0,    0x29, 0x14, 0x17, 0x18, 0x1A, 0x19, 0x15, 0x16, 0,
        0x31545844, 0x32545844, 0x33545844, 0x34545844, 0x35545844,
        0x504F4144, 0x544F4144, 0x41414144, 0x4C414144, 0x31414144,
        0x34414144, 0x38414144};

    // bpp from descriptor +4 (ximage DAT_06f841a8 idx 1–8 = RP8 DAT_06d70000).
    const unsigned int kPfBpp[22] = {
        0, 8, 24, 16, 16, 16, 16, 32, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    // Retail FUN_06d5cc70(id)+4 -- per-pfenum-id bpp lookup in BITS (not bytes), used by
    // check_texture_format's capability-gate math (rp8.dll.c:5212-5220). Retail's table is
    // valid for ids 0..14 (0xe); ids above that clamp to id 0 (bpp=0). kPfBpp already has 0
    // for the unpopulated 9..14/15..21 range, so this is a direct index with a range clamp.
    unsigned int RetailPfBppBits(unsigned int PfId)
    {
        return (PfId <= 14) ? kPfBpp[PfId] : kPfBpp[0];
    }

    struct FPixelFormatDescriptor
    {
        unsigned int PfId; // pfenum id — same domain as kPfToD3d/kPfBpp indices
        unsigned int Bpp;
        unsigned int RBits; // retail's table also uses this same field for the B-bit compare
        unsigned int GBits;
        unsigned int ABits;
    };

    // Supported pixel-format channel descriptors, ordered by format ID.
    const FPixelFormatDescriptor kPfDescriptors[] = {
        {1, 8, 0, 0, 0},   {2, 24, 8, 8, 0},  {3, 16, 5, 6, 0},  {4, 16, 5, 5, 0},
        {5, 16, 4, 4, 4},  {6, 16, 5, 5, 1},  {7, 32, 8, 8, 8},  {8, 32, 8, 8, 0},
    };

    // FUN_06d0e3d9: dword0 = IDirect3DTexture8* (or cube); dword1 0=2D, 1=cube.
    struct FRp8TextureHandle
    {
        IDirect3DTexture8* Texture;
        unsigned long Extra;
    };

    // Buffer lists borrow nodes; Handle==-1 addresses the inline scratch IB.
    struct FRp8VbList
    {
        FRp8Vb* First;
        FRp8Vb* Last;
        unsigned long Count;
    };
    struct FRp8IbList
    {
        FRp8Ib* First;
        FRp8Ib* Last;
        unsigned long Count;
    };
    static_assert(sizeof(FRp8Ib) == 0x1C, "create_ib operator new(0x1c)");
    static const unsigned int kOffVbList = 0x22E4;
    static const unsigned int kOffIbList = 0x22F0;

    static FRp8Ib* ResolveIbHandle(unsigned char* IbThis, void* Handle)
    {
        if (Handle == (void*)((intptr_t)(-1)))
            return (FRp8Ib*)(IbThis + 0x140);
        return (FRp8Ib*)(Handle);
    }

    // Pipeline-state id 7 (VIEWSPACE_LIGHTS) value slot: this+0x354 + 7*0xC. Read raw (no named
    // field exists for this id -- it currently lives inside Pad39C_) because it gates which of
    // View/World gets the Z-flip matrix in ApplyIdentityTransforms below (retail FUN_06d03c94).
    static const unsigned int kOffViewspaceLights = 0x3A8;
    static const unsigned int kOffStageCount = 0x39C;
    // FUN_06d1b480(0x200): Flags = 0x200|8 (D3DUSAGE_DYNAMIC|WRITEONLY).
    static const unsigned long kVbPoolUsage = 0x208;


    FRp8Factory* G_Factory = nullptr;

    bool IidEqual(const char* Left, const char* Right)
    {
        if (Left == nullptr || Right == nullptr)
            return false;
        return std::strcmp(Left, Right) == 0;
    }

    // Retail: MOV reg,[->DACOM.DLL::FDUMP]; CALL [reg] -- unconditional, no fallback.
    void Rp8Fdump(int Flags, int Line, const char* Kind, const char* Message,
                  const char* File = kSrcRpDx8)
    {
        FDUMP(Flags, kFdumpFmt, File, Line, Kind, Message);
    }

    // FUN_06d168f0 (cdecl): _vsnprintf(Buffer, 0x2000, Format, ...), returns Buffer. The text
    // is left unterminated when it fills all 0x2000 bytes; every caller's buffer is 0x2000+.
    char* Rp8Sprintf(char* Buffer, const char* Format, ...)
    {
        va_list Args;
        va_start(Args, Format);
        _vsnprintf(Buffer, 0x2000, Format, Args);
        va_end(Args);
        return Buffer;
    }

    // FUN_06d020ab apply list only. Offsets from listing (format-class pads, not Ability_).
    static const unsigned int kOffFmt8888Zero = 0x384;
    static const unsigned int kOffFmt8888 = 0x388;
    static const unsigned int kOffFmt8888Mirror = 0x38C;
    static const unsigned int kOffFmtDxtZero = 0x390;
    static const unsigned int kOffFmtDxt = 0x394;
    static const unsigned int kOffFmtDxtMirror = 0x398;
    static const unsigned int kOffFmtMod2xAlt = 0x3E4;
    static const unsigned int kOffFmtMod2x = 0x3E8;
    static const unsigned int kOffFmtMod2xMirror = 0x3EC;
    static const unsigned int kOffNoAlpha548 = 0x548;
    static const unsigned int kOffNoAlpha5D8 = 0x5D8;
    static const unsigned int kOffNoAlpha5E8 = 0x5E8;

    // FUN_06d01000 (cdecl; the name is pushed but never read): the value of the first record
    // of the database's live set whose name has the DACOM CRC Crc, or null.
    const char* MgsdbFindFlag(FMgsdbDatabase* Db, const char* Name, unsigned long Crc)
    {
        (void)Name;
        const FMgsdbRecordSet* Set = Db->GetRecords();
        for (size_t Index = 0; Index < Set->Records.size(); ++Index)
        {
            if (DACOM_CRC::GetCRC32(Set->Records[Index].Name.c_str()) == Crc)
                return Set->Records[Index].Value.c_str();
        }
        return nullptr;
    }

    // FUN_06d020ab's per-flag test: FUN_06d01000(db, name, DACOM_CRC::GetCRC32(name)).
    bool MgsdbHasFlag(FMgsdbDatabase* Db, const char* Name)
    {
        return MgsdbFindFlag(Db, Name, DACOM_CRC::GetCRC32(Name)) != nullptr;
    }

    // ---- StateCache.cpp light state (FUN_06d2be34..FUN_06d2d032) ----
    //
    // One record per light handle (the map value at node+0x10, 0x10 bytes). Its destructor
    // (inlined at 06d2c4f1..06d2c51b and in the map's erase) returns the D3D index to the pool.
    struct FRp8LightRecord
    {
        unsigned char bEnabled;     // +0 LightEnable state last committed to the device
        unsigned char bWantEnabled; // +1 state asked for by set_light_enable
        unsigned char bDataDirty;   // +2 Data must be resent with SetLight at the next flush
        long Generation;            // +4 manager generation the record was queued in
        int D3dIndex;               // +8 D3D light index from the pool, -1 = none
        const D3DLIGHT8* Data;      // +C light data (add_light passes the handle itself)
        ~FRp8LightRecord();
    };

    static_assert(sizeof(FRp8LightRecord) == 0x10, "light record 0x10");

    // The three globals in static-initializer order (table 06d64004/06d64008/06d6400c), so they
    // are destroyed in reverse, as retail's atexit entries run: the map first (its records
    // hand their indices back to the still-live pool), then the dirty list, then the pool.
    // FUN_06d2bc44: DAT_06d728e0 = vector<int>(0x40, 0), the free-index pool.
    std::vector<int> G_LightIndexPool(0x40, 0);
    // DAT_06d72908: pool entries handed out. DAT_06d7290c: next never-used D3D index.
    unsigned int G_LightIndexUsed = 0;
    unsigned int G_LightIndexNext = 0;
    // FUN_06d2bd86: DAT_06d728f0 = empty vector of records queued for the next flush.
    std::vector<FRp8LightRecord*> G_DirtyLights;
    // FUN_06d2c01a: DAT_06d728cc = map<handle, record> (keys compared unsigned).
    std::map<unsigned long, FRp8LightRecord> G_LightRecords;

    // FUN_06d2c553: take an index from the pool. Used >= size -> resize(size + 0x10, 0) and
    // dump " exceeded 64 allocated lights. now at: %d" (StateCache.cpp 0x7c) with the new size;
    // Used >= Next -> pool[Used] = Next++; return pool[Used++].
    int LightIndexAlloc()
    {
        const unsigned int Size = (unsigned int)(G_LightIndexPool.size());
        if (G_LightIndexUsed >= Size)
        {
            const unsigned int NewSize = Size + 0x10;
            G_LightIndexPool.resize(NewSize, 0);
            char Text[0x2000];
            _snprintf(Text, sizeof(Text), " exceeded 64 allocated lights. now at: %d",
                      (int)(NewSize));
            Text[sizeof(Text) - 1] = '\0';
            Rp8Fdump(kFdumpWarning, 0x7c, "WARNING", Text, kSrcStateCache);
        }
        if (G_LightIndexUsed >= G_LightIndexNext)
        {
            G_LightIndexPool[G_LightIndexUsed] = (int)(G_LightIndexNext);
            ++G_LightIndexNext;
        }
        const int Index = G_LightIndexPool[G_LightIndexUsed];
        ++G_LightIndexUsed;
        return Index;
    }

    // FUN_06d2c7a2: pool[--Used] = Index.
    void LightIndexFree(int Index)
    {
        --G_LightIndexUsed;
        G_LightIndexPool[G_LightIndexUsed] = Index;
    }

    FRp8LightRecord::~FRp8LightRecord()
    {
        if (D3dIndex != -1)
            LightIndexFree(D3dIndex);
    }

    // map::find on DAT_06d728cc (FUN_06d2e9f0 lower_bound, then end or key < node key -> end);
    // the callers turn end into a null record.
    FRp8LightRecord* LightFind(unsigned long Handle)
    {
        std::map<unsigned long, FRp8LightRecord>::iterator It = G_LightRecords.find(Handle);
        if (It == G_LightRecords.end())
            return nullptr;
        return &It->second;
    }



    // FUN_06d2be34 (cdecl): queue a record for the next flush once per generation, and only when
    // its enable state changes or it is on with data to resend.
    void LightQueueDirty(FRp8LightManager* Manager, FRp8LightRecord* Record)
    {
        if (Record->Generation == Manager->Generation)
            return;
        if (Record->bEnabled == Record->bWantEnabled)
        {
            if (Record->bWantEnabled == 0)
                return;
            if (Record->bDataDirty == 0)
                return;
        }
        const size_t Count = G_DirtyLights.size();
        G_DirtyLights.resize(Count + 1, nullptr);
        G_DirtyLights[Count] = Record;
        Record->Generation = Manager->Generation;
    }















    unsigned int FormatBpp(D3DFORMAT Format)
    {
        if (Format == 21 || Format == 22)
            return 32;
        if (Format == 23 || Format == 24 || Format == 25)
            return 16;
        return 16;
    }


    // FUN_06d2b964: DAT_06d72848[0] = 0; D3DXGetErrorStringA(Hr, DAT_06d72848, 0x80) (the
    // statically linked d3dx8 FUN_06d3095a, stdcall); returns the static 0x80-byte buffer.
    char G_HrText[0x80];
    const char* HrToText(HRESULT Hr)
    {
        G_HrText[0] = '\0';
        D3DXGetErrorStringA(Hr, G_HrText, 0x80);
        return G_HrText;
    }

    // Every failed D3D call in rp_dx8.cpp/dx8_*.h: FUN_06d168f0 (sprintf) of the literal
    // "<name>: %s" with FUN_06d2b964(hr) into a 0x2000-byte stack buffer, then
    // FDUMP(0x100002, "%s(%d) : WARNING:General:%s", <file>, <line>, buffer).
    void FdumpHr(const char* Format, HRESULT Hr, int Line, const char* File = kSrcRpDx8)
    {
        char Text[0x2000];
        _snprintf(Text, sizeof(Text), Format, HrToText(Hr));
        Text[sizeof(Text) - 1] = '\0';
        Rp8Fdump(kFdumpWarning, Line, "WARNING", Text, File);
    }

    bool TextureGetDesc(FRp8TextureHandle* Handle, UINT Level, D3DSURFACE_DESC* Desc)
    {
        IDirect3DTexture8* Texture = Handle->Extra != 0 ? nullptr : Handle->Texture;
        if (Texture != nullptr)
        {
            const HRESULT Hr = Texture->GetLevelDesc(Level, Desc);
            if (Hr < 0)
            {
                FdumpHr("get_desc: %s", Hr, 0xe3, kSrcDx8Texture);
                return false;
            }
            return true;
        }
        IDirect3DCubeTexture8* Cube = Handle->Extra == 1
                                          ? (IDirect3DCubeTexture8*)(Handle->Texture)
                                          : nullptr;
        if (Cube != nullptr)
        {
            const HRESULT Hr = Cube->GetLevelDesc(Level, Desc);
            if (Hr < 0)
            {
                FdumpHr("get_desc: %s", Hr, 0xf0, kSrcDx8Texture);
                return false;
            }
        }
        return true;
    }

    // dx8_texture.h get_subsurface (lock_texture 06d0df26..06d0e01d, unlock_texture
    // 06d0e253..06d0e35d): hr = E_FAIL; 2D -> GetSurfaceLevel (+0x3c)(Level); cube (Extra == 1)
    // -> GetCubeMapSurface (+0x3c)(face = Level, mip 0); hr < 0 -> dump line 0xab.
    HRESULT TextureGetSubSurface(FRp8TextureHandle* Handle, UINT Level, IDirect3DSurface8** Out)
    {
        HRESULT Hr = (HRESULT)(0x80004005);
        IDirect3DTexture8* Texture = Handle->Extra != 0 ? nullptr : Handle->Texture;
        if (Texture != nullptr)
            Hr = Texture->GetSurfaceLevel(Level, Out);
        else
        {
            IDirect3DCubeTexture8* Cube =
                Handle->Extra == 1 ? (IDirect3DCubeTexture8*)(Handle->Texture)
                                   : nullptr;
            if (Cube != nullptr)
                Hr = Cube->GetCubeMapSurface((D3DCUBEMAP_FACES)(Level), 0, Out);
        }
        if (Hr < 0)
            FdumpHr("get_subsurface: %s", Hr, 0xab, kSrcDx8Texture);
        return Hr;
    }

    // dx8_index_buffer.h:49 (06d13319.. / 06d13455..): sprintf("couldnt create ib (err:%x) %d
    // bytes ", hr, bytes) -> FDUMP WARNING.
    void FdumpIbCreate(HRESULT Hr, unsigned int Bytes)
    {
        char Text[0x2000];
        _snprintf(Text, sizeof(Text), "couldnt create ib (err:%x) %d bytes ",
                  (unsigned int)(Hr), (int)(Bytes));
        Text[sizeof(Text) - 1] = '\0';
        Rp8Fdump(kFdumpWarning, 49, "WARNING", Text, kSrcDx8IndexBuffer);
    }
}

// formerly LightManagerConstruct
void FRp8LightManager::Initialize()
{
    Generation = 0;
    EnabledCount = 0;
    G_DirtyLights.reserve(0x40);
}

// formerly LightFindOrCreate
int FRp8LightManager::FindOrCreate(unsigned long Handle, const D3DLIGHT8* Data)
{
    FRp8LightRecord Record;
    Record.bEnabled = 0;
    Record.bWantEnabled = 0;
    Record.bDataDirty = 0;
    Record.Generation = -1;
    Record.D3dIndex = -1;
    Record.Data = Data;
    std::pair<std::map<unsigned long, FRp8LightRecord>::iterator, bool> Result =
        G_LightRecords.insert(std::pair<unsigned long, FRp8LightRecord>(Handle, Record));
    if (Result.second)
    {
        Result.first->second.D3dIndex = LightIndexAlloc();
        if (Data != nullptr)
            Result.first->second.bDataDirty = 1;
    }
    return Result.first->second.D3dIndex;
}

// formerly LightRemove
bool FRp8LightManager::Remove(unsigned long Handle)
{
    std::map<unsigned long, FRp8LightRecord>::iterator It = G_LightRecords.find(Handle);
    if (It == G_LightRecords.end())
        return false;
    FRp8LightRecord* Record = &It->second;
    const int Count = (int)(G_DirtyLights.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        if (G_DirtyLights[Index] == Record)
        {
            if (Record->bEnabled != 0)
                --EnabledCount;
            G_DirtyLights[Index] = nullptr;
            break;
        }
    }
    G_LightRecords.erase(It);
    return true;
}

// formerly LightGetEnable
unsigned char FRp8LightManager::GetEnable(unsigned long Handle)
{
    FRp8LightRecord* Record = LightFind(Handle);
    if (Record == nullptr)
    {
        Rp8Fdump(kFdumpNotice, 0x14b, "NOTICE", "light not found", kSrcStateCache);
        return 0;
    }
    return Record->bWantEnabled;
}

// formerly LightUpdate
bool FRp8LightManager::Update(unsigned long Handle)
{
    FRp8LightRecord* Record = LightFind(Handle);
    if (Record == nullptr)
    {
        Rp8Fdump(kFdumpNotice, 0x157, "NOTICE", "light not found", kSrcStateCache);
        return false;
    }
    Record->bDataDirty = 1;
    LightQueueDirty(this, Record);
    return true;
}

// formerly LightSetEnable
bool FRp8LightManager::SetEnable(unsigned long Handle, int Enable)
{
    FRp8LightRecord* Record = LightFind(Handle);
    if (Record == nullptr)
    {
        if (Enable == 0)
            return true;
        FindOrCreate(Handle, nullptr);
        Record = LightFind(Handle);
    }
    Record->bWantEnabled = Enable != 0 ? 1 : 0;
    LightQueueDirty(this, Record);
    return true;
}

// formerly LightReset
void FRp8LightManager::Reset()
{
    G_DirtyLights.clear();
    Generation = 0;
    EnabledCount = 0;
    for (std::map<unsigned long, FRp8LightRecord>::iterator It = G_LightRecords.begin();
         It != G_LightRecords.end(); ++It)
    {
        FRp8LightRecord* Record = &It->second;
        if (Record->Data != nullptr)
            Record->bDataDirty = 1;
        if (Record->bWantEnabled != 0)
        {
            Record->bEnabled = 0;
            LightQueueDirty(this, Record);
        }
    }
}

// formerly LightFlush
HRESULT FRp8LightManager::Flush(IDirect3DDevice8* Device)
{
    HRESULT Result = 0;
    const int Count = (int)(G_DirtyLights.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        FRp8LightRecord* Record = G_DirtyLights[Index];
        if (Record == nullptr)
            continue;
        const unsigned char bWasEnabled = Record->bEnabled;
        HRESULT Hr = 0;
        if (Record->bDataDirty != 0 && Record->bWantEnabled != 0)
        {
            Record->bDataDirty = 0;
            Hr = Device->SetLight((DWORD)Record->D3dIndex, Record->Data);
        }
        if (Record->bWantEnabled != Record->bEnabled && Hr >= 0)
        {
            Hr = Device->LightEnable((DWORD)Record->D3dIndex,
                                     (BOOL)Record->bWantEnabled);
            if (Hr >= 0)
                Record->bEnabled = Record->bWantEnabled;
        }
        Result = Hr;
        if (Result < 0)
            break;
        if (bWasEnabled != Record->bEnabled)
        {
            if (Record->bEnabled != 0)
                ++EnabledCount;
            else
                --EnabledCount;
        }
    }
    G_DirtyLights.clear();
    ++Generation;
    return Result;
}

FRp8Inner::FRp8Inner() : RefCount(1), Owner(nullptr) {}

GENRESULT DACOM_API FRp8Inner::QueryInterface(const char* Iid, void** Out)
{
    if (Owner == nullptr)
        return GR_INTERFACE_UNSUPPORTED;
    return Owner->QueryLocal(Iid, Out);
}

unsigned long DACOM_API FRp8Inner::AddRef()
{
    ++RefCount;
    return RefCount;
}

unsigned long DACOM_API FRp8Inner::Release()
{
    if (RefCount > 0)
        --RefCount;
    if (RefCount == 0)
    {
        // The nondelegating controller remains referenced during owner teardown.
        ++RefCount;
        FDirectX8* Object = Owner;
        if (Object != nullptr)
        {
            Object->~FDirectX8();
            free(Object); // paired with the factory's calloc
        }
        return 0;
    }
    return RefCount;
}

FRp8Factory::FRp8Factory() : RefCount(1), Name(kRp8ComponentName) {}

GENRESULT DACOM_API FRp8Factory::QueryInterface(const char* Iid, void** Out)
{
    if (Out == nullptr)
        return GR_GENERIC;
    *Out = nullptr;
    if (!IidEqual(Iid, Iids::ComponentFactory))
        return GR_INTERFACE_UNSUPPORTED;
    *Out = (IComponentFactory*)(this);
    AddRef();
    return GR_OK;
}

unsigned long DACOM_API FRp8Factory::AddRef()
{
    ++RefCount;
    return RefCount;
}

unsigned long DACOM_API FRp8Factory::Release()
{
    if (RefCount > 0)
        --RefCount;
    unsigned long Left = RefCount;
    if (Left == 0)
    {
        delete this;
        if (G_Factory == this)
            G_Factory = nullptr;
    }
    return Left;
}

GENRESULT DACOM_API FRp8Factory::CreateInstance(AGGDESC* Desc, void** Instance)
{
    GENRESULT Result = GR_OK;
    FDirectX8* Object = nullptr;
    FRp8AggDesc* Ext = (FRp8AggDesc*)(Desc);
    if (Ext->Size != 0x14 || std::strcmp(Ext->Name, Name) != 0)
    {
        Result = GR_INTERFACE_UNSUPPORTED;
    }
    else
    {
        void* Memory = calloc(kRp8ObjectSize, 1);
        if (Memory != nullptr)
        {
            Object = new (Memory) FDirectX8();
            Object->Inner_.Owner = Object;
            Object->Outer_ = Ext->Outer;
            if (Object->Outer_ == nullptr)
                Object->Outer_ = &Object->Inner_;
            else
                *Ext->InnerOut = &Object->Inner_;
        }
        if (Object == nullptr)
        {
            Result = GR_OUT_OF_MEMORY;
        }
        else
        {
            Result = Object->ApplySectionName(Ext);
            if (Result != GR_OK)
            {
                if (Ext->InnerOut != nullptr)
                    *Ext->InnerOut = nullptr;
                Object->~FDirectX8();
                free(Object);
                Object = nullptr;
            }
        }
    }
    *Instance = (IRenderPipeline*)(Object);
    return Result;
}

// FUN_06d01143 on calloc'd memory: everything not named below stays zero. The containers
// (adapter vector +0x198, the property maps +0x278..+0x354) are constructed by their own
// constructors here.
FDirectX8::FDirectX8()
    : Parser_(nullptr), CreateFlags_(0), SwapFlags_(0), D3d_(nullptr), Device_(nullptr),
      LockBufferSurf_(nullptr), ScratchIbFlags_(0x200 | 8), ScratchIb_(nullptr), IbUsed_(0),
      IbCapacity_(0), VidMem_(0), VbPoolBytes_(0), DeviceReady_(0), FormatCatalogBegin_(nullptr),
      FormatCatalogEnd_(nullptr), FormatCatalogCap_(nullptr), AdapterIndex_(0xFFFFFFFF),
      DeviceType_(1), Field230_(0), BbPfIndex_(0), Hwnd_(nullptr), WindowX_(0), WindowY_(0),
      WindowW_(0), WindowH_(0), WindowOverride_(nullptr), BltNotFlip_(0), Field3F4_(0),
      Field3F8_(0), bDepthRangeValid_(0), bViewportRectValid_(0), Outer_(nullptr)
{
    memset(SectionName_, 0, sizeof(SectionName_));
    memset(LastSection_, 0, sizeof(LastSection_));
    memset(&StreamCache_, 0, sizeof(StreamCache_));
    // 06d011d3: the stream cache +0x138 = {0, 0, 0, -1, 0}.
    StreamCache_.BaseVertex = 0xFFFFFFFF;
    memset(Pad154_, 0, sizeof(Pad154_));
    memset(FormatCatalogHeader_, 0, sizeof(FormatCatalogHeader_));
    // 06d012f8 / 06d01316: +0x230 and +0x234 = FUN_06d5cc30(0).
    Field230_ = PfIndex(0);
    BbPfIndex_ = PfIndex(0);
    memset(MgsdbPath_, 0, sizeof(MgsdbPath_));
    memset(ModeBlob_, 0, sizeof(ModeBlob_));
    // 06d0140a: the pipeline-state slots of ids 0..0xD = {0, 0, 0}.
    memset(Pad354_, 0, sizeof(Pad354_));
    memset(Ability_, 0, sizeof(Ability_));
    memset(&RenderStates_, 0, sizeof(RenderStates_));
    memset(&DeviceStates_, 0, sizeof(DeviceStates_));
    // 06d014b4: FUN_06d2c31c(this + 0x21F4), between the texture-transform cache loop and the
    // VIEW matrix flags.
    LightManager_.Initialize();
    memset(&Transforms_, 0, sizeof(Transforms_));
    memset(&ViewportCache_, 0, sizeof(ViewportCache_));
    memset(Pad22E2_, 0, sizeof(Pad22E2_));
    RenderStates_.Initialize();
    Inner_.Owner = this;
}

FDirectX8::~FDirectX8()
{
    ShutdownPipeline();
    // The VB pool map (object+0x16C): the destructor frees the nodes that destroy_buffers /
    // VBMgr slot 4 only strip of their buffers.
    ReleasePoolVbs();
    if (Parser_ != nullptr)
    {
        Parser_->Release();
        Parser_ = nullptr;
    }
    // The adapter vector and the property maps are released by their own destructors after
    // this body (retail 06d016fb..06d0182b tears them down inline).
    ClearFormatCatalog(); // frees each node's Values array
    delete[] FormatCatalogBegin_;
    FormatCatalogBegin_ = nullptr;
    FormatCatalogEnd_ = nullptr;
    FormatCatalogCap_ = nullptr;
}

GENRESULT FDirectX8::QueryLocal(const char* Iid, void** Out)
{
    if (Out == nullptr)
        return GR_GENERIC;
    *Out = nullptr;
    IDAComponent* Face = nullptr;
    if (IidEqual(Iid, Iids::RenderPipeline8B))
        Face = (IRenderPipeline*)(this);
    else if (IidEqual(Iid, Iids::VertexBufferManager))
        Face = (IVertexBufferManager*)(this);
    else if (IidEqual(Iid, Iids::RPDraw))
        Face = (IRPDraw*)(this);
    else if (IidEqual(Iid, Iids::RPIndexBuffer))
        Face = (IRPIndexBuffer*)(this);
    else if (IidEqual(Iid, Iids::RPVertexBuffer))
        Face = (IRPVertexBuffer*)(this);
    else if (IidEqual(Iid, Iids::GammaControl))
        Face = (IGammaControl*)(this);
    else if (IidEqual(Iid, Iids::RPTexture))
        Face = (IRPTexture*)(this);
    else if (IidEqual(Iid, Iids::AggregateComponent))
        Face = (IAggregateComponent*)(this);
    if (Face == nullptr)
        return GR_INTERFACE_UNSUPPORTED;
    Face->AddRef();
    *Out = Face;
    return GR_OK;
}

GENRESULT DACOM_API FDirectX8::QueryInterface(const char* Iid, void** Out)
{
    if (Outer_ != nullptr && Outer_ != &Inner_)
        return Outer_->QueryInterface(Iid, Out);
    return QueryLocal(Iid, Out);
}

unsigned long DACOM_API FDirectX8::AddRef()
{
    if (Outer_ != nullptr && Outer_ != &Inner_)
        return Outer_->AddRef();
    ++Inner_.RefCount;
    return Inner_.RefCount;
}

unsigned long DACOM_API FDirectX8::Release()
{
    return Outer_->Release();
}

// FUN_06d01a06 (CreateInstance's last step): a non-empty AGGDESC+0x10 is strcpy'd into the
// 0x80-byte name buffer +0x24 (unbounded, as in retail), else "RenderPipeline"; then
// FDUMP(0x100003 NOTICE, rp_dx8.cpp line 0x2fb, the buffer). Always 0.
GENRESULT FDirectX8::ApplySectionName(const FRp8AggDesc* Desc)
{
    if (Desc->Extra != nullptr && Desc->Extra[0] != '\0')
        strcpy(SectionName_, Desc->Extra);
    else
        strcpy(SectionName_, kRp8DefaultSection);
    Rp8Fdump(kFdumpNotice, 0x2fb, "NOTICE", SectionName_);
    return GR_OK;
}

// e.g. get_pipeline_state FUN_06d0823e: CMP [this+0x1a8],-1; JNZ body; else FDUMP(0x100002,
// rp_dx8.cpp, 0x6d9, "get_pipeline_state() called outside of startup/shutdown"); OR EAX,-1.
GENRESULT FDirectX8::NeedStartup(const char* Message, int Line)
{
    if (AdapterIndex_ != 0xFFFFFFFF)
        return GR_OK;
    Rp8Fdump(kFdumpWarning, Line, "WARNING", Message);
    return GR_GENERIC;
}

// e.g. clear_buffers FUN_06d09b4c: MOV CL,byte [this+0x184]; AND CL,1; TEST; JNZ body; else
// FDUMP(0x100002, rp_dx8.cpp, 0x962, "clear_buffers() called outside of
// create_buffers/destroy_buffers"); OR EAX,-1. Device_ is not looked at.
GENRESULT FDirectX8::NeedBuffers(const char* Message, int Line)
{
    if ((DeviceReady_ & 1) != 0)
        return GR_OK;
    Rp8Fdump(kFdumpWarning, Line, "WARNING", Message);
    return GR_GENERIC;
}

// e.g. set_world FUN_06d0b09d: CMP [this+0x130],0; JNZ body; else FDUMP(0x100002, rp_dx8.cpp,
// line, "<name>() called outside of device lifetime"); OR EAX,-1. The create_buffers flag is
// not looked at.
GENRESULT FDirectX8::NeedDeviceLifetime(const char* Message, int Line)
{
    if (Device_ != nullptr)
        return GR_OK;
    Rp8Fdump(kFdumpWarning, Line, "WARNING", Message);
    return GR_GENERIC;
}

void FDirectX8::EnumAdapters()
{
    const UINT Count = D3d_->GetAdapterCount();
    std::vector<FRp8Adapter>& Adapters = Adapters_.Value;
    if (Adapters.capacity() < Count)
        Adapters.reserve(Count);
    for (UINT Index = 0; Index < Count; ++Index)
    {
        Adapters.resize(Index + 1);
        FRp8Adapter& Adapter = Adapters[Index];
        D3d_->GetAdapterIdentifier(Index, 2, &Adapter.Identifier);
        D3d_->GetAdapterDisplayMode(Index, &Adapter.DisplayMode);
        Adapter.ModeCount = D3d_->GetAdapterModeCount(Index);
        Adapter.Modes = new FRp8AdapterMode[Adapter.ModeCount];
        for (unsigned long Mode = 0; Mode < Adapter.ModeCount; ++Mode)
        {
            Adapter.Modes[Mode].PfIndex10 = PfIndex(0);
            Adapter.Modes[Mode].PfIndex14 = PfIndex(0);
        }
        D3d_->GetDeviceCaps(Index, (D3DDEVTYPE)(DeviceType_), &Adapter.Caps);
        Adapter.CanRenderWindowed = Adapter.Caps.Caps2 & 0x80000;
    }
}

unsigned long FDirectX8::QueryVidMem()
{
    static const GUID kIidDirectDraw7 = {
        0x15e65ec0, 0x3b9c, 0x11d2, {0xb9, 0x2f, 0x00, 0x60, 0x97, 0x97, 0xea, 0x5b}};
    static const GUID Direct3D7Iid = {
        0xf5049e77, 0x4861, 0x11d2, {0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8}};
    IDirectDraw7* Draw = nullptr;
    // Only the retained Direct3D7 interface's IUnknown::Release contract is needed.
    IUnknown* Direct3D7 = nullptr;
    HRESULT Hr = DirectDrawCreateEx(nullptr, (void**)&Draw, kIidDirectDraw7, nullptr);
    if (SUCCEEDED(Hr))
        Hr = Draw->QueryInterface(Direct3D7Iid, (void**)&Direct3D7);
    unsigned long Best = 0;
    if (SUCCEEDED(Hr))
    {
        const unsigned long Masks[3] = {0x10007000ul, 0x10005000ul, 0x10004040ul};
        for (int Index = 0; Index < 3; ++Index)
        {
            DDSCAPS2 Caps = {};
            Caps.dwCaps = Masks[Index];
            unsigned long Total = 0;
            unsigned long Free = 0;
            if (SUCCEEDED(Draw->GetAvailableVidMem(&Caps, &Total, &Free)) && Total != 0)
            {
                if (Best == 0 || Total < Best)
                    Best = Total;
            }
        }
    }
    if (Best != 0)
    {
        if (Best < 0x1000001ul)
            Best = (Best + 0x7FFFFFul) & 0xFF800000ul;  // round up to 8MB grain
        else if (Best < 0x4000001ul)
            Best = (Best + 0x1FFFFFFul) & 0xFE000000ul;  // round up to 32MB grain
        else
            Best = (Best + 0x3FFFFFFul) & 0xFC000000ul;  // round up to 64MB grain
    }
    if (Direct3D7 != nullptr)
        Direct3D7->Release();
    if (Draw != nullptr)
        Draw->Release();
    return Best;
}

// FUN_06d020ab 06d02490..06d035c3, after the database's Load succeeded: the "found d3d
// device" NOTICE, then one DACOM-CRC lookup per quirk flag (FUN_06d01000), each with its own
// NOTICE at its rp_dx8.cpp line.
void FDirectX8::ApplyMgsdbFlags(FMgsdbDatabase* Db)
{
    const char* DeviceName = Db->GetDeviceName(); // vtable +0x1C
    const char* VendorName = Db->GetVendorName(); // vtable +0x20
    char Text[0x2008];
    Rp8Fdump(kFdumpNotice, 0x3fb, "NOTICE",
             Rp8Sprintf(Text, "found d3d device %s %s", VendorName != nullptr ? VendorName : "???",
                        DeviceName != nullptr ? DeviceName : "???"));

    unsigned char* Base = ObjectBase();
    if (MgsdbHasFlag(Db, "NoAlphaTest"))
    {
        Rp8Fdump(kFdumpNotice, 0x3ff, "NOTICE", "MGS: ALPHA TEST DISALED");
        // Three default render states forced into the +0x2A0 map (FUN_06d16b90 insert, then
        // the node's value overwritten): {name, id, default, value, enabled}.
        FRp8PropertyMap& States = RenderStateMap_.Value;
        FRp8Property& AlphaTest = States[D3DRS_ALPHATESTENABLE];
        AlphaTest.Name = "ALPHATESTENABLE";
        AlphaTest.Id = D3DRS_ALPHATESTENABLE;
        AlphaTest.Default = 0;
        AlphaTest.Value = 0;
        AlphaTest.bEnabled = 1;
        FRp8Property& AlphaRef = States[D3DRS_ALPHAREF];
        AlphaRef.Name = "ALPHAREF";
        AlphaRef.Id = D3DRS_ALPHAREF;
        AlphaRef.Default = 0;
        AlphaRef.Value = 0;
        AlphaRef.bEnabled = 1;
        FRp8Property& AlphaFunc = States[D3DRS_ALPHAFUNC];
        AlphaFunc.Name = "ALPHAFUNC";
        AlphaFunc.Id = D3DRS_ALPHAFUNC;
        AlphaFunc.Default = 8;
        AlphaFunc.Value = 0;
        AlphaFunc.bEnabled = 1;
        RenderStates_.Entries[D3DRS_ALPHATESTENABLE].bEnabled = 0;
        RenderStates_.Entries[D3DRS_ALPHAREF].bEnabled = 0;
        RenderStates_.Entries[D3DRS_ALPHAFUNC].bEnabled = 0;
    }
    if (MgsdbHasFlag(Db, "NoTriLinear"))
    {
        Rp8Fdump(kFdumpNotice, 0x410, "NOTICE", "MGS: TRILINEAR FILTERING DISABLED");
        Ability_[16] = 0;
    }
    if (MgsdbHasFlag(Db, "DefaultBltNotFlip"))
    {
        Rp8Fdump(kFdumpNotice, 0x416, "NOTICE", "MGS: SCREEN FLIP DISABLED");
        if (Field3F4_ != 0)
        {
            Field3F8_ = 1;
            BltNotFlip_ = 1;
        }
        Field3F4_ = 0;
    }
    if (MgsdbHasFlag(Db, "NoParallelism"))
    {
        Rp8Fdump(kFdumpNotice, 0x421, "NOTICE", "MGS: PARALLELISM DISABLED");
        Ability_[19] = 1;
    }
    if (MgsdbHasFlag(Db, "Disable1600x1200"))
    {
        Rp8Fdump(kFdumpNotice, 0x428, "NOTICE", "MGS: 1600x1200 (and above) DISABLED");
        Ability_[17] = 0x640;
    }
    if (MgsdbHasFlag(Db, "Bad4444"))
    {
        Rp8Fdump(kFdumpNotice, 0x42d, "NOTICE", "MGS: 4x4x4x4 TEXTURES DISABLED");
        Ability_[18] = 1;
    }
    if (MgsdbHasFlag(Db, "LimitTextureSize"))
    {
        Rp8Fdump(kFdumpNotice, 0x432, "NOTICE", "MGS: LARGE TEXTURES DISABLED");
        if (Ability_[6] > 0x100)
            Ability_[6] = 0x100;
        if (Ability_[7] > 0x100)
            Ability_[7] = 0x100;
    }
    if (MgsdbHasFlag(Db, "ForceSquareTextures"))
    {
        Rp8Fdump(kFdumpNotice, 0x43b, "NOTICE", "MGS: NON-SQUARE TEXTURES DISABLED");
        Ability_[5] = 1;
    }
    if (MgsdbHasFlag(Db, "Bad8888"))
    {
        Rp8Fdump(kFdumpNotice, 0x440, "NOTICE", "MGS: 32 BIT TEXTURES DISABLED");
        if (Field388_ != 0)
        {
            Field38C_ = 1;
            TextureAllow32Bit_ = 0;
        }
        Field388_ = 0;
    }
    if (MgsdbHasFlag(Db, "FL_BAD_DXTN"))
    {
        Rp8Fdump(kFdumpNotice, 0x449, "NOTICE", "MGS: DTXN USAGE DISABLED");
        if (Field394_ != 0)
        {
            Field398_ = 1;
            TextureAllowDxt_ = 0;
        }
        Field394_ = 0;
    }
    if (MgsdbHasFlag(Db, "FL_BAD_MOD2X"))
    {
        Rp8Fdump(kFdumpNotice, 0x450, "NOTICE", "MGS: MOD2X USAGE DISABLED");
        if (*(int*)(Base + kOffFmtMod2x) != 0)
        {
            *(unsigned long*)(Base + kOffFmtMod2xMirror) = 1;
            *(unsigned long*)(Base + kOffFmtMod2xAlt) = 1;
        }
        *(unsigned long*)(Base + kOffFmtMod2x) = 0;
    }
    if (MgsdbHasFlag(Db, "FL_BAD_ENVMAP"))
    {
        Rp8Fdump(kFdumpNotice, 0x457, "NOTICE", "MGS: ENVMAP USAGE DISABLED");
        Ability_[8] = 0;
    }
    if (MgsdbHasFlag(Db, "FL_BAD_WFOG"))
    {
        Rp8Fdump(kFdumpNotice, 0x45d, "NOTICE", "MGS: WFOG USAGE DISABLED");
        Ability_[13] = 0;
    }
    if (MgsdbHasFlag(Db, "UnknownHardware"))
    {
        Rp8Fdump(kFdumpNotice, 0x466, "NOTICE", "MGS: Unknown hardware");
        Ability_[15] = 1;
    }
    if (MgsdbHasFlag(Db, "UnsupportedDriver"))
    {
        Rp8Fdump(kFdumpNotice, 0x46b, "NOTICE", "MGS: Unsupported Driver");
        Ability_[15] = 2;
    }
    if (MgsdbHasFlag(Db, "UnsupportedHardware"))
    {
        Rp8Fdump(kFdumpNotice, 0x470, "NOTICE", "MGS: Unsupported hardware");
        Ability_[15] = 3;
    }
}

// FUN_06d020ab's closing loop (06d035d8..06d03768; both early exits of the MGSDB part,
// 06d02400 / 06d02412, and its normal end 06d0248a / 06d03556 land here): every enabled node
// of the ability map +0x28C overrides [this + Id*4 + 0x3FC] with its Value.
void FDirectX8::ApplyAbilityOverrides()
{
    FRp8PropertyMap& Map = AbilityMap_.Value;
    for (FRp8PropertyMap::iterator It = Map.begin(); It != Map.end(); ++It)
    {
        if (It->second.bEnabled != 0)
            Ability_[It->second.Id] = It->second.Value;
    }
}

// FUN_06d020ab(caps), from startup and create_buffers: the 0x14 abilities from the caps
// (06d020bf..06d0239b), the texture-stage count into pipeline-state id 6 (06d023a1..06d023ef),
// the MGSDB database (06d023f1..06d035d1) and the INI ability overrides (06d035d8..06d03768).
void FDirectX8::ApplyDeviceCaps(const D3DCAPS8* Caps)
{
    memset(Ability_, 0, 0x14 * sizeof(unsigned long));
    Ability_[3] = VidMem_;
    Ability_[1] = (Caps->RasterCaps & 0x20000) != 0;
    Ability_[2] = (Caps->RasterCaps & 0x100000) != 0;
    Ability_[4] = ((Caps->DevCaps & 0x10000) != 0 && (Caps->DevCaps & 0x8000) != 0) ? 1 : 0;
    Ability_[10] = (Caps->RasterCaps & 0x80) != 0;
    Ability_[11] = (Caps->RasterCaps & 0x100) != 0;
    Ability_[12] = (Caps->RasterCaps & 0x10000) != 0;
    const bool bWFog = (Caps->RasterCaps & 0x100000) != 0;
    const bool bZFog = (Caps->RasterCaps & 0x200000) != 0;
    const bool bTnl = Ability_[4] != 0;
    bool bUseWFog = bWFog;
    if (bWFog && bZFog && !bTnl)
        bUseWFog = false;
    Ability_[13] = bUseWFog ? 1 : 0;
    Ability_[9] = (Caps->RasterCaps & 0x4000) != 0;
    Ability_[14] = (Caps->RasterCaps & 0x1000) != 0;
    Ability_[8] = ((Caps->TextureCaps & 0x800) != 0 && (Caps->VertexProcessingCaps & 1) != 0 &&
                   Ability_[4] != 0)
                      ? 1
                      : 0;
    Ability_[5] = (Caps->TextureCaps & 0x20) != 0;
    Ability_[16] = (Caps->TextureFilterCaps & 0x20000) != 0;
    Ability_[6] = Caps->MaxTextureWidth != 0 ? Caps->MaxTextureWidth : 0x100;
    Ability_[7] = Caps->MaxTextureHeight != 0 ? Caps->MaxTextureHeight : 0x100;

    unsigned long Stages = Caps->MaxTextureBlendStages;
    if (Stages >= 8)
        Stages = 7;
    // Pipeline-state id 6 {value +0x39C, +0x3A0, +0x3A4}: written only when +0x3A0 is set.
    unsigned long* StageSlot = (unsigned long*)(ObjectBase() + kOffStageCount);
    if (StageSlot[1] != 0)
    {
        StageSlot[2] = 1;
        StageSlot[0] = Stages;
    }

    if (MgsdbPath_[0] != '\0')
    {
        FMgsdbDatabase* Db = FMgsdbDatabase::Create();
        if (Db != nullptr)
        {
            D3DADAPTER_IDENTIFIER8 Ident;
            D3d_->GetAdapterIdentifier(AdapterIndex_, 2, &Ident);
            if (Db->Load(MgsdbPath_, &Ident, Caps, 0, Ability_[3], 0))
                ApplyMgsdbFlags(Db);
            Db->Destroy(); // vtable +0x4
        }
    }

    ApplyAbilityOverrides();
}

D3DFORMAT FDirectX8::PickDepthFormat(D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat,
                                     int* WantedZ, int* WantedS)
{
    const FZFormatRow* Best = nullptr;
    int BestSDelta = 0x64;
    int BestZDelta = 0x64;
    for (unsigned int Index = 0; Index < sizeof(kZTable) / sizeof(kZTable[0]); ++Index)
    {
        const FZFormatRow* Row = &kZTable[Index];
        const int ZDelta = *WantedZ - Row->ZBits;
        const int SDelta = *WantedS - Row->StencilBits;
        if (SDelta < 0 || ZDelta < 0)
            continue;
        if (Best != nullptr && SDelta > BestSDelta)
            continue;
        if (D3d_->CheckDeviceFormat(0, D3DDEVTYPE_HAL, AdapterFormat, D3DUSAGE_DEPTHSTENCIL,
                                    D3DRTYPE_SURFACE, Row->Format) < 0)
            continue;
        if (D3d_->CheckDepthStencilMatch(0, D3DDEVTYPE_HAL, AdapterFormat, BackBufferFormat,
                                         Row->Format) < 0)
            continue;
        if (Best != nullptr && SDelta >= BestSDelta && ZDelta >= BestZDelta)
            continue;
        Best = Row;
        BestSDelta = SDelta;
        BestZDelta = ZDelta;
    }
    if (Best == nullptr)
        return D3DFMT_UNKNOWN;
    *WantedZ = Best->ZBits;
    *WantedS = Best->StencilBits;
    return Best->Format;
}

// Keep format-specific capability gates before asking the device.
bool FDirectX8::CheckTextureFormat(unsigned int Format, unsigned long Usage, D3DRESOURCETYPE RType)
{
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 3051, "WARNING",
                 "check_texture_format() called outside of device lifetime");
        return true;
    }
    if (Format == 0)
        return false;
    // this+0x378 gate (rp8.dll.c:5208): TEXTURE_ALLOW_8BIT (default disabled) blocks D3DFMT_P8.
    if (TextureAllow8Bit_ == 0 && Format == 1)
        return false;
    // Unconditional check (rp8.dll.c:5212-5216), not gated by any capability flag.
    if (Format != 1 && RetailPfBppBits(Format) < 0x10 && FormatClassKeyFromPfId(Format) == 0)
        return false;
    // this+900(0x384) gate (rp8.dll.c:5217-5220): TEXTURE_ALLOW_32BIT (default enabled)
    // blocks any format whose bpp is >= 32 once hardware is flagged "Bad8888".
    if (TextureAllow32Bit_ == 0 && RetailPfBppBits(Format) > 0x1F)
        return false;
    // this+0x390 gate (rp8.dll.c:5221-5230): TEXTURE_ALLOW_DXT (default enabled) blocks the
    // compressed pfenum range [10..14] (DXT1..DXT5) once hardware is flagged "FL_BAD_DXTN".
    if (TextureAllowDxt_ == 0 && (int)(Format) >= 10 && (int)(Format) <= 14)
        return false;
    // 06d0d5b4: D3D+0x28 CheckDeviceFormat(+0x1A8, +0x1AC, pf_to_d3d(+0x230), usage, rtype,
    // pf_to_d3d(*format)); SETZ -- true only for hr == 0.
    const unsigned int AdapterFormat = PfToD3d(Field230_);
    const unsigned int CheckFormat = PfToD3d(Format);
    const HRESULT Hr = D3d_->CheckDeviceFormat(AdapterIndex_, (D3DDEVTYPE)(DeviceType_),
                                               (D3DFORMAT)(AdapterFormat), Usage, RType,
                                               (D3DFORMAT)(CheckFormat));
    return Hr == 0;
}


// Retail FUN_06d04693 -- linear scan [FormatCatalogBegin_, FormatCatalogEnd_) for a node
// whose Key matches. Exact port (node stride confirmed 0xC bytes / 3 dwords).
FormatClassNode* FDirectX8::FindFormatClass(unsigned int Key)
{
    for (FormatClassNode* Node = FormatCatalogBegin_; Node != FormatCatalogEnd_; ++Node)
    {
        if (Node->Key == Key)
            return Node;
    }
    return nullptr;
}

FormatClassNode* FDirectX8::FindOrInsertFormatClass(unsigned int Key)
{
    FormatClassNode* Existing = FindFormatClass(Key);
    if (Existing != nullptr)
        return Existing;
    const unsigned int Count = (FormatCatalogBegin_ == nullptr)
        ? 0
        : (unsigned int)(FormatCatalogEnd_ - FormatCatalogBegin_);
    const unsigned int Spare = (unsigned int)(FormatCatalogCap_ - FormatCatalogEnd_);
    if (Spare < 1)
    {
        const unsigned int NewCapacity = Count + (Count > 1 ? Count : 1);
        FormatClassNode* NewArray = new FormatClassNode[NewCapacity];
        for (unsigned int Index = 0; Index < Count; ++Index)
            NewArray[Index] = FormatCatalogBegin_[Index];
        delete[] FormatCatalogBegin_;
        FormatCatalogBegin_ = NewArray;
        FormatCatalogEnd_ = NewArray + Count;
        FormatCatalogCap_ = NewArray + NewCapacity;
    }
    FormatCatalogEnd_->Key = Key;
    FormatCatalogEnd_->Count = 0;
    FormatCatalogEnd_->Values = nullptr;
    ++FormatCatalogEnd_;
    return FormatCatalogBegin_ + (Count + 1 - 1);
}

// Retail FUN_06d16530 -- resets one node's candidate list (frees Values, Count=0), used by
// PopulateFormatCatalog's ini-override path when a "TextureFormatClasses" line replaces an
// existing class's candidates instead of appending to them.
void FDirectX8::ClearFormatClassCandidates(FormatClassNode* Node)
{
    if (Node == nullptr)
        return;
    delete[] Node->Values;
    Node->Values = nullptr;
    Node->Count = 0;
}

void FDirectX8::ClearFormatCatalog()
{
    for (FormatClassNode* Node = FormatCatalogBegin_; Node != FormatCatalogEnd_; ++Node)
        ClearFormatClassCandidates(Node);
    FormatCatalogEnd_ = FormatCatalogBegin_;
}

void FDirectX8::AppendFormatCandidate(FormatClassNode* Node, unsigned int Value)
{
    if (Node == nullptr || Value == 0)
        return;
    unsigned int* NewValues = new (std::nothrow) unsigned int[Node->Count + 1];
    if (NewValues == nullptr)
        return;
    for (unsigned int Index = 0; Index < Node->Count; ++Index)
        NewValues[Index] = Node->Values[Index];
    NewValues[Node->Count] = Value;
    delete[] Node->Values;
    Node->Values = NewValues;
    Node->Count += 1;
}

unsigned int FDirectX8::ResolvePixelFormatByComponents(unsigned int Bpp, unsigned int RBits,
                                                       unsigned int GBits, unsigned int BBits,
                                                       unsigned int AlphaBits)
{
    for (unsigned int Index = 0; Index < sizeof(kPfDescriptors) / sizeof(kPfDescriptors[0]); ++Index)
    {
        const FPixelFormatDescriptor& D = kPfDescriptors[Index];
        if (D.Bpp == Bpp && D.RBits == RBits && D.RBits == BBits && D.GBits == GBits &&
            D.ABits == AlphaBits)
            return D.PfId;
    }
    return 0;
}

unsigned int FDirectX8::FormatClassKeyFromPfId(unsigned int PfId)
{
    const int Signed = (int)(PfId);
    if (Signed <= 9)
        return 0;
    if (Signed < 0x16)
        return kPfToD3d[PfId];
    return PfId;
}

// Populate format candidates in profile order; retain the documented malformed-line behavior.
void FDirectX8::PopulateFormatCatalog()
{
    ClearFormatCatalog();

    const unsigned int kKeyDAOP = 0x504F4144; // "DAOP"
    const unsigned int kKeyDAOT = 0x544F4144; // "DAOT"
    const unsigned int kKeyDAA1 = 0x31414144; // "DAA1"
    const unsigned int kKeyDAA4 = 0x34414144; // "DAA4"
    const unsigned int kKeyDAA8 = 0x38414144; // "DAA8"
    const unsigned int kKeyDXT1 = 0x31545844; // "DXT1"
    const unsigned int kKeyDXT2 = 0x32545844; // "DXT2"
    const unsigned int kKeyDXT3 = 0x33545844; // "DXT3"
    const unsigned int kKeyDXT4 = 0x34545844; // "DXT4"
    const unsigned int kKeyDXT5 = 0x35545844; // "DXT5"

    FormatClassNode* Node = FindOrInsertFormatClass(kKeyDAOP);
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(8, 0, 0, 0, 0));     // P8
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 5, 6, 5, 0));  // R5G6B5
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 5, 5, 5, 0));  // X1R5G5B5
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 5, 5, 5, 1));  // A1R5G5B5
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x18, 8, 8, 8, 0));  // R8G8B8
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x20, 8, 8, 8, 8));  // A8R8G8B8

    Node = FindOrInsertFormatClass(kKeyDAOT);
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 5, 6, 5, 0));
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 5, 5, 5, 0));
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 5, 5, 5, 1));
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x18, 8, 8, 8, 0));
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x20, 8, 8, 8, 8));

    Node = FindOrInsertFormatClass(kKeyDAA1);
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 5, 5, 5, 1));
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 4, 4, 4, 4));
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x20, 8, 8, 8, 8));

    Node = FindOrInsertFormatClass(kKeyDAA4);
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 4, 4, 4, 4));
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x20, 8, 8, 8, 8));
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 5, 5, 5, 1));

    Node = FindOrInsertFormatClass(kKeyDAA8);
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x20, 8, 8, 8, 8));
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 4, 4, 4, 4));
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 5, 5, 5, 1));

    Node = FindOrInsertFormatClass(kKeyDXT1);
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 5, 5, 5, 1));
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 4, 4, 4, 4));

    Node = FindOrInsertFormatClass(kKeyDXT2);
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x20, 8, 8, 8, 8));
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 4, 4, 4, 4));

    Node = FindOrInsertFormatClass(kKeyDXT3);
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x20, 8, 8, 8, 8));
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 4, 4, 4, 4));

    Node = FindOrInsertFormatClass(kKeyDXT4);
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x20, 8, 8, 8, 8));
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 4, 4, 4, 4));

    Node = FindOrInsertFormatClass(kKeyDXT5);
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x20, 8, 8, 8, 8));
    AppendFormatCandidate(Node, ResolvePixelFormatByComponents(0x10, 4, 4, 4, 4));

    IProfileParser* Parser = nullptr;
    if (DACOM_Acquire()->QueryInterface(Iids::ProfileParser, (void**)(&Parser)) >= 0)
    {
        void* Section = Parser->OpenSection("TextureFormatClasses", 3);
        if (Section != nullptr)
        {
            // Retail's buffer is 0x400 bytes; the extra zeroed byte keeps the unconditional
            // step past the '=' search (below) inside the array for a 0x3FF-character line
            // without '=' (retail reads the next stack byte there).
            char Line[0x400 + 1];
            Line[0x400] = '\0';
            int LineIndex = 0;
            while (Parser->ReadLine(Section, LineIndex, Line, 0x400) != 0)
            {
                ++LineIndex;
                char* Semicolon = strchr(Line, ';');
                if (Semicolon != nullptr)
                    *Semicolon = '\0';
                char* Cursor = Line;
                while (*Cursor != '\0' && (*Cursor == ' ' || *Cursor == '\t'))
                    ++Cursor;
                char* TokenStart = Cursor;
                if (_strnicmp(Cursor, "clear", 3) == 0)
                {
                    ClearFormatCatalog();
                    continue;
                }
                // 06d05b55: the key is the four bytes at the token, little-endian, whatever
                // they are (bytes past a short token are stale buffer content, as in retail).
                unsigned int Key = 0;
                memcpy(&Key, TokenStart, sizeof(Key));
                FormatClassNode* OverrideNode = FindOrInsertFormatClass(Key);
                if (OverrideNode == nullptr)
                    continue;
                if (OverrideNode->Count != 0)
                    ClearFormatClassCandidates(OverrideNode);
                while (*Cursor != '\0' && *Cursor != '=')
                    ++Cursor;
                // 06d05bed: stepped past unconditionally, also when the scan stopped at the
                // terminator.
                ++Cursor;
                while (*Cursor != '\0')
                {
                    while (*Cursor != '\0' && (*Cursor == ' ' || *Cursor == '\t'))
                        ++Cursor;
                    char* ValueStart = Cursor;
                    while (*Cursor != '\0' && *Cursor != ',')
                        ++Cursor;
                    if (*Cursor != '\0')
                    {
                        *Cursor = '\0';
                        ++Cursor;
                    }
                    if (ValueStart != Cursor)
                    {
                        int Bpp = 0, R = 0, G = 0, B = 0, A = 0;
                        sscanf(ValueStart, "%d:%d:%d:%d:%d", &Bpp, &R, &G, &B, &A);
                        AppendFormatCandidate(
                            OverrideNode,
                            ResolvePixelFormatByComponents(
                                (unsigned int)(Bpp), (unsigned int)(R),
                                (unsigned int)(G), (unsigned int)(B),
                                (unsigned int)(A)));
                    }
                }
            }
            Parser->CloseSection(Section);
        }
    }
    // 06d05d8f: FUN_06d167a0 -- release the local reference when it is set.
    if (Parser != nullptr)
    {
        Parser->Release();
        Parser = nullptr;
    }
}

// Retail RP8 FUN_06d163f0 — vertex stride from D3D8 FVF dword.
static unsigned int VertexStrideFromFvf(unsigned long Fvf)
{
    unsigned int Stride = 0;
    if ((Fvf & 0xc) == 0xc)
        Stride = 0x1c;
    else if ((Fvf & 4) != 0)
        Stride = 0x10;
    else if ((Fvf & 2) != 0)
        Stride = 0xc;

    unsigned int NormalSize = 0;
    if ((Fvf & 0x10) != 0)
        NormalSize = 0xc;

    unsigned int DiffuseSize = 0;
    if ((Fvf & 0x40) != 0)
        DiffuseSize = 4;
    if ((Fvf & 0x80) != 0)
        DiffuseSize += 4;

    unsigned int TexSize = 0;
    const unsigned int TexCount = (Fvf & 0xf00) >> 8;
    for (unsigned int TexIndex = 0; TexIndex < TexCount; ++TexIndex)
    {
        switch ((Fvf >> (TexIndex * 2 + 16)) & 3)
        {
        case 0:
            TexSize += 8;
            break;
        case 1:
            TexSize += 12;
            break;
        case 2:
            TexSize += 16;
            break;
        case 3:
            TexSize += 4;
            break;
        }
    }
    return Stride + NormalSize + DiffuseSize + TexSize;
}

// Requested states are deferred until Flush; failed device writes leave Cached unchanged.
void FRp8RenderStateCache::Initialize() // formerly InitRsQueue
{
    for (unsigned int Index = 0; Index < StateCount; ++Index)
    {
        FRp8RenderStateEntry& Entry = Entries[Index];
        Entry.Cached = 0;
        Entry.Pending = 0;
        Entry.Generation = 0xFFFFFFFF;
        Entry.bEnabled = 1;
    }
    Generation = 0;
}

void FRp8RenderStateCache::ResetGeneration() // formerly RsQueueResetGeneration
{
    DirtyCount = 0;
    ++Generation;
}

// Returns the requested value even when a later device write fails.
unsigned long FRp8RenderStateCache::GetPending(unsigned int StateId) const // formerly RsQueueReadPending
{
    return Entries[StateId].Pending;
}

HRESULT FRp8RenderStateCache::Apply(IDirect3DDevice8* Device, unsigned int StateId,
                                    unsigned long Value) // formerly RsQueueApply
{
    const HRESULT Hr = Device->SetRenderState((D3DRENDERSTATETYPE)StateId, Value);
    if (SUCCEEDED(Hr))
    {
        Entries[StateId].Pending = Value;
        Entries[StateId].Cached = Value;
    }
    return Hr;
}

static bool g_RsQueueOverflowReported = false;

// Disabled states ignore requests; the result indicates membership in this generation.
bool FRp8RenderStateCache::MarkDirty(unsigned int StateId, unsigned long Value) // formerly RsQueueMarkDirty
{
    FRp8RenderStateEntry& Entry = Entries[StateId];
    if (Entry.bEnabled != 0)
    {
        if (Entry.Generation != Generation && Entry.Cached != Value)
        {
            if (DirtyCount < StateCount)
            {
                DirtyStates[DirtyCount] = StateId;
                ++DirtyCount;
                Entry.Generation = Generation;
            }
            else if (!g_RsQueueOverflowReported)
            {
                g_RsQueueOverflowReported = true;
                Rp8Fdump(kFdumpError, 0x41, "ERROR",
                         "critical error: too many renderstate requests", kSrcStateCache);
            }
        }
        Entry.Pending = Value;
    }
    return Entry.Generation == Generation;
}

void FRp8RenderStateCache::Flush(IDirect3DDevice8* Device) // formerly RsQueueFlush
{
    for (unsigned long Index = 0; Index < DirtyCount; ++Index)
    {
        const unsigned long StateId = DirtyStates[Index];
        const FRp8RenderStateEntry& Entry = Entries[StateId];
        if (Entry.Pending != Entry.Cached)
            Apply(Device, StateId, Entry.Pending);
    }
    ResetGeneration();
}

// formerly Rp8CommitTransform
void FRp8TransformCache::Commit(IDirect3DDevice8* Device, D3DTRANSFORMSTATETYPE Type)
{
    if (bCommitted != 0)
        return;
    bApplyMarker = 0;
    bCommitted = 1;
    const HRESULT Hr = Device->SetTransform(Type, (const D3DMATRIX*)Values);
    bCommitted = Hr >= 0 ? 1 : 0;
}

// Flush deferred states, stream bindings, projection/view/world transforms, then lights.
// formerly static FlushDrawState
void FDirectX8::FlushDrawState(IDirect3DDevice8* Device)
{
    RenderStates_.Flush(Device);
    FRp8StreamCache& Cache = StreamCache_;
    bool bStreamDone = false;
    if ((Cache.Flags.Dirty & StreamDirtyIndices) != 0)
    {
        Cache.Flags.Dirty = (unsigned char)(Cache.Flags.Dirty & ~StreamDirtyIndices);
        FRp8Ib* Ib = Cache.IndexBuffer;
        IDirect3DIndexBuffer8* IbGpu = nullptr;
        if (Ib != nullptr)
            IbGpu = Ib->Gpu;
        const HRESULT IndicesHr = Device->SetIndices(IbGpu, Cache.BaseVertex);
        if (IndicesHr < 0)
        {
            FdumpHr("flush_indices: %s", IndicesHr, 0x34, kSrcDx8ShaderInl);
            bStreamDone = true;
        }
    }
    if (!bStreamDone && (Cache.Flags.Dirty & StreamDirtyVertexSource) != 0)
    {
        Cache.Flags.Dirty = (unsigned char)(Cache.Flags.Dirty & ~StreamDirtyVertexSource);
        FRp8Vb* Vb = Cache.VertexBuffer;
        IDirect3DVertexBuffer8* VbGpu = nullptr;
        unsigned long Shader = 0;
        if (Vb == nullptr)
        {
            Shader = Cache.Fvf;
        }
        else
        {
            VbGpu = Vb->Gpu;
            if (Cache.Fvf != 0)
                Shader = Cache.Fvf;
            else
                Shader = Vb->Fvf;
        }
        bool bShaderOk = true;
        if (Shader != 0)
        {
            const HRESULT ShaderHr = Device->SetVertexShader(Shader);
            if (ShaderHr < 0)
            {
                FdumpHr("flush_vertex_shader: %s", ShaderHr, 0x4f, kSrcDx8ShaderInl);
                bShaderOk = false;
            }
        }
        if (bShaderOk)
        {
            const HRESULT StreamHr = Device->SetStreamSource(0, VbGpu, VertexStrideFromFvf(Shader));
            if (StreamHr < 0)
                FdumpHr("flush_stream_source: %s", StreamHr, 0x54, kSrcDx8ShaderInl);
        }
    }
    Transforms_.Projection.Commit(Device, D3DTS_PROJECTION);
    Transforms_.View.Commit(Device, D3DTS_VIEW);
    Transforms_.World.Commit(Device, D3DTS_WORLD);
    // 06d0466c: FUN_06d2d032(this + 0x21F4, [this+0x130]); the hr is stored and not used by
    // any caller.
    LightManager_.Flush(Device);
}

// formerly ReadTssCache
unsigned long FRp8DeviceStateCaches::ReadTextureStageState(IDirect3DDevice8* Device,
                                                         unsigned long Stage,
                                                         D3DTEXTURESTAGESTATETYPE Type)
{
    FRp8TextureStageState& Entry = TextureStates[Stage][Type];
    if (Entry.bValid == 0)
    {
        const HRESULT Hr = Device->GetTextureStageState(Stage, Type, &Entry.Value);
        Entry.bValid = Hr >= 0 ? 1 : 0;
        if (Entry.bValid == 0)
            Entry.Value = 0;
    }
    return Entry.Value;
}

// formerly ResetStreamCache
void FRp8StreamCache::Reset()
{
    Fvf = 0;
    VertexBuffer = nullptr;
    IndexBuffer = nullptr;
    BaseVertex = 0xFFFFFFFF;
    memset(&Flags, 0, sizeof(Flags));
}


// FUN_06d1bc60 (thiscall on the VB node): Gpu (+0x20) set -> if Locked (+0x10) {Locked = 0;
// Unlock (+0x30)}; Release; Gpu = 0. Every caller passes a live node.
// formerly ReleaseVbGpu
void FRp8Vb::ReleaseGpu()
{
    if (Gpu == nullptr)
        return;
    if (Locked != 0)
    {
        Locked = 0;
        Gpu->Unlock();
    }
    Gpu->Release();
    Gpu = nullptr;
}

// The same release block inlined for index buffers (destroy_buffers 06d09a70, destroy_ib
// 06d1312b, resize_ib 06d1327b/06d133a2): Gpu (+4) set -> if +8 {+8 = 0; Unlock (+0x30)};
// Release; Gpu = 0.
static void ReleaseIbGpu(FRp8Ib* Wrapper)
{
    if (Wrapper->Gpu == nullptr)
        return;
    if (Wrapper->LockedBits != nullptr)
    {
        Wrapper->LockedBits = nullptr;
        Wrapper->Gpu->Unlock();
    }
    Wrapper->Gpu->Release();
    Wrapper->Gpu = nullptr;
}

// FUN_06d19aa0 (VC6 map insert) of the pair {Fvf, FUN_06d1b480(0x200)}: the pool entry for
// Fvf, a fresh {Flags 0x200|8, everything else 0} node when there is none. (Retail's
// temporary leaves +0x1C uninitialized; every grow zeroes it before use.)
FRp8Vb* FDirectX8::FindOrInsertPoolVb(unsigned int Fvf)
{
    FRp8Vb Fresh = {};
    Fresh.Flags = kVbPoolUsage;
    std::pair<std::map<unsigned long, FRp8Vb>::iterator, bool> Inserted =
        VbPool_.Value.insert(std::pair<const unsigned long, FRp8Vb>(Fvf, Fresh));
    return &Inserted.first->second;
}

// Grow the pooled buffer in blocks of 0x400 vertices.
void FDirectX8::GrowPoolVb(FRp8Vb* Wrapper, unsigned int Fvf, unsigned int Count)
{
    if (Wrapper->Gpu != nullptr && Wrapper->Count >= Count)
        return;
    VbPoolBytes_ -= FvfStride(Wrapper->Fvf) * Wrapper->Count;
    unsigned int NewCount = Wrapper->Count;
    if (NewCount == 0)
        NewCount = 0x400;
    if (NewCount < Count)
        NewCount = Count + 0x3FF;
    NewCount = (NewCount >> 10) << 10;
    Wrapper->ReleaseGpu();
    Wrapper->Fvf = Fvf;
    Wrapper->Count = NewCount;
    Wrapper->Used = 0;
    Wrapper->Field1C = 0;
    Device_->CreateVertexBuffer(FvfStride(Wrapper->Fvf) * Wrapper->Count, Wrapper->Flags, Fvf,
                                D3DPOOL_DEFAULT, &Wrapper->Gpu);
    VbPoolBytes_ += FvfStride(Wrapper->Fvf) * Wrapper->Count;
    if (Wrapper == StreamCache_.VertexBuffer)
        SelectVb(nullptr);
}

// The destructor's FUN_06d1a780 over the pool (06d018d6): every node's value goes through
// FUN_06d1bc60 before the node is freed.
void FDirectX8::ReleasePoolVbs()
{
    std::map<unsigned long, FRp8Vb>& Pool = VbPool_.Value;
    for (std::map<unsigned long, FRp8Vb>::iterator It = Pool.begin(); It != Pool.end(); ++It)
        It->second.ReleaseGpu();
    Pool.clear();
}

// formerly MarkStreamVb / MarkStreamFvf
void FRp8StreamCache::SelectVertexStream(FRp8Vb* Buffer, unsigned long InFvf)
{
    if (Buffer != VertexBuffer || InFvf != Fvf)
    {
        Flags.Dirty = (unsigned char)(Flags.Dirty | StreamDirtyVertexSource);
        VertexBuffer = Buffer;
        Fvf = InFvf;
    }
}

// formerly MarkStreamScratchIb / SelectIb cache stores
void FRp8StreamCache::SelectIndices(FRp8Ib* Buffer, unsigned int InBaseVertex)
{
    if (Buffer != IndexBuffer || InBaseVertex != BaseVertex)
    {
        Flags.Dirty = (unsigned char)(Flags.Dirty | StreamDirtyIndices);
        IndexBuffer = Buffer;
        BaseVertex = InBaseVertex;
    }
}

// Ring-buffer offsets count 16-bit indices; OutBits is required.
// formerly LockIbRange
HRESULT FRp8Ib::LockRange(unsigned int Start, unsigned int Count, void** OutBits,
                           unsigned int* OutStart, unsigned char bSysLock)
{
    if (Gpu == nullptr)
        return E_FAIL;
    if (Count > Capacity)
        return E_FAIL;
    unsigned long LockFlags = bSysLock != 0 ? 0ul : D3DLOCK_NOSYSLOCK;
    if ((Flags & D3DUSAGE_DYNAMIC) != 0)
    {
        if (Cursor + Count > Capacity)
        {
            LockFlags |= D3DLOCK_DISCARD;
            Cursor = 0;
            Start = 0;
        }
        else
        {
            LockFlags |= D3DLOCK_NOOVERWRITE;
            Start = Cursor;
        }
    }
    const HRESULT Hr = Gpu->Lock(Start * 2u, Count * 2u,
                                     (BYTE**)&LockedBits, LockFlags);
    if (Hr >= 0)
    {
        if (OutStart != nullptr)
            *OutStart = Start;
        *OutBits = LockedBits;
        Cursor = Start + Count;
    }
    return Hr;
}

unsigned int FDirectX8::PrimCountFromVerts(unsigned long Prim, unsigned int Count)
{
    // FUN_06d010cf: JA on the type (unsigned); /2 is CDQ;SUB;SAR and /3 is CDQ;IDIV -- signed
    // divisions, so a negative count stays <= 0 for the callers' JLE test.
    switch (Prim)
    {
    case 0:
        return 0;
    case 1:
        return Count;
    case 2:
        return (unsigned int)((int)(Count) / 2);
    case 3:
        return Count - 1;
    case 4:
        return (unsigned int)((int)(Count) / 3);
    case 5:
        return Count - 2;
    case 6:
        return Count - 2;
    default:
        return 0;
    }
}

unsigned char* FDirectX8::ObjectBase()
{
    return (unsigned char*)(this);
}

int FDirectX8::PipelineStateValue(int Id)
{
    // Retail reads [this + 0x354 + id*0xC] inline at each site (all constant ids); no range test.
    int* Slot = (int*)(ObjectBase() + 0x354 + Id * 0xC);
    return Slot[0];
}

unsigned long FDirectX8::StageCount()
{
    return *(unsigned long*)(ObjectBase() + kOffStageCount);
}

// formerly StoreMatrix12
void FRp8TransformCache::StoreAffine(const float* M12)
{
    Values[0] = M12[0];
    Values[4] = M12[1];
    Values[8] = M12[2];
    Values[1] = M12[3];
    Values[5] = M12[4];
    Values[9] = M12[5];
    Values[2] = M12[6];
    Values[6] = M12[7];
    Values[10] = M12[8];
    Values[12] = M12[9];
    Values[13] = M12[10];
    Values[14] = M12[11];
    Values[3] = 0.0f;
    Values[7] = 0.0f;
    Values[11] = 0.0f;
    Values[15] = 1.0f;
    bCommitted = 0;
}

// formerly LoadMatrix12
void FRp8TransformCache::LoadAffine(float* Out12) const
{
    Out12[0] = Values[0];
    Out12[1] = Values[4];
    Out12[2] = Values[8];
    Out12[3] = Values[1];
    Out12[4] = Values[5];
    Out12[5] = Values[9];
    Out12[6] = Values[2];
    Out12[7] = Values[6];
    Out12[8] = Values[10];
    Out12[9] = Values[12];
    Out12[10] = Values[13];
    Out12[11] = Values[14];
}

// DAT_06d70540: rp8.dll's lazily created 3DMathEngine. Every user (FUN_06d15e60, FUN_06d15ec0,
// FUN_06d172f0, get_modelview FUN_06d0bf39) does `if (DAT_06d70540 == 0) DAT_06d70540 =
// FUN_06d5cd40();` and then calls through it without a null check.
static I3DMathEngine* G_Rp8MathEngine = nullptr;

// FUN_06d5cd40 (rp8.dll.txt 149794): a 0xC descriptor {0xC, "3DMathEngine", 0} on the stack,
// DACOM_Acquire()->CreateInstance(&Desc, &Out) (ICOManager +0x0C), returns Out.
static I3DMathEngine* Rp8CreateMathEngine()
{
    FMathDesc Desc;
    Desc.Size = sizeof(FMathDesc);
    Desc.InterfaceName = "3DMathEngine";
    Desc.Implementation = nullptr;
    void* Instance = nullptr;
    ICOManager* Manager = DACOM_Acquire();
    Manager->CreateInstance((AGGDESC*)(&Desc), &Instance);
    return (I3DMathEngine*)(Instance);
}

static I3DMathEngine* Rp8MathEngine()
{
    if (G_Rp8MathEngine == nullptr)
        G_Rp8MathEngine = Rp8CreateMathEngine();
    return G_Rp8MathEngine;
}

// FUN_06d15d10 then `FDIVR [1.0f]; FSTP float` and three `FLD float; FMUL float; FSTP float`
// (the inline normalise set_lookat does three times). The squared length and sqrt stay on the
// x87 stack, so they are double here; the reciprocal and each component are stored as float.
// No zero-length check: retail divides by whatever _CIsqrt returned.
static void Rp8NormalizeInPlace(FVector& Vector)
{
    const double Length = std::sqrt((double)Vector.X * Vector.X +
                                    (double)Vector.Y * Vector.Y +
                                    (double)Vector.Z * Vector.Z);
    const float Inverse = (float)(1.0 / Length);
    Vector.X = (float)((double)Vector.X * Inverse);
    Vector.Y = (float)((double)Vector.Y * Inverse);
    Vector.Z = (float)((double)Vector.Z * Inverse);
}

static_assert(sizeof(FTransform) == 12 * sizeof(float), "FTransform is the 12-float DA transform");

void FDirectX8::MulZFlip12(float* Out12, const float* In12)
{
    // The engine product uses a temporary, so Out12 may alias In12.
    FTransform ZFlip;
    ZFlip.Rotation.D[0][0] = 1.0f;
    ZFlip.Rotation.D[0][1] = 0.0f;
    ZFlip.Rotation.D[0][2] = 0.0f;
    ZFlip.Rotation.D[1][0] = 0.0f;
    ZFlip.Rotation.D[1][1] = 1.0f;
    ZFlip.Rotation.D[1][2] = 0.0f;
    ZFlip.Rotation.D[2][0] = 0.0f;
    ZFlip.Rotation.D[2][1] = 0.0f;
    ZFlip.Rotation.D[2][2] = -1.0f;
    ZFlip.Translation.X = 0.0f;
    ZFlip.Translation.Y = 0.0f;
    ZFlip.Translation.Z = 0.0f;
    FTransform Product;
    Rp8MathEngine()->MulTransform(Product, ZFlip, *(const FTransform*)(In12));
    std::memcpy(Out12, &Product, sizeof(Product));
}

// formerly StoreMatrix16
void FRp8TransformCache::StoreTransposed(const float* M16)
{
    Values[0] = M16[0];
    Values[4] = M16[1];
    Values[8] = M16[2];
    Values[12] = M16[3];
    Values[1] = M16[4];
    Values[5] = M16[5];
    Values[9] = M16[6];
    Values[13] = M16[7];
    Values[2] = M16[8];
    Values[6] = M16[9];
    Values[10] = M16[10];
    Values[14] = M16[11];
    Values[3] = M16[12];
    Values[7] = M16[13];
    Values[11] = M16[14];
    Values[15] = M16[15];
    bCommitted = 0;
}

// formerly LoadMatrix16
void FRp8TransformCache::LoadTransposed(float* Out16) const
{
    Out16[0] = Values[0];
    Out16[1] = Values[4];
    Out16[2] = Values[8];
    Out16[3] = Values[12];
    Out16[4] = Values[1];
    Out16[5] = Values[5];
    Out16[6] = Values[9];
    Out16[7] = Values[13];
    Out16[8] = Values[2];
    Out16[9] = Values[6];
    Out16[10] = Values[10];
    Out16[11] = Values[14];
    Out16[12] = Values[3];
    Out16[13] = Values[7];
    Out16[14] = Values[11];
    Out16[15] = Values[15];
}

// Apply View, World and Projection in order; keep each flip selector independent.
void FDirectX8::ApplyIdentityTransforms()
{
    const float Identity12[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                  0.0f};
    const bool bViewspaceLightsOff =
        *(const unsigned long*)(ObjectBase() + kOffViewspaceLights) == 0;
    struct FIdentityStep
    {
        FRp8TransformCache* Cache;
        bool bFlip;
        D3DTRANSFORMSTATETYPE Type;
    };
    const FIdentityStep Steps[3] = {
        {&Transforms_.View, bViewspaceLightsOff, D3DTS_VIEW},
        {&Transforms_.World, !bViewspaceLightsOff, D3DTS_WORLD},
        {&Transforms_.Projection, false, D3DTS_PROJECTION},
    };
    for (int Index = 0; Index < 3; ++Index)
    {
        FRp8TransformCache& Block = *Steps[Index].Cache;
        if (Steps[Index].bFlip)
        {
            float Flipped[12];
            MulZFlip12(Flipped, Identity12);
            Block.StoreAffine(Flipped);
        }
        else
            Block.StoreAffine(Identity12);
        Block.bCommitted = 0;
        Block.Commit(Device_, Steps[Index].Type);
        Block.bApplyMarker = 1;
    }
}

void FDirectX8::ApplyDeviceViewport()
{
    // Successful initialization leaves both cache validity bytes clear.
    bDepthRangeValid_ = 0;
    bViewportRectValid_ = 0;
    ViewportCache_.X = 0;
    ViewportCache_.Y = 0;
    ViewportCache_.Width = (DWORD)(ModeBlob_[2]);
    ViewportCache_.Height = (DWORD)(ModeBlob_[3]);
    HRESULT Hr = Device_->SetViewport(&ViewportCache_);
    if (Hr >= 0)
        bViewportRectValid_ = 0;
    if (Hr < 0)
        FdumpHr("set_viewport: %s", Hr, 2340);

    ViewportCache_.MinZ = 0.f;
    ViewportCache_.MaxZ = 1.f;
    Hr = Device_->SetViewport(&ViewportCache_);
    if (Hr >= 0)
        bDepthRangeValid_ = 0;
}

void FDirectX8::ApplyDefaultRenderStates()
{
    // Apply defaults immediately, bypassing the later per-state enable gate.
    RenderStates_.ResetGeneration();
    FRp8PropertyMap& States = RenderStateMap_.Value;
    for (FRp8PropertyMap::iterator It = States.begin(); It != States.end(); ++It)
    {
        if (It->second.bEnabled != 0)
            RenderStates_.Apply(Device_, (unsigned int)It->second.Id,
                         It->second.Value);
    }
}

// Apply the per-stage defaults, including the final disabled stage.
void FDirectX8::ApplyDefaultTextureStages()
{
    const float Identity12[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                  0.0f, 0.0f, 0.0f};
    const unsigned long Stages = StageCount();
    for (unsigned long Stage = 0; Stage <= Stages; ++Stage)
    {
        FRp8TextureBinding& Cache = DeviceStates_.TextureBindings[Stage];
        Cache.bValid = 0;
        Cache.Handle = nullptr;
        const HRESULT TextureHr = Device_->SetTexture(Stage, nullptr);
        if (TextureHr >= 0)
        {
            Cache.Handle = nullptr;
            Cache.bValid = 1;
        }

        FRp8TransformCache& Transform = DeviceStates_.TextureTransforms[Stage];
        Transform.StoreAffine(Identity12);
        Transform.Commit(Device_,
                           (D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + Stage));
        Transform.bApplyMarker = 1;

        FRp8PropertyMap& States = StageStateMaps_[Stage].Value;
        for (FRp8PropertyMap::iterator It = States.begin(); It != States.end(); ++It)
        {
            const FRp8Property& State = It->second;
            if (State.bEnabled == 0)
                continue;
            FRp8TextureStageState& Tss = DeviceStates_.TextureStates[Stage][State.Id];
            Tss.bValid = 0;
            const HRESULT Hr = Device_->SetTextureStageState(
                Stage, (D3DTEXTURESTAGESTATETYPE)State.Id, State.Default);
            Tss.bValid = Hr >= 0 ? 1 : 0;
            if (Tss.bValid != 0)
                Tss.Value = State.Default;
        }
    }
}

// The tail of create_buffers FUN_06d08811 (06d095d7..06d098d4), in retail order.
void FDirectX8::ApplyPostDeviceHelpers()
{
    ApplyDefaultRenderStates();  // FUN_06d0376f
    ApplyDefaultTextureStages(); // FUN_06d038ba
    ApplyIdentityTransforms();   // FUN_06d03c94
    PopulateFormatCatalog();     // FUN_06d047df
    ApplyDeviceViewport();       // 06d095f7..06d09877
    // 06d09877: FUN_06d2ce6a(this+0x21f4): the StateCache light records are re-queued for the
    // new device (data resent, requested-on lights re-enabled at the next draw).
    LightManager_.Reset();
    // 06d09885: stream cache +0x138 = {0, 0, 0, -1, 0}.
    StreamCache_.Reset();
    // (Removed: a port-only SetPipelineState(8, 0xff101820) "dark space sky" clear colour,
    // SetPipelineState(9, 1.0f) and SetPerspective(60, aspect, 1, 50000) -- none of them is in
    // FUN_06d08811; CLEAR_COLOR / CLEAR_ZDEPTH come from [RenderPipeline] and the callers.)
}

// Remove the handle from all stage bindings before releasing or updating its texture.
void FDirectX8::UnbindTextureHandle(void* Handle)
{
    for (unsigned int Stage = 0; Stage < FRp8DeviceStateCaches::StageCount; ++Stage)
    {
        FRp8TextureBinding& Cache = DeviceStates_.TextureBindings[Stage];
        if (Cache.bValid == 0)
        {
            Cache.bValid = 1;
            Cache.Handle = nullptr;
        }
        if (Cache.Handle == Handle)
        {
            if (Cache.bValid != 0 && Cache.Handle == nullptr)
                continue;
            const HRESULT Hr = Device_->SetTexture(Stage, nullptr);
            if (Hr >= 0)
            {
                Cache.Handle = nullptr;
                Cache.bValid = 1;
            }
        }
    }
}

unsigned int FDirectX8::PfIndex(unsigned int Format)
{
    for (unsigned int Index = 0; Index < 22; ++Index)
    {
        if (kPfToD3d[Index] == Format)
            return Index;
    }
    return 0;
}

unsigned int FDirectX8::PfToD3d(unsigned int Format)
{
    // FUN_06d5cbb0: TEST EAX,EAX / JL warn; CMP EAX,0x16 / JGE warn; else DAT_06d6ffa8[pf].
    // (A signed negative pf is >= 22 as unsigned, so one unsigned compare covers both.) An
    // earlier port tried a value lookup (FUN_06d5cc30) first; retail has no such step, and
    // every caller in the tree passes pfenum ids.
    if (Format < 22)
        return kPfToD3d[Format];
    {
        // 06d5cbdd: sprintf("pf_to_d3d: unknown pfenum passed (%x)", id) -> RPUL\Pixel.cpp:82.
        char Text[0x2000];
        _snprintf(Text, sizeof(Text), "pf_to_d3d: unknown pfenum passed (%x)", Format);
        Text[sizeof(Text) - 1] = '\0';
        Rp8Fdump(kFdumpWarning, 0x52, "WARNING", Text, kSrcPixel);
    }
    return kPfToD3d[0];
}

unsigned int FDirectX8::PfBytesPerPixel(unsigned int Format)
{
    unsigned int Index = PfIndex(Format);
    if (Index == 0 && Format < 22)
        Index = Format;
    if (Index >= 22)
        return 0;
    return (kPfBpp[Index] + 7) / 8;
}

unsigned int FDirectX8::FvfStride(unsigned int Fvf)
{
    unsigned int Base = 0;
    if ((Fvf & 0xC) == 0xC)
        Base = 0x1C;
    else if ((Fvf & 4) == 4)
        Base = 0x10;
    else if ((Fvf & 2) == 2)
        Base = 0xC;
    if ((Fvf & 0x10) != 0)
        Base += 0xC;
    if ((Fvf & 0x40) != 0)
        Base += 4;
    if ((Fvf & 0x80) != 0)
        Base += 4;
    const unsigned int TexCount = (Fvf & 0xF00) >> 8;
    for (unsigned int Index = 0; Index < TexCount; ++Index)
    {
        const unsigned int Size = (Fvf >> (Index * 2 + 0x10)) & 3;
        if (Size == 0)
            Base += 8;
        else if (Size == 1)
            Base += 0xC;
        else if (Size == 2)
            Base += 0x10;
        else
            Base += 4;
    }
    return Base;
}

// FUN_06d01c68 (IAggregateComponent slot 3). Returns 0 on every path.
GENRESULT DACOM_API FDirectX8::Initialize()
{
    // 06d01c78: no parser held -> DACOM_Acquire()->QueryInterface("1.11_IProfileParser",
    // &+0x20); the result is not checked.
    if (Parser_ == nullptr)
        DACOM_Acquire()->QueryInterface(Iids::ProfileParser, (void**)(&Parser_));
    // 06d01cda: DAT_06d70540 = FUN_06d5cd40() once.
    Rp8MathEngine();
    // 06d01ced..06d02032: the pipeline-state property map.
    FillPropertyMap(&PropertyMap_.Value, kPipelineProperties,
                    sizeof(kPipelineProperties) / sizeof(kPipelineProperties[0]), true);
    // 06d02037: FUN_06d19530, the ability map.
    FillPropertyMap(&AbilityMap_.Value, kAbilityProperties,
                    sizeof(kAbilityProperties) / sizeof(kAbilityProperties[0]), true);
    // 06d02049: FUN_06d176e0, the render-state map.
    FillPropertyMap(&RenderStateMap_.Value, kRenderStateProperties,
                    sizeof(kRenderStateProperties) / sizeof(kRenderStateProperties[0]), true);
    // 06d0205b: FUN_06d19c30(stage 0, 1), then 06d0206e..06d020a1 FUN_06d19c30(stage i, 0) for
    // i = 0..7 -- stage 0 is rebuilt with the flag clear, so every stage ends up COLOROP 1 /
    // ALPHAOP 1.
    FillStageStateMap(&StageStateMaps_[0].Value, true);
    for (unsigned int Stage = 0; Stage < 8; ++Stage)
        FillStageStateMap(&StageStateMaps_[Stage].Value, false);
    return GR_OK;
}

// FUN_06d05da0 (IRenderPipeline slot 3). No device is created here (create_buffers does).
GENRESULT DACOM_API FDirectX8::Startup(const char* SectionName)
{
    ICOManager* Dacom = DACOM_Acquire();
    D3d_ = Direct3DCreate8(0x78);
    if (D3d_ == nullptr)
    {
        // 06d05dd3: FUN_06d2b920 (vsprintf into a static buffer) of the text, WARNING 0x5ce.
        Rp8Fdump(kFdumpWarning, 0x5CE, "WARNING", "DX8RP: startup: couldnt create directx8 device");
        return GR_GENERIC;
    }
    if (SectionName == nullptr)
        SectionName = SectionName_;

    if (strcmp(SectionName, LastSection_) != 0)
    {
        char DeviceId[0x80];
        ReadProfileString(Dacom, Parser_, SectionName, "DeviceId",
                          "{00000000-0000-0000-0000-000000000000}", DeviceId);
        char DeviceType[0x80];
        ReadProfileString(Dacom, Parser_, SectionName, "DeviceType", "Hardware", DeviceType);
        unsigned long DeviceTypeFallback = 0;
        ReadProfileNumber(Dacom, Parser_, SectionName, "DeviceTypeFallback", 0,
                          &DeviceTypeFallback);
        strcpy(LastSection_, SectionName);
    }

    // 06d06595: MGSDB -> +0x1B0 (default DAT_06d70544, an empty string).
    ReadProfileString(Dacom, Parser_, SectionName, "MGSDB", "", MgsdbPath_);

    EnumAdapters();

    // 06d06ad0.
    AdapterIndex_ = 0;
    DeviceType_ = 1;
    CreateFlags_ = 0;
    // 06d06af7..06d07091 (FUN_06d16930 inlined, default 0).
    if (ReadProfileNumber(Dacom, Parser_, SectionName, "FPU_PRESERVE", 0, nullptr) != 0)
        CreateFlags_ |= 0x2;
    if (ReadProfileNumber(Dacom, Parser_, SectionName, "MULTITHREADED", 0, nullptr) != 0)
        CreateFlags_ |= 0x4;
    if (ReadProfileNumber(Dacom, Parser_, SectionName, "PUREDEVICE", 0, nullptr) != 0)
        CreateFlags_ |= 0x10;
    // 06d0735e: SOFTWARE -> 0x20, else MIXED -> 0x80, else 0x40 whatever HARDWARE_VERTEXPROCESSING
    // says (it is read, and both of its outcomes OR 0x40).
    if (ReadProfileNumber(Dacom, Parser_, SectionName, "SOFTWARE_VERTEXPROCESSING", 0, nullptr) != 0)
        CreateFlags_ |= 0x20;
    else if (ReadProfileNumber(Dacom, Parser_, SectionName, "MIXED_VERTEXPROCESSING", 0, nullptr) != 0)
        CreateFlags_ |= 0x80;
    else if (ReadProfileNumber(Dacom, Parser_, SectionName, "HARDWARE_VERTEXPROCESSING", 0, nullptr) !=
             0)
        CreateFlags_ |= 0x40;
    else
        CreateFlags_ |= 0x40;

    // 06d07457: +0x128 = 0, then bit0 LOCKABLE_BACKBUFFER (dacom.ini's commented-out
    // "LOCAKABLE_BACKBUFFER" is a typo retail never reads), bit1 USE_SYSLOCK, bit2 HANDLE_SWAPLOSS
    // (default 1).
    SwapFlags_ = 0;
    unsigned char* SwapByte = (unsigned char*)(&SwapFlags_);
    const unsigned char bLockable =
        ReadProfileNumber(Dacom, Parser_, SectionName, "LOCKABLE_BACKBUFFER", 0, nullptr) != 0 ? 1 : 0;
    *SwapByte = (unsigned char)((*SwapByte & 0xFE) | bLockable);
    const unsigned char bSysLock =
        ReadProfileNumber(Dacom, Parser_, SectionName, "USE_SYSLOCK", 0, nullptr) != 0 ? 1 : 0;
    *SwapByte = (unsigned char)((*SwapByte & 0xFD) | (bSysLock << 1));
    const unsigned char bSwapLoss =
        ReadProfileNumber(Dacom, Parser_, SectionName, "HANDLE_SWAPLOSS", 1, nullptr) != 0 ? 1 : 0;
    *SwapByte = (unsigned char)((*SwapByte & 0xFB) | (bSwapLoss << 2));

    // 06d07577: every enabled pipeline-state property is read into its node and stored as
    // {Value, 1, 1} at +0x354 + Id*0xC (the slot is first reset to {0, 1, 0}).
    FRp8PropertyMap& Properties = PropertyMap_.Value;
    for (FRp8PropertyMap::iterator It = Properties.begin(); It != Properties.end(); ++It)
    {
        FRp8Property& Property = It->second;
        if (Property.bEnabled == 0)
            continue;
        ReadProfileNumber(Dacom, Parser_, SectionName, Property.Name, Property.Default,
                          &Property.Value);
        unsigned long* Slot = (unsigned long*)(ObjectBase() + 0x354 + Property.Id * 0xC);
        Slot[1] = 1;
        Slot[2] = 0;
        Slot[0] = 0;
        if (Slot[1] != 0)
        {
            Slot[2] = 1;
            Slot[0] = Property.Value;
        }
    }

    // 06d07766: an ability key present in the last-read section (+0xA4) enables its node and
    // is read into it; FUN_06d020ab applies it.
    FRp8PropertyMap& Abilities = AbilityMap_.Value;
    for (FRp8PropertyMap::iterator It = Abilities.begin(); It != Abilities.end(); ++It)
    {
        FRp8Property& Ability = It->second;
        int Found = -1;
        IProfileParser* Parser = nullptr;
        if (AcquireStartupParser(Dacom, Parser_, &Parser))
        {
            void* Handle = Parser->OpenSection(LastSection_, 3);
            if (Handle != nullptr)
            {
                if (Parser->GetString(Handle, Ability.Name, G_ProfileProbeBuf, 0x400) != 0)
                    Found = 0;
                Parser->CloseSection(Handle);
            }
        }
        ReleaseStartupParser(&Parser);
        if (Found >= 0)
        {
            Ability.bEnabled = 1;
            ReadProfileNumber(Dacom, Parser_, LastSection_, Ability.Name, Ability.Default,
                              &Ability.Value);
        }
    }

    // 06d07971: every enabled render-state node is read from the INI (FUN_06d0376f applies it).
    FRp8PropertyMap& RenderStates = RenderStateMap_.Value;
    for (FRp8PropertyMap::iterator It = RenderStates.begin(); It != RenderStates.end(); ++It)
    {
        FRp8Property& State = It->second;
        if (State.bEnabled != 0)
            ReadProfileNumber(Dacom, Parser_, SectionName, State.Name, State.Default, &State.Value);
    }

    // 06d07a48: FUN_06d2b6e0.
    VidMem_ = QueryVidMem();
    // 06d07a56: GetDeviceCaps(+0x1A8, +0x1AC) -- the result is kept in a local nobody reads --
    // then FUN_06d020ab.
    D3DCAPS8 Caps;
    D3d_->GetDeviceCaps(AdapterIndex_, (D3DDEVTYPE)(DeviceType_), &Caps);
    ApplyDeviceCaps(&Caps);
    return GR_OK;
}

GENRESULT DACOM_API FDirectX8::ShutdownPipeline()
{
    // FUN_06d07aa8: destroy_buffers (vtbl+0x38), free every adapter's mode array, empty the
    // adapter vector (06d07b1a..06d08147: the inlined resize(0) -- its grow branch is dead),
    // release the device and IDirect3D8. +0x1A8 is left as it is.
    DestroyGpuResources();
    std::vector<FRp8Adapter>& Adapters = Adapters_.Value;
    for (std::vector<FRp8Adapter>::iterator It = Adapters.begin(); It != Adapters.end(); ++It)
    {
        delete[] It->Modes;
        It->Modes = nullptr;
    }
    Adapters.resize(0);
    if (Device_ != nullptr)
    {
        Device_->Release();
        Device_ = nullptr;
    }
    if (D3d_ != nullptr)
    {
        D3d_->Release();
        D3d_ = nullptr;
    }
    return GR_OK;
}

// FUN_06d081c2 (slot 5): no startup check, no id range check. Slot = +0x354 + Id*0xC; unless
// it already holds this value (Slot[2] != 0 && Slot[0] == Value), and only while Slot[1] != 0:
// Slot[2] = 1, Slot[0] = Value. Always 0.
GENRESULT DACOM_API FDirectX8::SetPipelineState(int Id, int Value)
{
    int* Slot = (int*)(ObjectBase() + 0x354 + Id * 0xC);
    const bool bChanged = !(Slot[2] != 0 && Slot[0] == Value);
    if (bChanged && Slot[1] != 0)
    {
        Slot[2] = 1;
        Slot[0] = Value;
    }
    return GR_OK;
}

GENRESULT DACOM_API FDirectX8::GetPipelineState(int Id, int* Out)
{
    // FUN_06d0823e: startup check (line 0x6d9); 0 <= Id <= 0xE (JL / JG signed) ->
    // *Out = [this+0x354+Id*0xC]; any other Id leaves *Out alone. Returns 0 either way.
    const GENRESULT Ready =
        NeedStartup("get_pipeline_state() called outside of startup/shutdown", 1753);
    if (Ready != GR_OK)
        return Ready;
    if (Id >= 0 && Id <= 0xE)
    {
        int* Slot =
            (int*)((unsigned char*)(this) + 0x354 + Id * 0xC);
        *Out = Slot[0];
    }
    return GR_OK;
}

GENRESULT DACOM_API FDirectX8::GetDeviceInfo(void* Out520)
{
    // FUN_06d082d1: startup check (line 0x6eb); Count = begin ? (end-begin)/0x520 : 0;
    // AdapterIndex_ < Count (JNC) -> REP MOVSD 0x148 dwords, 0; else -1.
    const GENRESULT Ready =
        NeedStartup("get_device_info() called outside of startup/shutdown", 1771);
    if (Ready != GR_OK)
        return Ready;
    const std::vector<FRp8Adapter>& Adapters = Adapters_.Value;
    const unsigned long Count = (unsigned long)(Adapters.size());
    if (AdapterIndex_ >= Count)
        return GR_GENERIC;
    memcpy(Out520, &Adapters[AdapterIndex_], 0x520);
    return GR_OK;
}

GENRESULT DACOM_API FDirectX8::QueryDeviceAbility(unsigned int Index, unsigned long* Out)
{
    // FUN_06d083d2: startup check (line 0x6fc); Index > 0x14 (JA) or Out == 0 -> -2.
    const GENRESULT Ready =
        NeedStartup("query_device_ability() called outside of startup/shutdown", 1788);
    if (Ready != GR_OK)
        return Ready;
    if (Index > 0x14 || Out == nullptr)
        return GR_INVALID_PARMS;
    *Out = Ability_[Index];
    return GR_OK;
}

GENRESULT DACOM_API FDirectX8::GetDisplayModeCount(unsigned int* Out)
{
    // FUN_06d08461: startup check (line 0x716); *Out = D3d->GetAdapterModeCount(AdapterIndex_)
    // (IDirect3D8 +0x18), 0.
    const GENRESULT Ready =
        NeedStartup("get_display_mode() called outside of startup/shutdown", 1814);
    if (Ready != GR_OK)
        return Ready;
    *Out = D3d_->GetAdapterModeCount(AdapterIndex_);
    return GR_OK;
}

GENRESULT DACOM_API FDirectX8::GetDisplayModeEnum(unsigned int Index, void* Out)
{
    const GENRESULT Ready =
        NeedStartup("get_display_mode() called outside of startup/shutdown", 1825);
    if (Ready != GR_OK)
        return Ready;
    D3DDISPLAYMODE Display;
    const HRESULT Hr = D3d_->EnumAdapterModes(AdapterIndex_, Index, &Display);
    if (Hr < 0)
    {
        FdumpHr("get_display_mode: %s", Hr, 1829);
        return (GENRESULT)(Hr);
    }
    unsigned long* Record = (unsigned long*)(Out);
    Record[0] = Index;
    Record[1] = Display.Width;
    Record[2] = Display.Height;
    Record[3] = Display.RefreshRate;
    Record[4] = PfIndex((unsigned int)(Display.Format));
    if (Ability_[0x11] != 0 && Record[1] >= Ability_[0x11])
        return (GENRESULT)(0xFFFFFFF8);
    return GR_OK;
}

GENRESULT DACOM_API FDirectX8::SelectMode(int* Mode, unsigned int* OutIndex)
{
    const GENRESULT Ready = NeedStartup("select_mode() called outside of startup/shutdown", 1906);
    if (Ready != GR_OK)
        return Ready;
    FRp8Mode* Desc = (FRp8Mode*)(Mode);
    const UINT Count = D3d_->GetAdapterModeCount(AdapterIndex_);
    int Found = -1;
    UINT Index = 0;
    for (; Index < Count; ++Index)
    {
        D3DDISPLAYMODE Display;
        D3d_->EnumAdapterModes(AdapterIndex_, Index, &Display);
        const unsigned int PfId = PfIndex((unsigned int)(Display.Format));
        if ((int)(Display.Width) != Desc->Width ||
            (int)(Display.Height) != Desc->Height)
            continue;
        if ((int)(RetailPfBppBits(PfId)) != Desc->Format)
            continue;
        if (Ability_[0x11] != 0 && Display.Width >= Ability_[0x11])
            continue;
        Found = (int)(Index);
        break;
    }
    if (Found < 0)
        return GR_GENERIC;
    *OutIndex = (unsigned int)(Found);
    return GR_OK;
}

GENRESULT DACOM_API FDirectX8::CreateBuffers(HWND Hwnd, int* Mode, int* OutMode)
{
    // FUN_06d08811 RET 0x10, traced line by line (06d08811..06d098dc).
    const GENRESULT Ready =
        NeedStartup("create_buffers() called outside of startup/shutdown", 1991);
    if (Ready != GR_OK)
        return Ready;
    FRp8Mode* Desc = (FRp8Mode*)(Mode);
    // 06d08890: a live device whose TestCooperativeLevel (+0xc) is D3DERR_DEVICELOST (raw bytes
    // 81 7d 88 68 08 76 88) -> OR EAX,-1.
    if (Device_ != nullptr &&
        Device_->TestCooperativeLevel() == (HRESULT)(0x88760868))
        return GR_GENERIC;
    // 06d088c8: this->vtbl[0x38] = destroy_buffers.
    DestroyGpuResources();
    // 06d088d4: back-buffer format (local_74) = 0, mode index (local_58) = -1;
    // GetAdapterDisplayMode (IDirect3D8 +0x20) failing -> 0x7e2, return hr.
    D3DFORMAT BackBuffer = D3DFMT_UNKNOWN;
    int ModeIndex = -1;
    D3DDISPLAYMODE Display;
    HRESULT Hr = D3d_->GetAdapterDisplayMode(AdapterIndex_, &Display);
    if (Hr < 0)
    {
        FdumpHr("create_buffers_select_mode: %s", Hr, 2018);
        return (GENRESULT)(Hr);
    }
    // 06d089a4: full screen (mode byte +0x25) picks an adapter mode: mode[0] < 0 -> a copy of
    // the descriptor (bits -1 -> the display format's bits) through this->vtbl[0x2c]
    // (select_mode), then EnumAdapterModes (+0x1c) of the found index, whose format becomes
    // the back buffer's; mode[0] >= 0 -> EnumAdapterModes of that index only.
    if (Desc->Fullscreen != 0)
    {
        ModeIndex = Desc->Field00;
        if (ModeIndex < 0)
        {
            FRp8Mode Wanted = *Desc;
            if (Desc->Format == -1)
                Wanted.Format = (int)(RetailPfBppBits(PfIndex(
                    (unsigned int)(Display.Format))));
            Hr = (HRESULT)(SelectMode((int*)(&Wanted),
                                                 (unsigned int*)(&ModeIndex)));
            if (Hr < 0)
            {
                FdumpHr("create_buffers_select_mode: %s", Hr, 2046);
                return (GENRESULT)(Hr);
            }
            Hr = D3d_->EnumAdapterModes(AdapterIndex_, (UINT)(ModeIndex), &Display);
            if (Hr < 0)
            {
                FdumpHr("create_buffers_select_mode: %s", Hr, 2050);
                return (GENRESULT)(Hr);
            }
            BackBuffer = Display.Format;
        }
        else
        {
            Hr = D3d_->EnumAdapterModes(AdapterIndex_, (UINT)(ModeIndex), &Display);
            if (Hr < 0)
            {
                FdumpHr("create_buffers_select_mode: %s", Hr, 2062);
                return (GENRESULT)(Hr);
            }
        }
    }
    // 06d08c81: display pfenum (local_68). No back-buffer format yet: bits -1 or equal to the
    // display's -> the display format; else the first of pfenum 0..8 with those bits whose
    // D3DFORMAT passes CheckDeviceFormat (+0x28)(adapter, type, display fmt,
    // D3DUSAGE_RENDERTARGET 1, D3DRTYPE_SURFACE 1, fmt). Still none -> -1.
    const unsigned int DisplayPf = PfIndex((unsigned int)(Display.Format));
    if (BackBuffer == D3DFMT_UNKNOWN)
    {
        if (Desc->Format == -1 || Desc->Format == (int)(RetailPfBppBits(DisplayPf)))
            BackBuffer = Display.Format;
        else
        {
            for (int Pf = 0; Pf < 9; ++Pf)
            {
                if ((int)(RetailPfBppBits((unsigned int)(Pf))) !=
                    Desc->Format)
                    continue;
                const D3DFORMAT Candidate =
                    (D3DFORMAT)(PfToD3d((unsigned int)(Pf)));
                if (D3d_->CheckDeviceFormat(AdapterIndex_, (D3DDEVTYPE)(DeviceType_),
                                            Display.Format, D3DUSAGE_RENDERTARGET,
                                            D3DRTYPE_SURFACE, Candidate) >= 0)
                {
                    BackBuffer = Candidate;
                    break;
                }
            }
        }
    }
    if (BackBuffer == D3DFMT_UNKNOWN)
        return GR_GENERIC;
    // 06d08daa: depth bits wanted = mode[5] (-1 -> the back buffer's colour bits), stencil =
    // mode[6] as given; FUN_06d159ff picks the depth format; none -> -1.
    int WantedZ = Desc->WantedZ;
    if (WantedZ == -1)
        WantedZ = (int)(RetailPfBppBits(PfIndex((unsigned int)(BackBuffer))));
    int WantedS = Desc->WantedStencil;
    const D3DFORMAT Depth = PickDepthFormat(Display.Format, BackBuffer, &WantedZ, &WantedS);
    if (Depth == D3DFMT_UNKNOWN)
        return GR_GENERIC;

    BOOL Windowed = TRUE;
    UINT BackBufferCount = 1;
    UINT PresentInterval = 0;
    DWORD SwapEffect = 0x7FFFFFFF;
    if (Desc->Fullscreen == 0)
    {
        if (Desc->SwapExtra != 0 && BltNotFlip_ == 0 && Desc->Interval != 0)
            SwapEffect = D3DSWAPEFFECT_COPY_VSYNC;
        else
            SwapEffect = D3DSWAPEFFECT_COPY;
    }
    else
    {
        Windowed = FALSE;
        if (Desc->SwapExtra != 0 && BltNotFlip_ == 0)
        {
            BackBufferCount = (UINT)(Desc->BufferCount - 1);
            SwapEffect = D3DSWAPEFFECT_FLIP;
        }
        else if (Desc->Interval == 0)
            SwapEffect = D3DSWAPEFFECT_COPY;
        else
            SwapEffect = D3DSWAPEFFECT_COPY_VSYNC;
        switch ((unsigned int)(Desc->Interval))
        {
        case 0:
            PresentInterval = 0x80000000u;
            break;
        case 1:
            PresentInterval = 1;
            break;
        case 2:
            PresentInterval = 2;
            break;
        case 3:
            PresentInterval = 4;
            break;
        case 4:
            PresentInterval = 8;
            break;
        default:
            break;
        }
    }
    // 06d08f3e: the D3DPRESENT_PARAMETERS on the stack (EBP-0x40).
    D3DPRESENT_PARAMETERS Present;
    Present.BackBufferWidth = (UINT)(Desc->Width);
    Present.BackBufferHeight = (UINT)(Desc->Height);
    Present.BackBufferFormat = BackBuffer;
    Present.BackBufferCount = BackBufferCount;
    Present.MultiSampleType = D3DMULTISAMPLE_NONE;
    Present.SwapEffect = (D3DSWAPEFFECT)(SwapEffect);
    Present.hDeviceWindow = Hwnd;
    Present.Windowed = Windowed;
    Present.EnableAutoDepthStencil = TRUE;
    Present.AutoDepthStencilFormat = Depth;
    Present.Flags = (SwapFlags_ & 1) != 0 ? 1 : 0;
    Present.FullScreen_RefreshRateInHz = 0;
    Present.FullScreen_PresentationInterval = PresentInterval;
    if (Device_ == nullptr)
    {
        // 06d08fb8: focus = GetAncestor(hwnd, GA_ROOT); CreateDevice (+0x3c)(adapter, type,
        // focus, +0x124, &pp, &+0x130); failure while a HARDWARE/MIXED bit (0xc0) is set ->
        // swap it for SOFTWARE_VERTEXPROCESSING (0x20) and retry (JMP 06d08fca).
        HWND Focus = GetAncestor(Hwnd, GA_ROOT);
        for (;;)
        {
            Hr = D3d_->CreateDevice(AdapterIndex_, (D3DDEVTYPE)(DeviceType_), Focus,
                                    CreateFlags_, &Present, &Device_);
            if (Hr >= 0 || (CreateFlags_ & 0xC0) == 0)
                break;
            CreateFlags_ = (CreateFlags_ & 0xFFFFFF3Ful) | 0x20ul;
        }
        if (Hr < 0)
        {
            FdumpHr("create_device: %s", Hr, 2273);
            return (GENRESULT)(Hr);
        }
        // 06d090f9: GetDeviceCaps (+0x1c) failing -> 'create_device_caps: %s' 0x8e9; else
        // FUN_06d020ab(caps) (abilities + MGSDB flags).
        D3DCAPS8 Caps;
        Hr = Device_->GetDeviceCaps(&Caps);
        if (Hr < 0)
        {
            FdumpHr("create_device_caps: %s", Hr, 2281);
            return (GENRESULT)(Hr);
        }
        ApplyDeviceCaps(&Caps);
    }
    else
    {
        // 06d091c7: Reset (+0x38)(&pp); failure -> 'create_device: %s' 0x8f4, return hr.
        Hr = Device_->Reset(&Present);
        if (Hr < 0)
        {
            FdumpHr("create_device: %s", Hr, 2292);
            return (GENRESULT)(Hr);
        }
    }

    FRp8Ib* ScratchWrapper = (FRp8Ib*)(ObjectBase() + 0x14C);
    ReleaseIbGpu(ScratchWrapper);
    ScratchWrapper->Capacity = 0x4000;
    ScratchWrapper->Cursor = 0;
    Hr = Device_->CreateIndexBuffer(0x4000 * 2, ScratchWrapper->Flags, D3DFMT_INDEX16,
                                    D3DPOOL_DEFAULT, &ScratchWrapper->Gpu);
    if (Hr < 0)
        FdumpIbCreate(Hr, 0x4000 * 2);
    if (Hr < 0)
    {
        FdumpHr("create_buffers_index_create: %s", Hr, 2299);
        return (GENRESULT)(Hr);
    }

    DeviceReady_ |= 1;
    Field230_ = DisplayPf;
    BbPfIndex_ = PfIndex((unsigned int)(Present.BackBufferFormat));
    Hwnd_ = Hwnd;
    ModeBlob_[0] = ModeIndex;
    ModeBlob_[2] = (int)(Present.BackBufferWidth);
    ModeBlob_[3] = (int)(Present.BackBufferHeight);
    ModeBlob_[4] = (int)(RetailPfBppBits(BbPfIndex_));
    ModeBlob_[5] = DepthFormatZBits(Present.AutoDepthStencilFormat);
    ModeBlob_[6] = DepthFormatStencilBits(Present.AutoDepthStencilFormat);
    ModeBlob_[7] = Desc->Interval;
    ModeBlob_[8] = (int)(Present.BackBufferCount + 1);
    unsigned char* const ModeFlags = (unsigned char*)(&ModeBlob_[9]);
    ModeFlags[0] = (Present.SwapEffect & 2) != 0 ? 1 : 0;
    ModeFlags[1] = Present.Windowed == 0 ? 1 : 0;
    // 06d095be: out mode (if given) = the 10 dwords at +0x238.
    if (OutMode != nullptr)
        memcpy(OutMode, ModeBlob_, sizeof(ModeBlob_));
    // 06d095d7..06d098d4: FUN_06d0376f, FUN_06d038ba, FUN_06d03c94, FUN_06d047df, viewport,
    // light cache, stream cache.
    ApplyPostDeviceHelpers();
    return (GENRESULT)(Hr);
}

// FUN_06d09982 RET 0x4 (destroy_buffers, slot 14), traced line by line. The device and the
// IDirect3D8 stay alive; only the D3DPOOL_DEFAULT resources go.
GENRESULT DACOM_API FDirectX8::DestroyGpuResources()
{
    unsigned char* Base = ObjectBase();
    // 06d09988: stream cache +0x138 = {0, 0, 0, -1, 0}.
    StreamCache_.Reset();
    // 06d099c4: every node of the VB list (+0x22E4, by +0x14) loses its buffer.
    FRp8Vb* Vb = ((FRp8VbList*)(Base + kOffVbList))->First;
    while (Vb != nullptr)
    {
        Vb->ReleaseGpu();
        Vb = Vb->Next;
    }
    // 06d09a40: same for the IB list (+0x22F0, by +0xC).
    FRp8Ib* Ib = ((FRp8IbList*)(Base + kOffIbList))->First;
    while (Ib != nullptr)
    {
        ReleaseIbGpu(Ib);
        Ib = Ib->Next;
    }
    // 06d09abc: IVertexBufferManager (+4) vtbl[0x10] -- the VB pool.
    VbMgrReleaseAll();
    // 06d09acc: the inline scratch IB node at +0x14C (Gpu +0x150 = ScratchIb_).
    ReleaseIbGpu((FRp8Ib*)(Base + 0x14C));
    // 06d09b22: +0x274 = 0; byte +0x184 &= 0xfe; return 0.
    WindowOverride_ = nullptr;
    DeviceReady_ &= ~1ul;
    return GR_OK;
}

GENRESULT DACOM_API FDirectX8::ClearBuffers(unsigned long Flags, const void* Rects)
{
    const GENRESULT Ready = NeedBuffers(
        "clear_buffers() called outside of create_buffers/destroy_buffers", 2402);
    if (Ready != GR_OK)
        return Ready;
    unsigned long D3dFlags = 0;
    int ZBits = 0x3F800000;
    if ((Flags & 1) != 0)
        D3dFlags |= D3DCLEAR_TARGET;
    if ((Flags & 2) != 0)
    {
        D3dFlags |= D3DCLEAR_ZBUFFER;
        ZBits = PipelineStateValue(9);
    }
    if ((Flags & 4) != 0)
        D3dFlags |= D3DCLEAR_STENCIL;
    float Z;
    memcpy(&Z, &ZBits, sizeof(Z));
    const unsigned long Color = (unsigned long)(PipelineStateValue(8));
    const unsigned long Stencil = (unsigned long)(PipelineStateValue(0xA));
    const DWORD Count = (Rects != nullptr) ? 1 : 0;
    const HRESULT Hr = Device_->Clear(Count, (const D3DRECT*)(Rects), D3dFlags,
                                      Color, Z, Stencil);
    if (FAILED(Hr))
        FdumpHr("clear: %s", Hr, 2435);
    return (GENRESULT)(Hr);
}

GENRESULT DACOM_API FDirectX8::SwapBuffers()
{
    const GENRESULT Ready = NeedBuffers(
        "swap_buffers() called outside of create_buffers/destroy_buffers", 2442);
    if (Ready != GR_OK)
        return Ready;
    RECT SrcRect;
    RECT DestRect;
    const RECT* Src = nullptr;
    const RECT* Dest = nullptr;
    HWND PresentHwnd = Hwnd_;
    if (WindowOverride_ != nullptr)
    {
        unsigned int SrcW = (unsigned int)(ModeBlob_[2]);
        unsigned int SrcH = (unsigned int)(ModeBlob_[3]);
        if ((unsigned int)(WindowW_) < SrcW)
            SrcW = (unsigned int)(WindowW_);
        if ((unsigned int)(WindowH_) < SrcH)
            SrcH = (unsigned int)(WindowH_);
        SetRect(&SrcRect, 0, 0, (int)(SrcW), (int)(SrcH));
        SetRect(&DestRect, WindowX_, WindowY_, WindowX_ + WindowW_, WindowY_ + WindowH_);
        Src = &SrcRect;
        Dest = &DestRect;
        PresentHwnd = WindowOverride_;
    }
    // FUN_06d09d28: the no-override branch also SetRects two locals (0,0,+0x240,+0x244) but
    // passes NULL for both rect pointers; those locals are never read, so they are not kept.
    HRESULT Hr = Device_->Present(Src, Dest, PresentHwnd, nullptr);
    // 06d09f25..06d09f8a: hr == D3DERR_DEVICELOST (CMP 0x88760868, raw bytes 81 7d e4 68 08 76
    // 88) and HANDLE_SWAPLOSS (byte +0x128 >> 2 & 1) -> hr = TestCooperativeLevel (+0xC); still
    // 0x88760868 (same constant, bytes checked) -> hr = this->vtable[0x30](Hwnd_, +0x238, 0),
    // i.e. create_buffers with the saved mode blob. Returns hr of whichever call ran last.
    if (Hr == (HRESULT)(0x88760868) && ((SwapFlags_ >> 2) & 1) != 0)
    {
        Hr = Device_->TestCooperativeLevel();
        if (Hr == (HRESULT)(0x88760868))
            Hr = (HRESULT)(CreateBuffers(Hwnd_, ModeBlob_, nullptr));
    }
    return (GENRESULT)(Hr);
}

GENRESULT DACOM_API FDirectX8::BeginScene()
{
    const GENRESULT Ready = NeedBuffers(
        "begin_scene() called outside of create_buffers/destroy_buffers", 3524);
    if (Ready != GR_OK)
        return Ready;
    // FUN_06d0f786 RET 0x4: BeginScene (+0x88); hr < 0 -> 'begin_scene: %s' 0xdd8; return hr.
    const HRESULT Hr = Device_->BeginScene();
    if (Hr < 0)
        FdumpHr("begin_scene: %s", Hr, 3544);
    return (GENRESULT)(Hr);
}

GENRESULT DACOM_API FDirectX8::EndScene()
{
    const GENRESULT Ready = NeedBuffers(
        "end_scene() called outside of create_buffers/destroy_buffers", 3556);
    if (Ready != GR_OK)
        return Ready;
    // FUN_06d0f8b5 RET 0x4: EndScene (+0x8c); hr < 0 -> 'end_scene: %s' 0xde8; return hr.
    const HRESULT Hr = Device_->EndScene();
    if (Hr < 0)
        FdumpHr("end_scene: %s", Hr, 3560);
    return (GENRESULT)(Hr);
}

GENRESULT DACOM_API FDirectX8::DrawIndexedPrimitive(unsigned long Prim, unsigned int MinIndex,
                                                    unsigned int NumVertices,
                                                    unsigned int StartIndex, unsigned int Count)
{
    IDirect3DDevice8* Device = *(IDirect3DDevice8**)(
        (unsigned char*)((IRPDraw*)(this)) + 0x128);
    unsigned long* Ready = (unsigned long*)(
        (unsigned char*)((IRPDraw*)(this)) + 0x17C);
    // FUN_06d111e1: MOV CL,byte [this+0x17c] (IRPDraw-adjusted DeviceReady_); AND 1; the device
    // pointer is not looked at.
    if (((*Ready) & 1) == 0)
    {
        Rp8Fdump(kFdumpWarning, 3945, "WARNING",
                 "draw_indexed_primitive() called outside of create_buffers/destroy_buffers");
        return GR_GENERIC;
    }
    // 06d11259..06d11351: hr = E_FAIL; PrimCount = FUN_06d010cf(Prim, Count); PrimCount <= 0
    // (JLE, signed) -> return E_FAIL; FUN_06d04138 (this-8); Device->DrawIndexedPrimitive
    // (+0x11c)(Prim, MinIndex, NumVertices, StartIndex, PrimCount); hr < 0 ->
    // "draw_indexed_primitive: %s" (line 0xf79); return hr.
    HRESULT Hr = (HRESULT)(0x80004005);
    const unsigned int PrimCount = PrimCountFromVerts(Prim, Count);
    if ((int)(PrimCount) <= 0)
        return (GENRESULT)(Hr);
    unsigned char* Base = (unsigned char*)((IRPDraw*)(this)) - 8;
    FlushDrawState(Device);
    Hr = Device->DrawIndexedPrimitive((D3DPRIMITIVETYPE)(Prim), MinIndex, NumVertices,
                                      StartIndex, PrimCount);
    if (Hr < 0)
        FdumpHr("draw_indexed_primitive: %s", Hr, 3961);
    return (GENRESULT)(Hr);
}

GENRESULT DACOM_API FDirectX8::VbMgrInit(void* /*A*/, void* /*B*/, void* /*C*/, void* /*D*/)
{
    // FUN_06d11354 RET 0x14: four explicit dwords, none of them read, body is `return 0`.
    return GR_OK;
}

GENRESULT DACOM_API FDirectX8::GetModeBlob(void* OutAdapter, void* OutMode)
{
    // LAB_06d098f3 RET 0xC. FDUMP string is create_buffers() called outside…
    // *out = +0x1A8; REP MOVSD 10 dwords from +0x238. No C++ name in RP8.
    // Failure: OR EAX,-1. Success has no return statement: EAX still holds AdapterIndex_
    // (MOV EAX,[EDX+0x1a8] at 06d0995d) after the REP MOVSD, reproduced.
    const GENRESULT Ready =
        NeedStartup("create_buffers() called outside of startup/shutdown", 2357);
    if (Ready != GR_OK)
        return Ready;
    *(unsigned long*)(OutAdapter) = AdapterIndex_;
    memcpy(OutMode, ModeBlob_, sizeof(ModeBlob_));
    return (GENRESULT)(AdapterIndex_);
}
GENRESULT DACOM_API FDirectX8::LockBuffer(void* Out)
{
    // FUN_06d09f96 RET 0x8, traced line by line. Out = 5 dwords {w, h, pitch, bits, pfenum}.
    const GENRESULT Ready = NeedBuffers(
        "lock_buffer() called outside of create_buffers/destroy_buffers", 2478);
    if (Ready != GR_OK)
        return Ready;
    // 06d0a00e: byte [+0x128] & 1 (LOCKABLE_BACKBUFFER) == 0 -> FDUMP 0x9b2, OR EAX,-1.
    if ((SwapFlags_ & 1) == 0)
    {
        Rp8Fdump(kFdumpWarning, 2482, "WARNING",
                 "lock_buffer: lock called but [RenderPipeline] wasnt configured with "
                 "LOCAKABLE_BACKBUFFER");
        return GR_GENERIC;
    }
    // 06d0a079: [+0x134] != 0 -> FDUMP 0x9b8, OR EAX,-1.
    if (LockBufferSurf_ != nullptr)
    {
        Rp8Fdump(kFdumpWarning, 2488, "WARNING", "lock_buffer: buffer already locked");
        return GR_GENERIC;
    }

    // 06d0a0e7: Back = 0; GetBackBuffer(0, 0, &Back) (+0x40). hr < 0 -> FDUMP 0x9bf, release
    // Back if set, return hr.
    IDirect3DSurface8* Back = nullptr;
    HRESULT Hr = Device_->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &Back);
    if (Hr < 0)
    {
        FdumpHr("lock_buffer_get_back_buffer: %s", Hr, 2495);
        if (Back != nullptr)
        {
            Back->Release();
            Back = nullptr;
        }
        return (GENRESULT)(Hr);
    }

    // 06d0a1e8: no override -> SetRect(&Area, 0,0,+0x240,+0x244) and a NULL rect pointer;
    // override -> rect pointer = &Area, Area = (0,0,min(+0x26c,+0x240),min(+0x270,+0x244))
    // with unsigned compares (JNC). Width/height are taken from Area in both cases.
    RECT Area = {};
    const RECT* SrcRects = nullptr;
    unsigned int RectCount = 0;
    int Width = ModeBlob_[2];
    int Height = ModeBlob_[3];
    if (WindowOverride_ != nullptr)
    {
        if ((unsigned int)(WindowW_) < (unsigned int)(Width))
            Width = WindowW_;
        if ((unsigned int)(WindowH_) < (unsigned int)(Height))
            Height = WindowH_;
        SetRect(&Area, 0, 0, Width, Height);
        SrcRects = &Area;
        RectCount = 1;
    }
    else
        SetRect(&Area, 0, 0, Width, Height);
    Width = Area.right - Area.left;
    Height = Area.bottom - Area.top;

    // 06d0a2f2: fmt = FUN_06d5cbb0(+0x234).
    const unsigned int D3dFormat = PfToD3d(BbPfIndex_);
    // 06d0a30f: release a surface still at +0x134 (already excluded above; kept as retail).
    if (LockBufferSurf_ != nullptr)
    {
        LockBufferSurf_->Release();
        LockBufferSurf_ = nullptr;
    }
    // 06d0a373: CreateImageSurface(w, h, fmt, &[+0x134]) (+0x6c). hr < 0 -> FDUMP 0x9ca,
    // release Back, return hr (the +0x134 slot is not touched).
    Hr = Device_->CreateImageSurface((UINT)(Width), (UINT)(Height),
                                     (D3DFORMAT)(D3dFormat), &LockBufferSurf_);
    if (Hr < 0)
    {
        FdumpHr("lock_buffer_create_copy: %s", Hr, 2506);
        if (Back != nullptr)
        {
            Back->Release();
            Back = nullptr;
        }
        return (GENRESULT)(Hr);
    }

    // 06d0a475: CopyRects(Back, rect, rect != 0, [+0x134], NULL) (+0x70); hr < 0 -> FDUMP 0x9cd.
    Hr = Device_->CopyRects(Back, SrcRects, RectCount, LockBufferSurf_, nullptr);
    if (Hr < 0)
        FdumpHr("lock_buffer_copy: %s", Hr, 2509);

    if (Hr >= 0)
    {
        // 06d0a515: NEG/SBB/AND 0xfffff800/ADD 0x800 -> USE_SYSLOCK (bit1) ? 0 : 0x800
        // (D3DLOCK_NOSYSLOCK); OR 0x10 (D3DLOCK_READONLY). LockRect(&Locked, NULL, flags) (+0x24).
        unsigned long LockFlags = ((SwapFlags_ >> 1) & 1) != 0 ? 0ul : 0x800ul;
        LockFlags |= 0x10;
        D3DLOCKED_RECT Locked;
        Hr = LockBufferSurf_->LockRect(&Locked, nullptr, LockFlags);
        if (Hr < 0)
            FdumpHr("lock_buffer_lock_rect: %s", Hr, 2519);
        else
        {
            // 06d0a611: Out+8 pitch, +0xc bits, +0x10 FUN_06d5cc30(fmt), +0 w, +4 h.
            unsigned long* OutDwords = (unsigned long*)(Out);
            OutDwords[2] = (unsigned long)(Locked.Pitch);
            OutDwords[3] = (unsigned long)(Locked.pBits);
            OutDwords[4] = PfIndex(D3dFormat);
            OutDwords[0] = (unsigned long)(Width);
            OutDwords[1] = (unsigned long)(Height);
        }
    }

    // 06d0a652: hr < 0 -> release and clear +0x134. Then release Back; return hr.
    if (Hr < 0 && LockBufferSurf_ != nullptr)
    {
        LockBufferSurf_->Release();
        LockBufferSurf_ = nullptr;
    }
    if (Back != nullptr)
    {
        Back->Release();
        Back = nullptr;
    }
    return (GENRESULT)(Hr);
}
GENRESULT DACOM_API FDirectX8::UnlockBuffer()
{
    // FUN_06d0a6c1 RET 0x4: ready bit; [+0x134] == 0 -> FDUMP 0x9f1, -1; else UnlockRect
    // (+0x28), Release, clear +0x134, return 0.
    const GENRESULT Ready = NeedBuffers(
        "unlock_buffer() called outside of create_buffers/destroy_buffers", 2541);
    if (Ready != GR_OK)
        return Ready;
    if (LockBufferSurf_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 2545, "WARNING", "unlock_buffer: buffer not locked");
        return GR_GENERIC;
    }
    LockBufferSurf_->UnlockRect();
    LockBufferSurf_->Release();
    LockBufferSurf_ = nullptr;
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetBufferInterface(void* /*Out*/, void* /*Unused*/)
{
    // FUN_06d0a7ec: checks ready bit only (not device pointer). Returns 0xFFFFFFF9 or -1.
    if ((DeviceReady_ & 1) == 0)
    {
        Rp8Fdump(kFdumpWarning, 2573, "WARNING",
                 "get_buffer_interface() called outside of create_buffers/destroy_buffers");
        return GR_GENERIC;
    }
    return (GENRESULT)(0xFFFFFFF9);
}
GENRESULT DACOM_API FDirectX8::GetDeviceStats(void* /*Out*/)
{
    // FUN_06d0a863: checks device pointer only (not ready bit). Returns 0xFFFFFFF9 or -1.
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 2583, "WARNING",
                 "get_device_stats() called outside of device lifetime");
        return GR_GENERIC;
    }
    return (GENRESULT)(0xFFFFFFF9);
}
GENRESULT DACOM_API FDirectX8::SetViewportRect(int X, int Y, int W, int H)
{
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 2603, "WARNING", "set_viewport() called outside of device lifetime");
        return GR_GENERIC;
    }

    HRESULT Hr = 0;
    if (bViewportRectValid_ == 0 || ViewportCache_.X != (DWORD)(X) ||
        ViewportCache_.Y != (DWORD)(Y) || ViewportCache_.Width != (DWORD)(W) ||
        ViewportCache_.Height != (DWORD)(H))
    {
        ViewportCache_.X = (DWORD)(X);
        ViewportCache_.Y = (DWORD)(Y);
        ViewportCache_.Width = (DWORD)(W);
        ViewportCache_.Height = (DWORD)(H);
        Hr = Device_->SetViewport(&ViewportCache_);
        if (!FAILED(Hr))
            bViewportRectValid_ = 1;
    }
    if (FAILED(Hr))
        FdumpHr("set_viewport: %s", Hr, 2606);
    return (GENRESULT)(Hr);
}
GENRESULT DACOM_API FDirectX8::GetViewportRect(int* OutX, int* OutY, int* OutW, int* OutH)
{
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 2613, "WARNING", "get_viewport() called outside of device lifetime");
        return GR_GENERIC;
    }

    if (bViewportRectValid_ == 0)
    {
        const HRESULT Hr = Device_->GetViewport(&ViewportCache_);
        if (FAILED(Hr))
            memset(&ViewportCache_, 0, sizeof(ViewportCache_));
        else
        {
            bDepthRangeValid_ = 1;
            bViewportRectValid_ = 1;
        }
    }
    *OutX = (int)(ViewportCache_.X);
    *OutY = (int)(ViewportCache_.Y);
    *OutW = (int)(ViewportCache_.Width);
    *OutH = (int)(ViewportCache_.Height);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetDepthRange(float MinZ, float MaxZ)
{
    const GENRESULT Ready = NeedDeviceLifetime(
        "set_depth_range() called outside of device lifetime", 2622);
    if (Ready != GR_OK)
        return Ready;
    if (bDepthRangeValid_ == 0 || MinZ != ViewportCache_.MinZ || MaxZ != ViewportCache_.MaxZ)
    {
        ViewportCache_.MinZ = MinZ;
        ViewportCache_.MaxZ = MaxZ;
        const HRESULT Hr = Device_->SetViewport(&ViewportCache_);
        if (SUCCEEDED(Hr))
            bDepthRangeValid_ = 1;
    }
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetDepthRange(float* OutMinZ, float* OutMaxZ)
{
    const GENRESULT Ready = NeedDeviceLifetime(
        "get_depth_range() called outside of device lifetime", 2631);
    if (Ready != GR_OK)
        return Ready;
    // FUN_06d0aca6 RET 0xC: +0x22e0 clear -> GetViewport (+0xa4) straight into the cache; ok ->
    // +0x22e0 = 1, else the 6 cache dwords zeroed (REP STOSD). Then *min/*max; return 0.
    // (The out pointers are not tested.)
    if (bDepthRangeValid_ == 0)
    {
        const HRESULT Hr = Device_->GetViewport(&ViewportCache_);
        if (Hr >= 0)
            bDepthRangeValid_ = 1;
        else
            memset(&ViewportCache_, 0, sizeof(ViewportCache_));
    }
    *OutMinZ = ViewportCache_.MinZ;
    *OutMaxZ = ViewportCache_.MaxZ;
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetWindow(HWND Hwnd, int X, int Y, int W, int H)
{
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 2643, "WARNING", "set_window() called outside of device lifetime");
        return GR_GENERIC;
    }
    WindowX_ = X;
    WindowY_ = Y;
    WindowW_ = W;
    WindowH_ = H;
    WindowOverride_ = Hwnd;
    return GR_OK;
}

GENRESULT DACOM_API FDirectX8::GetWindow(HWND* OutHwnd, int* OutLeft, int* OutRight, int* OutW,
                                        int* OutH)
{
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 2658, "WARNING", "get_window() called outside of device lifetime");
        return GR_GENERIC;
    }
    RECT Rect;
    if (WindowOverride_ == nullptr)
    {
        *OutHwnd = Hwnd_;
        SetRect(&Rect, 0, 0, ModeBlob_[2], ModeBlob_[3]);
    }
    else
    {
        *OutHwnd = WindowOverride_;
        SetRect(&Rect, WindowX_, WindowY_, WindowX_ + WindowW_, WindowY_ + WindowH_);
    }
    *OutLeft = Rect.left;
    *OutRight = Rect.right;
    *OutW = Rect.right - Rect.left;
    *OutH = Rect.bottom - Rect.top;
    return GR_OK;
}
// The matrix accessors below all inline FUN_06d172f0 / its unpack twin with a constant
// "commit" argument of 0: `XOR reg,reg; TEST reg,reg; JZ` skips FUN_06d17290, so a setter only
// writes the store and clears its dirty byte (+0x40 = 0, StoreMatrix12/16); FUN_06d04138
// (FlushDrawState) sends it at draw time. None of them tests the matrix pointer.
GENRESULT DACOM_API FDirectX8::SetWorld(const float* M12)
{
    // FUN_06d0b09d (line 0xa9a): bFlip = 0 (the local is 1 and is inverted by NEG/SBB/INC).
    const GENRESULT Ready =
        NeedDeviceLifetime("set_world() called outside of device lifetime", 0xa9a);
    if (Ready != GR_OK)
        return Ready;
    Transforms_.World.StoreAffine(M12);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetWorld(float* Out12)
{
    // FUN_06d0b3dd (line 0xaa5): unpack +0x2240, no flip (same inverted local as set_world).
    const GENRESULT Ready =
        NeedDeviceLifetime("get_world() called outside of device lifetime", 0xaa5);
    if (Ready != GR_OK)
        return Ready;
    Transforms_.World.LoadAffine(Out12);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetView(const float* M12)
{
    // FUN_06d0b607 (line 0xaaf): bFlip = 1 -> ZFlip * M12 into +0x21FC, dirty, no commit.
    const GENRESULT Ready =
        NeedDeviceLifetime("set_view() called outside of device lifetime", 0xaaf);
    if (Ready != GR_OK)
        return Ready;
    float Flipped[12];
    MulZFlip12(Flipped, M12);
    Transforms_.View.StoreAffine(Flipped);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetView(float* Out12)
{
    // FUN_06d0b939 (line 0xaba; retail's message really says get_modelview): unpack +0x21FC
    // then ZFlip * stored, which undoes set_view.
    const GENRESULT Ready =
        NeedDeviceLifetime("get_modelview() called outside of device lifetime", 0xaba);
    if (Ready != GR_OK)
        return Ready;
    float Stored[12];
    Transforms_.View.LoadAffine(Stored);
    MulZFlip12(Out12, Stored);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetModelview(const float* M12)
{
    // FUN_06d0bb58 (line 0xac4). bOff = ([this+0x3A8] == 0) (SETZ, VIEWSPACE_LIGHTS state 7).
    // WORLD: !bOff -> ZFlip * M12, else M12 (NEG/SBB/INC on bOff); dirty, no commit.
    // VIEW, only while [+0x21FC+0x41] == 0: an identity transform, flipped when bOff, stored
    // dirty without a commit, then +0x41 = 1. Returns 0.
    const GENRESULT Ready =
        NeedDeviceLifetime("set_modelview() called outside of device lifetime", 0xac4);
    if (Ready != GR_OK)
        return Ready;

    const bool bViewspaceLightsOff = PipelineStateValue(7) == 0;
    if (!bViewspaceLightsOff)
    {
        float Flipped[12];
        MulZFlip12(Flipped, M12);
        Transforms_.World.StoreAffine(Flipped);
    }
    else
    {
        Transforms_.World.StoreAffine(M12);
    }

    if (Transforms_.View.bApplyMarker == 0)
    {
        const float Identity12[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                      0.0f, 0.0f, 0.0f};
        if (bViewspaceLightsOff)
        {
            float Flipped[12];
            MulZFlip12(Flipped, Identity12);
            Transforms_.View.StoreAffine(Flipped);
        }
        else
        {
            Transforms_.View.StoreAffine(Identity12);
        }
        Transforms_.View.bApplyMarker = 1;
    }
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetModelview(float* Out12)
{
    // Undo the stored flips before multiplying View by World.
    const GENRESULT Ready =
        NeedDeviceLifetime("get_modelview() called outside of device lifetime", 0xad4);
    if (Ready != GR_OK)
        return Ready;
    const bool bViewspaceLightsOff = PipelineStateValue(7) == 0;

    FTransform World;
    float* World12 = (float*)(&World);
    Transforms_.World.LoadAffine(World12);
    if (!bViewspaceLightsOff)
        MulZFlip12(World12, World12);

    FTransform View;
    float* View12 = (float*)(&View);
    Transforms_.View.LoadAffine(View12);
    if (bViewspaceLightsOff)
        MulZFlip12(View12, View12);

    FTransform Modelview;
    Rp8MathEngine()->MulTransform(Modelview, View, World);
    std::memcpy(Out12, &Modelview, sizeof(Modelview));
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetProjection(const float* M16)
{
    // FUN_06d0c2a1 (line 0xae4): 16 dwords transposed into +0x2284, +0x40 = 0; the commit
    // block (06d0c3ee: SetTransform(D3DTS_PROJECTION = 3) via device +0x94) sits behind a
    // constant 0 and never runs. Returns 0.
    const GENRESULT Ready =
        NeedDeviceLifetime("set_projection() called outside of device lifetime", 0xae4);
    if (Ready != GR_OK)
        return Ready;
    Transforms_.Projection.StoreTransposed(M16);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetProjection(float* Out16)
{
    // FUN_06d0c438 (line 0xaed): unpack +0x2284 into Out16. Returns 0.
    const GENRESULT Ready =
        NeedDeviceLifetime("get_projection() called outside of device lifetime", 0xaed);
    if (Ready != GR_OK)
        return Ready;
    Transforms_.Projection.LoadTransposed(Out16);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetLookat(float EyeX, float EyeY, float EyeZ, float AtX, float AtY,
                                         float AtZ, float UpX, float UpY, float UpZ)
{
    // FUN_06d0c57c (rp8.dll.txt 18064-18443). Device check: FDUMP "set_lookat() called
    // outside of device lifetime" (line 0xAF6), return -1.
    const GENRESULT Ready =
        NeedDeviceLifetime("set_lookat() called outside of device lifetime", 0xaf6);
    if (Ready != GR_OK)
        return Ready;

    FVector Eye;
    Eye.X = EyeX;
    Eye.Y = EyeY;
    Eye.Z = EyeZ;

    // FUN_06d15da0(&Fwd, &At, &Eye): Fwd = At - Eye, then normalised (no zero check).
    FVector Fwd;
    Fwd.X = AtX - EyeX;
    Fwd.Y = AtY - EyeY;
    Fwd.Z = AtZ - EyeZ;
    Rp8NormalizeInPlace(Fwd);

    // FUN_06d15d50: Z = -Fwd (FCHS per component).
    FVector AxisZ;
    AxisZ.X = -Fwd.X;
    AxisZ.Y = -Fwd.Y;
    AxisZ.Z = -Fwd.Z;

    // X = Up x Z: each component `FLD a; FMUL b; FLD c; FMUL d; FSUBP` (a*b - c*d on the x87
    // stack) stored as float by FUN_06d15ce0; then normalised. No fallback axis.
    FVector AxisX;
    AxisX.X = (float)((double)UpY * AxisZ.Z -
                                 (double)UpZ * AxisZ.Y);
    AxisX.Y = (float)((double)UpZ * AxisZ.X -
                                 (double)UpX * AxisZ.Z);
    AxisX.Z = (float)((double)UpX * AxisZ.Y -
                                 (double)UpY * AxisZ.X);
    Rp8NormalizeInPlace(AxisX);

    // Y = Z x X, then normalised as well.
    FVector AxisY;
    AxisY.X = (float)((double)AxisZ.Y * AxisX.Z -
                                 (double)AxisZ.Z * AxisX.Y);
    AxisY.Y = (float)((double)AxisZ.Z * AxisX.X -
                                 (double)AxisZ.X * AxisX.Z);
    AxisY.Z = (float)((double)AxisZ.X * AxisX.Y -
                                 (double)AxisZ.Y * AxisX.X);
    Rp8NormalizeInPlace(AxisY);

    // FUN_06d15e00: rotation rows X, Y, Z. FUN_06d15e60 = MathEngine->TransformMatrix(Rotation,
    // Eye) (slot +0x2C), negated by FUN_06d15d50 into the translation.
    FTransform Xform;
    Xform.Rotation.D[0][0] = AxisX.X;
    Xform.Rotation.D[0][1] = AxisX.Y;
    Xform.Rotation.D[0][2] = AxisX.Z;
    Xform.Rotation.D[1][0] = AxisY.X;
    Xform.Rotation.D[1][1] = AxisY.Y;
    Xform.Rotation.D[1][2] = AxisY.Z;
    Xform.Rotation.D[2][0] = AxisZ.X;
    Xform.Rotation.D[2][1] = AxisZ.Y;
    Xform.Rotation.D[2][2] = AxisZ.Z;
    FVector Rotated;
    Rp8MathEngine()->TransformMatrix(Rotated, Xform.Rotation, Eye);
    Xform.Translation.X = -Rotated.X;
    Xform.Translation.Y = -Rotated.Y;
    Xform.Translation.Z = -Rotated.Z;
    const float* Xform12 = (const float*)(&Xform);

    // Every store goes through FUN_06d172f0 with its last argument 0: the matrix is written
    // and marked dirty (+0x40 = 0) but NOT sent to the device here -- FUN_06d04138 commits it
    // at draw time (FlushDrawState). The bFlip argument multiplies ZFlip in first.
    float Flipped[12];
    if (PipelineStateValue(7) != 0)
    {
        // FUN_06d172f0(+0x2240, device, bFlip=1, D3DTS_WORLD, &Xform, 0)
        MulZFlip12(Flipped, Xform12);
        Transforms_.World.StoreAffine(Flipped);
        // View store: `MOVZX [+0x21FC+0x41]; JZ` -- only while the VIEWSPACE view is unset,
        // FUN_06d172f0(+0x21FC, device, bFlip=0, D3DTS_VIEW, &Identity, 0), then +0x41 = 1.
        if (Transforms_.View.bApplyMarker == 0)
        {
            const float Identity12[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                          0.0f, 0.0f, 0.0f};
            Transforms_.View.StoreAffine(Identity12);
            Transforms_.View.bApplyMarker = 1;
        }
    }
    else
    {
        // FUN_06d172f0(+0x21FC, device, bFlip=1, D3DTS_VIEW, &Xform, 0)
        MulZFlip12(Flipped, Xform12);
        Transforms_.View.StoreAffine(Flipped);
    }
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetOrtho(float Left, float Right, float Bottom, float Top,
                                        float NearPlane, float FarPlane)
{
    const GENRESULT Ready = NeedDeviceLifetime(
        "set_ortho() called outside of device lifetime", 2855);
    if (Ready != GR_OK)
        return Ready;
    // Keep differences as double until the individual float stores.
    float Matrix[16];
    memset(Matrix, 0, sizeof(Matrix));
    const double Width = (double)Right - (double)Left;
    const double Height = (double)Top - (double)Bottom;
    const double Depth = (double)FarPlane - (double)NearPlane;
    Matrix[0] = (float)(2.0 / Width);
    Matrix[5] = (float)(2.0 / Height);
    Matrix[10] = (float)(2.0 / Depth);
    Matrix[12] = (float)(-((double)Right + (double)Left) / Width);
    Matrix[13] = (float)(-((double)Bottom + (double)Top) / Height);
    Matrix[14] = (float)(-(double)NearPlane / Depth);
    Matrix[15] = 1.0f;
    memcpy(Transforms_.Projection.Values, Matrix, sizeof(Matrix));
    Transforms_.Projection.bCommitted = 0;
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetPerspective(float FovDeg, float Aspect, float NearPlane,
                                              float FarPlane)
{
    const GENRESULT Ready = NeedDeviceLifetime(
        "set_perspective() called outside of device lifetime", 2877);
    if (Ready != GR_OK)
        return Ready;
    // Invalid near/far values fail; aspect/FOV and descending far ranges are accepted.
    if (NearPlane < 0.0f || NearPlane == 0.0f)
        return GR_GENERIC;
    if (FarPlane < 0.0f || FarPlane == 0.0f)
        return GR_GENERIC;
    if (FarPlane == NearPlane)
        return GR_GENERIC;
    // cot = float(1.0 / _CItan(pi/180.0 * fov)) -- the product stays unrounded on the x87
    // stack (FST, not FSTP); inv = float(1.0f / (far - near)).
    const float Cot = (float)(
        1.0 / tan(3.141592653589793 / 180.0 * (double)FovDeg));
    const float InvDepth =
        (float)(1.0 / ((double)FarPlane - (double)NearPlane));
    // Zeroed 4x4: [0] = cot/aspect, [5] = cot, [10] = far*inv, [11] = 1.0f,
    // [14] = -far*near*inv; into PROJECTION +0x2284, +0x40 = 0; constant-0 commit (not sent).
    float Matrix[16];
    memset(Matrix, 0, sizeof(Matrix));
    Matrix[0] = (float)((double)Cot / (double)Aspect);
    Matrix[5] = Cot;
    Matrix[10] = (float)((double)FarPlane * (double)InvDepth);
    Matrix[11] = 1.0f;
    Matrix[14] = (float)(-(double)FarPlane * (double)NearPlane *
                                    (double)InvDepth);
    memcpy(Transforms_.Projection.Values, Matrix, sizeof(Matrix));
    Transforms_.Projection.bCommitted = 0;
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetLight(void* Handle, const D3DLIGHT8* Light)
{
    // FUN_06d0ce57 (RET 0xC): no device -> 0xb8b, -1. Else the record's index
    // (FUN_06d2c487(Handle, 0): created without data when new) and Device +0xB0
    // SetLight(index, Light) at once; failure -> "set_light: %s" at 0xb96. Returns hr.
    const GENRESULT Ready = NeedDeviceLifetime(
        "set_light() called outside of device lifetime", 0xb8b);
    if (Ready != GR_OK)
        return Ready;
    const int Index = LightManager_.FindOrCreate(
                                        (unsigned long)(uintptr_t)Handle,
                                        nullptr);
    const HRESULT Hr =
        Device_->SetLight((DWORD)Index, Light);
    if (Hr < 0)
        FdumpHr("set_light: %s", Hr, 0xb96);
    return (GENRESULT)Hr;
}
GENRESULT DACOM_API FDirectX8::DestroyLight(void* Handle)
{
    // FUN_06d0cf9c (RET 8): no device -> 0xb9e, -1. Else FUN_06d2cb6f(Handle, 0), the flush
    // FUN_06d2d032(device) (the LightEnable(FALSE) reaches the device now), FUN_06d2c7cf(Handle);
    // 0.
    const GENRESULT Ready = NeedDeviceLifetime(
        "destroy_light() called outside of device lifetime", 0xb9e);
    if (Ready != GR_OK)
        return Ready;
    const unsigned long Key = (unsigned long)(uintptr_t)Handle;
    LightManager_.SetEnable(Key, 0);
    LightManager_.Flush(Device_);
    LightManager_.Remove(Key);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetLight(void* Handle, D3DLIGHT8* OutLight)
{
    // FUN_06d0d044 (RET 0xC): no device -> 0xbad, -1. Else Device +0xB4 GetLight with the first
    // argument passed through unchanged as the D3D light index; returns that hr.
    const GENRESULT Ready = NeedDeviceLifetime(
        "get_light() called outside of device lifetime", 0xbad);
    if (Ready != GR_OK)
        return Ready;
    return (GENRESULT)(
        Device_->GetLight((DWORD)((uintptr_t)Handle),
                          OutLight));
}
GENRESULT DACOM_API FDirectX8::SetLightEnable(void* Handle, int Enable)
{
    // FUN_06d0d0d7 (RET 0xC): no device -> 0xbb8, -1. Else FUN_06d2cb6f(this+0x21F4, Handle,
    // Enable) -- the request is only recorded; the draw-time flush commits it -- and 0.
    const GENRESULT Ready = NeedDeviceLifetime(
        "set_light_enable() called outside of device lifetime", 0xbb8);
    if (Ready != GR_OK)
        return Ready;
    LightManager_.SetEnable((unsigned long)(uintptr_t)Handle,
                   Enable);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetLightEnable(void* Handle, int* OutEnable)
{
    // FUN_06d0d157 (RET 0xC): no device -> 0xbc2, -1. Else *OutEnable = the byte
    // FUN_06d2c92a(this+0x21F4, Handle) returns (the requested state), zero-extended; 0.
    const GENRESULT Ready = NeedDeviceLifetime(
        "get_light_enable() called outside of device lifetime", 0xbc2);
    if (Ready != GR_OK)
        return Ready;
    *OutEnable = LightManager_.GetEnable(
                                (unsigned long)(uintptr_t)Handle);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetMaterial(const D3DMATERIAL8* Material)
{
    const GENRESULT Ready = NeedBuffers(
        "set_material() called outside of create_buffers/destroy_buffers", 3020);
    if (Ready != GR_OK)
        return Ready;
    // FUN_06d0d1dc 06d0d254..06d0d30d: Device->SetMaterial(+0xa8); hr < 0 ->
    // "set_material: %s" (line 0xbd0); return hr.
    const HRESULT Hr = Device_->SetMaterial(Material);
    if (Hr < 0)
        FdumpHr("set_material: %s", Hr, 3024);
    return (GENRESULT)Hr;
}
GENRESULT DACOM_API FDirectX8::GetMaterial(D3DMATERIAL8* Material)
{
    const GENRESULT Ready = NeedBuffers(
        "get_material() called outside of create_buffers/destroy_buffers", 3035);
    if (Ready != GR_OK)
        return Ready;
    // FUN_06d0d310 06d0d388..: Device->GetMaterial(+0xac); hr < 0 -> "get_material: %s"
    // (line 0xbdf); return hr.
    const HRESULT Hr = Device_->GetMaterial(Material);
    if (Hr < 0)
        FdumpHr("get_material: %s", Hr, 3039);
    return (GENRESULT)Hr;
}
GENRESULT DACOM_API FDirectX8::CreateTexture(int Width, int Height, unsigned int* Format,
                                             unsigned int Levels, unsigned int Flags,
                                             void** OutHandle)
{
    // FUN_06d0d628 opens with CMP [this+0x130],0 -- nothing is written to OutHandle first.
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 3099, "WARNING", "create_texture() called outside of device lifetime");
        return GR_GENERIC;
    }
    // 06d0d696: flags & 1 -> 0xFFFFFFFE (nothing written). The winning format starts as
    // FUN_06d5cc30(0); usage 0, D3DRTYPE_TEXTURE (3).
    if ((Flags & 1) != 0)
        return (GENRESULT)(0xFFFFFFFE);
    unsigned int WinningFormat = PfIndex(0);
    // 06d0d6e7: query_device_ability(5 = TEXTURE_SQUARE_ONLY) through the vtable (+0x20); the
    // result is not checked. Square-only and W != H -> both become the larger (signed JLE).
    unsigned long bSquareOnly = 0;
    QueryDeviceAbility(5, &bSquareOnly);
    if (Width != Height && bSquareOnly != 0)
    {
        if (Width > Height)
            Height = Width;
        else
            Width = Height;
    }
    // Try candidates in catalog order; keep the selected format and mip-chain policy.
    bool bFound = CheckTextureFormat(*Format, 0, D3DRTYPE_TEXTURE);
    if (bFound)
    {
        WinningFormat = *Format;
    }
    else if (FormatClassKeyFromPfId(*Format) != 0)
    {
        FormatClassNode* Node = FindFormatClass(FormatClassKeyFromPfId(*Format));
        if (Node != nullptr)
        {
            for (unsigned int Index = 0; Index < Node->Count; ++Index)
            {
                if (CheckTextureFormat(Node->Values[Index], 0, D3DRTYPE_TEXTURE))
                {
                    bFound = true;
                    WinningFormat = Node->Values[Index];
                    break;
                }
            }
        }
    }
    // 06d0d7dc: nothing usable -> NOTICE 0xc67 (the text ends in '\n'), -1.
    if (!bFound)
    {
        Rp8Fdump(kFdumpNotice, 0xc67, "NOTICE",
                 "DX8RP: create_texture: cannot find suitable texture format.\n");
        return GR_GENERIC;
    }
    // 06d0d839: Device+0x50 CreateTexture(W, H, levels, 0, pf_to_d3d(winner), D3DPOOL_MANAGED);
    // hr < 0 -> 'create_texture: %s' 0xc87. A texture -> operator new(8) {texture, 0}, else 0;
    // *OutHandle written either way; return hr.
    IDirect3DTexture8* Texture = nullptr;
    const HRESULT Hr = Device_->CreateTexture((UINT)(Width), (UINT)(Height),
                                              Levels, 0,
                                              (D3DFORMAT)(PfToD3d(WinningFormat)),
                                              D3DPOOL_MANAGED, &Texture);
    if (Hr < 0)
        FdumpHr("create_texture: %s", Hr, 0xc87);
    FRp8TextureHandle* Handle = nullptr;
    if (Texture != nullptr)
    {
        Handle = new FRp8TextureHandle;
        Handle->Texture = Texture;
        Handle->Extra = 0;
    }
    *OutHandle = Handle;
    return (GENRESULT)(Hr);
}
GENRESULT DACOM_API FDirectX8::DestroyTexture(void* Handle)
{
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 3217, "WARNING", "destroy_texture() called outside of device lifetime");
        return GR_GENERIC;
    }
    FRp8TextureHandle* Wrapper = (FRp8TextureHandle*)(Handle);
    // FUN_06d0d997 RET 0x8: invalid handle/texture -> dump (text ends in '\n', PE bytes) and
    // carry on; the stage-unbind loop runs only for a live texture; then release + delete.
    if (Wrapper == nullptr || Wrapper->Texture == nullptr)
        Rp8Fdump(kFdumpWarning, 0xc95, "WARNING", "destroy_texture: invalid texture\n");
    if (Wrapper != nullptr && Wrapper->Texture != nullptr)
        UnbindTextureHandle(Wrapper);
    // 06d0dbdc: the handle's scalar deleting destructor -- texture set -> Release, 0 -- then
    // operator delete (the handle comes from operator new(8)).
    if (Wrapper != nullptr)
    {
        if (Wrapper->Texture != nullptr)
        {
            Wrapper->Texture->Release();
            Wrapper->Texture = nullptr;
        }
        delete Wrapper;
    }
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::IsTexture(void* A)
{
    const GENRESULT Ready = NeedDeviceLifetime(
        "is_texture() called outside of device lifetime", 3232);
    if (Ready != GR_OK)
        return Ready;
    FRp8TextureHandle* Wrapper = (FRp8TextureHandle*)(A);
    if (Wrapper == nullptr || Wrapper->Texture == nullptr)
        return GR_GENERIC;
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::LockTexture(void* A, int Level, void* C)
{
    void* Handle = A;
    unsigned long* Out = (unsigned long*)(C);
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 3244, "WARNING", "lock_texture() called outside of device lifetime");
        return GR_GENERIC;
    }
    // FUN_06d0dce3 RET 0x10, traced line by line.
    // 06d0dd51: handle == 0 -> -2.
    if (Handle == nullptr)
        return (GENRESULT)(0xFFFFFFFE);
    FRp8TextureHandle* Wrapper = (FRp8TextureHandle*)(Handle);
    // 06d0dd67..06d0defe: get_desc (Level) failing -> -1.
    D3DSURFACE_DESC Desc;
    if (!TextureGetDesc(Wrapper, (UINT)(Level), &Desc))
        return GR_GENERIC;
    // 06d0df06..06d0e054: get_subsurface; hr < 0 -> release (the surface pointer is null) and -1.
    IDirect3DSurface8* Surface = nullptr;
    HRESULT Hr = TextureGetSubSurface(Wrapper, (UINT)(Level), &Surface);
    if (Hr < 0)
    {
        if (Surface != nullptr)
        {
            Surface->Release();
            Surface = nullptr;
        }
        return GR_GENERIC;
    }
    // 06d0e059: LockRect (+0x24)(&Locked, NULL, USE_SYSLOCK ? 0 : NOSYSLOCK 0x800); hr < 0 ->
    // 'lock_texture: %s' 0xcc8.
    const unsigned long LockFlags = ((SwapFlags_ >> 1) & 1) != 0 ? 0ul : 0x800ul;
    D3DLOCKED_RECT Locked;
    Hr = Surface->LockRect(&Locked, nullptr, LockFlags);
    if (Hr < 0)
        FdumpHr("lock_texture: %s", Hr, 3272);
    // 06d0e134: written whatever the lock returned: +0x10 FUN_06d5cc30(desc.Format),
    // +0 desc.Width, +4 desc.Height, +8 pitch, +0xc bits. Then release the surface, return hr.
    Out[4] = PfIndex((unsigned int)(Desc.Format));
    Out[0] = Desc.Width;
    Out[1] = Desc.Height;
    Out[2] = (unsigned long)(Locked.Pitch);
    Out[3] = (unsigned long)(Locked.pBits);
    Surface->Release();
    Surface = nullptr;
    return (GENRESULT)(Hr);
}
GENRESULT DACOM_API FDirectX8::UnlockTexture(void* A, int Level)
{
    void* Handle = A;
    if (Device_ == nullptr)
    {
        // FUN_06d0e1af (unlock_texture) reuses lock_texture's message verbatim.
        Rp8Fdump(kFdumpWarning, 3286, "WARNING", "lock_texture() called outside of device lifetime");
        return GR_GENERIC;
    }
    // FUN_06d0e1af RET 0xC: handle == 0 -> -2; get_subsurface (Level) failing -> -1; else
    // hr = UnlockRect (+0x28); release the surface; return hr.
    if (Handle == nullptr)
        return (GENRESULT)(0xFFFFFFFE);
    FRp8TextureHandle* Wrapper = (FRp8TextureHandle*)(Handle);
    IDirect3DSurface8* Surface = nullptr;
    HRESULT Hr = TextureGetSubSurface(Wrapper, (UINT)(Level), &Surface);
    if (Hr < 0)
    {
        if (Surface != nullptr)
        {
            Surface->Release();
            Surface = nullptr;
        }
        return GR_GENERIC;
    }
    Hr = Surface->UnlockRect();
    Surface->Release();
    Surface = nullptr;
    return (GENRESULT)(Hr);
}
GENRESULT DACOM_API FDirectX8::GetTextureFormat(void* Handle, int* OutFormat)
{
    // FUN_06d0e3d9 (RET 0xC), traced line by line: no device -> 0xce9, -1; null handle -> -2
    // without a dump; handle without a texture -> "get_texture_format: invalid texture\n" at
    // 0xcf5, -1; dx8_texture.h get_desc of level 0 (TextureGetDesc) failing -> -1; else
    // *OutFormat = FUN_06d5cc30(desc.Format) and 0.
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 0xce9, "WARNING",
                 "get_texture_format() called outside of device lifetime");
        return GR_GENERIC;
    }
    FRp8TextureHandle* Wrapper = (FRp8TextureHandle*)(Handle);
    if (Wrapper == nullptr)
        return GR_INVALID_PARMS;
    if (Wrapper->Texture == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 0xcf5, "WARNING", "get_texture_format: invalid texture\n");
        return GR_GENERIC;
    }
    // A handle tagged neither 0 nor 1 reports success with the descriptor untouched: retail
    // then converts stack garbage; the port's descriptor starts zeroed.
    D3DSURFACE_DESC Desc;
    memset(&Desc, 0, sizeof(Desc));
    if (!TextureGetDesc(Wrapper, 0, &Desc))
        return GR_GENERIC;
    *OutFormat = (int)(PfIndex((unsigned int)(Desc.Format)));
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetTextureDim(void* Handle, unsigned int* OutW, unsigned int* OutH,
                                             int* OutLevels)
{
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 0xd01, "WARNING", "get_texture_dim() called outside of device lifetime");
        return GR_GENERIC;
    }
    FRp8TextureHandle* Wrapper = (FRp8TextureHandle*)(Handle);
    if (Wrapper == nullptr || Wrapper->Texture == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 0xd09, "WARNING", "set_texture_dim: invalid texture\n");
        return GR_GENERIC;
    }
    // Retail leaves the descriptor as stack garbage when get_desc fails or the tag is neither
    // 0 nor 1; the port's starts zeroed.
    D3DSURFACE_DESC Desc;
    memset(&Desc, 0, sizeof(Desc));
    TextureGetDesc(Wrapper, 0, &Desc);
    *OutW = Desc.Width;
    *OutH = Desc.Height;
    if (Wrapper->Extra == 0)
        *OutLevels = (int)(Wrapper->Texture->GetLevelCount());
    else if (Wrapper->Extra == 1)
        *OutLevels = 6;
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetTextureInterface(void* /*A*/, void* /*UnusedA*/, void* /*UnusedB*/)
{
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 3349, "WARNING",
                 "get_texture_interface() called outside of device lifetime");
        return GR_GENERIC;
    }
    return (GENRESULT)(0xFFFFFFF9);
}
GENRESULT DACOM_API FDirectX8::SetTextureLevelData(void* Handle, int Level, int SrcW, int SrcH,
                                                   unsigned int Pitch, unsigned int* SrcFormat,
                                                   const void* Pixels, void* /*A*/, void* Palette)
{
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 3358, "WARNING",
                 "set_texture_level_data() called outside of device lifetime");
        return GR_GENERIC;
    }
    // FUN_06d0ea78 RET 0x28, traced line by line. The eighth argument is never read.
    // 06d0eaf5: handle 0 or texture 0 -> dump 0xd23 (text via sprintf, leading space and the
    // retail 'passsed' spelling, PE 06d67da0), -2.
    FRp8TextureHandle* Wrapper = (FRp8TextureHandle*)(Handle);
    if (Wrapper == nullptr || Wrapper->Texture == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 3363, "WARNING",
                 " set_texture_level_data: invalid texture handle passsed");
        return (GENRESULT)(0xFFFFFFFE);
    }
    // 06d0eb9b: *SrcFormat == 0 -> dump 0xd29 (PE 06d67e40), -2 (the pointer is not tested).
    if (*SrcFormat == 0)
    {
        Rp8Fdump(kFdumpWarning, 3369, "WARNING",
                 " set_texture_level_data: invalid src format passed");
        return (GENRESULT)(0xFFFFFFFE);
    }
    // 06d0ec36: stage-unbind loop for this handle.
    UnbindTextureHandle(Wrapper);
    // 06d0ed0e: a non-zero level with TEXTURE_LOD (pipeline state id 2, +0x36c) off -> 0.
    if (Level != 0 && PipelineStateValue(2) == 0)
        return GR_OK;
    // 06d0ed32: FUN_06d1b1c0 = get_desc of level 0 (its format result is never used); failure
    // -> -1.
    D3DSURFACE_DESC Level0Desc;
    if (!TextureGetDesc(Wrapper, 0, &Level0Desc))
        return GR_GENERIC;
    // 06d0ed92: get_subsurface (Level); failure -> release, -1.
    IDirect3DSurface8* Surface = nullptr;
    HRESULT Hr = TextureGetSubSurface(Wrapper, (UINT)(Level), &Surface);
    if (Hr < 0)
    {
        if (Surface != nullptr)
        {
            Surface->Release();
            Surface = nullptr;
        }
        return GR_GENERIC;
    }
    // 06d0eeeb: SrcRect = (0,0,SrcW,SrcH); filter D3DX_FILTER_NONE (1); surface GetDesc (+0x20);
    // failure -> 'get_texture_dim: %s' 0xd5b, release, return hr.
    RECT SrcRect;
    SetRect(&SrcRect, 0, 0, SrcW, SrcH);
    DWORD Filter = D3DX_FILTER_NONE;
    D3DSURFACE_DESC Desc;
    Hr = Surface->GetDesc(&Desc);
    if (Hr < 0)
    {
        FdumpHr("get_texture_dim: %s", Hr, 3419);
        Surface->Release();
        Surface = nullptr;
        return (GENRESULT)(Hr);
    }
    // 06d0eff8: a size mismatch switches the filter to 4 (D3DX_FILTER_TRIANGLE).
    if (Desc.Width != (UINT)(SrcW) || Desc.Height != (UINT)(SrcH))
        Filter = D3DX_FILTER_TRIANGLE;
    // 06d0f012: an RGB palette (3 bytes/entry) becomes 256 PALETTEENTRY with flags 0xff.
    PALETTEENTRY PaletteEntries[256];
    const PALETTEENTRY* SrcPalette = nullptr;
    if (Palette != nullptr)
    {
        SrcPalette = PaletteEntries;
        const unsigned char* PaletteBytes = (const unsigned char*)(Palette);
        for (int Index = 0; Index < 0x100; ++Index)
        {
            PaletteEntries[Index].peRed = PaletteBytes[Index * 3];
            PaletteEntries[Index].peGreen = PaletteBytes[Index * 3 + 1];
            PaletteEntries[Index].peBlue = PaletteBytes[Index * 3 + 2];
            PaletteEntries[Index].peFlags = 0xFF;
        }
    }
    // 06d0f0bc: D3DXLoadSurfaceFromMemory (FUN_06d2f6ee)(surface, NULL, NULL, Pixels,
    // FUN_06d5cbb0(*SrcFormat), Pitch, palette, &SrcRect, filter, 0); hr < 0 ->
    // 'set_texture_level_data: %s' 0xd7e. Release the surface, return hr.
    Hr = D3DXLoadSurfaceFromMemory(Surface, nullptr, nullptr, Pixels,
                                   (D3DFORMAT)(PfToD3d(*SrcFormat)), Pitch,
                                   SrcPalette, &SrcRect, Filter, 0);
    if (Hr < 0)
        FdumpHr("set_texture_level_data: %s", Hr, 3454);
    Surface->Release();
    Surface = nullptr;
    return (GENRESULT)(Hr);
}
// Blit through temporary surfaces; preserve release order on every failure.
GENRESULT DACOM_API FDirectX8::BlitTexture(void* DstHandle, int DstLevel, void* SrcHandle, int SrcLevel)
{
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 3463, "WARNING", "blit_texture() called outside of device lifetime");
        return GR_GENERIC;
    }
    // 06d0f22a / 06d0f2d0: each handle (or its texture) null -> the set_texture_level_data
    // text (PE 06d68088 / 06d68128, same bytes) at 0xd8c / 0xd93, -2.
    FRp8TextureHandle* DstWrapper = (FRp8TextureHandle*)(DstHandle);
    if (DstWrapper == nullptr || DstWrapper->Texture == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 3468, "WARNING",
                 " set_texture_level_data: invalid texture handle passsed");
        return (GENRESULT)(0xFFFFFFFE);
    }
    FRp8TextureHandle* SrcWrapper = (FRp8TextureHandle*)(SrcHandle);
    if (SrcWrapper == nullptr || SrcWrapper->Texture == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 3475, "WARNING",
                 " set_texture_level_data: invalid texture handle passsed");
        return (GENRESULT)(0xFFFFFFFE);
    }
    // 06d0f376: get_subsurface of the first (destination) handle; failure -> -1.
    IDirect3DSurface8* DstSurf = nullptr;
    HRESULT Hr = TextureGetSubSurface(DstWrapper, (UINT)(DstLevel), &DstSurf);
    if (Hr < 0)
    {
        if (DstSurf != nullptr)
        {
            DstSurf->Release();
            DstSurf = nullptr;
        }
        return GR_GENERIC;
    }
    // 06d0f4ce: get_subsurface of the second (source) handle; failure -> FUN_06d167a0 on it,
    // release the first, -1.
    IDirect3DSurface8* SrcSurf = nullptr;
    Hr = TextureGetSubSurface(SrcWrapper, (UINT)(SrcLevel), &SrcSurf);
    if (Hr < 0)
    {
        if (SrcSurf != nullptr)
        {
            SrcSurf->Release();
            SrcSurf = nullptr;
        }
        DstSurf->Release();
        DstSurf = nullptr;
        return GR_GENERIC;
    }
    // 06d0f62c: D3DXLoadSurfaceFromSurface (FUN_06d2fd77)(dst, NULL, NULL, src, NULL, NULL,
    // D3DX_FILTER_POINT 2, 0); no text on failure; release both; return hr.
    Hr = D3DXLoadSurfaceFromSurface(DstSurf, nullptr, nullptr, SrcSurf, nullptr, nullptr,
                                    D3DX_FILTER_POINT, 0);
    SrcSurf->Release();
    SrcSurf = nullptr;
    DstSurf->Release();
    DstSurf = nullptr;
    return (GENRESULT)(Hr);
}
GENRESULT DACOM_API FDirectX8::SetRenderTarget(void* /*A*/, void* /*UnusedA*/, void* /*UnusedB*/)
{
    if ((DeviceReady_ & 1) == 0)
    {
        Rp8Fdump(kFdumpWarning, 3508, "WARNING",
                 "set_render_target() called outside of create_buffers/destroy_buffers");
        return GR_GENERIC;
    }
    return (GENRESULT)(0xFFFFFFF9);
}
GENRESULT DACOM_API FDirectX8::GetRenderTarget(void* /*A*/)
{
    if ((DeviceReady_ & 1) == 0)
    {
        Rp8Fdump(kFdumpWarning, 3512, "WARNING",
                 "get_render_target() called outside of create_buffers/destroy_buffers");
        return GR_GENERIC;
    }
    return (GENRESULT)(0xFFFFFFF9);
}
GENRESULT DACOM_API FDirectX8::ResetRenderStatesToDefaults()
{
    // FUN_06d0f9e4: FUN_06d0376f + FUN_06d038ba.
    const GENRESULT Ready = NeedBuffers(
        "reset_render_states_to_defaults() called outside of create_buffers/destroy_buffers",
        3572);
    if (Ready != GR_OK)
        return Ready;
    ApplyDefaultRenderStates();
    ApplyDefaultTextureStages();
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetRenderState(D3DRENDERSTATETYPE State, int Value)
{
    const GENRESULT Ready = NeedBuffers(
        "set_render_state() called outside of create_buffers/destroy_buffers", 3586);
    if (Ready != GR_OK)
        return Ready;
    // Requests are deferred; device errors occur at the next draw-state flush.
    const bool bDirtyThisGeneration = RenderStates_.MarkDirty(State, (unsigned long)Value);
    if (bDirtyThisGeneration && State == D3DRS_SOFTWAREVERTEXPROCESSING)
        StreamCache_.Reset();
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetRenderState(D3DRENDERSTATETYPE State, int* Out)
{
    const GENRESULT Ready = NeedBuffers(
        "get_render_state() called outside of create_buffers/destroy_buffers", 3628);
    if (Ready != GR_OK)
        return Ready;
    *Out = (int)RenderStates_.GetPending(State);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetTextureStageState(int Stage, int Type, int Value)
{
    const GENRESULT Ready = NeedBuffers(
        "set_texture_stage_state() called outside of create_buffers/destroy_buffers", 3640);
    if (Ready != GR_OK)
        return Ready;
    // FUN_06d0fbcf 06d0fc40: entry {value, valid byte}; valid && value == Value -> nothing;
    // else Device+0xfc SetTextureStageState, valid = (hr >= 0), valid -> value = Value; 0.
    FRp8TextureStageState& Cache = DeviceStates_.TextureStates[Stage][Type];
    if (Cache.bValid != 0 && Cache.Value == (unsigned long)Value)
        return GR_OK;
    const HRESULT Hr = Device_->SetTextureStageState((DWORD)Stage,
                                                     (D3DTEXTURESTAGESTATETYPE)Type,
                                                     (DWORD)Value);
    Cache.bValid = Hr >= 0 ? 1 : 0;
    if (Cache.bValid != 0)
        Cache.Value = (unsigned long)Value;
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetTextureStageState(int Stage, int Type, int* Out)
{
    const GENRESULT Ready = NeedBuffers(
        "get_texture_stage_state() called outside of create_buffers/destroy_buffers", 3657);
    if (Ready != GR_OK)
        return Ready;
    // FUN_06d0fcd5 06d0fd46: cache entry +0x1854 + stage*0xE8 + type*8 {value, valid byte};
    // invalid -> Device+0xf8 GetTextureStageState(stage, type, &entry.value), valid = (hr >= 0),
    // still invalid -> value 0; *Out = value (Out is not tested); 0.
    *Out = (int)DeviceStates_.ReadTextureStageState(Device_, (unsigned long)Stage,
                                                  (D3DTEXTURESTAGESTATETYPE)Type);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetTextureStageTransform(int Stage, const float* M16)
{
    const GENRESULT Ready = NeedBuffers(
        "set_texture_stage_transform() called outside of create_buffers/destroy_buffers", 3667);
    if (Ready != GR_OK)
        return Ready;
    // FUN_06d0fdd2: M16 is not tested; transposed store into the 0x44 block
    // +0x1FD4 + stage*0x44, +0x40 = 0, then the inlined FUN_06d17290 commit with
    // type stage + D3DTS_TEXTURE0; returns 0.
    FRp8TransformCache& Cache = DeviceStates_.TextureTransforms[(unsigned int)Stage];
    Cache.StoreTransposed(M16);
    Cache.Commit(Device_, (D3DTRANSFORMSTATETYPE)((unsigned long)Stage + D3DTS_TEXTURE0));
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetTextureStageTransform(int Stage, float* Out16)
{
    const GENRESULT Ready = NeedBuffers(
        "get_texture_stage_transform() called outside of create_buffers/destroy_buffers", 3677);
    if (Ready != GR_OK)
        return Ready;
    // FUN_06d0ff7d: Out16 is not tested; transposed read of the stage block; returns 0.
    DeviceStates_.TextureTransforms[(unsigned int)Stage].LoadTransposed(Out16);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetTextureStageTexture(int Stage, void* Handle)
{
    const GENRESULT Ready = NeedBuffers(
        "set_texture_stage_texture() called outside of create_buffers/destroy_buffers", 3687);
    if (Ready != GR_OK)
        return Ready;
    // FUN_06d100d3: [this+0x354] (pipeline-state slot 0) == 0 -> 0; stage entry
    // +0x1F94 + stage*8 {handle, valid byte}: valid && handle == Handle -> 0; texture = 0,
    // Handle != 0 -> (Handle->Texture == 0 -> dx8_texture.h(0x139) WARNING) texture =
    // Handle->Texture; Device+0xf4 SetTexture(stage, texture); hr >= 0 -> entry = {Handle, 1}; 0.
    if (PipelineStateValue(0) == 0)
        return GR_OK;
    FRp8TextureBinding& Cache = DeviceStates_.TextureBindings[(unsigned int)Stage];
    if (Cache.bValid != 0 && Cache.Handle == Handle)
        return GR_OK;
    IDirect3DTexture8* Texture = nullptr;
    if (Handle != nullptr)
    {
        FRp8TextureHandle* Wrapper = (FRp8TextureHandle*)(Handle);
        if (Wrapper->Texture == nullptr)
            Rp8Fdump(kFdumpWarning, 0x139, "WARNING", "set_texture_stage_texture: invalid texture\n",
                     kSrcDx8Texture);
        Texture = Wrapper->Texture;
    }
    const HRESULT Hr = Device_->SetTexture((unsigned long)Stage, Texture);
    if (Hr >= 0)
    {
        Cache.Handle = Handle;
        Cache.bValid = 1;
    }
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetTextureStageTexture(int Stage, void** Out)
{
    const GENRESULT Ready = NeedBuffers(
        "get_texture_stage_texture() called outside of create_buffers/destroy_buffers", 3701);
    if (Ready != GR_OK)
        return Ready;
    // FUN_06d10247: result = 0; slot 0 set -> stage entry, invalid -> {0, valid 1};
    // result = entry handle; *Out = result (Out is not tested); 0.
    void* Result = nullptr;
    if (PipelineStateValue(0) != 0)
    {
        FRp8TextureBinding& Cache = DeviceStates_.TextureBindings[(unsigned int)Stage];
        if (Cache.bValid == 0)
        {
            Cache.bValid = 1;
            Cache.Handle = nullptr;
        }
        Result = Cache.Handle;
    }
    *Out = Result;
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::VerifyState()
{
    if ((DeviceReady_ & 1) == 0)
    {
        Rp8Fdump(kFdumpWarning, 3717, "WARNING",
                 "verify_state() called outside of create_buffers/destroy_buffers");
        return GR_GENERIC;
    }
    unsigned char* Base = ObjectBase();
    const unsigned long StageCount = *(unsigned long*)(Base + kOffStageCount);
    const unsigned int Fvf = (StageCount == 1) ? 0x152u : 0x252u;
    StreamCache_.SelectVertexStream(nullptr, Fvf);
    FlushDrawState(Device_);

    DWORD NumPasses = 0;
    HRESULT Hr = Device_->ValidateDevice(&NumPasses);
    GENRESULT Result = (GENRESULT)Hr;
    if (Hr >= 0)
    {
        if (NumPasses == 0)
            Result = (GENRESULT)0x80004005;
        if (DeviceStates_.ReadTextureStageState(Device_, StageCount, D3DTSS_COLOROP) != 1)
            return GR_GENERIC;
        if (DeviceStates_.ReadTextureStageState(Device_, StageCount, D3DTSS_ALPHAOP) != 1)
            return GR_GENERIC;
        const unsigned long Mod2xFlag = *(unsigned long*)(Base + 0x3E4);
        if (Mod2xFlag != 0)
        {
            for (unsigned long S = 0; S <= StageCount; ++S)
            {
                const unsigned long ColorOp = DeviceStates_.ReadTextureStageState(Device_, S, D3DTSS_COLOROP);
                if (ColorOp == 1)
                    break;
                if (ColorOp == 5)
                    return GR_GENERIC;
            }
        }
    }
    StreamCache_.Reset();
    return Result;
}
GENRESULT DACOM_API FDirectX8::DrawPrimitive(unsigned long Prim, unsigned int Fvf, const void* Verts,
                                            unsigned int Count, void* /*Unused*/)
{
    // FUN_06d1067f RET 0x18. Device +0x120 DrawPrimitiveUP.
    const GENRESULT Ready = NeedBuffers(
        "draw_primitive() called outside of create_buffers/destroy_buffers", 3789);
    if (Ready != GR_OK)
        return (GENRESULT)0xFFFFFFFF;
    HRESULT Result = (HRESULT)0x80004005;
    if ((int)Count <= 0)
        return (GENRESULT)Result;

    StreamCache_.SelectVertexStream(nullptr, Fvf);
    FlushDrawState(Device_);
    const unsigned int PrimCount = PrimCountFromVerts(Prim, Count);
    const unsigned int Stride = VertexStrideFromFvf(Fvf);
    Result = Device_->DrawPrimitiveUP((D3DPRIMITIVETYPE)(Prim), PrimCount, Verts, Stride);
    StreamCache_.Reset();
    if (FAILED(Result))
        FdumpHr("draw_primitive: %s", Result, 3809);
    return (GENRESULT)Result;
}
GENRESULT DACOM_API FDirectX8::DrawIndexedPrimitiveUp(unsigned long Prim, unsigned int Fvf,
                                                     const void* Verts, unsigned int VertCount,
                                                     const void* Indices, unsigned int IndexCount,
                                                     unsigned long Unused)
{
    // FUN_06d1097d RET 0x20 -- eight dwords, this plus seven arguments; the seventh is never
    // read here but the callee pops it and Freelancer.exe 0x004217b6 / 0x00421973 push it.
    // Device +0x124 DrawIndexedPrimitiveUP; minIndex=0, format=0x65.
    (void)Unused;
    const GENRESULT Ready = NeedBuffers(
        "draw_indexed_primitive() called outside of create_buffers/destroy_buffers", 3819);
    if (Ready != GR_OK)
        return (GENRESULT)0xFFFFFFFF;
    HRESULT Result = (HRESULT)0x80004005;
    const unsigned int PrimCount = PrimCountFromVerts(Prim, IndexCount);
    if ((int)PrimCount <= 0)
        return (GENRESULT)Result;

    StreamCache_.SelectVertexStream(nullptr, Fvf);
    FlushDrawState(Device_);
    const unsigned int Stride = VertexStrideFromFvf(Fvf);
    Result = Device_->DrawIndexedPrimitiveUP((D3DPRIMITIVETYPE)(Prim), 0, VertCount,
                                             PrimCount, Indices, D3DFMT_INDEX16, Verts, Stride);
    StreamCache_.Reset();
    if (FAILED(Result))
        FdumpHr("draw_indexed_primitive: %s", Result, 3844);
    return (GENRESULT)Result;
}
GENRESULT DACOM_API FDirectX8::DrawPrimitiveVb(unsigned long Prim, void* Vb, unsigned int StartVertex,
                                              unsigned int Count, void* /*Unused*/)
{
    // FUN_06d10c89 RET 0x18. Device +0x118 DrawPrimitive(prim, start, primCount).
    const GENRESULT Ready = NeedBuffers(
        "draw_primitive_vb() called outside of create_buffers/destroy_buffers", 3853);
    if (Ready != GR_OK)
        return (GENRESULT)0xFFFFFFFF;
    HRESULT Result = (HRESULT)0x80004005;
    const unsigned int PrimCount = PrimCountFromVerts(Prim, Count);
    if ((int)PrimCount <= 0)
        return (GENRESULT)Result;

    StreamCache_.SelectVertexStream((FRp8Vb*)Vb, 0);
    FlushDrawState(Device_);
    Result = Device_->DrawPrimitive((D3DPRIMITIVETYPE)(Prim), StartVertex, PrimCount);
    if (FAILED(Result))
        FdumpHr("draw_primitive_vb: %s", Result, 3866);
    return (GENRESULT)Result;
}
GENRESULT DACOM_API FDirectX8::DrawIndexedPrimitiveVb(unsigned long Prim, void* Vb,
                                                     unsigned int BaseVertex, unsigned int NumVerts,
                                                     const void* Indices, unsigned int IndexCount,
                                                     unsigned long Unused)
{
    (void)Unused;
    const GENRESULT Ready = NeedBuffers(
        "draw_indexed_primitive_vb() called outside of create_buffers/destroy_buffers", 3878);
    if (Ready != GR_OK)
        return (GENRESULT)0xFFFFFFFF;
    HRESULT Result = (HRESULT)0x80004005;
    const unsigned int PrimCount = PrimCountFromVerts(Prim, IndexCount);
    if ((int)PrimCount <= 0)
        return (GENRESULT)Result;

    // 06d10ee9..06d11032: FUN_06d1c660 on the scratch IB node +0x14C (start 0, bSysLock =
    // [+0x128] bit1); success -> copy IndexCount*2 bytes, node.LockedBits = 0, Unlock (+0x30,
    // result ignored), start index = the lock start. Failure -> "copy_indices: %s" at 0xf2f and
    // return hr. No null tests on the source or the locked pointer.
    unsigned char* Base = ObjectBase();
    FRp8Ib* Scratch = (FRp8Ib*)(Base + 0x14C);
    void* LockedBits = nullptr;
    unsigned int LockOffset = 0;
    const HRESULT LockHr = Scratch->LockRange(0, IndexCount, &LockedBits, &LockOffset,
                                       (unsigned char)((Base[0x128] >> 1) & 1));
    if (LockHr >= 0)
    {
        memcpy(LockedBits, Indices, IndexCount * 2u);
        Scratch->LockedBits = nullptr;
        Scratch->Gpu->Unlock();
    }
    Result = LockHr;
    if (Result < 0)
    {
        FdumpHr("copy_indices: %s", Result, 3887);
        return (GENRESULT)Result;
    }

    StreamCache_.SelectVertexStream((FRp8Vb*)Vb, 0);
    StreamCache_.SelectIndices((FRp8Ib*)&ScratchIbFlags_, BaseVertex);
    FlushDrawState(Device_);
    Result = Device_->DrawIndexedPrimitive((D3DPRIMITIVETYPE)(Prim), 0, NumVerts,
                                           LockOffset, PrimCount);
    if (Result < 0)
        FdumpHr("draw_indexed_primitive_vb: %s", Result, 3931);
    return (GENRESULT)Result;
}
GENRESULT DACOM_API FDirectX8::AddLight(void* Handle)
{
    // FUN_06d0ccb2 (RET 8): no device -> 0xb60, -1. Else FUN_06d2c487(this+0x21F4, Handle,
    // Handle) -- the handle is the caller's D3DLIGHT8 and doubles as the record's data, so a new
    // record is sent with SetLight at the next flush -- and 0.
    const GENRESULT Ready = NeedDeviceLifetime(
        "add_light() called outside of device lifetime", 0xb60);
    if (Ready != GR_OK)
        return Ready;
    LightManager_.FindOrCreate((unsigned long)(uintptr_t)Handle,
                      (const D3DLIGHT8*)(Handle));
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::RemoveLight(void* Handle)
{
    // FUN_06d0cd32 (RET 8): no device -> 0xb6c, -1. Else vtable +0xA4 set_light_enable(Handle,
    // 0) on this interface, the flush FUN_06d2d032(device), FUN_06d2c7cf(Handle); 0.
    const GENRESULT Ready = NeedDeviceLifetime(
        "remove_light() called outside of device lifetime", 0xb6c);
    if (Ready != GR_OK)
        return Ready;
    SetLightEnable(Handle, 0);
    LightManager_.Flush(Device_);
    LightManager_.Remove((unsigned long)(uintptr_t)Handle);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::UpdateLight(void* Handle)
{
    // FUN_06d0cddb (RET 8): no device -> 0xb7b, -1. Else FUN_06d2ca43(this+0x21F4, Handle) (the
    // record's data is resent at the next flush) and 0. The key is the same handle the other
    // light calls use.
    const GENRESULT Ready = NeedDeviceLifetime(
        "update_light() called outside of device lifetime", 0xb7b);
    if (Ready != GR_OK)
        return Ready;
    LightManager_.Update((unsigned long)(uintptr_t)Handle);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetWorldN(int WorldIndex, const float* M12)
{
    const GENRESULT Ready = NeedDeviceLifetime(
        "set_world_n() called outside of device lifetime", 2681);
    if (Ready != GR_OK)
        return Ready;
    // FUN_06d0af55 RET 0xC: unpack into the global DAT_06d70500 (a function-static here), then
    // return SetTransform (+0x94)(index + 0x100, it) as is. The matrix pointer is not tested.
    static float Scratch[16];
    Scratch[0] = M12[0];
    Scratch[4] = M12[1];
    Scratch[8] = M12[2];
    Scratch[1] = M12[3];
    Scratch[5] = M12[4];
    Scratch[9] = M12[5];
    Scratch[2] = M12[6];
    Scratch[6] = M12[7];
    Scratch[10] = M12[8];
    Scratch[12] = M12[9];
    Scratch[13] = M12[10];
    Scratch[14] = M12[11];
    Scratch[3] = 0.0f;
    Scratch[7] = 0.0f;
    Scratch[11] = 0.0f;
    Scratch[15] = 1.0f;
    const HRESULT Hr = Device_->SetTransform(
        (D3DTRANSFORMSTATETYPE)((unsigned long)WorldIndex + D3DTS_WORLDMATRIX(0)),
        (D3DMATRIX*)(Scratch));
    return (GENRESULT)Hr;
}

GENRESULT DACOM_API FDirectX8::VbMgrReleaseAll()
{
    // FUN_06d1135d RET 0x4, this = object+4. 06d11363: this+0x134 (= object+0x138, the
    // stream cache) = {0, 0, 0, -1, 0}.
    StreamCache_.Reset();
    // 06d1139f..06d114ab: walk the pool map (this+0x16c = object+0x170, its head) in key
    // order; every node value's VB is released (FUN_06d1bc60 inlined: Gpu set -> Locked ->
    // 0 + Unlock, Release, 0). The nodes themselves stay.
    std::map<unsigned long, FRp8Vb>& Pool = VbPool_.Value;
    for (std::map<unsigned long, FRp8Vb>::iterator It = Pool.begin(); It != Pool.end(); ++It)
        It->second.ReleaseGpu();
    // 06d114b0: this+0x17c (= object+0x180, pool byte total) = 0; return 0.
    VbPoolBytes_ = 0;
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::LockVertexMemory(unsigned int Fvf, unsigned int Count, FVertexMemoryLock* Out)
{
    FRp8Vb* Wrapper = FindOrInsertPoolVb(Fvf);
    GrowPoolVb(Wrapper, Fvf, Count);
    StreamCache_.SelectVertexStream(Wrapper, 0);
    unsigned int Offset = 0;
    const GENRESULT Result = LockVb(Wrapper, &Offset, &Out->Vertices, Count);
    if ((int)Result >= 0)
    {
        Out->Listing.BaseVertex = Offset;
        Out->Listing.VertexBuffer = Wrapper;
        Out->Listing.Fvf = Wrapper->Fvf;
    }
    else
        FdumpHr("lock_vertex_memory: %s", Result, 4018);
    return Result;
}
GENRESULT DACOM_API FDirectX8::UnlockVertexMemory(FVertexMemoryLock* Out)
{
    // FUN_06d11877 RET 0x8 (06d11877..06d118b9): node = *Out (Out itself is not tested);
    // node 0 -> 0xFFFFFFFE; else +0x10 = 0 and the result is Unlock's (+0x30) hr on +0x20.
    FRp8Vb* Wrapper = (FRp8Vb*)Out->Listing.VertexBuffer;
    if (Wrapper == nullptr)
        return (GENRESULT)0xFFFFFFFE;
    Wrapper->Locked = 0;
    return (GENRESULT)(Wrapper->Gpu->Unlock());
}
// formerly VbMgrSlot7
GENRESULT DACOM_API FDirectX8::UnsupportedManagerOperation()
{
    return (GENRESULT)0xFFFFFFF9;
}
namespace
{
    // Conversion callbacks read the canonical source-stream descriptor.

    typedef void (*RpVertexConvFn)(unsigned char* Dst, unsigned int DstFvf, const FRpVertexSrcDesc* Src,
                                   unsigned int StartVertex, unsigned int Count);

    unsigned int Rp8TexelByteSize(unsigned int Code)
    {
        switch (Code & 3)
        {
        case 0: return 8;
        case 1: return 0xC;
        case 2: return 0x10;
        default: return 4;
        }
    }

    // Mirrors FDirectX8::FvfStride's formula (verified identical against the same retail
    // stride computation, inlined 3x in the decompile: case3, FUN_06d21450, FUN_06d2230f).
    // Duplicated here (rather than calling the member function) so these free-function
    // converters don't need an FDirectX8 instance.
    unsigned int Rp8ComputeFvfStride(unsigned int Fvf)
    {
        unsigned int Stride = 0;
        if ((Fvf & 0xC) == 0xC)
            Stride = 0x1C;
        else if ((Fvf & 4) == 4)
            Stride = 0x10;
        else if ((Fvf & 2) == 2)
            Stride = 0xC;
        if ((Fvf & 0x10) != 0)
            Stride += 0xC;
        if ((Fvf & 0x40) != 0)
            Stride += 4;
        if ((Fvf & 0x80) != 0)
            Stride += 4;
        const unsigned int TexCount = (Fvf & 0xF00) >> 8;
        for (unsigned int Index = 0; Index < TexCount; ++Index)
            Stride += Rp8TexelByteSize(Fvf >> (Index * 2 + 0x10));
        return Stride;
    }

    // FUN_06d1f5fd — confirmed trivial empty thunk in retail (listing 47246-47252, 5 bytes);
    // a genuine intentional no-op jump-table target, not an unported stub.
    void ConvNoOp(unsigned char*, unsigned int, const FRpVertexSrcDesc*, unsigned int, unsigned int)
    {
    }

    // Copy sequential position/normal/UV channels using their fixed source strides.
    void ConvSeqPosNormTex1_S20(unsigned char* Dst, unsigned int, const FRpVertexSrcDesc* Src,
                                unsigned int StartVertex, unsigned int Count)
    {
        const unsigned char* PosPtr = Src->Position.Base + StartVertex * 0xCu;
        const unsigned char* NormPtr = Src->Normal.Base + StartVertex * 0xCu;
        const unsigned char* TexPtr = Src->Tex[0].Base + StartVertex * 8u;
        unsigned char* Out = Dst + StartVertex * 0x20u;
        for (unsigned int Index = 0; Index < Count; ++Index)
        {
            std::memcpy(Out, PosPtr, 0xC);
            std::memcpy(Out + 0xC, NormPtr, 0xC);
            std::memcpy(Out + 0x18, TexPtr, 8);
            Out += 0x20;
            PosPtr += 0xC;
            NormPtr += 0xC;
            TexPtr += 8;
        }
    }

    // Indexed converters retain fixed per-channel source strides.

    void ConvIndexedPosNorm_S18(unsigned char* Dst, unsigned int, const FRpVertexSrcDesc* Src,
                                 unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d1f602 — dest stride 0x18 (pos+normal, exact fit, no gap).
        const unsigned long* PosIdx = Src->Position.IndexArray + StartVertex;
        const unsigned long* NormIdx = Src->Normal.IndexArray + StartVertex;
        unsigned char* Out = Dst + StartVertex * 0x18u;
        for (unsigned int i = 0; i < Count; ++i, Out += 0x18, ++PosIdx, ++NormIdx)
        {
            std::memcpy(Out, Src->Position.Base + (*PosIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0xC, Src->Normal.Base + (*NormIdx) * 0xCu, 0xC);
        }
    }

    void ConvIndexedPosNorm_S28(unsigned char* Dst, unsigned int, const FRpVertexSrcDesc* Src,
                                 unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d1f818 — dest stride 0x28; only pos+normal (0x18 bytes) populated, bytes
        // [0x18,0x28) of every record are left untouched (matches retail exactly).
        const unsigned long* PosIdx = Src->Position.IndexArray + StartVertex;
        const unsigned long* NormIdx = Src->Normal.IndexArray + StartVertex;
        unsigned char* Out = Dst + StartVertex * 0x28u;
        for (unsigned int i = 0; i < Count; ++i, Out += 0x28, ++PosIdx, ++NormIdx)
        {
            std::memcpy(Out, Src->Position.Base + (*PosIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0xC, Src->Normal.Base + (*NormIdx) * 0xCu, 0xC);
        }
    }

    void ConvIndexedPosNorm_S2C(unsigned char* Dst, unsigned int, const FRpVertexSrcDesc* Src,
                                 unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d1fa33 — dest stride 0x2C; pos+normal populated, rest left untouched.
        const unsigned long* PosIdx = Src->Position.IndexArray + StartVertex;
        const unsigned long* NormIdx = Src->Normal.IndexArray + StartVertex;
        unsigned char* Out = Dst + StartVertex * 0x2Cu;
        for (unsigned int i = 0; i < Count; ++i, Out += 0x2C, ++PosIdx, ++NormIdx)
        {
            std::memcpy(Out, Src->Position.Base + (*PosIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0xC, Src->Normal.Base + (*NormIdx) * 0xCu, 0xC);
        }
    }

    void ConvIndexedPosNorm_S20(unsigned char* Dst, unsigned int, const FRpVertexSrcDesc* Src,
                                 unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d1fc57 — dest stride 0x20; pos+normal populated, rest left untouched.
        const unsigned long* PosIdx = Src->Position.IndexArray + StartVertex;
        const unsigned long* NormIdx = Src->Normal.IndexArray + StartVertex;
        unsigned char* Out = Dst + StartVertex * 0x20u;
        for (unsigned int i = 0; i < Count; ++i, Out += 0x20, ++PosIdx, ++NormIdx)
        {
            std::memcpy(Out, Src->Position.Base + (*PosIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0xC, Src->Normal.Base + (*NormIdx) * 0xCu, 0xC);
        }
    }

    void ConvIndexedPosNormTex1_S28(unsigned char* Dst, unsigned int, const FRpVertexSrcDesc* Src,
                                    unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d1fe6f — dest stride 0x28: pos[0x0], normal[0xC], tex0[0x18] (no gap).
        const unsigned long* PosIdx = Src->Position.IndexArray + StartVertex;
        const unsigned long* NormIdx = Src->Normal.IndexArray + StartVertex;
        const unsigned long* TexIdx = Src->Tex[0].IndexArray + StartVertex;
        unsigned char* Out = Dst + StartVertex * 0x28u;
        for (unsigned int i = 0; i < Count; ++i, Out += 0x28, ++PosIdx, ++NormIdx, ++TexIdx)
        {
            std::memcpy(Out, Src->Position.Base + (*PosIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0xC, Src->Normal.Base + (*NormIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0x18, Src->Tex[0].Base + (*TexIdx) * 8u, 8);
        }
    }

    void ConvIndexedPosNormTex1_S2C(unsigned char* Dst, unsigned int, const FRpVertexSrcDesc* Src,
                                    unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d20138 — dest stride 0x2C: pos[0x0], normal[0xC], 4-byte gap at [0x18,0x1C),
        // tex0[0x1C]. Bytes [0x24,0x2C) left untouched.
        const unsigned long* PosIdx = Src->Position.IndexArray + StartVertex;
        const unsigned long* NormIdx = Src->Normal.IndexArray + StartVertex;
        const unsigned long* TexIdx = Src->Tex[0].IndexArray + StartVertex;
        unsigned char* Out = Dst + StartVertex * 0x2Cu;
        for (unsigned int i = 0; i < Count; ++i, Out += 0x2C, ++PosIdx, ++NormIdx, ++TexIdx)
        {
            std::memcpy(Out, Src->Position.Base + (*PosIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0xC, Src->Normal.Base + (*NormIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0x1C, Src->Tex[0].Base + (*TexIdx) * 8u, 8);
        }
    }

    void ConvIndexedPosNormDiffTex1_S2C(unsigned char* Dst, unsigned int, const FRpVertexSrcDesc* Src,
                                        unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d20409 — dest stride 0x2C: pos[0x0], normal[0xC], diffuse[0x18] (1 dword),
        // tex0[0x1C]. Bytes [0x24,0x2C) left untouched.
        const unsigned long* PosIdx = Src->Position.IndexArray + StartVertex;
        const unsigned long* NormIdx = Src->Normal.IndexArray + StartVertex;
        const unsigned long* DiffIdx = Src->Diffuse.IndexArray + StartVertex;
        const unsigned long* TexIdx = Src->Tex[0].IndexArray + StartVertex;
        unsigned char* Out = Dst + StartVertex * 0x2Cu;
        for (unsigned int i = 0; i < Count; ++i, Out += 0x2C, ++PosIdx, ++NormIdx, ++DiffIdx, ++TexIdx)
        {
            std::memcpy(Out, Src->Position.Base + (*PosIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0xC, Src->Normal.Base + (*NormIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0x18, Src->Diffuse.Base + (*DiffIdx) * 4u, 4);
            std::memcpy(Out + 0x1C, Src->Tex[0].Base + (*TexIdx) * 8u, 8);
        }
    }

    void ConvIndexedPosNormTex2_S2C(unsigned char* Dst, unsigned int, const FRpVertexSrcDesc* Src,
                                    unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d20754 — dest stride 0x2C: pos[0x0], normal[0xC], 4-byte gap at [0x18,0x1C),
        // tex0[0x1C], tex1[0x24].
        const unsigned long* PosIdx = Src->Position.IndexArray + StartVertex;
        const unsigned long* NormIdx = Src->Normal.IndexArray + StartVertex;
        const unsigned long* Tex0Idx = Src->Tex[0].IndexArray + StartVertex;
        const unsigned long* Tex1Idx = Src->Tex[1].IndexArray + StartVertex;
        unsigned char* Out = Dst + StartVertex * 0x2Cu;
        for (unsigned int i = 0; i < Count; ++i, Out += 0x2C, ++PosIdx, ++NormIdx, ++Tex0Idx, ++Tex1Idx)
        {
            std::memcpy(Out, Src->Position.Base + (*PosIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0xC, Src->Normal.Base + (*NormIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0x1C, Src->Tex[0].Base + (*Tex0Idx) * 8u, 8);
            std::memcpy(Out + 0x24, Src->Tex[1].Base + (*Tex1Idx) * 8u, 8);
        }
    }

    void ConvIndexedPosNormTex1_S20(unsigned char* Dst, unsigned int, const FRpVertexSrcDesc* Src,
                                    unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d20ad2 — dest stride 0x20: pos[0x0], normal[0xC], tex0[0x18] (exact fit).
        const unsigned long* PosIdx = Src->Position.IndexArray + StartVertex;
        const unsigned long* NormIdx = Src->Normal.IndexArray + StartVertex;
        const unsigned long* TexIdx = Src->Tex[0].IndexArray + StartVertex;
        unsigned char* Out = Dst + StartVertex * 0x20u;
        for (unsigned int i = 0; i < Count; ++i, Out += 0x20, ++PosIdx, ++NormIdx, ++TexIdx)
        {
            std::memcpy(Out, Src->Position.Base + (*PosIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0xC, Src->Normal.Base + (*NormIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0x18, Src->Tex[0].Base + (*TexIdx) * 8u, 8);
        }
    }

    void ConvIndexedPosNormDiffTex1_S24(unsigned char* Dst, unsigned int, const FRpVertexSrcDesc* Src,
                                        unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d20d92 — dest stride 0x24: pos[0x0], normal[0xC], diffuse[0x18], tex0[0x1C]
        // (exact fit, no gap).
        const unsigned long* PosIdx = Src->Position.IndexArray + StartVertex;
        const unsigned long* NormIdx = Src->Normal.IndexArray + StartVertex;
        const unsigned long* DiffIdx = Src->Diffuse.IndexArray + StartVertex;
        const unsigned long* TexIdx = Src->Tex[0].IndexArray + StartVertex;
        unsigned char* Out = Dst + StartVertex * 0x24u;
        for (unsigned int i = 0; i < Count; ++i, Out += 0x24, ++PosIdx, ++NormIdx, ++DiffIdx, ++TexIdx)
        {
            std::memcpy(Out, Src->Position.Base + (*PosIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0xC, Src->Normal.Base + (*NormIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0x18, Src->Diffuse.Base + (*DiffIdx) * 4u, 4);
            std::memcpy(Out + 0x1C, Src->Tex[0].Base + (*TexIdx) * 8u, 8);
        }
    }

    void ConvIndexedPosNormTex2_S28(unsigned char* Dst, unsigned int, const FRpVertexSrcDesc* Src,
                                    unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d210dd — dest stride 0x28: pos[0x0], normal[0xC], tex0[0x18], tex1[0x20]
        // (exact fit, no gap).
        const unsigned long* PosIdx = Src->Position.IndexArray + StartVertex;
        const unsigned long* NormIdx = Src->Normal.IndexArray + StartVertex;
        const unsigned long* Tex0Idx = Src->Tex[0].IndexArray + StartVertex;
        const unsigned long* Tex1Idx = Src->Tex[1].IndexArray + StartVertex;
        unsigned char* Out = Dst + StartVertex * 0x28u;
        for (unsigned int i = 0; i < Count; ++i, Out += 0x28, ++PosIdx, ++NormIdx, ++Tex0Idx, ++Tex1Idx)
        {
            std::memcpy(Out, Src->Position.Base + (*PosIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0xC, Src->Normal.Base + (*NormIdx) * 0xCu, 0xC);
            std::memcpy(Out + 0x18, Src->Tex[0].Base + (*Tex0Idx) * 8u, 8);
            std::memcpy(Out + 0x20, Src->Tex[1].Base + (*Tex1Idx) * 8u, 8);
        }
    }

    // ---- Fixed-shape sequential (family-A) converters: source streams are already in
    // vertex order (no per-channel index arrays); source strides are again retail's literal
    // constants (0xC pos/normal, 8 per texcoord), advanced directly by StartVertex/vertex.

    void ConvSeqPosNormTex2_S2C(unsigned char* Dst, unsigned int, const FRpVertexSrcDesc* Src,
                                unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d21be9 — dest stride 0x2C: pos[0x0], normal[0xC], 4-byte gap at [0x18,0x1C),
        // tex0[0x1C], tex1[0x24].
        const unsigned char* PosPtr = Src->Position.Base + StartVertex * 0xCu;
        const unsigned char* NormPtr = Src->Normal.Base + StartVertex * 0xCu;
        const unsigned char* Tex0Ptr = Src->Tex[0].Base + StartVertex * 8u;
        const unsigned char* Tex1Ptr = Src->Tex[1].Base + StartVertex * 8u;
        unsigned char* Out = Dst + StartVertex * 0x2Cu;
        for (unsigned int i = 0; i < Count; ++i, Out += 0x2C, PosPtr += 0xC, NormPtr += 0xC,
                                 Tex0Ptr += 8, Tex1Ptr += 8)
        {
            std::memcpy(Out, PosPtr, 0xC);
            std::memcpy(Out + 0xC, NormPtr, 0xC);
            std::memcpy(Out + 0x1C, Tex0Ptr, 8);
            std::memcpy(Out + 0x24, Tex1Ptr, 8);
        }
    }

    void ConvSeqPosNormTex1_S2C(unsigned char* Dst, unsigned int, const FRpVertexSrcDesc* Src,
                                unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d21ea6 — dest stride 0x2C: pos[0x0], normal[0xC], 4-byte gap at [0x18,0x1C),
        // tex0[0x1C]. Bytes [0x24,0x2C) left untouched (only 1 of 2 possible texcoord slots
        // populated here).
        const unsigned char* PosPtr = Src->Position.Base + StartVertex * 0xCu;
        const unsigned char* NormPtr = Src->Normal.Base + StartVertex * 0xCu;
        const unsigned char* TexPtr = Src->Tex[0].Base + StartVertex * 8u;
        unsigned char* Out = Dst + StartVertex * 0x2Cu;
        for (unsigned int i = 0; i < Count; ++i, Out += 0x2C, PosPtr += 0xC, NormPtr += 0xC, TexPtr += 8)
        {
            std::memcpy(Out, PosPtr, 0xC);
            std::memcpy(Out + 0xC, NormPtr, 0xC);
            std::memcpy(Out + 0x1C, TexPtr, 8);
        }
    }

    void ConvSeqPosNormTex1_S28(unsigned char* Dst, unsigned int, const FRpVertexSrcDesc* Src,
                                unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d220df — dest stride 0x28: pos[0x0], normal[0xC], tex0[0x18] (no gap). Bytes
        // [0x20,0x28) left untouched.
        const unsigned char* PosPtr = Src->Position.Base + StartVertex * 0xCu;
        const unsigned char* NormPtr = Src->Normal.Base + StartVertex * 0xCu;
        const unsigned char* TexPtr = Src->Tex[0].Base + StartVertex * 8u;
        unsigned char* Out = Dst + StartVertex * 0x28u;
        for (unsigned int i = 0; i < Count; ++i, Out += 0x28, PosPtr += 0xC, NormPtr += 0xC, TexPtr += 8)
        {
            std::memcpy(Out, PosPtr, 0xC);
            std::memcpy(Out + 0xC, NormPtr, 0xC);
            std::memcpy(Out + 0x18, TexPtr, 8);
        }
    }

    // DAT_06d70548 / DAT_06d70648: the static buffers the generic converters sprintf their
    // "SLOOOW" message into (0x100 apart in .data; nothing reads them back).
    char G_ConvSeqMessage[0x100];
    char G_ConvIndexedMessage[0x100];

    // Generic conversions retain the unchecked byte walk for texture channels.

    void ConvIndexedGeneric(unsigned char* Dst, unsigned int DstFvf, const FRpVertexSrcDesc* Src,
                            unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d21450 — family-B (indexed) generic FVF conversion fallback. 06d2145b: the
        // message only goes into the static buffer DAT_06d70648 (sprintf, nothing prints it).
        sprintf(G_ConvIndexedMessage,
                "VertexBufferManager: Using SLOOOW indexed copy. src_fmt=0x%x dst_fmt=0x%x\n",
                Src->SrcFvf, DstFvf);

        const unsigned char* Base = (const unsigned char*)(Src);
        const unsigned int SrcFvf = Src->SrcFvf;

        const unsigned int PosStride = Src->Position.Stride;
        const unsigned long* PosIdx = Src->Position.IndexArray + StartVertex;

        const bool bSrcHasNormal = (SrcFvf & 0x10) != 0;
        const unsigned int NormStride = Src->Normal.Stride;
        const unsigned long* NormIdx = Src->Normal.IndexArray + StartVertex;

        const bool bSrcHasDiffuse = (SrcFvf & 0x40) != 0;
        const unsigned int DiffStride = Src->Diffuse.Stride;
        const unsigned long* DiffIdx = Src->Diffuse.IndexArray + StartVertex;

        const unsigned int SrcTexCount = (SrcFvf & 0xF00) >> 8;
        const unsigned char* TexBase[8] = {};
        unsigned int TexStride[8] = {};
        unsigned int TexByteSize[8] = {};
        const unsigned long* TexIdx[8] = {};
        for (unsigned int Index = 0; Index < SrcTexCount && Index < 8; ++Index)
        {
            const FRpVertexStream* Set = (const FRpVertexStream*)(Base + 0x40 + Index * 0x14);
            TexBase[Index] = Set->Base;
            TexByteSize[Index] = Set->ByteSize;
            TexStride[Index] = Set->Stride;
            TexIdx[Index] = Set->IndexArray + StartVertex;
        }

        const bool bDstHasNormal = (DstFvf & 0x10) != 0;
        const bool bDstHasDiffuse = (DstFvf & 0x40) != 0;
        const unsigned int DstTexCount = (DstFvf & 0xF00) >> 8;
        unsigned int DstTexByteSize[8] = {};
        for (unsigned int Index = 0; Index < DstTexCount && Index < 8; ++Index)
            DstTexByteSize[Index] = Rp8TexelByteSize(DstFvf >> (Index * 2 + 0x10));

        const unsigned int DstStride = Rp8ComputeFvfStride(DstFvf);

        for (unsigned int VertexIndex = 0; VertexIndex < Count; ++VertexIndex)
        {
            unsigned char* Out = Dst + (StartVertex + VertexIndex) * DstStride;

            std::memcpy(Out, Src->Position.Base + (*PosIdx) * PosStride, 0xC);
            ++PosIdx;
            unsigned char* Cursor = Out + 0xC;

            if (bDstHasNormal)
            {
                if (bSrcHasNormal)
                {
                    std::memcpy(Cursor, Src->Normal.Base + (*NormIdx) * NormStride, 0xC);
                    ++NormIdx;
                }
                Cursor += 0xC;
            }
            if (bDstHasDiffuse)
            {
                if (bSrcHasDiffuse)
                {
                    std::memcpy(Cursor, Src->Diffuse.Base + (*DiffIdx) * DiffStride, 4);
                    ++DiffIdx;
                }
                Cursor += 4;
            }
            for (unsigned int Index = 0; Index < DstTexCount && Index < 8; ++Index)
            {
                if (Index < SrcTexCount)
                {
                    std::memcpy(Cursor, TexBase[Index] + (*TexIdx[Index]) * TexStride[Index], TexByteSize[Index]);
                    ++TexIdx[Index];
                }
                Cursor += DstTexByteSize[Index];
            }
        }
    }

    void ConvSeqGeneric(unsigned char* Dst, unsigned int DstFvf, const FRpVertexSrcDesc* Src,
                        unsigned int StartVertex, unsigned int Count)
    {
        // FUN_06d2230f — family-A (sequential) generic FVF conversion fallback. 06d2231a: the
        // message only goes into the static buffer DAT_06d70548 (sprintf, nothing prints it).
        sprintf(G_ConvSeqMessage,
                "VertexBufferManager: Using SLOOOW unindexed copy. src_fmt=0x%x dst_fmt=0x%x\n",
                Src->SrcFvf, DstFvf);

        const unsigned char* Base = (const unsigned char*)(Src);
        const unsigned int SrcFvf = Src->SrcFvf;

        const unsigned int PosStride = Src->Position.Stride;
        const unsigned char* PosPtr = Src->Position.Base + StartVertex * PosStride;

        const bool bSrcHasNormal = (SrcFvf & 0x10) != 0;
        const unsigned int NormStride = Src->Normal.Stride;
        const unsigned char* NormPtr = Src->Normal.Base + StartVertex * NormStride;

        const bool bSrcHasDiffuse = (SrcFvf & 0x40) != 0;
        const unsigned int DiffStride = Src->Diffuse.Stride;
        const unsigned char* DiffPtr = Src->Diffuse.Base + StartVertex * DiffStride;

        const unsigned int SrcTexCount = (SrcFvf & 0xF00) >> 8;
        const unsigned char* TexPtr[8] = {};
        unsigned int TexStride[8] = {};
        unsigned int TexByteSize[8] = {};
        for (unsigned int Index = 0; Index < SrcTexCount && Index < 8; ++Index)
        {
            const FRpVertexStream* Set = (const FRpVertexStream*)(Base + 0x40 + Index * 0x14);
            TexByteSize[Index] = Set->ByteSize;
            TexStride[Index] = Set->Stride;
            const unsigned char* SetBase = Set->Base;
            TexPtr[Index] = SetBase + StartVertex * TexStride[Index];
        }

        const bool bDstHasNormal = (DstFvf & 0x10) != 0;
        const bool bDstHasDiffuse = (DstFvf & 0x40) != 0;
        const unsigned int DstTexCount = (DstFvf & 0xF00) >> 8;
        unsigned int DstTexByteSize[8] = {};
        for (unsigned int Index = 0; Index < DstTexCount && Index < 8; ++Index)
            DstTexByteSize[Index] = Rp8TexelByteSize(DstFvf >> (Index * 2 + 0x10));

        const unsigned int DstStride = Rp8ComputeFvfStride(DstFvf);

        for (unsigned int VertexIndex = 0; VertexIndex < Count; ++VertexIndex)
        {
            unsigned char* Out = Dst + (StartVertex + VertexIndex) * DstStride;

            std::memcpy(Out, PosPtr, 0xC);
            PosPtr += PosStride;
            unsigned char* Cursor = Out + 0xC;

            if (bDstHasNormal)
            {
                if (bSrcHasNormal)
                {
                    std::memcpy(Cursor, NormPtr, 0xC);
                    NormPtr += NormStride;
                }
                Cursor += 0xC;
            }
            if (bDstHasDiffuse)
            {
                if (bSrcHasDiffuse)
                {
                    std::memcpy(Cursor, DiffPtr, 4);
                    DiffPtr += DiffStride;
                }
                Cursor += 4;
            }
            for (unsigned int Index = 0; Index < DstTexCount && Index < 8; ++Index)
            {
                if (Index < SrcTexCount)
                {
                    std::memcpy(Cursor, TexPtr[Index], TexByteSize[Index]);
                    TexPtr[Index] += TexStride[Index];
                }
                Cursor += DstTexByteSize[Index];
            }
        }
    }

    // Retail's two 0x41-entry jump tables (data 0x06d6ba60 / 0x06d6b858), extracted byte-for-
    // byte from the .txt listing (every 8-byte slot's first dword). Index 0x40 in each is a
    // distinct fixed "3+ texcoords, non-direct" fallback slot IDA emitted as a standalone
    // `addr` (not part of the byte-scanned array) — decoded the same way.
    const RpVertexConvFn kFamilyASeqJumpTable[0x41] = {
        ConvSeqGeneric, ConvNoOp, ConvSeqGeneric, ConvNoOp,
        ConvSeqGeneric, ConvNoOp, ConvSeqGeneric, ConvNoOp,
        ConvNoOp, ConvNoOp, ConvNoOp, ConvNoOp,
        ConvSeqGeneric, ConvNoOp, ConvSeqGeneric, ConvNoOp,
        ConvSeqGeneric, ConvNoOp, ConvSeqPosNormTex1_S20, ConvNoOp,
        ConvSeqGeneric, ConvNoOp, ConvSeqGeneric, ConvNoOp,
        ConvNoOp, ConvNoOp, ConvNoOp, ConvNoOp,
        ConvSeqGeneric, ConvNoOp, ConvSeqGeneric, ConvNoOp,
        ConvNoOp, ConvNoOp, ConvNoOp, ConvNoOp,
        ConvNoOp, ConvNoOp, ConvNoOp, ConvNoOp,
        ConvNoOp, ConvNoOp, ConvNoOp, ConvNoOp,
        ConvNoOp, ConvNoOp, ConvNoOp, ConvNoOp,
        ConvSeqGeneric, ConvNoOp, ConvSeqGeneric, ConvNoOp,
        ConvSeqGeneric, ConvNoOp, ConvSeqGeneric, ConvNoOp,
        ConvNoOp, ConvNoOp, ConvNoOp, ConvNoOp,
        ConvSeqGeneric, ConvNoOp, ConvSeqGeneric, ConvNoOp,
        ConvSeqGeneric,
    };

    const RpVertexConvFn kFamilyBIndexedJumpTable[0x41] = {
        ConvIndexedGeneric, ConvNoOp, ConvIndexedPosNorm_S18, ConvNoOp,
        ConvIndexedGeneric, ConvNoOp, ConvIndexedGeneric, ConvNoOp,
        ConvNoOp, ConvNoOp, ConvNoOp, ConvNoOp,
        ConvIndexedGeneric, ConvNoOp, ConvIndexedGeneric, ConvNoOp,
        ConvIndexedGeneric, ConvNoOp, ConvIndexedPosNormTex1_S20, ConvNoOp,
        ConvIndexedGeneric, ConvNoOp, ConvIndexedPosNormDiffTex1_S24, ConvNoOp,
        ConvNoOp, ConvNoOp, ConvNoOp, ConvNoOp,
        ConvIndexedGeneric, ConvNoOp, ConvIndexedGeneric, ConvNoOp,
        ConvNoOp, ConvNoOp, ConvNoOp, ConvNoOp,
        ConvNoOp, ConvNoOp, ConvNoOp, ConvNoOp,
        ConvNoOp, ConvNoOp, ConvNoOp, ConvNoOp,
        ConvNoOp, ConvNoOp, ConvNoOp, ConvNoOp,
        ConvIndexedGeneric, ConvNoOp, ConvIndexedPosNormTex2_S28, ConvNoOp,
        ConvIndexedGeneric, ConvNoOp, ConvIndexedGeneric, ConvNoOp,
        ConvNoOp, ConvNoOp, ConvNoOp, ConvNoOp,
        ConvIndexedGeneric, ConvNoOp, ConvIndexedGeneric, ConvNoOp,
        ConvIndexedGeneric,
    };

    unsigned int Rp8FvfJumpIndex(unsigned int Fvf)
    {
        // Retail's local_8 formula (identical in both case1 and case5, decomp 14463-14474 /
        // 14574-14585).
        unsigned int TexBucket = (Fvf >> 8) & 3;
        unsigned int High = TexBucket << 4;
        if (TexBucket == 2)
            High = 0x30;
        return High | (((Fvf >> 6) & 3) << 2) | (((Fvf >> 4) & 1) << 1) | ((Fvf >> 2) & 1);
    }

    unsigned int Rp8JumpTableIndex(unsigned int DstFvf)
    {
        if (((DstFvf & 0xF00) >> 8) < 3 || (DstFvf & 4) != 0)
            return Rp8FvfJumpIndex(DstFvf);
        return 0x40u;
    }

    // FUN_06d1f040 case 4's nested if/goto chain (decomp 14531-14572), flattened into an
    // equivalent truth table by manual control-flow trace (every fallthrough/goto target
    // walked by hand; see notes on FUN_06d1f040 in decomp-tracker for the full derivation).
    void DispatchFamilyBHardcoded(unsigned char* Dst, unsigned int DstFvf, const FRpVertexSrcDesc* Src,
                                  unsigned int StartVertex, unsigned int Count, unsigned int SrcFvf)
    {
        if (SrcFvf == 0x12)
        {
            if (DstFvf == 0x252) { ConvIndexedPosNorm_S2C(Dst, DstFvf, Src, StartVertex, Count); return; }
            if (DstFvf == 0x212) { ConvIndexedPosNorm_S28(Dst, DstFvf, Src, StartVertex, Count); return; }
            if (DstFvf == 0x112) { ConvIndexedPosNorm_S20(Dst, DstFvf, Src, StartVertex, Count); return; }
        }
        else if (SrcFvf == 0x112)
        {
            if (DstFvf == 0x252) { ConvIndexedPosNormTex1_S2C(Dst, DstFvf, Src, StartVertex, Count); return; }
            if (DstFvf == 0x212) { ConvIndexedPosNormTex1_S28(Dst, DstFvf, Src, StartVertex, Count); return; }
        }
        else if (SrcFvf == 0x152)
        {
            if (DstFvf == 0x252) { ConvIndexedPosNormDiffTex1_S2C(Dst, DstFvf, Src, StartVertex, Count); return; }
        }
        else if (SrcFvf == 0x212)
        {
            if (DstFvf == 0x252) { ConvIndexedPosNormTex2_S2C(Dst, DstFvf, Src, StartVertex, Count); return; }
        }
        ConvIndexedGeneric(Dst, DstFvf, Src, StartVertex, Count);
    }

    // FUN_06d1f040 case 0's nested if/goto chain (decomp 14442-14461), flattened the same way.
    void DispatchFamilyASeqHardcoded(unsigned char* Dst, unsigned int DstFvf, const FRpVertexSrcDesc* Src,
                                     unsigned int StartVertex, unsigned int Count, unsigned int SrcFvf)
    {
        if (SrcFvf == 0x112)
        {
            if (DstFvf == 0x252) { ConvSeqPosNormTex1_S2C(Dst, DstFvf, Src, StartVertex, Count); return; }
            if (DstFvf == 0x212) { ConvSeqPosNormTex1_S28(Dst, DstFvf, Src, StartVertex, Count); return; }
        }
        else if (SrcFvf == 0x212 && DstFvf == 0x252)
        {
            ConvSeqPosNormTex2_S2C(Dst, DstFvf, Src, StartVertex, Count);
            return;
        }
        ConvSeqGeneric(Dst, DstFvf, Src, StartVertex, Count);
    }
} // namespace

GENRESULT DACOM_API FDirectX8::CopyVertexData(void* Destination, unsigned int DstFvf,
                                           const FRpVertexSrcDesc* Src,
                                           unsigned int StartVertex, unsigned int Count)
{
    // Retail FUN_06d114c5 calls FUN_06d1f040(A, B, C, D, E) directly and always returns 0;
    // FUN_06d1f040 is the real FVF (Flexible Vertex Format) mismatch-conversion dispatcher
    // (decomp 14426-14591 / listing 46629-47244). See the namespace block above for the full
    // sub-converter cluster (FUN_06d1f5fd, FUN_06d1f602..FUN_06d2230f) this now dispatches to.
    unsigned char* Dst = (unsigned char*)Destination;

    // 06d1f048..06d1f06c: the selector is (Src->SrcFvf == DstFvf) | (Src->Flags & 6), read
    // straight off the descriptor with no test of Dst, Src or Count (a zero Count reaches the
    // converters, which then copy nothing).
    const unsigned int SrcFvf = Src->SrcFvf;
    const unsigned int Selector = (unsigned int)(SrcFvf == DstFvf) | (Src->Flags & 6u);

    switch (Selector)
    {
    case 0:
        DispatchFamilyASeqHardcoded(Dst, DstFvf, Src, StartVertex, Count, SrcFvf);
        break;
    case 1:
        kFamilyASeqJumpTable[Rp8JumpTableIndex(DstFvf)](Dst, DstFvf, Src, StartVertex, Count);
        break;
    case 2:
        ConvSeqGeneric(Dst, DstFvf, Src, StartVertex, Count);
        break;
    case 3:
    {
        // Direct/interleaved source, matching layout: byte-for-byte copy (already the proven
        // fast path used elsewhere in this file, e.g. CopyVertices's memcpy fallback).
        const unsigned int Stride = FvfStride(DstFvf);
        const unsigned char* SrcBuf = Src->Position.Base + StartVertex * Src->Position.Stride;
        std::memcpy(Dst + StartVertex * Stride, SrcBuf, (size_t)(Stride) * Count);
        break;
    }
    case 4:
        DispatchFamilyBHardcoded(Dst, DstFvf, Src, StartVertex, Count, SrcFvf);
        break;
    case 5:
        kFamilyBIndexedJumpTable[Rp8JumpTableIndex(DstFvf)](Dst, DstFvf, Src, StartVertex, Count);
        break;
    case 7:
        ConvIndexedGeneric(Dst, DstFvf, Src, StartVertex, Count);
        break;
    default:
        // case 6 (Flags&6==6, SrcFvf!=DstFvf): retail's switch has no case for this value —
        // confirmed by re-reading the decompiled switch (decomp 14442-14589): control falls
        // straight through to the function's final `return` with no work done. A genuine
        // retail no-op, faithfully preserved (not a porting gap).
        break;
    }
    return GR_OK;
}

GENRESULT DACOM_API FDirectX8::CreateIb(unsigned int Count, unsigned int** Out,
                                        unsigned int Flags)
{
    // FUN_06d12d4e RET 0x10 (sub = object+0xC).
    unsigned char* IbThis = (unsigned char*)((IRPIndexBuffer*)(this));
    IDirect3DDevice8* Device = Device_;
    if (Device == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 4347, "WARNING",
                 "create_index_buffer() called outside of device lifetime");
        return GR_GENERIC;
    }
    // 06d12dca: usage = (flags&1 ? D3DUSAGE_DYNAMIC 0x200 : 0) | (flags&2 ? 0x10 : 0).
    unsigned long Usage = 0;
    if ((Flags & 1) != 0)
        Usage |= 0x200;
    if ((Flags & 2) != 0)
        Usage |= 0x10;
    // 06d12dfa: operator new(0x1c); Flags = usage|8 (WRITEONLY); +4,+8,+0xc,+0x10,+0x18 = 0.
    // +0x14 (Cursor) is not written here; ResizeIb below zeroes it before anyone reads it.
    FRp8Ib* Wrapper = new FRp8Ib;
    Wrapper->Flags = Usage | 8;
    Wrapper->Gpu = nullptr;
    Wrapper->LockedBits = nullptr;
    Wrapper->Next = nullptr;
    Wrapper->Prev = nullptr;
    Wrapper->Capacity = 0;
    // 06d12e86: append to the IB list (sub+0x22e4 = object+0x22F0).
    FRp8IbList* List = (FRp8IbList*)(IbThis - 0xC + kOffIbList);
    FRp8Ib* OldLast = List->Last;
    List->Last = Wrapper;
    if (OldLast == nullptr)
        List->First = Wrapper;
    else
        OldLast->Next = Wrapper;
    Wrapper->Next = nullptr;
    Wrapper->Prev = OldLast;
    List->Count += 1;
    // 06d12f5b: hr = this->vtbl[0x14](node, count) = resize.
    const GENRESULT Hr = ResizeIb(Wrapper, Count);
    if ((HRESULT)Hr < 0)
        FdumpHr("create_ib: %s", (HRESULT)Hr, 4363);
    // 06d12ff1: *Out = node even when resize failed (the node stays in the list).
    *Out = (unsigned int*)(Wrapper);
    return Hr;
}
// FUN_06d13002 RET 0x8 -- pointer to the caller's handle variable.
GENRESULT DACOM_API FDirectX8::DestroyIb(void** Handle)
{
    unsigned char* IbThis = (unsigned char*)((IRPIndexBuffer*)(this));
    IDirect3DDevice8* Device = Device_;
    // FUN_06d13002: CMP [this+0x124],0 -> FDUMP line 0x1114, -1.
    if (Device == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 4372, "WARNING",
                 "destroy_index_buffer() called outside of device lifetime");
        return GR_GENERIC;
    }
    // 06d13069: result = -1; *H == -1 -> return -1; node = *H; *H = 0; node == 0 -> -1.
    if (*Handle == (void*)((intptr_t)-1))
        return GR_GENERIC;
    FRp8Ib* Wrapper = (FRp8Ib*)(*Handle);
    *Handle = nullptr;
    if (Wrapper == nullptr)
        return GR_GENERIC;
    // 06d13097: node == [sub+0x134] (the selected IB) -> this->vtbl[0x24](0,0,0,0) = select_ib.
    if (Wrapper == StreamCache_.IndexBuffer)
        SelectIb(nullptr, 0, 0, 0);
    // 06d130b9: unlink from the IB list (next +0xc, prev +0x10), count--.
    FRp8IbList* List = (FRp8IbList*)(IbThis - 0xC + kOffIbList);
    if (Wrapper->Prev == nullptr)
        List->First = Wrapper->Next;
    else
        Wrapper->Prev->Next = Wrapper->Next;
    if (Wrapper->Next == nullptr)
        List->Last = Wrapper->Prev;
    else
        Wrapper->Next->Prev = Wrapper->Prev;
    List->Count -= 1;
    // 06d1312b: release (unlock first if +8 is set), operator delete; result = 0.
    ReleaseIbGpu(Wrapper);
    delete Wrapper;
    return GR_OK;
}
// FUN_06d131b2 RET 0xC.
GENRESULT DACOM_API FDirectX8::ResizeIb(void* A, unsigned int Count)
{
    FRp8Ib* Wrapper = (FRp8Ib*)(A);
    IDirect3DDevice8* Device = Device_;
    if (Device == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 4401, "WARNING", "resize() called outside of device lifetime");
        return GR_GENERIC;
    }
    // 06d13220: node == -1 or 0 -> 0x80004005.
    if (Wrapper == nullptr || Wrapper == (FRp8Ib*)((intptr_t)-1))
        return (GENRESULT)0x80004005;
    // 06d1325a: a live buffer of the same capacity (+0x18) -> 0.
    if (Wrapper->Gpu != nullptr && Count == Wrapper->Capacity)
        return GR_OK;
    // 06d1327b / 06d133a2: release (unlock if +8), +0x18 = count, +0x14 = 0,
    // CreateIndexBuffer(+0x60)(count*2, node Flags, D3DFMT_INDEX16 0x65, D3DPOOL_DEFAULT,
    // &node->Gpu); hr < 0 -> dx8_index_buffer.h:0x31 text; return hr.
    ReleaseIbGpu(Wrapper);
    Wrapper->Capacity = Count;
    Wrapper->Cursor = 0;
    const unsigned int Bytes = Count * 2;
    const HRESULT Hr = Device->CreateIndexBuffer(Bytes, Wrapper->Flags, D3DFMT_INDEX16,
                                                 D3DPOOL_DEFAULT, &Wrapper->Gpu);
    if (Hr < 0)
        FdumpIbCreate(Hr, Bytes);
    return (GENRESULT)Hr;
}
GENRESULT DACOM_API FDirectX8::CopyIndices(void* Handle, unsigned int* OutOffset, const unsigned short* SrcData,
                                           unsigned int Count)
{
    // FUN_06d13847: Handle==-1 → wrapper = this+0x140 (inline scratch). Ring cursor when
    // Flags&0x200; memcpy into lock; clear LockedBits; Unlock; write start index to OutOffset.
    unsigned char* IbThis = (unsigned char*)((IRPIndexBuffer*)(this));
    IDirect3DDevice8* Device = Device_;
    if (Device == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 4472, "WARNING", "copy_indices() called outside of device lifetime");
        return GR_GENERIC;
    }
    FRp8Ib* Wrapper = ResolveIbHandle(IbThis, Handle);
    if (Wrapper == nullptr)
        return (GENRESULT)0x80004005;
    unsigned int* InOutStart = OutOffset;
    void* LockedBits = nullptr;
    unsigned int Start = 0;
    const HRESULT Hr = Wrapper->LockRange(*InOutStart, Count, &LockedBits, &Start,
                                   (unsigned char)((IbThis[0x11C] >> 1) & 1));
    if (Hr >= 0)
    {
        memcpy(LockedBits, SrcData, Count * 2u);
        Wrapper->LockedBits = nullptr;
        Wrapper->Gpu->Unlock();
    }
    if (Hr < 0)
        FdumpHr("copy_indices: %s", Hr, 4488);
    if (Hr >= 0)
        *InOutStart = Start;
    return (GENRESULT)Hr;
}
GENRESULT DACOM_API FDirectX8::LockIb(void* Handle, unsigned int* Offset, void** Out,
                                      unsigned int Count)
{
    // FUN_06d134ec: Handle==-1 → ADD ECX,0x140 (address of inline FRp8Ib @ object+0x14C).
    unsigned char* IbThis = (unsigned char*)((IRPIndexBuffer*)(this));
    IDirect3DDevice8* Device = Device_;
    if (Device == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 4419, "WARNING", "lock_ib() called outside of device lifetime");
        return GR_GENERIC;
    }
    // 06d1355a..06d1378b: node == 0 -> E_FAIL (no dump); start = Offset ? *Offset : 0; the
    // body is FUN_06d1c660 inlined (LockIbRange, bSysLock = [this+0x11C] bit1, Out not tested);
    // any failure, including Gpu == 0 and Count > Capacity, dumps "ib_lock: %s" at 0x115a.
    FRp8Ib* Wrapper = ResolveIbHandle(IbThis, Handle);
    if (Wrapper == nullptr)
        return (GENRESULT)0x80004005;
    unsigned int Start = 0;
    if (Offset != nullptr)
        Start = *Offset;
    const HRESULT Hr = Wrapper->LockRange(Start, Count, Out, Offset,
                                   (unsigned char)((IbThis[0x11C] >> 1) & 1));
    if (Hr < 0)
        FdumpHr("ib_lock: %s", Hr, 4442);
    return (GENRESULT)Hr;
}
GENRESULT DACOM_API FDirectX8::UnlockIb(void* Handle)
{
    // FUN_06d13794: Handle==-1 → this+0x140; zero LockedBits; Gpu->Unlock.
    unsigned char* IbThis = (unsigned char*)((IRPIndexBuffer*)(this));
    IDirect3DDevice8* Device = Device_;
    if (Device == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 4450, "WARNING", "unlock_ib() called outside of device lifetime");
        return GR_GENERIC;
    }
    // 06d137f8: Handle == 0 -> E_FAIL; node->LockedBits = 0 and node->Gpu->Unlock (+0x30)
    // with no Gpu test; returns the Unlock hr.
    if (Handle == nullptr)
        return (GENRESULT)0x80004005;
    FRp8Ib* Wrapper = ResolveIbHandle(IbThis, Handle);
    Wrapper->LockedBits = nullptr;
    const HRESULT Hr = Wrapper->Gpu->Unlock();
    return (GENRESULT)Hr;
}
GENRESULT DACOM_API FDirectX8::SelectIb(void* Handle, unsigned int BaseVertexIndex,
                                        unsigned int /*Unused3*/, unsigned int /*Unused4*/)
{
    // FUN_06d13bce RET 0x14. Handle==-1 → ADDRESS this+0x140 (inline scratch). Retail only
    // marks the stream block (flags bit1); FUN_06d04138 calls SetIndices at the next draw.
    unsigned char* IbThis = (unsigned char*)((IRPIndexBuffer*)(this));
    const unsigned long Ready = *(unsigned long*)(IbThis + 0x178);
    // 06d13bd7: MOV CL,byte [this+0x178]; AND 1 -- the device pointer is not read at all.
    if ((Ready & 1) == 0)
    {
        Rp8Fdump(kFdumpWarning, 4520, "WARNING",
                 "select_ib() called outside of create_buffers/destroy_buffers");
        return GR_GENERIC;
    }

    FRp8StreamCache& Cache = StreamCache_;
    const unsigned int BaseVertex = BaseVertexIndex;
    if (Handle == nullptr)
    {
        FRp8Ib* PrevIb = Cache.IndexBuffer;
        const unsigned int PrevBase = Cache.BaseVertex;
        if (PrevIb != nullptr || PrevBase != 0)
        {
            Cache.Flags.Dirty = (unsigned char)(Cache.Flags.Dirty | StreamDirtyIndices);
            Cache.IndexBuffer = nullptr;
            Cache.BaseVertex = 0;
        }
        return (GENRESULT)0x80004005;
    }

    FRp8Ib* Wrapper = ResolveIbHandle(IbThis, Handle);
    if (Wrapper == nullptr || Wrapper->Gpu == nullptr)
        return (GENRESULT)0x80004005;

    FRp8Ib* Current = Cache.IndexBuffer;
    const unsigned int CurrentBase = Cache.BaseVertex;
    if (Wrapper != Current || BaseVertex != CurrentBase)
    {
        Cache.Flags.Dirty = (unsigned char)(Cache.Flags.Dirty | StreamDirtyIndices);
        Cache.IndexBuffer = Wrapper;
        Cache.BaseVertex = BaseVertex;
    }
    return GR_OK;
}
// FUN_06d13d23 RET 0xC. Handle==-1 → ADDRESS this+0x140; write Capacity through out-param.
GENRESULT DACOM_API FDirectX8::GetIbCapacity(void* A, unsigned int* OutCapacity)
{
    unsigned char* IbThis = (unsigned char*)((IRPIndexBuffer*)(this));
    FRp8Ib* Wrapper = ResolveIbHandle(IbThis, A);
    if (Wrapper == nullptr || OutCapacity == nullptr)
        return (GENRESULT)0x80004005;
    *OutCapacity = Wrapper->Capacity;
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::IsIbValid(void* A)
{
    // FUN_06d13b69: null → 0; -1 → ADDRESS this+0x140; return 1 iff Gpu != null.
    if (A == nullptr)
        return GR_OK;
    unsigned char* IbThis = (unsigned char*)((IRPIndexBuffer*)(this));
    FRp8Ib* Wrapper = ResolveIbHandle(IbThis, A);
    if (Wrapper != nullptr && Wrapper->Gpu != nullptr)
        return (GENRESULT)1;
    return GR_OK;
}

GENRESULT DACOM_API FDirectX8::CreateVb(unsigned int Fvf, unsigned int Count, unsigned int** Out,
                                        unsigned int Flags)
{
    IDirect3DDevice8* Device = Device_;
    if (Device == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 4048, "WARNING", "create_vb() called outside of device lifetime");
        return GR_GENERIC;
    }
    // 06d11942: flags & 1 -> the pooled buffer for Fvf (06d11950..06d11bf7): find-or-insert in
    // the pool map, the inlined grow (select_vb(0) only when it grew the selected node),
    // FUN_06d1c770(+0x138, node, 0), hr = 0, *Out = node.
    if ((Flags & 1) != 0)
    {
        FRp8Vb* Wrapper = FindOrInsertPoolVb(Fvf);
        GrowPoolVb(Wrapper, Fvf, Count);
        StreamCache_.SelectVertexStream(Wrapper, 0);
        *Out = (unsigned int*)(Wrapper);
        return GR_OK;
    }
    // 06d11bfc: usage = (flags&2 ? 0x10 : 0); operator new(0x24); Flags = usage|8
    // (WRITEONLY); +4,+8,+0xc,+0x10,+0x14,+0x18,+0x20 = 0 (+0x1c left as allocated; ResizeVb
    // zeroes it before use).
    unsigned long Usage = 0;
    if ((Flags & 2) != 0)
        Usage |= 0x10;
    FRp8Vb* Wrapper = new FRp8Vb;
    Wrapper->Flags = Usage | 8;
    Wrapper->Fvf = 0;
    Wrapper->Count = 0;
    Wrapper->Used = 0;
    Wrapper->Locked = 0;
    Wrapper->Next = nullptr;
    Wrapper->Prev = nullptr;
    Wrapper->Gpu = nullptr;
    // 06d11cbc: append to the VB list (sub+0x22d4 = object+0x22E4).
    FRp8VbList* List = (FRp8VbList*)(
        (unsigned char*)((IRPVertexBuffer*)(this)) - 0x10 + kOffVbList);
    FRp8Vb* OldLast = List->Last;
    List->Last = Wrapper;
    if (OldLast == nullptr)
        List->First = Wrapper;
    else
        OldLast->Next = Wrapper;
    Wrapper->Next = nullptr;
    Wrapper->Prev = OldLast;
    List->Count += 1;
    // 06d11d94: hr = this->vtbl[0x14](node, fvf, count) = resize_vb (which always returns 0);
    // hr >= 0 -> *Out = node. No hr text here.
    const GENRESULT Hr = ResizeVb(Wrapper, Fvf, Count);
    if ((HRESULT)Hr >= 0)
        *Out = (unsigned int*)(Wrapper);
    return Hr;
}
// FUN_06d11db3. The argument is a pointer to the caller's handle variable: retail reads it,
// clears it, and only then tears the buffer down. Taking the handle by value here is what let
// alchemy's `CallDestroyVb(Vb, &G_VbHandle)` reach free() with the address of a global.
GENRESULT DACOM_API FDirectX8::DestroyVb(void** Handle)
{
    IDirect3DDevice8* Device = Device_;
    if (Device == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 4081, "WARNING", "destroy_vb() called outside of device lifetime");
        return GR_GENERIC;
    }
    // 06d11e1a: *H == -1 -> return 0 with the handle untouched.
    if (*Handle == (void*)((intptr_t)-1))
        return GR_OK;
    // 06d11e2d: node = *H; *H = 0; node == 0 -> 0.
    FRp8Vb* Wrapper = (FRp8Vb*)(*Handle);
    *Handle = nullptr;
    if (Wrapper == nullptr)
        return GR_OK;
    // 06d11e48: a pool buffer (usage bit 0x200, create_vb flags&1) is not destroyed here.
    if ((Wrapper->Flags & 0x200) != 0)
        return GR_OK;
    // 06d11e64: node == [sub+0x12c] (the selected VB) -> this->vtbl[0x28](0) = select_vb.
    unsigned char* VbThis = (unsigned char*)((IRPVertexBuffer*)(this));
    if (Wrapper == StreamCache_.VertexBuffer)
        SelectVb(nullptr);
    // 06d11e80: unlink from the VB list (next +0x14, prev +0x18), count--.
    FRp8VbList* List = (FRp8VbList*)(VbThis - 0x10 + kOffVbList);
    if (Wrapper->Prev == nullptr)
        List->First = Wrapper->Next;
    else
        Wrapper->Prev->Next = Wrapper->Next;
    if (Wrapper->Next == nullptr)
        List->Last = Wrapper->Prev;
    else
        Wrapper->Next->Prev = Wrapper->Prev;
    List->Count -= 1;
    // 06d11eec: release (Locked -> 0 + Unlock first), operator delete; return 0 (06d11f70).
    Wrapper->ReleaseGpu();
    delete Wrapper;
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::ResizeVb(void* Handle, unsigned int Fvf, unsigned int Count)
{
    IDirect3DDevice8* Device = Device_;
    // FUN_06d11f78: CMP [this+0x120],0 -> FDUMP line 0x1010 (retail's own "ressize_vb"
    // spelling), OR EAX,-1.
    if (Device == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 4112, "WARNING", "ressize_vb() called outside of device lifetime");
        return GR_GENERIC;
    }
    // 06d11fe6: node == -1 or 0 -> return 0 (06d12284 XOR EAX,EAX).
    FRp8Vb* Wrapper = (FRp8Vb*)(Handle);
    if (Wrapper == (FRp8Vb*)((intptr_t)-1) || Wrapper == nullptr)
        return GR_OK;
    // 06d12016: release (Locked -> 0 + Unlock first); +4 fvf, +8 count, +0xc = 0, +0x1c = 0.
    Wrapper->ReleaseGpu();
    Wrapper->Fvf = Fvf;
    Wrapper->Count = Count;
    Wrapper->Used = 0;
    Wrapper->Field1C = 0;
    // 06d1208f: stride of node->Fvf (inlined position/normal/diffuse/specular sum +
    // FUN_06d16310 texcoords = FvfStride) * node->Count; CreateVertexBuffer(+0x5c)(bytes,
    // node Flags, fvf argument, D3DPOOL_DEFAULT, &node->Gpu); hr < 0 -> 'create_vb: %s' 0x101b.
    const unsigned int Stride = FvfStride(Wrapper->Fvf);
    const HRESULT Hr = Device->CreateVertexBuffer(
        Stride * Wrapper->Count, Wrapper->Flags,
        Fvf, D3DPOOL_DEFAULT, &Wrapper->Gpu);
    if (Hr < 0)
        FdumpHr("create_vb: %s", Hr, 4123);
    // 06d12268: node == [sub+0x12c] -> select_vb(0). The hr is not returned: always 0.
    if (Wrapper == StreamCache_.VertexBuffer)
        SelectVb(nullptr);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::CopyVertices(void* Handle, unsigned int* OutOffset, const FRpVertexSrcDesc* Src, unsigned int StartVertex, unsigned int Count)
{
    FRp8Vb* Wrapper = (FRp8Vb*)Handle;

    unsigned char* VbThis = (unsigned char*)((IRPVertexBuffer*)(this));
    IDirect3DDevice8* Device = Device_;
    if (Device == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 4136, "WARNING", "copy_vertices() called outside of device lifetime");
        return GR_GENERIC;
    }
    // FUN_06d1228c RET 0x18 (06d122f6..06d126b8): hr = -1; node 0 -> return it.
    HRESULT Hr = (HRESULT)0xFFFFFFFF;
    if (Wrapper == nullptr)
        return (GENRESULT)Hr;

    // 06d12307: node -1 -> the pooled buffer for the source descriptor's FVF ([desc+0x68], read
    // unguarded): find-or-insert, the grow (FUN_06d1b650 is the same release/refill/
    // CreateVertexBuffer as the inlined one), FUN_06d1c770 on the stream cache.
    if (Wrapper == (FRp8Vb*)((intptr_t)-1))
    {
        const unsigned int Fvf = Src->SrcFvf;
        Wrapper = FindOrInsertPoolVb(Fvf);
        GrowPoolVb(Wrapper, Fvf, Count);
        StreamCache_.SelectVertexStream(Wrapper, 0);
    }

    // 06d124db: the caller's offset (0 without an out-pointer); the node's stride (inlined
    // FvfStride). A node that is already locked (+0x10 holds its data pointer) is written in
    // place at stride * offset with hr 0; otherwise lock_vb (vtbl+0x1c) locks Count vertices and
    // updates the offset.
    unsigned int Offset = OutOffset != nullptr ? *OutOffset : 0;
    unsigned char* Bits = nullptr;
    const unsigned int Stride = FvfStride(Wrapper->Fvf);
    const bool bWasLocked = Wrapper->Locked != 0;
    if (bWasLocked)
    {
        Bits = (unsigned char*)((uintptr_t)Wrapper->Locked) + Stride * Offset;
        Hr = 0;
    }
    else
    {
        Hr = (HRESULT)(LockVb(Wrapper, &Offset, (void**)(&Bits), Count));
    }
    if (Hr < 0)
        return (GENRESULT)Hr;

    // 06d1264f: FUN_06d1f040(bits - stride * StartVertex, node Fvf, desc, StartVertex, Count)
    // (the copier adds StartVertex back); a node locked here is unlocked again (+0x10 = 0,
    // Unlock's hr is the result); the offset goes back to the caller.
    Bits -= Stride * StartVertex;
    CopyVertexData(Bits, Wrapper->Fvf, Src, StartVertex, Count);
    if (!bWasLocked)
    {
        Wrapper->Locked = 0;
        Hr = Wrapper->Gpu->Unlock();
    }
    if (OutOffset != nullptr)
        *OutOffset = Offset;
    return (GENRESULT)Hr;
}
GENRESULT DACOM_API FDirectX8::LockVb(void* Handle, unsigned int* Offset, void** Out,
                                      unsigned int Count)
{
    // FUN_06d126bb RET 0x14, traced line by line.
    unsigned char* VbThis = (unsigned char*)((IRPVertexBuffer*)(this));
    IDirect3DDevice8* Device = Device_;
    if (Device == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 4189, "WARNING", "lock_vb() called outside of device lifetime");
        return GR_GENERIC;
    }
    // 06d1272b: result = 0x80004005; node == 0 or -1 -> return it (no text).
    HRESULT Hr = (HRESULT)0x80004005;
    FRp8Vb* Wrapper = (FRp8Vb*)(Handle);
    if (Wrapper == nullptr || Wrapper == (FRp8Vb*)((INT_PTR)-1))
        return (GENRESULT)Hr;
    // 06d1274c: the caller's offset is taken as a BYTE offset (it is not scaled); on success
    // the vertex index is written back. 06d12763: USE_SYSLOCK = byte [sub+0x118] >> 1 & 1.
    unsigned int StartBytes = Offset != nullptr ? *Offset : 0;
    const bool bSysLock = ((VbThis[0x118] >> 1) & 1) != 0;
    if (Wrapper->Gpu == nullptr)
        Hr = (HRESULT)0x80004005;
    else if (Wrapper->Count < Count)
    {
        // 06d12795: [+8] < count (JNC skips) -> NOTICE dx8_vertex_buffer.h:0xa9.
        char Text[0x2000];
        _snprintf(Text, sizeof(Text),
                  "vertex buffer %x at %d verts too small to hold requested locked verts %d",
                  (unsigned int)((UINT_PTR)Wrapper->Gpu),
                  (int)Wrapper->Count, (int)Count);
        Text[sizeof(Text) - 1] = '\0';
        Rp8Fdump(kFdumpNotice, 169, "NOTICE", Text, kSrcDx8VertexBuffer);
        Hr = (HRESULT)0x80004005;
    }
    else
    {
        // 06d1283b: stride (FUN_06d162b0 + normal + FUN_06d163b0 + FUN_06d16310 = FvfStride);
        // total = stride * [+8]; size = count * stride; flags = SYSLOCK ? 0 : NOSYSLOCK 0x800.
        const unsigned int Stride = FvfStride(Wrapper->Fvf);
        const unsigned int CapacityBytes = Stride * Wrapper->Count;
        const unsigned int RequestBytes = Count * Stride;
        unsigned long LockFlags = bSysLock ? 0ul : 0x800ul;
        // 06d128ec: dynamic (usage 0x200): [+0xc] + size > total (JBE keeps) -> [+0xc] = 0,
        // [+0x1c] = 0, DISCARD 0x2000, offset 0; else NOOVERWRITE 0x1000, offset = [+0xc].
        if ((Wrapper->Flags & 0x200) != 0)
        {
            if (Wrapper->Used + RequestBytes > CapacityBytes)
            {
                Wrapper->Used = 0;
                Wrapper->Field1C = 0;
                LockFlags |= 0x2000;
                StartBytes = 0;
            }
            else
            {
                LockFlags |= 0x1000;
                StartBytes = Wrapper->Used;
            }
        }
        // 06d12993: Lock (+0x2c)(offset, size, &node+0x10, flags) -- the data pointer lands in
        // +0x10, which doubles as the "locked" marker everywhere else.
        Hr = Wrapper->Gpu->Lock(StartBytes, RequestBytes,
                                (BYTE**)(&Wrapper->Locked), LockFlags);
        if (Hr >= 0)
        {
            // 06d129a5: [+0x1c] = offset / stride (DIV); *Offset = [+0x1c] if given;
            // *Out = [+0x10] (always); [+0xc] = offset + size.
            Wrapper->Field1C = StartBytes / Stride;
            if (Offset != nullptr)
                *Offset = Wrapper->Field1C;
            *Out = (void*)((UINT_PTR)Wrapper->Locked);
            Wrapper->Used = StartBytes + RequestBytes;
        }
    }
    // 06d129f0: result < 0 -> 'vb_lock: %s' 0x1075 (also after the two early failures).
    if (Hr < 0)
        FdumpHr("vb_lock: %s", Hr, 4213);
    return (GENRESULT)Hr;
}
GENRESULT DACOM_API FDirectX8::UnlockVb(void* Handle)
{
    IDirect3DDevice8* Device = Device_;
    if (Device == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 4222, "WARNING", "unlock_vb() called outside of device lifetime");
        return GR_GENERIC;
    }
    // FUN_06d12a8b RET 0x8: node == 0 or -1 -> 0x80004005; +0x10 = 0; return
    // Gpu->Unlock (+0x30) -- the buffer pointer is not tested.
    FRp8Vb* Wrapper = (FRp8Vb*)(Handle);
    if (Wrapper == nullptr || Wrapper == (FRp8Vb*)((INT_PTR)-1))
        return (GENRESULT)0x80004005;
    Wrapper->Locked = 0;
    return (GENRESULT)(Wrapper->Gpu->Unlock());
}
// formerly VbSlot9
GENRESULT DACOM_API FDirectX8::UnsupportedVertexBufferOperation(void* /*Handle*/)
{
    // FUN_06d12b30 RET 0x8: one explicit dword, unread, always "unsupported".
    return (GENRESULT)0xFFFFFFF9;
}
GENRESULT DACOM_API FDirectX8::SelectVb(void* Handle)
{
    // IRPVertexBuffer this = object+0x10. FUN_06d12b9d RET 0x8 (this, handle).
    unsigned char* VbThis = (unsigned char*)((IRPVertexBuffer*)(this));
    const unsigned long Ready = *(unsigned long*)(VbThis + 0x174);
    // 06d12ba6: MOV CL,byte [this+0x174]; AND 1 -- the device pointer is not read at all.
    if ((Ready & 1) == 0)
    {
        Rp8Fdump(kFdumpWarning, 4265, "WARNING",
                 "select_vb() called outside of create_buffers/destroy_buffers");
        return GR_GENERIC;
    }

    FRp8StreamCache& Cache = StreamCache_;
    if (Handle == nullptr)
    {
        Cache.Fvf = 0;
        Cache.VertexBuffer = nullptr;
        Cache.IndexBuffer = nullptr;
        Cache.BaseVertex = 0xFFFFFFFF;
        memset(&Cache.Flags, 0, sizeof(Cache.Flags));
        return (GENRESULT)0x80004005;
    }

    // 06d12c59..06d12cd3: -1 -> the node already in [+4]; a null node or one without Gpu
    // (+0x20) -> E_FAIL; else the MarkStreamVb compare/stores and 0. The device is not called
    // here -- FUN_06d04138 binds the stream at the next draw.
    FRp8Vb* Wrapper = nullptr;
    if (Handle == (void*)((INT_PTR)-1))
        Wrapper = Cache.VertexBuffer;
    else
        Wrapper = (FRp8Vb*)(Handle);
    if (Wrapper == nullptr || Wrapper->Gpu == nullptr)
        return (GENRESULT)0x80004005;

    FRp8Vb* Current = Cache.VertexBuffer;
    const unsigned long OverrideFvf = Cache.Fvf;
    if (Wrapper != Current || OverrideFvf != 0)
    {
        Cache.Flags.Dirty = (unsigned char)(Cache.Flags.Dirty | StreamDirtyVertexSource);
        Cache.VertexBuffer = Wrapper;
        Cache.Fvf = 0;
    }
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetVbInfo(void* Handle, unsigned int* OutFvf, unsigned int* OutCount)
{
    if (Handle == nullptr || Handle == (void*)((intptr_t)(-1)))
        return (GENRESULT)(0x80004005);
    FRp8Vb* Wrapper = (FRp8Vb*)(Handle);
    if (Wrapper->Gpu == nullptr)
        return (GENRESULT)(0x80004005);
    GENRESULT Result = (GENRESULT)(0x80004005);
    if (OutFvf != nullptr)
    {
        *OutFvf = Wrapper->Fvf;
        Result = GR_OK;
    }
    if (OutCount != nullptr)
    {
        *OutCount = Wrapper->Count;
        Result = GR_OK;
    }
    return Result;
}
GENRESULT DACOM_API FDirectX8::IsVbValid(void* A)
{
    if (A == nullptr)
        return GR_OK;
    if (A == (void*)((intptr_t)(-1)))
        return (GENRESULT)(1);
    FRp8Vb* Wrapper = (FRp8Vb*)(A);
    return (Wrapper->Gpu != nullptr) ? (GENRESULT)(1) : GR_OK;
}

GENRESULT DACOM_API FDirectX8::SetGammaEnable(unsigned char Enable)
{
    // FUN_06d1535d RET 8: [g+0x3ec]==0 || [g+0x3f0]==0 -> -3; else byte [g+0x170] =
    // (byte & 0xfd) | ((Enable & 1) << 1); 0.
    if (Ability_[1] == 0 || Ability_[2] == 0)
        return (GENRESULT)(0xFFFFFFFD);
    DeviceReady_ = (DeviceReady_ & ~2ul) | ((unsigned long)(Enable & 1) << 1);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetGammaEnable()
{
    // FUN_06d153a4 RET 4: same -3 gate; bit1 of byte [g+0x170] set -> 0, else OR EAX,-1.
    if (Ability_[1] == 0 || Ability_[2] == 0)
        return (GENRESULT)(0xFFFFFFFD);
    if (((DeviceReady_ >> 1) & 1) == 0)
        return GR_GENERIC;
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetGammaFunction(unsigned int ChannelMask, float Gamma,
                                                float Brightness, float Contrast, float Bias)
{
    // Gamma calculations retain double intermediates and float storage boundaries.
    if ((DeviceReady_ & 1) == 0)
    {
        Rp8Fdump(kFdumpWarning, 4819, "WARNING",
                 "set_gamma_function() called outside of create_buffers/destroy_buffers");
        return GR_GENERIC;
    }
    if (Ability_[1] == 0)
        return (GENRESULT)(0xFFFFFFFD);
    D3DGAMMARAMP Ramp;
    const float InvGamma = (float)(1.0 / (double)(Gamma));
    const float Scale = 65535.0f;
    for (unsigned int Index = 0; Index < 0x100; ++Index)
    {
        const float X = (float)((double)(Index) / 255.0);
        float Value = (float)(
            (double)(Brightness) +
            pow((double)(Contrast) * (double)(X) + (double)(Bias),
                (double)(InvGamma)));
        if (Value < 0.0f)
            Value = 0.0f;
        if (Value > 1.0f)
            Value = 1.0f;
        const long Word = (long)((double)(Value) * (double)(Scale));
        if ((ChannelMask & 1) != 0)
            Ramp.red[Index] = (WORD)(Word);
        if ((ChannelMask & 2) != 0)
            Ramp.green[Index] = (WORD)(Word);
        if ((ChannelMask & 4) != 0)
            Ramp.blue[Index] = (WORD)(Word);
    }
    const DWORD Flags = ((DeviceReady_ >> 1) & 1) != 0 ? 1u : 0u;
    Device_->SetGammaRamp(Flags, &Ramp);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::SetGammaRamp(unsigned int ChannelMask, const unsigned short* RampData)
{
    // FUN_06d1560c RET 0xc (g = this+0x14): [g+0x194] (AdapterIndex_) == -1 -> FDUMP line
    // 0x12fc, OR EAX,-1; [g+0x3ec] == 0 -> -3; Mask != 7 -> Device->GetGammaRamp(+0x4c)(&ramp);
    // Mask & 1/2/4 -> REP MOVSD 0x80 dwords from RampData into red/green/blue (the same source
    // for each); Device->SetGammaRamp(+0x48)(bit1 of byte [g+0x170], &ramp); XOR EAX,EAX.
    if (AdapterIndex_ == 0xFFFFFFFF)
    {
        Rp8Fdump(kFdumpWarning, 4860, "WARNING",
                 "set_gamma_ramp() called outside of startup/shutdown");
        return GR_GENERIC;
    }
    if (Ability_[1] == 0)
        return (GENRESULT)(0xFFFFFFFD);
    D3DGAMMARAMP Ramp;
    if (ChannelMask != 7)
        Device_->GetGammaRamp(&Ramp);
    if ((ChannelMask & 1) != 0)
        memcpy(Ramp.red, RampData, sizeof(Ramp.red));
    if ((ChannelMask & 2) != 0)
        memcpy(Ramp.green, RampData, sizeof(Ramp.green));
    if ((ChannelMask & 4) != 0)
        memcpy(Ramp.blue, RampData, sizeof(Ramp.blue));
    const DWORD Flags = ((DeviceReady_ >> 1) & 1) != 0 ? 1u : 0u;
    Device_->SetGammaRamp(Flags, &Ramp);
    return GR_OK;
}
GENRESULT DACOM_API FDirectX8::GetGammaRamp(unsigned int Channel, unsigned short* OutRamp)
{
    // FUN_06d15757 RET 0xc (g = this+0x14): [g+0x194] (AdapterIndex_) == -1 -> FDUMP line
    // 0x1324, OR EAX,-1; [g+0x3ec] == 0 -> -3; Device->GetGammaRamp(+0x4c); Channel 1/2/4 ->
    // REP MOVSD 0x80 dwords of red/green/blue into OutRamp, 0; else FDUMP line 0x1334
    // ("...request\n"), OR EAX,-1.
    if (AdapterIndex_ == 0xFFFFFFFF)
    {
        Rp8Fdump(kFdumpWarning, 4900, "WARNING",
                 "get_gamma_ramp() called outside of startup/shutdown");
        return GR_GENERIC;
    }
    if (Ability_[1] == 0)
        return (GENRESULT)(0xFFFFFFFD);
    D3DGAMMARAMP Ramp;
    Device_->GetGammaRamp(&Ramp);
    if (Channel == 1)
        memcpy(OutRamp, Ramp.red, sizeof(Ramp.red));
    else if (Channel == 2)
        memcpy(OutRamp, Ramp.green, sizeof(Ramp.green));
    else if (Channel == 4)
        memcpy(OutRamp, Ramp.blue, sizeof(Ramp.blue));
    else
    {
        Rp8Fdump(kFdumpWarning, 0x1334, "WARNING", "get_gamma_ramp: bad gamma ramp requested\n");
        return GR_GENERIC;
    }
    return GR_OK;
}

GENRESULT DACOM_API FDirectX8::PrintScreen(void* ParentFileSystem, const char* FileName)
{
    // FUN_06d13d70 (interface this = object+0x18, RET 0xc), traced line by line.
    const GENRESULT Ready = NeedBuffers(
        "print_screen() called outside of create_buffers/destroy_buffers", 4579);
    if (Ready != GR_OK)
        return Ready;
    // 06d13de8 / 06d13e47: both argument failures return -2.
    if (ParentFileSystem != nullptr)
    {
        Rp8Fdump(kFdumpWarning, 0x11e7, "WARNING", "IFilesystem parent not yet supported");
        return GR_INVALID_PARMS;
    }
    if (FileName == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 0x11ec, "WARNING", "filename not specified");
        return GR_INVALID_PARMS;
    }
    // 06d13eb5..06d14093: the extension from strrchr('.') is matched with _stricmp against the
    // table {".BMP" 0, ".JPG" 1, ".TGA" 2, ".PNG" 3, ".DDS" 4, ".PPM" 5, ".DIB" 6} (PE
    // 06d6a0e8..06d6a118; the ids are D3DXIMAGE_FILEFORMAT). No '.' -> dump at 0x11f4, no
    // match -> dump at 0x1214, both -1.
    char Text[0x2000];
    const char* Extension = strrchr(FileName, '.');
    if (Extension == nullptr || *Extension == '\0')
    {
        _snprintf(Text, sizeof(Text), "couldnt find appropriate extension for %s", FileName);
        Text[sizeof(Text) - 1] = '\0';
        Rp8Fdump(kFdumpWarning, 0x11f4, "WARNING", Text);
        return GR_GENERIC;
    }
    struct FPrintScreenFormat
    {
        const char* Extension;
        unsigned long Format;
    };
    const FPrintScreenFormat Formats[7] = {{".BMP", 0}, {".JPG", 1}, {".TGA", 2}, {".PNG", 3},
                                           {".DDS", 4}, {".PPM", 5}, {".DIB", 6}};
    const int FormatCount = 7;
    int FormatIndex = 0;
    for (FormatIndex = 0; FormatIndex < FormatCount; ++FormatIndex)
    {
        if (_stricmp(Extension, Formats[FormatIndex].Extension) == 0)
            break;
    }
    if (FormatIndex >= FormatCount)
    {
        _snprintf(Text, sizeof(Text), "couldnt find appropriate extension for %s", FileName);
        Text[sizeof(Text) - 1] = '\0';
        Rp8Fdump(kFdumpWarning, 0x1214, "WARNING", Text);
        return GR_GENERIC;
    }
    // 06d14093: D3d_ (+0x20) GetAdapterDisplayMode(AdapterIndex_, &mode); failure -> dump at
    // 0x121d and return hr.
    D3DDISPLAYMODE Mode;
    HRESULT Hr = D3d_->GetAdapterDisplayMode(AdapterIndex_, &Mode);
    if (Hr < 0)
    {
        FdumpHr("print_screen_get_display_mode: %s", Hr, 0x121d);
        return (GENRESULT)(Hr);
    }
    // 06d14154: Device_ (+0x6c) CreateImageSurface(mode w, mode h, A8R8G8B8 0x15, &surf);
    // (+0x78) GetFrontBuffer(surf); each failure dumps (0x1224 / 0x1227), releases surf when
    // set, returns hr.
    IDirect3DSurface8* Surface = nullptr;
    Hr = Device_->CreateImageSurface(Mode.Width, Mode.Height, D3DFMT_A8R8G8B8, &Surface);
    if (Hr < 0)
    {
        FdumpHr("print_screen_create_copy: %s", Hr, 0x1224);
        if (Surface != nullptr)
        {
            Surface->Release();
            Surface = nullptr;
        }
        return (GENRESULT)(Hr);
    }
    Hr = Device_->GetFrontBuffer(Surface);
    if (Hr < 0)
    {
        FdumpHr("print_screen_get_front_buffer: %s", Hr, 0x1227);
        if (Surface != nullptr)
        {
            Surface->Release();
            Surface = nullptr;
        }
        return (GENRESULT)(Hr);
    }
    // 06d1433d: byte [+0x25D] == 0 (windowed) -> the source rect is the window's client area in
    // screen coordinates: SetRect from the mode size (+0x240/+0x244) with Hwnd_, or from
    // WindowX/Y/W/H with WindowOverride_ when that is set; then GetClientRect overwrites it and
    // MapWindowPoints(hwnd, 0, rect, 2) maps it. Fullscreen passes no rect.
    RECT ClientRect;
    RECT* SourceRect = nullptr;
    unsigned char* Base = ObjectBase();
    if (Base[0x25D] == 0)
    {
        SourceRect = &ClientRect;
        HWND Window = nullptr;
        if (WindowOverride_ == nullptr)
        {
            Window = Hwnd_;
            SetRect(&ClientRect, 0, 0, *(int*)(Base + 0x240),
                    *(int*)(Base + 0x244));
        }
        else
        {
            Window = WindowOverride_;
            SetRect(&ClientRect, WindowX_, WindowY_, WindowX_ + WindowW_, WindowY_ + WindowH_);
        }
        GetClientRect(Window, SourceRect);
        MapWindowPoints(Window, nullptr, (POINT*)(SourceRect), 2);
    }
    Hr = D3DXSaveSurfaceToFileA(FileName,
                                (D3DXIMAGE_FILEFORMAT)(Formats[FormatIndex].Format),
                                Surface, nullptr, SourceRect);
    if (Hr < 0)
    {
        FdumpHr("print_screen_save_surface: %s", Hr, 0x123b);
        if (Surface != nullptr)
        {
            Surface->Release();
            Surface = nullptr;
        }
        return (GENRESULT)(Hr);
    }
    if (Surface != nullptr)
    {
        Surface->Release();
        Surface = nullptr;
    }
    return GR_OK;
}
namespace
{
    void InitTextureFileDesc(FFileSystemDesc* Desc, const char* Name, const char* Component)
    {
        Desc->Size = 0x34;
        Desc->ComponentName = Component;
        Desc->Implementation = nullptr;
        Desc->Path = Name;
        Desc->Access = 0x80000000;
        Desc->ShareMode = 1;
        Desc->Security = nullptr;
        Desc->Creation = 3;
        Desc->Flags = 0x8000080;
        Desc->TemplateFile = nullptr;
        Desc->Parent = nullptr;
        Desc->Extra = 0;
        Desc->Field30 = 0xFFFFFFFF;
    }

    // The file mapping load_texture (06d14972..06d14ab5, 06d14cb1..06d14d58) and load_cubemap
    // (06d14e8c..06d14fcf, 06d151ca..06d1526f) open around the D3DX decode; the two bodies
    // are the same instruction sequence.
    struct FRp8TextureFileMap
    {
        IFileSystem* FileSystem;
        int Handle;
        int Mapping;
        unsigned long Size;
        int View;
    };

    // Handle = -1, Size = 0, View = 0, hr = -1; +0x7C OpenByDesc -> -1 gives -8; else +0x28
    // GetSize(handle, 0) and +0x3C (handle, 0, 2, 0, 0, 0) -> -1 gives -6; else +0x40
    // (mapping, 4, 0, 0, 0) and a non-zero view gives 0 (a zero view keeps -1).
    HRESULT OpenTextureFileMap(FRp8TextureFileMap* Map, const char* Name)
    {
        Map->Handle = -1;
        Map->Size = 0;
        Map->View = 0;
        FFileSystemDesc Desc;
        InitTextureFileDesc(&Desc, Name, "FileSystem");
        HRESULT Hr = (HRESULT)(-1);
        Map->Handle = Map->FileSystem->OpenByDesc(&Desc);
        if (Map->Handle == -1)
            return (HRESULT)(-8);
        Map->Size = Map->FileSystem->GetSize((unsigned long)(Map->Handle), 0);
        Map->Mapping = Map->FileSystem->CreateMapping((unsigned long)(Map->Handle), 0, 2, 0, 0, 0);
        if (Map->Mapping == -1)
            return (HRESULT)(-6);
        Map->View = Map->FileSystem->MapFileView((unsigned long)(Map->Mapping), 4, 0, 0, 0);
        if (Map->View != 0)
            Hr = 0;
        return Hr;
    }

    // Handle != -1: mapping != -1 -> (view != 0 -> +0x44 unmap(view), view = 0) and +0x10
    // close(mapping); then +0x10 close(handle) and handle = -1.
    void CloseTextureFileMap(FRp8TextureFileMap* Map)
    {
        if (Map->Handle == -1)
            return;
        if (Map->Mapping != -1)
        {
            if (Map->View != 0)
            {
                Map->FileSystem->UnmapFileView((unsigned long)(Map->View));
                Map->View = 0;
            }
            Map->FileSystem->CloseFile((unsigned long)(Map->Mapping));
        }
        Map->FileSystem->CloseFile((unsigned long)(Map->Handle));
        Map->Handle = -1;
    }

    // hr == -6 only: FUN_06d168f0 (sprintf) of Format with the FileSystem pointer (%x) and the
    // name, "<null>" for a null name, into the 0x2000-byte buffer, then FDUMP at Line.
    void FdumpTextureMapping(const char* Format, IFileSystem* FileSystem, const char* Name, int Line)
    {
        char Text[0x2000];
        _snprintf(Text, sizeof(Text), Format,
                  (unsigned int)((uintptr_t)(FileSystem)),
                  Name != nullptr ? Name : "<null>");
        Text[sizeof(Text) - 1] = '\0';
        Rp8Fdump(kFdumpWarning, Line, "WARNING", Text);
    }
}  // namespace

GENRESULT DACOM_API FDirectX8::LoadTexture(IFileSystem* FileSystem, const char* FileName,
                                           void** OutHandle)
{
    // FUN_06d148d4 (interface this = object+0x18, RET 0x10), traced line by line.
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 0x1265, "WARNING", "load_texture() called outside of device lifetime");
        return GR_GENERIC;
    }
    // 06d14942: a smart pointer holds FileSystem (+0x04 AddRef when non-null); it is released
    // (+0x08) at 06d14d5f on every path below. There is no null check before the +0x7C call.
    FRp8TextureFileMap Map;
    Map.FileSystem = FileSystem;
    if (FileSystem != nullptr)
        FileSystem->AddRef();
    HRESULT Hr = OpenTextureFileMap(&Map, FileName);
    if (Hr < 0)
    {
        // 06d14ac8: only the mapping failure is reported.
        if (Hr == (HRESULT)(-6))
            FdumpTextureMapping("load_texture: couldnt create file mapping %x %s", FileSystem,
                                FileName, 0x1271);
    }
    else
    {
        // 06d14b76: FUN_06d308fa = D3DXCreateTextureFromFileInMemory of the statically linked
        // D3DX 8.1 (the same 22 instructions as _D3DXCreateTextureFromFileInMemory@16 in
        // dx8\lib\d3dx8.lib, which this module links) on (device, view, size, &texture).
        IDirect3DTexture8* Texture = nullptr;
        Hr = D3DXCreateTextureFromFileInMemory(Device_,
                                               (const void*)(
                                                   (uintptr_t)(Map.View)),
                                               Map.Size, &Texture);
        if (Hr < 0)
            FdumpHr("load_texture: %s", Hr, 0x1278);
        if (Hr >= 0)
        {
            // 06d14c32: operator new(8) {texture, 0} when the texture is non-null, else a null
            // handle; *OutHandle is written either way.
            FRp8TextureHandle* Handle = nullptr;
            if (Texture != nullptr)
            {
                Handle = new FRp8TextureHandle;
                Handle->Texture = Texture;
                Handle->Extra = 0;
            }
            *OutHandle = Handle;
        }
    }
    CloseTextureFileMap(&Map);
    if (FileSystem != nullptr)
        FileSystem->Release();
    return (GENRESULT)(Hr);
}
GENRESULT DACOM_API FDirectX8::LoadSurfaceData(void* Handle, int Level, unsigned int SrcSize,
                                               const void* SrcData)
{
    // FUN_06d1455e (interface this = object+0x18, RET 0x14), traced line by line.
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 0x1242, "WARNING",
                 "load_surface_from_file() called outside of device lifetime");
        return GR_GENERIC;
    }
    // 06d145cc: a null handle or a handle without a texture -> dump at 0x1249, -2.
    FRp8TextureHandle* Wrapper = (FRp8TextureHandle*)(Handle);
    if (Wrapper == nullptr || Wrapper->Texture == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 0x1249, "WARNING", "load_tga_surface_data: invalid texture\n");
        return (GENRESULT)(0xFFFFFFFE);
    }
    // 06d14644..06d1476a: dx8_texture.h get_subsurface (hr E_FAIL; 2D -> GetSurfaceLevel(Level);
    // cube -> GetCubeMapSurface(Level, 0); failure dumped at dx8_texture.h 0xab).
    IDirect3DSurface8* Surface = nullptr;
    const HRESULT SubHr = TextureGetSubSurface(Wrapper, (UINT)(Level), &Surface);
    if (SubHr < 0)
    {
        // 06d1477f: dump at 0x1250, release the surface when set, -2.
        Rp8Fdump(kFdumpWarning, 0x1250, "WARNING",
                 "load_tga_surface_data: invalid sub surface specified\n");
        if (Surface != nullptr)
        {
            Surface->Release();
            Surface = nullptr;
        }
        return (GENRESULT)(0xFFFFFFFE);
    }
    // 06d147f6: FUN_06d2fcbf = D3DXLoadSurfaceFromFileInMemory of the statically linked D3DX
    // 8.1 (surface, no palette, no dest rect, SrcData = param_5, SrcSize = param_4, no src
    // rect, filter 1 = D3DX_FILTER_NONE, colour key 0, no info); failure -> "load_surface_data:
    // %s" at 0x125e. The surface is released and hr returned either way.
    const HRESULT Hr = D3DXLoadSurfaceFromFileInMemory(Surface, nullptr, nullptr, SrcData, SrcSize,
                                                       nullptr, D3DX_FILTER_NONE, 0, nullptr);
    if (Hr < 0)
        FdumpHr("load_surface_data: %s", Hr, 0x125e);
    if (Surface != nullptr)
    {
        Surface->Release();
        Surface = nullptr;
    }
    return (GENRESULT)(Hr);
}
GENRESULT DACOM_API FDirectX8::GetImageInfo(_D3DXIMAGE_INFO* Info, unsigned int SrcSize,
                                            const void* SrcData)
{
    // FUN_06d152e4 (interface this = object+0x18, RET 0x10): no device -> the load_texture
    // text at 0x12ac and -1; else FUN_06d2f6b6(param_4, param_3, param_2), which is
    // D3DXGetImageInfoFromFileInMemory of the statically linked D3DX 8.1 (instruction for
    // instruction _D3DXGetImageInfoFromFileInMemory@12 in dx8\lib\d3dx8.lib).
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 0x12ac, "WARNING", "load_texture() called outside of device lifetime");
        return GR_GENERIC;
    }
    return (GENRESULT)(D3DXGetImageInfoFromFileInMemory(SrcData, SrcSize, Info));
}
GENRESULT DACOM_API FDirectX8::LoadCubemap(IFileSystem* FileSystem, const char* FileName,
                                           void** OutHandle)
{
    // FUN_06d14dd7 (interface this = object+0x18, RET 0x10): load_texture's body with the
    // cube-map ability gate, its own texts/lines, the cube D3DX entry and handle tag 1.
    if (Device_ == nullptr)
    {
        Rp8Fdump(kFdumpWarning, 0x1286, "WARNING", "load_cubemap() called outside of device lifetime");
        return GR_GENERIC;
    }
    // 06d14e45: hr = -7; [this+0x404] (object+0x41C, TEXTURE_CUBEMAPS) == 0 -> return -7 before
    // the FileSystem is touched.
    HRESULT Hr = (HRESULT)(-7);
    if (Ability_[8] == 0)
        return (GENRESULT)(Hr);
    FRp8TextureFileMap Map;
    Map.FileSystem = FileSystem;
    if (FileSystem != nullptr)
        FileSystem->AddRef();
    Hr = OpenTextureFileMap(&Map, FileName);
    if (Hr < 0)
    {
        // 06d14fcf..06d1508b: the retail text for the cube path is spelled "maopping".
        if (Hr == (HRESULT)(-6))
            FdumpTextureMapping("load_texture: couldnt create file maopping %x %s", FileSystem,
                                FileName, 0x1296);
    }
    else
    {
        // 06d15090: FUN_06d3092a = D3DXCreateCubeTextureFromFileInMemory of the statically
        // linked D3DX 8.1 (instruction for instruction the library's
        // _D3DXCreateCubeTextureFromFileInMemory@16); failure -> "load_texture: %s" at 0x129d.
        IDirect3DCubeTexture8* CubeTexture = nullptr;
        Hr = D3DXCreateCubeTextureFromFileInMemory(Device_,
                                                   (const void*)(
                                                       (uintptr_t)(Map.View)),
                                                   Map.Size, &CubeTexture);
        if (Hr < 0)
            FdumpHr("load_texture: %s", Hr, 0x129d);
        if (Hr >= 0)
        {
            // 06d15145: operator new(8) {cube texture, 1} when non-null, else a null handle.
            FRp8TextureHandle* Handle = nullptr;
            if (CubeTexture != nullptr)
            {
                Handle = new FRp8TextureHandle;
                Handle->Texture = (IDirect3DTexture8*)(CubeTexture);
                Handle->Extra = 1;
            }
            *OutHandle = Handle;
        }
    }
    CloseTextureFileMap(&Map);
    if (FileSystem != nullptr)
        FileSystem->Release();
    return (GENRESULT)(Hr);
}

BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason != DLL_PROCESS_ATTACH)
        return TRUE;
    DisableThreadLibraryCalls(Module);
    ICOManager* Dacom = DACOM_Acquire();
    if (Dacom == nullptr)
        return TRUE;
    G_Factory = new (std::nothrow) FRp8Factory();
    if (G_Factory == nullptr)
        return TRUE;
    Dacom->RegisterComponent(G_Factory, kRp8ComponentName,
                             (void*)(kRp8RegisterPriority));
    G_Factory->Release();
    return TRUE;
}
