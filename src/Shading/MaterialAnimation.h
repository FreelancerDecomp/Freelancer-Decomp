// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "ShadingInternal.h"

// Implementations of the animation interfaces declared in render/shading.h.

class FImaData final : public IImaData
{
public:
    FImaData();
    ~FImaData();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    FMaKeyBlock* DACOM_API KeyBlock() override;
    void DACOM_API CrcOut(unsigned long* Out) override;

    unsigned long NameCrc; // +4
    FMaKeyBlock Keys;      // +8
    unsigned long RefCount; // +18
};

static_assert(offsetof(FImaData, NameCrc) == 0x04, "IMAData CRC +4");
static_assert(offsetof(FImaData, Keys) == 0x08, "IMAData keys +8");
static_assert(offsetof(FImaData, RefCount) == 0x18, "IMAData ref +18");

class FImaInstance final : public IImaInstance
{
public:
    FImaInstance();
    ~FImaInstance();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    void AttachData(FImaData* NewData);
    FMaKeyBlock* DACOM_API GetKeyBlock() override;
    void DACOM_API ResetTime() override;
    void DACOM_API Tick(float Dt) override;
    unsigned char DACOM_API FillUvMatrix(float* Out) override;

    FImaData* Data;        // +4
    float Time;            // +8
    unsigned long Frame;   // +C
    FImaInstance* Next;    // +10 DAT_06ed57e4 walk
    FImaInstance* Prev;    // +14
    unsigned long RefCount; // +18
};

static_assert(offsetof(FImaInstance, Data) == 0x04, "IMAInstance data +4");
static_assert(offsetof(FImaInstance, Time) == 0x08, "IMAInstance time +8");
static_assert(offsetof(FImaInstance, Frame) == 0x0C, "IMAInstance frame +C");
static_assert(offsetof(FImaInstance, Next) == 0x10, "IMAInstance next +10");
static_assert(offsetof(FImaInstance, Prev) == 0x14, "IMAInstance prev +14");
static_assert(offsetof(FImaInstance, RefCount) == 0x18, "IMAInstance ref +18");

class FMaterialAnimation;

// Retail non-delegating IUnknown at MaterialAnimation+0x20 (LAB_06ec8020 / PTR_LAB_06ed2b68).
// Factory writes this to AGGDESC InnerOut when Outer is set — not the Aggregate face at +4.
class FMaterialAnimationController final : public IDAComponent
{
public:
    explicit FMaterialAnimationController(FMaterialAnimation* InOwner);

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    FMaterialAnimation* Owner;
};

class FMatAnimAggregate final : public IAggregateComponent
{
public:
    FMatAnimAggregate();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    GENRESULT DACOM_API Initialize() override;

    FMaterialAnimation* Owner;
};

struct FCrcAnimSlot
{
    unsigned long Crc;
    FImaData* Data;
};

class FMaterialAnimation final : public IMatAnimLibrary
{
public:
    FMaterialAnimation();
    ~FMaterialAnimation();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    void* DACOM_API LoadAnimationGroup(void* FileSystem) override;
    GENRESULT DACOM_API UnloadAnimationGroup(void* Arg) override;
    GENRESULT DACOM_API CreateAnimationInstance(int Unused, void* Query, IImaInstance** OutInstance) override;
    GENRESULT DACOM_API TickAnimationInstances(int Filter, float Dt) override;

    FMatAnimAggregate Aggregate; // +4
    unsigned long RefCount;
    IDAComponent* Outer; // retail controlling unknown at +0x1C
    FMaterialAnimationController Controller; // retail non-delegating unknown @ +0x20
    std::vector<FCrcAnimSlot> CrcMap;

    FImaData* FindData(unsigned long Crc) const;
    void StoreData(unsigned long Crc, FImaData* Data);
};
