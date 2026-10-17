// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Export header-defined members in DALib and in each module retail exports its own copies from
// (freelancer.exe, RemoteClient.dll, remoteserver.dll); keep static accessors out of line.
#pragma once

// Only DALIB_API (and CriticalWarningFn for the DALib bodies) is needed here; DALib.h would
// drag the DirectX 8 render headers into FLServer.exe's MFC code (see dalib_core.h).
#include "dalib_core.h"

#include <windows.h>

// DirectPlay8 SDK (dx8\include, appended to the END of $(IncludePath) by each .vcxproj that
// includes this header -- DALib and freelancer; see src/include/render/d3d8_min.h for why the
// old SDK must not precede the Windows 10 SDK).
#include <dplay8.h>

#include <list>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// Retail stamps module-local copies of the header-defined members (implicit copy ctor/operator=,
// inline virtuals, vftable) into each module that subclasses these classes, and exports them
// (dumpbin -exports EXE\*): freelancer.exe IDPMsgHandler, CDPMsgList, CDPClient, CDPServer,
// CGunWrapper; RemoteClient.dll IDPMsgHandler, CDPMsgList, CDPClientProxy, CDPServer;
// remoteserver.dll IDPMsgHandler, CDPMsgList, CDPClient. Out-of-line members are still DALib's,
// reached through DALib.lib's import thunks -- so a module that exports one of these classes must
// not touch its static data members directly (an import library has no thunk for data).
#if defined(DALIB_EXPORTS) || defined(FREELANCER_EXPORTS)
#define DALIB_NET_API __declspec(dllexport)
#else
#define DALIB_NET_API __declspec(dllimport)
#endif

#if defined(DALIB_EXPORTS) || defined(FREELANCER_EXPORTS) || defined(REMOTECLIENT_EXPORTS) || \
    defined(REMOTESERVER_EXPORTS)
#define DALIB_NET_BASE_API __declspec(dllexport)
#else
#define DALIB_NET_BASE_API __declspec(dllimport)
#endif

#if defined(DALIB_EXPORTS) || defined(FREELANCER_EXPORTS) || defined(REMOTESERVER_EXPORTS)
#define DALIB_NET_CLIENT_API __declspec(dllexport)
#else
#define DALIB_NET_CLIENT_API __declspec(dllimport)
#endif

#if defined(DALIB_EXPORTS) || defined(FREELANCER_EXPORTS) || defined(REMOTECLIENT_EXPORTS)
#define DALIB_NET_SERVER_API __declspec(dllexport)
#else
#define DALIB_NET_SERVER_API __declspec(dllimport)
#endif

#if defined(DALIB_EXPORTS) || defined(REMOTECLIENT_EXPORTS)
#define DALIB_NET_PROXY_API __declspec(dllexport)
#else
#define DALIB_NET_PROXY_API __declspec(dllimport)
#endif

// C4251: the std:: members of the exported classes below need no dll-interface -- every module
// that touches them compiles them from this header with the same STL.
// C4275: CGunWrapper's base IGunQueueReader is not exported, as in retail (no export names it).
#pragma warning(push)
#pragma warning(disable : 4251 4275)

class CDPMessage;
class CDPClientProxy;
class CDPServer;

// Four callback slots; this base has no virtual destructor.
class DALIB_NET_BASE_API IDPMsgHandler
{
public:
    // User-declared so that no implicit move members are generated and exported:
    // VC6 had none, and retail exports exactly the default ctor, copy ctor and this.
    IDPMsgHandler& operator=(IDPMsgHandler const&) = default;

    virtual void OnReceive(unsigned char* Data, unsigned long Size)
    {
        (void)Data;
        (void)Size;
    }
    virtual void OnConnect(long Result)
    {
        (void)Result;
    }
    virtual void OnDisconnect()
    {
    }
    virtual void OnAddHost(_DPNMSG_ENUM_HOSTS_RESPONSE* Response)
    {
        (void)Response;
    }
};

// Queue payload; copying preserves borrowed/owned pointers and event-specific words.
class DALIB_API CDPMessage
{
public:
    CDPMessage() = default;
    virtual ~CDPMessage() = default;

    // .text:065c3240: MOV EAX,[ECX+0x8]; RET.
    unsigned char* const GetData()
    {
        return Data;
    }
    // .text:065c3250: MOV EAX,[ECX+0xc]; RET (ICF-shared with CGunWrapper::GetConnection).
    const unsigned long GetSize()
    {
        return Size;
    }

