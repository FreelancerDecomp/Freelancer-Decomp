// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "login_mgr.h"
#include "msvcrt_compat.h" // MsvcrtAtoi/MsvcrtWtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include "character_select_equip.h" // ApplyCharacterSelectEquipFields (FUN_005ab250 tail)
#include "common_ini.h"
#include "common_misc.h"   // ParseGUID
#include "common_server.h" // IServer
#include "dacom.h"         // FDUMP
#include "globals.h"       // g_client.packed_version (DAT_0067ecb8)
#include "perf_options.h"  // GetPerfOptionSlot (FUN_005b3510)
#include "player_spawn.h"  // DAT_00673344
#include "server_filter.h" // ServerFilter::Match (GetFilteredServers)
#include "reg_key_stack.h" // FRegKeyStack (login history)
#include "vc6_sort.h"      // Vc6Sort::Sort (GetFilteredServers' inlined VC6 std::sort)

#include <algorithm>
#include <malloc.h>        // _alloca (ReadRegHexString)
#include <mmsystem.h>      // timeGetTime
#include <stdio.h>
#include <time.h>          // time (SaveLogin)
#include <stdlib.h>        // atoi, _wtoi, wcstombs (Read)
#include <string.h>
#include <wchar.h>

// inet_addr / ntohl (Read, .text:005ab888 / 005ab88f): declared by the winsock.h that
// windows.h pulls in (WIN32_LEAN_AND_MEAN is not defined in this build).
#pragma comment(lib, "ws2_32.lib")

// .data:0067e7b8 -- constructed by the static initializer .text:005a81c0, destroyed through
// the atexit thunk LAB_005a81e0 (`MOV ECX,0067e7bc; JMP FUN_005a9040`).
CLoginMgr G_LoginMgr;

bool G_bServerDisconnected = false; // DAT_0067ecbe
bool G_bServerConnected = false;    // DAT_0067ecbf

// DAT_00615b18 / DAT_00615b10 -- both 01 in the PE image; only TypeSelect's multiplayer pick
// (0x00566d6b..0x00566d7a) writes them.
unsigned char G_bEnumerateLanHosts = 1;   // DAT_00615b18
unsigned char G_bInternetServerList = 1;  // DAT_00615b10

namespace
{
    char const kLoginMgrSource[] = "E:\\FL\\Scratch\\Source\\Client\\LoginMgr.cpp";

    // FUN_005b6640 (cdecl) -- a REG_SZ value of the top key, decoded by HexToWide into Out.
    // False when the value is missing, not REG_SZ, its size is -1, or the second read does
    // not return exactly the size the first one reported.
    bool ReadRegHexString(FRegKeyStack& Keys, std::wstring& Out, char const* Name)
    {
        DWORD Size = 0;                                                // 005b6653
        DWORD Type = 0;
        if (RegQueryValueExA(Keys.Top(), Name, nullptr, &Type, nullptr, &Size) != ERROR_SUCCESS ||
            Type != REG_SZ || Size == 0xffffffffu)                     // 005b667a..005b6692
        {
            return false;
        }

        // 005b6698..005b66a9: _alloca((Size + 3) & ~3).
        BYTE* const Buffer = static_cast<BYTE*>(_alloca((Size + 3) & ~3u));
        DWORD Length = Size;                                           // 005b66ab
        DWORD Type2 = 0;
        LONG const Result = RegQueryValueExA(Keys.Top(), Name, nullptr, &Type2, Buffer, &Length);
        DWORD const Read = (Result == ERROR_SUCCESS && Type2 == REG_SZ) ? Length : 0xffffffffu;
        if (Read != Size)                                              // 005b66e4
        {
            return false;
        }

        wchar_t Wide[0x400];                                           // [EBP-0x804], 0x800 bytes
        HexToWide(reinterpret_cast<char const*>(Buffer), Wide);        // 005b66f4
        Out = Wide;                                                    // 005b66fa..005b67b5
        return true;
    }

    // LAB_005aaee0 (cdecl) -- ForEachFaction's callback: the record pushed onto the vector the
    // context points at (the VC6 insert(end, 1, record) inline: grow to size + max(size, 1)
    // when full, FUN_005af120 copies), then true -- the walk always goes on. The id is not read.
    bool __cdecl PushFactionRecord(unsigned /*NicknameId*/, FNewCharFaction const* Faction,
                                   void* Context)
    {
        static_cast<std::vector<FNewCharFaction>*>(Context)->push_back(*Faction);
        return true;
    }
}

// Inline in the CLoginMgr constructor for +0x94 (.text:005a8c14-005a8ce2).
FServerInfo::FServerInfo()
    : IpAddress(-1)
    , Port(0)
    , Field_0x28(0)
    , Ping(-2)
    , LastResponseTime(0)
    , Field_0x34(0)
    , ResponseCount(0)
    , Field_0x3c(0)
    , Field_0x3d(0)
    , Field_0x3e(0)
    , Field_0x40(0)
    , PlayerCount(0)
    , MaxPlayers(0)
    , Version(0)
    , Id(0)
    , bPassword(0)
    , Field_0x95(1)
    , Field_0x96(1)
    , Field_0x98(0)
{
}

// FUN_00531050 (on +0x248, the map). +0x00 is not written by the CLoginMgr constructor
// (0x005a8eab..0x005a8f02); the retail object is static, so CurrentId starts at 0.
FLoginHistory::FLoginHistory()
    : CurrentId(0)
{
}

// FUN_005b6be0
bool FLoginHistory::Contains(unsigned int Id) const
{
    return Records.find(Id) != Records.end();
}

// FUN_005b6bd0 -- `LEA EAX,[ESP+4]; PUSH EAX; ADD ECX,4; CALL FUN_005ad350` (operator[]).
FLoginRecord& FLoginHistory::GetRecord(unsigned int Id)
{
    return Records[Id];
}

// FUN_005b6c40 -- `PUSH ECX; ADD ECX,4; CALL FUN_005ad350; MOV [EAX+0x20],param_1`: the key
// pointer is the history object, i.e. &CurrentId.
void FLoginHistory::SetTime(unsigned int Time)
{
    Records[CurrentId].Time = Time;
}

// FUN_005b6c60 -- the same `PUSH ECX` key as SetTime; the RET-4 argument is never read.
unsigned int FLoginHistory::GetTime(unsigned int IgnoredId)
{
    (void)IgnoredId;
    return Records[CurrentId].Time;
}

