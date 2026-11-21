// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// RPGPU's textures and buffers (docs/re/rpgpu.md): the handle tables, the texture images, the
// vertex and index buffers with RP8's lock and offset conventions, the pool and scratch rings,
// and the IRPTexture loaders.

#include "RpGpu.h"

#include "RpGpuDds.h"
#include "render/rp8_semantics.h"
#include "TexConvert.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <stdexcept>

using namespace RpGpuDetail;

namespace
{
    constexpr unsigned int kPoolGrowVertices = 0x400;

    // RP8 pixel-format numbers (pfenum): 1 P8, 2 R8G8B8, 3 R5G6B5, 4 X1R5G5B5, 5 A4R4G4B4,
    // 6 A1R5G5B5, 7 A8R8G8B8, 8 X8R8G8B8, 10..14 DXT1..5. 15..21 are format classes that RP8
    // resolves to one of those.
    unsigned int ConcreteFormat(unsigned int Format)
    {
        switch (Format)
        {
        case 15:
        case 16:
            return 3;
        case 19:
            return 6;
        case 20:
            return 5;
        case 17:
        case 18:
        case 21:
            return 7;
        default:
            return Format;
        }
    }

    bool IsKnownFormat(unsigned int Format)
    {
        return (Format >= 1 && Format <= 8) || (Format >= 10 && Format <= 14);
    }

    bool IsBlockFormat(unsigned int Format)
    {
        return Format >= 10 && Format <= 14;
    }

    // Formats 1..8 (IsKnownFormat): RP8's bit counts.
    unsigned int BytesPerPixel(unsigned int Format)
    {
        return kRp8PfBpp[Format] / 8;
    }

    unsigned int RowCount(unsigned int Format, unsigned int Height)
    {
        return IsBlockFormat(Format) ? std::max(1u, (Height + 3) / 4) : Height;
    }

