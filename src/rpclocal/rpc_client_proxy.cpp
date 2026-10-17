// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_client_proxy.cpp -- rpclocal's OWN IClient / IChatClient, retail .data:Client
// (0x06dbbcdc), exported as GetClientInterface (ordinal 111, 0x06d8eeb0).
//
// This is the outgoing half of the server -> client direction. Retail Server.dll does
// LoadLibraryA(SStartupInfo.ModulePath) + GetProcAddress("GetClientInterface") and gets
// THIS object, because IServerImpl::Startup (0x06d8f1e0) overwrote the first 0x104 bytes
// of the SStartupInfo copy it forwards with GetModuleFileNameA(rpclocal's own HMODULE).
// The caller's original path (freelancer.exe) is kept in GCallerModulePath, and Startup
// (slot 10) LoadLibraryA()s that one to reach the REAL client interface.
//
// Every sender slot mirrors the retail shape:
//     FUN_06d96300(Serializer /* this+0x08 */, Channel, Opcode, &OutPacket)
//     ... fill the payload from OutPacket+0x20 ...
//     this->vt[+0x24](x, OutPacket)              // PushToClient (slot 9), the outbound enqueue
// Channel/opcode, the FLPACKET_* name, the arity and the retail address of every slot
// come from docs/re/rpclocal.md, cross-checked against src/include/rpc_iclient.h: the
// header's argument counts and the doc's RET n / 4 column agree on all 126 slots.
//
// The header declares every slot with its typed signature (2026-10-16). The per-type
// payload layout still lives in the Marshal/Deliver pairs of rpc_payload_ch1.cpp /
// rpc_payload_ch2.cpp, whose Marshal side reads the sender's arguments as the retail
// stack image -- one dword per argument slot, a by-reference or pointer argument as its
// address, a by-value Costume as its thirteen dwords. Each slot below converts its typed
// arguments into that image explicitly (RpcArgAddress / RpcArgPointer / RpcArgFloatBits);
// every (channel, opcode) a slot sends has a codec (checked 2026-09-24: 102 of 102).

#include "rpc_transport.h"
#include "rpc_payloads.h"
#include "rpc_iclient.h"
#include "rpc_iclient_args.h"
#include "common_server_types.h"
#include "common_reputation.h"  // Costume, by value in SendServerSendComm

#include <cstdint>
#include <cstring>
#include <intrin.h>     // _ReturnAddress, the SENDCOMM frame image

// ---------------------------------------------------------------------------
// IClient's special members. src/include/rpc_iclient.h declares them out of line
// (retail rpclocal exports ??0IClient@@QAE@XZ and ??4IClient@@QAEAAV0@ABV0@@Z next to
// the vtable symbol), so every module that derives from IClient needs a definition of
// its own -- Server.dll has one in src/Server/IServerImpl.cpp, and this is rpclocal's.
// The class is abstract and carries no data, so all three are trivial.
// ---------------------------------------------------------------------------
IClient::IClient() {
}

IClient::IClient(const IClient& Other) {
    (void)Other;
}

IClient& IClient::operator=(const IClient& Other) {
    (void)Other;
    return *this;
}

// ---------------------------------------------------------------------------
// The argument-image conversions. Retail's senders read their arguments straight off
// their own stack frame: a by-reference / pointer argument is the address in that slot,
// a float is its bit pattern (FLD float ptr [ESP+n]). The codecs consume exactly that
// image, so these three produce it from the typed parameters.
// ---------------------------------------------------------------------------
template<typename T>
static unsigned int RpcArgAddress(const T& Value) {
    return static_cast<unsigned int>(reinterpret_cast<uintptr_t>(&Value));
}

static unsigned int RpcArgPointer(const void* Value) {
    return static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Value));
}

