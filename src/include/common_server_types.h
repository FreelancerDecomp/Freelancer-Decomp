// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_pub.h"
#include "common_physys.h"   // Quaternion (SSPObjUpdateInfo::Orientation)

#include <cfloat>
#include <cstring>
#include <list>
#include <vector>

// ---------------------------------------------------------------------------
// Legacy representation retained by unmigrated records and rpclocal's internal
// packet storage: allocator padding followed by three pointers. New public
// owning payloads, including XFireWeaponInfo and XTractorObjects, use std::vector.
// ---------------------------------------------------------------------------
template<typename T>
struct FRetailVector {
    unsigned int AllocPad;   // +0x00  allocator sub-object (padding)
    T* Begin;                // +0x04  _First
    T* End;                  // +0x08  _Last
    T* Cap;                  // +0x0c  _End

    unsigned int Count() const {
        return Begin != nullptr ? (unsigned int)(End - Begin) : 0u;
    }
};

// ---------------------------------------------------------------------------
// SStartupInfo — 0x214 bytes. Ground truth: Server.dll.c
// ?Startup@IServerImpl@@UAE_NABUSStartupInfo@@@Z (0x64b70) reads
//   PlayerDB::init(&Players, *(uint*)(param_1 + 0x208), (bool)param_1[0x211])
//   FUN_06d43340((LPCSTR)param_1)                      -> LoadLibrary(ModulePath)
//   DataConfig::startup((char*)(param_1 + 0x104), ...)  -> the INI path
//   FUN_06d448d0(param_1 + 0x104, ...)                  -> the INI path again
//   IClient vtable+0x28(*(uint*)(param_1+0x208), *(uint*)(param_1+0x20c))
// and rpclocal's IServerImpl::Startup (0x06d8f1e0) does
//   strncpy(&DAT_06dbbbc0, (char*)param_1, 0x104)  /  GetModuleFileNameA(..., copy, 0x104)
// i.e. the module path is the FIRST 0x104 bytes, at offset +0x000, and the INI
// path follows at +0x104. freelancer.exe's own mirror copy
// (src/freelancer/Client/server_connect.cpp::BuildStartupInfo) agrees.
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct SStartupInfo {
    // +0x000: path of the module that owns GetClientInterface (the caller's own
    //         module; rpclocal overwrites this with its own path before forwarding).
    char ModulePath[0x104];

    // +0x104: path of freelancer.ini (DataConfig::startup / initial_world reader).
    char IniPath[0x104];

    // +0x208: max player count (PlayerDB::init, IClient slot 10 arg 0).
    unsigned int MaxPlayers;

    // +0x20c: the DirectPlay host port. IServerImpl::Startup forwards it to IClient slot 10
    //         as arg 1; RemoteClient's slot 10 (.text:06b397a6) passes it to
    //         CDPServer::BeginHosting, which adds it as the address's "port" component when
    //         non-zero (DALib .text:065c50e6). FLServer's -P switch sets it (error string
    //         0xf01f "Could not set the server port: %u").
    unsigned int Port;

    // +0x210: players may damage other players. Forwarded to FUN_06cfffd0 (player
    //         subsystem init) as arg 1, which stores it in GbAllowPvp; the damage paths
    //         drop player-on-player hits while it is 0. FLServer's Server Configuration
    //         dialog fills it from "Allow Players to Harm Other Players" (control 1059);
    //         the client's single-player startup sets it to 1.
    unsigned char bAllowPvp;

    // +0x211: new accounts may be created (PlayerDB::init arg 1 -> PlayerDB+0x0c, which
    //         gates account creation in PlayerDB's login path, .text:06d53f04). FLServer
    //         fills it from "Allow New Players" (control 1010); single player sets it to 1.
    unsigned char bAllowNewPlayers;

    // +0x212..0x213: padding
    unsigned char Pad_0x212[2];
};
#pragma pack(pop)

static_assert(sizeof(SStartupInfo) == 0x214, "SStartupInfo size mismatch");

