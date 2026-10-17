// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_transport.cpp -- packet base, packet-type registry and the CDAConnection queues.
//
// Ground truth: EXE/Disasm/rpclocal.dll.txt.
// Retail addresses are quoted per function.

#include "rpc_transport.h"
#include "rpc_payloads.h"
#include "rpc_containers.h"
#include "common_server_types.h"

#include <new>
#include <ostream>
#include <cstring>
#include <cstdio>

// ===========================================================================
// FRpcRefObject -- the refcounted root, vtable 06db3c54.
// ===========================================================================

// FUN_06d8f4c0: `MOV [ECX],06db3c54 ; RET` -- nothing else to undo.
FRpcRefObject::~FRpcRefObject() {
}

// FUN_06d8f4a0, see the declaration. Only the decrement that REACHES zero destroys; a
// count already at zero is returned untouched (06d8f4a5 JBE).
unsigned int FRpcRefObject::Release() {
    if (UsageCount == 0) {
        return UsageCount;                               // 06d8f4a5 JBE -> 06d8f4b8
    }
    --UsageCount;                                        // 06d8f4a7 DEC / 06d8f4aa MOV
    if (UsageCount != 0) {
        return UsageCount;                               // 06d8f4ad JNZ -> 06d8f4b8
    }
    delete this;                                         // 06d8f4b1 PUSH 1 / CALL [EAX]
    return 0;                                            // 06d8f4b5 XOR EAX,EAX
}

// ===========================================================================
// FPacketBase -- retail CDAPacket, 0x20-byte base.
// ===========================================================================

// The five CDAPacket stores every retail packet constructor makes (UsageCount = 1 is
// FRpcRefObject's), e.g. FUN_06d9af10: 06d9af14 [+0x04] = 1, 06d9af1b [+0x08] = 0,
// 06d9af1e [+0x10] = 0, 06d9af21 [+0x1c] = 0, 06d9af43 [+0x0c] = 0x10001. +0x14 and
// +0x18 are left alone.
FPacketBase::FPacketBase(unsigned int InPacketId)
    : Field_0x08(nullptr), PacketId(InPacketId), MarshalCursor(0), MarshalError(0) {
}

// FUN_06da1370: `MOV [ECX],06db4fbc ; JMP FUN_06d8f4c0`.
FPacketBase::~FPacketBase() {
}

// FUN_06da1380 at 06da1383..06da13a2, instruction for instruction.
unsigned short RpcPacketHeaderWord(unsigned int PacketId) {
    const unsigned int Opcode = PacketId & 0xfffu;              // 06da1397 AND EDX,0xfff
    const unsigned int ChannelByte = (PacketId >> 16) & 0xffu;  // 06da1389 MOV BH,[this+0xe]
    return (unsigned short)(Opcode | (ChannelByte << 8));       // 06da13a0 OR EDX,EBX
}

// CDAPacket's own slot +0x04, 06da13d0: the header and nothing else. Five of the 162
// classes keep it (their vtables hold 06da13d0); the rest override it with their codec.
unsigned int FPacketBase::Encode(unsigned char** BufferHolder, unsigned int Capacity,
                                 unsigned int* OutLength) {
    return RpcEncodeWireEmptyInline(*this, BufferHolder, Capacity, OutLength);
}

// CDAPacket's own slot +0x08, 06da1470.
unsigned int FPacketBase::Decode(const unsigned char* Buffer, unsigned int Size,
                                 unsigned int* OutLength) {
    return RpcDecodeWireEmptyInline(*this, Buffer, Size, OutLength);
}

// Slot +0x0c: the shared `RET 0x4` body 150 classes keep. Twelve override it.
void FPacketBase::Dump(std::ostream&) const {
}

void FPacketBase::AddRef() {
    ++UsageCount;
}

// The constructor's own immediate stores; retail makes them with no test (e.g. 06d9d555
// `MOV dword ptr [ESI + 0xc],0x20003`), and every offset was read out of those stores,
// inside the class's own size.
void RpcPacketStoreField(FPacketBase* Packet, unsigned int Offset, unsigned int Width,
                         unsigned int Value) {
    unsigned char* Bytes = reinterpret_cast<unsigned char*>(Packet) + Offset;
    switch (Width) {
        case 1:
            *Bytes = (unsigned char)(Value & 0xffu);
            break;
        case 2:
            *reinterpret_cast<unsigned short*>(Bytes) = (unsigned short)(Value & 0xffffu);
            break;
        case 4:
            *reinterpret_cast<unsigned int*>(Bytes) = Value;
            break;
        default:
            break;
    }
}

// The 0x0c MSVCP60 std::list header, as every embedded list in this module wears it.
// Type-agnostic on purpose: the node size comes from the caller, so one body covers
// node sizes 0x0c, 0x10, 0x14 and 0x50 alike.
struct FRpcEmbeddedListHeader {
    unsigned char Allocator;        // +0x00
    unsigned char Pad_0x01[3];      // +0x01
    void* Head;                     // +0x04  the sentinel node
    unsigned int Size;              // +0x08
};

// The two links every node starts with.
struct FRpcEmbeddedListNode {
    FRpcEmbeddedListNode* Next;     // +0x00
    FRpcEmbeddedListNode* Prev;     // +0x04
};

// 06d9d52f..06d9d549 (FUN_06d9d4f0, the CHARACTERINFO ctor; the other list-building
// ctors are the same sequence with their own offset and node size). Nothing is tested:
// not the packet, not the operator new result -- 06d9d541 stores through EAX straight
// after the CALL. The sentinel's value bytes are left as operator new returned them.
void RpcPacketBuildListSentinel(FPacketBase* Packet, unsigned int Offset,
                                unsigned int NodeSize) {
    FRpcEmbeddedListHeader* Header = reinterpret_cast<FRpcEmbeddedListHeader*>(
        reinterpret_cast<unsigned char*>(Packet) + Offset);
    Header->Allocator = 0;                                  // 06d9d535 MOV [ESI+0x24],AL
    FRpcEmbeddedListNode* Sentinel =
        static_cast<FRpcEmbeddedListNode*>(::operator new(NodeSize));   // 06d9d52f / 06d9d538
    Sentinel->Next = Sentinel;                              // 06d9d541 MOV [EAX],EAX
    Sentinel->Prev = Sentinel;                              // 06d9d543 MOV [EAX+0x4],EAX
    Header->Size = 0;                                       // 06d9d546 MOV [ESI+0x2c],EDI
    Header->Head = Sentinel;                                // 06d9d549 MOV [ESI+0x28],EAX
}

