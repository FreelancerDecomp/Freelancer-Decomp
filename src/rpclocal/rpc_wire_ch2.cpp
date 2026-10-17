// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_wire_ch2.cpp -- retail channel-2 packet codec slots (vtable +0x04 Encode,
// +0x08 Decode), transcribed from EXE\Disasm\rpclocal.dll.txt.
//
// Ghidra produced NO decompilation for any function in this file: all six retail
// bodies are anonymous `LAB_` blocks reached only from an .rdata vtable slot, so
// rpclocal.dll.c does not contain them at all. Every claim below is therefore read
// off the raw listing.
//
// How each packet class was identified -- the routing header the constructor writes
// one or two instructions after the vptr:
//
//   ch2 0x28 FLPACKET_SERVER_CREATELOOT
//     06d9d1a5  MOV dword ptr [ESI],.rdata:PTR_FUN_06db50ac
//     06d9d1ab  MOV dword ptr [ESI + 0xc],0x20028
//     vtable 06db50ac = { dtor FUN_06d9d1d0, Encode LAB_06d928c0, Decode LAB_06d92d20,
//                         dump (the shared IServerImpl::GetServerStats body) }
//
//   ch2 0x29 FLPACKET_SERVER_SETREPUTATION
//     06d9d336  MOV dword ptr [ESI],.rdata:PTR_FUN_06db3d50
//     06d9d33c  MOV dword ptr [ESI + 0xc],0x20029
//     vtable 06db3d50 = { FUN_06d91eb0, LAB_06d91ed0, LAB_06d92350, FUN_06d92860 }
//
//   ch2 0x54 FLPACKET_SERVER_MISCOBJUPDATE
//     06d9edf5  MOV dword ptr [ESI],.rdata:PTR_FUN_06db4018
//     06d9edfb  MOV dword ptr [ESI + 0xc],0x20054
//     vtable 06db4018 = { FUN_06d98aa0, LAB_06d98d30, LAB_06d990d0, dump }
//
// The same scan over the whole listing finds exactly 162 such constructor pairs, one
// per registered packet type -- the same 162 the registry in rpc_packet_registry.cpp
// carries -- so the channel/opcode of a vtable is never a guess here.
//
// Argument conventions, re-verified rather than inherited:
//   * Encode: `RET 0xc`, three stack arguments. arg1 is a pointer TO the buffer
//     pointer and is DEREFERENCED (06d928cb `MOV ECX,[ESP+0x34]` then 06d928d7
//     `MOV EDX,[ECX]` -> packet+0x18); arg2 is the capacity; arg3 receives the byte
//     count INCLUDING the two header bytes (06d92cd2/06d92cd6).
//   * Decode: `RET 0xc` as well, but arg1 is the buffer pointer DIRECTLY
//     (06d92d2c `MOV ECX,[ESP+0x68]` then 06d92d36 `MOV [ESI+8],ECX`, no second
//     dereference), arg2 is the size and arg3 gets the consumed count.
//   * FUN_06da1380 (encode prologue) writes the two header bytes; FUN_06da13c0 is
//     `XOR EAX,EAX ; RET 0xc` and emits/consumes nothing -- retail calls it at the
//     END of an encoder and at the START of a decoder purely for symmetry.
//   * On any failure retail returns the sticky error WITHOUT touching the out-length
//     (LAB_06d92cb5 / LAB_06d92cc4 / LAB_06d93101); only the success tail writes it.
//     That is reproduced exactly, so a short buffer leaves the caller's length alone.
//
// None of this touches the single-player path: in SP a packet is queued by pointer
// and neither slot is ever called. The packet CONSTRUCTORS do run in SP, but they are
// not in this file.

#include "rpc_containers.h"
#include "rpc_payloads.h"

// COMMON.DLL entry points retail imports by name, IAT slots read off the call bytes:
//   06d92a0c `ff d3` with EBX loaded at 06d929f7 `8b1d 0831db06` -> [06db3108]
//                                            Archetype::LargeIDToSmallID
//   06d92ed4 `ff15 0030db06` -> [06db3000]   Archetype::SmallIDToLargeID
//   06d92b7f `ff15 5c30db06` -> [06db305c]   SinglePlayer
#include "common_goods.h"
#include "common_misc.h" // SinglePlayer

#include <cstdlib>
#include <cstring>
#include <new>

// ===========================================================================
// ch2 0x28 FLPACKET_SERVER_CREATELOOT -- Encode LAB_06d928c0, Decode LAB_06d92d20
//
// Payload 0x14 bytes (registry size 0x34 minus the 0x20 CDAPacket base), and the
// constructor at 06d9d171..06d9d1a2 settles its shape without any guessing:
//     [ESI+0x20] = 0        a dword
//     [ESI+0x24] = 0        a byte
//     [ESI+0x28] = AL       the std::list allocator byte (the low byte of the ctor's
//                           own argument -- garbage nobody reads)
//     PUSH 0x50 ; operator new ; [EAX] = EAX ; [EAX+4] = EAX
//     [ESI+0x2c] = EAX      the list's dummy node
//     [ESI+0x30] = 0        the element count
// so the tail of the payload is a retail std::list whose NODE is 0x50 bytes and whose
// element is therefore 0x48. FUN_06d931f0 (the decoder's insert) allocates the same
// 0x50 at 06d931fb, which is the second, independent witness for the stride.
//
// The wire element, in order, with the listing address of each store/load:
//     +0x00  dword                                            06d929f2 / 06d92e99
//     +0x04  Archetype::LargeIDToSmallID -> word              06d92a0c / 06d92ec7
//     +0x08  float (FLD/FSTP on encode, plain dword on decode) 06d92a5c / 06d92f03
//     +0x0c  word                                             06d92a8b / 06d92f2d
//     +0x10  Archetype::LargeIDToSmallID -> word              06d92a9f / 06d92f55
//     +0x14  float                                            06d92af3 / 06d92f91
//     +0x18  dword                                            06d92b23 / 06d92fb9
//     +0x1c  three dwords (FUN_06d8f510)                      06d92b75 / 06d92fe7
//   if (SinglePlayer()):
//     +0x28  three dwords (FUN_06d8f510)                      06d92bd9 / 06d9302b
//     +0x34  four dwords  (FUN_06d8f4f0)                      06d92c30 / 06d93065
//     +0x44  byte                                             06d92c5f / 06d93097
//     +0x45  byte                                             06d92c89 / 06d930bd
//
// THE SINGLE-PLAYER GATE RUNS THE OTHER WAY ROUND FROM CREATESHIP's. CREATESHIP's
// reputation tail is `06d8952f CALL SinglePlayer ; 06d89535 TEST AL,AL ; 06d89537
// JNZ` -- skipped in single player. CREATELOOT's is `06d92b7f CALL SinglePlayer ;
// 06d92b85 TEST AL,AL ; 06d92b87 0f84 JZ LAB_06d92c3a` on the encode side and
// `06d92ff1 ... 06d92ff9 74 JZ LAB_06d93075` on the decode side: the two extra
// vectors travel ONLY in single player. Both directions agree, so this is not the
// encoder/decoder inversion PLAYER_TRADE has; it is a genuinely per-site polarity,
// and rpc_containers.cpp already carries both senses for other types.
//
// FUN_06d8f510 and FUN_06d8f4f0 are three- and four-dword copies (rpclocal.dll.c
// lines 10072/10084) with the arguments the other way round between the two
// directions -- encode passes (this = write cursor, src = &field), decode passes
// (this = &field, src = read cursor).
// ===========================================================================

