// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// ===========================================================================
// server_createinfo.h -- the CREATE* record types Server.dll hands to IClient,
// the builders that fill them, and the ONE declaration rpclocal.dll and
// freelancer.exe read the same records through.
//
// NAMES. These are retail's own, read out of Server.dll's RTTI rather than
// invented. The record the CREATESHIP branch builds carries a vftable at +0x00
// (.rdata:PTR_LAB_06d65f78), and the dword at [vftable - 4] is a
// RTTICompleteObjectLocator:
//   .rdata:06d6c534  4c87d806  addr  .data:CShipCreateInfo::RTTI_Type_Descriptor
// while the base vftable the base constructor stamps
// (.rdata:PTR_LAB_06d65f68, .text:06cf64bb) has its own locator at
// .rdata:06d6c4e0, whose type descriptor is
//   .rdata:06d6c4ec  2c87d806  addr  .data:CEqObjCreateInfo::RTTI_Type_Descriptor
// The solar sibling's table is .rdata:PTR_LAB_06d66b7c (stamped at
// .text:06cf812f and .text:06d08257), locator .rdata:06d6c8e0, descriptor
// pointer .rdata:06d6c8ec -> .data:CSolarCreateInfo::RTTI_Type_Descriptor.
// The raw descriptor strings are
//   .data:06d88734  char[24]  ".?AUCEqObjCreateInfo@@"
//   .data:06d88754  char[24]  ".?AUCShipCreateInfo@@"
//   .data:06d88878  char[24]  ".?AUCSolarCreateInfo@@"
// `.?AU` is `struct`.
//
// ONE TYPE, THREE MODULES. The record crosses two module boundaries:
// Observer hands it by reference to IClient::SendServerCreateShip (CREATESHIP) /
// SendServerCreateSolar (CREATESOLAR), rpclocal's marshaller reads it back
// field by field, and rpclocal's receiver delivers its own static copy to the
// client's IClient. Retail compiled the same struct into both DLLs from a
// shared header with INLINE members -- rpclocal.dll carries its own copies of
// every one of them, with its own vftables:
//
//   member                        Server.dll        rpclocal.dll
//   CEqObjCreateInfo::ctor        .text:06cf63f0    .text:06d83df0
//   CEqObjCreateInfo::~dtor       .text:06cf64d0    .text:06d843b0
//   CEqObjCreateInfo::clear       .text:06cf0260    .text:06d83ed0
//   CShipCreateInfo::clear        .text:06cf0410    .text:06d83fa0
//   CEqObjCreateInfo vftable      .rdata:06d65f68   .rdata:06db3750
//   CShipCreateInfo vftable       .rdata:06d65f78   .rdata:06db373c
//   CSolarCreateInfo vftable      .rdata:06d66b7c   .rdata:06db3738
//
// The two copies are byte-for-byte the same code (same stores, same order, the
// same two operator_new(0x10)/(0x14) list sentinels). So the port does what
// retail did: the members are defined inline below and every module that
// constructs one gets its own copy. Nothing is exported, and nothing here is
// retail ABI -- the two std::list members and the DamageList are THIS
// toolchain's containers on both sides of the call, which is exactly why both
// sides must see this one declaration and no private mirror of it.
//
// The members that sit inside Common.dll's own classes (DamageList,
// EquipDescVector, CollisionGroupDesc, ID_String) are Common.dll's published
// types, included, never redeclared.
// ===========================================================================

#include <list>

#include "common_cobject.h"    // CObject / CShip / CLoot / CGuided, CollisionGroupDesc
#include "common_goods.h"      // EquipDescVector
#include "common_id.h"         // ID_String
#include "common_misc.h"       // DamageList, DamageEntry
#include "common_physys.h"     // Quaternion

// ---------------------------------------------------------------------------
// Shared helper: the matrix -> quaternion conversion every builder does through
// the lazily-acquired I3DMathEngine singleton at .data:DAT_06d8da08.
//   .text:06d076e5  MOV EAX,[.data:DAT_06d8da08]
//   .text:06d076ee  JNZ LAB_06d076fa
//   .text:06d076f0  CALL FUN_06d5ff70            ; Acquire3DMathEngine
//   .text:06d07707  CALL dword ptr [ECX + 0x50]  ; MatrixToQuaternion
// The same three-instruction preamble is repeated verbatim at .text:06d07c0a
// (loot), .text:06d07cb0 (countermeasure/mine) and .text:06d07d80 (guided).
// Server.dll-only; defined in src/Server/server_createinfo.cpp.
// ---------------------------------------------------------------------------
void Server_MatrixToQuat(Quaternion& Out, Matrix const& In);

