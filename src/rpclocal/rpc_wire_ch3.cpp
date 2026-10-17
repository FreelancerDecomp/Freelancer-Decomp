// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_wire_ch3.cpp -- the nine channel-3 packet codec slots that were still on the
// fallback path, plus the two channel-3 types whose payloads are near-twins.
//
// Every slot address below was read out of .rdata at the vtable the type's constructor
// publishes, and the constructor's LAST vtable store is the one that counts -- ch3 0x03
// MUNCOLLISION writes .rdata:PTR_FUN_06db4fbc at 06da07a7 and only then the real
// .rdata:PTR_FUN_06db5b54 at 06da07cc. Thirteen of the 162 constructors do that.
//
//   ch3 0x01 LOGIN            vtable 06db5ca4  enc LAB_06dafb10  dec FUN_06dafd40
//   ch3 0x03 MUNCOLLISION     vtable 06db5b54  enc LAB_06dae280  dec LAB_06dae430
//   ch3 0x0c OBJCOLLISION     vtable 06db5b74  enc LAB_06dae770  dec LAB_06dae910
//   ch3 0x18 REQADDITEM       vtable 06db4054  enc LAB_06d99d90  dec LAB_06d99f80
//   ch3 0x1a REQMODIFYITEM    vtable 06db4070  enc LAB_06d9a220  dec LAB_06d9a410
//   ch3 0x1e SAVEGAME         vtable 06db40a8  enc LAB_06d9a800  dec LAB_06d9a950
//   ch3 0x20 MINEASTEROID     vtable 06db5b84  enc LAB_06daeac0  dec LAB_06daeca0
//   ch3 0x31 REQUEST_EVENT    vtable 06db5dd4  enc LAB_06daa240  dec LAB_06daa3e0
//   ch3 0x39 CREATENEWCHAR    vtable 06db5c74  enc LAB_06daf410  dec LAB_06daf5b0
//
// ===========================================================================
// REQADDITEM AND REQMODIFYITEM ARE NOT THE SAME BODY -- and the way they are not is
// exactly the trap.
//
// Their four bodies measure 221 / 221 / 186 / 186 instructions, pair for pair, and every
// one of the 221 opcodes matches its opposite number. Diffing them position by position
// leaves nine differing positions in the encoders, of which two are the CALL rel32
// displacements to the shared helpers and one is the routing header, so SIX are real:
//
//   pos  ch3 0x18                                   ch3 0x1a
//    48  06d99e03 MOV ECX,dword ptr [EBP + 0x20]    06d9a293 MOV ECX,dword ptr [EBP + 0x28]
//    70  06d99e32 MOV EDX,dword ptr [EBP + 0x24]    06d9a2c2 MOV EDX,dword ptr [EBP + 0x20]
//   136  06d99ebc MOV EDX,dword ptr [EBP + 0x28]    06d9a34c MOV EDX,dword ptr [EBP + 0x24]
//   140  06d99ec6 MOV EAX,dword ptr [EBP + 0x28]    06d9a356 MOV EAX,dword ptr [EBP + 0x24]
//   160  06d99eef MOV EAX,dword ptr [EBP + 0x28]    06d9a37f MOV EAX,dword ptr [EBP + 0x24]
//
// So the three dwords at packet+0x20 / +0x24 / +0x28 go out in the order (0x20, 0x24,
// 0x28) for REQADDITEM and (0x28, 0x20, 0x24) for REQMODIFYITEM, and the field that
// doubles as the trailing block's length is packet+0x28 in one and packet+0x24 in the
// other. Serialising either in declaration order would put the wrong dword first and
// give the block the wrong length. The decoders carry the same permutation
// (06d99fee/06d9a016/06d9a08e against 06d9a47e/06d9a4a6/06d9a51e), so the two
// directions agree with each other and only the two TYPES differ.
//
// This is not a defect in retail: FPayloadClientReqAddItem already declares
// HardpointSize at payload+0x08 and FPayloadClientReqModifyItem declares it at
// payload+0x04, from the SENDERS, which were read in an earlier wave. Two independent
// readings of the same permutation.
//
// ch2 0x55 OBJECTCARGOUPDATE and ch2 0x57 SET_MUSIC are the other equal-length pair
// (207 / 166 both ways); they are in rpc_wire_closeout.cpp and are genuinely different
// field lists, not a permutation.
// ===========================================================================

