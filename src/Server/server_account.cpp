// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_account.cpp -- CAccount.
// Split out of server_classes.cpp unchanged; the declarations the split files share are in
// server_classes_internal.h.

#include "server_pub.h"
#include "common_net.h"
#include "common_physys.h"
#include "ivp_physics/ivp_real_object.h" // IVP_Real_Object +0x1c/+0xa4 (StarSystem_MpCollisionFilter)
#include "common_cequip.h" // TractorArm
#include "common_casteroid.h" // CmnAsteroid::CAsteroidSystem (SrvAsteroidSystem AddRef/Release/get_sys_id)
#include "common_id.h"
#include "common_server_playerdb.h"
#include "server_savegame.h" // PlayerData_CreateNewCharacter (retail FUN_06d4b790)
#include "server_savefile.h" // Server_AccountDirName / Server_GetAccountsRoot (FUN_06d49f60)
#include "common_ini.h"
#include "common_misc.h"
#include "x86math.h" // I3DMathEngine
#include "common_universe.h"
#include "common_reputation.h"
#include "server_sysdb.h"
#include "server_gameobject.h" // GameObject's +0x154..+0x160 slots (StarSystem::remove_player/unload)
#include "common_cobjdb.h"  // IObjDB::GetZoneDb (SrvAsteroid_UpdateSystem, FUN_06d03480)
#include "DALib.h"          // DALib::Data (SrvAsteroid's GetFileSysPath calls)
#include "readfile.h"       // IFileSystem::GetFileSysPath (+0x84), create_restart_file
#include "server_createinfo.h"
#include "server_characterset.h" // CCharacterSet (FUN_06d499c0)
#include "common_goods.h"    // AmbientScriptProperties (server_basedb.h uses it by value)
#include "server_basedb.h"
#include "rpc_iclient.h"
#include "rpc_iclient_args.h" // FServerSetReputationInfo / FPayloadServerActivateObject (add_player MP walk)
#include "md5hash.h"      // MD5HASH_API: Server exports its implicit members like retail
#include <windows.h>
#include <mmsystem.h>
#include "../include/platform.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <iterator>
#include <list>
#include <map>
#include <set>
#include <new>          // placement new: Observer is constructed into a raw
                        // player-list node, as std::list's _Construct does
#include <io.h>
#include <direct.h>
#include <wchar.h>
#include <stdarg.h>
#include <algorithm>
#include <unordered_map> // PORT(perf-index): the ObjectList blocks' id index (find_by_id)
#include <cassert>       // PORT(perf-index): the id index's _DEBUG cross-check
#include "server_classes_internal.h"

// ===========================================================================
// CAccount class implementation
// ===========================================================================

// ---------------------------------------------------------------------------
// CAccount::CAccount -- retail .text:06d567d0 .. .text:06d5685c, ordinal 4.
// Ported 1:1 2026-09-24 (claude-code:w5-server-06d4):
//   .text:06d567f6 .. 06d567ff  +0x04 name string empty
//   .text:06d5680a .. 06d56813  +0x14 directory string empty
//   .text:06d56821 .. 06d56835  +0x24 list: operator new(0x18) head, self-linked,
//                               size 0 (the std::list member's own ctor here)
//   .text:06d5683b  +0x32 admin = 0     .text:06d5683e  +0x30 locked = 0
//   .text:06d56841  +0x34 client id = 0 .text:06d56844  vftable
//   .text:06d5684a  +0x31 not-banned = 1
// ---------------------------------------------------------------------------
CAccount::CAccount()
    : m_Name()
    , m_DirectoryPath()
    , m_Characters()
    , m_bLocked(false)
    , m_bNotBanned(true)
    , m_bAdmin(false)
    , m_ClientID(0) {
}

// Retail .text:06ce1620 .. 06ce173b (RET 4), traced 2026-09-24
// (claude-code:w5-server-06ce) on the W5-E layout:
//   06ce163c .. 06ce1660  +0x04 name: allocator byte, then assign(src, 0, npos)
//                         (FUN_06ce38a0)
//   06ce1665 .. 06ce1688  +0x14 directory: allocator byte, then assign(src, 0,
//                         npos) (MSVCP60 import .rdata:06d64b70)
//   06ce168e .. 06ce1703  +0x24 list: allocator byte, a new 0x18-byte self-
//                         linked head, size 0, then every source node copied
//                         to the end in order (FUN_06ce5920 copy-constructs
//                         the CiWString), size incremented per node
//   06ce1705 .. 06ce1722  bytes +0x30 / +0x31 / +0x32, dword +0x34
//   06ce1726              the vftable
// Member-wise copy in declaration order is exactly that.
CAccount::CAccount(const CAccount& Other)
    : m_Name(Other.m_Name)
    , m_DirectoryPath(Other.m_DirectoryPath)
    , m_Characters(Other.m_Characters)
    , m_bLocked(Other.m_bLocked)
    , m_bNotBanned(Other.m_bNotBanned)
    , m_bAdmin(Other.m_bAdmin)
    , m_ClientID(Other.m_ClientID) {
}