unsigned int RpcEncodeServerCreateLoot(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerCreateLoot* Wire =
        reinterpret_cast<const FPayloadServerCreateLoot*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06d928f4 FUN_06da1380
    Stream.PutDword(Wire->Field_0x00);                         // 06d9293c  payload+0x00
    Stream.PutByte(Wire->Field_0x04);                          // 06d92973  payload+0x04

    // 06d9297a `MOV CL,byte ptr [EBP+0x30]` -- only the LOW BYTE of the list's Size
    // travels, and 06d929b0 `TEST CL,CL ; JBE` (CF is always clear after TEST, so the
    // JBE is a JZ) skips the whole loop when it is zero.
    const unsigned char Count = (unsigned char)(Wire->Items.Size & 0xffu);
    Stream.PutByte(Count);                                     // 06d929ad
    if (Count != 0) {
        // 06d929bd dereferences the dummy node with no null check. A non-zero count
        // means RpcPacketLootPushBack has built it.
        for (const FRpcLootWireNode* Node = Wire->Items.Head->Next;
             Node != Wire->Items.Head && !Stream.Failed(); Node = Node->Next) {
            const FRpcLootWireRecord& Record = Node->Value;
            Stream.PutDword(Record.Field_0x00);                                   // 06d929f2
            Stream.PutWord(Archetype::LargeIDToSmallID(Record.Field_0x04));       // 06d92a34
            Stream.PutDword(Record.Field_0x08);                                   // 06d92a64 FSTP
            Stream.PutWord(Record.Field_0x0c);                                    // 06d92a8f
            Stream.PutWord(Archetype::LargeIDToSmallID(Record.Field_0x10));       // 06d92ac9
            Stream.PutDword(Record.Field_0x14);                                   // 06d92af8 FSTP
            Stream.PutDword(Record.Field_0x18);                                   // 06d92b26
            // 06d92b32 advances the cursor by 0xc, then 06d92b75 FUN_06d8f510 moves
            // the three dwords at record+0x1c..+0x24 and 06d92b7a bumps the pointer.
            Stream.PutBytes(&Record.Field_0x1c, 12);
            if (SinglePlayer()) {                                                 // 06d92b7f / 06d92b87
                Stream.PutBytes(&Record.Field_0x28, 12);                          // 06d92bd9
                Stream.PutBytes(&Record.Field_0x34, 16);                          // 06d92c30
            }
            Stream.PutByte(Record.Field_0x44);                                    // 06d92c5f
            Stream.PutByte(Record.Field_0x45);                                    // 06d92c89
        }
    }
    // 06d92ca9 FUN_06da13c0 -- `XOR EAX,EAX ; RET 0xc`, emits nothing.
    if (Stream.Failed()) {
        // LAB_06d92cb5 / LAB_06d92cc4: the out-length is NOT written on the error path.
        return Stream.GetError();
    }
    *OutLength = Stream.Length();                                             // 06d92cd6
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerCreateLoot(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength) {
    FPayloadServerCreateLoot* Wire =
        reinterpret_cast<FPayloadServerCreateLoot*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06d92d4d FUN_06da13c0 -- consumes nothing; the dispatcher has already eaten the
    // two header bytes, which is why the decoder's length EXCLUDES them.

    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Field_0x00 = Scratch;                                // 06d92d94
    unsigned char Small8 = 0;
    if (!Stream.GetByte(Small8)) {
        return Stream.GetError();
    }
    Wire->Field_0x04 = Small8;                                 // 06d92db8
    unsigned char Count = 0;
    if (!Stream.GetByte(Count)) {                              // 06d92dd7
        return Stream.GetError();
    }
    // 06d92dda `TEST CL,CL ; JBE LAB_06d930dd` -- zero elements skips the loop; the
    // loop itself is a do/while on a BYTE counter (06d930c9..06d930d7).
    for (unsigned int Index = 0; Index < (unsigned int)Count; ++Index) {
        // 06d92df4..06d92e49: retail zeroes a 0x48-byte stack record, then calls
        // FUN_06d931c0 on record+0x34, which writes the IDENTITY QUATERNION
        // (0x3f800000, 0, 0, 0) there -- the only non-zero default -- and finally
        // clears record+0x44 and +0x45 at 06d92e5f/06d92e64.
        FRpcLootWireRecord Fresh;
        unsigned char* Raw = reinterpret_cast<unsigned char*>(&Fresh);
        for (unsigned int b = 0; b < (unsigned int)sizeof(FRpcLootWireRecord); ++b) {
            Raw[b] = 0;
        }
        Fresh.Field_0x34 = 0x3f800000u;                        // 06d931c4

        // 06d92e69 FUN_06d931f0 == std::list::insert(end(), record): the element is
        // linked in FIRST and filled in place afterwards, so a short buffer leaves a
        // partially decoded element behind exactly as retail does.
        RpcPacketLootPushBack(&Packet, Fresh);
        FRpcLootWireList* List = RpcPacketLootList(&Packet);
        // 06d92e6e..06d92e7e: `MOV ECX,[EBX+0x2c] ; MOV EBP,[ECX+4] ; ADD EBP,8`.
        FRpcLootWireRecord& Record = List->Head->Prev->Value;

        unsigned short Small = 0;
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Record.Field_0x00 = Scratch;                                              // 06d92e9d
        if (!Stream.GetWord(Small)) { return Stream.GetError(); }
        // 06d92ec9 `XOR ECX,ECX ; MOV CX,[EAX]` -- zero-extended, then converted.
        Record.Field_0x04 = Archetype::SmallIDToLargeID(Small);                   // 06d92eda
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Record.Field_0x08 = Scratch;                                              // 06d92f0a
        if (!Stream.GetWord(Small)) { return Stream.GetError(); }
        Record.Field_0x0c = Small;                                                // 06d92f33
        if (!Stream.GetWord(Small)) { return Stream.GetError(); }
        Record.Field_0x10 = Archetype::SmallIDToLargeID(Small);                   // 06d92f68
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Record.Field_0x14 = Scratch;                                              // 06d92f98
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Record.Field_0x18 = Scratch;                                              // 06d92fc0
        if (!Stream.GetBytes(&Record.Field_0x1c, 12)) { return Stream.GetError(); }// 06d92fe7
        if (SinglePlayer()) {                                                     // 06d92ff1 / 06d92ff9
            if (!Stream.GetBytes(&Record.Field_0x28, 12)) { return Stream.GetError(); }
            if (!Stream.GetBytes(&Record.Field_0x34, 16)) { return Stream.GetError(); }
        }
        if (!Stream.GetByte(Small8)) { return Stream.GetError(); }
        Record.Field_0x44 = Small8;                                               // 06d93099
        if (!Stream.GetByte(Small8)) { return Stream.GetError(); }
        Record.Field_0x45 = Small8;                                               // 06d930c0
    }
    // 06d930e6 FUN_06da13c0 -- consumes nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                                                 // LAB_06d93101
    }
    *OutLength = Stream.Length();                                             // 06d93114
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x29 FLPACKET_SERVER_SETREPUTATION -- Encode LAB_06d91ed0, Decode LAB_06d92350
//
// Registry size 0x68, payload 0x48. THE CONSTRUCTOR (06d9d290..06d9d33c) SETTLES A
// FIELD THE PAYLOAD HEADER GOT WRONG. rpc_payloads.h said of Field_0x18 that "the ctor
// does not even seed this one"; it does:
//     06d9d2dc  MOV EAX,0x2
//     06d9d2e1  MOV dword ptr [ESI + 0x38],EAX      ; packet+0x38 == payload+0x18
// alongside 06d9d2ef `[ESI+0x34] = 1`, 06d9d2f2 `[ESI+0x3c] = 4`, 06d9d2f9
// `[ESI+0x40] = 8` and 06d9d300 `[ESI+0x44] = 0x10`. Payload+0x14/+0x18/+0x1c/+0x20/
// +0x24 are therefore 1, 2, 4, 8, 0x10 -- FIVE BIT FLAGS, not five unrelated fields,
// and the byte at payload+0x34 that the ctor also clears (06d9d323) is the presence
// MASK built out of them. That is the whole shape of this codec.
//
// Both directions read the flag VALUES out of the payload rather than using literals,
// so the port does too -- an invented 0x01/0x02/0x04/0x08/0x10 would compile and would
// be indistinguishable until a packet arrived with different seeds.
//
// ENCODE (LAB_06d91ed0). Note it MUTATES the packet: payload+0x34 is an output of the
// encoder, not an input.
//     mask = 0                                              06d91f1c
//     if (Field_0x3c != 0xffffffff) mask = Flag1            06d91f14 / 06d91f25
//     if (SinglePlayer()) {                                 06d91f28 / 06d91f30 JZ
//         if (DisplayNameLength != 0)   mask |= Flag4       06d91f35 / 06d91f41
//         if (*(int*)NameABuffer || *(int*)NameBBuffer)
//                                       mask |= Flag2       06d91f47 / 06d91f4f / 06d91f5c
//         RelCount = Relations.Size                         06d91f5f
//         if (RelCount != 0)
//             mask |= (RelCount > 0xff) ? Flag10 : Flag8    06d91f6f JLE (SIGNED)
//     }
//     PutByte(mask)                                         06d91fb6
//     PutDword(VibeHandle)                                  06d91fe5
//     if (mask & Flag1) PutDword(Field_0x3c)                06d91fef / 06d9201e
//     if (mask & Flag4) { PutByte((byte)DisplayNameLength)  06d92057
//                         PutBytes(DisplayName, len * 2) }  06d92065 SHL 1 / 06d9209a
//     if (mask & Flag2) { size+bytes of NameA               06d920e8 / 06d9212b
//                         size+bytes of NameB }             06d9216f / 06d921b9
//     if (IsMPServer()) PutFloat(Attitude)                  06d921d0 / 06d92202
//     else if (RelCount != 0) {
//         (mask & Flag10) ? PutWord(count) : PutByte(count) 06d92250 / 06d92278
//         for each node: PutFloat(rel+0x04); PutDword(rel+0x00)   06d922a4 / 06d922d0
//     }
//
// DECODE (LAB_06d92350) is the inverse with THREE asymmetries, each verified:
//   1. The MP/relations fork is decided by a DIFFERENT FUNCTION. The encoder asks
//      `06d921d0 CALL COMMON.DLL::IsMPServer ; 06d921d6 TEST AL,AL ; 06d921d8 JZ`; the
//      decoder asks `06d92504 CALL COMMON.DLL::SinglePlayer ; 06d9250a TEST AL,AL ;
//      06d9250c JZ LAB_06d92668`, and LAB_06d92668 is the attitude read. This is not
//      the same predicate inverted (that is PLAYER_TRADE's defect) -- the two sides
//      genuinely consult two different globals, and both are reproduced verbatim.
//   2. The display-name length is written as the LOW BYTE of a dword (06d92037
//      `MOV CL,byte ptr [EBX+0x60]`) but read back SIGN-EXTENDED (06d9246b
//      `MOVSX EAX,CL`), so 0x80..0xff decode negative.
//   3. The decoder allocates the name buffer as `len * 4 + 4` bytes (06d92471
//      `LEA EAX,[EAX*0x4 + 0x4]`) while copying only `len * 2` and terminating with a
//      WORD at [buf + len*2] (06d924d7) -- twice the memory a 2-byte wchar_t needs.
//      Reproduced; it is retail's allocation, not a port choice.
// ===========================================================================

unsigned int RpcEncodeServerSetReputation(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength) {
    // NOT const: 06d91f1c / 06d91f25 / 06d91f41 / 06d91f5c / 06d91f79 / 06d91f86 all
    // write the mask back into payload+0x34.
    FPayloadServerSetReputation* Wire =
        reinterpret_cast<FPayloadServerSetReputation*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06d91efc FUN_06da1380

    const unsigned char Flag1 = (unsigned char)(Wire->Field_0x14 & 0xffu);
    const unsigned char Flag2 = (unsigned char)(Wire->Field_0x18 & 0xffu);
    const unsigned char Flag4 = (unsigned char)(Wire->Field_0x1c & 0xffu);
    const unsigned char Flag8 = (unsigned char)(Wire->Field_0x20 & 0xffu);
    const unsigned char Flag10 = (unsigned char)(Wire->Field_0x24 & 0xffu);

    unsigned char Mask = 0;                                    // 06d91f1c
    if (Wire->Field_0x3c != 0xffffffffu) {                     // 06d91f14 CMP ...,-1
        Mask = Flag1;                                          // 06d91f25
    }
    unsigned int RelCount = 0;                                 // 06d91f18
    if (SinglePlayer()) {                                      // 06d91f28 / 06d91f30
        if (Wire->DisplayNameLength != 0) {                    // 06d91f35
            Mask = (unsigned char)(Mask | Flag4);              // 06d91f41
        }
        // 06d91f47 / 06d91f4f dereference both buffers with no null test. The marshaller
        // always fills them (RpcFlattenFmtStr, as 06d88de7 / 06d88e26).
        const unsigned int* NameAFirst = static_cast<const unsigned int*>(Wire->NameABuffer);
        const unsigned int* NameBFirst = static_cast<const unsigned int*>(Wire->NameBBuffer);
        if (*NameAFirst != 0 || *NameBFirst != 0) {
            Mask = (unsigned char)(Mask | Flag2);              // 06d91f5c
        }
        RelCount = Wire->Relations.Size;                       // 06d91f5f
        if (RelCount != 0) {                                   // 06d91f68
            // 06d91f6a `CMP EAX,0xff ; 06d91f6f JLE` -- a SIGNED comparison on a
            // count that is stored unsigned. Kept signed.
            if ((int)RelCount > 0xff) {
                Mask = (unsigned char)(Mask | Flag10);         // 06d91f79
            } else {
                Mask = (unsigned char)(Mask | Flag8);          // 06d91f86
            }
        }
    }
    Wire->Field_0x34 = Mask;

    Stream.PutByte(Mask);                                      // 06d91fb6
    Stream.PutDword(Wire->VibeHandle);                         // 06d91fe5  payload+0x38
    if ((Flag1 & Mask) != 0) {                                 // 06d91fef
        Stream.PutDword(Wire->Field_0x3c);                     // 06d9201e
    }
    if ((Flag4 & Mask) != 0) {                                 // 06d92028
        Stream.PutByte((unsigned char)(Wire->DisplayNameLength & 0xffu));   // 06d92057
        Stream.PutBytes(Wire->DisplayName, Wire->DisplayNameLength * 2u);   // 06d9209a
    }
    if ((Flag2 & Mask) != 0) {                                 // 06d920b4
        RpcPutSizedBlock(Stream, Wire->NameASize, Wire->NameABuffer);       // 06d920e8
        RpcPutSizedBlock(Stream, Wire->NameBSize, Wire->NameBBuffer);       // 06d9216f
    }
    if (IsMPServer()) {                                        // 06d921d0 / 06d921d8
        Stream.PutFloat(Wire->Attitude);                       // 06d92202 FLD/FSTP
    } else if (RelCount != 0) {                                // 06d92216
        if ((Flag10 & Mask) != 0) {                            // 06d92227
            Stream.PutWord((unsigned short)(RelCount & 0xffffu));           // 06d92250
        } else {
            Stream.PutByte((unsigned char)(RelCount & 0xffu));              // 06d92278
        }
        // The dummy is dereferenced untested; a non-zero count means it was built.
        for (const FRpcRelationNode* Node = Wire->Relations.Head->Next;
             Node != Wire->Relations.Head && !Stream.Failed(); Node = Node->Next) {
            // 06d922a4 FLD [EDX+0x4] then 06d922d0 MOV EDX,[EDX] -- the value is
            // used untested, and the pair travels SECOND FIELD FIRST.
            const FRpcRelation* Relation = Node->Value;
            Stream.PutDword(Relation->Field_0x04);
            Stream.PutDword(Relation->Field_0x00);
        }
    }
    // 06d922ec FUN_06da13c0 -- emits nothing.
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();                          // 06d92308
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerSetReputation(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength) {
    FPayloadServerSetReputation* Wire =
        reinterpret_cast<FPayloadServerSetReputation*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06d9237f FUN_06da13c0 -- consumes nothing.

    const unsigned char Flag1 = (unsigned char)(Wire->Field_0x14 & 0xffu);
    const unsigned char Flag2 = (unsigned char)(Wire->Field_0x18 & 0xffu);
    const unsigned char Flag4 = (unsigned char)(Wire->Field_0x1c & 0xffu);
    const unsigned char Flag8 = (unsigned char)(Wire->Field_0x20 & 0xffu);
    const unsigned char Flag10 = (unsigned char)(Wire->Field_0x24 & 0xffu);

    unsigned char Mask = 0;
    if (!Stream.GetByte(Mask)) {                               // 06d923bd
        return Stream.GetError();
    }
    Wire->Field_0x34 = Mask;                                   // 06d923c0
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->VibeHandle = Scratch;                                // 06d923f2
    if ((Flag1 & Mask) != 0) {                                 // 06d923ef
        if (!Stream.GetDword(Scratch)) {
            return Stream.GetError();
        }
        Wire->Field_0x3c = Scratch;                            // 06d9241c
    } else {
        Wire->Field_0x3c = 0xffffffffu;                        // 06d92423
    }

    if (SinglePlayer()) {                                      // 06d9242a / 06d92432
        if ((Flag4 & Mask) != 0) {                             // 06d9243b
            unsigned char Raw = 0;
            if (!Stream.GetByte(Raw)) {                        // 06d92466
                return Stream.GetError();
            }
            // 06d9246b `MOVSX EAX,CL` -- SIGN-extended into the dword length.
            const int Signed = (int)(signed char)Raw;
            Wire->DisplayNameLength = (unsigned int)Signed;    // 06d9246e
            // A negative length is not refused: it reaches operator new wrapped and the
            // cursor check below wrapped (06d92486..06d92496), exactly as in retail.
            const unsigned int Chars = (unsigned int)Signed;
            // 06d92471 `LEA EAX,[EAX*0x4 + 0x4]` -- four bytes per character plus four,
            // for a buffer only ever used two bytes per character. Bare call (06d92479).
            void* Block = ::operator new(Chars * 4u + 4u, std::nothrow);
            Wire->DisplayName = static_cast<wchar_t*>(Block);  // 06d92481
            if (!Stream.GetBytes(Block, Chars * 2u)) {         // 06d92488 / 06d924bf
                return Stream.GetError();
            }
            // 06d924d7 `MOV word ptr [EAX + EDX*0x2],0x0`.
            static_cast<unsigned short*>(Block)[Chars] = 0;
        } else {
            Wire->DisplayNameLength = 0;                       // 06d924df
        }
        if ((Flag2 & Mask) != 0) {                             // 06d924e9
            // 06d924f4 / 06d924ff: FUN_06da1620 on payload+0x00 and payload+0x08, each
            // a contiguous {dword size, void* buffer} pair.
            if (!RpcGetSizedBlock(Stream, Wire->NameASize, Wire->NameABuffer)) {
                return Stream.GetError();
            }
            if (!RpcGetSizedBlock(Stream, Wire->NameBSize, Wire->NameBBuffer)) {
                return Stream.GetError();
            }
        }
    }

    // 06d92504: SinglePlayer() AGAIN, and this is the fork the encoder makes on
    // IsMPServer() instead.
    if (SinglePlayer()) {
        const unsigned char Present = (unsigned char)(Flag8 | Flag10);      // 06d9251c OR
        if ((Mask & Present) != 0) {                                        // 06d9251e
            unsigned int Count = 0;
            if ((Mask & Flag10) != 0) {                                     // 06d92528
                unsigned short Wide = 0;
                if (!Stream.GetWord(Wide)) {                                // 06d9254f
                    return Stream.GetError();
                }
                Count = Wide;                                               // 06d92558 zero-extended
            } else {
                unsigned char Narrow = 0;
                if (!Stream.GetByte(Narrow)) {                              // 06d92581
                    return Stream.GetError();
                }
                Count = Narrow;                                             // 06d92583 MOVZX
            }
            // 06d92592 `TEST EAX,EAX ; JBE` -- zero elements skips the loop entirely.
            for (unsigned int Index = 0; Index < Count; ++Index) {
                // 06d925a2 operator new(8), bare, BEFORE the two reads: a failed bounds
                // check leaves through LAB_06d9265b without freeing it -- retail leaks the
                // element, and so does the port.
                FRpcRelation* Relation = static_cast<FRpcRelation*>(
                    ::operator new(sizeof(FRpcRelation), std::nothrow));
                if (!Stream.GetDword(Scratch)) {
                    return Stream.GetError();
                }
                Relation->Field_0x04 = Scratch;                             // 06d925d9
                if (!Stream.GetDword(Scratch)) {
                    return Stream.GetError();
                }
                Relation->Field_0x00 = Scratch;                             // 06d92605
                // 06d92613 operator new(0xc) -- the node; 06d92648 bumps payload+0x30.
                RpcRelationListLink(&Wire->Relations, Relation);
            }
        }
    } else {
        if (!Stream.GetFloat(Wire->Attitude)) {                             // 06d92689
            return Stream.GetError();                                       // LAB_06d92668
        }
    }
    // 06d926a0 FUN_06da13c0 -- consumes nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                                           // LAB_06d92703
    }
    *OutLength = Stream.Length();                                       // 06d926c1
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x54 FLPACKET_SERVER_MISCOBJUPDATE -- Encode LAB_06d98d30, Decode LAB_06d990d0
//
// Registry size 0x6c, payload 0x4c. The payload's own shape (a 16-bit presence mask at
// +0x00 and nine optional members behind it) was already recovered from the eight
// senders and the receiver; the two codec slots confirm it independently and add two
// corrections that are carried in rpc_payloads.h rather than here:
//
//   * payload+0x20 is a basic_string<UNSIGNED SHORT>. The encoder doubles the character
//     count before both the bounds check and the copy (06d98ef5 `SHL EAX,0x1`), the
//     decoder allocates `len * 2 + 2` (06d9926c) and measures with MSVCRT wcslen
//     (06d992d2), and the empty-string fallback is
//     ?_Nullstr@?$basic_string@GU?$char_traits@G@std@@... at [06db3190] (06d98f17).
//   * payload+0x48 is the Ids ELEMENT COUNT, written on a second hop by the push_back
//     helper FUN_06d98cf0 (06d98d1e..06d98d27), not a spare byte.
//
// Field order, identical in both directions, with the mask bit that gates each:
//     ---     word   the mask itself                    06d98da1 / 06d9913d
//     0x0008  dword  payload+0x08                       06d98dd8 / 06d9916f
//     0x0004  dword  payload+0x04                       06d98e0d / 06d9919f
//     0x0001  dword  payload+0x14                       06d98e42 / 06d991cf
//     0x0002  dword  payload+0x18 then dword +0x1c      06d98e77 / 06d991ff
//     0x0010  word   character count, then count*2 bytes 06d98ee3 / 06d9925d
//     0x0020  dword  payload+0x34                       06d98f6e / 06d99354
//     0x0040  word   payload+0x0c                       06d98fa5 / 06d99388
//     0x0080  dword  payload+0x10                       06d98fdf / 06d993bf
//     0x0100  byte   the Ids count, then that many dwords 06d99014 / 06d993f5
//
// TWO TESTS THAT READ AS SOMETHING ELSE:
//   * bit 0x0080 is tested as `06d98fb5 MOV AL,byte ptr [EBP+0x20] ; 06d98fb8 TEST AL,AL
//     ; 06d98fba JNS` -- not a comparison against zero, a test of bit 7 of the mask's
//     LOW byte. The decoder does the same at 06d99398..06d9939d.
//   * bit 0x0100 cannot be reached in the low byte at all, so both sides switch to the
//     HIGH byte: `06d98fed TEST byte ptr [EBP+0x21],0x1` and `06d993cd` likewise.
//     Reading either as a byte-wide mask test would silently drop the Ids array.
//
// The decoder does NOT trust the length it just read for the string: it copies the
// wire run into a scratch buffer, terminates it, and then takes wcslen of that
// (06d992d2), so an embedded NUL truncates the stored string. Reproduced.
// ===========================================================================


unsigned int RpcEncodeServerMiscObjUpdate(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerMiscObjUpdate* Wire =
        reinterpret_cast<const FPayloadServerMiscObjUpdate*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06d98d61 FUN_06da1380
    const unsigned short Flags = Wire->Flags;
    Stream.PutWord(Flags);                                     // 06d98da7
    if ((Flags & kMiscObjUpdateFlagField_0x08) != 0) {         // 06d98daf TEST ...,0x8
        Stream.PutDword(Wire->Field_0x08);                     // 06d98ddd
    }
    if ((Flags & kMiscObjUpdateFlagField_0x04) != 0) {         // 06d98de4
        Stream.PutDword(Wire->Field_0x04);                     // 06d98e12
    }
    if ((Flags & kMiscObjUpdateFlagField_0x14) != 0) {         // 06d98e19
        Stream.PutDword(Wire->Field_0x14);                     // 06d98e47
    }
    if ((Flags & kMiscObjUpdateFlagPair) != 0) {               // 06d98e4e
        Stream.PutDword(Wire->Field_0x18);                     // 06d98e7c
        Stream.PutDword(Wire->Field_0x1c);                     // 06d98eab
    }
    if ((Flags & kMiscObjUpdateFlagText) != 0) {               // 06d98eb2
        // 06d98ec1 `MOV CX,word ptr [EBP+0x48]` -- only the LOW WORD of the character
        // count travels, and 06d98ef0/06d98ef5 use that same word, zero-extended and
        // doubled, as the byte run.
        const unsigned short Chars = (unsigned short)(Wire->Text.size() & 0xffffu);
        Stream.PutWord(Chars);                                 // 06d98ee3
        // 06d98f12..06d98f17: retail's buffer pointer, or MSVCP60's _Nullstr when the
        // string never allocated -- which is what std::wstring::c_str() is.
        Stream.PutBytes(Wire->Text.c_str(), (unsigned int)Chars * 2u);   // 06d98f2e
    }
    if ((Flags & kMiscObjUpdateFlagField_0x34) != 0) {         // 06d98f45
        Stream.PutDword(Wire->Field_0x34);                     // 06d98f74
    }
    if ((Flags & kMiscObjUpdateFlagField_0x0c) != 0) {         // 06d98f7c
        Stream.PutWord(Wire->Field_0x0c);                      // 06d98fac
    }
    if ((Flags & kMiscObjUpdateFlagField_0x10) != 0) {         // 06d98fb5 TEST AL,AL / JNS
        Stream.PutDword(Wire->Field_0x10);                     // 06d98fe5
    }
    if ((Flags & kMiscObjUpdateFlagIds) != 0) {                // 06d98fed TEST [+0x21],1
        Stream.PutByte(Wire->Field_0x48);                      // 06d9901a
        // 06d99023/06d99026: the walk is Begin..End, and it ships every element the
        // vector holds whether or not the count byte agrees.
        for (std::vector<unsigned int>::const_iterator Id = Wire->Ids.begin();
             Id != Wire->Ids.end() && !Stream.Failed(); ++Id) {
            Stream.PutDword(*Id);                              // 06d99050
        }
    }
    // 06d99068 FUN_06da13c0 -- emits nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                              // LAB_06d99074
    }
    *OutLength = Stream.Length();                          // 06d99083
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerMiscObjUpdate(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength) {
    FPayloadServerMiscObjUpdate* Wire =
        reinterpret_cast<FPayloadServerMiscObjUpdate*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06d990ff FUN_06da13c0 -- consumes nothing.

    unsigned short Flags = 0;
    if (!Stream.GetWord(Flags)) {                              // 06d9913f
        return Stream.GetError();
    }
    Wire->Flags = Flags;                                       // 06d9914b
    unsigned int Scratch = 0;
    if ((Flags & kMiscObjUpdateFlagField_0x08) != 0) {         // 06d99149 TEST AL,0x8
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire->Field_0x08 = Scratch;                            // 06d99176
    }
    if ((Flags & kMiscObjUpdateFlagField_0x04) != 0) {         // 06d9917b
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire->Field_0x04 = Scratch;                            // 06d991a6
    }
    if ((Flags & kMiscObjUpdateFlagField_0x14) != 0) {         // 06d991ab
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire->Field_0x14 = Scratch;                            // 06d991d6
    }
    if ((Flags & kMiscObjUpdateFlagPair) != 0) {               // 06d991db
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire->Field_0x18 = Scratch;                            // 06d99206
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire->Field_0x1c = Scratch;                            // 06d99230
    }
    if ((Flags & kMiscObjUpdateFlagText) != 0) {               // 06d99235
        unsigned short WireChars = 0;
        if (!Stream.GetWord(WireChars)) {                      // 06d9925f
            return Stream.GetError();
        }
        // 06d9926c `LEA EDX,[EDI + EDI*0x1 + 0x2]` then MSVCRT malloc (06d99271), bare --
        // a scratch buffer of count*2 + 2 bytes. A failed bounds check (06d9929c JNZ
        // LAB_06d9969f) leaves without freeing it: retail leaks the buffer there, and so
        // does the port. Only the success path frees it (06d99320 MSVCRT free).
        const unsigned int Bytes = (unsigned int)WireChars * 2u;
        wchar_t* Temp = static_cast<wchar_t*>(malloc(Bytes + 2u));
        if (!Stream.GetBytes(Temp, Bytes)) {                   // 06d992b3
            return Stream.GetError();
        }
        Temp[WireChars] = 0;                                   // 06d992c9
        // 06d992d2 MSVCRT wcslen -- the STORED length comes from the terminator, not
        // from the count that arrived, so an embedded NUL truncates the string.
        unsigned int Chars = 0;
        while (Temp[Chars] != 0) {
            ++Chars;
        }
        // 06d992e2 `_Grow(Chars, 1)` on the packet's own string, then 06d99300 copies
        // Chars characters in, 06d99312 stores the length and 06d99315 terminates:
        // basic_string::assign(Temp, Chars).
        Wire->Text.assign(Temp, (size_t)Chars);
        free(Temp);                                            // 06d99320 MSVCRT free
    }
    if ((Flags & kMiscObjUpdateFlagField_0x34) != 0) {         // 06d9932e
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire->Field_0x34 = Scratch;                            // 06d9935c
    }
    if ((Flags & kMiscObjUpdateFlagField_0x0c) != 0) {         // 06d99362
        unsigned short Word = 0;
        if (!Stream.GetWord(Word)) { return Stream.GetError(); }
        Wire->Field_0x0c = Word;                               // 06d99391
    }
    if ((Flags & kMiscObjUpdateFlagField_0x10) != 0) {         // 06d99398 TEST AL,AL / JNS
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire->Field_0x10 = Scratch;                            // 06d993c7
    }
    if ((Flags & kMiscObjUpdateFlagIds) != 0) {                // 06d993cd TEST [+0x21],1
        unsigned char Count = 0;
        if (!Stream.GetByte(Count)) {                          // 06d993f8
            return Stream.GetError();
        }
        Wire->Field_0x48 = Count;                              // 06d99402
        // 06d99400 `TEST AL,AL ; JBE` -- a zero count skips the loop; 06d9960d compares
        // the running index against this same byte, zero-extended.
        for (unsigned int Index = 0; Index < (unsigned int)Count; ++Index) {
            if (!Stream.GetDword(Scratch)) {                   // 06d9943a
                return Stream.GetError();
            }
            // 06d99442..06d99609: the inlined vector::insert(End, 1, value) -- push_back.
            Wire->Ids.push_back(Scratch);
        }
    }
    // 06d99632 FUN_06da13c0 -- consumes nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                              // LAB_06d9969f
    }
    *OutLength = Stream.Length();                          // 06d9965d
    // The byte-statistics tail retail runs on the SUCCESS PATH ONLY -- its error
    // exits jump straight past it. See FPacketWireStats.
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x4f FLPACKET_SERVER_SENDCOMM -- Encode LAB_06dad000, Decode LAB_06dad320
//
// The constructor recipe at the head of this file places the pair. Constructor
// FUN_06d9eab0:
//   06d9ead4  MOV dword ptr [EAX],.rdata:PTR_FUN_06db52dc
//   06d9eada  MOV dword ptr [EAX + 0xc],0x2004f
//   vtable 06db52dc = { dtor FUN_06d9eaf0, Encode LAB_06dad000,
//                       Decode LAB_06dad320, dump }
// and the slots' XREF headers agree: LAB_06dad000 from 06db52e0 (+4), LAB_06dad320
// from 06db52e4 (+8).
//
// That constructor is worth reading for one more reason: 06d9eab7 `MOV EDX,0x1` and
// 06d9eae4 `MOV byte ptr [EAX + 0x74],DL` DEFAULT THE DISCRIMINATOR AT payload+0x54 TO
// 1, so a freshly built SENDCOMM carries its id block unless something clears the flag.
// It also zeroes payload+0x3c (06d9ead1, the id count) and payload+0x44 (06d9eae1, the
// buffer pointer), which is what makes the decoder's free-then-allocate safe on a
// packet that has never been decoded.
//
// Registry size 0x78, payload 0x58 -- and FPayloadServerSendComm was recovered from the
// OTHER two ends (the IClient sender 0x06d88fc0 and the HandlePacket case at
// 0x06d8306c) before this codec was read. The two readings agree field for field, which
// is the strongest confirmation the 0x34-byte FRpcIdArrayBlock has had:
//
//     Body.Field_0x00  payload+0x0c   dword on the wire        06dad115 / 06dad43a
//     Body.Field_0x04  payload+0x10   dword on the wire        06dad13f / 06dad464
//     Body.Field_0x08  payload+0x14   NEVER TRAVELS
//     Body.Field_0x0c  payload+0x18   NEVER TRAVELS
//     Body.Ids[0..7]   payload+0x1c   dwords, Count of them    06dad19f / 06dad4c4
//     Body.Count       payload+0x3c   LOW BYTE on the wire     06dad168 / 06dad498
//
// i.e. the encoder's array cursor starts at `LEA EBP,[EDI+0x3c]` == packet+0x3c ==
// payload+0x1c (06dad176) and its loop bound is the dword at packet+0x5c ==
// payload+0x3c (06dad16d), exactly where the block puts Ids and Count. A layout guessed
// from one side would not have landed on that by accident.
//
// ONE CORRECTION TO THE PAYLOAD HEADER. rpc_payloads.h says of Field_0x54 that it "is
// written and never read back -- a dead field of the retail wire format". That is true
// of the DELIVERY path only. On the wire it is the discriminator for the whole id
// block: the encoder emits it at 06dad0e2 `MOV DL,byte ptr [EDI+0x74]`, re-reads it at
// 06dad0ea `MOV CL,byte ptr [EDI+0x74]` and jumps over payload+0x0c..+0x3c when it is
// zero (06dad0ef `JZ LAB_06dad1b6`); the decoder writes it at 06dad40a and takes the
// matching branch at 06dad40d. Far from dead -- dropping it would drop an entire array
// off the wire. The header comment is corrected in this wave.
//
// The rest of the wire, in order:
//     payload+0x00  dword                                     06dad069 / 06dad38c
//     payload+0x04  dword                                     06dad093 / 06dad3b6
//     payload+0x08  dword                                     06dad0bd / 06dad3e0
//     payload+0x54  byte, the discriminator above             06dad0e5 / 06dad40a
//   if (payload+0x54 != 0): the id block, as tabulated above
//     payload+0x40  dword                                     06dad1d9 / 06dad513
//     payload+0x48  byte, BufferCount                         06dad201 / 06dad53e
//     payload+0x44  BufferCount dwords THROUGH A POINTER      06dad235 / 06dad591
//     payload+0x4c  dword, but only its LOW WORD travels      06dad267 / 06dad5d3
//     payload+0x50  float, quantised to one byte              06dad2ad / 06dad61a
//
// THREE THINGS THAT ARE EASY TO GET BACKWARDS, all settled on bytes:
//
//  1. The two counts are not the same shape. Body.Count is a DWORD in the payload read
//     with a full-width signed test (06dad16d `MOV ECX,[EDI+0x5c]` / 06dad172
//     `TEST ECX,ECX` / 06dad174 `JLE`) but only its low byte travels (06dad144
//     `MOV DL,byte ptr [EDI+0x5c]`), and it comes back ZERO-extended (06dad490
//     `XOR EAX,EAX` / 06dad492 `MOV AL,CL`). BufferCount is a BYTE in the payload and is
//     read back SIGN-extended everywhere it is used (06dad23c and 06dad597
//     `MOVSX ECX/EAX,byte ptr [EDI+0x68]`, and again at 06dad54c before the
//     allocation). Same direction, two different extensions, one packet.
//
//  2. The decoder OWNS the buffer. 06dad539..06dad549 frees whatever pointer is already
//     at payload+0x44 (FUN_06db1dc2 is free), then 06dad54c `MOVSX EAX,byte ptr
//     [EDI+0x68]` / 06dad550 `SHL EAX,0x2` / 06dad554 `operator new` allocates
//     BufferCount * 4 and stores it back. ~FPacket_FLPACKET_SERVER_SENDCOMM (rpc_packet_registry.cpp) is
//     already the matching teardown, so no new ownership rule is introduced here.
//
//  3. payload+0x50 is the only field in the module quantised in TWO stages: 06dad26a
//     `FLD float ptr [EDI+0x70]`, 06dad26d `FMUL float 0.1f`, 06dad278 `FMUL double
//     255.0`, ftol, one byte. The decoder undoes it with the single constant
//     0.0392156862745098 == 1/25.5 (06dad614) and ZERO-extends the byte first (06dad5fb
//     `XOR EDX,EDX` / 06dad5fd `MOV DL,CL`). So the field covers [0,10], not [0,1], and
//     the tier is unsigned -- the opposite of the orientation bytes in rpc_wire_ch1.cpp.
//
// Single player never calls either slot; the packet is queued by pointer.
// ===========================================================================

// payload+0x50 is declared as raw bits because the delivery path moves it as a stack
// dword; the codec needs it as a float, so the four bytes are copied rather than
// type-punned through a pointer cast.
static void RpcSendCommMoveFloatBits(void* Dest, const void* Source) {
    unsigned char* Out = static_cast<unsigned char*>(Dest);
    const unsigned char* In = static_cast<const unsigned char*>(Source);
    for (unsigned int i = 0; i < 4u; ++i) {
        Out[i] = In[i];
    }
}

// The inline array is eight entries wide -- (0x3c - 0x1c) / 4 -- and retail bounds both
// Ids loops by the count alone, RE-READ from payload+0x3c on every pass (06dad1a1 /
// 06dad4c9) and compared SIGNED (06dad1b4 / 06dad4d4 JL). A count above eight walks off
// the end of Ids -- Ids[8] IS the count -- and the port does the same.

unsigned int RpcEncodeServerSendComm(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerSendComm* Wire =
        reinterpret_cast<const FPayloadServerSendComm*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06dad031 FUN_06da1380

    Stream.PutDword(Wire->Field_0x00);                         // 06dad069
    Stream.PutDword(Wire->Field_0x04);                         // 06dad093
    Stream.PutDword(Wire->Field_0x08);                         // 06dad0bd
    Stream.PutByte(Wire->Field_0x54);                          // 06dad0e5
    if (Wire->Field_0x54 != 0) {                               // 06dad0ea / 06dad0ef JZ
        Stream.PutDword(Wire->Body.Field_0x00);                // 06dad115
        Stream.PutDword(Wire->Body.Field_0x04);                // 06dad13f
        // 06dad144 takes the LOW BYTE of the dword count; 06dad16d re-reads the whole
        // dword for the loop bound, signed.
        Stream.PutByte((unsigned char)((unsigned int)Wire->Body.Count & 0xffu));  // 06dad168
        const unsigned int* const IdSlots = Wire->Body.Ids;
        for (int Index = 0; Index < Wire->Body.Count && !Stream.Failed(); ++Index) {
            Stream.PutDword(IdSlots[Index]);                   // 06dad19f
        }
    }
    Stream.PutDword(Wire->Field_0x40);                         // 06dad1d9
    Stream.PutByte(Wire->BufferCount);                         // 06dad201
    // 06dad206 `MOV CL,byte ptr [EDI+0x68]` / 06dad20b `TEST CL,CL` / 06dad20d `JLE`,
    // and 06dad23c `MOVSX ECX,byte ptr [EDI+0x68]` for the loop bound: a SIGNED byte.
    const int SignedBufferCount = (int)(signed char)Wire->BufferCount;
    if (SignedBufferCount > 0) {
        // 06dad22c dereferences payload+0x44 with no null test.
        for (int Index = 0; Index < SignedBufferCount && !Stream.Failed(); ++Index) {
            Stream.PutDword(Wire->Buffer[Index]);              // 06dad235
        }
    }
    // 06dad24a `MOV CX,word ptr [EDI+0x6c]` -- the dword's low half only.
    Stream.PutWord((unsigned short)(Wire->Field_0x4c & 0xffffu));   // 06dad267
    float Scaled = 0.0f;
    RpcSendCommMoveFloatBits(&Scaled, &Wire->Field_0x50Bits);
    Stream.PutQuantizedTwoStage(Scaled, FRpcMarshalStream::kTenthScale,
                                FRpcMarshalStream::kPercentScale);  // 06dad26a..06dad2ad

    // 06dad2ba FUN_06da13c0 -- emits nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                              // 06dad2c6 / LAB_06dad30f
    }
    *OutLength = Stream.Length();                          // 06dad2d8
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());  // 06dad2de..
    return 0;
}

