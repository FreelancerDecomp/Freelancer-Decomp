// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "render/IBatchedMaterial.h"
#include "render/IMaterial.h"

#include <cstddef>
#include <cstring>

// Tagged DWORD payload with explicit string/component ownership.
// Scalar payloads retain their bit patterns; lifetime is managed explicitly.
struct FShadingPropertyValue
{
    unsigned long Type;       // +0x00
    unsigned long Payload0;   // +0x04
    unsigned long Payload1;   // +0x08
    unsigned long Payload2;   // +0x0C
    unsigned long Payload3;   // +0x10

    void SetType(unsigned long NewType)
    {
        // Changing the tag releases its old owned payload; the same tag preserves it.
        if (NewType == Type)
        {
            Type = NewType;
            return;
        }
        if (Type == 5 || Type == 7)
        {
            if (Payload0 != 0)
                HeapFree(GetProcessHeap(), 0, (void*)Payload0);
        }
        else if (Type == 6)
        {
            IDAComponent* Component = (IDAComponent*)Payload0;
            Payload0 = 0; // Clear before and after the foreign Release callback.
            if (Component != nullptr)
                Component->Release();
        }
        Payload0 = 0;
        Type = NewType;
    }

    void ReleaseOwnedPayload()
    {
        // FUN_06eb1660: only ProcessHeap strings (5/7) and COM values (6)
        // have ownership.  The tag itself deliberately remains unchanged.
        if (Type == 5 || Type == 7)
        {
            if (Payload0 != 0)
                HeapFree(GetProcessHeap(), 0, (void*)Payload0);
        }
        else if (Type == 6)
        {
            IDAComponent* Component = (IDAComponent*)Payload0;
            Payload0 = 0; // Clear before and after the foreign Release callback.
            if (Component != nullptr)
                Component->Release();
        }
        Payload0 = 0;
    }

    void Assign(unsigned long NewType, const void* Source)
    {
        // Copy scalar words, own strings, and retain component references.
        SetType(NewType);

        switch (NewType)
        {
        case 1:
        case 8:
        case 9:
        case 10:
            Payload0 = (unsigned long)Source;
            break;
        case 2:
            Payload0 = *(const unsigned long*)Source;
            break;
        case 3:
            Payload2 = ((const unsigned long*)Source)[2];
            Payload1 = ((const unsigned long*)Source)[1];
            Payload0 = ((const unsigned long*)Source)[0];
            break;
        case 4:
            Payload3 = ((const unsigned long*)Source)[3];
            Payload2 = ((const unsigned long*)Source)[2];
            Payload1 = ((const unsigned long*)Source)[1];
            Payload0 = ((const unsigned long*)Source)[0];
            break;
        case 5:
        case 7:
        {
            // Replace an old string even when the type stays the same.
            if (Payload0 != 0)
                HeapFree(GetProcessHeap(), 0, (void*)Payload0);
            Payload0 = 0;
            const char* Text = (const char*)Source;
            if (Text != nullptr)
            {
                const size_t Size = std::strlen(Text) + 1;
                char* Copy = (char*)HeapAlloc(GetProcessHeap(), 0, Size);
                Payload0 = (unsigned long)Copy;
                // Publish before copying; the copy is mandatory.
                std::memcpy(Copy, Text, Size);
            }
            break;
        }
        case 6:
        {
            IDAComponent* Previous = (IDAComponent*)Payload0;
            // Source is a const view; component reference management needs its mutable face.
            IDAComponent* Component = static_cast<IDAComponent*>(const_cast<void*>(Source));
            Payload0 = (unsigned long)Component;
            if (Component != nullptr)
                Component->AddRef();
            if (Previous != nullptr)
                Previous->Release();
            break;
        }
        default:
            break;
        }
    }
};

struct FShadingPropertyRecord
{
    unsigned long Flags;                   // +0x00
    const char* Name;                       // +0x04
    const char* Description;                // +0x08
    void* Callback;                         // +0x0C
    FShadingPropertyValue Current;          // +0x10
    FShadingPropertyValue Default;          // +0x24
    FShadingPropertyValue Saved;            // +0x38
};

