// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_wire_closeout.cpp -- the last thirteen channel-1 / channel-2 codec slots.
//
// With these and the nine in rpc_wire_ch3.cpp, all 162 registered packet types have
// their real Encode/Decode pair and nothing falls back to "routing header plus a raw
// blit of our in-memory payload" any more.
//
//   ch1 0x05 CHATMSG                 vtable 06db4428  enc LAB_06da6420  dec LAB_06da65d0
//   ch1 0x1b POP_UP_DIALOG           vtable 06db43d8  enc LAB_06da5de0  dec LAB_06da5fe0
//   ch2 0x07 LAUNCH                  vtable 06db50ec  enc LAB_06daab40  dec LAB_06daad40
//   ch2 0x22 SYSTEM_SWITCH_IN        vtable 06db505c  enc LAB_06da9260  dec LAB_06da9430
//   ch2 0x2b SETGROUPFEELINGS        vtable 06db3d30  enc LAB_06d91890  dec LAB_06d91a40
//   ch2 0x32 REQUESTSPACESCRIPT      vtable 06db3ff8  enc LAB_06d986e0  dec LAB_06d98850
//   ch2 0x34 REPLACEMISSIONOBJECTIVE vtable 06db512c  enc LAB_06dab4b0  dec LAB_06dab630
//   ch2 0x4b SET_CAMERA              vtable 06db514c  enc LAB_06dab780  dec LAB_06dab960
//   ch2 0x4d LOADHINT                vtable 06db3cd0  enc LAB_06d90a50  dec LAB_06d90ba0
//   ch2 0x4e SETDIRECTIVE            vtable 06db509c  enc LAB_06d90d10  dec LAB_06d90f70
//   ch2 0x52 PLAYERLIST              vtable 06db4038  enc LAB_06d99990  dec LAB_06d99b60
//   ch2 0x55 OBJECTCARGOUPDATE       vtable 06db531c  enc LAB_06dad9b0  dec LAB_06dadb80
//   ch2 0x57 SET_MUSIC               vtable 06db533c  enc LAB_06dadd60  dec LAB_06dadf30
//
// ===========================================================================
// ch2 0x55 AND ch2 0x57 ARE NOT THE SAME BODY.
//
// They measure 207 / 207 encode and 166 / 166 decode, which is exactly the fingerprint
// that would let a careless check call them shared. Diffing the encoders position by
// position leaves nineteen differing positions; three are CALL displacements and the
// other sixteen are real, and they are not a permutation of one field list but two
// different lists:
//
//    76  06dada61 ADD EDX,0x2                   06dade11 ADD EDX,0x4
//    92  06dada81 MOV CX,word ptr [EDI + 0x28]  06dade31 MOV ECX,dword ptr [EDI + 0x28]
//   114  06dadab2 MOV DX,word ptr [EDI + 0x2a]  06dade60 MOV EDX,dword ptr [EDI + 0x2c]
//   136  06dadae3 FLD float ptr [EDI + 0x2c]    06dade8d MOV CL,byte ptr [EDI + 0x30]
//   158  06dadb11 MOV CL,byte ptr [EDI + 0x30]  06dadeba FLD float ptr [EDI + 0x34]
//
// ch2 0x55 is { dword, dword, WORD, WORD, float, byte } == 17 wire bytes; ch2 0x57 is
// { dword, dword, dword, dword, byte, float } == 21. Different widths AND a different
// order for the float and the byte. The decoders differ in 25 positions the same way.
// ===========================================================================

#include "rpc_containers.h"
#include "rpc_payloads.h"

#include <new>
#include <cstring>
#include <cstdlib>

// ===========================================================================
// ch1 0x05 FLPACKET_COMMON_CHATMSG -- LAB_06da6420 (174) / LAB_06da65d0 (154).
//
// { dword Size, Size bytes, dword Field_0x00, dword Field_0x04 } -- the block goes
// FIRST and the two scalars follow it, which is the reverse of the struct order.
// Neither side guards the zero-length case: 06da6495 writes the size and 06da64a7 adds
// it to the cursor with no TEST in between, and the decoder calls operator new(Size)
// unconditionally at 06da6643. That is the same shape RpcPutSizedBlock / RpcGetSizedBlock
// already model, so those are what is used here.
// ===========================================================================
unsigned int RpcEncodeCommonChatMsg(FPacketBase& Packet, unsigned char** BufferHolder,
                                    unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadCommonChatMsg* Wire =
        reinterpret_cast<const FPayloadCommonChatMsg*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06da6451
    if (!RpcPutSizedBlock(Stream, Wire->Size, Wire->Buffer)) {            // 06da6495/06da64de
        return Stream.GetError();
    }
    Stream.PutDword(Wire->Field_0x00);                                    // 06da651e
    Stream.PutDword(Wire->Field_0x04);                                    // 06da6554
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06da658a
    return 0;
}