#include "rpc_containers.h"
#include "rpc_payloads.h"

#include <new>
#include <cstring>
#include <cwchar>

// wcslen / wcscpy spelled out the way the rest of this port spells them (see
// RpcCh3WideLength in rpc_payload_ch3.cpp): retail reaches MSVCRT for them, and the
// loops below compute the same thing without dragging the _CRT_SECURE_NO_WARNINGS
// switch into the project.
static unsigned int RpcWideLength(const wchar_t* Text) {
    unsigned int Length = 0;
    if (Text == nullptr) {
        return 0;
    }
    while (Text[Length] != 0) {
        ++Length;
    }
    return Length;
}


// ===========================================================================
// ch3 0x01 FLPACKET_CLIENT_LOGIN -- LAB_06dafb10 (220) / FUN_06dafd40 (197).
//
// Wire: { byte CreateFlag, byte LenA, byte LenB, LenA wide chars, LenB wide chars }.
//
// THREE THINGS RETAIL DOES HERE THAT LOOK LIKE MISTAKES AND ARE NOT:
//
//  * the two lengths are wcslen TRUNCATED TO A BYTE and the NUL is NOT counted --
//    06dafb9b `CALL wcslen` then 06dafb9d `MOV byte ptr [ESP + 0x1c],AL`, AL only. A
//    name of 256 wide characters therefore travels as length 0, i.e. as no name.
//  * the encoder LOWERCASES THE PACKET IN PLACE before sending: 06dafc19
//    `LEA ESI,[EBX + 0x20]` / 06dafc1d `CALL _wcslwr`. It is the account name, not a
//    copy of it, so a second Encode of the same packet sees the lowered text. Kept,
//    because a copy would change what a re-sent packet contains.
//  * only the account name is lowered. The password at packet+0x220 is not.
//
// The decoder zeroes BOTH buffers first (06dafd87..06dafd93, `MOV ECX,0x100 ; REP STOSD
// ; STOSW` from packet+0x20 -- 0x402 bytes) and NUL-terminates each string after the
// copy (06dafe60 and 06dafec4, `MOV word ptr [EBX + reg*0x2 + 0x20],0x0` and
// `... + 0x220],0x0`).
// ===========================================================================
unsigned int RpcEncodeClientLogin(FPacketBase& Packet, unsigned char** BufferHolder,
                                  unsigned int Capacity, unsigned int* OutLength) {
    FPayloadClientLogin* Wire =
        reinterpret_cast<FPayloadClientLogin*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06dafb42
    if (!Stream.PutByte(Wire->CreateFlag)) {                              // 06dafb86/06dafb92
        return Stream.GetError();
    }
    // 06dafb9b / 06dafba8, both results kept in AL only.
    const unsigned char LenA =
        (unsigned char)(RpcWideLength(Wire->AccountName) & 0xffu);
    const unsigned char LenB =
        (unsigned char)(RpcWideLength(Wire->Password) & 0xffu);
    if (!Stream.PutByte(LenA)) {                                          // 06dafbe1
        return Stream.GetError();
    }
    if (!Stream.PutByte(LenB)) {                                          // 06dafc10
        return Stream.GetError();
    }
    if (LenA != 0) {                                                      // 06dafc17
        // MSVCRT `_wcslwr` through IAT .rdata:06db320c, on the packet itself.
        _wcslwr(Wire->AccountName);                                       // 06dafc1d
        // 06dafc2e `SHL EAX,0x1` -- the reservation and the copy are both 2*LenA.
        if (!Stream.PutBytes(Wire->AccountName, (unsigned int)LenA * 2u)) {  // 06dafc63
            return Stream.GetError();
        }
    }
    if (LenB != 0) {                                                      // 06dafc7b
        if (!Stream.PutBytes(Wire->Password, (unsigned int)LenB * 2u)) {     // 06dafcc1
            return Stream.GetError();
        }
    }
    // 06dafcde FUN_06da13c0 -- writes nothing.
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06dafd03
    return 0;
}

