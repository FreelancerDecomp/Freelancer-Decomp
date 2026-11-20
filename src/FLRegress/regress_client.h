// Freelancer Decomp: FLRegress, the headless regression harness (port-only, not part of retail).
//
// FRegressClient stands in for the game client. Server.dll loads the module named in
// SStartupInfo::ModulePath (FLRegress.exe itself) and takes its IClient / IChatClient from the
// exported GetClientInterface / GetChatClientInterface, as it does with freelancer.exe in
// single player. Every slot records the call, so the trace shows what the server sent. A few
// slots also keep their arguments in Player, for the scripted player of stage B.

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

    // What the server told the scripted player (session 1), from the hooked slots.
    struct FPlayerState
    {
        bool bLoginResponse = false;
        unsigned char LoginStatus = 0;
        unsigned int CharacterCount = 0;    // in the last character list
        bool bHaveCharacter = false;        // the list's first character, below
        CHARACTER_ID CharacterId;
        unsigned int CharacterSystemId = 0;
        unsigned int CharacterBaseId = 0;
        bool bCreateShipResponse = false;
        bool bCreateShipOk = false;
        unsigned int CreatedShipId = 0;
        bool bLaunched = false;
        unsigned int LaunchShipId = 0;
        unsigned int LaunchObjectId = 0;
        unsigned int LaunchKind = 0;
    };

    const std::vector<FCall>& GetTrace() const { return Trace; }
    void ClearTrace() { Trace.clear(); }
    const FPlayerState& GetPlayer() const { return Player; }

#include "regress_client_slots.inl"

    void SendCommonChatMsg(CHAT_ID TargetId, CHAT_ID SenderId, unsigned long Size, const void* Data) override
    {
        Record(1000, "SendCommonChatMsg");
    }

private:
    void Record(int Slot, const char* Name) { Trace.push_back(FCall{ Slot, Name }); }

    // Called by the generated slots in gen_regress_client.py's HOOKS (regress_client.cpp).
    void OnSendServerLoginResponse(unsigned int ClientId, const unsigned char& Status);
    void OnSendServerCharacterInfo(unsigned int ClientId, const CCharacterSet& Characters);
    void OnSendServerRequestCreateShipResp(unsigned int ClientId, bool bSuccess, unsigned int ShipId);
    void OnSendServerLaunch(unsigned int ClientId, const FServerLaunchInfo& Info);

    std::vector<FCall> Trace;
    FPlayerState Player;
};

FRegressClient& GetRegressClient();
