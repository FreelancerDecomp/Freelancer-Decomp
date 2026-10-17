// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_createinfo.cpp -- the six CREATE* record builders that Observer
// ::update needs before its five create branches can actually send anything.
//
//   (the create-info constructors, destructor and clear() are inline in
//    src/include/server_createinfo.h -- see the note below the helper)
//   FUN_06d07690  .text:06d07690  CShipCreateInfo fill                 (218 insns)
//   FUN_06d07b90  .text:06d07b90  LootDesc fill                        ( 75 insns)
//   FUN_06d07c80  .text:06d07c80  CREATECOUNTER / CREATEMINE fill      ( 62 insns)
//   FUN_06d07d40  .text:06d07d40  CREATEGUIDED fill                    ( 71 insns)
//
// Ground truth is EXE\Disasm\Server.dll.txt. The decompiled .c agrees on shape
// here but loses every register-allocated value in FUN_06d07690 (its
// `unaff_EBX` / `unaff_ESI` for the quaternion's first two dwords are the
// I3DMathEngine call's out-parameter, read back off the stack at .text:06d0770a
// and .text:06d0770e).
//
// See src/include/server_createinfo.h for how RTTI names these records and for
// the field-by-field agreement with rpclocal's independent reading of the same
// bytes from the receiving side.
// ---------------------------------------------------------------------------

#include "../include/server_createinfo.h"

#include "../include/common_archetype.h"
#include "../include/common_cequip.h"
#include "../include/common_csys.h"       // Matrix
#include "../include/dacom.h"             // DACOM_Acquire, ICOManager, AGGDESC (FUN_06d5ff70)
#include "../include/common_iface.h"      // IObjRW
#include "../include/common_misc.h"       // DamageList, SUBOBJ_ID_NONE
#include "../include/common_reputation.h" // Reputation::Vibe::GetRank
#include "../include/x86math.h"           // I3DMathEngine::MatrixToQuaternion

// ===========================================================================
// FUN_06d5ff70 -- see the declaration in server_createinfo.h.
//   .text:06d5ff70  SUB  ESP,0x10
//   .text:06d5ff73  MOV  [ESP + 0x8],"3DMathEngine"     ; desc.name
//   .text:06d5ff7b  MOV  [ESP + 0xc],0x0                ; desc third dword
//   .text:06d5ff83  MOV  [ESP + 0x4],0xc                ; desc.size
//   .text:06d5ff8b  CALL [->DACOM.DLL::DACOM_Acquire]
//   .text:06d5ff9e  CALL [ECX + 0xc]                    ; CreateInstance(&desc, &[ESP])
//   .text:06d5ffa1  MOV  EAX,[ESP]                      ; return the out slot
// The out slot is never initialised before the call, and neither the manager
// nor the result is tested. The port seeds the out slot with null so that a
// CreateInstance that does not write it hands back null rather than stack
// residue; every retail caller dereferences the result either way.
// ===========================================================================
I3DMathEngine* Server_Acquire3DMathEngine()
{
    struct FAGGDESC12
    {
        unsigned long Size;       // +0x00  0xc
        const char* Name;         // +0x04  "3DMathEngine"
        void* Field_0x08;         // +0x08  0
    };
    FAGGDESC12 Desc;
    Desc.Size = 0xc;
    Desc.Name = "3DMathEngine";
    Desc.Field_0x08 = nullptr;

    void* Engine = nullptr;
    DACOM_Acquire()->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &Engine);
    return static_cast<I3DMathEngine*>(Engine);
}

// .data:DAT_06d8da08 -- StarSys.cpp's file-static engine, shared by the four
// record builders below (.text:06d076e5, .text:06d07997, .text:06d07c0a,
// .text:06d07cb0, .text:06d07d80) and by FUN_06d0a200 / FUN_06d0aad0 /
// FUN_06d0ccde's own lazy fetches of the same slot.
static I3DMathEngine* GStarSysMathEngine = nullptr;

