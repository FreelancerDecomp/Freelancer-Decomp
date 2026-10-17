// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_server_types.h"   // CHARACTER_ID (PlayerData::selected_character)
#include "common_goods.h"            // EquipDescList (PlayerData::equipment, +0x278)
#include "common_archetype.h"        // SubObjectID::EquipIdMaker (+0x3C8), CollisionGroupDesc
#include "common_text.h"             // FmtStr (PlayerData::mission_log, +0x390)

#include <cstddef>
#include <list>
#include <map>
#include <string>
#include <vector>

class CPlayerTradeOffer;   // common_tradeoffer.h (PlayerData::trade_offer, +0x3CC)

// ---------------------------------------------------------------------------
// PlayerData — the port's one and only player record, retail's `struct
// PlayerData` (0x418 bytes, stride of the Server.dll `Players` array).
//
// THIS HEADER IS THE ONLY DEFINITION. Do not re-declare it in a .cpp.
// Until 2026-09-17 src/Common/server_stubs.cpp carried a second, 24-field
// `struct PlayerSlot` that the exported g_PlayersDB[] array was actually built
// from, while Server.dll (and Common.dll's own server_savegame.cpp) read the
// 52-field definition below through the very same PlayerDB_GetPlayer pointer.
// Only `session_id` (+0) and `active` (+4) happened to line up; every other
// field was read at a wrong address. This header being the single definition is
// what now guards against a repeat.
//
// 2026-09-17, second pass: the storage itself moved. The port used to keep TWO
// player tables -- Common.dll's `g_PlayersDB[64]` of this type, written by
// every IServerImpl handler, and Server.dll's `PlayerDB::m_PlayerArray` of
// calloc'd 0x418-byte blobs, written by the PlayerDB methods -- and they never
// saw each other. Retail has exactly one, and it lives in Server.dll:
//   .data:06d90260  ?Players@@3VPlayerDB@@A   (ordinal 352, RVA 0xb0260)
// a single exported `class PlayerDB Players` object whose FIRST member is the
// heap array base. Every walker in the listing loads that base out of it --
//   06ce900d  MOV  EDX,dword ptr [.data:Players]
//   06ce9013  IMUL ECX,ECX,0x418
//   06ce9019  CMP  dword ptr [ECX + EDX*0x1 + 0x348],EBX
// -- so `[.data:Players]` is a POINTER, not the array. PlayerDB::init
// (0x06d510c0) is what fills it: `operator new(MaxPlayers*0x418 + 4)`, count in
// the first dword, base at +4, and `[this+0x1c] = base + MaxPlayers*0x418`.
// The single store now lives in PlayerDB (src/Server/server_classes.cpp) and
// `PlayerSlot` below is only a compatibility alias for the old spelling.
//
// Offsets in the comments are RETAIL PlayerData offsets and are quoted in the
// two bases the Server.dll listing itself uses:
//   * `+0xNNN` — from the slot base, as PlayerData methods address `this`.
//   * `-0xNN`  — from the END of the slot, as the array walkers address it
//                (`IMUL EAX,idx,0x418` then `[EAX + Players - 0xNN]`).
// The stride is 0x418 (PlayerDB::free passes it to the vector-dtor helper at
// 0x06d50e64), so `-0xNN` == `+(0x418 - 0xNN)`. The port does NOT reproduce the
// retail byte layout: retail PlayerData embeds std::string/std::list members,
// and this struct uses modern STL equivalents in a port-native order.
// ---------------------------------------------------------------------------
struct PlayerData {
    // Retail FUN_06d494f0 (.text:06d494f0 .. .text:06d49751) -- the element
    // constructor PlayerDB::init's __vec_new runs over every slot
    // (PUSH FUN_06d494f0 at .text:06d51124). Defined in
    // src/Server/server_classes.cpp.
    PlayerData();
    // Retail FUN_06d50e90 -- the element destructor PlayerDB::free's vector-
    // deleting helper runs (PUSH FUN_06d50e90 at .text:06d50e50). Its first act
    // is the slot teardown FUN_06d49230 (.text:06d50eb9, PlayerData_Teardown);
    // the member destructors follow. Defined in src/Server/server_classes.cpp.
    ~PlayerData();

    // (2026-09-24, claude-code:w5-server-06d4) REMOVED, all port inventions with
    // no retail storage: `session_id` and `active` (retail's occupancy test is
    // client_id at +0x348 -- PlayerDB::is_valid 0x06ce1a00 and every pub::Player
    // guard, e.g. .text:06d56486), `hull_hp` / `hull_max_hp` / `shield_hp` /
    // `shield_max_hp` (nothing but the old reset ever touched them),
    // `active_mission` / `mission_state` (pub::Player::SetMsnID / GetMsnID write
    // and read +0x408/+0x40c = group_mission_id / group_mission_set_by),
    // `character_flid` (a second name for +0x3FC, see Field_0x3FC),
    // `position` / `orientation` (a second copy of +0x350/+0x35C, see initial_pos /
    // initial_ornt) and `mission_message_flag` (DisplayMissionMessage's "-0xd8"
    // byte IS +0x340, bSkipNextAutosave).
    // (2026-09-24, claude-code:w3-baseenter) `bool location_active` and
    // `bool base_active` were removed from here. Retail has no such flags:
    // every "in a room" / "in a base" test in Server.dll is the masked id itself
    // (`TEST [slot-0x20],0x3fffffff` / `TEST [slot-0x2c],0x3fffffff`, e.g.
    // IServerImpl::DisConnect .text:06d4500e / .text:06d45042), and the one
    // gate the port used base_active for is the +0x3F4 dword below
    // (entered_base_id).
    unsigned int current_location;     // -0x20 (+0x3F8): location ID
    unsigned int current_base;         // -0x2c (+0x3EC): base ID

    // Ship/character state.
    // Cash is an INT in retail, not a float: PlayerData::AdjustCash
    // (0x06d4f4e0) does `ADD [EDI+0x320],EAX` / `MOV ESI,[EDI+0x31c]` /
    // `ADD ESI,EAX` on plain dwords and clamps with integer CMP against
    // 0x3b9ac9ff (999999999). There is no float copy of it anywhere.
    int cash;                          // -0xfc (+0x31c): cash, clamped [0, 999999999]
    unsigned int system_id;            // -0x38 (+0x3E0): current system
    unsigned int ship_archetype;       // -0x1b4 (+0x264): ship archetype ID

