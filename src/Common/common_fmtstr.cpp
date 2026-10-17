// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FmtStr, FmtStr::Val and the mission-log pack helpers -- split out of common_text.cpp in
// CMN-SPLIT phase B.

#include "common_text.h"
#include "dacom.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <map>
#include <string>
#include <deque>
#include <list>
#include <new>
#include <vector>
#include <windows.h>

void __cdecl AppendMissionLogData(FmtStr const* Fmt, unsigned char*& Buffer, int& Offset, int MaxSize)
{
    int Result = Fmt->flatten(Buffer, MaxSize);
    Offset += Result;
    Buffer += Result;
}

void __cdecl ExtractMissionLogData(FmtStr* Fmt, unsigned char*& Buffer, int& Remaining)
{
    int Result = Fmt->unflatten(Buffer, 0);
    Remaining -= Result;
    Buffer += Result;
}

int __cdecl PackMissionLogData(int Type, FmtStr const* Fmt, unsigned char* Buffer, int MaxSize)
{
    *(int*)Buffer = Type;
    if (Type == 0)
    {
        *(int*)(Buffer + 4) = 0;
        return 8;
    }
    if (Type == 1)
    {
        *(int*)(Buffer + 4) = 1;
        int Result = Fmt->flatten(Buffer + 8, MaxSize);
        return Result + 8;
    }
    return 8;
}

// ============================================================================
// FmtStr — linked-list formatted string. Complex serialization logic.
// All methods fully RE'd from Ghidra: wire format, append_* with type tags,
// deep copy, comparison, flatten/unflatten.
// ============================================================================

FmtStr::Val::Val(unsigned short InType, void* InData, unsigned char InFlags)
{
    // RVA 0xAD570. The exported signature takes the payload as void*; retail stores the dword
    // as-is into +0x04, whatever the tag.
    Next = nullptr;
    Value = (unsigned int)(std::uintptr_t)InData;
    TypeTag = InType;
    Flags = InFlags;
}

FmtStr::Val::Val(void const* Src, int& OutConsumed)
{
    // RVA 0xAD660 (Common.dll.c 150131-150143). Build directly from a serialized Val, reporting
    // the number of bytes InitFromVoid consumed.
    Next = nullptr;
    OutConsumed = InitFromVoid(Src);
}

FmtStr::Val::Val(void const* Src)
{
    // RVA 0xAD680 (Common.dll.c 150147-150156). Same, but the caller doesn't need the count.
    Next = nullptr;
    InitFromVoid(Src);
}

FmtStr::Val::Val(const Val& Other)
{
    // RVA 0xAD6A0 (Common.dll.c 150160-150167). Retail forwards straight to operator=, which
    // deep-copies 0x21/0x4E payloads -- NOT a memberwise copy (see operator= above).
    *this = Other;
}

FmtStr::Val::~Val()
{
    // RVA 0xAD510 (Common.dll.c 150026-150053). Tag 0x21: walk Subtree->Head, ~Val() + free()
    // each child, zero Subtree->Head and free the subtree object itself (FUN_06391d5a, the
    // module's free() wrapper). Tag 0x4E: free the block.
    if (TypeTag == 0x21)
    {
        if (Subtree != nullptr)
        {
            Val* Child = Subtree->Head;
            while (Child != nullptr)
            {
                Val* NextChild = Child->Next;
                delete Child;
                Child = NextChild;
            }
            Subtree->Head = nullptr;
            // Retail calls operator delete directly; ~FmtStr on the emptied subtree does nothing.
            delete Subtree;
        }
    }
    else if (TypeTag == 0x4E)
    {
        // 0630d51f: FUN_06391d5a is the module's operator delete (PUSH; CALL free), the pair of
        // the operator new(0x14) that allocated the block; no null test, no store afterwards.
        delete Marker;
    }
}