unsigned int RpcDecodeClientLogin(FPacketBase& Packet, const unsigned char* Buffer,
                                  unsigned int Size, unsigned int* OutLength) {
    FPayloadClientLogin* Wire =
        reinterpret_cast<FPayloadClientLogin*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                         // 06dafd78
    }
    // 06dafd87..06dafd93: 0x100 dwords plus one word from packet+0x20, i.e. 0x402 bytes
    // -- both wide buffers and the two bytes after them. The CreateFlag at payload+0x400
    // is inside that run and is overwritten by the read below.
    ::memset(Wire->AccountName, 0, 0x402u);
    if (!Stream.GetByte(Wire->CreateFlag)) {                              // 06dafdba
        return Stream.GetError();
    }
    unsigned char LenA = 0;
    if (!Stream.GetByte(LenA)) {                                          // 06dafde5
        return Stream.GetError();
    }
    unsigned char LenB = 0;
    if (!Stream.GetByte(LenB)) {                                          // 06dafe08
        return Stream.GetError();
    }
    if (LenA != 0) {                                                      // 06dafe13
        if (!Stream.GetBytes(Wire->AccountName, (unsigned int)LenA * 2u)) {  // 06dafe49
            return Stream.GetError();
        }
        Wire->AccountName[LenA] = L'\0';                                  // 06dafe60
    }
    if (LenB != 0) {                                                      // 06dafe6e
        if (!Stream.GetBytes(Wire->Password, (unsigned int)LenB * 2u)) {     // 06dafeae
            return Stream.GetError();
        }
        Wire->Password[LenB] = L'\0';                                     // 06dafec4
    }
    // 06dafedf FUN_06da13c0 -- consumes nothing.
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06daff02
    return 0;
}

// ===========================================================================
// ch3 0x03 FLPACKET_CLIENT_MUNCOLLISION -- LAB_06dae280 (168) / LAB_06dae430 (158).
// Flat: dword, dword, dword, WORD, Vector. The word is the only 2-byte field and it
// leaves payload+0x0e as a hole neither side touches.
// ===========================================================================
unsigned int RpcEncodeClientMunCollision(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadClientMunCollision* Wire =
        reinterpret_cast<const FPayloadClientMunCollision*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06dae2b4
    Stream.PutDword(Wire->Field_0x00);                                    // 06dae2f4
    Stream.PutDword(Wire->Field_0x04);                                    // 06dae31e
    Stream.PutDword(Wire->ObjectId);                                      // 06dae348
    Stream.PutWord(Wire->Field_0x0c);                                     // 06dae372
    // 06dae383 reserves 0xc, 06dae39c..06dae3a4 load the three dwords into a stack temp
    // and 06dae3ba `CALL FUN_06d8f510` copies them out -- the three-dword copier, not a
    // memcpy of the packet.
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Field_0x10));
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Field_0x14));
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Field_0x18));
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    // 06dae3cc FUN_06da13c0.
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06dae3f3
    return 0;
}

unsigned int RpcDecodeClientMunCollision(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength) {
    FPayloadClientMunCollision* Wire =
        reinterpret_cast<FPayloadClientMunCollision*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->Field_0x00);                                    // 06dae4a0
    Stream.GetDword(Wire->Field_0x04);                                    // 06dae4ca
    Stream.GetDword(Wire->ObjectId);                                      // 06dae4f4
    Stream.GetWord(Wire->Field_0x0c);                                     // 06dae51f
    // 06dae528 reserves 0xc, 06dae543 `CALL FUN_06d8f510` with ECX = &packet+0x30.
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Field_0x10));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Field_0x14));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Field_0x18));
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06dae57a
    return 0;
}