// FUN_005b62a0 -- called from the CLoginMgr constructor (0x005a8f02).
bool FLoginHistory::Load()
{
    Records.clear();                                                   // 005b62c2..005b62df FUN_005b6e80

    // 005b62e4..005b6339: RegOpenKeyExA(HKCU, root, KEY_READ) pushed on the stack that starts
    // with HKEY_CURRENT_USER; a failed open or a null key ends the load.
    FRegKeyStack Keys;
    if (Keys.Open(G_FreelancerRegKey, KEY_READ) == nullptr)
    {
        return true;                                                   // LAB_005b65ba
    }

    // EBP: retail loads it from [ESP+0x1c] (0x005b6340), a slot only the failed-"time" path
    // below writes, so a first record without a readable "id" is keyed by whatever that stack
    // slot held. The port starts from 0.
    unsigned int Id = 0;
    for (int Index = 1; ; ++Index)                                     // 005b6344 / 005b65a7
    {
        char Name[0x2000];                                             // FUN_00402180, [ESP+0x198]
        _snprintf(Name, 0x2000, "MP\\Logins\\Login%d", Index);

        // 005b6362..005b63b2: Top(), depth < 32, RegOpenKeyExA(KEY_READ), pushed, non-null.
        if (Keys.Open(Name, KEY_READ) == nullptr)
        {
            break;                                                     // 005b65b0
        }

        // 005b63b8..005b6414: "id" into a 0x20-byte buffer; REG_DWORD as is, REG_SZ through
        // atoi, anything else (or a failed read) keeps the previous Id.
        DWORD Size = 0x20;
        DWORD Type = 0;
        BYTE Data[0x20];
        if (RegQueryValueExA(Keys.Top(), "id", nullptr, &Type, Data, &Size) == ERROR_SUCCESS)
        {
            if (Type == REG_DWORD)
            {
                Id = *reinterpret_cast<unsigned int const*>(Data);     // 005b63f5
            }
            else if (Type == REG_SZ)
            {
                Id = static_cast<unsigned int>(MsvcrtAtoi(reinterpret_cast<char const*>(Data)));
            }
        }

        // 005b6416..005b6494: insert {Id, FLoginRecord()} (FUN_005b6600 / FUN_005b6d30 /
        // FUN_005b6c70 -- a no-op when Id is present), then "account" into the node's record.
        std::map<unsigned int, FLoginRecord>::iterator Node =
            Records.insert(std::make_pair(Id, FLoginRecord())).first;
        ReadRegHexString(Keys, Node->second.Account, "account");

        // 005b6497..005b6515: the same insert again, then "password".
        Node = Records.insert(std::make_pair(Id, FLoginRecord())).first;
        ReadRegHexString(Keys, Node->second.Password, "password");

        // 005b6518..005b659b: "time" (0x20-byte buffer); REG_DWORD / REG_SZ into operator[](Id).
        // Time, anything else (or a failed read) stores 0.
        Size = 0x20;
        BYTE TimeData[0x20];
        if (RegQueryValueExA(Keys.Top(), "time", nullptr, &Type, TimeData, &Size) == ERROR_SUCCESS &&
            (Type == REG_DWORD || Type == REG_SZ))
        {
            unsigned int const Time =
                Type == REG_DWORD ? *reinterpret_cast<unsigned int const*>(TimeData)
                                  : static_cast<unsigned int>(MsvcrtAtoi(reinterpret_cast<char const*>(TimeData)));
            Records[Id].Time = Time;                                   // 005b6576..005b6586
        }
        else
        {
            Records[Id].Time = 0;                                      // 005b658b..005b659b
        }

        Keys.PopClose();                                               // 005b659e FUN_00568810
    }
    // 005b65b0 PopClose, 005b65ba CloseAll: the stack's destructor.
    return true;                                                       // 005b65e0 MOV AL,1
}

// FUN_005b67e0 -- called by CLoginMgr::SaveLogin (0x005a9645).
bool FLoginHistory::Save()
{
    FRegKeyStack Keys;                                                 // 005b680b..005b6835
    // 005b683e..005b6894 / 005b689a..005b68f2: RegCreateKeyExA(KEY_READ | KEY_WRITE) twice.
    if (Keys.Create(G_FreelancerRegKey, 0x2001f) == nullptr ||
        Keys.Create("MP\\Logins", 0x2001f) == nullptr)
    {
        return true;                                                   // LAB_005b6b20 / 005b6b24
    }

    char Name[0x2000];                                                 // FUN_00402180, [ESP+0x8b4]
    int Index = 1;                                                     // 005b6900
    for (std::map<unsigned int, FLoginRecord>::iterator It = Records.begin(); It != Records.end();
         ++It, ++Index)                                                // 005b6a9c..005b6ab4
    {
        _snprintf(Name, 0x2000, "Login%d", Index);
        // 005b694c..005b69aa: Top(), depth < 32, RegCreateKeyExA, pushed, non-null -- else the
        // record is skipped (its index is still used up).
        if (Keys.Create(Name, 0x2001f) == nullptr)
        {
            continue;
        }

        DWORD IdValue = It->first;                                     // 005b69b4
        RegSetValueExA(Keys.Top(), "id", 0, REG_DWORD, reinterpret_cast<BYTE const*>(&IdValue), 4);

        char AccountHex[0x400];                                        // [ESP+0x4b0]
        WideToHex(It->second.Account.c_str(), AccountHex);             // 005b69d7..005b69f0
        Keys.SetString("account", AccountHex);                         // 005b6a06 FUN_005b6b90

        // 005b6a0b..005b6a54: the same REG_SZ write, spelled inline.
        char PasswordHex[0x400];                                       // [ESP+0xb0]
        WideToHex(It->second.Password.c_str(), PasswordHex);
        Keys.SetString("password", PasswordHex);

        DWORD TimeValue = It->second.Time;                             // 005b6a56..005b6a82
        RegSetValueExA(Keys.Top(), "time", 0, REG_DWORD, reinterpret_cast<BYTE const*>(&TimeValue), 4);

        Keys.PopClose();                                               // 005b6a84..005b6a96 inline
    }

    // 005b6ab9..005b6afe: delete the stale LoginN keys past the last record until a delete fails.
    for (;; ++Index)
    {
        _snprintf(Name, 0x2000, "Login%d", Index);
        HKEY const Parent = Keys.Top();                                // 005b6ade..005b6ae8
        if (Parent == nullptr)
        {
            break;
        }
        if (RegDeleteKeyA(Parent, Name) != ERROR_SUCCESS)              // 005b6aec..005b6af8
        {
            break;
        }
    }
    // 005b6b00..005b6b72: the remaining keys closed (PopClose, then the stack's CloseAll).
    return true;                                                       // 005b6b7e MOV AL,1
}

// .text:005a8be0
CLoginMgr::CLoginMgr()
    : Field_0x180(90.0f)            // DAT_005e64c0 = 42B40000h
    , bConnected(0)
    , LoginName(L"SinglePlayer")    // .rdata:005e3094
    , bCreateAccount(0)
    , Field_0x1ac(50000.0f)         // DAT_005e64c4 = 47435000h
    , bLoginResponse(0)
    , LoginStatus(5)
    , Field_0x1c8(0)
    , SaveFailureCount(0)
    , Field_0x1d0(3000.0f)          // DAT_005e64c8 = 453B8000h
    , bCharacterInfoArrived(0)
    , Field_0x218(3000.0f)          // DAT_005e64cc = 453B8000h
    , Field_0x21c(0)
    , ListServerHost("FLListServer2") // .rdata:005e650c
    , ListServerPort(0x708a)
    , Gun(nullptr)
    , Field_0x238(0)
    , Field_0x23c(0)
    , Field_0x240(0)
    , Field_0x25c(0)
{
    SelectCharacterName[0] = L'\0'; // .text:005a8efb `MOV word ptr [ESI+0x1e8],BX`
    Logins.Load();                  // .text:005a8f02 FUN_005b62a0
    // Field_0x1c8 / SaveFailureCount: the constructor does not write them; the retail object
    // is static and therefore zero-initialised.
}

// .text:005a9440
bool CLoginMgr::SaveLogin()
{
    // 005a945e..005a9501: the current server's Address copied into a local that is never read
    // (the FServerInfo temporary FUN_005684a0 returned is destroyed right after).
    std::wstring const UnusedAddress = GetCurrentServer().Address;
    std::wstring const Account = GetLoginName();                       // 005a9508 FUN_005a96b0
    std::wstring const UnusedPassword = GetLoginPassword();            // 005a951c FUN_005a9800
    (void)UnusedAddress;
    (void)UnusedPassword;

    Logins.GetRecord(GetCurrentServer().Id);                           // 005a9531..005a954d, result unused
    Logins.CurrentId = GetCurrentServer().Id;                          // 005a95aa..005a95c5
    Logins.Records[Logins.CurrentId].Account = Account;                // 005a960f..005a962a
    Logins.SetTime(static_cast<unsigned int>(time(nullptr)));          // 005a9630..005a963e
    return Logins.Save();                                              // 005a9645
}

// .text:005a96b0 (RET 4, the copy is built in the caller's slot).
std::wstring CLoginMgr::GetLoginName() const
{
    return LoginName;
}