static unsigned int RpcArgFloatBits(const float& Value) {
    unsigned int Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

// A Costume by value occupies thirteen argument dwords (RET 0x5c of 0x06d88fc0).
static_assert(sizeof(Costume) == 13 * sizeof(unsigned int), "Costume is 13 dwords by value");

// retail .data:0x06dbbd18 -- set by Update (slot 16) the first time it runs while
// connected (06d8efe4), cleared by DisconnectClient (slot 17, 06d8f072). Nothing else
// touches it.
static bool GbFlag_06dbbd18 = false;
// retail .data:0x06dbbd19 -- the connected byte, shared with IServerImpl; see rpc_transport.h.
bool GbRpcConnected = false;

// retail .data:0x06dbbd0c / 0x06dbbd10 -- the interfaces Startup (slot 10, 0x06d8eed0) gets
// from the real client module; Startup copies them into GRealClient / GRealChatClient
// (.data:0x06dbbcd8 / 0x06dbbcd4) and Shutdown (slot 15, 0x06d8ef80) clears only these two.
static IClient* GCachedRealClient = nullptr;
static IChatClient* GCachedRealChatClient = nullptr;
// retail .data:0x06dbbd14 -- the module handle Startup gets from LoadLibraryA (06d8eee0)
// and Shutdown hands to FreeLibrary (06d8ef98) and clears (06d8ef9e). A module global,
// not a member: the Client object is exactly the two vptrs and the serializer pointer.
static HMODULE GRealClientModule = nullptr;
// retail .data:0x06dbbd1c -- the IClient proxy's copy of the CDAConnection pointer: written
// only by Update (06d8efdf, FUN_06d88280's answer once connected), read by Update
// (06d8f026, 06d8f04c) and PushToClient (06d8f080). Never cleared: after
// IServerImpl::Shutdown freed the object it dangles, as in retail (see GRpcConnection in
// rpc_transport.h).
static FRpcConnection* GClientConnection = nullptr;

// ===========================================================================
// FLocalClientProxy -- retail .data:Client, one object with two vtables (IClient at
// +0x00, IChatClient at +0x04), exactly like IServerImpl = IServer + IChatServer.
// Its constructor is LAB_06d8ee90: both vptrs plus a zeroed this+0x08 (the serializer).
// ===========================================================================
class FLocalClientProxy : public IClient, public IChatClient {
public:
    // LAB_06d8ee90: 06d8ee90 MOV [DAT_06dbbce4],0 (Client+0x08), then the IClient and the
    // IChatClient vptrs -- the compiler installs those two.
    FLocalClientProxy() : Serializer(nullptr) {
    }

    // -----------------------------------------------------------------------
    // Sender helpers -- the shape every retail sender slot repeats.
    // -----------------------------------------------------------------------
    void SendPacket(unsigned int Channel, unsigned int Opcode,
                    const unsigned int* Args, unsigned int ArgCount) {
        // Retail: this->vt[+0x24](param_1, Packet) -- e.g. 06d883c3 MOV ECX,[ESP+0xc] /
        // 06d883d1 PUSH ECX: the slot's own first argument.
        SendPacketEx(Channel, Opcode, ArgCount > 0 ? Args[0] : 0u, Args, ArgCount);
    }

    // As SendPacket, for the senders whose first parameter is a reference (the client id is
    // passed as `const unsigned int&`, Args[0] = its address): retail reads the dword
    // through it and hands THAT to PushToClient -- e.g. SendServerSetCash 06d8b238 MOV
    // EDX,[ESP+0xc] / 06d8b23f MOV ECX,[EDX] / PUSH ECX. The 21 senders that dereference
    // (rpc_iclient.h, re-checked 2026-10-16 on every sender of the vtable at 06db3978):
    // slots 50, 51, 70, 71, 72, 73, 75, 76, 77, 78, 79, 80, 81, 83, 85, 86, 87, 88, 89, 90
    // and 100; all other sender slots push param_1 itself. PushToClient never reads the
    // value (06d8f08a reads only [ESP+8], the packet), so the read is the whole of the
    // difference.
    void SendPacketClientRef(unsigned int Channel, unsigned int Opcode,
                             const unsigned int* Args, unsigned int ArgCount) {
        const unsigned int ClientId = *reinterpret_cast<const unsigned int*>(Args[0]);
        SendPacketEx(Channel, Opcode, ClientId, Args, ArgCount);
    }

    // As SendPacket, but with the value retail hands to PushToClient given separately from
    // the dwords the payload codec marshals. FLPACKET_COMMON_PLAYER_TRADE needs that split:
    // its payload starts with a sub-type literal that is not one of the slot's arguments.
    //
    // Every retail IClient sender slot is the same shape (0x06d883a0 as the model):
    //   06d883a9 MOV ECX,[ESI+0x8]  ; the serializer, untested
    //   06d883ac PUSH op / PUSH ch / 06d883b0 CALL FUN_06d96300 (Allocate, 0 = success)
    //   06d883b5 TEST EAX,EAX / JNZ -> epilogue
    //   payload writes from packet+0x20
    //   06d883d4 CALL [EDX+0x24]    ; PushToClient (param_1, packet)
    // Removed here, because none of it is in any sender slot: the lazy RegisterAll, the
    // raw-dword fallback for a missing codec (all 102 (channel, opcode) pairs these slots
    // send have a Marshal in GChannel1Codecs / GChannel2Codecs), the SendCount increment
    // and the no-codec log line.
    void SendPacketEx(unsigned int Channel, unsigned int Opcode, unsigned int PushClientId,
                      const unsigned int* Args, unsigned int ArgCount) {
        FPacketBase* Packet = nullptr;
        if (Serializer->Allocate(Channel, Opcode, &Packet) != 0) {   // 06d883a9 / 06d883b0
            return;
        }
        if (!RpcFindPayloadCodec(Channel, Opcode)->Marshal(Packet, Args, ArgCount)) {
            Packet->Release();
            return;
        }
        PushToClient(PushClientId, Packet->AsRaw());
    }

    // Channel 1 opcode 0x12 FLPACKET_COMMON_PLAYER_TRADE carries eight logical messages
    // discriminated by a bitmask in the payload's first dword (packet+0x20); retail's
    // sub-dispatcher inside HandlePacket reads exactly that dword. Each of the eight
    // retail senders writes its own literal there and then serializes only the fields its
    // sub-type needs -- MarshalPlayerTrade replays that, so what it gets here is
    // { SubType, arg 2, arg 3 } with the client id kept back for PushToClient.
    void SendTradePacket(unsigned int Subtype, const unsigned int* Args, unsigned int ArgCount) {
        unsigned int Marshalled[4];
        Marshalled[0] = Subtype;
        Marshalled[1] = 0;
        Marshalled[2] = 0;
        Marshalled[3] = 0;
        unsigned int Count = (ArgCount > 0) ? (ArgCount - 1) : 0u;
        if (Count > 3) {
            Count = 3;
        }
        for (unsigned int i = 0; i < Count; ++i) {
            Marshalled[i + 1] = Args[i + 1];
        }
        SendPacketEx(1, 0x12, (ArgCount > 0) ? Args[0] : 0u, Marshalled, Count + 1);
    }

    // +0x000  0x06d8b310  ch1 op 0x02  FLPACKET_COMMON_FIREWEAPON
    void SendCommonFireWeapon(unsigned int ClientId, const XFireWeaponInfo& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(1, 0x02, Args, 2);
    }

    // +0x004  0x06d8bbb0  ch1 op 0x08  FLPACKET_COMMON_ACTIVATEEQUIP
    void SendCommonActivateEquip(unsigned int ClientId, const XActivateEquip& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(1, 0x08, Args, 2);
    }

    // +0x008  0x06d8bc10  ch1 op 0x0e  FLPACKET_COMMON_ACTIVATECRUISE
    void SendCommonActivateCruise(unsigned int ClientId, const XActivateCruise& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(1, 0x0e, Args, 2);
    }

    // +0x00c  0x06d8bc60  ch1 op 0x15  FLPACKET_COMMON_ACTIVATETHRUSTERS
    void SendCommonActivateThrusters(unsigned int ClientId, const XActivateThrusters& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(1, 0x15, Args, 2);
    }

    // +0x010  0x06d8bd50  ch1 op 0x04  FLPACKET_COMMON_SETTARGET
    void SendCommonSetTarget(unsigned int ClientId, const XSetTarget& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(1, 0x04, Args, 2);
    }

    // +0x014  0x06d8be10  ch1 op 0x1d  FLPACKET_COMMON_TRACTOROBJECTS
    void SendCommonTractorObjects(unsigned int ClientId, const XTractorObjects& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(1, 0x1d, Args, 2);
    }

    // +0x018  0x06d8bcb0  ch1 op 0x0f  FLPACKET_COMMON_GOTRADELANE
    void SendCommonGoTradeLane(unsigned int ClientId, const XGoTradelane& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(1, 0x0f, Args, 2);
    }

    // +0x01c  0x06d8bd10  ch1 op 0x10  FLPACKET_COMMON_STOPTRADELANE
    void SendCommonStopTradeLane(unsigned int ClientId, unsigned int ShipId,
                                 unsigned int TradelaneRing1, unsigned int TradelaneRing2) override {
        const unsigned int Args[4] = { ClientId, ShipId, TradelaneRing1, TradelaneRing2 };
        SendPacket(1, 0x10, Args, 4);
    }

    // +0x020  0x06d8bdb0  ch1 op 0x14  FLPACKET_COMMON_JETTISONCARGO
    void SendCommonJettisonCargo(unsigned int ClientId, const XJettisonCargo& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(1, 0x14, Args, 2);
    }

    // +0x024  0x06d8f080 -- the single outbound enqueue point.
    // Retail: FUN_06d85e00(DAT_06dbbd1c /* the CDAConnection */, Packet). Every other
    // sender slot reaches the queue through this one (this->vt[+0x24](x, Packet)).
    //   06d8f080 MOV ECX,[DAT_06dbbd1c] / 06d8f086 TEST / 06d8f088 JZ -> 06d8f094 RET 8
    //   06d8f08a PUSH [ESP+8] (the packet) / 06d8f08f CALL FUN_06d85e00
    // Retail reads only [ESP+8]; the first argument -- the client id every sender passes,
    // by value or read through its reference -- is never used. Before the pump has
    // fetched the connection (Update, once IServerImpl::Connect set the connected byte)
    // the packet is dropped and never released, as in retail; FUN_06d85e00's answer is
    // discarded. Every type is queued, SET_WEAPON_GROUP 0x11 included.
    void PushToClient(unsigned int ClientId, CDAPacket* Packet) override {
        (void)ClientId;
        if (GClientConnection != nullptr) {
            GClientConnection->PushClientBound(reinterpret_cast<FPacketBase*>(Packet));
        }
    }

    // +0x028  0x06d8eed0
    // Retail: LoadLibraryA(.data:0x06dbbbc0 -- the module path IServerImpl::Startup saved,
    // i.e. freelancer.exe) -> GetClientInterface / GetChatClientInterface, caches them in
    // .data:0x06dbbcd8 / 0x06dbbcd4, forwards BOTH of its arguments to the real client's
    // vtable+0x28, then builds the packet serializer at this+0x08 and registers channels
    // 2 and 1 on it. Returns the real client's answer.
    //
    //   06d8eed1 PUSH DAT_06dbbbc0 / 06d8eed8 LoadLibraryA / 06d8eee0 -> DAT_06dbbd14
    //   06d8eee5 JZ 06d8ef12 -- on failure the cached interfaces keep whatever they held
    //   06d8eeee GetProcAddress("GetClientInterface") / 06d8eef6 CALL EAX -> DAT_06dbbd0c
    //   06d8ef02 GetProcAddress("GetChatClientInterface") / 06d8ef0a CALL EAX -> DAT_06dbbd10
    //            (neither address is tested; LoadLibraryA runs on EVERY call, no caching)
    //   06d8ef12 TEST DAT_06dbbd0c / JZ -> return 0
    //   06d8ef1c DAT_06dbbcd4 = DAT_06dbbd10 ; 06d8ef31 DAT_06dbbcd8 = DAT_06dbbd0c
    //   06d8ef3a CALL [EAX+0x28](MaxPlayers, Port) / TEST AL / JZ -> return 0
    //   06d8ef41 FUN_06d95e50 -> this+0x08 / JZ -> 0 ; 06d8ef4e FUN_06d9bc80 (ch2) / JNZ -> 0 ;
    //   06d8ef5e FUN_06d9ace0 (ch1) / JNZ -> 0 ; 06d8ef6a MOV AL,1
    bool Startup(unsigned int MaxPlayers, unsigned int Port) override {
        typedef IClient* (__cdecl *GetClientInterfaceFn)();
        typedef IChatClient* (__cdecl *GetChatClientInterfaceFn)();

        GRealClientModule = LoadLibraryA(GCallerModulePath);
        if (GRealClientModule != nullptr) {
            GetClientInterfaceFn GetClient =
                (GetClientInterfaceFn)GetProcAddress(GRealClientModule, "GetClientInterface");
            GCachedRealClient = GetClient();
            GetChatClientInterfaceFn GetChat =
                (GetChatClientInterfaceFn)GetProcAddress(GRealClientModule, "GetChatClientInterface");
            GCachedRealChatClient = GetChat();
        }
        if (GCachedRealClient == nullptr) {
            return false;
        }
        GRealChatClient = GCachedRealChatClient;
        GRealClient = GCachedRealClient;
        if (!GRealClient->Startup(MaxPlayers, Port)) {
            return false;
        }
        // 06d8ef41 CALL FUN_06d95e50 / 06d8ef48 MOV [EDI+0x8],EAX / 06d8ef4b JZ -> false
        Serializer = RpcGetPacketSerializer();
        if (Serializer == nullptr) {
            return false;
        }
        // 06d8ef4e FUN_06d9bc80 (channel 2) / 06d8ef58 JNZ -> false
        if (RpcRegisterChannel2Types(Serializer) != 0) {
            return false;
        }
        // 06d8ef5a MOV EAX,[EDI+0x8] / 06d8ef5e FUN_06d9ace0 (channel 1) / 06d8ef68 JNZ -> false
        if (RpcRegisterChannel1Types(Serializer) != 0) {
            return false;
        }
        return true;                                    // 06d8ef6a MOV AL,1
    }

    // +0x02c  0x06d8eae0 -- a bare RET 4, shared with IServer's GetServerStats /
    // OnConnect / SetActiveConnection. Incoming ch2 op 0x01 FLPACKET_SERVER_CONNECTRESPONSE
    // is delivered to THIS slot by IServerImpl::HandlePacket, and retail does nothing with it.
    void OnConnect(unsigned int ClientId) override {
        (void)ClientId;
    }

    // +0x030  0x06d883a0  ch2 op 0x02  FLPACKET_SERVER_LOGINRESPONSE
    void SendServerLoginResponse(unsigned int ClientId, const unsigned char& Status) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Status) };
        SendPacket(2, 0x02, Args, 2);
    }

    // +0x034  0x06d89890  ch2 op 0x03  FLPACKET_SERVER_CHARACTERINFO
    void SendServerCharacterInfo(unsigned int ClientId, const CCharacterSet& Characters) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Characters) };
        SendPacket(2, 0x03, Args, 2);
    }

    // +0x038  0x06d8a030  ch2 op 0x08  FLPACKET_SERVER_CHARSELECTVERIFIED
    void SendServerCharSelectVerified(unsigned int ClientId,
                                      const FServerCharSelectVerifiedInfo& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x08, Args, 2);
    }

    // +0x03c  0x06d8ef80
    //   06d8ef80 MOV ECX,[DAT_06dbbd0c] / 06d8ef89 CALL [EAX+0x3c]   ; untested
    //   06d8ef93 if DAT_06dbbd14 : FreeLibrary, DAT_06dbbd14 = 0, DAT_06dbbd10 = 0
    //   06d8efaa DAT_06dbbd0c = 0                                  ; unconditional
    // The published copies DAT_06dbbcd8 / 06dbbcd4 (GRealClient / GRealChatClient) and the
    // two pump flags are NOT touched.
    void Shutdown() override {
        GCachedRealClient->Shutdown();
        if (GRealClientModule != nullptr) {
            FreeLibrary(GRealClientModule);
            GRealClientModule = nullptr;
            GCachedRealChatClient = nullptr;
        }
        GCachedRealClient = nullptr;
    }

    // +0x040  0x06d8efc0 -- the client-side pump. IServerImpl::Update calls it as
    // FUN_06d8efc0(&Client) at 06d8f448.
    //   06d8efc1 TEST [DAT_06dbbd18] / JNZ 06d8f01d
    //   06d8efcd TEST [DAT_06dbbd19] / JZ 06d8f05b          ; not connected -> return 1
    //   06d8efda CALL FUN_06d88280 -> DAT_06dbbd1c          ; create only, no Start
    //   06d8efe4 DAT_06dbbd18 = 1
    //   06d8efeb ECX=[this+0x8] / TEST / JZ 06d8f01d        ; serializer
    //   06d8effb FUN_06d96300(2, 1, &pkt) / JNZ 06d8f01d    ; CONNECTRESPONSE
    //   06d8f008 [pkt+0x20] = 1 / 06d8f01a CALL [EDX+0x24](1, pkt)
    //   06d8f01d TEST [DAT_06dbbd19] / JZ 06d8f05b
    //   06d8f026 conn=[DAT_06dbbd1c]; [conn] null or [[conn]+0x2c]==0 -> 06d8f05b
    //   06d8f039 FUN_06d86580 pop / 06d8f047 FUN_06d86a70(1, pkt) / pop again until null
    //   06d8f05b MOV EAX,1
    // FUN_06d86a70 releases the packet itself at its common exit (06d87c69 CALL
    // FUN_06d8f4a0 on EBP = the packet); RpcDispatchToServer does not, so the Release sits
    // after the call here.
    int Update() override {
        if (!GbFlag_06dbbd18) {
            if (!GbRpcConnected) {
                return 1;                                   // 06d8efd4 JZ 06d8f05b
            }
            GClientConnection = RpcCreateConnection();      // 06d8efda / 06d8efdf
            GbFlag_06dbbd18 = true;
            // 06d8efeb MOV ECX,[ESI+0x8] / TEST / JZ 06d8f01d -- no serializer (Startup
            // has not run), no CONNECTRESPONSE; the drain below still runs.
            if (Serializer != nullptr) {
                const unsigned int NoArgs[1] = { 0 };
                SendPacketEx(2, 0x01, 1u, NoArgs, 0);   // FLPACKET_SERVER_CONNECTRESPONSE
            }
        }
        if (!GbRpcConnected) {
            return 1;                                       // 06d8f024 JZ 06d8f05b
        }
        // 06d8f026 MOV ECX,[DAT_06dbbd1c] (untested) ; PopServerBound answers null for a
        // missing or empty queue, which is the 06d8f02e / 06d8f037 test; the pointer is
        // re-read for every pop (06d8f04c).
        FPacketBase* Packet = GClientConnection->PopServerBound();
        while (Packet != nullptr) {
            RpcDispatchToServer(Packet, 1u);
            Packet->Release();
            Packet = GClientConnection->PopServerBound();
        }
        return 1;                                           // 06d8f05b MOV EAX,0x1
    }

    // +0x044  0x06d8f070 -- 06d8f070 XOR AL,AL / 06d8f072 -> DAT_06dbbd18 /
    // 06d8f077 -> DAT_06dbbd19 / RET 4.
    void DisconnectClient(unsigned int ClientId) override {
        (void)ClientId;
        GbFlag_06dbbd18 = false;
        GbRpcConnected = false;
    }

    // +0x048  0x06d8f0a0 -- XOR EAX,EAX; RET 4. Always 0.
    unsigned int GetSendQSize(unsigned int ClientId) override {
        (void)ClientId;
        return 0;
    }

    // +0x04c  0x06d8f0a0 -- literally the same code as GetSendQSize. Always 0.
    unsigned int GetSendQBytes(unsigned int ClientId) override {
        (void)ClientId;
        return 0;
    }

    // +0x050  0x06d8f0b0 -- FLD [0.0]; RET 4. Always 0.0.
    double GetLinkSaturation(unsigned int ClientId) override {
        (void)ClientId;
        return 0.0;
    }

    // +0x054  0x06d883e0  ch2 op 0x23  FLPACKET_SERVER_SETSHIPARCH
    void SendServerSetShipArch(unsigned int ClientId, unsigned int ShipArchId) override {
        const unsigned int Args[2] = { ClientId, ShipArchId };
        SendPacket(2, 0x23, Args, 2);
    }

    // +0x058  0x06d88420  ch2 op 0x49  FLPACKET_SERVER_SETHULLSTATUS
    // The codec copies the argument slot's bits (retail 06d8843d FLD float ptr [ESP+0x10]).
    void SendServerSetHullStatus(unsigned int ClientId, float HullStatus) override {
        const unsigned int Args[2] = { ClientId, RpcArgFloatBits(HullStatus) };
        SendPacket(2, 0x49, Args, 2);
    }

    // +0x05c  0x06d88460  ch2 op 0x48  FLPACKET_SERVER_SETCOLLISIONGROUPS
    void SendServerSetCollisionGroups(unsigned int ClientId,
                                      const std::list<CollisionGroupDesc>& Groups) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Groups) };
        SendPacket(2, 0x48, Args, 2);
    }

    // +0x060  0x06d884a0  ch2 op 0x24  FLPACKET_SERVER_SETEQUIPMENT
    void SendServerSetEquipment(unsigned int ClientId, const EquipDescVector& Equipment) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Equipment) };
        SendPacket(2, 0x24, Args, 2);
    }

    // +0x064  0x06d88530  ch2 op 0x25  FLPACKET_SERVER_SETCARGO
    void SendServerSetCargo(unsigned int ClientId, const EquipDescVector& Cargo) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Cargo) };
        SendPacket(2, 0x25, Args, 2);
    }

    // +0x068  0x06d885c0  ch2 op 0x2e  FLPACKET_SERVER_SETADDITEM
    void SendServerSetAddItem(unsigned int ClientId, int AddResult, const EquipDesc& Item) override {
        const unsigned int Args[3] = { ClientId, static_cast<unsigned int>(AddResult),
                                       RpcArgAddress(Item) };
        SendPacket(2, 0x2e, Args, 3);
    }

    // +0x06c  0x06d88670  ch2 op 0x2f  FLPACKET_SERVER_SETREMOVEITEM
    void SendServerSetRemoveItem(unsigned int ClientId, int RemoveResult, unsigned short ItemId,
                                 int Count) override {
        const unsigned int Args[4] = { ClientId, static_cast<unsigned int>(RemoveResult),
                                       static_cast<unsigned int>(ItemId),
                                       static_cast<unsigned int>(Count) };
        SendPacket(2, 0x2f, Args, 4);
    }

    // +0x070  0x06d886d0  ch2 op 0x0d  FLPACKET_SERVER_SETSTARTROOM
    void SendServerSetStartRoom(unsigned int ClientId, unsigned int BaseId, unsigned int RoomId) override {
        const unsigned int Args[3] = { ClientId, BaseId, RoomId };
        SendPacket(2, 0x0d, Args, 3);
    }

    // +0x074  0x06d88720  ch2 op 0x15  FLPACKET_SERVER_GFDESTROYCHARACTER
    void SendServerGFDestroyCharacter(unsigned int ClientId, unsigned int RoomId, unsigned int Key) override {
        const unsigned int Args[3] = { ClientId, RoomId, Key };
        SendPacket(2, 0x15, Args, 3);
    }

    // +0x078  0x06d88770  ch2 op 0x26  FLPACKET_SERVER_GFUPDATECHAR
    void SendServerGFUpdateChar(unsigned int ClientId, const unsigned char* Data, unsigned int Size) override {
        const unsigned int Args[3] = { ClientId, RpcArgPointer(Data), Size };
        SendPacket(2, 0x26, Args, 3);
    }

    // +0x07c  0x06d887c0  ch2 op 0x0f  FLPACKET_SERVER_GFCOMPLETECHARLIST
    void SendServerGFCompleteCharList(unsigned int ClientId, unsigned int RoomId) override {
        const unsigned int Args[2] = { ClientId, RoomId };
        SendPacket(2, 0x0f, Args, 2);
    }

    // +0x080  0x06d88800  ch2 op 0x1a  FLPACKET_SERVER_GFSCRIPTBEHAVIOR
    void SendServerGFScriptBehavior(unsigned int ClientId, const unsigned char* Data, unsigned int Size) override {
        const unsigned int Args[3] = { ClientId, RpcArgPointer(Data), Size };
        SendPacket(2, 0x1a, Args, 3);
    }

    // +0x084  0x06d88850  ch2 op 0x17  FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR
    void SendServerGFDestroyScriptBehavior(unsigned int ClientId, unsigned int RoomId,
                                           unsigned int BehaviorId) override {
        const unsigned int Args[3] = { ClientId, RoomId, BehaviorId };
        SendPacket(2, 0x17, Args, 3);
    }

    // +0x088  0x06d888a0  ch2 op 0x11  FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST
    void SendServerGFCompleteScriptBehaviorList(unsigned int ClientId, unsigned int RoomId) override {
        const unsigned int Args[2] = { ClientId, RoomId };
        SendPacket(2, 0x11, Args, 2);
    }

    // +0x08c  0x06d888e0  ch2 op 0x1f  FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT
    void SendServerGFUpdateAmbientScript(unsigned int ClientId, const unsigned char* Data,
                                         unsigned int Size) override {
        const unsigned int Args[3] = { ClientId, RpcArgPointer(Data), Size };
        SendPacket(2, 0x1f, Args, 3);
    }

    // +0x090  0x06d88930  ch2 op 0x19  FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT
    void SendServerGFDestroyAmbientScript(unsigned int ClientId, unsigned int RoomId, unsigned int Key) override {
        const unsigned int Args[3] = { ClientId, RoomId, Key };
        SendPacket(2, 0x19, Args, 3);
    }

    // +0x094  0x06d88980  ch2 op 0x13  FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST
    void SendServerGFCompleteAmbientScriptList(unsigned int ClientId, unsigned int RoomId) override {
        const unsigned int Args[2] = { ClientId, RoomId };
        SendPacket(2, 0x13, Args, 2);
    }

    // +0x098  0x06d889c0  ch2 op 0x16  FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER
    void SendServerGFDestroyMissionComputer(unsigned int ClientId, unsigned int BaseId, unsigned int Key) override {
        const unsigned int Args[3] = { ClientId, BaseId, Key };
        SendPacket(2, 0x16, Args, 3);
    }

    // +0x09c  0x06d88a10  ch2 op 0x1d  FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER
    void SendServerGFUpdateMissionComputer(unsigned int ClientId, const unsigned char* Data,
                                           unsigned int Size) override {
        const unsigned int Args[3] = { ClientId, RpcArgPointer(Data), Size };
        SendPacket(2, 0x1d, Args, 3);
    }

    // +0x0a0  0x06d88a60  ch2 op 0x10  FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST
    void SendServerGFCompleteMissionComputerList(unsigned int ClientId, unsigned int BaseId) override {
        const unsigned int Args[2] = { ClientId, BaseId };
        SendPacket(2, 0x10, Args, 2);
    }

    // +0x0a4  0x06d88aa0  ch2 op 0x20  FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE
    void SendServerGFMissionVendorAcceptance(unsigned int ClientId, const unsigned char* Data,
                                             unsigned int Size) override {
        const unsigned int Args[3] = { ClientId, RpcArgPointer(Data), Size };
        SendPacket(2, 0x20, Args, 3);
    }

    // +0x0a8  0x06d88af0  ch2 op 0x5a  FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY
    void SendServerGFMissionVendorWhyEmpty(unsigned int ClientId, unsigned int Reason) override {
        const unsigned int Args[2] = { ClientId, Reason };
        SendPacket(2, 0x5a, Args, 2);
    }

    // +0x0ac  0x06d88b30  ch2 op 0x14  FLPACKET_SERVER_GFDESTROYNEWSBROADCAST
    void SendServerGFDestroyNewsBroadcast(unsigned int ClientId, unsigned int BaseId, unsigned int Key) override {
        const unsigned int Args[3] = { ClientId, BaseId, Key };
        SendPacket(2, 0x14, Args, 3);
    }

    // +0x0b0  0x06d88b80  ch2 op 0x1e  FLPACKET_SERVER_GFUPDATENEWSBROADCAST
    void SendServerGFUpdateNewsBroadcast(unsigned int ClientId, const unsigned char* Data,
                                         unsigned int Size) override {
        const unsigned int Args[3] = { ClientId, RpcArgPointer(Data), Size };
        SendPacket(2, 0x1e, Args, 3);
    }

    // +0x0b4  0x06d88bd0  ch2 op 0x0e  FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST
    void SendServerGFCompleteNewsBroadcastList(unsigned int ClientId, unsigned int BaseId) override {
        const unsigned int Args[2] = { ClientId, BaseId };
        SendPacket(2, 0x0e, Args, 2);
    }

    // +0x0b8  0x06d89190  ch2 op 0x42  FLPACKET_SERVER_CREATESOLAR
    void SendServerCreateSolar(unsigned int ClientId, const CSolarCreateInfo& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x42, Args, 2);
    }

    // +0x0bc  0x06d89410  ch2 op 0x04  FLPACKET_SERVER_CREATESHIP
    void SendServerCreateShip(unsigned int ClientId, const CShipCreateInfo& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x04, Args, 2);
    }

    // +0x0c0  0x06d8aae0  ch2 op 0x28  FLPACKET_SERVER_CREATELOOT
    void SendServerCreateLoot(unsigned int ClientId, const FServerCreateLootRequest& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x28, Args, 2);
    }

    // +0x0c4  0x06d8a180  ch2 op 0x2c  FLPACKET_SERVER_CREATEMINE
    void SendServerCreateMine(unsigned int ClientId, const FServerCreateProjectileInfo& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x2c, Args, 2);
    }

    // +0x0c8  0x06d8a730  ch2 op 0x37  FLPACKET_SERVER_CREATEGUIDED
    void SendServerCreateGuided(const unsigned int& ClientId, const FServerCreateGuidedInfo& Info) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), RpcArgAddress(Info) };
        SendPacketClientRef(2, 0x37, Args, 2);
    }

    // +0x0cc  0x06d8a270  ch2 op 0x2d  FLPACKET_SERVER_CREATECOUNTER
    // ClientId by reference: 06d8a34a MOV ECX,[ESP+0xc] / 06d8a351 MOV EAX,[ECX] / PUSH EAX
    // before PushToClient. (Until 2026-10-16 the port pushed the argument itself.)
    void SendServerCreateCounter(const unsigned int& ClientId,
                                 const FServerCreateProjectileInfo& Info) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), RpcArgAddress(Info) };
        SendPacketClientRef(2, 0x2d, Args, 2);
    }

    // +0x0d0  0x06d8a360  ch2 op 0x31  FLPACKET_SERVER_EXPLODEASTEROIDMINE
    void SendServerExplodeAsteroidMine(unsigned int ClientId, const FMineExplosion& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x31, Args, 2);
    }

    // +0x0d4  0x06d8a800  ch2 op 0x4a  FLPACKET_SERVER_SETGUIDEDTARGET
    void SendServerSetGuidedTarget(unsigned int ClientId, unsigned int GuidedId, unsigned int TargetId,
                                   unsigned short TargetSubObjId) override {
        const unsigned int Args[4] = { ClientId, GuidedId, TargetId,
                                       static_cast<unsigned int>(TargetSubObjId) };
        SendPacket(2, 0x4a, Args, 4);
    }

    // +0x0d8  0x06d897c0  ch1 op 0x01  FLPACKET_COMMON_UPDATEOBJECT
    void SendCommonUpdateObject(unsigned int ClientId, const FCommonUpdateObjectInfo& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(1, 0x01, Args, 2);
    }

    // +0x0dc  0x06d89140  ch2 op 0x06  FLPACKET_SERVER_DESTROYOBJECT
    void SendServerDestroyObject(unsigned int ClientId, const FServerDestroyObjectInfo& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x06, Args, 2);
    }

    // +0x0e0  0x06d89770  ch2 op 0x0a  FLPACKET_SERVER_ACTIVATEOBJECT
    void SendServerActivateObject(unsigned int ClientId, const FPayloadServerActivateObject& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x0a, Args, 2);
    }

    // +0x0e4  0x06d8a860  ch2 op 0x21  FLPACKET_SERVER_SYSTEM_SWITCH_OUT
    void SendServerSystemSwitchOut(unsigned int ClientId, const FServerSystemSwitchOutInfo& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x21, Args, 2);
    }

    // +0x0e8  0x06d8a8b0  ch2 op 0x22  FLPACKET_SERVER_SYSTEM_SWITCH_IN
    void SendServerSystemSwitchIn(unsigned int ClientId, const FPayloadServerSystemSwitchIn& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x22, Args, 2);
    }

    // +0x0ec  0x06d8a990  ch2 op 0x0b  FLPACKET_SERVER_LAND
    void SendServerLand(unsigned int ClientId, const FServerLandInfo& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x0b, Args, 2);
    }

    // +0x0f0  0x06d8a080  ch2 op 0x07  FLPACKET_SERVER_LAUNCH
    void SendServerLaunch(unsigned int ClientId, const FServerLaunchInfo& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x07, Args, 2);
    }

    // +0x0f4  0x06d8a130  ch2 op 0x27  FLPACKET_SERVER_REQUESTCREATESHIPRESP
    // The codec keeps the argument slot's low byte (06d8a158 MOV CL,byte ptr [ESP+0x10]).
    void SendServerRequestCreateShipResp(unsigned int ClientId, bool bSuccess, unsigned int ShipId) override {
        const unsigned int Args[3] = { ClientId, bSuccess ? 1u : 0u, ShipId };
        SendPacket(2, 0x27, Args, 3);
    }

    // +0x0f8  0x06d8aa40  ch2 op 0x32  FLPACKET_SERVER_REQUESTSPACESCRIPT
    void SendServerRequestSpaceScript(unsigned int ClientId, const FServerRequestSpaceScriptInfo& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x32, Args, 2);
    }

    // +0x0fc  0x06d88c10  ch2 op 0x05  FLPACKET_SERVER_DAMAGEOBJECT
    void SendServerDamageObject(unsigned int ClientId, unsigned int ObjectId, const DamageList& Damage) override {
        const unsigned int Args[3] = { ClientId, ObjectId, RpcArgAddress(Damage) };
        SendPacket(2, 0x05, Args, 3);
    }

    // +0x100  0x06d88c60  ch2 op 0x38  FLPACKET_SERVER_ITEMTRACTORED
    void SendServerItemTractored(unsigned int ClientId, const FPayloadServerItemTractored& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x38, Args, 2);
    }

    // +0x104  0x06d88cc0  ch2 op 0x51  FLPACKET_SERVER_USE_ITEM
    void SendServerUseItem(unsigned int ClientId, const FPayloadServerUseItem& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x51, Args, 2);
    }

    // +0x108  0x06d88d70  ch2 op 0x29  FLPACKET_SERVER_SETREPUTATION
    void SendServerSetReputation(unsigned int ClientId, const FServerSetReputationInfo& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x29, Args, 2);
    }

    // +0x10c  0x06d88f70  ch2 op 0x2a  FLPACKET_SERVER_ADJUSTATTITUDE
    void SendServerAdjustAttitude(unsigned int ClientId, const FPayloadServerAdjustAttitude& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x2a, Args, 2);
    }

    // +0x110  0x06d88fc0  ch2 op 0x4f  FLPACKET_SERVER_SENDCOMM
    // MarshalSendComm reads the retail frame image (RpcSendCommFrameDword): the 23
    // argument dwords exactly as they sit on retail's stack (RET 0x5c) -- ClientId,
    // SenderId, ReceiverId, VoiceId, the thirteen dwords of the by-value Costume, CommType,
    // TextIds, TextCount, Mood, Duration's bits, the bool's slot -- plus the one other dword
    // retail's Ids loop can reach in this frame: the return address into the caller.
    void SendServerSendComm(unsigned int ClientId, unsigned int SenderId, unsigned int ReceiverId,
                            unsigned int VoiceId, Costume CommCostume, unsigned int CommType,
                            const unsigned int* TextIds, int TextCount, unsigned int Mood,
                            float Duration, bool bReceiverIsOwnShip) override {
        unsigned int Args[24];
        Args[0] = ClientId;
        Args[1] = SenderId;
        Args[2] = ReceiverId;
        Args[3] = VoiceId;
        std::memcpy(&Args[4], &CommCostume, sizeof(Costume));      // Args[4..16]
        Args[17] = CommType;
        Args[18] = RpcArgPointer(TextIds);
        Args[19] = static_cast<unsigned int>(TextCount);
        Args[20] = Mood;
        Args[21] = RpcArgFloatBits(Duration);
        // The sender keeps one byte of this slot (06d890db); a bool argument is 0 or 1.
        Args[22] = bReceiverIsOwnShip ? 1u : 0u;
        Args[23] = (unsigned int)(uintptr_t)_ReturnAddress();
        SendPacket(2, 0x4f, Args, 24);
    }

    // +0x114  0x06d89100  ch2 op 0x56  FLPACKET_SERVER_SENDNNMESSAGE
    void SendServerSendNNMessage(unsigned int ClientId, unsigned int MessageId) override {
        const unsigned int Args[2] = { ClientId, MessageId };
        SendPacket(2, 0x56, Args, 2);
    }

    // +0x118  0x06d8af60  ch2 op 0x45  FLPACKET_SERVER_SET_MISSION_MESSAGE
    void SendServerSetMissionMessage(const unsigned int& ClientId, const CSetMissionMessage& Message) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), RpcArgAddress(Message) };
        SendPacketClientRef(2, 0x45, Args, 2);
    }

    // +0x11c  0x06d8ad90  ch2 op 0x34  FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE
    void SendServerReplaceMissionObjective(const unsigned int& ClientId,
                                           const FServerReplaceMissionObjectiveInfo& Info) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), RpcArgAddress(Info) };
        SendPacketClientRef(2, 0x34, Args, 2);
    }

    // +0x120  0x06d8b020  ch2 op 0x35  FLPACKET_SERVER_SETMISSIONOBJECTIVES
    // The client id is read through its reference (.text:06d8b1f3 MOV EDX,[ESP+0x28] /
    // .text:06d8b1f7 MOV EDX,[EDX] / PUSH EDX before PushToClient).
    void SendServerSetMissionObjectives(const unsigned int& ClientId,
                                        const FServerSetMissionObjectivesInfo& Info) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), RpcArgAddress(Info) };
        SendPacketClientRef(2, 0x35, Args, 2);
    }

    // +0x124  0x06d8ac40  ch2 op 0x33  FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE
    void SendServerSetMissionObjectiveState(const unsigned int& ClientId,
                                            const FServerMissionObjectiveStateInfo& Info) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), RpcArgAddress(Info) };
        SendPacketClientRef(2, 0x33, Args, 2);
    }

    // +0x128  0x06d8aca0  ch2 op 0x5b  FLPACKET_SERVER_MISSIONSAVEA
    void SendServerMissionSaveA(unsigned int ClientId, unsigned long MissionId) override {
        const unsigned int Args[2] = { ClientId, static_cast<unsigned int>(MissionId) };
        SendPacket(2, 0x5b, Args, 2);
    }

    // +0x12c  0x06d8ace0  ch2 op 0x46  FLPACKET_SERVER_MARKOBJ
    void SendServerMarkObj(const unsigned int& ClientId, unsigned int ObjectId, int MarkType) override {
        const unsigned int Args[3] = { RpcArgAddress(ClientId), ObjectId,
                                       static_cast<unsigned int>(MarkType) };
        SendPacketClientRef(2, 0x46, Args, 3);
    }

    // +0x130  0x06d8ad30  ch2 op 0x47  FLPACKET_SERVER_CFGINTERFACENOTIFICATION
    void SendServerCfgInterfaceNotification(const unsigned int& ClientId,
                                            const FPayloadServerCfgInterfaceNotification& Info) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), RpcArgAddress(Info) };
        SendPacketClientRef(2, 0x47, Args, 2);
    }

    // +0x134  0x06d8b210  ch2 op 0x30  FLPACKET_SERVER_SETCASH
    void SendServerSetCash(const unsigned int& ClientId, int Cash) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), static_cast<unsigned int>(Cash) };
        SendPacketClientRef(2, 0x30, Args, 2);
    }

    // +0x138  0x06d8b250  ch2 op 0x3e  FLPACKET_SERVER_SETNUMKILLS
    void SendServerSetNumKills(const unsigned int& ClientId, int NumKills) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), static_cast<unsigned int>(NumKills) };
        SendPacketClientRef(2, 0x3e, Args, 2);
    }

    // +0x13c  0x06d8b290  ch2 op 0x3f  FLPACKET_SERVER_SETMISSIONSUCCESSES
    void SendServerSetMissionSuccesses(const unsigned int& ClientId, int NumSuccesses) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), static_cast<unsigned int>(NumSuccesses) };
        SendPacketClientRef(2, 0x3f, Args, 2);
    }

    // +0x140  0x06d8b2d0  ch2 op 0x40  FLPACKET_SERVER_SETMISSIONFAILURES
    void SendServerSetMissionFailures(const unsigned int& ClientId, int NumFailures) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), static_cast<unsigned int>(NumFailures) };
        SendPacketClientRef(2, 0x40, Args, 2);
    }

    // +0x144  0x06d8a3c0  ch2 op 0x4d  FLPACKET_SERVER_LOADHINT
    void SendServerLoadHint(const unsigned int& ClientId, BaseHint* Hint) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), RpcArgPointer(Hint) };
        SendPacketClientRef(2, 0x4d, Args, 2);
    }

    // +0x148  0x06d8a5c0  ch2 op 0x2b  FLPACKET_SERVER_SETGROUPFEELINGS
    void SendServerSetGroupFeelings(unsigned int ClientId, const FServerSetGroupFeelingsInfo& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x2b, Args, 2);
    }

    // +0x14c  0x06d8a9e0  ch2 op 0x44  FLPACKET_SERVER_REQUEST_RETURNED
    void SendServerRequestReturned(const unsigned int& ClientId, unsigned int ShipId, int RequestType,
                                   int Response, int Extra) override {
        const unsigned int Args[5] = { RpcArgAddress(ClientId), ShipId,
                                       static_cast<unsigned int>(RequestType),
                                       static_cast<unsigned int>(Response),
                                       static_cast<unsigned int>(Extra) };
        SendPacketClientRef(2, 0x44, Args, 5);
    }

    // +0x150  0x06d8bfe0  ch2 op 0x3c  FLPACKET_SERVER_REPAIROBJECT
    void SendServerRepairObject(unsigned int ClientId, const FServerRepairObjectInfo& Info) override {
        const unsigned int Args[2] = { ClientId, RpcArgAddress(Info) };
        SendPacket(2, 0x3c, Args, 2);
    }

    // +0x154  0x06d8c040  ch2 op 0x3d  FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE
    void SendServerRemoteObjectCargoUpdate(const unsigned int& ClientId, unsigned int ObjectId,
                                           const EquipDescVector& Cargo, unsigned char Flag) override {
        const unsigned int Args[4] = { RpcArgAddress(ClientId), ObjectId, RpcArgAddress(Cargo),
                                       static_cast<unsigned int>(Flag) };
        SendPacketClientRef(2, 0x3d, Args, 4);
    }

    // +0x158  0x06d8c0f0  ch2 op 0x55  FLPACKET_SERVER_OBJECTCARGOUPDATE
    void SendServerObjectCargoUpdate(const unsigned int& ClientId, unsigned int ObjectId,
                                     const EquipDesc& Item) override {
        const unsigned int Args[3] = { RpcArgAddress(ClientId), ObjectId, RpcArgAddress(Item) };
        SendPacketClientRef(2, 0x55, Args, 3);
    }

    // +0x15c  0x06d8c170  ch2 op 0x41  FLPACKET_SERVER_BURNFUSE
    void SendServerBurnFuse(const unsigned int& ClientId, const FServerBurnFuseInfo& Info) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), RpcArgAddress(Info) };
        SendPacketClientRef(2, 0x41, Args, 2);
    }

    // +0x160  0x06d8ae90  ch2 op 0x4b  FLPACKET_SERVER_SET_CAMERA
    void SendServerSetCamera(const unsigned int& ClientId, const FPayloadServerSetCamera& Info) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), RpcArgAddress(Info) };
        SendPacketClientRef(2, 0x4b, Args, 2);
    }

    // +0x164  0x06d8af20  ch2 op 0x4c  FLPACKET_SERVER_REVERT_CAMERA
    void SendServerRevertCamera(const unsigned int& ClientId) override {
        const unsigned int Args[1] = { RpcArgAddress(ClientId) };
        SendPacketClientRef(2, 0x4c, Args, 1);
    }

    // +0x168  0x06d8ae50  ch2 op 0x43  FLPACKET_SERVER_SET_STORY_CUE
    void SendServerSetStoryCue(const unsigned int& ClientId, const unsigned int& CueId) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), RpcArgAddress(CueId) };
        SendPacketClientRef(2, 0x43, Args, 2);
    }

    // +0x16c  0x06d8b4f0  ch1 op 0x11  FLPACKET_COMMON_SET_WEAPON_GROUP
    void SendCommonSetWeaponGroup(unsigned int ClientId, const unsigned char* Data, unsigned int Size) override {
        const unsigned int Args[3] = { ClientId, RpcArgPointer(Data), Size };
        SendPacket(1, 0x11, Args, 3);
    }

    // +0x170  0x06d8b540  ch1 op 0x13  FLPACKET_COMMON_SET_VISITED_STATE
    void SendCommonSetVisitedState(unsigned int ClientId, unsigned char* Data, unsigned int Size) override {
        const unsigned int Args[3] = { ClientId, RpcArgPointer(Data), Size };
        SendPacket(1, 0x13, Args, 3);
    }

    // +0x174  0x06d8b590  ch1 op 0x16  FLPACKET_COMMON_REQUEST_BEST_PATH
    void SendCommonRequestBestPath(unsigned int ClientId, const unsigned char* Data, unsigned int Size) override {
        const unsigned int Args[3] = { ClientId, RpcArgPointer(Data), Size };
        SendPacket(1, 0x16, Args, 3);
    }

    // +0x178  0x06d8b610  ch1 op 0x18  FLPACKET_COMMON_REQUEST_PLAYER_STATS
    void SendCommonRequestPlayerStats(unsigned int ClientId, const unsigned char* Data, unsigned int Size) override {
        const unsigned int Args[3] = { ClientId, RpcArgPointer(Data), Size };
        SendPacket(1, 0x18, Args, 3);
    }

    // +0x17c  0x06d8b690  ch1 op 0x1b  FLPACKET_COMMON_POP_UP_DIALOG
    void SendCommonPopUpDialog(unsigned int ClientId, const FmtStr& Title, const FmtStr& Text,
                               unsigned int Buttons) override {
        const unsigned int Args[4] = { ClientId, RpcArgAddress(Title), RpcArgAddress(Text), Buttons };
        SendPacket(1, 0x1b, Args, 4);
    }

    // +0x180  0x06d8b7a0  ch1 op 0x17  FLPACKET_COMMON_REQUEST_GROUP_POSITIONS
    void SendCommonRequestGroupPositions(unsigned int ClientId, unsigned char* Data, unsigned int Size) override {
        const unsigned int Args[3] = { ClientId, RpcArgPointer(Data), Size };
        SendPacket(1, 0x17, Args, 3);
    }

    // +0x184  0x06d8b7f0  ch1 op 0x19  FLPACKET_COMMON_SET_MISSION_LOG
    void SendCommonSetMissionLog(unsigned int ClientId, unsigned char* Data, unsigned int Size) override {
        const unsigned int Args[3] = { ClientId, RpcArgPointer(Data), Size };
        SendPacket(1, 0x19, Args, 3);
    }

    // +0x188  0x06d8b840  ch1 op 0x1c  FLPACKET_COMMON_SET_INTERFACE_STATE
    void SendCommonSetInterfaceState(unsigned int ClientId, unsigned char* Data, unsigned int Size) override {
        const unsigned int Args[3] = { ClientId, RpcArgPointer(Data), Size };
        SendPacket(1, 0x1c, Args, 3);
    }

    // +0x18c  0x06d8b890  ch1 op 0x1a  FLPACKET_COMMON_REQUEST_RANK_LEVEL
    void SendCommonRequestRankLevel(unsigned int ClientId, unsigned char* Data, unsigned int Size) override {
        const unsigned int Args[3] = { ClientId, RpcArgPointer(Data), Size };
        SendPacket(1, 0x1a, Args, 3);
    }

    // +0x190  0x06d8a480  ch2 op 0x4e  FLPACKET_SERVER_SETDIRECTIVE
    // ClientId by reference: 06d8a5a2 MOV EDX,[ESP+0x14] / 06d8a5a9 MOV ECX,[EDX] / PUSH ECX
    // before PushToClient. (Until 2026-10-16 the port pushed the argument itself.)
    void SendServerSetDirective(const unsigned int& ClientId, const FPayloadServerSetDirective& Info) override {
        const unsigned int Args[2] = { RpcArgAddress(ClientId), RpcArgAddress(Info) };
        SendPacketClientRef(2, 0x4e, Args, 2);
    }

    // +0x194  0x06d8b8e0  ch1 op 0x12  FLPACKET_COMMON_PLAYER_TRADE, sub-type 0x01
    void SendCommonPlayerTradeInitiateTrade(unsigned int ClientId, unsigned int ShipId) override {
        const unsigned int Args[2] = { ClientId, ShipId };
        SendTradePacket(0x01, Args, 2);
    }

    // +0x198  0x06d8b980  ch1 op 0x12  FLPACKET_COMMON_PLAYER_TRADE, sub-type 0x02
    void SendCommonPlayerTradeTerminateTrade(unsigned int ClientId, int Reason) override {
        const unsigned int Args[2] = { ClientId, static_cast<unsigned int>(Reason) };
        SendTradePacket(0x02, Args, 2);
    }

    // +0x19c  0x06d8b930  ch1 op 0x12  FLPACKET_COMMON_PLAYER_TRADE, sub-type 0x40
    // The codec keeps the slot's low byte (06d8b963 MOV DL,byte ptr [ESP+0x14]).
    void SendCommonPlayerTradeAcceptTrade(unsigned int ClientId, unsigned int ShipId, bool bAccepted) override {
        const unsigned int Args[3] = { ClientId, ShipId, bAccepted ? 1u : 0u };
        SendTradePacket(0x40, Args, 3);
    }

    // +0x1a0  0x06d8b9d0  ch1 op 0x12  FLPACKET_COMMON_PLAYER_TRADE, sub-type 0x04
    void SendCommonPlayerTradeSetTradeMoney(unsigned int ClientId, unsigned int ShipId,
                                            unsigned long Money) override {
        const unsigned int Args[3] = { ClientId, ShipId, static_cast<unsigned int>(Money) };
        SendTradePacket(0x04, Args, 3);
    }

    // +0x1a4  0x06d8ba20  ch1 op 0x12  FLPACKET_COMMON_PLAYER_TRADE, sub-type 0x08
    void SendCommonPlayerTradeAddTradeEquip(unsigned int ClientId, unsigned int ShipId,
                                            const EquipDesc& Item) override {
        const unsigned int Args[3] = { ClientId, ShipId, RpcArgAddress(Item) };
        SendTradePacket(0x08, Args, 3);
    }

    // +0x1a8  0x06d8baa0  ch1 op 0x12  FLPACKET_COMMON_PLAYER_TRADE, sub-type 0x10
    void SendCommonPlayerTradeDelTradeEquip(unsigned int ClientId, unsigned int ShipId,
                                            const EquipDesc& Item) override {
        const unsigned int Args[3] = { ClientId, ShipId, RpcArgAddress(Item) };
        SendTradePacket(0x10, Args, 3);
    }

    // +0x1ac  0x06d8bb10  ch1 op 0x12  FLPACKET_COMMON_PLAYER_TRADE, sub-type 0x20
    void SendCommonPlayerTradeRequestTrade(unsigned int ClientId, unsigned int ShipId) override {
        const unsigned int Args[2] = { ClientId, ShipId };
        SendTradePacket(0x20, Args, 2);
    }

    // +0x1b0  0x06d8bb60  ch1 op 0x12  FLPACKET_COMMON_PLAYER_TRADE, sub-type 0x80
    void SendCommonPlayerTradeStopTradeRequest(unsigned int ClientId, unsigned int ShipId) override {
        const unsigned int Args[2] = { ClientId, ShipId };
        SendTradePacket(0x80, Args, 2);
    }

    // +0x1b4  0x06d8a430  ch2 op 0x39  FLPACKET_SERVER_SCANNOTIFY
    void SendServerScanNotify(unsigned int ClientId, unsigned int ScannerId, unsigned int GoodId) override {
        const unsigned int Args[3] = { ClientId, ScannerId, GoodId };
        SendPacket(2, 0x39, Args, 3);
    }

    // +0x1b8  0x06d8c500  ch2 op 0x52  FLPACKET_SERVER_PLAYERLIST, kind 1
    // The codec keeps the last argument's low byte (06d8c51d).
    void SendServerPlayerListAdd(unsigned int ClientId, const wchar_t* Name, unsigned int PlayerId,
                                 bool bAlreadyPresent) override {
        const unsigned int Args[4] = { kPlayerListKindEntry, RpcArgPointer(Name), PlayerId,
                                       bAlreadyPresent ? 1u : 0u };
        SendPacketEx(2, 0x52, ClientId, Args, 4);
    }

    // +0x1bc  0x06d8c560  ch2 op 0x52  FLPACKET_SERVER_PLAYERLIST, kind 2
    void SendServerPlayerListRemove(unsigned int ClientId, unsigned int PlayerId) override {
        const unsigned int Args[2] = { kPlayerListKindDrop, PlayerId };
        SendPacketEx(2, 0x52, ClientId, Args, 2);
    }

    // +0x1c0  0x06d8c5b0  ch2 op 0x52  FLPACKET_SERVER_PLAYERLIST, kind 0
    void SendServerPlayerListClear(unsigned int ClientId) override {
        const unsigned int Args[1] = { kPlayerListKindClear };
        SendPacketEx(2, 0x52, ClientId, Args, 1);
    }

    // +0x1c4  0x06d8c1f0  ch2 op 0x54  FLPACKET_SERVER_MISCOBJUPDATE, mask 0x0009
    void SendServerMiscObjUpdateObjectGroup(unsigned int ClientId, unsigned int ObjectId,
                                            unsigned int GroupId) override {
        const unsigned int Args[3] = { kMiscObjUpdateVariant_ObjectGroup, ObjectId, GroupId };
        SendPacketEx(2, 0x54, ClientId, Args, 3);
    }

    // +0x1c8  0x06d8c290  ch2 op 0x54  FLPACKET_SERVER_MISCOBJUPDATE, mask 0x0002
    void SendServerMiscObjUpdateMission(unsigned int ClientId, unsigned int MissionId,
                                        unsigned int MissionSetBy) override {
        const unsigned int Args[3] = { kMiscObjUpdateVariant_Mission, MissionId, MissionSetBy };
        SendPacketEx(2, 0x54, ClientId, Args, 3);
    }

    // +0x1cc  0x06d8c430  ch2 op 0x54  FLPACKET_SERVER_MISCOBJUPDATE, mask 0x0010
    void SendServerMiscObjUpdateMotd(unsigned int ClientId, const std::wstring& Motd) override {
        const unsigned int Args[2] = { kMiscObjUpdateVariant_Motd, RpcArgAddress(Motd) };
        SendPacketEx(2, 0x54, ClientId, Args, 2);
    }

    // +0x1d0  0x06d8c240  ch2 op 0x54  FLPACKET_SERVER_MISCOBJUPDATE, mask 0x0028
    void SendServerMiscObjUpdateAffiliation(unsigned int ClientId, unsigned int ObjectId,
                                            unsigned int Affiliation) override {
        const unsigned int Args[3] = { kMiscObjUpdateVariant_Affiliation, ObjectId, Affiliation };
        SendPacketEx(2, 0x54, ClientId, Args, 3);
    }

    // +0x1d4  0x06d8c2e0  ch2 op 0x54  FLPACKET_SERVER_MISCOBJUPDATE, mask 0x0044
    void SendServerMiscObjUpdateRank(unsigned int ClientId, unsigned int PlayerId, int Rank) override {
        const unsigned int Args[3] = { kMiscObjUpdateVariant_Rank, PlayerId,
                                       static_cast<unsigned int>(Rank) };
        SendPacketEx(2, 0x54, ClientId, Args, 3);
    }

    // +0x1d8  0x06d8c390  ch2 op 0x54  FLPACKET_SERVER_MISCOBJUPDATE, mask 0x0005
    void SendServerMiscObjUpdatePlayerGroup(unsigned int ClientId, unsigned int PlayerId,
                                            unsigned int GroupId) override {
        const unsigned int Args[3] = { kMiscObjUpdateVariant_PlayerGroup, PlayerId, GroupId };
        SendPacketEx(2, 0x54, ClientId, Args, 3);
    }

    // +0x1dc  0x06d8c3e0  ch2 op 0x54  FLPACKET_SERVER_MISCOBJUPDATE, mask 0x0084
    void SendServerMiscObjUpdateSystem(unsigned int ClientId, unsigned int PlayerId,
                                       unsigned int SystemId) override {
        const unsigned int Args[3] = { kMiscObjUpdateVariant_System, PlayerId, SystemId };
        SendPacketEx(2, 0x54, ClientId, Args, 3);
    }

    // +0x1e0  0x06d8c330  ch2 op 0x54  FLPACKET_SERVER_MISCOBJUPDATE, mask 0x0100
    void SendServerMiscObjUpdateFormation(unsigned int ClientId,
                                          const std::vector<unsigned int>& MemberIds) override {
        const unsigned int Args[2] = { kMiscObjUpdateVariant_Formation, RpcArgAddress(MemberIds) };
        SendPacketEx(2, 0x54, ClientId, Args, 2);
    }

    // +0x1e4  0x06d8c600  ch2 op 0x53  FLPACKET_SERVER_FORMATION_UPDATE
    void SendServerFormationUpdate(unsigned int ClientId, unsigned int LeaderId, const Vector& Offset) override {
        const unsigned int Args[3] = { ClientId, LeaderId, RpcArgAddress(Offset) };
        SendPacket(2, 0x53, Args, 3);
    }

    // +0x1e8  0x06d8c660  ch2 op 0x57  FLPACKET_SERVER_SET_MUSIC
    // The codec keeps bFlag's low byte (06d8c6ad) and Fade's bits (06d8c67d FLD).
    void SendServerSetMusic(unsigned int ClientId, unsigned int SpaceMusic, unsigned int DangerMusic,
                            unsigned int BattleMusic, unsigned int OverrideMusic, bool bFlag,
                            float Fade) override {
        const unsigned int Args[7] = { ClientId, SpaceMusic, DangerMusic, BattleMusic, OverrideMusic,
                                       bFlag ? 1u : 0u, RpcArgFloatBits(Fade) };
        SendPacket(2, 0x57, Args, 7);
    }

    // +0x1ec  0x06d8c6e0  ch2 op 0x58  FLPACKET_SERVER_CANCEL_MUSIC
    void SendServerCancelMusic(unsigned int ClientId) override {
        const unsigned int Args[1] = { ClientId };
        SendPacket(2, 0x58, Args, 1);
    }

    // +0x1f0  0x06d8c710  ch2 op 0x59  FLPACKET_SERVER_PLAY_SOUND_EFFECT
    void SendServerPlaySoundEffect(unsigned int ClientId, unsigned int SoundId) override {
        const unsigned int Args[2] = { ClientId, SoundId };
        SendPacket(2, 0x59, Args, 2);
    }

    // +0x1f4  0x06d8f480 -- the same function as IServerImpl::DumpPacketStats, i.e.
    // void DumpPacketStats(char const *). Retail: FUN_06d961b0(FUN_06d95e50(), Name) --
    // the singleton asked afresh, not this+0x08.
    void DumpPacketStats(const char* Name) override {
        RpcGetPacketSerializer()->DumpStats(Name);
    }

    // -----------------------------------------------------------------------
    // IChatClient -- the second base, one slot. Retail's vtable at .rdata:0x06db3970
    // holds a single entry, 0x06d8c470 (RET 0x10, i.e. four arguments), and it sends
    // ch1 op 0x05 FLPACKET_COMMON_CHATMSG. TargetId and SenderId go to the payload whole
    // (06d8c4d4..06d8c4e3); PushToClient gets TargetId's low word -- the client id inside
    // the CHAT_ID (06d8c4ed MOVZX EAX,AX). rpclocal's PushToClient never reads it.
    // -----------------------------------------------------------------------
    void SendCommonChatMsg(CHAT_ID TargetId, CHAT_ID SenderId, unsigned long Size,
                           const void* Data) override {
        const unsigned int Args[4] = { TargetId.Id, SenderId.Id, static_cast<unsigned int>(Size),
                                       RpcArgPointer(Data) };
        SendPacketEx(1, 0x05, static_cast<unsigned int>(TargetId.GetPlayerId()), Args, 4);
    }

