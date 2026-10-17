// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MaterialBatcher.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include <algorithm>
#include <cmath>
#include <cstdlib>

void FMaterialBatcher::ResetScratch()
{
    // FUN_06eca5d0: release the owner stream arena and reset its cursors.
    delete[] ScratchBuffer;
    ScratchBuffer = nullptr;
    BufferSize = 0;
    FrameValue = 0;
    PendingFrameValue = 0;
}

void FMaterialBatcher::InitializeScratch(unsigned long Size)
{
    // FUN_06eca560: queue nodes use native containers; this arena owns stream copies.
    ResetScratch();
    if (Size != 0)
    {
        ScratchBuffer = new unsigned char[Size];
        BufferSize = Size;
    }
    FrameValue = 0;
    PendingFrameValue = 0;
}

void* FMaterialBatcher::AllocateScratch(unsigned long Size)
{
    // FUN_06eca620: integer pointer arithmetic and wrapping DWORD cursor.
    const uintptr_t Address = (uintptr_t)ScratchBuffer + FrameValue;
    FrameValue += Size;
    return (void*)Address;
}

unsigned short* FMaterialBatcher::CloneIndices(const unsigned short* Source, unsigned long Count)
{
    // FUN_06eca3d0: wrap the byte count in a DWORD, then copy every byte.
    const unsigned long Bytes = Count * 2;
    unsigned short* Dest = (unsigned short*)AllocateScratch(Bytes);
    // REP MOVS with zero count performs no memory access.
    if (Bytes != 0)
        std::memcpy(Dest, Source, Bytes);
    return Dest;
}

void FMaterialBatcher::CloneVertexStream(const FRpVertexStream& Source, FRpVertexStream& Dest,
                                         unsigned long IndexCount)
{
    // FUN_06eca400: test pointers alone; zero-length nonnull arrays stay nonnull.
    Dest.ByteSize = Source.ByteSize;
    Dest.Stride = Source.Stride;
    Dest.Count = Source.Count;
    if (Source.Base != nullptr)
    {
        const unsigned long Bytes = Source.Count * Source.Stride;
        unsigned char* Copy = (unsigned char*)AllocateScratch(Bytes);
        Dest.Base = Copy;
        std::memcpy(Copy, Source.Base, Bytes);
    }
    else
        Dest.Base = nullptr;
    if (Source.IndexArray != nullptr)
    {
        const unsigned long Bytes = IndexCount * sizeof(unsigned long);
        unsigned long* Copy = (unsigned long*)AllocateScratch(Bytes);
        Dest.IndexArray = Copy;
        std::memcpy(Copy, Source.IndexArray, Bytes);
    }
    else
        Dest.IndexArray = nullptr;
}

void FMaterialBatcher::CopyVertexSource(const FRpVertexSrcDesc& Source, FRpVertexSrcDesc& Dest)
{
    // FUN_06eca4a0: header then Position, Normal, Diffuse, Tex0, Tex1.
    Dest.Flags = Source.Flags;
    Dest.SrcFvf = Source.SrcFvf;
    Dest.SrcCount = Source.SrcCount;
    CloneVertexStream(Source.Position, Dest.Position, Source.SrcCount);
    CloneVertexStream(Source.Normal, Dest.Normal, Source.SrcCount);
    CloneVertexStream(Source.Diffuse, Dest.Diffuse, Source.SrcCount);
    CloneVertexStream(Source.Tex[0], Dest.Tex[0], Source.SrcCount);
    CloneVertexStream(Source.Tex[1], Dest.Tex[1], Source.SrcCount);
}

FRpVertexSrcDesc* FMaterialBatcher::CloneVertexSource(const FRpVertexSrcDesc& Source)
{
    // Descriptor and arrays share the scratch arena lifetime; only raw array bytes are copied.
    void* Storage = AllocateScratch(sizeof(FRpVertexSrcDesc));
    FRpVertexSrcDesc* Dest = new (Storage) FRpVertexSrcDesc();
    CopyVertexSource(Source, *Dest);
    return Dest;
}

FBatchRecord::FBatchRecord()
{
    std::memset(this, 0, sizeof(*this));
    Context.World = MatrixDataA;
    Context.View = MatrixDataB;
}

FBatchBin::FBatchBin() : Pad0(0), SortKind(0), RecordCount(0) {}



// Dispatch layout consumed by FUN_06ec9f10; the face is borrowed after flattening.
struct FBatchDispatch
{
    unsigned long MaterialFlags;
    float Key;
    FBatchRecord* Record;
    IBatchedMaterial* Batched;
    IMaterial* Material;
};

