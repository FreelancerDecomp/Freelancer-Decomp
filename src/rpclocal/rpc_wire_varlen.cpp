// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_wire_varlen.cpp -- the VARIABLE-LENGTH packet codec slots.
//
// Three retail Encode/Decode vtable pairs, covering five of the 162 registered packet
// types. All three differ from everything in rpc_wire_fields.cpp in the same way: the
// number of bytes on the wire is not a property of the type, it is read out of the
// packet (a strlen, a list size, a block size) and written as a leading count.
//
// WHY FIVE TYPES AND NOT THREE. The vtable slots were read out of .rdata, not guessed,
// and two pairs of types share a body outright:
//
//   .rdata:06db5c88  addr .text:LAB_06daf830   <- ch3 0x3a DESTROYCHAR   (ctor 06da0bcd)
//   .rdata:06db5c8c  addr .text:LAB_06daf970
//   .rdata:06db5c98  addr .text:LAB_06daf830   <- ch3 0x06 SELECTCHARACTER (ctor 06da0bfd)
//   .rdata:06db5c9c  addr .text:LAB_06daf970
//
//   .rdata:06db52b0  addr .text:LAB_06dacca0   <- ch2 0x48 SETCOLLISIONGROUPS (ctor 06d9e915)
//   .rdata:06db52b4  addr .text:LAB_06dacd70
//   .rdata:06db5df8  addr .text:LAB_06dacca0   <- ch3 0x36 REQCOLLISIONGROUPS (ctor 06da1115)
//   .rdata:06db5dfc  addr .text:LAB_06dacd70
//
// This CONTRADICTS the note at the head of the ch3 0x2d block in rpc_wire_fields.cpp,
// which claimed "every one of the 26 packet types still without a codec has a distinct
// encoder, checked by comparing full function extents". Two of them do not: these four
// types are served by two bodies. That note is corrected in place.
//
// HOW THE FOUR CONSTRUCTORS WERE FOUND. Earlier waves scanned for
// `MOV dword ptr [EAX + 0xc],<id>` and came up 152 short of 162, leaving ch2 0x2b,
// ch2 0x48 and ch3 0x36 looking as if they found their routing id "some other way".
// They do not. The constructors of those three have an SEH frame (they own a std::list
// or a std::string member), so MSVC keeps `this` in ESI instead of EAX and the very same
// store reads `MOV dword ptr [ESI + 0xc],<id>`:
//     .text:06d9c5ab  MOV dword ptr [ESI + 0xc],0x2002b
//     .text:06d9e915  MOV dword ptr [ESI + 0xc],0x20048
//     .text:06da1115  MOV dword ptr [ESI + 0xc],0x30036
// (operand text only: the listing prints these immediates truncated in its byte
// column, so spelling the encoding out would be reconstruction, not reading.)
// Re-run across every base register the scan yields exactly 162 stores and exactly 162
// distinct ids -- one per registry row, none missing and none doubled. The routing
// header is written for these three like it is for the other 159; nothing is inert.
//
// THE ROUTING WORD ITSELF. FUN_06da1380 builds it from a dword the constructor writes at
// packet+0x0c: 06da1383 `MOV DX,word ptr [this + 0xc]` is the opcode and 06da1389
// `MOV BH,byte ptr [this + 0xe]` is the channel. So `0x30005` is not a scalar id, it is
// the byte image `05 00 03 00` -- opcode word at +0x0c, channel byte at +0x0e, a spare
// byte at +0x0f that nothing reads.
//
// Single-player: in SP a packet is queued by pointer and neither slot is ever called.

#include "rpc_containers.h"
#include "rpc_payloads.h"

#include <new>

