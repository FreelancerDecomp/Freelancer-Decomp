// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_pub.h"
#include "common_csys.h"   // Matrix (ShipInfo / SolarInfo orientation)
#include "common_metalist.h" // OwnerList<CargoDesc> (ShipInfo +0x5c)
#include "server_exports.h"

// CObject — base game object in Server.dll
// Layout derived from retail Server.dll.c decompilation
// All offsets verified against GetAtmosphereRange (0x19800), GetType (0x18da0),
// and constructor FUN_06cee360

struct FCEquipManager; // forward declaration for FCObject::EquipMgr pointer

// ===========================================================================
// DEPRECATED, DO NOT ADD USERS.
//
// FCObject (and FCShip in server_cship.h) is a flat
// placeholder struct that conflates TWO different retail objects: the IObjRW
// wrapper Server.dll owns (vtables at +0x00/+0x04, CBase::Object at +0x10) and
// the Common.dll CObject it points at (ObjectClass at +0x4c, archetype at +0x88,
// physics body at +0x54). Nothing in retail has that layout.
//
// As of the "real objects instead of malloc blocks" pass, pub::SpaceObj::Create /
// CreateSolar / CreateLoot build genuine GameObject/Ship/Solar/Loot wrappers
// (src/include/server_gameobject.h) around genuine Common.dll CShip/CSolar/CLoot
// instances, and the FLID registry holds those wrappers. Every pub::SpaceObj::
// export now dispatches through IObjInspect instead of reading raw offsets.
//
// These structs and the FCEquipManager/FCEquipItem pair below survive only
// because nothing has claimed the job of deleting them yet; they have no users
// left in src/Server. TODO: remove them, along with COBJECT_MIN_SIZE and
// CSHIP_SIZE, once nothing in the tree mentions them.
// ===========================================================================
struct FCObject {
    // +0x00: vtable pointer (IObjInspect)
    void* VTable_Inspect;
    // +0x04: vtable pointer (IObjAffect)
    void* VTable_Affect;
    // +0x08: FLID / object ID
    unsigned int ObjectID;
    // +0x0C: PhySys-related vtable/pointer
    void* PhySysVTable;
    // +0x10: flags byte
    unsigned char Flags;
    // +0x11..0x17: padding/various small fields
    unsigned char Padding1[7];
    // +0x18: linked list node pointer (owner list)
    void* OwnerListNode;
    // +0x1C..0x34: various fields
    unsigned char Reserved1[0x18];
    // +0x38: radius (float) — used by GetAtmosphereRange, GetRadius
    float Radius;
    // +0x3C..0x4B: padding/fields
    unsigned char Reserved2[0x10];
    // +0x4C: type flags (uint)
    //   & 3 == 3 → game object
    //   & 0x103 == 0x103 → generic game object (ship, solar, loot)
    //   & 0x303 == 0x303 → planet/sun
    //   & 0x503 == 0x503 → ship
    //   & 0x803 == 0x803 → loot/cargo container
    unsigned int TypeFlags;
    // +0x50: unidentified dword. Retail's physics-body read is [CObject + 0x54], not
    // +0x50 — see pub::SpaceObj::AddImpulse at .text:06cf90f1 (MOV ECX,[EAX + 0x54]).
    unsigned int Field_0x50;
    // +0x54: physics body pointer (PhySys::Body*)
    void* PhysicsBody;
    // +0x58..0x87: controllers, system ref, etc.
    unsigned char Reserved3[0x30];
    // +0x88: archetype pointer (Archetype::Base* or similar)
    //   *(arch_ptr + 8) = archetype hash ID
    void* ArchetypePtr;
    // +0x8C: archetype array end (for terminal info iteration)
    void* ArchetypeEnd;
    // +0x90..0xDF: various fields
    unsigned char Reserved4[0x50];
    // +0xE0: object type (uint) — returned by GetType
    unsigned int ObjectType;
    // +0xE4: equipment manager pointer (CEquipManager*)
    FCEquipManager* EquipMgr;
    // +0xE8..0x103: various fields
    unsigned char Reserved5[0x1C];
    // +0x104: reputation vibe handle (int)
    int RepVibe;
    // +0x108: costume data (Costume struct, 0x34 bytes = 13 dwords)
    unsigned int Costume[13];
    // +0x13C: voice ID (uint)
    unsigned int VoiceID;
    // +0x140..0x14F: various fields
    unsigned char Reserved6[0x10];
    // +0x150: solar vtable/get_solar function pointer
    void* SolarVTable;
    // +0x154..0x15F: various fields
    unsigned char Reserved7[0xC];
    // +0x160: docking target primary (uint)
    unsigned int DockTargetPrimary;
    // +0x164: docking target secondary (uint) — preferred if valid FLID
    unsigned int DockTargetSecondary;
    // +0x168..0x193: various fields
    unsigned char Reserved8[0x2C];
    // +0x194: behavior interface pointer (IBehaviorManager*)
    void* BehaviorInterface;
    // +0x198..0x1AB: various fields
    unsigned char Reserved9[0x14];
    // +0x1AC: atmosphere range (float) — used by GetAtmosphereRange
    float AtmosphereRange;
    // +0x1B0..0x1D7: various fields
    unsigned char Reserved10[0x28];
    // +0x1D8: tradelane previous gate/ring (uint)
    unsigned int TradelanePrev;
    // +0x1DC: tradelane next gate/ring (uint)
    unsigned int TradelaneNext;
    // +0x1E0..0x1F7: various fields
    unsigned char Reserved11[0x18];
    // +0x1F8: wingman list begin pointer
    void** WingmanListBegin;
    // +0x1FC: wingman list end pointer
    void** WingmanListEnd;
    // +0x200+: additional fields
};

