// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_iclient_args.h - argument structs Server.dll hands to IClient slots.
//
// IClient's slots take every argument as a 4-byte placeholder (rpc_iclient.h); the
// structs below are what the pointer-typed ones point at. Server.dll builds them on
// its stack and rpclocal.dll's senders read them field by field, so each is declared
// once, here.
#pragma once

#include "common_reputation.h"   // Reputation::Relation
#include "common_text.h"         // FmtStr
#include "common_ini.h"          // Vector (FPayloadServerSetDirective)
#include "common_physys.h"       // Quaternion (FPayloadServerSystemSwitchIn)

// ---------------------------------------------------------------------------
// IClient slot 66 (+0x108), ch2 op 0x29 FLPACKET_SERVER_SETREPUTATION.
//
// Server.dll's IServerImpl::CharacterSelect builds it at S+0x44 of its frame
// (.text:06d45487 .. .text:06d45518): the two FmtStr default constructors
// (FUN_06d55bc0) at +0x18 / +0x30, +0x10 = 0, byte +0x14 = 0, word +0x48 = 0,
// +0x00 = the slot's rep vibe (.text:06d454af), +0x10 = operator new(
// Reputation::group_count() * 8) (.text:06d454d2), then
// Reputation::Vibe::Get(+0x00, +0x08, +0x0c, +0x14, +0x10, +0x18, +0x30, &name)
// (.text:06d454ee) and wcscpy(+0x48, name) (.text:06d45512). Common's
// ?Get@Vibe@Reputation@@YAHABHAAI1AAEQAURelation@2@AAUFmtStr@@4AAPBG@Z names the
// Relation element type of +0x10.
//
// rpclocal's sender (0x06d88d70) serializes +0x00, +0x04 and +0x08; +0x0c is
// filled by Get and read by nothing. CharacterSelect never writes +0x04 (retail
// ships whatever its stack held there).
// ---------------------------------------------------------------------------
struct FServerSetReputationInfo {
    int VibeHandle;                         // +0x00
    float Attitude;                         // +0x04  Reputation::Vibe::AttitudeTowards out-param
    unsigned int Field_0x08;                // +0x08  Reputation::Vibe::Get out-param 2
    unsigned int Field_0x0c;                // +0x0c  Get out-param 3 -- never reaches the wire
    ::Reputation::Relation* Relations;      // +0x10  Get out-param 5
    unsigned char RelationCount;            // +0x14  Get out-param 4 (a byte on both sides)
    unsigned char Pad_0x15[3];              // +0x15
    FmtStr NameA;                           // +0x18  Get out-param 6
    FmtStr NameB;                           // +0x30  Get out-param 7
    wchar_t DisplayName[24];                // +0x48  Get out-param 8, wcscpy'd in by the caller
};
static_assert(sizeof(FServerSetReputationInfo) == 0x78,
              "SETREPUTATION argument struct 0x78");

// ---------------------------------------------------------------------------
// IClient slot 14 (+0x38), ch2 op 0x08 FLPACKET_SERVER_CHARSELECTVERIFIED.
//
// IServerImpl::CharacterSelect builds it at S'+0x38 (.text:06d455ab ..
// .text:06d455c8): +0x00 = the SETREPUTATION struct's vibe handle, +0x08 = the
// server clock (FUN_06d447b0: DAT_06d8fb28 + DAT_06d8fb20). +0x04 is never
// written. rpclocal's sender reads dwords 0, 2 and 3 (rpc_payload_ch2.cpp,
// MarshalCharSelectVerified).
// ---------------------------------------------------------------------------
struct FServerCharSelectVerifiedInfo {
    int VibeHandle;                         // +0x00
    unsigned int Unused_0x04;               // +0x04  not written by the sender's caller
    double ServerTime;                      // +0x08
};
static_assert(sizeof(FServerCharSelectVerifiedInfo) == 0x10,
              "CHARSELECTVERIFIED argument struct 0x10");

// ---------------------------------------------------------------------------
// IClient slot 56 (+0x0e0), ch2 op 0x0a FLPACKET_SERVER_ACTIVATEOBJECT.
//
// The 0x0c-byte argument IS the wire payload: rpclocal's sender (0x06d89770)
// copies the three fields straight into packet+0x20 and its receiver (caseD_a,
// 0x06d823ab) rebuilds the same three into its statics. Moved here from
// src/rpclocal/rpc_payloads.h (2026-09-24, claude:srv-sys) because Server.dll
// builds it too -- StarSystem::add_player's multiplayer solar walk, once per
// set entry of CObject::get_activate_state (vtable +0xb8):
//   .text:06d081a1  MOV [ESP + 0x7c],EDX        ; +0x00 = CObject::Id (+0xb0)
//   .text:06d081b1  MOV byte [ESP + 0x84],0x1   ; +0x04 = 1   (one PUSH deep)
//   .text:06d081b9  MOV [ESP + 0x88],ESI        ; +0x08 = the entry's index
//   .text:06d081aa  MOV EDX,[ESP + 0x1f4]       ; the session id, BY VALUE
//   .text:06d081c3  CALL [EAX + 0xe0]           ; SendServerActivateObject(session, &rec)
// so +0x04 is "activated" and +0x08 is the index into the object's activation
// state vector.
// ---------------------------------------------------------------------------
struct FPayloadServerActivateObject {
    unsigned int ObjectId;      // +0x00
    unsigned char bActivate;    // +0x04
    unsigned char Pad_0x05[3];
    unsigned int Field_0x08;    // +0x08  index into CObject::get_activate_state's vector
};
static_assert(sizeof(FPayloadServerActivateObject) == 0x0c, "ACTIVATEOBJECT payload 0x0c");

