// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpclocal.cpp -- IServerImpl, the freelancer.exe-facing half of rpclocal.dll.
//
// Retail rpclocal is a BIDIRECTIONAL RPC proxy, not a wrapper: freelancer.exe asks it for
// IServer, Server.dll asks it for IClient, and every call in either direction becomes a
// CDAPacket on one of three channels. The transport that carries them lives beside this
// file: rpc_transport.{h,cpp} (packet base, registry, CDAConnection queues),
// rpc_packet_registry.cpp (all 162 retail packet types), rpc_client_proxy.cpp (the
// 126-slot IClient this DLL exports) and rpc_dispatch.cpp (HandlePacket).
//
// The IServer half below no longer delegates. All 81 serializing IServerImpl slots --
// every slot whose retail body contains a `CALL FUN_06d96300` -- now build a packet and
// hand it to PushToServer, and the pump inside Update() drains the server-bound queue
// into the real Server.dll through RpcDispatchToServer. The five that are left calling
// bin\Server.dll directly (Startup, Shutdown, ElapseTime, Connect, DisConnect) are the
// five that do not allocate a packet in retail either; IServerImpl::ElapseTime
// (0x06d8f370) is three instructions and a `JMP dword ptr [EAX + 0x30]` straight into the
// real IServer.
// Uses the unified IServer interface from common_server.h.

#include "common_server.h"
#include "rpc_transport.h"      // FPacketBase, the registry and the queues
#include "rpc_payloads.h"       // GRealServer / GRealChatServer, read by RpcDispatchToServer
#include "rpc_iclient.h"        // the real 126-slot IClient / IChatClient
#include "rpc_iserver.h"        // IServerImpl, Server, Get*Interface -- the exported ABI
#include "common_misc.h"       // GiveServerRunPermission / WaitForClientRunPermission
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <new>

// ============================================================================
// Global state
// ============================================================================
static HMODULE g_hServerDll = nullptr;
static IServer* g_pServerInterface = nullptr;
static void* g_pChatServerInterface = nullptr;
// retail .data:0x06dbbd58 -- IServerImpl's own copy of the CDAConnection pointer, written
// only by Connect (06d8f3a0) and cleared only by Shutdown (06d8f357); read by Update
// (06d8f409, 06d8f42e) and PushToServer (06d8f460). The object itself is the
// FUN_06d88280 singleton (GRpcConnection, DAT_06dbbb5c); the IClient proxy keeps its own
// copy (DAT_06dbbd1c). See rpc_transport.h.
static FRpcConnection* GServerConnection = nullptr;

// Retail DAT_06dbbccc: our own HMODULE, captured in DllMain(DLL_PROCESS_ATTACH).
// IServerImpl::Startup (0x06d8f1e0) needs it for GetModuleFileNameA.
static HMODULE g_hSelfModule = nullptr;

// ============================================================================
// CDAPacket queueing -- see src/rpclocal/rpc_transport.cpp.
//
// Retail's CDAConnection queues the packet POINTER -- FUN_06d86150 / FUN_06d85e00 push it
// into a std::deque<CDAPacket*> and FUN_06d864a0 / FUN_06d86580 pop it -- and never copies
// packet bytes. The block-copy queue that used to live here moved exactly
// sizeof(CDAPacket) == 0x20 bytes -- the shared base only -- so every concrete type's
// payload from +0x20 on was lost. FPacketQueue / FRpcConnection replace it with the
// retail pointer queue. (The earlier claim that the link runs through CDAPacket+0x08 was
// wrong; see the Field_0x08 comment in rpc_transport.h.)
// ============================================================================

// ============================================================================
// The IServer-direction marshallers.
//
// Channel 3 is client -> server only, so its codec table (GChannel3Codecs, built in
// rpc_payload_ch3.cpp) was derived from THESE senders and is used as-is. Channel 1 is
// bidirectional, and its codec table was derived from the IClient senders -- which for
// some of its types write a DIFFERENT payload than the IServerImpl senders do. Those types
// get a marshaller of their own here, each carrying the listing addresses it was read
// from. Everything else on channel 1 shares the existing codec, verified sender against
// sender (0x06d8e6c0 vs 0x06d8bbb0 for ACTIVATEEQUIP, 0x06d8e710 vs 0x06d8bcb0 for
// GOTRADELANE, 0x06d8ea30 vs 0x06d8bd50 for SETTARGET, and so on).
// ============================================================================

// The four instructions every retail block writer on this side is made of:
// operator_new(N) / REP MOVSD / REP MOVSB, the new bare (06d8ed0a, 06d8e02c). The packet
// owns the block afterwards. Where retail tests the source or the length first (CHATMSG,
// 06d8ed2a), the caller carries that test.
static void* RpcServerCopyBlock(const void* Source, unsigned int InSize) {
    unsigned char* Block = static_cast<unsigned char*>(::operator new(InSize, std::nothrow));
    const unsigned char* Src = static_cast<const unsigned char*>(Source);
    for (unsigned int i = 0; i < InSize; ++i) {
        Block[i] = Src[i];
    }
    return Block;
}

// The argument arrays below carry raw dwords, exactly as retail's stack frame does, so a
// float argument travels as its bit pattern -- a `MOV EAX,[ESP+n]` does not care which.
static unsigned int RpcFloatBits(float Value) {
    unsigned int Bits = 0;
    const unsigned char* Src = reinterpret_cast<const unsigned char*>(&Value);
    unsigned char* Dst = reinterpret_cast<unsigned char*>(&Bits);
    for (unsigned int i = 0; i < 4u; ++i) {
        Dst[i] = Src[i];
    }
    return Bits;
}

// ---------------------------------------------------------------------------
// ch1 0x01 FLPACKET_COMMON_UPDATEOBJECT -- sender IServerImpl::SPObjUpdate 0x06d8dc30.
//
// The IClient sender for this same type reads a 0x2c-byte struct with two floats at +0x20
// and +0x24; this one reads SSPObjUpdateInfo, 0x30 bytes, with a DOUBLE at +0x20 that it
// narrows on the way out. Read off the listing:
//   06d8dc8d  MOV  CL,byte ptr [EAX + 0x2c]   -> 06d8dca7 MOV byte ptr [ESI],CL  (payload+0x00)
//   06d8dc87  MOV  EDX,dword ptr [EAX]        -> 06d8dcb5 MOV [ESI + 0x4],EAX    (payload+0x04)
//   06d8dcb8  CALL FUN_06d8f510(ESI+0x8,  {[EAX+0x14],[EAX+0x18],[EAX+0x1c]})    (payload+0x08)
//   06d8dcc5  CALL FUN_06d8f4f0(ESI+0x14, {[EAX+0x04]..[EAX+0x10]})              (payload+0x14)
//   06d8dc56  FLD  double ptr [EAX + 0x20] ; FSTP float -> 06d8dcce [ESI + 0x24] (payload+0x24)
//   06d8dc90  MOV  EAX,dword ptr [EAX + 0x28] -> 06d8dcd5 MOV [ESI + 0x28],ECX   (payload+0x28)
// ---------------------------------------------------------------------------
static bool MarshalServerObjUpdate(FPacketBase* Packet, const unsigned int* Args,
                                   unsigned int ArgCount) {
    // 06d8dc87 `MOV EDX,dword ptr [EAX]`: the argument is dereferenced untested.
    const SSPObjUpdateInfo* Info = reinterpret_cast<const SSPObjUpdateInfo*>(Args[1]);
    FPayloadCommonUpdateObject Wire;
    Wire.Field_0x00 = Info->Field_0x2c;
    Wire.Pad_0x01[0] = 0;
    Wire.Pad_0x01[1] = 0;
    Wire.Pad_0x01[2] = 0;
    Wire.ObjectId = Info->ObjectId;
    Wire.Position = Info->Position;
    Wire.Orientation = Info->Orientation;
    Wire.Field_0x24 = (float)Info->Timestamp;
    Wire.Field_0x28 = 0.0f;
    RpcPayloadWrite(Packet, 0, Wire);
    // payload+0x28 is a plain dword copy in retail, not a float conversion; the wire field
    // is typed float only because the RECEIVER of the other direction reads it with FLD.
    const unsigned int RawField28 = Info->Field_0x28;
    return RpcPayloadWrite(Packet, 0x28, RawField28);
}

