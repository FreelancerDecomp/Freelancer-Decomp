// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "MaterialLibrary.h"
#include "ShadingInternal.h"

#include <list>
#include <map>
#include <memory>

struct FBatchRecord
{
    FlULong InternalFlags; // +0x00
    FMaterialSubmitContext Context; // +0x04, borrowed views into this queued record
    float MatrixDataA[12];       // +0x28
    float MatrixDataB[12];       // +0x58
    float Combined[12];          // +0x88
    FlULong PrimType;                // +0xB8
    FlULong Start;         // +0xBC
    FlULong Count;         // +0xC0
    FlULong Zero;          // +0xC4
    FlULong Field;         // +0xC8
    FVbListing VertexBufferListing; // +0xCC, formerly OptionalAData
    FIbScratch IndexBufferScratch;  // +0xD8, formerly OptionalBData
    FlULong Flags;         // +0xE0
    void* Field78;               // +0xE4
    void* Field7C;               // +0xE8
    void* Unused;                // +0xEC (Arg8; sort depth when flags&0x20)

    FBatchRecord();
};

FL_LAYOUT_ASSERT(sizeof(FBatchRecord) == 0xF0, "retail batch payload size 0xF0");
FL_LAYOUT_ASSERT(offsetof(FBatchRecord, Context) == 4, "queued context starts at +4");
FL_LAYOUT_ASSERT(sizeof(FMaterialSubmitContext) == 0x24, "queued context is nine words");
FL_LAYOUT_ASSERT(offsetof(FBatchRecord, Context) + offsetof(FMaterialSubmitContext, VbListing) == 0x18, "queued VB view +0x18");
FL_LAYOUT_ASSERT(offsetof(FBatchRecord, Context) + offsetof(FMaterialSubmitContext, IbScratch) == 0x1C, "queued IB view +0x1C");
FL_LAYOUT_ASSERT(offsetof(FBatchRecord, MatrixDataA) == 0x28, "world matrix follows context");
FL_LAYOUT_ASSERT(offsetof(FBatchRecord, MatrixDataB) == 0x58, "view matrix +0x58");
FL_LAYOUT_ASSERT(offsetof(FBatchRecord, Combined) == 0x88, "combined matrix +0x88");
FL_LAYOUT_ASSERT(sizeof(FVbListing) == 12 && sizeof(FIbScratch) == 8, "queued listing/scratch copy sizes");
FL_LAYOUT_ASSERT(offsetof(FBatchRecord, VertexBufferListing) == 0xCC, "owned VB listing +0xCC");
FL_LAYOUT_ASSERT(offsetof(FBatchRecord, IndexBufferScratch) == 0xD8, "owned IB scratch +0xD8");
FL_LAYOUT_ASSERT(offsetof(FBatchRecord, Flags) == 0xE0, "flags follow optional snapshots");

struct FBatchQueuedRecord
{
    FBatchRecord Retail;
    std::vector<unsigned short> OwnedIndices;
};

struct FBatchBin
{
    FlLong Pad0;
    FlULong SortKind; // +4
    std::map<IMaterial*, std::list<FBatchQueuedRecord>> Queue;
    FlULong RecordCount;

    FBatchBin();
};


class FMaterialBatcher;
struct FBatchDispatch;

class FMaterialBatcherController final : public IDAComponent
{
public:
    explicit FMaterialBatcherController(FMaterialBatcher* InOwner);

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;

    FMaterialBatcher* Owner;
};

class FMaterialBatcher final : public IMaterialBatcher2
{
public:
    FMaterialBatcher();
    ~FMaterialBatcher();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;

    GENRESULT DACOM_API ClearRules() override;
    GENRESULT DACOM_API AddRule(FlULong Kind, const char* Pattern) override;
    GENRESULT DACOM_API RemoveRule(FlULong Kind, const char* Pattern) override;
    GENRESULT DACOM_API SelectBin(FlULong Index) override;
    GENRESULT DACOM_API SetBinSortKind(FlULong Index, void* Value) override;
    GENRESULT DACOM_API SetBinOrder(FlULong Index, FlLong Key) override;
    GENRESULT DACOM_API FlushBin(FlULong Index, unsigned char Clear) override;
    GENRESULT DACOM_API FlushAll(unsigned char Clear) override;
    GENRESULT DACOM_API Submit(IMaterial* Material, void* Context, FlULong PrimType,
                               FlULong Start, FlULong Count,
                               FlULong Zero, FlULong Field,
                               void* Unused, FlULong Flags) override;
    GENRESULT DACOM_API ClearBins() override;
    GENRESULT DACOM_API NoOp(void* Arg) override;
    GENRESULT DACOM_API SetEvents(void* NewValue) override;
    GENRESULT DACOM_API GetEvents(void* Arg) override;
    GENRESULT DACOM_API SetRecordTag(void* Value) override;
    GENRESULT DACOM_API GetRecordTag(void* Arg) override;
    GENRESULT DACOM_API SetRecordTagContext(void* Value) override;
    GENRESULT DACOM_API GetRecordTagContext(void* Arg) override;

    FMaterialLibrary Library; // +4  QI 1.11_IMaterialLibrary
    std::vector<FBatchRule> RuleList; // retail this+0x58/+0x5C, slots 3/4/5
    FlULong RefCount;
    IDAComponent* Outer;
    IRenderPipeline* Pipeline;
    IRPVertexBuffer* RpVertexBuffer;
    IRPIndexBuffer* RpIndexBuffer;
    unsigned char* ScratchBuffer; // Owned byte arena for copied streams.
    FlULong BufferSize;      // retail owner +0x84 capacity
    FlULong Bin1SortOrder;
    std::unique_ptr<FBatchBin> Bins[2];
    FlULong BinOrder[2];     // retail this+0x60/+0x64
    FBatchBin* SelectedBin;        // retail this+0x70, slots 6/7
    IMaterialBatcherEvents* CachedInterface; // retail this+0x74, slots 14/15 (AddRef/Release-managed)
    void* Field78;                 // retail this+0x78, slots 16/17 (plain)
    void* Field7C;                 // retail this+0x7C, slots 18/19 (plain)
    FlULong FrameValue;      // retail this+0x88
    FlULong PendingFrameValue; // retail this+0x8C
    FMaterialBatcherController Controller; // retail nondelegating unknown @ +0x94
    IDAComponent* ControllingUnknown; // retail +90, distinct from initialized Outer+50

    GENRESULT InitializeFromDescriptor(const FAGGDESC14* Desc);
    GENRESULT StartupFromOuter();
    void ResetScratch();
    void InitializeScratch(FlULong Size);
    void* AllocateScratch(FlULong Size);
    unsigned short* CloneIndices(const unsigned short* Source, FlULong Count);
    void CloneVertexStream(const FRpVertexStream& Source, FRpVertexStream& Dest, FlULong IndexCount);
    void CopyVertexSource(const FRpVertexSrcDesc& Source, FRpVertexSrcDesc& Dest);
    FRpVertexSrcDesc* CloneVertexSource(const FRpVertexSrcDesc& Source);
    void ParseBatchedMaterials();
    void FlushOne(FBatchBin* Bin, unsigned char Clear);
    void ProcessQueuedBin(FBatchBin* Bin);
    void BuildBatchDispatch(FBatchBin* Bin, FBatchDispatch* Dispatch);
    void ClearBin(FBatchBin* Bin);
};