// ===========================================================================
// ch3 0x06 FLPACKET_CLIENT_SELECTCHARACTER and ch3 0x3a FLPACKET_CLIENT_DESTROYCHAR --
// LAB_06daf830 (133 instructions) / LAB_06daf970 (118).
//
// Both types are operator new(0x220) -- 06da00c5 `PUSH 0x220` in factory 06da00c0 and
// 06da01c5 `PUSH 0x220` in factory 06da01c0, both read off the listing rather than
// carried over from the registry table -- so the payload is 0x220 - 0x20 == 0x200 and
// IS the CHARACTER_ID, exactly as rpc_payload_ch3.cpp already treats it. The type's own
// dump slot confirms the field is the character name and nothing else:
//     .text:06dafac0  ADD ECX,0x20
//     .text:06dafac4  PUSH .rdata:s_Character:_06db66ef+1   ;= "   Character: "
//     .text:06dafaca  CALL MSVCP60.DLL::std::operator<<
//
// THE WIRE FORMAT is { word Length; char Text[Length] } -- NO terminator on the wire and
// NO fixed run. 06daf883 `SCASB.REPNE ES:EDI` over payload+0x00 with ECX preloaded to
// -1 at 06daf87b, then 06daf88a `NOT ECX` / 06daf88f `DEC ECX`, is a plain strlen; that
// count is what 06daf8b2 `MOV word ptr [EAX],CX` puts on the wire and what 06daf8f5
// `MOVSD.REP ES:EDI,ESI` copies.
//
// DO NOT CONFUSE THIS WITH ch3 0x2d DBGCONSOLE, which looks similar and is not: there
// 06db16f4 `ADD EDX,0x80` reserves the whole 128-byte run in one go and the bytes past
// the terminator go out on the wire as they sit in the packet. There is no SCASB and no
// length prefix anywhere in that pair. Here there is no fixed run at all and the bytes
// past the terminator never leave.
//
// The length is TRUNCATED TO 16 BITS before it is used again, not merely when it is
// stored: 06daf8bd `XOR EAX,EAX` / 06daf8bf `MOV AX,CX` rebuilds it from the word, and
// that truncated value is what feeds both the cursor bump at 06daf8c4 `ADD ECX,EAX` and
// the copy count at 06daf8e8 `MOV ECX,EAX`. Reproduced, although a CHARACTER_ID is only
// 0x200 bytes so nothing can reach the truncation in practice.
// ===========================================================================
unsigned int RpcEncodeWireCharacterId(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength) {
    const char* Name = reinterpret_cast<const char*>(Packet.Payload());

    // 06daf840 / 06daf846: Limit = arg2, Ptr = *arg1. 06daf849 / 06daf84f: Cursor = 0,
    // Error = 0.
    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06daf861

    // 06daf879..06daf88f -- the REPNE SCASB strlen over payload+0x00, unbounded, as
    // retail's. The payload is a CHARACTER_ID the marshaller copied whole, terminator
    // included.
    unsigned int Length = 0;
    while (Name[Length] != '\0') {
        ++Length;
    }
    const unsigned short WireLength = (unsigned short)(Length & 0xffffu);

    // 06daf88c `ADD EAX,0x2` reserves, 06daf894 `JBE` checks, 06daf8b2
    // `MOV word ptr [EAX],CX` stores.
    Stream.PutWord(WireLength);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06daf8a2
    }
    // 06daf8c4 `ADD ECX,EAX` reserves the run, 06daf8f5 / 06daf8fc copy it.
    Stream.PutBytes(Name, (unsigned int)WireLength);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06daf8d6
    }
    *OutLength = Stream.Length();                                  // 06daf92f
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());          // 06daf931..06daf968
    return 0;                                                          // 06daf968
}

// LAB_06daf970 -- Decode. kDecodeOverrun (0x17) at 06daf9c7 and 06daf9f9 in place of the
// encoder's 0x16, and both of its header/trailer hooks are FUN_06da13c0, the
// `XOR EAX,EAX ; RET 0xc` stub at 06da13c0..06da13c2, so neither can fail.
//
// After the copy, 06dafa2f `MOV byte ptr [EDX + EBX*0x1 + 0x20],0x0` terminates the
// payload at payload+Length. EDX there is the word read off the wire, zero-extended at
// 06daf9de/06daf9e0, and NOTHING bounds it against the 0x200-byte payload: the only
// check the length passes is the one against the INPUT buffer, at 06daf9f2 `CMP EAX,ECX`.
// A datagram longer than 0x220 bytes therefore writes past the packet in retail, and in
// the port.
unsigned int RpcDecodeWireCharacterId(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength) {
    char* Name = reinterpret_cast<char*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06daf9a2
    }
    unsigned short WireLength = 0;
    if (!Stream.GetWord(WireLength)) {                                 // 06daf9bb / 06daf9db
        return Stream.GetError();                                      // 06daf9d1
    }
    const unsigned int Length = (unsigned int)WireLength;
    if (!Stream.GetBytes(Name, Length)) {                              // 06daf9ee / 06dafa1b
        return Stream.GetError();                                      // 06dafa03
    }
    Name[Length] = '\0';                                               // 06dafa2f
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();                                  // 06dafa5c
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06dafa5e..06dafa9b
    return 0;                                                          // 06dafa9e
}