// FUN_06d9d1f0's list half (06d9d1f5..06d9d238), which is FUN_06d901f0 / COMMON.DLL's
// list::erase in a loop, then the sentinel.
void RpcPacketDestroyEmbeddedList(FPacketBase* Packet, unsigned int Offset) {
    FRpcEmbeddedListHeader* Header = reinterpret_cast<FRpcEmbeddedListHeader*>(
        reinterpret_cast<unsigned char*>(Packet) + Offset);
    FRpcEmbeddedListNode* Sentinel = static_cast<FRpcEmbeddedListNode*>(Header->Head);
    FRpcEmbeddedListNode* Node = Sentinel->Next;            // 06d9d1f8 MOV ESI,[EBX]
    while (Node != Sentinel) {                              // 06d9d1fa / 06d9d220 CMP
        FRpcEmbeddedListNode* Erased = Node;                // 06d9d200 MOV EAX,ESI
        Node = Node->Next;                                  // 06d9d207 MOV ESI,[ESI]
        Erased->Prev->Next = Erased->Next;                  // 06d9d202..06d9d209
        Erased->Next->Prev = Erased->Prev;                  // 06d9d20b..06d9d211
        ::operator delete(Erased);                          // 06d9d214
        --Header->Size;                                     // 06d9d219..06d9d222
    }
    ::operator delete(Header->Head);                        // 06d9d22b
    Header->Head = nullptr;                                 // 06d9d235
    Header->Size = 0;                                       // 06d9d238
}

// ===========================================================================
// FRpcMarshalStream -- see the derivation in rpc_transport.h.
// ===========================================================================

const double FRpcMarshalStream::kOrientationScale = 127.0;
const double FRpcMarshalStream::kOrientationInvScale = 0.007874015748031496;
const double FRpcMarshalStream::kPercentScale = 255.0;
const double FRpcMarshalStream::kPercentInvScale = 0.00392156862745098;
// DAT_06db6554 is a FLOAT (cd cc cc 3d), widened by the FMUL; DAT_06db6558 is the
// double 14 14 14 14 14 14 a4 3f.
const double FRpcMarshalStream::kTenthScale = 0.10000000149011612;
const double FRpcMarshalStream::kTenthInvScale = 0.0392156862745098;

// 06d97d31..06d97d43: Cursor = 0, Limit = capacity, Ptr = buffer, Error = 0.
FRpcMarshalStream::FRpcMarshalStream(FPacketBase& InPacket, void* InBuffer,
                                     unsigned int InCapacity)
    : Packet(InPacket) {
    Packet.MarshalCursor = 0;
    Packet.MarshalLimit = InCapacity;
    Packet.MarshalPtr = reinterpret_cast<unsigned int>(InBuffer);
    Packet.MarshalError = 0;
}

// The shared prologue: advance, bounds-check, then short-circuit if the stream has
// already failed. 06da3733..06da374c for the encoder, 06da4033..06da404e for the
// decoder. Note the comparison is strict: Cursor == Limit is still in bounds (JBE).
bool FRpcMarshalStream::Advance(unsigned int Width, unsigned int ErrorCode) {
    Packet.MarshalCursor += Width;
    if (Packet.MarshalCursor > Packet.MarshalLimit) {
        Packet.MarshalError = ErrorCode;
    }
    return Packet.MarshalError == 0;
}

// The bare cursor bump, with no access and no pointer move. See the note on the
// declaration: ch1 0x12 PLAYER_TRADE is the codec that needs the two apart.
bool FRpcMarshalStream::Reserve(unsigned int Width, unsigned int ErrorCode) {
    return Advance(Width, ErrorCode);
}

// The `JC` variant. 06da1509 `CMP EAX,ESI` / 06da150d `JC LAB_06da151b` continues only
// while Cursor < Limit, where every other bounds check in the module is `JBE` and lets
// Cursor == Limit through. The store of the advanced cursor happens BEFORE the test in
// both forms (06da150b `MOV dword ptr [EDI],EDX`), so a failure still leaves the cursor
// moved.
bool FRpcMarshalStream::AdvanceStrict(unsigned int Width, unsigned int ErrorCode) {
    Packet.MarshalCursor += Width;
    if (Packet.MarshalCursor >= Packet.MarshalLimit) {
        return false;
    }
    (void)ErrorCode;   // retail returns the code, it does not store it -- see the header
    return true;
}

// FUN_06da14f0, 06da14f0..06da156e.
bool FRpcMarshalStream::PutWideString(const wchar_t* Text) {
    if (!AdvanceStrict(2u, kEncodeOverflow)) {                         // 06da1504..06da150d
        return false;
    }
    unsigned int Length = 0;                                           // 06da1520 wcslen
    if (Text != nullptr) {
        while (Text[Length] != 0) {
            ++Length;
        }
    }
    const unsigned short Count = (unsigned short)(Length & 0xffffu);   // 06da1526 MOV word
    unsigned char* Ptr = reinterpret_cast<unsigned char*>(Packet.MarshalPtr);
    memcpy(Ptr, &Count, 2u);
    // Retail keeps the advanced pointer in EBX and writes it back to the caller's slot
    // exactly ONCE, at 06da1569 on the success path. Both failure exits leave the
    // caller's pointer where it was even though the length word is already in the
    // buffer, so the commit below happens at the end and not here.
    unsigned char* Local = Ptr + 2u;                                   // 06da1537 ADD EBX,0x2
    const unsigned int Bytes = (unsigned int)Count * 2u;               // 06da152e SHL EAX,0x1
    if (!AdvanceStrict(Bytes, kEncodeOverflow)) {                      // 06da1530..06da153e
        return false;
    }
    if (Bytes != 0 && Text != nullptr) {
        memcpy(Local, Text, Bytes);                                    // 06da1557
    }
    Packet.MarshalPtr = reinterpret_cast<unsigned int>(Local + Bytes); // 06da1565/06da1569
    return true;
}