    // +0x3F4 (-0x24 from the slot end). RENAMED AND RETYPED 2026-09-24
    // (claude-code:w3-baseenter): this dword is the base the session has
    // ENTERED through IServerImpl::BaseEnter, and nothing else. The port used to
    // carry it twice -- a `base_id` written by BaseEnter/BaseExit here, and a
    // `bool bAuthorized` "authorization flag" at the bottom of the struct that
    // the Req* handlers gated on and nothing ever set -- so in MP every
    // ReqAddItem / ReqRemoveItem / ReqModifyItem / ReqEquipment /
    // ReqCollisionGroups / ReqShipArch / ReqHullStatus returned at once. The
    // complete set of accesses in Server.dll.txt (grep `+ -0x24]` on the
    // `IMUL idx,0x418` walkers and `+ 0x3f4]` on slot bases):
    //   WRITES  .text:06d1774e  MOV [EDI-0x24],EBX   BaseEnter: = base id, next
    //                                                to -0x2c (current_base) and
    //                                                -0x28 (last_base_id)
    //           .text:06d17954  MOV [ESI+0x3f4],EAX  BaseExit: = 0 (EAX xor'ed)
    //           .text:06d49144  internal-login clear FUN_06d48e80: = 0
    //           .text:06d49486  slot teardown FUN_06d49230: = 0
    //           .text:06d496cb  slot initializer FUN_06d494f0: = 0
    //   READS   .text:06d17677  BaseEnter: `& 0x3fffffff` must be 0
    //           .text:06d177f8  BaseExit: `& 0x3fffffff` != 0, then == param_1
    //           .text:06d1731d / 06d17388 / 06d173fd / 06d1743d / 06d1747d /
    //           06d174fd / 06d1754d / 06d175bd / 06d175fd -- the MP gate of
    //           ReqShipArch, ReqHullStatus, ReqCollisionGroups, ReqEquipment,
    //           ReqAddItem, ReqRemoveItem, ReqModifyItem, ReqSetCash and
    //           ReqChangeCash: `SinglePlayer() || dword != 0` (UNMASKED TEST)
    // CharacterSelect writes +0x3EC/+0x3F0 from the save record but never this
    // one, and PlayerDB::cleanup's undock (FUN_06d18880) clears +0x3EC/+0x3F8
    // but not this one either -- it only goes back to 0 through BaseExit, the
    // login clear and the slot teardown.
    unsigned int entered_base_id;      // +0x3F4 (-0x24): base entered via BaseEnter, 0 = none

    // The ship FLID is ship_object_id (+0x3E4 == -0x34 from the 0x418 stride).
    // A second member, `ship_flid`, used to stand for the same retail dword;
    // Launch wrote one and ReqShipArch/pub::Player::GetShip read the other.
    // Merged 2026-09-24 (claude:srv-pdb).

    // Initial spawn position/orientation (SetInitialPos/SetInitialOrnt).
    // The constructor FUN_06d494f0 and the teardown FUN_06d49230 both store a
    // zero Vector at +0x350..+0x358 and a 3x3 IDENTITY at +0x35C (1.0f at
    // +0x35C/+0x36C/+0x37C, zeros elsewhere: .text:06d496f3..06d49735 and
    // .text:06d493cf..06d49416).
    float initial_pos[3];              // -0xc8 (+0x350): initial position vector
    float initial_ornt[9];             // -0xbc (+0x35C): initial orientation matrix (3x3)

    // ------------------------------------------------------------------
    // +0x278: the session's EquipDescList -- everything the player's ship
    // carries, mounted equipment and cargo-bay contents alike.
    //
    // This replaces the port's old `std::vector<int> cargo`, which was an
    // invention: retail has no such member, and nothing in the listing ever
    // stored a bare archetype id per cargo unit.
    //
    // The type is not a guess. Retail addresses the slot from the END of the
    // 0x418 stride (`IMUL idx,0x418` then `[idx + Players - 0x1a0]`, i.e.
    // +0x278) and calls Common.dll's EquipDescList members straight on it:
    //   .text:06d54d5b  LEA  ECX,[ECX + ESI*0x1 + 0xfffffe60]   ; &slot[-0x1a0]
    //   .text:06d54e55  LEA  ECX,[EAX + EDX*0x1 + 0xfffffe60]
    //   .text:06d54e5c  CALL [->COMMON.DLL::EquipDescList::get_cargo_space_occupied]
    //   .text:06d4f01b  CALL [->COMMON.DLL::EquipDescList::find_matching_cargo]
    //   .text:06d4f0ae  CALL [->COMMON.DLL::EquipDescList::add_equipment_item]
    //   .text:06d4f12b  CALL [->COMMON.DLL::EquipDescList::remove_equipment_item]
    //   .text:06d4f19a  CALL [->COMMON.DLL::EquipDescList::find_equipment_item]
    // and the slot reset at .text:06d49b56 runs `??0EquipDescList@@QAE@XZ` on
    // it. Retail's own container is the VC6 std::list<EquipDesc> whose head
    // node sits at +0x27c (every walker does `MOV EAX,[slot+0x27c]` /
    // `MOV ESI,[EAX]` and compares back against EAX); the port's
    // EquipDescList models that with the current CRT's std::list<EquipDesc>, so
    // the internal node layout is the only thing that differs and nothing
    // outside Common.dll touches it.
    //
    // Node-to-field mapping used throughout the port of this family (VC6 list
    // node = {_Next, _Prev, value}, so value starts at node+8):
    //   node+0x0a -> EquipDesc::Id        node+0x0c -> EquipDesc::ArchId
    //   node+0x10 -> EquipDesc::Hardpoint node+0x14 -> EquipDesc::bEquipped
    //   node+0x18 -> EquipDesc::Health    node+0x1c -> EquipDesc::Count
    // ------------------------------------------------------------------
    EquipDescList equipment;           // +0x278

    // ------------------------------------------------------------------
    // +0x26C: the hull's undamaged collision groups. ADDED 2026-09-17
    // (wave 11); PlayerData had no member for it at all, so every
    // ReqCollisionGroups round trip and every ship swap lost the list.
    //
    // The container shape is read straight off PlayerData::set_ship_arch
    // (.text:06d4ec10), which is the only writer:
    //   .text:06d4ec40  81c76c020000  ADD  EDI,0x26c        ; &this->list
    //   .text:06d4ec47  8b5f04        MOV  EBX,[EDI + 0x4]   ; _Head  (+0x270)
    //   .text:06d4ec4a  8b33          MOV  ESI,[EBX]         ; first node
    //   .text:06d4ec4c  3bf3          CMP  ESI,EBX           ; circular sentinel
    //   ... unlink + FUN_06d5ffb0(node) per node ...
    //   .text:06d4ec69  8b4f08        MOV  ECX,[EDI + 0x8]   ; _Size  (+0x274)
    //   .text:06d4ec6f  49            DEC  ECX
    // i.e. the textbook VC6 std::list triple {allocator byte, _Head, _Size}
    // at +0x26C/+0x270/+0x274. The refill names the element type for us:
    //   .text:06d4ec80  55            PUSH EBP                ; ShipArch
    //   .text:06d4ec81  CALL [->COMMON.DLL::Archetype::GetShipArch]
    //   .text:06d4ec8e  57            PUSH EDI                ; &this->list
    //   .text:06d4ec8f  8bc8          MOV  ECX,EAX            ; the Ship archetype
    //   .text:06d4ec91  CALL [->COMMON.DLL:: ... ]            ; IAT .rdata:06d644a0
    // and that IAT slot demangles to
    //   ?get_undamaged_collision_group_list@EqObj@Archetype@@QBE_NAAV?$list@
    //    UCollisionGroupDesc@@V?$allocator@UCollisionGroupDesc@@@std@@@std@@@Z
    // -- `std::list<CollisionGroupDesc>&`, spelled out in the mangling. No
    // guesswork left in the element type.
    //
    // CollisionGroupDesc used to live in common_cobject.h, which is why this
    // member kept being deferred; it now lives in common_archetype.h, which
    // this header already gets through common_goods.h.
    // ------------------------------------------------------------------
    std::list<CollisionGroupDesc> collision_groups;   // +0x26C

    // +0x324 / +0x328: the "what the player had when they last landed"
    // snapshot, written by retail FUN_06d4fc30 (ported as
    // PlayerData_SnapshotEquipment). That function is a two-liner on the
    // bytes -- `MOV [this+0x324],[this+0x264]` at .text:06d4fc4b, then a
    // std::list assignment from the list at +0x278 into the one at +0x328
    // (.text:06d4fc3b/.text:06d4fc41 take both addresses, and the body is the
    // textbook VC6 list::operator= : MOVSD.REP 8 dwords per surviving node,
    // operator_delete for the surplus, operator_new(0x28) + EquipDesc copy
    // ctor for the shortfall). The port had neither field, so the snapshot
    // had nowhere to live.
    unsigned int equip_snapshot_arch;  // +0x324: ship_archetype at snapshot time
    EquipDescList equip_snapshot;      // +0x328: equipment at snapshot time