// .text:005a9800 -- FUN_005a96b0's body on +0x198 (the two diff only in the offset).
std::wstring CLoginMgr::GetLoginPassword() const
{
    return LoginPassword;
}

// .text:005684a0 (RET 4) -- FServerInfo's copy constructor inlined on +0x94.
FServerInfo CLoginMgr::GetCurrentServer() const
{
    return CurrentServer;
}

// .text:005a9040 (the atexit thunk) / .text:005a8f20 (deleting destructor). Member
// destructors and ~CDPClient follow implicitly in the retail order.
CLoginMgr::~CLoginMgr()
{
    ResetServerLists();          // .text:005a907b FUN_005abc80
    bLoginResponse = 0;          // .text:005a9088
    bCharacterInfoArrived = 0;   // .text:005a908e
    Field_0x21c = 0;             // .text:005a9094
}

// .text:005ab590 (Gun2::IGUNQueueRead slot 0, RET 8) -- the list server's server-list reply
// 0x7204. A non-empty page asks the browser for the next one (slot 5, (0x65, page, 0, 0, 0));
// each entry becomes an FServerInfo, appended to Servers when no entry with the same Id and
// +0x64 key is there yet, and copied into +0x160 when its key is non-empty and the Servers
// entry it matches has +0x3e clear. Every path returns 0 (.text:005abc24).
long __stdcall CLoginMgr::Read(Gun2::GUNQueueMessage* Message)
{
    Gun2::GUNMessageBody* Body = Message->Body;                        // 005ab5a9
    unsigned long const Type = Body->Type;                             // 005ab5ac
    if ((Type & 0xf00) != 0x200 || (Type & 0xff000) != 0x7000 || Type != 0x7204)
    {
        return 0;                                                      // 005ab5c5 / 5d9 / 5e5
    }

    Gun2::GUNServerEntry** Entries = nullptr;                          // 005ab5f4
    unsigned long EntryCount = 0;                                      // 005ab5f8
    if (Body->bHasEntries != 0)                                        // 005ab5fc
    {
        EntryCount = Body->EntryCount;                                 // 005ab5fe
        Entries = Body->Entries;                                       // 005ab603
    }
    if (Body->bHasEntries != 0 && EntryCount > 0)                      // 005ab60e JBE
    {
        unsigned int const Page = Field_0x23c;                         // 005ab617
        Field_0x23c = Page + 1;                                        // 005ab623
        Gun->GetBrowser()->SendRequest7104(0x65, Page, 0, 0, 0);                // 005ab629..005ab638
    }
    else
    {
        Field_0x23c = 0;                                               // 005ab644
    }

    for (unsigned long Index = 0; Index < EntryCount; ++Index)         // 005abc05..005abc14
    {
        Gun2::GUNServerEntry* Entry = Entries[Index];                  // 005ab65e

        // 005ab665..005ab76e: the FServerInfo default spelled inline, then +0x28 = 1,
        // +0x88 = 1, +0x98 = 2 (the other stores it repeats -- +0x2c -2, +0x84 / +0x8c /
        // +0x90 / +0x94 0, +0x95 / +0x96 1 -- are the default constructor's values).
        FServerInfo Info;
        Info.Field_0x28 = 1;                                           // 005ab746
        Info.MaxPlayers = 1;                                           // 005ab755
        Info.Field_0x98 = 2;                                           // 005ab75c

        Info.Name = Entry->Name;                                       // 005ab76e..005ab7b7
        Info.Field_0x54 = Entry->Description;                          // 005ab7bd..005ab802
        Info.Version = static_cast<unsigned int>(MsvcrtWtoi(Entry->Version)); // 005ab808..005ab812
        Info.Address = Entry->Address;                                 // 005ab819..005ab858

        // 005ab85c..005ab895: [ESP+0xd4], 0x84 bytes up to the record buffer.
        char AddressText[0x84];
        sprintf(AddressText, "%S", Info.Address.c_str());
        Info.IpAddress = static_cast<int>(ntohl(inet_addr(AddressText)));

        // 005ab899..005ab8aa: "p1,p2,...:+0x88:password:+0x96:+0x95:+0x64:Id". Any missing ':'
        // or an Id of -1 drops the entry (LAB_005abb89).
        char Record[0x400];
        wcstombs(Record, Entry->Record, sizeof(Record));
        do
        {
            char* Colon = strchr(Record, ':');                         // 005ab8c0
            if (Colon == nullptr)
            {
                break;
            }
            *Colon = '\0';                                             // 005ab8d9

            // 005ab8db..005ab94a: split from the right, so Ports holds the list reversed.
            char* Comma = strrchr(Record, ',');
            while (Comma != nullptr)
            {
                *Comma = '\0';                                         // 005ab8f0
                Info.Ports.push_back(static_cast<unsigned int>(MsvcrtAtoi(Comma + 1)));
                Comma = strrchr(Record, ',');                          // 005ab91c
            }
            Info.Ports.push_back(static_cast<unsigned int>(MsvcrtAtoi(Record))); // 005ab929..005ab94a

            char* Field = Colon + 1;                                   // 005ab94f
            Colon = strchr(Field, ':');
            if (Colon == nullptr)
            {
                break;
            }
            *Colon = '\0';
            Info.MaxPlayers = MsvcrtAtoi(Field);                             // 005ab96d..005ab96f

            Field = Colon + 1;                                         // 005ab976
            Colon = strchr(Field, ':');
            if (Colon == nullptr)
            {
                break;
            }
            *Colon = '\0';
            Info.bPassword = static_cast<unsigned char>(MsvcrtAtoi(Field) != 0); // 005ab996..005ab9a3

            Field = Colon + 1;                                         // 005ab99a
            Colon = strchr(Field, ':');
            if (Colon == nullptr)
            {
                break;
            }
            *Colon = '\0';
            Info.Field_0x96 = static_cast<unsigned char>(MsvcrtAtoi(Field) != 0); // 005ab9c4..005ab9cb

            Field = Colon + 1;                                         // 005ab9d2
            Colon = strchr(Field, ':');
            if (Colon == nullptr)
            {
                break;
            }
            *Colon = '\0';
            Info.Field_0x95 = static_cast<unsigned char>(MsvcrtAtoi(Field) != 0); // 005ab9f2..005ab9fd

            Field = Colon + 1;                                         // 005ab9f9
            Colon = strchr(Field, ':');                                // 005aba04
            if (Colon == nullptr)
            {
                break;
            }
            *Colon = '\0';                                             // 005aba18
            Info.Field_0x64 = Field;                                   // 005aba1a..005aba62

            Info.Id = static_cast<unsigned int>(MsvcrtAtoi(Colon + 1));      // 005aba65..005aba75
            if (Info.Id == 0xffffffffu)                                // 005aba72 CMP EAX,-1
            {
                break;
            }

            // 005aba82..005abb01: always true here (1 and 2 above), tested all the same.
            if (Info.Field_0x28 != 0 && Info.Field_0x98 != 0)
            {
                bool bKnown = false;
                for (size_t ServerIndex = 0; ServerIndex < Servers.size(); ++ServerIndex)
                {
                    FServerInfo const& Server = Servers[ServerIndex];
                    if (Server.Id == Info.Id && Server.Field_0x64 == Info.Field_0x64)
                    {
                        bKnown = true;                                 // 005abacf -> 005abb0a
                        break;
                    }
                }
                if (!bKnown)
                {
                    Servers.push_back(Info);                           // 005abaf5 FUN_005adfc0
                    Field_0x238 = 1;                                   // 005abafa
                }
            }

            // 005abb0a..005abb84
            if (!Info.Field_0x64.empty())
            {
                FServerInfo* Match = nullptr;
                for (size_t ServerIndex = 0; ServerIndex < Servers.size(); ++ServerIndex)
                {
                    FServerInfo& Server = Servers[ServerIndex];
                    if (Server.Field_0x64 == Info.Field_0x64 && Server.Id == Info.Id)
                    {
                        Match = &Server;                               // 005abb53 -> 005abb67
                        break;
                    }
                }
                if (Match != nullptr && Match->Field_0x3e == 0)        // 005abb67..005abb6e
                {
                    Field_0x160.push_back(Info);                       // 005abb84 FUN_005adfc0
                }
            }
        } while (false);
        // 005abb89..005abbff: Info destroyed.
    }
    return 0;                                                          // 005abc24
}