// ---------------------------------------------------------------------------
// ch1 0x05 FLPACKET_COMMON_CHATMSG -- sender IServerImpl::SubmitChat 0x06d8ecd0.
//
// Retail reaches the serializer through [EBP + 0x4] and PushToServer through [EBP - 0x4]:
// EBP is the IChatServer sub-object at IServerImpl+0x04, so +0x4 is the serializer at
// IServerImpl+0x08 and -0x4 is the IServer vtable. Same object, adjusted this.
//
//   06d8ecee  MOV  ESI,[ESP+0xc]   ; argument 3, the message body
//   06d8ecfb  MOV  ECX,[ESP+0x8]   ; argument 2, its LENGTH -- this argument is a byte
//                                  ; count, whatever the port's parameter name says
//   06d8ed08  MOV  [EBX],ECX       ; EBX = packet+0x28  -> payload+0x08 = length
//   06d8ed0a  CALL operator_new    ; 06d8ed13 MOV [EDI+0x2c],EAX -> payload+0x0c = block
//   06d8ed1b  REP MOVSD / 06d8ed25 REP MOVSB
//   06d8ed2a  (both zero when either the pointer or the length is zero)
//   06d8ed38  MOV  [EAX+0x20],ECX  ; argument 4, the TARGET id -> payload+0x00
// payload+0x04 is never written by this sender. Argument 1 (the sender id) and argument 5
// are not serialized at all -- the receiver rebuilds them from the session id and -1.
// Args: { sender, length, body, target, system }.
// ---------------------------------------------------------------------------
static bool MarshalServerChatMsg(FPacketBase* Packet, const unsigned int* Args,
                                 unsigned int ArgCount) {
    FPayloadCommonChatMsg Wire;
    Wire.Field_0x00 = Args[3];
    Wire.Field_0x04 = 0;
    Wire.Size = 0;
    Wire.Buffer = nullptr;
    if (Args[2] != 0 && Args[1] != 0) {
        Wire.Size = Args[1];
        Wire.Buffer = RpcServerCopyBlock(reinterpret_cast<const void*>(Args[2]), Args[1]);
    }
    return RpcPayloadWrite(Packet, 0, Wire);
}

// ---------------------------------------------------------------------------
// The buffer family in the IServer direction -- and it is NOT the same family it is in
// the IClient direction.
//
// The IClient senders of ch1 0x11/0x13/0x17/0x19/0x1a/0x1c (0x06d8b4f0, 0x06d8b540, ...)
// store the caller's raw (buffer, length) pair in the payload (see docs/re/deviations.md,
// the ch1 buffer-family entry). That is NOT true of the IServerImpl
// senders: 0x06d8e000 (0x13), 0x06d8e080 (0x16), 0x06d8e100 (0x18), 0x06d8e1c0 (0x17),
// 0x06d8e240 (0x19), 0x06d8e2c0 (0x1c) and 0x06d8e340 (0x1a) all contain
// `CALL MSVCRT.DLL::operator_new` followed by `REP MOVSD` / `REP MOVSB`, i.e. the packet
// owns a copy. Verified instruction by instruction on 0x06d8e000:
//   06d8e01d  MOV  EAX,[ECX + 0x20]      ; only allocate when the slot is still empty
//   06d8e02c  CALL operator_new(EBX)     ; EBX = argument 3, the length
//   06d8e035  MOV  [EDX + 0x20],EAX      ; payload+0x00 = the block
//   06d8e03f  MOV  byte ptr [EAX+0x28],1 ; payload+0x08 = 1, the "packet owns it" flag
//   06d8e055  REP MOVSD ES:EDI,ESI       ; from argument 2
//   06d8e062  MOV  [ECX + 0x24],EBX      ; payload+0x04 = the length
// The one exception is SET_WEAPON_GROUP (0x06d8dfb0), which is a plain
// `MOV [pkt+0x20],arg2` / `MOV [pkt+0x24],arg3` with no flag byte -- for that one the
// shared MarshalSetWeaponGroup is already correct.
// ---------------------------------------------------------------------------
static bool MarshalServerBufferCopy(FPacketBase* Packet, const unsigned int* Args,
                                    unsigned int ArgCount) {
    FPayloadCommonBuffer Wire;
    Wire.Buffer = RpcServerCopyBlock(reinterpret_cast<const void*>(Args[1]), Args[2]);
    Wire.Size = Args[2];
    Wire.bOwnsBuffer = (unsigned char)1;
    Wire.Pad_0x09[0] = 0;
    Wire.Pad_0x09[1] = 0;
    Wire.Pad_0x09[2] = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

// IServerImpl::StopTradelane 06d8e770 writes ship, entrance and exit at
// 06d8e791 / 06d8e79c / 06d8e7a7. IClient's mirror 06d8bd10 only writes the ship id.
static bool MarshalServerStopTradelane(FPacketBase* Packet, const unsigned int* Args,
                                      unsigned int ArgCount) {
    FPayloadCommonStopTradelane Wire;
    Wire.Field_0x00 = Args[1];
    Wire.Field_0x04 = Args[2];
    Wire.Field_0x08 = Args[3];
    return RpcPayloadWrite(Packet, 0, Wire);
}

// ---------------------------------------------------------------------------
// ch1 0x1b FLPACKET_COMMON_POP_UP_DIALOG -- sender IServerImpl::PopUpDialog 0x06d8e180.
//
// Nothing like its IClient counterpart. The whole body between the allocation and the
// push is two instructions:
//   06d8e19d  MOV  EDX,dword ptr [ESP + param_2 + 0x8]
//   06d8e1a1  MOV  dword ptr [ECX + 0x30],EDX          -> payload+0x10
// and the export name agrees: ?PopUpDialog@IServerImpl@@UAEXII@Z, two unsigned ints,
// RET 0x8. The two flattened FmtStr blocks in this payload belong to the other direction
// only; here payload+0x00..+0x0f stay zero.
// ---------------------------------------------------------------------------
static bool MarshalServerPopUpDialog(FPacketBase* Packet, const unsigned int* Args,
                                     unsigned int ArgCount) {
    const unsigned int Value = Args[1];
    return RpcPayloadWrite(Packet, 0x10, Value);
}

// ============================================================================
// IServerImpl -- the retail 92-slot IServer, IServerImpl's own PushToServer (slot 92),
// plus IChatServer.
//
// Retail rpclocal does NOT delegate: every slot serializes its arguments into a
// CDAPacket and pushes it through PushToServer (vtable +0x170). This port now does the
// same. The shape is the same fourteen instructions in every slot -- read off
// IServerImpl::BaseEnter (0x06d8d480), the shortest of them:
//
//   06d8d482  LEA  EAX,[ESP + 0x4]        ; &OutPacket
//   06d8d489  MOV  ECX,[ESI + 0x8]        ; the serializer, at IServerImpl+0x08
//   06d8d48c  PUSH 0x7 ; PUSH 0x3         ; opcode, then channel
//   06d8d490  CALL FUN_06d96300           ; returns 0 on success
//   06d8d495  TEST EAX,EAX ; JNZ out      ; a refused allocation is dropped in silence
//   06d8d49d  MOV  EDX,[ESP + param_1+8]
//   06d8d4a1  MOV  [ECX + 0x20],EDX       ; the payload, from packet+0x20 on
//   06d8d4ad  CALL dword ptr [EAX + 0x170]; IServerImpl::PushToServer, through the vtable
//   06d8d4b5  RET  0x8
//
// NOTHING IS SENT BACK. Every decorated export of a serializing slot returns void
// (`UAEX...`); the only five IServerImpl exports with a return type are the five that are
// not RPCs at all -- ?Startup@ (_N), ?Update@ (H), ?SwapConnections@ (_N), ?GetServerID@
// (I) and ?GetServerSig@ (PBD) -- and not one of them allocates a packet. Retail does not
// have to answer the question "what happens to a return value" because it never asks it.
//
// The session id is likewise never on the wire -- with ONE exception the listing is very
// clear about: IServerImpl::StopTradeRequest (0x06d8e620) has exactly one argument, and
//   06d8e641  MOV  dword ptr [ECX + 0x20],0x80
//   06d8e64c  MOV  dword ptr [EDX + 0x28],EAX     ; EAX = that one argument
// puts it at payload+0x08, the slot its seven siblings use for the TARGET id. The
// receiver (rpc_payload_ch3.cpp, sub-type 0x80) already reproduced that quirk.
// ============================================================================
// -----------------------------------------------------------------------
// The one helper every serializing slot below funnels through: allocate, marshal,
// push. See the block comment above for the retail instruction sequence it replays.
//
// MarshalOverride is the IServer-direction marshaller where this port needs one;
// nullptr means "take the codec table's", which is right for all of channel 3 and for
// all of channel 1 except the direction-specific payloads listed above.
//
// 2026-09-24 (rpclocal-verify), trimmed to retail's shape. Removed, none of it in the
// fourteen instructions above:
//   * the lazy `if (!IsRegistered()) RegisterAll()` -- retail reads the serializer at
//     IServerImpl+0x08 untested (06d8d489) and IServerImpl::Startup registers before any
//     slot can run; with nothing registered Allocate answers 0x19 and the slot drops the
//     call, which is the TEST/JNZ path;
//   * the raw-argument-dword fallback and its one-time "layout not ported" log -- every
//     (channel, opcode) an IServerImpl slot allocates now has a Marshal in GChannel3Codecs,
//     GChannel1Codecs or a local override (checked against the 81 CALL FUN_06d96300 sites
//     of the IServerImpl range), so the branch was dead;
//   * `++Info->SendCount` -- a counter of ours that nothing reads; retail's serializer
//     keeps no send count (FUN_06d96300 06d96300..06d96378 writes no statistic);
//   * the second `Packet == nullptr` test -- Allocate returns 0 only with a packet;
//   * the DestroyOverride parameter, which no caller passed any more.
// The Marshal-refused branch stays: it is the FRpcMarshalFn contract, reachable only if a
// payload were smaller than its codec's struct, which the rpc_payloads.h static_asserts
// rule out.
// -----------------------------------------------------------------------
static void RpcSendToServer(IServerImpl* Self, unsigned int Channel, unsigned int Opcode,
                            const unsigned int* Args, unsigned int ArgCount, FRpcMarshalFn MarshalOverride) {
    FRpcMarshalFn Marshal = MarshalOverride;
    if (Marshal == nullptr) {
        if (Channel == 3u) {
            Marshal = RpcFindServerCodec(3u, Opcode)->Marshal;
        } else {
            Marshal = RpcFindPayloadCodec(Channel, Opcode)->Marshal;
        }
    }

    // 06d8d489 MOV ECX,[ESI+0x8] -- this->Serializer, untested -- /
    // 06d8d490 CALL FUN_06d96300 / 06d8d495 TEST EAX,EAX / JNZ -> epilogue.
    FPacketBase* Packet = nullptr;
    if (Self->Serializer->Allocate(Channel, Opcode, &Packet) != 0) {
        return;
    }

    if (!Marshal(Packet, Args, ArgCount)) {
        Packet->Release();
        return;
    }

    // retail: CALL dword ptr [EAX + 0x170] -- always through the vtable.
    Self->PushToServer(Packet->AsRaw());
}

// The common case: the codec table knows this type.
static void RpcSendToServer(IServerImpl* Self, unsigned int Channel, unsigned int Opcode,
                            const unsigned int* Args, unsigned int ArgCount) {
    RpcSendToServer(Self, Channel, Opcode, Args, ArgCount, nullptr);
}

// FLPACKET_COMMON_PLAYER_TRADE: eight logical messages under one opcode, told apart
// by the bitmask each sender writes into payload+0x00. MarshalPlayerTrade replays
// that, so what it wants is { SubType, A1, A2 } -- and on THIS side A1 is often zero,
// because the IServerImpl senders write fewer fields than their IClient twins.
static void RpcSendTradeToServer(IServerImpl* Self, unsigned int SubType, unsigned int A1, unsigned int A2) {
    const unsigned int Args[3] = { SubType, A1, A2 };
    RpcSendToServer(Self, 1u, 0x12u, Args, 3u, nullptr);
}

// === Slots 0-8: combat and ship control (channel 1) ===
void IServerImpl::FireWeapon(unsigned int player_id, const XFireWeaponInfo& info) {
    // retail 0x06d8ddd0: ch1 op 0x02 FLPACKET_COMMON_FIREWEAPON
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x02u, Args, 2u);
}
void IServerImpl::ActivateEquip(unsigned int player_id, const XActivateEquip& info) {
    // retail 0x06d8e6c0: ch1 op 0x08 FLPACKET_COMMON_ACTIVATEEQUIP
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x08u, Args, 2u);
}
void IServerImpl::ActivateCruise(unsigned int player_id, const XActivateCruise& info) {
    // retail 0x06d8e7c0: ch1 op 0x0e FLPACKET_COMMON_ACTIVATECRUISE
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x0eu, Args, 2u);
}
void IServerImpl::ActivateThrusters(unsigned int player_id, const XActivateThrusters& info) {
    // retail 0x06d8e810: ch1 op 0x15 FLPACKET_COMMON_ACTIVATETHRUSTERS
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x15u, Args, 2u);
}
void IServerImpl::SetTarget(unsigned int player_id, const XSetTarget& info) {
    // retail 0x06d8ea30: ch1 op 0x04 FLPACKET_COMMON_SETTARGET
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x04u, Args, 2u);
}
void IServerImpl::TractorObjects(unsigned int player_id, const XTractorObjects& info) {
    // retail 0x06d8e860: ch1 op 0x1d FLPACKET_COMMON_TRACTOROBJECTS
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x1du, Args, 2u);
}
void IServerImpl::GoTradelane(unsigned int player_id, const XGoTradelane& info) {
    // retail 0x06d8e710: ch1 op 0x0f FLPACKET_COMMON_GOTRADELANE
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x0fu, Args, 2u);
}
void IServerImpl::StopTradelane(unsigned int session_id, unsigned int arg1, unsigned int arg2, unsigned int arg3) {
    // retail 0x06d8e770: ch1 op 0x10. The three writes are at 06d8e791, 06d8e79c and
    // 06d8e7a7 -- param_2, param_3 and param_4; the session id is not one of them.
    const unsigned int Args[4] = { session_id, arg1, arg2, arg3 };
    RpcSendToServer(this, 1u, 0x10u, Args, 4u, MarshalServerStopTradelane);
}
void IServerImpl::JettisonCargo(unsigned int player_id, const XJettisonCargo& info) {
    // retail 0x06d8ea90: ch1 op 0x14 FLPACKET_COMMON_JETTISONCARGO
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x14u, Args, 2u);
}

