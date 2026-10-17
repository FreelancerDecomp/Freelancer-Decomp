// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "EyeMaterial.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <new>

// .data:06602ad0, read from the PE bytes (the listing truncates it): the __FILE__ of the one
// FDUMP site in the material code (065ff37f, line 0x23F).
static const char* const G_FdumpFileMaterialImpl =
    "C:\\work\\builds\\dalibs\\DALIBS~1\\build\\include\\imaterialimpl.h";

// .data:066031ac: four zero bytes, the Description of every EyeMaterial parameter.
static const char G_ParamNoDescription[4] = {0, 0, 0, 0};
// .data:06602b30.
static const char* const G_TextureNameNone = "(None)";
// .rdata:066013fc and .rdata:0660140c: the defaults / ranges LAB_065ff750 hands FUN_065ffab0
// by address.
static const float G_ParamZeros[4] = {0.0f, 0.0f, 0.0f, 0.0f};
static const float G_ParamOnes[4] = {1.0f, 1.0f, 1.0f, 1.0f};
// .rdata:06601578: FLT_EPSILON, the slack of LAB_065ff470's range test.
static const float G_ParamRangeSlack = 1.1920928955078125e-07f;
// .rdata:06601580 (a double): FUN_066001c0's dead zone.
static const double G_EyeUvDeadZone = 1.0e-4;

static void* DwordToPointer(unsigned long Value)
{
    return (void*)((uintptr_t)(Value));
}

static unsigned long PointerToDword(const void* Pointer)
{
    return (unsigned long)((uintptr_t)(Pointer));
}


namespace
{
    struct FPixelFormatChannel
    {
        unsigned long Bits;
        unsigned long Mask;
        unsigned long Shift;
        unsigned long Loss;
    };

    // DAT_06602c88 entry (0x4C).
    struct FPixelFormatDesc
    {
        unsigned long Id;            // +0x00
        unsigned long Bpp;           // +0x04
        FPixelFormatChannel Red;     // +0x08
        FPixelFormatChannel Green;   // +0x18
        FPixelFormatChannel Blue;    // +0x28
        FPixelFormatChannel Alpha;   // +0x38
        unsigned long HasAlpha;      // +0x48 (low byte read)
    };

    static_assert(sizeof(FPixelFormatDesc) == 0x4C, "PixelFormat descriptor is 0x4C bytes");

    // DAT_06602c30 (.data, 0x16 dwords), read verbatim from the PE bytes.
    unsigned long G_PixelFormatD3D[0x16] = {
        0x0,        0x29,       0x14,       0x17,       0x18,       0x1a,
        0x19,       0x15,       0x16,       0x0,        0x31545844, 0x32545844,
        0x33545844, 0x34545844, 0x35545844, 0x504f4144, 0x544f4144, 0x41414144,
        0x4c414144, 0x31414144, 0x34414144, 0x38414144};

    // DAT_06602c88 (.data, 15 x 0x4C), read verbatim from the PE bytes.
    FPixelFormatDesc G_PixelFormatDescs[15] = {
        {0, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
        {1, 8, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
        {2, 0x18, {8, 0xff0000, 0x10, 0}, {8, 0xff00, 8, 0}, {8, 0xff, 0, 0}, {0, 0, 0, 0}, 0},
        {3, 0x10, {5, 0xf800, 0xb, 3}, {6, 0x7e0, 5, 2}, {5, 0x1f, 0, 3}, {0, 0, 0, 0}, 0},
        {4, 0x10, {5, 0x7c00, 0xa, 3}, {5, 0x3e0, 5, 3}, {5, 0x1f, 0, 3}, {0, 0, 0, 0}, 0},
        {5, 0x10, {4, 0xf00, 8, 4}, {4, 0xf0, 4, 4}, {4, 0xf, 0, 4}, {4, 0xf000, 0xc, 4}, 1},
        {6, 0x10, {5, 0x7c00, 0xa, 3}, {5, 0x3e0, 5, 3}, {5, 0x1f, 0, 3}, {1, 0x8000, 0xf, 7}, 1},
        {7, 0x20, {8, 0xff0000, 0x10, 0}, {8, 0xff00, 8, 0}, {8, 0xff, 0, 0},
         {8, 0xff000000, 0x18, 0}, 1},
        {8, 0x20, {8, 0xff0000, 0x10, 0}, {8, 0xff00, 8, 0}, {8, 0xff, 0, 0}, {0, 0, 0, 0}, 0},
        {9, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, 0},
        {0xa, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {1, 0, 0, 0}, 1},
        {0xb, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, 1},
        {0xc, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, 1},
        {0xd, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, 1},
        {0xe, 0, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, 1}};
}

// FUN_06600d90: MOV EAX,DAT_06602c30; RET.
static unsigned long* PixelFormatGetD3DFormats()
{
    return G_PixelFormatD3D;
}

// FUN_06600da0 (cdecl): the index of the first entry equal to D3DFormat among 0x16, else 0.
static int PixelFormatFromD3D(unsigned long D3DFormat)
{
    const unsigned long* const Formats = PixelFormatGetD3DFormats();
    int Index = 0;
    while (Index < 0x16)
    {
        if (Formats[Index] == D3DFormat)
            return Index;
        Index += 1;
    }
    return 0;
}

// FUN_06600dc0 (cdecl): Id > 14 (signed JLE) -> entry 0, else &DAT_06602c88[Id].
static FPixelFormatDesc* PixelFormatGetDesc(int Id)
{
    if (Id > 0xe)
        return &G_PixelFormatDescs[0];
    return &G_PixelFormatDescs[Id];
}


static GENRESULT MaterialFsResult(bool bOk)
{
    return (GENRESULT)(((unsigned long)(bOk ? 1 : 0) - 1u) & 0x80004005u);
}

static void FillMaterialWriteDesc(FFileSystemDesc* Desc, const char* Key)
{
    // 066006e0..0731: the reader fields, then +8 "UTF", +0x10 0xC0000000, +0x1C 2.
    FillFileDesc(Desc, Key);
    Desc->Implementation = "UTF";
    Desc->Access = 0xC0000000;
    Desc->Creation = 2;
}

// FUN_066003a0 (FS, Key, Out): Read(h, &Data, 4, &Got, 0); *Out = Data only when the read
// answered non-zero with Got == 4.
static GENRESULT MaterialFsReadDword(IFileSystem* Fs, const char* Key, unsigned long* Out)
{
    FFileSystemDesc Desc;
    FillFileDesc(&Desc, Key);
    bool bOk = false;
    const int Handle = Fs->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        unsigned long Data;
        unsigned long Got;
        if (Fs->Read((unsigned long)(Handle), &Data, 4, &Got, 0) != 0 && Got == 4)
        {
            *Out = Data;
            bOk = true;
        }
        Fs->CloseFile((unsigned long)(Handle));
    }
    return MaterialFsResult(bOk);
}

// FUN_06600460: FUN_066003a0 with the dword moved through FLD/FSTP float.
static GENRESULT MaterialFsReadFloat(IFileSystem* Fs, const char* Key, float* Out)
{
    FFileSystemDesc Desc;
    FillFileDesc(&Desc, Key);
    bool bOk = false;
    const int Handle = Fs->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        float Data;
        unsigned long Got;
        if (Fs->Read((unsigned long)(Handle), &Data, 4, &Got, 0) != 0 && Got == 4)
        {
            *Out = Data;
            bOk = true;
        }
        Fs->CloseFile((unsigned long)(Handle));
    }
    return MaterialFsResult(bOk);
}