// The inverse, through the same file-static engine (.data:DAT_06d8da08):
// I3DMathEngine slot +0x54, QuaternionToMatrix. The two object builders call
// it on their request's +0x58 quaternion to rebuild CreateParms' 3x3:
//   FUN_06d0a200 (solar)  .text:06d0a377 lazy fetch, .text:06d0a3b0 CALL [ECX+0x54]
//   FUN_06d0a880 (ship)   .text:06d0aa9f lazy fetch, .text:06d0aaed CALL [ECX+0x54]
// No null test on the engine in either. Defined in server_createinfo.cpp.
void Server_QuatToMatrix(Matrix& Out, Quaternion const& In);

// ---------------------------------------------------------------------------
// FUN_06d5ff70 (.text:06d5ff70, 12 instructions, no arguments). Server.dll's own
// "3DMathEngine" acquire: a 0xC-byte descriptor on the stack
// {0xc, "3DMathEngine", 0} (.text:06d5ff73 / .text:06d5ff7b / .text:06d5ff83),
// DACOM_Acquire() (.text:06d5ff8b) and the manager's vtable +0x0c,
// ICOManager::CreateInstance(&desc, &out) (.text:06d5ff9e), returning `out`.
// It does NOT cache and checks nothing. The caching is at the call sites, one
// file-static per retail translation unit, each in the MSVC lazy idiom
// `MOV EAX,[DAT] / CMP EAX,EBX / JNZ / CALL FUN_06d5ff70 / MOV [DAT],EAX`:
//   .data:DAT_06d8d7f8  Observer.cpp  (.text:06cf5ce0, 06cf5eea, 06cf7204)
//   .data:DAT_06d8da08  StarSys.cpp   (.text:06d076e5, 06d07997, 06d07c0a, ...)
//   .data:DAT_06d902f0  pub::Player::SetCamera (.text:06d55ec3)
// (and DAT_06d8d758 / DAT_06d8d868 / DAT_06d8d924 in three more files).
// Server.dll-only; defined in src/Server/server_createinfo.cpp.
// ---------------------------------------------------------------------------
struct I3DMathEngine;
I3DMathEngine* Server_Acquire3DMathEngine();

// ===========================================================================
// CEqObjCreateInfo -- retail size 0xac: both constructors' last store is the
// byte at +0xa8 (.text:06cf64b5 / .text:06d83eb5) and both base clear()s clear
// it (.text:06cf0275 / .text:06d83ee5). +0xa8 is therefore a BASE member, which
// is also why CREATESHIP (payload+0xc8) and CREATESOLAR (payload+0x7d) both
// carry it (rpclocal .text:06d8950a / .text:06d891e8).
// ===========================================================================
struct CEqObjCreateInfo
{
    // .text:06cf63f0 / rpclocal .text:06d83df0. Retail's store trail, EBX == 0
    // and EAX == -1 throughout:
    //   06cf6411 +0x10 = -1   06cf6414..6426 +0x04/+0x08/+0x0c/+0x14..+0x20 = 0
    //   06cf6429 +0x44 = 0    06cf642c +0x68 = -1   06cf6433 +0x48 = 0
    //   06cf6436 +0x6c = 0    06cf643d..6446 the vector at +0x70 (allocator
    //                          byte, then _First/_Last/_End = 0)
    //   06cf644d..6469 +0x80 list: operator_new(0x10) sentinel, _Size 0
    //   06cf6473..6494 +0x8c list: operator_new(0x14) sentinel, _Size 0
    //   06cf649a..64ac +0x98 byte / +0x9c / +0xa0 / +0xa4 = 0  (the DamageList
    //                          tail -- Common's DamageList() does the same)
    //   06cf64b5 +0xa8 byte = 0     06cf64bb the base vftable
    // Retail leaves +0x24..+0x43 (accessory array) and +0x4c..+0x64 (position,
    // quaternion) as they lie; every fill overwrites them. The port zeroes them
    // so that no reader ever sees indeterminate bytes -- no branch depends on
    // it and every writer overwrites them, so it changes nothing retail does.
    CEqObjCreateInfo()
        : ObjId(0u)
        , ArchetypeNickname(0u)
        , DockingBaseId(0u)
        , VibeGroupId(-1)
        , CostumeHead(0u)
        , CostumeBody(0u)
        , CostumeLeftHand(0u)
        , CostumeRightHand(0u)
        , CostumeAccessoryCount(0)
        , VoiceId(0u)
        , HitPts(-1)
        , Field_0x6c(0u)
        , bMarkedBy(0u)
    {
        for (int Index = 0; Index < 8; ++Index)
            CostumeAccessory[Index] = 0u;
        Position.x = 0.0f;
        Position.y = 0.0f;
        Position.z = 0.0f;
        Orientation.w = 0.0f;
        Orientation.x = 0.0f;
        Orientation.y = 0.0f;
        Orientation.z = 0.0f;
    }

