// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_objectlist.cpp -- ObjectList (count_type, collect_in_sphere and the port-only id index).
// Split out of server_classes.cpp unchanged. It uses none of the declarations the other split
// files share (server_classes_internal.h), so it does not include them.

#include "common_server.h"
#include "common_net.h"
#include "common_physys.h"
#include "common_ivp_real_object.h" // IVP_Real_Object +0x1c/+0xa4 (StarSystem_MpCollisionFilter)
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

// ===========================================================================
// ObjectList::count_type -- FUN_06cf5130 (.text:06cf5130, `RET 0x4`) -- "how
// many objects in this dispatch block answer this type mask?".
//
// It is NOT a size read, and it is not an equality test either:
//   .text:06cf5144  8b7704    MOV  ESI,[EDI + 0x4]     ; the block's head
//   .text:06cf5149  8b36      MOV  ESI,[ESI]           ; ... and on from there
//   .text:06cf514f  8b4e08    MOV  ECX,[ESI + 0x8]     ; node->Data, a pointer
//   .text:06cf5154  ff9054010000  CALL [EAX + 0x154]   ; get_type_bits()
//   .text:06cf515a  85c3      TEST EBX,EAX             ; mask & bits
//   .text:06cf515c  74e2      JZ   <next>
//   .text:06cf515e  45        INC  EBP
// Every caller in StarSystem passes 0xffffffff, so in practice it counts the
// nodes whose object reports ANY type bit at all. node->Data is dereferenced
// unguarded, as in retail (the null test the port-only FSystemObjectEntry
// bridge needed is gone with the bridge, 2026-09-24).
// ===========================================================================
unsigned int ObjectList::count_type(unsigned int Mask) const
{
    unsigned int Count = 0;
    for (MetaNode<GameObject>* Node = Head; Node != nullptr; Node = Node->Next) {
        if ((Mask & Node->Data->get_type_bits()) != 0)
            ++Count;
    }
    return Count;
}

// ObjectList::collect_in_sphere -- FUN_06cf5020 (.text:06cf5020 .. 06cf5129,
// `RET 0x14`). PORTED 2026-09-24 (claude-code:w5-server-06d0).
//   .text:06cf5023 .. 06cf5033  FLD / FMUL / FSTP float: Radius * Radius
//   .text:06cf503b .. 06cf5041  MaxCount == 0 (JBE) -> 0
//   .text:06cf504b .. 06cf5060  node = node ? node->Next : Head; null ends it
//   .text:06cf5066 .. 06cf508f  wrapper null, CBase::Object null or (& 3) != 3
//                               -> next
//   .text:06cf5095 .. 06cf509b  CSimple TypeBits (+0xe0) & TypeMask == 0 -> next
//   .text:06cf509d .. 06cf50b3  TypeMask & 0x7f0000: is_destroyed (vt +0x160) -> next
//   .text:06cf50b5 .. 06cf50c4  FCOMP double 0.0 / TEST AH,5 / JP: Radius < 0.0
//                               takes the object without a distance test
//   .text:06cf50d5 .. 06cf5106  (dx*dx + dz*dz) + dy*dy on the x87 stack against
//                               the float square, TEST AH,0x41 / JP: <= keeps it
//   .text:06cf50c6 / 06cf5108   OutIds[Count++] = CSimple Id (+0xb0)
//   .text:06cf5116 .. 06cf511a  loop while Count < MaxCount (unsigned)
unsigned int ObjectList::collect_in_sphere(unsigned int* OutIds, unsigned int MaxCount,
                                           Vector const* Pos, float Radius,
                                           unsigned int TypeMask) const
{
    float const RadiusSq = static_cast<float>(static_cast<double>(Radius) * static_cast<double>(Radius));
    unsigned int Count = 0;
    if (MaxCount == 0u)
        return 0u;

    MetaNode<GameObject>* Node = nullptr;
    do
    {
        Node = (Node == nullptr) ? Head : Node->Next;
        if (Node == nullptr)
            break;

        GameObject* const Wrapper = Node->Data;
        if (Wrapper == nullptr)
            continue;
        CObject* const Raw = Wrapper->Object;
        if (Raw == nullptr || (static_cast<unsigned int>(Raw->ObjectClass) & 3u) != 3u)
            continue;
        CSimple* const Simple = static_cast<CSimple*>(Raw);
        if ((Simple->TypeBits & TypeMask) == 0u)
            continue;
        if ((TypeMask & 0x7f0000u) != 0u && Wrapper->is_destroyed())
            continue;

        if (static_cast<double>(Radius) < 0.0)
        {
            OutIds[Count] = Simple->Id;
            ++Count;
            continue;
        }

        double const Dx = static_cast<double>(Pos->x) - static_cast<double>(Simple->WorldPosition.x);
        double const Dy = static_cast<double>(Pos->y) - static_cast<double>(Simple->WorldPosition.y);
        double const Dz = static_cast<double>(Pos->z) - static_cast<double>(Simple->WorldPosition.z);
        double const DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
        if (DistSq <= static_cast<double>(RadiusSq))
        {
            OutIds[Count] = Simple->Id;
            ++Count;
        }
    } while (Count < MaxCount);
    return Count;
}

