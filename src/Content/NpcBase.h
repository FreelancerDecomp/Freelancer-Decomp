// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU20 (06eb6c00-06eb7360): the base class of every space object Content
// tracks per system — solar NPCs (TU24, FUN_06ed5720), the TU50 ship base (FUN_06f571a0:
// NPC ships TU43 FUN_06f336a0 and the player ship TU46 FUN_06f471d0). SystemInfo (TU58)
// finds these objects by space object id (FUN_06f73a40) and by nickname (FUN_06f73990,
// name at +0xd8).
//
// Retail: vtable 06fb464c has one slot (scalar deleting dtor FUN_06eb7210), size 0x118.
// No RTTI in Content; the class name is the port's.

#include "common_ini.h"       // Vector
#include "common_csys.h"      // Matrix
#include "common_tstring.h"   // TString

#include <vector>

struct FContentPlayerStateBase;   // ContentPlayerState.h: refcounted reputation handle (FUN_06eb6be0)
struct FContentBase;              // BaseDB.h (TU17 record, FUN_06eac0c0)
struct FSystemInfo;               // SystemInfo.h (TU58)

// Construction parameters. FUN_06eb7260 (callers GameSupport 06efff90, TU43 06f33521,
// TU58 06f748f3) is the default ctor. Derived parameter blocks embed it: TU50's at +0
// (FUN_06f571a0 passes the block itself), TU24's at +0x48 (FUN_06ed5720 converts with a
// null check, i.e. a base-pointer conversion).
struct FContentSpaceObjParams
{
    FContentPlayerStateBase* ReputationHandle = nullptr;   // +0x00
    FContentBase* Base = nullptr;                          // +0x04
    // +0x08: FUN_06eb7260 zeroes Length and Name[0] only.
    TString<0x30> Nickname;                                // +0x08
    FSystemInfo* System = nullptr;                         // +0x3c
    // +0x40: -1 from FUN_06eb7260 (default hit points). Written by the mission ship spawn
    // FUN_06f15e20 (06f16650: the slot's hit_points), read by the TU43 ship create 06f3386e as
    // pub::SpaceObj::ShipInfo::HitPts.
    int HitPoints = -1;

    // FUN_06eb7260.
    FContentSpaceObjParams();
    // Every parameter block's dtor ends with FUN_06f28e10 on its +0 (TU58 06f74a3d after the
    // TU24 record is built; the TU50 block dtor FUN_06ef4ab0 06ef4b92): ReputationHandle ?
    // Release() : -, then 0. The +0 member is a releasing handle; the space object copies it
    // raw (no AddRef).
    ~FContentSpaceObjParams();
    // Copy (inlined at the head of the TU50 block copy ctor FUN_06f73680, 06f736a0-06f736e2):
    // the handle with an AddRef (vtable slot 0, no AddRef when null), the rest member-wise.
    FContentSpaceObjParams(FContentSpaceObjParams const& Other);
    FContentSpaceObjParams& operator=(FContentSpaceObjParams const&) = delete;
};

static_assert(sizeof(FContentSpaceObjParams) == 0x44, "FContentSpaceObjParams size");

// One of the eight attacker slots at +0x14 (0x18 bytes). FUN_06eb7280 finds or recycles a
// slot; TU56 FUN_06f6ff90 (06f70203 onwards, CombatReputation.cpp) fills it on the ATTACKING
// player's ship, keyed by the damaged object.
struct FSpaceObjAttacker
{
    unsigned int AttackerId;   // +0x00 the damaged object's id; 0 = free slot
    // +0x04 (float: TU56 06f702a5 / 06f702b9 FADD): hostility, +RelativeDamage * 50 per hit and
    // +10 when more than a second passed since HostilityTime.
    float Hostility;
    float FirstTime;           // +0x08 TU56: time the attacker was first recorded
    float LastTime;            // +0x0c TU56: time of the latest hit; FUN_06eb7280 recycles the lowest
    float HostilityTime;       // +0x10 TU56: time of the last +10 step
    unsigned char bWarned;     // +0x14 TU56: Hostility > 25 reported (message 0x1005, word 0 = 0)
    unsigned char bProvoked;   // +0x15 TU56: Hostility >= 50 reported (word 0 = 1)
};

static_assert(sizeof(FSpaceObjAttacker) == 0x18, "FSpaceObjAttacker size");