// .text:005accd0 (CLoginMgr's CDPClient vftable 005e64f0 slot 3, thiscall on the CDPClient
// sub-object, RET 4). The reply data, when present, is "+0x96:+0x95:Version:Id:+0x64:"
// followed by a wide string that becomes +0x54 (005ad0ce wcslen on the byte after the fifth
// ':'); a missing ':' or an Id of -1 drops the reply.
void CLoginMgr::OnAddHost(_DPNMSG_ENUM_HOSTS_RESPONSE* Response)
{
    DPN_APPLICATION_DESC const* Desc = Response->pApplicationDescription; // 005accfa

    // 005acd06..005acdf3: the FServerInfo default spelled inline.
    FServerInfo Info;
    Info.Port = 0;                                                     // 005acdfd
    Info.Field_0x28 = 1;                                               // 005ace01
    Info.Ping = static_cast<int>(Response->dwRoundTripLatencyMS); // 005acdfa / 005ace09
    if (Desc->dwCurrentPlayers != 0)                                   // 005ace10
    {
        Info.PlayerCount = static_cast<int>(Desc->dwCurrentPlayers - 1); // 005ace1f
    }
    else
    {
        Info.PlayerCount = 0;                                           // 005ace29
    }
    Info.MaxPlayers = static_cast<int>(Desc->dwMaxPlayers - 1);        // 005ace30..005ace34
    Info.Field_0x98 = 2;                                               // 005ace3b
    Info.LastResponseTime = static_cast<int>(timeGetTime());                 // 005ace46
    Info.Version = 0;                                                  // 005ace53
    if (Response->pvUserContext == nullptr)                            // 005ace50
    {
        Info.Field_0x3e = 1;                                           // 005ace5c
    }
    Info.Name = Desc->pwszSessionName;                                 // 005ace61..005acea3

    // 005acea9..005acedc: [ESP+0x14c], 0x800 bytes; data type 1 (DPNA_DATATYPE_STRING) in and
    // out through the same dword for both lookups. The HRESULTs are not read.
    wchar_t HostName[0x400];
    DWORD Size = sizeof(HostName);
    DWORD DataType = DPNA_DATATYPE_STRING;
    Response->pAddressSender->GetComponentByName(L"hostname", HostName, &Size, &DataType);
    Info.Address = HostName;                                           // 005acedf..005acf24

    // 005acf2a..005acf5d: [ESP+0xcc], 0x80 bytes up to the host-name buffer.
    char AddressText[0x80];
    sprintf(AddressText, "%S", Info.Address.c_str());
    Info.IpAddress = static_cast<int>(ntohl(inet_addr(AddressText))); // 005acf78

    DWORD PortValue = 0;                                               // 005acf84
    Size = sizeof(PortValue);                                          // 005acf8b
    Response->pAddressSender->GetComponentByName(L"port", &PortValue, &Size, &DataType);
    Info.Port = PortValue;                                             // 005acf99..005acfa3

    // 005acfa7..005acfb1: DPNSESSION_REQUIREPASSWORD (0x80).
    Info.bPassword = static_cast<unsigned char>((Desc->dwFlags >> 7) & 1);

    char* Data = static_cast<char*>(Response->pvResponseData);         // 005acfa0
    if (Data != nullptr)                                               // 005acfb8
    {
        char* Colon = strchr(Data, ':');                               // 005acfc7
        if (Colon == nullptr)
        {
            return;                                                    // LAB_005ad212
        }
        *Colon = '\0';                                                 // 005acfdd
        Info.Field_0x96 = static_cast<unsigned char>(MsvcrtAtoi(Data) != 0); // 005acfdf..005acfec

        char* Field = Colon + 1;                                       // 005acfe3
        Colon = strchr(Field, ':');
        if (Colon == nullptr)
        {
            return;
        }
        *Colon = '\0';
        Info.Field_0x95 = static_cast<unsigned char>(MsvcrtAtoi(Field) != 0); // 005ad00d..005ad014

        Field = Colon + 1;                                             // 005ad01b
        Colon = strchr(Field, ':');
        if (Colon == nullptr)
        {
            return;
        }
        *Colon = '\0';
        Info.Version = static_cast<unsigned int>(MsvcrtAtoi(Field));         // 005ad03b..005ad041

        Field = Colon + 1;                                             // 005ad03d
        Colon = strchr(Field, ':');
        if (Colon == nullptr)
        {
            return;
        }
        *Colon = '\0';
        Info.Id = static_cast<unsigned int>(MsvcrtAtoi(Field));              // 005ad062..005ad068

        Field = Colon + 1;                                             // 005ad064
        Colon = strchr(Field, ':');                                    // 005ad06f
        if (Colon == nullptr)
        {
            return;                                                    // 005ad07a -> LAB_005ad214
        }
        *Colon = '\0';                                                 // 005ad085
        Info.Field_0x64 = Field;                                       // 005ad080..005ad0c8
        Info.Field_0x54 = reinterpret_cast<wchar_t const*>(Colon + 1); // 005ad0ce..005ad103
    }

    if (Info.Id == 0xffffffffu)                                        // 005ad109..005ad115
    {
        return;
    }

    RemovePendingDirect(Info);                                         // 005ad136 FUN_005ac010
    RemoveRetryDirect(Info);                                           // 005ad142 FUN_005abde0

    // 005ad147..005ad199
    for (size_t ServerIndex = 0; ServerIndex < Servers.size(); ++ServerIndex)
    {
        FServerInfo& Server = Servers[ServerIndex];
        if (Server.Field_0x64 == Info.Field_0x64 && Server.Id == Info.Id)
        {
            // 005ad283..005ad33a: refresh the known entry. Address, Port, +0x10, +0x64 and Id
            // are left as they are.
            Server.Field_0x96 = Info.Field_0x96;
            Server.Field_0x95 = Info.Field_0x95;
            Server.bPassword = Info.bPassword;
            Server.Field_0x54 = Info.Field_0x54;                       // 005ad2bf assign(s, 0, npos)
            Server.Name = Info.Name;                                   // 005ad2d7
            Server.PlayerCount = Info.PlayerCount;
            Server.MaxPlayers = Info.MaxPlayers;
            Server.ResponseCount = Server.ResponseCount + 1;                 // 005ad2f7
            // 005ad301..005ad30b: unsigned minimum (CMP ECX,EAX; JNC keeps the old value).
            if (static_cast<unsigned int>(Info.Ping) < static_cast<unsigned int>(Server.Ping))
            {
                Server.Ping = Info.Ping;
            }
            Server.Field_0x40 = 0;                                     // 005ad30e
            Server.LastResponseTime = Info.LastResponseTime;                       // 005ad315
            Server.Version = Info.Version;                             // 005ad326
            Server.Field_0x3e = Info.Field_0x3e;                       // 005ad330
            Field_0x238 = 1;                                           // 005ad333 [CDPClient+0x234]
            Server.Field_0x3d = 0;                                     // 005ad33a
            return;
        }
    }

    // 005ad19b..005ad20b: always true here (1 and 2 above), tested all the same; the second
    // search, with the terms the other way round, cannot find what the first one missed.
    if (Info.Field_0x28 != 0 && Info.Field_0x98 != 0)
    {
        for (size_t ServerIndex = 0; ServerIndex < Servers.size(); ++ServerIndex)
        {
            FServerInfo const& Server = Servers[ServerIndex];
            if (Server.Id == Info.Id && Server.Field_0x64 == Info.Field_0x64)
            {
                return;                                                // 005ad1e2 -> LAB_005ad212
            }
        }
        Servers.push_back(Info);                                       // 005ad206 FUN_005ad6a0
        Field_0x238 = 1;                                               // 005ad20b
    }
    // 005ad212..005ad262: Info destroyed.
}