// FUN_06da1580, 06da1580..06da161a.
bool FRpcMarshalStream::GetWideString(wchar_t*& OutText) {
    if (!AdvanceStrict(2u, kDecodeOverrun)) {                          // 06da158f..06da159d
        return false;
    }
    const unsigned char* Ptr = reinterpret_cast<const unsigned char*>(Packet.MarshalPtr);
    unsigned short Count = 0;
    memcpy(&Count, Ptr, 2u);                                           // 06da15ab MOV SI,[EBP]
    const unsigned char* After = Ptr + 2u;                             // 06da15b4 ADD EBP,0x2
    const unsigned int Bytes = (unsigned int)Count * 2u;               // 06da15b7 LEA EBX,[EDX+EDX]
    if (!AdvanceStrict(Bytes, kDecodeOverrun)) {                       // 06da15ba..06da15c0
        return false;
    }
    OutText = nullptr;                                                 // 06da15d5
    if (Count == 0) {                                                  // 06da15ce/06da15db
        // Retail jumps to LAB_06da1614 and never reaches 06da1612, the ONLY store of the
        // advanced buffer pointer -- so the two bytes the length word cost the cursor are
        // not taken off the pointer. Reproduced: MarshalPtr is deliberately left alone.
        return true;
    }
    wchar_t* Block = static_cast<wchar_t*>(
        ::operator new(Bytes + 2u, std::nothrow));                     // 06da15dd/06da15e2, bare
    memcpy(Block, After, Bytes);                                       // 06da15f8
    Block[Count] = 0;                                                  // 06da1607
    OutText = Block;
    Packet.MarshalPtr = reinterpret_cast<unsigned int>(After + Bytes); // 06da1610/06da1612
    return true;
}

bool FRpcMarshalStream::PutDwordUncounted(unsigned int Value) {
    if (Packet.MarshalError != 0) {
        return false;
    }
    unsigned char* Ptr = reinterpret_cast<unsigned char*>(Packet.MarshalPtr);
    memcpy(Ptr, &Value, 4u);
    Packet.MarshalPtr += 4u;
    return true;
}

bool FRpcMarshalStream::GetDwordUncounted(unsigned int& OutValue) {
    if (Packet.MarshalError != 0) {
        return false;
    }
    const unsigned char* Ptr = reinterpret_cast<const unsigned char*>(Packet.MarshalPtr);
    memcpy(&OutValue, Ptr, 4u);
    Packet.MarshalPtr += 4u;
    return true;
}

bool FRpcMarshalStream::PutDword(unsigned int Value) {
    if (!Advance(4u, kEncodeOverflow)) {
        return false;
    }
    unsigned char* Ptr = reinterpret_cast<unsigned char*>(Packet.MarshalPtr);
    memcpy(Ptr, &Value, 4u);
    Packet.MarshalPtr += 4u;
    return true;
}

bool FRpcMarshalStream::PutWord(unsigned short Value) {
    if (!Advance(2u, kEncodeOverflow)) {
        return false;
    }
    unsigned char* Ptr = reinterpret_cast<unsigned char*>(Packet.MarshalPtr);
    memcpy(Ptr, &Value, 2u);
    Packet.MarshalPtr += 2u;
    return true;
}

// See the note on the declaration. No Advance, no check. Two retail spellings, one
// behaviour; the addresses below are the inlined LAB_06da13d0 unless marked otherwise.
void FRpcMarshalStream::PutHeaderUnchecked(unsigned short Value) {
    unsigned char* Ptr = reinterpret_cast<unsigned char*>(Packet.MarshalPtr);
    // 06da1404 MOV word ptr [EAX + -0x2],DX   (out of line: 06da13a2, same operands)
    memcpy(Ptr, &Value, 2u);
    // 06da140f MOV dword ptr [ESI + 0x18],EAX (out of line: 06da13ac MOV dword ptr
    // [ESI],EAX, where ESI is the caller's &MarshalPtr rather than the stream base)
    Packet.MarshalPtr += 2u;
    // 06da1408 MOV dword ptr [ESI + 0x10],0x2 (out of line: 06da13a6 MOV dword ptr
    // [this],0x2, `this` reloaded at 06da138c from param_3 == &MarshalCursor). SET, not
    // advanced -- which is the same thing only because the cursor is 0 at every site.
    Packet.MarshalCursor = 2u;
    // 06da1415 MOV dword ptr [ESI + 0x1c],ECX with ECX zeroed at 06da13e3. Out of line
    // FUN_06da1380 never touches this field; the CALLER does, with the always-zero
    // return value -- 06dae657 MOV dword ptr [EDI + 0x1c],EAX and its 73 siblings.
    Packet.MarshalError = 0u;
}

bool FRpcMarshalStream::PutByte(unsigned char Value) {
    if (!Advance(1u, kEncodeOverflow)) {
        return false;
    }
    unsigned char* Ptr = reinterpret_cast<unsigned char*>(Packet.MarshalPtr);
    *Ptr = Value;
    Packet.MarshalPtr += 1u;
    return true;
}

bool FRpcMarshalStream::PutFloat(float Value) {
    if (!Advance(4u, kEncodeOverflow)) {
        return false;
    }
    unsigned char* Ptr = reinterpret_cast<unsigned char*>(Packet.MarshalPtr);
    memcpy(Ptr, &Value, 4u);
    Packet.MarshalPtr += 4u;
    return true;
}

