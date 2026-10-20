// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_common.h"
#include "common_misc.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

// wave23 (sonnet:alchemy-wave23-lane1, 2026-09-11): needed for
// FGenericFxObjectBase's full definition (InstanceListHead_/InstanceListTail_/
// LiveInstanceCount_ member access from IntegratePool/ApplyFieldsToPool
// below, both friended there). Included here in the .cpp, not in
// alchemy_common.h, to avoid a header include cycle — alchemy_genericfx.h
// itself includes alchemy_common.h, which by the time this file processes
// this include is already fully parsed once (via the include above), so this
// is a one-directional dependency at the .cpp level only. See
// alchemy_common.h's FGenericFxObjectBase forward-declaration comment.
#include "alchemy_genericfx.h"

// The concrete ramp classes the node-lib readers construct with `new`, as retail does
// (FUN_06229eb0 / FUN_06229f00 children, the FUN_06227c70 IAnimatedSingle children).
#include "alchemy_ramp.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <intrin.h>
#include <xmmintrin.h>

// alchemy.dll — DACOM particle FX scaffolding.
// Shared constants, CRC/hash helpers, string helpers, FDUMP/debug helpers,
// appearance-pool alloc/free, COM-pointer helpers, particle/math helpers,
// mesh-pose helpers, byte-bag helpers, generic COM/VB call-thunk helpers and
// the ALE node-lib parsing helpers used by more than one of the concrete
// alchemy classes. This file also owns the definitions of every shared
// global declared extern in alchemy_common.h.
namespace AlchemyImpl
{
    const GENRESULT GR_ALE_LOAD_FAIL = static_cast<GENRESULT>(0xFFFFFFFA);
    const GENRESULT GR_ALE_CREATE_FAIL = static_cast<GENRESULT>(0xFFFFFFF8);
    const void* kRegisterPriority = reinterpret_cast<void*>(0x80000000);

    const char kAleNodeName[] = "ALEffectLib";
    const char kNodeLibName[] = "AlchemyNodeLibrary";
    const char kFxNodeFactory[] = "FxNode";
    const char kColideTypo[] = "FxColideField";
    const char kCollideFixed[] = "FxCollideField";
    const unsigned long kNodeParentRoot = 0x8000;
    const char kAleLeaveNode[] = "..";
    const char kAleFileSystemName[] = "FileSystem";
    const unsigned long kAleDescSize = 0x34;
    const unsigned long kAleAccessRead = 0x80000000;
    const unsigned long kAleAccessWrite = 0xC0000000;
    const unsigned long kAleShareRead = 1;
    const unsigned long kAleOpenExisting = 3;
    const unsigned long kAleCreateAlways = 2;
    const unsigned long kAleFileFlags = 0x08000080;
    const unsigned long kAleVersion10Bits = 0x3F800000; // 1.0f DAT_0624bac4
    const unsigned long kAleVersion11Bits = 0x3F8CCCCD; // 1.1f
    // kAleNameMax is now a constexpr in alchemy_common.h (used as an array
    // bound from other translation units).
    // FUN_0623ef70 / IAlchemy+0x14 / DAT_06256d68: same 256-dword table as
    // dacom.dll DAT_065b8530 (NOT IEEE poly 0xEDB88320). Case-preserving —
    // unlike DACOM_CRC::GetCRC32 which lowercases. VisEffect effect_crc and
    // InsertArchetype keys are this hash of the ALE effect name.
    static const unsigned long kAlchemyCrc32Table[256] = {
        0x00000000u, 0x09073096u, 0x120E612Cu, 0x1B0951BAu, 0xFF6DC419u, 0xF66AF48Fu, 0xED63A535u, 0xE46495A3u,
        0xFEDB8832u, 0xF7DCB8A4u, 0xECD5E91Eu, 0xE5D2D988u, 0x01B64C2Bu, 0x08B17CBDu, 0x13B82D07u, 0x1ABF1D91u,
        0xFDB71064u, 0xF4B020F2u, 0xEFB97148u, 0xE6BE41DEu, 0x02DAD47Du, 0x0BDDE4EBu, 0x10D4B551u, 0x19D385C7u,
        0x036C9856u, 0x0A6BA8C0u, 0x1162F97Au, 0x1865C9ECu, 0xFC015C4Fu, 0xF5066CD9u, 0xEE0F3D63u, 0xE7080DF5u,
        0xFB6E20C8u, 0xF269105Eu, 0xE96041E4u, 0xE0677172u, 0x0403E4D1u, 0x0D04D447u, 0x160D85FDu, 0x1F0AB56Bu,
        0x05B5A8FAu, 0x0CB2986Cu, 0x17BBC9D6u, 0x1EBCF940u, 0xFAD86CE3u, 0xF3DF5C75u, 0xE8D60DCFu, 0xE1D13D59u,
        0x06D930ACu, 0x0FDE003Au, 0x14D75180u, 0x1DD06116u, 0xF9B4F4B5u, 0xF0B3C423u, 0xEBBA9599u, 0xE2BDA50Fu,
        0xF802B89Eu, 0xF1058808u, 0xEA0CD9B2u, 0xE30BE924u, 0x076F7C87u, 0x0E684C11u, 0x15611DABu, 0x1C662D3Du,
        0xF6DC4190u, 0xFFDB7106u, 0xE4D220BCu, 0xEDD5102Au, 0x09B18589u, 0x00B6B51Fu, 0x1BBFE4A5u, 0x12B8D433u,
        0x0807C9A2u, 0x0100F934u, 0x1A09A88Eu, 0x130E9818u, 0xF76A0DBBu, 0xFE6D3D2Du, 0xE5646C97u, 0xEC635C01u,
        0x0B6B51F4u, 0x026C6162u, 0x196530D8u, 0x1062004Eu, 0xF40695EDu, 0xFD01A57Bu, 0xE608F4C1u, 0xEF0FC457u,
        0xF5B0D9C6u, 0xFCB7E950u, 0xE7BEB8EAu, 0xEEB9887Cu, 0x0ADD1DDFu, 0x03DA2D49u, 0x18D37CF3u, 0x11D44C65u,
        0x0DB26158u, 0x04B551CEu, 0x1FBC0074u, 0x16BB30E2u, 0xF2DFA541u, 0xFBD895D7u, 0xE0D1C46Du, 0xE9D6F4FBu,
        0xF369E96Au, 0xFA6ED9FCu, 0xE1678846u, 0xE860B8D0u, 0x0C042D73u, 0x05031DE5u, 0x1E0A4C5Fu, 0x170D7CC9u,
        0xF005713Cu, 0xF90241AAu, 0xE20B1010u, 0xEB0C2086u, 0x0F68B525u, 0x066F85B3u, 0x1D66D409u, 0x1461E49Fu,
        0x0EDEF90Eu, 0x07D9C998u, 0x1CD09822u, 0x15D7A8B4u, 0xF1B33D17u, 0xF8B40D81u, 0xE3BD5C3Bu, 0xEABA6CADu,
        0xEDB88320u, 0xE4BFB3B6u, 0xFFB6E20Cu, 0xF6B1D29Au, 0x12D54739u, 0x1BD277AFu, 0x00DB2615u, 0x09DC1683u,
        0x13630B12u, 0x1A643B84u, 0x016D6A3Eu, 0x086A5AA8u, 0xEC0ECF0Bu, 0xE509FF9Du, 0xFE00AE27u, 0xF7079EB1u,
        0x100F9344u, 0x1908A3D2u, 0x0201F268u, 0x0B06C2FEu, 0xEF62575Du, 0xE66567CBu, 0xFD6C3671u, 0xF46B06E7u,
        0xEED41B76u, 0xE7D32BE0u, 0xFCDA7A5Au, 0xF5DD4ACCu, 0x11B9DF6Fu, 0x18BEEFF9u, 0x03B7BE43u, 0x0AB08ED5u,
        0x16D6A3E8u, 0x1FD1937Eu, 0x04D8C2C4u, 0x0DDFF252u, 0xE9BB67F1u, 0xE0BC5767u, 0xFBB506DDu, 0xF2B2364Bu,
        0xE80D2BDAu, 0xE10A1B4Cu, 0xFA034AF6u, 0xF3047A60u, 0x1760EFC3u, 0x1E67DF55u, 0x056E8EEFu, 0x0C69BE79u,
        0xEB61B38Cu, 0xE266831Au, 0xF96FD2A0u, 0xF068E236u, 0x140C7795u, 0x1D0B4703u, 0x060216B9u, 0x0F05262Fu,
        0x15BA3BBEu, 0x1CBD0B28u, 0x07B45A92u, 0x0EB36A04u, 0xEAD7FFA7u, 0xE3D0CF31u, 0xF8D99E8Bu, 0xF1DEAE1Du,
        0x1B64C2B0u, 0x1263F226u, 0x096AA39Cu, 0x006D930Au, 0xE40906A9u, 0xED0E363Fu, 0xF6076785u, 0xFF005713u,
        0xE5BF4A82u, 0xECB87A14u, 0xF7B12BAEu, 0xFEB61B38u, 0x1AD28E9Bu, 0x13D5BE0Du, 0x08DCEFB7u, 0x01DBDF21u,
        0xE6D3D2D4u, 0xEFD4E242u, 0xF4DDB3F8u, 0xFDDA836Eu, 0x19BE16CDu, 0x10B9265Bu, 0x0BB077E1u, 0x02B74777u,
        0x18085AE6u, 0x110F6A70u, 0x0A063BCAu, 0x03010B5Cu, 0xE7659EFFu, 0xEE62AE69u, 0xF56BFFD3u, 0xFC6CCF45u,
        0xE00AE278u, 0xE90DD2EEu, 0xF2048354u, 0xFB03B3C2u, 0x1F672661u, 0x166016F7u, 0x0D69474Du, 0x046E77DBu,
        0x1ED16A4Au, 0x17D65ADCu, 0x0CDF0B66u, 0x05D83BF0u, 0xE1BCAE53u, 0xE8BB9EC5u, 0xF3B2CF7Fu, 0xFAB5FFE9u,
        0x1DBDF21Cu, 0x14BAC28Au, 0x0FB39330u, 0x06B4A3A6u, 0xE2D03605u, 0xEBD70693u, 0xF0DE5729u, 0xF9D967BFu,
        0xE3667A2Eu, 0xEA614AB8u, 0xF1681B02u, 0xF86F2B94u, 0x1C0BBE37u, 0x150C8EA1u, 0x0E05DF1Bu, 0x0702EF8Du,
    };

    // Kept for any reader of G_Crc32Table / G_bCrc32Ready (alchemy_common.h).
    unsigned long G_Crc32Table[256];
    unsigned char G_bCrc32Ready = 0;
    char G_ResolvedName[0x100];
    char G_NodeLibraryResolvedName[0x100];
    char G_EffectResolvedName[0x100];

    void EnsureCrc32Table()
    {
        if (G_bCrc32Ready != 0)
            return;
        for (unsigned long Index = 0; Index < 256; ++Index)
            G_Crc32Table[Index] = kAlchemyCrc32Table[Index];
        G_bCrc32Ready = 1;
    }

    unsigned long HashNameCrc(const char* Name)
    {
        // FUN_0623ef70: init 0xFFFFFFFF, table DAT_06256d68, final ~Crc. No lowercase.
        if (Name == nullptr)
            return 0;
        EnsureCrc32Table();
        unsigned long Crc = 0xFFFFFFFF;
        const unsigned char* Bytes = reinterpret_cast<const unsigned char*>(Name);
        while (*Bytes != 0)
        {
            const unsigned long Mix = static_cast<unsigned long>(*Bytes) ^ (Crc & 0xFF);
            Crc = (Crc >> 8) ^ G_Crc32Table[Mix];
            Bytes += 1;
        }
        return ~Crc;
    }

    // The eleven raw slot calls left in this module all reach interfaces this port does not
    // own, and not one of them is a node: ICamera (+0x0C/+0x10/+0x40) is Common.dll's and only
    // forward-declared here, the animation object (+0x20/+0x2C) is DALib's, and the "Extra2"
    // pose pair (+0x0C/+0x10) belongs to a mesh object whose interface has not been recovered.
    // TODO: each needs its owner to publish, in the sense of docs/re/cross_dll_protocol.md.

    void CopyCString(char* Dest, unsigned long DestChars, const char* Src)
    {
        if (Dest == nullptr || DestChars == 0)
            return;
        Dest[0] = 0;
        if (Src == nullptr)
            return;
        unsigned long Index = 0;
        while (Index + 1 < DestChars && Src[Index] != 0)
        {
            Dest[Index] = Src[Index];
            Index += 1;
        }
        Dest[Index] = 0;
    }

    void FixColideFieldAlias(char* Name)
    {
        if (Name == nullptr)
            return;
        const char* Typo = kColideTypo;
        unsigned long Index = 0;
        while (Typo[Index] != 0)
        {
            if (Name[Index] != Typo[Index])
                return;
            Index += 1;
        }
        if (Name[Index] != 0)
            return;
        CopyCString(Name, kAleNameMax, kCollideFixed);
    }

    const char kAlchemySection[] = "Alchemy";
    // FUN_062013f0 / LAB_06201330 FDUMP — EXE/alchemy.dll exact bytes.
    const char kAlchemyCppPath[] =
        "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\Alchemy.cpp";
    const char kFdumpErrorFmt[] = "%s(%d) : ERROR:General:%s";
    const char kFdumpWarningFmt[] = "%s(%d) : WARNING:General:%s";
    const char kMsgAcquirePipeline[] = "Alchemy: init: unable to acquire IID_IRenderPipeline";
    const char kMsgAcquireTexLib[] = "Alchemy: init: unable to acquire IID_ITextureLibrary";
    const char kMsgAcquireVb[] = "Alchemy: init: unable to acquire IID_IRPVertexBuffer";
    const char kMsgAcquireIb[] = "Alchemy: init: unable to acquire IID_IRPIndexBuffer";
    const char kMsgAcquireDraw[] = "Alchemy: init: unable to acquire IID_IRPDraw";
    const char kMsgCreateVb[] = "Alchemy unable to create Vertex Buffer.  Effects will not work";
    const char kMsgDrawWithoutBegin[] =
        "You must call begin_draw method before call any draw method of Alchemy.\n";
    const char kMsgBeginWithoutEnd[] =
        "You must call end_draw method before call beging_draw method.\n";
    const char kMsgEndWithoutBegin[] =
        "You must call begin_draw method before call end_draw method.\n";
    const unsigned int kAlchemyVbFvf = 0x142; // D3DFVF_XYZ|DIFFUSE|TEX1
    const unsigned long kAlchemyVertexStride = 0x18; // FUN_06208aa0 pointer += 0x18
    const unsigned long kInitNodeBoundBits = 0x7F7FFFFF;
    const unsigned long kBatchListCount = 4; // DAT_062578b4 .. DAT_062578e4 stride 0xC
    const char kDrawContextCpp[] =
        "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\ALIL\\ImpFxDrawContext.cpp";
    const char kMsgEndBeforeBegin[] = "End() called before Begin()\n";
    const char kMsgMoreVerts[] =
        "MORE vertices submitted to vertex buffer, than requested at lock";
    const char kMsgMoreIndices[] =
        "MORE indices submitted to vertex buffer, than requested at lock";
    // FUN_06240f50 0x76/0x7d — DynamicVB/IB capacity pre-checks in Begin().
    // Retail's own text names both overflow cases "vertices" (copy-paste in
    // the retail message table itself); kept byte-accurate.
    const char kMsgMaxVerticesExceeded[] =
        "Max Vertices exceeded\n  (%d) vertices requested from DynamicVB of size %d\n";
    const char kMsgMaxIndicesExceeded[] =
        "Max Indices exceeded\n  (%d) vertices requested from DynamicVB of size %d\n";
    const char kMsgEndBeforeDraw[] =
        "ALDrawContext::End() must be called before ALDrawContext::Draw()";
    const unsigned long kAssocEmitterAppearance = 1;
    const unsigned long kAssocAppearanceField = 2;

    // FUN_06203c20 / FUN_06203480 defaults when the key or parser is missing.
    const unsigned long kDefaultPoolSize = 1000;
    const unsigned long kDefaultUseMaterialBatcher = 1;

    // FUN_0623ebc0 element sizes (third arg) per appearance pool.
    const unsigned long kStrideBasic = 0x30;
    const unsigned long kStrideParticle = 0x74;
    const unsigned long kStrideRect = 0x30;
    const unsigned long kStridePerp = 0x3C;
    const unsigned long kStrideMesh = 0x34;
    const unsigned long kStrideOriented = 0x30;
    const unsigned long kStrideDust = 0x3C;
    const unsigned long kStrideBeam = 0x34;