FmtStr::Val const& FmtStr::Val::operator=(const Val& Other)
{
    // RVA 0xAD6C0 (Common.dll.c 150174-150219, `??4Val@FmtStr@@...`). BUG FIX: this was a
    // naive shallow copy (Next/payload/TypeTag/Flags), which for TypeTag 0x21/0x4E leaves two
    // Val nodes pointing at the SAME heap block -- both destructors (~Val, TypeTag-aware) free
    // it, a double-free/use-after-free. Retail always copies Next/TypeTag/Flags shallowly (a
    // pointer or scalar has no owned payload) but for 0x21/0x4E it allocates and deep-copies a
    // fresh subtree/block, exactly like CopyFmtStrChain() (FUN_0630de20) does when building a new node
    // from scratch. Retail has no self-assignment guard here; matched (self-assign of a Val is
    // not something append_*/operator= on FmtStr ever does).
    Next = Other.Next;
    Value = Other.Value; // the raw payload dword, whatever the tag
    TypeTag = Other.TypeTag;
    Flags = Other.Flags;

    if (TypeTag == 0x21)
    {
        // 0630d71a-0630d74b: VC6 operator new returns null on failure (hence nothrow); a null
        // subtree skips the copy and the node keeps a null payload.
        FmtStr* const NewSub = new (std::nothrow) FmtStr();
        if (NewSub != nullptr)
            *NewSub = *Other.Subtree;
        Subtree = NewSub;
    }
    else if (TypeTag == 0x4E)
    {
        // 0630d6ff-0630d716: operator new(0x14) (VC6: null on failure, hence nothrow), then the
        // 5 dwords are copied with no null test of the new block.
        NavMarker* const NewBlock = new (std::nothrow) NavMarker;
        *NewBlock = *Other.Marker;
        Marker = NewBlock;
    }
    return *this;
}

bool FmtStr::Val::operator==(const Val& Other) const
{
    // RVA 0xAD770
    if (TypeTag != Other.TypeTag)
    {
        return false;
    }
    if (TypeTag == 0x4E)
    {
        // BUG FIX: this previously ran the float-epsilon compare over all 5 dwords. Retail
        // (Common.dll.c ~150238-150253, `?operator==@Val@FmtStr@@...`) only does that for
        // dwords[2..4] (the NavMarker's position, presumably); dwords[0..1] (presumably two
        // integer ids) are compared with plain integer `!=`, no epsilon at all.
        // 0630d790-0630d7ea: both payloads dereferenced without a null test.
        unsigned int const* ThisData = Marker->Data;
        unsigned int const* OtherData = Other.Marker->Data;
        if (ThisData[0] != OtherData[0] || ThisData[1] != OtherData[1])
        {
            return false;
        }
        for (int i = 2; i < 5; i++)
        {
            // FLD float / FSUB float / FMUL: the square stays unrounded on the FPU (double) and
            // is FCOMPed against the float 0.01f (063a35dc, 0x3c23d70b); TEST AH,0x41 + JP
            // leaves for "greater" and for "unordered", so a NaN component is unequal too.
            // The dwords hold float bit patterns (FLD float ptr), so the bits are reinterpreted.
            float FA = *(float const*)&ThisData[i];
            float FB = *(float const*)&OtherData[i];
            double const Delta = (double)FA - (double)FB;
            double const Diff = Delta * Delta;
            if (!(Diff <= 0.01f))
            {
                return false;
            }
        }
        return true;
    }
    if (TypeTag == 0x21)
    {
        // Compare nested FmtStr subtrees: first check MadLibId, then walk both child lists.
        // BUG FIX: retail (Common.dll.c:150257, `piVar2[1] == (*(param_1+4))[1]`) compares the
        // subtree's field at +4 -- that's `FmtStr::MadLibId` in our now-complete layout, not a
        // mystery "count field" (the header used to be Head-pointer-only when this comment was
        // written). Skipping it meant two subtrees with different mad-lib ids but identical
        // value lists compared equal.
        // 0630d7f9-0630d802: both subtrees dereferenced without a null test.
        FmtStr const* ThisSub = Subtree;
        FmtStr const* OtherSub = Other.Subtree;
        if (ThisSub->MadLibId != OtherSub->MadLibId)
        {
            return false;
        }
        Val* A = ThisSub->Head;
        Val* B = OtherSub->Head;
        while (A != nullptr && B != nullptr)
        {
            if (!(*A == *B))
            {
                return false;
            }
            A = A->Next;
            B = B->Next;
        }
        return (A == nullptr && B == nullptr);
    }
    // Default: compare the values themselves
    return Value == Other.Value;
}

void* FmtStr::Val::operator new(size_t Size)
{
    // RVA 0x9D560
    return malloc(Size);
}

void FmtStr::Val::operator delete(void* Ptr)
{
    // RVA 0x9D570
    free(Ptr);
}