// === Slots 9-12: core lifecycle (NOT serialized -- direct Server.dll calls) ===
bool IServerImpl::Startup(const SStartupInfo& info) {
    // 06d8f1f1 PUSH 0x104 / 06d8f1f6 PUSH ESI / 06d8f1f7 PUSH .data:DAT_06dbbbc0 /
    // 06d8f200 CALL [->MSVCRT.DLL::strncpy]: the caller's module path (SStartupInfo+0x00)
    // into the one 0x104-byte buffer at .data:0x06dbbbc0. n is the whole buffer, so a
    // source of 260 characters or more leaves it unterminated -- exactly as retail.
    // IClient slot 10 (0x06d8eed0) LoadLibraryA's this path to reach the REAL client.
    strncpy(GCallerModulePath, info.ModulePath, 0x104u);

    typedef IServer* (*GetServerInterfaceFn)();
    typedef void* (*GetChatServerInterfaceFn)();

    // 06d8f209 PUSH "server.dll" (.rdata:06db3c48) / 06d8f20e LoadLibraryA /
    // 06d8f216 MOV [.data:DAT_06dbbd64],EAX / 06d8f21b JZ 06d8f248.
    g_hServerDll = LoadLibraryA("server.dll");
    if (g_hServerDll != nullptr) {
        // 06d8f223 GetProcAddress("GetServerInterface") / 06d8f22b CALL EAX /
        // 06d8f22d -> DAT_06dbbd5c; 06d8f237 GetProcAddress("GetChatServerInterface") /
        // 06d8f23f CALL EAX / 06d8f241 -> DAT_06dbbd60. Neither address is tested.
        GetServerInterfaceFn GetServer =
            (GetServerInterfaceFn)GetProcAddress(g_hServerDll, "GetServerInterface");
        g_pServerInterface = GetServer();
        GetChatServerInterfaceFn GetChat =
            (GetChatServerInterfaceFn)GetProcAddress(g_hServerDll, "GetChatServerInterface");
        g_pChatServerInterface = GetChat();
    } else {
        // 06d8f248..06d8f265: DACOM's FDUMP through the import slot .rdata:06db311c,
        // severity 0x100001, format .rdata:06db3bb4, file .rdata:06db3bf0, line 0x3e,
        // and the literal "server.dll" (.rdata:06db3c48) -- retail's own spelling.
        FDUMP(0x100001, "%s(%d) : *** ERROR: Connect - failed to LoadLibrary(%s)\n",
              "E:\\FL\\Scratch\\Source\\RPCLocal\\RPCServer.cpp", 0x3e, "server.dll");
    }

    // 06d8f26a MOV EAX,[.data:DAT_06dbbd5c] / TEST / JZ 06d8f2ed -> MOV AL,BL with BL == 0
    // (06d8f1fe XOR BL,BL). Nothing is freed or cleared on this path.
    if (g_pServerInterface == nullptr) {
        return false;
    }

    // 06d8f273..06d8f28e: the whole 0x214-byte SStartupInfo is copied to the stack
    // (REP MOVSD, ECX=0x85) and its ModulePath is overwritten with
    // GetModuleFileNameA(DAT_06dbbccc /* rpclocal itself */, copy, 0x104). Server.dll is
    // told that rpclocal.dll is the loading module; IniPath (+0x104) passes through.
    SStartupInfo Forwarded = info;
    GetModuleFileNameA(g_hSelfModule, Forwarded.ModulePath, 0x104u);

    // 06d8f2a1 CALL [EDX+0x24] (IServer::Startup) / 06d8f2a4 MOV BL,AL /
    // 06d8f2a6 TEST BL,BL / 06d8f2a8 JZ 06d8f2ed. A refusal is returned as is -- the
    // library stays loaded and both interface pointers stay set.
    const bool bStarted = g_pServerInterface->Startup(Forwarded);
    if (!bStarted) {
        return false;
    }

    // 06d8f2aa..06d8f2bc: DAT_06dbbd24 = DAT_06dbbd5c ; DAT_06dbbd20 = DAT_06dbbd60 --
    // the copies FUN_06d86a70 (RpcDispatchToServer, rpc_payload_ch3.cpp) reads.
    GRealServer = g_pServerInterface;
    GRealChatServer = static_cast<IChatServer*>(g_pChatServerInterface);

    // 06d8f2c2 CALL FUN_06d95e50 / 06d8f2c9 MOV [EBP+0x8],EAX -- the serializer
    // singleton, stored whatever it is. Server.dll's Startup above has usually already
    // created it: it calls IClient slot 10, which asks FUN_06d95e50 first and registers
    // channels 2 and 1; this call then gets the same object back.
    Serializer = RpcGetPacketSerializer();
    // 06d8f2cc JZ 06d8f2ed ; 06d8f2cf FUN_06d9f100 (channel 3) / 06d8f2d9 JNZ 06d8f2ed ;
    // 06d8f2df FUN_06d9ace0 (channel 1). Every path from here returns 1: a null
    // serializer and a non-zero FUN_06d9f100 both land on 06d8f2ed MOV AL,BL (BL == 1),
    // and FUN_06d9ace0's answer only chooses between 06d8f2e9 MOV AL,0x1 and MOV AL,BL.
    if (Serializer != nullptr && RpcRegisterChannel3Types(Serializer) == 0) {
        RpcRegisterChannel1Types(Serializer);
    }
    return true;
}

