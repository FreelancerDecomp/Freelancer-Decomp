// Freelancer Decomp: FLRegress, the headless regression harness (port-only, not part of retail).
//
// FRegressClient stands in for the game client. Server.dll loads the module named in
// SStartupInfo::ModulePath (FLRegress.exe itself) and takes its IClient / IChatClient from the
// exported GetClientInterface / GetChatClientInterface, as it does with freelancer.exe in
// single player. Every slot only records the call, so the trace shows what the server sent.

#pragma once

#include <string>
#include <vector>

#include "common_reputation.h"   // Costume (by value in SendServerSendComm)
#include "common_server_types.h" // CHAT_ID (by value in SendCommonChatMsg)
#include "rpc_iclient.h"

class FRegressClient : public IClient, public IChatClient
{
public:
    struct FCall
    {
        int Slot;
        const char* Name;
    };

    const std::vector<FCall>& GetTrace() const { return Trace; }
    void ClearTrace() { Trace.clear(); }

#include "regress_client_slots.inl"

    void SendCommonChatMsg(CHAT_ID TargetId, CHAT_ID SenderId, unsigned long Size, const void* Data) override
    {
        Record(1000, "SendCommonChatMsg");
    }

private:
    void Record(int Slot, const char* Name) { Trace.push_back(FCall{ Slot, Name }); }

    std::vector<FCall> Trace;
};

FRegressClient& GetRegressClient();
