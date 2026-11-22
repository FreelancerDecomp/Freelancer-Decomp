// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "render/IRenderPipeline.h"
#include "x86math.h"
#include "luaprofile.h"
#include "render/shading.h"
#include "readfile.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include "MaterialRegex.h"
#include <utility>
#include <string>
#include <vector>

static const void* kRegisterPriority = (const void*)0x80000000;

// D3D8 numbers (enum names are d3d8.h matches, not DLL strings).
static const D3DRENDERSTATETYPE kRsLighting = D3DRS_LIGHTING;
static const D3DRENDERSTATETYPE kRsColorVertex = D3DRS_COLORVERTEX;
static const D3DRENDERSTATETYPE kRsSpecularEnable = D3DRS_SPECULARENABLE;
static const D3DRENDERSTATETYPE kRsAlphaBlendEnable = D3DRS_ALPHABLENDENABLE;
static const D3DRENDERSTATETYPE kRsSrcBlend = D3DRS_SRCBLEND;
static const D3DRENDERSTATETYPE kRsDestBlend = D3DRS_DESTBLEND;
static const D3DRENDERSTATETYPE kRsZWriteEnable = D3DRS_ZWRITEENABLE;
static const D3DRENDERSTATETYPE kRsZFunc = D3DRS_ZFUNC;
static const D3DRENDERSTATETYPE kRsCullMode = D3DRS_CULLMODE;
static const D3DRENDERSTATETYPE kRsAlphaRef = D3DRS_ALPHAREF;
static const D3DRENDERSTATETYPE kRsAlphaFunc = D3DRS_ALPHAFUNC;
static const D3DRENDERSTATETYPE kRsAlphaTestEnable = D3DRS_ALPHATESTENABLE;
static constexpr FlULong kZFuncLessEqual = 4;

static constexpr FlULong kTssColorOp = 1;
static constexpr FlULong kTssColorArg1 = 2;
static constexpr FlULong kTssColorArg2 = 3;
static constexpr FlULong kTssAlphaOp = 4;
static constexpr FlULong kTssAlphaArg1 = 5;
static constexpr FlULong kTssAlphaArg2 = 6;
static constexpr FlULong kTssTexCoordIndex = 0xB;
static constexpr FlULong kTssAddressU = 0xD;
static constexpr FlULong kTssAddressV = 0xE;
static constexpr FlULong kTssTextureTransformFlags = 0x18;

static constexpr FlULong kTopDisable = 1;
static constexpr FlULong kTopSelectArg1 = 2;
static constexpr FlULong kTopModulate = 4;
static constexpr FlULong kTopModulate2X = 5;
static constexpr FlULong kTopAdd = 7;
static constexpr FlULong kTaDiffuse = 0;
static constexpr FlULong kTaCurrent = 1;
static constexpr FlULong kTaTexture = 2;
static constexpr FlULong kBlendZero = 1;
static constexpr FlULong kBlendSrcAlpha = 5;
static constexpr FlULong kBlendInvSrcAlpha = 6;
static constexpr FlULong kBlendDestColor = 9;
static constexpr FlULong kBlendOne = 2;

static constexpr FlULong kSubmitFlagIrDraw = 0x100;
static constexpr FlULong kSubmitFlagSkipAlphaTest = 0x400;


// DAT_06ed4848 dword[4]: NONE stays, CW↔CCW. Indexed by D3DRS_CULLMODE (0x16).
static constexpr FlULong kCullFlip[4] = {0, 1, 3, 2};

// D3D8 D3DPRIMITIVETYPE. SphereMesh slot-10 arg2 is immediate 4 (TRIANGLELIST).
static constexpr FlULong kD3dPrimTriangleList = 4;
static constexpr FlULong kD3dPrimMax = 6;

enum EMaterialBind
{
    BindDcDt = 0,
    BindLightMap,
    BindNebula,
    BindAtmosphere,
    BindDcDtEt,
    BindEcEt,
    BindDetailMap,
    BindBtDetail,
    BindNull,
    BindStub
};

enum EMaterialSubmit
{
    SubmitDcDt = 0,
    SubmitDcDtTwo,
    SubmitKernel,
    SubmitLightMap,
    SubmitDetail2Pass,
    SubmitIllumDetail2Pass,
    SubmitMaskedDetail2Pass,
    SubmitAtmosphere,
    SubmitNull,
    SubmitStub,
    SubmitBtDetailTwo
};

