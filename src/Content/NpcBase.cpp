// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU20 (06eb6c00-06eb7360): base class of the space objects Content tracks
// (see NpcBase.h).

#include "NpcBase.h"
#include "ContentPlayerState.h"
#include "SystemInfo.h"
#include "ContentStaticIds.h"   // TU20's 16 static-init thunks 06eb6c00-06eb6de0
#include "common_server.h"
#include "server_cobject.h"     // pub::SpaceObj::Destroy, DestroyType

// FUN_06eb7260 (06eb7260-06eb7279): +0/+4/+8 = 0, byte +0xc = 0, +0x3c = 0, +0x40 = -1.
// Nickname chars past [0] are left as they are.
FContentSpaceObjParams::FContentSpaceObjParams()
{
    Nickname.Length = 0;
    Nickname.Data[0] = '\0';
}

// FUN_06f28e10 (06f28e10-06f28e25, ICF-shared releasing-handle dtor) on +0.
FContentSpaceObjParams::~FContentSpaceObjParams()
{
    if (ReputationHandle != nullptr)
    {
        ReputationHandle->Release();
        ReputationHandle = nullptr;
    }
}

// FUN_06f73680 06f736a0-06f736e2: +0 copied and AddRef'ed when non-null, +4 copied, the
// nickname as 0xd dwords (REP MOVSD from +8), +0x3c / +0x40 copied.
FContentSpaceObjParams::FContentSpaceObjParams(FContentSpaceObjParams const& Other)
    : ReputationHandle(Other.ReputationHandle)
    , Base(Other.Base)
    , Nickname(Other.Nickname)
    , System(Other.System)
    , HitPoints(Other.HitPoints)
{
    if (ReputationHandle != nullptr)
        ReputationHandle->AddRef();
}

// FUN_06eb7190 (06eb7190-06eb7204). The attacker loop (06eb71b6-06eb71ce) writes the five
// dwords and the two bytes of each slot, not the padding; the nickname is a 0x34-byte copy
// (REP MOVSD, 0xd dwords from Params+8); the vector copies the allocator byte only.
FContentSpaceObj::FContentSpaceObj(FContentSpaceObjParams const* Params)
    : SpaceObjId(0)
    , ReputationHandle(Params->ReputationHandle)
    , Base(Params->Base)
    , System(Params->System)
    , Nickname(Params->Nickname)
{
    for (int Index = 0; Index < 8; ++Index)
    {
        FSpaceObjAttacker& Slot = Attackers[Index];
        Slot.AttackerId = 0;
        Slot.Hostility = 0.0f;
        Slot.FirstTime = 0.0f;
        Slot.LastTime = 0.0f;
        Slot.HostilityTime = 0.0f;
        Slot.bWarned = 0;
        Slot.bProvoked = 0;
    }
}

// FUN_06eb7230 (06eb7230-06eb725d): frees the +0x108 buffer and zeroes its three
// pointers; ReputationHandle is not released here.
FContentSpaceObj::~FContentSpaceObj() = default;

// FUN_06eb6e00 (06eb6e00-06eb6e8f). Linear search of PendingComms for Key; on the first
// match the system's comm wrapper is called and the element is erased (the tail is
// shifted down by 0x10 and +0x110 decremented).
void FContentSpaceObj::SendPendingComm(unsigned int Unused1, unsigned int Key, unsigned int Unused3)
{
    (void)Unused1;
    (void)Unused3;
    for (std::vector<FSpaceObjPendingComm>::iterator It = PendingComms.begin();
         It != PendingComms.end(); ++It)
    {
        if (It->Key == Key)
        {
            // 06eb6e28-06eb6e42: PUSH 0.5f, 0, 0, 0, [+4], [+8], SpaceObjId; ECX = System.
            System->SendComm(SpaceObjId, It->TargetId, It->TextId, 0, 0, nullptr, 0.5f);
            PendingComms.erase(It);
            return;
        }
    }
}

