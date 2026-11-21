// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_playerview.cpp -- the per-player Observer (SystemPlayerView_*) and its tracked list.
// Split out of server_classes.cpp unchanged; the declarations the split files share are in
// server_classes_internal.h.

#include "server_pub.h"
#include "common_net.h"
#include "common_physys.h"
#include "ivp_physics/ivp_real_object.h" // IVP_Real_Object +0x1c/+0xa4 (StarSystem_MpCollisionFilter)
#include "common_cequip.h" // TractorArm
#include "common_casteroid.h" // CmnAsteroid::CAsteroidSystem (SrvAsteroidSystem AddRef/Release/get_sys_id)
#include "common_id.h"
#include "common_server_playerdb.h"
#include "server_savegame.h" // PlayerData_CreateNewCharacter (retail FUN_06d4b790)
#include "server_savefile.h" // Server_AccountDirName / Server_GetAccountsRoot (FUN_06d49f60)
#include "common_ini.h"
#include "common_misc.h"
#include "x86math.h" // I3DMathEngine
#include "common_universe.h"
#include "common_reputation.h"
#include "server_sysdb.h"
#include "server_gameobject.h" // GameObject's +0x154..+0x160 slots (StarSystem::remove_player/unload)
#include "common_cobjdb.h"  // IObjDB::GetZoneDb (SrvAsteroid_UpdateSystem, FUN_06d03480)
#include "DALib.h"          // DALib::Data (SrvAsteroid's GetFileSysPath calls)
#include "readfile.h"       // IFileSystem::GetFileSysPath (+0x84), create_restart_file
#include "server_createinfo.h"
#include "server_characterset.h" // CCharacterSet (FUN_06d499c0)
#include "common_goods.h"    // AmbientScriptProperties (server_basedb.h uses it by value)
#include "server_basedb.h"
#include "rpc_iclient.h"
#include "rpc_iclient_args.h" // FServerSetReputationInfo / FPayloadServerActivateObject (add_player MP walk)
#include "md5hash.h"      // MD5HASH_API: Server exports its implicit members like retail
#include <windows.h>
#include <mmsystem.h>
#include "../include/platform.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <iterator>
#include <list>
#include <map>
#include <set>
#include <new>          // placement new: Observer is constructed into a raw
                        // player-list node, as std::list's _Construct does
#include <io.h>
#include <direct.h>
#include <wchar.h>
#include <stdarg.h>
#include <algorithm>
#include <unordered_map> // PORT(perf-index): the ObjectList blocks' id index (find_by_id)
#include <cassert>       // PORT(perf-index): the id index's _DEBUG cross-check
#include "server_classes_internal.h"

// ===========================================================================
// ===========================================================================
// The per-player object tracker that hangs off StarSystem+0x0C.
// Structures and the byte trail are in server_sysdb.h.
// ===========================================================================

// The three squared radii FUN_06cf7260 tests against. Read as raw bytes off the
// listing, not guessed: .rdata:06d66aec = 20 bc be 4a = 0x4abebc20 = 6.25e6,
// .rdata:06d66af0 = 0x4bbebc20 = 2.5e7, .rdata:06d66af4 = 0x4cbebc20 = 1.0e8.
// They are 2500^2, 5000^2 and 10000^2.
static float const kViewRadiusSqUnowned = 6.25e6f;    // .rdata:06d66aec
static float const kViewRadiusSqStranger = 2.5e7f;    // .rdata:06d66af0
static float const kViewRadiusSqGroupMate = 1.0e8f;   // .rdata:06d66af4

// ---------------------------------------------------------------------------
// The mask FUN_06cf6820's third exit tests GameObject::get_type_bits against:
//   .text:06cf6905  ff9254010000  CALL dword ptr [EDX + 0x154]
//   .text:06cf690b  a9fffd0000    TEST EAX,0xfdff
//   .text:06cf6910  7417          JZ   LAB_06cf6929        ; silent return
//
// 0xFDFF is not an arbitrary number: it is every OBJECT_TYPE bit below 0x10000
// EXCEPT 0x200. The bit values are not guessed -- they come out of Common.dll's
// own name/value table at .data:063ec810, an array of 0xE8/8 = 29
// {const char*, unsigned int} pairs that both DecodeType (?DecodeType@@YAIPBD@Z,
// Common.dll 0x8f3d0, the `type =` ini handler's lookup at .text:062f06e4)
// walk with `ADD ESI,0x8` until `CMP EDI,0xe8`:
//
//   NONE 0x00000000   MOON 0x00000001   PLANET 0x00000002   SUN  0x00000004
//   BLACKHOLE 0x08    SATELLITE 0x10    DOCKING_RING 0x20   JUMP_GATE 0x40
//   TRADELANE_RING 0x80   STATION 0x100   WAYPOINT 0x200    AIRLOCK_GATE 0x400
//   JUMP_HOLE 0x800   WEAPONS_PLATFORM 0x1000   DESTROYABLE_DEPOT 0x2000
//   NON_TARGETABLE 0x4000   MISSION_SATELLITE 0x8000
//   FIGHTER 0x00010000   FREIGHTER 0x00020000   GUNBOAT 0x00040000
//   CRUISER 0x00080000   TRANSPORT 0x00100000   CAPITAL 0x00200000
//   MINING  0x00400000   GUIDED 0x01000000   BULLET 0x02000000
//   MINE    0x04000000   LOOT   0x10000000   ASTEROID 0x20000000
//
// OR the low-16 group together and drop WAYPOINT and you get exactly 0xFDFF.
// So the predicate reads "this is a SOLAR, and not a waypoint": the static
// furniture of the system, which a client instantiates from the system ini
// rather than from a server create-message. That is precisely why retail still
// sends the destroy notification for an object that was never in this player's
// tracked list -- the client has it anyway. Ships, projectiles, mines, loot and
// asteroids are all outside the mask and stay silent.
static unsigned int const kSolarTypeBits = 0x0000FDFFu;

// ---------------------------------------------------------------------------
// PORT(perf-index): index of an Observer's tracked list (no retail counterpart).
//
// Retail answers "is this object in the player's view" with a walk of the
// view's tracked list: FUN_06cf73d0 compares each record's object pointer,
// FUN_06cf65b0 / FUN_06cf6820 / FUN_06cf6e60 / FUN_06cf6fc0 call get_id on each
// record's object. A view tracks every object of its system, and
// StarSystem::sp_obj_update fans every position update out to every other
// player in the system, so those walks cost players^2 x objects per update
// round. The index below answers the same questions from two hash maps.
//
// Why the answer is the walk's answer:
//   - The list is only ever appended to by SystemPlayerView_AddTrackedObject
//     and shrunk by the four removals (~Observer, OnObjectDestroyed,
//     OnObjectSwitchedOut, OnObjectLanded); each one updates the index. The
//     list stays the source of every order.
//   - A record's Object is written once, before the append, and its id cannot
//     change while it is tracked (see ObjectList::find_by_id's index: CSimple::Id
//     is only written by CSimple::init, which runs before any append).
//   - The walks return the FIRST matching node. While each object and each id
//     occurs at most once, the first match is the only match. The moment an
//     append would repeat an object or an id, or would add a record whose id
//     the walk could not read (null object, not a CSimple), the index stops
//     answering and every lookup walks, exactly as in retail, until the list is
//     empty again. A copy of a non-empty list (Observer's copy constructor) also
//     starts in that state.
//   - Lookups were never read-only in this module's callers, and the server is
//     single-threaded, so the index needs no locking.
// ---------------------------------------------------------------------------
namespace {

struct FTrackedListIndex {
    std::unordered_map<GameObject const*, SystemTrackedNode*> ByObject;
    std::unordered_map<unsigned int, SystemTrackedNode*> ById;
    std::unordered_map<SystemTrackedNode const*, unsigned int> IdOfNode;
    bool bUsable;