static_assert(sizeof(FShadingPropertyValue) == 0x14, "retail typed property value stride");
static_assert(offsetof(FShadingPropertyRecord, Current) == 0x10, "property current +0x10");
static_assert(offsetof(FShadingPropertyRecord, Default) == 0x24, "property default +0x24");
static_assert(offsetof(FShadingPropertyRecord, Saved) == 0x38, "property saved +0x38");
static_assert(sizeof(FShadingPropertyRecord) == 0x4C, "retail property record stride");

static const char kIidImaData[] = "IMAData";
static const char kIidImaInstance[] = "IMAInstance";


// Texture groups, records, frames and playback state share one library object.
struct ITextureLibrary2 : IDAComponent
{
    // 3/4 +0x0C/+0x10 LAB_06ec0d10/0d30: generic dword-slot array at this+0x10 (7 words).
    virtual GENRESULT DACOM_API SetGenericSlot(int Index, unsigned long Value) = 0;
    virtual GENRESULT DACOM_API GetGenericSlot(int Index, unsigned long* Out) = 0;
    // LAB_06ec1c40: load every child below "Texture library" and return the
    // id of the resulting texture group.
    virtual GENRESULT DACOM_API LoadTextureGroup(void* FileSystem, int* OutGroupId) = 0; // 5 +0x14 RET 0xC
    // FUN_06ec18b0: load one named child below "Texture library".
    virtual GENRESULT DACOM_API LoadTextureLibrary(void* FileSystem, const char* Name) = 0; // 6 +0x18 RET 0xC
    // +0x1C: RET 8 includes stack this; ECX is unused at entry (06ec1f30, exe 0041b080).
    virtual GENRESULT DACOM_API GarbageCollect(int GroupId) = 0;
    // 8 +0x20 LAB_06ec2060: advance every playing entry's frame clock.
    virtual GENRESULT DACOM_API TickAllPlayback(int Unused, float DeltaSeconds) = 0;
    // 9/10 +0x24/+0x28 FUN_06ec20f0/21a0: rename/read a texture entry by id.
    virtual GENRESULT DACOM_API SetGroupName(int Id, const char* Name) = 0;
    virtual GENRESULT DACOM_API GetGroupName(int Id, char* Buf, unsigned long Size) = 0;
    // 11 +0x2C LAB_06ec2760: find-or-create by name, returns via slot-12 style handle (§15.1).
    virtual GENRESULT DACOM_API FindOrCreateByName(const char* Name, void** OutTex) = 0;
    // 12 +0x30 LAB_06ec2810 RET 0xC: Query+4 = CRC, *OutTex = texture object (node+0x10).
    virtual GENRESULT DACOM_API FindTextureByCrc(void* Query, void** OutTex) = 0;
    // 13 +0x34 LAB_06ec3050: AddRef SourceRecord, hand back an id for slots 29–43.
    virtual GENRESULT DACOM_API CreatePlaybackGroup(void* SourceRecord, int* OutId) = 0;
    // 14 +0x38 LAB_06ec2870: DecRef a texture RECORD pointer (FUN_06ec0bd0(record, flag=1)).
    // Callers pass FindTextureByCrc OutTex / FTexShape+0x00, not an integer id.
    virtual GENRESULT DACOM_API ReleaseTextureRecord(void* Record) = 0;
    // 15/16 +0x3C/+0x40 FUN_06ec3140 / LAB_06ec3260: AddRef/DecRef a texture entry by id.
    virtual GENRESULT DACOM_API AddRefById(int Id) = 0;
    virtual GENRESULT DACOM_API DecRefById(int Id) = 0;
    // 17 +0x44 LAB_06ec2890: bounded copy of a resolved entry's name.
    virtual GENRESULT DACOM_API CopyName(void* Node, char* Buf, unsigned long Size) = 0;
    // 18 +0x48 LAB_06ec28e0: replace a record's name and re-index it in the
    // by-name map. The port's name lookup is a linear scan over merged entries.
    virtual GENRESULT DACOM_API SetRecordName(void* Record, const char* Name) = 0;
    // 19 +0x4C LAB_06ec29d0: place a texture id into a record's child slot,
    // creating an anonymous texture record when the id is not registered.
    virtual GENRESULT DACOM_API SetSubTexture(void* Record, int Index, int TextureId) = 0;
    // 20/21 +0x50/+0x54 LAB_06ec2bf0/2d60: write one frame's handle / UV rect.
    virtual GENRESULT DACOM_API SetFrameHandle(void* Record, int Index, unsigned long Handle) = 0;
    virtual GENRESULT DACOM_API SetFrameRect(void* Record, int Index, float U0, float V0, float U1,
                                             float V1) = 0;
    // 22 +0x58 LAB_06ec2ee0 RET 0x10: tex* from slot 12, frame, out 5 dwords (handle+UV).
    // frame==-2 → last. Miss/null: 0,0,0,1.0,1.0 and EAX=-1. flmaterials efxshadow after +0x30.
    virtual GENRESULT DACOM_API GetTextureFrame(void* Tex, int Frame, unsigned long* Out) = 0;
    // 23 +0x5C FUN_06ec3780: multi-frame record's first child's GPU pixel format.
    // Retail stdcall RET 0x10 = this + Record + Unused + OutFormat (Unused unread).
    virtual GENRESULT DACOM_API GetGroupPixelFormat(void* Record, void* Unused, int* OutFormat) = 0;
    // 24/25/26 +0x60/+0x64/+0x68 LAB_06ec2fc0/2fe0/3000: TileRate + frame count on a record (§15.1).
    virtual GENRESULT DACOM_API SetTileRate(void* Record, float Value) = 0;
    virtual GENRESULT DACOM_API GetTileRate(void* Record, float* Out) = 0;
    virtual GENRESULT DACOM_API GetFrameCount(void* Record, int* Out) = 0;
    // 27 +0x6C FUN_06ec3290: resolve an id-map entry to the corresponding
    // texture record. Retail returns -1 for id==-1/miss and leaves Out intact.
    virtual GENRESULT DACOM_API ResolveTextureRecord(int Id, void** Out) = 0;
    virtual GENRESULT DACOM_API GetFrame(int Id, int Frame, unsigned long* Out) = 0; // 28 +0x70 RET 0x10
    // 29–37 playback-state accessors on record+0x18, keyed by id.
    virtual GENRESULT DACOM_API SetElapsedTime(int Id, float Seconds) = 0;
    virtual GENRESULT DACOM_API GetElapsedTime(int Id, float* Out) = 0;
    virtual GENRESULT DACOM_API SetFrameIndex(int Id, unsigned int Index) = 0;
    virtual GENRESULT DACOM_API GetFrameIndex(int Id, unsigned int* Out) = 0;
    virtual GENRESULT DACOM_API SetLocalRate(int Id, float Value) = 0;
    virtual GENRESULT DACOM_API GetEffectiveRate(int Id, float* Out) = 0;
    virtual GENRESULT DACOM_API SetWrapMode(int Id, int Mode) = 0;
    virtual GENRESULT DACOM_API GetWrapMode(int Id, int* Out) = 0;
    virtual GENRESULT DACOM_API AdvanceTime(int Id, float DeltaSeconds) = 0;
    // 38 +0x98 LAB_06ec2240: peek next group id.
    virtual GENRESULT DACOM_API PeekNextGroupId(unsigned long* Out) = 0;
    // 39 +0x9C LAB_06ec2260: ordinal 0 maps to reserved id 0; later ordinals
    // enumerate the named-group tree and return its stored id.
    virtual GENRESULT DACOM_API EnumGroup(int Index, int* OutId) = 0;
    // 40 +0xA0: root canonical count or named-group playback member count.
    virtual GENRESULT DACOM_API GetSubCount(int Id, int* Out) = 0;
    virtual GENRESULT DACOM_API EnumTexture(int Id, int Index, void** OutRecord) = 0; // 41 +0xA4
    virtual GENRESULT DACOM_API AddSubTexture(int GroupId, void* Record) = 0;
    virtual GENRESULT DACOM_API RemoveSubTexture(int GroupId, void* Record) = 0;
    virtual GENRESULT DACOM_API PickGpuFormat(const unsigned long* Src, unsigned long* OutId) = 0; // 44 +0xB0 RET 0xC
    // 45 +0xB4 FUN_06ec1950: allocate a fresh unnamed group with no playback members.
    virtual GENRESULT DACOM_API AllocateNewTextureId(int* OutId) = 0;
};

