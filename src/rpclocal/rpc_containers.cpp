// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_containers.cpp -- the port of rpclocal's container serializer.
//
// Every function below names the retail function it reproduces and, where the retail
// body is not obvious, quotes the instruction that fixes the behaviour. The layouts the
// code walks are documented once, at the top of rpc_containers.h.
//
// Ownership, as retail has it:
//   * the equipment handle (payload+0x00 for the four EQUIPMENT/CARGO types) owns its
//     list header, its dummy node, every node, every 0x18 record and every hardpoint
//     string that hangs off a record;
//   * the collision-group list header IS the payload, and owns its dummy node and every
//     node;
//   * the EquipDescList and std::vector<EquipDesc> the receivers build are stack
//     temporaries that die with the call.
//
// Retail's per-type packet classes do the allocation and the teardown in their own
// constructor/destructor, and so does this port (rpc_packet_registry.cpp); what is left
// here are the list and handle primitives those bodies and the codecs share.

#include "rpc_containers.h"
#include "rpc_payloads.h"

// Retail rpclocal imports COMMON.DLL::EquipDesc::is_internal (.rdata:06db30d8) and
// ::make_internal (.rdata:06db3070) by name and uses them as the hardpoint gate on both
// sides of the wire. The project links Common.lib, so the port calls the same two.
#include "common_goods.h"
// ch2 0x04 CREATESHIP / 0x42 CREATESOLAR share a payload codec that calls four more
// COMMON.DLL entry points retail imports by name, plus one data import. IAT slots read
// off the call bytes, not off the Ghidra label:
//   06da376c `ff15 0831db06` -> [06db3108]  Archetype::LargeIDToSmallID
//   06da4091 `ff15 0030db06` -> [06db3000]  Archetype::SmallIDToLargeID
//   06da3aab `ff15 1031db06` -> [06db3110]  Archetype::GetArchMaxHitPts
//   06da3afa `ff15 5c30db06` -> [06db305c]  SinglePlayer
//   06da4883 `a1   0031db06` -> [06db3100]  SUBOBJ_ID_NONE  (a data import: the slot
//                                           holds the ADDRESS of the ushort, and
//                                           06da4888 reads the word through it)
// The three Archetype ones arrive with common_goods.h -> common_archetype.h;
// SinglePlayer and SUBOBJ_ID_NONE come from common_misc.h.
#include "common_misc.h"

#include <cstddef>
#include <cstring>
#include <map>
#include <new>
#include <string>

// ===========================================================================
// Small helpers. Retail calls MSVCRT's operator new -- the VC6 one, which returns NULL
// on failure rather than throwing -- and FUN_06db1dc2 (operator delete) directly. The
// port asks for the same contract with std::nothrow and, like retail, tests the result
// only where the retail site does: a new-expression with an initializer compiles to
// `TEST EAX,EAX ; JZ`, a bare operator new call does not, and a bare call that fails
// faults on the very next store in retail and here alike.
// ===========================================================================

// strlen, spelled the way every retail hardpoint site spells it -- OR ECX,-1 ;
// XOR EAX,EAX ; REPNE SCASB ; NOT ECX, strlen+1 in one step (06d8d04f..06d8d054) --
// with no null test in front. Callers add the 1 back explicitly.
static unsigned int RpcContainerStringLength(const char* Text) {
    unsigned int Length = 0;
    while (Text[Length] != '\0') {
        ++Length;
    }
    return Length;
}

// operator new(SizeWithNul) with no test, then retail's byte loop that stops after
// copying the NUL (06d8d070..06d8d078, 06d91503..06d9150b). The loop is bounded by the
// terminator, not by the size it allocated.
static char* RpcContainerCopyString(const char* Text, unsigned int SizeWithNul) {
    char* Block = static_cast<char*>(::operator new(SizeWithNul, std::nothrow));
    char* Out = Block;
    char Byte;
    do {
        Byte = *Text;
        ++Text;
        *Out = Byte;
        ++Out;
    } while (Byte != '\0');
    return Block;
}

static void RpcContainerInitRecord(FRpcEquipWireRecord* Record) {
    // retail 06d8c78d..06d8c7a3 / 06d8cffe..06d8d014 / 06d9149f..06d914b7: all eight
    // fields written as zero before anything else, in exactly this order.
    Record->ArchId = 0;
    Record->Count = 0;
    Record->Status = 0.0f;
    Record->Id = 0;
    Record->bEquipped = 0;
    Record->Field_0x0f = 0;
    Record->HardpointSize = 0;
    Record->Pad_0x12[0] = 0;
    Record->Pad_0x12[1] = 0;
    Record->Hardpoint = nullptr;
}

static void RpcContainerFreeRecord(FRpcEquipWireRecord* Record) {
    // retail FUN_06d91340 at 06d9135b: `TEST EDI,EDI ; JZ` on the record, then
    // `if (rec+0x14) operator delete(rec+0x14)` (06d91362), then operator delete(rec).
    // The HardpointSize field is NOT consulted.
    if (Record == nullptr) {
        return;
    }
    if (Record->Hardpoint != nullptr) {
        ::operator delete(Record->Hardpoint);
    }
    ::operator delete(Record);
}

// ===========================================================================
// FUN_06d913e0 -- `MOV EAX,dword ptr [ECX]; RET`
// ===========================================================================
FRpcEquipWireListHeader* RpcEquipHandleGetList(const FRpcEquipWireHandle* Handle) {
    return Handle->List;
}

// ===========================================================================
// FUN_06d91580 -- push_back
//
// Retail splits into two halves. When *this is still null (06d9159a
// `CMP dword ptr [EDI],0x0`) it builds the list: operator new(0xc) for the header,
// one byte of allocator into header+0x00, operator new(0xc) for the dummy node, which
// it links to itself (`MOV [EAX],EAX ; MOV [EAX+0x4],EAX`), header+0x04 = the dummy,
// header+0x08 = 0. Then the common half links a new operator new(0xc) node in front of
// the dummy and bumps the size.
// ===========================================================================
// The "build the list" half on its own. Retail inlines it twice, identically: push_back
// 06d9159f..06d915ea and the deep copy 06d91426..06d91469. The header allocation is a
// new-expression, so it is tested (06d915af / 06d91436) and a null header is stored
// into the handle all the same (06d915e8 / 06d91467) before the caller bails out on it.
// The dummy's operator new (06d915c3 / 06d91446) is a bare call: no test.
static FRpcEquipWireListHeader* RpcEquipHandleBuildList(FRpcEquipWireHandle* Handle) {
    FRpcEquipWireListHeader* Header =
        static_cast<FRpcEquipWireListHeader*>(::operator new(sizeof(FRpcEquipWireListHeader),
                                                             std::nothrow));
    if (Header != nullptr) {
        // +0x00 is the list's (empty) allocator object: retail copies one byte of an
        // allocator temporary that holds no value. +0x01..+0x03 are never written.
        Header->Allocator = 0;
        Header->Pad_0x01[0] = 0;
        Header->Pad_0x01[1] = 0;
        Header->Pad_0x01[2] = 0;
        FRpcEquipWireNode* Dummy =
            static_cast<FRpcEquipWireNode*>(::operator new(sizeof(FRpcEquipWireNode),
                                                           std::nothrow));
        Dummy->Next = Dummy;
        Dummy->Prev = Dummy;
        Dummy->Value = nullptr;   // never written by retail; the dummy's value is never read
        Header->Head = Dummy;
        Header->Size = 0;
    }
    Handle->List = Header;
    return Header;
}

// The common half of push_back (06d915ec..06d9161f), which the deep copy also inlines
// (06d9150d..06d9153f). The node's operator new is a bare call; the only test is the
// placement-new one on &Node->Value (06d91615 / 06d91539), which is never null.
static void RpcEquipHandleLinkBack(FRpcEquipWireListHeader* Header, FRpcEquipWireRecord* Record) {
    FRpcEquipWireNode* Dummy = Header->Head;
    FRpcEquipWireNode* Tail = Dummy->Prev;
    FRpcEquipWireNode* Node =
        static_cast<FRpcEquipWireNode*>(::operator new(sizeof(FRpcEquipWireNode), std::nothrow));
    Node->Next = Dummy;
    // retail 06d915ff: `TEST EDI,EDI ; JNZ ... ; MOV EDI,EAX` -- when the dummy's Prev
    // is still null the new node becomes its own predecessor. Our dummy is always
    // self-linked, so the branch never fires; it is reproduced anyway.
    if (Tail == nullptr) {
        Tail = Node;
    }
    Node->Prev = Tail;
    Dummy->Prev = Node;
    Node->Prev->Next = Node;
    Node->Value = Record;
    ++Header->Size;
}

void RpcEquipHandlePushBack(FRpcEquipWireHandle* Handle, FRpcEquipWireRecord* Record) {
    if (Handle->List == nullptr && RpcEquipHandleBuildList(Handle) == nullptr) {
        return;   // 06d915ea JZ LAB_06d91623
    }
    RpcEquipHandleLinkBack(Handle->List, Record);
}

// ===========================================================================
// FUN_06d91340 -- the handle's destructor
// ===========================================================================
void RpcEquipHandleDestroy(FRpcEquipWireHandle* Handle) {
    // 06d91343 `MOV EAX,[EBX] ; TEST EAX,EAX ; JZ` -- the handle itself is not tested.
    if (Handle->List == nullptr) {
        return;
    }
    // Phase 1 (06d91358): free the records, leaving the chain intact. The loop bound
    // re-reads the handle's dummy every round (06d91378..06d9137c).
    for (FRpcEquipWireNode* Node = Handle->List->Head->Next; Node != Handle->List->Head;
         Node = Node->Next) {
        RpcContainerFreeRecord(Node->Value);
    }
    // Phase 2 (06d91381): the handle is re-read and re-tested, then every node is
    // unlinked and freed with the size decremented as retail does, then the dummy.
    FRpcEquipWireListHeader* Header = Handle->List;
    if (Header == nullptr) {
        return;
    }
    FRpcEquipWireNode* Dummy = Header->Head;
    FRpcEquipWireNode* Node = Dummy->Next;
    while (Node != Dummy) {
        FRpcEquipWireNode* Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        ::operator delete(Node);
        --Header->Size;
        Node = Next;
    }
    ::operator delete(Dummy);
    Header->Head = nullptr;
    Header->Size = 0;
    ::operator delete(Header);
    // Retail leaves the caller's handle pointing at the freed header (it never writes
    // [EBX] back). The port nulls it because RpcCreateObjectHeaderRelease runs on the
    // receiver statics, whose handle is filled again by the next packet; how retail
    // keeps those statics alive is a separate ledger item (the "channel-2 close" entry,
    // CREATESHIP / CREATESOLAR dummy nodes). Not a guard: nothing is skipped.
    Handle->List = nullptr;
}

// The destructor of a handle object -- stack locals, record and header members. Retail
// runs FUN_06d91340 on every one of them on both the normal and the unwind path (see the
// struct in rpc_containers.h).
__declspec(dllexport) FRpcEquipWireHandle::~FRpcEquipWireHandle() {
    RpcEquipHandleDestroy(this);
}

// ===========================================================================
// FUN_06d913f0 -- deep copy, `this` = the destination handle, argument = the source.
//
// Retail bails out at once when the SOURCE has no list (06d9141c), lazily builds the
// destination list when it has none (06d91422..06d91469), then for every source node
// allocates a fresh operator new(0x18) record, zeroes it, copies the six scalar fields
// -- including the byte at +0x0f -- and, when HardpointSize is non-zero, allocates
// operator new(HardpointSize) and copies the name up to its NUL.
// ===========================================================================
void RpcEquipHandleCopy(FRpcEquipWireHandle* Dest, const FRpcEquipWireHandle* Source) {
    if (Source->List == nullptr) {                                        // 06d9141c
        return;
    }
    if (Dest->List == nullptr && RpcEquipHandleBuildList(Dest) == nullptr) {
        return;                                                           // 06d91469
    }
    // 06d9146f..06d9147c: first node, and the loop bound re-reads the source's dummy
    // every round (06d91548..06d91557).
    for (const FRpcEquipWireNode* Node = Source->List->Head->Next; Node != Source->List->Head;
         Node = Node->Next) {
        const FRpcEquipWireRecord* From = Node->Value;                    // 06d91488
        FRpcEquipWireRecord* To =
            static_cast<FRpcEquipWireRecord*>(::operator new(sizeof(FRpcEquipWireRecord),
                                                             std::nothrow)); // 06d9148d
        if (To == nullptr) {
            // 06d91497 `CMP ESI,EBP ; JZ LAB_06d91548` -- the new-expression's own test:
            // retail skips this node and keeps walking.
            continue;
        }
        RpcContainerInitRecord(To);
        To->HardpointSize = From->HardpointSize;                          // 06d914ba
        To->ArchId = From->ArchId;
        To->Status = From->Status;
        To->Count = From->Count;
        To->Id = From->Id;
        To->bEquipped = From->bEquipped;
        To->Field_0x0f = From->Field_0x0f;
        if (To->HardpointSize != 0) {                                     // 06d914e1 JBE
            To->Hardpoint = RpcContainerCopyString(From->Hardpoint, To->HardpointSize);
        }
        RpcEquipHandleLinkBack(Dest->List, To);                           // 06d9150d
    }
}

// ===========================================================================
// FUN_06d8fa90 -- `if (packet+0x20 == 0) { packet+0x20 = operator new(4); *it = 0; }
//                  if (packet+0x20) FUN_06d913f0(packet+0x20, Source);`
// ===========================================================================
// The payload's four bytes hold a POINTER to the handle, not the handle itself:
// retail allocates the handle with operator new(4) and stores its address at
// packet+0x20 (06d8fa9a..06d8fab2), then hands THAT address to FUN_06d913f0 as the
// destination. The receiver side reads the same dword back and passes it as the SOURCE
// (06d95d20). Both indirections are reproduced.
void RpcPacketEquipAssign(FPacketBase* Packet, const FRpcEquipWireHandle* Source) {
    FRpcEquipWireHandle** Slot = reinterpret_cast<FRpcEquipWireHandle**>(Packet->Payload());
    if (*Slot == nullptr) {                                               // 06d8fa96
        FRpcEquipWireHandle* Handle =
            static_cast<FRpcEquipWireHandle*>(::operator new(sizeof(FRpcEquipWireHandle),
                                                             std::nothrow));
        if (Handle != nullptr) {                                          // 06d8faa4
            Handle->List = nullptr;
        }
        *Slot = Handle;                                                   // 06d8fab2
    }
    if (*Slot != nullptr) {                                               // 06d8fab8
        RpcEquipHandleCopy(*Slot, Source);
    }
}

// ===========================================================================
// FUN_06d95d20 -- the inverse: `if (packet+0x20) FUN_06d913f0(Dest, packet+0x20)`.
// ===========================================================================
__declspec(dllexport) void RpcPacketEquipExtract(const FPacketBase* Packet, FRpcEquipWireHandle* Dest) {
    FRpcEquipWireHandle* const* Slot =
        reinterpret_cast<FRpcEquipWireHandle* const*>(Packet->Payload());
    if (*Slot != nullptr) {                                               // 06d95d23
        RpcEquipHandleCopy(Dest, *Slot);
    }
}

// ===========================================================================
// FUN_06d8c750 -- std::vector<EquipDesc> -> handle
//
// Retail walks `[param_1+0x4] .. [param_1+0x8]` with a stride of 0x20 and, for each
// element, either skips it (when param_3 is 0 and the element's bEquipped byte at
// +0x0c is non-zero -- 06d8c770) or builds one 0x18 record. The hardpoint is copied
// only when COMMON.DLL::EquipDesc::is_internal() returns false.
// ===========================================================================
void RpcEquipHandleFromVector(FRpcEquipWireHandle* Handle,
                              const EquipDescVector* Source,
                              bool bAll) {
    // 06d8c755..06d8c761: EBP = first, [ESP+..] = last, `CMP EBP,EAX ; JZ` -- the only
    // test before the loop.
    const EquipDesc* Item = Source->Begin;
    const EquipDesc* End = Source->End;

    for (; Item != End; ++Item) {
        if (!bAll && Item->bEquipped) {
            // retail 06d8c776: `CMP byte ptr [EBP+0xc],BL ; JNZ next` -- with param_3
            // clear only the UNequipped items travel, which is exactly the cargo half.
            continue;
        }
        // 06d8c781 operator new(0x18) is a new-expression: `CMP EAX,EBX ; JZ` skips only
        // the zeroing (06d8c78b), and the stores below then go through the null pointer
        // exactly as 06d8c7af does.
        FRpcEquipWireRecord* Record =
            static_cast<FRpcEquipWireRecord*>(::operator new(sizeof(FRpcEquipWireRecord),
                                                             std::nothrow));
        if (Record != nullptr) {
            RpcContainerInitRecord(Record);
        }
        Record->ArchId = Item->ArchId;
        Record->Count = Item->Count;
        Record->Status = Item->Health;
        Record->Id = Item->Id;
        Record->bEquipped = Item->bEquipped;

        // Retail gates the string copy on COMMON.DLL::EquipDesc::is_internal() called on
        // the element itself (06d8c7cd, imported by name at .rdata:06db30d8), which
        // compares the hardpoint against Common.dll's interned cargo-bay name.
        //
        // is_internal() is the ONLY gate (06d8c7d5 JNZ): a non-internal hardpoint travels
        // even when it is the empty string, as size 1 and a lone NUL. The size is stored
        // (06d8c7ea) before the operator new it sizes (06d8c7ef, MOVZX of the word).
        if (!Item->is_internal()) {
            Record->HardpointSize =
                (unsigned short)(RpcContainerStringLength(Item->Hardpoint) + 1u);
            Record->Hardpoint = RpcContainerCopyString(Item->Hardpoint, Record->HardpointSize);
        }
        RpcEquipHandlePushBack(Handle, Record);                           // 06d8c80f
    }
}

// ===========================================================================
// FUN_06d84d60 -- handle -> std::vector<EquipDesc>
// ===========================================================================
void RpcEquipDescVectorRelease(EquipDescVector* Vector) {
    // retail 06d88(0)0d / 06d84dca: `std::vector<EquipDesc>::_Destroy(first, last)`
    // then `operator delete(first)`. EquipDesc owns no heap (its hardpoint is an interned
    // StringAlloc string), so _Destroy runs trivial destructors; operator delete(nullptr)
    // is a no-op, as MSVCRT's is.
    for (EquipDesc* It = Vector->Begin; It != Vector->End; ++It) {
        It->~EquipDesc();
    }
    ::operator delete(Vector->Begin);
    Vector->Begin = nullptr;
    Vector->End = nullptr;
    Vector->Cap = nullptr;
}

__declspec(dllexport) void RpcEquipVectorFromHandle(EquipDescVector* Dest,
                              const FRpcEquipWireHandle* Source) {
    const FRpcEquipWireListHeader* Header = RpcEquipHandleGetList(Source);
    if (Header == nullptr) {
        // retail 06d84d75: a null list leaves the vector untouched.
        return;
    }

    // retail 06d84d95: reserve(list.Size) when the current capacity is smaller -- one
    // allocation of `Size << 5` bytes, i.e. Size * sizeof(EquipDesc), into which retail
    // first MOVES the elements it already had. Every caller of this function hands in a
    // freshly initialised, empty vector, so the port drops the move and just replaces
    // the block; on an empty vector the two are the same thing.
    const unsigned int Wanted = Header->Size;
    if (Wanted != 0) {
        RpcEquipDescVectorRelease(Dest);
        // 06d84da5: a bare operator new, no test.
        EquipDesc* Block = static_cast<EquipDesc*>(
            ::operator new(sizeof(EquipDesc) * Wanted, std::nothrow));
        Dest->Begin = Block;
        Dest->End = Block;
        Dest->Cap = Block + Wanted;
    }

    const FRpcEquipWireNode* Dummy = Header->Head;
    for (const FRpcEquipWireNode* Node = Dummy->Next; Node != Dummy; Node = Node->Next) {
        const FRpcEquipWireRecord* Record = Node->Value;
        if (Record == nullptr) {
            // retail 06d84e1a: `TEST EDI,EDI ; JZ next` -- a null record is skipped.
            continue;
        }
        // 06d84e26 EquipDesc::EquipDesc() on a local, 06d84e31 vector insert of one copy at
        // end, then the setters run on the new last element (ESI = end - 0x20, 06d84e3c):
        // set_arch_id(rec+0x00) 06d84e42, set_count(rec+0x04) 06d84e4e,
        // set_status(rec+0x08) 06d84e5a, set_id(word rec+0x0c) 06d84e69,
        // set_equipped(byte rec+0x0e) 06d84e77. The block above holds exactly
        // Header->Size slots and at most that many records are appended, so the insert
        // never reallocates; constructing in place is the same object.
        EquipDesc* Slot = new (Dest->End) EquipDesc();
        ++Dest->End;
        Slot->set_arch_id(Record->ArchId);
        Slot->set_count(Record->Count);
        Slot->set_status(Record->Status);
        Slot->set_id(Record->Id);
        Slot->set_equipped(Record->bEquipped);
        // 06d84e7d `CMP word [EDI+0x10],0 ; JBE`: set_hardpoint(StringAlloc(rec+0x14, 0))
        // (06d84e8a / 06d84e9e) when rec+0x10 != 0, else make_internal() (06d84ea8).
        // Interning is not cosmetic: EquipDesc::set_hardpoint just stores the pointer, so
        // handing it the RECORD's copy would leave the EquipDesc pointing into the
        // packet's owned block, which the packet destructor frees.
        if (Record->HardpointSize != 0) {
            CacheString Hp;
            Hp.Value = StringAlloc(Record->Hardpoint, false);
            Slot->set_hardpoint(Hp);
        } else {
            Slot->make_internal();
        }
    }
}

// ===========================================================================
// FUN_06d87ec0 / FUN_06d880a0 -- handle -> equipment list
//
// Retail builds its EquipDescList with COMMON.DLL::std::list<EquipDesc>::_Buynode:
// once with (0, 0) for the dummy node, then once per element with (dummy, dummy->Prev).
// The element is default-constructed (EquipDesc::EquipDesc) and blitted into the node
// with FUN_06d854b0, after which the five setters and the hardpoint decision run
// against the node's own copy, in this exact order:
//   set_arch_id, set_count, set_status, set_id, set_equipped,
//   then set_hardpoint(StringAlloc(rec+0x14, 0)) when rec+0x10 != 0, else make_internal.
//
// The port uses ITS compiler's container for the same job -- retail's node chain was
// its compiler's std::list too -- and keeps the wire side byte-exact. What it no longer
// does is hand IServer a MIRROR of EquipDescList: the element really is a Common.dll
// EquipDesc, built through the very setters retail imports by name, and it goes into a
// real EquipDescList. Appending is a raw tail insert (retail's _Buynode(dummy,
// dummy->Prev) at 06d87f56), NOT add_equipment_item -- retail does no combinable
// merging here and never touches NextId.
// ===========================================================================
__declspec(dllexport) void RpcEquipListFromHandle(EquipDescList& Dest, const FRpcEquipWireHandle* Source) {
    const FRpcEquipWireListHeader* Header = RpcEquipHandleGetList(Source);
    if (Header == nullptr) {
        // retail 06d87f24: a null list means an empty EquipDescList, not a failure.
        return;
    }

    const FRpcEquipWireNode* Dummy = Header->Head;
    for (const FRpcEquipWireNode* Node = Dummy->Next; Node != Dummy; Node = Node->Next) {
        // 06d87f37 `MOV ESI,[EBX+0x8]` -- the record is used with no test.
        const FRpcEquipWireRecord* Record = Node->Value;
        // retail 06d87f3e: `LEA ECX,[ESP+0x24] ; CALL EquipDesc::EquipDesc` -- one
        // default-constructed temp per element.
        EquipDesc Desc;
        Desc.set_arch_id(Record->ArchId);           // 06d87f88 set_arch_id(rec+0x00)
        Desc.set_count(Record->Count);              // 06d87f94 set_count  (rec+0x04)
        Desc.set_status(Record->Status);            // 06d87fa0 set_status (rec+0x08)
        Desc.set_id(Record->Id);                    // 06d87faf set_id     (rec+0x0c)
        Desc.set_equipped(Record->bEquipped);  // 06d87fbd set_equipped(rec+0x0e)
        // retail 06d87fc3 `CMP word ptr [ESI+0x10],0x0 ; JBE make_internal`, i.e. a
        // zero length is the internal case; otherwise StringAlloc(rec+0x14, 0) at
        // 06d87fd0 and set_hardpoint at 06d87fe4. Interning is not cosmetic:
        // set_hardpoint only stores the pointer, and the RECORD's copy dies with the
        // packet.
        if (Record->HardpointSize != 0) {
            CacheString Hardpoint;
            Hardpoint.Value = StringAlloc(Record->Hardpoint, false);
            Desc.set_hardpoint(Hardpoint);
        } else {
            Desc.make_internal();
        }
        Dest.Items.push_back(Desc);
    }
}

// ===========================================================================
// IServerImpl::ReqEquipment 0x06d8cff0..0x06d8d08a -- equipment list -> handle
//
// `this = param_1` (the EquipDescList), `EAX = [this+0x4]` is the dummy node and
// `ESI = [EAX]` the first real one, so every field offset below is NODE-relative and
// the EquipDesc itself starts at node+0x08:
//
//   06d8d01d  MOV EAX,[ESI+0x0c]                -> EquipDesc+0x04 ArchId    -> wire+0x00
//   06d8d023  MOV EAX,[ESI+0x1c]                -> EquipDesc+0x14 Count     -> wire+0x04
//   06d8d029  MOV EDX,[ESI+0x18]                -> EquipDesc+0x10 Health    -> wire+0x08
//   06d8d02f  MOV AX,word ptr [ESI+0x0a]        -> EquipDesc+0x02 Id        -> wire+0x0c
//   06d8d037  MOV AL,byte ptr [ESI+0x14]        -> EquipDesc+0x0c bEquipped -> wire+0x0e
//   06d8d03a  LEA ECX,[ESI+0x8] ; CALL EquipDesc::is_internal
//   06d8d048  JNZ skip                          -- internal equipment sends no hardpoint
//   06d8d04a..06d8d078  strlen via REPNE SCASB, +1 into wire+0x10, operator new that
//                       many bytes into wire+0x14, byte copy including the terminator
//
// Two fields retail deliberately leaves at the zero FUN_06d8d006..06d8d014 wrote:
// wire+0x0f (EquipDesc::bMission) and everything Owner-shaped. REQEQUIPMENT/REQCARGO do
// not carry them; only ch2 0x03 CHARACTERINFO does. Reproduced as found.
// ===========================================================================
void RpcEquipHandleFromList(FRpcEquipWireHandle* Handle, const EquipDescList& Source) {
    for (std::list<EquipDesc>::const_iterator It = Source.Items.begin();
         It != Source.Items.end(); ++It) {
        // 06d8cff2 operator new(0x18) is a new-expression: `CMP EAX,EBX ; JZ` (06d8cffa)
        // skips only the zeroing, and the stores below then go through the null pointer
        // exactly as 06d8d020 does.
        FRpcEquipWireRecord* Record =
            static_cast<FRpcEquipWireRecord*>(::operator new(sizeof(FRpcEquipWireRecord),
                                                             std::nothrow));
        if (Record != nullptr) {
            RpcContainerInitRecord(Record);
        }
        Record->ArchId = It->ArchId;
        Record->Count = It->Count;
        Record->Status = It->Health;
        Record->Id = It->Id;
        Record->bEquipped = It->bEquipped;

        // retail 06d8d040: the ONLY gate is EquipDesc::is_internal(). There is no
        // "length != 0" test anywhere in the sender, so an empty but non-internal
        // hardpoint really does travel as size 1 and a lone terminator. No null test on
        // the hardpoint either (06d8d052 REPNE SCASB): a non-internal Hardpoint always
        // points at an interned string. The size is written at 06d8d058, BEFORE the
        // operator new it sizes at 06d8d060 (MOVZX of that word).
        if (!It->is_internal()) {
            Record->HardpointSize =
                (unsigned short)(RpcContainerStringLength(It->Hardpoint) + 1u);
            Record->Hardpoint = RpcContainerCopyString(It->Hardpoint, Record->HardpointSize);
        }
        RpcEquipHandlePushBack(Handle, Record);                           // 06d8d07f
    }
}