// ===========================================================================
// The shared quaternion helper.
//
//   .text:06d076e5  MOV  EAX,[.data:DAT_06d8da08]
//   .text:06d076ee  JNZ  LAB_06d076fa
//   .text:06d076f0  CALL FUN_06d5ff70 / MOV [.data:DAT_06d8da08],EAX
//   .text:06d07700  PUSH EDX        ; &CObject::Orientation  (object + 0x08)
//   .text:06d07705  PUSH EDX        ; the 16-byte out slot
//   .text:06d07706  PUSH EAX        ; the engine
//   .text:06d07707  ff5150  CALL dword ptr [ECX + 0x50]
// I3DMathEngine's slot +0x50 is MatrixToQuaternion; it is the same slot the
// observer update calls at .text:06cf5cff. No null test on the engine and no
// default for the output: retail's out slot is uninitialised stack that the
// call always fills.
// ===========================================================================
void Server_MatrixToQuat(Quaternion& Out, Matrix const& In)
{
    if (GStarSysMathEngine == nullptr)
        GStarSysMathEngine = Server_Acquire3DMathEngine();
    GStarSysMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Out),
                                           reinterpret_cast<FMatrix const&>(In));
}

// ===========================================================================
// QuaternionToMatrix through the same engine -- the step both object builders
// take from their request's +0x58 quaternion back to CreateParms' 3x3:
//   FUN_06d0a200  .text:06d0a377 MOV EAX,[.data:DAT_06d8da08] / JNZ / CALL FUN_06d5ff70
//                 .text:06d0a3a3 LEA EDX,[EBP + 0x58] / PUSH EDX ; the quaternion
//                 .text:06d0a3a7 LEA EDX,[ESP + 0x11c] / PUSH EDX ; 36-byte out slot
//                 .text:06d0a3b0 CALL dword ptr [ECX + 0x54]
//   FUN_06d0a880  .text:06d0aa9f .. .text:06d0aaed, the same shape.
// Both then copy the nine dwords with REP MOVSD into the parms. No null test on
// the engine.
// ===========================================================================
void Server_QuatToMatrix(Matrix& Out, Quaternion const& In)
{
    if (GStarSysMathEngine == nullptr)
        GStarSysMathEngine = Server_Acquire3DMathEngine();
    GStarSysMathEngine->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Out),
                                           reinterpret_cast<FQuaternion const&>(In));
}

// ===========================================================================
// CEqObjCreateInfo / CShipCreateInfo / CSolarCreateInfo members --
// .text:06cf63f0 (ctor), .text:06cf64d0 (dtor), .text:06cf0260 and
// .text:06cf0410 (clear) -- are defined INLINE in
// src/include/server_createinfo.h, because retail compiled them from a shared
// header into both Server.dll and rpclocal.dll (rpclocal's copies are
// .text:06d83df0 / .text:06d843b0 / .text:06d83ed0 / .text:06d83fa0). The
// store-by-store derivation is quoted there, next to each member.
// ===========================================================================