    FTrackedListIndex() : bUsable(true) {}
};

typedef std::unordered_map<FObserverTrackedList const*, FTrackedListIndex> FTrackedListIndexTable;

// Created on first use and never freed: views are destroyed during static
// teardown (SysDB::SysMap) and still reach TrackedIndex_Drop.
FTrackedListIndexTable* GTrackedListIndexes = nullptr;

FTrackedListIndex& TrackedIndex_Get(FObserverTrackedList const& List)
{
    if (GTrackedListIndexes == nullptr)
        GTrackedListIndexes = new FTrackedListIndexTable;
    return (*GTrackedListIndexes)[&List];
}

// A fresh list (empty: usable) or a copy (usable only when empty).
void TrackedIndex_Reset(FObserverTrackedList const& List)
{
    FTrackedListIndex& Index = TrackedIndex_Get(List);
    Index.ByObject.clear();
    Index.ById.clear();
    Index.IdOfNode.clear();
    Index.bUsable = (List.Size == 0);
}

void TrackedIndex_Drop(FObserverTrackedList const& List)
{
    if (GTrackedListIndexes != nullptr)
        GTrackedListIndexes->erase(&List);
}

// Called right after Node was appended (its record and object already set).
void TrackedIndex_OnAppend(FObserverTrackedList const& List, SystemTrackedNode* Node)
{
    FTrackedListIndex& Index = TrackedIndex_Get(List);
    if (!Index.bUsable)
        return;
    GameObject* const Obj = Node->Data->Object;
    CObject* const Raw = (Obj != nullptr) ? Obj->Object : nullptr;
    if (Raw == nullptr || (static_cast<unsigned int>(Raw->ObjectClass) & 3u) != 3u) {
        Index.bUsable = false;
        return;
    }
    unsigned int const Id = static_cast<unsigned int>(Obj->get_id());
    if (Index.ByObject.find(Obj) != Index.ByObject.end() || Index.ById.find(Id) != Index.ById.end()) {
        Index.bUsable = false;
        return;
    }
    Index.ByObject.insert(std::make_pair(static_cast<GameObject const*>(Obj), Node));
    Index.ById.insert(std::make_pair(Id, Node));
    Index.IdOfNode.insert(std::make_pair(static_cast<SystemTrackedNode const*>(Node), Id));
}

// Called before Node is unlinked, while Node->Data is still the record.
void TrackedIndex_OnRemove(FObserverTrackedList const& List, SystemTrackedNode const* Node)
{
    FTrackedListIndex& Index = TrackedIndex_Get(List);
    if (Index.bUsable) {
        std::unordered_map<SystemTrackedNode const*, unsigned int>::iterator const It =
            Index.IdOfNode.find(Node);
        if (It != Index.IdOfNode.end()) {
            Index.ById.erase(It->second);
            Index.ByObject.erase(Node->Data->Object);
            Index.IdOfNode.erase(It);
        }
    } else if (List.Size == 1) {
        // The list becomes empty: every lookup can be answered again.
        Index.ByObject.clear();
        Index.ById.clear();
        Index.IdOfNode.clear();
        Index.bUsable = true;
    }
}

// The answer of a first-match walk, or Walk when the index cannot give it.
// Returns true when Out holds the answer (null: no such record).
bool TrackedIndex_FindByObject(FObserverTrackedList const& List, GameObject const* Obj,
                               SystemTrackedNode*& Out)
{
    FTrackedListIndex& Index = TrackedIndex_Get(List);
    if (!Index.bUsable)
        return false;
    std::unordered_map<GameObject const*, SystemTrackedNode*>::const_iterator const It =
        Index.ByObject.find(Obj);
    Out = (It != Index.ByObject.end()) ? It->second : nullptr;
    return true;
}

bool TrackedIndex_FindById(FObserverTrackedList const& List, unsigned int Id, SystemTrackedNode*& Out)
{
    FTrackedListIndex& Index = TrackedIndex_Get(List);
    if (!Index.bUsable)
        return false;
    std::unordered_map<unsigned int, SystemTrackedNode*>::const_iterator const It = Index.ById.find(Id);
    Out = (It != Index.ById.end()) ? It->second : nullptr;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// FObserverTrackedList -- Observer+0x08, retail's std::list<SystemTrackedObject*>.
// Its three members are the list's share of Observer's special members; see
// server_sysdb.h for the address ranges.
// ---------------------------------------------------------------------------

// .text:06cf5205 .. 06cf5217 (inside FUN_06cf51e0).
FObserverTrackedList::FObserverTrackedList()
{
    // .text:06cf5205 -- in retail the allocator byte is a copy of a leftover
    // argument byte; it carries no value, the port writes 0.
    Alloc = 0;
    // .text:06cf5208 -- operator new(0xc), then `MOV [EAX],EAX` /
    // `MOV [EAX+4],EAX`: a self-referencing sentinel, the empty list.
    SystemTrackedNode* const Sentinel = new SystemTrackedNode;
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    Head = Sentinel;                  // .text:06cf5214
    Size = 0;                         // .text:06cf5217
    TrackedIndex_Reset(*this); // PORT(perf-index) (see the index above)
}

// .text:06d0f705 .. 06d0f778 (inside FUN_06d0f6d0).
FObserverTrackedList::FObserverTrackedList(FObserverTrackedList const& Source)
{
    Alloc = Source.Alloc;             // 06d0f705
    // 06d0f70c -- operator new(0xc), self-linked; size 0.
    SystemTrackedNode* const Sentinel = new SystemTrackedNode;
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    Head = Sentinel;
    Size = 0;

    // 06d0f740 .. 06d0f778 -- for every source node a new node linked before
    // the sentinel (FUN_06cfc340), its Data copied from the source node's Data
    // (FUN_06d06720 is a pointer copy -- the RECORD is shared, not
    // duplicated), ++size.
    SystemTrackedNode* const SrcSentinel = Source.Head;
    for (SystemTrackedNode* Src = SrcSentinel->Next; Src != SrcSentinel; Src = Src->Next) {
        SystemTrackedNode* const Node = new SystemTrackedNode;
        Node->Next = Sentinel;
        Node->Prev = Sentinel->Prev;
        Sentinel->Prev = Node;
        Node->Prev->Next = Node;
        Node->Data = Src->Data;
        ++Size;
    }
    TrackedIndex_Reset(*this); // PORT(perf-index): usable only when the copy is empty
}

// .text:06cf538d .. 06cf53ca (the tail of FUN_06cf52d0). MSVC6's ~list():
// erase(begin(), end()) with FUN_06d0f620, free the sentinel, head = size = 0.
// ~Observer's body has already emptied the list, so the walk finds nothing --
// in retail as here.
FObserverTrackedList::~FObserverTrackedList()
{
    SystemTrackedNode* const Sentinel = Head;
    // .text:06cf5390 `MOV ESI,[EBP]` / .text:06cf5396 `CMP ESI,EBP / JZ`.
    SystemTrackedNode* Node = Sentinel->Next;
    while (Node != Sentinel) {
        // FUN_06d0f620 -- list::erase(node): unlink, free, --size.
        SystemTrackedNode* const Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        --Size;
        Node = Next;
    }
    delete Sentinel;                  // .text:06cf53bb
    Head = nullptr;                   // .text:06cf53c7
    Size = 0;                         // .text:06cf53ca
    TrackedIndex_Drop(*this); // PORT(perf-index) (see the index above)
}

// ---------------------------------------------------------------------------
// FUN_06cf51e0 (.text:06cf51e0, thiscall, `RET 0x4`). Observer's constructor.
// Member construction in declaration order is retail's order:
//   .text:06cf5205 .. 06cf5217  +0x08 the tracked list (FObserverTrackedList)
//   .text:06cf5228 .. 06cf5278  +0x14 the _Tree: allocator bytes +0x14/+0x15,
//                               _Multi +0x20 = 0, _Nil (+0x1C) and _Head (+0x18)
//                               nodes, _Size +0x24 = 0 -- std::set's own ctor
//   .text:06cf527b .. 06cf5290  +0x28 = 0, the double +0x30 = 0 (two dword
//                               zeros), +0x38 = the argument, +0x3C..+0x44 = 0
//   .text:06cf5294              vptr = .rdata:06d66988 (the compiler's own store)
// +0x04 and +0x2C are not written by retail and are not written here.
// ---------------------------------------------------------------------------
Observer::Observer(unsigned int InPlayerId)
    : Tracked()
    , PendingDamageIds()
{
    bShipGone = 0;                    // .text:06cf527b
    ElapsedTime = 0.0;                // .text:06cf527e / 06cf5281
    PlayerId = InPlayerId;            // .text:06cf5284
    Position.x = 0.0f;                // .text:06cf528a
    Position.y = 0.0f;                // .text:06cf528d
    Position.z = 0.0f;                // .text:06cf5290
}

// ---------------------------------------------------------------------------
// FUN_06d0f6d0 (.text:06d0f6d0, cdecl (Dest, Source)) -- Observer's copy
// constructor, compiled as std::list's _Construct(p, v), i.e. a placement new.
// The `CMP EDI,EBP / JZ` at .text:06d0f6f4 is placement-new's null test on
// Dest; the port reaches this only through a placement new on a live list
// node. Callers: FUN_06d0f330 (.text:06d0f365, list::insert, from
// StarSystem::add_player) and FUN_06d109c0 (.text:06d10a08, StarSystem's copy
// constructor).
//   06d0f705 .. 06d0f778  +0x08 the tracked list (FObserverTrackedList copy)
//   06d0f77a .. 06d0f7df  +0x14 the _Tree: allocator bytes and _Multi copied,
//                         fresh _Nil/_Head, FUN_06ce37a0 = the tree copy --
//                         std::set's copy constructor
//   06d0f7e4 .. 06d0f80f  +0x28, the double at +0x30, +0x38 and the Vector at
//                         +0x3c copied
//   06d0f813              vptr = .rdata:06d66988
// +0x04 and +0x2c are not written.
// ---------------------------------------------------------------------------
Observer::Observer(Observer const& Source)
    : Tracked(Source.Tracked)
    , PendingDamageIds(Source.PendingDamageIds)
{
    bShipGone = Source.bShipGone;     // 06d0f7e4
    ElapsedTime = Source.ElapsedTime; // 06d0f7ea .. 06d0f7f3
    PlayerId = Source.PlayerId;       // 06d0f7f6
    Position = Source.Position;       // 06d0f7ff .. 06d0f810
}

// ---------------------------------------------------------------------------
// FUN_06cf52d0 (.text:06cf52d0). ~Observer. Slot 0 of .rdata:06d66988 is the
// compiler's scalar deleting destructor around it (FUN_06cf52b0: this body,
// then operator delete when flag & 1); every retail dispatch passes flag 0.
//   .text:06cf52f2          vptr restamp (the compiler's own store)
//   .text:06cf52f9 .. 532d  body, pass 1: every RECORD destroyed
//   .text:06cf532f .. 5353  body, pass 2: FUN_06d0f620 list::erase on every node
//   .text:06cf535d .. 5385  ~set on +0x14 (FUN_06cf7540 erase all, free _Head
//                           and _Nil, clear) -- the member declared last is
//                           destroyed first
//   .text:06cf538d .. 53ca  ~FObserverTrackedList on +0x08
// ---------------------------------------------------------------------------
Observer::~Observer()
{
    // (w4-server-guards) No null test on the list head: retail reads
    // [this + 0xc] and derefs it unconditionally (.text:06cf52f9 `MOV EAX,[EBP
    // + 0xc]` / .text:06cf52fc `MOV ESI,[EAX]`), and both constructors always
    // allocate the sentinel.
    SystemTrackedNode* const Sentinel = Tracked.Head;
    SystemTrackedNode* Node;
    // Pass 1, .text:06cf52f9 .. 06cf532d: every RECORD. .text:06cf52fe `CMP
    // ESI,EAX / JZ` and .text:06cf532a `CMP ESI,[EBP+0xc] / JNZ` -- the walk
    // stops only at the sentinel.
    for (Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        // `delete` runs ~CRemotePhysicsSimulation on the embedded member
        // (.text:06cf5317 LEA ECX,[EDI+0x8] / CALL) and frees the record
        // (.text:06cf5320). Retail's `TEST EDI,EDI / JZ` (.text:06cf5313) is
        // delete's own null test.
        delete Node->Data;
    }
    // Pass 2, .text:06cf532f .. 06cf5353: FUN_06d0f620 = list::erase(node) on
    // every node -- unlink, free, --size. The PORT(perf-index) index forgets them all
    // first (pass 1 freed the records it would otherwise read).
    TrackedIndex_Drop(Tracked);
    Node = Sentinel->Next;
    while (Node != Sentinel) {
        SystemTrackedNode* const Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        --Tracked.Size;
        Node = Next;
    }
    // The members follow: ~PendingDamageIds (.text:06cf535d .. 06cf5385), then
    // ~Tracked (.text:06cf538d .. 06cf53ca).
}

// ---------------------------------------------------------------------------
// FUN_06cf7140 (.text:06cf7140, `RET 0x4`). The only producer the tracked-object
// list has in retail.
// ---------------------------------------------------------------------------
SystemTrackedObject* SystemPlayerView_AddTrackedObject(Observer* View, GameObject* Obj)
{
    // No null test on the view or on its list head: retail reads [this+0xc]
    // unconditionally at .text:06cf71b4, and every view this is called on has
    // been through an Observer constructor, both of which always allocate the
    // sentinel (.text:06cf5208 / .text:06cf5214, 06d0f70c).

    // .text:06cf715e  PUSH 0x2f8 / operator new; .text:06cf7181 constructs the
    // CRemotePhysicsSimulation in place at +0x08; .text:06cf7189 zeroes the
    // object pointer and .text:06cf718f the created-on-client byte. +0x04 is
    // never written (see SystemTrackedObject in server_sysdb.h).
    SystemTrackedObject* const Record = new SystemTrackedObject;
    Record->Object = nullptr;
    Record->bCreatedOnClient = false;

    // .text:06cf71a6  MOV [ESI],EBP -- the real object, stored after the zero.
    Record->Object = Obj;

    // .text:06cf71a8 / .text:06cf71ae -- the flag is seeded from SinglePlayer(),
    // not from false: in single player the client already has everything, so the
    // record counts as created the moment it exists.
    Record->bCreatedOnClient = SinglePlayer();

    // .text:06cf71b4 .. .text:06cf71de -- push_back onto the sentinel list.
    SystemTrackedNode* const Sentinel = View->Tracked.Head;
    SystemTrackedNode* const Node = new SystemTrackedNode;
    Node->Next = Sentinel;
    Node->Prev = Sentinel->Prev;
    Node->Data = Record;
    Sentinel->Prev->Next = Node;
    Sentinel->Prev = Node;

    // .text:06cf71e4  INC dword ptr [EDI + 0x10]
    ++View->Tracked.Size;
    TrackedIndex_OnAppend(View->Tracked, Node); // PORT(perf-index) (see the index above)

    // .text:06cf71e7 .. .text:06cf7237 -- outside single player, prime the
    // simulation with the object's current state. SinglePlayer() is called a
    // SECOND time here (.text:06cf71e7 / TEST AL,AL / JNZ .text:06cf723a), not
    // reused from the flag above.
    //   .text:06cf71f1  MOV EDX,[EBP] / CALL [EDX]       -> EBX: IObjInspect slot 0,
    //                                                       get_position()
    //   .text:06cf71fa  MOV EAX,[EBP] / CALL [EAX+0xc]   -> EBP: IObjInspect slot 3,
    //                                                       get_transform() (ICF-
    //                                                       folded with slot 4,
    //                                                       get_orientation, at
    //                                                       Common 0x062bf1f0; the
    //                                                       pointer is used as the
    //                                                       Matrix at Transform+0)
    //   .text:06cf7204 .. .text:06cf7212  DAT_06d8d7f8, the lazily cached
    //                                     "3DMathEngine" (FUN_06d5ff70 is
    //                                     DACOM_Acquire + QueryInterface)
    //   .text:06cf7220  CALL [ECX+0x50]   I3DMathEngine::MatrixToQuaternion
    //                                     (engine, &Local /*[ESP+0x18]*/, *EBP)
    //   .text:06cf7237  CALL [EAX+0x4]    ??_7CRemotePhysicsSimulation slot 1,
    //                                     RemoteUpdate(*EBX, Local,
    //                                     View->ElapsedTime) on record+0x08.
    // Stack check for the quaternion: the LEA at .text:06cf721a sits one PUSH
    // below the frame (ESP+0x1c -> frame+0x18) and the LEA at .text:06cf722e two
    // PUSHes below it after the callee-cleaned COM call returned (ESP+0x20 ->
    // frame+0x18): the same local. The two PUSHes at .text:06cf7223/06cf722a are
    // View+0x34 then View+0x30 -- one double, ElapsedTime.
    // The engine is DAT_06d8d7f8, Observer.cpp's file-static, which this
    // function shares with the observer update -- Observer_MatrixToQuat
    // (server_observer.cpp) is that lazy fetch through Server.dll's own
    // FUN_06d5ff70. Retail never checks the engine for null and neither does
    // this.
    if (!SinglePlayer())
    {
        Vector const& Position = Obj->get_position();          // slot 0
        Transform const& Xform = Obj->get_transform();         // slot 3
        Quaternion Orientation;
        Observer_MatrixToQuat(Orientation, Xform.Rotation);
        Record->Simulation.RemoteUpdate(Position, Orientation, View->ElapsedTime);
    }

    // .text:06cf723f  MOV EAX,ESI -- retail returns the record.
    return Record;
}

// FUN_06cf7260 (.text:06cf7260), `RET 0x4`.
bool SystemPlayerView_ShouldBeVisible(Observer* View, GameObject* Obj)
{
    // .text:06cf7265 / .text:06cf726a / .text:06cf7278 -- a null object and
    // single player are both unconditionally "visible"; the tracker is a
    // multiplayer-only filter. There is no test on the view: this is a
    // thiscall and EDI = ECX is dereferenced below.
    if (Obj == nullptr)
        return true;
    if (SinglePlayer())
        return true;

    // .text:06cf727e .. .text:06cf7292 -- the inlined CSimple accessor, with
    // retail's own shape: CBase::Object at +0x10, nulled unless
    // (ObjectClass & 3) == 3 (.text:06cf7290 `XOR EAX,EAX`), and then
    // CSimple::OwnerPlayer (+0xb4) read THROUGH the result with no test. Every
    // object a Observer tracks is a CSimple, so retail never reaches
    // the null read; the port does not guard it either.
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;

    // .text:06cf7298 -- your own ship is never filtered.
    if (OwnerPlayer == View->PlayerId)
        return true;

    // .text:06cf72a1 `MOV EAX,[ESI]` / .text:06cf72a5 `CALL [EAX]` -- the
    // wrapper's own vtable slot 0, IObjInspect::get_position, and then FLD off
    // the returned pointer (.text:06cf72a7 / 06cf72b1 / 06cf72b7).
    Vector const& ObjPos = Obj->get_position();

    // .text:06cf72a7 .. .text:06cf72d1 -- the squared distance, computed but
    // not yet compared; retail keeps it on the x87 stack across the group test.
    float const Dx = ObjPos.x - View->Position.x;
    float const Dy = ObjPos.y - View->Position.y;
    float const Dz = ObjPos.z - View->Position.z;
    float const DistSq = Dx * Dx + Dy * Dy + Dz * Dz;

    // .text:06cf72d3 .. .text:06cf72ee -- the owner is re-read through the
    // same inlined accessor; an object nobody owns gets the tightest radius.
    // Every compare is `FCOM/FCOMP` + `TEST AH,0x41` + `JP`, i.e. "visible when
    // DistSq <= radius" (C0 or C3 set), and a NaN (C0 = C2 = C3) is not.
    if (OwnerPlayer == 0)
        return DistSq <= kViewRadiusSqUnowned;           // .text:06cf7354

    // .text:06cf72f0 .. .text:06cf7328 -- the group widening.
    //   .text:06cf72f0  MOV  EDX,[EDI + 0x38]                ; this view's player
    //   .text:06cf72f3  MOV  ECX,[.data:Players]
    //   .text:06cf72f9  IMUL EDX,EDX,0x418
    //   .text:06cf72ff  MOV  EAX,[EDX + ECX*0x1 + -0x14]     ; my group
    //   .text:06cf7303  TEST EAX,EAX / JZ  -> stranger radius
    //   .text:06cf7318  MOV  EDX,[ESI + 0xb4]                ; the owner
    //   .text:06cf731e  IMUL EDX,EDX,0x418
    //   .text:06cf7324  CMP  EAX,[EDX + ECX*0x1 + -0x14]     ; his group
    //   .text:06cf7328  JNZ  -> stranger radius
    //   .text:06cf732a  FCOM [.rdata:06d66af4]               ; 10000^2
    // 0x418 - 0x14 = 0x404 = PlayerData::group_ptr, the CPlayerGroup*. Both
    // slots are indexed raw, with no PlayerDB::is_valid -- PlayerDB::operator[]
    // is the same raw `m_PlayerArray[Id - 1]`. A group mate outside 10000
    // falls through to the stranger test (.text:06cf7335 `JP LAB_06cf7340`),
    // which it then fails as well.
    void const* const MyGroup = Players[View->PlayerId].group_ptr;
    if (MyGroup != nullptr && MyGroup == Players[OwnerPlayer].group_ptr) {
        if (DistSq <= kViewRadiusSqGroupMate)
            return true;
    }
    return DistSq <= kViewRadiusSqStranger;              // .text:06cf7340
}

// Retail's inner-list search, shared by FUN_06cf73d0 (.text:06cf7410) and
// FUN_06cf6820 (.text:06cf6860). The two differ in what they match on:
// FUN_06cf73d0 compares the stored object POINTER against the candidate
// (.text:06cf7413 `CMP [ECX],ESI`), FUN_06cf6820 compares the stored object's
// get_id() against the candidate's (.text:06cf686a). For a live object those
// agree; the id form is what survives an object already being torn down, which
// is why the destroy path uses it.
static SystemTrackedNode* SystemPlayerView_FindByPointer(Observer* View, GameObject* Obj)
{
    // .text:06cf7405 .. .text:06cf741b -- the sentinel is always there (the
    // constructor allocates it) and every node's Data is a live record
    // (AddTrackedObject is the only producer), so retail tests neither:
    // `MOV ECX,[EAX + 0x8]` / `CMP [ECX],ESI`.
    SystemTrackedNode* Indexed;
    if (TrackedIndex_FindByObject(View->Tracked, Obj, Indexed)) // PORT(perf-index)
        return Indexed;
    SystemTrackedNode* const Sentinel = View->Tracked.Head;
    for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        if (Node->Data->Object == Obj)
            return Node;
    }
    return nullptr;
}

static SystemTrackedNode* SystemPlayerView_FindById(Observer* View, unsigned int ObjId)
{
    // (w4-server-guards) FUN_06cf6820's inlined search tests neither the
    // sentinel nor the record nor its object: .text:06cf684c `MOV EAX,[EDI+0xc]`
    // / 06cf6853 `MOV ESI,[EAX]`, then per node 06cf6860 `MOV EDX,[ESI+0x8]` /
    // 06cf6863 `MOV ECX,[EDX]` / 06cf6867 `CALL [EAX+0x20]` (get_id).
    SystemTrackedNode* Indexed;
    if (TrackedIndex_FindById(View->Tracked, ObjId, Indexed)) // PORT(perf-index)
        return Indexed;
    SystemTrackedNode* const Sentinel = View->Tracked.Head;
    for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        if (Node->Data->Object->get_id() == ObjId)
            return Node;
    }
    return nullptr;
}