unsigned int RpcDecodeCommonChatMsg(FPacketBase& Packet, const unsigned char* Buffer,
                                    unsigned int Size, unsigned int* OutLength) {
    FPayloadCommonChatMsg* Wire =
        reinterpret_cast<FPayloadCommonChatMsg*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    if (!RpcGetSizedBlock(Stream, Wire->Size, Wire->Buffer)) {            // 06da6636/06da6686
        return Stream.Failed() ? Stream.GetError()
                               : FRpcMarshalStream::kDecodeOverrun;
    }
    Stream.GetDword(Wire->Field_0x00);                                    // 06da66c7
    Stream.GetDword(Wire->Field_0x04);                                    // 06da66fa
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06da6720
    return 0;
}

// ===========================================================================
// ch1 0x1b FLPACKET_COMMON_POP_UP_DIALOG -- LAB_06da5de0 (206) / LAB_06da5fe0 (101).
//
// The decoder is two RpcGetSizedBlock calls plus a dword: 06da6028 and 06da6033 both
// `CALL FUN_06da1620`, on packet+0x20 and packet+0x28, then 06da605c stores the dword
// at packet+0x30.
//
// THE ENCODER DOES NOT MATCH ITS OWN DECODER, and this is a retail defect, not a
// mis-reading. Full operand text of the four instructions that settle it:
//
//   .text:06da5ede  8b4d28   MOV ECX,dword ptr [EBP + 0x28]   ; the LENGTH it writes = SizeB
//   .text:06da5ee8  8b4520   MOV EAX,dword ptr [EBP + 0x20]   ; the cursor reservation = SizeA
//   .text:06da5f14  8b4d20   MOV ECX,dword ptr [EBP + 0x20]   ; the REP MOVS count     = SizeA
//   .text:06da5f19  8b752c   MOV ESI,dword ptr [EBP + 0x2c]   ; the source             = BufferB
//
// So the second block announces SizeB on the wire and then emits SizeA bytes out of
// BufferB. The first block (06da5e55 / 06da5e8b / 06da5e90) uses SizeA consistently, so
// this is specific to the second one. The two sizes are independent -- they are two
// separate RpcFlattenFmtStr results, see MarshalPopUpDialog in rpc_payload_ch1.cpp --
// so they differ in practice and the stream desynchronises whenever they do.
//
// Reproduced whole: the announced length is SizeB and the emitted bytes are SizeA of
// BufferB, so when SizeA > SizeB the copy reads past the end of BufferB, as retail's
// REP MOVS does.
// ===========================================================================
unsigned int RpcEncodeCommonPopUpDialog(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadCommonPopUpDialog* Wire =
        reinterpret_cast<const FPayloadCommonPopUpDialog*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06da5e11
    // First pair, consistent: length SizeA, SizeA bytes of BufferA.
    if (!RpcPutSizedBlock(Stream, Wire->SizeA, Wire->BufferA)) {          // 06da5e55/06da5e9e
        return Stream.GetError();
    }
    // Second pair, retail's mismatch.
    if (!Stream.PutDword(Wire->SizeB)) {                                  // 06da5ede
        return Stream.GetError();
    }
    // 06da5ee8 reserves SizeA, 06da5f14 / 06da5f19 copy SizeA bytes out of BufferB.
    if (!Stream.PutBytes(Wire->BufferB, Wire->SizeA)) {                   // 06da5f29
        return Stream.GetError();
    }
    Stream.PutDword(Wire->Field_0x10);                                    // 06da5f66
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06da5f9c
    return 0;
}

unsigned int RpcDecodeCommonPopUpDialog(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength) {
    FPayloadCommonPopUpDialog* Wire =
        reinterpret_cast<FPayloadCommonPopUpDialog*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    if (!RpcGetSizedBlock(Stream, Wire->SizeA, Wire->BufferA)) {          // 06da6028
        return Stream.Failed() ? Stream.GetError()
                               : FRpcMarshalStream::kDecodeOverrun;
    }
    if (!RpcGetSizedBlock(Stream, Wire->SizeB, Wire->BufferB)) {          // 06da6033
        return Stream.Failed() ? Stream.GetError()
                               : FRpcMarshalStream::kDecodeOverrun;
    }
    Stream.GetDword(Wire->Field_0x10);                                    // 06da605c
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06da608b
    return 0;
}

