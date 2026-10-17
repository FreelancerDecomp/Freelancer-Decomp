// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <list>
#include <set>

#include "common_universe.h"
#include "common_cobjdb.h"
#include "common_metalist.h"
#include "server_exports.h"
// CRemotePhysicsSimulation. The tracked record embeds one BY VALUE, so the
// definition has to be visible here and not only in the .cpp -- see the
// tracker comment below for the `LEA ECX,[ESI + 0x8]` that settles it.
#include "common_server.h"

// server_gameobject.h -- what retail stores in the lists below. NOT SERVER_API:
// retail Server.dll exports no GameObject symbol at all (EXE/Server.dll's export
// table has no ?...@GameObject@@ name), and a dllexport on this forward
// declaration made the whole class dllexport -- which exported its implicit copy
// constructor, operator= and all three ??_7 tables (5 names retail lacks).
class GameObject;
class EqObj;   // server_gameobject.h (StarSystem::tractor_loot)
class Loot;    // server_gameobject.h; `::Loot` inside StarSystem, whose +0x48 block is named Loot
class Solar;   // server_gameobject.h (StarSystem::create_solar)
class Ship;    // server_gameobject.h (StarSystem::create_ship)
struct FShipCreateRequest; // server_createinfo.h (StarSystem::create_ship)

// common_misc.h's DamageList. Only ever used through a pointer here
// (StarSystem_DamageObject below), so a declaration is enough and this header
// stays cheap to include.
struct DamageList;

// server_gameobject.h -- StarSystem's asteroid factories hand these back, and
// the explosion takes its record by reference. Pointer/reference only here.
namespace SrvAsteroid {
class MedAsteroid;
class MedMine;
}
struct ExplosionDamageEvent;
struct FLootRecord;   // server_gameobject.h, the 0x6c-byte loot element
struct FServerLaunchRecord;  // server_gameobject.h, the ServerEquip launch record (0x54)
struct FServerBulletRecord;  // server_gameobject.h, SrvGun::launch_bullets' record (0x48)
struct FCommonUpdateObjectInfo;  // rpc_iclient_args.h, the IClient SendCommonUpdateObject argument (0x2c)

// The record StarSystem::send_mine_explosion (FUN_06d09270) fans out and
// SystemPlayerView_SendMineExplosion (FUN_06cf6df0) hands to IClient +0xd0
// (FLPACKET_SERVER_EXPLODEASTEROIDMINE, whose payload is these four dwords).
// Built by MedMine::update (.text:06d04f3a .. 06d04f80): the mine's own id
// (vt +0x20) at +0, then Direction scaled by ExplosionOffset at +4 .. +0xc.
struct FMineExplosion {
    unsigned int MineId;              // +0x00  FUN_06cf6df0 resolves it (.text:06cf6df9)
    Vector Offset;                    // +0x04
};
static_assert(sizeof(FMineExplosion) == 0x10, "FMineExplosion is four dwords");

// The record GameObject::on_collision (+0x1a4, .text:06ceeab3 .. 06ceeae6) builds
// on its stack and StarSystem::collision_damage (FUN_06d0d730) reads: the
// collision source (this object), the sub-object it was hit on, the partner and
// the partner's sub-object, and Collision::CalcCollisionDamage's result.
struct FCollisionDamage {
    unsigned int SourceId;            // +0x00  .text:06ceeab9  get_id()
    unsigned int SourceSubObjId;      // +0x04  .text:06ceeac0  MOVZX of the event's word
    unsigned int TargetId;            // +0x08  .text:06ceead0  partner get_id()
    unsigned short TargetSubObjId;    // +0x0c  .text:06ceead8
    unsigned short Pad0e;
    float Damage;                     // +0x10  .text:06ceeae2
};
static_assert(sizeof(FCollisionDamage) == 0x14, "FCollisionDamage is the 0x14-byte collision record");

// rpc_iclient_args.h -- the IClient +0x15c (BURNFUSE) argument EqObj::light_fuse /
// unlight_fuse build and StarSystem::broadcast_fuse fans out.
struct FServerBurnFuseInfo;

// server_createinfo.h's CREATESHIP record, the first argument of
// SystemPlayerView_CreateShipForPlayer below. Pointer-only here as well.
struct CShipCreateInfo;
struct Costume;           // common_reputation.h, 13 dwords (FLPACKET_SERVER_SENDCOMM)
struct CSolarCreateInfo;  // server_createinfo.h, CREATESOLAR's record (FUN_06cf66f0)
struct FServerCreateLootRequest;  // server_createinfo.h, CREATELOOT's 0x14-byte record

// ---------------------------------------------------------------------------
// ObjectList -- the type of StarSystem's ten dispatch blocks (+0x34 .. +0xFB).
//
// Retail's RTTI names it: the vtable the StarSystem constructor stamps on every
// block, .rdata:06d67ce4 (one slot, FUN_06d06e60, the scalar deleting
// destructor), has its COL at .rdata:06d6cfc8 whose type descriptor
// .data:06d88b1c is ".?AUObjectList@@"; its base is MetaList<GameObject>
// (vtable .rdata:06d67d00 -> FUN_06d0f8f0, type descriptor .data:06d88af4
// ".?AV?$MetaList@UGameObject@@@@"). So a block is exactly Common's
// MetaList<T> (src/include/common_metalist.h): vptr +0x00, Head +0x04, Tail
// +0x08, bUseHeap byte +0x0C, FreeList +0x10, nodes {Next, Prev, Data} of 0xC
// bytes, NULL-terminated -- .text:06d08794 `MOV ESI,[EBP+0x38]` for the head,
// `MOV ESI,[ESI]` to advance, `TEST ESI,ESI` to stop.
//
// node->Data IS the IObjRW wrapper: .text:06d0879f loads it straight into ECX
// and calls vtable slots +0x20/+0x158/+0x160 on it, and .text:06d072fe deletes
// it virtually through its CBase sub-object at +0x0c.
//
// These are NOT the same nodes as the player list at StarSystem+0x0c. That list
// is circular through a self-referencing sentinel and its element is EMBEDDED at
// node+0x08 -- .text:06d0876c is `LEA ECX,[ESI + 0x8]`, an address computation.
// Do not unify them.
//
// The two constructors differ in ONE byte, and it matters:
//   default  -- MetaList(true): the StarSystem constructor (.text:06d06ce0)
//               writes bUseHeap = 1 inline for +0x34/+0x48/+0x5c/+0x70
//               (.text:06d06d63 .. 06d06d96), through FUN_06d0f660(block, 1)
//               for +0x84 .. +0xd4 (.text:06d06da3 .. 06d06e04) and through
//               FUN_06d06e40 for +0xe8 (.text:06d06e4d `MOV byte [EAX+0xc],1`).
//   copy     -- MetaList(false), and NOTHING is copied: the StarSystem copy
//               constructor (.text:06d10520) builds +0x34 .. +0xd4 with
//               FUN_06d0f660(block, 0) (`PUSH 0x0` at .text:06d1063a ..
//               06d10701) and +0xe8 with FUN_06d10760, which ignores its
//               source argument and stores bUseHeap = 0 (.text:06d1076d,
//               `RET 0x4`). The pair constructor FUN_06d11330 does the same.
// SysDB's build loop (FUN_06d0fb40) default-constructs a temporary and inserts
// a COPY of it, so every StarSystem that lives in SysDB::SysMap has
// bUseHeap = 0 on all ten blocks: nodes are deleted, never pooled.
//
// There is no destructor body of ObjectList's own: FUN_06d06e60 runs
// FUN_06d112d0, which re-stamps MetaList<GameObject>'s vtable and does
// MetaList::free() then free_heap() -- MetaList<T>'s own destructor.
// ---------------------------------------------------------------------------
struct ObjectList : public MetaList<GameObject> {
    ObjectList() : MetaList<GameObject>(true) {}
    ObjectList(ObjectList const& Other) : MetaList<GameObject>(false) { (void)Other; }

    // StarSystem reads and writes the list's fields directly throughout
    // (e.g. .text:06d08794 `[EBP+0x38]`, .text:06d0bfe4 `[EBX+0xd8]`).
    using MetaList<GameObject>::Head;
    using MetaList<GameObject>::Tail;
    using MetaList<GameObject>::bUseHeap;
    using MetaList<GameObject>::FreeList;
    using MetaList<GameObject>::free_node;

    // FUN_06cf4f00 (.text:06cf4f00, `RET 0x4`): the first node whose object's
    // IObjInspect::get_id (vtable +0x20, .text:06cf4f24) equals Id, or null.
    MetaNode<GameObject>* find_by_id(unsigned int Id) const;

    // FUN_06cf5130 (.text:06cf5130, `RET 0x4`): how many nodes' objects answer
    // a non-zero `Mask & get_type_bits()` (vtable +0x154, .text:06cf5154 /
    // .text:06cf515a `TEST EBX,EAX`). Unguarded: node->Data is dereferenced
    // straight away (.text:06cf514f).
    unsigned int count_type(unsigned int Mask) const;

    // FUN_06cf5020 (.text:06cf5020, `RET 0x14`; the name is the port's): up to
    // MaxCount CSimple ids (+0xb0) into OutIds of the objects in this block whose
    // CSimple (& 3) TypeBits (+0xe0) meet TypeMask -- not destroyed (vt +0x160)
    // when the mask has 0x7f0000 -- and that lie within Radius of *Pos (squared
    // distance <= Radius * Radius as a float); a negative Radius takes every
    // match. Returns the count. Callers: StarSystem::scan_objects (.text:06d0bd15
    // ships, .text:06d0be2c solars).
    unsigned int collect_in_sphere(unsigned int* OutIds, unsigned int MaxCount, Vector const* Pos,
                                   float Radius, unsigned int TypeMask) const;

    ObjectList& operator=(ObjectList const&) = delete;
};

static_assert(sizeof(ObjectList) == 0x14, "ObjectList is retail's 0x14-byte MetaList");

// The node of an ObjectList. Kept as a name because the per-block walks all
// over Server.dll spell it this way.
typedef MetaNode<GameObject> FSystemListNode;

// ---------------------------------------------------------------------------
// The per-player object tracker that hangs off StarSystem+0x08.
//
// StarSystem+0x08 is the std::list OBJECT itself, in the MSVC6 shape:
// allocator byte at +0x08, _Head at +0x0C, _Size at +0x10.
// StarSystem::add_player proves it -- .text:06d07e79 is `LEA ECX,[EDI + 0x8]`,
// the list's own `this`, and .text:06d07e76 pushes `[EDI + 0xc]`, the
// sentinel, as the insert position.
//
// The list's ELEMENT IS EMBEDDED at node+0x08: .text:06d0876c, .text:06d0ecf9,
// .text:06d0b24a and .text:06d08dda are all `LEA ECX,[ESI+8]`, address
// computations, not loads. The element is a small polymorphic object --
// StarSystem::remove_player disposes of it through `(**(code**)node[2])(0)`,
// a virtual call on node+0x08 -- and it owns a SECOND list, of every object
// this player currently knows about.
//
// The element's constructor is FUN_06cf51e0 (.text:06cf51e0, `RET 0x4`, one
// argument: the session id) and its destructor FUN_06cf52d0; slot 0 of its
// vtable (.rdata:PTR_FUN_06d66988 -> .text:06cf52b0) is the scalar deleting
// destructor. Layout, read off that constructor and off FUN_06cf6820 /
// FUN_06cf73d0 / FUN_06cf7260 / FUN_06cf6950:
//   +0x00  vptr                                        (.text:06cf5294)
//   +0x04  the constructor never touches it
//   +0x08  std::list<SystemTrackedObject*>: allocator byte (.text:06cf5205),
//   +0x0C    _Head -- operator new(0xc), self-linked (.text:06cf5208..06cf5214)
//   +0x10    _Size, zeroed                             (.text:06cf5217)
//   +0x14  an MSVC6 _Tree (std::set/std::map): allocator bytes at +0x14/+0x15
//          (.text:06cf5228 / .text:06cf522b), _Nil = operator new(0x14) at
//          +0x1C (.text:06cf5231..06cf524f), _Head = operator new(0x14) at
//          +0x18 with _Left = _Right = itself and _Parent = _Nil
//          (.text:06cf5257..06cf5275), a byte at +0x20, _Size at +0x24, a byte
//          at +0x28. CORRECTED 2026-09-17 (wave 13): FUN_06cf6950 does NOT run
//          a lower_bound over it -- it runs MSVC6's `_Tree::insert(const
//          value_type&)` descent, `_Ans = comp(_V, _Key(_X))` with `_Y`
//          trailing (.text:06cf69f4 .. .text:06cf6a25), followed by the
//          uniqueness check at .text:06cf6a4b .. .text:06cf6a8a and a call into
//          `_Insert` (FUN_06cf7770). It is a std::set<unsigned int> of object
//          ids; see the member's own comment below.
//   +0x30  two dwords, zeroed by the constructor and handed to the record's
//   +0x34    CRemotePhysicsSimulation, slot 1 (.text:06cf7223 .. .text:06cf7237)
//   +0x38  the player (session) id -- the constructor's only argument
//          (.text:06cf5284; read back at .text:06cf688b / .text:06cf7400 /
//          .text:06cf72f0, and as node+0x40 in StarSystem::remove_player)
//   +0x3C  Vector -- the player's own position, subtracted componentwise at
//          .text:06cf72a9 / .text:06cf72b4 / .text:06cf72ba
// Total 0x48 bytes, so a node is 0x08 + 0x48 = 0x50.
//
// CAUTION on the allocator bytes at +0x08/+0x14/+0x15. Retail's constructor
// copies each one from an uninitialised `allocator()` temporary that the
// compiler happens to overlap with the incoming argument slot, so the listing
// reads `MOV AL,[ESP+0x14]` / `MOV [ESI+8],AL` against the SAME stack slot
// that later feeds the player id at +0x38 (.text:06cf51f6, .text:06cf521a and
// .text:06cf525c -- Ghidra calls all three `param_1`). They are NOT a copy of
// the player id; they are MSVC6's 1-byte std::allocator member and carry no
// value at all.
//
// The tracked RECORD is a separate heap block of 0x2F8 bytes, allocated by
// FUN_06cf7140 (.text:06cf715e `PUSH 0x2f8`, .text:06cf7167 operator new):
//   +0x000  GameObject*            (.text:06cf7189 zeroes it, .text:06cf71a6
//           stores the real one)
//   +0x004  never written
//   +0x008  CRemotePhysicsSimulation BY VALUE. .text:06cf7181 is
//           `LEA ECX,[ESI + 0x8]` followed by a direct CALL to
//           COMMON.DLL::CRemotePhysicsSimulation::CRemotePhysicsSimulation,
//           and FUN_06cf6940 (`ADD ECX,0x8` then a tail jump into the
//           destructor) is called with ECX = the RECORD, never the element.
//           0x2E8 bytes, per the vector-deleting destructor at .text:06ce2ffa,
//           and 0x08 + 0x2E8 = 0x2F0 = exactly the flag offset below.
//   +0x2F0  "the client has been told this object exists". Seeded to
//           COMMON.DLL::SinglePlayer() (.text:06cf71a8 / .text:06cf71ae), so
//           in single player every record starts already-created.
// ---------------------------------------------------------------------------