    const char kCharsetValue[] = "0123456789truefalsynoxTRUEFALSYNOX+-";
    const char kCharsetDigits[] = "0123456789+-";

    // Retail loads DACOM's FDUMP pointer and calls through it with no null test (e.g.
    // 0x06201511: MOV EAX,[->FDUMP]; CALL [EAX]); ours is statically initialised to
    // DefaultFdump (dacom.cpp), so the port calls it unconditionally too.
    void FdumpAlchemyError(int Line, const char* Message)
    {
        FDUMP(0x100001, kFdumpErrorFmt, kAlchemyCppPath, Line, Message);
    }

    void FdumpAlchemyWarning(int Line, const char* Message)
    {
        FDUMP(0x100002, kFdumpWarningFmt, kAlchemyCppPath, Line, Message);
    }

    void FdumpDrawContextError(int Line, const char* Message)
    {
        FDUMP(0x100001, kFdumpErrorFmt, kDrawContextCpp, Line, Message);
    }

    // The QI table walks (0620ab50 and every twin) compare the requested IID byte by byte
    // with no null test.
    bool IidEqual(const char* Left, const char* Right)
    {
        return std::strcmp(Left, Right) == 0;
    }

    // Callers pass G_Specs[] names and string literals only, so there is no null case
    // (the old null guard had no retail counterpart).
    bool NameEqual(const char* Left, const char* Right)
    {
        return std::strcmp(Left, Right) == 0;
    }