// SLoginInfo — passed to IServer::Login and PlayerDB::login.
// Retail layout: wide-string account name at offset 0 (used as (wchar_t*)param_1
// throughout the decompilation), a creation-permission flag at offset 0xC0
// (checked in load_user_data before calling create_account).
// 96 wchar_t = 0xC0 bytes, then flag + padding to 0xC8 total.
#pragma pack(push, 1)
struct SLoginInfo {
    // AccountName cannot span +0x00..+0xBF: retail's sender,
    // rpclocal IServerImpl::Login (0x06d8cc70), does wcscpy from param_1 AND a
    // second wcscpy from LEA EDX,[ESI + 0xa0] (0x06d8cca5), then reads a byte at
    // +0xC0. The receiver caps the two strings at 0x28 and 0x10 wide characters
    // and writes their terminators at +0x4E and +0xBE, which fixes both extents.
    // freelancer.exe's own mirror in Client/server_connect.cpp already uses
    // wchar_t[40], i.e. the corrected bound rather than the old 96.
    wchar_t AccountName[40];    // +0x00: account/character name (wide), 0x50 bytes
    unsigned char Field_0x50[0x50]; // +0x50: TODO: not written by Login's sender
    // TODO: semantics unrecovered -- retail decorates neither side of this call,
    // so all that is confirmed is a second 16-wide-character string at +0xA0.
    wchar_t Field_0xA0[16];     // +0xA0: second wide string, 0x20 bytes
    unsigned int CreateFlag;    // +0xC0: nonzero = allow account creation
    unsigned int Pad_C4;        // +0xC4: padding
};
#pragma pack(pop)
static_assert(sizeof(SLoginInfo) == 0xC8, "SLoginInfo size mismatch");

// ===========================================================================
// IServer payload structures.
//
// Every layout below is reconstructed from two independent readers of the same
// bytes: the rpclocal.dll serializer (writes the fields into a CDAPacket from
// +0x20 on) and the Server.dll consumer (reads the same fields and dispatches
// them). Per-field derivation: IServer card in decomp-tracker.
// A static_assert is present only where the total size is actually proven.
// Fields whose SEMANTICS are not proven keep a mechanical Field_0xNN name and
// an open TODO — the primitive type IS proven in every such case.
// ===========================================================================

// XFireWeaponInfo — retail 0x20 bytes, fully recovered (§3.1). Slot 0.
// Retail +0x10 is a real VC6 std::vector<unsigned short> (allocator byte, _First +0x14,
// _Last +0x18, _End +0x1c) owned by the record. Per the "no retail ABI" rule the port
// holds a modern std::vector, so sizeof differs from retail's 0x20 and carries no
// static_assert; every module that sees this struct is ours.
// Special members, as retail inlines them (Common.dll exports none of them):
//   ctor -- freelancer.exe 0x0052d33c..0x0052d364 (FireBarrelAim), 0x005348b0..0x005348d5
//           (FUN_00534870); Server.dll 0x06d0e306..0x06d0e326 (notify_fire_weapon);
//           rpclocal packet ctor 0x06d9afc5..0x06d9afe3: ObjectId = 0, TargetPos = three
//           0x7f7fffff (FLT_MAX), the vector built empty.
//   dtor -- freelancer.exe FUN_0052d3f0 (out of line; also the inlined epilogues
//           0x0052d3cd, 0x00534b20, Server 0x06d0e4bc): operator delete(_First), then
//           _First/_Last/_End = 0 -- the implicit destructor of Hardpoints.
//   copy -- VC6 vector<unsigned short>::operator= inlined, always a deep copy of the
//           elements (Server 0x06d0e34a..0x06d0e474; rpclocal to-server caseD_2
//           0x06d875ad..0x06d876cb and client caseD_2 0x06d83426..0x06d8353c;
//           remoteserver FireWeapon 0x06bd81a0..): what the implicit copy does.
struct XFireWeaponInfo {
    XFireWeaponInfo()
        : ObjectId(0u)
    {
        TargetPos.x = FLT_MAX;
        TargetPos.y = FLT_MAX;
        TargetPos.z = FLT_MAX;
    }

    unsigned int ObjectId;                     // +0x00
    Vector TargetPos;                          // +0x04  3 floats (rpclocal FUN_06d8f510)
    std::vector<unsigned short> Hardpoints;    // +0x10  element stride 2 ((last-first) >> 1)
};