    // +0x2E4: the session Costume. Three of the four pub::Player costume
    // entry points write it directly and one fills it through Common.dll:
    //   .text:06d5663a  MOV  dword ptr [slot + 0x2e4],0x88db0044      (SetMonkey)
    //   .text:06d56691  MOV  dword ptr [EAX + 0x2e4],0x0              (SetRobot)
    //   .text:06d5669b  MOV  dword ptr [EAX + 0x2e8],0x8dffc147
    //   .text:06d566a5  MOV  dword ptr [EAX + 0x2ec],0x9dc4c883
    //   .text:06d566af  MOV  dword ptr [EAX + 0x2f0],0x93582d4f
    //   .text:06d56701  ADD  EAX,0x2e4 / PUSH EAX / PUSH 0xb8249f07 / (SetTrent)
    //                   CALL [->COMMON.DLL::GetCostumeDescriptions]
    //                   MOV  ECX,EAX
    //                   CALL [->COMMON.DLL::CostumeDescriptions::get_costume]
    // The four consecutive dwords SetRobot writes are Costume::PartIds[0..3],
    // which is what fixes the field at +0x2E4 as a Costume and not four loose
    // ids: SetTrent hands the SAME address to get_costume(int, Costume&).
    // Note the two pushes at .text:06d56706/.text:06d56707 belong to
    // get_costume (a thiscall taking two stack args), not to
    // GetCostumeDescriptions, which takes none -- there is no ESP adjustment
    // between the two calls.
    Costume costume;                   // +0x2E4

    // +0x28C: the SECOND Costume, the one the save file writes under the "com_"
    // key prefix. Retail's save serializer calls the costume writer twice, with
    // two different PlayerData addresses and two different key prefixes:
    //   .text:06d4d3e8  LEA  EAX,[EBX + 0x28c]   / PUSH .rdata:06d6b12c = "com_"
    //   .text:06d4d3f9  CALL FUN_06d46930
    //   .text:06d4d3fe  LEA  EAX,[EBX + 0x2e4]   / PUSH .rdata:06d66ee4 = ""
    //   .text:06d4d40f  CALL FUN_06d46930
    // and FUN_06d46930 indexes its third argument as PartIds[1]/[0]/[2]/[3]
    // (.text:06d46949 `MOV ESI,[EAX + 0x4]` = body, .text:06d469a1
    // `MOV EDI,[EAX]` = head, +0x8 lefthand, +0xc righthand), i.e. exactly the
    // Costume shape the field at +0x2E4 already has. So +0x28C is a Costume too,
    // and the keys it produces are com_body / com_head / com_lefthand /
    // com_righthand against the bare body / head / lefthand / righthand of the
    // other one. TODO(port): find and port the WRITER of this field -- nothing
    // in the port fills it yet, so it always serializes as "no parts".
    Costume com_costume;               // +0x28C

    // +0x2C4: the `voice` the save file records, a bare narrow nickname buffer.
    // Retail passes its ADDRESS, not its contents, to the "%s = %s\n" fprintf:
    //   .text:06d4d3d4  8d83c4020000  LEA  EAX,[EBX + 0x2c4]
    //   .text:06d4d3da  50            PUSH EAX
    //   .text:06d4d3db  PUSH .rdata:s_voice_06d6b134 = "voice"
    //   .text:06d4d3e0  PUSH .rdata:s_%s_=_%s_06d6b578
    // so it is an inline char array on the slot, not a pointer. The extent
    // 0x2C4..0x2E4 is what the two neighbours leave free (+0x28C is the com_
    // Costume above, +0x2E4 the session Costume), so 0x20 bytes.
    // TODO(port): find the writer (pub::Player::SetVoice or the save loader) --
    // nothing in the port fills it yet, so it always serializes empty.
    char voice[0x20];                  // +0x2C4

    // +0x2C0: the Length half of the TString<32> whose Data is `voice` above.
    // ADDED 2026-09-24 (claude:srv-save): CharacterSelect's record copy stores
    // FUN_06d03cf0's return value here (.text:06d4ae1f `MOV [EBX+0x2c0],EAX`,
    // and .text:06d4adfe for the null case). The 0x34-byte com_ Costume at
    // +0x28C ends exactly at +0x2C0, so this is the dword in between.
    unsigned int voice_length;         // +0x2C0

    // Rank & difficulty (retail offsets +0x284, +0x3C4)
    unsigned int rank_level;           // -0x194: player rank level
    int money_needed_to_next_rank;     // -0x190: cash needed for next rank
    float difficulty_scale;            // +0x3C4: clamped [0.0, 2.0]

    // Stats
    int num_kills;                     // -0xe4: kill count
    int num_mission_successes;         // -0xe0: mission success count
    int num_mission_failures;          // -0xdc: mission failure count

    // Reputation
    int rep_vibe;                      // -0x100: reputation vibe handle

    // Health. Both the constructor and the teardown store 0 here
    // (.text:06d4953b / .text:06d49368), not 1.0f.
    float relative_health;             // -0x1b0 (+0x268): normalized hull health (0..1)

    // Group pointer (CPlayerGroup*)
    void* group_ptr;                   // -0x14: CPlayerGroup* (nullptr if no group)

    // Group mission state. Retail keeps it PER PLAYER, not on CPlayerGroup:
    // CPlayerGroup::GetMissionID (0x06d5c330) walks its member set and returns
    // the first non-zero [PlayerData+0x408]; GetMissionSetBy (0x06d5c3b0) does
    // the same with +0x40c; SetMissionID (0x06d5c430) writes both on every
    // member and then fires IClient slot 114 (+0x1c8).
    unsigned int group_mission_id;     // -0x10 (+0x408): mission id shared by the group
    unsigned int group_mission_set_by; // -0x0c (+0x40c): who set it

    // Running total of cash awarded. Retail PlayerData::AdjustCash
    // (FUN_06d4f4e0, 0x06d4f4e0) adds the delta here first when its bool
    // argument is true, then clamps cash to [0, 999999999].
    int total_earned;                  // +0x320

    // Retail's slot-occupied gate. NOT an IClient*: the dword at -0xd0 (+0x348)
    // is a client/player id passed BY ADDRESS as `unsigned int const&` --
    // PlayerData::AdjustCash does `ADD EDI,0x348` / `PUSH EDI` before calling
    // IClient vtable +0x134, and PlayerDB::is_valid (0x06ce1a00) only ever
    // TESTs it for zero. The IClient itself is a single global (DAT_06d8fb14),
    // not a per-player pointer.
    unsigned int client_id;            // -0xd0 (+0x348): client id, 0 = slot free

    // +0x34C. The "a cheat has already been reported for this session" latch.
    // Retail reads it at .text:06d4cd0b (`CMP byte ptr [EBX + 0x34c],0x1` --
    // bail out entirely when set) and sets it at .text:06d4cd8d
    // (`MOV byte ptr [EBX + 0x34c],0x1`) immediately after PlayerData_ReportCheat
    // returns. Added 2026-09-17 (wave 8) together with the gate itself; an
    // earlier note placed this latch at +0x1a6, which is not what the bytes say.
    unsigned char bCheatReported;      // -0xcc (+0x34C)