    GENRESULT CreateNamedFx(const char* Name, IDAComponent** Out)
    {
        if (Out != nullptr)
            *Out = nullptr;
        if (Name == nullptr || Out == nullptr)
            return GR_ALE_CREATE_FAIL;
        ICOManager* Dacom = DACOM_Acquire();
        if (Dacom == nullptr)
            return GR_ALE_CREATE_FAIL;
        FAggDesc8 Desc;
        Desc.Size = 8;
        Desc.NameAt4 = Name;
        const GENRESULT Result =
            Dacom->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), reinterpret_cast<void**>(Out));
        if (Result != GR_OK || *Out == nullptr)
            return GR_ALE_CREATE_FAIL;
        return GR_OK;
    }

    // FUN_06227710 0x0622775e..0x0622776d: the found entry's prototype (+0x10) is called
    // through its outer +0x0C -- the class's clone (IGenericFx::CloneNode) -- with Out, no
    // QueryInterface, and that result is returned as is.
    GENRESULT InstantiateFromNodeLibPrototype(IDAComponent* Prototype, IDAComponent** Out)
    {
        return AsFxNode(Prototype)->CloneNode(Out);
    }


    FAlchemyDefaults G_Defaults = {kDefaultPoolSize,
                                  kDefaultPoolSize,
                                  kDefaultPoolSize,
                                  kDefaultPoolSize,
                                  kDefaultPoolSize,
                                  kDefaultPoolSize,
                                  kDefaultPoolSize,
                                  kDefaultPoolSize,
                                  static_cast<unsigned char>(kDefaultUseMaterialBatcher)};

    FAppearancePool G_PoolBasic = {};
    FAppearancePool G_PoolParticle = {};
    FAppearancePool G_PoolRect = {};
    FAppearancePool G_PoolPerp = {};
    FAppearancePool G_PoolMesh = {};
    FAppearancePool G_PoolOriented = {};
    FAppearancePool G_PoolDust = {};
    FAppearancePool G_PoolBeam = {};

    unsigned long G_MaxVertices = 4096; // FUN_06202260 / DAT_06255064
    unsigned long G_MaxIndices = 8192;  // FUN_06202270 / DAT_06255068
    unsigned long G_DrawMaxVertices = 0; // DAT_06257bd8
    unsigned long G_DrawMaxIndices = 0;  // DAT_06257bdc

    bool G_bInDraw = false;              // DAT_06257918
    IAlchemy* G_EffectAlchemy = nullptr;             // DAT_06257a3c
    IFxNodeLibrary* G_EffectNodeLibrary = nullptr;    // DAT_06257a38
    // DAT_06257b9c: IFxRuntime +0x24 LAB_0623b6a0 store / +0x28 LAB_0623b740 get+AddRef.
    // Client FUN_004f70d0 writes DAT_00674cdc = FUN_004f67e0 (8-byte, vtable 005d98bc).
    // Not IEngine. Mesh Extra2 is the OUT of factory+0x0C = FUN_004f6810, not this pointer.
    IDAComponent* G_FxMeshFactory = nullptr;
    void* G_RuntimeAlchemy = nullptr;       // DAT_06257b98
    void* G_RuntimeUnknown = nullptr;       // DAT_06257b94
    void* G_RuntimeEffectLibrary = nullptr; // DAT_06257b90
    I3DMathEngine* G_3DMath = nullptr;    // DAT_06257a6c via FUN_0623e830
    IDAComponent* G_RootNode = nullptr;  // DAT_062578b0 — GetRootNode / Detach root
    IDAComponent* G_PipelineOuter = nullptr;   // DAT_0625790c desc+8
    IRenderPipeline* G_RenderPipeline = nullptr;  // DAT_06257bc0
    IDAComponent* G_TextureLibrary = nullptr;  // DAT_06257bbc
    IDAComponent* G_RpVertexBuffer = nullptr;  // DAT_06257bc4
    IDAComponent* G_RpIndexBuffer = nullptr;   // DAT_06257bc8
    IDAComponent* G_RpDraw = nullptr;          // DAT_06257bcc
    void* G_VbHandle = nullptr;                // DAT_0625791c
    void* G_IbHandle = nullptr;
    // wave15 (sonnet:alchemy-wave15-lane3): DAT_06257908 is NOT an opaque
    // handle in retail — every one of its 12+ XREFs dereferences it as a COM
    // object (`(**(code**)(*DAT_06257908 + N))(DAT_06257908, ...)`, N in
    // {0xc,0x10,0x40}). It is populated directly from IAlchemy::DrawNodes's
    // own first stack parameter (LAB_06201910, alchemy.dll.c: `MOV
    // ECX,[ESP+8]` then `MOV [DAT_06257908],ECX`, executed before the
    // node-tree walk begins) — NOT via a DACOM_Acquire/CreateInstance call
    // like G_3DMath and its per-appearance-type siblings
    // (DAT_06257ac0/b8c/79c8, all lazily built by FUN_0623e830). This port's
    // FAlchemy::DrawNodes (alchemy_alchemy.cpp) already receives and stores
    // that exact parameter unchanged:
    //     G_DrawCookie = static_cast<unsigned long>(reinterpret_cast<unsigned int>(Cookie));
    // See DrawCameraFromCookie/GetDrawCameraPosition below for the
    // retail-verified vtable+0xc "GetPosition" method this cookie exposes —
    // consumed as a live camera/viewpoint position by at least
    // FLDustAppearance (FUN_0620fcb0, alchemy.dll.c 11485-11526, distance
    // fade) and FxRectAppearance (FUN_06238a70, alchemy.dll.c 35349-35354,
    // camera-facing axis). Recovering this needs no change to
    // FAlchemy::DrawNodes, its signature, or any file outside this one: the
    // pointer already round-trips through this unsigned long bit-for-bit on
    // this project's Win32 target (alchemy.vcxproj) — it was only ever
    // stored as a plain integer because nothing previously dereferenced it.
    unsigned long G_DrawCookie = 0;            // DAT_06257908
    unsigned long G_DrawCounters[9] = {};      // DAT_062578e4
    unsigned char G_DrawFlagA = 1;             // DAT_06255060
    unsigned char G_DrawFlagB = 1;             // DAT_06255061


    FBatchList G_BatchLists[kBatchListCount] = {};


    // Zero-initialised .data, then FUN_06240f40 from the __xc table (0x06204ec5 beam,
    // 0x0620f5d5 dust, 0x06215d55 basic, 0x0622bb95 oriented, 0x062329d5 perp, 0x06238025
    // rect) zeroes +8 / +0xa / +0xc again -- no observable difference on zeroed storage.
    FAlDrawContext G_BeamDrawContext = {};     // DAT_0625792c
    FAlDrawContext G_DustDrawContext = {};     // DAT_0625797c
    FAlDrawContext G_BasicDrawContext = {};    // DAT_062579dc
    FAlDrawContext G_OrientedDrawContext = {}; // DAT_06257a74
    FAlDrawContext G_PerpDrawContext = {};     // DAT_06257aec
    FAlDrawContext G_RectDrawContext = {};     // DAT_06257b40
    void* G_DrawVbHandle = nullptr;            // DAT_06257bd4


    // Field descriptor ramp defaults (ALC-4 2026-09-30, from the descriptor builders and
    // named by each field's inner ramp getter):
    //   gravity 0x06223408: inner +0x4c = 9.8 (0x411ccccd), GravityField_Gravity
    //     0xE02B8BD4 (getter LAB_06223310);
    //   air 0x062140c2 / 0x062140f6: +0x4c = 1.0 AirField_Approach 0x10423CEB,
    //     +0x50 = 1.0 AirField_Magnitude 0xE5E3524C (getter LAB_06213fd0).
    const float kDefaultGravity = 9.8f;
    const float kDefaultAirApproach = 1.0f;
    const float kDefaultAirMagnitude = 1.0f;
    // wave 12 (alchemy-wave12-lane2): independently re-read FUN_06235f50
    // (listing 0x06235f50-0x062361cb) byte-for-byte. It builds three
    // FxAnimatedSingleRamps (FUN_06206ce0 + FUN_06209ad0) with weights 0x41200000,
    // 0x3e99999a, 0x42480000 = 10.0f/0.3f/50.0f in that order, matching the
    // read order of RadialField_Magnitude/RadialField_Attenuation/
    // RadialField_Approach in FGenericFxObject::BeginField. Previously all
    // three were the placeholder 1.0f.
    // ALC-4: the names follow the descriptor's property table (.data 0x062569c8):
    // +0x4c RadialField_Magnitude 10.0, +0x50 RadialField_Approach 0.3,
    // +0x54 RadialField_Radius 50.0 (the old Atten/Approach names were crossed).
    const float kDefaultRadialMagnitude = 10.0f;
    const float kDefaultRadialApproach = 0.3f;
    const float kDefaultRadialRadius = 50.0f;
    // Turbulence (ALC-4 2026-09-30): the descriptor builder at 0x0623d547..0x0623d59b
    // gives inner +0x4c the 1.0 ramp and +0x50 the 10.0 ramp (FUN_06210900(10.0,
    // &+0x50)); the inner getter LAB_0623d470 names them: 0x1B970B9A -> +0x4c, 0x1CEAC6D1
    // -> +0x50, and the property table .data 0x06256ccc pairs 0x1CEAC6D1 with
    // "TurbulenceField_Magnitude" and 0x1B970B9A with "TurbulenceField_Approach"
    // (0x06256d08). So Approach = 1.0 and Magnitude = 10.0; the wave-14 reading
    // (Magnitude 1.0, Approach 10.0) had them crossed.
    const float kDefaultTurbulenceApproach = 1.0f;
    const float kDefaultTurbulenceMagnitude = 10.0f;
    // wave 14 (sonnet:alchemy-wave14-lane1): FxCollideField's real ctor
    // FUN_06219a30 (listing line 52965 / .text:06219aec) hardcodes literal
    // 0x3f333333 = 0.7f as the reflectivity default — confirmed the ONLY
    // occurrence of that literal anywhere in alchemy.dll via a whole-binary
    // grep of the raw listing. Was previously an arbitrary 1.0f placeholder.
    const float kDefaultCollideReflect = 0.7f;
    // ALC-4: the same builder's +0x50 CollideField_Width and +0x54 CollideField_Height
    // ramps, both 0x41200000 (0x06219b92, 0x06219c38).
    const float kDefaultCollideWidth = 10.0f;
    const float kDefaultCollideHeight = 10.0f;
    // kBeamRibbonHalfWidth (0.25f) removed 2026-09-15: live-debug (dbgmcp,
    // PID 41976) plus raw-listing tracing proved it was never retail's real
    // multiplier — see FillBeamRibbon's doc comment and alchemy_common.h's
    // declaration-site comment for the full evidence trail.
    // FLDustAppearance's .rdata floats (DAT_062559f4..06255a08, 0624c354/58) are read
    // inline by FLDustAppearance::DrawOrEnqueue (alchemy_appearances.cpp) as FloatFromBits
    // locals; the old shared copies here fed only the deleted FillDustAppearanceQuad /
    // ComputeDustCameraFadeAlpha lookalikes (removed 2026-09-26, ALC-2).
    const unsigned long kDustKillAgeBits = 0x7F7FFFFF;
    // FUN_0623f1f0's state (DAT_06257bac) and call counter (DAT_06257bb0).
    unsigned long G_AlchemyRandState = 0;
    unsigned long G_AlchemyRandCounter = 0;
    // DAT_0624bc98 (0x3b808081 = 1/255).
    const float kAlchemyRandScale = 0.003921568859368563f;

    void UnlinkPoolNode(FPoolLink** Head, FPoolLink** Tail, FPoolLink* Node)
    {
        FPoolLink* Prev = Node->Prev;
        FPoolLink* Next = Node->Next;
        if (Prev != nullptr)
            Prev->Next = Next;
        else
            *Head = Next;
        if (Next != nullptr)
            Next->Prev = Prev;
        else
            *Tail = Prev;
        Node->Next = nullptr;
        Node->Prev = nullptr;
    }

    void AppendPoolNode(FPoolLink** Head, FPoolLink** Tail, FPoolLink* Node)
    {
        Node->Next = nullptr;
        Node->Prev = *Tail;
        if (*Tail != nullptr)
            (*Tail)->Next = Node;
        else
            *Head = Node;
        *Tail = Node;
    }

    // wave23 (sonnet:alchemy-wave23-lane1, 2026-09-11): mechanically
    // identical to UnlinkPoolNode/AppendPoolNode above, typed for
    // FPooledParticle's own Next/Prev instead of FPoolLink's. Backs
    // FGenericFxObjectBase::InstanceListHead_/InstanceListTail_
    // (alchemy_genericfx.h) — see IntegratePool/ApplyFieldsToPool below and
    // decomp-tracker item 132111 for what this closes.
    void UnlinkParticleNode(FPooledParticle** Head, FPooledParticle** Tail, FPooledParticle* Node)
    {
        FPooledParticle* Prev = Node->Prev;
        FPooledParticle* Next = Node->Next;
        if (Prev != nullptr)
            Prev->Next = Next;
        else
            *Head = Next;
        if (Next != nullptr)
            Next->Prev = Prev;
        else
            *Tail = Prev;
        Node->Next = nullptr;
        Node->Prev = nullptr;
    }

    void AppendParticleNode(FPooledParticle** Head, FPooledParticle** Tail, FPooledParticle* Node)
    {
        Node->Next = nullptr;
        Node->Prev = *Tail;
        if (*Tail != nullptr)
            (*Tail)->Next = Node;
        else
            *Head = Node;
        *Tail = Node;
    }

    // The link-in half of FUN_0620be90 (0x0620bee4..0x0620bf04): Prev = 0,
    // Next = head, old head's Prev = node, head = node, and tail = node when the
    // tail was empty. The beam spawn prepends; everything else appends.
    void PrependParticleNode(FPooledParticle** Head, FPooledParticle** Tail, FPooledParticle* Node)
    {
        Node->Prev = nullptr;
        Node->Next = *Head;
        if (*Head != nullptr)
            (*Head)->Prev = Node;
        FPooledParticle* OldTail = *Tail;
        *Head = Node;
        if (OldTail == nullptr)
            *Tail = Node;
    }

    void ResetAppearancePoolLists(FAppearancePool* Pool)
    {
        Pool->FreeCount = 0;
        Pool->UsedHead = nullptr;
        Pool->UsedTail = nullptr;
        Pool->FreeHead = nullptr;
        Pool->FreeTail = nullptr;
    }

    void ReleaseComPtr(IDAComponent** Slot);

    void FreeAppearancePool(FAppearancePool* Pool)
    {
        if (Pool == nullptr)
            return;
        // FUN_0623eb40 (and the same prologue of FUN_0623ebc0): unlink every node of the
        // used (+0x14/+0x18) and free (+0x1c/+0x20) chains, free the item and node buffers,
        // zero the counters. No COM release of any live item -- the per-item teardown
        // (IFxMesh slot 7, the particle record's child effects) belongs to the erasers.
        // ALC-1 2026-09-26: removed the port's live-item ReleaseComPtr(+0x30 / +0x6c) walk
        // that stood here; retail has none, and on the perp/dust pools +0x30 is a float
        // triple, so the walk would Release a float.
        ::operator delete(Pool->Items);
        ::operator delete(Pool->FreeNodes);
        Pool->Items = nullptr;
        Pool->FreeNodes = nullptr;
        Pool->Stride = 0;
        Pool->Capacity = 0;
        ResetAppearancePoolLists(Pool);
    }

    unsigned long PoolUsedCount(const FAppearancePool* Pool)
    {
        if (Pool == nullptr || Pool->Capacity <= Pool->FreeCount)
            return 0;
        return Pool->Capacity - Pool->FreeCount;
    }

    FAppearancePool* PoolForAppearanceFactory(const char* Factory)
    {
        if (Factory == nullptr)
            return nullptr;
        if (_stricmp(Factory, "FxBasicAppearance") == 0)
            return &G_PoolBasic;
        if (_stricmp(Factory, "FxParticleAppearance") == 0)
            return &G_PoolParticle;
        if (_stricmp(Factory, "FxRectAppearance") == 0)
            return &G_PoolRect;
        if (_stricmp(Factory, "FxPerpAppearance") == 0)
            return &G_PoolPerp;
        if (_stricmp(Factory, "FxMeshAppearance") == 0)
            return &G_PoolMesh;
        if (_stricmp(Factory, "FxOrientedAppearance") == 0)
            return &G_PoolOriented;
        if (_stricmp(Factory, "FLDustAppearance") == 0)
            return &G_PoolDust;
        if (_stricmp(Factory, "FLBeamAppearance") == 0)
            return &G_PoolBeam;
        return nullptr;
    }

    // LAB_06219520 / LAB_06219540 (Basic +0xd0/+0xd4): live * 4 verts / * 6 idx.
    // ComputeDrawBufferHints: Beam * 5 / * 0x18. Particle/Mesh not in max VB.
    //
    // wave87 (sonnet:alchemy-wave87-lane2, 2026-09-15): FxSphereEmitter added
    // after byte-verifying its own copy of GetQueuedVertCount/
    // GetQueuedIndexCount (decomp-tracker items 134765/134766, alchemy.dll.c
    // 36669-36683, alchemy.dll.txt 119966-120001, reached via the class's own
    // this+0xf0 LiveInstanceCount-style field, `<<2` / `*6` respectively) --
    // same literal 4/6 shape as the billboard-appearance family above, just
    // implemented as a per-class hardcoded copy instead of a shared body, and
    // present even though FxSphereEmitter is an emitter, not an appearance
    // (every FGenericFxObjectBase-shaped leaf mechanically carries the full
    // IGenericFx vtable, including draw-unit slots it may never actually be
    // queried through). Before this fix, GetQueuedVertCount/GetQueuedIndexCount
    // silently returned 0 for FxSphereEmitter instances instead of matching
    // retail's 4/6 -- a real, previously-unmodeled gap, not merely an
    // unconfirmed accessor. Cone/Cube emitters were not re-audited this wave
    // (out of this wave's assigned cluster) and may carry the same gap; left
    // as an open TODO for whichever wave next touches their own copies of
    // these two vtable slots.
    void AppearanceDrawUnits(const char* Factory, unsigned long* VertsPer,
                             unsigned long* IndicesPer)
    {
        if (VertsPer != nullptr)
            *VertsPer = 0;
        if (IndicesPer != nullptr)
            *IndicesPer = 0;
        if (Factory == nullptr)
            return;
        if (_stricmp(Factory, "FLBeamAppearance") == 0)
        {
            if (VertsPer != nullptr)
                *VertsPer = 5;
            if (IndicesPer != nullptr)
                *IndicesPer = 0x18;
            return;
        }
        if (_stricmp(Factory, "FxParticleAppearance") == 0 ||
            _stricmp(Factory, "FxMeshAppearance") == 0)
            return;
        if (_stricmp(Factory, "FxBasicAppearance") == 0 ||
            _stricmp(Factory, "FxRectAppearance") == 0 ||
            _stricmp(Factory, "FxPerpAppearance") == 0 ||
            _stricmp(Factory, "FxOrientedAppearance") == 0 ||
            _stricmp(Factory, "FLDustAppearance") == 0 ||
            _stricmp(Factory, "FxSphereEmitter") == 0)
        {
            if (VertsPer != nullptr)
                *VertsPer = 4;
            if (IndicesPer != nullptr)
                *IndicesPer = 6;
        }
    }

    void* AllocPoolItem(FAppearancePool* Pool);
    void FreePoolItem(FAppearancePool* Pool, void* Item);

    // FUN_062257b0 thiscall RET 4: *slot = value; if value: AddRef (+4). No QI,
    // no Release of the previous pointer (listing 062257b0–062257c6).
    IDAComponent** AssignComPtr(IDAComponent** Slot, IDAComponent* Value)
    {
        *Slot = Value;
        if (Value != nullptr)
            Value->AddRef();
        return Slot;
    }

    // FUN_062257d0 thiscall RET 0: if *slot: Release (+8); *slot = 0.
    // XREF unwind 06248e06 / 06248e11 / 06248ed6 / 06248f06 and item+0x30.
    void ReleaseComPtr(IDAComponent** Slot)
    {
        if (*Slot == nullptr)
            return;
        (*Slot)->Release();
        *Slot = nullptr;
    }

    FPooledParticle* ParticleFromUsedLink(FAppearancePool* Pool, FPoolLink* Link)
    {
        if (Pool == nullptr || Link == nullptr || Pool->Items == nullptr ||
            Pool->FreeNodes == nullptr || Pool->Stride < 0x30)
            return nullptr;
        const unsigned long Index = static_cast<unsigned long>(Link - Pool->FreeNodes);
        if (Index >= Pool->Capacity)
            return nullptr;
        return reinterpret_cast<FPooledParticle*>(static_cast<unsigned char*>(Pool->Items) +
                                                  Index * Pool->Stride);
    }

    // FUN_0620be90 / FUN_06215f40: AllocPoolItem + 6 floats + 1/duration. Not poolSize loop.
    void* EmitOneParticle(FAppearancePool* Pool, FGenericFxObjectBase* Appearance, const float* Six, float Age,
                          float Duration)
    {
        if (Pool == nullptr || Six == nullptr || Duration == 0.0f)
            return nullptr;
        unsigned char* Item = static_cast<unsigned char*>(AllocPoolItem(Pool));
        if (Item == nullptr)
            return nullptr;
        FPooledParticle* Particle = reinterpret_cast<FPooledParticle*>(Item);
        Particle->Appearance = Appearance;
        Particle->PosX = Six[0];
        Particle->PosY = Six[1];
        Particle->PosZ = Six[2];
        Particle->VelX = Six[3];
        Particle->VelY = Six[4];
        Particle->VelZ = Six[5];
        Particle->Age = Age;
        Particle->Duration = Duration;
        Particle->InvDuration = 1.0f / Duration;
        // Mesh particles never come through here: FxMeshAppearance overrides EmitOne with
        // FUN_06224c20 (alchemy_appearances.cpp), which asks the mesh factory and fills
        // Extra2 +0x30 itself. Particle (0x74) records never come through here either:
        // FxParticleAppearance::Pad47 (FUN_0622f6a0) allocates and fills them itself.
        //
        // kStridePerp (0x3C) also satisfies Stride>=0x34 but its +0x30 tail is
        // NOT a COM pointer: FUN_06233330 reads/writes three floats there
        // (Item+0x30/+0x34/+0x38, the persisted velocity-facing billboard "up"
        // axis fed back frame to frame — see BuildPerpBillboardBasis /
        // FillPerpBillboardQuad). Treating it as a COM pointer would make
        // FreePoolItem call Release() on a bit-reinterpreted float when a Perp
        // particle dies — a real crash. Zero the float triplet instead: pool
        // storage is a raw new[] with no zero-init, so without this the very
        // first cross-product for a freshly carved slot reads heap garbage.
        // wave34 (sonnet:alchemy-wave34-lane1): kStrideDust == kStridePerp
        // (both 0x3C), so this same `==` branch also zero-inits Dust's own
        // +0x30 float triplet — FillDustAppearanceQuad's persisted
        // "previous frame CameraSpacePos" (see its doc comment) relies on
        // this exact zero-init for a freshly emitted particle's first
        // frame, the same way Perp's Axis already did. Not a coincidence to
        // "fix": kept as one shared branch rather than duplicated per pool.
        if (Pool->Stride == kStridePerp)
        {
            float* Axis = reinterpret_cast<float*>(Item + 0x30);
            Axis[0] = 0.0f;
            Axis[1] = 0.0f;
            Axis[2] = 0.0f;
        }
        else if (Pool->Stride >= 0x34)
            AssignComPtr(reinterpret_cast<IDAComponent**>(Item + 0x30), nullptr);
        return Particle;
    }

    // FUN_06206c70: identity 3x3 at [0,4,8]=1. FUN_0622ff00 then copies pos
    // to translation and, if extra+0x6c != 0, calls extra+0x0C. flags&1 quat
    // from vel is leftover (long acos/sincos). Mesh 0x34 has no extra+0x6c.
    void FillIdentity3x3(float* Nine)
    {
        if (Nine == nullptr)
            return;
        Nine[0] = 1.0f;
        Nine[1] = 0.0f;
        Nine[2] = 0.0f;
        Nine[3] = 0.0f;
        Nine[4] = 1.0f;
        Nine[5] = 0.0f;
        Nine[6] = 0.0f;
        Nine[7] = 0.0f;
        Nine[8] = 1.0f;
    }

    // The math engine's Rotate (+0x30) on the 12-float FTransform layout (3x3 row-major,
    // then translation): Out = R * (X, Y, Z). Every retail caller (field Begins gravity
    // 0x0622424b, air, collide 0x0621ab79) goes through the engine singleton
    // (FUN_0623e830, cached per call site), so the port calls the same engine rather
    // than an inline product with its own rounding (ALC-5, 2026-09-30).
    void TransformDirection(const float* Xform12, float X, float Y, float Z, float* Out)
    {
        const FVector Source = { X, Y, Z };
        Acquire3DMath()->Rotate(*reinterpret_cast<FVector*>(Out),
                                *reinterpret_cast<const FTransform*>(Xform12), Source);
    }

    // The math engine's TransformXform (+0x28): Out = R * (X, Y, Z) + T (radial Begin
    // 0x06236f19.., collide Begin 0x0621ac0b, list bounds sphere).
    void TransformPoint(const float* Xform12, float X, float Y, float Z, float* Out)
    {
        const FVector Source = { X, Y, Z };
        Acquire3DMath()->TransformXform(*reinterpret_cast<FVector*>(Out),
                                        *reinterpret_cast<const FTransform*>(Xform12), Source);
    }

    // The lerp every retail field Apply inlines (e.g. radial 0x062366c8..0x062366fb:
    // FLD T; FSUB V; FMUL Alpha; FADD V; FSTP float per axis -- one rounding, at the
    // store). All callers pass PosVel6 + 3 and a stack/member array, so there is no null
    // case (the old null guard had no retail counterpart).
    void LerpVel3(float* Vel, const float* Target, float Alpha)
    {
        LerpVel3(Vel, static_cast<double>(Target[0]), Target[1], Target[2], Alpha);
    }

    // The same lerp when the x target never left the FPU (radial 0x062366a9 / turbulence
    // 0x0623e427: FMUL into ST0, then FSUB [vel.x] on it); y and z were FSTP'd to float.
    void LerpVel3(float* Vel, double TargetX, float TargetY, float TargetZ, float Alpha)
    {
        Vel[0] = static_cast<float>((TargetX - Vel[0]) * Alpha + Vel[0]);
        Vel[1] = static_cast<float>((static_cast<double>(TargetY) - Vel[1]) * Alpha + Vel[1]);
        Vel[2] = static_cast<float>((static_cast<double>(TargetZ) - Vel[2]) * Alpha + Vel[2]);
    }

    // ALC-1 2026-09-26: MultiplyQuatWxyz / SlerpQuatWxyz / ApplySmoothRotationSlerp /
    // WriteVelOrientQuat were removed with IntegratePool's particle branch. They were a
    // lookalike of FUN_0622ff00 (nlerp for the engine's QuatNlerp, acos(N.y) on the
    // |N.y| >= 0.99 side where retail takes acos(N.x), an invented zero-length guard);
    // the listing port is FxParticleAppearance::IntegrateRecord (alchemy_appearances.cpp).

    // FUN_0623f1f0: the module's shift-register generator. State S = DAT_06257bac,
    // call counter C = DAT_06257bb0 (both 0 at load).
    //   Mixed = ((S & 0xff) << 8) ^ (S & 0xff00) | ((C ^ S) & 0xff)
    //   Bit   = ((S >> 14) & 1) ^ (S & 1);  C += 1
    //   S     = Bit + Mixed * 2
    //   return ((S & 0xff) ^ ((S >> 8) & 0xff)) * DAT_0624bc98 (1/255), in [0, 1].
    // Replaces an LCG (1664525 / 1013904223) that no retail instruction matches.
    // The result is FILD qword * FMUL float (0x0623f250..0x0623f254) left in ST0: an
    // unrounded x87 value, so it is returned as double (ALC-5; it used to be rounded to
    // float here). The product of an 8-bit integer and a 24-bit mantissa is exact in
    // double.
    double AlchemyRand01()
    {
        const unsigned long State = G_AlchemyRandState;
        unsigned long Mixed = ((State & 0xFFu) << 8) ^ (State & 0xFF00u);
        Mixed |= (G_AlchemyRandCounter ^ State) & 0xFFu;
        const unsigned long Bit = ((State >> 14) & 1u) ^ (State & 1u);
        G_AlchemyRandCounter = G_AlchemyRandCounter + 1;
        const unsigned long Next = Bit + Mixed * 2;
        G_AlchemyRandState = Next;
        const unsigned long Byte = (Next & 0xFFu) ^ ((Next >> 8) & 0xFFu);
        return static_cast<double>(static_cast<long>(Byte)) *
               static_cast<double>(kAlchemyRandScale);
    }

    // ALC-3 2026-09-30: ComputeRampSlopes / SampleHermite (a lookalike of FUN_06246550 /
    // FUN_06246950 with invented guards) were removed; the listing ports are
    // FFxAnimatedSingle::ComputeTrackSlopes / EvalSegment (alchemy_ramp.cpp).

    // FUN_0620bda0 (age/pos formula) + FUN_062188c0 (confirmed retail
    // traversal root — decomp-tracker items 132111/132872): age += dt; if
    // age <= duration then pos += vel * dt; else unlink-from-owner's-list
    // and free. (The particle appearance's FUN_06230370 / FUN_0622ff00 are not
    // this function -- see FxParticleAppearance::Pad5 / IntegrateRecord.)
    //
    // alchemy-wave14-lane2 (2026-09-10) re-verified an earlier wave's claim
    // that "no actual particle array exists" in this port: FALSE as stated —
    // this function plus AllocPoolItem/FreePoolItem/EmitOneParticle/
    // ApplyFieldsToPool below already implement a real per-particle pool
    // (age/position/velocity integration, freelist recycling, orientation
    // quaternion) and were correctly closed ready_unverified back in wave 11.
    // Cross-checking retail bytes at 0x0623a000/a070/a190/a280/a2a0/a2e0/
    // a360/a4a0/a590/b410 and the pool-ctor/dtor pair at 0x0623ea30/e890/
    // e950/eb40 found those addresses are a genuinely SEPARATE mechanism —
    // Cluster A, a per-emitter-NODE private-instance tracking system
    // (AllocPrivatePoolItem/FreePrivatePoolItem below already give it a
    // pool-arena carve-out; the node-side list/refcount fields themselves
    // still belong on the emitter leaves, alchemy_emitters.*, out of this
    // lane's ownership — see alchemy_genericfx.cpp's Pad25 doc comment for
    // the full Cluster A/B trace) — NOT this function's own subject.
    //
    // wave23 (sonnet:alchemy-wave23-lane1, 2026-09-11): this function itself
    // used to walk Pool->UsedHead (every live particle of this FACTORY
    // across every instance sharing the pool) once per ANY instance's Pad25
    // tick — a wave-11 simplification substituting for retail's real
    // per-OWNER-INSTANCE sentinel list, confirmed by wave19/20/21/22's
    // extensive byte trace (decomp-tracker item 132111) and independently
    // reconfirmed by FIVE separate retail call sites (FUN_06215f40 emit-
    // and-link, FUN_062188c0 per-instance age/integrate, FUN_06217ae0
    // unlink+free, FUN_06219070 destructor erase-range, FUN_06218780
    // per-instance field-apply — items 132827/132872/132839/132921/132871 —
    // plus FUN_06230370's independently-retraced parallel lineage on a
    // different concrete leaf class). Now walks Owner->InstanceListHead_
    // (FGenericFxObjectBase, alchemy_genericfx.h) — this instance's OWN
    // active particles, threaded through FPooledParticle::Next/Prev
    // (alchemy_types.h) — instead. FAppearancePool's own Used/Free
    // bookkeeping (Pool->UsedHead/UsedTail/FreeHead/FreeTail, a physically
    // separate FPoolLink*-typed list) is untouched: FreePoolItem below still
    // unlinks the freed slot from Pool->UsedHead and appends it to
    // Pool->FreeHead exactly as before, only reached via
    // Owner->InstanceListHead_ now instead of a direct Pool->UsedHead walk.
    //
    // NOT YET HANDLED (decomp-tracker item 132111, step 8 — deliberately
    // deferred, see ~FGenericFxObjectBase's TODO in alchemy_genericfx.cpp):
    // an instance destroyed while InstanceListHead_ still has live nodes
    // leaks those particles permanently (no other instance's Owner-scoped
    // walk can ever reach them again) — this function's own per-Owner
    // scoping is exactly why that gap is now load-bearing rather than
    // cosmetic.
    // ALC-1 2026-09-26: the G_PoolParticle (stride 0x74) orientation branch that lived
    // here, with its WriteVelOrientQuat / ApplySmoothRotationSlerp / SlerpQuatWxyz helpers
    // and OrientFlags parameter, was removed: FxParticleAppearance has its own update
    // (FUN_06230370 -> FxParticleAppearance::Pad5) and integrate (FUN_0622ff00 ->
    // IntegrateRecord), so this function is only reached for the billboard pools and the
    // beam pool, neither of which carries the 0x74 tail.
    //
    // FUN_062188c0 (basic; perp FUN_06234c70, rect FUN_0623a4a0, oriented FUN_0622de40 are
    // the same instructions): for each item, Age = Dt + Age (0x06218912); FLD Age / FCOMP
    // Duration / AND 0x4100 / JNZ -> not greater (or unordered) integrates pos += vel * Dt,
    // greater erases through FUN_06217ae0. The erase unlinks, decrements the live count of
    // the item's own appearance ([item+8] + 0xe4, only when [item+8] is non-null) and
    // frees the slot to the pool (FUN_0623ecf0). No Dt, pool or count guards.