// Material animation key data shared by the library and its instances.
struct FMaDelta
{
    float Duration; // +0 slot 5 tick
    float Field4;
    float Field8;
    float FieldC;
    float Field10;
};

static_assert(sizeof(FMaDelta) == 0x14, "FMaDelta 0x14");

struct FMaKey
{
    float A;
    float B;
    float C;
    float D;
};

static_assert(sizeof(FMaKey) == 0x10, "FMaKey 0x10");

struct FMaKeyBlock
{
    unsigned long Count; // +0 MACount
    unsigned long Flags; // +4 MAFlags
    FMaDelta* Deltas;    // +8 MADeltas
    FMaKey* Keys;        // +C MAKeys
};

static_assert(offsetof(FMaKeyBlock, Keys) == 0x0C, "key block +C");

class IImaData : public IDAComponent
{
public:
    virtual FMaKeyBlock* DACOM_API KeyBlock() = 0;
    virtual void DACOM_API CrcOut(unsigned long* Out) = 0;
};

class IImaInstance : public IDAComponent
{
public:
    virtual FMaKeyBlock* DACOM_API GetKeyBlock() = 0;
    virtual void DACOM_API ResetTime() = 0;
    virtual void DACOM_API Tick(float Dt) = 0;
    virtual unsigned char DACOM_API FillUvMatrix(float* Out) = 0;
};