// ===========================================================================
// Collision groups
// ===========================================================================

// FUN_06d8f7d0 -- `LEA EAX,[param_1 + 0x20]; RET`.
FRpcCollisionGroupList* RpcPacketCollisionGroups(FPacketBase* Packet) {
    return reinterpret_cast<FRpcCollisionGroupList*>(Packet->Payload());
}

__declspec(dllexport) const FRpcCollisionGroupList* RpcPacketCollisionGroups(const FPacketBase* Packet) {
    return reinterpret_cast<const FRpcCollisionGroupList*>(Packet->Payload());
}

// Stand-in for the concrete packet constructors (0x06d9e8b0 / 0x06da10b0) and the
// create-object record constructors, which build the list's dummy node. The packet
// classes build it themselves now (rpc_packet_registry.cpp); the records still come here. The dummy's
// operator new is the bare call of std::list's constructor -- no test.
void RpcCollisionGroupListInit(FRpcCollisionGroupList* List) {
    if (List->Head != nullptr) {
        return;   // already constructed
    }
    FRpcCollisionGroupNode* Dummy =
        static_cast<FRpcCollisionGroupNode*>(::operator new(sizeof(FRpcCollisionGroupNode),
                                                            std::nothrow));
    Dummy->Next = Dummy;
    Dummy->Prev = Dummy;
    Dummy->Field_0x00 = 0;   // the dummy's value is never read
    Dummy->Field_0x04 = 0;
    List->Allocator = 0;
    List->Pad_0x01[0] = 0;
    List->Pad_0x01[1] = 0;
    List->Pad_0x01[2] = 0;
    List->Head = Dummy;
    List->Size = 0;
}

// FUN_06d8f710 -- list assignment, in retail's three phases, from an array of 8-byte
// values. The phases live in RpcCollisionGroupListAssignRange (rpc_containers.h).
void RpcCollisionGroupListAssign(FRpcCollisionGroupList* List,
                                 const FRpcCollisionGroupDescMirror* Items,
                                 unsigned int Count) {
    RpcCollisionGroupListInit(List);
    RpcCollisionGroupListAssignRange(List, Items, Items + Count);
}

// ===========================================================================
// The CREATELOOT list (ch2 0x28)
// ===========================================================================

// retail FUN_06d931b0 -- `LEA EAX,[this + 0x28]; RET`, i.e. payload+0x08.
FRpcLootWireList* RpcPacketLootList(FPacketBase* Packet) {
    return reinterpret_cast<FRpcLootWireList*>(Packet->Payload() + 8);
}

__declspec(dllexport) const FRpcLootWireList* RpcPacketLootList(const FPacketBase* Packet) {
    return reinterpret_cast<const FRpcLootWireList*>(Packet->Payload() + 8);
}

// Stand-in for the CREATELOOT packet constructor's list constructor; bare operator new.
void RpcPacketLootListInit(FPacketBase* Packet) {
    FRpcLootWireList* List = RpcPacketLootList(Packet);
    if (List->Head != nullptr) {
        return;   // already constructed
    }
    FRpcLootWireNode* Dummy =
        static_cast<FRpcLootWireNode*>(::operator new(sizeof(FRpcLootWireNode),
                                                      std::nothrow));
    Dummy->Next = Dummy;
    Dummy->Prev = Dummy;
    unsigned char* Value = reinterpret_cast<unsigned char*>(&Dummy->Value);
    for (unsigned int i = 0; i < (unsigned int)sizeof(FRpcLootWireRecord); ++i) {
        Value[i] = 0;   // the dummy's value is never read
    }
    List->Allocator = 0;
    List->Pad_0x01[0] = 0;
    List->Pad_0x01[1] = 0;
    List->Pad_0x01[2] = 0;
    List->Head = Dummy;
    List->Size = 0;
}

// FUN_06d93160 -- push_back. Retail reads the dummy node out of packet+0x2c, allocates
// operator new(0x50) with no test (06d9316d), links it at the tail, copies the value with
// FUN_06d93280 and increments packet+0x30.
void RpcPacketLootPushBack(FPacketBase* Packet, const FRpcLootWireRecord& Record) {
    RpcPacketLootListInit(Packet);
    FRpcLootWireList* List = RpcPacketLootList(Packet);
    FRpcLootWireNode* Dummy = List->Head;
    FRpcLootWireNode* Tail = Dummy->Prev;
    FRpcLootWireNode* New =
        static_cast<FRpcLootWireNode*>(::operator new(sizeof(FRpcLootWireNode),
                                                      std::nothrow));
    New->Next = Dummy;
    if (Tail == nullptr) {
        // retail 06d93175: `TEST EBX,EBX ; JNZ ... ; MOV EBX,EAX`.
        Tail = New;
    }
    New->Prev = Tail;
    Dummy->Prev = New;
    New->Prev->Next = New;
    New->Value = Record;
    ++List->Size;
}

void RpcPacketLootListRelease(FPacketBase* Packet) {
    FRpcLootWireList* List = RpcPacketLootList(Packet);
    if (List->Head == nullptr) {
        return;   // never constructed
    }
    FRpcLootWireNode* Dummy = List->Head;
    FRpcLootWireNode* Node = Dummy->Next;
    while (Node != Dummy) {
        FRpcLootWireNode* Next = Node->Next;
        ::operator delete(Node);
        Node = Next;
    }
    ::operator delete(Dummy);
    List->Head = nullptr;
    List->Size = 0;
}

// ===========================================================================
// The reputation relation list (ch2 0x29, 0x2b)
// ===========================================================================

void RpcRelationListPushBack(FRpcRelationList* List, const FRpcRelation& Value) {
    // 06d88e81 `PUSH 0x8 ; CALL operator_new` -- the value lives on its own, and the
    // result is stored through with no test (06d88e9d).
    FRpcRelation* Block =
        static_cast<FRpcRelation*>(::operator new(sizeof(FRpcRelation), std::nothrow));
    Block->Field_0x00 = Value.Field_0x00;
    Block->Field_0x04 = Value.Field_0x04;
    RpcRelationListLink(List, Block);
}

void RpcRelationListLink(FRpcRelationList* List, FRpcRelation* Block) {
    // The dummy is read untested (FUN_06d8cad0 06d8cad4): every list this reaches was built
    // by its packet's constructor (06d9d30c..06d9d31a for 0x29, 06d9c59f for 0x2b).
    FRpcRelationNode* Dummy = List->Head;
    // FUN_06d8cad0: the node's operator new(0xc) at 06d8cade is bare as well.
    FRpcRelationNode* Tail = Dummy->Prev;
    FRpcRelationNode* New =
        static_cast<FRpcRelationNode*>(::operator new(sizeof(FRpcRelationNode),
                                                      std::nothrow));
    New->Next = Dummy;
    if (Tail == nullptr) {
        // FUN_06d8cad0 06d8cae6 `TEST EDI,EDI` / 06d8caea `JNZ` / 06d8caec `MOV EDI,EAX`:
        // `if (piVar2 == 0) piVar2 = piVar1;`
        Tail = New;
    }
    New->Prev = Tail;
    Dummy->Prev = New;
    New->Prev->Next = New;
    New->Value = Block;   // 06d8cafc: the placement test on &New->Value, never null
    ++List->Size;
}

// ===========================================================================
// The mission-objective record list (ch2 0x35)
// ===========================================================================

void RpcMissionObjectiveListPushBack(FRpcMissionObjectiveList* List,
                                     FRpcMissionObjectiveRecord* Record) {
    // FUN_06d8fc10 06d8fc16: `if (param_1 != 0)` -- a null record is silently dropped and
    // neither counter moves. Retail's own test.
    if (Record == nullptr) {
        return;
    }
    // The dummy is read untested: the packet constructor built it (06d9daad).
    FRpcMissionObjectiveNode* Dummy = List->Head;
    FRpcMissionObjectiveNode* Tail = Dummy->Prev;
    // 06d8fc26 operator new(0xc): a bare call, no test.
    FRpcMissionObjectiveNode* New =
        static_cast<FRpcMissionObjectiveNode*>(
            ::operator new(sizeof(FRpcMissionObjectiveNode), std::nothrow));
    New->Next = Dummy;
    if (Tail == nullptr) {
        Tail = New;   // 06d8fc2e..06d8fc34
    }
    New->Prev = Tail;
    Dummy->Prev = New;
    New->Prev->Next = New;
    New->Value = Record;   // 06d8fc44: the placement test on &New->Value, never null
    ++List->Size;
}

__declspec(dllexport) const FRpcMissionObjectiveRecord* RpcMissionObjectiveListAt(
    const FRpcMissionObjectiveList* List, unsigned int Index) {
    // 06d8fc60 `MOV ECX,[ECX+0x40] ; MOV EAX,[ECX]` -- the dummy is read untested.
    const FRpcMissionObjectiveNode* Dummy = List->Head;
    const FRpcMissionObjectiveNode* Node = Dummy->Next;
    unsigned int At = 0;
    while (Node != Dummy) {
        if (At == Index) {
            return Node->Value;
        }
        Node = Node->Next;
        ++At;
    }
    return nullptr;
}

// ===========================================================================
// The seam between the retail-shaped containers some payloads still keep (the equipment
// handle, the SETCOLLISIONGROUPS / REQCOLLISIONGROUPS collision image) and the Common.dll
// types the shared CEqObjCreateInfo (src/include/server_createinfo.h) really holds. See
// the declarations in rpc_containers.h.
// ===========================================================================

// One CollisionGroupDesc as retail's node copy moves it: two dwords (06d8c9c8 /
// 06d8c9ce), the first holding the ushort id and the struct's two padding bytes.
static void RpcCollisionGroupDescDecode(CollisionGroupDesc& Desc, unsigned int Word0,
                                        unsigned int Word1) {
    Desc.Id = static_cast<unsigned short>(Word0 & 0xffffu);
    Desc.Pad02 = static_cast<unsigned short>(Word0 >> 16);
    std::memcpy(&Desc.Health, &Word1, sizeof(Word1));
}

// 06d8505c..06d85124: overwrite the nodes already in Dest, erase the surplus, append
// the rest -- which is what std::list assignment is. The receiver's static has been
// cleared by clear() since the previous packet, so in practice only the append runs.
__declspec(dllexport) void RpcCollisionGroupListFromWire(std::list<CollisionGroupDesc>& Dest,
                                   const FRpcCollisionGroupList* Source) {
    const FRpcCollisionGroupNode* Dummy = Source->Head;
    const FRpcCollisionGroupNode* Node = Dummy->Next;
    std::list<CollisionGroupDesc>::iterator It = Dest.begin();
    while (It != Dest.end() && Node != Dummy) {
        RpcCollisionGroupDescDecode(*It, Node->Field_0x00, Node->Field_0x04);
        ++It;
        Node = Node->Next;
    }
    Dest.erase(It, Dest.end());
    while (Node != Dummy) {
        CollisionGroupDesc Desc;
        RpcCollisionGroupDescDecode(Desc, Node->Field_0x00, Node->Field_0x04);
        Dest.push_back(Desc);
        Node = Node->Next;
    }
}

// 06d8c8e2..06d8c99d, inlined in FUN_06d8c830: one operator new(0x18) wire record per
// EquipDesc of the record's EquipDescVector, with NO bEquipped filter (this is not
// FUN_06d8c750's cargo path). Per element: ArchId <- +0x04 (06d8c923), Count <- +0x14
// (06d8c929), Status <- +0x10 (06d8c92f FLD/FSTP), Id <- +0x02 (06d8c935), bEquipped <-
// +0x0c (06d8c93d); then, unless COMMON.DLL::EquipDesc::is_internal() (06d8c945), the
// hardpoint: strlen+1 into +0x10 (06d8c962) and an operator new'd byte copy into +0x14.
// Pushed with FUN_06d91580 (06d8c989).
static void RpcEquipHandleFromEquipDescVector(FRpcEquipWireHandle* Handle,
                                              const EquipDescVector& Source) {
    for (const EquipDesc* Item = Source.Begin; Item != Source.End; ++Item) {
        // 06d8c8fa operator new(0x18) is a new-expression: `CMP EAX,EBP ; JZ` (06d8c902)
        // skips only the zeroing, and the stores below go through the result either way.
        FRpcEquipWireRecord* Record =
            static_cast<FRpcEquipWireRecord*>(::operator new(sizeof(FRpcEquipWireRecord),
                                                             std::nothrow));
        if (Record != nullptr) {
            RpcContainerInitRecord(Record);             // 06d8c906..06d8c91d
        }
        Record->ArchId = Item->ArchId;
        Record->Count = Item->Count;
        Record->Status = Item->Health;
        Record->Id = Item->Id;
        Record->bEquipped = Item->bEquipped;
        // is_internal() is the only gate (06d8c94d); no null test on the hardpoint
        // (06d8c957 REPNE SCASB), and the size is stored (06d8c962) before the operator
        // new it sizes (06d8c967).
        if (!Item->is_internal()) {
            Record->HardpointSize =
                static_cast<unsigned short>(RpcContainerStringLength(Item->Hardpoint) + 1u);
            Record->Hardpoint = RpcContainerCopyString(Item->Hardpoint, Record->HardpointSize);
        }
        RpcEquipHandlePushBack(Handle, Record);                           // 06d8c989
    }
}

// Retail appends through COMMON.DLL::std::vector<EquipDesc>::insert(end, 1, value)
// (06d84fc8); VC6 grows that by doubling. The block comes from ::operator new because
// ~EquipDescVector (Common 0x06269110) releases it with ::operator delete.
static void RpcEquipDescVectorPushBack(EquipDescVector& Vector, const EquipDesc& Item) {
    if (Vector.End == Vector.Cap) {
        const unsigned int Count = static_cast<unsigned int>(Vector.End - Vector.Begin);
        const unsigned int NewCap = (Count == 0u) ? 1u : Count * 2u;
        // VC6 allocator<EquipDesc>::allocate: a bare operator new, no test.
        EquipDesc* Block = static_cast<EquipDesc*>(
            ::operator new(sizeof(EquipDesc) * NewCap, std::nothrow));
        for (unsigned int Index = 0; Index < Count; ++Index) {
            new (&Block[Index]) EquipDesc(Vector.Begin[Index]);
        }
        ::operator delete(Vector.Begin);
        Vector.Begin = Block;
        Vector.End = Block + Count;
        Vector.Cap = Block + NewCap;
    }
    new (Vector.End) EquipDesc(Item);
    ++Vector.End;
}

// 06d84f86..06d85056, inlined in FUN_06d84ed0. For every node of the handle's list
// (FUN_06d913e0 at 06d84f89; an absent list or an empty one skips the loop, 06d84f94 /
// 06d84fa5): a default EquipDesc (06d84fb8, EquipDesc::EquipDesc) is APPENDED to the
// record's vector (06d84fc8), and then the element just appended (`[EBX+0x78] - 0x20`,
// 06d84fce/06d84fd3) gets, in this order,
//   set_arch_id(rec+0x00)  06d84fd9      set_count(rec+0x04)    06d84fe5
//   set_id(rec+0x0c)       06d84ff4      set_equipped(rec+0x0e) 06d85002
//   set_status(rec+0x08)   06d8500e
// and then set_hardpoint(StringAlloc(rec+0x14, 0)) when the word at rec+0x10 is
// non-zero (06d85014 `CMP word ptr [EDI+0x10],0 ; JBE`), else make_internal() (06d8503f).
// Unlike FUN_06d84d60 there is no null-record test here: 06d84fb1 loads the record and
// 06d84fd1 dereferences it.
static void RpcEquipDescVectorAppendFromHandle(EquipDescVector& Dest,
                                               const FRpcEquipWireHandle* Source) {
    const FRpcEquipWireListHeader* Header = RpcEquipHandleGetList(Source);
    if (Header == nullptr) {
        return;   // 06d84f94
    }
    const FRpcEquipWireNode* Dummy = Header->Head;
    for (const FRpcEquipWireNode* Node = Dummy->Next; Node != Dummy; Node = Node->Next) {
        const FRpcEquipWireRecord* Record = Node->Value;
        EquipDesc Blank;
        RpcEquipDescVectorPushBack(Dest, Blank);
        EquipDesc& Slot = *(Dest.End - 1);
        Slot.set_arch_id(Record->ArchId);
        Slot.set_count(Record->Count);
        Slot.set_id(Record->Id);
        Slot.set_equipped(Record->bEquipped);
        Slot.set_status(Record->Status);
        if (Record->HardpointSize != 0) {
            CacheString Hardpoint;
            Hardpoint.Value = StringAlloc(Record->Hardpoint, false);
            Slot.set_hardpoint(Hardpoint);
        } else {
            Slot.make_internal();
        }
    }
}

// ===========================================================================
// The create-object header (ch2 0x04 CREATESHIP, 0x42 CREATESOLAR)
//
// FUN_06d8c830 and FUN_06d84ed0 on the shared CEqObjCreateInfo; the wire side is laid
// out in rpc_containers.h.
// ===========================================================================

// The abstract base's constructor part, inlined into both packet constructors in the
// same order: CREATESHIP 06d9cca2..06d9ccd2, CREATESOLAR 06d9e66b..06d9e6a7.
//   packet+0x20/+0x24/+0x28/+0x2c = 0             06d9cca2..06d9ccab
//   FUN_06d9cda0(packet+0x30): six dwords zeroed, LAST first (06d9cda4..06d9cdb3), i.e.
//     +0x44 (Ids), +0x40 (IdCount), +0x3c, +0x38, +0x34, +0x30 -- CREATESOLAR inlines the
//     same six stores at 06d9e67b..06d9e68a
//   packet+0x48/+0x4c/+0x50/+0x54 = 0             06d9ccb3..06d9ccc4
//   FUN_06d931c0(packet+0x58): the identity quaternion, 1.0f then three zeroes (06d931c4)
//   packet+0x68/+0x6c/+0x70 = 0                   06d9cccc..06d9ccd2
// Nothing is tested. The two Common.dll containers that follow are the derived
// FRpcCreateObjectWire's members and are built by their own constructors.
FRpcCreateObjectHead::FRpcCreateObjectHead() {
    Field_0x00 = 0;                                     // 06d9cca2
    Field_0x04 = 0;                                     // 06d9cca5
    Field_0x08 = 0;                                     // 06d9cca8
    VibeGroupId = 0;                                    // 06d9ccab
    Ids = nullptr;                                      // 06d9cda4 (+0x44)
    IdCount = 0;                                        // 06d9cda7 (+0x40)
    Field_0x1c = 0;                                     // 06d9cdaa (+0x3c)
    Field_0x18 = 0;                                     // 06d9cdad (+0x38)
    Field_0x14 = 0;                                     // 06d9cdb0 (+0x34)
    Field_0x10 = 0;                                     // 06d9cdb3 (+0x30)
    Field_0x28 = 0;                                     // 06d9ccb3
    Field_0x2c = 0.0f;                                  // 06d9ccbe (integer 0 == +0.0f)
    Field_0x30 = 0.0f;                                  // 06d9ccc1
    Field_0x34 = 0.0f;                                  // 06d9ccc4
    Field_0x38 = 1.0f;                                  // 06d931c4 MOV [EAX],0x3f800000
    Field_0x3c = 0.0f;                                  // FUN_06d931c0
    Field_0x40 = 0.0f;
    Field_0x44 = 0.0f;
    Field_0x48 = 0;                                     // 06d9cccc
    Field_0x4c = 0;                                     // 06d9cccf
    Equipment.List = nullptr;                           // 06d9ccd2
}

// The tail of the base part of FUN_06d97b90 (== FUN_06d9e740 from 06d9e81c): the handle at
// packet+0x70 through FUN_06d91340 (06d97c94 LEA ECX,[EBX+0x70] / 06d97ca2 CALL), then the
// id block at +0x44 through FUN_06d95b60's body inlined (06d97ca7..06d97cb1: TEST, JZ,
// operator delete, no nulling). The two containers above it in memory were destroyed
// first, by the derived FRpcCreateObjectWire's member destructors -- retail's
// 06d97bff (DamageList) and 06d97c54 (list) -- which is also the EH order: this
// destructor is states 2 and 1 of FuncInfo_06db70a4 (Unwind@06db2873 FUN_06d91340 on +0x70,
// Unwind@06db2868 FUN_06d95b60 on +0x30), run by the compiler if a container constructor
// throws after the head is built.
FRpcCreateObjectHead::~FRpcCreateObjectHead() {
    // The handle goes first, as at 06d97ca2; its own member destructor then finds it
    // null and does nothing.
    RpcEquipHandleDestroy(&Equipment);                  // 06d97ca2 / Unwind@06db2873
    if (Ids != nullptr) {                               // 06d97ca7 / 06d95b63
        ::operator delete(Ids);                         // 06d97cb1 / 06d95b68
    }
}

void RpcCreateObjectHeaderWrite(FRpcCreateObjectWire* Wire,
                                const CEqObjCreateInfo& Source) {
    Wire->Field_0x00 = Source.ObjId;                    // 06d8c836 / 06d8c840
    Wire->Field_0x04 = Source.ArchetypeNickname;        // 06d8c843 / 06d8c846
    Wire->Field_0x08 = Source.DockingBaseId;            // 06d8c849 / 06d8c84c
    Wire->VibeGroupId = Source.VibeGroupId;             // 06d8c84f / 06d8c852
    Wire->Field_0x10 = Source.CostumeHead;              // 06d8c867 / 06d8c86a
    Wire->Field_0x14 = Source.CostumeBody;              // 06d8c86d / 06d8c870
    Wire->Field_0x18 = Source.CostumeLeftHand;          // 06d8c873 / 06d8c876
    Wire->Field_0x1c = Source.CostumeRightHand;         // 06d8c879 / 06d8c87c

    // 06d8c87f..06d8c8b6: the accessory count, then operator new(count << 2) -- a bare
    // call, made unconditionally, a count of 0 included (06d8c88c) -- and the copy: a
    // signed `JLE` guard (06d8c8a0), then src+0x24 walked with no bound (06d8c8a5..
    // 06d8c8b6). A Costume holds at most eight accessories (Costume::accessory[8]), so a
    // CEqObjCreateInfo built from one never carries a larger count.
    const int Count = Source.CostumeAccessoryCount;
    Wire->IdCount = Count;
    unsigned int* Block = static_cast<unsigned int*>(
        ::operator new(static_cast<unsigned int>(Count) << 2, std::nothrow));
    Wire->Ids = Block;
    const unsigned int* Accessory = &Source.CostumeAccessory[0];
    for (int i = 0; i < Count; ++i) {
        Block[i] = Accessory[i];
    }

    Wire->Field_0x28 = Source.VoiceId;                  // 06d8c855 / 06d8c858
    Wire->Field_0x2c = Source.Position.x;               // 06d8c8b8 / 06d8c8bb
    Wire->Field_0x30 = Source.Position.y;               // 06d8c8be / 06d8c8c1
    Wire->Field_0x34 = Source.Position.z;               // 06d8c8c4 / 06d8c8c7
    Wire->Field_0x38 = Source.Orientation.w;            // 06d8c8ca / 06d8c8cd
    Wire->Field_0x3c = Source.Orientation.x;            // 06d8c8d0 / 06d8c8d3
    Wire->Field_0x40 = Source.Orientation.y;            // 06d8c8d6 / 06d8c8d9
    Wire->Field_0x44 = Source.Orientation.z;            // 06d8c8dc / 06d8c8df
    Wire->Field_0x48 = static_cast<unsigned int>(Source.HitPts);  // 06d8c85b / 06d8c85e
    Wire->Field_0x4c = Source.Field_0x6c;               // 06d8c861 / 06d8c864

    RpcEquipHandleFromEquipDescVector(&Wire->Equipment, Source.Equipment);
    // 06d8c9a1..06d8ca2c: std::list<CollisionGroupDesc>::operator= inlined -- overwrite the
    // nodes already there, erase the surplus, append the rest -- into the list the packet
    // constructor built (06d9cce2 / 06d9e6b7). The packet list is a real
    // std::list<CollisionGroupDesc> now, so this is that assignment.
    Wire->CollisionGroups = Source.CollisionGroups;
    // 06d8ca2c..06d8cac5, one DamageList assignment: the node chain, then +0x0c as a BYTE
    // and +0x10/+0x14/+0x18 (06d8caae..06d8cac5) -- DamageList::operator=, into the
    // DamageList the packet constructor built (06d9ccf3 / 06d9e6c8).
    Wire->Damage = Source.Damage;
}

__declspec(dllexport) void RpcCreateObjectHeaderRead(CEqObjCreateInfo& Dest,
                               const FRpcCreateObjectWire* Wire) {
    Dest.ObjId = Wire->Field_0x00;                      // 06d84edd / 06d84ee0
    Dest.ArchetypeNickname = Wire->Field_0x04;          // 06d84ee3 / 06d84ee6
    Dest.DockingBaseId = Wire->Field_0x08;              // 06d84ee9 / 06d84eec
    Dest.VibeGroupId = Wire->VibeGroupId;               // 06d84f34 / 06d84f37
    Dest.CostumeHead = Wire->Field_0x10;                // 06d84ef5 / 06d84ef8
    Dest.CostumeBody = Wire->Field_0x14;                // 06d84eef / 06d84ef2
    Dest.CostumeLeftHand = Wire->Field_0x18;            // 06d84f01 / 06d84f05
    Dest.CostumeRightHand = Wire->Field_0x1c;           // 06d84efb / 06d84efe

    // 06d84f08..06d84f32. Retail's loop condition is unsigned (`CMP EAX,EDX ; JC`) after
    // a signed `JLE` guard, so a negative count copies nothing. No bound on dest+0x24 and
    // no test on the id block: the count and the block were produced together by
    // RpcCreateObjectHeaderWrite (at most eight, see there).
    const int Count = Wire->IdCount;
    Dest.CostumeAccessoryCount = Count;
    if (Count > 0) {
        unsigned int* Accessory = &Dest.CostumeAccessory[0];
        for (unsigned int i = 0; i < static_cast<unsigned int>(Wire->IdCount); ++i) {
            Accessory[i] = Wire->Ids[i];
        }
    }

    Dest.VoiceId = Wire->Field_0x28;                    // 06d84f3a / 06d84f3d
    Dest.Position.x = Wire->Field_0x2c;                 // 06d84f52 / 06d84f55
    Dest.Position.y = Wire->Field_0x30;                 // 06d84f4f / 06d84f58
    Dest.Position.z = Wire->Field_0x34;                 // 06d84f4c / 06d84f5b
    Dest.Orientation.w = Wire->Field_0x38;              // 06d84f6e / 06d84f71
    Dest.Orientation.x = Wire->Field_0x3c;              // 06d84f74 / 06d84f77
    Dest.Orientation.y = Wire->Field_0x40;              // 06d84f7a / 06d84f7d
    Dest.Orientation.z = Wire->Field_0x44;              // 06d84f80 / 06d84f83
    Dest.HitPts = static_cast<int>(Wire->Field_0x48);   // 06d84f40 / 06d84f43
    Dest.Field_0x6c = Wire->Field_0x4c;                 // 06d84f46 / 06d84f49

    // 06d84f86..06d85056.
    RpcEquipDescVectorAppendFromHandle(Dest.Equipment, &Wire->Equipment);
    // 06d8505c..06d85124: the same list assignment in the other direction, into the
    // record's own list (built by its constructor, emptied by clear() since the last packet).
    Dest.CollisionGroups = Wire->CollisionGroups;
    // 06d85124..06d851c1, the mirror of the write side's 06d8ca2c..06d8cac5:
    // DamageList::operator=, the four tail members at 06d851a9..06d851c1.
    Dest.Damage = Wire->Damage;
}

// The receiver statics' constructor (06d83df0), per-packet clear (06d83ed0 / 06d83fa0)
// and destructor (06d843b0) used to be hand-rolled here as RpcCreateObjectCommonInit /
// ::Release over the private mirror. They are now the shared type's own inline members
// in src/include/server_createinfo.h, which is what retail compiled into this module.