    // +0x340. ADDED 2026-09-24 (claude:srv-save). The one-shot "skip the next
    // autosave" byte. FUN_06d4c5f0 (.text:06d4c5f0, called from .text:06d453bb)
    // sets it to 1; the autosave FUN_06d4c430 tests it at .text:06d4c57d and, when
    // it is set (or the base forbids autosaving), clears it at .text:06d4c5ce
    // instead of saving. CharacterSelect clears it at .text:06d4af92.
    // pub::Player::DisplayMissionMessage also sets it for a type-0 message
    // (.text:06d558fb `MOV byte ptr [EAX+0x340],0x1`) -- the port used to keep a
    // separate `mission_message_flag` ("-0xd8") for that write; same byte.
    unsigned char bSkipNextAutosave;   // +0x340

    // +0x344. ADDED 2026-09-24 (claude:srv-save). Failed-save counter:
    // FUN_06d4c800 increments it when FUN_06d4ccd0 returns false
    // (.text:06d4ca15 MOV EDX,[EBX+0x344] / INC EDX / MOV [EBX+0x344],EDX);
    // CharacterSelect zeroes it at .text:06d4af6e.
    unsigned int save_failure_count;   // +0x344

    // +0x414. ADDED 2026-09-24 (claude:srv-save). The save record's
    // `starting_ring` (record+0x2b8), copied here at .text:06d4af9f by
    // CharacterSelect. Its readers are not ported yet.
    unsigned int starting_ring;        // +0x414

    // CORRECTED 2026-09-17 (wave 6): +0x64 is NOT a bool "MP enable". It is the
    // CHARACTER_ID of the character this session currently has SELECTED -- a
    // 512-byte NUL-terminated narrow buffer, the same CHARACTER_ID type
    // common_server_types.h already models.
    //
    // Proof on the raw bytes, inside the CharacterSelect worker FUN_06d4a5d0
    // (EBX is that function's `this`, the slot base -- 0x06d4a61a does
    // `LEA ECX,[EBX + 0x50]` straight into the saved-characters container this
    // header documents at +0x50):
    //   .text:06d4a7cf  52              PUSH  EDX                  ; the incoming CHARACTER_ID const&
    //   .text:06d4a7d0  8d4b64          LEA   ECX,[EBX + 0x64]     ; &slot->selected_character
    //   .text:06d4a7d3  ff15ec48d606    CALL  [->COMMON.DLL::CHARACTER_ID::operator=]
    // A bool field is not the `this` of CHARACTER_ID::operator=.
    //
    // That also re-reads every "gate" site. The shape
    //   CALL IsMPServer / TEST AL,AL / JZ do_work
    //   MOV DL,[player+0x64] / TEST DL,DL / SETNZ AL / TEST AL,AL / JZ skip
    // is `!IsMPServer() || selected_character[0] != 0`, i.e. "single player, or
    // this session has actually picked a character". Read that way the gates
    // finally make sense: PlayerDB::GetActiveCharacterName (0x06d54684) refuses
    // to resolve a name for a session with no character selected, and the
    // logout player-list broadcast skips sessions that never got that far.
    //
    // The three "writers that all store zero" from the old note are the empty-
    // string stores `buf[0] = 0`: 0x06d494b3 and 0x06d4952b in the slot
    // initializer/reset, and 0x06d4a5b7 when CharacterSelect deselects. The
    // real writer is the CHARACTER_ID::operator= above. Nothing about this
    // field is dead.
    //
    // TYPE NOTE (closed 2026-09-17, wave 7): this used to be `char[0x200]`
    // because Common.dll did not export CHARACTER_ID's members, so PlayerData's
    // implicit copy-assignment would have referenced an unresolvable symbol.
    // Retail Common.dll exports all seven of them (see common_server_types.h for
    // the ordinals) and the port now does too, so the field has its real type.
    CHARACTER_ID selected_character;   // +0x64: "" = none selected

    // Weapon group (retail offset +0x380)
    std::string weapon_group_name;     // +0x380: weapon group assignment string

    // Interface state (retail offset +0x39C)
    unsigned int interface_state;      // +0x39C: UI/interface state flag

    // -----------------------------------------------------------------------
    // Mission log. The container object itself sits at +0x390, not +0x394:
    // PlayerData's dtor (FUN_06d50e90) erases `param_1 + 0xe4` (0xe4*4 = 0x390)
    // and then frees `param_1[0xe5]` (+0x394), i.e. +0x394 is the list's head
    // node, the second dword of the container. .text:06d50617 confirms the third
    // dword is the size: an erase there does `MOV ECX,[EBP + 0x398]` / `DEC ECX`
    // / `MOV [EBP + 0x398],ECX`.
    //
    // CORRECTED 2026-09-17 (wave 18). This used to be
    // `std::vector<{std::string text; unsigned int timestamp;}>`, which was
    // invented -- retail has no such struct and stores no text here at all. The
    // element is FmtStr, and both halves of the module agree:
    //
    //   WRITER  FUN_06d4cc20 (.text:06d4cc20 .. .text:06d4ccc0, RET 0x8) reads
    //           `[node + 0x4]` as the first "%u", walks the chain at `[node]`
    //           counting nodes for the second, then per node emits
    //           ", %u, %u, %u" from `[n+0x4]` (dword), `[n+0x8]` (word) and
    //           `[n+0xa]` (byte) -- and when the word is 0x21 it writes a bare
    //           ',' (.text:06d4cc9c `c6012c MOV byte ptr [ECX],0x2c`) and
    //           RECURSES into `[n+0x4]` as another record (.text:06d4cca9).
    //   LOADER  FUN_06d4ca60 builds the same thing with
    //           FmtStr::begin_mad_lib / end_mad_lib / append(Val*), allocating
    //           0x18 bytes per list node payload and 0xc per Val.
    //
    // FmtStr::Head is +0x00, FmtStr::MadLibId is +0x04, and Val is
    // { Next +0x00, Data +0x04, TypeTag +0x08, Flags +0x0a } -- the same four
    // fields, in the same places, with TypeTag 0x21 already documented in
    // common_text.h as "nested FmtStr". So this is FmtStr, not a new type.
    //
    // Checked against data: of 3234 `log =` lines in the fifty saved character
    // files on this machine, 3231 parse as
    //   MadLibId, ValCount, { Data, TypeTag, Flags } x ValCount
    // and the remaining three parse to completion only when the TypeTag == 0x21
    // recursion is honoured.
    // -----------------------------------------------------------------------
    std::list<FmtStr> mission_log;     // +0x390

    // Visit tracking. Retail keeps TWO containers here, not one, and the port
    // used to have only the second:
    //
    //   +0x3A0  std::map<unsigned int, unsigned char>  -- the PERSISTENT table
    //           (0x14 bytes: +0x3A0 allocator, +0x3A4 _Head, +0x3B0 _Size; the
    //           mapped byte sits at node+0x10, key at node+0x0c)
    //   +0x3B8  std::vector<pair<unsigned int, unsigned char>> -- the PENDING
    //           queue, element stride 8 (+0x3B8 _First, +0x3BC _Last)
    //
    // FUN_06d4b450 (ported below as PlayerData_FlushPendingVisits) is the flush:
    // for every pending entry it ORs `entry.flags | 8` into the map's existing
    // byte for that key, inserting the key when absent, and then empties the
    // vector with `[+0x3BC] = [+0x3B8]`. Bit 3 (0x08) is set unconditionally by
    // the flush itself -- retail's `| 8` at 0x06d4b48a-ish -- so it marks
    // "committed", not anything the caller supplied.
    //
    // FUN_06d4b520 is the reader: it walks the MAP (not the vector), packs
    // 5 bytes per entry (dword key + byte flags) behind a dword count into one
    // operator_new block and hands it to IClient vtable +0x170.
    struct FVisitedState {
        unsigned int system_id;
        unsigned char visit_flags;
    };
    std::map<unsigned int, unsigned char> visit_map;  // +0x3A0: committed visit flags
    std::vector<FVisitedState> visited_states;        // +0x3B8: pending, flushed by FUN_06d4b450