// ---------------------------------------------------------------------------
// IClient slot 58 (+0x0e8), ch2 op 0x22 FLPACKET_SERVER_SYSTEM_SWITCH_IN. Registry
// size 0x44, payload 0x24. Sender 0x06d8a8b0: two dwords straight through, then the
// three-dword copier FUN_06d8f510 into packet+0x28 and the four-dword copier
// FUN_06d8f4f0 into packet+0x34, in the SAME order as this struct. Receiver
// caseD_22 at 0x06d822c6 spills all nine dwords into the file-static block at
// .data:0x06dbb91c and passes ITS address.
//
// Its only builder is Server.dll FUN_06d0cc00 (StarSystem::arrive_ship):
//   .text:06d0ccb2 .. 06d0ccbf  +0x00 = Obj->get_id()      (IObjInspect vt +0x20)
//   .text:06d0ccd0              +0x04 = the jump object's get_type_bits() (vt +0x154),
//                                       0 when there is none
//   .text:06d0ccb9 .. 06d0ccd8  +0x08 = *Pos (three dwords)
//   .text:06d0cce8 .. 06d0cd17  +0x14 = MatrixToQuaternion(*Orient), I3DMathEngine
//                                       vt +0x50, the four floats as written
// The field names stay offset names because rpclocal's codec reads them so.
// ---------------------------------------------------------------------------
struct FPayloadServerSystemSwitchIn {
    unsigned int Field_0x00;     // +0x00  object id              -> .data:0x06dbb91c
    unsigned int Field_0x04;     // +0x04  jump object type bits  -> .data:0x06dbb920
    Vector Position;             // +0x08  -> 0x06dbb924/928/92c
    Quaternion Orientation;      // +0x14  -> 0x06dbb930/934/938/93c
};
static_assert(sizeof(FPayloadServerSystemSwitchIn) == 0x24, "SYSTEM_SWITCH_IN payload 0x24");

// ---------------------------------------------------------------------------
// IClient slot 64 (+0x100), ch2 op 0x38 FLPACKET_SERVER_ITEMTRACTORED. Registry
// size 0x2c, payload 0x0c. Sender 0x06d88c60 (dword, dword, word, word straight
// through), receiver caseD_38 at 0x06d81e3d (a straight copy).
// Server.dll's only builder is StarSystem::tractor_loot (FUN_06d0ed40,
// .text:06d0ee85 .. 06d0eecd) at S+0x24 of its frame:
//   +0x00  the tractored loot's get_id()           (.text:06d0eeaa / 06d0eead)
//   +0x04  the tractoring ship's get_id()          (.text:06d0eeb5 / 06d0eec1)
//   +0x08  the tractor's equipment id (word)       (.text:06d0eebc / 06d0eec8)
//   +0x0a  the units taken, low word               (.text:06d0eecd)
// The field names stay offset names because rpclocal's codec reads them so.
// ---------------------------------------------------------------------------
struct FPayloadServerItemTractored {
    unsigned int Field_0x00;     // +0x00  loot object id
    unsigned int Field_0x04;     // +0x04  tractoring ship id
    unsigned short Field_0x08;   // +0x08  tractor equipment id
    unsigned short Field_0x0a;   // +0x0a  units taken
};
static_assert(sizeof(FPayloadServerItemTractored) == 0x0c, "ITEMTRACTORED payload 0x0c");

// ---------------------------------------------------------------------------
// IClient slot 65 (+0x104), ch2 op 0x51 FLPACKET_SERVER_USE_ITEM. Registry size
// 0x28, payload 0x08. Sender 0x06d88cc0, receiver caseD_51 at 0x06d81e77 (a
// straight copy). Server.dll's only builder is StarSystem::use_item
// (FUN_06d0efe0, .text:06d0f05c .. 06d0f073) at S+0x14 of its frame:
//   +0x00  the object's get_id()                   (.text:06d0f060 / 06d0f067)
//   +0x04  the equipment id (word)                 (.text:06d0f06e)
//   +0x06  the count (low word)                    (.text:06d0f073)
// The field names stay offset names because rpclocal's codec reads them so.
// ---------------------------------------------------------------------------
struct FPayloadServerUseItem {
    unsigned int Field_0x00;     // +0x00  object id
    unsigned short Field_0x04;   // +0x04  equipment id
    unsigned short Field_0x06;   // +0x06  count
};
static_assert(sizeof(FPayloadServerUseItem) == 0x08, "USE_ITEM payload 0x08");

