// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// SubObjectID: collision-group / equipment / shield sub-object id makers and range tests.
#include "common_cobject.h"

// ============================================================================
// SubObjectID implementation
// ============================================================================

namespace SubObjectID
{
    // Common.dll.c 0xd4550: retail starts collision-group IDs at 3 (0/1/2 are
    // reserved -- see IsArchGroupID/IsEquipID/IsShieldEquipID ranges below).
    CollGroupIdMaker::CollGroupIdMaker()
        : NextId(3)
    {
    }

    unsigned short CollGroupIdMaker::CreateGroupID()
    {
        // Common.dll.c 0xd4560 [ICF-folded with CreateEquipID/CreateShieldID]:
        // `INC word ptr [this]; MOV AX,word ptr [this]` -- pre-increment, not post.
        ++NextId;
        return NextId;
    }

    void CollGroupIdMaker::Reset()
    {
        // Common.dll.c 0xd4570: `MOV word ptr [this],0x3`.
        NextId = 3;
    }

    // Common.dll.c 0xd4580: retail starts equipment IDs at 0x21 (33) -- matches
    // SubObjectID::IsEquipID's range check ([0x21, 0xffff)).
    EquipIdMaker::EquipIdMaker()
        : NextId(0x21)
    {
    }

    unsigned short EquipIdMaker::CreateEquipID()
    {
        // Common.dll.c 0xd4560 [ICF-folded]. Pre-increment, same shared body as
        // CollGroupIdMaker::CreateGroupID / ShieldIdMaker::CreateShieldID.
        ++NextId;
        return NextId;
    }

    void EquipIdMaker::Reset()
    {
        // Common.dll.c 0xd4590: `MOV word ptr [this],0x21`.
        NextId = 0x21;
    }

    // Common.dll.c 0xd45a0: retail starts shield IDs at 0xfff0 -- matches
    // SubObjectID::IsShieldEquipID's range check ([0xfff0, 0xffff)).
    ShieldIdMaker::ShieldIdMaker()
        : NextId(0xfff0)
    {
    }

    unsigned short ShieldIdMaker::CreateShieldID()
    {
        // Common.dll.c 0xd4560 [ICF-folded]. Pre-increment, same shared body as the
        // other two CreateXID methods above.
        ++NextId;
        return NextId;
    }

    void ShieldIdMaker::Reset()
    {
        // Common.dll.c 0xd45b0: `MOV word ptr [this],0xfff0`.
        NextId = 0xfff0;
    }

    bool IsArchGroupID(unsigned short Id)
    {
        // Common.dll.c 0xd44f0 (listing 06334f0): range check, NOT a bit test.
        // True for CollGroupIdMaker's range: 3 <= Id < 0x21.
        return Id >= 3 && Id < 0x21;
    }

    bool IsEquipID(unsigned short Id)
    {
        // Common.dll.c 0xd4510 (listing 06334510): range check, NOT a bit test.
        // True for EquipIdMaker's range: 0x21 <= Id < 0xffff (this also covers the
        // ShieldIdMaker sub-range -- retail callers check IsShieldEquipID first
        // when they need to distinguish shields from ordinary equipment).
        return Id >= 0x21 && Id < 0xffff;
    }

    bool IsShieldEquipID(unsigned short Id)
    {
        // Common.dll.c 0xd4530 (listing 06334530): range check, NOT a bit test.
        // True for ShieldIdMaker's range: 0xfff0 <= Id < 0xffff.
        return Id >= 0xfff0 && Id < 0xffff;
    }
}