// One tracked object, the Data of a node in the element's inner list.
struct SystemTrackedObject {
    // +0x00 -- the object being tracked. .text:06cf6863 loads it and calls the
    // wrapper's get_id through vtable +0x20; .text:06cf7413 compares it against
    // the candidate object directly.
    GameObject* Object;
    // +0x04 -- retail never writes it. It is the alignment gap that the
    // simulation's 8-byte alignment forces. Placeholder.
    unsigned int Field_0x04;
    // +0x08 -- the network-physics interpolator, BY VALUE (0x2E8 bytes).
    CRemotePhysicsSimulation Simulation;
    // +0x2F0 -- FUN_06cf73d0 returns it verbatim as the visibility answer
    // (.text:06cf742a), and FUN_06cf6820 only sends the destroy notification
    // when it is set (.text:06cf68a0).
    bool bCreatedOnClient;
};

struct SystemTrackedNode {
    SystemTrackedNode* Next;          // +0x00
    SystemTrackedNode* Prev;          // +0x04
    SystemTrackedObject* Data;        // +0x08  a real pointer (.text:06cf6860 MOV)
};

// +0x08 .. +0x13 of Observer: retail's MSVC6 std::list<SystemTrackedObject*>
// {allocator byte +0, _Head +4, _Size +8}. Its three special members are the
// parts of Observer's constructor / copy constructor / destructor that belong to
// this member, and they run where member construction and destruction put them:
//   ctor       .text:06cf5205 .. 06cf5217  allocator byte; operator new(0xc)
//              self-linked sentinel; size 0 -- before the _Tree is built
//   copy ctor  .text:06d0f705 .. 06d0f778  allocator byte; a fresh sentinel; one
//              new node per source node carrying the SAME record pointer
//              (FUN_06d06720 is a pointer copy); ++size each
//   dtor       .text:06cf538d .. 06cf53ca  FUN_06d0f620 erase over what is left
//              (nothing: ~Observer's body has emptied it), free the sentinel,
//              head = size = 0 -- after the _Tree teardown, as the member
//              declared first is destroyed last.
struct FObserverTrackedList {
    FObserverTrackedList();
    FObserverTrackedList(FObserverTrackedList const& Source);
    ~FObserverTrackedList();

    unsigned char Alloc;              // +0x00  MSVC6 std::allocator, no value
    unsigned char Pad0[3];
    SystemTrackedNode* Head;          // +0x04  self-referencing sentinel
    unsigned int Size;                // +0x08
};

// Observer -- retail's own name: RTTI TypeDescriptor .data:06d88858
// ".?AVObserver@@" (a `class`, CHD 06d6c888: one entry, no base class), source
// file E:\FL\Scratch\Source\Server\space\Observer.cpp. The per-player view
// element embedded at StarSystem player-list node+0x08.
//
// Polymorphic in retail, with a one-slot vftable .rdata:06d66988 (COL 06d6c898):
//   slot 0  FUN_06cf52b0  the compiler's scalar deleting destructor around
//                         ~Observer (FUN_06cf52d0): destroy, then free if flag&1.
// The table's extent is exactly one dword (06d66988..06d6698b; the next dword is
// 00000000 alignment ahead of the double DAT_06d66990). Three places stamp it --
// the constructor (.text:06cf5294), the copy constructor (.text:06d0f813) and the
// destructor (.text:06cf52f2) -- and three places call through it, every one a
// std::list erase that destroys the element in place with flag 0 and frees the
// node itself afterwards:
//   .text:06d0876c  LEA ECX,[ESI+0x8] / MOV EDX,[ECX] / PUSH 0 / CALL [EDX]
//                   (StarSystem::remove_player, FUN_06d08740)
//   .text:06d0f25a  (FUN_06d0f230, the player list's destructor)
//   .text:06d0f3bc  (FUN_06d0f390, the player list's erase(first, last))
// The port spells those three as `Element->~Observer()` through a pointer, which
// MSVC compiles to the same `PUSH 0 / CALL [vptr+0]`.
//
// The node is a raw heap block in both retail and this port (operator new
// 0x50 in retail), and the element is placement-constructed into it, exactly as
// MSVC6's std::list<Observer> constructs its value into _Buynode's storage.
class Observer {
public:
    // FUN_06cf51e0 (.text:06cf51e0, `RET 0x4`) -- builds the tracked-object
    // list's sentinel and the pending-damage set, zeroes the rest; the only value
    // that comes in is the session id.
    explicit Observer(unsigned int InPlayerId);

    // FUN_06d0f6d0 (.text:06d0f6d0) -- run in place on a raw node by the list's
    // insert (FUN_06d0f330, .text:06d0f365) and by StarSystem's copy constructor
    // (FUN_06d109c0, .text:06d10a08). The tracked records are shared with the
    // source, not duplicated (FUN_06d06720 copies the pointer); the
    // pending-damage set is copied.
    Observer(Observer const& Source);

    // FUN_06cf52d0 (.text:06cf52d0) -- destroys every tracked record (each
    // one's CRemotePhysicsSimulation first, .text:06cf5317), frees the inner
    // nodes, the pending-damage set and the sentinel. Virtual: slot 0.
    virtual ~Observer();

    // +0x00  vptr (.rdata:06d66988)
    unsigned int Field_0x04;          // +0x04  never written by either constructor
    FObserverTrackedList Tracked;     // +0x08 .. +0x13
    // +0x14 .. +0x27 -- retail's _Tree. DECODED 2026-09-17 (wave 13). The
    // wave-12 note here said the port "has no consumer for it yet (FUN_06cf6950
    // is its only reader)". Both halves were wrong: FUN_06cf6950 is its only
    // WRITER, and it has three readers.
    //
    // It is `std::set<unsigned int>`, and the key is an OBJECT ID. The layout
    // reads straight off the constructor (.text:06cf5228 .. .text:06cf5275):
    // allocator bytes at +0x14/+0x15, `_Nil` = operator new(0x14) at +0x1C with
    // _Color = _Black and _Isnil = 1, `_Head` = operator new(0x14) at +0x18 with
    // _Parent = _Nil and _Left = _Right = itself, `_Multi` at +0x20 and `_Size`
    // at +0x24. Node = {_Left +0, _Parent +4, _Right +8, _Value +0xC,
    // _Color +0x10, _Isnil +0x11} = 0x14 bytes, which is why every walk of it
    // compares against `node + 0x0C`.
    //
    // The same 0x14-byte shape appears at a base of its own in FUN_06cf7440,
    // which takes a SECOND set of the same type by pointer and reads it as
    // {_Head +4, _Nil +8, _Multi +0xC, _Size +0x10} -- exactly this block
    // rebased to +0x14. That is the independent confirmation.
    //
    // WHAT IT IS FOR. FUN_06cf6950 (.text:06cf6950) is the per-player
    // FLPACKET_SERVER_DAMAGEOBJECT gate. When the player cannot see the object
    // and nothing else forces the send, retail does not drop the update: it
    // remembers the object id here and lets the per-frame observer update
    // (FUN_06cf53e0) retry it. That retry is the reader:
    //   .text:06cf6031  the loop opens on `[EBP+0x18]`'s _Lmost and walks with
    //                   FUN_06ce5700 (operator++), resolving each id and keeping
    //                   the ones FUN_06cf7260 now calls visible;
    //   .text:06cf61dd  builds the damage record through the object's own
    //                   vtable +0x1a8 with mask 0x40000000;
    //   .text:06cf6208  CALL dword ptr [EDI + 0xfc]  -- IClient SendServerDamageObject;
    //   .text:06cf6222  8d4d14        LEA  ECX,[EBP + 0x14]
    //   .text:06cf6225  e8c6130000    CALL FUN_06cf75f0   ; set::erase(key)
    // and FUN_06cf6e60 (system switch-out) clears the whole set.
    // FUN_06cf52d0 destroys it.
    //
    // A real std::set, because retail has one: a member of Observer, built by
    // its constructors and torn down by its destructor. Do not memcpy an
    // Observer.
    std::set<unsigned int> PendingDamageIds;   // +0x14 .. +0x27
    // +0x28 -- CORRECTED 2026-09-17 (wave 12). This byte used to be labelled
    // `SetMulti`, i.e. the last member of the _Tree above. It is not: the _Tree
    // ends at +0x28, and this is a bool of Observer's own.
    //
    // Proof, on the raw bytes rather than on the constructor's zero-fill.
    // FUN_06cf6e60 (.text:06cf6e60, the "an object left this system" notifier)
    // takes the view in ECX:
    //   .text:06cf6e69  8be9      MOV  EBP,this          ; EBP = the view
    //   .text:06cf6e6b  8b450c    MOV  EAX,[EBP + 0xc]   ; ... its tracked list
    // and, after dropping the object from the tracked list, does
    //   .text:06cf6f66  8b4538    MOV  EAX,[EBP + 0x38]        ; this view's player id
    //   .text:06cf6f69  8b35....  MOV  ESI,[.data:Players]
    //   .text:06cf6f73  69c918040000  IMUL this,this,0x418
    //   .text:06cf6f79  3b5431cc  CMP  EDX,[this + ESI*1 + -0x34]  ; slot+0x3E4,
    //                                                 ; PlayerData::ship_object_id
    //   .text:06cf6f7d  7504      JNZ  LAB_06cf6f83
    //   .text:06cf6f7f  c6452801  MOV  byte ptr [EBP + 0x28],0x1
    // i.e. it is LATCHED when the object that just left IS this player's own
    // ship. MSVC never re-writes a live _Tree's `_Multi`, and turning a set into
    // a multiset halfway through would be meaningless; this is a state flag.
    //
    // Every one of the five "create this object on the client" notifiers gates
    // on it and returns silently when it is set -- .text:06cf665b
    // (ship), .text:06cf66f3 (solar), .text:06cf6bc5 (countermeasure),
    // .text:06cf6c55 (loot), .text:06cf6cf5 (guided), .text:06cf6d65 (mine) --
    // and .text:06cf6f9b reads it back inside FUN_06cf6e60 itself. So: "this
    // player no longer has a ship in this system; stop telling them about it".
    // The name is the port's, retail names nothing here.
    unsigned char bShipGone;          // +0x28
    unsigned char Pad3[3];
    unsigned int Field_0x2C;          // +0x2C  never written by the constructor
    // +0x30 .. +0x37 -- CORRECTED 2026-09-17 (wave 14). These were two
    // `unsigned int` placeholders called Field_0x30/Field_0x34. They are ONE
    // double, and the x87 opcodes say so outright. The per-frame observer
    // update (FUN_06cf53e0) opens by accumulating its own `double` argument
    // into it:
    //   .text:06cf5475  dd8424c8020000  FLD   double ptr [ESP + 0x2c8]  ; the arg
    //   .text:06cf547f  dc4530          FADD  double ptr [EBP + 0x30]
    //   .text:06cf5488  dd5d30          FSTP  double ptr [EBP + 0x30]
    // and reads it back narrowed to a float for the UPDATEOBJECT payload:
    //   .text:06cf5f14  dd4530          FLD   double ptr [EBP + 0x30]
    //   .text:06cf5f39  d99c248c000000  FSTP  float  ptr [ESP + 0x8c]
    // The "two dwords" reading came from .text:06cf7223/.text:06cf722a, where
    // SystemPlayerView_AddTrackedObject pushes the pair as two PUSHes -- which
    // is simply how a double is passed on the stack. Checklist class: direction
    // confused with type.
    //
    // It is the observer's own clock: the total time this view has been
    // running, stamped into every UPDATEOBJECT so the client can order samples.
    double ElapsedTime;               // +0x30 .. +0x37
    unsigned int PlayerId;            // +0x38
    Vector Position;                  // +0x3C
};

// The player-list node. The element really is embedded, so `View` must stay the
// third member and must not become a pointer.
struct SystemPlayerNode {
    SystemPlayerNode* Next;           // +0x00
    SystemPlayerNode* Prev;           // +0x04
    Observer View;                    // +0x08
};

// FUN_06cf7140 (.text:06cf7140, `RET 0x4`) -- start tracking an object for this
// player. Allocates the 0x2F8-byte record, constructs its
// CRemotePhysicsSimulation in place, seeds "created on the client" from
// SinglePlayer(), push_backs the record and bumps the size. Returns the record,
// exactly as retail does (EAX at .text:06cf723f).
SystemTrackedObject* SystemPlayerView_AddTrackedObject(Observer* View, GameObject* Obj);

// FUN_06cf7260 -- "this object is not tracked yet; should it become visible?"
// Reached only from FUN_06cf73d0's miss path (.text:06cf7420).
//   true  when the object is null, when COMMON.DLL::SinglePlayer(), or when the
//         object's own CSimple::OwnerPlayer is this view's player
//         (.text:06cf726a / .text:06cf7278 / .text:06cf729b)
//   otherwise a squared-distance test against one of three radii, chosen by
//   ownership and grouping:
//     object has no owning player            -> 2500^2 (.rdata:06d66aec = 6.25e6)
//     owner is in a different group, or this
//     player is in no group at all           -> 5000^2 (.rdata:06d66af0 = 2.5e7)
//     owner is in the SAME group             -> 10000^2 (.rdata:06d66af4 = 1e8)
//   The group value is PlayerData+0x404: retail indexes `Players + id*0x418`
//   and reads -0x14 off that (.text:06cf72f9/.text:06cf72ff and
//   .text:06cf731e/.text:06cf7324), and 0x418 - 0x14 = 0x404. It is only ever
//   tested for zero and compared between two players, which is exactly what
//   common_server_playerdb.h already names PlayerData::group_ptr, the
//   CPlayerGroup*.
bool SystemPlayerView_ShouldBeVisible(Observer* View, GameObject* Obj);

// FUN_06cf73d0 -- "can this player see this object right now?" `RET 0x4`.
bool SystemPlayerView_IsObjectVisible(Observer* View, GameObject* Obj);

// FUN_06cf7370 (.text:06cf7370, `RET 0x4`) -- the same question by object id:
// true in single player; otherwise the id is resolved (FUN_06d006c0, the port's
// Server_LookupGameObject) and the answer is FUN_06cf73d0's for the object, a
// null object included (.text:06cf73a4 pushes 0 on the miss path).
bool SystemPlayerView_IsObjectIdVisible(Observer* View, unsigned int ObjId);

// FUN_06cf65b0 (.text:06cf65b0, `RET 0x4`) -- multiplayer only: the tracked
// record for Rec->ObjectId gets FLPACKET_COMMON_UPDATEOBJECT (IClient +0xd8,
// SendCommonUpdateObject) when already created on the client (+0x2F0), and its
// CRemotePhysicsSimulation::RemoteUpdate(pos, orientation, time) either way.
// Only caller: StarSystem::sp_obj_update.
void SystemPlayerView_SendObjectUpdate(Observer* View, FCommonUpdateObjectInfo const* Rec);

// FUN_06cf70c0 (.text:06cf70c0, `RET 0x4`) -- if ObjId is a CShip this view can
// see, IClient +0x1c4 (SendServerMiscObjUpdateObjectGroup)(view's PlayerId, ObjId, the ship's +0x1b4 group
// id). Only caller: StarSystem_SendGroupChange.
void SystemPlayerView_SendGroupChange(Observer* View, unsigned int ObjId);

// FUN_06cf7440 (.text:06cf7440, `RET 0x4`) -- collect into Owners the owning
// player (CSimple::OwnerPlayer, +0xb4) of every tracked object this view has
// already created on its client (+0x2F0). Local chat's recipient list.
void SystemPlayerView_CollectCreatedOwners(Observer* View, std::set<unsigned int>* Owners);

