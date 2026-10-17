// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_wire_generic.cpp -- the SHARED packet codec slots.
//
// Retail rpclocal.dll has 162 packet classes but nowhere near 162 codec pairs: most of
// the small types compile down to one of a dozen shape-only Encode/Decode bodies that
// the linker folds and every vtable of that shape points at. This file transcribes
// twenty-four such bodies; between them they carry EIGHTY-SIX of the 162 registered
// types, which is why they are worth a file of their own.
//
// HOW EACH BODY WAS TIED TO ITS TYPES -- the same constructor scan the ch1/ch2 wire
// files use (`MOV [reg],<vtable>` followed by `MOV [reg+0xc],0xCCCCOOOO`), re-run here
// and cross-checked from the other end by counting the .rdata slots that actually name
// each body. Example, the byte-payload pair:
//
//   ch2 0x02 FLPACKET_SERVER_LOGINRESPONSE, constructor FUN_06d9c860
//     06d9c877  MOV dword ptr [EAX],.rdata:PTR_FUN_06db4ffc
//     06d9c87d  MOV dword ptr [EAX + 0xc],0x20002
//   and .rdata:06db4ffc..06db5008 = { FUN_06d9af50, LAB_06da82e0, LAB_06da83c0,
//                                     IServerImpl::GetServerStats }
//
// A CAUTION THAT COST A DETOUR, AND THE ONE CLAIM RE-VERIFIED FROM SCRATCH THIS WAVE:
// THE LISTING'S OWN XREF HEADER UNDER-REPORTS. Ghidra prints
//     LAB_06db0310:  ;XREF[21]: 06db4fe0(*),06db4ff0(*), ...
// but grepping the `.rdata:... addr .text:LAB_06db0310` lines finds THIRTY, and
// LAB_06db03f0 has THIRTY-THREE against a printed 21. PTR_FUN_06db5cf4 (ch3 0x47
// RTCDONE) is one of the nine the header omits -- .rdata:06db5cf8 names LAB_06db0310
// and .rdata:06db5cfc names LAB_06db03f0, both plainly in the listing, neither in the
// XREF line. Every other body in this file was checked the same way and its two counts
// agree. The type lists below come from the .rdata scan, never from the XREF header.
//
// WHAT A SHARED BODY DOES AND DOES NOT PROVE. It proves the wire format and the
// payload OFFSETS its types have in common -- retail could not fold two layouts into
// one function. It proves nothing about the fields past what the body touches, so
// nothing here renames or re-shapes a per-type payload struct in rpc_payloads.h.
// Where such a struct already existed it was compared against the body and agreed;
// the sharpest agreement is the blob pair, where rpc_payloads.h had ALREADY split
// FPayloadServerBlob {Size, Buffer} from FPayloadServerBlobReversed {Buffer, Size} on
// sender/receiver evidence alone, and the two decoders here (LAB_06d978f0 stores the
// count at packet+0x20, LAB_06d97730 stores it at packet+0x24) land on exactly that
// split from the other direction.
//
// FOUR DIFFERENCES BETWEEN OTHERWISE IDENTICAL BODIES, ALL LOAD-BEARING:
//   * LAB_06da13d0 inlines the header write and loses the capacity check with it;
//     LAB_06dae0d0 calls FUN_06da1380 and can return 0x16. Same payload (none).
//     -- WITHDRAWN 2026-09-17. This "difference" is not there. FUN_06da1380
//     (06da1380..06da13b2) contains no CMP, no TEST and no conditional jump, never reads
//     the Limit it is handed, and ends `XOR EAX,EAX ; RET 0xc`. LAB_06dae0d0 therefore
//     cannot return 0x16 from its header either, and LAB_06da13d0's inliner dropped
//     nothing. The two bodies differ only in whether the header write is a call. See the
//     corrected derivation on FRpcMarshalStream::PutHeaderUnchecked.
//   * LAB_06da5bc0 allocates Size + 1 (06da5c3c `INC EAX`) and NUL-terminates
//     (06da5ca1 `MOV byte ptr [EDX + ECX*0x1],0x0`); LAB_06da62d0, byte-identical
//     otherwise, does neither. FLPACKET_COMMON_SET_WEAPON_GROUP's blob is a C string.
//   * LAB_06db10e0 skips the allocation and the copy when the count is zero
//     (06db114f `TEST EAX,EAX` / 06db1154 `JBE`); LAB_06d97730 does not.
//   * LAB_06da62d0 / LAB_06da5bc0 reuse a buffer the packet already holds
//     (06da5c33 `TEST EAX,EAX` / 06da5c38 `JNZ`); LAB_06d978f0 and LAB_06d97730
//     allocate unconditionally and leak the old one. Reproduced, not corrected.
//
// STACK TRAP, CONFIRMED ON THESE BODIES. The blob encoder LAB_06da6190 parks the
// destination pointer at 06da624a `MOV dword ptr [ESP + 0x18],EDI` and reads it back
// at 06da6257 -- and with EBX/EBP/ESI/EDI pushed, [ESP+0x18] is the slot of its OWN
// second argument. Reading the stack slots as consecutive locals would have permuted
// the fields; the value that matters is the one the spill holds, not the argument.
//
// Every body ends in the statistics tail (FUN_06da16b0 + the accumulator), including
// the decoder quirk that the maximum is compared against Max + 2 and stored as
// Length + 2, so a new maximum has to beat the old length by four. Verified again here
// at 06da14c3..06da14d3 and 06db04a8..06db04b4.
//
// Single-player: in SP a packet is queued by pointer and neither slot is ever called.
// Nothing in this file touches the SP path.

#include "rpc_containers.h"
#include "rpc_payloads.h"

#include <cstddef>
#include <cstring>
#include <new>