    // .text:06cf64d0 / rpclocal .text:06d843b0. NOT in the vftable -- the
    // one-entry table at .rdata:06d65f68 holds only clear() -- so this is
    // non-virtual, reached by a direct CALL (.text:06cf5a20) and, in rpclocal,
    // by the two atexit thunks .text:06d84230 / .text:06d84250 that tear the
    // two receiver statics down. Retail re-stamps the base vftable
    // (.text:06cf64f2), erases the +0x8c list, then the +0x80 list, then frees
    // the vector's buffer (.text:06cf657b `MOV EAX,[EBX + 0x74]` / CALL
    // FUN_06d5ffb0) and zeroes +0x74/+0x78/+0x7c -- that is ~EquipDescVector
    // (Common 0x06269110) inlined. The lists, the DamageList and the
    // EquipDescVector members destroy themselves here.
    ~CEqObjCreateInfo()
    {
    }

    // Copy constructor -- Server.dll FUN_06d0af00 (.text:06d0af00 .. 06d0b106,
    // RET 4, SEH frame). Not exported, and out of line in retail: its only caller
    // is the CShipCreateInfo copy FUN_06d0a880 makes of its request
    // (.text:06d0a8e9), which then copies the derived tail inline. The store
    // trail, in order:
    //   06d0af1d .. 06d0af35  +0x04 / +0x08 / +0x0c / +0x10
    //   06d0af3a .. 06d0af45  REP MOVSD 0xd dwords: +0x14 .. +0x47 (the Costume)
    //   06d0af47 .. 06d0af8e  +0x48, +0x4c Vector, +0x58 four dwords, +0x68,
    //                         +0x6c, the vector's allocator byte +0x70
    //   06d0af91 .. 06d0affc  the vector: n = _First ? (_Last - _First) >> 5 : 0,
    //                         a negative n clamped to 0 (JGE), operator new(n *
    //                         0x20) -- even for n == 0 --, EquipDesc's copy
    //                         constructor on each element (the destination
    //                         null-tested), then _Last = _End = the end
    //   06d0b002 .. 06d0b063  +0x80 allocator byte, a fresh 0x10 sentinel, _Size
    //                         0, then list<CollisionGroupDesc>::insert(end, x)
    //                         per source element
    //   06d0b065 .. 06d0b0b5  +0x8c allocator byte, a fresh 0x14 sentinel, _Size
    //                         0, list<DamageEntry>::insert(end, first, last)
    //   06d0b0bb .. 06d0b0e6  +0x98 byte, +0x9c / +0xa0 / +0xa4, +0xa8 byte
    //   06d0b0f1              the base vftable
    // The two lists and the DamageList tail are those members' own copies.
    CEqObjCreateInfo(CEqObjCreateInfo const& Other)
        : ObjId(Other.ObjId)
        , ArchetypeNickname(Other.ArchetypeNickname)
        , DockingBaseId(Other.DockingBaseId)
        , VibeGroupId(Other.VibeGroupId)
        , CostumeHead(Other.CostumeHead)
        , CostumeBody(Other.CostumeBody)
        , CostumeLeftHand(Other.CostumeLeftHand)
        , CostumeRightHand(Other.CostumeRightHand)
        , CostumeAccessoryCount(Other.CostumeAccessoryCount)
        , VoiceId(Other.VoiceId)
        , Position(Other.Position)
        , Orientation(Other.Orientation)
        , HitPts(Other.HitPts)
        , Field_0x6c(Other.Field_0x6c)
        , CollisionGroups(Other.CollisionGroups)
        , Damage(Other.Damage)
        , bMarkedBy(Other.bMarkedBy)
    {
        for (int Index = 0; Index < 8; ++Index)
            CostumeAccessory[Index] = Other.CostumeAccessory[Index];

        Equipment.AllocPad = Other.Equipment.AllocPad;
        int Count = (Other.Equipment.Begin != nullptr)
            ? static_cast<int>(Other.Equipment.End - Other.Equipment.Begin)
            : 0;
        if (Count < 0)
            Count = 0;
        EquipDesc* const Block =
            static_cast<EquipDesc*>(::operator new(static_cast<size_t>(Count) * sizeof(EquipDesc)));
        EquipDesc* Dest = Block;
        for (EquipDesc const* Source = Other.Equipment.Begin; Source != Other.Equipment.End; ++Source)
        {
            if (Dest != nullptr)
                new (Dest) EquipDesc(*Source);
            ++Dest;
        }
        Equipment.Begin = Block;
        Equipment.End = Dest;
        Equipment.Cap = Dest;
    }
    CEqObjCreateInfo& operator=(CEqObjCreateInfo const&) = delete;