// FUN_06cf6730 (.text:06cf6730, `RET 0x4`) -- FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE
// for one view. If Obj is tracked by this view and already created on its client
// (+0x2F0), the ship's CEqObj::get_equip_desc_list (CObject vtable +0xac) fills a
// local EquipDescVector and IClient +0x154 (SendServerRemoteObjectCargoUpdate) sends it with (&view->PlayerId,
// the ship's CSimple::Id, &vector, 1). The ship is the 0x503-classed cobject(); retail
// does not null-test it. Only caller: StarSystem_SendRemoteCargoUpdate.
void SystemPlayerView_SendRemoteCargoUpdate(Observer* View, GameObject* Obj);

// FUN_06cf6820 -- "this object has been destroyed". `RET 0x8`.
bool SystemPlayerView_OnObjectDestroyed(Observer* View, GameObject* Obj, int Kind);

// FUN_06cf6950 (.text:06cf6950, `RET 0xc`) -- the per-player
// FLPACKET_SERVER_DAMAGEOBJECT gate. Ported 2026-09-17 (wave 13).
//
// Retail's three stack arguments are (object id, a record the caller already
// built, a second record the caller built locally). Both records are MSVC6
// std::lists -- the only thing this function does with either is test the
// SECOND one's _Size at +0x08 (.text:06cf69e9 `MOV ECX,[EAX + 0x8]`) and hand
// whichever it picked to IClient SendServerDamageObject as a bare pointer. The sole caller,
// FUN_06d0b800 (.text:06d0bb43 .. .text:06d0bb58), passes its own `param_2`
// and a std::list it filled on the stack at [ESP+0x10].
//
// Both records are Common's DamageList (the caller's param_2 and its stack
// DamageList), and rpclocal's DAMAGEOBJECT marshaller reads the SendServerDamageObject pointer
// as that same type (rpc_payload_ch2.cpp, MarshalDamageObject: DamageList::operator=).
// The +0x08 _Size test reads DeferredRecord->Count (the port's mirror of it).
void SystemPlayerView_DamageObject(Observer* View, unsigned int ObjId,
                                   DamageList* PrimaryRecord,
                                   DamageList* DeferredRecord);

// FUN_06cf53e0 (.text:06cf53e0, `RET 0x8` -- ONE stack argument, a `double`).
// The per-frame observer update. Ported 2026-09-17 (wave 14) in
// src/Server/server_observer.cpp.
//
// Ghidra types the argument as two `undefined4`s; it is one double. The
// prologue leaves ESP at (entry - 0x2c4) and the very first thing the body does
// with it is
//   .text:06cf5475  dd8424c8020000  FLD   double ptr [ESP + 0x2c8]
// which is exactly the first stack argument, and `RET 0x8` at .text:06cf6314
// confirms the eight bytes.
//
// Everything below the top-level `if (SinglePlayer()) return;`
// (.text:06cf5405/.text:06cf540d) runs in five phases, each one re-checking the
// IClient bandwidth gauge (see the kBandwidthCeiling note in the .cpp):
//   1. split the tracked list in two -- already-created records get
//      CRemotePhysicsSimulation::Doit(Dt), not-yet-created ones are queued;
//   2. for every not-yet-created record that is now visible, send the matching
//      CREATE* packet and move it to the created list;
//   3. for every created record: if it is no longer visible send
//      DESTROYOBJECT and clear its flag, otherwise ask CheckForSync and, when
//      it says yes, insert the record into a third list kept in DESCENDING
//      CRemotePhysicsSimulation::GetSimulationTime() order;
//   4. drain that third list into UPDATEOBJECT packets;
//   5. retry the deferred-damage set (PendingDamageIds) -- resolve each id,
//      keep the ones that are visible again, build the object's DamageList and
//      send DAMAGEOBJECT, erasing the id from the set.
void SystemPlayerView_Update(Observer* View, double Dt);

// Observer.cpp's matrix -> quaternion step: the lazily filled file-static
// .data:DAT_06d8d7f8 (Server_Acquire3DMathEngine, FUN_06d5ff70) and its
// vtable +0x50, I3DMathEngine::MatrixToQuaternion. Its three retail users are
// .text:06cf5ce0 / .text:06cf5eea (the update above) and .text:06cf7204
// (SystemPlayerView_AddTrackedObject). Defined in src/Server/server_observer.cpp.
void Observer_MatrixToQuat(Quaternion& Out, Matrix const& In);

// FUN_06cf4f40 (.text:06cf4f40, thiscall on the block, RET 8) — walk one of
// StarSystem's ObjectLists, call GameObject::update(Dt) (vtable +0x16c) on each
// node's object, and when update returns false MetaList::remove the node and
// append its object to Deferred (always the +0xe8 list).
void StarSystem_TickObjectList(ObjectList* List, float Dt, ObjectList* Deferred);

// FUN_06cf6e60 (.text:06cf6e60, `RET 0x8`) -- "this object has left the
// system". Ported 2026-09-17 (wave 13). Drops the object from the tracked list,
// CLEARS the whole deferred-damage set, latches bShipGone when the departing
// object is this player's own ship, and sends
// FLPACKET_SERVER_SYSTEM_SWITCH_OUT (IClient +0x0e4 == SendServerSystemSwitchOut) when either the
// record had been created on the client or bShipGone is now set.
//
// The wire payload is two dwords built on the stack at .text:06cf6f89 /
// .text:06cf6f8f: `{ *param_1, *param_2 }`, i.e. the object id and the
// destination the caller points at. Retail passes both by pointer.
void SystemPlayerView_OnObjectLeftSystem(Observer* View,
                                         unsigned int const* ObjId,
                                         unsigned int const* Destination);

// FUN_06cf6fc0 (.text:06cf6fc0, `RET 0xc`) -- "this object has landed".
// Ported 2026-09-24. Drops the object from the tracked list, and when the
// record had been created on the client or the object is this player's own
// ship, sends FLPACKET_SERVER_LAND (IClient +0x0ec == SendServerLand) with the payload
// {*ObjId, *Arg2, *Arg3}; outside single player the player's own ship landing
// first autosaves the slot (FUN_06d4c430). Its only caller is the per-system
// step FUN_06d0be50 at .text:06d0c280, inside the deferred-object block
// .text:06d0c163 .. .text:06d0c3f3, ported 2026-09-24 in StarSystem_Update
// (IServerImpl.cpp), which walks the dock queue (SystemDockNode below).
// Arg2 is the caller's ESP+0x10 local (.text:06d0c273; zeroed at 06d0c20e and
// set from a vtable +0x20 get_id at 06d0c248) and Arg3 is EBX+0x18.
void SystemPlayerView_OnObjectLanded(Observer* View,
                                     unsigned int const* ObjId,
                                     unsigned int const* Arg2,
                                     unsigned int const* Arg3);

// ---------------------------------------------------------------------------
// The five "tell this client the object exists" notifiers, and the six of the
// seven SystemPlayerView_AddTrackedObject call sites that live inside them.
//
// Retail has no names for them; the FDUMP each one issues when it is handed an
// already-destroyed object does, and it is retail's own text, from
// E:\FL\Scratch\Source\Server\space\Observer.cpp (the path string lives at
// .rdata:06d669dc and is pushed at .text:06cf66d7 as `68dc69d606`) -- so this
// family is NOT in StarSys.cpp, which is where the system itself lives:
//   FUN_06cf6630  line 0x19b  "Ship(%d) 'd..."            -> IClient +0x0bc
//   FUN_06cf6bc0  line 0x23c  "CounterMeasure(%d) ..."    -> IClient +0x0cc
//   FUN_06cf6c50  line 0x256  "Loot(%d) 'dea..."          -> IClient +0x0c0
//   FUN_06cf6cf0  (none)                                  -> IClient +0x0c8
//   FUN_06cf6d60  line 0x28a  "Mine(%d) 'd..."            -> IClient +0x0c4
// and those five vtable offsets are, in rpc_iclient.h's own table,
// SendServerCreateShip, SendServerCreateCounter, SendServerCreateLoot,
// SendServerCreateGuided and SendServerCreateMine. The FDUMP text and the packet name agree independently in
// all four cases that have both, which is what fixes FUN_06cf6cf0 as the guided
// one without a string of its own.
//
// All five share one body:
//   1. return at once when View->bShipGone is set;
//   2. per-object: when GameObject::is_destroyed() (retail vtable +0x160), FDUMP
//      and return without tracking anything;
//   3. SystemPlayerView_AddTrackedObject(View, Obj);
//   4. outside single player, drop out silently unless
//      SystemPlayerView_ShouldBeVisible(View, Obj);
//   5. mark the record bCreatedOnClient and hand the payload to the IClient slot.
// The ship one (FUN_06cf6630) is the odd one out and does NOT follow 4/5 -- see
// its own comment in server_classes.cpp.
//
// The first argument is the record the packet carries. Retail builds it in the
// caller (FUN_06d07690 for ships, FUN_06d07b90 for loot, ...) and these five
// only forward it. All five record types are published in
// src/include/server_createinfo.h, the one declaration rpclocal's marshallers
// read too. The ship notifier takes its CShipCreateInfo by type, because it
// WRITES a field of it (+0xd4, .text:06cf66a0); the other four only forward
// theirs and still take it as `void*`.
// TODO(port): type the other four once their callers build the records.
void SystemPlayerView_CreateShipForPlayer(Observer* View, CShipCreateInfo* Info, GameObject* Obj);
// FUN_06cf66f0 (.text:06cf66f0, `RET 0x8`) -- CREATESOLAR for one view: nothing when
// bShipGone; otherwise IClient +0xb8 (SendServerCreateSolar)(PlayerId, Info) when the solar should
// be visible to this view or Info->Field_0xac is set. No tracked record is made.
// Only caller FUN_06d0a200 (pub::SpaceObj::CreateSolar's per-player walk).
void SystemPlayerView_CreateSolarForPlayer(Observer* View, CSolarCreateInfo* Info, GameObject* Obj);
// FUN_06cf6ac0 (.text:06cf6ac0, thiscall on the view, RET 0x54: 21 dwords) --
// FLPACKET_SERVER_SENDCOMM for one view. Outside single player both the sender and
// the receiver must be visible to the view (FUN_06d006c0 + FUN_06cf73d0 each); then
// IClient +0x110 (SendServerSendComm) with the view's PlayerId, the 20 caller dwords and
// "the receiver is this view's own ship". Only caller StarSystem::send_comm.
void SystemPlayerView_SendComm(Observer* View, unsigned int SenderId,
                               unsigned int ReceiverId, unsigned int VoiceId,
                               Costume const& Cost, unsigned int CommType,
                               unsigned int* TextIds, int TextCount, unsigned int Mood,
                               float Duration);
void SystemPlayerView_CreateCounterForPlayer(Observer* View, void* Payload, GameObject* Obj);
void SystemPlayerView_CreateMineForPlayer(Observer* View, void* Payload, GameObject* Obj);
// FUN_06cf6c50 / FUN_06cf6cf0 take a std::vector<GameObject*> rather than one
// object: .text:06cf6c60 is `MOV ESI,[EBX + 0x4]` against `CMP ESI,[EBX + 0x8]`,
// the vector's first/last pair. The loot one walks the whole range; the guided
// one uses only `*First` (.text:06cf6d13 `MOV EAX,[ESI]` after checking the
// range is non-empty at .text:06cf6d0a..06cf6d11). The port passes the range
// explicitly instead of a container so that neither the iterator-debug pointer
// nor a copy of the vector can creep in.
void SystemPlayerView_CreateLootForPlayer(Observer* View, FServerCreateLootRequest* Payload,
                                          GameObject* const* First, GameObject* const* Last);
void SystemPlayerView_CreateGuidedForPlayer(Observer* View, void* Payload,
                                            GameObject* const* First, GameObject* const* Last);

// FUN_06cf6df0 (.text:06cf6df0, thiscall on the view, RET 4) -- the per-player
// half of StarSystem::send_mine_explosion. In multiplayer the mine is resolved
// by id (FUN_06d006c0) and the packet goes out only when this player can see it
// (FUN_06cf73d0); in single player it always goes. IClient +0xd0 (SendServerExplodeAsteroidMine,
// FLPACKET_SERVER_EXPLODEASTEROIDMINE) with the view's player id and Rec.
void SystemPlayerView_SendMineExplosion(Observer* View, FMineExplosion* Rec);

// ---------------------------------------------------------------------------
// The element of StarSystem's second list (+0x14 .. +0x1F): one object that has
// finished docking with something this step and is waiting for the system's
// own update (FUN_06d0be50) to land it or hand it over.
//
// The shape comes from the three functions that touch the element:
//   FUN_06d0d370 (.text:06d0d370, the only producer) builds it on the stack at
//     ESP+0x14 .. ESP+0x28: two watchers zeroed at .text:06d0d390..06d0d3ac,
//     `*param_3` at +0x10 and the byte `*param_4` at +0x14 (.text:06d0d408 ..
//     06d0d41b);
//   FUN_06d0f830 (.text:06d0f830) copy-constructs it into the node at node+8:
//     each watcher re-watches its source's object (`[src] - 8 + 8`, i.e. the
//     same Watchable, .text:06d0f86e..06d0f890 and .text:06d0f8a6..06d0f8c1),
//     then the dword at +0x10 and the byte at +0x14 are copied;
//   FUN_06d0d4a0 (.text:06d0d4a0) destroys it: the watcher at +0x08 first
//     (.text:06d0d4be..06d0d4d8), then the one at +0x00 (.text:06d0d4da..
//     06d0d4eb) -- reverse declaration order, which is what a C++ destructor
//     does anyway.
// Both watchers watch the Watchable sub-object of a GameObject wrapper
// (FUN_06d0d370 stores `param + 8`; FUN_06d0be50 subtracts 8 back at
// .text:06d0c1f7 and .text:06d0c205).
//
// Names are the port's. Retail's element reads: +0x00 is the object that docks
// (the ship), +0x08 what it docked with (null when IServerImpl::Dock queues a
// forced landing), +0x10 the base id the LAND packet carries, and +0x14 the
// byte that selects "land" (1) over "dock without landing" (0: the
// moor_small/moor_medium/moor_large docking spheres, types 3/4/5 -- see
// pub::SpaceObj::InstantDock's docking-sphere test).
// ---------------------------------------------------------------------------
struct FSystemDockEntry {
    BaseWatcher Object;               // +0x00
    BaseWatcher Target;               // +0x08
    unsigned int BaseId;              // +0x10
    bool bLand;                       // +0x14

    FSystemDockEntry() : BaseId(0), bLand(false) {}
    // FUN_06d0f830. BaseWatcher's own implicit copy would copy the watcher
    // chain link raw; retail re-links each watcher instead.
    FSystemDockEntry(FSystemDockEntry const& Other)
        : BaseId(Other.BaseId), bLand(Other.bLand)
    {
        Object.set(Other.Object.target());
        Target.set(Other.Target.target());
    }
    FSystemDockEntry& operator=(FSystemDockEntry const&) = delete;

    // The watched Watchable back to its wrapper -- retail's `[w] - 8` with the
    // null test (.text:06d0c1f0 .. .text:06d0c1fc).
    static GameObject* WatchedObject(BaseWatcher const& Watcher);
};

static_assert(offsetof(FSystemDockEntry, Target) == 0x08, "FSystemDockEntry+0x08 target watcher");
static_assert(offsetof(FSystemDockEntry, BaseId) == 0x10, "FSystemDockEntry+0x10 base id");
static_assert(offsetof(FSystemDockEntry, bLand) == 0x14, "FSystemDockEntry+0x14 land flag");