    unsigned long Type = 0;         // +0x04 -- 0 receive, 1 connect complete, 3 terminated, 4 enum response
    unsigned char* Data = nullptr;  // +0x08
    unsigned long Size = 0;         // +0x0c
    DPNHANDLE ReceiveBufferHandle = 0; // formerly Field10; returned to DirectPlay after dispatch
    unsigned long SenderId = 0;     // +0x14 -- DPNID of the sender; ExtractMsgs filters on this
    void* Owner = nullptr;          // +0x18 -- owning CDPClient / CDPClientProxy
    HRESULT ConnectResult = 0;      // formerly Field1C; connect-complete result
};

static_assert(sizeof(DPNHANDLE) == 4 && sizeof(HRESULT) == 4, "DirectPlay message words");
static_assert(sizeof(CDPMessage) == 0x20, "CDPMessage footprint");
static_assert(offsetof(CDPMessage, ReceiveBufferHandle) == 0x10, "receive-buffer handle");
static_assert(offsetof(CDPMessage, ConnectResult) == 0x1c, "connect result");

// Synchronized message queue with a manual-reset nonempty event.
class DALIB_NET_BASE_API CDPMsgList
{
public:
    CDPMsgList();
    virtual ~CDPMsgList();

    void Lock();
    void Unlock();
    void Add(CDPMessage* Message);
    CDPMessage* GetNextMsg();
    CDPMessage* PeekNextMsg();
    // Moves every queued message whose SenderId == Id from this list into Dest.
    void ExtractMsgs(unsigned long Id, CDPMsgList& Dest);
    // Moves every message from Other into this list, then clears Other and resets its event.
    void CopyList(CDPMsgList& Other);
    void WaitForMsg(unsigned long TimeoutMs);
    void SetEmptyEvent();

private:
    std::list<CDPMessage*> Messages;
    CRITICAL_SECTION Cs;
    HANDLE EmptyEvent = nullptr;
};

// Callback base plus virtual destructor; game subclasses provide event handlers.
class DALIB_NET_CLIENT_API CDPClient : public IDPMsgHandler
{
public:
    CDPClient();
    virtual ~CDPClient();

    IDirectPlay8Client* GetClient();

    void Cleanup();
    bool AddConnectAttempt(const wchar_t* Host, wchar_t* Port);
    bool ProcessConnectAttempt();
    void Close();
    void CancelEnums();
    bool Send(unsigned char* Data, unsigned long Size);
    void SetGUID(GUID& ApplicationGuid);
    bool EnumerateHosts();
    // Argument order from .text:065c2b49-2b61 (EnumHosts dwEnumCount/dwRetryInterval/dwTimeOut =
    // params 3/4/5); freelancer.exe .text:005ac69f passes (host, port, 2, 0, 0).
    bool EnumerateHost(const wchar_t* HostAddress, unsigned long Port, unsigned long EnumCount,
                       unsigned long RetryInterval, unsigned long TimeOut);

    // .text:065c2eb0: MOV EAX,[ECX+0x7c]; RET. Inline: retail freelancer.exe exports its own
    // copy (.text:0041c1b0).
    long GetConnectResult()
    {
        return ConnectResult;
    }

    // Process queued messages. Must be called on the game thread; HandleClientMsg (DirectPlay8
    // IO thread callback) only queues into IncomingMsgs.
    void DispatchMsgs(unsigned long TimeoutMs);

    static unsigned long GetLastMsgTimestamp();
    static unsigned long GetPingDelay();
    static unsigned long GetSendQueueSize();
    static bool IsPingOutstanding();
    static void SetSourcePort(unsigned long Port);
    static unsigned long GetLinkQuality();

protected:
    bool ConnectToServer(const wchar_t* Host, const wchar_t* Port);

    static long __stdcall HandleClientMsg(void* UserContext, unsigned long MessageId, void* MsgData);