int FmtStr::Val::InitFromVoid(const void* Src)
{
    // RVA 0xAD5A0 — deserializes a Val from raw bytes
    const unsigned char* P = (const unsigned char*)Src;
    unsigned short Tag = *(const unsigned short*)P;
    TypeTag = Tag;
    Flags = P[2];

    if (Tag == 0x21)
    {
        // Nested FmtStr subtree: allocate and unflatten. BUG FIX: retail (Common.dll.c:150111
        // -150112) returns `unflatten(...) + 3` directly -- the actual number of bytes
        // unflatten consumed, plus this node's own 3-byte tag+flags header. This previously
        // recomputed the size from scratch via get_flattened_size() instead of trusting
        // unflatten's own return value; the two happen to agree in the normal case, but
        // diverge if unflatten ever bails out early (e.g. a malloc failure partway through
        // the child list), so match retail exactly.
        // 0630d5f7-0630d644: VC6 operator new returns null on failure (hence nothrow); retail
        // then calls unflatten on the null subtree, which faults on its first read -- so does
        // the port (no guard).
        FmtStr* Sub = new (std::nothrow) FmtStr();
        Subtree = Sub;
        int const Consumed = Sub->unflatten((void*)(P + 3), 0);
        return Consumed + 3;
    }
    if (Tag == 0x4E)
    {
        // Data block: 5 dwords (0x14 bytes)
        // 0630d5d2-0630d5e5: operator new(0x14) (VC6: null on failure, hence nothrow); the
        // block is filled with no null test.
        NavMarker* Block = new (std::nothrow) NavMarker;
        const unsigned int* SrcBlock = (const unsigned int*)(P + 3);
        for (int i = 0; i < 5; i++)
        {
            Block->Data[i] = SrcBlock[i];
        }
        Marker = Block;
        return 0x17;
    }
    // Simple value: 4 bytes
    Value = *(unsigned int const*)(P + 3);
    return 7;
}

unsigned int FmtStr::Val::flatten(void* Dest, unsigned int MaxSize) const
{
    // RVA 0xAD460 — serializes this Val node into a buffer
    unsigned char* P = (unsigned char*)Dest;
    *(unsigned short*)P = TypeTag;
    P[2] = Flags;

    if (TypeTag == 0x21)
    {
        // BUG FIX x2 (Common.dll.c 149990-150006, `?flatten@Val@FmtStr@@...`):
        // (1) P+3 must hold the nested subtree's OWN MadLibId (`puVar5[1]`, i.e. Sub+0x04) --
        //     this previously wrote NodeCount there again (duplicating the P+7 field), likely
        //     written before FmtStr grew a real MadLibId field at +4.
        // (2) The header (tag+flags+subMadLibId+childcount = 9 bytes, P..P+9) already accounts
        //     for the leading 3-byte tag+flags -- retail returns `(param_2-uVar4)+3`, which is
        //     exactly `Cur-P` (9+children), not `(Cur-P)+3`. The extra +3 this previously added
        //     made every 0x21 node report 3 bytes larger than it actually wrote.
        // 0630d4aa-0630d504: the subtree is dereferenced without a null test.
        FmtStr const* Sub = Subtree;
        *(unsigned int*)(P + 3) = Sub->MadLibId;
        unsigned int NodeCount = 0;
        for (Val* V = Sub->Head; V != nullptr; V = V->Next)
        {
            NodeCount++;
        }
        unsigned short ChildCount = (unsigned short)NodeCount;
        *(unsigned short*)(P + 7) = ChildCount;
        // Recursively flatten each child starting at P+9
        unsigned char* Cur = P + 9;
        unsigned int Remaining = MaxSize - 6;
        for (Val* V = Sub->Head; V != nullptr; V = V->Next)
        {
            unsigned int Written = V->flatten(Cur, Remaining);
            Cur += Written;
            Remaining -= Written;
        }
        return (unsigned int)(Cur - P);
    }
    if (TypeTag == 0x4E)
    {
        // Copy 5 dwords from Data block
        unsigned int const* SrcBlock = Marker->Data;
        unsigned int* DestBlock = (unsigned int*)(P + 3);
        for (int i = 0; i < 5; i++)
        {
            DestBlock[i] = SrcBlock[i];
        }
        return 0x17;
    }
    // Simple value: 4 bytes
    *(unsigned int*)(P + 3) = Value;
    return 7;
}

