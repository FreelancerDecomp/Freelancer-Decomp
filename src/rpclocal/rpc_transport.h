// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_transport.h -- the rpclocal RPC transport foundation.
//
// Ground truth for everything below: docs/re/rpclocal.md (waves 1-2, verified
// against EXE/Disasm/rpclocal.dll.txt).
//
// Retail rpclocal.dll is a BIDIRECTIONAL proxy, not a wrapper:
//   * freelancer.exe asks it for IServer  -> IServerImpl serializes each call into a
//     CDAPacket (channel 1 or 3) and pushes it through PushToServer (vtable +0x170);
//   * Server.dll asks it for IClient      -> the .data:Client object serializes each
//     call into a CDAPacket (channel 1 or 2) and hands it to IClient slot 9 (+0x24).
// IServerImpl::Update drains the client-bound queue and IServerImpl::HandlePacket
// (0x06d81170) re-dispatches each packet into the REAL IClient inside freelancer.exe
// (.data:0x06dbbcd8) or the chat client (.data:0x06dbbcd4).
//
// Routing header: the dword at CDAPacket+0x0c.
//   Channel = header >> 16 ; Opcode = header & 0xfff ; payload starts at +0x20.
#pragma once

#include <windows.h>
#include <cstdio>
#include <deque>
#include <iosfwd>
#include <map>
#include <string>

class CDAPacket;
class IClient;
class IChatClient;

// FRpcRefObject, FPacketBase, FPacketFactoryFn, the factory / registry return codes,
// FPacketTypeInfo and FPacketSerializer are shared with remoteserver.dll (its
// IServerImpl allocates packets the same way) and live in src/include/rpc_packet.h.
#include "rpc_packet.h"

// Stores one field of 1, 2 or 4 bytes at a packet-relative Offset, unchecked -- one
// retail constructor store (e.g. 06d9afcd `MOV dword ptr [EAX + 0x24],EDX`).
void RpcPacketStoreField(FPacketBase* Packet, unsigned int Offset, unsigned int Width,
                         unsigned int Value);

// Builds one MSVCP60 std::list embedded in a packet, as the constructor does. Eight
// constructors build theirs INLINE, and the sequence is the same in all eight --
// FLPACKET_SERVER_CHARACTERINFO's:
//     06d9d52b  MOV  AL,byte ptr [ESP + local_11+0x1c]  ; the allocator<T> byte
//     06d9d52f  PUSH 0xc                                ; == 8 + sizeof(T)
//     06d9d535  MOV  byte ptr [ESI + 0x24],AL           ; header+0x00, the allocator
//     06d9d538  CALL MSVCRT.DLL::operator_new
//     06d9d541  MOV  dword ptr [EAX],EAX                ; sentinel->Next = sentinel
//     06d9d543  MOV  dword ptr [EAX + 0x4],EAX          ; sentinel->Prev = sentinel
//     06d9d546  MOV  dword ptr [ESI + 0x2c],EDI         ; header+0x08, Size = 0
//     06d9d549  MOV  dword ptr [ESI + 0x28],EAX         ; header+0x04, Head = sentinel
// CREATESHIP and CREATESOLAR reach the same result through COMMON.DLL's
// std::list<CollisionGroupDesc>::list (06d9cce2) and DamageList::DamageList (06d9ccf3).
// Nothing is tested. The allocator byte is stored as 0: retail copies it out of an
// uninitialised stack slot, and nothing reads it.
void RpcPacketBuildListSentinel(FPacketBase* Packet, unsigned int Offset,
                                unsigned int NodeSize);

// Tears down one list built by RpcPacketBuildListSentinel, as every retail packet
// destructor does it -- e.g. FLPACKET_SERVER_CREATELOOT's FUN_06d9d1f0:
//     06d9d200..06d9d225  per node: Prev->Next = Next ; Next->Prev = Prev ; free ;
//                         --Size   (FUN_06d901f0 and COMMON.DLL's list::erase are the
//                         same thing out of line)
//     06d9d227..06d9d238  free(Head) ; Head = 0 ; Size = 0
// The node values are NOT touched; a destructor that owns them frees them first.
void RpcPacketDestroyEmbeddedList(FPacketBase* Packet, unsigned int Offset);