// XActivateEquip — 8 bytes, fully recovered (§3.2). Slot 1.
struct XActivateEquip {
    unsigned int ObjectId;   // +0x00
    unsigned short EquipId;  // +0x04
    bool bActivate;          // +0x06
    unsigned char Pad_0x07;  // +0x07
};
static_assert(sizeof(XActivateEquip) == 0x8, "XActivateEquip sizeof 0x8");

// XActivateCruise — 8 bytes (§3.3). Slot 2.
struct XActivateCruise {
    unsigned int ObjectId;      // +0x00
    bool bActivate;             // +0x04
    // TODO: semantics unproven. Second byte; rpclocal writes it to packet+0x25 and
    // Server.dll passes it as the 2nd argument of the sub-object's vtable+0x50.
    unsigned char Field_0x05;   // +0x05
    unsigned char Pad_0x06[2];  // +0x06
};
static_assert(sizeof(XActivateCruise) == 0x8, "XActivateCruise sizeof 0x8");

// XActivateThrusters — 8 bytes, fully recovered (§3.4). Slot 3.
struct XActivateThrusters {
    unsigned int ObjectId;      // +0x00
    bool bActivate;             // +0x04
    unsigned char Pad_0x05[3];  // +0x05
};
static_assert(sizeof(XActivateThrusters) == 0x8, "XActivateThrusters sizeof 0x8");

// XSetTarget — 0x10 bytes, fully recovered (§3.5). Slot 4.
struct XSetTarget {
    unsigned int ObjectId;          // +0x00
    unsigned short SubObjId;        // +0x04  compared against the SUBOBJ_ID_NONE export
    unsigned short Pad_0x06;        // +0x06
    unsigned int TargetObjectId;    // +0x08
    unsigned short TargetSubObjId;  // +0x0c
    unsigned short Pad_0x0e;        // +0x0e
};
static_assert(sizeof(XSetTarget) == 0x10, "XSetTarget sizeof 0x10");

// XTractorObjects — IServer slot 5. Retail's vector at +0x08 owns its ids;
// all our callers use this modern owning vector, never the VC6 object layout.
struct XTractorObjects {
    unsigned int ObjectId;                  // +0x00
    unsigned short TractorEquipId;          // +0x04
    unsigned short Pad_0x06;                // +0x06
    std::vector<unsigned int> TargetIds;
};

// XGoTradelane — 0x10 bytes, fully recovered (§3.7). Slot 6.
struct XGoTradelane {
    unsigned int ObjectId;        // +0x00  compared against the player's ship id (player+0x3e4)
    unsigned int TradelaneRing1;  // +0x04
    unsigned int TradelaneRing2;  // +0x08
    // TODO: semantics unproven (direction?). One byte; 5th argument of FUN_06d0df40.
    bool Field_0x0c;              // +0x0c
    unsigned char Pad_0x0d[3];    // +0x0d
};
static_assert(sizeof(XGoTradelane) == 0x10, "XGoTradelane sizeof 0x10");

// XJettisonCargo — 0x0c bytes, fully recovered (§3.8). Slot 8.
struct XJettisonCargo {
    unsigned int ObjectId;    // +0x00
    unsigned short ItemId;    // +0x04
    unsigned short Pad_0x06;  // +0x06
    unsigned int Count;       // +0x08
};
static_assert(sizeof(XJettisonCargo) == 0xc, "XJettisonCargo sizeof 0xc");

// SSPUseItem — 8 bytes, fully recovered (§3.10). Slot 56.
struct SSPUseItem {
    unsigned int ObjectId;   // +0x00
    unsigned short ItemId;   // +0x04
    unsigned short Count;    // +0x06
};
static_assert(sizeof(SSPUseItem) == 0x8, "SSPUseItem sizeof 0x8");

