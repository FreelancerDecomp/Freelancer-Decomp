// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MaterialLibrary.h"

#include "Material.h"
#include "MaterialBatcher.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

FMaterialLibraryController::FMaterialLibraryController(FMaterialLibrary* InOwner)
    : Owner(InOwner)
{
}

GENRESULT DACOM_API FMaterialLibraryController::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06ecf100: raw map06ed2fe0 contains only offsets0/4 and a null sentinel.
    IDAComponent* Face;
    if (std::strcmp(Iid, Iids::MaterialLibrary) == 0)
        Face = Owner;
    else if (std::strcmp(Iid, Iids::AggregateComponent) == 0)
        Face = &Owner->Aggregate;
    else
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }
    Face->AddRef();
    *Out = Face;
    return GR_OK;
}

unsigned long DACOM_API FMaterialLibraryController::AddRef()
{
    // Shared ICF FUN_06ecb930. Factory f06d always installs the owning library.
    return ++Owner->RefCount;
}

unsigned long DACOM_API FMaterialLibraryController::Release()
{
    // LAB_06ecf1c0: zero never wraps, and destructor callbacks observe count one.
    if (Owner->RefCount > 0)
        --Owner->RefCount;
    const unsigned long Result = Owner->RefCount;
    if (Result == 0)
    {
        Owner->RefCount = 1;
        delete Owner;
        return 0;
    }
    return Result;
}

FLibraryAggregate::FLibraryAggregate() : Owner(nullptr) {}