// ===========================================================================
// FRpcMarshalStream -- the cursor the packet codec slots run on.
//
// Recovered 2026-09-17 from the CREATESHIP/CREATESOLAR codec pair, which is the
// clearest instance: encoder FUN_06da3720, decoder FUN_06da4020, driven from the
// Encode/Decode slots 0x06d97d20 / 0x06d98250 (CREATESHIP) and 0x06dac7c0 /
// 0x06daca80 (CREATESOLAR).
//
// The state is NOT a separate object -- it is the four dwords at packet+0x10..+0x1c
// (FPacketBase::Marshal*). The slot sets them up and then the codec addresses them
// straight off `this`; 0x06d97d20 does it at 06d97d31..06d97d43:
//     LEA EBP,[EBX+0x10] ; [EBP+0x00] = 0 ; [EBP+0x04] = capacity
//                        ; [EBP+0x08] = *bufferPtrArg ; [EBP+0x0c] = 0
//
// Every field then follows one idiom, verified at 06da3733..06da3760 (encode) and
// 06da4033..06da405f (decode):
//     Cursor += Width;
//     if (Cursor > Limit) Error = Code;     // JBE at 06da3742 -- Cursor == Limit is OK
//     if (Error) return Code;               // 06da3747
//     <write *Ptr / read *Ptr>; Ptr += Width;
// The cursor is advanced and checked BEFORE the access, and once Error is set every
// later field short-circuits, so a codec never partially writes past the end.
//
// Error codes are per direction and are the codec's return value too: 0x16 for the
// encoder (06da373d MOV EDI,0x16), 0x17 for the decoder (06da403d MOV EDI,0x17).
//
// This type exists so the per-packet codecs become field lists rather than
// hand-transcribed pointer arithmetic. The codecs themselves are NOT ported yet --
// see the tracker notes on 0x06da3720 / 0x06da4020 / 0x06d949d0 / 0x06d934d0.
// ===========================================================================
class FRpcMarshalStream {
public:
    // Retail's two direction codes, used both as the sticky error and as the value the
    // codec returns to its caller.
    static const unsigned int kEncodeOverflow = 0x16;
    static const unsigned int kDecodeOverrun = 0x17;

    // The quantizers, decoded from .rdata rather than guessed. Encode multiplies by the
    // scale; decode multiplies by a SEPARATE precomputed reciprocal constant rather than
    // dividing, so both directions are reproduced with the constant retail actually uses.
    //   DAT_06db3fd8 = 00 00 00 00 00 c0 5f 40 = 127.0                (06da3992 FMUL)
    //   DAT_06db3fe0 = 08 04 02 81 40 20 80 3f = 0.007874015748031496 (06da4301 FMUL)
    //   DAT_06db60f0 = 00 00 00 00 00 e0 6f 40 = 255.0                (06da3c9b FMUL)
    //   DAT_06db60f8 = 10 10 10 10 10 10 70 3f = 0.00392156862745098  (06da4387 FMUL)
    // The two pairs are exact reciprocals (1/127 and 1/255).
    static const double kOrientationScale;      // 127.0
    static const double kOrientationInvScale;   // 1/127
    static const double kPercentScale;          // 255.0
    static const double kPercentInvScale;       // 1/255
    // A third tier, found 2026-09-17 on ch2 0x4f SENDCOMM: retail chains TWO multiplies
    // before the ftol -- 06dad26d `FMUL float ptr [.rdata:DAT_06db6554]` (the float
    // 0.1f, bytes cd cc cc 3d) then 06dad278 `FMUL double ptr [.rdata:DAT_06db60f0]`
    // (255.0) -- so the field covers [0,10] in 255 steps rather than [0,1]. The decoder
    // undoes it with a SINGLE constant, DAT_06db6558 = 14 14 14 14 14 14 a4 3f =
    // 0.0392156862745098 == 1/25.5 (06dad614). Note this is ten times kPercentInvScale
    // and a different .rdata slot, so the two are not interchangeable.
    static const double kTenthScale;            // 0.1f widened to double
    static const double kTenthInvScale;         // 1/25.5

    // Mirrors the slot prologue. Buffer/Capacity are the codec's own two arguments.
    FRpcMarshalStream(FPacketBase& InPacket, void* InBuffer, unsigned int InCapacity);

    unsigned int GetError() const { return Packet.MarshalError; }
    bool Failed() const { return Packet.MarshalError != 0; }
    // Bytes emitted or consumed so far -- what the slot reports back through its
    // out-parameter.
    unsigned int Length() const { return Packet.MarshalCursor; }

