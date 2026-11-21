// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_metalist.h" // MetaList<T>, OwnerList<T>
#include "common_goods.h"    // CharacterProperties, MissionVendorProperties, ...
#include "common_iface.h"    // ScriptBehavior
#include <map>

class BaseData;
class RoomData;
struct FmtStr;

// ============================================================================
// gf/BaseDB.cpp -- Server.dll's runtime base / room database.
//
// Retail keeps one static BaseDB object at .data:DAT_06d8da70 (constructed by
// the __xc initializer FUN_06d15620 -> FUN_06d13550, destroyed through the
// atexit thunk FUN_06d15640 -> FUN_06d13960). FUN_06d15650 is the accessor:
// its whole body is `MOV EAX,0x06d8da70 / RET`.
//
// Every live game-framework record (NPC characters, their script behaviours,
// ambient scripts, mission-vendor offers, news broadcasts) is a heap
// BroadcastRecord in a per-room or per-base record list, and every session
// that asked for a base's / room's info is a payload in that base's / room's
// session list. Creating, changing or destroying a record pushes the change
// to every session in the list that passes the record's session filter.
//
// Class names come from Server.dll's RTTI where it has any (the COL at
// vtable-4): BroadcastRecord (.rdata:06d68384), CharacterRecord (06d68374),
// BehaviorRecord (06d68394), AmbientScriptRecord (06d683c0),
// MissionComputerRecord (06d6843c), NewsBroadcastRecord (06d6844c), and the
// MetaList<T> instantiations the record lists use (06d68348 CharacterRecord,
// 06d68340 BehaviorRecord, 06d68338 AmbientScriptRecord, 06d683d8
// MissionComputerRecord, 06d683d0 NewsBroadcastRecord, 06d68324 unsigned int).
// The base, room and record-list classes have no vtable and therefore no
// recoverable name; FBaseEntry / FRoomEntry / FRecordList / FSessionList /
// FHandlePool are the port's names for them. Implementation:
// src/Server/server_basedb.cpp.
// ============================================================================

// ----------------------------------------------------------------------------
// FSessionList -- the ids of the sessions subscribed to a base or a room
// (base+0x40, room+0x54), each a heap-allocated unsigned int it owns.
//
// Retail's object has the dynamic type MetaList<unsigned int>, not a class of
// its own: the constructor FUN_06d115c0 stores exactly one vptr, MetaList<
// unsigned int>'s (.text:06d115c4 -> .rdata:06d68324, whose only slot is the
// scalar deleting destructor FUN_06d117d0), and Server.dll has no OwnerList<
// unsigned int> vftable or TypeDescriptor at all. The payload ownership lives
// OUTSIDE MetaList's virtual destructor: FUN_06d115e0, called directly as the
// member destructor (.text:06d11d22 room, .text:06d12a22 base, and the unwind
// funclets 06d61a87 / 06d61ac7 / 06d61b5c / 06d61b8c), deletes every payload
// and releases the nodes, and only THEN runs MetaList<unsigned int>'s
// destructor inline (vptr restamp .text:06d1163c, Tail = Head = 0, free_heap).
// A class DERIVED from MetaList would carry its own vftable (its destructor
// overrides the virtual one), so this is a non-polymorphic class holding the
// MetaList at +0x00 -- the same shape FRecordList<T> has below.
// ----------------------------------------------------------------------------
class FSessionList
{
public:
    // FUN_06d115c0: MetaList<unsigned int>(false) on the member -- Head, Tail,
    // FreeList = 0, bUseHeap = 0 -- and the only vptr store is MetaList's.
    FSessionList()
        : List(false)
    {
    }

    // FUN_06d115e0: free() inlined (.text:06d115f0 .. 06d11635), then the
    // member's ~MetaList<unsigned int> (.text:06d11637 .. 06d11661).
    ~FSessionList() { free(); }

    // The room / base destructors call it before the member destructor runs
    // (.text:06d11d0f room, .text:06d12a0f base). Retail's target is the
    // ICF-folded OwnerList<CargoDesc>::free body (.text:06ce57a0): the element
    // type has a trivial destructor, so the code is the same for any T.
    // Phase 1: delete every payload and null its slot. Phase 2: every node to
    // FreeList (bUseHeap) or deleted, then Tail = Head = 0 (MetaList::free).
    void free();

    // FUN_06d11670: the first node whose payload equals SessionId; payloads are
    // dereferenced with no null test (.text:06d1168f).
    bool contains(unsigned int SessionId) const;