// The list node. The element is embedded, like the player list's
// (SystemPlayerNode): 0x08 header + 0x18 element = 0x20, which is the size the
// constructor's sentinel (.text:06d06d26 `PUSH 0x20`) and every node use.
struct SystemDockNode {
    SystemDockNode* Next;             // +0x00
    SystemDockNode* Prev;             // +0x04
    FSystemDockEntry Entry;           // +0x08
};

static_assert(sizeof(SystemDockNode) == 0x20, "SystemDockNode is retail's 0x20-byte node");

// StarSystem -- runtime per-system state in Server.dll SysDB, 0x100 bytes.
// Retail: std::map<unsigned int, StarSystem> SysDB::SysMap.
//
// POLYMORPHIC, and an IObjDB. RTTI settles the hierarchy: the vtable at
// .rdata:06d67cd0 has its COL at .rdata:06d67ccc -> .rdata:06d6d028, type
// descriptor .data:06d88b50 ".?AVStarSystem@@", class hierarchy
// .rdata:06d6d018 with two entries (StarSystem, IObjDB), single inheritance.
// The constructor (.text:06d06ce0) stores IObjDB's vtable first
// (.text:06d06d08, .rdata:06d67cec) and its own last (.text:06d06e26). There is
// no virtual destructor: the vtable has exactly IObjDB's four slots.
//   slot 0 +0x00  .text:06d0dde0  GetNext
//   slot 1 +0x04  .text:06d0dd40  FindById   (-> FUN_06d0c840)
//   slot 2 +0x08  .text:06ce10f0  GetZoneDb  (ICF-shared with CPlayerGroup::GetID:
//                                             `MOV EAX,[ECX+4]; RET` = SystemInfo)
//   slot 3 +0x0c  .text:06d06e90  GetBucket  (`MOV EAX,[ECX+0x20]; RET 4`)
//
// Layout (constructor .text:06d06ce0, copy constructor .text:06d10520,
// destructor body inlined in ~pair .text:06d10030):
//   +0x00 vptr   +0x04 SystemInfo   +0x08 player list   +0x14 dock queue
//   +0x20 Spatial   +0x24 a std::vector of 0xC-byte elements
//   +0x34 .. +0xFB ten ObjectLists   +0xFC bLoaded
class StarSystem : public IObjDB {
public:
    // .text:06d06ce0. Only caller: SysDB's build loop (.text:06d0fb8e), which
    // builds a temporary and inserts a copy of it.
    StarSystem();
    // .text:06d10520 (called at .text:06d0fba9 to build the map's value_type
    // pair) and the same body inlined into the node constructor FUN_06d11330.
    StarSystem(StarSystem const& Other);
    // The body of FUN_06d10030 after its first `LEA ESI,[ECX+0x4]` --
    // ~pair<unsigned const, StarSystem> is ~StarSystem inlined.
    ~StarSystem();
    StarSystem& operator=(StarSystem const&) = delete;

    // FUN_06d06ea0 (.text:06d06ea0, `RET 0x4`): SysDB's per-entry init, run on
    // the mapped value right after the insert (.text:06d0fe0a).
    void init(unsigned int SystemId);

    // -- IObjDB ---------------------------------------------------------------
    IObjRW* __thiscall GetNext(void* IterState) override;
    IObjRW* __thiscall FindById(unsigned const* ObjectId) override;
    Universe::ISystem* __thiscall GetZoneDb() override;
    void* __thiscall GetBucket(unsigned TypeMask) override;

    // FUN_06d0c840 (.text:06d0c840, `RET 0x4`, 16 callers): the object with
    // this id in any of seven blocks, searched in the order +0x34, +0x48,
    // +0x70, +0x98, +0xac, +0xc0, +0x5c; null for an id with no bit inside
    // 0x3fffffff. +0x84, +0xd4 and +0xe8 are never searched.
    GameObject* find_object(unsigned int Id);

    // FUN_06d0c8d0 (.text:06d0c8d0, `RET 0x8`, 11 callers): the same, but the
    // type mask picks ONE block (-1 falls back to find_object).
    GameObject* find_object(unsigned int Id, unsigned int TypeMask);

    // +0x04: the Universe system this entry stands for.
    //
    // CORRECTED 2026-09-17 (wave 6). This was "secondary vtable/data pointer"
    // and the system's own id was believed to live at +0x10. Both were wrong.
    // FUN_06d06ea0 -- the initializer SysDB's build loop runs on every freshly
    // inserted entry, called as FUN_06d06ea0(node + 0x10, sysId) where node+0x10
    // IS the mapped StarSystem -- stores Universe::get_system(sysId) here.
    // So +0x04 is a Universe::ISystem*, and every "the system's own nickname id"
    // read in the module is the virtual call `(**(code**)**(undefined4**)(this+4))()`
    // on it -- FUN_06d087f0/FUN_06d08890 build their event payloads that way.
    Universe::ISystem* SystemInfo;

    // +0x08..+0x13: std::list of the players currently in this system.
    // MSVC6 std::list is {allocator byte (padded), _Head, _Size}: the ctor
    // writes the allocator byte at +0x08, the head node from
    // FUN_06d0f3e0(0,0) (operator new(0x50), self-linked) at +0x0C and zero
    // at +0x10. count_players walks `**(this+0xC)` to the sentinel `*(this+0xC)`;
    // remove_player decrements `*(this+0x10)` after unlinking a node.
    // Node layout: SystemPlayerNode above.
    unsigned char ListAlloc;
    unsigned char Pad1[3];
    void* PlayerListHead;              // +0x0C  _Head (self-referencing sentinel)
    unsigned int PlayerListSize;       // +0x10  _Size -- NOT the system id

    // +0x14..+0x1F: a second std::list, same shape (allocator byte at +0x14, a
    // fresh operator_new(0x20) self-referencing head at +0x18, size at +0x1C).
    // It is the dock queue -- SystemDockNode / FSystemDockEntry above.
    // queue_dock (FUN_06d0d370) is its only producer, the landing block of the
    // system update (FUN_06d0be50, .text:06d0c1ae .. .text:06d0c3f3) its only
    // consumer, and erase_docks (FUN_06d0f410) empties it. The field names
    // predate that decoding.
    unsigned char ObjectListAlloc;     // +0x14
    unsigned char Pad2[3];
    void* ObjectListHead;              // +0x18  _Head
    unsigned int ObjectListSize;       // +0x1C  _Size

    // +0x20: the system's spatial partition. load() stores
    // COMMON.DLL::NewSpatial("BSP") here (.text:06d0705c .. 06d07067) and bulk-
    // adds every solar; GetBucket returns it (.text:06d06e90); unload frees it
    // with FreeSpatial and nulls it (.text:06d075d0 .. 06d075e1). The copy
    // constructor copies the pointer (.text:06d105d8); the destructor does not
    // free it.
    ISpatialPartition* Spatial;

    // +0x24..+0x33: an MSVC6 std::vector {allocator byte +0x24, _First +0x28,
    // _Last +0x2C, _End +0x30} whose element is 0xC bytes (the copy
    // constructor sizes it with `IMUL 0x2aaaaaab / SAR 1`, .text:06d105f7,
    // and FUN_06d0f2f0 moves three dwords per element). The destructor frees
    // _First (.text:06d10180) and unload empties it (FUN_06d0f2f0,
    // .text:06d07551). What the element is has not been traced; no port code
    // fills it.
    unsigned char VectorAlloc;         // +0x24
    unsigned char Pad3[3];
    void* VectorBegin;                 // +0x28
    void* VectorEnd;                   // +0x2C
    void* VectorCapacity;              // +0x30

    // +0x34 .. +0xFB: the ten dispatch blocks (ObjectList above). The type
    // mask each one answers to in GetNext / find_object(Id, Mask) names it.
    ObjectList Ships;                  // +0x34  mask 0x7f0000 (IClient CREATESHIP in add_player)
    ObjectList Loot;                   // +0x48  mask 0x10000000 (Loot::get_type_bits, CREATELOOT)
    ObjectList Solars;                 // +0x5C  mask 0xfdff (CREATESOLAR; the BSP is built from it)
    ObjectList List70;                 // +0x70  mask 0x1000000 (.text:06cf3d70 get_type_bits)
    ObjectList Beams;                  // +0x84  mask 0x2000000; the MP step calls CBeam::move on
                                       //        every object in it (.text:06d0c45f)
    ObjectList List98;                 // +0x98  mask 0x4000000 (.text:06cf3400 get_type_bits)
    ObjectList ListAC;                 // +0xAC  mask 0x8000000 (.text:06ce61f0 get_type_bits)
    ObjectList ListC0;                 // +0xC0  masks 0x20000000 / 0x60000000; torn down next to
                                       //        the asteroid fields in unload (FUN_06d075f0)
    ObjectList ListD4;                 // +0xD4  in no GetNext mask; every object in it is fed to
                                       //        the per-step +0x16c pass (.text:06d0bfe4)
    ObjectList Deferred;               // +0xE8  objects whose update() answered false; the step
                                       //        deletes them (.text:06d0c152 .. 06d0c1ac)

    // +0xFC: loaded flag (add_player tests it for zero at .text:06d07e49,
    // load() for one at .text:06d06ef2).
    unsigned char bLoaded;
    unsigned char Pad4[3];

    // count_players — retail RVA 0x2BCB0, ordinal 476
    // Iterates the player list at this+0x0C, counting entries where node[0x10] != ExcludeId.
    // The ONLY StarSystem member Server.dll exports (?count_players@StarSystem@@QBEII@Z),
    // so it carries SERVER_API itself rather than the class.
    SERVER_API unsigned int count_players(unsigned int ExcludeId) const;

    // ------------------------------------------------------------------
    // remove_player — retail FUN_06d08740(this, SessionId), the first half of
    // both FUN_06d087f0 and FUN_06d08890 (the two "player left this system"
    // entry points). Two independent passes:
    //
    //  (a) the player list at this+0x0C: walk it from `**(this+0xC)` until the
    //      sentinel; on the first node whose dword at node+0x40 (node[0x10])
    //      equals SessionId, unlink it (`[node[1]] = node[0]` /
    //      `[node[0]+4] = node[1]`), call its own `(**(code**)node[2])(0)`
    //      disposer, free the node, and decrement the count at this+0x10.
    //      Retail breaks after the first match.
    //
    //  (b) the Ships list (head at this+0x38): for every node,
    //      take obj = node->Data (a real pointer -- .text:06d0879f is a MOV, not
    //      a LEA), resolve its CBase::Object at +0x10 under the usual
    //      `(ObjectClass & 3) == 3` guard, and if that CSimple's OwnerPlayer
    //      (+0xb4) equals SessionId and `obj->is_destroyed()` (vtable +0x160) is
    //      false, call `obj->destroy(0, obj->get_id())`
    //      (vtable +0x158 with vtable +0x20's result as the second argument).
    //
    // PORTED 2026-09-17 (wave 8). The three vtable offsets are no longer opaque:
    // +0x20 is IObjInspect::get_id (slot 8) and +0x158/+0x160 are two of the
    // four GameObject extension slots, now named in server_gameobject.h.
    void remove_player(unsigned int SessionId);

    // player_switched_out -- retail FUN_06d087f0 (.text:06d087f0, thiscall,
    // RET 8; the name is the port's). The system-switch twin of FUN_06d08890
    // (PlayerDB_LeaveSystem in server_classes.cpp). Only caller:
    // IServerImpl::SystemSwitchOutComplete (.text:06cef848), pushing
    // (SessionId, the destination system id).
    //   .text:06d087f6 .. 06d087fd  remove_player(SessionId) (FUN_06d08740)
    //   .text:06d08802 .. 06d08817  FUN_06d4b450 on Players[SessionId]
    //                               (PlayerData_FlushPendingVisits)
    //   .text:06d0881c .. 06d08843  DestSystemId != 0 -> controller event 0x3a
    //                               {SystemInfo->vtable[0](), DestSystemId,
    //                               SessionId}
    //   .text:06d08846 .. 06d0887d  single player and bLoaded (+0xfc): clear it,
    //                               event 0x3d {system id}, unload() (FUN_06d072c0)
    void player_switched_out(unsigned int SessionId, unsigned int DestSystemId);

    // activate_object -- retail FUN_06d0d8a0 (.text:06d0d8a0, thiscall, RET 0xc;
    // the name is the port's). Only caller: pub::SpaceObj::Activate
    // (.text:06cf9b24), pushing (obj, activate, index).
    //   .text:06d0d8ab .. 06d0d8c8  __RTDynamicCast<EqObj>(Obj); null -> false
    //   .text:06d0d8ce .. 06d0d8da  FUN_06ce7850: CEqObj::activate(bOn, Index)
    //                               (vt +0xb4 on the & 0x103 narrowing)
    //   .text:06d0d8df .. 06d0d8ec  Failed -> false; not Changed -> true
    //   .text:06d0d8f2 .. 06d0d971  ACTIVATEOBJECT {id, bOn, Index} through
    //                               IClient SendServerActivateObject to every player when
    //                               cobject() is a solar (& 0x303), else only
    //                               where SystemPlayerView_IsObjectVisible
    //   .text:06d0d976              true
    bool activate_object(GameObject* Obj, bool bOn, unsigned int Index);

    // spawn_mined_loot -- retail FUN_06d0d9b0 (.text:06d0d9b0, thiscall, RET
    // 0x10; the name is the port's). Only caller: IServerImpl::MineAsteroid
    // (.text:06cf07f3), pushing (&pos, crate archetype, loot archetype, count).
    //   .text:06d0d9e2 .. 06d0da05  a fresh id (CFLIDMaker, inlined allocate)
    //   .text:06d0da04 .. 06d0da16  GetEquipment(ContentsArchId); null -> return
    //   .text:06d0da1c .. 06d0da54  container = GetEquipment(ContainerArchId) if
    //                               non-zero, else/null -> contents'
    //                               get_loot_appearance(); null -> return
    //   .text:06d0da5a .. 06d0db18  FLootRecord {id, container, its HitPts,
    //                               contents, its HitPts, Count, 0, 0,
    //                               random_inside_sphere(20), Pos, identity,
    //                               random_inside_sphere(3), 1, 0}
    //   .text:06d0db1f              create_loot(0, 3, &rec, 1)
    void spawn_mined_loot(Vector const& Pos, unsigned int ContainerArchId,
                          unsigned int ContentsArchId, int Count);

    // send_tractor_objects -- retail FUN_06d0e7a0 (.text:06d0e7a0, thiscall,
    // RET 8, SEH frame; the name is the port's). Callers FUN_06ce6ba0
    // (.text:06ce6cfa) and FUN_06ce6d80 (.text:06ce6f17), pushing (&the id of
    // the player to skip, &the XTractorObjects they built).
    //   .text:06d0e7b8 .. 06d0e8ae  for every player node but *SkipPlayerId
    //                               that sees Info.ObjectId (FUN_06cf7370):
    //                               a copy {ObjectId, TractorEquipId, the ids
    //                               of Info.TargetIds that player sees}, IClient
    //                               +0x14 (SendCommonTractorObjects) with node+0x40, then the
    //                               copy's vector block freed (FUN_06d5ffb0)
    void send_tractor_objects(unsigned int const& SkipPlayerId, XTractorObjects const& Info);