    static unsigned long m_dwLastMsgReceivedTime; // .data:065ca120
    static unsigned long m_dwPingSendTime;        // .data:065ca1ac
    static unsigned long m_dwPingReceiveTime;     // .data:065ca1b0
    static unsigned long m_dwPingSendCount;       // .data:065ca1b4
    static unsigned long m_dwPingReceiveCount;    // .data:065ca1b8
    static unsigned long m_dwPingLastTime;        // .data:065ca1bc
    static unsigned long m_dwRoundTripTime;       // .data:065ca1c0
    static unsigned long m_dwSourcePort;          // .data:065ca1c4
    static unsigned long m_dwLinkQuality;         // .data:065ca020

private:
    IDirectPlay8Client* Client = nullptr;    // +0x04
    bool bConnected = false;                 // +0x08
    unsigned long Reserved0xC = 0;           // +0x0c -- set by ctor, never read within DALib.dll

    CDPMsgList IncomingMsgs;                 // +0x10 -- fed by HandleClientMsg (IO thread)
    CDPMsgList WorkingMsgs;                  // +0x3c -- DispatchMsgs's own working copy

    GUID ApplicationGuid{};                  // +0x68..+0x74

protected:
    // Protected async handle is read by client subclasses; keep declaration order.
    DPNHANDLE PendingConnectHandle = 0;

private:
    long ConnectResult = 0;                  // +0x7c

    std::list<std::pair<std::wstring, std::wstring>> PendingConnectAttempts; // +0x80
    bool bCoInitialized = false;             // +0x8c
};

// Server-side client proxy; game subclasses provide event handlers.
class DALIB_NET_PROXY_API CDPClientProxy : public IDPMsgHandler
{
public:
    CDPClientProxy();
    virtual ~CDPClientProxy();

    bool SendMsgs();
    bool Send(void* Data, unsigned long Size);
    bool Disconnect();
    bool GetConnectionStats(_DPN_CONNECTION_INFO* Info);
    double GetLinkSaturation();
    void UpdateQueueHistory();
    unsigned int GetSendQSize();
    unsigned int GetSendQBytes();
    // DPN_MSGID_SEND_COMPLETE hook (CDPServer::HandleDPServerMsg .text:065c48e9); retail body
    // is the empty `RET 4` at .text:065c3af0.
    void OnMsgSent(unsigned long SendTime);

    CDPServer* Owner = nullptr;       // +0x04
    unsigned long PlayerId = 0;       // +0x08
    bool bConnected = true;           // +0x60

private:
    friend class CDPServer;

    DWORD LastActivityTime = 0;       // +0x0c
    CDPMsgList SendQueue;             // +0x10
    int QueuedBytes = 0;              // +0x3c
    int QueuedCount = 0;              // +0x40
    unsigned long QueueHistory[3] = {1000, 1000, 1000}; // +0x44,+0x48,+0x4c
    DWORD LastHistoryUpdateTime = 0;  // +0x50
    double LinkSaturation = 0.0;      // +0x58
};

// Server owns a proxy-creation hook and has no IDPMsgHandler base.
class DALIB_NET_SERVER_API CDPServer
{
public:
    CDPServer();
    virtual ~CDPServer();
    // Factory hook: a derived server supplies its own CDPClientProxy subclass.
    virtual CDPClientProxy* CreateClientProxy();

    void SetGUID(GUID& Guid);
    void SetSessionName(const wchar_t* Name);
    void SetMaxPlayers(int MaxPlayers);
    void SetPassword(const wchar_t* Password);
    bool SetEnumResponse(void* Data, unsigned long Size);

    bool BeginHosting(unsigned int Port);
    void StopHosting();

    bool SendTo(CDPClientProxy* Proxy, void* Data, unsigned long Size);
    void DisconnectClient(unsigned long PlayerId);
    bool GetConnectionStats(CDPClientProxy* Proxy, _DPN_CONNECTION_INFO* Info);
    bool GetConnectionStats(_DPN_CONNECTION_INFO* Info);
    unsigned int GetSendQSize(CDPClientProxy* Proxy);
    unsigned int GetSendQBytes(CDPClientProxy* Proxy);
    void GetPort(std::vector<unsigned long>& Ports);

    void DispatchMsgs();

    static unsigned long GetLastMsgTimestamp();
    static void CrashCleanup();

protected:
    static long __stdcall HandleDPServerMsg(void* UserContext, unsigned long MessageId, void* MsgData);

    void GetHostAddresses();
    void ReleaseHostAddresses();
    void UpdateDescription();