// ===========================================================================
// ch2 0x48 FLPACKET_SERVER_SETCOLLISIONGROUPS and ch3 0x36 FLPACKET_CLIENT_
// REQCOLLISIONGROUPS -- LAB_06dacca0 (83 instructions) / LAB_06dacd70 (82).
//
// Both are operator new(0x2c) -- 06d9e875 `PUSH 0x2c` and 06da03c5 `PUSH 0x2c` -- so the
// payload is 0x0c, which is exactly one retail std::list header. FRpcCollisionGroupList
// and its node are already declared in rpc_containers.h and the list is built by the
// packet constructor (rpc_packet_registry.cpp); this file only adds the two wire slots.
//
// NEITHER SLOT DOES THE WORK ITSELF. The encoder's whole payload is one call at
// 06daccf6 to FUN_06d8f530 and the decoder's is one call at 06dacdbf to FUN_06d8f5e0.
// Both helpers are declared `RET 0xc`, and both are handed the slot's own three
// arguments -- 06daccf1..06daccf3 push OutLength, Capacity and BufferHolder -- but
// NEITHER HELPER READS ONE OF THEM. Every access in FUN_06d8f530 goes through ECX:
// param_1+0x10 the cursor, +0x14 the limit, +0x18 the pointer, +0x1c the error,
// +0x24 the list head and +0x28 its size. Checklist class 2: the arity is real and the
// arguments are dead.
//
// THE WIRE FORMAT is { word Count; { word Id; float Health; } Count times }.
//   * 06d8f535 `MOV SI,word ptr [param_1 + 0x28]` takes the count from the LIST HEADER's
//     size field, then 06d8f55b `MOV word ptr [EDX],SI` puts it on the wire;
//   * the loop that follows is driven by the LIST ITSELF -- 06d8f56c `MOV EDX,[EDI]` and
//     06d8f5c4 `MOV EDX,[EDX]` with 06d8f5c6 `CMP EDX,EDI` as the sentinel test -- not
//     by that count. The two agree in every retail sender, and the port keeps both
//     halves as retail wrote them rather than deriving the count from the walk.
//   * per node: 06d8f58f `MOV BP,word ptr [EDX + 0x8]` is the LOW WORD of the value's
//     +0x00 only, and 06d8f5b6 `FLD float ptr [EDX + 0xc]` / 06d8f5bc `FSTP float ptr
//     [ESI]` is the health as a raw float. No quantizer on this pair, unlike the
//     CREATESHIP/CREATESOLAR header codec which sends the same node type through a
//     255.0 scale.
// ===========================================================================
unsigned int RpcEncodeWireCollisionGroups(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength) {
    const FRpcCollisionGroupList* List = RpcPacketCollisionGroups(&Packet);

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06daccd1

    // FUN_06d8f530 begins here.
    const unsigned short Count = (unsigned short)(List->Size & 0xffffu);
    Stream.PutWord(Count);                                             // 06d8f539 / 06d8f55b
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06d8f554
    }
    // 06d8f561 `TEST SI,SI` / 06d8f567 `JBE` -- TEST clears CF, so JBE is JZ here: a
    // zero count skips the walk entirely. 06d8f56e `CMP EDX,EDI` / 06d8f570 `JZ` skips
    // it again when the list is empty.
    // The dummy is dereferenced untested (06d8f56c); a non-zero count means it was built.
    if (Count != 0) {
        const FRpcCollisionGroupNode* Node = List->Head->Next;
        while (Node != List->Head) {
            Stream.PutWord((unsigned short)(Node->Field_0x00 & 0xffffu));  // 06d8f58f
            if (Stream.Failed()) {
                return Stream.GetError();                              // 06d8f588
            }
            // The value is moved as a float through the x87 stack, but FLD/FSTP of a
            // single-precision slot is a bit-exact 4-byte move; Field_0x04 already holds
            // the bit pattern, so no reinterpretation is needed either way.
            Stream.PutDword(Node->Field_0x04);                         // 06d8f5b6 / 06d8f5bc
            if (Stream.Failed()) {
                return Stream.GetError();                              // 06d8f5b2
            }
            Node = Node->Next;
        }
    }

    // 06dacd07 FUN_06da13c0 -- the trailer hook, the `XOR EAX,EAX ; RET 0xc` no-op.
    *OutLength = Stream.Length();                                  // 06dacd25
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());          // 06dacd27..06dacd57
    return 0;                                                          // 06dacd5a
}

