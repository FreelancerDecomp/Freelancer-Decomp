// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_goods.h" // EquipDescList

// CPlayerTradeOffer -- retail sizeof 0x24, one per player while a trade is in progress.
// Server.dll allocates two per trade in IServerImpl::InitiateTrade (.text:06cf185d ..
// 06cf18f4) and keeps them in PlayerData::trade_offer (+0x3CC); the Server-side
// FTradeOffer stand-in that used to replace this class was removed 2026-09-24. This
// port's own sizeof differs from retail's 0x24 (the port's EquipDescList is the current CRT's
// std::list, not retail's 0xc-byte VC6 list) -- harmless since every accessor goes through named members,
// not raw offsets, on both sides of the DLL boundary.
//
// Layout confirmed byte-for-byte from CPlayerTradeOffer::CPlayerTradeOffer(CPlayerTradeOffer
// const&) (0x0631d630), CPlayerTradeOffer::operator= (0x0631d720), the default ctor
// (0x0631d8a0, 0x0631d8a0-0x0631d8db) and ~CPlayerTradeOffer (0x0631d8e0):
//   +0x00 vfptr (sole slot: compiler-generated scalar/vector-deleting destructor thunk,
//         Common.dll.txt 0x0631d840 -- provided implicitly here by `virtual ~CPlayerTradeOffer`,
//         not hand-ported separately)
//   +0x04 SourcePlayerId          (existing SetSourcePlayerID)
//   +0x08 TargetPlayerId          (existing GetTargetPlayerID/SetTargetPlayerID)
//   +0x0c TargetOffer             (existing SetTargetOffer)
//   +0x10 List -- an EquipDescList (retail: a VC6 std::list<EquipDesc>, allocator byte +0x10,
//         head +0x14, size +0x18). Proven by the export ?GetList@CPlayerTradeOffer@@QAEAAV
//         EquipDescList@@XZ, whose body is `LEA EAX,[ECX+0x10]`. The byte the old port called
//         "Unknown10" is that list's allocator byte: the copy ctor copies it and operator=
//         does not, exactly as VC6's std::list copy ctor / operator= do. The port's
//         EquipDescList is not retail-sized (the current CRT's std::list, not VC6's), so this
//         class is not retail's 0x24; nothing crosses a module boundary by raw offset.
//   +0x1c MoneyOffered            (existing GetMoneyOffered/SetMoneyOffered)
//   +0x20 Accepted                (existing GetAccepted/SetAccepted)
class COMMON_API CPlayerTradeOffer
{
public:
    CPlayerTradeOffer();
    CPlayerTradeOffer(CPlayerTradeOffer const& Other);
    CPlayerTradeOffer& operator=(CPlayerTradeOffer const& Other);
    virtual ~CPlayerTradeOffer();

    unsigned int GetTargetPlayerID();
    unsigned long GetMoneyOffered();

    // Common.dll.c 0xbd950 -- shared retail thunk (`*(ulong*)(this+0x1c) = param_1`), the
    // same ICF-folded code backing CEquip::ConnectClientEquip and EquipDesc::set_owner.
    // Same offset as GetMoneyOffered above.
    void SetMoneyOffered(unsigned long Money);

    // Common.dll.txt 0xbd620 -- plain byte-flag getter (`return *(byte*)(this+0x20)`), the
    // trivial companion to SetAccepted below.
    bool GetAccepted();

    // Common.dll.txt 0xbd960 -- plain byte-flag setter (`*(byte*)(this+0x20) = param_1`),
    // not ICF-folded with any other class. Referenced by IServerImpl::AcceptTrade
    // (offer[0x20] mutual-accept check).
    void SetAccepted(bool bAccepted);

    // Common.dll.c 0xa9b30 -- shared retail thunk (`*(uint*)(this+4) = param_1`) backs several
    // unrelated classes' setters (CPlayerTradeOffer::SetSourcePlayerID, EquipDesc::set_arch_id,
    // FmtStr::begin_mad_lib, CmnAsteroid::CAsteroidSystem::set_sys_id, ...). This is this
    // class's instance of it.
    void SetSourcePlayerID(unsigned int Id);

    // Common.dll.txt 0xbd940 -- another shared retail thunk (`*(uint*)(this+8) = param_1`),
    // also backing NebulaExclusionZone::set_fog_far, PositionNode::set_pos_v,
    // StateMachineMessage::set_sender. This is this class's instance of it.
    void SetTargetPlayerID(unsigned int Id);

    // Common.dll.c 0xd2f40 -- shared retail thunk (`*(void**)(this+0xc) = param_1`), the
    // same ICF-folded code backing TRANode::set_mask, PositionNode::set_pos_h(float), and
    // StateMachineMessage::set_receiver. This is this class's instance of it.
    void SetTargetOffer(CPlayerTradeOffer* Offer);

    // 0x06261dc0 ?GetList@CPlayerTradeOffer@@QAEAAVEquipDescList@@XZ: LEA EAX,[ECX+0x10] /
    // RET -- the offered items are an EquipDescList embedded at +0x10.
    EquipDescList& GetList();
    // 0x062d1ad0 ?GetSourcePlayerID@CPlayerTradeOffer@@QAEIXZ: MOV EAX,[ECX+4] / RET.
    unsigned int GetSourcePlayerID();
    // 0x0629fb90 ?GetTargetOffer@CPlayerTradeOffer@@QAEPAV1@XZ: MOV EAX,[ECX+0xc] / RET.
    CPlayerTradeOffer* GetTargetOffer();

private:
    unsigned int SourcePlayerId;        // +0x04
    unsigned int TargetPlayerId;        // +0x08
    CPlayerTradeOffer* TargetOffer;     // +0x0c
    EquipDescList List;                 // +0x10 -- see class comment
    unsigned long MoneyOffered;         // +0x1c
    bool Accepted;                      // +0x20
};