    // +0x3C8: the session's SubObjectID::EquipIdMaker -- the per-player counter
    // that hands out sub-object ids for the equipment mounted on this player's
    // ship. Retail's slot teardown FUN_06d49230 calls
    // `SubObjectID::EquipIdMaker::Reset(slot + 0x3C8)` on logout (imported from
    // Common.dll through .rdata:06d648dc,
    // `?Reset@EquipIdMaker@SubObjectID@@QAEXXZ`, ordinal 1937 in Server.dll's
    // import table / 1938 in Common.dll's export table).
    //
    // The class is a bare 16-bit counter and nothing more. Common.dll.txt:
    //   ctor          0xd4580  MOV EAX,this / MOV word ptr [EAX],0x21 / RET
    //   CreateEquipID 0xd4560  INC word ptr [this] / MOV AX,[this] / RET
    //   Reset         0xd4590  MOV word ptr [this],0x21 / RET
    // 0x21 is the low end of SubObjectID::IsEquipID's accepted range.
    //
    // The real class since 2026-09-24 (claude:srv-pdb): SubObjectID's id makers
    // moved from common_cobject.h to common_archetype.h, which this header
    // already reaches through common_goods.h.
    SubObjectID::EquipIdMaker equip_id_maker; // +0x3C8

    // Trade state (matches retail player data offsets)
    // +0x3CC: the live trade offer, a real Common.dll CPlayerTradeOffer (common_tradeoffer.h).
    // IServerImpl::InitiateTrade (.text:06cf185d .. 06cf190e) allocates two of them with
    // operator new(0x24) + the imported ctor and stores one here per side;
    // TerminateTrade deletes them through vtable[0](1) (.text:06cf1a0b / 06cf1a23).
    CPlayerTradeOffer* trade_offer;
    unsigned int trade_request_partner; // +0x3D0: pending OUTBOUND trade request, partner session id
    // Retail +0x3D4: the std::list of session ids that have an outstanding
    // trade request pointed AT this player (head node at +0x3D8). The slot
    // teardown FUN_06d49230 walks it -- `local_4 = *(int**)param_1[0xf6]` with
    // param_1[0xf6] == +0x3D8 -- clearing each requester's own +0x3D0 and
    // notifying it through IClient slot 108, then erases the list with
    // FUN_06cf7720(param_1 + 0xf5, ...). The port had no such member, so half
    // of a cancelled trade negotiation was left dangling on logout.
    std::vector<unsigned int> inbound_trade_requests; // +0x3D4
    unsigned int ship_object_id;       // +0x3E4 (-0x34): current ship FLID, 0 when docked

    // +0x3F0. NAMED 2026-09-17 (wave 17); it used to be `Field_0x3F0`. It is the
    // base the save file records as `last_base`, and it is a DIFFERENT field from
    // current_base (+0x3EC). Two sites in the save serializer prove it:
    //   .text:06d4d477  f783ec030000  TEST dword ptr [EBX + 0x3ec],0x3fffffff
    //   .text:06d4d483  8b8bf0030000  MOV  ECX,dword ptr [EBX + 0x3f0]
    //   .text:06d4d489  898bec030000  MOV  dword ptr [EBX + 0x3ec],ECX
    // -- when the ship object id at +0x3E4 is zero AND current_base carries no
    // object bits, current_base is REFILLED from +0x3F0 before the `base` key is
    // written; and then, separately,
    //   .text:06d4dacd  8b83f0030000  MOV  EAX,dword ptr [EBX + 0x3f0]
    //   .text:06d4dad4  CALL [->COMMON.DLL::Universe::get_base]
    //   .text:06d4db79  8b4c240c      MOV  ECX,dword ptr [ESP + 0xc]   ; that base
    //   .text:06d4db7f  ff5204        CALL dword ptr [EDX + 0x4]       ; its nickname
    //   .text:06d4db87  PUSH .rdata:s_last_base_06d6b1cc = "last_base"
    // resolves `last_base` from +0x3F0's own base object, not from the `base`
    // written earlier out of +0x3EC.
    // BaseEnter also stores the entered base here (.text:06d1774b
    // `MOV [EDI-0x28],EBX`), so `last_base` is the last base the session
    // docked at. +0x3F4 is `entered_base_id`, declared with the prefix above.
    unsigned int last_base_id;         // +0x3F0: the base `last_base` names

    // Account pointer (retail offset -0x18)
    void* account_ptr;                 // -0x18 (+0x400): CAccount* for this player

    // The account name retail keeps at PlayerData+0x00. FUN_06d48e80 -- the
    // internal-login helper PlayerDB::load_user_data tail-calls at 0x06d536bc
    // with `LEA ECX,[EAX + ECX*0x1 + 0xfffffbe8]` (= base + (sid-1)*0x418) --
    // stores the session id with `MOV [EBP+0x348],ECX` at 0x06d48f73 and then
    // does `PUSH EBX / PUSH ECX / PUSH EBP / CALL wcscpy` at 0x06d48f88, i.e.
    // `wcscpy(slot, loginName)` straight into the slot base. The port used to
    // emulate that with `wcsncpy((wchar_t*)PlayerSlot_ptr, name, 96)` over a
    // port-native struct whose first member is a 4-byte int -- 192 bytes of
    // heap corruption starting at session_id. It is a real field now.
    //
    // CORRECTED 2026-09-17 (wave 5): the buffer is 0x50 bytes, i.e. 40 wide
    // characters, NOT 50. The earlier note put the ceiling at the MP enable
    // byte (+0x64), but +0x50 is already taken: FUN_06d48d50 (0x06d48d50) --
    // the per-save-file loader FUN_06d48e80 calls for every `.fl` in the
    // account directory -- does `this_00 = (int)this + 0x50` and then
    // `FUN_06d57090(this_00, &charId)` / `*(int**)((int)this + 0x54)`, i.e.
    // +0x50 is an associative container keyed by CHARACTER_ID and +0x54 is its
    // end sentinel. The slot reset FUN_06d49230 agrees from the other side: it
    // fills `param_1[0 .. 0x13]` with 0xcdcdcdcd -- exactly 0x14 dwords = the
    // 0x50 bytes of this buffer -- and only then calls FUN_06d57c70 on
    // `param_1 + 0x14` (= +0x50) to clear that container.
    wchar_t AccountName[40];           // +0x00: login account name (0x50 bytes)

