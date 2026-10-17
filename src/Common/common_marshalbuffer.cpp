// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll MarshalBuffer: the network message (de)serialiser. Both halves of the port live
// here: the primitive / string / FmtStr / Costume members (formerly common_net.cpp) and the
// EquipDescList / GoodDescList members (formerly common_goods.cpp).

#include "common_net.h"
#include "common_text.h"  // FmtStr -- complete type for put_FmtStr / get_FmtStr
#include "common_goods.h" // EquipDescList, GoodDescList, GoodDesc
#include "common_ini.h"   // StringAlloc -- get_EquipDescList's hardpoint names

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <list>

// ===========================================================================
// MarshalBuffer
// ===========================================================================

MarshalBuffer::MarshalBuffer()
    : BufferBase(nullptr)
    , BufferSize(0)
    , Cursor(nullptr)
    , Remaining(0)
    , NumWritten(0)
    , bReadMode(1)
{
}

MarshalBuffer& MarshalBuffer::operator=(const MarshalBuffer& Other)
{
    // Common.dll.c 10818-10833 (0x3b40, ICF-folded with Personality::RepairUseStruct::
    // operator=): plain 6-dword raw copy of the whole 0x18-byte object, no self-assignment
    // check.
    BufferBase = Other.BufferBase;
    BufferSize = Other.BufferSize;
    Cursor = Other.Cursor;
    Remaining = Other.Remaining;
    NumWritten = Other.NumWritten;
    bReadMode = Other.bReadMode;
    return *this;
}

void MarshalBuffer::clear()
{
    BufferBase = nullptr;
    BufferSize = 0;
    Cursor = nullptr;
    Remaining = 0;
    NumWritten = 0;
    bReadMode = 1;
}

void MarshalBuffer::use_default()
{
    // Points to internal static buffer at DAT_063fd2c8 (0x4000 bytes in .data).
    static unsigned char G_DefaultBuffer[0x4000];
    BufferBase = G_DefaultBuffer;
    BufferSize = 0x4000;
    Cursor = G_DefaultBuffer;
    Remaining = 0x4000;
    NumWritten = 0;
    bReadMode = 0;
}

void MarshalBuffer::use_read_buffer(const unsigned char* Data, int Size)
{
    BufferBase = const_cast<unsigned char*>(Data);
    BufferSize = Size;
    Cursor = const_cast<unsigned char*>(Data);
    Remaining = Size;
    NumWritten = 0;
    bReadMode = 1;
}

void MarshalBuffer::use_write_buffer(unsigned char* Data, int Size)
{
    BufferBase = Data;
    BufferSize = Size;
    Cursor = Data;
    Remaining = Size;
    NumWritten = 0;
    bReadMode = 0;
}

// --- Core put/get_void ---

void MarshalBuffer::put_void(const void* Src, int Count)
{
    if (Count == 0)
        return;

    unsigned char* Dst = Cursor;
    const unsigned char* S = (const unsigned char*)Src;

    // DWORD copy loop (matches retail's REP MOVSD), then the 0..3 tail bytes.
    unsigned int Dwords = (unsigned int)Count >> 2;
    for (unsigned int i = 0; i < Dwords; i++)
    {
        memcpy(Dst, S, 4);
        S += 4;
        Dst += 4;
    }

    // Remaining bytes
    unsigned int Bytes = (unsigned int)Count & 3;
    for (unsigned int i = 0; i < Bytes; i++)
    {
        *Dst = *S;
        S++;
        Dst++;
    }

    Cursor = Dst;
    Remaining -= Count;
    NumWritten += Count;
}

