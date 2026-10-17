// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_containers.h -- the retail container serializer of rpclocal.dll.
//
// Seven packet types (channel 2: 0x24 SETEQUIPMENT, 0x25 SETCARGO, 0x3d
// REMOTEOBJECTCARGOUPDATE, 0x48 SETCOLLISIONGROUPS; channel 3: 0x16 REQEQUIPMENT,
// 0x17 REQCARGO, 0x36 REQCOLLISIONGROUPS) carry a CONTAINER in their payload rather
// than a flat record. All seven go through the same two mechanisms, and both are
// recovered here byte for byte from EXE/Disasm/rpclocal.dll.txt.
//
// ===========================================================================
// 1. The retail std::list layout, read off the listing (NOT assumed from VC6 lore)
// ===========================================================================
//
// FUN_06d8f710 (list assignment into the packet) is the clearest witness:
//
//   .text:06d8f712  LEA  EBP,[this + 0x20]      ; the destination list lives AT payload+0
//   .text:06d8f723  MOV  ESI,[EBP + 0x4]        ; +0x04 is the dummy node
//   .text:06d8f76c  MOV  ECX,[EBP + 0x8]        ; +0x08 is the element count (DEC/INC'd)
//   .text:06d8f785  PUSH 0x10 ; CALL operator_new
//   .text:06d8f791  MOV  [EAX],ESI              ; node+0x00 = Next
//   .text:06d8f797  MOV  [EAX + 0x4],EDI        ; node+0x04 = Prev
//   .text:06d8f7a2  ADD  EAX,0x8                ; node+0x08 = the value
//   .text:06d8f7a9  MOV  EDX,[EBX + 0x8] ; MOV [EAX],EDX
//   .text:06d8f7ae  MOV  ECX,[EBX + 0xc] ; MOV [EAX + 0x4],ECX
//
// so:
//   header = { allocator (1 byte, padded to 4) @+0x00, Node* Head @+0x04,
//              size_t Size @+0x08 }  -- 0x0c bytes
//   node   = { Node* Next @+0x00, Node* Prev @+0x04, T Value @+0x08 }
//
// The allocator byte is not guesswork either: FUN_06d913f0 at 0x06d9143e does
// `MOV AL,byte ptr [ESP+0x28]` (the low byte of its own param_1 stack slot -- the
// compiler reusing that slot for the default-constructed allocator temporary) and
// stores it at header+0x00 before filling +0x04 and +0x08. Only one byte is ever
// touched there, and nothing ever reads it back.
//
// A THIRD, independent confirmation of the 0x0c header comes from the packet registry
// (rpc_packet_registry.cpp): FLPACKET_SERVER_SETCOLLISIONGROUPS and
// FLPACKET_CLIENT_REQCOLLISIONGROUPS both allocate 0x2c bytes -- 0x20 of CDAPacket base
// plus exactly 0x0c of payload, and their receiver FUN_06d8f7d0 is nothing but
// `LEA EAX,[packet+0x20]; RET`. The list header IS the payload.
//
// Node size is proven per element type by the operator new() argument:
//   * 0x10 for CollisionGroupDesc  (0x06d8f785: PUSH 0x10) -> 8 bytes of value;
//   * 0x0c for the equipment list  (0x06d91519, 0x06d915f5, 0x06d9161f: PUSH 0xc)
//     -> a 4-byte value, and 0x06d9153d stores a POINTER there. The equipment list is
//     a list OF POINTERS to heap records, not a list of records.
//
// ===========================================================================
// 2. The 0x18-byte equipment wire record
// ===========================================================================
//
// Three senders build it identically -- FUN_06d8c750 (channel 2), and the bodies of
// IServerImpl::ReqEquipment (0x06d8cf90) and ::ReqCargo (0x06d8d0e0) inline the same
// code. Each does `PUSH 0x18; CALL operator_new`, zeroes all eight fields, and fills
// them from a real Common.dll EquipDesc:
//
//   rec+0x00 <- EquipDesc+0x04      ArchId        (06d8c7ac / 06d8d01d)
//   rec+0x04 <- EquipDesc+0x14      Count         (06d8c7b1 / 06d8d023)
//   rec+0x08 <- EquipDesc+0x10      Health, FLD/FSTP -- a float (06d8c7b7)
//   rec+0x0c <- EquipDesc+0x02      Id, 16 bits   (06d8c7bd / 06d8d02f)
//   rec+0x0e <- EquipDesc+0x0c      bEquipped, 8 bits (06d8c7c5 / 06d8d037)
//   rec+0x0f <- EquipDesc+0x18      bMission -- only the CHARACTERINFO sender fills it
//                                   (06d89d72); the three above zero it and FUN_06d913f0
//                                   copies it verbatim (06d914e7)
//   rec+0x10 <- strlen(Hardpoint)+1 as a 16-bit count, 0 when the item is internal
//   rec+0x14 <- operator new(that count) + strcpy of EquipDesc+0x08
//
// The hardpoint is copied only when COMMON.DLL::EquipDesc::is_internal() says no
// (06d8c7cd / 06d8d040): internal equipment ships WITHOUT its hardpoint name, and the
// receivers use that same absence as the predicate for calling make_internal().
//
// The two receivers, FUN_06d87ec0/FUN_06d880a0 (into an EquipDescList) and FUN_06d84d60
// (into a std::vector<EquipDesc>), apply the fields back through the named Common.dll
// imports in a fixed order, which is what names the fields above beyond doubt:
//   set_arch_id(rec+0x00), set_count(rec+0x04), set_status(rec+0x08),
//   set_id(rec+0x0c), set_equipped(rec+0x0e),
//   then set_hardpoint(StringAlloc(rec+0x14, 0)) or make_internal().
//
// ===========================================================================
// What this port can and cannot do
// ===========================================================================
//
// CORRECTION 2026-09-17: the paragraph that used to stand here said "rpclocal.dll links
// kernel32 and user32 only -- no Common.dll, so none of EquipDesc, EquipDescList,
// CollisionGroupDesc or their accessors is reachable from here". That was never true of
// retail and is no longer true of the port. Retail rpclocal.dll's import table is
// COMMON.DLL, DACOM.DLL, KERNEL32.DLL, MSVCP60.DLL, MSVCRT.DLL, and it imports the
// EquipDesc accessors BY NAME (.rdata:06db3070..06db308c: make_internal, set_hardpoint,
// set_equipped, set_status, set_count, set_id, set_arch_id, plus is_internal at
// 06db30d8); the project now links Common.lib the same way. The old sentence was a
// property of our own build mistaken for a property of retail.
//
// The module-private MIRRORS below stay, for a different and still-valid reason: they
// are the WIRE layouts -- the 0x18 record, the 0x0c/0x10/0x14 nodes, the 4-byte handle --
// which are rpclocal's own inventions and are not any Common.dll type. No place bridges a
// mirror to a real interface type any more: the real Common types cross the interface
// (former-BRIDGE entry in docs/re/deviations.md).
#pragma once

#include "rpc_transport.h"
#include "rpc_payloads.h"
// EquipDesc / EquipDescList. Retail rpclocal.dll imports EquipDesc::EquipDesc
// (.rdata:06db3004), set_arch_id/set_count/set_status/set_id/set_equipped/set_hardpoint/
// make_internal/is_internal (06db3070..06db308c, 06db30d8) and
// std::list<EquipDesc>::_Buynode (06db30ac) BY NAME, and rpclocal.vcxproj links
// Common.lib, so the real types are what the two REQEQUIPMENT/REQCARGO helpers speak.
#include "common_goods.h"
// The CREATESHIP / CREATESOLAR source record, shared with Server.dll and freelancer.exe.
#include "server_createinfo.h"
#include "server_characterset.h"   // CCharacterSet::CharacterInfo (ch2 0x03)

#include <list>
#include <new>
#include <string>
#include <vector>

// ===========================================================================
// VC6 std::vector<T>::insert(_Last, 1, Value) on a retail-layout vector
//
// Inlined at 06da5945..06da5a1f (ch1 FIREWEAPON decode, T = unsigned short) and
// 06da7ced..06da7e53 (ch1 TRACTOROBJECTS decode, T = unsigned int), and called out of
// line through COMMON.DLL's copy (IAT .rdata:06db3114) at 06da5682. With room
// (`(_End - _Last) >= 1`, unsigned JNC at 06da5945) it stores and bumps _Last. Otherwise
// the new capacity is `Size + max(Size, 1)` (06da594b..06da596e; a null _First counts
// as size 0 and grows by 1), clamped at zero (06da5976), bare operator new of that many
// elements (06da597e), the old elements copied across (06da5993..06da59a5), the value
// stored (06da59ab), the old block freed (06da59e1, operator delete of a null _First
// included), then _End, _Last and _First rewritten (06da59ed..06da5a03).
// ===========================================================================
template <typename T>
void RpcRetailVectorAppend(FRetailVector<T>& Vector, T Value) {
    if ((unsigned int)(Vector.Cap - Vector.End) >= 1u) {
        *Vector.End = Value;
        ++Vector.End;
        return;
    }
    const unsigned int Size =
        (Vector.Begin != nullptr) ? (unsigned int)(Vector.End - Vector.Begin) : 0u;
    unsigned int Growth = 1u;
    if (Vector.Begin != nullptr && Size > 1u) {
        Growth = Size;
    }
    int NewCapacity = (int)(Size + Growth);
    if (NewCapacity < 0) {
        NewCapacity = 0;
    }
    T* Block = static_cast<T*>(
        ::operator new((unsigned int)NewCapacity * (unsigned int)sizeof(T), std::nothrow));
    for (unsigned int i = 0; i < Size; ++i) {
        Block[i] = Vector.Begin[i];
    }
    Block[Size] = Value;
    ::operator delete(Vector.Begin);
    Vector.Cap = Block + NewCapacity;
    Vector.End = Block + Size + 1u;
    Vector.Begin = Block;
}

// ===========================================================================
// The retail std::list, laid out by hand
// ===========================================================================

// One node of a retail list whose value is a CollisionGroupDesc. operator new(0x10).
struct FRpcCollisionGroupNode {
    FRpcCollisionGroupNode* Next;   // +0x00
    FRpcCollisionGroupNode* Prev;   // +0x04
    // CollisionGroupDesc, 8 bytes.
    //
    // UPDATED 2026-09-17 by the CREATESHIP/CREATESOLAR header codec, which is the first
    // thing in rpclocal to look INSIDE the value instead of copying it whole, and which
    // confirms common_cobject.h's reading { ushort Id; ushort Pad; float Health; }:
    //   * +0x00 is an id whose LOW BYTE is all that travels -- 06da3ebd
    //     `MOV CL,byte ptr [ESI+0x8]` out, 06da47d5 `MOV word ptr [EDX+0x8],AX` back in
    //     (a word store over a zero-extended byte), so the high half never leaves;
    //   * +0x04 is a FLOAT in [0,1] -- 06da3ee1 `FLD float ptr [ESI+0xc] ; FMUL 255.0`
    //     out, 06da481d `FMUL (1/255) ; FSTP float ptr [EDX+0xc]` back in.
    // The type below stays `unsigned int` because every other user of this node moves
    // the pair as raw dwords; the codec bit-casts. TODO: retype +0x04 to float once the
    // remaining movers are ported and can be checked in one pass.
    unsigned int Field_0x00;        // +0x08  low byte == CollisionGroupDesc::Id
    unsigned int Field_0x04;        // +0x0c  float health fraction, bit pattern
};

static_assert(sizeof(FRpcCollisionGroupNode) == 0x10,
              "retail CollisionGroupDesc list node is operator new(0x10)");

// The 0x0c-byte list header. For the two COLLISIONGROUPS types this struct IS the
// payload, at packet+0x20, exactly as retail keeps it.
struct FRpcCollisionGroupList {
    unsigned char Allocator;        // +0x00  VC6 allocator<T>: one byte, never read back
    unsigned char Pad_0x01[3];      // +0x01
    FRpcCollisionGroupNode* Head;   // +0x04  dummy node; Next == Prev == itself when empty
    unsigned int Size;              // +0x08
};

static_assert(sizeof(FRpcCollisionGroupList) == 0x0c,
              "retail std::list header is 0x0c: allocator, head, size");