// ===========================================================================
// ch2 0x03 CHARACTERINFO -- the record conversions and the wire list
//
// Sender 0x06d89890 / FUN_06d955c0, receiver IServerImpl::HandleCharacterInfo
// 0x06d84490. The layout derivation is in rpc_containers.h.
// ===========================================================================

// 06d89b30 "PUSH 0x18" and its array; 06d84782..06d847b7 back the other way.
// 06d89b30..06d89c23 (BlockA; BlockB is the same run at 06d89c25..06d89d12).
// The inline block is a Costume (server_characterset.h): four part ids, eight accessory
// ids, the accessory count.
static void RpcIdBlockToWire(FRpcIdArrayBlockWire** Out, const Costume& Source) {
    // 06d89b38 operator new(0x18); the new-expression's test (06d89b42) only skips the
    // six zero stores -- the pointer is stored and written through either way.
    FRpcIdArrayBlockWire* Block = static_cast<FRpcIdArrayBlockWire*>(
        ::operator new(sizeof(FRpcIdArrayBlockWire), std::nothrow));
    if (Block != nullptr) {
        Block->Ids = nullptr;
        Block->Count = 0;
        Block->Field_0x0c = 0;
        Block->Field_0x08 = 0;
        Block->Field_0x04 = 0;
        Block->Field_0x00 = 0;
    }
    *Out = Block;                                     // 06d89b5d
    Block->Field_0x04 = Source.PartIds[1];            // 06d89b73 <- src+0x278
    Block->Field_0x00 = Source.PartIds[0];            // 06d89b86 <- src+0x274
    Block->Field_0x08 = Source.PartIds[2];            // 06d89b98
    Block->Field_0x0c = Source.PartIds[3];            // 06d89bab
    Block->Count = static_cast<int>(Source.AccessoryCount);   // 06d89bbe <- src+0x2a4
    // 06d89bce `CMP EAX,EBX ; JBE` and 06d89c21 `JC`: the count is UNSIGNED and the copy
    // is not bounded by the eight-slot array -- it walks on from src+0x284.
    if ((unsigned int)Block->Count != 0) {
        Block->Ids = static_cast<unsigned int*>(
            ::operator new((unsigned int)Block->Count << 2, std::nothrow));   // 06d89bda
        const unsigned int* SourceIds = &Source.AccessoryIds[0];
        for (unsigned int i = 0; i < (unsigned int)Block->Count; ++i) {
            Block->Ids[i] = SourceIds[i];             // 06d89c0d
        }
    }
}

static void RpcIdBlockFromWire(Costume& Dest, const FRpcIdArrayBlockWire* Block) {
    // FUN_06d84bf0, the record constructor's block init (06d845f8 / 06d84603): +0x00,
    // +0x04, +0x08, +0x0c and the count at +0x30. The eight ids are left alone there;
    // the port zeroes them too (heap garbage in retail, see the uninitialised-memory rule).
    Dest.PartIds[0] = 0;
    Dest.PartIds[1] = 0;
    Dest.PartIds[2] = 0;
    Dest.PartIds[3] = 0;
    Dest.AccessoryCount = 0;
    for (unsigned int i = 0; i < 8u; ++i) {
        Dest.AccessoryIds[i] = 0;
    }
    if (Block == nullptr) {
        // retail 06d8473c / 06d847c1: a null block pointer skips the whole group.
        return;
    }
    Dest.PartIds[1] = Block->Field_0x04;              // 06d84741
    Dest.PartIds[0] = Block->Field_0x00;              // 06d8474f
    Dest.PartIds[3] = Block->Field_0x0c;              // 06d8475e
    Dest.PartIds[2] = Block->Field_0x08;              // 06d8476d
    Dest.AccessoryCount = static_cast<unsigned int>(Block->Count);   // 06d8477c
    // 06d8478b `JBE` / 06d847b5 `JC`: unsigned count, no bound against the eight slots,
    // and the wire array is not tested -- exactly the sender's loop mirrored.
    unsigned int* DestIds = &Dest.AccessoryIds[0];
    for (unsigned int i = 0; i < (unsigned int)Block->Count; ++i) {
        DestIds[i] = Block->Ids[i];                   // 06d847a3
    }
}

// FUN_06d95b60 -- the wire id block's destructor, whole:
//   06d95b60 MOV EAX,[ECX+0x14] / 06d95b63 TEST EAX,EAX / 06d95b65 JZ -> RET
//   06d95b67 PUSH EAX / 06d95b68 CALL FUN_06db1dc2 (free) / RET
// Called directly by the CHARACTERINFO packet destructor FUN_06d93350 (06d933b3, 06d933cd)
// and reached by three EH unwind funclets (06db280e, 06db286e, 06db290e) on an id block
// embedded at +0x30 of a half-built object; the port's constructors do not throw, so only
// the two direct calls exist here.
static void RpcIdBlockWireDestroy(FRpcIdArrayBlockWire* Block) {
    if (Block->Ids != nullptr) {
        ::operator delete(Block->Ids);
    }
}

// FUN_06d84c10 -- the scalar deleting destructor: FUN_06d95b60's body, then the block.
static void RpcIdBlockWireFree(FRpcIdArrayBlockWire* Block) {
    if (Block == nullptr) {
        return;
    }
    RpcIdBlockWireDestroy(Block);
    ::operator delete(Block);
}

void RpcCharacterInfoRecordToWire(FRpcCharacterInfoWireRecord* Wire,
                                  const CCharacterSet::CharacterInfo* Source) {
    // Retail's record constructor (06d89944..06d899d9) puts every owning field in a
    // releasable state before a single value is copied; the port does the same, because
    // the packet destructor can run after a half-built record.
    Wire->BlockA = nullptr;
    Wire->BlockB = nullptr;
    Wire->Equipment.List = nullptr;
    Wire->CollisionGroups.Allocator = 0;
    Wire->CollisionGroups.Pad_0x01[0] = 0;
    Wire->CollisionGroups.Pad_0x01[1] = 0;
    Wire->CollisionGroups.Pad_0x01[2] = 0;
    Wire->CollisionGroups.Head = nullptr;
    Wire->CollisionGroups.Size = 0;
    // 06d899c1: the record constructor builds the std::list at +0x268 (COMMON.DLL's list
    // ctor, i.e. the dummy node); RpcCollisionGroupListInit stands in for it.
    RpcCollisionGroupListInit(&Wire->CollisionGroups);

    Wire->Name = Source->Id;                         // 06d899ef CHARACTER_ID::operator=
    Wire->Field_0x200 = Source->bIsRestart;          // 06d899f5
    Wire->Field_0x201 = Source->bIsAutoSave;         // 06d89a05
    Wire->TextA = Source->Name;                      // 06d89a2f basic_string::assign
    Wire->TextB = Source->Description;               // 06d89a50
    Wire->Field_0x224 = Source->DescripStrId;        // 06d89a56
    Wire->Field_0x228 = Source->TimestampLow;        // 06d89a66
    Wire->Field_0x22c = Source->TimestampHigh;       // 06d89a76
    Wire->Field_0x230 = Source->ShipArchetype;       // 06d89a86
    Wire->Field_0x234 = static_cast<unsigned int>(Source->Money);   // 06d89a96
    // The permuted nine. Each line is one MOV pair in the sender and its inverse in the
    // receiver; the addresses are on the struct in rpc_containers.h.
    Wire->Field_0x238 = Source->SystemId;            // 06d89aa6 <- src+0x260
    Wire->Field_0x244 = Source->VoiceId;             // 06d89ab6 <- src+0x26c
    Wire->Field_0x248 = Source->Rank;                // 06d89ac6 <- src+0x270
    Wire->Field_0x23c = Source->BaseId;              // 06d89ad6 <- src+0x264
    Wire->Field_0x240 = Source->LocationId;          // 06d89ae6 <- src+0x268
    // 06d89986: the record ctor's zero is the ONLY write retail ever makes to +0x24c.
    // It still travels -- see the note on the struct.
    Wire->Field_0x24c = 0;
    // src+0x244 is the float HullStatus; the sender moves its four bytes as a dword.
    std::memcpy(&Wire->Field_0x250, &Source->HullStatus, sizeof(Wire->Field_0x250));  // 06d89af6
    Wire->Field_0x254 = static_cast<unsigned int>(Source->NumKills);             // 06d89b06 <- src+0x238
    Wire->Field_0x258 = static_cast<unsigned int>(Source->NumMissionFailures);   // 06d89b16 <- src+0x240
    Wire->Field_0x25c = static_cast<unsigned int>(Source->NumMissionSuccesses);  // 06d89b26 <- src+0x23c
    // 06d899a4: hop one only zeroes it. The real value is written by the PACKET-BOUND
    // hop FUN_06d955c0 at 06d959c6..06d959d2, which this port folds in below, once the
    // equipment list exists.
    Wire->Field_0x260 = 0;

    RpcIdBlockToWire(&Wire->BlockA, Source->ComCostume);   // 06d89b30..06d89c23 <- src+0x274
    RpcIdBlockToWire(&Wire->BlockB, Source->BaseCostume);  // 06d89c25..06d89d12 <- src+0x2a8

    // 06d89d28..06d89dc1: one operator new(0x18) wire record per EquipDesc. Unlike
    // FUN_06d8c830 this loop has NO is_internal gate -- it always copies the hardpoint
    // (06d89d78 strlen, 06d89d87 operator new, 06d89d97 the byte loop).
    for (std::list<EquipDesc>::const_iterator It = Source->Equipment.Items.begin();
         It != Source->Equipment.Items.end(); ++It) {
        // 06d89d2a operator new(0x18); the test at 06d89d34 skips only the zero stores.
        FRpcEquipWireRecord* Record = static_cast<FRpcEquipWireRecord*>(
            ::operator new(sizeof(FRpcEquipWireRecord), std::nothrow));
        if (Record != nullptr) {
            RpcContainerInitRecord(Record);
        }
        Record->ArchId = It->ArchId;                          // 06d89d53
        Record->Count = It->Count;                            // 06d89d58
        Record->Status = It->Health;                          // 06d89d5e
        Record->Id = It->Id;                                  // 06d89d64
        Record->bEquipped = It->bEquipped;   // 06d89d6c
        // 06d89d72 "MOV DL,[EBP+0x20] ; MOV [EBX+0xf],DL". Node+0x20 is EquipDesc+0x18,
        // i.e. bMission -- and the receiver puts it straight back with 06d848fa/06d848fd
        // "MOV AL,[ESI+0xf] ; MOV [EDI+0x18],AL". That pair is what finally names
        // FRpcEquipWireRecord::Field_0x0f, which every other user of the record leaves
        // at zero.
        Record->Field_0x0f = (unsigned char)(It->bMission ? 1 : 0);
        const unsigned int Length = RpcContainerStringLength(It->Hardpoint);
        Record->Hardpoint = RpcContainerCopyString(It->Hardpoint, Length + 1u);
        Record->HardpointSize = (unsigned short)(Length + 1u);        // 06d89dad
        RpcEquipHandlePushBack(&Wire->Equipment, Record);             // 06d89dbc FUN_06d91580
    }

    // FUN_06d955c0 at 06d959c6..06d959da -- the packet-bound hop's own postscript, and
    // the only thing it does that the first hop does not:
    //     EAX = FUN_06d913e0(&dst+0x264)          ; *(header**)(rec+0x264)
    //     if (EAX) dst+0x260 = (byte)[EAX+0x8]    ; the list's Size, truncated
    //     else     dst+0x260 = 0
    // Folding the two hops into one, as this port does, dropped it -- and +0x260 is
    // exactly the count the wire codec emits and the decoder loops on (06d9528e /
    // 06d93fcf), so leaving it zero would have put every character's equipment list on
    // the wire as empty.
    {
        const FRpcEquipWireListHeader* Header = RpcEquipHandleGetList(&Wire->Equipment);
        Wire->Field_0x260 = (Header != nullptr) ? (unsigned char)(Header->Size & 0xffu) : 0;
    }

    // 06d89dd2..06d89e46: std::list<CollisionGroupDesc>::operator= of the caller's list
    // at src+0x254 onto rec+0x268 -- the self-assignment test (06d89de4), the overwrite
    // loop (06d89df8..06d89e0e), the range insert (06d89e18..06d89e3b) and the erase of
    // the surplus (06d89e46), each value moved as two raw dwords.
    RpcCollisionGroupListAssignRange(&Wire->CollisionGroups,
                                     Source->CollisionGroups.begin(),
                                     Source->CollisionGroups.end());
}

void RpcCharacterInfoRecordFromWire(CCharacterSet::CharacterInfo* Dest,
                                    const FRpcCharacterInfoWireRecord* Wire) {
    Dest->Id = Wire->Name;                           // 06d8462a
    // 06d84636 "TEST CL,CL ; SETNZ DL" and 06d84647: the receiver re-normalises both
    // flags to 0/1 instead of copying the byte back verbatim.
    Dest->bIsRestart = (unsigned char)(Wire->Field_0x200 != 0 ? 1 : 0);
    Dest->bIsAutoSave = (unsigned char)(Wire->Field_0x201 != 0 ? 1 : 0);
    Dest->Name = Wire->TextA;                        // 06d84669
    Dest->Description = Wire->TextB;                 // 06d84686
    Dest->DescripStrId = Wire->Field_0x224;          // 06d8468c
    Dest->TimestampLow = Wire->Field_0x228;          // 06d84698
    Dest->TimestampHigh = Wire->Field_0x22c;         // 06d846a4
    Dest->ShipArchetype = Wire->Field_0x230;         // 06d846b0
    Dest->Money = static_cast<int>(Wire->Field_0x234);                 // 06d846bc
    Dest->VoiceId = Wire->Field_0x244;               // 06d846c8 -> dst+0x26c
    Dest->Rank = Wire->Field_0x248;                  // 06d846d4 -> dst+0x270
    Dest->BaseId = Wire->Field_0x23c;                // 06d846e0 -> dst+0x264
    Dest->LocationId = Wire->Field_0x240;            // 06d846ec -> dst+0x268
    Dest->SystemId = Wire->Field_0x238;              // 06d846f8 -> dst+0x260
    std::memcpy(&Dest->HullStatus, &Wire->Field_0x250, sizeof(Dest->HullStatus));  // 06d84704 -> dst+0x244
    Dest->NumKills = static_cast<int>(Wire->Field_0x254);              // 06d84710 -> dst+0x238
    Dest->NumMissionFailures = static_cast<int>(Wire->Field_0x258);    // 06d8471c -> dst+0x240
    Dest->NumMissionSuccesses = static_cast<int>(Wire->Field_0x25c);   // 06d84728 -> dst+0x23c

    RpcIdBlockFromWire(Dest->ComCostume, Wire->BlockA);   // 06d84734..06d847b7 -> dst+0x274
    RpcIdBlockFromWire(Dest->BaseCostume, Wire->BlockB);  // 06d847b9..06d84837 -> dst+0x2a8

    // 06d84839..06d8492a: one EquipDesc per wire record, through the real setters. The
    // hardpoint is unconditional here -- there is no make_internal branch, unlike
    // FUN_06d84ed0 -- and bMission is written directly at EquipDesc+0x18 (06d848fd).
    Dest->Equipment.Items.clear();
    // 06d8483f FUN_06d913e0 and 06d8484a JZ: only the list HEADER is tested; the dummy
    // (06d84850) and each node's record (06d84860) are used as they are.
    const FRpcEquipWireListHeader* Header = RpcEquipHandleGetList(&Wire->Equipment);
    if (Header != nullptr) {
        for (const FRpcEquipWireNode* Node = Header->Head->Next; Node != Header->Head;
             Node = Node->Next) {
            const FRpcEquipWireRecord* Record = Node->Value;
            EquipDesc Item;                                   // EquipDesc::EquipDesc()
            Item.set_arch_id(Record->ArchId);                 // 06d848bf set_arch_id
            Item.set_count(Record->Count);                    // 06d848cb set_count
            Item.set_id(Record->Id);                          // 06d848da set_id
            Item.set_equipped(Record->bEquipped);        // 06d848e8 set_equipped
            Item.set_status(Record->Status);                  // 06d848f4 set_status
            Item.bMission = (Record->Field_0x0f != 0);        // 06d848fd, a direct store
            // 06d84900..06d8491a: `MOV ESI,[ESI+0x14] ; PUSH 0 ; PUSH ESI ; CALL
            // StringAlloc ; ... set_hardpoint`. Unlike the other two equipment receivers
            // this one has NO conditional and no make_internal branch -- it interns and
            // stores whatever the record holds. Interning is required here too, because
            // set_hardpoint only stores the pointer and the record's copy dies with the
            // packet.
            CacheString Hardpoint;
            Hardpoint.Value = StringAlloc(Record->Hardpoint, false);
            Item.set_hardpoint(Hardpoint);
            Dest->Equipment.Items.push_back(Item);
        }
    }

    // 06d84934..06d849a5: std::list operator= from rec+0x268 onto the fresh record's
    // +0x254 (06d84942 self test, overwrite loop, range insert, erase). The destination
    // is the record constructor's empty list, so it reduces to one append per node.
    Dest->CollisionGroups.clear();
    for (const FRpcCollisionGroupNode* Node = Wire->CollisionGroups.Head->Next;
         Node != Wire->CollisionGroups.Head; Node = Node->Next) {
        // The value moves as two raw dwords (06d8495d / 06d84963), pad included.
        CollisionGroupDesc Item;
        std::memcpy(&Item, &Node->Field_0x00, sizeof(Item));
        Dest->CollisionGroups.push_back(Item);
    }
}

// 06d94305..06d94341 (decoder) and 06d95ad4..06d95afe (FUN_06d955c0): bare
// operator new(0xc), `if (Prev == 0) Prev = New`, splice before the dummy, and the value
// stored through the never-null `&node->value` test. The dummy is the one the packet
// constructor built (06d9d52f..06d9d549, FPacket_FLPACKET_SERVER_CHARACTERINFO), so it is
// read without a test, as retail reads packet+0x28.
void RpcCharacterInfoWireListPushBack(FRpcCharacterInfoWireList* List,
                                      FRpcCharacterInfoWireRecord* Record) {
    FRpcCharacterInfoWireNode* Dummy = List->Head;
    FRpcCharacterInfoWireNode* Tail = Dummy->Prev;
    FRpcCharacterInfoWireNode* Node = static_cast<FRpcCharacterInfoWireNode*>(
        ::operator new(sizeof(FRpcCharacterInfoWireNode), std::nothrow));
    Node->Next = Dummy;
    if (Tail == nullptr) {
        Tail = Node;
    }
    Node->Prev = Tail;
    Dummy->Prev = Node;
    Node->Prev->Next = Node;
    Node->Value = Record;
    ++List->Size;
}

// FUN_06d93350, the CHARACTERINFO destructor's body, in retail's two passes.
//   Pass 1, 06d9337c..06d9346c: for every node whose value is non-null (06d93396), the
//   record's owned members in retail's own order, then the record; the nodes and their
//   value pointers are left alone.
//   Pass 2, 06d93472..06d934ab: every node through FUN_06d901f0 (unlink, free,
//   --Size), then the sentinel; Head = 0, Size = 0.
void RpcCharacterInfoWireListRelease(FRpcCharacterInfoWireList* List) {
    FRpcCharacterInfoWireNode* Node = List->Head->Next;              // 06d9337c / 06d9337f
    while (Node != List->Head) {                                     // 06d93381 / 06d93465
        FRpcCharacterInfoWireRecord* Record = Node->Value;           // 06d93393
        if (Record != nullptr) {                                     // 06d93396
            // 06d933a2..06d933be: FUN_06d95b60 (the id array at +0x14) and the block.
            if (Record->BlockA != nullptr) {
                RpcIdBlockWireDestroy(Record->BlockA);               // 06d933b3
                ::operator delete(Record->BlockA);                   // 06d933b9
            }
            // 06d933c1..06d933d8: the same for +0x278.
            if (Record->BlockB != nullptr) {
                RpcIdBlockWireDestroy(Record->BlockB);               // 06d933cd
                ::operator delete(Record->BlockB);                   // 06d933d3
            }
            // 06d933db..06d9341f: COMMON.DLL's list<CollisionGroupDesc>::erase(begin, end),
            // the sentinel freed, Head = 0, Size = 0. No test: the record's constructor
            // always built the sentinel.
            FRpcCollisionGroupList& Groups = Record->CollisionGroups;
            FRpcCollisionGroupNode* Group = Groups.Head->Next;
            while (Group != Groups.Head) {
                FRpcCollisionGroupNode* Erased = Group;
                Group = Group->Next;
                Erased->Prev->Next = Erased->Next;
                Erased->Next->Prev = Erased->Prev;
                ::operator delete(Erased);
                --Groups.Size;
            }
            ::operator delete(Groups.Head);                          // 06d9340e
            Groups.Head = nullptr;                                   // 06d9341c
            Groups.Size = 0;                                         // 06d9341f
            RpcEquipHandleDestroy(&Record->Equipment);               // 06d93427 FUN_06d91340
            // 06d93439 / 06d9344c: the two basic_string destructors, +0x214 then +0x204,
            // and 06d93453 the record -- the member destructors run in that same order.
            delete Record;
        }
        Node = Node->Next;                                           // 06d93463
    }
    FRpcCharacterInfoWireNode* Sentinel = List->Head;                // 06d93472
    FRpcCharacterInfoWireNode* Erase = Sentinel->Next;               // 06d93475
    while (Erase != Sentinel) {                                      // 06d93478 / 06d93494
        FRpcCharacterInfoWireNode* Erased = Erase;
        Erase = Erase->Next;                                         // 06d93485
        Erased->Prev->Next = Erased->Next;                           // FUN_06d901f0
        Erased->Next->Prev = Erased->Prev;
        ::operator delete(Erased);
        --List->Size;
    }
    ::operator delete(List->Head);                                   // 06d9349c
    List->Head = nullptr;                                            // 06d934a8
    List->Size = 0;                                                  // 06d934ab
}

// FUN_06d94460's id-block copy, 06d94692..06d9476e for +0x274 and 06d94770..06d9484e for
// +0x278: only when the source has a block; operator new(0x18) whose new-expression test
// (06d946aa) skips just the six zero stores; the five scalars in retail's order; then,
// for an UNSIGNED non-zero count (06d94724 JBE), a bare operator new(count * 4) and a
// dword copy whose bound re-reads the DESTINATION's count (06d94768 / 06d9476e JC).
static void RpcIdBlockWireCopy(FRpcIdArrayBlockWire** Out, const FRpcIdArrayBlockWire* Source) {
    if (Source == nullptr) {                                         // 06d94692 / 06d94770
        return;
    }
    FRpcIdArrayBlockWire* Block = static_cast<FRpcIdArrayBlockWire*>(
        ::operator new(sizeof(FRpcIdArrayBlockWire), std::nothrow));  // 06d946a0
    if (Block != nullptr) {
        Block->Ids = nullptr;
        Block->Count = 0;
        Block->Field_0x0c = 0;
        Block->Field_0x08 = 0;
        Block->Field_0x04 = 0;
        Block->Field_0x00 = 0;
    }
    *Out = Block;                                                    // 06d946c1
    Block->Field_0x04 = Source->Field_0x04;                          // 06d946d0
    Block->Field_0x00 = Source->Field_0x00;                          // 06d946e1
    Block->Field_0x0c = Source->Field_0x0c;                          // 06d946f2
    Block->Field_0x08 = Source->Field_0x08;                          // 06d94704
    Block->Count = Source->Count;                                    // 06d94716
    if ((unsigned int)Block->Count != 0) {                           // 06d94722 / 06d94724
        Block->Ids = static_cast<unsigned int*>(
            ::operator new((unsigned int)Block->Count << 2));        // 06d9472e
        for (unsigned int i = 0; i < (unsigned int)Block->Count; ++i) {
            Block->Ids[i] = Source->Ids[i];                          // 06d94759 / 06d9475f
        }
    }
}

// IServerImpl::HandleCharacterInfo 06d849fe..06d84a73. Same members, same order as the
// packet destructor's pass 1 (RpcCharacterInfoWireListRelease), but inline in the
// receiver's loop and with the id blocks going through FUN_06d84c10 (destroy + free).
void RpcCharacterInfoWireRecordFree(FRpcCharacterInfoWireRecord* Record) {
    if (Record->BlockA != nullptr) {                                 // 06d849fe..06d84a0b
        RpcIdBlockWireFree(Record->BlockA);                          // 06d84a0f FUN_06d84c10(1)
    }
    if (Record->BlockB != nullptr) {                                 // 06d84a14..06d84a1c
        RpcIdBlockWireFree(Record->BlockB);                          // 06d84a20 FUN_06d84c10(1)
    }
    // 06d84a30: COMMON.DLL's std::list<CollisionGroupDesc> destructor on +0x268 -- every
    // node, then the dummy (built by the record constructor, so never null).
    FRpcCollisionGroupList& Groups = Record->CollisionGroups;
    FRpcCollisionGroupNode* Group = Groups.Head->Next;
    while (Group != Groups.Head) {
        FRpcCollisionGroupNode* Erased = Group;
        Group = Group->Next;
        Erased->Prev->Next = Erased->Next;
        Erased->Next->Prev = Erased->Prev;
        ::operator delete(Erased);
        --Groups.Size;
    }
    ::operator delete(Groups.Head);
    Groups.Head = nullptr;
    Groups.Size = 0;
    RpcEquipHandleDestroy(&Record->Equipment);                       // 06d84a41 FUN_06d91340
    // 06d84a56 / 06d84a6c: the two basic_string _Tidy(1), +0x214 then +0x204, and 06d84a73
    // the record -- the member destructors run in that same order.
    delete Record;
}

// FUN_06d94460 (`this` = the packet, argument = the destination header; RET 4).
void RpcCharacterInfoWireListCopy(FRpcCharacterInfoWireList* Dest,
                                  const FRpcCharacterInfoWireList* Source) {
    // 06d94478..06d94487: the source dummy, read untested; an empty list returns at once.
    // The loop bound re-reads the source dummy every round (06d949ab).
    for (const FRpcCharacterInfoWireNode* Node = Source->Head->Next; Node != Source->Head;
         Node = Node->Next) {
        const FRpcCharacterInfoWireRecord* From = Node->Value;       // 06d944a0
        if (From == nullptr) {                                       // 06d944a3 JZ 06d949a9
            continue;
        }
        // 06d944ab operator new(0x27c); the new-expression's test (06d944be) skips only
        // the constructor (06d9457b `XOR ESI,ESI`) -- the copy below runs on the result
        // either way, as in the sender. The constructor (06d944ca..06d94573) zeroes the
        // first name byte, +0x200/+0x201, +0x224, +0x228/+0x22c, +0x244..+0x25c, +0x260,
        // the handle at +0x264 and both block pointers, builds the two strings and the
        // collision list's dummy (06d94555, COMMON list ctor); +0x230..+0x240 it leaves
        // alone. The value-initialised record and RpcCollisionGroupListInit give the same
        // state, and every one of those fields is overwritten below.
        FRpcCharacterInfoWireRecord* To = new (std::nothrow) FRpcCharacterInfoWireRecord();
        RpcCollisionGroupListInit(&To->CollisionGroups);             // 06d94555..06d9455e

        To->Name = From->Name;                                       // 06d94588 CHARACTER_ID::operator=
        To->Field_0x200 = From->Field_0x200;                         // 06d94594
        To->Field_0x201 = From->Field_0x201;                         // 06d945a0
        To->TextA = From->TextA;                                     // 06d945bc basic_string::assign
        To->TextB = From->TextB;                                     // 06d945d8
        To->Field_0x224 = From->Field_0x224;                         // 06d945e4
        To->Field_0x228 = From->Field_0x228;                         // 06d945f0
        To->Field_0x22c = From->Field_0x22c;                         // 06d945fc
        To->Field_0x230 = From->Field_0x230;                         // 06d94608
        To->Field_0x234 = From->Field_0x234;                         // 06d94614
        To->Field_0x238 = From->Field_0x238;                         // 06d94620
        To->Field_0x23c = From->Field_0x23c;                         // 06d9462c
        To->Field_0x240 = From->Field_0x240;                         // 06d94638
        To->Field_0x244 = From->Field_0x244;                         // 06d94644
        To->Field_0x248 = From->Field_0x248;                         // 06d94650
        To->Field_0x24c = From->Field_0x24c;                         // 06d9465c
        To->Field_0x250 = From->Field_0x250;                         // 06d94668
        To->Field_0x254 = From->Field_0x254;                         // 06d94674
        To->Field_0x258 = From->Field_0x258;                         // 06d94680
        To->Field_0x25c = From->Field_0x25c;                         // 06d9468c

        RpcIdBlockWireCopy(&To->BlockA, From->BlockA);               // 06d94692..06d9476e
        RpcIdBlockWireCopy(&To->BlockB, From->BlockB);               // 06d94770..06d9484e

        // 06d9485f FUN_06d913f0: the equipment handle, deep.
        RpcEquipHandleCopy(&To->Equipment, &From->Equipment);
        // 06d94864..06d9487a: +0x260 is DERIVED from the copy, not copied -- the low byte
        // of the new list's Size, or 0 without a list (same postscript as FUN_06d955c0).
        {
            const FRpcEquipWireListHeader* Header = RpcEquipHandleGetList(&To->Equipment);
            To->Field_0x260 = (Header != nullptr) ? (unsigned char)(Header->Size & 0xffu) : 0;
        }

        // 06d94880..06d94968: std::list<CollisionGroupDesc>::operator= inlined -- the
        // self-assignment test (06d9488c, never true here), the overwrite loop
        // (06d948b0..06d948cc), the erase of the surplus (06d948d4..06d94918) and the
        // appends (06d94928..06d94968), each value moved as two raw dwords.
        RpcCollisionGroupListAssignRange(&To->CollisionGroups,
                                         FRpcCollisionGroupNodeIterator(From->CollisionGroups.Head->Next),
                                         FRpcCollisionGroupNodeIterator(From->CollisionGroups.Head));

        // 06d9496a..06d9499e: bare operator new(0xc), linked before Dest's dummy, value =
        // the record, ++Size.
        RpcCharacterInfoWireListPushBack(Dest, To);
    }
}