// FUN_06600520 (FS, Key, Count, Out, OutCount): with OutCount, Count = min(GetSize(h, 0) >> 2,
// Count) is stored through it first; then Read(h, Out, Count * 4) must deliver all of it.
static GENRESULT MaterialFsReadFloats(IFileSystem* Fs, const char* Key, unsigned long Count,
                                      float* Out, unsigned long* OutCount)
{
    FFileSystemDesc Desc;
    FillFileDesc(&Desc, Key);
    bool bOk = false;
    const int Handle = Fs->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        if (OutCount != nullptr)
        {
            const unsigned long Available = Fs->GetSize((unsigned long)(Handle), 0) >> 2;
            if (Available < Count)
                Count = Available;
            *OutCount = Count;
        }
        const unsigned long Bytes = Count * 4;
        unsigned long Got;
        bOk = Fs->Read((unsigned long)(Handle), Out, Bytes, &Got, 0) != 0 && Got == Bytes;
        Fs->CloseFile((unsigned long)(Handle));
    }
    return MaterialFsResult(bOk);
}

static GENRESULT MaterialFsReadBlock(IFileSystem* Fs, const char* Key, unsigned long Size,
                                     char** Out)
{
    FFileSystemDesc Desc;
    FillFileDesc(&Desc, Key);
    bool bOk = false;
    const int Handle = Fs->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        if (Size == 0)
            Size = Fs->GetSize((unsigned long)(Handle), 0);
        char* const Block = new (std::nothrow) char[Size];
        if (Block != nullptr)
        {
            *Out = Block;
            unsigned long Got;
            bOk = Fs->Read((unsigned long)(Handle), Block, Size, &Got, 0) != 0 &&
                  Got == Size;
        }
        Fs->CloseFile((unsigned long)(Handle));
    }
    return MaterialFsResult(bOk);
}

// IFileSystem::Write +0x18: (h, Src, Size, &Done, 0).
static bool MaterialFsWrite(IFileSystem* Fs, int Handle, const void* Src, unsigned long Size)
{
    unsigned long Done;
    return Fs->Write((unsigned long)(Handle), Src, Size, &Done, nullptr) != 0 &&
           Done == Size;
}

// FUN_066006d0 (FS, Key, Src): 4 bytes.
static GENRESULT MaterialFsWriteDword(IFileSystem* Fs, const char* Key, const void* Src)
{
    FFileSystemDesc Desc;
    FillMaterialWriteDesc(&Desc, Key);
    bool bOk = false;
    const int Handle = Fs->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        bOk = MaterialFsWrite(Fs, Handle, Src, 4);
        Fs->CloseFile((unsigned long)(Handle));
    }
    return MaterialFsResult(bOk);
}

// FUN_06600790 (FS, Key, Count, Src): Count * 4 bytes.
static GENRESULT MaterialFsWriteFloats(IFileSystem* Fs, const char* Key, unsigned long Count,
                                       const void* Src)
{
    FFileSystemDesc Desc;
    FillMaterialWriteDesc(&Desc, Key);
    bool bOk = false;
    const int Handle = Fs->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        bOk = MaterialFsWrite(Fs, Handle, Src, Count * 4);
        Fs->CloseFile((unsigned long)(Handle));
    }
    return MaterialFsResult(bOk);
}

// FUN_06600850 (FS, Key, Size, Src): Size bytes.
static GENRESULT MaterialFsWriteBlock(IFileSystem* Fs, const char* Key, unsigned long Size,
                                      const void* Src)
{
    FFileSystemDesc Desc;
    FillMaterialWriteDesc(&Desc, Key);
    bool bOk = false;
    const int Handle = Fs->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        bOk = MaterialFsWrite(Fs, Handle, Src, Size);
        Fs->CloseFile((unsigned long)(Handle));
    }
    return MaterialFsResult(bOk);
}

// FUN_065fe590 (cdecl: Flags, Which): Which 0 -> Flags & 3, 1 -> (Flags >> 2) & 3, else 0.
static unsigned long MaterialAddressModeBits(unsigned long Flags, int Which)
{
    if (Which == 0)
        return Flags & 3u;
    if (Which == 1)
        return (Flags >> 2) & 3u;
    return 0;
}

// The pair index a type-9 parameter works on: 0065fe4e8 / 065fe86f -- flag 0x20000 -> 0 (U),
// else 0x40000 -> 1 (V), else 0.
static int MaterialAddressModeWhich(unsigned long Flags)
{
    if ((Flags & 0x20000) != 0)
        return 0;
    if ((Flags & 0x40000) != 0)
        return 1;
    return 0;
}

// =============================================================================================
// FMaterialParamValue
// =============================================================================================

FMaterialParamValue::FMaterialParamValue()
    : Type(0)
{
    Value[0] = 0;
}

FMaterialParamValue::FMaterialParamValue(int InType)
    : Type(0)
{
    Value[0] = 0;
    if (InType != 0)
        Clear();
    Type = InType;
}

FMaterialParamValue::~FMaterialParamValue()
{
    Clear();
}

void FMaterialParamValue::Clear()
{
    // FUN_065fe5c0, 065fe5c0..e613.
    if (Type == 5 || Type == 7)
    {
        if (Value[0] != 0)
        {
            HeapFree(GetProcessHeap(), 0, DwordToPointer(Value[0]));
            Value[0] = 0;
        }
        Value[0] = 0;
        return;
    }
    if (Type == 6)
    {
        IDAComponent* const Com = (IDAComponent*)(DwordToPointer(Value[0]));
        Value[0] = 0;
        if (Com != nullptr)
        {
            Com->Release();
            Value[0] = 0;
        }
        return;
    }
    Value[0] = 0;
}

void FMaterialParamValue::DupString(const char* Src)
{
    // FUN_065fe620, 065fe620..e67d.
    if (Value[0] != 0)
    {
        HeapFree(GetProcessHeap(), 0, DwordToPointer(Value[0]));
        Value[0] = 0;
    }
    if (Src == nullptr)
        return;
    const size_t Length = std::strlen(Src) + 1;
    char* const Copy = (char*)(HeapAlloc(GetProcessHeap(), 0, Length));
    Value[0] = PointerToDword(Copy);
    std::memcpy(Copy, Src, Length);
}

void FMaterialParamValue::SwapCom(IDAComponent* NewCom)
{
    // FUN_065fe930, 065fe930..e950.
    IDAComponent* const Old = (IDAComponent*)(DwordToPointer(Value[0]));
    Value[0] = PointerToDword(NewCom);
    if (NewCom != nullptr)
        NewCom->AddRef();
    if (Old != nullptr)
        Old->Release();
}

FMaterialParamValue& FMaterialParamValue::operator=(const FMaterialParamValue& Src)
{
    // FUN_065fef00, 065fef00..f026 (jump table 065ff02c): the old value is cleared by the OLD
    // type when the types differ, then copied by the new one.
    if (Src.Type != Type)
        Clear();
    Type = Src.Type;
    switch (Src.Type)
    {
    case 1:
    case 2:
    case 8:
    case 9:
    case 10:
        Value[0] = Src.Value[0];
        break;
    case 4:
        Value[3] = Src.Value[3];
        // fall through (065fef72 -> ef78)
    case 3:
        Value[2] = Src.Value[2];
        Value[1] = Src.Value[1];
        Value[0] = Src.Value[0];
        break;
    case 5:
    case 7:
        DupString((const char*)(DwordToPointer(Src.Value[0])));
        break;
    case 6:
        SwapCom((IDAComponent*)(DwordToPointer(Src.Value[0])));
        break;
    default:
        break;
    }
    return *this;
}