// ---------------------------------------------------------------------------
// IClient slot 60 (+0x0f0), ch2 op 0x07 FLPACKET_SERVER_LAUNCH -- the argument
// struct (0x28). rpclocal's sender (0x06d8a080) reorders it into
// FPayloadServerLaunch (rpc_payloads.h) and the receiver (caseD_7, 0x06d81951)
// puts every dword back. Moved here from rpclocal's private header because
// Server.dll builds it: IServerImpl::PlayerLaunch at S+0x48 of its frame
// (.text:06cefd1e .. 06cefdd0), then IClient +0xf0 (sid, &it) at .text:06cefdd7:
//   +0x00  the launching ship's id (PlayerLaunch's first argument) (.text:06cefd47)
//   +0x04  the object launched from, Server_GetLaunchPosition's *OutObjId (.text:06cefd7f)
//   +0x08  its launch_pos index, *OutKind, -1 without one        (.text:06cefd9c)
//   +0x0c  the request's orientation quaternion as is            (.text:06cefd91 ..)
//   +0x1c  the launch position minus the rotated HpMount offset  (.text:06cefd68 ..)
// The field names stay offset names because rpclocal's codec reads them so.
// ---------------------------------------------------------------------------
struct FServerLaunchInfo {
    unsigned int Field_0x00;    // +0x00  ship id
    unsigned int Field_0x04;    // +0x04  launch object id
    unsigned int Field_0x08;    // +0x08  launch_pos index (-1: none)
    Quaternion Orientation;     // +0x0c
    Vector Position;            // +0x1c
};
static_assert(sizeof(FServerLaunchInfo) == 0x28, "LAUNCH arg struct 0x28");

// ---------------------------------------------------------------------------
// IClient slot 54 (+0x0d8), ch1 op 0x01 FLPACKET_COMMON_UPDATEOBJECT. The argument
// struct (0x2c); the wire payload FPayloadCommonUpdateObject (rpclocal) holds the
// same fields in another order. Server.dll's only builder is StarSystem::
// sp_obj_update (FUN_06d0bbc0, .text:06d0bc27 .. 06d0bc70) at S+0xc of its frame,
// copied from the player's SSPObjUpdateInfo:
//   +0x00  ObjectId                      <- info +0x00
//   +0x04  Orientation                   <- info +0x04
//   +0x14  Position                      <- info +0x14
//   +0x20  the timestamp as a float      <- info +0x20 (FLD double / FSTP float)
//   +0x24  a raw dword copy              <- info +0x28 (the wire and the client
//                                           read it as a float)
//   +0x28  a byte                        <- info +0x2c
// The field names stay offset names because rpclocal's codec reads them so.
// ---------------------------------------------------------------------------
struct FCommonUpdateObjectInfo {
    unsigned int ObjectId;       // +0x00
    Quaternion Orientation;      // +0x04
    Vector Position;             // +0x14
    float Field_0x20;            // +0x20  timestamp
    float Field_0x24;            // +0x24  SSPObjUpdateInfo::Field_0x28, bit copy
    unsigned char Field_0x28;    // +0x28  SSPObjUpdateInfo::Field_0x2c
    unsigned char Pad_0x29[3];
};
static_assert(sizeof(FCommonUpdateObjectInfo) == 0x2c, "UPDATEOBJECT arg struct 0x2c");

// ---------------------------------------------------------------------------
// IClient slot 100 (+0x190), ch2 op 0x4e FLPACKET_SERVER_SETDIRECTIVE. Registry size
// 0xb4, payload 0x94.
//
// The argument IS the wire payload: rpclocal's sender (0x06d8a480, RET 8) writes the same
// offsets one by one and its receiver (caseD_4e, 0x06d81c3b) is `MOV ECX,0x25 ; REP MOVSD`
// from packet+0x20. Moved here from src/rpclocal/rpc_payloads.h (2026-09-24,
// claude:srv-ai) because Server.dll builds it: pub::AI::SubmitDirective
// (.text:06d5cbb0), when the target ship belongs to a player, fills one at S+0x24 of its
// frame and calls SendServerSetDirective(&OwnerPlayer, &Info) (.text:06d5d218 / .text:06d5d425).
//
// Default construction is inlined at every one of SubmitDirective's six case entries (and
// is rpclocal's FUN_06d84100): +0x18 = -1, +0x40 = 3, every other field 0, +0x00 unseeded.
// +0x00 is the directive kind SubmitDirective stores last, and each kind fills only its own
// fields from the op (the op offsets are common_pub.h's):
//   0 DirectiveCancelOp  nothing
//   1 DirectiveDelayOp   +0x0c = op+0x0c
//   2 DirectiveDockOp    +0x10 = op+0x0c, +0x14 = op+0x10, byte +0x08 = op+0x14,
//                        +0x18 = op+0x18, +0x1c = op+0x1c, +0x20 = op+0x20, +0x24 = op+0x24
//   3 DirectiveFollowOp  +0x28 = op+0x0c, +0x2c = op+0x10, +0x30 = op+0x14..0x1c,
//                        +0x3c = op+0x20
//   4 DirectiveGotoOp    +0x40 = op+0x0c, +0x44 = op+0x10..0x18, +0x50 = op+0x1c,
//                        +0x54/+0x60/+0x6c/+0x78 = op+0x20/+0x2c/+0x38/+0x44 (3 dwords each),
//                        +0x84 = op+0x50, +0x88 = op+0x54, bytes +0x8c..+0x8f = op+0x58..0x5b,
//                        +0x1c = op+0x5c, +0x20 = op+0x60, +0x24 = op+0x64, byte +0x90 = op+0x6c
//   5 DirectiveIdleOp    nothing
// The types below are the ones those source fields carry. The four 3-dword blocks at +0x54..
// +0x78 are seeded by Server's 3-dword setter FUN_06ce7c10 and filled from twelve uint32
// DirectiveGotoOp fields whose meaning is not recovered, so they stay three dwords.
// ---------------------------------------------------------------------------
struct FRpcDirectiveBlock {
    unsigned int Field_0x00;        // +0x00
    unsigned int Field_0x04;        // +0x04
    unsigned int Field_0x08;        // +0x08
};
static_assert(sizeof(FRpcDirectiveBlock) == 0x0c, "FUN_06d8f510 moves exactly three dwords");