struct FBatchTriangle
{
    float Key;
    unsigned long PositionIndices[3];
    unsigned short Indices[3];
    unsigned short Padding;
};

static int CompareBatchKeys(float A, float B, bool bAscending)
{
    // Ascending TEST AH,05 + JP falls through only for less; descending
    // TEST AH,41 + JNZ falls through only for greater. TEST AH,44 + JP
    // yields +1 for unordered, 0 for equal, +1 for the remaining order.
    if (bAscending ? A < B : A > B)
        return -1;
    return A == B ? 0 : 1;
}

static int __cdecl CompareBatchDispatchAscending(const void* A, const void* B)
{
    return CompareBatchKeys(((const FBatchDispatch*)A)->Key,
                            ((const FBatchDispatch*)B)->Key, true);
}

static int __cdecl CompareBatchDispatchDescending(const void* A, const void* B)
{
    return CompareBatchKeys(((const FBatchDispatch*)A)->Key,
                            ((const FBatchDispatch*)B)->Key, false);
}

static int __cdecl CompareBatchTriangleAscending(const void* A, const void* B)
{
    return CompareBatchKeys(((const FBatchTriangle*)A)->Key,
                            ((const FBatchTriangle*)B)->Key, true);
}

static int __cdecl CompareBatchTriangleDescending(const void* A, const void* B)
{
    return CompareBatchKeys(((const FBatchTriangle*)A)->Key,
                            ((const FBatchTriangle*)B)->Key, false);
}

static double ComputeBatchPositionDepth(const FBatchRecord& Record, const float* Position)
{
    // x87 evaluates Z + Y, then X, then translation; no float spill occurs.
    double Depth = (double)Record.Combined[8] * Position[2];
    Depth += (double)Record.Combined[5] * Position[1];
    Depth += (double)Record.Combined[2] * Position[0];
    Depth += Record.Combined[11];
    return Depth;
}

static float ComputeBatchDepth(const FBatchRecord& Record)
{
    if ((Record.Flags & 0x20) != 0)
    {
        const unsigned long Bits = (unsigned long)Record.Unused;
        float Depth;
        std::memcpy(&Depth, &Bits, sizeof(Depth));
        return -Depth;
    }
    const FRpVertexSrcDesc& Source = *Record.Context.SourceVertices;
    const unsigned char* Cursor = Source.Position.Base;
    const uintptr_t End = (uintptr_t)Cursor +
                          Source.Position.Count * Source.Position.Stride;
    double Sum = 0.0;
    while ((uintptr_t)Cursor != End)
    {
        Sum = ComputeBatchPositionDepth(Record, (const float*)Cursor) + Sum;
        Cursor += Source.Position.Stride;
    }
    return (float)(Sum / (double)Source.Position.Count);
}

void FMaterialBatcher::BuildBatchDispatch(FBatchBin* Bin, FBatchDispatch* Dispatch)
{
    // FUN_06ec9da0: query once per ordered material group, release after its list.
    for (std::map<IMaterial*, std::list<FBatchQueuedRecord>>::iterator Group = Bin->Queue.begin();
         Group != Bin->Queue.end(); ++Group)
    {
        IMaterial* Material = Group->first;
        unsigned long MaterialFlags = 0;
        Material->GetRenderFlags(&MaterialFlags);
        IBatchedMaterial* Batched = nullptr;
        if (Material->QueryInterface(Iids::BatchedMaterial,
                                     (void**)&Batched) >= 0)
        {
            for (std::list<FBatchQueuedRecord>::iterator Item = Group->second.begin();
                 Item != Group->second.end(); ++Item)
            {
                Dispatch->MaterialFlags = MaterialFlags;
                Dispatch->Record = &Item->Retail;
                Dispatch->Batched = Batched;
                Dispatch->Material = Material;
                Dispatch->Key = ComputeBatchDepth(Item->Retail);
                ++Dispatch;
            }
            if (Batched != nullptr)
                Batched->Release();
        }
    }
}

FMaterialBatcherController::FMaterialBatcherController(FMaterialBatcher* InOwner)
    : Owner(InOwner)
{
}

GENRESULT DACOM_API FMaterialBatcherController::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06ecba70: fixed map 06ed2e34 has offsets 0, 0, 4, 8.
    IDAComponent* Face;
    if (std::strcmp(Iid, Iids::MaterialBatcher) == 0 || std::strcmp(Iid, Iids::MaterialBatcher2) == 0)
        Face = Owner;
    else if (std::strcmp(Iid, Iids::MaterialLibrary) == 0)
        Face = &Owner->Library;
    else if (std::strcmp(Iid, Iids::AggregateComponent) == 0)
        Face = &Owner->Library.Aggregate;
    else
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    Face->AddRef();
    *Out = Face;
    return GR_OK;
}