// 06da38b8..06da38fc: the cursor is advanced by the whole run first, then REP MOVSD /
// REP MOVSB copies it. A zero-length run still costs nothing and cannot fail. The source
// is not tested: REP MOVS reads through whatever ESI holds (06da38f5 / 06da38fc).
bool FRpcMarshalStream::PutBytes(const void* Source, unsigned int Count) {
    if (!Advance(Count, kEncodeOverflow)) {
        return false;
    }
    if (Count != 0) {
        memcpy(reinterpret_cast<unsigned char*>(Packet.MarshalPtr), Source, Count);
    }
    Packet.MarshalPtr += Count;
    return true;
}

// The cursor moves over Count bytes and nothing is written -- the three-byte holes of
// ch2 0x4e SETDIRECTIVE (see rpc_wire_closeout.cpp), which retail fills from its own
// uninitialised stack temporary. Same bounds check as PutBytes.
bool FRpcMarshalStream::PutGap(unsigned int Count) {
    if (!Advance(Count, kEncodeOverflow)) {
        return false;
    }
    Packet.MarshalPtr += Count;
    return true;
}

// 06da398f..06da39ac: FLD the float, FMUL the scale, MSVCRT ftol, store one byte.
// ftol truncates toward zero, so a plain cast to long is the same operation.
bool FRpcMarshalStream::PutQuantized(float Value, double Scale) {
    const long Scaled = static_cast<long>(static_cast<double>(Value) * Scale);
    return PutByte(static_cast<unsigned char>(static_cast<unsigned long>(Scaled) & 0xffu));
}

// 06da5208..06da524d, the FIREWEAPON middle tier: the same truncating ftol, stored as a
// word instead of a byte.
bool FRpcMarshalStream::PutQuantizedWord(float Value, double Scale) {
    const long Scaled = static_cast<long>(static_cast<double>(Value) * Scale);
    return PutWord(static_cast<unsigned short>(static_cast<unsigned long>(Scaled) & 0xffffu));
}

// 06dad26a..06dad27e, the SENDCOMM tier: FLD the float, FMUL the float 0.1f, FMUL the
// double 255.0, ftol, store one byte. Written as two separate multiplies on purpose --
// see the note on the declaration.
bool FRpcMarshalStream::PutQuantizedTwoStage(float Value, double Scale1, double Scale2) {
    const double Stage1 = static_cast<double>(Value) * Scale1;   // 06dad26d
    const long Scaled = static_cast<long>(Stage1 * Scale2);      // 06dad278 + ftol
    return PutByte(static_cast<unsigned char>(static_cast<unsigned long>(Scaled) & 0xffu));
}

bool FRpcMarshalStream::GetDword(unsigned int& OutValue) {
    if (!Advance(4u, kDecodeOverrun)) {
        return false;
    }
    const unsigned char* Ptr = reinterpret_cast<const unsigned char*>(Packet.MarshalPtr);
    memcpy(&OutValue, Ptr, 4u);
    Packet.MarshalPtr += 4u;
    return true;
}

bool FRpcMarshalStream::GetWord(unsigned short& OutValue) {
    if (!Advance(2u, kDecodeOverrun)) {
        return false;
    }
    const unsigned char* Ptr = reinterpret_cast<const unsigned char*>(Packet.MarshalPtr);
    memcpy(&OutValue, Ptr, 2u);
    Packet.MarshalPtr += 2u;
    return true;
}

bool FRpcMarshalStream::GetByte(unsigned char& OutValue) {
    if (!Advance(1u, kDecodeOverrun)) {
        return false;
    }
    const unsigned char* Ptr = reinterpret_cast<const unsigned char*>(Packet.MarshalPtr);
    OutValue = *Ptr;
    Packet.MarshalPtr += 1u;
    return true;
}

bool FRpcMarshalStream::GetFloat(float& OutValue) {
    if (!Advance(4u, kDecodeOverrun)) {
        return false;
    }
    const unsigned char* Ptr = reinterpret_cast<const unsigned char*>(Packet.MarshalPtr);
    memcpy(&OutValue, Ptr, 4u);
    Packet.MarshalPtr += 4u;
    return true;
}

// The destination is not tested either: every retail decoder REP MOVSes into it once the
// bounds check has passed (e.g. 06d9891f, ch2 0x3a's name buffer at packet+0x38).
bool FRpcMarshalStream::GetBytes(void* Dest, unsigned int Count) {
    if (!Advance(Count, kDecodeOverrun)) {
        return false;
    }
    if (Count != 0) {
        memcpy(Dest, reinterpret_cast<const unsigned char*>(Packet.MarshalPtr), Count);
    }
    Packet.MarshalPtr += Count;
    return true;
}

// The decode-side twin of PutGap: consumes Count bytes and stores none of them.
bool FRpcMarshalStream::GetGap(unsigned int Count) {
    if (!Advance(Count, kDecodeOverrun)) {
        return false;
    }
    Packet.MarshalPtr += Count;
    return true;
}

// The inverse of PutQuantized. Retail reads the byte back as a SIGNED quantity --
// 06da42ed `MOVSX EAX,byte ptr [...]`, 06da42f6 `MOVSX EAX,BL`, 06da4307 `MOVSX EDX,DL`
// -- then FILDs it and multiplies by the reciprocal constant (06da4301 FMUL
// DAT_06db3fe0). Sign extension is what makes the 127.0 scale cover [-1,1].
bool FRpcMarshalStream::GetQuantizedSigned(float& OutValue, double InvScale) {
    unsigned char Raw = 0;
    if (!GetByte(Raw)) {
        return false;
    }
    const double Signed = static_cast<double>(static_cast<signed char>(Raw));
    OutValue = static_cast<float>(Signed * InvScale);
    return true;
}