GENRESULT DACOM_API FLibraryAggregate::QueryInterface(const char* Iid, void** Out)
{
    return Owner->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FLibraryAggregate::AddRef()
{
    return Owner->AddRef();
}

unsigned long DACOM_API FLibraryAggregate::Release()
{
    return Owner->Release();
}

GENRESULT DACOM_API FLibraryAggregate::Initialize()
{
    if (Owner->OwnerBatcher != nullptr)
        return Owner->OwnerBatcher->StartupFromOuter();
    return Owner->InitializeBase(Owner->Outer);
}

GENRESULT FMaterialLibrary::InitializeFromDescriptor(const FMaterialAggregateDesc* Desc)
{
    // FUN_06ecbfb0: mandatory descriptor, copy +8 outer into library +40.
    Outer = Desc->OuterAt8;
    return GR_OK;
}

GENRESULT FMaterialLibrary::InitializeBase(IDAComponent* InOuter)
{
    // FUN_06ecc1f0: the base initialization path is distinct from batcher startup.
    Outer = InOuter;
    IProfileParser* Parser = nullptr;
    const GENRESULT Result = DACOM_Acquire()->QueryInterface(
        Iids::ProfileParser, (void**)&Parser);
    if (Result < 0)
    {
        if (Parser != nullptr)
            Parser->Release();
        return (GENRESULT)-1;
    }
    ParseMaterialMap();
    if (Parser != nullptr)
        Parser->Release();
    return GR_OK;
}

FMaterialLibrary::FMaterialLibrary()
    : RefCount(1), Outer(nullptr), OwnerBatcher(nullptr), NextGroupId(1), Controller(this), ControllingUnknown(&Controller)
{
    Aggregate.Owner = this;
    RemapScratch[0] = '\0';
}

GENRESULT FMaterialLibrary::ClearRuntime()
{
    // 06ecc270: clear map rules and group payloads; retain the master map.
    MapRules.clear();
    Groups.clear();
    return (GENRESULT)-1;
}

FMaterialLibrary::~FMaterialLibrary()
{
    // 06ecbe70: container cleanup only; material payloads have no Release here.
    MapRules.clear();
    Groups.clear();
    CrcMap.clear();
}

static_assert(offsetof(FMaterialLibrary, Aggregate) == 0x04, "IAggregateComponent at library+4");

GENRESULT DACOM_API FMaterialLibrary::QueryInterface(const char* Iid, void** Out)
{
    // Dedicated +44 controller; embedded library points to its batcher face.
    return ControllingUnknown->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FMaterialLibrary::AddRef()
{
    // Dedicated +44 controller; embedded library points to its batcher face.
    return ControllingUnknown->AddRef();
}

unsigned long DACOM_API FMaterialLibrary::Release()
{
    // Dedicated +44 controller; embedded library points to its batcher face.
    return ControllingUnknown->Release();
}

GENRESULT DACOM_API FMaterialLibrary::AddMapRule(int Key, const char* Pattern, const char* TypeName)
{
    // LAB_06ecd1d0 and LAB_06ecd3e0: 0 is a name rule; 1 is a type rule.
    char Message[0x2000];
    _snprintf(Message, sizeof(Message), "Adding material map: %s:%s:%s",
              Key == 1 ? "Type" : "Name", Pattern, TypeName);
    FDUMP(0x100003, "%s(%d) : NOTICE:General:%s",
          "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\MaterialLibrary.cpp",
          0x362, Message);
    FMaterialMapRule Rule;
    char TypeCopy[64];
    std::strncpy(TypeCopy, TypeName, sizeof(TypeCopy));
    TypeCopy[sizeof(TypeCopy) - 1] = '\0';
    Rule.Key = Key;
    Rule.bNamePattern = (Key == 0);
    Rule.Expression = FMaterialRegex::Compile(Pattern);
    Rule.Pattern.assign(Pattern);
    Rule.Type.assign(TypeCopy);
    MapRules.insert(MapRules.begin(), std::move(Rule));
    return GR_OK;
}

GENRESULT DACOM_API FMaterialLibrary::FindMapRule(int Key, const char* Pattern, void* /*Unused*/,
                                                  char* OutType)
{
    for (size_t Index = 0; Index < MapRules.size(); ++Index)
    {
        const FMaterialMapRule& Rule = MapRules[Index];
        if (Rule.Key == Key && std::strcmp(Rule.Pattern.c_str(), Pattern) == 0)
        {
            std::strcpy(OutType, Rule.Type.c_str());
            return GR_OK;
        }
    }
    return GR_GENERIC;
}

GENRESULT DACOM_API FMaterialLibrary::RemapType(const char* Name, const char* Type,
                                                void* /*Unused*/, char* OutType)
{
    for (size_t Index = 0; Index < MapRules.size(); ++Index)
    {
        const FMaterialMapRule& Rule = MapRules[Index];
        if (Rule.Key != 0 && Rule.Key != 1)
            continue;
        const char* Candidate = Rule.bNamePattern ? Name : Type;
        if (!FMaterialRegex::Search(Rule.Expression.get(), Candidate))
            continue;
        std::strcpy(OutType, Rule.Type.c_str());
        return GR_OK;
    }
    return (GENRESULT)-1;
}

GENRESULT DACOM_API FMaterialLibrary::RemoveMapRule(int Key, const char* Pattern)
{
    for (size_t Index = 0; Index < MapRules.size(); ++Index)
    {
        const FMaterialMapRule& Rule = MapRules[Index];
        if (Rule.Key != Key || std::strcmp(Rule.Pattern.c_str(), Pattern) != 0)
            continue;
        MapRules.erase(MapRules.begin() + (long)Index);
        return GR_OK;
    }
    return GR_GENERIC;
}

GENRESULT DACOM_API FMaterialLibrary::AllocateGroup(unsigned long* OutId)
{
    FMaterialGroup& Group = Groups[NextGroupId];
    Group.Id = NextGroupId;
    Group.Name.clear();
    Group.bHasName = false;
    Group.Members.clear();
    *OutId = NextGroupId;
    ++NextGroupId;
    return GR_OK;
}

GENRESULT DACOM_API FMaterialLibrary::LoadFromFileSystem(void* FileSystem, void* Extra)
{
    unsigned long* OutGroupId = (unsigned long*)Extra;
    const char* SourceFile = "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\MaterialLibrary.cpp";
    if (FileSystem == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", SourceFile, 0x195,
              "IFileSystem argument is NULL in MaterialLibrary::load_library");
        return (GENRESULT)-1;
    }
    if (FileSystemOpenNode(FileSystem, "Material library") == 0)
    {
        if (OutGroupId != nullptr)
            *OutGroupId = (unsigned long)-1;
        return (GENRESULT)-1;
    }

    FFileSystemDesc Desc = {};
    Desc.Size = 0x34;
    Desc.ComponentName = "FileSystem";
    Desc.Path = "Material count";
    Desc.Access = 0x80000000;
    Desc.ShareMode = 1;
    Desc.Creation = 3;
    Desc.Flags = 0x08000080;
    Desc.Field30 = 0xFFFFFFFF;
    const int CountHandle = FileSystemOpenByDesc(FileSystem, &Desc);
    if (CountHandle != -1)
    {
        unsigned long MaterialCount;
        unsigned long BytesRead;
        FileSystemRead(FileSystem, (unsigned long)CountHandle,
                       &MaterialCount, 4, &BytesRead);
        FileSystemClose(FileSystem, (unsigned long)CountHandle);
    }

    std::vector<unsigned long> LoadedMembers;
    WIN32_FIND_DATAA FindData;
    const int FindHandle = FileSystemFindFirst(FileSystem, "*.*", &FindData);
    if (FindHandle != -1)
    {
        do
        {
            const char* ChildName = FindData.cFileName;
            if (ChildName[0] == '.' ||
                (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                continue;
            const unsigned long Crc = DACOM_CRC::GetCRC32(ChildName);
            FMaterialLibraryRecord* Slot = FindByCrc(Crc);
            if (Slot != nullptr)
            {
                if (OutGroupId != nullptr)
                {
                    ++Slot->RefCount;
                    Slot->Material->AddRef();
                    LoadedMembers.push_back(Crc);
                }
                continue;
            }
            if (FileSystemOpenNode(FileSystem, ChildName) == 0)
                continue;

            Desc.Path = "Type";
            const int TypeHandle = FileSystemOpenByDesc(FileSystem, &Desc);
            char Type[0x104];
            bool bTypeRead = false;
            if (TypeHandle != -1)
            {
                unsigned long Size = FileSystemGetSize(FileSystem,
                    (unsigned long)TypeHandle);
                if (Size >= 0x103)
                    Size = 0x103;
                unsigned long BytesRead;
                const int ReadResult = FileSystemRead(FileSystem,
                    (unsigned long)TypeHandle, Type, Size, &BytesRead);
                if (ReadResult != 0 && BytesRead == Size)
                {
                    Type[BytesRead] = '\0';
                    bTypeRead = true;
                }
                FileSystemClose(FileSystem, (unsigned long)TypeHandle);
            }
            if (bTypeRead)
            {
                Slot = CreateAfterMap(ChildName, Type, 1, nullptr);
                if (Slot != nullptr)
                {
                    if (Slot->Material->LoadFromFileSystem(FileSystem) >= 0)
                    {
                        Slot->Material->SetName(ChildName);
                        if (OutGroupId != nullptr)
                        {
                            ++Slot->RefCount;
                            Slot->Material->AddRef();
                            LoadedMembers.push_back(Crc);
                        }
                    }
                    else
                    {
                        char Message[0x2000];
                        std::snprintf(Message, sizeof(Message),
                            "Unable to load material '%s' as type '%s'\n", ChildName, Type);
                        FDUMP(0x100003, "%s(%d) : NOTICE:General:%s",
                              SourceFile, 0x1C1, Message);
                        unsigned long Record[2] = {
                            (unsigned long)ChildName, Crc};
                        RemoveMaterial(0, Record);
                    }
                }
            }
            FileSystemOpenNode(FileSystem, "..");
        }
        while (FileSystemFindNext(FileSystem, FindHandle, &FindData) != 0);
        FileSystemFindClose(FileSystem, FindHandle);
    }
    FileSystemOpenNode(FileSystem, "..");
    if (OutGroupId != nullptr)
    {
        FMaterialGroup* Group = &Groups[NextGroupId];
        Group->Id = NextGroupId;
        Group->Name.clear();
        Group->bHasName = false;
        Group->Members = LoadedMembers;
        *OutGroupId = NextGroupId;
        ++NextGroupId;
    }
    return GR_OK;
}

GENRESULT DACOM_API FMaterialLibrary::UnsupportedOperation(void* /*Arg1*/, void* /*Arg2*/)
{
    return (GENRESULT)0xFFFFFFF9u;
}

GENRESULT DACOM_API FMaterialLibrary::DeleteGroup(int GroupId)
{
    if (GroupId == -1)
        return (GENRESULT)-1;
    if (GroupId == 0)
    {
        std::map<unsigned long, FCrcSlot>::iterator It = CrcMap.begin();
        while (It != CrcMap.end())
        {
            FCrcSlot& Slot = It->second;
            if ((int)Slot.RefCount <= 1)
            {
                --Slot.RefCount;
                Slot.Material->Release();
                Slot.Material = nullptr;
                It = CrcMap.erase(It);
            }
            else
            {
                ++It;
            }
        }
        return GR_OK;
    }
    FMaterialGroup* Group = FindGroup((unsigned long)GroupId);
    if (Group == nullptr)
        return (GENRESULT)-1;
    for (size_t Index = 0; Index < Group->Members.size(); ++Index)
    {
        FCrcSlot* Slot = FindByCrc(Group->Members[Index]);
        if (Slot == nullptr)
            continue;
        if ((int)Slot->RefCount > 1)
        {
            --Slot->RefCount;
            Slot->Material->Release();
        }
        if ((int)Slot->RefCount <= 1)
        {
            --Slot->RefCount;
            Slot->Material->Release();
            Slot->Material = nullptr;
            CrcMap.erase(Group->Members[Index]);
        }
    }
    Groups.erase((unsigned long)GroupId);
    return GR_OK;
}

GENRESULT DACOM_API FMaterialLibrary::ApplyVerifyGroup(int GroupId, void* Context, void* Extra)
{
    if (GroupId == -1)
        return (GENRESULT)-1;
    if (GroupId != 0)
    {
        FMaterialGroup* Group = FindGroup((unsigned long)GroupId);
        if (Group == nullptr)
            return GR_OK;
        for (size_t Index = 0; Index < Group->Members.size(); ++Index)
        {
            FCrcSlot* Slot = FindByCrc(Group->Members[Index]);
            if (Slot != nullptr)
                Slot->Material->ApplyVerify(Context, Extra);
        }
        return GR_OK;
    }
    for (std::map<unsigned long, FCrcSlot>::iterator It = CrcMap.begin();
         It != CrcMap.end(); ++It)
    {
        It->second.Material->ApplyVerify(Context, Extra);
    }
    return GR_OK;
}

GENRESULT DACOM_API FMaterialLibrary::ResolveTexturesGroup(int GroupId, float Delta)
{
    if (GroupId == -1)
        return (GENRESULT)-1;
    if (GroupId != 0)
    {
        FMaterialGroup* Group = FindGroup((unsigned long)GroupId);
        if (Group == nullptr)
            return GR_OK;
        for (size_t Index = 0; Index < Group->Members.size(); ++Index)
        {
            FCrcSlot* Slot = FindByCrc(Group->Members[Index]);
            if (Slot != nullptr)
                Slot->Material->ResolveTextures(Delta);
        }
        return GR_OK;
    }
    for (std::map<unsigned long, FCrcSlot>::iterator It = CrcMap.begin();
         It != CrcMap.end(); ++It)
    {
        It->second.Material->ResolveTextures(Delta);
    }
    return GR_OK;
}

GENRESULT DACOM_API FMaterialLibrary::SetGroupName(int GroupId, const char* Name)
{
    if (GroupId == -1 || GroupId == 0)
        return (GENRESULT)-1;
    FMaterialGroup* Group = FindGroup((unsigned long)GroupId);
    if (Group == nullptr)
        return (GENRESULT)-1;
    Group->Name.clear();
    Group->bHasName = false;
    if (Name != nullptr)
    {
        Group->Name = Name;
        Group->bHasName = true;
    }
    return GR_OK;
}

GENRESULT DACOM_API FMaterialLibrary::GetGroupName(int GroupId, char* Buffer, unsigned long Size)
{
    if (GroupId == -1 || GroupId == 0)
        return (GENRESULT)-1;
    FMaterialGroup* Group = FindGroup((unsigned long)GroupId);
    if (Group == nullptr)
        return (GENRESULT)-1;
    if (Group->bHasName)
    {
        std::strncpy(Buffer, Group->Name.c_str(), Size);
        Buffer[Size - 1] = '\0';
    }
    else
    {
        Buffer[0] = '\0';
    }
    return GR_OK;
}

FCrcSlot* FMaterialLibrary::FindByCrc(unsigned long Crc)
{
    const std::map<unsigned long, FCrcSlot>::iterator Found = CrcMap.find(Crc);
    return Found == CrcMap.end() ? nullptr : &Found->second;
}

void FMaterialLibrary::StoreCrc(unsigned long Crc, IMaterial* Material)
{
    // 06ecc42d..06ecc449: store the material, take its reference, set count=1.
    FCrcSlot& Slot = CrcMap[Crc];
    Slot.Crc = Crc;
    Slot.Material = Material;
    if (Material != nullptr)
        Material->AddRef();
    Slot.RefCount = 1;
}

FMaterialGroup* FMaterialLibrary::FindGroup(unsigned long Id)
{
    const std::map<unsigned long, FMaterialGroup>::iterator It = Groups.find(Id);
    return It == Groups.end() ? nullptr : &It->second;
}

const char* FMaterialLibrary::ResolveType(const char* Name, const char* Type)
{
    const char* Candidate = (Type != nullptr) ? Type : Name;
    for (size_t Index = 0; Index < MapRules.size(); ++Index)
    {
        const FMaterialMapRule& Rule = MapRules[Index];
        if (Rule.bNamePattern)
        {
            if (FMaterialRegex::Search(Rule.Expression.get(), Name))
                return Rule.Type.c_str();
        }
        else if (Rule.Key == 1 && FMaterialRegex::Search(Rule.Expression.get(), Candidate))
        {
            return Rule.Type.c_str();
        }
    }
    return Candidate;
}

GENRESULT DACOM_API FMaterialLibrary::FindOrCreate(const char* Name, const char* Type,
                                                  IDAComponent* AggregateOuter, IMaterial** Out)
{
    const unsigned long Crc = DACOM_CRC::GetCRC32(Name);
    FMaterialLibraryRecord* Record = FindByCrc(Crc);
    if (Record == nullptr)
    {
        Record = CreateAfterMap(Name, Type, 1, AggregateOuter);
        if (Record == nullptr)
            return (GENRESULT)-1;
    }
    *Out = Record->Material;
    if (Record->Material != nullptr)
        Record->Material->AddRef();
    return GR_OK;
}

GENRESULT DACOM_API FMaterialLibrary::FindMaterialByCrc(void* Query, IMaterial** Out)
{
    // LAB_06ecd5c0: a miss leaves the caller's output untouched.
    const unsigned long Crc = ((unsigned long*)Query)[1];
    FCrcSlot* Slot = FindByCrc(Crc);
    if (Slot == nullptr)
        return (GENRESULT)-1;
    *Out = Slot->Material;
    Slot->Material->AddRef();
    return GR_OK;
}

GENRESULT DACOM_API FMaterialLibrary::AddMaterial(int GroupId, void* Record, IMaterial* Material)
{
    const unsigned long Crc = ((unsigned long*)Record)[1];
    FMaterialGroup* Group = nullptr;
    if (GroupId != 0)
    {
        Group = FindGroup((unsigned long)GroupId);
        if (Group == nullptr)
            return (GENRESULT)-1;
        // The retail group append precedes the master lookup and its error path.
        Group->Members.push_back(Crc);
    }
    FCrcSlot* Slot = FindByCrc(Crc);
    if (Slot != nullptr)
    {
        if (Group == nullptr)
            return (GENRESULT)-1;
        ++Slot->RefCount;
        Slot->Material->AddRef();
        return GR_OK;
    }
    StoreCrc(Crc, Material);
    if (Group != nullptr)
    {
        Slot = FindByCrc(Crc);
        Slot->RefCount = 2;
        Slot->Material->AddRef();
    }
    return GR_OK;
}

GENRESULT DACOM_API FMaterialLibrary::RemoveMaterial(unsigned long GroupId, void* Record)
{
    const unsigned long Crc = ((unsigned long*)Record)[1];
    FCrcSlot* Slot = FindByCrc(Crc);
    GENRESULT Result = GR_OK;
    if (GroupId != 0)
    {
        FMaterialGroup* Group = FindGroup(GroupId);
        if (Group == nullptr)
        {
            Result = (GENRESULT)-1;
        }
        else
        {
            for (size_t Index = 0; Index < Group->Members.size(); ++Index)
            {
                if (Group->Members[Index] != Crc)
                    continue;
                Group->Members.erase(Group->Members.begin() + Index);
                if (Slot != nullptr)
                {
                    if ((int)Slot->RefCount <= 1)
                    {
                        CrcMap.erase(Crc);
                        return Result;
                    }
                    --Slot->RefCount;
                    Slot->Material->Release();
                }
                break;
            }
        }
    }
    else if (Slot != nullptr)
    {
        --Slot->RefCount;
        Slot->Material->Release();
        Slot->Material = nullptr;
    }
    // 06ecd7c7 unconditionally re-reads the resolved record, even on a miss.
    if ((int)Slot->RefCount <= 1)
        CrcMap.erase(Crc);
    return Result;
}

GENRESULT DACOM_API FMaterialLibrary::GetGroupCount(unsigned long* OutCount)
{
    // LAB_06ecd830 reads the RB-tree node count at this+0x38 and includes the
    // reserved all-materials group at ordinal zero.
    *OutCount = (unsigned long)Groups.size() + 1;
    return GR_OK;
}

GENRESULT DACOM_API FMaterialLibrary::EnumGroup(unsigned long Index, unsigned long* OutId)
{
    const unsigned long Count = (unsigned long)Groups.size() + 1;
    if (Index >= Count)
        return (GENRESULT)-1;
    if (Index == 0)
    {
        *OutId = 0;
        return GR_OK;
    }
    std::map<unsigned long, FMaterialGroup>::iterator It = Groups.begin();
    for (unsigned long Ordinal = 1; Ordinal < Index; ++Ordinal)
        ++It;
    *OutId = It->second.Id;
    return GR_OK;
}

GENRESULT DACOM_API FMaterialLibrary::GetGroupMaterialCount(unsigned long GroupId, int* OutCount)
{
    if (GroupId == (unsigned long)-1)
        return (GENRESULT)-1;
    if (GroupId == 0)
    {
        *OutCount = (int)CrcMap.size();
        return GR_OK;
    }
    FMaterialGroup* Group = FindGroup(GroupId);
    if (Group == nullptr)
        return (GENRESULT)-1;
    *OutCount = (int)Group->Members.size();
    return GR_OK;
}

GENRESULT DACOM_API FMaterialLibrary::EnumGroupMaterial(unsigned long GroupId, unsigned long Index,
                                                         IMaterial** Out)
{
    if (GroupId == (unsigned long)-1)
        return (GENRESULT)-1;
    if (GroupId == 0)
    {
        if (Index >= CrcMap.size())
            return (GENRESULT)-1;
        std::map<unsigned long, FCrcSlot>::iterator It = CrcMap.begin();
        for (unsigned long Ordinal = 0; Ordinal < Index; ++Ordinal)
            ++It;
        *Out = It->second.Material;
        It->second.Material->AddRef();
        return GR_OK;
    }
    FMaterialGroup* Group = FindGroup(GroupId);
    if (Group == nullptr || Index >= Group->Members.size())
        return (GENRESULT)-1;
    FCrcSlot* Slot = FindByCrc(Group->Members[Index]);
    if (Slot == nullptr)
        return (GENRESULT)-1;
    *Out = Slot->Material;
    if (Slot->Material != nullptr)
        Slot->Material->AddRef();
    return GR_OK;
}

struct FGlobalMaterialRecord
{
    unsigned char bActive;
    unsigned char Padding[3];
    unsigned long Value;
};

static_assert(sizeof(FGlobalMaterialRecord) == 8, "global material record stride");

// Private game record storage, independent of the compiler's vector layout.
// Byte counts preserve 32-bit shifts/wrap; each record includes uninitialized padding.
struct FGlobalMaterialRecordBuffer
{
    unsigned char* Data;
    unsigned long SizeBytes;
    unsigned long CapacityBytes;

    unsigned long Size() const
    {
        return Data != nullptr ? (unsigned long)((long)SizeBytes >> 3) : 0;
    }

    unsigned long Capacity() const
    {
        return Data != nullptr ? (unsigned long)((long)CapacityBytes >> 3) : 0;
    }

    FGlobalMaterialRecord* Record(unsigned long Index) const
    {
        return (FGlobalMaterialRecord*)((uintptr_t)Data + Index * 8UL);
    }
};

static FGlobalMaterialRecordBuffer GGlobalMaterialRecords = {};
static unsigned char GGlobalMaterialRecordsInitialized = 0;

static void DestroyGlobalMaterialRecords()
{
    // FUN_06ecf780: release storage and clear it; the lazy guard stays set.
    delete[] GGlobalMaterialRecords.Data;
    GGlobalMaterialRecords.Data = nullptr;
    GGlobalMaterialRecords.SizeBytes = 0;
    GGlobalMaterialRecords.CapacityBytes = 0;
}

static FGlobalMaterialRecordBuffer& GlobalMaterialRecords()
{
    // FUN_06ecf670: set the guard BEFORE allocation, also on failure/reentry.
    // Register teardown only after the five records have been published.
    if ((GGlobalMaterialRecordsInitialized & 1) == 0)
    {
        GGlobalMaterialRecordsInitialized |= 1;
        GGlobalMaterialRecords.Data = nullptr;
        GGlobalMaterialRecords.SizeBytes = 0;
        GGlobalMaterialRecords.CapacityBytes = 0;
        FGlobalMaterialRecord Empty;
        Empty.bActive = 0;
        Empty.Value = (unsigned long)-1;
        unsigned char* Data = new unsigned char[5 * sizeof(FGlobalMaterialRecord)];
        for (unsigned long Index = 0; Index < 5; ++Index)
        {
            unsigned char* Destination = (unsigned char*)((uintptr_t)Data + Index * 8UL);
            if (Destination != nullptr)
                std::memcpy(Destination, &Empty, 8);
        }
        delete[] GGlobalMaterialRecords.Data;
        GGlobalMaterialRecords.CapacityBytes = 5 * 8;
        GGlobalMaterialRecords.SizeBytes = 5 * 8;
        GGlobalMaterialRecords.Data = Data;
        std::atexit(DestroyGlobalMaterialRecords);
    }
    return GGlobalMaterialRecords;
}

static unsigned char* GlobalRecordAddress(unsigned char* Data, unsigned long Offset)
{
    return (unsigned char*)((uintptr_t)Data + Offset);
}

static void CopyGlobalRecordBytes(unsigned char* Destination, const unsigned char* Source,
                                 unsigned long SizeBytes)
{
    // 69f0/fa80: two DWORDs per record, including compiler padding.
    for (unsigned long Offset = 0; Offset != SizeBytes; Offset += 8)
    {
        unsigned char* Target = GlobalRecordAddress(Destination, Offset);
        if (Target != nullptr)
            std::memcpy(Target, (const unsigned char*)((uintptr_t)Source + Offset), 8);
    }
}

static void FillGlobalRecords(unsigned char* Destination, unsigned long Count,
                              const FGlobalMaterialRecord& Value)
{
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        unsigned char* Target = GlobalRecordAddress(Destination, Index * 8UL);
        if (Target != nullptr)
            std::memcpy(Target, &Value, 8);
    }
}

static void ReserveGlobalRecords(FGlobalMaterialRecordBuffer& Records, unsigned long Capacity)
{
    // daa0..db7e: signed allocation count, unsigned 32-bit byte multiplication.
    const unsigned long AllocationCount = (long)Capacity < 0 ? 0 : Capacity;
    unsigned char* Data = new unsigned char[AllocationCount * 8UL];
    CopyGlobalRecordBytes(Data, Records.Data, Records.SizeBytes);
    delete[] Records.Data;
    Records.CapacityBytes = Capacity * 8UL;
    Records.SizeBytes = Records.Size() * 8UL;
    Records.Data = Data;
}

static void AppendGlobalRecords(FGlobalMaterialRecordBuffer& Records, unsigned long Count,
                                const FGlobalMaterialRecord& Value)
{
    // df00 has only two retail callers (dbc6, f4f9), both insert at end.
    const unsigned long Available = (unsigned long)((long)(Records.CapacityBytes - Records.SizeBytes) >> 3);
    if (Available >= Count)
    {
        FillGlobalRecords(GlobalRecordAddress(Records.Data, Records.SizeBytes), Count, Value);
        Records.SizeBytes += Count * 8UL;
        return;
    }
    const unsigned long Size = Records.Size();
    const unsigned long Growth = Size > Count ? Size : Count;
    const unsigned long Capacity = Size + Growth;
    const unsigned long AllocationCount = (long)Capacity < 0 ? 0 : Capacity;
    unsigned char* Data = new unsigned char[AllocationCount * 8UL];
    CopyGlobalRecordBytes(Data, Records.Data, Records.SizeBytes);
    FillGlobalRecords(GlobalRecordAddress(Data, Records.SizeBytes), Count, Value);
    delete[] Records.Data;
    Records.CapacityBytes = Capacity * 8UL;
    Records.SizeBytes = (Size + Count) * 8UL;
    Records.Data = Data;
}

static void ResizeGlobalRecords(FGlobalMaterialRecordBuffer& Records, unsigned long Size)
{
    const unsigned long OldSize = Records.Size();
    if (OldSize < Size)
    {
        FGlobalMaterialRecord Empty;
        Empty.bActive = 0;
        Empty.Value = (unsigned long)-1;
        AppendGlobalRecords(Records, Size - OldSize, Empty);
    }
    else if (Records.Data != nullptr && Size < OldSize)
    {
        // e150 caller passes last=end: no tail copy, only the end changes.
        Records.SizeBytes = Size * 8UL;
    }
}

void ActivateMaterialGlobalRecord(unsigned long Index)
{
    // Each f440 activation reacquires the buffer after intervening registrations.
    FGlobalMaterialRecordBuffer& Records = GlobalMaterialRecords();
    if (Records.Size() <= Index)
    {
        if (Records.Capacity() <= Index && Records.Capacity() < Index + 6)
            ReserveGlobalRecords(Records, Index + 6);
        ResizeGlobalRecords(Records, Index + 1);
    }
    FGlobalMaterialRecord* Record = Records.Record(Index);
    Record->Value = 1;
    Record->bActive = 1;
}

void InitializeMaterialGlobalRecords()
{
    ActivateMaterialGlobalRecord(2);
    ActivateMaterialGlobalRecord(1);
    ActivateMaterialGlobalRecord(3);
    ActivateMaterialGlobalRecord(0);
}

unsigned long GetMaterialGlobalRecordValue(unsigned long Index)
{
    return GlobalMaterialRecords().Record(Index)->Value;
}

GENRESULT DACOM_API FMaterialLibrary::SetGlobalRecord(unsigned long Index,
                                                       const unsigned long* Value)
{
    // Full daa0..dc05: no range/input guard, including wrapping resize/erase.
    FGlobalMaterialRecordBuffer& Records = GlobalMaterialRecords();
    if (Records.Size() <= Index)
    {
        if (Records.Capacity() <= Index && Records.Capacity() < Index + 6)
            ReserveGlobalRecords(Records, Index + 6);
        ResizeGlobalRecords(Records, Index + 1);
    }
    FGlobalMaterialRecord* Record = Records.Record(Index);
    Record->Value = *Value;
    Record->bActive = 1;
    return GR_OK;
}

GENRESULT DACOM_API FMaterialLibrary::SetGlobalRecordValue(unsigned long Index,
                                                            const unsigned long* Value)
{
    FGlobalMaterialRecordBuffer& Records = GlobalMaterialRecords();
    unsigned long* RecordValue = (unsigned long*)((uintptr_t)Records.Data +
        Index * sizeof(FGlobalMaterialRecord) + offsetof(FGlobalMaterialRecord, Value));
    // 06ecdc1c-06ecdc30: only the computed address is tested.
    if (RecordValue == nullptr)
        return (GENRESULT)-1;
    *RecordValue = *Value;
    return GR_OK;
}

GENRESULT DACOM_API FMaterialLibrary::GetGlobalRecordValue(unsigned long Index,
                                                            unsigned long* OutValue)
{
    FGlobalMaterialRecordBuffer& Records = GlobalMaterialRecords();
    const unsigned long* RecordValue = (const unsigned long*)((uintptr_t)Records.Data +
        Index * sizeof(FGlobalMaterialRecord) + offsetof(FGlobalMaterialRecord, Value));
    // 06ecdc4c-06ecdc60: the caller's output is mandatory on success.
    if (RecordValue == nullptr)
        return (GENRESULT)-1;
    *OutValue = *RecordValue;
    return GR_OK;
}

FMaterialLibraryRecord* FMaterialLibrary::CreateGenericRecord(const char* Name, const char* Type,
                                                               unsigned char bRemap,
                                                               IDAComponent* AggregateOuter)
{
    // 06ecc2d0: CRC is computed before truncating/lowercasing the name.
    const unsigned long Crc = DACOM_CRC::GetCRC32(Name);
    char NameCopy[0x40];
    std::strncpy(NameCopy, Name, sizeof(NameCopy));
    NameCopy[sizeof(NameCopy) - 1] = '\0';
    _strlwr(NameCopy);
    char TypeCopy[0x40];
    std::strncpy(TypeCopy, Type, sizeof(TypeCopy));
    TypeCopy[sizeof(TypeCopy) - 1] = '\0';
    char SelectedType[0x104];
    if (bRemap == 0 ||
        RemapType(NameCopy, TypeCopy, (void*)0x104, SelectedType) < 0)
        std::strcpy(SelectedType, TypeCopy);

    FMaterialAggregateDesc Desc = {};
    IDAComponent* Inner;
    Desc.Size = sizeof(Desc);
    Desc.NameAt4 = SelectedType;
    Desc.OuterAt8 = AggregateOuter;
    Desc.InnerOutAtC = (void**)&Inner;
    IDAComponent* Created = nullptr;
    IMaterial* Material = nullptr;
    FMaterialLibraryRecord* Record = nullptr;
    const GENRESULT Result = DACOM_Acquire()->CreateInstance(
        (AGGDESC*)&Desc, (void**)&Created);
    if (Result < 0)
    {
        char Message[0x2000];
        std::snprintf(Message, sizeof(Message),
            "Unable to create material '%s' ('%s', '%s')\n", NameCopy, TypeCopy, SelectedType);
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
            "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\MaterialLibrary.cpp",
            0x14F, Message);
        return nullptr;
    }
    if (Created->QueryInterface(Iids::Material, (void**)&Material) >= 0)
    {
        if (Material->Initialize(Outer) >= 0)
        {
            Material->SetName(NameCopy);
            StoreCrc(Crc, Material);
            Record = FindByCrc(Crc);
        }
        if (Material != nullptr)
            Material->Release();
    }
    if (Created != nullptr)
        Created->Release();
    return Record;
}

FMaterialLibraryRecord* DACOM_API FMaterialLibrary::CreateAfterMap(const char* Name, const char* Type,
                                                                unsigned char bRemap,
                                                                IDAComponent* AggregateOuter)
{
    if (OwnerBatcher == nullptr)
        return CreateGenericRecord(Name, Type, bRemap, AggregateOuter);

    // 06ec9aa0: override selects a wrapper after ordinary material remapping.
    const unsigned long Crc = DACOM_CRC::GetCRC32(Name);
    char NameCopy[0x40];
    std::strncpy(NameCopy, Name, sizeof(NameCopy));
    NameCopy[sizeof(NameCopy) - 1] = '\0';
    _strlwr(NameCopy);
    char TypeCopy[0x40];
    std::strncpy(TypeCopy, Type, sizeof(TypeCopy));
    TypeCopy[sizeof(TypeCopy) - 1] = '\0';
    char SelectedType[0x104];
    if (bRemap == 0 ||
        RemapType(NameCopy, TypeCopy, (void*)0x104, SelectedType) < 0)
        std::strcpy(SelectedType, TypeCopy);

    bool bWrap = false;
    for (size_t Index = 0; Index < OwnerBatcher->RuleList.size(); ++Index)
    {
        const FBatchRule& Rule = OwnerBatcher->RuleList[Index];
        const char* Candidate = nullptr;
        if (Rule.Kind == 1)
            Candidate = SelectedType;
        else if (Rule.Kind == 2)
            Candidate = NameCopy;
        if (FMaterialRegex::Search(Rule.Expression.get(), Candidate))
        {
            bWrap = true;
            break;
        }
    }
    if (!bWrap)
        return CreateGenericRecord(NameCopy, SelectedType, 0, AggregateOuter);

    FMaterialAggregateDesc Desc = {};
    IDAComponent* Inner;
    Desc.Size = sizeof(Desc);
    Desc.NameAt4 = "BatchedMaterial";
    Desc.OuterAt8 = AggregateOuter;
    Desc.InnerOutAtC = (void**)&Inner;
    IDAComponent* Created = nullptr;
    IMaterial* Material = nullptr;
    IBatchedMaterial* Batched = nullptr;
    FMaterialLibraryRecord* Record = nullptr;
    if (DACOM_Acquire()->CreateInstance((AGGDESC*)&Desc,
                                      (void**)&Created) >= 0)
    {
        if (Created->QueryInterface(Iids::Material, (void**)&Material) >= 0)
        {
            if (Created->QueryInterface(Iids::BatchedMaterial,
                                        (void**)&Batched) >= 0)
            {
                if (Batched->CreateInnerMaterial(SelectedType) >= 0)
                {
                    if (Material->Initialize(Outer) >= 0)
                    {
                        Material->SetName(NameCopy);
                        StoreCrc(Crc, Material);
                        Record = FindByCrc(Crc);
                    }
                }
                else
                {
                    char Message[0x2000];
                    std::snprintf(Message, sizeof(Message),
                        "Unable to set super type for '%s' (%s) [%s]\n",
                        NameCopy, TypeCopy, SelectedType);
                    FDUMP(0x100003, "%s(%d) : NOTICE:General:%s",
                        "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Shading\\Materials\\MaterialBatcher.cpp",
                        0x441, Message);
                }
                if (Batched != nullptr)
                    Batched->Release();
            }
            if (Material != nullptr)
                Material->Release();
        }
        if (Created != nullptr)
            Created->Release();
    }
    return Record;
}

GENRESULT FMaterialLibrary::ParseMaterialMap()
{
    // FUN_06ecbfe0: this parser independently acquires its own ProfileParser.
    IProfileParser* Parser = nullptr;
    const GENRESULT Result = DACOM_Acquire()->QueryInterface(
        Iids::ProfileParser, (void**)&Parser);
    if (Result >= 0)
    {
        void* Section = Parser->OpenSection("MaterialMap", 3);
        if (Section != nullptr)
        {
            char Line[0x400];
            int LineIndex = 0;
            while (Parser->ReadLine(Section, LineIndex, Line, sizeof(Line)) != 0)
            {
                char* LastEq = std::strrchr(Line, '=');
                if (LastEq != nullptr)
                {
                    char* FirstEq = std::strchr(Line, '=');
                    int Mode = 1;
                    char* Pattern = Line;
                    if (FirstEq != nullptr && FirstEq != LastEq)
                    {
                        char* Selector = Line;
                        while (*Selector == ' ' || *Selector == '\t')
                            ++Selector;
                        char* SelectorEnd = FirstEq;
                        while (*SelectorEnd != '\0' &&
                               (*SelectorEnd == '=' || *SelectorEnd == ';' ||
                                *SelectorEnd == ' ' || *SelectorEnd == '\t'))
                            --SelectorEnd;
                        SelectorEnd[1] = '\0';
                        Mode = _stricmp(Selector, "name") != 0 ? 1 : 0;
                        Pattern = FirstEq + 1;
                    }
                    while (*Pattern == ' ' || *Pattern == '\t')
                        ++Pattern;
                    char* PatternEnd = LastEq;
                    while (*PatternEnd != '\0' &&
                           (*PatternEnd == '=' || *PatternEnd == ';' ||
                            *PatternEnd == ' ' || *PatternEnd == '\t'))
                        --PatternEnd;
                    PatternEnd[1] = '\0';
                    char* Type = LastEq + 1;
                    while (*Type == ' ' || *Type == '\t')
                        ++Type;
                    char* TypeEnd = Type + std::strlen(Type) - 1;
                    while (*TypeEnd != '\0' &&
                           (*TypeEnd == ';' || *TypeEnd == ' ' || *TypeEnd == '\t'))
                        --TypeEnd;
                    TypeEnd[1] = '\0';
                    AddMapRule(Mode, Pattern, Type);
                }
                ++LineIndex;
            }
            Parser->CloseSection(Section);
        }
    }
    if (Parser != nullptr)
        Parser->Release();
    return GR_OK;
}