// .text:005a9990
int CLoginMgr::Connect(bool bDisconnectFirst)
{
    // .text:005a99b5-005a9a0a: copies of Name and Address are built and destroyed unused.
    if (bDisconnectFirst && bConnected && GetPlayerConnectionId() != 0)
    {
        G_Server->DisConnect(GetPlayerConnectionId(), EFLConnection::NONE); // +0x40
        G_bServerDisconnected = false;
        G_bServerConnected = false;
        bConnected = 0;
        bLoginResponse = 0;
        bCharacterInfoArrived = 0;
        Characters.free();          // FUN_005ae7a0
        Field_0x21c = 0;
    }

    bConnected = 0;                 // .text:005a9a7b

    char Address[0x400];
    if (SinglePlayer())
    {
        FDUMP(0x100003, "%s(%d) : NOTICE:General:%s", kLoginMgrSource, 0x15f,
              "Starting single player game.\n");
        SetupSinglePlayer();        // FUN_005aa210; its result is not read
        sprintf(Address, "%s", "localpass");
    }
    else
    {
        char Notice[0x2000];
        sprintf(Notice, "MP Connect: Server %S, IP %S\n", CurrentServer.Name.c_str(),
                CurrentServer.Address.c_str());
        FDUMP(0x100003, "%s(%d) : NOTICE:General:%s", kLoginMgrSource, 0x165, Notice);

        char* Cursor = Address + sprintf(Address, "tcpip, %S:", CurrentServer.Address.c_str());
        if (CurrentServer.Port != 0)
        {
            sprintf(Cursor, "%u", CurrentServer.Port);
        }
        else
        {
            // .text:005a9b57-005a9b97: "%u" per port, joined with ','.
            std::vector<unsigned int>::const_iterator It = CurrentServer.Ports.begin();
            while (It != CurrentServer.Ports.end())
            {
                Cursor += sprintf(Cursor, "%u", *It);
                ++It;
                if (It == CurrentServer.Ports.end())
                {
                    break;
                }
                Cursor[0] = ',';
                Cursor[1] = '\0';
                ++Cursor;
            }
        }
    }

    // .text:005a9ba7: a null pointer when the password is empty (_Len == 0), not L"".
    unsigned short* Password = nullptr;
    if (!ServerPassword.empty())
    {
        Password = reinterpret_cast<unsigned short*>(const_cast<wchar_t*>(ServerPassword.c_str()));
    }
    G_Server->Connect(Address, Password); // +0x3c

    return WaitForServerResponse(&bConnected, 5.0f, 60.0f);
}

// .text:005a9c10 -- IClient slot 11 (LAB_005a81f0) after it sets DAT_0067ecbf.
void CLoginMgr::OnConnectResponse(unsigned int ClientId)
{
    SetPlayerConnectionId(ClientId);
    bConnected = 1;
}

// .text:005aa210
bool CLoginMgr::SetupSinglePlayer()
{
    if (bConnected)
    {
        return false;
    }

    CurrentServer.Address = L"localpass";          // .rdata:005e65b8
    CurrentServer.IpAddress = -1;
    CurrentServer.Field_0x28 = 1;
    CurrentServer.Ping = 0;
    CurrentServer.PlayerCount = 0;
    CurrentServer.MaxPlayers = 1;
    CurrentServer.Field_0x98 = 2;
    CurrentServer.Version = g_client.packed_version; // DAT_0067ecb8
    CurrentServer.Name = L"";                       // .rdata:005c8ce0
    CurrentServer.Field_0x54 = L"";
    CurrentServer.Field_0x74 = "";                  // .rdata:005c865c

    // .text:005aa3a9-005aa432: the body of FUN_005abc80, inlined.
    ResetServerLists();
    return true;
}

// .text:005aa440
bool CLoginMgr::Logout()
{
    if (GetPlayerConnectionId() != 0)
    {
        G_Server->DisConnect(GetPlayerConnectionId(), EFLConnection::NONE); // +0x40
        G_bServerDisconnected = false;
        G_bServerConnected = false;
        bConnected = 0;
        bLoginResponse = 0;
        bCharacterInfoArrived = 0;
        Characters.free();          // inlined OwnerList::free (.text:005aa47f-005aa4ef)
        Field_0x21c = 0;
    }
    return true;
}

// .text:005aa510
int CLoginMgr::Login()
{
    if (!bConnected)
    {
        return 0;
    }

    bLoginResponse = 0;

    SLoginInfo Info = {};
    Info.CreateFlag = bCreateAccount;                      // byte at +0xc0
    wcsncpy(Info.AccountName, LoginName.c_str(), 0x28);
    wcsncpy(Info.Field_0xA0, LoginPassword.c_str(), 0x10);
    Info.AccountName[0x27] = L'\0';                        // word at +0x4e
    Info.Field_0xA0[0x0f] = L'\0';                         // word at +0xbe
    G_Server->Login(Info, GetPlayerConnectionId());        // +0x48

    int const Result = WaitForServerResponse(&bLoginResponse, 5.0f, 60.0f);
    if (Result != 1 || LoginStatus != 3)
    {
        return 0;
    }

    if (!SinglePlayer())
    {
        SaveLogin();                // .text:005aa5e2 FUN_005a9440, result ignored
        return Result;
    }

    if (G_Server == nullptr)
    {
        return Result;
    }

    // +0x15c ReqDifficultyScale: 0 under "+G", else perf-option slot 6.
    float const Scale = G_bCommandLineG ? 0.0f : GetPerfOptionSlot(6);
    G_Server->ReqDifficultyScale(Scale, GetPlayerConnectionId());
    return Result;
}

// .text:005aa660 -- IClient slot 12 (LAB_005a8210 jumps here with both arguments).
void CLoginMgr::OnLoginResponse(unsigned int Unused, unsigned char const* Status)
{
    (void)Unused;
    LoginStatus = *Status;
    bLoginResponse = 1;
}

// .text:005aa680
int CLoginMgr::RequestCharacterInfo()
{
    if (G_Server == nullptr)
    {
        return 0;
    }

    bCharacterInfoArrived = 0;
    Characters.free();              // inlined OwnerList::free (.text:005aa6a3-005aa70f)
    G_Server->CharacterInfoReq(GetPlayerConnectionId(), false); // +0x4c
    return WaitForServerResponse(&bCharacterInfoArrived, 5.0f, 60.0f);
}

// .text:005aa8f0 (RET 4). The same inlined OwnerList::free as RequestCharacterInfo
// (.text:005aa913-005aa980, instruction for instruction), then IServer::DestroyCharacter and
// the wait on +0x1d4 (FUN_005a89b0(&+0x1d4, 5.0f, 60.0f)).
int CLoginMgr::DestroyCharacter(CHARACTER_ID const& CharId)
{
    if (G_Server == nullptr)
    {
        return 0;
    }

    bCharacterInfoArrived = 0;
    Characters.free();
    G_Server->DestroyCharacter(CharId, GetPlayerConnectionId()); // +0x5c
    return WaitForServerResponse(&bCharacterInfoArrived, 5.0f, 60.0f);
}