// 06da573a..06da5755: `MOV CX,word ptr [EAX]` then `MOVSX EAX,CX`, FILD, FMUL the
// reciprocal. The sign extension is what makes the 32767.0 scale cover [-1,+1].
bool FRpcMarshalStream::GetQuantizedWordSigned(float& OutValue, double InvScale) {
    unsigned short Raw = 0;
    if (!GetWord(Raw)) {
        return false;
    }
    const double Signed = static_cast<double>(static_cast<short>(Raw));
    OutValue = static_cast<float>(Signed * InvScale);
    return true;
}

// The 255.0 family. 06da452b `MOVZX ECX,CL` (equipment status) and 06da4804
// `XOR EAX,EAX ; MOV AL,CL` (collision-group health) both widen WITHOUT sign, so the
// byte covers [0,+1] and 0xff is +1.0, not -1/127.
bool FRpcMarshalStream::GetQuantizedUnsigned(float& OutValue, double InvScale) {
    unsigned char Raw = 0;
    if (!GetByte(Raw)) {
        return false;
    }
    const double Unsigned = static_cast<double>(static_cast<unsigned int>(Raw));
    OutValue = static_cast<float>(Unsigned * InvScale);
    return true;
}

// ===========================================================================
// FPacketSerializer -- the retail packet-type registry.
// Registration call: FUN_06d960a0(Serializer, Channel << 16 | Opcode, Name, Factory).
// ===========================================================================

// The body FUN_06d95e50 inlines after operator new(0x1c) succeeded:
//   06d95e92 [+0x04] = 1 and 06d95e99 vptr 06db3c54  -> FRpcRefObject()
//   06d95eae..06d95ef9 the empty map at +0x08 (allocator/compare bytes, head node, size 0)
//                                                    -> the std::map member
//   06d95f01 vptr 06db3e18                           -> this class's vtable
//   06d95f07 CALL FUN_06da2820, the hardpoint-name dictionary the CREATESHIP /
//            CREATESOLAR equipment codec reads.
FPacketSerializer::FPacketSerializer() {
    RpcBuildHardpointDictionary();
}

// FUN_06d95f30. 06d95f52 installs this class's vptr again; the loop at 06d95f70..06d95fc5
// visits every node, frees the record's std::string buffer and then the record itself
// (06d95fa4), and nulls the node's value (06d95fb4); 06d95fc7..06d96070 then erase every
// node and free the head; 06d9607b CALL FUN_06d8f4c0 is the FRpcRefObject destructor.
// Deleting the record runs the same std::string destructor; the std::map member and the
// base destructor do the rest. The scalar deleting destructor FUN_06d964c0 (vtable
// 06db3e18 slot 0: CALL FUN_06d95f30 / TEST [ESP+4],1 / JZ / operator delete / RET 4) is
// what the compiler emits for this virtual destructor, reached through `delete this` in
// FRpcRefObject::Release.
FPacketSerializer::~FPacketSerializer() {
    for (std::map<unsigned int, FPacketTypeInfo*>::iterator It = Types.begin();
         It != Types.end(); ++It) {
        delete It->second;                              // 06d95f77..06d95fa4
        It->second = nullptr;                           // 06d95fb4
    }
}

// retail .data:DAT_06dbbe38 -- the serializer singleton's cache. Written once, at
// 06d95f13, and never cleared; see RpcGetPacketSerializer in the header.
static FPacketSerializer* GRpcPacketSerializer = nullptr;

// FUN_06d95e50, every branch:
//   06d95e5e MOV EAX,[DAT_06dbbe38] / 06d95e70 CMP EAX,0 / 06d95e72 JNZ 06d95f19 -> return it
//   06d95e79 PUSH 0x1c / operator_new / 06d95e8f JZ 06d95f0f -> ESI = 0
//   06d95e92..06d95f07 construct (see the constructor above)
//   06d95f11 MOV EAX,ESI / 06d95f13 MOV [DAT_06dbbe38],EAX -- published, null included
//   06d95f28 RET
// MSVCRT's operator new of the time answered null instead of throwing; nothrow new is
// the same contract.
FPacketSerializer* RpcGetPacketSerializer() {
    if (GRpcPacketSerializer != nullptr) {
        return GRpcPacketSerializer;
    }
    GRpcPacketSerializer = new (std::nothrow) FPacketSerializer();
    return GRpcPacketSerializer;
}

// FUN_06d960a0. 06d960a7..06d960e0: lower_bound on the id (FUN_06d96e60) and, when
// the node found is not the head and its key equals the id, 06d960e4 `MOV EAX,0x42`.
// Otherwise 06d960f0 `PUSH 0x14` / operator_new, the factory at +0x00 (06d9612b), the
// name assigned into the std::string at +0x04 (06d96140), the pointer inserted under
// the id, and 06d9619c `XOR EAX,EAX`. Nothing is range-checked.
int FPacketSerializer::RegisterType(unsigned int PacketId, const char* Name,
                                    FPacketFactoryFn Factory) {
    std::map<unsigned int, FPacketTypeInfo*>::iterator It = Types.lower_bound(PacketId);
    if (It != Types.end() && It->first == PacketId) {
        return kPacketRegisterDuplicate;                // 06d960e4 MOV EAX,0x42
    }
    FPacketTypeInfo* Info = new FPacketTypeInfo;        // 06d960f0 PUSH 0x14
    Info->Factory = Factory;                            // 06d9612b
    Info->Name = Name;                                  // 06d96140
    Types.insert(It, std::map<unsigned int, FPacketTypeInfo*>::value_type(PacketId, Info));
    return 0;                                           // 06d9619c XOR EAX,EAX
}