    // ------------------------------------------------------------------
    // The saved characters found in this player's account directory.
    //
    // Retail keeps them in the associative container at PlayerData+0x50,
    // keyed by CHARACTER_ID (the `.fl` file name, lowercased): FUN_06d48d50
    // inserts the key, hands the fresh value record to the save-file parser
    // FUN_06d47120, and erases the entry again when the parse fails (with the
    // FDUMP "*** WARNING: Failed to load saved game %s" from
    // E:\FL\Scratch\Source\Server\PlayerDB.cpp line 1192).
    //
    // The important part is what it is NOT: login does not apply any of this
    // to the player's live state. FUN_06d48e80 runs the whole `.fl` scan and
    // THEN zeroes +0x3E0/+0x3E4/+0x3E8/+0x3EC/+0x3F0/+0x3F4/+0x3F8/+0x3FC, so
    // nothing the scan parsed can survive in the live fields by construction.
    // The port used to write system_id/base_id/position/cash/ship_archetype
    // straight onto the slot during that scan, which is why the clear block had
    // to be hoisted to the top of load_user_data to stop it wiping them.
    //
    // WIDENED 2026-09-24 (claude:srv-save) to the WHOLE retail record. The
    // value type is what FUN_06d47120 fills and FUN_06d4a5d0 copies onto the
    // live slot; its default state is retail's record constructor FUN_06d46ab0
    // (.text:06d46ab0 .. .text:06d46d16). Retail's node is 0x5a8 bytes
    // (FUN_06d59ef0 PUSHes 0x5a8): 0xc of tree links, the 0x200-byte
    // CHARACTER_ID key, then this 0x398-byte record (FUN_06d57090 returns
    // node + 0x20c, and its two FUN_06d46d20 temporaries are 0x398 apart on
    // its frame). Offsets below are record offsets; the port uses modern std
    // members, so only the order and the values are retail's, not the bytes.
    // Fields the constructor does NOT touch are marked "(ctor: untouched)" --
    // retail leaves them to the per-[Player] reset at .text:06d47298 .. 06d4736b.
    // ------------------------------------------------------------------
    struct FSavedCharacter {
        char FileName[0x200];                        // +0x000 ctor "" ; FUN_06d48d50 safe_strcpy's the file name here
        std::wstring Name;                           // +0x200 `name`, HexToWide'd
        std::wstring Description;                    // +0x210 `description`, HexToWide'd
        unsigned int DescripStrId;                   // +0x220 `descrip_strid`
        unsigned int TimestampLow;                   // +0x224 `tstamp` value 1
        unsigned int TimestampHigh;                  // +0x228 `tstamp` value 0
        unsigned int ShipArchetype;                  // +0x22c `ship_archetype`
        int Money;                                   // +0x230 `money` (ftol of a float)
        int NumKills;                                // +0x234
        int NumMissionSuccesses;                     // +0x238
        int NumMissionFailures;                      // +0x23c
        float HullStatus;                            // +0x240 ctor 1.0f
        // +0x244 / +0x260 are COMMON.DLL EquipDescLists, not bare std::lists: the
        // record constructor FUN_06d46ab0 builds them inline (.text:06d46b1c /
        // .text:06d46b68, the list _Buynode the inline ctor expands to), and its EH
        // map destroys them with the IMPORTED EquipDescList::~EquipDescList
        // (Unwind@06d62f7e ECX = this+0x244, Unwind@06d62f9c ECX = this+0x260, both
        // JMP [.rdata:06d643f0]); so do the other record temporaries' funclets
        // (Unwind@06d63b2e / @06d63b4c, ...). Users go through EquipDescList::Items.
        EquipDescList Equipment;                     // +0x244 `equip` + `cargo`
        std::list<CollisionGroupDesc> CollisionGroups;     // +0x250
        float BaseHullStatus;                        // +0x25c ctor 1.0f
        EquipDescList BaseEquipment;                 // +0x260 `base_equip` + `base_cargo`
        std::list<CollisionGroupDesc> BaseCollisionGroups; // +0x26c
        unsigned int BaseId;                         // +0x278
        unsigned int LastBaseId;                     // +0x27c
        unsigned int LocationId;                     // +0x280
        unsigned int SystemId;                       // +0x284
        float Position[3];                           // +0x288 (ctor: untouched)
        float Orientation[3][3];                     // +0x294 (ctor: untouched)
        unsigned int StartingRing;                   // +0x2b8 an ID_String; ctor ID_String::clear
        unsigned int Rank;                           // +0x2bc
        std::vector< ::Reputation::Relation> Relations; // +0x2c0 `house` / `group` / `initial_rep`
        unsigned int RepGroup;                       // +0x2d0 ctor 0xffffffff
        Costume ComCostume;                          // +0x2d4 the `com_*` keys
        TString<32> Voice;                           // +0x308
        Costume BaseCostume;                         // +0x32c the bare costume keys
        SubObjectID::EquipIdMaker EquipIdMaker;      // +0x360 ctor 0x21 (Common.dll 0xd4580)
        std::string WeaponGroups;                    // +0x364 `wg` lines, verbatim
        std::list<FmtStr> MissionLog;                // +0x374 `log`
        unsigned int Interface;                      // +0x380 (ctor: untouched)
        std::map<unsigned int, unsigned char> Visits; // +0x384 `visit`

        // FUN_06d46ab0. What retail leaves uninitialised (Position,
        // Orientation, Interface and the accessory slots past the count) is
        // zeroed here: there is no retail value to reproduce for them.
        FSavedCharacter()
            : Name(), Description(), DescripStrId(0), TimestampLow(0), TimestampHigh(0),
              ShipArchetype(0), Money(0), NumKills(0), NumMissionSuccesses(0),
              NumMissionFailures(0), HullStatus(1.0f), Equipment(), CollisionGroups(),
              BaseHullStatus(1.0f), BaseEquipment(), BaseCollisionGroups(), BaseId(0),
              LastBaseId(0), LocationId(0), SystemId(0), StartingRing(0), Rank(0),
              Relations(), RepGroup(0xffffffffu), EquipIdMaker(), WeaponGroups(),
              MissionLog(), Interface(0), Visits() {
            FileName[0] = '\0';
            Position[0] = 0.0f;
            Position[1] = 0.0f;
            Position[2] = 0.0f;
            for (int Row = 0; Row < 3; ++Row)
                for (int Col = 0; Col < 3; ++Col)
                    Orientation[Row][Col] = 0.0f;
            ComCostume = Costume();
            BaseCostume = Costume();
            Voice.Length = 0;
            Voice.Data[0] = '\0';
        }
    };
    // Keyed by CHARACTER_ID, i.e. ordered by Common.dll's operator< (stricmp,
    // Common.dll 0xce740) -- which is why retail never lowercases the key.
    std::map<CHARACTER_ID, FSavedCharacter> saved_characters; // +0x50

    // Three dwords retail's internal login clears alongside the ship/base block
    // (the +0x3E0..+0x3FC run and the separate +0x410). +0x3E8 and +0x410 are
    // identified below. +0x3FC (-0x1c) is what pub::Player::GetCharacter reads
    // (retail 0x74c70); every write in Server.dll stores zero (.text:06d49156,
    // .text:06d49498, .text:06d4af7a), so its meaning is still unrecovered.
    // TODO: identify and name +0x3FC.
    // +0x3E8 (-0x30 from the slot end). NAMED 2026-09-24
    // (claude-code:w3-baseenter): the ship id RequestCreateShip reserved for the
    // session. Its only non-zero writer is IServerImpl::RequestCreateShip
    // (.text:06cefa77 `MOV [slot-0x30],EDX`, the masked CFLIDMaker id; the same
    // function answers with the existing one when .text:06cefa1e finds it set),
    // and IServerImpl::PlayerLaunch requires `& 0x3fffffff != 0` and
    // `param_1 == it` (.text:06cefb98 / .text:06cefba9). Zeroed by the login
    // clear (.text:06d49138), the slot teardown (.text:06d4947a), the initializer
    // (.text:06d496c5) and CharacterSelect (.text:06d4af86).
    unsigned int created_ship_id;      // +0x3E8: ship id reserved by RequestCreateShip
    unsigned int Field_0x3FC;          // +0x3FC