// Retail 0x06d8f300, read instruction by instruction:
//   06d8f300  MOV EAX,[.data:DAT_06dbbd58]
//   06d8f308  CMP EAX,EBX(=0) / 06d8f30d JZ LAB_06d8f36c   <-- THE WHOLE BODY IS
//             GUARDED. With no connection, Shutdown does nothing at all: it does not
//             call Server.dll, does not FreeLibrary, does not touch the serializer.
//   06d8f30f  MOV ECX,[.data:DAT_06dbbd5c] / CALL [EAX+0x28]   ; real IServer::Shutdown
//   06d8f31a  if DAT_06dbbd64 (the server.dll HMODULE) : FreeLibrary, then clear
//             DAT_06dbbd64 and DAT_06dbbd60 (the chat server)
//   06d8f33f  DAT_06dbbd5c = 0                                  ; unconditional
//   06d8f347  if DAT_06dbbd58 : CALL FUN_06d857c0(conn) ; free(conn)
//   06d8f357  DAT_06dbbd58 = 0
//   06d8f35d  MOV ECX,[this+0x8] / CALL FUN_06d8f4a0            ; release the serializer
//   06d8f365  DAT_06dbbd19 = 0                                  ; the "connected" byte
// The release is untested and so is the pointer: FUN_06d8f4a0 reads [ECX+4] first. The
// serializer's use count is 1 and nothing ever raises it (FUN_06d95e50 returns the cached
// object without an AddRef), so this destroys it -- while the IClient proxy still holds
// the same pointer at Client+0x08 and DAT_06dbbe38 still caches it. Retail, reproduced.
//
// Not in retail, removed 2026-09-24: the null test on the server interface, the clearing
// of the published DAT_06dbbd24 / 06dbbd20 copies (GRealServer / GRealChatServer -- retail
// leaves them set), and a call to IClient slot 15 (0x06d8ef80) that retail Shutdown never
// makes -- Server.dll's own Shutdown calls that slot on its IClient.
void IServerImpl::Shutdown() {
    // 06d8f300 MOV EAX,[DAT_06dbbd58] / 06d8f308 CMP / 06d8f30d JZ -- no connection, no
    // shutdown.
    if (GServerConnection == nullptr) {
        return;
    }

    // 06d8f30f MOV ECX,[DAT_06dbbd5c] / 06d8f317 CALL [EAX+0x28]
    g_pServerInterface->Shutdown();

    // 06d8f31a..06d8f330
    if (g_hServerDll != nullptr) {
        FreeLibrary(g_hServerDll);
        g_hServerDll = nullptr;
        g_pChatServerInterface = nullptr;
    }

    // 06d8f33f DAT_06dbbd5c = 0
    g_pServerInterface = nullptr;

    // 06d8f337 MOV ESI,[DAT_06dbbd58] / 06d8f33d CMP / 06d8f345 JZ ;
    // 06d8f349 FUN_06d857c0(conn) / 06d8f34f free(conn) ; 06d8f357 DAT_06dbbd58 = 0.
    // Only this copy is cleared: DAT_06dbbb5c (GRpcConnection) and DAT_06dbbd1c (the
    // IClient proxy's) keep the freed pointer, as in retail -- see rpc_transport.h.
    if (GServerConnection != nullptr) {
        delete GServerConnection;
    }
    GServerConnection = nullptr;

    // 06d8f35d MOV ECX,[EDI+0x8] / 06d8f360 CALL FUN_06d8f4a0
    Serializer->Release();

    // 06d8f365 DAT_06dbbd19 = 0
    GbRpcConnected = false;
}

// Retail 0x06d8f400, instruction for instruction:
//   06d8f403  CALL [->COMMON.DLL::GiveServerRunPermission]   (IAT .rdata:06db30fc,
//             import name string .rdata:06db8484 "?GiveServerRunPermission@@YAXXZ")
//   06d8f409  MOV ECX,[.data:DAT_06dbbd58]      ; the connection
//   06d8f40f  MOV EAX,[ECX+0x8]                 ; the client-bound READ queue
//   06d8f412  TEST EAX,EAX / JZ  LAB_06d8f43d
//   06d8f416  MOV EDX,[EAX+0x2c]                ; deque::size()
//   06d8f419  TEST EDX,EDX / JBE LAB_06d8f43d   ; JBE after TEST == JZ, CF is 0
//   06d8f41d  CALL FUN_06d864a0                 ; pop
//   06d8f426  loop: PUSH EAX / CALL IServerImpl::HandlePacket / pop again
//   06d8f43d  CALL [->COMMON.DLL::WaitForClientRunPermission] (IAT .rdata:06db30f8,
//             name string .rdata:06db8458 "?WaitForClientRunPermission@@YAXXZ")
//   06d8f443  MOV ECX,.data:Client / CALL FUN_06d8efc0
//   06d8f44d  MOV EAX,0x1 / RET
//
// The 2026-09-17 note claiming "the two run-permission calls were never located
// (inline, or in Common.dll)" was simply wrong: both are ordinary IAT imports and
// both are already ported in src/Common/common_misc.cpp behind COMMON_API. rpclocal
// already links Common.lib (rpclocal.vcxproj AdditionalDependencies), so there is
// nothing to bridge.
//
// The packet is released by HandlePacket itself, as in retail (06d83b21, the single
// exit of 0x06d81170), not here. The 2026-09-17 comment that stood here ("retail never
// releases the packet") was wrong on the bytes.
//
// DAT_06dbbd58 is read untested, as retail does at 06d8f409 / 06d8f40f: Update before
// Connect faults here exactly as it would in retail. freelancer.exe calls Connect inside
// the same synchronous connect sequence that follows Startup (RunConnectFlow ->
// RunPostConnectGate -> InvokeServerBeginLogin, src/freelancer/Client/), before the frame
// tick can reach Update. The pointer is re-read for every pop (06d8f42e).
int IServerImpl::Update() {
    GiveServerRunPermission();

    FPacketBase* Packet = GServerConnection->PopClientBound();
    while (Packet != nullptr) {
        HandlePacket(Packet->AsRaw());   // releases the packet (06d83b21)
        Packet = GServerConnection->PopClientBound();
    }

    WaitForClientRunPermission();

    // retail FUN_06d8efc0(&Client) -- IClient slot 16, the client-side pump
    RpcClientPump();
    return 1;
}

// Retail 0x06d8f370: MOV ECX,[DAT_06dbbd5c] / JMP [EAX+0x30] -- no test.
void IServerImpl::ElapseTime(float dt) {
    g_pServerInterface->ElapseTime(dt);
}

// === Slots 13-14: connection swap ===
// Retail 0x06d8eae0 is a single RET shared by three exported names:
// ?OnConnect@, ?GetServerStats@ (non-virtual) and ?SetActiveConnection@.
void IServerImpl::SetActiveConnection(EFLConnection conn) {
}
// Retail 0x06d8f3f0: MOV AL,1 / RET 8 -- always true, never forwarded.
bool IServerImpl::SwapConnections(EFLConnection conn_a, EFLConnection conn_b) {
    return true;
}