// ===========================================================================
// ch3 0x0c FLPACKET_CLIENT_OBJCOLLISION -- LAB_06dae770 (186) / LAB_06dae910 (151).
// dword, WORD, dword, WORD, float. Both 2-byte fields leave a 2-byte hole behind them
// in the struct and neither hole reaches the wire.
// ===========================================================================
unsigned int RpcEncodeClientObjCollision(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadClientObjCollision* Wire =
        reinterpret_cast<const FPayloadClientObjCollision*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06dae7a1
    Stream.PutDword(Wire->ObjectId);                                      // 06dae7e3
    Stream.PutWord(Wire->SubObjId);                                       // 06dae812
    Stream.PutDword(Wire->TargetObjectId);                                // 06dae843
    Stream.PutWord(Wire->TargetSubObjId);                                 // 06dae872
    // 06dae8a3 FLD float ptr [EDI + 0x30] / 06dae8a6 FSTP float ptr [EAX].
    Stream.PutFloat(*reinterpret_cast<const float*>(&Wire->Field_0x10));
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06dae8d6
    return 0;
}

unsigned int RpcDecodeClientObjCollision(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength) {
    FPayloadClientObjCollision* Wire =
        reinterpret_cast<FPayloadClientObjCollision*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->ObjectId);                                      // 06dae97e
    Stream.GetWord(Wire->SubObjId);                                       // 06dae9a7
    Stream.GetDword(Wire->TargetObjectId);                                // 06dae9d0
    Stream.GetWord(Wire->TargetSubObjId);                                 // 06dae9f9
    // 06daea17 reads it as a plain dword, not through the x87 stack.
    Stream.GetDword(Wire->Field_0x10);                                    // 06daea1b
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06daea4a
    return 0;
}

// ===========================================================================
// ch3 0x18 FLPACKET_CLIENT_REQADDITEM -- LAB_06d99d90 (221) / LAB_06d99f80 (186).
// Wire order: ArchId, Count, Health, bMounted, HardpointSize, HardpointSize bytes.
// See the permutation note at the head of this file before touching the order.
// ===========================================================================
unsigned int RpcEncodeClientReqAddItem(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadClientReqAddItem* Wire =
        reinterpret_cast<const FPayloadClientReqAddItem*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06d99dc1
    Stream.PutDword(Wire->ArchId);                                        // 06d99e03  packet+0x20
    Stream.PutDword((unsigned int)Wire->Count);                           // 06d99e32  packet+0x24
    Stream.PutFloat(Wire->Health);                                        // 06d99e61  packet+0x2c
    Stream.PutByte(Wire->bMounted);                                       // 06d99e8f  packet+0x30
    Stream.PutDword(Wire->HardpointSize);                                 // 06d99ebc  packet+0x28
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    // 06d99ec9 `TEST EAX,EAX ; 06d99ecb JBE` -- a zero length skips the block outright.
    if (Wire->HardpointSize != 0) {
        if (!Stream.PutBytes(Wire->Hardpoint, Wire->HardpointSize)) {     // 06d99f02
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06d99f45
    return 0;
}

unsigned int RpcDecodeClientReqAddItem(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength) {
    FPayloadClientReqAddItem* Wire =
        reinterpret_cast<FPayloadClientReqAddItem*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->ArchId);                                        // 06d99fee
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Count));      // 06d9a016
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Health));     // 06d9a03e
    Stream.GetByte(Wire->bMounted);                                       // 06d9a062
    Stream.GetDword(Wire->HardpointSize);                                 // 06d9a08e
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    // 06d9a094 operator new(HardpointSize) -- retail allocates unconditionally, before
    // the byte run's own bounds check, and the packet's destructor frees it.
    Wire->Hardpoint = static_cast<char*>(
        ::operator new(Wire->HardpointSize, std::nothrow));               // 06d9a09c, bare
    if (!Stream.GetBytes(Wire->Hardpoint, Wire->HardpointSize)) {         // 06d9a0d6
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06d9a118
    return 0;
}