// LAB_06dacd70 / FUN_06d8f5e0 -- Decode.
//
// 06d8f5fa calls COMMON.DLL's std::list::erase(begin, end) on the payload's own list
// before reading anything, so a decode always starts from an empty list;
// RpcCollisionGroupListAssign(List, nullptr, 0) is that erase plus the Size bookkeeping
// retail's callee does, and it is already verified against FUN_06d8f710.
//
// The append is retail's own splice, not a helper: 06d8f660 `MOV [EDI+4],EAX`,
// 06d8f663 `MOV EDX,[EAX+4]`, 06d8f66a `MOV [EDX],EAX`, 06d8f67c `INC EDX` /
// 06d8f67d `MOV [EBX+8],EDX`.
//
// A RETAIL DEFECT REPRODUCED ONLY IN PART. 06d8f671 calls FUN_06d85500 -- the
// CollisionGroupDesc copy-constructor, 06d85500..06d85516, eight bytes with a null-dest
// guard -- to construct the new node's value, and its SOURCE is the stack slot at
// ESP+0x14. That slot is ESP-relative E-0x8 inside the `SUB ESP,0xc` frame opened at
// 06d8f5e0, and nothing ever writes it: the only slot of that frame that is written is
// E-0xc, at 06d8f635, and that one holds the loop bound (06d8f6e1 `CMP BP,word ptr
// [ESP + 0x10]` reads it back). So retail constructs every node from eight uninitialised
// stack bytes. The two fields that follow overwrite +0x04 completely but only the LOW
// WORD of +0x00 (06d8f6af `MOV word ptr [EAX],DX`), which leaves the high half of every
// decoded CollisionGroupDesc::Id holding stack residue. The port zeroes instead; see
// docs/re/deviations.md.
unsigned int RpcDecodeWireCollisionGroups(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength) {
    FRpcCollisionGroupList* List = RpcPacketCollisionGroups(&Packet);

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06dacd9f
    }
    // FUN_06d8f5e0 begins here. 06d8f5fa -- erase(begin, end).
    RpcCollisionGroupListAssign(List, nullptr, 0u);
    unsigned short Count = 0;
    if (!Stream.GetWord(Count)) {                                      // 06d8f606 / 06d8f629
        return Stream.GetError();                                      // 06d8f61c
    }
    // 06d8f62f `TEST CX,CX` / 06d8f639 `JBE`, then the same test again at 06d8f641 --
    // MSVC emitted the zero check twice. Either one alone has the same effect.
    for (unsigned int Index = 0; Index < (unsigned int)Count; ++Index) {
        // 06d8f65a COMMON.DLL list _Buynode: the node is used untested (06d8f660).
        FRpcCollisionGroupNode* New =
            static_cast<FRpcCollisionGroupNode*>(::operator new(sizeof(FRpcCollisionGroupNode),
                                                               std::nothrow));
        FRpcCollisionGroupNode* Dummy = List->Head;
        FRpcCollisionGroupNode* Tail = Dummy->Prev;
        New->Next = Dummy;
        New->Prev = Tail;
        Dummy->Prev = New;
        Tail->Next = New;
        // Retail's FUN_06d85500 copies eight uninitialised bytes here.
        New->Field_0x00 = 0;
        New->Field_0x04 = 0;
        ++List->Size;                                                  // 06d8f67d

        unsigned short Id = 0;
        if (!Stream.GetWord(Id)) {                                     // 06d8f68c / 06d8f6ac
            return Stream.GetError();                                  // 06d8f6a5
        }
        New->Field_0x00 = (unsigned int)Id;                            // 06d8f6af, low word only
        unsigned int Health = 0;
        if (!Stream.GetDword(Health)) {                                // 06d8f6be / 06d8f6db
            return Stream.GetError();                                  // 06d8f6d4
        }
        New->Field_0x04 = Health;                                      // 06d8f6e6
    }

    // 06dacdcd FUN_06da13c0 -- the trailer hook, the same no-op.
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();                                  // 06dacdeb
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06dacded..06dace28
    return 0;                                                          // 06dace2b
}