// ===========================================================================
// ch2 0x03 CHARACTERINFO -- the wire codec, packet vtable slots +0x04 / +0x08.
//
// Retail LAB_06d949d0 (encode) and FUN_06d934d0 (decode). Both walk the payload field
// by field with the one idiom FRpcMarshalStream implements:
//     Cursor += Width; if (Cursor > Limit) Error = code; if (Error) bail; access; Ptr += Width;
// -- see 06d94a23..06d94a56 on the encode side and 06d9352d..06d93554 on the decode
// side. Error is sticky and the codes differ by direction, 0x16 out and 0x17 back.
//
// FIELD ORDER, which is NOT the struct's order and is the thing that has to be right.
// Every line below names the encoder address that emits it; the decoder's mirror runs
// in exactly the same order.
//
//   header    u16   FUN_06da1380 (06d94a08), cursor starts at 2
//   count     u8    (byte)Records.Size, read from packet+0x2c        06d94a28
//   per record, walking packet+0x28's dummy-node ring:
//     NameLen u16 + NameLen bytes of the CHARACTER_ID at rec+0x000   06d94a92 / 06d94afe
//     u8    rec+0x200                                                06d94b3c
//     u8    rec+0x201                                                06d94b65
//     u16   TextA length in CHARACTERS + 2*length bytes              06d94b73 / 06d94bd5
//     u16   TextB length in CHARACTERS + 2*length bytes              06d94bf0 / 06d94c61
//     u32   rec+0x224, +0x22c, +0x228  (note the 22c/228 SWAP)       06d94ca1..06d94cfb
//     u32   rec+0x230, +0x234, +0x238, +0x23c, +0x240, +0x244        06d94d28..06d94e09
//     u32   rec+0x248, +0x24c, +0x250, +0x254, +0x258, +0x25c        06d94e36..06d94f18
//     BlockA: u8 present; then A+0x04, A+0x00, A+0x08, A+0x0c,
//             A+0x10 = Count, then Count*4 bytes from A+0x14         06d94f26..06d9508d
//     BlockB: the same, from rec+0x278                               06d950c6..06d9522b
//     u8    rec+0x260 = the equipment element count                  06d9528e
//     per equipment record E (the whole list at *(rec+0x264)):
//       u32 E+0x04, float E+0x08, u32 E+0x00, u16 E+0x0c,
//       u8 E+0x0e, u8 E+0x0f, u16 E+0x10 = byte count,
//       then that many bytes from E+0x14                             06d952f3..06d9542a
//     u32   rec+0x270 = the collision list's Size                    06d9545c
//     per collision node N (the ring at rec+0x26c):
//       u16 (low half of N.Field_0x00), float N.Field_0x04           06d954c3 / 06d954ec
//   tail      u32   packet+0x20                                      06d95546
//   trailer         FUN_06da13c0 (06d9555c), a no-op
//
// The header is written with PutHeaderUnchecked, i.e. like FUN_06da1380 with no limit
// test (06da13a2). The name is memcpy'd into the 0x200-byte CHARACTER_ID with the full
// wire length and terminated at rec[NameLen], unbounded, as 06d936cb..06d936e0 do.
// ===========================================================================

// 06d94230..06d94272 -- the decoder's own splice into rec+0x26c, which is the ordinary
// insert-before-the-dummy with retail's `if (Prev == 0) Prev = the new node` quirk. The
// node is bare operator new(0x10) (06d9423f, no test); the value goes in through the
// never-null `&node->value` test at 06d94261. Returns the node: both decoders splice it
// in BEFORE reading the fields they then write into it.
static FRpcCollisionGroupNode* RpcCollisionGroupListPushBack(FRpcCollisionGroupList* List,
                                                             unsigned int Field_0x00,
                                                             unsigned int Field_0x04) {
    RpcCollisionGroupListInit(List);
    FRpcCollisionGroupNode* Dummy = List->Head;
    FRpcCollisionGroupNode* Tail = Dummy->Prev;
    FRpcCollisionGroupNode* New = static_cast<FRpcCollisionGroupNode*>(
        ::operator new(sizeof(FRpcCollisionGroupNode), std::nothrow));
    New->Next = Dummy;
    if (Tail == nullptr) {
        Tail = New;                                   // 06d9424f
    }
    New->Prev = Tail;
    Dummy->Prev = New;
    New->Prev->Next = New;
    New->Field_0x00 = Field_0x00;
    New->Field_0x04 = Field_0x04;
    ++List->Size;                                     // 06d94272 INC [EDI+0x270]
    return New;
}

// 06d94a92: `OR ECX,-1 ; XOR EAX,EAX ; REPNE SCASB ; NOT ECX ; DEC ECX`, i.e. strlen.
// Bounded here by the buffer retail is scanning, which is always NUL-terminated.
static unsigned int RpcCharacterIdLength(const CHARACTER_ID& Id) {
    unsigned int Length = 0;
    while (Length < (unsigned int)sizeof(Id.Buffer) && Id.Buffer[Length] != '\0') {
        ++Length;
    }
    return Length;
}

// The id block's five scalars and its array, in the encoder's order.
static void RpcEncodeIdBlock(FRpcMarshalStream& Stream, const FRpcIdArrayBlockWire* Block) {
    if (Block == nullptr) {
        Stream.PutByte(0);                            // 06d950bf / 06d95264
        return;
    }
    Stream.PutByte(1);                                // 06d94f51 / 06d950f1
    Stream.PutDword(Block->Field_0x04);               // 06d94f74
    Stream.PutDword(Block->Field_0x00);               // 06d94fa4
    Stream.PutDword(Block->Field_0x08);               // 06d94fd3
    Stream.PutDword(Block->Field_0x0c);               // 06d95003
    Stream.PutDword((unsigned int)Block->Count);      // 06d95033
    // 06d95047: `SHL ECX,0x2` -- Count DWORDS, copied as Count*4 bytes from +0x14. The
    // encoder has no zero test here, and a zero-length run costs nothing.
    Stream.PutBytes(Block->Ids, (unsigned int)Block->Count * 4u);
}

static bool RpcDecodeIdBlock(FRpcMarshalStream& Stream, FRpcIdArrayBlockWire** Out) {
    *Out = nullptr;
    unsigned char bPresent = 0;
    if (!Stream.GetByte(bPresent)) {                  // 06d93bd8
        return false;
    }
    if (bPresent == 0) {                              // 06d93bdf JZ
        return true;
    }
    // 06d93be5 operator new(0x18). The new-expression's test (06d93bf3) only skips the
    // six zero stores: a null block is stored at 06d93c0e and written through at 06d93c44,
    // exactly as retail does.
    FRpcIdArrayBlockWire* Block = static_cast<FRpcIdArrayBlockWire*>(
        ::operator new(sizeof(FRpcIdArrayBlockWire), std::nothrow));
    if (Block != nullptr) {
        Block->Ids = nullptr;
        Block->Count = 0;
        Block->Field_0x0c = 0;
        Block->Field_0x08 = 0;
        Block->Field_0x04 = 0;
        Block->Field_0x00 = 0;
    }
    *Out = Block;
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) { return false; }
    Block->Field_0x04 = Scratch;                      // 06d93c44
    if (!Stream.GetDword(Scratch)) { return false; }
    Block->Field_0x00 = Scratch;                      // 06d93c7a
    if (!Stream.GetDword(Scratch)) { return false; }
    Block->Field_0x08 = Scratch;                      // 06d93caf
    if (!Stream.GetDword(Scratch)) { return false; }
    Block->Field_0x0c = Scratch;                      // 06d93ce5
    if (!Stream.GetDword(Scratch)) { return false; }
    Block->Count = (int)Scratch;                      // 06d93d1e
    if ((unsigned int)Block->Count != 0) {            // 06d93d2c TEST / JBE: unsigned
        const unsigned int Bytes = (unsigned int)Block->Count << 2;
        // 06d93d3c: bare operator new, stored at once; the stream bound is checked only
        // AFTER the allocation (06d93d63), then the copy (06d93da0).
        Block->Ids = static_cast<unsigned int*>(::operator new(Bytes, std::nothrow));
        if (!Stream.GetBytes(Block->Ids, Bytes)) {
            return false;
        }
    }
    return true;
}

unsigned int RpcEncodeServerCharacterInfo(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerCharacterInfo* Wire =
        reinterpret_cast<const FPayloadServerCharacterInfo*>(Packet.Payload());

    // 06d949dc / 06d949e6: the capacity is arg2, the cursor is *arg1.
    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // FUN_06da1380

    // 06d94a28 `MOV CL,byte ptr [ESI+0x2c]` -- the LOW BYTE of the list's Size.
    const unsigned char Count = (unsigned char)(Wire->Records.Size & 0xffu);
    Stream.PutByte(Count);
    // 06d94a5c and 06d94a6d: an empty count OR an empty ring skips straight to the tail.
    if (Count != 0 && Wire->Records.Head != nullptr) {
        for (const FRpcCharacterInfoWireNode* Node = Wire->Records.Head->Next;
             Node != Wire->Records.Head && !Stream.Failed(); Node = Node->Next) {
            const FRpcCharacterInfoWireRecord* Record = Node->Value;
            if (Record == nullptr) {
                continue;                                          // 06d94a80 JZ
            }
            const unsigned int NameLen = RpcCharacterIdLength(Record->Name);
            Stream.PutWord((unsigned short)NameLen);                // 06d94abb
            Stream.PutBytes(Record->Name.Buffer, NameLen);          // 06d94afe
            Stream.PutByte(Record->Field_0x200);                    // 06d94b42
            Stream.PutByte(Record->Field_0x201);                    // 06d94b6b
            // 06d94b73 / 06d94ba8: the length is in CHARACTERS and the run is 2*that.
            const unsigned short LenA = (unsigned short)(Record->TextA.size() & 0xffffu);
            Stream.PutWord(LenA);
            Stream.PutBytes(LenA != 0 ? Record->TextA.data() : nullptr,
                            (unsigned int)LenA * 2u);
            const unsigned short LenB = (unsigned short)(Record->TextB.size() & 0xffffu);
            Stream.PutWord(LenB);                                   // 06d94c1b
            Stream.PutBytes(LenB != 0 ? Record->TextB.data() : nullptr,
                            (unsigned int)LenB * 2u);
            Stream.PutDword(Record->Field_0x224);                   // 06d94ca7
            Stream.PutDword(Record->Field_0x22c);                   // 06d94cd4
            Stream.PutDword(Record->Field_0x228);                   // 06d94d01
            Stream.PutDword(Record->Field_0x230);                   // 06d94d2e
            Stream.PutDword(Record->Field_0x234);                   // 06d94d5b
            Stream.PutDword(Record->Field_0x238);                   // 06d94d88
            Stream.PutDword(Record->Field_0x23c);                   // 06d94db5
            Stream.PutDword(Record->Field_0x240);                   // 06d94de2
            Stream.PutDword(Record->Field_0x244);                   // 06d94e0f
            Stream.PutDword(Record->Field_0x248);                   // 06d94e3c
            Stream.PutDword(Record->Field_0x24c);                   // 06d94e69
            Stream.PutDword(Record->Field_0x250);                   // 06d94e8d FLD/FSTP
            Stream.PutDword(Record->Field_0x254);                   // 06d94ec4
            Stream.PutDword(Record->Field_0x258);                   // 06d94ef1
            Stream.PutDword(Record->Field_0x25c);                   // 06d94f1e
            RpcEncodeIdBlock(Stream, Record->BlockA);
            RpcEncodeIdBlock(Stream, Record->BlockB);
            // 06d9528e: the stored count byte, then the WHOLE list. Retail trusts the
            // two to agree because FUN_06d955c0 derives the byte from the list's Size;
            // reproduced rather than recomputed, so a caller that desynchronises them
            // gets retail's behaviour and not a quieter one.
            Stream.PutByte(Record->Field_0x260);
            // 06d952a0 FUN_06d913e0 + 06d952ab JZ: the header is tested, the dummy
            // (06d952b1) and each node's record (06d952c8) are not.
            const FRpcEquipWireListHeader* Equip = RpcEquipHandleGetList(&Record->Equipment);
            if (Equip != nullptr) {
                for (const FRpcEquipWireNode* Item = Equip->Head->Next;
                     Item != Equip->Head && !Stream.Failed(); Item = Item->Next) {
                    const FRpcEquipWireRecord* E = Item->Value;
                    Stream.PutDword((unsigned int)E->Count);        // 06d952f6  E+0x04
                    Stream.PutFloat(E->Status);                     // 06d9531a  E+0x08
                    Stream.PutDword(E->ArchId);                     // 06d9534a  E+0x00
                    Stream.PutWord(E->Id);                          // 06d95375  E+0x0c
                    Stream.PutByte(E->bEquipped);                   // 06d9539e  E+0x0e
                    Stream.PutByte(E->Field_0x0f);                  // 06d953c4  E+0x0f
                    Stream.PutWord(E->HardpointSize);               // 06d953ed  E+0x10
                    // 06d953f6 `MOVZX ECX,word [EDX+0x10]` with NO shift -- a BYTE count,
                    // unlike the two strings above.
                    Stream.PutBytes(E->Hardpoint, E->HardpointSize);
                }
            }
            Stream.PutDword(Record->CollisionGroups.Size);          // 06d95486  rec+0x270
            // 06d9548e `MOV ESI,[ESI+0x26c]`: the dummy of the record ctor's list, used
            // untested.
            for (const FRpcCollisionGroupNode* Group = Record->CollisionGroups.Head->Next;
                 Group != Record->CollisionGroups.Head && !Stream.Failed();
                 Group = Group->Next) {
                // 06d954c3 `MOV AX,word ptr [ECX+0x8]` -- only the LOW HALF of the
                // first dword travels, which is why common_cobject.h's reading of
                // CollisionGroupDesc as { ushort Id; ushort Pad; float Health; } fits.
                Stream.PutWord((unsigned short)(Group->Field_0x00 & 0xffffu));
                Stream.PutDword(Group->Field_0x04);                 // 06d954ec FLD/FSTP
            }
        }
    }
    Stream.PutDword(Wire->Field_0x00);                              // 06d9554c packet+0x20
    // 06d9555c FUN_06da13c0 -- `XOR EAX,EAX ; RET 0xc`, nothing to emit.
    *OutLength = Stream.Length();                               // 06d9557d
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    if (!Stream.Failed()) {
        RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    }
    return Stream.GetError();
}

unsigned int RpcDecodeServerCharacterInfo(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength) {
    FPayloadServerCharacterInfo* Wire =
        reinterpret_cast<FPayloadServerCharacterInfo*>(Packet.Payload());

    // 06d934e8 / 06d93506: the buffer pointer is taken DIRECTLY, no dereference, and
    // 06d93517's FUN_06da13c0 reads no header.
    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);

    unsigned char Count = 0;
    if (!Stream.GetByte(Count)) {                                   // 06d93554
        return Stream.GetError();
    }
    for (unsigned int Index = 0; Index < (unsigned int)Count; ++Index) {
        // 06d93580 operator new(0x27c). 06d9365e `TEST ESI,ESI ; JZ LAB_06d94346`: a
        // failed allocation skips the record's whole body -- no bytes are read for it --
        // and the loop goes on with the next index.
        FRpcCharacterInfoWireRecord* Record = new (std::nothrow) FRpcCharacterInfoWireRecord();
        if (Record == nullptr) {
            continue;
        }
        // 06d9362c: the record constructor's std::list at +0x268 (the dummy node).
        RpcCollisionGroupListInit(&Record->CollisionGroups);

        // Every failed read below returns the error at once, as retail's jumps to
        // LAB_06d943ab / 06d943b0 / 06d943b5 / 06d943ba -> LAB_06d9440a do: the record is
        // not in the packet list yet and retail neither frees it nor reports a length
        // (the *OutLength store at 06d943c6 is on the success path only). The port leaks
        // it the same way.
        unsigned short Length = 0;
        unsigned int Scratch = 0;

        // 06d9368a..06d936e0: the length word, then memcpy of the FULL wire length into
        // the 0x200-byte CHARACTER_ID and `rec[len] = 0` -- no bound, like retail.
        if (!Stream.GetWord(Length)) {
            return Stream.GetError();
        }
        char* NameBytes = Record->Name.Buffer;
        if (!Stream.GetBytes(NameBytes, (unsigned int)Length)) {
            return Stream.GetError();
        }
        NameBytes[Length] = '\0';
        if (!Stream.GetByte(Record->Field_0x200)) { return Stream.GetError(); }   // 06d93710
        if (!Stream.GetByte(Record->Field_0x201)) { return Stream.GetError(); }   // 06d93739
        // 06d93761..06d937f5: the CHARACTER count, append(count, 0x20) through
        // FUN_06d94420, then 2*count bytes over it.
        if (!Stream.GetWord(Length)) {
            return Stream.GetError();
        }
        Record->TextA.append((size_t)Length, L' ');
        if (!Stream.GetBytes(Length != 0 ? &Record->TextA[0] : nullptr,
                             (unsigned int)Length * 2u)) {
            return Stream.GetError();
        }
        if (!Stream.GetWord(Length)) {                               // 06d9384a
            return Stream.GetError();
        }
        Record->TextB.append((size_t)Length, L' ');
        if (!Stream.GetBytes(Length != 0 ? &Record->TextB[0] : nullptr,
                             (unsigned int)Length * 2u)) {
            return Stream.GetError();
        }
        if (!Stream.GetDword(Record->Field_0x224)) { return Stream.GetError(); }  // 06d9393a
        if (!Stream.GetDword(Record->Field_0x22c)) { return Stream.GetError(); }  // 06d93967
        if (!Stream.GetDword(Record->Field_0x228)) { return Stream.GetError(); }  // 06d93994
        if (!Stream.GetDword(Record->Field_0x230)) { return Stream.GetError(); }  // 06d939c1
        if (!Stream.GetDword(Record->Field_0x234)) { return Stream.GetError(); }  // 06d939ee
        if (!Stream.GetDword(Record->Field_0x238)) { return Stream.GetError(); }  // 06d93a1b
        if (!Stream.GetDword(Record->Field_0x23c)) { return Stream.GetError(); }  // 06d93a48
        if (!Stream.GetDword(Record->Field_0x240)) { return Stream.GetError(); }  // 06d93a75
        if (!Stream.GetDword(Record->Field_0x244)) { return Stream.GetError(); }  // 06d93aa2
        if (!Stream.GetDword(Record->Field_0x248)) { return Stream.GetError(); }  // 06d93acf
        if (!Stream.GetDword(Record->Field_0x24c)) { return Stream.GetError(); }  // 06d93afc
        if (!Stream.GetDword(Record->Field_0x250)) { return Stream.GetError(); }  // 06d93b29
        if (!Stream.GetDword(Record->Field_0x254)) { return Stream.GetError(); }  // 06d93b56
        if (!Stream.GetDword(Record->Field_0x258)) { return Stream.GetError(); }  // 06d93b83
        if (!Stream.GetDword(Record->Field_0x25c)) { return Stream.GetError(); }  // 06d93bad
        if (!RpcDecodeIdBlock(Stream, &Record->BlockA)) {            // 06d93bd8..06d93daf
            return Stream.GetError();
        }
        if (!RpcDecodeIdBlock(Stream, &Record->BlockB)) {            // 06d93ddd..06d93fa8
            return Stream.GetError();
        }
        if (!Stream.GetByte(Record->Field_0x260)) {                  // 06d93fcf
            return Stream.GetError();
        }
        // 06d93fdd `TEST AL,AL ; JBE` then a do/while over the byte just stored.
        for (unsigned int i = 0; i < (unsigned int)Record->Field_0x260; ++i) {
            // 06d93ff2 operator new(0x18); 06d94002 `TEST ESI,ESI ; JZ LAB_06d941ce`:
            // a failed allocation skips this element's reads and goes to the next index.
            FRpcEquipWireRecord* E = static_cast<FRpcEquipWireRecord*>(
                ::operator new(sizeof(FRpcEquipWireRecord), std::nothrow));
            if (E == nullptr) {
                continue;
            }
            RpcContainerInitRecord(E);                               // 06d9400a..06d94020
            if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }   // 06d94050 E+0x04
            E->Count = (int)Scratch;
            if (!Stream.GetFloat(E->Status)) { return Stream.GetError(); } // 06d9407c E+0x08
            if (!Stream.GetDword(E->ArchId)) { return Stream.GetError(); } // 06d940a8 E+0x00
            if (!Stream.GetWord(E->Id)) { return Stream.GetError(); }      // 06d940d4 E+0x0c
            if (!Stream.GetByte(E->bEquipped)) { return Stream.GetError(); }   // 06d940fd
            if (!Stream.GetByte(E->Field_0x0f)) { return Stream.GetError(); }  // 06d94125
            if (!Stream.GetWord(E->HardpointSize)) { return Stream.GetError(); }  // 06d9414f
            // 06d9415d: operator new(HardpointSize) UNCONDITIONALLY -- a zero length gives
            // a zero-byte block -- untested, and stored before the stream bound check
            // (06d94176); then the copy (06d941a9).
            E->Hardpoint = static_cast<char*>(
                ::operator new((unsigned int)E->HardpointSize, std::nothrow));
            if (!Stream.GetBytes(E->Hardpoint, E->HardpointSize)) {
                return Stream.GetError();
            }
            RpcEquipHandlePushBack(&Record->Equipment, E);           // 06d941c5 FUN_06d91580
        }
        if (!Stream.GetDword(Scratch)) {                             // 06d94212  rec+0x270
            return Stream.GetError();
        }
        // 06d94227 `TEST ECX,ECX ; JBE` -- the count leads, the ring follows.
        for (unsigned int i = 0; i < Scratch; ++i) {
            // 06d94230..06d94272: the node is spliced in FIRST, its value initialised
            // from the stack pair at [ESP+0x28] -- which holds the count just read
            // (06d94219) -- and [ESP+0x2c], stack residue that the dword read at 06d942eb
            // overwrites (0 here, per the uninitialised-memory rule). Then 06d942ae
            // overwrites only the low WORD of the first dword, so its high half keeps the
            // count's high half.
            FRpcCollisionGroupNode* Group =
                RpcCollisionGroupListPushBack(&Record->CollisionGroups, Scratch, 0u);
            unsigned short GroupId = 0;
            if (!Stream.GetWord(GroupId)) {                          // 06d942a8
                return Stream.GetError();
            }
            Group->Field_0x00 = (Group->Field_0x00 & 0xffff0000u) | (unsigned int)GroupId;
            if (!Stream.GetDword(Group->Field_0x04)) {               // 06d942eb
                return Stream.GetError();
            }
        }
        // 06d94305..06d94341: operator new(0xc), splice before the dummy, INC packet+0x2c.
        RpcCharacterInfoWireListPushBack(&Wire->Records, Record);
    }
    Stream.GetDword(Wire->Field_0x00);                              // 06d94384 packet+0x20
    // 06d94393 FUN_06da13c0 -- the trailer hook, a no-op on this side too.
    *OutLength = Stream.Length();                               // 06d943c6
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    if (!Stream.Failed()) {
        RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    }
    return Stream.GetError();
}

// ===========================================================================
// The hardpoint-name dictionary -- FUN_06da2820 (build), FUN_06da2980 (name -> code),
// FUN_06da2a80 (code -> name).
//
// Only the CREATESHIP/CREATESOLAR equipment codec uses it, and it is a pure name
// compressor: rather than ship "HpRunningLight07" as a length byte plus sixteen bytes,
// the encoder ships one byte.
//
// The dictionary is NOT read from a file and CANNOT grow at runtime. It is a fixed,
// null-terminated array of char* in rpclocal's own .data at 06dbb770..06dbb814, with the
// terminating null dword at 06dbb818; every entry is transcribed below in table order
// with the .rdata address Ghidra gives its string.
//
// FUN_06da2820 -- called once, from the serializer's construction at 06d95f07 -- walks
// that array and, per entry, inserts name -> index into the std::map at .data:06dbbf64
// and leaves the count in .data:06dbbf78 (06da2942 `INC EDX` / 06da2959). Indices start
// at 1: 06da2856 seeds the counter with 1, and FUN_06da2a80 indexes DAT_06dbb76c[code]
// (06da2a94 `MOV EAX,[EAX*0x4 + DAT_06dbb76c]`) where 06dbb76c + 4*1 == 06dbb770, the
// first entry. Code 0 therefore means "not in the dictionary".
//
// The port does the same: RpcBuildHardpointDictionary is FUN_06da2820 and runs from
// FPacketSerializer's constructor, name -> code goes through the map (FUN_06da2980's
// find), and code -> name indexes the array bounded by the built count (FUN_06da2a80).
//
// The gap is retail's, not a transcription slip: 06dbb7b0 is HpRunningLight07 and
// 06dbb7b4 is HpRunningLight10 -- there is no 08 or 09.
// ===========================================================================
static const char* const GRpcHardpointNames[] = {
    nullptr,               // code 0 -- DAT_06dbb76c itself, never a valid hardpoint
    "HpCM01",              // 06dbb770 -> 06db60d8
    "HpCargo01",           // 06dbb774 -> 06db60cc
    "HpCargo02",           // 06dbb778 -> 06db60c0
    "HpContrail01",        // 06dbb77c -> 06db60b0
    "HpContrail02",        // 06dbb780 -> 06db60a0
    "HpDockLight01",       // 06dbb784 -> 06db6090
    "HpDockLight02",       // 06dbb788 -> 06db6080
    "HpDockLight03",       // 06dbb78c -> 06db6070
    "HpHeadlight",         // 06dbb790 -> 06db6064
    "HpMine01",            // 06dbb794 -> 06db6058
    "HpRunningLight01",    // 06dbb798 -> 06db6044
    "HpRunningLight02",    // 06dbb79c -> 06db6030
    "HpRunningLight03",    // 06dbb7a0 -> 06db601c
    "HpRunningLight04",    // 06dbb7a4 -> 06db6008
    "HpRunningLight05",    // 06dbb7a8 -> 06db5ff4
    "HpRunningLight06",    // 06dbb7ac -> 06db5fe0
    "HpRunningLight07",    // 06dbb7b0 -> 06db5fcc
    "HpRunningLight10",    // 06dbb7b4 -> 06db5fb8   (no 08 / 09 in retail)
    "HpRunningLight11",    // 06dbb7b8 -> 06db5fa4
    "HpRunningLight12",    // 06dbb7bc -> 06db5f90
    "HpRunningLight13",    // 06dbb7c0 -> 06db5f7c
    "HpShield01",          // 06dbb7c4 -> 06db5f70
    "HpThruster01",        // 06dbb7c8 -> 06db5f60
    "HpTurret01",          // 06dbb7cc -> 06db5f54
    "HpTurret02",          // 06dbb7d0 -> 06db5f48
    "HpTurret03",          // 06dbb7d4 -> 06db5f3c
    "HpTurret04",          // 06dbb7d8 -> 06db5f30
    "HpTurret05",          // 06dbb7dc -> 06db5f24
    "HpTurret06",          // 06dbb7e0 -> 06db5f18
    "HpTurret07",          // 06dbb7e4 -> 06db5f0c
    "HpTurret08",          // 06dbb7e8 -> 06db5f00
    "HpTurret09",          // 06dbb7ec -> 06db5ef4
    "HpTurret_U1_01",      // 06dbb7f0 -> 06db5ee4
    "HpTurret_U1_02",      // 06dbb7f4 -> 06db5ed4
    "HpTurret_U1_03",      // 06dbb7f8 -> 06db5ec4
    "HpTurret_U1_04",      // 06dbb7fc -> 06db5eb4
    "HpTurret_U1_05",      // 06dbb800 -> 06db5ea4
    "HpTurret_U3_01",      // 06dbb804 -> 06db5e94
    "HpWeapon01",          // 06dbb808 -> 06db5e88
    "HpWeapon02",          // 06dbb80c -> 06db5e7c
    "HpWeapon03",          // 06dbb810 -> 06db5e70
    "HpWeapon04",          // 06dbb814 -> 06db5e64
    nullptr                // 06dbb818, the terminator FUN_06da2820 stops on (06da2943)
};