struct FPayloadServerSetDirective {
    unsigned int Field_0x00;        // +0x00  directive kind 0..5 (see above); never seeded
    unsigned int Field_0x04;        // +0x04  always 0 from SubmitDirective
    unsigned char Field_0x08;       // +0x08  a byte on both sides (06d8a4b4); dock op+0x14
    unsigned char Pad_0x09[3];      // +0x09  retail leaves these three bytes untouched
    float Field_0x0c;               // +0x0c  delay op+0x0c (DelaySeconds)
    unsigned int Field_0x10;        // +0x10  dock op+0x0c (DockObjectId)
    int Field_0x14;                 // +0x14  dock op+0x10
    int Field_0x18;                 // +0x18  ctor seed -1; dock op+0x18 (DockPortIndex)
    unsigned int Field_0x1c;        // +0x1c  dock op+0x1c / goto op+0x5c
    float Field_0x20;               // +0x20  dock op+0x20 / goto op+0x60
    float Field_0x24;               // +0x24  dock op+0x24 / goto op+0x64
    unsigned int Field_0x28;        // +0x28  follow op+0x0c (TargetId)
    float Field_0x2c;               // +0x2c  follow op+0x10 (FollowDistance)
    Vector Field_0x30;              // +0x30  follow op+0x14..0x1c
    float Field_0x3c;               // +0x3c  follow op+0x20
    int Field_0x40;                 // +0x40  ctor seed 3; goto op+0x0c
    Vector Field_0x44;              // +0x44  goto op+0x10 (Destination)
    int Field_0x50;                 // +0x50  goto op+0x1c
    FRpcDirectiveBlock Block_0x54;  // +0x54  goto op+0x20..0x28
    FRpcDirectiveBlock Block_0x60;  // +0x60  goto op+0x2c..0x34
    FRpcDirectiveBlock Block_0x6c;  // +0x6c  goto op+0x38..0x40
    FRpcDirectiveBlock Block_0x78;  // +0x78  goto op+0x44..0x4c
    float Field_0x84;               // +0x84  goto op+0x50
    float Field_0x88;               // +0x88  goto op+0x54
    unsigned char Field_0x8c;       // +0x8c  goto op+0x58
    unsigned char Field_0x8d;       // +0x8d  goto op+0x59
    unsigned char Field_0x8e;       // +0x8e  goto op+0x5a
    unsigned char Field_0x8f;       // +0x8f  goto op+0x5b
    unsigned char Field_0x90;       // +0x90  goto op+0x6c
    unsigned char Pad_0x91[3];      // +0x91  swept up by the receiver's REP MOVSD
};
static_assert(offsetof(FPayloadServerSetDirective, Field_0x30) == 0x30, "SETDIRECTIVE +0x30");
static_assert(offsetof(FPayloadServerSetDirective, Field_0x44) == 0x44, "SETDIRECTIVE +0x44");
static_assert(offsetof(FPayloadServerSetDirective, Block_0x78) == 0x78, "SETDIRECTIVE +0x78");
static_assert(offsetof(FPayloadServerSetDirective, Field_0x90) == 0x90, "SETDIRECTIVE +0x90");
static_assert(sizeof(FPayloadServerSetDirective) == 0x94, "SETDIRECTIVE payload 0x94");