    // FUN_06d116b0: duplicate scan, operator new(4) payload, FUN_06d11850 node
    // (MetaList::alloc_node), tail link.
    void add(unsigned int SessionId);

    // FUN_06d11720: find, delete the payload, unlink, pool-or-delete the node.
    void remove(unsigned int SessionId);

    // FUN_06d117c0: `MOV EAX,[ECX+4] / TEST / SETZ` -- Head == 0.
    bool empty() const { return List.get_head() == nullptr; }

    // MetaList<unsigned int>::traverse on the list itself: the record lists walk
    // the sessions through their FSessionList pointer, which is the MetaList's
    // own address (it sits at +0x00).
    bool traverse(MetaNode<unsigned int>*& Node, bool bForward) const
    {
        return List.traverse(Node, bForward);
    }

private:
    MetaList<unsigned int> List; // +0x00 .. +0x13

    FSessionList(FSessionList const&) = delete;
    FSessionList& operator=(FSessionList const&) = delete;
};

// ----------------------------------------------------------------------------
// BroadcastRecord -- the abstract base of every game-framework record.
// Layout: vptr +0x00, Filter +0x04 (0 = every session, else the one session
// the record is visible to), bDirty +0x08 (set by the creators and by
// BaseDB::set_character_behavior, cleared once a broadcast went out), the
// Common.dll properties object at +0x0c. Its own vtable (.rdata:06d68384) has
// three _purecall slots:
//   +0x00 send_update(sid)          marshal the properties, IClient "update"
//   +0x04 send_destroy(sid)         IClient "destroy" (sid, owner id, key)
//   +0x08 matches(unsigned const&)  LAB_06d11930 in every derived vtable,
//                                   `return this[0xc] == *key` (ICF-shared)
// The destructors are NOT virtual: the record lists delete through the
// concrete type (FUN_06d16dc0 / 16e60 / 16f80 / 17020 are called directly).
// ----------------------------------------------------------------------------
class BroadcastRecord
{
public:
    BroadcastRecord()
        : Filter(0)
        , bDirty(false)
    {
    }

    virtual void send_update(unsigned int SessionId) = 0;
    virtual void send_destroy(unsigned int SessionId) = 0;
    virtual bool matches(unsigned int const& Key) const = 0;

    // FUN_06d11b50 (called or inlined at every dispatch site):
    // `return sid == 0 || filter == 0 || filter == sid;`
    static bool session_passes(unsigned int SessionId, unsigned int RecordFilter)
    {
        return SessionId == 0 || RecordFilter == 0 || RecordFilter == SessionId;
    }

    unsigned int Filter; // +0x04
    bool bDirty;         // +0x08

protected:
    ~BroadcastRecord() {}
};

// operator new(0x70), vtable .rdata:06d68374. Slots: FUN_06d118c0 (IClient
// +0x78), LAB_06d11910 (IClient +0x74 with Props.RoomId, Props.Key).
class CharacterRecord : public BroadcastRecord
{
public:
    void send_update(unsigned int SessionId) override;
    void send_destroy(unsigned int SessionId) override;
    bool matches(unsigned int const& Key) const override { return Props.Key == Key; }

    CharacterProperties Props; // +0x0c
};

// operator new(0x26c), vtable .rdata:06d68394. Slots: FUN_06d11950 (IClient
// +0x80), LAB_06d119a0 (IClient +0x84 with Props.Field04 = room id, key).
class BehaviorRecord : public BroadcastRecord
{
public:
    void send_update(unsigned int SessionId) override;
    void send_destroy(unsigned int SessionId) override;
    bool matches(unsigned int const& Key) const override { return (unsigned int)Props.BehaviorId == Key; }

    ScriptBehavior Props; // +0x0c
};

// operator new(0x20), vtable .rdata:06d683c0. Slots: FUN_06d119d0 (IClient
// +0x8c), LAB_06d11a20 (IClient +0x90 with Props.RoomId, key).
class AmbientScriptRecord : public BroadcastRecord
{
public:
    void send_update(unsigned int SessionId) override;
    void send_destroy(unsigned int SessionId) override;
    bool matches(unsigned int const& Key) const override { return (unsigned int)Props.Key == Key; }

    AmbientScriptProperties Props; // +0x0c
};