// FUN_06cf73d0 (.text:06cf73d0), `RET 0x4`.
bool SystemPlayerView_IsObjectVisible(Observer* View, GameObject* Obj)
{
    // .text:06cf73da / .text:06cf73e4 -- same two unconditional yeses. No test
    // on the view (thiscall, EDI = ECX).
    if (Obj == nullptr)
        return true;
    if (SinglePlayer())
        return true;

    // .text:06cf73e6 .. .text:06cf7400 -- your own ship again, through the
    // same inlined CSimple accessor as FUN_06cf7260: the `XOR EAX,EAX` null
    // path at .text:06cf73f8 is read through at .text:06cf73fa, unguarded.
    if (Obj->csimple()->OwnerPlayer == View->PlayerId)
        return true;

    // .text:06cf7405 .. .text:06cf7415 -- already tracked? then the answer is
    // the record's own "created on the client" byte, verbatim
    // (.text:06cf742a MOV AL,[ECX+0x2f0]).
    SystemTrackedNode* const Node = SystemPlayerView_FindByPointer(View, Obj);
    if (Node != nullptr)
        return Node->Data->bCreatedOnClient;

    // .text:06cf7420 -- not tracked: fall through to the range test.
    return SystemPlayerView_ShouldBeVisible(View, Obj);
}