    // Vftable slot 0, the only slot. .text:06cf0260 / rpclocal .text:06d83ed0:
    //   +0x04/+0x08/+0x0c/+0x48/+0x6c = 0, +0xa8 byte = 0, +0x10 = -1
    //   the vector: _Last := _First (VC6 erase(begin, end) on a POD element --
    //     `ADD EAX,0x20 / MOV ECX,0x8 / REP MOVSD`, the 0x20 stride that is
    //     sizeof(EquipDesc)), buffer KEPT
    //   the +0x80 list and the +0x8c list: every node unlinked and freed, _Size
    //     decremented per node, sentinel KEPT
    // It does NOT touch +0x68, the Costume block, position, orientation, or the
    // DamageList's four tail members (+0x98..+0xa4).
    virtual void clear()
    {
        ObjId = 0u;
        ArchetypeNickname = 0u;
        DockingBaseId = 0u;
        VoiceId = 0u;
        Field_0x6c = 0u;
        bMarkedBy = 0u;
        VibeGroupId = -1;
        Equipment.End = Equipment.Begin;
        CollisionGroups.clear();
        // Retail's list erase decrements the list's own _Size (+0x94). The
        // port's DamageList keeps that count a second time in Count, so both
        // go to zero together; the tail members are left alone as retail does.
        Damage.Entries.clear();
        Damage.Count = 0;
    }

    // +0x00 is the vptr.
    unsigned int ObjId;                 // +0x04  CObject::Id (+0xb0)
    unsigned int ArchetypeNickname;     // +0x08  get_archetype()->Nickname (Root+0x08)
    unsigned int DockingBaseId;         // +0x0c  CEqObj+0x160
    int VibeGroupId;                    // +0x10  CEqObj::VibeId (+0x104); ctor seeds -1
    // +0x14 .. +0x44 is pub::Costume, copied out of CEqObj+0x108 .. +0x138.
    // The copy is field-per-field and then a counted loop, which is what proves
    // the shape: .text:06d077b9/.../06d077ca move the four head dwords,
    // .text:06d077d4 moves the count, and .text:06d077e8 copies exactly `count`
    // dwords starting at source+0x10 into record+0x24.
    unsigned int CostumeHead;           // +0x14
    unsigned int CostumeBody;           // +0x18
    unsigned int CostumeLeftHand;       // +0x1c
    unsigned int CostumeRightHand;      // +0x20
    unsigned int CostumeAccessory[8];   // +0x24 .. +0x43
    int CostumeAccessoryCount;          // +0x44
    unsigned int VoiceId;               // +0x48  CEqObj+0x13c
    Vector Position;                    // +0x4c  CObject::WorldPosition (+0x2c)
    // Wire order is x,y,z,w; rpclocal's header codec FLDs all four as floats
    // and quantizes them against 127.0 (a unit quaternion).
    Quaternion Orientation;             // +0x58  from CObject::Orientation (+0x08)
    // An INTEGER: rpclocal .text:06da3a9c `FILD dword ptr [EBP+0x68]` on the way
    // out and .text:06da4394 stores ftol's result back in.
    int HitPts;                         // +0x68  (int)CSimple::get_hit_pts(); ctor seeds -1
    unsigned int Field_0x6c;            // +0x6c  always stored 0 (.text:06d07778)
    EquipDescVector Equipment;          // +0x70  CEqObj::get_equip_desc_list, vtable +0xac
    // Node operator_new(0x10) -> an 8-byte value; the destructor erases it
    // through .rdata:06d643d0, COMMON.DLL::std::list<CollisionGroupDesc>::erase.
    std::list<CollisionGroupDesc> CollisionGroups;  // +0x80 .. +0x8b
    // ONE Common.dll DamageList, retail 0x1c bytes (+0x8c .. +0xa7): the list
    // header at +0x8c (node operator_new(0x14) -> a 12-byte DamageEntry, erased
    // through .rdata:06d641e8, std::list<DamageEntry>::erase) and then the
    // class's own four tail members --
    //   +0x98 bDestroyed (a BYTE store in both ctors, .text:06cf649a /
    //         .text:06d83e9a, and a byte copy on the wire, rpclocal
    //         .text:06d8caae `MOV DL,byte ptr [EBX + 0xc]` with EBX == src+0x8c)
    //   +0x9c Cause, +0xa0 InflictorId, +0xa4 InflictorOwner
    //         (rpclocal .text:06d8cab4 / .text:06d8caba / .text:06d8cac1, the
    //         dwords at +0x10/+0x14/+0x18 of the same object)
    // which is exactly common_misc.h's DamageList. It was previously sliced here
    // into a bare list plus four loose fields named as if they were the
    // record's own.
    DamageList Damage;                  // +0x8c .. +0xa7
    unsigned char bMarkedBy;            // +0xa8  GameObject::is_marked_by(PlayerId),
                                        //        .text:06cf58b8 / .text:06cf58c3 (ship),
                                        //        .text:06d0a636 / .text:06d0a63b (solar)
};