unsigned long DACOM_API FMaterialBatcherController::AddRef()
{
    return ++Owner->RefCount;
}

unsigned long DACOM_API FMaterialBatcherController::Release()
{
    // LAB_06ecbb30: saturate at zero, restore one before owner destruction.
    if (Owner->RefCount != 0)
        --Owner->RefCount;
    const unsigned long Next = Owner->RefCount;
    if (Next == 0)
    {
        FMaterialBatcher* DeleteOwner = Owner;
        DeleteOwner->RefCount = 1;
        delete DeleteOwner;
    }
    return Next;
}

FMaterialBatcher::FMaterialBatcher()
    : RefCount(1),
      Outer(nullptr),
      Pipeline(nullptr),
      RpVertexBuffer(nullptr),
      RpIndexBuffer(nullptr),
      ScratchBuffer(nullptr),
      BufferSize(0),
      Bin1SortOrder(0),
      SelectedBin(nullptr),
      CachedInterface(nullptr),
      Field78(nullptr),
      Field7C(nullptr),
      FrameValue(0),
      PendingFrameValue(0),
      Controller(this),
      ControllingUnknown(&Controller)
{
    Library.OwnerBatcher = this;
    Library.ControllingUnknown = this;
    BinOrder[0] = (unsigned long)-1;
    BinOrder[1] = (unsigned long)-1;
}

FMaterialBatcher::~FMaterialBatcher()
{
    Library.ClearRuntime();
    for (unsigned long Index = 0; Index < 2; ++Index)
    {
        ClearBin(Bins[Index].get());
        Bins[Index].reset();
    }
    ResetScratch();
    RuleList.clear();
    Outer = nullptr;
    Pipeline = nullptr;
    RpVertexBuffer = nullptr;
    RpIndexBuffer = nullptr;
}

static_assert(offsetof(FMaterialBatcher, Library) == 0x04, "IMaterialLibrary at Batcher+4");