    // +0x410 (-0x8 from the slot end). NAMED 2026-09-24
    // (claude-code:w3-baseenter): the base the session last LEFT. BaseExit
    // copies current_base here just before clearing it
    // (.text:06d17942 `MOV EDX,[slot-0x2c]` / .text:06d17948
    // `MOV [ESI+0x410],EDX`), and BaseEnter reads it back (.text:06d1768d
    // `MOV EAX,[EDI-0x8]`): when it is non-zero and differs from the base being
    // entered, the session is treated as having changed system (events 0x3a /
    // 0x3b). PlayerLaunch also puts it in event 6's payload (.text:06cefbee).
    // The internal-login clear (.text:06d4920b), the slot teardown
    // (.text:06d4949e) and CharacterSelect (.text:06d4af8c) reset it.
    unsigned int exited_base_id;       // +0x410: base left by the last BaseExit
};

// Compatibility alias. `PlayerSlot` was the port's own name for this record
// while the two tables existed; ~230 call sites still spell it that way. The
// CLASS is PlayerData, which is what retail's exported mangled names carry
// (`??APlayerDB@@QAEAAUPlayerData@@ABI@Z` -- `U` for struct), so the alias must
// stay an alias and never become the real type name.
typedef PlayerData PlayerSlot;

// (2026-09-24, claude-code:w5-server-06d4) The offsetof asserts that pinned the
// port-native POD prefix (session_id at 0, current_location at 8, ...) are gone
// with session_id / active / the invented float block: that prefix order had no
// retail meaning, and PlayerData no longer crosses a DLL boundary (the one table
// lives in Server.dll's `Players`). The layout is NOT retail's -- see the header
// comment -- and the retail offset is recorded on each member instead.
static_assert(sizeof(((PlayerData*)nullptr)->cash) == 4, "PlayerData::cash must stay a 4-byte int");

// Common.dll exports for Server.dll (C++ linkage to match COMMON_API)
#ifdef COMMON_EXPORTS
#define PLAYERDB_API __declspec(dllexport)
#else
#define PLAYERDB_API __declspec(dllimport)
#endif

// The lookup helpers are NO LONGER Common.dll exports. They were the interface
// to `g_PlayersDB[]`, the second of the port's two player tables; that table is
// gone and these are now plain Server.dll-internal forwarders onto the single
// `Players` object (defined in src/Server/server_classes.cpp). Retail has no
// such functions at all -- every call site inlines `Players.operator[]` or the
// `[.data:Players]` walk -- so they carry no retail names and no export.
//
// PlayerDB_init / _free / _login / _logout / _logout_all are gone with the
// table: call Players.init / .free / .login / .logout / .logout_all directly,
// which is what IServerImpl now does.
PlayerSlot* __cdecl PlayerDB_GetPlayer(unsigned int session_id);

// ---------------------------------------------------------------------------
// The PlayerData equipment-list family. These are real retail member functions
// of PlayerData (thiscall, `this` == the slot base); the port spells them as
// free functions because PlayerData is a plain struct here. None of them is an
// export -- they are Server.dll-internal, defined in
// src/Server/server_classes.cpp and used from src/Server/server_pub.cpp and
// src/Server/IServerImpl.cpp.
//
// All four share the same MP gate at their top (retail
// `CALL COMMON.DLL::IsMPServer / TEST AL,AL / JZ work` followed by
// `MOV DL,[this+0x64] / TEST DL,DL / SETNZ AL / TEST AL,AL / JZ bail`), i.e.
// "run unless this is an MP server and no character is selected yet".
// ---------------------------------------------------------------------------

// Retail FUN_06d4efc0 (.text:06d4efc0, RET 0x1c -> 7 stack args).
// Appends one EquipDesc to the slot's list and returns the sub-object id it
// was given (SUBOBJ_ID_NONE when the MP gate refuses).
//   bNoCombine -- retail's incoming +0x18 byte. When it is CLEAR *and*
//     arch_is_combinable(ArchId), retail looks for an existing stack with
//     find_matching_cargo(ArchId, 0, 1.0f) and reuses its Id; otherwise it
//     mints a fresh id from the slot's EquipIdMaker at +0x3c8. Retail then
//     OVERWRITES that same stack slot with the combinable flag it passes on to
//     add_equipment_item (1 on the combining path, 0 otherwise) -- see
//     .text:06d4f016 (`MOV byte ptr [ESP+0x54],1`) and .text:06d4f02b
//     (`MOV byte ptr [ESP+0x48],0`).
//   bNotify -- retail's +0x1c byte: when set, IClient slot 26 (+0x68) is fired
//     with (client_id, add_equipment_item's return, &Desc).
unsigned short __cdecl PlayerData_AddEquipment(PlayerSlot& Slot, unsigned int ArchId,
                                               CacheString const& Hardpoint, int Count,
                                               float Health, unsigned char bEquipped,
                                               bool bNoCombine, bool bNotify);

// Retail FUN_06d4f100 (.text:06d4f100, RET 0xc).
// remove_equipment_item(Id, Count) on the slot's list; IClient slot 27 (+0x6c)
// with (client_id, result, Id, Count) when bNotify.
void __cdecl PlayerData_RemoveEquipment(PlayerSlot& Slot, unsigned short Id, int Count,
                                        bool bNotify);

// Retail FUN_06d4f260 (.text:06d4f260, RET 0x10), ported in
// src/Server/server_classes.cpp. find_matching_cargo(ArchId, Count, Health),
// FDUMP on a miss, else PlayerData_RemoveEquipment(hit->Id, Count, bNotify).
void __cdecl PlayerData_RemoveCargo(PlayerSlot& Slot, unsigned int ArchId, int Count,
                                    float Health, bool bNotify);

// Retail FUN_06d4f170 (.text:06d4f170, RET 0x18).
// find_equipment_item(Id) then set_hardpoint/set_count/set_status/set_equipped.
// When the id is absent retail FDUMPs
// "*** ERROR: PlayerData::m..." from PlayerDB.cpp line 3543 (0xdd7).
void __cdecl PlayerData_ModifyEquipment(PlayerSlot& Slot, unsigned short Id,
                                        CacheString const& Hardpoint, int Count,
                                        float Health, unsigned char bEquipped);

// Retail FUN_06d4fc30 (.text:06d4fc30). No MP gate on this one.
void __cdecl PlayerData_SnapshotEquipment(PlayerSlot& Slot);

// Retail FUN_06d4f560, FUN_06d4f460, FUN_06d4f480 and FUN_06d4f4e0, ported in
// src/Server/server_classes.cpp: +0x320 = total equipment value; cash read with
// the [0, 999999999] clamp; cash set / adjusted with the MP selected-character
// gate, the clamp, IClient SETCASH (slot 77) and controller message 0.
void __cdecl PlayerData_ResetTotalEarned(PlayerSlot& Slot);
int __cdecl PlayerData_GetCash(PlayerSlot const& Slot);
void __cdecl PlayerData_SetCash(PlayerSlot& Slot, int Cash);
void __cdecl PlayerData_AdjustCash(PlayerSlot& Slot, int Amount, bool bCountEarned);

// Retail FUN_06d498f0 (.text:06d498f0, thiscall on the PlayerData slot, RET 0xc):
// the MP position bound SPObjUpdate applies (.text:06cf0698). Retail takes three
// stack arguments and reads only the first (the position); the other two are
// never touched, so the port takes the one that matters. Returns false after
// destroying the ship when the position is out of bounds. Defined in
// src/Server/server_classes.cpp.
class Vector;
bool __cdecl PlayerData_CheckPositionBound(PlayerSlot& Slot, Vector const* Position);

