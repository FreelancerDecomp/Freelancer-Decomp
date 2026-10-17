// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

class FPacketBase;
class IClient;

// Internal channel-2 receiver. The caller retains packet ownership and releases it.
void RpcReceiveServerChannel(unsigned int ClientId, FPacketBase* Packet, IClient* Client);