    // --- encode side, sticky error kEncodeOverflow ---
    bool PutDword(unsigned int Value);
    bool PutWord(unsigned short Value);
    bool PutByte(unsigned char Value);
    bool PutFloat(float Value);
    bool PutBytes(const void* Source, unsigned int Count);
    // Advances over Count bytes without writing them (a hole retail leaves unwritten).
    bool PutGap(unsigned int Count);
    // The two header bytes written WITHOUT a bounds check, cursor forced to 2 and the
    // sticky error cleared.
    //
    // CORRECTED 2026-09-17, AND THE OLD NOTE HERE HAD IT BACKWARDS. It claimed the
    // missing capacity test was an INLINING ARTEFACT of LAB_06da13d0 and that "every
    // OTHER encoder calls FUN_06da1380 and honours its return value". FUN_06da1380 has
    // no capacity test to lose. Its whole body is 06da1380..06da13b2 -- nineteen
    // instructions, recounted 2026-09-17; the note said twenty -- and it contains not
    // one CMP, TEST or conditional jump:
    //     06da1383  MOV DX,word ptr [this + 0xc]        ; the opcode
    //     06da1389  MOV BH,byte ptr [this + 0xe]        ; the channel
    //     06da1395  MOV EAX,dword ptr [ESI]             ; ESI is arg1 == &Ptr
    //     06da139d  ADD EAX,0x2
    //     06da13a2  MOV word ptr [EAX + -0x2],DX        ; written unconditionally
    //     06da13a6  MOV dword ptr [this],0x2            ; `this` here is arg3 == &Cursor
    //     06da13ac  MOV dword ptr [ESI],EAX
    //     06da13af  XOR EAX,EAX                         ; the ONLY return value
    // Its second argument -- the Limit every caller dutifully passes -- is never read.
    // So retail's header write cannot fail ANYWHERE, the `if (result) return 0x16` branch
    // in every encoder is unreachable, and the cursor is SET to 2 rather than advanced
    // by 2. PutHeaderUnchecked is the faithful model for every encoder, not just for the
    // inlined one.
    //
    // FINISHED 2026-09-17: all 48 header sites in the port now call this instead of
    // PutWord, so the extra bounds check and the unreachable early-return are gone. Two
    // further facts were settled on the listing before the change:
    //
    //   * The call site, not the callee, is what clears the sticky error. Every one of
    //     the 74 `CALL FUN_06da1380` sites is followed by the same three instructions --
    //     e.g. at ch1 0x14 JETTISONCARGO:
    //         06dae655  TEST EAX,EAX
    //         06dae657  MOV dword ptr [EDI + 0x1c],EAX     ; Error := 0, unconditionally
    //         06dae65a  JZ LAB_06dae667                    ; always taken
    //         06dae65e  MOV EAX,0x16                       ; unreachable
    //     Two sites reorder the store ahead of the TEST and invert the jump, with the
    //     same effect -- 06da4a19 `MOV dword ptr [EDI + 0x1c],EAX` / 06da4a1c
    //     `TEST EAX,EAX` / 06da4a1e `MOV EAX,0x16` / 06da4a23 `JNZ LAB_06da4c87`, and
    //     06dad036 likewise. Two more test against a register that already holds zero
    //     rather than against itself -- 06d91f01 `CMP EAX,EDI` and 06da7060
    //     `CMP EAX,EBX`. So `Packet.MarshalError = 0` below is a real retail store, not
    //     merely the redundant one at 06da1415 inside the inlined LAB_06da13d0.
    //
    //   * Every one of the 48 sites is the FIRST write on its stream -- checked
    //     mechanically, each is immediately preceded by the FRpcMarshalStream
    //     constructor with only comments between. That is what makes "set cursor to 2"
    //     and "advance cursor by 2" interchangeable here; no codec calls the header
    //     writer mid-stream, so nothing else had to be left alone.
    //
    // What this deliberately reproduces: with a Capacity below 2, retail writes the two
    // routing bytes past the end of the buffer and reports success. Recorded in
    // docs/re/deviations.md.
    void PutHeaderUnchecked(unsigned short Value);
    // ftol(Value * Scale) truncated into one byte, as at 06da398f..06da39ac. The CRT
    // ftol truncates toward zero, which is a plain cast to long.
    bool PutQuantized(float Value, double Scale);
    // The same operation into TWO bytes. Retail's only user so far is the middle
    // precision tier of ch1 0x02 FIREWEAPON: 06da5208 `FLD [EDI+0x24] ; FMUL
    // DAT_06db6120 ; FMUL DAT_06db6118 ; ftol` then 06da524a `MOV word ptr [ECX],AX`.
    bool PutQuantizedWord(float Value, double Scale);
    // ftol(Value * Scale1 * Scale2) into one byte. Kept apart from PutQuantized instead
    // of folding the two constants into one product: MSVC leaves the x87 control word at
    // 53-bit precision, so each FMUL rounds to double and the chain really is two
    // multiplies. Retail's only user is the SENDCOMM tier described above.
    bool PutQuantizedTwoStage(float Value, double Scale1, double Scale2);