// ===========================================================================
// ch2 0x07 FLPACKET_SERVER_LAUNCH -- LAB_06daab40 (213) / LAB_06daad40 (159).
//
// dword, dword, dword, Vector, Quaternion. The two aggregates do NOT go through
// PutBytes: 06daac6e `CALL FUN_06d8f510` moves three dwords and 06daaccc
// `CALL FUN_06d8f4f0` moves four, in index order, and the cursor is reserved 0xc then
// 0x10 (06daac2f, 06daac7d). Same bytes, so the field list below is the model.
//
// FUN_06d8f4f0 was the last unported helper this family needed. Its whole body is
// 06d8f4f0..06d8f50a: four `MOV EDX,dword ptr [EAX + n]` / `MOV dword ptr [ECX + n],EDX`
// pairs at offsets 0, 4, 8, 0xc and `RET 0x4`. FUN_06d8f510 is the same with three
// pairs. FUN_06d91160 (06d91160..06d91176) is a THIRD copy of the three-dword one --
// same effect, different address, not an ICF fold.
// ===========================================================================
unsigned int RpcEncodeServerLaunch(FPacketBase& Packet, unsigned char** BufferHolder,
                                   unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerLaunch* Wire =
        reinterpret_cast<const FPayloadServerLaunch*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06daab74
    Stream.PutDword(Wire->Field_0x00);                                    // 06daabbc
    Stream.PutDword(Wire->Field_0x04);                                    // 06daabee
    Stream.PutDword(Wire->Field_0x08);                                    // 06daac20
    Stream.PutFloat(Wire->Position.x);                                    // 06daac50/06daac6e
    Stream.PutFloat(Wire->Position.y);
    Stream.PutFloat(Wire->Position.z);
    Stream.PutFloat(Wire->Orientation.w);                                 // 06daaca7/06daaccc
    Stream.PutFloat(Wire->Orientation.x);
    Stream.PutFloat(Wire->Orientation.y);
    Stream.PutFloat(Wire->Orientation.z);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06daad05
    return 0;
}

unsigned int RpcDecodeServerLaunch(FPacketBase& Packet, const unsigned char* Buffer,
                                   unsigned int Size, unsigned int* OutLength) {
    FPayloadServerLaunch* Wire =
        reinterpret_cast<FPayloadServerLaunch*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->Field_0x00);                                    // 06daadb0
    Stream.GetDword(Wire->Field_0x04);                                    // 06daadda
    Stream.GetDword(Wire->Field_0x08);                                    // 06daae04
    // 06daae2b / 06daae59 copy the two aggregates straight in, dword by dword.
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.x));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.y));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.z));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.w));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.x));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.y));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.z));
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06daae90
    return 0;
}

// ===========================================================================
// ch2 0x22 FLPACKET_SERVER_SYSTEM_SWITCH_IN -- LAB_06da9260 (190) / LAB_06da9430 (143).
// The same shape as LAUNCH minus one leading dword.
// ===========================================================================
unsigned int RpcEncodeServerSystemSwitchIn(FPacketBase& Packet, unsigned char** BufferHolder,
                                           unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerSystemSwitchIn* Wire =
        reinterpret_cast<const FPayloadServerSystemSwitchIn*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06da9294
    Stream.PutDword(Wire->Field_0x00);                                    // 06da92dc
    Stream.PutDword(Wire->Field_0x04);                                    // 06da930e
    Stream.PutFloat(Wire->Position.x);                                    // 06da933e/06da935c
    Stream.PutFloat(Wire->Position.y);
    Stream.PutFloat(Wire->Position.z);
    Stream.PutFloat(Wire->Orientation.w);                                 // 06da9395/06da93ba
    Stream.PutFloat(Wire->Orientation.x);
    Stream.PutFloat(Wire->Orientation.y);
    Stream.PutFloat(Wire->Orientation.z);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06da93f3
    return 0;
}

unsigned int RpcDecodeServerSystemSwitchIn(FPacketBase& Packet, const unsigned char* Buffer,
                                           unsigned int Size, unsigned int* OutLength) {
    FPayloadServerSystemSwitchIn* Wire =
        reinterpret_cast<FPayloadServerSystemSwitchIn*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->Field_0x00);                                    // 06da94a0
    Stream.GetDword(Wire->Field_0x04);                                    // 06da94ca
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.x));  // 06da94f1
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.y));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.z));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.w)); // 06da951f
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.x));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.y));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orientation.z));
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06da9556
    return 0;
}