unsigned int RpcDecodeServerSendComm(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength) {
    FPayloadServerSendComm* Wire =
        reinterpret_cast<FPayloadServerSendComm*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06dad346 FUN_06da13c0 at the start -- consumes nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                              // 06dad350
    }

    unsigned int Scratch = 0;
    unsigned short Small16 = 0;
    unsigned char Small8 = 0;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x00 = Scratch;                                // 06dad38c
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x04 = Scratch;                                // 06dad3b6
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x08 = Scratch;                                // 06dad3e0
    if (!Stream.GetByte(Small8)) { return Stream.GetError(); }
    Wire->Field_0x54 = Small8;                                 // 06dad40a
    if (Small8 != 0) {                                         // 06dad40d JZ
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire->Body.Field_0x00 = Scratch;                       // 06dad43a
        if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
        Wire->Body.Field_0x04 = Scratch;                       // 06dad464
        // 06dad467 / 06dad46a clear these two from EBX, which 06dad336 zeroed. They are
        // not on the wire at all, and the decoder is what makes that visible.
        Wire->Body.Field_0x08 = 0;
        Wire->Body.Field_0x0c = 0;
        if (!Stream.GetByte(Small8)) { return Stream.GetError(); }
        // 06dad490 `XOR EAX,EAX` / 06dad492 `MOV AL,CL` -- zero-extended into the dword.
        Wire->Body.Count = (int)(unsigned int)Small8;          // 06dad498
        unsigned int* const IdSlots = Wire->Body.Ids;
        for (int Index = 0; Index < Wire->Body.Count; ++Index) {
            if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
            IdSlots[Index] = Scratch;                          // 06dad4c4
        }
    } else {
        // LAB_06dad4dd: 06dad4dd..06dad4e9 clear payload+0x0c, +0x10, +0x14, +0x18 and
        // +0x3c, i.e. the whole id block except its Ids array.
        Wire->Body.Field_0x00 = 0;
        Wire->Body.Field_0x04 = 0;
        Wire->Body.Field_0x08 = 0;
        Wire->Body.Field_0x0c = 0;
        Wire->Body.Count = 0;
    }
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x40 = Scratch;                                // 06dad513
    if (!Stream.GetByte(Small8)) { return Stream.GetError(); }
    Wire->BufferCount = Small8;                                // 06dad53e

    // 06dad539 tests the OLD pointer against zero and frees it before allocating the new
    // one, so a packet decoded twice does not leak.
    if (Wire->Buffer != nullptr) {
        ::operator delete(Wire->Buffer);                       // 06dad544
        Wire->Buffer = nullptr;
    }
    const int SignedBufferCount = (int)(signed char)Wire->BufferCount;   // 06dad54c MOVSX
    // 06dad550 `SHL EAX,0x2` then a bare operator new (06dad554), with no sign test: a
    // wire byte of 0x80..0xff reaches operator new as a negative count shifted left, and
    // a zero count allocates zero bytes. The loop alone is gated (06dad564 JLE).
    Wire->Buffer = static_cast<unsigned int*>(
        ::operator new((unsigned int)SignedBufferCount << 2, std::nothrow));  // 06dad559
    if (SignedBufferCount > 0) {
        for (int Index = 0; Index < SignedBufferCount; ++Index) {
            if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
            Wire->Buffer[Index] = Scratch;                     // 06dad591
        }
    }
    if (!Stream.GetWord(Small16)) { return Stream.GetError(); }
    // 06dad5cd `MOVZX ECX,CX` -- the word widens into the whole dword.
    Wire->Field_0x4c = (unsigned int)Small16;                  // 06dad5d3
    float Scaled = 0.0f;
    if (!Stream.GetQuantizedUnsigned(Scaled, FRpcMarshalStream::kTenthInvScale)) {
        return Stream.GetError();                              // 06dad5f9..06dad61a
    }
    RpcSendCommMoveFloatBits(&Wire->Field_0x50Bits, &Scaled);

    // 06dad61d FUN_06da13c0 -- consumes nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                              // LAB_06dad635 / 06dad689
    }
    *OutLength = Stream.Length();                          // 06dad645
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());  // 06dad64b..06dad684
    return 0;
}