    // --- decode side, sticky error kDecodeOverrun ---
    // Out-parameters are references and are NEVER tested for null: testing a T& tests
    // the value, which is the guard-by-reference defect. They are left untouched when
    // the stream has already failed.
    bool GetDword(unsigned int& OutValue);
    bool GetWord(unsigned short& OutValue);
    bool GetByte(unsigned char& OutValue);
    bool GetFloat(float& OutValue);
    bool GetBytes(void* Dest, unsigned int Count);
    // Consumes Count bytes without storing them.
    bool GetGap(unsigned int Count);
    // InvScale is the reciprocal constant (kOrientationInvScale / kPercentInvScale), not
    // the forward scale -- retail multiplies by it instead of dividing.
    //
    // THE TWO DIRECTIONS OF EXTENSION ARE NOT INTERCHANGEABLE, and picking the wrong one
    // is a silent one-bit-per-byte defect. Proven 2026-09-17 on the CREATESOLAR/CREATESHIP
    // decoder FUN_06da4020, which uses both within twenty instructions of each other:
    //   * the orientation quadruple SIGN-extends -- 06da42ed `MOVSX EAX,byte ptr [...]`,
    //     06da42f6 `MOVSX EAX,BL`, 06da4307 `MOVSX EDX,DL`, 06da430a `MOVSX EAX,CL` --
    //     which is what makes the 127.0 scale cover [-1,+1];
    //   * every 255.0-scaled percentage ZERO-extends -- the hull fraction at 06da4370
    //     `XOR EDX,EDX ; MOV DL,BL`, the equipment status at 06da452b `MOVZX ECX,CL` and
    //     the collision-group health at 06da4804 `XOR EAX,EAX ; MOV AL,CL` -- so it
    //     covers [0,+1].
    // The encode side needs no such split: PutQuantized is one truncating ftol either way.
    bool GetQuantizedSigned(float& OutValue, double InvScale);
    bool GetQuantizedUnsigned(float& OutValue, double InvScale);
    // The 16-bit inverse of PutQuantizedWord. Retail sign-extends the word before the
    // FILD -- 06da5744 `MOVSX EAX,CX`, 06da57de `MOVSX ECX,CX`, 06da587b `MOVSX ECX,CX`
    // -- exactly as the 8-bit signed form does, so the word covers [-1,+1] of its range.
    // There is no unsigned 16-bit form in retail; nothing asks for one.
    bool GetQuantizedWordSigned(float& OutValue, double InvScale);

    // --- the cursor and the buffer pointer, moved separately ---
    //
    // Retail keeps them in two different packet fields and nothing forces a codec to
    // move both. Most do, which is why every Put*/Get* above pairs them. ch1 0x12
    // PLAYER_TRADE does not: its trailing dword is ALWAYS written and read, but the
    // cursor bump that accounts for it is conditional on COMMON.DLL::IsMPServer, and
    // the encoder and the decoder disagree about which way round that condition goes
    // (see the derivation in rpc_containers.cpp). Expressing that needs the two halves
    // apart, so they are here rather than being faked with an extra Put/Get.
    //
    // Reserve is the bare advance-and-check; ErrorCode is a parameter because retail's
    // PLAYER_TRADE decoder stores the ENCODER's 0x16 at one of its own bounds checks
    // (06da7541) and that is reproduced, not corrected.
    bool Reserve(unsigned int Width, unsigned int ErrorCode);

    // --- the wide-string pair, FUN_06da14f0 / FUN_06da1580 ---
    //
    // These two are a SEPARATE primitive from RpcPutSizedBlock's {dword size, bytes},
    // and they are the only two call sites of either in the module (06daf465 and
    // 06daf5ff, ch3 0x39 CREATENEWCHAR). Three differences from everything else here:
    //
    //  * the count is a WORD and it is wcslen WITHOUT the terminator -- 06da1526
    //    `MOV word ptr [EBX],AX`;
    //  * the bounds test is `JC`, not `JBE` -- 06da150d, 06da153e, 06da159d, 06da15c0 --
    //    so Cursor == Limit is an ERROR here where it is in bounds everywhere else, and
    //    a string that exactly fills the buffer is rejected;
    //  * a failure does NOT set the sticky error. Retail returns 0x16 / 0x17 straight
    //    out of the helper (06da1512, 06da15a2) without touching packet+0x1c, and the
    //    caller's `TEST EAX,EAX` is what stops the codec. So these two return false
    //    with Failed() still false, and the caller must return on false rather than
    //    leaning on the sticky error.
    //
    // GetWideString allocates 2*len+2 with operator new and NUL-terminates (06da15e2,
    // 06da1607); the caller owns the block. When len is zero it yields null AND leaves
    // the buffer pointer un-advanced even though the cursor moved by 2 (06da1612 is
    // inside the block 06da15db skips) -- a retail asymmetry, reproduced.
    bool PutWideString(const wchar_t* Text);
    bool GetWideString(wchar_t*& OutText);
    // Store / load four bytes at the buffer pointer and move it on, leaving the cursor
    // alone. Both refuse to touch memory once the stream has failed, exactly as retail's
    // `if (packet+0x1c) return` gate does.
    bool PutDwordUncounted(unsigned int Value);
    bool GetDwordUncounted(unsigned int& OutValue);

private:
    // The advance-and-check prologue every field shares.
    bool Advance(unsigned int Width, unsigned int ErrorCode);
    // The same, with the `JC` comparison the two wide-string helpers use: the cursor is
    // still advanced, but reaching the limit exactly is already an error.
    bool AdvanceStrict(unsigned int Width, unsigned int ErrorCode);