// ===========================================================================
// ch2 0x2b FLPACKET_SERVER_SETGROUPFEELINGS -- LAB_06d91890 (180) / LAB_06d91a40 (223).
//
// The vtable is 06db3d30 and NOT 06db4fbc: constructor 06d9c540 writes
// .rdata:PTR_FUN_06db4fbc at 06d9c575 (the intermediate base, shared by thirteen types)
// and .rdata:PTR_FUN_06db3d30 at 06d9c5a5, one instruction before 06d9c5ab
// `MOV dword ptr [ESI + 0xc],0x2002b`. Taking the first store would have attached this
// type to LAB_06da13d0 / LAB_06da1470, which is a different family's codec entirely.
//
// THE ENCODER NEVER WRITES THE RELATION COUNT ITS OWN DECODER READS. The encoder's whole
// wire output is 06d91901..06d919cc:
//   06d91903  MOV ECX,dword ptr [EBP + 0x24]   -> dword NameSize
//   06d91910  TEST EAX,EAX / 06d91912 JBE      -> skip the name when NameSize == 0
//   06d91949  MOVSD.REP                        -> NameSize bytes of Name
//   06d91960  CMP word ptr [EBP + 0x30],0x0    -> the list size, TESTED
//   06d91994  FLD float ptr [EDX + 0x4]        -> per element, Field_0x04 then
//   06d919c2  MOV EDX,dword ptr [EDX]          -> Field_0x00
// There is no PutWord or PutByte of the count anywhere between 06d91960 and 06d919cc --
// the size at packet+0x30 is only ever compared. The decoder, by contrast, reserves two
// bytes at 06d91b1a and reads a word count at 06d91b3b `MOV CX,word ptr [EAX]`.
//
// So retail's two directions disagree by the count word plus the elements. Reproduced
// exactly rather than repaired: the decoder's own bounds checks stop a bogus count (each
// element costs 8 wire bytes and the count is at most 0xffff), so faithfully reproducing
// it is safe, and "fixing" it would put bytes on the wire retail does not put there.
// Recorded in docs/re/deviations.md.
// ===========================================================================
unsigned int RpcEncodeServerSetGroupFeelings(FPacketBase& Packet, unsigned char** BufferHolder,
                                             unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerSetGroupFeelings* Wire =
        reinterpret_cast<const FPayloadServerSetGroupFeelings*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06d918c1
    if (!Stream.PutDword(Wire->NameSize)) {                               // 06d91903
        return Stream.GetError();
    }
    if (Wire->NameSize != 0) {                                            // 06d91912
        if (!Stream.PutBytes(Wire->Name, Wire->NameSize)) {                // 06d91949
            return Stream.GetError();
        }
    }
    // 06d91960 `CMP word ptr [EBP + 0x30],0x0` -- only the LOW WORD of the size is
    // tested, so a list of exactly 0x10000 elements is treated as empty. Kept.
    // The dummy is dereferenced untested (06d91967 / 06d9196a); a non-zero low word
    // means the list holds nodes, so it was built.
    if ((unsigned short)(Wire->Relations.Size & 0xffffu) != 0) {
        for (const FRpcRelationNode* Node = Wire->Relations.Head->Next;
             Node != Wire->Relations.Head && !Stream.Failed(); Node = Node->Next) {
            const FRpcRelation* Relation = Node->Value;
            if (Relation == nullptr) {
                continue;                                                 // 06d91975
            }
            Stream.PutDword(Relation->Field_0x04);                        // 06d91994 FLD/FSTP
            Stream.PutDword(Relation->Field_0x00);                        // 06d919c2
        }
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06d919ff
    return 0;
}

unsigned int RpcDecodeServerSetGroupFeelings(FPacketBase& Packet, const unsigned char* Buffer,
                                             unsigned int Size, unsigned int* OutLength) {
    FPayloadServerSetGroupFeelings* Wire =
        reinterpret_cast<FPayloadServerSetGroupFeelings*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    if (!Stream.GetDword(Wire->NameSize)) {                               // 06d91aba
        return Stream.GetError();
    }
    // 06d91ac0 operator new(NameSize) -- unconditional, before the byte run's check.
    // Bare: stored untested (06d91ac5).
    Wire->Name = static_cast<char*>(
        ::operator new(Wire->NameSize, std::nothrow));                    // 06d91ac5
    if (!Stream.GetBytes(Wire->Name, Wire->NameSize)) {                   // 06d91afe
        return Stream.GetError();
    }
    unsigned short Count = 0;
    if (!Stream.GetWord(Count)) {                                         // 06d91b3b
        return Stream.GetError();
    }
    for (unsigned short Index = 0; Index < Count; ++Index) {              // 06d91b4a
        // 06d91b5a operator new(8), bare, BEFORE the two reads: a failed bounds check
        // leaves through LAB_06d91c30 without freeing it -- retail leaks the element,
        // and so does the port.
        FRpcRelation* Relation = static_cast<FRpcRelation*>(
            ::operator new(sizeof(FRpcRelation), std::nothrow));
        if (!Stream.GetDword(Relation->Field_0x04)) {                     // 06d91b86
            return Stream.GetError();
        }
        if (!Stream.GetDword(Relation->Field_0x00)) {                     // 06d91bae
            return Stream.GetError();
        }
        // 06d91bc3 operator new(0xc) -- the node; 06d91bfd bumps the size at packet+0x30.
        RpcRelationListLink(&Wire->Relations, Relation);
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06d91c49
    return 0;
}

// ===========================================================================
// ch2 0x32 FLPACKET_SERVER_REQUESTSPACESCRIPT -- LAB_06d986e0 (148) / LAB_06d98850 (136).
//
// { dword Field_0x00, dword NameSize, NameSize bytes }. Field_0x08 / _0x0c / _0x10 /
// _0x14 of the payload never reach the wire -- the encoder's only two field reads are
// 06d98752 `MOV ECX,dword ptr [EBP + 0x20]` and 06d98787 `MOV EDX,dword ptr [EBP + 0x24]`,
// and after the block copy it goes straight to 06d987f2 FUN_06da13c0.
//
// THE DECODER COPIES INTO A POINTER IT DOES NOT ALLOCATE: 06d98911
// `MOV EDI,dword ptr [EBP + 0x38]`, and packet+0x38 is Name, which constructor 06d9db10
// zeroes at 06d9db34. On a freshly constructed packet that is a copy to address zero.
// Retail gets away with it because this codec never runs in the shipped single-player
// path. The port copies into the same pointer, allocation-free, as retail does.
// ===========================================================================
unsigned int RpcEncodeServerRequestSpaceScript(FPacketBase& Packet,
                                               unsigned char** BufferHolder,
                                               unsigned int Capacity,
                                               unsigned int* OutLength) {
    const FPayloadServerRequestSpaceScript* Wire =
        reinterpret_cast<const FPayloadServerRequestSpaceScript*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06d98710
    Stream.PutDword(Wire->Field_0x00);                                    // 06d98752
    if (!Stream.PutDword(Wire->NameSize)) {                               // 06d98787
        return Stream.GetError();
    }
    if (Wire->NameSize != 0) {                                            // 06d98796 JBE
        if (!Stream.PutBytes(Wire->Name, Wire->NameSize)) {                // 06d987d4
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06d98815
    return 0;
}

unsigned int RpcDecodeServerRequestSpaceScript(FPacketBase& Packet,
                                               const unsigned char* Buffer,
                                               unsigned int Size,
                                               unsigned int* OutLength) {
    FPayloadServerRequestSpaceScript* Wire =
        reinterpret_cast<FPayloadServerRequestSpaceScript*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->Field_0x00);                                    // 06d988be
    if (!Stream.GetDword(Wire->NameSize)) {                               // 06d988ea
        return Stream.GetError();
    }
    if (Wire->NameSize != 0) {                                            // 06d988ed
        // 06d988ef..06d98906: the bounds check first; then 06d98911 loads packet+0x38
        // and REP MOVS copies into it untested (06d9891f) -- exactly GetBytes.
        if (!Stream.GetBytes(Wire->Name, Wire->NameSize)) {
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06d98961
    return 0;
}

// ===========================================================================
// ch2 0x34 FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE -- LAB_06dab4b0 (150) /
// LAB_06dab630 (110).
//
// { dword, dword, dword TextSize, TextSize bytes }. The decoder is the clean witness
// that the trailing pair is the FUN_06da1620 primitive: 06dab6c7 `LEA EAX,[EDI + 0x28]`
// then 06dab6d0 `CALL FUN_06da1620` -- the CONTIGUOUS {TextSize, TextBuffer} pair at
// payload+0x08 / +0x0c.
// ===========================================================================
unsigned int RpcEncodeServerReplaceMissionObjective(FPacketBase& Packet,
                                                    unsigned char** BufferHolder,
                                                    unsigned int Capacity,
                                                    unsigned int* OutLength) {
    const FPayloadServerReplaceMissionObjective* Wire =
        reinterpret_cast<const FPayloadServerReplaceMissionObjective*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06dab4e1
    Stream.PutDword(Wire->Field_0x00);                                    // 06dab51e
    Stream.PutDword(Wire->State);                                         // 06dab548
    if (!RpcPutSizedBlock(Stream, Wire->TextSize, Wire->TextBuffer)) {    // 06dab572/06dab5af
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06dab5ef
    return 0;
}

unsigned int RpcDecodeServerReplaceMissionObjective(FPacketBase& Packet,
                                                    const unsigned char* Buffer,
                                                    unsigned int Size,
                                                    unsigned int* OutLength) {
    FPayloadServerReplaceMissionObjective* Wire =
        reinterpret_cast<FPayloadServerReplaceMissionObjective*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->Field_0x00);                                    // 06dab69d
    Stream.GetDword(Wire->State);                                         // 06dab6cd
    if (!RpcGetSizedBlock(Stream, Wire->TextSize, Wire->TextBuffer)) {    // 06dab6d0
        return Stream.Failed() ? Stream.GetError()
                               : FRpcMarshalStream::kDecodeOverrun;
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06dab6fe
    return 0;
}

// ===========================================================================
// ch2 0x4b FLPACKET_SERVER_SET_CAMERA -- LAB_06dab780 (190) / LAB_06dab960 (141).
// Vector, Quaternion, float, float -- no leading scalars at all; the first thing after
// the routing word is the 0xc reservation at 06dab7d4.
// ===========================================================================
unsigned int RpcEncodeServerSetCamera(FPacketBase& Packet, unsigned char** BufferHolder,
                                      unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerSetCamera* Wire =
        reinterpret_cast<const FPayloadServerSetCamera*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06dab7b4
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Position.x));  // 06dab7fc/06dab81a
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Position.y));
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Position.z));
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Orient.w));    // 06dab853/06dab878
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Orient.x));
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Orient.y));
    Stream.PutDword(*reinterpret_cast<const unsigned int*>(&Wire->Orient.z));
    // 06dab8b1 FLD float ptr [EDI + 0x3c] / 06dab8b4 FSTP -- these two DO go through the
    // x87 stack where the seven above are plain dword moves.
    Stream.PutFloat(Wire->Fov);                                           // 06dab8b1
    Stream.PutFloat(Wire->Aspect);                                        // 06dab8ea
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06dab923
    return 0;
}