// ===========================================================================
// Shape 0 -- no payload at all. Registry size 0x20, payload 0x00.
//
// LAB_06da13d0 / LAB_06da1470, 8 .rdata slots each, 5 of them registered types:
//   ch3 0x05 REQUESTCHARINFO, 0x0a GFREQUESTSHIPINFO, 0x10 REQUESTCREATESHIP,
//   ch3 0x38 REQUESTNEWCHARINFO, 0x42 SKIP_AUTOSAVE.
// The other three slots (vtables 06db3fb4, 06db4fbc, 06db5e54) belong to classes whose
// constructors do not write a 0xCCCCOOOO routing header.
//
// RESOLVED 2026-09-17, and the conclusion drawn from that absence was HALF WRONG.
// All three really are BASE-CLASS vtables. The first pass then said that no constructor
// writes them at all and that every reference is a destructor performing MSVC's
// base-subobject vtable swap; re-checked instruction by instruction later the same day,
// that is true for 06db3fb4 and 06db5e54 and FALSE for 06db4fbc.
//   * 06db4fbc is the root packet base, and THIRTEEN of its fourteen references are
//     CONSTRUCTORS. The giveaway is a three-store sequence a destructor cannot produce:
//     base vtable, then the derived vtable, then the routing header. SETGROUPFEELINGS is
//     the worked example --
//       .text:06d9c575  c706bc4fdb06  MOV dword ptr [ESI],.rdata:PTR_FUN_06db4fbc
//       .text:06d9c58e  e8f7590100    CALL MSVCRT.DLL::operator_new
//       .text:06d9c5a5  c706303ddb06  MOV dword ptr [ESI],.rdata:PTR_FUN_06db3d30
//       .text:06d9c5ab  c7460c2b0...  MOV dword ptr [ESI + 0xc],0x2002b
//     -- and the same base/derived/0xCCCCOOOO triple stands at 06d9c905+06d9c93b+06d9c941
//     (0x20005), 06d9ceb7+06d9cf01+06d9cf07 (0x2002d), 06d9d175+06d9d1a5+06d9d1ab
//     (0x20028), 06d9d2c6+06d9d336+06d9d33c (0x20029), 06d9d525+06d9d54f+06d9d555
//     (0x20003), 06d9da75+06d9dab3+06d9dab9 (0x20035), 06d9e5b7+06d9e5e0+06d9e5e6
//     (0x20041), 06d9e665+06d9e6f6+06d9e6fc (0x20042), 06d9e8e5+06d9e90f+06d9e915
//     (0x20048), 06da07a7+06da07cc+06da07d2 (0x30003) and 06da10e5+06da110f+06da1115
//     (0x30036); 06d9cc95 is the thirteenth. Only the FOURTEENTH reference is a
//     destructor: FUN_06da1370 is `MOV dword ptr [param_1],.rdata:PTR_FUN_06db4fbc` at
//     06da1370 followed by `JMP FUN_06d8f4c0` at 06da1376, and FUN_06d8f4c0 is
//     `MOV dword ptr [param_1],.rdata:PTR_LAB_06db3c54` at 06d8f4c0 then `RET` -- the
//     one-slot grandparent table whose only entry, LAB_06d8f4d0, is a scalar deleting
//     destructor. So the chain is {1 slot: dtor} -> {4 slots: dtor, Encode, Decode,
//     Dump} -> the 162 leaves, and the base vtable is written from BOTH ends.
//     Method note, because it cost a wrong claim: grepping the listing text for
//     "06db4fbc" finds only THIRTEEN rows, because 06da1370's operand is printed
//     truncated as ".rdata:PTR_FUN_06...". Ghidra's XREF[14] header was right and the
//     grep was wrong -- the opposite of the usual undercount, and worth remembering.
//   * 06db3fb4 is an intermediate base of the create-object family: 06d97bf9 and
//     06d9e78a, both inside destructors (FUN_06d97b90, FUN_06d9e740), and its slot 0 is
//     LAB_06db1dd0, not a constructor.
//   * 06db5e54 is the base of the FIVE equipment-carrying packet classes. Its only
//     reference, 06da21ee, sits inside FUN_06da21d0, which frees the equipment handle at
//     packet+0x20 through FUN_06d91340 -- and FUN_06da21d0's callers are 06d8f816,
//     06d91316, 06d91676, 06d95bc6 and 06d95cf6, i.e. exactly the destructors of ch2
//     0x3d, ch2 0x24, ch2 0x25, ch3 0x16 and ch3 0x17. That is the same five types the
//     equipment codec at the end of this file serves, arrived at from the other end.
// So none of the three is a registry type, and the empty codec pair they carry is simply
// what a base class that never goes on the wire is given.
//
// THE OTHER TWO ADDRESSES ON THAT LIST WERE NOT BASES AT ALL, and calling them
// "not registry types" was a straight misreading -- the scan found each one's DESTRUCTOR
// and stopped before its CONSTRUCTOR:
//   * 06db408c (the list said 06db4090, its Encode slot): FUN_06da08e0 at 06da08fa
//     writes `MOV dword ptr [EAX + 0xc],0x30027` -- ch3 0x27 FLPACKET_CLIENT_TRADERESPONSE.
//     06d9a637, the reference the scan saw, is FUN_06d9a630, its destructor.
//   * 06db5d64: FUN_06da0ec0 at 06da0ee0 writes `MOV dword ptr [EAX + 0xc],0x30023` --
//     ch3 0x23 FLPACKET_CLIENT_DBGLOADSYSTEM, which this very table already routes to
//     RpcEncodeWireBlobBufferFirst / RpcDecodeWireBlobBufferFirstSkipEmpty. 06db10b8 is
//     FUN_06db10b0, its destructor.
// ch3 0x27's codec is transcribed at the end of this file.
// ===========================================================================

// 0x06da13d0 -- Encode. Header only, and it cannot fail; see PutHeaderUnchecked.
unsigned int RpcEncodeWireEmptyInline(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength) {
    // 06da13db/06da13e0: Limit = arg2, Ptr = *arg1. 06da13e5/06da13e8: Cursor = 0,
    // Error = 0.
    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06da13f0..06da1415
    *OutLength = Stream.Length();                                  // 06da141c, always 2
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());          // 06da141e..06da145c
    return 0;                                                          // 06da145f
}

// 0x06da1470 -- Decode. Consumes nothing; FUN_06da13c0 is `XOR EAX,EAX ; RET 0xc`.
unsigned int RpcDecodeWireEmptyInline(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength) {
    // 06da147b/06da1480: Limit = arg2, Ptr = arg1 DIRECTLY -- no second dereference,
    // the asymmetry every decoder in the module has.
    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    *OutLength = Stream.Length();                                  // 06da1493, always 0
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06da1495..06da14df
    return 0;                                                          // 06da14e2
}

// ===========================================================================
// Shape 0b -- no payload, but the out-of-line form. Registry size 0x20.
//
// LAB_06dae0d0 / LAB_06dabad0, 3 .rdata slots each, all three registered:
//   ch2 0x4c REVERT_CAMERA, ch2 0x58 CANCEL_MUSIC, ch3 0x2e DBGFREESYSTEM.
// Identical result to shape 0 on a buffer big enough; unlike shape 0 it reports 0x16
// when FUN_06da1380 cannot fit the two header bytes.
// ===========================================================================

// 0x06dae0d0 -- Encode.
unsigned int RpcEncodeWireEmpty(FPacketBase& Packet, unsigned char** BufferHolder,
                                unsigned int Capacity, unsigned int* OutLength) {
    // 06dae0dd `LEA EDI,[ESI+0x10]`; +0x04 = Capacity, +0x08 = *BufferHolder, +0x00 = 0,
    // +0x0c = 0.
    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06dae100
    // 06dae11f FUN_06da13c0 -- emits nothing, checked anyway.
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06dae12d
    }
    *OutLength = Stream.Length();                                  // 06dae13c
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());          // 06dae13e..06dae173
    return 0;                                                          // 06dae176
}

// 0x06dabad0 -- Decode.
unsigned int RpcDecodeWireEmpty(FPacketBase& Packet, const unsigned char* Buffer,
                                unsigned int Size, unsigned int* OutLength) {
    // 06dabae7/06dabaea: Limit = arg2, Ptr = arg1 directly.
    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06dabb09, 0x17
    }
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06dabb26
    }
    *OutLength = Stream.Length();                                  // 06dabb35
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06dabb37..06dabb78
    return 0;                                                          // 06dabb7b
}