    // Retail holds NO state beyond the packet's own four dwords, so neither do we --
    // a cached buffer pointer here would be a member retail does not have.
    FPacketBase& Packet;
};

// The two bytes every retail encoder puts first, from FUN_06da1380 (06da1383..06da13a2):
//     header = (word)(*(ushort*)(packet+0x0c) & 0xfff) | (*(uchar*)(packet+0x0e) << 8)
// It is a plain OR of two overlapping ranges, not a bitfield pack -- bits 8..11 come
// from both the opcode and the channel byte. Reproduced exactly.
unsigned short RpcPacketHeaderWord(unsigned int PacketId);

// ===========================================================================
// FPacketWireStats -- the per-packet-type byte counters every codec feeds.
//
// Recovered 2026-09-17 from the raw listing, and it is NOT a guess: retail's own CSV
// header names every column. FUN_06d961b0 (DumpPacketStats) fopen()s its argument and
// writes .rdata:06db3e40, which reads in full (from EXE\rpclocal.dll, .rdata file
// offset 0x33e40, 0x8b bytes):
//
//   "Packet Name,In Count,In Max Size,In Avg Size,In Total Bytes,Exec Time Total,
//    Exec Time Avg,Out Count,Out Max Size,Out Avg Size,Out Total Bytes\n\n"
//
// and then prints them with two fprintf calls whose argument order fixes the offsets:
//   06d9623f..06d96265  "%u,%u,%u,%u,%f,%f,"  <- +0x04, +0x14, +0x0c, +0x1c,
//                                                (double)+0x20, (double)+0x28
//   06d9626b..06d96286  "%u,%u,%u,%u"         <- +0x00, +0x10, +0x08, +0x18
// The 0x30-byte total agrees independently with the zeroed 12-dword temp FUN_06da1730
// copies into a fresh map entry (06da1792 `MOV ECX,0xc` + 06da179f `REP MOVSD`).
//
// Retail keeps these in a std::map<unsigned int, FPacketWireStats> at .data:06dbbf4c,
// reached through FUN_06da16b0(PacketId) -- a five-instruction thunk onto
// FUN_06da1730, which is the map's operator[]: an id it has never seen gets a zeroed
// record inserted. RpcFindWireStats is that pair; the map is GRpcPacketStats.
//
// ExecTimeTotal / ExecTimeAvg are the two doubles. NOTHING IN rpclocal.dll WRITES
// THEM -- all 151 codec tails touch only the six dwords below, and the dump is the
// only other reader of the record. They are carried so the CSV keeps retail's column
// count, and they stay zero, exactly as they do in retail unless some other module
// reaches into the map.
struct FPacketWireStats {
    unsigned int OutCount;        // +0x00  bumped by every encoder tail
    unsigned int InCount;         // +0x04  bumped by every decoder tail
    unsigned int OutAvgSize;      // +0x08  OutTotalBytes / OutCount, INTEGER division
    unsigned int InAvgSize;       // +0x0c  InTotalBytes / InCount
    unsigned int OutMaxSize;      // +0x10
    unsigned int InMaxSize;       // +0x14
    unsigned int OutTotalBytes;   // +0x18
    unsigned int InTotalBytes;    // +0x1c
    double ExecTimeTotal;         // +0x20  never written inside this module
    double ExecTimeAvg;           // +0x28  never written inside this module
};

// The tail every encoder and every decoder runs, transcribed instruction for
// instruction from 06da4c48..06da4c85 (encode) and 06da4edc..06da4f15 (decode).
//
// TWO ASYMMETRIES, both on bytes and both easy to get backwards:
//   * the ENCODER accounts the cursor as it stands, because its own length already
//     includes the two header bytes FUN_06da1380 wrote;
//   * the DECODER adds 2 to everything (06da4ef7 `ADD EDX,0x2` into the total,
//     06da4f01 `ADD EAX,0x2` onto the max before the compare, 06da4f08 `ADD ESI,0x2`
//     into the new max) because its length EXCLUDES the header the dispatcher ate.
//   * the max compare is `if (Length > Max)` written as `CMP ESI,EAX ; JBE skip`, so
//     an equal length does NOT rewrite the max.
// The average is recomputed after each packet as an unsigned integer DIV by the count
// that was just incremented (06da4c80 / 06da4f13), so it truncates. Reproduced.
void RpcRecordEncodedPacket(unsigned int PacketId, unsigned int Length);
void RpcRecordDecodedPacket(unsigned int PacketId, unsigned int Length);