unsigned int FmtStr::Val::get_flattened_size() const
{
    // RVA 0x3A20
    if (TypeTag != 0x21)
    {
        if (TypeTag == 0x4E)
        {
            return 0x17;
        }
        return 7;
    }
    // Type 0x21: nested FmtStr subtree, dereferenced without a null test (06263a3b).
    unsigned int Total = 6;
    for (Val* V = Subtree->Head; V != nullptr; V = V->Next)
    {
        Total += V->get_flattened_size();
    }
    return Total + 3;
}

namespace
{
    // The tail link every append_* inlines (0630d9b8-0630d9c8 and friends): walk to the last
    // node and store the new one there, or into Head when the list is empty.
    void LinkFmtStrTail(FmtStr& Str, FmtStr::Val* Node)
    {
        FmtStr::Val** Link = &Str.Head;
        while (*Link != nullptr)
            Link = &(*Link)->Next;
        *Link = Node;
    }

    // The simple append_* bodies (0630d980 system ... 0630dd50 loot): a 0x0c-byte node from the
    // CRT malloc; when that fails nothing happens (the counter does not move either). Otherwise
    // Flags = the kind's counter (post-incremented), Next = 0, Value = the value, the tag, and
    // the node goes to the tail.
    void AppendValueNode(FmtStr& Str, unsigned short Tag, unsigned int Value, unsigned char& Counter)
    {
        FmtStr::Val* const Node = (FmtStr::Val*)malloc(sizeof(FmtStr::Val));
        if (Node == nullptr)
            return;
        Node->Flags = Counter;
        ++Counter;
        Node->Value = Value;
        Node->Next = nullptr;
        Node->TypeTag = Tag;
        LinkFmtStrTail(Str, Node);
    }

    // FUN_0630de20 (operator= and the (id, list) ctor): free Dest's chain (destroy() inlined),
    // Head = 0, then copy Src node by node. Each node is a CRT malloc of 0x0c bytes taking the
    // source's three dwords; tag 0x4E gets a fresh 0x14-byte block filled without a null test,
    // tag 0x21 a new FmtStr (null on failure, else Head = 0 and operator=) -- the source payload
    // is not null-tested either. A failed node malloc stores 0 into the current link and the
    // next node's link becomes that null node (0630df27-0630df3d), exactly as retail.
    void CopyFmtStrChain(FmtStr& Dest, FmtStr::Val const* Src)
    {
        Dest.destroy();
        FmtStr::Val** Link = &Dest.Head;
        for (FmtStr::Val const* Cur = Src; Cur != nullptr; Cur = Cur->Next)
        {
            FmtStr::Val* const Node = (FmtStr::Val*)malloc(sizeof(FmtStr::Val));
            if (Node != nullptr)
            {
                Node->Next = Cur->Next;
                Node->Value = Cur->Value; // the raw payload slot, whatever the tag
                Node->TypeTag = Cur->TypeTag;
                Node->Flags = Cur->Flags;
                if (Node->TypeTag == 0x21)
                {
                    // 0630def1-0630df22: VC6 operator new returns null on failure (hence
                    // nothrow); a null subtree skips the copy and leaves a null payload.
                    FmtStr* const Sub = new (std::nothrow) FmtStr();
                    if (Sub != nullptr)
                        *Sub = *Cur->Subtree;
                    Node->Subtree = Sub;
                }
                else if (Node->TypeTag == 0x4E)
                {
                    // 0630ded6: operator new(0x14), filled without a null test.
                    FmtStr::NavMarker* const Block = new (std::nothrow) FmtStr::NavMarker;
                    *Block = *Cur->Marker;
                    Node->Marker = Block;
                }
            }
            *Link = Node;
            Link = (FmtStr::Val**)Node; // &Node->Next (Next is at +0); null after a failed malloc
        }
    }
}

// FmtStr::FmtStr() is inline in common_text.h and not exported -- see the note on the struct.

FmtStr::FmtStr(const FmtStr& Other)
{
    // RVA 0xAE1F0. Retail zeroes only the head pointer and then runs operator=, which copies
    // the id and all fourteen counters.
    Head = nullptr;
    MadLibId = 0;
    memset(ValueIndex, 0, sizeof(ValueIndex));
    *this = Other;
}