// ===========================================================================
// ch2 0x45 FLPACKET_SERVER_SET_MISSION_MESSAGE -- LAB_06dabbb0 (134 instructions) /
// LAB_06dabd00 (93). Its vtable is 06db516c, from 06d9d9b0
// `MOV dword ptr [EAX + 0xc],0x20045`, and .rdata:06db5170 / 06db5174 are the two slots.
// Registry size 0x2c -> payload 0x0c, the {TextSize, TextBuffer, Field_0x08} triple that
// rpc_payloads.h already declares.
//
// THE WIRE ORDER IS NOT THE STRUCT ORDER, which is the whole reason this pair had to be
// transcribed rather than assumed: the LAST field of the payload goes out FIRST.
//   06dabbfe `ADD EDX,0x4` / 06dabc1e `MOV EDX,dword ptr [EBP + 0x28]` / 06dabc21
//            `MOV dword ptr [ECX],EDX`   -- packet+0x28 == payload+0x08, first on the wire
//   06dabc2d `ADD EDI,0x4` / 06dabc48 `MOV EDX,dword ptr [EBP + 0x20]` / 06dabc4b
//            `MOV dword ptr [ECX],EDX`   -- payload+0x00, the size
//   06dabc5a `ADD EDI,ECX` / 06dabc85 `MOVSD.REP ES:EDI,ESI` from 06dabc77
//            `MOV ESI,dword ptr [EBP + 0x24]` -- payload+0x04, that many bytes
// A codec written from the declaration order would have put the size first and been
// byte-wrong for every packet of this type.
//
// The size/bytes pair is retail's one variable-length primitive; the decoder reaches it
// through the real function FUN_06da1620 (06dabd72 `CALL FUN_06da1620` with 06dabd64
// `LEA EDX,[EDI + 0x20]`, i.e. &payload+0x00), which is already modelled by
// RpcPutSizedBlock / RpcGetSizedBlock in rpc_containers.cpp -- allocation before the
// second bounds check included.
// ===========================================================================
unsigned int RpcEncodeServerSetMissionMessage(FPacketBase& Packet,
                                              unsigned char** BufferHolder,
                                              unsigned int Capacity,
                                              unsigned int* OutLength) {
    const FPayloadServerSetMissionMessage* Wire =
        reinterpret_cast<const FPayloadServerSetMissionMessage*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06dabbe1
    Stream.PutDword(Wire->Field_0x08);                                 // 06dabbfe / 06dabc21
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06dabc14
    }
    RpcPutSizedBlock(Stream, Wire->TextSize, Wire->TextBuffer);        // 06dabc2d / 06dabc5a
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06dabc3e / 06dabc6a
    }
    // 06dabca1 FUN_06da13c0 -- the trailer hook, the no-op.
    *OutLength = Stream.Length();                                  // 06dabcbf
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());          // 06dabcc1..06dabcf1
    return 0;                                                          // 06dabcf4
}

unsigned int RpcDecodeServerSetMissionMessage(FPacketBase& Packet,
                                              const unsigned char* Buffer,
                                              unsigned int Size,
                                              unsigned int* OutLength) {
    FPayloadServerSetMissionMessage* Wire =
        reinterpret_cast<FPayloadServerSetMissionMessage*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06dabd2f
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {                                   // 06dabd47 / 06dabd62
        return Stream.GetError();                                      // 06dabd5c
    }
    Wire->Field_0x08 = Scratch;                                        // 06dabd67
    // The decoder overwrites the pointer WITHOUT freeing whatever the payload already
    // held; only the packet's destructor (FUN_06d9d9e0) ever frees packet+0x24. Retail
    // leaks on a second decode into the same packet, and so does this.
    if (!RpcGetSizedBlock(Stream, Wire->TextSize, Wire->TextBuffer)) {
        return Stream.GetError();                                      // 06dabd72 FUN_06da1620
    }
    // 06dabd7c FUN_06da13c0 -- the trailer hook.
    *OutLength = Stream.Length();                                  // 06dabd9a
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06dabd9c..06dabdd7
    return 0;                                                          // 06dabdda
}