// ===========================================================================
// ch3 0x1a FLPACKET_CLIENT_REQMODIFYITEM -- LAB_06d9a220 (221) / LAB_06d9a410 (186).
// Same shape, DIFFERENT order: EquipId, Count, Health, bMounted, HardpointSize, block.
// ===========================================================================
unsigned int RpcEncodeClientReqModifyItem(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadClientReqModifyItem* Wire =
        reinterpret_cast<const FPayloadClientReqModifyItem*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06d9a251
    Stream.PutDword(Wire->EquipId);                                       // 06d9a293  packet+0x28
    Stream.PutDword((unsigned int)Wire->Count);                           // 06d9a2c2  packet+0x20
    Stream.PutFloat(Wire->Health);                                        // 06d9a2f1  packet+0x2c
    Stream.PutByte(Wire->bMounted);                                       // 06d9a31f  packet+0x30
    Stream.PutDword(Wire->HardpointSize);                                 // 06d9a34c  packet+0x24
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    if (Wire->HardpointSize != 0) {                                       // 06d9a35b
        if (!Stream.PutBytes(Wire->Hardpoint, Wire->HardpointSize)) {     // 06d9a392
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06d9a3d5
    return 0;
}

unsigned int RpcDecodeClientReqModifyItem(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength) {
    FPayloadClientReqModifyItem* Wire =
        reinterpret_cast<FPayloadClientReqModifyItem*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->EquipId);                                       // 06d9a47e
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Count));      // 06d9a4a6
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Health));     // 06d9a4ce
    Stream.GetByte(Wire->bMounted);                                       // 06d9a4f2
    Stream.GetDword(Wire->HardpointSize);                                 // 06d9a51e
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Wire->Hardpoint = static_cast<char*>(
        ::operator new(Wire->HardpointSize, std::nothrow));               // 06d9a52c, bare
    if (!Stream.GetBytes(Wire->Hardpoint, Wire->HardpointSize)) {         // 06d9a566
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06d9a5a8
    return 0;
}