// The 8-byte element on its own, for callers that only want the value.
struct FRpcCollisionGroupDescMirror {
    unsigned int Field_0x00;
    unsigned int Field_0x04;
};

static_assert(sizeof(FRpcCollisionGroupDescMirror) == 8,
              "CollisionGroupDesc mirror must be 8 like the real one");

// ===========================================================================
// The equipment wire record and its list
// ===========================================================================

// operator new(0x18); see the field derivation at the top of this file.
struct FRpcEquipWireRecord {
    unsigned int ArchId;            // +0x00 -> EquipDesc::set_arch_id
    int Count;                      // +0x04 -> EquipDesc::set_count
    float Status;                   // +0x08 -> EquipDesc::set_status (EquipDesc::Health)
    unsigned short Id;              // +0x0c -> EquipDesc::set_id
    unsigned char bEquipped;        // +0x0e -> EquipDesc::set_equipped
    // +0x0f: EquipDesc::bMission. RESOLVED 2026-09-17 by ch2 0x03 CHARACTERINFO, the one
    // user of this record that touches the field at both ends:
    //   sender   06d89d72 "MOV DL,[EBP+0x20] ; MOV [EBX+0xf],DL"  (node+0x20 == the
    //            EquipDesc value's +0x18, since the node's value starts at +0x08)
    //   receiver 06d848fa "MOV AL,[ESI+0xf]" / 06d848fd "MOV [EDI+0x18],AL"  -- straight
    //            into EquipDesc+0x18, without a setter, exactly as caseD_55 does it.
    // The other four users (06d8c79c, 06d8d00d, 06d8d15d and FUN_06d913f0 at 06d914e7)
    // only zero it and copy it, which is why it read as padding for so long.
    unsigned char Field_0x0f;       // +0x0f  EquipDesc::bMission
    unsigned short HardpointSize;   // +0x10  strlen + 1, or 0 for internal equipment
    unsigned char Pad_0x12[2];      // +0x12
    char* Hardpoint;                // +0x14  owned by the record
};

static_assert(sizeof(FRpcEquipWireRecord) == 0x18,
              "retail equipment wire record is operator new(0x18)");

// One node of the equipment list. operator new(0x0c) -- the value is a POINTER.
struct FRpcEquipWireNode {
    FRpcEquipWireNode* Next;        // +0x00
    FRpcEquipWireNode* Prev;        // +0x04
    FRpcEquipWireRecord* Value;     // +0x08
};

static_assert(sizeof(FRpcEquipWireNode) == 0x0c,
              "retail equipment list node is operator new(0x0c)");

// The header of the equipment list. Same 0x0c shape as the collision-group one.
struct FRpcEquipWireListHeader {
    unsigned char Allocator;        // +0x00
    unsigned char Pad_0x01[3];      // +0x01
    FRpcEquipWireNode* Head;        // +0x04
    unsigned int Size;              // +0x08
};

static_assert(sizeof(FRpcEquipWireListHeader) == 0x0c,
              "retail std::list header is 0x0c: allocator, head, size");

// The 4-byte handle behind the equipment payload. FUN_06d8fa90 allocates it with
// operator new(4) and stores its ADDRESS at packet+0x20 -- so the payload's four bytes
// are a pointer to this struct, not the struct itself. The five senders keep an
// identical handle on their own stack while they build the list, then deep-copy it in.
// The registry backs the size up: SETEQUIPMENT, SETCARGO, REQEQUIPMENT and REQCARGO all
// allocate 0x24 bytes, i.e. exactly four bytes of payload.
//
// A class with a destructor in retail: FUN_06d91340 (RpcEquipHandleDestroy) is what
// every EH unwind funclet runs on a live handle -- the receiver's stack handle for
// SETEQUIPMENT / SETCARGO / REMOTEOBJECTCARGOUPDATE (Unwind@06db2163 / @06db217a /
// @06db21fe, [EBP-0x21c]), REQEQUIPMENT / REQCARGO's (Unwind@06db24d9, FUN_06d87ec0 /
// FUN_06d880a0), the senders' (Unwind@06db24f0 / @06db25d0 / @06db25f0), and the handle
// inside a record (+0x264, Unwind@06db2385..) or a create-object header (+0x70,
// Unwind@06db2819 / @06db2879 / @06db2919). The constructor is the inline `MOV [x],0`
// every one of them starts with (06d87efe, 06d8cfcc, ...). So the port's handle
// constructs null and destroys itself; the explicit RpcEquipHandleDestroy calls that
// remain (in a packet's or record's own destructor body, where retail calls it
// directly) leave it null, which makes the member destructor a no-op after them.
// The handles the payload slot points at are raw operator new(4) blocks, as in retail,
// and are torn down by hand (RpcEquipHandleDestroy + operator delete, 06da2205).
struct FRpcEquipWireHandle {
    FRpcEquipWireListHeader* List;  // +0x00, null until the first push_back

    FRpcEquipWireHandle() : List(nullptr) {}
    __declspec(dllexport) ~FRpcEquipWireHandle();         // FUN_06d91340
    FRpcEquipWireHandle(const FRpcEquipWireHandle&) = delete;
    FRpcEquipWireHandle& operator=(const FRpcEquipWireHandle&) = delete;
};

static_assert(sizeof(FRpcEquipWireHandle) == 4,
              "retail equipment payload handle is operator new(4)");

// ===========================================================================
// The two Common.dll containers the senders and receivers speak
// ===========================================================================

// retail std::vector<EquipDesc> is Common's EquipDescVector (common_goods.h): VC6 keeps
// the allocator as a REAL member, so the three pointers start at +0x04. FUN_06d8c750
// proves it directly -- 06d8c755 `MOV EBP,[EAX+0x4]` is begin, 06d8c758
// `MOV EAX,[EAX+0x8]` is end, and the walk advances by 0x20 per element (06d8c818
// `ADD EBP,0x20`), which is sizeof EquipDesc. FUN_06d84d60 reads the same
// +0x04/+0x08/+0x0c triple and computes the capacity as `(cap - begin) >> 5`
// (06d84d92 `SAR EAX,0x5`). EquipDescVector's AllocPad/Begin/End/Cap are that layout.

// retail EquipDescList == std::list<EquipDesc>, with the same { allocator @+0x00,
// dummy node @+0x04, size @+0x08 } header and { Next, Prev, EquipDesc } nodes. This is
// not an inference from the name: the import rpclocal calls to grow it is decorated
// `COMMON.DLL::std::list<struct EquipDesc,...>::_Buynode` (.rdata:0x06db30ac);
// FUN_06d87ec0 calls it once with (0, 0) for the dummy node and once per element with
// (dummy, dummy->Prev), reaches the value at node+0x08 (06d87f63 `ADD EAX,0x8`), keeps
// the count at header+0x08 (06d87f78) and then passes the header's address as
// `EquipDescList const&`.
//
// The PORT does not reproduce that node chain by hand -- retail's node chain was its own
// compiler's std::list, and ours is ours. It DOES use the real EquipDescList, since
// 2026-09-17: the old note here claimed "nothing in this module can construct one" and
// handed IServer::ReqEquipment / ::ReqCargo a std::list<FRpcEquipDescMirror> through a
// reference cast instead. That was false twice over. Retail imports the EquipDesc ctor
// and every one of its setters BY NAME (the IAT slots listed at the top of this file),
// and rpclocal.vcxproj has linked Common.lib since the PLAYER_TRADE equipment sub-types
// were closed. Worse, the cast was not merely unproven, it was wrong: the port's
// EquipDescList was then `{ std::vector<EquipDesc> Items; }`, so reading a std::list
// header through it fed Server.dll a node pointer as a vector begin/end pair. (Since
// 2026-09-25 EquipDescList::Items is a std::list<EquipDesc> again, as in retail; a
// mirror cast would still be wrong, the element types differ.) What stays retail-exact
// is the WIRE format -- the handle, the 0x0c nodes and the 0x18 records above.

// ===========================================================================
// The ported helpers. Every one names the retail function it reproduces.
// ===========================================================================

// FUN_06d913e0 -- `MOV EAX,[ECX]; RET`. The handle's only accessor.
FRpcEquipWireListHeader* RpcEquipHandleGetList(const FRpcEquipWireHandle* Handle);

// FUN_06d91580 -- push_back(Record). Creates the header and its dummy node on the
// first call (operator new(0xc) twice), then links one operator new(0xc) node at the
// tail and bumps Size. The list takes ownership of Record.
void RpcEquipHandlePushBack(FRpcEquipWireHandle* Handle, FRpcEquipWireRecord* Record);

// FUN_06d91340 -- the handle's destructor. For every node: free Record->Hardpoint,
// then the record; then unlink and free every node, the dummy node and the header.
// Retail leaves the handle's own pointer dangling afterwards; the port nulls it.
void RpcEquipHandleDestroy(FRpcEquipWireHandle* Handle);

// FUN_06d913f0 -- deep copy of one handle into another. Allocates a fresh 0x18 record
// per element, including a fresh copy of the hardpoint string.
void RpcEquipHandleCopy(FRpcEquipWireHandle* Dest, const FRpcEquipWireHandle* Source);

// FUN_06d8fa90 -- `if (*(Handle**)payload is null) allocate the handle; deep-copy
// Source into it`.
void RpcPacketEquipAssign(FPacketBase* Packet, const FRpcEquipWireHandle* Source);

// FUN_06d95d20 -- the inverse: `if (payload handle is non-null) copy it into Dest`.
__declspec(dllexport) void RpcPacketEquipExtract(const FPacketBase* Packet, FRpcEquipWireHandle* Dest);

// FUN_06d8c750 -- fills Handle from a retail std::vector<EquipDesc>. bAll false keeps
// only the items whose EquipDesc::bEquipped is 0 (the cargo filter at 06d8c770).
void RpcEquipHandleFromVector(FRpcEquipWireHandle* Handle,
                              const EquipDescVector* Source,
                              bool bAll);

// FUN_06d84d60 -- the inverse: rebuilds a std::vector<EquipDesc> out of a handle. The
// storage behind Begin comes from one operator new; release it with
// RpcEquipDescVectorRelease.
__declspec(dllexport) void RpcEquipVectorFromHandle(EquipDescVector* Dest,
                              const FRpcEquipWireHandle* Source);

// retail 06d88(0)0d / 06d84dca: `std::vector<EquipDesc>::_Destroy(first, last)` then
// operator delete(first).
void RpcEquipDescVectorRelease(EquipDescVector* Vector);

// FUN_06d87ec0 / FUN_06d880a0 -- rebuilds the equipment list out of a handle. Both
// retail functions are the same 153 instructions; the only difference is the IServer
// slot they finish on (+0x70 ReqEquipment vs +0x74 ReqCargo), so the rebuild is
// factored out here and the slot choice stays in the channel-3 codec.
__declspec(dllexport) void RpcEquipListFromHandle(EquipDescList& Dest, const FRpcEquipWireHandle* Source);

// The marshal direction of the same pair: one 0x18 wire record per element. This is
// retail's SENDER, IServerImpl::ReqEquipment's own loop at 0x06d8cff0..0x06d8d08a, not
// the std::vector walk of FUN_06d8c750 that the previous port reused for it.
void RpcEquipHandleFromList(FRpcEquipWireHandle* Handle, const EquipDescList& Source);

// ===========================================================================
// Collision groups
// ===========================================================================

// The payload's own list header, i.e. retail FUN_06d8f7d0 (`LEA EAX,[packet+0x20]`).
FRpcCollisionGroupList* RpcPacketCollisionGroups(FPacketBase* Packet);
__declspec(dllexport) const FRpcCollisionGroupList* RpcPacketCollisionGroups(const FPacketBase* Packet);

// The payload's list header is built by the concrete packet constructor
// (FPacket_FLPACKET_SERVER_SETCOLLISIONGROUPS / _CLIENT_REQCOLLISIONGROUPS in
// rpc_packet_registry.cpp, retail 0x06d9e8b0 / 0x06da10b0). The former
// RpcPacketCollisionGroupsInit stand-in was removed 2026-09-24.

// FUN_06d8f710 -- list assignment into the payload: RpcCollisionGroupListAssignRange
// (below, with the create-object primitives) on RpcPacketCollisionGroups(Packet).