// SGFGoodBuyInfo — 0x10 bytes; the SIZE is proven (rpclocal 0x06d8d5a0 and
// Server 0x06d17bb0 both move exactly 4 dwords), the field semantics are not (§3.11).
struct SGFGoodBuyInfo {
    unsigned int Field_0x00;  // TODO: semantics unproven
    unsigned int Field_0x04;  // TODO: semantics unproven
    unsigned int Field_0x08;  // TODO: semantics unproven
    unsigned int Field_0x0c;  // TODO: semantics unproven
};
static_assert(sizeof(SGFGoodBuyInfo) == 0x10, "SGFGoodBuyInfo sizeof 0x10");

// SGFGoodSellInfo — PARTIAL (§3.12). Three dwords are serialized (rpclocal
// 0x06d8d600 / Server 0x06d17bf0); no reader touches anything past +0x08, so the
// total size is NOT proven and deliberately carries no static_assert.
struct SGFGoodSellInfo {
    unsigned int Field_0x00;  // TODO: semantics unproven
    unsigned int Field_0x04;  // TODO: semantics unproven
    unsigned int Field_0x08;  // TODO: semantics unproven
};

// SGFGoodVaporizedInfo — PARTIAL (§3.13), identical situation to SGFGoodSellInfo
// (rpclocal 0x06d8d650 / Server 0x06d17c30). Size NOT proven.
struct SGFGoodVaporizedInfo {
    unsigned int Field_0x00;  // TODO: semantics unproven
    unsigned int Field_0x04;  // TODO: semantics unproven
    unsigned int Field_0x08;  // TODO: semantics unproven
};

// SSPObjUpdateInfo — 0x30 bytes, fully recovered (§3.17). Slot 52.
struct SSPObjUpdateInfo {
    unsigned int ObjectId;       // +0x00
    Quaternion Orientation;      // +0x04  4 floats (rpclocal FUN_06d8f4f0)
    Vector Position;             // +0x14  3 floats (rpclocal FUN_06d8f510)
    double Timestamp;            // +0x20  read as an 8-byte double, narrowed to float on the wire
    // TODO: semantics unproven (throttle?). Serialized to packet+0x48.
    unsigned int Field_0x28;     // +0x28
    // TODO: semantics unproven (flags/state?). One byte, serialized to packet+0x20.
    unsigned char Field_0x2c;    // +0x2c
    unsigned char Pad_0x2d[3];   // +0x2d
};
static_assert(sizeof(SSPObjUpdateInfo) == 0x30, "SSPObjUpdateInfo sizeof 0x30");

// SSPMunitionCollisionInfo — 0x1c bytes, fully recovered (§3.18). Slot 53.
// Semantics proven 2026-09-24 by its consumer, Server.dll StarSystem::munition_collision
// (FUN_06d0d500, .text:06d0d51f-06d0d637), which reads every field: +0x00 goes to
// Archetype::GetEquipment (the munition archetype id), +0x04 to find_object and becomes
// FMunitionImpact::ShooterId and the DamageList inflictor, +0x08 is the object hit, +0x0c
// FMunitionImpact::SubObjId, +0x10..+0x18 FMunitionImpact::HitPos (three floats). The
// Field_0x names are kept because rpclocal's wire marshalling copies them dword for dword.
struct SSPMunitionCollisionInfo {
    unsigned int Field_0x00;   // +0x00  munition archetype id (GetEquipment, .text:06d0d5a4)
    unsigned int Field_0x04;   // +0x04  shooter object id (find_object, .text:06d0d533)
    unsigned int ObjectId;     // +0x08  Server.dll looks the object up by THIS field
    unsigned short Field_0x0c; // +0x0c  sub-object hit (FMunitionImpact +0x08, .text:06d0d5aa)
    unsigned short Pad_0x0e;   // +0x0e
    unsigned int Field_0x10;   // +0x10  hit position x (float bits, FMunitionImpact +0x0c)
    unsigned int Field_0x14;   // +0x14  hit position y (float bits)
    unsigned int Field_0x18;   // +0x18  hit position z (float bits)
};
static_assert(sizeof(SSPMunitionCollisionInfo) == 0x1c, "SSPMunitionCollisionInfo sizeof 0x1c");