// ---------------------------------------------------------------------------
// IClient slot 70 (+0x118), ch2 op 0x45 FLPACKET_SERVER_SET_MISSION_MESSAGE.
//
// Retail's own name: Server.dll exports
// ?SetMissionMessage@CPlayerGroup@@QAEXAAUCSetMissionMessage@@@Z, which takes this
// very record. pub::Player::DisplayMissionMessage (.text:06d55830) builds it at
// ESP+0x8 of its frame: the FmtStr is default-constructed inline (.text:06d55887 ..
// .text:06d558c3 -- two dwords and fourteen bytes zeroed), FmtStr::operator= from
// the caller's text (.text:06d558d4), then +0x18 = the MissionMessageType argument
// (.text:06d558e0). rpclocal's sender (0x06d8af60) flattens +0x00 and ships +0x18
// (06d8afe9 / 06d8aff4). Moved here from src/rpclocal/rpc_payloads.h (2026-09-24,
// claude-code:w5-server-06d4) because Server.dll builds it; rpclocal keeps its old
// spelling through the typedef below.
// ---------------------------------------------------------------------------
struct CSetMissionMessage {
    FmtStr Text;                    // +0x00
    unsigned int Type;              // +0x18  MissionMessageType
};
static_assert(sizeof(CSetMissionMessage) == 0x1c, "SET_MISSION_MESSAGE argument struct 0x1c");
typedef CSetMissionMessage FServerSetMissionMessageInfo;

// ---------------------------------------------------------------------------
// IClient slot 71 (+0x11c), ch2 op 0x34 FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE.
//
// pub::Player::ReplaceMissionObjective (.text:06d55c10) builds it at ESP+0xc:
// +0x00 = *MissionId (.text:06d55cb9), +0x04 the FmtStr, default-constructed inline
// then FmtStr::operator=(&Objective.Description) (.text:06d55cca), +0x1c =
// Objective.Type (.text:06d55cd0 MOV ECX,[ESI] / .text:06d55cd2), byte +0x20 = the
// third argument (.text:06d55cc6). rpclocal's sender (0x06d8ad90) ships +0x00, the
// byte at +0x20 and the flattened +0x04 -- it never reads +0x1c. Moved here from
// src/rpclocal/rpc_payloads.h (2026-09-24, claude-code:w5-server-06d4).
// ---------------------------------------------------------------------------
struct FServerReplaceMissionObjectiveInfo {
    unsigned int Field_0x00;        // +0x00  mission id
    FmtStr Text;                    // +0x04  MissionObjective::Description
    unsigned int ObjectiveType;     // +0x1c  MissionObjective::Type (Server writes it, rpclocal never reads it)
    unsigned char State;            // +0x20  ReplaceMissionObjective's third argument, as a byte
    unsigned char Pad_0x21[3];      // +0x21
};
static_assert(sizeof(FServerReplaceMissionObjectiveInfo) == 0x24,
              "REPLACEMISSIONOBJECTIVE argument struct 0x24");

// ---------------------------------------------------------------------------
// IClient slot 73 (+0x124), ch2 op 0x33 FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE.
//
// pub::Player::SetMissionObjectiveState (.text:06d55d20) builds it at ESP+0x4:
// +0x00 = *MissionId (.text:06d55d61), byte +0x04 = the (int) third argument
// truncated to a byte (.text:06d55d5d / .text:06d55d78), +0x08 = the fourth argument
// (.text:06d55d69). rpclocal's sender (0x06d8ac40) reads the dword, the byte and the
// low word of +0x08. Moved here from src/rpclocal/rpc_payloads.h (2026-09-24,
// claude-code:w5-server-06d4).
// ---------------------------------------------------------------------------
struct FServerMissionObjectiveStateInfo {
    unsigned int Field_0x00;    // +0x00  mission id
    unsigned char Field_0x04;   // +0x04  objective index (third argument, as a byte)
    unsigned char Pad_0x05[3];
    unsigned int Field_0x08;    // +0x08  the fourth argument; receiver zero-extends the word
};
static_assert(sizeof(FServerMissionObjectiveStateInfo) == 0x0c, "SETMISSIONOBJECTIVESTATE arg struct 0x0c");

// ---------------------------------------------------------------------------
// IClient slot 87 (+0x15c), ch2 op 0x41 FLPACKET_SERVER_BURNFUSE.
//
// EqObj::light_fuse (.text:06cec4d0) builds it at ESP+0x28 of its frame
// (.text:06cec71c .. 06cec799): +0x00 = the igniting source id, +0x04 = the
// object's own id (vt +0x20), +0x08 = the fuse's ID_String value, word +0x0c =
// the sub-object (first SUBOBJ_ID_NONE, then the argument), +0x10 = the ignition
// time, +0x14 = the lifetime (first -1.0f, then the argument), byte +0x18 = 1
// (lit). EqObj::unlight_fuse (.text:06cec7f0) builds the same record with +0x18 = 0.
// StarSystem FUN_06d0e750 reads +0x04 for its visibility test and hands the
// record to IClient +0x15c; rpclocal's sender (0x06d8c170, MarshalBurnFuse) copies
// +0x00 .. +0x18. Declared here 2026-09-24 (claude-code:w5-server-06ce).
// ---------------------------------------------------------------------------
struct FServerBurnFuseInfo {
    unsigned int SourceId;      // +0x00
    unsigned int ObjectId;      // +0x04
    unsigned int FuseId;        // +0x08  ID_String value
    unsigned short SubObjId;    // +0x0c
    unsigned short Pad_0x0e;
    float AtT;                  // +0x10
    float Lifetime;             // +0x14
    unsigned char bLit;         // +0x18
    unsigned char Pad_0x19[3];
};
static_assert(sizeof(FServerBurnFuseInfo) == 0x1c, "BURNFUSE argument struct 0x1c");