// ===========================================================================
// Shape 1 -- one dword. Registry size 0x24, payload 0x04.
//
// LAB_06db0310 (30 .rdata slots) / LAB_06db03f0 (33). The decoder serves three more
// vtables than the encoder because the float encoder below shares it. The registered
// types, from the .rdata scan:
//   ch2 0x01 CONNECTRESPONSE, 0x0e GFCOMPLETENEWSBROADCASTLIST,
//   ch2 0x0f GFCOMPLETECHARLIST, 0x10 GFCOMPLETEMISSIONCOMPUTERLIST,
//   ch2 0x11 GFCOMPLETESCRIPTBEHAVIORLIST, 0x13 GFCOMPLETEAMBIENTSCRIPTLIST,
//   ch2 0x23 SETSHIPARCH, 0x30 SETCASH, 0x3e SETNUMKILLS, 0x3f SETMISSIONSUCCESSES,
//   ch2 0x40 SETMISSIONFAILURES, 0x43 SET_STORY_CUE, 0x56 SENDNNMESSAGE,
//   ch2 0x59 PLAY_SOUND_EFFECT, 0x5b MISSIONSAVEA,
//   ch3 0x04 REQUESTLAUNCH, 0x07 ENTERBASE, 0x0b SYSTEM_SWITCH_OUT_COMPLETE,
//   ch3 0x0d EXITBASE, 0x0e ENTERLOCATION, 0x0f EXITLOCATION, 0x13 GFSELECTOBJECT,
//   ch3 0x15 REQSHIPARCH, 0x1b REQSETCASH, 0x1c REQCHANGECASH, 0x24 DOCK,
//   ch3 0x35 INTERFACEITEMUSED, 0x41 ABORT_MISSION, 0x45 MISSIONSAVEB, 0x47 RTCDONE.
// ===========================================================================

// 0x06db0310 -- Encode.
unsigned int RpcEncodeWireDword(FPacketBase& Packet, unsigned char** BufferHolder,
                                unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerDword* Wire =
        reinterpret_cast<const FPayloadServerDword*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06db0340
    // 06db0357..06db0385: advance 4 and check, then `MOV ECX,[EDI+0x20] ; MOV [EAX],ECX`.
    Stream.PutDword(Wire->Field_0x00);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06db0377 / 06db03a2
    }
    *OutLength = Stream.Length();                                  // 06db03b1
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());          // 06db03b3..06db03e6
    return 0;
}

// 0x06db03f0 -- Decode.
unsigned int RpcDecodeWireDword(FPacketBase& Packet, const unsigned char* Buffer,
                                unsigned int Size, unsigned int* OutLength) {
    FPayloadServerDword* Wire = reinterpret_cast<FPayloadServerDword*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06db0429
    }
    unsigned int Scratch = 0;
    // 06db0432..06db0456. On failure 06db044e `JNZ LAB_06db04c3` returns the sticky
    // error rather than a fresh 0x17 -- the same value either way.
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Field_0x00 = Scratch;                                        // 06db0456
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06db0470
    }
    *OutLength = Stream.Length();                                  // 06db047f
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06db0481..06db04be
    return 0;
}

// ===========================================================================
// Shape 1f -- one float. Registry size 0x24, payload 0x04.
//
// LAB_06db1cd0, 3 .rdata slots, all registered: ch2 0x49 SETHULLSTATUS,
// ch3 0x3b REQHULLSTATUS, ch3 0x46 REQDIFFICULTYSCALE.
// The ONLY difference from shape 1's encoder is 06db1d42 `FLD float ptr [EDI + 0x20]` /
// 06db1d45 `FSTP float ptr [EAX]` where shape 1 has a MOV -- the same four bytes. The
// DECODE side is literally shape 1's LAB_06db03f0, which is why these three types read
// the field back as a dword.
// ===========================================================================

// 0x06db1cd0 -- Encode.
unsigned int RpcEncodeWireFloat(FPacketBase& Packet, unsigned char** BufferHolder,
                                unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerFloat* Wire =
        reinterpret_cast<const FPayloadServerFloat*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06db1d00
    // 06db1d1c advances 4 and checks BEFORE the FLD/FSTP at 06db1d42.
    Stream.PutFloat(Wire->Field_0x00);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06db1d37 / 06db1d62
    }
    *OutLength = Stream.Length();                                  // 06db1d71
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());          // 06db1d73..06db1da6
    return 0;
}

// ===========================================================================
// Shape 2 -- one byte. Registry size 0x24, payload 0x04, but only the first byte
// travels: 06da832c `INC EDX` is the whole cursor bump.
//
// LAB_06da82e0 / LAB_06da83c0, 2 .rdata slots each, both registered:
//   ch2 0x02 LOGINRESPONSE, ch2 0x5a GFMISSIONVENDORWHYEMPTY.
// ===========================================================================

// 0x06da82e0 -- Encode.
unsigned int RpcEncodeWireByte(FPacketBase& Packet, unsigned char** BufferHolder,
                               unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerByte* Wire =
        reinterpret_cast<const FPayloadServerByte*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06da8310
    // 06da8350 `MOV CL,byte ptr [EDI + 0x20]` / 06da8353 `MOV byte ptr [EAX],CL`.
    Stream.PutByte(Wire->Field_0x00);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06da8345 / 06da836e
    }
    *OutLength = Stream.Length();                                  // 06da837d
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());          // 06da837f..06da83b2
    return 0;
}

// 0x06da83c0 -- Decode.
unsigned int RpcDecodeWireByte(FPacketBase& Packet, const unsigned char* Buffer,
                               unsigned int Size, unsigned int* OutLength) {
    FPayloadServerByte* Wire = reinterpret_cast<FPayloadServerByte*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06da83f9
    }
    unsigned char Small8 = 0;
    if (!Stream.GetByte(Small8)) {                                     // 06da8407 / 06da8420
        return Stream.GetError();
    }
    Wire->Field_0x00 = Small8;                                         // 06da8424
    // Retail leaves the other three payload bytes exactly as the constructor zeroed
    // them; nothing writes them here, so neither do we.
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06da843c
    }
    *OutLength = Stream.Length();                                  // 06da844b
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06da8451..06da848a
    return 0;
}

// ===========================================================================
// Shape 3 -- two dwords. Registry size 0x28, payload 0x08.
//
// LAB_06da9110 / LAB_06d95d40, 13 .rdata slots each, all registered:
//   ch2 0x0d SETSTARTROOM, 0x14 GFDESTROYNEWSBROADCAST, 0x15 GFDESTROYCHARACTER,
//   ch2 0x16 GFDESTROYMISSIONCOMPUTER, 0x17 GFDESTROYSCRIPTBEHAVIOR,
//   ch2 0x19 GFDESTROYAMBIENTSCRIPT, 0x21 SYSTEM_SWITCH_OUT, 0x39 SCANNOTIFY,
//   ch3 0x19 REQREMOVEITEM, 0x2b CARGOSCAN, 0x3e LAUNCHCOMPLETE, 0x43 JUMPINCOMPLETE.
// (Thirteen slots, twelve registered types plus vtable 06db4090, whose constructor
// writes no routing header.)
// ===========================================================================

// 0x06da9110 -- Encode.
unsigned int RpcEncodeWireDwordPair(FPacketBase& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerDwordPair* Wire =
        reinterpret_cast<const FPayloadServerDwordPair*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06da9140
    Stream.PutDword(Wire->Field_0x00);                                 // 06da9182
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06da9177
    }
    Stream.PutDword(Wire->Field_0x04);                                 // 06da91b7
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06da91ac / 06da91d7
    }
    *OutLength = Stream.Length();                                  // 06da91e6
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());          // 06da91e8..06da921d
    return 0;
}