// SSPObjCollisionInfo — 0x14 bytes, fully recovered (§3.19). Slot 55.
struct SSPObjCollisionInfo {
    unsigned int TargetObjectId;    // +0x00
    unsigned short TargetSubObjId;  // +0x04
    unsigned short Pad_0x06;        // +0x06
    unsigned int ObjectId;          // +0x08  Server.dll looks the object up by THIS field
    unsigned short SubObjId;        // +0x0c
    unsigned short Pad_0x0e;        // +0x0e
    // TODO: semantics unproven (energy/damage — probably a float). Serialized to packet+0x30.
    unsigned int Field_0x10;        // +0x10
};
static_assert(sizeof(SSPObjCollisionInfo) == 0x14, "SSPObjCollisionInfo sizeof 0x14");

// SSPBadLandsObjCollisionInfo — 0x0c bytes, fully recovered (§3.20). Slot 54.
struct SSPBadLandsObjCollisionInfo {
    unsigned int Field_0x00;    // TODO: semantics unproven
    unsigned int ObjectId;      // +0x04
    unsigned short Field_0x08;  // TODO: semantics unproven (SubObjId?)
    unsigned short Pad_0x0a;    // +0x0a
};
static_assert(sizeof(SSPBadLandsObjCollisionInfo) == 0xc, "SSPBadLandsObjCollisionInfo sizeof 0xc");

// CollisionGroupDesc — 8 bytes, size proven (§3.15). ReqCollisionGroups takes a
// std::list<CollisionGroupDesc> const&; rpclocal's list copier FUN_06d8f710
// allocates 0x10 per node (8 bytes of prev/next links + this payload) and copies
// exactly two dwords.
// It is NOT redeclared here: CollisionGroupDesc is a Common.dll type, already
// published in common_cobject.h as { ushort Id; ushort Pad02; float Health; }
// with its own static_assert(sizeof == 8) and comparison operators. A second
// body would be exactly the "two declarations, drifting slot order" hazard
// AGENTS.md warns about, so this is a forward declaration only; translation
// units that need the complete type include common_cobject.h.
struct CollisionGroupDesc;

// InvincibilityReason — SPRequestInvincibility's 3rd argument (§3.16).
// rpclocal 0x06d8ec80 writes it as a single byte (packet+0x24), so every value
// fits in a byte; the ABI still passes a full dword (W4 + RET 16).
// TODO: the enumerator list was NOT recovered from ground truth. The only two
// values observed are the ones Server.dll's SPRequestInvincibility branches on
// (0x11380): reason 2 scans the player's system for nearby objects, reason 5
// dispatches directly. They are named after their numeric value on purpose.
// A fixed underlying type also admits every raw BYTE value decoded from the wire.
enum InvincibilityReason : int {
    INVINCIBILITY_REASON_2 = 2,
    INVINCIBILITY_REASON_5 = 5,
};

// CommResult enum
enum class CommResult : int {
    SUCCESS = 0,
    FAILED = 1,
    CANCELLED = 2,
    TIMEOUT = 3
};

// EFLConnection enum
enum class EFLConnection : int {
    NONE = 0,
    LOCAL = 1,
    REMOTE = 2
};

// XSetManeuver — 0x0c bytes (§3.9). The size was already right; the field NAMES
// were not. Server.dll's SetManeuver (0x11540) resolves the CEqObj from +0x04, so
// +0x04 is the object id and the other two dwords are still unidentified.
// The only client sender, HUD_Maneuvers::RequestManeuver (freelancer.exe 0x004d935a..0x004d93ac,
// hud_maneuvers.cpp), fills +0x00 = the id of the HUD's watched object (the player ship),
// +0x04 = the player ship's target id, +0x08 = the behaviour id it switched to.
struct XSetManeuver {
    // TODO: semantics unproven (maneuver type?). rpclocal writes it to packet+0x20.
    unsigned int Field_0x00;  // +0x00
    unsigned int ObjectId;    // +0x04  FUN_06d006c0((uint*)(info + 4), ...)
    // TODO: semantics unproven (target/parameter?). rpclocal writes it to packet+0x28.
    unsigned int Field_0x08;  // +0x08
};
static_assert(sizeof(XSetManeuver) == 0xc, "XSetManeuver sizeof 0xc");