// Object lookup (defined in server_pub.cpp): Server_LookupGameObject
// (server_gameobject.h) with the result flattened to void*. Server-internal,
// not exported.
void* Server_LookupObject(unsigned int obj_id);

// Minimum allocation size for FCObject
static const unsigned int COBJECT_MIN_SIZE = 0x1B0;

// Helper to check if object is a game object (type flags & 3 == 3)
inline bool IsGameObject(const FCObject* obj) {
    return obj && (obj->TypeFlags & 3) == 3;
}

// Helper to check if object is planet/sun (type flags & 0x303 == 0x303)
inline bool IsPlanetOrSun(const FCObject* obj) {
    return obj && (obj->TypeFlags & 0x303) == 0x303;
}

// Helper to check if object is a generic game object (type flags & 0x103 == 0x103)
inline bool IsGenericObject(const FCObject* obj) {
    return obj && (obj->TypeFlags & 0x103) == 0x103;
}

// Helper to check if object is a ship (type flags & 0x503 == 0x503)
inline bool IsShip(const FCObject* obj) {
    return obj && (obj->TypeFlags & 0x503) == 0x503;
}

// Helper to check if object is a loot/cargo container (type flags & 0x803 == 0x803)
inline bool IsLootContainer(const FCObject* obj) {
    return obj && (obj->TypeFlags & 0x803) == 0x803;
}

// ===========================================================================
// Equipment system — CEquipManager / CEquip / CECargo
// Layout derived from retail Server.dll.c decompilation
// CEquipManager stored at FCObject + 0xE4
// ===========================================================================

// Equipment type masks (used by CEquipTraverser)
//
// Only two of these are confirmed against retail:
//   0x10000 - pub::SpaceObj::EnumerateCargo   (CEquipTraverser(0x10000))
//   0x80000 - pub::SpaceObj::GetScannerRange  (CEquipManager::FindFirst(mgr, 0x80000))
// The rest are unverified guesses from an earlier pass, and at least one NAME is
// provably wrong: pub::SpaceObj::ActivateEquipment case 2 uses mask 0x1000 - called
// EQUIP_TYPE_COUNTERMEASURE here - while its case 3, which walks CELightEquip items,
// uses mask 1, for which there is no constant below at all (EQUIP_TYPE_LIGHT is 0x100).
// TODO: re-derive the whole table from the retail CEquip class hierarchy before
// trusting any of the unconfirmed entries.
enum EEquipTypeMask {
    EQUIP_TYPE_CARGO       = 0x10000,   // confirmed
    EQUIP_TYPE_SCANNER     = 0x80000,   // confirmed
    EQUIP_TYPE_LIGHT       = 0x100,     // unconfirmed - retail traverses lights with mask 1
    EQUIP_TYPE_SHIELD      = 0x200,     // unconfirmed
    EQUIP_TYPE_WEAPON      = 0x400,     // unconfirmed
    EQUIP_TYPE_MINE        = 0x800,     // unconfirmed
    EQUIP_TYPE_COUNTERMEASURE = 0x1000, // value confirmed (ActivateEquipment case 2), name is not
};