FmtStr::FmtStr(unsigned int InMadLibId, const Val* ValueList)
{
    // RVA 0xAE2D0. The first argument is the MAD-LIB ID, not a node count, and the second is
    // the head of a node chain to deep-copy -- retail hands both to the same helper operator=
    // uses (FUN_0630de20). The port used to read them as (count, array).
    // 0630e2d0-0630e333: Head and the fourteen counters zeroed, the id stored, FUN_0630de20.
    Head = nullptr;
    memset(ValueIndex, 0, sizeof(ValueIndex));
    MadLibId = InMadLibId;
    CopyFmtStrChain(*this, ValueList);
}

FmtStr::~FmtStr()
{
    // RVA 0xAE210 — walk list, free nodes, recurse into subtrees
    destroy();
}

const FmtStr& FmtStr::operator=(const FmtStr& Other)
{
    // RVA 0xADDA0. Copies the mad-lib id, deep-copies the node chain, then copies all fourteen
    // counters one by one (0630ddb8..0630de09). Retail returns a CONST reference.
    // Retail has no self test: self-assignment frees the chain FUN_0630de20 is about to copy
    // (use after free, undefined behaviour). The port keeps the chain instead.
    if (this != &Other)
    {
        MadLibId = Other.MadLibId;
        CopyFmtStrChain(*this, Other.Head);
        memcpy(ValueIndex, Other.ValueIndex, sizeof(ValueIndex));
    }
    return *this;
}

bool FmtStr::operator==(const FmtStr& Other) const
{
    // RVA 0xAE060. BUG FIX: retail (Common.dll.c:151057) compares MadLibId FIRST and returns
    // false immediately on a mismatch, before ever looking at the node lists -- this previously
    // skipped that check entirely and would call two FmtStr with different mad-lib ids (but
    // coincidentally identical value lists) equal.
    if (MadLibId != Other.MadLibId)
    {
        return false;
    }
    Val* A = Head;
    Val* B = Other.Head;
    while (A != nullptr && B != nullptr)
    {
        if (!(*A == *B))
        {
            return false;
        }
        A = A->Next;
        B = B->Next;
    }
    return (A == nullptr && B == nullptr);
}

void FmtStr::destroy()
{
    // RVA 0xAE270 — identical to dtor body. Retail leaves the counters and the mad-lib id
    // alone here; only the node chain goes.
    Val* Current = Head;
    while (Current != nullptr)
    {
        Val* Next = Current->Next;
        if (Current->TypeTag == 0x21 && Current->Subtree != nullptr)
        {
            FmtStr* SubTree = Current->Subtree;
            SubTree->destroy();
            delete SubTree;
        }
        else if (Current->TypeTag == 0x4E)
        {
            delete Current->Marker; // FUN_06391d5a, operator delete; no null test
        }
        free(Current);
        Current = Next;
    }
    Head = nullptr;
}

void FmtStr::begin_mad_lib(unsigned int Id)
{
    // RVA 0xA9B30 — a one-line setter the linker folded together with a dozen others.
    MadLibId = Id;
}

void FmtStr::end_mad_lib()
{
    // RVA 0x48920 — an empty function, folded with every other empty one in the DLL.
}

void FmtStr::append(Val* Node)
{
    // 0630d850-0630d86a. Takes ownership of an already-built node and links it at the tail;
    // a null node is ignored. Node->Next is not touched, so a chain is appended whole.
    if (Node == nullptr)
        return;
    LinkFmtStrTail(*this, Node);
}

void FmtStr::append_fmt_str(const FmtStr& Other)
{
    // RVA 0xADC50 — tag 0x21, a nested FmtStr subtree. BUG FIX: this previously stamped a
    // hardcoded 0 into the new node's Flags. Retail (Common.dll.c:150717-150718) reads/posts
    // -increments `this[0x13]` same as every other append_*, i.e. it DOES have its own counter
    // -- `ValueIndex[KindUnused13]` (offset +0x13), despite that slot's header comment claiming
    // no exported append_* touches it. See also common_text.h's ValueKind comment, which should
    // be corrected: only offset +0x0D (KindUnused0D) is genuinely never stamped.
    // 0630dc50-0630dcf7: the node is allocated first; a failed malloc does nothing at all.
    // 0630dc8a-0630dcb7: VC6 operator new returns null on failure (hence nothrow); a null
    // subtree skips the copy and the node is still appended with a null payload.
    Val* const Node = (Val*)malloc(sizeof(Val));
    if (Node == nullptr)
        return;
    FmtStr* const Sub = new (std::nothrow) FmtStr();
    if (Sub != nullptr)
        *Sub = Other;
    Node->Flags = ValueIndex[KindUnused13];
    ++ValueIndex[KindUnused13];
    Node->Subtree = Sub;
    Node->Next = nullptr;
    Node->TypeTag = 0x21;
    LinkFmtStrTail(*this, Node);
}