// Walks a retail-layout node chain front to back, for RpcCollisionGroupListAssignRange
// when the SOURCE is itself a retail-layout list (the create-object record copy in
// rpc_containers.cpp). ch2 0x48 SETCOLLISIONGROUPS no longer uses it: since W4-C its
// IClient argument is a typed std::list<CollisionGroupDesc>*.
struct FRpcCollisionGroupNodeIterator {
    const FRpcCollisionGroupNode* Node;

    explicit FRpcCollisionGroupNodeIterator(const FRpcCollisionGroupNode* InNode)
        : Node(InNode) {}
    const FRpcCollisionGroupDescMirror& operator*() const {
        return *reinterpret_cast<const FRpcCollisionGroupDescMirror*>(&Node->Field_0x00);
    }
    FRpcCollisionGroupNodeIterator& operator++() {
        Node = Node->Next;
        return *this;
    }
    bool operator!=(const FRpcCollisionGroupNodeIterator& Other) const {
        return Node != Other.Node;
    }
};

// ===========================================================================
// The CREATELOOT list (ch2 0x28)
//
// Header at payload+0x08 (retail accessor FUN_06d931b0 is `LEA EAX,[this+0x28]`), nodes
// of operator new(0x50) built by FUN_06d93160, values copied field by field by
// FUN_06d93280. Only the sender ever grows it and only the receiver ever walks it, so
// push_back plus a walk is the whole API -- retail has no assign for this one.
// ===========================================================================

FRpcLootWireList* RpcPacketLootList(FPacketBase* Packet);
__declspec(dllexport) const FRpcLootWireList* RpcPacketLootList(const FPacketBase* Packet);

// The packet constructor's share: dummy node pointing at itself, Size 0.
void RpcPacketLootListInit(FPacketBase* Packet);

// FUN_06d93160 -- one operator new(0x50) node at the tail, value copied by
// FUN_06d93280, then `INC [packet+0x30]`.
void RpcPacketLootPushBack(FPacketBase* Packet, const FRpcLootWireRecord& Record);

// Frees the node chain and the dummy node. Retail does it from the packet destructor.
void RpcPacketLootListRelease(FPacketBase* Packet);

// ===========================================================================
// The reputation relation list (ch2 0x29 SETREPUTATION, 0x2b SETGROUPFEELINGS)
//
// std::list<Relation*>: the node is operator new(0xc) and its value is a POINTER to a
// separately operator new(8)'d pair. Retail builds it twice per packet -- the sender
// fills a stack-local list (FUN_06d8cb20 makes the dummy, FUN_06d8cad0 push_backs) and
// then transfers it node by node into the packet (FUN_06d92760 for 0x29, FUN_06d91ca0
// for 0x2b), each transfer allocating a FRESH 8-byte value. The reverse pair
// (FUN_06d927e0 / FUN_06d91d20) does the same on the way out. Because every hop
// re-allocates the value, this port collapses the two hops into one push_back per
// element, which produces byte-identical packet contents.
//
// The list header sits INSIDE the payload (payload+0x28 for 0x29, payload+0x08 for
// 0x2b), so these take the header, not the packet.
// ===========================================================================

// Every list these reach was built by its packet's constructor (FUN_06d9d290 for 0x29,
// FUN_06d9c540 for 0x2b), so none of them builds or tests the dummy. The old idempotent
// RpcRelationListInit / RpcRelationListRelease stand-ins were removed 2026-09-24: the
// marshallers no longer overwrite the constructor's list header, and the packet
// destructors free the lists.

// FUN_06d8cad0 / the tail of FUN_06d92760: operator new(8) for the value, operator
// new(0xc) for the node, link at the tail, ++Size.
void RpcRelationListPushBack(FRpcRelationList* List, const FRpcRelation& Value);

// The node half alone: FUN_06d8cad0's insert of a value the caller already allocated --
// the decoders (06d91b5a / 06d925a2) new(8) the element before reading into it, so a
// failed read leaks it, as retail's does.
void RpcRelationListLink(FRpcRelationList* List, FRpcRelation* Block);

// ===========================================================================
// The mission-objective record list (ch2 0x35 SETMISSIONOBJECTIVES)
//
// Header at payload+0x1c. Again std::list<T*>: the node is operator new(0xc) and holds a
// pointer to an operator new(0xc) record. Retail's push_back is FUN_06d8fc10, which also
// increments payload+0x18 -- the caller here does that separately, because payload+0x18
// belongs to the payload struct and not to the list header.
//
// The accessor is FUN_06d8fc60: a LINEAR walk from the head, returning the Index'th
// node's value or null when the walk reaches the dummy first. Retail really does re-walk
// the list from the start for every element; the receiver's loop is O(n^2) and this port
// keeps the same accessor so the null-on-overrun behaviour is identical.
// ===========================================================================

// The list is the one the packet constructor built (FUN_06d9da40, 06d9daad); the old
// RpcMissionObjectiveListInit / RpcMissionObjectiveListRelease stand-ins were removed
// 2026-09-24 along with the relation-list pair.

// Takes ownership of Record (retail hands FUN_06d8fc10 the operator new(0xc) block it
// just filled, and the packet destructor frees it).
void RpcMissionObjectiveListPushBack(FRpcMissionObjectiveList* List,
                                     FRpcMissionObjectiveRecord* Record);

// FUN_06d8fc60.
__declspec(dllexport) const FRpcMissionObjectiveRecord* RpcMissionObjectiveListAt(
    const FRpcMissionObjectiveList* List, unsigned int Index);

// ===========================================================================
// The create-object header (ch2 0x04 CREATESHIP, 0x42 CREATESOLAR)
//
// These two types are the only users of retail's FUN_06d8c830 (marshal) and
// FUN_06d84ed0 (deliver), a 0x7c-byte prefix that both payloads start with. It is the
// largest structure on channel 2 and the only one carrying three containers at once.
//
// FUN_06d8c830(this, Source, Packet) copies, in this order (packet offsets; payload ==
// packet - 0x20):
//
//   06d8c836/06d8c840  pkt+0x20 <- src+0x04    06d8c885  pkt+0x40 <- src+0x44 (count N)
//   06d8c843/06d8c846  pkt+0x24 <- src+0x08    06d8c888  operator new(N << 2)
//   06d8c849/06d8c84c  pkt+0x28 <- src+0x0c    06d8c891  pkt+0x44 <- that block
//   06d8c84f/06d8c852  pkt+0x2c <- src+0x10    06d8c8a5  block[i] <- src+0x24 + 4*i
//   06d8c855/06d8c858  pkt+0x48 <- src+0x48    06d8c8b8  pkt+0x4c..0x64 <- src+0x4c..0x64
//   06d8c85b/06d8c85e  pkt+0x68 <- src+0x68
//   06d8c861/06d8c864  pkt+0x6c <- src+0x6c
//   06d8c867..06d8c87c pkt+0x30..0x3c <- src+0x14..0x20
//
// then the three containers:
//
//   * src+0x70 is a std::vector<EquipDesc> -- 06d8c8e5 "MOV ESI,[EBX+0x74]" is _First,
//     06d8c8e2 "MOV EAX,[EBX+0x78]" is _Last and 06d8c992 "ADD ESI,0x20" is the stride,
//     so the vector header itself begins at src+0x70 with the VC6 allocator byte. Each
//     element becomes one operator new(0x18) wire record (the same record as the
//     SETEQUIPMENT family above: 06d8c923..06d8c940 fills ArchId/Count/Status/Id/
//     bEquipped, 06d8c945 gates the hardpoint on COMMON.DLL::EquipDesc::is_internal)
//     pushed onto the handle at pkt+0x70 by FUN_06d91580 (06d8c986 "LEA ECX,[EAX+0x70]").
//   * src+0x80 is a std::list<CollisionGroupDesc> assigned into the one at pkt+0x74
//     (06d8c9a1/06d8c9a7); the per-node copy is two dwords, 06d8c9c8/06d8c9ce.
//   * src+0x8c is a std::list<DamageEntry> assigned into the one at pkt+0x80
//     (06d8ca2c/06d8ca32); the per-node copy is three dwords, 06d8ca69..06d8ca7d.
//     The decorated imports name both element types outright -- .rdata:06db309c is
//     "?erase@?$list@UCollisionGroupDesc@@..." and .rdata:06db3060 is
//     "?erase@?$list@UDamageEntry@@...".
//
// and finally the four-field tail, 06d8caae..06d8cac5:
//   pkt+0x8c (BYTE) <- src+0x98,  pkt+0x90 <- src+0x9c,
//   pkt+0x94 <- src+0xa0,         pkt+0x98 <- src+0xa4.
//
// FUN_06d84ed0(Packet, Dest) is the strict inverse of every one of those moves --
// 06d84edd..06d84f04 for the head, 06d84f21 for the array, 06d84f4c for the three
// floats, 06d84f86 (FUN_06d913e0) for the equipment handle, 06d8505c for the collision
// groups, 06d85124 for the damage entries and 06d851a9..06d851c1 for the tail. It goes
// through the real Common.dll setters on the way out (set_arch_id, set_count, set_id,
// set_equipped, set_status, then set_hardpoint(StringAlloc(...)) or make_internal --
// .rdata:06db308c / 3084 / 3088 / 307c / 3080 / 3074 / 3070), which is what names every
// field of the wire record beyond doubt.
//
// One retail detail worth naming and NOT reproducing: 06d84f5e writes 1.0f to Dest+0x58
// and zeroes Dest+0x5c..+0x64 immediately before overwriting all four from the packet.
// That is a constructor default emitted into a function that does not need it; it has no
// observable effect.
// ===========================================================================

// The source/destination record is NOT declared here. It is Server.dll's own
// CEqObjCreateInfo / CShipCreateInfo / CSolarCreateInfo, published once in
// src/include/server_createinfo.h: the sender (Server.dll) builds one and hands its
// address to IClient::SendServerCreateSolar / SendServerCreateShip, the marshallers below read it field by field, and
// the receivers deliver their own static copy of the same type. Retail compiled that
// struct into rpclocal from the same header -- rpclocal carries its own copies of the
// constructor (06d83df0), destructor (06d843b0) and both clear()s (06d83ed0 /
// 06d83fa0), which is what src/include now reproduces with inline members.
//
// The private mirror that stood here (FRpcCreateObjectCommon, with FRpcCreateShipSource
// and FRpcCreateSolarSource derived from it) was deleted 2026-09-23. It reproduced the
// VC6 byte image -- 0x0c-byte list headers at +0x80 / +0x8c -- while Server.dll sent
// its own compiler's std::list and DamageList, so the two sides agreed up to +0x80 and
// read different fields from +0x8c on. It also ended the base at +0xa8 where retail's
// base constructor (06d83eb5) and both base clears (06d83ee5, Server 06cf0275) put
// the byte at +0xa8 on the BASE side.

// The create-object part of both packets, packet+0x20 onwards. Retail builds it in the
// abstract base's constructor inlined into FUN_06d9cc60 (CREATESHIP) and FUN_06d9e630
// (CREATESOLAR), and tears it down in the base part of FUN_06d97b90 / FUN_06d9e740. Its
// EH map, FuncInfo_06db70a4 (handler 06db288a, pushed by both constructors), names the
// sub-objects that have destructors, in construction order:
//   state 0  CDAPacket base            -> Unwind@06db2860  FUN_06da1370
//   state 1  +0x30 (Wire+0x10..+0x27)  -> Unwind@06db2868  FUN_06d95b60, frees +0x44 (Ids)
//   state 2  +0x70 (Wire+0x50)         -> Unwind@06db2873  FUN_06d91340, the handle
//   state 3  +0x74 (Wire+0x54)         -> Unwind@06db287e  std::list<CollisionGroupDesc>::~list
// and the DamageList at +0x80 is the last member built (06d9ccf3 / 06d9e6c8).
//
// The port splits it the same way: FRpcCreateObjectHead is everything up to and including
// the handle, and its destructor is states 1 and 2; FRpcCreateObjectWire adds the two
// Common.dll containers as REAL members, so the compiler's own unwind destroys the list
// if the DamageList constructor throws (state 3), then the head (states 2 and 1), then
// FPacketBase (state 0) -- the retail map, in the retail order. The normal destructor
// runs in the same order as the base part of FUN_06d97b90: DamageList (06d97bff), list
// (06d97c54), handle (06d97c94), id block (06d97ca7).
//
// No retail ABI: the two containers are this toolchain's std::list<CollisionGroupDesc>
// and Common.dll's DamageList (common_misc.h), both imported from Common.dll as retail
// imports them (.rdata:06db30b4, .rdata:06db310c). Their sizes follow the build, so the
// wire header is no longer 0x7c bytes and each payload's tail follows it at
// sizeof(FRpcCreateObjectWire); the retail tail offsets are kept as RELATIVE asserts
// (rpc_containers.cpp). DAMAGEOBJECT made the same move for its DamageList.
struct FRpcCreateObjectHead {
    // 06d9cca2..06d9ccd2 (CREATESHIP) == 06d9e66b..06d9e6a7 (CREATESOLAR).
    FRpcCreateObjectHead();
    // 06d97c94..06d97cb1 == 06d9e81c..06d9e83c.
    ~FRpcCreateObjectHead();
    // The id block and the handle are owned; retail never copies a packet.
    FRpcCreateObjectHead(const FRpcCreateObjectHead&) = delete;
    FRpcCreateObjectHead& operator=(const FRpcCreateObjectHead&) = delete;

