// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// freelancer.exe's exported client facades: the IClientImpl object `Client` and its two getters.
// The IClient slot bodies live in client_iclient.cpp / client_iclient_space.cpp (see
// freelancer_client.h); retail Server / rpclocal / remoteserver reach them through these exports.

#include "freelancer_client.h"
// MD5Hash: nothing in this file calls it. Retail FreeLancer.exe compiled md5hash.h
// with the class dllexport and so exports its own copy constructor (ord 13,
// .text:00564910), operator= (ord 24, .text:00564970), GetStatePtr (ord 47,
// .text:004d9980) and ??_7 (ord 34, .rdata:005e2668); including the header in this
// exports translation unit is what emits them (MD5HASH_API = dllexport under
// FREELANCER_EXPORTS). Every other member resolves to DALib.lib's import thunks.
#include "md5hash.h"

// IClient special members — same trivial bodies Server/rpclocal define for their
// own link of this abstract base (exe does not import Server's copies).
IClient::IClient() {}
IClient::IClient(const IClient& Other) { (void)Other; }
IClient& IClient::operator=(const IClient& Other)
{
    (void)Other;
    return *this;
}

// Retail .data:Client (0x0067ea64), exported as the undecorated data name `Client`
// (ordinal 66) -- extern "C" for that reason, which is also why it cannot carry the G
// prefix. Retail remoteserver.dll imports it (IAT, name hint 0x41). The static initializer
// .text:005b0840 is IClientImpl's constructor (client_iclient.cpp).
IClientImpl Client;

// Retail .text:005b0860 (ordinal 68): `MOV EAX,Client; RET`.
extern "C" __declspec(dllexport) IClient* GetClientInterface()
{
    return static_cast<IClient*>(&Client);
}

// Retail .text:005b0870 (ordinal 67): `MOV EAX,Client+4; RET` -- the IChatClient
// sub-object of Client.
extern "C" __declspec(dllexport) IChatClient* GetChatClientInterface()
{
    return static_cast<IChatClient*>(&Client);
}