void FmtStr::append_nav_marker(const NavMarker& Marker)
{
    // RVA 0xAD8E0 (0630d8e0-0630d97b) — tag 0x4E, counter +0x08. The node is allocated first (a
    // failed malloc does nothing); the 0x14-byte payload block is null-tested by retail itself
    // (0630d92e): on failure the node keeps a null payload.
    Val* const Node = (Val*)malloc(sizeof(Val));
    if (Node == nullptr)
        return;
    // 0630d916: operator new(0x14) (VC6: null on failure, hence nothrow).
    NavMarker* const Block = new (std::nothrow) NavMarker;
    if (Block != nullptr)
        *Block = Marker;
    Node->Flags = ValueIndex[KindNavMarker];
    ++ValueIndex[KindNavMarker];
    Node->Next = nullptr;
    Node->Marker = Block;
    Node->TypeTag = 0x4E;
    LinkFmtStrTail(*this, Node);
}

int FmtStr::append_void(void* Data)
{
    // RVA 0xAD870 — rebuilds a node straight from a serialized Val, so the flags come from the
    // source and no counter moves. Returns the number of bytes consumed.
    // 0630d870-0630d8d9. A failed malloc returns EAX as it stands, which is the Data argument
    // loaded at 0630d8a4 -- very strange, reproduced on purpose. Otherwise Next = 0,
    // InitFromVoid fills the rest, and the node goes to the tail.
    Val* const Node = (Val*)malloc(sizeof(Val));
    if (Node == nullptr)
        return (int)(std::uintptr_t)Data;
    Node->Next = nullptr;
    int const Consumed = Node->InitFromVoid(Data);
    LinkFmtStrTail(*this, Node);
    return Consumed;
}

void FmtStr::append_system(const unsigned int& Id)
{
    // RVA 0xAD980 — tag 0x53 'S', counter +0x09.
    AppendValueNode(*this, 0x53, Id, ValueIndex[KindSystem]);
}

void FmtStr::append_base(const unsigned int& Id)
{
    // RVA 0xAD9D0 — tag 0x42 'B', counter +0x0A. Retail takes an ID by reference, not a
    // char* -- the port used to declare append_base(char const*).
    AppendValueNode(*this, 0x42, Id, ValueIndex[KindBase]);
}

void FmtStr::append_string(unsigned int Id)
{
    // RVA 0xADA20 — tag 0x73 's', counter +0x0B. Again an IDS id by value, not a char*.
    AppendValueNode(*this, 0x73, Id, ValueIndex[KindString]);
}

void FmtStr::append_good(const unsigned int& Id)
{
    // RVA 0xADA70 — tag 0x47 'G', counter +0x0C.
    AppendValueNode(*this, 0x47, Id, ValueIndex[KindGood]);
}

void FmtStr::append_int(unsigned int Value)
{
    // RVA 0xADAC0 — tag 0x64 'd', counter +0x0E.
    AppendValueNode(*this, 0x64, Value, ValueIndex[KindInt]);
}

void FmtStr::append_rep_instance(const int& Id)
{
    // RVA 0xADB10 — tag 0x52 'R', counter +0x0F. Signed, unlike its group sibling.
    AppendValueNode(*this, 0x52, Id, ValueIndex[KindRepInstance]);
}

void FmtStr::append_rep_group(const unsigned int& Id)
{
    // RVA 0xADB60 — tag 0x46 'F', counter +0x10.
    AppendValueNode(*this, 0x46, Id, ValueIndex[KindRepGroup]);
}

void FmtStr::append_zone_id(const unsigned int& Id)
{
    // RVA 0xADBB0 — tag 0x5A 'Z', counter +0x11.
    AppendValueNode(*this, 0x5A, Id, ValueIndex[KindZoneId]);
}

void FmtStr::append_spaceobj_id(const unsigned int& Id)
{
    // RVA 0xADC00 — tag 0x4F 'O', counter +0x12.
    AppendValueNode(*this, 0x4F, Id, ValueIndex[KindSpaceObjId]);
}

