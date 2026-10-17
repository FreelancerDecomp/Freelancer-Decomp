// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TriMesh archetype, vertex streams and face groups.

#include "TriMesh.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------------------
// Streams and descriptors
// ---------------------------------------------------------------------------------------

// FUN_06c23dc0 (cdecl): delete the element and index arrays, zero all five dwords.
void TriMeshStreamFree(FRpVertexStream* Stream)
{
    delete[] Stream->Base;
    delete[] (const unsigned char*)Stream->IndexArray;
    Stream->Base = nullptr;
    Stream->ByteSize = 0;
    Stream->Stride = 0;
    Stream->IndexArray = nullptr;
    Stream->Count = 0;
}

// FUN_06c24c60 (cdecl; source, dest, index count): ByteSize / Stride / Count copied; the elements
// (Count * Stride bytes) and the index array (IndexCount dwords) duplicated when present.
void TriMeshStreamCopy(const FRpVertexStream* Source, FRpVertexStream* Dest, unsigned long IndexCount)
{
    Dest->ByteSize = Source->ByteSize;
    Dest->Stride = Source->Stride;
    Dest->Count = Source->Count;
    if (Source->Base != nullptr)
    {
        unsigned char* Elements = new unsigned char[Source->Count * Source->Stride];
        Dest->Base = Elements;
        std::memcpy(Elements, Source->Base, Source->Count * Source->Stride);
    }
    else
        Dest->Base = nullptr;
    if (Source->IndexArray != nullptr)
    {
        void* Indices = new unsigned char[IndexCount * 4];
        Dest->IndexArray = (unsigned long*)Indices;
        std::memcpy(Indices, Source->IndexArray, IndexCount * 4);
    }
    else
        Dest->IndexArray = nullptr;
}

// FUN_06c23f90 (cdecl): free the five streams, then zero the whole 0x70 descriptor.
void TriMeshDescFree(FRpVertexSrcDesc* Desc)
{
    TriMeshStreamFree(&Desc->Position);
    TriMeshStreamFree(&Desc->Normal);
    TriMeshStreamFree(&Desc->Diffuse);
    TriMeshStreamFree(&Desc->Tex[0]);
    TriMeshStreamFree(&Desc->Tex[1]);
    std::memset(Desc, 0, sizeof(FRpVertexSrcDesc));
}

// FUN_06c29190 / FUN_06c24be0 (cdecl; source, dest; two identical bodies): Flags, SrcFvf and
// SrcCount copied, each stream duplicated with SrcCount indices.
static void TriMeshDescCopy(const FRpVertexSrcDesc* Source, FRpVertexSrcDesc* Dest)
{
    Dest->Flags = Source->Flags;
    Dest->SrcFvf = Source->SrcFvf;
    Dest->SrcCount = Source->SrcCount;
    TriMeshStreamCopy(&Source->Position, &Dest->Position, Source->SrcCount);
    TriMeshStreamCopy(&Source->Normal, &Dest->Normal, Source->SrcCount);
    TriMeshStreamCopy(&Source->Diffuse, &Dest->Diffuse, Source->SrcCount);
    TriMeshStreamCopy(&Source->Tex[0], &Dest->Tex[0], Source->SrcCount);
    TriMeshStreamCopy(&Source->Tex[1], &Dest->Tex[1], Source->SrcCount);
}

static FRpVertexSrcDesc* TriMeshDescNew()
{
    return new FRpVertexSrcDesc{};
}

// FUN_06c29250 (cdecl; desc, channel bits): false when a requested channel has no index array
// (1 position +0x10, 8 normal +0x24, 2 tex0 +0x4C, 4 tex1 +0x60, 0x10 diffuse +0x38).
static bool TriMeshDescHasIndexArrays(const FRpVertexSrcDesc* Desc, unsigned char Channels)
{
    if ((Channels & 0x1) != 0 && Desc->Position.IndexArray == nullptr)
        return false;
    if ((Channels & 0x8) != 0 && Desc->Normal.IndexArray == nullptr)
        return false;
    if ((Channels & 0x2) != 0 && Desc->Tex[0].IndexArray == nullptr)
        return false;
    if ((Channels & 0x4) != 0 && Desc->Tex[1].IndexArray == nullptr)
        return false;
    if ((Channels & 0x10) != 0 && Desc->Diffuse.IndexArray == nullptr)
        return false;
    return true;
}

// ---------------------------------------------------------------------------------------
// Face groups
// ---------------------------------------------------------------------------------------

// FUN_06c276a0: Type 4, Bucket 4, everything else 0.
// formerly TriMeshGroupInit
void FTriMeshGroup::Initialize()
{
    this->Type = D3DPT_TRIANGLELIST;
    this->MaterialCrc = 0;
    this->Material = nullptr;
    this->Field_0x0C = nullptr;
    this->MinIndex = 0;
    this->MaxIndex = 0;
    this->Indices = nullptr;
    this->IndexCount = 0;
    this->EdgeIndices = nullptr;
    this->EdgeAngles = nullptr;
    this->EdgeCount = 0;
    this->Bucket = 4;
    this->Vertices = nullptr;
    this->SharedVertices = nullptr;
    this->ExtraIndexCounts = nullptr;
    this->ExtraStreams = nullptr;
}

static FTriMeshGroup* TriMeshGroupNew()
{
    FTriMeshGroup* Group = new FTriMeshGroup;
    Group->Initialize();
    return Group;
}

// FUN_06c29000 (thiscall): with no extra streams yet, a zeroed 0x10 count block (+0x34) and a
// zeroed 0x50 block of four streams (+0x38).
// formerly TriMeshGroupAllocExtras
void FTriMeshGroup::AllocateExtraStreams()
{
    if (ExtraStreams != nullptr)
        return;
    ExtraIndexCounts = new unsigned long[ExtraStreamCount]{};
    ExtraStreams = new FRpVertexStream[ExtraStreamCount]{};
}

// FUN_06c29040 (thiscall): with extra streams, delete the count block, free the four streams and
// delete their block.
// formerly TriMeshGroupFreeExtras
void FTriMeshGroup::FreeExtraStreams()
{
    if (ExtraStreams == nullptr)
        return;
    delete[] ExtraIndexCounts;
    ExtraIndexCounts = nullptr;
    for (unsigned long Index = 0; Index < ExtraStreamCount; ++Index)
        TriMeshStreamFree(&ExtraStreams[Index]);
    delete[] ExtraStreams;
    ExtraStreams = nullptr;
}

// FUN_06c27910 (thiscall, RET 0xC; count, indices, angles): both arrays deleted and cleared, the
// count zeroed; a non-zero count copies Count ushorts and, with angles, Count / 2 floats.
// formerly TriMeshGroupSetEdges
void FTriMeshGroup::SetEdges(unsigned long Count, const unsigned short* Indices, const float* Angles)
{
    delete[] (const unsigned char*)this->EdgeIndices;
    this->EdgeIndices = nullptr;
    delete[] (const unsigned char*)this->EdgeAngles;
    this->EdgeAngles = nullptr;
    this->EdgeCount = 0;
    if (Count == 0)
        return;
    this->EdgeIndices = (unsigned short*)new unsigned char[Count * 2];
    std::memcpy(this->EdgeIndices, Indices, Count * 2);
    if (Angles != nullptr)
    {
        const unsigned long Bytes = (Count >> 1) * 4;
        this->EdgeAngles = (float*)new unsigned char[Bytes];
        std::memcpy(this->EdgeAngles, Angles, Bytes);
    }
    this->EdgeCount = Count;
}

// FUN_06c278b0 (thiscall, RET 8; count, indices): the index array replaced by a copy, Min/Max 0.
// formerly TriMeshGroupSetIndices
void FTriMeshGroup::SetIndices(unsigned long Count, const unsigned short* Source)
{
    delete[] (const unsigned char*)this->Indices;
    this->Indices = nullptr;
    if (Count != 0)
    {
        this->Indices = (unsigned short*)new unsigned char[Count * 2];
        std::memcpy(this->Indices, Source, Count * 2);
    }
    this->MinIndex = 0;
    this->MaxIndex = 0;
    this->IndexCount = Count;
}

// FUN_06c27a20 (thiscall): a non-zero MinIndex is added back onto every index, then cleared.
// formerly TriMeshGroupUnrebase
void FTriMeshGroup::Unrebase()
{
    if (this->MinIndex == 0)
        return;
    for (unsigned long Index = 0; Index < this->IndexCount; ++Index)
        this->Indices[Index] = (unsigned short)(
            this->Indices[Index] + (unsigned short)this->MinIndex);
    this->MinIndex = 0;
}

// FUN_06c279a0 (thiscall): FUN_06c27a20, then Min/Max seeded from the first and the last index
// (read even when IndexCount is 0, as retail), widened over every index, and Min subtracted.
// formerly TriMeshGroupRebase
void FTriMeshGroup::Rebase()
{
    Unrebase();
    unsigned short* Indices = this->Indices;
    const unsigned long Count = this->IndexCount;
    const unsigned short First = Indices[0];
    const unsigned short Last = Indices[Count - 1];
    this->MinIndex = (First < Last) ? First : Last;
    this->MaxIndex = (First >= Last) ? First : Last;
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        const unsigned long Value = Indices[Index];
        if (Value < this->MinIndex)
            this->MinIndex = Value;
        else if (Value > this->MaxIndex)
            this->MaxIndex = Value;
    }
    for (unsigned long Index = 0; Index < Count; ++Index)
        Indices[Index] = (unsigned short)(Indices[Index] - (unsigned short)this->MinIndex);
}

// formerly TriMeshGroupDestroy
void FTriMeshGroup::ReleaseResources()
{
    SetEdges(0, nullptr, nullptr);
    if (this->Vertices != this->SharedVertices)
    {
        TriMeshDescFree(this->Vertices);
        delete this->Vertices;
    }
    this->Vertices = nullptr;
    this->SharedVertices = nullptr;
    this->FreeExtraStreams();
    delete[] (const unsigned char*)this->Indices;
    if (this->Material != nullptr)
    {
        this->Material->Release();
        this->Material = nullptr;
    }
}

// formerly TriMeshGroupAssign
void FTriMeshGroup::CopyFrom(const FTriMeshGroup* Source)
{
    SetIndices(Source->IndexCount, Source->Indices);
    SetEdges(Source->EdgeCount, Source->EdgeIndices, Source->EdgeAngles);
    this->Type = Source->Type;
    this->MinIndex = Source->MinIndex;
    this->MaxIndex = Source->MaxIndex;
    this->MaterialCrc = Source->MaterialCrc;
    if (this->Material != nullptr)
    {
        this->Material->Release();
        this->Material = nullptr;
    }
    this->Material = Source->Material;
    if (this->Material != nullptr)
        this->Material->AddRef();
    this->Field_0x0C = Source->Field_0x0C;
    this->SharedVertices = Source->SharedVertices;
    this->Bucket = Source->Bucket;
    if (Source->Vertices != Source->SharedVertices)
    {
        this->Vertices = TriMeshDescNew();
        TriMeshDescCopy(Source->Vertices, this->Vertices);
    }
    else
        this->Vertices = Source->Vertices;
    if (Source->ExtraStreams != nullptr)
    {
        if (this->ExtraStreams != nullptr)
        {
            for (int Index = 0; Index < 4; ++Index)
                TriMeshStreamFree(&this->ExtraStreams[Index]);
        }
        else
            this->AllocateExtraStreams();
        for (int Index = 0; Index < 4; ++Index)
        {
            this->ExtraIndexCounts[Index] = Source->ExtraIndexCounts[Index];
            TriMeshStreamCopy(&Source->ExtraStreams[Index], &this->ExtraStreams[Index],
                              Source->ExtraIndexCounts[Index]);
        }
    }
    else if (this->ExtraStreams != nullptr)
        this->FreeExtraStreams();
}

// operator new(0x40) + FUN_06c276e0 (the copy ctor: FUN_06c276a0, then FUN_06c27780).
FTriMeshGroup* TriMeshGroupCopy(const FTriMeshGroup* Source)
{
    FTriMeshGroup* Group = TriMeshGroupNew();
    Group->CopyFrom(Source);
    return Group;
}

// FUN_06c27a50 (thiscall, RET 4): Type and CRC copied, the material swapped (Release; AddRef when
// the source has one), +0x0C copied.
// formerly TriMeshGroupAssignHeader
void FTriMeshGroup::CopyHeader(const FTriMeshGroup* Source)
{
    this->Type = Source->Type;
    this->MaterialCrc = Source->MaterialCrc;
    if (this->Material != nullptr)
    {
        this->Material->Release();
        this->Material = nullptr;
    }
    this->Material = Source->Material;
    if (this->Material != nullptr)
        this->Material->AddRef();
    this->Field_0x0C = Source->Field_0x0C;
}

// formerly TriMeshGroupHasChannels
bool FTriMeshGroup::HasChannels(unsigned long Channels) const
{
    if ((Channels & 0x80) != 0 && this->EdgeIndices == nullptr)
        return false;
    if ((Channels & 0x100) != 0 && this->EdgeAngles == nullptr)
        return false;
    if ((Channels & 0x4000) != 0 &&
        (this->ExtraStreams == nullptr || this->ExtraStreams[0].IndexArray == nullptr))
        return false;
    if ((Channels & 0x8000) != 0 &&
        (this->ExtraStreams == nullptr || this->ExtraStreams[1].IndexArray == nullptr))
        return false;
    if ((Channels & 0x10000) != 0 &&
        (this->ExtraStreams == nullptr || this->ExtraStreams[2].IndexArray == nullptr))
        return false;
    if ((Channels & 0x20000) != 0 &&
        (this->ExtraStreams == nullptr || this->ExtraStreams[3].IndexArray == nullptr))
        return false;
    return TriMeshDescHasIndexArrays(this->Vertices, (unsigned char)Channels);
}

// FUN_06c29090 (thiscall): a type-5 strip becomes a type-4 list. Degenerate triangles are
// dropped, odd strip positions swap the last two corners.
// formerly TriMeshGroupStripToList
void FTriMeshGroup::ConvertStripToList()
{
    if (this->Type != D3DPT_TRIANGLESTRIP)
        return;
    const unsigned long TriangleSlots = this->IndexCount - 2;
    unsigned short* List = (unsigned short*)new unsigned char[TriangleSlots * 3 * 2];
    const unsigned short* Strip = this->Indices;
    unsigned long Written = 0;
    for (unsigned long Index = 0; Index < TriangleSlots; ++Index)
    {
        const unsigned short A = Strip[Index];
        const unsigned short B = Strip[Index + 1];
        const unsigned short C = Strip[Index + 2];
        if (A == B || B == C || C == A)
            continue;
        List[Written] = A;
        if ((Index & 1) != 0)
        {
            List[Written + 1] = C;
            List[Written + 2] = B;
        }
        else
        {
            List[Written + 1] = B;
            List[Written + 2] = C;
        }
        Written += 3;
    }
    this->IndexCount = Written;
    delete[] (const unsigned char*)this->Indices;
    this->Indices = (unsigned short*)new unsigned char[Written * 2];
    std::memcpy(this->Indices, List, Written * 2);
    delete[] (const unsigned char*)List;
    this->Type = D3DPT_TRIANGLELIST;
}