// === Slots 15-17: connections ===
void IServerImpl::Connect(const char* server_name, unsigned short* port) {
    // Retail 0x06d8f390:
    //   06d8f390  MOV EAX,[.data:DAT_06dbbd58] / TEST / JNZ LAB_06d8f3bb
    //             -- an existing connection is reused and NOT restarted.
    //   06d8f399  CALL FUN_06d88280 ; 06d8f3a0 MOV [.data:DAT_06dbbd58],EAX
    //   06d8f3a5  JZ LAB_06d8f3bb                      ; allocation failed
    //   06d8f3a7  PUSH 0x1 / MOV ECX,EAX / CALL FUN_06d85b60   ; Start(mode = 1)
    //   06d8f3b2  JNZ LAB_06d8f3bb                     ; only a ZERO return counts
    //   06d8f3b4  MOV byte [.data:DAT_06dbbd19],0x1    ; the "connected" byte
    //   06d8f3bb  MOV ECX,[.data:DAT_06dbbd5c] / JMP [EAX+0x3c]  ; always forwarded
    // A connection that already exists is reused and NOT restarted, and the flag is
    // left exactly as it was -- retail's 06d8f397 jumps past the whole block. Nothing but
    // this function starts the connection, so only a first Start can answer here.
    if (GServerConnection == nullptr) {
        GServerConnection = RpcCreateConnection();
        if (GServerConnection != nullptr) {
            if (GServerConnection->Start(FRpcConnection::kModeSingleBuffered) ==
                FRpcConnection::kStartOk) {
                GbRpcConnected = true;
            }
        }
    }
    g_pServerInterface->Connect(server_name, port);
}
// Retail 0x06d8f3d0: MOV ECX,[DAT_06dbbd5c] / 06d8f3e2 CALL [EAX+0x40](param_1, param_2) /
// 06d8f3e5 MOV byte [DAT_06dbbd19],0 / RET 8. No test.
void IServerImpl::DisConnect(unsigned int session_id, EFLConnection type) {
    g_pServerInterface->DisConnect(session_id, type);
    GbRpcConnected = false;
}
// Retail 0x06d8eae0 -- the same shared RET as SetActiveConnection. A no-op.
void IServerImpl::OnConnect(unsigned int /*session_id*/) {
}
// Retail 0x06d8eae0 again (ordinal 38, public NON-virtual): the out-parameter is never
// written.
void IServerImpl::GetServerStats(ServerStats& /*Stats*/) {
}

// === Slots 18-24: authentication and character ===
void IServerImpl::Login(const SLoginInfo& info, unsigned int session_id) {
    // retail 0x06d8cc70: ch3 op 0x01 FLPACKET_CLIENT_LOGIN
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x01u, Args, 2u);
}
void IServerImpl::CharacterInfoReq(unsigned int session_id, bool full_info) {
    // retail 0x06d8cd00: ch3 op 0x05, payload 0 -- the flag is not serialized
    const unsigned int Args[2] = { session_id, full_info ? 1u : 0u };
    RpcSendToServer(this, 3u, 0x05u, Args, 2u);
}
void IServerImpl::CharacterSelect(const CHARACTER_ID& char_id, unsigned int session_id) {
    // retail 0x06d8cd60: ch3 op 0x06, serializes the CHARACTER_ID string
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&char_id), session_id };
    RpcSendToServer(this, 3u, 0x06u, Args, 2u);
}
void IServerImpl::NewCharacterInfoReq(unsigned int session_id) {
    // retail 0x06d8cdc0: ch3 op 0x38, payload 0
    const unsigned int Args[1] = { session_id };
    RpcSendToServer(this, 3u, 0x38u, Args, 1u);
}
void IServerImpl::CreateNewCharacter(const SCreateCharacterInfo& info, unsigned int session_id) {
    // retail 0x06d8cdf0: ch3 op 0x39, wcsncpy of info.Name plus the four dwords at +0x30
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x39u, Args, 2u);
}
void IServerImpl::DestroyCharacter(const CHARACTER_ID& char_id, unsigned int session_id) {
    // retail 0x06d8ce90: ch3 op 0x3a
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&char_id), session_id };
    RpcSendToServer(this, 3u, 0x3au, Args, 2u);
}
void IServerImpl::CharacterSkipAutosave(unsigned int session_id) {
    // retail 0x06d8cd30: ch3 op 0x42, payload 0
    const unsigned int Args[1] = { session_id };
    RpcSendToServer(this, 3u, 0x42u, Args, 1u);
}

// === Slots 25-32: ship and equipment requests ===
void IServerImpl::ReqShipArch(unsigned int arch_id, unsigned int session_id) {
    // retail 0x06d8ced0: ch3 op 0x15, serializes arch_id only
    const unsigned int Args[2] = { arch_id, session_id };
    RpcSendToServer(this, 3u, 0x15u, Args, 2u);
}
void IServerImpl::ReqHullStatus(float hull_status, unsigned int session_id) {
    // retail 0x06d8cf10: ch3 op 0x3b, one dword -- a float, read back with FLD
    const unsigned int Args[2] = { RpcFloatBits(hull_status), session_id };
    RpcSendToServer(this, 3u, 0x3bu, Args, 2u);
}
void IServerImpl::ReqCollisionGroups(const std::list<CollisionGroupDesc>& groups, unsigned int session_id) {
    // retail 0x06d8cf50: ch3 op 0x36; FUN_06d8f710 deep-copies the list node by node
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&groups), session_id };
    RpcSendToServer(this, 3u, 0x36u, Args, 2u);
}
void IServerImpl::ReqEquipment(const EquipDescList& equipment, unsigned int session_id) {
    // retail 0x06d8cf90: ch3 op 0x16; walks the list and copies 0x18 bytes per element
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&equipment), session_id };
    RpcSendToServer(this, 3u, 0x16u, Args, 2u);
}
void IServerImpl::ReqCargo(const EquipDescList& cargo, unsigned int session_id) {
    // retail 0x06d8d0e0: ch3 op 0x17. This is the REAL slot 29 -- the old "Connect2"
    // entry here was a fabricated duplicate of Connect.
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&cargo), session_id };
    RpcSendToServer(this, 3u, 0x17u, Args, 2u);
}
void IServerImpl::ReqAddItem(unsigned int arch_id, const char* hardpoint, int count, float health,
                unsigned char b_mounted, unsigned int session_id) {
    // retail 0x06d8d230: ch3 op 0x18
    const unsigned int Args[6] = {
        arch_id, reinterpret_cast<unsigned int>(hardpoint), (unsigned int)count,
        RpcFloatBits(health), static_cast<unsigned int>(b_mounted), session_id
    };
    RpcSendToServer(this, 3u, 0x18u, Args, 6u);
}
void IServerImpl::ReqRemoveItem(unsigned short equip_id, int count, unsigned int session_id) {
    // retail 0x06d8d2b0: ch3 op 0x19
    const unsigned int Args[3] = { (unsigned int)equip_id, (unsigned int)count, session_id };
    RpcSendToServer(this, 3u, 0x19u, Args, 3u);
}
void IServerImpl::ReqModifyItem(unsigned short equip_id, const char* hardpoint, int count, float health,
                   unsigned char b_mounted, unsigned int session_id) {
    // retail 0x06d8d300: ch3 op 0x1a
    const unsigned int Args[6] = {
        (unsigned int)equip_id, reinterpret_cast<unsigned int>(hardpoint), (unsigned int)count,
        RpcFloatBits(health), static_cast<unsigned int>(b_mounted), session_id
    };
    RpcSendToServer(this, 3u, 0x1au, Args, 6u);
}

// === Slots 33-34: cash (int, not float) ===
void IServerImpl::ReqSetCash(int cash, unsigned int session_id) {
    // retail 0x06d8d380: ch3 op 0x1b
    const unsigned int Args[2] = { (unsigned int)cash, session_id };
    RpcSendToServer(this, 3u, 0x1bu, Args, 2u);
}
void IServerImpl::ReqChangeCash(int delta, unsigned int session_id) {
    // retail 0x06d8d3c0: ch3 op 0x1c
    const unsigned int Args[2] = { (unsigned int)delta, session_id };
    RpcSendToServer(this, 3u, 0x1cu, Args, 2u);
}