// .text:005aa750 (RET 4).
int CLoginMgr::CreateNewCharacter(SCreateCharacterInfo const& Info)
{
    // 0x005aa751..0x005aa765: no server -> 0.
    if (G_Server == nullptr)
    {
        return 0;
    }

    // 0x005aa769..0x005aa7dd: +0x1d4 = 0 and the inlined OwnerList::free (the same body as
    // RequestCharacterInfo's).
    bCharacterInfoArrived = 0;
    Characters.free();
    // 0x005aa7e3..0x005aa7f6: IServer slot 22 (+0x58) with the client id DAT_00673344.
    G_Server->CreateNewCharacter(Info, GetPlayerConnectionId());
    // 0x005aa7f9..0x005aa808: FUN_005a89b0(&+0x1d4, 5.0f, 60.0f).
    int Result = WaitForServerResponse(&bCharacterInfoArrived, 5.0f, 60.0f);

    // 0x005aa80d..0x005aa821: no answer (the byte at +0x1d4), or an answer that brought
    // characters (+0x1b8, the list head) -> the wait's result.
    if (bCharacterInfoArrived == 0 || Characters.get_head() != nullptr)
    {
        return Result;
    }

    // 0x005aa827..0x005aa836: the server re-tested; gone -> 0.
    if (G_Server == nullptr)
    {
        return 0;
    }

    // 0x005aa839..0x005aa8b5: RequestCharacterInfo's body inlined (+0x1d4 = 0, the list freed,
    // IServer slot 19 +0x4c (client id, false), FUN_005a89b0(&+0x1d4, 5.0f, 60.0f)).
    bCharacterInfoArrived = 0;
    Characters.free();
    G_Server->CharacterInfoReq(GetPlayerConnectionId(), false);
    Result = WaitForServerResponse(&bCharacterInfoArrived, 5.0f, 60.0f);
    // 0x005aa8da..0x005aa8df: 1 -> 4.
    if (Result == 1)
    {
        Result = 4;
    }
    return Result;
}

// .text:005aaca0 (RET 4). Walk from the head (+0x1b8) counting nodes; the node at Index gives
// its data (+0x08), a list shorter than Index + 1 gives null.
CCharacterSet::CharacterInfo* CLoginMgr::CharacterAt(int Index)
{
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
    int Count = 0;
    while (Characters.traverse(Node, true))
    {
        if (Count == Index)
        {
            return Node->Data;
        }
        Count = Count + 1;
    }
    return nullptr;
}

// .text:005a9300 -- FUN_005abc80's body, then three flags.
void CLoginMgr::Reset()
{
    ResetServerLists();
    bLoginResponse = 0;
    bCharacterInfoArrived = 0;
    Field_0x21c = 0;
}

// .text:005abc80
void CLoginMgr::ResetServerLists()
{
    CancelEnums();
    // erase(begin, end) through FUN_005ad910: the three lists end up empty.
    Servers.clear();
    Field_0x160.clear();
    Field_0x170.clear();
    Field_0x25c = 0;

    // .text:005abd80: clear +0x3d on the source entry, then insert a copy at the end of
    // +0x160 (FUN_005adfc0(end, 1, entry)).
    for (size_t Index = 0; Index < DirectServers.size(); ++Index)
    {
        DirectServers[Index].Field_0x3d = 0;
        Field_0x160.push_back(DirectServers[Index]);
    }
}

// .text:005ac710 (thiscall, RET 4).
bool CLoginMgr::AddDirectServer(char const* HostPort)
{
    // 0x005ac72b..0x005ac757: no argument, or no copy -> false.
    if (HostPort == nullptr)
        return false;
    char* const Copy = _strdup(HostPort);
    if (Copy == nullptr)
        return false;

    // 0x005ac75c..0x005ac76d: the last ':'; none -> the copy freed, true (0x005ac99c).
    char* const Colon = strrchr(Copy, ':');
    if (Colon != nullptr)
    {
        // 0x005ac773..0x005ac784: the ':' cut to 0, the host widened into a 0x400-wchar local.
        *Colon = '\0';
        char const* const PortText = Colon + 1;
        wchar_t Host[0x400];
        mbstowcs(Host, Copy, 0x400);

        // 0x005ac78a..0x005ac89e: a default server entry (the FServerInfo ctor inlined).
        FServerInfo Entry;
        // 0x005ac89e..0x005ac8e3: Address = the widened host (wcslen, grow, copy, eos).
        Entry.Address.assign(Host, wcslen(Host));
        // 0x005ac8e4..0x005ac8f3: IpAddress = ntohl(inet_addr(host)).
        Entry.IpAddress = static_cast<int>(ntohl(inet_addr(Copy)));
        // 0x005ac8f7..0x005ac914: one port, atoi(port), into Ports (FUN_00537260 insert(end, 1)).
        unsigned const Port = static_cast<unsigned>(MsvcrtAtoi(PortText));
        Entry.Ports.push_back(Port);
        // 0x005ac919..0x005ac931: appended to +0x150 (FUN_005adfc0(end, 1, &Entry)).
        DirectServers.push_back(Entry);
    } // 0x005ac936..0x005ac996: the entry's strings, its port buffer and its address destroyed

    // 0x005ac99c..0x005ac9a6.
    free(Copy);
    return true;
}

// .text:005abc40
int CLoginMgr::GetGunStatus()
{
    if (Gun == nullptr)
    {
        return 3;
    }
    return static_cast<int>(Gun->GetStatus());
}

// .text:005abc60 -- vtable +4 with 1 is CGunWrapper's deleting destructor.
void CLoginMgr::DestroyGun()
{
    if (Gun != nullptr)
    {
        delete Gun;
        Gun = nullptr;
    }
}

// .text:005abdb0
FServerInfo* CLoginMgr::FindServer(unsigned int Id)
{
    for (size_t Index = 0; Index < Servers.size(); ++Index)
    {
        if (Servers[Index].Id == Id)
        {
            return &Servers[Index];
        }
    }
    return nullptr;
}

// .text:005a9c30
bool CLoginMgr::SetCurrentServer(FServerInfo const& Server)
{
    if (Server.Field_0x28 == 0 || Server.Field_0x98 == 0)
        return false;
    // .text:005a9c4c..005a9e87: FServerInfo::operator= (FUN_005a9ea0) inline into +0x94.
    CurrentServer = Server;
    return true;
}

// .text:005ab4c0
bool CLoginMgr::CreateGun()
{
    if (G_bInternetServerList != 0 && Gun == nullptr)
    {
        // .rdata:005e64d0 -- the same application GUID CDPServer hosts with.
        GUID Guid = {0xa690f026, 0x26f0, 0x4e57, {0xac, 0xa0, 0xec, 0xf8, 0x68, 0xe4, 0x8d, 0x21}};
        // 0x005ab532..0x005ab558: operator_new(0x28), then the ctor with the host's text
        // (the empty string when there is none), the port and no cd-key.
        Gun = new CGunWrapper("..\\exe\\gundll.dll", Guid, ListServerHost.c_str(),
                              ListServerPort, nullptr);
        Field_0x23c = 0;
    }
    return true;
}

// .text:005ac240
bool CLoginMgr::RequestServerList()
{
    if (Gun != nullptr && Gun->GetBrowser() != nullptr &&
        static_cast<int>(Gun->GetStatus()) == 2)
    {
        Field_0x240 = 0;
        if (Field_0x23c != 0)
            return false;
        Gun->GetBrowser()->SendRequest7104(0x65, 0, 0, 0, 0);
        ++Field_0x23c;
        return true;
    }
    Field_0x240 = 1;
    return false;
}

namespace
{
    // The two lookups PingServers spells inline (0x005ac423..0x005ac471 and
    // 0x005ac5a0..0x005ac5e9): the list-server entry with Entry's +0x64 text and Id, or null.
    // Only asked when that text is not empty.
    FServerInfo* FindServerByKey(std::vector<FServerInfo>& Servers, FServerInfo const& Entry)
    {
        for (size_t Index = 0; Index < Servers.size(); ++Index)
        {
            if (Servers[Index].Field_0x64 == Entry.Field_0x64 && Servers[Index].Id == Entry.Id)
                return &Servers[Index];
        }
        return nullptr;
    }