// ===========================================================================
// FUN_06d07690 -- CShipCreateInfo fill. __cdecl(Info, Ship).
//
// EDI is Info (read from the param_1 slot at .text:06d076b7, i.e. AFTER the
// PUSH EDI at .text:06d076ae -- `MOV EDI,[ESP+0x5c]` with ESP at entry-0x58)
// and ESI is Ship.
// ===========================================================================
void __cdecl CShipCreateInfo_Fill(CShipCreateInfo* Info, CShip* Ship)
{
    // (w4-server-guards) No null test on either argument: .text:06d076af
    // `MOV ECX,ESI` / 06d076b1 calls through Ship and 06d076bd stores through
    // Info with nothing in between.

    // .text:06d076b1  CALL [->COMMON.DLL::CShip::is_using_tradelane]
    // .text:06d076bd  MOV byte ptr [EDI + 0xdc],AL
    Info->bUsingTradelane =
        static_cast<unsigned char>(Ship->is_using_tradelane() ? 1 : 0);

    // .text:06d076c3  CALL [->COMMON.DLL::CObject::get_archetype]
    // .text:06d076c9  MOV EAX,[EAX + 0x8]   -- Archetype::Root+0x08 is Nickname.
    // (w4-server-guards) The archetype is read with no null test.
    Archetype::Root const* const Arch = Ship->get_archetype();
    Info->ArchetypeNickname = Arch->get_id();

    // .text:06d076cf  LEA EAX,[ESI + 0x2c] -- the position is read as a FIELD,
    // not through get_position(); +0x2c is EngineObject::WorldPosition.
    Info->Position = Ship->WorldPosition;

    // .text:06d076fd  LEA EDX,[ESI + 0x8] -- and the orientation likewise, as
    // the raw Matrix at EngineObject+0x08.
    Server_MatrixToQuat(Info->Orientation, Ship->Orientation);

    // .text:06d07731  ff5054  CALL dword ptr [EAX + 0x54]
    // CObject's vtable +0x54 is get_velocity: in Common.dll's own named tables
    // ??_7CEqObj@@6B@ (.rdata:0639be5c) has .rdata:0639beb0 == base + 0x54
    // pointing at .text:CObject::get_velocity, and ??_7CLoot@@6B@ agrees at its
    // own .rdata:0639d870. It returns a Vector by value, which is why retail
    // pushes a 12-byte out slot and copies three dwords back.
    Info->Velocity = Ship->get_velocity();

    // .text:06d07753  ff5258  CALL dword ptr [EDX + 0x58] -- the very next slot,
    // .rdata:0639beb4 == CObject::get_angular_velocity.
    Info->AngularVelocity = Ship->get_angular_velocity();

    // .text:06d0776c  MOV ECX,[ESI + 0xb0] -- CObject::Id, read as a field.
    Info->ObjId = Ship->Id;
    Info->Field_0x6c = 0u;                      // .text:06d07778

    // .text:06d07780  ff92ac000000  CALL dword ptr [EDX + 0xac]
    // .rdata:0639bf08 == CEqObj vtable base + 0xac == CEqObj::get_equip_desc_list.
    // The argument is `Info + 0x70`, the EquipDescVector itself.
    Ship->get_equip_desc_list(Info->Equipment);

    // .text:06d0778f  CALL [->COMMON.DLL::CEqObj::get_collision_group_des...]
    // The decorated import spells the element type out:
    // ?get_collision_group_description@CEqObj@@QBEXAAV?$list@UCollisionGroupDesc@@...
    Ship->get_collision_group_description(Info->CollisionGroups);

    // .text:06d07795  MOV EDX,[ESI + 0x29c] -- moved as a dword, but +0x29c is
    // `float Throttle` (common_cobject.h pins it), and rpclocal's encoder proves
    // the type from the other end: it FLDs the field this one feeds and
    // quantizes it with the 127.0 [-1,+1] scale.
    Info->Throttle = Ship->Throttle;

    // .text:06d077a1 .. .text:06d077f6 -- CEqObj+0x104 then the Costume block at
    // CEqObj+0x108..+0x138. Four head dwords, the count, and then exactly
    // `count` accessory dwords -- .text:06d077e8 `MOV ECX,[ECX + EDX*0x1]` walks
    // source+0x118 upward while EBP counts to [Info + 0x44].
    Info->VibeGroupId = static_cast<int>(Ship->VibeId);
    Info->CostumeHead = Ship->SpaceCostume.PartIds[0];
    Info->CostumeBody = Ship->SpaceCostume.PartIds[1];
    Info->CostumeLeftHand = Ship->SpaceCostume.PartIds[2];
    Info->CostumeRightHand = Ship->SpaceCostume.PartIds[3];
    Info->CostumeAccessoryCount = Ship->SpaceCostume.AccessoryCount;
    // Unbounded, as retail: the signed JLE at .text:06d077d7 skips a count <= 0,
    // and the JL at .text:06d077f6 re-reads the bound from Info+0x44 each pass.
    for (int Index = 0; Index < Info->CostumeAccessoryCount; ++Index)
        Info->CostumeAccessory[Index] = Ship->SpaceCostume.AccessoryIds[Index];

    Info->VoiceId = Ship->VoiceId;               // .text:06d077fe,  CEqObj+0x13c
    Info->Field_0xb0 = Ship->GroupNameId;        // .text:06d07807,  CShip+0x21c
    Info->OwnerPlayer = Ship->OwnerPlayer;       // .text:06d07813,  CSimple+0xb4
    Info->DockingBaseId = Ship->DockingBaseId;   // .text:06d0781f,  CEqObj+0x160
    // .text:06d07822  MOV ECX,dword ptr [ESI + 0x1b4]
    // .text:06d07828  MOV dword ptr [EDI + 0xd8],ECX
    // CShip+0x1b4 is the multiplayer group id (common_cobject.h GroupId).
    Info->Field_0xd8 = Ship->GroupId;

    // .text:06d07830  CALL [->COMMON.DLL::CSimple::get_hit_pts]
    // .text:06d07836  CALL MSVCRT.DLL::ftol            ; truncate toward zero
    // .text:06d0783f  MOV [EDI + 0x68],EAX
    Info->HitPts = static_cast<int>(Ship->get_hit_pts());

    // .text:06d0784a  CALL [->COMMON.DLL::Reputation::Vibe::GetRank]
    // __cdecl with two pushed arguments -- `LEA EAX,[ESI + 0x104]` (the group id
    // BY REFERENCE, not a Vibe object) and the address of a stack unsigned. The
    // `ADD ESP,0xc` at .text:06d0786e balances those two plus the operator_new
    // argument pushed in between.
    {
        unsigned int Rank = 0u;
        int const GroupId = static_cast<int>(Ship->VibeId);
        Reputation::Vibe::GetRank(GroupId, Rank);
        Info->ReputationRank = Rank;             // .text:06d0785a
    }

    // -----------------------------------------------------------------------
    // .text:06d07864 .. .text:06d07938 -- and now the part that looks like a
    // decode error and is not.
    //
    // Retail builds a COMPLETE DamageList on the stack (operator_new(0x14) for
    // the list sentinel at .text:06d07864, then the four POD fields zeroed at
    // .text:06d07879/.../.text:06d07885), walks the ship's equipment with a
    // CEquipTraverser(0x100) and adds one entry per item --
    //   .text:06d078be  ff523c  CALL dword ptr [EDX + 0x3c]   ; CEquip::GetHitPoints
    //   .text:06d078ca  CALL [->COMMON.DLL::DamageList::add_damage_entry]
    // with the arguments pushed 0 / hit-points / id -- and then DESTROYS it at
    // .text:06d078f0 .. .text:06d07938 without ever storing it anywhere. The
    // record's own DamageList at +0x8c is left exactly as the constructor
    // made it: empty.
    //
    // That is not a misread. The list's base is the stack slot at entry-0x28
    // (`LEA ECX,[ESP+0x40]` at .text:06d078c6 with three arguments already
    // pushed, ESP at entry-0x68), no register ever holds `Info + 0x8c`, and the
    // teardown loop at .text:06d078dd reads back the same entry-0x24 head it
    // allocated. Ghidra's own output agrees: `aDStack_40` is a local and
    // `param_1 + 0x8c` never appears.
    //
    // It is reproduced rather than dropped because it is what retail does, and
    // because CEquip::GetHitPoints is virtual -- some override could in
    // principle have a side effect this port has not seen yet.
    // -----------------------------------------------------------------------
    {
        DamageList Discarded;
        // .text:06d07889  PUSH 0x100 -- the ONE-argument CEquipTraverser ctor
        // (.rdata:06d64204 "??0CEquipTraverser@@QAE@H@Z"), mask 0x100. Note this
        // is a different mask and a different overload from the (-1, true) the
        // damage-list slot uses.
        CEquipTraverser Walk(0x100);
        for (;;) {
            // .text:06d078af  CALL EBP=>COMMON.DLL::CEquipManager::Traverse
            CEquip* const Equip = Ship->EquipManager.Traverse(Walk);
            if (Equip == nullptr)
                break;
            // .text:06d078b7  MOV DI,word ptr [EAX + 0x8] -- the id is taken as
            // a WORD out of CEquip+0x08 while EDI still holds the high half of
            // the Info pointer; the callee only reads the low word.
            Discarded.add_damage_entry(Equip->Id, Equip->GetHitPoints(),
                                       DamageEntry::SubObjFate::None);
        }
    }
}