// === Slots 35-40: base and location ===
void IServerImpl::BaseEnter(unsigned int base_id, unsigned int session_id) {
    // retail 0x06d8d480: ch3 op 0x07, serializes base_id only
    const unsigned int Args[2] = { base_id, session_id };
    RpcSendToServer(this, 3u, 0x07u, Args, 2u);
}
void IServerImpl::BaseExit(unsigned int base_id, unsigned int session_id) {
    // retail 0x06d8d4c0: ch3 op 0x0d
    const unsigned int Args[2] = { base_id, session_id };
    RpcSendToServer(this, 3u, 0x0du, Args, 2u);
}
void IServerImpl::LocationEnter(unsigned int location_id, unsigned int session_id) {
    // retail 0x06d8d400: ch3 op 0x0e
    const unsigned int Args[2] = { location_id, session_id };
    RpcSendToServer(this, 3u, 0x0eu, Args, 2u);
}
void IServerImpl::LocationExit(unsigned int location_id, unsigned int session_id) {
    // retail 0x06d8d440: ch3 op 0x0f
    const unsigned int Args[2] = { location_id, session_id };
    RpcSendToServer(this, 3u, 0x0fu, Args, 2u);
}
void IServerImpl::BaseInfoRequest(unsigned int base_id, unsigned int session_id, bool b_full) {
    // retail 0x06d8d500: ch3 op 0x08, serializes base_id and b_full
    const unsigned int Args[3] = { base_id, session_id, b_full ? 1u : 0u };
    RpcSendToServer(this, 3u, 0x08u, Args, 3u);
}
void IServerImpl::LocationInfoRequest(unsigned int location_id, unsigned int session_id, bool b_full) {
    // retail 0x06d8d550: ch3 op 0x09
    const unsigned int Args[3] = { location_id, session_id, b_full ? 1u : 0u };
    RpcSendToServer(this, 3u, 0x09u, Args, 3u);
}

// === Slots 41-46: GF events ===
void IServerImpl::GFObjSelect(unsigned int object_id, unsigned int session_id) {
    // retail 0x06d8da40: ch3 op 0x13, serializes object_id only
    const unsigned int Args[2] = { object_id, session_id };
    RpcSendToServer(this, 3u, 0x13u, Args, 2u);
}
void IServerImpl::GFGoodVaporized(const SGFGoodVaporizedInfo& info, unsigned int session_id) {
    // retail 0x06d8d650: ch3 op 0x3c, the struct's first three dwords
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x3cu, Args, 2u);
}
void IServerImpl::MissionResponse(unsigned int Arg0, unsigned long MissionId, unsigned char bAccepted, unsigned int SessionId) {
    // retail 0x06d8da80: ch3 op 0x14
    const unsigned int Args[4] = {
        Arg0, static_cast<unsigned int>(MissionId), bAccepted, SessionId
    };
    RpcSendToServer(this, 3u, 0x14u, Args, 4u);
}
void IServerImpl::TradeResponse(const unsigned char* data, int size, unsigned int session_id) {
    // retail 0x06d8dad0: ch3 op 0x27, the blob is copied into the packet
    const unsigned int Args[3] = {
        reinterpret_cast<unsigned int>(data), (unsigned int)size, session_id
    };
    RpcSendToServer(this, 3u, 0x27u, Args, 3u);
}
void IServerImpl::GFGoodBuy(const SGFGoodBuyInfo& info, unsigned int session_id) {
    // retail 0x06d8d5a0: ch3 op 0x12, the struct's first four dwords
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x12u, Args, 2u);
}
void IServerImpl::GFGoodSell(const SGFGoodSellInfo& info, unsigned int session_id) {
    // retail 0x06d8d600: ch3 op 0x11, the struct's first three dwords
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x11u, Args, 2u);
}

// === Slots 47-51: system and launch ===
void IServerImpl::SystemSwitchOutComplete(unsigned int gate_object_id, unsigned int session_id) {
    // retail 0x06d8d6a0: ch3 op 0x0b
    const unsigned int Args[2] = { gate_object_id, session_id };
    RpcSendToServer(this, 3u, 0x0bu, Args, 2u);
}
void IServerImpl::PlayerLaunch(unsigned int ship_id, unsigned int session_id) {
    // retail 0x06d8da00: ch3 op 0x04
    const unsigned int Args[2] = { ship_id, session_id };
    RpcSendToServer(this, 3u, 0x04u, Args, 2u);
}
void IServerImpl::LaunchComplete(unsigned int arg0, unsigned int object_id) {
    // retail 0x06d8d8e0: ch3 op 0x3e, serializes BOTH arguments
    const unsigned int Args[2] = { arg0, object_id };
    RpcSendToServer(this, 3u, 0x3eu, Args, 2u);
}
void IServerImpl::JumpInComplete(unsigned int arg0, unsigned int object_id) {
    // retail 0x06d8d930: ch3 op 0x43, serializes BOTH arguments
    const unsigned int Args[2] = { arg0, object_id };
    RpcSendToServer(this, 3u, 0x43u, Args, 2u);
}
void IServerImpl::Hail(unsigned int object_id, unsigned int arg1, unsigned int arg2) {
    // retail 0x06d8d980: ch3 op 0x3f, serializes all three
    const unsigned int Args[3] = { object_id, arg1, arg2 };
    RpcSendToServer(this, 3u, 0x3fu, Args, 3u);
}

// === Slots 52-55: SP collisions ===
void IServerImpl::SPObjUpdate(const SSPObjUpdateInfo& info, unsigned int session_id) {
    // retail 0x06d8dc30: ch1 op 0x01 FLPACKET_COMMON_UPDATEOBJECT. The argument struct
    // is NOT the one the IClient sender reads, so this type marshals its own way.
    const unsigned int Args[2] = { session_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x01u, Args, 2u, MarshalServerObjUpdate);
}
void IServerImpl::SPMunitionCollision(const SSPMunitionCollisionInfo& info, unsigned int session_id) {
    // retail 0x06d8d6e0: ch3 op 0x03
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x03u, Args, 2u);
}
void IServerImpl::SPBadLandsObjCollision(const SSPBadLandsObjCollisionInfo& info, unsigned int session_id) {
    // retail 0x06d8d760: ch3 op 0x3d
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x3du, Args, 2u);
}
void IServerImpl::SPObjCollision(const SSPObjCollisionInfo& info, unsigned int session_id) {
    // retail 0x06d8d7b0: ch3 op 0x0c
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x0cu, Args, 2u);
}

// === Slots 56-59: SP requests ===
void IServerImpl::SPRequestUseItem(const SSPUseItem& info, unsigned int session_id) {
    // retail 0x06d8ec20: ch3 op 0x40
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x40u, Args, 2u);
}
void IServerImpl::SPRequestInvincibility(unsigned int ship_id, unsigned char b_invincible,
                            InvincibilityReason reason, unsigned int session_id) {
    // retail 0x06d8ec80: ch3 op 0x44; the reason and the flag are written as single bytes
    const unsigned int Args[4] = {
        ship_id, b_invincible, (unsigned int)reason, session_id
    };
    RpcSendToServer(this, 3u, 0x44u, Args, 4u);
}
void IServerImpl::SaveGame(const CHARACTER_ID& char_id, const wchar_t* description, unsigned int session_id) {
    // retail 0x06d8db10: ch3 op 0x1e
    const unsigned int Args[3] = {
        reinterpret_cast<unsigned int>(&char_id),
        reinterpret_cast<unsigned int>(description), session_id
    };
    RpcSendToServer(this, 3u, 0x1eu, Args, 3u);
}
void IServerImpl::MissionSaveB(unsigned int session_id, unsigned long mission_id) {
    // retail 0x06d8db70: ch3 op 0x45, serializes mission_id only
    const unsigned int Args[2] = { session_id, (unsigned int)mission_id };
    RpcSendToServer(this, 3u, 0x45u, Args, 2u);
}

// === Slots 60-63: events and mining ===
void IServerImpl::RequestEvent(int event_type, unsigned int arg1, unsigned int arg2, unsigned int arg3,
                  unsigned long arg4, unsigned int session_id) {
    // retail 0x06d8eaf0: ch3 op 0x31
    const unsigned int Args[6] = {
        (unsigned int)event_type, arg1, arg2, arg3, (unsigned int)arg4, session_id
    };
    RpcSendToServer(this, 3u, 0x31u, Args, 6u);
}
void IServerImpl::RequestCancel(int event_type, unsigned int arg1, unsigned int arg2, unsigned long arg3,
                   unsigned int session_id) {
    // retail 0x06d8eb60: ch3 op 0x32
    const unsigned int Args[5] = {
        (unsigned int)event_type, arg1, arg2, (unsigned int)arg3, session_id
    };
    RpcSendToServer(this, 3u, 0x32u, Args, 5u);
}
void IServerImpl::MineAsteroid(unsigned int system_id, const Vector& position, unsigned int loot_id,
                  unsigned int secondary_loot, unsigned int quantity, unsigned int session_id) {
    // retail 0x06d8dcf0: ch3 op 0x20
    const unsigned int Args[6] = {
        system_id, reinterpret_cast<unsigned int>(&position), loot_id,
        secondary_loot, quantity, session_id
    };
    RpcSendToServer(this, 3u, 0x20u, Args, 6u);
}
void IServerImpl::CommComplete(unsigned int arg0, unsigned int arg1, unsigned int arg2, CommResult result) {
    // retail 0x06d8dd70: ch3 op 0x37
    const unsigned int Args[4] = { arg0, arg1, arg2, (unsigned int)result };
    RpcSendToServer(this, 3u, 0x37u, Args, 4u);
}