// ---------------------------------------------------------------------------
// IClient slot 72 (+0x120), ch2 op 0x35 FLPACKET_SERVER_SETMISSIONOBJECTIVES.
// The sender (rpclocal 0x06d8b020) takes the client id BY REFERENCE
// (.text:06d8b1f3 MOV EDX,[ESP+0x28] / .text:06d8b1f7 MOV EDX,[EDX]) and reads
// this record from the second argument: +0x00 (06d8b049), the two FmtStrs at
// +0x0c / +0x24, the count at +0x08 and the entry array at +0x04 (06d8b11a /
// 06d8b156), the byte at +0x3c (06d8b04e). Built by pub::Player::SetMissionObjectives
// (.text:06d55960), CPlayerGroup::SetMissionObjectives (.text:06d5bfd0) and, empty,
// CPlayerGroup::DelMember (.text:06d5ba56 .. 06d5ba96). Moved here from
// rpclocal/rpc_payloads.h 2026-09-24 (claude-code:w5-server-06d4).
// ---------------------------------------------------------------------------
// The receiver rebuilds the element array with a VC6 vector-new: operator new(n*0x1c + 4),
// count in the first dword, elements from +4, FmtStr ctor/dtor as the element hooks
// (06d827bc..06d827ee, and the matching FUN_06db1dd6 teardown at 06d82887). The port uses
// a plain array plus the count already in the struct instead of the +4 cookie, which is
// the same thing for every reader the port has.
struct FServerMissionObjectiveEntry {
    FmtStr Text;                    // +0x00
    unsigned int State;             // +0x18  a BYTE on the wire, MOVZX'd to a dword here
};
static_assert(sizeof(FServerMissionObjectiveEntry) == 0x1c,
              "objective entry is 0x1c: FmtStr plus one dword");

struct FServerSetMissionObjectivesInfo {
    unsigned int Field_0x00;                // +0x00
    FServerMissionObjectiveEntry* Entries;  // +0x04
    unsigned int Count;                     // +0x08
    FmtStr TextA;                           // +0x0c
    FmtStr TextB;                           // +0x24
    unsigned char Field_0x3c;               // +0x3c
    unsigned char Pad_0x3d[3];              // +0x3d
};
static_assert(sizeof(FServerSetMissionObjectivesInfo) == 0x40,
              "SETMISSIONOBJECTIVES argument struct 0x40");

// ---------------------------------------------------------------------------
// IClient slot 62 (+0xf8), ch2 op 0x32 FLPACKET_SERVER_REQUESTSPACESCRIPT.
//
// Built by Server.dll's pub::SpaceObj::RequestSpaceScript (.text:06cf9b40): +0x000 = the
// object id (06cf9b62), +0x004 = safe_strcpy(.., 0x104, name) (06cf9ba3), +0x108..+0x110 =
// the Vector (06cf9b6c / 06cf9b89 / 06cf9b7a), +0x114 = *param_3 (06cf9b9c); then
// IClient+0xf8(param_4, &record). rpclocal's sender 0x06d8aa40 reads the same offsets, and
// the client's handler LAB_0053d9d0 reads +0x000 (object), +0x004 (name), +0x108 (position)
// and +0x114 (1 = keep the camera transform, 2 = identity orientation at +0x108).
// Moved here from rpclocal/rpc_payloads.h 2026-09-26 (claude-code:exe-w3d).
// ---------------------------------------------------------------------------
// 0x104 is not a guess: the rpclocal receiver's string local starts at ESP+0x54 and the next
// field it writes is at ESP+0x158 (06d81c8f), i.e. exactly 0x104 bytes later; Server.dll's
// safe_strcpy bound is the same 0x104.
const unsigned int kRequestSpaceScriptNameSize = 0x104;

struct FServerRequestSpaceScriptInfo {
    unsigned int Field_0x000;                       // +0x000  object id
    char Name[kRequestSpaceScriptNameSize];         // +0x004  script name
    unsigned int Field_0x108;                       // +0x108  position x (float bits)
    unsigned int Field_0x10c;                       // +0x10c  position y (float bits)
    unsigned int Field_0x110;                       // +0x110  position z (float bits)
    unsigned int Field_0x114;                       // +0x114  1 or 2; see rpc_payloads.h remap
};
static_assert(sizeof(FServerRequestSpaceScriptInfo) == 0x118, "REQUESTSPACESCRIPT argument struct 0x118");