// FShadingPropertyValue / FShadingPropertyRecord (FUN_06eb28f0, stride 0x4C) are public now:
// render/shading.h (included above), because freelancer.exe's SwappedMaterial reads them.

inline bool IidEqual(const char* Left, const char* Right)
{
    if (Left == nullptr || Right == nullptr)
        return false;
    return std::strcmp(Left, Right) == 0;
}

inline GENRESULT QiFrom(void* Object, const char* Iid, void** Out)
{
    if (Out != nullptr)
        *Out = nullptr;
    if (Object == nullptr || Iid == nullptr || Out == nullptr)
        return GR_GENERIC;
    IDAComponent* Component = (IDAComponent*)Object;
    return Component->QueryInterface(Iid, Out);
}

inline void* VtableSlot(void* Object, FlULong ByteOffset)
{
    if (Object == nullptr)
        return nullptr;
    void** Table = *(void***)Object;
    return Table[ByteOffset / 4];
}

inline void PipeSetMaterial(IRenderPipeline* Pipeline, const float* MaterialBlock)
{
    if (Pipeline != nullptr)
        SetMaterialBlock(Pipeline, MaterialBlock);
}

inline void PipeSetRs(IRenderPipeline* Pipeline, D3DRENDERSTATETYPE State, FlULong Value)
{
    if (Pipeline != nullptr)
        Pipeline->SetRenderState(State, (int)Value);
}

inline FlULong PipeGetRs(IRenderPipeline* Pipeline, D3DRENDERSTATETYPE State)
{
    int Value = 0;
    if (Pipeline != nullptr)
        Pipeline->GetRenderState(State, &Value);
    return (FlULong)Value;
}

// IRenderPipeline +0xC8 slot 50 — get_texture_format (FUN_06d0e3d9).
inline GENRESULT PipeGetTextureFormat(IRenderPipeline* Pipeline, FlULong Handle,
                                      int* OutFormat)
{
    if (Pipeline == nullptr || OutFormat == nullptr)
        return GR_GENERIC;
    return Pipeline->GetTextureFormat((void*)Handle, OutFormat);
}

static constexpr FlULong kPixelFormatField04[15] = {
    0, 8, 0x18, 0x10, 0x10, 0x10, 0x10, 0x20, 0x20, 0, 0, 0, 0, 0, 0};
static constexpr FlULong kPixelFormatField08[15] = {0, 0, 8, 5, 5, 4, 5, 8, 8, 0, 0, 0, 0, 0, 0};
static constexpr FlULong kPixelFormatField18[15] = {0, 0, 8, 6, 5, 4, 5, 8, 8, 0, 0, 0, 0, 0, 0};
static constexpr FlULong kPixelFormatField28[15] = {0, 0, 8, 5, 5, 4, 5, 8, 8, 0, 0, 0, 0, 0, 0};
static constexpr FlULong kPixelFormatAlphaBits[15] = {
    0, 0, 0, 0, 0, 4, 1, 8, 0, 0, 1, 0, 0, 0, 0};
static constexpr unsigned char kPixelFormatHasAlpha[15] = {0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1};
static constexpr int kDxtFormatIndexMin = 10;
static constexpr int kDxtFormatIndexMax = 14;
static constexpr int kPixelFormatFourCcCount = 22;
// DAT_06ed51d8 — FUN_06ed0b50 / FUN_06ed0b80 (ids 9…21).
static constexpr FlULong kPixelFormatFourCc[22] = {
    0, 0x29, 0x14, 0x17, 0x18, 0x1A, 0x19, 0x15, 0x16, 0, 0x31545844u, 0x32545844u,
    0x33545844u, 0x34545844u, 0x35545844u, 0x504F4144u, 0x544F4144u, 0x41414144u,
    0x4C414144u, 0x31414144u, 0x34414144u, 0x38414144u};

constexpr FlULong PixelFormatAlphaBits(int FormatIndex)
{
    if (FormatIndex < 0 || FormatIndex > 14)
        return kPixelFormatAlphaBits[0];
    return kPixelFormatAlphaBits[FormatIndex];
}

constexpr unsigned char PixelFormatHasAlpha(int FormatIndex)
{
    if (FormatIndex < 0 || FormatIndex > 14)
        return kPixelFormatHasAlpha[0];
    return kPixelFormatHasAlpha[FormatIndex];
}