// ===========================================================================
// CShipCreateInfo -- IClient +0x0bc (SendServerCreateShip) FLPACKET_SERVER_CREATESHIP.
//
// Retail size 0xe0: CShipCreateInfo::clear() (.text:06cf0410) ends at
// .text:06cf052c `MOV byte ptr [EBP + 0xdc],0x0`, and the inlined constructor
// writes the same seven fields and no others -- at the CREATESHIP call site
// (.text:06cf5835 .. .text:06cf5867) and in rpclocal's one-time init of the
// receiver static .data:06dbb940 (.text:06d81fed .. .text:06d82011).
//
// Note what neither the constructor nor clear() touches: +0xb8 .. +0xcc, the
// two Vectors. They are unconditionally overwritten by the fill.
// ===========================================================================
struct CShipCreateInfo : CEqObjCreateInfo
{
    //   .text:06cf5835  +0xac = 0     .text:06cf583c  +0xb0 = 0
    //   .text:06cf5843  +0xb4 = 0     .text:06cf584a  +0xd0 = 0
    //   .text:06cf5855  +0xd4 = -1    .text:06cf5860  +0xd8 = 0
    //   .text:06cf5867  +0xdc byte = 0
    //   .text:06cf586e  the derived vftable (.rdata:PTR_LAB_06d65f78)
    // The port zeroes the two Vectors as well (see the base constructor).
    CShipCreateInfo()
        : OwnerPlayer(0u)
        , Field_0xb0(0u)
        , ReputationRank(0u)
        , Throttle(0.0f)
        , VibeOtherGroupId(-1)
        , Field_0xd8(0u)
        , bUsingTradelane(0u)
    {
        Velocity.x = 0.0f;
        Velocity.y = 0.0f;
        Velocity.z = 0.0f;
        AngularVelocity.x = 0.0f;
        AngularVelocity.y = 0.0f;
        AngularVelocity.z = 0.0f;
    }

    // .text:06cf0410 / rpclocal .text:06d83fa0. Retail inlines the base body
    // (.text:06cf041b .. .text:06cf0500, byte for byte the stores of
    // .text:06cf0260) and then the derived tail .text:06cf0502 .. .text:06cf052c:
    // +0xac/+0xb0/+0xb4/+0xd0/+0xd8 = 0, +0xd4 = -1, +0xdc byte = 0. rpclocal's
    // CREATESHIP receiver runs the same body inlined after the slot call
    // (.text:06d8222e .. .text:06d822bb).
    virtual void clear() override
    {
        CEqObjCreateInfo::clear();
        OwnerPlayer = 0u;
        Field_0xb0 = 0u;
        ReputationRank = 0u;
        Throttle = 0.0f;
        Field_0xd8 = 0u;
        VibeOtherGroupId = -1;
        bUsingTradelane = 0u;
    }