// ===========================================================================
// The orientation quadruple below lives in payload fields the port declares as raw
// dwords (the payload-level marshallers copy argument dwords into them), while the
// codec really does FLD / FMUL / ftol on them. These two turn one into the other; it is
// the same reinterpretation retail gets for free by putting an FLD where a MOV would
// have gone. Kept file-local: rpc_containers.cpp has its own static pair.
// ===========================================================================
static float RpcCreateBitsToFloat(unsigned int Bits) {
    float Value = 0.0f;
    memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

static unsigned int RpcCreateFloatToBits(float Value) {
    unsigned int Bits = 0;
    memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

// ===========================================================================
// ch2 0x37 FLPACKET_SERVER_CREATEGUIDED -- Encode LAB_06da84e0, Decode FUN_06da8810
//
// Constructor FUN_06d9c9a0, and it names the packet without a guess:
//     06d9c9cd  MOV dword ptr [EAX],.rdata:PTR_FUN_06db500c
//     06d9c9d3  MOV dword ptr [EAX + 0xc],0x20037          ; channel 2, opcode 0x37
// vtable 06db500c = { dtor FUN_06d9af50, Encode LAB_06da84e0 (06db5010),
//                     Decode FUN_06da8810 (06db5014), the shared dump body }.
// Registry 0x58, payload 0x38 (0x58 - 0x20).
//
// THE CONSTRUCTOR IS ALSO THE SECOND WITNESS FOR THE QUATERNION, exactly as it was for
// ch1 0x01 UPDATEOBJECT: 06d9c9bd writes 0x3f800000 to packet+0x48 and 06d9c9c4 /
// 06d9c9c7 / 06d9c9ca zero the next three dwords, so payload+0x28 is W and +0x2c..+0x34
// are X, Y, Z. The codec sends X, Y, Z and W LAST -- the memory order and the wire order
// genuinely differ, which is why the constructor is worth reading separately.
//
// PAYLOAD+0x00 NEVER CROSSES THE WIRE. The encoder touches packet+0x24..+0x54 and never
// +0x20, and the decoder settles it beyond argument-from-absence: 06da8850
// `MOV dword ptr [EDI + 0x20],EBP` with EBP already zeroed at 06da8827 writes a literal
// zero there before reading anything. FUN_06da1380 does not emit it either -- its whole
// body is the 0xfff / 0xf000 routing word out of packet+0xc and packet+0xe.
//
// ENCODE field order, each with the listing address of the store:
//     PutWord(LargeIDToSmallID(payload+0x04))   06da8533 CALL / 06da8564 MOV word
//     PutDword(payload+0x08)                    06da858e / 06da8591
//     PutWord (payload+0x0c)                    06da85ba MOV DX,word ptr [EDI + 0x2c]
//     PutDword(payload+0x10)                    06da85e8
//     PutWord (payload+0x14)                    06da8614 MOV DX,word ptr [EDI + 0x34]
//     PutDword(payload+0x18)                    06da8642
//     PutBytes(payload+0x1c, 12)                06da866b..06da868a FUN_06d8f510
//     four quantized bytes                      see below
//
// The quantizer is retail's DAT_06db3fd8 = 127.0 and the decoder's DAT_06db3fe0 = 1/127,
// i.e. FRpcMarshalStream::kOrientationScale / kOrientationInvScale. The decoder
// SIGN-extends all four -- 06da8a24 `MOVSX EAX,byte ptr [ESP + 0x18]`, 06da8a2d,
// 06da8a40 `MOVSX EDX,DL`, 06da8a43 `MOVSX EAX,CL` -- so GetQuantizedSigned, not
// GetQuantizedUnsigned.
//
// A trap worth naming, because the encoder computes its four quantized bytes into stack
// slots long before it writes them: 06da86ac stores the FIRST ftol result at [ESP+0x28],
// 06da86be the second at [ESP+0x24] and 06da86d0 the third at [ESP+0x13], while the
// fourth stays in AL. With `SUB ESP,0x10` plus four PUSHes in the prologue, [ESP+0x24]
// and [ESP+0x28] are the codec's OWN INCOMING ARGUMENT SLOTS, reused as scratch now that
// arg1 and arg2 are spent -- and [ESP+0x2c], arg3, is left alone because 06da87ba still
// needs it for the out-length. Reading those three stores as three consecutive locals
// would have permuted the quadruple.
// ===========================================================================

unsigned int RpcEncodeServerCreateGuided(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerCreateGuided* Wire =
        reinterpret_cast<const FPayloadServerCreateGuided*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06da8514 FUN_06da1380
    Stream.PutWord(Archetype::LargeIDToSmallID(Wire->Field_0x04));    // 06da8533 / 06da8564
    Stream.PutDword(Wire->Field_0x08);                                // 06da8591
    Stream.PutWord(Wire->Field_0x0c);                                 // 06da85be
    Stream.PutDword(Wire->Field_0x10);                                // 06da85eb
    Stream.PutWord(Wire->Field_0x14);                                 // 06da8618
    Stream.PutDword(Wire->Field_0x18);                                // 06da8645
    Stream.PutBytes(&Wire->Field_0x1c, 12);                           // 06da868a
    Stream.PutQuantized(RpcCreateBitsToFloat(Wire->Field_0x2c),
                        FRpcMarshalStream::kOrientationScale);        // 06da868f -> 06da8709
    Stream.PutQuantized(RpcCreateBitsToFloat(Wire->Field_0x30),
                        FRpcMarshalStream::kOrientationScale);        // 06da86a3 -> 06da8738
    Stream.PutQuantized(RpcCreateBitsToFloat(Wire->Field_0x34),
                        FRpcMarshalStream::kOrientationScale);        // 06da86b5 -> 06da8767
    Stream.PutQuantized(RpcCreateBitsToFloat(Wire->Field_0x28),
                        FRpcMarshalStream::kOrientationScale);        // 06da86c7 -> 06da8792

    // 06da87a0 FUN_06da13c0 -- `XOR EAX,EAX ; RET 0xc`, emits nothing.
    if (Stream.Failed()) {
        // LAB_06da87f7 and the five inline exits: the out-length is NOT written.
        return Stream.GetError();
    }
    *OutLength = Stream.Length();                                 // 06da87be
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());         // 06da87c4..06da87f2
    return 0;
}

unsigned int RpcDecodeServerCreateGuided(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength) {
    FPayloadServerCreateGuided* Wire =
        reinterpret_cast<FPayloadServerCreateGuided*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06da8837 FUN_06da13c0 -- consumes nothing.

    Wire->Field_0x00 = 0;                                             // 06da8850
    unsigned short Small16 = 0;
    unsigned int Scratch = 0;
    if (!Stream.GetWord(Small16)) { return Stream.GetError(); }
    // 06da8877 `XOR ECX,ECX ; MOV CX,[EAX]` -- zero-extended before the call.
    Wire->Field_0x04 = Archetype::SmallIDToLargeID(Small16);          // 06da8888
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x08 = Scratch;                                       // 06da88ba
    if (!Stream.GetWord(Small16)) { return Stream.GetError(); }
    Wire->Field_0x0c = Small16;                                       // 06da88e3
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x10 = Scratch;                                       // 06da890c
    if (!Stream.GetWord(Small16)) { return Stream.GetError(); }
    Wire->Field_0x14 = Small16;                                       // 06da8935
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x18 = Scratch;                                       // 06da895e
    if (!Stream.GetBytes(&Wire->Field_0x1c, 12)) {                    // 06da8983 FUN_06d8f510
        return Stream.GetError();
    }

    // 06da8a32..06da8a7e: four FILD / FMUL DAT_06db3fe0 / FSTP, in the byte order the
    // encoder wrote them -- payload+0x2c, +0x30, +0x34, then W at +0x28.
    float Quantized = 0.0f;
    if (!Stream.GetQuantizedSigned(Quantized, FRpcMarshalStream::kOrientationInvScale)) {
        return Stream.GetError();
    }
    Wire->Field_0x2c = RpcCreateFloatToBits(Quantized);               // 06da8a46
    if (!Stream.GetQuantizedSigned(Quantized, FRpcMarshalStream::kOrientationInvScale)) {
        return Stream.GetError();
    }
    Wire->Field_0x30 = RpcCreateFloatToBits(Quantized);               // 06da8a5e
    if (!Stream.GetQuantizedSigned(Quantized, FRpcMarshalStream::kOrientationInvScale)) {
        return Stream.GetError();
    }
    Wire->Field_0x34 = RpcCreateFloatToBits(Quantized);               // 06da8a71
    if (!Stream.GetQuantizedSigned(Quantized, FRpcMarshalStream::kOrientationInvScale)) {
        return Stream.GetError();
    }
    Wire->Field_0x28 = RpcCreateFloatToBits(Quantized);               // 06da8a7e

    // 06da8a81 FUN_06da13c0 -- consumes nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                                     // LAB_06da8ae1
    }
    *OutLength = Stream.Length();                                 // 06da8a9d
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());         // 06da8aa3..06da8adc
    return 0;
}