// UTF readers.

static const GENRESULT kTriMeshReadFailed = (GENRESULT)0x80004005;

// FUN_06c249d0 (file, name, buffer size, buffer): at most Size - 1 bytes; success needs the read
// to report exactly that many, and then terminates the string.
static GENRESULT TriMeshReadString(IFileSystem* File, const char* Name, unsigned long Size, char* Buffer)
{
    FFileSystemDesc Desc;
    FileDescInit(&Desc, Name, "FileSystem");
    int bOk = 0;
    const int Handle = File->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        unsigned long Length = File->GetSize((unsigned long)Handle, 0);
        if (Length >= Size - 1)
            Length = Size - 1;
        unsigned long ReadCount = 0;
        if (File->Read((unsigned long)Handle, Buffer, Length, &ReadCount, 0) != 0 &&
            ReadCount == Length)
        {
            bOk = 1;
            Buffer[ReadCount] = '\0';
        }
        File->CloseFile((unsigned long)Handle);
    }
    return (bOk != 0) ? GR_OK : kTriMeshReadFailed;
}

// FUN_06c271f0 (file, name, out): four bytes; only an exact 4-byte read stores.
static GENRESULT TriMeshReadInt(IFileSystem* File, const char* Name, unsigned long* Out)
{
    FFileSystemDesc Desc;
    FileDescInit(&Desc, Name, "FileSystem");
    int bOk = 0;
    const int Handle = File->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        unsigned long Value = 0;
        unsigned long ReadCount = 0;
        if (File->Read((unsigned long)Handle, &Value, 4, &ReadCount, 0) != 0 && ReadCount == 4)
        {
            bOk = 1;
            *Out = Value;
        }
        File->CloseFile((unsigned long)Handle);
    }
    return (bOk != 0) ? GR_OK : kTriMeshReadFailed;
}

// FUN_06c27390: FUN_06c271f0 for a float (FLD/FSTP of the four bytes).
static GENRESULT TriMeshReadFloat(IFileSystem* File, const char* Name, float* Out)
{
    FFileSystemDesc Desc;
    FileDescInit(&Desc, Name, "FileSystem");
    int bOk = 0;
    const int Handle = File->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        float Value = 0.0f;
        unsigned long ReadCount = 0;
        if (File->Read((unsigned long)Handle, &Value, 4, &ReadCount, 0) != 0 && ReadCount == 4)
        {
            *Out = Value;
            bOk = 1;
        }
        File->CloseFile((unsigned long)Handle);
    }
    return (bOk != 0) ? GR_OK : kTriMeshReadFailed;
}

// FUN_06c27450 (2-byte elements) / FUN_06c27530 (4) / FUN_06c272b0 (8): Count = Size / element,
// a Count * element buffer, one read of the whole leaf size. A non-zero read result stores the
// count and the buffer (the read length is not checked); a zero result deletes the buffer.
template<typename ElementType>
static GENRESULT TriMeshReadArray(IFileSystem* File, const char* Name, unsigned int Shift,
                                  unsigned long* OutCount, ElementType** OutData)
{
    FFileSystemDesc Desc;
    FileDescInit(&Desc, Name, "FileSystem");
    int Result = 0;
    const int Handle = File->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        const unsigned long Size = File->GetSize((unsigned long)Handle, 0);
        const unsigned long Count = Size >> Shift;
        unsigned char* Buffer = new unsigned char[Count << Shift];
        unsigned long ReadCount = 0;
        Result = File->Read((unsigned long)Handle, Buffer, Size, &ReadCount, 0);
        if (Result != 0)
        {
            *OutCount = Count;
            *OutData = (ElementType*)Buffer;
        }
        else
            delete[] Buffer;
        File->CloseFile((unsigned long)Handle);
    }
    return (Result != 0) ? GR_OK : kTriMeshReadFailed;
}

// ---------------------------------------------------------------------------------------
// Construction, reset, destruction
// ---------------------------------------------------------------------------------------

FTriMeshArch::FTriMeshArch()
    : HookFacet(this)
    , RenderableFacet(this)
    , Cursor(0)
    , LockedMask(0)
    , Field_0x1E4(nullptr)
    , EditMode(0)
    , MaterialLibrary(nullptr)
    , Pipeline(nullptr)
    , VbManager(nullptr)
{
    std::memset(&UvAnim, 0, sizeof(UvAnim));
    Cursor = Groups.size();
    for (int Index = 0; Index < 10; ++Index)
        Locked[Index] = nullptr;
    std::memset(&Vertices, 0, sizeof(Vertices));
    Name[0] = '\0';
    ResetMesh();
}

// FUN_06c22550. The reset (FUN_06c23ea0), the three interfaces released in the order +0x224,
// +0x220, +0x21C, the two UV0 arrays deleted and the UV0 block zeroed; the vector storage
// (already emptied by the reset) is freed by std::vector.
FTriMeshArch::~FTriMeshArch()
{
    Reset();
    if (VbManager != nullptr)
    {
        VbManager->Release();
        VbManager = nullptr;
    }
    if (Pipeline != nullptr)
    {
        Pipeline->Release();
        Pipeline = nullptr;
    }
    if (MaterialLibrary != nullptr)
    {
        MaterialLibrary->Release();
        MaterialLibrary = nullptr;
    }
    delete[] (const unsigned char*)UvAnim.Lookup;
    delete[] (const unsigned char*)UvAnim.Frames;
    std::memset(&UvAnim, 0, sizeof(UvAnim));
}

int FTriMeshArch::Reset()
{
    CloseFaceGroup();
    Center[0] = 0.0f;
    Center[1] = 0.0f;
    Center[2] = 0.0f;
    for (int Index = 0; Index < 6; ++Index)
        Box[Index] = 0.0f;
    SphereCenter[0] = 0.0f;
    SphereCenter[1] = 0.0f;
    SphereCenter[2] = 0.0f;
    SphereRadius = -1.0f;
    for (size_t Index = 0; Index < Groups.size(); ++Index)
    {
        FTriMeshGroup* Group = Groups[Index];
        if (Group != nullptr)
        {
            Group->ReleaseResources();
            delete Group;
        }
    }
    Groups.clear();
    TriMeshDescFree(&Vertices);
    delete[] (const unsigned char*)UvAnim.Lookup;
    delete[] (const unsigned char*)UvAnim.Frames;
    std::memset(&UvAnim, 0, sizeof(UvAnim));
    Flags = 0;
    EditMode = 0;
    return 0;
}

// ---------------------------------------------------------------------------------------
// IDAComponent
// ---------------------------------------------------------------------------------------

// FUN_06c26530 (RET 0xC): exact compares, no AddRef. "1.11_IMesh" and "1.11_IDAComponent" ->
// this, "1.11_IMatHookUp" -> +4, "1.11_IRenderableMesh" -> +8, each 0. A miss is -1 with Out
// untouched. (The null-this answers of the +4/+8 cases cannot occur for a C++ object.)
GENRESULT DACOM_API FTriMeshArch::QueryInterface(const char* Iid, void** Out)
{
    if (std::strcmp(Iid, Iids::Mesh) == 0)
    {
        *Out = (IMesh*)this;
        return GR_OK;
    }
    if (std::strcmp(Iid, Iids::MatHookUp) == 0)
    {
        *Out = (IMatHookUp*)(&HookFacet);
        return GR_OK;
    }
    if (std::strcmp(Iid, Iids::RenderableMesh) == 0)
    {
        *Out = (IRenderableMesh*)(&RenderableFacet);
        return GR_OK;
    }
    if (std::strcmp(Iid, Iids::DAComponent) == 0)
    {
        *Out = (IMesh*)this;
        return GR_OK;
    }
    return (GENRESULT)-1;
}

// FUN_06c2b100 (06c3614c / 06c36150): MOV EAX,1. The archetype is not reference counted.
unsigned long DACOM_API FTriMeshArch::AddRef()
{
    return 1;
}

unsigned long DACOM_API FTriMeshArch::Release()
{
    return 1;
}