// FUN_06da16b0 -> FUN_06da1730: the record for PacketId, inserted zeroed when missing.
FPacketWireStats* RpcFindWireStats(unsigned int PacketId);
// retail .data:06dbbf4c
extern std::map<unsigned int, FPacketWireStats> GRpcPacketStats;


// FUN_06da2820: builds the CREATESHIP / CREATESOLAR hardpoint-name dictionary
// (.data:06dbbf64 map, .data:06dbbf78 count). Retail's serializer construction calls it
// last (06d95f07); FPacketSerializer's constructor does the same. Defined in
// rpc_containers.cpp next to the table and the two lookups.
void RpcBuildHardpointDictionary();


// Retail FUN_06d95e50, `RET` with no argument: the serializer singleton over
// .data:DAT_06dbbe38. Returns the cached object when there is one (06d95e5e / 06d95e70
// JNZ 06d95f19 -- the use count is NOT incremented), otherwise builds a new one and
// publishes it (06d95f13), null included when operator new failed (06d95e8f JZ ->
// 06d95f0f XOR ESI,ESI). Nothing ever clears DAT_06dbbe38 -- its only xrefs are 06d95e5e
// (R) and 06d95f13 (W) -- so after IServerImpl::Shutdown has released the object the
// cache points at freed memory. Reproduced as is: freelancer.exe FreeLibrary()s the
// local-server module right after Shutdown (src/freelancer/Client/server_connect.cpp,
// ShutdownLocalServer), so the next load starts from a fresh .data.
// Callers: IServerImpl::Startup (06d8f2c2), IClient slot 10 (06d8ef41), both storing the
// result at this+0x08, and IServerImpl::DumpPacketStats / IClient slot 125 (06d8f485).
FPacketSerializer* RpcGetPacketSerializer();

// The three retail registrators, one translation unit of their own
// (src/rpclocal/rpc_packet_registry.cpp), 162 entries in total. __cdecl, one argument;
// 4 when it is null, 0 otherwise.
int RpcRegisterChannel1Types(FPacketSerializer* Serializer);   // retail FUN_06d9ace0
int RpcRegisterChannel2Types(FPacketSerializer* Serializer);   // retail FUN_06d9bc80
int RpcRegisterChannel3Types(FPacketSerializer* Serializer);   // retail FUN_06d9f100

// ===========================================================================
// FPacketQueue / FRpcConnection -- the retail CDAConnection.
//
// RE-MEASURED 2026-09-17 on EXE\Disasm\rpclocal.dll.txt. The earlier description in
// this header ("a 0x30-byte block allocator", "three queues", "the queue links the
// packet through CDAPacket+0x08", "server-bound is +0x04") was wrong on every point.
//
// The CONNECTION is the 0x30-byte object FUN_06d88280 allocates:
//     06d8828c  PUSH 0x30 / CALL operator_new
//     06d8829a..06d882a8  [EAX+0x00..0x10] = 0 ; [EAX+0x14] = 0 (byte)
//   +0x00  FPacketQueue*  server-bound READ  queue   (FUN_06d86580 pops from [conn+0x00],
//                                                     06d86584 `MOV ESI,[param_1]`)
//   +0x04  FPacketQueue*  server-bound WRITE queue   (FUN_06d86150 tries [conn+0x04] first,
//                                                     06d86155 `MOV ESI,[param_1+0x4]`,
//                                                     and falls back to [conn+0x00] at
//                                                     LAB_06d86302)
//   +0x08  FPacketQueue*  client-bound READ  queue   (FUN_06d864a0 pops from [conn+0x08],
//                                                     06d864a4 `MOV ESI,[param_1+0x8]`;
//                                                     IServerImpl::Update reads the same
//                                                     offset at 06d8f40f)
//   +0x0c  FPacketQueue*  client-bound WRITE queue   (FUN_06d85e00 tries [conn+0x0c] first,
//                                                     06d85e05 `MOV ESI,[param_1+0xc]`,
//                                                     falls back to [conn+0x08] at
//                                                     LAB_06d85fb2)
//   +0x10  unsigned int   the Start() mode           (06d85dc4 `MOV [EDI+0x10],ECX`)
//   +0x14  bool           started                    (06d85dc0 `MOV byte [EDI+0x14],0x1`)
//   +0x18  CRITICAL_SECTION                          (06d85db2 `LEA EAX,[EDI+0x18]` ->
//                                                     InitializeCriticalSection; 0x18 bytes,
//                                                     which is what makes the object 0x30)
//
// So there are FOUR queues, not three, and they are two READ/WRITE PAIRS. FUN_06d85b60
// (Start) allocates two of them for mode 1 and all four for mode 2:
//     06d85ba1  TEST AL,0x1 -> two queues: [conn+0x00] (06d85bf2) and [conn+0x08] (06d85c37)
//     06d85c5d  TEST AL,0x2 -> four:       [conn+0x00] (06d85cb2), [conn+0x08] (06d85cff),
//                                          [conn+0x0c] (06d85d4d), [conn+0x04] (06d85d8f)
// and returns 3 when already started (06d85b7b..06d85b86), 1 when the mode bit is neither
// (06d85de3), 2 when an allocation failed (06d85c46), 0 on success (06d85dc9).
// IServerImpl::Connect passes mode 1 (06d8f3a7 `PUSH 0x1`), so in our build the WRITE
// slots stay null and both directions push into the same queue they pop from.
//
// Each QUEUE is a separate operator_new(0x30) object -- and it is not a bespoke block
// allocator, it is a plain MSVC6 std::deque<CDAPacket*> with one leading byte:
//   +0x00  byte      the Start() mode, copied in (06d85bc8 `MOV [ESI],AL`)
//   +0x04  iterator  _First { _First, _Last, _Next, _Map } -- 4 dwords, FUN_06d86950
//                    is its default ctor (four zero stores, 06d86954..06d8695c)
//   +0x14  iterator  _Last
//   +0x24  T**       _Map        (FUN_06d868c0 allocates it, FUN_06d868e0 is _Growmap)
//   +0x28  int       _Mapsize
//   +0x2c  size_t    _Size
// FUN_06d86960 is the deque iterator ctor and settles the block size: it stores
// `*_Map + 0x1000` into _Last (06d8696c `ADD EDX,0x1000`), and push_back starts a fresh
// block in the MIDDLE of it (06d85e88 `ADD EDI,0x800`). FUN_06d8cb70 is _Construct:
// `if (dest) *dest = *src` (06d8cb74..06d8cb7e) -- the pointer is copied INTO the deque
// block, so CDAPacket+0x08 is never touched by the queue.
//
// The port therefore uses std::deque<FPacketBase*> directly. Retail's push/pop take no
// lock at all -- none of FUN_06d85e00, FUN_06d86150, FUN_06d864a0, FUN_06d86580 calls
// EnterCriticalSection -- and neither do ours. The CRITICAL_SECTION at conn+0x18 is only
// ever initialised (Start, 06d85db6) and deleted (FUN_06d857c0, 06d857d0).
// ===========================================================================
class FPacketQueue {
public:
    explicit FPacketQueue(unsigned char InMode);