    // tractor_loot -- retail FUN_06d0ed40 (.text:06d0ed40, thiscall, RET 0x10;
    // the name is the port's). Only caller: SrvTractor::notify (.text:06d06aee),
    // pushing (loot, ship, the ship's free space for the contents, the tractor's
    // equipment id).
    //   .text:06d0ed4d .. 06d0ed9d  CLoot (& 0x803), Take = min(Space, Units)
    //                               (unsigned), the contents' archetype id
    //   .text:06d0ed9f .. 06d0ee1c  Health: 1.0f; a non-combinable good carries
    //                               ContentsHitPts / HitPts; a combinable
    //                               commodity with decay > 0 taken whole carries
    //                               max(ContentsHitPts - Loot::DecayAccum, 0) /
    //                               HitPts; any other combinable good 1.0f
    //   .text:06d0ee20 .. 06d0ee43  add_cargo(Ship, arch, Take, Health, bTemporary)
    //   .text:06d0ee48 .. 06d0ee80  controller event 0x18 {loot id, ship id,
    //                               cargo sub id, -, Arch2Good(arch), Take}
    //   .text:06d0ee85 .. 06d0ef09  ITEMTRACTORED (IClient +0x100, SendServerItemTractored) to
    //                               every player that sees the ship
    //   .text:06d0ef0f .. 06d0efd6  Take < Units: Units -= Take. Otherwise the
    //                               tractor drops the target (unless a player
    //                               ship in single player), loot->destroy(2,
    //                               ship id), Units = 0
    void tractor_loot(::Loot* LootObj, EqObj* Ship, unsigned int Space, unsigned short TractorId);

    // use_item -- retail FUN_06d0efe0 (.text:06d0efe0, thiscall, RET 0xc; the
    // name follows its packet, FLPACKET_SERVER_USE_ITEM). Only caller: the EqObj
    // IObjAffect method LAB_06ce7210 (.text:06ce72ff), after
    // CECargo::RemoveFromStack, pushing (the EqObj, equip id, count).
    //   .text:06d0efeb .. 06d0f057  a player's ship (is_valid_ship_owner,
    //                               inlined): FUN_06d4f100(slot, id, count, 0)
    //                               (PlayerData_RemoveEquipment)
    //   .text:06d0f05c .. 06d0f0ad  USE_ITEM {id, EquipId, count} through
    //                               IClient +0x104 (SendServerUseItem) to every player
    //                               that sees the object (FUN_06cf7370)
    void use_item(GameObject* Obj, unsigned short EquipId, int Count);

    // sp_obj_update -- retail FUN_06d0bbc0 (.text:06d0bbc0, thiscall, RET 8; the
    // name follows its caller). Only caller: IServerImpl::SPObjUpdate
    // (.text:06cf06a5), pushing (&info, the sender's session id).
    //   .text:06d0bbcc .. 06d0bc0d  the ship (+0x34 block) whose CSimple id is
    //                               Info.ObjectId; none -> return
    //   .text:06d0bc0f .. 06d0bc21  PhySys::RCSet(ship, pos, orientation, time)
    //   .text:06d0bc27 .. 06d0bc70  FCommonUpdateObjectInfo from Info
    //   .text:06d0bc69 .. 06d0bc99  FUN_06cf65b0 (SystemPlayerView_SendObjectUpdate)
    //                               for every player but SessionId
    void sp_obj_update(SSPObjUpdateInfo const& Info, unsigned int SessionId);

    // scan_objects -- retail FUN_06d0bcd0 (.text:06d0bcd0, thiscall, RET 0x14;
    // the name is the port's). Callers in pub::System / pub::SpaceObj
    // (.text:06cf9c51, 06cf9c98, 06cfa0c4, 06cfa105, 06cfad4f).
    //   .text:06d0bcf7 .. 06d0bd21  TypeMask & 0x7f0000: the ship block's
    //                               collect_in_sphere (FUN_06cf5020)
    //   .text:06d0bd23 .. 06d0bd29  TypeMask & 0xfdff == 0 -> return
    //   .text:06d0bd2f .. 06d0bd41  Radius not > 0: the solar block's
    //                               collect_in_sphere into the rest (06d0be11)
    //   .text:06d0bd47 .. 06d0be00  otherwise GetBucket(0xfdff)'s Fill (vt +0x1c)
    //                               with {Pos, Radius} into a 0x800-slot block;
    //                               each hit whose get_type (vt +0x88) succeeds
    //                               and meets TypeMask adds its get_id()
    // Returns how many ids were written.
    unsigned int scan_objects(unsigned int* OutIds, unsigned int MaxCount, Vector const* Pos,
                              float Radius, unsigned int TypeMask);

    // ------------------------------------------------------------------
    // add_player -- retail FUN_06d07e20(this, SessionId), .text:06d07e20,
    // `RET 0x4`. The counterpart remove_player never had; called from
    // .text:06cef850, .text:06cefbdb and .text:06cfacfb.
    //
    // Its head is the only thing in the whole module that ever grows the
    // player list, and it is four steps:
    //   .text:06d07e41  MOV AL,[EDI+0xfc] / CMP AL,BL / JNZ -- when the system
    //                   is not loaded yet, load it first (FUN_06d06ed0, a
    //                   thiscall: ECX is still `this`; it sets +0xfc to 1,
    //                   starts PhySys and reads the system ini).
    //   .text:06d07e5d  TEST EBP,0x3fffffff / JZ -- a session id with no bit
    //                   inside 0x3fffffff is rejected outright. Note the mask:
    //                   it is NOT `!= 0`, the top two bits do not count.
    //   .text:06d07e6a  a Observer is built on the STACK from the
    //                   session id (FUN_06cf51e0), handed to
    //                   `std::list<Observer>::insert` at
    //                   .text:06d07e8a with `LEA ECX,[EDI+0x8]` as the list and
    //                   `[EDI+0xc]` (the sentinel) as the position -- so it is
    //                   a push_back -- and then destroyed again
    //                   (.text:06d07ea1).
    //   .text:06d07ea6  the live element is recovered as
    //                   `[[EDI+0xc] + 4] + 8`, i.e. sentinel->prev->View.
    //
    // Everything past .text:06d07eb3 is the initial synchronisation, and it is
    // three passes over three of the system's dispatch blocks. Which block is
    // which is not a guess: each pass ends in a different IClient create slot.
    //   block at +0x34 (head +0x38)  SHIPS   -- .text:06d08369 / .text:06d0855c
    //   block at +0x48 (head +0x4c)  LOOT    -- .text:06d085c0 / .text:06d08695
    //   block at +0x5c (head +0x60)  SOLARS  -- .text:06d07f15
    // The counts come from FUN_06cf5130(block, 0xffffffff), which is NOT a plain
    // size: it walks the NULL-terminated list at block+0x04 and counts only the
    // nodes whose object answers a non-zero `mask & get_type_bits()`
    // (.text:06cf5154 `CALL [EAX + 0x154]` / .text:06cf515a `TEST EBX,EAX`).
    //
    // The single-player / multiplayer split is the opposite way round from what
    // it looks like, and .text:06d08302 `JZ LAB_06d08550` settles it: the JZ is
    // taken when SinglePlayer() returned FALSE.
    //   SINGLE PLAYER -- build the wire payload for every object and send it
    //     straight away (CREATESHIP at .text:06d08466, CREATELOOT at
    //     .text:06d0864e). Consistent with AddTrackedObject seeding
    //     bCreatedOnClient from SinglePlayer(): the local client is told
    //     everything at once.
    //   MULTIPLAYER -- only register each object in the joining player's tracked
    //     list (.text:06d08582 and .text:06d086b7, two of the seven
    //     FUN_06cf7140 call sites) and leave bCreatedOnClient false, so the
    //     per-frame range check sends the creates later.
    // The one thing multiplayer does eagerly is the SOLAR pass
    // (.text:06d07ed4..06d082f5), preceded by PlayerDB::SendSystemID at
    // .text:06d07ecf. Per node: a DYNAMIC solar (CSolar+0x1a9) gets CREATESOLAR
    // (IClient SendServerCreateSolar, payload from FUN_06d07950); every other node, static
    // solars included, gets its damage list (SendServerDamageObject) if it has one. Every
    // solar then gets SETREPUTATION (SendServerSetReputation), one SETTARGET per
    // functioning gun with a target (SendCommonSetTarget) and one ACTIVATEOBJECT per
    // active state (SendServerActivateObject).
    //
    // PORTED: the load call, the mask guard, the insert, SendSystemID, both
    // multiplayer tracked-list passes (wave 12), both single-player create
    // passes and the multiplayer solar walk (2026-09-24).
    // ------------------------------------------------------------------
    void add_player(unsigned int SessionId);

    // ------------------------------------------------------------------
    // load -- retail FUN_06d06ed0 (.text:06d06ed0). add_player's first
    // statement, and the only thing that ever puts objects into a system.
    //
    // ARITY CORRECTION. Ghidra declares it `void FUN_06d06ed0(void)` -- no
    // `this` at all, which is why every earlier note called the system id
    // unreachable from inside it. The listing says otherwise:
    //   .text:06d06ef0  8bd9          MOV  EBX,ECX            ; ECX IS `this`
    //   .text:06d06ef2  80bbfc000...  CMP  byte ptr [EBX + 0xfc],0x1
    //   .text:06d07120  c3            RET                     ; no stack args
    // so it is `void __thiscall StarSystem::load()`. Note also that the guard
    // compares against 1, not against zero, while add_player's own guard
    // (.text:06d07e49 `CMP AL,BL` with EBX = 0) compares against zero.
    //
    // Body, in order:
    //   .text:06d06eff  sysId = SystemInfo->vtable[0]()
    //   .text:06d06f0b  COMMON.DLL::PhySys::Startup(sysId)   -- cdecl, one arg.
    //                   (It is PhySys::Startup, not "PhySys::Start".)
    //   .text:06d06f1c  if SinglePlayer():
    //     .text:06d06f20    FUN_06d07170(this)  -- wrap every CObject the client
    //                       already built: CObject::FindFirst(0x303) / AddRef /
    //                       operator new(0x90) / FUN_06d02b70 / FUN_06d02c20 /
    //                       vtable +0x170 / vtable +0x1bc / Release, push_back
    //                       onto the +0x5c block. FDUMPs "failed to find any
    //                       solar objects\n" (StarSys.cpp line 0x129) when the
    //                       walk found nothing.
    //     .text:06d06f26    FUN_06d03500(sysId)
    //   otherwise:
    //     .text:06d06f38    name = SystemInfo->vtable[0x14]()
    //     .text:06d06f41    Universe::get_filename(&path, name)
    //     .text:06d06f48    CmnAsteroid::Load(sysId)
    //     .text:06d06f54    FUN_06d032b0(sysId, &path)
    //     .text:06d06f63    INI_Reader over `path`, and for every [Object]
    //                       section FUN_06d09d60(this, reader) (.text:06d06fc1).
    //   .text:06d07047  count = FUN_06cf5130(this + 0x5c, 0xffffffff)
    //   .text:06d0705c  this->Spatial = COMMON.DLL::NewSpatial("BSP")
    //   .text:06d07074  when that returned non-null, build a count * 0x14 array,
    //                   one 0x14-byte record per solar: {object, its position
    //                   (three dwords copied off vtable[0]'s result), its radius}
    //                   and hand it to the partition's vtable +0x14 as
    //                   (array, count) -- .text:06d070d5 pushes the count first
    //                   and the array second, so the array is the FIRST
    //                   argument. Then free the array (.text:06d070df).
    //                   The radius is a FLOAT, not the dword the decompilation
    //                   shows: .text:06d070be is `FLD float ptr [EAX + 0x38]`
    //                   against .text:06d070c5 `FSTP float ptr [EDI]`.
    //   .text:06d070eb  bLoaded = 1
    //   .text:06d070ff  FUN_06d43650(0x3c, &sysId)   -- "system loaded" event.
    //
    // PORTED 2026-09-17 (wave 12) as this member; since 2026-09-24
    // (claude-code:w5-server-06d0) the populate half is retail's too:
    // load_solars (FUN_06d07170) and load_object (FUN_06d09d60) below. The
    // port's former StarSystem_LoadObjects rewrite is gone.
    // ------------------------------------------------------------------
    void load();

    // ------------------------------------------------------------------
    // load_solars -- retail FUN_06d07170 (.text:06d07170, thiscall, no stack
    // arguments; the name is the port's). Single player only: the client
    // already built the system's solars, so each one is wrapped.
    //   .text:06d0718a .. 06d071a6  CObject::FindFirst(0x303); null -> the
    //                               warning below
    //   per object (.text:06d071b0 .. 06d0727c):
    //     CObject::AddRef; operator new(0x90) + Solar ctor (FUN_06d02b70);
    //     attach_existing(object when its class & 0x303 == 0x303, else 0)
    //     (FUN_06d02c20); enter_system(this, 0) (vt +0x170); check_fuses
    //     (vt +0x1bc); CObject::Release; the wrapper appended to Solars
    //     (+0x5c, MetaList append inlined); ++count; CObject::FindNext
    //   .text:06d07282 .. 06d072ac  count == 0 -> FDUMP(0x100002, "%s(%d) :
    //                               *** WARNING: %s", StarSys.cpp, 0x129,
    //                               "failed to find any solar objects\n")
    // ------------------------------------------------------------------
    void load_solars();

    // ------------------------------------------------------------------
    // load_object -- retail FUN_06d09d60 (.text:06d09d60, thiscall, RET 4; the
    // name is the port's). Multiplayer: one [Object] section of the system file.
    //   .text:06d09d80 .. 06d09e47  a CSolar::CreateParms on the stack (the
    //                               inlined ctor: FUN_06d08e20, FUN_06d0a060,
    //                               EquipDescVector, the CollisionGroupDesc
    //                               list, the ID_String clears and zeroes, and
    //                               the anim-name std::list at +0xe4 whose ctor
    //                               FUN_06d1ad30 allocates the 0xc head node)
    //   .text:06d09e69              CSolar::ReadObj(Reader, &Arch, &Parms);
    //                               its bool is dropped
    //   Arch == 0 (.text:06d09fd9 .. 06d0a043): the parms destroyed, return 0
    //   otherwise (.text:06d09e82 .. 06d09fd7):
    //     Arch vt +0xc (Archetype::Root::load); operator new(0x90) + Solar
    //     ctor; create(Arch) (vt +0x164); CSolar::init(Parms) on the CBase
    //     object under the 0x303 guard; the wrapper appended to Solars;
    //     Archetype::Root::Release; enter_system(this, 0) (vt +0x170);
    //     check_fuses (vt +0x1bc); the parms destroyed; return the wrapper
    // ------------------------------------------------------------------
    GameObject* load_object(INI_Reader& Reader);