// ===========================================================================
// ch2 0x2c FLPACKET_SERVER_CREATEMINE   -- Encode LAB_06da95d0, Decode LAB_06da98d0
// ch2 0x2d FLPACKET_SERVER_CREATECOUNTER -- Encode LAB_06da9b50, Decode LAB_06da9e40
//
// "BOTH PAYLOAD 0x68 AND LIKELY SIBLINGS, TAKE THEM AS ONE PAIR" DOES NOT SURVIVE THE
// BYTES, on either half of the claim.
//
//   * 0x68 is the REGISTRY ALLOCATION, not the payload. Payload == 0x68 - 0x20 == 0x48
//     for both, which is what FPayloadServerCreateMine and FPayloadServerCreateCounter
//     already declare and what both constructors confirm by writing packet+0x20..+0x64.
//   * CREATEMINE translates its archetype -- 06da965a
//     `CALL dword ptr [->COMMON.DLL::Archetype::LargeIDToSmallID]` on payload+0x04, and
//     06da996e SmallIDToLargeID on the way back. CREATECOUNTER calls NEITHER. Its whole
//     body (listing lines 78652..78949 and 78964..79204) contains no COMMON.DLL import
//     at all. A shared codec would have shipped a small id here and corrupted the field.
//   * CREATEMINE puts three quarters of its payload behind `if (SinglePlayer())` --
//     06da9745 `CALL dword ptr [->COMMON.DLL::SinglePlayer] ; TEST AL,AL ;
//     JZ LAB_06da985b`, mirrored by the decoder at 06da99fe. CREATECOUNTER has no
//     conditional at all: every one of its six early exits is the sticky-error exit.
//   * the field widths differ exactly where the two look most alike. Both constructors
//     seed a unit quaternion, at offsets one field apart:
//         CREATEMINE    06d9ce1d MOV word ptr [EAX + 0x2c],DX    ; payload+0x0c is a WORD
//                       06d9ce21 MOV dword ptr [EAX + 0x30],0x3f800000
//         CREATECOUNTER 06d9cec9 MOV EDX,dword ptr [->COMMON.DLL::SUBOBJ_ID_NONE]
//                       06d9ced2 MOV word ptr [EAX + 0x30],DX    ; payload+0x10 is a WORD
//                       06d9ced6 MOV dword ptr [EAX + 0x34],0x3f800000
//     so CREATECOUNTER carries one extra dword at payload+0x0c, and its word field is a
//     subobject id seeded from a Common.dll global rather than a zero.
//
// They are transcribed separately below. Recorded as a disproof rather than a silent
// divergence, because "0x68 == 0x68, therefore one codec" is exactly the shape of a
// fabrication that would have compiled and stayed invisible until a mine went up.
//
// CREATEMINE: constructor FUN_06d9ce00 -- 06d9ce4f writes the vtable 06db506c, 06d9ce55
// writes 0x2002c. CREATECOUNTER: constructor FUN_06d9cea0 -- 06d9cf01 writes 06db507c,
// 06d9cf07 writes 0x2002d.
//
// CREATEMINE's payload+0x44 does not cross the wire, and the decoder says so out loud
// rather than by omission: 06da9aab `MOV dword ptr [EDI + 0x64],0x0`, placed after the
// single-player join label LAB_06da9aa2 so it runs on both paths.
//
// Neither codec quantizes anything. There is no FMUL in either of the four bodies; the
// position and orientation blocks move through FUN_06d8f510 (three dwords,
// 06d8f510..06d8f524) and FUN_06d8f4f0 (four dwords, 06d8f4f0..06d8f50a) as raw bytes.
// ===========================================================================