void MarshalBuffer::get_void(void* Dst, int Count)
{
    if (Count <= 0 || Count > Remaining)
        return;

    unsigned char* D = (unsigned char*)Dst;
    const unsigned char* S = Cursor;

    unsigned int Dwords = (unsigned int)Count >> 2;
    for (unsigned int i = 0; i < Dwords; i++)
    {
        memcpy(D, S, 4);
        S += 4;
        D += 4;
    }

    unsigned int Bytes = (unsigned int)Count & 3;
    for (unsigned int i = 0; i < Bytes; i++)
    {
        *D = *S;
        S++;
        D++;
    }

    Cursor = const_cast<unsigned char*>(S);
    Remaining -= Count;
}

void MarshalBuffer::skip_over(int Count)
{
    Cursor += Count;
    Remaining -= Count;
}

// --- Primitives ---

void MarshalBuffer::put_U8(unsigned char Value)
{
    put_void(&Value, 1);
}

// get_U8 / get_U16 / get_U32 / get_float (06263aa0 / 06263ad0 / 06263af0 / 06263b20) keep their
// result in the stack slot made by PUSH ECX, so when get_void reads nothing (Count > Remaining)
// they return the bytes of `this` that sit there: the high byte, the high word, the whole
// pointer, the pointer's bits as a float. Very strange (очень странно), reproduced on purpose.
unsigned char MarshalBuffer::get_U8()
{
    unsigned char V = (unsigned char)((std::uintptr_t)this >> 24);
    get_void(&V, 1);
    return V;
}

void MarshalBuffer::put_U16(unsigned short Value)
{
    put_void(&Value, 2);
}

unsigned short MarshalBuffer::get_U16()
{
    unsigned short V = (unsigned short)((std::uintptr_t)this >> 16);
    get_void(&V, 2);
    return V;
}

void MarshalBuffer::put_U32(unsigned long Value)
{
    put_void(&Value, 4);
}

unsigned long MarshalBuffer::get_U32()
{
    unsigned long V = (unsigned long)(std::uintptr_t)this;
    get_void(&V, 4);
    return V;
}

void MarshalBuffer::put_int(int Value)
{
    put_void(&Value, 4);
}

int MarshalBuffer::get_int()
{
    int V = 0;
    get_void(&V, 4);
    return V;
}

void MarshalBuffer::put_float(float Value)
{
    put_void(&Value, 4);
}

float MarshalBuffer::get_float()
{
    unsigned long Bits = (unsigned long)(std::uintptr_t)this;
    get_void(&Bits, 4);
    float V;
    memcpy(&V, &Bits, sizeof(V));
    return V;
}

void MarshalBuffer::put_bool(bool Value)
{
    // Retail writes a single byte directly (not via put_void)
    *Cursor = Value ? 1 : 0;
    Cursor++;
    Remaining--;
    NumWritten++;
}

bool MarshalBuffer::get_bool()
{
    // 06312970..0631299c. An empty buffer compares the high byte of the pushed `this`
    // (06312990) with 1 -- очень странно, reproduced on purpose.
    if (Remaining >= 1)
    {
        unsigned char V = *Cursor;
        Cursor++;
        Remaining--;
        return V == 1;
    }
    return (unsigned char)((std::uintptr_t)this >> 24) == 1;
}

// --- Strings ---

void MarshalBuffer::put_string(const char* Str)
{
    if (Str == nullptr)
    {
        // Write sentinel -1
        int const Sentinel = -1;
        memcpy(Cursor, &Sentinel, 4);
        Cursor += 4;
        Remaining -= 4;
        NumWritten += 4;
        return;
    }

    unsigned int const Len = (unsigned int)strlen(Str);
    memcpy(Cursor, &Len, 4);
    Cursor += 4;
    Remaining -= 4;
    NumWritten += 4;

    if (Len > 0)
    {
        put_void(Str, (int)Len);
    }
}