// FUN_06cf7370 (.text:06cf7370, `RET 0x4`). PORTED 2026-09-24
// (claude-code:w5-server-06ce), 34 instructions:
//   .text:06cf7376  CALL SinglePlayer / JZ     -- single player: MOV AL,1 / RET
//   .text:06cf7389 .. 06cf7398  FUN_06d006c0(&ObjId, &obj, &sys) -- the lookup
//   .text:06cf73a4 .. 06cf73a9  miss: FUN_06cf73d0(view, 0)
//   .text:06cf73b5 .. 06cf73bc  hit:  FUN_06cf73d0(view, obj)
// FUN_06d006c0 is Server_LookupGameObject in this port (the substitution every
// other caller makes); it answers null on a miss, which is exactly the object
// the miss path hands on.
bool SystemPlayerView_IsObjectIdVisible(Observer* View, unsigned int ObjId)
{
    if (SinglePlayer())
        return true;
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    return SystemPlayerView_IsObjectVisible(View, Obj);
}

// FUN_06cf65b0 (.text:06cf65b0 .. 06cf6627, thiscall on the view, RET 4). PORTED
// 2026-09-24 (claude-code:w5-server-06d0). Only caller StarSystem::sp_obj_update
// (FUN_06d0bbc0, .text:06d0bc8f).
//   .text:06cf65b6 .. 06cf65be  SinglePlayer() -> return
//   .text:06cf65c0 .. 06cf65e3  the tracked list (+0x0c, sentinel re-read each
//                               step): the record whose Object (+0x00) answers
//                               get_id() (vt +0x20) == Rec->ObjectId; none -> return
//   .text:06cf65ec .. 06cf6603  bCreatedOnClient (+0x2f0): IClient +0xd8 (SendCommonUpdateObject)
//                               with (view +0x38, Rec)
//   .text:06cf6609 .. 06cf6620  Simulation (+0x08) vtable slot 1, RemoteUpdate(
//                               &Rec->Position, &Rec->Orientation, (double) the
//                               float at Rec +0x20)
void SystemPlayerView_SendObjectUpdate(Observer* View, FCommonUpdateObjectInfo const* Rec)
{
    if (SinglePlayer())
        return;

    // The walk is SystemPlayerView_FindById's (first record whose object's
    // get_id() is the id), answered from the PORT(perf-index) index when it can.
    SystemTrackedNode* const Node = SystemPlayerView_FindById(View, Rec->ObjectId);
    if (Node == nullptr)
        return;

    SystemTrackedObject* const Tracked = Node->Data;
    if (Tracked->bCreatedOnClient)
        ::g_pClientInterface->SendCommonUpdateObject(View->PlayerId, *Rec);

    Tracked->Simulation.RemoteUpdate(Rec->Position, Rec->Orientation,
                                     static_cast<double>(Rec->Field_0x20));
}

// FUN_06cf70c0 (.text:06cf70c0 .. .text:06cf7138), thiscall on the view, (ObjId),
// `RET 0x4`. Only caller FUN_06d0f1c0 (StarSystem_SendGroupChange below). Tells
// this view's client the group id now stamped on a player ship:
//   .text:06cf70d6  FUN_06d006c0(&ObjId, &obj, &owner) -- a miss (AL == 0) or a
//                   null obj (.text:06cf70e6) returns
//   .text:06cf70ed  FUN_06cf73d0(view, obj) -- not visible to this view: return
//   .text:06cf70fa  obj->cobject() (vt +0x150); null returns
//   .text:06cf7104  (ObjectClass & 0x503) != 0x503 (not a CShip) returns
//   .text:06cf7115  the ship's +0x1b4 dword -- the group id CPlayerGroup::AddMember
//                   / DelMember store there (.text:06d5b8a5 / .text:06d5bb1b)
//   .text:06cf712d  IClient +0x1c4 (SendServerMiscObjUpdateObjectGroup)(view->PlayerId
//                   [+0x38], ObjId, group)
// No interface null test. FUN_06d006c0 is Server_LookupGameObject in this port;
// its null on a miss covers both of retail's early-outs.
void SystemPlayerView_SendGroupChange(Observer* View, unsigned int ObjId)
{
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    if (Obj == nullptr)
        return;
    if (!SystemPlayerView_IsObjectVisible(View, Obj))
        return;
    CObject const* const Cobj = Obj->cobject();
    if (Cobj == nullptr)
        return;
    if ((static_cast<unsigned int>(Cobj->ObjectClass) & 0x503u) != 0x503u)
        return;
    unsigned int const GroupId = static_cast<CShip const*>(Cobj)->GroupId;
    g_pClientInterface->SendServerMiscObjUpdateObjectGroup(View->PlayerId, ObjId, GroupId);
}