void FMaterialParamValue::Set(int InType, unsigned long Arg)
{
    // FUN_065ffab0, 065ffab0..fbed (jump table 065ffbf0).
    if (InType != Type)
        Clear();
    Type = InType;
    const unsigned long* const Source = (const unsigned long*)(DwordToPointer(Arg));
    switch (InType)
    {
    case 1:
    case 8:
    case 9:
    case 10:
        Value[0] = Arg;
        break;
    case 2:
        Value[0] = Source[0];
        break;
    case 4:
        Value[3] = Source[3];
        // fall through (065ffb36 -> fb3c)
    case 3:
        Value[2] = Source[2];
        Value[1] = Source[1];
        Value[0] = Source[0];
        break;
    case 5:
    case 7:
        DupString((const char*)(DwordToPointer(Arg)));
        break;
    case 6:
        SwapCom((IDAComponent*)(DwordToPointer(Arg)));
        break;
    default:
        break;
    }
}

// =============================================================================================
// FMaterialParamDesc / FMaterialTextureRow
// =============================================================================================

FMaterialParamDesc::FMaterialParamDesc()
    : Flags(0)
    , Key(nullptr)
    , Name(nullptr)
    , Description(nullptr)
{
}

FMaterialParamDesc::~FMaterialParamDesc()
{
    Flags = 0;
    Key = nullptr;
    Name = nullptr;
    Description = nullptr;
}

void FMaterialParamDesc::Init(int Type, unsigned long InFlags, const char* InKey,
                              const char* InName, const char* InDescription,
                              unsigned long Default, unsigned long InMin, unsigned long InMax)
{
    Flags = InFlags;
    Key = InKey;
    Name = InName;
    Description = InDescription;
    Value.Set(Type, Default);
    Min.Set(Type, InMin);
    Max.Set(Type, InMax);
}

FMaterialTextureRow::FMaterialTextureRow()
    : Name(nullptr)
    , Handle(-1)
    , Frame(0)
    , WrapMode(4)
    , AddressU(0)
    , AddressV(0)
    , bSingleFrame(0)
{
}

FMaterialTextureRow::~FMaterialTextureRow()
{
    delete[] Name;
    Name = nullptr;
}

// =============================================================================================
// FEyeMaterialImpl (vtable 06601598)
// =============================================================================================

FEyeMaterialImpl::FEyeMaterialImpl()
    : Pipeline(nullptr)
    , TextureLib(nullptr)
    , VertexBufferMgr(nullptr)
    , Draw(nullptr)
    , Name(nullptr)
    , TextureRows(nullptr)
    , TextureRowCount(0)
    , bTranslucent(0)
    , bOneBitAlpha(0)
{
    // LAB_065fe070 065fe0f2..e119: the 0x11 dwords of +0x20 zeroed, then Diffuse and Ambient
    // (+0x20..+0x3C) set to 1.0f.
    std::memset(&Colors, 0, sizeof(Colors));
    int Index = 0;
    while (Index < 4)
    {
        Colors.Diffuse[Index] = 1.0f;
        Colors.Ambient[Index] = 1.0f;
        Index += 1;
    }
}

FEyeMaterialImpl::~FEyeMaterialImpl()
{
    // FUN_06600a30, 06600a30..0ae3 (vtable 06601598 stored first).
    unsigned long Index = 0;
    while (Index < TextureRowCount)
    {
        FMaterialTextureRow* const Row = &TextureRows[Index];
        if (Row->Handle != -1)
        {
            TextureLib->DecRefById(Row->Handle); // +0x40
            Row->Handle = -1;
        }
        Index += 1;
    }
    if (TextureRows != nullptr)
        delete[] TextureRows; // rows destroyed last to first, then the counted block
    TextureRows = nullptr;
    TextureLib = nullptr;
    Pipeline = nullptr;
    VertexBufferMgr = nullptr;
    delete[] Name;
    Name = nullptr;
}

void* __thiscall FEyeMaterialImpl::ScalarDeletingDestructor(unsigned char bDelete)
{
    // FUN_06600a30: the destructor body, then operator delete(this) when bit 0 is set; EAX =
    // this.
    if ((bDelete & 1) != 0)
        delete this;
    else
        this->~FEyeMaterialImpl();
    return this;
}

GENRESULT DACOM_API FEyeMaterialImpl::Initialize(IDAComponent* Outer)
{
    // FUN_065fe1b0 (RET 8), 065fe1b0..e311.
    if (Outer == nullptr)
        return GR_GENERIC;
    if (Outer->QueryInterface(Iids::TextureLibraryA, (void**)(&TextureLib)) < 0)
        return GR_GENERIC;
    TextureLib->Release();
    if (Outer->QueryInterface(Iids::RenderPipeline8B, (void**)(&Pipeline)) < 0)
        return GR_GENERIC;
    Pipeline->Release();
    if (Outer->QueryInterface(Iids::RPDraw, (void**)(&Draw)) < 0)
        return GR_GENERIC;
    Draw->Release();
    if (Outer->QueryInterface(Iids::VertexBufferManager,
                              (void**)(&VertexBufferMgr)) < 0)
        return GR_GENERIC;
    VertexBufferMgr->Release();

    // 065fe260..e30b: one texture row per type-7 parameter, then every parameter's default
    // stored through slot 26.
    FMaterialParamDesc* Table = nullptr;
    const unsigned long Count = GetParameterTable((void**)(&Table));
    TextureRowCount = 0;
    unsigned long Index = 0;
    while (Index < Count)
    {
        if (Table[Index].Value.Type == 7)
            TextureRowCount += 1;
        Index += 1;
    }
    if (TextureRowCount != 0)
        TextureRows = new (std::nothrow) FMaterialTextureRow[TextureRowCount];
    Index = 0;
    while (Index < Count)
    {
        StoreParameterValue(Index, &Table[Index], &Table[Index].Value);
        Index += 1;
    }
    return GR_OK;
}

GENRESULT DACOM_API FEyeMaterialImpl::LoadFromFileSystem(void* FileSystem)
{
    IFileSystem* const Fs = (IFileSystem*)(FileSystem);
    FMaterialParamDesc* Table = nullptr;
    const unsigned long Count = GetParameterTable((void**)(&Table));
    unsigned long Index = 0;
    while (Index < Count)
    {
        FMaterialParamDesc* const Entry = &Table[Index];
        if ((Entry->Flags & 1) == 0)
        {
            FMaterialParamValue Cell(Entry->Value.Type);
            GENRESULT Result = GR_OK;
            switch (Cell.Type)
            {
            case 1:
                // 065fe3a2..e41b: FUN_066003a0's body inline, FUN_065f3e80 building the desc.
                Result = MaterialFsReadDword(Fs, Entry->Key, &Cell.Value[0]);
                break;
            case 2:
                Result = MaterialFsReadFloat(Fs, Entry->Key,
                                             (float*)(&Cell.Value[0]));
                break;
            case 3:
            case 4:
                Result = MaterialFsReadFloats(Fs, Entry->Key, (unsigned long)(Cell.Type),
                                              (float*)(&Cell.Value[0]), nullptr);
                break;
            case 5:
            case 7:
            {
                char* Text;
                Result = MaterialFsReadBlock(Fs, Entry->Key, 0, &Text);
                if (Result >= 0)
                {
                    Cell.DupString(Text);
                    delete[] Text;
                }
                break;
            }
            case 8:
            {
                unsigned long Raw;
                Result = MaterialFsReadDword(Fs, Entry->Key, &Raw);
                if (Result >= 0)
                    Cell.Value[0] = (Raw >> 4) & 0xF;
                break;
            }
            case 9:
            {
                unsigned long Raw;
                Result = MaterialFsReadDword(Fs, Entry->Key, &Raw);
                if (Result >= 0)
                    Cell.Value[0] = MaterialAddressModeBits(Raw,
                                                            MaterialAddressModeWhich(Entry->Flags));
                break;
            }
            default:
                break;
            }
            if (Result >= 0)
                StoreParameterValue(Index, Entry, &Cell);
        }
        Index += 1;
    }
    return GR_OK;
}

