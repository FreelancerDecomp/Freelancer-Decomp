// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_payload_ch2.cpp -- the channel-2 packet types (server -> client), marshalled and
// delivered the way retail does it.
//
// Channel 2 travels in ONE direction only: rpclocal's IClient (which retail Server.dll
// calls) serializes, and IServerImpl::HandlePacket deserializes into the real IClient
// inside freelancer.exe. So unlike channel 1 there is no IServer-side mirror to check a
// layout against -- instead every type here is read from three places:
//
//   1. the sender, the body of the IClient slot in rpclocal.dll.txt;
//   2. the receiver, the matching case of the channel-2 switch inside
//      IServerImpl::HandlePacket (jump table .text:0x06d83b44, EBP = the packet);
//   3. the registry size, payload == operator new() size - 0x20.
//
// Where the sender permutes the argument struct (LAUNCH, CREATEMINE, SETREMOVEITEM), the
// receiver's own permutation is the strict inverse -- that agreement is what promotes the
// layout from "read once" to "checked", and it is noted per codec.
//
// Two retail conventions that channel 1 does not use are reproduced verbatim here; both
// are explained in the channel-2 section of rpc_payloads.h:
//   * the "1" that replaces the session id is passed BY REFERENCE for one family of
//     types, because those slots take uint const&;
//   * four types deliver out of file-static storage instead of a stack temporary.

#include "rpc_payloads.h"
#include "rpc_containers.h"
#include "rpc_iclient.h"
// ch2 0x4d LOADHINT serializes a CostumeHint through its own virtuals, exactly as retail
// does through COMMON.DLL::CostumeHint::{Serialize,UnSerialize,CleanUp}.
#include "common_goods.h"
// CollisionGroupDesc's complete definition (ch2 0x48 SETCOLLISIONGROUPS).
#include "common_archetype.h"
// ch2 0x04/0x42 run the reputation half of their tail through the same COMMON.DLL entry
// points retail imports by name (.rdata:06db3050..06db30ec and 06db305c): EnsureExists,
// SetName, SetAffiliation, SetAttitudeTowardsPlayer, GetName, GetAffiliation,
// AttitudeTowards and SinglePlayer. common_goods.h already pulls common_reputation.h in;
// SinglePlayer comes from common_misc.h.
#include "common_misc.h"
// FMineExplosion, the EXPLODEASTEROIDMINE argument (IClient::SendServerExplodeAsteroidMine).
#include "server_sysdb.h"

#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>

// ---------------------------------------------------------------------------
// Shared helpers -- the two block writers retail uses on this channel.
// ---------------------------------------------------------------------------

// FUN_06d97b00 / FUN_06d90a00 (06d97b0b / 06d90a0b) and the body of FUN_06d97470 /
// FUN_06d989b0: bare operator new(N) -- a size of 0 included -- and REP MOVSD/MOVSB the
// caller's bytes in. The packet owns the block from then on and its destructor frees it.
// FUN_06d97470 / FUN_06d989b0 add `if (len == 0 || src == 0) return` in front
// (06d97474..06d97482, 06d989b4..06d989c2); their callers carry that test themselves.
static void* RpcCh2CopyBlock(const void* Source, unsigned int InSize) {
    unsigned char* Block = static_cast<unsigned char*>(::operator new(InSize, std::nothrow));
    const unsigned char* Src = static_cast<const unsigned char*>(Source);
    for (unsigned int i = 0; i < InSize; ++i) {
        Block[i] = Src[i];
    }
    return Block;
}

// strlen, spelled out the way retail's SCASB.REPNE loop computes it (no null test --
// REPNE SCASB would fault on one).
static unsigned int RpcCh2StringLength(const char* Text) {
    unsigned int Length = 0;
    while (Text[Length] != '\0') {
        ++Length;
    }
    return Length;
}