GENRESULT DACOM_API FMaterialBatcher::QueryInterface(const char* Iid, void** Out)
{
    // +90 is selected by the factory before descriptor initialization.
    return ControllingUnknown->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FMaterialBatcher::AddRef()
{
    // +90 is selected by the factory before descriptor initialization.
    return ControllingUnknown->AddRef();
}

unsigned long DACOM_API FMaterialBatcher::Release()
{
    // +90 is selected by the factory before descriptor initialization.
    return ControllingUnknown->Release();
}

GENRESULT FMaterialBatcher::InitializeFromDescriptor(const FMaterialAggregateDesc* Desc)
{
    // FUN_06ec85a0: initialize the embedded library before the batcher outer.
    if (Library.InitializeFromDescriptor(Desc) < 0)
        return GR_GENERIC;
    Outer = Desc->OuterAt8;
    return GR_OK;
}

// The two inlined keyed readers in LAB_06ec85d0 share retail DAT_06ed5860.
static char GBatcherProfileValue[0x400];

static unsigned long ReadBatcherProfileUnsigned(ICOManager* Manager, IProfileParser* InitialParser,
                                               const char* Key, unsigned long DefaultValue)
{
    IProfileParser* Parser = InitialParser;
    if (Parser != nullptr)
        Parser->AddRef();
    else
    {
        Parser = nullptr;
        const GENRESULT Result = Manager->QueryInterface(Iids::ProfileParser, (void**)&Parser);
        // The fallback tests exact zero, unlike the initial startup QI.
        if (Result != 0)
        {
            const unsigned long Value = (unsigned long)(uintptr_t)Parser;
            if (Parser != nullptr)
                Parser->Release();
            return Value;
        }
    }
    unsigned long Value = DefaultValue;
    void* Section = Parser->OpenSection("MaterialBatcher", 3);
    if (Section != nullptr)
    {
        if (Parser->GetString(Section, Key, GBatcherProfileValue, sizeof(GBatcherProfileValue)) != 0)
        {
            // Raw PE 06ed4bb4: exact 37-byte accepted-prefix table, including NUL.
            const size_t Span = std::strspn(GBatcherProfileValue,
                "0123456789truefalsynoxTRUEFALSYNOX+-");
            if (Span != 0)
                GBatcherProfileValue[Span] = '\0';
            if (_stricmp(GBatcherProfileValue, "true") == 0 ||
                _stricmp(GBatcherProfileValue, "yes") == 0)
                Value = 1;
            else if (_stricmp(GBatcherProfileValue, "false") == 0 ||
                     _stricmp(GBatcherProfileValue, "no") == 0)
                Value = 0;
            else if (std::strchr(GBatcherProfileValue, 'x') != nullptr ||
                     std::strchr(GBatcherProfileValue, 'X') != nullptr)
                Value = std::strtoul(GBatcherProfileValue, nullptr, 16);
            else
            {
                // Retail calls strspn("0123456789+-") but ignores its result.
                std::strspn(GBatcherProfileValue, "0123456789+-");
                Value = (unsigned long)MsvcrtAtoi(GBatcherProfileValue);
            }
        }
        Parser->CloseSection(Section);
    }
    Parser->Release();
    return Value;
}

GENRESULT FMaterialBatcher::StartupFromOuter()
{
    IProfileParser* Parser = nullptr;
    ICOManager* Manager = DACOM_Acquire();
    const GENRESULT ParserResult = Manager->QueryInterface(Iids::ProfileParser, (void**)&Parser);
    if (ParserResult < 0)
    {
        FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
              R"(C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\MaterialBatcher.cpp)",
              0x1AA, "Unable to acquire IID_IProfileParser, cannot continue");
        if (Parser != nullptr)
            Parser->Release();
        return GR_GENERIC;
    }

    // Success includes nonzero HRESULT. Each acquired dependency is borrowed:
    // the aggregate/pipeline owns it after this immediately balances the QI ref.
    const GENRESULT PipelineResult = Outer->QueryInterface(Iids::RenderPipeline8B,
                                                          (void**)&Pipeline);
    if (PipelineResult < 0)
    {
        FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
              R"(C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\MaterialBatcher.cpp)",
              0x1AF, "Unable to acquire IID_IRenderPipeline, cannot continue");
        if (Parser != nullptr)
            Parser->Release();
        return GR_GENERIC;
    }
    Pipeline->Release();
    const GENRESULT VertexResult = Pipeline->QueryInterface(Iids::RPVertexBuffer,
                                                            (void**)&RpVertexBuffer);
    if (VertexResult < 0)
        FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
              R"(C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\MaterialBatcher.cpp)",
              0x1B5, "Unable to acquire IID_IRPVertexBuffer........");
    else
        RpVertexBuffer->Release();
    const GENRESULT IndexResult = Pipeline->QueryInterface(Iids::RPIndexBuffer,
                                                           (void**)&RpIndexBuffer);
    if (IndexResult < 0)
        FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
              R"(C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\MaterialBatcher.cpp)",
              0x1BC, "Unable to acquire IID_IRPIndexBuffer........");
    else
        RpIndexBuffer->Release();

    BufferSize = ReadBatcherProfileUnsigned(Manager, Parser, "BufferSize", 0x100000);
    InitializeScratch(BufferSize);
    const GENRESULT BaseResult = Library.InitializeBase(Outer);
    if (BaseResult < 0)
    {
        if (Parser != nullptr)
            Parser->Release();
        return GR_GENERIC;
    }
    ParseBatchedMaterials();
    Bin1SortOrder = ReadBatcherProfileUnsigned(Manager, Parser, "Bin1SortOrder", 2);
    BinOrder[0] = 0;
    FBatchBin* FirstBin = new FBatchBin();
    // LAB_06ec8b3c overwrites the old pointer without destroying its descriptor.
    // Keep that ownership behavior on repeated Startup, including allocation order.
    Bins[0].release();
    Bins[0].reset(FirstBin);
    Bins[0]->RecordCount = 0;
    Bins[0]->Pad0 = -1;
    Bins[0]->SortKind = 1;
    BinOrder[1] = 1;
    FBatchBin* SecondBin = new FBatchBin();
    Bins[1].release();
    Bins[1].reset(SecondBin);
    Bins[1]->RecordCount = 0;
    Bins[1]->Pad0 = 1;
    Bins[1]->SortKind = Bin1SortOrder;
    SelectedBin = Bins[0].get();
    PendingFrameValue = FrameValue;
    if (Parser != nullptr)
        Parser->Release();
    return GR_OK;
}