char* MarshalBuffer::get_string()
{
    // 06312b70..06312bed. With fewer than 4 bytes left the length is the PUSH ECX slot, i.e.
    // (unsigned)this (06312b92) -- очень странно; any real object address is >= 0x4000, so
    // that returns NULL. malloc is not tested (06312ba7).
    unsigned int Len = (unsigned int)(std::uintptr_t)this;
    if (Remaining >= 4)
    {
        Len = *(unsigned int*)Cursor;
        Cursor += 4;
        Remaining -= 4;
    }

    if (Len == 0xFFFFFFFF || Len >= 0x4000)
        return nullptr;

    char* Result = (char*)malloc(Len + 1);
    get_void(Result, (int)Len);
    Result[Len] = '\0';
    return Result;
}

void MarshalBuffer::get_string_to_buffer(char* Buf, int MaxLen)
{
    // 06312bf0..06312cd3. EBX = MaxLen - 1, compared UNSIGNED with the length (JA).
    unsigned int const Cap = (unsigned int)(MaxLen - 1);

    // With fewer than 4 bytes left the length is read from the Buf argument's own slot
    // (06312c18), i.e. (unsigned)Buf -- очень странно; for a real pointer (>= 0x4000) that
    // ends in Buf[0] = 0.
    unsigned int Len = (unsigned int)(std::uintptr_t)Buf;
    if (Remaining >= 4)
    {
        Len = *(unsigned int*)Cursor;
        Cursor += 4;
        Remaining -= 4;
    }

    if (Len == 0xFFFFFFFF || Len >= 0x4000)
    {
        Buf[0] = '\0';
        return;
    }

    if (Len > Cap)
    {
        // Truncating: only Cap bytes fit in Buf, but retail still consumes the FULL Len
        // bytes from the wire so later reads stay cursor-aligned (Common.dll.c:155975-155996;
        // a prior version of this port only advanced by Cap here, desyncing the cursor for
        // every read after a too-small destination buffer truncated a string).
        // 06312c76..06312cc5: the inlined get_void of Cap bytes, Buf[Cap] = 0, then the rest.
        get_void(Buf, (int)Cap);
        Buf[Cap] = '\0';
        Cursor += (Len - Cap);
        Remaining -= (int)(Len - Cap);
    }
    else
    {
        // 06312c38..06312c6e: the inlined get_void of Len bytes, Buf[Len] = 0.
        get_void(Buf, (int)Len);
        Buf[Len] = '\0';
    }
}

void MarshalBuffer::put_wstring(const wchar_t* Str)
{
    if (Str == nullptr)
    {
        int const Sentinel = -1;
        memcpy(Cursor, &Sentinel, 4);
        Cursor += 4;
        Remaining -= 4;
        NumWritten += 4;
        return;
    }

    unsigned int const CharLen = (unsigned int)wcslen(Str);
    memcpy(Cursor, &CharLen, 4);
    Cursor += 4;
    Remaining -= 4;
    NumWritten += 4;

    unsigned int const ByteLen = CharLen * 2;
    if (ByteLen > 0)
    {
        put_void(Str, (int)ByteLen);
    }
}

wchar_t* MarshalBuffer::get_wstring()
{
    // 06312a40..06312ac3, the wide twin of get_string: fewer than 4 bytes left -> the length
    // is (unsigned)this from the PUSH ECX slot (06312a62, очень странно) -> NULL for any real
    // object; malloc(Len*2 + 2) is not tested; the inlined get_void of Len*2 bytes.
    unsigned int CharLen = (unsigned int)(std::uintptr_t)this;
    if (Remaining >= 4)
    {
        CharLen = *(unsigned int*)Cursor;
        Cursor += 4;
        Remaining -= 4;
    }

    if (CharLen == 0xFFFFFFFF || CharLen >= 0x4000)
        return nullptr;

    wchar_t* Result = (wchar_t*)malloc(CharLen * 2 + 2);
    get_void(Result, (int)(CharLen * 2));
    Result[CharLen] = 0;
    return Result;
}

// --- Image helpers (thin wrappers) ---