    // ------------------------------------------------------------------
    // create_solar -- retail FUN_06d0a200 (.text:06d0a200 .. 06d0a763,
    // thiscall, RET 4; the name is the port's). A scripted solar from a
    // CREATESOLAR request; only caller pub::SpaceObj::CreateSolar
    // (.text:06cf82bf). Returns the new wrapper (EBX). Req is written to:
    // +0xa8 and +0xb0 per player before FUN_06cf66f0 hands it on.
    //   .text:06d0a21e .. 06d0a289  operator new(0x90) + Solar ctor
    //                               (FUN_06d02b70); Archetype::GetSolar(Req
    //                               +0x08), load (vt +0xc), create (vt +0x164),
    //                               Root::Release; the 0x303 CSolar kept
    //   .text:06d0a2b0 .. 06d0a34b  CSolar::CreateParms at [S+0x28]: FUN_06d0a770
    //                               (the CSimple / CEqObj halves), then the
    //                               CSolar tail: ID_String clears +0xb8 / +0xc0
    //                               / +0xcc, +0xbc / +0xc4 / +0xc5 bytes 0,
    //                               +0xc8 / +0xd0..+0xe0 zero, the anim list at
    //                               +0xe4 (FUN_06cfc340 head, size 0)
    //   .text:06d0a352 .. 06d0a453  Id, ObjDb = this, OwnerPlayer 0, HitPts =
    //                               FILD +0x68, Pos, Orient = QuaternionToMatrix
    //                               (+0x58) via DAT_06d8da08, +0xbc <- +0xb4,
    //                               +0x5c and +0xe0 <- +0x0c, +0x60 <- +0x10,
    //                               +0x64..+0x70 <- +0x14..+0x20, +0x98 <- +0x48,
    //                               +0x94 <- +0x44 and that many dwords from +0x24
    //   .text:06d0a455 .. 06d0a482  +0x6c != 0 -> Equipment = Loadout::Get(+0x6c)
    //                               ->Items (vector operator=); then
    //                               EquipDescVector::append(Req +0x70)
    //   .text:06d0a488 .. 06d0a506  ArchGroupDescs = Req +0x80 (list operator=,
    //                               inlined)
    //   .text:06d0a50c .. 06d0a537  +0xb8 <- +0xb8, +0xc4 = 1, +0xc5 <- +0xac;
    //                               CSolar::init
    //   .text:06d0a53d .. 06d0a54f  +0xbc & 0x3fffffff -> mark_for_player(+0xbc, 1)
    //   .text:06d0a554 .. 06d0a569  enter_system(this, 0) (vt +0x170),
    //                               check_fuses (vt +0x1bc)
    //   .text:06d0a56f .. 06d0a596  Solars append (FUN_06d11850 node, inlined link)
    //   .text:06d0a599 .. 06d0a614  GetBucket(0xfdff) non-null -> its vt +0x10
    //                               with {wrapper, get_position(), CSolar radius
    //                               +0x38} (the radius read first)
    //   .text:06d0a617 .. 06d0a61d  CObject::enable_collisions_r(true)
    //   .text:06d0a623 .. 06d0a66f  every player node: Req +0xa8 = is_marked_by
    //                               (node +0x40), Req +0xb0 = Players[id].rep_vibe,
    //                               SystemPlayerView_CreateSolarForPlayer
    //                               (FUN_06cf66f0)(&node->View, &Req, wrapper)
    //   .text:06d0a671 .. 06d0a74a  the parms destroyed (anim list cleared and
    //                               its head freed, the group list, the vector)
    // ------------------------------------------------------------------
    ::Solar* create_solar(CSolarCreateInfo& Req);

    // ------------------------------------------------------------------
    // create_ship -- retail FUN_06d0a880 (.text:06d0a880 .. 06d0aef2, thiscall,
    // RET 4; the name is the port's). Callers pub::SpaceObj::Create
    // (.text:06cf7f94) and IServerImpl::PlayerLaunch (.text:06cf00ba). Returns
    // the new wrapper (EBX). S = ESP after the four register pushes.
    //   .text:06d0a89f .. 06d0a8d0  operator new(0xc8) + Ship ctor (FUN_06d00e80)
    //   .text:06d0a8d2 .. 06d0a98f  a CShipCreateInfo copy of Req at [S+0x2c]
    //                               (FUN_06d0af00 + the derived tail)
    //   owner +0xac != 0 and SinglePlayer (.text:06d0a997 .. 06d0a9ec):
    //     find(Req +0x04) (vt +0x168); CShip = the 0x503 narrowing or 0
    //   otherwise (.text:06d0a9f1 .. 06d0ad15):
    //     Archetype::GetShip(copy +0x08), load (vt +0xc), create (vt +0x164),
    //     Root::Release; CShip = the 0x503 narrowing or 0;
    //     CShip::CreateParms at [S+0x10c]: FUN_06d0a770, then +0xe8 / +0xec /
    //     +0xf0 = 0, byte +0xf4 = 0; Id, ObjDb = this, OwnerPlayer <- +0xac,
    //     HitPts FILD +0x68, Pos, Orient = QuaternionToMatrix(+0x58) via
    //     DAT_06d8da08, Velocity <- +0xb8, AngularVelocity <- +0xc4, +0x60 <-
    //     +0x10, +0xf4 <- byte +0xdc, +0x64..+0x70 <- +0x14..+0x20, +0x98 <-
    //     +0x48, +0xf0 <- +0xb4, +0x94 <- +0x44 and that many dwords from
    //     +0x24; +0x6c != 0 -> Equipment = Loadout::Get(+0x6c)->Items;
    //     EquipDescVector::append(copy +0x70); ArchGroupDescs = copy +0x80;
    //     +0xec <- +0xb0, +0x5c <- +0x0c; CShip::init (vt +0x10c); owner != 0
    //     and Players[owner].group_ptr != 0 -> CShip +0x1b4 = group +0x04; the
    //     parms destroyed
    //   .text:06d0ad19 .. 06d0ad4c  Ship::FEquipState from Req +0xe0 / +0xe4;
    //                               Req +0xe8 & 0x3fffffff -> mark_for_player(it, 1)
    //   .text:06d0ad51 .. 06d0ad65  enter_system(this, 0) (vt +0x170),
    //                               set_equip_state (FUN_06d010e0)
    //   .text:06d0ad6a .. 06d0ad8f  Ships append (FUN_06d11850, inlined link)
    //   .text:06d0ad92 .. 06d0adab  !SinglePlayer || owner == 0 ->
    //                               CObject::enable_collisions_r(true) on the CShip
    //   .text:06d0adb1 .. 06d0adf0  every player node but the owner: copy +0xa8 =
    //                               is_marked_by(id), SystemPlayerView_
    //                               CreateShipForPlayer (FUN_06cf6630)(&view,
    //                               &copy, wrapper)
    //   .text:06d0adf2 .. 06d0aed2  the copy destroyed
    // ------------------------------------------------------------------
    ::Ship* create_ship(FShipCreateRequest const& Req);

    // ------------------------------------------------------------------
    // on_object_destroyed -- retail FUN_06d0b160 (.text:06d0b160, RET 0x8).
    // GameObject::destroy's middle statement: `MOV ECX,[ESI + 0x28]` then
    // `CALL FUN_06d0b160` at .text:06d0b160's caller .text:06cee659, so the
    // `this` really is the object's own OwnerSystem.
    //
    //  (a) resolve the object's CSimple::OwnerPlayer (+0xb4) under the usual
    //      (ObjectClass & 3) == 3 guard (.text:06d0b16a..06d0b182); when it is
    //      zero, skip straight to (b).
    //  (b) PlayerDB::is_valid(&Players, &sessionId) and, only when the slot's
    //      ship object id (+0x3E4) is this object's own get_id()
    //      (.text:06d0b1c4 `CMP [ESI + EAX + -0x34],EBX`):
    //        .text:06d0b1d1  FUN_06d50af0(slot)            terminate the trade
    //        .text:06d0b1dc  PUSH 1 / FUN_06d50a40(slot)   cancel the request
    //        .text:06d0b1ea  CMP EBP,0x1 -> FUN_06d4ed10(slot, 0.0f, false)
    //        .text:06d0b20a  slot->ship_object_id = 0
    //        .text:06d0b22b  FUN_06d43650(0x37, {slot->system_id, sessionId})
    //  (c) unconditionally, for every node of the player list at +0x0C, call
    //      FUN_06cf6820(node + 8, Obj, Kind).
    //
    // Kind is the FIRST argument of GameObject::destroy; the second (the object
    // id) never reaches here.
    // ------------------------------------------------------------------
    void on_object_destroyed(GameObject* Obj, int Kind);

    // ------------------------------------------------------------------
    // find_solar -- retail FUN_06d0c7e0 (.text:06d0c7e0, RET 0x4; the key is
    // passed by pointer, `CMP EDX,[EBX]` at .text:06d0c831). Walks the SOLAR
    // block's list (node = [this+0x60], .text:06d0c7f4, then node = node->Next)
    // and returns node->Data -- the GameObject wrapper -- of the first node
    // whose CSolar::Nickname (+0x1a4, .text:06d0c82b) equals *NicknameId.
    // The loop stops on the node after the match (.text:06d0c7ff `TEST EAX,EAX`
    // / JNZ exit), so the FIRST match wins. Class test per node:
    // [Data+0x10] & 0x303 == 0x303 (.text:06d0c818..06d0c827), otherwise the
    // object pointer is zeroed and +0x1a4 is read off it anyway -- retail
    // relies on the block holding solars only.
    // Callers: pub::System::Find (.text:06cfb2b1), SystemSwitchOutComplete
    // (.text:06cef85c), FUN_06d00830 (.text:06d00c11).
    // ------------------------------------------------------------------
    GameObject* find_solar(unsigned int const* NicknameId) const;

    // ------------------------------------------------------------------
    // create_med_asteroid / create_med_mine -- retail FUN_06d09bb0 /
    // FUN_06d09c80 (thiscall, RET 4; the one argument is a POINTER to the id,
    // handed on unchanged as find()'s `unsigned int const&`). Both bodies are
    // the same four steps (.text:06d09bc8 .. 06d09c7b / .text:06d09c98 ..
    // 06d09d4e):
    //   operator new(0x68 / 0x88) + the class constructor (a null allocation
    //   leaves the object pointer 0 and carries on);
    //   push the object onto the +0xc0 block (ListC0): a pooled node from
    //   FreeList (+0xd0) when bUseHeap (+0xcc) is set and the pool is not
    //   empty, else operator new(0xc); Next = 0, Prev = Tail, Data = object;
    //   linked after Tail, or made Head when there is no Tail; Tail = node --
    //   MetaList::append(T*) inlined;
    //   object->find(*Id)                           (vtable +0x168);
    //   object->enter_system(this, nullptr)         (vtable +0x170);
    // and return the object. Only caller: FSrvAsteroidCube::apply_marker
    // (.text:06d04b0b / .text:06d04b39).
    // ------------------------------------------------------------------
    SrvAsteroid::MedAsteroid* create_med_asteroid(unsigned int const& ObjId);
    SrvAsteroid::MedMine* create_med_mine(unsigned int const& ObjId);

    // ------------------------------------------------------------------
    // send_mine_explosion -- retail FUN_06d09270 (.text:06d09270, thiscall,
    // RET 4). Copies the four dwords of *Rec onto the stack (.text:06d0927b ..
    // 06d09297) and hands that copy to SystemPlayerView_SendMineExplosion
    // (FUN_06cf6df0) for every node of the player list at +0x0c, in list
    // order. Only caller: MedMine::update (.text:06d04f84).
    // ------------------------------------------------------------------
    void send_mine_explosion(FMineExplosion const& Rec);

    // ------------------------------------------------------------------
    // explode -- retail FUN_06d0b260 (.text:06d0b260, thiscall, RET 4). One
    // explosion in this system. Seven callers: .text:06ce64ed, .text:06ce8502
    // (EqObj::explode), .text:06cf2bf8, .text:06cf397f, .text:06cf458f,
    // .text:06cff42e and MedMine::update (.text:06d04f35).
    //  (1) Inflictor = find_object(Blast.InflictorId) (.text:06d0b290).
    //  (2) PhySys::FindSphereCollisions(system id, Blast.Position,
    //      Blast.Explosion->Radius, parts[0x3c], 0x3c) (.text:06d0b2b8); a
    //      std::vector<CObject*> reserved to that count collects GetRoot(part)
    //      for every part whose PhySys::GetCollisionGroup is not the
    //      record's SkipCollisionGroup (stricmp, both non-null).
    //  (3) Damage pass, only when Explosion->HullDamage > 0 or
    //      EnergyDamage > 0 (.text:06d0b3c7 / .text:06d0b3d7): per root of
    //      class (& 3) == 3 whose CSimple id is not the exploding object's and
    //      not already seen: find_object(id); live objects -- unless the
    //      GbAllowPvp gate says two players do not hurt each other --
    //      get a DamageList {cause, inflictor id, inflictor's OwnerPlayer},
    //      explosion_hit(Blast, list) and apply_damage_list(list).
    //  (4) Impulse pass, only when Explosion->Impulse != 0 (.text:06d0b5e1):
    //      per root of class (& 3) == 3 with OwnerPlayer 0 -- duplicates
    //      included -- the unit vector from the blast to its centre of mass
    //      (a random unit vector when they coincide) times Impulse, applied
    //      with PhySys::AddImpulseAtPoint at Blast.Position when the object has
    //      a physics body (CObject+0x54).
    // ------------------------------------------------------------------
    void explode(ExplosionDamageEvent const& Blast);

    // ------------------------------------------------------------------
    // munition_collision -- retail FUN_06d0d500 (.text:06d0d500, thiscall,
    // RET 4). One munition hitting one object. Three callers, each passing a
    // 0x1c-byte SSPMunitionCollisionInfo: IServerImpl::SPMunitionCollision
    // (.text:06cf073e, the client's record as is), Guided +0x1a4
    // (.text:06cf42f8) and Bullet +0x1a4 (.text:06cf4e96). The record, as
    // this body reads it: +0x00 the munition archetype id
    // (Archetype::GetEquipment), +0x04 the shooter's object id, +0x08 the
    // object hit, +0x0c the sub-object hit (word), +0x10 the hit position.
    //  (1) Target = find_object(+0x08), Shooter = find_object(+0x04).
    //  (2) GbAllowPvp == 0 and a Shooter that is_player(): return
    //      unless there is a Target and it is not a player.
    //  (3) No Target, or a destroyed one: return.
    //  (4) FMunitionImpact {shooter, munition, sub-object, hit position} and
    //      a DamageList {cause 2 for an ObjectType 0x2000000 munition, 3 for
    //      0x1000000, else 1; inflictor = shooter id; inflictor owner = the
    //      Shooter's CSimple OwnerPlayer when the Shooter exists};
    //      Target->munition_hit, then Target->apply_damage_list.
    // ------------------------------------------------------------------
    void munition_collision(SSPMunitionCollisionInfo const& Info);

    // ------------------------------------------------------------------
    // create_loot -- retail FUN_06d08a40 (.text:06d08a40, thiscall, RET 0x10;
    // the name is the port's). Every caller pushes (Count, Records, Mode,
    // source->get_id()) with ECX = the source's OwnerSystem:
    //   Ship jettison tick   .text:06d017e2 .. 06d017f6  (Mode 1, Count 1)
    //   EqObj dump_cargo     .text:06ce90fc               (Mode 2)
    //   EqObj drop_loot      .text:06ce8760 area          (Mode 2)
    //   EqObj destroy_equip  .text:06ce9f70 area, twice   (1 when the cause is
    //                        0x16, else 2)
    // One Loot per record (operator new(0x70), .text:06d08b90). Records is
    // only read (EBP walks it at a 0x6c stride, .text:06d08dac).
    // ------------------------------------------------------------------
    void create_loot(unsigned int SourceObjId, int Mode, FLootRecord const* Records, int Count);

    // ------------------------------------------------------------------
    // Four members EqObj/GameObject reach through OwnerSystem, ported
    // 2026-09-24 (claude-code:w5-server-06ce). Names are the port's.
    //
    // notify_object_left -- FUN_06d0cfa0 (.text:06d0cfa0, RET 4). Walks the
    // ship block (+0x34, head +0x38) then the solar block (+0x5c, head +0x60)
    // and calls every object's GameObject +0x178 (on_object_left_system) with
    // ObjId. Caller: GameObject::leave_system (.text:06cee838).
    void notify_object_left(unsigned int ObjId);