// Borrowed library master payload; ownership changes through library operations.
struct FMaterialLibraryRecord
{
    unsigned long RefCount;
    IMaterial* Material;
};

struct IMaterialLibrary : IDAComponent
{
    // 3 +0x0C 06ecd1d0: prepend a compiled mapping rule; Key 0 matches
    // material name, Key 1 matches material type. Deformable2 uses Key 0.
    virtual GENRESULT DACOM_API AddMapRule(int Key, const char* Pattern, const char* TypeName) = 0;
    // 4 +0x10 06ecd360: exact Key/Pattern lookup; third argument unused.
    virtual GENRESULT DACOM_API FindMapRule(int Key, const char* Pattern, void* Unused,
                                            char* OutType) = 0;
    // Match name/type rules and copy the mapped type; Unused is ignored.
    virtual GENRESULT DACOM_API RemapType(const char* Name, const char* Type,
                                          void* Unused, char* OutType) = 0;
    // 6 +0x18 06ecd480: remove the first exact Key/Pattern rule.
    virtual GENRESULT DACOM_API RemoveMapRule(int Key, const char* Pattern) = 0;
    virtual GENRESULT DACOM_API AllocateGroup(unsigned long* OutId) = 0; // 7 +0x1C
    virtual GENRESULT DACOM_API LoadFromFileSystem(void* FileSystem, void* Extra) = 0; // 8 +0x20 RET 0xC
    // 9 +0x24 LAB_06eccce0: unimplemented in retail
    // — unconditional `return 0xFFFFFFF9` ignoring both args.
    virtual GENRESULT DACOM_API UnsupportedOperation(void* Arg1, void* Arg2) = 0;
    // 10 +0x28 LAB_06ecccf0: delete one group, or clear the library for id 0.
    virtual GENRESULT DACOM_API DeleteGroup(int GroupId) = 0;
    // Verify one group, or all materials when GroupId is zero.
    virtual GENRESULT DACOM_API ApplyVerifyGroup(int GroupId, void* Context, void* Extra) = 0;
    // Resolve one group, or all materials when GroupId is zero.
    virtual GENRESULT DACOM_API ResolveTexturesGroup(int GroupId, float Delta) = 0;
    // 13/14 +0x34/+0x38 FUN_06ecd080/0130: set/get the name stored on a group node.
    virtual GENRESULT DACOM_API SetGroupName(int GroupId, const char* Name) = 0;
    virtual GENRESULT DACOM_API GetGroupName(int GroupId, char* Buffer, unsigned long Size) = 0;
    virtual GENRESULT DACOM_API FindOrCreate(const char* Name, const char* Type,
                                             IDAComponent* AggregateOuter, IMaterial** Out) = 0; // 15 +0x3C
    // 16 +0x40 RET 0xC LAB_06ecd5c0: query+4 CRC lookup, AddRef, no create. Miss → −1.
    virtual GENRESULT DACOM_API FindMaterialByCrc(void* Query, IMaterial** Out) = 0;
    // Add to the material map and, for nonzero GroupId, to the group.
    virtual GENRESULT DACOM_API AddMaterial(int GroupId, void* Record, IMaterial* Material) = 0;
    virtual GENRESULT DACOM_API RemoveMaterial(unsigned long GroupId, void* Record) = 0; // 18 +0x48 RET 0xC
    // 19 +0x4C LAB_06ecd830: group-tree size plus reserved group zero.
    virtual GENRESULT DACOM_API GetGroupCount(unsigned long* OutCount) = 0;
    // Enumerate group IDs, including the reserved group zero.
    virtual GENRESULT DACOM_API EnumGroup(unsigned long Index, unsigned long* OutId) = 0;
    // 21/22 +0x54/+0x58 FUN_06ecd8f0/d990: group member count/enumeration;
    // group zero addresses the complete material map.
    virtual GENRESULT DACOM_API GetGroupMaterialCount(unsigned long GroupId, int* OutCount) = 0;
    virtual GENRESULT DACOM_API EnumGroupMaterial(unsigned long GroupId, unsigned long Index,
                                                   IMaterial** Out) = 0;
    // 23 +0x5C LAB_06ecdaa0: grow the process-wide 8-byte record array through
    // Index, mark the selected record active, and copy Value[0] into record+4.
    virtual GENRESULT DACOM_API SetGlobalRecord(unsigned long Index,
                                                 const unsigned long* Value) = 0;
    // 24/25 +0x60/+0x64 LAB_06ecdc10/dc40: overwrite/read record+4.
    virtual GENRESULT DACOM_API SetGlobalRecordValue(unsigned long Index,
                                                      const unsigned long* Value) = 0;
    virtual GENRESULT DACOM_API GetGlobalRecordValue(unsigned long Index,
                                                      unsigned long* OutValue) = 0;
    // 26 +0x68: borrowed master record, remap byte, aggregate outer.
    virtual FMaterialLibraryRecord* DACOM_API CreateAfterMap(const char* Name, const char* Type,
                                               unsigned char bRemap, IDAComponent* AggregateOuter) = 0;
};