inline void PipeSetTss(IRenderPipeline* Pipeline, FlULong Stage, FlULong Type,
                       FlULong Value)
{
    if (Pipeline != nullptr)
        Pipeline->SetTextureStageState((int)Stage, (int)Type,
                                       (int)Value);
}

inline void PipeSetTexture(IRenderPipeline* Pipeline, FlULong Stage, FlULong Handle)
{
    if (Pipeline != nullptr)
        Pipeline->SetTextureStageTexture((int)Stage,
                                         (void*)Handle);
}

// IRenderPipeline +0x100 slot 64 — set_texture_stage_transform (FUN_06d0fdd2).
inline void PipeSetTextureTransform(IRenderPipeline* Pipeline, FlULong Stage,
                                    const float* Matrix)
{
    if (Pipeline == nullptr || Matrix == nullptr)
        return;
    Pipeline->SetTextureStageTransform((int)Stage, Matrix);
}

inline GENRESULT PipeVerify(IRenderPipeline* Pipeline)
{
    if (Pipeline != nullptr)
        return Pipeline->VerifyState();
    return GR_OK;
}

// Draw a borrowed vertex buffer with its base vertex and index span.
inline GENRESULT PipeDrawIndexedVb(IRenderPipeline* Pipeline, FlULong Prim, void* Vb,
                                   FlULong StartIndex, FlULong NumVerts, void* Indices,
                                   FlULong Count)
{
    if (Pipeline == nullptr)
        return GR_GENERIC;
    return Pipeline->DrawIndexedPrimitiveVb(Prim, Vb, (unsigned int)StartIndex,
                                            (unsigned int)NumVerts, Indices,
                                            (unsigned int)Count, 0);
}

// Convert the published opaque file-system argument at the local boundary.
inline IFileSystem* AsFileSystem(void* FileSystem)
{
    return (IFileSystem*)FileSystem;
}

inline IVertexBufferManager* AsVbMgr(void* VbMgr)
{
    return (IVertexBufferManager*)VbMgr;
}

inline IRPDraw* AsRpDraw(void* Draw)
{
    return (IRPDraw*)Draw;
}

inline ITextureLibrary2* AsTexLib(void* TexLib)
{
    return (ITextureLibrary2*)TexLib;
}

inline GENRESULT RpDrawIndexed(void* Draw, FlULong Prim, FlULong MinIndex,
                               FlULong NumVertices, FlULong StartIndex,
                               FlULong Count)
{
    if (Draw == nullptr)
        return GR_GENERIC;
    return AsRpDraw(Draw)->DrawIndexedPrimitive(Prim, (unsigned int)MinIndex,
                                      (unsigned int)NumVertices,
                                      (unsigned int)StartIndex,
                                      (unsigned int)Count);
}

// FUN_06eb5940
constexpr FlULong FvfPosSize(FlULong Fvf)
{
    const unsigned char Bits = (unsigned char)Fvf;
    if ((Bits & 0xC) == 0xC)
        return 0x1C;
    if ((Bits & 4) == 4)
        return 0x10;
    if ((Bits & 2) == 2)
        return 0xC;
    return 0;
}

// FUN_06eb5980
constexpr FlULong FvfColorSize(FlULong Fvf)
{
    FlULong Size = 0;
    if ((Fvf & 0x40) != 0)
        Size = 4;
    if ((Fvf & 0x80) != 0)
        Size += 4;
    return Size;
}

// FUN_06eb3310
constexpr FlULong FvfTexSize(FlULong Fvf)
{
    FlULong Size = 0;
    FlULong Remaining = (Fvf >> 8) & 0xF;
    unsigned char Shift = 0x10;
    while (Remaining != 0)
    {
        switch ((Fvf >> (Shift & 0x1F)) & 3)
        {
        case 0:
            Size += 8;
            break;
        case 1:
            Size += 0xC;
            break;
        case 2:
            Size += 0x10;
            break;
        case 3:
            Size += 4;
            break;
        }
        Shift = (unsigned char)(Shift + 2);
        Remaining -= 1;
    }
    return Size;
}

constexpr FlULong FvfNormalSize(FlULong Fvf)
{
    if ((Fvf & 0x10) != 0)
        return 0xC;
    return 0;
}

constexpr FlULong FvfStride(FlULong Fvf)
{
    return FvfPosSize(Fvf) + FvfNormalSize(Fvf) + FvfColorSize(Fvf) + FvfTexSize(Fvf);
}
static_assert(FvfStride(0x112) == 0x20); // XYZ | NORMAL | TEX1
static_assert(FvfStride(0x144) == 0x1C); // XYZRHW | DIFFUSE | TEX1