    // collision_damage -- FUN_06d0d730 (.text:06d0d730, RET 8). Damages Target
    // for a collision: skipped when DAT_06d88a80 is clear and both sides are
    // players, and when Target is null or already destroyed; otherwise a
    // cause-1 DamageList (inflictor = the source, its owner player) through
    // Target's +0x17c then +0x188. Caller: GameObject::on_collision.
    void collision_damage(FCollisionDamage const& Rec, GameObject* Target);

    // badlands_collision -- FUN_06d0d6e0 (.text:06d0d6e0, RET 4). The object
    // named by Info.ObjectId (+4), found in this system (FUN_06d0c840), is
    // destroyed -- kind 1, killer Info.Field_0x00 -- unless it is missing,
    // already destroyed (vt +0x160) or invincible (vt +0x19c). Only caller
    // IServerImpl::SPBadLandsObjCollision (.text:06cf07b9).
    void badlands_collision(SSPBadLandsObjCollisionInfo const& Info);

    // collect_dockable_solars -- FUN_06d0cd60 (.text:06d0cd60, RET 8). Appends to
    // Out every object of the solar block (+0x5c) whose CEqObj has a docking
    // base id (+0x164) -- all of them when BaseId is 0, else the ones whose id
    // equals it -- and, only when BaseId is 0, every one without a docking base
    // whose TypeBits (+0xe0) lie inside 0xc40. Callers IServerImpl::
    // SPRequestInvincibility (.text:06cf1484) and FUN_06d00830 (.text:06d00907).
    void collect_dockable_solars(std::list<GameObject*>& Out, unsigned int BaseId);

    // send_comm -- FUN_06d0db30 (.text:06d0db30, thiscall, RET 0x58: 22 dwords, the
    // Costume by value). pub::SpaceObj::SendComm's system half: resolve the sender's
    // position (when it has an id), then for every valid player view either send
    // FLPACKET_SERVER_SENDCOMM (FUN_06cf6ac0) or skip it, by who the view's ship is,
    // bBroadcast and a 10000-unit radius. Only caller pub::SpaceObj::SendComm
    // (.text:06cf9367).
    void send_comm(unsigned int SenderId, unsigned int ReceiverId, unsigned int VoiceId,
                   Costume const& Cost, unsigned int CommType, unsigned int* TextIds,
                   int TextCount, unsigned int Mood, float Duration, bool bBroadcast);

    // broadcast_fuse -- FUN_06d0e750 (.text:06d0e750, RET 4). IClient +0x15c
    // (BURNFUSE) to every player of this system who can see Info.ObjectId
    // (FUN_06cf7370). Callers: EqObj::light_fuse / unlight_fuse.
    void broadcast_fuse(FServerBurnFuseInfo const& Info);

    // ------------------------------------------------------------------
    // The IObjAffect notifiers Ship/EqObj/Guided call after changing a ship's
    // controls (2026-09-24, claude-code:w5-server-06d0). Names are the port's;
    // bodies and instruction maps in server_classes.cpp. Every one tells the
    // players of this system who can see the object (FUN_06cf7370 /
    // FUN_06cf73d0), and the ones with SourceClient skip the player whose id
    // equals it -- the client that asked for the change.
    //
    // FUN_06d0e500 (RET 0x10) -- IClient +0x04 (XActivateEquip). EqObj slot_4c.
    void notify_activate_equip(unsigned int const& ObjId, unsigned short const& EquipId,
                               bool bActivate, int const& SourceClient);
    // FUN_06d0e580 (RET 0x10) -- IClient +0x08 (XActivateCruise). Ship slot_50.
    void notify_cruise(unsigned int const& ObjId, bool bEngage, bool bArg2,
                       int const& SourceClient);
    // FUN_06d0e600 (RET 0xc) -- IClient +0x0c (XActivateThrusters). Ship slot_54.
    void notify_thrusters(unsigned int const& ObjId, bool bActivate, int const& SourceClient);
    // FUN_06d0e670 (RET 0x14) -- IClient +0x10 (XSetTarget). Ship set_target,
    // EqObj slot_44, Guided set_target.
    void notify_set_target(unsigned int const& ObjId, unsigned short const& SubObjId,
                           IObjRW* Target, unsigned short const& SubTarget,
                           int const& SourceClient);
    // FUN_06d0f0c0 (RET 0xc) -- the owner's PlayerData loses the items
    // (FUN_06d4f100), then IClient +0x20 (XJettisonCargo). Ship slot_38.
    void notify_jettison(GameObject* Obj, unsigned short EquipId, int Count);
    // FUN_06d0c970 (RET 8; the name is the port's) -- a ship has gone into a
    // dock (base, gate, lane). Callers: EqObj::process_dock_queue
    // (.text:06ce7b4a / 06ce7b5b) and pub::SpaceObj::InstantDock
    // (.text:06cfa986), each pushing (Dock, ShipObj) so ShipObj is the first
    // argument. ShipObj must be a Ship: the body writes its +0x90 / +0x94.
    //   .text:06d0c978 .. 06d0c985  ShipObj +0x90 (DeathSequenceObjId) != 0 ->
    //                               return (already on its way out)
    //   .text:06d0c993 .. 06d0ca43  every unowned ship in the ship block with a
    //                               behavior interface gets a type-3 behavior
    //                               event {3, &{Dock id, Dock get_type (vt
    //                               +0x88), ShipObj id}} through IBehaviorManager
    //                               slot 0 -- GAP, see the body
    //   .text:06d0ca48 .. 06d0ca85  controller event 0x11 {ShipObj id, Dock id,
    //                               system id}
    //   .text:06d0ca8a .. 06d0cac8  every player view: FUN_06cf6e60(view,
    //                               &ShipObj id, &Dock id)
    //   .text:06d0cacc .. 06d0cad1  ShipObj +0x90 = Dock id
    //   owned ship (.text:06d0cba2 .. 06d0cbe3): Dock type bits 0x40 or 0x800
    //       -> ShipObj set_invincible({2, 1, 1, 0.0f}) (vt +0x194)
    //   unowned, single player (.text:06d0cafb .. 06d0cb32): CShip +0x214 = 0
    //   unowned, multiplayer (.text:06d0cb35 .. 06d0cb9f):
    //       CObject::enable_collisions_r(false), then ShipObj +0x94 =
    //       12.0f (.rdata:06d67cc0) when the Dock's type bits have 0x40 or
    //       0x800, else 0.0f (.rdata:06d67cc4)
    void complete_dock(GameObject* ShipObj, GameObject* Dock);

    // ------------------------------------------------------------------
    // The system-transfer trio (names are the port's). Callers:
    // IServerImpl::SystemSwitchOutComplete (.text:06cef836 / 06cef921 /
    // 06cef934), pub::SpaceObj::JumpIn / Relocate (.text:06cfa2e9 / 06cfa32f /
    // 06cfa34b / 06cfa43c / 06cfa47b) and InstantDock (.text:06cfa61e /
    // 06cfa676).
    //
    // arrive_ship -- FUN_06d0cc00 (RET 0x10), pushed (Obj, JumpObj, &Pos, &Orient):
    //   .text:06d0cc0d .. 06d0cc1c  Bits = JumpObj ? JumpObj->get_type_bits() : 0
    //   .text:06d0cc1e .. 06d0cc47  Obj's CSimple::OwnerPlayer (inlined (& 3)
    //                               accessor, read unguarded); owned -> skip
    //   unowned: Bits 0x40 or 0x800 -> CShip::jump_in(Obj, Pos, Orient, 0.5f)
    //            (.text:06d0cca4 PUSH 0x3f000000; JumpObj argument is Obj
    //            itself, EDI); else Bits 0x400 -> CShip::airlock_in(Pos, Orient);
    //            the CShip is the 0x503-narrowed object, not null-tested
    //   .text:06d0ccb2 .. 06d0cd17  FPayloadServerSystemSwitchIn {Obj id, Bits,
    //                               Pos, MatrixToQuaternion(Orient)} (the
    //                               StarSys.cpp engine, .data:DAT_06d8da08)
    //   .text:06d0cd22 .. 06d0cd4b  every player view with FUN_06cf73d0(view,
    //                               Obj): IClient +0xe8 (SendServerSystemSwitchIn)(node+0x40, &rec)
    void arrive_ship(GameObject* Obj, GameObject* JumpObj, Vector const& Pos, Matrix const& Orient);

    // remove_ship -- FUN_06d0d000 (RET 8), pushed (Obj, OutParms):
    //   .text:06d0d001 .. 06d0d01d  Owner = Obj's CSimple::OwnerPlayer (inlined
    //                               (& 3) accessor, read unguarded)
    //   .text:06d0d023 .. 06d0d045  every player view whose PlayerId (+0x40) is
    //                               not Owner: FUN_06cf6820(view, Obj, 0)
    //   .text:06d0d047 .. 06d0d05c  Server_LookupCacheForget(Obj->get_id())
    //   .text:06d0d060 .. 06d0d077  the first Ships node (head +0x38) holding Obj
    //   .text:06d0d079 .. 06d0d0de  found: MetaList::remove inlined (FreeList
    //                               +0x44 when bUseHeap +0x40, else operator
    //                               delete FUN_06d5ffb0)
    //   .text:06d0d0cd / 06d0d0ea   Obj->leave_system(OutParms) (vt +0x174) on
    //                               both the found and the not-found path
    void remove_ship(GameObject* Obj, PhySys::CreateParms* OutParms);

    // add_ship -- FUN_06d0d100 (RET 0x10), pushed (Obj, Parms, &Pos, &Orient):
    //   .text:06d0d11f .. 06d0d138  Parms +0x14 = SystemInfo->vtable[0]() (the
    //                               system id; Parms not null-tested)
    //   .text:06d0d13b .. 06d0d186  the raw object under the (& 1) test, NOT
    //                               null-tested after it: +0x2c = Pos, then
    //                               +0x08 = Orient (REP MOVSD, 9 dwords)
    //   .text:06d0d188 .. 06d0d196  DALib::Engine vt +0x6c UpdateInstance(its
    //                               +0x04 instance, 0, 0)
    //   .text:06d0d199 .. 06d0d1a7  Obj->enter_system(this, Parms) (vt +0x170)
    //   .text:06d0d1ad .. 06d0d1d2  Ships.append(Obj), inlined (FUN_06d11850)
    //   .text:06d0d1d5 .. 06d0d25b  CShipCreateInfo (ctor FUN_06cf63f0 inlined
    //                               tail); the 0x503-narrowed CShip non-null ->
    //                               FUN_06d07690(&Info, CShip)
    //   .text:06d0d25e .. 06d0d28c  every player view: Info +0xa8 =
    //                               Obj->is_marked_by(PlayerId) (FUN_06cef450),
    //                               then FUN_06cf6630(view, &Info, Obj)
    //   .text:06d0d290 .. 06d0d360  the CShipCreateInfo destructor, inlined
    void add_ship(GameObject* Obj, PhySys::CreateParms* Parms, Vector const& Pos, Matrix const& Orient);

    // launch -- FUN_06d0cee0 (.text:06d0cee0, thiscall, RET 0xc). Retail's own
    // name, from its FDUMP "StarSystem::launch() called on a player!"
    // (StarSys.cpp line 0x9f2). `this` is not read. Only caller:
    // pub::SpaceObj::InstantDock's launch arm (.text:06cfa68c), after add_ship.
    //   .text:06d0cee1 .. 06d0cf01  ShipObj's CSimple::OwnerPlayer (inlined (& 3)
    //                               accessor, read unguarded) != 0 -> FDUMP
    //                               0x100001 "%s(%d) : *** ERROR: %s" and return
    //   .text:06d0cf03 .. 06d0cf37  Dock's object under (& 0x103), null tests on
    //                               Dock and on the object
    //   .text:06d0cf37 .. 06d0cf55  ShipObj's object under (& 0x503), not tested
    //   .text:06d0cf55 .. 06d0cf5e  CShip::launch(ShipObj, the CEqObj, LaunchType)
    //   .text:06d0cf64 .. 06d0cf66  Ship::apply_launch_invincibility (FUN_06d01110)
    void launch(GameObject* ShipObj, GameObject* Dock, int LaunchType);
    // FUN_06d0df40 (RET 0x14) -- controller event 0x16, CShip::use_tradelane(true),
    // the unowned ships' behaviors, IClient +0x18 (XGoTradelane).
    // IServerImpl::GoTradelane and Ship go_tradelane.
    void notify_go_tradelane(unsigned int ShipId, unsigned int EntranceId, unsigned int ExitId,
                             bool bFastExit, float Speed);
    // FUN_06d0e120 (RET 0xc) -- controller event 0x17, CShip::use_tradelane(false),
    // the unowned ships' behaviors, IClient +0x1c. IServerImpl::StopTradelane and
    // Ship stop_tradelane.
    void notify_stop_tradelane(unsigned int ShipId, unsigned int EntranceId, unsigned int ExitId);

    // remove_from_list_d4 -- FUN_06d0c730 (.text:06d0c730, RET 4). Unlinks the
    // first node of ListD4 (+0xD4) whose object is Obj (MetaList::remove,
    // inlined). Caller: EqObj::destroy_notify, once per DockQueue entry.
    void remove_from_list_d4(GameObject* Obj);

    // add_to_list_d4 -- FUN_06d0c6d0 (.text:06d0c6d0, RET 4; the name is the
    // port's). Appends Obj to ListD4 (+0xD4, MetaList append inlined via
    // FUN_06d11850) when its get_type_bits() (vt +0x154) has any bit of 0xfdff;
    // otherwise nothing. Caller: the EqObj dock-queue insert (.text:06ce78c4),
    // once the queue entry is linked.
    void add_to_list_d4(GameObject* Obj);

    // ------------------------------------------------------------------
    // remove_solar -- retail FUN_06d08940 (.text:06d08940, thiscall, RET 4; the
    // name is the port's). Only caller: Solar::destroy (.text:06d02d2c).
    //   .text:06d0894a .. 06d08958  Server_LookupCacheForget(&Obj->get_id())
    //                               (FUN_06d005d0)
    //   .text:06d08960 .. 06d08978  the first Solars node (+0x5c, head +0x60)
    //                               whose Data is Obj; none -> return false
    //   .text:06d0897a .. 06d089d1  MetaList::remove inlined: unlink, then the
    //                               node to FreeList (+0x6c) when bUseHeap (+0x68),
    //                               else its deleting destructor FUN_06d0f9a0
    //   .text:06d089d6 .. 06d089eb  GetBucket(0xfdff) (vt +0xc); non-null ->
    //                               its vtable +0xc (remove) with Obj
    //   .text:06d089ee .. 06d08a20  Deferred (+0xe8) append(Obj) (alloc_node
    //                               FUN_06d11850, linked after the tail)
    //   return true
    bool remove_solar(GameObject* Obj);

    // ------------------------------------------------------------------
    // The ServerEquip launch/notify entry points (declared 2026-09-24 by
    // claude-code:w5-server-06ce for its callers; bodies are the 06d0* lane's).
    // create_mine -- FUN_06d08ef0 (RET 4). Caller SrvMineDropper::launch
    // (.text:06cedafc), one call per ProjLaunchInfo.
    void create_mine(FServerLaunchRecord const& Rec);
    // create_counter_measure -- FUN_06d092c0 (RET 4). Caller
    // SrvCounterMeasureDropper::launch (.text:06cedd5c).
    void create_counter_measure(FServerLaunchRecord const& Rec);
    // create_guided -- FUN_06d09640 (RET 4). Caller SrvGun::launch_guided
    // (.text:06cee1ae).
    void create_guided(FServerLaunchRecord const& Rec);
    // create_bullet -- FUN_06d099e0 (RET 4). Caller SrvGun::launch_bullets
    // (.text:06cee2b6).
    void create_bullet(FServerBulletRecord const& Rec);
    // notify_fire_weapon -- FUN_06d0e2e0 (RET 0x10). Caller SrvGun::fire_forward
    // (.text:06cedfd6): the shooter's id, the one-element id vector, the aim point
    // (rotated into the lock target's frame when there is one) and the client.
    void notify_fire_weapon(unsigned int const& ObjId, std::vector<unsigned short> const& EquipIds,
                            Vector const& Target, int SourceClient);