// .data:06dbbf64, the std::map<std::string,int> FUN_06da2820 fills -- a CRT-initialised
// global, as in retail. Its builder runs from the serializer's constructor, which
// FUN_06d95e50 (RpcGetPacketSerializer) reaches only from IServerImpl::Startup, IClient
// slot 10 or DumpPacketStats, i.e. long after static initialisation.
static std::map<std::string, int> GRpcHardpointMap;

// .data:06dbbf78, the count FUN_06da2820 leaves: 42. Constant-initialised, so the
// builder's store survives whatever runs later.
static unsigned int GRpcHardpointCount = 0;

// FUN_06da2820, whole: count = 0 (06da2842); if the first entry is non-null (06da2848),
// then per entry `map[std::string(entry)] = code` (06da28e1 FUN_06da2b50, the store to
// node+0x1c at 06da28fe), code and count incremented together (06da2941 / 06da2942),
// until the next entry is null (06da2943 / 06da295f). Codes start at 1 (06da2856).
void RpcBuildHardpointDictionary() {
    GRpcHardpointCount = 0;
    int Code = 1;
    for (const char* const* Entry = &GRpcHardpointNames[1]; *Entry != nullptr; ++Entry) {
        GRpcHardpointMap[std::string(*Entry)] = Code;
        ++Code;
        ++GRpcHardpointCount;
    }
}

unsigned int RpcHardpointDictionaryCount() {
    return GRpcHardpointCount;
}

// FUN_06da2980. Retail builds a std::string from the argument (06da29b0 strlen,
// 06da29c7 assign) and runs the map's find -- lower_bound (06da29e0 FUN_06da3180) then
// `!(key < node)` (06da29fa) -- returning node+0x1c, the mapped int (06da2a69), or 0 on
// the end node (06da2a41 / 06da2a57).
unsigned int RpcHardpointNameToCode(const char* Name) {
    std::map<std::string, int>::const_iterator It = GRpcHardpointMap.find(std::string(Name));
    if (It == GRpcHardpointMap.end()) {
        return 0;
    }
    return static_cast<unsigned int>(It->second);
}

// FUN_06da2a80, whole: `if (!code) return 0; if (code > count) return 0;
// return table[code];`. The upper bound is the UNSIGNED `JBE` at 06da2a8f against the
// BUILT count at .data:06dbbf78, so the valid range is [1, count].
const char* RpcHardpointCodeToName(unsigned int Code) {
    if (Code == 0) {
        return nullptr;
    }
    if (Code > GRpcHardpointCount) {
        return nullptr;
    }
    return GRpcHardpointNames[Code];
}

// ===========================================================================
// ch2 0x04 CREATESHIP / 0x42 CREATESOLAR -- the shared 0x7c-byte header codec
//
// Encoder FUN_06da3720, decoder FUN_06da4020. Neither is a whole packet codec: both are
// called from the concrete type's Encode/Decode slot (06d97d20 / 06d98250 for CREATESHIP,
// 06dac7c0 / 06daca80 for CREATESOLAR) AFTER the routing header has been dealt with, and
// each type's own tail follows on return. Every byte below therefore belongs to
// FRpcCreateObjectWire and stops at Wire+0x6c: the DamageList's four tail members
// (Damage.Field_0x0c/0x10/0x14/0x18 == packet+0x8c..0x98) are NOT touched by either
// function -- no [EBP + 0x8c] or higher appears anywhere in either listing. Only the
// node chain and the Size at packet+0x88 travel.
//
// `MOV EBP,ECX` at 06da3725 / 06da4025 is never reassigned, so every [EBP+0xNN] is a
// packet field and Wire+0xNN == packet+0x20+0xNN.
//
// ARITY: all 22 return sites of the encoder and both of the decoder are `RET 0xc`, i.e.
// three pushed dwords on top of the `this` in ECX -- and not one of the three is read.
// The routines run entirely off the packet's own four marshalling dwords, which the slot
// has already filled, so the port takes the FRpcMarshalStream view (which IS those four
// dwords) and nothing else.
//
// RETURN: 0, or the direction's sticky error -- 0x16 encode (06da373d `MOV EDI,0x16`),
// 0x17 decode (06da403d `MOV EDI,0x17`).
// ===========================================================================

// The per-equipment-record flag byte, built at 06da3ba3..06da3c0d and taken apart at
// 06da4479..06da45f3. Every bit is a wire-width or presence switch.
static const unsigned char kEquipFlagEquipped      = 0x01; // 06da3bb0 -> 06da4481
static const unsigned char kEquipFlagMission       = 0x02; // 06da3bb9 -> 06da447b
static const unsigned char kEquipFlagWideCount     = 0x04; // 06da3bc8 -> 06da449b
static const unsigned char kEquipFlagWideId        = 0x08; // 06da3bd1 -> 06da4580
static const unsigned char kEquipFlagHardpointText = 0x10; // 06da3bf1 -> 06da45f3
static const unsigned char kEquipFlagNoHardpoint   = 0x20; // 06da3bdb -> 06da45e7
static const unsigned char kEquipFlagUnitStatus    = 0x40; // 06da3c0d -> 06da44f8
static const unsigned char kEquipFlagUnitCount     = 0x80; // 06da3bfa -> 06da4490

// 06da3ba0..06da3e1b -- one equipment record.
static void RpcEncodeCreateObjectEquip(FRpcMarshalStream& Stream,
                                       const FRpcEquipWireRecord* Record) {
    unsigned char Flags = 0;
    if (Record->bEquipped != 0) {                        // 06da3ba3 / 06da3bb0
        Flags |= kEquipFlagEquipped;
    }
    if (Record->Field_0x0f != 0) {                       // 06da3bb2 / 06da3bb9
        Flags |= kEquipFlagMission;
    }
    // 06da3bc4 `CMP ECX,EAX ; JLE` with EAX == 0xff -- a SIGNED compare on the int Count.
    if (Record->Count > 255) {
        Flags |= kEquipFlagWideCount;
    }
    // 06da3bcb `CMP word ptr [ESI+0xc],AX ; JBE` -- UNSIGNED, on the ushort Id.
    if (Record->Id > 255) {
        Flags |= kEquipFlagWideId;
    }
    // 06da3bd4..06da3bf1: no hardpoint at all, a dictionary hit, or a literal string.
    unsigned char HardpointCode = 0;
    if (Record->Hardpoint == nullptr) {
        Flags |= kEquipFlagNoHardpoint;
    } else {
        HardpointCode =
            static_cast<unsigned char>(RpcHardpointNameToCode(Record->Hardpoint) & 0xffu);
        if (HardpointCode == 0) {
            Flags |= kEquipFlagHardpointText;
        }
    }
    if (Record->Count == 1) {                            // 06da3bf4 `CMP dword,0x1`
        Flags |= kEquipFlagUnitCount;
    }
    // 06da3bfd `FLD [ESI+8] ; FCOMP DAT_06db3c58` with
    // .rdata:06db3c58 = 00 00 00 00 00 00 f0 3f == 1.0. TEST AH,0x44 + JP sets the bit
    // only on C3, i.e. only on equality, so an undamaged item ships no status byte.
    if (static_cast<double>(Record->Status) == 1.0) {
        Flags |= kEquipFlagUnitStatus;
    }

    Stream.PutByte(Flags);                               // 06da3c36
    if ((Flags & kEquipFlagUnitCount) == 0) {            // 06da3c3e JS
        if ((Flags & kEquipFlagWideCount) != 0) {
            Stream.PutDword(static_cast<unsigned int>(Record->Count));   // 06da3c69
        } else {
            Stream.PutByte(static_cast<unsigned char>(
                static_cast<unsigned int>(Record->Count) & 0xffu));      // 06da3c8d
        }
    }
    if ((Flags & kEquipFlagUnitStatus) == 0) {           // 06da3c96
        // 06da3c98 `FLD [ESI+8] ; FMUL DAT_06db60f0 (255.0) ; ftol ; MOV [ptr],AL`.
        Stream.PutQuantized(Record->Status, FRpcMarshalStream::kPercentScale);
    }
    // 06da3cd5 -- the archetype id narrows to a word exactly as the header's own did.
    Stream.PutWord(Archetype::LargeIDToSmallID(Record->ArchId));         // 06da3d07
    if ((Flags & kEquipFlagWideId) != 0) {               // 06da3d0d
        Stream.PutWord(Record->Id);                                      // 06da3d3a
    } else {
        Stream.PutByte(static_cast<unsigned char>(Record->Id & 0xffu));  // 06da3d5f
    }
    if ((Flags & kEquipFlagNoHardpoint) != 0) {          // 06da3d62 -> LAB_06da3e1e
        return;
    }
    if ((Flags & kEquipFlagHardpointText) != 0) {        // 06da3d6e
        // 06da3d79 takes the LOW BYTE of HardpointSize for the length but 06da3da1
        // MOVZXes the whole WORD for the run, so a name of 256 bytes or more would ship
        // a truncated length with a full-length body. Retail's own names top out at
        // fifteen characters, and the quirk is reproduced rather than corrected.
        Stream.PutByte(static_cast<unsigned char>(Record->HardpointSize & 0xffu));
        Stream.PutBytes(Record->Hardpoint, Record->HardpointSize);       // 06da3ddb
    } else {
        Stream.PutByte(HardpointCode);                                   // 06da3e18
    }
}

// 06da4421..06da4703 -- the same record, read back.
static bool RpcDecodeCreateObjectEquip(FRpcMarshalStream& Stream,
                                       FRpcEquipWireRecord* Record) {
    unsigned char Flags = 0;
    if (!Stream.GetByte(Flags)) {                                        // 06da4473
        return false;
    }
    // 06da4479..06da448d: both bits land as a plain 0/1, not as the mask.
    Record->Field_0x0f = static_cast<unsigned char>((Flags >> 1) & 0x01u);
    Record->bEquipped = static_cast<unsigned char>(Flags & 0x01u);

    unsigned int Scratch = 0;
    if ((Flags & kEquipFlagUnitCount) != 0) {                            // 06da4490 JNS
        Record->Count = 1;                                               // 06da4492
    } else if ((Flags & kEquipFlagWideCount) != 0) {                     // 06da449b
        if (!Stream.GetDword(Scratch)) {                                 // 06da44c3
            return false;
        }
        Record->Count = static_cast<int>(Scratch);
    } else {
        unsigned char Narrow = 0;
        if (!Stream.GetByte(Narrow)) {                                   // 06da44eb
            return false;
        }
        Record->Count = static_cast<int>(Narrow);                        // 06da44f1 zero-ext
    }

    if ((Flags & kEquipFlagUnitStatus) != 0) {                           // 06da44f8
        // 06da44fd `MOV dword ptr [EBX+0x8],0x3f800000` -- exactly 1.0f, which is the
        // same constant the encoder compared against.
        Record->Status = 1.0f;
    } else if (!Stream.GetQuantizedUnsigned(Record->Status,
                                            FRpcMarshalStream::kPercentInvScale)) {
        return false;                                                    // 06da4526..06da4540
    }

    unsigned short Small = 0;
    if (!Stream.GetWord(Small)) {                                        // 06da4569
        return false;
    }
    Record->ArchId = Archetype::SmallIDToLargeID(Small);                 // 06da4573 / 06da4583

    if ((Flags & kEquipFlagWideId) != 0) {                               // 06da4580
        if (!Stream.GetWord(Record->Id)) {                               // 06da45a9
            return false;
        }
    } else {
        unsigned char Narrow = 0;
        if (!Stream.GetByte(Narrow)) {                                   // 06da45d8
            return false;
        }
        Record->Id = static_cast<unsigned short>(Narrow);                // 06da45de zero-ext
    }

    if ((Flags & kEquipFlagNoHardpoint) != 0) {
        // 06da46fc: both fields cleared again, though operator new(0x18) had already
        // been zeroed at 06da4435..06da444d.
        Record->HardpointSize = 0;
        Record->Hardpoint = nullptr;
        return true;
    }
    unsigned char Raw = 0;
    if ((Flags & kEquipFlagHardpointText) != 0) {                        // 06da45f3
        if (!Stream.GetByte(Raw)) {                                      // 06da461c
            return false;
        }
        Record->HardpointSize = static_cast<unsigned short>(Raw);        // 06da4622 zero-ext
        if (Record->HardpointSize == 0) {                                // 06da4627 / 06da462e
            return true;
        }
        // 06da463a: bare operator new, stored at once (06da4643), bound checked after
        // (06da4653), then the copy.
        Record->Hardpoint = static_cast<char*>(
            ::operator new(Record->HardpointSize, std::nothrow));
        return Stream.GetBytes(Record->Hardpoint, Record->HardpointSize);// 06da4681
    }
    if (!Stream.GetByte(Raw)) {                                          // 06da46b2
        return false;
    }
    // 06da46bd FUN_06da2a80: an out-of-range code simply leaves the record hardpointless
    // (06da46c9 `CMP ESI,EDI ; JZ`), which is how retail survives a dictionary mismatch
    // between the two ends of the wire.
    const char* Name = RpcHardpointCodeToName(static_cast<unsigned int>(Raw));
    if (Name == nullptr) {
        return true;
    }
    // 06da46cd..06da46d8: `OR ECX,-1 ; REPNE SCASB ; NOT ECX` is strlen+1, stored WITH
    // the terminator, and 06da46f0 copies the terminator too.
    unsigned int Length = 0;
    while (Name[Length] != '\0') {
        ++Length;
    }
    ++Length;
    Record->HardpointSize = static_cast<unsigned short>(Length & 0xffffu);   // 06da46d8
    // 06da46dc MOVZX / 06da46e0: bare operator new of the stored WORD, then the byte
    // loop 06da46f0..06da46f8 up to and including the NUL.
    Record->Hardpoint = static_cast<char*>(
        ::operator new(static_cast<unsigned int>(Record->HardpointSize), std::nothrow));
    unsigned int Index = 0;
    do {
        Record->Hardpoint[Index] = Name[Index];
    } while (Name[Index++] != '\0');
    return true;
}

// 06da4764..06da4826 -- one collision-group node, appended before the dummy with
// retail's `if (Prev == 0) Prev = the new node` quirk, exactly as the CHARACTERINFO
// decoder does it. RpcCollisionGroupListPushBack above already is that splice.

unsigned int RpcEncodeCreateObjectCommon(FRpcMarshalStream& Stream,
                                         const FRpcCreateObjectWire* Wire) {
    Stream.PutDword(Wire->Field_0x00);                                   // 06da3760
    // 06da376c -- the archetype id goes out narrowed to 16 bits and comes back through
    // SmallIDToLargeID at 06da4091.
    Stream.PutWord(Archetype::LargeIDToSmallID(Wire->Field_0x04));       // 06da37a0
    Stream.PutDword(Wire->Field_0x08);                                   // 06da37d6
    Stream.PutDword(static_cast<unsigned int>(Wire->VibeGroupId));       // 06da380b
    // The swap is retail's: packet+0x34 (Wire+0x14) goes out BEFORE packet+0x30.
    Stream.PutDword(Wire->Field_0x14);                                   // 06da3840
    Stream.PutDword(Wire->Field_0x10);                                   // 06da3875
    // 06da3877 reads the count as a BYTE off the int field, before the bounds check.
    Stream.PutByte(static_cast<unsigned char>(
        static_cast<unsigned int>(Wire->IdCount) & 0xffu));              // 06da38a9
    // 06da38b8 `SHL EAX,0x2` -- IdCount DWORDS, checked and copied as IdCount*4 bytes.
    Stream.PutBytes(Wire->Ids, static_cast<unsigned int>(Wire->IdCount) * 4u);
    Stream.PutDword(Wire->Field_0x28);                                   // 06da3934
    // 06da393c checks 0xc at once and 06da398a hands FUN_06d8f510 the three dwords
    // Wire+0x2c/0x30/0x34 in memory order; FUN_06d8f510 is a plain three-dword copy
    // (06d8f514..06d8f521), not a quantizer.
    Stream.PutBytes(&Wire->Field_0x2c, 12u);
    // 06da398f..06da3a9a: four ftol(f * 127.0) bytes. Memory order is w,x,y,z but the
    // wire order is x,y,z,w -- Field_0x38 goes LAST.
    Stream.PutQuantized(Wire->Field_0x3c, FRpcMarshalStream::kOrientationScale);
    Stream.PutQuantized(Wire->Field_0x40, FRpcMarshalStream::kOrientationScale);
    Stream.PutQuantized(Wire->Field_0x44, FRpcMarshalStream::kOrientationScale);
    Stream.PutQuantized(Wire->Field_0x38, FRpcMarshalStream::kOrientationScale);
    {
        // 06da3a9c..06da3af4, the hull fraction:
        //   FILD  [EBP+0x68]            -- Field_0x48 read as a SIGNED int
        //   FSTP  float [local_14]      -- rounded to float before the divide
        //   FILD  GetArchMaxHitPts(Field_0x04)
        //   FDIVR float [local_14] ; FMUL 255.0 ; ftol ; store one byte
        const float HullPoints = static_cast<float>(static_cast<int>(Wire->Field_0x48));
        const int MaxHitPts = Archetype::GetArchMaxHitPts(Wire->Field_0x04);
        unsigned char Percent = 0;
        if (MaxHitPts != 0) {
            const double Scaled = (static_cast<double>(HullPoints) /
                                   static_cast<double>(MaxHitPts)) *
                                  FRpcMarshalStream::kPercentScale;
            Percent = static_cast<unsigned char>(
                static_cast<unsigned long>(static_cast<long>(Scaled)) & 0xffu);
        }
        // MaxHitPts == 0 cannot be reached through Common.dll (GetArchMaxHitPts defaults
        // to 1 on a miss). If it ever were, retail's FDIVR would produce an infinity and
        // its ftol would return 0x80000000, whose low byte is 0 -- which is the byte the
        // branch above emits. So this is retail's result without retail's undefined cast.
        Stream.PutByte(Percent);                                         // 06da3af4
    }
    if (SinglePlayer()) {                                                // 06da3afa / 06da3b02
        Stream.PutDword(Wire->Field_0x4c);                               // 06da3b32
    }
    // 06da3b3a FUN_06d913e0 -- the handle's list pointer, which is null until the first
    // push_back. A null list ships a zero COUNT and no records (LAB_06da3e3f), which is
    // not the same code path as a present-but-empty list.
    const FRpcEquipWireListHeader* Equip = RpcEquipHandleGetList(&Wire->Equipment);
    if (Equip == nullptr) {                                              // 06da3b48
        Stream.PutWord(static_cast<unsigned short>(0));                  // 06da3e6a
    } else {
        // 06da3b51 `MOV CX,word ptr [EAX+0x8]` -- the LOW WORD of the list's Size.
        Stream.PutWord(static_cast<unsigned short>(Equip->Size & 0xffffu));
        // 06da3b8a `MOV EAX,[EDX+0x4] ; MOV EDI,[EAX]`: the dummy is used untested, and
        // so is each node's record (06da3ba3 reads [ESI+0xe] straight away -- unlike
        // CHARACTERINFO's encoder, which tests it at 06d94a80). The marshaller builds all
        // three lists (RpcCreateObjectHeaderWrite), so every dummy exists here.
        for (const FRpcEquipWireNode* Node = Equip->Head->Next;
             Node != Equip->Head && !Stream.Failed(); Node = Node->Next) {
            RpcEncodeCreateObjectEquip(Stream, Node->Value);
        }
    }
    // 06da3e7b `MOV CL,byte ptr [EBP+0x7c]` -- the LOW BYTE of the list's _Size (packet
    // +0x7c); the collision-group count is ONE BYTE here, unlike CHARACTERINFO's dword at
    // 06d95486.
    Stream.PutByte(static_cast<unsigned char>(Wire->CollisionGroups.size() & 0xffu));
    // 06da3eb1 `MOV ESI,[EBX]`: begin() off the list the packet constructor built.
    for (std::list<CollisionGroupDesc>::const_iterator Group = Wire->CollisionGroups.begin();
         Group != Wire->CollisionGroups.end() && !Stream.Failed(); ++Group) {
        // 06da3ebd `MOV CL,byte ptr [ESI+0x8]` -- one byte of CollisionGroupDesc::Id,
        // where CHARACTERINFO sends a word (06d954c3).
        Stream.PutByte(static_cast<unsigned char>(Group->Id & 0xffu));
        // 06da3ee1 `FLD float [ESI+0xc] ; FMUL 255.0` -- CollisionGroupDesc::Health, a
        // FLOAT in [0,1]; CHARACTERINFO's raw dword at 06d954ec is the same field
        // unquantized.
        Stream.PutQuantized(Group->Health, FRpcMarshalStream::kPercentScale);
    }
    if (!SinglePlayer()) {                                               // 06da3f1c / 06da3f24
        return Stream.GetError();
    }
    // 06da3f64: the DamageList's list _Size (packet+0x88) as a whole dword.
    Stream.PutDword(static_cast<unsigned int>(Wire->Damage.Entries.size()));
    // 06da3f72 `MOV EAX,[ECX]`: begin() off the DamageList the constructor built.
    for (std::list<DamageEntry>::const_iterator Entry = Wire->Damage.Entries.begin();
         Entry != Wire->Damage.Entries.end() && !Stream.Failed(); ++Entry) {
        // 06da3f9d word SubObjId (value+0x00), 06da3fc6 dword Fate (value+0x08),
        // 06da3fee float HitPts (value+0x04) -- the middle field goes out LAST.
        Stream.PutWord(Entry->SubObjId);
        Stream.PutDword(static_cast<unsigned int>(Entry->Fate));
        Stream.PutFloat(Entry->HitPts);
    }
    return Stream.GetError();                                            // 06da400d XOR EAX,EAX
}