// Flush callbacks; FieldA/FieldB are borrowed tags stored with the queued record.
struct IMaterialBatcherEvents : IDAComponent
{
    virtual void DACOM_API BeginFlush(unsigned char Clear) = 0;                         // 3 +0x0c
    virtual void DACOM_API EndFlush(unsigned char Clear) = 0;                           // 4 +0x10
    virtual void DACOM_API BeginRecord(void* Material, void* FieldA, void* FieldB) = 0; // 5 +0x14
    virtual void DACOM_API EndRecord(void* Material, void* FieldA, void* FieldB) = 0;   // 6 +0x18
};

// Queues geometry in two ordered bins and drives flush callbacks.
struct IMaterialBatcher2 : IDAComponent
{
    // 3 +0x0C 06ec8f20: Clear compiled name/type rules.
    virtual GENRESULT DACOM_API ClearRules() = 0;
    // 4 +0x10 06ec8fa0: Append Kind/pattern rule.
    virtual GENRESULT DACOM_API AddRule(unsigned long Kind, const char* Pattern) = 0;
    // 5 +0x14 06ec9080: Remove first exact Kind/pattern rule.
    virtual GENRESULT DACOM_API RemoveRule(unsigned long Kind, const char* Pattern) = 0;
    // 6 +0x18 06ec9140: Select one of two queue bins.
    virtual GENRESULT DACOM_API SelectBin(unsigned long Index) = 0;
    // 7 +0x1C 06ec9160: Set queue sorting kind.
    virtual GENRESULT DACOM_API SetBinSortKind(unsigned long Index, void* Value) = 0;
    // 8 +0x20 06ec9190: Set signed bin ordering key; negative disables batching.
    virtual GENRESULT DACOM_API SetBinOrder(unsigned long Index, long Key) = 0;
    // 9 +0x24 06ec9270: Flush one indexed bin.
    virtual GENRESULT DACOM_API FlushBin(unsigned long Index, unsigned char Clear) = 0;
    // 10 +0x28 06ec9350: Flush bins in configured order.
    virtual GENRESULT DACOM_API FlushAll(unsigned char Clear) = 0;
    // 11 +0x2C 06ec9570: Draw immediately or queue context/geometry in the selected bin.
    virtual GENRESULT DACOM_API Submit(IMaterial* Material, void* Context,
                                       unsigned long PrimType, unsigned long Start,
                                       unsigned long Count, unsigned long IbStart,
                                       unsigned long IndexSpan, void* Unused,
                                       unsigned long Flags) = 0;
    // 12 +0x30 06ec9480: Clear both bin queues and synchronize frame value.
    virtual GENRESULT DACOM_API ClearBins() = 0;
    // 13 +0x34 06ec9560: Retail success stub; ignores argument.
    virtual GENRESULT DACOM_API NoOp(void* Arg) = 0;
    // 14 +0x38 06ec99f0: Publish/AddRef events sink and Release previous sink.
    virtual GENRESULT DACOM_API SetEvents(void* NewValue) = 0;
    // 15 +0x3C 06ec9a20: Return referenced events sink.
    virtual GENRESULT DACOM_API GetEvents(void* Arg) = 0;
    // 16 +0x40 06ec9a40: Set borrowed record tag (BeginRecord/EndRecord argument 2).
    virtual GENRESULT DACOM_API SetRecordTag(void* Value) = 0;
    // 17 +0x44 06ec9a50: Get borrowed record tag.
    virtual GENRESULT DACOM_API GetRecordTag(void* Arg) = 0;
    // 18 +0x48 06ec9a70: Set borrowed tag context (BeginRecord/EndRecord argument 3).
    virtual GENRESULT DACOM_API SetRecordTagContext(void* Value) = 0;
    // 19 +0x4C 06ec9a80: Get borrowed tag context.
    virtual GENRESULT DACOM_API GetRecordTagContext(void* Arg) = 0;
};

struct IMatAnimLibrary : IDAComponent
{
    // Load the MaterialAnim directories into an opaque group.
    virtual void* DACOM_API LoadAnimationGroup(void* FileSystem) = 0;
    // +0x10 LAB_06ec70c0 RET 4: null/-1 no-op; first erase current data CRCs
    // from the library map, then release all group references and the group.
    virtual GENRESULT DACOM_API UnloadAnimationGroup(void* Arg) = 0;
    // +0x14 LAB_06ec7190 RET 0xC: Query+4 CRC -> IMAInstance; Unused ignored.
    virtual GENRESULT DACOM_API CreateAnimationInstance(int Unused, void* Query, IImaInstance** OutInstance) = 0;
    // +0x18 LAB_06ec7430 RET 8: Filter==-1 ticks every linked instance.
    virtual GENRESULT DACOM_API TickAnimationInstances(int Filter, float Dt) = 0;
};