unsigned int RpcDecodeServerSetCamera(FPacketBase& Packet, const unsigned char* Buffer,
                                      unsigned int Size, unsigned int* OutLength) {
    FPayloadServerSetCamera* Wire =
        reinterpret_cast<FPayloadServerSetCamera*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.x));  // 06dab9cc
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.y));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Position.z));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orient.w));    // 06dab9fe
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orient.x));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orient.y));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Orient.z));
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Fov));         // 06daba31
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Aspect));      // 06daba5e
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06daba8d
    return 0;
}

// ===========================================================================
// ch2 0x4d FLPACKET_SERVER_LOADHINT -- LAB_06d90a50 (134) / LAB_06d90ba0 (140).
//
// { dword Size, dword Kind, Size bytes }. NOT the FUN_06da1620 pair: the two scalars are
// adjacent on the wire and the block comes after BOTH of them (06d90abe, 06d90ae8, then
// 06d90af2 reads the size again for the reservation). The decoder's operator new is at
// 06d90c41 and its argument is the size read first, 06d90c35 `MOV EDX,dword ptr
// [EBP + 0x24]`.
// ===========================================================================
unsigned int RpcEncodeServerLoadHint(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerLoadHint* Wire =
        reinterpret_cast<const FPayloadServerLoadHint*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06d90a81
    Stream.PutDword(Wire->Size);                                          // 06d90abe
    if (!Stream.PutDword(Wire->Kind)) {                                   // 06d90ae8
        return Stream.GetError();
    }
    // NO zero-length guard on either side of this type, checked instruction by
    // instruction: 06d90af2 `MOV ECX,dword ptr [EBP + 0x24]` is followed straight by
    // 06d90af5 `MOV EDI,dword ptr [EBX]` and 06d90afa `ADD EDI,ECX`, with no TEST and no
    // conditional jump in between, and the decoder likewise runs 06d90c41 operator_new
    // into 06d90c49 with nothing between them. An `if (Size != 0)` here would be an
    // invented guard.
    if (!Stream.PutBytes(Wire->Buffer, Wire->Size)) {                      // 06d90b25
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06d90b65
    return 0;
}

unsigned int RpcDecodeServerLoadHint(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength) {
    FPayloadServerLoadHint* Wire =
        reinterpret_cast<FPayloadServerLoadHint*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->Size);                                          // 06d90c0e
    if (!Stream.GetDword(Wire->Kind)) {                                   // 06d90c3c
        return Stream.GetError();
    }
    // 06d90c41 operator new(Size) -- unconditional, before the run's own bounds check.
    Wire->Buffer = ::operator new(Wire->Size, std::nothrow);              // 06d90c46, bare
    if (!Stream.GetBytes(Wire->Buffer, Wire->Size)) {                     // 06d90c7f
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06d90cc0
    return 0;
}

// ===========================================================================
// ch2 0x4e FLPACKET_SERVER_SETDIRECTIVE -- LAB_06d90d10 (176) / LAB_06d90f70 (156).
//
// The whole 0x94-byte payload, verbatim. The encoder reserves 0x94 ONCE at 06d90d6b,
// rebuilds the struct on its own stack frame field by field (06d90d98..06d90e69 plus
// 06d90e6d / 06d90e80 `CALL FUN_06d91160`, the three-dword copier) and then blits the
// temporary out with 06d90ee9 `CALL FUN_06d911b0` -- a 0x94-byte member-by-member
// assignment whose body is 06d911b0..06d912b0.
//
// The decoder is the readable half and it settles the mapping: every field it writes is
// `wire + n` to `packet + 0x20 + n`, from 06d90fdb (`MOV EDX,dword ptr [EDI]` ->
// packet+0x20) through 06d910d1 (`MOV DL,byte ptr [EDI + 0x90]` -> packet+0xb0). No
// permutation anywhere.
//
// TWO RANGES ARE NEVER TOUCHED, and they are the reason this is not one memcpy: the
// three bytes after the byte field at wire+0x08 and the three after the byte field at
// wire+0x90. Retail's stack temporary leaves them as whatever was on the stack, so its
// wire carries stack residue there. Copying them from the packet would be a different
// six bytes; leaving the destination alone is the closer model and it is what the two
// PutGap / GetGap runs below do.
// ===========================================================================
unsigned int RpcEncodeServerSetDirective(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength) {
    const unsigned char* Wire = Packet.Payload();

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06d90d49
    // 06d90d6b reserves 0x94 in one go; the four runs below add up to exactly 0x94.
    Stream.PutBytes(Wire + 0x00, 0x09u);                                  // 06d90d98..06d90d9e
    Stream.PutGap(0x03u);                                                 // wire+0x09..0x0b
    Stream.PutBytes(Wire + 0x0c, 0x85u);                                  // 06d90da5..06d90ee9
    Stream.PutGap(0x03u);                                                 // wire+0x91..0x93
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06d90f2b
    return 0;
}

unsigned int RpcDecodeServerSetDirective(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength) {
    unsigned char* Wire = Packet.Payload();

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    // 06d90fb7 reserves 0x94 once, 06d90fdb..06d910d9 writes the fields.
    Stream.GetBytes(Wire + 0x00, 0x09u);
    Stream.GetGap(0x03u);
    Stream.GetBytes(Wire + 0x0c, 0x85u);
    Stream.GetGap(0x03u);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06d9110f
    return 0;
}

// ===========================================================================
// ch2 0x52 FLPACKET_SERVER_PLAYERLIST -- LAB_06d99990 (205) / LAB_06d99b60 (170).
//
// { dword Kind, dword Field_0x08, byte Field_0x0c, byte N, 2*N bytes } where
//
//     N = (Name == nullptr) ? 0 : (unsigned char)(wcslen(Name) + 1)
//
// and the `+ 1` is applied to AL ALONE -- 06d99a78 `INC AL`, after 06d99a6f's wcslen has
// already been truncated to a byte. So the terminator IS included in the count, and a
// name of 255 wide characters wraps to N == 0 and travels as no name at all.
//
// The decoder uses MSVCRT malloc, not operator new -- 06d99c4c
// `CALL dword ptr [->MSVCRT.DLL::malloc]` -- and its argument is 2*N (06d99c45
// `SHL EDI,0x1`). ~FPacket_FLPACKET_SERVER_PLAYERLIST (rpc_packet_registry.cpp) frees this field with
// ::free, so the pairing is kept.
//
// When N is zero the decoder jumps 06d99c40 straight past the allocation and leaves
// Name exactly as it found it.
// ===========================================================================
unsigned int RpcEncodeServerPlayerList(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerPlayerList* Wire =
        reinterpret_cast<const FPayloadServerPlayerList*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06d999c1
    Stream.PutDword(Wire->Kind);                                          // 06d99a03
    Stream.PutDword(Wire->Field_0x08);                                    // 06d99a32
    if (!Stream.PutByte(Wire->Field_0x0c)) {                              // 06d99a5f
        return Stream.GetError();
    }
    unsigned char Count = 0;                                              // 06d99a7c XOR AL,AL
    if (Wire->Name != nullptr) {                                          // 06d99a6c
        unsigned int Length = 0;                                          // 06d99a6f wcslen
        while (Wire->Name[Length] != 0) {
            ++Length;
        }
        Count = (unsigned char)(((unsigned char)(Length & 0xffu)) + 1u);  // 06d99a78 INC AL
    }
    if (!Stream.PutByte(Count)) {                                         // 06d99a9f
        return Stream.GetError();
    }
    if (Count != 0) {                                                     // 06d99aa6
        // 06d99ab0 `SHL EAX,0x1` -- the reservation and the copy are both 2*Count.
        if (!Stream.PutBytes(Wire->Name, (unsigned int)Count * 2u)) {      // 06d99add
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06d99b20
    return 0;
}

unsigned int RpcDecodeServerPlayerList(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength) {
    FPayloadServerPlayerList* Wire =
        reinterpret_cast<FPayloadServerPlayerList*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->Kind);                                          // 06d99bce
    Stream.GetDword(Wire->Field_0x08);                                    // 06d99bf6
    Stream.GetByte(Wire->Field_0x0c);                                     // 06d99c1a
    unsigned char Count = 0;
    if (!Stream.GetByte(Count)) {                                         // 06d99c39
        return Stream.GetError();
    }
    if (Count != 0) {                                                     // 06d99c40
        const unsigned int Bytes = (unsigned int)Count * 2u;              // 06d99c45 SHL
        // 06d99c4c malloc, stored at once (06d99c52) and never tested.
        Wire->Name = static_cast<wchar_t*>(::malloc(Bytes));              // 06d99c4c
        if (!Stream.GetBytes(Wire->Name, Bytes)) {                        // 06d99c87
            return Stream.GetError();
        }
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06d99cc9
    return 0;
}

// ===========================================================================
// ch2 0x55 FLPACKET_SERVER_OBJECTCARGOUPDATE -- LAB_06dad9b0 (207) / LAB_06dadb80 (166).
// dword, dword, WORD, WORD, float, byte. See the twin note at the head of this file.
// ===========================================================================
unsigned int RpcEncodeServerObjectCargoUpdate(FPacketBase& Packet,
                                              unsigned char** BufferHolder,
                                              unsigned int Capacity,
                                              unsigned int* OutLength) {
    const FPayloadServerObjectCargoUpdate* Wire =
        reinterpret_cast<const FPayloadServerObjectCargoUpdate*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06dad9e1
    Stream.PutDword(Wire->ObjectId);                                      // 06dada23
    Stream.PutDword(Wire->ArchId);                                        // 06dada52
    Stream.PutWord(Wire->EquipId);                                        // 06dada81
    Stream.PutWord(Wire->Count);                                          // 06dadab2
    Stream.PutFloat(Wire->Status);                                        // 06dadae3 FLD/FSTP
    Stream.PutByte(Wire->bMission);                                       // 06dadb11
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06dadb42
    return 0;
}

unsigned int RpcDecodeServerObjectCargoUpdate(FPacketBase& Packet,
                                              const unsigned char* Buffer,
                                              unsigned int Size,
                                              unsigned int* OutLength) {
    FPayloadServerObjectCargoUpdate* Wire =
        reinterpret_cast<FPayloadServerObjectCargoUpdate*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->ObjectId);                                      // 06dadbee
    Stream.GetDword(Wire->ArchId);                                        // 06dadc16
    Stream.GetWord(Wire->EquipId);                                        // 06dadc3f
    Stream.GetWord(Wire->Count);                                          // 06dadc69
    // 06dadc8b reads it as a plain dword, not through the x87 stack.
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Status));      // 06dadc92
    Stream.GetByte(Wire->bMission);                                       // 06dadcb1
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06dadcde
    return 0;
}