GENRESULT DACOM_API FEyeMaterialImpl::SaveProperties(void* FileSystem)
{
    IFileSystem* const Fs = (IFileSystem*)(FileSystem);
    FMaterialParamDesc* Table = nullptr;
    const unsigned long Count = GetParameterTable((void**)(&Table));
    GENRESULT Result = GR_OK;
    unsigned long Index = 0;
    while (Index < Count)
    {
        const FMaterialParamDesc* const Entry = &Table[Index];
        if ((Entry->Flags & 1) == 0)
        {
            FMaterialParamValue Cell(Entry->Value.Type);
            const unsigned long* const Address =
                (const unsigned long*)(GetParameterAddress(Index));
            if (Address == nullptr)
            {
                Result = GR_GENERIC;
            }
            else
            {
                switch (Cell.Type)
                {
                case 1:
                case 2:
                case 8:
                case 9:
                case 10:
                    Cell.Value[0] = Address[0];
                    break;
                case 4:
                    Cell.Value[3] = Address[3];
                    // fall through (065fe716 -> e71d)
                case 3:
                    Cell.Value[2] = Address[2];
                    Cell.Value[1] = Address[1];
                    Cell.Value[0] = Address[0];
                    break;
                case 5:
                case 7:
                    Cell.DupString((const char*)(DwordToPointer(Address[0])));
                    break;
                case 6:
                    Cell.SwapCom((IDAComponent*)(DwordToPointer(Address[0])));
                    break;
                default:
                    break;
                }

                GENRESULT WriteResult = GR_OK;
                switch (Cell.Type)
                {
                case 1:
                case 2:
                    WriteResult = MaterialFsWriteDword(Fs, Entry->Key, &Cell.Value[0]);
                    break;
                case 3:
                case 4:
                    WriteResult = MaterialFsWriteFloats(Fs, Entry->Key,
                                                        (unsigned long)(Cell.Type),
                                                        &Cell.Value[0]);
                    break;
                case 5:
                case 7:
                {
                    const char* const Text = (const char*)(DwordToPointer(Cell.Value[0]));
                    WriteResult = MaterialFsWriteBlock(
                        Fs, Entry->Key, (unsigned long)(std::strlen(Text) + 1), Text);
                    break;
                }
                case 8:
                {
                    // 065fe817..e855: the stored dword is read back (result ignored) and only its
                    // bits 4..7 replaced.
                    unsigned long Raw = 0;
                    MaterialFsReadDword(Fs, Entry->Key, &Raw);
                    Raw ^= ((Cell.Value[0] << 4) ^ Raw) & 0xF0u;
                    WriteResult = MaterialFsWriteDword(Fs, Entry->Key, &Raw);
                    break;
                }
                case 9:
                {
                    // 065fe85a..e8b8: pair 0 (U) writes bits 0..1 AND then falls into the pair-1
                    // code writing bits 2..3 as well (JMP 065fe896); pair 1 writes bits 2..3.
                    unsigned long Raw = 0;
                    MaterialFsReadDword(Fs, Entry->Key, &Raw);
                    const int Which = MaterialAddressModeWhich(Entry->Flags);
                    const unsigned long Mode = Cell.Value[0];
                    if (Which == 0)
                        Raw ^= (Raw ^ Mode) & 3u;
                    if (Which == 0 || Which == 1)
                        Raw ^= ((Mode << 2) ^ Raw) & 0xCu;
                    WriteResult = MaterialFsWriteDword(Fs, Entry->Key, &Raw);
                    break;
                }
                default:
                    break;
                }
                if (WriteResult < 0)
                    Result = GR_GENERIC;
            }
        }
        Index += 1;
    }
    return Result;
}

GENRESULT DACOM_API FEyeMaterialImpl::ApplyVerify(void* /*Context*/, void* /*Extra*/)
{
    // FUN_065fe960 (RET 0xC): slot 24 Setup, slot 8 Bind(0), Pipeline +0x110 VerifyState, slot 9
    // Teardown(0); the VerifyState result.
    Setup();
    Bind(nullptr);
    const GENRESULT Result = Pipeline->VerifyState();
    Teardown(nullptr);
    return Result;
}

GENRESULT DACOM_API FEyeMaterialImpl::ResolveTextures(float /*Delta*/)
{
    // LAB_065fe9a0 (RET 8): per row, Handle -1 -> Frame = 0; else a row that is not single-frame
    // re-reads its frame through TextureLib +0x70 GetFrame(Handle, -1, &Frame). Always 0.
    unsigned long Index = 0;
    while (Index < TextureRowCount)
    {
        FMaterialTextureRow* const Row = &TextureRows[Index];
        if (Row->Handle == -1)
            Row->Frame = 0;
        else if (Row->bSingleFrame == 0)
            TextureLib->GetFrame(Row->Handle, -1, &Row->Frame);
        Index += 1;
    }
    return GR_OK;
}

// Context +0x0C is the source-vertex descriptor; a non-null diffuse stream turns
// D3DRS_COLORVERTEX (0x8D) on around the draw (06600b0b / 065fff3e / 06600290 / 06600b44).
static bool ContextHasDiffuseStream(const FMaterialSubmitContext* Context)
{
    if (Context == nullptr)
        return false;
    const FRpVertexSrcDesc* const Source = Context->SourceVertices;
    return Source != nullptr && Source->Diffuse.Base != nullptr;
}

GENRESULT DACOM_API FEyeMaterialImpl::Bind(void* Context)
{
    // FUN_06600af0 (RET 8): Pipeline +0xAC SetMaterial(&+0x20); diffuse stream ->
    // SetRenderState(0x8D, 1). 0.
    Pipeline->SetMaterial((const D3DMATERIAL8*)&Colors);
    if (ContextHasDiffuseStream((FMaterialSubmitContext*)(Context)))
        Pipeline->SetRenderState(D3DRS_COLORVERTEX, 1);
    return GR_OK;
}

GENRESULT DACOM_API FEyeMaterialImpl::Teardown(void* Context)
{
    // LAB_06600b40 (RET 8): diffuse stream -> SetRenderState(0x8D, 0). 0.
    if (ContextHasDiffuseStream((FMaterialSubmitContext*)(Context)))
        Pipeline->SetRenderState(D3DRS_COLORVERTEX, 0);
    return GR_OK;
}