unsigned int RpcEncodeServerCreateMine(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerCreateMine* Wire =
        reinterpret_cast<const FPayloadServerCreateMine*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06da9604 FUN_06da1380
    Stream.PutDword(Wire->Field_0x00);                                // 06da964f
    Stream.PutWord(Archetype::LargeIDToSmallID(Wire->Field_0x04));    // 06da965a / 06da9689
    Stream.PutDword(Wire->Field_0x08);                                // 06da96bc
    Stream.PutWord(Wire->Field_0x0c);                                 // 06da96ef
    // 06da971d..06da973b: the three dwords at packet+0x40 / +0x44 / +0x48, i.e.
    // payload+0x20..+0x2b -- NOT payload+0x10, which travels later and only in SP.
    Stream.PutBytes(&Wire->Field_0x20, 12);                           // 06da973b
    if (SinglePlayer()) {                                             // 06da9745 / 06da974d
        Stream.PutBytes(&Wire->Field_0x10, 16);                       // 06da97a7 FUN_06d8f4f0
        Stream.PutBytes(&Wire->Field_0x2c, 12);                       // 06da97fc FUN_06d8f510
        Stream.PutBytes(&Wire->Field_0x38, 12);                       // 06da9851 FUN_06d8f510
    }

    // 06da9863 FUN_06da13c0 -- emits nothing.
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();                                 // 06da9884
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());         // 06da988a..06da98bc
    return 0;
}

unsigned int RpcDecodeServerCreateMine(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength) {
    FPayloadServerCreateMine* Wire =
        reinterpret_cast<FPayloadServerCreateMine*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06da98fc FUN_06da13c0 -- consumes nothing.

    unsigned int Scratch = 0;
    unsigned short Small16 = 0;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x00 = Scratch;                                       // 06da993e
    if (!Stream.GetWord(Small16)) { return Stream.GetError(); }
    Wire->Field_0x04 = Archetype::SmallIDToLargeID(Small16);          // 06da996e / 06da9974
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x08 = Scratch;                                       // 06da99a1
    if (!Stream.GetWord(Small16)) { return Stream.GetError(); }
    Wire->Field_0x0c = Small16;                                       // 06da99ca
    if (!Stream.GetBytes(&Wire->Field_0x20, 12)) {                    // 06da99f4
        return Stream.GetError();
    }
    if (SinglePlayer()) {                                             // 06da99fe / 06da9a06
        if (!Stream.GetBytes(&Wire->Field_0x10, 16)) {                // 06da9a34
            return Stream.GetError();
        }
        if (!Stream.GetBytes(&Wire->Field_0x2c, 12)) {                // 06da9a66
            return Stream.GetError();
        }
        if (!Stream.GetBytes(&Wire->Field_0x38, 12)) {                // 06da9a98
            return Stream.GetError();
        }
    }
    Wire->Field_0x44 = 0;                                             // 06da9aab

    // 06da9ab2 FUN_06da13c0 -- consumes nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                                     // LAB_06da9b14
    }
    *OutLength = Stream.Length();                                 // 06da9ad0
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());         // 06da9ad6..06da9b0f
    return 0;
}

