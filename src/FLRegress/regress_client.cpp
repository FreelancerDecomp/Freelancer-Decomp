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

// The scripted player's account holds only the character it created, so the list's first
// entry is the one to select.
void FRegressClient::OnSendServerCharacterInfo(unsigned int ClientId, const CCharacterSet& Characters)
{
    (void)ClientId;
    Player.CharacterCount = 0;
    Player.bHaveCharacter = false;
    for (const MetaNode<CCharacterSet::CharacterInfo>* Node = Characters.Characters.get_head(); Node != nullptr;
         Node = Node->Next)
    {
        const CCharacterSet::CharacterInfo* Info = Node->Data;
        if (Info == nullptr)
        {
            continue;
        }
        ++Player.CharacterCount;
        if (!Player.bHaveCharacter)
        {
            Player.bHaveCharacter = true;
            Player.CharacterId = Info->Id;
            Player.CharacterSystemId = Info->SystemId;
            Player.CharacterBaseId = Info->BaseId;
        }
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