inline GENRESULT VbMgrLock(void* VbMgr, FlULong Fvf, FlULong Count,
                           FVertexMemoryLock* Out)
{
    if (VbMgr == nullptr || Out == nullptr)
        return GR_GENERIC;
    return AsVbMgr(VbMgr)->LockVertexMemory((unsigned int)Fvf,
                                   (unsigned int)Count, Out);
}

inline void VbMgrUnlock(void* VbMgr, FVertexMemoryLock* Out)
{
    if (VbMgr == nullptr || Out == nullptr)
        return;
    AsVbMgr(VbMgr)->UnlockVertexMemory(Out);
}

// IVertexBufferManager+0x20 FUN_06d114c5 → FUN_06d1f040. RET 0x18.
inline void VbMgrCopy(IVertexBufferManager* VbMgr, void* Dest, FlULong Fvf, const FRpVertexSrcDesc* Mesh,
                      FlULong Start, FlULong Count)
{
    if (VbMgr == nullptr || Dest == nullptr || Mesh == nullptr)
        return;
    VbMgr->CopyVertexData(Dest, Fvf, Mesh, Start, Count);
}

inline FlULong PipeGetState(IRenderPipeline* Pipeline, FlULong Id)
{
    int Value = 0;
    if (Pipeline != nullptr)
        Pipeline->GetPipelineState((int)Id, &Value);
    return (FlULong)Value;
}

inline void PipeSetWorld12(IRenderPipeline* Pipeline, const float* M12)
{
    if (Pipeline == nullptr || M12 == nullptr)
        return;
    Pipeline->SetWorld(M12);
}

inline void PipeSetModelview12(IRenderPipeline* Pipeline, const float* M12)
{
    if (Pipeline == nullptr || M12 == nullptr)
        return;
    Pipeline->SetModelview(M12);
}

inline int FileSystemOpenNode(void* FileSystem, const char* Name)
{
    return AsFileSystem(FileSystem)->OpenNode(Name);
}

inline int FileSystemClose(void* FileSystem, FlULong Handle)
{
    return (FileSystem != nullptr) ? AsFileSystem(FileSystem)->CloseFile(Handle) : 0;
}

inline int FileSystemRead(void* FileSystem, FlULong Handle, void* Buffer,
                          FlULong Size, FlULong* BytesRead)
{
    return (FileSystem != nullptr) ? AsFileSystem(FileSystem)->Read(Handle, Buffer, Size, BytesRead, 0) : 0;
}

inline FlULong FileSystemGetSize(void* FileSystem, FlULong Handle)
{
    return (FileSystem != nullptr) ? AsFileSystem(FileSystem)->GetSize(Handle, 0) : 0;
}

inline int FileSystemFindFirst(void* FileSystem, const char* Pattern,
                               WIN32_FIND_DATAA* FindData)
{
    if (FileSystem == nullptr)
        return -1;
    return AsFileSystem(FileSystem)->FindFirst(Pattern,
                              FindData);
}

inline int FileSystemFindNext(void* FileSystem, int Handle, WIN32_FIND_DATAA* FindData)
{
    if (FileSystem == nullptr)
        return 0;
    return AsFileSystem(FileSystem)->FindNext((FlULong)Handle,
                              FindData);
}

inline int FileSystemFindClose(void* FileSystem, int Handle)
{
    return (FileSystem != nullptr) ? AsFileSystem(FileSystem)->FindClose((FlULong)Handle) : 0;
}

inline int FileSystemOpenByDesc(void* FileSystem, void* Desc)
{
    if (FileSystem == nullptr)
        return -1;
    return AsFileSystem(FileSystem)->OpenByDesc((FFileSystemDesc*)Desc);
}

// RS/TSS table walk:
// first dword = start state/type; [+4]=value; [+8]=next; stride 8; stop at next==0.
inline void ApplyStateTable(IRenderPipeline* Pipeline, const FlULong* Table,
                            bool bTextureStage,
                            FlULong Stage)
{
    if (Pipeline == nullptr || Table == nullptr)
        return;
    FlULong State = Table[0];
    FlULong Index = 0;
    if (State == 0)
        return;
    while (true)
    {
        const FlULong Value = Table[Index + 1];
        if (bTextureStage)
            PipeSetTss(Pipeline, Stage, State, Value);
        else
            PipeSetRs(Pipeline, (D3DRENDERSTATETYPE)State, Value);
        const FlULong Next = Table[Index + 2];
        if (Next == 0)
            break;
        State = Next;
        Index += 2;
    }
}