// +0x108 vector element (0x10 bytes): one line of a mission dialog (TU28, MissionDialogs.h:
// the +0x6c part of a dialog line). FUN_06eb6e00 matches Key and hands TargetId / TextId to
// FSystemInfo::SendComm (FUN_06f72930 -> pub::SpaceObj::SendComm). The whole vector is
// assigned by TU28 06ef28fc (StartMissionDialog) and TU58 06f7e7cd.
struct FSpaceObjPendingComm
{
    unsigned int Key;          // +0x00 the dialog line after which this one is spoken
    unsigned int TextId;       // +0x04 the line's id (pub::GetNicknameId), SendComm's TextId
    unsigned int TargetId;     // +0x08 the addressee's SpaceObj id, SendComm's TargetId
    unsigned int Field_0x0c;   // +0x0c not read by TU20 (TU28 loader writes 0)
};

static_assert(sizeof(FSpaceObjPendingComm) == 0x10, "FSpaceObjPendingComm size");

// Message 0x1001 sent to controller 0 by the ContentCallback combat_state_notification
// overrides of the solar NPC (TU24 06ed5a00) and the NPC ship (TU43 06f34090): the three
// callback arguments, then the space object's system id (FUN_06eea860()->+0x48).
struct FSpaceObjCombatStateMsg
{
    unsigned int Type;         // +0x00 CSNType
    unsigned int Param1;       // +0x04
    unsigned int Param2;       // +0x08
    unsigned int SystemId;     // +0x0c
};

static_assert(sizeof(FSpaceObjCombatStateMsg) == 0x10, "FSpaceObjCombatStateMsg size");

// FUN_06eb70a0 output (0x30 bytes): orientation first, then position.
struct FSpaceObjPlacement
{
    Matrix Orientation;        // +0x00
    Vector Position;           // +0x24
};

static_assert(sizeof(FSpaceObjPlacement) == 0x30, "FSpaceObjPlacement size");

class FContentSpaceObj
{
public:
    // FUN_06eb7190 (RET 4).
    explicit FContentSpaceObj(FContentSpaceObjParams const* Params);
    // FUN_06eb7230 (vptr reset + free the +0x108 buffer); slot 0 FUN_06eb7210.
    virtual ~FContentSpaceObj();
    // +0x04 (slot 1). Retail's base vtable 06fb464c has only the dtor: every derived class
    // declares slot 1 itself (FSolarNpc 06ed59f0 = byte +0x164, FContentShipBase 06ea1480 =
    // false, FContentNpcShip 06ea1460 = true), and TU25 06eebd10 / TU53 call it on objects
    // that FindShipOrSolar / the SpaceObjs list give as the base type. The base is never
    // created on its own, so the port declares it here, pure. Name unknown.
    virtual bool Slot04() = 0;

    // FUN_06eb6e00 (RET 0xc; only the second argument is read).
    void SendPendingComm(unsigned int Unused1, unsigned int Key, unsigned int Unused3);
    // FUN_06eb7070: affiliation of the reputation handle, -1 without one.
    unsigned int GetAffiliation() const;
    // FUN_06eb7080: reputation of the handle, -1 without one.
    int GetReputation() const;
    // FUN_06eb7090 (RET 4): +0x10 = System.
    void SetSystem(FSystemInfo* InSystem);
    // FUN_06eea860 (in TU25's range, ICF-shared getter `MOV EAX,[ECX+0x10]; RET`): the
    // callers TU24 06ed5a25, TU43 06f3401b/06f34063/06f340b5 and TU37 pass a space object.
    FSystemInfo* GetSystem() const
    {
        return System;
    }
    // FUN_06eb70a0 (RET 4).
    void GetPlacement(FSpaceObjPlacement* Out) const;
    // FUN_06eb70f0 (RET 4).
    void GetPosition(Vector* Out) const;
    // FUN_06eb7130 (RET 4).
    void GetOrientation(Matrix* Out) const;
    // FUN_06eb7170 (RET 8).
    void GetMotion(Vector& OutLinear, Vector& OutAngular) const;
    // FUN_06f34670 (in TU43's range; callers TU58 06f72b34, 06f742a3 on SystemInfo's
    // SpaceObjs): pub::SpaceObj::Destroy(SpaceObjId, DESTROY_NORMAL).
    void DestroySpaceObj();
    // FUN_06eb7280 (RET 8): always returns true.
    bool FindOrAllocAttacker(unsigned int AttackerId, FSpaceObjAttacker** Out);

    unsigned int SpaceObjId;                              // +0x04 0 from the ctor
    FContentPlayerStateBase* ReputationHandle;            // +0x08 not released by the dtor
    FContentBase* Base;                                   // +0x0c
    FSystemInfo* System;                                  // +0x10
    FSpaceObjAttacker Attackers[8];                       // +0x14
    TString<0x30> Nickname;                               // +0xd4 (chars at +0xd8)
    std::vector<FSpaceObjPendingComm> PendingComms;       // +0x108
};