// ObjectList::find_by_id -- FUN_06cf4f00 (.text:06cf4f00, `RET 0x4`):
//   .text:06cf4f10 .. 06cf4f1b  node = node ? node->Next : Head; stop on null
//   .text:06cf4f1f .. 06cf4f24  CALL [Data->vtable + 0x20]   ; get_id()
//   .text:06cf4f27  CMP EAX,EDI / JNZ -- the first match ends the walk
//   .text:06cf4f2c  return node (null when nothing matched)
//
// PORT(perf-index): id index (no retail counterpart). StarSystem::find_object runs this
// walk on up to seven blocks per lookup, so a station costs a get_id call on
// every ship, projectile and loot crate first. The index below remembers, per
// block, the FIRST node of each id in list order -- exactly the node the walk
// stops at -- for every node up to the first one whose id cannot be read. It is
// built from that same walk, then kept in step by the ObjectList forwards in
// server_sysdb.h: append and remove update it in place, every other edit (and
// StarSystem_DeleteBlock's own Head/Tail rewrite) marks it stale. The list stays
// the source of every order; the index only answers ids.
//
// Why the answer is the walk's answer:
//   - Duplicate ids keep their first node, and the cross-block order is
//     find_object's own, which is unchanged.
//   - An object's id cannot change while its node is linked and the list is
//     not edited: get_id is IObjInspectImpl's `InspectSimple(Object)->Id`; the
//     wrapper holds a reference on its CObject until its destructor
//     (GameObject::~GameObject), CSimple::Id is only written by CSimple::init,
//     and every create path sets Object and runs init before the append. So an
//     id read at build time or at append time is the one the walk would read.
//   - The one exception, ListC0's create_med_asteroid / create_med_mine, appends
//     while the wrapper's Object is still null (CBase::CBase) and finds the
//     CObject right after. Its id cannot be read at the append, so the append
//     marks the index stale, as any append did before, and the next build
//     reads the id.
//   - An append is recorded only when every earlier node is indexed (nothing
//     unreadable before it) and its own id can be read; the new node is the
//     Tail, so it is the first of its id only if no indexed node has that id.
//   - A remove reads no id: the index keeps each indexed node's id and how many
//     indexed nodes share it. Removing the first node of an id hands "first"
//     to the next indexed node with that id in list order. Removing a node the
//     index does not hold (the first unreadable one or anything after it)
//     marks the index stale.
//   - The build stops at the first node whose get_id would fault (null Data,
//     null Object, or not a CSimple: the walk would crash there). Lookups the
//     index cannot answer continue the original walk from that node, so they
//     return, or fault, exactly where the retail walk does.
//   - While the index is stale, lookups use the plain walk; it is rebuilt only
//     after a few of them, so a list edited between every lookup never costs
//     more than about one extra walk per edit.
// Lookups were already not read-only in retail (Server_FindObject updates its
// one-entry cache), so the index needs no locking beyond what the lists need.
namespace {

struct FObjectListIdEntry {
    MetaNode<GameObject>* First;      // the first indexed node with this id, in list order
    unsigned int Count;               // indexed nodes with this id
};

struct FObjectListIdIndex {
    std::unordered_map<unsigned int, FObjectListIdEntry> ById;
    std::unordered_map<MetaNode<GameObject>*, unsigned int> IdOfNode; // every indexed node
    MetaNode<GameObject>* WalkFrom;   // first node the build could not read; null = whole list
    unsigned int StaleLookups;        // lookups answered by the plain walk since the last edit
    bool bValid;