inline bool MatchSimplePattern(const char* Pattern, const char* Text)
{
    const std::unique_ptr<FMaterialRegex> Expression = FMaterialRegex::Compile(Pattern);
    return FMaterialRegex::Search(Expression.get(), Text);
}


// Shared ReadFile descriptor; Size selects its DOS/UTF prefix.
inline void InitializeShadingFileDesc(FFileSystemDesc* Desc, const char* Path,
                                      const char* ComponentName = "FileSystem")
{
    std::memset(Desc, 0, sizeof(*Desc));
    Desc->Size = 0x34;
    Desc->ComponentName = ComponentName;
    Desc->Path = Path;
    Desc->Access = 0x80000000;
    Desc->ShareMode = 1;
    Desc->Creation = 3;
    Desc->Flags = 0x08000080;
    Desc->Field30 = 0xFFFFFFFF;
}

inline bool ReadNamedBytes(void* FileSystem, const char* Name, void* Buffer, FlULong Size)
{
    if (FileSystem == nullptr || Name == nullptr || Buffer == nullptr)
        return false;
    FFileSystemDesc Desc;
    InitializeShadingFileDesc(&Desc, Name);
    const int Handle = FileSystemOpenByDesc(FileSystem, &Desc);
    if (Handle == -1)
        return false;
    FlULong BytesRead = 0;
    const int ReadOk =
        FileSystemRead(FileSystem, (FlULong)Handle, Buffer, Size, &BytesRead);
    FileSystemClose(FileSystem, (FlULong)Handle);
    // FUN_06eb3450 treats a zero Read result as failure even if a provider
    // happens to report the requested byte count; preserve that contract.
    return ReadOk != 0 && BytesRead == Size;
}

inline int FileSystemWrite(void* FileSystem, FlULong Handle, const void* Buffer,
                           FlULong Size, FlULong* BytesWritten)
{
    if (FileSystem == nullptr)
        return 0;
    return AsFileSystem(FileSystem)->Write(Handle, Buffer, Size, BytesWritten, nullptr);
}

inline bool WriteNamedBytes(void* FileSystem, const char* Name, const void* Buffer,
                            FlULong Size)
{
    if (FileSystem == nullptr || Name == nullptr || Buffer == nullptr)
        return false;
    FFileSystemDesc Desc;
    InitializeShadingFileDesc(&Desc, Name);
    Desc.Implementation = "UTF";
    Desc.Access = 0xC0000000;
    Desc.Creation = 2;
    const int Handle = FileSystemOpenByDesc(FileSystem, &Desc);
    if (Handle == -1)
        return false;
    FlULong BytesWritten = 0;
    const int WriteOk = FileSystemWrite(FileSystem, (FlULong)Handle, Buffer,
                                        Size, &BytesWritten);
    FileSystemClose(FileSystem, (FlULong)Handle);
    return WriteOk != 0 && BytesWritten == Size;
}

// UTF +0x7C OpenByDesc is the retail no-IDataBuffer stub. Named leaves: OpenNode + Read(0) + "..".
inline FlULong ReadUtfNamedLeaf(void* FileSystem, const char* Name, void* Buffer,
                                      FlULong MaxBytes)
{
    if (FileSystem == nullptr || Name == nullptr || Buffer == nullptr || MaxBytes == 0)
        return 0;
    if (FileSystemOpenNode(FileSystem, Name) == 0)
        return 0;
    FlULong Size = FileSystemGetSize(FileSystem, 0);
    if (Size > MaxBytes)
        Size = MaxBytes;
    FlULong BytesRead = 0;
    int ReadOk = 0;
    if (Size != 0)
        ReadOk = FileSystemRead(FileSystem, 0, Buffer, Size, &BytesRead);
    FileSystemOpenNode(FileSystem, "..");
    if (ReadOk == 0)
        return 0;
    return BytesRead;
}

struct FTexBlock
{
    const char* Name;           // +0
    int TextureId;              // +4
    FlULong Handle;       // +8  TexLib +0x70 writes 5 dwords from here
    float Uv0;                  // +C
    float Uv1;                  // +10
    float Uv2;                  // +14
    float Uv3;                  // +18
    FlULong Flags;        // +1C
    FlULong AddressU;     // +20
    FlULong AddressV;     // +24
    unsigned char bSkipResolve; // +28
    unsigned char Pad29[3];
};