// FUN_06ec8bd0: independent parser acquisition and section lifetime.
void FMaterialBatcher::ParseBatchedMaterials()
{
    IProfileParser* Parser = nullptr;
    ICOManager* Manager = DACOM_Acquire();
    const GENRESULT Result = Manager->QueryInterface(Iids::ProfileParser, (void**)&Parser);
    if (Result >= 0)
    {
        void* Section = Parser->OpenSection("BatchedMaterials", 3);
        if (Section != nullptr)
        {
            char Line[0x400];
            int LineIndex = 0;
            while (Parser->ReadLine(Section, LineIndex, Line, sizeof(Line)) != 0)
            {
                char* Eq = std::strchr(Line, '=');
                if (Eq != nullptr)
                {
                    char* Key = Line;
                    while (*Key != '\0' && *Key != ';' && (*Key == ' ' || *Key == '\t'))
                        ++Key;
                    char* KeyEnd = Eq;
                    while (*KeyEnd != '\0' &&
                           (*KeyEnd == '=' || *KeyEnd == ';' || *KeyEnd == ' ' || *KeyEnd == '\t'))
                        --KeyEnd;
                    KeyEnd[1] = '\0';
                    char* Value = Eq + 1;
                    while (*Value != '\0' && *Value != ';' && (*Value == ' ' || *Value == '\t'))
                        ++Value;
                    char* ValueEnd = Value + std::strlen(Value) - 1;
                    while (*ValueEnd != '\0' &&
                           (*ValueEnd == ';' || *ValueEnd == ' ' || *ValueEnd == '\t'))
                        --ValueEnd;
                    ValueEnd[1] = '\0';
                    if (_stricmp(Key, "type") == 0)
                        AddRule(1, Value);
                    else if (_stricmp(Key, "name") == 0)
                        AddRule(2, Value);
                }
                ++LineIndex;
            }
            Parser->CloseSection(Section);
        }
    }
    if (Parser != nullptr)
        Parser->Release();
}

void FMaterialBatcher::ClearBin(FBatchBin* Bin)
{
    if (Bin == nullptr)
        return;
    Bin->Queue.clear();
    Bin->RecordCount = 0;
}

static void WarnUnsupportedBatchSort()
{
    // FUN_06eca370: exact five FDUMP arguments; no queue processing.
    FDUMP(0x100003, "%s(%d) : NOTICE:General:%s",
        "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\MaterialBatcher.cpp", 0x5d2, "IMB_SO_PRIM_BACK_TO_FRONT/FRONT_TO_BACK sort order unimplemented, contact pbleisch\n");
}

static void WarnMalformedBatchSort()
{
    // FUN_06eca3a0: preserve the retail typo and exact diagnostic metadata.
    FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
        "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\MaterialBatcher.cpp", 0x5d9, "MaterialBatcher: sort statte not well implemented, contact stravis\n");
}

void FMaterialBatcher::FlushOne(FBatchBin* Bin, unsigned char Clear)
{
    // FUN_06ec9d20: both callbacks execute even for an empty bin.
    if (CachedInterface != nullptr)
        CachedInterface->BeginFlush(Clear);
    switch (Bin->SortKind)
    {
    case 1:
        WarnMalformedBatchSort();
        break;
    case 2:
    case 3:
    case 4:
    case 5:
        ProcessQueuedBin(Bin);
        break;
    case 6:
    case 7:
        WarnUnsupportedBatchSort();
        break;
    default:
        break;
    }
    // Reload after the callback and processing: either can change this member.
    if (CachedInterface != nullptr)
        CachedInterface->EndFlush(Clear);
}