// ---------------------------------------------------------------------------
// CDAPacket — the RPC packet base, exactly 0x20 bytes. Ground truth: the packet
// constructors the rpclocal registry installs, e.g. FUN_06d9c860
// (FLPACKET_SERVER_LOGINRESPONSE, operator new(0x24)):
//     [EAX+0x04] = 1                 ; usage count
//     [EAX+0x08] = 0                 ; queue link
//     [EAX+0x10] = 0
//     [EAX+0x1c] = 0
//     [EAX+0x20] = 0                 ; first payload byte
//     [EAX]      = PTR_FUN_06db4ffc  ; vptr, 4 slots { dtor, codec A, codec B, dump }
//     [EAX+0x0c] = 0x20002           ; PacketId = Channel << 16 | Opcode
// Both dispatchers read the routing dword at +0x0c; the payload of a concrete
// packet type starts at +0x20 and its total size is that type's operator new
// argument (0x24 minimum, 0xec maximum). Full registry: src/rpclocal/rpc_packet_registry.cpp (protocol: docs/re/rpclocal.md).
// The previous {type, size, data[]} declaration here was fabricated.
// ---------------------------------------------------------------------------
// A class, not a struct: retail rpclocal and Server.dll both export
// ?PushToServer@IServerImpl@@UAEXPAVCDAPacket@@@Z (`PAV` = pointer to class).
class CDAPacket {
public:
    void* VTable;             // +0x00  packet-class vtable (4 slots)
    unsigned int UsageCount;   // +0x04  ctor sets 1
    CDAPacket* QueueLink;      // +0x08  CDAConnection links queued packets here
    unsigned int PacketId;     // +0x0c  Channel << 16 | Opcode
    unsigned int Field_0x10;   // +0x10  zeroed by the ctor; TODO: semantics unproven
    unsigned int Field_0x14;   // +0x14  the ctor does not touch it
    unsigned int Field_0x18;   // +0x18  the ctor does not touch it
    unsigned int Field_0x1c;   // +0x1c  zeroed by the ctor; TODO: semantics unproven
    // Payload starts at +0x20 and belongs to the derived packet class.

    unsigned int GetChannel() const { return PacketId >> 16; }
    unsigned int GetOpcode() const { return PacketId & 0xfff; }
};
static_assert(sizeof(CDAPacket) == 0x20, "CDAPacket base sizeof 0x20");

// EquipDescList / EquipDesc — real definitions in common_goods.h; referenced here
// only by const-reference (ReqCargo/ReqEquipment/AddTradeEquip/DelTradeEquip), so
// forward declarations suffice and the types stay declared in exactly one place.
// EquipDescList is a class in retail (?ReqCargo@IServerImpl@@UAEXABVEquipDescList@@I@Z).
class EquipDescList;
struct EquipDesc;