unsigned int RpcEncodeServerCreateCounter(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerCreateCounter* Wire =
        reinterpret_cast<const FPayloadServerCreateCounter*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06da9b84 FUN_06da1380
    Stream.PutDword(Wire->Field_0x00);                                // 06da9bc7
    Stream.PutDword(Wire->Field_0x04);                                // 06da9bf1
    Stream.PutDword(Wire->Field_0x08);                                // 06da9c1b
    Stream.PutDword(Wire->Field_0x0c);                                // 06da9c45
    Stream.PutWord(Wire->Field_0x10);                                 // 06da9c70
    Stream.PutBytes(&Wire->Field_0x14, 16);                           // 06da9cbd FUN_06d8f4f0
    Stream.PutBytes(&Wire->Field_0x24, 12);                           // 06da9d12 FUN_06d8f510
    Stream.PutBytes(&Wire->Field_0x30, 12);                           // 06da9d67 FUN_06d8f510
    Stream.PutBytes(&Wire->Field_0x3c, 12);                           // 06da9dbc FUN_06d8f510

    // 06da9dce FUN_06da13c0 -- emits nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                                     // LAB_06da9e28
    }
    *OutLength = Stream.Length();                                 // 06da9def
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());         // 06da9df5..06da9e23
    return 0;
}

unsigned int RpcDecodeServerCreateCounter(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength) {
    FPayloadServerCreateCounter* Wire =
        reinterpret_cast<FPayloadServerCreateCounter*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06da9e6c FUN_06da13c0 -- consumes nothing.

    unsigned int Scratch = 0;
    unsigned short Small16 = 0;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x00 = Scratch;                                       // 06da9eb0
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x04 = Scratch;                                       // 06da9eda
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x08 = Scratch;                                       // 06da9f04
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x0c = Scratch;                                       // 06da9f2e
    if (!Stream.GetWord(Small16)) { return Stream.GetError(); }
    Wire->Field_0x10 = Small16;                                       // 06da9f59
    if (!Stream.GetBytes(&Wire->Field_0x14, 16)) {                    // 06da9f81
        return Stream.GetError();
    }
    if (!Stream.GetBytes(&Wire->Field_0x24, 12)) {                    // 06da9fb3
        return Stream.GetError();
    }
    if (!Stream.GetBytes(&Wire->Field_0x30, 12)) {                    // 06da9fe5
        return Stream.GetError();
    }
    if (!Stream.GetBytes(&Wire->Field_0x3c, 12)) {                    // 06daa013
        return Stream.GetError();
    }

    // 06daa026 FUN_06da13c0 -- consumes nothing.
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());         // 06daa04a onwards
    return 0;
}

// ===========================================================================
// ch2 0x2e FLPACKET_SERVER_SETADDITEM -- Encode LAB_06d96f90, Decode LAB_06d97210
//
// Constructor FUN_06d9c440: 06d9c45d writes the vtable 06db3ee4, 06d9c463 writes
// 0x2002e. Registry 0x40, payload 0x20. The constructor also confirms one width the
// payload struct already had right -- 06d9c457 `MOV byte ptr [EAX + 0x39],BL` is a BYTE
// store to payload+0x19, which is bMission.
//
// THE WIRE ORDER IS NOT THE STRUCT ORDER, and it is not the argument order either:
//     PutDword(ArchId)          payload+0x00   06d97003
//     PutDword(Id)              payload+0x10   06d97032   <- jumps forward
//     PutDword(Count)           payload+0x04   06d97061   <- and back
//     PutFloat(Health)          payload+0x14   06d97090 FLD / 06d97096 FSTP
//     PutByte (bEquipped)       payload+0x18   06d970be
//     PutByte (bMission)        payload+0x19   06d970e9
//     PutDword(Field_0x0c)      payload+0x0c   06d97116
//     PutDword(HardpointSize)   payload+0x08   06d97145
//     the hardpoint bytes                      06d97178 REP MOVSD / REP MOVSB
// and the decoder is a strict inverse in the same order (06d9727e, 06d972a6, 06d972ce,
// 06d972f6, 06d9731a, 06d9733e, 06d97366, 06d97392). That is what turns this layout from
// "read" into "checked": the same permutation appears twice, from two independent bodies.
//
// Health travels through FLD/FSTP on the way out (06d97090) and a plain dword MOV on the
// way back (06d972f1 `ADD EAX,0x4` / 06d972f6 `MOV [EBP+0x34],EDX`). Both are four raw
// bytes; there is no FMUL anywhere in either body, so this field is NOT quantized and
// scaling it by analogy with the create-object health would have been an invention.
//
// The trailing string is guarded by a nonzero length on both sides -- 06d9714f
// `MOV EAX,[EBP+0x28] ; TEST EAX,EAX ; JBE LAB_06d971a2` on the way out (CF is clear
// after TEST, so the JBE is a JZ) and 06d97390 on the way back. HardpointSize already
// includes the terminator: the payload header records that the sender stores strlen + 1.
//
// TWO RETAIL BEHAVIOURS REPRODUCED, not corrected:
//   * the decoder calls operator new (06d97398) BEFORE the bounds check that covers the
//     bytes (06d973a3..06d973b1), so a truncated packet leaves the block allocated and
//     only the packet's destructor frees it. Same shape as FUN_06da1620.
//   * it does not free a previous Hardpoint first. ch2 0x4f SENDCOMM's decoder does
//     (06dad539 tests the old pointer, 06dad544 frees it); this one does not, so a packet
//     decoded twice leaks the first buffer. Left as retail has it -- see
//     docs/re/deviations.md.
// ===========================================================================

unsigned int RpcEncodeServerSetAddItem(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerSetAddItem* Wire =
        reinterpret_cast<const FPayloadServerSetAddItem*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06d96fc1 FUN_06da1380
    Stream.PutDword(Wire->ArchId);                                    // 06d97006
    Stream.PutDword(Wire->Id);                                        // 06d97035
    Stream.PutDword((unsigned int)Wire->Count);                       // 06d97064
    Stream.PutFloat(Wire->Health);                                    // 06d97096
    Stream.PutByte(Wire->bEquipped);                                  // 06d970c1
    Stream.PutByte(Wire->bMission);                                   // 06d970ec
    Stream.PutDword(Wire->Field_0x0c);                                // 06d97119
    Stream.PutDword(Wire->HardpointSize);                             // 06d97148
    if (Wire->HardpointSize != 0) {                                   // 06d9714f / 06d97154
        Stream.PutBytes(Wire->Hardpoint, Wire->HardpointSize);        // 06d97178..06d97192
    }

    // 06d971ad FUN_06da13c0 -- emits nothing.
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();                                 // 06d971c8
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());         // 06d971ce..06d971fe
    return 0;
}

unsigned int RpcDecodeServerSetAddItem(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength) {
    FPayloadServerSetAddItem* Wire =
        reinterpret_cast<FPayloadServerSetAddItem*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06d9723a FUN_06da13c0 -- consumes nothing.

    unsigned int Scratch = 0;
    unsigned char Small8 = 0;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->ArchId = Scratch;                                           // 06d9727e
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Id = Scratch;                                               // 06d972a6
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Count = (int)Scratch;                                       // 06d972ce
    if (!Stream.GetFloat(Wire->Health)) { return Stream.GetError(); } // 06d972f6
    if (!Stream.GetByte(Small8)) { return Stream.GetError(); }
    Wire->bEquipped = Small8;                                         // 06d9731a
    if (!Stream.GetByte(Small8)) { return Stream.GetError(); }
    Wire->bMission = Small8;                                          // 06d9733e
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x0c = Scratch;                                       // 06d97366
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->HardpointSize = Scratch;                                    // 06d97392
    if (Wire->HardpointSize != 0) {                                   // 06d97390 / 06d97395
        // Allocated before the bounds check, exactly as retail does; the pointer is
        // stored whether or not the copy then fits.
        char* Block = static_cast<char*>(
            ::operator new(Wire->HardpointSize, std::nothrow));       // 06d97398, bare
        Wire->Hardpoint = Block;                                      // 06d973a0
        if (!Stream.GetBytes(Wire->Hardpoint, Wire->HardpointSize)) { // 06d973c7..06d973e1
            return Stream.GetError();
        }
    }

    // 06d973f8 FUN_06da13c0 -- consumes nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                                     // LAB_06d9745a
    }
    *OutLength = Stream.Length();                                 // 06d97416
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());         // 06d9741b onwards
    return 0;
}

// ===========================================================================
// ch2 0x35 FLPACKET_SERVER_SETMISSIONOBJECTIVES -- Encode LAB_06d8fc90,
//                                                  Decode FUN_06d8ffd0
//
// Constructor FUN_06d9da40: 06d9dab3 writes the vtable 06db3c90, 06d9dab9 writes
// 0x20035. Registry 0x48, payload 0x28. The constructor builds the objective list's
// dummy node itself -- 06d9da92 `PUSH 0xc` / 06d9da9c operator new / 06d9daa5
// `MOV [EAX],EAX` / 06d9daa7 `MOV [EAX+4],EAX` / 06d9daaa `MOV [ESI+0x44],EDI` (zero) /
// 06d9daad `MOV [ESI+0x40],EAX` -- which independently fixes the list header at
// payload+0x1c as {allocator byte, Head, Size} and the node at 0xc bytes.
//
// The byte at packet+0x3c that 06d9da99 writes from `MOV AL,[ESP + 0x0b]` is the one-byte
// MSVC6 std::allocator copied out of an uninitialised temporary. It is the list header's
// allocator slot, not a field; reading it as data is the mistake that nearly invented
// four members in the sibling lane.
//
// ENCODE. Field order, again not struct order:
//     PutDword(payload+0x00)                   06d8fd09
//     PutDword(payload+0x18)  the count        06d8fd35  <- the count goes SECOND, ahead
//     PutDword(payload+0x04)                   06d8fd61     of everything it counts
//     {dword size, bytes} payload+0x08/+0x0c   06d8fd8d / 06d8fdb8
//     {dword size, bytes} payload+0x10/+0x14   06d8fe12 / 06d8fe4c
//     per node: PutByte(record+0x00)           06d8febe
//               {dword size, bytes} record+0x04/+0x08   06d8feec / 06d8ff14
//
// THE LIST WALK IS GUARDED TWICE, and both guards are reproduced:
//     06d8fe74  MOV ECX,[EAX + 0x38] ; TEST ECX,ECX ; JBE LAB_06d8ff51   (count == 0)
//     06d8fe7f  MOV EDX,[EAX + 0x40] ; MOV ECX,[EDX] ; CMP ECX,EDX ; JZ  (list empty)
// The loop then re-reads the head out of packet+0x40 on every iteration (06d8ff39) --
// retail's own re-load, kept.
//
// A stack-adjustment trap sits in the middle of this encoder, and it is why the `this`
// pointer has to be re-read rather than kept: 06d8fdbe `MOV ESI,[ESI + 0x2c]` CLOBBERS
// the packet pointer to use ESI as the REP MOVS source, so 06d8fdf5
// `MOV EAX,[ESP + 0x10]` reloads it from the spill 06d8fcc4 made at [ESP + 0x1c] -- the
// same slot, because the three PUSHes in between and FUN_06da1380's `RET 0xc` cancel
// out. Following [ESP+0x1c] literally past the call would have read the wrong local.
//
// DECODE (FUN_06d8ffd0). FUN_06da1620 is the {dword size, operator new, that many bytes}
// primitive already shared as RpcGetSizedBlock; it is called three times here -- 06d90099
// on payload+0x08, 06d900a4 on payload+0x10 and 06d9010f on record+0x04. The
// per-objective record is operator new(0xc) at 06d900c0 with +0x04 and +0x08 zeroed
// (06d900ce / 06d900d1) and byte 0 taken straight from the wire (06d90103). The push_back
// at 06d90118..06d90146 is the ordinary std::list insert before the sentinel, and
// 06d90148 bumps the list's own Size -- the payload's Count at packet+0x38 is NOT touched
// by the decoder, it was read off the wire at 06d90068.
// ===========================================================================