// ===========================================================================
// ch3 0x1e FLPACKET_CLIENT_SAVEGAME -- LAB_06d9a800 (133) / LAB_06d9a950 (128).
//
// { dword DescriptionCount, 2*DescriptionCount bytes }. The 0x200-byte CHARACTER_ID at
// payload+0x000 is NOT serialized by either side -- the encoder's first and only field
// read is 06d9a875 `MOV ECX,dword ptr [EBP + 0x224]`, payload+0x204. That is retail's
// wire format, not an omission here.
//
// The count is in WIDE CHARACTERS: 06d9a891 `LEA EDX,[EAX + EAX*0x1]` reserves 2*n on
// the encode side and 06d9a8c6 `SHL EAX,0x1` is the copy length; the decoder does the
// same twice, 06d9a9c9 for the operator new() argument and 06d9a9e3 for the reservation.
// ===========================================================================
unsigned int RpcEncodeClientSaveGame(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadClientSaveGame* Wire =
        reinterpret_cast<const FPayloadClientSaveGame*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06d9a831
    if (!Stream.PutDword(Wire->DescriptionCount)) {                       // 06d9a875
        return Stream.GetError();
    }
    if (Wire->DescriptionCount != 0) {                                    // 06d9a88a
        if (!Stream.PutBytes(Wire->Description,
                             Wire->DescriptionCount * 2u)) {              // 06d9a8d3
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06d9a914
    return 0;
}

unsigned int RpcDecodeClientSaveGame(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength) {
    FPayloadClientSaveGame* Wire =
        reinterpret_cast<FPayloadClientSaveGame*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    if (!Stream.GetDword(Wire->DescriptionCount)) {                       // 06d9a9c1
        return Stream.GetError();
    }
    if (Wire->DescriptionCount != 0) {                                    // 06d9a9c7
        Wire->Description = static_cast<wchar_t*>(
            ::operator new(Wire->DescriptionCount * 2u, std::nothrow));   // 06d9a9cd, bare
        if (!Stream.GetBytes(Wire->Description,
                             Wire->DescriptionCount * 2u)) {              // 06d9aa1f
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06d9aa61
    return 0;
}

// ===========================================================================
// ch3 0x20 FLPACKET_CLIENT_MINEASTEROID -- LAB_06daeac0 (202) / LAB_06daeca0 (160).
// dword, Vector, dword, dword, dword.
// ===========================================================================
unsigned int RpcEncodeClientMineAsteroid(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadClientMineAsteroid* Wire =
        reinterpret_cast<const FPayloadClientMineAsteroid*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06daeaf4
    Stream.PutDword(Wire->SystemId);                                      // 06daeb3e
    // 06daeb4d reserves 0xc; 06daeb75..06daeb7d load the three floats into a stack temp
    // in the order (0x24, 0x2c, 0x28) -- the LOAD order, which is the compiler's, not
    // the wire order. FUN_06d8f510 at 06daeb93 then copies the temp out in index order.
    Stream.PutFloat(Wire->PositionX);
    Stream.PutFloat(Wire->PositionY);
    Stream.PutFloat(Wire->PositionZ);
    Stream.PutDword(Wire->LootId);                                        // 06daebca
    Stream.PutDword(Wire->SecondaryLoot);                                 // 06daebfc
    Stream.PutDword(Wire->Quantity);                                      // 06daec2e
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06daec64
    return 0;
}

unsigned int RpcDecodeClientMineAsteroid(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength) {
    FPayloadClientMineAsteroid* Wire =
        reinterpret_cast<FPayloadClientMineAsteroid*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->SystemId);                                      // 06daed0d
    // 06daed17 reserves 0xc, 06daed3a `CALL FUN_06d8f510` with ECX = &packet+0x24.
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->PositionX));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->PositionY));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->PositionZ));
    Stream.GetDword(Wire->LootId);                                        // 06daed70
    Stream.GetDword(Wire->SecondaryLoot);                                 // 06daed98
    Stream.GetDword(Wire->Quantity);                                      // 06daedc1
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06daeded
    return 0;
}

// ===========================================================================
// ch3 0x31 FLPACKET_CLIENT_REQUEST_EVENT -- LAB_06daa240 (185) / LAB_06daa3e0 (151).
// byte, dword, dword, dword, byte.
// ===========================================================================
unsigned int RpcEncodeClientRequestEvent(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadClientRequestEvent* Wire =
        reinterpret_cast<const FPayloadClientRequestEvent*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06daa271
    Stream.PutByte(Wire->EventType);                                      // 06daa2b1
    Stream.PutDword(Wire->Field_0x04);                                    // 06daa2de
    Stream.PutDword(Wire->Field_0x08);                                    // 06daa30d
    Stream.PutDword(Wire->Field_0x0c);                                    // 06daa33c
    Stream.PutByte(Wire->Field_0x10);                                     // 06daa369
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06daa39a
    return 0;
}

unsigned int RpcDecodeClientRequestEvent(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength) {
    FPayloadClientRequestEvent* Wire =
        reinterpret_cast<FPayloadClientRequestEvent*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetByte(Wire->EventType);                                      // 06daa44a
    Stream.GetDword(Wire->Field_0x04);                                    // 06daa472
    Stream.GetDword(Wire->Field_0x08);                                    // 06daa49a
    Stream.GetDword(Wire->Field_0x0c);                                    // 06daa4c2
    Stream.GetByte(Wire->Field_0x10);                                     // 06daa4e1
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06daa50e
    return 0;
}

