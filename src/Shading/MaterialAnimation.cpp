// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MaterialAnimation.h"

static FImaInstance* GImaHead = nullptr;
static FImaInstance* GImaTail = nullptr;
static unsigned long GImaCount = 0;

struct FMatAnimLoadEntry
{
    unsigned long Crc;
    FImaData* Data;
};

struct FMatAnimLoadGroup
{
    std::vector<FMatAnimLoadEntry> Entries;
};

static void LinkInstance(FImaInstance* Inst)
{
    Inst->Next = nullptr;
    Inst->Prev = GImaTail;
    if (GImaTail != nullptr)
        GImaTail->Next = Inst;
    else
        GImaHead = Inst;
    GImaTail = Inst;
    ++GImaCount;
}

static void UnlinkInstance(FImaInstance* Inst)
{
    if (Inst->Prev != nullptr)
        Inst->Prev->Next = Inst->Next;
    else
        GImaHead = Inst->Next;
    if (Inst->Next != nullptr)
        Inst->Next->Prev = Inst->Prev;
    else
        GImaTail = Inst->Prev;
    // Preserve the dying instance links through Data->Release; decrement once after unlinking.
    --GImaCount;
}

static void ClearKeyBlock(FMaKeyBlock* Block)
{
    if (Block == nullptr)
        return;
    // These buffers use raw byte allocation, not array new.
    if (Block->Deltas != nullptr)
    {
        operator delete(Block->Deltas);
        Block->Deltas = nullptr;
    }
    if (Block->Keys != nullptr)
    {
        operator delete(Block->Keys);
        Block->Keys = nullptr;
    }
    Block->Count = 0;
    Block->Flags = 0;
}

static void InitializeMaFileDesc(FFileSystemDesc* Desc, const char* Name)
{
    std::memset(Desc, 0, sizeof(*Desc));
    Desc->Size = 0x34;
    Desc->ComponentName = "FileSystem";
    Desc->Path = Name;
    Desc->Access = 0x80000000;
    Desc->ShareMode = 1;
    Desc->Creation = 3;
    Desc->Flags = 0x08000080;
    Desc->Field30 = 0xFFFFFFFF;
}

static void ParseMaKeys(void* FileSystem, FMaKeyBlock* Block)
{
    // FUN_06ec6a60..6cb3: scalar reads commit only on success with four bytes;
    // array reads retain their published allocation regardless of read result/size.
    IFileSystem* Fs = AsFileSystem(FileSystem);
    FFileSystemDesc Desc;
    unsigned long BytesRead;
    unsigned long Scalar;
    InitializeMaFileDesc(&Desc, "MACount");
    int Handle = Fs->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        const int ReadResult = Fs->Read(Handle, &Scalar, 4, &BytesRead, 0);
        if (ReadResult != 0 && BytesRead == 4)
            Block->Count = Scalar;
        Fs->CloseFile(Handle);
    }
    if (Block->Count == 0)
        return;

    InitializeMaFileDesc(&Desc, "MAFlags");
    Handle = Fs->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        const int ReadResult = Fs->Read(Handle, &Scalar, 4, &BytesRead, 0);
        if (ReadResult != 0 && BytesRead == 4)
            Block->Flags = Scalar;
        Fs->CloseFile(Handle);
    }

    unsigned long DeltaCount = Block->Count;
    InitializeMaFileDesc(&Desc, "MADeltas");
    Handle = Fs->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        if (DeltaCount == 0)
            DeltaCount = Fs->GetSize(Handle, 0) / 0x14;
        const unsigned long DeltaBytes = DeltaCount * 0x14;
        // The wrapped byte size may be smaller than a whole array of records.
        FMaDelta* Deltas = (FMaDelta*)operator new(DeltaBytes);
        if (Deltas != nullptr)
        {
            Block->Deltas = Deltas;
            Fs->Read(Handle, Deltas, DeltaBytes, &BytesRead, 0);
        }
        Fs->CloseFile(Handle);
    }

    // The MAKeys branch is independent of MADeltas open/allocation/read success.
    if (Block->Count <= 1)
        return;
    unsigned long KeyCount = Block->Count - 1;
    InitializeMaFileDesc(&Desc, "MAKeys");
    Handle = Fs->OpenByDesc(&Desc);
    if (Handle != -1)
    {
        if (KeyCount == 0)
            KeyCount = Fs->GetSize(Handle, 0) >> 4;
        const unsigned long KeyBytes = KeyCount << 4;
        FMaKey* Keys = (FMaKey*)operator new(KeyBytes);
        if (Keys != nullptr)
        {
            Block->Keys = Keys;
            Fs->Read(Handle, Keys, KeyBytes, &BytesRead, 0);
        }
        Fs->CloseFile(Handle);
    }
}