// LAB_06c27660 / LAB_06c27670: this - 4, then the archetype QI / FUN_06c2b100.
GENRESULT DACOM_API FTriMeshArch::FHookFacet::QueryInterface(const char* Iid, void** Out)
{
    return Owner->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FTriMeshArch::FHookFacet::AddRef()
{
    return 1;
}

unsigned long DACOM_API FTriMeshArch::FHookFacet::Release()
{
    return 1;
}

// LAB_06c27680 / LAB_06c27690: this - 8, then the archetype QI / FUN_06c2b100.
GENRESULT DACOM_API FTriMeshArch::FRenderableFacet::QueryInterface(const char* Iid, void** Out)
{
    return Owner->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FTriMeshArch::FRenderableFacet::AddRef()
{
    return 1;
}

unsigned long DACOM_API FTriMeshArch::FRenderableFacet::Release()
{
    return 1;
}

// The query IMaterialLibrary Slot16 takes: only +4 is read; retail's +0 is the address of
// "???" (DAT_06c37330).
struct FTriMeshMaterialQuery
{
    const char* Name;
    unsigned long Crc;
};

// formerly Slot3
int DACOM_API FTriMeshArch::FHookFacet::HookUpMaterials()
{
    FTriMeshArch* Arch = Owner;
    if (Arch->MaterialLibrary == nullptr)
        return -1;
    unsigned char bFailed = 0;
    std::vector<FTriMeshGroup*> Buckets[5];
    for (size_t Index = 0; Index < Arch->Groups.size(); ++Index)
    {
        FTriMeshGroup* Group = Arch->Groups[Index];
        IMaterial* Previous = Group->Material;
        if (Previous != nullptr)
            Previous->AddRef();
        bool bBound = false;
        if (Group->MaterialCrc != 0)
        {
            FTriMeshMaterialQuery Query;
            Query.Name = "???";
            Query.Crc = Group->MaterialCrc;
            if (Group->Material != nullptr)
            {
                Group->Material->Release();
                Group->Material = nullptr;
            }
            if (Arch->MaterialLibrary->FindMaterialByCrc(&Query, &Group->Material) == 0)
                bBound = true;
        }
        if (!bBound)
        {
            if (Group->Material != nullptr)
            {
                Group->Material->Release();
                Group->Material = nullptr;
            }
            bFailed = 1;
        }
        Group->Bucket = 4;
        if (Group->Material != nullptr)
        {
            unsigned long RenderFlags = 0;
            Group->Material->GetRenderFlags(&RenderFlags);
            const unsigned char Low = (unsigned char)RenderFlags;
            if ((Low & 0x1) != 0)
                Group->Bucket = 2;
            else if ((Low & 0x8) == 0)
                Group->Bucket = 3;
            else if ((Low & 0x12) != 0)
                Group->Bucket = 1;
            else
                Group->Bucket = 0;
        }
        Buckets[Group->Bucket].push_back(Group);
        if (Previous != nullptr)
            Previous->Release();
    }
    Arch->Groups.clear();
    for (int Bucket = 0; Bucket < 5; ++Bucket)
    {
        for (size_t Index = 0; Index < Buckets[Bucket].size(); ++Index)
            Arch->Groups.push_back(Buckets[Bucket][Index]);
    }
    return (bFailed != 0) ? -1 : 0;
}

// LAB_06c23df0 (RET 8): *Out = the shared descriptor's SrcCount (+0x88).
// formerly Slot3
int DACOM_API FTriMeshArch::FRenderableFacet::GetVertexCount(int* Out)
{
    *Out = (int)(Owner->Vertices.SrcCount);
    return 0;
}

// formerly Slot4
int DACOM_API FTriMeshArch::FRenderableFacet::CopyVertexData(void* Dest, unsigned int Fvf, unsigned int StartVertex, unsigned int VertexCount)
{
    return (int)(Owner->VbManager->CopyVertexData(Dest, Fvf, &Owner->Vertices, StartVertex, VertexCount));
}

// LAB_06c23e80 (RET 8): *Out = the first group's index count (the vector is not checked).
// formerly Slot5
int DACOM_API FTriMeshArch::FRenderableFacet::GetIndexCount(int* Out)
{
    FTriMeshGroup* First = Owner->Groups.data()[0];
    *Out = (int)First->IndexCount;
    return 0;
}

// formerly Slot6
int DACOM_API FTriMeshArch::FRenderableFacet::CopyIndices(unsigned short** Dest, int Count)
{
    FTriMeshGroup* First = Owner->Groups.data()[0];
    if ((unsigned long)Count != First->IndexCount)
        return -1;
    std::memcpy(*Dest, First->Indices, (unsigned long)Count * 2);
    return 0;
}

// ---------------------------------------------------------------------------------------
// IMesh (PTR_FUN_06c36148)
// ---------------------------------------------------------------------------------------

// FUN_06c225f0 (RET 4): the reset; a negative result -> -1, else +0x1E4 = 0 and 0.
// formerly Slot3
int DACOM_API FTriMeshArch::ResetMesh()
{
    if (Reset() < 0)
        return -1;
    Field_0x1E4 = nullptr;
    return 0;
}

// FUN_06c2f5e0 (RET 4): XOR EAX,EAX.
// formerly Slot4
int DACOM_API FTriMeshArch::NoOp()
{
    return 0;
}

// FUN_06c22620 (RET 8): the IMatHookUp facet's Slot3 (the hook-up, result dropped); flag bit 2
// calls FUN_06c2e3e0, which is empty. 0.
// formerly Slot5
int DACOM_API FTriMeshArch::RefreshMaterials(unsigned long /*Flags*/)
{
    HookFacet.HookUpMaterials();
    return 0;
}

// LAB_06c22650: OR EAX,-1; RET 8.
// formerly Slot6
int DACOM_API FTriMeshArch::RejectOperation(void* /*A*/)
{
    return -1;
}

// FUN_06c22660: OR EAX,-1; RET 4.
// formerly Slot7
int DACOM_API FTriMeshArch::RejectOperation()
{
    return -1;
}

// LAB_06c22670 (RET 8): *Out = the group count (0 for a vector that never allocated). 0.
// formerly Slot8
int DACOM_API FTriMeshArch::GetFaceGroupCount(int* Out)
{
    if (Groups.data() == nullptr)
    {
        *Out = 0;
        return 0;
    }
    *Out = (int)(Groups.size());
    return 0;
}

// LAB_06c226a0 (RET 0xC): open an edit on group Index. An edit already open, no vector or an
// index past the end -> -1. Else EditMode = Mode, the cursor on the group, 0.
// formerly Slot9
int DACOM_API FTriMeshArch::OpenFaceGroup(int Index, int Mode)
{
    if (!CursorAtEnd())
        return -1;
    if (Groups.data() == nullptr || (unsigned int)Index >= Groups.size())
        return -1;
    EditMode = Mode;
    Cursor = (size_t)Index;
    return 0;
}

// formerly Slot10
int DACOM_API FTriMeshArch::CloseFaceGroup()
{
    if (CursorAtEnd())
        return -1;
    CloseFaceGroupChannels();
    UnlockFaceGroupChannels();
    Cursor = Groups.size();
    if (EditMode == 2)
    {
        ShareGroupVertices(false);
        CompactVertices();
    }
    return 0;
}

// FUN_06c22740 (RET 8): with no edit open, a new group of type A is appended (FUN_06c26fa0) and
// the cursor put at the end; else -1.
// formerly Slot11
int DACOM_API FTriMeshArch::AddFaceGroup(void* A)
{
    if (!CursorAtEnd())
        return -1;
    FTriMeshGroup* Group = TriMeshGroupNew();
    Group->Type = (unsigned long)((uintptr_t)A);
    Groups.insert(Groups.end(), 1, Group);
    Cursor = Groups.size();
    return 0;
}

// formerly Slot12
int DACOM_API FTriMeshArch::RemoveFaceGroup(int Index)
{
    Cursor = Groups.size();
    if (Groups.data() == nullptr)
        return -1;
    Groups.erase(Groups.begin() + Index);
    Cursor = Groups.size();
    return 0;
}

// LAB_06c22800 (RET 8): no edit -> -1; *Out = the group's Type dword.
// formerly Slot13
int DACOM_API FTriMeshArch::GetFaceGroupPrimitiveType(unsigned int* Out)
{
    if (CursorAtEnd())
        return -1;
    *Out = Groups[Cursor]->Type;
    return 0;
}

// LAB_06c22830 (RET 8): no edit -> -1. A group with a CRC hands out its material (AddRef'd when
// present); without a CRC *Out = 0. 0.
// formerly Slot14
int DACOM_API FTriMeshArch::GetFaceGroupMaterial(IMaterial** Out)
{
    if (CursorAtEnd())
        return -1;
    FTriMeshGroup* Group = Groups[Cursor];
    if (Group->MaterialCrc == 0)
    {
        *Out = nullptr;
        return 0;
    }
    IMaterial* Material = Group->Material;
    *Out = Material;
    if (Material != nullptr)
        Material->AddRef();
    return 0;
}

// formerly Slot15
int DACOM_API FTriMeshArch::SetFaceGroupMaterial(IMaterial* Material)
{
    if (CursorAtEnd())
        return -1;
    FTriMeshGroup* Group = Groups[Cursor];
    if (Group->Material != nullptr)
    {
        Group->Material->Release();
        if (Group->Material != nullptr)
        {
            Group->Material->Release();
            Group->Material = nullptr;
        }
    }
    if (Group->MaterialCrc != 0)
        Group->MaterialCrc = 0;
    IMaterial* NewMaterial = Material;
    if (NewMaterial == nullptr)
        return 0;
    if (Group->Material != NewMaterial)
    {
        if (Group->Material != nullptr)
        {
            Group->Material->Release();
            Group->Material = nullptr;
        }
        Group->Material = NewMaterial;
        NewMaterial->AddRef();
    }
    Group->Material->AddRef();
    char MaterialName[0x40];
    if (NewMaterial->GetName(MaterialName, 0x40) == 0)
        Group->MaterialCrc = DACOM_CRC::GetCRC32(MaterialName);
    return 0;
}

// LAB_06c22960 (RET 8): no edit -> -1; *Out = the group's +0x0C.
// formerly Slot16
int DACOM_API FTriMeshArch::GetFaceGroupMaterialContext(void** Out)
{
    if (CursorAtEnd())
        return -1;
    *Out = Groups[Cursor]->Field_0x0C;
    return 0;
}

// LAB_06c22990 (RET 8): no edit -> -1; the group's +0x0C = A.
// formerly Slot17
int DACOM_API FTriMeshArch::SetFaceGroupMaterialContext(void* A)
{
    if (CursorAtEnd())
        return -1;
    Groups[Cursor]->Field_0x0C = A;
    return 0;
}

// FUN_06c229c0 (RET 8): no edit -> -1; a channel the group lacks (FUN_06c28f50) -> 0xFFFFFFF8;
// else the group made private in mode 2 (FUN_06c26610), 0.
// formerly Slot18
int DACOM_API FTriMeshArch::OpenFaceGroupChannels(unsigned long Flags)
{
    if (CursorAtEnd())
        return -1;
    if (!Groups[Cursor]->HasChannels(Flags))
        return (int)0xFFFFFFF8;
    PrivatizeCursorGroup();
    return 0;
}

// LAB_06c22a00 (RET 4): 0 while an edit is open, -1 otherwise.
// formerly Slot19
int DACOM_API FTriMeshArch::CloseFaceGroupChannels()
{
    return CursorAtEnd() ? -1 : 0;
}

// formerly Slot20
int DACOM_API FTriMeshArch::LockFaceGroupChannels(unsigned long Mask)
{
    if (CursorAtEnd())
        return -1;
    if (!Groups[Cursor]->HasChannels(Mask))
        return (int)0xFFFFFFF8;
    PrivatizeCursorGroup();
    const unsigned long Order[9] = { 0x1, 0x8, 0x2, 0x4, 0x10, 0x4000, 0x8000, 0x10000, 0x20000 };
    for (int Index = 0; Index < 9; ++Index)
    {
        const unsigned long Bit = Order[Index];
        if ((Mask & Bit) != 0 && (LockedMask & Bit) == 0)
        {
            if (LockChannel(Bit) < 0)
                return -1;
            LockedMask |= Bit;
        }
    }
    if ((Mask & 0x80) != 0 && (LockedMask & 0x80) == 0)
        LockedMask |= 0x80;
    if ((Mask & 0x100) != 0 && (LockedMask & 0x100) == 0)
    {
        if (LockEdgeAngles() < 0)
            return -1;
        LockedMask |= 0x100;
    }
    return ((LockedMask & Mask) == Mask) ? 0 : -1;
}

// formerly Slot21
int DACOM_API FTriMeshArch::UnlockFaceGroupChannels()
{
    if (CursorAtEnd())
        return -1;
    if (LockedMask == 0)
        return 0;
    const unsigned long Order[9] = { 0x1, 0x8, 0x2, 0x4, 0x10, 0x4000, 0x8000, 0x10000, 0x20000 };
    for (int Index = 0; Index < 9; ++Index)
    {
        if ((LockedMask & Order[Index]) != 0)
            UnlockChannel(Order[Index]);
    }
    if ((LockedMask & 0x100) != 0)
        UnlockEdgeAngles();
    LockedMask = 0;
    return 0;
}

// formerly Slot22
int DACOM_API FTriMeshArch::GetFaceGroupData(int Channel, void** A, void** B, void** C, void** D)
{
    if (CursorAtEnd())
        return -1;
    FTriMeshGroup* Group = Groups[Cursor];
    const unsigned long Bits = (unsigned long)Channel;
    if (!Group->HasChannels(Bits))
        return -1;
    const FRpVertexStream* Stream = nullptr;
    switch (Bits)
    {
    case 0x1:
    case 0x80:
        Stream = &Group->Vertices->Position;
        break;
    case 0x2:
        Stream = &Group->Vertices->Tex[0];
        break;
    case 0x4:
        Stream = &Group->Vertices->Tex[1];
        break;
    case 0x8:
        Stream = &Group->Vertices->Normal;
        break;
    case 0x10:
        Stream = &Group->Vertices->Diffuse;
        break;
    case 0x100:
        *A = Group->EdgeAngles;
        *B = (void*)((uintptr_t)4);
        *D = (void*)((uintptr_t)(Group->EdgeCount >> 1));
        *C = (void*)((uintptr_t)4);
        return 0;
    case 0x4000:
    case 0x8000:
    case 0x10000:
    case 0x20000:
    {
        int Extra = 0;
        if (Bits == 0x8000)
            Extra = 1;
        else if (Bits == 0x10000)
            Extra = 2;
        else if (Bits == 0x20000)
            Extra = 3;
        if (Group->ExtraStreams == nullptr)
        {
            *A = nullptr;
            *B = nullptr;
            *D = nullptr;
            *C = nullptr;
            return 0;
        }
        Stream = &Group->ExtraStreams[Extra];
        *A = (unsigned char*)Stream->Base;
        *B = (void*)((uintptr_t)Stream->Stride);
        *D = (void*)((uintptr_t)Stream->Count);
        *C = (void*)((uintptr_t)Stream->ByteSize);
        return 0;
    }
    default:
        return -1;
    }
    *A = (unsigned char*)Stream->Base;
    *B = (void*)((uintptr_t)Stream->Stride);
    *D = (void*)((uintptr_t)Stream->Count);
    *C = (void*)((uintptr_t)Stream->ByteSize);
    return 0;
}

static void TriMeshStreamWrite(FRpVertexStream* Stream, const unsigned char* Src, unsigned long SrcStride,
                               unsigned long Size, unsigned long Count)
{
    const unsigned long Needed = Size * Count;
    if (Needed > Stream->Count * Stream->ByteSize)
    {
        delete[] Stream->Base;
        Stream->Base = new unsigned char[Needed];
    }
    unsigned char* Dest = (unsigned char*)Stream->Base;
    if (SrcStride == Size)
        std::memcpy(Dest, Src, SrcStride * Count);
    else
    {
        unsigned long Offset = 0;
        for (unsigned long Index = 0; Index < Count; ++Index)
        {
            std::memcpy(Dest + Offset, Src, Size);
            Src += SrcStride;
            Offset += Size;
        }
    }
    Stream->Stride = SrcStride;
    Stream->Count = Count;
    Stream->ByteSize = Size;
}

// formerly Slot23
int DACOM_API FTriMeshArch::SetFaceGroupData(int Channel, void* A, void* B, void* C, void* D)
{
    if (CursorAtEnd())
        return -1;
    if (EditMode != 2)
        return -1;
    FTriMeshGroup* Group = Groups[Cursor];
    const unsigned char* Src = (const unsigned char*)A;
    const unsigned long SrcStride = (unsigned long)((uintptr_t)B);
    const unsigned long Size = (unsigned long)((uintptr_t)C);
    const unsigned long Count = (unsigned long)((uintptr_t)D);
    switch ((unsigned long)Channel)
    {
    case 0x1:
        if (Count != Group->Vertices->Position.Count)
            Group->SetEdges(0, nullptr, nullptr);
        TriMeshStreamWrite(&Group->Vertices->Position, Src, SrcStride, Size, Count);
        return 0;
    case 0x2:
        TriMeshStreamWrite(&Group->Vertices->Tex[0], Src, SrcStride, Size, Count);
        return 0;
    case 0x4:
        TriMeshStreamWrite(&Group->Vertices->Tex[1], Src, SrcStride, Size, Count);
        return 0;
    case 0x8:
        TriMeshStreamWrite(&Group->Vertices->Normal, Src, SrcStride, Size, Count);
        return 0;
    case 0x10:
        TriMeshStreamWrite(&Group->Vertices->Diffuse, Src, SrcStride, Size, Count);
        return 0;
    case 0x4000:
    case 0x8000:
    case 0x10000:
    case 0x20000:
    {
        int Extra = 0;
        if (Channel == 0x8000)
            Extra = 1;
        else if (Channel == 0x10000)
            Extra = 2;
        else if (Channel == 0x20000)
            Extra = 3;
        if (Group->ExtraStreams == nullptr)
            Group->AllocateExtraStreams();
        TriMeshStreamWrite(&Group->ExtraStreams[Extra], Src, SrcStride, Size, Count);
        return 0;
    }
    default:
        return -1;
    }
}

// The locked-copy slot of a channel (+0x1F0 + 4 * n): 1 -> 0, 8 -> 1, 2 -> 2, 4 -> 3, 0x10 -> 4,
// 0x100 -> 5, 0x4000 -> 6, 0x8000 -> 7, 0x10000 -> 8, 0x20000 -> 9.
static int TriMeshLockedSlot(unsigned long Channel)
{
    switch (Channel)
    {
    case 0x1: return 0;
    case 0x8: return 1;
    case 0x2: return 2;
    case 0x4: return 3;
    case 0x10: return 4;
    case 0x100: return 5;
    case 0x4000: return 6;
    case 0x8000: return 7;
    case 0x10000: return 8;
    case 0x20000: return 9;
    default: return -1;
    }
}

// formerly Slot24
int DACOM_API FTriMeshArch::GetFaceGroupIndices(int Channel, void** A, void** B)
{
    if (CursorAtEnd())
        return -1;
    FTriMeshGroup* Group = Groups[Cursor];
    const unsigned long Bits = (unsigned long)Channel;
    if (Bits == 0x80)
    {
        *A = Group->EdgeIndices;
        *B = (void*)((uintptr_t)Group->EdgeCount);
        return 0;
    }
    const int Slot = TriMeshLockedSlot(Bits);
    if (Slot < 0)
        return -1;
    *A = Locked[Slot];
    if (Bits == 0x100)
        *B = (void*)((uintptr_t)(Group->EdgeCount >> 1));
    else
        *B = (void*)((uintptr_t)Group->IndexCount);
    return 0;
}

// formerly Slot25
int DACOM_API FTriMeshArch::SetFaceGroupIndices(int Channel, void* Src, int Count)
{
    if (CursorAtEnd())
        return -1;
    if (EditMode != 2)
        return -1;
    const unsigned long Bits = (unsigned long)Channel;
    if ((LockedMask & Bits) == 0)
        return -1;
    FTriMeshGroup* Group = Groups[Cursor];
    if ((unsigned long)Count != Group->IndexCount)
        return -1;
    if (Bits == 0x80 || Bits == 0x100)
        return -1;
    const int Slot = TriMeshLockedSlot(Bits);
    if (Slot < 0)
        return -1;
    std::memcpy(Locked[Slot], Src, (unsigned long)Count * 2);
    return 0;
}

// LAB_06c23550 (RET 0xC): no edit -> -1. Selector 1: *Out = index count; 2: *Out = EdgeCount;
// anything else -> -1.
// formerly Slot26
int DACOM_API FTriMeshArch::GetFaceGroupIndexCount(int Selector, void** Out)
{
    if (CursorAtEnd())
        return -1;
    FTriMeshGroup* Group = Groups[Cursor];
    if (Selector == 1)
    {
        *(unsigned long*)Out = Group->IndexCount;
        return 0;
    }
    if (Selector == 2)
    {
        *(unsigned long*)Out = Group->EdgeCount;
        return 0;
    }
    return -1;
}

// formerly Slot27
int DACOM_API FTriMeshArch::SetFaceGroupIndexCount(int Selector, int Count)
{
    if (CursorAtEnd())
        return -1;
    if (EditMode != 2)
        return -1;
    if (LockedMask != 0)
        return -1;
    if (Selector != 1)
        return -1;
    PrivatizeCursorGroup();
    Groups[Cursor]->SetEdges(0, nullptr, nullptr);
    FTriMeshGroup* Group = Groups[Cursor];
    if (Group->IndexCount != (unsigned long)Count)
    {
        delete[] (const unsigned char*)Group->Indices;
        Group->Indices = (unsigned short*)new unsigned char[(unsigned long)Count * 2];
        Group->IndexCount = (unsigned long)Count;
    }
    return 0;
}

// formerly Slot28
int DACOM_API FTriMeshArch::AddFaceGroupChannels(unsigned long Flags)
{
    if (CursorAtEnd())
        return -1;
    if (EditMode != 2)
        return -1;
    if (LockedMask != 0)
        return -1;
    int Result = -1;
    PrivatizeCursorGroup();
    FTriMeshGroup* Group = Groups[Cursor];
    FRpVertexSrcDesc* Desc = Group->Vertices;
    if ((Flags & 0x1) != 0)
    {
        Result = 0;
        if (Desc->Position.IndexArray == nullptr)
        {
            Desc->Position.IndexArray = (unsigned long*)new unsigned char[Desc->SrcCount * 4];
            Desc->SrcFvf |= 0x2;
        }
    }
    if ((Flags & 0x8) != 0)
    {
        Result = 0;
        if (Desc->Normal.IndexArray == nullptr)
        {
            Desc->Normal.IndexArray = (unsigned long*)new unsigned char[Desc->SrcCount * 4];
            Desc->SrcFvf |= 0x10;
        }
    }
    if ((Flags & 0x2) != 0)
    {
        Result = 0;
        if (Desc->Tex[0].IndexArray == nullptr)
        {
            Desc->Tex[0].IndexArray = (unsigned long*)new unsigned char[Desc->SrcCount * 4];
            Desc->SrcFvf = (Desc->SrcFvf & 0xFFFFF1FF) | 0x100;
        }
    }
    if ((Flags & 0x4) != 0)
    {
        Result = 0;
        if (Desc->Tex[1].IndexArray == nullptr)
        {
            Desc->Tex[1].IndexArray = (unsigned long*)new unsigned char[Desc->SrcCount * 4];
            Desc->SrcFvf = (Desc->SrcFvf & 0xFFFFF2FF) | 0x200;
        }
    }
    if ((Flags & 0x10) != 0)
    {
        Result = 0;
        if (Desc->Diffuse.IndexArray == nullptr)
        {
            Desc->Diffuse.IndexArray = (unsigned long*)new unsigned char[Desc->SrcCount * 4];
            Desc->SrcFvf |= 0x40;
        }
    }
    const unsigned long ExtraBits[4] = { 0x4000, 0x8000, 0x10000, 0x20000 };
    for (int Extra = 0; Extra < 4; ++Extra)
    {
        if ((Flags & ExtraBits[Extra]) == 0)
            continue;
        Result = 0;
        if (Group->ExtraStreams == nullptr)
            Group->AllocateExtraStreams();
        if (Group->ExtraStreams[Extra].IndexArray == nullptr)
        {
            Group->ExtraStreams[Extra].IndexArray =
                (unsigned long*)new unsigned char[Desc->SrcCount * 4];
            Group->ExtraIndexCounts[Extra] = Desc->SrcCount;
        }
    }
    if ((Flags & 0x80) != 0)
        Result = -1;
    if ((Flags & 0x100) != 0)
        Result = -1;
    return Result;
}

// formerly Slot29
int DACOM_API FTriMeshArch::RemoveFaceGroupChannels(unsigned long Flags)
{
    if (CursorAtEnd())
        return -1;
    if (EditMode != 2)
        return -1;
    if (LockedMask != 0)
        return -1;
    int Result = -1;
    PrivatizeCursorGroup();
    FTriMeshGroup* Group = Groups[Cursor];
    FRpVertexSrcDesc* Desc = Group->Vertices;
    if ((Flags & 0x1) != 0)
    {
        TriMeshStreamFree(&Desc->Position);
        Desc->SrcFvf &= 0xFFFFFFFD;
        Result = 0;
    }
    if ((Flags & 0x8) != 0)
    {
        TriMeshStreamFree(&Desc->Normal);
        Desc->SrcFvf &= 0xFFFFFFEF;
        Result = 0;
    }
    if ((Flags & 0x2) != 0)
    {
        TriMeshStreamFree(&Desc->Tex[0]);
        Desc->SrcFvf &= 0xFFFFF0FF;
        Result = 0;
    }
    if ((Flags & 0x4) != 0)
    {
        TriMeshStreamFree(&Desc->Tex[1]);
        Desc->SrcFvf = (Desc->SrcFvf & 0xFFFFF1FF) | 0x100;
        Result = 0;
    }
    if ((Flags & 0x10) != 0)
    {
        TriMeshStreamFree(&Desc->Diffuse);
        Desc->SrcFvf &= 0xFFFFFFBF;
        Result = 0;
    }
    const unsigned long ExtraBits[4] = { 0x4000, 0x8000, 0x10000, 0x20000 };
    for (int Extra = 0; Extra < 4; ++Extra)
    {
        if ((Flags & ExtraBits[Extra]) != 0 && Group->ExtraStreams != nullptr)
        {
            TriMeshStreamFree(&Group->ExtraStreams[Extra]);
            Group->ExtraIndexCounts[Extra] = 0;
            Result = 0;
        }
    }
    if ((Flags & 0x80) != 0)
    {
        delete[] (const unsigned char*)Group->EdgeIndices;
        Group->EdgeIndices = nullptr;
        if (Group->EdgeAngles == nullptr)
            Group->EdgeCount = 0;
        Result = 0;
    }
    if ((Flags & 0x100) != 0)
    {
        delete[] (const unsigned char*)Group->EdgeAngles;
        Group->EdgeAngles = nullptr;
        if (Group->EdgeIndices == nullptr)
            Group->EdgeCount = 0;
        Result = 0;
    }
    return Result;
}

// ---------------------------------------------------------------------------------------
// Loader (FUN_06c23fd0)
// ---------------------------------------------------------------------------------------

// One pool of the "Geometry" node, inline in retail: Count = Size / ElementSize, a Count *
// ElementSize buffer, one read of the whole leaf. A non-zero read keeps the buffer (Pool) and
// reports the count; a zero read deletes it. Returns false when the leaf is missing.
template<typename ElementType>
static bool TriMeshReadPool(IFileSystem* File, const char* Name, unsigned long ElementSize,
                            ElementType** OutBuffer, unsigned long* OutCount, bool* bOutRead)
{
    FFileSystemDesc Desc;
    FileDescInit(&Desc, Name, "FileSystem");
    const int Handle = File->OpenByDesc(&Desc);
    if (Handle == -1)
        return false;
    const unsigned long Size = File->GetSize((unsigned long)Handle, 0);
    const unsigned long Count = Size / ElementSize;
    unsigned char* Buffer = new unsigned char[Count * ElementSize];
    *OutBuffer = (ElementType*)Buffer;
    unsigned long ReadCount = 0;
    if (File->Read((unsigned long)Handle, Buffer, Size, &ReadCount, 0) != 0)
    {
        *OutCount = Count;
        *bOutRead = true;
    }
    else
    {
        delete[] Buffer;
        *bOutRead = false;
    }
    File->CloseFile((unsigned long)Handle);
    return true;
}

// A "*_indices" leaf: dwords, Count = Size >> 2. A good read sets SrcCount (+0x88) and the
// stream's index array; a failed one deletes the buffer.
static void TriMeshReadCornerIndices(IFileSystem* File, const char* Name, FRpVertexSrcDesc* Desc,
                                     FRpVertexStream* Stream)
{
    unsigned long* Buffer = nullptr;
    unsigned long Count = 0;
    bool bRead = false;
    if (!TriMeshReadPool(File, Name, 4, &Buffer, &Count, &bRead))
        return;
    if (bRead)
    {
        Desc->SrcCount = Count;
        Stream->IndexArray = Buffer;
    }
}

int FTriMeshArch::Load(IFileSystem* File, IRenderPipeline* InPipeline, IVertexBufferManager* InVbManager,
                       IMaterialLibrary* InLibrary, int bTristrips)
{
    File->ResolvePath(Name, "", 0x104);
    Flags &= 0xFFFFFFFE;
    if (File->OpenNode("Mesh") == 0)
        return -1;
    std::memset(&Vertices, 0, sizeof(Vertices));
    Vertices.Flags |= 0x4;

    if (File->OpenNode("Geometry") != 0)
    {
        // [ESP+0x18]: the pool pointer handed from each pool read to its stream.
        unsigned char* PoolBase = nullptr;
        unsigned char* Buffer = nullptr;
        unsigned long Count = 0;
        bool bRead = false;

        if (TriMeshReadPool(File, "Points", 0xC, &Buffer, &Count, &bRead))
        {
            PoolBase = Buffer;
            if (bRead)
                Vertices.Position.Count = Count;
        }
        Vertices.Position.Base = PoolBase;
        Vertices.SrcFvf = 0x2;
        Vertices.Position.Stride = 0xC;
        Vertices.Position.ByteSize = 0xC;
        TriMeshReadCornerIndices(File, "Point_indices", &Vertices, &Vertices.Position);

        if (TriMeshReadPool(File, "Vertex_normals", 0xC, &Buffer, &Count, &bRead) && bRead)
        {
            Vertices.Normal.Count = Count;
            PoolBase = Buffer;
        }
        if (Vertices.Normal.Count != 0)
        {
            Vertices.SrcFvf |= 0x10;
            Vertices.Normal.Base = PoolBase;
            Vertices.Normal.Stride = 0xC;
            Vertices.Normal.ByteSize = 0xC;
            TriMeshReadCornerIndices(File, "Vertex_normal_indices", &Vertices, &Vertices.Normal);
        }

        if (TriMeshReadPool(File, "UV0", 0x8, &Buffer, &Count, &bRead) && bRead)
        {
            Vertices.Tex[0].Count = Count;
            PoolBase = Buffer;
        }
        if (Vertices.Tex[0].Count != 0)
        {
            Vertices.SrcFvf = (Vertices.SrcFvf & 0xFFFFF1FF) | 0x100;
            Vertices.Tex[0].Base = PoolBase;
            Vertices.Tex[0].Stride = 0x8;
            Vertices.Tex[0].ByteSize = 0x8;
            TriMeshReadCornerIndices(File, "UV0_indices", &Vertices, &Vertices.Tex[0]);
        }

        // UV1 is allocated through the vector-new helper (FUN_06c22410, trivial 8-byte element).
        if (TriMeshReadPool(File, "UV1", 0x8, &Buffer, &Count, &bRead) && bRead)
        {
            Vertices.Tex[1].Count = Count;
            PoolBase = Buffer;
        }
        if (Vertices.Tex[1].Count != 0)
        {
            Vertices.SrcFvf = (Vertices.SrcFvf & 0xFFFFF2FF) | 0x200;
            Vertices.Tex[1].Base = PoolBase;
            Vertices.Tex[1].Stride = 0x8;
            Vertices.Tex[1].ByteSize = 0x8;
            TriMeshReadCornerIndices(File, "UV1_indices", &Vertices, &Vertices.Tex[1]);
        }

        // Color0 hands its buffer over in [ESP+0x14], not the pool slot.
        unsigned char* ColorBase = nullptr;
        if (TriMeshReadPool(File, "Color0", 0x4, &Buffer, &Count, &bRead) && bRead)
        {
            Vertices.Diffuse.Count = Count;
            ColorBase = Buffer;
        }
        if (Vertices.Diffuse.Count != 0)
        {
            Vertices.SrcFvf |= 0x40;
            Vertices.Diffuse.Base = ColorBase;
            Vertices.Diffuse.Stride = 0x4;
            Vertices.Diffuse.ByteSize = 0x4;
            TriMeshReadCornerIndices(File, "Color0_indices", &Vertices, &Vertices.Diffuse);
        }

        TriMeshReadArray(File, "UV0_anim_lookup", 2, &UvAnim.LookupCount,
                         &UvAnim.Lookup);
        if (UvAnim.LookupCount != 0)
        {
            TriMeshReadInt(File, "UV0_frame_count", &UvAnim.FrameCount);
            unsigned long FrameEntries = 0;
            TriMeshReadArray(File, "UV0_anim", 3, &FrameEntries, &UvAnim.Frames);
            TriMeshReadFloat(File, "UV0_fps", &UvAnim.Fps);
            TriMeshReadInt(File, "UV0_interpolate", &UvAnim.Interpolate);
            // FILD of the zero-extended FrameCount - 1 (an unsigned 32-bit value), FDIV, FSTP.
            UvAnim.Period = (float)(
                (double)((unsigned long)(UvAnim.FrameCount - 1)) /
                (double)UvAnim.Fps);
        }
        File->OpenNode("..");
    }

    if (File->OpenNode("Face_groups") != 0)
    {
        unsigned long GroupCount = 0;
        if (TriMeshReadInt(File, "Count", &GroupCount) >= 0)
        {
            for (unsigned long Index = 0; Index < GroupCount; ++Index)
            {
                char GroupName[0x2000];
                _snprintf(GroupName, sizeof(GroupName), "Group%d", (int)Index);
                if (File->OpenNode(GroupName) == 0)
                    continue;
                FTriMeshGroup* Group = TriMeshGroupNew();
                char MaterialName[0x40];
                if (TriMeshReadString(File, "Material_name", 0x40, MaterialName) < 0)
                    RendCompFdumpAt(kFdumpWarning, G_FileTriMeshArchetype, 0x61A, "No Material_name found");
                else
                {
                    _strlwr(MaterialName);
                    Group->MaterialCrc = DACOM_CRC::GetCRC32(MaterialName);
                }
                TriMeshReadArray(File, "Tristrip_indices", 1, &Group->IndexCount,
                                 &Group->Indices);
                if (Group->Indices != nullptr)
                {
                    Group->Type = D3DPT_TRIANGLESTRIP;
                    if (bTristrips == 0)
                        Group->ConvertStripToList();
                }
                else
                {
                    Group->Type = D3DPT_TRIANGLELIST;
                    TriMeshReadArray(File, "Face_indices", 1, &Group->IndexCount,
                                     &Group->Indices);
                }
                TriMeshReadArray(File, "Edge_indices", 1, &Group->EdgeCount,
                                 &Group->EdgeIndices);
                unsigned long AngleCount = 0;
                TriMeshReadArray(File, "Edge_angles", 2, &AngleCount, &Group->EdgeAngles);
                Group->Rebase();
                Group->Vertices = &Vertices;
                Group->SharedVertices = Group->Vertices;
                Groups.insert(Groups.end(), Group);
                Cursor = Groups.size();
                File->OpenNode("..");
            }
        }
        if (MaterialLibrary != InLibrary)
        {
            if (MaterialLibrary != nullptr)
            {
                MaterialLibrary->Release();
                MaterialLibrary = nullptr;
            }
            MaterialLibrary = InLibrary;
            if (InLibrary != nullptr)
                InLibrary->AddRef();
        }
        if (Pipeline != InPipeline)
        {
            if (Pipeline != nullptr)
            {
                Pipeline->Release();
                Pipeline = nullptr;
            }
            Pipeline = InPipeline;
            if (InPipeline != nullptr)
                InPipeline->AddRef();
        }
        if (VbManager != InVbManager)
        {
            if (VbManager != nullptr)
            {
                VbManager->Release();
                VbManager = nullptr;
            }
            VbManager = InVbManager;
            if (InVbManager != nullptr)
                InVbManager->AddRef();
        }
        HookFacet.HookUpMaterials();
        File->OpenNode("..");
    }
    File->OpenNode("..");
    Flags |= 0x1;
    return 0;
}

// ---------------------------------------------------------------------------------------
// Clone (FUN_06c24aa0)
// ---------------------------------------------------------------------------------------

int FTriMeshArch::Clone(FTriMeshArch** Out)
{
    FTriMeshArch* Copy = new FTriMeshArch();
    *Out = Copy;
    TriMeshDescCopy(&Vertices, &Copy->Vertices);
    for (size_t Index = 0; Index < Groups.size(); ++Index)
    {
        FTriMeshGroup* Group = TriMeshGroupCopy(Groups[Index]);
        if (Group->Vertices == Group->SharedVertices)
        {
            Group->Vertices = &Copy->Vertices;
            Group->SharedVertices = Group->Vertices;
        }
        else
            Group->SharedVertices = &Copy->Vertices;
        Copy->Groups.insert(Copy->Groups.end(), 1, Group);
    }
    Copy->Flags = Flags;
    Copy->Center[0] = Center[0];
    Copy->Center[1] = Center[1];
    Copy->Center[2] = Center[2];
    for (int Index = 0; Index < 6; ++Index)
        Copy->Box[Index] = Box[Index];
    Copy->SphereCenter[0] = SphereCenter[0];
    Copy->SphereCenter[1] = SphereCenter[1];
    Copy->SphereCenter[2] = SphereCenter[2];
    Copy->SphereRadius = SphereRadius;
    std::strncpy(Copy->Name, Name, 0x104);
    Copy->Cursor = Copy->Groups.size();
    return 0;
}

// ---------------------------------------------------------------------------------------
// Channel locking (IMesh Slot20 / Slot21)
// ---------------------------------------------------------------------------------------

// The per-vertex index array a channel locks (FUN_06c25e30 / FUN_06c26000): 1 position, 8 normal,
// 2 tex0, 4 tex1, 0x10 diffuse; 0x4000..0x20000 extra stream 0..3. Null for anything else.
static const unsigned long** TriMeshChannelIndexArray(FTriMeshGroup* Group, unsigned long Channel)
{
    FRpVertexSrcDesc* Desc = Group->Vertices;
    switch (Channel)
    {
    case 0x1: return &Desc->Position.IndexArray;
    case 0x8: return &Desc->Normal.IndexArray;
    case 0x2: return &Desc->Tex[0].IndexArray;
    case 0x4: return &Desc->Tex[1].IndexArray;
    case 0x10: return &Desc->Diffuse.IndexArray;
    case 0x4000: return &Group->ExtraStreams[0].IndexArray;
    case 0x8000: return &Group->ExtraStreams[1].IndexArray;
    case 0x10000: return &Group->ExtraStreams[2].IndexArray;
    case 0x20000: return &Group->ExtraStreams[3].IndexArray;
    default: return nullptr;
    }
}

int FTriMeshArch::LockChannel(unsigned long Channel)
{
    FTriMeshGroup* Group = Groups[Cursor];
    if (Channel == 0x4000 || Channel == 0x8000 || Channel == 0x10000 || Channel == 0x20000)
    {
        if (Group->ExtraStreams == nullptr)
            Group->AllocateExtraStreams();
    }
    // TriMeshChannelIndexArray is non-null exactly for the nine channels 06c25e3e..06c25f34
    // map, and for those TriMeshLockedSlot gives the slot the LEA EBX picks (never 5).
    const unsigned long** IndexArray = TriMeshChannelIndexArray(Group, Channel);
    if (IndexArray == nullptr)
        return -1;
    const int Slot = TriMeshLockedSlot(Channel);
    const unsigned long* Source = *IndexArray;
    if (Source == nullptr)
        return -1;
    unsigned short* Copy = (unsigned short*)new unsigned char[Group->IndexCount * 2];
    Locked[Slot] = Copy;
    const unsigned long* Base = Source + Group->MinIndex;
    for (unsigned long Index = 0; Index < Group->IndexCount; ++Index)
        Copy[Index] = (unsigned short)(Base[Group->Indices[Index]]);
    return 0;
}

int FTriMeshArch::UnlockChannel(unsigned long Channel)
{
    FTriMeshGroup* Group = Groups[Cursor];
    unsigned long* Dest = nullptr;
    unsigned short** Slot = nullptr;
    const bool bExtra = (Channel == 0x4000 || Channel == 0x8000 || Channel == 0x10000 || Channel == 0x20000);
    if (!bExtra || Group->ExtraStreams != nullptr)
    {
        // Non-null exactly for the nine mapped channels (see LockChannel).
        const unsigned long** IndexArray = TriMeshChannelIndexArray(Group, Channel);
        if (IndexArray != nullptr)
        {
            Dest = (unsigned long*)*IndexArray;
            Slot = &Locked[TriMeshLockedSlot(Channel)];
        }
    }
    if (EditMode == 2)
    {
        Group->MinIndex = 0;
        FTriMeshGroup* Edited = Groups[Cursor];
        const unsigned short* Written = *Slot;
        for (unsigned long Index = 0; Index < Edited->IndexCount; ++Index)
            Dest[Edited->Indices[Index]] = Written[Index];
        Groups[Cursor]->Rebase();
    }
    delete[] (const unsigned char*)(*Slot);
    *Slot = nullptr;
    return 0;
}

// FUN_06c261b0: no edge angles -> -1. Locked[5] (+0x204) = new((EdgeCount >> 1) * 2) holding
// 0, 1, 2, ... 0.
int FTriMeshArch::LockEdgeAngles()
{
    FTriMeshGroup* Group = Groups[Cursor];
    if (Group->EdgeAngles == nullptr)
        return -1;
    const int Count = (int)(Group->EdgeCount >> 1);
    Locked[5] = (unsigned short*)new unsigned char[(unsigned long)Count * 2];
    for (int Index = 0; Index < Count; ++Index)
        Locked[5][Index] = (unsigned short)Index;
    return 0;
}

// FUN_06c26200: Locked[5] deleted and cleared. 0.
int FTriMeshArch::UnlockEdgeAngles()
{
    delete[] (const unsigned char*)Locked[5];
    Locked[5] = nullptr;
    return 0;
}

// ---------------------------------------------------------------------------------------
// Counts, bounds
// ---------------------------------------------------------------------------------------

// FUN_06c26220: triangles over every group (a strip counts IndexCount - 2, a list IndexCount / 3).
int FTriMeshArch::TriangleCount()
{
    unsigned long Total = 0;
    for (size_t Index = 0; Index < Groups.size(); ++Index)
    {
        const FTriMeshGroup* Group = Groups[Index];
        if (Group->Type == D3DPT_TRIANGLESTRIP)
            Total += Group->IndexCount - 2;
        else
            Total += Group->IndexCount / 3;
    }
    return (int)Total;
}

// FUN_06c26260: vertices over every group -- a private descriptor's SrcCount each time, the
// shared descriptor's once.
int FTriMeshArch::VertexCount()
{
    unsigned char bSharedCounted = 0;
    unsigned long Total = 0;
    for (size_t Index = 0; Index < Groups.size(); ++Index)
    {
        const FTriMeshGroup* Group = Groups[Index];
        if (Group->Vertices != Group->SharedVertices)
            Total += Group->Vertices->SrcCount;
        else if (bSharedCounted == 0)
        {
            Total += Group->SharedVertices->SrcCount;
            bSharedCounted = 1;
        }
    }
    return (int)Total;
}

// FUN_06c265f0 (thiscall, RET 4): *Out = +0x1E4; AL = (+0x1E4 != 0).
bool FTriMeshArch::GetPrebuiltListing(void** Out)
{
    *Out = Field_0x1E4;
    return Field_0x1E4 != nullptr;
}

int FTriMeshArch::GetBox(float* Out)
{
    if ((Flags & 0x1) == 0)
        return -1;
    if ((Flags & 0x4) == 0)
    {
        for (int Index = 0; Index < 6; ++Index)
            Box[Index] = 0.0f;
        const unsigned long Count = Vertices.Position.Count;
        if (Count != 0)
        {
            const float* Point = (const float*)Vertices.Position.Base;
            Box[1] = Point[0];
            Box[0] = Point[0];
            Box[3] = Point[1];
            Box[2] = Point[1];
            Box[5] = Point[2];
            Box[4] = Point[2];
            Point += 3;
            for (unsigned long Index = 1; Index < Count; ++Index)
            {
                if (Point[0] > Box[0])
                    Box[0] = Point[0];
                else if (Point[0] < Box[1])
                    Box[1] = Point[0];
                if (Point[1] > Box[2])
                    Box[2] = Point[1];
                else if (Point[1] < Box[3])
                    Box[3] = Point[1];
                if (Point[2] > Box[4])
                    Box[4] = Point[2];
                else if (Point[2] < Box[5])
                    Box[5] = Point[2];
                Point += 3;
            }
            Flags |= 0x4;
        }
    }
    for (int Index = 0; Index < 6; ++Index)
        Out[Index] = Box[Index];
    return 0;
}

int FTriMeshArch::GetCenter(float* Out)
{
    if ((Flags & 0x1) == 0)
        return -1;
    if ((Flags & 0x2) != 0)
    {
        Out[0] = Center[0];
        Out[1] = Center[1];
        Out[2] = Center[2];
        return 0;
    }
    Center[2] = 0.0f;
    Center[1] = 0.0f;
    Center[0] = 0.0f;
    const unsigned long Count = Vertices.Position.Count;
    if (Count != 0)
    {
        const float* Point = (const float*)Vertices.Position.Base;
        for (unsigned long Index = 0; Index < Count; ++Index)
        {
            Center[0] = Center[0] + Point[0];
            Center[1] = Point[1] + Center[1];
            Center[2] = Point[2] + Center[2];
        }
        // FILD of the zero-extended count, FDIVR 1.0f (DAT_06c361ec); held on the x87 stack.
        const double Scale = 1.0 / (double)Count;
        Center[0] = (float)(Scale * Center[0]);
        Center[1] = (float)(Scale * Center[1]);
        Center[2] = (float)(Scale * Center[2]);
        Flags |= 0x8;
    }
    Out[0] = Center[0];
    Out[1] = Center[1];
    Out[2] = Center[2];
    return 0;
}

int FTriMeshArch::GetSphere(float* OutCenter, float* OutRadius)
{
    if ((Flags & 0x1) == 0)
        return -1;
    if ((Flags & 0x2) != 0)
    {
        OutCenter[0] = SphereCenter[0];
        OutCenter[1] = SphereCenter[1];
        OutCenter[2] = SphereCenter[2];
        *OutRadius = SphereRadius;
        return 0;
    }
    SphereCenter[2] = 0.0f;
    SphereCenter[1] = 0.0f;
    SphereCenter[0] = 0.0f;
    SphereRadius = 0.0f;
    const unsigned long Count = Vertices.Position.Count;
    if (Count != 0)
    {
        const float* Points = (const float*)Vertices.Position.Base;
        unsigned long MinX = 0;
        unsigned long MaxX = 0;
        unsigned long MinY = 0;
        unsigned long MaxY = 0;
        unsigned long MinZ = 0;
        unsigned long MaxZ = 0;
        for (unsigned long Index = 1; Index < Count; ++Index)
        {
            const float* Point = Points + Index * 3;
            // 06c25997: TEST AH,0x5 / JP -- fall-through (strictly less) takes the new minimum.
            if (Point[0] < Points[MinX * 3])
                MinX = Index;
            else if (Point[0] > Points[MaxX * 3])
                MaxX = Index;
            if (Point[1] < Points[MinY * 3 + 1])
                MinY = Index;
            else if (Point[1] > Points[MaxY * 3 + 1])
                MaxY = Index;
            if (Point[2] < Points[MinZ * 3 + 2])
                MinZ = Index;
            else if (Point[2] > Points[MaxZ * 3 + 2])
                MaxZ = Index;
        }
        const float ExtentX = Points[MaxX * 3] - Points[MinX * 3];
        const float ExtentY = Points[MaxY * 3 + 1] - Points[MinY * 3 + 1];
        const float ExtentZ = Points[MaxZ * 3 + 2] - Points[MinZ * 3 + 2];
        const float SumX = Points[MinX * 3] + Points[MaxX * 3];
        const float SumY = Points[MaxY * 3 + 1] + Points[MinY * 3 + 1];
        const float SumZ = Points[MaxZ * 3 + 2] + Points[MinZ * 3 + 2];
        // FUN_06c25d10 (Out = In * 0.5f), each product stored as float.
        SphereCenter[0] = 0.5f * SumX;
        SphereCenter[1] = 0.5f * SumY;
        SphereCenter[2] = 0.5f * SumZ;
        // 06c25acf..06c25b35: the largest extent, ties going to the later operand (TEST AH,0x41 /
        // JNZ takes the second value when the first is not greater).
        float Largest = 0.0f;
        const float LargerXY = (ExtentX > ExtentY) ? ExtentX : ExtentY;
        if (LargerXY > ExtentZ)
            Largest = (ExtentX > ExtentY) ? ExtentX : ExtentY;
        else
            Largest = ExtentZ;
        const double Radius = (double)Largest * 0.5;
        SphereRadius = (float)Radius;
        float RadiusSquared = (float)(Radius * Radius);
        for (unsigned long Index = 0; Index < Count; ++Index)
        {
            const float* Point = Points + Index * 3;
            // FUN_06c25ce0: Delta = Point - Centre, stored as float.
            const float DeltaX = Point[0] - SphereCenter[0];
            const float DeltaY = Point[1] - SphereCenter[1];
            const float DeltaZ = Point[2] - SphereCenter[2];
            const double DistanceSquared = (double)DeltaY * DeltaY +
                                           (double)DeltaZ * DeltaZ +
                                           (double)DeltaX * DeltaX;
            // 06c25b83: FCOM r^2 / TEST AH,0x41 / JNZ skips a point that is not outside.
            if (!(DistanceSquared > (double)RadiusSquared))
                continue;
            const double Distance = std::sqrt(DistanceSquared);
            const float DistanceFloat = (float)Distance;
            const double NewRadius = (Distance + (double)SphereRadius) * 0.5;
            SphereRadius = (float)NewRadius;
            RadiusSquared = (float)(NewRadius * NewRadius);
            const float Toward = (float)((double)DistanceFloat - NewRadius);
            // FUN_06c25d10 twice (Point * (d - r'), Centre * r'), summed, FUN_06c25cc0, then each
            // component times 1 / d (FLD 1.0f, FDIV d) and FUN_06c25cc0 again.
            const float PartX = Toward * Point[0];
            const float PartY = Toward * Point[1];
            const float PartZ = Toward * Point[2];
            const float KeepX = SphereRadius * SphereCenter[0];
            const float KeepY = SphereRadius * SphereCenter[1];
            const float KeepZ = SphereRadius * SphereCenter[2];
            const float SumCenterX = PartX + KeepX;
            const float SumCenterY = PartY + KeepY;
            const float SumCenterZ = PartZ + KeepZ;
            const double Inverse = 1.0 / (double)DistanceFloat;
            SphereCenter[0] = (float)((double)SumCenterX * Inverse);
            SphereCenter[1] = (float)((double)SumCenterY * Inverse);
            SphereCenter[2] = (float)((double)SumCenterZ * Inverse);
        }
        Flags |= 0x2;
    }
    OutCenter[0] = SphereCenter[0];
    OutCenter[1] = SphereCenter[1];
    OutCenter[2] = SphereCenter[2];
    *OutRadius = SphereRadius;
    return 0;
}

// ---------------------------------------------------------------------------------------
// Vertex compaction (private / shared descriptors)
// ---------------------------------------------------------------------------------------

// FUN_06c26c40 (cdecl; map, count, sentinel): every entry equal to the sentinel is renumbered
// 0, 1, 2, ... in order.
static void TriMeshDenseMap(long* Map, long Count, long Sentinel)
{
    long Next = 0;
    for (long Index = 0; Index < Count; ++Index)
    {
        if (Map[Index] == Sentinel)
        {
            Map[Index] = Next;
            Next += 1;
        }
    }
}

static long* TriMeshNewMap(unsigned long Count)
{
    long* Map = (long*)new unsigned char[Count * 4];
    std::memset(Map, 0xFF, Count * 4);
    return Map;
}

static void TriMeshCompactStream(const FRpVertexStream* Source, long SourceVertexCount, FRpVertexStream* Dest,
                                 unsigned long DestVertexCount, unsigned long DestElementCount,
                                 const long* VertexMap, const long* ElementMap)
{
    Dest->ByteSize = Source->ByteSize;
    Dest->Stride = Source->ByteSize;
    Dest->IndexArray = (unsigned long*)new unsigned char[DestVertexCount * 4];
    Dest->Count = DestElementCount;
    Dest->Base = new unsigned char[Dest->Stride * DestElementCount];
    const unsigned long* SourceIndices = Source->IndexArray;
    unsigned long* DestIndices = (unsigned long*)Dest->IndexArray;
    for (long Vertex = 0; Vertex < SourceVertexCount; ++Vertex)
    {
        const long NewVertex = VertexMap[Vertex];
        if (NewVertex >= 0)
            DestIndices[NewVertex] = (unsigned long)(ElementMap[SourceIndices[Vertex]]);
    }
    for (unsigned long Element = 0; Element < Source->Count; ++Element)
    {
        const long NewElement = ElementMap[Element];
        if (NewElement >= 0)
            std::memcpy((unsigned char*)Dest->Base + Dest->Stride * NewElement,
                        Source->Base + Source->Stride * Element, Source->ByteSize);
    }
}

static int TriMeshCompactInto(FTriMeshGroup** List, int GroupCount, const FRpVertexSrcDesc* Source,
                              FRpVertexSrcDesc* Dest)
{
    long* VertexMap = TriMeshNewMap(Source->SrcCount);
    long* PositionMap = nullptr;
    if (((unsigned char)Source->SrcFvf & 0x2) != 0)
        PositionMap = TriMeshNewMap(Source->Position.Count);
    long* NormalMap = nullptr;
    if (((unsigned char)Source->SrcFvf & 0x10) != 0)
        NormalMap = TriMeshNewMap(Source->Normal.Count);
    long* Tex0Map = nullptr;
    if ((Source->SrcFvf & 0xF00) >= 0x100)
        Tex0Map = TriMeshNewMap(Source->Tex[0].Count);
    long* Tex1Map = nullptr;
    if ((Source->SrcFvf & 0xF00) >= 0x200)
        Tex1Map = TriMeshNewMap(Source->Tex[1].Count);
    long* ColorMap = nullptr;
    if (((unsigned char)Source->SrcFvf & 0x40) != 0)
        ColorMap = TriMeshNewMap(Source->Diffuse.Count);

    unsigned long VerticesUsed = 0;
    unsigned long PositionsUsed = 0;
    unsigned long NormalsUsed = 0;
    unsigned long Tex0Used = 0;
    unsigned long Tex1Used = 0;
    unsigned long ColorsUsed = 0;
    const unsigned long* PositionIndices = Source->Position.IndexArray;
    const unsigned long* NormalIndices = Source->Normal.IndexArray;
    const unsigned long* Tex0Indices = Source->Tex[0].IndexArray;
    const unsigned long* Tex1Indices = Source->Tex[1].IndexArray;
    const unsigned long* ColorIndices = Source->Diffuse.IndexArray;
    for (int GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
    {
        FTriMeshGroup* Group = List[GroupIndex];
        Group->Unrebase();
        for (unsigned long Corner = 0; Corner < Group->IndexCount; ++Corner)
        {
            const unsigned short Vertex = Group->Indices[Corner];
            if (VertexMap[Vertex] != -1)
                continue;
            VerticesUsed += 1;
            VertexMap[Vertex] = -2;
            if (PositionMap != nullptr && PositionMap[PositionIndices[Vertex]] == -1)
            {
                PositionMap[PositionIndices[Vertex]] = -2;
                PositionsUsed += 1;
            }
            if (NormalMap != nullptr && NormalMap[NormalIndices[Vertex]] == -1)
            {
                NormalMap[NormalIndices[Vertex]] = -2;
                NormalsUsed += 1;
            }
            if (Tex0Map != nullptr && Tex0Map[Tex0Indices[Vertex]] == -1)
            {
                Tex0Map[Tex0Indices[Vertex]] = -2;
                Tex0Used += 1;
            }
            if (Tex1Map != nullptr && Tex1Map[Tex1Indices[Vertex]] == -1)
            {
                Tex1Map[Tex1Indices[Vertex]] = -2;
                Tex1Used += 1;
            }
            if (ColorMap != nullptr && ColorMap[ColorIndices[Vertex]] == -1)
            {
                ColorMap[ColorIndices[Vertex]] = -2;
                ColorsUsed += 1;
            }
        }
    }

    if (VerticesUsed == Source->SrcCount && PositionsUsed == Source->Position.Count &&
        NormalsUsed == Source->Normal.Count && Tex0Used == Source->Tex[0].Count &&
        Tex1Used == Source->Tex[1].Count && ColorsUsed == Source->Diffuse.Count)
    {
        delete[] (const unsigned char*)VertexMap;
        delete[] (const unsigned char*)PositionMap;
        delete[] (const unsigned char*)NormalMap;
        delete[] (const unsigned char*)Tex0Map;
        delete[] (const unsigned char*)Tex1Map;
        delete[] (const unsigned char*)ColorMap;
        for (int GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
            List[GroupIndex]->Rebase();
        return -1;
    }

    Dest->Flags = Source->Flags;
    Dest->SrcFvf = Source->SrcFvf;
    TriMeshDenseMap(VertexMap, (long)Source->SrcCount, -2);
    TriMeshDenseMap(PositionMap, (long)(Source->Position.Count), -2);
    TriMeshDenseMap(NormalMap, (long)(Source->Normal.Count), -2);
    TriMeshDenseMap(Tex0Map, (long)(Source->Tex[0].Count), -2);
    TriMeshDenseMap(Tex1Map, (long)(Source->Tex[1].Count), -2);
    TriMeshDenseMap(ColorMap, (long)(Source->Diffuse.Count), -2);
    for (int GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
    {
        FTriMeshGroup* Group = List[GroupIndex];
        for (unsigned long Corner = 0; Corner < Group->IndexCount; ++Corner)
            Group->Indices[Corner] = (unsigned short)(VertexMap[Group->Indices[Corner]]);
        for (unsigned long Edge = 0; Edge < Group->EdgeCount; ++Edge)
            Group->EdgeIndices[Edge] = (unsigned short)(PositionMap[Group->EdgeIndices[Edge]]);
        Group->Rebase();
    }
    Dest->SrcCount = VerticesUsed;
    const long SourceCount = (long)Source->SrcCount;
    if (PositionMap != nullptr)
        TriMeshCompactStream(&Source->Position, SourceCount, &Dest->Position, VerticesUsed, PositionsUsed,
                             VertexMap, PositionMap);
    if (NormalMap != nullptr)
        TriMeshCompactStream(&Source->Normal, SourceCount, &Dest->Normal, VerticesUsed, NormalsUsed,
                             VertexMap, NormalMap);
    if (Tex0Map != nullptr)
        TriMeshCompactStream(&Source->Tex[0], SourceCount, &Dest->Tex[0], VerticesUsed, Tex0Used,
                             VertexMap, Tex0Map);
    if (Tex1Map != nullptr)
        TriMeshCompactStream(&Source->Tex[1], SourceCount, &Dest->Tex[1], VerticesUsed, Tex1Used,
                             VertexMap, Tex1Map);
    if (ColorMap != nullptr)
        TriMeshCompactStream(&Source->Diffuse, SourceCount, &Dest->Diffuse, VerticesUsed, ColorsUsed,
                             VertexMap, ColorMap);
    delete[] (const unsigned char*)VertexMap;
    delete[] (const unsigned char*)PositionMap;
    delete[] (const unsigned char*)NormalMap;
    delete[] (const unsigned char*)Tex0Map;
    delete[] (const unsigned char*)Tex1Map;
    delete[] (const unsigned char*)ColorMap;
    return 0;
}

// formerly TriMeshGroupPrivatize
void FTriMeshGroup::PrivatizeVertices()
{
    if (this->Vertices != this->SharedVertices)
        return;
    this->Vertices = TriMeshDescNew();
    FTriMeshGroup* List[1];
    List[0] = this;
    if (TriMeshCompactInto(List, 1, this->SharedVertices, this->Vertices) == 1)
        TriMeshDescCopy(this->SharedVertices, this->Vertices);
}

// FUN_06c26610: in EditMode 2 the edited group is made private (FUN_06c27aa0). 0.
int FTriMeshArch::PrivatizeCursorGroup()
{
    if (EditMode == 2)
        Groups[Cursor]->PrivatizeVertices();
    return 0;
}

int FTriMeshArch::ShareGroupVertices(bool bAll)
{
    const size_t Size = (Groups.data() == nullptr) ? 0 : Groups.size();
    FTriMeshGroup** Shared = (FTriMeshGroup**)new unsigned char[Size * 4];
    int SharedCount = 0;
    for (size_t Index = 0; Index < Groups.size(); ++Index)
    {
        FTriMeshGroup* Group = Groups[Index];
        if (Group->Vertices == Group->SharedVertices)
        {
            Shared[SharedCount] = Group;
            SharedCount += 1;
        }
    }
    if (SharedCount <= 0)
        TriMeshDescFree(&Vertices);
    else if ((Groups.data() != nullptr && (size_t)SharedCount < Groups.size()) || bAll)
    {
        FRpVertexSrcDesc Compacted;
        std::memset(&Compacted, 0, sizeof(Compacted));
        if (TriMeshCompactInto(Shared, SharedCount, &Vertices, &Compacted) == 0)
        {
            TriMeshDescFree(&Vertices);
            std::memcpy(&Vertices, &Compacted, sizeof(Vertices));
        }
    }
    delete[] (const unsigned char*)Shared;
    return 0;
}

// Copies Count elements of Bytes bytes from Source (at SourceStride) to Dest + (Offset + i) *
// DestStride -- the element loops of FUN_06c25140.
static void TriMeshAppendElements(unsigned char* Dest, unsigned long DestStride, unsigned long Offset,
                                  const unsigned char* Source, unsigned long SourceStride, unsigned long Count,
                                  unsigned long Bytes)
{
    for (unsigned long Element = 0; Element < Count; ++Element)
        std::memcpy(Dest + (Element + Offset) * DestStride, Source + SourceStride * Element, Bytes);
}

// The index loops of FUN_06c25140: Dest[VertexOffset + v] = Source[v] + ElementOffset.
static void TriMeshAppendIndices(unsigned long* Dest, unsigned long VertexOffset, const unsigned long* Source,
                                 unsigned long VertexCount, unsigned long ElementOffset)
{
    unsigned long* Out = Dest + VertexOffset;
    const unsigned long* In = Source;
    for (unsigned long Vertex = 0; Vertex < VertexCount; ++Vertex)
        Out[Vertex] = In[Vertex] + ElementOffset;
}

int FTriMeshArch::CompactVertices()
{
    FTriMeshGroup** Begin = Groups.data();
    const size_t Count = Groups.size();
    FTriMeshGroup* First = Begin[0];
    unsigned char bSame = 1;
    unsigned long SumVertices = 0;
    unsigned long SumPositions = 0;
    unsigned long SumNormals = 0;
    unsigned long SumTex0 = 0;
    unsigned long SumTex1 = 0;
    unsigned long SumColors = 0;
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const FRpVertexSrcDesc* Desc = Begin[Index]->Vertices;
        if (Desc == &Vertices)
            return 0;
        if (First->Vertices->SrcFvf == Desc->SrcFvf && First->Vertices->Flags == Desc->Flags)
        {
            const unsigned long Fvf = Desc->SrcFvf;
            SumVertices += Desc->SrcCount;
            if (((unsigned char)Fvf & 0x2) != 0)
                SumPositions += Desc->Position.Count;
            if (((unsigned char)Fvf & 0x10) != 0)
                SumNormals += Desc->Normal.Count;
            if ((Fvf & 0xF00) >= 0x100)
                SumTex0 += Desc->Tex[0].Count;
            if ((Fvf & 0xF00) >= 0x200)
                SumTex1 += Desc->Tex[1].Count;
            if (((unsigned char)Fvf & 0x40) != 0)
                SumColors += Desc->Diffuse.Count;
        }
        else
            bSame = 0;
    }
    TriMeshDescFree(&Vertices);
    if (bSame == 0)
        return 0;
    if (Begin != nullptr && Count == 1)
    {
        std::memcpy(&Vertices, Begin[0]->Vertices, sizeof(Vertices));
        delete Begin[0]->Vertices;
        Begin[0]->Vertices = &Vertices;
        return 0;
    }

    const FRpVertexSrcDesc* FirstDesc = First->Vertices;
    Vertices.Flags = FirstDesc->Flags;
    Vertices.SrcFvf = FirstDesc->SrcFvf;
    Vertices.SrcCount = SumVertices;
    Vertices.Position.Count = SumPositions;
    Vertices.Position.ByteSize = FirstDesc->Position.ByteSize;
    Vertices.Position.Stride = FirstDesc->Position.Stride;
    Vertices.Position.IndexArray = (unsigned long*)new unsigned char[SumVertices * 4];
    Vertices.Position.Base = new unsigned char[FirstDesc->Position.Stride * SumPositions];
    if (SumNormals != 0)
    {
        Vertices.Normal.Count = SumNormals;
        Vertices.Normal.ByteSize = FirstDesc->Normal.ByteSize;
        Vertices.Normal.Stride = FirstDesc->Normal.Stride;
        Vertices.Normal.IndexArray = (unsigned long*)new unsigned char[SumVertices * 4];
        Vertices.Normal.Base = new unsigned char[FirstDesc->Normal.Stride * SumNormals];
    }
    if (SumTex0 != 0)
    {
        Vertices.Tex[0].Count = SumTex0;
        Vertices.Tex[0].ByteSize = FirstDesc->Tex[0].ByteSize;
        Vertices.Tex[0].Stride = FirstDesc->Tex[0].Stride;
        Vertices.Tex[0].IndexArray = (unsigned long*)new unsigned char[SumVertices * 4];
        Vertices.Tex[0].Base = new unsigned char[FirstDesc->Tex[0].Stride * SumTex0];
    }
    if (SumTex1 != 0)
    {
        Vertices.Tex[1].Count = SumTex1;
        Vertices.Tex[1].ByteSize = FirstDesc->Tex[1].ByteSize;
        Vertices.Tex[1].Stride = FirstDesc->Tex[1].Stride;
        Vertices.Tex[1].IndexArray = (unsigned long*)new unsigned char[SumVertices * 4];
        Vertices.Tex[1].Base = new unsigned char[FirstDesc->Tex[1].Stride * SumTex1];
    }
    if (SumColors != 0)
    {
        Vertices.Diffuse.Count = SumColors;
        Vertices.Diffuse.ByteSize = FirstDesc->Diffuse.ByteSize;
        Vertices.Diffuse.Stride = FirstDesc->Diffuse.Stride;
        Vertices.Diffuse.IndexArray = (unsigned long*)new unsigned char[SumVertices * 4];
        Vertices.Diffuse.Base = new unsigned char[FirstDesc->Diffuse.Stride * SumColors];
    }

    unsigned long DoneVertices = 0;
    unsigned long DonePositions = 0;
    unsigned long DoneNormals = 0;
    unsigned long DoneTex0 = 0;
    unsigned long DoneTex1 = 0;
    unsigned long DoneColors = 0;
    for (size_t Index = 0; Index < Count; ++Index)
    {
        FTriMeshGroup* Group = Begin[Index];
        Group->Unrebase();
        for (unsigned long Corner = 0; Corner < Group->IndexCount; ++Corner)
            Group->Indices[Corner] = (unsigned short)(
                Group->Indices[Corner] + (unsigned short)DoneVertices);
        FRpVertexSrcDesc* Desc = Group->Vertices;
        unsigned long* PositionOut = (unsigned long*)Vertices.Position.IndexArray;
        TriMeshAppendIndices(PositionOut, DoneVertices, Desc->Position.IndexArray, Desc->SrcCount, DonePositions);
        TriMeshAppendElements((unsigned char*)Vertices.Position.Base, Vertices.Position.Stride,
                              DonePositions, Desc->Position.Base, Desc->Position.Stride, Desc->Position.Count, 0xC);
        if (SumNormals != 0)
        {
            TriMeshAppendIndices((unsigned long*)Vertices.Normal.IndexArray, DoneVertices,
                                 Desc->Normal.IndexArray, Desc->SrcCount, DoneNormals);
            TriMeshAppendElements((unsigned char*)Vertices.Normal.Base, Vertices.Normal.Stride,
                                  DoneNormals, Desc->Normal.Base, Desc->Normal.Stride, Desc->Normal.Count, 0xC);
        }
        if (SumTex0 != 0)
        {
            TriMeshAppendIndices((unsigned long*)Vertices.Tex[0].IndexArray, DoneVertices,
                                 Desc->Tex[0].IndexArray, Desc->SrcCount, DoneTex0);
            TriMeshAppendElements((unsigned char*)(Vertices.Tex[0].Base), Vertices.Tex[0].Stride,
                                  DoneTex0, Desc->Tex[0].Base, Desc->Tex[0].Stride, Desc->Tex[0].Count,
                                  Vertices.Tex[0].ByteSize);
        }
        if (SumTex1 != 0)
        {
            TriMeshAppendIndices((unsigned long*)Vertices.Tex[1].IndexArray, DoneVertices,
                                 Desc->Tex[1].IndexArray, Desc->SrcCount, DoneTex1);
            TriMeshAppendElements((unsigned char*)(Vertices.Tex[1].Base), Vertices.Tex[1].Stride,
                                  DoneTex1, Desc->Tex[1].Base, Desc->Tex[1].Stride, Desc->Tex[1].Count,
                                  Vertices.Tex[1].ByteSize);
        }
        if (SumColors != 0)
        {
            TriMeshAppendIndices((unsigned long*)Vertices.Diffuse.IndexArray, DoneVertices,
                                 Desc->Diffuse.IndexArray, Desc->SrcCount, DoneColors);
            TriMeshAppendElements((unsigned char*)Vertices.Diffuse.Base, Vertices.Diffuse.Stride,
                                  DoneColors, Desc->Diffuse.Base, Desc->Diffuse.Stride, Desc->Diffuse.Count,
                                  Vertices.Diffuse.ByteSize);
        }
        DoneVertices += Desc->SrcCount;
        DoneNormals += Desc->Normal.Count;
        DoneTex0 += Desc->Tex[0].Count;
        DoneTex1 += Desc->Tex[1].Count;
        DonePositions += Desc->Position.Count;
        DoneColors += Desc->Diffuse.Count;
        TriMeshDescFree(Desc);
        delete Desc;
        Group->Vertices = &Vertices;
        Group->Rebase();
    }
    return 0;
}

// ---------------------------------------------------------------------------------------
// Plane split (IRenderComponent Slot15)
// ---------------------------------------------------------------------------------------

// formerly TriMeshClassifyTriangle
unsigned long FTriMeshGroup::ClassifyTriangle(int Triangle, const float* Plane, float PlaneD) const
{
    const FRpVertexSrcDesc* Desc = this->Vertices;
    const unsigned long* PositionIndices = Desc->Position.IndexArray;
    const unsigned long Stride = Desc->Position.Stride;
    const unsigned char* Base = Desc->Position.Base;
    const unsigned short* Corner = this->Indices + Triangle * 3;
    const float* P0 = (const float*)(Base + PositionIndices[Corner[0]] * Stride);
    const float* P1 = (const float*)(Base + PositionIndices[Corner[1]] * Stride);
    const float* P2 = (const float*)(Base + PositionIndices[Corner[2]] * Stride);
    const double NegD = -(double)PlaneD;
    const double Dot0 = (double)P0[2] * Plane[2] + (double)P0[1] * Plane[1] +
                        (double)Plane[0] * P0[0];
    const double Dot1 = (double)P1[2] * Plane[2] + (double)P1[1] * Plane[1] +
                        (double)Plane[0] * P1[0];
    const double Dot2 = (double)P2[2] * Plane[2] + (double)P2[1] * Plane[1] +
                        (double)Plane[0] * P2[0];
    const int Side0 = (Dot0 > NegD) ? 1 : 2;
    const int Side1 = (Dot1 > NegD) ? 1 : 2;
    const int Side2 = (Dot2 > NegD) ? 1 : 2;
    if (Side0 == 1)
    {
        if (Side1 == 1)
            return (Side2 == 1) ? TriMeshTriangleAllFront : TriMeshTriangleFrontFrontBack;
        return (Side2 == 1) ? TriMeshTriangleFrontBackFront : TriMeshTriangleFrontBackBack;
    }
    if (Side1 == 2)
        return (Side2 == 2) ? TriMeshTriangleAllBack : TriMeshTriangleBackBackFront;
    return (Side2 == 2) ? TriMeshTriangleBackFrontBack : TriMeshTriangleBackFrontFront;
}

// formerly TriMeshGroupTakeTriangles
void FTriMeshGroup::CopyTriangles(const FTriMeshGroup* Source, const unsigned long* Classes, unsigned long Wanted, unsigned long Count)
{
    this->IndexCount = Count * 3;
    this->Indices = (unsigned short*)new unsigned char[this->IndexCount * 2];
    unsigned long Written = 0;
    const unsigned long Triangles = Source->IndexCount / 3;
    for (unsigned long Triangle = 0; Triangle < Triangles; ++Triangle)
    {
        if (Classes[Triangle] != Wanted)
            continue;
        this->Indices[Written] = Source->Indices[Triangle * 3];
        this->Indices[Written + 1] = Source->Indices[Triangle * 3 + 1];
        this->Indices[Written + 2] = Source->Indices[Triangle * 3 + 2];
        Written += 3;
    }
    PrivatizeVertices();
}


int FTriMeshArch::SplitByPlane(const float* Plane, float PlaneD, unsigned char SplitFlags,
                               FTriMeshArch** OutFront, FTriMeshArch** OutBack)
{
    *OutFront = new FTriMeshArch();
    *OutBack = new FTriMeshArch();
    for (size_t GroupIndex = 0; GroupIndex < Groups.size(); ++GroupIndex)
    {
        FTriMeshGroup* Source = Groups[GroupIndex];
        const unsigned char bStrip = (Source->Type == D3DPT_TRIANGLESTRIP) ? 1 : 0;
        int FrontCount = 0;
        int BackCount = 0;
        int StraddleCount = 0;
        unsigned short* StripIndices = nullptr;
        unsigned long StripCount = 0;
        if (bStrip != 0)
        {
            StripCount = Source->IndexCount;
            StripIndices = Source->Indices;
            const unsigned long Slots = StripCount - 2;
            unsigned short* List = (unsigned short*)new unsigned char[Slots * 2];
            Source->Indices = List;
            Source->IndexCount = 0;
            for (unsigned long Index = 0; Index < Slots; ++Index)
            {
                const unsigned short A = StripIndices[Index];
                const unsigned short B = StripIndices[Index + 1];
                const unsigned short C = StripIndices[Index + 2];
                if (A == B || B == C || C == A)
                    continue;
                List[Source->IndexCount] = A;
                if ((Index & 1) != 0)
                {
                    List[Source->IndexCount + 1] = C;
                    List[Source->IndexCount + 2] = B;
                }
                else
                {
                    List[Source->IndexCount + 1] = B;
                    List[Source->IndexCount + 2] = C;
                }
                Source->IndexCount += 3;
            }
            Source->Type = D3DPT_TRIANGLELIST;
        }

        const int Triangles = (int)(Source->IndexCount / 3);
        unsigned long* Classes = (unsigned long*)new unsigned char[(unsigned long)Triangles * 4];
        for (int Triangle = 0; Triangle < Triangles; ++Triangle)
        {
            const unsigned long Class = Source->ClassifyTriangle(Triangle, Plane, PlaneD);
            Classes[Triangle] = Class;
            if (Class == TriMeshTriangleAllFront)
                FrontCount += 1;
            else if (Class == TriMeshTriangleAllBack)
                BackCount += 1;
            else if ((SplitFlags & 0x1) == 0 && (Class & 0x8) != 0)
            {
                if ((SplitFlags & 0xC) != 0)
                    StraddleCount += 1;
                else if ((Class & 0x380) != 0)
                {
                    Classes[Triangle] = 1;
                    FrontCount += 1;
                }
                else
                {
                    Classes[Triangle] = 2;
                    BackCount += 1;
                }
            }
        }

        FTriMeshGroup* Front = TriMeshGroupNew();
        Front->CopyHeader(Source);
        Front->Vertices = &(*OutFront)->Vertices;
        Front->SharedVertices = Front->Vertices;
        FTriMeshGroup* Back = TriMeshGroupNew();
        Back->CopyHeader(Source);
        Back->Vertices = &(*OutBack)->Vertices;
        Back->SharedVertices = Back->Vertices;
        if (FrontCount > 0)
        {
            Front->Vertices = Source->Vertices;
            Front->SharedVertices = Front->Vertices;
            Front->CopyTriangles(Source, Classes, TriMeshTriangleAllFront, (unsigned long)FrontCount);
            Front->SharedVertices = &(*OutFront)->Vertices;
        }
        if (BackCount > 0)
        {
            Back->Vertices = Source->Vertices;
            Back->SharedVertices = Back->Vertices;
            Back->CopyTriangles(Source, Classes, TriMeshTriangleAllBack, (unsigned long)BackCount);
            Back->SharedVertices = &(*OutBack)->Vertices;
        }
        if (StraddleCount > 0)
            Source->ClipStraddling(Front, Back, Classes, StraddleCount);
        if (Front->IndexCount > 0)
        {
            (*OutFront)->Groups.insert((*OutFront)->Groups.end(), 1, Front);
            (*OutFront)->Cursor = (*OutFront)->Groups.size();
        }
        else
        {
            Front->ReleaseResources();
            delete Front;
        }
        if (Back->IndexCount > 0)
        {
            (*OutBack)->Groups.insert((*OutBack)->Groups.end(), 1, Back);
            (*OutBack)->Cursor = (*OutBack)->Groups.size();
        }
        else
        {
            Back->ReleaseResources();
            delete Back;
        }
        delete[] (const unsigned char*)Classes;
        if (bStrip != 0)
        {
            delete[] (const unsigned char*)Source->Indices;
            Source->Indices = StripIndices;
            Source->IndexCount = StripCount;
            Source->Type = D3DPT_TRIANGLESTRIP;
        }
    }

    FTriMeshArch* FrontArch = *OutFront;
    FTriMeshArch* BackArch = *OutBack;
    if (FrontArch->Groups.data() != nullptr && !FrontArch->Groups.empty() && BackArch->Groups.data() != nullptr &&
        !BackArch->Groups.empty())
    {
        FrontArch->CompactVertices();
        BackArch->CompactVertices();
        FrontArch->Flags = 1;
        BackArch->Flags = 1;
        FrontArch->Name[0] = '\0';
        BackArch->Name[0] = '\0';
        return 0;
    }
    if (FrontArch != nullptr)
        delete FrontArch;
    *OutFront = nullptr;
    if (BackArch != nullptr)
        delete BackArch;
    *OutBack = nullptr;
    return -1;
}

// FUN_06c28ed0 (cdecl; stream, old vertex count, new vertex count): the element buffer grows by
// (New - Old) elements (Count elements copied over), the index array is reallocated to New dwords
// (Old copied over). Count is not changed.
static void TriMeshStreamGrow(FRpVertexStream* Stream, unsigned long OldCount, unsigned long NewCount)
{
    unsigned char* Elements = new unsigned char[(NewCount - OldCount + Stream->Count) * Stream->Stride];
    std::memcpy(Elements, Stream->Base, Stream->Stride * Stream->Count);
    delete[] Stream->Base;
    Stream->Base = Elements;
    unsigned long* Indices = (unsigned long*)new unsigned char[NewCount * 4];
    std::memcpy(Indices, Stream->IndexArray, OldCount * 4);
    delete[] (const unsigned char*)Stream->IndexArray;
    Stream->IndexArray = Indices;
}

// One corner of FUN_06c27cc0's output: ByteSize bytes appended to the stream, the vertex's index
// slot set to the element just written.
static void TriMeshStreamAppend(FRpVertexStream* Stream, unsigned long Vertex, const unsigned char* Source)
{
    std::memcpy((unsigned char*)Stream->Base + Stream->Stride * Stream->Count, Source, Stream->ByteSize);
    ((unsigned long*)Stream->IndexArray)[Vertex] = Stream->Count;
    Stream->Count += 1;
}

// The element a vertex names in a stream (base + index[vertex] * stride).
static const unsigned char* TriMeshStreamElement(const FRpVertexStream* Stream, unsigned short Vertex)
{
    const unsigned long* Indices = Stream->IndexArray;
    return Stream->Base + Indices[Vertex] * Stream->Stride;
}

// One output vertex of FUN_06c27cc0 (06c28990..06c28c1b for the front, ..06c28e89 for the back):
// position always, normal / tex0 / tex1 / diffuse per the source FVF, then the group gets the new
// vertex's index and SrcCount grows.
static void TriMeshClipEmit(FRpVertexSrcDesc* Desc, FTriMeshGroup* Group, const unsigned char* Position,
                            const unsigned char* Normal, const unsigned char* Tex0, const unsigned char* Tex1,
                            const unsigned char* Color, bool bNormal, bool bTex0, bool bTex1, bool bColor)
{
    TriMeshStreamAppend(&Desc->Position, Desc->SrcCount, Position);
    if (bNormal)
        TriMeshStreamAppend(&Desc->Normal, Desc->SrcCount, Normal);
    if (bTex0)
        TriMeshStreamAppend(&Desc->Tex[0], Desc->SrcCount, Tex0);
    if (bTex1)
        TriMeshStreamAppend(&Desc->Tex[1], Desc->SrcCount, Tex1);
    if (bColor)
        TriMeshStreamAppend(&Desc->Diffuse, Desc->SrcCount, Color);
    Group->Indices[Group->IndexCount] = (unsigned short)Desc->SrcCount;
    Group->IndexCount += 1;
    Desc->SrcCount += 1;
}

// A group still drawing its archetype's shared descriptor gets a zeroed private one carrying the
// source's Flags, SrcFvf and the ByteSize / Stride of every stream (06c27d74..06c27e1e).
static void TriMeshClipPrepareDesc(FTriMeshGroup* Group, const FRpVertexSrcDesc* Source)
{
    if (Group->SharedVertices != Group->Vertices)
        return;
    FRpVertexSrcDesc* Desc = TriMeshDescNew();
    Group->Vertices = Desc;
    Desc->Flags = Source->Flags;
    Desc->SrcFvf = Source->SrcFvf;
    Desc->Position.ByteSize = Source->Position.ByteSize;
    Desc->Position.Stride = Source->Position.Stride;
    Desc->Normal.ByteSize = Source->Normal.ByteSize;
    Desc->Normal.Stride = Source->Normal.Stride;
    Desc->Tex[0].ByteSize = Source->Tex[0].ByteSize;
    Desc->Tex[0].Stride = Source->Tex[0].Stride;
    Desc->Tex[1].ByteSize = Source->Tex[1].ByteSize;
    Desc->Tex[1].Stride = Source->Tex[1].Stride;
    Desc->Diffuse.ByteSize = Source->Diffuse.ByteSize;
    Desc->Diffuse.Stride = Source->Diffuse.Stride;
}

static float TriMeshHalfSum(float A, float B)
{
    return (float)(((double)A + (double)B) * 0.5);
}

// The midpoint's z in all three FUN_06c27cc0 cases (06c2812d / 06c2841a / 06c2870f): the sum is
// FSTP'd to a float local first, then FLD / FMUL 0.5f / FSTP float.
static float TriMeshHalfSumStored(float A, float B)
{
    const float Sum = (float)((double)A + (double)B);
    return (float)((double)Sum * 0.5);
}

// formerly TriMeshClipStraddling
void FTriMeshGroup::ClipStraddling(FTriMeshGroup* Front, FTriMeshGroup* Back, const unsigned long* Classes, int StraddleCount) const
{
    if (StraddleCount < 1)
        return;
    const unsigned long Added = (unsigned long)StraddleCount * 3;
    unsigned short* FrontIndices = (unsigned short*)new unsigned char[(Front->IndexCount + Added) * 2];
    std::memcpy(FrontIndices, Front->Indices, Front->IndexCount * 2);
    delete[] (const unsigned char*)Front->Indices;
    Front->Indices = FrontIndices;
    unsigned short* BackIndices = (unsigned short*)new unsigned char[(Back->IndexCount + Added) * 2];
    std::memcpy(BackIndices, Back->Indices, Back->IndexCount * 2);
    delete[] (const unsigned char*)Back->Indices;
    Back->Indices = BackIndices;

    const FRpVertexSrcDesc* SourceDesc = this->Vertices;
    TriMeshClipPrepareDesc(Front, SourceDesc);
    TriMeshClipPrepareDesc(Back, SourceDesc);

    FRpVertexSrcDesc* FrontDesc = Front->Vertices;
    FRpVertexSrcDesc* BackDesc = Back->Vertices;
    const unsigned long Fvf = SourceDesc->SrcFvf;
    const bool bNormal = ((Fvf >> 4) & 1) != 0;
    const bool bTex0 = (Fvf & 0xF00) >= 0x100;
    const bool bTex1 = (Fvf & 0xF00) >= 0x200;
    const bool bColor = ((Fvf >> 6) & 1) != 0;
    TriMeshStreamGrow(&FrontDesc->Position, FrontDesc->SrcCount, FrontDesc->SrcCount + Added);
    TriMeshStreamGrow(&BackDesc->Position, BackDesc->SrcCount, BackDesc->SrcCount + Added);
    if (bNormal)
    {
        TriMeshStreamGrow(&FrontDesc->Normal, FrontDesc->SrcCount, FrontDesc->SrcCount + Added);
        TriMeshStreamGrow(&BackDesc->Normal, BackDesc->SrcCount, BackDesc->SrcCount + Added);
    }
    if (bTex0)
    {
        TriMeshStreamGrow(&FrontDesc->Tex[0], FrontDesc->SrcCount, FrontDesc->SrcCount + Added);
        TriMeshStreamGrow(&BackDesc->Tex[0], BackDesc->SrcCount, BackDesc->SrcCount + Added);
    }
    if (bTex1)
    {
        TriMeshStreamGrow(&FrontDesc->Tex[1], FrontDesc->SrcCount, FrontDesc->SrcCount + Added);
        TriMeshStreamGrow(&BackDesc->Tex[1], BackDesc->SrcCount, BackDesc->SrcCount + Added);
    }
    if (bColor)
    {
        TriMeshStreamGrow(&FrontDesc->Diffuse, FrontDesc->SrcCount, FrontDesc->SrcCount + Added);
        TriMeshStreamGrow(&BackDesc->Diffuse, BackDesc->SrcCount, BackDesc->SrcCount + Added);
    }

    // The two corner tables (06c27cc0 stack: A at +0x70 / +0x8C / +0xBC / +0x98 / +0xA4, B at
    // +0xE0 / +0xD4 / +0xB0 / +0xEC / +0xC8) and the midpoint buffers. A stream the FVF lacks
    // keeps whatever its table held; it is never read.
    const unsigned char* PositionA[3] = { nullptr, nullptr, nullptr };
    const unsigned char* PositionB[3] = { nullptr, nullptr, nullptr };
    const unsigned char* NormalA[3] = { nullptr, nullptr, nullptr };
    const unsigned char* NormalB[3] = { nullptr, nullptr, nullptr };
    const unsigned char* Tex0A[3] = { nullptr, nullptr, nullptr };
    const unsigned char* Tex0B[3] = { nullptr, nullptr, nullptr };
    const unsigned char* Tex1A[3] = { nullptr, nullptr, nullptr };
    const unsigned char* Tex1B[3] = { nullptr, nullptr, nullptr };
    const unsigned char* ColorA[3] = { nullptr, nullptr, nullptr };
    const unsigned char* ColorB[3] = { nullptr, nullptr, nullptr };
    float MidPosition[3] = { 0.0f, 0.0f, 0.0f };
    float MidTex0[2] = { 0.0f, 0.0f };
    float MidTex1[2] = { 0.0f, 0.0f };
    unsigned long MidColor = 0;
    const unsigned char* Mid = (const unsigned char*)MidPosition;
    const unsigned char* MidT0 = (const unsigned char*)MidTex0;
    const unsigned char* MidT1 = (const unsigned char*)MidTex1;
    const unsigned char* MidC = (const unsigned char*)(&MidColor);

    const unsigned long Triangles = this->IndexCount / 3;
    for (unsigned long Triangle = 0; Triangle < Triangles; ++Triangle)
    {
        const unsigned long Class = Classes[Triangle];
        if ((Class & 0x8) == 0)
            continue;
        const unsigned short* Corner = this->Indices + Triangle * 3;
        // Which corner pair is cut, as the listing routes it: 0 = (P0, P2), 1 = (P1, P2),
        // 2 = (P0, P1).
        int Cut = 0;
        if ((Class & 0x10) != 0 || (Class & 0x200) != 0)
            Cut = 2;
        else if ((Class & 0x20) != 0 || (Class & 0x100) != 0)
            Cut = 1;
        // A = (Keep0, Keep1, M) / B = (M, Keep2, Keep3) in corner indices, per the table above
        // (the 0x18 / 0x208 case puts M second in A).
        int A0 = 0;
        int A1 = 0;
        int B1 = 0;
        int B2 = 0;
        int EdgeLeft = 0;
        int EdgeRight = 0;
        unsigned long SideShift = 0;
        if (Cut == 0)
        {
            A0 = 1; A1 = 2; B1 = 0; B2 = 1; EdgeLeft = 0; EdgeRight = 2; SideShift = 6;
        }
        else if (Cut == 1)
        {
            A0 = 0; A1 = 1; B1 = 2; B2 = 0; EdgeLeft = 1; EdgeRight = 2; SideShift = 5;
        }
        else
        {
            A0 = 0; A1 = 2; B1 = 1; B2 = 2; EdgeLeft = 0; EdgeRight = 1; SideShift = 4;
        }

        const unsigned char* P[3];
        for (int Index = 0; Index < 3; ++Index)
            P[Index] = TriMeshStreamElement(&SourceDesc->Position, Corner[Index]);
        const float* PL = (const float*)P[EdgeLeft];
        const float* PR = (const float*)P[EdgeRight];
        MidPosition[0] = TriMeshHalfSum(PL[0], PR[0]);
        MidPosition[1] = TriMeshHalfSum(PL[1], PR[1]);
        MidPosition[2] = TriMeshHalfSumStored(PL[2], PR[2]);
        if (Cut == 2)
        {
            PositionA[0] = P[0]; PositionA[1] = Mid; PositionA[2] = P[2];
        }
        else
        {
            PositionA[0] = P[A0]; PositionA[1] = P[A1]; PositionA[2] = Mid;
        }
        PositionB[0] = Mid; PositionB[1] = P[B1]; PositionB[2] = P[B2];

        if (bNormal)
        {
            const unsigned char* N[3];
            for (int Index = 0; Index < 3; ++Index)
                N[Index] = TriMeshStreamElement(&SourceDesc->Normal, Corner[Index]);
            if (Cut == 2)
            {
                NormalA[0] = N[0]; NormalA[1] = Mid; NormalA[2] = N[2];
            }
            else
            {
                NormalA[0] = N[A0]; NormalA[1] = N[A1]; NormalA[2] = Mid;
            }
            NormalB[0] = Mid; NormalB[1] = N[B1]; NormalB[2] = N[B2];
        }
        if (bTex0)
        {
            const unsigned char* T[3];
            for (int Index = 0; Index < 3; ++Index)
                T[Index] = TriMeshStreamElement(&SourceDesc->Tex[0], Corner[Index]);
            const float* TL = (const float*)T[EdgeLeft];
            const float* TR = (const float*)T[EdgeRight];
            MidTex0[0] = TriMeshHalfSum(TL[0], TR[0]);
            MidTex0[1] = TriMeshHalfSum(TL[2], TR[2]);
            if (Cut == 2)
            {
                Tex0A[0] = T[0]; Tex0A[1] = MidT0; Tex0A[2] = T[2];
            }
            else
            {
                Tex0A[0] = T[A0]; Tex0A[1] = T[A1]; Tex0A[2] = MidT0;
            }
            Tex0B[0] = MidT0; Tex0B[1] = T[B1]; Tex0B[2] = T[B2];
        }
        if (bTex1)
        {
            const unsigned char* T[3];
            for (int Index = 0; Index < 3; ++Index)
                T[Index] = TriMeshStreamElement(&SourceDesc->Tex[1], Corner[Index]);
            const float* TL = (const float*)T[EdgeLeft];
            const float* TR = (const float*)T[EdgeRight];
            MidTex1[0] = TriMeshHalfSum(TL[0], TR[0]);
            MidTex1[1] = TriMeshHalfSum(TL[2], TR[2]);
            if (Cut == 2)
            {
                Tex1A[0] = T[0]; Tex1A[1] = MidT1; Tex1A[2] = T[2];
            }
            else
            {
                Tex1A[0] = T[A0]; Tex1A[1] = T[A1]; Tex1A[2] = MidT1;
            }
            Tex1B[0] = MidT1; Tex1B[1] = T[B1]; Tex1B[2] = T[B2];
        }
        if (bColor)
        {
            const unsigned char* C[3];
            for (int Index = 0; Index < 3; ++Index)
                C[Index] = TriMeshStreamElement(&SourceDesc->Diffuse, Corner[Index]);
            MidColor = (*(const unsigned long*)C[EdgeLeft] +
                        *(const unsigned long*)C[EdgeRight]) >> 1;
            if (Cut == 2)
            {
                ColorA[0] = C[0]; ColorA[1] = MidC; ColorA[2] = C[2];
            }
            else
            {
                ColorA[0] = C[A0]; ColorA[1] = C[A1]; ColorA[2] = MidC;
            }
            ColorB[0] = MidC; ColorB[1] = C[B1]; ColorB[2] = C[B2];
        }

        // 06c2897f: NOT AL / AND AL,1 on Class >> shift, then 06c2899b TEST / JNZ to the B tables:
        // the front takes B when that class bit is clear (0x88, 0x108, 0x208), A when it is set.
        const bool bFrontTakesB = ((Class >> SideShift) & 1) == 0;
        for (int CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            if (!bFrontTakesB)
            {
                TriMeshClipEmit(FrontDesc, Front, PositionA[CornerIndex], NormalA[CornerIndex], Tex0A[CornerIndex],
                                Tex1A[CornerIndex], ColorA[CornerIndex], bNormal, bTex0, bTex1, bColor);
                TriMeshClipEmit(BackDesc, Back, PositionB[CornerIndex], NormalB[CornerIndex], Tex0B[CornerIndex],
                                Tex1B[CornerIndex], ColorB[CornerIndex], bNormal, bTex0, bTex1, bColor);
            }
            else
            {
                TriMeshClipEmit(FrontDesc, Front, PositionB[CornerIndex], NormalB[CornerIndex], Tex0B[CornerIndex],
                                Tex1B[CornerIndex], ColorB[CornerIndex], bNormal, bTex0, bTex1, bColor);
                TriMeshClipEmit(BackDesc, Back, PositionA[CornerIndex], NormalA[CornerIndex], Tex0A[CornerIndex],
                                Tex1A[CornerIndex], ColorA[CornerIndex], bNormal, bTex0, bTex1, bColor);
            }
        }
    }
}