// FUN_06eb7070 (06eb7070-06eb707f): handle ? FUN_06eb6bb0(handle) : -1.
unsigned int FContentSpaceObj::GetAffiliation() const
{
    if (ReputationHandle != nullptr)
        return ReputationHandle->GetAffiliation();
    return 0xffffffffu;
}

// FUN_06eb7080 (06eb7080-06eb708f): handle ? FUN_06f33f80(handle) : -1.
int FContentSpaceObj::GetReputation() const
{
    if (ReputationHandle != nullptr)
        return ReputationHandle->GetReputation();
    return -1;
}

// FUN_06eb7090 (06eb7090-06eb7097).
void FContentSpaceObj::SetSystem(FSystemInfo* InSystem)
{
    System = InSystem;
}

// FUN_06eb70a0 (06eb70a0-06eb70e9): GetLocation into two locals (status ignored), then
// the matrix (9 dwords) and the position are copied out.
void FContentSpaceObj::GetPlacement(FSpaceObjPlacement* Out) const
{
    Vector Position;
    Matrix Orientation;
    pub::SpaceObj::GetLocation(SpaceObjId, Position, Orientation);
    Out->Orientation = Orientation;
    Out->Position = Position;
}

// FUN_06eb70f0 (06eb70f0-06eb7124).
void FContentSpaceObj::GetPosition(Vector* Out) const
{
    Vector Position;
    Matrix Orientation;
    pub::SpaceObj::GetLocation(SpaceObjId, Position, Orientation);
    *Out = Position;
}

// FUN_06eb7130 (06eb7130-06eb7162).
void FContentSpaceObj::GetOrientation(Matrix* Out) const
{
    Vector Position;
    Matrix Orientation;
    pub::SpaceObj::GetLocation(SpaceObjId, Position, Orientation);
    *Out = Orientation;
}

// FUN_06eb7170 (06eb7170-06eb7187): GetMotion(+4, linear, angular), status dropped.
void FContentSpaceObj::GetMotion(Vector& OutLinear, Vector& OutAngular) const
{
    pub::SpaceObj::GetMotion(SpaceObjId, OutLinear, OutAngular);
}

// FUN_06eb7280 (06eb7280-06eb7304). One pass over the eight slots, starting from
// MinTime = FLT_MAX (DAT_06fb4650 = 0x7f7fffff):
//   - AttackerId match: *Out = slot, return true (slot untouched);
//   - AttackerId == 0: EmptyIndex = index (the last free slot wins);
//   - `FCOM [slot+0xc]; TEST AH,0x41; JNZ` skips when MinTime <= LastTime (or unordered);
//     the fall-through (MinTime > LastTime) takes this slot as the oldest.
// Without a match the last free slot, else the oldest, is zeroed and returned.
bool FContentSpaceObj::FindOrAllocAttacker(unsigned int AttackerId, FSpaceObjAttacker** Out)
{
    float MinTime = 3.40282347e+38f;
    int OldestIndex = -1;
    int EmptyIndex = -1;
    for (int Index = 0; Index < 8; ++Index)
    {
        FSpaceObjAttacker& Slot = Attackers[Index];
        if (Slot.AttackerId == AttackerId)
        {
            *Out = &Slot;
            return true;
        }
        if (Slot.AttackerId == 0)
            EmptyIndex = Index;
        if (MinTime > Slot.LastTime)
        {
            OldestIndex = Index;
            MinTime = Slot.LastTime;
        }
    }

    int Chosen = EmptyIndex;
    if (Chosen == -1)
        Chosen = OldestIndex;
    FSpaceObjAttacker* Slot = &Attackers[0] + Chosen;
    *Out = Slot;
    Slot->AttackerId = 0;
    Slot->Hostility = 0.0f;
    Slot->FirstTime = 0.0f;
    Slot->LastTime = 0.0f;
    Slot->HostilityTime = 0.0f;
    Slot->bWarned = 0;
    Slot->bProvoked = 0;
    return true;
}

// FUN_06f34670 (06f34670-06f3467f): Destroy(+4, 0); the result is dropped.
void FContentSpaceObj::DestroySpaceObj()
{
    pub::SpaceObj::Destroy(SpaceObjId, DESTROY_NORMAL);
}