    unsigned int Field_0x00;                // +0x00 <- src+0x04
    unsigned int Field_0x04;                // +0x04 <- src+0x08
    unsigned int Field_0x08;                // +0x08 <- src+0x0c
    int VibeGroupId;                        // +0x0c <- src+0x10
    unsigned int Field_0x10;                // +0x10 <- src+0x14
    unsigned int Field_0x14;                // +0x14 <- src+0x18
    unsigned int Field_0x18;                // +0x18 <- src+0x1c
    unsigned int Field_0x1c;                // +0x1c <- src+0x20
    int IdCount;                            // +0x20 <- src+0x44
    unsigned int* Ids;                      // +0x24    operator new(IdCount << 2)
    unsigned int Field_0x28;                // +0x28 <- src+0x48
    float Field_0x2c;                       // +0x2c <- src+0x4c
    float Field_0x30;                       // +0x30 <- src+0x50
    float Field_0x34;                       // +0x34 <- src+0x54
    // CEqObjCreateInfo::Orientation (src+0x58..+0x64), a unit float quaternion. It
    // ships as four quantized bytes in the order x,y,z,w -- the encoder FLDs all four as
    // `float ptr` (06da398f/39a3/39b3/39c5) and scales by 127.0, the decoder sign-extends
    // the byte back through 1/127 (06da42ed..06da433c).
    float Field_0x38;                       // +0x38 <- src+0x58  quaternion w
    float Field_0x3c;                       // +0x3c <- src+0x5c  quaternion x
    float Field_0x40;                       // +0x40 <- src+0x60  quaternion y
    float Field_0x44;                       // +0x44 <- src+0x64  quaternion z
    // +0x48: an INTEGER hit-point count, not a float -- 06da3a9c `FILD dword ptr
    // [EBP+0x68]` on the way out and 06da4394 stores ftol's long back into it. It ships
    // as one byte of percentage against Archetype::GetArchMaxHitPts(Field_0x04).
    unsigned int Field_0x48;                // +0x48 <- src+0x68  current hit points
    unsigned int Field_0x4c;                // +0x4c <- src+0x6c
    FRpcEquipWireHandle Equipment;          // +0x50    packet+0x70, FUN_06d91580's this
};

static_assert(sizeof(FRpcCreateObjectHead) == 0x54,
              "the create-object head ends at packet+0x74, where the collision list starts");

struct FRpcCreateObjectWire : FRpcCreateObjectHead {
    // packet+0x74. Retail constructs this one through Common.dll rather than inline --
    // 06d9cce2 / 06d9e6b7 `CALL dword ptr [->COMMON.DLL::std::list<struct_Collisio...]`,
    // .rdata:06db30b4, whose XREF header lists 06d845c9, 06d899c1, 06d9362c, 06d9cce2
    // and 06d9e6b7 -- and destroys it through the import too (06db2884, the state-3
    // funclet). The element type is named by .rdata:06db309c
    // "?erase@?$list@UCollisionGroupDesc@@...".
    std::list<CollisionGroupDesc> CollisionGroups;
    // packet+0x80, ONE Common.dll DamageList (CEqObjCreateInfo::Damage on the source side;
    // the copy's tail 06d8caae..06d8cac5 reads +0xc/+0x10/+0x14/+0x18 of src+0x8c).
    // Constructed at 06d9ccf3 / 06d9e6c8 through .rdata:06db310c, `??0DamageList@@QAE@XZ`.
    DamageList Damage;
};

// The header-level primitives behind RpcPacketCollisionGroups*, for the retail-image
// collision lists that still exist (SETCOLLISIONGROUPS / REQCOLLISIONGROUPS payloads and
// the CHARACTERINFO record).
void RpcCollisionGroupListInit(FRpcCollisionGroupList* List);
void RpcCollisionGroupListAssign(FRpcCollisionGroupList* Dest,
                                 const FRpcCollisionGroupDescMirror* Items,
                                 unsigned int Count);

// FUN_06d8f710's three phases, generic over the SOURCE. Retail's source is the caller's
// own std::list<CollisionGroupDesc>, whose nodes it walks directly (06d8f72a..06d8f72d,
// 06d8f749, 06d8f7bf); the port's callers hand either their std::list<CollisionGroupDesc>
// -- walked directly as well, no intermediate array -- or an array of 8-byte mirrors. The
// value travels as 8 raw bytes in two dword moves (06d8f739..06d8f742 and
// 06d8f7a9..06d8f7b1), whatever type the iterator yields.
//
// Retail's one guard, `payload == source` (06d8f719), compares two objects of the same
// type; the port's source and destination are never the same object, so it has no
// counterpart here. The destination's dummy node exists already: the caller runs
// RpcCollisionGroupListInit, the stand-in for the packet constructor.
template<typename TIterator>
void RpcCollisionGroupListAssignRange(FRpcCollisionGroupList* List, TIterator First,
                                      TIterator Last) {
    static_assert(sizeof(*First) == 8, "a CollisionGroupDesc value is 8 bytes");
    FRpcCollisionGroupNode* Dummy = List->Head;

    // Phase 1 (06d8f735..06d8f74b): overwrite the values of the nodes already there,
    // stopping at whichever of the two lists runs out first.
    FRpcCollisionGroupNode* Node = Dummy->Next;
    while (Node != Dummy && First != Last) {
        const unsigned int* Value = reinterpret_cast<const unsigned int*>(&(*First));
        Node->Field_0x00 = Value[0];
        Node->Field_0x04 = Value[1];
        Node = Node->Next;
        ++First;
    }

    // Phase 2 (06d8f753..06d8f778): erase the surplus, decrementing Size per node exactly
    // as retail does at 06d8f76c.
    while (Node != Dummy) {
        FRpcCollisionGroupNode* Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        ::operator delete(Node);
        --List->Size;
        Node = Next;
    }

    // Phase 3 (06d8f782..06d8f7c3): append the rest. operator new(0x10) is a bare call
    // (06d8f787, no test); the only test is the placement-new one on the value address
    // (06d8f7a5), which is never null.
    while (First != Last) {
        FRpcCollisionGroupNode* New =
            static_cast<FRpcCollisionGroupNode*>(::operator new(sizeof(FRpcCollisionGroupNode),
                                                                std::nothrow));
        FRpcCollisionGroupNode* Tail = Dummy->Prev;
        New->Next = Dummy;
        if (Tail == nullptr) {
            // retail 06d8f78f: `TEST EDI,EDI ; JNZ ... ; MOV EDI,EAX`.
            Tail = New;
        }
        New->Prev = Tail;
        Dummy->Prev = New;
        New->Prev->Next = New;
        const unsigned int* Value = reinterpret_cast<const unsigned int*>(&(*First));
        New->Field_0x00 = Value[0];
        New->Field_0x04 = Value[1];
        ++List->Size;
        ++First;
    }
}

// FUN_06d8c830 and FUN_06d84ed0, on the shared CEqObjCreateInfo. Both sides' containers
// are already built by their constructors -- the packet's by FRpcCreateObjectWire's, the
// record's by its own -- so both directions assign into them.
void RpcCreateObjectHeaderWrite(FRpcCreateObjectWire* Wire,
                                const CEqObjCreateInfo& Source);
__declspec(dllexport) void RpcCreateObjectHeaderRead(CEqObjCreateInfo& Dest,
                               const FRpcCreateObjectWire* Wire);

// The one conversion left between a retail-image collision list (SETCOLLISIONGROUPS /
// REQCOLLISIONGROUPS keep the VC6 node image in their payload) and the std::list the
// callers hold: 06d8505c-style three-phase assignment, two dwords per node (06d8c9c8 /
// 06d8c9ce). The create-object header no longer needs it -- its list is a real
// std::list<CollisionGroupDesc> -- and the DamageList mirror converters that stood here
// went with the mirror.
__declspec(dllexport) void RpcCollisionGroupListFromWire(std::list<CollisionGroupDesc>& Dest,
                                   const FRpcCollisionGroupList* Source);

// ---------------------------------------------------------------------------
// ch2 0x42 FLPACKET_SERVER_CREATESOLAR -- registry 0xb8, payload 0x98.
//
// Sender 0x06d89190 (IClient+0x0b8), receiver caseD_42 at 0x06d82c62. After the shared
// header the type carries two bytes, two flattened FmtStrs and the reputation pair. The
// whole tail is inside an "if (!SinglePlayer())" on BOTH sides (06d891fb / 06d82dab),
// so in single player the six fields stay zero and the receiver never looks at them.
//
//   06d891d8 pkt+0x9c (BYTE) <- src+0xad     06d82d90 src+0xad <- pkt+0x9c
//   06d891f5 pkt+0x9d (BYTE) <- src+0xa8     06d82d9c src+0xa8 <- pkt+0x9d
//            (read at 06d891e8 `MOV CL,byte ptr [ESI + 0xa8]`, stored at 06d891f5
//            `MOV byte ptr [EDX + 0x9d],CL`. The address that stood here, 06d891e5,
//            is not an instruction start at all -- it is four bytes into 06d891e2.)
//   06d8921d GetAffiliation(&pkt+0x2c, pkt+0xb0, false)
//   06d89239 AttitudeTowards(&pkt+0x2c, &src+0xb0, pkt+0xb4)
//   06d892e6 GetName(&pkt+0x2c, NameA, NameB, &wide)  -- the wide name is DISCARDED here,
//            unlike CREATESHIP which ships it
//   then    pkt+0xa0/0xa4 = flatten(NameA), pkt+0xa8/0xac = flatten(NameB)
//
// and the receiver runs the exact inverse: unflatten(pkt+0xa4, pkt+0xa0) into NameA,
// unflatten(pkt+0xac, pkt+0xa8) into NameB (06d82ec7 / 06d82ede), then EnsureExists,
// SetName(&pkt+0x2c, NameA, NameB, 0), SetAffiliation(&pkt+0x2c, pkt+0xb0, false) and
// SetAttitudeTowardsPlayer(*(&pkt+0x2c), pkt+0xb4) -- 06d82ee4..06d82f1b, ten pushed
// dwords, balanced by the "ADD ESP,0x28" at 06d82f27.
// ---------------------------------------------------------------------------
struct FPayloadServerCreateSolar {
    FRpcCreateObjectWire Common;    // retail +0x00 .. +0x7b; the tail follows at sizeof
    unsigned char Field_0x7c;       // +0x7c <- src+0xad
    unsigned char Field_0x7d;       // +0x7d <- src+0xa8
    unsigned char Pad_0x7e[2];      // +0x7e
    unsigned int NameASize;         // +0x80
    void* NameABuffer;              // +0x84
    unsigned int NameBSize;         // +0x88
    void* NameBBuffer;              // +0x8c
    unsigned int Affiliation;       // +0x90    Reputation::Vibe::GetAffiliation out
    float Attitude;                 // +0x94    Reputation::Vibe::AttitudeTowards out
};
// Retail's payload is 0x98 (operator new(0xb8)); the port's is sizeof(FRpcCreateObjectWire)
// + 0x1c, the tail offsets being asserted relative to Field_0x7c in rpc_containers.cpp.