// Single equipment/cargo item
struct FCEquipItem {
    FCEquipItem* Next;            // +0x00: next in linked list
    FCEquipItem* Prev;            // +0x04: prev in linked list
    unsigned int ArchetypeID;     // +0x08: archetype hash
    unsigned short EquipID;       // +0x0C: unique equipment slot ID
    unsigned short Padding;       // +0x0E
    unsigned int Count;           // +0x10: stack count (for cargo)
    float Health;                 // +0x14: item health/status (0..1)
    unsigned int TypeMask;        // +0x18: equipment type flags
    bool bEquipped;               // +0x1C: is currently equipped
    unsigned char Padding2[3];    // +0x1D
    void* OwnerObject;            // +0x20: owning CObject pointer
    // vtable at +0x00 when used as CEquip (vtable+0x28 = GetEquipDesc)
    void* VTable;                 // +0x24
};

// Equipment manager — manages all equipment on a CObject
struct FCEquipManager {
    FCEquipItem* Head;            // +0x00: first item
    FCEquipItem* Tail;            // +0x04: last item  
    unsigned int ItemCount;       // +0x08: total items
    unsigned short NextEquipID;   // +0x0C: next available equip slot ID

    // Traverse items matching type_mask
    // Returns first matching item, or nullptr
    FCEquipItem* Traverse(unsigned int type_mask) {
        FCEquipItem* item = Head;
        while (item) {
            if (item->TypeMask & type_mask) return item;
            item = item->Next;
        }
        return nullptr;
    }

    // Find item by equip slot ID
    FCEquipItem* FindByID(unsigned short equip_id) {
        FCEquipItem* item = Head;
        while (item) {
            if (item->EquipID == equip_id) return item;
            item = item->Next;
        }
        return nullptr;
    }

    // Add item to manager
    void AddItem(FCEquipItem* item) {
        item->Prev = Tail;
        item->Next = nullptr;
        if (Tail) Tail->Next = item;
        else Head = item;
        Tail = item;
        ItemCount++;
    }

    // Remove item from manager
    void RemoveItem(FCEquipItem* item) {
        if (item->Prev) item->Prev->Next = item->Next;
        else Head = item->Next;
        if (item->Next) item->Next->Prev = item->Prev;
        else Tail = item->Prev;
        ItemCount--;
    }
};

// Get CEquipManager pointer from FCObject (at offset +0xE4)
inline FCEquipManager* GetEquipManager(FCObject* obj) {
    if (!obj) return nullptr;
    return *(FCEquipManager**)((char*)obj + 0xE4);
}

// ===========================================================================
// pub::SpaceObj info structures — input to Create / CreateSolar / CreateLoot
// Layouts reconstructed from retail Server.dll.c decompilation access patterns
// ===========================================================================

// DestroyType -- a GLOBAL enum in retail, not pub::SpaceObj's:
//   ?Destroy@SpaceObj@pub@@YAHIW4DestroyType@@@Z
enum DestroyType {
    DESTROY_NORMAL = 0,
    DESTROY_TYPE1 = 1,
    DESTROY_TYPE2 = 2,
};

// EQUIPMENT_RTYPE -- return type for ActivateEquipment, also GLOBAL:
//   ?ActivateEquipment@SpaceObj@pub@@YA?AW4EQUIPMENT_RTYPE@@ABIPAUEQInfo@12@@Z
// (EQInfo, by contrast, is pub::SpaceObj's -- the `@12@` back-reference.)
enum EQUIPMENT_RTYPE {
    EQUIPMENT_RTYPE_OK = 0,
    EQUIPMENT_RTYPE_INVALID = -1,
    EQUIPMENT_RTYPE_NOT_FOUND = -2,
    EQUIPMENT_RTYPE_ERROR = -4,
    EQUIPMENT_RTYPE_BAD_TYPE = -5,
};

