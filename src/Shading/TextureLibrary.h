// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "ShadingInternal.h"

#include <map>

// ID values (+0x68) are owned independently of canonical texture records.
// The active-playback tree (+0x7c) borrows these stable map values.
struct FTexturePlayback
{
    FTextureEntry* Source;
    float LocalRate;
    bool bUseLocalRate;
    float PlaybackDirection;
    unsigned int CurrentFrame;
    unsigned int FrameCount;
    double ElapsedTime;
    double TotalDuration;
    unsigned int RefCount;
    int WrapMode;

    FTexturePlayback() : Source(nullptr), LocalRate(0.0f), bUseLocalRate(false),
        PlaybackDirection(1.0f), CurrentFrame(0), FrameCount(0), ElapsedTime(0.0),
        TotalDuration(0.0), RefCount(0), WrapMode(1) {}
};

// Named group owns playback IDs, independently of canonical name records.
struct FTextureGroup
{
    std::unique_ptr<char[]> Name;
    std::vector<int> Members;

    ~FTextureGroup() { Name.reset(); }
    void SetName(const char* Source)
    {
        Name.reset();
        if (Source != nullptr)
        {
            const size_t Length = std::strlen(Source) + 1;
            Name.reset(new char[Length]);
            std::memcpy(Name.get(), Source, Length);
        }
    }
};

class FTextureLibrary;

// Retail non-delegating IUnknown at TextureLibrary+0x98 (LAB_06ec6880 / PTR_LAB_06ed2ac8).
// SystemContainer stores this face in Node->Inner; public QI thunks through Outer/controlling.
class FTextureLibraryController final : public IDAComponent
{
public:
    explicit FTextureLibraryController(FTextureLibrary* InOwner);

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;

    FTextureLibrary* Owner;
};

class FTexLibAggregate final : public IAggregateComponent
{
public:
    FTexLibAggregate();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;
    GENRESULT DACOM_API Initialize() override;

    FTextureLibrary* Owner;
};

class FTextureLibrary final : public ITextureLibrary2
{
public:
    FTextureLibrary();
    ~FTextureLibrary();

    GENRESULT InitializeFromDescriptor(const FAGGDESC14* Desc);

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;

    GENRESULT DACOM_API SetGenericSlot(int Index, FlULong Value) override;
    GENRESULT DACOM_API GetGenericSlot(int Index, FlULong* Out) override;
    GENRESULT DACOM_API LoadTextureGroup(void* FileSystem, int* OutGroupId) override;
    GENRESULT DACOM_API LoadTextureLibrary(void* FileSystem, const char* Name) override;
    GENRESULT DACOM_API GarbageCollect(int GroupId) override;
    GENRESULT DACOM_API TickAllPlayback(int Unused, float DeltaSeconds) override;
    GENRESULT DACOM_API SetGroupName(int Id, const char* Name) override;
    GENRESULT DACOM_API GetGroupName(int Id, char* Buf, FlULong Size) override;
    GENRESULT DACOM_API FindOrCreateByName(const char* Name, void** OutTex) override;
    GENRESULT DACOM_API FindTextureByCrc(void* Query, void** OutTex) override;
    GENRESULT DACOM_API CreatePlaybackGroup(void* SourceRecord, int* OutId) override;
    GENRESULT DACOM_API ReleaseTextureRecord(void* Record) override;
    GENRESULT DACOM_API AddRefById(int Id) override;
    GENRESULT DACOM_API DecRefById(int Id) override;
    GENRESULT DACOM_API CopyName(void* Node, char* Buf, FlULong Size) override;
    GENRESULT DACOM_API SetRecordName(void* Record, const char* Name) override;
    GENRESULT DACOM_API SetSubTexture(void* Record, int Index, int TextureId) override;
    GENRESULT DACOM_API SetFrameHandle(void* Record, int Index, FlULong Handle) override;
    GENRESULT DACOM_API SetFrameRect(void* Record, int Index, float U0, float V0, float U1,
                                     float V1) override;
    GENRESULT DACOM_API GetTextureFrame(void* Tex, int Frame, FlULong* Out) override;
    GENRESULT DACOM_API GetGroupPixelFormat(void* Record, void* Unused, int* OutFormat) override;
    GENRESULT DACOM_API SetTileRate(void* Record, float Value) override;
    GENRESULT DACOM_API GetTileRate(void* Record, float* Out) override;
    GENRESULT DACOM_API GetFrameCount(void* Record, int* Out) override;
    GENRESULT DACOM_API ResolveTextureRecord(int Id, void** Out) override;
    GENRESULT DACOM_API GetFrame(int Id, int Frame, FlULong* Out) override;
    GENRESULT DACOM_API SetElapsedTime(int Id, float Seconds) override;
    GENRESULT DACOM_API GetElapsedTime(int Id, float* Out) override;
    GENRESULT DACOM_API SetFrameIndex(int Id, unsigned int Index) override;
    GENRESULT DACOM_API GetFrameIndex(int Id, unsigned int* Out) override;
    GENRESULT DACOM_API SetLocalRate(int Id, float Value) override;
    GENRESULT DACOM_API GetEffectiveRate(int Id, float* Out) override;
    GENRESULT DACOM_API SetWrapMode(int Id, int Mode) override;
    GENRESULT DACOM_API GetWrapMode(int Id, int* Out) override;
    GENRESULT DACOM_API AdvanceTime(int Id, float DeltaSeconds) override;
    GENRESULT DACOM_API PeekNextGroupId(FlULong* Out) override;
    GENRESULT DACOM_API EnumGroup(int Index, int* OutId) override;
    GENRESULT DACOM_API GetSubCount(int Id, int* Out) override;
    GENRESULT DACOM_API EnumTexture(int Id, int Index, void** OutRecord) override;
    GENRESULT DACOM_API AddSubTexture(int GroupId, void* Record) override;
    GENRESULT DACOM_API RemoveSubTexture(int GroupId, void* Record) override;
    GENRESULT DACOM_API PickGpuFormat(const FlULong* Src, FlULong* OutId) override;
    GENRESULT DACOM_API AllocateNewTextureId(int* OutId) override;