CAccount::~CAccount() {
}

// Retail .text:06ce1740 .. 06ce1954 (RET 4), traced 2026-09-24
// (claude-code:w5-server-06ce) on the W5-E layout. There is NO whole-object
// `this != &Other` test (the port had one; removed): each member's own
// assignment handles aliasing, then the scalars are copied unconditionally.
//   06ce1743 .. 06ce17f2  +0x04 name = src (basic_string::assign(src, 0, npos)
//                         inlined, its own self-assignment branch at 06ce1764)
//   06ce17f6 .. 06ce18ae  +0x14 directory = src (the same, MSVCP60 imports)
//   06ce18b2 .. 06ce192b  +0x24 list = src, skipped when the two lists are the
//                         same object (06ce18bc): element-wise assign over the
//                         common prefix, then insert the rest or erase the
//                         surplus (FUN_06ce4650 / FUN_06ce3b70)
//   06ce1933 .. 06ce1948  bytes +0x30 / +0x31 / +0x32, dword +0x34
CAccount& CAccount::operator=(const CAccount& Other) {
    m_Name = Other.m_Name;
    m_DirectoryPath = Other.m_DirectoryPath;
    m_Characters = Other.m_Characters;
    m_bLocked = Other.m_bLocked;
    m_bNotBanned = Other.m_bNotBanned;
    m_bAdmin = Other.m_bAdmin;
    m_ClientID = Other.m_ClientID;
    return *this;
}

// ---------------------------------------------------------------------------
// CAccount::InitFromFolder -- retail .text:06d56940 .. .text:06d56bc0 (thiscall,
// RET 4). Ported 1:1 2026-09-24 (claude-code:w5-server-06d4).
//   .text:06d56968  _makepath(path, 0, folder, "name", 0); fopen "rb"
//                   (.rdata:06d6aea8); a null handle skips to the markers
//   .text:06d5699b  n = fread(raw, 2, 0x400, f); decoded[n] = 0 (the store at
//                   .text:06d569a4 lands in the DECODED buffer); fclose
//   .text:06d569b4 .. 06d56a97  key = the account CRC of the marker string
//                   PTR_DAT_06d8d158 (low 16 bits); decoded[i] = raw[i] ^ key
//   .text:06d56aa8 .. 06d56b38  +0x04 name = decoded (assign(ptr, wcslen))
//   .text:06d56b53 .. 06d56b73  "banned" opens -> +0x31 = 0, fclose
//   .text:06d56b8f .. 06d56bad  "admin" opens  -> +0x32 = 1, fclose
// Neither marker writes the flag when its file is absent (the ctor values stand).
// Retail does not store the folder anywhere: +0x14 is written only by
// FUN_06d48e80 at login. The port's m_DirectoryPath store, its null test on the
// path and its 15-character name cut are gone.
// ---------------------------------------------------------------------------
void CAccount::InitFromFolder(const char* FolderPath) {
    char Path[260];
    _makepath(Path, nullptr, FolderPath, "name", nullptr);                  // .text:06d56968
    FILE* File = fopen(Path, "rb");                                         // .text:06d56978
    if (File != nullptr) {
        unsigned short Raw[0x400];
        wchar_t Decoded[0x400 + 1];
        size_t const Count = fread(Raw, 2, 0x400, File);                    // .text:06d5699b
        // Retail's decoded buffer is 0x400 wide chars directly below the raw one,
        // so a full 0x400-char read puts this terminator on raw[0]; one spare
        // slot here keeps that (2 KiB name file) case from overrunning instead.
        Decoded[Count] = L'\0';                                             // .text:06d569a4
        fclose(File);                                                       // .text:06d569ae
        unsigned short const XorKey = AccountNameFileXorKey();              // .text:06d569b4..06d56a6a
        for (size_t I = 0; I < Count; ++I)
            Decoded[I] = static_cast<wchar_t>(Raw[I] ^ XorKey);             // .text:06d56a80
        m_Name.assign(Decoded, wcslen(Decoded));                            // .text:06d56aa8..06d56b38
    }

    _makepath(Path, nullptr, FolderPath, "banned", nullptr);                // .text:06d56b53
    File = fopen(Path, "rb");                                               // .text:06d56b65
    if (File != nullptr) {
        m_bNotBanned = false;                                               // .text:06d56b73
        fclose(File);
    }

    _makepath(Path, nullptr, FolderPath, "admin", nullptr);                 // .text:06d56b8f
    File = fopen(Path, "rb");                                               // .text:06d56b9b
    if (File != nullptr) {
        m_bAdmin = true;                                                    // .text:06d56bad
        fclose(File);
    }
}