// ===========================================================================
// FUN_06d07950 -- CSolarCreateInfo fill. __cdecl(Info, Solar); its only caller,
// StarSystem::add_player's multiplayer solar walk, balances with `ADD ESP,0x8`
// at .text:06d0827e. Ported 2026-09-24 from the raw listing
// (.text:06d07950 .. .text:06d07b8c).
//
// ESI is Solar (read at .text:06d0796a, `[ESP+0x58]` two pushes into the 0x48
// frame == entry+8) and EDI is Info (.text:06d0797a, entry+4). It is the ship
// fill's shape minus everything ship-specific: no tradelane byte, no
// velocities, no throttle, no rank, no owner, no +0x1b4 -- and two solar bytes
// of its own at the end. Nothing is null-tested; the caller only passes an
// object whose (ObjectClass & 0x303) == 0x303.
// ===========================================================================
void __cdecl CSolarCreateInfo_Fill(CSolarCreateInfo* Info, CSolar* Solar)
{
    // .text:06d07971  CALL [->COMMON.DLL::CObject::get_archetype]
    // .text:06d07977  MOV EAX,[EAX + 0x8] / .text:06d0797e MOV [EDI + 0x8],EAX
    Info->ArchetypeNickname = Solar->get_archetype()->get_id();

    // .text:06d07981 .. .text:06d07994 -- the three dwords at CObject+0x2c, as
    // a field, into Info+0x4c.
    Info->Position = Solar->WorldPosition;

    // .text:06d07997 .. .text:06d079d7 -- DAT_06d8da08's MatrixToQuaternion on
    // CObject+0x08 (`LEA EDX,[ESI + 0x8]`), the four result dwords into +0x58.
    Server_MatrixToQuat(Info->Orientation, Solar->Orientation);

    Info->ObjId = Solar->Id;                     // .text:06d079da / .text:06d079e0
    Info->Field_0x6c = 0u;                       // .text:06d079e6  MOV [EDI + 0x6c],EBX

    // .text:06d079ee  ff92ac000000  CALL dword ptr [EDX + 0xac] on Info+0x70 --
    // CEqObj::get_equip_desc_list, the same slot the ship fill uses.
    Solar->get_equip_desc_list(Info->Equipment);

    // .text:06d079fd  CALL [->COMMON.DLL::CEqObj::get_collision_group_description]
    // on Info+0x80.
    Solar->get_collision_group_description(Info->CollisionGroups);

    // .text:06d07a03 .. .text:06d07a5e -- CEqObj+0x104 into +0x10, the four
    // Costume head dwords at CEqObj+0x108.. into +0x14.., the count at
    // CEqObj+0x138 into +0x44 (.text:06d07a36), and then exactly `count`
    // accessory dwords, source CEqObj+0x118.. walked by the distance
    // `ECX - EAX` against the destination pointer (.text:06d07a3b / 06d07a50).
    // The loop re-reads the bound from Info+0x44 each pass (.text:06d07a55) and
    // is not clamped.
    Info->VibeGroupId = static_cast<int>(Solar->VibeId);
    Info->CostumeHead = Solar->SpaceCostume.PartIds[0];
    Info->CostumeBody = Solar->SpaceCostume.PartIds[1];
    Info->CostumeLeftHand = Solar->SpaceCostume.PartIds[2];
    Info->CostumeRightHand = Solar->SpaceCostume.PartIds[3];
    Info->CostumeAccessoryCount = Solar->SpaceCostume.AccessoryCount;
    for (int Index = 0; Index < Info->CostumeAccessoryCount; ++Index)
        Info->CostumeAccessory[Index] = Solar->SpaceCostume.AccessoryIds[Index];

    Info->VoiceId = Solar->VoiceId;               // .text:06d07a60 / .text:06d07a66
    Info->DockingBaseId = Solar->DockingBaseId;   // .text:06d07a69 / .text:06d07a6f

    // .text:06d07a72 / .text:06d07a78 -- CSolar+0x1a8 into +0xad, and
    // .text:06d07a7e / .text:06d07a86 -- CSolar+0x1a9 into +0xac.
    Info->Field_0xad = static_cast<unsigned char>(Solar->bDestructible ? 1 : 0);
    Info->Field_0xac = static_cast<unsigned char>(Solar->bDynamic ? 1 : 0);

    // .text:06d07a8c  CALL [->COMMON.DLL::CSimple::get_hit_pts]
    // .text:06d07a92  CALL MSVCRT.DLL::ftol / .text:06d07a97 MOV [EDI + 0x68],EAX
    Info->HitPts = static_cast<int>(Solar->get_hit_pts());

    // .text:06d07a9e .. .text:06d07b78 -- the same dead DamageList the ship fill
    // builds: operator_new(0x14) sentinel (.text:06d07aa4), the four POD fields
    // zeroed (.text:06d07ab5 .. 06d07ac5), CEquipTraverser(0x100)
    // (.text:06d07ad6) over CEqObj+0xe4 (.text:06d07ae6), one
    // DamageList::add_damage_entry(equip->Id as a word, GetHitPoints (vtable
    // +0x3c), 0) per item (.text:06d07af7 .. .text:06d07b0a), then every node
    // erased and freed (.text:06d07b30 .. .text:06d07b6c) and the sentinel
    // freed (.text:06d07b73). It is a stack local; nothing reads it.
    {
        DamageList Discarded;
        CEquipTraverser Walk(0x100);
        for (;;) {
            CEquip* const Equip = Solar->EquipManager.Traverse(Walk);
            if (Equip == nullptr)
                break;
            Discarded.add_damage_entry(Equip->Id, Equip->GetHitPoints(),
                                       DamageEntry::SubObjFate::None);
        }
    }
}