FImaData::FImaData() : NameCrc(0), RefCount(1)
{
    Keys.Count = 0;
    Keys.Flags = 0;
    Keys.Deltas = nullptr;
    Keys.Keys = nullptr;
}

FImaData::~FImaData()
{
    ClearKeyBlock(&Keys);
}

GENRESULT DACOM_API FImaData::QueryInterface(const char* Iid, void** Out)
{
    if (std::strcmp(Iid, kIidImaData) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }
    AddRef();
    *Out = (IImaData*)this;
    return GR_OK;
}

unsigned long DACOM_API FImaData::AddRef()
{
    return ++RefCount;
}

unsigned long DACOM_API FImaData::Release()
{
    if (RefCount != 0)
        --RefCount;
    const unsigned long Next = RefCount;
    if (Next == 0)
    {
        RefCount = 1;
        // Deallocate only: invoking the destructor would also free the key buffers.
        operator delete(this);
    }
    return Next;
}

FMaKeyBlock* DACOM_API FImaData::KeyBlock()
{
    return &Keys;
}

void DACOM_API FImaData::CrcOut(unsigned long* Out)
{
    *Out = NameCrc;
}

FImaInstance::FImaInstance()
    : Data(nullptr), Time(0.0f), Frame(0), Next(nullptr), Prev(nullptr), RefCount(1)
{
    LinkInstance(this);
}

FImaInstance::~FImaInstance()
{
    UnlinkInstance(this);
    if (Data != nullptr)
    {
        Data->Release();
        Data = nullptr;
    }
}

GENRESULT DACOM_API FImaInstance::QueryInterface(const char* Iid, void** Out)
{
    if (std::strcmp(Iid, kIidImaInstance) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }
    AddRef();
    *Out = (IImaInstance*)this;
    return GR_OK;
}

unsigned long DACOM_API FImaInstance::AddRef()
{
    return ++RefCount;
}

unsigned long DACOM_API FImaInstance::Release()
{
    if (RefCount != 0)
        --RefCount;
    const unsigned long Next = RefCount;
    if (Next == 0)
    {
        RefCount = 1;
        delete this;
    }
    return Next;
}

void FImaInstance::AttachData(FImaData* NewData)
{
    if (Data != nullptr)
    {
        Data->Release();
        Data = nullptr;
    }
    Data = NewData;
    if (Data != nullptr)
        Data->AddRef();
}

FMaKeyBlock* DACOM_API FImaInstance::GetKeyBlock()
{
    return Data->KeyBlock();
}

void DACOM_API FImaInstance::ResetTime()
{
    Frame = 0;
    Time = 0.0f;
}

void DACOM_API FImaInstance::Tick(float Dt)
{
    // 06ec72b7 TEST AH,0x41 / JNZ: not-greater (including unordered) returns.
    if (Data == nullptr || !(Dt > 0.0f))
        return;
    FMaKeyBlock* Block = GetKeyBlock();
    if (Block->Count == 0)
        return;
    double ComparisonTime = (double)Dt + Time;
    Time = (float)ComparisonTime; // FST, first FCOMP retains the sum.
    while (ComparisonTime > Block->Deltas[Frame].Duration)
    {
        const double RemainingTime = (double)Time - Block->Deltas[Frame].Duration;
        ++Frame;
        Time = (float)RemainingTime; // FSTP before wrap and next FCOMP.
        if (Frame >= Block->Count)
            Frame = 0;
        ComparisonTime = Time;
    }
}

unsigned char DACOM_API FImaInstance::FillUvMatrix(float* Out)
{
    // LAB_06ec7330..06ec742a: only Out==null has an early return.
    if (Out == nullptr)
        return 0;
    std::memset(Out, 0, 16 * sizeof(float));
    Out[0] = 1.0f;
    Out[5] = 1.0f;
    Out[10] = 1.0f;
    Out[15] = 1.0f;
    FMaKeyBlock* Block = Data->KeyBlock();

    // DAT_06ed1284 = float 0: Frame==0 initializes all four key values to 0.
    float KeyA = 0.0f;
    float KeyB = 0.0f;
    float KeyC = 0.0f;
    float KeyD = 0.0f;
    if (Frame != 0)
    {
        const FMaKey& Key = Block->Keys[Frame - 1];
        KeyA = Key.A;
        KeyB = Key.B;
        KeyC = Key.C;
        KeyD = Key.D;
    }
    const FMaDelta& Delta = Block->Deltas[Frame];
    // Keep x87 intermediates until their actual stores. B and D are spilled
    // to float at 06ec73d9/06ec73f3; C survives on the FPU stack through +1.
    const float ValueB = (float)((double)Delta.Field8 * Time + KeyB);
    const double ValueC = (double)Delta.FieldC * Time + KeyC;
    const float ValueD = (float)((double)Delta.Field10 * Time + KeyD);
    // 06ec73f7/73fe load and multiply A before 06ec7401 writes Out[6].
    const double ValueAProduct = (double)Delta.Field4 * Time;
    Out[6] = ValueB;
    Out[2] = (float)(ValueAProduct + KeyA);
    Out[0] = (float)(ValueC + 1.0); // DAT_06ed2bd8 = double 1
    Out[5] = (float)((double)ValueD + 1.0);
    return 1;
}