unsigned int RpcDecodeCreateObjectCommon(FRpcMarshalStream& Stream,
                                         FRpcCreateObjectWire* Wire) {
    if (!Stream.GetDword(Wire->Field_0x00)) {                            // 06da4057
        return Stream.GetError();
    }
    unsigned short Small = 0;
    if (!Stream.GetWord(Small)) {                                        // 06da4087
        return Stream.GetError();
    }
    Wire->Field_0x04 = Archetype::SmallIDToLargeID(Small);               // 06da4091 / 06da4097
    if (!Stream.GetDword(Wire->Field_0x08)) {                            // 06da40bf
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {                                     // 06da40ec
        return Stream.GetError();
    }
    Wire->VibeGroupId = static_cast<int>(Scratch);
    if (!Stream.GetDword(Wire->Field_0x14)) {                            // 06da4119
        return Stream.GetError();
    }
    if (!Stream.GetDword(Wire->Field_0x10)) {                            // 06da4146
        return Stream.GetError();
    }
    // 06da4151 / 06da4154: two fields the encoder never sends are cleared here, which is
    // the only thing in either direction that touches packet+0x38 / +0x3c.
    Wire->Field_0x18 = 0;
    Wire->Field_0x1c = 0;

    unsigned char Narrow = 0;
    if (!Stream.GetByte(Narrow)) {                                       // 06da4178
        return Stream.GetError();
    }
    Wire->IdCount = static_cast<int>(Narrow);                            // 06da417e zero-ext
    if (Wire->IdCount != 0) {                                            // 06da4187 JBE
        // 06da4191 bare operator new, stored unchecked at 06da4196.
        Wire->Ids = static_cast<unsigned int*>(
            ::operator new(static_cast<unsigned int>(Wire->IdCount) * 4u, std::nothrow));
        if (!Stream.GetBytes(Wire->Ids, static_cast<unsigned int>(Wire->IdCount) * 4u)) {
            return Stream.GetError();                                    // 06da41d6
        }
    }
    if (!Stream.GetDword(Wire->Field_0x28)) {                            // 06da4210
        return Stream.GetError();
    }
    // 06da421b checks 0xc and 06da4242 hands FUN_06d8f510 the stream cursor as the
    // SOURCE this time -- the arguments are the mirror of the encoder's.
    if (!Stream.GetBytes(&Wire->Field_0x2c, 12u)) {
        return Stream.GetError();
    }
    {
        // 06da426b..06da433c: retail reads all FOUR bytes first and only then runs the
        // conversions, so a short stream leaves none of the four fields written. Done the
        // same way here rather than field by field.
        unsigned char Raw[4] = { 0, 0, 0, 0 };
        for (unsigned int Index = 0; Index < 4u; ++Index) {
            if (!Stream.GetByte(Raw[Index])) {
                return Stream.GetError();
            }
        }
        // MOVSX at 06da42ed / 06da42f6 / 06da4307 / 06da430a, then FILD * (1/127).
        Wire->Field_0x3c = static_cast<float>(
            static_cast<double>(static_cast<signed char>(Raw[0])) *
            FRpcMarshalStream::kOrientationInvScale);                    // 06da430d
        Wire->Field_0x40 = static_cast<float>(
            static_cast<double>(static_cast<signed char>(Raw[1])) *
            FRpcMarshalStream::kOrientationInvScale);                    // 06da431e
        Wire->Field_0x44 = static_cast<float>(
            static_cast<double>(static_cast<signed char>(Raw[2])) *
            FRpcMarshalStream::kOrientationInvScale);                    // 06da432f
        Wire->Field_0x38 = static_cast<float>(
            static_cast<double>(static_cast<signed char>(Raw[3])) *
            FRpcMarshalStream::kOrientationInvScale);                    // 06da433c
    }
    {
        // 06da435d..06da4394, the hull fraction back to absolute hit points:
        //   byte -> ZERO-extended (06da4370 `XOR EDX,EDX ; MOV DL,BL`)
        //   FILD GetArchMaxHitPts(Field_0x04) ; FILD byte ; FMUL (1/255) ; FMULP ; ftol
        // Note the decoder multiplies by the reciprocal where the encoder divided, and
        // widens WITHOUT sign where the orientation quadruple above widened WITH it.
        unsigned char Percent = 0;
        if (!Stream.GetByte(Percent)) {
            return Stream.GetError();
        }
        const double MaxHitPts =
            static_cast<double>(Archetype::GetArchMaxHitPts(Wire->Field_0x04));
        const double Fraction = static_cast<double>(static_cast<unsigned int>(Percent)) *
                                FRpcMarshalStream::kPercentInvScale;
        Wire->Field_0x48 = static_cast<unsigned int>(
            static_cast<long>(MaxHitPts * Fraction));                    // 06da4394
    }
    if (SinglePlayer()) {                                                // 06da4397 / 06da439f
        if (!Stream.GetDword(Wire->Field_0x4c)) {                        // 06da43c8
            return Stream.GetError();
        }
    }

    unsigned short Count = 0;
    if (!Stream.GetWord(Count)) {                                        // 06da43fc
        return Stream.GetError();
    }
    // 06da4402 `TEST CX,CX ; JBE` -- TEST clears CF, so this is a plain zero test.
    for (unsigned int Index = 0; Index < static_cast<unsigned int>(Count); ++Index) {
        FRpcEquipWireRecord* Record = static_cast<FRpcEquipWireRecord*>(
            ::operator new(sizeof(FRpcEquipWireRecord), std::nothrow));  // 06da4421
        if (Record == nullptr) {
            // 06da442d `CMP EBX,EDI ; JZ LAB_06da4713` -- retail SKIPS the record and
            // keeps looping, leaving the stream pointing at a record body it never reads.
            continue;
        }
        RpcContainerInitRecord(Record);                                  // 06da4435..06da444d
        if (!RpcDecodeCreateObjectEquip(Stream, Record)) {
            // A stream error: retail returns straight out of LAB_06da4997 with the record
            // still in hand and not yet in the list -- leaked, and so it is here.
            return Stream.GetError();
        }
        RpcEquipHandlePushBack(&Wire->Equipment, Record);                // 06da4707
    }

    unsigned char GroupCount = 0;
    if (!Stream.GetByte(GroupCount)) {                                   // 06da474c
        return Stream.GetError();
    }
    for (unsigned int Index = 0; Index < static_cast<unsigned int>(GroupCount); ++Index) {
        // 06da4764..06da479c: push_back on the packet's std::list<CollisionGroupDesc> --
        // the node is spliced in and _Size bumped FIRST, its value copied from an
        // unwritten stack pair (FUN_06d85500 from local_14 -- residue in retail, 0 here
        // per the uninitialised-memory rule); the reads then fill it in place.
        CollisionGroupDesc Blank = {};
        Wire->CollisionGroups.push_back(Blank);
        CollisionGroupDesc& Group = Wire->CollisionGroups.back();
        unsigned char GroupId = 0;
        if (!Stream.GetByte(GroupId)) {                                  // 06da47c4
            return Stream.GetError();
        }
        // 06da47d5 writes only the WORD at node+0x8 (a zero-extended byte): the Id.
        Group.Id = static_cast<unsigned short>(GroupId);
        float Health = 0.0f;
        if (!Stream.GetQuantizedUnsigned(Health,
                                         FRpcMarshalStream::kPercentInvScale)) {
            return Stream.GetError();                                    // 06da47fe..06da4823
        }
        Group.Health = Health;                                           // 06da4823 FSTP
    }

    if (!SinglePlayer()) {                                               // 06da482c / 06da4834
        return Stream.GetError();
    }
    unsigned int DamageCount = 0;
    if (!Stream.GetDword(DamageCount)) {                                 // 06da4863
        return Stream.GetError();
    }
    // 06da4868 `CMP ECX,EBX ; JBE` with EBX zeroed at 06da4856 -- and it really is zeroed
    // there, even though the collision loop above left its own counter in EBX. A zero
    // count is simply the end of the packet.
    if (DamageCount == 0) {
        return Stream.GetError();
    }
    for (unsigned int Index = 0; Index < DamageCount; ++Index) {
        // 06da4877..06da4888: the value is default constructed to { SUBOBJ_ID_NONE (a WORD
        // through the data import at [06db3100]), 0xbf800000 == -1.0f, 0 } -- which is
        // DamageEntry's own constructor -- then 06da4894 `PUSH 0x14` / 06da489b bare
        // operator new, the splice before the dummy, and 06da48d1 `INC dword ptr
        // [EBP+0x88]`: the list's _Size, which the port's DamageList also keeps as Count.
        // The reads below then fill the node in place, so a short stream leaves the
        // defaults in it, as retail does.
        Wire->Damage.Entries.push_back(DamageEntry());
        ++Wire->Damage.Count;
        DamageEntry& Entry = Wire->Damage.Entries.back();
        if (!Stream.GetWord(Entry.SubObjId)) {                           // 06da48ff / 06da490b
            return Stream.GetError();
        }
        unsigned int Fate = 0;
        if (!Stream.GetDword(Fate)) {                                    // 06da4943
            return Stream.GetError();
        }
        Entry.Fate = static_cast<DamageEntry::SubObjFate>(Fate);
        // 06da4978: four raw bytes into value+0x04, the float HitPts.
        if (!Stream.GetFloat(Entry.HitPts)) {
            return Stream.GetError();
        }
    }
    return Stream.GetError();                                            // 06da498b XOR EAX,EAX
}

// ===========================================================================
// The four OUTER wire slots -- ch2 0x04 CREATESHIP and ch2 0x42 CREATESOLAR
//
//   0x06d97d20 CREATESHIP  encode   0x06d98250 CREATESHIP  decode
//   0x06dac7c0 CREATESOLAR encode   0x06daca80 CREATESOLAR decode
//
// Each is the packet vtable's own +0x04 / +0x08 entry. Shape, identical in all four:
//   1. fill packet+0x10..0x1c -- Cursor = 0, Limit = arg2, Ptr = the buffer, Error = 0
//      (06d97d31 / 06d9825f / 06dac7ce / 06daca8e, always `LEA reg,[this+0x10]`);
//   2. the routing-header hook -- FUN_06da1380 out (two bytes, cursor := 2),
//      FUN_06da13c0 back (a no-op, the dispatcher already ate them);
//   3. the shared 0x7c header, FUN_06da3720 / FUN_06da4020, all three stack arguments
//      passed straight through;
//   4. this type's own tail, below;
//   5. FUN_06da13c0 again as the trailer hook, in BOTH directions;
//   6. *arg3 = the cursor, then the per-type statistics record.
//
// CORRECTION, established here on the bytes: the DamageList's four tail members
// (FRpcCreateObjectWire::Damage.Field_0x0c..0x18 == packet+0x8c..0x98) do NOT travel and
// do NOT belong to these slots. Every packet offset the four listings mention is either
// the marshalling scratch (0x10..0x1c), the packet id (0x0c) or 0x9c and above -- the
// lowest payload byte any of them touches is payload+0x7c, exactly where the shared
// header ends. The shared codec stops below it too (its highest is packet+0x88, inside
// the damage list's own Size). So payload+0x6c..0x78 -- DamageList's bDestroyed, cause,
// inflictor id and inflictor owner -- exists only on the in-process FUN_06d8c830 /
// FUN_06d84ed0 path and is never serialized by anyone.
//
// ARGUMENT ASYMMETRY, re-proven on both pairs rather than inherited:
//   * 06d97d20 `MOV EAX,[ESP+0x4]` then 06d97d37 `MOV ECX,dword ptr [EAX]` -- and
//     06dac7c0 / 06dac7d4 are the same two instructions -- so encode's arg1 is a
//     pointer TO the buffer pointer;
//   * 06d98251 `MOV EAX,[ESP+0x8]` (after the `PUSH ECX` at 06d98250) then 06d9826b
//     `MOV [EBP+0x8],EAX`, and 06daca80 / 06daca9a likewise, with no load in between --
//     decode's arg1 IS the buffer.
//
// Retail's epilogue also updates a per-type statistics record --
// FUN_06da16b0(packet+0x0c) -> FUN_06da1730 on .data:DAT_06dbbf4c, then count/total/
// max/average at record+0x00/0x18/0x10/0x08 for the encode side and +0x04/0x1c/0x14/
// 0x0c for the decode side (06d98211..06d98245, 06d985c3..06d985fe, 06daca45..06daca71,
// 06dacbf1..06dacc28). That is RpcRecordEncodedPacket / RpcRecordDecodedPacket on
// GRpcPacketStats (rpc_transport.cpp), which every codec's success path calls.
// Note that the decode side's maximum really is
// `if (Cursor > Max + 2) Max = Cursor + 2` (06d985ea `ADD EAX,0x2` against the
// unmodified cursor in EBP at 06d985ed), which is NOT `if (Cursor + 2 > Max)`; both
// decoders emit it identically, so it is retail's and not a transcription slip.
// ===========================================================================

// Both create-object slots move packet+0xa0..0xa8 as ONE twelve-byte run (06d97dec
// checks 0xc in a single step), so the three members really must be adjacent with no
// padding between them. Checked, not assumed.
//
// The shared header is no longer retail's 0x7c bytes (its two containers are this
// toolchain's std::list and DamageList, FRpcCreateObjectWire), so each tail is pinned
// RELATIVE to its first field, which follows the header directly: retail offset minus
// 0x7c on the right-hand side.
static_assert(offsetof(FPayloadServerCreateShip, Field_0x7c) == sizeof(FRpcCreateObjectWire),
              "CREATESHIP's tail follows the shared header directly");
static_assert(offsetof(FPayloadServerCreateSolar, Field_0x7c) == sizeof(FRpcCreateObjectWire),
              "CREATESOLAR's tail follows the shared header directly");
static_assert(offsetof(FPayloadServerCreateShip, Field_0x80) -
                  offsetof(FPayloadServerCreateShip, Field_0x7c) == 0x80 - 0x7c,
              "the twelve-byte run starts at CREATESHIP payload+0x80");
static_assert(offsetof(FPayloadServerCreateShip, Field_0x84) -
                  offsetof(FPayloadServerCreateShip, Field_0x7c) == 0x84 - 0x7c,
              "FUN_06d8f510 moves packet+0xa0, +0xa4 and +0xa8 back to back");
static_assert(offsetof(FPayloadServerCreateShip, Field_0x88) -
                  offsetof(FPayloadServerCreateShip, Field_0x7c) == 0x88 - 0x7c,
              "FUN_06d8f510 moves packet+0xa0, +0xa4 and +0xa8 back to back");
static_assert(offsetof(FPayloadServerCreateShip, Field_0x98) -
                  offsetof(FPayloadServerCreateShip, Field_0x7c) == 0x98 - 0x7c,
              "the 127.0-quantized float is CREATESHIP payload+0x98");
static_assert(offsetof(FPayloadServerCreateShip, DisplayNameLength) -
                  offsetof(FPayloadServerCreateShip, Field_0x7c) == 0xb8 - 0x7c,
              "the wide display name is length at payload+0xb8, buffer at +0xbc");
static_assert(sizeof(FPayloadServerCreateShip) -
                  offsetof(FPayloadServerCreateShip, Field_0x7c) == 0xcc - 0x7c,
              "CREATESHIP's tail is 0x50 bytes (retail payload 0xcc)");
static_assert(offsetof(FPayloadServerCreateSolar, NameASize) -
                  offsetof(FPayloadServerCreateSolar, Field_0x7c) == 0x80 - 0x7c,
              "FUN_06da1620 is handed &payload+0x80 for CREATESOLAR's first name block");
static_assert(offsetof(FPayloadServerCreateSolar, NameBSize) -
                  offsetof(FPayloadServerCreateSolar, Field_0x7c) == 0x88 - 0x7c,
              "FUN_06da1620 is handed &payload+0x88 for CREATESOLAR's second name block");
static_assert(sizeof(FPayloadServerCreateSolar) -
                  offsetof(FPayloadServerCreateSolar, Field_0x7c) == 0x98 - 0x7c,
              "CREATESOLAR's tail is 0x1c bytes (retail payload 0x98)");

// The create-object encoders inline this pair twice each: a dword byte-count followed by
// that many raw bytes -- 06d9803b/06d9807e and 06d980d8/06d9811d for CREATESHIP,
// 06dac923/06dac955 and 06dac9aa/06dac9ea for CREATESOLAR.
bool RpcPutSizedBlock(FRpcMarshalStream& Stream, unsigned int Size,
                      const void* Buffer) {
    if (!Stream.PutDword(Size)) {
        return false;
    }
    return Stream.PutBytes(Buffer, Size);
}

// FUN_06da1620, the decode-side counterpart, called with a pointer to the CONTIGUOUS
// {Size, Buffer} pair (06d9859e / 06d985ac, 06dacbb1 / 06dacbbf). Body at
// 06da1620..06da16a8: advance 4 and read the size into pair[0], operator_new(size) into
// pair[1], advance by size, then REP MOVS the bytes in. Retail allocates BEFORE the
// second bounds check, which is why the pointer is stored even when the read then fails
// -- the packet's destructor is what frees it either way.
bool RpcGetSizedBlock(FRpcMarshalStream& Stream, unsigned int& OutSize,
                      void*& OutBuffer) {
    if (!Stream.GetDword(OutSize)) {                                     // 06da1623
        return false;
    }
    OutBuffer = ::operator new(OutSize, std::nothrow);                   // 06da165a, untested
    return Stream.GetBytes(OutBuffer, OutSize);                          // 06da1664
}

// The CREATESHIP tail's one flag byte, built at 06d97d82..06d97daf and taken apart at
// 06d982d0..06d982e5. Bit 0x04 is a presence switch as well as a value bit: it decides
// between the wide display name and the two flattened FmtStrs.
static const unsigned char kCreateShipFlagField_0x7c = 0x01;  // 06d97d91 -> 06d982da
static const unsigned char kCreateShipFlagField_0xc8 = 0x02;  // 06d97daf -> 06d982e5
static const unsigned char kCreateShipFlagField_0x9c = 0x04;  // 06d97da0 -> 06d983b9

// 0x06d97d20 -- CREATESHIP, packet vtable +0x04.
unsigned int RpcEncodeServerCreateShip(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerCreateShip* Wire =
        reinterpret_cast<const FPayloadServerCreateShip*>(Packet.Payload());

    // 06d97d31..06d97d43: Cursor = 0, Limit = Capacity, Ptr = *BufferHolder, Error = 0.
    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    // 06d97d55 FUN_06da1380 -- the two routing bytes, and the cursor starts at 2.
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    // 06d97d7d FUN_06da3720, the shared 0x7c header. Retail DISCARDS the return value
    // here (06d97d82 reads the next field straight away) and lets the sticky error stop
    // the first field below, so this port does not add a test retail has not got.
    RpcEncodeCreateObjectCommon(Stream, &Wire->Common);

    unsigned char Flags = 0;                                             // 06d97d8a
    if (Wire->Field_0x7c != 0) {                                         // 06d97d82 byte test
        Flags |= kCreateShipFlagField_0x7c;
    }
    if (Wire->Field_0x9c != 0) {                                         // 06d97d96 DWORD test
        Flags |= kCreateShipFlagField_0x9c;
    }
    if (Wire->Field_0xc8 != 0) {                                         // 06d97da5 byte test
        Flags |= kCreateShipFlagField_0xc8;
    }
    if (!Stream.PutByte(Flags)) {                                        // 06d97db4 / 06d97de7
        return Stream.GetError();
    }

    // 06d97dec checks 0xc in one go and 06d97e42 hands FUN_06d8f510 a stack copy of the
    // three dwords at packet+0xa0..0xa8; FUN_06d8f510 is a plain three-dword move, so
    // the twelve bytes go out in memory order.
    unsigned char VectorBytes[12];
    memcpy(VectorBytes, &Wire->Field_0x80, sizeof(VectorBytes));         // 06d97e1b..06d97e3e
    if (!Stream.PutBytes(VectorBytes, sizeof(VectorBytes))) {
        return Stream.GetError();
    }
    // 06d97e47 `FLD float ptr [EBX+0xb8]` * DAT_06db3fd8 (127.0), ftol, one byte.
    if (!Stream.PutQuantized(Wire->Field_0x98, FRpcMarshalStream::kOrientationScale)) {
        return Stream.GetError();
    }
    if (!Stream.PutWord(Wire->Field_0xa0)) {                             // 06d97eb7
        return Stream.GetError();
    }
    if (!Stream.PutByte(Wire->Field_0xa2)) {                             // 06d97eec
        return Stream.GetError();
    }

    if ((Flags & kCreateShipFlagField_0x9c) != 0) {                      // 06d97ef9 TEST AL,0x4
        // Only the LOW BYTE of the dword travels (06d97f09 `MOV DL,byte [EBX+0xbc]`);
        // the decoder zero-extends it back into all four bytes.
        if (!Stream.PutByte((unsigned char)(Wire->Field_0x9c & 0xffu))) {
            return Stream.GetError();
        }
        // Field_0xa4 rides on this branch ONLY -- nothing writes it when the bit is
        // clear, and the decoder leaves the packet's zero in place.
        if (!Stream.PutWord(Wire->Field_0xa4)) {                         // 06d97f5c
            return Stream.GetError();
        }
        if (!SinglePlayer()) {                                           // 06d97f6b / 06d97f73
            // 06d97f7f writes the LOW BYTE of the dword length but 06d97fbf copies
            // `length * 2` bytes off the full dword. Retail's own desynchronisation for
            // a name of 128 characters or more; reproduced, not quietly repaired.
            if (!Stream.PutByte((unsigned char)(Wire->DisplayNameLength & 0xffu))) {
                return Stream.GetError();
            }
            if (!Stream.PutBytes(Wire->DisplayName, Wire->DisplayNameLength * 2u)) {
                return Stream.GetError();
            }
        }
    } else if (!SinglePlayer()) {                                        // 06d97ffe / 06d98006
        if (!RpcPutSizedBlock(Stream, Wire->NameASize, Wire->NameABuffer)) {
            return Stream.GetError();
        }
        if (!RpcPutSizedBlock(Stream, Wire->NameBSize, Wire->NameBBuffer)) {
            return Stream.GetError();
        }
    }

    // 06d98149: retail asks SinglePlayer() a second time here rather than reusing the
    // answer; it is a pure read of a Common.dll flag, so asking twice is the same.
    if (!SinglePlayer()) {
        if (!Stream.PutDword(Wire->Affiliation)) {                       // 06d98189
            return Stream.GetError();
        }
        // 06d98191 `FLD float ptr [EBX+0xe4]` * 127.0 -- the attitude, [-1,+1].
        if (!Stream.PutQuantized(Wire->Attitude, FRpcMarshalStream::kOrientationScale)) {
            return Stream.GetError();
        }
    }
    // 06d981e9 FUN_06da13c0 -- `XOR EAX,EAX ; RET 0xc`, nothing to emit.
    *OutLength = Stream.Length();                                    // 06d98204
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;                                                            // 06d98248
}

// 0x06d98250 -- CREATESHIP, packet vtable +0x08.
unsigned int RpcDecodeServerCreateShip(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength) {
    FPayloadServerCreateShip* Wire =
        reinterpret_cast<FPayloadServerCreateShip*>(Packet.Payload());

    // 06d9825f..06d98275, and 06d9826b takes the buffer pointer with no dereference.
    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06d9827c FUN_06da13c0 -- the header hook on this side reads nothing.
    // 06d982a2 FUN_06da4020, the shared 0x7c header; its return value is discarded at
    // 06d982a7 exactly as on the encode side.
    RpcDecodeCreateObjectCommon(Stream, &Wire->Common);

    unsigned char Flags = 0;
    if (!Stream.GetByte(Flags)) {                                        // 06d982a7 / 06d982cb
        return Stream.GetError();
    }
    // 06d982d0 `MOV AL,0x1` -- both bits store the literal 1, and a clear bit leaves the
    // field alone rather than zeroing it. The packet arrives zeroed, so it comes to the
    // same thing; transcribed as retail has it.
    if ((Flags & kCreateShipFlagField_0x7c) != 0) {                      // 06d982d2
        Wire->Field_0x7c = 1;                                            // 06d982da
    }
    if ((Flags & kCreateShipFlagField_0xc8) != 0) {                      // 06d982e0
        Wire->Field_0xc8 = 1;                                            // 06d982e5
    }

    // 06d982eb checks 0xc at once, 06d98318 hands FUN_06d8f510 the cursor as the source
    // and &packet+0xa0 as the destination -- the same three dwords, read back in place.
    if (!Stream.GetBytes(&Wire->Field_0x80, 12u)) {
        return Stream.GetError();
    }
    // 06d9834c `MOVSX EAX,CL` then FILD * DAT_06db3fe0 (1/127): SIGNED, which is what
    // makes the byte cover [-1,+1].
    if (!Stream.GetQuantizedSigned(Wire->Field_0x98,
                                   FRpcMarshalStream::kOrientationInvScale)) {
        return Stream.GetError();
    }
    if (!Stream.GetWord(Wire->Field_0xa0)) {                             // 06d98385
        return Stream.GetError();
    }
    if (!Stream.GetByte(Wire->Field_0xa2)) {                             // 06d983b4
        return Stream.GetError();
    }

    if ((Flags & kCreateShipFlagField_0x9c) != 0) {                      // 06d983b9
        unsigned char Narrow = 0;
        if (!Stream.GetByte(Narrow)) {                                   // 06d983ea
            return Stream.GetError();
        }
        // 06d983ef `XOR EAX,EAX ; MOV AL,CL` -- ZERO-extended into the whole dword.
        Wire->Field_0x9c = (unsigned int)Narrow;                         // 06d983f3
        if (!Stream.GetWord(Wire->Field_0xa4)) {                         // 06d9841b
            return Stream.GetError();
        }
        if (!SinglePlayer()) {                                           // 06d9842a / 06d98432
            unsigned char LengthByte = 0;
            if (!Stream.GetByte(LengthByte)) {                           // 06d9845c
                return Stream.GetError();
            }
            // 06d98461 `MOVSX EAX,CL` -- retail SIGN-extends this count and uses the
            // signed value for the stored length (06d98469), for `operator new(2*n + 2)`
            // (06d98464 / 06d9846f, bare, stored unchecked at 06d98474) and for the byte
            // run (06d98486 SHL, 06d984c3 REP MOVS). A byte of 0x80..0xff therefore
            // wraps the cursor backwards past the bound check and copies a wrapped
            // count -- reproduced as it is, like retail.
            const unsigned int Length = (unsigned int)(int)(signed char)LengthByte;
            Wire->DisplayNameLength = Length;                            // 06d98469
            Wire->DisplayName = static_cast<wchar_t*>(
                ::operator new((Length * 2u) + 2u, std::nothrow));       // 06d9846f
            if (!Stream.GetBytes(Wire->DisplayName, Length * 2u)) {      // 06d9847a / 06d984c3
                return Stream.GetError();
            }
            Wire->DisplayName[Length] = L'\0';                           // 06d984e1
        }
    } else if (!SinglePlayer()) {                                        // 06d98587 / 06d9858f
        if (!RpcGetSizedBlock(Stream, Wire->NameASize, Wire->NameABuffer)) {
            return Stream.GetError();                                    // 06d9859e
        }
        if (!RpcGetSizedBlock(Stream, Wire->NameBSize, Wire->NameBBuffer)) {
            return Stream.GetError();                                    // 06d985ac
        }
    }

    if (!SinglePlayer()) {                                               // 06d984ec / 06d984f4
        if (!Stream.GetDword(Wire->Affiliation)) {                       // 06d98519
            return Stream.GetError();
        }
        // 06d9854a `MOVSX EDX,CL` * 1/127 -- signed again, same as Field_0x98.
        if (!Stream.GetQuantizedSigned(Wire->Attitude,
                                       FRpcMarshalStream::kOrientationInvScale)) {
            return Stream.GetError();
        }
    }
    // 06d98571 FUN_06da13c0 -- the trailer hook, a no-op on this side too.
    *OutLength = Stream.Length();                                    // 06d985b6
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;                                                            // 06d98601
}

// 0x06dac7c0 -- CREATESOLAR, packet vtable +0x04. Shorter than CREATESHIP and with NO
// flag byte at all: its two leading bytes go out unconditionally, and the reputation
// pair comes BEFORE the two name blocks rather than after them.
unsigned int RpcEncodeServerCreateSolar(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerCreateSolar* Wire =
        reinterpret_cast<const FPayloadServerCreateSolar*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);           // 06dac7ce
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));     // 06dac7f1
    RpcEncodeCreateObjectCommon(Stream, &Wire->Common);                  // 06dac816
    if (!Stream.PutByte(Wire->Field_0x7c)) {                             // 06dac845
        return Stream.GetError();
    }
    if (!Stream.PutByte(Wire->Field_0x7d)) {                             // 06dac87a
        return Stream.GetError();
    }
    if (!SinglePlayer()) {                                               // 06dac885 / 06dac88d
        if (!Stream.PutDword(Wire->Affiliation)) {                       // 06dac8bf
            return Stream.GetError();
        }
        // 06dac8c7 `FLD float ptr [EBP+0xb4]` * 127.0.
        if (!Stream.PutQuantized(Wire->Attitude, FRpcMarshalStream::kOrientationScale)) {
            return Stream.GetError();
        }
        if (!RpcPutSizedBlock(Stream, Wire->NameASize, Wire->NameABuffer)) {
            return Stream.GetError();                                    // 06dac925
        }
        if (!RpcPutSizedBlock(Stream, Wire->NameBSize, Wire->NameBBuffer)) {
            return Stream.GetError();                                    // 06dac9ad
        }
    }
    // 06daca21 FUN_06da13c0 -- the trailer hook.
    *OutLength = Stream.Length();                                    // 06daca39
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;                                                            // 06daca74
}

// 0x06daca80 -- CREATESOLAR, packet vtable +0x08.
unsigned int RpcDecodeServerCreateSolar(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength) {
    FPayloadServerCreateSolar* Wire =
        reinterpret_cast<FPayloadServerCreateSolar*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06dacaaa FUN_06da13c0, then 06dacacf FUN_06da4020 with its result discarded.
    RpcDecodeCreateObjectCommon(Stream, &Wire->Common);
    if (!Stream.GetByte(Wire->Field_0x7c)) {                             // 06dacaf7
        return Stream.GetError();
    }
    if (!Stream.GetByte(Wire->Field_0x7d)) {                             // 06dacb20
        return Stream.GetError();
    }
    if (!SinglePlayer()) {                                               // 06dacb2b / 06dacb33
        if (!Stream.GetDword(Wire->Affiliation)) {                       // 06dacb5d
            return Stream.GetError();
        }
        // 06dacb8e `MOVSX EDX,CL` * 1/127 -- signed, like every quantized field in this
        // family; the 255.0 pair belongs to the shared header codec, not to these tails.
        if (!Stream.GetQuantizedSigned(Wire->Attitude,
                                       FRpcMarshalStream::kOrientationInvScale)) {
            return Stream.GetError();
        }
        if (!RpcGetSizedBlock(Stream, Wire->NameASize, Wire->NameABuffer)) {
            return Stream.GetError();                                    // 06dacbb1
        }
        if (!RpcGetSizedBlock(Stream, Wire->NameBSize, Wire->NameBBuffer)) {
            return Stream.GetError();                                    // 06dacbbf
        }
    }
    // 06dacbcd FUN_06da13c0 -- the trailer hook.
    *OutLength = Stream.Length();                                    // 06dacbe5
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;                                                            // 06dacc2b
}