// operator new(0x6c), vtable .rdata:06d6843c. Slots: FUN_06d11a50 (IClient
// +0x9c), LAB_06d11aa0 (IClient +0x98 with Props.BaseId, key).
class MissionComputerRecord : public BroadcastRecord
{
public:
    void send_update(unsigned int SessionId) override;
    void send_destroy(unsigned int SessionId) override;
    bool matches(unsigned int const& Key) const override { return (unsigned int)Props.Key == Key; }

    MissionVendorProperties Props; // +0x0c
};

// operator new(0x6c), vtable .rdata:06d6844c. Slots: FUN_06d11ad0 (IClient
// +0xb0), LAB_06d11b20 (IClient +0xac with Props.BaseId, key).
class NewsBroadcastRecord : public BroadcastRecord
{
public:
    void send_update(unsigned int SessionId) override;
    void send_destroy(unsigned int SessionId) override;
    bool matches(unsigned int const& Key) const override { return (unsigned int)Props.Key == Key; }

    NewsBroadcastProperties Props; // +0x0c
};

// ----------------------------------------------------------------------------
// FRecordList<T> -- 0x18 bytes: the owning base's / room's session list
// pointer at +0x00 (set by the owner's init() to its own FSessionList) and a
// MetaList<T> of heap records at +0x04. No vtable of its own.
// ----------------------------------------------------------------------------
template <typename T>
class FRecordList
{
public:
    FRecordList()
        : Sessions(nullptr)
    {
    }

    // The member destructor inlined in FUN_06d11c20 / FUN_06d12900 and the
    // out-of-line FUN_06d161e0 / 16410 / 16640 / 16940 / 16b70: clear(), then
    // ~MetaList<T> (FUN_06d17170 + FUN_06d171b0).
    ~FRecordList() { clear(); }

    // FUN_06d16730 (ambient; inlined for the others in FUN_06d11c20 /
    // FUN_06d12900): delete every record and null its slot, MetaList::free,
    // Sessions = 0.
    void clear()
    {
        MetaNode<T>* Node = nullptr;
        while (Records.traverse(Node, true))
        {
            delete Node->Data;
            Node->Data = nullptr;
        }
        Records.free();
        Sessions = nullptr;
    }

    // FUN_06d162d0 / 16500 / 16800 / 16a30 / 16c60: the first record whose
    // matches(Key) answers true, else return; send_destroy to every session
    // the record's filter admits; delete the record; unlink and pool-or-delete
    // the node.
    void remove(unsigned int const& Key)
    {
        MetaNode<T>* Node = nullptr;
        for (;;)
        {
            if (!Records.traverse(Node, true))
                return;
            if (Node->Data->matches(Key))
                break;
        }
        T* Record = Node->Data;
        MetaNode<unsigned int>* Session = nullptr;
        while (Sessions->traverse(Session, true))
        {
            unsigned int const SessionId = *Session->Data;
            if (BroadcastRecord::session_passes(SessionId, Record->Filter))
                Record->send_destroy(SessionId);
        }
        delete Record;
        Records.remove(Node);
    }

    // FUN_06d17120: send_update to every session the record's filter admits.
    void broadcast(T* Record)
    {
        MetaNode<unsigned int>* Session = nullptr;
        while (Sessions->traverse(Session, true))
        {
            unsigned int const SessionId = *Session->Data;
            if (BroadcastRecord::session_passes(SessionId, Record->Filter))
                Record->send_update(SessionId);
        }
    }

    FSessionList* Sessions; // +0x00
    MetaList<T> Records;    // +0x04

private:
    FRecordList(FRecordList const&) = delete;
    FRecordList& operator=(FRecordList const&) = delete;
};

// The ambient-script list has one member of its own.
class FAmbientScriptList : public FRecordList<AmbientScriptRecord>
{
public:
    // FUN_06d167a0(sid, room id): send_update of every record the session
    // passes, then IClient +0x94 (sid, room id).
    void send_all(unsigned int SessionId, unsigned int RoomId);
};

// ----------------------------------------------------------------------------
// FHandlePool -- std::bitset<4096> (0x200 bytes) + the last-allocated cursor
// at +0x200. Five in retail, each zeroed by its own __xc initializer:
//   NPC characters     DAT_06d8e328 / e528  (LAB_06d118a0)
//   script behaviours  DAT_06d8e120 / e320  (LAB_06d122d0)
//   ambient scripts    DAT_06d8df18 / e118  (LAB_06d12500)
//   mission vendors    DAT_06d8dd10 / df10  (LAB_06d13050)
//   news broadcasts    DAT_06d8db08 / dd08  (LAB_06d13330)
// ----------------------------------------------------------------------------
class FHandlePool
{
public:
    bool test(unsigned int Position) const
    {
        return (Bits[Position >> 5] & (1u << (Position & 0x1f))) != 0;
    }