GENRESULT DACOM_API FEyeMaterialImpl::Submit(void* Context, unsigned long PrimType,
                                          unsigned long Start, unsigned long Count,
                                          unsigned long IbStart, unsigned long IndexSpan,
                                          unsigned long Flags)
{
    // LAB_065fea00 (RET 0x20), 065fea00..ebf7.
    FMaterialSubmitContext* const Ctx = (FMaterialSubmitContext*)(Context);
    GENRESULT Result;
    if ((Flags & 0x100) != 0)
    {
        // 065fea1c: IRPDraw +0xC with the IB scratch bias.
        Result = Draw->DrawIndexedPrimitive(PrimType, Start, Count, Ctx->IbScratch->Bias + IbStart,
                                            IndexSpan);
        if (Result >= 0)
            return Result;
    }
    Result = GR_GENERIC;
    if (Ctx->VbListing != nullptr)
    {
        Result = Pipeline->DrawIndexedPrimitiveVb(PrimType, Ctx->VbListing->VertexBuffer,
                                                  Ctx->VbListing->BaseVertex + Start, Count,
                                                  Ctx->Indices + IbStart, IndexSpan, 0);
    }
    if (Result >= 0)
        return Result;

    // 065fea93..ebea: copy the vertices through the VB manager and draw from its buffer.
    const FRpVertexSrcDesc* const Source = Ctx->SourceVertices;
    if (Source == nullptr || Count == 0 || IndexSpan == 0)
        return Result;
    FVertexMemoryLock Lock;
    Result = VertexBufferMgr->LockVertexMemory(Source->SrcFvf, Count, &Lock);
    if (Result < 0)
        return Result;

    // 065feacb..eb78: the vertex stride from the locked FVF.
    const unsigned long Fvf = Lock.Listing.Fvf;
    unsigned long PositionBytes = 0;
    if ((Fvf & 0xC) == 0xC)
        PositionBytes = 0x1C;
    else if ((Fvf & 4) == 4)
        PositionBytes = 0x10;
    else if ((Fvf & 2) == 2)
        PositionBytes = 0xC;
    const unsigned long NormalBytes = (Fvf & 0x10) != 0 ? 0xCu : 0u;
    unsigned long ColorBytes = (Fvf & 0x40) != 0 ? 4u : 0u;
    if ((Fvf & 0x80) != 0)
        ColorBytes += 4;
    unsigned long TexBytes = 0;
    unsigned long TexCount = (Fvf >> 8) & 0xF;
    unsigned long Shift = 0x10;
    while (TexCount > 0)
    {
        // SHR EAX,CL: x86 masks the count to five bits.
        switch ((Fvf >> (Shift & 31u)) & 3u)
        {
        case 0:
            TexBytes += 8;
            break;
        case 1:
            TexBytes += 0xC;
            break;
        case 2:
            TexBytes += 0x10;
            break;
        case 3:
            TexBytes += 4;
            break;
        }
        Shift += 2;
        TexCount -= 1;
    }
    const unsigned long Stride = TexBytes + ColorBytes + NormalBytes + PositionBytes;

    VertexBufferMgr->CopyVertexData((unsigned char*)Lock.Vertices - Stride * Start, Fvf, Source, Start, Count); // +0x20
    VertexBufferMgr->UnlockVertexMemory(&Lock);                                  // +0x18
    return Pipeline->DrawIndexedPrimitiveVb(PrimType, Lock.Listing.VertexBuffer, Lock.Listing.BaseVertex, Count,
                                            Ctx->Indices + IbStart, IndexSpan, 0);
}

GENRESULT DACOM_API FEyeMaterialImpl::SetName(const char* InName)
{
    // LAB_065fec10 (RET 8): the old name deleted; null -> 0. Otherwise a copy of at most 0x40
    // bytes (strlen + 1 capped), NUL-terminated and lowercased. 0.
    delete[] Name;
    Name = nullptr;
    if (InName == nullptr)
        return GR_OK;
    size_t Size = std::strlen(InName) + 1;
    if (Size > 0x40)
        Size = 0x40;
    Name = new char[Size];
    std::strncpy(Name, InName, Size);
    Name[Size - 1] = '\0';
    _strlwr(Name);
    return GR_OK;
}

GENRESULT DACOM_API FEyeMaterialImpl::GetName(char* Out, unsigned long Size)
{
    // LAB_065fec80 (RET 0xC): -1 before SetName; else strncpy and a forced NUL.
    if (Name == nullptr)
        return GR_GENERIC;
    std::strncpy(Out, Name, Size);
    Out[Size - 1] = '\0';
    return GR_OK;
}

GENRESULT DACOM_API FEyeMaterialImpl::CopyClassName(char* Out, unsigned long Size)
{
    // FUN_065fecc0 (RET 0xC): slot 20 tested, then called again for the strncpy.
    if (ClassName() == nullptr)
        return GR_GENERIC;
    std::strncpy(Out, ClassName(), Size);
    Out[Size - 1] = '\0';
    return GR_OK;
}

GENRESULT DACOM_API FEyeMaterialImpl::GetPassCount(int* Out)
{
    // LAB_065fed00 (RET 8).
    *Out = 1;
    return GR_OK;
}

GENRESULT DACOM_API FEyeMaterialImpl::GetParameterCount(unsigned long* OutCount)
{
    // LAB_065fed10 (RET 8): slot 27 with a null out-pointer.
    *OutCount = GetParameterTable(nullptr);
    return GR_OK;
}

GENRESULT DACOM_API FEyeMaterialImpl::GetParameterDescriptor(unsigned long Index, void* OutDesc)
{
    FMaterialParamDesc* Table = nullptr;
    if (GetParameterTable((void**)(&Table)) <= Index)
        return GR_GENERIC;
    const FMaterialParamDesc* const Entry = &Table[Index];
    FMaterialParamDesc* const Out = (FMaterialParamDesc*)(OutDesc);
    Out->Flags = Entry->Flags;
    Out->Key = Entry->Key;
    Out->Name = Entry->Name;
    Out->Description = Entry->Description;
    Out->Value = Entry->Value;
    Out->Min = Entry->Min;
    Out->Max = Entry->Max;
    return GR_OK;
}

GENRESULT DACOM_API FEyeMaterialImpl::GetParameterValue(unsigned long Index, void* InOutDesc)
{
    // FUN_065ff060 (RET 0xC), jump table 065ff1c8. InOutDesc is one value cell: retyped to the
    // parameter's type (old value cleared by its old type), then filled from slot 28's address.
    FMaterialParamDesc* Table = nullptr;
    if (GetParameterTable((void**)(&Table)) <= Index)
        return GR_GENERIC;
    FMaterialParamValue* const Out = (FMaterialParamValue*)(InOutDesc);
    const int Type = Table[Index].Value.Type;
    if (Out->Type != Type)
        Out->Clear();
    Out->Type = Type;
    const unsigned long* const Address = (const unsigned long*)(GetParameterAddress(Index));
    if (Address == nullptr)
        return GR_GENERIC;
    switch (Out->Type)
    {
    case 1:
    case 2:
    case 8:
    case 9:
    case 10:
        Out->Value[0] = Address[0];
        break;
    case 4:
        Out->Value[3] = Address[3];
        // fall through (065ff110 -> f116)
    case 3:
        Out->Value[2] = Address[2];
        Out->Value[1] = Address[1];
        Out->Value[0] = Address[0];
        break;
    case 5:
    case 7:
        Out->DupString((const char*)(DwordToPointer(Address[0])));
        break;
    case 6:
        Out->SwapCom((IDAComponent*)(DwordToPointer(Address[0])));
        break;
    default:
        break;
    }
    return GR_OK;
}