    // Wire: payload+0x9c (a full dword, and the encoder's flag bit 0x04 is
    // `!= 0` on it -- rpclocal .text:06d97d96).
    unsigned int OwnerPlayer;      // +0xac  CSimple::OwnerPlayer (+0xb4)
    // Wire: only the LOW WORD travels (rpclocal .text:06d89478 / .text:06d89483)
    // and the receiver widens it back with MOVZX (.text:06d82058).
    unsigned int Field_0xb0;       // +0xb0  CShip+0x21c
    // Wire: only the LOW BYTE travels (.text:06d8948a / .text:06d89494,
    // MOVZX back at .text:06d82064).
    unsigned int ReputationRank;   // +0xb4  Reputation::Vibe::GetRank out-parameter
    Vector Velocity;               // +0xb8  vtable +0x54 == CObject::get_velocity
    Vector AngularVelocity;        // +0xc4  vtable +0x58 == CObject::get_angular_velocity
    // Quantized against 127.0 on the wire (rpclocal .text:06d97e47).
    float Throttle;                // +0xd0  CShip::Throttle (+0x29c)
    // Sender-only: the second group of Reputation::Vibe::AttitudeTowards
    // (rpclocal .text:06d89716). Server.dll stores PlayerData::rep_vibe here
    // (.text:06cf58a4 / .text:06cf66a0).
    int VibeOtherGroupId;          // +0xd4  PlayerData::rep_vibe; ctor seeds -1
    // Wire: only the LOW WORD travels (.text:06d8951a / .text:06d89528,
    // MOVZX back at .text:06d8204b).
    unsigned int Field_0xd8;       // +0xd8  CShip+0x1b4
    unsigned char bUsingTradelane; // +0xdc  CShip::is_using_tradelane()
};

// ===========================================================================
// FShipCreateRequest -- the argument of StarSystem::create_ship (FUN_06d0a880).
// Its callers (pub::SpaceObj::Create .text:06cf7f94, IServerImpl::PlayerLaunch
// .text:06cf00ba) build a CShipCreateInfo and three more members right after
// its 0xe0 bytes; FUN_06d0a880 copies only the CShipCreateInfo part
// (FUN_06d0af00 + the derived tail, .text:06d0a8e9 .. 06d0a98f) and reads the
// rest straight from the request:
//   .text:06d0ad23  MOV DX,word ptr [EBP + 0xe0]   -> the EquipIdMaker state
//   .text:06d0ad2a  MOV EAX,dword ptr [EBP + 0xe4] -> Ship::FEquipState::Data
//   .text:06d0ad30  MOV EBP,dword ptr [EBP + 0xe8] -> & 0x3fffffff, then
//                   mark_for_player(it, 1)
// Retail struct name not recovered.
// ===========================================================================
struct FShipCreateRequest : CShipCreateInfo
{
    SubObjectID::EquipIdMaker EquipIds;   // +0xe0 (a word)
    void const* EquipStateData;           // +0xe4
    unsigned int MarkPlayerId;            // +0xe8
};

// ===========================================================================
// CSolarCreateInfo -- IClient +0x0b8 (SendServerCreateSolar) FLPACKET_SERVER_CREATESOLAR.
//
// Its vftable .rdata:06d66b7c holds .text:06cf0260 in slot 0 -- the BASE
// clear() -- so it has no override of its own; rpclocal's receiver static
// .data:06dbb838 likewise gets .rdata:06db3738 (.text:06d82d73), whose slot 0
// is rpclocal's copy of the base clear, .text:06d83ed0.
//
// The derived constructor is inlined at every construction site and writes the
// same three fields and nothing else:
//   Server  .text:06cf8116 / .text:06cf811d / .text:06cf8124   (pub::SpaceObj::CreateSolar)
//   Server  .text:06d0823e / .text:06d08245 / .text:06d0824c   (StarSystem::add_player)
//   rpclocal .text:06d82d61 / .text:06d82d67 / .text:06d82d6d  (receiver static)
// i.e. +0xac byte 0, +0xad byte 0, +0xb0 dword -1.
//
// The last three members are proven by pub::SpaceObj::CreateSolar
// (.text:06cf80d0, record at entry-0xd4) and their only reader,
// FUN_06d0a200, which receives the record's address at .text:06cf82bf:
//   +0xb4  written .text:06cf82a8 (byte from the SolarInfo's +0x80),
//          read    .text:06d0a3b3 `MOV AL,byte ptr [EBP + 0xb4]`
//   +0xb8  ID_String::clear on it at .text:06cf814d (`LEA ECX,[ESP + 0xd8]`,
//          ESP == entry-0xf4), written .text:06cf818f, read .text:06d0a50c
//   +0xbc  written .text:06cf829e (dword from the SolarInfo's +0xcc),
//          read    .text:06d0a53d and tested `& 0x3fffffff` at .text:06d0a543
// Retail size is therefore at least 0xc0; nothing in either module touches a
// byte past +0xbf. The constructor leaves these three as they lie, and so does
// the port.
// ===========================================================================
struct CSolarCreateInfo : CEqObjCreateInfo
{
    CSolarCreateInfo()
        : Field_0xac(0u)
        , Field_0xad(0u)
        , VibeOtherGroupId(-1)
    {
    }