    // FUN_06d15d80: out-of-range throws through FUN_06d15f90.
    void set(unsigned int Position, bool bValue);

    // FUN_06d15720 (NPC pool): the scan below; returns the bit index + 1, or
    // -1 when every bit is taken.
    int allocate();

    // The copy the four other creators inline (e.g. .text:06d12308 ..
    // .text:06d123eb): the same scan, returning the bit index itself, or
    // 0xffffffff when every bit is taken.
    unsigned int allocate_index();

    unsigned int Bits[0x80]; // +0x000
    unsigned int Last;       // +0x200
};

// ----------------------------------------------------------------------------
// FRoomEntry -- 0x68 bytes, one per room of a base.
// ----------------------------------------------------------------------------
class FRoomEntry
{
public:
    // FUN_06d11b70: members, then the init() body.
    FRoomEntry();
    // FUN_06d11c20.
    ~FRoomEntry();

    // FUN_06d11ec0.
    void init();

    // FUN_06d11ee0: the entering player's own character record.
    void enter(unsigned int SessionId);

    // FUN_06d120b0: an NPC character; returns its handle.
    int create_character(CharacterProperties const& Props, unsigned int RecordFilter);
    // FUN_06d122f0.
    unsigned int create_behavior(ScriptBehavior const& Props, unsigned int RecordFilter);
    // FUN_06d12520.
    unsigned int create_ambient_script(AmbientScriptProperties const& Props, unsigned int RecordFilter);

    // FUN_06d127d0.
    unsigned int get_character_on_placement(unsigned int SessionFilter, int PlacementIndex) const;

    unsigned int BaseId;                       // +0x00  RoomData+0x00
    unsigned int RoomId;                       // +0x04  RoomData+0x04
    RoomData* Data;                            // +0x08
    FRecordList<CharacterRecord> Characters;   // +0x0c (list +0x10)
    FRecordList<BehaviorRecord> Behaviors;     // +0x24 (list +0x28)
    FAmbientScriptList AmbientScripts;         // +0x3c (list +0x40)
    FSessionList Sessions;                     // +0x54

private:
    FRoomEntry(FRoomEntry const&) = delete;
    FRoomEntry& operator=(FRoomEntry const&) = delete;
};

// ----------------------------------------------------------------------------
// FBaseEntry -- 0x54 bytes, one per base.
// ----------------------------------------------------------------------------
class FBaseEntry
{
public:
    // FUN_06d12860: members, then the init() body.
    FBaseEntry();
    // FUN_06d12900.
    ~FBaseEntry();

    // FUN_06d12b10.
    void init();
    // FUN_06d12b30.
    void load(BaseData* Source);

    // FUN_06d12be0: IClient +0x70 (sid, base id, start room).
    void send_start_room(unsigned int SessionId);

    // FUN_06d13070.
    unsigned int create_mission_vendor(MissionVendorProperties const& Props, unsigned int RecordFilter);
    // FUN_06d13270.
    void mission_vendor_acceptance(unsigned long Handle, bool bAccepted, FmtStr const& Reason,
                                   unsigned int SessionId);
    // FUN_06d13350.
    unsigned int create_news_broadcast(NewsBroadcastProperties const& Props, unsigned int RecordFilter);

    unsigned int BaseId;                               // +0x00
    BaseData* Data;                                    // +0x04
    FRoomEntry* Rooms;                                 // +0x08  new[] with a count prefix
    int NumRooms;                                      // +0x0c
    FRecordList<MissionComputerRecord> MissionVendors; // +0x10 (list +0x14)
    FRecordList<NewsBroadcastRecord> NewsBroadcasts;   // +0x28 (list +0x2c)
    FSessionList Sessions;                             // +0x40

private:
    FBaseEntry(FBaseEntry const&) = delete;
    FBaseEntry& operator=(FBaseEntry const&) = delete;
};

// ----------------------------------------------------------------------------
// BaseDB -- DAT_06d8da70.
// ----------------------------------------------------------------------------
class BaseDB
{
public:
    // FUN_06d13550.
    BaseDB();
    // FUN_06d13960.
    ~BaseDB();