unsigned int RpcEncodeServerSetMissionObjectives(FPacketBase& Packet,
                                                 unsigned char** BufferHolder,
                                                 unsigned int Capacity,
                                                 unsigned int* OutLength) {
    const FPayloadServerSetMissionObjectives* Wire =
        reinterpret_cast<const FPayloadServerSetMissionObjectives*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06d8fcc8 FUN_06da1380
    Stream.PutDword(Wire->Field_0x00);                                // 06d8fd0c
    Stream.PutDword(Wire->Count);                                     // 06d8fd38
    Stream.PutDword(Wire->Field_0x04);                                // 06d8fd64
    RpcPutSizedBlock(Stream, Wire->TextASize, Wire->TextABuffer);     // 06d8fd90 / 06d8fdcc
    RpcPutSizedBlock(Stream, Wire->TextBSize, Wire->TextBBuffer);     // 06d8fe15 / 06d8fe62

    // 06d8fe74 `TEST ECX,ECX ; JBE` on the count; the sentinel at payload+0x20 is then
    // dereferenced untested (06d8fe82) -- the count and the list size move together
    // (FUN_06d8fc10), so a non-zero count means the list was built.
    if (Wire->Count != 0) {
        const FRpcMissionObjectiveNode* Sentinel = Wire->Objectives.Head;
        for (const FRpcMissionObjectiveNode* Node = Sentinel->Next;
             Node != Sentinel && !Stream.Failed(); Node = Node->Next) {
            // 06d8fe98 `MOV EDX,[ECX+0x8]`: the value is used untested.
            const FRpcMissionObjectiveRecord* Record = Node->Value;
            Stream.PutByte(Record->State);                            // 06d8febe
            RpcPutSizedBlock(Stream, Record->TextSize, Record->TextBuffer);
                                                                      // 06d8feec / 06d8ff2a
        }
    }

    // 06d8ff59 FUN_06da13c0 -- emits nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                                     // LAB_06d8ff69 / ffb7
    }
    *OutLength = Stream.Length();                                 // 06d8ff7e
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());         // 06d8ff84..06d8ffb2
    return 0;
}

unsigned int RpcDecodeServerSetMissionObjectives(FPacketBase& Packet,
                                                 const unsigned char* Buffer,
                                                 unsigned int Size,
                                                 unsigned int* OutLength) {
    FPayloadServerSetMissionObjectives* Wire =
        reinterpret_cast<FPayloadServerSetMissionObjectives*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06d8fffb FUN_06da13c0 -- consumes nothing.

    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x00 = Scratch;                                       // 06d90040
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Count = Scratch;                                            // 06d90068
    if (!Stream.GetDword(Scratch)) { return Stream.GetError(); }
    Wire->Field_0x04 = Scratch;                                       // 06d9008e
    if (!RpcGetSizedBlock(Stream, Wire->TextASize, Wire->TextABuffer)) {
        return Stream.GetError();                                     // 06d90099 FUN_06da1620
    }
    if (!RpcGetSizedBlock(Stream, Wire->TextBSize, Wire->TextBBuffer)) {
        return Stream.GetError();                                     // 06d900a4 FUN_06da1620
    }

    for (unsigned int Index = 0; Index < Wire->Count; ++Index) {      // 06d900a9 / 06d9015f
        FRpcMissionObjectiveRecord* Record =
            static_cast<FRpcMissionObjectiveRecord*>(
                ::operator new(sizeof(FRpcMissionObjectiveRecord), std::nothrow));
                                                                      // 06d900c0 PUSH 0xc
        // The new-expression's test (06d900ca) skips only the zeroing of +0x04/+0x08;
        // the reads below store through the result either way. Bytes 0..3 are left as
        // the heap had them in retail; zeroed here per the uninitialised-memory rule.
        if (Record != nullptr) {
            Record->State = 0;
            Record->Pad_0x01[0] = 0;
            Record->Pad_0x01[1] = 0;
            Record->Pad_0x01[2] = 0;
            Record->TextSize = 0;                                     // 06d900ce
            Record->TextBuffer = nullptr;                             // 06d900d1
        }

        // Both error exits (LAB_06d9018a after the byte, LAB_06d9017d after
        // FUN_06da1620) abandon the record -- and any text block FUN_06da1620 already
        // allocated -- without freeing either: the record is not linked yet, so retail
        // leaks it, and so does the port.
        unsigned char Small8 = 0;
        if (!Stream.GetByte(Small8)) {                                // 06d900e0 / 06d90101
            return Stream.GetError();
        }
        Record->State = Small8;                                       // 06d90103
        if (!RpcGetSizedBlock(Stream, Record->TextSize, Record->TextBuffer)) {
            return Stream.GetError();                                 // 06d9010f
        }
        // 06d90118..06d90148: insert before the sentinel (bare operator new(0xc) at
        // 06d90120), then bump the list's Size.
        RpcMissionObjectiveListPushBack(&Wire->Objectives, Record);
    }

    // 06d90171 FUN_06da13c0 -- consumes nothing.
    if (Stream.Failed()) {
        return Stream.GetError();                                     // LAB_06d901dd
    }
    *OutLength = Stream.Length();                                 // 06d9019b
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());         // 06d901a1..06d901d8
    return 0;
}

// ===========================================================================
// ch2 0x41 FLPACKET_SERVER_BURNFUSE -- LAB_06dac3c0 / LAB_06dac5c0
//
// Constructor FUN_06d9e5a0:
//   06d9e5e0  MOV dword ptr [EAX],.rdata:PTR_FUN_06db528c
//   06d9e5e6  MOV dword ptr [EAX + 0xc],0x20041
//   vtable 06db528c = { dtor FUN_06d9af50, Encode LAB_06dac3c0, Decode LAB_06dac5c0,
//                       IServerImpl::GetServerStats }
// and the vtable's XREF header names exactly one writer, 06d9e5e0, the constructor
// itself. The dtor slot is FUN_06d9af50, the base's -- BURNFUSE owns nothing.
//
// Wire layout -- one field per payload slot, in payload order, nothing shared:
//     dword  payload+0x00  Field_0x00   06dac431 / 06dac625
//     dword  payload+0x04  Field_0x04   06dac460 / 06dac64d
//     dword  payload+0x08  Field_0x08   06dac48f / 06dac675
//     word   payload+0x0c  Field_0x0c   06dac4be / 06dac69d
//     float  payload+0x10  Field_0x10   06dac4ef / 06dac6c7
//     float  payload+0x14  Field_0x14   06dac51e / 06dac6ef
//     byte   payload+0x18  Field_0x18   06dac54c / 06dac711
// 0x04 + 0x04 + 0x04 + 0x02 (+2 pad) + 0x04 + 0x04 + 0x01 (+3 pad) == 0x1c == 0x3c - 0x20,
// which is the registry size, so the payload has no room for anything else.
//
// TWO OF THE SEVEN ARE FLOATS, AND THE CONSTRUCTOR SAYS SO FIRST. 06d9e5d6 writes
// `MOV dword ptr [EAX + 0x34],0xbf800000` -- -1.0f -- into payload+0x14 while leaving
// payload+0x10 at zero, and the encoder moves both through the x87 stack: 06dac4f1
// `FLD float ptr [EDI + 0x30]` / 06dac4f4 `FSTP float ptr [EAX]` and 06dac520
// `FLD float ptr [EDI + 0x34]` / 06dac526 `FSTP float ptr [EAX + -0x4]`. Neither has an
// FMUL anywhere near it, so neither is quantised. The DECODER takes both back as plain
// dwords (06dac6c9 and 06dac6f1 are both `MOV EDX,dword ptr [EAX]`), so the four bytes
// cross unchanged and PutDword is the faithful move in both directions -- the same
// reading rpc_containers.cpp already applies to CHARACTERINFO's FLD/FSTP fields.
// FPayloadServerBurnFuse keeps Field_0x10/Field_0x14 as `unsigned int` for now, as its
// sender and receiver do; retyping them is one pass with the DamageEntry TODO.
//
// WHAT THE CONSTRUCTOR DOES *NOT* SAY: 06d9e5c6..06d9e5cf seed payload+0x0c with
// COMMON.DLL::SUBOBJ_ID_NONE as a WORD, so Field_0x0c is a SubObjId in disguise -- but
// nothing here names it, because retail does not: the sender at 0x06d8c170 copies it in
// from its caller without a setter. It stays Field_0x0c.
//
// Single-player: SP queues by pointer and calls neither slot.
// ===========================================================================

unsigned int RpcEncodeServerBurnFuse(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerBurnFuse* Wire =
        reinterpret_cast<const FPayloadServerBurnFuse*>(Packet.Payload());

    // 06dac3d1/06dac3d6: Limit = arg2, Ptr = *arg1.
    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06dac3f1 FUN_06da1380
    if (!Stream.PutDword(Wire->Field_0x00)) {                      // 06dac409..06dac43b
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutDword(Wire->Field_0x04)) {                      // 06dac43d..06dac46a
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutDword(Wire->Field_0x08)) {                      // 06dac46c..06dac499
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutWord(Wire->Field_0x0c)) {                       // 06dac49b..06dac4ca
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutDword(Wire->Field_0x10)) {                      // 06dac4ef FLD/FSTP
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutDword(Wire->Field_0x14)) {                      // 06dac51e FLD/FSTP
        return FRpcMarshalStream::kEncodeOverflow;
    }
    if (!Stream.PutByte(Wire->Field_0x18)) {                       // 06dac54c..06dac55c
        return FRpcMarshalStream::kEncodeOverflow;
    }
    // 06dac55e FUN_06da13c0 -- nothing more to emit.
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerBurnFuse(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength) {
    FPayloadServerBurnFuse* Wire =
        reinterpret_cast<FPayloadServerBurnFuse*>(Packet.Payload());

    // 06dac5c0: Ptr = arg1 directly, Limit = arg2.
    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06dac5ea FUN_06da13c0 -- no header consumed.

    if (!Stream.GetDword(Wire->Field_0x00)) {                      // 06dac602..06dac62e
        return Stream.GetError();
    }
    if (!Stream.GetDword(Wire->Field_0x04)) {                      // 06dac631..06dac656
        return Stream.GetError();
    }
    if (!Stream.GetDword(Wire->Field_0x08)) {                      // 06dac659..06dac67e
        return Stream.GetError();
    }
    if (!Stream.GetWord(Wire->Field_0x0c)) {                       // 06dac681..06dac6a7
        return Stream.GetError();
    }
    if (!Stream.GetDword(Wire->Field_0x10)) {                      // 06dac6ab..06dac6d0
        return Stream.GetError();
    }
    if (!Stream.GetDword(Wire->Field_0x14)) {                      // 06dac6d3..06dac6f8
        return Stream.GetError();
    }
    if (!Stream.GetByte(Wire->Field_0x18)) {                       // 06dac6fb..06dac717
        return Stream.GetError();
    }
    // 06dac720 FUN_06da13c0 -- nothing to consume.
    *OutLength = Stream.Length();                              // 06dac738..06dac73a
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());      // 06dac740..06dac77b
    return 0;
}