// 0x06d95d40 -- Decode.
unsigned int RpcDecodeWireDwordPair(FPacketBase& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength) {
    FPayloadServerDwordPair* Wire =
        reinterpret_cast<FPayloadServerDwordPair*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06d95d79
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();                                      // 06d95d9e
    }
    Wire->Field_0x00 = Scratch;                                        // 06d95dad
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();                                      // 06d95dcc
    }
    Wire->Field_0x04 = Scratch;                                        // 06d95dda
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06d95dee
    }
    *OutLength = Stream.Length();                                  // 06d95dfd
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06d95dff..06d95e3a
    return 0;
}

// ===========================================================================
// Shape 4 -- a dword then a byte. Registry size 0x28, payload 0x08, five bytes on the
// wire.
//
// LAB_06dab0e0 / LAB_06daf230, 5 .rdata slots each, all registered:
//   ch1 0x15 ACTIVATETHRUSTERS, ch2 0x06 DESTROYOBJECT, ch2 0x46 MARKOBJ,
//   ch3 0x08 REQUESTBASEINFO, ch3 0x09 REQUESTLOCATIONINFO.
// rpc_payloads.h had already given DESTROYOBJECT and MARKOBJ the {dword, byte, 3 pad}
// shape from their senders; the shared codec is the independent second witness, and it
// also settles the other three, which had no per-type struct.
// ===========================================================================

// 0x06dab0e0 -- Encode.
unsigned int RpcEncodeWireDwordByte(FPacketBase& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerDwordByte* Wire =
        reinterpret_cast<const FPayloadServerDwordByte*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06dab110
    Stream.PutDword(Wire->Field_0x00);                                 // 06dab152
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06dab147 / 06dab17a
    }
    Stream.PutByte(Wire->Field_0x04);                                  // 06dab185
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06dab1a3
    }
    *OutLength = Stream.Length();                                  // 06dab1b2
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());          // 06dab1b4..06dab1e9
    return 0;
}

// 0x06daf230 -- Decode.
unsigned int RpcDecodeWireDwordByte(FPacketBase& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength) {
    FPayloadServerDwordByte* Wire =
        reinterpret_cast<FPayloadServerDwordByte*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06daf269
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();                                      // 06daf282
    }
    Wire->Field_0x00 = Scratch;                                        // 06daf29d
    unsigned char Small8 = 0;
    if (!Stream.GetByte(Small8)) {
        return Stream.GetError();                                      // 06daf2ae
    }
    Wire->Field_0x04 = Small8;                                         // 06daf2c6
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06daf2da
    }
    *OutLength = Stream.Length();                                  // 06daf2e9
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06daf2ef..06daf326
    return 0;
}

// ===========================================================================
// Shape 5 -- three dwords. Registry size 0x2c, payload 0x0c.
//
// LAB_06db0160 / LAB_06da6e40, 8 .rdata slots each, all registered:
//   ch1 0x10 STOPTRADELANE, ch2 0x0b LAND, ch2 0x2f SETREMOVEITEM,
//   ch2 0x3c REPAIROBJECT, ch3 0x11 GFGOODSELL, ch3 0x2f SETMANEUVER,
//   ch3 0x3c GFGOODVAPORIZED, ch3 0x3f HAIL.
// Offsets read back from the decoder: 06da6eae +0x20, 06da6ed6 +0x24, 06da6ef7 +0x28,
// in that order, which is the order the encoder writes them (06db01d2 / 06db0207 /
// 06db023c). Types whose per-type struct calls the middle field a byte -- ch2 0x2f
// SETREMOVEITEM is one -- still move a whole dword here, exactly as retail does; the
// padding travels.
// ===========================================================================

// 0x06db0160 -- Encode.
unsigned int RpcEncodeWireThreeDwords(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerThreeDwords* Wire =
        reinterpret_cast<const FPayloadServerThreeDwords*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06db0190
    Stream.PutDword(Wire->Field_0x00);                                 // 06db01d2
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06db01c7
    }
    Stream.PutDword(Wire->Field_0x04);                                 // 06db0207
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06db01fc
    }
    Stream.PutDword(Wire->Field_0x08);                                 // 06db023c
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06db0231 / 06db025c
    }
    *OutLength = Stream.Length();                                  // 06db026b
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());          // 06db026d..06db02a0
    return 0;
}

// 0x06da6e40 -- Decode.
unsigned int RpcDecodeWireThreeDwords(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength) {
    FPayloadServerThreeDwords* Wire =
        reinterpret_cast<FPayloadServerThreeDwords*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06da6e79
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();                                      // 06da6e9f
    }
    Wire->Field_0x00 = Scratch;                                        // 06da6eae
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();                                      // 06da6ec7
    }
    Wire->Field_0x04 = Scratch;                                        // 06da6ed6
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();                                      // 06da6eef
    }
    Wire->Field_0x08 = Scratch;                                        // 06da6ef7
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06da6f11
    }
    *OutLength = Stream.Length();                                  // 06da6f20
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06da6f22..06da6f5f
    return 0;
}

// ===========================================================================
// Shape 6 -- a dword then a twelve-byte block. Registry size 0x30, payload 0x10.
//
// LAB_06db14f0 / LAB_06dad820, 3 .rdata slots each, all registered:
//   ch2 0x31 EXPLODEASTEROIDMINE, ch2 0x53 FORMATION_UPDATE, ch3 0x30 DBGRELOCATE_SHIP.
// The twelve bytes move through FUN_06d8f510, the three-dword copier retail uses for a
// Vector (06db15c3 out, 06dad8b6 back into `LEA ECX,[EDI + 0x24]`), and the cursor is
// bumped by 0xc in one go (06db157d / 06dad897) -- so it is one run, not three fields.
// rpc_payloads.h calls the FORMATION_UPDATE form {dword, Vector} and the
// EXPLODEASTEROIDMINE form {four dwords}; both are the same sixteen bytes and the
// shared codec cannot tell them apart, so this transcription uses the neutral one.
// ===========================================================================

// 0x06db14f0 -- Encode.
unsigned int RpcEncodeWireDwordBlock12(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerFourDwords* Wire =
        reinterpret_cast<const FPayloadServerFourDwords*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06db1524
    Stream.PutDword(Wire->Field_0x00);                                 // 06db156e
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06db1560
    }
    // 06db157d `ADD EDX,0xc` guards the whole run; 06db15c3 then copies it.
    Stream.PutBytes(&Wire->Field_0x04, 12u);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06db1599 / 06db15e4
    }
    *OutLength = Stream.Length();                                  // 06db15f6
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());          // 06db15f8..06db162d
    return 0;
}

// 0x06dad820 -- Decode.
unsigned int RpcDecodeWireDwordBlock12(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength) {
    FPayloadServerFourDwords* Wire =
        reinterpret_cast<FPayloadServerFourDwords*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06dad85b
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();                                      // 06dad874
    }
    Wire->Field_0x00 = Scratch;                                        // 06dad88d
    if (!Stream.GetBytes(&Wire->Field_0x04, 12u)) {                    // 06dad897 / 06dad8b6
        return Stream.GetError();
    }
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06dad8d8
    }
    *OutLength = Stream.Length();                                  // 06dad8e7
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06dad8ed..06dad926
    return 0;
}