void FMaterialBatcher::ProcessQueuedBin(FBatchBin* Bin)
{
    FBatchDispatch* Dispatch = (FBatchDispatch*)AllocateScratch(Bin->RecordCount * sizeof(FBatchDispatch));
    BuildBatchDispatch(Bin, Dispatch);
    const bool bSortTriangles = Bin->SortKind == 4 || Bin->SortKind == 5;
    if (Bin->SortKind == 2 || Bin->SortKind == 4)
        std::qsort(Dispatch, Bin->RecordCount, sizeof(FBatchDispatch), CompareBatchDispatchAscending);
    else if (Bin->SortKind == 3 || Bin->SortKind == 5)
        std::qsort(Dispatch, Bin->RecordCount, sizeof(FBatchDispatch), CompareBatchDispatchDescending);

    IBatchedMaterial* Active = nullptr;
    void* PreviousContext = nullptr;
    void* ActiveField78 = nullptr;
    for (unsigned long Index = 0; Index < Bin->RecordCount; ++Index)
    {
        FBatchDispatch& Item = Dispatch[Index];
        FBatchRecord& Record = *Item.Record;
        if (bSortTriangles)
        {
            const unsigned long TriangleCount = Record.Field / 3;
            const FRpVertexSrcDesc& Source = *Record.Context.SourceVertices;
            const float* Positions = (const float*)Source.Position.Base;
            const unsigned long* Remap = Source.Position.IndexArray;
            FBatchTriangle* Triangles = (FBatchTriangle*)AllocateScratch(TriangleCount * sizeof(FBatchTriangle));
            const unsigned short* Indices = Record.Context.Indices + Record.Zero;
            for (unsigned long TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
            {
                FBatchTriangle& Triangle = Triangles[TriangleIndex];
                for (unsigned long Corner = 0; Corner < 3; ++Corner)
                {
                    Triangle.Indices[Corner] = Indices[TriangleIndex * 3 + Corner];
                    Triangle.PositionIndices[Corner] = Remap != nullptr ?
                        Remap[Triangle.Indices[Corner]] : Triangle.Indices[Corner];
                }
                // Retail triangle path uses packed float3 positions regardless of stride.
                double Depth = ComputeBatchPositionDepth(Record, Positions + Triangle.PositionIndices[1] * 3);
                Depth += ComputeBatchPositionDepth(Record, Positions + Triangle.PositionIndices[2] * 3);
                Depth += ComputeBatchPositionDepth(Record, Positions + Triangle.PositionIndices[0] * 3);
                Triangle.Key = (float)(Depth * (double)0.3333333432674408f);
            }
            if (Bin->SortKind == 4)
                std::qsort(Triangles, TriangleCount, sizeof(FBatchTriangle), CompareBatchTriangleAscending);
            else if (Bin->SortKind == 5)
                std::qsort(Triangles, TriangleCount, sizeof(FBatchTriangle), CompareBatchTriangleDescending);
            if ((Record.InternalFlags & 1) == 0)
            {
                Record.Context.Indices = (unsigned short*)AllocateScratch(TriangleCount * 6);
                Record.Zero = 0;
            }
            unsigned short* Destination = Record.Context.Indices + Record.Zero;
            for (unsigned long TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
            {
                Destination[TriangleIndex * 3] = Triangles[TriangleIndex].Indices[0];
                Destination[TriangleIndex * 3 + 1] = Triangles[TriangleIndex].Indices[1];
                Destination[TriangleIndex * 3 + 2] = Triangles[TriangleIndex].Indices[2];
            }
        }
        if ((Record.Flags & 0x100) != 0)
        {
            if (RpVertexBuffer->SelectVb(Record.VertexBufferListing.VertexBuffer) < 0 ||
                RpIndexBuffer->SelectIb(Record.IndexBufferScratch.IndexBuffer,
                                        Record.VertexBufferListing.BaseVertex, 0, 0) < 0)
                Record.Flags &= ~0x100UL;
        }
        int StateValue = 1;
        Pipeline->GetPipelineState(7, &StateValue);
        if (StateValue != 0)
            Pipeline->SetModelview(Record.Combined);
        else
        {
            Pipeline->SetWorld(Record.MatrixDataA);
            Pipeline->SetView(Record.MatrixDataB);
        }
        if (Active != Item.Batched || (Item.MaterialFlags & 0x20) != 0)
        {
            if (Active != nullptr)
                Active->Teardown(PreviousContext);
            Active = Item.Batched;
            Active->Bind(&Record.Context);
        }
        if (CachedInterface != nullptr && (Record.Field78 == nullptr || ActiveField78 != Record.Field78))
        {
            CachedInterface->BeginRecord(Item.Material, Record.Field78, Record.Field7C);
            ActiveField78 = Record.Field78;
        }
        PreviousContext = &Record.Context;
        Active->Submit(PreviousContext, Record.PrimType, Record.Start, Record.Count,
                      Record.Zero, Record.Field, Record.Flags);
        if (CachedInterface != nullptr)
            CachedInterface->EndRecord(Item.Material, Record.Field78, Record.Field7C);
    }
    if (PreviousContext != nullptr)
        Active->Teardown(PreviousContext);
}

GENRESULT DACOM_API FMaterialBatcher::ClearRules()
{
    // ClearRuleList — retail takes no explicit arg.
    RuleList.clear();
    return GR_OK;
}

GENRESULT DACOM_API FMaterialBatcher::AddRule(unsigned long Kind, const char* Pattern)
{
    // 8fa0 duplicates before compiling, then 9050 transfers both owners to the node.
    FBatchRule Rule;
    Rule.Kind = Kind;
    Rule.Pattern.assign(Pattern);
    Rule.Expression = FMaterialRegex::Compile(Pattern);
    RuleList.push_back(std::move(Rule));
    return GR_OK;
}

GENRESULT DACOM_API FMaterialBatcher::RemoveRule(unsigned long Kind, const char* Pattern)
{
    // 9080 removes the first exact Kind/strcmp match, including duplicate rules.
    for (size_t Index = 0; Index < RuleList.size(); ++Index)
    {
        if (RuleList[Index].Kind == Kind && RuleList[Index].Pattern == Pattern)
        {
            RuleList.erase(RuleList.begin() + Index);
            return GR_OK;
        }
    }
    return (GENRESULT)-1;
}

GENRESULT DACOM_API FMaterialBatcher::SelectBin(unsigned long Index)
{
    if (Index >= 2)
        return (GENRESULT)-1;
    SelectedBin = Bins[Index].get();
    return GR_OK;
}

GENRESULT DACOM_API FMaterialBatcher::SetBinSortKind(unsigned long Index, void* Value)
{
    if (Index >= 2)
        return (GENRESULT)-1;
    Bins[Index]->SortKind = (unsigned long)(uintptr_t)Value;
    return GR_OK;
}

GENRESULT DACOM_API FMaterialBatcher::SetBinOrder(unsigned long Index, long Key)
{
    if (Index >= 2)
        return (GENRESULT)-1;

    Bins[Index]->Pad0 = Key;
    BinOrder[0] = (unsigned long)-1;
    BinOrder[1] = (unsigned long)-1;

    unsigned long Count = 0;
    for (unsigned long Candidate = 0; Candidate < 2; ++Candidate)
    {
        if (Bins[Candidate]->Pad0 < 0)
            continue;
        unsigned long Position = 0;
        while (Position < Count &&
               Bins[BinOrder[Position]]->Pad0 <=
                   Bins[Index]->Pad0)
            ++Position;
        for (unsigned long Move = Count; Move > Position; --Move)
            BinOrder[Move] = BinOrder[Move - 1];
        BinOrder[Position] = Candidate;
        ++Count;
    }
    return GR_OK;
}

GENRESULT DACOM_API FMaterialBatcher::FlushBin(unsigned long Index, unsigned char Clear)
{
    // Retail indexes exactly the same two descriptors as slots 6/7/8/12.
    if (Index >= 2)
        return (GENRESULT)-1;
    FBatchBin* Target = Bins[Index].get();
    if (Target->RecordCount == 0)
        return GR_OK;
    FlushOne(Target, Clear);
    if (Clear != 0)
    {
        // 92ac/92b7 reload the indexed descriptor after foreign callbacks.
        Bins[Index]->RecordCount = 0;
        ClearBin(Bins[Index].get());
    }
    return GR_OK;
}

GENRESULT DACOM_API FMaterialBatcher::FlushAll(unsigned char Clear)
{
    // LAB_06ec9350: order slots carry descriptor indices, including disabled -1.
    for (unsigned long Position = 0; Position < 2; ++Position)
    {
        const unsigned long Index = BinOrder[Position];
        if (Index >= 2)
            continue;
        FBatchBin* Bin = Bins[Index].get();
        if (Bin->RecordCount == 0)
            continue;
        FlushOne(Bin, Clear);
        if (Clear != 0)
            ClearBin(Bin);
    }
    if (Clear != 0)
        FrameValue = PendingFrameValue;
    return GR_OK;
}

GENRESULT DACOM_API FMaterialBatcher::Submit(IMaterial* Material, void* Context, unsigned long PrimType,
                                              unsigned long Start, unsigned long Count,
                                              unsigned long Zero, unsigned long Field,
                                              void* Unused, unsigned long Flags)
{
    // LAB_06ec9570: mandatory selected descriptor, material and queued context.
    if (SelectedBin->Pad0 < 0)
    {
        IBatchedMaterial* Batched = nullptr;
        if (Material->QueryInterface(Iids::BatchedMaterial,
                                     (void**)&Batched) >= 0)
        {
            Batched->Bind(Context);
            Batched->Submit(Context, PrimType, Start, Count, Zero, Field, Flags);
            const GENRESULT Result = Batched->Teardown(Context);
            if (Batched != nullptr)
                Batched->Release();
            return Result;
        }
        return Material->Submit(Context, PrimType, Start, Count, Zero, Field, Flags);
    }

    ++SelectedBin->RecordCount;
    std::list<FBatchQueuedRecord>& Records = SelectedBin->Queue[Material];
    Records.emplace_back();
    FBatchQueuedRecord& Queued = Records.back();
    FBatchRecord& Record = Queued.Retail;
    // Construct in place: all subobject pointers remain valid for the queue lifetime.
    const FMaterialSubmitContext* Source = (const FMaterialSubmitContext*)Context;
    Record.PrimType = PrimType;
    Record.Flags = Flags;
    Record.Unused = Unused;
    Record.Zero = Zero;
    Record.Field = Field;
    Record.InternalFlags = 0;
    Record.Start = Start;
    Record.Count = Count;
    Record.Field7C = Field7C;
    Record.Field78 = Field78;
    Record.Context.Inst = Source->Inst;
    Record.Context.Field20 = Source->Field20;
    Record.Context.ReplacedMaterial = Source->ReplacedMaterial;
    const FTransform* MatrixA = (const FTransform*)Source->World;
    const FTransform* MatrixB = (const FTransform*)Source->View;
    static I3DMathEngine* GBatchMathEngine = nullptr;
    if (GBatchMathEngine == nullptr)
    {
        FMathDesc Desc;
        Desc.Size = sizeof(Desc);
        Desc.InterfaceName = "3DMathEngine";
        Desc.Implementation = nullptr;
        DACOM_Acquire()->CreateInstance((AGGDESC*)&Desc,
                                        (void**)&GBatchMathEngine);
    }
    FTransform Combined;
    GBatchMathEngine->MulTransform(Combined, *MatrixB, *MatrixA);
    const unsigned long QueuedFlags = Record.Flags;
    std::memcpy(Record.Combined, &Combined, sizeof(Record.Combined));
    // 98fd/990a reread both context fields after the foreign math callback.
    std::memcpy(Record.MatrixDataA,
        Source->World, sizeof(Record.MatrixDataA));
    std::memcpy(Record.MatrixDataB,
        Source->View, sizeof(Record.MatrixDataB));
    if ((QueuedFlags & 0x100) != 0)
    {
        const FVbListing* Listing = Source->VbListing;
        const FIbScratch* Scratch = Source->IbScratch;
        std::memcpy(&Record.VertexBufferListing, Listing, sizeof(Record.VertexBufferListing));
        std::memcpy(&Record.IndexBufferScratch, Scratch, sizeof(Record.IndexBufferScratch));
        Record.Context.VbListing = &Record.VertexBufferListing;
        Record.Context.IbScratch = &Record.IndexBufferScratch;
    }
    else
        Record.Context.VbListing = nullptr;
    const FRpVertexSrcDesc* VertexSource = Source->SourceVertices;
    if ((QueuedFlags & 4) != 0)
        Record.Context.SourceVertices = VertexSource;
    else
        Record.Context.SourceVertices = CloneVertexSource(*VertexSource);
    unsigned short* IndexSource = Source->Indices;
    if ((Record.Flags & 8) != 0)
        Record.Context.Indices = IndexSource;
    else
    {
        const uintptr_t IndexAddress = (uintptr_t)IndexSource + Count * 2;
        Record.Context.Indices = CloneIndices((const unsigned short*)IndexAddress, Zero);
        Record.InternalFlags |= 1;
        Record.Zero = 0;
    }
    return GR_OK;
}

GENRESULT DACOM_API FMaterialBatcher::ClearBins()
{
    for (unsigned long Index = 0; Index < 2; ++Index)
        ClearBin(Bins[Index].get());
    FrameValue = PendingFrameValue;
    return GR_OK;
}

GENRESULT DACOM_API FMaterialBatcher::NoOp(void* /*Arg*/)
{
    return GR_OK;
}

GENRESULT DACOM_API FMaterialBatcher::SetEvents(void* NewValue)
{
    // LAB_06ec99f0: AddRef the new value, swap into this+0x74, Release the old.
    IMaterialBatcherEvents* NewRef = (IMaterialBatcherEvents*)NewValue;
    IMaterialBatcherEvents* Old = CachedInterface;
    CachedInterface = NewRef;
    if (NewRef != nullptr)
        NewRef->AddRef();
    if (Old != nullptr)
        Old->Release();
    return GR_OK;
}

GENRESULT DACOM_API FMaterialBatcher::GetEvents(void* Arg)
{
    // LAB_06ec9a20: publish the initial reference, then reload it for AddRef.
    IMaterialBatcherEvents* Initial = CachedInterface;
    *(IMaterialBatcherEvents**)Arg = Initial;
    if (Initial != nullptr)
        CachedInterface->AddRef();
    return GR_OK;
}

GENRESULT DACOM_API FMaterialBatcher::SetRecordTag(void* Value)
{
    // LAB_06ec9a40: plain store into this+0x78, no refcount management.
    Field78 = Value;
    return GR_OK;
}

GENRESULT DACOM_API FMaterialBatcher::GetRecordTag(void* Arg)
{
    // LAB_06ec9a50: *out = this+0x78.
    if (Arg == nullptr)
        return GR_GENERIC;
    *(void**)Arg = Field78;
    return GR_OK;
}

GENRESULT DACOM_API FMaterialBatcher::SetRecordTagContext(void* Value)
{
    // LAB_06ec9a70: plain store into this+0x7C, no refcount management.
    Field7C = Value;
    return GR_OK;
}

GENRESULT DACOM_API FMaterialBatcher::GetRecordTagContext(void* Arg)
{
    // LAB_06ec9a80: *out = this+0x7C.
    if (Arg == nullptr)
        return GR_GENERIC;
    *(void**)Arg = Field7C;
    return GR_OK;
}