// ===========================================================================
// FUN_06d07b90 -- the 0x48-byte LootDesc. __cdecl(Desc, Loot).
//
// ESI is Loot (param_2, read at .text:06d07b94 after one PUSH) and EDI is Desc
// (param_1, read at .text:06d07b9f after two).
// ===========================================================================
void __cdecl ServerLootDesc_Fill(FServerLootDesc* Desc, CLoot* Loot)
{
    // (w4-server-guards) No null test on Desc, Loot, ContentsArch or the
    // archetype: .text:06d07b98 reads Loot, 06d07ba3 stores through Desc,
    // 06d07bab `MOV EDX,[ECX + 0x8]` reads ContentsArch, 06d07bcb
    // `MOV ECX,[EAX + 0x8]` reads get_archetype()'s result, and 06d07bec
    // `CMP EAX,[ECX + 0x14]` reads ContentsArch again -- all unconditionally.

    Desc->LootId = Loot->Id;                     // .text:06d07b98 / .text:06d07ba3

    // .text:06d07ba5  MOV ECX,[ESI + 0xec]      ; CLoot::ContentsArch
    // .text:06d07bab  MOV EDX,[ECX + 0x8]       ; Archetype::Root+0x08 Nickname
    Archetype::Equipment const* const Contents = Loot->ContentsArch;
    Desc->ContentsNickname = Contents->get_id();

    Desc->ContentsHitPts = Loot->ContentsHitPts; // .text:06d07bb1  FLD [ESI+0xf4]
    Desc->Units = Loot->Units;                   // .text:06d07bba / .text:06d07bc2

    // .text:06d07bc5  CALL [->COMMON.DLL::CObject::get_archetype]
    Archetype::Root const* const Container = Loot->get_archetype();
    Desc->ContainerNickname = Container->get_id();

    // .text:06d07bd3  CALL [->COMMON.DLL::CSimple::get_hit_pts]
    // .text:06d07bd9  d95f14  FSTP float ptr [EDI + 0x14] -- kept as a FLOAT
    // here, unlike CShipCreateInfo+0x68 which ftol's the same accessor.
    Desc->HitPts = Loot->get_hit_pts();

    // .text:06d07be0  ff9288000000  CALL dword ptr [EDX + 0x88]
    // CObject-family vtable +0x88 is get_name: ??_7CLoot@@6B@'s own slot at
    // .rdata:0639d8a4 (base .rdata:0639d81c) is .text:CLoot::get_name, and the
    // matching slot of ??_7CEqObj@@6B@ at .rdata:0639bee4 is CEqObj::get_name.
    //
    // .text:06d07bec  CMP EAX,dword ptr [ECX + 0x14] -- ECX is still
    // CLoot::ContentsArch, so +0x14 is Archetype::Root::IdsName. The name only
    // travels when it is an OVERRIDE; when get_name() answers the contents
    // archetype's own ids_name the field keeps whatever the caller left there
    // (the caller zeroed +0x18 at .text:06cf577f).
    unsigned int const Name = Loot->get_name();
    if (Name != static_cast<unsigned int>(Contents->IdsName))
        Desc->NameOverride = Name;

    Desc->Position = Loot->WorldPosition;        // .text:06d07bf4 .. .text:06d07c07
    Server_MatrixToQuat(Desc->Orientation, Loot->Orientation); // .text:06d07c1f/.text:06d07c29
    Desc->Velocity = Loot->get_velocity();       // .text:06d07c53  CALL [EAX + 0x54]
    // .text:06d07c69 / .text:06d07c6f
    Desc->bTemporary = static_cast<unsigned char>(Loot->bTemporary ? 1 : 0);
}