    void Push(FPacketBase* Packet);     // retail deque::push_back
    FPacketBase* Pop();                 // retail front() + pop_front()
    void DestroyAll();                  // one arm of FUN_06d857c0
    unsigned int Num() const { return static_cast<unsigned int>(Packets.size()); }
    unsigned char GetMode() const { return Mode; }

private:
    unsigned char Mode;                 // +0x00
    std::deque<FPacketBase*> Packets;   // +0x04 .. +0x2f
};

class FRpcConnection {
public:
    // Retail FUN_06d85b60 return codes, used verbatim.
    enum EStartResult {
        kStartOk            = 0,    // 06d85dc9 XOR EAX,EAX
        kStartBadMode       = 1,    // 06d85de3 MOV EAX,0x1
        kStartAllocFailed   = 2,    // 06d85c46 MOV EAX,0x2
        kStartAlreadyOn     = 3     // 06d85b86 MOV EAX,0x3
    };
    // The two mode bits FUN_06d85b60 tests, at 06d85ba1 and 06d85c5d.
    enum EStartMode {
        kModeSingleBuffered = 1,    // two queues; what IServerImpl::Connect passes
        kModeDoubleBuffered = 2     // four queues, separate read and write sides
    };
    // FUN_06d85e00 / FUN_06d86150 return 4 when the target queue pointer is null
    // (06d86136 and 06d86488 both `MOV EAX,0x4`).
    enum EPushResult {
        kPushOk             = 0,
        kPushNoQueue        = 4
    };

    FRpcConnection();                   // the stores FUN_06d88280 makes (06d8829a..06d882a8)
    ~FRpcConnection();                  // retail FUN_06d857c0; `delete` adds the free after it

    unsigned int Start(unsigned int InMode);    // retail FUN_06d85b60
    unsigned int GetMode() const { return Mode; }

    unsigned int PushServerBound(FPacketBase* Packet);  // retail FUN_06d86150
    FPacketBase* PopServerBound();                      // retail FUN_06d86580
    unsigned int PushClientBound(FPacketBase* Packet);  // retail FUN_06d85e00
    FPacketBase* PopClientBound();                      // retail FUN_06d864a0

    unsigned int NumServerBound() const;
    unsigned int NumClientBound() const;

private:
    void DestroyQueue(FPacketQueue* Queue);             // one arm of FUN_06d857c0