    // The match RemovePendingDirect / RemoveRetryDirect share (0x005abe00..0x005abe46).
    bool IsSameDirectServer(FServerInfo const& Entry, FServerInfo const& Server)
    {
        if (!Entry.Field_0x64.empty() && Entry.Field_0x64 == Server.Field_0x64 &&
            Entry.Id == Server.Id)
            return true;
        if (Entry.Port != 0 && Entry.Address == Server.Address && Entry.Port == Server.Port)
            return true;
        return false;
    }
}

// .text:005ac2b0
void CLoginMgr::PingServers()
{
    // 0x005ac2c3: CMP [ESI+0x7c],0 -- CDPClient::PendingConnectHandle (+0x78) read directly.
    if (G_bEnumerateLanHosts != 0 && PendingConnectHandle == 0)
        EnumerateHosts();

    DWORD const Now = timeGetTime();
    // 0x005ac300..0x005ac3b4: an entry that answered the list (+0x3e) and has been silent for
    // more than 5 s shows no ping; past 30 s it is dropped (erase, the next one moves up).
    size_t Index = 0;
    while (Index < Servers.size())
    {
        FServerInfo& Entry = Servers[Index];
        unsigned long const Age = Now - static_cast<unsigned long>(Entry.LastResponseTime);
        if (Entry.Field_0x3e != 0 && Age > 5000)
        {
            bool const bDrop = Age > 30000;
            Entry.Ping = -1;
            Field_0x238 = 1;
            if (bDrop)
            {
                Servers.erase(Servers.begin() + Index);
                Field_0x238 = 1;
                continue;
            }
        }
        ++Index;
    }

    if (Field_0x23c != 0)
        return;

    // 0x005ac3e0..0x005ac4f6: direct servers whose enumeration has run for a second. Under
    // five tries they go to the retry list +0x170; after that their list-server twin (if any)
    // shows no ping. Either way they leave +0x160 and the in-flight count drops.
    Index = 0;
    while (Index < Field_0x160.size())
    {
        FServerInfo& Entry = Field_0x160[Index];
        if (Entry.Field_0x3d == 0 ||
            Now - static_cast<unsigned long>(Entry.LastResponseTime) < 1000)
        {
            ++Index;
            continue;
        }
        if (static_cast<unsigned>(Entry.Field_0x34) < 5)
        {
            Entry.Field_0x3d = 0;
            Field_0x170.push_back(Entry);
        }
        else
        {
            FServerInfo* const Twin =
                Entry.Field_0x64.empty() ? nullptr : FindServerByKey(Servers, Entry);
            if (Twin != nullptr)
            {
                Twin->Ping = -1;
                Field_0x238 = 1;
            }
        }
        Field_0x160.erase(Field_0x160.begin() + Index);
        --Field_0x25c;
    }

    if (Field_0x25c >= 5)
        return;

    // 0x005ac509..0x005ac658: +0x160 drained -> every retry goes back into it (+0x3d cleared),
    // its list-server twin showing "????" (-2); then +0x170 is emptied.
    if (Field_0x160.empty() && !Field_0x170.empty())
    {
        for (size_t Retry = 0; Retry < Field_0x170.size(); ++Retry)
        {
            FServerInfo& Entry = Field_0x170[Retry];
            Entry.Field_0x3d = 0;
            Field_0x160.push_back(Entry);
            FServerInfo* const Twin =
                Entry.Field_0x64.empty() ? nullptr : FindServerByKey(Servers, Entry);
            if (Twin != nullptr)
            {
                Twin->Ping = -2;
                Field_0x238 = 1;
            }
        }
        Field_0x170.clear();
    }

    // 0x005ac676..0x005ac6f9: start an enumeration on every port of each idle entry until five
    // are in flight.
    size_t const End = Field_0x160.size();
    for (Index = 0; Index < End; ++Index)
    {
        FServerInfo& Entry = Field_0x160[Index];
        if (Entry.Field_0x3d != 0)
            continue;
        bool bStarted = false;
        for (size_t PortIndex = 0; PortIndex < Entry.Ports.size(); ++PortIndex)
        {
            if (EnumerateHost(Entry.Address.c_str(), Entry.Ports[PortIndex], 2, 0, 0))
                bStarted = true;
        }
        if (bStarted)
        {
            ++Field_0x25c;
            Entry.Field_0x3d = 1;
        }
        Entry.LastResponseTime = static_cast<int>(timeGetTime());
        ++Entry.Field_0x34;
        if (Field_0x25c >= 5)
            return;
    }
}

// .text:005ac9d0
bool CLoginMgr::PumpServerList()
{
    if (Gun != nullptr && Gun->GetConnection() != nullptr)
    {
        Gun->GetConnection()->DispatchQueue(static_cast<IGunQueueReader*>(this));
        Gun->Update();
    }
    DispatchMsgs(0xffffffff);
    PingServers();

    // DAT_0067ea58 -- the last time the page was told to refresh.
    static DWORD LastRefreshTime = 0;
    bool bRefresh = false;
    if (Field_0x238 != 0 && timeGetTime() - LastRefreshTime >= 1000)
    {
        bRefresh = true;
        Field_0x238 = 0;
        LastRefreshTime = timeGetTime();
    }
    return bRefresh;
}

// .text:005aca40
void CLoginMgr::GetFilteredServers(std::vector<FServerInfo*>& Out, FServerCompare Compare,
                                   ServerFilter const* Filter)
{
    Out.clear();
    size_t const Count = Servers.size();
    Out.reserve(Count);
    for (size_t Index = 0; Index < Count; ++Index)
    {
        FServerInfo* const Server = &Servers[Index];
        if (Filter == nullptr || Filter->Match(*Server))
            Out.push_back(Server);
    }
    // 0x005acb92..0x005acca6: no comparator (EBX) -> unsorted; else VC6 std::sort inlined
    // (FUN_005affc0 / FUN_005b0060 / FUN_005b00d0 / FUN_005afea0), which vc6_sort.h ports so
    // that servers with equal keys keep retail's order.
    if (Compare != nullptr)
        Vc6Sort::Sort(Out.data(), Out.data() + Out.size(), Compare);
}

// .text:005ac010
void CLoginMgr::RemovePendingDirect(FServerInfo const& Server)
{
    for (size_t Index = 0; Index < Field_0x160.size(); ++Index)
    {
        FServerInfo const& Entry = Field_0x160[Index];
        if (Entry.Field_0x3d != 0 && IsSameDirectServer(Entry, Server))
        {
            Field_0x160.erase(Field_0x160.begin() + Index);
            --Field_0x25c;
            return;
        }
    }
}

// .text:005abde0
void CLoginMgr::RemoveRetryDirect(FServerInfo const& Server)
{
    for (size_t Index = 0; Index < Field_0x170.size(); ++Index)
    {
        if (IsSameDirectServer(Field_0x170[Index], Server))
        {
            Field_0x170.erase(Field_0x170.begin() + Index);
            return;
        }
    }
}

// .text:005a9950
bool CLoginMgr::IsKnownLogin(FServerInfo const& Server) const
{
    return Logins.Contains(Server.Id);
}

// .text:005a9970
unsigned int CLoginMgr::GetLoginTime(FServerInfo const& Server)
{
    return Logins.GetTime(Server.Id);
}

// .text:005ab3e0 -- an unbounded wcscpy into the 24-character buffer, as retail.
void CLoginMgr::SetSelectCharacterName(wchar_t const* Name)
{
    wcscpy(SelectCharacterName, Name);
}

// .text:005aad30 (RET 4). wcscmp of each record's Name (+0x204; the _Nullstr when empty)
// against Name. There is no end-of-list test: the step past the tail reads node 0
// (.text:005aad4e / 005aad50), as retail does.
CCharacterSet::CharacterInfo* CLoginMgr::FindCharacterByName(wchar_t const* Name)
{
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
    for (;;)
    {
        Characters.traverse(Node, true);
        if (wcscmp(Node->Data->Name.c_str(), Name) == 0)
        {
            return Node->Data;
        }
    }
}