// ===========================================================================
// Shape 7 -- {dword count, that many raw bytes}, count FIRST in the payload.
// Registry size 0x28, payload 0x08 == FPayloadServerBlob {Size, Buffer}.
//
// LAB_06d97570 / LAB_06d978f0, 5 .rdata slots each, all registered:
//   ch2 0x1a GFSCRIPTBEHAVIOR, 0x1d GFUPDATEMISSIONCOMPUTER,
//   ch2 0x1e GFUPDATENEWSBROADCAST, 0x20 GFMISSIONVENDORACCEPTANCE, 0x26 GFUPDATECHAR.
//
// THE DECODER ALLOCATES UNCONDITIONALLY -- 06d97963 `CALL operator_new` sits between
// the count's bounds check and the run's, with no test of the existing pointer. A
// packet decoded twice leaks its first block. That is retail; the port does not add a
// guard, and it does not free the block here either -- the (channel, opcode) payload
// packet class's destructor owns it (rpc_packet_registry.cpp).
// ===========================================================================

// 0x06d97570 -- Encode.
unsigned int RpcEncodeWireBlobSizeFirst(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerBlob* Wire =
        reinterpret_cast<const FPayloadServerBlob*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06d975a1
    Stream.PutDword(Wire->Size);                                       // 06d975e5
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06d975da / 06d97612
    }
    // 06d9761b takes the count again and 06d97620 the source pointer; the cursor is
    // advanced by the whole run before the REP MOVSD/MOVSB.
    Stream.PutBytes(Wire->Buffer, Wire->Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06d97659
    }
    *OutLength = Stream.Length();                                  // 06d97668
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());          // 06d9766a..06d9769d
    return 0;
}

// 0x06d978f0 -- Decode.
unsigned int RpcDecodeWireBlobSizeFirst(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength) {
    FPayloadServerBlob* Wire = reinterpret_cast<FPayloadServerBlob*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06d97929
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();                                      // 06d9794e
    }
    Wire->Size = Scratch;                                              // 06d97960
    Wire->Buffer = ::operator new(Wire->Size, std::nothrow);           // 06d97963
    if (!Stream.GetBytes(Wire->Buffer, Wire->Size)) {                  // 06d9796e / 06d979a6
        return Stream.GetError();                                      // 06d9798c
    }
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06d979d2
    }
    *OutLength = Stream.Length();                                  // 06d979e1
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06d979e7..06d97a1e
    return 0;
}

// ===========================================================================
// Shape 8 -- the same {dword count, raw bytes} pair, but the payload holds the POINTER
// first and the count second. Encoder LAB_06da6190, 10 .rdata slots.
//
// Its ten types do not all share a decoder, which is what makes the encoder worth
// reading carefully: the count it emits comes from packet+0x24 (06da6205) and the bytes
// from packet+0x20 (06da6240), the mirror of shape 7. The registered types:
//   ch1 0x11 SET_WEAPON_GROUP, 0x13 SET_VISITED_STATE, 0x16 REQUEST_BEST_PATH,
//   ch1 0x17 REQUEST_GROUP_POSITIONS, 0x18 REQUEST_PLAYER_STATS,
//   ch1 0x19 SET_MISSION_LOG, 0x1a REQUEST_RANK_LEVEL, 0x1c SET_INTERFACE_STATE,
//   ch2 0x1f GFUPDATEAMBIENTSCRIPT, ch3 0x23 DBGLOADSYSTEM.
// The eight channel-1 ones carry the 0x0c FPayloadCommonBuffer; the last two carry the
// 0x08 FPayloadServerBlobReversed. Both start {Buffer, Size}, which is all this encoder
// touches, so one body really does serve both.
// ===========================================================================

// 0x06da6190 -- Encode.
unsigned int RpcEncodeWireBlobBufferFirst(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerBlobReversed* Wire =
        reinterpret_cast<const FPayloadServerBlobReversed*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06da61c1
    Stream.PutDword(Wire->Size);                                       // 06da6205
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06da61fa / 06da6232
    }
    // 06da6217 advances the cursor by the count, then 06da624e/06da6255 copy. The
    // destination pointer is spilled at 06da624a into [ESP+0x18] -- the function's own
    // second argument slot -- and read back at 06da6257; see the file header.
    Stream.PutBytes(Wire->Buffer, Wire->Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06da6279
    }
    *OutLength = Stream.Length();                                  // 06da6288
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());          // 06da628a..06da62be
    return 0;
}

// 0x06d97730 -- Decode, the 0x28 form. ch2 0x1f GFUPDATEAMBIENTSCRIPT only.
// Allocates unconditionally, exactly like shape 7's decoder.
unsigned int RpcDecodeWireBlobBufferFirst(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength) {
    FPayloadServerBlobReversed* Wire =
        reinterpret_cast<FPayloadServerBlobReversed*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06d97769
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();                                      // 06d9778e
    }
    Wire->Size = Scratch;                                              // 06d977a0
    Wire->Buffer = ::operator new(Wire->Size, std::nothrow);           // 06d977a3
    if (!Stream.GetBytes(Wire->Buffer, Wire->Size)) {                  // 06d977ae onward
        return Stream.GetError();
    }
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06d97812
    }
    *OutLength = Stream.Length();                                  // 06d97821
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06d97827..06d9785e
    return 0;
}

// 0x06db10e0 -- Decode, the 0x28 form again, ch3 0x23 DBGLOADSYSTEM only.
//
// The same body as 0x06d97730 EXCEPT for one branch: 06db114f `TEST EAX,EAX` /
// 06db1154 `JBE LAB_06db11af` skips BOTH the allocation and the byte run when the
// decoded count is zero. (After TEST the carry flag is clear, so JBE is exactly
// `if (Size == 0)`.) The buffer pointer is then left at whatever the constructor put
// there, i.e. null. Reproduced rather than unified.
unsigned int RpcDecodeWireBlobBufferFirstSkipEmpty(FPacketBase& Packet,
                                                   const unsigned char* Buffer,
                                                   unsigned int Size,
                                                   unsigned int* OutLength) {
    FPayloadServerBlobReversed* Wire =
        reinterpret_cast<FPayloadServerBlobReversed*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06db111f
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();                                      // 06db113e
    }
    Wire->Size = Scratch;                                              // 06db1151
    if (Wire->Size != 0) {                                             // 06db114f / 06db1154
        Wire->Buffer = ::operator new(Wire->Size, std::nothrow);       // 06db1157
        if (!Stream.GetBytes(Wire->Buffer, Wire->Size)) {              // 06db1162 onward
            return Stream.GetError();
        }
    }
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06db11c6
    }
    *OutLength = Stream.Length();                                  // 06db11d5
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06db11db..06db1214
    return 0;
}