// ---------------------------------------------------------------------------
// CAccount::ForceLogout -- retail .text:06d56dd0 .. .text:06d56de3, ordinal 134.
//   .text:06d56dd0  client id at +0x34; zero (JBE) -> return
//   .text:06d56de0  DAT_06d8fb14 vtable+0x44 = IClient slot 17 (id)
// No null test on the interface (the port's was removed 2026-09-24,
// claude-code:w5-server-06d4), and the stored id is not cleared.
// ---------------------------------------------------------------------------
void CAccount::ForceLogout() {
    if (m_ClientID == 0) {
        return;
    }
    extern IClient* g_pClientInterface;
    g_pClientInterface->DisconnectClient(m_ClientID);                       // .text:06d56de0
}

// CAccount::AppendCharacterNames — retail .text:06ce1050, ordinal 60:
// `RET 4`, the whole body (ICF-folded with IServerImpl::NewCharacterInfoReq /
// PushToServer / SetActiveConnection and ContentCallback::scan_state_change).
void CAccount::AppendCharacterNames(std::list<CiWString>& Names) {
    (void)Names;
}

// The port's CAccount::Save / SetAdmin / SetBanned / SetPassword / SetName
// members are gone: none is a retail function (the name/flag bytes are
// written in place by the PlayerDB code that owns them), each was an extra
// Server.dll export, and only SetName had a caller.

// ---------------------------------------------------------------------------
// CAccount::DeleteCharacterFromID -- retail .text:06d56bd0 .. .text:06d56dc9
// (thiscall, RET 4). Ported 1:1 2026-09-24 (claude-code:w5-server-06d4).
// Walks the character list (head at +0x28); for each entry:
//   .text:06d56c12  FUN_06d46370(hash, entry or L"") -- Server_AccountDirName
//   .text:06d56c33  _makepath(file, 0, "" (file[0] = 0 at .text:06d56c0a),
//                   hash, "fl"); .text:06d56c3a _strlwr(hash)
//   .text:06d56c65  FUN_06d5b040: CharId.compare(0, CharId.size(), file,
//                   strlen(file)) -- the ci_char_traits compare; non-zero ->
//                   next entry; the end of the list returns
// On the first match:
//   .text:06d56cad  IsMPServer() ? FUN_06d46370(dir, account name)
//                   : wcstombs(dir, account name, 0x104)
//   .text:06d56cf4  _makepath(path, 0, FUN_06d460f0(), dir, 0)
//   .text:06d56cfe .. 06d56d4b  _wcsdup/_wcslwr/CRC/"%02x-%08x" of the entry
//                   name -- FUN_06d46370 inlined again
//   .text:06d56d74  _makepath(path, 0, path, hash, "fl"); _strlwr(hash);
//                   .text:06d56d89 _unlink(path)
//   .text:06d56d8f .. 06d56dbc  the node unlinked, its string and the node
//                   freed, the size at +0x2c decremented -- list::erase
// The port's empty-id / empty-list early returns were not retail and are gone.
// ---------------------------------------------------------------------------
void CAccount::DeleteCharacterFromID(CiString& CharId) {
    std::list<CiWString>::iterator It = m_Characters.begin();
    for (; It != m_Characters.end(); ++It) {
        char Hash[260];
        char FileName[260];
        // Retail's dir argument is FileName itself with its first byte zeroed
        // (.text:06d56c0a), i.e. an empty directory.
        Server_AccountDirName(Hash, It->c_str());                           // .text:06d56c12
        _makepath(FileName, nullptr, "", Hash, "fl");                       // .text:06d56c33
        _strlwr(Hash);                                                      // .text:06d56c3a
        if (CharId.compare(0, CharId.size(), FileName, strlen(FileName)) != 0) // .text:06d56c65
            continue;

        char DirName[260];
        if (IsMPServer())                                                   // .text:06d56cad
            Server_AccountDirName(DirName, m_Name.c_str());                 // .text:06d56cc0
        else
            wcstombs(DirName, m_Name.c_str(), 0x104);                       // .text:06d56cd1
        char AccountPath[260];
        _makepath(AccountPath, nullptr, Server_GetAccountsRoot(), DirName, nullptr); // .text:06d56cf4
        Server_AccountDirName(Hash, It->c_str());                           // .text:06d56cfe..06d56d52
        // Retail writes this into the same buffer it passes as dir; a second
        // buffer gives the same string without relying on _makepath's overlap
        // behaviour.
        char FilePath[260];
        _makepath(FilePath, nullptr, AccountPath, Hash, "fl");              // .text:06d56d74
        _strlwr(Hash);                                                      // .text:06d56d7b
        _unlink(FilePath);                                                  // .text:06d56d89
        m_Characters.erase(It);                                             // .text:06d56d8f..06d56dbc
        return;
    }
}
