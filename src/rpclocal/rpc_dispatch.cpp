// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_dispatch.cpp -- retail IServerImpl::HandlePacket (0x06d81170, 3128 instructions),
// the whole server -> client half of rpclocal.
//
// Ground truth, raw listing at 0x06d81194:
//     MOV EAX,[EBP+0x0c] ; MOV EDX,EAX ; SHR EDX,0x10 ; MOVZX EDX,DX   -> Channel
//     AND EAX,0xfff                                                     -> Opcode
//     DEC EDX ; JZ 0x06d83256          -> Channel 1 switch (opcodes 1..0x1d)
//     DEC EDX ; JNZ default            -> Channel 2 switch (opcodes 1..0x5b)
// Anything else is dropped. Each case deserializes the payload that starts at
// packet+0x20 and calls the real IClient inside freelancer.exe (.data:0x06dbbcd8), or
// the chat client (.data:0x06dbbcd4) for channel 1 opcode 5. The cases themselves are
// the Deliver column of GChannel1Codecs / GChannel2Codecs (rpc_payload_ch1.cpp,
// rpc_payload_ch2.cpp).
//
// Removed 2026-09-24 (claude-code:w4-rpclocal-tail): the opcode -> slot tables
// (GChannel1Slots / GChannel2Slots / GTradeSubTypes), the arity table GClientSlotArity and
// RpcCallClientSlot, the raw-dword fallback they served. The fallback was already
// unreachable (every retail case has a codec) and none of it exists in retail.

#include "rpc_transport.h"
#include "rpc_payloads.h"
#include "rpc_iclient.h"
#include "common_server_types.h"

// ---------------------------------------------------------------------------
// retail IServerImpl::HandlePacket (0x06d81170)
// ---------------------------------------------------------------------------
bool RpcHandleIncomingPacket(FPacketBase* Packet) {
    // 06d8118d MOV EBP,[param_1] / 06d81194 MOV EAX,[EBP+0xc] -- no packet test.
    // 06d81197..06d811a0: channel = header >> 16, opcode = header & 0xfff.
    // 06d811a5 DEC EDX / JZ 06d83256 -> channel 1 table (.text:06d83cb0, ops 1..0x1d);
    // 06d811ad DEC EDX / JNZ default -> channel 2 table (.text:06d83b44, ops 1..0x5b);
    // anything else, and every table slot holding 06d83b1f, is the default branch.
    //
    // Every non-default slot of both tables has exactly one Deliver in GChannel1Codecs /
    // GChannel2Codecs, and none of the default slots has one (checked 2026-09-24 against
    // the two jump tables: ch1 defaults 3,6,7,9,a,b,c,d; ch2 defaults 9,c,12,18,1b,1c,36,
    // 3a,3b,50; 21 + 81 = 102 codecs). A missing codec therefore IS the default branch.
    // Each case reads DAT_06dbbcd8 (or DAT_06dbbcd4 for ch1 op 5) and calls through it
    // untested, so no null test on the real client here either.
    //
    // Removed 2026-09-24 (not in retail): the packet null test, the RecvCount increment,
    // the "real client not resolved" guards and log lines, and the raw-dword fallback that
    // the codec coverage above had made unreachable.
    const FRpcPayloadCodec* Codec = RpcFindPayloadCodec(Packet->GetChannel(), Packet->GetOpcode());
    if (Codec == nullptr) {
        return false;
    }
    return Codec->Deliver(Packet, GRealClient, GRealChatClient);
}
