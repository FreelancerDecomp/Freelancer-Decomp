// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Declarations shared by the files split out of server_classes.cpp (server_classes.cpp,
// server_playerdb.cpp, server_playergroup.cpp, server_account.cpp, server_asteroid.cpp,
// server_starsystem.cpp, server_objectlist.cpp, server_playerview.cpp, server_space.cpp).
// Internal to Server.dll: nothing here is exported.
#pragma once

#include <map>

#include "common_server.h"
#include "rpc_iclient.h"

// Forward declaration for IClient (defined in rpc_iclient.h)
class IClient;

// The IClient / IChatClient halves of rpclocal's Client object (retail
// .data:DAT_06d8fb14 and .data:DAT_06d8fb18). Both are defined in IServerImpl.cpp;
// CPlayerGroup's senders reach for them at file scope.
extern IClient* g_pClientInterface;
extern IChatClient* g_pChatClientInterface;

// Retail .data:DAT_06d8f5dc — the group-control chat channel. Its CRT initializer
// 0x06d189c0 writes word 4 AND `MOV byte ptr [DAT_06d8f5dc+2],0x1`, so the CHAT_ID
// is 0x00010004, not 4 (the old value here dropped the channel byte). It belongs
// to FLChatServer.cpp's block of chat ids and is defined in server_chat.cpp.
extern const unsigned int GGroupChatSourceId;
// Retail .data:DAT_06d9042c — the "channel" CPlayerGroup::DeliverChat stamps on
// every copy. CORRECTED: it IS written, by the CRT initializer 0x06d5b190
// (`MOV word ptr [DAT_06d9042c],0x3` / `MOV byte ptr [DAT_06d9042c+2],0x1`),
// i.e. 0x00010003 -- the group chat channel, the same value as SubmitChat's
// DAT_06d8f5e0.
static constexpr unsigned int GDeliverChatSourceId = 0x00010003u;

// FUN_06d19180: builds a CHAT_ID out of a player/session id -- word0 = (ushort)Id,
// bytes 2 and 3 zeroed. Every IChatClient call in this file goes through it.
static unsigned int MakeChatId(unsigned int Id) { return Id & 0xffff; }

// The 8-byte block FUN_06d190d0 (0x06d190d0) builds on its own stack before
// handing it to IChatClient::SendCommonChatMsg: [0] = the message type it was called with
// (0x06d190d3), [4] = its ushort argument zero-extended to a dword
// (0x06d190d7 MOVZX / 0x06d190ec MOV). The literal 8 pushed at 0x06d190ea is
// this struct's size.
struct FGroupMsgPayload {
    unsigned int MsgType;
    unsigned int Param;
};
static_assert(sizeof(FGroupMsgPayload) == 8, "FGroupMsgPayload: the 8-byte group chat payload");

// server_playerdb.cpp: the account "name" file's constant XOR key (Server_AccountCrc of
// GAccountNameFileKeySource); CAccount::InitFromFolder uses it too.
unsigned short AccountNameFileXorKey();

namespace SrvAsteroid {

// DAT_06d8d980, defined in server_classes.cpp next to the module's other globals.
typedef std::map<unsigned int, SrvAsteroidSystem> FSrvAsteroidSystemMap;
extern FSrvAsteroidSystemMap GSrvAsteroidSystems;

} // namespace SrvAsteroid