// ===========================================================================
// ch3 0x39 FLPACKET_CLIENT_CREATENEWCHAR -- LAB_06daf410 (180) / LAB_06daf5b0 (163).
//
// { word NameLength, NameLength wide chars, dword, dword, dword, dword }, and the two
// string halves are the only two callers of FUN_06da14f0 / FUN_06da1580 in the module.
// Those two are NOT the {dword size, bytes} pair FUN_06da1620 handles; they are their
// own primitive and they differ from it in three ways worth spelling out:
//
//  * the length is a WORD, not a dword -- 06da1526 `MOV word ptr [EBX],AX`;
//  * the length is wcslen WITHOUT the NUL, and the decoder re-terminates instead,
//    06da1607 `MOV word ptr [EBX + ECX*0x1],0x0` at offset 2*len of a 2*len+2 block;
//  * the bounds test is `JC`, not `JBE` -- 06da150d / 06da153e / 06da159d / 06da15c0.
//    Cursor == Limit is an ERROR for these two where it is fine everywhere else, so a
//    string that exactly fills the buffer is rejected. Reproduced with Reserve() plus an
//    explicit equality test rather than by reusing Put/GetBytes.
//
// One more asymmetry, left as retail has it: when the length is zero FUN_06da1580 skips
// LAB_06da1614 and never writes the advanced buffer pointer back (06da1612 is inside
// the skipped block), so the two length bytes it consumed from the cursor are not
// consumed from the pointer. The decoder below reproduces that exactly; see
// docs/re/deviations.md.
// ===========================================================================
unsigned int RpcEncodeClientCreateNewChar(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadClientCreateNewChar* Wire =
        reinterpret_cast<const FPayloadClientCreateNewChar*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06daf441
    if (!Stream.PutWideString(Wire->Name)) {                              // 06daf465 FUN_06da14f0
        return FRpcMarshalStream::kEncodeOverflow;                        // 06daf46e
    }
    Stream.PutDword(Wire->Field_0x80);                                    // 06daf4a4
    Stream.PutDword(Wire->Field_0x84);                                    // 06daf4d6
    Stream.PutDword(Wire->Field_0x88);                                    // 06daf508
    Stream.PutDword(Wire->Field_0x8c);                                    // 06daf53a
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06daf570
    return 0;
}

unsigned int RpcDecodeClientCreateNewChar(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength) {
    FPayloadClientCreateNewChar* Wire =
        reinterpret_cast<FPayloadClientCreateNewChar*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    wchar_t* Name = nullptr;
    if (!Stream.GetWideString(Name)) {                                    // 06daf5ff FUN_06da1580
        return FRpcMarshalStream::kDecodeOverrun;                         // 06daf608
    }
    // 06daf614 `MOV EAX,dword ptr [ESP + 0x18]` / 06daf618 `TEST EAX,EAX` / 06daf61a
    // `JZ LAB_06daf634` -- retail DOES guard the wcscpy against the null an empty string
    // leaves behind, so the destination keeps whatever it held.
    if (Name != nullptr) {
        // 06daf621 CALL MSVCRT wcscpy, unbounded, as retail's: the wire length is a word,
        // so a name longer than the 0x40-wchar_t field runs over Field_0x80..Field_0x8c
        // (which the reads below then overwrite) and on past the end of the packet.
        wcscpy(Wire->Name, Name);
        // 06daf62c CALL FUN_06db1dc2 -- the CRT free of the temporary FUN_06da1580
        // allocated with operator new.
        ::operator delete(Name);
    }
    Stream.GetDword(Wire->Field_0x80);                                    // 06daf660
    Stream.GetDword(Wire->Field_0x84);                                    // 06daf68b
    Stream.GetDword(Wire->Field_0x88);                                    // 06daf6b6
    Stream.GetDword(Wire->Field_0x8c);                                    // 06daf6da
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06daf70c
    return 0;
}