#pragma optimize("", off)
#pragma float_control(except, on, push)
    __declspec(noinline) void IntegratePool(FGenericFxObjectBase* Owner, FAppearancePool* Pool,
                                           const float& Dt)
    {
        FPooledParticle* Particle = Owner->InstanceListHead_;
        while (Particle != nullptr)
        {
            Particle->Age = static_cast<float>(static_cast<double>(Dt) + Particle->Age);
            if (Particle->Age > Particle->Duration)
            {
                FPooledParticle* const Next = Particle->Next;
                UnlinkParticleNode(&Owner->InstanceListHead_, &Owner->InstanceListTail_, Particle);
                FGenericFxObjectBase* const ItemAppearance =
                    static_cast<FGenericFxObjectBase*>(Particle->Appearance);
                if (ItemAppearance != nullptr)
                    --ItemAppearance->LiveInstanceCount_;
                ReleaseAppearancePoolItem(Pool, Particle);
                Particle = Next;
            }
            else
            {
                // 06218950..1897f rounds only the Z product before all three additions.
                const double DeltaX = static_cast<double>(Dt) * Particle->VelX;
                const double DeltaY = static_cast<double>(Dt) * Particle->VelY;
                const float DeltaZ = static_cast<float>(static_cast<double>(Dt) * Particle->VelZ);
                Particle->PosX = static_cast<float>(DeltaX + Particle->PosX);
                Particle->PosY = static_cast<float>(DeltaY + Particle->PosY);
                Particle->PosZ = static_cast<float>(static_cast<double>(DeltaZ) + Particle->PosZ);
                Particle = Particle->Next;
            }
        }
    }