    // A zeroed image of one level; false when its memory could not be allocated.
    bool MakeImage(unsigned int Format, unsigned int Width, unsigned int Height, FRpGpuImage* Out)
    {
        Out->Width = std::max(1u, Width);
        Out->Height = std::max(1u, Height);
        // Bytes per row of blocks for DXT, per row of pixels otherwise. Sized in 64 bits: on Win32
        // the pitch or the byte count can exceed 32 bits, and a count above max_size() would throw
        // length_error past CreateTexture's cleanup.
        const std::uint64_t Pitch64 =
            IsBlockFormat(Format)
                ? static_cast<std::uint64_t>(std::max(1u, (Out->Width + 3) / 4)) * (Format == 10 ? 8u : 16u)
                : static_cast<std::uint64_t>(Out->Width) * BytesPerPixel(Format);
        const std::uint64_t Bytes64 = Pitch64 * RowCount(Format, Out->Height);
        if (Pitch64 > 0xFFFFFFFFull || Bytes64 > Out->Bits.max_size())
            return false;
        Out->Pitch = static_cast<unsigned int>(Pitch64);
        try
        {
            Out->Bits.assign(static_cast<std::size_t>(Bytes64), 0);
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        return true;
    }

    // A pool capacity: Count rounded up to the pool step, false when that does not fit.
    bool RoundToPoolStep(unsigned int Count, unsigned int* Out)
    {
        if (Count > 0xFFFFFFFFu - (kPoolGrowVertices - 1))
            return false;
        *Out = (Count + kPoolGrowVertices - 1) / kPoolGrowVertices * kPoolGrowVertices;
        return true;
    }
}

// --- textures --------------------------------------------------------------------------------

FRpGpuTexture* FRpGpu::LiveTexture(void* Handle) const
{
    FRpGpuTexture* const Texture = static_cast<FRpGpuTexture*>(Handle);
    return Textures_.count(Texture) != 0 ? Texture : nullptr;
}

GENRESULT DACOM_API FRpGpu::CreateTexture(int Width, int Height, unsigned int* Format, unsigned int Levels,
                                          unsigned int Flags, void** OutHandle)
{
    if ((Flags & 1) != 0 || Format == nullptr || OutHandle == nullptr || Width <= 0 || Height <= 0)
        return GR_INVALID_PARMS;
    const unsigned int Concrete = ConcreteFormat(*Format);
    if (!IsKnownFormat(Concrete))
        return GR_GENERIC;
    // P8 only with TEXTURE_ALLOW_8BIT, as in RP8; TexConvert fills it only from a P8 source.
    if (Concrete == 1 && PipelineState_[kPsAllow8Bit] == 0)
        return GR_GENERIC;

    // 0 asks for the full chain down to 1x1; more levels than that is an invalid request, as
    // Direct3D's CreateTexture answers it.
    const unsigned int MaxLevels =
        MaxMipLevels(static_cast<unsigned int>(Width), static_cast<unsigned int>(Height));
    if (Levels > MaxLevels)
        return GR_GENERIC;
    const unsigned int Count = Levels == 0 ? MaxLevels : Levels;

    FRpGpuTexture* const Texture = new (std::nothrow) FRpGpuTexture();
    if (Texture == nullptr)
        return GR_OUT_OF_MEMORY;
    Texture->Width = static_cast<unsigned int>(Width);
    Texture->Height = static_cast<unsigned int>(Height);
    Texture->Format = Concrete;
    Texture->Levels = Count;
    try
    {
        Texture->Images.resize(Count);
    }
    catch (const std::bad_alloc&)
    {
        delete Texture;
        return GR_OUT_OF_MEMORY;
    }
    for (unsigned int Level = 0; Level < Count; ++Level)
    {
        if (!MakeImage(Concrete, Texture->Width >> Level, Texture->Height >> Level, &Texture->Images[Level]))
        {
            delete Texture;
            return GR_OUT_OF_MEMORY;
        }
    }
    Textures_.insert(Texture);
    *OutHandle = Texture;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::DestroyTexture(void* Handle)
{
    FRpGpuTexture* const Texture = LiveTexture(Handle);
    if (Texture != nullptr)
    {
        Textures_.erase(Texture);
        delete Texture;
    }
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::IsTexture(void* Handle)
{
    return LiveTexture(Handle) != nullptr ? GR_OK : GR_GENERIC;
}

// Out: width, height, pitch in bytes, bits, pixel format (pfenum).
GENRESULT DACOM_API FRpGpu::LockTexture(void* Handle, int Level, FRpTextureLock* Out)
{
    FRpGpuTexture* const Texture = LiveTexture(Handle);
    if (Texture == nullptr || Out == nullptr)
        return GR_INVALID_PARMS;
    if (Level < 0 || static_cast<std::size_t>(Level) >= Texture->Images.size())
        return GR_GENERIC;
    FRpGpuImage& Image = Texture->Images[static_cast<std::size_t>(Level)];
    Out->Width = Image.Width;
    Out->Height = Image.Height;
    Out->Pitch = Image.Pitch;
    Out->Bits = Image.Bits.data();
    Out->Format = Texture->Format;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::UnlockTexture(void* Handle, int Level)
{
    (void)Level;
    return LiveTexture(Handle) != nullptr ? GR_OK : GR_INVALID_PARMS;
}

GENRESULT DACOM_API FRpGpu::GetTextureFormat(void* Handle, int* OutFormat)
{
    FRpGpuTexture* const Texture = LiveTexture(Handle);
    if (Texture == nullptr)
        return GR_INVALID_PARMS;
    if (OutFormat != nullptr)
        *OutFormat = static_cast<int>(Texture->Format);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetTextureDim(void* Handle, unsigned int* OutW, unsigned int* OutH, int* OutLevels)
{
    FRpGpuTexture* const Texture = LiveTexture(Handle);
    if (Texture == nullptr)
        return GR_INVALID_PARMS;
    if (OutW != nullptr)
        *OutW = Texture->Width;
    if (OutH != nullptr)
        *OutH = Texture->Height;
    if (OutLevels != nullptr)
        *OutLevels = Texture->bCube ? 6 : static_cast<int>(Texture->Levels);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetTextureInterface(void* Handle, void* UnusedA, void* UnusedB)
{
    (void)Handle;
    (void)UnusedA;
    (void)UnusedB;
    return kUnsupported;
}

// The loaders (Shading's DDS and TGA readers, ximage) fill a level from their own pixels: RP8
// hands them to D3DXLoadSurfaceFromMemory, RPGPU to TexConvert, which converts the format and
// resizes when the sizes differ (docs/re/texconvert.md). B is the P8 palette, 256 RGB triplets.
GENRESULT DACOM_API FRpGpu::SetTextureLevelData(void* Handle, int Level, int SrcW, int SrcH, unsigned int Pitch,
                                                unsigned int* SrcFormat, const void* Pixels, void* A, void* B)
{
    (void)A;
    FRpGpuTexture* const Texture = LiveTexture(Handle);
    if (Texture == nullptr || SrcFormat == nullptr || *SrcFormat == 0)
        return GR_INVALID_PARMS;
    if (Level > 0 && PipelineState_[kPsTextureLod] == 0)
        return GR_OK;
    if (Level < 0 || static_cast<std::size_t>(Level) >= Texture->Images.size() || SrcW <= 0 || SrcH <= 0 ||
        Pixels == nullptr)
        return GR_GENERIC;
    FRpGpuImage& Image = Texture->Images[static_cast<std::size_t>(Level)];
    TexConvert::FImage Destination;
    Destination.Format = Texture->Format;
    Destination.Width = Image.Width;
    Destination.Height = Image.Height;
    Destination.Pitch = Image.Pitch;
    Destination.Bits = Image.Bits.data();
    TexConvert::FSource Source;
    Source.Format = ConcreteFormat(*SrcFormat);
    Source.Width = static_cast<unsigned int>(SrcW);
    Source.Height = static_cast<unsigned int>(SrcH);
    Source.Pitch = Pitch;
    Source.Bits = Pixels;
    Source.PaletteRgb = static_cast<const unsigned char*>(B);
    // The conversion allocates a working buffer; a failed allocation leaves the level as it was.
    try
    {
        return TexConvert::Load(Destination, Source) != TexConvert::EResult::Failed ? GR_OK : GR_GENERIC;
    }
    catch (const std::bad_alloc&)
    {
        return GR_OUT_OF_MEMORY;
    }
    catch (const std::length_error&)
    {
        return GR_OUT_OF_MEMORY; // TexConvert refuses such sizes first; this only backs that up
    }
}

GENRESULT DACOM_API FRpGpu::BlitTexture(void* DstHandle, int DstLevel, void* SrcHandle, int SrcLevel)
{
    (void)DstHandle;
    (void)DstLevel;
    (void)SrcHandle;
    (void)SrcLevel;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetRenderTarget(void* Handle, void* UnusedA, void* UnusedB)
{
    (void)Handle;
    (void)UnusedA;
    (void)UnusedB;
    return kUnsupported;
}

GENRESULT DACOM_API FRpGpu::GetRenderTarget(void* Handle)
{
    (void)Handle;
    return kUnsupported;
}

// --- buffers ---------------------------------------------------------------------------------

// A ring lock: Count elements from the cursor, wrapping to 0 when they do not fit; a pool
// grows when a single lock is larger than the whole ring.
bool FRpGpu::LockRing(FRpGpuBuffer* Buffer, unsigned int Count, unsigned int* OutStart)
{
    if (Count > Buffer->Count)
    {
        if (Buffer->Fvf == 0)
            return false; // the scratch index buffer has a fixed size
        unsigned int Grown = 0;
        if (!RoundToPoolStep(Count, &Grown) || !ResizeStorage(Buffer, Grown, Buffer->Stride))
            return false;
    }
    if (Buffer->Cursor + Count > Buffer->Count)
        Buffer->Cursor = 0;
    *OutStart = Buffer->Cursor;
    Buffer->Cursor += Count;
    return true;
}

// New backing for Count elements of Stride bytes. The buffer changes only when the allocation
// succeeded; the old contents are dropped, as a re-created D3D buffer's are.
bool FRpGpu::ResizeStorage(FRpGpuBuffer* Buffer, unsigned int Count, unsigned int Stride)
{
    if (Count == 0 || Stride == 0 || Count > static_cast<std::size_t>(-1) / Stride)
        return false; // the byte size does not fit a size_t (Win32)
    std::vector<unsigned char> Storage;
    try
    {
        Storage.assign(static_cast<std::size_t>(Count) * Stride, 0);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    Buffer->Data.swap(Storage);
    Buffer->Count = Count;
    Buffer->Stride = Stride;
    Buffer->Cursor = 0;
    return true;
}

FRpGpuBuffer* FRpGpu::PoolBuffer(unsigned int Fvf)
{
    auto Found = VbPools_.find(Fvf);
    if (Found != VbPools_.end())
        return Found->second;
    FRpGpuBuffer* const Pool = new (std::nothrow) FRpGpuBuffer();
    if (Pool == nullptr)
        return nullptr;
    Pool->Fvf = Fvf;
    Pool->bRing = true;
    if (!ResizeStorage(Pool, kPoolGrowVertices, std::max(1u, Rp8FvfStride(Fvf))))
    {
        delete Pool;
        return nullptr;
    }
    VbPools_[Fvf] = Pool;
    return Pool;
}

FRpGpuBuffer* FRpGpu::LiveVb(void* Handle) const
{
    FRpGpuBuffer* const Buffer = static_cast<FRpGpuBuffer*>(Handle);
    if (VertexBuffers_.count(Buffer) != 0)
        return Buffer;
    for (const auto& Pool : VbPools_)
    {
        if (Pool.second == Buffer)
            return Buffer;
    }
    return nullptr;
}

FRpGpuBuffer* FRpGpu::LiveIb(void* Handle) const
{
    FRpGpuBuffer* const Buffer = static_cast<FRpGpuBuffer*>(Handle);
    return IndexBuffers_.count(Buffer) != 0 ? Buffer : nullptr;
}

GENRESULT DACOM_API FRpGpu::VbMgrInit(void* A, void* B, void* C, void* D)
{
    (void)A;
    (void)B;
    (void)C;
    (void)D;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::VbMgrReleaseAll()
{
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::LockVertexMemory(unsigned int Fvf, unsigned int Count, FVertexMemoryLock* Out)
{
    if (Out == nullptr)
        return GR_INVALID_PARMS;
    FRpGpuBuffer* const Pool = PoolBuffer(Fvf);
    unsigned int Start = 0;
    if (Pool == nullptr || !LockRing(Pool, Count, &Start))
        return GR_OUT_OF_MEMORY;
    Out->Listing.VertexBuffer = Pool;
    Out->Listing.Fvf = Fvf;
    Out->Listing.BaseVertex = Start;
    Out->Vertices = Pool->Data.data() + static_cast<std::size_t>(Start) * Pool->Stride;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::UnlockVertexMemory(FVertexMemoryLock* Out)
{
    if (Out == nullptr || Out->Listing.VertexBuffer == nullptr)
        return GR_INVALID_PARMS;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::UnsupportedManagerOperation()
{
    return kUnsupported;
}

GENRESULT DACOM_API FRpGpu::CopyVertexData(void* Dest, unsigned int Fvf, const FRpVertexSrcDesc* Source,
                                           unsigned int StartVertex, unsigned int Count)
{
    // RP8 converts the source streams into Dest. Nothing reads the vertices yet.
    (void)Dest;
    (void)Fvf;
    (void)Source;
    (void)StartVertex;
    (void)Count;
    return GR_OK;
}

// Index buffers. Handle -1 is the scratch ring (0x4000 indices) alchemy draws from.
GENRESULT DACOM_API FRpGpu::CreateIb(unsigned int Count, unsigned int** Out, unsigned int Flags)
{
    if (Out == nullptr)
        return GR_INVALID_PARMS;
    FRpGpuBuffer* const Buffer = new (std::nothrow) FRpGpuBuffer();
    if (Buffer == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    Buffer->bRing = (Flags & 1) != 0;
    if (!ResizeStorage(Buffer, std::max(1u, Count), 2))
    {
        delete Buffer;
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    IndexBuffers_.insert(Buffer);
    *Out = reinterpret_cast<unsigned int*>(Buffer);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::DestroyIb(void** Handle)
{
    if (Handle == nullptr || *Handle == kMinusOne)
        return GR_GENERIC;
    FRpGpuBuffer* const Buffer = LiveIb(*Handle);
    if (Buffer != nullptr)
    {
        IndexBuffers_.erase(Buffer);
        delete Buffer;
    }
    *Handle = nullptr;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::ResizeIb(void* Handle, unsigned int Count)
{
    FRpGpuBuffer* const Buffer = LiveIb(Handle);
    if (Buffer == nullptr)
        return kFail;
    return ResizeStorage(Buffer, std::max(1u, Count), 2) ? GR_OK : GR_OUT_OF_MEMORY;
}

GENRESULT DACOM_API FRpGpu::CopyIndices(void* Handle, unsigned int* OutOffset, const unsigned short* SrcData,
                                        unsigned int Count)
{
    void* Bits = nullptr;
    const GENRESULT Result = LockIb(Handle, OutOffset, &Bits, Count);
    if (Result != GR_OK)
        return Result;
    if (SrcData != nullptr && Bits != nullptr)
        std::memcpy(Bits, SrcData, static_cast<std::size_t>(Count) * 2);
    return UnlockIb(Handle);
}

// Offset is in indices, in and out. A dynamic buffer ignores the input and returns its ring
// position.
GENRESULT DACOM_API FRpGpu::LockIb(void* Handle, unsigned int* Offset, void** Out, unsigned int Count)
{
    FRpGpuBuffer* const Buffer = Handle == kMinusOne ? &ScratchIb_ : LiveIb(Handle);
    if (Buffer == nullptr || Count > Buffer->Count || Out == nullptr)
        return kFail;
    unsigned int Start = Offset != nullptr ? *Offset : 0;
    if (Buffer->bRing)
    {
        if (!LockRing(Buffer, Count, &Start))
            return kFail;
    }
    else if (Start > Buffer->Count || Count > Buffer->Count - Start)
    {
        return kFail;
    }
    if (Offset != nullptr)
        *Offset = Start;
    Buffer->bLocked = true;
    *Out = Buffer->Data.data() + static_cast<std::size_t>(Start) * 2;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::UnlockIb(void* Handle)
{
    FRpGpuBuffer* const Buffer = Handle == kMinusOne ? &ScratchIb_ : LiveIb(Handle);
    if (Buffer == nullptr)
        return kFail;
    Buffer->bLocked = false;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SelectIb(void* Handle, unsigned int BaseVertexIndex, unsigned int Unused3,
                                     unsigned int Unused4)
{
    (void)BaseVertexIndex;
    (void)Unused3;
    (void)Unused4;
    return (Handle == kMinusOne || LiveIb(Handle) != nullptr) ? GR_OK : kFail;
}

GENRESULT DACOM_API FRpGpu::GetIbCapacity(void* Handle, unsigned int* OutCapacity)
{
    FRpGpuBuffer* const Buffer = Handle == kMinusOne ? &ScratchIb_ : LiveIb(Handle);
    if (OutCapacity != nullptr)
        *OutCapacity = Buffer != nullptr ? Buffer->Count : 0;
    return GR_OK;
}

// 1 when valid, 0 otherwise: Deformable2 re-creates its buffer on 0.
GENRESULT DACOM_API FRpGpu::IsIbValid(void* Handle)
{
    return (Handle == kMinusOne || LiveIb(Handle) != nullptr) ? 1 : 0;
}

// Vertex buffers. Flags & 1 hands out the shared pool buffer of that FVF.
GENRESULT DACOM_API FRpGpu::CreateVb(unsigned int Fvf, unsigned int Count, unsigned int** Out, unsigned int Flags)
{
    if (Out == nullptr)
        return GR_INVALID_PARMS;
    FRpGpuBuffer* Buffer = nullptr;
    if ((Flags & 1) != 0)
    {
        // The shared pool, grown first when it is smaller than the request.
        Buffer = PoolBuffer(Fvf);
        if (Buffer != nullptr && Count > Buffer->Count)
        {
            unsigned int Grown = 0;
            if (!RoundToPoolStep(Count, &Grown) || !ResizeStorage(Buffer, Grown, Buffer->Stride))
                Buffer = nullptr;
        }
    }
    else
    {
        Buffer = new (std::nothrow) FRpGpuBuffer();
        if (Buffer != nullptr)
        {
            Buffer->Fvf = Fvf;
            if (ResizeStorage(Buffer, std::max(1u, Count), std::max(1u, Rp8FvfStride(Fvf))))
            {
                VertexBuffers_.insert(Buffer);
            }
            else
            {
                delete Buffer;
                Buffer = nullptr;
            }
        }
    }
    *Out = reinterpret_cast<unsigned int*>(Buffer);
    return Buffer != nullptr ? GR_OK : GR_OUT_OF_MEMORY;
}

GENRESULT DACOM_API FRpGpu::DestroyVb(void** Handle)
{
    if (Handle == nullptr)
        return GR_GENERIC;
    if (*Handle == kMinusOne)
        return GR_OK;
    FRpGpuBuffer* const Buffer = static_cast<FRpGpuBuffer*>(*Handle);
    if (VertexBuffers_.count(Buffer) != 0)
    {
        VertexBuffers_.erase(Buffer);
        delete Buffer;
    }
    *Handle = nullptr; // a pool buffer stays alive
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::ResizeVb(void* Handle, unsigned int Fvf, unsigned int Count)
{
    FRpGpuBuffer* const Buffer = static_cast<FRpGpuBuffer*>(Handle);
    if (VertexBuffers_.count(Buffer) != 0)
    {
        if (!ResizeStorage(Buffer, std::max(1u, Count), std::max(1u, Rp8FvfStride(Fvf))))
            return GR_OUT_OF_MEMORY;
        Buffer->Fvf = Fvf;
    }
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::CopyVertices(void* Handle, unsigned int* OutOffset, const FRpVertexSrcDesc* SrcData,
                                         unsigned int StartVertex, unsigned int Count)
{
    (void)StartVertex;
    if (Handle == nullptr)
        return GR_GENERIC;
    void* Target = Handle;
    if (Handle == kMinusOne)
    {
        if (SrcData == nullptr)
            return GR_GENERIC;
        Target = PoolBuffer(SrcData->SrcFvf);
    }
    FRpGpuBuffer* const Buffer = LiveVb(Target);
    if (Buffer == nullptr)
        return GR_GENERIC;
    if (Buffer->bLocked)
        return GR_OK; // the offset is already a vertex index
    // The vertices are not converted yet: nothing reads them.
    void* Bits = nullptr;
    unsigned int Offset = OutOffset != nullptr ? *OutOffset : 0; // bytes in, as LockVb takes them
    const GENRESULT Result = LockVb(Target, &Offset, &Bits, Count);
    if (Result != GR_OK)
        return Result;
    if (OutOffset != nullptr)
        *OutOffset = Offset;
    return UnlockVb(Target);
}

// Offset goes in as bytes and comes back as a vertex index. Pool buffers ignore the input and
// return their ring position.
GENRESULT DACOM_API FRpGpu::LockVb(void* Handle, unsigned int* Offset, void** Out, unsigned int Count)
{
    if (Handle == nullptr || Handle == kMinusOne || Out == nullptr)
        return kFail;
    FRpGpuBuffer* const Buffer = LiveVb(Handle);
    if (Buffer == nullptr)
        return kFail;
    unsigned int Start = 0;
    if (Buffer->bRing)
    {
        if (!LockRing(Buffer, Count, &Start))
            return kFail;
    }
    else
    {
        Start = (Offset != nullptr ? *Offset : 0) / Buffer->Stride;
        if (Start > Buffer->Count || Count > Buffer->Count - Start)
            return kFail;
    }
    if (Offset != nullptr)
        *Offset = Start;
    Buffer->bLocked = true;
    *Out = Buffer->Data.data() + static_cast<std::size_t>(Start) * Buffer->Stride;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::UnlockVb(void* Handle)
{
    if (Handle == nullptr || Handle == kMinusOne)
        return kFail;
    FRpGpuBuffer* const Buffer = LiveVb(Handle);
    if (Buffer == nullptr)
        return kFail;
    Buffer->bLocked = false;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::UnsupportedVertexBufferOperation(void* Handle)
{
    (void)Handle;
    return kUnsupported;
}

GENRESULT DACOM_API FRpGpu::SelectVb(void* Handle)
{
    return LiveVb(Handle) != nullptr ? GR_OK : kFail;
}

GENRESULT DACOM_API FRpGpu::GetVbInfo(void* Handle, unsigned int* OutFvf, unsigned int* OutCount)
{
    FRpGpuBuffer* const Buffer = LiveVb(Handle);
    if (OutFvf != nullptr)
        *OutFvf = Buffer != nullptr ? Buffer->Fvf : 0;
    if (OutCount != nullptr)
        *OutCount = Buffer != nullptr ? Buffer->Count : 0;
    return GR_OK;
}

// 1 when valid (or the pool handle -1), 0 otherwise.
GENRESULT DACOM_API FRpGpu::IsVbValid(void* Handle)
{
    if (Handle == kMinusOne)
        return 1;
    return LiveVb(Handle) != nullptr ? 1 : 0;
}

GENRESULT DACOM_API FRpGpu::LoadTexture(IFileSystem* FileSystem, const char* FileName, void** OutHandle)
{
    (void)FileSystem;
    (void)FileName;
    if (OutHandle != nullptr)
        *OutHandle = nullptr;
    return GR_GENERIC; // no caller
}

GENRESULT DACOM_API FRpGpu::LoadSurfaceData(void* Handle, int Level, unsigned int SrcSize, const void* SrcData)
{
    (void)Level;
    (void)SrcSize;
    (void)SrcData;
    return LiveTexture(Handle) != nullptr ? GR_OK : GR_INVALID_PARMS;
}

// D3DXIMAGE_INFO for a DDS file, read from its header: +0 width, +4 height, +8 depth,
// +0xC mip levels, +0x10 D3DFORMAT, +0x14 resource type, +0x18 file format (4, DDS). Shading
// loads only DDS this way; anything else fails as D3DX's answer would make it.
GENRESULT DACOM_API FRpGpu::GetImageInfo(RpImageInfo* Info, unsigned int SrcSize, const void* SrcData)
{
    return ReadDdsImageInfo(Info, SrcSize, SrcData) ? GR_OK : GR_GENERIC;
}

GENRESULT DACOM_API FRpGpu::LoadCubemap(IFileSystem* FileSystem, const char* FileName, void** OutHandle)
{
    // Ability 8 (cube maps) is 0: RP8 answers this before touching the file system.
    (void)FileSystem;
    (void)FileName;
    if (OutHandle != nullptr)
        *OutHandle = nullptr;
    return kUnsupported;
}