void MarshalBuffer::put_image(const unsigned char& V)  { put_void(&V, 1); }
void MarshalBuffer::get_image(unsigned char* V)        { get_void(V, 1); }
void MarshalBuffer::put_image(const unsigned short& V) { put_void(&V, 2); }
void MarshalBuffer::get_image(unsigned short* V)       { get_void(V, 2); }
void MarshalBuffer::put_image(const int& V)            { put_void(&V, 4); }
void MarshalBuffer::get_image(int* V)                  { get_void(V, 4); }
void MarshalBuffer::put_image(const unsigned long& V)  { put_void(&V, 4); }
void MarshalBuffer::get_image(unsigned long* V)        { get_void(V, 4); }
void MarshalBuffer::put_image(const float& V)          { put_void(&V, 4); }
void MarshalBuffer::get_image(float* V)                { get_void(V, 4); }

// --- FL_ID ---

void MarshalBuffer::put_FL_ID(unsigned int Id)
{
    memcpy(Cursor, &Id, 4);
    Cursor += 4;
    Remaining -= 4;
    NumWritten += 4;
}

unsigned int MarshalBuffer::get_FL_ID()
{
    // 06313560..06313584. Fewer than 4 bytes left returns the pushed `this` (0631357e) --
    // очень странно, reproduced on purpose.
    if (Remaining >= 4)
    {
        unsigned int Id = *(unsigned int*)Cursor;
        Cursor += 4;
        Remaining -= 4;
        return Id;
    }
    return (unsigned int)(std::uintptr_t)this;
}

// --- Complex type marshalling (stubbed — depend on unported types) ---

void MarshalBuffer::put_FmtStr(const FmtStr& Fmt)
{
    // Common.dll.c 156025-156069 (0xb2ce0): compute an upper-bound size by walking the node
    // list (FUN_06312d80, a per-node recursive sizer -- header 6 + each node's own
    // contribution, +3 extra for a nested FmtStr on top of ITS OWN recursive total), malloc
    // that much, flatten() into it, then copy out exactly what flatten() reports writing (its
    // real return value, not the pre-computed bound -- matches retail's `uVar5 =
    // FmtStr::flatten(...); if (uVar5 != 0) { ...copy uVar5 bytes... }`).
    //
    // FmtStr::Val::get_flattened_size() (common_fmtstr.cpp) already implements this exact
    // per-node recursion (6 + children's sizes + 3 for a nested subtree, 0x17/7 for a leaf),
    // so this just sums it over the top-level list instead of duplicating one hand-rolled
    // level of nesting here (a prior version only unrolled one level manually, which
    // under-counted a FmtStr nested more than one level deep).
    unsigned int TotalSize = 6;
    for (FmtStr::Val const* Node = Fmt.Head; Node != nullptr; Node = Node->Next)
        TotalSize += Node->get_flattened_size();

    // 06312d25 operator new / 06312d6c operator delete (FUN_06391d5a); the block is not tested.
    unsigned char* TempBuf = new unsigned char[TotalSize];
    int const Written = Fmt.flatten(TempBuf, TotalSize);
    // 06312d3a: TEST EAX,EAX / JZ, then the inlined put_void of Written bytes.
    if (Written != 0)
    {
        put_void(TempBuf, Written);
    }

    delete[] TempBuf;
}

FmtStr MarshalBuffer::get_FmtStr()
{
    // 06312dd0..06312e8f: unflatten(Cursor, 0) into a local (the size argument is 0), then
    // Remaining and Cursor move by unflatten's own return value. Retail then copy-constructs
    // the result from the local and destroys the local; returning the local directly has the
    // same effect.
    FmtStr Result;
    int const Consumed = Result.unflatten(Cursor, 0);
    Remaining -= Consumed;
    Cursor += Consumed;
    return Result;
}