GENRESULT DACOM_API FEyeMaterialImpl::SetParameterValue(unsigned long Index, void* Value)
{
    // LAB_065ff1f0 (RET 0xC): Index >= count or slot 25 false -> -1; else slot 26, 0.
    FMaterialParamDesc* Table = nullptr;
    if (GetParameterTable((void**)(&Table)) <= Index)
        return GR_GENERIC;
    if (!ValueWithinDeclaredRange(Index, &Table[Index], Value))
        return GR_GENERIC;
    StoreParameterValue(Index, &Table[Index], Value);
    return GR_OK;
}

GENRESULT __thiscall FEyeMaterialImpl::InitializeFromDescriptor(void* /*Arg*/)
{
    // FUN_06600ba0: XOR EAX,EAX; RET 4.
    return GR_OK;
}

GENRESULT __thiscall FEyeMaterialImpl::ApplyInitialRenderStates(void* /*Arg*/)
{
    // FUN_06600ba0 again.
    return GR_OK;
}

GENRESULT FEyeMaterialImpl::UpdateTranslucency()
{
    bTranslucent = Colors.Diffuse[3] < 1.0f ? 1 : 0;
    bOneBitAlpha = 0;
    if (bTranslucent != 0)
        return 1;
    // FUN_06600da0(0) -- the value that survives as the result when the frame is 0.
    GENRESULT Result = PixelFormatFromD3D(0);
    if (TextureRows[0].Frame == 0)
        return Result;
    int Format;
    Result = Pipeline->GetTextureFormat(DwordToPointer(TextureRows[0].Frame), &Format); // +0xC8
    if (Result < 0)
        return Result;
    bOneBitAlpha = PixelFormatGetDesc(Format)->Alpha.Bits == 1 ? 1 : 0;
    if ((Format < 10 || Format > 14) &&
        (unsigned char)(PixelFormatGetDesc(Format)->HasAlpha) == 0)
    {
        bTranslucent = 0;
        return 0;
    }
    if (bOneBitAlpha != 0)
    {
        bTranslucent = 0;
        return 0;
    }
    bTranslucent = 1;
    return 1;
}

GENRESULT __thiscall FEyeMaterialImpl::Setup()
{
    char Message[0x2000];
    unsigned long Index = 0;
    while (Index < TextureRowCount)
    {
        FMaterialTextureRow* const Row = &TextureRows[Index];
        ITextureLibrary2* const Library = TextureLib;
        const int OldHandle = Row->Handle;
        Row->Frame = 0;
        bool bVerified = true;
        Row->Handle = -1;
        Row->bSingleFrame = 1;
        if (Row->Name != nullptr)
        {
            FEngineName Query;
            Query.Text = Row->Name;
            Query.Crc = DACOM_CRC::GetCRC32(Row->Name);
            void* Texture;
            if (Library->FindTextureByCrc(&Query, &Texture) < 0)
            {
                // 065ff32d: REPE CMPSB over the 7 bytes of "(None)".
                bVerified = std::strcmp(Row->Name, G_TextureNameNone) == 0;
            }
            else
            {
                int FrameCount;
                Library->GetFrameCount(Texture, &FrameCount);
                Row->bSingleFrame = FrameCount == 1 ? 1 : 0;
                if (Library->CreatePlaybackGroup(Texture, &Row->Handle) >= 0 &&
                    Row->bSingleFrame != 0)
                    Library->GetFrame(Row->Handle, 0, &Row->Frame);
                Library->ReleaseTextureRecord(Texture);
            }
        }
        if (OldHandle != -1)
            Library->DecRefById(OldHandle);
        if (!bVerified)
        {
            FDUMP(0x100003, "%s(%d) : NOTICE:General:%s", G_FdumpFileMaterialImpl, 0x23F,
                  FormatDeformMessage(Message, "texture verify failed: %s: %s", Name, Row->Name));
        }
        Index += 1;
    }
    return UpdateTranslucency();
}

bool __thiscall FEyeMaterialImpl::ValueWithinDeclaredRange(unsigned long /*Index*/, void* Entry,
                                                        void* Value)
{
    const FMaterialParamDesc* const Desc = (const FMaterialParamDesc*)(Entry);
    const FMaterialParamValue* const Cell = (const FMaterialParamValue*)(Value);
    if (Cell->Type != Desc->Value.Type)
        return false;
    int Components = 0;
    if (Desc->Value.Type == 2)
        Components = 1;
    else if (Desc->Value.Type == 3)
        Components = 3;
    else if (Desc->Value.Type == 4)
        Components = 4;
    else
        return true;
    const float* const Min = (const float*)(&Desc->Min.Value[0]);
    const float* const Max = (const float*)(&Desc->Max.Value[0]);
    const float* const Candidate = (const float*)(&Cell->Value[0]);
    int Component = 0;
    while (Component < Components)
    {
        const double Test = (double)(Candidate[Component]);
        if ((double)(Max[Component]) + (double)(G_ParamRangeSlack) <= Test)
            return false;
        if ((double)(Min[Component]) - (double)(G_ParamRangeSlack) >= Test)
            return false;
        Component += 1;
    }
    return true;
}

GENRESULT __thiscall FEyeMaterialImpl::StoreParameterValue(unsigned long Index, void* Entry,
                                                        void* Value)
{
    const FMaterialParamValue* const Cell = (const FMaterialParamValue*)(Value);
    unsigned long* const Address = (unsigned long*)(GetParameterAddress(Index));
    if (Address != nullptr)
    {
        switch (Cell->Type)
        {
        case 1:
        case 2:
        case 8:
        case 9:
        case 10:
            Address[0] = Cell->Value[0];
            break;
        case 4:
            Address[3] = Cell->Value[3];
            // fall through (065ff58a -> f590)
        case 3:
            Address[2] = Cell->Value[2];
            Address[1] = Cell->Value[1];
            Address[0] = Cell->Value[0];
            break;
        case 5:
        case 7:
        {
            char** const Text = (char**)(Address);
            if (*Text != nullptr)
            {
                delete[] *Text;
                *Text = nullptr;
            }
            const char* const Source = (const char*)(DwordToPointer(Cell->Value[0]));
            if (Source != nullptr)
            {
                const size_t Length = std::strlen(Source) + 1;
                *Text = new char[Length];
                std::memcpy(*Text, Source, Length);
            }
            break;
        }
        case 6:
        {
            IDAComponent** const Slot = (IDAComponent**)(Address);
            IDAComponent* const Old = *Slot;
            IDAComponent* const NewCom = (IDAComponent*)(DwordToPointer(Cell->Value[0]));
            *Slot = NewCom;
            if (NewCom != nullptr)
                NewCom->AddRef();
            if (Old != nullptr)
                Old->Release();
            break;
        }
        default:
            break;
        }
    }

    const char* const Key = ((const FMaterialParamDesc*)(Entry))->Key;
    if (Key[0] == 'D' && Key[1] == 'c' && Key[2] == '\0')
    {
        int Component = 0;
        while (Component < 4)
        {
            Colors.Ambient[Component] = Colors.Diffuse[Component];
            Component += 1;
        }
        return GR_OK;
    }
    if (Key[0] == 'O' && Key[1] == 'c' && Key[2] == '\0')
        return UpdateTranslucency();
    return GR_OK;
}

unsigned long __thiscall FEyeMaterialImpl::GetParameterTable(void** OutBase)
{
    // LAB_06600b80 (RET 4): no table.
    if (OutBase != nullptr)
        *OutBase = nullptr;
    return 0;
}