// ===========================================================================
// FUN_06d07c80 -- CREATECOUNTER (IClient::SendServerCreateCounter) and CREATEMINE
// (IClient::SendServerCreateMine) share one
// builder. __cdecl(Info, Projectile).
//
// The parameters are in the OPPOSITE registers from the loot builder: ESI is
// param_1 (.text:06d07c84 `MOV ESI,[ESP+0x18]`, one PUSH deep) and EDI is
// param_2 (.text:06d07c89, two deep). Ghidra's own stack-variable annotation
// says so on both lines, and every subsequent store goes through ESI.
// ===========================================================================
void __cdecl ServerCreateProjectileInfo_Fill(FServerCreateProjectileInfo* Info,
                                             CProjectile* Projectile)
{
    // (w4-server-guards) No null test on Info, Projectile or the archetype:
    // .text:06d07c8d reads Projectile, 06d07c95 stores through Info and
    // 06d07c9d `MOV ECX,[EAX + 0x8]` reads get_archetype()'s result.
    Info->ObjId = Projectile->Id;                // .text:06d07c8d / .text:06d07c95

    Archetype::Root const* const Arch = Projectile->get_archetype();
    Info->ArchetypeNickname = Arch->get_id();    // .text:06d07ca0

    // .text:06d07ca9  MOV AX,word ptr [EDX] / .text:06d07cac MOV [ESI + 0xc],AX
    Info->SubObjId = SUBOBJ_ID_NONE;

    Server_MatrixToQuat(Info->Orientation, Projectile->Orientation); // .text:06d07cc5..cef
    Info->OwnerId = Projectile->OwnerId;         // .text:06d07cf0, CProjectile+0xe4
    Info->Position = Projectile->WorldPosition;  // .text:06d07cf9 .. .text:06d07d0c
    Info->Field_0x44 = 0u;                       // .text:06d07d13
    Info->Velocity = Projectile->get_velocity(); // .text:06d07d1f  CALL [EDX + 0x54]
}