// Retail FUN_06d96300(Serializer, Channel, Opcode, &OutPacket):
//   06d96305 TEST out / 06d9630a JNZ ; 06d9630d MOV EAX,0x4 ; RET 0xc
//   06d96316..06d96324  id = (MOVZX Channel word << 16) | MOVZX Opcode word
//   06d96334..06d96361  lower_bound (FUN_06d96e60) plus the key compare = find; the
//                       head node -> 06d96372 MOV EAX,0x19
//   06d96363 MOV EAX,[node+0x10] (the record) / 06d96366 PUSH out / 06d96367 CALL [EAX]
//   and the factory's own answer is returned as it stands.
// *OutPacket is not pre-cleared here: every factory does that itself (06d9af77).
int FPacketSerializer::Allocate(unsigned int Channel, unsigned int Opcode,
                                FPacketBase** OutPacket) {
    if (OutPacket == nullptr) {
        return kPacketAllocateNullOut;                  // 06d9630d MOV EAX,0x4
    }
    const unsigned int PacketId = ((Channel & 0xffffu) << 16) | (Opcode & 0xffffu);
    std::map<unsigned int, FPacketTypeInfo*>::const_iterator It = Types.find(PacketId);
    if (It == Types.end()) {
        return kPacketAllocateUnknownType;              // 06d96372 MOV EAX,0x19
    }
    return It->second->Factory(OutPacket);              // 06d96367 CALL dword ptr [EAX]
}

// Retail FUN_06d961b0 -- IServerImpl::DumpPacketStats (0x06d8f480) and IClient slot 125
// are the same function and both land here.
//
// THE ARGUMENT IS A FILE NAME, NOT A LABEL. 06d961b9 `MOV EAX,[EBP+param_1+0x4]`,
// 06d961c1 `PUSH .rdata:DAT_06db3ed0` (the one-character string "w"), 06d961c6
// `PUSH EAX`, 06d961cb `CALL [->MSVCRT.DLL::fopen]` -- the parameter goes straight in
// as fopen's path and the whole body is skipped when the open fails (06d961d8 `JZ`).
//
// ORDER. 06d961ea `MOV EAX,[ESI+0xc]` takes the tree head, 06d961ed `MOV EDI,[EAX]`
// its leftmost node, and the loop at 06d96200 reads the key `[EDI+0xc]` and the record
// `[EDI+0x10]` before stepping to the successor -- ascending id, which is what
// iterating the std::map gives. For each entry it asks FUN_06da16b0 for the counters
// (06d9620b, inserting a zeroed record for an id never counted) and skips the row only
// when OutCount and InCount are both zero (06d96217..06d96220).
void FPacketSerializer::DumpStats(const char* Name) const {
    FILE* File = fopen(Name, "w");                      // 06d961cb
    if (File == nullptr) {
        return;                                         // 06d961d8
    }
    fprintf(File,                                       // 06d961e4, string .rdata:06db3e40
            "Packet Name,In Count,In Max Size,In Avg Size,In Total Bytes,"
            "Exec Time Total,Exec Time Avg,"
            "Out Count,Out Max Size,Out Avg Size,Out Total Bytes\n\n");
    for (std::map<unsigned int, FPacketTypeInfo*>::const_iterator It = Types.begin();
         It != Types.end(); ++It) {
        const FPacketTypeInfo* Info = It->second;       // 06d96203 [EDI+0x10]
        const FPacketWireStats* Stats = RpcFindWireStats(It->first);   // 06d9620b
        if (Stats->OutCount == 0 && Stats->InCount == 0) {
            continue;                                   // 06d96217 / 06d9621b
        }
        // 06d96226..06d9622d: the std::string's data pointer, MSVCP60's shared empty
        // buffer when it is null. The format at .rdata:06db3e3c is "%s," WITH the comma.
        fprintf(File, "%s,", Info->Name.c_str());       // 06d96239
        fprintf(File, "%u,%u,%u,%u,%f,%f,",             // 06d96265
                Stats->InCount, Stats->InMaxSize, Stats->InAvgSize, Stats->InTotalBytes,
                Stats->ExecTimeTotal, Stats->ExecTimeAvg);
        fprintf(File, "%u,%u,%u,%u",                    // 06d96286
                Stats->OutCount, Stats->OutMaxSize, Stats->OutAvgSize, Stats->OutTotalBytes);
        fprintf(File, "\n");                            // 06d96291
    }
    fclose(File);
}

// ===========================================================================
// The codec statistics tails -- retail FUN_06da16b0 plus the inline accumulator every
// Encode and Decode slot ends with. See the derivation above FPacketWireStats.
// ===========================================================================

// retail .data:06dbbf4c
std::map<unsigned int, FPacketWireStats> GRpcPacketStats;

// FUN_06da16b0: FUN_06da1730 on the map at .data:06dbbf4c with the id as the key --
// std::map::operator[], so an id seen for the first time gets a zeroed record.
FPacketWireStats* RpcFindWireStats(unsigned int PacketId) {
    return &GRpcPacketStats[PacketId];
}

void RpcRecordEncodedPacket(unsigned int PacketId, unsigned int Length) {
    FPacketWireStats* Stats = RpcFindWireStats(PacketId);
    ++Stats->OutCount;                                  // 06da4c66 INC EBP
    Stats->OutTotalBytes += Length;                     // 06da4c6b ADD EBX,EDX
    if (Length > Stats->OutMaxSize) {                   // 06da4c72 CMP ESI,EAX ; JBE
        Stats->OutMaxSize = Length;                     // 06da4c78
    }
    Stats->OutAvgSize = Stats->OutTotalBytes / Stats->OutCount;   // 06da4c80 DIV EDI
}