void* __thiscall FEyeMaterialImpl::GetParameterAddress(unsigned long /*Index*/)
{
    // FUN_06600ba0.
    return nullptr;
}

// =============================================================================================
// FEyeMaterialInner (vtable 0660156c)
// =============================================================================================

namespace
{
    // .rdata:06601588: {IID, offset} pairs, a null IID ends the list. An offset with bit 31
    // set would name a pointer to load instead of a sub-object; this table has none.
    struct FInterfaceOffset
    {
        const char* Iid;
        unsigned long Offset;
    };

    const FInterfaceOffset G_EyeMaterialInterfaces[] = {
        {Iids::Material, 0},
        {nullptr, 0}};
}

GENRESULT DACOM_API FEyeMaterialInner::QueryInterface(const char* Iid, void** Out)
{
    int Index = 0;
    while (G_EyeMaterialInterfaces[Index].Iid != nullptr)
    {
        if (std::strcmp(G_EyeMaterialInterfaces[Index].Iid, Iid) == 0)
        {
            const unsigned long Offset = G_EyeMaterialInterfaces[Index].Offset;
            unsigned char* const Base = (unsigned char*)(Owner);
            IDAComponent* Face;
            if ((Offset & 0x80000000u) != 0)
                Face = *(IDAComponent**)(Base + (Offset & 0x7FFFFFFFu));
            else
                Face = (IDAComponent*)(Base + Offset);
            Face->AddRef();
            *Out = Face;
            return GR_OK;
        }
        Index += 1;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

unsigned long DACOM_API FEyeMaterialInner::AddRef()
{
    // LAB_065fe060 (the factory's AddRef too): ++[this+4].
    RefCount += 1;
    return RefCount;
}

unsigned long DACOM_API FEyeMaterialInner::Release()
{
    // LAB_066009d0 (RET 4): decrement when above 0; reaching 0 re-arms it to 1 and calls the
    // owner's slot 21 with 1, then returns 0.
    if (RefCount > 0)
        RefCount -= 1;
    if (RefCount != 0)
        return RefCount;
    RefCount = 1;
    if (Owner != nullptr)
        Owner->ScalarDeletingDestructor(1);
    return 0;
}

// =============================================================================================
// FEyeMaterial (vtable 066014f8)
// =============================================================================================

// Bind's fixed stage and render states: {state, value} pairs ended by state 0.
namespace
{
    struct FStateValue
    {
        int State;
        int Value;
    };

    // .rdata:0660146c -- stage 0 texture-stage states.
    const FStateValue G_EyeStage0States[] = {{D3DTSS_COLOROP, 4}, {D3DTSS_COLORARG1, 2}, {D3DTSS_COLORARG2, 0}, {D3DTSS_ALPHAOP, 4}, {D3DTSS_ALPHAARG1, 2}, {D3DTSS_ALPHAARG2, 0}, {0, 0}};
    // .rdata:066014a4 -- stage 1.
    const FStateValue G_EyeStage1States[] = {{D3DTSS_COLOROP, 1}, {D3DTSS_COLORARG1, 2}, {D3DTSS_COLORARG2, 1}, {D3DTSS_ALPHAOP, 1}, {D3DTSS_ALPHAARG1, 2}, {D3DTSS_ALPHAARG2, 1}, {0, 0}};
    // .rdata:0660143c -- render states when translucent.
    const FStateValue G_EyeBlendStates[] = {{D3DRS_LIGHTING, 1}, {D3DRS_SPECULARENABLE, 0}, {D3DRS_ALPHABLENDENABLE, 1}, {D3DRS_SRCBLEND, 5}, {D3DRS_DESTBLEND, 6},
                                            {0, 0}};
    // .rdata:0660141c -- render states when opaque.
    const FStateValue G_EyeOpaqueStates[] = {{D3DRS_LIGHTING, 1}, {D3DRS_SPECULARENABLE, 0}, {D3DRS_ALPHABLENDENABLE, 0}, {0, 0}};
}

static bool EyeUvClampPair(const FUvDeformRecord* Record, const float* In, float* OutU,
                           float* OutV)
{
    float U = (float)((double)(Record->BoneXToU) * In[0] *
                                 Record->PlaneDistance);
    double V = (double)(In[1]) * Record->BoneYToV * Record->PlaneDistance;
    if (!(std::fabs((double)(U)) > G_EyeUvDeadZone) &&
        !(std::fabs(V) > G_EyeUvDeadZone))
        return false;
    if (U < Record->MinDu)
        U = Record->MinDu;
    else if (U > Record->MaxDu)
        U = Record->MaxDu;
    if (V < Record->MinDv)
        V = Record->MinDv;
    else if (V > Record->MaxDv)
        V = Record->MaxDv;
    *OutU = U;
    *OutV = (float)(V);
    return true;
}

FEyeMaterial::FEyeMaterial()
    : Controlling(nullptr)
{
    Inner.RefCount = 1;
    Inner.Owner = this;
}

GENRESULT DACOM_API FEyeMaterial::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06600a00: [ESP+4] = [this+0x68], JMP [vtable+0].
    return Controlling->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FEyeMaterial::AddRef()
{
    // LAB_06600a10.
    return Controlling->AddRef();
}

unsigned long DACOM_API FEyeMaterial::Release()
{
    // LAB_06600a20.
    return Controlling->Release();
}

GENRESULT DACOM_API FEyeMaterial::Bind(void* Context)
{
    // LAB_065fff20 (RET 8), 065fff20..001ae.
    FMaterialSubmitContext* const Ctx = (FMaterialSubmitContext*)(Context);
    Pipeline->SetMaterial((const D3DMATERIAL8*)&Colors); // +0xAC
    if (ContextHasDiffuseStream(Ctx))
        Pipeline->SetRenderState(D3DRS_COLORVERTEX, 1);

    // 065fff63..ffb4: the first texture row on stage 0; its wrap mode less 4 (0 below 4) as
    // state 0xB, the address modes plus 1 as 0xD / 0xE.
    IRenderPipeline* const StagePipeline = Pipeline;
    const FMaterialTextureRow* const Row = TextureRows;
    StagePipeline->SetTextureStageTexture(0, DwordToPointer(Row->Frame)); // +0x108
    StagePipeline->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, (int)(Row->WrapMode >= 4 ? Row->WrapMode - 4 : 0));
    StagePipeline->SetTextureStageState(0, D3DTSS_ADDRESSU, (int)(Row->AddressU + 1));
    StagePipeline->SetTextureStageState(0, D3DTSS_ADDRESSV, (int)(Row->AddressV + 1));

    IRenderPipeline* ListPipeline = Pipeline;
    for (const FStateValue* State = G_EyeStage0States; State->State != 0; ++State)
        ListPipeline->SetTextureStageState(0, State->State, State->Value);
    ListPipeline = Pipeline;
    for (const FStateValue* State = G_EyeStage1States; State->State != 0; ++State)
        ListPipeline->SetTextureStageState(1, State->State, State->Value);
    ListPipeline = Pipeline;
    const FStateValue* const RenderStates = bTranslucent != 0 ? G_EyeBlendStates : G_EyeOpaqueStates;
    for (const FStateValue* State = RenderStates; State->State != 0; ++State)
        ListPipeline->SetRenderState((D3DRENDERSTATETYPE)State->State, State->Value);

    if (Ctx == nullptr)
        return GR_OK;
    FUvDeformBinding* const Binding = (FUvDeformBinding*)(Ctx->Field20);
    if (Binding == nullptr || Binding->Record == nullptr)
        return GR_OK;
    const FBoneRuntime* const Bone = Binding->Bone;
    const float* const BoneLocal =
        Bone->LocalTransform;
    float Axis[3];
    Axis[0] = BoneLocal[2];
    Axis[1] = BoneLocal[5];
    Axis[2] = BoneLocal[8];
    void* const Parent = G_Engine->GetInstanceParent(Bone->Instance);
    if (Parent != (void*)((intptr_t)(-1)))
    {
        const float* const Orientation = G_Engine->GetInstanceOrientation(Parent);
        float InParent[3];
        MathTransformVec3(InParent, Orientation, Axis);
        Axis[0] = InParent[0];
        Axis[1] = InParent[1];
        Axis[2] = InParent[2];
    }
    float U;
    float V;
    if (!EyeUvClampPair(Binding->Record, Axis, &U, &V))
        return GR_OK;
    const float Transform[16] = {
        1.0f, 0.0f, U,    0.0f,
        0.0f, 1.0f, V,    0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    Pipeline->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, 2);
    Pipeline->SetTextureStageTransform(0, Transform);
    Binding->EyeUvTransformCount += 1;
    return GR_OK;
}

GENRESULT DACOM_API FEyeMaterial::Teardown(void* Context)
{
    // LAB_06600270 (RET 8): no context -> 0 at once. Else D3DTSS_TEXTURETRANSFORMFLAGS off on
    // stage 0 and, with a diffuse stream, D3DRS_COLORVERTEX off.
    if (Context == nullptr)
        return GR_OK;
    Pipeline->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
    if (ContextHasDiffuseStream((FMaterialSubmitContext*)(Context)))
        Pipeline->SetRenderState(D3DRS_COLORVERTEX, 0);
    return GR_OK;
}

GENRESULT DACOM_API FEyeMaterial::GetRenderFlags(unsigned long* Out)
{
    // LAB_065ffef0 (RET 8): NEG/SBB -> 0xA when translucent, else 8.
    *Out = bTranslucent != 0 ? 0xAu : 0x8u;
    return GR_OK;
}

const char* __thiscall FEyeMaterial::ClassName()
{
    // LAB_065fff10: MOV EAX,"EyeMaterial" (.rdata:066014dc); RET.
    return G_EyeMaterialName;
}

unsigned long __thiscall FEyeMaterial::GetParameterTable(void** OutBase)
{
    static FMaterialParamDesc Table[10];
    if (Table[0].Value.Type == 0)
    {
        const unsigned long Ones = PointerToDword(G_ParamOnes);
        const unsigned long Zeros = PointerToDword(G_ParamZeros);
        Table[0].Init(3, 0, "Ac", "Ambient Color", G_ParamNoDescription, Ones, Zeros, Ones);
        Table[1].Init(3, 0, "Dc", "Diffuse Color", G_ParamNoDescription, Ones, Zeros, Ones);
        Table[2].Init(3, 0, "Ec", "Emissive Color", G_ParamNoDescription, Zeros, Zeros, Ones);
        Table[3].Init(3, 0, "Sc", "Specular Color", G_ParamNoDescription, Zeros, Zeros, Ones);
        Table[4].Init(2, 0, "Sp", "Specular Power", G_ParamNoDescription, Zeros, Zeros, Ones);
        Table[5].Init(2, 0, "Oc", "Opacity ", G_ParamNoDescription, Ones, Zeros, Ones);
        Table[6].Init(7, 0, "Dt_name", "Texture Name", G_ParamNoDescription,
                      PointerToDword(G_TextureNameNone), 0, 0);
        Table[7].Init(8, 0, "Dt_flags", "Texture Wrap Mode", G_ParamNoDescription, 4, 0, 0);
        Table[8].Init(9, 0x20000, "Dt_flags", "Texture U Address Mode", G_ParamNoDescription, 2,
                      0, 0);
        // 065ffa6f: the last one through the FUN_065ffc50 constructor.
        Table[9].Init(9, 0x40000, "Dt_flags", "Texture V Address Mode", G_ParamNoDescription, 2,
                      0, 0);
    }
    if (OutBase != nullptr)
        *OutBase = Table;
    return 10;
}

void* __thiscall FEyeMaterial::GetParameterAddress(unsigned long Index)
{
    // LAB_065ffe60 (RET 4), jump table 065ffebc. 6..9 are offsets from the row pointer itself
    // (no null test).
    switch (Index)
    {
    case 0:
        return Colors.Ambient; // Ambient
    case 1:
        return Colors.Diffuse; // Diffuse
    case 2:
        return Colors.Emissive; // Emissive
    case 3:
        return Colors.Specular; // Specular
    case 4:
        return &Colors.Power; // Power
    case 5:
        return &Colors.Diffuse[3]; // Diffuse alpha
    case 6:
        return &TextureRows[0].Name;        // row 0 Name
    case 7:
        return &TextureRows[0].WrapMode; // row 0 WrapMode
    case 8:
        return &TextureRows[0].AddressU; // row 0 AddressU
    case 9:
        return &TextureRows[0].AddressV; // row 0 AddressV
    default:
        return nullptr;
    }
}

GENRESULT DACOM_API FEyeMaterial::InnerQueryInterface(const char* Iid, void** Out)
{
    return Inner.QueryInterface(Iid, Out);
}

unsigned long DACOM_API FEyeMaterial::InnerAddRef()
{
    return Inner.AddRef();
}

unsigned long DACOM_API FEyeMaterial::InnerRelease()
{
    return Inner.Release();
}

// =============================================================================================
// Factory (vtable 066014e8)
// =============================================================================================

namespace
{
    struct FEyeMaterialFactory : FDeformableFactory
    {
        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Out) override;
    };
}