// .text:005aace0 (RET 4) -- the first record whose key equals CharId (COMMON operator==), else
// null. The walk starts at the list head +0x1b8 and follows each node's next.
CCharacterSet::CharacterInfo* CLoginMgr::FindCharacterById(CHARACTER_ID const& CharId)
{
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
    while (Characters.traverse(Node, true))
    {
        if (Node->Data->Id == CharId)
        {
            return Node->Data;
        }
    }
    return nullptr;
}

// .text:005aad80 -- the first record with +0x200 (bIsRestart) set.
CCharacterSet::CharacterInfo* CLoginMgr::FindRestartCharacter()
{
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
    while (Characters.traverse(Node, true))
    {
        if (Node->Data->bIsRestart != 0)
        {
            return Node->Data;
        }
    }
    return nullptr;
}

// .text:005aadb0 -- the first record with +0x201 (bIsAutoSave) set.
CCharacterSet::CharacterInfo* CLoginMgr::FindAutoSaveCharacter()
{
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
    while (Characters.traverse(Node, true))
    {
        if (Node->Data->bIsAutoSave != 0)
        {
            return Node->Data;
        }
    }
    return nullptr;
}

// .text:005aade0.
bool CLoginMgr::ReloadFactions()
{
    // 0x005aade4..0x005aaeb9: erase(begin, end) inline -- nothing to move down (the copy loop
    // runs from end to end), every record destroyed (FUN_005af7f0), end = begin.
    Factions.clear();
    // 0x005aaebc..0x005aaecc: NewChar::TheDB slot 4 (+0x10) with LAB_005aaee0 and &Factions;
    // its result is not read.
    NewChar::TheDB->ForEachFaction(PushFactionRecord, &Factions);
    return true; // 0x005aaed2
}

// .text:005ab0e0 -- LEA EAX,[ECX+0x1d8]; RET.
std::vector<FNewCharFaction>& CLoginMgr::GetFactions()
{
    return Factions;
}

// .text:005ab0f0. Reached from FUN_005b20a0 (the [Login] auto-login, 005b2146).
bool CLoginMgr::SelectAutoCharacter()
{
    MetaNode<CCharacterSet::CharacterInfo>* Found = nullptr; // EBP
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;  // ESI
    while (Characters.traverse(Node, true))
    {
        if (!SinglePlayer())
        {
            // .text:005ab123..005ab14c: the first record whose name _wcsicmp-matches.
            if (_wcsicmp(Node->Data->Name.c_str(), SelectCharacterName) != 0)
            {
                continue;
            }
            Found = Node;
            break;
        }

        // .text:005ab169: single player. [Login] char = "AutoSave" (DAT_005e65cc) picks the
        // first autosave record.
        if (_wcsicmp(SelectCharacterName, L"AutoSave") == 0)
        {
            if (Node->Data->bIsAutoSave == 0)
            {
                continue;
            }
            Found = Node;
            break;
        }

        // .text:005ab195..005ab1dd: otherwise every record with the name is a candidate; the
        // first one is kept unless it is an autosave and a later non-autosave record matches.
        if (_wcsicmp(Node->Data->Name.c_str(), SelectCharacterName) != 0)
        {
            continue;
        }
        if (Found != nullptr)
        {
            if (Found->Data->bIsAutoSave == 0)
            {
                continue;
            }
            if (Node->Data->bIsAutoSave != 0)
            {
                continue;
            }
        }
        Found = Node;
    }

    // .text:005ab14e..005ab15e / 005ab1e2.
    if (Found == nullptr)
    {
        return false;
    }
    return SelectCharacter(Found->Data);
}

// .text:005ab1f0 (RET 4).
bool CLoginMgr::SelectCharacterById(CHARACTER_ID const& CharId)
{
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
    while (Characters.traverse(Node, true))
    {
        if (Node->Data->Id == CharId) // ??8@YA_NABUCHARACTER_ID@@0@Z
        {
            return SelectCharacter(Node->Data);
        }
    }
    return SelectCharacter(nullptr); // .text:005ab23c
}

// .text:005ab250 (RET 4).
bool CLoginMgr::SelectCharacter(CCharacterSet::CharacterInfo* Info)
{
    if (Info == nullptr)
    {
        return false;
    }
    Field_0x21c = 0;
    G_Server->CharacterSelect(Info->Id, GetPlayerConnectionId()); // +0x50
    WaitForServerResponse(&Field_0x21c, 5.0f, 60.0f);              // FUN_005a89b0
    if (Field_0x21c == 0)
    {
        return false;
    }
    // .text:005ab29c..005ab36d: the record into the player globals, then its name into
    // DAT_006732f0 (character_select_equip.cpp).
    ApplyCharacterSelectEquipFields(Info);
    return true;
}

// .text:005ab380 (RET 4), reached from IClient slot 14 (LAB_005a8240).
void CLoginMgr::OnCharacterSelectResponse(unsigned int const* Response)
{
    Field_0x21c = 1;
    NotifyCharacterSelectComplete(Response);
}

// .text:005ab400 -- std::string::assign(Host, strlen(Host)).
void CLoginMgr::SetListServerHost(char const* Host)
{
    ListServerHost.assign(Host, strlen(Host));
}

// .text:005ab4b0
void CLoginMgr::SetListServerPort(unsigned int Port)
{
    ListServerPort = Port;
}

// .text:005ac2a0 -- `ADD ECX,4; JMP CDPClient::SetGUID`.
void CLoginMgr::SetAppGuid(GUID& Guid)
{
    SetGUID(Guid);
}

// .text:005a82d0 (cdecl; the first argument, the command line, is not read). The server
// module name goes to G_LocalServerDll, the rest into the context.
void CLoginMgr::LoadConfig(char const* IniPath)
{
    GUID AppGuid;
    bool bHaveGuid = false;

    INI_Reader Reader;
    Reader.open(IniPath, false);   // result not tested (.text:005a8328)
    if (!Reader.read_header())
    {
        return;
    }

    do
    {
        if (Reader.is_header("Freelancer"))
        {
            while (Reader.read_value())
            {
                if (Reader.is_value("local_server"))
                {
                    char const* Value = Reader.get_value_string();
                    if (Value != nullptr)
                    {
                        // .text:005a8397-005a83c4: at most 31 characters, then a terminator.
                        size_t Length = strlen(Value);
                        if (Length >= 0x20)
                        {
                            Length = 0x1f;
                        }
                        memcpy(G_LocalServerDll, Value, Length);
                        G_LocalServerDll[Length] = '\0';
                    }
                }
                else if (Reader.is_value("AppGUID"))
                {
                    ParseGUID(Reader.get_value_string(), &AppGuid);
                    bHaveGuid = true;
                }
            }
        }
        else if (Reader.is_header("ListServer"))
        {
            while (Reader.read_value())
            {
                if (Reader.is_value("hostname"))
                {
                    SetListServerHost(Reader.get_value_string());
                }
                else if (Reader.is_value("port"))
                {
                    // get_value_float(0) then _ftol.
                    SetListServerPort(static_cast<unsigned int>(
                        static_cast<long long>(Reader.get_value_float(0))));
                }
            }
        }
        else if (Reader.is_header("Login"))
        {
            while (Reader.read_value())
            {
                if (Reader.is_value("name"))
                {
                    LoginName = std::wstring(Reader.get_value_wstring());
                }
                else if (Reader.is_value("pwd"))
                {
                    LoginPassword = std::wstring(Reader.get_value_wstring());
                }
                else if (Reader.is_value("char"))
                {
                    SetSelectCharacterName(Reader.get_value_wstring());
                }
            }
        }
    } while (Reader.read_header());

    if (bHaveGuid)
    {
        SetAppGuid(AppGuid);        // FUN_005ac2a0
    }
}