// ---------------------------------------------------------------------------
// MarshalBuffer::put_EquipDescList / get_EquipDescList (Common.dll 0xb2ea0 /
// 0xb2ff0). Declared in common_net.h; defined here since EquipDescList is an
// EQUIP-batch type.
//
// The Ghidra .c decomp of get_EquipDescList mis-resolves several of the
// per-field setter calls to unrelated classes (CPlayerTradeOffer::
// SetSourcePlayerID, CNudgeEngine::SetOwner, DamageList::set_cause,
// DamageList::set_destroyed) -- these are linker /OPT:ICF folds: the actual
// call targets are one-line "store this-relative field" stubs byte-identical
// to unrelated classes' setters, and Ghidra just labels the merged code with
// whichever symbol it happened to keep. Field order/widths below were
// recovered by computing each call's real target address from the raw .txt
// disassembly and reading that address's own bytes directly (not trusting
// the misleading names):
//   0x06309b30 ("SetSourcePlayerID") -> MOV [this+0x4],param   (ArchId, u32)
//   0x06309b40 ("set_hardpoint")     -> deref param, [this+0x8]=*param (Hardpoint)
//   0x0633bad0 ("SetOwner")          -> MOV [this+0x14],param  (Count, u32)
//   0x06332f50 ("set_cause")         -> MOV [this+0x10],param  (Health, u32/float)
//   0x06309b20 ("set_id")            -> MOV word[this+0x2],param (Id, u16)
//   0x06309b50 ("set_destroyed")     -> MOV byte[this+0xc],param (bEquipped, u8)
// put_EquipDescList's own body (not ICF-folded, verified directly) confirms
// the same seven fields in the same order via raw cursor writes.
// Wire format: item count (int), then per item: ArchId (u32), Hardpoint
// (length-prefixed string -- null encoded as length -1, same format as
// PutString/GetString), Count (int), Health (float), Id (u16 widened to a
// full 4-byte slot on both sides), bEquipped (byte, normalized 0/1),
// bMission (byte, normalized 0/1). EquipDesc::Cached (+0x00) and Owner
// (+0x1c) are never on the wire in this format.
// ---------------------------------------------------------------------------

void MarshalBuffer::put_EquipDescList(const EquipDescList& List)
{
    put_int((int)List.Items.size());
    for (std::list<EquipDesc>::const_iterator It = List.Items.begin(); It != List.Items.end(); ++It)
    {
        EquipDesc const& Item = *It;
        put_U32(Item.ArchId);
        put_string(Item.Hardpoint);
        put_int(Item.Count);
        put_float(Item.Health);
        put_U32((unsigned long)Item.Id);
        put_U8(Item.bEquipped ? 1 : 0);
        put_U8(Item.bMission ? 1 : 0);
    }
}

EquipDescList MarshalBuffer::get_EquipDescList()
{
    EquipDescList Result;
    int ItemCount = get_int();
    for (int Index = 0; Index < ItemCount; ++Index)
    {
        EquipDesc Item;
        Item.ArchId = get_U32();

        // 063130a1-063130e9: the malloc'd text (null for length -1 or >= 0x4000) goes to
        // StringAlloc(text, false) and is never freed -- retail leaks it on every item.
        char* HpText = get_string();
        CacheString HpCache;
        HpCache.Value = StringAlloc(HpText, false);
        Item.set_hardpoint(HpCache);

        Item.Count = get_int();
        Item.Health = get_float();
        Item.Id = (unsigned short)get_U32();
        Item.bEquipped = get_U8() == 1;
        Item.bMission = get_U8() == 1;

        Result.Items.push_back(Item);
    }
    return Result;
}

// ---------------------------------------------------------------------------
// MarshalBuffer::put_GoodDescList / get_GoodDescList (Common.dll 0xb3250 /
// 0xb3330). Declared in common_net.h; defined here since GoodDescList is a
// GOODS-batch type. Wire format: item count (int) followed, per item, by
// GoodId (u32), Count (int), Price (float), Field0C (float) -- the same four
// raw dwords GoodDesc's own default ctor initializes.
// ---------------------------------------------------------------------------