FMatAnimAggregate::FMatAnimAggregate() : Owner(nullptr) {}

GENRESULT DACOM_API FMatAnimAggregate::QueryInterface(const char* Iid, void** Out)
{
    return Owner->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FMatAnimAggregate::AddRef()
{
    return Owner->AddRef();
}

unsigned long DACOM_API FMatAnimAggregate::Release()
{
    return Owner->Release();
}

GENRESULT DACOM_API FMatAnimAggregate::Initialize()
{
    return GR_OK;
}

FMaterialAnimationController::FMaterialAnimationController(FMaterialAnimation* InOwner)
    : Owner(InOwner)
{
}

GENRESULT DACOM_API FMaterialAnimationController::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06ec8020: immutable map at 06ed2c00 has offsets 0 and 4 only.
    IDAComponent* Face;
    if (std::strcmp(Iid, Iids::MatAnimLibrary) == 0)
        Face = (IMatAnimLibrary*)Owner;
    else if (std::strcmp(Iid, Iids::AggregateComponent) == 0)
        Face = (IAggregateComponent*)&Owner->Aggregate;
    else
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }
    Face->AddRef();
    *Out = Face;
    return GR_OK;
}

unsigned long DACOM_API FMaterialAnimationController::AddRef()
{
    return ++Owner->RefCount;
}

unsigned long DACOM_API FMaterialAnimationController::Release()
{
    if (Owner->RefCount != 0)
        --Owner->RefCount;
    const unsigned long Next = Owner->RefCount;
    if (Next == 0)
    {
        FMaterialAnimation* DeleteOwner = Owner;
        DeleteOwner->RefCount = 1;
        // LAB_06ec80e0 keeps its owning-object pointer live during teardown.
        delete DeleteOwner;
    }
    return Next;
}

FMaterialAnimation::FMaterialAnimation() : RefCount(1), Outer(nullptr), Controller(this)
{
    Aggregate.Owner = this;
}

FMaterialAnimation::~FMaterialAnimation()
{
    for (size_t Index = 0; Index < CrcMap.size(); ++Index)
    {
        if (CrcMap[Index].Data != nullptr)
        {
            CrcMap[Index].Data->Release();
            CrcMap[Index].Data = nullptr; // FUN_06ec8240 clears after Release.
        }
    }
    CrcMap.clear(); // Reset the native count after releasing every payload.
}