// 0x06da62d0 -- Decode, the 0x0c channel-1 buffer family. 7 .rdata slots:
//   ch1 0x13 SET_VISITED_STATE, 0x16 REQUEST_BEST_PATH, 0x17 REQUEST_GROUP_POSITIONS,
//   ch1 0x18 REQUEST_PLAYER_STATS, 0x19 SET_MISSION_LOG, 0x1a REQUEST_RANK_LEVEL,
//   ch1 0x1c SET_INTERFACE_STATE.
//
// Two differences from the 0x28 forms, both read off the bytes:
//   * the allocation is SKIPPED when the packet already holds a pointer --
//     06da6340 `MOV EAX,[EBX + 0x20]` then a TEST/JNZ over the operator_new;
//   * on a fresh allocation the third payload field is set to 1
//     (06da6358 `MOV byte ptr [EBX + 0x28],0x1`), which is the same literal 1 the
//     channel-1 senders write: bOwnsBuffer, which the class destructor reads to
//     decide whether to free the block (FPayloadCommonBuffer, rpc_payloads.h).
unsigned int RpcDecodeWireOwnedBuffer(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength) {
    FPayloadCommonBuffer* Wire =
        reinterpret_cast<FPayloadCommonBuffer*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06da6310
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();                                      // 06da6325
    }
    Wire->Size = Scratch;                                              // 06da6345
    if (Wire->Buffer == nullptr) {                                     // 06da6340
        Wire->Buffer = ::operator new(Wire->Size, std::nothrow);       // 06da634d
        Wire->bOwnsBuffer = 1;                                         // 06da6358
    }
    if (!Stream.GetBytes(Wire->Buffer, Wire->Size)) {                  // 06da635c / 06da6382
        return Stream.GetError();                                      // 06da6370
    }
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06da63c2
    }
    *OutLength = Stream.Length();                                  // 06da63d2
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06da63d8..06da6411
    return 0;
}

// 0x06da5bc0 -- Decode, ch1 0x11 SET_WEAPON_GROUP only (1 .rdata slot, 06db439c).
//
// Byte for byte 0x06da62d0 plus TWO instructions, and they change what the field IS:
//   06da5c3c `INC EAX` before the operator_new -- the block is Size + 1 bytes;
//   06da5ca1 `MOV byte ptr [EDX + ECX*0x1],0x0` after the copy -- Buffer[Size] = 0.
// So SET_WEAPON_GROUP's blob is a NUL-terminated string and the other seven members of
// the family's blobs are not. Folding this into 0x06da62d0 would have dropped the
// terminator and handed the receiver an unterminated buffer.
unsigned int RpcDecodeWireOwnedString(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength) {
    FPayloadCommonBuffer* Wire =
        reinterpret_cast<FPayloadCommonBuffer*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06da5c00
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();                                      // 06da5c15
    }
    Wire->Size = Scratch;                                              // 06da5c35
    if (Wire->Buffer == nullptr) {                                     // 06da5c30 / 06da5c38
        Wire->Buffer = ::operator new(Wire->Size + 1u, std::nothrow);  // 06da5c3c / 06da5c3e
        Wire->bOwnsBuffer = 1;                                         // 06da5c49
    }
    if (!Stream.GetBytes(Wire->Buffer, Wire->Size)) {                  // 06da5c4d / 06da5c87
        return Stream.GetError();                                      // 06da5c61
    }
    // 06da5ca1: the terminator goes in AFTER the copy, at Buffer[Size]. Retail writes
    // it unconditionally, including when the buffer was one it found already in the
    // payload and therefore did not size itself.
    reinterpret_cast<unsigned char*>(Wire->Buffer)[Wire->Size] = 0;
    if (Stream.Failed()) {
        return Stream.GetError();                                      // 06da5cbd
    }
    *OutLength = Stream.Length();                                  // 06da5ccd
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());          // 06da5cd3..06da5d0c
    return 0;
}

// ===========================================================================
// Shape 25 -- the EQUIPMENT/CARGO LIST. Registry size 0x24, payload 0x04.
//
// LAB_06d916a0 / LAB_06d95bf0 are the two slots; the bodies that do the work are
// FUN_06da2240 (encode, 206 instructions) and FUN_06da2480 (decode, 213), inlined here
// because the only other caller of either is the ch2 0x3d REMOTEOBJECTCARGOUPDATE
// encoder at 06d8f902, which reaches FUN_06da2240 for the same payload+0x20 handle.
//
// FOUR TYPES, each proven from its own constructor's routing header, not from a name:
//   06d9c6a0  MOV [EAX],.rdata:PTR_FUN_06db3cf0  /  MOV [EAX+0xc],0x20024   ch2 0x24
//   06d9c710  MOV [EAX],.rdata:PTR_FUN_06db3d10  /  MOV [EAX+0xc],0x20025   ch2 0x25
//   06da0700  MOV [EAX],.rdata:PTR_FUN_06db3de0  /  MOV [EAX+0xc],0x30016   ch3 0x16
//   06da06a0  MOV [EAX],.rdata:PTR_FUN_06db3dfc  /  MOV [EAX+0xc],0x30017   ch3 0x17
// and each of those four vtables carries LAB_06d916a0 in slot +0x04 and LAB_06d95bf0 in
// slot +0x08 (.rdata:06db3cf4/06db3cf8, 06db3d14/06db3d18, 06db3de4/06db3de8,
// 06db3e00/06db3e04). Counted from the .rdata side as well as from the XREF header, per
// this file's own caution: four slots each way, and here the two counts agree.
// So SETEQUIPMENT, SETCARGO, REQEQUIPMENT and REQCARGO share ONE codec pair -- which is
// why cargo and equipment have the same wire format rather than merely a similar one.
//
// THE RECORD IS NOT A FLAT STRUCT AND THE WIRE ORDER IS NOT THE STRUCT ORDER. The
// payload's four bytes are a POINTER to the 4-byte handle (operator new(4)); the handle
// points at a 0x0c list header { allocator, dummy node, Size }; each 0x0c node holds a
// POINTER to a 0x18 record. All of that already existed in rpc_containers.h -- this
// codec only had to agree with it, and it does, field for field.
//
// Wire order, read off the encoder:
//   word  Count            06da2259 `MOV CX,word ptr [EAX+0x8]` -- the list's Size,
//                          32-bit in memory, only its LOW WORD on the wire
//   per element:
//   dword Count            06da2304 record+0x04
//   float Status           06da232c `FLD float ptr [EDX+0x8]` / 06da2332 `FSTP`
//   dword ArchId           06da235c record+0x00
//   word  Id               06da2388 record+0x0c
//   byte  bEquipped        06da23b6 record+0x0e
//   byte  Field_0x0f       06da23e0 record+0x0f  (EquipDesc::bMission)
//   word  HardpointSize    06da240b record+0x10
//   HardpointSize bytes    06da243b..06da2455, a REP MOVSD/MOVSB run, no length shift
//
// THE FLOAT IS A CLAIM RE-VERIFIED ON THE BYTES RATHER THAN INHERITED. The `.c` renders
// Status as `*puVar3 = puVar10[2]`, an ordinary dword move, which would have left the
// field's type unproven. The listing does not: 06da232c is `FLD float ptr [EDX + 0x8]`
// and 06da2332 is `FSTP float ptr [EAX]`, so retail really does route those four bytes
// through the x87 stack and the field is a float. There is no FMUL anywhere between
// them, so it is NOT quantised -- unlike the equipment status inside CREATESHIP, which
// is a 255.0-scaled byte. Two wire formats for the same EquipDesc field; confusing them
// would be a silent precision loss.
// The DECODER moves the same four bytes with a plain `MOV EDX,[EAX]` / `MOV [EBX+8],EDX`
// (06da257d/06da2582). The asymmetry is retail's and is bit-exact either way, which is
// why FRpcMarshalStream::GetFloat (a memcpy) is the faithful reading of it.
//
// SECOND INDEPENDENT CONFIRMATION, from a type that is NOT in this family: ch2 0x03
// CHARACTERINFO embeds the very same seven fields in the very same order at
// 06d952f6..06d953f6, and rpc_containers.cpp already transcribed them there. Two
// unrelated codecs agreeing field for field is what makes the record layout settled
// rather than merely self-consistent.
//
// WHAT THE DECODER DOES THAT LOOKS LIKE A BUG AND IS REPRODUCED ANYWAY:
//   * 06da265c `CALL operator_new` with HardpointSize in EDX -- called even when the
//     size is zero (VC6 returns a 1-byte block), and 06da2661 stores the result with NO
//     null check.
//   * 06da2510 `JZ LAB_06da26bf` -- when operator new(0x18) fails the iteration is
//     skipped but the loop counter still advances, so a failed allocation silently
//     drops one element instead of failing the packet.
//   * an error in the middle of a record returns without freeing that record. The
//     half-built 0x18 block and its hardpoint buffer leak. Retail leaks them too.
//   * the decoder APPENDS. It never clears a list the packet already holds.
//   * 06da26c4 `CMP AX,SI` -- the loop counter is compared as a WORD, not a dword.
// ===========================================================================