// The caller-side record is CSolarCreateInfo (src/include/server_createinfo.h). The
// receiver static is .data:0x06dbb838; its one-time init (06d82c83..06d82d73) is that
// type's constructor, and the marshaller reads +0xad, +0xa8 and +0xb0 of it.

// ---------------------------------------------------------------------------
// ch2 0x04 FLPACKET_SERVER_CREATESHIP -- registry 0xec, payload 0xcc.
//
// Sender 0x06d89410 (IClient+0x0bc), receiver caseD_4 at 0x06d81fc5. Same shape as
// CREATESOLAR with a longer tail, and one extra branch: GetName's fourth out-parameter
// is a wide display name, and when it comes back non-empty (06d895f6 `CALL MSVCRT.DLL::wcslen`, tested at 06d895ff / 06d89601) the
// sender ships THAT (FUN_06d97ce0 -> pkt+0xd8 length, pkt+0xdc operator new + wcscpy)
// and leaves both flattened FmtStr slots zero. The receiver unflattens both regardless
// and passes the wide pointer as SetName's fourth argument (06d821ac/06d821be), so an
// empty pair plus a name, or a name pair plus a null, are the two legal states.
//
// Three fields are NARROWED on the way out and widened back on the way in, which is the
// clearest single proof that sender and receiver share one struct type:
//   pkt+0xc0 <- WORD  src+0xb0 (06d89478 read / 06d89483 store)  ->  06d82058 MOVZX
//   pkt+0xc2 <- BYTE  src+0xb4 (06d8948a read / 06d89494 store)  ->  06d82064 MOVZX
//   pkt+0xc4 <- WORD  src+0xd8 (06d8951a read / 06d89528 store)  ->  06d8204b MOVZX
// The three addresses above were 06d89479 / 06d89485 / 06d894fe until 2026-09-17.
// None of the three was an instruction start; each sat inside the instruction before
// it. The three NARROWINGS they describe are real -- see the operands quoted above.
// The high halves never travel.
// ---------------------------------------------------------------------------
struct FPayloadServerCreateShip {
    FRpcCreateObjectWire Common;    // retail +0x00 .. +0x7b; the tail follows at sizeof
    unsigned char Field_0x7c;       // +0x7c <- src+0xdc  (06d8945c read / 06d89462 store,
                                    //        `MOV byte ptr [EAX + 0x9c],DL` == payload+0x7c;
                                    //        the old 06d8943e was inside a CALL opcode)
    unsigned char Pad_0x7d[3];      // +0x7d
    unsigned int Field_0x80;        // +0x80 <- src+0xb8
    float Field_0x84;               // +0x84 <- src+0xbc  (receiver FLDs it, 06d820af)
    float Field_0x88;               // +0x88 <- src+0xc0  (06d820a9)
    unsigned int Field_0x8c;        // +0x8c <- src+0xc4
    float Field_0x90;               // +0x90 <- src+0xc8  (06d8208c)
    float Field_0x94;               // +0x94 <- src+0xcc  (06d82083)
    // CORRECTED 2026-09-17 by the outer wire slots: this is a FLOAT, not a dword. The
    // encoder 06d97e47 does `FLD float ptr [EBX+0xb8]` (packet+0xb8 == payload+0x98) and
    // quantizes it with the 127.0 scale; the decoder 06d9834c..06d9835d sign-extends the
    // byte back (`MOVSX EAX,CL`) and FSTPs 1/127 * it into the same slot. The sender and
    // the receiver only ever MOV it, which is why the type was invisible until now.
    float Field_0x98;               // +0x98 <- src+0xd0, [-1,+1], one quantized byte
    // +0x9c is the wire's own discriminator as well as a value: the encoder sets flag
    // bit 0x04 from `Field_0x9c != 0` (06d97d96) and only then ships its LOW BYTE
    // (06d97f09) plus Field_0xa4 -- and the display name instead of the two FmtStrs.
    unsigned int Field_0x9c;        // +0x9c <- src+0xac
    unsigned short Field_0xa0;      // +0xa0 <- LOW WORD of src+0xb0
    unsigned char Field_0xa2;       // +0xa2 <- LOW BYTE of src+0xb4
    unsigned char Pad_0xa3;         // +0xa3
    unsigned short Field_0xa4;      // +0xa4 <- LOW WORD of src+0xd8
    unsigned char Pad_0xa6[2];      // +0xa6
    unsigned int NameASize;         // +0xa8
    void* NameABuffer;              // +0xac
    unsigned int NameBSize;         // +0xb0
    void* NameBBuffer;              // +0xb4
    unsigned int DisplayNameLength; // +0xb8    wcslen, written but never read back
    wchar_t* DisplayName;           // +0xbc    operator new((len*2)+2) + wcscpy
    unsigned int Affiliation;       // +0xc0
    float Attitude;                 // +0xc4
    unsigned char Field_0xc8;       // +0xc8 <- src+0xa8  (06d8950a read / 06d89514 store,
                                    //        `MOV byte ptr [ECX + 0xe8],AL` == payload+0xc8;
                                    //        the old 06d894f2 was inside 06d894ee)
    unsigned char Pad_0xc9[3];      // +0xc9
};
// Retail's payload is 0xcc (operator new(0xec)); the port's is sizeof(FRpcCreateObjectWire)
// + 0x50, the tail offsets being asserted relative to Field_0x7c in rpc_containers.cpp.

// The caller-side record is CShipCreateInfo (src/include/server_createinfo.h). The
// receiver static is .data:0x06dbb940; its one-time init (06d81fe3..06d82017) is that
// type's constructor. The three fields the wire narrows (+0xb0, +0xb4, +0xd8) are
// DWORDS in the record -- the receiver writes all four bytes of each (MOVZX then a
// dword store).

// ===========================================================================
// ch2 0x03 FLPACKET_SERVER_CHARACTERINFO -- registry 0x30, payload 0x10
//
// Sender 0x06d89890 (IClient+0x034, 492 instructions), receiver
// IServerImpl::HandleCharacterInfo 0x06d84490, which caseD_3 tail-calls at 06d81715.
// The listing decorates that receiver --
// "?HandleCharacterInfo@IServerImpl@@QAE_NPAVCDAPCharacterInfo@@PAVIClient@@I@Z" -- so
// the packet class's retail name is CDAPCharacterInfo and its three arguments are
// (CDAPCharacterInfo*, IClient*, unsigned). That is the only channel-2 packet class
// rpclocal names anywhere.
// This is the character-select list, and it is the only channel-2 type whose payload is
// almost entirely a container:
//
//   payload+0x00  a dword, argument struct +0x14   (06d89e9d..06d89ea8 out,
//                                                   06d84a8c..06d84aa2 back in)
//   payload+0x04  std::list<Record*>, header at packet+0x24 -- FUN_06d955c0 pushes into
//                 it and FUN_06d94460 reads it back, both via "[packet+0x28]" as the
//                 dummy node (06d95ace / 06d94478).
//
// Two DIFFERENT record layouts exist, and conflating them is the trap here:
//   * the CALLER's record is 0x2dc (06d84517 "PUSH 0x2dc") and holds its two id blocks
//     INLINE at +0x274 and +0x2a8, a real EquipDescList at +0x248 and a
//     std::list<CollisionGroupDesc> at +0x254;
//   * the WIRE record is 0x27c (06d8991a / 06d9560b "PUSH 0x27c") and holds POINTERS to
//     two operator new(0x18) id blocks at +0x274/+0x278, the 4-byte equipment handle at
//     +0x264 and the collision list at +0x268.
// Fifteen scalar fields are also PERMUTED between the two, and the receiver's permutation
// is the strict inverse of the sender's -- that agreement is what promotes this layout
// from "read once" to "checked". The pairs are listed on FRpcCharacterInfoWireRecord.
//
// The 0x18 id-block-on-the-wire is the same compaction FUN_06d8c830 uses for the
// create-object array: four dwords, a count, and one heap block of count dwords.
//
// WHAT THIS PORT CANNOT KEEP BYTE-EXACT: the two strings at record+0x204 and +0x214 are
// MSVCP60 std::basic_string, 0x10 bytes each in retail (imports .rdata:06db31a0 and
// 06db31ac). This port has no MSVCP60, so the mirrors below use std::wstring and neither
// record is 0x2dc or 0x27c any more. That costs nothing on the wire: both records are
// heap objects reached only through a pointer, by code on both sides of this module, and
// the only size that has to match retail is the payload's 0x10 -- which is asserted.
// ===========================================================================

// The id block as it travels: 06d89b30 "PUSH 0x18" then the five fields and the array.
struct FRpcIdArrayBlockWire {
    unsigned int Field_0x00;        // +0x00
    unsigned int Field_0x04;        // +0x04
    unsigned int Field_0x08;        // +0x08
    unsigned int Field_0x0c;        // +0x0c
    int Count;                      // +0x10
    unsigned int* Ids;              // +0x14  operator new(Count << 2)
};
static_assert(sizeof(FRpcIdArrayBlockWire) == 0x18,
              "the wire id block is operator new(0x18)");

// The wire record, 0x27c in retail. Field order below is retail's field order; the arrow
// names the CALLER-side offset each one comes from, and every one of those pairs is
// re-inverted by 06d84630..06d8472e in the receiver (06d8472e `MOV dword ptr [EBX +
// 0x23c],EDX` is the last store of the run; the 06d84730 that stood here is inside it).
struct FRpcCharacterInfoWireRecord {
    CHARACTER_ID Name;              // +0x000 CHARACTER_ID::operator= (06d899ef / 06d8462a)
    unsigned char Field_0x200;      // +0x200 <- src+0x200; the receiver re-normalises it
                                    //          to 0/1 with SETNZ (06d84638)
    unsigned char Field_0x201;      // +0x201 <- src+0x201, same SETNZ (06d84649)
    // +0x204 / +0x214: retail MSVCP60 basic_string, 0x10 bytes each. The ELEMENT TYPE is
    // 16-bit, not char: the IAT entries this record's ctor/assign/_Tidy go through are
    // .rdata:06db31a0 / 06db31a4 / 06db31ac / 06db3190, and the listing decorates all
    // four with `?$basic_string@GU?$char_traits@G@std@@V?$allocator@G@2@` -- `G` is
    // `unsigned short`. The codec agrees from the other side: it writes the length as a
    // CHARACTER count and then length*2 bytes (06d94ba8 `SHL ECX,0x1`), the decoder
    // pads with FUN_06d94420, a 16-bit STOSW fill (06d94445), and terminates with
    // `MOV word ptr [EDX + EAX*0x2],0x0` (06d937d0).
    // VC6 spelled wchar_t `unsigned short`, so these are retail's wide strings; the
    // modern equivalent is std::wstring.
    std::wstring TextA;             // +0x204
    std::wstring TextB;             // +0x214
    unsigned int Field_0x224;       // +0x224 <- src+0x224
    unsigned int Field_0x228;       // +0x228 <- src+0x228
    unsigned int Field_0x22c;       // +0x22c <- src+0x22c
    unsigned int Field_0x230;       // +0x230 <- src+0x230
    unsigned int Field_0x234;       // +0x234 <- src+0x234
    unsigned int Field_0x238;       // +0x238 <- src+0x260   (06d89aa6 / 06d846f8)
    unsigned int Field_0x23c;       // +0x23c <- src+0x264   (06d89ad6 / 06d8472e)
    unsigned int Field_0x240;       // +0x240 <- src+0x268   (06d89ae6 / 06d8471c)
    unsigned int Field_0x244;       // +0x244 <- src+0x26c   (06d89ab6 / 06d846c8)
    unsigned int Field_0x248;       // +0x248 <- src+0x270   (06d89ac6 / 06d846d4)
    // +0x24c: a DEAD field of the wire record that nevertheless TRAVELS. Both record
    // constructors zero it (06d89986 in the sender, 06d935f2 in the decoder), the
    // sender never fills it, the receiver never reads it -- but the codec pair carries
    // it like any other dword (06d94f18 out, 06d93afc back in), and the packet-bound
    // second hop copies it 1:1 (06d955c0's `MOV [ESI+0x24c],EDX`). Same class as
    // FPayloadServerSendComm::Field_0x54. It was missing from this struct until
    // 2026-09-17, which silently renamed every field from +0x250 on.
    unsigned int Field_0x24c;       // +0x24c
    // +0x250: retail's ENCODER reads it with `FLD float ptr [EDX + 0x250]` (06d94e8d)
    // and its decoder writes it back with a plain `MOV` (06d93b29), and the sender
    // moves it as a raw dword (06d89af6/06d89b00). The four bytes are the whole story,
    // so the port keeps the bit pattern and marshals it as a dword.
    unsigned int Field_0x250;       // +0x250 <- src+0x244   (06d89af6 / 06d84704)
    unsigned int Field_0x254;       // +0x254 <- src+0x238   (06d89b06 / 06d84710)
    unsigned int Field_0x258;       // +0x258 <- src+0x240   (06d89b16 / 06d8471c)
    unsigned int Field_0x25c;       // +0x25c <- src+0x23c   (06d89b26 / 06d84728)
    // +0x260: the EQUIPMENT ELEMENT COUNT, and it does travel. The first copy hop
    // (06d899a4) only zeroes it; the packet-bound hop FUN_06d955c0 fills it at
    // 06d959c6..06d959d2 -- `FUN_06d913e0(&rec+0x264)` (i.e. *(header**)(rec+0x264)),
    // then `MOV CL,[EAX+0x8] ; MOV [ESI+0x260],CL`, the low byte of the equipment
    // list's Size, or 0 when the handle is still null. The encoder writes it at
    // 06d9528e and the decoder reads it back at 06d93fcf and uses it as the loop bound.
    unsigned char Field_0x260;      // +0x260
    FRpcEquipWireHandle Equipment;  // +0x264 built from the caller's EquipDescList
    FRpcCollisionGroupList CollisionGroups; // +0x268 <- the caller's list at src+0x254
    FRpcIdArrayBlockWire* BlockA;   // +0x274 <- the inline block at src+0x274
    FRpcIdArrayBlockWire* BlockB;   // +0x278 <- the inline block at src+0x2a8
};