#pragma float_control(pop)
#pragma optimize("", on)

    // FUN_0623e830 creates the service; its callers retain their own lazy caches.
    I3DMathEngine* Create3DMath()
    {
        FMathDesc Descriptor;
        Descriptor.Size = sizeof(Descriptor);
        Descriptor.InterfaceName = "3DMathEngine";
        Descriptor.Implementation = nullptr;
        I3DMathEngine* Instance = nullptr;
        DACOM_Acquire()->CreateInstance(reinterpret_cast<AGGDESC*>(&Descriptor),
                                        reinterpret_cast<void**>(&Instance));
        return Instance;
    }

    I3DMathEngine* Acquire3DMath()
    {
        if (G_3DMath != nullptr)
            return G_3DMath;
        ICOManager* Manager = DACOM_Acquire();
        if (Manager == nullptr)
            return nullptr;
        struct FMathDesc12
        {
            unsigned long Size;
            const char* NameAt4;
            const char* Field8;
        };
        FMathDesc12 Desc;
        Desc.Size = 0xC;
        Desc.NameAt4 = "3DMathEngine";
        Desc.Field8 = nullptr;
        void* Instance = nullptr;
        if (Manager->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &Instance) != GR_OK)
            return nullptr;
        G_3DMath = static_cast<I3DMathEngine*>(Instance);
        return G_3DMath;
    }

    // DrawNodes forwards the caller's canonical camera through the draw cookie.
    ICamera* DrawCameraFromCookie()
    {
        return (ICamera*)G_DrawCookie;
    }

    bool GetDrawCameraPosition(float* Out)
    {
        if (Out == nullptr)
            return false;
        ICamera* Camera = DrawCameraFromCookie();
        if (Camera == nullptr)
            return false;
        const float* Pos = Camera->GetPosition();
        if (Pos == nullptr)
            return false;
        Out[0] = Pos[0];
        Out[1] = Pos[1];
        Out[2] = Pos[2];
        return true;
    }

    const float* GetDrawCameraTransform()
    {
        ICamera* Camera = DrawCameraFromCookie();
        if (Camera == nullptr)
            return nullptr;
        return Camera->GetViewTransform();
    }

    const float* GetDrawCameraOrientation()
    {
        ICamera* Camera = DrawCameraFromCookie();
        if (Camera == nullptr)
            return nullptr;
        return Camera->GetOrientation();
    }

    // FUN_0622d2e0: independent DAT_06257ac0 lazy creator, unchecked native
    // GeneralInverse(temporary, OutW, Source), then the complete 48-byte raw copy.
    // Perp/Rect/Oriented fills use the resulting local transform for camera position.
    static I3DMathEngine* G_Inverse3DMath = nullptr;
    static I3DMathEngine* G_PerpBounds3DMath = nullptr; // DAT_06257978, 08640/08690.

    FVector* TransformPerpBoundsCenter(const FTransform& Transform, FVector& Out,
                                       const FVector& Input)
    {
        if (G_PerpBounds3DMath == nullptr)
            G_PerpBounds3DMath = Create3DMath();
        FVector Temporary;
        G_PerpBounds3DMath->TransformXform(Temporary, Transform, Input);
        std::memcpy(&Out, &Temporary, sizeof(Out));
        return &Out;
    }

    FVector* TransformOrientedBoundsCenter(const FTransform& Transform, FVector& Out,
                                           const FVector& Input)
    {
        if (G_Inverse3DMath == nullptr)
            G_Inverse3DMath = Create3DMath();
        G_Inverse3DMath->TransformXform(Out, Transform, Input);
        return &Out;
    }

    FTransform* ComputeGeneralInverseTransform(const FTransform& Source,
                                              FTransform& Out, float& OutW)
    {
        if (G_Inverse3DMath == nullptr)
            G_Inverse3DMath = Create3DMath();
        FTransform Temporary;
        G_Inverse3DMath->GeneralInverse(Temporary, OutW, Source);
        std::memcpy(&Out, &Temporary, sizeof(Out));
        return &Out;
    }

    void* AllocPoolItem(FAppearancePool* Pool)
    {
        // FUN_0623ec90. Retail XREF only from emit-one (FUN_0620be90 / FUN_06211440 /
        // FUN_06215f40 / FUN_06234ab0 / FUN_0623a2e0): copy 6 floats + lifetime.
        // Not IAlchemy::Initialize / BeginDraw / IFxEffect+0x48. No poolSize one-shot.
        if (Pool->FreeHead == nullptr)
            return nullptr;
        FPoolLink* Node = Pool->FreeHead;
        Pool->FreeCount -= 1;
        UnlinkPoolNode(&Pool->FreeHead, &Pool->FreeTail, Node);
        AppendPoolNode(&Pool->UsedHead, &Pool->UsedTail, Node);
        const unsigned long LinkOffset = static_cast<unsigned long>(
            reinterpret_cast<std::uintptr_t>(Node) -
            reinterpret_cast<std::uintptr_t>(Pool->FreeNodes));
        const unsigned long ItemOffset = (LinkOffset >> 3) * Pool->Stride;
        return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(Pool->Items) +
                                       ItemOffset);
    }

    void FreePoolItem(FAppearancePool* Pool, void* Item)
    {
        const unsigned long Offset = static_cast<unsigned long>(
            reinterpret_cast<std::uintptr_t>(Item) -
            reinterpret_cast<std::uintptr_t>(Pool->Items));
        const unsigned long Index = Offset / Pool->Stride;
        FPoolLink* const Node = &Pool->FreeNodes[Index];
        UnlinkPoolNode(&Pool->UsedHead, &Pool->UsedTail, Node);
        AppendPoolNode(&Pool->FreeHead, &Pool->FreeTail, Node);
        ++Pool->FreeCount;
    }

    template <typename ComponentType> struct FPoolComponentSlotOwner
    {
        ComponentType** Slot;

        void Release()
        {
            ComponentType** const Current = Slot;
            Slot = nullptr;
            if (*Current != nullptr)
            {
                (*Current)->Release();
                *Current = nullptr;
            }
        }

        ~FPoolComponentSlotOwner() noexcept(false)
        {
            if (Slot != nullptr)
                Release();
        }
    };

    void ReleaseAppearancePoolItem(FAppearancePool* Pool, void* Item)
    {
        unsigned char* const Bytes = static_cast<unsigned char*>(Item);
        if (Pool == &G_PoolMesh)
        {
            IFxMesh** const MeshSlot = reinterpret_cast<IFxMesh**>(Bytes + 0x30);
            FPoolComponentSlotOwner<IFxMesh> MeshOwner = {MeshSlot};
            if (*MeshSlot != nullptr)
                (*MeshSlot)->Slot7();
            MeshOwner.Release();
        }
        else if (Pool->Stride != kStridePerp && Pool->Stride >= 0x34 &&
                 Pool != &G_PoolParticle)
        {
            ReleaseComPtr(reinterpret_cast<IDAComponent**>(Bytes + 0x30));
        }
        if (Pool == &G_PoolParticle)
        {
            IFxEffect** const Life = reinterpret_cast<IFxEffect**>(Bytes + 0x6c);
            IFxEffect** const Death = reinterpret_cast<IFxEffect**>(Bytes + 0x70);
            FPoolComponentSlotOwner<IFxEffect> LifeOwner = {Life};
            FPoolComponentSlotOwner<IFxEffect> DeathOwner = {Death};
            if (*Life != nullptr)
                (*Life)->Slot13(reinterpret_cast<void*>(7));
            if (*Death != nullptr)
                (*Death)->Slot13(reinterpret_cast<void*>(7));
            DeathOwner.Release();
            LifeOwner.Release();
        }
        FreePoolItem(Pool, Item);
    }

    // AllocPrivatePoolItem / FreePrivatePoolItem (wave19) were removed on
    // 2026-09-26 with the private particle list they served: retail keeps one list
    // per appearance node (node+0xd8), allocated straight from the pool with
    // FUN_0623ec90 and left on the pool's used chain -- see
    // FGenericFxObjectBase::SpawnListParticle.

    void AllocateAppearancePool(FAppearancePool* Pool, unsigned long Count, unsigned long Stride)
    {
        while (Pool->UsedHead != nullptr)
        {
            FPoolLink* const Node = Pool->UsedHead;
            Pool->UsedHead = Node->Next;
            Node->Next = nullptr;
            Node->Prev = nullptr;
        }
        Pool->UsedHead = nullptr;
        Pool->UsedTail = nullptr;
        while (Pool->FreeHead != nullptr)
        {
            FPoolLink* const Node = Pool->FreeHead;
            Pool->FreeHead = Node->Next;
            Node->Next = nullptr;
            Node->Prev = nullptr;
        }
        Pool->FreeHead = nullptr;
        Pool->FreeTail = nullptr;
        if (Pool->Items != nullptr)
            ::operator delete(Pool->Items);
        Pool->Items = nullptr;
        if (Pool->FreeNodes != nullptr)
            ::operator delete(Pool->FreeNodes);
        Pool->FreeNodes = nullptr;
        Pool->Stride = Stride;
        Pool->Capacity = Count;
        Pool->FreeCount = Count;
        const unsigned long ItemBytes = Count * Stride;
        Pool->Items = ::operator new(ItemBytes);
        const unsigned long LinkBytes = Count * sizeof(FPoolLink);
        Pool->FreeNodes = static_cast<FPoolLink*>(::operator new(LinkBytes));
        unsigned long Index = 0;
        for (Index = 0; Index < Pool->Capacity; ++Index)
            AppendPoolNode(&Pool->FreeHead, &Pool->FreeTail, &Pool->FreeNodes[Index]);
    }

    void FreeAllAppearancePools()
    {
        FreeAppearancePool(&G_PoolOriented);
        FreeAppearancePool(&G_PoolMesh);
        FreeAppearancePool(&G_PoolRect);
        FreeAppearancePool(&G_PoolPerp);
        FreeAppearancePool(&G_PoolParticle);
        FreeAppearancePool(&G_PoolBasic);
        FreeAppearancePool(&G_PoolDust);
        FreeAppearancePool(&G_PoolBeam);
    }

    unsigned long MaxULong(unsigned long Left, unsigned long Right)
    {
        return Left >= Right ? Left : Right;
    }

    static unsigned long MaxSignedDword(unsigned long Left, unsigned long Right)
    {
        return static_cast<long>(Left) > static_cast<long>(Right) ? Left : Right;
    }

    // FUN_06202260: the VB hint and draw-context limit have distinct initial values.
    static void SetAlchemyMaxVertices(unsigned long Value)
    {
        G_MaxVertices = Value;
        G_DrawMaxVertices = Value;
    }

    // FUN_06202270: the IB hint and draw-context limit have distinct initial values.
    static void SetAlchemyMaxIndices(unsigned long Value)
    {
        G_MaxIndices = Value;
        G_DrawMaxIndices = Value;
    }

    // FUN_06203480: products wrap to 32 bits before the signed JLE/JG comparisons.
    // Basic/Rect/Perp/Oriented/Dust use *4/*6; Beam uses *5/*0x18.
    void ComputeDrawBufferHints()
    {
        unsigned long Vertices = G_Defaults.BasicPoolSize * 4;
        unsigned long Indices = G_Defaults.BasicPoolSize * 6;
        Vertices = MaxSignedDword(Vertices, G_Defaults.RectPoolSize * 4);
        Indices = MaxSignedDword(Indices, G_Defaults.RectPoolSize * 6);
        Vertices = MaxSignedDword(Vertices, G_Defaults.PerpPoolSize * 4);
        Indices = MaxSignedDword(Indices, G_Defaults.PerpPoolSize * 6);
        Vertices = MaxSignedDword(Vertices, G_Defaults.OrientedPoolSize * 4);
        Indices = MaxSignedDword(Indices, G_Defaults.OrientedPoolSize * 6);
        Vertices = MaxSignedDword(Vertices, G_Defaults.DustPoolSize * 4);
        Indices = MaxSignedDword(Indices, G_Defaults.DustPoolSize * 6);
        Vertices = MaxSignedDword(Vertices, G_Defaults.BeamPoolSize * 5);
        Indices = MaxSignedDword(Indices, G_Defaults.BeamPoolSize * 0x18);
        SetAlchemyMaxVertices(Vertices);
        SetAlchemyMaxIndices(Indices);
    }


    GENRESULT CallIfaceQi(IDAComponent* Outer, const char* Iid, IDAComponent** Out)
    {
        if (Out != nullptr)
            *Out = nullptr;
        if (Outer == nullptr || Iid == nullptr || Out == nullptr)
            return GR_GENERIC;
        return Outer->QueryInterface(Iid, reinterpret_cast<void**>(Out));
    }

    void ReleaseCachedIface(IDAComponent* Ptr)
    {
        if (Ptr != nullptr)
            Ptr->Release();
    }

    GENRESULT CallCreateVb(IDAComponent* Vb, unsigned int Fvf, unsigned int Count, void** Out,
                           unsigned int Flags)
    {
        if (Vb == nullptr || Out == nullptr)
            return GR_GENERIC;
        return static_cast<IRPVertexBuffer*>(Vb)->CreateVb(
            Fvf, Count, reinterpret_cast<unsigned int**>(Out), Flags);
    }

    GENRESULT CallDestroyVb(IDAComponent* Vb, void** Handle)
    {
        if (Vb == nullptr || Handle == nullptr)
            return GR_GENERIC;
        return static_cast<IRPVertexBuffer*>(Vb)->DestroyVb(Handle);
    }

    GENRESULT CallSelectVb(IDAComponent* Vb, void* Handle)
    {
        if (Vb == nullptr)
            return GR_GENERIC;
        return static_cast<IRPVertexBuffer*>(Vb)->SelectVb(Handle);
    }

    GENRESULT CallGetRenderState(IRenderPipeline* Pipeline, int State, int* Out)
    {
        if (Pipeline == nullptr)
            return GR_GENERIC;
        return Pipeline->GetRenderState((D3DRENDERSTATETYPE)State, Out);
    }

    GENRESULT CallSetRenderState(IRenderPipeline* Pipeline, int State, int Value)
    {
        if (Pipeline == nullptr)
            return GR_GENERIC;
        return Pipeline->SetRenderState((D3DRENDERSTATETYPE)State, Value);
    }

    // LAB_06201330. Null IRPVB → no CreateVb (no live device required to compile).
    GENRESULT RecreateAlchemyVertexBuffer()
    {
        if (G_RpVertexBuffer == nullptr)
        {
            G_VbHandle = nullptr;
            return GR_GENERIC;
        }
        if (G_VbHandle != nullptr)
        {
            CallDestroyVb(G_RpVertexBuffer, &G_VbHandle);
            G_VbHandle = nullptr;
        }
        const GENRESULT Result =
            CallCreateVb(G_RpVertexBuffer, kAlchemyVbFvf, G_MaxVertices, &G_VbHandle, 1);
        if (Result < 0)
        {
            FdumpAlchemyError(0x43, kMsgCreateVb);
            if (G_RpVertexBuffer != nullptr)
                CallDestroyVb(G_RpVertexBuffer, &G_VbHandle);
            G_VbHandle = nullptr;
            return GR_GENERIC;
        }
        return GR_OK;
    }

    void ClearPipelineCache()
    {
        G_PipelineOuter = nullptr;
        G_RenderPipeline = nullptr;
        G_TextureLibrary = nullptr;
        G_RpVertexBuffer = nullptr;
        G_RpIndexBuffer = nullptr;
        G_RpDraw = nullptr;
        G_VbHandle = nullptr;
        G_IbHandle = nullptr;
    }

    GENRESULT CallLockVb(IDAComponent* Vb, void* Handle, unsigned int* Offset, void** Out,
                         unsigned int Count)
    {
        if (Vb == nullptr)
            return GR_GENERIC;
        return static_cast<IRPVertexBuffer*>(Vb)->LockVb(Handle, Offset, Out, Count);
    }

    GENRESULT CallUnlockVb(IDAComponent* Vb, void* Handle)
    {
        if (Vb == nullptr)
            return GR_GENERIC;
        return static_cast<IRPVertexBuffer*>(Vb)->UnlockVb(Handle);
    }

    GENRESULT CallLockIb(IDAComponent* Ib, void* Handle, unsigned int* Offset, void** Out,
                         unsigned int Count)
    {
        if (Ib == nullptr)
            return GR_GENERIC;
        return static_cast<IRPIndexBuffer*>(Ib)->LockIb(Handle, Offset, Out, Count);
    }

    GENRESULT CallUnlockIb(IDAComponent* Ib, void* Handle)
    {
        if (Ib == nullptr)
            return GR_GENERIC;
        return static_cast<IRPIndexBuffer*>(Ib)->UnlockIb(Handle);
    }

    // FUN_06241180 0x062411ee pushes (-1, [ctx+0x20], 0, 0): the base vertex index is the
    // offset the VB Lock returned.
    GENRESULT CallSelectIb(IDAComponent* Ib, void* Handle, unsigned int BaseVertexIndex)
    {
        if (Ib == nullptr)
            return GR_GENERIC;
        return static_cast<IRPIndexBuffer*>(Ib)->SelectIb(Handle, BaseVertexIndex, 0, 0);
    }

    GENRESULT CallDrawIndexed(IDAComponent* Draw, unsigned long Prim, unsigned int MinIndex,
                              unsigned int NumVerts, unsigned int StartIndex, unsigned int Count)
    {
        if (Draw == nullptr)
            return GR_GENERIC;
        return static_cast<IRPDraw*>(Draw)->DrawIndexedPrimitive(Prim, MinIndex, NumVerts,
                                                                  StartIndex, Count);
    }

    // FUN_06208640, __thiscall with this = A, RET 8 (Out, B): MulTransform (math engine
    // vt+0x1c, lazily cached in DAT_06257978 -- the same engine Acquire3DMath hands out)
    // into a stack local, then a 12-dword copy to Out. Callers: beam 0x0620592d, dust
    // 0x0620fd3f, basic 0x06216629, mesh 0x062253a9, oriented 0x0622d079.
    void ComposeTransform(float* Out12, const float* A12, const float* B12)
    {
        if (G_PerpBounds3DMath == nullptr)
            G_PerpBounds3DMath = Create3DMath();
        FTransform Result;
        G_PerpBounds3DMath->MulTransform(Result, *reinterpret_cast<const FTransform*>(A12),
                                       *reinterpret_cast<const FTransform*>(B12));
        std::memcpy(Out12, &Result, sizeof(Result));
    }

    // FUN_06244f30 (the frame lookup the draws make) is FFxTextureWrapper::LookupFrame
    // (alchemy_genericfx.cpp) since ALC-4; the free helper that took a bare handle and
    // added null tests retail does not make is gone.

    // FUN_06208aa0 (thiscall on a draw context, RET 0x18), 0x06208aa0..0x06208af4:
    //   (u16 count at +4, zero-extended) > [+0x18] (signed JG) -> return (u16)[+0x18], nothing
    //   written; else the 6 dwords go to [+0x10], [+0x10] += 0x18, [+4] += 1, return the count
    //   before the increment. No null test on the write pointer.
    unsigned short AlSubmitVertex(FAlDrawContext& Context, float X, float Y, float Z,
                                  unsigned long Diffuse, float U, float V)
    {
        const long Count = Context.VertCount;
        if (Count > static_cast<long>(Context.RequestedVerts))
            return static_cast<unsigned short>(Context.RequestedVerts);
        FAlchemyVertex* const Vert = static_cast<FAlchemyVertex*>(Context.LockedVerts);
        Vert->X = X;
        Vert->Y = Y;
        Vert->Z = Z;
        Vert->Diffuse = Diffuse;
        Vert->U = U;
        Vert->V = V;
        Context.LockedVerts = Vert + 1;
        Context.VertCount = static_cast<unsigned short>(Count + 1);
        return static_cast<unsigned short>(Count);
    }

    // FUN_06208b50 (thiscall, RET 0x10), 0x06208b50..0x06208bb2: (u16 [+6]) + 5 > [+0x1c]
    // (signed JG) -> nothing; else indices I0, I1, I2, I2, I1, I3 at [+0x14], [+6] += 6,
    // [+0xc] += 2, [+0x14] += 12.
    void AlSubmitQuadIndices(FAlDrawContext& Context, unsigned short I0, unsigned short I1,
                             unsigned short I2, unsigned short I3)
    {
        if (static_cast<long>(Context.IndexCount) + 5 > static_cast<long>(Context.RequestedIndices))
            return;
        unsigned short* const Index = static_cast<unsigned short*>(Context.LockedIndices);
        Index[0] = I0;
        Index[1] = I1;
        Index[2] = I2;
        Index[3] = I2;
        Index[4] = I1;
        Index[5] = I3;
        Context.IndexCount = static_cast<unsigned short>(Context.IndexCount + 6);
        Context.PrimCount = static_cast<unsigned short>(Context.PrimCount + 2);
        Context.LockedIndices = Index + 6;
    }

    // FUN_06208b00 (thiscall, RET 0xc), 0x06208b00..0x06208b45: (u16 [+6]) + 2 > [+0x1c]
    // (signed JG) -> nothing; else indices I0, I1, I2 at [+0x14], [+6] += 3, [+0xc] += 1,
    // [+0x14] += 6.
    void AlSubmitTriangleIndices(FAlDrawContext& Context, unsigned short I0, unsigned short I1,
                                 unsigned short I2)
    {
        if (static_cast<long>(Context.IndexCount) + 2 > static_cast<long>(Context.RequestedIndices))
            return;
        unsigned short* const Index = static_cast<unsigned short*>(Context.LockedIndices);
        Index[0] = I0;
        Index[1] = I1;
        Index[2] = I2;
        Context.IndexCount = static_cast<unsigned short>(Context.IndexCount + 3);
        Context.PrimCount = static_cast<unsigned short>(Context.PrimCount + 1);
        Context.LockedIndices = Index + 3;
    }

    static float FloatFromBits(unsigned long Bits)
    {
        float Value;
        std::memcpy(&Value, &Bits, sizeof(Value));
        return Value;
    }

    // FUN_0623f590. Scalar SSE intrinsics preserve each MULSS/ADDSS store
    // precision and CVTTSS2SI's indefinite result, without C++ conversion UB.
    static void ComputeSinCosSse(float AngleRadians, float* Out)
    {
        unsigned long AngleBits;
        std::memcpy(&AngleBits, &AngleRadians, sizeof(AngleBits));
        const unsigned long AbsBits = AngleBits & 0x7FFFFFFFu;
        float AbsAngle;
        std::memcpy(&AbsAngle, &AbsBits, sizeof(AbsAngle));
        __m128 CosValue = _mm_load_ss(&AbsAngle);
        CosValue = _mm_mul_ss(CosValue, _mm_set_ss(FloatFromBits(0x3F22F983u)));
        __m128 SinValue = CosValue;
        CosValue = _mm_add_ss(CosValue, _mm_set_ss(1.0f));
        const int CosQuadrant = _mm_cvtt_ss2si(CosValue);
        const int SinQuadrant = _mm_cvtt_ss2si(SinValue);
        const unsigned long CosSign =
            (static_cast<unsigned long>(CosQuadrant) << 30) & 0x80000000u;
        const unsigned long SinSign = (AngleBits & 0x80000000u) ^
            ((static_cast<unsigned long>(SinQuadrant) << 30) & 0x80000000u);
        const __m128 CosQuadrantFloat = _mm_cvtsi32_ss(_mm_setzero_ps(), CosQuadrant);
        const __m128 SinQuadrantFloat = _mm_cvtsi32_ss(_mm_setzero_ps(), SinQuadrant);
        CosValue = _mm_sub_ss(CosValue, CosQuadrantFloat);
        if ((CosQuadrant & 1) != 0)
            CosValue = _mm_sub_ss(_mm_set_ss(1.0f), CosValue);
        SinValue = _mm_sub_ss(SinValue, SinQuadrantFloat);
        if ((SinQuadrant & 1) != 0)
            SinValue = _mm_sub_ss(_mm_set_ss(1.0f), SinValue);

        __m128 CosFactor = CosValue;
        CosValue = _mm_mul_ss(CosValue, CosValue);
        __m128 SinFactor = SinValue;
        SinValue = _mm_mul_ss(SinValue, SinValue);
        std::memcpy(&Out[0], &CosSign, sizeof(CosSign));
        const __m128 CosSquare = CosValue;
        CosValue = _mm_mul_ss(CosValue, _mm_set_ss(FloatFromBits(0xBB996966u)));
        const __m128 SinSquare = SinValue;
        SinValue = _mm_mul_ss(SinValue, _mm_set_ss(FloatFromBits(0xBB996966u)));
        CosValue = _mm_add_ss(CosValue, _mm_set_ss(FloatFromBits(0x3DA335E3u)));
        std::memcpy(&Out[1], &SinSign, sizeof(SinSign));
        SinValue = _mm_add_ss(SinValue, _mm_set_ss(FloatFromBits(0x3DA335E3u)));
        CosValue = _mm_mul_ss(CosValue, CosSquare);
        const __m128 CosSignValue = _mm_load_ss(&Out[0]);
        SinValue = _mm_mul_ss(SinValue, SinSquare);
        const __m128 SinSignValue = _mm_load_ss(&Out[1]);
        CosValue = _mm_add_ss(CosValue, _mm_set_ss(FloatFromBits(0xBF255DE7u)));
        SinValue = _mm_add_ss(SinValue, _mm_set_ss(FloatFromBits(0xBF255DE7u)));
        CosValue = _mm_mul_ss(CosValue, CosSquare);
        CosFactor = _mm_or_ps(CosFactor, CosSignValue);
        SinValue = _mm_mul_ss(SinValue, SinSquare);
        SinFactor = _mm_or_ps(SinFactor, SinSignValue);
        CosValue = _mm_add_ss(CosValue, _mm_set_ss(FloatFromBits(0x3FC90FDBu)));
        SinValue = _mm_add_ss(SinValue, _mm_set_ss(FloatFromBits(0x3FC90FDBu)));
        CosValue = _mm_mul_ss(CosValue, CosFactor);
        SinValue = _mm_mul_ss(SinValue, SinFactor);
        _mm_store_ss(&Out[0], CosValue);
        _mm_store_ss(&Out[1], SinValue);
    }

    static void ComputeSinCosFpu(float AngleRadians, float* Output)
    {
        Output[0] = std::cos(AngleRadians);
        Output[1] = std::sin(AngleRadians);
    }

    FnAlchemySinCos G_AlchemySinCos = ComputeSinCosFpu; // PTR_FUN_062572a0

    void SelectAlchemyMathImplementation()
    {
        int CpuInfo[4];
        __cpuid(CpuInfo, 1);
        if ((CpuInfo[3] & 0x02000000) != 0)
            G_AlchemySinCos = ComputeSinCosSse;
    }

    void ComputeSinCosPair(float AngleRadians, float* OutCos, float* OutSin)
    {
        float Result[2];
        G_AlchemySinCos(AngleRadians, Result);
        if (OutCos != nullptr)
            *OutCos = Result[0];
        if (OutSin != nullptr)
            *OutSin = Result[1];
    }

    // FUN_06240f50 (thiscall on a draw context, RET 0xc), traced 2026-09-26 against
    // alchemy.dll.txt 0x06240f50-0x06241065:
    //   [+0x18] = verts, [+0x1c] = indices -- stored before the capacity checks;
    //   DAT_06257bd8 (max verts) < verts -> FDUMP 0x76 "Max Vertices exceeded", false;
    //   DAT_06257bdc (max indices) < indices -> FDUMP 0x7d, false (signed JGE both);
    //   [+8] = 1; DAT_06257bd4 = VB handle ([DAT_06257bb8] IAlchemy vt+0x88, whose body
    //     LAB_062024e0 returns DAT_0625791c = G_VbHandle; DAT_06257bd4 is ONE module global
    //     shared by the six contexts, G_DrawVbHandle);
    //   [+0] = prim, [+4] = [+6] = 0, [+0x20] = [+0x24] = 0;
    //   VB vt+0x1c Lock(DAT_06257bd4, &[+0x20], &[+0x10], verts);
    //   IB vt+0x1c Lock(-1, &[+0x24], &[+0x14], indices) -- both always called;
    //   true (AL = 1) only when both results are >= 0, else false.
    // Both overflow returns leave the counters and the locked pointers as they were.
    bool AlDrawContextBegin(FAlDrawContext& Context, unsigned long Prim, unsigned long VertCount,
                            unsigned long IndexCount)
    {
        Context.RequestedVerts = VertCount;
        Context.RequestedIndices = IndexCount;
        if (static_cast<long>(G_DrawMaxVertices) < static_cast<long>(VertCount))
        {
            char Message[256];
            _snprintf(Message, sizeof(Message), kMsgMaxVerticesExceeded, VertCount,
                      G_DrawMaxVertices);
            Message[sizeof(Message) - 1] = '\0';
            FdumpDrawContextError(0x76, Message);
            return false;
        }
        if (static_cast<long>(G_DrawMaxIndices) < static_cast<long>(IndexCount))
        {
            char Message[256];
            _snprintf(Message, sizeof(Message), kMsgMaxIndicesExceeded, IndexCount,
                      G_DrawMaxIndices);
            Message[sizeof(Message) - 1] = '\0';
            FdumpDrawContextError(0x7D, Message);
            return false;
        }

        Context.bInBegin = 1;
        G_DrawVbHandle = G_InitializedAlchemy->GetVertexBufferHandle();
        Context.Prim = Prim;
        Context.IndexCount = 0;
        Context.VertCount = 0;
        Context.VbOffset = 0;
        Context.IbOffset = 0;
        const GENRESULT VbLock = static_cast<IRPVertexBuffer*>(G_RpVertexBuffer)->LockVb(
            G_DrawVbHandle, &Context.VbOffset, &Context.LockedVerts, VertCount);
        const GENRESULT IbLock =
            static_cast<IRPIndexBuffer*>(G_RpIndexBuffer)
                ->LockIb(reinterpret_cast<void*>(static_cast<unsigned int>(-1)),
                         &Context.IbOffset, &Context.LockedIndices, IndexCount);
        return VbLock >= 0 && IbLock >= 0;
    }

    // FUN_06241070 (thiscall, RET), traced 2026-09-26 against 0x06241070-0x0624117e:
    //   [+8] == 0 -> FDUMP 0x9e "End() called before Begin()", return 0;
    //   VB vt+0x20 Unlock(DAT_06257bd4); IB vt+0x20 Unlock(-1);
    //   requested verts < [+4] -> FDUMP 0xa9, [+6] = 0;
    //   requested indices < [+6] -> FDUMP 0xb1, [+6] = 0 (signed JGE both);
    //   [+8] = 0; return [+6].
    unsigned long AlDrawContextEnd(FAlDrawContext& Context)
    {
        if (Context.bInBegin == 0)
        {
            FdumpDrawContextError(0x9E, kMsgEndBeforeBegin);
            return 0;
        }
        CallUnlockVb(G_RpVertexBuffer, G_DrawVbHandle);
        CallUnlockIb(G_RpIndexBuffer, reinterpret_cast<void*>(static_cast<unsigned int>(-1)));
        if (static_cast<long>(Context.RequestedVerts) < static_cast<long>(Context.VertCount))
        {
            FdumpDrawContextError(0xA9, kMsgMoreVerts);
            Context.IndexCount = 0;
        }
        if (static_cast<long>(Context.RequestedIndices) < static_cast<long>(Context.IndexCount))
        {
            FdumpDrawContextError(0xB1, kMsgMoreIndices);
            Context.IndexCount = 0;
        }
        Context.bInBegin = 0;
        return Context.IndexCount;
    }

    // FUN_06241180 (thiscall, RET), traced 2026-09-26 against 0x06241180-0x0624122a:
    //   [+8] != 0 -> FDUMP 0xc2 "End() must be called before Draw()", return -1;
    //   VB vt+0x28 Select(DAT_06257bd4) < 0 -> return 0;
    //   IB vt+0x24 Select(-1, [+0x20], 0, 0) < 0 -> return 0 (the VB offset is the base
    //     vertex index);
    //   Draw vt+0xc DrawIndexedPrimitive([+0], 0, [+4], [+0x24], [+6]); return 0.
    GENRESULT AlDrawContextDraw(FAlDrawContext& Context)
    {
        if (Context.bInBegin != 0)
        {
            FdumpDrawContextError(0xC2, kMsgEndBeforeDraw);
            return GR_GENERIC;
        }
        if (CallSelectVb(G_RpVertexBuffer, G_DrawVbHandle) < 0)
            return GR_OK;
        if (CallSelectIb(G_RpIndexBuffer, reinterpret_cast<void*>(static_cast<unsigned int>(-1)),
                         Context.VbOffset) < 0)
            return GR_OK;
        CallDrawIndexed(G_RpDraw, Context.Prim, 0, Context.VertCount, Context.IbOffset,
                        Context.IndexCount);
        return GR_OK;
    }

    // The four records zeroed, as LAB_06201030 (load) and BeginDraw 0x06201abf do. The list
    // holds no references (FUN_06201c70 does not AddRef), so there is nothing to release.
    void ClearBatchLists()
    {
        unsigned long List = 0;
        for (List = 0; List < kBatchListCount; ++List)
        {
            G_BatchLists[List].Head = nullptr;
            G_BatchLists[List].Tail = nullptr;
            G_BatchLists[List].Count = 0;
        }
    }

    namespace
    {
        FGenericFxObjectBase* BatchLinksOf(IGenericFx* Appearance)
        {
            return static_cast<FGenericFxObjectBase*>(Appearance);
        }
    } // namespace

    // FUN_06201c70 (and the IAlchemy slot LAB_06202480, the same body): append to
    // list[ListIndex] through the appearance's own links.
    //   list.Tail = A;
    //   empty (old tail 0): list.Head = A, A.next = 0, A.prev = 0;
    //   else: oldTail.next = A, A.next = 0, A.prev = oldTail;
    //   ++list.Count; return 0.
    // No bounds check, no null check, no AddRef.
    GENRESULT EnqueueAppearanceToList(unsigned long ListIndex, IDAComponent* Appearance)
    {
        FBatchList& List = G_BatchLists[ListIndex];
        IGenericFx* Node = AsFxNode(Appearance);
        IGenericFx* OldTail = List.Tail;
        List.Tail = Node;
        if (OldTail == nullptr)
            List.Head = Node;
        else
            BatchLinksOf(OldTail)->BatchNext_ = Node;
        BatchLinksOf(Node)->BatchNext_ = nullptr;
        BatchLinksOf(Node)->BatchPrev_ = OldTail;
        List.Count += 1;
        return GR_OK;
    }

    // FUN_06201cc0, called from EndDraw (0x06201bea) when the batcher is on. For each of
    // the four lists with a head:
    //   0x06201ce2: sum +0xd0 / +0xd4 (queued vertex / index counts, locals zeroed before
    //     each call) over the list, following [outer+8];
    //   0x06201d21: head->+0x9C(verts, indices) -- result ignored;
    //   0x06201d27: +0xA4 on every member, head first;
    //   0x06201d3b: head->+0xA0.
    // The lists are not cleared here; BeginDraw clears them.
    void FlushAppearanceBatches()
    {
        unsigned long List = 0;
        for (List = 0; List < kBatchListCount; ++List)
        {
            IGenericFx* Head = G_BatchLists[List].Head;
            if (Head == nullptr)
                continue;
            unsigned long TotalVerts = 0;
            unsigned long TotalIndices = 0;
            IGenericFx* Member = Head;
            while (Member != nullptr)
            {
                unsigned long Verts = 0;
                unsigned long Indices = 0;
                Member->GetQueuedVertCount(&Verts);
                Member->GetQueuedIndexCount(&Indices);
                TotalVerts += Verts;
                TotalIndices += Indices;
                Member = BatchLinksOf(Member)->BatchNext_;
            }
            Head->BeginBatchedDraw(TotalVerts, TotalIndices);
            Member = Head;
            while (Member != nullptr)
            {
                Member->FillBatchedGeometry();
                Member = BatchLinksOf(Member)->BatchNext_;
            }
            Head->EndBatchedDraw();
        }
    }

    // FUN_06203c20 value parse: strspn charset, then "true"/"yes"/"false"/"no", else atoi/strtoul.
    unsigned long ParseAlchemyIniValue(char* Buffer, unsigned long /*Default*/)
    {
        const size_t Span = strspn(Buffer, kCharsetValue);
        if (Span != 0)
            Buffer[Span] = '\0';

        if (_stricmp(Buffer, "true") == 0 || _stricmp(Buffer, "yes") == 0)
            return 1;
        if (_stricmp(Buffer, "false") == 0 || _stricmp(Buffer, "no") == 0)
            return 0;

        if (strchr(Buffer, 'x') != nullptr || strchr(Buffer, 'X') != nullptr)
            return strtoul(Buffer, nullptr, 16);

        strspn(Buffer, kCharsetDigits);
        return static_cast<unsigned long>(MsvcrtAtoi(Buffer));
    }

    // FUN_06203c20 owns only the parser across exceptions; an open section is
    // closed on the normal GetString path. DAT_06257f80 is shared across reads.
    char G_AlchemyIniBuffer[0x400] = {};

    struct FAlchemyIniParserRef
    {
        IProfileParser* Parser = nullptr;

        ~FAlchemyIniParserRef()
        {
            if (Parser != nullptr)
                Parser->Release();
        }
    };

    unsigned long ReadAlchemyIniValue(ICOManager* Manager, IProfileParser* Parser,
                                     const char* SectionName, const char* Key,
                                     unsigned long Default, unsigned long* Out)
    {
        unsigned long LocalResult =
            static_cast<unsigned long>(reinterpret_cast<uintptr_t>(Parser));
        if (Out == nullptr)
            Out = &LocalResult;

        FAlchemyIniParserRef Owned;
        Owned.Parser = Parser;
        if (Owned.Parser != nullptr)
            Owned.Parser->AddRef();
        if (Owned.Parser == nullptr &&
            Manager->QueryInterface(Iids::ProfileParser,
                                    reinterpret_cast<void**>(&Owned.Parser)) != GR_OK)
            return *Out;

        *Out = Default;
        void* Section = Owned.Parser->OpenSection(SectionName, 3);
        if (Section != nullptr)
        {
            const int Got = Owned.Parser->GetString(Section, Key, G_AlchemyIniBuffer, 0x400);
            if (Got != 0)
                *Out = ParseAlchemyIniValue(G_AlchemyIniBuffer, Default);
            Owned.Parser->CloseSection(Section);
        }
        // Capture the value before Owned releases the parser, as 06203dfe does.
        return *Out;
    }

    // RTC stack poisoning would otherwise seed the two intentionally untouched
    // retail words with a compiler-defined value before either parser QI.