// ===========================================================================
// ch1 0x02 FLPACKET_COMMON_FIREWEAPON -- encoder LAB_06da4f60 (597 instructions),
// decoder LAB_06da5550 (557). The heaviest anonymous vtable pair left in the module,
// and the only codec so far that changes the WIDTH of a field depending on its value.
//
// WHOSE SLOTS THESE ARE, established before a line of it was ported. Both addresses
// appear exactly once in the whole listing, as .rdata:06db438c and .rdata:06db4390 --
// consecutive entries of the four-word table PTR_FUN_06db4388. The table's only
// reference is 06d9afe6 `MOV dword ptr [EAX],.rdata:PTR_FUN_06db4388` inside
// FUN_06d9afb0, and that function's next instruction, 06d9afec
// `MOV dword ptr [EAX + 0xc],0x10002`, writes the routing header: channel 1, opcode 2,
// which rpc_packet_registry.cpp registers as FLPACKET_COMMON_FIREWEAPON (ctor 0x06d9afb0,
// factory 0x06d9af70, size 0x40). So the pair is +0x04 Encode / +0x08 Decode of the
// FIREWEAPON packet class, not of anything else, and the payload they walk is
// FPayloadCommonFireWeapon.
//
// The same constructor also settles the payload layout, which had been read off the
// IServer argument struct alone:
//   06d9afbf  [EAX+0x10] = 0          -- the marshal state, packet-wide
//   06d9afc5  [EAX+0x20] = 0          -- ObjectId
//   06d9afcd  [EAX+0x24] = 0x7f7fffff -- TargetPos.x, and +0x28 / +0x2c the same FLT_MAX
//   06d9afda  [EAX+0x30] = the ctor's char argument
//   06d9afdd  [EAX+0x34] = [EAX+0x38] = [EAX+0x3c] = 0
// That +0x30 byte is NOT a field of its own: it is the allocator sub-object of the
// hardpoint vector, whose _First/_Last/_End follow at +0x34/+0x38/+0x3c. The compiler
// copies a byte of the incoming allocator into it and nothing ever reads it -- the same
// junk-allocator-copy this port already confirmed at 06d87ee8 on a std::list. Neither
// codec serialises it. FRetailVector<unsigned short> at payload+0x10 reproduces exactly
// that, so FPayloadCommonFireWeapon needed no change.
//
// THE WIRE FORMAT. The first two bytes are the routing word (06da4f94 FUN_06da1380),
// then the object id as a plain dword. What follows depends on COMMON.DLL::SinglePlayer
// (06da4fea / 06da55cb), which BOTH directions consult, so the two ends agree:
//
//   single player -- 06da4ff8..06da50d7 / 06da55df..06da5690
//       12 raw bytes of TargetPos (FUN_06d8f510 is a plain three-dword move),
//       one count byte, then one word per hardpoint id. No flag byte at all.
//
//   multiplayer -- 06da50e8..06da54db / 06da56bd..06da5aee
//       one flag byte, then each of x, y, z in the width its flag bits chose, then the
//       count byte (unless bit 0x40 says there is exactly one id), then the ids.
//
// THE TIERS, from the .rdata constants rather than from the shape of the code:
//   DAT_06db6130 = 40 50 .. = 64.0        the small threshold   (06da50f3 FCOM)
//   DAT_06db6128 = 40 c0 .. = 8192.0      the medium threshold  (06da5106 FCOMP)
//   DAT_06db6124 = 3c800000 = 2^-6        (06da51b5 FMUL, the small tier's pre-scale)
//   DAT_06db3fd8 = 127.0                  (06da51bb FMUL, the small tier's scale)
//   DAT_06db6120 = 39000000 = 2^-13       (06da520b FMUL, the medium tier's pre-scale)
//   DAT_06db6118 = 40dfffc0.. = 32767.0   (06da5211 FMUL, the medium tier's scale)
//   DAT_06db6140 = 3fe02040.. = 64/127    (06da5712 FMUL, the small tier's inverse)
//   DAT_06db6138 = 3fd00020.. = 8192/32767(06da574f FMUL, the medium tier's inverse)
// The two inverses are not guesses: 64/127 is (1/2)(1 + 2^-7 + 2^-14 + ...) and
// 8192/32767 is (1/4)(1 + 2^-15 + 2^-30 + ...), and both mantissas in .rdata are exactly
// those repeating patterns (02 04 08 10 20 40 80 and 20 00 40 00 80).
//
// The port folds retail's two FMULs into one constant. That is not an approximation:
// the first factor is an exact power of two, so `v * 2^-6` is exact in any precision and
// `(v * 2^-6) * 127.0` and `v * (127.0/64.0)` round identically.
//
// FOUR THINGS TO LEAVE ALONE, all of which read as mistakes and are not:
//
//  1. Z HAS NO MEDIUM TIER. The flag computation tests x against both thresholds
//     (06da50f3 and 06da5106) and y against both (06da511e and 06da5132), but z against
//     64.0 only -- 06da514b `FCOMP double ptr [DAT_06db6130]`, and then nothing. Bit 0x20
//     is therefore never set by any encoder. Both the encoder's writer (06da53c5
//     `TEST BL,0x20`) and the decoder (06da5859 `TEST DL,0x20`) still handle it, so the
//     branch is transcribed and left unreachable, exactly as retail leaves it. Filling in
//     the missing tier "for symmetry" would put bytes on the wire that retail's own
//     decoder would read back correctly and retail's own encoder would never produce --
//     a divergence that no test would catch until two different builds talked.
//  2. THE TWO ONE-HARDPOINT TESTS ARE NOT THE SAME TEST. 06da516d compares the whole
//     int (`CMP EAX,0x1`) to decide bit 0x40; 06da5466 compares only the low byte
//     (`CMP AL,0x1`) to decide whether to leave the count byte off. They disagree at
//     exactly 257 hardpoints. Reproduced as found.
//  3. THE SINGLE-PLAYER BRANCH HAS NO 0x40 SHORTCUT. 06da5061 writes the count byte
//     unconditionally.
//  4. THE FLAG BYTE IS COMPUTED BEFORE IT IS WRITTEN, AND THE COUNT AFTER. Retail does
//     not cache the count across the two uses: it recomputes `(last-first)>>1` at
//     06da515f for the flag and again at 06da545f for the write.
//
// The decoder's own quirk: on both branches it reads the count as an UNSIGNED byte and
// the loop counts in a BYTE register on both branches, so a count of 0 reads nothing and
// a count of 0xff reads 255 ids. The two branches do not share a register pair, and the
// note here used to give one branch's instruction text with the other branch's address:
//   single player  06da568c `INC BL` / 06da568e `CMP BL,AL`  / 06da5690 `JC LAB_06da5640`
//   multiplayer    06da5ae6 `INC AL` / 06da5ae8 `CMP AL,CL`  / 06da5aee `JC LAB_06da5900`
// (the MP counter is the byte at ESP+0x13 and its bound the byte at ESP+0x24; the
// 06da5aec that stood here is inside 06da5aea `MOV byte ptr [ESP + 0x13],AL`).
// ===========================================================================

// |v|, without pulling in <cmath>. Retail uses FABS; the two agree on NaN as well --
// retail's FCOM leaves C0 and C2 both set for an unordered compare, TEST AH,0x5 then has
// even parity, the JP is taken, and the value falls through to the raw-float tier, which
// is what `!(NaN < limit)` does here too.
static double RpcFireWeaponAbs(double Value) {
    return Value < 0.0 ? -Value : Value;
}

// 06da50f3 / 06da5106 and their y and z copies.
static const double kFireWeaponSmallLimit = 64.0;      // DAT_06db6130
static const double kFireWeaponMediumLimit = 8192.0;   // DAT_06db6128
// DAT_06db6124 * DAT_06db3fd8 and DAT_06db6120 * DAT_06db6118, folded (see above).
static const double kFireWeaponSmallScale = 127.0 / 64.0;
static const double kFireWeaponMediumScale = 32767.0 / 8192.0;
static const double kFireWeaponSmallInvScale = 64.0 / 127.0;      // DAT_06db6140
static const double kFireWeaponMediumInvScale = 8192.0 / 32767.0; // DAT_06db6138

static const unsigned char kFireWeaponXSmall = 0x01;   // 06da5102 MOV BL,0x1
static const unsigned char kFireWeaponXMedium = 0x02;  // 06da5113 MOV BL,0x2
static const unsigned char kFireWeaponYSmall = 0x04;   // 06da512d OR BL,0x4
static const unsigned char kFireWeaponYMedium = 0x08;  // 06da513f OR BL,0x8
static const unsigned char kFireWeaponZSmall = 0x10;   // 06da5158 OR BL,0x10
// Read at 06da53c5 and 06da5859, written nowhere. See note 1 above.
static const unsigned char kFireWeaponZMedium = 0x20;
static const unsigned char kFireWeaponOneHardpoint = 0x40;  // 06da5172 OR BL,0x40

// The per-component width decision, 06da50e8..06da515f. bMedium is false for z.
static unsigned char RpcFireWeaponPickTier(float Value, unsigned char SmallBit,
                                           unsigned char MediumBit, bool bHasMediumTier) {
    const double Abs = RpcFireWeaponAbs(static_cast<double>(Value));
    if (Abs < kFireWeaponSmallLimit) {
        return SmallBit;
    }
    if (bHasMediumTier && Abs < kFireWeaponMediumLimit) {
        return MediumBit;
    }
    return 0;
}

// 06da51b2 / 06da5208 / 06da5282 and their y and z copies -- one body, three call sites,
// exactly as retail's three copies differ only in the field and the bits.
static bool RpcFireWeaponPutComponent(FRpcMarshalStream& Stream, float Value,
                                      unsigned char Flags, unsigned char SmallBit,
                                      unsigned char MediumBit) {
    if ((Flags & SmallBit) != 0) {
        return Stream.PutQuantized(Value, kFireWeaponSmallScale);
    }
    if ((Flags & MediumBit) != 0) {
        return Stream.PutQuantizedWord(Value, kFireWeaponMediumScale);
    }
    // 06da5285 `FLD float ptr [EDI+0x24] ; FSTP float ptr [EAX]` -- the bits as they are.
    return Stream.PutFloat(Value);
}

// 06da5702 / 06da573a / 06da5772 and their y and z copies.
static bool RpcFireWeaponGetComponent(FRpcMarshalStream& Stream, float& OutValue,
                                      unsigned char Flags, unsigned char SmallBit,
                                      unsigned char MediumBit) {
    if ((Flags & SmallBit) != 0) {
        return Stream.GetQuantizedSigned(OutValue, kFireWeaponSmallInvScale);
    }
    if ((Flags & MediumBit) != 0) {
        return Stream.GetQuantizedWordSigned(OutValue, kFireWeaponMediumInvScale);
    }
    // 06da5774 `MOV ECX,[EAX] ; MOV [ESI+0x24],ECX` -- a dword move, not a conversion.
    return Stream.GetFloat(OutValue);
}