// ===========================================================================
// ch2 0x3d FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE -- LAB_06d8f840 (128 instructions) /
// LAB_06d8f970 (115). Its vtable is 06db3c70, written at 06d9ecfd
// `MOV dword ptr [EAX],.rdata:PTR_FUN_06db3c70`, with the id at 06d9ed03
// `MOV dword ptr [EAX + 0xc],0x2003d` -- EAX, not ESI: this constructor is one of the
// plain ones. .rdata:06db3c74 / 06db3c78 are the two slots.
// Registry size 0x2c -> payload 0x0c.
//
// The type carries the SAME equipment/cargo list the ch2 0x24 / 0x25 and ch3 0x16 / 0x17
// family carries, and reaches it through the very same out-of-line bodies: 06d8f902
// `CALL FUN_06da2240` and 06d8fa17 `CALL FUN_06da2480`. Those bodies were already
// transcribed for that family; they are now RpcPutEquipList / RpcGetEquipList in
// rpc_wire_generic.cpp, called from both places exactly as retail calls them, rather
// than copied. Nothing about the record layout is re-derived here.
//
// THE WIRE ORDER IS AGAIN NOT THE STRUCT ORDER, and this one is a three-way shuffle:
//   06d8f88e `ADD EDX,0x4`  / 06d8f8b5 `MOV ECX,dword ptr [EDI + 0x28]` / 06d8f8b8
//            `MOV dword ptr [EAX],ECX`  -- payload+0x08 leads
//   06d8f8c4 `INC EDX`      / 06d8f8e9 `MOV DL,byte ptr [EDI + 0x24]`   / 06d8f8f4
//            `MOV byte ptr [EAX],DL`    -- payload+0x04, ONE byte, not a dword
//   06d8f902 the list at payload+0x00 comes last
// The byte is a byte on both sides -- 06d8fa02 `MOV DL,byte ptr [EAX]` / 06d8fa14
// `MOV byte ptr [EDI + 0x24],DL` -- so the three padding bytes of Field_0x04 never
// travel and are not read back.
// ===========================================================================
unsigned int RpcEncodeServerRemoteObjectCargoUpdate(FPacketBase& Packet,
                                                    unsigned char** BufferHolder,
                                                    unsigned int Capacity,
                                                    unsigned int* OutLength) {
    const FPayloadServerRemoteObjectCargoUpdate* Wire =
        reinterpret_cast<const FPayloadServerRemoteObjectCargoUpdate*>(Packet.Payload());
    // payload+0x00 is the operator new(4) handle the list hangs off; the struct declares
    // it as a dword because every other user of the field moves it as one.
    FRpcEquipWireHandle* const* Slot =
        reinterpret_cast<FRpcEquipWireHandle* const*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06d8f871
    Stream.PutDword(Wire->Field_0x08);                                 // 06d8f88e / 06d8f8b8
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06d8f8a3
    }
    Stream.PutByte(Wire->Field_0x04);                                  // 06d8f8c4 / 06d8f8f4
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06d8f8d7
    }
    if (!RpcPutEquipList(Stream, Slot)) {                              // 06d8f902 FUN_06da2240
        return FRpcMarshalStream::kEncodeOverflow;                     // 06d8f909
    }
    // 06d8f913 FUN_06da13c0 -- the trailer hook, the no-op.
    *OutLength = Stream.Length();                                  // 06d8f92d
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());          // 06d8f930..06d8f962
    return 0;                                                          // 06d8f965
}

unsigned int RpcDecodeServerRemoteObjectCargoUpdate(FPacketBase& Packet,
                                                    const unsigned char* Buffer,
                                                    unsigned int Size,
                                                    unsigned int* OutLength) {
    FPayloadServerRemoteObjectCargoUpdate* Wire =
        reinterpret_cast<FPayloadServerRemoteObjectCargoUpdate*>(Packet.Payload());
    FRpcEquipWireHandle** Slot =
        reinterpret_cast<FRpcEquipWireHandle**>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06d8f99f
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {                                   // 06d8f9b7 / 06d8f9d6
        return Stream.GetError();                                      // 06d8f9cc
    }
    Wire->Field_0x08 = Scratch;                                        // 06d8f9dd
    unsigned char Flag = 0;
    if (!Stream.GetByte(Flag)) {                                       // 06d8f9e5 / 06d8fa02
        return Stream.GetError();                                      // 06d8f9f8
    }
    Wire->Field_0x04 = Flag;                                           // 06d8fa14
    if (!RpcGetEquipList(Stream, Slot)) {                              // 06d8fa17 FUN_06da2480
        return Stream.GetError();                                      // 06d8fa1e
    }
    // 06d8fa25 FUN_06da13c0 -- the trailer hook.
    *OutLength = Stream.Length();                                  // 06d8fa43
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06d8fa45..06d8fa82
    return 0;                                                          // 06d8fa85
}