// A float field moved as its bit pattern -- retail's senders and receivers copy these
// with plain dword MOVs (no FLD/FSTP), so the port copies the bits, never the value.
static unsigned int RpcCh2FloatBits(const float& Value) {
    unsigned int Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

static void RpcCh2SetFloatBits(float& Out, unsigned int Bits) {
    std::memcpy(&Out, &Bits, sizeof(Bits));
}

// The payload itself, packet+0x20 -- what every retail sender writes through, with no size
// test (retail knows each class's size statically). Senders below store field by field, as
// retail does, instead of going through RpcPayloadWrite: that helper writes the whole
// struct, pad bytes included, which retail leaves untouched.
template<typename T>
static T* Ch2Payload(FPacketBase* Packet) {
    return reinterpret_cast<T*>(Packet->Payload());
}

// ===========================================================================
// Generic marshallers. Retail repeats these shapes across dozens of types; the codec
// table below names the sender address of every type that uses each one.
// ===========================================================================

// payload+0x00 = the 2nd argument, by value.
static bool MarshalCh2Dword(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // e.g. 06d883fd MOV EDX,[ESP+0x10] / 06d88401 MOV [ECX+0x20],EDX
    FPayloadServerDword* Wire = Ch2Payload<FPayloadServerDword>(Packet);
    Wire->Field_0x00 = Args[1];
    return true;
}

// payload+0x00 = *(dword*)the 2nd argument. Used where the slot takes a const reference.
static bool MarshalCh2DwordByRef(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8ae69 MOV ECX,[ESP+0x10] / 06d8ae6d MOV EDX,[ECX] / 06d8ae77 MOV [EAX+0x20],EDX
    FPayloadServerDword* Wire = Ch2Payload<FPayloadServerDword>(Packet);
    Wire->Field_0x00 = *reinterpret_cast<const unsigned int*>(Args[1]);
    return true;
}

// payload+0x00 = the low byte of the 2nd argument.
static bool MarshalCh2Byte(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d88b0d MOV DL,byte ptr [ESP+0x10] / 06d88b11 MOV byte ptr [ECX+0x20],DL -- one byte,
    // the three after it are not touched.
    FPayloadServerByte* Wire = Ch2Payload<FPayloadServerByte>(Packet);
    Wire->Field_0x00 = (unsigned char)(Args[1] & 0xffu);
    return true;
}

// payload+0x00 = *(byte*)the 2nd argument. Retail: MOV DL,byte ptr [ECX].
static bool MarshalCh2ByteByRef(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d883b9 MOV ECX,[ESP+0x10] / 06d883bd MOV DL,byte ptr [ECX] / 06d883c7 MOV byte ptr
    // [EAX+0x20],DL -- one byte, the three after it are not touched.
    FPayloadServerByte* Wire = Ch2Payload<FPayloadServerByte>(Packet);
    Wire->Field_0x00 = *reinterpret_cast<const unsigned char*>(Args[1]);
    return true;
}

// payload+0x00 = 2nd argument, payload+0x04 = 3rd argument.
static bool MarshalCh2DwordPair(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d886ed/06d886f1 [+0x20] = param_2 ; 06d886f8/06d886fc [+0x24] = param_3. Some senders
    // (0x06d88930, 0x06d889c0) store the two in the other order; the bytes come out the same.
    FPayloadServerDwordPair* Wire = Ch2Payload<FPayloadServerDwordPair>(Packet);
    Wire->Field_0x00 = Args[1];
    Wire->Field_0x04 = Args[2];
    return true;
}

// FUN_06d97b00: payload+0x00 = size (3rd argument), payload+0x04 = an owned copy of the
// buffer at the 2nd argument.
static bool MarshalCh2Blob(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // FUN_06d97b00(this = packet, src, size): 06d97b0b operator new(size) -> 06d97b14
    // [+0x24]; REP MOVSD/MOVSB copy; 06d97b2e [+0x20] = size.
    FPayloadServerBlob* Wire = Ch2Payload<FPayloadServerBlob>(Packet);
    Wire->Buffer = RpcCh2CopyBlock(reinterpret_cast<const void*>(Args[1]), Args[2]);
    Wire->Size = Args[2];
    return true;
}

// FUN_06d90a00: the same, fields the other way round.
static bool MarshalCh2BlobReversed(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // FUN_06d90a00: 06d90a0b operator new(size) -> 06d90a14 [+0x20]; copy; 06d90a2e
    // [+0x24] = size.
    FPayloadServerBlobReversed* Wire = Ch2Payload<FPayloadServerBlobReversed>(Packet);
    Wire->Buffer = RpcCh2CopyBlock(reinterpret_cast<const void*>(Args[1]), Args[2]);
    Wire->Size = Args[2];
    return true;
}

// Types whose payload is empty: the sender allocates the packet and pushes it straight at
// slot 9 (FLPACKET_SERVER_REVERT_CAMERA 0x06d8af20, FLPACKET_SERVER_CANCEL_MUSIC
// 0x06d8c6e0). Registry size 0x20, i.e. the bare CDAPacket base.
static bool MarshalCh2Empty(FPacketBase*, const unsigned int*, unsigned int) {
    return true;
}

// ===========================================================================
// Generic delivery shapes.
//
// Retail pushes the literal 1 in place of the session id on every channel-2 case. Where
// the slot takes uint const& it instead builds a stack local holding 1 and pushes its
// ADDRESS; those deliverers are named ...ByRef and keep the local alive across the call,
// exactly as the retail frame does.
// ===========================================================================

// The dword is handed on unchanged; for the slots whose parameter is an int / unsigned long
// that is the same 32 bits, converted explicitly. (SETHULLSTATUS's float is not in this
// family -- DeliverSetHullStatus below moves its bits.)
#define RPC_CH2_DELIVER_DWORD(FnName, SlotCall, ParamType)                                 \
    static bool FnName(const FPacketBase* Packet, IClient* Client, IChatClient*) {          \
        const FPayloadServerDword Wire = RpcPayloadRead<FPayloadServerDword>(Packet, 0);    \
        Client->SlotCall(1u, static_cast<ParamType>(Wire.Field_0x00));                      \
        return true;                                                                        \
    }

#define RPC_CH2_DELIVER_DWORD_BYREF(FnName, SlotCall, ParamType)                           \
    static bool FnName(const FPacketBase* Packet, IClient* Client, IChatClient*) {          \
        const FPayloadServerDword Wire = RpcPayloadRead<FPayloadServerDword>(Packet, 0);    \
        unsigned int One = 1u;                                                              \
        Client->SlotCall(One, static_cast<ParamType>(Wire.Field_0x00));                     \
        return true;                                                                        \
    }

// (1, payload+0x00, payload+0x04)
#define RPC_CH2_DELIVER_PAIR(FnName, SlotCall)                                             \
    static bool FnName(const FPacketBase* Packet, IClient* Client, IChatClient*) {          \
        const FPayloadServerDwordPair Wire =                                                \
            RpcPayloadRead<FPayloadServerDwordPair>(Packet, 0);                             \
        Client->SlotCall(1u, Wire.Field_0x00, Wire.Field_0x04);                             \
        return true;                                                                        \
    }

// (1, payload+0x04, payload+0x00) -- the blob shape, buffer first then length.
#define RPC_CH2_DELIVER_BLOB(FnName, SlotCall)                                             \
    static bool FnName(const FPacketBase* Packet, IClient* Client, IChatClient*) {          \
        const FPayloadServerBlob Wire = RpcPayloadRead<FPayloadServerBlob>(Packet, 0);      \
        Client->SlotCall(1u, static_cast<const unsigned char*>(Wire.Buffer), Wire.Size);    \
        return true;                                                                        \
    }

#define RPC_CH2_DELIVER_BLOB_REVERSED(FnName, SlotCall)                                    \
    static bool FnName(const FPacketBase* Packet, IClient* Client, IChatClient*) {          \
        const FPayloadServerBlobReversed Wire =                                             \
            RpcPayloadRead<FPayloadServerBlobReversed>(Packet, 0);                          \
        Client->SlotCall(1u, static_cast<const unsigned char*>(Wire.Buffer), Wire.Size);    \
        return true;                                                                        \
    }

// ===========================================================================
// ch2 0x02 FLPACKET_SERVER_LOGINRESPONSE
// Sender 0x06d883a0 (IClient+0x030, RET 8): MOV DL,byte ptr [A1]; MOV [pkt+0x20],DL.
// Receiver caseD_2 at 0x06d816ec: copies the byte into .data:0x06dbb904 and calls
// RealClient->vt[+0x30](1, &DAT_06dbb904) -- the address of a STATIC, not of a temporary.
// ===========================================================================
static unsigned char GLoginResponseStatic = 0;   // retail .data:0x06dbb904

static bool DeliverLoginResponse(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerByte Wire = RpcPayloadRead<FPayloadServerByte>(Packet, 0);
    GLoginResponseStatic = Wire.Field_0x00;
    Client->SendServerLoginResponse(1u, GLoginResponseStatic);
    return true;
}

// ===========================================================================
// ch2 0x06 FLPACKET_SERVER_DESTROYOBJECT
// Sender 0x06d89140 (IClient+0x0dc): payload+0x00 = [A1+0x00], payload+0x04 = byte [A1+0x04].
// Receiver caseD_6 at 0x06d81f6a: statics .data:0x06dbb824 / 0x06dbb828 (the one-shot
// atexit registration around them is retail's lazy init of that static pair and has no
// payload meaning), then vt[+0xdc](1, &DAT_06dbb824).
// ===========================================================================
static FServerDestroyObjectInfo GDestroyObjectStatic = { 0, 0, { 0, 0, 0 } };

static bool MarshalDestroyObject(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d89159..06d8916d: dword [+0x20] = src[0] ; byte [+0x24] = byte src[4].
    const FServerDestroyObjectInfo* Info =
        reinterpret_cast<const FServerDestroyObjectInfo*>(Args[1]);
    FPayloadServerDestroyObject* Wire = Ch2Payload<FPayloadServerDestroyObject>(Packet);
    Wire->ObjectId = Info->ObjectId;
    Wire->Field_0x04 = Info->Field_0x04;
    return true;
}

static bool DeliverDestroyObject(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerDestroyObject Wire =
        RpcPayloadRead<FPayloadServerDestroyObject>(Packet, 0);
    GDestroyObjectStatic.ObjectId = Wire.ObjectId;
    // Retail stores a zero-extended dword here, which is the same bytes as the byte plus
    // three cleared padding bytes.
    GDestroyObjectStatic.Field_0x04 = Wire.Field_0x04;
    GDestroyObjectStatic.Pad_0x05[0] = 0;
    GDestroyObjectStatic.Pad_0x05[1] = 0;
    GDestroyObjectStatic.Pad_0x05[2] = 0;
    Client->SendServerDestroyObject(1u, GDestroyObjectStatic);
    return true;
}

// ===========================================================================
// ch2 0x07 FLPACKET_SERVER_LAUNCH
// Sender 0x06d8a080 (IClient+0x0f0), receiver caseD_7 at 0x06d81951.
// ===========================================================================
static bool MarshalLaunch(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FServerLaunchInfo* Info = reinterpret_cast<const FServerLaunchInfo*>(Args[1]);
    FPayloadServerLaunch Wire;
    Wire.Field_0x00 = Info->Field_0x00;
    Wire.Field_0x04 = Info->Field_0x04;
    Wire.Field_0x08 = Info->Field_0x08;
    Wire.Position = Info->Position;
    Wire.Orientation = Info->Orientation;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverLaunch(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerLaunch Wire = RpcPayloadRead<FPayloadServerLaunch>(Packet, 0);
    FServerLaunchInfo Info;
    Info.Field_0x00 = Wire.Field_0x00;
    Info.Field_0x04 = Wire.Field_0x04;
    Info.Field_0x08 = Wire.Field_0x08;
    Info.Orientation = Wire.Orientation;
    Info.Position = Wire.Position;
    Client->SendServerLaunch(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x0a FLPACKET_SERVER_ACTIVATEOBJECT
// Sender 0x06d89770 (IClient+0x0e0), receiver caseD_a at 0x06d823ab (three statics).
// ===========================================================================
static FPayloadServerActivateObject GActivateObjectStatic = { 0, 0, { 0, 0, 0 }, 0 };

static bool MarshalActivateObject(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d89789..06d897a7: dword [+0x20] = src[0] ; dword [+0x28] = src[8] ; byte [+0x24] =
    // byte src[4].
    const FPayloadServerActivateObject* Info =
        reinterpret_cast<const FPayloadServerActivateObject*>(Args[1]);
    FPayloadServerActivateObject* Wire = Ch2Payload<FPayloadServerActivateObject>(Packet);
    Wire->ObjectId = Info->ObjectId;
    Wire->Field_0x08 = Info->Field_0x08;
    Wire->bActivate = Info->bActivate;
    return true;
}

static bool DeliverActivateObject(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerActivateObject Wire =
        RpcPayloadRead<FPayloadServerActivateObject>(Packet, 0);
    GActivateObjectStatic.ObjectId = Wire.ObjectId;
    GActivateObjectStatic.bActivate = Wire.bActivate;
    GActivateObjectStatic.Pad_0x05[0] = 0;
    GActivateObjectStatic.Pad_0x05[1] = 0;
    GActivateObjectStatic.Pad_0x05[2] = 0;
    GActivateObjectStatic.Field_0x08 = Wire.Field_0x08;
    Client->SendServerActivateObject(1u, GActivateObjectStatic);
    return true;
}

// ===========================================================================
// ch2 0x0b FLPACKET_SERVER_LAND
// Sender 0x06d8a990 (IClient+0x0ec), receiver caseD_b at 0x06d8237c.
// ===========================================================================
// The argument is FServerLandInfo {ShipId, DockId, BaseId} (rpc_iclient_args.h); the wire
// keeps the generic three-dword shape, order preserved.
static bool MarshalThreeDwordsByRef(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8a9a9..06d8a9c7: [+0x20] = src[0], [+0x28] = src[8], [+0x24] = src[4].
    const FServerLandInfo* Info = reinterpret_cast<const FServerLandInfo*>(Args[1]);
    FPayloadServerThreeDwords* Wire = Ch2Payload<FPayloadServerThreeDwords>(Packet);
    Wire->Field_0x00 = Info->ShipId;
    Wire->Field_0x08 = Info->BaseId;
    Wire->Field_0x04 = Info->DockId;
    return true;
}

static bool DeliverLand(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerThreeDwords Wire =
        RpcPayloadRead<FPayloadServerThreeDwords>(Packet, 0);
    FServerLandInfo Info;
    Info.ShipId = Wire.Field_0x00;
    Info.DockId = Wire.Field_0x04;
    Info.BaseId = Wire.Field_0x08;
    Client->SendServerLand(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x21 FLPACKET_SERVER_SYSTEM_SWITCH_OUT
// Sender 0x06d8a860 (IClient+0x0e4): two dwords out of the struct at A1.
// Receiver caseD_21 at 0x06d82351: statics .data:0x06dbb82c / 0x06dbb830, then
// vt[+0xe4](1, &DAT_06dbb82c).
// ===========================================================================
// The argument is FServerSystemSwitchOutInfo {ShipId, GateId} (rpc_iclient_args.h); the
// wire keeps the generic dword-pair shape.
static FServerSystemSwitchOutInfo GSystemSwitchOutStatic = { 0, 0 };

static bool MarshalDwordPairByRef(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8a879..06d8a88d: two dwords of *param_2 into +0x20/+0x24.
    const FServerSystemSwitchOutInfo* Info =
        reinterpret_cast<const FServerSystemSwitchOutInfo*>(Args[1]);
    FPayloadServerDwordPair* Wire = Ch2Payload<FPayloadServerDwordPair>(Packet);
    Wire->Field_0x00 = Info->ShipId;
    Wire->Field_0x04 = Info->GateId;
    return true;
}

static bool DeliverSystemSwitchOut(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerDwordPair Wire = RpcPayloadRead<FPayloadServerDwordPair>(Packet, 0);
    GSystemSwitchOutStatic.ShipId = Wire.Field_0x00;
    GSystemSwitchOutStatic.GateId = Wire.Field_0x04;
    Client->SendServerSystemSwitchOut(1u, GSystemSwitchOutStatic);
    return true;
}

// ===========================================================================
// ch2 0x27 FLPACKET_SERVER_REQUESTCREATESHIPRESP
// Sender 0x06d8a130 (IClient+0x0f4), receiver caseD_27 at 0x06d819b1.
// ===========================================================================
static bool MarshalRequestCreateShipResp(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8a14d/06d8a151 [+0x24] = param_3 ; 06d8a158/06d8a15c byte [+0x20] = param_2.
    FPayloadServerRequestCreateShipResp* Wire = Ch2Payload<FPayloadServerRequestCreateShipResp>(Packet);
    Wire->Field_0x04 = Args[2];
    Wire->Field_0x00 = (unsigned char)(Args[1] & 0xffu);
    return true;
}

static bool DeliverRequestCreateShipResp(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerRequestCreateShipResp Wire =
        RpcPayloadRead<FPayloadServerRequestCreateShipResp>(Packet, 0);
    // The receiver pushes the byte zero-extended; the sender only ever stored a bool's 0/1.
    Client->SendServerRequestCreateShipResp(1u, Wire.Field_0x00 != 0, Wire.Field_0x04);
    return true;
}

// ===========================================================================
// ch2 0x2c FLPACKET_SERVER_CREATEMINE
// Sender 0x06d8a180 (IClient+0x0c4), receiver caseD_2c at 0x06d81ac1. The two field
// groups swap places on the wire and swap back on arrival.
// ===========================================================================
// The argument is Server.dll's FServerCreateProjectileInfo (server_createinfo.h, built by
// FUN_06d07c80 -- the same record CREATECOUNTER sends). Every float member moves as its
// bits: the sender and receiver copy all eighteen fields with plain MOVs.
static bool MarshalCreateMine(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FServerCreateProjectileInfo* Info =
        reinterpret_cast<const FServerCreateProjectileInfo*>(Args[1]);
    FPayloadServerCreateMine Wire;
    Wire.Field_0x00 = Info->ObjId;                                    // arg +0x00
    Wire.Field_0x04 = Info->ArchetypeNickname;                        // arg +0x04
    Wire.Field_0x08 = Info->OwnerId;                                  // arg +0x08
    Wire.Field_0x0c = Info->SubObjId;                                 // arg +0x0c, a word
    Wire.Pad_0x0e = 0;
    Wire.Field_0x10 = RpcCh2FloatBits(Info->Orientation.w);           // arg +0x1c
    Wire.Field_0x14 = RpcCh2FloatBits(Info->Orientation.x);           // arg +0x20
    Wire.Field_0x18 = RpcCh2FloatBits(Info->Orientation.y);           // arg +0x24
    Wire.Field_0x1c = RpcCh2FloatBits(Info->Orientation.z);           // arg +0x28
    Wire.Field_0x20 = RpcCh2FloatBits(Info->Position.x);              // arg +0x10
    Wire.Field_0x24 = RpcCh2FloatBits(Info->Position.y);              // arg +0x14
    Wire.Field_0x28 = RpcCh2FloatBits(Info->Position.z);              // arg +0x18
    Wire.Field_0x2c = RpcCh2FloatBits(Info->Velocity.x);              // arg +0x2c
    Wire.Field_0x30 = RpcCh2FloatBits(Info->Velocity.y);              // arg +0x30
    Wire.Field_0x34 = RpcCh2FloatBits(Info->Velocity.z);              // arg +0x34
    Wire.Field_0x38 = RpcCh2FloatBits(Info->Field_0x38.x);            // arg +0x38
    Wire.Field_0x3c = RpcCh2FloatBits(Info->Field_0x38.y);            // arg +0x3c
    Wire.Field_0x40 = RpcCh2FloatBits(Info->Field_0x38.z);            // arg +0x40
    Wire.Field_0x44 = Info->Field_0x44;                               // arg +0x44
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverCreateMine(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCreateMine Wire = RpcPayloadRead<FPayloadServerCreateMine>(Packet, 0);
    FServerCreateProjectileInfo Info;
    Info.ObjId = Wire.Field_0x00;
    Info.ArchetypeNickname = Wire.Field_0x04;
    Info.OwnerId = Wire.Field_0x08;
    Info.SubObjId = Wire.Field_0x0c;
    // Retail never writes this half of the local; we zero it rather than ship the stack
    // garbage retail ships. Documented in docs/re/deviations.md.
    Info.Pad0e = 0;
    RpcCh2SetFloatBits(Info.Position.x, Wire.Field_0x20);
    RpcCh2SetFloatBits(Info.Position.y, Wire.Field_0x24);
    RpcCh2SetFloatBits(Info.Position.z, Wire.Field_0x28);
    RpcCh2SetFloatBits(Info.Orientation.w, Wire.Field_0x10);
    RpcCh2SetFloatBits(Info.Orientation.x, Wire.Field_0x14);
    RpcCh2SetFloatBits(Info.Orientation.y, Wire.Field_0x18);
    RpcCh2SetFloatBits(Info.Orientation.z, Wire.Field_0x1c);
    RpcCh2SetFloatBits(Info.Velocity.x, Wire.Field_0x2c);
    RpcCh2SetFloatBits(Info.Velocity.y, Wire.Field_0x30);
    RpcCh2SetFloatBits(Info.Velocity.z, Wire.Field_0x34);
    RpcCh2SetFloatBits(Info.Field_0x38.x, Wire.Field_0x38);
    RpcCh2SetFloatBits(Info.Field_0x38.y, Wire.Field_0x3c);
    RpcCh2SetFloatBits(Info.Field_0x38.z, Wire.Field_0x40);
    Info.Field_0x44 = Wire.Field_0x44;
    Client->SendServerCreateMine(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x2e FLPACKET_SERVER_SETADDITEM
// Sender 0x06d885c0 (IClient+0x068), receiver caseD_2e at 0x06d819ed.
// ===========================================================================
static bool MarshalSetAddItem(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // Retail reads a real Common.dll EquipDesc through ESI (06d885e2). The port used a
    // layout-identical local mirror only to avoid the include; this TU already includes
    // common_goods.h, so use the real type and get its exported predicates with it.
    const EquipDesc* Desc = reinterpret_cast<const EquipDesc*>(Args[2]);
    FPayloadServerSetAddItem Wire;
    Wire.ArchId = Desc->ArchId;
    Wire.Count = Desc->Count;
    Wire.HardpointSize = 0;
    Wire.Field_0x0c = Args[1];
    Wire.Id = (unsigned int)Desc->Id;
    Wire.Health = Desc->Health;
    Wire.bEquipped = Desc->bEquipped;
    Wire.bMission = (unsigned char)(Desc->bMission ? 1 : 0);
    Wire.Pad_0x1a[0] = 0;
    Wire.Pad_0x1a[1] = 0;
    Wire.Hardpoint = nullptr;

    // 06d8862a: `MOV ECX,ESI ; CALL [->COMMON.DLL::EquipDesc::is_internal] ; TEST AL,AL ;
    // JNZ 06d88652` -- the hardpoint travels only when the item is NOT in the internal
    // cargo bay. This is NOT the same test as "the hardpoint string is empty": an internal
    // item carries a non-empty, interned cargo-bay name, which retail still omits from the
    // wire. The port used to substitute the string test and so would have sent the bay
    // name for every internal item.
    // rpclocal.vcxproj already links Common.lib -- retail rpclocal.dll imports this exact
    // symbol (IAT .rdata:0003826c), so this is retail's own dependency, not a new one.
    if (!Desc->is_internal()) {
        // 06d88636..06d8864c: strlen via REPNE SCASB, then FUN_06d97470(packet, str, len),
        // which itself no-ops when len == 0 (06d97474 TEST EAX,EAX / JBE).
        const unsigned int Length = RpcCh2StringLength(Desc->Hardpoint);
        if (Length != 0) {                                    // 06d97474 JBE
            Wire.Hardpoint = static_cast<char*>(RpcCh2CopyBlock(Desc->Hardpoint, Length + 1u));
            Wire.HardpointSize = Length + 1u;                 // 06d974a8
        }
    }

    // RpcPayloadWrite cannot fail; the former free-and-return-false branch was dead and is
    // not in retail (06d8864c FUN_06d97470, then 06d88652..06d88660 straight on to slot 9).
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverSetAddItem(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetAddItem Wire = RpcPayloadRead<FPayloadServerSetAddItem>(Packet, 0);
    // caseD_2e at 0x06d819ed builds a real Common.dll EquipDesc on the stack and fills it
    // through the exported setters, in exactly this order.
    EquipDesc Desc;                                     // 06d819f1 EquipDesc::EquipDesc
    Desc.set_arch_id(Wire.ArchId);                      // 06d819ff  <- payload+0x00
    Desc.set_id((unsigned short)(Wire.Id & 0xffffu));   // 06d81a10  <- payload+0x10 as a word
    Desc.set_count(Wire.Count);                         // 06d81a1e  <- payload+0x04
    Desc.set_status(Wire.Health);                       // 06d81a2c  <- payload+0x14
    Desc.set_equipped(Wire.bEquipped);             // 06d81a3c  <- payload+0x18
    // 06d81a42/06d81a45: retail stores bMission straight into the struct at +0x18; there
    // is no setter for it.
    Desc.bMission = (Wire.bMission != 0);
    if (Wire.HardpointSize != 0) {                      // 06d81a4e CMP EAX,EBX / JBE
        // 06d81a57: StringAlloc(payload+0x1c, 0) interns the name in Common.dll's string
        // cache so the EquipDesc keeps a permanent pointer, then set_hardpoint takes a
        // CacheString by reference (06d81a73).
        // The `MOV [ESP+0x1cc],EAX` at 06d81a6c lands on the very slot whose address was
        // pushed at 06d81a67 -- the PUSH in between moved ESP down by 4 -- so retail's
        // CacheString really is one pointer, matching our 4-byte CacheString.
        CacheString Interned;
        Interned.Value = StringAlloc(Wire.Hardpoint, false);
        Desc.set_hardpoint(Interned);
    } else {
        Desc.make_internal();                           // 06d81a7f
    }
    Client->SendServerSetAddItem(1u, static_cast<int>(Wire.Field_0x0c), Desc);
    return true;
}

// ===========================================================================
// ch2 0x2f FLPACKET_SERVER_SETREMOVEITEM
// Sender 0x06d88670 (IClient+0x06c), receiver caseD_2f at 0x06d81aa0.
// ===========================================================================
static bool MarshalSetRemoveItem(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8868d/06d88696 [+0x28] = param_2 ; 06d88691 MOVZX EAX,word param_3 -> 06d8869d
    // [+0x20] ; 06d886a4/06d886ac [+0x24] = param_4.
    FPayloadServerSetRemoveItem* Wire = Ch2Payload<FPayloadServerSetRemoveItem>(Packet);
    Wire->Field_0x08 = Args[1];
    Wire->Field_0x00 = Args[2] & 0xffffu;
    Wire->Field_0x04 = Args[3];
    return true;
}

static bool DeliverSetRemoveItem(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetRemoveItem Wire =
        RpcPayloadRead<FPayloadServerSetRemoveItem>(Packet, 0);
    Client->SendServerSetRemoveItem(1u, static_cast<int>(Wire.Field_0x08),
                                    static_cast<unsigned short>(Wire.Field_0x00 & 0xffffu),
                                    static_cast<int>(Wire.Field_0x04));
    return true;
}

// ===========================================================================
// ch2 0x31 FLPACKET_SERVER_EXPLODEASTEROIDMINE
// Sender 0x06d8a360 (IClient+0x0d0), receiver caseD_31 at 0x06d81b6d.
// ===========================================================================
// The argument is Server.dll's FMineExplosion {MineId, Vector Offset} (server_sysdb.h,
// built by MedMine::update); the wire keeps the generic four-dword shape and the three
// floats move as their bits (plain dword MOVs on both sides).
static bool MarshalFourDwordsByRef(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8a379..06d8a3a1: four dwords of *param_2 into +0x20..+0x2c.
    const FMineExplosion* Info = reinterpret_cast<const FMineExplosion*>(Args[1]);
    FPayloadServerFourDwords* Wire = Ch2Payload<FPayloadServerFourDwords>(Packet);
    Wire->Field_0x00 = Info->MineId;
    Wire->Field_0x04 = RpcCh2FloatBits(Info->Offset.x);
    Wire->Field_0x08 = RpcCh2FloatBits(Info->Offset.y);
    Wire->Field_0x0c = RpcCh2FloatBits(Info->Offset.z);
    return true;
}

static bool DeliverExplodeAsteroidMine(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerFourDwords Wire = RpcPayloadRead<FPayloadServerFourDwords>(Packet, 0);
    FMineExplosion Info;
    Info.MineId = Wire.Field_0x00;
    RpcCh2SetFloatBits(Info.Offset.x, Wire.Field_0x04);
    RpcCh2SetFloatBits(Info.Offset.y, Wire.Field_0x08);
    RpcCh2SetFloatBits(Info.Offset.z, Wire.Field_0x0c);
    Client->SendServerExplodeAsteroidMine(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x33 FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE
// Sender 0x06d8ac40 (IClient+0x124), receiver caseD_33 at 0x06d823df. The "1" travels by
// reference here.
// ===========================================================================
static bool MarshalSetMissionObjectiveState(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8ac59..06d8ac79: dword [+0x20] = src[0] ; 06d8ac66 MOVZX ECX,byte [src+4] -> dword
    // [+0x24] ; word [+0x28] = word [src+8]. +0x2a is not touched.
    const FServerMissionObjectiveStateInfo* Info =
        reinterpret_cast<const FServerMissionObjectiveStateInfo*>(Args[1]);
    FPayloadServerSetMissionObjectiveState* Wire =
        Ch2Payload<FPayloadServerSetMissionObjectiveState>(Packet);
    Wire->Field_0x00 = Info->Field_0x00;
    Wire->Field_0x04 = (unsigned int)Info->Field_0x04;
    Wire->Field_0x08 = (unsigned short)(Info->Field_0x08 & 0xffffu);
    return true;
}

static bool DeliverSetMissionObjectiveState(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetMissionObjectiveState Wire =
        RpcPayloadRead<FPayloadServerSetMissionObjectiveState>(Packet, 0);
    FServerMissionObjectiveStateInfo Info;
    Info.Field_0x00 = Wire.Field_0x00;
    Info.Field_0x04 = (unsigned char)(Wire.Field_0x04 & 0xffu);
    Info.Pad_0x05[0] = 0;
    Info.Pad_0x05[1] = 0;
    Info.Pad_0x05[2] = 0;
    Info.Field_0x08 = (unsigned int)Wire.Field_0x08;
    unsigned int One = 1u;
    Client->SendServerSetMissionObjectiveState(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x38 FLPACKET_SERVER_ITEMTRACTORED
// Sender 0x06d88c60 (IClient+0x100), receiver caseD_38 at 0x06d81e3d.
// ===========================================================================
static bool MarshalItemTractored(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d88c79..06d88ca4: dword +0x00, dword +0x04, word +0x08, word +0x0a of *param_2.
    const FPayloadServerItemTractored* Info =
        reinterpret_cast<const FPayloadServerItemTractored*>(Args[1]);
    FPayloadServerItemTractored* Wire = Ch2Payload<FPayloadServerItemTractored>(Packet);
    Wire->Field_0x00 = Info->Field_0x00;
    Wire->Field_0x04 = Info->Field_0x04;
    Wire->Field_0x08 = Info->Field_0x08;
    Wire->Field_0x0a = Info->Field_0x0a;
    return true;
}

static bool DeliverItemTractored(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerItemTractored Wire =
        RpcPayloadRead<FPayloadServerItemTractored>(Packet, 0);
    FPayloadServerItemTractored Info = Wire;
    Client->SendServerItemTractored(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x3c FLPACKET_SERVER_REPAIROBJECT
// Sender 0x06d8bfe0 (IClient+0x150), receiver caseD_3c at 0x06d82b55. The literal 1 here
// is passed by VALUE, unlike its neighbours in the same slot range.
// ===========================================================================
static bool MarshalRepairObject(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8bffd MOVZX ECX,word [src+4] -> 06d8c005 [+0x24] ; 06d8c008/06d8c00e [+0x20] =
    // src[0] ; 06d8c011/06d8c018 [+0x28] = src[8].
    const FServerRepairObjectInfo* Info =
        reinterpret_cast<const FServerRepairObjectInfo*>(Args[1]);
    FPayloadServerRepairObject* Wire = Ch2Payload<FPayloadServerRepairObject>(Packet);
    Wire->Field_0x04 = (unsigned int)Info->Field_0x04;
    Wire->Field_0x00 = Info->Field_0x00;
    Wire->Field_0x08 = Info->Field_0x08;
    return true;
}

static bool DeliverRepairObject(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerRepairObject Wire =
        RpcPayloadRead<FPayloadServerRepairObject>(Packet, 0);
    FServerRepairObjectInfo Info;
    Info.Field_0x00 = Wire.Field_0x00;
    Info.Field_0x04 = (unsigned short)(Wire.Field_0x04 & 0xffffu);
    Info.Pad_0x06 = 0;   // retail leaves this half untouched; see deviations.md
    Info.Field_0x08 = Wire.Field_0x08;
    Client->SendServerRepairObject(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x41 FLPACKET_SERVER_BURNFUSE
// Sender 0x06d8c170 (IClient+0x15c), receiver caseD_41 at 0x06d82c04.
// ===========================================================================
static bool MarshalBurnFuse(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8c189..06d8c1d1: dwords +0x00/+0x04/+0x08, word +0x0c, dwords +0x10/+0x14, byte
    // +0x18 of *param_2. The pad bytes are not touched.
    // The argument is Server.dll's FServerBurnFuseInfo (src/include/rpc_iclient_args.h,
    // built by EqObj::light_fuse / unlight_fuse); the two floats travel bit for bit.
    const FServerBurnFuseInfo* Info = reinterpret_cast<const FServerBurnFuseInfo*>(Args[1]);
    FPayloadServerBurnFuse* Wire = Ch2Payload<FPayloadServerBurnFuse>(Packet);
    Wire->Field_0x00 = Info->SourceId;
    Wire->Field_0x04 = Info->ObjectId;
    Wire->Field_0x08 = Info->FuseId;
    Wire->Field_0x0c = Info->SubObjId;
    std::memcpy(&Wire->Field_0x10, &Info->AtT, sizeof(unsigned int));
    std::memcpy(&Wire->Field_0x14, &Info->Lifetime, sizeof(unsigned int));
    Wire->Field_0x18 = Info->bLit;
    return true;
}

static bool DeliverBurnFuse(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerBurnFuse Wire = RpcPayloadRead<FPayloadServerBurnFuse>(Packet, 0);
    // A straight copy back into the argument struct (caseD_41), the two floats as bits and
    // the pads as the wire carried them.
    static_assert(sizeof(FServerBurnFuseInfo) == sizeof(FPayloadServerBurnFuse),
                  "BURNFUSE argument and payload are the same 0x1c bytes");
    FServerBurnFuseInfo Info;
    Info.SourceId = Wire.Field_0x00;
    Info.ObjectId = Wire.Field_0x04;
    Info.FuseId = Wire.Field_0x08;
    Info.SubObjId = Wire.Field_0x0c;
    Info.Pad_0x0e = Wire.Pad_0x0e;
    RpcCh2SetFloatBits(Info.AtT, Wire.Field_0x10);
    RpcCh2SetFloatBits(Info.Lifetime, Wire.Field_0x14);
    Info.bLit = Wire.Field_0x18;
    Info.Pad_0x19[0] = Wire.Pad_0x19[0];
    Info.Pad_0x19[1] = Wire.Pad_0x19[1];
    Info.Pad_0x19[2] = Wire.Pad_0x19[2];
    unsigned int One = 1u;
    Client->SendServerBurnFuse(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x43 FLPACKET_SERVER_SET_STORY_CUE
// Sender 0x06d8ae50 (IClient+0x168), receiver caseD_43 at 0x06d82541. Both the session id
// and the payload dword travel by reference.
// ===========================================================================
static bool DeliverSetStoryCue(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerDword Wire = RpcPayloadRead<FPayloadServerDword>(Packet, 0);
    FPayloadServerDword Info = Wire;
    unsigned int One = 1u;
    Client->SendServerSetStoryCue(One, Info.Field_0x00);
    return true;
}

// ===========================================================================
// ch2 0x44 FLPACKET_SERVER_REQUEST_RETURNED
// Sender 0x06d8a9e0 (IClient+0x14c), receiver caseD_44 at 0x06d81918.
// ===========================================================================
static bool MarshalRequestReturned(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadServerRequestReturned Wire;
    Wire.Field_0x00 = Args[1];
    Wire.Field_0x04 = (unsigned char)(Args[2] & 0xffu);
    Wire.Field_0x05 = (signed char)(Args[3] & 0xffu);
    Wire.Field_0x06 = (signed char)(Args[4] & 0xffu);
    Wire.Pad_0x07 = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverRequestReturned(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerRequestReturned Wire =
        RpcPayloadRead<FPayloadServerRequestReturned>(Packet, 0);
    unsigned int One = 1u;
    Client->SendServerRequestReturned(One,
                   Wire.Field_0x00,
                   static_cast<int>(Wire.Field_0x04),       // MOVZX
                   static_cast<int>(Wire.Field_0x05),       // MOVSX
                   static_cast<int>(Wire.Field_0x06));      // MOVSX
    return true;
}

// ===========================================================================
// ch2 0x46 FLPACKET_SERVER_MARKOBJ
// Sender 0x06d8ace0 (IClient+0x12c), receiver caseD_46 at 0x06d82420.
// ===========================================================================
static bool MarshalMarkObj(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8acfd/06d8ad01 [+0x20] = param_2 ; 06d8ad08/06d8ad0c byte [+0x24] = param_3.
    FPayloadServerDwordByte* Wire = Ch2Payload<FPayloadServerDwordByte>(Packet);
    Wire->Field_0x00 = Args[1];
    Wire->Field_0x04 = (unsigned char)(Args[2] & 0xffu);
    return true;
}

static bool DeliverMarkObj(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerDwordByte Wire = RpcPayloadRead<FPayloadServerDwordByte>(Packet, 0);
    unsigned int One = 1u;
    Client->SendServerMarkObj(One, Wire.Field_0x00, static_cast<int>(Wire.Field_0x04));
    return true;
}

// ===========================================================================
// ch2 0x47 FLPACKET_SERVER_CFGINTERFACENOTIFICATION
// Sender 0x06d8ad30 (IClient+0x130), receiver caseD_47 at 0x06d8244f.
// ===========================================================================
static bool MarshalCfgInterfaceNotification(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8ad49..06d8ad67: dword +0x00, dword +0x04, byte +0x08 of *param_2.
    const FPayloadServerCfgInterfaceNotification* Info =
        reinterpret_cast<const FPayloadServerCfgInterfaceNotification*>(Args[1]);
    FPayloadServerCfgInterfaceNotification* Wire =
        Ch2Payload<FPayloadServerCfgInterfaceNotification>(Packet);
    Wire->Field_0x00 = Info->Field_0x00;
    Wire->Field_0x04 = Info->Field_0x04;
    Wire->Field_0x08 = Info->Field_0x08;
    return true;
}

static bool DeliverCfgInterfaceNotification(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCfgInterfaceNotification Wire =
        RpcPayloadRead<FPayloadServerCfgInterfaceNotification>(Packet, 0);
    FPayloadServerCfgInterfaceNotification Info;
    Info.Field_0x00 = Wire.Field_0x00;
    Info.Field_0x04 = Wire.Field_0x04;
    Info.Field_0x08 = (unsigned char)((Wire.Field_0x08 != 0) ? 1 : 0);   // retail TEST/SETNZ
    Info.Pad_0x09[0] = 0;
    Info.Pad_0x09[1] = 0;
    Info.Pad_0x09[2] = 0;
    unsigned int One = 1u;
    Client->SendServerCfgInterfaceNotification(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x49 FLPACKET_SERVER_SETHULLSTATUS
// Sender 0x06d88420 (IClient+0x058): FLD float ptr [ESP+0x10]; FSTP float ptr [pkt+0x20]
// -- the 2nd argument slot IS the float, loaded by value, not through a pointer. Receiver
// caseD_49 at 0x06d83032 pushes the raw dword straight back out.
// ===========================================================================
static bool MarshalSetHullStatus(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8843d FLD float ptr [ESP+0x10] / 06d88441 FSTP float ptr [ECX+0x20]. The port copies
    // the float's bit pattern; x87 would additionally quiet a signalling NaN on the way.
    FPayloadServerDword* Wire = Ch2Payload<FPayloadServerDword>(Packet);
    Wire->Field_0x00 = Args[1];
    return true;
}

// ===========================================================================
// ch2 0x4a FLPACKET_SERVER_SETGUIDEDTARGET
// Sender 0x06d8a800 (IClient+0x0d4), receiver caseD_4a at 0x06d83048.
// ===========================================================================
static bool MarshalSetGuidedTarget(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8a81d/06d8a821 [+0x20] = param_2 ; 06d8a828/06d8a82c [+0x24] = param_3 ;
    // 06d8a833/06d8a83c word [+0x28] = param_4.
    FPayloadServerSetGuidedTarget* Wire = Ch2Payload<FPayloadServerSetGuidedTarget>(Packet);
    Wire->Field_0x00 = Args[1];
    Wire->Field_0x04 = Args[2];
    Wire->Field_0x08 = (unsigned short)(Args[3] & 0xffffu);
    return true;
}

static bool DeliverSetGuidedTarget(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetGuidedTarget Wire =
        RpcPayloadRead<FPayloadServerSetGuidedTarget>(Packet, 0);
    Client->SendServerSetGuidedTarget(1u, Wire.Field_0x00, Wire.Field_0x04, Wire.Field_0x08);
    return true;
}

// ===========================================================================
// ch2 0x4b FLPACKET_SERVER_SET_CAMERA
// Sender 0x06d8ae90 (IClient+0x160), receiver caseD_4b at 0x06d82573.
// ===========================================================================
static bool MarshalSetCamera(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FPayloadServerSetCamera* Info =
        reinterpret_cast<const FPayloadServerSetCamera*>(Args[1]);
    FPayloadServerSetCamera Wire = *Info;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverSetCamera(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetCamera Wire = RpcPayloadRead<FPayloadServerSetCamera>(Packet, 0);
    FPayloadServerSetCamera Info = Wire;
    unsigned int One = 1u;
    Client->SendServerSetCamera(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x4c FLPACKET_SERVER_REVERT_CAMERA / 0x58 FLPACKET_SERVER_CANCEL_MUSIC
// Empty payloads. 0x4c takes the "1" by reference (caseD_4c at 0x06d825fd), 0x58 takes it
// by value (caseD_58 at 0x06d813a1).
// ===========================================================================
static bool DeliverRevertCamera(const FPacketBase*, IClient* Client, IChatClient*) {
    unsigned int One = 1u;
    Client->SendServerRevertCamera(One);
    return true;
}

static bool DeliverCancelMusic(const FPacketBase*, IClient* Client, IChatClient*) {
    Client->SendServerCancelMusic(1u);
    return true;
}

// ===========================================================================
// ch2 0x51 FLPACKET_SERVER_USE_ITEM
// Sender 0x06d88cc0 (IClient+0x104), receiver caseD_51 at 0x06d81e77.
// ===========================================================================
static bool MarshalUseItem(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d88cd9..06d88cfa: dword +0x00, word +0x04, word +0x06 of *param_2.
    const FPayloadServerUseItem* Info = reinterpret_cast<const FPayloadServerUseItem*>(Args[1]);
    FPayloadServerUseItem* Wire = Ch2Payload<FPayloadServerUseItem>(Packet);
    Wire->Field_0x00 = Info->Field_0x00;
    Wire->Field_0x04 = Info->Field_0x04;
    Wire->Field_0x06 = Info->Field_0x06;
    return true;
}

static bool DeliverUseItem(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerUseItem Wire = RpcPayloadRead<FPayloadServerUseItem>(Packet, 0);
    FPayloadServerUseItem Info = Wire;
    Client->SendServerUseItem(1u, Info);
    return true;
}

// ===========================================================================
// The generic deliverers, one per slot.
// ===========================================================================

RPC_CH2_DELIVER_DWORD(DeliverSetShipArch, SendServerSetShipArch, unsigned int)                    // 0x23 -> +0x054
RPC_CH2_DELIVER_DWORD(DeliverGfCompleteCharList, SendServerGFCompleteCharList, unsigned int)       // 0x0f -> +0x07c
RPC_CH2_DELIVER_DWORD(DeliverGfCompleteScriptBehaviorList, SendServerGFCompleteScriptBehaviorList, unsigned int)   // 0x11 -> +0x088
RPC_CH2_DELIVER_DWORD(DeliverGfCompleteAmbientScriptList, SendServerGFCompleteAmbientScriptList, unsigned int)     // 0x13 -> +0x094
RPC_CH2_DELIVER_DWORD(DeliverGfCompleteMissionComputerList, SendServerGFCompleteMissionComputerList, unsigned int) // 0x10 -> +0x0a0
RPC_CH2_DELIVER_DWORD(DeliverGfCompleteNewsBroadcastList, SendServerGFCompleteNewsBroadcastList, unsigned int)     // 0x0e -> +0x0b4
RPC_CH2_DELIVER_DWORD(DeliverSendNnMessage, SendServerSendNNMessage, unsigned int)                 // 0x56 -> +0x114
RPC_CH2_DELIVER_DWORD(DeliverMissionSaveA, SendServerMissionSaveA, unsigned long)                  // 0x5b -> +0x128
RPC_CH2_DELIVER_DWORD(DeliverPlaySoundEffect, SendServerPlaySoundEffect, unsigned int)             // 0x59 -> +0x1f0

RPC_CH2_DELIVER_DWORD_BYREF(DeliverSetCash, SendServerSetCash, int)                                // 0x30 -> +0x134
RPC_CH2_DELIVER_DWORD_BYREF(DeliverSetNumKills, SendServerSetNumKills, int)                        // 0x3e -> +0x138
RPC_CH2_DELIVER_DWORD_BYREF(DeliverSetMissionSuccesses, SendServerSetMissionSuccesses, int)        // 0x3f -> +0x13c
RPC_CH2_DELIVER_DWORD_BYREF(DeliverSetMissionFailures, SendServerSetMissionFailures, int)          // 0x40 -> +0x140

RPC_CH2_DELIVER_PAIR(DeliverSetStartRoom, SendServerSetStartRoom)                       // 0x0d -> +0x070
RPC_CH2_DELIVER_PAIR(DeliverGfDestroyCharacter, SendServerGFDestroyCharacter)           // 0x15 -> +0x074
RPC_CH2_DELIVER_PAIR(DeliverGfDestroyScriptBehavior, SendServerGFDestroyScriptBehavior) // 0x17 -> +0x084
RPC_CH2_DELIVER_PAIR(DeliverGfDestroyAmbientScript, SendServerGFDestroyAmbientScript)   // 0x19 -> +0x090
RPC_CH2_DELIVER_PAIR(DeliverGfDestroyMissionComputer, SendServerGFDestroyMissionComputer) // 0x16 -> +0x098
RPC_CH2_DELIVER_PAIR(DeliverGfDestroyNewsBroadcast, SendServerGFDestroyNewsBroadcast)   // 0x14 -> +0x0ac
RPC_CH2_DELIVER_PAIR(DeliverScanNotify, SendServerScanNotify)                           // 0x39 -> +0x1b4

RPC_CH2_DELIVER_BLOB(DeliverGfUpdateChar, SendServerGFUpdateChar)                       // 0x26 -> +0x078
RPC_CH2_DELIVER_BLOB(DeliverGfScriptBehavior, SendServerGFScriptBehavior)               // 0x1a -> +0x080
RPC_CH2_DELIVER_BLOB(DeliverGfUpdateMissionComputer, SendServerGFUpdateMissionComputer) // 0x1d -> +0x09c
RPC_CH2_DELIVER_BLOB(DeliverGfMissionVendorAcceptance, SendServerGFMissionVendorAcceptance) // 0x20 -> +0x0a4
RPC_CH2_DELIVER_BLOB(DeliverGfUpdateNewsBroadcast, SendServerGFUpdateNewsBroadcast)     // 0x1e -> +0x0b0

RPC_CH2_DELIVER_BLOB_REVERSED(DeliverGfUpdateAmbientScript, SendServerGFUpdateAmbientScript) // 0x1f -> +0x08c

// ch2 0x49 FLPACKET_SERVER_SETHULLSTATUS -> +0x058. Receiver caseD_49 at 0x06d83032 pushes
// the payload dword straight back out as the float argument; the bits are moved, not the
// value.
static bool DeliverSetHullStatus(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerDword Wire = RpcPayloadRead<FPayloadServerDword>(Packet, 0);
    float HullStatus = 0.0f;
    RpcCh2SetFloatBits(HullStatus, Wire.Field_0x00);
    Client->SendServerSetHullStatus(1u, HullStatus);
    return true;
}

// ch2 0x5a FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY, sender 0x06d88af0, receiver caseD_5a
// at 0x06d829fa: MOVZX from the payload's first byte.
static bool DeliverGfMissionVendorWhyEmpty(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerByte Wire = RpcPayloadRead<FPayloadServerByte>(Packet, 0);
    Client->SendServerGFMissionVendorWhyEmpty(1u, (unsigned int)Wire.Field_0x00);
    return true;
}

// ===========================================================================
// The four container types of channel 2, closed by the serializer in
// rpc_containers.{h,cpp}.
//
// 0x24 SETEQUIPMENT   sender 0x06d884a0  receiver caseD_24 @0x06d817be  SendServerSetEquipment (+0x060)
// 0x25 SETCARGO       sender 0x06d88530  receiver caseD_25 @0x06d8171f  SendServerSetCargo (+0x064)
// 0x3d REMOTEOBJECTCARGOUPDATE
//                     sender 0x06d8c040  receiver caseD_3d @0x06d82a14  SendServerRemoteObjectCargoUpdate (+0x154)
// 0x48 SETCOLLISIONGROUPS
//                     sender 0x06d88460  receiver caseD_48 @0x06d83012  SendServerSetCollisionGroups (+0x05c)
//
// The first three share one marshaller, retail FUN_06d8c750: walk a
// std::vector<EquipDesc>, build one 0x18 wire record per element, push it onto the
// handle, then hang the handle off the payload with FUN_06d8fa90. They share one
// demarshaller too, FUN_06d95d20 + FUN_06d84d60, which rebuilds the vector. Registry
// agreement: SETEQUIPMENT and SETCARGO are 0x24 bytes (payload 0x04 = the handle
// pointer), REMOTEOBJECTCARGOUPDATE is 0x2c (0x04 handle + a byte at +0x04 + a dword at
// +0x08), SETCOLLISIONGROUPS is 0x2c (payload 0x0c = the list header itself).
//
// The one real difference between the three equipment senders is FUN_06d8c750's third
// argument. SETEQUIPMENT and SETCARGO both pass 1 (06d884d8, 06d88568), so both ship
// the WHOLE vector; only REMOTEOBJECTCARGOUPDATE passes 0 (06d8c08a), which turns on
// the filter at 06d8c770 and drops every element whose bEquipped byte is set. So on
// this channel "cargo" is a matter of which vector the caller hands over, not of the
// flag -- except for 0x3d, where retail does filter.
//
// The IClient slots these four end on take `const EquipDescVector&` (SendServerSetEquipment,
// SendServerSetCargo, SendServerRemoteObjectCargoUpdate) and `const std::list<
// CollisionGroupDesc>&` (SendServerSetCollisionGroups); the vector the port hands over is
// Common's own EquipDescVector (retail std::vector<EquipDesc>, same allocator + three
// pointers), and for 0x48 a std::list rebuilt from the payload's list.
// ===========================================================================

// --- the shared equipment marshaller (retail FUN_06d8c750 + FUN_06d8fa90) ---
// The stack handle is an object with a destructor in retail: the senders zero it, fill
// it, deep-copy it into the packet and run FUN_06d91340 on it on the way out -- and from
// Unwind@06db24f0 (06d884a0 / 06d88530) / Unwind@06db25d0 (06d8c040) on a throw.
static bool MarshalEquipVectorCommon(FPacketBase* Packet, unsigned int VectorArg, bool bAll) {
    const EquipDescVector* Source = reinterpret_cast<const EquipDescVector*>(VectorArg);
    FRpcEquipWireHandle Handle;
    RpcEquipHandleFromVector(&Handle, Source, bAll);
    RpcPacketEquipAssign(Packet, &Handle);
    return true;
}

// --- the shared equipment demarshaller (retail FUN_06d95d20 + FUN_06d84d60) ---
//
// Both temporaries -- the wire handle and the rebuilt vector -- live on the receiver's
// frame across the IClient call and are torn down after it, the HANDLE first: caseD_24/25
// reach 06d81784 `LEA ECX,[ESP+0x14] ; CALL FUN_06d91340` (the handle at local_21c) before
// the COMMON vector _Destroy + FUN_06db1dc2 on the vector at local_218 (06d817a6/06d817b1);
// caseD_3d does the same at 06d82a9b then 06d82ab0. The rebuilt EquipDescs hold interned
// StringAlloc hardpoints (06d84e8a), so nothing in the vector points into the handle.
// Both are objects with destructors, in retail and here: on a throw, Unwind@06db2163 /
// @06db217a / @06db21fe run FUN_06d91340 on the handle ([EBP-0x21c]). Declared vector
// first, handle second, so the handle is destroyed first, as on the normal path.
namespace {

struct FEquipVectorScope {
    EquipDescVector Vector;         // ctor zeroes the four dwords (Common 0x06266310);
                                    // ~EquipDescVector frees the buffer (06d817a6/06d817b1)
    FRpcEquipWireHandle Handle;     // ~FRpcEquipWireHandle = FUN_06d91340 (06d81788)

    explicit FEquipVectorScope(const FPacketBase* Packet) {
        RpcPacketEquipExtract(Packet, &Handle);
        RpcEquipVectorFromHandle(&Vector, &Handle);
    }

private:
    FEquipVectorScope(const FEquipVectorScope&);
    FEquipVectorScope& operator=(const FEquipVectorScope&);
};

}  // namespace

// ch2 0x24 SETEQUIPMENT. Args: [0] the by-value session id retail drops, [1] the vector.
static bool MarshalSetEquipment(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    return MarshalEquipVectorCommon(Packet, Args[1], true);
}

static bool DeliverSetEquipment(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    FEquipVectorScope Scope(Packet);
    Client->SendServerSetEquipment(1u, Scope.Vector);
    return true;
}

// ch2 0x25 SETCARGO -- byte for byte the same sender and receiver, one opcode and one
// IClient slot apart.
static bool MarshalSetCargo(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    return MarshalEquipVectorCommon(Packet, Args[1], true);
}

static bool DeliverSetCargo(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    FEquipVectorScope Scope(Packet);
    Client->SendServerSetCargo(1u, Scope.Vector);
    return true;
}

// ch2 0x3d REMOTEOBJECTCARGOUPDATE. Sender 0x06d8c040, RET 0x10 -- four arguments:
//   Args[0] a POINTER whose first dword retail dereferences for the "1" slot
//           (06d8c0ba `MOV EAX,[ECX]`), i.e. the by-reference session family;
//   Args[1] -> payload+0x08 (06d8c081);
//   Args[2] the std::vector<EquipDesc>, filtered to the unequipped items;
//   Args[3] -> payload+0x04 as a single byte (06d8c076).
// Receiver caseD_3d pushes them back as SendServerRemoteObjectCargoUpdate(&one,
// payload+0x08, &vector, byte).
static bool MarshalRemoteObjectCargoUpdate(FPacketBase* Packet, const unsigned int* Args,
                                           unsigned int ArgCount) {
    // In place, in retail's order: the byte (06d8c072/06d8c076 `MOV [pkt+0x24],DL`), the
    // dword (06d8c07d/06d8c081 `MOV [pkt+0x28],ECX`), then the equipment handle. The three
    // pad bytes at +0x25 are neither written here nor seeded by the constructor
    // (FUN_06d9ece0 stores only the byte at 06d9ecf7), so they are left alone.
    FPayloadServerRemoteObjectCargoUpdate* Wire =
        Ch2Payload<FPayloadServerRemoteObjectCargoUpdate>(Packet);
    Wire->Field_0x04 = (unsigned char)(Args[3] & 0xffu);
    Wire->Field_0x08 = Args[1];
    // bAll is false here and only here: retail passes 0 as FUN_06d8c750's third
    // argument (06d8c084 `XOR EAX,EAX` / 06d8c08a `PUSH EAX`), which keeps only the items
    // whose bEquipped byte is clear.
    return MarshalEquipVectorCommon(Packet, Args[2], false);
}

static bool DeliverRemoteObjectCargoUpdate(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerRemoteObjectCargoUpdate Wire =
        RpcPayloadRead<FPayloadServerRemoteObjectCargoUpdate>(Packet, 0);
    FEquipVectorScope Scope(Packet);
    // retail 06d82a6a: `MOV dword ptr [ESP+local_94],1 ; LEA EAX,[local_94] ; PUSH EAX`
    // -- this type is in the by-reference "1" family.
    unsigned int One = 1u;
    Client->SendServerRemoteObjectCargoUpdate(One, Wire.Field_0x08, Scope.Vector, Wire.Field_0x04);
    return true;
}

// ch2 0x48 SETCOLLISIONGROUPS. Sender 0x06d88460 is three calls long; receiver caseD_48
// is `FUN_06d8f7d0(packet)` -- i.e. `LEA EAX,[packet+0x20]` -- followed by
// `SendServerSetCollisionGroups(1, that address)`. The payload IS the list, so nothing is
// copied either way.
//
// The caller's list is a std::list<CollisionGroupDesc> -- Server.dll's
// PlayerData_SetCollisionGroups passes &PlayerData::collision_groups (06d4f400). Until
// W4-C this sender read that address as the retail-layout FRpcCollisionGroupList and the
// receiver handed the packet's mirror to the exe, which cannot read it as the std::list
// FUN_004c3f30 takes. Both ends now follow the ch3 0x36 REQCOLLISIONGROUPS seam
// (rpc_payload_ch3.cpp): the caller's own iterators into the packet list, and the packet
// list back into a real std::list for the IClient call.
static bool MarshalSetCollisionGroups(FPacketBase* Packet, const unsigned int* Args,
                                      unsigned int ArgCount) {
    const std::list<CollisionGroupDesc>& Source =
        *reinterpret_cast<const std::list<CollisionGroupDesc>*>(Args[1]);
    // FUN_06d8f710(packet, Source): the caller's node chain is walked directly, no
    // intermediate array, into the list the packet constructor built (06d9e909) -- no
    // re-initialisation here. 06d8f712..06d8f71d: `LEA EBP,[this+0x20] ; CMP EBP,param_1 ;
    // JZ` -- self-assignment does nothing; the caller's list is a different type here, so
    // the two can never be the same object (as in MarshalReqCollisionGroups).
    FRpcCollisionGroupList* List = RpcPacketCollisionGroups(Packet);
    RpcCollisionGroupListAssignRange(List, Source.begin(), Source.end());
    return true;
}

static bool DeliverSetCollisionGroups(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    // Retail caseD_48: `LEA EAX,[packet+0x20] ; PUSH EAX ; PUSH 1 ; CALL [ESI+0x5c]`. The
    // packet's list is rebuilt into a real std::list first (RpcCollisionGroupListFromWire,
    // retail's inline copy at 06d8505c), exactly as DeliverReqCollisionGroups does.
    std::list<CollisionGroupDesc> Groups;
    RpcCollisionGroupListFromWire(Groups, RpcPacketCollisionGroups(Packet));
    Client->SendServerSetCollisionGroups(1u, Groups);
    return true;
}

// ===========================================================================
// ch2 0x01 FLPACKET_SERVER_CONNECTRESPONSE. Retail's sender lives inside the pump
// FUN_06d8efc0, not in an IClient slot, and the payload is a literal.
// ===========================================================================
static bool MarshalConnectResponse(FPacketBase* Packet, const unsigned int*, unsigned int) {
    FPayloadServerConnectResponse Wire;
    Wire.Field_0x00 = 1u;   // retail: `MOV dword ptr [ECX + 0x20],0x1`
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverConnectResponse(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerConnectResponse Wire =
        RpcPayloadRead<FPayloadServerConnectResponse>(Packet, 0);
    // The one channel-2 case with no leading 1 -- retail pushes the payload dword alone.
    Client->OnConnect(Wire.Field_0x00);
    return true;
}

// ===========================================================================
// ch2 0x08 FLPACKET_SERVER_CHARSELECTVERIFIED.
// ===========================================================================
static bool MarshalCharSelectVerified(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8a049..06d8a067: [+0x20] = src[0], [+0x28] = src[2], [+0x2c] = src[3]; +0x24 is
    // left alone, as retail leaves it.
    const unsigned int* Source = reinterpret_cast<const unsigned int*>(Args[1]);
    FPayloadServerCharSelectVerified* Wire = Ch2Payload<FPayloadServerCharSelectVerified>(Packet);
    Wire->Field_0x00 = Source[0];
    Wire->Field_0x08 = Source[2];
    Wire->Field_0x0c = Source[3];
    return true;
}

static bool DeliverCharSelectVerified(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCharSelectVerified Wire =
        RpcPayloadRead<FPayloadServerCharSelectVerified>(Packet, 0);
    // The four dwords back into FServerCharSelectVerifiedInfo: +0x00, the hole at +0x04
    // (zeroed, see FPayloadServerCharSelectVerified), and the two halves of the double.
    FServerCharSelectVerifiedInfo Info;
    Info.VibeHandle = static_cast<int>(Wire.Field_0x00);
    Info.Unused_0x04 = 0;
    unsigned char* const TimeBytes = reinterpret_cast<unsigned char*>(&Info.ServerTime);
    std::memcpy(TimeBytes, &Wire.Field_0x08, sizeof(unsigned int));
    std::memcpy(TimeBytes + sizeof(unsigned int), &Wire.Field_0x0c, sizeof(unsigned int));
    Client->SendServerCharSelectVerified(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x22 FLPACKET_SERVER_SYSTEM_SWITCH_IN. Delivered out of a file-static block, the
// way retail does -- .data:0x06dbb91c, not re-entrant there either.
// ===========================================================================
static FPayloadServerSystemSwitchIn GSystemSwitchInStatic_06dbb91c;

static bool MarshalSystemSwitchIn(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    const FPayloadServerSystemSwitchIn* Source =
        reinterpret_cast<const FPayloadServerSystemSwitchIn*>(Args[1]);
    return RpcPayloadWrite(Packet, 0, *Source);
}

static bool DeliverSystemSwitchIn(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    GSystemSwitchInStatic_06dbb91c = RpcPayloadRead<FPayloadServerSystemSwitchIn>(Packet, 0);
    Client->SendServerSystemSwitchIn(1u, GSystemSwitchInStatic_06dbb91c);
    return true;
}

// ===========================================================================
// ch2 0x2a FLPACKET_SERVER_ADJUSTATTITUDE.
// ===========================================================================
static bool MarshalAdjustAttitude(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d88f89..06d88f93 dword [+0x20] = src[0] ; 06d88f96 FLD float [src+4] / 06d88fa1 FSTP
    // float [+0x24]. The float moves as its bit pattern here (x87 would quiet an sNaN).
    const FPayloadServerAdjustAttitude* Source =
        reinterpret_cast<const FPayloadServerAdjustAttitude*>(Args[1]);
    FPayloadServerAdjustAttitude* Wire = Ch2Payload<FPayloadServerAdjustAttitude>(Packet);
    Wire->Field_0x00 = Source->Field_0x00;
    ::memcpy(&Wire->Field_0x04, &Source->Field_0x04, sizeof(float));
    return true;
}

static bool DeliverAdjustAttitude(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    FPayloadServerAdjustAttitude Local = RpcPayloadRead<FPayloadServerAdjustAttitude>(Packet, 0);
    Client->SendServerAdjustAttitude(1u, Local);
    return true;
}

// ===========================================================================
// ch2 0x53 FLPACKET_SERVER_FORMATION_UPDATE.
// ===========================================================================
static bool MarshalFormationUpdate(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8c61d/06d8c625 [+0x20] = param_2 ; 06d8c628..06d8c642 three dwords of *param_3
    // into +0x24/+0x28/+0x2c with integer moves -- a bit copy.
    FPayloadServerFormationUpdate* Wire = Ch2Payload<FPayloadServerFormationUpdate>(Packet);
    Wire->Field_0x00 = Args[1];
    ::memcpy(&Wire->Offset, reinterpret_cast<const void*>(Args[2]), sizeof(Vector));
    return true;
}

static bool DeliverFormationUpdate(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerFormationUpdate Wire =
        RpcPayloadRead<FPayloadServerFormationUpdate>(Packet, 0);
    Vector Local = Wire.Offset;
    Client->SendServerFormationUpdate(1u, Wire.Field_0x00, Local);
    return true;
}

// ===========================================================================
// ch2 0x57 FLPACKET_SERVER_SET_MUSIC.
// ===========================================================================
static bool MarshalSetMusic(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // 06d8c67d..06d8c6b8: [+0x20..+0x2c] = param_2..param_5, byte [+0x30] = param_6, and
    // FLD/FSTP float param_7 -> [+0x34]. The proxy hands Fade over as its argument slot's
    // bits, so the float travels as its bit pattern (x87 would quiet an sNaN). Pad bytes
    // untouched.
    FPayloadServerSetMusic* Wire = Ch2Payload<FPayloadServerSetMusic>(Packet);
    Wire->Field_0x00 = Args[1];
    Wire->Field_0x04 = Args[2];
    Wire->Field_0x08 = Args[3];
    Wire->Field_0x0c = Args[4];
    Wire->Field_0x10 = (unsigned char)(Args[5] & 0xffu);
    ::memcpy(&Wire->Field_0x14, &Args[6], sizeof(float));
    return true;
}

static bool DeliverSetMusic(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetMusic Wire = RpcPayloadRead<FPayloadServerSetMusic>(Packet, 0);
    // The byte is pushed zero-extended (`XOR EAX,EAX; MOV AL,[EBP+0x30]`); the sender only
    // ever stored a bool's 0/1 there. The float is handed on as the payload's bits.
    Client->SendServerSetMusic(1u, Wire.Field_0x00, Wire.Field_0x04, Wire.Field_0x08, Wire.Field_0x0c,
                               Wire.Field_0x10 != 0, Wire.Field_0x14);
    return true;
}

// ===========================================================================
// ch2 0x52 FLPACKET_SERVER_PLAYERLIST -- three senders, three slots, one dword of
// discriminator at payload+0x00.
//
// Sender addresses and their kind literal:
//   0x06d8c5b0 (IClient+0x1c0)  `MOV [ECX+0x20],EAX` with EAX still zero  -> kind 0
//   0x06d8c500 (IClient+0x1b8)  `MOV dword [ECX+0x20],1`                  -> kind 1
//   0x06d8c560 (IClient+0x1bc)  `MOV dword [ECX+0x20],2`                  -> kind 2
// All three then call the same setter FUN_06d99950(packet, Name, Value), which frees the
// previous wide string, `_wcsdup`s the new one into packet+0x24 and stores Value into
// packet+0x28. The receiver (.text:0x06d83102) reads the kind back and calls one slot.
// ===========================================================================

// FUN_06d99950's copy half: `TEST EAX,EAX ; JZ` on the source (06d99976), then
// MSVCRT _wcsdup (06d9997b), the result stored untested (06d99984). Retail's own pairing
// is _wcsdup / free, so the copy comes from the same allocator the packet's destructor
// hands back to.
static wchar_t* RpcCh2DupWide(const wchar_t* Text) {
    if (Text == nullptr) {
        return nullptr;
    }
    return ::_wcsdup(Text);
}

// Args[0] is the kind literal the sender slot writes; Args[1..] are that slot's own
// arguments minus the session id, which retail never serializes.
static bool MarshalPlayerList(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    FPayloadServerPlayerList Wire;
    Wire.Kind = Args[0];
    Wire.Name = nullptr;
    Wire.Field_0x08 = 0;
    Wire.Field_0x0c = 0;
    Wire.Pad_0x0d[0] = 0;
    Wire.Pad_0x0d[1] = 0;
    Wire.Pad_0x0d[2] = 0;

    if (Wire.Kind == kPlayerListKindEntry) {
        Wire.Name = RpcCh2DupWide(reinterpret_cast<const wchar_t*>(Args[1]));
        Wire.Field_0x08 = Args[2];
        Wire.Field_0x0c = (unsigned char)(Args[3] & 0xffu);
    } else if (Wire.Kind == kPlayerListKindDrop) {
        // Retail pushes the zero left in EAX by the allocation's success test as the
        // string, so kind 2 never carries a name; its one argument is the value.
        Wire.Field_0x08 = Args[1];
    }

    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverPlayerList(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerPlayerList Wire = RpcPayloadRead<FPayloadServerPlayerList>(Packet, 0);
    if (Wire.Kind == kPlayerListKindClear) {
        Client->SendServerPlayerListClear(1u);
        return true;
    }
    if (Wire.Kind == kPlayerListKindEntry) {
        Client->SendServerPlayerListAdd(1u, Wire.Name, Wire.Field_0x08, Wire.Field_0x0c != 0);
        return true;
    }
    if (Wire.Kind == kPlayerListKindDrop) {
        Client->SendServerPlayerListRemove(1u, Wire.Field_0x08);
        return true;
    }
    // Retail's own fallthrough: anything else (the constructor's 3 included) reaches
    // caseD_9 and the packet is discarded without a call.
    return false;
}

// ===========================================================================
// ch2 0x54 FLPACKET_SERVER_MISCOBJUPDATE -- eight senders, eight slots, a two-byte
// bitmask at payload+0x00. See the flag/setter table in rpc_payloads.h.
// ===========================================================================

// Unchecked, as retail's member access is (the setters FUN_06d98ac0..FUN_06d98cf0 write
// `[this + n]` straight away); the block is the class's own size (rpc_packet_registry.cpp).
static FPayloadServerMiscObjUpdate* Ch2MiscObjUpdate(FPacketBase* Packet) {
    return reinterpret_cast<FPayloadServerMiscObjUpdate*>(Packet->Payload());
}

static const FPayloadServerMiscObjUpdate* Ch2MiscObjUpdate(const FPacketBase* Packet) {
    return reinterpret_cast<const FPayloadServerMiscObjUpdate*>(Packet->Payload());
}

// FUN_06d98b20: `basic_string<unsigned short>::assign(source, 0, npos)` into packet+0x40.
// The packet owns the characters from then on.
//
// CORRECTED 2026-09-17: the string is WIDE, not narrow. FUN_06d98b20's three MSVCP60
// imports are all `?$basic_string@GU?$char_traits@G@...` (06d98b52 _Split, 06d98b8a
// _Grow, 06d98b97 _Eos) and both codec slots move two bytes per character.
static void Ch2MiscObjUpdateSetText(FPayloadServerMiscObjUpdate* Wire,
                                    const std::wstring& Source) {
    Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagText);   // 06d98b20
    Wire->Text.assign(Source, 0, std::wstring::npos);         // 06d98b31..06d98b97
}

// FUN_06d98cf0, called once per element by the sender's loop (06d8c34f..06d8c36b), which
// runs zero times on an empty vector -- so an empty vector leaves the flag clear and the
// receiver never calls slot 120.
static void Ch2MiscObjUpdateAddId(FPayloadServerMiscObjUpdate* Wire, unsigned int Value) {
    // 06d98cf4 `OR byte ptr [ESI + 0x21],0x1` -- the flag, once per element.
    Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagIds);
    // vector::insert(_Last, 1, value), i.e. push_back.
    Wire->Ids.push_back(Value);
    // 06d98d17..06d98d27: every push_back ends by recomputing `(End - Begin) >> 2` and
    // storing its low byte at packet+0x68 -- the wire count both codec slots use.
    Wire->Field_0x48 = (unsigned char)(Wire->Ids.size() & 0xffu);
}

// Args[0] is the sender variant; Args[1..] are that slot's arguments minus the session id.
static bool MarshalMiscObjUpdate(FPacketBase* Packet, const unsigned int* Args, unsigned int ArgCount) {
    // No re-initialisation: the class factory ran the ctor's work (0x06d9edc0) on this
    // freshly allocated packet, and retail's senders go straight to the setters.
    FPayloadServerMiscObjUpdate* Wire = Ch2MiscObjUpdate(Packet);

    switch (Args[0]) {
    case kMiscObjUpdateVariant_ObjectGroup:   // 0x06d8c1f0: FUN_06d98ac0 + FUN_06d98af0
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x08);
        Wire->Field_0x08 = Args[1];
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x14);
        Wire->Field_0x14 = Args[2];
        return true;

    case kMiscObjUpdateVariant_Mission:   // 0x06d8c290: FUN_06d98b00, the only pair setter
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagPair);
        Wire->Field_0x18 = Args[1];
        Wire->Field_0x1c = Args[2];
        return true;

    case kMiscObjUpdateVariant_Motd: { // 0x06d8c430: FUN_06d98b20
        // The argument is the wide string's address -- retail's VC6 basic_string<G>,
        // ours std::wstring (Server's PlayerDB::SetMOTD, 0x746e0, hands it &Message).
        // FUN_06d98b20 reads the string (06d98b31 `MOV EAX,[EBX + 0x8]`) untested.
        const std::wstring* Source = reinterpret_cast<const std::wstring*>(Args[1]);
        Ch2MiscObjUpdateSetText(Wire, *Source);
        return true;
    }

    case kMiscObjUpdateVariant_Affiliation:   // 0x06d8c240: FUN_06d98ac0 + FUN_06d98ae0
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x08);
        Wire->Field_0x08 = Args[1];
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x34);
        Wire->Field_0x34 = Args[2];
        return true;

    case kMiscObjUpdateVariant_Rank:   // 0x06d8c2e0: FUN_06d98ad0 + FUN_06d98ce0 (word)
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x04);
        Wire->Field_0x04 = Args[1];
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x0c);
        Wire->Field_0x0c = (unsigned short)(Args[2] & 0xffffu);
        return true;

    case kMiscObjUpdateVariant_PlayerGroup:   // 0x06d8c390: FUN_06d98ad0 + FUN_06d98af0
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x04);
        Wire->Field_0x04 = Args[1];
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x14);
        Wire->Field_0x14 = Args[2];
        return true;

    case kMiscObjUpdateVariant_System:   // 0x06d8c3e0: FUN_06d98ad0 + FUN_06d98cd0
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x04);
        Wire->Field_0x04 = Args[1];
        Wire->Flags = (unsigned short)(Wire->Flags | kMiscObjUpdateFlagField_0x10);
        Wire->Field_0x10 = Args[2];
        return true;

    case kMiscObjUpdateVariant_Formation: { // 0x06d8c330: FUN_06d98cf0 per element
        // The argument is the vector's address (retail const std::vector<uint>&).
        const std::vector<unsigned int>* Source =
            reinterpret_cast<const std::vector<unsigned int>*>(Args[1]);
        // 06d8c34b..06d8c355: the vector's first/last read untested, `CMP ; JZ` on equal;
        // then one FUN_06d98cf0 per element.
        for (std::vector<unsigned int>::const_iterator It = Source->begin();
             It != Source->end(); ++It) {
            Ch2MiscObjUpdateAddId(Wire, *It);
        }
        return true;
    }

    default:
        return false;
    }
}

// The receiver's seven tests, in retail's order. More than one can fire from a single
// packet -- the port keeps that property instead of turning the mask back into a switch.
static bool DeliverMiscObjUpdate(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerMiscObjUpdate* Wire = Ch2MiscObjUpdate(Packet);
    bool bDelivered = false;

    if ((Wire->Flags & kMiscObjUpdateFlagField_0x34) != 0) {
        Client->SendServerMiscObjUpdateAffiliation(1u, Wire->Field_0x08, Wire->Field_0x34);
        bDelivered = true;
    }
    if ((Wire->Flags & kMiscObjUpdateFlagField_0x14) != 0) {
        if ((Wire->Flags & kMiscObjUpdateFlagField_0x04) != 0) {
            Client->SendServerMiscObjUpdatePlayerGroup(1u, Wire->Field_0x04, Wire->Field_0x14);
        } else {
            Client->SendServerMiscObjUpdateObjectGroup(1u, Wire->Field_0x08, Wire->Field_0x14);
        }
        bDelivered = true;
    }
    if ((Wire->Flags & kMiscObjUpdateFlagPair) != 0) {
        Client->SendServerMiscObjUpdateMission(1u, Wire->Field_0x18, Wire->Field_0x1c);
        bDelivered = true;
    }
    if ((Wire->Flags & kMiscObjUpdateFlagText) != 0) {
        // Retail: `LEA EDX,[EBP+0x40]` -- the string object inside the packet is passed
        // by address, not copied out.
        Client->SendServerMiscObjUpdateMotd(1u, Wire->Text);
        bDelivered = true;
    }
    if ((Wire->Flags & kMiscObjUpdateFlagField_0x0c) != 0) {
        // MOVZX: the word is delivered zero-extended.
        Client->SendServerMiscObjUpdateRank(1u, Wire->Field_0x04, static_cast<int>(Wire->Field_0x0c));
        bDelivered = true;
    }
    if ((Wire->Flags & kMiscObjUpdateFlagField_0x10) != 0) {
        Client->SendServerMiscObjUpdateSystem(1u, Wire->Field_0x04, Wire->Field_0x10);
        bDelivered = true;
    }
    if ((Wire->Flags & kMiscObjUpdateFlagIds) != 0) {
        // Retail: `LEA EDX,[EBP+0x58]`.
        Client->SendServerMiscObjUpdateFormation(1u, Wire->Ids);
        bDelivered = true;
    }
    return bDelivered;
}

// ===========================================================================
// Wave-3 types: the six channel-2 payloads that carry a container, a permutation or a
// verbatim struct. Every one of them was read four ways -- sender body, receiver case,
// the registry's operator new() size, and (where the sender permutes) the receiver's
// permutation being the strict inverse. The per-type derivation lives next to the
// structs in rpc_payloads.h; only the code is here.
// ===========================================================================

// Typed views on a payload that holds a live container: the generic RpcPayloadWrite
// would overwrite the list header, so these types address their fields in place, the
// way retail's concrete packet classes do.
//
// No test in either: retail's concrete class addresses its own members at fixed offsets
// (e.g. 06d88c35 `MOV [ECX+0x20],EDX`, 06d811ca FUN_06d931b0 `LEA EAX,[this+0x28]`), and
// the block is always the class's own size (registry `PUSH n`, payload static_asserts).
static FPayloadServerDamageObject* RpcDamageObjectPayload(FPacketBase* Packet) {
    return reinterpret_cast<FPayloadServerDamageObject*>(Packet->Payload());
}

static const FPayloadServerDamageObject* RpcDamageObjectPayload(const FPacketBase* Packet) {
    return reinterpret_cast<const FPayloadServerDamageObject*>(Packet->Payload());
}

static FPayloadServerCreateLoot* RpcCreateLootPayload(FPacketBase* Packet) {
    return reinterpret_cast<FPayloadServerCreateLoot*>(Packet->Payload());
}

static const FPayloadServerCreateLoot* RpcCreateLootPayload(const FPacketBase* Packet) {
    return reinterpret_cast<const FPayloadServerCreateLoot*>(Packet->Payload());
}

// ===========================================================================
// ch2 0x05 FLPACKET_SERVER_DAMAGEOBJECT
// Sender 0x06d88c10 (IClient+0x0fc, RET 0xc), receiver caseD_5 at 0x06d81ed4.
// Both sides pass the literal 1 by VALUE: the receiver pushes it at 06d81f1d
// (`PUSH 0x1`), and the sender forwards its own first argument unchanged
// (06d88c46 `MOV ECX,[ESP+0xc]` then 06d88c4d `PUSH ECX`, no dereference).
//
// IServerImpl::HandleDamageObject (0x06d84c60, the other caller of FUN_06d90290) is NOT
// on the HandlePacket path: caseD_5 inlines the same logic, so this codec alone carries
// the type end to end.
// ===========================================================================
static bool MarshalDamageObject(FPacketBase* Packet, const unsigned int* Args,
                                unsigned int ArgCount) {
    // The third argument is Common's DamageList -- what both Server.dll senders push
    // (server_observer.cpp's stack DamageList, SystemPlayerView_DamageObject's two
    // DamageList records) -- read through its named members, not a retail-shaped view.
    const DamageList* Source = reinterpret_cast<const DamageList*>(Args[2]);
    FPayloadServerDamageObject* Wire = RpcDamageObjectPayload(Packet);
    Wire->Field_0x00 = Args[1];                       // 06d88c35 MOV [ECX+0x20],EDX
    // FUN_06d90290: the list assignment plus 06d90459..06d90474, the four tail fields,
    // into the DamageList the packet ctor built -- i.e. DamageList::operator=.
    Wire->Damage = *Source;
    return true;
}

static bool DeliverDamageObject(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerDamageObject* Wire = RpcDamageObjectPayload(Packet);
    // Retail builds the destination DamageList on its own stack (the COMMON.DLL
    // std::list constructor imported at 06d81ee3), assigns into it, calls the slot and
    // tears it down again at 06d81f27..06d81f5d. The receiver in freelancer.exe reads
    // it as the same Common DamageList the server built.
    DamageList Local;
    Local = Wire->Damage;                             // FUN_06d90290(packet, &local)
    Client->SendServerDamageObject(1u, Wire->Field_0x00, Local);
    return true;
}

// IServerImpl::HandleDamageObject (0x06d84c60), the exported member rpclocal.cpp
// forwards to. Same body as caseD_5 above except that slot 63's first argument is the
// member's third parameter (06d84cd7) instead of the literal 1:
//   06d84c7a..06d84c82  a null packet returns false (06d84d41 XOR AL,AL)
//   06d84c8d..06d84cc3  local DamageList, filled by FUN_06d90290(packet, &local)
//   06d84cc8..06d84cdc  Client->vt[+0xfc](Arg, [packet+0x20], &local)
//   06d84ce2..06d84d23  the local list is torn down; 06d84d28 MOV AL,1
__declspec(dllexport) bool RpcHandleDamageObject(const FPacketBase* Packet, IClient* Client, unsigned int Arg) {
    if (Packet == nullptr) {
        return false;
    }
    const FPayloadServerDamageObject* Wire = RpcDamageObjectPayload(Packet);
    DamageList Local;
    Local = Wire->Damage;                             // 06d84c8d..06d84cc3
    Client->SendServerDamageObject(Arg, Wire->Field_0x00, Local);
    return true;
}

// ===========================================================================
// ch2 0x28 FLPACKET_SERVER_CREATELOOT
// Sender 0x06d8aae0 (IClient+0x0c0, RET 8), receiver caseD_28 at 0x06d811c8. The
// literal 1 travels by value on both sides (06d812fd `PUSH 0x1`, 06d8ac24/06d8ac2b).
// ===========================================================================
// The argument is Server.dll's FServerCreateLootRequest with its array of FServerLootDesc
// (server_createinfo.h); both are the records retail's two ends walk, so the old private
// mirrors FServerCreateLootInfo / FRpcLootDescMirror are gone (2026-10-16). Every float
// member moves as its bits: both directions copy the record with plain dword MOVs.
static_assert(sizeof(FServerLootDesc) == 0x48, "CREATELOOT record is 0x48 (the array stride)");
static_assert(offsetof(FServerLootDesc, Orientation) == 0x28, "CREATELOOT record +0x28");
static_assert(offsetof(FServerLootDesc, Velocity) == 0x38, "CREATELOOT record +0x38");
static_assert(offsetof(FServerLootDesc, Field_0x44) == 0x44, "CREATELOOT record +0x44");
static_assert(sizeof(FServerCreateLootRequest) == 0x14, "CREATELOOT argument struct 0x14");
static_assert(offsetof(FServerCreateLootRequest, Items) == 0x0c, "CREATELOOT argument +0x0c");

static bool MarshalCreateLoot(FPacketBase* Packet, const unsigned int* Args,
                              unsigned int ArgCount) {
    const FServerCreateLootRequest* Source =
        reinterpret_cast<const FServerCreateLootRequest*>(Args[1]);
    FPayloadServerCreateLoot* Wire = RpcCreateLootPayload(Packet);
    RpcPacketLootListInit(Packet);                                        // the packet ctor's dummy
    Wire->Field_0x00 = Source->OwnerId;                                   // 06d8ab09
    Wire->Field_0x04 = (unsigned char)(Source->Kind & 0xffu);             // 06d8ab0c/06d8ab13
    Wire->Pad_0x05[0] = 0;
    Wire->Pad_0x05[1] = 0;
    Wire->Pad_0x05[2] = 0;

    const unsigned int Count = Source->Count;                             // 06d8ab19 MOV AL,[EAX+0x10]
    if (Count == 0) {
        return true;                                                      // 06d8ab71 JBE
    }
    // 06d8ab79: the array at src+0x0c is walked with no test of its own.
    for (unsigned int i = 0; i < Count; ++i) {
        const FServerLootDesc& In = Source->Items[i];
        // Retail zeroes the whole temporary once before the loop and seeds wire+0x34
        // with 1.0f (06d8ab55 `MOV [ESP+0x3c],0x3f800000`); every field including that
        // one is then overwritten on every iteration, so the seed never ships.
        FRpcLootWireRecord Out;
        Out.Field_0x00 = In.LootId;                                        // record +0x00
        Out.Field_0x04 = In.ContentsNickname;                              // record +0x04
        Out.Field_0x08 = RpcCh2FloatBits(In.ContentsHitPts);               // record +0x08
        Out.Field_0x0c = (unsigned short)(In.Units & 0xffffu);             // record +0x0c, a word
        Out.Pad_0x0e = 0;
        Out.Field_0x10 = In.ContainerNickname;                             // record +0x10
        Out.Field_0x14 = RpcCh2FloatBits(In.HitPts);                       // record +0x14
        Out.Field_0x18 = In.NameOverride;                                  // record +0x18
        Out.Field_0x1c = RpcCh2FloatBits(In.Position.x);                   // record +0x1c
        Out.Field_0x20 = RpcCh2FloatBits(In.Position.y);                   // record +0x20
        Out.Field_0x24 = RpcCh2FloatBits(In.Position.z);                   // record +0x24
        Out.Field_0x28 = RpcCh2FloatBits(In.Velocity.x);                   // record +0x38
        Out.Field_0x2c = RpcCh2FloatBits(In.Velocity.y);                   // record +0x3c
        Out.Field_0x30 = RpcCh2FloatBits(In.Velocity.z);                   // record +0x40
        Out.Field_0x34 = RpcCh2FloatBits(In.Orientation.w);                // record +0x28
        Out.Field_0x38 = RpcCh2FloatBits(In.Orientation.y);   // record +0x30; 06d8abe3 -- the transposed pair, see the header
        Out.Field_0x3c = RpcCh2FloatBits(In.Orientation.x);   // record +0x2c; 06d8abea
        Out.Field_0x40 = RpcCh2FloatBits(In.Orientation.z);                // record +0x34
        Out.Field_0x44 = In.bMarkedBy;                                     // record +0x46
        Out.Field_0x45 = In.bTemporary;                                    // record +0x45
        Out.Pad_0x46[0] = 0;
        Out.Pad_0x46[1] = 0;
        RpcPacketLootPushBack(Packet, Out);                               // 06d8ac0f FUN_06d93160
    }
    return true;
}

static bool DeliverCreateLoot(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCreateLoot* Wire = RpcCreateLootPayload(Packet);
    // 06d811ca FUN_06d931b0: the list header's address, used untested.
    const FRpcLootWireList* List = RpcPacketLootList(Packet);
    if (List->Size == 0) {
        // 06d811d6 `CMP EAX,EBX ; JBE caseD_9` -- an empty list drops the packet and the
        // slot is never called.
        return true;
    }
    // 06d811e9 `MOVZX ESI,AL`: retail keeps only the LOW BYTE of the list size, both as
    // the array length it allocates and as the count it hands on -- then walks the WHOLE
    // list into that array (06d81267..06d812ea) with a BYTE index (06d8126c `MOV CL,BL`,
    // 06d8126e `INC BL`). A list of 256 or more therefore allocates short and writes past
    // the end; that is reproduced as it is.
    const unsigned int Count = List->Size & 0xffu;
    // 06d8120b operator new(Count * 0x48); the array new-expression's test (06d81224)
    // only skips the element constructors -- the loop below writes through it anyway.
    FServerLootDesc* Items = static_cast<FServerLootDesc*>(
        ::operator new(sizeof(FServerLootDesc) * Count, std::nothrow));
    if (Items != nullptr) {
        // FUN_06d840d0, once per element (06d81230..06d8123b): zero, then +0x44 = 1,
        // +0x45 = +0x46 = 0.
        for (unsigned int i = 0; i < Count; ++i) {
            unsigned char* Raw = reinterpret_cast<unsigned char*>(&Items[i]);
            for (unsigned int b = 0; b < (unsigned int)sizeof(FServerLootDesc); ++b) {
                Raw[b] = 0;
            }
            Items[i].Field_0x44 = 1;
            Items[i].bTemporary = 0;
            Items[i].bMarkedBy = 0;
        }
    }
    unsigned char Index = 0;
    for (const FRpcLootWireNode* Node = List->Head->Next; Node != List->Head;
         Node = Node->Next, ++Index) {
        const FRpcLootWireRecord& In = Node->Value;
        FServerLootDesc& Out = Items[Index];
        Out.LootId = In.Field_0x00;                                        // record +0x00
        Out.ContentsNickname = In.Field_0x04;                              // record +0x04
        RpcCh2SetFloatBits(Out.ContentsHitPts, In.Field_0x08);             // record +0x08
        // Retail writes the word only; the element's upper half is the ctor's zero.
        Out.Units = (unsigned int)In.Field_0x0c;                           // record +0x0c
        Out.ContainerNickname = In.Field_0x10;                             // record +0x10
        RpcCh2SetFloatBits(Out.HitPts, In.Field_0x14);                     // record +0x14
        Out.NameOverride = In.Field_0x18;                                  // record +0x18
        RpcCh2SetFloatBits(Out.Position.x, In.Field_0x1c);                 // record +0x1c
        RpcCh2SetFloatBits(Out.Position.y, In.Field_0x20);                 // record +0x20
        RpcCh2SetFloatBits(Out.Position.z, In.Field_0x24);                 // record +0x24
        RpcCh2SetFloatBits(Out.Velocity.x, In.Field_0x28);                 // record +0x38
        RpcCh2SetFloatBits(Out.Velocity.y, In.Field_0x2c);                 // record +0x3c
        RpcCh2SetFloatBits(Out.Velocity.z, In.Field_0x30);                 // record +0x40
        RpcCh2SetFloatBits(Out.Orientation.w, In.Field_0x34);              // record +0x28
        RpcCh2SetFloatBits(Out.Orientation.x, In.Field_0x38);   // record +0x2c; 06d812c7 -- the transposed pair
        RpcCh2SetFloatBits(Out.Orientation.y, In.Field_0x3c);   // record +0x30; 06d812cd
        RpcCh2SetFloatBits(Out.Orientation.z, In.Field_0x40);              // record +0x34
        Out.bMarkedBy = In.Field_0x44;                                     // record +0x46, 06d812d9
        Out.bTemporary = In.Field_0x45;                                    // record +0x45, 06d812df
    }

    FServerCreateLootRequest Info;
    Info.OwnerId = Wire->Field_0x00;
    Info.Kind = Wire->Field_0x04;                // 06d811e2 MOVZX, stored as a dword
    Info.bMarkedBy = 0;                          // 06d811f7
    Info.bTemporary = 0;                         // 06d811fb
    Info.Pad0a[0] = 0;
    Info.Pad0a[1] = 0;
    Info.Items = Items;
    Info.Count = (unsigned char)Count;
    Info.Pad11[0] = 0;
    Info.Pad11[1] = 0;
    Info.Pad11[2] = 0;
    Client->SendServerCreateLoot(1u, Info);
    ::operator delete(Items);                    // 06d81305..06d8130f
    return true;
}

// ===========================================================================
// ch2 0x2d FLPACKET_SERVER_CREATECOUNTER
// Sender 0x06d8a270 (IClient+0x0cc), receiver caseD_2d at 0x06d81ce9. Eighteen fields,
// permuted; the receiver is the strict inverse. The literal 1 is by REFERENCE.
// ===========================================================================
static bool MarshalCreateCounter(FPacketBase* Packet, const unsigned int* Args,
                                 unsigned int ArgCount) {
    // The argument is Server.dll's FServerCreateProjectileInfo (server_createinfo.h), the
    // record CREATEMINE sends too; the private mirror FServerCreateCounterInfo is gone
    // (2026-10-16). Float members move as their bits (plain dword MOVs on both sides).
    const FServerCreateProjectileInfo* Source =
        reinterpret_cast<const FServerCreateProjectileInfo*>(Args[1]);
    FPayloadServerCreateCounter Wire;
    Wire.Field_0x00 = Source->ArchetypeNickname;                      // arg +0x04
    Wire.Field_0x04 = Source->ObjId;                                  // arg +0x00
    Wire.Field_0x08 = Source->Field_0x44;                             // arg +0x44
    Wire.Field_0x0c = Source->OwnerId;                                // arg +0x08
    Wire.Field_0x10 = Source->SubObjId;                               // arg +0x0c, a word
    Wire.Pad_0x12 = 0;
    Wire.Field_0x14 = RpcCh2FloatBits(Source->Orientation.w);         // arg +0x1c
    Wire.Field_0x18 = RpcCh2FloatBits(Source->Orientation.x);         // arg +0x20
    Wire.Field_0x1c = RpcCh2FloatBits(Source->Orientation.y);         // arg +0x24
    Wire.Field_0x20 = RpcCh2FloatBits(Source->Orientation.z);         // arg +0x28
    Wire.Field_0x24 = RpcCh2FloatBits(Source->Position.x);            // arg +0x10
    Wire.Field_0x28 = RpcCh2FloatBits(Source->Position.y);            // arg +0x14
    Wire.Field_0x2c = RpcCh2FloatBits(Source->Position.z);            // arg +0x18
    Wire.Field_0x30 = RpcCh2FloatBits(Source->Velocity.x);            // arg +0x2c
    Wire.Field_0x34 = RpcCh2FloatBits(Source->Velocity.y);            // arg +0x30
    Wire.Field_0x38 = RpcCh2FloatBits(Source->Velocity.z);            // arg +0x34
    Wire.Field_0x3c = RpcCh2FloatBits(Source->Field_0x38.x);          // arg +0x38
    Wire.Field_0x40 = RpcCh2FloatBits(Source->Field_0x38.y);          // arg +0x3c
    Wire.Field_0x44 = RpcCh2FloatBits(Source->Field_0x38.z);          // arg +0x40
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverCreateCounter(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCreateCounter Wire =
        RpcPayloadRead<FPayloadServerCreateCounter>(Packet, 0);
    FServerCreateProjectileInfo Info;
    Info.ObjId = Wire.Field_0x04;
    Info.ArchetypeNickname = Wire.Field_0x00;
    Info.OwnerId = Wire.Field_0x0c;
    Info.SubObjId = Wire.Field_0x10;
    Info.Pad0e = 0;
    RpcCh2SetFloatBits(Info.Position.x, Wire.Field_0x24);
    RpcCh2SetFloatBits(Info.Position.y, Wire.Field_0x28);
    RpcCh2SetFloatBits(Info.Position.z, Wire.Field_0x2c);
    RpcCh2SetFloatBits(Info.Orientation.w, Wire.Field_0x14);
    RpcCh2SetFloatBits(Info.Orientation.x, Wire.Field_0x18);
    RpcCh2SetFloatBits(Info.Orientation.y, Wire.Field_0x1c);
    RpcCh2SetFloatBits(Info.Orientation.z, Wire.Field_0x20);
    RpcCh2SetFloatBits(Info.Velocity.x, Wire.Field_0x30);
    RpcCh2SetFloatBits(Info.Velocity.y, Wire.Field_0x34);
    RpcCh2SetFloatBits(Info.Velocity.z, Wire.Field_0x38);
    RpcCh2SetFloatBits(Info.Field_0x38.x, Wire.Field_0x3c);
    RpcCh2SetFloatBits(Info.Field_0x38.y, Wire.Field_0x40);
    RpcCh2SetFloatBits(Info.Field_0x38.z, Wire.Field_0x44);
    Info.Field_0x44 = Wire.Field_0x08;
    // 06d81d8d: `MOV dword ptr [ESP+0x1e4],0x1` then `LEA EDX,[ESP+0x1e4] ; PUSH EDX`.
    unsigned int One = 1u;
    Client->SendServerCreateCounter(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x37 FLPACKET_SERVER_CREATEGUIDED
// Sender 0x06d8a730 (IClient+0x0c8), receiver caseD_37 at 0x06d81da6. Fourteen fields,
// rotated; the receiver is the strict inverse. The literal 1 is by REFERENCE.
// ===========================================================================
static bool MarshalCreateGuided(FPacketBase* Packet, const unsigned int* Args,
                                unsigned int ArgCount) {
    const FServerCreateGuidedInfo* Source =
        reinterpret_cast<const FServerCreateGuidedInfo*>(Args[1]);
    // 06d8a791..06d8a7dd copy the argument's first eight dwords -- ObjId, Position and
    // Orientation -- as plain dwords, so they are moved as bits here too.
    const unsigned int* Head = reinterpret_cast<const unsigned int*>(Source);
    FPayloadServerCreateGuided Wire;
    Wire.Field_0x00 = Source->Field_0x2c;             // 06d8a751
    Wire.Field_0x04 = Source->ArchetypeNickname;      // 06d8a75b
    Wire.Field_0x08 = Source->OwnerId;                // 06d8a765
    Wire.Field_0x0c = Source->SubObjId;               // 06d8a76f, a word
    Wire.Pad_0x0e = 0;
    Wire.Field_0x10 = Source->TargetId;               // 06d8a77b
    Wire.Field_0x14 = Source->TargetSubObjId;         // 06d8a785, a word
    Wire.Pad_0x16 = 0;
    Wire.Field_0x18 = Head[0];                        // ObjId
    Wire.Field_0x1c = Head[1];                        // Position
    Wire.Field_0x20 = Head[2];
    Wire.Field_0x24 = Head[3];
    Wire.Field_0x28 = Head[4];                        // Orientation
    Wire.Field_0x2c = Head[5];
    Wire.Field_0x30 = Head[6];
    Wire.Field_0x34 = Head[7];
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverCreateGuided(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCreateGuided Wire =
        RpcPayloadRead<FPayloadServerCreateGuided>(Packet, 0);
    // caseD_37 (06d81da6..06d81e20) fills a stack local at ESP+0x50 field by field and
    // never writes +0x26 / +0x36; the port zeroes those two pads.
    FServerCreateGuidedInfo Info;
    unsigned int* Head = reinterpret_cast<unsigned int*>(&Info);
    Head[0] = Wire.Field_0x18;                        // ObjId
    Head[1] = Wire.Field_0x1c;                        // Position
    Head[2] = Wire.Field_0x20;
    Head[3] = Wire.Field_0x24;
    Head[4] = Wire.Field_0x28;                        // Orientation
    Head[5] = Wire.Field_0x2c;
    Head[6] = Wire.Field_0x30;
    Head[7] = Wire.Field_0x34;
    Info.TargetId = Wire.Field_0x10;
    Info.TargetSubObjId = Wire.Field_0x14;
    Info.Pad26 = 0;
    Info.ArchetypeNickname = Wire.Field_0x04;
    Info.Field_0x2c = Wire.Field_0x00;
    Info.OwnerId = Wire.Field_0x08;
    Info.SubObjId = Wire.Field_0x0c;
    Info.Pad36 = 0;
    // 06d81e24: `MOV dword ptr [ESP+0x1ec],0x1` then `LEA EAX,[ESP+0x1ec] ; PUSH EAX`.
    unsigned int One = 1u;
    Client->SendServerCreateGuided(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x32 FLPACKET_SERVER_REQUESTSPACESCRIPT
// Sender 0x06d8aa40 (IClient+0x0f8), receiver caseD_32 at 0x06d81c7d. The literal 1 is
// by VALUE here (06d81cdc `PUSH 0x1`).
// ===========================================================================
static bool MarshalRequestSpaceScript(FPacketBase* Packet, const unsigned int* Args,
                                      unsigned int ArgCount) {
    const FServerRequestSpaceScriptInfo* Source =
        reinterpret_cast<const FServerRequestSpaceScriptInfo*>(Args[1]);
    FPayloadServerRequestSpaceScript Wire;
    Wire.Field_0x00 = Source->Field_0x000;
    Wire.Field_0x08 = Source->Field_0x108;
    Wire.Field_0x0c = Source->Field_0x10c;
    Wire.Field_0x10 = Source->Field_0x110;
    // 06d8aa84..06d8aaa1: only 1 and 2 are mapped (1 -> byte 1 at 06d8aa93, 2 -> byte 0
    // at 06d8aaa1). Any other value leaves the byte as the packet constructor seeded it:
    // FUN_06d9db10 06d9db31 stores byte 1 at packet+0x34 -- so it ships 1, not garbage.
    if (Source->Field_0x114 == 1u) {
        Wire.Field_0x14 = 1;
    } else if (Source->Field_0x114 == 2u) {
        Wire.Field_0x14 = 0;
    } else {
        Wire.Field_0x14 = 1;
    }
    Wire.Pad_0x15[0] = 0;
    Wire.Pad_0x15[1] = 0;
    Wire.Pad_0x15[2] = 0;

    // FUN_06d989b0: operator new(strlen + 1), copy strlen + 1 bytes, store the pointer
    // at payload+0x18 and the count at payload+0x04. A zero-length name ships nothing,
    // exactly as the retail guard at 06d989b4 does (the name is an inline array at
    // src+0x04, 06d8aaa5, so the null half of that guard never fires).
    const unsigned int Length = RpcCh2StringLength(Source->Name);
    if (Length == 0) {
        Wire.NameSize = 0;
        Wire.Name = nullptr;
    } else {
        Wire.Name = static_cast<char*>(RpcCh2CopyBlock(Source->Name, Length + 1));  // 06d989cf
        Wire.NameSize = Length + 1;                                              // 06d989e8
    }
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverRequestSpaceScript(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerRequestSpaceScript Wire =
        RpcPayloadRead<FPayloadServerRequestSpaceScript>(Packet, 0);
    FServerRequestSpaceScriptInfo Info;
    Info.Field_0x000 = Wire.Field_0x00;
    Info.Field_0x108 = Wire.Field_0x08;
    Info.Field_0x10c = Wire.Field_0x0c;
    Info.Field_0x110 = Wire.Field_0x10;
    // 06d81ca0..06d81cb6: `XOR EDX,EDX ; CMP AL,BL ; SETZ DL ; INC EDX`.
    Info.Field_0x114 = (Wire.Field_0x14 == 0) ? 2u : 1u;
    // 06d81cbd..06d81ccd: a byte loop that copies up to and including the NUL, with no
    // bound, skipped when the pointer is null (06d81cbd JZ). The 0x104-byte field is
    // otherwise the caller's stack; zeroed here per the uninitialised-memory rule.
    for (unsigned int i = 0; i < kRequestSpaceScriptNameSize; ++i) {
        Info.Name[i] = '\0';
    }
    if (Wire.Name != nullptr) {
        char* Dest = Info.Name;
        unsigned int i = 0;
        do {
            Dest[i] = Wire.Name[i];
        } while (Wire.Name[i++] != '\0');
    }
    Client->SendServerRequestSpaceScript(1u, Info);
    return true;
}

// ===========================================================================
// ch2 0x4e FLPACKET_SERVER_SETDIRECTIVE
// Sender 0x06d8a480 (IClient+0x190), receiver caseD_4e at 0x06d81c3b. The payload IS the
// argument struct: the receiver is `MOV ECX,0x25 ; REP MOVSD` (0x94 bytes) and the
// sender writes the same offsets one at a time. The literal 1 is by REFERENCE.
// ===========================================================================
static bool MarshalSetDirective(FPacketBase* Packet, const unsigned int* Args,
                                unsigned int ArgCount) {
    const FPayloadServerSetDirective* Source =
        reinterpret_cast<const FPayloadServerSetDirective*>(Args[1]);
    FPayloadServerSetDirective Wire = *Source;
    // Retail never writes payload+0x09..+0x0b (06d8a4b4 moves a single byte); this port
    // zeroes them so the packet carries no stack residue.
    Wire.Pad_0x09[0] = 0;
    Wire.Pad_0x09[1] = 0;
    Wire.Pad_0x09[2] = 0;
    Wire.Pad_0x91[0] = 0;
    Wire.Pad_0x91[1] = 0;
    Wire.Pad_0x91[2] = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

static bool DeliverSetDirective(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetDirective Wire =
        RpcPayloadRead<FPayloadServerSetDirective>(Packet, 0);
    FPayloadServerSetDirective Info = Wire;
    // 06d81c64: `MOV dword ptr [ESP+0x1dc],0x1` then `LEA EDX,[ESP+0x1dc] ; PUSH EDX`.
    unsigned int One = 1u;
    Client->SendServerSetDirective(One, Info);
    return true;
}

// ===========================================================================
// The flattened-FmtStr family: ch2 0x29, 0x2b, 0x34, 0x35, 0x45.
//
// The four COMMON.DLL entry points retail rpclocal imports for this -- flatten,
// unflatten, ~FmtStr and Val::get_flattened_size -- are all in src/include/common_text.h
// with matching signatures, and this project now links Common.lib the way retail's own
// import table does. The long form of that argument is at the head of the family section
// in rpc_payloads.h.
// ===========================================================================

// RpcFlattenFmtStr / RpcUnflattenFmtStr live in rpc_containers.cpp -- channel 1's
// POP_UP_DIALOG uses the same pair.

// ===========================================================================
// ch2 0x29 FLPACKET_SERVER_SETREPUTATION
// Sender 0x06d88d70 (IClient+0x108, RET 8), receiver caseD_29 at 0x06d813cf, which calls
// the very same slot (+0x108) on the real IClient -- so the argument struct is the same
// object at both ends and the round trip is checkable field by field. It is: +0x00 ->
// payload+0x38 -> +0x00, +0x04 -> payload+0x10 -> +0x04, +0x08 -> payload+0x3c -> +0x08,
// the relation array -> the packet's list -> a fresh array, both FmtStrs, and the wide
// name. The literal 1 replaces the session id BY VALUE (06d81518 `PUSH 0x1`).
//
// Retail drops arg+0x0c on the floor: Reputation::Vibe::Get fills it (Server.dll.c:89958)
// and no sender field ever reads it. Reproduced.
// ===========================================================================
static bool MarshalSetReputation(FPacketBase* Packet, const unsigned int* Args,
                                 unsigned int ArgCount) {
    const FServerSetReputationInfo* Source =
        reinterpret_cast<const FServerSetReputationInfo*>(Args[1]);
    // Retail stores into the packet FUN_06d96300 just built (06d88da5 on), field by field.
    // Everything it does not store keeps the constructor's seed (FUN_06d9d290): +0x14..+0x24
    // = 1, 2, 4, 8, 0x10 (06d9d2dc..06d9d300 -- +0x18 IS seeded, with 2), the mask byte
    // +0x34 = 0, the display name null, and the relation list's dummy node already built
    // (06d9d30c..06d9d31a). So no whole-payload write here: that would put 0 over the 2 at
    // +0x18 and a null over the constructor's dummy, leaking it.
    FPayloadServerSetReputation* Wire = Ch2Payload<FPayloadServerSetReputation>(Packet);
    Wire->VibeHandle = (unsigned int)Source->VibeHandle;     // 06d88db0/06d88db2 <- arg+0x00
    Wire->Field_0x3c = Source->Field_0x08;                   // 06d88db5/06d88dbc <- arg+0x08
    // 06d88dbf..06d88df9 / 06d88dff..06d88e39: size (6 + the Val sizes), bare operator new,
    // FmtStr::flatten -- size at +0x00/+0x08, block at +0x04/+0x0c.
    Wire->NameABuffer = RpcFlattenFmtStr(Source->NameA, Wire->NameASize);
    Wire->NameBBuffer = RpcFlattenFmtStr(Source->NameB, Wire->NameBSize);

    // 06d88e3f..06d88e52 wcslen(src+0x48), unbounded, then FUN_06d92710(src+0x48, len):
    // `if (src == 0 || len == 0) return` (06d92716 / 06d92720) -- an empty name ships no
    // block and leaves the constructor's null -- else operator new((len+1)*4), bare
    // (06d92731): retail allocates FOUR bytes per wchar_t and copies (len+1)*2 bytes,
    // terminator included (06d92743), so the block is twice the size it needs. Reproduced.
    const wchar_t* NameSource = Source->DisplayName;
    unsigned int NameChars = 0;
    while (NameSource[NameChars] != L'\0') {
        ++NameChars;
    }
    if (NameChars != 0) {
        const unsigned int Total = NameChars + 1;
        wchar_t* NameBlock = static_cast<wchar_t*>(::operator new(Total * 4, std::nothrow));
        for (unsigned int i = 0; i < Total; ++i) {
            NameBlock[i] = NameSource[i];
        }
        Wire->DisplayName = NameBlock;                       // 06d9273e
        Wire->DisplayNameLength = Total;                     // 06d92750
    }

    // 06d88e57..06d88ece: a stack list (FUN_06d8cb20), one bare new(8) copy per relation
    // pushed by FUN_06d8cad0 while the 16-bit index is below the byte at arg+0x14, re-read
    // every pass (06d88ec5); the array at arg+0x10 is read untested (06d88e88 / 06d88e9f).
    // FUN_06d92760 (06d88ed9) then appends a fresh new(8) copy of every value to the
    // packet's list, and 06d88efb..06d88f4c frees the stack list, values and all. The two
    // hops collapse into one push_back per element onto the constructor's list -- the
    // packet ends up byte-identical (see the relation-list note in rpc_containers.h).
    FRpcRelationList* List = &Wire->Relations;
    const unsigned int Count = Source->RelationCount;
    for (unsigned int i = 0; i < Count; ++i) {
        // Reputation::Relation -> its wire view FRpcRelation (same two dwords).
        RpcRelationListPushBack(List,
            reinterpret_cast<const FRpcRelation&>(Source->Relations[i]));
    }
    Wire->Attitude = Source->Attitude;                       // 06d88ede..06d88ee9 <- arg+0x04
    return true;
}

static bool DeliverSetReputation(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSetReputation Wire =
        RpcPayloadRead<FPayloadServerSetReputation>(Packet, 0);

    FServerSetReputationInfo Info;
    Info.VibeHandle = (int)Wire.VibeHandle;                  // 06d8142b -> S+0x50
    Info.Attitude = Wire.Attitude;                           // 06d8142e -> S+0x54
    Info.Field_0x08 = Wire.Field_0x3c;                       // 06d8143e -> S+0x58
    Info.Field_0x0c = 0;                                     // retail leaves it as garbage
    Info.Relations = nullptr;                                // 06d813f1
    Info.RelationCount = 0;                                  // 06d813f5
    Info.Pad_0x15[0] = 0;
    Info.Pad_0x15[1] = 0;
    Info.Pad_0x15[2] = 0;
    for (unsigned int i = 0; i < 24; ++i) {
        Info.DisplayName[i] = L'\0';                         // 06d813f9 writes only [0]
    }

    RpcUnflattenFmtStr(Info.NameA, Wire.NameABuffer, Wire.NameASize);   // 06d8145a
    RpcUnflattenFmtStr(Info.NameB, Wire.NameBBuffer, Wire.NameBSize);   // 06d8146b

    // 06d8146d..06d81483 `if (packet+0x64) wcscpy(local, packet+0x64) else local[0]=0`,
    // the wcscpy unbounded, as retail's.
    if (Wire.DisplayName != nullptr) {
        wcscpy(Info.DisplayName, Wire.DisplayName);
    }

    const FRpcRelationList* List =
        reinterpret_cast<const FRpcRelationList*>(Packet->Payload() + 0x28);
    // 06d81490..06d81497 FUN_06d927e0(packet, &local): the packet's list is copied into a
    // stack list -- its dummy read untested (06d927e1..06d927e5; the packet constructor
    // always builds it, 06d9d30c..06d9d31a), a fresh new(8) per value, and NULL VALUES
    // SKIPPED (06d927f5 JZ), so the local size counts only the non-null ones. The port
    // reads the packet's list in place (see the relation-list note in rpc_containers.h),
    // which makes that count explicit here.
    unsigned int Size = 0;
    for (const FRpcRelationNode* Node = List->Head->Next; Node != List->Head;
         Node = Node->Next) {
        if (Node->Value != nullptr) {
            ++Size;
        }
    }
    // 06d8149c..06d814aa: that size is read as a dword for the >0 test and as a BYTE for
    // the count that ships on, then the array is operator new(size * 8).
    Info.RelationCount = (unsigned char)(Size & 0xffu);
    FRpcRelation* Items = nullptr;
    if (Size != 0) {                                         // 06d814aa JBE, on the dword
        // 06d814b0: bare operator new(Size * 8). The walk (06d814c8..06d81503) covers the
        // WHOLE list with a 16-bit index (06d814d7 `MOV AX,DI`), skipping null values
        // (06d814cb) -- no bound against Size.
        Items = static_cast<FRpcRelation*>(
            ::operator new(sizeof(FRpcRelation) * Size, std::nothrow));
        const FRpcRelationNode* Dummy = List->Head;
        unsigned short At = 0;
        for (const FRpcRelationNode* Node = Dummy->Next; Node != Dummy; Node = Node->Next) {
            if (Node->Value != nullptr) {
                Items[At] = *Node->Value;
                ++At;
            }
        }
        // The wire view back to Reputation::Relation (same two dwords).
        Info.Relations = reinterpret_cast< ::Reputation::Relation*>(Items);
    }

    Client->SendServerSetReputation(1u, Info);

    // 06d81520 `CMP byte [ESP+0x64],BL ; JBE`: the array is freed only when the BYTE
    // count is non-zero, so a list of exactly 256*k relations leaks it, as in retail. Both
    // the count and the pointer (06d81526 `MOV ECX,[ESP+0x60]`) are read back out of the
    // local the slot was handed.
    if (Info.RelationCount > 0) {
        ::operator delete(Info.Relations);
    }
    return true;
}

// ===========================================================================
// ch2 0x2b FLPACKET_SERVER_SETGROUPFEELINGS
// Sender 0x06d8a5c0 (IClient+0x148, RET 8), receiver caseD_2b at 0x06d815bb -> the same
// slot (+0x148). No FmtStr here: the name is a narrow string written by FUN_06d91850
// (operator new(strlen+1) + strncpy, the count at packet+0x24) and read back by a plain
// strncpy at 06d815f1. The relation list is the same std::list<Relation*> as 0x29.
// ===========================================================================
static bool MarshalSetGroupFeelings(FPacketBase* Packet, const unsigned int* Args,
                                    unsigned int ArgCount) {
    const FServerSetGroupFeelingsInfo* Source =
        reinterpret_cast<const FServerSetGroupFeelingsInfo*>(Args[1]);
    // Retail stores into the packet FUN_06d96300 just built (06d8a5f4 on). Its constructor
    // (FUN_06d9c540) zeroed the name pair and built the relation list's dummy node
    // (06d9c59f), so there is no whole-payload write here: that would put a null over the
    // constructor's dummy and leak it.
    FPayloadServerSetGroupFeelings* Wire = Ch2Payload<FPayloadServerSetGroupFeelings>(Packet);

    // 06d8a5fa..06d8a609: `OR ECX,-1 ; REPNE SCASB ; NOT ECX` is strlen+1, unbounded, and
    // that count -- the terminator included -- is both the block size and packet+0x24.
    // FUN_06d91850: bare operator new(n) (06d91869), strncpy(block, src, n) (06d91878),
    // count stored (06d91881). No zero-length test: an empty name still ships one byte.
    unsigned int Length = 0;
    while (Source->Name[Length] != '\0') {
        ++Length;
    }
    const unsigned int Total = Length + 1;
    char* Block = static_cast<char*>(::operator new(Total, std::nothrow));
    for (unsigned int i = 0; i < Total; ++i) {
        Block[i] = Source->Name[i];
    }
    Wire->Name = Block;                                      // 06d91875
    Wire->NameSize = Total;                                  // 06d91881

    // 06d8a612..06d8a68f: a stack list (dummy at 06d8a618), one bare new(8) copy per
    // relation pushed by FUN_06d8cad0 while the 16-bit index is below the byte at
    // src+0x14, re-read every pass (06d8a686); the array at src+0x10 is read untested
    // (06d8a649). FUN_06d91ca0 (06d8a69a) then appends a fresh copy of every value to the
    // packet's list, and 06d8a6b1.. frees the stack list. The two hops collapse into one
    // push_back per element onto the constructor's list (rpc_containers.h, relation list).
    FRpcRelationList* List = &Wire->Relations;
    const unsigned int Count = Source->RelationCount;
    for (unsigned int i = 0; i < Count; ++i) {
        RpcRelationListPushBack(List,
            reinterpret_cast<const FRpcRelation&>(Source->Relations[i]));
    }
    return true;
}

static bool DeliverSetGroupFeelings(const FPacketBase* Packet, IClient* Client,
                                    IChatClient*) {
    const FPayloadServerSetGroupFeelings Wire =
        RpcPayloadRead<FPayloadServerSetGroupFeelings>(Packet, 0);

    FServerSetGroupFeelingsInfo Info;
    for (unsigned int i = 0; i < 0x10; ++i) {
        Info.Name[i] = '\0';
    }
    Info.Relations = nullptr;
    Info.RelationCount = 0;
    Info.Pad_0x15[0] = 0;
    Info.Pad_0x15[1] = 0;
    Info.Pad_0x15[2] = 0;

    // 06d815d9..06d815f1 `strncpy(local, packet+0x20, packet+0x24)`: no null test, no
    // bound -- retail trusts the count the sender wrote (strlen+1, so the copy always
    // carries the terminator).
    strncpy(Info.Name, Wire.Name, Wire.NameSize);

    const FRpcRelationList* List =
        reinterpret_cast<const FRpcRelationList*>(Packet->Payload() + 0x08);
    // 06d815fa..06d81601 FUN_06d91d20(packet, &local): the packet's list is copied into a
    // stack list -- its dummy read untested (06d91d21..06d91d25; the packet constructor
    // always builds it, 06d9c59f), a fresh new(8) per value, NULL VALUES SKIPPED
    // (06d91d37 JZ) -- so the local size counts only the non-null ones. The port reads the
    // packet's list in place, which makes that count explicit here.
    unsigned int Size = 0;
    for (const FRpcRelationNode* Node = List->Head->Next; Node != List->Head;
         Node = Node->Next) {
        if (Node->Value != nullptr) {
            ++Size;
        }
    }
    Info.RelationCount = (unsigned char)(Size & 0xffu);      // 06d8160c
    FRpcRelation* Items = nullptr;
    if (Size != 0) {                                         // 06d81614 JBE, on the dword
        // 06d8161a: bare operator new(Size * 8). Retail copies the packet's list into a
        // local one (FUN_06d91d20, 06d81601) and walks THAT (06d81632..06d8166d): the
        // whole list, a 16-bit index (06d81641 `MOV AX,DI`), null values skipped
        // (06d81637) -- no bound against Size.
        Items = static_cast<FRpcRelation*>(
            ::operator new(sizeof(FRpcRelation) * Size, std::nothrow));
        const FRpcRelationNode* Dummy = List->Head;
        unsigned short At = 0;
        for (const FRpcRelationNode* Node = Dummy->Next; Node != Dummy; Node = Node->Next) {
            if (Node->Value != nullptr) {
                Items[At] = *Node->Value;
                ++At;
            }
        }
        Info.Relations = reinterpret_cast< ::Reputation::Relation*>(Items);
    }

    Client->SendServerSetGroupFeelings(1u, Info);

    // 06d81684..06d816d3: after the slot retail frees only its local list's nodes and
    // dummy, then leaves the case. The relation array is never freed -- retail leaks it
    // on every SETGROUPFEELINGS, and so does the port.
    return true;
}

// ===========================================================================
// ch2 0x34 FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE
// Sender 0x06d8ad90 (IClient+0x11c, RET 8), receiver caseD_34 at 0x06d82494 -> the same
// slot (+0x11c). The state byte is MOVZX'd to a dword going out (06d8adbd) and read back
// as a byte (06d824dc), which is why the payload field is a dword and the struct field a
// byte. The literal 1 travels BY REFERENCE (06d8250c..06d82520 `MOV [ESP+0x1f0],1` then
// `LEA EAX,[ESP+0x1f0] ; PUSH EAX`).
// ===========================================================================
static bool MarshalReplaceMissionObjective(FPacketBase* Packet, const unsigned int* Args,
                                           unsigned int ArgCount) {
    // In place, in retail's order; all four payload dwords are written, nothing is tested.
    const FServerReplaceMissionObjectiveInfo* Source =
        reinterpret_cast<const FServerReplaceMissionObjectiveInfo*>(Args[1]);
    FPayloadServerReplaceMissionObjective* Wire =
        Ch2Payload<FPayloadServerReplaceMissionObjective>(Packet);
    Wire->Field_0x00 = Source->Field_0x00;                   // 06d8adb4 / 06d8adba
    Wire->State = Source->State;                             // 06d8adbd MOVZX / 06d8adcb
    // 06d8adce..06d8ae25: size of the FmtStr at arg+0x04 into +0x08 (06d8ae12), bare
    // operator new, pointer into +0x0c (06d8ae22), FmtStr::flatten.
    Wire->TextBuffer = RpcFlattenFmtStr(Source->Text, Wire->TextSize);
    return true;
}

static bool DeliverReplaceMissionObjective(const FPacketBase* Packet, IClient* Client,
                                           IChatClient*) {
    FPayloadServerReplaceMissionObjective Wire =
        RpcPayloadRead<FPayloadServerReplaceMissionObjective>(Packet, 0);

    FServerReplaceMissionObjectiveInfo Info;
    Info.Field_0x00 = Wire.Field_0x00;                       // 06d824df
    Info.ObjectiveType = 0;
    Info.State = (unsigned char)(Wire.State & 0xffu);        // 06d824dc / 06d824f7
    Info.Pad_0x21[0] = 0;
    Info.Pad_0x21[1] = 0;
    Info.Pad_0x21[2] = 0;
    RpcUnflattenFmtStr(Info.Text, Wire.TextBuffer, Wire.TextSize);   // 06d824fb

    unsigned int One = 1u;
    Client->SendServerReplaceMissionObjective(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x45 FLPACKET_SERVER_SET_MISSION_MESSAGE
// Sender 0x06d8af60 (IClient+0x118, RET 8), receiver caseD_45 at 0x06d82623 -> the same
// slot (+0x118). One FmtStr and one dword. The literal 1 is BY REFERENCE (06d82694).
// ===========================================================================
static bool MarshalSetMissionMessage(FPacketBase* Packet, const unsigned int* Args,
                                     unsigned int ArgCount) {
    // In place, in retail's order; all three payload dwords are written, nothing is tested.
    const FServerSetMissionMessageInfo* Source =
        reinterpret_cast<const FServerSetMissionMessageInfo*>(Args[1]);
    FPayloadServerSetMissionMessage* Wire = Ch2Payload<FPayloadServerSetMissionMessage>(Packet);
    // 06d8af8b..06d8afe3: size of the FmtStr at arg+0x00 into +0x00 (06d8afd0), bare
    // operator new, pointer into +0x04 (06d8afe0), FmtStr::flatten.
    Wire->TextBuffer = RpcFlattenFmtStr(Source->Text, Wire->TextSize);
    Wire->Field_0x08 = Source->Type;                          // 06d8afe9 / 06d8aff4
    return true;
}

static bool DeliverSetMissionMessage(const FPacketBase* Packet, IClient* Client,
                                     IChatClient*) {
    FPayloadServerSetMissionMessage Wire =
        RpcPayloadRead<FPayloadServerSetMissionMessage>(Packet, 0);

    FServerSetMissionMessageInfo Info;
    Info.Type = Wire.Field_0x08;                             // 06d82682 -> S+0x68
    RpcUnflattenFmtStr(Info.Text, Wire.TextBuffer, Wire.TextSize);   // 06d8267c

    unsigned int One = 1u;
    Client->SendServerSetMissionMessage(One, Info);
    return true;
}

// ===========================================================================
// ch2 0x35 FLPACKET_SERVER_SETMISSIONOBJECTIVES
// Sender 0x06d8b020 (IClient+0x120, RET 8), receiver caseD_35 at 0x06d826c9 -> the same
// slot (+0x120). Two top-level FmtStrs, a count, and a list of per-objective records.
//
// The receiver rebuilds the element array with a VC6 vector-new (operator new(n*0x1c + 4),
// count cookie in the first dword, FmtStr ctor per element) and walks the list with the
// LINEAR accessor FUN_06d8fc60 -- so element i costs i list steps. Kept as-is: the
// accessor returns null once the walk runs off the end, and that null path is real.
// The literal 1 is BY REFERENCE (06d8284f..06d82863).
// ===========================================================================
static bool MarshalSetMissionObjectives(FPacketBase* Packet, const unsigned int* Args,
                                        unsigned int ArgCount) {
    const FServerSetMissionObjectivesInfo* Source =
        reinterpret_cast<const FServerSetMissionObjectivesInfo*>(Args[1]);
    // Retail stores into the packet FUN_06d96300 just built (06d8b040 on). Its constructor
    // (FUN_06d9da40) zeroed +0x08..+0x18 and built the record list's dummy node
    // (06d9daad), so there is no whole-payload write here: that would put a null over the
    // constructor's dummy and leak it.
    FPayloadServerSetMissionObjectives* Live =
        reinterpret_cast<FPayloadServerSetMissionObjectives*>(Packet->Payload());
    Live->Field_0x00 = Source->Field_0x00;                   // 06d8b049/06d8b04b
    Live->Field_0x04 = Source->Field_0x3c;                   // 06d8b04e/06d8b058 MOVZX
    // 06d8b05b..06d8b0b6 / 06d8b0bc..06d8b114: size (6 + the per-Val sizes, inlined here),
    // bare operator new, FmtStr::flatten -- size at +0x08/+0x10, block at +0x0c/+0x14.
    Live->TextABuffer = RpcFlattenFmtStr(Source->TextA, Live->TextASize);
    Live->TextBBuffer = RpcFlattenFmtStr(Source->TextB, Live->TextBSize);
    // 06d8b11a `MOV EAX,[EBX+0x8] ; CMP EAX,EDI ; JBE` -- the dword count at arg+0x08 is
    // the only bound; the entry array at arg+0x04 is read untested (06d8b156).
    const unsigned int Count = Source->Count;
    for (unsigned int i = 0; i < Count; ++i) {
        // 06d8b142 operator new(0xc); the new-expression's test (06d8b14a) skips only the
        // zeroing of +0x04/+0x08 -- the stores below go through the pointer either way.
        FRpcMissionObjectiveRecord* Record =
            static_cast<FRpcMissionObjectiveRecord*>(
                ::operator new(sizeof(FRpcMissionObjectiveRecord), std::nothrow));
        if (Record != nullptr) {
            Record->TextSize = 0;                            // 06d8b14e
            Record->TextBuffer = nullptr;                    // 06d8b151
        }
        Record->State = (unsigned char)(Source->Entries[i].State & 0xffu);   // 06d8b15d
        Record->Pad_0x01[0] = 0;
        Record->Pad_0x01[1] = 0;
        Record->Pad_0x01[2] = 0;
        // 06d8b16f..06d8b1b5: size, bare operator new, flatten.
        Record->TextBuffer = RpcFlattenFmtStr(Source->Entries[i].Text, Record->TextSize);
        // FUN_06d8fc10 (06d8b1c0) pushes and bumps packet+0x38 and packet+0x44 together.
        RpcMissionObjectiveListPushBack(&Live->Objectives, Record);
        ++Live->Count;
    }
    return true;
}

static bool DeliverSetMissionObjectives(const FPacketBase* Packet, IClient* Client,
                                        IChatClient*) {
    const FPayloadServerSetMissionObjectives* Wire =
        reinterpret_cast<const FPayloadServerSetMissionObjectives*>(Packet->Payload());

    FServerSetMissionObjectivesInfo Info;
    Info.Field_0x00 = Wire->Field_0x00;                      // 06d82779
    Info.Entries = nullptr;                                  // 06d827ac
    Info.Count = Wire->Count;                                // 06d827b0
    Info.Field_0x3c = (unsigned char)(Wire->Field_0x04 & 0xffu);   // 06d827a2 / 06d827a5
    Info.Pad_0x3d[0] = 0;
    Info.Pad_0x3d[1] = 0;
    Info.Pad_0x3d[2] = 0;

    RpcUnflattenFmtStr(Info.TextA, Wire->TextABuffer, Wire->TextASize);  // 06d8278d
    RpcUnflattenFmtStr(Info.TextB, Wire->TextBBuffer, Wire->TextBSize);  // 06d8279b

    FServerMissionObjectiveEntry* Entries = nullptr;
    const unsigned int Count = Wire->Count;
    if (Count != 0) {                                        // 06d827b4 JBE
        // 06d827c3 vector-new; its test (06d827cf) skips only the element constructors
        // (06d827ee) -- the loop below writes through the result either way.
        Entries = new (std::nothrow) FServerMissionObjectiveEntry[Count];
        for (unsigned int i = 0; i < Count; ++i) {
            // FUN_06d8fc60 (06d82813), used untested (06d82818): the count and the list
            // size are bumped together by the sender (FUN_06d8fc10), so retail never
            // walks past the end.
            const FRpcMissionObjectiveRecord* Record =
                RpcMissionObjectiveListAt(&Wire->Objectives, i);
            Entries[i].State = Record->State;                // 06d82818 MOVZX
            RpcUnflattenFmtStr(Entries[i].Text, Record->TextBuffer, Record->TextSize);
        }
        Info.Entries = Entries;
    }

    unsigned int One = 1u;
    Client->SendServerSetMissionObjectives(One, Info);

    // 06d8286a..06d82892 -- the vector-delete, guarded on both the count and the pointer,
    // and both are RE-READ from the local the slot was handed (06d8286a `CMP [ESP+0x58]`,
    // Info.Count; 06d82870 `MOV EAX,[ESP+0x54]`, Info.Entries), not from the registers
    // that built it.
    if (Info.Count != 0 && Info.Entries != nullptr) {
        delete[] Info.Entries;
    }
    return true;
}

// ===========================================================================
// ch2 0x4d FLPACKET_SERVER_LOADHINT
// Sender 0x06d8a3c0 (IClient+0x144, RET 8), receiver caseD_4d at 0x06d81ba3 -> the same
// slot (+0x144). The serializable object is a CostumeHint; see the long note in
// rpc_payloads.h for why the old "blocked on COMMON.DLL::CostumeHint" reading was stale.
//
// The sender dispatches Serialize (06d8a3ed `CALL [EDX+0x4]`) and CleanUp (06d8a412
// `CALL [EDX+0xc]`) through the object's own vtable, so it takes any BaseHint: the three
// virtuals are declared pure on BaseHint (??_7BaseHint@@6B@ .rdata:063a3700, slots 1..3
// -> _purecall), and IServer::LoadHint passes a BaseHint* (server_pub.cpp).
// ===========================================================================

// 06d81bce..06d81bd4: after COMMON's CostumeHint::CostumeHint() returns, the receiver
// stores rpclocal's OWN vftable (.rdata:06db3740) over the one COMMON's ctor installed.
// That table is the VC6 local copy emitted for a dllimport class: slot 0 is rpclocal's
// scalar deleting dtor FUN_06d83dd0 (COMMON's ~CostumeHint, then rpclocal's operator
// delete FUN_06db1dc2 when bit 0 is set), slots 1..3 are the import thunks 06db1dbc /
// 06db1db6 / 06db1db0 to COMMON's Serialize / UnSerialize / CleanUp. A modern compiler
// does not re-stamp a dllimport class's vptr, so the same object shape comes from a
// derived class local to this module: its ctor runs COMMON's ctor and then stores this
// module's vftable, its deleting dtor runs COMMON's dtor and frees with this module's
// operator delete, and the three hint methods are inherited unchanged. No members are
// added, so the object stays 0x3c bytes.
struct FRpcLocalCostumeHint final : public CostumeHint
{
};

static_assert(sizeof(FRpcLocalCostumeHint) == 0x3C, "operator new(0x3c) at 06d81bad");

static bool MarshalLoadHint(FPacketBase* Packet, const unsigned int* Args,
                            unsigned int ArgCount) {
    BaseHint* Source = reinterpret_cast<BaseHint*>(Args[1]);
    FPayloadServerLoadHint* Wire = Ch2Payload<FPayloadServerLoadHint>(Packet);

    // 06d8a3e2 seeds the stack pointer with 0 before the virtual call, then
    // 06d8a3ed `CALL [EDX+0x4]` -- Serialize(&OutBuf), returning the byte count.
    unsigned char* Serialized = nullptr;
    const unsigned int Size = Source->Serialize(&Serialized);
    // FUN_06d90a00 (06d8a3fa), called with no test of either value: bare operator
    // new(Size) (06d90a0b), a dword-then-byte copy, the block at packet+0x20, Size at
    // packet+0x24.
    {
        void* Block = ::operator new(Size, std::nothrow);
        unsigned char* Dest = static_cast<unsigned char*>(Block);
        for (unsigned int i = 0; i < Size; ++i) {
            Dest[i] = Serialized[i];
        }
        Wire->Buffer = Block;                                // 06d90a14 [+0x20]
        Wire->Size = Size;                                   // 06d90a2e [+0x24]
    }
    Wire->Kind = Source->Kind;                               // 06d8a3ff / 06d8a406 [+0x28]

    // Retail stores straight into the packet and has no failure path, so neither does this.
    Source->CleanUp(Serialized);                             // 06d8a412 `CALL [EDX+0xc]`
    return true;
}

static bool DeliverLoadHint(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerLoadHint Wire = RpcPayloadRead<FPayloadServerLoadHint>(Packet, 0);

    CostumeHint* Hint = nullptr;
    if (Wire.Kind == 1) {                                    // 06d81baa `DEC EAX ; JNZ`
        // 06d81bad operator new(0x3c); the new-expression's test (06d81bbd / 06d81bca)
        // skips only the constructor (and the vftable store at 06d81bd4). UnSerialize is
        // then called through the vtable with no test (06d81bde `MOV EDI,[ESI]`,
        // 06d81c01 `CALL [EDI+0x8]`).
        Hint = new (std::nothrow) FRpcLocalCostumeHint;
        Hint->UnSerialize(static_cast<unsigned char*>(Wire.Buffer), Wire.Size);
    }

    // 06d81c0a..06d81c20: the hint pointer goes out even when it is null, and the literal
    // 1 travels BY REFERENCE (`MOV [ESP+0x1d4],1 ; LEA EAX,[ESP+0x1d4] ; PUSH EAX`).
    // No implementer null-tests it; the exe's LAB_0053de70 reads Kind (+4) through it first.
    unsigned int One = 1u;
    Client->SendServerLoadHint(One, Hint);

    if (Hint != nullptr) {
        delete Hint;                                         // 06d81c34 `CALL [EDX]`, arg 1
    }
    return true;
}

// ===========================================================================
// ch2 0x55 FLPACKET_SERVER_OBJECTCARGOUPDATE
// Sender 0x06d8c0f0 (IClient+0x158, RET 0xc), receiver caseD_55 at 0x06d82abd -> the same
// slot (+0x158). The receiver rebuilds a real EquipDesc through seven COMMON.DLL entry
// points; see the note in rpc_payloads.h for why the old "blocked on five Common.dll
// setters" reading was stale and which single method really was missing (set_count).
// ===========================================================================
static bool MarshalObjectCargoUpdate(FPacketBase* Packet, const unsigned int* Args,
                                     unsigned int ArgCount) {
    // In place, in retail's order. The pad +0x11..+0x13 is neither written here nor seeded
    // by the constructor FUN_06d9ee70 (06d9ee9a stores only the byte at +0x30).
    const EquipDesc* Source = reinterpret_cast<const EquipDesc*>(Args[2]);
    FPayloadServerObjectCargoUpdate* Wire = Ch2Payload<FPayloadServerObjectCargoUpdate>(Packet);
    Wire->ObjectId = Args[1];                                // 06d8c111 / 06d8c115
    Wire->EquipId = Source->Id;                              // 06d8c118 / 06d8c120, word
    Wire->ArchId = Source->ArchId;                           // 06d8c124 / 06d8c12b
    Wire->Count = (unsigned short)(Source->Count & 0xffff);  // 06d8c12e / 06d8c136, WORD-truncated
    Wire->Status = Source->Health;                           // 06d8c13a FLD / 06d8c141 FSTP
    // 06d8c144 / 06d8c14f: the byte at EquipDesc+0x18 moved as is, no normalisation.
    Wire->bMission = *reinterpret_cast<const unsigned char*>(&Source->bMission);
    return true;
}

static bool DeliverObjectCargoUpdate(const FPacketBase* Packet, IClient* Client,
                                     IChatClient*) {
    const FPayloadServerObjectCargoUpdate Wire =
        RpcPayloadRead<FPayloadServerObjectCargoUpdate>(Packet, 0);

    EquipDesc Desc;                                          // 06d82ac1, the real ctor
    Desc.set_id(Wire.EquipId);                               // 06d82ad2
    Desc.set_arch_id(Wire.ArchId);                           // 06d82ae0
    Desc.set_count((int)Wire.Count);                         // 06d82aef
    Desc.set_status(Wire.Status);                            // 06d82afd
    Desc.make_internal();                                    // 06d82b07
    Desc.set_equipped(false);                                // 06d82b14, the literal 0
    // 06d82b1a `MOV AL,[EBP+0x30] ; CMP AL,BL ; SETNZ AL ; MOV [ESP+0x68],AL` -- retail
    // writes EquipDesc+0x18 straight, without a setter, and normalises to 0/1 first.
    Desc.bMission = (Wire.bMission != 0);

    unsigned int One = 1u;
    Client->SendServerObjectCargoUpdate(One, Wire.ObjectId, Desc);
    return true;
}

// ===========================================================================
// ch2 0x4f FLPACKET_SERVER_SENDCOMM
// Sender 0x06d88fc0 (IClient+0x110, RET 0x5c == 23 args), receiver caseD_4f at
// 0x06d8306c. Layout and the per-field accessor list are on FPayloadServerSendComm.
// ===========================================================================
// The dword the sender's Ids loop reads for index I -- `MOV ESI,[ESP + ECX*4 + 0x24]` at
// 06d89047. With ESP0 the address of the return address on entry, the frame is:
// 06d88fc0 SUB ESP,0x38 / 06d88fc3 PUSH EBP / 06d89004..06d89006 three PUSHes put ESP at
// ESP0-0x48, and 06d89007..06d89014 copied thirteen dwords A4..A16 from ESP0+0x14 into
// ESP0-0x34..ESP0-0x04. The load address is ESP0-0x24+4*I, so
//   I 0..8   ESP0-0x24..ESP0-0x04  the scratch copy, A8..A16
//   I 9      ESP0+0x00             the return address into the caller
//   I 10..32 ESP0+0x04..ESP0+0x5c  the arguments themselves again, A0..A22
//   I >= 33  ESP0+0x60 onward      the CALLER's frame -- not determinable from here.
// Args carries the 23 arguments plus the return address at [23] (the proxy's SendServerSendComm puts
// it there with _ReturnAddress()). For I >= 33 the port reads Args[23] and then past the
// array, i.e. its own stack, where retail reads Server.dll's: garbage in both, and by then both have
// already written past the 0x78-byte packet (Ids[15] is payload+0x58, the block's end).
static unsigned int RpcSendCommFrameDword(const unsigned int* Args, int Index) {
    if (Index <= 8) {
        return Args[8 + Index];
    }
    if (Index == 9) {
        return Args[23];
    }
    return Args[Index - 10];
}

static bool MarshalSendComm(FPacketBase* Packet, const unsigned int* Args,
                            unsigned int ArgCount) {
    // Retail stores straight into the constructed packet (06d88fe7..06d890db), so the
    // port does too -- the Ids loop below can run past the array into the fields after
    // it, and the old-buffer test at 06d89071 reads whatever that left.
    FPayloadServerSendComm* Wire =
        reinterpret_cast<FPayloadServerSendComm*>(Packet->Payload());
    Wire->Field_0x00 = Args[1];                              // 06d88fe7
    Wire->Field_0x04 = Args[2];                              // 06d88ff2
    Wire->Field_0x08 = Args[3];                              // 06d88ffd

    Wire->Body.Field_0x00 = Args[4];                         // 06d89021
    Wire->Body.Field_0x04 = Args[5];                         // 06d89027
    Wire->Body.Field_0x08 = Args[6];                         // 06d8902e
    Wire->Body.Field_0x0c = Args[7];                         // 06d89038
    Wire->Body.Count = (int)Args[16];                        // 06d8903f
    // 06d89007..06d89014 copies A4..A16 into a 13-dword scratch block; 06d89047..06d89056
    // then stores scratch[4 + i] -- A8 onwards -- into Ids[i] while i < Count, the bound
    // RE-READ from payload+0x3c every pass (06d8904d) and compared SIGNED (06d89056 JL).
    // No clamp to the eight-entry array: Ids[8] is Count itself (it receives A16 again),
    // Ids[9..14] are the fields stored below, and past that retail writes beyond the
    // payload. For i > 8 retail reads past its scratch block into the rest of its frame;
    // RpcSendCommFrameDword gives the same dword (see there).
    unsigned int* const IdSlots = Wire->Body.Ids;
    for (int i = 0; i < Wire->Body.Count; ++i) {
        IdSlots[i] = RpcSendCommFrameDword(Args, i);
    }

    Wire->Field_0x40 = Args[17];                             // 06d89063
    // 06d8906f..06d8907b: the old pointer at payload+0x44 is tested and freed.
    if (Wire->Buffer != nullptr) {
        ::operator delete(Wire->Buffer);                     // 06d89076
    }
    // 06d8907e..06d890b2: bare operator new(A19 << 2) whatever A19's sign (06d8908d),
    // then A19 dwords copied out of A18, untested, while A19 > 0 (06d89099 JLE).
    const int BufferCount = (int)Args[19];
    Wire->Buffer = static_cast<unsigned int*>(
        ::operator new((unsigned int)BufferCount << 2, std::nothrow));      // 06d89092
    // 06d8909b `MOV byte ptr [EDI+0x68],BL` -- the count is truncated to a byte here and
    // sign-extended back by MOVSX at 06d84200. Both halves are reproduced verbatim.
    Wire->BufferCount = (unsigned char)((unsigned int)BufferCount & 0xffu);
    const unsigned int* Source = reinterpret_cast<const unsigned int*>(Args[18]);
    for (int i = 0; i < BufferCount; ++i) {
        Wire->Buffer[i] = Source[i];                         // 06d890ac
    }

    Wire->Field_0x4c = Args[20];                             // 06d890c6
    Wire->Field_0x50Bits = Args[21];                         // 06d890d4, FLD/FSTP
    Wire->Field_0x54 = (unsigned char)(Args[22] & 0xffu);    // 06d890db
    return true;
}

static bool DeliverSendComm(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerSendComm Wire = RpcPayloadRead<FPayloadServerSendComm>(Packet, 0);
    // 06d84200 `MOVSX EAX,byte ptr [ECX+0x68]` -- SIGNED, so 0x80..0xff arrive negative.
    const int BufferCount = (int)(signed char)Wire.BufferCount;
    // The thirteen Body dwords are the by-value Costume, in its own order: PartIds[4],
    // AccessoryIds[8], AccessoryCount.
    Costume CommCostume;
    CommCostume.PartIds[0] = Wire.Body.Field_0x00;
    CommCostume.PartIds[1] = Wire.Body.Field_0x04;
    CommCostume.PartIds[2] = Wire.Body.Field_0x08;
    CommCostume.PartIds[3] = Wire.Body.Field_0x0c;
    for (unsigned int i = 0; i < 8; ++i) {
        CommCostume.AccessoryIds[i] = Wire.Body.Ids[i];
    }
    CommCostume.AccessoryCount = (unsigned int)Wire.Body.Count;
    float Duration = 0.0f;
    RpcCh2SetFloatBits(Duration, Wire.Field_0x50Bits);
    Client->SendServerSendComm(1u,
                               Wire.Field_0x00, Wire.Field_0x04, Wire.Field_0x08,
                               CommCostume,
                               Wire.Field_0x40,
                               Wire.Buffer,
                               BufferCount,
                               Wire.Field_0x4c,
                               Duration,
                               // 06d83083 `PUSH 0x0` -- the last argument is a literal,
                               // NOT payload+0x54, which retail writes and never reads.
                               false);
    return true;
}

// ===========================================================================
// ch2 0x42 FLPACKET_SERVER_CREATESOLAR and 0x04 FLPACKET_SERVER_CREATESHIP
//
// Both start with the 0x7c-byte create-object header (rpc_containers.h) and both gate
// their reputation tail on SinglePlayer().
// ===========================================================================
// Both records are the ONE type Server.dll builds and sends -- CSolarCreateInfo /
// CShipCreateInfo from src/include/server_createinfo.h -- read here through their named
// members. Retail keeps each receiver's destination in a file static,
// .data:0x06dbb838 (CREATESOLAR) and .data:0x06dbb940 (CREATESHIP), constructed on the
// first packet behind a bit of .data:0x06dbb914 (bit 3 at 06d82c6f, bit 1 at 06d81fcf),
// destroyed through an atexit thunk (06d84230 / 06d84250 -> 06d843b0), and CLEARED --
// not rebuilt -- after every slot call. A function-local static is that same lazy
// construction plus atexit destruction, so each receiver keeps one.

static bool MarshalCreateSolar(FPacketBase* Packet, const unsigned int* Args,
                               unsigned int ArgCount) {
    const CSolarCreateInfo* Source = reinterpret_cast<const CSolarCreateInfo*>(Args[1]);
    FPayloadServerCreateSolar* Wire =
        reinterpret_cast<FPayloadServerCreateSolar*>(Packet->Payload());
    RpcCreateObjectHeaderWrite(&Wire->Common, *Source);   // 06d891d3
    Wire->Field_0x7c = Source->Field_0xad;                   // 06d891d8
    Wire->Field_0x7d = Source->bMarkedBy;                    // 06d891e8 / 06d891f5 (src+0xa8)
    // Nothing else is stored before the SinglePlayer() test. The name block +0x80..+0x8c
    // keeps the constructor's zeroes (FUN_06d9e630 06d9e6d2..06d9e6e4); the pad at +0x7e
    // and Affiliation / Attitude (+0x90 / +0x94) are not seeded by it, so in single
    // player they ship as the factory's operator new left them -- in retail and here
    // alike (the receiver reads them only when !SinglePlayer(), 06d82dab).
    if (SinglePlayer()) {                                    // 06d891fb
        return true;
    }
    Reputation::Vibe::GetAffiliation(Wire->Common.VibeGroupId,
                                     Wire->Affiliation, false);          // 06d8921d
    Reputation::Vibe::AttitudeTowards(Wire->Common.VibeGroupId,
                                      Source->VibeOtherGroupId,
                                      Wire->Attitude);                   // 06d89239 (src+0xb0)
    FmtStr NameA;
    FmtStr NameB;
    wchar_t const* DisplayName = nullptr;
    Reputation::Vibe::GetName(Wire->Common.VibeGroupId, NameA, NameB, DisplayName);
    // 06d892e6. CREATESOLAR throws the wide name away -- only CREATESHIP ships it.
    Wire->NameABuffer = RpcFlattenFmtStr(NameA, Wire->NameASize);
    Wire->NameBBuffer = RpcFlattenFmtStr(NameB, Wire->NameBSize);
    return true;
}

static bool DeliverCreateSolar(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCreateSolar* Wire =
        reinterpret_cast<const FPayloadServerCreateSolar*>(Packet->Payload());
    // caseD_42's one-time init (06d82c68..06d82d7d) is CSolarCreateInfo's constructor:
    // the base part (06d82c83..06d82d54, both list sentinels at 06d82cfa / 06d82d22)
    // and then +0xac/+0xad = 0, +0xb0 = -1 (06d82d61..06d82d6d) and the vftable
    // .rdata:06db3738 (06d82d73).
    static CSolarCreateInfo Static;
    RpcCreateObjectHeaderRead(Static, &Wire->Common);   // 06d82d8b
    Static.Field_0xad = Wire->Field_0x7c;                    // 06d82d90 / 06d82d96
    Static.bMarkedBy = Wire->Field_0x7d;                     // 06d82d9c / 06d82da5 (+0xa8)
    if (!SinglePlayer()) {                                   // 06d82dab
        FmtStr NameA;
        FmtStr NameB;
        RpcUnflattenFmtStr(NameA, Wire->NameABuffer, Wire->NameASize);   // 06d82ec7
        RpcUnflattenFmtStr(NameB, Wire->NameBBuffer, Wire->NameBSize);   // 06d82ede
        Reputation::Vibe::EnsureExists(Wire->Common.VibeGroupId);        // 06d82ee4
        Reputation::Vibe::SetName(Wire->Common.VibeGroupId, NameA, NameB, nullptr);
        Reputation::Vibe::SetAffiliation(Wire->Common.VibeGroupId,
                                         Wire->Affiliation, false);      // 06d82f0b
        Reputation::Vibe::SetAttitudeTowardsPlayer(Wire->Common.VibeGroupId,
                                                   Wire->Attitude);      // 06d82f1b
    }
    Client->SendServerCreateSolar(1u, Static);               // 06d82f5a
    // 06d82f60..06d8300d is the base clear() inlined -- the static type has no override
    // (its table's slot 0 is 06d83ed0): +0x10 = -1, +0x04/+0x08/+0x0c/+0x48/+0x6c = 0,
    // +0xa8 = 0, the vector's _Last := _First, and both lists emptied down to their
    // sentinels.
    Static.clear();
    return true;
}

static bool MarshalCreateShip(FPacketBase* Packet, const unsigned int* Args,
                              unsigned int ArgCount) {
    const CShipCreateInfo* Source = reinterpret_cast<const CShipCreateInfo*>(Args[1]);
    FPayloadServerCreateShip* Wire =
        reinterpret_cast<FPayloadServerCreateShip*>(Packet->Payload());
    RpcCreateObjectHeaderWrite(&Wire->Common, *Source);   // 06d89453
    Wire->Field_0x7c = Source->bUsingTradelane;              // 06d8945c / 06d89462 (src+0xdc)
    // src+0xb8..+0xcc, the two Vectors: all six dwords move with a plain MOV pair
    // (06d894aa..06d89504), no FLD/FSTP, so all six are bit copies.
    std::memcpy(&Wire->Field_0x80, &Source->Velocity.x, sizeof(Wire->Field_0x80));  // 06d894aa / 06d894b4
    std::memcpy(&Wire->Field_0x84, &Source->Velocity.y, sizeof(Wire->Field_0x84));  // 06d894ba / 06d894c4
    std::memcpy(&Wire->Field_0x88, &Source->Velocity.z, sizeof(Wire->Field_0x88));  // 06d894ca / 06d894d4
    std::memcpy(&Wire->Field_0x8c, &Source->AngularVelocity.x, sizeof(Wire->Field_0x8c));  // 06d894da / 06d894e4
    std::memcpy(&Wire->Field_0x90, &Source->AngularVelocity.y, sizeof(Wire->Field_0x90));  // 06d894ee / 06d894f4
    std::memcpy(&Wire->Field_0x94, &Source->AngularVelocity.z, sizeof(Wire->Field_0x94));  // 06d894fa / 06d89504
    std::memcpy(&Wire->Field_0x98, &Source->Throttle, sizeof(Wire->Field_0x98));   // 06d8949a / 06d894a4 (src+0xd0)
    Wire->Field_0x9c = Source->OwnerPlayer;                  // 06d8946c / 06d89472 (src+0xac)
    Wire->Field_0xa0 = (unsigned short)(Source->Field_0xb0 & 0xffffu);        // 06d89478 / 06d89483
    Wire->Field_0xa2 = (unsigned char)(Source->ReputationRank & 0xffu);       // 06d8948a / 06d89494
    Wire->Field_0xa4 = (unsigned short)(Source->Field_0xd8 & 0xffffu);        // 06d8951a / 06d89528
    Wire->Field_0xc8 = Source->bMarkedBy;                    // 06d8950a / 06d89514 (src+0xa8)
    // Nothing else is stored. The name block, the display name and Affiliation /
    // Attitude (+0xa8..+0xc4) keep the constructor's zeroes (FUN_06d9cc60
    // 06d9cd47..06d9cd71); the pads +0x7d, +0xa3, +0xa6 and +0xc9 are seeded by nobody,
    // in retail or here.
    if (SinglePlayer()) {                                    // 06d8952f
        return true;
    }
    FmtStr NameA;
    FmtStr NameB;
    wchar_t const* DisplayName = nullptr;
    Reputation::Vibe::GetName(Wire->Common.VibeGroupId, NameA, NameB, DisplayName);
    // 06d895f1 `CMP EAX,EBX` / 06d895f3 `JZ` then 06d895f6 wcslen: a non-empty
    // display name is shipped INSTEAD of the two flattened FmtStrs, which then stay
    // null/zero.
    unsigned int DisplayLength = 0;
    if (DisplayName != nullptr) {
        while (DisplayName[DisplayLength] != L'\0') {
            ++DisplayLength;
        }
    }
    if (DisplayLength == 0) {
        Wire->NameABuffer = RpcFlattenFmtStr(NameA, Wire->NameASize);
        Wire->NameBBuffer = RpcFlattenFmtStr(NameB, Wire->NameBSize);
    } else {
        // FUN_06d97ce0: pkt+0xd8 = wcslen, pkt+0xdc = operator new((len*2)+2) + wcscpy.
        wchar_t* Copy = static_cast<wchar_t*>(
            ::operator new((DisplayLength * 2u) + 2u, std::nothrow));   // 06d97cfe, bare
        for (unsigned int i = 0; i <= DisplayLength; ++i) {
            Copy[i] = DisplayName[i];
        }
        Wire->DisplayNameLength = DisplayLength;
        Wire->DisplayName = Copy;
    }
    Reputation::Vibe::GetAffiliation(Wire->Common.VibeGroupId,
                                     Wire->Affiliation, false);          // 06d896fa
    Reputation::Vibe::AttitudeTowards(Wire->Common.VibeGroupId,
                                      Source->VibeOtherGroupId,
                                      Wire->Attitude);                   // 06d89716 (src+0xd4)
    return true;
}

static bool DeliverCreateShip(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    const FPayloadServerCreateShip* Wire =
        reinterpret_cast<const FPayloadServerCreateShip*>(Packet->Payload());
    // caseD_4's one-time init (06d81fcf..06d82021) is CShipCreateInfo's constructor:
    // FUN_06d83df0 for the base (06d81fe3), then +0xac/+0xb0/+0xb4/+0xd0 = 0,
    // +0xd4 = -1, +0xd8 = 0, +0xdc = 0 (06d81fed..06d82011) and the vftable
    // .rdata:06db373c (06d82017).
    static CShipCreateInfo Static;
    RpcCreateObjectHeaderRead(Static, &Wire->Common);   // 06d8202f
    Static.bUsingTradelane = Wire->Field_0x7c;               // 06d82034 / 06d8203a (+0xdc)
    Static.OwnerPlayer = Wire->Field_0x9c;                   // 06d8203f / 06d82045 (+0xac)
    Static.Field_0xd8 = (unsigned int)Wire->Field_0xa4;      // 06d8204b MOVZX (+0xd8)
    Static.Field_0xb0 = (unsigned int)Wire->Field_0xa0;      // 06d82058 MOVZX (+0xb0)
    Static.ReputationRank = (unsigned int)Wire->Field_0xa2;  // 06d82064 MOVZX (+0xb4)
    Static.Throttle = Wire->Field_0x98;                      // 06d82071 / 06d82077 (+0xd0)
    std::memcpy(&Static.AngularVelocity.x, &Wire->Field_0x8c,
                sizeof(Wire->Field_0x8c));                   // 06d8207d / 06d82092 (+0xc4)
    Static.AngularVelocity.y = Wire->Field_0x90;             // 06d8208c / 06d82097 (+0xc8)
    Static.AngularVelocity.z = Wire->Field_0x94;             // 06d82083 / 06d8209d (+0xcc)
    std::memcpy(&Static.Velocity.x, &Wire->Field_0x80,
                sizeof(Wire->Field_0x80));                   // 06d820a3 / 06d820b5 (+0xb8)
    Static.Velocity.y = Wire->Field_0x84;                    // 06d820af / 06d820bb (+0xbc)
    Static.Velocity.z = Wire->Field_0x88;                    // 06d820a9 / 06d820c1 (+0xc0)
    Static.bMarkedBy = Wire->Field_0xc8;                     // 06d820c7 / 06d820cd (+0xa8)
    if (!SinglePlayer()) {                                   // 06d820d3
        FmtStr NameA;
        FmtStr NameB;
        RpcUnflattenFmtStr(NameA, Wire->NameABuffer, Wire->NameASize);   // 06d8218c
        RpcUnflattenFmtStr(NameB, Wire->NameBBuffer, Wire->NameBSize);   // 06d821a0
        Reputation::Vibe::EnsureExists(Wire->Common.VibeGroupId);        // 06d821a6
        // 06d821ac: unlike CREATESOLAR this one really does pass the wide name on.
        Reputation::Vibe::SetName(Wire->Common.VibeGroupId, NameA, NameB,
                                  Wire->DisplayName);
        Reputation::Vibe::SetAffiliation(Wire->Common.VibeGroupId,
                                         Wire->Affiliation, false);      // 06d821cd
        Reputation::Vibe::SetAttitudeTowardsPlayer(Wire->Common.VibeGroupId,
                                                   Wire->Attitude);      // 06d821dd
    }
    Client->SendServerCreateShip(1u, Static);                // 06d82216
    // 06d8221c..06d822bb is CShipCreateInfo::clear() inlined (the body of 06d83fa0).
    Static.clear();
    return true;
}

// ===========================================================================
// ch2 0x03 FLPACKET_SERVER_CHARACTERINFO
// Sender 0x06d89890 (IClient+0x034) plus FUN_06d955c0, receiver
// IServerImpl::HandleCharacterInfo 0x06d84490 reached from caseD_3 at 06d81715.
// ===========================================================================
static bool MarshalCharacterInfo(FPacketBase* Packet, const unsigned int* Args,
                                 unsigned int ArgCount) {
    // Slot 13's second argument is the caller's CCharacterSet (Server.dll's
    // IServerImpl::CharacterInfoReq local, src/include/server_characterset.h).
    const CCharacterSet* Source = reinterpret_cast<const CCharacterSet*>(Args[1]);
    // The list's dummy already stands: the packet ctor built it (06d9d52f..06d9d549 idiom,
    // FPacket_FLPACKET_SERVER_CHARACTERINFO), exactly where retail's FUN_06d955c0 finds it.
    FPayloadServerCharacterInfo* Wire =
        reinterpret_cast<FPayloadServerCharacterInfo*>(Packet->Payload());
    // 06d898f0..06d89e8a: the NULL-terminated walk. Retail builds the copy twice (into a
    // stack list, then again into the packet through FUN_06d955c0) and frees the first
    // copy right after the send; every hop re-allocates, so one hop here puts exactly the
    // same bytes in the packet.
    //
    // 06d89914 `CMP ESI,EBX ; JZ LAB_06d89e4c`: a null source value jumps straight to the
    // push-back tail (06d89e4c..06d89e71), which appends local_38 -- the copy made for the
    // PREVIOUS entry -- to the stack list a second time. FUN_06d955c0 then deep-copies
    // both nodes into the packet, so a null entry DUPLICATES the record before it on the
    // wire. The port converts the previous source again, which yields the same bytes.
    // Before any record local_38 is uninitialised stack; the port reads it as null, and
    // FUN_06d955c0 skips a null value (06d95605), so a leading null ships nothing.
    // (Retail's stack list then frees the shared copy twice at 06d89ed0..; the port has
    // no stack list, so there is nothing to double-free.)
    const CCharacterSet::CharacterInfo* Previous = nullptr;
    for (const MetaNode<CCharacterSet::CharacterInfo>* Node = Source->Characters.get_head();
         Node != nullptr; Node = Node->Next) {
        const CCharacterSet::CharacterInfo* Record = Node->Data;
        if (Record == nullptr) {
            Record = Previous;
            if (Record == nullptr) {
                continue;
            }
        }
        // 06d8991f operator new(0x27c); the new-expression's test (06d8992d) skips only
        // the constructor (06d899e1 `XOR EDI,EDI`) -- the conversion runs on the result
        // either way (06d899ef).
        FRpcCharacterInfoWireRecord* Copy = new (std::nothrow) FRpcCharacterInfoWireRecord();
        RpcCharacterInfoRecordToWire(Copy, Record);
        RpcCharacterInfoWireListPushBack(&Wire->Records, Copy);
        Previous = Record;
    }
    Wire->Field_0x00 = Source->SaveFailureCount;              // 06d89e9d / 06d89ea8
    return true;
}

namespace {

// HandleCharacterInfo's own stack std::list<record*> (local_50 allocator byte, local_4c
// head, local_48 size). Built at 06d844c6..06d844e1 -- a bare operator new(0xc) dummy
// linked to itself, size 0 -- and torn down after SendServerCharacterInfo at 06d84aea..06d84b39: every
// node through FUN_06d901f0 (unlink, free, --Size), then the dummy. Nodes only: each
// node's record was already freed inside the loop (06d849fe..06d84a73), right after its
// conversion. Retail's unwind covers the list too (state 1 of FuncInfo_06db6a84).
struct FCharacterInfoLocalList {
    FRpcCharacterInfoWireList List;

    FCharacterInfoLocalList() {
        List.Allocator = 0;          // 06d844cc stores an uninitialised stack byte here
        List.Pad_0x01[0] = 0;
        List.Pad_0x01[1] = 0;
        List.Pad_0x01[2] = 0;
        FRpcCharacterInfoWireNode* Dummy = static_cast<FRpcCharacterInfoWireNode*>(
            ::operator new(sizeof(FRpcCharacterInfoWireNode)));     // 06d844d0
        Dummy->Next = Dummy;                                         // 06d844d5
        Dummy->Prev = Dummy;                                         // 06d844d7
        Dummy->Value = nullptr;      // the dummy's value is never read
        List.Head = Dummy;                                           // 06d844dd
        List.Size = 0;                                               // 06d844e1
    }

    ~FCharacterInfoLocalList() {
        FRpcCharacterInfoWireNode* Node = List.Head->Next;           // 06d84aea..06d84aee
        while (Node != List.Head) {                                  // 06d84af0 / 06d84b2b
            FRpcCharacterInfoWireNode* Erased = Node;
            Node = Node->Next;
            Erased->Prev->Next = Erased->Next;                       // FUN_06d901f0
            Erased->Next->Prev = Erased->Prev;
            ::operator delete(Erased);
            --List.Size;
        }
        ::operator delete(List.Head);                                // 06d84b39
        List.Head = nullptr;                                         // 06d84b41
        List.Size = 0;                                               // 06d84b45
    }

private:
    FCharacterInfoLocalList(const FCharacterInfoLocalList&);
    FCharacterInfoLocalList& operator=(const FCharacterInfoLocalList&);
};

}  // namespace

// IServerImpl::HandleCharacterInfo (0x06d84490) itself: rpclocal.cpp's exported member
// forwards here. Arg is its third parameter, which 06d84a9b loads and 06d84aaf pushes as
// slot 13's first argument; HandlePacket's caseD_3 passes 1 (06d81711 PUSH 0x1).
// No packet test: 06d844ea loads the packet argument and dereferences it untested (unlike
// HandleDamageObject, which does test at 06d84c7a).
__declspec(dllexport) bool RpcHandleCharacterInfo(const FPacketBase* Packet, IClient* Client, unsigned int Arg) {
    const FPayloadServerCharacterInfo* Wire =
        reinterpret_cast<const FPayloadServerCharacterInfo*>(Packet->Payload());
    // 06d844ab..06d844bf: the local CCharacterSet (local_44). FUN_06d852c0 is
    // MetaList(bool) with `PUSH EBP` = 0 (bUseHeap false), and the vtable stored at
    // 06d844b7 is OwnerList<CCharacterSet::CharacterInfo>'s. The object dies at the end
    // of this call -- its destructor (FUN_06d85340 then FUN_06d852e0) frees every
    // record and node after the client has consumed it, as retail's does.
    CCharacterSet Set;

    // 06d844c6..06d844e1: the stack list; 06d844f6 FUN_06d94460(packet, &local) deep-copies
    // the packet's records into it (the dummy at packet+0x28 is read untested -- the
    // packet ctor built it). Everything below reads the COPY, not the packet.
    FCharacterInfoLocalList Local;
    RpcCharacterInfoWireListCopy(&Local.List, &Wire->Records);

    // 06d844fb..06d84514: the local list's dummy and first node.
    for (const FRpcCharacterInfoWireNode* Node = Local.List.Head->Next;
         Node != Local.List.Head; Node = Node->Next) {
        const FRpcCharacterInfoWireRecord* Source = Node->Value;
        // 06d84514 `MOV EDI,[EAX+0x8]`: the node's value is loaded and used untested
        // (FUN_06d94460 never appends a null one).
        //
        // 06d84520 operator new(0x2dc). The test at 06d8452e / 06d84535 is on that
        // RESULT: null skips both the constructor and the conversion (06d84618 `XOR
        // EBX,EBX`, 06d84621 JZ LAB_06d849ad) and the node is still appended, with a
        // NULL value (06d849d8).
        CCharacterSet::CharacterInfo* Record =
            new (std::nothrow) CCharacterSet::CharacterInfo();
        if (Record != nullptr) {
            RpcCharacterInfoRecordFromWire(Record, Source);    // 06d8462a..06d849a5
        }
        // 06d849ad..06d849eb: MetaList::append inlined -- alloc_node (the free-pool
        // branch, then operator new(0xc)), Next = 0, Prev = the old tail, Data = the
        // record; the head takes it when the list was empty.
        Set.Characters.append(Record);
        // 06d849eb..06d84a78: the node's value re-read and, when non-null (06d849fc),
        // the copied wire record torn down on the spot. The node keeps the pointer.
        if (Node->Value != nullptr) {
            RpcCharacterInfoWireRecordFree(Node->Value);
        }
    }
    Set.SaveFailureCount = Wire->Field_0x00;                  // 06d84a93 / 06d84aa2
    Client->SendServerCharacterInfo(Arg, Set);                // 06d84ab0
    return true;
}

// HandlePacket caseD_3 (06d8170c..06d81715): HandleCharacterInfo(packet, DAT_06dbbcd8, 1).
static bool DeliverCharacterInfo(const FPacketBase* Packet, IClient* Client, IChatClient*) {
    return RpcHandleCharacterInfo(Packet, Client, 1u);
}

// ===========================================================================
// The table. Order follows the opcode.
//
// Every entry is self-contained: the payload holds scalars, or a block the packet owns and
// its Destroy frees. Nothing on this channel stores a caller-owned pointer, so nothing here
// needs synchronous delivery -- these types queue and are drained by IServerImpl::Update,
// which is what retail does.
// ===========================================================================
const FRpcPayloadCodec GChannel2Codecs[] = {
    { 2, 0x01, "FLPACKET_SERVER_CONNECTRESPONSE",              0x04,
      MarshalConnectResponse,        DeliverConnectResponse },
    { 2, 0x02, "FLPACKET_SERVER_LOGINRESPONSE",                0x04,
      MarshalCh2ByteByRef,           DeliverLoginResponse },
    { 2, 0x03, "FLPACKET_SERVER_CHARACTERINFO",               0x10,
      MarshalCharacterInfo,          DeliverCharacterInfo },
    { 2, 0x04, "FLPACKET_SERVER_CREATESHIP",                  0xcc,
      MarshalCreateShip,             DeliverCreateShip },
    { 2, 0x05, "FLPACKET_SERVER_DAMAGEOBJECT",                 (unsigned short)sizeof(FPayloadServerDamageObject),
      MarshalDamageObject,           DeliverDamageObject },
    { 2, 0x06, "FLPACKET_SERVER_DESTROYOBJECT",                0x08,
      MarshalDestroyObject,          DeliverDestroyObject },
    { 2, 0x07, "FLPACKET_SERVER_LAUNCH",                       0x28,
      MarshalLaunch,                 DeliverLaunch },
    { 2, 0x08, "FLPACKET_SERVER_CHARSELECTVERIFIED",           0x10,
      MarshalCharSelectVerified,     DeliverCharSelectVerified },
    { 2, 0x0a, "FLPACKET_SERVER_ACTIVATEOBJECT",               0x0c,
      MarshalActivateObject,         DeliverActivateObject },
    { 2, 0x0b, "FLPACKET_SERVER_LAND",                         0x0c,
      MarshalThreeDwordsByRef,       DeliverLand },
    { 2, 0x0d, "FLPACKET_SERVER_SETSTARTROOM",                 0x08,
      MarshalCh2DwordPair,           DeliverSetStartRoom },
    { 2, 0x0e, "FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST",  0x04,
      MarshalCh2Dword,               DeliverGfCompleteNewsBroadcastList },
    { 2, 0x0f, "FLPACKET_SERVER_GFCOMPLETECHARLIST",           0x04,
      MarshalCh2Dword,               DeliverGfCompleteCharList },
    { 2, 0x10, "FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST",0x04,
      MarshalCh2Dword,               DeliverGfCompleteMissionComputerList },
    { 2, 0x11, "FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST", 0x04,
      MarshalCh2Dword,               DeliverGfCompleteScriptBehaviorList },
    { 2, 0x13, "FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST",  0x04,
      MarshalCh2Dword,               DeliverGfCompleteAmbientScriptList },
    { 2, 0x14, "FLPACKET_SERVER_GFDESTROYNEWSBROADCAST",       0x08,
      MarshalCh2DwordPair,           DeliverGfDestroyNewsBroadcast },
    { 2, 0x15, "FLPACKET_SERVER_GFDESTROYCHARACTER",           0x08,
      MarshalCh2DwordPair,           DeliverGfDestroyCharacter },
    { 2, 0x16, "FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER",     0x08,
      MarshalCh2DwordPair,           DeliverGfDestroyMissionComputer },
    { 2, 0x17, "FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR",      0x08,
      MarshalCh2DwordPair,           DeliverGfDestroyScriptBehavior },
    { 2, 0x19, "FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT",       0x08,
      MarshalCh2DwordPair,           DeliverGfDestroyAmbientScript },
    { 2, 0x1a, "FLPACKET_SERVER_GFSCRIPTBEHAVIOR",             0x08,
      MarshalCh2Blob,                DeliverGfScriptBehavior },
    { 2, 0x1d, "FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER",      0x08,
      MarshalCh2Blob,                DeliverGfUpdateMissionComputer },
    { 2, 0x1e, "FLPACKET_SERVER_GFUPDATENEWSBROADCAST",        0x08,
      MarshalCh2Blob,                DeliverGfUpdateNewsBroadcast },
    { 2, 0x1f, "FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT",        0x08,
      MarshalCh2BlobReversed,        DeliverGfUpdateAmbientScript },
    { 2, 0x20, "FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE",    0x08,
      MarshalCh2Blob,                DeliverGfMissionVendorAcceptance },
    { 2, 0x21, "FLPACKET_SERVER_SYSTEM_SWITCH_OUT",            0x08,
      MarshalDwordPairByRef,         DeliverSystemSwitchOut },
    { 2, 0x22, "FLPACKET_SERVER_SYSTEM_SWITCH_IN",             0x24,
      MarshalSystemSwitchIn,         DeliverSystemSwitchIn },
    { 2, 0x23, "FLPACKET_SERVER_SETSHIPARCH",                  0x04,
      MarshalCh2Dword,               DeliverSetShipArch },
    { 2, 0x24, "FLPACKET_SERVER_SETEQUIPMENT",                 0x04,
      MarshalSetEquipment,           DeliverSetEquipment },
    { 2, 0x25, "FLPACKET_SERVER_SETCARGO",                     0x04,
      MarshalSetCargo,               DeliverSetCargo },
    { 2, 0x26, "FLPACKET_SERVER_GFUPDATECHAR",                 0x08,
      MarshalCh2Blob,                DeliverGfUpdateChar },
    { 2, 0x27, "FLPACKET_SERVER_REQUESTCREATESHIPRESP",        0x08,
      MarshalRequestCreateShipResp,  DeliverRequestCreateShipResp },
    { 2, 0x28, "FLPACKET_SERVER_CREATELOOT",                   0x14,
      MarshalCreateLoot,             DeliverCreateLoot },
    { 2, 0x29, "FLPACKET_SERVER_SETREPUTATION",                0x48,
      MarshalSetReputation,          DeliverSetReputation },
    { 2, 0x2a, "FLPACKET_SERVER_ADJUSTATTITUDE",               0x08,
      MarshalAdjustAttitude,         DeliverAdjustAttitude },
    { 2, 0x2b, "FLPACKET_SERVER_SETGROUPFEELINGS",             0x14,
      MarshalSetGroupFeelings,       DeliverSetGroupFeelings },
    { 2, 0x2c, "FLPACKET_SERVER_CREATEMINE",                   0x48,
      MarshalCreateMine,             DeliverCreateMine },
    { 2, 0x2d, "FLPACKET_SERVER_CREATECOUNTER",                0x48,
      MarshalCreateCounter,          DeliverCreateCounter },
    { 2, 0x2e, "FLPACKET_SERVER_SETADDITEM",                   0x20,
      MarshalSetAddItem,             DeliverSetAddItem },
    { 2, 0x2f, "FLPACKET_SERVER_SETREMOVEITEM",                0x0c,
      MarshalSetRemoveItem,          DeliverSetRemoveItem },
    { 2, 0x30, "FLPACKET_SERVER_SETCASH",                      0x04,
      MarshalCh2Dword,               DeliverSetCash },
    { 2, 0x31, "FLPACKET_SERVER_EXPLODEASTEROIDMINE",          0x10,
      MarshalFourDwordsByRef,        DeliverExplodeAsteroidMine },
    { 2, 0x32, "FLPACKET_SERVER_REQUESTSPACESCRIPT",           0x1c,
      MarshalRequestSpaceScript,     DeliverRequestSpaceScript },
    { 2, 0x33, "FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE",     0x0c,
      MarshalSetMissionObjectiveState, DeliverSetMissionObjectiveState },
    { 2, 0x34, "FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE",      0x10,
      MarshalReplaceMissionObjective, DeliverReplaceMissionObjective },
    { 2, 0x35, "FLPACKET_SERVER_SETMISSIONOBJECTIVES",         0x28,
      MarshalSetMissionObjectives,   DeliverSetMissionObjectives },
    { 2, 0x37, "FLPACKET_SERVER_CREATEGUIDED",                 0x38,
      MarshalCreateGuided,           DeliverCreateGuided },
    { 2, 0x38, "FLPACKET_SERVER_ITEMTRACTORED",                0x0c,
      MarshalItemTractored,          DeliverItemTractored },
    { 2, 0x39, "FLPACKET_SERVER_SCANNOTIFY",                   0x08,
      MarshalCh2DwordPair,           DeliverScanNotify },
    { 2, 0x3c, "FLPACKET_SERVER_REPAIROBJECT",                 0x0c,
      MarshalRepairObject,           DeliverRepairObject },
    { 2, 0x3d, "FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE",      0x0c,
      MarshalRemoteObjectCargoUpdate, DeliverRemoteObjectCargoUpdate },
    { 2, 0x3e, "FLPACKET_SERVER_SETNUMKILLS",                  0x04,
      MarshalCh2Dword,               DeliverSetNumKills },
    { 2, 0x3f, "FLPACKET_SERVER_SETMISSIONSUCCESSES",          0x04,
      MarshalCh2Dword,               DeliverSetMissionSuccesses },
    { 2, 0x40, "FLPACKET_SERVER_SETMISSIONFAILURES",           0x04,
      MarshalCh2Dword,               DeliverSetMissionFailures },
    { 2, 0x41, "FLPACKET_SERVER_BURNFUSE",                     0x1c,
      MarshalBurnFuse,               DeliverBurnFuse },
    { 2, 0x42, "FLPACKET_SERVER_CREATESOLAR",                 0x98,
      MarshalCreateSolar,            DeliverCreateSolar },
    { 2, 0x43, "FLPACKET_SERVER_SET_STORY_CUE",                0x04,
      MarshalCh2DwordByRef,          DeliverSetStoryCue },
    { 2, 0x44, "FLPACKET_SERVER_REQUEST_RETURNED",             0x08,
      MarshalRequestReturned,        DeliverRequestReturned },
    { 2, 0x45, "FLPACKET_SERVER_SET_MISSION_MESSAGE",          0x0c,
      MarshalSetMissionMessage,      DeliverSetMissionMessage },
    { 2, 0x46, "FLPACKET_SERVER_MARKOBJ",                      0x08,
      MarshalMarkObj,                DeliverMarkObj },
    { 2, 0x47, "FLPACKET_SERVER_CFGINTERFACENOTIFICATION",     0x0c,
      MarshalCfgInterfaceNotification, DeliverCfgInterfaceNotification },
    { 2, 0x48, "FLPACKET_SERVER_SETCOLLISIONGROUPS",           0x0c,
      MarshalSetCollisionGroups,     DeliverSetCollisionGroups },
    { 2, 0x49, "FLPACKET_SERVER_SETHULLSTATUS",                0x04,
      MarshalSetHullStatus,          DeliverSetHullStatus },
    { 2, 0x4a, "FLPACKET_SERVER_SETGUIDEDTARGET",              0x0c,
      MarshalSetGuidedTarget,        DeliverSetGuidedTarget },
    { 2, 0x4b, "FLPACKET_SERVER_SET_CAMERA",                   0x24,
      MarshalSetCamera,              DeliverSetCamera },
    { 2, 0x4c, "FLPACKET_SERVER_REVERT_CAMERA",                0x00,
      MarshalCh2Empty,               DeliverRevertCamera },
    { 2, 0x4d, "FLPACKET_SERVER_LOADHINT",                     0x0c,
      MarshalLoadHint,               DeliverLoadHint },
    { 2, 0x4e, "FLPACKET_SERVER_SETDIRECTIVE",                 0x94,
      MarshalSetDirective,           DeliverSetDirective },
    { 2, 0x4f, "FLPACKET_SERVER_SENDCOMM",                     0x58,
      MarshalSendComm,               DeliverSendComm },
    { 2, 0x51, "FLPACKET_SERVER_USE_ITEM",                     0x08,
      MarshalUseItem,                DeliverUseItem },
    { 2, 0x52, "FLPACKET_SERVER_PLAYERLIST",                   0x10,
      MarshalPlayerList,             DeliverPlayerList },
    { 2, 0x53, "FLPACKET_SERVER_FORMATION_UPDATE",             0x10,
      MarshalFormationUpdate,        DeliverFormationUpdate },
    { 2, 0x54, "FLPACKET_SERVER_MISCOBJUPDATE",                (unsigned short)sizeof(FPayloadServerMiscObjUpdate),
      MarshalMiscObjUpdate,          DeliverMiscObjUpdate },
    { 2, 0x55, "FLPACKET_SERVER_OBJECTCARGOUPDATE",            0x14,
      MarshalObjectCargoUpdate,      DeliverObjectCargoUpdate },
    { 2, 0x56, "FLPACKET_SERVER_SENDNNMESSAGE",                0x04,
      MarshalCh2Dword,               DeliverSendNnMessage },
    { 2, 0x57, "FLPACKET_SERVER_SET_MUSIC",                    0x18,
      MarshalSetMusic,               DeliverSetMusic },
    { 2, 0x58, "FLPACKET_SERVER_CANCEL_MUSIC",                 0x00,
      MarshalCh2Empty,               DeliverCancelMusic },
    { 2, 0x59, "FLPACKET_SERVER_PLAY_SOUND_EFFECT",            0x04,
      MarshalCh2Dword,               DeliverPlaySoundEffect },
    { 2, 0x5a, "FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY",      0x04,
      MarshalCh2Byte,                DeliverGfMissionVendorWhyEmpty },
    { 2, 0x5b, "FLPACKET_SERVER_MISSIONSAVEA",                 0x04,
      MarshalCh2Dword,               DeliverMissionSaveA },
};

const unsigned int GChannel2CodecCount =
    (unsigned int)(sizeof(GChannel2Codecs) / sizeof(GChannel2Codecs[0]));
