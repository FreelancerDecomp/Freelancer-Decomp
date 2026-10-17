// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <vector>

#ifdef REMOTECLIENT_EXPORTS
#define REMOTECLIENT_API __declspec(dllexport)
#define REMOTECLIENT_GETTER_API __declspec(dllexport)
#else
#define REMOTECLIENT_API __declspec(dllimport)
#define REMOTECLIENT_GETTER_API
#endif

class IClient;
class IChatClient;

// Only the final five words are written by RemoteClient.dll (06b39b10).
// The caller owns the preceding part of each statistics record.
struct FClientStats
{
    unsigned char Field_0x00[0x64];
    unsigned long BytesSentGuaranteed;
    unsigned long BytesReceivedGuaranteed;
    unsigned long PacketsSentGuaranteed;
    unsigned long PacketsReceivedGuaranteed;
    unsigned long ThroughputBps;
};
typedef FClientStats client_stats_t;

// ErrorCode is the public name in retail's exported logging callback signature.
struct ErrorCode
{
    unsigned int Value;
};
typedef int (__cdecl* FRemoteClientLogFunction)(ErrorCode Code, const char* Format, ...);

extern "C"
{
    // Several RPC modules export their own getters. Callers select the owner DLL.
    REMOTECLIENT_GETTER_API IClient* GetClientInterface();
    REMOTECLIENT_GETTER_API IChatClient* GetChatClientInterface();
}

REMOTECLIENT_API void SetRemoteClientResponseData(bool bAllowPvp, bool bAllowNewPlayers,
    int ServerVersion, const wchar_t* Description, unsigned int ServerId,
    const char* ServerSignature);
REMOTECLIENT_API int GetNumClients();
REMOTECLIENT_API int GetClientStats(FClientStats* Stats, int* Count);
REMOTECLIENT_API void SetRemoteClientSessionName(const wchar_t* Name);
REMOTECLIENT_API void SetRemoteClientPassword(const wchar_t* Password);
REMOTECLIENT_API void GetRemoteClientPort(std::vector<unsigned long>& Ports);
REMOTECLIENT_API void ForceClientLogout(unsigned int ClientId);
REMOTECLIENT_API void SetServerLogFunction(FRemoteClientLogFunction Function);