// CHARACTER_ID — retail is a fixed 512-byte (0x200), NUL-terminated, case-insensitive
// character/account identifier string — NOT a 4-byte int. Evidence (Common.dll RVAs):
//   ctor      0x1030  MOV byte ptr [this],0        — writes ONE byte (empty string)
//   is_valid  0x1040  reads ONE byte, returns it != 0 (non-empty-string check)
//   invalidate 0x1050 writes ONE byte to 0
//   operator= 0xce720 byte-copy loop that stops after copying a 0 byte  (== strcpy)
//   operator< 0xce740 stricmp(a,b) < 0
//   operator== 0xce760 stricmp(a,b) == 0
//   operator!= 0xce780 stricmp(a,b) != 0
// Buffer size confirmed from freelancer.exe.c FUN_005aa9c0 (0x005aa9c0): a per-player record
// is allocated as operator_new(0x2dc), CHARACTER_ID is copied at offset 0, and two flag bytes
// immediately follow at [0x200]/[0x201] — i.e. the CHARACTER_ID field spans exactly [0x000,0x200).
// Corroborated by scratch buffers built the same way elsewhere: `CHARACTER_ID local_200[512]`
// (FUN_00487550-adjacent) and `CHARACTER_ID aCStack_208[516]` (FUN_0056ce30) — both padded-up
// staging buffers for a value later strcpy'd into a real CHARACTER_ID via operator=.
//
// EXPORTS. Retail Common.dll exports SEVEN CHARACTER_ID symbols and no more --
// there is no destructor, no copy constructor and no operator> in the table.
// Read straight off Common.dll.txt's export annotations (RVA / ordinal /
// decorated name):
//   0x1030   154  ??0CHARACTER_ID@@QAE@XZ            CHARACTER_ID::CHARACTER_ID(void)
//   0x1040  3228  ?is_valid@CHARACTER_ID@@QBE_NXZ    bool is_valid(void) const
//   0x1050  3166  ?invalidate@CHARACTER_ID@@QAEXXZ   void invalidate(void)
//   0xce720  735  ??4CHARACTER_ID@@QAEABU0@ABU0@@Z   CHARACTER_ID const& operator=(CHARACTER_ID const&)
//   0xce740  952  ??M@YA_NABUCHARACTER_ID@@0@Z       bool operator<(CHARACTER_ID const&, CHARACTER_ID const&)
//   0xce760  883  ??8@YA_NABUCHARACTER_ID@@0@Z       bool operator==(CHARACTER_ID const&, CHARACTER_ID const&)
//   0xce780  891  ??9@YA_NABUCHARACTER_ID@@0@Z       bool operator!=(CHARACTER_ID const&, CHARACTER_ID const&)
// Server.dll imports operator= by name through IAT slot .rdata:06d648ec
// (`06d4a7d3  CALL dword ptr [->COMMON.DLL::CHARACTER_ID::operator=]`), so the
// export is a real cross-module contract, not decoration. Every one of the
// seven therefore carries COMMON_API, and the two bodies that used to be inline
// are now out of line in src/Common/common_id.cpp: a __declspec(dllimport)
// declaration must not also carry a definition in the importing TU.
struct CHARACTER_ID {
    char Buffer[0x200];

    // Retail ctor only zeroes Buffer[0]; we zero the whole buffer to avoid reading
    // uninitialized memory elsewhere (harmless deviation — no code inspects trailing bytes).
    COMMON_API CHARACTER_ID();

    COMMON_API bool is_valid() const;
    COMMON_API void invalidate();

    // Retail returns a const reference: ??4CHARACTER_ID@@QAEABU0@ABU0@@Z.
    COMMON_API CHARACTER_ID const& operator=(CHARACTER_ID const& other);
};

static_assert(sizeof(CHARACTER_ID) == 0x200, "CHARACTER_ID must match the retail 512-byte buffer");

COMMON_API bool operator<(CHARACTER_ID const& lhs, CHARACTER_ID const& rhs);
COMMON_API bool operator==(CHARACTER_ID const& lhs, CHARACTER_ID const& rhs);
COMMON_API bool operator!=(CHARACTER_ID const& lhs, CHARACTER_ID const& rhs);

// ---------------------------------------------------------------------------
// SCreateCharacterInfo — PARTIALLY recovered (§3.14). CreateNewCharacter's first
// argument.
//   +0x00  wide, NUL-terminated character name. rpclocal 0x06d8cdf0 does
//          wcsncpy(scratch, (wchar_t*)param_1, 0x40); Server.dll's
//          PlayerDB::create_new_character (0x06d53fd0) does wcslen((wchar_t*)param_1).
//   +0x30..+0x3f  four dwords, read individually by rpclocal's serializer.
// The reads bound Name to [0x00, 0x30) — 24 wchar_t — because +0x30 is read as a
// dword. That contradicts rpclocal's wcsncpy count of 0x40, which is only safe
// because the string is NUL-terminated and the destination is a 0x80-byte stack
// buffer. TODO: the exact Name capacity is NOT proven from ground truth, hence
// the total size is not proven either and this struct carries no static_assert.
// (The serialized packet payload is exactly 0x90 bytes — 0x24 dwords — but that
// is the packet's size, not this struct's.)
// The old {wchar_t Name[64]; CostumeId; ShipArchId; field_88;} put the trailing
// fields at +0x80; retail has them at +0x30, and there are four of them.
// ---------------------------------------------------------------------------
struct SCreateCharacterInfo {
    wchar_t Name[24];         // +0x00 .. +0x2f
    // Proven 2026-09-24 (claude:srv-save-2) by Server.dll FUN_06d4b790, which
    // looks each one up in NewChar::TheDB (.text:06d4b8b1 .. .text:06d4b8fe),
    // against Common.dll's Load that fills those trees (.text:063149d0):
    unsigned int Field_0x30;  // [Faction] nickname id -- slot 0, faction tree +0x04
    unsigned int Field_0x34;  // home base id (a [Faction] `base`) -- slot 3, tree +0x40
    unsigned int Field_0x38;  // [Package] nickname id -- slot 1, tree +0x18
    unsigned int Field_0x3c;  // [Pilot] nickname id -- slot 2, tree +0x2c
    // Names kept as offsets only because rpclocal's codec (in another lane's
    // hands this wave) spells them; rename to FactionId/BaseId/PackageId/PilotId.
};