    FPacketQueue* ServerBoundRead;      // +0x00
    FPacketQueue* ServerBoundWrite;     // +0x04  null in mode 1
    FPacketQueue* ClientBoundRead;      // +0x08
    FPacketQueue* ClientBoundWrite;     // +0x0c  null in mode 1
    unsigned int  Mode;                 // +0x10
    bool          bStarted;             // +0x14
    // +0x18. Initialised only on Start's success path (06d85db6) and deleted
    // unconditionally by the destructor (06d857d0); FUN_06d88280 leaves it as raw heap and
    // nothing ever enters it. A connection destroyed without a successful Start deletes
    // an uninitialised section -- retail's defect, reproduced: the only destroyer is
    // IServerImpl::Shutdown, and the only object it destroys is the one Connect started.
    CRITICAL_SECTION Lock;
};

// ===========================================================================
// Module-wide transport state (retail keeps the same things in .data)
// ===========================================================================
// The CDAConnection. Retail spreads it over THREE .data slots, not one:
//   .data:DAT_06dbbb5c  FUN_06d88280's own cache. Read at 06d88280, written at 06d882ab
//                       (success) and 06d882b4 (allocation failure).
//   .data:DAT_06dbbd58  the SERVER-side pointer. Only IServerImpl touches it: Connect
//                       stores FUN_06d88280's result at 06d8f3a0, Update loads it at
//                       06d8f409 and 06d8f42e, PushToServer at 06d8f460, Shutdown reads
//                       it at 06d8f300/06d8f337 and clears it at 06d8f357.
//   .data:DAT_06dbbd1c  the CLIENT-side pointer. Only the IClient proxy touches it: slot
//                       16 stores FUN_06d88280's result at 06d8efdf, then reads it at
//                       06d8f026 and 06d8f04c; slot 9 reads it at 06d8f080.
// All three hold the same object because FUN_06d88280 is a singleton factory. The
// port keeps the three slots apart as retail does: GRpcConnection here (DAT_06dbbb5c),
// GServerConnection in rpclocal.cpp (DAT_06dbbd58) and GClientConnection in
// rpc_client_proxy.cpp (DAT_06dbbd1c). The retail defect that follows is reproduced:
// IServerImpl::Shutdown frees the object and clears only DAT_06dbbd58, leaving
// DAT_06dbbb5c and DAT_06dbbd1c dangling, so a second Connect inside the same load of the
// DLL would get the freed pointer back from FUN_06d88280. freelancer.exe never does that:
// it FreeLibrary()s the local-server module right after Shutdown (ShutdownLocalServer,
// src/freelancer/Client/server_connect.cpp).
extern FRpcConnection* GRpcConnection;
// The packet-type registry is NOT a module global: IServerImpl and the IClient proxy each
// hold it at this+0x08 (IServerImpl::Serializer, FLocalClientProxy::Serializer), filled
// from RpcGetPacketSerializer() above.
// retail .data:0x06dbbcd8 / 0x06dbbcd4 -- the REAL client interfaces inside freelancer.exe
extern IClient* GRealClient;
extern IChatClient* GRealChatClient;
// retail .data:0x06dbbbc0 -- the caller's module path, saved by IServerImpl::Startup
extern char GCallerModulePath[260];
// retail .data:0x06dbbd19 -- the "connected" byte. IServerImpl::Connect sets it (06d8f3b4)
// once FUN_06d85b60 started the connection, IServerImpl::DisConnect (06d8f3e5),
// IServerImpl::Shutdown (06d8f365) and IClient slot 17 (06d8f077) clear it, and IClient
// slot 16 -- the pump -- reads it (06d8efcd, 06d8f01d). Defined in rpc_client_proxy.cpp.
extern bool GbRpcConnected;

// Retail FUN_06d88280 exactly: return the singleton, creating it on first call. It does
// NOT start it -- IServerImpl::Connect (06d8f399) starts it with FUN_06d85b60(conn, 1);
// IClient slot 16 (06d8efda) only fetches it, after Connect has set the connected byte.
// Nothing else creates or starts the connection: a packet pushed before Connect is
// dropped by PushToServer (06d8f468) or IClient slot 9 (06d8f088), exactly as in retail.
FRpcConnection* RpcCreateConnection();

// ===========================================================================
// Incoming dispatch -- retail IServerImpl::HandlePacket (0x06d81170)
// ===========================================================================
// Returns true when the packet was routed into a real IClient / IChatClient slot.
bool RpcHandleIncomingPacket(FPacketBase* Packet);

// ===========================================================================
// The IClient proxy rpclocal exports (retail .data:Client, 0x06dbbcdc).
// ===========================================================================
IClient* RpcGetClientProxy();
IChatClient* RpcGetChatClientProxy();
// retail IClient slot 16 (0x06d8efc0): the client-side pump IServerImpl::Update runs.
void RpcClientPump();