namespace pub {
namespace SpaceObj {

// ShipInfo -- pub::SpaceObj::Create's argument
// (?Create@SpaceObj@pub@@YAHAAIABUShipInfo@12@@Z, .text:06cf7c90).
//
// Every placed member is proven by a load in pub::SpaceObj::Create (ESI = &info,
// .text:06cf7cf7). Create copies them into its CShipCreateInfo request (built on
// the stack, CEqObjCreateInfo ctor .text:06cf7d23, server_createinfo.h) and hands
// that to FUN_06d0a880, which copies it again (FUN_06d0af00) and fills
// CShip::CreateParms from the copy. The "request +N" and "Parms +N" columns below
// name those two hops. Retail offsets throughout; the last load is +0xbc, so the
// retail size is at least 0xc0. Members Create never reads are placeholders.

// CargoDesc -- one element of ShipInfo's cargo list (forward-declared in
// common_server.h for the OwnerList<CargoDesc> exports). Only the two dwords
// FUN_06d5f990 reads are known; retail's full size is not recovered, and
// nothing in the port allocates one.
struct CargoDesc {
    unsigned int GoodId;   // +0x00  GoodList::find_by_id (.text:06d5faa4), Good2Arch (.text:06d5fc9d)
    // FILD qword with a zeroed high dword (.text:06d5fad6 .. 06d5fae5), so the
    // division reads it unsigned; set_count takes it raw (.text:06d5fcb5).
    unsigned int Count;    // +0x04
};

struct ShipInfo {
    // FUN_06d43820 (Server_ControllerExists, IServerImpl.cpp) is called with &info
    // (.text:06cf7f60) and dereferences it as the controller's FLID; the same
    // dword is stored into the new controller-map entry's +0x10 (.text:06cf7fe1).
    unsigned int ControllerId;       // +0x00
    unsigned int SystemID;           // +0x04  FUN_06d00280 (.text:06cf7cfe)
    unsigned int ShipArchID;         // +0x08  request +0x08 (.text:06cf7e08), Archetype::GetShip (.text:06cf7eca)
    Vector Position;                 // +0x0c  request +0x4c (.text:06cf7e0b)            -> Parms +0x10
    Vector AngularVelocity;          // +0x18  request +0xc4 (.text:06cf7e9d)            -> Parms +0x4c
    Vector Velocity;                 // +0x24  request +0xb8 (.text:06cf7e84)            -> Parms +0x40
    // I3DMathEngine::MatrixToQuaternion on &info+0x30 (.text:06cf7e4b / 06cf7e55),
    // quaternion -> request +0x58; FUN_06d0a880 turns it back into a matrix
    // (QuaternionToMatrix, .text:06d0aaed) and copies 9 dwords -> Parms +0x1c.
    Matrix Orientation;              // +0x30 .. +0x53
    unsigned int Field_0x54;         // +0x54  request +0xb0 (.text:06cf7e01) -> Parms +0xec (CShip+0x21c); TODO: semantic name unknown
    unsigned int LoadoutID;          // +0x58  Loadout::Get (.text:06cf7f29), request +0x6c (.text:06cf7ec3)
    // FUN_06d5f990's third argument is &info+0x5c (.text:06cf7f4c). It reads
    // the dword at +4 and walks `node = *node` until NULL (.text:06d5f9ae ..
    // .text:06d5f9c5), and each node's +8 is a pointer to {GoodId, Count}:
    // MetaNode<CargoDesc> {Next, Prev, Data}, i.e. the OwnerList<CargoDesc>
    // (vptr +0x5c, Head +0x60, Tail +0x64, bUseHeap +0x68, FreeList +0x6c)
    // whose members Server.dll exports. sizeof 0x14 fills +0x5c..+0x6f exactly.
    OwnerList<CargoDesc> Cargo;      // +0x5c
    // pub::Costume (13 dwords), copied field by field and then a counted loop:
    unsigned int CostumeHead;        // +0x70  request +0x14 (.text:06cf7d8c)       -> Parms +0x64
    unsigned int CostumeBody;        // +0x74  request +0x18 (.text:06cf7d89)       -> Parms +0x68
    unsigned int CostumeLeftHand;    // +0x78  request +0x1c (.text:06cf7d9d)       -> Parms +0x6c
    unsigned int CostumeRightHand;   // +0x7c  request +0x20 (.text:06cf7d96)       -> Parms +0x70
    unsigned int CostumeAccessory[8];// +0x80  request +0x24, `count` dwords (.text:06cf7dd0) -> Parms +0x74
    int CostumeAccessoryCount;       // +0xa0  request +0x44 (.text:06cf7da4)       -> Parms +0x94
    int VibeGroupId;                 // +0xa4  request +0x10 (.text:06cf7ded)       -> Parms +0x60
    unsigned int VoiceId;            // +0xa8  request +0x48 (.text:06cf7de1)       -> Parms +0x98
    unsigned int DockingBaseId;      // +0xac  request +0x0c (.text:06cf7eb6)       -> Parms +0x5c
    // An INTEGER: FUN_06d0a880 FILDs the request copy (.text:06d0aa6d) into
    // Parms +0x0c HitPts.
    int HitPts;                      // +0xb0  request +0x68 (.text:06cf7ed4)
    unsigned int Field_0xb4;         // +0xb4  not read by Create
    // request +0xe8 (.text:06cf7ee1); FUN_06d0a880 tests it `& 0x3fffffff` after
    // the ship is built (.text:06d0ad30 / .text:06d0ad36).
    unsigned int Field_0xb8;         // +0xb8  TODO: semantic name unknown
    unsigned int ReputationRank;     // +0xbc  request +0xb4 (.text:06cf7de7)       -> Parms +0xf0 (CShip::CreateParms::Rank)
};

// SolarInfo -- pub::SpaceObj::CreateSolar's argument
// (?CreateSolar@SpaceObj@pub@@YAHAAIABUSolarInfo@12@@Z, .text:06cf80d0).
//
// Placed from the loads in pub::SpaceObj::CreateSolar (ESI = &info, .text:06cf80ed)
// into its CSolarCreateInfo request (server_createinfo.h) and from there by
// FUN_06d0a200 into CSolar::CreateParms (FUN_06d0a770 ctor at ESP+0x28). The
// earlier reconstruction split the matrix into two runs (+0x0c and +0x40) and
// read +0x88 as a char pointer; the listing shows a Vector at +0x0c, one 3x3 at
// +0x18 and an inline string at +0x88. Retail offsets; the last load is +0xcc.
struct SolarInfo {
    unsigned int Field_0x00;         // +0x00  not read by CreateSolar
    unsigned int SolarArchID;        // +0x04  request +0x08 (.text:06cf818c), Archetype::GetSolar (.text:06cf8254)
    unsigned int SystemID;           // +0x08  FUN_06d00280 (.text:06cf80f4)
    Vector Position;                 // +0x0c  request +0x4c (.text:06cf8200)       -> Parms +0x10
    // MatrixToQuaternion on &info+0x18 (.text:06cf8234 / 06cf823e) -> request
    // +0x58; FUN_06d0a200 QuaternionToMatrix (.text:06d0a3b0) -> Parms +0x1c.
    Matrix Orientation;              // +0x18 .. +0x3b
    unsigned int LoadoutID;          // +0x3c  request +0x6c (.text:06cf8263); Loadout::Get in FUN_06d0a200 (.text:06d0a455)
    unsigned int CostumeHead;        // +0x40  request +0x14 (.text:06cf81b5)       -> Parms +0x64
    unsigned int CostumeBody;        // +0x44  request +0x18 (.text:06cf81ae)       -> Parms +0x68
    unsigned int CostumeLeftHand;    // +0x48  request +0x1c (.text:06cf81c2)       -> Parms +0x6c
    unsigned int CostumeRightHand;   // +0x4c  request +0x20 (.text:06cf81bb)       -> Parms +0x70
    unsigned int CostumeAccessory[8];// +0x50  request +0x24, `count` dwords (.text:06cf81e8) -> Parms +0x74
    int CostumeAccessoryCount;       // +0x70  request +0x44 (.text:06cf81c9)       -> Parms +0x94
    int VibeGroupId;                 // +0x74  request +0x10 (.text:06cf81fd)       -> Parms +0x60
    unsigned int VoiceId;            // +0x78  request +0x48 (.text:06cf81fa)       -> Parms +0x98
    unsigned int DockingBaseId;      // +0x7c  request +0x0c (.text:06cf8189)       -> Parms +0x5c and +0xe0
    unsigned char Field_0x80;        // +0x80  byte, request +0xb4 (.text:06cf8294) -> Parms +0xbc (VisitValue)
    unsigned char Pad81[3];          // +0x81
    // An INTEGER: FUN_06d0a200 FILDs the request copy (.text:06d0a352).
    int HitPts;                      // +0x84  request +0x68 (.text:06cf819a)
    // Inline, not a pointer: CreateSolar tests the first byte (.text:06cf815a,
    // empty -> -4) and passes &info+0x88 to CreateID (.text:06cf8179). Nothing
    // in either function bounds it; 0x44 is the gap up to the next read.
    char Nickname[0x44];             // +0x88 .. +0xcb
    // request +0xbc (.text:06cf828e); FUN_06d0a200 calls FUN_06cef3b0(value, 1)
    // when `value & 0x3fffffff` (.text:06d0a53d .. 06d0a54f).
    unsigned int Field_0xcc;         // +0xcc  TODO: semantic name unknown
};

// LootInfo — passed to pub::SpaceObj::CreateLoot (.text:06cf84a0), which copies it
// field by field into one 0x6c-byte FLootRecord (server_gameobject.h) and hands that to
// StarSystem::create_loot (FUN_06d08a40) in mode 4. The names follow the record slot each
// field lands in (.text:06cf8503-06cf85c6); highest offset read is +0x64.
struct LootInfo {
    unsigned int SystemID;          // +0x00  FUN_06d00280 system lookup (missing -> -4)
    unsigned int EquipArchID;       // +0x04  Archetype::GetEquipment -> record +0x0c (contents)
    unsigned int NameId;            // +0x08  -> record +0x1c (CLoot NameId)
    unsigned int OwnerObjId;        // +0x0C  create_loot's SourceObjId (CLoot OwnerId)
    Vector Velocity;                // +0x10  -> record +0x20
    Vector Position;                // +0x1C  -> record +0x2c
    Matrix Orientation;             // +0x28  -> record +0x38 (REP MOVSD ECX=9)
    Vector AngularVelocity;         // +0x4C  -> record +0x5c
    unsigned int Units;             // +0x58  -> record +0x14
    float HitPtsScale;              // +0x5C  times the loot appearance's hit points -> record +0x08
    unsigned char bCanAiTractor;    // +0x60  -> record +0x68
    unsigned char bMission;         // +0x61  -> record +0x69
    unsigned char Pad62[2];         // +0x62
    unsigned int MarkForPlayer;     // +0x64  -> record +0x18 (mark_for_player when non-zero)
};
static_assert(sizeof(LootInfo) == 0x68, "LootInfo: last field read at +0x64");

// DestroyType and EQUIPMENT_RTYPE are GLOBAL enums -- declared above, before
// namespace pub.

// EQInfo — equipment activation descriptor passed to ActivateEquipment.
// Offsets confirmed against pub::SpaceObj::ActivateEquipment (0x193f0) and
// pub::SpaceObj::Dock (0x1a6e0), which between them read +0x00, +0x04 (a single BYTE,
// the on/off flag Dock derives from DOCK_HOST_RESPONSE), +0x08, +0x0c and +0x10.
// The struct is 0x20 bytes: +0x10 is an EquipDescVector-sized block (see CargoOut).
struct EQInfo {
    unsigned int Type;              // +0x00: 0=shield/scanner, 1=countermeasure, 2=mine, 3=lights
    unsigned short EquipId;         // +0x04: low byte is the on/off flag Dock writes
    unsigned short Pad06;           // +0x06: alignment
    unsigned int Count;             // +0x08: case 0 requires this to be exactly 1
    unsigned int Field_0x0c;        // +0x0c: case 1 gate — the branch is taken when != 0
                                    //        TODO: semantic name unknown
    // +0x10: case 1 hands &CargoOut to the object's IObjInspect::scan_cargo (+0x120,
    // `ADD ESI,0x10; PUSH ESI` at .text:06cf9490) as its EquipDescVector& out-parameter,
    // so the field is 0x10 bytes (sizeof(EquipDescVector)), not the one dword declared
    // here before. It is raw storage rather than an EquipDescVector member because
    // retail EQInfo is trivial: pub::SpaceObj::Dock builds one on its stack with no
    // constructor or destructor call (.text:06cfa755 .. 06cfa77b).
    unsigned int CargoOut[4];       // +0x10 .. +0x1f
};
static_assert(sizeof(EQInfo) == 0x20, "EQInfo: +0x10 carries a 0x10-byte EquipDescVector");

// pub::SpaceObj::ActivateEquipment — Server.dll .text:06cf93f0. Body: server_pub.cpp.
// Content.dll FUN_06f14780 at 06f148c0: Type=2, byte at +4 = 0.
SERVER_API EQUIPMENT_RTYPE __cdecl ActivateEquipment(unsigned int const& ObjId, EQInfo* Info);

// TerminalInfo — output struct for GetTerminalInfo
// Retail: 0x24 bytes, name at +0x00 (32 chars), value at +0x20
struct TerminalInfo {
    char Name[0x20];                // +0x00: terminal name string (max 31 chars + null)
    unsigned int Value;             // +0x20: terminal value dword
};

// ?GetTerminalInfo@SpaceObj@pub@@YAHABIHAAUTerminalInfo@12@@Z -- Server.dll 0x19900, body
// server_pub.cpp. Content.dll TU53 dock queues (FUN_06f6bdd0, FUN_06f6c580, FUN_06f6c920,
// FUN_06f6e9d0, FUN_06f6ee40): Value is the docking sphere's type.
SERVER_API int __cdecl GetTerminalInfo(const unsigned int& ObjId, int Index, TerminalInfo& OutInfo);

// PosSelectionType — position selection algorithm for GetEmptyPos
// Retail: ?GetEmptyPos@SpaceObj@pub@@YAHABIABVTransform@@ABM2ABW4PosSelectionType@12@AAVVector@@@Z
enum PosSelectionType {
    POS_SEL_XY_PLANE = 0,
    POS_SEL_XZ_PLANE = 1,
    POS_SEL_YZ_PLANE = 2,
    POS_SEL_3D_VOLUME = 3,
};

// ?GetEmptyPos@SpaceObj@pub@@YAHABIABVTransform@@ABM2ABW4PosSelectionType@12@AAVVector@@@Z --
// Server.dll .text:06cf9bd0, body server_pub.cpp:4005. Content.dll TU38 FUN_06f15e20 (06f16455),
// FUN_06f169a0 (06f16b52).
SERVER_API int __cdecl GetEmptyPos(const unsigned int& system_id_ref, const Transform& base_transform,
                                   const float& start_radius, const float& clearance,
                                   const PosSelectionType& selection_type, Vector& out_pos);

// ?IsPosEmpty@SpaceObj@pub@@YAHABIABVVector@@ABMAA_N@Z -- Server.dll .text:06cfa060, body
// server_pub.cpp:4147. Content.dll TU38 FUN_06f15e20 (06f1641f), FUN_06f169a0 (06f16b1a).
SERVER_API int __cdecl IsPosEmpty(const unsigned int& system_id_ref, const Vector& pos,
                                  const float& radius, bool& out_empty);

// ?Create@SpaceObj@pub@@YAHAAIABUShipInfo@12@@Z — Server.dll .text:06cf7c90. Body:
// server_pub.cpp. Content.dll NPC ship ctor FUN_06f336a0 (06f33aaf): OutId = the ship's +4.
SERVER_API int __cdecl Create(unsigned int& OutId, ShipInfo const& Info);

// ?CreateSolar@SpaceObj@pub@@YAHAAIABUSolarInfo@12@@Z -- Server.dll .text:06cf80d0, body
// server_pub.cpp:3482. Content.dll TU38 FUN_06f169a0 (06f16d2b).
SERVER_API int __cdecl CreateSolar(unsigned int& OutId, SolarInfo const& Info);

// ?CreateLoot@SpaceObj@pub@@YAHAAIABULootInfo@12@@Z -- Server.dll .text:06cf84a0, body
// server_pub.cpp:2536. Content.dll TU38 FUN_06f16fa0 (06f170b4 / 06f170c8).
SERVER_API int __cdecl CreateLoot(unsigned int& OutId, LootInfo const& Info);

// ?GetHealth@SpaceObj@pub@@YAHABIAAM1@Z -- body server_pub.cpp:3397. Content.dll TU38 snapshot
// FUN_06f130b0 (06f134b3, 06f137b1, 06f139c3, 06f13aa8, 06f13d27): the current hit points.
SERVER_API int __cdecl GetHealth(const unsigned int& obj_id_ref, float& out_current, float& out_max);

// ?Destroy@SpaceObj@pub@@YAHIW4DestroyType@@@Z — body server_pub.cpp. Content.dll
// FUN_06f34670 (06f34676, DESTROY_NORMAL).
SERVER_API int __cdecl Destroy(unsigned int ObjId, DestroyType Type);

} // namespace SpaceObj
} // namespace pub