// ===========================================================================
// FUN_06da2240 / FUN_06da2480, factored out of the slots that call them.
//
// Retail really does share these two bodies between two unrelated slot families, and it
// is a CALL in both, not an inlining: `CALL FUN_06da2240` appears at 06d916f6 (the
// SETEQUIPMENT / SETCARGO / REQEQUIPMENT / REQCARGO encoder LAB_06d916a0) and at
// 06d8f902 (the ch2 0x3d REMOTEOBJECTCARGOUPDATE encoder LAB_06d8f840), and nowhere
// else; `CALL FUN_06da2480` appears at 06d95c3f and 06d8fa17, and nowhere else. Those
// four call sites are the whole XREF set, counted on the listing rather than taken from
// Ghidra's XREF header.
//
// The three stack arguments both take are DEAD in both bodies -- every access goes
// through ECX, i.e. the packet -- which is why the helpers here take the stream and the
// payload slot instead of a codec's argument triple. The wire format, the retail quirks
// and the per-field addresses are documented on the slots below, unchanged.
// ===========================================================================
bool RpcPutEquipList(FRpcMarshalStream& Stream, FRpcEquipWireHandle* const* Slot) {
    // 06da2247..06da225d: the count is zero when the payload slot is null OR when the
    // handle has no list yet. Both tests are real and both are separate.
    unsigned short Count = 0;
    const FRpcEquipWireListHeader* Header = RpcEquipHandleGetList(*Slot);
    if (Header != nullptr) {
        Count = (unsigned short)(Header->Size & 0xffffu);               // 06da2259
    }
    if (!Stream.PutWord(Count)) {                                      // 06da2261..06da2295
        return false;                     // 06da2286
    }
    // 06da22a1 `JBE LAB_06da2473` on `TEST CX,CX` -- TEST clears CF, so JBE is JZ here.
    if (Count != 0) {
        // 06da22a7: the handle is dereferenced a SECOND time and re-tested (06da22b5).
        const FRpcEquipWireListHeader* Walk = RpcEquipHandleGetList(*Slot);
        if (Walk != nullptr && Walk->Head != nullptr) {
            const FRpcEquipWireNode* Node = Walk->Head->Next;           // 06da22be
            // 06da22c0/06da22c6: an empty ring falls straight through to the tail.
            while (Node != Walk->Head) {
                // 06da22e0 `MOV EDX,[EAX+0x8]` -- retail does NOT null-test the node's
                // value, and neither does this; adding a test would be a guard retail
                // does not have.
                const FRpcEquipWireRecord* Item = Node->Value;
                if (!Stream.PutDword((unsigned int)Item->Count)) {      // 06da2304
                    return false;
                }
                if (!Stream.PutFloat(Item->Status)) {                   // 06da232c FLD/FSTP
                    return false;
                }
                if (!Stream.PutDword(Item->ArchId)) {                   // 06da235c
                    return false;
                }
                if (!Stream.PutWord(Item->Id)) {                        // 06da2388
                    return false;
                }
                if (!Stream.PutByte(Item->bEquipped)) {                 // 06da23b6
                    return false;
                }
                if (!Stream.PutByte(Item->Field_0x0f)) {                // 06da23e0
                    return false;
                }
                if (!Stream.PutWord(Item->HardpointSize)) {             // 06da240b
                    return false;
                }
                // 06da2418 `MOVZX EAX,word ptr [EDX+0x10]` with no shift: the run is
                // BYTES, not characters -- the wide-string doubling does not apply here.
                if (!Stream.PutBytes(Item->Hardpoint, Item->HardpointSize)) {
                    return false;          // 06da2435
                }
                Node = Node->Next;                                      // 06da2464
            }                                                           // 06da2466/06da246d
        }
    }
    return true;
}

bool RpcGetEquipList(FRpcMarshalStream& Stream, FRpcEquipWireHandle** Slot) {
    unsigned short Count = 0;
    if (!Stream.GetWord(Count)) {                                       // 06da2487..06da24b2
        return false;                                       // 06da26d3
    }
    // 06da24c2 `JBE LAB_06da26d1` -- an empty list is a complete packet, and the handle
    // is NOT allocated for it.
    if (Count != 0) {
        if (*Slot == nullptr) {                                         // 06da24c8
            FRpcEquipWireHandle* Handle =
                static_cast<FRpcEquipWireHandle*>(::operator new(sizeof(FRpcEquipWireHandle),
                                                                 std::nothrow));
            if (Handle != nullptr) {
                Handle->List = nullptr;                                 // 06da24e1
            }
            *Slot = Handle;                                             // 06da24eb
        }
        // 06da24f9: retail tests the count a SECOND time before entering the loop.
        for (unsigned short Index = 0; Index < Count; ++Index) {        // 06da26bf..06da26cb
            FRpcEquipWireRecord* Item =
                static_cast<FRpcEquipWireRecord*>(::operator new(sizeof(FRpcEquipWireRecord),
                                                                 std::nothrow));
            if (Item == nullptr) {
                // 06da2510 `JZ LAB_06da26bf` -- the element is silently dropped and the
                // loop moves on. Not an error in retail, so not one here.
                continue;
            }
            // 06da2516..06da252c, all eight fields, in this order.
            Item->ArchId = 0;
            Item->Count = 0;
            Item->Status = 0.0f;
            Item->Id = 0;
            Item->bEquipped = 0;
            Item->Field_0x0f = 0;
            Item->HardpointSize = 0;
            Item->Pad_0x12[0] = 0;
            Item->Pad_0x12[1] = 0;
            Item->Hardpoint = nullptr;

            unsigned int Scratch = 0;
            if (!Stream.GetDword(Scratch)) {                            // 06da2551
                return false;                               // 06da26dd
            }
            Item->Count = (int)Scratch;                                 // record+0x04
            // 06da257d/06da2582 `MOV EDX,[EAX]` / `MOV [EBX+0x8],EDX` -- a raw dword
            // store, no x87 on this side; the bit pattern crosses unchanged.
            if (!Stream.GetFloat(Item->Status)) {
                return false;
            }
            if (!Stream.GetDword(Item->ArchId)) {                       // 06da25a3
                return false;
            }
            if (!Stream.GetWord(Item->Id)) {                            // 06da25d9
                return false;
            }
            if (!Stream.GetByte(Item->bEquipped)) {                     // 06da2600
                return false;
            }
            if (!Stream.GetByte(Item->Field_0x0f)) {                    // 06da2620
                return false;
            }
            if (!Stream.GetWord(Item->HardpointSize)) {                 // 06da2647
                return false;
            }
            // 06da2657..06da2661: allocated BEFORE the run is bounds-checked, with the
            // decoded size and no null test on the result. A zero size still allocates.
            Item->Hardpoint =
                static_cast<char*>(::operator new(Item->HardpointSize, std::nothrow));
            if (!Stream.GetBytes(Item->Hardpoint, Item->HardpointSize)) {
                return false;                               // 06da2684
            }
            // 06da26b1 FUN_06d91580 -- the list takes ownership of the record.
            RpcEquipHandlePushBack(*Slot, Item);
        }
    }
    return true;
}