GENRESULT DACOM_API FEyeMaterialFactory::CreateInstance(AGGDESC* Desc, void** Out)
{
    // LAB_065fe070 (RET 0xC), 065fe070..e1a8. The desc is {0x14, name, outer, inner-out}.
    FAGGDESC14* const Aggregate = (FAGGDESC14*)(Desc);
    if (Aggregate->Size != 0x14 || std::strcmp(Aggregate->NameAt4, Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }
    FEyeMaterial* const Object = new (std::nothrow) FEyeMaterial();
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    IDAComponent* const Outer = Aggregate->OuterUnknown;
    Object->Controlling = Outer;
    if (Outer != nullptr)
        *Aggregate->InnerUnknown = &Object->Inner;
    else
        Object->Controlling = &Object->Inner;

    const GENRESULT Result = Object->InitializeFromDescriptor(Desc); // +0x58
    if (Result == GR_OK)
    {
        *Out = (IMaterial*)(Object);
        return GR_OK;
    }
    if (Aggregate->InnerUnknown != nullptr)
        *Aggregate->InnerUnknown = nullptr;
    Object->ScalarDeletingDestructor(1); // +0x54
    *Out = nullptr;
    return Result;
}

bool RegisterEyeMaterialFactory()
{
    // FUN_065fdfd0, 065fdfd0..e019.
    FEyeMaterialFactory* const Factory = new (std::nothrow) FEyeMaterialFactory();
    if (Factory == nullptr)
        return false;
    Factory->RefCount = 1;
    Factory->Name = G_EyeMaterialName;
    ICOManager* const Dacom = DACOM_Acquire();
    Dacom->RegisterComponent((IDAComponent*)(Factory), G_EyeMaterialName,
                             (void*)(0x80000000));
    Factory->Release();
    return true;
}