#pragma runtime_checks("", off)
    void ApplyAlchemyIniAndPools(ICOManager* Manager)
    {
        // The two inline retail reads seed their defaults only after parser QI.
        // A QI failure leaves these stack words untouched; there is no fallback.
        unsigned long BasicPoolSize;
        unsigned long ParticlePoolSize;
        ReadAlchemyIniValue(Manager, nullptr, kAlchemySection, "FxBasicAppearance.poolSize",
                            kDefaultPoolSize, &BasicPoolSize);
        ReadAlchemyIniValue(Manager, nullptr, kAlchemySection, "FxParticleAppearance.poolSize",
                            kDefaultPoolSize, &ParticlePoolSize);
        G_Defaults.BasicPoolSize = BasicPoolSize;
        G_Defaults.ParticlePoolSize = ParticlePoolSize;
        G_Defaults.RectPoolSize = ReadAlchemyIniValue(
            Manager, nullptr, kAlchemySection, "FxRectAppearance.poolSize", kDefaultPoolSize, nullptr);
        G_Defaults.PerpPoolSize = ReadAlchemyIniValue(
            Manager, nullptr, kAlchemySection, "FxPerpAppearance.poolSize", kDefaultPoolSize, nullptr);
        G_Defaults.MeshPoolSize = ReadAlchemyIniValue(
            Manager, nullptr, kAlchemySection, "FxMeshAppearance.poolSize", kDefaultPoolSize, nullptr);
        G_Defaults.OrientedPoolSize = ReadAlchemyIniValue(
            Manager, nullptr, kAlchemySection, "FxOrientedAppearance.poolSize", kDefaultPoolSize, nullptr);
        G_Defaults.DustPoolSize = ReadAlchemyIniValue(
            Manager, nullptr, kAlchemySection, "FLDustAppearance.poolSize", kDefaultPoolSize, nullptr);
        G_Defaults.BeamPoolSize = ReadAlchemyIniValue(
            Manager, nullptr, kAlchemySection, "FLBeamAppearance.poolSize", kDefaultPoolSize, nullptr);
        // Retail reads this key but discards its value (0620398c onward).
        ReadAlchemyIniValue(Manager, nullptr, kAlchemySection, "Alchemy.useMaterialBatcher",
                            kDefaultUseMaterialBatcher, nullptr);

        AllocateAppearancePool(&G_PoolBasic, G_Defaults.BasicPoolSize, kStrideBasic);
        AllocateAppearancePool(&G_PoolParticle, G_Defaults.ParticlePoolSize, kStrideParticle);
        AllocateAppearancePool(&G_PoolRect, G_Defaults.RectPoolSize, kStrideRect);
        AllocateAppearancePool(&G_PoolPerp, G_Defaults.PerpPoolSize, kStridePerp);
        AllocateAppearancePool(&G_PoolMesh, G_Defaults.MeshPoolSize, kStrideMesh);
        AllocateAppearancePool(&G_PoolOriented, G_Defaults.OrientedPoolSize, kStrideOriented);
        AllocateAppearancePool(&G_PoolDust, G_Defaults.DustPoolSize, kStrideDust);
        AllocateAppearancePool(&G_PoolBeam, G_Defaults.BeamPoolSize, kStrideBeam);
        ComputeDrawBufferHints();
    }