private:
    // +0x08 -- the packet-type registry: zeroed by the constructor (06d8ee90), filled by
    // Startup (06d8ef48), read untested by every sender (06d883a9 MOV ECX,[ESI+0x8]) and
    // tested only by the pump (06d8efeb). Retail never releases it on this side; the one
    // release is IServerImpl::Shutdown's, after which this pointer dangles.
    FPacketSerializer* Serializer;
};

// The singleton retail keeps at .data:Client (0x06dbbcdc) and exports as the undecorated
// data name `Client` (ordinal 108); GetClientInterface (0x06d8eeb0) returns it and
// GetChatClientInterface returns Client + 4, i.e. the IChatClient sub-object of this very
// instance. extern "C" because the export name is the bare C name -- which is also why it
// cannot carry the G prefix. Its class is private to this file, so the declaration lives
// here rather than in src/include/rpc_iserver.h.
extern "C" RPCLOCAL_API FLocalClientProxy Client;
FLocalClientProxy Client;

IClient* RpcGetClientProxy() {
    return static_cast<IClient*>(&Client);
}

IChatClient* RpcGetChatClientProxy() {
    return static_cast<IChatClient*>(&Client);
}

// retail IClient::Update (slot 16, 0x06d8efc0), called from IServerImpl::Update.
void RpcClientPump() {
    Client.Update();
}