// === Slots 64-66 ===
void IServerImpl::RequestCreateShip(unsigned int session_id) {
    // retail 0x06d8d9d0: ch3 op 0x10, payload 0
    const unsigned int Args[1] = { session_id };
    RpcSendToServer(this, 3u, 0x10u, Args, 1u);
}
void IServerImpl::SPScanCargo(const unsigned int& scanner_id, const unsigned int& target_id, unsigned int session_id) {
    // retail 0x06d8ebd0: ch3 op 0x2b, serializes *scanner_id and *target_id
    const unsigned int Args[3] = {
        reinterpret_cast<unsigned int>(&scanner_id),
        reinterpret_cast<unsigned int>(&target_id), session_id
    };
    RpcSendToServer(this, 3u, 0x2bu, Args, 3u);
}
void IServerImpl::SetManeuver(unsigned int session_id, const XSetManeuver& info) {
    // retail 0x06d8e670: ch3 op 0x2f -- the session comes FIRST in this one
    const unsigned int Args[2] = { session_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 3u, 0x2fu, Args, 2u);
}

// === Slots 67-70: interface and mission ===
void IServerImpl::InterfaceItemUsed(unsigned int session_id, unsigned int item_id) {
    // retail 0x06d8d820: ch3 op 0x35, serializes item_id only
    const unsigned int Args[2] = { session_id, item_id };
    RpcSendToServer(this, 3u, 0x35u, Args, 2u);
}
void IServerImpl::AbortMission(unsigned int session_id, unsigned int mission_id) {
    // retail 0x06d8d8a0: ch3 op 0x41, serializes mission_id only
    const unsigned int Args[2] = { session_id, mission_id };
    RpcSendToServer(this, 3u, 0x41u, Args, 2u);
}
void IServerImpl::RTCDone(unsigned int session_id, unsigned int rtc_id) {
    // retail 0x06d8d860: ch3 op 0x47, serializes rtc_id only
    const unsigned int Args[2] = { session_id, rtc_id };
    RpcSendToServer(this, 3u, 0x47u, Args, 2u);
}
void IServerImpl::SetWeaponGroup(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8dfb0: ch1 op 0x11. The ONE member of the buffer family that stores
    // the caller's pointer on this side too (06d8dfd1 / 06d8dfdc are plain MOVs, no
    // operator_new anywhere in the body) -- so the shared codec is already right. The
    // packet is queued like every other, the caller's pointer inside it, as retail's is.
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x11u, Args, 3u);
}

// === Slots 71-78: buffer-parameter methods (channel 1) ===
//
// Seven of the eight COPY on this side -- see MarshalServerBufferCopy for the listing
// evidence. 0x16 and 0x18 copy on the IClient side as well, so those two keep the
// shared codec; the other five need the copying marshal. Teardown is the same for all
// eight in both directions -- the packet class's own destructor, which frees the block only when
// payload+0x08 (bOwnsBuffer) is set, exactly as the shared class destructor does
// (FUN_06d9b1a0 and siblings, see FPayloadCommonBuffer).
void IServerImpl::SetVisitedState(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8e000: ch1 op 0x13, operator_new + REP MOVS
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x13u, Args, 3u, MarshalServerBufferCopy);
}
void IServerImpl::RequestBestPath(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8e080: ch1 op 0x16, copies in both directions
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x16u, Args, 3u);
}
void IServerImpl::RequestPlayerStats(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8e100: ch1 op 0x18, copies in both directions
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x18u, Args, 3u);
}
void IServerImpl::PopUpDialog(unsigned int session_id, unsigned int arg1) {
    // retail 0x06d8e180: ch1 op 0x1b -- one dword at payload+0x10 and nothing else
    const unsigned int Args[2] = { session_id, arg1 };
    RpcSendToServer(this, 1u, 0x1bu, Args, 2u, MarshalServerPopUpDialog);
}
void IServerImpl::RequestGroupPositions(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8e1c0: ch1 op 0x17, operator_new + REP MOVS
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x17u, Args, 3u, MarshalServerBufferCopy);
}
void IServerImpl::SetMissionLog(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8e240: ch1 op 0x19, operator_new + REP MOVS
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x19u, Args, 3u, MarshalServerBufferCopy);
}
void IServerImpl::SetInterfaceState(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8e2c0: ch1 op 0x1c, operator_new + REP MOVS
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x1cu, Args, 3u, MarshalServerBufferCopy);
}
void IServerImpl::RequestRankLevel(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8e340: ch1 op 0x1a, operator_new + REP MOVS
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x1au, Args, 3u, MarshalServerBufferCopy);
}

// === Slots 79-86: trade (all ch1 op 0x12, distinguished by the bitmask at packet+0x20) ===
void IServerImpl::InitiateTrade(unsigned int session_id, unsigned int target_session_id) {
    // retail 0x06d8e3c0: subtype 0x01 at packet+0x20, target id at packet+0x28
    (void)session_id;
    RpcSendTradeToServer(this, 0x01u, target_session_id, 0u);
}
void IServerImpl::TerminateTrade(unsigned int session_id, int reason) {
    // retail 0x06d8e410: subtype 0x02, reason at packet+0x24 -- MarshalPlayerTrade's
    // case 0x02 puts its A1 exactly there.
    (void)session_id;
    RpcSendTradeToServer(this, 0x02u, (unsigned int)reason, 0u);
}
void IServerImpl::AcceptTrade(unsigned int session_id, bool b_accepted) {
    // retail 0x06d8e460: subtype 0x40, the flag BYTE at packet+0x30 and nothing at
    // +0x28 -- the IClient sender writes a target id there, this one does not.
    (void)session_id;
    RpcSendTradeToServer(this, 0x40u, 0u, b_accepted ? 1u : 0u);
}
void IServerImpl::SetTradeMoney(unsigned int session_id, unsigned long money) {
    // retail 0x06d8e4b0: subtype 0x04, money at packet+0x2c, nothing at +0x28.
    (void)session_id;
    RpcSendTradeToServer(this, 0x04u, 0u, (unsigned int)money);
}
void IServerImpl::AddTradeEquip(unsigned int session_id, const EquipDesc& equip) {
    // retail 0x06d8e500: subtype 0x08. The four reads are EquipDesc+0x04 (arch id),
    // +0x14 (count), +0x10 (a float, FLD/FSTP) and the WORD at +0x02 (the id) --
    // the EquipDesc fields ArchId, Count, Health and Id (common_goods.h).
    (void)session_id;
    RpcSendTradeToServer(this, 0x08u, 0u, reinterpret_cast<unsigned int>(&equip));
}
void IServerImpl::DelTradeEquip(unsigned int session_id, const EquipDesc& equip) {
    // retail 0x06d8e570: subtype 0x10, the same three minus the float at +0x3c.
    (void)session_id;
    RpcSendTradeToServer(this, 0x10u, 0u, reinterpret_cast<unsigned int>(&equip));
}
void IServerImpl::RequestTrade(unsigned int session_id, unsigned int target_session_id) {
    // retail 0x06d8e5d0: subtype 0x20, target id at packet+0x28
    (void)session_id;
    RpcSendTradeToServer(this, 0x20u, target_session_id, 0u);
}
void IServerImpl::StopTradeRequest(unsigned int session_id) {
    // retail 0x06d8e620: subtype 0x80, and its ONE argument goes to packet+0x28 --
    // the only place in the whole module where a session id reaches the wire. The
    // receiver reads it back and passes it on instead of its own session id.
    RpcSendTradeToServer(this, 0x80u, session_id, 0u);
}