void FmtStr::append_installation(int Id)
{
    // RVA 0xADD00 — tag 0x49 'I', counter +0x14.
    AppendValueNode(*this, 0x49, Id, ValueIndex[KindInstallation]);
}

void FmtStr::append_loot(int Id)
{
    // RVA 0xADD50 — tag 0x4C 'L', counter +0x15.
    AppendValueNode(*this, 0x4C, Id, ValueIndex[KindLoot]);
}

int FmtStr::flatten(void* Dest, unsigned int MaxSize) const
{
    // RVA 0xADF60. The wire header is {mad-lib id : dword, node count : word} -- 0630df6d
    // writes [this+0x04] first, and only then the counted list length. The port used to write
    // the node count twice because it had nowhere to keep the id.
    unsigned char* const Base = (unsigned char*)Dest;
    *(unsigned int*)Base = MadLibId;

    unsigned short NodeCount = 0;
    for (Val const* V = Head; V != nullptr; V = V->Next)
        NodeCount += 1;
    *(unsigned short*)(Base + 4) = NodeCount;

    unsigned char* Cursor = Base + 6;
    unsigned int Remaining = MaxSize - 6;
    for (Val const* V = Head; V != nullptr; V = V->Next)
    {
        *(unsigned short*)Cursor = V->TypeTag;
        *(Cursor + 2) = V->Flags;

        unsigned int Written;
        if (V->TypeTag == 0x21)
        {
            // BUG FIX: this previously hardcoded Written=3 (header only), which silently
            // dropped the nested subtree's own payload bytes from Cursor/Remaining and would
            // clobber it with the next sibling. Retail (Common.dll.c:150936-150939) does
            // `iVar2 = flatten(puVar1[1], Cursor+3, uVar5) + 3;` -- the nested FmtStr::flatten
            // call recurses with the SAME (not reduced) remaining budget, and its returned byte
            // count is added to the 3-byte tag+flags header to get this node's total size.
            // 0630dfe1-0630dfef: the subtree is called without a null test.
            Written = V->Subtree->flatten(Cursor + 3, Remaining) + 3;
        }
        else if (V->TypeTag == 0x4E)
        {
            memcpy(Cursor + 3, V->Marker, sizeof(NavMarker));
            Written = 0x17;
        }
        else
        {
            *(unsigned int*)(Cursor + 3) = V->Value;
            Written = 7;
        }
        Cursor += Written;
        Remaining -= Written;
    }
    return (int)(Cursor - Base);
}

int FmtStr::unflatten(void* Src, unsigned int Size)
{
    // RVA 0xAE010 — the inverse of flatten. Returns the number of bytes consumed.
    // BUG FIX: retail (Common.dll.c:150989-150997) unconditionally zeroes the WHOLE object
    // (Head, MadLibId, and all 14 ValueIndex counters -- 6 dwords total) before checking
    // whether Src is null, and the wire format never carries the counters back in, so after
    // any unflatten() (null Src included) they must end up all-zero. This previously only
    // called destroy() (Head only) and left MadLibId/ValueIndex stale on both the null-Src
    // early-out and the success path.
    (void)Size;
    destroy();
    MadLibId = 0;
    memset(ValueIndex, 0, sizeof(ValueIndex));
    if (Src == nullptr)
        return 0;

    unsigned char const* const Base = (unsigned char const*)Src;
    MadLibId = *(unsigned int const*)Base;
    unsigned short const NodeCount = *(unsigned short const*)(Base + 4);

    // 0630e0a7-0630e0f5: per node a CRT malloc of 0x0c, Next = 0, InitFromVoid; the node is
    // stored into the current link and becomes the next link. A failed malloc stores 0 and
    // advances the cursor by the previous step (EBP, which starts as the Src argument) -- very
    // strange, reproduced on purpose; the next node's link store then goes through null.
    unsigned char const* Cursor = Base + 6;
    Val** Link = &Head;
    std::uintptr_t Step = (std::uintptr_t)Src;
    for (unsigned int Index = 0; Index < NodeCount; ++Index)
    {
        Val* const Node = (Val*)malloc(sizeof(Val));
        if (Node != nullptr)
        {
            Node->Next = nullptr;
            Step = (std::uintptr_t)Node->InitFromVoid(Cursor);
        }
        Cursor += Step;
        *Link = Node;
        Link = (Val**)Node; // &Node->Next (Next is at +0)
    }
    return (int)(Cursor - Base);
}