    static unsigned long m_dwLastMsgReceivedTime; // .data:065ca254
    static CDPServer* m_pServer;                  // .data:065ca284

private:
    IDirectPlay8Server* Server = nullptr; // +0x04
    DPN_APPLICATION_DESC AppDesc{};       // +0x08 (pwszSessionName / pwszPassword point below)
    wchar_t SessionName[260]{};           // +0x50
    wchar_t Password[260]{};              // +0x258

    IDirectPlay8Address* HostAddresses[64]{}; // +0x460
    DWORD NumHostAddresses = 0;               // +0x560

    bool bHosting = false;         // +0x564
    unsigned long OwnPlayerId = 0; // +0x568

    CDPMsgList IncomingMsgs;       // +0x56c

    unsigned char EnumResponseData[0x400]{}; // +0x598
    unsigned long EnumResponseSize = 0;      // +0x998
};

// GUN COM slot purposes not yet derived retain their offset names and original arity.
namespace Gun2
{
    // Only recovered consumer fields are named; other payload bytes remain opaque.

    // One server of a list-server page: the reader copies the four wide strings at +0x08..+0x18
    // into an FServerInfo (freelancer.exe login_mgr.cpp).
    struct GUNServerEntry
    {
        unsigned char Field_0x00[8];    // not read by freelancer.exe
        wchar_t const* Name;            // +0x08 -> FServerInfo +0x44 (0x005ab76e)
        wchar_t const* Description;     // +0x0c -> FServerInfo +0x54 (0x005ab7bd)
        wchar_t const* Version;         // +0x10 _wtoi -> FServerInfo +0x8c (0x005ab808)
        wchar_t const* Record;          // +0x14 "ports:max:pw:+0x96:+0x95:+0x64:id" (0x005ab899)
        wchar_t const* Address;         // +0x18 -> FServerInfo +0x00 (0x005ab819)
    };

    // The message body at GUNQueueMessage +0x10.
    struct GUNMessageBody
    {
        unsigned char Field_0x00[4];    // not read by freelancer.exe
        unsigned char bHasEntries;      // +0x04 -- zero: no entries read, the page count reset
        unsigned char Pad_0x05[3];
        unsigned long Type;             // +0x08 -- the reader takes 0x7204 only
        unsigned char Field_0x0c[0x18]; // not read by freelancer.exe
        unsigned long EntryCount;       // +0x24
        GUNServerEntry** Entries;       // +0x28
    };

    // Passed by pointer to IGunQueueReader::Read (CGunWrapper::Read, CLoginMgr::Read).
    struct GUNQueueMessage
    {
        unsigned char Field_0x00[0x10]; // not read by freelancer.exe or DALib.dll
        GUNMessageBody* Body;           // +0x10 (0x005ab5a9)
    };
}

// Queue-reader callback passed to the GUN connection dispatcher.
struct IGunQueueReader
{
    virtual long __stdcall Read(Gun2::GUNQueueMessage* Message) = 0;
};

struct IGunConnection : public IUnknown
{
    // Only the values CGunWrapper::Update tests are known: 2 (query host/browser), 3 and 4
    // (reconnect window; 4 also arms a 10 s retry). Enumerator names are not recovered.
    enum ConnectStatus
    {
    };

    // +0x0c CGunWrapper ctor .text:065c58f5: (GUID&, cd-key, 0, L"CD-KEY"); non-zero aborts.
    virtual long __stdcall InitializeConnection(GUID* Guid, wchar_t* CdKey, unsigned long Zero,
                                  const wchar_t* KeyName) = 0; // formerly Slot03
    // +0x10 ctor .text:065c5907 / Update .text:065c5bb6: (server name, port).
    virtual long __stdcall Connect(const char* ServerName, unsigned short Port) = 0; // formerly Slot04
    // +0x14 called right before the final Release in the teardown (.text:065c59dd / 5aed).
    virtual long __stdcall Disconnect() = 0; // formerly Slot05
    // +0x18 Update .text:065c5b70 / 5bc4: writes the current ConnectStatus.
    virtual long __stdcall GetConnectionStatus(ConnectStatus* Status) = 0; // formerly Slot06
    // +0x1c: stored connection failure HRESULT, or zero outside the error state.
    virtual long __stdcall GetConnectionError() = 0; // formerly Slot07
    // +0x20 DispatchQueue .text:065c5c4b: (IGunQueueReader*).
    virtual long __stdcall DispatchQueue(IGunQueueReader* Reader) = 0; // formerly Slot08
};