    FObjectListIdIndex() : WalkFrom(nullptr), StaleLookups(0), bValid(false) {}
};

typedef std::unordered_map<ObjectList const*, FObjectListIdIndex> FObjectListIdIndexTable;

// Created on the first lookup and never freed: the blocks of SysDB::SysMap's
// systems are destroyed during static teardown and still call drop_index.
FObjectListIdIndexTable* GObjectListIdIndexes = nullptr;

// Stale lookups answered by the plain walk before the index is rebuilt.
unsigned int const ObjectListIdIndexRebuildAfter = 4;

// This list's index, or null when it has none yet (no lookup so far).
FObjectListIdIndex* ObjectList_FindIdIndex(ObjectList const* List)
{
    if (GObjectListIdIndexes == nullptr)
        return nullptr;
    FObjectListIdIndexTable::iterator const It = GObjectListIdIndexes->find(List);
    return (It != GObjectListIdIndexes->end()) ? &It->second : nullptr;
}

void ObjectList_MarkStale(FObjectListIdIndex& Index)
{
    Index.bValid = false;
    Index.StaleLookups = 0;
}

// The walk's get_id call on Node cannot fault: Data, its Object, and a CSimple.
bool ObjectList_CanReadId(MetaNode<GameObject> const* Node)
{
    GameObject* const Data = Node->Data;
    CObject* const Raw = (Data != nullptr) ? Data->Object : nullptr;
    return Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 3u) == 3u;
}

// Records Node, which comes after every node already indexed.
void ObjectList_IndexNode(FObjectListIdIndex& Index, MetaNode<GameObject>* Node)
{
    unsigned int const Id = static_cast<unsigned int>(Node->Data->get_id());
    Index.IdOfNode[Node] = Id;
    FObjectListIdEntry& Entry = Index.ById[Id];
    if (Entry.Count == 0)
        Entry.First = Node;
    ++Entry.Count;
}

// The walk below, recorded: every node up to the first one the walk could not
// call get_id on.
void ObjectList_BuildIdIndex(ObjectList const& List, FObjectListIdIndex& Index)
{
    Index.ById.clear();
    Index.IdOfNode.clear();
    Index.WalkFrom = nullptr;
    for (MetaNode<GameObject>* Node = List.Head; Node != nullptr; Node = Node->Next) {
        if (!ObjectList_CanReadId(Node)) {
            Index.WalkFrom = Node;
            break;
        }
        ObjectList_IndexNode(Index, Node);
    }
    Index.bValid = true;
    Index.StaleLookups = 0;
}

// The retail walk (.text:06cf4f10 .. 06cf4f2c), starting at Node; null finds nothing.
MetaNode<GameObject>* ObjectList_WalkById(MetaNode<GameObject>* Node, unsigned int Id)
{
    for (; Node != nullptr; Node = Node->Next) {
        if (Node->Data->get_id() == Id)
            break;
    }
    return Node;
}

} // namespace

void ObjectList::invalidate_index() const
{
    if (FObjectListIdIndex* const Index = ObjectList_FindIdIndex(this))
        ObjectList_MarkStale(*Index);
}