void MarshalBuffer::put_GoodDescList(const GoodDescList& List)
{
    put_int((int)List.count());
    MetaNode<GoodDesc>* Node = nullptr;
    while (List.traverse(Node, true))
    {
        put_U32(Node->Data->GoodId);
        put_int(Node->Data->Count);
        put_float(Node->Data->Price);
        put_float(Node->Data->Field0C);
    }
}

// Retail allocates each element with ??2GoodDesc (0x063133a6) and reads the four
// dwords straight into it before appending.
GoodDescList MarshalBuffer::get_GoodDescList()
{
    GoodDescList Result;
    int ItemCount = get_int();
    for (int Index = 0; Index < ItemCount; ++Index)
    {
        GoodDesc* Item = new GoodDesc;
        Item->GoodId = get_U32();
        Item->Count = get_int();
        Item->Price = get_float();
        Item->Field0C = get_float();
        Result.append(Item);
    }
    return Result;
}

void MarshalBuffer::put_Costume(const Costume& C)
{
    // 06313590..06313672. Wire format: the four part ids, the accessory count, then exactly
    // that many accessory ids (signed loop, no clamp to 8). Every dword is stored raw at the
    // cursor with no bounds check, as put_FL_ID does.
    for (int Index = 0; Index < 4; ++Index)
        put_FL_ID(C.PartIds[Index]);
    put_FL_ID(C.AccessoryCount);
    for (int Index = 0; Index < (int)C.AccessoryCount; ++Index)
        put_FL_ID(C.AccessoryIds[Index]);
}

Costume MarshalBuffer::get_Costume()
{
    // 06313680..0631377c. The inverse of put_Costume: four part ids, the count (stored into
    // the local's +0x30), then count accessory ids into +0x10 + 4*i, the bound re-read each
    // pass (a count above 8 runs into the count slot itself, as in retail). Every read is an
    // inline `Remaining >= 4` read into one register. The local is then copied out whole
    // (REP MOVSD, 13 dwords).
    // Named gap (retail UB): a short read of the first five dwords stores the hidden return
    // pointer (063136a0 ...), the loop starts from it too (0631373a) and then keeps the
    // previous value; accessory slots past the count are never written (stack garbage). The
    // port uses 0 for the return pointer and zeroes the local.
    Costume C;
    memset(&C, 0, sizeof(C));
    unsigned int Scratch = 0;
    for (int Index = 0; Index < 4; ++Index)
    {
        Scratch = 0;
        if (Remaining >= 4)
        {
            Scratch = *(unsigned int*)Cursor;
            Cursor += 4;
            Remaining -= 4;
        }
        C.PartIds[Index] = Scratch;
    }
    Scratch = 0;
    if (Remaining >= 4)
    {
        Scratch = *(unsigned int*)Cursor;
        Cursor += 4;
        Remaining -= 4;
    }
    C.AccessoryCount = Scratch;
    Scratch = 0;
    for (int Index = 0; Index < (int)C.AccessoryCount; ++Index)
    {
        if (Remaining >= 4)
        {
            Scratch = *(unsigned int*)Cursor;
            Cursor += 4;
            Remaining -= 4;
        }
        C.AccessoryIds[Index] = Scratch;
    }
    return C;
}

// --- Status ---

bool MarshalBuffer::is_empty() const
{
    // Common.dll.txt 0x06263a80: *(this+0xc) == 0, i.e. exactly Remaining == 0.
    return Remaining == 0;
}

int MarshalBuffer::get_num_written() const
{
    // .text:06263a70: MOV EAX,[this+0x10]; RET.
    return NumWritten;
}

unsigned char const* MarshalBuffer::get_buffer() const
{
    // .text:06261d90: MOV EAX,[this]; RET (shared ICF body).
    return BufferBase;
}

int MarshalBuffer::get_buffer_size() const
{
    // .text:062d1ad0: MOV EAX,[this+0x4]; RET (shared ICF body).
    return BufferSize;
}