    // pub::SpaceObj::CreateSolar stores 1 (.text:06cf82af); FUN_06d0a200
    // hands it to CSolar::init's parameter block (.text:06d0a512 / .text:06d0a530).
    unsigned char Field_0xac;       // +0xac
    // pub::SpaceObj::CreateSolar stores 1 (.text:06cf82b7). Wire payload+0x7c
    // (rpclocal .text:06d891d8 out, .text:06d82d90 back in).
    unsigned char Field_0xad;       // +0xad
    // The second group of Reputation::Vibe::AttitudeTowards (rpclocal
    // .text:06d89239). FUN_06d0a200 stores PlayerData::rep_vibe here per
    // player (.text:06d0a64f / .text:06d0a65b), the same `id*0x418 - 0x100`
    // expression the ship path uses for its +0xd4.
    int VibeOtherGroupId;           // +0xb0  ctor seeds -1
    unsigned char Field_0xb4;       // +0xb4
    ID_String Field_0xb8;           // +0xb8
    unsigned int Field_0xbc;        // +0xbc
};

// ===========================================================================
// The four flat records. These are plain retail POD and they are written here
// exactly as the lane brief asked -- one named member per store, with the
// storing instruction quoted. rpclocal's codecs (rpc_payload_ch2.cpp) read and
// rebuild these very types for IClient::SendServerCreateLoot / CreateCounter /
// CreateMine / CreateGuided: their former private mirrors (0x14 / 0x48 / 0x48 /
// 0x38) were folded onto them 2026-10-16.
// ===========================================================================

// The 0x48-byte LootDesc that FUN_06d07b90 fills. It is NOT the SendServerCreateLoot
// argument -- it hangs off FServerCreateLootRequest::Items below.
struct FServerLootDesc
{
    unsigned int LootId;              // +0x00  .text:06d07ba3  MOV [EDI],EAX
    unsigned int ContentsNickname;    // +0x04  .text:06d07bae, CLoot::ContentsArch->Nickname
    float ContentsHitPts;             // +0x08  .text:06d07bb7  FSTP [EDI + 0x8]
    unsigned int Units;               // +0x0c  .text:06d07bc2, CLoot::Units (+0xf0)
    unsigned int ContainerNickname;   // +0x10  .text:06d07bce, get_archetype()->Nickname
    float HitPts;                     // +0x14  .text:06d07bd9  FSTP [EDI + 0x14]
    unsigned int NameOverride;        // +0x18  .text:06d07bf1, only when get_name() differs
                                      //        from ContentsArch->IdsName (Root+0x14)
    Vector Position;                  // +0x1c  .text:06d07bfc/06d07c01/06d07c07
    Quaternion Orientation;           // +0x28  .text:06d07c37 .. .text:06d07c4b
    Vector Velocity;                  // +0x38  .text:06d07c5b/06d07c60/06d07c66, vtable +0x54
    // The three tail bytes are written by three different places, which is why
    // they are three separate members and not one dword:
    unsigned char Field_0x44;         // +0x44  caller, .text:06cf5786  MOV byte [ESP+0x114],1
    unsigned char bTemporary;         // +0x45  builder, .text:06d07c6f, CLoot+0xfd
    unsigned char bMarkedBy;          // +0x46  caller, .text:06cf57ba, is_marked_by(PlayerId)
    unsigned char Pad47;              // +0x47
};

// The SendServerCreateLoot argument block, built at S+0x6c by the CREATELOOT branch.
struct FServerCreateLootRequest
{
    unsigned int OwnerId;             // +0x00  .text:06cf57c7, CLoot::OwnerId (+0xe4)
    unsigned int Kind;                // +0x04  .text:06cf57df  MOV [ESP + 0x74],0x5
    unsigned char bMarkedBy;          // +0x08  .text:06cf57fe
    unsigned char bTemporary;         // +0x09  .text:06cf5808, CLoot::bTemporary (+0xfd)
    unsigned char Pad0a[2];
    FServerLootDesc* Items;           // +0x0c  .text:06cf57e7
    unsigned char Count;              // +0x10  .text:06cf57eb  MOV byte [ESP + 0x80],0x1
    unsigned char Pad11[3];
};