// LAB_06da4f60 -- FIREWEAPON, packet vtable +0x04.
unsigned int RpcEncodeCommonFireWeapon(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadCommonFireWeapon* Wire =
        reinterpret_cast<const FPayloadCommonFireWeapon*>(Packet.Payload());

    // 06da4f71..06da4f82: Cursor = 0, Limit = Capacity (the SECOND argument), Ptr =
    // *BufferHolder (the FIRST, dereferenced), Error = 0.
    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    // 06da4f94 FUN_06da1380 -- the two routing bytes.
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    // 06da4faf..06da4fe7.
    if (!Stream.PutDword(Wire->ObjectId)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }

    if (SinglePlayer()) {                                        // 06da4fea / 06da4ff2
        // 06da5025..06da504c: retail copies the three floats to its own stack and hands
        // that to FUN_06d8f510, a plain three-dword move, so twelve bytes go out in
        // memory order.
        unsigned char VectorBytes[12];
        memcpy(VectorBytes, &Wire->TargetPos, sizeof(VectorBytes));
        if (!Stream.PutBytes(VectorBytes, sizeof(VectorBytes))) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        // 06da504f..06da508f: the count byte, unconditionally on this branch.
        const unsigned int SpCount = Wire->Hardpoints.Count();
        if (!Stream.PutByte((unsigned char)(SpCount & 0xffu))) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        // 06da5098 enters the loop only when _First != _Last, then walks by 2.
        for (unsigned int i = 0; i < SpCount; ++i) {              // 06da50a6
            if (!Stream.PutWord(Wire->Hardpoints.Begin[i])) {
                return FRpcMarshalStream::kEncodeOverflow;
            }
        }
    } else {
        unsigned char Flags = 0;                                 // 06da50eb XOR BL,BL
        Flags |= RpcFireWeaponPickTier(Wire->TargetPos.x, kFireWeaponXSmall,
                                       kFireWeaponXMedium, true);
        Flags |= RpcFireWeaponPickTier(Wire->TargetPos.y, kFireWeaponYSmall,
                                       kFireWeaponYMedium, true);
        // false: z really is tested against 64.0 only. See note 1 at the top.
        Flags |= RpcFireWeaponPickTier(Wire->TargetPos.z, kFireWeaponZSmall,
                                       kFireWeaponZMedium, false);
        // 06da515f..06da5175: the full int is compared here, unlike 06da5466.
        if (Wire->Hardpoints.Begin != nullptr && Wire->Hardpoints.Count() == 1u) {
            Flags |= kFireWeaponOneHardpoint;
        }
        if (!Stream.PutByte(Flags)) {                            // 06da5179..06da51a7
            return FRpcMarshalStream::kEncodeOverflow;
        }

        if (!RpcFireWeaponPutComponent(Stream, Wire->TargetPos.x, Flags,
                                       kFireWeaponXSmall, kFireWeaponXMedium)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        if (!RpcFireWeaponPutComponent(Stream, Wire->TargetPos.y, Flags,
                                       kFireWeaponYSmall, kFireWeaponYMedium)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        if (!RpcFireWeaponPutComponent(Stream, Wire->TargetPos.z, Flags,
                                       kFireWeaponZSmall, kFireWeaponZMedium)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }

        // 06da5454..06da549b: the count is recomputed, and the byte is SKIPPED when its
        // low byte is 1 -- the decoder recovers that case from bit 0x40 instead.
        const unsigned int MpCount = Wire->Hardpoints.Count();
        const unsigned char CountByte = (unsigned char)(MpCount & 0xffu);
        if (!(Wire->Hardpoints.Begin != nullptr && CountByte == 1u)) {
            if (!Stream.PutByte(CountByte)) {
                return FRpcMarshalStream::kEncodeOverflow;
            }
        }
        // 06da549e enters the loop only when _First != _Last.
        for (unsigned int i = 0; i < MpCount; ++i) {              // 06da54a6
            if (!Stream.PutWord(Wire->Hardpoints.Begin[i])) {
                return FRpcMarshalStream::kEncodeOverflow;
            }
        }
    }

    // 06da54e5 FUN_06da13c0 -- `XOR EAX,EAX ; RET 0xc`, the trailer hook, a no-op.
    *OutLength = Stream.Length();                            // 06da5500..06da5506
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;                                                    // 06da5541
}

// LAB_06da5550 -- FIREWEAPON, packet vtable +0x08.
unsigned int RpcDecodeCommonFireWeapon(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength) {
    FPayloadCommonFireWeapon* Wire =
        reinterpret_cast<FPayloadCommonFireWeapon*>(Packet.Payload());

    // 06da5561..06da5579: Limit = Size (the SECOND argument), Ptr = Buffer (the FIRST,
    // taken WITHOUT a dereference -- the decode side is handed the bytes, not a holder).
    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06da5580 FUN_06da13c0 -- the same no-op hook; the routing word is already gone by
    // the time the decoder is called, so the cursor starts at the object id.
    if (!Stream.GetDword(Wire->ObjectId)) {                      // 06da559b..06da55c9
        return FRpcMarshalStream::kDecodeOverrun;
    }

    // Retail rebuilds the hardpoint vector one element at a time, at _Last: with
    // COMMON.DLL::std::vector<unsigned short>::insert(_Last, 1, v) (IAT .rdata:06db3114,
    // ?insert@?$vector@GV?$allocator@G@std@@@std@@QAEXPAGIABG@Z) on the single-player
    // branch at 06da5682, and with the same routine INLINED on the multiplayer branch at
    // 06da5945..06da5a1f. RpcRetailVectorAppend is that insert, capacity policy and all.
    // It APPENDS: a freshly constructed packet holds the three nulls FUN_06d9afb0 wrote.
    unsigned char Count = 0;
    if (SinglePlayer()) {                                        // 06da55cb / 06da55d9
        // 06da55fd..06da560b: twelve bytes straight into TargetPos.
        unsigned char VectorBytes[12];
        if (!Stream.GetBytes(VectorBytes, sizeof(VectorBytes))) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        memcpy(&Wire->TargetPos, VectorBytes, sizeof(VectorBytes));
        if (!Stream.GetByte(Count)) {                            // 06da560d..06da563c
            return FRpcMarshalStream::kDecodeOverrun;
        }
    } else {
        unsigned char Flags = 0;
        if (!Stream.GetByte(Flags)) {                            // 06da56bd..06da56e2
            return FRpcMarshalStream::kDecodeOverrun;
        }
        if (!RpcFireWeaponGetComponent(Stream, Wire->TargetPos.x, Flags,
                                       kFireWeaponXSmall, kFireWeaponXMedium)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        if (!RpcFireWeaponGetComponent(Stream, Wire->TargetPos.y, Flags,
                                       kFireWeaponYSmall, kFireWeaponYMedium)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        if (!RpcFireWeaponGetComponent(Stream, Wire->TargetPos.z, Flags,
                                       kFireWeaponZSmall, kFireWeaponZMedium)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        // 06da58b8: bit 0x40 stands in for a count byte of 1.
        if ((Flags & kFireWeaponOneHardpoint) != 0) {
            Count = 1;                                           // 06da58bd
        } else if (!Stream.GetByte(Count)) {                     // 06da58c4..06da58eb
            return FRpcMarshalStream::kDecodeOverrun;
        }
    }

    // 06da563e / 06da58f8: `TEST CL,CL ; JBE` -- an unsigned test, so 0 reads nothing.
    // 06da568c / 06da5aee: the counter is a byte compared unsigned against Count.
    for (unsigned int Index = 0; Index < (unsigned int)Count; ++Index) {
        unsigned short Id = 0;
        if (!Stream.GetWord(Id)) {                               // 06da5640 / 06da5900
            return FRpcMarshalStream::kDecodeOverrun;            // LAB_06da5af9
        }
        RpcRetailVectorAppend(Wire->Hardpoints, Id);             // 06da5682 / 06da5945
    }

    // 06da569e FUN_06da13c0 again, and the same no-op.
    *OutLength = Stream.Length();                            // 06da5b06 onward
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch3 0x22 FLPACKET_CLIENT_DBGCREATESHIP -- LAB_06db0560 (+0x04, 597 instructions) and
// LAB_06db0b30 (+0x08, 495). The layout, and the two corrections to the note that
// identified them, are set out on FPayloadClientDbgCreateShip in rpc_payloads.h.
//
// WHOSE SLOTS THESE ARE, re-checked rather than inherited. Both addresses appear
// exactly once in the listing, at .rdata:06db5d58 and .rdata:06db5d5c, the second and
// third words of PTR_FUN_06db5d54. The table is published at 06da0e89
// `MOV dword ptr [EAX],.rdata:PTR_FUN_06db5d54` inside FUN_06da0e20, whose very next
// instruction 06da0e8f writes `dword ptr [EAX+0xc],0x30022` -- channel 3, opcode 0x22.
// The table's only other reference, 06db0508, is the same class's destructor
// FUN_06db0500 restoring the vptr before it frees the four blocks, not another type.
//
// ARGUMENT ASYMMETRY, verified here on these two bodies and not carried over:
//   * encode  06db0560 `MOV EAX,[ESP+0x8]`  (arg2 = capacity) and 06db056b
//     `MOV ECX,[ESP+0x1c]` after SUB ESP,0x10 + two PUSHes = arg1, then 06db0576
//     `MOV EDX,dword ptr [ECX]` -- arg1 is a pointer TO the buffer pointer;
//   * decode  06db0b30 `MOV EAX,[ESP+0x8]` (arg2 = size) and 06db0b39
//     `MOV ECX,[ESP+0x10]` after three PUSHes = arg1, stored straight into the stream
//     at 06db0b43 `MOV [EBX+0x8],ECX` with no load between -- arg1 IS the buffer.
// Both end in `RET 0xc`, so three dword arguments each.
//
// NOT A QUANTIZER IN SIGHT. Neither body contains a single FLD/FMUL/ftol: every field
// travels at its natural width. The two Vectors and the Quaternion go out as raw
// dwords through the plain 3- and 4-dword copiers. So the "pick GetQuantizedSigned or
// GetQuantizedUnsigned per field from the bytes" rule has nothing to decide here, and
// inventing a quantizer by analogy with FIREWEAPON would have put bytes on the wire
// retail never writes.
//
// THE ZERO-COUNT SHORTCUT IS REAL AND IS IN BOTH DIRECTIONS. Each of the four blocks
// is preceded by `TEST EAX,EAX ; JBE <past the block>` on the COUNT -- 06db07e2,
// 06db0871, 06db08ff, 06db0a63 out and 06db0d3a, 06db0dc9, 06db0e58, 06db0f9a back --
// so a count of zero consumes no bytes and, on the decode side, allocates nothing and
// leaves the pointer as the constructor left it.
//
// TODO, the same open gap the create-object slots carry: retail's epilogue also
// updates the per-type statistics record through FUN_06da16b0(packet+0x0c). The encode
// side does count/total/max/average at record+0x00/+0x18/+0x10/+0x08 (06db0aee..
// 06db0b1a) and the decode side at +0x04/+0x1c/+0x14/+0x0c (06db1035..06db106c),
// against a cursor that the decoder biases by two because its header hook reads
// nothing. No codec in this port reproduces it yet; it wants doing once for all of
// them together.
// ===========================================================================

// LAB_06db0560 -- DBGCREATESHIP, packet vtable +0x04.
unsigned int RpcEncodeClientDbgCreateShip(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadClientDbgCreateShip* Wire =
        reinterpret_cast<const FPayloadClientDbgCreateShip*>(Packet.Payload());

    // 06db056f..06db0582: Cursor = 0, Limit = Capacity, Ptr = *BufferHolder, Error = 0.
    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    // 06db0594 FUN_06da1380 -- the two routing bytes; the cursor starts at 2.
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));

    // 06db05a9..06db0671, five plain dwords.
    if (!Stream.PutDword(Wire->Field_0x00) ||
        !Stream.PutDword(Wire->Field_0x04) ||
        !Stream.PutDword(Wire->Field_0x08) ||
        !Stream.PutDword(Wire->Field_0x0c) ||
        !Stream.PutDword(Wire->Field_0x10)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }

    // 06db067b..06db070c: two twelve-byte runs through FUN_06d8f510, a plain three-dword
    // move, so the floats go out in memory order.
    if (!Stream.PutBytes(&Wire->Field_0x14, (unsigned int)sizeof(Vector)) ||
        !Stream.PutBytes(&Wire->Field_0x20, (unsigned int)sizeof(Vector))) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    // 06db0716..06db076a: sixteen bytes through FUN_06d8f4f0, the four-dword move.
    if (!Stream.PutBytes(&Wire->Orientation, (unsigned int)sizeof(Quaternion))) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    // 06db0774..06db07a1.
    if (!Stream.PutDword(Wire->Field_0x3c)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }

    // 06db07ab..06db0830: the wide name -- the CHARACTER count, then count*2 bytes.
    // Retail does not null-check the pointer before the REP MOVS and neither does this,
    // for the same reason the FIREWEAPON encoder does not check Hardpoints.Begin.
    if (!Stream.PutDword(Wire->NameCharCount)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (Wire->NameCharCount != 0 &&
        !Stream.PutBytes(Wire->NameChars, Wire->NameCharCount * 2u)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }

    // 06db0838..06db08be and 06db08c6..06db094c: two BYTE blocks, count then bytes.
    if (!Stream.PutDword(Wire->BlockASize)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (Wire->BlockASize != 0 && !Stream.PutBytes(Wire->BlockA, Wire->BlockASize)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutDword(Wire->BlockBSize)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (Wire->BlockBSize != 0 && !Stream.PutBytes(Wire->BlockB, Wire->BlockBSize)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }

    // 06db0954..06db0a19, four more plain dwords.
    if (!Stream.PutDword(Wire->Field_0x50) ||
        !Stream.PutDword(Wire->Field_0x54) ||
        !Stream.PutDword(Wire->Field_0x58) ||
        !Stream.PutDword(Wire->Field_0x5c)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }

    // 06db0a4a..06db0ab9: the dword array -- the ELEMENT count (06db0a6a `SHL EAX,0x2`
    // turns it into bytes), then count*4 bytes.
    if (!Stream.PutDword(Wire->DwordArrayCount)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (Wire->DwordArrayCount != 0 &&
        !Stream.PutBytes(Wire->DwordArray, Wire->DwordArrayCount * 4u)) {
        return FRpcMarshalStream::kEncodeOverflow;
    }

    // 06db0ac7 FUN_06da13c0 -- `XOR EAX,EAX ; RET 0xc`, the no-op trailer hook.
    *OutLength = Stream.Length();                            // 06db0ae2..06db0ae8
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;                                                    // 06db0b1d
}

// LAB_06db0b30 -- DBGCREATESHIP, packet vtable +0x08.
//
// Retail allocates each block BEFORE the bounds check that guards the copy, and stores
// the pointer either way -- 06db0d3f / 06db0dcc / 06db0e5b / 06db0fa0 all write the
// packet field immediately after operator_new and only then advance the cursor. The
// packet's destructor FUN_06db0500 is what frees it, so a half-decoded packet still
// releases cleanly. The port keeps that order for the same reason.
unsigned int RpcDecodeClientDbgCreateShip(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength) {
    FPayloadClientDbgCreateShip* Wire =
        reinterpret_cast<FPayloadClientDbgCreateShip*>(Packet.Payload());

    // 06db0b3d..06db0b55: Limit = Size, Ptr = Buffer taken WITHOUT a dereference,
    // Cursor = 0, Error = 0.
    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06db0b5c FUN_06da13c0 -- the no-op header hook; the routing word is already gone.

    if (!Stream.GetDword(Wire->Field_0x00) ||                    // 06db0b74..06db0c48
        !Stream.GetDword(Wire->Field_0x04) ||
        !Stream.GetDword(Wire->Field_0x08) ||
        !Stream.GetDword(Wire->Field_0x0c) ||
        !Stream.GetDword(Wire->Field_0x10)) {
        return FRpcMarshalStream::kDecodeOverrun;
    }

    if (!Stream.GetBytes(&Wire->Field_0x14, (unsigned int)sizeof(Vector)) ||
        !Stream.GetBytes(&Wire->Field_0x20, (unsigned int)sizeof(Vector))) {
        return FRpcMarshalStream::kDecodeOverrun;               // 06db0c4b..06db0ca1
    }
    if (!Stream.GetBytes(&Wire->Orientation, (unsigned int)sizeof(Quaternion))) {
        return FRpcMarshalStream::kDecodeOverrun;               // 06db0cab..06db0cd3
    }
    if (!Stream.GetDword(Wire->Field_0x3c)) {                    // 06db0cdd..06db0d09
        return FRpcMarshalStream::kDecodeOverrun;
    }

    // 06db0d0c..06db0d91: the wide name.
    if (!Stream.GetDword(Wire->NameCharCount)) {
        return FRpcMarshalStream::kDecodeOverrun;
    }
    if (Wire->NameCharCount != 0) {
        const unsigned int NameBytes = Wire->NameCharCount * 2u;   // 06db0d3c ADD EAX,EAX
        Wire->NameChars = static_cast<wchar_t*>(
            ::operator new(NameBytes, std::nothrow));              // 06db0d3f, untested
        if (!Stream.GetBytes(Wire->NameChars, NameBytes)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
    }

    // 06db0d99..06db0e20 and 06db0e28..06db0eaf: the two byte blocks.
    if (!Stream.GetDword(Wire->BlockASize)) {
        return FRpcMarshalStream::kDecodeOverrun;
    }
    if (Wire->BlockASize != 0) {
        Wire->BlockA = ::operator new(Wire->BlockASize, std::nothrow);  // 06db0dcc, untested
        if (!Stream.GetBytes(Wire->BlockA, Wire->BlockASize)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
    }
    if (!Stream.GetDword(Wire->BlockBSize)) {
        return FRpcMarshalStream::kDecodeOverrun;
    }
    if (Wire->BlockBSize != 0) {
        Wire->BlockB = ::operator new(Wire->BlockBSize, std::nothrow);  // 06db0e5b, untested
        if (!Stream.GetBytes(Wire->BlockB, Wire->BlockBSize)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
    }

    if (!Stream.GetDword(Wire->Field_0x50) ||                    // 06db0eb7..06db0f64
        !Stream.GetDword(Wire->Field_0x54) ||
        !Stream.GetDword(Wire->Field_0x58) ||
        !Stream.GetDword(Wire->Field_0x5c)) {
        return FRpcMarshalStream::kDecodeOverrun;
    }

    // 06db0f67..06db1000: the dword array.
    if (!Stream.GetDword(Wire->DwordArrayCount)) {
        return FRpcMarshalStream::kDecodeOverrun;
    }
    if (Wire->DwordArrayCount != 0) {
        const unsigned int ArrayBytes = Wire->DwordArrayCount * 4u;  // 06db0f9c SHL EAX,2
        Wire->DwordArray = static_cast<unsigned int*>(
            ::operator new(ArrayBytes, std::nothrow));               // 06db0fa0, untested
        if (!Stream.GetBytes(Wire->DwordArray, ArrayBytes)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
    }

    // 06db1014 FUN_06da13c0 again, the same no-op.
    *OutLength = Stream.Length();                            // 06db1029..06db102f
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;                                                    // 06db106f
}


// ===========================================================================
// ch1 0x12 FLPACKET_COMMON_PLAYER_TRADE -- LAB_06da7030 (+0x04, 481 instructions) and
// LAB_06da7430 (+0x08, 483). One packet type carrying eight logical messages, picked by
// the sub-type byte the port already models as FPayloadCommonPlayerTrade::SubType.
//
// OWNERSHIP, on the bytes. Both addresses appear exactly once, at .rdata:06db449c and
// .rdata:06db44a0, the second and third words of PTR_FUN_06db4498 = { FUN_06d9af50,
// LAB_06da7030, LAB_06da7430, IServerImpl::GetServerStats }. The table's only reference
// is 06d9bae4 `MOV dword ptr [EAX],.rdata:PTR_FUN_06db4498` inside the constructor
// FUN_06d9bad0, and the next instruction, 06d9baea, writes 0x10012 to packet+0x0c --
// channel 1, opcode 0x12, registry size 0x44, payload 0x24.
//
// THE SWITCH IS A JUMP TABLE, NOT A BITMASK. `EAX = SubType ; DEC EAX ; CMP EAX,0x7f ;
// JA default ; MOVZX ECX,byte [EAX + 0x06da73b0] ; JMP [ECX*4 + 0x06da7390]`
// (06da70b5..06da70c9; the decoder's copy is at 06da74b0..06da74c4 with its own tables
// at 06da7818 / 06da77f8). The 0x80-byte index table maps only the eight powers of two
// to a case and everything else, including any COMBINATION of bits, to the default --
// which writes nothing beyond the sub-type byte. So a packet with two sub-type bits set
// travels as a bare header, not as two messages.
//
// The two index tables agree value for value (0x00->0, 0x01->1, 0x03->2, 0x07->3,
// 0x0f->4, and every other byte 7) but differ on the last three: the encoder's table has
// 0x1f->5, 0x3f->6, 0x7f->0 while the decoder's has 0x1f->0, 0x3f->5, 0x7f->6. That is
// only code sharing -- the compiler folded sub-type 0x80 onto sub-type 1's body in the
// encoder and sub-type 0x20 onto it in the decoder -- and the three bodies are the same
// three instructions either way. Not a divergence.
//
// THE MP FLAG IS READ THE OPPOSITE WAY ROUND IN THE TWO DIRECTIONS. This is the one
// thing in the pair that reads as a transcription slip and is not:
//   encoder 06da70ad  TEST AL,AL ; MOV DL,1 ; JNZ +2 ; MOV DL,BL   -> DL =  IsMPServer()
//   decoder 06da74a8  TEST AL,AL ; MOV DL,1 ; JZ  +2 ; MOV DL,BL   -> DL = !IsMPServer()
// One byte apart -- 75 against 74 -- and both bodies then do `TEST DL,DL ; JZ <past the
// reserve>`. So the trailing dword at payload+0x08 is accounted for in the cursor only
// when the ENCODING end is the MP server and only when the DECODING end is not. Between
// a server and a client those two conditions are the same condition, which is what makes
// the pair self-consistent; it is not self-consistent with itself. The dword is written
// by Encode and normally read by Decode (except Decode subtype 0x40, which writes
// the existing packet value into the input). Only the cursor bump, i.e. the bounds check and the reported
// length, is conditional. Transcribed exactly, with the two halves of a field access
// split apart (FRpcMarshalStream::Reserve / PutDwordUncounted / GetDwordUncounted),
// because there is no way to write this with a single Put/Get and stay honest.
//
// ONE MORE RETAIL SLIP, KEPT: the decoder's sub-type 0x40 bounds check stores the
// ENCODER's error code -- 06da7541 `MOV dword ptr [ESI+0xc],0x16` and 06da7552
// `MOV EAX,0x16` -- where all twelve of its other checks store 0x17. It is the only
// 0x16 in the decoder.
//
// FIELD WIDTHS, each read off the instruction rather than off the struct:
//   payload+0x00 SubType       one BYTE out (06da709d), read back ZERO-extended
//                              (06da7493 `XOR EDX,EDX ; MOV DL,CL`).
//   payload+0x04 Field_0x04    one BYTE out (06da7162), read back SIGN-extended
//                              (06da74f0 `MOVSX EDX,CL`). Sub-type 2 only, and sub-type
//                              2 sends nothing else -- not even payload+0x08.
//   payload+0x08 Field_0x08    dword, every sub-type except 2.
//   payload+0x0c Field_0x0c    dword, sub-type 4 only (06da719a / 06da7591).
//   payload+0x10 Field_0x10    one BYTE out (06da70fc), read back NORMALISED to 0/1
//                              (06da7522 `TEST CL,CL ; SETNZ AL`). Sub-type 0x40 only.
//   payload+0x14 EquipArchId   dword, sub-types 8 and 0x10.
//   payload+0x18 EquipCount    dword, sub-types 8 and 0x10.
//   payload+0x1c EquipStatus   FLOAT out (06da7251 `FLD float ptr [EDI+0x3c] ; FSTP
//                              float ptr [EAX]`), a plain dword move back (06da766e).
//                              Sub-type 8 ONLY -- sub-type 0x10 skips it, which is why
//                              its wire record is four bytes shorter.
//   payload+0x20 EquipId       WORD, sub-types 8 and 0x10.
// No quantizer appears anywhere in either body.
// ===========================================================================

// COMMON.DLL::IsMPServer, which retail imports by name and calls at 06da70a7 and
// 06da74a2 (`ff15 0431db06` -> the IAT slot at .rdata:06db3104).
//
// It is already declared for this translation unit -- common_goods.h above includes
// common_net.h, which is where COMMON_API bool __cdecl IsMPServer() lives, and that
// same chain is what makes the symbol exported from our Common.dll at all (the
// definition in src/Common/common_misc.cpp carries no macro of its own and relies on
// the declaration common_goods.h drags in). Restated here only because a reader of this
// codec should not have to trace four headers to find out where the call comes from;
// the signature is common_net.h's, verbatim, so the redeclaration is a no-op.
COMMON_API bool __cdecl IsMPServer();

static const unsigned int kPlayerTradeSubAccept = 0x01;
static const unsigned int kPlayerTradeSubCash = 0x02;
static const unsigned int kPlayerTradeSubOffer = 0x04;
static const unsigned int kPlayerTradeSubAddEquip = 0x08;
static const unsigned int kPlayerTradeSubRemoveEquip = 0x10;
static const unsigned int kPlayerTradeSubClear = 0x20;
static const unsigned int kPlayerTradeSubFlag = 0x40;
static const unsigned int kPlayerTradeSubEnd = 0x80;

// LAB_06da7030 -- PLAYER_TRADE, packet vtable +0x04.
unsigned int RpcEncodeCommonPlayerTrade(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadCommonPlayerTrade* Wire =
        reinterpret_cast<const FPayloadCommonPlayerTrade*>(Packet.Payload());

    // 06da703e..06da704d: Cursor = 0, Limit = Capacity, Ptr = *BufferHolder, Error = 0.
    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    // 06da705b FUN_06da1380 -- the two routing bytes, cursor := 2.
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    // 06da7073..06da70a0: the sub-type goes out as ONE byte.
    if (!Stream.PutByte((unsigned char)(Wire->SubType & 0xffu))) {
        return FRpcMarshalStream::kEncodeOverflow;
    }
    // 06da70a3 `MOV byte ptr [ESP+0x18],BL` clobbers the low byte of the incoming
    // capacity's own stack slot with zero. It is a dead store -- the capacity was read
    // into the stream at 06da7041 and is never read from the stack again -- so there is
    // nothing here to reproduce.
    const bool bAccountTrailer = IsMPServer();       // 06da70a7..06da70b3

    // The trailing dword, shared by every sub-type except 2. Retail reaches it through
    // three different labels (06da7310, 06da731a and the fall-through at 06da711d) that
    // all do the same two things.
    bool bNeedTrailer = true;

    switch (Wire->SubType) {
    case kPlayerTradeSubFlag:                        // 0x40, caseD_40 at 06da70d0
        if (!Stream.PutByte(Wire->Field_0x10)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        break;
    case kPlayerTradeSubCash:                        // 0x02, caseD_2 at 06da7134
        // 06da7139 takes the LOW BYTE of payload+0x04 and 06da7165 jumps straight to the
        // pointer update, past every trailing-dword label. Sub-type 2 sends no more.
        if (!Stream.PutByte((unsigned char)(Wire->Field_0x04 & 0xffu))) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        bNeedTrailer = false;
        break;
    case kPlayerTradeSubOffer:                       // 0x04, caseD_4 at 06da716a
        if (!Stream.PutDword(Wire->Field_0x0c)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        break;
    case kPlayerTradeSubAddEquip:                    // 0x08, caseD_8 at 06da71d4
        if (!Stream.PutDword(Wire->EquipArchId) ||
            !Stream.PutDword(Wire->EquipCount) ||
            !Stream.PutFloat(Wire->EquipStatus) ||   // 06da7251 FLD/FSTP
            !Stream.PutWord(Wire->EquipId)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        break;
    case kPlayerTradeSubRemoveEquip:                 // 0x10, caseD_10 at 06da72ac
        // No status float on this branch -- 06da7303 goes straight from EquipCount to
        // the shared word writer at 06da7259.
        if (!Stream.PutDword(Wire->EquipArchId) ||
            !Stream.PutDword(Wire->EquipCount) ||
            !Stream.PutWord(Wire->EquipId)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        break;
    case kPlayerTradeSubAccept:                      // 0x01 / 0x80, caseD_1 at 06da71a6
    case kPlayerTradeSubEnd:
    case kPlayerTradeSubClear:                       // 0x20, caseD_20 at 06da7106
        // These three ARE the unconditional reserve: their bodies are the four
        // instructions that bump the cursor by 4, with no IsMPServer test in front.
        Stream.Reserve(4u, FRpcMarshalStream::kEncodeOverflow);
        if (Stream.Failed()) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        if (!Stream.PutDwordUncounted(Wire->Field_0x08)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        bNeedTrailer = false;
        break;
    default:
        // caseD_3: everything that is not one of the eight, including any combination
        // of bits, falls straight through to the trailer hook.
        bNeedTrailer = false;
        break;
    }

    if (bNeedTrailer) {
        // 06da7104 / 06da71a4 / 06da7282 -- `TEST DL,DL ; JZ <past the reserve>`.
        if (bAccountTrailer) {
            Stream.Reserve(4u, FRpcMarshalStream::kEncodeOverflow);
        }
        if (Stream.Failed()) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        if (!Stream.PutDwordUncounted(Wire->Field_0x08)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
    }

    // 06da7330 FUN_06da13c0 -- the no-op trailer hook.
    *OutLength = Stream.Length();                // 06da7348..06da734e
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    // TODO, shared with every other codec in this file: 06da7354 also updates the
    // per-type statistics record through FUN_06da16b0.
    return 0;                                        // 06da7389
}

// LAB_06da7430 -- PLAYER_TRADE, packet vtable +0x08.
unsigned int RpcDecodeCommonPlayerTrade(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength) {
    FPayloadCommonPlayerTrade* Wire =
        reinterpret_cast<FPayloadCommonPlayerTrade*>(Packet.Payload());

    // 06da743e..06da7453: Limit = Size, Ptr = Buffer taken WITHOUT a dereference.
    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06da7456 FUN_06da13c0 -- the no-op header hook.

    unsigned char SubTypeByte = 0;
    if (!Stream.GetByte(SubTypeByte)) {              // 06da746e..06da7498
        return FRpcMarshalStream::kDecodeOverrun;
    }
    // 06da7493 `XOR EDX,EDX ; MOV DL,CL` -- zero-extended, unlike the sub-type 2 field
    // eight instructions later.
    Wire->SubType = (unsigned int)SubTypeByte;
    // 06da749b clobbers the low byte of the incoming buffer pointer's stack slot; dead,
    // for the same reason as the encoder's.
    const bool bAccountTrailer = !IsMPServer();      // 06da74a2..06da74ae, INVERTED

    bool bNeedTrailer = true;

    switch (Wire->SubType) {
    case kPlayerTradeSubCash: {                      // 0x02, caseD_2 at 06da74cb
        unsigned char Value = 0;
        if (!Stream.GetByte(Value)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        // 06da74f0 MOVSX -- signed, where the sub-type byte above was not.
        Wire->Field_0x04 = (unsigned int)(int)(signed char)Value;
        bNeedTrailer = false;
        break;
    }
    case kPlayerTradeSubFlag: {                      // 0x40, caseD_40 at 06da74fc
        unsigned char Value = 0;
        if (!Stream.GetByte(Value)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        // 06da7522 `TEST CL,CL ; SETNZ AL` -- any non-zero byte comes back as 1.
        Wire->Field_0x10 = (unsigned char)(Value != 0 ? 1 : 0);
        // 06da752f..06da7558: the ONE place in this decoder that uses the encoder's
        // error code. Reproduced, not corrected.
        if (bAccountTrailer) {
            Stream.Reserve(4u, FRpcMarshalStream::kEncodeOverflow);
        }
        if (Stream.Failed()) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        // 06da755b..06da7561 write the EXISTING packet target into the input.
        // The pointer advances four bytes at 06da75c3 even when MP omitted Reserve.
        if (!Stream.PutDwordUncounted(Wire->Field_0x08)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
        bNeedTrailer = false;
        break;
    }
    case kPlayerTradeSubOffer:                       // 0x04, caseD_4 at 06da7565
        if (!Stream.GetDword(Wire->Field_0x0c)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        break;
    case kPlayerTradeSubAddEquip: {                  // 0x08, caseD_8 at 06da75ee
        unsigned int Status = 0;
        if (!Stream.GetDword(Wire->EquipArchId) ||
            !Stream.GetDword(Wire->EquipCount) ||
            // 06da766e `MOV EBX,[EAX] ; MOV [EDI+0x3c],EBX` -- a dword move, not a
            // conversion, so the float's bits come back untouched.
            !Stream.GetDword(Status) ||
            !Stream.GetWord(Wire->EquipId)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        memcpy(&Wire->EquipStatus, &Status, sizeof(float));
        break;
    }
    case kPlayerTradeSubRemoveEquip:                 // 0x10, caseD_10 at 06da76c9
        if (!Stream.GetDword(Wire->EquipArchId) ||
            !Stream.GetDword(Wire->EquipCount) ||
            !Stream.GetWord(Wire->EquipId)) {
            return FRpcMarshalStream::kDecodeOverrun;
        }
        break;
    case kPlayerTradeSubAccept:                      // 0x01 / 0x20, caseD_1 at 06da777a
    case kPlayerTradeSubClear:
    case kPlayerTradeSubEnd:                         // 0x80, caseD_80 at 06da7599
        Stream.Reserve(4u, FRpcMarshalStream::kDecodeOverrun);
        if (Stream.Failed()) {
            return Stream.GetError();
        }
        if (!Stream.GetDwordUncounted(Wire->Field_0x08)) {
            return Stream.GetError();
        }
        bNeedTrailer = false;
        break;
    default:                                         // caseD_3 at 06da75c9
        bNeedTrailer = false;
        break;
    }

    if (bNeedTrailer) {
        // 06da7597 / 06da76a7 / 06da7758, and LAB_06da75b0 behind all three.
        if (bAccountTrailer) {
            Stream.Reserve(4u, FRpcMarshalStream::kDecodeOverrun);
        }
        if (Stream.Failed()) {
            return Stream.GetError();
        }
        if (!Stream.GetDwordUncounted(Wire->Field_0x08)) {
            return Stream.GetError();
        }
    }

    // 06da75d2 FUN_06da13c0 again.
    *OutLength = Stream.Length();                // 06da77a5..06da77ab
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;                                        // 06da77ed
}

// FUN_06d9bad0, the PLAYER_TRADE constructor. Retail zeroes packet+0x20..+0x3c with
// two exceptions: 06d9bafa writes 0xffffffff to packet+0x34 -- payload+0x14,
// EquipArchId, so the default archetype is "none", not 0 -- and 06d9bb0a writes
// payload+0x10 as a BYTE with CL, which is zero at that point (06d9bac7 XOR ECX,ECX),
// so it is a zero store and not a default. The 0xffffffff is now a store in
// FPacket_FLPACKET_COMMON_PLAYER_TRADE's constructor in
// rpc_packet_registry.cpp; this type no longer needs a factory of its own.

// ===========================================================================
// ch2 0x05 FLPACKET_SERVER_DAMAGEOBJECT -- LAB_06d90480 / LAB_06d906c0
//
// Constructor FUN_06d9c8d0:
//   06d9c93b  MOV dword ptr [ESI],.rdata:PTR_FUN_06db3cb0
//   06d9c941  MOV dword ptr [ESI + 0xc],0x20005
//   vtable 06db3cb0 = { dtor FUN_06d909b0, Encode LAB_06d90480, Decode LAB_06d906c0,
//                       IServerImpl::GetServerStats }
// Each slot's XREF header names exactly one .rdata address, 06db3cb4 and 06db3cb8; the
// vtable's own second reference, 06d90235, is inside the destructor. One type, one pair.
//
// THE CONSTRUCTOR CONFIRMS THE PAYLOAD rpc_payloads.h ALREADY HAD, list and all:
//   06d9c8fe  packet+0x1c = 0                 (the marshal error, not payload)
//   06d9c915  MOV byte ptr [ESI+0x24],AL      payload+0x04 -- ONE BYTE, the VC6
//                                             std::allocator copied out of an
//                                             uninitialised stack temporary. Not a field.
//   06d9c918  operator new(0x14) -> self-linked dummy node -> packet+0x28 (payload+0x08)
//   06d9c929  packet+0x2c = 0                 payload+0x0c  the list's Size
//   06d9c92c  MOV byte ptr [ESI+0x30],BL      payload+0x10  a real byte field
//   06d9c92f/32/35 packet+0x34/+0x38/+0x3c = 0  payload+0x14/+0x18/+0x1c
// i.e. a DamageList in place at payload+0x04 -- which is what FPayloadServerDamageObject
// holds now: Common.dll's own DamageList, so the fields below are its named members
// (+0x0c bDestroyed, +0x10 Cause, +0x14 InflictorId, +0x18 InflictorOwner).
//
// Wire layout, and THE ORDER IS NOT THE STRUCT ORDER -- the list's count travels FOURTH,
// after three fields that sit BEHIND it in memory:
//     dword  payload+0x00                       06d904f1 / 06d9072b
//     byte   payload+0x10  Damage.Field_0x0c    06d9051e / 06d90751
//     dword  payload+0x14  Damage.Field_0x10    06d9054b / 06d90777
//     dword  payload+0x18  Damage.Field_0x14    06d9057a / 06d9079f
//     word   payload+0x0c  Damage.Entries.Size  06d905ab / 06d907c7
//     per entry:
//       word   Value.Field_0x00 (low half)      06d905e8 / 06d9084b
//       byte   Value.Field_0x08 (low byte)      06d9060c / 06d90875
//       float  Value.Field_0x04                 06d90632 / 06d9089c
//
// PAYLOAD+0x1C NEVER TRAVELS. Neither slot touches packet+0x3c. It is a field the
// constructor zeroes and the wire ignores, not an omission here.
//
// THE PER-ENTRY ORDER IS A TRAP THE LISTING SETTLES AND THE DECOMPILER WOULD NOT. The
// encoder LOADS all three fields before it writes any of them -- 06d905c4
// `FLD float ptr [EAX + 0xc]`, 06d905c7 `MOV BP,word ptr [EAX + 0x8]`, 06d905cb
// `MOV DL,byte ptr [EAX + 0x10]` -- so the float is read FIRST and written LAST. Reading
// the loads as the wire order would have put the float in front of the word and the
// byte. The proof that the float really is still on the x87 stack across the two bounds
// checks is the error exit itself: LAB_06d90662 begins `POP EDI ; FSTP ST0`, a pop of a
// register the codec does not need, to unload the float the checks jumped over.
//
// The decoder reads them back word, byte, float (06d9084d, 06d90877, 06d9089e) into a
// stack temporary and only then allocates the node -- the opposite of the equipment
// decoder, which allocates first and leaks the record on a short buffer. This one leaks
// nothing.
//
// THE PADDING HALF OF THE FIRST DWORD. 06d90810..06d90831 seed
// that temporary with COMMON.DLL::SUBOBJ_ID_NONE, 0xbf800000 (-1.0f) and 0 -- and the
// first of those is a WORD store into a DWORD slot (06d9081d `MOV word ptr [ESP+0x18],CX`),
// as is the decoded value that replaces it (06d908a0). The upper half of that dword is
// never written, and 06d908f0 copies the whole dword into the node. Retail therefore
// stores stack residue in the top half of DamageEntry's first dword on every element.
// Those two bytes are padding behind the ushort SubObjId of common_misc.h's DamageEntry;
// the port's DamageEntry is built by its own ctor and nothing reads that half.
//
// THE DECODER APPENDS. It never clears the list the packet already holds, and the count
// it just read is used only as the loop bound (06d90909 `CMP EAX,ECX` / 06d90912 `JC`,
// unsigned) -- the size at payload+0x0c is incremented per element (06d908fc/06d9090b),
// not assigned.
// ===========================================================================

unsigned int RpcEncodeServerDamageObject(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerDamageObject* Wire =
        reinterpret_cast<const FPayloadServerDamageObject*>(Packet.Payload());
    const DamageList& Damage = Wire->Damage;

    // 06d90491/06d90496: Limit = arg2, Ptr = *arg1.
    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06d904b1 FUN_06da1380
    if (!Stream.PutDword(Wire->Field_0x00)) {                      // 06d904c9..06d904fb
        return FRpcMarshalStream::kEncodeOverflow;
    }
    // 06d9051e: DamageList+0x0c, moved as a byte.
    if (!Stream.PutByte(Damage.bDestroyed ? 1u : 0u)) {            // 06d904fd..06d90526
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutDword(static_cast<unsigned int>(Damage.Cause))) {  // 06d90528..06d90555
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutDword(Damage.InflictorId)) {                    // 06d90557..06d90588
        return FRpcMarshalStream::kEncodeOverflow;
    }
    // 06d90581 `MOV CX,word ptr [EBX+0x2c]` -- only the LOW WORD of the list's _Size.
    const unsigned int Count = static_cast<unsigned int>(Damage.Entries.size());
    if (!Stream.PutWord((unsigned short)(Count & 0xffffu))) {
        return FRpcMarshalStream::kEncodeOverflow;                 // 06d905a4
    }
    // 06d905b8/06d905bc: the list the packet ctor built, walked untested; an empty list
    // skips the loop.
    for (std::list<DamageEntry>::const_iterator It = Damage.Entries.begin();
         It != Damage.Entries.end(); ++It) {                       // 06d90639/06d9063b
        if (!Stream.PutWord(It->SubObjId)) {
            return FRpcMarshalStream::kEncodeOverflow;         // 06d905e8, LAB_06d90662
        }
        // 06d905cb `MOV DL,byte ptr [EAX + 0x10]` -- the low byte of the fate.
        if (!Stream.PutByte((unsigned char)(static_cast<unsigned int>(It->Fate) & 0xffu))) {
            return FRpcMarshalStream::kEncodeOverflow;         // 06d9060c
        }
        // 06d90632 FSTP -- the float loaded back at 06d905c4, four bytes either way.
        if (!Stream.PutFloat(It->HitPts)) {
            return FRpcMarshalStream::kEncodeOverflow;
        }
    }
    // 06d9064d FUN_06da13c0 -- nothing more to emit.
    *OutLength = Stream.Length();                              // 06d90670..06d90676
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());      // 06d90678..06d906b1
    return 0;
}

unsigned int RpcDecodeServerDamageObject(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength) {
    FPayloadServerDamageObject* Wire =
        reinterpret_cast<FPayloadServerDamageObject*>(Packet.Payload());
    DamageList& Damage = Wire->Damage;

    // 06d906da/06d906dd: Limit = arg2, Ptr = arg1 directly.
    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06d906ed FUN_06da13c0 -- no header consumed.

    if (!Stream.GetDword(Wire->Field_0x00)) {                      // 06d90708..06d90734
        return Stream.GetError();                                  // 06d90976
    }
    unsigned char Destroyed = 0;
    if (!Stream.GetByte(Destroyed)) {                              // 06d90737..06d90758
        return Stream.GetError();
    }
    Damage.bDestroyed = (Destroyed != 0);
    unsigned int Cause = 0;
    if (!Stream.GetDword(Cause)) {                                 // 06d9075b..06d90780
        return Stream.GetError();
    }
    Damage.Cause = static_cast<DamageCause>(Cause);
    if (!Stream.GetDword(Damage.InflictorId)) {                    // 06d90783..06d907a8
        return Stream.GetError();
    }
    unsigned short Count = 0;
    if (!Stream.GetWord(Count)) {                                  // 06d907ab..06d907cf
        return Stream.GetError();
    }
    // 06d907de/06d907e4 `TEST EAX,EAX ; JA` on the zero-extended word.
    for (unsigned int Index = 0; Index < (unsigned int)Count; ++Index) { // 06d90908..06d90912
        // 06d90810..06d90831 seed the stack temporary with SUBOBJ_ID_NONE, -1.0f and 0;
        // all three are overwritten below before the node is made.
        DamageEntry Entry;
        if (!Stream.GetWord(Entry.SubObjId)) {                     // 06d9084b..06d90853
            return Stream.GetError();                              // 06d90921
        }
        unsigned char Fate = 0;
        if (!Stream.GetByte(Fate)) {                               // 06d90875..06d9087a
            return Stream.GetError();
        }
        Entry.Fate = static_cast<DamageEntry::SubObjFate>(Fate);
        if (!Stream.GetFloat(Entry.HitPts)) {                      // 06d9089c FLD/FSTP
            return Stream.GetError();
        }
        // 06d908b2..06d908df: the node is allocated and spliced only AFTER all three
        // fields are safely in hand, so a short buffer leaks nothing here. The list is
        // APPENDED to, never cleared, and its size is incremented per element
        // (06d908fc/06d9090b) -- which is also the port's DamageList::Count.
        Damage.Entries.push_back(Entry);
        ++Damage.Count;
    }
    // 06d907eb FUN_06da13c0 -- nothing to consume.
    *OutLength = Stream.Length();                              // 06d9092e..06d90934
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());      // 06d90936..06d9096f
    return 0;
}

// ===========================================================================
// The flattened FmtStr (ch1 0x1b, ch2 0x29, 0x34, 0x35, 0x45)
// ===========================================================================

unsigned int RpcFlattenedFmtStrSize(const FmtStr& Source) {
    unsigned int Total = 6;
    for (const FmtStr::Val* Node = Source.Head; Node != nullptr; Node = Node->Next) {
        Total += Node->get_flattened_size();
    }
    return Total;
}

// Every retail site stores the size, calls a bare operator new(size) and hands the
// result straight to FmtStr::flatten (06d88de7, 06d88e26, 06d89339, 06d89663, 06d8ae14,
// 06d8afd2, 06d8b0a3, 06d8b1a3, 06d8b702 ...) -- no test between the two.
void* RpcFlattenFmtStr(const FmtStr& Source, unsigned int& OutSize) {
    const unsigned int Size = RpcFlattenedFmtStrSize(Source);
    OutSize = Size;
    void* Block = ::operator new(Size, std::nothrow);
    Source.flatten(Block, Size);
    return Block;
}

__declspec(dllexport) void RpcUnflattenFmtStr(FmtStr& Dest, void* Buffer, unsigned int Size) {
    Dest.unflatten(Buffer, Size);
}


// The two CHARACTERINFO list destructors (FUN_06d852e0 / FUN_06d85340) are
// ~MetaList / ~OwnerList of OwnerList<CCharacterSet::CharacterInfo>
// (src/include/common_metalist.h + server_characterset.h).