// ===========================================================================
// FUN_06d07d40 -- CREATEGUIDED (IClient::SendServerCreateGuided). __cdecl(Info, Guided).
// ===========================================================================
void __cdecl ServerCreateGuidedInfo_Fill(FServerCreateGuidedInfo* Info, CGuided* Guided)
{
    // (w4-server-guards) No null test on Info, Guided or the archetype:
    // .text:06d07d4b calls through Guided, 06d07d51 `MOV EAX,[EAX + 0x8]`
    // reads the archetype and 06d07d58 stores through Info.
    Archetype::Root const* const Arch = Guided->get_archetype();
    Info->ArchetypeNickname = Arch->get_id();    // .text:06d07d58
    Info->Field_0x2c = 0u;                       // .text:06d07d5b
    Info->ObjId = Guided->Id;                    // .text:06d07d62 / .text:06d07d68
    Info->Position = Guided->WorldPosition;      // .text:06d07d6a .. .text:06d07d7d
    Server_MatrixToQuat(Info->Orientation, Guided->Orientation); // .text:06d07d95..dbd
    Info->OwnerId = Guided->OwnerId;             // .text:06d07dc6, CProjectile+0xe4

    // .text:06d07dc9 .. .text:06d07dfa -- CGuided::get_target() inlined, then
    // IObjRW's vtable slot +0x20 (get_id) on the result:
    //   .text:06d07dc9  MOV EAX,[EDI + 0xf0]     ; BaseWatcher::Watched
    //   .text:06d07dd3  ADD EAX,-0x8             ; Watchable -> IObjRW
    //   .text:06d07dd8  JZ  LAB_06d07df8         ; the adjusted pointer null-checked
    //   .text:06d07de4  MOV EDX,[EAX + -0x8]
    //   .text:06d07dea  ff5220  CALL dword ptr [EDX + 0x20]
    // Retail re-loads +0xf0 for the call and has a LAB_06d07def arm that does
    // `XOR ECX,ECX / MOV EDX,[ECX]` -- a guaranteed null dereference, reachable
    // only if the field changed between the two loads, which it cannot. The port
    // evaluates get_target() once.
    IObjRW* const Target = static_cast<IObjRW*>(Guided->get_target());
    Info->TargetId = (Target != nullptr) ? Target->get_id() : 0u;

    Info->TargetSubObjId = Guided->SubTarget;    // .text:06d07dfd / .text:06d07e04
    Info->SubObjId = SUBOBJ_ID_NONE;             // .text:06d07e0e / .text:06d07e12
}
