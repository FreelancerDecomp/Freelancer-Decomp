// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "ShadingInternal.h"

#include <map>

FlULong GetMaterialGlobalRecordValue(FlULong Index);
void ActivateMaterialGlobalRecord(FlULong Index);
void InitializeMaterialGlobalRecords();

class FMaterialLibrary;

class FMaterialLibraryController final : public IDAComponent
{
public:
    explicit FMaterialLibraryController(FMaterialLibrary* InOwner);
    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;

    FMaterialLibrary* Owner;
};

class FLibraryAggregate final : public IAggregateComponent
{
public:
    FLibraryAggregate();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;
    GENRESULT DACOM_API Initialize() override;

    class FMaterialLibrary* Owner;
};

class FMaterialLibrary final : public IMaterialLibrary
{
public:
    FMaterialLibrary();
    ~FMaterialLibrary();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;

    GENRESULT DACOM_API AddMapRule(int Unused, const char* Pattern, const char* TypeName) override;
    GENRESULT DACOM_API FindMapRule(int Key, const char* Pattern, void* Unused,
                                    char* OutType) override;
    GENRESULT DACOM_API RemapType(const char* Name, const char* Type, void* Unused,
                                  char* OutType) override;
    GENRESULT DACOM_API RemoveMapRule(int Key, const char* Pattern) override;
    GENRESULT DACOM_API AllocateGroup(FlULong* OutId) override;
    GENRESULT DACOM_API LoadFromFileSystem(void* FileSystem, void* Extra) override;
    GENRESULT DACOM_API UnsupportedOperation(void* Arg1, void* Arg2) override;
    GENRESULT DACOM_API DeleteGroup(int GroupId) override;
    GENRESULT DACOM_API ApplyVerifyGroup(int GroupId, void* Context, void* Extra) override;
    GENRESULT DACOM_API ResolveTexturesGroup(int GroupId, float Delta) override;
    GENRESULT DACOM_API SetGroupName(int GroupId, const char* Name) override;
    GENRESULT DACOM_API GetGroupName(int GroupId, char* Buffer, FlULong Size) override;
    GENRESULT DACOM_API FindOrCreate(const char* Name, const char* Type, IDAComponent* AggregateOuter,
                                     IMaterial** Out) override;
    GENRESULT DACOM_API FindMaterialByCrc(void* Query, IMaterial** Out) override;
    GENRESULT DACOM_API AddMaterial(int GroupId, void* Record, IMaterial* Material) override;
    GENRESULT DACOM_API RemoveMaterial(FlULong GroupId, void* Record) override;
    GENRESULT DACOM_API GetGroupCount(FlULong* OutCount) override;
    GENRESULT DACOM_API EnumGroup(FlULong Index, FlULong* OutId) override;
    GENRESULT DACOM_API GetGroupMaterialCount(FlULong GroupId, int* OutCount) override;
    GENRESULT DACOM_API EnumGroupMaterial(FlULong GroupId, FlULong Index,
                                           IMaterial** Out) override;
    GENRESULT DACOM_API SetGlobalRecord(FlULong Index,
                                         const FlULong* Value) override;
    GENRESULT DACOM_API SetGlobalRecordValue(FlULong Index,
                                              const FlULong* Value) override;
    GENRESULT DACOM_API GetGlobalRecordValue(FlULong Index,
                                              FlULong* OutValue) override;
    FMaterialLibraryRecord* DACOM_API CreateAfterMap(const char* Name, const char* Type,
                                       unsigned char bRemap, IDAComponent* AggregateOuter) override;
    FMaterialLibraryRecord* CreateGenericRecord(const char* Name, const char* Type,
                                       unsigned char bRemap, IDAComponent* AggregateOuter);

    FLibraryAggregate Aggregate; // +4
    FlULong RefCount;
    IDAComponent* Outer;
    class FMaterialBatcher* OwnerBatcher;
    std::vector<FMaterialMapRule> MapRules;
    std::map<FlULong, FCrcSlot> CrcMap;
    std::map<FlULong, FMaterialGroup> Groups;
    FlULong NextGroupId;
    char RemapScratch[64];
    FMaterialLibraryController Controller;
    IDAComponent* ControllingUnknown; // standalone +44, distinct from base Outer+40

    GENRESULT ClearRuntime();
    GENRESULT InitializeBase(IDAComponent* InOuter);
    GENRESULT InitializeFromDescriptor(const FAGGDESC14* Desc);
    GENRESULT ParseMaterialMap();
    const char* ResolveType(const char* Name, const char* Type);
    FCrcSlot* FindByCrc(FlULong Crc);
    FMaterialGroup* FindGroup(FlULong Id);
    void StoreCrc(FlULong Crc, IMaterial* Material);
};