// The caller's record, 0x2dc in retail, and the 0x18 argument IClient slot 13 takes:
// CCharacterSet::CharacterInfo and CCharacterSet, declared ONCE for Server.dll, this
// module and freelancer.exe in src/include/server_characterset.h (retail's own names,
// from the OwnerList<CCharacterSet::CharacterInfo> / MetaList<> RTTI in all three).
// This module's FUN_06d852c0 / FUN_06d852e0 / FUN_06d85340 (vtables .rdata:06db3758 /
// 06db3754) are that list's MetaList(bool) / ~MetaList / ~OwnerList; the record's
// destructor pass in ~OwnerList (06d85380 .. 06d853d3) is OwnerListDeleteData's
// specialisation there. The chain is NULL-terminated (MetaList), not circular: the
// sender walks it as "p = p ? p->Next : Head" and stops on null (06d898f0..06d89909).

// The wire list header, at packet+0x24. Same retail 0x0c shape as everywhere else, with
// a pointer value.
struct FRpcCharacterInfoWireNode {
    FRpcCharacterInfoWireNode* Next;        // +0x00
    FRpcCharacterInfoWireNode* Prev;        // +0x04
    FRpcCharacterInfoWireRecord* Value;     // +0x08
};
struct FRpcCharacterInfoWireList {
    unsigned char Allocator;                // +0x00
    unsigned char Pad_0x01[3];              // +0x01
    FRpcCharacterInfoWireNode* Head;        // +0x04
    unsigned int Size;                      // +0x08
};
static_assert(sizeof(FRpcCharacterInfoWireList) == 0x0c, "retail std::list header is 0x0c");

struct FPayloadServerCharacterInfo {
    unsigned int Field_0x00;                // +0x00 <- argument struct +0x14
    FRpcCharacterInfoWireList Records;      // +0x04 .. +0x0f
};
static_assert(sizeof(FPayloadServerCharacterInfo) == 0x10, "CHARACTERINFO payload 0x10");

// FUN_06d955c0 -- the sender's transfer of its temporary list into the packet. Retail
// builds the deep copy twice (once into a stack list, then again into the packet, each
// hop allocating fresh records); every hop re-allocates, so one copy here produces
// identical packet contents. The dummy node already stands: the packet constructor built
// it (FPacket_FLPACKET_SERVER_CHARACTERINFO's constructor, rpc_packet_registry.cpp).
void RpcCharacterInfoWireListPushBack(FRpcCharacterInfoWireList* List,
                                      FRpcCharacterInfoWireRecord* Record);
// Frees every record (its equipment handle, its collision list, its two id blocks and
// their arrays), every node and the dummy. Retail's packet destructor does this.
void RpcCharacterInfoWireListRelease(FRpcCharacterInfoWireList* List);

// FUN_06d94460 -- the RECEIVER's deep copy of the packet's list (header at packet+0x24,
// dummy read from packet+0x28 at 06d94478) into a list of its own: one fresh 0x27c record
// per non-null source record, every owned member re-allocated, pushed at Dest's tail.
// Dest's dummy must already stand.
void RpcCharacterInfoWireListCopy(FRpcCharacterInfoWireList* Dest,
                                  const FRpcCharacterInfoWireList* Source);

// The receiver's inline teardown of ONE copied record, right after it is converted
// (IServerImpl::HandleCharacterInfo 06d849fe..06d84a73): both id blocks, the collision
// list, the equipment handle, the two strings, the record. The node keeps its (now
// dangling) value pointer, as in retail.
void RpcCharacterInfoWireRecordFree(FRpcCharacterInfoWireRecord* Record);

// The two record conversions, i.e. the body of the sender's loop (06d8991a..06d89e46)
// and the body of the receiver's (06d84517..06d849ab).
void RpcCharacterInfoRecordToWire(FRpcCharacterInfoWireRecord* Wire,
                                  const CCharacterSet::CharacterInfo* Source);
void RpcCharacterInfoRecordFromWire(CCharacterSet::CharacterInfo* Dest,
                                    const FRpcCharacterInfoWireRecord* Wire);