#pragma runtime_checks("", restore)

    static_assert(sizeof(FAlchemyVertex) == kAlchemyVertexStride,
                  "FAlchemyVertex must match FUN_06208aa0 stride 0x18");

    IGenericFx* QueryGenericFx(IDAComponent* Node)
    {
        if (Node == nullptr)
            return nullptr;
        void* Out = nullptr;
        if (Node->QueryInterface(Iids::FxNode, &Out) != GR_OK || Out == nullptr)
            return nullptr;
        return static_cast<IGenericFx*>(Out);
    }

    // Field pointers are borrowed from the captured vector range. Alive callbacks
    // reload the slot; only an erase refreshes the loop end. Particle arguments alias
    // the stored position/velocity and age directly, including on exception paths.
    void ApplyFieldsToPool(FGenericFxObjectBase* Owner, bool bLiveItemsOnly)
    {
        IDAComponent** Position = Owner->AssociatedFields_;
        IDAComponent** LoopEnd = Position;
        if (Owner->FieldCount_ != 0)
            LoopEnd += Owner->FieldCount_;
        if (Position == LoopEnd)
            return;
        do
        {
            if (static_cast<unsigned char>(AsFxNode(*Position)->IsNodeDead()) != 0)
            {
                Owner->EraseAssociatedField(Position);
                LoopEnd = Owner->AssociatedFields_ + Owner->FieldCount_;
            }
            else
            {
                AsFxNode(*Position)->BeginField(static_cast<IGenericFx*>(Owner), 0.0f);
                FPooledParticle* Particle = Owner->InstanceListHead_;
                while (Particle != nullptr)
                {
                    if (!bLiveItemsOnly || Particle->Age < Particle->Duration)
                        AsFxNode(*Position)->ApplyFieldToVel(&Particle->PosX, &Particle->Age, 0.0f);
                    Particle = Particle->Next;
                }
                AsFxNode(*Position)->Pad40();
                ++Position;
            }
        } while (Position != LoopEnd);
    }

    template <typename TInterface>
    struct FAlchemyReaderRef
    {
        TInterface* Pointer;

        explicit FAlchemyReaderRef(TInterface* InPointer = nullptr) : Pointer(InPointer) {}
        FAlchemyReaderRef(const FAlchemyReaderRef&) = delete;
        FAlchemyReaderRef& operator=(const FAlchemyReaderRef&) = delete;
        ~FAlchemyReaderRef() { ReleaseDescriptorSlot(Pointer); }

        TInterface* Detach()
        {
            TInterface* Result = Pointer;
            Pointer = nullptr;
            return Result;
        }
    };

    // FUN_0622ae40: target mutations are interleaved with the chunk reads.
    int ReadRampSingleBlock(FAleChunk* Chunk, IRampSingle* Target)
    {
        if (Target == nullptr)
            return -1;
        Target->Reset();
        unsigned char Mode;
        if (Chunk->ReadBytes(&Mode, 1) < 0)
            return -6;
        Target->SetMode(Mode);
        unsigned char Count;
        int Result = Chunk->ReadBytes(&Count, 1);
        if (Result < 0)
            return -6;
        for (unsigned char Index = 0; Index < Count; ++Index)
        {
            float Time;
            if (Chunk->ReadBytes(&Time, 4) < 0)
                return -6;
            float Value;
            Result = Chunk->ReadBytes(&Value, 4);
            if (Result < 0)
                return -6;
            Target->AddKey(Time, &Value);
        }
        return Result;
    }

    // FUN_0622af10: mode/count bytes followed by time and three-float colour.
    int ReadRampColorBlock(FAleChunk* Chunk, IRampColor* Target)
    {
        if (Target == nullptr)
            return -1;
        Target->Reset();
        unsigned char Mode;
        if (Chunk->ReadBytes(&Mode, 1) < 0)
            return -6;
        Target->SetMode(Mode);
        unsigned char Count;
        int Result = Chunk->ReadBytes(&Count, 1);
        if (Result < 0)
            return -6;
        for (unsigned char Index = 0; Index < Count; ++Index)
        {
            float Time;
            if (Chunk->ReadBytes(&Time, 4) < 0)
                return -6;
            float Rgb[3];
            Result = Chunk->ReadBytes(Rgb, 0xC);
            if (Result < 0)
                return -6;
            Target->AddKey(Time, Rgb);
        }
        return Result;
    }

    static void DumpUnknownRampVersion(int Line)
    {
        FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
              "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxNodeLibrary.cpp",
              Line, "Unknown version");
    }

    // FUN_06227a20: Reset, header, SetWeight, keys, then SetLoopType.
    int ReadAnimatedSingleBlock(FAleChunk* Chunk, IFxAnimatedSingle* Target)
    {
        if (Target == nullptr)
            return -1;
        Target->Reset();
        float Weight;
        if (Chunk->ReadBytes(&Weight, 4) < 0)
            return -6;
        unsigned short LoopType;
        if (Chunk->ReadBytes(&LoopType, 2) < 0)
            return -6;
        unsigned short KeyCount;
        int Result = Chunk->ReadBytes(&KeyCount, 2);
        if (Result < 0)
            return -6;
        Target->SetWeight(Weight);
        for (unsigned short Index = 0; Index < KeyCount; ++Index)
        {
            float Time;
            float Value[3] = {0.0f, 0.0f, 0.0f};
            if (Chunk->ReadBytes(&Time, 4) < 0)
                return -6;
            Result = Chunk->ReadBytes(Value, 0xC);
            if (Result < 0)
                return -6;
            Target->AddKey(Time, Value);
        }
        if (Target->SetLoopType(LoopType) != GR_OK && LoopType != 0)
        {
            FDUMP(0x100002, kFdumpWarningFmt,
                  "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxNodeLibrary.cpp",
                  0x150, "Failed to set non-zero loop type for animated single on load.\n");
            return -1;
        }
        return Result;
    }

    // FUN_06227c70: the temporary child exists before its time is read.
    int ReadAnimatedSingleRamp(FAleChunk* Chunk, INestedSingleRamp* Outer,
                               unsigned long VersionBits)
    {
        if (Outer == nullptr)
            return -1;
        Outer->Reset();
        if (VersionBits == kAleVersion10Bits)
        {
            FAlchemyReaderRef<IFxAnimatedSingle> Child(new FFxAnimatedSingle());
            const int Result = ReadAnimatedSingleBlock(Chunk, Child.Pointer);
            if (Result < 0)
                return -6;
            Outer->AddKey(0.0f, Child.Pointer);
            return Result;
        }
        if (VersionBits == kAleVersion11Bits)
        {
            unsigned char Mode;
            if (Chunk->ReadBytes(&Mode, 1) < 0)
                return -6;
            Outer->SetMode(Mode);
            unsigned char Count;
            int Result = Chunk->ReadBytes(&Count, 1);
            if (Result < 0)
                return -6;
            for (unsigned char Index = 0; Index < Count; ++Index)
            {
                FAlchemyReaderRef<IFxAnimatedSingle> Child(new FFxAnimatedSingle());
                float Time;
                if (Chunk->ReadBytes(&Time, 4) < 0)
                    return -6;
                Result = ReadAnimatedSingleBlock(Chunk, Child.Pointer);
                if (Result < 0)
                    return -6;
                Outer->AddKey(Time, Child.Pointer);
            }
            return Result;
        }
        DumpUnknownRampVersion(0x198);
        return -1;
    }

    // FUN_062280a0: packed modes and, under the header sign gate, nine channels.
    int ReadAnimatedTransform(FAleChunk* Chunk, IAnimatedTransform* Anim,
                              unsigned long VersionBits)
    {
        if (Anim == nullptr)
            return -1;
        signed char Header[4];
        int Result = Chunk->ReadBytes(Header, 4);
        if (Result < 0)
            return -6;
        Anim->SetPackedModes(static_cast<unsigned long>(static_cast<long>(Header[0])),
                             static_cast<unsigned long>(static_cast<long>(Header[1])),
                             static_cast<unsigned long>(static_cast<long>(Header[2])));
        if (Header[3] >= 0)
            return Result;
        for (unsigned long Channel = 0; Channel < 9; ++Channel)
        {
            FAlchemyReaderRef<IDAComponent> Child;
            Anim->GetChild(Channel, &Child.Pointer);
            Result = ReadAnimatedSingleRamp(Chunk,
                                            static_cast<INestedSingleRamp*>(Child.Pointer),
                                            VersionBits);
            if (Result < 0)
                return -6;
        }
        Anim->UpdateChannelFlags();
        return Result;
    }

    // FUN_062291a0 case 0x200. Empty children receive a zero key only after
    // the version-specific read completed; every getter reloads the live key count.
    static void ReadNestedSingleRamp(FAleChunk* Chunk, INestedSingleRamp* Ramp,
                                      unsigned long VersionBits)
    {
        Ramp->Reset();
        if (VersionBits == kAleVersion10Bits)
        {
            FAlchemyReaderRef<IRampSingle> Child(new FRampSingle());
            if (ReadRampSingleBlock(Chunk, Child.Pointer) < 0)
                return;
            Ramp->AddKey(0.0f, Child.Pointer);
        }
        else if (VersionBits == kAleVersion11Bits)
        {
            unsigned char Mode;
            if (Chunk->ReadBytes(&Mode, 1) < 0)
                return;
            Ramp->SetMode(Mode);
            unsigned char Count;
            if (Chunk->ReadBytes(&Count, 1) < 0)
                return;
            for (unsigned char Index = 0; Index < Count; ++Index)
            {
                FAlchemyReaderRef<IRampSingle> Child(new FRampSingle());
                float Time;
                if (Chunk->ReadBytes(&Time, 4) < 0)
                    return;
                if (ReadRampSingleBlock(Chunk, Child.Pointer) < 0)
                    return;
                Ramp->AddKey(Time, Child.Pointer);
            }
        }
        else
        {
            DumpUnknownRampVersion(0x104);
            return;
        }
        for (int Index = 0; Index < Ramp->GetKeyCount(); ++Index)
        {
            FAlchemyReaderRef<IDAComponent> Child;
            Ramp->GetKeyValue(Index, &Child.Pointer);
            IRampSingle* Track = static_cast<IRampSingle*>(Child.Pointer);
            if (Track->GetKeyCount() == 0)
            {
                const float Zero = 0.0f;
                Track->AddKey(0.0f, &Zero);
            }
        }
    }

    // FUN_062291a0 case 0x201, with the retail grey default for empty colour tracks.
    static void ReadNestedColorRamp(FAleChunk* Chunk, IRampColorRamp* Ramp,
                                     unsigned long VersionBits)
    {
        Ramp->Reset();
        if (VersionBits == kAleVersion10Bits)
        {
            FAlchemyReaderRef<IRampColor> Child(new FRampColor());
            if (ReadRampColorBlock(Chunk, Child.Pointer) < 0)
                return;
            Ramp->AddKey(0.0f, Child.Pointer);
        }
        else if (VersionBits == kAleVersion11Bits)
        {
            unsigned char Mode;
            if (Chunk->ReadBytes(&Mode, 1) < 0)
                return;
            Ramp->SetMode(Mode);
            unsigned char Count;
            if (Chunk->ReadBytes(&Count, 1) < 0)
                return;
            for (unsigned char Index = 0; Index < Count; ++Index)
            {
                FAlchemyReaderRef<IRampColor> Child(new FRampColor());
                float Time;
                if (Chunk->ReadBytes(&Time, 4) < 0)
                    return;
                if (ReadRampColorBlock(Chunk, Child.Pointer) < 0)
                    return;
                Ramp->AddKey(Time, Child.Pointer);
            }
        }
        else
        {
            DumpUnknownRampVersion(0x104);
            return;
        }
        for (int Index = 0; Index < Ramp->GetKeyCount(); ++Index)
        {
            FAlchemyReaderRef<IRampColor> Child;
            Ramp->GetKeyValue(Index, &Child.Pointer);
            if (Child.Pointer->GetKeyCount() == 0)
            {
                const float Grey[3] = {0.8f, 0.8f, 0.8f};
                Child.Pointer->AddKey(0.0f, Grey);
            }
        }
    }


    // Until 2026-09-16 the helpers below reached IGenericFx through hand-written slot
    // numbers, two of them (CallNodeGetter / CallNodeSetter) taking the number as a RUNTIME
    // argument. The interface was declared the whole time, in this module's own
    // alchemy_types.h -- what was missing was the type of the variable. Retail casts too:
    // 0x0621f537 calls [node+0x2c] straight off the stored pointer, with no QueryInterface.
    IGenericFx* AsFxNode(void* Node)
    {
        return static_cast<IGenericFx*>(static_cast<IDAComponent*>(Node));
    }

    // FUN_0621eed0: CALL [QI sink+0x9C] / [QI sink+0xAC], stdcall this+1.
    void CallAssociateAppearance(IGenericFx* Sink, IDAComponent* Appearance)
    {
        if (Sink != nullptr)
            Sink->AssociateAppearance(Appearance);
    }

    void CallAssociateField(IGenericFx* Sink, IDAComponent* Field)
    {
        if (Sink != nullptr)
            Sink->AssociateField(Field);
    }

    // FUN_06201e90: PUSH child; CALL [child+0x28]; TEST EAX.
    IDAComponent* CallGetAttachParent(void* Node)
    {
        return (Node != nullptr) ? AsFxNode(Node)->GetAttachParent() : nullptr;
    }

    IDAComponent* CallGetFirstChild(void* Node)
    {
        return (Node != nullptr) ? AsFxNode(Node)->GetFirstChild() : nullptr;
    }

    IDAComponent* CallGetPrevSibling(void* Node)
    {
        return (Node != nullptr) ? AsFxNode(Node)->GetPrevSibling() : nullptr;
    }

    IDAComponent* CallGetNextSibling(void* Node)
    {
        return (Node != nullptr) ? AsFxNode(Node)->GetNextSibling() : nullptr;
    }

    int CallGetSortKey(void* Node)
    {
        return (Node != nullptr) ? AsFxNode(Node)->GetSortKey() : 0;
    }

    void CallSetParent(IDAComponent* Node, IDAComponent* Parent)
    {
        if (Node != nullptr)
            AsFxNode(Node)->SetParent(Parent);
    }

    void CallSetFirstChild(IDAComponent* Node, IDAComponent* Child)
    {
        if (Node != nullptr)
            AsFxNode(Node)->SetFirstChild(Child);
    }

    void CallSetPrevSibling(IDAComponent* Node, IDAComponent* Prev)
    {
        if (Node != nullptr)
            AsFxNode(Node)->SetPrevSibling(Prev);
    }

    void CallSetNextSibling(IDAComponent* Node, IDAComponent* Next)
    {
        if (Node != nullptr)
            AsFxNode(Node)->SetNextSibling(Next);
    }

    // FUN_06201e00: list link, not particle update. Ghidra .c drops SetParent
    // arg and compares sibling key to parent pointer — listing is source.
    void LinkNodeTree(IDAComponent* Parent, IDAComponent* Child)
    {
        IGenericFx* const ParentNode = AsFxNode(Parent);
        IGenericFx* const ChildNode = AsFxNode(Child);
        ChildNode->SetParent(Parent);
        IDAComponent* const First = ParentNode->GetFirstChild();
        if (First == nullptr)
        {
            ParentNode->SetFirstChild(Child);
            return;
        }
        const int ChildKey = ChildNode->GetSortKey();
        IDAComponent* Cursor = ParentNode->GetFirstChild();
        IDAComponent* Prev = nullptr;
        while (Cursor != nullptr)
        {
            if (AsFxNode(Cursor)->GetSortKey() > ChildKey)
                break;
            Prev = Cursor;
            Cursor = AsFxNode(Cursor)->GetNextSibling();
        }
        if (Prev != nullptr)
            AsFxNode(Prev)->SetNextSibling(Child);
        else
            ParentNode->SetFirstChild(Child);
        if (Cursor != nullptr)
            AsFxNode(Cursor)->SetPrevSibling(Child);
        ChildNode->SetPrevSibling(Prev);
        ChildNode->SetNextSibling(Cursor);
    }

    // Unused port helper. Retail FUN_06201f00 is FAlchemy::DetachNode.
    GENRESULT UnlinkNodeTree(IDAComponent* Node)
    {
        IDAComponent* Parent = CallGetAttachParent(Node);
        if (Parent == nullptr)
            return GR_GENERIC;
        IDAComponent* Prev = CallGetPrevSibling(Node);
        IDAComponent* Next = CallGetNextSibling(Node);
        if (Prev != nullptr)
            CallSetNextSibling(Prev, Next);
        if (Next != nullptr)
            CallSetPrevSibling(Next, Prev);
        IDAComponent* ParentAgain = CallGetAttachParent(Node);
        if (ParentAgain != nullptr)
        {
            if (CallGetFirstChild(ParentAgain) == Node)
                CallSetFirstChild(ParentAgain, Next);
        }
        else
            G_RootNode = Next;
        CallSetParent(Node, nullptr);
        CallSetNextSibling(Node, nullptr);
        CallSetPrevSibling(Node, nullptr);
        return GR_OK;
    }

    // LAB_06228d74: DAT_062563a8 pairs until key==0.
    unsigned long RemapNodeLibHash(unsigned long Hash)
    {
        if (Hash == 0xEE1429BA)
            return 0xFE077E05;
        if (Hash == 0xF0382D6B)
            return 0x0C53B008;
        return Hash;
    }

    // LAB_06228da0: version==1.0 remaps 0x100/101/102 → 0x200/201/202 (DAT_06256390).
    unsigned long RemapNodeLibKind(unsigned long Kind, unsigned long VersionBits)
    {
        if (VersionBits != kAleVersion10Bits)
            return Kind;
        if (Kind == 0x100)
            return 0x200;
        if (Kind == 0x101)
            return 0x201;
        if (Kind == 0x102)
            return 0x202;
        return Kind;
    }

    static const char kFxNodeLibraryCppPath[] =
        "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxNodeLibrary.cpp";

    // FUN_062291a0: one property; its caller ignores EAX and continues the stream.
    static void ReadNodeLibProperty(FAleChunk* Chunk, IFxNodePropertySet* Set,
                                    unsigned long Hash, unsigned long Type,
                                    unsigned long VersionBits)
    {
        const unsigned long Kind = Type & 0x7FFF;
        if (Kind == 1)
        {
            Set->SetPropBool(Hash, (Type & 0x8000) == 0x8000 ? 1 : 0);
        }
        else if (Kind == 2 || Kind == 3)
        {
            unsigned long Value;
            if (Chunk->ReadBytes(&Value, 4) < 0)
                return;
            if (Kind == 2)
            {
                Set->SetPropInt(Hash, Value);
            }
            else
            {
                float FloatValue;
                std::memcpy(&FloatValue, &Value, sizeof(FloatValue));
                Set->SetPropFloat(Hash, FloatValue);
            }
        }
        else if (Kind == 0x103)
        {
            char Name[kAleNameMax];
            if (Chunk->ReadString(Name, kAleNameMax) < 0)
                return;
            Set->SetPropName(Hash, Name);
        }
        else if (Kind == 0x104)
        {
            unsigned long Pair[2];
            if (Chunk->ReadBytes(Pair, 8) < 0)
                return;
            Set->SetPropPair(Hash, Pair);
        }
        else if (Kind == 0x100)
        {
            FAlchemyReaderRef<IRampSingle> Ramp;
            Set->GetPropSingleRamp(Hash, &Ramp.Pointer);
            ReadRampSingleBlock(Chunk, Ramp.Pointer);
        }
        else if (Kind == 0x101)
        {
            FAlchemyReaderRef<IRampColor> Ramp;
            Set->GetPropColorRamp(Hash, &Ramp.Pointer);
            ReadRampColorBlock(Chunk, Ramp.Pointer);
        }
        else if (Kind == 0x102)
        {
            FAlchemyReaderRef<IFxAnimatedSingle> Single;
            Set->GetPropAnimatedSingle(Hash, &Single.Pointer);
            ReadAnimatedSingleBlock(Chunk, Single.Pointer);
        }
        else if (Kind == 0x105)
        {
            FAlchemyReaderRef<IAnimatedTransform> Anim;
            Set->GetPropTransform(Hash, &Anim.Pointer);
            ReadAnimatedTransform(Chunk, Anim.Pointer, VersionBits);
        }
        else if (Kind == 0x200)
        {
            FAlchemyReaderRef<INestedSingleRamp> Ramp;
            if (Set->GetPropSingleRampRamp(Hash, &Ramp.Pointer) < 0)
            {
                ReleaseDescriptorSlot(Ramp.Pointer);
                FAlchemyReaderRef<IDAComponent> Created;
                CreateRampKeyValue("FxRampSingleRamp", &Created.Pointer);
                Ramp.Pointer = static_cast<INestedSingleRamp*>(Created.Detach());
            }
            if (Ramp.Pointer != nullptr)
                ReadNestedSingleRamp(Chunk, Ramp.Pointer, VersionBits);
        }
        else if (Kind == 0x201)
        {
            FAlchemyReaderRef<IRampColorRamp> Ramp;
            Set->GetPropColorRampRamp(Hash, &Ramp.Pointer);
            if (Ramp.Pointer != nullptr)
                ReadNestedColorRamp(Chunk, Ramp.Pointer, VersionBits);
        }
        else if (Kind == 0x202)
        {
            FAlchemyReaderRef<INestedSingleRamp> Ramp;
            if (Set->GetPropAnimatedSingleRamp(Hash, &Ramp.Pointer) < 0)
            {
                ReleaseDescriptorSlot(Ramp.Pointer);
                FAlchemyReaderRef<IDAComponent> Created;
                CreateRampKeyValue("FxAnimatedSingleRamp", &Created.Pointer);
                Ramp.Pointer = static_cast<INestedSingleRamp*>(Created.Detach());
            }
            ReadAnimatedSingleRamp(Chunk, Ramp.Pointer, VersionBits);
        }
        else
        {
            FDUMP(0x100001, "%s(%d) : ERROR:General:%s", kFxNodeLibraryCppPath, 0x345,
                  "Unknown type of property");
        }
    }


    // The node library loader's property loop (0x06228d44..0x06228dfd), using its
    // already-acquired set: {Type u16 (0 or a short read ends it), Hash (a short read ends
    // it), the hash remap table 0x062563a8, for a 1.0 file the type remap 0x06256390 on
    // the whole 16-bit type, FUN_062291a0}. Nothing a property does ends the loop.
    // Returns 0 when a zero type ended it, -1 on a short read.
    int ReadNodeLibProperties(FAleChunk* Chunk, IFxNodePropertySet* Set, unsigned long VersionBits)
    {
        int Result = 0;
        for (;;)
        {
            unsigned short Type = 0;
            if (Chunk->ReadBytes(&Type, 2) < 0)
            {
                Result = -1;
                break;
            }
            if (Type == 0)
                break;
            unsigned long Hash = 0;
            if (Chunk->ReadBytes(&Hash, 4) < 0)
            {
                Result = -1;
                break;
            }
            Hash = RemapNodeLibHash(Hash);
            const unsigned long RemappedType =
                RemapNodeLibKind(static_cast<unsigned long>(Type), VersionBits);
            ReadNodeLibProperty(Chunk, Set, Hash, RemappedType, VersionBits);
        }
        return Result;
    }

    namespace
    {
        // The context FUN_062281c0 hands LAB_06228350 ([ESP+0x24] = {0, &chunk, set}).
        struct FNodeWriteContext
        {
            FAleChunk* Chunk;
            IFxNodePropertySet* Set;
        };

        // FUN_0622ace0 (Chunk, Ramp; cdecl): Mode (+0x30) and key count (+0x38) as one
        // byte each, then per key GetKeyTime (+0x20) and GetKeyValue (+0x24), four bytes
        // each. Write results are ignored throughout the writer.
        void WriteRampSingleBlock(FAleChunk* Chunk, IRampSingle* Ramp)
        {
            const unsigned char Mode = static_cast<unsigned char>(Ramp->GetMode());
            const unsigned char Count = static_cast<unsigned char>(Ramp->GetKeyCount());
            Chunk->WriteBytes(&Mode, 1);
            Chunk->WriteBytes(&Count, 1);
            for (unsigned char Index = 0; Index < Count; ++Index)
            {
                float Time = 0.0f;
                float Value = 0.0f;
                Ramp->GetKeyTime(Index, &Time);
                Ramp->GetKeyValue(Index, &Value);
                Chunk->WriteBytes(&Time, 4);
                Chunk->WriteBytes(&Value, 4);
            }
        }

        // FUN_0622ad90: the same over FxRampColor -- Mode +0x34, count +0x3c, GetKeyTime
        // +0x24, GetKeyRgb +0x28 (twelve bytes).
        void WriteRampColorBlock(FAleChunk* Chunk, IRampColor* Ramp)
        {
            const unsigned char Mode = static_cast<unsigned char>(Ramp->GetMode());
            const unsigned char Count = static_cast<unsigned char>(Ramp->GetKeyCount());
            Chunk->WriteBytes(&Mode, 1);
            Chunk->WriteBytes(&Count, 1);
            for (unsigned char Index = 0; Index < Count; ++Index)
            {
                float Time = 0.0f;
                float Rgb[3];
                Ramp->GetKeyTime(Index, &Time);
                Ramp->GetKeyRgb(Index, Rgb);
                Chunk->WriteBytes(&Time, 4);
                Chunk->WriteBytes(Rgb, 0xC);
            }
        }

        // FUN_06227950 (Chunk, Single; cdecl): GetWeight (+0x1c), GetKeyCount (+0x14),
        // GetLoopType (+0x40); the weight (4), the loop type's low word and the count's
        // low word; per key (u16 index) a zeroed triple, GetKeyTime (+0x2c) and
        // GetKeyValue (+0x30), then the time (4) and the triple (12).
        void WriteAnimatedSingleBlock(FAleChunk* Chunk, IFxAnimatedSingle* Single)
        {
            float Weight = 0.0f;
            Single->GetWeight(&Weight);
            const unsigned long KeyCount = static_cast<unsigned long>(Single->GetKeyCount());
            unsigned long LoopType = 0;
            Single->GetLoopType(&LoopType);
            Chunk->WriteBytes(&Weight, 4);
            Chunk->WriteBytes(&LoopType, 2);
            Chunk->WriteBytes(&KeyCount, 2);
            const unsigned short Count = static_cast<unsigned short>(KeyCount);
            for (unsigned short Index = 0; Index < Count; ++Index)
            {
                float Value[3] = {0.0f, 0.0f, 0.0f};
                float Time = 0.0f;
                Single->GetKeyTime(Index, &Time);
                Single->GetKeyValue(Index, Value);
                Chunk->WriteBytes(&Time, 4);
                Chunk->WriteBytes(Value, 0xC);
            }
        }

        // The nested writers LAB_06228350 inlines for kinds 0x200 / 0x202 and FUN_06227ef0
        // for each channel: Mode (+0x30) and count (+0x38) bytes, then per key GetKeyTime
        // (+0x20), GetKeyValue (+0x24, AddRef'd) -> the time and the child block, and the
        // child released.
        void WriteRampSingleRampBlock(FAleChunk* Chunk, INestedSingleRamp* Ramp)
        {
            const unsigned char Mode = static_cast<unsigned char>(Ramp->GetMode());
            const unsigned char Count = static_cast<unsigned char>(Ramp->GetKeyCount());
            Chunk->WriteBytes(&Mode, 1);
            Chunk->WriteBytes(&Count, 1);
            for (unsigned char Index = 0; Index < Count; ++Index)
            {
                float Time = 0.0f;
                IDAComponent* Child = nullptr;
                Ramp->GetKeyTime(Index, &Time);
                Ramp->GetKeyValue(Index, &Child);
                Chunk->WriteBytes(&Time, 4);
                WriteRampSingleBlock(Chunk, static_cast<IRampSingle*>(Child));
                if (Child != nullptr)
                    Child->Release();
            }
        }

        void WriteAnimatedSingleRampBlock(FAleChunk* Chunk, INestedSingleRamp* Ramp)
        {
            const unsigned char Mode = static_cast<unsigned char>(Ramp->GetMode());
            const unsigned char Count = static_cast<unsigned char>(Ramp->GetKeyCount());
            Chunk->WriteBytes(&Mode, 1);
            Chunk->WriteBytes(&Count, 1);
            for (unsigned char Index = 0; Index < Count; ++Index)
            {
                float Time = 0.0f;
                IDAComponent* Child = nullptr;
                Ramp->GetKeyTime(Index, &Time);
                Ramp->GetKeyValue(Index, &Child);
                Chunk->WriteBytes(&Time, 4);
                WriteAnimatedSingleBlock(Chunk, static_cast<IFxAnimatedSingle*>(Child));
                if (Child != nullptr)
                    Child->Release();
            }
        }

        // Kind 0x201 inline (0x06228767): Mode +0x34, count +0x3c, GetKeyTime +0x24,
        // GetKeyValue +0x28 (AddRef'd), FUN_0622ad90 per child.
        void WriteRampColorRampBlock(FAleChunk* Chunk, IRampColorRamp* Ramp)
        {
            const unsigned char Mode = static_cast<unsigned char>(Ramp->GetMode());
            const unsigned char Count = static_cast<unsigned char>(Ramp->GetKeyCount());
            Chunk->WriteBytes(&Mode, 1);
            Chunk->WriteBytes(&Count, 1);
            for (unsigned char Index = 0; Index < Count; ++Index)
            {
                float Time = 0.0f;
                IRampColor* Child = nullptr;
                Ramp->GetKeyTime(Index, &Time);
                Ramp->GetKeyValue(Index, &Child);
                Chunk->WriteBytes(&Time, 4);
                WriteRampColorBlock(Chunk, Child);
                if (Child != nullptr)
                    Child->Release();
            }
        }

        // FUN_06227ef0 (Chunk, Anim; cdecl): GetPackedModes (+0x28) into header bytes 0..2,
        // UpdateChannelFlags (+0x18), GetChannelFlags (+0x1c); byte 3 = 0 when the flags
        // are 0x1ff, else 0x80 (`SETE; DEC; AND 0x80`); the four bytes; then unless the
        // flags are 0x1ff each of the nine channels (GetChild +0x14, AddRef'd) as a nested
        // animated-single ramp, released.
        void WriteAnimatedTransformBlock(FAleChunk* Chunk, IAnimatedTransform* Anim)
        {
            unsigned long Modes[3] = {0, 0, 0};
            Anim->GetPackedModes(&Modes[0], &Modes[1], &Modes[2]);
            unsigned char Header[4];
            Header[0] = static_cast<unsigned char>(Modes[0]);
            Header[1] = static_cast<unsigned char>(Modes[1]);
            Header[2] = static_cast<unsigned char>(Modes[2]);
            Anim->UpdateChannelFlags();
            unsigned long Flags = 0;
            Anim->GetChannelFlags(&Flags);
            Header[3] = Flags == 0x1FF ? 0 : 0x80;
            Chunk->WriteBytes(Header, 4);
            if (Flags == 0x1FF)
                return;
            for (unsigned long Channel = 0; Channel < 9; ++Channel)
            {
                IDAComponent* ChildObject = nullptr;
                Anim->GetChild(Channel, &ChildObject);
                INestedSingleRamp* const Child = static_cast<INestedSingleRamp*>(ChildObject);
                WriteAnimatedSingleRampBlock(Chunk, Child);
                if (Child != nullptr)
                    Child->Release();
            }
        }

        // LAB_06228350 (Row, Context; cdecl), the save's enumeration callback: Node_ClassName
        // (0xF54EF296) is skipped (the save writes it before the list); the type is the
        // row's kind (low word), with 0x8000 when a bool reads non-zero (+0x1c); the type
        // (2) and hash (4); then per kind the getter and its bytes. Getter results are not
        // tested and no ramp is null-checked (every row a set enumerates is one its own
        // getters serve). Always returns 0, so the walk visits every row.
        int WriteNodeProperty(const FNodePropertyRow* Row, void* ContextRaw)
        {
            if (Row->Hash == 0xF54EF296u)
                return 0;
            FNodeWriteContext* const Context = static_cast<FNodeWriteContext*>(ContextRaw);
            FAleChunk* const Chunk = Context->Chunk;
            IFxNodePropertySet* const Set = Context->Set;
            const unsigned long Hash = Row->Hash;
            const unsigned long Kind = Row->Kind;

            unsigned short Type = static_cast<unsigned short>(Kind);
            if (Kind == 1)
            {
                unsigned char Value = 0;
                Set->GetPropBool(Hash, &Value);
                if (Value != 0)
                    Type = static_cast<unsigned short>(Type | 0x8000);
            }
            Chunk->WriteBytes(&Type, 2);
            Chunk->WriteBytes(&Hash, 4);

            if (Kind == 1)
            {
            }
            else if (Kind == 2)
            {
                unsigned long Value = 0;
                Set->GetPropInt(Hash, &Value);
                Chunk->WriteBytes(&Value, 4);
            }
            else if (Kind == 3)
            {
                float Value = 0.0f;
                Set->GetPropFloat(Hash, &Value);
                Chunk->WriteBytes(&Value, 4);
            }
            else if (Kind == 0x100)
            {
                IRampSingle* Ramp = nullptr;
                Set->GetPropSingleRamp(Hash, &Ramp);
                WriteRampSingleBlock(Chunk, Ramp);
                Ramp->Release();
            }
            else if (Kind == 0x101)
            {
                IRampColor* Ramp = nullptr;
                Set->GetPropColorRamp(Hash, &Ramp);
                WriteRampColorBlock(Chunk, Ramp);
                Ramp->Release();
            }
            else if (Kind == 0x102)
            {
                IFxAnimatedSingle* Single = nullptr;
                Set->GetPropAnimatedSingle(Hash, &Single);
                WriteAnimatedSingleBlock(Chunk, Single);
                if (Single != nullptr)
                    Single->Release();
            }
            else if (Kind == 0x103)
            {
                const char* Name = nullptr;
                Set->GetPropName(Hash, &Name);
                Chunk->WriteString(Name);
            }
            else if (Kind == 0x104)
            {
                unsigned long Pair[2];
                Set->GetPropPair(Hash, Pair);
                Chunk->WriteBytes(Pair, 8);
            }
            else if (Kind == 0x105)
            {
                IAnimatedTransform* Anim = nullptr;
                Set->GetPropTransform(Hash, &Anim);
                WriteAnimatedTransformBlock(Chunk, Anim);
                if (Anim != nullptr)
                    Anim->Release();
            }
            else if (Kind == 0x200)
            {
                INestedSingleRamp* Ramp = nullptr;
                Set->GetPropSingleRampRamp(Hash, &Ramp);
                WriteRampSingleRampBlock(Chunk, Ramp);
                if (Ramp != nullptr)
                    Ramp->Release();
            }
            else if (Kind == 0x201)
            {
                IRampColorRamp* Ramp = nullptr;
                Set->GetPropColorRampRamp(Hash, &Ramp);
                WriteRampColorRampBlock(Chunk, Ramp);
                if (Ramp != nullptr)
                    Ramp->Release();
            }
            else if (Kind == 0x202)
            {
                INestedSingleRamp* Ramp = nullptr;
                Set->GetPropAnimatedSingleRamp(Hash, &Ramp);
                WriteAnimatedSingleRampBlock(Chunk, Ramp);
                if (Ramp != nullptr)
                    Ramp->Release();
            }
            else
            {
                // 0x062289a7.
                FDUMP(0x100001, "%s(%d) : ERROR:General:%s", kFxNodeLibraryCppPath, 0x257,
                      "Unknown type of property");
            }
            return 0;
        }
    } // namespace

    // FUN_062281c0's per-node body (0x0622826f..0x062282f7): the node's set (outer +0x80),
    // its Node_ClassName (+0x48) as a string, the set's rows through LAB_06228350
    // (+0x5c), a zero u16, the set released. Write results are ignored; returns 0.
    int WriteNodeLibProperties(FAleChunk* Chunk, IDAComponent* Node)
    {
        struct FPropertySetReference
        {
            FPropertySetReference() : Set(nullptr) {}
            ~FPropertySetReference()
            {
                if (Set != nullptr)
                    Set->Release();
            }
            IFxNodePropertySet* Set;
        } Owner;
        AsFxNode(Node)->GetPropertySet(&Owner.Set); // 0x06228280
        const char* ClassName = nullptr;
        Owner.Set->GetPropName(0xF54EF296u, &ClassName);
        Chunk->WriteString(ClassName);
        FNodeWriteContext Context;
        Context.Chunk = Chunk;
        Context.Set = Owner.Set;
        Owner.Set->EnumerateProps(&WriteNodeProperty, &Context);
        const unsigned short End = 0;
        Chunk->WriteBytes(&End, 2);
        return 0;
    }

} // namespace AlchemyImpl