// QueryInterface'd from the connection (IIDs {EEEEEE26-...} / {EEEEEE27-...}); DALib only ever
// Releases them.
// Provider: EXE\gundll.dll, vtable .rdata:100492e8 (14 slots; arities from each body's RET n).
// Names are not recovered; slots stay SlotNN.
struct IGunHost : public IUnknown
{
    // +0x0c FUN_10004790, RET 8: the session (server) name, wide. FLServer UpdateGunReport
    // .text:0040c77d passes ServerName.c_str(); non-zero aborts the report.
    virtual long __stdcall Slot03(const wchar_t* Name) = 0;
    // +0x10 LAB_10004b40, RET 4.
    virtual long __stdcall Slot04() = 0;
    // +0x14 FUN_10004eb0, RET 0xc. TODO: argument meaning.
    virtual long __stdcall Slot05(unsigned long Arg0, unsigned long Arg1) = 0;
    // +0x18 LAB_10005160, RET 8. TODO: argument meaning.
    virtual long __stdcall Slot06(unsigned long Arg0) = 0;
    // +0x1c LAB_100052b0, RET 8: stores (count - 1) through the pointer.
    virtual long __stdcall Slot07(unsigned long* OutCount) = 0;
    // +0x20 LAB_100052f0, RET 8. TODO: argument meaning.
    virtual long __stdcall Slot08(unsigned long Arg0) = 0;
    // +0x24 LAB_100053a0, RET 0x10: (selector, narrow key, wide value); 0x80070057 when the
    // key is null/empty or the value null. FLServer .text:0040c881 ("GameOpts") /
    // 0040c89f ("GameDesc") with selector 0.
    virtual long __stdcall Slot09(unsigned long Selector, const char* Key, const wchar_t* Value) = 0;
    // +0x28..+0x34 (FUN_10005500 RET 0x14, LAB_10005610 / FUN_10005750 / FUN_10005830 RET 0xc)
    // are not called by any ported module and are not declared.
};

struct IGunBrowser : public IUnknown
{
    // DWORD argument names describe packet storage; Text is copied as UTF-16.
    virtual long __stdcall SendRequest7102(unsigned long Field1C) = 0; // formerly Slot03
    virtual long __stdcall SendRequest7103(unsigned long Field20,
                                           unsigned long Field1C) = 0; // formerly Slot04
    virtual long __stdcall SendRequest7104(unsigned long A, unsigned long B, unsigned long C,
                                           const wchar_t* Text, unsigned long E) = 0; // formerly Slot05
};

// GUN queue reader and COM-reference lifetime owner.
class DALIB_NET_API CGunWrapper : public IGunQueueReader
{
public:
    CGunWrapper(const char* DllPath, GUID& Guid, const char* ServerName, unsigned long Port,
                wchar_t* CdKey);
    virtual long __stdcall Read(Gun2::GUNQueueMessage* Message) override;
    virtual ~CGunWrapper();

    IGunConnection::ConnectStatus GetStatus();
    void Update();
    static void Shutdown();

    // .text:065c3250 (ICF-shared with CDPMessage::GetSize): MOV EAX,[ECX+0xc]; RET. Inline:
    // retail freelancer.exe exports its own (.text:004148a0).
    IGunConnection* GetConnection()
    {
        return Connection;
    }
    // .text:065c5c20 / exe .text:0041be70.
    IGunHost* GetHost()
    {
        return Host;
    }
    // .text:065c5c30 / exe .text:00415070.
    IGunBrowser* GetBrowser()
    {
        return Browser;
    }
    // .text:065c5c40 / exe .text:0041be80: hand this object to the connection as the reader.
    void DispatchQueue()
    {
        if (Connection != nullptr)
        {
            Connection->DispatchQueue(this);
        }
    }

private:
    HMODULE Module;                         // +0x04 -- LoadLibraryA(DllPath)
    unsigned char Field_0x08;               // +0x08 -- zeroed by ctor and dtor, never read. TODO: meaning
    IGunConnection* Connection;             // +0x0c
    IGunHost* Host;                         // +0x10
    IGunBrowser* Browser;                   // +0x14
    IGunConnection::ConnectStatus Status;   // +0x18
    char* ServerName;                       // +0x1c -- _strdup(ServerName)
    unsigned long Port;                     // +0x20
    unsigned long RetryTime;                // +0x24 -- timeGetTime() deadline for the next reconnect
};

#pragma warning(pop)