void RpcRecordDecodedPacket(unsigned int PacketId, unsigned int Length) {
    FPacketWireStats* Stats = RpcFindWireStats(PacketId);
    // The decoder's own length excludes the two header bytes, so every term gets +2.
    const unsigned int Accounted = Length + 2u;         // 06da4ef7 ADD EDX,0x2
    ++Stats->InCount;                                   // 06da4eef INC EDX
    Stats->InTotalBytes += Accounted;                   // 06da4efa ADD EBP,EDX
    // RETAIL COMPARES THE UNADJUSTED LENGTH AGAINST Max + 2 AND THEN STORES Length + 2,
    // so the new maximum has to beat the old one by four, not by one. Verified twice --
    // 06da4eff `MOV ESI,[ESI]` / 06da4f01 `ADD EAX,0x2` / 06da4f04 `CMP ESI,EAX` /
    // 06da4f06 `JBE` / 06da4f08 `ADD ESI,0x2`, and identically at 06d9313b..06d93147 in
    // the CREATELOOT decoder. Kept as written; "correcting" it would be an invented
    // condition.
    if (Length > Stats->InMaxSize + 2u) {               // 06da4f04 CMP ESI,EAX ; JBE
        Stats->InMaxSize = Accounted;                   // 06da4f08 ADD ESI,0x2 ; 06da4f0b
    }
    Stats->InAvgSize = Stats->InTotalBytes / Stats->InCount;      // 06da4f13 DIV EDI
}

// The three per-channel registrators live in their own translation unit,
// src/rpclocal/rpc_packet_registry.cpp.

// ===========================================================================
// FPacketQueue / FRpcConnection -- retail CDAConnection (0x30 bytes) and its four
// queues. The queue stores the packet POINTER by value inside its own std::deque block
// (FUN_06d8cb70 is `*dest = *src`); CDAPacket+0x08 is NOT a link. Full layout evidence
// is in the header.
// ===========================================================================

// --- FPacketQueue: retail's operator_new(0x30) queue object -------------------------
// The mode byte at +0x00 comes from FUN_06d85b60's argument (06d85bc8 `MOV [ESI],AL`);
// everything above it is an MSVC6 std::deque<CDAPacket*>.
FPacketQueue::FPacketQueue(unsigned char InMode) : Mode(InMode) {
}

void FPacketQueue::Push(FPacketBase* Packet) {
    // No null test: neither FUN_06d85e00 nor FUN_06d86150 looks at the packet before
    // storing it. Retail push_back: the map block is grown by FUN_06d868e0 when the write iterator
    // reaches the end of the map (06d85ee6 `CMP EAX,ECX` / `JNC LAB_06d85f31`), then
    // FUN_06d8cb70 copies the pointer into the block and +0x2c is incremented
    // (06d8611f..06d86127). std::deque does all of that for us.
    Packets.push_back(Packet);
}

FPacketBase* FPacketQueue::Pop() {
    if (Packets.empty()) {
        return nullptr;
    }
    // Retail pop: EBX = *_Next (06d864e0 `MOV EBX,[EAX]`), _Next += 4 and --_Size
    // (06d864e4 / 06d864e9), then the exhausted block is freed (06d864ff) and, when the
    // deque went empty, the map itself (06d86539).
    FPacketBase* Packet = Packets.front();
    Packets.pop_front();
    return Packet;
}

// One arm of retail FUN_06d857c0 (the +0x0c arm, 06d857d6..06d8583b, as the model):
//   06d857dd MOV EAX,[queue+0x2c] / TEST / JBE -> done        ; size == 0
//   06d857f7 MOV ESI,[front] / 06d857f9 TEST ESI,ESI / JZ -> done   ; a null front STOPS it
//   06d85804 CALL FUN_06d86660                                ; pop_front
//   06d8580b PUSH 0x1 / 06d8580f CALL [EAX]                   ; scalar deleting dtor
//   06d85814 size again / JBE -> done, else loop
// The packet is DELETED through slot 0, not released: its use count is not consulted.
void FPacketQueue::DestroyAll() {
    while (!Packets.empty()) {
        FPacketBase* Packet = Packets.front();
        if (Packet == nullptr) {
            break;
        }
        Packets.pop_front();
        delete Packet;
    }
}

// --- FRpcConnection: retail's operator_new(0x30) CDAConnection ----------------------
// FUN_06d88280 zeroes +0x00..+0x10 and the byte at +0x14 and stops there.
FRpcConnection::FRpcConnection()
    : ServerBoundRead(nullptr)
    , ServerBoundWrite(nullptr)
    , ClientBoundRead(nullptr)
    , ClientBoundWrite(nullptr)
    , Mode(0)
    , bStarted(false) {
    // +0x18..+0x2f (Lock) is left as raw heap, as FUN_06d88280 leaves it.
}

// Retail FUN_06d85b60. Argument is `uint`, pushed by IServerImpl::Connect as 1
// (06d8f3a7 `PUSH 0x1`); the body tests bit 0 then bit 1.
unsigned int FRpcConnection::Start(unsigned int InMode) {
    // 06d85b7b  MOV AL,[EDI+0x14] / 06d85b82 JZ -> otherwise MOV EAX,0x3 and return.
    if (bStarted) {
        return kStartAlreadyOn;
    }

    const unsigned char ModeByte = static_cast<unsigned char>(InMode);

    if ((InMode & kModeSingleBuffered) != 0) {
        // 06d85ba9..06d85c37: two queues, at +0x00 and +0x08.
        ServerBoundRead = new (std::nothrow) FPacketQueue(ModeByte);
        ClientBoundRead = new (std::nothrow) FPacketQueue(ModeByte);
        if (ServerBoundRead == nullptr || ClientBoundRead == nullptr) {
            return kStartAllocFailed;           // 06d85c46 MOV EAX,0x2
        }
    } else if ((InMode & kModeDoubleBuffered) != 0) {
        // 06d85c65..06d85d8f: four queues. Retail's allocation order is +0x00, +0x08,
        // +0x0c, +0x04 and it checks all four before succeeding (06d85d8d..06d85dac).
        ServerBoundRead  = new (std::nothrow) FPacketQueue(ModeByte);
        ClientBoundRead  = new (std::nothrow) FPacketQueue(ModeByte);
        ClientBoundWrite = new (std::nothrow) FPacketQueue(ModeByte);
        ServerBoundWrite = new (std::nothrow) FPacketQueue(ModeByte);
        if (ServerBoundRead == nullptr || ClientBoundRead == nullptr ||
            ClientBoundWrite == nullptr || ServerBoundWrite == nullptr) {
            return kStartAllocFailed;
        }
    } else {
        return kStartBadMode;                   // 06d85de3 MOV EAX,0x1
    }

    // 06d85db2..06d85dc9: InitializeCriticalSection(conn+0x18), started = 1,
    // conn+0x10 = the mode, return 0.
    InitializeCriticalSection(&Lock);
    bStarted = true;
    Mode = InMode;
    return kStartOk;
}