// ===========================================================================
// ch2 0x57 FLPACKET_SERVER_SET_MUSIC -- LAB_06dadd60 (207) / LAB_06dadf30 (166).
// dword, dword, dword, dword, byte, float. NOT the same list as ch2 0x55.
// ===========================================================================
unsigned int RpcEncodeServerSetMusic(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerSetMusic* Wire =
        reinterpret_cast<const FPayloadServerSetMusic*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));      // 06dadd91
    Stream.PutDword(Wire->Field_0x00);                                    // 06daddd3
    Stream.PutDword(Wire->Field_0x04);                                    // 06dade02
    Stream.PutDword(Wire->Field_0x08);                                    // 06dade31
    Stream.PutDword(Wire->Field_0x0c);                                    // 06dade60
    Stream.PutByte(Wire->Field_0x10);                                     // 06dade8d
    Stream.PutFloat(Wire->Field_0x14);                                    // 06dadeba FLD/FSTP
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());             // 06dadeed
    return 0;
}

unsigned int RpcDecodeServerSetMusic(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength) {
    FPayloadServerSetMusic* Wire =
        reinterpret_cast<FPayloadServerSetMusic*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.GetDword(Wire->Field_0x00);                                    // 06dadf9e
    Stream.GetDword(Wire->Field_0x04);                                    // 06dadfc6
    Stream.GetDword(Wire->Field_0x08);                                    // 06dadfee
    Stream.GetDword(Wire->Field_0x0c);                                    // 06dae016
    Stream.GetByte(Wire->Field_0x10);                                     // 06dae03a
    Stream.GetDword(*reinterpret_cast<unsigned int*>(&Wire->Field_0x14));  // 06dae05b
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());             // 06dae08a
    return 0;
}