// ===========================================================================
// The ch2 0x03 WIRE CODEC -- packet vtable slots +0x04 and +0x08.
//
// Retail: LAB_06d949d0 (encode, 1047 instructions) and FUN_06d934d0 (decode, 1246),
// installed as slots +0x04 / +0x08 of PTR_FUN_06db3dc4 by the CDAPCharacterInfo ctor
// at 0x06d9d54f. Both are `RET 0xc`, i.e. __thiscall with THREE stack arguments --
// the tracker's old "RET 4" note on the encoder was wrong.
//
// The two argument shapes are NOT symmetric, and that asymmetry is retail's:
//   * encode takes a pointer TO the buffer pointer. 06d949dc reads arg1 into ECX and
//     06d949e6 does `MOV EDX,[ECX]` before storing it as the moving cursor; the
//     capacity is arg2, a plain value (06d949d3 / 06d949e3). The shared payload-less
//     encoder LAB_06da13d0 does exactly the same two loads. Retail never writes the
//     caller's pointer back -- the moving cursor lives in packet+0x18.
//   * decode takes the buffer pointer DIRECTLY: 06d934e8 reads arg1 and 06d93506
//     stores it as the cursor with no dereference, and arg2 is again the capacity.
// Both report the byte count through arg3 (06d95577 / 06d943c6 `MOV dword ptr
// [ECX],EAX`; the 06d9439e that stood here is the TEST after the header hook, not the
// report) and return 0, or
// FRpcMarshalStream's 0x16 / 0x17.
//
// Encode's count INCLUDES the two header bytes FUN_06da1380 writes (it sets the cursor
// to 2 at 06da13a6); decode's does NOT, because its header hook FUN_06da13c0 is
// `XOR EAX,EAX ; RET 0xc` -- by then the dispatcher has already consumed the header.
// ===========================================================================
unsigned int RpcEncodeServerCharacterInfo(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerCharacterInfo(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);

// ===========================================================================
// The {dword size, that many raw bytes} pair, retail's only variable-length primitive
// outside the strings. The encode side is inlined at every call site; the decode side
// is the real function FUN_06da1620 (06da1620..06da16a8), which allocates the buffer
// with operator new BEFORE its second bounds check -- so the pointer is stored even
// when the byte run then overruns, and the packet's destructor is what frees it.
//
// Shared out of rpc_containers.cpp because ch2 0x29 SETREPUTATION calls FUN_06da1620
// twice (06d924f4 / 06d924ff) on payload+0x00 and payload+0x08 and inlines the encode
// side the same way the create-object pair does.
// ===========================================================================
bool RpcPutSizedBlock(FRpcMarshalStream& Stream, unsigned int Size, const void* Buffer);
bool RpcGetSizedBlock(FRpcMarshalStream& Stream, unsigned int& OutSize, void*& OutBuffer);

// ===========================================================================
// FUN_06da2240 / FUN_06da2480 -- the equipment/cargo list on the wire, { word Count;
// Count * { dword Count, float Status, dword ArchId, word Id, byte bEquipped, byte
// Field_0x0f, word HardpointSize, HardpointSize bytes } }.
//
// Retail calls both out of line from TWO slot families, not one: 06d916f6 / 06d95c3f are
// the SETEQUIPMENT / SETCARGO / REQEQUIPMENT / REQCARGO slots, and 06d8f902 / 06d8fa17
// are the ch2 0x3d REMOTEOBJECTCARGOUPDATE slots. Those four are every call site in the
// module. The bodies live in rpc_wire_generic.cpp, where the field derivation is.
// ===========================================================================
bool RpcPutEquipList(FRpcMarshalStream& Stream, FRpcEquipWireHandle* const* Slot);
bool RpcGetEquipList(FRpcMarshalStream& Stream, FRpcEquipWireHandle** Slot);

// ===========================================================================
// ch2 codec slots transcribed in rpc_wire_ch2.cpp -- see the file header there for
// how each packet class was tied to its channel/opcode through its constructor.
// ===========================================================================
unsigned int RpcEncodeServerCreateLoot(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerCreateLoot(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSetReputation(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSetReputation(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerMiscObjUpdate(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerMiscObjUpdate(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSendComm(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSendComm(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerCreateGuided(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerCreateGuided(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerCreateMine(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerCreateMine(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerCreateCounter(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerCreateCounter(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSetAddItem(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSetAddItem(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSetMissionObjectives(FPacketBase& Packet,
                                                 unsigned char** BufferHolder,
                                                 unsigned int Capacity,
                                                 unsigned int* OutLength);
unsigned int RpcDecodeServerSetMissionObjectives(FPacketBase& Packet,
                                                 const unsigned char* Buffer,
                                                 unsigned int Size,
                                                 unsigned int* OutLength);

// ===========================================================================
// ch1 codec slots transcribed in rpc_wire_ch1.cpp.
// ===========================================================================
unsigned int RpcEncodeCommonUpdateObject(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonUpdateObject(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
// 0x06da7a00 / 0x06da7b90 -- ch1 0x1d TRACTOROBJECTS: dword, word, word count, then that
// many dwords appended into the payload's own std::vector<unsigned int>.
unsigned int RpcEncodeCommonTractorObjects(FPacketBase& Packet, unsigned char** BufferHolder,
                                           unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonTractorObjects(FPacketBase& Packet, const unsigned char* Buffer,
                                           unsigned int Size, unsigned int* OutLength);

// ===========================================================================
// The shape-only codec slots transcribed in rpc_wire_generic.cpp.
//
// These are NOT per-type codecs: retail folds the small packet classes onto twenty-four
// shared Encode/Decode bodies, and one body serves up to thirty-three vtables. The
// retail address is on each declaration; the type lists and the four pairs of
// near-identical bodies that are NOT interchangeable are documented in the .cpp.
// Between them they cover 86 of the 162 registered types.
// ===========================================================================

// 0x06da13d0 / 0x06da1470 -- no payload, header inlined, cannot fail.
unsigned int RpcEncodeWireEmptyInline(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireEmptyInline(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength);
// 0x06dae0d0 / 0x06dabad0 -- no payload, out-of-line header, CAN return 0x16.
unsigned int RpcEncodeWireEmpty(FPacketBase& Packet, unsigned char** BufferHolder,
                                unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireEmpty(FPacketBase& Packet, const unsigned char* Buffer,
                                unsigned int Size, unsigned int* OutLength);
// 0x06db0310 / 0x06db03f0 -- one dword at payload+0x00.
unsigned int RpcEncodeWireDword(FPacketBase& Packet, unsigned char** BufferHolder,
                                unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDword(FPacketBase& Packet, const unsigned char* Buffer,
                                unsigned int Size, unsigned int* OutLength);
// 0x06db1cd0 -- the same four bytes moved with FLD/FSTP; decodes with RpcDecodeWireDword.
unsigned int RpcEncodeWireFloat(FPacketBase& Packet, unsigned char** BufferHolder,
                                unsigned int Capacity, unsigned int* OutLength);
// 0x06da82e0 / 0x06da83c0 -- one byte at payload+0x00.
unsigned int RpcEncodeWireByte(FPacketBase& Packet, unsigned char** BufferHolder,
                               unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireByte(FPacketBase& Packet, const unsigned char* Buffer,
                               unsigned int Size, unsigned int* OutLength);
// 0x06da9110 / 0x06d95d40 -- two dwords.
unsigned int RpcEncodeWireDwordPair(FPacketBase& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordPair(FPacketBase& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength);
// 0x06dab0e0 / 0x06daf230 -- a dword then a byte, five bytes on the wire.
unsigned int RpcEncodeWireDwordByte(FPacketBase& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordByte(FPacketBase& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength);
// 0x06db0160 / 0x06da6e40 -- three dwords.
unsigned int RpcEncodeWireThreeDwords(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireThreeDwords(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength);
// ===========================================================================
// The plain field-list slots, transcribed in rpc_wire_fields.cpp. Seventeen more retail
// bodies covering twenty-three types; the retail Encode/Decode addresses head each one.
// Attribution is the exhaustive constructor -> vtable -> slot scan described at the top
// of that file, which also re-verified every codec row already in this header.
// ===========================================================================
// 0x06dae620 / 0x06da78d0 -- dword, word, dword. ch1 0x14, ch3 0x3d.
unsigned int RpcEncodeWireDwordWordDword(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordWordDword(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
// 0x06daaf90 / 0x06dacea0 -- dword, dword, word. ch2 0x33, ch2 0x4a.
unsigned int RpcEncodeWireDwordDwordWord(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordDwordWord(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
// 0x06db19b0 / 0x06dad690 -- dword, word, word. ch2 0x51, ch3 0x40.
unsigned int RpcEncodeWireDwordWordWord(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordWordWord(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);
// 0x06da8b50 / 0x06da8cd0 -- dword, dword, word, word. ch1 0x04, ch2 0x38.
unsigned int RpcEncodeWireDwordDwordWordWord(FPacketBase& Packet, unsigned char** BufferHolder,
                                             unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordDwordWordWord(FPacketBase& Packet, const unsigned char* Buffer,
                                             unsigned int Size, unsigned int* OutLength);
// 0x06daffc0 / 0x06d9ab00 -- four flat dwords. ch3 0x12, ch3 0x37.
unsigned int RpcEncodeWireFourDwords(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireFourDwords(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength);
// 0x06db1b60 / 0x06da6a50 -- dword, byte, byte. ch1 0x0e, ch3 0x44.
unsigned int RpcEncodeWireDwordTwoBytes(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordTwoBytes(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);
// 0x06da67a0 / 0x06da68f0 -- ch1 0x08, and the wire reorders the last two fields.
unsigned int RpcEncodeCommonActivateEquip(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonActivateEquip(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
// 0x06da6b80 / 0x06da6cf0 -- ch1 0x0f, three dwords then a byte.
unsigned int RpcEncodeCommonGoTradelane(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonGoTradelane(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);
// 0x06da8e30 / 0x06da8f80 -- ch2 0x0a, dword, byte, dword.
unsigned int RpcEncodeServerActivateObject(FPacketBase& Packet, unsigned char** BufferHolder,
                                           unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerActivateObject(FPacketBase& Packet, const unsigned char* Buffer,
                                           unsigned int Size, unsigned int* OutLength);
// 0x06dab200 / 0x06dab350 -- ch2 0x47, and the wire reorders the last two fields the
// OTHER way round from ch1 0x08's.
unsigned int RpcEncodeServerCfgInterfaceNotification(FPacketBase& Packet,
                                                     unsigned char** BufferHolder,
                                                     unsigned int Capacity,
                                                     unsigned int* OutLength);
unsigned int RpcDecodeServerCfgInterfaceNotification(FPacketBase& Packet,
                                                     const unsigned char* Buffer,
                                                     unsigned int Size,
                                                     unsigned int* OutLength);
// 0x06daeec0 / 0x06daf010 -- ch3 0x14, dword, dword, byte.
unsigned int RpcEncodeClientMissionResponse(FPacketBase& Packet, unsigned char** BufferHolder,
                                            unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientMissionResponse(FPacketBase& Packet, const unsigned char* Buffer,
                                            unsigned int Size, unsigned int* OutLength);
// 0x06daa820 / 0x06daa990 -- ch2 0x44, dword then three consecutive bytes.
unsigned int RpcEncodeServerRequestReturned(FPacketBase& Packet, unsigned char** BufferHolder,
                                            unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerRequestReturned(FPacketBase& Packet, const unsigned char* Buffer,
                                            unsigned int Size, unsigned int* OutLength);
// 0x06daa560 / 0x06daa6d0 -- ch3 0x32, byte, dword, dword, byte.
unsigned int RpcEncodeClientRequestCancel(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientRequestCancel(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
// 0x06da7fe0 / 0x06da8100 -- ch2 0x27, byte then dword.
unsigned int RpcEncodeServerRequestCreateShipResp(FPacketBase& Packet,
                                                  unsigned char** BufferHolder,
                                                  unsigned int Capacity,
                                                  unsigned int* OutLength);
unsigned int RpcDecodeServerRequestCreateShipResp(FPacketBase& Packet,
                                                  const unsigned char* Buffer,
                                                  unsigned int Size,
                                                  unsigned int* OutLength);
// 0x06db12c0 / 0x06db13e0 -- ch3 0x25, dword then word.
unsigned int RpcEncodeClientDbgDestroyObject(FPacketBase& Packet, unsigned char** BufferHolder,
                                             unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientDbgDestroyObject(FPacketBase& Packet, const unsigned char* Buffer,
                                             unsigned int Size, unsigned int* OutLength);
// 0x06daa0c0 -- ch2 0x2a, dword then FLOAT. Its DECODE slot is 0x06d95d40, i.e. the
// dword-pair family's decoder above, which this port already had: the type shares half
// its pair and only the encoder is its own.
unsigned int RpcEncodeServerAdjustAttitude(FPacketBase& Packet, unsigned char** BufferHolder,
                                           unsigned int Capacity, unsigned int* OutLength);
// 0x06dabdf0 / 0x06dabf10 -- ch2 0x08, a dword and then eight bytes, skipping the hole
// at payload+0x04.
unsigned int RpcEncodeServerCharSelectVerified(FPacketBase& Packet,
                                               unsigned char** BufferHolder,
                                               unsigned int Capacity,
                                               unsigned int* OutLength);
unsigned int RpcDecodeServerCharSelectVerified(FPacketBase& Packet,
                                               const unsigned char* Buffer,
                                               unsigned int Size,
                                               unsigned int* OutLength);
// 0x06db1670 / 0x06db17a0 -- a dword then one 128-byte run. ch3 0x2d only.
unsigned int RpcEncodeClientDbgConsole(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientDbgConsole(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);

// --- rpc_wire_varlen.cpp: the two codec pairs whose length is read out of the packet ---
// 0x06daf830 / 0x06daf970 -- { word Length; char Text[Length] }, the strlen of the
// CHARACTER_ID at payload+0x00. ch3 0x06 SELECTCHARACTER and ch3 0x3a DESTROYCHAR share
// the pair outright (.rdata:06db5c88/06db5c98 and 06db5c8c/06db5c9c).
unsigned int RpcEncodeWireCharacterId(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireCharacterId(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength);
// 0x06dacca0 / 0x06dacd70, bodies FUN_06d8f530 / FUN_06d8f5e0 -- { word Count; { word
// Id; float Health; } * Count } over the payload's own FRpcCollisionGroupList. ch2 0x48
// SETCOLLISIONGROUPS and ch3 0x36 REQCOLLISIONGROUPS share the pair outright
// (.rdata:06db52b0/06db5df8 and 06db52b4/06db5dfc).
unsigned int RpcEncodeWireCollisionGroups(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireCollisionGroups(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
// 0x06dabbb0 / 0x06dabd00 -- ch2 0x45 SET_MISSION_MESSAGE, { dword Field_0x08; dword
// TextSize; bytes }. The payload's LAST field leads on the wire; see the .cpp.
unsigned int RpcEncodeServerSetMissionMessage(FPacketBase& Packet,
                                              unsigned char** BufferHolder,
                                              unsigned int Capacity,
                                              unsigned int* OutLength);
unsigned int RpcDecodeServerSetMissionMessage(FPacketBase& Packet,
                                              const unsigned char* Buffer,
                                              unsigned int Size, unsigned int* OutLength);
// 0x06d8f840 / 0x06d8f970 -- ch2 0x3d REMOTEOBJECTCARGOUPDATE, { dword; byte; the
// equipment/cargo list }. Reaches FUN_06da2240 / FUN_06da2480 through the two helpers
// above, the same way retail's slots do.
unsigned int RpcEncodeServerRemoteObjectCargoUpdate(FPacketBase& Packet,
                                                    unsigned char** BufferHolder,
                                                    unsigned int Capacity,
                                                    unsigned int* OutLength);
unsigned int RpcDecodeServerRemoteObjectCargoUpdate(FPacketBase& Packet,
                                                    const unsigned char* Buffer,
                                                    unsigned int Size,
                                                    unsigned int* OutLength);

// 0x06db14f0 / 0x06dad820 -- a dword then one twelve-byte run (FUN_06d8f510).
unsigned int RpcEncodeWireDwordBlock12(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireDwordBlock12(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);
// 0x06d97570 / 0x06d978f0 -- {count, bytes}, payload {Size, Buffer}.
unsigned int RpcEncodeWireBlobSizeFirst(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireBlobSizeFirst(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);
// 0x06da6190 -- {count, bytes}, payload {Buffer, Size}. Four different decoders pair
// with this one encoder; the differences between them are real, see the .cpp.
unsigned int RpcEncodeWireBlobBufferFirst(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
// 0x06d97730 -- allocates unconditionally.
unsigned int RpcDecodeWireBlobBufferFirst(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
// 0x06db10e0 -- skips the allocation and the run when the count is zero.
unsigned int RpcDecodeWireBlobBufferFirstSkipEmpty(FPacketBase& Packet,
                                                   const unsigned char* Buffer,
                                                   unsigned int Size,
                                                   unsigned int* OutLength);
// 0x06da62d0 -- the 0x0c channel-1 buffer family: reuses a buffer already in the payload.
unsigned int RpcDecodeWireOwnedBuffer(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength);
// 0x06da5bc0 -- the same, but allocates Size + 1 and NUL-terminates. ch1 0x11 only.
unsigned int RpcDecodeWireOwnedString(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength);
// 0x06d9a660 -- ch3 0x27 TRADERESPONSE's own decoder. Its ENCODE side is the shared
// RpcEncodeWireDwordPair (0x06da9110); the pair does not round-trip, see rpc_wire_generic.cpp.
unsigned int RpcDecodeWireTradeResponse(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);
// 0x06d90480 / 0x06d906c0 -- ch2 0x05 DAMAGEOBJECT: three scalars, then the list's count
// and one { word, byte, float } per DamageEntry. Lives in rpc_containers.cpp, next to the
// damage-list helpers it needs.
unsigned int RpcEncodeServerDamageObject(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerDamageObject(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
// 0x06dac3c0 / 0x06dac5c0 -- ch2 0x41 BURNFUSE, seven flat fields. In rpc_wire_ch2.cpp.
unsigned int RpcEncodeServerBurnFuse(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerBurnFuse(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength);
// 0x06d916a0 + 0x06da2240 / 0x06d95bf0 + 0x06da2480 -- the equipment/cargo list. Four
// types: ch2 0x24 SETEQUIPMENT, ch2 0x25 SETCARGO, ch3 0x16 REQEQUIPMENT, ch3 0x17
// REQCARGO. A word count then one { dword, float, dword, word, byte, byte, word, run }
// record per element; the payload's four bytes are the handle POINTER, not the handle.
unsigned int RpcEncodeWireEquipList(FPacketBase& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeWireEquipList(FPacketBase& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength);

// ===========================================================================
// The hardpoint-name dictionary (FUN_06da2820 / FUN_06da2980 / FUN_06da2a80)
//
// A fixed 42-entry table in rpclocal's own .data (06dbb770..06dbb814), used by the
// CREATESHIP / CREATESOLAR equipment codec to ship a common hardpoint name as one byte.
// Codes run 1..42; 0 means "not in the table", and both directions fall back to the
// literal string for it. The derivation and the whole table are in rpc_containers.cpp.
// RpcBuildHardpointDictionary (FUN_06da2820, declared in rpc_transport.h) builds it from
// the serializer's construction (retail 06d95f07); both lookups read what it built.
// ===========================================================================
unsigned int RpcHardpointDictionaryCount();
unsigned int RpcHardpointNameToCode(const char* Name);
const char* RpcHardpointCodeToName(unsigned int Code);

// ===========================================================================
// ch2 0x04 CREATESHIP / 0x42 CREATESOLAR -- the shared 0x7c-byte header codec
//
// FUN_06da3720 (encode) and FUN_06da4020 (decode). NOT whole-packet codecs: the
// concrete type's Encode/Decode slot sets the marshalling cursor up, deals with the
// routing header, calls one of these for the shared header, and then writes its own
// tail. Both stop at Wire+0x6c -- the four-field wire tail belongs to the slot.
//
// Retail passes three stack dwords on top of the `this` in ECX (all 22 `RET 0xc`
// returns of the encoder and both of the decoder prove the count) and reads none of
// them: the state is entirely the packet's own four marshalling dwords, which is what
// the FRpcMarshalStream view is. Return value is 0, or the direction's sticky error --
// FRpcMarshalStream::kEncodeOverflow / ::kDecodeOverrun.
// ===========================================================================
unsigned int RpcEncodeCreateObjectCommon(FRpcMarshalStream& Stream,
                                         const FRpcCreateObjectWire* Wire);
unsigned int RpcDecodeCreateObjectCommon(FRpcMarshalStream& Stream,
                                         FRpcCreateObjectWire* Wire);

// ===========================================================================
// The four OUTER wire slots of the create-object pair
//
//   ch2 0x04 CREATESHIP   encode 0x06d97d20 (vtable .rdata:06db3fc8)
//                         decode 0x06d98250 (vtable .rdata:06db3fcc)
//   ch2 0x42 CREATESOLAR  encode 0x06dac7c0
//                         decode 0x06daca80 (vtable .rdata:06db52a4)
//
// These are the packet vtable's +0x04 / +0x08 entries themselves. Each one sets the
// four marshalling dwords up, runs the two-byte routing header hook, calls the shared
// 0x7c header codec above, ships its own tail and reports the byte count. All four are
// `RET 0xc` -- three stack arguments on top of the `this` in ECX -- and they pass all
// three straight through to FUN_06da3720 / FUN_06da4020, which read none of them.
//
// THE ARGUMENT ASYMMETRY IS REAL AND IT IS PER DIRECTION, confirmed on both pairs:
//   * encode's first argument is a pointer TO the buffer pointer. 06d97d37
//     `MOV ECX,dword ptr [EAX]` and 06dac7d4 `MOV ECX,dword ptr [EAX]` dereference it
//     before it becomes the cursor at packet+0x18. Neither writes the pointer back.
//   * decode's first argument IS the buffer. 06d98250/06d9826b and 06daca80/06daca9a
//     store it as the cursor with no `MOV reg,[reg]` anywhere in between.
//
// The header hooks differ by direction too: encode calls FUN_06da1380, which writes the
// two routing bytes and sets the cursor to 2, so its count INCLUDES them; decode calls
// FUN_06da13c0 (`XOR EAX,EAX ; RET 0xc`) at BOTH ends, so its count excludes them.
// ===========================================================================
unsigned int RpcEncodeServerCreateShip(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerCreateShip(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerCreateSolar(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerCreateSolar(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);

// ===========================================================================
// ch1 0x02 FLPACKET_COMMON_FIREWEAPON -- the adaptive-precision direction codec.
//
// Packet vtable PTR_FUN_06db4388 = { FUN_06d9b000, LAB_06da4f60, LAB_06da5550,
// IServerImpl::GetServerStats }, published by the type's constructor FUN_06d9afb0 at
// 06d9afe6; the very next instruction (06d9afec `MOV [EAX+0xc],0x10002`) is what names
// the type -- channel 1, opcode 2. Slot +0x04 is the encoder and +0x08 the decoder,
// as everywhere else on this channel.
// ===========================================================================
unsigned int RpcEncodeCommonFireWeapon(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonFireWeapon(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);

// ===========================================================================
// ch3 0x22 FLPACKET_CLIENT_DBGCREATESHIP -- LAB_06db0560 / LAB_06db0b30, slots +0x04
// and +0x08 of PTR_FUN_06db5d54, the table the constructor FUN_06da0e20 publishes at
// 06da0e89 one instruction before it writes 0x30022 to packet+0x0c. The derivation and
// the payload layout are on FPayloadClientDbgCreateShip in rpc_payloads.h.
//
// This type has no sender in the release image, so the codecs are reachable only from
// the network Encode/Decode path, never from the single-player queue.
// ===========================================================================
unsigned int RpcEncodeClientDbgCreateShip(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientDbgCreateShip(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
// The type's constructor FUN_06da0e20 is FPacket_FLPACKET_CLIENT_DBGCREATESHIP's, with
// its identity quaternion, in rpc_packet_registry.cpp.

// ===========================================================================
// ch1 0x12 FLPACKET_COMMON_PLAYER_TRADE -- LAB_06da7030 / LAB_06da7430, slots +0x04 and
// +0x08 of PTR_FUN_06db4498, published by the constructor FUN_06d9bad0 at 06d9bae4 one
// instruction before it writes 0x10012 to packet+0x0c. Eight sub-types behind one
// jump table; the full derivation, including the inverted IsMPServer test and the
// decoder's one stray 0x16, is in rpc_containers.cpp.
// ===========================================================================
unsigned int RpcEncodeCommonPlayerTrade(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonPlayerTrade(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);
// FUN_06d9bad0's one non-zero default, EquipArchId = 0xffffffff, is stored by
// FPacket_FLPACKET_COMMON_PLAYER_TRADE's constructor in rpc_packet_registry.cpp.

// ===========================================================================
// The flattened FmtStr (ch1 0x1b, ch2 0x29, 0x34, 0x35, 0x45)
//
// Retail rpclocal imports COMMON.DLL::FmtStr::flatten, ::unflatten and
// ::Val::get_flattened_size (IAT .rdata:06db30dc / 06db3008 / 06db30e0) and the port
// links Common.lib to do the same. The senders that inline the size walk instead of
// calling the export compute exactly what get_flattened_size computes -- five separate
// copies of the same six instructions, listed in rpc_payloads.h.
// ===========================================================================

// Base 6 (the mad-lib id dword plus the ushort node count) plus one term per top node.
unsigned int RpcFlattenedFmtStrSize(const FmtStr& Source);

// `MOV [size],N ; PUSH N ; CALL operator_new ; MOV [buffer],EAX ; CALL FmtStr::flatten`.
// The packet owns the returned block; its class destructor frees it, as the
// retail per-type packet destructor does. Returns null (and OutSize 0) on failure.
void* RpcFlattenFmtStr(const FmtStr& Source, unsigned int& OutSize);

// `PUSH size ; PUSH buffer ; CALL FmtStr::unflatten`. A null buffer is fine: unflatten
// clears the object and returns 0 (src/Common/common_fmtstr.cpp, FmtStr::unflatten).
__declspec(dllexport) void RpcUnflattenFmtStr(FmtStr& Dest, void* Buffer, unsigned int Size);

// ===========================================================================
// The closeout wave: the last 22 codec slots, and the twelve per-type dump slots.
//
// rpc_wire_closeout.cpp holds the thirteen channel-1 / channel-2 pairs, rpc_wire_ch3.cpp
// the nine channel-3 pairs and rpc_packet_dump.cpp the dumps. With these, every one of
// the 162 registered types has its real vtable +0x04 / +0x08 body, and FPacketBase's
// Encode / Decode no longer fall back to blitting our in-memory payload.
//
// Each file header carries the vtable address the type constructor publishes and the
// per-field derivation. The three retail asymmetries they reproduce -- ch1 0x1b's second
// block length, ch2 0x2b's missing relation count and ch2 0x32's unallocated destination
// -- are written up in docs/re/deviations.md.
// ===========================================================================
unsigned int RpcEncodeCommonChatMsg(FPacketBase& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonChatMsg(FPacketBase& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeCommonPopUpDialog(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeCommonPopUpDialog(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerLaunch(FPacketBase& Packet, unsigned char** BufferHolder,
                                   unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerLaunch(FPacketBase& Packet, const unsigned char* Buffer,
                                   unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSystemSwitchIn(FPacketBase& Packet, unsigned char** BufferHolder,
                                           unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSystemSwitchIn(FPacketBase& Packet, const unsigned char* Buffer,
                                           unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSetGroupFeelings(FPacketBase& Packet, unsigned char** BufferHolder,
                                             unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSetGroupFeelings(FPacketBase& Packet, const unsigned char* Buffer,
                                             unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerRequestSpaceScript(FPacketBase& Packet, unsigned char** BufferHolder,
                                               unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerRequestSpaceScript(FPacketBase& Packet, const unsigned char* Buffer,
                                               unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerReplaceMissionObjective(FPacketBase& Packet, unsigned char** BufferHolder,
                                                    unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerReplaceMissionObjective(FPacketBase& Packet, const unsigned char* Buffer,
                                                    unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSetCamera(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSetCamera(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerLoadHint(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerLoadHint(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSetDirective(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSetDirective(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerPlayerList(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerPlayerList(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerObjectCargoUpdate(FPacketBase& Packet, unsigned char** BufferHolder,
                                              unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerObjectCargoUpdate(FPacketBase& Packet, const unsigned char* Buffer,
                                              unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeServerSetMusic(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeServerSetMusic(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientLogin(FPacketBase& Packet, unsigned char** BufferHolder,
                                  unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientLogin(FPacketBase& Packet, const unsigned char* Buffer,
                                  unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientMunCollision(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientMunCollision(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientObjCollision(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientObjCollision(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientReqAddItem(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientReqAddItem(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientReqModifyItem(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientReqModifyItem(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientSaveGame(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientSaveGame(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientMineAsteroid(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientMineAsteroid(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientRequestEvent(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientRequestEvent(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength);
unsigned int RpcEncodeClientCreateNewChar(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength);
unsigned int RpcDecodeClientCreateNewChar(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength);

// Packet vtable slot +0x0c: eleven bodies serving twelve types. See rpc_packet_dump.cpp.
void RpcDumpServerConnectResponse(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpServerLoginResponse(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpServerCharacterInfo(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpServerLaunch(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpServerCharSelectVerified(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpServerSetReputation(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpServerRepairObject(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpClientLogin(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpClientRequestLaunch(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpWireCharacterIdDump(const FPacketBase& Packet, std::ostream& Out);
void RpcDumpClientCreateNewChar(const FPacketBase& Packet, std::ostream& Out);


// The two CHARACTERINFO list destructors (FUN_06d852e0 / FUN_06d85340, read off the raw
// listing on 2026-09-17) are ~MetaList / ~OwnerList of OwnerList<CCharacterSet::
// CharacterInfo> -- the template in src/include/common_metalist.h, instantiated by
// src/include/server_characterset.h. The hand-rolled copies that stood here are gone.