// ServerStats — 0x2c bytes, matches retail layout (11 DWORDs)
// GetServerStats copies from a global stats buffer, then overrides:
//   +0x00 = active player count (from traverse_active)
//   +0x20, +0x24, +0x28 = 0
// Named as retail names it: ?GetServerStats@IServerImpl@@QAEXAAUServerStats@@@Z
// (rpclocal ordinal 38, Server.dll ordinal 265). rpclocal's body is a bare RET 4
// (0x06d8eae0) and never touches the struct.
// Server.dll's global copy lives at .data:06d8f998 (GServerStats, IServerImpl.cpp);
// Startup zero-fills all 11 dwords (.text:06d44c04 REP STOSD) and the counters
// below are the only other writers in the module.
struct ServerStats {
    unsigned int PlayerCount;           // +0x00: active player count (overridden)
    unsigned int LoginSuccessCount;     // +0x04: INC at .text:06d4511d (Login, result 3)
    unsigned int LogoutCount;           // +0x08: INC at .text:06d54489 (PlayerDB::logout)
    unsigned int LoginFailCount;        // +0x0c: INC at .text:06d45127 (Login, other results)
    // +0x10: INC at .text:06ce825f, the only writer besides Startup. It sits in
    // EqObj's +0x158 destroy override (FUN_06ce8080) on the path taken when the
    // object has an owning player (CSimple+0xb4 != 0), !SinglePlayer(), the
    // destroy kind is 1 and is_player() (IObjInspect slot 47) is true, right
    // after FUN_06d19110 broadcasts control message 0x14 with that player's id:
    // a count of multiplayer player-ship deaths. Ported: EqObj::destroy
    // (src/Server/server_gameobject.cpp) increments it on that path.
    unsigned int Field_0x10;
    unsigned int Field_0x14;            // +0x14: only the Startup zero-fill writes it
    unsigned int Field_0x18;            // +0x18: only the Startup zero-fill writes it
    unsigned int AbortMissionCount;     // +0x1c: INC at .text:06d4582f (AbortMission)
    unsigned int Reserved1;             // +0x20: always 0
    unsigned int Reserved2;             // +0x24: always 0
    unsigned int Reserved3;             // +0x28: always 0
};
static_assert(sizeof(ServerStats) == 0x2c, "ServerStats sizeof 0x2c");

// CHAT_ID — 4-byte chat identifier (passed by value in retail)
// Low 16 bits = player/session ID, upper bits = flags/channel info
// The retail name is the type name: rpclocal and Server.dll both export
// ?SubmitChat@IServerImpl@@UAEXUCHAT_ID@@KPBX0H@Z (`U` = struct CHAT_ID, by value).
struct CHAT_ID {
    unsigned int Id;

    CHAT_ID() : Id(0) {}
    CHAT_ID(unsigned int InId) : Id(InId) {}
    unsigned short GetPlayerId() const { return (unsigned short)(Id & 0xFFFF); }
};

// DamageEntry lives in common_misc.h -- the real one, with SubObjFate and the retail 0x18
// layout. A three-field stand-in used to be declared here; nothing referenced it, and it
// collided with the real definition as soon as both headers met in one translation unit.