// 0x06d916a0 (slot) + 0x06da2240 (body) -- Encode.
unsigned int RpcEncodeWireEquipList(FPacketBase& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength) {
    FRpcEquipWireHandle* const* Slot =
        reinterpret_cast<FRpcEquipWireHandle* const*>(Packet.Payload());

    // 06d916b1/06d916b6: Limit = arg2, Ptr = *arg1; 06d916b9/06d916bf: cursor and error
    // cleared.
    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));   // 06d916d1

    if (!RpcPutEquipList(Stream, Slot)) {                                // FUN_06da2240
        return FRpcMarshalStream::kEncodeOverflow;
    }

    // 06d91707 FUN_06da13c0 -- `XOR EAX,EAX ; RET 0xc`, nothing more to emit.
    *OutLength = Stream.Length();                                   // 06d9171f..06d91725
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());           // 06d91727..06d91757
    return 0;                                                           // 06d9175a
}

// 0x06d95bf0 (slot) + 0x06da2480 (body) -- Decode.
unsigned int RpcDecodeWireEquipList(FPacketBase& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength) {
    FRpcEquipWireHandle** Slot = reinterpret_cast<FRpcEquipWireHandle**>(Packet.Payload());

    // 06d95c07/06d95c0a: Limit = arg2, Ptr = arg1 DIRECTLY -- the decoder's asymmetry.
    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06d95c1a FUN_06da13c0 -- no header is consumed on the way in.

    if (!RpcGetEquipList(Stream, Slot)) {                                // FUN_06da2480
        return Stream.GetError();
    }

    // 06d95c4d FUN_06da13c0 again -- still nothing to consume.
    *OutLength = Stream.Length();                                   // 06d95c65..06d95c6b
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());           // 06d95c6d..06d95ca8
    return 0;
}

// ===========================================================================
// ch3 0x27 FLPACKET_CLIENT_TRADERESPONSE -- LAB_06da9110 / LAB_06d9a660
//
// The type this file's header note was wrong about. Its vtable is 06db408c:
//   FUN_06da08e0  06da08f4  MOV dword ptr [EAX],.rdata:PTR_FUN_06db408c
//                 06da08fa  MOV dword ptr [EAX + 0xc],0x30027
//   vtable 06db408c = { dtor FUN_06d9a7b0, Encode LAB_06da9110, Decode LAB_06d9a660,
//                       IServerImpl::GetServerStats }
// The scan that produced the header note saw only 06d9a637 -- FUN_06d9a630, the
// DESTRUCTOR -- and concluded there was no routing header. There is one.
//
// THE ENCODER IS SHARED AND THE DECODER IS NOT, which is unusual and is the whole story
// here. 06db4090 is one of THIRTEEN .rdata slots naming LAB_06da9110 (the XREF header
// says 13 and the .rdata scan finds 13, so for once they agree), i.e. the plain
// dword-pair encoder this file already exposes as RpcEncodeWireDwordPair. The decode
// slot 06db4094 names LAB_06d9a660, which nothing else uses.
//
// AND THE PAIR DOES NOT ROUND-TRIP. rpc_payloads.h has the payload as
// FPayloadClientBlob { unsigned int Size; void* Buffer; } -- proven from the sender,
// IServerImpl::TradeResponse at 0x06d8dad0, which calls FUN_06d97b00, the same block
// writer the channel-2 blob family uses, and from caseD_27, which calls IServer slot 44
// as TradeResponse(payload+0x04, payload+0x00, Session). So payload+0x04 is a POINTER to
// a packet-owned copy, and FUN_06d9a7b0's destructor frees it (06d9a633/06d9a63d
// `MOV EAX,[ESI+0x24]` / `operator delete`).
// The shared encoder ships that pointer verbatim: 06da91b7 `MOV EDX,dword ptr [EDI+0x24]`
// / 06da91ba `MOV dword ptr [EAX],EDX`. Four bytes of address, not Size bytes of payload.
// The decoder then makes the mirror-image mistake, and it is worth spelling out because
// it looks like a block codec for eleven instructions and is not:
//   06da9(6c4..6d1)  first dword -> packet+0x20 (Size), and the SAME value is pushed
//   06d9a6d3         CALL operator_new  -- Size bytes
//   06d9a6d8         MOV dword ptr [EDI+0x24],EAX   -- the block lands in Buffer
//   06d9a702         MOV dword ptr [EDI+0x24],ECX   -- and the SECOND DWORD OFF THE WIRE
//                                                     overwrites it, unread
// The allocation is leaked on the spot, nothing is ever copied into it, and Buffer ends
// up holding the sender's address. Retail's TRADERESPONSE simply does not survive a real
// wire; it only works because rpclocal's local path hands the packet over by pointer.
// Both halves are reproduced, including the leak -- correcting either one would be a
// behaviour this port invented. The single-player path does not reach here.
//
// One thing checked and found NOT to be the case: this decoder does not inherit
// PLAYER_TRADE's cross-direction error code. Both of its bounds checks store the proper
// decoder value -- 06d9a6b2 and 06d9a6ee are each `MOV dword ptr [ESI+0xc],0x17`.
// ===========================================================================

// 0x06d9a660 -- Decode. The encode side is RpcEncodeWireDwordPair (0x06da9110).
unsigned int RpcDecodeWireTradeResponse(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength) {
    FPayloadClientBlob* Wire = reinterpret_cast<FPayloadClientBlob*>(Packet.Payload());

    // 06d9a677/06d9a67a: Limit = arg2, Ptr = arg1 directly.
    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    // 06d9a68a FUN_06da13c0 -- no header consumed.

    if (!Stream.GetDword(Wire->Size)) {                            // 06d9a6a2..06d9a6ce
        return Stream.GetError();                                  // 06d9a76f
    }
    // 06d9a6d3: allocated with the decoded size, stored into Buffer, and never written
    // to. The very next field overwrites the pointer, so this block is unreachable from
    // the moment it exists.
    Wire->Buffer = ::operator new(Wire->Size, std::nothrow);       // 06d9a6d8
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {                               // 06d9a6db..06d9a6fe
        return Stream.GetError();
    }
    // 06d9a702 `MOV dword ptr [EDI+0x24],ECX` -- the second wire dword lands on top of
    // the pointer. Faithful, and deliberately not repaired.
    Wire->Buffer = reinterpret_cast<void*>(static_cast<size_t>(Scratch));

    // 06d9a70d FUN_06da13c0 -- nothing to consume.
    *OutLength = Stream.Length();                              // 06d9a725..06d9a72b
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());      // 06d9a72d..06d9a766
    return 0;
}
