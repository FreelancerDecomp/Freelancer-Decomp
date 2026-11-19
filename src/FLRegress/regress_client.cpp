// Freelancer Decomp: FLRegress, the headless regression harness (port-only, not part of retail).

#include "regress_client.h"

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