// Retail FUN_06d4a200 (.text:06d4a200, thiscall on the PlayerData slot, RET 0),
// PlayerData::reload_ship_data by its own FDUMP text. Puts the ship back to the
// selected character's last-base loadout. Called by the save serializer
// (.text:06d4cde2) for a dead MP ship. Defined in src/Server/server_classes.cpp.
void __cdecl PlayerData_ReloadShipData(PlayerSlot& Slot);

// Retail FUN_06d4f430 (.text:06d4f430, `RET 0x8` -- fourteen instructions).
// ADDED 2026-09-17 (wave 14); StarSystem_DamageObject is its only caller.
//
//   .text:06d4f430  8b8970020000  MOV  ECX,[this + 0x270]   ; the list's _Head
//   ... walk the circular list, `CMP (short)node[2],arg0` ...
//   .text:....      node[3] = arg1
//
// +0x270 is the _Head of the collision-group list at +0x26C, whose element
// CollisionGroupDesc is {unsigned short Id; float Health;} (common_archetype.h,
// sizeof 8) -- so node+0x08 is Id and node+0x0C is Health, which is exactly the
// pair the walk compares and writes. The second argument arrives as an int in
// the decompilation because it is a float pushed through `FSTP [ESP]` at the
// call site (.text:06d0ba23), the same tell PlayerData_SetRelativeHealth has.
//
// Silently does nothing when the id is not in the list -- no FDUMP, no insert.
void __cdecl PlayerData_SetCollisionGroupHealth(PlayerSlot& Slot,
                                                unsigned short GroupId,
                                                float Health);

// Retail FUN_06d4ed10 (.text:06d4ed10, RET 0x8). Stores the relative hull
// health at +0x268 and, when bNotify, fires IClient slot 22 (+0x58) with
// (client_id, the same dword). The first argument really is a float -- see the
// FSTP-into-the-argument-slot at .text:06d0ba77 quoted at the implementation.
// No MP gate.
void __cdecl PlayerData_SetRelativeHealth(PlayerSlot& Slot, float RelativeHealth,
                                          bool bNotify);

// Retail FUN_06d50af0 (.text:06d50af0, RET 0). When the session has a live
// trade offer at +0x3CC, calls IServerImpl::TerminateTrade(client_id, 0) on the
// `Server` singleton.
void __cdecl PlayerData_TerminateTrade(PlayerSlot& Slot);

// Retail FUN_06d50a40 (.text:06d50a40, RET 0x4 -- ONE stack argument, which the
// decompilation hides as `unaff_BL`). Cancels this session's OUTBOUND trade
// request: notifies the partner through IClient slot 108 (+0x1b0), optionally
// removes this session from the partner's inbound-request list, and clears
// +0x3D0.
void __cdecl PlayerData_CancelTradeRequest(PlayerSlot& Slot, bool bDropPartnerEntry);

// Retail FUN_06d50920 (.text:06d50920, RET 0x4). Cancels any outbound request
// (FUN_06d50a40's body with the bool set, inlined), then -- only when the target
// slot's client id is non-zero -- adds this session to the target's inbound list,
// sets +0x3D0 = TargetId and notifies the target through IClient slot 107 (+0x1ac).
void __cdecl PlayerData_SetTradeRequest(PlayerSlot& Slot, unsigned int TargetId);

// Retail FUN_06d4f320 (.text:06d4f320, RET 0x4). Replaces the list at +0x26C
// wholesale and reports it with IClient slot 23 (+0x5c). No gate of its own.
void __cdecl PlayerData_SetCollisionGroups(PlayerSlot& Slot,
                                           std::list<CollisionGroupDesc> const& Groups);

// Retail FUN_06d4ec10 (.text:06d4ec10) -- validates the archetype, and when it
// differs from the current one rebuilds the std::list<CollisionGroupDesc> at
// +0x26c. CORRECTED: that list is NOT "per-hardpoint". The refill call at
// .text:06d4ec91 goes through Server.dll IAT .rdata:06d644a0 =
// Archetype::EqObj::get_undamaged_collision_group_list, so +0x26c holds the
// hull's undamaged collision groups. Still unported -- see the TODO at the
// implementation in server_classes.cpp.
void __cdecl PlayerData_SetShipArch(PlayerSlot& Slot, unsigned int ShipArch);

// Retail FUN_06d4eeb0 (.text:06d4eeb0) -- replaces the slot's whole equipment
// list with NewList, minting a fresh EquipIdMaker id for every element that
// arrives carrying SUBOBJ_ID_NONE.
void __cdecl PlayerData_LoadEquipment(PlayerSlot& Slot, EquipDescList const& NewList);

// ---------------------------------------------------------------------------
// The save-file family. All three live in Server.dll and are declared here
// because they are PlayerData members in retail; none of them is exported.
// ---------------------------------------------------------------------------

// Retail FUN_06d4ccd0's PROLOGUE (.text:06d4cd01 .. .text:06d4cdc4), ported in
// src/Server/server_classes.cpp. Returns true when a cheat was detected, which
// is the condition under which retail's save refuses to run.
bool __cdecl PlayerData_CheckForCheats(PlayerSlot& Slot);

// Retail FUN_06d4b450 (.text:06d4b450), ported in src/Server/server_classes.cpp.
// Drains the pending-visit queue at +0x3B8 into the committed map at +0x3A0,
// ORing 8 into every entry's flags. FUN_06d4ccd0 inlines the same drain at
// .text:06d4cde7 before it serializes the map, so the serializer calls this.
void __cdecl PlayerData_FlushPendingVisits(PlayerSlot& Slot);

// Retail FUN_06d4b200 (thiscall, RET 8), ported in src/Server/server_classes.cpp.
// Folds the committed byte for Key (map at +0x3A0) into Flags and answers false
// only when both 0x10 and 0x08 are then set.
bool __cdecl PlayerData_ShouldQueueVisit(PlayerSlot const& Slot, unsigned int Key,
                                         unsigned char Flags);

// Retail FUN_06d4b270 (thiscall, RET 8), ported in src/Server/server_classes.cpp.
// Appends {Key, Flags} to the pending-visit vector at +0x3B8.
void __cdecl PlayerData_QueueVisit(PlayerSlot& Slot, unsigned int Key, unsigned char Flags);

// Retail FUN_06d4ccd0 (.text:06d4ccd0 .. .text:06d4ea4f), ported in
// src/Server/server_pub.cpp. `FileName` is retail's param_1: a BASE FILE NAME,
// not a path -- the function builds the destination itself out of
// GetUserDataPath, "Accts" / "Accts\MultiPlayer" and the account directory
// name, and appends the ".fl" extension in single player. Returns false when
// the cheat gate fires or the file cannot be opened.
bool __cdecl PlayerData_SerializeToFile(PlayerSlot& Slot, char const* FileName,
                                        wchar_t const* Description,
                                        unsigned int DescripStrId);
unsigned int __cdecl PlayerDB_GetMaxPlayers();
bool __cdecl PlayerDB_AllowsNewPlayers();

// (PlayerDB_GetSlotSize, the port's Common.dll-exported PlayerData size tripwire that
// IServerImpl::Startup used to check, is gone: retail Common.dll has no such export and
// retail Startup goes straight from CreateID("Freelancer") to Players.init (0x06d44bd9).
// Common.dll no longer takes a PlayerData across the DLL boundary at all.)