GENRESULT DACOM_API FMaterialAnimation::QueryInterface(const char* Iid, void** Out)
{
    IDAComponent* Controlling = Outer != nullptr ? Outer : &Controller;
    return Controlling->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FMaterialAnimation::AddRef()
{
    IDAComponent* Controlling = Outer != nullptr ? Outer : &Controller;
    return Controlling->AddRef();
}

unsigned long DACOM_API FMaterialAnimation::Release()
{
    IDAComponent* Controlling = Outer != nullptr ? Outer : &Controller;
    return Controlling->Release();
}

FImaData* FMaterialAnimation::FindData(unsigned long Crc) const
{
    for (size_t Index = 0; Index < CrcMap.size(); ++Index)
    {
        if (CrcMap[Index].Crc == Crc)
            return CrcMap[Index].Data;
    }
    return nullptr;
}

void FMaterialAnimation::StoreData(unsigned long Crc, FImaData* Data)
{
    size_t Index = 0;
    while (Index < CrcMap.size() && CrcMap[Index].Crc != Crc)
        ++Index;
    if (Index == CrcMap.size())
    {
        FCrcAnimSlot Slot;
        Slot.Crc = Crc;
        Slot.Data = nullptr;
        CrcMap.push_back(Slot);
    }
    if (CrcMap[Index].Data == Data)
        return;
    if (CrcMap[Index].Data != nullptr)
    {
        CrcMap[Index].Data->Release();
        CrcMap[Index].Data = nullptr;
    }
    CrcMap[Index].Data = Data;
    if (Data != nullptr)
        Data->AddRef();
}

void* DACOM_API FMaterialAnimation::LoadAnimationGroup(void* FileSystem)
{
    // LAB_06ec6e40..7083: child filesystem creation, never parent-node mutation.
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode("MaterialAnim") == 0)
        return nullptr;

    FMatAnimLoadGroup* Group = nullptr;
    WIN32_FIND_DATAA FindData;
    const int Find = FileSystemFindFirst(FileSystem, "*.*", &FindData);
    if (Find != -1)
    {
        do
        {
            const char* ChildName = FindData.cFileName;
            if ((FindData.dwFileAttributes & 0x10) != 0 && ChildName[0] != '.')
            {
                const unsigned long Crc = DACOM_CRC::GetCRC32(ChildName);
                FImaData* Data = new FImaData();
                Data->NameCrc = Crc;
                FFileSystemDesc Desc;
                InitializeMaFileDesc(&Desc, ChildName);
                Desc.Field30 = (unsigned long)Find;
                IFileSystem* ChildFs = nullptr;
                const GENRESULT Result = Fs->CreateInstance(
                    (AGGDESC*)&Desc, (void**)&ChildFs);
                if (Result >= 0)
                {
                    // ParseMaKeys always succeeds in retail, including missing key files.
                    ParseMaKeys(ChildFs, Data->KeyBlock());
                    if (Group == nullptr)
                        Group = new FMatAnimLoadGroup();
                    StoreData(Data->NameCrc, Data);
                    FMatAnimLoadEntry Entry;
                    Entry.Crc = Data->NameCrc;
                    Entry.Data = Data;
                    Data->AddRef(); // Temporary smart-pointer source (06ec7010).
                    Group->Entries.push_back(Entry);
                    Data->AddRef(); // Copied group payload (FUN_06ec7460).
                    Data->Release(); // Temporary source (06ec7035).
                }
                Data->Release();
                if (ChildFs != nullptr)
                    ChildFs->Release();
            }
        } while (FileSystemFindNext(FileSystem, Find, &FindData) != 0);
    }
    // Retail calls OpenNode("..") after enumeration; there is no FindClose call.
    Fs->OpenNode("..");
    return Group;
}

GENRESULT DACOM_API FMaterialAnimation::UnloadAnimationGroup(void* Arg)
{
    if (Arg == nullptr || Arg == (void*)(uintptr_t)-1)
        return GR_OK;

    FMatAnimLoadGroup* Group = (FMatAnimLoadGroup*)Arg;
    // LAB_06ec70c0: remove every live CRC before releasing any group reference.
    for (size_t EntryIndex = 0; EntryIndex < Group->Entries.size(); ++EntryIndex)
    {
        unsigned long Crc;
        Group->Entries[EntryIndex].Data->CrcOut(&Crc);
        size_t MapIndex = 0;
        while (MapIndex < CrcMap.size())
        {
            if (CrcMap[MapIndex].Crc != Crc)
            {
                ++MapIndex;
                continue;
            }
            FImaData* Data = CrcMap[MapIndex].Data;
            CrcMap.erase(CrcMap.begin() + MapIndex);
            if (Data != nullptr)
                Data->Release();
        }
    }
    // FUN_06ec75d0 unlinks each group node before releasing its payload.
    while (!Group->Entries.empty())
    {
        FImaData* Data = Group->Entries.front().Data;
        Group->Entries.erase(Group->Entries.begin());
        if (Data != nullptr)
            Data->Release();
    }
    delete Group;
    return GR_OK;
}

GENRESULT DACOM_API FMaterialAnimation::CreateAnimationInstance(int /*Unused*/, void* Query, IImaInstance** OutInstance)
{
    // LAB_06ec7190..7275: lookup miss leaves the caller's output untouched.
    const unsigned long Crc = *(const unsigned long*)((const unsigned char*)Query + 4);
    size_t MapIndex = 0;
    while (MapIndex < CrcMap.size() && CrcMap[MapIndex].Crc != Crc)
        ++MapIndex;
    if (MapIndex == CrcMap.size())
        return GR_GENERIC;
    FImaInstance* Inst = new FImaInstance();
    // Reload map payload after construction, as retail does after linking the instance.
    Inst->AttachData(CrcMap[MapIndex].Data);
    *OutInstance = Inst;
    return GR_OK;
}

GENRESULT DACOM_API FMaterialAnimation::TickAnimationInstances(int Filter, float Dt)
{
    if (Filter != -1)
        return GR_OK;
    FImaInstance* Inst = GImaHead;
    while (Inst != nullptr)
    {
        Inst->Tick(Dt);
        Inst = Inst->Next;
    }
    return GR_OK;
}
