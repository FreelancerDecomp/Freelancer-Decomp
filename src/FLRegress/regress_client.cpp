// Freelancer Decomp: FLRegress, the headless regression harness (port-only, not part of retail).

#include "regress_client.h"

#include <cstring>

#include "rpc_iclient_args.h"       // FServerLaunchInfo
#include "server_characterset.h"    // CCharacterSet

// IClient's user-declared special members are defined once per module that links IClient
// (see rpc_iclient.h); the class carries no data, so all three are trivial.
IClient::IClient() {}
IClient::IClient(const IClient& Other) { (void)Other; }
IClient& IClient::operator=(const IClient& Other)
{
    (void)Other;
    return *this;
}

FRegressClient& GetRegressClient()
{
    static FRegressClient Client;
    return Client;
}

// Looked up by Server.dll with GetProcAddress on the module in SStartupInfo::ModulePath.
extern "C" __declspec(dllexport) IClient* GetClientInterface()
{
    return static_cast<IClient*>(&GetRegressClient());
}

extern "C" __declspec(dllexport) IChatClient* GetChatClientInterface()
{
    return static_cast<IChatClient*>(&GetRegressClient());
}

void FRegressClient::OnSendServerLoginResponse(unsigned int ClientId, const unsigned char& Status)
{
    (void)ClientId;
    Player.bLoginResponse = true;
    Player.LoginStatus = Status;
}

// The single-player new game selects the character the server made from NewPlayer.fl:
// Restart.fl, flagged bIsRestart in the list.
void FRegressClient::OnSendServerCharacterInfo(unsigned int ClientId, const CCharacterSet& Characters)
{
    (void)ClientId;
    for (const MetaNode<CCharacterSet::CharacterInfo>* Node = Characters.Characters.get_head(); Node != nullptr;
         Node = Node->Next)
    {
        const CCharacterSet::CharacterInfo* Info = Node->Data;
        if (Info == nullptr || Info->bIsRestart == 0)
        {
            continue;
        }
        Player.bHaveRestart = true;
        Player.RestartId = Info->Id;
        Player.RestartSystemId = Info->SystemId;
        Player.RestartBaseId = Info->BaseId;
        return;
    }
}

void FRegressClient::OnSendServerRequestCreateShipResp(unsigned int ClientId, bool bSuccess, unsigned int ShipId)
{
    (void)ClientId;
    Player.bCreateShipResponse = true;
    Player.bCreateShipOk = bSuccess;
    Player.CreatedShipId = ShipId;
}

void FRegressClient::OnSendServerLaunch(unsigned int ClientId, const FServerLaunchInfo& Info)
{
    (void)ClientId;
    Player.bLaunched = true;
    Player.LaunchShipId = Info.Field_0x00;
    Player.LaunchObjectId = Info.Field_0x04;
    Player.LaunchKind = Info.Field_0x08;
}
