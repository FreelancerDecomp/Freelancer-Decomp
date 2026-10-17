// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "ShadingInternal.h"

#include <map>

unsigned long GetMaterialGlobalRecordValue(unsigned long Index);
void ActivateMaterialGlobalRecord(unsigned long Index);
void InitializeMaterialGlobalRecords();

class FMaterialLibrary;

class FMaterialLibraryController final : public IDAComponent
{
public:
    explicit FMaterialLibraryController(FMaterialLibrary* InOwner);
    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    FMaterialLibrary* Owner;
};

class FLibraryAggregate final : public IAggregateComponent
{
public:
    FLibraryAggregate();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    GENRESULT DACOM_API Initialize() override;

    class FMaterialLibrary* Owner;
};

class FMaterialLibrary final : public IMaterialLibrary
{
public:
    FMaterialLibrary();
    ~FMaterialLibrary();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    GENRESULT DACOM_API AddMapRule(int Unused, const char* Pattern, const char* TypeName) override;
    GENRESULT DACOM_API FindMapRule(int Key, const char* Pattern, void* Unused,
                                    char* OutType) override;
    GENRESULT DACOM_API RemapType(const char* Name, const char* Type, void* Unused,
                                  char* OutType) override;
    GENRESULT DACOM_API RemoveMapRule(int Key, const char* Pattern) override;
    GENRESULT DACOM_API AllocateGroup(unsigned long* OutId) override;
    GENRESULT DACOM_API LoadFromFileSystem(void* FileSystem, void* Extra) override;
    GENRESULT DACOM_API UnsupportedOperation(void* Arg1, void* Arg2) override;
    GENRESULT DACOM_API DeleteGroup(int GroupId) override;
    GENRESULT DACOM_API ApplyVerifyGroup(int GroupId, void* Context, void* Extra) override;
    GENRESULT DACOM_API ResolveTexturesGroup(int GroupId, float Delta) override;
    GENRESULT DACOM_API SetGroupName(int GroupId, const char* Name) override;
    GENRESULT DACOM_API GetGroupName(int GroupId, char* Buffer, unsigned long Size) override;
    GENRESULT DACOM_API FindOrCreate(const char* Name, const char* Type, IDAComponent* AggregateOuter,
                                     IMaterial** Out) override;
    GENRESULT DACOM_API FindMaterialByCrc(void* Query, IMaterial** Out) override;
    GENRESULT DACOM_API AddMaterial(int GroupId, void* Record, IMaterial* Material) override;
    GENRESULT DACOM_API RemoveMaterial(unsigned long GroupId, void* Record) override;
    GENRESULT DACOM_API GetGroupCount(unsigned long* OutCount) override;
    GENRESULT DACOM_API EnumGroup(unsigned long Index, unsigned long* OutId) override;
    GENRESULT DACOM_API GetGroupMaterialCount(unsigned long GroupId, int* OutCount) override;
    GENRESULT DACOM_API EnumGroupMaterial(unsigned long GroupId, unsigned long Index,
                                           IMaterial** Out) override;
    GENRESULT DACOM_API SetGlobalRecord(unsigned long Index,
                                         const unsigned long* Value) override;
    GENRESULT DACOM_API SetGlobalRecordValue(unsigned long Index,
                                              const unsigned long* Value) override;
    GENRESULT DACOM_API GetGlobalRecordValue(unsigned long Index,
                                              unsigned long* OutValue) override;
    FMaterialLibraryRecord* DACOM_API CreateAfterMap(const char* Name, const char* Type,
                                       unsigned char bRemap, IDAComponent* AggregateOuter) override;
    FMaterialLibraryRecord* CreateGenericRecord(const char* Name, const char* Type,
                                       unsigned char bRemap, IDAComponent* AggregateOuter);

    FLibraryAggregate Aggregate; // +4
    unsigned long RefCount;
    IDAComponent* Outer;
    class FMaterialBatcher* OwnerBatcher;
    std::vector<FMaterialMapRule> MapRules;
    std::map<unsigned long, FCrcSlot> CrcMap;
    std::map<unsigned long, FMaterialGroup> Groups;
    unsigned long NextGroupId;
    char RemapScratch[64];
    FMaterialLibraryController Controller;
    IDAComponent* ControllingUnknown; // standalone +44, distinct from base Outer+40

    GENRESULT ClearRuntime();
    GENRESULT InitializeBase(IDAComponent* InOuter);
    GENRESULT InitializeFromDescriptor(const FMaterialAggregateDesc* Desc);
    GENRESULT ParseMaterialMap();
    const char* ResolveType(const char* Name, const char* Type);
    FCrcSlot* FindByCrc(unsigned long Crc);
    FMaterialGroup* FindGroup(unsigned long Id);
    void StoreCrc(unsigned long Crc, IMaterial* Material);
};