// ---------------------------------------------------------------------------
// IClient slot 82 (+0x148), ch2 op 0x2b FLPACKET_SERVER_SETGROUPFEELINGS.
//
// rpclocal's receiver caseD_2b (0x06d815bb) strncpy()s the name to +0x00 (06d815f1), puts the
// relation array at +0x10 and its non-null count (a byte) at +0x14 (06d8160c); its sender
// 0x06d8a5c0 reads the same three fields (strlen of +0x00, the array at +0x10 while the index
// is below the byte at +0x14). freelancer.exe's FUN_0053da90 reads them the same way (name
// into a TString<16>, count * 8 bytes of Relation, Reputation::set). The inline name buffer
// is exactly 0x10 bytes (the array pointer follows it).
// Moved here from rpclocal/rpc_payloads.h 2026-09-26 (claude-code:exe-w3a, W4-C); the
// relation array is typed as Common's Reputation::Relation, as FServerSetReputationInfo's is.
// ---------------------------------------------------------------------------
struct FServerSetGroupFeelingsInfo {
    char Name[0x10];                        // +0x00
    ::Reputation::Relation* Relations;      // +0x10
    unsigned char RelationCount;            // +0x14
    unsigned char Pad_0x15[3];              // +0x15
};
static_assert(sizeof(FServerSetGroupFeelingsInfo) == 0x18,
              "SETGROUPFEELINGS argument struct 0x18");

// ---------------------------------------------------------------------------
// IClient::SendServerSystemSwitchOut (slot 57, +0x0e4), ch2 op 0x21
// FLPACKET_SERVER_SYSTEM_SWITCH_OUT. Registry size 0x28, payload 0x08.
//
// rpclocal's sender 0x06d8a860 reads two dwords through its second argument
// (06d8a87b MOV ECX,[EAX] -> packet+0x20; MOV EAX,[EAX+0x4] -> packet+0x24) and
// its receiver caseD_21 spills them into a file-static pair. Server.dll's builder
// (FUN_06cf6e60, .text:06cf6f89 / 06cf6f8f) puts *ObjId at +0x00 and *Destination
// at +0x04; freelancer.exe's LAB_0053d500 reads +0x00 as the ship (bucket 1) and
// +0x04 as the gate (bucket 2). Declared 2026-10-16 (IClient naming, phase 1) for
// the new IClient signature; rpclocal still marshals it through its generic
// FPayloadServerDwordPair and Server.dll through a local `unsigned int Wire[2]` --
// both are to be switched to this type.
// ---------------------------------------------------------------------------
struct FServerSystemSwitchOutInfo {
    unsigned int ShipId;        // +0x00
    unsigned int GateId;        // +0x04  the jump object the ship leaves through
};
static_assert(sizeof(FServerSystemSwitchOutInfo) == 0x08, "SYSTEM_SWITCH_OUT argument struct 0x08");

// ---------------------------------------------------------------------------
// IClient::SendServerLand (slot 59, +0x0ec), ch2 op 0x0b FLPACKET_SERVER_LAND.
// Registry size 0x2c, payload 0x0c.
//
// rpclocal's sender 0x06d8a990 copies three dwords through its second argument,
// order preserved (+0x00 -> packet+0x20, +0x08 -> packet+0x28, +0x04 -> packet+0x24).
// Server.dll's builder (FUN_06cf6fc0, .text:06cf702e .. 06cf7048) fills
// {*ObjId, *Arg2, *Arg3}; freelancer.exe's FUN_0053d720 looks +0x00 up as the ship,
// +0x04 as the dock object (its CEqObj +0x164 is the docking base) and hands &+0x08 to
// FUN_00543b40 as the base id. Declared 2026-10-16 (IClient naming, phase 1); rpclocal
// still marshals it through its generic FPayloadServerThreeDwords and Server.dll
// through a local `unsigned int Wire[3]` -- both are to be switched to this type.
// ---------------------------------------------------------------------------
struct FServerLandInfo {
    unsigned int ShipId;        // +0x00
    unsigned int DockId;        // +0x04  the object docked with
    unsigned int BaseId;        // +0x08
};
static_assert(sizeof(FServerLandInfo) == 0x0c, "LAND argument struct 0x0c");

// ===========================================================================
// The five argument structs below were private to src/rpclocal/rpc_payloads.h until
// 2026-10-16 (IClient naming, phase 2): Server.dll builds them and freelancer.exe /
// RemoteClient.dll read the same records, so each is declared once, here. The ones whose
// argument layout IS the wire payload keep their FPayload* name; rpclocal's codecs use
// them for both.
// ===========================================================================

// ---------------------------------------------------------------------------
// IClient::SendServerDestroyObject (slot 55, +0x0dc), ch2 op 0x06
// FLPACKET_SERVER_DESTROYOBJECT. Sender 0x06d89140 reads the dword at +0x00 and the byte
// at +0x04 through its second argument; receiver caseD_6 at 0x06d81f6a stores them into
// .data:0x06dbb824 / 0x06dbb828 and passes that address. The wire shape is
// FPayloadServerDestroyObject (rpc_payloads.h).
// ---------------------------------------------------------------------------
struct FServerDestroyObjectInfo {
    unsigned int ObjectId;      // +0x00
    unsigned char Field_0x04;   // +0x04  TODO: semantics unproven
    unsigned char Pad_0x05[3];
};
static_assert(sizeof(FServerDestroyObjectInfo) == 0x08, "DESTROYOBJECT arg struct 0x08");