    FTexLibAggregate Aggregate; // +4
    FlULong RefCount;
    IDAComponent* AggregateOwner; // borrowed descriptor +8, retail object +0x0c
    IDAComponent* Outer; // retail controlling unknown at +0x94 (Outer or &Controller)
    FTextureLibraryController Controller; // retail non-delegating unknown @ +0x98
    IRenderPipeline* Pipeline;
    bool bCapabilitiesCached; // retail +0x2c
    FlULong bSquareTexturesOnly; // QueryDeviceAbility(5), retail +0x30
    FlULong MaxTextureWidth; // QueryDeviceAbility(6), retail +0x34
    FlULong MaxTextureHeight; // QueryDeviceAbility(7), retail +0x38
    std::map<int, FTextureGroup> GroupRecords; // retail signed-key tree +0x3c
    std::map<FlULong, FTextureEntry*> CanonicalRecords; // retail name tree +0x54
    std::map<int, FTexturePlayback> PlaybackRecords;
    std::map<int, FTexturePlayback*> ActivePlayback;
    int NextPlaybackId; // retail +0x90, independent of group counter +0x50
    int NextId;
    FlULong GenericSlots[7]; // slots 3/4 — this+0x10 generic dword array

    GENRESULT RefreshDeviceAbilities();
    FTexturePlayback* FindPlayback(int Id);
    int ReleasePlaybackReference(int Id);
    FTextureGroup* FindGroup(int Id);
    FTextureEntry* FindEntryByCrc(FlULong Crc);
    void AddRefEntryRecursive(FTextureEntry* Entry);
    FlULong SelectTextureMipLevels(FlULong* Width, FlULong* Height);
    GENRESULT SelectTextureDimensions(FlULong Width, FlULong Height,
                                      FlULong* OutWidth, FlULong* OutHeight);
    GENRESULT LoadDdsTexture(IFileSystem* FileSystem, const char* Name, void** OutHandle);
    GENRESULT LoadTgaTexture(IFileSystem* FileSystem, const char* Name, void** OutHandle);
    GENRESULT ProbeTextureCubemap(IFileSystem* FileSystem, void** OutHandle);
    GENRESULT LoadTextureRecord(IFileSystem* RootFileSystem, FFileSystemDesc* Desc,
                                FTextureEntry** Out, bool bRetain);
    GENRESULT LoadLegacyTextureRecord(IFileSystem* RootFileSystem, IFileSystem* TextureFileSystem,
                                      const char* Name, FTextureEntry** Out, bool bRetain);
    void DestroyEntry(FTextureEntry* Entry);
    void StoreByNameCrc(FTextureEntry* Entry);
    bool CreateTextureRecord(FlULong GpuHandle, const char* Name, FTextureEntry** Out, bool bRetain);
    int ReleaseEntryReference(FTextureEntry* Entry, bool bCleanup = true);
};