// === Slots 87-92: tail ===
void IServerImpl::ReqDifficultyScale(float scale, unsigned int session_id) {
    // retail 0x06d8dbf0: ch3 op 0x46, one dword -- a float, read back with FLD
    const unsigned int Args[2] = { RpcFloatBits(scale), session_id };
    RpcSendToServer(this, 3u, 0x46u, Args, 2u);
}
// Retail 0x06d8cda0: XOR EAX,EAX / RET -- always zero, never forwarded to Server.dll.
unsigned int IServerImpl::GetServerID() {
    return 0;
}
// Retail 0x06d8cdb0: MOV EAX,.data:DAT_06dbbbb0 / RET. DAT_06dbbbb0 is an 8-byte .data
// buffer with exactly one xref (this load) and no writer anywhere in the image, so the
// local server's signature is the empty string.
const char* IServerImpl::GetServerSig() {
    static const char kEmptySig[8] = {0};
    return kEmptySig;
}
void IServerImpl::DumpPacketStats(const char* name) {
    // Retail 0x06d8f480: 06d8f484 PUSH name / 06d8f485 CALL FUN_06d95e50 / 06d8f48a
    // MOV ECX,EAX / 06d8f48c CALL FUN_06d961b0 -- the singleton, NOT this->Serializer,
    // asked afresh (so a dump before Startup builds an empty registry) and untested; it
    // never forwards to Server.dll.
    RpcGetPacketSerializer()->DumpStats(name);
}
void IServerImpl::Dock(const unsigned int& base_id, const unsigned int& session_id) {
    // retail 0x06d8dbb0: ch3 op 0x24, serializes *base_id only
    const unsigned int Args[2] = {
        reinterpret_cast<unsigned int>(&base_id),
        reinterpret_cast<unsigned int>(&session_id)
    };
    RpcSendToServer(this, 3u, 0x24u, Args, 2u);
}
// Retail 0x06d8f460, whole: 06d8f460 MOV ECX,[DAT_06dbbd58] / 06d8f466 TEST / 06d8f468
// JZ -> 06d8f46f RET 4 ; 06d8f46a JMP FUN_06d86150 -- links the packet POINTER into the
// CDAConnection's server-bound queue. With no connection (before Connect, after
// Shutdown) the packet is dropped without a word and never released, as in retail; so is
// FUN_06d86150's answer. Every type is queued; the pump drains it on the next Update.
void IServerImpl::PushToServer(CDAPacket* packet) {
    if (GServerConnection != nullptr) {
        GServerConnection->PushServerBound(reinterpret_cast<FPacketBase*>(packet));
    }
}

// IChatServer::SubmitChat -- retail 0x06d8ecd0 sends ch1 op 0x05 FLPACKET_COMMON_CHATMSG
// through the serializer it reaches as [EBP+4], EBP being this sub-object at
// IServerImpl+0x04; that is IServerImpl+0x08, the same serializer every IServer slot
// uses. The push goes through [EBP-4]'s vtable +0x170, i.e. this very PushToServer.
void IServerImpl::SubmitChat(CHAT_ID sender_id, unsigned long msg_type, const void* msg_data, CHAT_ID target_id, int system_id) {
    // NOTE on the second parameter's NAME: retail uses it as the operator_new() size
    // and the REP MOVS length for msg_data (06d8ecfb..06d8ed25), so it is a byte
    // count, not a message type. Renaming it is a common_server.h change and belongs
    // to the lane that owns that header; the marshaller treats it as the length.
    const unsigned int Args[5] = {
        sender_id.Id, (unsigned int)msg_type, reinterpret_cast<unsigned int>(msg_data),
        target_id.Id, (unsigned int)system_id
    };
    RpcSendToServer(this, 1u, 0x05u, Args, 5u, MarshalServerChatMsg);
}

// === Internal packet handling ===
//
// Retail IServerImpl::HandlePacket (0x06d81170, 3128 instructions) is the whole
// server -> client half of rpclocal. Verified against the raw listing at 0x06d81194:
//
//   MOV EAX,[EBP+0xc] ; SHR EDX,0x10 ; MOVZX EDX,DX   -> Channel  = header >> 16
//   AND EAX,0xfff                                      -> Opcode   = header & 0xfff
//   DEC EDX ; JZ 0x06d83256                            -> Channel 1 switch (op 1..0x1d)
//   DEC EDX ; JNZ default                              -> Channel 2 switch (op 1..0x5b)
//
// Each case deserializes the payload that starts at packet+0x20 and calls one IClient
// vtable slot (global DAT_06dbbcd8), or IChatClient (DAT_06dbbcd4) for channel 1 op 5.
//
// The case bodies are the Deliver halves in rpc_payload_ch1.cpp / rpc_payload_ch2.cpp,
// selected by RpcHandleIncomingPacket (rpc_dispatch.cpp). Every path through retail's
// switch -- every case and the default -- ends at 06d83b1f MOV ECX,EBP /
// 06d83b21 CALL FUN_06d8f4a0, i.e. HandlePacket releases the packet it was given.
void IServerImpl::HandlePacket(CDAPacket* pkt) {
    FPacketBase* Packet = reinterpret_cast<FPacketBase*>(pkt);
    RpcHandleIncomingPacket(Packet);
    Packet->Release();
}

// ordinal 41, 0x06d84490. HandlePacket's caseD_3 calls it at 06d81715 with
// (packet, DAT_06dbbcd8 = the real client, 1); the body lives beside the CHARACTERINFO
// codec in src/rpclocal/rpc_payload_ch2.cpp. The port's packet object is an FPacketBase,
// the same 0x20-byte base CDAPCharacterInfo derives from in retail.
bool IServerImpl::HandleCharacterInfo(CDAPCharacterInfo* Packet, IClient* Client, unsigned int Arg) {
    return RpcHandleCharacterInfo(reinterpret_cast<const FPacketBase*>(Packet), Client, Arg);
}

// ordinal 42, 0x06d84c60. Not called by HandlePacket (caseD_5 inlines the same logic);
// exported only. Body in src/rpclocal/rpc_payload_ch2.cpp.
bool IServerImpl::HandleDamageObject(CDAPDamageSpaceObj* Packet, IClient* Client, unsigned int Arg) {
    return RpcHandleDamageObject(reinterpret_cast<const FPacketBase*>(Packet), Client, Arg);
}

// ============================================================================
// Special members and the exported singleton.
//
// Retail ??0IServerImpl@@QAE@XZ (0x06d81110), ??0IServerImpl@@QAE@ABU0@@Z (0x06d81130) and
// ??4IServerImpl@@QAEAAU0@ABU0@@Z (0x06d81160):
//   0x06d81110  06d81119 MOV [EAX+0x8],0 and the two vptrs (06d81120 / 06d81126)
//   0x06d81130  06d81143/06d81146 copies [src+0x8] into [this+0x8], then the two vptrs
//   0x06d81160  06d81166/06d81169 copies [src+0x8] into [this+0x8], nothing else
// The pointer is copied without an AddRef in both, which is what the defaulted members
// do with a raw pointer member.
// ============================================================================
IServerImpl::IServerImpl() : Serializer(nullptr) {
}
IServerImpl::IServerImpl(const IServerImpl& Other) = default;
IServerImpl& IServerImpl::operator=(const IServerImpl& Other) = default;

// retail .data:Server (0x06dbbd28), export ordinal 113, built by LAB_06d8f1a0: the same
// three stores as the default constructor, inlined (06d8f1a0 MOV [DAT_06dbbd30],0 --
// Server+0x08 -- then the IServer and IChatServer vptrs).
IServerImpl Server;

// ============================================================================
// Exported functions
// ============================================================================

extern "C" {

// Retail ordinal 112, 0x06d8f1c0: MOV EAX, .data:Server; RET.
IServer* __cdecl GetServerInterface() {
    return &Server;
}

// Retail ordinal 110, 0x06d8f1d0: MOV EAX, .data:0x06dbbd2c (Server + 4); RET.
void* __cdecl GetChatServerInterface() {
    return static_cast<IChatServer*>(&Server);
}

// Retail ordinal 111, 0x06d8eeb0: MOV EAX, .data:Client; RET. This is the object retail
// Server.dll receives, because IServerImpl::Startup hands it rpclocal's own module path.
// The 126-slot proxy behind it lives in src/rpclocal/rpc_client_proxy.cpp.
IClient* __cdecl GetClientInterface() {
    return RpcGetClientProxy();
}

// Retail ordinal 109: &DAT_06dbbce0 == Client + 4 -- the IChatClient sub-object of the
// very same instance, one slot wide.
void* __cdecl GetChatClientInterface() {
    return RpcGetChatClientProxy();
}

} // extern "C"

// ============================================================================
// DllMain
// ============================================================================
// Retail 0x06d8ed90, the whole body:
//   06d8ed94  DEC EAX / JNZ 06d8eda7        ; only DLL_PROCESS_ATTACH (1) does anything
//   06d8ed9c  MOV [DAT_06dbbccc],EAX        ; our HMODULE
//   06d8eda1  CALL DisableThreadLibraryCalls
//   06d8eda7  MOV EAX,1 / RET 0xc
// PROCESS_DETACH is a no-op in retail. Until 2026-09-24 the port called
// IServerImpl::Shutdown here (which FreeLibrary()s Server.dll under the loader lock) and
// logged both reasons; neither exists in the listing.
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    (void)lpReserved;
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_hSelfModule = hModule;              // 06d8ed9c, retail DAT_06dbbccc
        DisableThreadLibraryCalls(hModule);   // 06d8eda1
    }
    return TRUE;                              // 06d8eda7
}