// One arm of retail FUN_06d857c0: `if (queue)` (06d857d9 TEST / JZ), drain it destroying
// every packet, then the deque destructor (FUN_06d85ac0, 06d85844) and the free of the
// queue object itself (FUN_06db1dc2, 06d8584a). The member is not cleared -- the whole
// connection is freed right after.
void FRpcConnection::DestroyQueue(FPacketQueue* Queue) {
    if (Queue == nullptr) {
        return;
    }
    Queue->DestroyAll();
    delete Queue;
}

// Retail FUN_06d857c0, the destructor IServerImpl::Shutdown runs before the free
// (06d8f349 / 06d8f34f): DeleteCriticalSection first and unconditionally (06d857c8 LEA
// EAX,[EBP+0x18] / 06d857d0), then the four queues in the order +0x0c (06d857d6), +0x04
// (06d85852), +0x08 (06d858ed), +0x00 (06d85988), then RET (06d85ab7). It touches no
// other field.
FRpcConnection::~FRpcConnection() {
    DeleteCriticalSection(&Lock);
    DestroyQueue(ClientBoundWrite);
    DestroyQueue(ServerBoundWrite);
    DestroyQueue(ClientBoundRead);
    DestroyQueue(ServerBoundRead);
}

// Retail FUN_06d86150: the write queue when there is one, otherwise the read queue;
// 4 when neither exists. IServerImpl::PushToServer (0x06d8f460) tail-jumps into it and
// drops the packet outright when DAT_06dbbd58 is null (06d8f468 `JZ LAB_06d8f46f`).
// 06d86155 [conn+0x04] / TEST / JZ LAB_06d86302 -> [conn+0x00] / TEST / JZ -> 4
// (06d86488); otherwise push_back and 0. No packet test, no lock.
unsigned int FRpcConnection::PushServerBound(FPacketBase* Packet) {
    FPacketQueue* Queue = (ServerBoundWrite != nullptr) ? ServerBoundWrite : ServerBoundRead;
    if (Queue == nullptr) {
        return kPushNoQueue;
    }
    Queue->Push(Packet);
    return kPushOk;
}

// Retail FUN_06d86580 -- reads [conn+0x00] only; drained by IClient slot 16 (0x06d8efc0).
// Null queue or size 0 -> 0, the same two tests as FUN_06d864a0 below. No lock.
FPacketBase* FRpcConnection::PopServerBound() {
    return (ServerBoundRead != nullptr) ? ServerBoundRead->Pop() : nullptr;
}

// Retail FUN_06d85e00, called from IClient slot 9 (0x06d8f080).
// 06d85e05 [conn+0x0c] / TEST / JZ LAB_06d85fb2 -> [conn+0x08] / TEST / JZ 06d86134
// (MOV EAX,0x4); otherwise push_back (FUN_06d8cb70 at 06d8611a, ++size at 06d86127) and
// 06d8612b XOR EAX,EAX. No packet test, no lock.
unsigned int FRpcConnection::PushClientBound(FPacketBase* Packet) {
    FPacketQueue* Queue = (ClientBoundWrite != nullptr) ? ClientBoundWrite : ClientBoundRead;
    if (Queue == nullptr) {
        return kPushNoQueue;
    }
    Queue->Push(Packet);
    return kPushOk;
}

// Retail FUN_06d864a0 -- reads [conn+0x08] only; drained by IServerImpl::Update.
// 06d864a4 [conn+0x08] / 06d864a7 TEST / JZ -> 0 ; 06d864af [queue+0x2c] / JZ -> 0 ;
// otherwise the front, popped. No lock.
FPacketBase* FRpcConnection::PopClientBound() {
    return (ClientBoundRead != nullptr) ? ClientBoundRead->Pop() : nullptr;
}

// IServerImpl::Update reads the count straight out of the queue before calling
// FUN_06d864a0 (06d8f416 `MOV EDX,[EAX+0x2c]`), and IClient slot 16 does the same at
// 06d8f032. Both read the READ queue.
unsigned int FRpcConnection::NumServerBound() const {
    return (ServerBoundRead != nullptr) ? ServerBoundRead->Num() : 0u;
}

unsigned int FRpcConnection::NumClientBound() const {
    return (ClientBoundRead != nullptr) ? ClientBoundRead->Num() : 0u;
}

// ===========================================================================
// Module-wide transport state (retail keeps the same things in .data)
// ===========================================================================
FRpcConnection* GRpcConnection = nullptr;        // retail .data:DAT_06dbbb5c
IClient* GRealClient = nullptr;                  // retail .data:0x06dbbcd8
IChatClient* GRealChatClient = nullptr;          // retail .data:0x06dbbcd4
char GCallerModulePath[260] = { 0 };             // retail .data:0x06dbbbc0

// Retail FUN_06d88280 verbatim: return the cached singleton, creating it on the first
// call and caching null again when operator new fails (06d882b2..06d882b4). It does not
// start the connection.
//   06d88280 MOV EAX,[DAT_06dbbb5c] / 06d8828a JNZ 06d882b9 -> return it
//   06d8828c operator_new(0x30) / 06d88298 JZ 06d882b2 -> XOR EAX,EAX / cache 0
//   06d8829a..06d882a8 the five zero stores (the constructor) / 06d882ab cache it
FRpcConnection* RpcCreateConnection() {
    if (GRpcConnection == nullptr) {
        GRpcConnection = new (std::nothrow) FRpcConnection();
    }
    return GRpcConnection;
}