// FUN_06d0f1c0 (.text:06d0f1c0 .. .text:06d0f1e5), thiscall on the StarSystem,
// (ObjId), `RET 0x4`: FUN_06cf70c0(&node->View, ObjId) for every node of the
// player list at +0x0c, first to last, re-reading the head each round
// (.text:06d0f1dd). Callers CPlayerGroup::AddMember (.text:06d5b8d6) and
// DelMember (.text:06d5bb3e).
void StarSystem_SendGroupChange(StarSystem& System, unsigned int ObjId)
{
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(System.PlayerListHead);
    for (SystemPlayerNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        SystemPlayerView_SendGroupChange(&Node->View, ObjId);
}

// FUN_06cf6730 (.text:06cf6730 .. .text:06cf6818), thiscall on the view, (Obj),
// `RET 0x4`. Ported 2026-09-24 (claude-code:w5-server-06cf):
//   06cf674d .. 06cf676b  walk the tracked list at +0x0C; a record whose +0x00
//                         object is Obj ends the walk (06cf6765), running off the
//                         end returns (06cf676d)
//   06cf6781 .. 06cf678e  the record's +0x2F0 (bCreatedOnClient) clear -> out
//   06cf6790 .. 06cf67a6  a local std::vector<EquipDesc> -- the allocator byte is
//                         the low byte of the argument, _First/_Last/_End zero
//                         (EquipDescVector's own zeroing constructor here)
//   06cf67aa .. 06cf67c9  Obj->cobject() (IObjInspect +0x150); narrowed to CShip
//                         with (ObjectClass & 0x503) == 0x503, else 0
//   06cf67cb .. 06cf67d4  ship->vtable[+0xac](&vector) -- CEqObj::get_equip_desc_list
//                         (CShip's vftable .rdata:0639c02c + 0xac = 0639c0d8). NO null
//                         test on the narrowed pointer: retail calls through it.
//   06cf67da .. 06cf67f4  IClient +0x154 (SendServerRemoteObjectCargoUpdate)(&view->PlayerId [+0x38],
//                         ship->Id [+0xb0], &vector, 1) -- the first argument is the
//                         ADDRESS of the id (rpclocal's REMOTEOBJECTCARGOUPDATE sender
//                         dereferences it, rpc_payload_ch2.cpp)
//   06cf67fa .. 06cf6804  operator delete(vector._First) -- the vector's destructor
void SystemPlayerView_SendRemoteCargoUpdate(Observer* View, GameObject* Obj)
{
    // SystemPlayerView_FindByPointer's walk (first record whose object is Obj).
    SystemTrackedNode* const Node = SystemPlayerView_FindByPointer(View, Obj);
    if (Node == nullptr)
        return;
    if (!Node->Data->bCreatedOnClient)
        return;

    EquipDescVector Items;
    CObject const* const Cobj = Obj->cobject();
    CShip const* Ship = nullptr;
    if (Cobj != nullptr && (static_cast<unsigned int>(Cobj->ObjectClass) & 0x503u) == 0x503u)
        Ship = static_cast<CShip const*>(Cobj);
    Ship->get_equip_desc_list(Items);
    g_pClientInterface->SendServerRemoteObjectCargoUpdate(View->PlayerId, Ship->Id, Items, 1u);
}

// FUN_06d0b110 (.text:06d0b110 .. .text:06d0b154), thiscall on the StarSystem, (Obj),
// `RET 0x4`. Ported 2026-09-24 (claude-code:w5-server-06cf):
//   06d0b113 .. 06d0b11b  first = [[this+0x0C]]; an empty list returns
//   06d0b122 .. 06d0b134  per node: Obj's CBase::Object (+0x10) under the
//                         (ObjectClass & 3) == 3 guard, else 0 -- csimple()
//   06d0b136 .. 06d0b13f  CSimple::OwnerPlayer [+0xb4], read with no null test,
//                         == the node's PlayerId [node+0x40 = view+0x38] -> skip
//   06d0b141 .. 06d0b145  FUN_06cf6730(&node->View, Obj)
//   06d0b14a .. 06d0b14f  next; the head is re-read each round
void StarSystem_SendRemoteCargoUpdate(StarSystem& System, GameObject* Obj)
{
    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(System.PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(System.PlayerListHead); Node = Node->Next)
    {
        if (Obj->csimple()->OwnerPlayer != Node->View.PlayerId)
            SystemPlayerView_SendRemoteCargoUpdate(&Node->View, Obj);
    }
}

// FUN_06cf7440 (.text:06cf7440), `RET 0x4`. Its only caller is
// FUN_06d0f190 below, i.e. IServerImpl::SubmitChat's local-chat branch.
void SystemPlayerView_CollectCreatedOwners(Observer* View, std::set<unsigned int>* Owners)
{
    // .text:06cf7443 .. .text:06cf7452 -- head = [View+0xc], first = [head];
    // the loop re-reads [View+0xc] as its end every round (.text:06cf7527).
    for (SystemTrackedNode* Node = View->Tracked.Head->Next; Node != View->Tracked.Head;
         Node = Node->Next) {
        SystemTrackedObject* const Record = Node->Data;        // .text:06cf7464
        GameObject* const Obj = Record->Object;                // .text:06cf7467
        if (Obj == nullptr)                                    // .text:06cf746b
            continue;
        // .text:06cf7471 .. .text:06cf7485 -- CBase::Object / (ObjectClass & 3)
        // == 3 narrowing, then CSimple::OwnerPlayer at +0xb4 read with NO null
        // check: retail dereferences 0+0xb4 for a non-CSimple object, so it only
        // ever tracks CSimples here. Not guarded, as retail is not.
        unsigned int const Owner = Obj->csimple()->OwnerPlayer;
        if (Owner == 0)                                        // .text:06cf748b
            continue;
        if (!Record->bCreatedOnClient)                         // .text:06cf7497
            continue;
        // .text:06cf74a5 .. .text:06cf7518 -- the inlined std::set insert
        // (lower-bound walk, then FUN_06cf7770 _Insert unless the key exists).
        Owners->insert(Owner);
    }
}

// FUN_06d0f190 (.text:06d0f190), `RET 0x8`, thiscall on the StarSystem.
void StarSystem_CollectCreatedOwners(StarSystem* System, unsigned int PlayerId,
                                     std::set<unsigned int>* Owners)
{
    // .text:06d0f190 / .text:06d0f193 -- head = [this+0xc], node = [head].
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(System->PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next) {
        // .text:06d0f1a0 -- CMP [node+0x40],id; node+0x40 == View.PlayerId.
        if (Node->View.PlayerId == PlayerId) {
            // .text:06d0f1b3 -- LEA ECX,[node+0x8] / CALL FUN_06cf7440.
            SystemPlayerView_CollectCreatedOwners(&Node->View, Owners);
            return;
        }
    }
}

// FUN_06cf6820 (.text:06cf6820), `RET 0x8`.
//
// Returns nothing in retail; the bool here says whether the record was found
// and dropped, which the callers ignore, exactly as retail does.
bool SystemPlayerView_OnObjectDestroyed(Observer* View, GameObject* Obj, int Kind)
{
    // (w4-server-guards) No null test on View or Obj: thiscall, and
    // .text:06cf6828 `MOV EAX,[EBP]` reads Obj's vtable before anything else.

    // .text:06cf6831 .. .text:06cf684f -- the two-dword block retail passes to
    // IClient::SendServerDestroyObject by address, built BEFORE the search and reused on
    // every exit: { Obj->get_id(), Kind }. It is rpc_iclient_args.h's
    // FServerDestroyObjectInfo (the sender reads the dword and the byte at +0x04); retail
    // stores Kind as a whole dword, so its upper three bytes land in the pad.
    FServerDestroyObjectInfo Wire;
    Wire.ObjectId = Obj->get_id();
    unsigned int const KindDword = (unsigned int)Kind;
    Wire.Field_0x04 = static_cast<unsigned char>(KindDword & 0xffu);
    Wire.Pad_0x05[0] = static_cast<unsigned char>((KindDword >> 8) & 0xffu);
    Wire.Pad_0x05[1] = static_cast<unsigned char>((KindDword >> 16) & 0xffu);
    Wire.Pad_0x05[2] = static_cast<unsigned char>((KindDword >> 24) & 0xffu);

    SystemTrackedNode* const Node = SystemPlayerView_FindById(View, Wire.ObjectId);

    if (Node == nullptr) {
        // .text:06cf6877 .. .text:06cf6894 -- not tracked. Retail still tells
        // the client when the object belonged to this very player...
        // (w4-server-guards) .text:06cf6889 `XOR EAX,EAX` then 06cf688e
        // `CMP [EAX+0xb4],EDX`: retail reads OwnerPlayer through the narrowed
        // pointer with no null test, like SystemPlayerView_ShouldBeVisible.
        unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;
        if (OwnerPlayer == View->PlayerId) {
            // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
            g_pClientInterface->SendServerDestroyObject(View->PlayerId, Wire);
            return false;
        }

        // .text:06cf6900 .. .text:06cf6910 -- ...and otherwise asks the object
        // itself, through vtable +0x154 == GameObject::get_type_bits, and
        // notifies only when the answer has a bit inside 0xfdff set. That mask
        // is "any solar type except WAYPOINT"; see kSolarTypeBits above for the
        // Common.dll table it was read out of. Such objects exist on the client
        // whether or not this player ever tracked them, which is why retail
        // still sends the notification.
        if ((Obj->get_type_bits() & kSolarTypeBits) != 0) {
            // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
            g_pClientInterface->SendServerDestroyObject(View->PlayerId, Wire);
        }
        return false;
    }

    // .text:06cf689d .. .text:06cf68a8 -- tracked. The notification goes out
    // only when the client was ever told the object exists.
    // (w4-server-guards) 06cf689d `MOV ECX,[ESI+0x8]` / 06cf68a0
    // `MOV AL,[ECX+0x2f0]` -- the record is read with no null test here; the
    // only null test is the one before the free below (06cf68c4).
    SystemTrackedObject* const Record = Node->Data;
    if (Record->bCreatedOnClient) {
        // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
        g_pClientInterface->SendServerDestroyObject(View->PlayerId, Wire);
    }

    // .text:06cf68c1 .. .text:06cf68d5 -- destroy the record's
    // CRemotePhysicsSimulation (FUN_06cf6940 is `ADD ECX,8` then a tail jump
    // into COMMON.DLL::CRemotePhysicsSimulation::~CRemotePhysicsSimulation)
    // and free the record.
    TrackedIndex_OnRemove(View->Tracked, Node); // PORT(perf-index): before the record goes
    if (Record != nullptr) {
        // Record::Simulation is a real CRemotePhysicsSimulation by value now, so
        // `delete` runs its destructor -- which is exactly what FUN_06cf6940
        // (`ADD ECX,0x8` + tail jump into ~CRemotePhysicsSimulation) does before
        // retail frees the block at .text:06cf68d0.
        delete Record;
        Node->Data = nullptr;
    }

    // .text:06cf68d8 .. .text:06cf68f4 -- unlink, free the node, size--.
    Node->Prev->Next = Node->Next;
    Node->Next->Prev = Node->Prev;
    delete Node;
    // (w4-server-guards) 06cf68ed `MOV EAX,[EDI+0x10]` / 06cf68f3 `DEC EAX` /
    // 06cf68f4 store -- unconditional; a found node means size >= 1.
    View->Tracked.Size--;
    return true;
}

// ===========================================================================
// The five "create this object on that player's client" notifiers, and six of
// the seven SystemPlayerView_AddTrackedObject call sites. Declared in
// server_sysdb.h, where the FDUMP-line / IClient-slot cross-check that names
// them lives.
//
// Retail's own source file for all five is
// E:\FL\Scratch\Source\Server\space\Observer.cpp (.rdata:06d669dc, pushed at
// .text:06cf66d7 / .text:06cf6c39 / .text:06cf6cda / .text:06cf6dd9) -- NOT
// StarSys.cpp, which is where add_cargo and the system itself come from. The
// class is retail's per-player Observer (RTTI .?AVObserver@@ at .data:06d88858
// names it too); the free functions keep their SystemPlayerView_ prefix.
// ===========================================================================

static char const kObserverCppPath[] =
    "E:\\FL\\Scratch\\Source\\Server\\space\\Observer.cpp";

// FUN_06cf6630 (.text:06cf6630, `RET 0x8`) -- CREATESHIP, IClient +0x0bc.
//
// The odd one out of the five. It does NOT consult
// SystemPlayerView_ShouldBeVisible and it does NOT set bCreatedOnClient; it
// sends only in SINGLE player (.text:06cf667c CALL SinglePlayer /
// .text:06cf6684 `JZ LAB_06cf66e7`, the silent exit), which is consistent
// because AddTrackedObject already seeds bCreatedOnClient from SinglePlayer().
// In multiplayer it therefore only registers the ship and lets the per-frame
// range check send the create later.
void SystemPlayerView_CreateShipForPlayer(Observer* View, CShipCreateInfo* Info, GameObject* Obj)
{
    // No test of View or Obj: ESI = ECX and EDI = the second stack argument are
    // dereferenced as they come (.text:06cf6632 / .text:06cf6636).
    //
    // .text:06cf6636 .. .text:06cf664c -- the usual `(ObjectClass & 3) == 3`
    // guard on CBase::Object, then CSimple::OwnerPlayer at +0xb4 read through
    // the result with no test (.text:06cf664a `XOR EAX,EAX` falls straight
    // into .text:06cf664c `MOV EDX,[EAX + 0xb4]`). Every object reaching here
    // is a ship, so retail never takes the null read; the port does not guard
    // it either.
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;

    // .text:06cf6652  CMP EDX,[ESI + 0x38] / .text:06cf6655 JZ -- a player is
    // never told to create their own ship.
    if (OwnerPlayer == View->PlayerId)
        return;

    // .text:06cf665b  MOV AL,[ESI + 0x28] / TEST AL,AL / JNZ
    if (View->bShipGone != 0)
        return;

    // .text:06cf666a  CALL [EAX + 0x160] -- is_destroyed.
    if (Obj->is_destroyed()) {
        // .text:06cf66c0 .. .text:06cf66e4 -- level 0x100002, line 0x19b, and
        // the object's own get_id() (vtable +0x20) as the third argument.
        // .text:06cf66cd MOV EAX,[->DACOM.DLL::FDUMP] / .text:06cf66e2 CALL [EAX]
        // -- the import is called with no null test (dacom seeds it with
        // DefaultFdump).
        FDUMP(0x100002, "%s(%d) : *** WARNING: Ship(%d) 'dead' before creation packet sent.",
              kObserverCppPath, 0x19b, Obj->get_id());
        return;
    }

    // .text:06cf6677  CALL FUN_06cf7140 -- the return value is discarded here,
    // unlike in the other four.
    SystemPlayerView_AddTrackedObject(View, Obj);

    if (!SinglePlayer())
        return;

    // .text:06cf6686 .. .text:06cf66a0 -- retail copies Players[PlayerId]+0x318
    // into the payload at +0xd4 before sending. Raw bytes:
    //   .text:06cf6686  8b4e38        MOV  ECX,[ESI + 0x38]        ; PlayerId
    //   .text:06cf6689  8b156002d906  MOV  EDX,[.data:Players]
    //   .text:06cf668f  69c918040000  IMUL ECX,ECX,0x418
    //   .text:06cf6695  8b8c1100ffffff MOV ECX,[ECX + EDX*1 + -0x100]
    //   .text:06cf669c  8b44240c      MOV  EAX,[ESP + 0xc]         ; the PAYLOAD,
    //                                 ; i.e. the FIRST stack argument, not the
    //                                 ; second -- ESP is entry-8 here
    //   .text:06cf66a0  8988d4000000  MOV  [EAX + 0xd4],ECX
    // and 0x418 - 0x100 == 0x318.
    //
    // CORRECTED 2026-09-17 (wave 13). The wave-12 note said "PlayerData+0x318
    // is not a field this port has identified". It is: PlayerData::rep_vibe,
    // documented at -0x100 in common_server_playerdb.h and written through
    // `ADD ECX,0x318` in this very file (see the Reputation::Vibe::GetName
    // call site). The same dword goes into the CREATESOLAR payload at +0xb0
    // through the identical `id*0x418 - 0x100 + Players` expression
    // (.text:06d0a63b region), which is the cross-check.
    //
    // +0xd4 is CShipCreateInfo::VibeOtherGroupId (src/include/server_createinfo.h),
    // the record rpclocal's CREATESHIP marshaller reads through the same
    // declaration. Retail stores unconditionally; the only caller hands in the
    // address of its own stack record, so there is no null case to test. The
    // slot is indexed raw (PlayerDB::operator[] is the same unchecked
    // `m_PlayerArray[Id - 1]`), with no PlayerDB::is_valid.
    Info->VibeOtherGroupId = Players[View->PlayerId].rep_vibe;

    // .text:06cf66a6 .. .text:06cf66b3 -- IClient vtable +0xbc, and the player
    // id goes by VALUE (.text:06cf66af `MOV EAX,[ESI + 0x38]` / `PUSH EAX`).
    // DAT_06d8fb14 is loaded with no test (.text:06cf66a6).
    g_pClientInterface->SendServerCreateShip(View->PlayerId, *Info);
}

// FUN_06cf6bc0 (.text:06cf6bc0, `RET 0x8`) -- CREATECOUNTER, IClient +0x0cc.
void SystemPlayerView_CreateCounterForPlayer(Observer* View, void* Payload, GameObject* Obj)
{
    // (w4-server-guards) No null test on View or Obj: .text:06cf6bc5 reads
    // [EDI + 0x28] first and 06cf6bd0 `MOV EAX,[ESI]` reads Obj's vtable.

    // .text:06cf6bc5  MOV AL,[EDI + 0x28] / TEST AL,AL / JNZ
    if (View->bShipGone != 0)
        return;

    // .text:06cf6bd4  CALL [EAX + 0x160]
    if (Obj->is_destroyed()) {
        // .text:06cf6c22 .. .text:06cf6c46 -- level 0x100002, line 0x23c.
        // 06cf6c2f `MOV EAX,[->DACOM.DLL::FDUMP]` / 06cf6c44 `CALL [EAX]` --
        // the import is called with no null test.
        FDUMP(0x100002, "%s(%d) : *** WARNING: CounterMeasure(%d) 'dead' before creation packet sent.",
              kObserverCppPath, 0x23c, Obj->get_id());
        return;
    }

    // .text:06cf6be1  CALL FUN_06cf7140, and the record is KEPT this time.
    SystemTrackedObject* const Record = SystemPlayerView_AddTrackedObject(View, Obj);

    // .text:06cf6be8 .. .text:06cf6bfc -- outside single player the object has
    // to be in range before the client is told about it at all.
    if (!SinglePlayer() && !SystemPlayerView_ShouldBeVisible(View, Obj))
        return;

    // .text:06cf6c02  MOV byte ptr [EBX + 0x2f0],0x1 -- no test on EBX, the
    // record FUN_06cf7140 just allocated.
    Record->bCreatedOnClient = true;

    // .text:06cf6c09 .. .text:06cf6c16 -- IClient vtable +0xcc. NOTE the first
    // argument: .text:06cf6c12 is `ADD EDI,0x38` and .text:06cf6c15 `PUSH EDI`,
    // so this slot is handed the ADDRESS of View->PlayerId, not its value --
    // unlike the ship / loot / mine slots, which push `[EDI + 0x38]`:
    // IClient::SendServerCreateCounter takes the id as `const unsigned int&`.
    // Payload is the caller's FServerCreateProjectileInfo (server_createinfo.h).
    // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
    g_pClientInterface->SendServerCreateCounter(
        View->PlayerId, *static_cast<const FServerCreateProjectileInfo*>(Payload));
}

// FUN_06cf6d60 (.text:06cf6d60, `RET 0x8`) -- CREATEMINE, IClient +0x0c4.
// Byte-for-byte the countermeasure one with a different FDUMP line, a different
// slot, and the player id passed BY VALUE (.text:06cf6db2 `MOV EAX,[EDI+0x38]`).
void SystemPlayerView_CreateMineForPlayer(Observer* View, void* Payload, GameObject* Obj)
{
    // (w4-server-guards) No null test on View or Obj (.text:06cf6d65 reads
    // [EDI + 0x28], 06cf6d70 `MOV EAX,[ESI]`), none on FDUMP (06cf6dcf .. the
    // import is called through directly), none on the record (06cf6da2).
    if (View->bShipGone != 0)                       // .text:06cf6d65
        return;

    if (Obj->is_destroyed()) {                      // .text:06cf6d74
        // .text:06cf6dc2 .. .text:06cf6de6 -- level 0x100002, line 0x28a.
        FDUMP(0x100002, "%s(%d) : *** WARNING: Mine(%d) 'dead' before creation packet sent.",
              kObserverCppPath, 0x28a, Obj->get_id());
        return;
    }

    SystemTrackedObject* const Record =
        SystemPlayerView_AddTrackedObject(View, Obj);   // .text:06cf6d81

    if (!SinglePlayer() && !SystemPlayerView_ShouldBeVisible(View, Obj))
        return;                                      // .text:06cf6d88 / 06cf6d95

    Record->bCreatedOnClient = true;                 // .text:06cf6da2

    // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
    // Payload is the caller's FServerCreateProjectileInfo (server_createinfo.h).
    g_pClientInterface->SendServerCreateMine(
        View->PlayerId, *static_cast<const FServerCreateProjectileInfo*>(Payload));
}

// FUN_06cf6df0 (.text:06cf6df0, `RET 0x4`) -- EXPLODEASTEROIDMINE, IClient
// +0x0d0. Only caller StarSystem::send_mine_explosion (.text:06d092a9).
//   .text:06cf6df9  id = Rec->MineId (kept in a local)
//   .text:06cf6e01  SinglePlayer() -> straight to the send
//   .text:06cf6e1a  FUN_06d006c0(&id, &obj, &sys); on failure obj = 0
//                   (.text:06cf6e26, redundant after the lookup's own clear)
//   .text:06cf6e35  FUN_06cf73d0(view, obj) == 0 -> return, nothing sent
//   .text:06cf6e3e  DAT_06d8fb14->vtable[+0xd0](view->PlayerId (+0x38), Rec)
// Neither the IClient pointer nor the record is tested.
void SystemPlayerView_SendMineExplosion(Observer* View, FMineExplosion* Rec)
{
    unsigned int const MineId = Rec->MineId;
    if (!SinglePlayer()) {
        // FUN_06d006c0 is the port's registry lookup (server_pub.cpp,
        // Server_LookupGameObject) -- the same substitution every other
        // FUN_06d006c0 caller in this port makes.
        GameObject* const Obj = Server_LookupGameObject(MineId);
        if (!SystemPlayerView_IsObjectVisible(View, Obj))
            return;
    }
    g_pClientInterface->SendServerExplodeAsteroidMine(View->PlayerId, *Rec);
}

// FUN_06cf66f0 (.text:06cf66f0 .. .text:06cf672a), thiscall on the view,
// (Info, Obj), `RET 0x8`. Ported 2026-09-24 (claude-code:w5-server-06cf):
//   06cf66f3 .. 06cf66f8  view+0x28 (bShipGone) set -> return
//   06cf66fa .. 06cf6704  FUN_06cf7260(view, Obj) -- SystemPlayerView_ShouldBeVisible
//   06cf670a .. 06cf6714  not visible: Info+0xac byte clear -> return
//   06cf6716 .. 06cf6723  IClient +0xb8 (SendServerCreateSolar)(view->PlayerId [+0x38], Info)
// Unlike the ship/loot/guided/mine notifiers there is no FUN_06cf7140 here: a
// solar never gets a tracked record. No IClient null test.
void SystemPlayerView_CreateSolarForPlayer(Observer* View, CSolarCreateInfo* Info, GameObject* Obj)
{
    if (View->bShipGone != 0)
        return;
    if (!SystemPlayerView_ShouldBeVisible(View, Obj) && Info->Field_0xac == 0)
        return;
    g_pClientInterface->SendServerCreateSolar(View->PlayerId, *Info);
}

// FUN_06cf6ac0 (.text:06cf6ac0 .. .text:06cf6bb4), thiscall on the view, 21 stack
// dwords (RET 0x54): SenderId, ReceiverId, VoiceId, the 13-dword Costume by value,
// CommType, TextIds, TextCount, Mood, Duration. Ported 2026-09-24
// (claude-code:w5-server-06cf):
//   06cf6ad7 .. 06cf6b0c  SinglePlayer() clear: FUN_06d006c0(&SenderId, &obj, &sys),
//                         a miss zeroes obj; FUN_06cf73d0(view, obj) false -> return
//   06cf6b1a .. 06cf6b4d  SinglePlayer() (called again) clear: the same for ReceiverId
//   06cf6b4f .. 06cf6b64  ReceiverId == Players[view->PlayerId].ship_object_id
//                         [slot-0x34] -> SETZ CL (only the low byte is set; the rest of
//                         ECX is PlayerId*0x418 -- rpclocal's SENDCOMM marshaller reads
//                         A22 & 0xff, so the port passes 0/1)
//   06cf6b67 .. 06cf6ba7  IClient +0x110 (SendServerSendComm)(view->PlayerId, SenderId, ReceiverId,
//                         VoiceId, Costume[0..12], CommType, TextIds, TextCount, Mood,
//                         Duration bits, that flag). No IClient null test.
void SystemPlayerView_SendComm(Observer* View, unsigned int SenderId,
                               unsigned int ReceiverId, unsigned int VoiceId,
                               Costume const& Cost, unsigned int CommType,
                               unsigned int* TextIds, int TextCount, unsigned int Mood,
                               float Duration)
{
    if (!SinglePlayer()) {
        GameObject* SenderObj = nullptr;
        StarSystem* SenderSystem = nullptr;
        if (!Server_FindObject(SenderId, SenderObj, SenderSystem))       // .text:06cf6aee
            SenderObj = nullptr;
        if (!SystemPlayerView_IsObjectVisible(View, SenderObj))          // .text:06cf6b05
            return;
    }
    if (!SinglePlayer()) {
        GameObject* ReceiverObj = nullptr;
        StarSystem* ReceiverSystem = nullptr;
        if (!Server_FindObject(ReceiverId, ReceiverObj, ReceiverSystem)) // .text:06cf6b2f
            ReceiverObj = nullptr;
        if (!SystemPlayerView_IsObjectVisible(View, ReceiverObj))        // .text:06cf6b46
            return;
    }

    bool const bReceiverIsOwnShip =
        ReceiverId == Players[View->PlayerId].ship_object_id;            // .text:06cf6b60
    // The Costume goes by value -- the 13-dword copy is .text:06cf6b97 REP MOVSD.
    FL_LAYOUT_ASSERT(sizeof(Costume) == 13 * sizeof(unsigned int), "Costume is 13 dwords");
    g_pClientInterface->SendServerSendComm(View->PlayerId, SenderId, ReceiverId, VoiceId,
                                           Cost, CommType, TextIds, TextCount, Mood, Duration,
                                           bReceiverIsOwnShip);         // .text:06cf6ba7
}

// FUN_06cf6c50 (.text:06cf6c50, `RET 0x8`) -- CREATELOOT, IClient +0x0c0.
// The only one of the five that tracks MORE THAN ONE object per call: it walks
// the whole [First, Last) range and sends a single packet afterwards.
void SystemPlayerView_CreateLootForPlayer(Observer* View, FServerCreateLootRequest* Payload,
                                          GameObject* const* First, GameObject* const* Last)
{
    // (w4-server-guards) No null test on View (.text:06cf6c55 reads
    // [EDI + 0x28] first), none on First (the only test is 06cf6c63
    // `CMP ESI,[EBX + 0x8]`; a vector's null begin comes with a null end),
    // none on an element (06cf6c68 `MOV ECX,[ESI]` / 06cf6c6a `MOV EAX,[ECX]`),
    // none on FUN_06cf6c50's FDUMP import (06cf6cd0 .. called through
    // directly) and none on the payload (06cf6c85 stores unconditionally).

    // .text:06cf6c55  MOV AL,[EDI + 0x28] / TEST AL,AL / JNZ
    if (View->bShipGone != 0)
        return;

    // .text:06cf6c60  MOV ESI,[EBX + 0x4] / .text:06cf6c63 CMP ESI,[EBX + 0x8]
    // -- the vector's first/last pair; an empty range skips straight to the
    // send, it does not return.
    for (GameObject* const* It = First; It != Last; ++It) {
        GameObject* const Obj = *It;                 // .text:06cf6c68

        // .text:06cf6c6c  CALL [EAX + 0x160]. Retail ABANDONS the whole call on
        // the first dead entry -- .text:06cf6c74 `JNZ LAB_06cf6cc3` leaves via
        // the FDUMP and never reaches the send. It is not a `continue`.
        if (Obj->is_destroyed()) {
            // .text:06cf6cc3 .. .text:06cf6ce7 -- level 0x100001 (ERROR, not
            // WARNING like the other three), line 0x256.
            FDUMP(0x100001, "%s(%d) : *** ERROR: Loot(%d) 'dead' before creation packet sent.",
                  kObserverCppPath, 0x256, Obj->get_id());
            return;
        }

        // .text:06cf6c76 .. .text:06cf6c85 -- retail asks FUN_06cef450 "is this
        // view's player id in the object's own list at GameObject+0x18?" and
        // stores the answer as a BYTE at Payload+0x8:
        //   .text:06cf6c76  8b4f38    MOV  ECX,[EDI + 0x38]   ; View->PlayerId
        //   .text:06cf6c79  51        PUSH ECX
        //   .text:06cf6c7a  8b0e      MOV  ECX,[ESI]          ; the loot object
        //   .text:06cf6c7c  e8cf87ffff CALL FUN_06cef450
        //   .text:06cf6c81  8b542410  MOV  EDX,[ESP + 0x10]   ; the PAYLOAD
        //   .text:06cf6c85  884208    MOV  byte ptr [EDX + 0x8],AL
        //
        // DECODED 2026-09-17 (wave 13): that list is the object's "marked by"
        // player set -- pub::Player::MarkObj (.text:06d549d5) is its only
        // producer -- and it is modelled now (GameObject::is_marked_by).
        //
        // Note that this store is INSIDE the per-element loop, so the last
        // element of the range wins. That is retail's own behaviour, not a
        // simplification; do not hoist it.
        //
        // +0x08 is FServerCreateLootRequest::bMarkedBy (server_createinfo.h).
        Payload->bMarkedBy = Obj->is_marked_by(View->PlayerId) ? 1u : 0u;

        // .text:06cf6c8d  CALL FUN_06cf7140 -- once per element.
        SystemPlayerView_AddTrackedObject(View, Obj);
    }

    // .text:06cf6c9c .. .text:06cf6ca4 -- and unlike the other four, the loot
    // packet goes out ONLY in single player; there is no ShouldBeVisible arm and
    // bCreatedOnClient is never written here (AddTrackedObject's own seeding is
    // what covers it).
    if (!SinglePlayer())
        return;

    // .text:06cf6caa .. .text:06cf6cb7 -- IClient vtable +0xc0, player id by
    // value (.text:06cf6cb3 `MOV EAX,[EDI + 0x38]`).
    // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
    g_pClientInterface->SendServerCreateLoot(View->PlayerId, *Payload);
}

// FUN_06cf6cf0 (.text:06cf6cf0, `RET 0x8`) -- CREATEGUIDED, IClient +0x0c8.
// Takes the same vector the loot one does but uses ONLY its first element:
// .text:06cf6d00 loads first, .text:06cf6d03 rejects a null one,
// .text:06cf6d07..06cf6d11 computes `(last - first) / 4` and rejects zero, and
// .text:06cf6d13 is `MOV EAX,[ESI]` -- one dereference, no loop.
void SystemPlayerView_CreateGuidedForPlayer(Observer* View, void* Payload,
                                            GameObject* const* First, GameObject* const* Last)
{
    // (w4-server-guards) No null test on View (.text:06cf6cf5 reads
    // [EDI + 0x28] first), none on the element (06cf6d13 `MOV EAX,[ESI]` /
    // 06cf6d15 `PUSH EAX` straight into FUN_06cf7140), none on the record
    // (06cf6d39). The null-first and empty-range exits below are retail's.
    if (View->bShipGone != 0)                        // .text:06cf6cf5
        return;

    if (First == nullptr || First == Last)           // .text:06cf6d03 / 06cf6d11
        return;

    GameObject* const Obj = *First;                  // .text:06cf6d13

    // No is_destroyed() guard and no FDUMP in this one -- the only member of the
    // family without them. Do not add one.
    SystemTrackedObject* const Record =
        SystemPlayerView_AddTrackedObject(View, Obj);    // .text:06cf6d16

    if (!SinglePlayer() && !SystemPlayerView_ShouldBeVisible(View, Obj))
        return;                                      // .text:06cf6d1d / 06cf6d2c

    Record->bCreatedOnClient = true;                 // .text:06cf6d39

    // .text:06cf6d49  ADD EDI,0x38 / .text:06cf6d4c PUSH EDI -- like the
    // countermeasure slot, this one takes the ADDRESS of the player id
    // (IClient::SendServerCreateGuided's `const unsigned int&`). Payload is the caller's
    // FServerCreateGuidedInfo (server_createinfo.h).
    // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
    g_pClientInterface->SendServerCreateGuided(
        View->PlayerId, *static_cast<const FServerCreateGuidedInfo*>(Payload));
}

// ===========================================================================
// FUN_06cf6950 (.text:06cf6950, `RET 0xc`) -- the per-player DAMAGEOBJECT gate,
// and the only writer of the deferred-damage set at View+0x14.
//
// Retail's shape, read off the raw listing rather than off the `.c` (Ghidra
// types param_2 as `int` and then casts it to `int *`, which is the tell):
//
//   .text:06cf6968  CALL FUN_06d006c0(&id, &obj, &flag)   ; id -> GameObject*
//   .text:06cf6988  CALL [EAX + 0x150]                    ; the wrapper's CObject
//   .text:06cf6992  MOV  ECX,[EAX + 0x4c] / AND 0x303 / CMP 0x303
//                                                         ; keep it only if SOLAR
//   .text:06cf69ac  CALL FUN_06cf73d0                     ; IsObjectVisible
//   .text:06cf69b3  JNZ  LAB_06cf6a8c                     ; visible -> send
//   .text:06cf69bd  MOV  EDX,[ESI + 0xe0] / SHR 7 / TEST DL,1
//                                                         ; CSimple::TypeBits bit
//                                                         ; 0x80 == TRADELANE_RING
//   .text:06cf69c9  JNZ  LAB_06cf6a8c                     ; -> send anyway
//   .text:06cf69cf  CALL COMMON.DLL::SinglePlayer / JNZ out
//   .text:06cf69df  JZ   out                              ; not a solar -> drop
//   .text:06cf69e5  MOV  EAX,[ESP + 0x24]                 ; the SECOND record
//   .text:06cf69e9  MOV  ECX,[EAX + 0x8]                  ; its std::list _Size
//   .text:06cf69ee  JNZ  LAB_06cf6a90                     ; non-empty -> send IT
//   .text:06cf69f4 .. .text:06cf6a8a                      ; else insert the id
//   .text:06cf6a90 .. .text:06cf6aa2  CALL [EDX + 0xfc]   ; IClient::SendServerDamageObject
//
// and the send takes THREE stack arguments, `(PlayerId, ObjId, record)`, where
// `record` is param_2 on the visible path (EAX comes from [ESP+0x20] at
// .text:06cf6a8c) and param_3 on the deferred-but-non-empty path (EAX is still
// param_3 from .text:06cf69e5). Two different arguments reaching one PUSH is
// exactly the kind of thing the decompiled `piVar7` hides.
// ===========================================================================
void SystemPlayerView_DamageObject(Observer* View, unsigned int ObjId,
                                   DamageList* PrimaryRecord,
                                   DamageList* DeferredRecord)
{
    // (w4-server-guards) No null test on View: thiscall, EBX = ECX
    // (.text:06cf695b) and nothing tests it before 06cf69ac passes it on.

    // .text:06cf6968 / .text:06cf6972 / .text:06cf697e -- a lookup that fails,
    // and a null result, both leave without touching anything.
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    if (Obj == nullptr)
        return;

    // .text:06cf6988 .. .text:06cf69a7. Retail asks the wrapper's own vtable
    // +0x150 for the CObject and then narrows on (ObjectClass & 0x303) ==
    // 0x303, which is CObject::ClassSolar. GameObject::cobj() already applies
    // the weaker (ObjectClass & 3) == 3, so composing the two is the same test.
    CObject* const Raw = Obj->cobj();
    CSimple* Solar = nullptr;
    if (Raw != nullptr &&
        (static_cast<unsigned>(Raw->ObjectClass) & 0x303u) == 0x303u) {
        Solar = Obj->csimple();
    }

    // .text:06cf69ac .. .text:06cf69c9 -- visible, or a tradelane ring.
    // CSimple::TypeBits is +0xe0 (common_cobject.h has the static_assert) and
    // bit 0x80 is TRADELANE_RING in the OBJECT_TYPE table this file already
    // quotes above kSolarTypeBits. A ring's damage state has to reach the
    // client whether or not the ring is in range.
    bool bSend = SystemPlayerView_IsObjectVisible(View, Obj);
    DamageList* Record = PrimaryRecord;
    if (!bSend && Solar != nullptr && (Solar->TypeBits & 0x80u) != 0)
        bSend = true;

    if (!bSend) {
        // .text:06cf69cf -- in single player there is nothing to defer TO.
        if (SinglePlayer())
            return;
        // .text:06cf69dd -- and a non-solar is dropped outright.
        if (Solar == nullptr)
            return;
        // .text:06cf69e5  MOV EAX,[ESP + 0x24]  ; param_3, the second record
        // .text:06cf69e9  MOV ECX,[EAX + 0x8]   ; its _Size (DamageList::Count)
        if (DeferredRecord->Count != 0) {
            // .text:06cf69ee -- the caller's second record is already populated,
            // so send that one instead of remembering the id.
            Record = DeferredRecord;
            bSend = true;
        } else {
            // .text:06cf69f4 .. .text:06cf6a8a -- `set::insert(ObjId)`. Retail
            // open-codes MSVC6's descent, the uniqueness check and `_Insert`;
            // std::set::insert IS that code.
            View->PendingDamageIds.insert(ObjId);
            return;
        }
    }

    // .text:06cf6a90 .. .text:06cf6aa2 -- IClient vtable +0xfc == SendServerDamageObject,
    // ch2 op 0x05 FLPACKET_SERVER_DAMAGEOBJECT. The player id goes by VALUE
    // (.text:06cf6a9e `MOV EAX,[EBX + 0x38]` / `PUSH EAX`).
    // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
    g_pClientInterface->SendServerDamageObject(View->PlayerId, ObjId, *Record);
}

// ===========================================================================
// FUN_06cf6e60 (.text:06cf6e60, `RET 0x8`) -- "this object has left the
// system". The three things it owns are the tracked-list drop, the
// deferred-damage set CLEAR, and the bShipGone latch.
// ===========================================================================
void SystemPlayerView_OnObjectLeftSystem(Observer* View,
                                         unsigned int const* ObjId,
                                         unsigned int const* Destination)
{
    // (w4-server-guards) No null test on View, ObjId or Destination: thiscall,
    // .text:06cf6e6b `MOV EAX,[EBP + 0xc]` reads the view, 06cf6e8a
    // `CMP EAX,[EBX]` and 06cf6f6f `MOV EDX,[EBX]` read *ObjId, and 06cf6f8d
    // `MOV ECX,[EDX]` reads *Destination -- all unconditionally.

    // .text:06cf6e6b .. .text:06cf6ed4 -- walk the tracked list, match on the
    // wrapper's own get_id (vtable +0x20) at .text:06cf6e87 / .text:06cf6e8a,
    // latch the record's bCreatedOnClient (.text:06cf6e9c reads record+0x2f0,
    // .text:06cf6ea2 spills it), destroy the record (.text:06cf6eaa
    // FUN_06cf6940, .text:06cf6eb0 free), unlink the node (.text:06cf6eb8 ..
    // .text:06cf6ec8) and decrement the size (.text:06cf6ecd .. .text:06cf6ed4).
    // `break` is retail's: .text:06cf6e8c `JZ LAB_06cf6e97` leaves the loop on
    // the first hit and the unlink falls straight through to LAB_06cf6ed7.
    // (w4-server-guards) The walk tests neither the sentinel, nor a node, nor
    // the record, nor its object: 06cf6e6f `MOV ESI,[EAX]` / 06cf6e71
    // `CMP ESI,EAX`, then 06cf6e80 `MOV EAX,[ESI+0x8]` / 06cf6e83
    // `MOV ECX,[EAX]` / 06cf6e87 `CALL [EDX+0x20]`. On the hit, +0x2f0 is read
    // at 06cf6e9c before the `TEST EDI,EDI` at 06cf6e9a takes effect (the JZ is
    // at 06cf6ea6), so only the free is null-tested -- `delete` covers that --
    // and size-- at 06cf6ecd .. 06cf6ed4 is unconditional.
    bool bWasCreatedOnClient = false;
    SystemTrackedNode* const Node = SystemPlayerView_FindById(View, *ObjId); // the same first-match walk
    if (Node != nullptr) {
        SystemTrackedObject* const Record = Node->Data;
        bWasCreatedOnClient = (Record->bCreatedOnClient != false);
        TrackedIndex_OnRemove(View->Tracked, Node); // PORT(perf-index)
        delete Record;                   // .text:06cf6eaa FUN_06cf6940 + free
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;                     // .text:06cf6ec8
        --View->Tracked.Size;             // .text:06cf6ed4
    }

    // .text:06cf6ed7 .. .text:06cf6f64 -- `_Tree::erase(begin(), end())` on the
    // set at +0x14 (.text:06cf6edf `LEA ESI,[EBP + 0x14]` is its `this`). Retail
    // takes the fast path (.text:06cf6f0f `_Erase` on the whole root, then reset
    // _Size/_Lmost/_Rmost) when the range really is the whole container, and the
    // node-by-node path at .text:06cf6f41 .. .text:06cf6f64 otherwise. Both are
    // the same observable clear().
    View->PendingDamageIds.clear();

    // .text:06cf6f66 .. .text:06cf6f7f -- the latch. `-0x34` off `Players +
    // id*0x418` is PlayerData+0x3E4, the player's own ship object id.
    // (w4-server-guards) No PlayerDB::is_valid here: 06cf6f66 .. 06cf6f79 index
    // the slot raw (`IMUL ECX,ECX,0x418` / `CMP EDX,[ECX + ESI*1 + -0x34]`);
    // Players[] is the same raw `m_PlayerArray[Id - 1]`.
    if (*ObjId == Players[View->PlayerId].ship_object_id)
        View->bShipGone = 1;                 // .text:06cf6f7f

    // .text:06cf6f83 .. .text:06cf6fb0 -- the two-dword stack payload and
    // IClient vtable +0xe4 == SendServerSystemSwitchOut, ch2 op 0x21
    // FLPACKET_SERVER_SYSTEM_SWITCH_OUT. Retail sends it when EITHER the record
    // it just dropped had been created on the client OR bShipGone is now set
    // (.text:06cf6f97 / .text:06cf6fa0). The payload is rpc_iclient_args.h's
    // FServerSystemSwitchOutInfo {ShipId, GateId}.
    if (bWasCreatedOnClient || View->bShipGone != 0) {
        FServerSystemSwitchOutInfo Wire;
        Wire.ShipId = *ObjId;                                         // .text:06cf6f89
        Wire.GateId = *Destination;                                   // .text:06cf6f8f
        // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
        g_pClientInterface->SendServerSystemSwitchOut(View->PlayerId, Wire);
    }
}

// ===========================================================================
// FUN_06cf6fc0 (.text:06cf6fc0 .. .text:06cf70b1, thiscall on the view,
// `RET 0xc`) -- "this object has landed". The LAND twin of the function
// above: same tracked-list drop, no set clear, no latch, and the packet is
// IClient +0x0ec == SendServerLand, ch2 op 0x0b FLPACKET_SERVER_LAND, with a
// THREE-dword payload (rpc_iclient_args.h's FServerLandInfo {ShipId, DockId, BaseId}).
// ===========================================================================
void SystemPlayerView_OnObjectLanded(Observer* View,
                                     unsigned int const* ObjId,
                                     unsigned int const* Arg2,
                                     unsigned int const* Arg3)
{
    // .text:06cf6fc8 .. .text:06cf702b -- first node whose record's object
    // answers get_id (vtable +0x20, .text:06cf6fda) == *ObjId: BL = the
    // record's +0x2f0 (read at .text:06cf6ff4 BEFORE the null test), record
    // destroyed and freed when non-null (.text:06cf6ffe FUN_06cf6940,
    // .text:06cf7004), node unlinked and freed (.text:06cf700c ..
    // .text:06cf701c), size decremented unguarded (.text:06cf7027). One hit
    // at most: the match jumps out of the walk.
    bool bWasCreatedOnClient = false;
    SystemTrackedNode* const Node = SystemPlayerView_FindById(View, *ObjId); // the same first-match walk
    if (Node != nullptr) {
        SystemTrackedObject* const Record = Node->Data;
        bWasCreatedOnClient = (Record->bCreatedOnClient != false);
        TrackedIndex_OnRemove(View->Tracked, Node); // PORT(perf-index)
        delete Record;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        --View->Tracked.Size;
    }

    // .text:06cf702e .. .text:06cf7048 -- the payload {*ObjId, *Arg2, *Arg3}
    // at ESP+0xc/+0x10/+0x14, built before either test.
    FServerLandInfo Wire;
    Wire.ShipId = *ObjId;
    Wire.DockId = *Arg2;
    Wire.BaseId = *Arg3;

    // .text:06cf704c .. .text:06cf7061 -- a record that was never created on
    // the client only goes out when the object is this player's own ship
    // (Players + id*0x418 - 0x34 = PlayerData+0x3e4, ship_object_id).
    PlayerData& Owner = Players[View->PlayerId];
    if (!bWasCreatedOnClient && Wire.ShipId != Owner.ship_object_id)
        return;

    // .text:06cf7063 .. .text:06cf708f -- outside single player, the player's
    // own ship landing autosaves the slot (FUN_06d4c430, `LEA ECX,[EAX-0x418]`).
    if (!SinglePlayer() && *ObjId == Owner.ship_object_id)
        PlayerData_AutoSave(Owner);

    // .text:06cf7094 .. .text:06cf70a5 -- IClient +0x0ec (SendServerLand)(player id,
    // &payload). No null test on the interface.
    g_pClientInterface->SendServerLand(View->PlayerId, Wire);
}