FL_LAYOUT_ASSERT(sizeof(FTexBlock) == 0x2C, "FTexBlock stride 0x2C");

// Serialized frame first word selects a canonical child, never a GPU handle.
struct FTextureFrame
{
    FlULong ChildIndex;
    float Uv[4];
};
FL_LAYOUT_ASSERT(sizeof(FTextureFrame) == 0x14, "Texture frame file payload");

// Canonical name record: own GPU handle, frame descriptors and child pointers.
// Playback state is independently owned by FTexturePlayback in TextureLibrary.h.
struct FTextureEntry
{
    int RefCount;
    FlULong GpuHandle; // retail canonical record+8
    FlULong Crc;
    bool bHasName; // Distinguish retail null name from an allocated empty string.
    std::string FullName;
    char Name[128]; // Compatibility name mirror, identity uses FullName.
    std::vector<FTextureFrame> Frames;
    std::vector<FTextureEntry*> Children;
    float TileRate;

    FTextureEntry() : RefCount(0), GpuHandle(0), Crc(0), bHasName(false), TileRate(0.0f)
    {
        Name[0] = '\0';
    }

    // 06ec0ca4..0ceb: release the owned name, then child storage, then frames.
    ~FTextureEntry()
    {
        std::string().swap(FullName);
        std::vector<FTextureEntry*>().swap(Children);
        std::vector<FTextureFrame>().swap(Frames);
    }

    const char* GetName() const
    {
        return bHasName ? FullName.c_str() : nullptr;
    }

    void SetName(const char* NewName)
    {
        bHasName = NewName != nullptr;
        if (NewName == nullptr)
        {
            FullName.clear();
            Name[0] = '\0';
            return;
        }
        FullName.assign(NewName);
        std::strncpy(Name, NewName, sizeof(Name) - 1);
        Name[sizeof(Name) - 1] = '\0';
    }
};

struct FMaterialMapRule
{
    int Key = 1;
    bool bNamePattern = false;
    std::unique_ptr<FMaterialRegex> Expression;
    std::string Pattern;
    std::string Type;

    FMaterialMapRule() = default;
    // 06ecde20 / 06ece1c0: release program, pattern, then replacement type.
    ~FMaterialMapRule()
    {
        Expression.reset();
        std::string().swap(Pattern);
        std::string().swap(Type);
    }
    // 06ecd320: move owning fields, retain the source key.
    FMaterialMapRule(FMaterialMapRule&& Other) noexcept
        : Key(Other.Key), bNamePattern(Other.bNamePattern),
          Expression(std::move(Other.Expression)), Pattern(std::move(Other.Pattern)),
          Type(std::move(Other.Type))
    {
        Other.Pattern.clear();
        Other.Type.clear();
    }

    FMaterialMapRule& operator=(FMaterialMapRule&& Other) noexcept
    {
        Key = Other.Key;
        bNamePattern = Other.bNamePattern;
        Expression = std::move(Other.Expression);
        Pattern = std::move(Other.Pattern);
        Type = std::move(Other.Type);
        Other.Pattern.clear();
        Other.Type.clear();
        return *this;
    }
};

struct FMaterialGroup
{
    FlULong Id;
    // Retail duplicates group names into heap storage; keep native ownership
    // dynamic so SetGroupName does not truncate valid long names.
    std::string Name;
    bool bHasName = false;
    std::vector<FlULong> Members;
};

struct FCrcSlot : FMaterialLibraryRecord
{
    FlULong Crc;
};

struct FBatchRule
{
    FlULong Kind = 0;
    std::string Pattern;
    // Destruction is compiled program before original pattern, as in 8f20/9080.
    std::unique_ptr<FMaterialRegex> Expression;

    FBatchRule() = default;
    FBatchRule(FBatchRule&& Other) noexcept = default;
    FBatchRule& operator=(FBatchRule&& Other) noexcept
    {
        // vector erase moves over the removed element: discard its program first.
        Expression = std::move(Other.Expression);
        Pattern = std::move(Other.Pattern);
        Kind = Other.Kind;
        return *this;
    }
};

class FMaterial;
class FMaterialLibrary;
class FMaterialBatcher;
class FTextureLibrary;
class FMaterialAnimation;

FMaterial* CreateMaterialInstance(const char* FactoryName);
FMaterial* AsShadingFMaterial(IMaterial* Material);
void RegisterShadingComponents();