// The 0x48-byte record FUN_06d07c80 fills, sent through IClient +0x0cc (SendServerCreateCounter,
// CREATECOUNTER) and +0x0c4 (SendServerCreateMine, CREATEMINE). One builder, two packets.
struct FServerCreateProjectileInfo
{
    unsigned int ObjId;               // +0x00  .text:06d07c95, CObject::Id (+0xb0)
    unsigned int ArchetypeNickname;   // +0x04  .text:06d07ca0
    unsigned int OwnerId;             // +0x08  .text:06d07cf6, CProjectile::OwnerId (+0xe4)
    unsigned short SubObjId;          // +0x0c  .text:06d07cac, SUBOBJ_ID_NONE
    unsigned short Pad0e;
    Vector Position;                  // +0x10  .text:06d07d01/06d07d06/06d07d0c
    Quaternion Orientation;           // +0x1c  .text:06d07cdd .. .text:06d07ced
    Vector Velocity;                  // +0x2c  .text:06d07d27/06d07d2c/06d07d33, vtable +0x54
    // +0x38 is a THIRD Vector, not padding. Neither builder ever writes it, but
    // both call sites construct it: the countermeasure branch at
    // .text:06cf5a7b (`LEA ECX,[ESP + 0x154]`, ESP == S-0xc, record at S+0x110)
    // and the mine branch at .text:06cf5b17 (`LEA ECX,[ESP + 0x19c]`, record at
    // S+0x158) both hand record+0x38 to FUN_06ce7c10, the three-float Vector
    // constructor, with three zeroes.
    Vector Field_0x38;                // +0x38
    unsigned int Field_0x44;          // +0x44  .text:06d07d13  MOV [ESI + 0x44],0x0
};

// The 0x38-byte record FUN_06d07d40 fills, sent through IClient +0x0c8 (SendServerCreateGuided).
struct FServerCreateGuidedInfo
{
    unsigned int ObjId;               // +0x00  .text:06d07d68
    Vector Position;                  // +0x04  .text:06d07d72/06d07d77/06d07d7d
    Quaternion Orientation;           // +0x10  .text:06d07dad .. .text:06d07dbd
    unsigned int TargetId;            // +0x20  .text:06d07dfa, target->get_id() or 0
    unsigned short TargetSubObjId;    // +0x24  .text:06d07e04, CGuided+0xf8
    unsigned short Pad26;
    unsigned int ArchetypeNickname;   // +0x28  .text:06d07d58
    unsigned int Field_0x2c;          // +0x2c  .text:06d07d5b  MOV [ESI + 0x2c],0x0
    unsigned int OwnerId;             // +0x30  .text:06d07dc6, CProjectile::OwnerId (+0xe4)
    unsigned short SubObjId;          // +0x34  .text:06d07e12, SUBOBJ_ID_NONE
    unsigned short Pad36;
};
// rpclocal's sender 0x06d8a730 reads up to +0x34 (a word) and caseD_37 rebuilds 0x38 bytes.
static_assert(sizeof(FServerCreateGuidedInfo) == 0x38, "CREATEGUIDED argument struct 0x38");

// ---------------------------------------------------------------------------
// The builders, named for what retail's RTTI says they build.
// ---------------------------------------------------------------------------

// .text:06d07690 (218 instructions), __cdecl -- the caller balances with
// `ADD ESP,0x8` at .text:06cf58ab.
void __cdecl CShipCreateInfo_Fill(CShipCreateInfo* Info, CShip* Ship);

// .text:06d07950 (181 instructions), __cdecl -- the caller balances with
// `ADD ESP,0x8` at .text:06d0827e. Its only caller is StarSystem::add_player's
// multiplayer solar walk.
void __cdecl CSolarCreateInfo_Fill(CSolarCreateInfo* Info, CSolar* Solar);

// .text:06d07b90 (75 instructions), __cdecl.
void __cdecl ServerLootDesc_Fill(FServerLootDesc* Desc, CLoot* Loot);

// .text:06d07c80 (62 instructions), __cdecl. CProjectile covers both CMine and
// CCounterMeasure; retail's own Ghidra signature says CObject and only ever
// reads CObject/CProjectile fields.
void __cdecl ServerCreateProjectileInfo_Fill(FServerCreateProjectileInfo* Info,
                                             CProjectile* Projectile);

// .text:06d07d40 (71 instructions), __cdecl.
void __cdecl ServerCreateGuidedInfo_Fill(FServerCreateGuidedInfo* Info, CGuided* Guided);