    // ------------------------------------------------------------------
    // add_cargo / remove_cargo -- retail FUN_06d0e8d0 (.text:06d0e8d0,
    // RET 0x14) and FUN_06d0ebf0 (.text:06d0ebf0, RET 0x0C). The in-space half
    // of pub::Player::AddCargo / RemoveCargo: the docked half writes the
    // PlayerData EquipDescList directly, these two write the LIVE ship object's
    // CEquipManager and then mirror the result back into the slot.
    //
    // Their names are retail's own: the failure path FDUMPs
    //   "StarSys::add_cargo().  Failed to get client cargo description."
    // from E:\FL\Scratch\Source\Server\space\StarSys.cpp line 0xd8e
    // (.text:06d0eba0 .. .text:06d0ebc0).
    //
    // ARITY CORRECTION for remove_cargo: Ghidra declares it
    // `FUN_06d0ebf0(int* param_1, uint param_2)` with the `this` lost, but the
    // epilogue is `RET 0xc` (.text:06d0ed37) -- three stack arguments -- and the
    // third is read at .text:06d0ec56 as `MOV EBX,[ESP + 0x40]`, which resolves
    // to entry+0x0C once the 0x24-byte frame and the four register pushes are
    // accounted for. It is the COUNT; the decompilation passes `param_2` twice
    // (once as the equipment id, once as the amount) because of that.
    //
    // Retail reaches `this` through FUN_06d006c0's third out-parameter (the
    // object's owning container, .text:06d54f10 `LEA EDX,[ESP+0x4]`); this port
    // has no owner chain in its FLID registry, so pub::Player:: calls these
    // through GameObject::OwnerSystem, which is the same StarSystem -- see the
    // member's own comment in server_gameobject.h.
    // ------------------------------------------------------------------
    unsigned short add_cargo(GameObject* Obj, unsigned int ArchId, unsigned int Count,
                             float Health, bool bMission);
    void remove_cargo(GameObject* Obj, unsigned short EquipId, int Count);

    // ------------------------------------------------------------------
    // unload — retail FUN_06d072c0(this). The single-player "last player left,
    // tear the system down" pass that FUN_06d087f0/FUN_06d08890 run after
    // firing event 0x3d. For each of the ten sub-object dispatch blocks it
    // walks the block's node list twice: once issuing a VIRTUAL DELETE on every
    // node whose Data is non-null, then again freeing the nodes through
    // FUN_06d0f970 and nulling the block's head/tail pair.
    //
    // The first pass is not a "notification": .text:06d072fe reads the vtable at
    // Data+0x0c, sets `this` to Data+0x0c and calls slot 0 with the deleting
    // flag 1. Data+0x0c is the CBase sub-object of the IObjRW wrapper (see
    // server_gameobject.h's layout note), so that line destroys the whole
    // object through a secondary base -- `delete static_cast<CBase*>(Obj)`.
    //
    // The whole body, in order (re-read 2026-09-24 against the listing):
    //   .text:06d072cd .. 06d072d7  erase the whole player list (FUN_06d0f390:
    //                               each view's own destructor, the node freed,
    //                               --size);
    //   +0x34, +0x48, +0x70, +0x84, +0x98, +0xac -- each: delete every non-null
    //                               Data, then every node through FUN_06d0f970
    //                               (MetaList::free_node) and Head = Tail = 0;
    //   .text:06d074f0 .. 06d0753b  +0xe8: traverse + delete Data, then
    //                               MetaList::free (FUN_06d17170);
    //   .text:06d07542  FUN_06d07570: the same two passes over +0x5c, then
    //                   FreeSpatial(+0x20) and +0x20 = 0 when it was set;
    //   .text:06d07549  FUN_06d075f0: FUN_06d03410(sysId) (SrvAsteroid_
    //                   UnloadSystem), CmnAsteroid::Unload(sysId), then the two
    //                   passes over +0xc0;
    //   .text:06d07551  FUN_06d0f2f0: the +0x24 vector emptied (_Last = _First);
    //   .text:06d07556 .. 06d0755e  PhySys::Shutdown(sysId).
    // +0xd4 is never touched, and +0xfc is not written (shutdown clears it).
    void unload();

    // ------------------------------------------------------------------
    // queue_dock -- retail FUN_06d0d370 (.text:06d0d370, thiscall, RET 0x10).
    // push_back one FSystemDockEntry onto the dock queue at +0x14:
    //   .text:06d0d390 .. 06d0d3ac  a stack element with both watchers zeroed;
    //   .text:06d0d3ba              BaseWatcher::set(nullptr) on the second
    //                               one (a no-op on a zeroed watcher);
    //   .text:06d0d3c0 .. 06d0d3e2  watcher 1 := Object ? Object + 8 : null,
    //                               set_pointer only when it differs;
    //   .text:06d0d3e8 .. 06d0d402  watcher 2 := Target ? Target + 8 : null,
    //                               the same way;
    //   .text:06d0d408 .. 06d0d41b  +0x10 := *BaseId, +0x14 := *bLand;
    //   .text:06d0d414 .. 06d0d441  operator new(0x20), linked in before the
    //                               sentinel ([this+0x18]) -- a push_back;
    //   .text:06d0d448              FUN_06d0f830: copy the element into node+8;
    //   .text:06d0d44d .. 06d0d454  ++[this+0x1c];
    //   .text:06d0d457 .. 06d0d483  the stack element's watchers are released.
    // Callers: IServerImpl::Dock (.text:06cf1656, Target null, bLand 1),
    // pub::SpaceObj::InstantDock (.text:06cfa9da) and FUN_06ce7930
    // (.text:06ce7ba8, the dock-bay tick of an EqObj).
    // ------------------------------------------------------------------
    void queue_dock(GameObject* Object, GameObject* Target,
                    unsigned int const* BaseId, bool const* bLand);

    // ------------------------------------------------------------------
    // erase_docks -- retail FUN_06d0f410 (.text:06d0f410, thiscall on
    // `this + 0x14`, RET 0xc): std::list<FSystemDockEntry>::erase(First, Last).
    // For every node from First up to (not including) Last: unlink it
    // (.text:06d0f422 .. 06d0f435), destroy the element (FUN_06d0d4a0 at
    // .text:06d0f438), free the node (.text:06d0f43e) and --[this+0x1c]
    // (.text:06d0f443 .. 06d0f44c; unguarded). Returns Last (.text:06d0f452).
    // ------------------------------------------------------------------
    SystemDockNode* erase_docks(SystemDockNode* First, SystemDockNode* Last);

    // ------------------------------------------------------------------
    // shutdown -- retail FUN_06d07130 (.text:06d07130, thiscall, no stack
    // arguments). When the system is loaded: clear +0xfc (.text:06d07141),
    // fire FUN_06d43650(0x3d, &systemId) with the id read off SystemInfo's
    // vtable slot 0 (.text:06d0714a .. 06d07157) and tear the system down with
    // FUN_06d072c0 (StarSystem::unload, .text:06d07161). Nothing when it is
    // not loaded. Callers: the landing block of FUN_06d0be50 (.text:06d0c38d,
    // single player only) and .text:06d1021e.
    // ------------------------------------------------------------------
    void shutdown();
};

static const unsigned int STARSYSTEM_SIZE = 0x100;

static_assert(sizeof(StarSystem) == 0x100, "StarSystem is retail's 0x100 bytes");
static_assert(offsetof(StarSystem, SystemInfo) == 0x04, "StarSystem+0x04 SystemInfo");
static_assert(offsetof(StarSystem, PlayerListHead) == 0x0C, "StarSystem+0x0C player list head");

// FUN_06d0f1c0 (.text:06d0f1c0, thiscall, `RET 0x4`) -- SystemPlayerView_SendGroupChange
// (FUN_06cf70c0) on every view in the system's player list. Callers
// CPlayerGroup::AddMember / DelMember after they stamp a ship's group id.
void StarSystem_SendGroupChange(StarSystem& System, unsigned int ObjId);

// FUN_06d0b110 (.text:06d0b110, thiscall, `RET 0x4`) -- SystemPlayerView_SendRemoteCargoUpdate
// (FUN_06cf6730) on every view in the system's player list whose PlayerId is not
// Obj's own CSimple::OwnerPlayer. Only caller: IServerImpl::AcceptTrade
// (.text:06cf1eed / .text:06cf1ef9), once per trading ship.
void StarSystem_SendRemoteCargoUpdate(StarSystem& System, GameObject* Obj);
static_assert(offsetof(StarSystem, ObjectListHead) == 0x18, "StarSystem+0x18 dock queue head");
static_assert(offsetof(StarSystem, Spatial) == 0x20, "StarSystem+0x20 spatial partition");
static_assert(offsetof(StarSystem, VectorBegin) == 0x28, "StarSystem+0x28 vector _First");
static_assert(offsetof(StarSystem, Ships) == 0x34, "StarSystem+0x34 first ObjectList");
static_assert(offsetof(StarSystem, Solars) == 0x5C, "StarSystem+0x5C solar ObjectList");
static_assert(offsetof(StarSystem, Deferred) == 0xE8, "StarSystem+0xE8 last ObjectList");
static_assert(offsetof(StarSystem, bLoaded) == 0xFC, "StarSystem+0xFC loaded flag");

// Backward compatibility alias for existing code
typedef StarSystem FStarSystem;

// SysDB::SysMap -- retail .data RVA 0xADA2C, export ordinal 460:
//   ?SysMap@SysDB@@3V?$map@IVStarSystem@@U?$less@I@std@@V?$allocator@VStarSystem@@@3@@std@@A
// VC6 spelled the default allocator with the MAPPED type (allocator<StarSystem>).
// Ours is the same map as an ordinary modern std::map with its default allocator,
// so its decorated name differs from retail's on purpose (project rule, 2026-09-24).
namespace SysDB {
    typedef std::map<unsigned int, StarSystem> SysMapType;
    extern SERVER_API SysMapType SysMap;
}

// FUN_06d0f190 (.text:06d0f190, `RET 0x8`, thiscall on the StarSystem) -- find
// PlayerId's node in the system's player list (node+0x40 == View.PlayerId) and
// run SystemPlayerView_CollectCreatedOwners on its view. No match: nothing.
void StarSystem_CollectCreatedOwners(StarSystem* System, unsigned int PlayerId,
                                     std::set<unsigned int>* Owners);

// SysDB lookup — equivalent of retail FUN_06d00280
// Returns FStarSystem* for given system_id, or nullptr if not found
FStarSystem* SysDB_LookupSystem(unsigned int system_id);

// SysDB init — equivalent of retail FUN_06d0fb40
// Creates FStarSystem entries for all Universe systems
void SysDB_Initialize();

// SysDB cleanup — equivalent of retail FUN_06d0fa60
void SysDB_Shutdown();

// Mark system as loaded/active
void SysDB_SetSystemLoaded(unsigned int system_id, bool loaded);

// DAT_06d88a80 (.data, initial value 1). Written once, by FUN_06cfffd0
// (.text:06cfffd9), from the byte at IServer::Startup's info +0x210
// (.text:06d44c5e `MOV AL,byte ptr [ESI + 0x210]`). Read only by three
// StarSystem members, each as `if (!flag && obj && obj->vt[+0xbc]()) ...`:
// FUN_06d0b260 (.text:06d0b4fd, explosion), FUN_06d0d500 (.text:06d0d53a,
// munition collision) and FUN_06d0d730 (.text:06d0d75e, object collision).
// Server.dll does not name it; FLServer.exe does: its Server Configuration dialog
// (.text:00406b70 -> FServerControl .text:00402390) fills info +0x210 from the
// "Allow Players to Harm Other Players" checkbox, and the three readers drop
// player-on-player damage while it is 0. Defined in src/Server/IServerImpl.cpp.
extern unsigned char GbAllowPvp;

// The one-entry cache of FUN_06d006c0 (the object lookup by id; see
// server_gameobject.h, Server_LookupGameObject). Retail's storage, defined in
// src/Server/IServerImpl.cpp. Readers and writers: FUN_06d006c0 (Server_FindObject),
// FUN_06d005d0 (Server_LookupCacheForget), FUN_06d00380 / FUN_06d00410
// (Server_TakeInTransit / Server_DestroyInTransit), all in server_classes.cpp,
// and FUN_06cffff0 (Server_CleanupPhysics, IServerImpl.cpp).
//   DAT_06d8d8ec  watcher on the last object found (its Watchable, IObjRW+8);
//                 FUN_06d006c0 .text:06d006dc reads it, -8 back to the IObjRW.
//                 CRT init LAB_06cfff90 (FUN_06d00d50(0): both dwords zero),
//                 atexit FUN_06cfffb0 -> FUN_06cfffc0 (set_pointer(0) when
//                 something is watched).
//   DAT_06d8d92c  the StarSystem that object was found in (.text:06d0071d);
//                 zeroed by FUN_06cffff0 (.text:06cffffa) at server shutdown.
//   DAT_06d8d930 / DAT_06d8d934  hit / miss counters (.text:06d00715 /
//                 .text:06d0072c), never read by anything else.
struct BaseWatcher;
extern BaseWatcher GLookupCacheObject;
extern StarSystem* GLookupCacheSystem;
extern unsigned int GLookupCacheHits;
extern unsigned int GLookupCacheMisses;

// FUN_06d005d0 (.text:06d005d0, cdecl, one argument; the name is the port's).
// Forgets the cached object when it is the one with this id: the watcher's
// Watchable - 8 is the wrapper, its CBase::Object under the (& 3) == 3 guard
// gives the CSimple id (+0xb0, read with no null test on the guard's result);
// equal -> BaseWatcher::set_pointer(0) on GLookupCacheObject and
// GLookupCacheSystem = 0. Callers: StarSystem::remove_solar (.text:06d08958)
// and FUN_06d0d000 (StarSystem object removal).
void Server_LookupCacheForget(unsigned int const& ObjId);

// ---------------------------------------------------------------------------
// FUN_06d0b800 (.text:06d0b800, `RET 0x8`) -- "this object has just taken
// damage". Ported 2026-09-17 (wave 14) in src/Server/server_observer.cpp.
//
// Retail's three callers (.text:06ceb4bf, .text:06ceef18, .text:06cf28bb) all
// reach it as a thiscall on a StarSystem with (IObjRW*, DamageList*). It does
// two unrelated things:
//   * when the damaged object IS the current ship of a valid player, it walks
//     the DamageList and mirrors every entry into that player's own PlayerData
//     -- equipment through CEquipManager::FindByID + PlayerData_ModifyEquipment
//     / PlayerData_RemoveEquipment, collision groups through
//     CArchGroupManager::FindByID + PlayerData_SetCollisionGroupHealth, and the
//     root subobject through PlayerData_SetRelativeHealth;
//   * then it tells every player in the system, via
//     SystemPlayerView_DamageObject. For a SOLAR it first builds a reduced
//     DamageList holding only the entries whose Fate != None, and that reduced
//     list is the one a player who cannot currently see the object receives.
void StarSystem_DamageObject(StarSystem* Sys, GameObject* Obj, DamageList* Damage);