// ---------------------------------------------------------------------------
// IClient::SendServerRepairObject (slot 84, +0x150), ch2 op 0x3c
// FLPACKET_SERVER_REPAIROBJECT. Sender 0x06d8bfe0 reads dword +0x00, word +0x04 (MOVZX)
// and dword +0x08; receiver caseD_3c at 0x06d82b55 writes the word back as a word and
// leaves +0x06 untouched. The wire shape is FPayloadServerRepairObject (rpc_payloads.h).
// ---------------------------------------------------------------------------
struct FServerRepairObjectInfo {
    unsigned int Field_0x00;
    unsigned short Field_0x04;
    unsigned short Pad_0x06;    // the receiver never writes this half
    unsigned int Field_0x08;
};
static_assert(sizeof(FServerRepairObjectInfo) == 0x0c, "REPAIROBJECT arg struct 0x0c");

// ---------------------------------------------------------------------------
// IClient::SendServerCfgInterfaceNotification (slot 76, +0x130), ch2 op 0x47
// FLPACKET_SERVER_CFGINTERFACENOTIFICATION, size 0x2c -> payload 0x0c.
// Sender 0x06d8ad30, receiver caseD_47 at 0x06d8244f, which normalises the flag through
// TEST/SETNZ before storing it. The argument IS the wire payload. Server.dll's builder is
// pub::Player::CfgInterfaceNotification (.text:06d54a10; block at .text:06d54a43..06d54a66:
// notification type, the int param, the bool), which writes an int into +0x04;
// freelancer.exe reads +0x04 as float bits -- TODO(type).
// ---------------------------------------------------------------------------
struct FPayloadServerCfgInterfaceNotification {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
    unsigned char Field_0x08;
    unsigned char Pad_0x09[3];
};
static_assert(sizeof(FPayloadServerCfgInterfaceNotification) == 0x0c, "CFGINTERFACENOTIFICATION payload 0x0c");

// ---------------------------------------------------------------------------
// IClient::SendServerSetCamera (slot 88, +0x160), ch2 op 0x4b FLPACKET_SERVER_SET_CAMERA,
// size 0x44 -> payload 0x24. Sender 0x06d8ae90, receiver caseD_4b at 0x06d82573. Nine
// dwords copied straight through; the receiver moves the four at +0x0c..+0x18 with
// FLD/FSTP. The argument IS the wire payload. Server.dll's builder is pub::Player::SetCamera
// (.text:06d55e60): +0x00 = transform.Translation, +0x0c = I3DMathEngine::MatrixToQuaternion
// (transform.Rotation) in (W,X,Y,Z) order, +0x1c = fov, +0x20 = aspect -- the field names
// below are that builder's.
// ---------------------------------------------------------------------------
struct FPayloadServerSetCamera {
    Vector Position;            // +0x00
    Quaternion Orient;          // +0x0c  (W, X, Y, Z)
    float Fov;                  // +0x1c
    float Aspect;               // +0x20
};
static_assert(offsetof(FPayloadServerSetCamera, Orient) == 0x0c, "SET_CAMERA +0x0c");
static_assert(offsetof(FPayloadServerSetCamera, Fov) == 0x1c, "SET_CAMERA +0x1c");
static_assert(sizeof(FPayloadServerSetCamera) == 0x24, "SET_CAMERA payload 0x24");

// ---------------------------------------------------------------------------
// IClient::SendServerAdjustAttitude (slot 67, +0x10c), ch2 op 0x2a
// FLPACKET_SERVER_ADJUSTATTITUDE, size 0x28 -> payload 0x08. Sender 0x06d88f70,
// receiver caseD_2a at 0x06d81317. The second field is a float on both sides (FLD/FSTP at
// 0x06d88f96 and 0x06d8131a). The argument IS the wire payload; no Server.dll caller.
// ---------------------------------------------------------------------------
struct FPayloadServerAdjustAttitude {
    unsigned int Field_0x00;    // +0x00
    float Field_0x04;           // +0x04
};
static_assert(sizeof(FPayloadServerAdjustAttitude) == 0x08, "ADJUSTATTITUDE payload 0x08");

// ---------------------------------------------------------------------------
// ch2 0x52 FLPACKET_SERVER_PLAYERLIST: one opcode for IClient slots 110..112, told apart
// by the Kind dword the packet carries (rpclocal payload +0x00, the remote packet's +0x20;
// rpclocal's receiver compares it against 0, 1 and 2 at .text:06d83102). Both packet
// constructors seed 3 (rpclocal 06d9ec60, RemoteClient 06b4cf20), which no receiver
// delivers. Shared by rpclocal (FPayloadServerPlayerList) and RemoteClient / RemoteServer
// (FRpcServerPlayerListPacket) so the senders and receivers cannot drift apart.
// ---------------------------------------------------------------------------
const unsigned int kPlayerListKindClear = 0u;   // slot 112 SendServerPlayerListClear
const unsigned int kPlayerListKindEntry = 1u;   // slot 110 SendServerPlayerListAdd
const unsigned int kPlayerListKindDrop  = 2u;   // slot 111 SendServerPlayerListRemove
const unsigned int kPlayerListKindNone  = 3u;   // the constructors' seed