    // FUN_06d13cc0 (IServerImpl::Startup through the thunk FUN_06d18840).
    void init();
    // FUN_06d13bf0 (IServerImpl::Shutdown through FUN_06d18850).
    void shutdown();
    // FUN_06d13ef0 (IServerImpl::ElapseTime through FUN_06d18860).
    void update(float DeltaTime);

    // FUN_06d140f0 / FUN_06d14170: find; a miss FDUMPs and returns 0.
    FBaseEntry* find_base(unsigned int Id);
    FRoomEntry* find_location(unsigned int Id);

    // FUN_06d14230 / FUN_06d142c0: RoomData::load / unload of every room.
    void load_base(unsigned int SessionId, unsigned int Id);
    void unload_base(unsigned int SessionId, unsigned int Id);
    // FUN_06d14350 / FUN_06d143e0.
    void enter_location(unsigned int SessionId, unsigned int Id);
    void exit_location(unsigned int SessionId, unsigned int Id);
    // FUN_06d14570 / FUN_06d146e0.
    void base_info(unsigned int SessionId, unsigned int Id, bool bFull);
    void location_info(unsigned int SessionId, unsigned int Id, bool bFull);

    // FUN_06d14870 / FUN_06d149c0 / FUN_06d14ac0.
    int create_character(CharacterProperties const& Props, unsigned int RecordFilter);
    void destroy_character(unsigned int Handle);
    void set_character_behavior(unsigned int Handle, unsigned long Behavior);
    // FUN_06d14b70 / FUN_06d14cc0.
    unsigned int create_behavior(ScriptBehavior const& Props, unsigned int RecordFilter);
    void destroy_behavior(unsigned long Handle);
    // FUN_06d14dc0 / FUN_06d14f10 / FUN_06d15010 / FUN_06d150a0.
    unsigned int create_mission_vendor(MissionVendorProperties const& Props, unsigned int RecordFilter);
    void destroy_mission_vendor(unsigned long Handle);
    int count_mission_vendors(unsigned int Id, unsigned int SessionFilter);
    void mission_vendor_acceptance(unsigned long Handle, bool bAccepted, FmtStr const& Reason,
                                   unsigned int SessionId);
    // FUN_06d15140 / FUN_06d15290.
    unsigned int create_news_broadcast(NewsBroadcastProperties const& Props, unsigned int RecordFilter);
    void destroy_news_broadcast(unsigned long Handle);
    // FUN_06d15390 / FUN_06d15510.
    unsigned int create_ambient_script(AmbientScriptProperties const& Props, unsigned int RecordFilter);
    void destroy_ambient_script(unsigned long Handle);

    FBaseEntry* Bases;                                    // +0x00  new[] with a count prefix
    int NumBases;                                         // +0x04
    std::map<unsigned int, FBaseEntry*> BaseMap;          // +0x08  base id -> base
    std::map<unsigned int, FRoomEntry*> LocationMap;      // +0x1c  room id -> room
    std::map<unsigned int, FRoomEntry*> CharacterMap;     // +0x30  character handle -> room
    std::map<unsigned int, FRoomEntry*> BehaviorMap;      // +0x44  behaviour handle -> room
    std::map<unsigned int, FBaseEntry*> MissionVendorMap; // +0x58  offer handle -> base
    std::map<unsigned int, FBaseEntry*> NewsMap;          // +0x6c  broadcast handle -> base
    std::map<unsigned int, FRoomEntry*> AmbientScriptMap; // +0x80  ambient handle -> room

private:
    BaseDB(BaseDB const&) = delete;
    BaseDB& operator=(BaseDB const&) = delete;
};

// FUN_06d15650: `return &DAT_06d8da70;`
BaseDB* GetBaseDB();

// The three IServerImpl forwarding thunks (each `CALL FUN_06d15650 / MOV ECX,EAX`
// then the member). Retail has no symbol for them; the names are the port's.
// FUN_06d18840 -> BaseDB::init (IServerImpl::Startup, .text:06d44c4f).
void Server_SubsystemInit();
// FUN_06d18850 -> BaseDB::shutdown (IServerImpl::Shutdown, .text:06d451d5).
void Server_SubsystemShutdown();
// FUN_06d18860 -> BaseDB::update(dt) (IServerImpl::ElapseTime, .text:06d4519c;
// the float is ElapseTime's own argument, pushed from ESI).
void Server_SubsystemUpdate(float DeltaTime);