void ObjectList::drop_index() const
{
    if (GObjectListIdIndexes != nullptr)
        GObjectListIdIndexes->erase(this);
}

// After MetaList::append: Node is the new Tail.
void ObjectList::index_appended(MetaNode<GameObject>* Node) const
{
    FObjectListIdIndex* const Index = ObjectList_FindIdIndex(this);
    if (Index == nullptr)
        return;
    if (!Index->bValid || Index->WalkFrom != nullptr || !ObjectList_CanReadId(Node)) {
        ObjectList_MarkStale(*Index);
        return;
    }
    ObjectList_IndexNode(*Index, Node);
}

// Before MetaList::remove: Node is still linked (null is remove's no-op).
void ObjectList::index_removing(MetaNode<GameObject>* Node) const
{
    if (Node == nullptr)
        return;
    FObjectListIdIndex* const Index = ObjectList_FindIdIndex(this);
    if (Index == nullptr)
        return;
    if (!Index->bValid) {
        ObjectList_MarkStale(*Index);
        return;
    }
    std::unordered_map<MetaNode<GameObject>*, unsigned int>::iterator const Indexed =
        Index->IdOfNode.find(Node);
    if (Indexed == Index->IdOfNode.end()) {
        ObjectList_MarkStale(*Index); // WalkFrom or a node after it
        return;
    }
    unsigned int const Id = Indexed->second;
    Index->IdOfNode.erase(Indexed);

    std::unordered_map<unsigned int, FObjectListIdEntry>::iterator const Entry = Index->ById.find(Id);
    if (--Entry->second.Count == 0) {
        Index->ById.erase(Entry);
        return;
    }
    if (Entry->second.First != Node)
        return;
    // The next node with this id in list order; every node between Node and
    // WalkFrom is indexed, and at least one of them has this id.
    for (MetaNode<GameObject>* Next = Node->Next; Next != nullptr && Next != Index->WalkFrom;
         Next = Next->Next) {
        std::unordered_map<MetaNode<GameObject>*, unsigned int>::const_iterator const Other =
            Index->IdOfNode.find(Next);
        if (Other != Index->IdOfNode.end() && Other->second == Id) {
            Entry->second.First = Next;
            return;
        }
    }
    ObjectList_MarkStale(*Index); // not reached while the index matches the list
}

// FUN_06cf4f00 -- see the walk and the index above.
MetaNode<GameObject>* ObjectList::find_by_id(unsigned int Id) const
{
    if (GObjectListIdIndexes == nullptr)
        GObjectListIdIndexes = new FObjectListIdIndexTable;
    FObjectListIdIndex& Index = (*GObjectListIdIndexes)[this];
    if (!Index.bValid) {
        if (Index.StaleLookups < ObjectListIdIndexRebuildAfter) {
            ++Index.StaleLookups;
            return ObjectList_WalkById(Head, Id);
        }
        ObjectList_BuildIdIndex(*this, Index);
    }

    std::unordered_map<unsigned int, FObjectListIdEntry>::const_iterator const Hit = Index.ById.find(Id);
    if (Hit != Index.ById.end()) {
#ifdef _DEBUG
        // PORT(perf-index): cross-check: the index's node is the retail walk's. Every
        // node before it is indexed, so the walk cannot fault on the way.
        assert(ObjectList_WalkById(Head, Id) == Hit->second.First);
#endif
        return Hit->second.First;
    }
    // Every node before WalkFrom has another id; the walk goes on from there.
#ifdef _DEBUG
    // PORT(perf-index): cross-check of the miss: no node the index covers (all readable,
    // so the walk cannot fault here) has this id.
    for (MetaNode<GameObject>* Node = Head; Node != Index.WalkFrom; Node = Node->Next)
        assert(static_cast<unsigned int>(Node->Data->get_id()) != Id);
#endif
    return ObjectList_WalkById(Index.WalkFrom, Id);
}

// ===========================================================================
