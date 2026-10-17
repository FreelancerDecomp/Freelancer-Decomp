// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/common_net.h"
#include "../include/common_physys.h"
#include "../include/common_ivp_real_object.h" // IVP_Real_Object +0x1c/+0xa4 (StarSystem_MpCollisionFilter)
#include "../include/common_cequip.h" // TractorArm
#include "../include/common_casteroid.h" // CmnAsteroid::CAsteroidSystem (SrvAsteroidSystem AddRef/Release/get_sys_id)
#include "../include/common_id.h"
#include "../include/common_server_playerdb.h"
#include "../include/server_savegame.h" // PlayerData_CreateNewCharacter (retail FUN_06d4b790)
#include "../include/server_savefile.h" // Server_AccountDirName / Server_GetAccountsRoot (FUN_06d49f60)
#include "../include/common_ini.h"
#include "../include/common_misc.h"
#include "../include/x86math.h" // I3DMathEngine
#include "../include/common_universe.h"
#include "../include/common_reputation.h"
#include "../include/server_sysdb.h"
#include "../include/server_gameobject.h" // GameObject's +0x154..+0x160 slots (StarSystem::remove_player/unload)
#include "../include/common_cobjdb.h"  // IObjDB::GetZoneDb (SrvAsteroid_UpdateSystem, FUN_06d03480)
#include "../include/DALib.h"          // DALib::Data (SrvAsteroid's GetFileSysPath calls)
#include "../include/readfile.h"       // IFileSystem::GetFileSysPath (+0x84), create_restart_file
#include "../include/server_createinfo.h"
#include "../include/server_characterset.h" // CCharacterSet (FUN_06d499c0)
#include "../include/common_goods.h"    // AmbientScriptProperties (server_basedb.h uses it by value)
#include "../include/server_basedb.h"
#include "../include/rpc_iclient.h"
#include "../include/rpc_iclient_args.h" // FServerSetReputationInfo / FPayloadServerActivateObject (add_player MP walk)
#include "../include/md5hash.h"      // MD5HASH_API: Server exports its implicit members like retail
#include <windows.h>
#include <mmsystem.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
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

#pragma comment(lib, "winmm.lib")

// Forward declaration for IClient (defined in rpc_iclient.h)
class IClient;

// Retail FUN_06d43650(msgId, payload) == pub::Controller::_SendMessage(&0u, msgId, payload),
// i.e. a BROADCAST to every loaded controller's slot 0 (retail 0x06d44080). The port used
// to route it through Common.dll's Server_SendMessage, whose handler table is never
// populated, so all of it was dead. Server_ControllerDispatch (IServerImpl.cpp, declared in
// common_server_controller.h) is the real walk.

// Retail reaches IServerImpl members through the exported `Server` object
// (e.g. .text:06d50b03 `MOV ECX,.data:Server` / .text:06d50b08 `CALL
// IServerImpl::TerminateTrade`). IServerImpl's body lives in IServerImpl.cpp,
// so this file takes the same object from GetServerInterface() (retail
// 0x06d44760 returns &Server) and GetChatServerInterface() (Server + 4); the
// dynamic type is IServerImpl, so each call lands on the member retail calls.

// The IClient / IChatClient halves of rpclocal's Client object (retail
// .data:DAT_06d8fb14 and .data:DAT_06d8fb18). Both are defined in IServerImpl.cpp;
// CPlayerGroup's senders reach for them at file scope.
extern IClient* g_pClientInterface;
extern IChatClient* g_pChatClientInterface;

// Retail .data:DAT_06d8f5dc — the group-control chat channel. Its CRT initializer
// 0x06d189c0 writes word 4 AND `MOV byte ptr [DAT_06d8f5dc+2],0x1`, so the CHAT_ID
// is 0x00010004, not 4 (the old value here dropped the channel byte). It belongs
// to FLChatServer.cpp's block of chat ids and is defined in server_chat.cpp.
extern const unsigned int GGroupChatSourceId;
// Retail .data:DAT_06d9042c — the "channel" CPlayerGroup::DeliverChat stamps on
// every copy. CORRECTED: it IS written, by the CRT initializer 0x06d5b190
// (`MOV word ptr [DAT_06d9042c],0x3` / `MOV byte ptr [DAT_06d9042c+2],0x1`),
// i.e. 0x00010003 -- the group chat channel, the same value as SubmitChat's
// DAT_06d8f5e0.
static const unsigned int GDeliverChatSourceId = 0x00010003u;

// FUN_06d19180: builds a CHAT_ID out of a player/session id -- word0 = (ushort)Id,
// bytes 2 and 3 zeroed. Every IChatClient call in this file goes through it.
static unsigned int MakeChatId(unsigned int Id) { return Id & 0xffff; }

// The 8-byte block FUN_06d190d0 (0x06d190d0) builds on its own stack before
// handing it to IChatClient::SendCommonChatMsg: [0] = the message type it was called with
// (0x06d190d3), [4] = its ushort argument zero-extended to a dword
// (0x06d190d7 MOVZX / 0x06d190ec MOV). The literal 8 pushed at 0x06d190ea is
// this struct's size.
struct FGroupMsgPayload {
    unsigned int MsgType;
    unsigned int Param;
};

// FUN_06d19110 (cdecl, plain RET), traced .text:06d19110 .. .text:06d1917e:
//   slot = Players.traverse_active(0); none -> return
//   EBX = MOVZX word [param_2] (read once, before the loop); EDI = param_1
//   per slot: CHAT_ID{FUN_06d191a0(slot) = slot+0x348} (FUN_06d19180, written
//   into param_2's own stack slot), then IChatClient slot 0 through
//   DAT_06d8fb18 with no null test: (target, DAT_06d8f5dc, 8, &{EDI, EBX});
//   slot = Players.traverse_active(slot).
void Server_BroadcastGroupControl(unsigned int MsgType, unsigned int Param) {
    PlayerData* Slot = Players.traverse_active(nullptr);
    if (Slot == nullptr)
        return;
    unsigned int const Word = Param & 0xffff;
    do {
        unsigned int const Target = MakeChatId(Slot->client_id);
        FGroupMsgPayload Payload;
        Payload.MsgType = MsgType;
        Payload.Param = Word;
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(Target), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
        Slot = Players.traverse_active(Slot);
    } while (Slot != nullptr);
}

// ===========================================================================
// PlayerDB class implementation
// ===========================================================================

// ---------------------------------------------------------------------------
// The one and only PlayerDB. Retail: `?Players@@3VPlayerDB@@A`, Server.dll
// ordinal 352, .data:06d90260, constructed by the CRT initializer at
// 0x06d50b10 and referenced 21 times in the listing. Everything that walks the
// player table loads its first member:
//     06ce900d  8b156002d906  MOV  EDX,dword ptr [.data:Players]
//     06ce9013  69c918040000  IMUL ECX,ECX,0x418
//     06ce9019  399c11480...  CMP  dword ptr [ECX + EDX*0x1 + 0x348],EBX
// and everything that calls a method loads its address:
//     06ce904b  b96002d906    MOV  ECX,.data:Players
//     06ce9057  e8348affff    CALL PlayerDB::operator[]
// The port used to have this object AND a second table (`g_PlayersDB[64]` in
// Common.dll), with the PlayerDB methods writing one and every IServerImpl
// handler writing the other. This is now the single store.
//
// The object itself is defined near the bottom of this file, in the
// "Missing global exports" block; it is declared in common_server.h.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// PlayerData::PlayerData -- retail FUN_06d494f0 (.text:06d494f0 .. .text:06d49751,
// fastcall/thiscall on the slot, returns it). PlayerDB::init's __vec_new helper
// FUN_06d601a8 runs it over every element (PUSH FUN_06d494f0 at .text:06d51124).
// Traced term by term 2026-09-24 (claude-code:w5-server-06d4). Retail stores, in
// order (EBX = 0, EBP = 1.0f):
//   +0x050 map ctor FUN_06d576d0              saved_characters
//   +0x064 byte 0                             selected_character[0]
//   +0x264 0 / +0x268 0                       ship_archetype / relative_health
//   +0x26c list ctor (COMMON list<> import)   collision_groups
//   +0x278 EquipDescList ctor                 equipment
//   +0x284 0                                  rank_level
//   +0x28c..+0x298 0, +0x2bc 0                com_costume.PartIds[0..3], .AccessoryCount
//   +0x2c0 0 / byte +0x2c4 0                  voice_length / voice[0]
//   +0x2e4..+0x2f0 0, +0x314 0                costume.PartIds[0..3], .AccessoryCount
//   +0x318 0xffffffff                         rep_vibe
//   +0x320 0 / +0x324 0                       total_earned / equip_snapshot_arch
//   +0x328 list ctor                          equip_snapshot
//   +0x348 0 / byte +0x34c 0                  client_id / bCheatReported
//   +0x380 basic_string ctor (MSVCP60)        weapon_group_name
//   +0x390 list ctor FUN_06d10aa0             mission_log
//   +0x3a0 tree ctor FUN_06d15ef0             visit_map
//   +0x3b8..+0x3c0 0                          visited_states (empty vector)
//   +0x3c4 1.0f                               difficulty_scale
//   +0x3c8 SubObjectID::EquipIdMaker ctor     equip_id_maker (0x21)
//   +0x3cc 0 / +0x3d0 0                       trade_offer / trade_request_partner
//   +0x3d4 list ctor FUN_06cfc340             inbound_trade_requests
//   +0x3e4 / +0x3e8 / +0x3f4 0                ship_object_id / created_ship_id / entered_base_id
//   +0x400 / +0x404 / +0x408 / +0x40c 0       account_ptr / group_ptr / group_mission_id / _set_by
//   +0x414 ID_String::clear                   starting_ring
//   +0x350..+0x358 0, +0x35c/+0x36c/+0x37c 1.0f, the other six 0
//                                             initial_pos / initial_ornt (identity)
//   +0x344 0                                  save_failure_count
// Everything else retail leaves as whatever operator new returned: AccountName
// (+0x00), money_needed_to_next_rank (+0x288), both costumes' accessory ids, cash
// (+0x31c), the three mission counters (+0x334..+0x33c), bSkipNextAutosave
// (+0x340), interface_state (+0x39c), system_id (+0x3e0), current_base (+0x3ec),
// last_base_id (+0x3f0), current_location (+0x3f8), Field_0x3FC and
// exited_base_id (+0x410). There is no retail value to reproduce for those; the
// port zeroes them so a fresh slot is deterministic (the same convention
// FSavedCharacter's constructor documents). The old PlayerData_Reset's invented
// values (cash 1000, hull/shield 100.0f, zero orientation, relative_health 1.0f)
// are gone.
// ---------------------------------------------------------------------------
PlayerData::PlayerData()
    : current_location(0), current_base(0), cash(0), system_id(0), ship_archetype(0),
      entered_base_id(0), equipment(), collision_groups(), equip_snapshot_arch(0),
      equip_snapshot(), voice_length(0), rank_level(0), money_needed_to_next_rank(0),
      difficulty_scale(1.0f), num_kills(0), num_mission_successes(0),
      num_mission_failures(0), rep_vibe(-1), relative_health(0.0f), group_ptr(nullptr),
      group_mission_id(0), group_mission_set_by(0), total_earned(0), client_id(0),
      bCheatReported(0), bSkipNextAutosave(0), save_failure_count(0), starting_ring(0),
      selected_character(), weapon_group_name(), interface_state(0), mission_log(),
      visit_map(), visited_states(), equip_id_maker(), trade_offer(nullptr),
      trade_request_partner(0), inbound_trade_requests(), ship_object_id(0),
      last_base_id(0), account_ptr(nullptr), saved_characters(), created_ship_id(0),
      Field_0x3FC(0), exited_base_id(0) {
    // +0x064: `MOV byte ptr [ESI+0x64],BL` -- the inlined CHARACTER_ID ctor;
    // selected_character() in the initializer list above is that call.
    // +0x28c..+0x2c4 and +0x2e4..+0x314: the part ids and the accessory count.
    for (int Part = 0; Part < 4; ++Part) {
        com_costume.PartIds[Part] = 0;
        costume.PartIds[Part] = 0;
    }
    com_costume.AccessoryCount = 0;
    costume.AccessoryCount = 0;
    voice[0] = '\0';
    // Not written by retail (see above): zeroed for determinism only.
    for (int Accessory = 0; Accessory < 8; ++Accessory) {
        com_costume.AccessoryIds[Accessory] = 0;
        costume.AccessoryIds[Accessory] = 0;
    }
    for (size_t Index = 1; Index < sizeof(voice); ++Index)
        voice[Index] = '\0';
    for (size_t Index = 0; Index < sizeof(AccountName) / sizeof(AccountName[0]); ++Index)
        AccountName[Index] = L'\0';
    // +0x350..+0x37c: zero Vector, identity 3x3.
    for (int Axis = 0; Axis < 3; ++Axis)
        initial_pos[Axis] = 0.0f;
    for (int Cell = 0; Cell < 9; ++Cell)
        initial_ornt[Cell] = (Cell % 4 == 0) ? 1.0f : 0.0f;
}

// ---------------------------------------------------------------------------
// PlayerData::~PlayerData -- retail FUN_06d50e90 (.text:06d50e90). The first call
// is the slot teardown FUN_06d49230 (.text:06d50eb9); what follows is the member
// destruction the compiler emits here as well (the +0x3d4 list at .text:06d50eda,
// the +0x3b8 vector buffer, FUN_06d56fd0 on the +0x3a0 tree, the +0x390 list, the
// +0x380 string, the +0x328 / +0x278 / +0x26c lists and the +0x50 map).
// ---------------------------------------------------------------------------
static void PlayerData_Teardown(PlayerData& Slot);

PlayerData::~PlayerData() {
    PlayerData_Teardown(*this);
}

// ---------------------------------------------------------------------------
// The three lookup helpers the rest of Server.dll calls. They used to be
// Common.dll exports over `g_PlayersDB[]`; they are plain forwarders onto
// `Players` now. Retail has no equivalents -- its call sites inline the
// `[.data:Players]` walk -- so they keep port-native names.
//
// PlayerDB_GetPlayer keeps the old contract: null for an out-of-range session
// AND null for a slot that is not logged in, so the ~190 `if (!p) return;`
// guards in IServerImpl.cpp and server_pub.cpp stay correct. "Logged in" is
// now retail's own test, PlayerData::client_id (+0x348) != 0, the same one
// PlayerDB::is_valid (0x06ce1a00) uses, rather than the port's `active` bool.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// CiWString (basic_string<wchar_t, ci_wchar_traits>) and std::wstring differ only
// in their traits type, so the few helpers that take a std::wstring get a copy.
// ---------------------------------------------------------------------------
static std::wstring CiToWide(const CiWString& Str) {
    return std::wstring(Str.c_str(), Str.size());
}

PlayerSlot* __cdecl PlayerDB_GetPlayer(unsigned int session_id) {
    if (!Players.is_valid(session_id)) {
        return nullptr;
    }
    return &Players[session_id];
}

unsigned int __cdecl PlayerDB_GetMaxPlayers() {
    return Players.GetMaxPlayerCount();
}

bool __cdecl PlayerDB_AllowsNewPlayers() {
    return Players.m_bAllowNewPlayers;   // friend of PlayerDB; [Players + 0x0c]
}

// ---------------------------------------------------------------------------
// PlayerDB::PlayerDB — retail RVA 0x70B10, ordinal 15
// Retail constructs the CPlayerAccount at this+0x10, an empty MOTD string at
// this+0x20, the two empty containers at this+0x30 and this+0x44, and finally
// zeroes this+0x00 (array base), this+0x1c (array end), this+0x04 (MaxPlayers)
// and this+0x08 (ServerID). ServerID starts at ZERO, not 1.
// ---------------------------------------------------------------------------
PlayerDB::PlayerDB() {
    m_PlayerArray = nullptr;
    m_MaxPlayers = 0;
    m_ServerID = 0;
    m_bAllowNewPlayers = false;
    m_PlayerArrayEnd = nullptr;
}

// ---------------------------------------------------------------------------
// PlayerDB::PlayerDB(const PlayerDB&) — retail RVA 0x1AE0, ordinal 14
// Exported but never called from inside Server.dll (the only XREFs on
// 0x06ce1ae0 are the entry point and the export table at 0x06d8006c) — the
// familiar exported-but-dead special member.
// Retail copies the slot array pointer, MaxPlayers, ServerID and the array-end
// pointer BY VALUE — it is a SHALLOW copy, no reallocation — bitwise-copies the
// CPlayerAccount at +0x10 (vtable written fresh, both hash pointers shared),
// then deep-copies the MOTD string and both containers.
// ---------------------------------------------------------------------------
PlayerDB::PlayerDB(const PlayerDB& Other)
    // Retail bitwise-copies the CPlayerAccount at +0x10 -- both hash pointers
    // are SHARED with the source, the vtable is written fresh. CPlayerAccount's
    // own copy ctor is what reproduces that.
    : m_ServerAccount(Other.m_ServerAccount) {
    m_PlayerArray = Other.m_PlayerArray;
    m_MaxPlayers = Other.m_MaxPlayers;
    m_ServerID = Other.m_ServerID;
    m_bAllowNewPlayers = Other.m_bAllowNewPlayers;
    m_PlayerArrayEnd = Other.m_PlayerArrayEnd;
    m_MOTD = Other.m_MOTD;
    m_CharacterIDMap = Other.m_CharacterIDMap;
    m_AccountNameMap = Other.m_AccountNameMap;
}

// ---------------------------------------------------------------------------
// PlayerDB::~PlayerDB — retail RVA 0x70C30, ordinal 21
// Retail: destroy the slot array (FUN_06d51060) and zero +0x00/+0x1c/+0x04;
// then walk the account-name container at this+0x48 and invoke the scalar
// deleting destructor on the CAccount* stored at each node+0x1c — PlayerDB owns
// those CAccount objects; then tear both containers down, release the MOTD
// string and destroy the CPlayerAccount at this+0x10.
// ---------------------------------------------------------------------------
PlayerDB::~PlayerDB() {
    free();

    // The name map owns its CAccount objects (retail deletes them here).
    std::map<CiWString, CAccount*>::iterator It = m_AccountNameMap.begin();
    for (; It != m_AccountNameMap.end(); ++It) {
        delete It->second;
        It->second = nullptr;
    }
    m_AccountNameMap.clear();
    // The character-id map at this+0x30 only borrows the same pointers; retail
    // does not delete through it.
    m_CharacterIDMap.clear();
}

// ---------------------------------------------------------------------------
// PlayerDB::operator= — retail RVA 0x1C50, ordinal 31
// Also exported but never called internally (XREFs on 0x06ce1c50: entry point
// and the export table only). Retail does NOT free the existing slot array: it
// overwrites the array pointer, MaxPlayers, ServerID and the array-end pointer
// with the source's, copies the two CPlayerAccount hash pointers (leaving the
// vtable alone), then assigns the MOTD string and the two containers, each
// guarded by a self-assignment check.
// ---------------------------------------------------------------------------
PlayerDB& PlayerDB::operator=(const PlayerDB& Other) {
    m_PlayerArray = Other.m_PlayerArray;
    m_MaxPlayers = Other.m_MaxPlayers;
    m_ServerID = Other.m_ServerID;
    m_bAllowNewPlayers = Other.m_bAllowNewPlayers;
    m_PlayerArrayEnd = Other.m_PlayerArrayEnd;
    if (this != &Other) {
        // Retail copies the two CPlayerAccount hash pointers and leaves the
        // vtable alone. The implicit copy assignment does exactly that: it
        // copies HashName/HashVerify and, because the class is polymorphic,
        // never touches the vptr.
        m_ServerAccount = Other.m_ServerAccount;
        m_MOTD = Other.m_MOTD;
        m_CharacterIDMap = Other.m_CharacterIDMap;
        m_AccountNameMap = Other.m_AccountNameMap;
    }
    return *this;
}

// The translation-unit singleton `GServerAccount` that used to stand in for
// retail's PlayerDB+0x10 is gone: PlayerDB::m_ServerAccount is a real
// CPlayerAccount member now (see common_server.h). It only ever existed to keep
// common_net.h out of common_server.h's include graph, and that include has
// been verified acyclic.

// Retail keeps the accounts root in the global char buffer at DAT_06d90158.
static char GAccountRootDir[260] = { 0 };

// ---------------------------------------------------------------------------
// Server-account bootstrap, lifted from retail PlayerDB::init (0x06d510c0).
// Builds <UserData>\Accts[\MultiPlayer], then for an MP server opens
// <root>\accounts.cfg. If it does not exist, one is created: two random dwords
// (the second becomes a provisional server id) followed by the 0x24-byte
// account-name hash and the 0x24-byte account-signature hash of a freshly
// generated "SERVER" account. The file is then read back, the server id is
// re-derived by sscanf("%x") from the *signature* string (retail discards the
// dword it just wrote), the account is reloaded from the two strings and
// verified; a failing verify unlinks the file and retries.
// ---------------------------------------------------------------------------
static void InitServerAccount(CPlayerAccount& ServerAccount, unsigned int& OutServerID) {
    char UserDataPath[260];
    GetUserDataPath(UserDataPath);
    const char* SubDir = IsMPServer() ? "Accts\\MultiPlayer" : "Accts";
    _makepath_s(GAccountRootDir, sizeof(GAccountRootDir), nullptr, UserDataPath, SubDir, nullptr);

    if (!IsMPServer()) {
        return;
    }

    BuildDirectory(GAccountRootDir);

    char CfgPath[260];
    _makepath_s(CfgPath, sizeof(CfgPath), nullptr, GAccountRootDir, "accounts", "cfg");

    // Retail loops forever here; bound the retry count instead.
    for (int Attempt = 0; Attempt < 4; ++Attempt) {
        FILE* File = nullptr;
        if (fopen_s(&File, CfgPath, "rb") != 0 || File == nullptr) {
            if (fopen_s(&File, CfgPath, "wb") != 0 || File == nullptr) {
                return;
            }
            srand(timeGetTime());
            unsigned int Salt = ((unsigned int)rand() << 16) | ((unsigned int)rand() & 0xffff);
            unsigned int IdA = ((unsigned int)rand() << 16) | ((unsigned int)rand() & 0xffff);
            unsigned int IdB = ((unsigned int)rand() << 16) | ((unsigned int)rand() & 0xffff);
            OutServerID = IdB ^ IdA;
            fwrite(&Salt, 4, 1, File);
            fwrite(&OutServerID, 4, 1, File);
            ServerAccount.GenerateAccount("SERVER");
            const char* NameHash = ServerAccount.GetAccountNameChar();
            const char* SigHash = ServerAccount.GetAccountNameSigChar();
            fwrite(NameHash, 0x24, 1, File);
            fwrite(SigHash, 0x24, 1, File);
            fclose(File);
            continue;
        }

        unsigned int SaltIn = 0;
        unsigned int IdIn = 0;
        char NameHash[0x25];
        char SigHash[0x25];
        memset(NameHash, 0, sizeof(NameHash));
        memset(SigHash, 0, sizeof(SigHash));
        fread(&SaltIn, 4, 1, File);
        fread(&IdIn, 4, 1, File);
        fread(NameHash, 0x24, 1, File);
        fread(SigHash, 0x24, 1, File);
        fclose(File);

        // Retail: sscanf(SigHash, "%x", this + 8) — the server id is the leading
        // hex run of the signature hash, not the dword stored above.
        sscanf_s(SigHash, "%x", &OutServerID);
        ServerAccount.LoadAccountFromStrings(NameHash, SigHash);
        if (ServerAccount.VerifyAccount()) {
            return;
        }
        _unlink(CfgPath);
    }
}

// ---------------------------------------------------------------------------
// GCheatMessage / LoadCheatMessage — retail .data:06d8d160 and FUN_06d46150.
//
// FUN_06d46150 is the very last thing PlayerDB::init does (CALL at 0x06d5188f).
// The previous note on this item said "nothing is known about it beyond its
// call site". It is the cheat-warning string override, and it is MP-only:
//
//     if (SinglePlayer()) return;
//     GetModuleFileNameA(0, path, 0x104);
//     _splitpath(path, drive, dir, 0, 0);
//     _makepath(path, drive, dir, "CheatMessage", "txt");     ; 0x06d46198
//     f = fopen(path, "rb");
//     if (!f) return;
//     n = fread(buf, 2, 0x50, f);                              ; 0x50 WIDE chars
//     if (n != 0) {
//         memcpy(u_Possible_cheating_detected_, buf, n * 2);   ; REP MOVSD+MOVSB
//         if (n < 0x4f) { dest[n] = 0; fclose(f); return; }
//         dest[0x4f] = 0;                                      ; _DAT_06d8d1fe
//     }
//     fclose(f);
//
// So the file is read as raw UTF-16, not as text, and the destination is a
// wchar_t[0x50] whose shipped default is L"Possible cheating detected: "
// (.data:06d8d160, referenced from 0x06d461fc and 0x06d4f753).
//
// TODO(port): the CONSUMER is not ported. Retail's only other reference is at
// 0x06d4f753, inside the anti-cheat report path that also FDUMPs
// "LOG: Possible cheating detected (...)" at 0x06d4f6c0: it builds a rich-text
// message out of COMMON.DLL TextNode/StyleNode nodes (0x06d4f6f0 onwards) with
// this string as the leading run, and pushes it to the client. Until that is
// ported nothing reads GCheatMessage.
// ---------------------------------------------------------------------------
static wchar_t GCheatMessage[0x50] = L"Possible cheating detected: ";

static void LoadCheatMessage() {
    if (SinglePlayer()) {
        return;
    }

    char ModulePath[260];
    GetModuleFileNameA(nullptr, ModulePath, sizeof(ModulePath));

    char Drive[4], Dir[260];
    _splitpath(ModulePath, Drive, Dir, nullptr, nullptr);

    char MessagePath[260];
    _makepath(MessagePath, Drive, Dir, "CheatMessage", "txt");

    FILE* File = fopen(MessagePath, "rb");
    if (File == nullptr) {
        return;
    }

    wchar_t Buffer[0x50];
    size_t Count = fread(Buffer, 2, 0x50, File);
    if (Count != 0) {
        memcpy(GCheatMessage, Buffer, Count * 2);
        if (Count < 0x4f) {
            GCheatMessage[Count] = 0;
            fclose(File);
            return;
        }
        GCheatMessage[0x4f] = 0;
    }
    fclose(File);
}

// ---------------------------------------------------------------------------
// PlayerData_ReportCheat — retail FUN_06d4f570(PlayerData* slot, int Reason),
// 0x06d4f570. THE consumer of GCheatMessage: the previous wave's note said
// "until that is ported nothing reads GCheatMessage", and this is it.
//
// Four things happen, in this order:
//
//  (1) Reputation::Vibe::GetName(&slot->rep_vibe, &fmt1, &fmt2, &name); when it
//      returns -2 the name falls back to the literal L">>>BAD   NAME<<<"
//      (.data:06d8d40c -- three spaces, not one).
//      FDUMP(0x100001, "LOG: Possible cheating detected (%s) by %s (%s)!",
//            GCheatReason[Reason], characterName, accountName).
//      Both %s arguments are NARROW copies the retail code builds one character
//      at a time with std::string::append(1, (char)wide) -- a straight truncating
//      cast, no MultiByteToWideChar. The character name comes from the vibe, the
//      account name from the slot's own AccountName at +0x00 (retail walks
//      `this` two bytes at a time from offset 0).
//
//  (2) an RDL message: StyleNode(0xd681), then TextNode(GCheatMessage), then
//      TextNode(characterName), each cloned into the display list through its
//      vtable +0x1c (RDLNode::Clone).
//
//  (3) BinaryRDLWriter::write_buffer(rdl, GCheatChatBuffer, 0x200, &Size), and on
//      success IServerImpl::SubmitChat(CHAT_ID(0), Size, GCheatChatBuffer,
//      DAT_06d902c8, -1). Note the second argument: SubmitChat's mangled export
//      is ?SubmitChat@IServerImpl@@UAEXUCHAT_ID@@KPBX0H@Z, and the `unsigned long`
//      carries the BYTE COUNT, not a message type. DAT_06d902c8 is a CHAT_ID
//      global that FUN_06d46020 sets exactly once, as `word[0] = 0` /
//      `byte[2] = 1` -- i.e. 0x00010000.
//
//  (4) a line appended to <UserDataPath>\Accts[\MultiPlayer]\cheaters.log.
//
// BinaryRDLWriter::write_buffer is ported (src/Common/common_rdlio.cpp, retail
// 0x0632a970); the three nodes encode as a Type 6 record and two Type 2 records.
//
// CORRECTED 2026-09-17 (wave 8). This block used to say the caller was
// FUN_06d4cc20 and that it gated on slot+0x1a6 (an "already reported" latch)
// and slot+0x1f6. Every part of that is wrong on the bytes:
//
//   * PlayerData_ReportCheat has exactly ONE caller in the whole listing, and
//     it is .text:06d4cd88, inside FUN_06d4ccd0 -- not FUN_06d4cc20.
//   * FUN_06d4cc20 has nothing to do with cheating. It is a small RECURSIVE
//     string formatter over a linked list of equipment records: it counts the
//     list (06d4cc35..06d4cc3a), sprintf's "%u, %u" (.rdata:06d6b628) and then
//     ", %u, %u, %u" (.rdata:06d6b618) per node from node+0x04 / node+0x08 /
//     node+0x0a, and recurses into itself at .text:06d4cca9 when a node's
//     word at +0x08 equals 0x21. It never touches +0x1a6 or +0x1f6, and it
//     never mentions a PlayerData slot at all.
//   * The real latch is at +0x34C, not +0x1a6: `CMP byte ptr [EBX+0x34c],0x1`
//     at .text:06d4cd0b and `MOV byte ptr [EBX+0x34c],0x1` at .text:06d4cd8d.
//     The other gate is +0x3EC (the current base id), not +0x1f6:
//     `MOV EAX,[EBX+0x3ec] / TEST EAX,EAX` at .text:06d4cd2d.
//
// The gate is ported below as PlayerData_CheckForCheats.
// ---------------------------------------------------------------------------
namespace {
    // .data:06d8d430, five entries (the table's own XREFs are 06d4f6b3 and
    // 06d4f9c9). Entry 4's string starts one byte after the ':' Ghidra folded
    // into it at .rdata:06d6adaf.
    const char* const GCheatReason[5] = {
        "None",
        "Ship Related",
        "Ship or Equipment not sold on base",
        "Equipment exceeds ship specs",
        "Ship capacity exceeded"
    };

    // .data:06d8fb38, the 0x200-byte scratch the writer fills and SubmitChat sends.
    unsigned char GCheatChatBuffer[0x200];

    // .data:06d902c8 -- word 0, byte[2] = 1 (FUN_06d46020 at 0x06d46020/0x06d46029).
    const unsigned int GCheatChatTargetId = 0x00010000u;

    // Retail's narrow-copy loop: `append(1, (char)wideChar)` per character.
    std::string NarrowCopy(const wchar_t* Text) {
        std::string Out;
        // (w4-server-guards) no null test: both inlined loops read the first
        // character straight away (06d4f621 `CMP word ptr [ESI],0`,
        // 06d4f65b `CMP word ptr [EDI],0`).
        while (*Text != L'\0') {
            Out.append(1, static_cast<char>(*Text));
            ++Text;
        }
        return Out;
    }

    const wchar_t GCheatBadName[] = L">>>BAD   NAME<<<";
}

// Server.dll's own RDL append: every caller in the module does
//     clone = node.Clone()                (vtable +0x1c)
//     FUN_06d1b1b0(&rdl, &out, rdl.NodeList, &clone)   // list::insert(end(), clone)
//     FUN_06d19f00(&clone)                // delete the caller's clone
// and FUN_06d1b1b0 (.text:06d1b1b0, `RET 0xc`) allocates the 0xC-byte node
// (next = sentinel, prev = sentinel->prev, relinks both neighbours), builds the
// stored value through FUN_06d1b4a0 -- which clones AGAIN (`CALL [EDX+0x1c]` at
// .text:06d1b4d3) -- and bumps the count at +0x08. Net effect: the list gains one
// clone at its tail and the caller's temporary is destroyed; this does the same
// with a single Clone. Common.dll keeps its own identical copy, as retail does.
// Retail tests none of: a null sentinel, a null clone, a failed node allocation
// (.text:06d1b1cb writes the new node unconditionally), so neither does this.
// Shared with server_chat.cpp (FLChatServer.cpp's header/echo/redirect lists).
void Server_RdlAppend(RenderDisplayList& Rdl, const RDLNode& Node) {
    RDLNode* Copy = Node.Clone();
    void** Sentinel = (void**)Rdl.NodeList;
    void** Tail = static_cast<void**>(Sentinel[1]);
    void** Entry = static_cast<void**>(malloc(0xC));
    Entry[0] = Sentinel;   // next
    Entry[1] = Tail;       // prev
    Entry[2] = Copy;
    Sentinel[1] = Entry;
    Tail[0] = Entry;
    Rdl.Count++;
}

void PlayerData_ReportCheat(PlayerData& Slot, int Reason) {
    // (1) resolve the character name off the reputation vibe.
    const wchar_t* CharacterNameWide = GCheatBadName;
    {
        FmtStr NameA;
        FmtStr NameB;
        const wchar_t* Resolved = GCheatBadName;
        int Result = ::Reputation::Vibe::GetName(Slot.rep_vibe, NameA, NameB, Resolved);
        // Retail only re-forces the fallback on -2; any other failure code
        // leaves whatever GetName wrote into the out-parameter in place.
        CharacterNameWide = (Result == -2) ? GCheatBadName : Resolved;
    }

    const std::string CharacterName = NarrowCopy(CharacterNameWide);
    const std::string AccountName = NarrowCopy(Slot.AccountName);

    // (w4-server-guards) retail 06d4f6b3 `MOV EDX,[ECX*4 + .data:06d8d430]` indexes the
    // reason table directly -- no range clamp -- and 06d4f6c6/06d4f6cb load the
    // DACOM FDUMP import and CALL it with no null test.
    const char* ReasonText = GCheatReason[Reason];

    FDUMP(0x100001, "LOG: Possible cheating detected (%s) by %s (%s)!",
          ReasonText,
          CharacterName.empty() ? "" : CharacterName.c_str(),
          AccountName.empty() ? "" : AccountName.c_str());

    // (2) build the rich-text message.
    RenderDisplayList Rdl;
    {
        StyleNode Style(static_cast<unsigned short>(0xd681));
        Server_RdlAppend(Rdl, Style);
    }
    {
        TextNode Prefix(GCheatMessage, -1);
        Server_RdlAppend(Rdl, Prefix);
    }
    {
        // .text:06d4f7b5 .. .text:06d4f830 -- retail does NOT reuse the pointer
        // from (1): it builds two fresh FmtStr locals (S+0x80 / S+0x98, EH
        // states 9 / 10), resets the out-parameter to L">>>BAD   NAME<<<"
        // (.text:06d4f7d9), calls Reputation::Vibe::GetName(&slot->rep_vibe,
        // S+0x80, S+0x98, &name) again (.text:06d4f7f3), re-forces the fallback
        // on -2 (.text:06d4f801) and destroys both FmtStrs before the TextNode.
        wchar_t const* NodeName = GCheatBadName;
        {
            FmtStr NameC;                                            // S+0x80
            FmtStr NameD;                                            // S+0x98
            int const Result = ::Reputation::Vibe::GetName(Slot.rep_vibe, NameC, NameD, NodeName);
            if (Result == -2)
                NodeName = GCheatBadName;
        }
        TextNode Name(NodeName, -1);                                 // .text:06d4f83d
        Server_RdlAppend(Rdl, Name);
    }

    // (3) serialize and submit.
    {
        BinaryRDLWriter Writer;
        unsigned int Size = 0;
        if (Writer.write_buffer(Rdl, reinterpret_cast<char*>(GCheatChatBuffer),
                                sizeof(GCheatChatBuffer), Size)) {
            // .text:06d4f8e1 MOV ECX,.data:DAT_06d8fad0 (Server + 4) / CALL
            // IServerImpl::SubmitChat -- a static object, no null test.
            IChatServer* Chat = static_cast<IChatServer*>(GetChatServerInterface());
            Chat->SubmitChat(CHAT_ID(0), Size, GCheatChatBuffer,
                             CHAT_ID(GCheatChatTargetId), -1);
        }
    }

    // (4) append to cheaters.log.
    char UserDataPath[260] = {0};
    GetUserDataPath(UserDataPath);

    char AcctDir[260] = {0};
    _makepath(AcctDir, nullptr, UserDataPath,
              IsMPServer() ? "Accts\\MultiPlayer" : "Accts", nullptr);

    char LogPath[260] = {0};
    _makepath(LogPath, nullptr, AcctDir, "cheaters.log", nullptr);

    FILE* Log = fopen(LogPath, "a+");
    if (Log != nullptr) {
        SYSTEMTIME Now;
        GetLocalTime(&Now);
        fprintf(Log, "%d/%d/%d %.2d:%.2d:%.2d Possible cheating detected (",
                (unsigned int)Now.wMonth, (unsigned int)Now.wDay, (unsigned int)Now.wYear,
                (unsigned int)Now.wHour, (unsigned int)Now.wMinute, (unsigned int)Now.wSecond);
        fprintf(Log, "%s) by %s(%s)\n", ReasonText,
                CharacterName.empty() ? "" : CharacterName.c_str(),
                AccountName.empty() ? "" : AccountName.c_str());
        fclose(Log);
    }
}

// ---------------------------------------------------------------------------
// PlayerData_CheckForCheats — the prologue of retail FUN_06d4ccd0
// (.text:06d4ccd0 .. .text:06d4cdc7), the one and only caller of
// PlayerData_ReportCheat. FUN_06d4ccd0 itself is a 2123-instruction function
// with a 0x2044-byte stack frame (it _chkstk's at .text:06d4ccf1) whose body
// past the gate is the save/land record writer; only the gate is ported here,
// because only the gate is what ReportCheat needs in order to stop being dead.
//
// Retail, instruction for instruction:
//   .text:06d4cd01  CALL COMMON.DLL::SinglePlayer / JNZ past the whole check
//   .text:06d4cd0b  CMP byte ptr [EBX+0x34c],0x1   ; already reported -> bail
//   .text:06d4cd2d  MOV EAX,[EBX+0x3ec] / TEST     ; no base -> skip the check
//   .text:06d4cd3d  CALL FUN_06d50120  -> Reason 3
//   .text:06d4cd4f  CALL FUN_06d4fd20  -> Reason 2
//   .text:06d4cd61  CALL FUN_06d4faf0  ; CMP EAX,[EBX+0x320] / JLE -> Reason 1
//   .text:06d4cd77  CALL FUN_06d4faa0  -> Reason 4
//   .text:06d4cd88  CALL FUN_06d4f570(Reason)      ; PlayerData_ReportCheat
//   .text:06d4cd8d  MOV byte ptr [EBX+0x34c],0x1   ; set the latch
//   .text:06d4cd9a  IClient slot +0x24 (client_id, 0) when not single player
//
// The four reason codes line up with GCheatReason[] above, which is the
// independent confirmation that these four calls are the cheat detectors:
//   3 "Equipment exceeds ship specs"       FUN_06d50120 reads
//       Archetype::GetShip(slot+0x264) and walks a large literal slot table
//   2 "Ship or Equipment not sold on base" FUN_06d4fd20 resolves the base
//       through BaseDB (FUN_06d15650 -> FUN_06d140f0) and compares against its
//       COMMON.DLL::GoodList
//   1 "Ship Related"                       FUN_06d4faf0 prices the whole
//       EquipDescList at slot+0x278 (Arch2Good / arch_is_combinable /
//       FUN_06d12cd0, clamped to 0x3b9ac9ff) and compares against total_earned
//   4 "Ship capacity exceeded"             FUN_06d4faa0 compares
//       EquipDescList's used volume against Archetype::GetShip(...)+0x100
//
// Factoring note: retail's gate is not a function, it is the early-return
// structure at the top of FUN_06d4ccd0. `true` here means what retail's
// `RET 0xc` with AL == 0 at .text:06d4cd2a / .text:06d4cdc4 means -- the caller
// must stop, either because the latch was already set or because a cheat was
// just reported.
// ---------------------------------------------------------------------------
namespace {
    // Retail's cash ceiling, 999999999. Every arithmetic path in FUN_06d4faf0
    // saturates at it (`CMP ...,0x3b9ac9ff / JG`).
    int const GCashCeiling = 0x3b9ac9ff;

    // Retail FUN_06d50120 (.text:06d50120 .. .text:06d504ea) -- reason 3,
    // "Equipment exceeds ship specs".
    //
    // The two literal blocks an earlier note called "a large literal slot table"
    // are two 0-terminated chains of HpAttachmentType enumerators, written one
    // dword at a time onto the stack:
    //   .text:06d50150 .. .text:06d50292  30 entries, then 0:
    //     0x2e,0x38,0x42, 0x2d,0x37,0x41, ... 0x25,0x2f,0x39
    //   .text:06d50299 .. .text:06d50339  20 entries, then 0:
    //     0x18,0x22, 0x17,0x21, ... 0x0f,0x19
    // Decoded through Common.dll's own hp_type name table (.data:063eca40, see
    // common_archetype.h's HpAttachmentType comment) the first chain is
    // (fighter, elite, freighter) shield-special 10 down to 1, and the second is
    // (gun, turret) special 10 down to 1. Both are "best first": an item's
    // INDEX in the chain is its rank, and a SMALLER index is a better hardpoint.
    //
    // The hull's own budget is the first chain entry for which
    // Archetype::Ship::get_legal_hps returns non-null (.text:06d50340 for the
    // shield chain, .text:06d50360 for the gun chain); if none does, the rank
    // is the terminator's index, i.e. worse than anything (.text:06d50352 /
    // .text:06d50380 zero the found type, and the ranking loop at
    // .text:06d50392 / .text:06d503b3 then walks all the way to the 0).
    //
    // Then, for every item in the slot's EquipDescList (.text:06d503c4):
    //   * Hardpoint == EquipDesc::CARGO_BAY_HP_NAME       -> skipped (.text:06d503f0)
    //   * Archetype::GetEquipment(ArchId) == null          -> skipped (.text:06d50411)
    //   * get_class_type() == 0xe (AClassShieldGenerator, vtable +0x10 at
    //     .text:06d5041b): rank get_hp_type() (vtable +0x18) in the shield chain
    //     and report if it beats the hull's (.text:06d50443 "CMP EDX,EBX / JL").
    //   * get_class_type() == 0x11 (AClassGun, .text:06d50452): take the MAXIMUM
    //     HpAttachmentType over Gun::get_hp_type_by_index(0..get_number_of_hp_types)
    //     (.text:06d50470 .. .text:06d5048b), rank it in the gun chain and report
    //     if it beats the hull's (.text:06d504ad "CMP ECX,[ESP+0x18] / JL").
    // Anything else falls through to the next node. AL=1 means "cheating".
    HpAttachmentType const* ShieldSpecialChain(unsigned int& OutCount) {
        // .text:06d50150 .. .text:06d50292, in store order.
        static HpAttachmentType const kChain[30] = {
            HpTypeFighterShieldSpecial10, HpTypeEliteShieldSpecial10, HpTypeFreighterShieldSpecial10,
            HpTypeFighterShieldSpecial9,  HpTypeEliteShieldSpecial9,  HpTypeFreighterShieldSpecial9,
            HpTypeFighterShieldSpecial8,  HpTypeEliteShieldSpecial8,  HpTypeFreighterShieldSpecial8,
            HpTypeFighterShieldSpecial7,  HpTypeEliteShieldSpecial7,  HpTypeFreighterShieldSpecial7,
            HpTypeFighterShieldSpecial6,  HpTypeEliteShieldSpecial6,  HpTypeFreighterShieldSpecial6,
            HpTypeFighterShieldSpecial5,  HpTypeEliteShieldSpecial5,  HpTypeFreighterShieldSpecial5,
            HpTypeFighterShieldSpecial4,  HpTypeEliteShieldSpecial4,  HpTypeFreighterShieldSpecial4,
            HpTypeFighterShieldSpecial3,  HpTypeEliteShieldSpecial3,  HpTypeFreighterShieldSpecial3,
            HpTypeFighterShieldSpecial2,  HpTypeEliteShieldSpecial2,  HpTypeFreighterShieldSpecial2,
            HpTypeFighterShieldSpecial1,  HpTypeEliteShieldSpecial1,  HpTypeFreighterShieldSpecial1};
        OutCount = 30;
        return kChain;
    }

    HpAttachmentType const* GunSpecialChain(unsigned int& OutCount) {
        // .text:06d50299 .. .text:06d50339, in store order.
        static HpAttachmentType const kChain[20] = {
            HpTypeGunSpecial10, HpTypeTurretSpecial10,
            HpTypeGunSpecial9,  HpTypeTurretSpecial9,
            HpTypeGunSpecial8,  HpTypeTurretSpecial8,
            HpTypeGunSpecial7,  HpTypeTurretSpecial7,
            HpTypeGunSpecial6,  HpTypeTurretSpecial6,
            HpTypeGunSpecial5,  HpTypeTurretSpecial5,
            HpTypeGunSpecial4,  HpTypeTurretSpecial4,
            HpTypeGunSpecial3,  HpTypeTurretSpecial3,
            HpTypeGunSpecial2,  HpTypeTurretSpecial2,
            HpTypeGunSpecial1,  HpTypeTurretSpecial1};
        OutCount = 20;
        return kChain;
    }

    // The ranking loop at .text:06d50392 / .text:06d503b3 / .text:06d50436 /
    // .text:06d504a0: index of Type in Chain, or Count (the terminator's index)
    // when Type is not in it -- including Type == 0, which is how "the hull has
    // no special hardpoint of this family at all" arrives here.
    unsigned int HpChainRank(HpAttachmentType const* Chain, unsigned int Count, HpAttachmentType Type) {
        for (unsigned int Index = 0; Index < Count; ++Index) {
            if (Chain[Index] == Type)
                return Index;
        }
        return Count;
    }

    // .text:06d50340 / .text:06d50360 -- the first chain entry the hull declares.
    HpAttachmentType BestLegalHp(Archetype::Ship const& Hull, HpAttachmentType const* Chain,
                                 unsigned int Count) {
        for (unsigned int Index = 0; Index < Count; ++Index) {
            if (Hull.get_legal_hps(Chain[Index]) != nullptr)
                return Chain[Index];
        }
        return static_cast<HpAttachmentType>(0);
    }

    bool PlayerData_EquipmentExceedsShipSpecs(PlayerData const& Slot) {
        // .text:06d50126 / .text:06d50144 -- no hull archetype, nothing to check.
        Archetype::Ship* Hull = Archetype::GetShip(Slot.ship_archetype);
        if (Hull == nullptr)
            return false;

        unsigned int ShieldCount = 0;
        HpAttachmentType const* ShieldChain = ShieldSpecialChain(ShieldCount);
        unsigned int GunCount = 0;
        HpAttachmentType const* GunChain = GunSpecialChain(GunCount);

        unsigned int const HullShieldRank =
            HpChainRank(ShieldChain, ShieldCount, BestLegalHp(*Hull, ShieldChain, ShieldCount));
        unsigned int const HullGunRank =
            HpChainRank(GunChain, GunCount, BestLegalHp(*Hull, GunChain, GunCount));

        CacheString const& CargoBayHp = EquipDesc::CARGO_BAY_HP_NAME;

        for (std::list<EquipDesc>::const_iterator ItemIt = Slot.equipment.Items.begin();
             ItemIt != Slot.equipment.Items.end(); ++ItemIt) {
            EquipDesc const& Item = *ItemIt;

            // .text:06d503f5 -- retail compares the interned Hardpoint POINTER.
            if (Item.Hardpoint == CargoBayHp.Value)
                continue;

            Archetype::Equipment* Equip = Archetype::GetEquipment(Item.ArchId);
            if (Equip == nullptr)
                continue;

            Archetype::AClassType const Class = Equip->get_class_type();

            if (Class == Archetype::AClassShieldGenerator) {
                unsigned int const Rank =
                    HpChainRank(ShieldChain, ShieldCount, Equip->get_hp_type());
                if (Rank < HullShieldRank)
                    return true;
            }

            // Retail re-reads the class here rather than using an else-if
            // (.text:06d5044b calls vtable +0x10 a second time), so a shield
            // generator that survived the test above still falls through to a
            // class test it cannot pass. Same observable behaviour.
            if (Class == Archetype::AClassGun) {
                Archetype::Gun const* GunArch = static_cast<Archetype::Gun const*>(Equip);
                HpAttachmentType Best = static_cast<HpAttachmentType>(0);
                int const Types = GunArch->get_number_of_hp_types();
                for (int TypeIndex = 0; TypeIndex < Types; ++TypeIndex) {
                    HpAttachmentType const Candidate = GunArch->get_hp_type_by_index(TypeIndex);
                    if (Candidate > Best)
                        Best = Candidate;
                }
                unsigned int const Rank = HpChainRank(GunChain, GunCount, Best);
                if (Rank < HullGunRank)
                    return true;
            }
        }

        return false;
    }

    // Retail's per-archetype tally, written out three times inside FUN_06d4fd20
    // (.text:06d4ff20, .text:06d4ff54, .text:06d4ffa0) with the same two guards each
    // time: a node only contributes when its Count is > 0 and < 0xf4240 (1000000).
    int CountArchInList(std::list<EquipDesc> const& Items, unsigned int ArchId) {
        int Total = 0;
        for (std::list<EquipDesc>::const_iterator It = Items.begin(); It != Items.end(); ++It) {
            if (It->ArchId != ArchId)
                continue;
            int const Count = It->Count;
            if (Count > 0 && Count < 0xf4240)
                Total += Count;
        }
        return Total;
    }

    // Retail FUN_06d4fd20 (.text:06d4fd20 .. .text:06d50106) -- reason 2,
    // "Ship or Equipment not sold on base". The 0x340 bytes an earlier note
    // called "SEH-wrapped list building" are two local std::list<EquipDesc>
    // objects and the C++ EH bookkeeping that unwinds them: the PUSH -1 /
    // PUSH handler / FS:[0] frame at .text:06d4fd20 and the three
    // `MOV [ESP+0x48],<trylevel>` stores are the whole of it. There is no
    // logic inside the exception machinery.
    //
    // The base is resolved exactly like reason 1: FUN_06d15650 (&BaseDB) then
    // FUN_06d140f0 (BaseDB::find_base), and the market map hangs off
    // FBaseEntry+0x04 -> BaseData+0x10.
    //
    // Step 1 (.text:06d4fd88 .. .text:06d4fe37) -- only when the ship archetype
    // differs from the landing snapshot (`CMP [ESI+0x324],[ESI+0x264]`): walk the
    // base's market for a good that (a) the base actually sells, (b) has
    // GoodInfo+0x4c == 3 (GoodCategoryShip) and (c) whose GoodInfo+0x90 hull good
    // has GoodInfo+0x54 == this ship archetype. If no such good exists the ship
    // was not bought here -> cheat (.text:06d4fdbf / .text:06d4fe21 both land on
    // LAB_06d50079, which returns AL=1). On a hit, retail copies that good's
    // GoodInfo+0x94 addon list into a local (.text:06d4fe31); +0x94 is the
    // default/"low" addon slot -- Common.dll's goods parser sends "mid" to +0xa0
    // (.text:06270bc3) and "high" to +0xac (.text:06270c38), leaving +0x94 for
    // every addon line with no slot token.
    //
    // Step 2 (.text:06d4fe3b .. .text:06d4fead) -- copy the live EquipDescList,
    // then, only when the ship archetype DID match the snapshot, drop from the
    // copy every item the snapshot already knew about
    // (EquipDescList::find_equipment_item on +0x328, then
    // remove_equipment_item(Id, -1) on the copy).
    //
    // Step 3 (.text:06d4feb1 .. .text:06d4ffd1) -- for every survivor:
    //   * Archetype::GetEquipment(ArchId) == null -> cheat (.text:06d4fecf)
    //   * Arch2Good(ArchId) found in the base's market with a transaction type of
    //     0 or 2 -> the base sells it, fine (.text:06d4feeb FUN_06d13000 returns
    //     &entry, .text:06d4fef4 reads entry+0x10)
    //   * otherwise compare how much of that archetype the player holds
    //     (sum of Count over +0x27c, each Count clamped to 0 < c < 1000000)
    //     against how much the snapshot had (same sum over +0x32c). Holding more
    //     than the snapshot is only allowed when the ship itself changed AND the
    //     new ship's addon list contains that archetype (.text:06d4ff70
    //     `CMP ESI,EBX / JLE ok`, then .text:06d4ff7e / .text:06d4ffbc).
    //
    // The two Common.dll naming problems this code used to work around are
    // FIXED as of 2026-09-17; MarketGoodInfo now matches retail's 0x1C-byte
    // layout and `It->second.Type` is the `enum TransactionType` this reads.
    // The transaction-type filter here (0 or 2) is the same "the base sells
    // it" test pub::Market::GetCommoditiesForSale applies at .text:06d12ec4.
    bool PlayerData_EquipmentNotSoldOnBase(PlayerData const& Slot) {
        // .text:06d4fd3d / .text:06d4fd57 / .text:06d4fd73 -- all three bail with AL=0.
        if (Slot.current_base == 0)
            return false;
        // .text:06d4fd66 .. .text:06d4fd73 -- FUN_06d140f0 on the BaseDB; only
        // its result is tested. The BaseData* at entry+0x04 is read untested
        // (.text:06d4fda2).
        // A miss FDUMPs "Base %u is invalid" (line 0x659) inside find_base.
        FBaseEntry* Base = GetBaseDB()->find_base(Slot.current_base);
        if (Base == nullptr)
            return false;
        BaseData const& Data = *Base->Data;

        bool const bShipUnchanged = (Slot.equip_snapshot_arch == Slot.ship_archetype);

        // Retail's local list at .text:06d4fd82; stays empty when the ship did not change.
        std::list<EquipDesc> ShipAddons;

        if (!bShipUnchanged) {
            GoodInfo const* ShipGood = nullptr;
            for (std::map<unsigned int, MarketGoodInfo>::const_iterator It = Data.Market.begin();
                 It != Data.Market.end(); ++It) {
                // .text:06d4fdd0 -- 0 or 2 means the base sells it.
                if (It->second.Type != TransactionBuyAndSell &&
                    It->second.Type != TransactionSaleOnly)
                    continue;
                GoodInfo const* Good = GoodList::find_by_id(It->second.GoodId);
                if (Good == nullptr)
                    continue;
                // .text:06d4fdeb -- GoodInfo+0x4c == 3.
                if (Good->Category != (unsigned)GoodCategoryShip)
                    continue;
                // .text:06d4fdf0 / .text:06d4fdf9 -- the hull good's ship archetype,
                // read straight off find_by_id's result with no null test.
                GoodInfo const* Hull = GoodList::find_by_id(Good->HullGoodId);
                if (Hull->ShipArchId == Slot.ship_archetype) {
                    ShipGood = Good;
                    break;
                }
            }
            if (ShipGood == nullptr)
                return true;
            ShipAddons.assign(ShipGood->AddonLow.begin(), ShipGood->AddonLow.end());
        }

        // .text:06d4fe46 -- a working copy of the player's own list.
        EquipDescList Work(Slot.equipment);

        if (bShipUnchanged) {
            // .text:06d4fe81 .. .text:06d4feab.
            for (std::list<EquipDesc>::const_iterator It = Slot.equipment.Items.begin();
                 It != Slot.equipment.Items.end(); ++It) {
                unsigned short const Id = It->Id;
                if (Slot.equip_snapshot.find_equipment_item(Id) != nullptr)
                    Work.remove_equipment_item(Id, -1);
            }
        }

        for (std::list<EquipDesc>::const_iterator WorkIt = Work.Items.begin();
             WorkIt != Work.Items.end(); ++WorkIt) {
            unsigned int const ArchId = WorkIt->ArchId;

            // .text:06d4fec4 / .text:06d4fecf.
            if (Archetype::GetEquipment(ArchId) == nullptr)
                return true;

            unsigned int const GoodId = Arch2Good(ArchId);
            if (GoodId != 0) {
                std::map<unsigned int, MarketGoodInfo>::const_iterator It = Data.Market.find(GoodId);
                if (It != Data.Market.end() &&
                    (It->second.Type == TransactionBuyAndSell ||
                     It->second.Type == TransactionSaleOnly)) {
                    continue; // .text:06d4ffc8 -- sold here, nothing to explain.
                }
            }

            // .text:06d4ff08 .. .text:06d4ff6e -- how much the snapshot accounted
            // for versus how much is actually on board. Retail's per-node filter is
            // `Count > 0 && Count < 0xf4240`, applied identically to both sums.
            int const Accounted = CountArchInList(Slot.equip_snapshot.Items, ArchId);
            int const OnBoard = CountArchInList(Slot.equipment.Items, ArchId);
            if (OnBoard <= Accounted)
                continue;

            // .text:06d4ff7e -- ship unchanged, so the surplus has no source at all.
            if (bShipUnchanged)
                return true;

            // .text:06d4ff8a .. .text:06d4ffbe -- otherwise it has to come out of
            // the new ship's addon list.
            if (CountArchInList(ShipAddons, ArchId) <= 0)
                return true;
        }

        return false;
    }

    // Retail FUN_06d4faf0 (.text:06d4faf0). NOT "total resale value" as the
    // earlier note said -- it is the player's whole NET WORTH: cash plus every
    // equipment item priced at the base they are sitting on, plus the hull.
    // .text:06d4cd66 then compares it against slot+0x320, the running total of
    // everything the server ever granted them, so "worth more than you were
    // ever given" is the cheat.
    //
    //   .text:06d4faf7  MOV EAX,[EBX + 0x3ec]      ; no base -> 0
    //   .text:06d4fb08  CALL FUN_06d15650          ; &BaseDB
    //   .text:06d4fb1e  CALL FUN_06d140f0          ; BaseDB::find_base(baseId)
    //   .text:06d4fb23  MOV EBP,[EBX + 0x31c]      ; running total starts at cash
    //   .text:06d4fb2d  MOV EAX,[EBX + 0x27c]      ; walk the EquipDescList
    //   .text:06d4fb44  CALL Arch2Good
    //   .text:06d4fb56  CALL FUN_06d12cd0          ; base price for that good
    //   .text:06d4fb60  CALL arch_is_combinable
    //     combinable : ECX = Count * (int)price                 (.text:06d4fb76)
    //     otherwise  : ECX = (int)(0.3f*price + 0.5f) * Count   (.text:06d4fb7e,
    //                  constants .rdata:06d6ae7c = 0x3e99999a and
    //                  .rdata:06d6b87c = 0x3effffde)
    //   .text:06d4fbce  GoodList_get()->find_by_ship_arch(ship_arch), then
    //                   FUN_06d12cd0 on its +0x48 and (int)(0.8f * price)
    //                   (.rdata:06d6ae78 = 0x3f4ccccd).
    // Every add saturates at GCashCeiling.
    //
    // NOT A DEVIATION (this replaces the note that called it one): retail's own
    // pub::Market::GetPrice (.text:06d18610) IS
    //     FUN_06d12cd0(BaseDB::find_base(BaseId), GoodId)
    // -- the exact same two calls this function makes inline, with the same
    // singleton (.text:06d1862f CALL FUN_06d15650 / .text:06d18636 CALL
    // FUN_06d140f0 / .text:06d18642 CALL FUN_06d12cd0). Routing the lookup
    // through the port's pub::Market::GetPrice therefore reproduces retail's
    // data path exactly, and the remaining work was in GetPrice itself
    // (server_pub.cpp), where the truncation retail bakes into the stored price
    // was missing. FUN_06d12cd0 in full:
    //   .text:06d12cd2  MOV ESI,[this+4] / ADD ESI,0x10  ; FBaseEntry+4 = BaseData*,
    //                   BaseData+0x10 = the market map (confirmed from the other
    //                   end: BaseData::set_market_good does ADD EBX,0x10 at
    //                   Common.dll .text:06264c22)
    //   .text:06d12ced  CALL FUN_06d15d10                ; map::find(GoodId)
    //   .text:06d12d02  FLD [node+0x10+4]                ; hit: the stored price
    //   .text:06d12d0c  GoodList::find_by_id(GoodId) / FLD [EAX+0x58]  ; miss: nominal
    // and the stored price is `(float)(int)(GoodInfo::Price * price_mult)`,
    // computed once at load time (Common.dll .text:06265c21 FLD [EAX+0x58] /
    // FMUL price_mult / CALL ftol / FILD / FSTP).
    int PlayerData_TotalEquipmentValue(PlayerData const& Slot) {
        // .text:06d4faf7 / .text:06d4fb0f -- no base, or no BaseDB: 0.
        if (Slot.current_base == 0)
            return 0;

        int Total = Slot.cash;

        for (std::list<EquipDesc>::const_iterator ItemIt = Slot.equipment.Items.begin();
             ItemIt != Slot.equipment.Items.end(); ++ItemIt) {
            EquipDesc const& Item = *ItemIt;
            unsigned int const GoodId = Arch2Good(Item.ArchId);
            if (GoodId == 0)
                continue;

            float Price = 0.0f;
            pub::Market::GetPrice(Slot.current_base, GoodId, Price);

            int Value = 0;
            if (arch_is_combinable(Item.ArchId)) {
                Value = Item.Count * (int)Price;
            } else {
                // .rdata:06d6ae7c = 0x3e99999a = 0.30000001f and
                // .rdata:06d6b87c = 0x3effffde = 0.4999989867f. The port used to
                // spell the second one 0.49999970197f, which is 0x3efffff6 -- a
                // different float; these two are the bytes.
                Value = (int)(0.3f * Price + 0.4999989867f) * Item.Count;
            }

            // .text:06d4fb99 / .text:06d4fba1 -- either side over the ceiling
            // pins the running total AT the ceiling.
            if (Value > GCashCeiling || Total > GCashCeiling) {
                Total = GCashCeiling;
            } else {
                Total += Value;
            }
        }

        // .text:06d4fbbd .. .text:06d4fc04 -- the hull itself.
        int HullValue = 0;
        if ((Slot.ship_archetype & 0x3fffffffu) != 0) {
            GoodInfoList* Goods = GoodList_get();
            GoodInfo const* ShipGood = (Goods != nullptr)
                ? Goods->find_by_ship_arch(Slot.ship_archetype)
                : nullptr;
            if (ShipGood != nullptr) {
                float HullPrice = 0.0f;
                // .text:06d4fbe0  MOV EAX,[EAX + 0x48].
                //
                // CORRECTED: retail's GoodInfo+0x48 is the good's OWN Id, not a
                // "hull good id". Proof from a second, independent site --
                // Common.dll's BaseDataList::load_market_data does
                // `CALL GoodList::find_by_nickname` / `MOV ESI,[EAX+0x48]`
                // (.text:06265aa1/.text:06265aad) and then uses ESI as the market
                // map KEY, the very key FUN_06d12cd0 looks up with the result of
                // Arch2Good. Retail's GoodInfo derives from CommReferrable, whose
                // sub-object occupies +0x00..+0x47, which is why its first own
                // field lands at +0x48.
                //
                // That matters here: find_by_ship_arch matches GoodInfo+0x54
                // (ShipArchId, .text:06271314), and in goods.ini it is the
                // `shiphull` good that carries `ship =` -- the same good that
                // carries the price. Its own `hull =` field is empty, so the port
                // reading HullGoodId here was pricing good id 0 and scoring every
                // player's hull at zero.
                pub::Market::GetPrice(Slot.current_base, ShipGood->Id, HullPrice);
                HullValue = (int)(0.8f * HullPrice);
                if (HullValue > GCashCeiling)
                    return GCashCeiling;
            }
        }

        if (Total > GCashCeiling)
            return GCashCeiling;
        return Total + HullValue;
    }

    // Retail FUN_06d4faa0 (.text:06d4faa0) -- ported 1:1.
    //   .text:06d4faa4  MOV EAX,[ESI + 0x264]
    //   .text:06d4faab  CALL COMMON.DLL::Archetype::GetShip   ; null -> false
    //   .text:06d4fabd  MOV ECX,[EAX + 0x100]                 ; Ship::HoldSize
    //   .text:06d4facd  CALL COMMON.DLL::EquipDescList::get_cargo_space_occupied
    //   .text:06d4fad3  FCOMP / FNSTSW / TEST AH,0x41 / JNZ false
    // AH & 0x41 non-zero is "occupied < hold" or "occupied == hold", so the
    // cheat is strictly-greater.
    bool PlayerData_HoldOverCapacity(PlayerData const& Slot) {
        Archetype::Ship* Hull = Archetype::GetShip(Slot.ship_archetype);
        if (Hull == nullptr)
            return false;
        return Slot.equipment.get_cargo_space_occupied() > Hull->HoldSize;
    }
}

bool __cdecl PlayerData_CheckForCheats(PlayerData& Slot) {
    // .text:06d4cd01 -- single player is never cheat-checked.
    if (SinglePlayer())
        return false;

    // .text:06d4cd0b -- one report per session.
    if (Slot.bCheatReported == 1)
        return true;

    // .text:06d4cd2d -- the whole check needs a base to price against.
    if (Slot.current_base == 0)
        return false;

    int Reason = 0;
    if (PlayerData_EquipmentExceedsShipSpecs(Slot)) {
        Reason = 3;
    } else if (PlayerData_EquipmentNotSoldOnBase(Slot)) {
        Reason = 2;
    } else if (PlayerData_TotalEquipmentValue(Slot) > Slot.total_earned) {
        Reason = 1;
    } else if (PlayerData_HoldOverCapacity(Slot)) {
        Reason = 4;
    } else {
        return false;
    }

    PlayerData_ReportCheat(Slot, Reason);
    Slot.bCheatReported = 1;

    // .text:06d4cd9a -- IClient slot +0x24 (index 9) with (client_id, 0).
    // TODO(port): rpc_iclient.h names it PushToClient; wiring it needs the same
    // IClient global the rest of this file reaches through
    // Server_ControllerDispatch, which is a separate open item.
    return true;
}

// ===========================================================================
// PlayerData's equipment-list family -- retail FUN_06d4ec10 / FUN_06d4eeb0 /
// FUN_06d4efc0 / FUN_06d4f100 / FUN_06d4f170 / FUN_06d4fc30, all thiscall
// members of PlayerData whose `this` is the slot base. Declared in
// common_server_playerdb.h; the port spells them as free functions because
// PlayerData is a plain struct here.
//
// Four of the six open with the same MP gate, verbatim:
//     .text:06d4efc6  CALL  [->COMMON.DLL::IsMPServer]
//     .text:06d4efcc  TEST  AL,AL
//     .text:06d4efce  JZ    do_work
//     .text:06d4efd0  MOV   DL,byte ptr [ESI + 0x64]
//     .text:06d4efd3  TEST  DL,DL
//     .text:06d4efd5  SETNZ AL
//     .text:06d4efd8  TEST  AL,AL
//     .text:06d4efda  JNZ   do_work
// i.e. `!IsMPServer() || selected_character[0] != 0`, the exact shape
// common_server_playerdb.h documents for +0x64.
// ===========================================================================
static bool PlayerData_EquipGateOpen(PlayerSlot const& Slot) {
    if (!IsMPServer())
        return true;
    return Slot.selected_character.is_valid();
}

// COMMON.DLL SubObjectID::EquipIdMaker::CreateEquipID (Common.dll 0xd4560):
// `INC word ptr [this] / MOV AX,[this] / RET` -- PRE-increment, so the first
// id handed out after a Reset (which stores 0x21) is 0x22.
static unsigned short PlayerData_NextEquipId(PlayerSlot& Slot) {
    return Slot.equip_id_maker.CreateEquipID();
}

unsigned short __cdecl PlayerData_AddEquipment(PlayerSlot& Slot, unsigned int ArchId,
                                               CacheString const& Hardpoint, int Count,
                                               float Health, unsigned char bEquipped,
                                               bool bNoCombine, bool bNotify) {
    // .text:06d4efdc .. .text:06d4efe8 -- the gate returns SUBOBJ_ID_NONE.
    if (!PlayerData_EquipGateOpen(Slot))
        return SUBOBJ_ID_NONE;

    unsigned short EquipId = 0;
    bool bCombine = false;

    // .text:06d4eff3 / .text:06d4f004 -- arch_is_combinable(ArchId) && !bNoCombine.
    if (arch_is_combinable(ArchId) && !bNoCombine) {
        // .text:06d4f016 -- retail writes the combinable flag it is about to
        // hand add_equipment_item BEFORE it knows whether a stack was found, so
        // a miss still adds combinable; it just gets a fresh id.
        bCombine = true;
        // .text:06d4f008 .. .text:06d4f01b -- find_matching_cargo(ArchId, 0, 1.0f).
        EquipDesc const* Existing = Slot.equipment.find_matching_cargo(ArchId, 0, 1.0f);
        if (Existing != nullptr) {
            // .text:06d4f025  MOV DI,word ptr [EAX + 0x2]  -- EquipDesc::Id.
            EquipId = Existing->Id;
        } else {
            EquipId = PlayerData_NextEquipId(Slot);
        }
    } else {
        // .text:06d4f02b -- bCombine := 0, and always a fresh id.
        EquipId = PlayerData_NextEquipId(Slot);
    }

    // .text:06d4f03e .. .text:06d4f094 -- the six setters, in retail's order.
    // Note what is NOT here: Server.dll imports no EquipDesc "set_mission" at
    // all (its whole EquipDesc import set is EquipDesc/copy-ctor/set_hardpoint/
    // is_internal/set_count/make_internal/set_equipped/set_status/set_arch_id/
    // set_id/CARGO_BAY_HP_NAME), so EquipDesc::bMission is never written on
    // this path.
    EquipDesc Desc;
    Desc.set_arch_id(ArchId);
    Desc.set_id(EquipId);
    Desc.set_hardpoint(Hardpoint);
    Desc.set_count(Count);
    Desc.set_status(Health);
    Desc.set_equipped(bEquipped);

    // .text:06d4f0ae -- add_equipment_item(Desc, bCombine).
    int const Combined = Slot.equipment.add_equipment_item(Desc, bCombine);

    // .text:06d4f0b4 .. .text:06d4f0d1 -- IClient::SendServerSetAddItem (+0x68), ch2 op 0x2e
    // FLPACKET_SERVER_SETADDITEM, with (client_id, add_equipment_item's return,
    // &Desc). Note the client id goes BY VALUE here, unlike AdjustCash's +0x134.
    if (bNotify) {
        ::g_pClientInterface->SendServerSetAddItem(Slot.client_id, Combined, Desc);
    }

    // .text:06d4f0dd -- FUN_06d43650(0, &this->client_id).
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &Slot.client_id);
    return EquipId;
}

void __cdecl PlayerData_RemoveEquipment(PlayerSlot& Slot, unsigned short Id, int Count,
                                        bool bNotify) {
    // .text:06d4f103 -- same gate; retail just returns.
    if (!PlayerData_EquipGateOpen(Slot))
        return;

    // .text:06d4f12b -- remove_equipment_item(Id, Count).
    int const Result = Slot.equipment.remove_equipment_item(Id, Count);

    // .text:06d4f139 .. .text:06d4f14b -- IClient::SendServerSetRemoveItem (+0x6c), ch2 op 0x2f
    // FLPACKET_SERVER_SETREMOVEITEM, with (client_id, result, Id, Count).
    if (bNotify) {
        ::g_pClientInterface->SendServerSetRemoveItem(Slot.client_id, Result, Id, Count);
    }

    // .text:06d4f157 -- FUN_06d43650(0, &this->client_id).
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &Slot.client_id);
}

// ---------------------------------------------------------------------------
// PlayerData_RemoveCargo -- retail FUN_06d4f260 (thiscall, RET 0x10):
// (ArchId, Count, Health, bNotify).
//   .text:06d4f27c  EquipDescList::find_matching_cargo(ArchId, Count, Health)
//                   on +0x278; a miss FDUMPs (0x100002, "%s(%d) : *** WARNING:
//                   %s", PlayerDB.cpp, 0xdea, "Tried to remove cargo player
//                   doesn't have!") and returns (.text:06d4f2f0 .. 06d4f318).
//   .text:06d4f286  Id = the hit's word at +0x02;
//   .text:06d4f28f  then exactly FUN_06d4f100's body: the MP selected-character
//                   gate, remove_equipment_item(Id, Count) (.text:06d4f2ae),
//                   IClient +0x6c (slot 27) (client_id, result, Id, Count) when
//                   bNotify, FUN_06d43650(0, &client_id).
// Callers: FUN_06ced840 .text:06ced921, FUN_06cf1970 .text:06cf1e28 /
// .text:06cf1e78.
// ---------------------------------------------------------------------------
void __cdecl PlayerData_RemoveCargo(PlayerSlot& Slot, unsigned int ArchId, int Count,
                                    float Health, bool bNotify) {
    EquipDesc const* Found = Slot.equipment.find_matching_cargo(ArchId, Count, Health);
    if (Found == nullptr) {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
              "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0xdea,
              "Tried to remove cargo player doesn't have!");
        return;
    }
    PlayerData_RemoveEquipment(Slot, Found->Id, Count, bNotify);
}

void __cdecl PlayerData_ModifyEquipment(PlayerSlot& Slot, unsigned short Id,
                                        CacheString const& Hardpoint, int Count,
                                        float Health, unsigned char bEquipped) {
    // .text:06d4f175 -- same gate.
    if (!PlayerData_EquipGateOpen(Slot))
        return;

    // .text:06d4f19a -- find_equipment_item(Id).
    EquipDesc* Item = Slot.equipment.find_equipment_item(Id);
    if (Item == nullptr) {
        // .text:06d4f1f1 .. .text:06d4f222 -- FDUMP, level 0x100001, format
        // "%s(%d) : *** ERROR: PlayerData::m...", PlayerDB.cpp line 0xdd7
        // (3543), with the id and the client id as arguments. The port has no
        // FDUMP; the observable behaviour (no change, no packet) is the same.
        return;
    }

    // .text:06d4f1ad .. .text:06d4f1d4 -- four setters; no set_arch_id here.
    Item->set_hardpoint(Hardpoint);
    Item->set_count(Count);
    Item->set_status(Health);
    Item->set_equipped(bEquipped);

    // .text:06d4f1e3 -- FUN_06d43650(0, &this->client_id). Retail sends NO
    // IClient packet on this path, only the controller broadcast.
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &Slot.client_id);
}

// ===========================================================================
// PlayerData::set_relative_health -- retail FUN_06d4ed10 (.text:06d4ed10),
// RET 0x8, so TWO stack arguments. The whole body is five instructions:
//   .text:06d4ed10  8b442404      MOV  EAX,[ESP + 0x4]        ; arg1
//   .text:06d4ed14  898168020000  MOV  [this + 0x268],EAX
//   .text:06d4ed1a  8a442408      MOV  AL,byte ptr [ESP + 0x8]; arg2
//   .text:06d4ed1e  84c0          TEST AL,AL / JZ out
//   .text:06d4ed22  8b8948030000  MOV  ECX,[this + 0x348]     ; client_id
//   .text:06d4ed38  ff5258        CALL dword ptr [EDX + 0x58] ; IClient slot 22
// +0x268 is `relative_health` (0x418 - 0x1b0), and arg1 is a FLOAT, not the
// `undefined4` the decompilation shows. The proof is the OTHER call site --
// .text:06d0ba6a in the arch-group damage walker:
//   .text:06d0ba6a  6a00      PUSH  0x0                   ; arg2 := false
//   .text:06d0ba6e  CALL [->COMMON.DLL::CSimple::get_relative_health]  (no args)
//   .text:06d0ba74  51        PUSH  ECX                   ; reserve arg1's slot
//   .text:06d0ba77  d91c24    FSTP  float ptr [ESP]       ; ...and store ST0 in it
//   .text:06d0ba7a  CALL FUN_06d4ed10
// An `FSTP [ESP]` into the argument slot is only ever a float parameter, and
// what it stores is a relative health straight out of Common.dll.
// ===========================================================================
void __cdecl PlayerData_SetRelativeHealth(PlayerSlot& Slot, float RelativeHealth,
                                          bool bNotify) {
    Slot.relative_health = RelativeHealth;
    // .text:06d4ed38 -- IClient::SendServerSetHullStatus (+0x58), with (client_id, health).
    // The health crosses as its raw bits: retail re-pushes the same dword it
    // just stored (.text:06d4ed30 `MOV ESI,[ESP + param_1+0x4]` / PUSH ESI).
    if (bNotify) {
        ::g_pClientInterface->SendServerSetHullStatus(Slot.client_id, RelativeHealth);
    }
}

// ===========================================================================
// PlayerData::terminate_trade -- retail FUN_06d50af0 (.text:06d50af0), RET 0,
// i.e. no arguments at all beyond `this`:
//   .text:06d50af0  8b81cc030000  MOV  EAX,[this + 0x3cc]     ; trade_offer
//   .text:06d50af6  85c0          TEST EAX,EAX / JZ out
//   .text:06d50afa  8b8148030000  MOV  EAX,[this + 0x348]     ; client_id
//   .text:06d50b00  6a00          PUSH 0x0                    ; reason := 0
//   .text:06d50b02  50            PUSH EAX
//   .text:06d50b03  b9ccfad806    MOV  ECX,.data:Server       ; the IServerImpl
//   .text:06d50b08  CALL IServerImpl::TerminateTrade
// Four call sites, one of them StarSystem::on_object_destroyed.
// ===========================================================================
void __cdecl PlayerData_TerminateTrade(PlayerSlot& Slot) {
    if (Slot.trade_offer == nullptr)
        return;
    GetServerInterface()->TerminateTrade(Slot.client_id, 0);
}

// ===========================================================================
// PlayerData::cancel_trade_request -- retail FUN_06d50a40 (.text:06d50a40).
//
// ARITY CORRECTION: the function ends in `RET 0x4` (.text:06d50ae7), so it has
// ONE stack argument on top of `this`. The decompilation renders that argument
// as `char unaff_BL` -- a Ghidra artifact, not a real uninitialised read. Its
// only caller inside StarSystem::on_object_destroyed pushes it explicitly:
//   .text:06d0b1dc  6a01      PUSH 0x1
//   .text:06d0b1de  LEA  ECX,[ESI + ECX + 0xfffffbe8]    ; the slot
//   .text:06d0b1e5  CALL FUN_06d50a40
// and the body reads it back at .text:06d50a7a as
// `MOV AL,byte ptr [ESP + 0x14]`, which is entry+4 after the four register
// pushes and the two-argument call that precedes it -- the stack argument.
//
// What it does:
//   * nothing at all when +0x3d0 (the OUTBOUND trade request partner) is 0
//   * .text:06d50a74  IClient slot 108 (+0x1b0) with (partnerId, my ship
//     object id at slot+0x3e4) -- tell the partner the request is gone
//   * when the bool is set, walk the PARTNER's inbound-request list
//     (its +0x3d8 head, count at +0x3dc) and unlink every node whose value
//     equals MY client id (.text:06d50aa3 .. .text:06d50ad7)
//   * .text:06d50adb  clear my own +0x3d0
// ===========================================================================
void __cdecl PlayerData_CancelTradeRequest(PlayerSlot& Slot, bool bDropPartnerEntry) {
    if (Slot.trade_request_partner == 0)
        return;

    // .text:06d50a5b .. .text:06d50a74 -- the notification reads MY ship object
    // id out of the array (`IMUL ESI,[this+0x348],0x418` then `[ESI+Players-0x34]`,
    // which is slot+0x3e4), not the partner's.
    ::g_pClientInterface->SendCommonPlayerTradeStopTradeRequest(Slot.trade_request_partner,
                                                                Slot.ship_object_id);

    if (bDropPartnerEntry) {
        PlayerSlot* const Partner = PlayerDB_GetPlayer(Slot.trade_request_partner);
        if (Partner != nullptr) {
            std::vector<unsigned int>& Requests = Partner->inbound_trade_requests;
            for (std::vector<unsigned int>::iterator It = Requests.begin();
                 It != Requests.end(); ) {
                if (*It == Slot.client_id)
                    It = Requests.erase(It);
                else
                    ++It;
            }
        }
    }

    Slot.trade_request_partner = 0;
}

// ===========================================================================
// PlayerData::set_trade_request -- retail FUN_06d50920 (.text:06d50920, RET 0x4),
// called only from IServerImpl::RequestTrade (.text:06cf2345) with the target
// session id.
//
//   .text:06d50927 .. .text:06d5099b  if +0x3d0 is set: IClient slot 108 (+0x1b0)
//       with (old partner, [Players[client_id] + 0x3e4]), unlink every node equal
//       to MY client id from the old partner's inbound list (+0x3d8 head, erase via
//       FUN_06d0f620), then clear +0x3d0. That is FUN_06d50a40 with the bool set,
//       inlined -- PlayerData_CancelTradeRequest(Slot, true) above.
//   .text:06d509a5 .. .text:06d509c5  gate on the TARGET slot's client id
//       ([target*0x418 + Players - 0xd0], raw bytes 8b 84 11 30 ff ff ff = +0x348
//       of slot target-1); no range check on the id.
//   .text:06d509c7 .. .text:06d50a07  append MY client id to the target's inbound
//       list: the new node goes before the head (_Buynode(head, head->_Prev)),
//       i.e. push_back, and ++size at +0x3dc.
//   .text:06d50a13  +0x3d0 = target
//   .text:06d50a23  IClient slot 107 (+0x1ac) with (target, my +0x3e4 ship id).
// When the target slot is free, only the cancel half runs.
// ===========================================================================
void __cdecl PlayerData_SetTradeRequest(PlayerSlot& Slot, unsigned int TargetId) {
    PlayerData_CancelTradeRequest(Slot, true);

    PlayerData& Target = Players[TargetId];
    if (Target.client_id == 0)
        return;

    Target.inbound_trade_requests.push_back(Slot.client_id);
    Slot.trade_request_partner = TargetId;
    ::g_pClientInterface->SendCommonPlayerTradeRequestTrade(TargetId, Slot.ship_object_id);
}

// ===========================================================================
// PlayerData::set_collision_groups -- retail FUN_06d4f320 (.text:06d4f320),
// RET 0x4. The only writer of the list at +0x26C other than set_ship_arch.
//   .text:06d4f324 .. .text:06d4f365  empty the list at +0x270, zeroing +0x274
//   .text:06d4f367 .. .text:06d4f3fa  the textbook VC6 std::list::operator=
//                                     from the incoming list (self-assignment
//                                     guarded at .text:06d4f36b)
//   .text:06d4f400  IClient::SendServerSetCollisionGroups (+0x5c) with (client_id, &this->list)
// There is NO MP/authorized gate inside this function (its caller has one) and
// NO controller broadcast -- the port used to send message 0x35 here with a
// two-dword payload of its own invention.
// ===========================================================================
void __cdecl PlayerData_SetCollisionGroups(PlayerSlot& Slot,
                                           std::list<CollisionGroupDesc> const& Groups) {
    if (&Slot.collision_groups == &Groups)
        return;
    Slot.collision_groups = Groups;
    ::g_pClientInterface->SendServerSetCollisionGroups(Slot.client_id, Slot.collision_groups);
}

void __cdecl PlayerData_SnapshotEquipment(PlayerSlot& Slot) {
    // .text:06d4fc30  MOV EAX,[param_1 + 0x264]
    // .text:06d4fc4b  MOV dword ptr [param_1 + 0x324],EAX
    Slot.equip_snapshot_arch = Slot.ship_archetype;
    // .text:06d4fc57 .. .text:06d4fd0f -- std::list<EquipDesc>::operator=
    // (MOVSD.REP of 8 dwords per surviving node, operator_delete for the
    // surplus, operator_new(0x28) + EquipDesc copy ctor for the shortfall).
    Slot.equip_snapshot = Slot.equipment;
}

// ---------------------------------------------------------------------------
// PlayerData_ResetTotalEarned -- retail FUN_06d4f560 (fastcall on the slot,
// plain RET): `[this + 0x320] = FUN_06d4faf0(this)` (.text:06d4f563 /
// .text:06d4f568). Sole caller: IServerImpl::BaseEnter .text:06d1778a, MP only,
// right before FUN_06d4fc30 (PlayerData_SnapshotEquipment).
// ---------------------------------------------------------------------------
void __cdecl PlayerData_ResetTotalEarned(PlayerSlot& Slot) {
    Slot.total_earned = PlayerData_TotalEquipmentValue(Slot);
}

// ---------------------------------------------------------------------------
// PlayerData_GetCash -- retail FUN_06d4f460 (fastcall, plain RET): +0x31c
// clamped to [0, 999999999] on the way out, never written back
// (.text:06d4f466 JGE / .text:06d4f46d CMP 0x3b9ac9ff / JLE).
// ---------------------------------------------------------------------------
int __cdecl PlayerData_GetCash(PlayerSlot const& Slot) {
    int Cash = Slot.cash;
    if (Cash < 0)
        return 0;
    if (Cash > 999999999)
        Cash = 999999999;
    return Cash;
}

// ---------------------------------------------------------------------------
// PlayerData_SetCash -- retail FUN_06d4f480 (thiscall, RET 4).
//   .text:06d4f483  IsMPServer(); in MP the body runs only when the selected
//                   CHARACTER_ID's first byte (+0x64) is non-zero.
//   .text:06d4f499  clamp to [0, 0x3b9ac9ff], store at +0x31c.
//   .text:06d4f4c7  IClient +0x134 (SendServerSetCash)(&this->+0x348, cash).
//   .text:06d4f4d0  FUN_06d43650(0, &this->+0x348).
// Caller: IServerImpl::ReqSetCash .text:06d175d8.
// ---------------------------------------------------------------------------
void __cdecl PlayerData_SetCash(PlayerSlot& Slot, int Cash) {
    if (IsMPServer() && !Slot.selected_character.is_valid())
        return;
    if (Cash < 0)
        Cash = 0;
    else if (Cash > 999999999)
        Cash = 999999999;
    Slot.cash = Cash;
    ::g_pClientInterface->SendServerSetCash(Slot.client_id, Cash);
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &Slot.client_id);
}

// ---------------------------------------------------------------------------
// PlayerData_AdjustCash -- retail FUN_06d4f4e0 (thiscall, RET 8).
//   .text:06d4f4e6  bCountEarned -> ADD [this + 0x320],Amount  (before the gate)
//   .text:06d4f4f6  NewCash = [this + 0x31c] + Amount          (before the gate)
//   .text:06d4f4fe  IsMPServer(); in MP the rest runs only when +0x64 != 0.
//   .text:06d4f514  clamp to [0, 0x3b9ac9ff], store at +0x31c.
//   .text:06d4f53f  IClient +0x134 (SendServerSetCash)(&this->+0x348, NewCash).
//   .text:06d4f548  FUN_06d43650(0, &this->+0x348).
// Callers: IServerImpl::ReqChangeCash .text:06d1761b (bCountEarned = false),
// FUN_06cf1970 .text:06cf1de5 / .text:06cf1df7, FUN_06d5c240 .text:06d5c2db;
// pub::Player::AdjustCash (0x06d553c0) inlines it with true.
// ---------------------------------------------------------------------------
void __cdecl PlayerData_AdjustCash(PlayerSlot& Slot, int Amount, bool bCountEarned) {
    if (bCountEarned)
        Slot.total_earned += Amount;
    int NewCash = Slot.cash + Amount;
    if (IsMPServer() && !Slot.selected_character.is_valid())
        return;
    if (NewCash < 0)
        NewCash = 0;
    else if (NewCash > 999999999)
        NewCash = 999999999;
    Slot.cash = NewCash;
    ::g_pClientInterface->SendServerSetCash(Slot.client_id, NewCash);
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &Slot.client_id);
}

// ---------------------------------------------------------------------------
// PlayerData_CheckPositionBound -- retail FUN_06d498f0 (.text:06d498f0 ..
// .text:06d4995b), thiscall on the PlayerData slot, RET 0xc. Ported 2026-09-24
// (claude-code:w5-server-06d4).
//   .text:06d498f3  SinglePlayer() -> return true
//   .text:06d49904  FUN_06d00670(this+0x3e4 ship id) (Server_LookupLiveObject);
//                   null -> return true
//   .text:06d49916  FLD z / FLD y / FLD x, then x*x + y*y (FADDP) + z*z (FADDP)
//   .text:06d4992e  FCOMP double [DAT_06d6b450] -- bytes 00 00 20 9b f3 5e 60 42
//                   = 562500000000.0 = 750000^2
//   .text:06d4993a  TEST AH,0x41 / JNZ -> return true when the square is below,
//                   equal or unordered (C0 / C3); only a strictly greater
//                   square falls through
//   .text:06d4994c  obj vt+0x158 (GameObject::destroy)(1, this+0x3e4)
//   .text:06d49952  return false
// Retail sums in x87 extended precision; the port sums in double. The two can
// only disagree for a square within a double ulp of 5.625e11.
// Sole caller: IServerImpl::SPObjUpdate (.text:06cf0698).
// ---------------------------------------------------------------------------
bool __cdecl PlayerData_CheckPositionBound(PlayerSlot& Slot, Vector const* Position) {
    if (SinglePlayer())
        return true;                                                       // .text:06d49958
    GameObject* const ShipObj = Server_LookupLiveObject(Slot.ship_object_id); // .text:06d49904
    if (ShipObj == nullptr)
        return true;
    double const X = Position->x;
    double const Y = Position->y;
    double const Z = Position->z;
    double const DistSq = X * X + Y * Y + Z * Z;                           // .text:06d4991e
    if (!(DistSq > 562500000000.0))
        return true;                                                       // .text:06d4993f
    ShipObj->destroy(1, Slot.ship_object_id);                                 // .text:06d4994c
    return false;
}

// ---------------------------------------------------------------------------
// PlayerData_ReloadShipData -- retail FUN_06d4a200 (.text:06d4a200 ..
// .text:06d4a49d), thiscall on the PlayerData slot, RET 0. Ported 2026-09-24
// (claude-code:w5-server-06d4). The name is retail's own, from the FDUMP text.
//   .text:06d4a221  !IsMPServer() -> FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
//                   PlayerDB.cpp, 0x70e, "reload_ship_data() is not valid in
//                   singleplayer\n") -- and execution CONTINUES
//   .text:06d4a24f  +0x3ec (current_base) = +0x3f0 (last_base_id)
//   .text:06d4a25b .. 06d4a2bf  Record = saved_characters[+0x64]: a default
//                   record (FUN_06d46ab0) paired with the key (FUN_06d57da0) and
//                   handed to the map's unique insert (FUN_06d575f0); both
//                   temporaries destroyed (FUN_06d46d20). Node offsets below
//                   are record + 0x20c.
//   .text:06d4a2d8  +0x268 (relative_health) = Record.BaseHullStatus (node+0x468)
//   .text:06d4a2d6 .. 06d4a354  collision_groups (+0x26c) = Record.
//                   BaseCollisionGroups (node+0x478): list::operator= inlined --
//                   skipped when the two are the same object, element-wise copy,
//                   then erase the surplus tail or insert the missing one
//   .text:06d4a367  a local std::list<EquipDesc>
//   .text:06d4a384 .. 06d4a3ab  push_back every Record.BaseEquipment (node+0x46c)
//                   entry whose bEquipped (EquipDesc+0xc) is set
//   .text:06d4a3c0 .. 06d4a410  then every CURRENT equipment (+0x278) entry that is
//                   NOT mounted and whose Archetype::GetEquipment(ArchId)->
//                   get_class_type() (vt+0x10) is 0x1b, 0x1a, 0x1f, 0x20 or 0x21
//                   (shield battery, repair kit, munition, mine, countermeasure);
//                   no null test on the archetype
//   .text:06d4a429 / 06d4a436  equipment.erase(all); equipment = local list
//   .text:06d4a443  Record.BaseEquipment (node+0x46c) = equipment
//   .text:06d4a450  Record.Equipment     (node+0x450) = equipment
//   .text:06d4a472 / 06d4a47d  the local list's nodes and head freed
// PlayerData::equipment and the record's Equipment / BaseEquipment are all
// EquipDescLists (whose Items is a std::list, common_goods.h), so the three
// list assignments are assignments of Items.
// ---------------------------------------------------------------------------
void __cdecl PlayerData_ReloadShipData(PlayerSlot& Slot) {
    if (!IsMPServer()) {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0x70e,
              "reload_ship_data() is not valid in singleplayer\n");       // .text:06d4a24a
    }
    Slot.current_base = Slot.last_base_id;                                  // .text:06d4a255

    PlayerData::FSavedCharacter& Record =
        Slot.saved_characters[Slot.selected_character];                    // .text:06d4a291

    Slot.relative_health = Record.BaseHullStatus;                          // .text:06d4a2d8
    Slot.collision_groups = Record.BaseCollisionGroups;                    // .text:06d4a2d6..06d4a354

    std::list<EquipDesc> Kept;                                             // .text:06d4a367
    std::list<EquipDesc>::const_iterator Base;
    for (Base = Record.BaseEquipment.Items.begin(); Base != Record.BaseEquipment.Items.end();
         ++Base) {
        if (Base->bEquipped)                                               // .text:06d4a384
            Kept.push_back(*Base);                                         // .text:06d4a39d
    }
    std::list<EquipDesc>::const_iterator Cur;
    for (Cur = Slot.equipment.Items.begin(); Cur != Slot.equipment.Items.end(); ++Cur) {
        if (Cur->bEquipped)                                                // .text:06d4a3c5
            continue;
        Archetype::AClassType const Class =
            Archetype::GetEquipment(Cur->ArchId)->get_class_type();        // .text:06d4a3d4
        if (Class == Archetype::AClassShieldBattery ||                     // 0x1b
            Class == Archetype::AClassRepairKit ||                         // 0x1a
            Class == Archetype::AClassMunition ||                          // 0x1f
            Class == Archetype::AClassMine ||                              // 0x20
            Class == Archetype::AClassCounterMeasure) {                    // 0x21
            Kept.push_back(*Cur);                                          // .text:06d4a402
        }
    }

    Slot.equipment.Items.clear();                                          // .text:06d4a429
    Slot.equipment.Items.assign(Kept.begin(), Kept.end());                 // .text:06d4a436
    Record.BaseEquipment.Items = Slot.equipment.Items;                     // .text:06d4a443
    Record.Equipment.Items = Slot.equipment.Items;                         // .text:06d4a450
}

void __cdecl PlayerData_SetShipArch(PlayerSlot& Slot, unsigned int ShipArch) {
    // .text:06d4ec16 .. .text:06d4ec2d -- a non-zero archetype that does not
    // resolve aborts the whole call, notifications included.
    if (ShipArch != 0 && Archetype::GetShip(ShipArch) == nullptr)
        return;

    // .text:06d4ec33 -- the clear-and-refill only runs when the archetype
    // actually changes.
    // The import at 0x9a7fe is now NAMED, and it settles what +0x26c is.
    // Server.dll IAT .rdata:06d644a0 is
    //   ?get_undamaged_collision_group_list@EqObj@Archetype@@QBE_NAAV?$list@
    //    UCollisionGroupDesc@@V?$allocator@UCollisionGroupDesc@@@std@@@std@@@Z
    // i.e. Archetype::EqObj::get_undamaged_collision_group_list, which the port
    // ALREADY has (common_archetype.h, implemented in common_archetype_ships.cpp).
    // So slot+0x26c is a std::list<CollisionGroupDesc> -- the hull's undamaged
    // collision groups -- and NOT a "per-hardpoint list" as this port's notes
    // (and common_server_playerdb.h's declaration comment) have been calling it.
    //
    // Retail, when the archetype changes: unlink and operator_delete every node
    // of that list and zero its size (.text:06d4ec40 .. .text:06d4ec75), then,
    // when (ShipArch & 0x3fffffff) != 0, re-resolve the Ship and call
    // get_undamaged_collision_group_list(slot->CollisionGroups)
    // (.text:06d4ec80 .. .text:06d4ec91), FDUMPing
    //   "%s(%d) : *** ERROR: Error retriev..." (PlayerDB.cpp, line 0xd07)
    // when it will not resolve (.text:06d4eca0).
    //
    // PORTED 2026-09-17 (wave 11). PlayerData::collision_groups exists now; the
    // include that used to block it was avoided entirely by moving
    // CollisionGroupDesc from common_cobject.h into common_archetype.h, which
    // common_server_playerdb.h already reaches through common_goods.h.
    if (Slot.ship_archetype != ShipArch) {
        // .text:06d4ec40 .. .text:06d4ec75 -- the list is emptied first, and it
        // is emptied even when the archetype will not resolve below.
        Slot.collision_groups.clear();

        // .text:06d4ec77  f7c5ffffff3f  TEST EBP,0x3fffffff
        // -- the low 30 bits, not the whole dword.
        if ((ShipArch & 0x3fffffffu) != 0) {
            Archetype::Ship* const Arch = Archetype::GetShip(ShipArch);
            if (Arch != nullptr) {
                // .text:06d4ec8e  PUSH EDI (= &slot->collision_groups)
                // .text:06d4ec91  CALL [->COMMON.DLL::Archetype::EqObj::
                //                        get_undamaged_collision_group_list]
                Arch->get_undamaged_collision_group_list(Slot.collision_groups);
            } else {
                // .text:06d4eca0 .. .text:06d4ecbd -- FDUMP(0x100001,
                // .rdata:06d6b700, .rdata:06d6b280 = PlayerDB.cpp, 0xd07, ShipArch),
                // called through the import with no null test.
                FDUMP(0x100001, "%s(%d) : *** ERROR: Error retrieving ship type %u",
                      "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0xd07, ShipArch);
            }
        }
    }

    // .text:06d4ecc4 -- reached from BOTH the "unchanged" jump at
    // .text:06d4ec3a and the "changed" path, so the store and both
    // notifications below are unconditional.
    Slot.ship_archetype = ShipArch;

    // .text:06d4ece0 -- IClient::SendServerSetShipArch (+0x54), ch2 op 0x23
    // FLPACKET_SERVER_SETSHIPARCH, with (client_id, ShipArch). No null test on
    // the interface (.text:06d4ecd0 loads DAT_06d8fb14 and calls through it).
    ::g_pClientInterface->SendServerSetShipArch(Slot.client_id, ShipArch);
    // .text:06d4ece6 -- FUN_06d43650(0x35, &this->client_id). Message id 0x35
    // here, NOT the 0 every other member of this family broadcasts.
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerShipArchSet, &Slot.client_id);
}

void __cdecl PlayerData_LoadEquipment(PlayerSlot& Slot, EquipDescList const& NewList) {
    // .text:06d4eecb -- same gate.
    if (!PlayerData_EquipGateOpen(Slot))
        return;

    // .text:06d4eee5 .. .text:06d4ef0b -- the slot's list is erased and then
    // assigned from the incoming one.
    Slot.equipment = NewList;

    // .text:06d4ef22 .. .text:06d4ef51 -- every element that arrived carrying
    // SUBOBJ_ID_NONE gets a fresh id from the slot's EquipIdMaker at +0x3c8.
    for (std::list<EquipDesc>::iterator It = Slot.equipment.Items.begin();
         It != Slot.equipment.Items.end(); ++It) {
        if (It->Id == SUBOBJ_ID_NONE) {
            It->set_id(PlayerData_NextEquipId(Slot));
        }
    }

    // .text:06d4ef59 -- EquipDescVector(EquipDescList const&), then
    // IClient::SendServerSetEquipment (+0x60), ch2 op 0x24 FLPACKET_SERVER_SETEQUIPMENT.
    EquipDescVector Wire(Slot.equipment);
    ::g_pClientInterface->SendServerSetEquipment(Slot.client_id, Wire);
    // .text:06d4ef87 -- FUN_06d43650(0, &this->client_id).
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &Slot.client_id);
    // .text:06d4ef91 -- the vector's buffer is freed: ~EquipDescVector (Common
    // 0x06269110) runs at scope exit.
}

void PlayerDB::init(unsigned int MaxPlayers, bool bAllowNewPlayers) {
    free();
    // Retail 0x06d510fe / 0x06d51101: [this+0x0c] = the bool parameter,
    // [this+0x04] = MaxPlayers. The port used to drop the parameter on the
    // floor and answer the flag out of Common.dll's single-player global.
    m_bAllowNewPlayers = bAllowNewPlayers;
    m_MaxPlayers = MaxPlayers;
    if (MaxPlayers > 0) {
        // Retail: operator new(MaxPlayers*0x418 + 4), element count in the
        // leading dword, base at +4, then the __vec_new helper FUN_06d601a8
        // runs the PlayerData ctor FUN_06d494f0 over every slot. new[] is that
        // sequence: PlayerData::PlayerData() is FUN_06d494f0 (see above). No
        // per-slot wipe follows in retail -- the old PlayerData_Reset loop that
        // stood here was the port's own.
        m_PlayerArray = new PlayerData[MaxPlayers];
    }
    // Retail 0x06d51160: [this+0x1c] = base + MaxPlayers*0x418.
    m_PlayerArrayEnd = m_PlayerArray ? (m_PlayerArray + MaxPlayers) : nullptr;
    InitServerAccount(m_ServerAccount, m_ServerID);
    // Phases (D) and (E) of retail 0x06d510c0: the account/character directory
    // scan. Everything from phase (B) onward is inside `if (IsMPServer())`, and
    // InitServerAccount already returns early for a single-player server, so
    // the same gate applies here.
    if (IsMPServer() && GAccountRootDir[0] != 0) {
        // ---- phase (D) -------------------------------------------------------
        char SearchPath[260];
        _makepath_s(SearchPath, sizeof(SearchPath), nullptr, GAccountRootDir, "*", nullptr);

        std::list<std::string> AccountDirs;
        struct _finddata_t DirFind;
        intptr_t DirHandle = _findfirst(SearchPath, &DirFind);
        if (DirHandle != -1) {
            do {
                // Retail 0x06d5164c-ish: `acStack_32c[0] != '.'` and
                // `(attrib & 0x10) != 0`. The '.' test is on the FIRST CHARACTER
                // only, which is how "." and ".." are skipped.
                if (DirFind.name[0] == '.') continue;
                if ((DirFind.attrib & _A_SUBDIR) == 0) continue;
                AccountDirs.push_back(std::string(DirFind.name));
            } while (_findnext(DirHandle, &DirFind) == 0);
            _findclose(DirHandle);
        }

        // ---- phase (E) -------------------------------------------------------
        std::list<std::string>::const_iterator DirIt = AccountDirs.begin();
        for (; DirIt != AccountDirs.end(); ++DirIt) {
            char AccountDir[260];
            _makepath_s(AccountDir, sizeof(AccountDir), nullptr, GAccountRootDir,
                        DirIt->c_str(), nullptr);

            CAccount* Account = new CAccount();
            Account->InitFromFolder(AccountDir);

            // The name container at this+0x44. PlayerDB owns these CAccount objects
            // (its destructor deletes through this map, as retail's does).
            // Retail is operator[]: FUN_06d59b00 inserts {name, 0} (.text:06d515fd),
            // and .text:06d5164a `MOV [ESI+0x1c],EBP` then stores the account into
            // the returned node without testing the insert's bool. A second
            // directory with the same account name therefore REPLACES the first
            // entry (the first CAccount is orphaned) and its *.fl files are still
            // registered below -- there is no skip.
            m_AccountNameMap[Account->m_Name] = Account;             // .text:06d5164a

            // Every "<dir>\*.fl" save file.
            char FileSearch[260];
            _makepath_s(FileSearch, sizeof(FileSearch), nullptr, AccountDir, "*", "fl");

            struct _finddata_t FileFind;
            intptr_t FileHandle = _findfirst(FileSearch, &FileFind);
            if (FileHandle == -1) {
                continue;
            }
            do {
                char LowerName[260];
                strcpy_s(LowerName, sizeof(LowerName), FileFind.name);
                _strlwr(LowerName);

                m_CharacterIDMap[CiString(LowerName)] = Account;

                char SavePath[260];
                _makepath_s(SavePath, sizeof(SavePath), nullptr, AccountDir,
                            FileFind.name, nullptr);
                CiWString CharacterName;
                ReadCharacterName(SavePath, CharacterName);
                if (!CharacterName.empty()) {
                    // Retail pushes the NAME, not the file name: the node payload
                    // is a 0x10-byte VC6 basic_string<wchar_t, ci_wchar_traits> (G in its mangling)
                    // (see ?AppendCharacterNames@CAccount@@QAEXAAV?$list@V?$basic_
                    // string@GUci_wchar_traits@... and CAccount::DeleteCharacterFrom
                    // ID at 0x06d56bd0, which hashes each entry back into a
                    // "<hash>.fl" before comparing).
                    Account->m_Characters.push_back(CharacterName);
                }
            } while (_findnext(FileHandle, &FileFind) == 0);
            _findclose(FileHandle);
        }
    }

    // Retail 0x06d5188f, the last call in PlayerDB::init: the cheat-warning
    // string override. Outside the IsMPServer() gate above because it does its
    // own SinglePlayer() check.
    LoadCheatMessage();
}

// ---------------------------------------------------------------------------
// PlayerDB::init phases (D) and (E), retail 0x06d510c0 -- inline in init() above,
// as in retail (they are ~350 of the function's 600 instructions). The port used
// to split them into a PlayerDB::ScanAccountDirectories member, which became an
// export retail does not have.
//
// (D) findfirst("<root>\*"), and for every entry whose name does not start with
//     '.' and whose attribute word has _A_SUBDIR (0x10) set, push the bare
//     directory name onto a temporary list.
// (E) for each collected directory:
//       operator new(0x38) for a CAccount, CAccount::InitFromFolder(acct, dir);
//       insert the account NAME into the container at this+0x44, storing the
//       bare CAccount* at node+0x1c;
//       findfirst("<dir>\*.fl") and for every save file: _strlwr the file name,
//       insert it into the character-id container at this+0x30 (same bare
//       CAccount* at node+0x1c), call ReadCharacterName on it and, when a name
//       came back, push THAT NAME onto the account's own std::list at +0x24
//       (the count at +0x2c is that list's size member, maintained by the
//       insert).
//     Finally FUN_06d46150() -- LoadCheatMessage, ported above -- and the
//     temporary list is released.
//
// Retail's map values are bare CAccount* pointers, and so are the port's
// (the invented FAccountInfo wrapper was removed 2026-09-24); the flags are
// the bytes at CAccount+0x30..+0x32 -- see CAccount in common_server.h.
// FUN_06d46150, called once at the end of phase (E), is LoadCheatMessage; it is
// ported above and called from init() rather than from here.
// ---------------------------------------------------------------------------

void PlayerDB::free() {
    if (m_PlayerArray) {
        // Retail .text:06d50e49..06d50e62: FUN_06d6001e(array, 0x418,
        // array[-1], FUN_06d50e90) -- the vector destructor iterator, last slot
        // to first -- then operator delete (FUN_06d5ffb0) on array-4. That is
        // delete[]: PlayerData::~PlayerData() is FUN_06d50e90, whose first act
        // is the slot teardown FUN_06d49230 (.text:06d50eb9) followed by the
        // member destructors, so slot k is fully destroyed before slot k-1 is
        // touched, as in retail.
        delete[] m_PlayerArray;
        m_PlayerArray = nullptr;
    }
    m_PlayerArrayEnd = nullptr;
    m_MaxPlayers = 0;
}

// Retail .data:DAT_06d8f9a0 is GServerStats + 0x08 (the counter block at
// .data:06d8f998 that IServerImpl::GetServerStats copies out): incremented once,
// at 0x06d54489 inside PlayerDB::logout. Defined in IServerImpl.cpp.
extern ServerStats GServerStats;

// ---------------------------------------------------------------------------
// PlayerData_FlushPendingVisits — retail FUN_06d4b450(slot), 0x06d4b450,
// __fastcall on the slot alone (the body ends in a plain RET).
//
// Reading the raw bytes settles what this is: NOT "per-slot spatial teardown",
// as the previous note guessed, but the commit of the pending visit queue into
// the persistent visit map. Both containers are documented on PlayerData now.
//
//   06d4b480  LEA  EBP,[ESI + 0x3a0]              ; the map
//   06d4b486  MOV  EAX,[ESI + 0x3b8]              ; the pending vector's _First
//   06d4b48c  MOV  ECX,[EAX + EDI*0x8]            ; entry.key      (stride 8)
//   06d4b48f  MOV  BL,byte ptr [EAX + EDI*0x8+4]  ; entry.flags
//   06d4b4a6  OR   BL,0x8                         ; the "committed" bit
//   06d4b4a9  CALL FUN_06d15d10                   ; map.find(key)
//   06d4b4b2  CMP  EAX,[ESI + 0x3a4]              ; == end() ?
//   06d4b4ba  OR   BL,byte ptr [EAX + 0x10]       ; fold in the existing byte
// FUN_06d15d10 is find, not lower_bound: after the lower_bound descent
// (06d15d30..06d15d40) it returns the head -- end() -- unless the candidate is
// not the head AND !(key < node.key) (06d15d43..06d15d50, JC to 06d15d64,
// which stores EDX = head). So the OR only ever folds in the SAME key's flags.
//   06d4b4d6  CALL FUN_06d5c600                   ; map.insert(key, 0)
//   06d4b4e6  MOV  byte ptr [ECX + 0x10],BL       ; store the merged flags
//   06d4b515  MOV  [ESI + 0x3bc],ECX              ; pending._Last = _First
//
// The count is `(_Last - _First) >> 3` (06d4b471 SAR EAX,0x3), and when _First
// is null retail takes it as zero rather than computing a difference.
// ---------------------------------------------------------------------------
void __cdecl PlayerData_FlushPendingVisits(PlayerData& Slot) {
    for (size_t Index = 0; Index < Slot.visited_states.size(); ++Index) {
        const PlayerData::FVisitedState& Pending = Slot.visited_states[Index];
        unsigned char Flags = static_cast<unsigned char>(Pending.visit_flags | 0x08);

        // .text:06d4b4a9 .. .text:06d4b4ba -- find(key); a hit ORs its byte in.
        std::map<unsigned int, unsigned char>::iterator Found =
            Slot.visit_map.find(Pending.system_id);
        if (Found != Slot.visit_map.end())
            Flags = static_cast<unsigned char>(Flags | Found->second);
        // .text:06d4b4d6 / .text:06d4b4e6 -- insert(key, 0), then the node's
        // byte = the merged flags.
        Slot.visit_map[Pending.system_id] = Flags;
    }
    Slot.visited_states.clear();
}

// ---------------------------------------------------------------------------
// PlayerData_ShouldQueueVisit -- retail FUN_06d4b200 (.text:06d4b200 ..
// .text:06d4b263), thiscall on the slot, (Key, Flags), `RET 8`.
//   FUN_06d59ea0 = lower_bound(&this->+0x3a0, &Key); a result equal to the head
//   (+0x3a4) or with Key < node.key (node+0x0c) is end() -- i.e. find(Key).
//   A hit ORs the node's byte (node+0x10) into Flags.
//   Returns 0 when (Flags & 0x10) && (Flags & 0x08), else 1.
// Callers: FUN_06ce8f50 .text:06ce905e, FUN_06ce9550 .text:06ce9d25 /
// .text:06cea07b, each with (solar+0xb0, CSolar::GetVisitValue()).
// ---------------------------------------------------------------------------
bool __cdecl PlayerData_ShouldQueueVisit(PlayerData const& Slot, unsigned int Key,
                                         unsigned char Flags) {
    std::map<unsigned int, unsigned char>::const_iterator Found = Slot.visit_map.find(Key);
    if (Found != Slot.visit_map.end())
        Flags = static_cast<unsigned char>(Flags | Found->second);
    if ((Flags & 0x10) != 0 && (Flags & 0x08) != 0)
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// PlayerData_QueueVisit -- retail FUN_06d4b270 (.text:06d4b270 ..), thiscall on
// the slot, (Key, Flags), `RET 8`: the VC6 vector<pair<uint, uchar>>::insert(end,
// 1, {Key, Flags}) on +0x3b4 (first +0x3b8, last +0x3bc, end +0x3c0), with its
// grow-by-max(size, 1) reallocation -- push_back here.
// Callers: FUN_06ce8f50 .text:06ce9192, FUN_06ce9550 .text:06ce9f05 /
// .text:06cea345.
// ---------------------------------------------------------------------------
void __cdecl PlayerData_QueueVisit(PlayerData& Slot, unsigned int Key, unsigned char Flags) {
    PlayerData::FVisitedState State;
    State.system_id = Key;
    State.visit_flags = Flags;
    Slot.visited_states.push_back(State);
}

// ---------------------------------------------------------------------------
// The two teardown steps PlayerDB::cleanup and PlayerDB::logout share, retail
// FUN_06d00010 and FUN_06d18880. Both are file-scope __cdecl helpers there too.
//
// FUN_06d00010(sid):
//     sysId = [slot+0x3E0]                       (PlayerData::system_id)
//     if ((sysId & 0x3fffffff) != 0):
//         node = SysDB::SysMap.lower_bound(sysId)
//         if (node != end && node->key <= sysId) FUN_06d08890(&node->value, sid)
//         else                                   FUN_06d08890(&end->value, sid)
// FUN_06d08890(system, sid):
//     FUN_06d08740(system, sid)                  <- StarSystem::remove_player
//     FUN_06d4b450(slot)                         <- flush the pending visits
//     if ([slot+0x3E0] != 0):
//         [slot+0x3E0] = 0
//         FUN_06d43650(0x3a, { system->SystemInfo->vtable[0](), 0, sid })
//     if (SinglePlayer() && [system+0xfc]):
//         [system+0xfc] = 0
//         FUN_06d43650(0x3d, system->SystemInfo->vtable[0]())
//         FUN_06d072c0(system)                   <- StarSystem::unload
//
// THE MISS ARM. On a miss retail calls FUN_06d08890 on the map's head node
// (DAT_06d8da30 = SysMap+4) + 0x10, the slot where a node keeps its StarSystem.
// SysMap's constructor FUN_06d104a0 allocates that head with a bare
// operator new(0x114) (.text:06d104ed) and writes only its links: +0x00 / +0x08
// = the head itself, +0x04 = the nil node, +0x110 colour, +0x111 isnil
// (.text:06d104f7 .. .text:06d10511). The 0x100 bytes at +0x10 are never
// constructed and nothing in Server.dll writes them. So every field FUN_06d08890
// reads there holds whatever that heap block held before:
//   +0x0c  the player-list head, read first and dereferenced at once
//          (.text:06d08748 / .text:06d0874c, in FUN_06d08740);
//   +0x38  the object-list head (.text:06d08794);
//   +0x04  the Universe::ISystem*, called through its vtable
//          (.text:06d088d0 / .text:06d0890a);
//   +0xfc  the loaded byte, read and cleared in single player
//          (.text:06d08900 / .text:06d0890d).
// Retail therefore faults at .text:06d0874c unless the stale bytes happen to
// form valid pointers. The miss needs a system id with no SysMap entry. SysMap
// holds every Universe system (FUN_06d0fb40), but +0x3e0 is also copied from
// IBase::SystemId, which Common's base loader takes as a bare CreateID of the
// `system` key (.text:0633884d) without checking it against the system list.
//
// MSVC's std::map has the same shape. _Buyheadnode allocates a whole
// _Tree_node and constructs only _Left/_Parent/_Right/_Color/_Isnil, so the
// head's _Myval is equally unconstructed storage. On a miss the port addresses
// that storage, as retail does -- through the iterator's node pointer, because
// dereferencing end() is checked in Debug -- and runs the same body on it. This
// is MSVC-STL specific (_Ptr, _Myval), which is the only toolchain here.
// ---------------------------------------------------------------------------
static void PlayerDB_LeaveSystem(PlayerData& Slot, unsigned int SessionId) {
    unsigned int const SystemId = Slot.system_id;                  // .text:06d00026
    if ((SystemId & 0x3fffffff) == 0) {
        return;
    }

    // .text:06d0003f lower_bound, .text:06d0004a == head?, .text:06d00056
    // `CMP id,[node+0xc] / JC` -- key above the id is a miss too.
    SysDB::SysMapType::iterator It = SysDB::SysMap.lower_bound(SystemId);
    if (It == SysDB::SysMap.end() || SystemId < It->first) {
        It = SysDB::SysMap.end();
    }
    // Both arms: FUN_06d08890(&node->value, sid), the head's on a miss.
    // KNOWN CRASH (intentional, retail-faithful): on a miss System is the
    // unconstructed garbage storage of SysMap's head node. Cause: the player's
    // system id (from IBase::SystemId, the base's `system` key in universe.ini)
    // has no SysMap entry -- check universe.ini data. See THE MISS ARM above;
    // retail faults at .text:06d0874c. Do not fix: user decision 2026-09-24.
    StarSystem& System = It._Ptr->_Myval.second; // KNOWN CRASH on SysMap miss, see above

    System.remove_player(SessionId);                               // FUN_06d08740

    PlayerData_FlushPendingVisits(Slot);                           // FUN_06d4b450

    // .text:06d088c0 .. .text:06d088ee
    if (Slot.system_id != 0) {
        Slot.system_id = 0;
        unsigned int Payload[3];
        // KNOWN CRASH site on SysMap miss (garbage SystemInfo, retail .text:06d088d0) -- see System above.
        Payload[0] = System.SystemInfo->NicknameId;                // ISystem slot 0, [+0x4c]
        Payload[1] = 0;
        Payload[2] = SessionId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);
    }

    // .text:06d088f6 .. .text:06d0892d -- the single-player tail: the last
    // player out turns the system off.
    if (SinglePlayer() && System.bLoaded != 0) {
        System.bLoaded = 0;
        // KNOWN CRASH site on SysMap miss (garbage SystemInfo, retail .text:06d0890a) -- see System above.
        unsigned int SystemNickname = System.SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemUnloaded, &SystemNickname);
        System.unload();                                           // FUN_06d072c0
    }
}

// FUN_06d18880(sid) — the UNDOCK, not an AI teardown:
//     room = [slot+0x3F8]   (PlayerData::current_location)
//     base = [slot+0x3EC]   (PlayerData::current_base)
//     if ((base & 0x3fffffff) != 0):
//         if ((room & 0x3fffffff) != 0): FUN_06d143e0(FUN_06d15650(), sid, room)
//         FUN_06d142c0(FUN_06d15650(), sid, base)
//     [slot+0x3EC] = 0; [slot+0x3F8] = 0        <- unconditional, both paths
//
// CORRECTION to the previous note on this function: FUN_06d15650 is not an
// unported "base-manager singleton". Its entire body is `return &DAT_06d8da70;`
// (Server.dll.c 42248) -- the BaseDB global (src/include/server_basedb.h,
// GetBaseDB()). The two teardown entry points are BaseDB::exit_location
// (FUN_06d143e0) and BaseDB::unload_base (FUN_06d142c0).
static void PlayerDB_LeaveBase(PlayerData& Slot, unsigned int SessionId) {
    if ((Slot.current_base & 0x3fffffff) != 0) {
        if ((Slot.current_location & 0x3fffffff) != 0) {
            GetBaseDB()->exit_location(SessionId, Slot.current_location);
        }
        GetBaseDB()->unload_base(SessionId, Slot.current_base);
    }
    Slot.current_base = 0;
    Slot.current_location = 0;
    // Retail does NOT touch +0x3F4 (entered_base_id) here: FUN_06d18880 writes
    // only [slot-0x2c] and [slot-0x20] (.text:06d188cf / .text:06d188e3 and the
    // location store). The port's location_active/base_active mirrors it used to
    // clear here no longer exist.
}

// ---------------------------------------------------------------------------
// PlayerDB::cleanup — retail RVA 0x74340, ordinal 473
// Raw listing 0x06d54340. Note that retail ignores `this` entirely: it loads
// the global `Players` array base (MOV ECX,[.data:Players]) and indexes
// base + SessionId*0x418 - 0x418. Since `Players` is the one and only PlayerDB
// instance, going through `this` is equivalent.
//
// Four independent actions, in this order:
//   1. FUN_06d00010(SessionId) — looks up SysDB::SysMap by the slot's system
//      field (+0x3E0) and unregisters the player from that StarSystem.
//   2. FUN_06d18880(SessionId) — undocks the session: it reads the base id at
//      slot+0x3EC and the room/location id at slot+0x3F8 (the very fields
//      pub::Player::GetBase and GetLocation return), tells the base manager
//      singleton to drop the player, and then zeroes both.
//   3. if ([slot+0x3E0] & 0x3fffffff) != 0 -> pub::Controller::_SendMessage
//      (FUN_06d43650) event 0x3a with the 12-byte payload {field3E0, 0, SessionId}.
//   4. if ([slot+0x3CC] != 0) -> IServerImpl::TerminateTrade(SessionId, 4).
//
// It is NOT a wrapper around logout(); retail logout (0x06d543c0) starts with
// the same four steps and then does considerably more, so delegating here
// destroyed the slot far too early.
// ---------------------------------------------------------------------------
void PlayerDB::cleanup(unsigned int SessionId) {
    if (SessionId == 0 || SessionId > m_MaxPlayers || !m_PlayerArray) return;

    PlayerData& Slot = m_PlayerArray[SessionId - 1];

    PlayerDB_LeaveSystem(Slot, SessionId);
    PlayerDB_LeaveBase(Slot, SessionId);

    // Step 3: despawn notification. Retail's [slot+0x3E0] is the system id,
    // PlayerData::system_id here.
    //
    // Note on retail: by the time control reaches here, FUN_06d00010 has
    // already zeroed +0x3E0 whenever it was non-zero, so this guard cannot pass
    // in a normal teardown -- step 3 is vestigial in the retail binary. It is
    // reproduced rather than folded away, and PlayerDB_LeaveSystem above clears
    // system_id the same way FUN_06d08890 does, so the port keeps the shape.
    unsigned int SystemField = Slot.system_id;
    if ((SystemField & 0x3fffffff) != 0) {
        unsigned int Payload[3];
        Payload[0] = SystemField;
        Payload[1] = 0;
        Payload[2] = SessionId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);
    }

    // Step 4: cancel any trade this session is engaged in, with retail's
    // reason code 4 (PUSH 4 / PUSH sid / MOV ECX,.data:Server / CALL
    // IServerImpl::TerminateTrade at 0x06d543a3).
    // Retail's [slot+0x3CC] is the active CPlayerTradeOffer*. `Server` is a
    // static object, so retail has nothing to null-test (06d543a6).
    if (Slot.trade_offer != nullptr) {
        GetServerInterface()->TerminateTrade(SessionId, 4);
    }
}

unsigned int PlayerDB::GetMaxPlayerCount() {
    return m_MaxPlayers;
}

unsigned int PlayerDB::GetServerID() {
    return m_ServerID;
}

// ---------------------------------------------------------------------------
// PlayerDB::GetServerSig — retail RVA 0x1AC0, ordinal 264
// Raw listing 0x06ce1ac0 is a two-instruction thunk:
//   ADD  ECX,0x10
//   JMP  dword ptr [->COMMON.DLL::CPlayerAccount::GetAccountNameSigChar]
// i.e. it returns the signature-hash string of the CPlayerAccount embedded in
// PlayerDB at this+0x10 -- m_ServerAccount here.
// ---------------------------------------------------------------------------
const char* PlayerDB::GetServerSig() {
    return m_ServerAccount.GetAccountNameSigChar();
}

bool PlayerDB::is_valid(const unsigned int& Id) {
    // .text:06ce1a06 / 06ce1a15: id == 0, then (id - 1) >= [this+4]. Retail has no test
    // on the array pointer itself (.text:06ce1a1c reads it straight); the port's
    // `!m_PlayerArray` early out was an addition and is gone (claude-code:w5-server-06cf).
    if (Id == 0 || Id - 1 >= m_MaxPlayers) return false;
    // Retail 0x06ce1a19: CMP dword ptr [EAX + EDX*0x1 + 0x348],0 -- the
    // client-id field doubles as the slot-occupied flag.
    return m_PlayerArray[Id - 1].client_id != 0;
}

// ---------------------------------------------------------------------------
// PlayerDB::is_valid_ship_owner — retail RVA 0x1A40, ordinal 496
// Raw listing 0x06ce1a40: the FIRST reference argument is the player id
// (TEST EDX,EDX -> CMP (id-1) against [this+0x4] -> [base+(id-1)*0x418+0x348]),
// the SECOND is the ship flid, compared against [base + id*0x418 - 0x34]
// (= slot+0x3E4). The declaration in common_server.h still names the
// parameters the other way round; the names there are cosmetic only, both
// are `const unsigned int&`, but they should be swapped as well.
// ---------------------------------------------------------------------------
bool PlayerDB::is_valid_ship_owner(const unsigned int& PlayerId, const unsigned int& ShipFlid) {
    if (!is_valid(PlayerId)) return false;
    // Retail compares against [base + id*0x418 - 0x34] == slot+0x3E4, the
    // current ship object id.
    return m_PlayerArray[PlayerId - 1].ship_object_id == ShipFlid;
}

PlayerData& PlayerDB::operator[](const unsigned int& Id) {
    return m_PlayerArray[Id - 1];
}

PlayerData* PlayerDB::traverse_active(PlayerData* Current) const {
    if (!m_PlayerArray) return nullptr;
    // Retail walks to [this+0x1c], the end pointer init stores, not to a bound
    // recomputed from MaxPlayers.
    PlayerData* Start = (Current == nullptr) ? m_PlayerArray : Current + 1;
    for (PlayerData* Slot = Start; Slot < m_PlayerArrayEnd; ++Slot) {
        if (Slot->client_id != 0) return Slot;
    }
    return nullptr;
}

// ===========================================================================
// PlayerDB::login — retail RVA 0x732F0, ordinal 501
// Public login method. Iterates through active player slots to check if the
// account is already logged in (wcsicmp on the wchar_t name at slot+0x00).
// If already logged in, returns 1 (duplicate).
// If not found, delegates to load_user_data for the actual login.
// ===========================================================================
unsigned char PlayerDB::login(const SLoginInfo& Info, unsigned int SessionId) {
    // Retail: loops through all player slots checking for name match
    // Slot layout: wchar_t name at offset 0, connection ID at +0x348
    const wchar_t* LoginName = Info.AccountName;
    if (!m_PlayerArray) return 2;

    // Retail walks the active slots with traverse_active and compares the
    // account name at slot+0x00. That name is PlayerData::AccountName here --
    // a real field now; it used to be `reinterpret_cast<wchar_t*>(Current)`
    // over a struct whose first member is `unsigned int session_id`.
    PlayerData* Current = traverse_active(nullptr);
    while (Current != nullptr) {
        if (_wcsicmp(Current->AccountName, LoginName) == 0) {
            return 1; // Account already logged in
        }
        Current = traverse_active(Current);
    }

    // Not already logged in — proceed with load_user_data
    return load_user_data(Info, SessionId);
}

// ---------------------------------------------------------------------------
// PlayerData_Teardown — retail FUN_06d49230(slot), 0x06d49230, whole: the
// prologue that touches other slots and subsystems, then the field wipe
// (formerly split off as the port's PlayerData_Reset, now gone).
//
// Retail, in dword indices off the slot (param_1[n] == slot + n*4):
//
//  (a) param_1[0xf4] (+0x3D0) -- this player's OUTBOUND trade request. When
//      non-zero:
//          IClient->[+0x1b0](partner, Players[slot->client_id].ship_object_id)
//          FUN_06d571a0(&Players[partner] + 0x3d4, &slot->client_id)
//          slot->+0x3D0 = 0
//      i.e. tell the partner the request is gone (slot 108) and pull our id out
//      of the partner's inbound list. Note the second argument: retail indexes
//      Players by [slot+0x348] (the client id) and reads -0x34, which is
//      +0x3E4 -- the ship object id -- NOT the session id.
//
//  (b) param_1[0xf6] (+0x3D8, the head of the list at +0x3D4) -- every INBOUND
//      request. For each node, node[2] is a session id; if that session's own
//      +0x3D0 is non-zero, fire the same slot-108 notification for it and zero
//      its +0x3D0. Then erase the whole list (FUN_06cf7720).
//
//  (c) param_1[0xc6] (+0x318) -- the reputation vibe. `if (v != -1) {
//      Reputation::Vibe::Free(&v); v = -1; }`. -1, not 0, is the empty sentinel.
//
//  (d) param_1[0x100] (+0x400) -- the CAccount. `if (a != 0) *(a + 0x34) = 0;`
//      CAccount+0x34 is m_ClientID, so the account is marked logged-out.
//
//  (e) SubObjectID::EquipIdMaker::Reset(param_1 + 0xf2) (+0x3C8) -- PORTED as
//      of wave 7. PlayerData gained the counter; see the field note there.
// ---------------------------------------------------------------------------
static void PlayerData_Teardown(PlayerData& Slot) {
    PlayerDB& Db = Players;

    // (a) the outbound request, .text:06d49236 .. .text:06d4929b. No bounds
    //     checks anywhere: Players is indexed raw (IMUL 0x418 over the array,
    //     disp -0x34 = +0x3e4) and through the unchecked PlayerDB::operator[],
    //     and IClient is called with no null test.
    if (Slot.trade_request_partner != 0) {
        unsigned int const Partner = Slot.trade_request_partner;
        // .text:06d49254 .. .text:06d4927c -- SendCommonPlayerTradeStopTradeRequest(partner,
        // Players[slot->client_id].+0x3e4).
        g_pClientInterface->SendCommonPlayerTradeStopTradeRequest(Partner,
                                                                  Db[Slot.client_id].ship_object_id);
        // .text:06d49282 .. .text:06d49296 -- FUN_06d571a0 = list::remove(value)
        // on Players[partner]+0x3d4: EVERY node equal to our client id goes.
        std::vector<unsigned int>& PartnerInbound = Db[Partner].inbound_trade_requests;
        for (size_t Index = 0; Index < PartnerInbound.size();) {
            if (PartnerInbound[Index] == Slot.client_id) {
                PartnerInbound.erase(PartnerInbound.begin()
                                     + static_cast<std::ptrdiff_t>(Index));
            } else {
                ++Index;
            }
        }
        // .text:06d4929b
        Slot.trade_request_partner = 0;
    }

    // (b) every inbound request, .text:06d4929d .. .text:06d4930f: each node's
    //     session indexes Players raw; a requester whose own +0x3d0 is set gets
    //     SendCommonPlayerTradeStopTradeRequest(its +0x3d0, Players[its client_id].+0x3e4)
    //     and +0x3d0 = 0.
    for (size_t Index = 0; Index < Slot.inbound_trade_requests.size(); ++Index) {
        PlayerData& RequesterSlot = Db[Slot.inbound_trade_requests[Index]];
        if (RequesterSlot.trade_request_partner == 0) continue;
        g_pClientInterface->SendCommonPlayerTradeStopTradeRequest(RequesterSlot.trade_request_partner,
                                                                  Db[RequesterSlot.client_id].ship_object_id);
        RequesterSlot.trade_request_partner = 0;
    }
    // .text:06d49311 .. .text:06d49326 -- FUN_06cf7720 erases the whole list.
    Slot.inbound_trade_requests.clear();

    // (c) the reputation vibe, .text:06d4932b .. .text:06d49346.
    if (Slot.rep_vibe != -1) {
        Reputation::Vibe::Free(Slot.rep_vibe);
        Slot.rep_vibe = -1;
    }

    // The field wipe, in retail order (EBX = 0 throughout).
    // .text:06d4934c .. 06d49358 -- REP STOSD 0x14 dwords of 0xcdcdcdcd over the
    // 0x50-byte AccountName.
    std::memset(Slot.AccountName, 0xcd, sizeof(Slot.AccountName));
    // .text:06d4935a -- FUN_06d57c70(+0x50): the saved-character map's clear.
    Slot.saved_characters.clear();
    // .text:06d49362 / 06d49368 -- +0x264 / +0x268 = 0.
    Slot.ship_archetype = 0;
    Slot.relative_health = 0.0f;
    // .text:06d4936e .. 06d493a4 -- COMMON std::list<>::erase(begin, end) on the
    // EquipDescList at +0x278, then on the collision-group list at +0x26c.
    Slot.equipment.Items.clear();
    Slot.collision_groups.clear();
    // .text:06d493a4 .. 06d493b4 -- 0xd dwords of 0xcdcdcdcd over com_costume
    // (+0x28c, the whole 0x34-byte Costume).
    std::memset(&Slot.com_costume, 0xcd, sizeof(Slot.com_costume));
    // .text:06d493b6 / 06d493bc -- voice length 0, voice[0] = 0.
    Slot.voice_length = 0;
    Slot.voice[0] = '\0';
    // .text:06d493c2 .. 06d493cd -- the same 0xcd fill over costume (+0x2e4).
    std::memset(&Slot.costume, 0xcd, sizeof(Slot.costume));
    // .text:06d493cf .. 06d49416 -- initial_pos = 0, initial_ornt = identity
    // (1.0f at +0x35c / +0x36c / +0x37c, every other cell 0).
    for (int Axis = 0; Axis < 3; ++Axis)
        Slot.initial_pos[Axis] = 0.0f;
    for (int Cell = 0; Cell < 9; ++Cell)
        Slot.initial_ornt[Cell] = (Cell % 4 == 0) ? 1.0f : 0.0f;
    // .text:06d49424 -- cash = 0x29a (666), the retail fill value.
    Slot.cash = 0x29a;
    // .text:06d4942e .. 06d4943a -- the three mission counters.
    Slot.num_kills = 0;
    Slot.num_mission_successes = 0;
    Slot.num_mission_failures = 0;
    // .text:06d49440 .. 06d49452 -- +0x340 byte, +0x344, +0x348 (client id),
    // +0x34c byte.
    Slot.bSkipNextAutosave = 0;
    Slot.save_failure_count = 0;
    Slot.client_id = 0;
    Slot.bCheatReported = 0;

    // (d) .text:06d4941c / 06d49458 / 06d4945a -- `if (+0x400 != 0)
    //     account->+0x34 = 0`: CAccount::m_ClientID, the account is marked
    //     logged out. +0x400 itself is NOT cleared.
    CAccount* Account = static_cast<CAccount*>(Slot.account_ptr);
    if (Account != nullptr) {
        Account->m_ClientID = 0;
    }

    // (e) SubObjectID::EquipIdMaker::Reset on the maker retail keeps at
    //     slot+0x3C8. Reset is one instruction in Common.dll --
    //       .text:06334590  66c7012100  MOV word ptr [this],0x21
    //       .text:06334595  c3          RET
    //     .text:06d4945d LEA ECX,[ESI + 0x3c8] / .text:06d49463 CALL
    //     [->COMMON.DLL::SubObjectID::EquipIdMaker::Reset].
    Slot.equip_id_maker.Reset();

    // .text:06d4946e .. 06d4949e -- +0x3e0, +0x3e4, +0x3e8, +0x3ec, +0x3f4,
    // +0x3f0, +0x3f8, +0x3fc, +0x410 = 0.
    Slot.system_id = 0;
    Slot.ship_object_id = 0;
    Slot.created_ship_id = 0;
    Slot.current_base = 0;
    Slot.entered_base_id = 0;
    Slot.last_base_id = 0;
    Slot.current_location = 0;
    Slot.Field_0x3FC = 0;
    Slot.exited_base_id = 0;
    // .text:06d49469 / 06d494a4 / 06d494aa -- +0x414 = CreateID("NO WHERE").
    Slot.starting_ring = CreateID("NO WHERE");
    // .text:06d494b3 -- byte +0x64 = 0 (CHARACTER_ID::invalidate inlined).
    Slot.selected_character.invalidate();
    // .text:06d494b6 / 06d494bc -- +0x404 (group) and +0x40c = 0; +0x408 is
    // left alone.
    Slot.group_ptr = nullptr;
    Slot.group_mission_set_by = 0;
    // .text:06d494c2 .. 06d494d7 -- FUN_06d57540(begin, end) on the +0x3a0
    // visit map: erase of the whole range.
    Slot.visit_map.clear();
    // Not touched by retail: rank_level, +0x288, total_earned, the equipment
    // snapshot, weapon_group_name, mission_log, interface_state, the pending
    // visits vector, difficulty_scale, trade_offer, group_mission_id.
}

void PlayerDB::logout(unsigned int SessionId) {
    if (SessionId == 0 || SessionId > m_MaxPlayers || !m_PlayerArray) return;
    PlayerData& Slot = m_PlayerArray[SessionId - 1];

    // (1) MP only (retail 0x06d543f7: CALL SinglePlayer / TEST AL,AL / JNZ skip,
    //     then `MOV ECX,EBX / CALL FUN_06d4c430` with EBX = &slot).
    //
    //     CORRECTION to the previous note on this item, which called
    //     FUN_06d4c430 "the equipment-commit pass ... EquipDescList at
    //     slot+0x278 ... EquipIdMaker at slot+0x3c8 ... IClient vtable +0x60 ...
    //     event 0". None of that is in the function. FUN_06d4c430 (0x06d4c430,
    //     Server.dll.c 72990) is the SAVE-GAME writer -- the same one pub::Save
    //     (server_pub.cpp) already documents as "retail FUN_06d4c430: auto-save":
    //       * !IsMPServer(): if the current base does not forbid autosave and
    //         [slot+0x340] is clear, safe_strcpy(name, 0x200, "AutoSave") and
    //         tail-call FUN_06d4c800(slot, name, 0, 0x65c) -- the serializer.
    //         Otherwise clear [slot+0x340] and return.
    //       * IsMPServer(): look the SELECTED CHARACTER_ID at slot+0x64 up in
    //         the save-record container at slot+0x50
    //         (FUN_06d5a180(slot+0x50, (CHARACTER_ID*)(slot+0x64))), and write
    //         that character's own file.
    //     So logout step 1 is "on an MP server, flush this session's character
    //     to disk before tearing the slot down".
    //     2026-09-24 (claude:srv-save): FUN_06d4c430 is ported
    //     (PlayerData_AutoSave, server_savegame.cpp) and this is its call,
    //     unconditionally, exactly as .text:06d54401 makes it -- its own
    //     IsMPServer / +0x64 / +0x3e0 gates decide whether anything is written.
    if (!SinglePlayer()) {
        PlayerData_AutoSave(Slot);
    }

    // (2) the same two teardown steps cleanup() runs, in the same order.
    PlayerDB_LeaveSystem(Slot, SessionId);
    PlayerDB_LeaveBase(Slot, SessionId);

    // (3) despawn event 0x3a, same guard (TEST EAX,0x3fffffff) and the same
    //     three-dword payload as cleanup. Vestigial for the same reason.
    unsigned int SystemField = Slot.system_id;
    if ((SystemField & 0x3fffffff) != 0) {
        unsigned int Payload[3];
        Payload[0] = SystemField;
        Payload[1] = 0;
        Payload[2] = SessionId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);
    }

    // (4) TerminateTrade(sid, 4) when a trade is live -- PUSH 4 / PUSH EDI /
    // MOV ECX,.data:Server / CALL IServerImpl::TerminateTrade (06d54462..6a),
    // no null test.
    if (Slot.trade_offer != nullptr) {
        GetServerInterface()->TerminateTrade(SessionId, 4);
    }

    // (5) the logout event: FUN_06d43650(8, &sid) -- a ONE-dword payload, not
    //     the three-dword despawn record.
    unsigned int LogoutPayload = SessionId;
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLogout, &LogoutPayload);

    // (6) the global counter at .data:DAT_06d8f9a0 (GServerStats + 0x08).
    ++GServerStats.LogoutCount;

    // (7) tell the player's group, if any. Retail writes it as `_Str + 0x202`
    //     with _Str a wchar_t*, i.e. BYTE offset 0x404 -- PlayerData::group_ptr.
    //     An earlier note on this item read that as slot+0x808; it is not.
    if (Slot.group_ptr != nullptr) {
        static_cast<CPlayerGroup*>(Slot.group_ptr)->HandleClientLogout(SessionId);
    }

    // (8) tell every OTHER occupied session to drop this player from its player
    //     list. Retail 0x06d5449f: `(!IsMPServer() || slot->selected_character[0])
    //     && this->MaxPlayers != 0`, then a 1..MaxPlayers walk that skips self,
    //     bounds each index against .data:DAT_06d90264 (== &Players + 4,
    //     MaxPlayers again) and requires [otherSlot+0x348] != 0, calling IClient
    //     vtable +0x1bc -- slot 111, ch2 op 0x52 FLPACKET_SERVER_PLAYERLIST --
    //     as SendServerPlayerListRemove(otherSessionId, SessionId).
    //     `MOV DL,[EBX+0x64] / TEST DL,DL / SETNZ AL` is the first byte of the
    //     CHARACTER_ID at +0x64: "this session actually picked a character".
    if ((!IsMPServer() || Slot.selected_character.is_valid())
        && m_MaxPlayers != 0 && g_pClientInterface) {
        for (unsigned int Other = 1; Other <= m_MaxPlayers; ++Other) {
            if (Other == SessionId) continue;
            if (m_PlayerArray[Other - 1].client_id == 0) continue;
            g_pClientInterface->SendServerPlayerListRemove(Other, SessionId);
        }
    }

    // (9) look the account up by the login name stored at slot+0x00 and, on an
    //     MP server, log it. Retail's map is .data:DAT_06d902a4 == &Players +
    //     0x44, i.e. m_AccountNameMap.
    CiWString LogoutName(Slot.AccountName);
    CAccount* Account = FindAccountFromName(LogoutName);
    if (Account != nullptr && !SinglePlayer()) {
        // Retail's second field is *(char**)(account + 0x18) -- the _Ptr of the
        // narrow std::string at CAccount+0x14 that FUN_06d48e80 fills with the
        // account DIRECTORY PATH at login. It is a real member now
        // (CAccount::m_DirectoryPath). Retail's own null guard is the MSVCP60
        // `_Nullstr` fallback at 0x06d545bc, which is the empty string.
        FDUMP(0x100003, "LOG: Logout on account %S (\"%s\")\n",
              Slot.AccountName, Account->m_DirectoryPath.c_str());
    }

    // (10) the slot teardown, retail FUN_06d49230(slot) = PlayerData_Teardown
    //      (trade-request cancellations, Vibe::Free, the field wipe, the
    //      account's client-id clear). Clearing client_id (+0x348) is what frees
    //      the slot for reuse.
    PlayerData_Teardown(Slot);
}

// ===========================================================================
// PlayerData_DeselectCharacter -- retail FUN_06d4a4a0 (.text:06d4a4a0 ..
// .text:06d4a5c1), thiscall on the slot, (SessionId), `RET 4`. Leaving the
// world without logging out: the same system / base / trade teardown as
// logout, then the player-list removal and event 7, then the selection is
// dropped. `this` (EBP) is only read for +0x64; everything else goes through
// ESI = Players + SessionId*0x418 - 0x418 (.text:06d4a4c7).
// ===========================================================================
void __cdecl PlayerData_DeselectCharacter(PlayerData& Slot, unsigned int SessionId) {
    // .text:06d4a4a7 / .text:06d4a4ce -- single player, or nothing selected.
    if (SinglePlayer())
        return;
    PlayerData& Owner = Players[SessionId];
    if (!Slot.selected_character.is_valid())
        return;

    // .text:06d4a4dd FUN_06d00010(sid) ; .text:06d4a4e3 FUN_06d18880(sid)
    PlayerDB_LeaveSystem(Owner, SessionId);
    PlayerDB_LeaveBase(Owner, SessionId);

    // .text:06d4a4e8 .. .text:06d4a50f -- event 0x3a {system, 0, sid}, when
    // +0x3e0 still holds object bits (vestigial, as in cleanup / logout).
    unsigned int const SystemField = Owner.system_id;
    if ((SystemField & 0x3fffffff) != 0) {
        unsigned int Payload[3];
        Payload[0] = SystemField;
        Payload[1] = 0;
        Payload[2] = SessionId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);
    }

    // .text:06d4a517 .. .text:06d4a529 -- +0x3cc non-null: the global Server's
    // IServerImpl::TerminateTrade (FUN_06cf1990, ECX = .data:06d8facc)(sid, 4).
    if (Owner.trade_offer != nullptr)
        GetServerInterface()->TerminateTrade(SessionId, 4);

    // .text:06d4a52e .. .text:06d4a535 -- event 8, the one-dword &SessionId.
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLogout, &SessionId);

    // .text:06d4a53a .. .text:06d4a54c -- CPlayerGroup::HandleClientLogout
    // (FUN_06d5bd60) on +0x404.
    if (Owner.group_ptr != nullptr)
        static_cast<CPlayerGroup*>(Owner.group_ptr)->HandleClientLogout(SessionId);

    // .text:06d4a551 .. .text:06d4a5a6 -- every other occupied session
    // (1..MaxPlayers inclusive, client_id != 0) gets IClient +0x1bc (SendServerPlayerListRemove,
    // FLPACKET_SERVER_PLAYERLIST)(other, sid). No null test on the interface.
    unsigned int const MaxPlayers = Players.GetMaxPlayerCount();
    for (unsigned int Other = 1; Other <= MaxPlayers; ++Other) {
        if (!Players.is_valid(Other) || Other == SessionId)
            continue;
        g_pClientInterface->SendServerPlayerListRemove(Other, SessionId);
    }

    // .text:06d4a5a8 .. .text:06d4a5af -- event 7, &SessionId.
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLogin, &SessionId);

    // .text:06d4a5b7 -- MOV byte ptr [EBP + 0x64],0: the selection is gone.
    Slot.selected_character.Buffer[0] = '\0';
}

// ===========================================================================
// FUN_06d49960 (.text:06d49960 .. .text:06d499b1, cdecl, two record pointers):
// the character list's sort order. True when New goes BEFORE Existing:
//   * the Restart.fl flags differ -> New.bIsRestart != 0 (.text:06d49968);
//   * the AutoSave.fl flags differ -> New.bIsAutoSave != 0 (.text:06d4997c);
//   * otherwise CompareFileTime(&New+0x228, &Existing+0x228) == 1, i.e. New is
//     the more recent save; -1 and 0 are both false (.text:06d4999d .. 06d499ae).
// ===========================================================================
static bool CharacterInfo_GoesBefore(CCharacterSet::CharacterInfo const& New,
                                     CCharacterSet::CharacterInfo const& Existing) {
    if (New.bIsRestart != Existing.bIsRestart)
        return New.bIsRestart != 0;
    if (New.bIsAutoSave != Existing.bIsAutoSave)
        return New.bIsAutoSave != 0;
    FILETIME NewTime;
    NewTime.dwLowDateTime = New.TimestampLow;
    NewTime.dwHighDateTime = New.TimestampHigh;
    FILETIME ExistingTime;
    ExistingTime.dwLowDateTime = Existing.TimestampLow;
    ExistingTime.dwHighDateTime = Existing.TimestampHigh;
    LONG const Order = CompareFileTime(&NewTime, &ExistingTime);
    if (Order == -1)
        return false;
    return Order == 1;
}

// ===========================================================================
// PlayerData_BuildCharacterSet -- retail FUN_06d499c0 (.text:06d499c0 ..
// .text:06d49f4f), thiscall on the slot, one argument (the CCharacterSet at
// IServerImpl::CharacterInfoReq's ESP+0xc), `RET 4`. Its only caller is
// CharacterInfoReq (.text:06d452aa), when that call's bool is false.
// ===========================================================================
void __cdecl PlayerData_BuildCharacterSet(PlayerData& Slot, CCharacterSet& Out) {
    // .text:06d499eb .. .text:06d49a05 -- IsMPServer() twice: an MP server with
    // a selected character (the byte test on +0x64) leaves the world first.
    if (IsMPServer() && (!IsMPServer() || Slot.selected_character.Buffer[0] != '\0')) {
        // .text:06d49a09 -- FUN_06d4c430, the autosave.
        PlayerData_AutoSave(Slot);

        // .text:06d49a0e .. .text:06d49a23 -- the session id is the slot's own
        // +0x348, and EDI is Players[that id] recomputed from it.
        unsigned int const SessionId = Slot.client_id;
        PlayerData& Owner = Players[SessionId];

        // .text:06d49a2a FUN_06d00010(sid) ; .text:06d49a30 FUN_06d18880(sid)
        PlayerDB_LeaveSystem(Owner, SessionId);
        PlayerDB_LeaveBase(Owner, SessionId);

        // .text:06d49a35 .. .text:06d49a5a -- event 0x3a {system, 0, sid}.
        unsigned int const SystemField = Owner.system_id;
        if ((SystemField & 0x3fffffff) != 0) {
            unsigned int Payload[3];
            Payload[0] = SystemField;
            Payload[1] = 0;
            Payload[2] = SessionId;
            Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);
        }

        // .text:06d49a62 .. .text:06d49a72 -- IServerImpl::TerminateTrade on the
        // global Server, (sid, 4), when [EDI+0x3cc] is set.
        if (Owner.trade_offer != nullptr)
            GetServerInterface()->TerminateTrade(SessionId, 4);
    }

    // .text:06d49a7b .. .text:06d49aa4 -- the two special file names,
    // sprintf'd with "%s.fl" (.rdata:06d6b438).
    char RestartName[0x104];
    char AutoSaveName[0x104];
    sprintf(RestartName, "%s.fl", "Restart");                           // local_214
    sprintf(AutoSaveName, "%s.fl", "AutoSave");                         // local_110

    // .text:06d49aa6 .. .text:06d49f1c -- every record in the container at
    // +0x50, in key order (the map's begin at [head], FUN_06d59620 = ++).
    for (std::map<CHARACTER_ID, PlayerData::FSavedCharacter>::const_iterator It =
             Slot.saved_characters.begin();
         It != Slot.saved_characters.end(); ++It) {
        PlayerData::FSavedCharacter const& Saved = It->second;         // EDI = node + 0x20c

        // .text:06d49abf operator new(0x2dc) and the inlined constructor.
        CCharacterSet::CharacterInfo* Info = new CCharacterSet::CharacterInfo();

        Info->Id = It->first;                                           // .text:06d49bdc
        // .text:06d49bee / .text:06d49c05 -- DACOM stricmp on the record's
        // file name (record+0x0).
        Info->bIsRestart = static_cast<unsigned char>(_stricmp(RestartName, Saved.FileName) == 0);
        Info->bIsAutoSave = static_cast<unsigned char>(_stricmp(AutoSaveName, Saved.FileName) == 0);
        Info->Name = Saved.Name;                                        // .text:06d49c2d
        Info->Description = Saved.Description;                          // .text:06d49c4c
        Info->DescripStrId = Saved.DescripStrId;                        // .text:06d49c58
        Info->TimestampLow = Saved.TimestampLow;                        // .text:06d49c64
        Info->TimestampHigh = Saved.TimestampHigh;                      // .text:06d49c70

        // .text:06d49c76 .. .text:06d49cc0 -- the autosave's description is
        // replaced by .rdata:06d6b2f8, which is L"" (four zero bytes: wcslen 0,
        // _Grow(0, true) empties the string and returns false), and its strid
        // by 0x65c.
        if (Info->bIsAutoSave != 0) {
            Info->Description = L"";
            Info->DescripStrId = 0x65c;
        }

        Info->ShipArchetype = Saved.ShipArchetype;                      // .text:06d49cd2
        Info->Money = Saved.Money;                                      // .text:06d49cde
        Info->NumKills = Saved.NumKills;                                // .text:06d49cea
        Info->NumMissionSuccesses = Saved.NumMissionSuccesses;          // .text:06d49cf6
        Info->NumMissionFailures = Saved.NumMissionFailures;            // .text:06d49d02
        Info->HullStatus = Saved.HullStatus;                            // .text:06d49d14
        Info->Equipment = Saved.Equipment;                              // .text:06d49d21
        Info->CollisionGroups = Saved.CollisionGroups;                  // .text:06d49d27 .. 06d49da3
        Info->SystemId = Saved.SystemId;                                // .text:06d49db1 <- +0x284
        Info->BaseId = Saved.BaseId;                                    // .text:06d49dbd <- +0x278
        Info->LocationId = Saved.LocationId;                            // .text:06d49dd0 <- +0x280
        Info->VoiceId = CreateID(Saved.Voice.Data);                     // .text:06d49dd6 <- +0x30c
        Info->Rank = Saved.Rank;                                        // .text:06d49de8 <- +0x2bc
        CharacterInfoCopyCostume(Info->BaseCostume, Saved.BaseCostume); // .text:06d49dee <- +0x32c
        CharacterInfoCopyCostume(Info->ComCostume, Saved.ComCostume);   // .text:06d49e3a <- +0x2d4

        // .text:06d49e81 .. .text:06d49f1c -- sorted insert. Walk the list with
        // the traverse idiom; the first node the new record goes before ends
        // the walk and the record is linked after the previous node
        // (insert_after: prepend when there is none, FUN_06d5b0b0 at
        // .text:06d49ebe; append when the previous node is the tail,
        // .text:06d49ed5; else the inline link .text:06d49eda .. 06d49ef1).
        // Running off the end appends (FUN_06d5a930, .text:06d49f0a).
        MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
        MetaNode<CCharacterSet::CharacterInfo>* Previous = nullptr;
        bool bPlaced = false;
        while (Out.Characters.traverse(Node, true)) {
            if (CharacterInfo_GoesBefore(*Info, *Node->Data)) {
                Out.Characters.insert_after(Previous, Info);
                bPlaced = true;
                break;
            }
            Previous = Node;
        }
        if (!bPlaced)
            Out.Characters.append(Info);
    }

    // .text:06d49f21 .. .text:06d49f39 -- the failed-save counter travels with
    // the list and is reset.
    Out.SaveFailureCount = Slot.save_failure_count;
    Slot.save_failure_count = 0;
}

// ===========================================================================
// PlayerData_SelectCharacter -- retail FUN_06d4a5d0 (.text:06d4a5d0 ..
// .text:06d4b1fa), thiscall on the slot, (CHARACTER_ID const&, SessionId),
// returns AL. The only caller is IServerImpl::CharacterSelect (.text:06d45461).
// This is the head and the MP-only front (.text:06d4a5d0 .. .text:06d4aa85);
// the shared tail is PlayerData_LoadSelectedCharacter (server_savegame.cpp).
//
// Frame (S = ESP after the ESI/EDI pushes at .text:06d4a638): the CHARACTER_ID
// at S+0x1920, the session at S+0x1924. EBX is `this`; the session's own slot
// Players[sid] is recomputed from the argument (.text:06d4a605 -> S+0x20,
// .text:06d4a6ab -> EDI). S+0x18 is this session's group id, S+0x10 the
// record's wide name, S+0x1c the MaxPlayers snapshot, S+0x14 i*0x418.
// ===========================================================================
bool __cdecl PlayerData_SelectCharacter(PlayerData& Slot, CHARACTER_ID const& Id,
                                        unsigned int SessionId) {
    // .text:06d4a61a .. .text:06d4a633 -- FUN_06d57d10 = find(Id) in the record
    // container at +0x50; end -> return 0.
    std::map<CHARACTER_ID, PlayerData::FSavedCharacter>::iterator Found =
        Slot.saved_characters.find(Id);
    if (Found == Slot.saved_characters.end())
        return false;
    PlayerData::FSavedCharacter const& Record = Found->second;     // EBP = node + 0x20c

    // .text:06d4a646 / .text:06d4a648 -- SinglePlayer() twice (the first result
    // is discarded); single player skips the whole front.
    SinglePlayer();
    if (!SinglePlayer()) {
        PlayerData& Owner = Players[SessionId];                      // S+0x20

        // .text:06d4a659 .. .text:06d4a68f -- S+0x18: this session's group id,
        // is_valid(sid) && [slot+0x404] != 0 ? group->m_GroupID (+0x04) : 0.
        unsigned int OwnGroupId = 0;
        if (Players.is_valid(SessionId) && Owner.group_ptr != nullptr)
            OwnGroupId = static_cast<CPlayerGroup*>(Owner.group_ptr)->GetID();

        // .text:06d4a697 -- only when a character is already selected.
        if (Slot.selected_character.Buffer[0] != '\0') {
            // .text:06d4a6b2 FUN_06d00010(sid) ; .text:06d4a6b8 FUN_06d18880(sid)
            PlayerDB_LeaveSystem(Owner, SessionId);
            PlayerDB_LeaveBase(Owner, SessionId);

            // .text:06d4a6bd .. .text:06d4a6e4 -- event 0x3a {system, 0, sid}.
            unsigned int const SystemField = Owner.system_id;
            if ((SystemField & 0x3fffffff) != 0) {
                unsigned int Payload[3];
                Payload[0] = SystemField;
                Payload[1] = 0;
                Payload[2] = SessionId;
                Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);
            }

            // .text:06d4a6ec .. .text:06d4a6fe -- IServerImpl::TerminateTrade on the
            // global Server (.data:Server), (sid, 4).
            if (Owner.trade_offer != nullptr)
                GetServerInterface()->TerminateTrade(SessionId, 4);

            // .text:06d4a703 .. .text:06d4a71a -- Common's operator!=(Id, +0x64):
            // re-selecting the same character skips the player-list removal.
            if (Id != Slot.selected_character) {
                // .text:06d4a720 .. .text:06d4a72a -- event 8, &sid.
                Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLogout, &SessionId);

                // .text:06d4a72f .. .text:06d4a74d -- CPlayerGroup::HandleClientLogout
                // on Players[sid]+0x404, and the cached group id drops to 0.
                if (Owner.group_ptr != nullptr) {
                    static_cast<CPlayerGroup*>(Owner.group_ptr)->HandleClientLogout(SessionId);
                    OwnGroupId = 0;
                }

                // .text:06d4a755 .. .text:06d4a7b4 -- every other occupied session
                // (1..MaxPlayers inclusive) gets IClient +0x1bc (SendServerPlayerListRemove)(i, sid).
                unsigned int const MaxPlayers = Players.GetMaxPlayerCount();
                for (unsigned int Other = 1; Other <= MaxPlayers; ++Other) {
                    if (!Players.is_valid(Other) || Other == SessionId)
                        continue;
                    g_pClientInterface->SendServerPlayerListRemove(Other, SessionId);
                }

                // .text:06d4a7b6 .. .text:06d4a7c0 -- event 7, &sid.
                Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLogin, &SessionId);
            }
        }

        // .text:06d4a7c8 .. .text:06d4a7d3 -- CHARACTER_ID::operator=(+0x64, Id).
        Slot.selected_character = Id;

        // .text:06d4a7d9 .. .text:06d4a7e9 -- IClient +0x1c0 (SendServerPlayerListClear)(sid).
        g_pClientInterface->SendServerPlayerListClear(SessionId);

        // .text:06d4a7ef .. .text:06d4a80f -- S+0x10: the record's wide name
        // (record+0x204, the MSVCP60 _Nullstr when empty).
        wchar_t const* const RecordName = Record.Name.c_str();

        // .text:06d4a7f7 .. .text:06d4aa7f -- the reputation / player-list
        // exchange with every other session, i = 1..MaxPlayers (S+0x1c).
        unsigned int const MaxPlayers = Players.GetMaxPlayerCount();
        for (unsigned int Other = 1; Other <= MaxPlayers; ++Other) {
            // .text:06d4a821 .. .text:06d4a850 -- i != 0, i-1 < MaxPlayers,
            // [slot_i+0x348] != 0.
            if (!Players.is_valid(Other))
                continue;
            PlayerData& OtherSlot = Players[Other];                  // S+0x28
            // .text:06d4a863 .. .text:06d4a87b -- on an MP server, only sessions
            // that have picked a character.
            if (IsMPServer() && OtherSlot.selected_character.Buffer[0] == '\0')
                continue;

            // .text:06d4a881 .. .text:06d4a8a7 -- GetRank(copy of [slot_i+0x318],
            // S+0x20 = 0).
            int OtherVibe = OtherSlot.rep_vibe;                      // S+0x24
            unsigned int OtherRank = 0;                              // S+0x20
            ::Reputation::Vibe::GetRank(OtherVibe, OtherRank);

            // .text:06d4a8ad .. .text:06d4a8e4 -- S+0x24: slot_i's group id, with
            // the same is_valid bounds and a +0x404 null test, else 0.
            unsigned int OtherGroupId = 0;
            if (Players.is_valid(Other) && OtherSlot.group_ptr != nullptr)
                OtherGroupId = static_cast<CPlayerGroup*>(OtherSlot.group_ptr)->GetID();

            // .text:06d4a8ec -- tell the selecting session about everyone else.
            if (Other != SessionId) {
                // .text:06d4a8f9 .. .text:06d4a97a -- two FmtStr locals (FUN_06d55bc0),
                // name defaults to L">>>BAD   NAME<<<" (.data:06d8d40c, three
                // spaces) and is reset to it when GetName(&[slot_i+0x318], ...)
                // returns -2.
                static wchar_t const BadName[] = L">>>BAD   NAME<<<";
                wchar_t const* OtherName = BadName;
                {
                    FmtStr NameA;                                    // S+0x84
                    FmtStr NameB;                                    // S+0x6c
                    int const NameResult = ::Reputation::Vibe::GetName(
                        OtherSlot.rep_vibe, NameA, NameB, OtherName);
                    if (NameResult == -2)
                        OtherName = BadName;
                }
                // .text:06d4a980 .. .text:06d4a9ee
                g_pClientInterface->SendServerPlayerListAdd(SessionId, OtherName, Other, true);   // +0x1b8
                g_pClientInterface->SendServerMiscObjUpdatePlayerGroup(SessionId, Other, OtherGroupId); // +0x1d8
                g_pClientInterface->SendServerMiscObjUpdateRank(SessionId, Other,
                                                                static_cast<int>(OtherRank));   // +0x1d4
                g_pClientInterface->SendServerMiscObjUpdateSystem(SessionId, Other,
                                                                  OtherSlot.system_id);         // +0x1dc
            }

            // .text:06d4a9f4 .. .text:06d4aa64 -- tell session i (the selecting
            // one included) about the new character, from the RECORD: its name,
            // this session's group id (S+0x18), record+0x2bc rank, record+0x284
            // system.
            g_pClientInterface->SendServerPlayerListAdd(Other, RecordName, SessionId, false); // +0x1b8
            g_pClientInterface->SendServerMiscObjUpdatePlayerGroup(Other, SessionId, OwnGroupId); // +0x1d8
            g_pClientInterface->SendServerMiscObjUpdateRank(Other, SessionId,
                                                            static_cast<int>(Record.Rank));   // +0x1d4
            g_pClientInterface->SendServerMiscObjUpdateSystem(Other, SessionId, Record.SystemId); // +0x1dc
        }
    }

    // .text:06d4aa85 .. .text:06d4b1fa -- the tail both paths share.
    return PlayerData_LoadSelectedCharacter(Slot, Record, SessionId);
}

// ===========================================================================
// PlayerData_DestroyCharacter -- retail FUN_06d49f60 (.text:06d49f60 ..
// .text:06d4a1d4), thiscall on the slot, (CHARACTER_ID const&, SessionId),
// `RET 8`. The only caller is IServerImpl::DestroyCharacter (.text:06d456f5).
// ===========================================================================
bool __cdecl PlayerData_DestroyCharacter(PlayerData& Slot, CHARACTER_ID const& Id,
                                         unsigned int SessionId) {
    // .text:06d49f8b -- deleting the selected character deselects it first.
    if (Slot.selected_character == Id)
        PlayerData_DeselectCharacter(Slot, SessionId);

    // .text:06d49fb0 -- FUN_06d57d10 = find in the records at +0x50.
    std::map<CHARACTER_ID, PlayerData::FSavedCharacter>::iterator It =
        Slot.saved_characters.find(Id);
    if (It == Slot.saved_characters.end())
        return false;                                                   // .text:06d49fbe
    PlayerData::FSavedCharacter& Record = It->second;

    // .text:06d49fc4 .. .text:06d49ff6 -- sprintf("%s.fl", "Restart") and
    // DACOM stricmp against the record's file name: Restart.fl is never
    // deleted. (_stricmp: DACOM's stricmp is not declared in src/include.)
    char RestartName[0x200];
    sprintf(RestartName, "%s.fl", "Restart");
    if (_stricmp(Record.FileName, RestartName) == 0)
        return false;

    // .text:06d49ffe .. .text:06d4a0b2 -- <accts>\<account dir>\<file name>:
    // the inline FUN_06d46370 hash in MP (_wcsdup/_wcslwr/wcslen, "%02x-%08x")
    // or wcstombs(.., 0x104) in SP, FUN_06d460f0's root, then the same buffer
    // _makepath'd again with the record's file name and no extension.
    char AccountDirName[0x104];
    if (IsMPServer()) {
        Server_AccountDirName(AccountDirName, Slot.AccountName);
    } else {
        wcstombs(AccountDirName, Slot.AccountName, 0x104);
    }
    // Retail reuses one buffer as both the second _makepath's dir and its
    // output; two buffers here, because the modern debug CRT may fill the
    // destination before reading the source. Same resulting string.
    char AccountDir[0x104];
    _makepath(AccountDir, nullptr, Server_GetAccountsRoot(), AccountDirName, nullptr);
    char Path[0x104];
    _makepath(Path, nullptr, AccountDir, Record.FileName, nullptr);

    // .text:06d4a0b5 -- _strlwr on the RECORD's own file name, in place.
    _strlwr(Record.FileName);

    // .text:06d4a0c2 .. .text:06d4a157 -- the global character-id map at
    // .data:06d90290 (Players.m_CharacterIDMap), keyed by that lowered name:
    // find (FUN_06d571f0), CAccount::DeleteCharacterFromID (FUN_06d56bd0) on
    // node+0x1c when non-null, then erase-by-key (FUN_06d581c0). That is
    // PlayerDB::DeleteCharacterFromID's body.
    CiString CharacterKey(Record.FileName);
    Players.DeleteCharacterFromID(CharacterKey);

    // .text:06d4a177 -- _unlink(path).
    _unlink(Path);

    // .text:06d4a18d -- FUN_06d57760: erase the record from +0x50.
    Slot.saved_characters.erase(It);

    // .text:06d4a192 .. .text:06d4a1b3 -- outside single player, log it.
    if (!SinglePlayer()) {
        FDUMP(0x100003, "LOG: Delete character on account %S (\"%s\")\n",
              Slot.AccountName, Path);
    }
    return true;                                                        // .text:06d4a1b8
}

const wchar_t* PlayerDB::GetActiveCharacterName(unsigned int SessionId) const {
    // Retail 0x06d54640, straight off the raw listing:
    //   06d54644  TEST ECX,ECX / JBE fail                 (session id must be > 0)
    //   06d54649  MOV  EDX,[.data:DAT_06d90264]           (== &Players + 4, MaxPlayers)
    //   06d5464f  LEA  EAX,[ECX + -1] / CMP EAX,EDX / JNC fail
    //   06d54656  MOV  EDX,[.data:Players]                (the slot array base)
    //   06d54662  MOV  ESI,[EAX + EDX + 0x348] / TEST ESI,ESI / JZ fail
    //   06d54673  LEA  ESI,[ECX*0x418 + EDX + 0xfffffbe8] (&slot[sid-1])
    //   06d5467a  CALL [COMMON.DLL::IsMPServer] / TEST AL,AL / JZ take_it
    //   06d54684  MOV  DL,[ESI+0x64] / TEST DL,DL / SETNZ AL / TEST AL,AL / JZ fail
    //   06d54690  MOV  ECX,ESI / CALL FUN_06d4eda0
    if (SessionId == 0 || SessionId > m_MaxPlayers || !m_PlayerArray) return nullptr;
    const PlayerData& Slot = m_PlayerArray[SessionId - 1];
    if (Slot.client_id == 0) return nullptr;
    // The MP gate the previous pass was missing. Retail 0x06d54684 is
    // `MOV DL,[ESI+0x64] / TEST DL,DL / SETNZ AL / TEST AL,AL / JZ fail`, i.e.
    // the first byte of the CHARACTER_ID at +0x64: on an MP server a session
    // that has not selected a character has no active character name to give.
    if (IsMPServer() && !Slot.selected_character.is_valid()) return nullptr;

    // CORRECTION to the note on this tracker item: FUN_06d4eda0 is NOT "a
    // pointer to the live wide character name inside the slot". Its whole body
    // (0x06d4eda0; the argument setup is at 06d4ee41-06d4ee65) is
    //     local_40 = u">>>BAD   NAME<<<";                 // .data:06d8d40c
    //     ADD ECX,0x318                                   // &slot->rep_vibe
    //     r = Reputation::Vibe::GetName(&vibe, fmt1, fmt2, &local_40);
    //     if (r == -2) local_40 = u">>>BAD   NAME<<<";
    //     return local_40;
    // -- the name is resolved through the REPUTATION system from the vibe handle
    // at slot+0x318, and the returned pointer aliases the Vibe entry's own
    // display-name buffer (or the static fallback). The port's narrow
    // character_name copied into a function-local `static wchar_t[64]` by
    // mbstowcs_s was wrong on both counts: wrong source and a shared buffer.
    static const wchar_t BadName[] = L">>>BAD   NAME<<<";
    const wchar_t* DisplayName = BadName;
    FmtStr Name1;
    FmtStr Name2;
    const int VibeId = Slot.rep_vibe;
    if (::Reputation::Vibe::GetName(VibeId, Name1, Name2, DisplayName) == -2) {
        DisplayName = BadName;
    }
    return DisplayName;
}

unsigned int PlayerDB::CountPlayersInSystem(int SystemId) {
    unsigned int Count = 0;
    if (!m_PlayerArray) return 0;
    for (unsigned int Index = 0; Index < m_MaxPlayers; ++Index) {
        const PlayerData& Slot = m_PlayerArray[Index];
        if (Slot.client_id != 0 && (int)Slot.system_id == SystemId) {
            Count++;
        }
    }
    return Count;
}

unsigned int PlayerDB::GetGroupID(unsigned int PlayerId) {
    if (!is_valid(PlayerId)) return 0;
    void* GroupPtr = m_PlayerArray[PlayerId - 1].group_ptr;
    if (GroupPtr != nullptr) {
        CPlayerGroup* Group = (CPlayerGroup*)GroupPtr;
        return Group->GetID();
    }
    return 0;
}

void PlayerDB::SendGroupID(unsigned int PlayerId, unsigned int GroupId) {
    // Retail (0x74890): iterate all player slots, for each valid player call
    // IClient vtable+0x1d8 with (player_id, PlayerId, GroupId)
    extern IClient* g_pClientInterface;
    if (!g_pClientInterface) return;

    unsigned int max_players = PlayerDB_GetMaxPlayers();
    if (max_players == 0) return;

    // vtable+0x1d8 is IClient slot 118 (SendServerMiscObjUpdatePlayerGroup). Call it as
    // the virtual it is: the
    // proxy's methods are __thiscall, so a hand-built __stdcall pointer with an
    // explicit self argument would pass this in the wrong place and unwind the
    // stack by one dword too few on every call.
    for (unsigned int i = 1; i <= max_players; i++) {
        if (!is_valid(i)) continue;
        g_pClientInterface->SendServerMiscObjUpdatePlayerGroup(i, PlayerId, GroupId);
    }
}

void PlayerDB::SendSystemID(unsigned int PlayerId, unsigned int SystemId) {
    // Retail (0x74900): iterate all player slots, for each valid player call
    // IClient vtable+0x1dc with (player_id, PlayerId, SystemId)
    extern IClient* g_pClientInterface;
    if (!g_pClientInterface) return;

    unsigned int max_players = PlayerDB_GetMaxPlayers();
    if (max_players == 0) return;

    // vtable+0x1dc is IClient slot 119 (SendServerMiscObjUpdateSystem) -- see the note in
    // SendGroupID.
    for (unsigned int i = 1; i <= max_players; i++) {
        if (!is_valid(i)) continue;
        g_pClientInterface->SendServerMiscObjUpdateSystem(i, PlayerId, SystemId);
    }
}

// ---------------------------------------------------------------------------
// PlayerDB::BuildLocalUserDir -- retail .text:06d518f0 .. .text:06d51954 (RET 0).
//   .text:06d518fd  IsMPServer() == 0 -> return (single player builds nothing)
//   .text:06d51908 .. 06d51936  GetUserDataPath + _makepath(DAT_06d90158, 0,
//                   local, IsMPServer() ? "Accts\MultiPlayer" : "Accts", 0) --
//                   the body of FUN_06d460f0 inlined, same global buffer
//   .text:06d51944  COMMON.DLL::BuildDirectory(DAT_06d90158)
// Ported 1:1 2026-09-24 (claude-code:w5-server-06d4); the port used to be empty.
// ---------------------------------------------------------------------------
void PlayerDB::BuildLocalUserDir() {
    if (!IsMPServer())                                                      // .text:06d518fd
        return;
    BuildDirectory(Server_GetAccountsRoot());                               // .text:06d51944
}

// ---------------------------------------------------------------------------
// PlayerDB::MakeLocalUserPath -- retail .text:06d51960 .. .text:06d519d1
// (thiscall, RET 8).
//   .text:06d5196e .. 06d519a0  FUN_06d460f0's body inlined: the accounts root
//                   into DAT_06d90158
//   .text:06d519c3  sprintf(OutPath, "%s\\%s", DAT_06d90158, FileName)
//                   (.rdata:s_%s\%s_06d6b89c)
//   .text:06d519c9  return true -- no null tests, no failure path
// Ported 1:1 2026-09-24 (claude-code:w5-server-06d4). The previous port joined
// the bare user-data path (no "Accts" segment) through _makepath_s, fell back to
// the bare file name on a GetUserDataPath failure and returned false on null
// arguments -- none of which retail does.
// ---------------------------------------------------------------------------
bool PlayerDB::MakeLocalUserPath(char* OutPath, const char* FileName) {
    char const* const AccountsRoot = Server_GetAccountsRoot();
    sprintf(OutPath, "%s\\%s", AccountsRoot, FileName);                     // .text:06d519c3
    return true;
}

CAccount* PlayerDB::FindAccountFromClientID(unsigned int ClientId) {
    // Retail (0x06d52b40) is eight instructions: if ClientId == 0 return null,
    // else return *(CAccount**)(Players.m_PlayerArray + ClientId*0x418 - 0x18),
    // i.e. slot[ClientId-1] + 0x400. It does NOT bounds-check and it does not
    // consult either account container.
    if (ClientId == 0) return nullptr;
    if (ClientId > m_MaxPlayers || !m_PlayerArray) return nullptr;
    return reinterpret_cast<CAccount*>(m_PlayerArray[ClientId - 1].account_ptr);
}

// ---------------------------------------------------------------------------
// Account/character hashes use Server_AccountCrc (server_pub.cpp, declared in
// server_savefile.h): retail's CRC engine DAT_06d8fd38 -- width 31, poly 0x8005,
// reflected, XorIn/XorOut 0 (LAB_06d46250), NOT the standard CRC-32. E.g.
// PlayerDB::FindAccountFromCharacterName (0x06d52b60) runs FUN_06d46850 over
// 2*wcslen bytes of the lowercased name and FUN_06d467b0, then "%02x-%08x".
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The account "name" file is obfuscated with a CONSTANT key. Retail
// PlayerDB::CreateAccount (0x06d523e0), PlayerDB::load_user_data (0x06d533a0)
// and CAccount::InitFromFolder (0x06d56940) all run the CRC engine over the
// fixed wide marker string behind PTR_DAT_06d8d158 (= .rdata:06d6ae80, bytes
// 41 00 4b 00 4c 00 21 00 38 00 31 00 36 00 37 00 66 00 47 00 42 00 41 00
// 30 00 37 00 31 00 32 00 00 00) and take the low 16 bits of the result.
// Because the key does not depend on the payload, this one IS reversible —
// unlike the password file, whose key is derived from the password itself.
// ---------------------------------------------------------------------------
static const wchar_t* const GAccountNameFileKeySource = L"AKL!8167fGBA0712";

static unsigned short AccountNameFileXorKey() {
    return (unsigned short)Server_AccountCrc(
        reinterpret_cast<const unsigned char*>(GAccountNameFileKeySource),
        (unsigned int)(wcslen(GAccountNameFileKeySource) * sizeof(wchar_t)));
}

// ComputeHashedCharacterID: lowercase the wide name, account-CRC its raw bytes,
// format as "%02x-%08x" (length, hash). Matches retail sprintf pattern.
static std::string ComputeHashedCharacterID(const std::wstring& CharName) {
    // Duplicate and lowercase
    std::wstring Lower = CharName;
    for (std::wstring::size_type i = 0; i < Lower.size(); ++i) {
        Lower[i] = (wchar_t)towlower(Lower[i]);
    }
    unsigned int CharLen = (unsigned int)Lower.size();
    // Hash the raw bytes of the wchar_t string (length * sizeof(wchar_t))
    unsigned int Hash = Server_AccountCrc(
        reinterpret_cast<const unsigned char*>(Lower.c_str()),
        CharLen * (unsigned int)sizeof(wchar_t));
    char Buf[32];
    sprintf_s(Buf, sizeof(Buf), "%02x-%08x", CharLen, Hash);
    return std::string(Buf);
}

static std::string ComputeHashedCharacterID(const CiWString& CharName) {
    return ComputeHashedCharacterID(CiToWide(CharName));
}

// BuildAccountFolderPath: constructs UserDataPath / "Accts" or "Accts\MultiPlayer"
static void BuildAccountFolderPath(char* OutPath, unsigned int OutPathSize) {
    char UserDataPath[260];
    GetUserDataPath(UserDataPath);
    const char* SubDir = SinglePlayer() ? "Accts" : "Accts\\MultiPlayer";
    _makepath_s(OutPath, OutPathSize, nullptr, UserDataPath, SubDir, nullptr);
}

// BuildAccountFilePath: builds full path for an account by name
static void BuildAccountFilePath(char* OutPath, unsigned int OutPathSize,
                                 const std::wstring& AccountName) {
    char DirPath[260];
    BuildAccountFolderPath(DirPath, sizeof(DirPath));
    // For MP server: hash the lowercase name to get directory name
    if (!SinglePlayer()) {
        std::wstring Lower = AccountName;
        for (std::wstring::size_type i = 0; i < Lower.size(); ++i) {
            Lower[i] = (wchar_t)towlower(Lower[i]);
        }
        unsigned int CharLen = (unsigned int)Lower.size();
        unsigned int Hash = Server_AccountCrc(
            reinterpret_cast<const unsigned char*>(Lower.c_str()),
            CharLen * (unsigned int)sizeof(wchar_t));
        char HashBuf[32];
        sprintf_s(HashBuf, sizeof(HashBuf), "%02x-%08x", CharLen, Hash);
        _makepath_s(OutPath, OutPathSize, nullptr, DirPath, HashBuf, nullptr);
    } else {
        // Single player: use name directly as narrow string
        char NameBuf[260];
        wcstombs(NameBuf, AccountName.c_str(), sizeof(NameBuf));
        _makepath_s(OutPath, OutPathSize, nullptr, DirPath, NameBuf, nullptr);
    }
}

// ===========================================================================
// FindAccountFromCharacterID (RVA 0x72AE0, export 125)
// Looks up CAccount* by character ID string in m_CharacterIDMap.
// Retail: lower_bound on char-keyed BST at this+0x30, compare with _strnicmp,
//   return CAccount* from node+0x1c if found.
// ===========================================================================
CAccount* PlayerDB::FindAccountFromCharacterID(CiString& CharId) {
    // Retail: FUN_06d5a440 lower_bound, FUN_06d5af80 compare,
    //   return *(CAccount**)(node+0x1c) or nullptr
    std::map<CiString, CAccount*>::iterator It =
        m_CharacterIDMap.find(CharId);
    if (It != m_CharacterIDMap.end()) {
        return It->second;
    }
    return nullptr;
}

// ===========================================================================
// FindAccountFromCharacterName (RVA 0x72B60, export 126)
// Hashes the character name to produce a character-ID filename (XX-XXXXXXXX.fl),
// then looks up in m_CharacterIDMap.
// Retail: _wcsdup + _wcslwr, account-CRC hash via FUN_06d46850/FUN_06d467b0,
//   sprintf "%02x-%08x", _makepath with ".fl", lookup in char-ID map.
// ===========================================================================
CAccount* PlayerDB::FindAccountFromCharacterName(CiWString& CharName) {
    // Compute the hashed character ID (retail account-CRC hash pattern)
    std::string CharId = ComputeHashedCharacterID(CharName);
    // Build the filename with ".fl" extension for the lookup key
    char FileName[260];
    _makepath_s(FileName, sizeof(FileName), nullptr, nullptr, CharId.c_str(), "fl");
    // Lowercase the filename for case-insensitive lookup
    _strlwr(FileName);
    // Look up in the character ID map
    std::map<CiString, CAccount*>::iterator It =
        m_CharacterIDMap.find(CiString(FileName));
    if (It != m_CharacterIDMap.end()) {
        return It->second;
    }
    return nullptr;
}

// ===========================================================================
// FindAccountFromName (RVA 0x72CD0, export 128)
// Looks up CAccount* by account name in m_AccountNameMap.
// Retail: lower_bound on wstring-keyed BST at this+0x44, compare with
//   _wcsnicmp, return CAccount* from node+0x1c if found.
// ===========================================================================
CAccount* PlayerDB::FindAccountFromName(CiWString& Name) {
    // Retail: FUN_06d5a770 lower_bound, FUN_06d5af30 compare,
    //   return *(CAccount**)(node+0x1c) or nullptr
    std::map<CiWString, CAccount*>::iterator It =
        m_AccountNameMap.find(Name);
    if (It != m_AccountNameMap.end()) {
        return It->second;
    }
    return nullptr;
}

// ===========================================================================
// GetAccountAdminRights (RVA 0x72730, export 144)
// Returns the admin flag for the account identified by Name.
// Retail: lower_bound on wstring map at this+0x44, return bool at node+0x32.
// ===========================================================================
bool PlayerDB::GetAccountAdminRights(CiWString& Name) {
    // Retail: FUN_06d5a770 lower_bound, FUN_06d5af30 compare,
    //   return *(bool*)(iVar1 + 0x32) where iVar1 = *(int*)(node+0x1c)
    // .text:06d52740 .. 06d52785 (verified 2026-09-24, claude-code:w5-server-06d4):
    // lower_bound + compare = find; a hit with a non-null account returns the
    // byte at account+0x32, everything else returns 0 (.text:06d52788).
    std::map<CiWString, CAccount*>::iterator It =
        m_AccountNameMap.find(Name);
    if (It != m_AccountNameMap.end() && It->second != nullptr) {
        return It->second->m_bAdmin;                                        // .text:06d52781
    }
    return false;
}

// ===========================================================================
// SetAccountAdminRights (RVA 0x72790, export 406)
// Sets the admin flag for the account and creates/deletes the "admin" file.
// Retail: lookup account, build file path (hashed for MP, plain for SP),
//   create "admin" file if bAdmin=true, unlink if bAdmin=false,
//   set bool at node+0x32.
// ===========================================================================
bool PlayerDB::SetAccountAdminRights(CiWString& Name, bool bAdmin) {
    // Retail: FUN_06d583b0 = map::find on the wstring map at this+0x44
    std::map<CiWString, CAccount*>::iterator It =
        m_AccountNameMap.find(Name);
    if (It == m_AccountNameMap.end() || It->second == nullptr) {
        return false;
    }

    CAccount* Account = It->second;

    // Build the account file path
    char AccountPath[260];
    BuildAccountFilePath(AccountPath, sizeof(AccountPath), CiToWide(Name));

    // Build the "admin" file path
    char AdminFilePath[260];
    _makepath_s(AdminFilePath, sizeof(AdminFilePath), nullptr, AccountPath, "admin", nullptr);

    if (bAdmin) {
        // Create the admin marker file
        FILE* File = fopen(AdminFilePath, "wb");
        if (File != nullptr) {
            fclose(File);
            Account->m_bAdmin = bAdmin;                                     // account+0x32
            return true;
        }
    } else {
        // Delete the admin marker file
        _unlink(AdminFilePath);
    }
    Account->m_bAdmin = bAdmin;                                             // account+0x32
    return true;
}

// ===========================================================================
// LockAccountAccess (RVA 0x72A40, export 331)
// Locks the account: if the account has an active client, calls
// IServer::OnConnect(clientId) to disconnect it, then sets bLocked=true.
// Retail: FUN_06d583b0 (map::find), check node+0x1c (CAccount*),
//   if node+0x34 (ClientID) != 0 call vtable slot 0x44 (OnConnect),
//   set byte at node+0x30 to 1.
// ===========================================================================
void PlayerDB::LockAccountAccess(CiWString& Name) {
    // Retail: FUN_06d583b0 = map::find on the wstring map at this+0x44
    // .text:06d52a50 FUN_06d583b0 = map::find; .text:06d52a5c / 06d52a63 miss or
    // null account -> return.
    std::map<CiWString, CAccount*>::iterator It =
        m_AccountNameMap.find(Name);
    if (It == m_AccountNameMap.end() || It->second == nullptr) {
        return;
    }

    CAccount* Account = It->second;

    // .text:06d52a65 .. 06d52a75: a non-zero client id at account+0x34 (JBE on
    // zero) is kicked through DAT_06d8fb14 vtable+0x44 = IClient slot 17, with
    // no null test on the interface (the port's guard was removed 2026-09-24,
    // claude-code:w5-server-06d4). DAT_06d8fb14 is the CLIENT interface global,
    // the same pointer SendGroupID / SendSystemID / SetMOTD go through.
    if (Account->m_ClientID != 0) {
        extern IClient* g_pClientInterface;
        g_pClientInterface->DisconnectClient(Account->m_ClientID);         // .text:06d52a75
    }

    Account->m_bLocked = true;                                              // .text:06d52a78
}

// ===========================================================================
// PlayerDB::create_restart_file -- retail .text:06d536d0 .. .text:06d53b5b
// (thiscall, RET 4), ordinal 479. Ported 1:1 2026-09-24 (claude-code:w5-server-06d4).
// Builds <AccountDir>\Restart.fl through the ini writer (SaveGame_Writer, vptr
// 06d6b5c0 then 06d6b5a0) from two sources, one after the other:
//   .text:06d53715  _makepath(restart, 0, dir, "Restart", "fl" .rdata:06d6aea4)
//   .text:06d5371e .. 06d53794  writer ctor (SaveGame base FUN_06d4eac0 on
//                   writer+4 at .text:06d5371e, then the vptrs and the path
//                   string), path assign, fopen(restart, "w+t")
//                   -- SaveGame_Writer::Open; a null handle -> FDUMP line 0x156c
//                   "for writing" on the restart path, return false
//   .text:06d537b2 .. 06d537ed  GetModuleFileNameA / _splitpath / _makepath:
//                   <exe dir>\NewPlayer.fl
//   .text:06d537f6 .. 06d5388f  {FILE*, string} pair: fopen "rt", fread 4 bytes;
//                   "FLS1" -> FUN_06d4eae0, fopen "rb", FUN_06d48960(4),
//                   FUN_06d48b60; "FLSG" -> fail; anything else ->
//                   FUN_06d48960(0)
//   .text:06d5389a  no open handle left -> fail (FDUMP line 0x1565 "for
//                   reading" on the template path, FUN_06d4eb00 = Close(),
//                   remove(restart), false)
//   .text:06d538b8  fprintf(writer file, template text) -- the text IS the format
//   .text:06d538c5 .. 06d538d3  the template handle closed and cleared
//   .text:06d538f0 .. 06d53909  (*DALib::Data)->GetFileSysPath +0x84, then
//                   _makepath(buf, 0, buf, DAT_06d8f9c8 = initial_world, 0)
//   .text:06d53915  fopen(world, "rt"):
//     open   -> 0x200-byte fread/fwrite copy into the writer while a full block
//               came back, fclose, result = 1; writer file set ->
//               FUN_06d53b90, FUN_06d4eae0, the "wb" FLS1 rewrite
//     closed -> FDUMP line 0x155e "for reading" on the world path; writer file
//               set -> the same encode + rewrite; remove(restart); false
// Severity: .text:06d53a0d loads 0x100002 directly, the other two build it with
// FUN_06d53b60(0x10000, 2).
// The port's "rb" template open, its fallback-only DATA path, its size check,
// its byte copy without the writer and its OutputDebugStringA logging were not
// retail and are gone.
// ===========================================================================

// Retail FUN_06d53b60 (thiscall on an int out-slot, RET 8; callers
// .text:06d53aab and .text:06d53b03): slot = ((Category << 4) ^ Level) & 0xf
// ^ (Category << 4), returning the slot's address. The previous slot value is
// spilled to the stack (.text:06d53b65) and never read back.
static int PlayerDB_MakeDumpFlags(int Category, unsigned int Level) {
    unsigned int const High = static_cast<unsigned int>(Category) << 4;    // .text:06d53b6d
    return static_cast<int>(((High ^ Level) & 0xfu) ^ High);                // .text:06d53b77..06d53b7c
}

bool PlayerDB::create_restart_file(const char* AccountDir) {
    static char const* const SourceFile = "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp";
    bool bResult = false;                                                   // .text:06d53710
    char RestartPath[0x104];
    _makepath(RestartPath, nullptr, AccountDir, "Restart", "fl");           // .text:06d53715

    SaveGame_Writer Save;                                                   // .text:06d5371e..06d5374a
    if (!Save.Open(RestartPath)) {                                          // .text:06d5377b..06d5379f
        FDUMP(PlayerDB_MakeDumpFlags(0x10000, 2),                           // .text:06d53b03
              "%s(%d) : *** WARNING: Failed to open '%s' for writing.\n",   // .rdata:06d6b900
              SourceFile, 0x156c, RestartPath);
        return bResult;
    }

    char TemplatePath[0x104];
    GetModuleFileNameA(nullptr, TemplatePath, 0x104);                       // .text:06d537b2
    char Drive[4];
    char Dir[0x100];
    _splitpath(TemplatePath, Drive, Dir, nullptr, nullptr);                 // .text:06d537ce
    _makepath(TemplatePath, Drive, Dir, "NewPlayer.fl", nullptr);           // .text:06d537ed

    FSaveFileBuffer Template;                                               // .text:06d537f6
    Template.File = fopen(TemplatePath, "rt");                              // .text:06d5380d, .rdata:06d6b314
    if (Template.File != nullptr) {
        // Retail reads into an uninitialised dword; zero here only so that a file
        // shorter than four bytes compares as neither magic (the ReadTail(0) arm).
        unsigned int Magic = 0;
        fread(&Magic, 1, 4, Template.File);                                 // .text:06d5382a
        if (Magic == 0x31534c46u) {                                         // .text:06d5383c, "FLS1"
            Template.CloseFile();                                           // .text:06d53844 FUN_06d4eae0
            Template.File = fopen(TemplatePath, "rb");                      // .text:06d53853, .rdata:06d6aea8
            if (Template.File != nullptr) {
                Template.ReadTail(4);                                       // .text:06d53866 FUN_06d48960
                Template.Encode();                                          // .text:06d5386f FUN_06d48b60
            }
        } else if (Magic == 0x47534c46u) {                                  // .text:06d53881, "FLSG"
            goto TemplateFailed;                                            // .text:06d53883
        } else {
            Template.ReadTail(0);                                           // .text:06d5388f FUN_06d48960
        }
    }
    if (Template.File == nullptr)                                           // .text:06d53894..06d5389f
        goto TemplateFailed;

    {
        fprintf(Save.File, Template.Contents.c_str());                      // .text:06d538b3 [writer+4], 06d538b8
        if (Template.File != nullptr) {                                     // .text:06d538c5
            fclose(Template.File);                                          // .text:06d538ca
            Template.File = nullptr;                                        // .text:06d538d3
        }

        // Retail builds the world path in place (one buffer as out and dir);
        // the data directory gets its own buffer here so the result does not
        // depend on the modern _makepath's handling of an overlapping output.
        char DataPath[0x104];
        DALib::Data->GetFileSysPath(DataPath, 0x104);                       // .text:06d538f0
        extern char g_InitialWorldPath[260];                                // .data:DAT_06d8f9c8
        char WorldPath[0x104];
        _makepath(WorldPath, nullptr, DataPath, g_InitialWorldPath, nullptr); // .text:06d53909

        FILE* World = fopen(WorldPath, "rt");                               // .text:06d53915
        if (World != nullptr) {
            char Block[0x200];
            size_t Got;
            do {
                Got = fread(Block, 1, 0x200, World);                        // .text:06d53934
                if (Got != 0)                                               // .text:06d5393d JBE
                    fwrite(Block, 1, Got, Save.File);                       // .text:06d5393f [writer+4], 06d5394f
            } while (Got >= 0x200);                                         // .text:06d53958 JNC
            fclose(World);                                                  // .text:06d53967
            bResult = true;                                                 // .text:06d53972
            if (Save.File != nullptr) {                                     // .text:06d53969 [writer+4], 06d53970
                Save.ReadBackAndEncode();                                   // .text:06d53981
                Save.CloseFile();                                           // .text:06d5398a FUN_06d4eae0 on writer+4
                Save.RewriteEncoded();                                      // .text:06d5398f..06d539e5
            }
            return bResult;                                                 // .text:06d539ed, 06d53b3b
        }

        FDUMP(0x100002,                                                     // .text:06d53a0d
              "%s(%d) : *** WARNING: Failed to open '%s' for reading.\n",   // .rdata:06d6b938
              SourceFile, 0x155e, WorldPath);
        if (Save.File != nullptr) {                                         // .text:06d53a1a [writer+4], 06d53a21
            Save.ReadBackAndEncode();                                       // .text:06d53a29
            Save.CloseFile();                                               // .text:06d53a32 FUN_06d4eae0 on writer+4
            Save.RewriteEncoded();                                          // .text:06d53a37..06d53a8d
        }
        remove(RestartPath);                                                // .text:06d53a95..06d53ae3
        return bResult;
    }

TemplateFailed:
    FDUMP(PlayerDB_MakeDumpFlags(0x10000, 2),                               // .text:06d53aab
          "%s(%d) : *** WARNING: Failed to open '%s' for reading.\n",       // .rdata:06d6b938
          SourceFile, 0x1565, TemplatePath);
    Save.Close();                                                           // .text:06d53ad6 FUN_06d4eb00
    remove(RestartPath);                                                    // .text:06d53ae3
    return bResult;
}

unsigned int PlayerDB::to_index(unsigned int SessionId) {
    return (SessionId > 0) ? SessionId - 1 : 0;
}

std::wstring& PlayerDB::GetMOTD() {
    return m_MOTD;
}

// ---------------------------------------------------------------------------
// PlayerDB::SetMOTD — retail RVA 0x746E0, ordinal 421
// Assigns the string into the MOTD at this+0x20 (the whole first half of the
// retail body is the inlined VC6 basic_string assignment), then walks slots
// 1..MaxPlayers and, for every slot whose connection word at +0x348 is
// non-zero, calls (**(code**)(*DAT_06d8fb14 + 0x1cc))(slotId, &Message) —
// the client-interface global, byte offset 0x1cc, the same table SendGroupID
// (+0x1d8) and SendSystemID (+0x1dc) use.
// ---------------------------------------------------------------------------
void PlayerDB::SetMOTD(std::wstring& Message) {
    m_MOTD = Message;

    extern IClient* g_pClientInterface;
    if (!g_pClientInterface || !m_PlayerArray) return;

    // vtable+0x1cc is IClient slot 115 (SendServerMiscObjUpdateMotd) -- see the note in
    // SendGroupID.

    for (unsigned int Index = 1; Index <= m_MaxPlayers; ++Index) {
        if (m_PlayerArray[Index - 1].client_id != 0) {
            g_pClientInterface->SendServerMiscObjUpdateMotd(Index, Message);
        }
    }
}

// ===========================================================================
// PlayerDB::UnlockAccountAccess — retail RVA 0x72A80, ordinal 469
// Looks up account by name in m_AccountNameMap (this+0x44). If found,
// clears the bLocked flag (account+0x30 = 0).
// Retail: FUN_06d5a770 lower_bound, FUN_06d5af30 compare.
// ===========================================================================
void PlayerDB::UnlockAccountAccess(CiWString& CharName) {
    // Look up account in the name map (this+0x44 = m_AccountNameMap)
    std::map<CiWString, CAccount*>::iterator It =
        m_AccountNameMap.find(CharName);

    if (It == m_AccountNameMap.end()) {
        return;
    }

    // .text:06d52aca .. 06d52ad1: a non-null account gets account+0x30 = 0.
    CAccount* Account = It->second;
    if (Account != nullptr) {
        Account->m_bLocked = false;                                         // .text:06d52ad1
    }
}

// ---------------------------------------------------------------------------
// PlayerDB::ReadCharacterName -- retail .text:06d52fe0 .. .text:06d532e5
// (thiscall, RET 8). Ported 1:1 2026-09-24 (claude-code:w5-server-06d4).
//   .text:06d53010 / 06d53016  the {FILE*, string} pair at ESP+0xc, File = 0
//   .text:06d5302d  INI_Reader at ESP+0x28
//   .text:06d5304e  File = fopen(path, "rt") (.rdata:06d6b314); null -> fail
//   .text:06d53069  fread(magic, 1, 4, File)
//   .text:06d5307f  magic == "FLS1" (.rdata:06d6b30c): fclose, File = 0,
//                   File = fopen(path, "rb") (.rdata:06d6aea8), null -> fail;
//                   FUN_06d48960(4) then FUN_06d48b60 (the FLS1 XOR)
//   .text:06d530d1  magic == "FLSG" (.rdata:06d6b304) -> fail
//   .text:06d530df  anything else: FUN_06d48960(0) on the "rt" handle
//   .text:06d5310c  INI_Reader::open_memory(contents, length)
//   .text:06d53116  read_header() == 0 -> fail
//   .text:06d53130 .. 06d531a3  per header: counter = 0 (.text:06d53139);
//                   only "Player" headers are walked; per value, is_value("/*")
//                   increments the counter, otherwise is_value("*/") decrements
//                   it; while counter <= 0 (JG at .text:06d53174) is_value("name")
//                   is the hit
//   .text:06d5326a .. 06d532c3  hit: HexToWide(get_value_string(), local[0x400
//                   wchar]); OutName.assign(local, wcslen(local))
//   .text:06d531a5 .. 06d531df  fail: OutName.assign(L"", 0) (.rdata:06d6b2f8)
//   .text:06d531e4 .. 06d5324a  INI_Reader destroyed, fclose(File) if open,
//                   the string freed -- the local destructors below, same order
// The out-string is written only on these two exits; retail never clears it on
// entry and has no null test on the path.
// ---------------------------------------------------------------------------
void PlayerDB::ReadCharacterName(const char* FilePath, CiWString& OutName) {
    FSaveFileBuffer Buffer;                                                 // .text:06d53016, SaveGame ctor inlined
    INI_Reader Reader;                                                      // .text:06d5302d

    bool bLoaded = false;
    Buffer.File = fopen(FilePath, "rt");                                    // .text:06d5304e
    if (Buffer.File != nullptr) {
        char Magic[4] = { 0, 0, 0, 0 };
        fread(Magic, 1, 4, Buffer.File);                                    // .text:06d53069
        if (memcmp(Magic, Server_FLS1Magic, 4) == 0) {                      // .text:06d5307f
            fclose(Buffer.File);
            Buffer.File = nullptr;
            Buffer.File = fopen(FilePath, "rb");                            // .text:06d530a3
            if (Buffer.File != nullptr) {
                Buffer.ReadTail(4);                                         // .text:06d530b6 FUN_06d48960
                Buffer.Encode();                                            // .text:06d530bf FUN_06d48b60
                bLoaded = true;
            }
        } else if (memcmp(Magic, "FLSG", 4) != 0) {                         // .text:06d530d1
            Buffer.ReadTail(0);                                             // .text:06d530df
            bLoaded = true;
        }
    }

    if (bLoaded) {
        Reader.open_memory(Buffer.Contents.c_str(),
                           static_cast<unsigned int>(Buffer.Contents.size())); // .text:06d5310c
        if (Reader.read_header()) {                                         // .text:06d53116
            do {
                int CommentDepth = 0;                                       // .text:06d53139
                if (Reader.is_header("Player") && Reader.read_value()) {
                    do {
                        if (Reader.is_value("/*"))                          // .text:06d53159
                            ++CommentDepth;
                        else if (Reader.is_value("*/"))                     // .text:06d5316b
                            --CommentDepth;
                        if (CommentDepth <= 0 && Reader.is_value("name")) { // .text:06d5317f
                            wchar_t WideBuf[0x400];
                            HexToWide(Reader.get_value_string(), WideBuf);  // .text:06d5327d
                            OutName.assign(WideBuf, wcslen(WideBuf));       // .text:06d532a2..
                            return;
                        }
                    } while (Reader.read_value());                          // .text:06d5318d
                }
            } while (Reader.read_header());                                 // .text:06d5319b
        }
    }

    OutName.assign(L"", wcslen(L""));                                       // .text:06d531a5..
}

// ===========================================================================
// PlayerDB::DeleteCharacterFromID — retail RVA 0x72D30, ordinal 100
// Looks up the character ID in m_CharacterIDMap (this+0x30). If found and
// the associated account is non-null, calls CAccount::DeleteCharacterFromID
// to delete the .fl file and remove from the account's character list.
// Then erases the entry from the character ID map.
// Retail: FUN_06d571f0 lower_bound, FUN_06d5a440/FUN_06d5a850 erase range.
// ===========================================================================
void PlayerDB::DeleteCharacterFromID(CiString& CharId) {
    // Look up character in the character ID map (this+0x30 = m_CharacterIDMap)
    std::map<CiString, CAccount*>::iterator It =
        m_CharacterIDMap.find(CharId);

    if (It == m_CharacterIDMap.end()) {
        return;
    }

    // If found and has an associated account, delete from the account's character list
    // Retail: CAccount::DeleteCharacterFromID(*(CAccount**)(node + 0x1c), charId)
    CAccount* Account = It->second;
    if (Account != nullptr) {
        Account->DeleteCharacterFromID(CharId);
    }

    // Remove from the character ID map
    // Retail: erases node from BST at this+0x30
    m_CharacterIDMap.erase(It);
}

bool PlayerDB::DeleteCharacterFromName(CiWString& CharName) {
    // Retail RVA 0x72D90: converts character name to ID via the account-CRC hash,
    // then calls DeleteCharacterFromID. The hash is the same as used for
    // character file naming: sprintf("%02x-%08x", wcslen(lowername), crc32(lowername_bytes))
    if (CharName.empty()) return true;

    // Compute the hashed character ID (lowercase + account CRC, as retail)
    std::string CharId = ComputeHashedCharacterID(CharName);

    // Build the filename with ".fl" extension for the map lookup key
    char FileName[260];
    _makepath_s(FileName, sizeof(FileName), nullptr, nullptr, CharId.c_str(), "fl");
    _strlwr(FileName);

    // Delete using the computed ID
    CiString CharKey(FileName);
    DeleteCharacterFromID(CharKey);

    return true;
}

void PlayerDB::logout_all() {
    // Retail: iterates through all player slots, finds active ones, logs them out
    if (!m_PlayerArray) return;

    PlayerData* Current = nullptr;
    while (true) {
        Current = traverse_active(Current);
        if (Current == nullptr) break;

        // Retail reads the session id out of the slot's +0x348 field --
        // PlayerData::client_id -- and logs that session out.
        unsigned int SessionId = Current->client_id;
        if (SessionId != 0) {
            logout(SessionId);
        }
    }
}

// ===========================================================================
// PlayerData_LoginAccount -- retail FUN_06d48e80 (.text:06d48e80 .. .text:06d49228),
// thiscall on the PlayerData slot, RET 0xc: (AccountDir, SLoginInfo const&,
// SessionId). Only caller: PlayerDB::load_user_data (.text:06d536bc).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06d4); it used to be inlined into
// load_user_data with the locked flag read from the port's map-side copy.
//   .text:06d48eaf .. 06d48f2e  a CiWString of the login name, looked up in the
//                   account-name map (FUN_06d583b0 = find on Players+0x44);
//                   slot+0x400 = hit ? node CAccount* : 0; the string freed
//   .text:06d48f44 .. 06d48f65  a non-null account: +0x30 set -> return 7;
//                   +0x31 clear -> return 0
//   .text:06d48f73  slot+0x348 (client_id) = SessionId
//   .text:06d48f7b  a non-null account: +0x34 = SessionId
//   .text:06d48f88  wcscpy(slot+0x00, name) -- unbounded
//   .text:06d48f98  result = 6
//   .text:06d48f9d  _access(dir, 6) != 0 skips everything down to .text:06d49120
//   .text:06d48fc2 .. 06d49054  _findfirst(dir\*.fl), sprintf("%s.fl","Restart");
//                   per file FUN_06d48d50 (PlayerData_LoadSavedCharacter); a
//                   load sets result 3 and, until the first hit, stricmp
//                   against the Restart name sets the found flag
//   .text:06d4905b  _findclose -- also for a -1 handle
//   .text:06d49064  single player: no Restart file loaded ->
//                   Players.create_restart_file(dir) and FUN_06d48d50 on it;
//                   both succeeding sets result 3
//   .text:06d490a8  otherwise result 3; !SinglePlayer() (re-tested at
//                   .text:06d490ad) logs "LOG: Login on account %S (\"%s\")";
//                   then (slot+0x400)->+0x14 = dir, with no null test
//   .text:06d4912c .. 06d49156  slot+0x3e0..+0x3fc = 0
//   .text:06d4916c .. 06d491da  the name looked up a SECOND time, slot+0x400 =
//                   the result (create_account may have inserted it)
//   .text:06d4920b  slot+0x410 = 0; return result
// DACOM's stricmp (.text:06d49030) is spelled _stricmp here, as everywhere in
// src/Server.
// ===========================================================================
static unsigned char PlayerData_LoginAccount(PlayerData& Slot, const char* AccountDir,
                                             const SLoginInfo& Info, unsigned int SessionId) {
    {
        CiWString LoginName(Info.AccountName, wcslen(Info.AccountName));   // .text:06d48ece
        Slot.account_ptr = Players.FindAccountFromName(LoginName);         // .text:06d48f2e
    }
    CAccount* Account = static_cast<CAccount*>(Slot.account_ptr);
    if (Account != nullptr) {
        if (Account->m_bLocked)                                             // .text:06d48f4e
            return 7;
        if (!Account->m_bNotBanned)                                         // .text:06d48f5c
            return 0;
    }
    Slot.client_id = SessionId;                                             // .text:06d48f73
    if (Account != nullptr)
        Account->m_ClientID = SessionId;                                    // .text:06d48f7b
    wcscpy(Slot.AccountName, Info.AccountName);                             // .text:06d48f88

    unsigned char Result = 6;                                               // .text:06d48f98
    if (_access(AccountDir, 6) == 0) {                                      // .text:06d48f9d
        char SearchPattern[260];
        _makepath(SearchPattern, nullptr, AccountDir, "*", "fl");           // .text:06d48fc2
        struct _finddata_t FindData;
        intptr_t const FindHandle = _findfirst(SearchPattern, &FindData);   // .text:06d48fd5
        char RestartName[260];
        sprintf(RestartName, "%s.fl", "Restart");                           // .text:06d48ff3
        bool bFoundRestart = false;                                         // BL, .text:06d48ff1
        if (FindHandle != -1) {                                             // .text:06d49000
            int FindResult = 0;
            while (FindResult == 0) {                                       // .text:06d49005
                if (PlayerData_LoadSavedCharacter(Slot, AccountDir, FindData.name)) { // .text:06d49011
                    Result = 3;                                             // .text:06d4901c
                    if (!bFoundRestart && _stricmp(FindData.name, RestartName) == 0)
                        bFoundRestart = true;                               // .text:06d4903d
                }
                FindResult = _findnext(FindHandle, &FindData);              // .text:06d49049
            }
        }
        _findclose(FindHandle);                                             // .text:06d4905b

        if (SinglePlayer()) {                                               // .text:06d49064
            if (!bFoundRestart && Players.create_restart_file(AccountDir) &&
                PlayerData_LoadSavedCharacter(Slot, AccountDir, RestartName)) {
                Result = 3;                                                 // .text:06d490a1
            }
        } else {
            Result = 3;                                                     // .text:06d490a8
            if (!SinglePlayer()) {                                          // .text:06d490ad
                FDUMP(0x100003, "LOG: Login on account %S (\"%s\")\n",
                      Info.AccountName, AccountDir);                        // .text:06d490d1
            }
            static_cast<CAccount*>(Slot.account_ptr)->m_DirectoryPath = AccountDir; // .text:06d490d6..06d4911b
        }
    }

    Slot.system_id = 0;                                                     // +0x3e0
    Slot.ship_object_id = 0;                                                // +0x3e4
    Slot.created_ship_id = 0;                                               // +0x3e8
    Slot.current_base = 0;                                                  // +0x3ec
    Slot.entered_base_id = 0;                                               // +0x3f4
    Slot.last_base_id = 0;                                                  // +0x3f0
    Slot.current_location = 0;                                              // +0x3f8
    Slot.Field_0x3FC = 0;                                                   // +0x3fc
    {
        CiWString LoginName(Info.AccountName, wcslen(Info.AccountName));   // .text:06d4917e
        Slot.account_ptr = Players.FindAccountFromName(LoginName);         // .text:06d491da
    }
    Slot.exited_base_id = 0;                                                // .text:06d4920b
    return Result;
}

// ===========================================================================
// PlayerDB::load_user_data -- retail .text:06d533a0 .. .text:06d536cb
// (thiscall, RET 8), ordinal 499. Ported 1:1 2026-09-24 (claude-code:w5-server-06d4).
//   .text:06d533b2  IsMPServer(): the account directory name is the
//                   "%02x-%08x" of the lowercased login name (_wcsdup/_wcslwr,
//                   2*wcslen bytes through the account CRC, free) -- exactly
//                   FUN_06d46370 inlined, Server_AccountDirName here; the root
//                   is FUN_06d460f0 inlined (GetUserDataPath + "Accts" /
//                   "Accts\\MultiPlayer" into DAT_06d90158)
//   .text:06d534ac  otherwise wcstombs(name, login, 0x104) and FUN_06d460f0
//   .text:06d53496 / 06d534d2  _makepath(dir, 0, root, name, 0)
//   .text:06d534e8  _access(dir, 6) != 0: Info+0xc0 clear -> 2; create_account
//                   non-zero -> 2
//   .text:06d5352b  !SinglePlayer(): "<dir>\name" opened "rb" (null -> 2), up
//                   to 0x400 wide chars read, XOR-decoded with the key the
//                   account CRC makes of the marker string PTR_DAT_06d8d158;
//                   a count different from wcslen(login) or a byte mismatch
//                   over 2*count bytes -> 2
//   .text:06d536bc  return FUN_06d48e80(&slot[SessionId - 1], dir, Info,
//                   SessionId) -- the slot indexed raw off [Players]
// The port's entry guards (session range, null array, empty name) and its
// _access(dir, 0) were not retail and are gone.
// ===========================================================================
unsigned char PlayerDB::load_user_data(const SLoginInfo& Info, unsigned int SessionId) {
    char DirName[260];
    char AccountDir[260];
    if (IsMPServer()) {                                                     // .text:06d533b2
        Server_AccountDirName(DirName, Info.AccountName);                   // .text:06d533c8..06d5342f
        _makepath(AccountDir, nullptr, Server_GetAccountsRoot(), DirName, nullptr); // .text:06d53496
    } else {
        wcstombs(DirName, Info.AccountName, 0x104);                         // .text:06d534ac
        _makepath(AccountDir, nullptr, Server_GetAccountsRoot(), DirName, nullptr); // .text:06d534d2
    }

    if (_access(AccountDir, 6) != 0) {                                      // .text:06d534e8
        // .text:06d534f5 MOV AL,[EBP + 0xc0]: retail tests the low BYTE only.
        if (static_cast<unsigned char>(Info.CreateFlag) == 0)
            return 2;
        if (create_account(Info) != 0)                                      // .text:06d53513
            return 2;
    }

    if (!SinglePlayer()) {                                                  // .text:06d5352b
        char NamePath[260];
        _makepath(NamePath, nullptr, AccountDir, "name", nullptr);          // .text:06d5354f
        FILE* NameFile = fopen(NamePath, "rb");                             // .text:06d5355b
        if (NameFile == nullptr)
            return 2;
        unsigned short Encoded[0x400];
        size_t const CharsRead = fread(Encoded, 2, 0x400, NameFile);        // .text:06d5357a
        fclose(NameFile);                                                   // .text:06d53583
        unsigned short const XorKey = AccountNameFileXorKey();              // .text:06d53589..06d53641
        unsigned short Decoded[0x400];
        for (size_t I = 0; I < CharsRead; ++I)
            Decoded[I] = static_cast<unsigned short>(Encoded[I] ^ XorKey);  // .text:06d53650
        if (CharsRead != wcslen(Info.AccountName))                          // .text:06d53673
            return 2;
        if (memcmp(Info.AccountName, Decoded, CharsRead * 2) != 0)          // .text:06d53685
            return 2;
    }

    return PlayerData_LoginAccount(m_PlayerArray[SessionId - 1], AccountDir, Info,
                                   SessionId);                              // .text:06d536bc
}

// ===========================================================================
// PlayerDB::create_account -- retail .text:06d53d50 .. .text:06d53fcd
// (thiscall, RET 4), ordinal 477. Ported 1:1 2026-09-24 (claude-code:w5-server-06d4).
//   .text:06d53d6f  result = -1
//   .text:06d53d7b  IsMPServer(): FUN_06d46370 and FUN_06d460f0 inlined, as in
//                   load_user_data; otherwise wcstombs(name, login, 0x104) and
//                   FUN_06d460f0; _makepath(dir, 0, root, name, 0)
//   .text:06d53ebd  SinglePlayer(): _access(dir, 6) != 0 -> BuildDirectory(dir);
//                   create_restart_file(dir) ? 0 : -1
//   .text:06d53f04  otherwise: PlayerDB+0x0c clear -> -1; else CreateAccount
//                   on a CiWString of the login name, true -> 0
// ===========================================================================
int PlayerDB::create_account(const SLoginInfo& Info) {
    int Result = -1;                                                        // .text:06d53d77
    char DirName[260];
    char AccountDir[260];
    if (IsMPServer()) {                                                     // .text:06d53d7b
        Server_AccountDirName(DirName, Info.AccountName);                   // .text:06d53d92..06d53dfc
        _makepath(AccountDir, nullptr, Server_GetAccountsRoot(), DirName, nullptr); // .text:06d53e72
    } else {
        wcstombs(DirName, Info.AccountName, 0x104);                         // .text:06d53e92
        _makepath(AccountDir, nullptr, Server_GetAccountsRoot(), DirName, nullptr); // .text:06d53eb2
    }

    if (SinglePlayer()) {                                                   // .text:06d53ebd
        if (_access(AccountDir, 6) != 0)                                    // .text:06d53ece
            BuildDirectory(AccountDir);                                     // .text:06d53ee0
        if (!create_restart_file(AccountDir))                               // .text:06d53ef0
            return Result;
        return 0;
    }

    // .text:06d53f04 MOV AL,[EBX + 0xc] / JZ -- the PlayerDB+0x0c byte (the
    // flag init() stores from SStartupInfo +0x211, FLServer's "Allow New
    // Players") gates account creation.
    if (!m_bAllowNewPlayers)
        return Result;
    CiWString Name(Info.AccountName, wcslen(Info.AccountName));            // .text:06d53f3d
    if (CreateAccount(Name))                                                // .text:06d53f76
        Result = 0;
    return Result;
}

// ===========================================================================
// PlayerDB::create_new_character — retail RVA 0x73FD0, ordinal 478
// Creates a new character for the given session. Steps:
//   1. Convert SCreateCharacterInfo.Name to narrow string
//   2. Build filename "<name>.fl", lowercase for map key
//   3. Check if character ID already exists in m_CharacterIDMap
//   4. Check if account has < 5 characters
//   5. Register character in account's list and character ID map
// Returns true on success, false on failure.
// ===========================================================================
bool PlayerDB::create_new_character(const SCreateCharacterInfo& Info, unsigned int SessionId) {
    // (w4-server-guards) removed two port-only entry exits: a session-range /
    // null-array test and a wcstombs "empty narrow name" test. Retail's entry
    // (06d53fd0 .. 06d54108) goes straight to FUN_06d46370 on the wide name
    // (06d54025), _makepath/_strlwr and the map lookup; the only early return
    // before the account read is the "id already taken" one at 06d54158.

    // Build the character-id filename. Retail derives it from the HASH of the
    // lowercased character name ("%02x-%08x" + ".fl"), not from the name
    // itself — that is the spelling every lookup path expects
    // (FindAccountFromCharacterName 0x72b60, DeleteCharacterFromName 0x72e60
    // and init's directory scan all build the key that way). Registering
    // "<name>.fl" here made every newly created character invisible to them.
    std::string HashedId = ComputeHashedCharacterID(std::wstring(Info.Name));
    char FileName[260];
    _makepath(FileName, nullptr, nullptr, HashedId.c_str(), "fl");

    // Lowercase the filename for case-insensitive map key
    char LowerFileName[260];
    strcpy_s(LowerFileName, sizeof(LowerFileName), FileName);
    _strlwr(LowerFileName);

    // Check if this character ID already exists in the character map (this+0x30)
    CiString CharKey(LowerFileName);
    std::map<CiString, CAccount*>::iterator ExistingIt =
        m_CharacterIDMap.find(CharKey);

    if (ExistingIt != m_CharacterIDMap.end()) {
        // Character already exists. Retail 0x06d54127 SETNZ [ESP+0x17] on the
        // lookup result, 0x06d5413f JZ, then 0x06d54158 XOR AL,AL -> false.
        return false;
    }

    // Retail indexes the slot directly: slot = m_PlayerArray + (SessionId-1),
    // then reads the owning account from slot+0x400. The raw-offset read this
    // replaces was applied to a `PlayerSlot*` from the other table -- 1024
    // bytes past the end of that object.
    PlayerData* Player = &m_PlayerArray[SessionId - 1];
    CAccount* Account = reinterpret_cast<CAccount*>(Player->account_ptr);
    // (w4-server-guards) no null test: 06d54173 `MOV ECX,[EBP+0x400]` feeds
    // 06d54179 `CMP dword ptr [ECX+0x2c],0x5` directly.

    // Check the per-account character limit. Retail:
    //   06d54173  MOV ECX,[EBP + 0x400]          ; the owning CAccount
    //   06d54179  CMP dword ptr [ECX + 0x2c],0x5
    //   06d5417d  JC  create                     ; i.e. proceed when count < 5
    //   06d54196  MOV AL,0x1                     ; and RETURN TRUE when full
    // +0x2c is the size member of the std::list at CAccount+0x24, which is what
    // m_Characters.size() is here -- a previous note called this a substitution;
    // it is the same quantity. The `true` on a full account is not a port bug
    // either: retail really does report success without creating anything.
    if (Account->m_Characters.size() >= 5) {
        return true;
    }

    // Retail 0x06d5419e: MOV ECX,EBP / PUSH EBX / CALL FUN_06d4b790 -- the save
    // file is written FIRST, with the slot as `this` and the new character name
    // as the argument, and its boolean result is both the gate on everything
    // below (0x06d541ab JZ) and this function's return value. Retail does not
    // touch the slot's character name here.
    if (!PlayerData_CreateNewCharacter(*Player, Info)) {
        return false;
    }

    // Add the character to the account's character list. Retail pushes the
    // NAME onto the std::list at account+0x24 (FUN_06d57350, head at +0x28),
    // which also bumps the size member at +0x2c -- it is not a separate counter.
    Account->m_Characters.push_back(CiWString(Info.Name));

    // Register in the character ID map (this+0x30 = m_CharacterIDMap), keyed by
    // the lowercased "<hash>.fl", value = the same bare CAccount* retail stores
    // at node+0x1c.
    m_CharacterIDMap[CharKey] = Account;

    return true;
}

// ===========================================================================
// Account management helpers (static, file-local)
// ===========================================================================

// BuildAccountDir — builds full path: <UserData>/Accts[\\MultiPlayer]/<DirName>
// In MP mode DirName = sprintf("%02x-%08x", wcslen, AccountCrc(name_bytes));
// in SP mode DirName = wcstombs(name).
// Retail hashes the raw bytes of the wstring via the account CRC (same table as character
// ID hashing). Account names are NOT lowercased before hashing (unlike character
// IDs which are lowercased).
static void BuildAccountDir(char* OutPath, const wchar_t* AccountName) {
    char BaseDir[260];
    GetUserDataPath(BaseDir);
    char AcctsDir[260];
    if (!SinglePlayer()) {
        // Multiplayer: account-CRC-hash account name for directory.
        // Retail (0x71d20, 0x719f0, 0x72040, ...) always runs _wcsdup + _wcslwr
        // on the name first and hashes the LOWERCASED copy; without that, a
        // mixed-case account name resolves to a different folder than the one
        // init created.
        std::wstring Lowered(AccountName);
        for (unsigned int i = 0; i < Lowered.size(); ++i) {
            Lowered[i] = (wchar_t)towlower(Lowered[i]);
        }
        AccountName = Lowered.c_str();
        size_t NameLen = wcslen(AccountName);
        unsigned int Hash = Server_AccountCrc(
            reinterpret_cast<const unsigned char*>(AccountName),
            (unsigned int)(NameLen * sizeof(wchar_t)));
        char HashedName[64];
        sprintf_s(HashedName, sizeof(HashedName), "%02x-%08x", (unsigned int)NameLen, Hash);
        _makepath(AcctsDir, nullptr, BaseDir, "Accts\\MultiPlayer", nullptr);
        _makepath(OutPath, nullptr, AcctsDir, HashedName, nullptr);
    } else {
        // Single player: use account name directly
        char NarrowName[260];
        wcstombs(NarrowName, AccountName, 260);
        _makepath(AcctsDir, nullptr, BaseDir, "Accts", nullptr);
        _makepath(OutPath, nullptr, AcctsDir, NarrowName, nullptr);
    }
}

// GetAccountsBaseDir — returns base accounts directory path.
static void GetAccountsBaseDir(char* OutPath) {
    char BaseDir[260];
    GetUserDataPath(BaseDir);
    if (!SinglePlayer()) {
        _makepath(OutPath, nullptr, BaseDir, "Accts\\MultiPlayer", nullptr);
    } else {
        _makepath(OutPath, nullptr, BaseDir, "Accts", nullptr);
    }
}

// XorEncodeWString — XOR each wchar_t with a 16-bit key derived from the hash.
static void XorEncodeWString(unsigned short* Out, const wchar_t* In, unsigned short Key) {
    while (*In != L'\0') {
        *Out = (unsigned short)(*In ^ Key);
        ++In;
        ++Out;
    }
}

// ===========================================================================
// PlayerDB::GetCharactersForAccount — retail 0x719E0 (ordinal 171)
// .text:06d519e0  MOV AL,0x1 / RET 0x8 -- the whole function. It reads neither
// argument and leaves the list untouched (ICF-folded with
// IServerImpl::SwapConnections). The port used to fill the list from the
// account's character names, which retail never does.
// ===========================================================================
bool PlayerDB::GetCharactersForAccount(CiWString& AccountName, std::list<CiWString>& OutCharacters) {
    (void)AccountName;
    (void)OutCharacters;
    return true;
}

// ===========================================================================
// PlayerDB::SetAccountPassword — retail 0x719F0 (ordinal 407)
// Builds account dir path, checks existence, writes XOR-encoded password file.
// ===========================================================================
bool PlayerDB::SetAccountPassword(CiWString& AccountName, CiWString& Password) {
    const wchar_t* NamePtr = AccountName.c_str();

    // Build account directory path
    char AccountDir[260];
    BuildAccountDir(AccountDir, NamePtr);

    // Retail checks _access(dir, 6) — read AND write permission, not mere
    // existence.
    if (_access(AccountDir, 6) != 0) {
        return false;
    }

    // Build password file path
    char PasswordPath[260];
    _makepath(PasswordPath, nullptr, AccountDir, "password", nullptr);

    // Write XOR-encoded password to file
    FILE* File = fopen(PasswordPath, "wb");
    if (File != nullptr) {
        const wchar_t* PassPtr = Password.c_str();
        size_t PassLen = wcslen(PassPtr);

        // Retail 0x06d519f0 runs the same configurable CRC engine it uses for
        // the account-directory hash (table at DAT_06d8fd38, init DAT_06d90144,
        // xor-out DAT_06d90148) over wcslen(password)*2 raw bytes, then takes
        // the low 16 bits of the result as the XOR key. Note this makes the
        // file a verification token, not reversible storage: the key depends on
        // the plaintext, so checking a password means re-encoding the candidate
        // and comparing bytes.
        unsigned short XorKey = (unsigned short)Server_AccountCrc(
            reinterpret_cast<const unsigned char*>(PassPtr),
            (unsigned int)(PassLen * sizeof(wchar_t)));

        unsigned short Encoded[1024];
        XorEncodeWString(Encoded, PassPtr, XorKey);
        fwrite(Encoded, 2, PassLen, File);
        fclose(File);
    }
    return true;
}

// ===========================================================================
// PlayerDB::BanAccount — retail 0x71D20 (ordinal 62)
// Looks up account in map; if found, creates or deletes "banned" marker file
// and sets the account's banned flag.
// ===========================================================================
bool PlayerDB::BanAccount(CiWString& AccountName, bool bBan) {
    // Look up account in our map
    std::map<CiWString, CAccount*>::iterator It = m_AccountNameMap.find(AccountName);
    if (It == m_AccountNameMap.end() || It->second == nullptr) {
        return false;
    }
    CAccount* Account = It->second;

    // Build account directory and "banned" file path
    const wchar_t* NamePtr = AccountName.c_str();
    char AccountDir[260];
    BuildAccountDir(AccountDir, NamePtr);

    char BannedPath[260];
    _makepath(BannedPath, nullptr, AccountDir, "banned", nullptr);

    if (bBan) {
        // Create the "banned" marker file
        FILE* File = fopen(BannedPath, "wb");
        if (File != nullptr) {
            fclose(File);
        }
    } else {
        // Remove the "banned" marker file
        _unlink(BannedPath);
    }

    // Retail: account[0x31] = !bBan (0x31 is the "not banned" byte).
    Account->m_bNotBanned = !bBan;
    return true;
}

// ===========================================================================
// PlayerDB::GetAccountBanned — retail 0x71FD0 (ordinal 145)
// Looks up account in map; returns the banned flag if found, false otherwise.
// Retail checks account[0x31] == '\0' (0 = banned, 1 = not banned).
// ===========================================================================
bool PlayerDB::GetAccountBanned(CiWString& AccountName) {
    // .text:06d51fe0 .. 06d52034 (verified 2026-09-24, claude-code:w5-server-06d4):
    // lower_bound + compare = find; miss or null account -> 0 (.text:06d52031);
    // otherwise SETZ on the byte at account+0x31 (.text:06d52028).
    std::map<CiWString, CAccount*>::iterator It = m_AccountNameMap.find(AccountName);
    if (It == m_AccountNameMap.end() || It->second == nullptr) {
        return false;
    }
    CAccount* Account = It->second;
    return Account->m_bNotBanned == false;                                  // .text:06d52028
}

// ===========================================================================
// PlayerDB::DeleteAccount — retail 0x72040 (ordinal 98)
// Looks up account in map; force-logs out if active; deletes all files in the
// account directory; removes directory; erases account from map and deletes it.
// ===========================================================================
void PlayerDB::DeleteAccount(CiWString& AccountName) {
    const wchar_t* NamePtr = AccountName.c_str();

    // Look up account in map
    std::map<CiWString, CAccount*>::iterator It = m_AccountNameMap.find(AccountName);
    CAccount* Account = nullptr;
    if (It != m_AccountNameMap.end()) {
        Account = It->second;
    }

    // Retail drops the account's live client before touching anything on disk:
    // (**(code**)(*DAT_06d8fb14 + 0x44))(*(int*)(account + 0x34)) — IClient
    // slot 17 with the stored client id, the same call LockAccountAccess makes.
    if (Account != nullptr && Account->m_ClientID != 0) {
        extern IClient* g_pClientInterface;
        if (g_pClientInterface) {
            g_pClientInterface->DisconnectClient(Account->m_ClientID);
        }
    }

    // Build account directory path
    char AccountDir[260];
    BuildAccountDir(AccountDir, NamePtr);

    // Delete all files in the account directory
    char SearchPath[260];
    _makepath(SearchPath, nullptr, AccountDir, "*.*", nullptr);

    struct _finddata_t FindData;
    intptr_t FindHandle = _findfirst(SearchPath, &FindData);
    if (FindHandle != -1) {
        do {
            // Skip "." and ".." directories and subdirectories
            if (FindData.name[0] != '.' && (FindData.attrib & _A_SUBDIR) == 0) {
                char FilePath[260];
                _makepath(FilePath, nullptr, AccountDir, FindData.name, nullptr);
                _unlink(FilePath);
            }
        } while (_findnext(FindHandle, &FindData) == 0);
        _findclose(FindHandle);
    }

    // Remove the (now empty) directory
    _rmdir(AccountDir);

    // .text:06d521e7 .. 06d52218: !SinglePlayer() logs through the FDUMP hook
    // (0x100003, .rdata:06d6b8b8), not OutputDebugString.
    if (!SinglePlayer()) {
        FDUMP(0x100003, "LOG: Delete account %S \"%s\"\n", NamePtr, AccountDir);
    }

    // Force logout and delete account object
    if (Account != nullptr) {
        Account->ForceLogout();

        // Retail walks the CAccount's own character list (head at account+0x28),
        // rebuilds each "<hash>.fl" key and erases it from the character-id
        // container at this+0x30 before erasing the account itself. Leaving
        // those entries behind leaves dangling CAccount* pointers in
        // m_CharacterIDMap. Erasing by stored pointer is equivalent and does
        // not depend on reproducing the key spelling.
        std::map<CiString, CAccount*>::iterator CharIt =
            m_CharacterIDMap.begin();
        while (CharIt != m_CharacterIDMap.end()) {
            if (CharIt->second == Account) {
                m_CharacterIDMap.erase(CharIt++);
            } else {
                ++CharIt;
            }
        }

        // Erase from map
        m_AccountNameMap.erase(It);

        // Delete the CAccount object
        delete Account;
    }
}

// ===========================================================================
// PlayerDB::CreateAccount — retail 0x723E0 (ordinal 89)
// Builds account dir path; if dir already exists returns false.
// Creates directory, writes XOR-encoded "name" file, allocates CAccount,
// calls InitFromFolder, inserts into account map.
// ===========================================================================
bool PlayerDB::CreateAccount(CiWString& AccountName) {
    const wchar_t* NamePtr = AccountName.c_str();

    // Build account directory path
    char AccountDir[260];
    BuildAccountDir(AccountDir, NamePtr);

    // Retail checks _access(dir, 6) — read AND write — both before and after
    // BuildDirectory, and returns false when the directory already exists.
    if (_access(AccountDir, 6) == 0) {
        return false;
    }

    // Create the directory
    BuildDirectory(AccountDir);

    // Verify it was created
    if (_access(AccountDir, 6) != 0) {
        return false;
    }

    // Write XOR-encoded "name" file
    char NamePath[260];
    _makepath(NamePath, nullptr, AccountDir, "name", nullptr);

    FILE* NameFile = fopen(NamePath, "wb");
    if (NameFile == nullptr) {
        // Could not create name file — clean up
        DeleteAccount(AccountName);
        return false;
    }

    // XOR encode the account name with the constant marker-string key.
    size_t NameLen = wcslen(NamePtr);
    unsigned short XorKey = AccountNameFileXorKey();
    unsigned short Encoded[1024];
    XorEncodeWString(Encoded, NamePtr, XorKey);
    fwrite(Encoded, 2, NameLen, NameFile);
    fclose(NameFile);

    // .text:06d525c6 .. 06d525f9: !SinglePlayer() logs through the FDUMP hook
    // (0x100003), not OutputDebugString (fixed 2026-09-24, claude-code:w5-server-06d4).
    if (!SinglePlayer()) {
        FDUMP(0x100003, "LOG: Create account %S \"%s\"\n", NamePtr, AccountDir);
    }

    // .text:06d525fe .. 06d52682: operator new(0x38) and the default ctor inlined
    // (name/directory strings empty, list head, +0x31 = 1, +0x30/+0x32/+0x34 = 0).
    // Nothing else is written here: the port's name copy and flag stores were not
    // retail; InitFromFolder fills the name from the "name" file just written.
    CAccount* NewAccount = new CAccount();
    NewAccount->InitFromFolder(AccountDir);                                 // .text:06d52697

    // .text:06d5269c .. 06d526f4: operator[] keyed by a copy of the ACCOUNT's
    // own +0x04 name (FUN_06d56e50 / FUN_06d59b00), node+0x1c = the account.
    m_AccountNameMap[NewAccount->m_Name] = NewAccount;

    return true;
}

// ===========================================================================
// CPlayerGroup class implementation
// ===========================================================================

// (`Players` is declared in common_server.h and defined at the bottom of this
// file; CPlayerGroup's member add/remove paths route the group-id notification
// through it, as retail does on .data:Players.)

// Retail .data:06d90400, the protected static CPlayerGroup::s_GroupIDToGroupPtrMap
// (export ordinal 508; see common_server.h for the declared type).
CPlayerGroup::GroupMapType CPlayerGroup::s_GroupIDToGroupPtrMap;

// Retail .data:06d90450, zero-initialised: the ctor pre-increments and the dtor
// gives the id back when it was the last one handed out.
unsigned int CPlayerGroup::s_uiGroupID = 0;

// ---------------------------------------------------------------------------
// CPlayerGroup::CPlayerGroup — retail 0x7b320 (0x06d5b320), ordinal 7
// Two empty std::set<uint> trees, then
//     m_GroupID = ++s_uiGroupID;                       (0x06d5b3f3-0x06d5b400)
//     s_GroupIDToGroupPtrMap[m_GroupID] = this;        (0x06d5b40d-0x06d5b42c)
// Note the PRE-increment: retail reads s_uiGroupID, INCs, stores it back and
// only then copies it into [EDI+4].
// ---------------------------------------------------------------------------
CPlayerGroup::CPlayerGroup() {
    m_GroupID = ++s_uiGroupID;
    s_GroupIDToGroupPtrMap[m_GroupID] = this;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::CPlayerGroup(const CPlayerGroup&) — retail 0x1100 (0x06ce1100),
// ordinal 6, and CPlayerGroup::operator= — retail 0x1280 (0x06ce1280), ordinal
// 26. Both are exported and neither has a single call site inside Server.dll
// (their only XREFs are the entry-point table and the export thunks at
// 0x06d8004c / 0x06d808cc): the familiar exported-but-uncalled special-member
// pattern. Retail copies m_GroupID (0x06ce111e) and both sets, and does NOT
// register the copy in s_GroupIDToGroupPtrMap.
// ---------------------------------------------------------------------------
CPlayerGroup::CPlayerGroup(const CPlayerGroup& Other) {
    m_GroupID = Other.m_GroupID;
    m_Members = Other.m_Members;
    m_Invites = Other.m_Invites;
}

CPlayerGroup& CPlayerGroup::operator=(const CPlayerGroup& Other) {
    if (this != &Other) {
        m_GroupID = Other.m_GroupID;
        m_Members = Other.m_Members;
        m_Invites = Other.m_Invites;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::~CPlayerGroup — retail 0x7b440 (0x06d5b440), ordinal 20
// 0x06d5b467-0x06d5b482:  if (m_GroupID == s_uiGroupID) --s_uiGroupID;
// 0x06d5b487-0x06d5b4cb:  GGroupIDToGroupPtrMap.erase(m_GroupID)
// then both trees are torn down. The id give-back was missing from the port.
// ---------------------------------------------------------------------------
CPlayerGroup::~CPlayerGroup() {
    if (m_GroupID == s_uiGroupID) {
        --s_uiGroupID;
    }
    s_GroupIDToGroupPtrMap.erase(m_GroupID);
}

// ---------------------------------------------------------------------------
// CPlayerGroup::FromGroupID — retail 0x7b5c0 (0x06d5b5c0), ordinal 138
// lower_bound + the usual !(key < it->first) recheck (0x06d5b5e4), then
// it->second from node+0x10 (0x06d5b603).
// ---------------------------------------------------------------------------
CPlayerGroup* CPlayerGroup::FromGroupID(unsigned int GroupId) {
    GroupMapType::iterator It = s_GroupIDToGroupPtrMap.find(GroupId);
    if (It != s_GroupIDToGroupPtrMap.end()) {
        return It->second;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::AddMember -- retail .text:06d5b610 .. .text:06d5b90c, ordinal 53,
// thiscall (PlayerId), RET 4. Rewritten 2026-09-24 (claude-code:w5-server-06d4)
// from the listing; every branch below is retail's.
//   06d5b61d..06d5b63a  Joiner = &Players[PlayerId] (raw IMUL 0x418, no guard)
//   06d5b63e..06d5b690  for each member M: FUN_06d190d0(CHAT_ID{M}, 6,
//                       CHAT_ID{PlayerId}) -- the dword pushed at 06d5b657 is the
//                       joiner's id as a word plus two zero bytes
//   06d5b692..06d5b6df  for each member M: FUN_06d190d0(CHAT_ID{PlayerId}, 6,
//                       CHAT_ID{M})
//   06d5b6e1..06d5b726  GroupMission = the first member's non-zero +0x408, else 0
//   06d5b72a..06d5b784  GroupSetBy   = the first member's non-zero +0x40c, else 0
//                       (a separate scan)
//   06d5b786..06d5b796  if Joiner+0x408 == 0 || GroupMission != 0:
//     06d5b8e7..06d5b906  Joiner+0x408 = GroupMission, Joiner+0x40c = GroupSetBy,
//                         IClient +0x1c8 (SendServerMiscObjUpdateMission)(PlayerId,
//                         GroupMission, GroupSetBy)
//                       else (the joiner brings a mission into a group without one):
//     06d5b79c..06d5b808  every member gets the joiner's +0x408/+0x40c and a
//                         SendServerMiscObjUpdateMission(member, those two)
//   06d5b81b            m_Members.insert(PlayerId)
//   06d5b841            PlayerDB::SendGroupID(PlayerId, m_GroupID)
//   06d5b84a            Joiner+0x404 (group_ptr) = this
//   06d5b85b            FUN_06d43650(0xa, &{PlayerId}) -- controller broadcast
//   06d5b871..06d5b8d6  FUN_06d006c0(&Joiner+0x3e4 ship id, &obj, &owner); on a hit
//                       with obj != 0, obj->cobject() (vt +0x150) non-null and a
//                       CShip ((class & 0x503) == 0x503): ship+0x1b4 = m_GroupID,
//                       then SysDB::SysMap.find(Joiner+0x3e0); when found,
//                       FUN_06d0f1c0(&system, ship id) -- StarSystem_SendGroupChange
//   06d5b8de            return true -- no IsFull test anywhere (the callers do it)
// No IChatClient / IClient null test anywhere. FUN_06d006c0 is
// Server_LookupGameObject in this port (null on a miss covers both early-outs).
// ---------------------------------------------------------------------------
bool CPlayerGroup::AddMember(unsigned int PlayerId) {
    PlayerData& Joiner = Players[PlayerId];                                 // .text:06d5b62b

    std::set<unsigned int>::const_iterator It;
    for (It = m_Members.begin(); It != m_Members.end(); ++It) {            // .text:06d5b650
        FGroupMsgPayload Payload;
        Payload.MsgType = (unsigned int)BaseGroupMessage::MemberJoined;    // 6
        Payload.Param = MakeChatId(PlayerId);                              // .text:06d5b657
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(*It)), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
    }
    for (It = m_Members.begin(); It != m_Members.end(); ++It) {            // .text:06d5b6a0
        FGroupMsgPayload Payload;
        Payload.MsgType = (unsigned int)BaseGroupMessage::MemberJoined;    // 6
        Payload.Param = MakeChatId(*It);                                   // .text:06d5b6b2
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(PlayerId)), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
    }

    unsigned int GroupMission = 0;                                         // .text:06d5b728
    for (It = m_Members.begin(); It != m_Members.end(); ++It) {
        if (Players[*It].group_mission_id != 0) {                          // .text:06d5b70e
            GroupMission = Players[*It].group_mission_id;                  // .text:06d5b774
            break;
        }
    }
    unsigned int GroupSetBy = 0;                                           // .text:06d5b784
    for (It = m_Members.begin(); It != m_Members.end(); ++It) {
        if (Players[*It].group_mission_set_by != 0) {                      // .text:06d5b752
            GroupSetBy = Players[*It].group_mission_set_by;                // .text:06d5b77c
            break;
        }
    }

    unsigned int const JoinerMission = Joiner.group_mission_id;            // .text:06d5b786
    if (JoinerMission == 0 || GroupMission != 0) {
        Joiner.group_mission_id = GroupMission;                            // .text:06d5b8ec
        Joiner.group_mission_set_by = GroupSetBy;                          // .text:06d5b8f2
        g_pClientInterface->SendServerMiscObjUpdateMission(PlayerId, GroupMission, GroupSetBy); // .text:06d5b906
    } else {
        unsigned int const JoinerSetBy = Joiner.group_mission_set_by;      // .text:06d5b7a0
        for (It = m_Members.begin(); It != m_Members.end(); ++It) {
            PlayerData& Member = Players[*It];
            Member.group_mission_id = JoinerMission;                       // .text:06d5b7ce
            Member.group_mission_set_by = JoinerSetBy;                     // .text:06d5b7d4
            g_pClientInterface->SendServerMiscObjUpdateMission(*It, JoinerMission, JoinerSetBy); // .text:06d5b7ec
        }
    }

    m_Members.insert(PlayerId);                                            // .text:06d5b81b
    Players.SendGroupID(PlayerId, m_GroupID);                              // .text:06d5b841
    Joiner.group_ptr = this;                                               // .text:06d5b84a
    unsigned int JoinEvent = PlayerId;                                     // .text:06d5b857
    Server_ControllerDispatch(0, pub::Controller::EMessage::GroupMemberJoined, &JoinEvent); // .text:06d5b85b

    // .text:06d5b860 .. .text:06d5b8d6 -- stamp the group on the joiner's ship.
    GameObject* const ShipObj = Server_LookupGameObject(Joiner.ship_object_id);
    if (ShipObj == nullptr)
        return true;
    CObject const* const Cobj = ShipObj->cobject();                        // .text:06d5b887
    if (Cobj == nullptr)
        return true;
    if ((static_cast<unsigned int>(Cobj->ObjectClass) & 0x503u) != 0x503u)
        return true;
    const_cast<CShip*>(static_cast<CShip const*>(Cobj))->GroupId = m_GroupID; // .text:06d5b8a5
    SysDB::SysMapType::iterator System = SysDB::SysMap.find(Joiner.system_id); // .text:06d5b8bf
    if (System != SysDB::SysMap.end())
        StarSystem_SendGroupChange(System->second, Joiner.ship_object_id); // .text:06d5b8d6
    return true;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::DelMember -- retail .text:06d5b920 .. .text:06d5bb79, ordinal 96,
// thiscall (PlayerId), RET 4. Rewritten 2026-09-24 (claude-code:w5-server-06d4)
// from the listing; every branch below is retail's.
//   06d5b94d..06d5b95d  m_Members.find(PlayerId); == end() -> return false
//   06d5b962..06d5b971  FUN_06d43650(9, &{PlayerId}) -- controller broadcast,
//                       BEFORE the erase
//   06d5b985            m_Members.erase(it)
//   06d5b98a..06d5b9a0  Leaver = &Players[PlayerId] (raw IMUL 0x418, no guard)
//   06d5b9c0..06d5ba00  for each remaining member M: FUN_06d190d0(CHAT_ID{M}, 8,
//                       CHAT_ID{PlayerId}) -- the dword pushed at 06d5b9c0 is the
//                       leaver's id as a word plus two zero bytes
//   06d5ba02..06d5ba2d  FUN_06d190d0(CHAT_ID{PlayerId}, 7, CHAT_ID{PlayerId})
//   06d5ba35..06d5ba50  Leaver+0x408 = Leaver+0x40c = 0; IClient +0x1c8
//                       (SendServerMiscObjUpdateMission)(PlayerId, 0, 0)
//   06d5ba56..06d5ba96  a 0x40 record at [ESP+0x24]: +0x00, +0x04, +0x08 = 0,
//                       +0x3c byte = 0, two zeroed FmtStrs at +0x0c / +0x24
//                       (FUN_06d55bc0 is the inlined FmtStr() body); IClient
//                       +0x120 (SendServerSetMissionObjectives)(&PlayerId, &record) -- the id goes BY
//                       ADDRESS (LEA EAX,[ESP+0x78] = &param_1)
//   06d5baa7            PlayerDB::SendGroupID(PlayerId, 0)
//   06d5babd            Leaver+0x404 (group_ptr) = 0
//   06d5bac3..06d5baf2  FUN_06d006c0(&Leaver+0x3e4 ship id, &obj, &owner); on a hit
//                       with obj != 0, obj->cobject() (vt +0x150) non-null and a
//                       CShip ((class & 0x503) == 0x503):
//   06d5bb05              ship+0x1b4 = 0
//   06d5bb0b..06d5bb3e    SysDB::SysMap.find(Leaver+0x3e0) (FUN_06d5c810 is the
//                         lower_bound, 06d5bb1f re-tests the key); when found,
//                         FUN_06d0f1c0(&system, ship id) -- StarSystem_SendGroupChange
//   06d5bb65            return true
// No IChatClient / IClient null test anywhere. FUN_06d006c0 is
// Server_LookupGameObject in this port (null on a miss covers both early-outs).
// ---------------------------------------------------------------------------
bool CPlayerGroup::DelMember(unsigned int PlayerId) {
    std::set<unsigned int>::iterator Found = m_Members.find(PlayerId);    // .text:06d5b94d
    if (Found == m_Members.end())
        return false;                                                      // .text:06d5b95b

    unsigned int LeaveEvent = PlayerId;                                    // .text:06d5b96d
    Server_ControllerDispatch(0, pub::Controller::EMessage::GroupMemberLeft, &LeaveEvent); // .text:06d5b971
    m_Members.erase(Found);                                                // .text:06d5b985

    PlayerData& Leaver = Players[PlayerId];                                // .text:06d5b9a0

    std::set<unsigned int>::const_iterator It;
    for (It = m_Members.begin(); It != m_Members.end(); ++It) {            // .text:06d5b9c0
        FGroupMsgPayload Payload;
        Payload.MsgType = (unsigned int)BaseGroupMessage::MemberLeft;      // 8
        Payload.Param = MakeChatId(PlayerId);                              // .text:06d5b9c7
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(*It)), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
    }
    {
        FGroupMsgPayload Payload;
        Payload.MsgType = (unsigned int)BaseGroupMessage::RemovedFromGroup; // 7
        Payload.Param = MakeChatId(PlayerId);                              // .text:06d5ba11
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(PlayerId)), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
    }

    Leaver.group_mission_id = 0;                                           // .text:06d5ba35
    Leaver.group_mission_set_by = 0;                                       // .text:06d5ba3b
    g_pClientInterface->SendServerMiscObjUpdateMission(PlayerId, 0, 0);    // .text:06d5ba50

    FServerSetMissionObjectivesInfo NoObjectives;                          // .text:06d5ba56
    NoObjectives.Field_0x00 = 0;                                           // .text:06d5ba7b
    NoObjectives.Entries = nullptr;                                        // .text:06d5ba87
    NoObjectives.Count = 0;                                                // .text:06d5ba7f
    NoObjectives.Field_0x3c = 0;                                           // .text:06d5ba83
    g_pClientInterface->SendServerSetMissionObjectives(PlayerId, NoObjectives); // .text:06d5ba96

    Players.SendGroupID(PlayerId, 0);                                      // .text:06d5baa7
    Leaver.group_ptr = nullptr;                                            // .text:06d5babd

    // .text:06d5bac3 .. .text:06d5bb3e -- clear the group on the leaver's ship.
    GameObject* const ShipObj = Server_LookupGameObject(Leaver.ship_object_id);
    if (ShipObj == nullptr)
        return true;
    CObject const* const Cobj = ShipObj->cobject();                        // .text:06d5bad9
    if (Cobj == nullptr)
        return true;
    if ((static_cast<unsigned int>(Cobj->ObjectClass) & 0x503u) != 0x503u)
        return true;
    const_cast<CShip*>(static_cast<CShip const*>(Cobj))->GroupId = 0;     // .text:06d5bb05
    SysDB::SysMapType::iterator System = SysDB::SysMap.find(Leaver.system_id); // .text:06d5bb0b
    if (System != SysDB::SysMap.end())
        StarSystem_SendGroupChange(System->second, Leaver.ship_object_id); // .text:06d5bb3e
    return true;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::IsMember — retail 0x7bb90 (0x06d5bb90), ordinal 316
// lower_bound on m_Members, then SETNZ against end() (0x06d5bbc5).
// ---------------------------------------------------------------------------
bool CPlayerGroup::IsMember(unsigned int PlayerId) {
    return m_Members.find(PlayerId) != m_Members.end();
}

// ---------------------------------------------------------------------------
// CPlayerGroup::AddInvite — retail 0x7bbf0 (0x06d5bbf0), ordinal 52
// A plain m_Invites.insert with the hint machinery inlined; MOV AL,1 on every
// exit (0x06d5bc44 / 0x06d5bc85), so it can never fail -- there is no
// eight-invite cap, which the old port invented.
// ---------------------------------------------------------------------------
bool CPlayerGroup::AddInvite(unsigned int PlayerId) {
    m_Invites.insert(PlayerId);
    return true;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::DelInvite — retail 0x7bc90 (0x06d5bc90), ordinal 95
// lower_bound, false when absent (0x06d5bcd0), otherwise erase and true.
// ---------------------------------------------------------------------------
bool CPlayerGroup::DelInvite(unsigned int PlayerId) {
    std::set<unsigned int>::iterator It = m_Invites.find(PlayerId);
    if (It == m_Invites.end()) {
        return false;
    }
    m_Invites.erase(It);
    return true;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::IsInvited — retail 0x7bd00 (0x06d5bd00), ordinal 315
// ---------------------------------------------------------------------------
bool CPlayerGroup::IsInvited(unsigned int PlayerId) {
    return m_Invites.find(PlayerId) != m_Invites.end();
}

// ---------------------------------------------------------------------------
// Counters. Retail reads the two trees' _Size words directly:
//   GetMemberCount 0x7bb80 -> MOV EAX,[this+0x18]   (m_Members.size())
//   GetInviteCount 0x7bcf0 -> MOV EAX,[this+0x2c]   (m_Invites.size())
//   GetID          0x10f0  -> MOV EAX,[this+0x04]
//   IsFull         0x10e0  -> CMP [this+0x18],8 / SBB EAX,EAX / INC EAX
// All four are QAE, i.e. NOT const -- the port used to declare them const,
// which changes the exported mangled name.
// ---------------------------------------------------------------------------
unsigned int CPlayerGroup::GetMemberCount() { return (unsigned int)m_Members.size(); }
unsigned int CPlayerGroup::GetInviteCount() { return (unsigned int)m_Invites.size(); }
unsigned int CPlayerGroup::GetID() { return m_GroupID; }
bool CPlayerGroup::IsFull() { return m_Members.size() >= 8; }

// ---------------------------------------------------------------------------
// CPlayerGroup::HandleClientLogout — retail 0x7bd60 (0x06d5bd60), ordinal 299
//   m_Invites.erase(PlayerId) when present  (0x06d5bd7f-0x06d5bd95)
//   DelMember(PlayerId)                     (0x06d5bd9d)
//   if (m_Members.size() == 0) vftable[0](1) -- the scalar deleting destructor
//   with the delete flag, i.e. `delete this` (0x06d5bda2-0x06d5bdaf).
// That self-destruct was missing from the port, so empty groups leaked and
// stayed reachable through FromGroupID.
// ---------------------------------------------------------------------------
void CPlayerGroup::HandleClientLogout(unsigned int PlayerId) {
    std::set<unsigned int>::iterator It = m_Invites.find(PlayerId);
    if (It != m_Invites.end()) {
        m_Invites.erase(It);
    }
    DelMember(PlayerId);
    if (m_Members.empty()) {
        delete this;
    }
}

// ---------------------------------------------------------------------------
// CPlayerGroup::SendGroup — retail 0x7bdc0 (0x06d5bdc0), ordinal 402
// For every member: FUN_06d19180 (0x06d5bdfc) makes CHAT_ID{member} and
// FUN_06d190d0 (0x06d5be01) -- whose entire body at 0x06d190d0 is
//     IChatClient::SendCommonChatMsg(CHAT_ID, DAT_06d8f5dc, 8, &{ MsgType, (uint)Param })
// -- ships it. The 8 is sizeof(payload), not a message id.
// ---------------------------------------------------------------------------
void CPlayerGroup::SendGroup(BaseGroupMessage::Type MsgType, unsigned int Param) {
    // No null test on DAT_06d8fb18: FUN_06d190d0 loads it and calls slot 0
    // directly (.text:06d190d0..06d190f5); the guard the port had was removed
    // 2026-09-24 (s2).
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        FGroupMsgPayload Payload;
        Payload.MsgType = (unsigned int)MsgType;
        Payload.Param = Param;
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(*It)), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
    }
}

// CPlayerGroup::SendChat -- retail .text:06d5be60 .. .text:06d5bf1c (cdecl, `this`
// on the stack, ?SendChat@CPlayerGroup@@QAAXHPBGZZ). Ported 1:1 2026-09-24
// (claude-code:w5-server-06d4).
//   .text:06d5be7d  vswprintf(local[0x400 wchar], Msg, varargs) -- unbounded
//   .text:06d5bea1 .. 06d5bf12  for each member of the set at this+0xc:
//                   FUN_06d18ea0(Style, CHAT_ID{member} (FUN_06d19180 at 06d5beba),
//                   L"%s" (.rdata:06d6b9b8, PE bytes 25 00 73 00 00 00), local)
// The first int is the StyleNode style FUN_06d18ea0 renders the text with (it is
// pushed from [ESP+0x814] straight into FUN_06d18ea0's ushort slot), not a
// sender id. It is NOT a pub::Controller message: the id 0x3d an earlier port used
// was invented for this site.
void CPlayerGroup::SendChat(int Style, const wchar_t* Msg, ...) {
    wchar_t FormattedMsg[0x400];
    va_list Args;
    va_start(Args, Msg);
    _vswprintf(FormattedMsg, Msg, Args);                                  // .text:06d5be7d
    va_end(Args);

    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        Server_SendStyledChat(static_cast<unsigned short>(Style), MakeChatId(*It),
                              L"%s", FormattedMsg);                         // .text:06d5bec0
    }
}

// CPlayerGroup::DeliverChat — retail 0x7bf20 (0x06d5bf20). Per member:
//     IChatClient::SendCommonChatMsg(CHAT_ID{member}, DAT_06d9042c, MsgType, MsgData)
// (the vtable is loaded from DAT_06d8fb18 at 0x06d5bf40 and slot 0 is called at
// 0x06d5bf6a; FUN_06d19180 at 0x06d5bf5f overwrites the first of the four pushed
// dwords with CHAT_ID{member}). The id 0x3e the port sent here was invented for this
// site -- 0x3e is a real id, but it belongs to the timer-expiry path (0x06d5e011).
// The first argument is the BYTE SIZE and the second the RDL buffer: the one caller,
// IServerImpl::SubmitChat's group branch (.text:06d19d79), pushes the encoded chat
// buffer DAT_06d8e5a8 and its size. Retail has no null check on DAT_06d8fb18 --
// it reloads it per member (0x06d5bf40) -- so neither does this.
void CPlayerGroup::DeliverChat(unsigned long MsgType, const void* MsgData) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(*It)), CHAT_ID(GDeliverChatSourceId),
                                                  MsgType, MsgData);
    }
}

// CPlayerGroup::SetMissionObjectives — retail 0x7bfd0 (0x06d5bfd0). 0x120 is a vtable
// BYTE offset, not a message id: 0x120 / 4 = IClient slot 72
// (FLPACKET_SERVER_SETMISSIONOBJECTIVES). Retail 0x06d5bfea-0x06d5bfef pushes
// &node._Myval (LEA EDX,[ESI+0xc]) and the CMissionObjectives*, i.e. the slot's first
// parameter is `unsigned int const&` -- rpclocal's own body dereferences it
// (0x06d8b1f3: MOV EDX,[ESP+0x28] / MOV EDX,[EDX]) -- SendServerSetMissionObjectives
// takes it as a reference, bound here to the set node's own value.
// (claude-code:w5-server-06d4, 2026-09-24) No IClient null test -- DAT_06d8fb14 is
// loaded and called per member (.text:06d5bfe2..06d5bfef) -- and the address pushed is
// the set node's own value (LEA EDX,[ESI+0xc]), not a copy: both now as retail.
// The record retail calls CMissionObjectives IS the 0x40-byte SETMISSIONOBJECTIVES
// argument (rpc_iclient_args.h's FServerSetMissionObjectivesInfo); common_server.h still
// declares CMissionObjectives as a one-pointer placeholder, so the reference is
// reinterpreted here, as the old dword cast did.
void CPlayerGroup::SetMissionObjectives(CMissionObjectives& Objs) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        g_pClientInterface->SendServerSetMissionObjectives(
            *It, reinterpret_cast<const FServerSetMissionObjectivesInfo&>(Objs));
    }
}

// CPlayerGroup::SetMissionMessage — retail 0x7c050 (0x06d5c050). 0x118 / 4 = IClient
// slot 70 (FLPACKET_SERVER_SET_MISSION_MESSAGE); same &member / &Msg shape as
// SetMissionObjectives above (0x06d5c06a-0x06d5c06f). The id 0x41 the port sent was
// invented for this site. No IClient null test (.text:06d5c062..06d5c06f loads
// DAT_06d8fb14 and calls per member); the node's value is pushed by address
// (LEA EDX,[ESI+0xc]). Guard and copy removed 2026-09-24 (claude-code:w5-server-06d4).
void CPlayerGroup::SetMissionMessage(CSetMissionMessage& Msg) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        g_pClientInterface->SendServerSetMissionMessage(*It, Msg);
    }
}

// ---------------------------------------------------------------------------
// CPlayerGroup::StoreMemberList — retail 0x7c0d0 (0x06d5c0d0), ordinal 455
// One inlined push_back per member (the whole 0x06d5c0f0-0x06d5c294 block is
// std::vector<uint>::insert(end(), *it) expanded). It does NOT clear the
// vector first -- the port's OutMembers.clear() had no counterpart.
// ---------------------------------------------------------------------------
void CPlayerGroup::StoreMemberList(std::vector<unsigned int>& OutMembers) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        OutMembers.push_back(*It);
    }
}

// ---------------------------------------------------------------------------
// CPlayerGroup::RewardMembers -- retail .text:06d5c2b0 .. .text:06d5c32a,
// ordinal 389, thiscall (Amount), RET 4. Per member (in-order walk):
//   .text:06d5c2c2 .. 06d5c2d4  this = &Players[member] (raw IMUL 0x418, no guard)
//   .text:06d5c2db              FUN_06d4f4e0(Amount, 1) -- PlayerData_AdjustCash
//                               with bCountEarned = true (earned counter, MP gate,
//                               clamp, IClient slot 77, controller message 0)
// Rewritten 2026-09-24 (claude-code:w5-server-06d4): the old body inlined a copy
// of the clamp without the MP gate, the slot 77 notification or the controller
// message, and skipped members PlayerDB_GetPlayer rejected.
// ---------------------------------------------------------------------------
void CPlayerGroup::RewardMembers(int Amount) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        PlayerData_AdjustCash(Players[*It], Amount, true);                // .text:06d5c2db
    }
}

// ---------------------------------------------------------------------------
// CPlayerGroup::GetMissionID — retail 0x7c330 (0x06d5c330), ordinal 216
// CPlayerGroup::GetMissionSetBy — retail 0x7c3b0 (0x06d5c3b0), ordinal 217
// The mission is not stored on the group at all: both walk m_Members and
// return the first non-zero PlayerData field (+0x408 resp. +0x40c), zero when
// nobody has one. The port used to `return 0;` unconditionally.
// ---------------------------------------------------------------------------
// Both index Players raw (.text:06d5c342..06d5c34b: IMUL 0x418, disp -0x10 /
// -0xc) -- no is_valid, no null test.
unsigned int CPlayerGroup::GetMissionID() {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        if (Players[*It].group_mission_id != 0) {
            return Players[*It].group_mission_id;
        }
    }
    return 0;
}

unsigned int CPlayerGroup::GetMissionSetBy() {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        if (Players[*It].group_mission_set_by != 0) {
            return Players[*It].group_mission_set_by;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::SetMissionID — retail 0x7c430 (0x06d5c430), ordinal 423
// For every member: +0x408 = MissionId, +0x40c = SetBy (0x06d5c45d/0x06d5c463)
// then IClient slot 114 (+0x1c8, 0x06d5c477) with (member, MissionId, SetBy).
// The port used to have an empty body.
// ---------------------------------------------------------------------------
void CPlayerGroup::SetMissionID(unsigned int MissionId, unsigned int SetBy) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    // Raw Players index (.text:06d5c450/06d5c456) and no interface null test
    // (.text:06d5c469 loads DAT_06d8fb14 and calls).
    for (; It != m_Members.end(); ++It) {
        PlayerData& Player = Players[*It];
        Player.group_mission_id = MissionId;
        Player.group_mission_set_by = SetBy;
        g_pClientInterface->SendServerMiscObjUpdateMission(*It, MissionId, SetBy);
    }
}

// ---------------------------------------------------------------------------
// CPlayerGroup::GetMember — retail 0x7c4c0 (0x06d5c4c0), ordinal 213
// Index < 0 or Index >= m_Members.size() gives 0 (0x06d5c4c8 / 0x06d5c4cd),
// otherwise it advances the set iterator Index times and returns *it. This is
// the one accessor retail declares const (QBE).
// ---------------------------------------------------------------------------
unsigned int CPlayerGroup::GetMember(int Index) const {
    if (Index < 0 || (unsigned int)Index >= m_Members.size()) {
        return 0;
    }
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (int i = 0; i < Index; ++i) {
        ++It;
    }
    return *It;
}

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

// ===========================================================================
// CDeadReckonedVector -- declared once, in common_cobject.h. Server.dll's own
// copies of the members retail compiled from the header (copy ctor .text:06ce2020,
// operator= .text:06ce2180, CalcError .text:06ce1f40, GetSample .text:06ce1f70,
// NormalizeProjectedPosition .text:06ce1f80, NormalizeTrackedPosition
// .text:06ce1fd0, and ??_7 at .rdata:06d64f10) are those inline bodies and the
// implicit copy members of the SHARED_INLINE_API class; this module defines
// nothing for it. The Common-only members resolve through Common.lib.
// ===========================================================================

// ===========================================================================
// CRemotePhysicsSimulation -- declared once in common_server.h; the class is
// Common.dll's. The eight members retail emits into Common.dll, Server.dll and
// freelancer.exe alike (copy ctor ord 8, operator= ord 27, CalcPositionError 70,
// GetLatestUpdate 200, GetSample 258, GetSampleCount 259, GetSimulationTime 273,
// SetLogging 420, and Server's own ??_7 ord 38) are defined inline in that header;
// the class-level SHARED_INLINE_API makes this module emit and export its own
// copies, exactly as retail's Server.dll carries its own (.text:06ce2380 ..
// .text:06ce29b0 and ??_7 .rdata:06d64f18, instruction-identical to Common's
// .text:06283500 .. .text:06283b30 with addresses masked). The seven
// Common-only members -- default ctor, dtor, Reset, tracef, RemoteUpdate, Doit,
// CheckForSync -- have no body here and resolve through Common.lib's import
// thunks, as retail Server.dll imports them (.text:06cf7184 ctor, 06cf531a dtor,
// 06cf5527 Doit, 06cf5c29 Reset, 06cf5d34 CheckForSync). Their only bodies are in
// src/Common/common_physys.cpp. Do not add a body for any CRemotePhysicsSimulation
// member to this file: the copies that used to live here drifted from Common's.
// ===========================================================================

// ===========================================================================
// SrvAsteroid -- the server's per-system asteroid bookkeeping. Retail source
// file "E:\FL\Scratch\Source\Server\Space\SrvAsteroid.cpp" (.rdata:06d6777c,
// the __FILE__ of the one FDUMP below).
//
// Three levels, all owned by the Server.dll-static map DAT_06d8d980:
//   std::map<unsigned int, SrvAsteroidSystem>   one per loaded system
//     SrvAsteroidSystem::m_Fields               one FSrvAsteroidField per
//                                               CmnAsteroid::CAsteroidField
//       FSrvAsteroidField::Cubes                one FSrvAsteroidCube per cube
//                                               that reported changes
//         FSrvAsteroidCube::Objects             the server object standing in
//                                               for each live asteroid/mine
//
// The containers are std:: containers, not VC6 layouts (AGENTS.md "No retail
// ABI"). Every member keeps its retail offset in a comment.
//
// The server objects themselves are MedAsteroid (ctor FUN_06d04ba0, vtable
// .rdata:06d67894) and MedMine (ctor FUN_06d04c70, vtable .rdata:06d67aec),
// declared in server_gameobject.h and defined below; StarSystem makes them
// (create_med_asteroid / create_med_mine, FUN_06d09bb0 / FUN_06d09c80).
// ===========================================================================

namespace SrvAsteroid {

namespace {

// .rdata:06d6777c -- the __FILE__ of SrvAsteroidSystem::load's FDUMP.
char const* const SrvAsteroidSource = "E:\\FL\\Scratch\\Source\\Server\\Space\\SrvAsteroid.cpp";

// FUN_06d03cf0 (.text:06d03cf0, cdecl (Dst, Size, Src)); the same body is
// CopyBoundedString in src/Server/server_savegame.cpp. SrvAsteroidSystem::load
// inlines the null arm itself (.text:06d039bb / .text:06d039f7), so only the
// non-null arm is called: length clamped to Size - 1, copied, terminated,
// returned.
unsigned int SrvAsteroid_CopyBounded(char* Dst, unsigned int Size, char const* Src) {
    unsigned int Length = static_cast<unsigned int>(strlen(Src));
    if (!(Length < Size))
        Length = Size - 1;
    memcpy(Dst, Src, Length);
    Dst[Length] = '\0';
    return Length;
}

// DAT_06d8d980. Built by the static initializer LAB_06d03270 (FUN_06ce3730,
// the map constructor) and destroyed by FUN_06d032a0, which that initializer
// registers with atexit (.text:06d03285) -- a namespace-scope object's own
// construction and destruction.
typedef std::map<unsigned int, SrvAsteroidSystem> FSrvAsteroidSystemMap;
FSrvAsteroidSystemMap GSrvAsteroidSystems;

} // namespace

// ---------------------------------------------------------------------------
// MedAsteroid / MedMine. Declarations and the vtable dump in
// src/include/server_gameobject.h.
// ---------------------------------------------------------------------------

// FUN_06d04ba0 (.text:06d04ba0 .. 06d04bc7): FUN_06cee360 (GameObject), then
//   .text:06d04baa  MOV [ESI+0x64],EAX   ; 0
//   .text:06d04bad  MOV [ESI+0x60],AL    ; 0
// and the three vtables (.rdata:06d67894 / 06d67814 / 06d67808).
MedAsteroid::MedAsteroid()
    : Field_0x60(0)
    , Field_0x64(nullptr)
{
}

// FUN_06d04c00 (.text:06d04c00, ECX = the CBase sub-object, so [ESI+0x58] is
// +0x64): the vtables restored, `if ([+0x64] != 0) operator delete([+0x64])`
// (.text:06d04c06 .. 06d04c1f), then `JMP FUN_06cee4a0` -- GameObject's
// destructor, which the compiler runs after this body. The scalar deleting
// destructor FUN_06d04bd0 frees this - 0xc (the object start) when bit 0 of its
// flag is set: plain `delete` of the most derived object.
MedAsteroid::~MedAsteroid()
{
    if (Field_0x64 != nullptr)
        operator delete(Field_0x64);
}

// LAB_06d02cb0 (.text:06d02cb0): `XOR AL,AL / RET 4`. The same body sits in 21
// slots across Server.dll's wrappers (create of MedAsteroid/MedMine, find of
// CounterMeasure/Mine, and more); here it is create().
bool MedAsteroid::create(Archetype::Root* Arch)
{
    (void)Arch;
    return false;
}

// LAB_06d04c30 (.text:06d04c30, RET 4), shared by MedAsteroid and MedMine:
//   .text:06d04c35  PUSH 0x1003 / PUSH Id / CALL CObject::Find
//   .text:06d04c48  MOV [ESI+0x10],EAX          ; CBase::Object
//   .text:06d04c4b  JZ -> skip the advise
//   .text:06d04c52  CBase::advise(1)
//   .text:06d04c58 .. 06d04c5f  return [ESI+0x10] != 0
// Unlike Ship::find (GameObject::FindOfClass), advise runs only on a hit.
bool MedAsteroid::find(unsigned int const& Id)
{
    Object = CObject::Find(Id, CObject::ClassAsteroid);
    if (Object != nullptr)
        advise(true);
    return Object != nullptr;
}

// .text:06d28880 `RET 8` (the listing's IServerImpl::Connect label -- ICF).
void MedAsteroid::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// .text:06d28880 `RET 8`.
void MedAsteroid::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// .text:06d28880 `RET 8`.
void MedAsteroid::explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage)
{
    (void)Blast;
    (void)Damage;
}

// .text:06ce1050 `RET 4` (the listing's CAccount::AppendCharacterNames label).
// MedMine does NOT override this slot; it keeps GameObject's FUN_06ceee70.
void MedAsteroid::apply_damage_list(DamageList& Damage)
{
    (void)Damage;
}

// FUN_06d04c70 (.text:06d04c70 .. 06d04cab): FUN_06cee360 (GameObject), then
//   .text:06d04c78 .. 06d04c83  +0x80 = +0x7c = 0x3f800000 (1.0f)
//   .text:06d04c86 .. 06d04c9a  vtables .rdata:06d67aec / 06d67a6c / 06d67a60,
//                               and the CObjectCB vtable .rdata:06d67a54 at +0x60
//   .text:06d04ca1  +0x78 = 0
// +0x64, +0x68, +0x6c..+0x74 and +0x84 are not touched (arm() writes them);
// the port zeroes them rather than leave indeterminate values in its members.
MedMine::MedMine()
    : Phantom(nullptr)
    , IntruderCount(0)
    , Explosion(nullptr)
    , RechargeTimer(1.0f)
    , RechargeTime(1.0f)
    , ExplosionOffset(0.0f)
{
    Direction.x = 0.0f;
    Direction.y = 0.0f;
    Direction.z = 0.0f;
}

// FUN_06d04ce0 (.text:06d04ce0, ECX = the CBase sub-object, so [ESI+0x58] is
// +0x64): vtables restored (the CObjectCB one included, .text:06d04d11), then
//   .text:06d04d18 .. 06d04d24  PhySys::DestroyPhantom([+0x64]) -- no null test
//   .text:06d04d2f              [+0x64] = 0
//   .text:06d04d3e              FUN_06cee4a0 (GameObject's destructor)
// FUN_06d04cb0 is the scalar deleting destructor (frees this - 0xc).
MedMine::~MedMine()
{
    PhySys::DestroyPhantom(Phantom);
    Phantom = nullptr;
}

// LAB_06d02cb0 -- see MedAsteroid::create.
bool MedMine::create(Archetype::Root* Arch)
{
    (void)Arch;
    return false;
}

// LAB_06d04c30 -- see MedAsteroid::find (the same body, ClassAsteroid 0x1003).
bool MedMine::find(unsigned int const& Id)
{
    Object = CObject::Find(Id, CObject::ClassAsteroid);
    if (Object != nullptr)
        advise(true);
    return Object != nullptr;
}

// .text:06d28880 `RET 8`.
void MedMine::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// .text:06d28880 `RET 8`.
void MedMine::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// .text:06d28880 `RET 8`.
void MedMine::explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage)
{
    (void)Blast;
    (void)Damage;
}

// .text:06d28880 `RET 8` -- MedMine's own +0x1bc (.rdata:06d67ca8): nothing.
void MedMine::Slot1bc(unsigned int Arg1, unsigned int Arg2)
{
    (void)Arg1;
    (void)Arg2;
}

// FUN_06d04d60 (.text:06d04d60 .. 06d04e43, RET 8). Arguments (radius, system
// id): .text:06d04dd4 pushes [entry+4] as CreatePhantom's float, .text:06d04da1
// stores [entry+8] into the parms' first dword. PhantomParms is built on the
// stack at S+4:
//   .text:06d04d6a  +0x68 (IntruderCount) = 0
//   .text:06d04d6d .. 06d04d9d  bytes +4/+5/+6 = 0; +0x10..+0x18 and the
//                   off-diagonal +0x20..+0x28/+0x30..+0x38 = 0; +0x40 = 0
//   .text:06d04da6 .. 06d04db6  1.0f at +0x1c/+0x2c/+0x3c (identity basis)
//   .text:06d04dbe  +0x00 = SystemId
//   .text:06d04dc2 .. 06d04ddc  +0x10 = vt[0]() (get_position, three dwords)
//   .text:06d04de9  +0x40 = this + 0x60 (the CObjectCB sub-object)
//   .text:06d04ded  PhySys::CreatePhantom(parms, radius, &[+0x64])
//   .text:06d04dfa  PhySys::AttachPhantom([+0x10], [+0x64])   ; raw CBase::Object
//   .text:06d04e05  PhySys::SetCollisionStatePhantom([+0x64], true)
//   .text:06d04e11  arch = CObject::get_archetype([+0x10])
//   .text:06d04e1d  +0x78 = Archetype::GetExplosion(arch + 0x24)
//   .text:06d04e26  +0x84 = arch +0x60 (ExplosionOffset)
//   .text:06d04e2f .. 06d04e3c  +0x80 = +0x7c = arch +0x64 (RechargeTime)
// Name[1..9] of the parms is never written (retail leaves stack garbage).
void MedMine::arm(float DetectRadius, unsigned int SystemId)
{
    IntruderCount = 0;

    PhySys::PhantomParms Parms;
    Parms.Flag04 = 0;
    Parms.Flag05 = 0;
    Parms.Name[0] = '\0';
    Parms.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                               0.0f, 1.0f, 0.0f,
                               0.0f, 0.0f, 1.0f);
    Parms.SystemId = SystemId;
    Parms.Position = get_position();
    Parms.Listener = static_cast<PhySys::CObjectCB*>(this);

    PhySys::CreatePhantom(Parms, DetectRadius, Phantom);
    PhySys::AttachPhantom(Object, Phantom);
    PhySys::SetCollisionStatePhantom(Phantom, true);

    Archetype::Asteroid const* const Arch =
        static_cast<Archetype::Asteroid const*>(Object->get_archetype());
    Explosion = Archetype::GetExplosion(Arch->ExplosionArch);
    ExplosionOffset = Arch->ExplosionOffset;
    RechargeTime = Arch->RechargeTime;
    RechargeTimer = Arch->RechargeTime;
}

// LAB_06d04e50 (.text:06d04e50 .. 06d04fa0, RET 4) -- MedMine's update (+0x16c).
//   .text:06d04e56 .. 06d04e65  FCOMP timer (+0x7c) vs limit (+0x80),
//                   `TEST AH,0x5 / JP` -- the add runs only for timer < limit
//   .text:06d04e67 .. 06d04e6e  timer = Dt + timer
//   .text:06d04e71 .. 06d04e78  count (+0x68) == 0 -> done (JBE, unsigned)
//   .text:06d04e7e .. 06d04e8c  `TEST AH,0x1 / JNZ` -- timer < limit -> done
//   .text:06d04e92 .. 06d04ee7  the blast record at S+0x24: +0 = +4 = get_id()
//                   (vt +0x20, twice), +8 = 7 (after a first store of 4),
//                   +0xc = vt[0]() (position), +0x18 = [+0x78], +0x1c = 0
//   .text:06d04eda .. 06d04f2b  Explosion != 0 && Radius > 0.0001 (double
//                   DAT_06d65190, `TEST AH,0x41 / JNZ` = not greater) && (Impulse
//                   > 0.0001 || HullDamage > 0.0001 || EnergyDamage > 0.0001)
//   .text:06d04f35  StarSystem::explode(OwnerSystem (+0x28), record)
//   .text:06d04f3a .. 06d04f80  {get_id(), ExplosionOffset (+0x84) * Direction
//                   (+0x6c/+0x70/+0x74)}, handed to
//   .text:06d04f84  StarSystem::send_mine_explosion(OwnerSystem, record)
//   .text:06d04f89  timer = 0
//   .text:06d04f8c .. 06d04f9c  return !vt +0x160 (is_destroyed)
// The explosion test failing skips only the explode call; the packet and the
// timer reset still happen.
bool MedMine::update(float Dt)
{
    if (RechargeTimer < RechargeTime)
        RechargeTimer = Dt + RechargeTimer;

    if (IntruderCount != 0 && RechargeTimer >= RechargeTime) {
        ExplosionDamageEvent Blast;
        Blast.ExplodingObjId = get_id();
        Blast.InflictorId = get_id();
        // 7 is not one of common_misc.h's (unsourced) DamageCause names; it is
        // the literal at .text:06d04ec4.
        Blast.Cause = static_cast<DamageCause>(7);
        Blast.Position = get_position();
        Blast.Explosion = Explosion;
        Blast.SkipCollisionGroup = nullptr;
        if (Explosion != nullptr && Explosion->Radius > 0.0001 &&
            (Explosion->Impulse > 0.0001 || Explosion->HullDamage > 0.0001 ||
             Explosion->EnergyDamage > 0.0001))
            OwnerSystem->explode(Blast);

        FMineExplosion Rec;
        Rec.Offset.x = ExplosionOffset * Direction.x;
        Rec.Offset.y = ExplosionOffset * Direction.y;
        Rec.Offset.z = ExplosionOffset * Direction.z;
        Rec.MineId = get_id();
        OwnerSystem->send_mine_explosion(Rec);
        RechargeTimer = 0.0f;
    }
    return !is_destroyed();
}

// LAB_06d04fb0 (.text:06d04fb0 .. 06d05060, RET 4) -- CObjectCB slot 0, ECX =
// this + 0x60.
//   .text:06d04fbc .. 06d04fda  GetRoot(Obj); null or (class & 0x503) != 0x503
//                               -> nothing
//   .text:06d04fdc .. 06d05017  Direction = Obj's own WorldPosition (+0x2c;
//                   the ARGUMENT, not its root) minus vt[0]() of the mine
//                   ([ESI-0x60]), stored to +0x6c..+0x74
//   .text:06d0501a .. 06d05032  length = sqrt((x*x + y*y) + z*z), read back
//                               from the stored floats
//   .text:06d0503a  FDIVR DAT_06d65520 (1.0f) -- no zero test
//   .text:06d05040 .. 06d05051  each component times 1/length
//   .text:06d05054 .. 06d05058  ++[+0x68]
void MedMine::added(CObject* Obj)
{
    CObject* const Root = GetRoot(Obj);
    if (Root == nullptr)
        return;
    if ((static_cast<unsigned int>(Root->ObjectClass) & 0x503u) != 0x503u)
        return;

    Vector const& MinePos = get_position();
    Direction.x = Obj->WorldPosition.x - MinePos.x;
    Direction.y = Obj->WorldPosition.y - MinePos.y;
    Direction.z = Obj->WorldPosition.z - MinePos.z;

    float const Length = sqrtf((Direction.x * Direction.x + Direction.y * Direction.y) +
                               Direction.z * Direction.z);
    float const Inverse = 1.0f / Length;
    Direction.x = Inverse * Direction.x;
    Direction.y = Inverse * Direction.y;
    Direction.z = Inverse * Direction.z;

    ++IntruderCount;
}

// LAB_06d05070 (.text:06d05070 .. 06d0509a, RET 4) -- CObjectCB slot 1:
// GetRoot(Obj); a ship ((class & 0x503) == 0x503) -> --[+0x68] (.text:06d05096).
void MedMine::deleted(CObject* Obj)
{
    CObject* const Root = GetRoot(Obj);
    if (Root == nullptr)
        return;
    if ((static_cast<unsigned int>(Root->ObjectClass) & 0x503u) != 0x503u)
        return;
    --IntruderCount;
}

// ---------------------------------------------------------------------------
// The value of a field entry's cube map: node+0x10, 0x24 bytes. Offsets from
// FUN_06d04480's temporary (.text:06d044f3 .. 06d04503: +0x00/+0x04/+0x08 = 0,
// +0x0c = 0xfa) and its post-insert stores (.text:06d045cf .. 06d045e7).
// ---------------------------------------------------------------------------
struct FSrvAsteroidCube {
    CmnAsteroid::CAsteroidCube* Cube;              // +0x00
    unsigned int SystemId;                         // +0x04 (FUN_06d04ab0 reads it for SysMap)
    unsigned long CubeId;                          // +0x08
    int CubeSize;                                  // +0x0c
    // +0x10..+0x20: asteroid_marker::AsteroidId -> the server object (node+0x10).
    // Size at +0x20 is what FUN_06d04480's prune pass tests (node+0x30).
    std::map<unsigned long, GameObject*> Objects;

    FSrvAsteroidCube()
        : Cube(nullptr)
        , SystemId(0)
        , CubeId(0)
        , CubeSize(0xfa) {
    }

    // FUN_06d048f0 (on the value) and FUN_06d046c0 (on the pair, value at +4):
    // every object's vtable +0x15c -- GameObject::destroy_notify -- in map
    // order (.text:06d04579 in the inlined copy), then the tree is freed.
    ~FSrvAsteroidCube() {
        for (std::map<unsigned long, GameObject*>::iterator It = Objects.begin(); It != Objects.end(); ++It)
            It->second->destroy_notify();
    }

    void apply_marker(CmnAsteroid::asteroid_marker const* Marker);
};

// FUN_06d04ab0 (.text:06d04ab0, thiscall, RET 4). Only caller FUN_06d04480
// (.text:06d045ed).
//   .text:06d04acb  find Marker->AsteroidId (+0x04) in Objects
//   .text:06d04adc  bCubeActive (+0x0c) == 0:
//     .text:06d04b77  found -> object vtable +0x15c, erase the node (FUN_06d58e50)
//   .text:06d04ae4  active and found -> object vtable[0] (.text:06d04b69),
//                   IObjInspectImpl::get_position through the import thunk
//                   .text:06d5fef4; the result is not used
//   active and missing:
//     .text:06d04af4  SysDB::SysMap.find(SystemId) (+0x04), no end check
//     .text:06d04b09  bMine (+0x0d) ? FUN_06d09c80 (StarSystem: new MedMine,
//                     push onto the +0xc0 block, find(ObjId)) followed by
//                     FUN_06d04d60(mine, archetype +0x5c, SystemId)
//                   : FUN_06d09bb0 (new MedAsteroid, likewise) with ObjId (+0x08)
//       .text:06d04b16  the mine's radius: vt +0x150 (cobject(), no class
//                       narrowing) -> CObject::get_archetype -> FLD [+0x5c]
//       .text:06d04b27  MOV ECX,[EDI] -- SystemId (+0x04) is the second argument
//     .text:06d04b55  Objects.insert({AsteroidId, object})
void FSrvAsteroidCube::apply_marker(CmnAsteroid::asteroid_marker const* Marker) {
    std::map<unsigned long, GameObject*>::iterator It = Objects.find(Marker->AsteroidId);
    if (!Marker->bCubeActive) {
        if (It != Objects.end()) {
            It->second->destroy_notify();
            Objects.erase(It);
        }
        return;
    }
    if (It == Objects.end()) {
        StarSystem& System = SysDB::SysMap.find(SystemId)->second;
        GameObject* Obj = nullptr;
        if (Marker->bMine) {
            MedMine* const Mine = System.create_med_mine(Marker->ObjId);
            Archetype::Asteroid const* const Arch =
                static_cast<Archetype::Asteroid const*>(Mine->cobject()->get_archetype());
            Mine->arm(Arch->DetectRadius, SystemId);
            Obj = Mine;
        } else {
            Obj = System.create_med_asteroid(Marker->ObjId);
        }
        Objects.insert(std::map<unsigned long, GameObject*>::value_type(Marker->AsteroidId, Obj));
        return;
    }
    It->second->get_position();
}

// ---------------------------------------------------------------------------
// A field entry: operator_new(0x20). Constructed in place at .text:06d03a74 ..
// 06d03a7e (load), in map_asteroid_fields and inside FUN_06d03500: map
// initialised, +0x00 = 0, +0x1c = 0, +0x18 = 0xfa.
// ---------------------------------------------------------------------------
struct FSrvAsteroidField {
    CmnAsteroid::CAsteroidField* Field;                   // +0x00
    std::map<unsigned long, FSrvAsteroidCube> Cubes;      // +0x04..+0x14, keyed by CubeId
    int CubeSize;                                         // +0x18
    unsigned int SystemId;                                // +0x1c

    FSrvAsteroidField()
        : Field(nullptr)
        , CubeSize(0xfa)
        , SystemId(0) {
    }

    // FUN_06d03fc0, reached through the scalar deleting destructor
    // FUN_06d038a0: CAsteroidField::Release on +0x00 when set, then the cube
    // map is torn down (each value's FUN_06d046c0) -- here the member's own
    // destructor, which runs after this body as in retail.
    ~FSrvAsteroidField() {
        if (Field != nullptr)
            Field->Release();
    }

    void set_field(CmnAsteroid::CAsteroidField* InField);
    void load(char const* Path);
    void update();
};

// FUN_06d04810 (.text:06d04810, thiscall, RET 4): store; when non-null AddRef
// and CubeSize = field+0x44. The previous field is not released.
void FSrvAsteroidField::set_field(CmnAsteroid::CAsteroidField* InField) {
    Field = InField;
    if (InField != nullptr) {
        InField->AddRef();
        CubeSize = Field->CubeSize;
    }
}

// FUN_06d04140 (.text:06d04140, thiscall, one argument: the [Asteroids]
// "file" value). Only caller SrvAsteroidSystem::load (.text:06d03b24).
//   .text:06d0416a/6e  FillDist = EmptyFreq = 0 (both ints)
//   .text:06d04197     GetFileSysPath(path, Path, DALib::Data); open(path, false)
//   .text:06d041c7     get_state -- the start of the file, for the second pass
//   every [Field]: cube_size -> this+0x18, fill_dist, empty_cube_frequency,
//                  each ftol(get_value_float(0))
//   .text:06d04294     when Field is set: set_state, CAsteroidField::load(reader,
//                      CubeSize), then field+0x48 = FillDist and
//                      field+0x4c = (float)EmptyFreq (FILD .text:06d042bb)
void FSrvAsteroidField::load(char const* Path) {
    int FillDist = 0;
    int EmptyFreq = 0;
    INI_Reader Reader;
    char FullPath[0x100];
    GetFileSysPath(FullPath, Path, DALib::Data);
    if (!Reader.open(FullPath, false))
        return;

    INI_Reader::State Start;
    Reader.get_state(Start);
    while (Reader.read_header()) {
        if (!Reader.is_header("Field"))
            continue;
        while (Reader.read_value()) {
            // .text:06d04224 / 06d04244 / 06d04265 -- MSVCRT ftol straight on
            // the unrounded ST0 (64-bit truncation, low dword stored).
            if (Reader.is_value("cube_size"))
                CubeSize = (int)(long long)Reader.get_value_float(0);
            else if (Reader.is_value("fill_dist"))
                FillDist = (int)(long long)Reader.get_value_float(0);
            else if (Reader.is_value("empty_cube_frequency"))
                EmptyFreq = (int)(long long)Reader.get_value_float(0);
        }
    }

    if (Field != nullptr) {
        Reader.set_state(Start);
        Field->load(Reader, CubeSize);
        Field->FillDist = FillDist;
        Field->EmptyFreq = static_cast<float>(EmptyFreq);
    }
}

// FUN_06d04480 (.text:06d04480, thiscall on the entry). Callers
// SrvAsteroidSystem::update (.text:06d03d40) and FUN_06d03480.
//   .text:06d044a3  markers = Field->get_changes(); for each (Next at +0x10):
//     .text:06d044c8  Cubes.find(marker->CubeId)
//     missing: insert {CubeId, FSrvAsteroidCube()} (FUN_06d05690), then
//       .text:06d045c3  cube = Field->find_cube(marker->CubeId); when non-null
//                       record = {cube, SystemId, cube->get_id(), CubeSize}
//     FSrvAsteroidCube::apply_marker(marker)            (.text:06d045ed)
//   .text:06d04602 .. 06d04699  erase every record whose Objects is empty
void FSrvAsteroidField::update() {
    CmnAsteroid::asteroid_marker* Marker = Field->get_changes();
    while (Marker != nullptr) {
        FSrvAsteroidCube* Record;
        std::map<unsigned long, FSrvAsteroidCube>::iterator It = Cubes.find(Marker->CubeId);
        if (It == Cubes.end()) {
            It = Cubes.insert(std::make_pair(Marker->CubeId, FSrvAsteroidCube())).first;
            Record = &It->second;
            CmnAsteroid::CAsteroidCube* Cube = Field->find_cube(Marker->CubeId);
            if (Cube != nullptr) {
                Record->Cube = Cube;
                Record->CubeId = Cube->get_id();
                Record->CubeSize = CubeSize;
                Record->SystemId = SystemId;
            }
        } else {
            Record = &It->second;
        }
        Record->apply_marker(Marker);
        Marker = Marker->Next;
    }

    std::map<unsigned long, FSrvAsteroidCube>::iterator It = Cubes.begin();
    while (It != Cubes.end()) {
        if (It->second.Objects.size() == 0)
            It = Cubes.erase(It);
        else
            ++It;
    }
}

// Retail .text:06d03710 (ordinal 17): +0x00 CmnSystem = 0, +0x04/+0x08/+0x0c the
// empty field-entry list, +0x10 SystemId = 0, +0x14 RefCount = 0.
SrvAsteroidSystem::SrvAsteroidSystem()
    : m_CmnSystem(nullptr)
    , m_SystemId(0)
    , m_RefCount(0) {
}

// Retail .text:06ce30d0 (ordinal 16): a member-wise copy. CmnSystem without an
// AddRef, the list element by element (the entry pointers, shallow), SystemId
// and RefCount as they are.
SrvAsteroidSystem::SrvAsteroidSystem(const SrvAsteroidSystem& Other)
    : m_CmnSystem(Other.m_CmnSystem)
    , m_Fields(Other.m_Fields)
    , m_SystemId(Other.m_SystemId)
    , m_RefCount(Other.m_RefCount) {
}

// Retail .text:06d03740 (ordinal 22): CAsteroidSystem::Release when set; every
// non-null field entry through its destructor (FUN_06d03fc0, inlined) and
// freed; then the list nodes and the head.
SrvAsteroidSystem::~SrvAsteroidSystem() {
    if (m_CmnSystem != nullptr)
        m_CmnSystem->Release();
    for (std::list<FSrvAsteroidField*>::iterator It = m_Fields.begin(); It != m_Fields.end(); ++It) {
        if (*It != nullptr)
            delete *It;
    }
    m_Fields.clear();
}

// Retail .text:06ce3170 (ordinal 32): CmnSystem copied without AddRef (before
// the self check, which guards only the list assign), the list assigned, then
// SystemId and RefCount copied.
SrvAsteroidSystem& SrvAsteroidSystem::operator=(const SrvAsteroidSystem& Other) {
    m_CmnSystem = Other.m_CmnSystem;
    m_Fields = Other.m_Fields;
    m_SystemId = Other.m_SystemId;
    m_RefCount = Other.m_RefCount;
    return *this;
}

// Retail .text:06d038c0 (ordinal 498).
//   .text:06d03917  GetFileSysPath(path, Path, DALib::Data); open(path, false)
//   every [Asteroids]: "file" -> char[0x100], "zone" -> char[0x40]; a null
//     value string zeroes the length and the first byte (.text:06d039bb /
//     .text:06d039f7), otherwise FUN_06d03cf0
//   both lengths non-zero (.text:06d03a2a / 06d03a34):
//     .text:06d03a42  field = CmnAsteroid::CreateAsteroidField(SystemId)
//     .text:06d03a4c  new entry, push_back (FUN_06d05d90); entry->Field = field;
//                     non-null -> AddRef, CubeSize = field+0x44; SystemId
//     .text:06d03ad8  zone = Universe::get_zone(SystemId, zone name); null ->
//                     FDUMP (SrvAsteroid.cpp line 0xa1)
//     .text:06d03b11  zone vtable +0x18, result unused
//     .text:06d03b17  CAsteroidField::set_zone(field, zone)
//     .text:06d03b24  FUN_06d04140(entry, file)
void SrvAsteroidSystem::load(const char* Path) {
    INI_Reader Reader;
    char FullPath[0x100];
    GetFileSysPath(FullPath, Path, DALib::Data);
    if (!Reader.open(FullPath, false))
        return;

    while (Reader.read_header()) {
        if (!Reader.is_header("Asteroids"))
            continue;

        unsigned int FileLen = 0;
        char FileName[0x100];
        FileName[0] = '\0';
        unsigned int ZoneLen = 0;
        char ZoneName[0x40];
        ZoneName[0] = '\0';

        while (Reader.read_value()) {
            if (Reader.is_value("file")) {
                char const* Value = Reader.get_value_string();
                if (Value == nullptr) {
                    FileLen = 0;
                    FileName[0] = '\0';
                } else {
                    FileLen = SrvAsteroid_CopyBounded(FileName, 0x100, Value);
                }
            } else if (Reader.is_value("zone")) {
                char const* Value = Reader.get_value_string();
                if (Value == nullptr) {
                    ZoneLen = 0;
                    ZoneName[0] = '\0';
                } else {
                    ZoneLen = SrvAsteroid_CopyBounded(ZoneName, 0x40, Value);
                }
            }
        }

        if (FileLen == 0 || ZoneLen == 0)
            continue;

        CmnAsteroid::CAsteroidField* Field = CmnAsteroid::CreateAsteroidField(m_SystemId);
        m_Fields.push_back(new FSrvAsteroidField());
        FSrvAsteroidField* Entry = m_Fields.back();
        Entry->Field = Field;
        if (Field != nullptr) {
            Field->AddRef();
            Entry->CubeSize = Entry->Field->CubeSize;
        }
        Entry->SystemId = m_SystemId;

        Universe::IZone const* Zone = Universe::get_zone(m_SystemId, ZoneName);
        if (Zone == nullptr)
            FDUMP(0x100001, "%s(%d) : *** ERROR: AsteroidField linked to missing Zone (%s)",
                  SrvAsteroidSource, 0xa1, ZoneName);
        // .text:06d03b11: zone vtable +0x18 -- the shape id (common_universe.h,
        // EZoneShape). Retail discards the result; a null zone faults here.
        static_cast<void>(Zone->Shape);
        Field->set_zone(Zone);
        Entry->load(FileName);
    }
}

// Retail .text:06d03d40 (ordinal 520): FUN_06d04480 on every field entry.
void SrvAsteroidSystem::update() {
    for (std::list<FSrvAsteroidField*>::iterator It = m_Fields.begin(); It != m_Fields.end(); ++It)
        (*It)->update();
}

// Retail .text:06d03d70 (ordinal 514).
void SrvAsteroidSystem::set_sys_id(unsigned int SystemId) {
    m_SystemId = SystemId;
}

// Retail .text:06d03d80 (ordinal 511): store the pointer; null -> SystemId = 0;
// otherwise CAsteroidSystem::AddRef(Sys) and SystemId = Sys->SystemId (+0x04).
// The previous pointer is NOT released.
void SrvAsteroidSystem::set_cmn_system(CmnAsteroid::CAsteroidSystem* Sys) {
    m_CmnSystem = Sys;
    if (Sys == nullptr) {
        m_SystemId = 0;
        return;
    }
    Sys->AddRef();
    m_SystemId = m_CmnSystem->get_sys_id();
}

// Retail .text:06d03db0 (ordinal 504). No null check on CmnSystem. For every
// CAsteroidField from FindFirst/FindNext: AddRef, a new entry pushed back,
// the entry's FUN_06d04810 inlined (Field, AddRef, CubeSize = field+0x44),
// entry SystemId = this SystemId, Release.
void SrvAsteroidSystem::map_asteroid_fields() {
    CmnAsteroid::CAsteroidField* Field = m_CmnSystem->FindFirst();
    while (Field != nullptr) {
        Field->AddRef();
        m_Fields.push_back(new FSrvAsteroidField());
        FSrvAsteroidField* Entry = m_Fields.back();
        Entry->set_field(Field);
        Entry->SystemId = m_SystemId;
        Field->Release();
        Field = m_CmnSystem->FindNext();
    }
}

// Retail .text:06d03ed0 (ordinal 54).
int SrvAsteroidSystem::AddRef() {
    return ++m_RefCount;
}

// Retail .text:06d03ee0 (ordinal 358).
int SrvAsteroidSystem::Release() {
    return --m_RefCount;
}

// FUN_06d032b0 (.text:06d032b0).
//   .text:06d032d7  find(SystemId); found -> ++RefCount (node+0x24), return 2
//   .text:06d0336d  insert {SystemId, SrvAsteroidSystem()} (FUN_06d05de0); the
//                   temporary is destroyed (.text:06d0337a ..)
//   .text:06d033dd  ++RefCount; .text:06d033e6 SystemId (node+0x20) = SystemId
//   .text:06d033e9  load(Path); return 1
int SrvAsteroid_LoadSystem(unsigned int SystemId, const char* Path) {
    FSrvAsteroidSystemMap::iterator It = GSrvAsteroidSystems.find(SystemId);
    if (It != GSrvAsteroidSystems.end()) {
        ++It->second.m_RefCount;
        return 2;
    }
    It = GSrvAsteroidSystems.insert(std::make_pair(SystemId, SrvAsteroidSystem())).first;
    ++It->second.m_RefCount;
    It->second.m_SystemId = SystemId;
    It->second.load(Path);
    return 1;
}

// FUN_06d03410 (.text:06d03410): lower_bound + key check (= find); missing ->
// 0; --RefCount (node+0x24) non-zero -> 2; otherwise erase (FUN_06ce50d0,
// which runs ~SrvAsteroidSystem) -> 1.
int SrvAsteroid_UnloadSystem(unsigned int SystemId) {
    FSrvAsteroidSystemMap::iterator It = GSrvAsteroidSystems.find(SystemId);
    if (It == GSrvAsteroidSystems.end())
        return 0;
    if (--It->second.m_RefCount != 0)
        return 2;
    GSrvAsteroidSystems.erase(It);
    return 1;
}

// FUN_06d03480 (.text:06d03480, cdecl). The argument is the StarSystem as an
// IObjDB (.text:06d0c13c pushes EBX, the same `this` CmnAsteroid::Update got):
//   .text:06d0348b  Db->GetZoneDb() (vtable +0x08) -> the Universe::ISystem
//   .text:06d03492  its vtable[0] -> the system id (this port's flat ISystem
//                   keeps it in NicknameId, common_universe.h)
//   .text:06d034a2  find; found -> FUN_06d04480 on every field entry (node+0x18
//                   is SrvAsteroidSystem::m_Fields' head)
void SrvAsteroid_UpdateSystem(IObjDB* Db) {
    Universe::ISystem const* Info = Db->GetZoneDb();
    unsigned int SystemId = Info->NicknameId;
    FSrvAsteroidSystemMap::iterator It = GSrvAsteroidSystems.find(SystemId);
    if (It == GSrvAsteroidSystems.end())
        return;
    for (std::list<FSrvAsteroidField*>::iterator Field = It->second.m_Fields.begin();
         Field != It->second.m_Fields.end(); ++Field)
        (*Field)->update();
}

// FUN_06d03500 (.text:06d03500, cdecl). Single player.
//   .text:06d03520  Cmn = CmnAsteroid::Find(SystemId); null -> return
//   .text:06d0353b  Cmn->AddRef()
//   .text:06d035a6  insert {SystemId, SrvAsteroidSystem()} -- no find first: an
//                   existing entry is kept and reused
//   then, inline, ++RefCount; set_cmn_system(Cmn) without its null arm
//   (CmnSystem, AddRef, SystemId = Cmn+4); map_asteroid_fields (the entry
//   built with FUN_06d05230 and filled by FUN_06d04810)
//   Cmn->Release()
void SrvAsteroid_MapSystem(unsigned int SystemId) {
    CmnAsteroid::CAsteroidSystem* Cmn = CmnAsteroid::Find(SystemId);
    if (Cmn == nullptr)
        return;
    Cmn->AddRef();
    FSrvAsteroidSystemMap::iterator It =
        GSrvAsteroidSystems.insert(std::make_pair(SystemId, SrvAsteroidSystem())).first;
    SrvAsteroidSystem& System = It->second;
    ++System.m_RefCount;
    System.set_cmn_system(Cmn);
    System.map_asteroid_fields();
    Cmn->Release();
}

} // namespace SrvAsteroid

// ===========================================================================
// OwnerList<pub::SpaceObj::CargoDesc> -- the template's members are defined in
// src/include/common_server.h. This explicit instantiation definition is what makes
// Server.dll emit and export the six members retail's Server.dll exports: ??0 (ord
// 2, .text:06ce3040), copy ??0 (ord 1, .text:06ce3060), ??1 (ord 18, .text:06ce5810),
// ??4 (ord 23, .text:06ce10d0), ??_7 (ord 34, .rdata:06d64f24) and free (ord 483,
// .text:06ce57a0). Common.dll and freelancer.exe carry the same instantiation.
// ===========================================================================
// Member-level export (common_server.h): ??_7 is the one retail export this cannot emit.
OWNERLIST_CARGODESC_EXPORTS

// ===========================================================================
// MD5Hash, IClient, IServerImpl, CDeadReckonedVector, pub::AI::ContentCallback:
// their Server.dll exports (copy ctors, operator=, inline members, ??_7 tables,
// the ten ContentCallback virtuals) come from the declarations themselves --
// MD5HASH_API (md5hash.h), RPC_ICLIENT_API (rpc_iclient.h), SERVER_API on
// IServerImpl, SHARED_INLINE_API (common_cobject.h / common_pub.h) -- the way
// retail's headers produced them. No linker /EXPORT aliases.
// ===========================================================================

// ===========================================================================
// Missing global exports — retail Server.dll data section
// ===========================================================================

// Players (global PlayerDB) — `?Players@@3VPlayerDB@@A`, ordinal 352,
// .data:06d90260 (RVA 0xB0260). Retail's one and only player database: its
// first member is the heap slot-array base every walker loads with
// `MOV EDX,dword ptr [.data:Players]`, and its address is what every method
// call passes in ECX (`MOV ECX,.data:Players`). Since 2026-09-17 this object
// owns the port's only player table as well — Common.dll's g_PlayersDB[64] is
// gone. Static-init order note: the ctor only nulls pointers and default-
// constructs its containers, and nothing touches the table before
// IServerImpl::Startup calls Players.init().
SERVER_API PlayerDB Players;

// SysDB::SysMap (global std::map) — RVA 0xADA2C, export ordinal 460. Declared in
// server_sysdb.h as an ordinary std::map; this is THE system table --
// IServerImpl.cpp's SysDB_* helpers used to keep their own g_SysDBMap next to an
// exported map nobody filled.
namespace SysDB {
    SysMapType SysMap;
}

// Chat hook function pointers — RVAs 0xAF604, 0xAF608, 0xAF60C
// Retail type: void (__cdecl*)(unsigned short*, int) -- VC6's wchar_t: the text is the
// chat message BinaryRDLReader::extract_text_from_buffer just wrote (Server.dll.c
// FLChatServer path, DAT_06d8e9b0).
typedef void (__cdecl *ChatHookFn)(wchar_t*, int);
SERVER_API ChatHookFn g_pUniverseChatHook = nullptr;  // RVA 0xAF604
SERVER_API ChatHookFn g_pPrivateChatHook = nullptr;   // RVA 0xAF608
SERVER_API ChatHookFn g_pSystemChatHook = nullptr;    // RVA 0xAF60C

// The `Server` export (ordinal 524, RVA 0xAFACC) is the IServerImpl object
// itself, not a string -- it is defined next to IServerImpl in IServerImpl.cpp.

// ===========================================================================
// StarSystem::count_players — retail RVA 0x2BCB0, ordinal 476
// Iterates the player list at this+0x0C (doubly-linked list with sentinel).
// Counts entries where node[0x10] (dword at node+0x40) != ExcludeId.
// Retail: for (node = **(this+0xC); node != *(this+0xC); node = *node)
//           if (node[0x10] != param_1) count++;
// ===========================================================================
unsigned int StarSystem::count_players(unsigned int ExcludeId) const
{
    unsigned int Count = 0;
    // PlayerListHead at this+0x0C is a pointer to the list sentinel
    unsigned int const* Sentinel = reinterpret_cast<unsigned int const*>(
        *reinterpret_cast<void* const*>(
            reinterpret_cast<unsigned char const*>(this) + 0x0C));
    // No null test: .text:06d0bcb0 `MOV EDX,[ECX+0xc]` / .text:06d0bcb3
    // `MOV ECX,[EDX]` dereferences the sentinel at once.

    // First node: dereference the sentinel to get the head pointer
    // Retail: puVar1 = **(this + 0xc)
    unsigned int const* Node = reinterpret_cast<unsigned int const*>(*Sentinel);
    while (Node != Sentinel) {
        // node[0x10] = player ID at offset 0x40 bytes (0x10 dwords)
        if (Node[0x10] != ExcludeId) {
            Count++;
        }
        // Advance to next node: node = *node (first dword = next pointer)
        Node = reinterpret_cast<unsigned int const*>(*Node);
    }
    return Count;
}

// ===========================================================================
// StarSystem::remove_player — retail FUN_06d08740(this, SessionId), 0x06d08740.
// The first half of both system-exit entry points (FUN_06d087f0 and the one
// PlayerDB::cleanup/logout reach, FUN_06d08890).
// ===========================================================================
void StarSystem::remove_player(unsigned int SessionId)
{
    // (a) drop this session's node out of the player list at this+0x0C.
    //     Retail unlinks with `*(int*)node[1] = node[0]` (prev->next = next) and
    //     `*(int*)(node[0] + 4) = node[1]` (next->prev = prev), then calls the
    //     node's own disposer through node[2], frees the node, decrements the
    //     size at this+0x10, and BREAKS -- one match only.
    //
    //     Retail reads the head and dereferences it at once, with no null test,
    //     and the loop's only exit test is `node != head`:
    //       .text:06d08748  8b450c  MOV  EAX,[EBP + 0xc]    ; head
    //       .text:06d0874c  8b30    MOV  ESI,[EAX]          ; first = head->next
    //       .text:06d0874e  3bf0    CMP  ESI,EAX / JZ 06d08788
    //       .text:06d08753  395e40  CMP  [ESI + 0x40],EBX   ; View.PlayerId == sid?
    //       .text:06d08758  8b36    MOV  ESI,[ESI] / CMP ESI,EAX / JNZ 06d08753
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);
    // KNOWN CRASH (intentional, retail-faithful): if this faults on a garbage
    // Sentinel and the call came from PlayerDB_LeaveSystem, `this` is SysMap's
    // unconstructed head node -- a SysMap miss (bad universe.ini system id).
    // Retail .text:06d08748 / .text:06d0874c. See PlayerDB_LeaveSystem.
    for (SystemPlayerNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        if (Node->View.PlayerId == SessionId) {
            // .text:06d08760 .. .text:06d0876f
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            // Retail disposes the element through its own vtable slot 0.
            // Ghidra renders this `(**(code **)piVar1[2])(0)`, which reads
            // as "load node[2] and call through it"; the raw bytes say
            // otherwise -- it is a LEA, so the element is embedded IN the
            // node at +0x08, not pointed to from it:
            //   .text:06d0876c  8d4e08  LEA   ECX,[ESI + 0x8]   ; &node->Element
            //   .text:06d08772  8b11    MOV   EDX,[ECX]         ; its vtable
            //   .text:06d08774  6a00    PUSH  0x0               ; "do not free"
            //   .text:06d08776  ff12    CALL  dword ptr [EDX]   ; slot 0
            // (The +0x38 list below is the opposite shape: 06d0879f does
            // `MOV ECX,[ESI + 0x8]`, a real pointer load.)
            //
            // `PUSH 0` is the deleting flag: destroy in place, do NOT free,
            // because the node is freed separately on the next line. An
            // explicit destructor call through a pointer is exactly that
            // dispatch: vptr slot 0 (the scalar deleting destructor) with 0.
            Observer* const Element = &Node->View;                  // .text:06d0876c
            Element->~Observer();                                  // .text:06d08772 .. 06d08776
            ::operator delete(Node);                               // .text:06d08779
            --PlayerListSize;                                      // .text:06d0877e .. 06d08785
            break;
        }
    }

    // (b) the dispatch pass over the Ships list (head at this+0x38).
    //     +0x20 is IObjInspect::get_id, +0x158 GameObject::destroy and +0x160
    //     GameObject::is_destroyed (server_gameobject.h). Retail:
    //       .text:06d08788  XOR  ESI,ESI              ; node := null
    //       .text:06d08790  TEST ESI,ESI / JNZ        ; first pass: head,
    //       .text:06d08794  MOV  ESI,[EBP + 0x38]     ;   else node->next
    //       .text:06d08799  MOV  ESI,[ESI]            ;   (read AFTER destroy)
    //       .text:06d0879b  TEST ESI,ESI / JZ exit    ; NULL-terminated
    //       .text:06d0879f  MOV  ECX,[ESI + 0x8]      ; node->Data, a POINTER
    //       .text:06d087a2  MOV  EAX,[ECX + 0x10]     ; CBase::Object
    //       .text:06d087a5  TEST EAX,EAX / JZ 06d087b4
    //       .text:06d087a9  MOV  EDX,[EAX + 0x4c] / AND EDX,3 / CMP DL,3 / JZ 06d087b6
    //       .text:06d087b4  XOR  EAX,EAX              ; cobj() == null ...
    //       .text:06d087b6  CMP  [EAX + 0xb4],EBX     ; ... read through unguarded
    //       .text:06d087be  CALL [EAX + 0x160] / TEST AL,AL / JNZ next
    //       .text:06d087cf  CALL [EDI + 0x20]         ; get_id()
    //       .text:06d087d8  PUSH EAX / PUSH 0 / CALL [EDI + 0x158]
    //     The null path at .text:06d087b4 has no test before the +0xb4 read, so
    //     retail faults on a non-CSimple object: every object on this list is a
    //     CSimple there, and the port does the same unguarded read. node->Data
    //     is the wrapper itself (2026-09-24), so there is no registry miss to
    //     skip any more either.
    FSystemListNode* Node = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->csimple()->OwnerPlayer != SessionId)
            continue;
        if (Obj->is_destroyed())
            continue;
        Obj->destroy(0, Obj->get_id());
    }
}

// ---------------------------------------------------------------------------
// StarSystem::player_switched_out -- retail FUN_06d087f0 (.text:06d087f0 ..
// 06d08888, RET 8). See server_sysdb.h. Stack base T = ESP after the three
// register pushes; the 0x3a payload is [T+0xc] {system id, DestSystemId,
// SessionId}.
// ---------------------------------------------------------------------------
void StarSystem::player_switched_out(unsigned int SessionId, unsigned int DestSystemId)
{
    remove_player(SessionId);                                       // .text:06d087fd FUN_06d08740

    // .text:06d08802 .. 06d08817 -- `Players + id*0x418 - 0x418`, the raw slot.
    PlayerData_FlushPendingVisits(Players[SessionId]);              // FUN_06d4b450

    if (DestSystemId != 0u)                                         // .text:06d0881c .. 06d08822
    {
        unsigned int Payload[3];
        Payload[0] = SystemInfo->NicknameId;                        // .text:06d08824 .. 06d0882f ISystem slot 0
        Payload[1] = DestSystemId;                                  // .text:06d0883a
        Payload[2] = SessionId;                                     // .text:06d08827
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);                // .text:06d0883e
    }

    // .text:06d08846 .. 06d0887d -- the single-player tail.
    if (SinglePlayer() && bLoaded != 0)
    {
        bLoaded = 0;                                                // .text:06d0885d
        unsigned int SystemNickname = SystemInfo->NicknameId;       // .text:06d0885a .. 06d08868
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemUnloaded, &SystemNickname);        // .text:06d08873
        unload();                                                   // .text:06d0887d FUN_06d072c0
    }
}

// FUN_06d0d8a0 (.text:06d0d8a0, RET 0xc). See server_sysdb.h for the map.
bool StarSystem::activate_object(GameObject* Obj, bool bOn, unsigned int Index)
{
    // .text:06d0d8ab .. 06d0d8c8 -- __RTDynamicCast(Obj, GameObject -> EqObj);
    // a failed cast returns BL, zeroed at .text:06d0d8bc.
    EqObj* const Eq = dynamic_cast<EqObj*>(Obj);
    if (Eq == nullptr)
        return false;

    // .text:06d0d8ce .. 06d0d8da -- FUN_06ce7850 = EqObj::activate(bOn, Index):
    // the (& 0x103) narrowing of the raw object (not null-tested) tail-jumping
    // into CEqObj::activate (vt +0xb4).
    ObjActivateResult const Result = Eq->activate(bOn, Index);

    // .text:06d0d8df .. 06d0d8ec -- 0 returns false; anything but 1 returns CL = 1.
    if (Result == ObjActivateFailed)
        return false;
    if (Result != ObjActivateChanged)
        return true;

    // .text:06d0d8f2 .. 06d0d91a -- cobject() (vt +0x150) under the (& 0x303)
    // test: a solar is announced to every player, anything else only where seen.
    CObject const* Solar = Obj->cobject();
    if (Solar != nullptr &&
        (static_cast<unsigned int>(Solar->ObjectClass) & 0x303u) != 0x303u)
        Solar = nullptr;

    // .text:06d0d91d .. 06d0d92f -- {get_id() (vt +0x20), BL, Index}; the pad
    // bytes are never written.
    FPayloadServerActivateObject Rec;
    Rec.ObjectId = Obj->get_id();
    Rec.bActivate = static_cast<unsigned char>(bOn);
    Rec.Field_0x08 = Index;

    // .text:06d0d928 .. 06d0d971 -- every player node; FUN_06cf73d0 on node+8
    // only when there is no solar, IClient +0xe0 (SendServerActivateObject) with node+0x40.
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        if (Solar == nullptr && !SystemPlayerView_IsObjectVisible(&Node->View, Obj))
            continue;
        ::g_pClientInterface->SendServerActivateObject(Node->View.PlayerId, Rec);
    }

    // .text:06d0d976 -- AL = 1.
    return true;
}

// FUN_06d0d9b0 (.text:06d0d9b0, RET 0x10). See server_sysdb.h for the map.
void StarSystem::spawn_mined_loot(Vector const& Pos, unsigned int ContainerArchId,
                                  unsigned int ContentsArchId, int Count)
{
    // .text:06d0d9bb .. 06d0d9db -- the record's +0x04, +0x08, +0x10 .. +0x1c are
    // zeroed first, byte +0x68 = 1, byte +0x69 = 0; +0x6a/+0x6b are never written.
    FLootRecord Rec;
    Rec.Arch = nullptr;
    Rec.HitPts = 0.0f;
    Rec.ContentsHitPts = 0.0f;
    Rec.Count = 0;
    Rec.Field_0x18 = 0u;
    Rec.Field_0x1c = 0u;
    Rec.Field_0x68 = 1;
    Rec.bMission = 0;

    // .text:06d0d9e2 .. 06d0da05 -- CFLIDMaker::AllocateObjectID inlined, taken
    // before any archetype is looked up (and so spent even on the early outs).
    Rec.Id = CFLIDMaker::GetInstance()->AllocateObjectID();

    // .text:06d0da04 .. 06d0da16 -- the contents archetype; none -> return.
    Archetype::Equipment* const Contents = Archetype::GetEquipment(ContentsArchId);
    if (Contents == nullptr)
        return;
    Rec.ContentsArch = Contents;                                    // .text:06d0da12  +0x0c
    Rec.ContentsHitPts = Contents->HitPts;                          // .text:06d0da25 / 06d0da2f  +0x10 (Root +0x1c)
    Rec.Count = Count;                                              // .text:06d0da28 / 06d0da33  +0x14

    // .text:06d0da1c .. 06d0da54 -- the container: GetEquipment(ContainerArchId)
    // when the id is non-zero; zero or unknown falls back to the contents'
    // get_loot_appearance() (vt +0x1c); null there -> return.
    Archetype::Equipment* Container = nullptr;
    if (ContainerArchId != 0u)
        Container = Archetype::GetEquipment(ContainerArchId);
    if (Container == nullptr)
    {
        Container = Contents->get_loot_appearance();
        if (Container == nullptr)
            return;
    }
    Rec.Arch = Container;                                           // .text:06d0da41 / 06d0da50  +0x04
    Rec.HitPts = Container->HitPts;                                 // .text:06d0da5a / 06d0da6c  +0x08

    // .text:06d0da5d .. 06d0da88 -- +0x2c the position, copied as three dwords.
    Rec.Position = Pos;

    // .text:06d0da8c .. 06d0dacc -- +0x38 the identity orientation.
    Rec.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                             0.0f, 1.0f, 0.0f,
                             0.0f, 0.0f, 1.0f);

    // .text:06d0da7e .. 06d0daf0 -- +0x20 random_inside_sphere(20.0f).
    Rec.Velocity = random_inside_sphere(20.0f);

    // .text:06d0dae6 .. 06d0db18 -- +0x5c random_inside_sphere(3.0f).
    Rec.AngularVelocity = random_inside_sphere(3.0f);

    // .text:06d0db05 .. 06d0db1f -- create_loot(0, 3, &Rec, 1) (FUN_06d08a40).
    create_loot(0u, 3, &Rec, 1);
}

// FUN_06d0e7a0 (.text:06d0e7a0, RET 8). See server_sysdb.h for the map.
void StarSystem::send_tractor_objects(unsigned int const& SkipPlayerId, XTractorObjects const& Info)
{
    // .text:06d0e7b8 .. 06d0e7c8 / 06d0e8a1 .. 06d0e8ae -- every player node,
    // the sentinel re-read from +0xc on each step.
    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next)
    {
        // .text:06d0e7d7 .. 06d0e7e0 -- the player named by *SkipPlayerId is skipped.
        if (Node->View.PlayerId == SkipPlayerId)
            continue;

        // .text:06d0e7e6 .. 06d0e7f3 -- FUN_06cf7370 on the tractoring object's id.
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, Info.ObjectId))
            continue;

        // .text:06d0e7f9 .. 06d0e820 -- the local XTractorObjects: an empty vector
        // owned id vector, ObjectId, TractorEquipId. The pad word +0x06 is never written.
        XTractorObjects Msg;
        Msg.ObjectId = Info.ObjectId;
        Msg.TractorEquipId = Info.TractorEquipId;

        // .text:06d0e80d .. 06d0e868 -- n = Begin ? (End - Begin) >> 2 : 0, a signed
        // loop; each id the player sees goes through FUN_06cecbd0 (_Insert_n at
        // end, 1) -- a push_back into the message's own vector.
        int const Count = static_cast<int>(Info.TargetIds.size());
        for (int Index = 0; Index < Count; ++Index)
        {
            if (SystemPlayerView_IsObjectIdVisible(&Node->View, Info.TargetIds[Index]))
                Msg.TargetIds.push_back(Info.TargetIds[Index]);
        }

        // .text:06d0e86c .. 06d0e87d -- IClient +0x14 (SendCommonTractorObjects) with
        // node+0x40, sent even when no target id survived.
        ::g_pClientInterface->SendCommonTractorObjects(Node->View.PlayerId, Msg);

        // .text:06d0e880 .. 06d0e89d -- FUN_06d5ffb0 (operator delete) on the
        // block, begin/end/capacity zeroed: here the std::vector going out of scope.
    }
}

// FUN_06d0ed40 (.text:06d0ed40, RET 0x10). See server_sysdb.h for the map.
void StarSystem::tractor_loot(::Loot* LootObj, EqObj* Ship, unsigned int Space, unsigned short TractorId)
{
    // .text:06d0ed4d .. 06d0ed7c -- (ObjectClass & 0x803) on the loot's raw
    // object; a null wrapper gives null too. Kept at [S+0x10].
    CLoot* const Cl = (LootObj != nullptr) ? LootObj->cloot() : nullptr;

    // .text:06d0ed7e .. 06d0ed9d -- ContentsArch (+0xec), Units (+0xf0), the
    // contents' archetype id (Root +0x08), Take = min(Space, Units) unsigned (JBE).
    Archetype::Equipment const* const Contents = Cl->ContentsArch;
    unsigned int const Units = Cl->Units;
    unsigned int const ContentsArchId = Contents->get_id();
    unsigned int Take = Space;
    if (Take > Units)
        Take = Units;

    // .text:06d0eda0 -- the Space slot is reused for the health, 1.0f.
    float Health = 1.0f;

    // .text:06d0ed9f .. 06d0ee1c -- the carried health.
    if (!arch_is_combinable(ContentsArchId))
    {
        // .text:06d0edb5 .. 06d0edc4 -- FLD ContentsHitPts / FDIV Root::HitPts / FSTP.
        Health = Cl->ContentsHitPts / Contents->HitPts;
    }
    else if (Contents->get_class_type() == Archetype::AClassCommodity)   // .text:06d0edc6 .. 06d0edd4 vt +0x10
    {
        // .text:06d0edd6 .. 06d0eded -- get_decay_per_second() compared with 0.0f
        // (.rdata:06d65188), TEST AH,0x41 / JNZ: only a strictly positive decay.
        Archetype::Commodity const* const CommodityArch =
            static_cast<Archetype::Commodity const*>(Contents);
        if (CommodityArch->get_decay_per_second() > 0.0f && Take == Units)   // .text:06d0edef JNZ
        {
            // .text:06d0edf5 .. 06d0ee1c -- (ContentsHitPts - Loot::DecayAccum
            // (+0x68)) held on the x87 stack, replaced by 0.0f unless strictly
            // positive (FCOM / TEST AH,0x41 / JZ), divided by Root::HitPts and
            // stored as a float.
            double Remaining = static_cast<double>(Cl->ContentsHitPts) -
                               static_cast<double>(LootObj->DecayAccum);
            if (!(Remaining > 0.0))
                Remaining = 0.0;
            Health = static_cast<float>(Remaining / static_cast<double>(Contents->HitPts));
        }
    }

    // .text:06d0ee20 .. 06d0ee43 -- add_cargo (FUN_06d0e8d0) on the ship with
    // the CLoot's bTemporary (+0xfd) as the mission flag.
    unsigned short const SubObjId = add_cargo(Ship, ContentsArchId, Take, Health, Cl->bTemporary);

    // .text:06d0ee48 .. 06d0ee80 -- controller event 0x18 through FUN_06d43650,
    // the record at S+0x30. The word at +0x0a is never written.
    struct FControllerMsg18 {
        unsigned int LootId;          // +0x00  .text:06d0ee4f / 06d0ee56
        unsigned int ShipId;          // +0x04  .text:06d0ee5a / 06d0ee5d
        unsigned short SubObjId;      // +0x08  .text:06d0ee77, add_cargo's result
        unsigned short Pad0a;         // +0x0a
        unsigned int GoodId;          // +0x0c  .text:06d0ee66 / 06d0ee73
        unsigned int Count;           // +0x10  .text:06d0ee7c, Take
    } Msg18;
    Msg18.LootId = LootObj->get_id();
    Msg18.ShipId = Ship->get_id();
    Msg18.GoodId = Arch2Good(ContentsArchId);
    Msg18.SubObjId = SubObjId;
    Msg18.Count = Take;
    Server_ControllerDispatch(0, pub::Controller::EMessage::LootAcquired, &Msg18);

    // .text:06d0ee85 .. 06d0eecd -- ITEMTRACTORED at S+0x24: the inlined
    // constructor's {0, 0, SUBOBJ_ID_NONE, 0} is overwritten field by field
    // with {loot id, ship id, TractorId, (ushort)Take}.
    FPayloadServerItemTractored Tractored;
    Tractored.Field_0x00 = 0u;
    Tractored.Field_0x04 = 0u;
    Tractored.Field_0x08 = SUBOBJ_ID_NONE;
    Tractored.Field_0x0a = 0;
    Tractored.Field_0x00 = LootObj->get_id();
    Tractored.Field_0x04 = Ship->get_id();
    Tractored.Field_0x08 = TractorId;
    Tractored.Field_0x0a = static_cast<unsigned short>(Take);

    // .text:06d0eec5 .. 06d0ef09 -- every player node that sees the ship
    // (FUN_06cf7370): IClient +0x100 (SendServerItemTractored) with node+0x40.
    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next)
    {
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, Tractored.Field_0x04))
            continue;
        ::g_pClientInterface->SendServerItemTractored(Node->View.PlayerId, Tractored);
    }

    // .text:06d0ef0f .. 06d0efd6 -- part of the stack: the rest stays in space.
    if (Take != Units)
    {
        Cl->Units = Units - Take;
        return;
    }

    // .text:06d0ef1b .. 06d0ef41 -- the ship's CSimple (& 3, not null-tested)
    // OwnerPlayer (+0xb4): a player's ship in single player keeps its tractor
    // target.
    if (!(Ship->csimple()->OwnerPlayer != 0 && SinglePlayer()))
    {
        // .text:06d0ef43 .. 06d0ef90 -- the ship's CShip (& 0x503, not null-tested
        // before the EquipManager at +0xe4), FindByID(TractorId), CETractor::cast;
        // a tractor drops the loot's id.
        CObject* const Raw = Ship->Object;
        CShip* const ShipCobj =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CShip*>(Raw) : nullptr;
        CETractor* const Tractor = CETractor::cast(ShipCobj->EquipManager.FindByID(TractorId));
        if (Tractor != nullptr)
            Tractor->RemoveTarget(LootObj->get_id());
    }

    // .text:06d0ef96 .. 06d0efa5 -- destroy(2, ship id) through vt +0x158.
    unsigned int const ShipId = Ship->get_id();
    LootObj->destroy(2, ShipId);

    // .text:06d0efab .. 06d0efb2 -- Units = 0.
    Cl->Units = 0u;
}

// LAB_06d06a40 (.text:06d06a40, RET 8) -- SrvTractor's vtable slot 0
// (.rdata:06d65544).
void SrvTractor::notify(Event Code, void* Param)
{
    // .text:06d06a40 .. 06d06a67 -- only event 0x18 (TractorArm::Update's
    // consume timer ran out; Param is the TractorArm), and only with a target:
    // TargetWatch's watchable (+0x08) minus 0x9c, the CLoot (TractorArm::GetTarget).
    if (Code == Event_0x18)
    {
        CLoot* const Target = static_cast<TractorArm*>(Param)->GetTarget();
        if (Target != nullptr)
        {
            // .text:06d06a6d .. 06d06aa3 -- the owner's CShip: CBase (+0xc) then
            // its Object under (& 0x503); null otherwise, and not tested before
            // the call.
            CObject* const Raw = (Owner != nullptr) ? Owner->Object : nullptr;
            CShip* const ShipCobj =
                (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                    ? static_cast<CShip*>(Raw) : nullptr;

            // .text:06d06aa3 .. 06d06aae -- CShip::get_space_for_cargo_type(the
            // CLoot's ContentsArch, +0xec); no room -> nothing.
            int const Space = ShipCobj->get_space_for_cargo_type(Target->ContentsArch);
            if (Space != 0)
            {
                // .text:06d06ab0 .. 06d06acb -- the loot's server object by its
                // CSimple id (+0xb0) with mask 0x10000000 (FUN_06d0c8d0) in the
                // owner's system (+0x28).
                GameObject* const LootObj = Owner->OwnerSystem->find_object(Target->Id, 0x10000000u);
                // .text:06d06acd .. 06d06ad9 -- not while it is being destroyed (vt +0x160).
                if (LootObj != nullptr && !LootObj->is_destroyed())
                {
                    // .text:06d06adb .. 06d06aee -- FUN_06d0ed40(loot, owner,
                    // space, the equip's id (CEquip +0x08, zero-extended)). The
                    // mask above only matches Loot::get_type_bits.
                    Owner->OwnerSystem->tractor_loot(static_cast<::Loot*>(LootObj), Owner,
                                                     static_cast<unsigned int>(Space), Equip->Id);
                }
            }
        }
    }

    // .text:06d06af3 .. 06d06aff -- always the base body (FUN_06ced360).
    ServerEquip::notify(Code, Param);
}

// FUN_06d0efe0 (.text:06d0efe0, RET 0xc). See server_sysdb.h for the map.
void StarSystem::use_item(GameObject* Obj, unsigned short EquipId, int Count)
{
    // .text:06d0efeb .. 06d0f00d -- the (& 3) narrowing, not null-tested, for
    // OwnerPlayer (+0xb4); get_id() (vt +0x20) is taken before the test.
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();

    // .text:06d0f010 .. 06d0f057 -- a player's object whose slot is valid
    // ((id - 1) below DAT_06d90264, client_id +0x348 set) and whose ship
    // (+0x3e4) is this object: FUN_06d4f100(slot, EquipId, Count, 0).
    if (OwnerPlayer != 0u && Players.is_valid_ship_owner(OwnerPlayer, ObjId))
        PlayerData_RemoveEquipment(Players[OwnerPlayer], EquipId, Count, false);

    // .text:06d0f05c .. 06d0f073 -- USE_ITEM {get_id() again, EquipId, (ushort)Count}.
    FPayloadServerUseItem Rec;
    Rec.Field_0x00 = Obj->get_id();
    Rec.Field_0x04 = EquipId;
    Rec.Field_0x06 = static_cast<unsigned short>(Count);

    // .text:06d0f06b .. 06d0f0ad -- every player node that sees the object
    // (FUN_06cf7370): IClient +0x104 (SendServerUseItem) with node+0x40.
    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next)
    {
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, Rec.Field_0x00))
            continue;
        ::g_pClientInterface->SendServerUseItem(Node->View.PlayerId, Rec);
    }
}

// FUN_06d0bbc0 (.text:06d0bbc0, RET 8). See server_sysdb.h for the map.
void StarSystem::sp_obj_update(SSPObjUpdateInfo const& Info, unsigned int SessionId)
{
    // .text:06d0bbcc .. 06d0bc0d -- the ship block (+0x34, Head +0x38): `node =
    // node ? node->Next : Head`, a null node returns. Each node's wrapper (null
    // tested), its CBase::Object under (& 3), and that CSimple's Id (+0xb0) read
    // without a test on the narrowing.
    CSimple* Ship = nullptr;
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            return;
        Ship = (Node->Data != nullptr) ? Node->Data->csimple() : nullptr;
        if (Ship->Id == Info.ObjectId)
            break;
    }

    // .text:06d0bc0f .. 06d0bc21 -- PhySys::RCSet(ship, &Position, &Orientation,
    // the double timestamp).
    PhySys::RCSet(Ship, Info.Position, Info.Orientation, Info.Timestamp);

    // .text:06d0bc27 .. 06d0bc70 -- the UPDATEOBJECT argument at S+0xc: id,
    // orientation, position, the timestamp stored as a float (FLD double /
    // FSTP float), +0x24 a raw dword copy of Info +0x28, byte +0x28 from Info
    // +0x2c. The pad bytes are never written.
    FCommonUpdateObjectInfo Rec;
    Rec.ObjectId = Info.ObjectId;
    Rec.Orientation = Info.Orientation;
    Rec.Position = Info.Position;
    Rec.Field_0x20 = static_cast<float>(Info.Timestamp);
    std::memcpy(&Rec.Field_0x24, &Info.Field_0x28, sizeof(Rec.Field_0x24));
    Rec.Field_0x28 = Info.Field_0x2c;

    // .text:06d0bc69 .. 06d0bc99 -- every player node but the sender's
    // (node+0x40 == SessionId skipped): FUN_06cf65b0 on the view at node+8.
    for (SystemPlayerNode* Player = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Player != static_cast<SystemPlayerNode*>(PlayerListHead); Player = Player->Next)
    {
        if (Player->View.PlayerId == SessionId)
            continue;
        SystemPlayerView_SendObjectUpdate(&Player->View, &Rec);
    }
}

// FUN_06d0bcd0 (.text:06d0bcd0, RET 0x14, 0x201c-byte frame through the stack
// probe FUN_06d60160). See server_sysdb.h for the map.
unsigned int StarSystem::scan_objects(unsigned int* OutIds, unsigned int MaxCount, Vector const* Pos,
                                      float Radius, unsigned int TypeMask)
{
    unsigned int Count = 0;

    // .text:06d0bcf7 .. 06d0bd21 -- ships first: FUN_06cf5020 on the +0x34 block.
    if ((TypeMask & 0x7f0000u) != 0u)
        Count = Ships.collect_in_sphere(OutIds, MaxCount, Pos, Radius, TypeMask);

    // .text:06d0bd23 .. 06d0bd29 -- nothing else asked for: done.
    if ((TypeMask & 0xfdffu) == 0u)
        return Count;

    // .text:06d0bd2f .. 06d0bd41 -- FCOMP 0.0f / TEST AH,0x41 / JNZ: a radius
    // that is not strictly positive walks the solar block (+0x5c) instead of
    // the spatial bucket, into what is left of the buffer.
    if (!(Radius > 0.0f))
    {
        // .text:06d0be11 .. 06d0be31
        Count += Solars.collect_in_sphere(OutIds + Count, MaxCount - Count, Pos, Radius, TypeMask);
        return Count;
    }

    // .text:06d0bd47 .. 06d0bd55 -- GetBucket (IObjDB vt +0xc) with 0xfdff; none -> done.
    void* const Bucket = GetBucket(0xfdffu);
    if (Bucket == nullptr)
        return Count;

    // .text:06d0bd5b .. 06d0bd98 -- the sphere {*Pos, Radius} at S+0x14 and the
    // result block at S+0x24 {capacity 0x800, count 0, 0x800 slots}, handed to
    // the partition's vtable +0x1c (Fill, thiscall). Its return value is not
    // used; the count is read back from the block.
    struct FBucketQuery
    {
        Vector Pos;
        float Radius;
    } Sphere;
    Sphere.Pos = *Pos;
    Sphere.Radius = Radius;
    struct FBucketResult
    {
        unsigned int Capacity;
        unsigned int Count;
        IObjRW* Data[0x800];
    } Found;
    Found.Capacity = 0x800u;
    Found.Count = 0u;
    // ISpatialPartition +0x1c (sphere query); the stack sphere and result have the layouts of
    // Geometry::Sphere and FSpatialQueryResult.
    static_cast<ISpatialPartition*>(Bucket)->QuerySphere(reinterpret_cast<Geometry::Sphere const*>(&Sphere),
                                                         reinterpret_cast<FSpatialQueryResult*>(&Found));

    // .text:06d0bd9b .. 06d0be00 -- each hit while the buffer has room (the
    // unsigned Count < MaxCount test runs before each element): the element (the
    // inlined bounds-checked accessor gives null past the count), its
    // IObjInspect::get_type (vt +0x88) -- a non-zero result skips it -- then
    // type & TypeMask, and get_id (vt +0x20) into the buffer.
    for (unsigned int Index = 0; Index < Found.Count; ++Index)
    {
        if (Count >= MaxCount)
            return Count;
        IObjRW* const Hit = (Index < Found.Count) ? Found.Data[Index] : nullptr;
        unsigned int HitType;
        if (Hit->get_type(HitType) != 0)
            continue;
        if ((HitType & TypeMask) == 0u)
            continue;
        OutIds[Count] = Hit->get_id();
        ++Count;
    }
    return Count;
}

// ===========================================================================
// ObjectList::count_type -- FUN_06cf5130 (.text:06cf5130, `RET 0x4`) -- "how
// many objects in this dispatch block answer this type mask?".
//
// It is NOT a size read, and it is not an equality test either:
//   .text:06cf5144  8b7704    MOV  ESI,[EDI + 0x4]     ; the block's head
//   .text:06cf5149  8b36      MOV  ESI,[ESI]           ; ... and on from there
//   .text:06cf514f  8b4e08    MOV  ECX,[ESI + 0x8]     ; node->Data, a pointer
//   .text:06cf5154  ff9054010000  CALL [EAX + 0x154]   ; get_type_bits()
//   .text:06cf515a  85c3      TEST EBX,EAX             ; mask & bits
//   .text:06cf515c  74e2      JZ   <next>
//   .text:06cf515e  45        INC  EBP
// Every caller in StarSystem passes 0xffffffff, so in practice it counts the
// nodes whose object reports ANY type bit at all. node->Data is dereferenced
// unguarded, as in retail (the null test the port-only FSystemObjectEntry
// bridge needed is gone with the bridge, 2026-09-24).
// ===========================================================================
unsigned int ObjectList::count_type(unsigned int Mask) const
{
    unsigned int Count = 0;
    for (MetaNode<GameObject>* Node = Head; Node != nullptr; Node = Node->Next) {
        if ((Mask & Node->Data->get_type_bits()) != 0)
            ++Count;
    }
    return Count;
}

// ObjectList::collect_in_sphere -- FUN_06cf5020 (.text:06cf5020 .. 06cf5129,
// `RET 0x14`). PORTED 2026-09-24 (claude-code:w5-server-06d0).
//   .text:06cf5023 .. 06cf5033  FLD / FMUL / FSTP float: Radius * Radius
//   .text:06cf503b .. 06cf5041  MaxCount == 0 (JBE) -> 0
//   .text:06cf504b .. 06cf5060  node = node ? node->Next : Head; null ends it
//   .text:06cf5066 .. 06cf508f  wrapper null, CBase::Object null or (& 3) != 3
//                               -> next
//   .text:06cf5095 .. 06cf509b  CSimple TypeBits (+0xe0) & TypeMask == 0 -> next
//   .text:06cf509d .. 06cf50b3  TypeMask & 0x7f0000: is_destroyed (vt +0x160) -> next
//   .text:06cf50b5 .. 06cf50c4  FCOMP double 0.0 / TEST AH,5 / JP: Radius < 0.0
//                               takes the object without a distance test
//   .text:06cf50d5 .. 06cf5106  (dx*dx + dz*dz) + dy*dy on the x87 stack against
//                               the float square, TEST AH,0x41 / JP: <= keeps it
//   .text:06cf50c6 / 06cf5108   OutIds[Count++] = CSimple Id (+0xb0)
//   .text:06cf5116 .. 06cf511a  loop while Count < MaxCount (unsigned)
unsigned int ObjectList::collect_in_sphere(unsigned int* OutIds, unsigned int MaxCount,
                                           Vector const* Pos, float Radius,
                                           unsigned int TypeMask) const
{
    float const RadiusSq = static_cast<float>(static_cast<double>(Radius) * static_cast<double>(Radius));
    unsigned int Count = 0;
    if (MaxCount == 0u)
        return 0u;

    MetaNode<GameObject>* Node = nullptr;
    do
    {
        Node = (Node == nullptr) ? Head : Node->Next;
        if (Node == nullptr)
            break;

        GameObject* const Wrapper = Node->Data;
        if (Wrapper == nullptr)
            continue;
        CObject* const Raw = Wrapper->Object;
        if (Raw == nullptr || (static_cast<unsigned int>(Raw->ObjectClass) & 3u) != 3u)
            continue;
        CSimple* const Simple = static_cast<CSimple*>(Raw);
        if ((Simple->TypeBits & TypeMask) == 0u)
            continue;
        if ((TypeMask & 0x7f0000u) != 0u && Wrapper->is_destroyed())
            continue;

        if (static_cast<double>(Radius) < 0.0)
        {
            OutIds[Count] = Simple->Id;
            ++Count;
            continue;
        }

        double const Dx = static_cast<double>(Pos->x) - static_cast<double>(Simple->WorldPosition.x);
        double const Dy = static_cast<double>(Pos->y) - static_cast<double>(Simple->WorldPosition.y);
        double const Dz = static_cast<double>(Pos->z) - static_cast<double>(Simple->WorldPosition.z);
        double const DistSq = (Dx * Dx + Dz * Dz) + Dy * Dy;
        if (DistSq <= static_cast<double>(RadiusSq))
        {
            OutIds[Count] = Simple->Id;
            ++Count;
        }
    } while (Count < MaxCount);
    return Count;
}

// ObjectList::find_by_id -- FUN_06cf4f00 (.text:06cf4f00, `RET 0x4`):
//   .text:06cf4f10 .. 06cf4f1b  node = node ? node->Next : Head; stop on null
//   .text:06cf4f1f .. 06cf4f24  CALL [Data->vtable + 0x20]   ; get_id()
//   .text:06cf4f27  CMP EAX,EDI / JNZ -- the first match ends the walk
//   .text:06cf4f2c  return node (null when nothing matched)
MetaNode<GameObject>* ObjectList::find_by_id(unsigned int Id) const
{
    MetaNode<GameObject>* Node = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? Head : Node->Next;
        if (Node == nullptr)
            break;
        if (Node->Data->get_id() == Id)
            break;
    }
    return Node;
}

// ===========================================================================
// StarSystem::load — retail FUN_06d06ed0 (.text:06d06ed0).
// See the declaration in server_sysdb.h for the whole instruction map, for the
// arity correction (Ghidra says `void FUN_06d06ed0(void)`; .text:06d06ef0 is
// `MOV EBX,ECX`, so it is a thiscall with no stack arguments) and for what the
// populate helper does and does not reproduce.
// ===========================================================================
void StarSystem::load()
{
    // .text:06d06ef2  CMP byte ptr [EBX + 0xfc],0x1 / .text:06d06ef9 JZ -- note
    // that this guard tests for 1, not for "non-zero"; add_player's own guard
    // at .text:06d07e49 is the one that tests for zero.
    if (bLoaded == 1)
        return;

    // .text:06d06eff .. .text:06d06f06 -- `SystemInfo->vtable[0]()`. This port's
    // Universe::ISystem is a flat record rather than retail's interface (see
    // common_universe.h), so the same value is the NicknameId field. Retail
    // calls through SystemInfo without a null test; SysDB_Initialize sets it on
    // every entry it builds.
    unsigned int const SystemId = SystemInfo->NicknameId;

    // .text:06d06f0b  CALL COMMON.DLL::PhySys::Startup, cdecl, one argument
    // (.text:06d06f11 `ADD ESP,0x4`). The import is PhySys::Startup; earlier
    // notes in this tree called it "PhySys::Start", which is not the name in
    // the import table.
    PhySys::Startup(SystemId);

    // .text:06d06f14 .. .text:06d06f2e -- single player: wrap the solars the
    // client already built (FUN_06d07170), then mirror its asteroid fields
    // (FUN_06d03500).
    if (SinglePlayer()) {
        load_solars();
        SrvAsteroid::SrvAsteroid_MapSystem(SystemId);
    } else {
        // .text:06d06f33 .. .text:06d06f41 -- the system file's path:
        // SystemInfo vtable +0x14 is the file name (the flat record's File
        // field in this port), Universe::get_filename into a MAX_PATH buffer
        // (stack +0x18 .. +0x11c).
        char SystemIniPath[260];
        Universe::get_filename(SystemIniPath, SystemInfo->File);

        // .text:06d06f48 CmnAsteroid::Load(sysId), .text:06d06f54
        // FUN_06d032b0(sysId, path); both results dropped (.text:06d06f59
        // ADD ESP,0x14 with no test).
        CmnAsteroid::Load(SystemId);
        SrvAsteroid::SrvAsteroid_LoadSystem(SystemId, SystemIniPath);

        // .text:06d06f5c .. .text:06d06fd1 -- INI_Reader over the file; every
        // [Object] section goes to FUN_06d09d60, its result dropped. The
        // reader's destructor (.text:06d06fd3 .. 06d07041: FileMap::close and
        // the string destructors, inlined) closes it.
        INI_Reader Reader;
        if (Reader.open(SystemIniPath, false)) {
            while (Reader.read_header()) {
                if (Reader.is_header("Object"))
                    load_object(Reader);
            }
        }
    }

    // .text:06d07047 .. .text:06d070e7 -- the spatial partition over the
    // solars:
    //   06d07047  count = Solars.count_type(0xffffffff)      (FUN_06cf5130)
    //   06d0705c  this->Spatial = COMMON.DLL::NewSpatial("BSP") (.rdata:06d67d04)
    //   06d07065  when that is null nothing else happens (JZ 06d070e8)
    //   06d07074  operator new(count * 0x14)
    //   06d07085 .. 06d070ca  for EVERY node of the Solars list (the count
    //             above filtered, the walk does not) one 0x14-byte record:
    //             +0x00 node->Data                    (06d07098)
    //             +0x04 get_position(), three dwords  (06d0709f CALL [EAX] ..
    //                                                  06d070b1)
    //             +0x10 cobject()->Radius, a float    (06d070b8 CALL [EDX+0x150],
    //                                                  06d070be FLD [EAX+0x38])
    //   06d070d7  Spatial->vtable[+0x14](array, count)  -- the count is pushed
    //             first, so the array is the first argument
    //   06d070df  operator delete(array)
    int const SolarCount = static_cast<int>(Solars.count_type(0xffffffffu));
    Spatial = NewSpatial("BSP");
    if (Spatial != nullptr) {
        FSpatialEntry* const Entries = static_cast<FSpatialEntry*>(
            ::operator new(static_cast<size_t>(SolarCount) * sizeof(FSpatialEntry)));
        FSpatialEntry* Out = Entries;
        FSystemListNode* Node = nullptr;
        for (;;) {
            Node = (Node == nullptr) ? Solars.Head : Node->Next;
            if (Node == nullptr)
                break;
            GameObject* const Obj = Node->Data;
            Out->Object = static_cast<IObjRW*>(Obj);
            Out->Position = Obj->get_position();
            Out->Radius = Obj->cobject()->Radius;
            ++Out;
        }
        // ISpatialPartition +0x14: AddArray(entries, count).
        Spatial->AddArray(Entries, static_cast<unsigned>(SolarCount));
        ::operator delete(Entries);
    }

    // .text:06d070eb  MOV byte ptr [EBX + 0xfc],0x1
    bLoaded = 1;

    // .text:06d070e8 .. .text:06d07101 -- retail re-reads the system id off
    // SystemInfo (it is the same value) into a stack slot and broadcasts
    // FUN_06d43650(0x3c, &sysId): "this system is now loaded". FUN_06d43650 is
    // pub::Controller::_SendMessage(0, id, data), which this port reaches
    // through Server_ControllerDispatch.
    unsigned int EventData = SystemId;
    Server_ControllerDispatch(0u, pub::Controller::EMessage::SystemLoaded, &EventData);
}

// ===========================================================================
// StarSystem::load_solars -- retail FUN_06d07170 (.text:06d07170). See the
// declaration in server_sysdb.h for the instruction map.
// ===========================================================================
void StarSystem::load_solars()
{
    int Count = 0;                                                  // .text:06d07191
    CObject* Obj = CObject::FindFirst(CObject::ClassSolar);         // .text:06d07199 PUSH 0x303
    if (Obj != nullptr)
    {
        do
        {
            // .text:06d071b2 -- the walk's own reference; attach_existing
            // takes the wrapper's, and .text:06d07224 gives this one back.
            Obj->AddRef();
            ::Solar* const Wrapper = new ::Solar();                 // .text:06d071bd / FUN_06d02b70

            // .text:06d071e2 .. 06d07206 -- the object only when its class
            // passes the 0x303 test, else 0.
            CObject* const Attached =
                ((static_cast<unsigned>(Obj->ObjectClass) & 0x303u) == 0x303u) ? Obj : nullptr;
            Wrapper->attach_existing(Attached);                     // FUN_06d02c20

            Wrapper->enter_system(this, nullptr);                   // .text:06d07212 vt +0x170
            Wrapper->check_fuses();                                 // .text:06d0721c vt +0x1bc
            Obj->Release();                                         // .text:06d07224

            // .text:06d0722a .. 06d0726b -- MetaList append, inlined (the
            // FreeList reuse when bUseHeap, else operator new(0xc)).
            Solars.append(Wrapper);
            ++Count;

            Obj = CObject::FindNext();                              // .text:06d07272
        } while (Obj != nullptr);

        if (Count != 0)                                             // .text:06d07286 JNZ
            return;
    }

    // .text:06d0728b .. 06d072ac
    FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
          "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x129,
          "failed to find any solar objects\n");
}

// ===========================================================================
// StarSystem::load_object -- retail FUN_06d09d60 (.text:06d09d60, RET 4). See
// the declaration in server_sysdb.h for the instruction map.
//
// The CSolar::CreateParms stack object: retail inlines its constructor and
// destructor. The CSimple half is the type's default member initialisers
// (FUN_06d08e20's values); the EqObj fields the inlined ctor writes are
// written below in the same order, and the ones it leaves alone are zeroed
// (retail leaves them as stack garbage for CSolar::ReadObj to overwrite).
// The anim-name list at +0xe4 (allocator byte) / +0xe8 (head) / +0xec (size)
// is a std::list<char const*> member of CSolar::CreateParms
// (CSolar::CreateParms::ContinualAnims, common_cobject.h): its ctor FUN_06d1ad30
// allocates a head linked to itself, CSolar::ReadObj appends one name per "anim"
// key, and the parms destructor (inlined at .text:06d09f4f / 06d09fd9: FUN_06d5c540
// begin + FUN_06cf7720 erase(first, last), then the head) frees it after
// CSolar::init. On an exception the same destructor runs from Unwind@06d613f4
// (-> ~CSolar::CreateParms FUN_06d0a120: that list first, then +0xac, then the
// vector); the member's own destructor is that unwind in the port.
// ===========================================================================
GameObject* StarSystem::load_object(INI_Reader& Reader)
{
    // .text:06d09d80 .. 06d09e47 -- the parms.
    CSolar::CreateParms Parms;
    Parms.Pad58 = 0;
    Parms.DockingCount = 0;
    Parms.VibeId = 0;
    Parms.SpaceCostume.PartIds[0] = 0;                             // FUN_06d0a060 +0x64
    Parms.SpaceCostume.PartIds[1] = 0;                             //              +0x68
    Parms.SpaceCostume.PartIds[2] = 0;                             //              +0x6c
    Parms.SpaceCostume.PartIds[3] = 0;                             //              +0x70
    for (int Index = 0; Index < 8; ++Index)
        Parms.SpaceCostume.AccessoryIds[Index] = 0;
    Parms.SpaceCostume.AccessoryCount = 0;                         //              +0x94
    Parms.VoiceId = 0;                                             // .text:06d09daa +0x98
    Parms.NicknameSrc.Id = 0;                                      // ID_String::clear +0xb8
    Parms.VisitValue = 0;                                          // .text:06d09df1 +0xbc
    Parms.ParentNicknameId = 0;                                    // ID_String::clear +0xc0
    Parms.bDestructible = 0;                                       // .text:06d09e01 +0xc4
    Parms.bDynamic = 0;                                            // .text:06d09e08 +0xc5
    Parms.DestSystem = 0;                                          // .text:06d09e0f +0xc8
    Parms.DestGateId = 0;                                          // ID_String::clear +0xcc
    Parms.PrevRing = 0;                                            // .text:06d09e24 +0xd0
    Parms.NextRing = 0;                                            // .text:06d09e2b +0xd4
    Parms.TradelaneSpaceNameId = 0;                                // .text:06d09e32 +0xd8
    Parms.AtmosphereRange = 0.0f;                                  // .text:06d09e39 +0xdc
    Parms.DockWithBaseId = 0;                                      // .text:06d09e40 +0xe0
    // +0xe4: Parms.ContinualAnims was built empty with Parms (FUN_06d1ad30).

    // .text:06d09e4c .. 06d09e69 -- the bool result is not read.
    Archetype::Solar* Arch = nullptr;
    CSolar::ReadObj(Reader, Arch, Parms);
    // .text:06d09e78 MOV [ESP+0x20],EDI -- Parms+0x04 (ObjDb) = this, after ReadObj and
    // before the archetype test; CSolar::init's connect() hands it to init_physics.
    Parms.ObjDb = this;

    ::Solar* Wrapper = nullptr;
    if (Arch != nullptr)                                           // .text:06d09e76 / 06d09e7c
    {
        Arch->load();                                              // .text:06d09e84 vt +0xc
        Wrapper = new ::Solar();                                   // .text:06d09e8c / FUN_06d02b70
        Wrapper->create(Arch);                                     // .text:06d09ec2 vt +0x164
        Wrapper->csolar()->init(Parms);                            // .text:06d09ec8 .. 06d09ef0

        // .text:06d09ef6 .. 06d09f30 -- MetaList append, inlined.
        Solars.append(Wrapper);

        Arch->Release();                                           // .text:06d09f33
        Wrapper->enter_system(this, nullptr);                      // .text:06d09f3f vt +0x170
        Wrapper->check_fuses();                                    // .text:06d09f49 vt +0x1bc
    }

    // .text:06d09f4f .. 06d09f9e / .text:06d09fd9 .. 06d0a02c -- the parms
    // destroyed: the anim list, then the CollisionGroupDesc list and the
    // EquipDescVector. That is Parms' destructor, run on return.
    return Wrapper;                                                // .text:06d09fd5 / 06d0a043
}

// ===========================================================================
// StarSystem::create_solar -- retail FUN_06d0a200 (.text:06d0a200, RET 4). See
// the declaration in server_sysdb.h for the instruction map. S = ESP after the
// four register pushes; `this` is kept at [S+0x10], the CSolar at [S+0x1c],
// the CSolar::CreateParms at [S+0x28].
// ===========================================================================
::Solar* StarSystem::create_solar(CSolarCreateInfo& Req)
{
    // .text:06d0a21e .. 06d0a24d -- operator new(0x90) + FUN_06d02b70.
    ::Solar* const Wrapper = new ::Solar();

    // .text:06d0a255 .. 06d0a289
    Archetype::Solar* const Arch = Archetype::GetSolar(Req.ArchetypeNickname);
    Arch->load();                                                  // vt +0xc
    Wrapper->create(Arch);                                         // vt +0x164
    Arch->Release();                                               // Archetype::Root::Release

    // .text:06d0a289 .. 06d0a2b0 -- the CBase object under the 0x303 guard.
    CObject* const Raw = Wrapper->Object;
    CSolar* const Cs =
        (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x303u) == 0x303u)
            ? static_cast<CSolar*>(Raw) : nullptr;

    // .text:06d0a2b0 .. 06d0a2b4 -- FUN_06d0a770: the CSimple half is the
    // type's default member initialisers (Id / ObjDb / OwnerPlayer 0, HitPts
    // -1.0f, Pos 0, Orient identity via FUN_06d0a850, Velocity / AngularVelocity
    // 0); then +0x58 / +0x5c 0, +0x60 -1, +0x64..+0x70 0, +0x94 / +0x98 0, the
    // EquipDescVector and the CollisionGroupDesc list empty. +0x74..+0x93
    // (the accessory ids) are not written by it.
    CSolar::CreateParms Parms;
    Parms.Pad58 = 0u;
    Parms.DockingCount = 0u;
    Parms.VibeId = 0xffffffffu;                                    // .text:06d0a7ce
    Parms.SpaceCostume.PartIds[0] = 0u;
    Parms.SpaceCostume.PartIds[1] = 0u;
    Parms.SpaceCostume.PartIds[2] = 0u;
    Parms.SpaceCostume.PartIds[3] = 0u;
    Parms.SpaceCostume.AccessoryCount = 0;
    Parms.VoiceId = 0u;

    // .text:06d0a2bf .. 06d0a34b -- the CSolar tail.
    Parms.NicknameSrc.clear();                                     // +0xb8
    Parms.VisitValue = 0u;                                         // +0xbc
    reinterpret_cast<ID_String&>(Parms.ParentNicknameId).clear();  // +0xc0
    Parms.bDestructible = 0u;                                      // +0xc4
    Parms.bDynamic = 0u;                                           // +0xc5
    Parms.DestSystem = 0u;                                         // +0xc8
    reinterpret_cast<ID_String&>(Parms.DestGateId).clear();        // +0xcc
    Parms.PrevRing = 0u;                                           // +0xd0
    Parms.NextRing = 0u;                                           // +0xd4
    Parms.TradelaneSpaceNameId = 0u;                               // +0xd8
    Parms.AtmosphereRange = 0.0f;                                  // +0xdc
    Parms.DockWithBaseId = 0u;                                     // +0xe0
    // +0xe4: Parms.ContinualAnims was built empty with Parms (FUN_06cfc340(0, 0) head).

    // .text:06d0a352 .. 06d0a3cc
    Parms.HitPts = static_cast<float>(Req.HitPts);                 // FILD +0x68
    Parms.Id = Req.ObjId;                                          // +0x04
    Parms.ObjDb = this;
    Parms.Pos = Req.Position;                                      // +0x4c
    Parms.OwnerPlayer = 0u;
    Server_QuatToMatrix(Parms.Orient, Req.Orientation);            // DAT_06d8da08 vt +0x54, REP MOVSD

    // .text:06d0a3b3 .. 06d0a453
    Parms.VisitValue = Req.Field_0xb4;
    Parms.DockingCount = Req.DockingBaseId;                        // +0x0c -> +0x5c
    Parms.DockWithBaseId = Req.DockingBaseId;                      //       -> +0xe0
    Parms.VibeId = static_cast<unsigned int>(Req.VibeGroupId);     // +0x10 -> +0x60
    Parms.SpaceCostume.PartIds[0] = Req.CostumeHead;               // +0x14 -> +0x64
    Parms.VoiceId = Req.VoiceId;                                   // +0x48 -> +0x98
    Parms.SpaceCostume.PartIds[1] = Req.CostumeBody;               // +0x18 -> +0x68
    Parms.SpaceCostume.PartIds[2] = Req.CostumeLeftHand;           // +0x1c -> +0x6c
    Parms.SpaceCostume.PartIds[3] = Req.CostumeRightHand;          // +0x20 -> +0x70
    Parms.SpaceCostume.AccessoryCount = Req.CostumeAccessoryCount; // +0x44 -> +0x94
    // JLE / JL (signed), the count re-read from Parms+0x94 every pass; no clamp.
    for (int Index = 0; Index < (int)Parms.SpaceCostume.AccessoryCount; ++Index)
        Parms.SpaceCostume.AccessoryIds[Index] = Req.CostumeAccessory[Index];

    // .text:06d0a455 .. 06d0a471 -- vector<EquipDesc>::operator= from the
    // loadout's items (Map +4; the port's Map keeps them in a std::vector).
    // Parms.Equipment is still empty here, so VC6's operator= takes its
    // reallocating branch for a non-empty source and does nothing for an
    // empty one. Loadout::Get's result is not tested.
    if (Req.Field_0x6c != 0u)
    {
        Loadout::Map const* const Map = Loadout::Get(Req.Field_0x6c);
        int const Count = static_cast<int>(Map->Items.size());
        if (Count > 0)
        {
            EquipDesc* const Block =
                static_cast<EquipDesc*>(::operator new(static_cast<size_t>(Count) * sizeof(EquipDesc)));
            EquipDesc* Dest = Block;
            for (int Index = 0; Index < Count; ++Index)
            {
                new (Dest) EquipDesc(Map->Items[static_cast<size_t>(Index)]);
                ++Dest;
            }
            Parms.Equipment.Begin = Block;
            Parms.Equipment.End = Dest;
            Parms.Equipment.Cap = Dest;
        }
    }
    Parms.Equipment.append(Req.Equipment);                         // .text:06d0a482

    // .text:06d0a488 .. 06d0a506 -- list operator=, inlined (the two lists are
    // never the same object).
    Parms.ArchGroupDescs = Req.CollisionGroups;

    // .text:06d0a50c .. 06d0a537
    Parms.NicknameSrc = Req.Field_0xb8;                            // +0xb8
    Parms.bDestructible = 1u;                                      // +0xc4
    Parms.bDynamic = Req.Field_0xac;                               // +0xc5
    Cs->init(Parms);

    // .text:06d0a53d .. 06d0a54f
    if ((Req.Field_0xbc & 0x3fffffffu) != 0u)
        Wrapper->mark_for_player(Req.Field_0xbc, 1);

    Wrapper->enter_system(this, nullptr);                          // .text:06d0a55f vt +0x170
    Wrapper->check_fuses();                                        // .text:06d0a569 vt +0x1bc

    // .text:06d0a56f .. 06d0a596 -- MetaList append on +0x5c, inlined.
    Solars.append(Wrapper);

    // .text:06d0a599 .. 06d0a614 -- the spatial partition's single add (vt +0x10)
    // with the same 0x14-byte record load() bulk-adds.
    void* const Bucket = GetBucket(0xfdffu);
    if (Bucket != nullptr)
    {
        CObject* const BucketRaw = Wrapper->Object;
        CSolar* const BucketSolar =
            (BucketRaw != nullptr && (static_cast<unsigned int>(BucketRaw->ObjectClass) & 0x303u) == 0x303u)
                ? static_cast<CSolar*>(BucketRaw) : nullptr;
        float const Radius = BucketSolar->Radius;                  // .text:06d0a5cc [EAX+0x38]
        Vector const& Position = Wrapper->get_position();          // .text:06d0a5d7 vt 0
        FSpatialEntry Entry;
        Entry.Position = Position;
        Entry.Radius = Radius;
        Entry.Object = static_cast<IObjRW*>(Wrapper);
        static_cast<ISpatialPartition*>(Bucket)->Add(&Entry); // +0x10
    }

    // .text:06d0a617 .. 06d0a61d -- on the CSolar kept at [S+0x1c].
    Cs->enable_collisions_r(true);

    // .text:06d0a623 .. 06d0a66f -- every player of this system.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        Req.bMarkedBy = Wrapper->is_marked_by(Player->View.PlayerId) ? 1u : 0u;  // +0xa8
        Req.VibeOtherGroupId = Players[Player->View.PlayerId].rep_vibe;           // +0xb0
        SystemPlayerView_CreateSolarForPlayer(&Player->View, &Req, Wrapper);
    }

    // .text:06d0a671 .. 06d0a74a -- the parms destroyed on return: the anim
    // list (every node erased, FUN_06d1b490 / FUN_06d0f620, then the head),
    // then the group list and the EquipDescVector (.text:06d0a6da .. 06d0a744).
    return Wrapper;                                                // .text:06d0a753
}

// ===========================================================================
// StarSystem::create_ship -- retail FUN_06d0a880 (.text:06d0a880, RET 4). See
// the declaration in server_sysdb.h for the instruction map. S = ESP after the
// four register pushes: the wrapper at [S+0x18], `this` at [S+0x20], the
// CShip at [S+0x14], the request copy at [S+0x2c], the CShip::CreateParms at
// [S+0x10c].
// ===========================================================================
::Ship* StarSystem::create_ship(FShipCreateRequest const& Req)
{
    // .text:06d0a8a1 .. 06d0a8d0 -- operator new(0xc8) + FUN_06d00e80.
    ::Ship* const Wrapper = new ::Ship();

    // .text:06d0a8d2 .. 06d0a98f -- FUN_06d0af00 (the CEqObjCreateInfo copy
    // constructor) and the derived tail +0xac .. +0xdc; the vftable is the
    // CShipCreateInfo one (.rdata:06d65f78).
    CShipCreateInfo Copy(static_cast<CShipCreateInfo const&>(Req));

    CShip* Cs = nullptr;                                            // [S+0x14]
    if (Copy.OwnerPlayer != 0u && SinglePlayer())                   // .text:06d0a997 / 06d0a9a6
    {
        // .text:06d0a9b0 .. 06d0a9ec -- vt +0x168 with &Req +0x04 (the request,
        // not the copy), then the 0x503 narrowing.
        Wrapper->find(Req.ObjId);
        CObject* const Raw = Wrapper->Object;
        Cs = (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                 ? static_cast<CShip*>(Raw) : nullptr;
    }
    else
    {
        // .text:06d0a9f1 .. 06d0aa15
        Archetype::Ship* const Arch = Archetype::GetShip(Copy.ArchetypeNickname);
        Arch->load();                                               // vt +0xc
        Wrapper->create(Arch);                                      // vt +0x164
        Arch->Release();                                            // Archetype::Root::Release

        // .text:06d0aa1b .. 06d0aa44
        CObject* const Raw = Wrapper->Object;
        Cs = (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                 ? static_cast<CShip*>(Raw) : nullptr;

        // .text:06d0aa44 .. 06d0aa65 -- FUN_06d0a770 (the CSimple half is the
        // type's default member initialisers; see create_solar), then the
        // CShip tail. The CEqObj tail +0xb8 .. +0xe4 is not written (retail
        // leaves it as stack garbage; CShip::init does not read it).
        CShip::CreateParms Parms;
        Parms.Pad58 = 0u;
        Parms.DockingCount = 0u;
        Parms.VibeId = 0xffffffffu;                                // .text:06d0a7ce
        Parms.SpaceCostume.PartIds[0] = 0u;
        Parms.SpaceCostume.PartIds[1] = 0u;
        Parms.SpaceCostume.PartIds[2] = 0u;
        Parms.SpaceCostume.PartIds[3] = 0u;
        Parms.SpaceCostume.AccessoryCount = 0;
        Parms.VoiceId = 0u;
        Parms.FieldE8 = 0u;                                       // +0xe8 [S+0x1f4]
        Parms.GroupNameId = 0u;                                    // +0xec [S+0x1f8]
        Parms.Rank = 0u;                                           // +0xf0 [S+0x1fc]
        Parms.bUsingTradelane = 0u;                                // +0xf4 [S+0x200]

        // .text:06d0aa6d .. 06d0abbf
        Parms.HitPts = static_cast<float>(Copy.HitPts);            // FILD +0x68
        Parms.OwnerPlayer = Copy.OwnerPlayer;                      // +0xac -> +0x08
        Parms.Id = Copy.ObjId;                                     // +0x04 -> +0x00
        Parms.Pos = Copy.Position;                                 // +0x4c -> +0x10
        Parms.ObjDb = this;                                        // EDI -> +0x04
        Server_QuatToMatrix(Parms.Orient, Copy.Orientation);       // +0x58, REP MOVSD -> +0x1c
        Parms.Velocity = Copy.Velocity;                            // +0xb8 -> +0x40
        Parms.AngularVelocity = Copy.AngularVelocity;              // +0xc4 -> +0x4c
        Parms.VibeId = static_cast<unsigned int>(Copy.VibeGroupId); // +0x10 -> +0x60
        Parms.bUsingTradelane = Copy.bUsingTradelane;              // byte +0xdc -> +0xf4
        Parms.SpaceCostume.PartIds[0] = Copy.CostumeHead;          // +0x14 -> +0x64
        Parms.VoiceId = Copy.VoiceId;                              // +0x48 -> +0x98
        Parms.SpaceCostume.PartIds[3] = Copy.CostumeRightHand;     // +0x20 -> +0x70
        Parms.Rank = Copy.ReputationRank;                          // +0xb4 -> +0xf0
        Parms.SpaceCostume.PartIds[2] = Copy.CostumeLeftHand;      // +0x1c -> +0x6c
        Parms.SpaceCostume.PartIds[1] = Copy.CostumeBody;          // +0x18 -> +0x68
        Parms.SpaceCostume.AccessoryCount = Copy.CostumeAccessoryCount; // +0x44 -> +0x94
        // .text:06d0abc6 JLE / .text:06d0abc8 loop (signed), the count re-read from
        // Parms+0x94 every pass; no clamp.
        for (int Index = 0; Index < (int)Parms.SpaceCostume.AccessoryCount; ++Index)
            Parms.SpaceCostume.AccessoryIds[Index] = Copy.CostumeAccessory[Index];

        // .text:06d0abdf .. 06d0abff -- vector<EquipDesc>::operator= from the
        // loadout's items (see create_solar: the destination is empty, so only
        // the reallocating branch or nothing). No test on Loadout::Get.
        if (Copy.Field_0x6c != 0u)
        {
            Loadout::Map const* const Map = Loadout::Get(Copy.Field_0x6c);
            int const Count = static_cast<int>(Map->Items.size());
            if (Count > 0)
            {
                EquipDesc* const Block =
                    static_cast<EquipDesc*>(::operator new(static_cast<size_t>(Count) * sizeof(EquipDesc)));
                EquipDesc* Dest = Block;
                for (int Index = 0; Index < Count; ++Index)
                {
                    new (Dest) EquipDesc(Map->Items[static_cast<size_t>(Index)]);
                    ++Dest;
                }
                Parms.Equipment.Begin = Block;
                Parms.Equipment.End = Dest;
                Parms.Equipment.Cap = Dest;
            }
        }
        Parms.Equipment.append(Copy.Equipment);                    // .text:06d0ac14

        // .text:06d0ac1a .. 06d0ac8e -- list operator=, inlined.
        Parms.ArchGroupDescs = Copy.CollisionGroups;

        // .text:06d0ac94 .. 06d0acbd
        Parms.GroupNameId = Copy.Field_0xb0;                       // +0xb0 -> +0xec
        Parms.DockingCount = Copy.DockingBaseId;                   // +0x0c -> +0x5c
        Cs->init(Parms);                                           // vt +0x10c

        // .text:06d0acc3 .. 06d0ace5 -- the owner's player group id into
        // CShip +0x1b4 (GroupId).
        if (Copy.OwnerPlayer != 0u)
        {
            CPlayerGroup* const Group =
                static_cast<CPlayerGroup*>(Players[Copy.OwnerPlayer].group_ptr);   // slot -0x14
            if (Group != nullptr)
            {
                // [group+4] read inline in retail; GetID is that same load.
                unsigned int const GroupId = Group->GetID();
                Cs->GroupId = GroupId;
            }
        }
        // .text:06d0aceb .. 06d0ad0f -- the parms' list and vector destroyed.
    }

    // .text:06d0ad19 .. 06d0ad41 -- an EquipIdMaker constructed, then its word
    // and the dword after it overwritten from Req +0xe0 / +0xe4.
    ::Ship::FEquipState State;
    State.EquipIds = Req.EquipIds;
    State.Data = Req.EquipStateData;

    // .text:06d0ad30 .. 06d0ad4c
    if ((Req.MarkPlayerId & 0x3fffffffu) != 0u)
        Wrapper->mark_for_player(Req.MarkPlayerId, 1);

    Wrapper->enter_system(this, nullptr);                          // .text:06d0ad58 vt +0x170
    Wrapper->set_equip_state(State);                               // .text:06d0ad65 FUN_06d010e0

    // .text:06d0ad6a .. 06d0ad8f -- MetaList append on +0x34, inlined.
    Ships.append(Wrapper);

    // .text:06d0ad92 .. 06d0adab
    if (!SinglePlayer() || Copy.OwnerPlayer == 0u)
        Cs->enable_collisions_r(true);

    // .text:06d0adb1 .. 06d0adf0 -- every player of this system but the owner.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (Player->View.PlayerId == Copy.OwnerPlayer)
            continue;
        Copy.bMarkedBy = Wrapper->is_marked_by(Player->View.PlayerId) ? 1u : 0u;   // copy +0xa8
        SystemPlayerView_CreateShipForPlayer(&Player->View, &Copy, Wrapper);
    }

    // .text:06d0adf2 .. 06d0aed2 -- ~CShipCreateInfo on the copy.
    return Wrapper;                                                // .text:06d0aedf
}

// ===========================================================================
// StarSystem::add_player — retail FUN_06d07e20(this, SessionId), 0x06d07e20.
// The list-insert half; see the declaration in server_sysdb.h for the full
// instruction map and for what is still missing.
// ===========================================================================
void StarSystem::add_player(unsigned int SessionId)
{
    // .text:06d07e41  MOV AL,[EDI + 0xfc] / .text:06d07e49 CMP AL,BL /
    // .text:06d07e4f JNZ / .text:06d07e51 CALL FUN_06d06ed0 -- an unloaded
    // system is loaded first, and the load is what sets +0xfc to 1. EBX is zero
    // from .text:06d07e47 `XOR EBX,EBX`, so the test really is against zero
    // here, while load()'s own guard compares against 1 (.text:06d06ef2).
    if (bLoaded == 0)
        load();

    // .text:06d07e5d  TEST EBP,0x3fffffff / .text:06d07e63 JZ -- and note the
    // mask. Retail does not test the id against zero; it tests it against
    // 0x3fffffff, so an id whose only set bits are the top two is rejected too.
    if ((SessionId & 0x3fffffffu) == 0)
        return;

    // .text:06d07e76 / .text:06d07e79 -- the list is `this + 0x8` and the
    // insert position is its sentinel `this + 0xc`, i.e. a push_back.
    // No null test on the sentinel: retail loads [EDI+0xc] and links through it
    // (FUN_06d0f330, .text:06d0f330 -- `MOV ECX,[EAX+4]` on the position node
    // with no test). The StarSystem constructor always allocates it
    // (.text:06d06d17 FUN_06d0f3e0(0,0)).
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);

    // .text:06d07e8a  std::list<Observer>::insert(end(), Observer(SessionId)).
    // Retail builds the element on the stack first (.text:06d07e69 PUSH EBP /
    // .text:06d07e71 CALL FUN_06cf51e0), lets insert copy it into the freshly
    // allocated node, then destroys the temporary with a direct call
    // (.text:06d07ea1 CALL FUN_06cf52d0) -- the end of this block.
    {
        Observer Temp(SessionId);

        // FUN_06d0f330 (.text:06d0f330, list::insert, `RET 0xc`), inlined:
        //   06d0f336  EBP = position->Prev (read before the allocation)
        //   06d0f33f  operator new(0x50)
        //   06d0f34b  node->Next = position
        //   06d0f347 .. 06d0f34f  `TEST EBP,EBP / JNZ / MOV EBP,ESI` -- MSVC6
        //             _Buynode's null fallback: a null Prev becomes the node
        //   06d0f355  node->Prev = EBP; 06d0f358 position->Prev = node;
        //   06d0f35b .. 06d0f363  node->Prev->Next = node
        //   06d0f365  FUN_06d0f6d0 -- the element copy-constructed at node+8
        //   06d0f36a .. 06d0f371  ++size
        SystemPlayerNode* Prev = Sentinel->Prev;
        SystemPlayerNode* const Node =
            static_cast<SystemPlayerNode*>(::operator new(sizeof(SystemPlayerNode)));
        Node->Next = Sentinel;
        if (Prev == nullptr)
            Prev = Node;
        Node->Prev = Prev;
        Sentinel->Prev = Node;
        Node->Prev->Next = Node;
        ::new (static_cast<void*>(&Node->View)) Observer(Temp);
        ++PlayerListSize;
    }

    // .text:06d07ea6  MOV EAX,[EDI+0xc] / MOV EAX,[EAX+4] / ADD EAX,0x8 --
    // retail recovers the live element as sentinel->prev->View and keeps it in
    // local_1d0 ([ESP+0x1c]) for every pass below.
    Observer* const View = &Sentinel->Prev->View;

    // .text:06d07eb3  CALL COMMON.DLL::SinglePlayer / TEST AL,AL /
    // .text:06d07ebb JNZ LAB_06d082fa -- the multiplayer-only head is SKIPPED in
    // single player.
    bool const bSingle = SinglePlayer();

    if (!bSingle) {
        // .text:06d07ec1 .. .text:06d07ecf -- the system id comes off
        // SystemInfo's own vtable slot 0, and the two arguments are pushed
        // id-then-session, so the session is the FIRST parameter:
        //   .text:06d07ec8  50  PUSH EAX   ; the system id  -> 2nd argument
        //   .text:06d07ec9  55  PUSH EBP   ; the session id -> 1st argument
        Players.SendSystemID(SessionId,
                             (SystemInfo != nullptr) ? SystemInfo->NicknameId : 0u);

        // .text:06d07ed4 .. .text:06d07ef4 -- latch Players[SessionId] + 0x318
        // (`IMUL EAX,EAX,0x418` / `LEA EAX,[EAX + ECX + -0x418]` /
        // `MOV EAX,[EAX + 0x318]`) into [ESP+0x30]. PlayerDB::operator[] is the
        // same unchecked `m_PlayerArray[Id-1]`. Read back only by the
        // AttitudeTowards call below.
        int const PlayerRep = Players[SessionId].rep_vibe;

        // .text:06d07eef .. .text:06d07eff -- count the SOLAR block at +0x5c
        // (FUN_06cf5130 with mask -1), and skip the walk when it is empty.
        FSystemListNode* const SolarHead = Solars.Head;                // [EDI + 0x60]
        if (Solars.count_type(0xffffffffu) != 0) {
            // .text:06d07f05 .. .text:06d07f28 -- node = [this+0x60], then
            // node = node->Next, until null.
            for (FSystemListNode* SolarNode = SolarHead; SolarNode != nullptr;
                 SolarNode = SolarNode->Next) {
                // Retail dereferences node->Data unguarded (.text:06d07f2e).
                GameObject* const Obj = SolarNode->Data;

                // .text:06d07f2e .. .text:06d07f54 -- CALL [EAX + 0x150]
                // (IObjInspect::cobject), then `AND 0x303 / CMP 0x303`. Anything
                // that is not a solar keeps no CObject (`XOR EDI,EDI`).
                CObject* Raw = const_cast<CObject*>(Obj->cobject());
                CSolar* Solar = nullptr;
                if (Raw != nullptr &&
                    (static_cast<unsigned int>(Raw->ObjectClass) & 0x303u) == 0x303u)
                    Solar = static_cast<CSolar*>(Raw);

                if (Solar != nullptr && Solar->bDynamic) {
                    // .text:06d08226 CMP byte [EDI + 0x1a9],BL / JZ -- a DYNAMIC
                    // solar gets CREATESOLAR and skips the damage list.
                    // .text:06d08232 .. .text:06d08257 -- FUN_06cf63f0 base ctor,
                    // +0xac = 0, +0xad = 0, +0xb0 = -1, vtable 06cf0260.
                    CSolarCreateInfo Info;
                    // .text:06d08276  CALL FUN_06d07950(&Info, cobj)
                    CSolarCreateInfo_Fill(&Info, Solar);
                    // .text:06d0827b .. .text:06d08295 -- FUN_06cef450 on
                    // node->Data, byte into Info+0xa8.
                    Info.bMarkedBy = Obj->is_marked_by(SessionId) ? 1u : 0u;
                    // .text:06d0829f  CALL [EAX + 0xb8] -- IClient::SendServerCreateSolar,
                    // FLPACKET_SERVER_CREATESOLAR, (session, &Info).
                    ::g_pClientInterface->SendServerCreateSolar(SessionId, Info);
                    // .text:06d082a5 .. .text:06d082ef -- Info's destructor.
                } else {
                    // .text:06d07f56 .. .text:06d07fde -- everything else,
                    // including a static solar: a DamageList on the stack
                    // (+0x0c = 0, +0x10 = 0x19 at .text:06d07f8a, +0x14 = 0,
                    // +0x18 = 0), filled by node->Data's get_damage_list
                    // (CALL [EDX + 0x1a8], PUSH 0x3f800000 = 1.0f).
                    DamageList Damage;
                    Damage.set_destroyed(false);
                    // The same unnamed cause 0x19 the observer writes
                    // (.text:06cf61d2); see the note there.
                    Damage.set_cause(static_cast<DamageCause>(0x19));
                    Damage.set_inflictor_id(0u);
                    Damage.set_inflictor_owner_player(0u);
                    Obj->get_damage_list(Damage, 1.0f);
                    // .text:06d07fa6  CMP [ESP + 0x68],EBX -- the list's size.
                    if (!Damage.Entries.empty()) {
                        // .text:06d07fbe CALL [EDX + 0x20] (get_id) /
                        // .text:06d07fc9 CALL [ESI + 0xfc] -- IClient::SendServerDamageObject,
                        // DAMAGEOBJECT, (session, id, &list).
                        ::g_pClientInterface->SendServerDamageObject(SessionId, Obj->get_id(), Damage);
                    }
                }

                // .text:06d07fe4  CMP EDI,EBX / JZ -- the rest only for a solar,
                // dynamic or not.
                if (Solar == nullptr)
                    continue;

                // .text:06d07fec .. .text:06d08083 -- SETREPUTATION for the
                // solar's vibe. Two FmtStr default ctors (FUN_06d55bc0),
                // +0x10 = 0, byte +0x14 = 0, word +0x48 = 0, +0x00 = cobj+0x104.
                // +0x04/+0x08 come back from the two Reputation calls; +0x0c is
                // never written (stack residue in retail, zeroed here).
                FServerSetReputationInfo Rep;
                Rep.Field_0x0c = 0u;
                Rep.Relations = nullptr;
                Rep.RelationCount = 0;
                Rep.DisplayName[0] = L'\0';
                Rep.VibeHandle = static_cast<int>(Solar->VibeId);
                // .text:06d08052  AttitudeTowards(&Rep+0, &PlayerRep, &Rep+4)
                ::Reputation::Vibe::AttitudeTowards(Rep.VibeHandle, PlayerRep,
                                                    Rep.Attitude);
                // .text:06d08069  GetAffiliation(&Rep+0, &Rep+8, false)
                ::Reputation::Vibe::GetAffiliation(Rep.VibeHandle, Rep.Field_0x08,
                                                   false);
                // .text:06d08083  CALL [EDX + 0x108] -- IClient::SendServerSetReputation.
                ::g_pClientInterface->SendServerSetReputation(SessionId, Rep);

                // .text:06d08089 .. .text:06d0813f -- CEquipTraverser(0x80) over
                // cobj+0xe4. No CEGun::cast: the traverser's mask already
                // selects guns, and GetTarget is called on the element as is.
                CEquipTraverser Walk(0x80);
                for (CEquip* Equip = Solar->EquipManager.Traverse(Walk);
                     Equip != nullptr;
                     Equip = Solar->EquipManager.Traverse(Walk)) {
                    // .text:06d080b7  CALL [EAX + 0xc] -- IsFunctioning.
                    if (!Equip->IsFunctioning())
                        continue;
                    // .text:06d080c4 .. .text:06d080dd -- target = 0,
                    // sub = SUBOBJ_ID_NONE, CEGun::GetTarget(target, sub).
                    CSimple* Target = nullptr;
                    unsigned short TargetSub = SUBOBJ_ID_NONE;
                    static_cast<CEGun*>(Equip)->GetTarget(Target, TargetSub);
                    if (Target == nullptr)
                        continue;
                    // .text:06d080eb .. .text:06d08127 -- XSetTarget at S+0x50:
                    // +0x00 = cobj+0xb0, word +0x04 = equip+0x08,
                    // +0x08 = target+0xb0, word +0x0c = sub; the two pad words
                    // are never written. IClient::SendCommonSetTarget (CALL [EDX + 0x10]).
                    XSetTarget Msg;
                    Msg.ObjectId = Solar->Id;
                    Msg.SubObjId = Equip->Id;
                    Msg.Pad_0x06 = 0u;
                    Msg.TargetObjectId = Target->Id;
                    Msg.TargetSubObjId = TargetSub;
                    Msg.Pad_0x0e = 0u;
                    ::g_pClientInterface->SendCommonSetTarget(SessionId, Msg);
                }

                // .text:06d0814c .. .text:06d081d1 -- CALL [EAX + 0xb8]
                // (get_activate_state) into a fresh vector; on true, one
                // ACTIVATEOBJECT {cobj+0xb0, 1, index} per set entry through
                // IClient::SendServerActivateObject (CALL [EAX + 0xe0]).
                std::vector<bool> States;
                if (Solar->get_activate_state(States)) {
                    for (unsigned int Index = 0;
                         Index < static_cast<unsigned int>(States.size()); ++Index) {
                        if (!States[Index])
                            continue;
                        FPayloadServerActivateObject Activate;
                        Activate.ObjectId = Solar->Id;
                        Activate.bActivate = 1;
                        Activate.Pad_0x05[0] = 0;
                        Activate.Pad_0x05[1] = 0;
                        Activate.Pad_0x05[2] = 0;
                        Activate.Field_0x08 = Index;
                        ::g_pClientInterface->SendServerActivateObject(SessionId, Activate);
                    }
                }
                // .text:06d081da .. .text:06d0821f -- the vector and both FmtStr
                // are destroyed; JMP back to the next node.
            }
        }
    }

    // .text:06d082fa .. .text:06d08302 -- the SHIP block at +0x34/+0x38. The
    // branch is `TEST AL,AL / JZ LAB_06d08550`, so the JZ (the tracked-list-only
    // pass) is the NOT-single-player one.
    {
        FSystemListNode* Node2 = Ships.Head;
        while (Node2 != nullptr) {
            FSystemListNode* const Next = Node2->Next;
            GameObject* const Obj = Node2->Data;
            if (bSingle) {
                // .text:06d08308 .. .text:06d08474 -- SP eager CREATESHIP.
                CObject* const Raw = Obj->cobj();
                CShip* const Ship =
                    (Raw != nullptr &&
                     (static_cast<unsigned>(Raw->ObjectClass) & 0x503u) == 0x503u)
                        ? static_cast<CShip*>(Raw)
                        : nullptr;
                if (Ship != nullptr) {
                    CShipCreateInfo Info;
                    CShipCreateInfo_Fill(&Info, Ship);
                    // .text:06d0844a CALL FUN_06cef450 / store at Info+0xa8
                    Info.bMarkedBy =
                        Obj->is_marked_by(SessionId) ? 1u : 0u;
                    SystemPlayerView_CreateShipForPlayer(View, &Info, Obj);
                }
            } else if (!Obj->is_destroyed()) {
                // .text:06d08570  CALL [EDX + 0x160]   ; is_destroyed
                // .text:06d08578  JNZ                   ; skip when destroyed
                // .text:06d0857a  MOV EAX,[ESI + 0x8]  ; node->Data
                // .text:06d0857d  MOV ECX,[ESP + 0x1c] ; the live view
                // .text:06d08582  CALL FUN_06cf7140
                SystemPlayerView_AddTrackedObject(View, Obj);
            }
            Node2 = Next;
        }
    }

    // .text:06d084c4  PUSH -1 / .text:06d084c6 LEA ECX,[EDI + 0x48] /
    // .text:06d084c9 CALL FUN_06cf5130 -- the LOOT block at +0x48/+0x4c, counted
    // the same way, and BOTH branches return immediately when the count is zero
    // (.text:06d084e6 and .text:06d0866e).
    {
        FSystemListNode* const LootHead = Loot.Head;
        unsigned int const LootCount = Loot.count_type(0xffffffffu);
        if (LootCount == 0)
            return;

        if (bSingle) {
            // .text:06d084ec .. .text:06d0866a -- SP eager CREATELOOT.
            // Retail: operator_new(count*0x48), zero each record, seed
            // +0x44=1 / +0x45=0 / +0x46=0, fill+push every node, then
            // SystemPlayerView_CreateLootForPlayer. OwnerId stays 0 here
            // (observer's per-object path is the one that copies CLoot+0xe4).
            std::vector<GameObject*> LootObjs;
            LootObjs.reserve(LootCount);
            FServerLootDesc* const Descs = static_cast<FServerLootDesc*>(
                ::operator new(sizeof(FServerLootDesc) * LootCount));
            if (Descs == nullptr)
                return;
            unsigned int Filled = 0u;
            for (FSystemListNode* LootNode = LootHead; LootNode != nullptr;
                 LootNode = LootNode->Next) {
                if (Filled >= LootCount)
                    break;
                GameObject* const Obj = LootNode->Data;
                CObject* const Raw = Obj->cobj();
                CLoot* const Loot =
                    (Raw != nullptr &&
                     (static_cast<unsigned>(Raw->ObjectClass) & 0x803u) == 0x803u)
                        ? static_cast<CLoot*>(Raw)
                        : nullptr;
                FServerLootDesc* const Desc = &Descs[Filled];
                std::memset(Desc, 0, sizeof(*Desc));
                Desc->Field_0x44 = 1;   // .text:06d0853c (puVar10+0xf with base+8)
                ServerLootDesc_Fill(Desc, Loot);
                Desc->bMarkedBy = Obj->is_marked_by(SessionId) ? 1u : 0u;
                LootObjs.push_back(Obj);
                ++Filled;
            }

            FServerCreateLootRequest Req;
            Req.OwnerId = 0u;           // .text:06d084f8  uStack_1b0 = 0
            Req.Kind = 5u;              // .text:06d08507
            Req.bMarkedBy = 0;
            Req.bTemporary = 0;
            Req.Pad0a[0] = 0;
            Req.Pad0a[1] = 0;
            Req.Items = Descs;
            Req.Count = static_cast<unsigned char>(Filled);
            Req.Pad11[0] = 0;
            Req.Pad11[1] = 0;
            Req.Pad11[2] = 0;

            if (Filled > 0u) {
                SystemPlayerView_CreateLootForPlayer(
                    View, &Req, &LootObjs[0], &LootObjs[0] + LootObjs.size());
            }
            ::operator delete(Descs);
        } else {
            FSystemListNode* Node2 = LootHead;
            while (Node2 != nullptr) {
                FSystemListNode* const Next = Node2->Next;
                GameObject* const Obj = Node2->Data;
                if (!Obj->is_destroyed()) {
                    // .text:06d086a5  CALL [EDX + 0x160]
                    // .text:06d086b2  MOV ECX,[ESP + 0x1c]
                    // .text:06d086b7  CALL FUN_06cf7140
                    SystemPlayerView_AddTrackedObject(View, Obj);
                }
                Node2 = Next;
            }
        }
    }
}

// ===========================================================================
// ===========================================================================
// The per-player object tracker that hangs off StarSystem+0x0C.
// Structures and the byte trail are in server_sysdb.h.
// ===========================================================================

// The three squared radii FUN_06cf7260 tests against. Read as raw bytes off the
// listing, not guessed: .rdata:06d66aec = 20 bc be 4a = 0x4abebc20 = 6.25e6,
// .rdata:06d66af0 = 0x4bbebc20 = 2.5e7, .rdata:06d66af4 = 0x4cbebc20 = 1.0e8.
// They are 2500^2, 5000^2 and 10000^2.
static float const kViewRadiusSqUnowned = 6.25e6f;    // .rdata:06d66aec
static float const kViewRadiusSqStranger = 2.5e7f;    // .rdata:06d66af0
static float const kViewRadiusSqGroupMate = 1.0e8f;   // .rdata:06d66af4

// ---------------------------------------------------------------------------
// The mask FUN_06cf6820's third exit tests GameObject::get_type_bits against:
//   .text:06cf6905  ff9254010000  CALL dword ptr [EDX + 0x154]
//   .text:06cf690b  a9fffd0000    TEST EAX,0xfdff
//   .text:06cf6910  7417          JZ   LAB_06cf6929        ; silent return
//
// 0xFDFF is not an arbitrary number: it is every OBJECT_TYPE bit below 0x10000
// EXCEPT 0x200. The bit values are not guessed -- they come out of Common.dll's
// own name/value table at .data:063ec810, an array of 0xE8/8 = 29
// {const char*, unsigned int} pairs that both DecodeType (?DecodeType@@YAIPBD@Z,
// Common.dll 0x8f3d0, the `type =` ini handler's lookup at .text:062f06e4)
// walk with `ADD ESI,0x8` until `CMP EDI,0xe8`:
//
//   NONE 0x00000000   MOON 0x00000001   PLANET 0x00000002   SUN  0x00000004
//   BLACKHOLE 0x08    SATELLITE 0x10    DOCKING_RING 0x20   JUMP_GATE 0x40
//   TRADELANE_RING 0x80   STATION 0x100   WAYPOINT 0x200    AIRLOCK_GATE 0x400
//   JUMP_HOLE 0x800   WEAPONS_PLATFORM 0x1000   DESTROYABLE_DEPOT 0x2000
//   NON_TARGETABLE 0x4000   MISSION_SATELLITE 0x8000
//   FIGHTER 0x00010000   FREIGHTER 0x00020000   GUNBOAT 0x00040000
//   CRUISER 0x00080000   TRANSPORT 0x00100000   CAPITAL 0x00200000
//   MINING  0x00400000   GUIDED 0x01000000   BULLET 0x02000000
//   MINE    0x04000000   LOOT   0x10000000   ASTEROID 0x20000000
//
// OR the low-16 group together and drop WAYPOINT and you get exactly 0xFDFF.
// So the predicate reads "this is a SOLAR, and not a waypoint": the static
// furniture of the system, which a client instantiates from the system ini
// rather than from a server create-message. That is precisely why retail still
// sends the destroy notification for an object that was never in this player's
// tracked list -- the client has it anyway. Ships, projectiles, mines, loot and
// asteroids are all outside the mask and stay silent.
static unsigned int const kSolarTypeBits = 0x0000FDFFu;

// ---------------------------------------------------------------------------
// FObserverTrackedList -- Observer+0x08, retail's std::list<SystemTrackedObject*>.
// Its three members are the list's share of Observer's special members; see
// server_sysdb.h for the address ranges.
// ---------------------------------------------------------------------------

// .text:06cf5205 .. 06cf5217 (inside FUN_06cf51e0).
FObserverTrackedList::FObserverTrackedList()
{
    // .text:06cf5205 -- in retail the allocator byte is a copy of a leftover
    // argument byte; it carries no value, the port writes 0.
    Alloc = 0;
    // .text:06cf5208 -- operator new(0xc), then `MOV [EAX],EAX` /
    // `MOV [EAX+4],EAX`: a self-referencing sentinel, the empty list.
    SystemTrackedNode* const Sentinel = new SystemTrackedNode;
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    Head = Sentinel;                  // .text:06cf5214
    Size = 0;                         // .text:06cf5217
}

// .text:06d0f705 .. 06d0f778 (inside FUN_06d0f6d0).
FObserverTrackedList::FObserverTrackedList(FObserverTrackedList const& Source)
{
    Alloc = Source.Alloc;             // 06d0f705
    // 06d0f70c -- operator new(0xc), self-linked; size 0.
    SystemTrackedNode* const Sentinel = new SystemTrackedNode;
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    Head = Sentinel;
    Size = 0;

    // 06d0f740 .. 06d0f778 -- for every source node a new node linked before
    // the sentinel (FUN_06cfc340), its Data copied from the source node's Data
    // (FUN_06d06720 is a pointer copy -- the RECORD is shared, not
    // duplicated), ++size.
    SystemTrackedNode* const SrcSentinel = Source.Head;
    for (SystemTrackedNode* Src = SrcSentinel->Next; Src != SrcSentinel; Src = Src->Next) {
        SystemTrackedNode* const Node = new SystemTrackedNode;
        Node->Next = Sentinel;
        Node->Prev = Sentinel->Prev;
        Sentinel->Prev = Node;
        Node->Prev->Next = Node;
        Node->Data = Src->Data;
        ++Size;
    }
}

// .text:06cf538d .. 06cf53ca (the tail of FUN_06cf52d0). MSVC6's ~list():
// erase(begin(), end()) with FUN_06d0f620, free the sentinel, head = size = 0.
// ~Observer's body has already emptied the list, so the walk finds nothing --
// in retail as here.
FObserverTrackedList::~FObserverTrackedList()
{
    SystemTrackedNode* const Sentinel = Head;
    // .text:06cf5390 `MOV ESI,[EBP]` / .text:06cf5396 `CMP ESI,EBP / JZ`.
    SystemTrackedNode* Node = Sentinel->Next;
    while (Node != Sentinel) {
        // FUN_06d0f620 -- list::erase(node): unlink, free, --size.
        SystemTrackedNode* const Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        --Size;
        Node = Next;
    }
    delete Sentinel;                  // .text:06cf53bb
    Head = nullptr;                   // .text:06cf53c7
    Size = 0;                         // .text:06cf53ca
}

// ---------------------------------------------------------------------------
// FUN_06cf51e0 (.text:06cf51e0, thiscall, `RET 0x4`). Observer's constructor.
// Member construction in declaration order is retail's order:
//   .text:06cf5205 .. 06cf5217  +0x08 the tracked list (FObserverTrackedList)
//   .text:06cf5228 .. 06cf5278  +0x14 the _Tree: allocator bytes +0x14/+0x15,
//                               _Multi +0x20 = 0, _Nil (+0x1C) and _Head (+0x18)
//                               nodes, _Size +0x24 = 0 -- std::set's own ctor
//   .text:06cf527b .. 06cf5290  +0x28 = 0, the double +0x30 = 0 (two dword
//                               zeros), +0x38 = the argument, +0x3C..+0x44 = 0
//   .text:06cf5294              vptr = .rdata:06d66988 (the compiler's own store)
// +0x04 and +0x2C are not written by retail and are not written here.
// ---------------------------------------------------------------------------
Observer::Observer(unsigned int InPlayerId)
    : Tracked()
    , PendingDamageIds()
{
    bShipGone = 0;                    // .text:06cf527b
    ElapsedTime = 0.0;                // .text:06cf527e / 06cf5281
    PlayerId = InPlayerId;            // .text:06cf5284
    Position.x = 0.0f;                // .text:06cf528a
    Position.y = 0.0f;                // .text:06cf528d
    Position.z = 0.0f;                // .text:06cf5290
}

// ---------------------------------------------------------------------------
// FUN_06d0f6d0 (.text:06d0f6d0, cdecl (Dest, Source)) -- Observer's copy
// constructor, compiled as std::list's _Construct(p, v), i.e. a placement new.
// The `CMP EDI,EBP / JZ` at .text:06d0f6f4 is placement-new's null test on
// Dest; the port reaches this only through a placement new on a live list
// node. Callers: FUN_06d0f330 (.text:06d0f365, list::insert, from
// StarSystem::add_player) and FUN_06d109c0 (.text:06d10a08, StarSystem's copy
// constructor).
//   06d0f705 .. 06d0f778  +0x08 the tracked list (FObserverTrackedList copy)
//   06d0f77a .. 06d0f7df  +0x14 the _Tree: allocator bytes and _Multi copied,
//                         fresh _Nil/_Head, FUN_06ce37a0 = the tree copy --
//                         std::set's copy constructor
//   06d0f7e4 .. 06d0f80f  +0x28, the double at +0x30, +0x38 and the Vector at
//                         +0x3c copied
//   06d0f813              vptr = .rdata:06d66988
// +0x04 and +0x2c are not written.
// ---------------------------------------------------------------------------
Observer::Observer(Observer const& Source)
    : Tracked(Source.Tracked)
    , PendingDamageIds(Source.PendingDamageIds)
{
    bShipGone = Source.bShipGone;     // 06d0f7e4
    ElapsedTime = Source.ElapsedTime; // 06d0f7ea .. 06d0f7f3
    PlayerId = Source.PlayerId;       // 06d0f7f6
    Position = Source.Position;       // 06d0f7ff .. 06d0f810
}

// ---------------------------------------------------------------------------
// FUN_06cf52d0 (.text:06cf52d0). ~Observer. Slot 0 of .rdata:06d66988 is the
// compiler's scalar deleting destructor around it (FUN_06cf52b0: this body,
// then operator delete when flag & 1); every retail dispatch passes flag 0.
//   .text:06cf52f2          vptr restamp (the compiler's own store)
//   .text:06cf52f9 .. 532d  body, pass 1: every RECORD destroyed
//   .text:06cf532f .. 5353  body, pass 2: FUN_06d0f620 list::erase on every node
//   .text:06cf535d .. 5385  ~set on +0x14 (FUN_06cf7540 erase all, free _Head
//                           and _Nil, clear) -- the member declared last is
//                           destroyed first
//   .text:06cf538d .. 53ca  ~FObserverTrackedList on +0x08
// ---------------------------------------------------------------------------
Observer::~Observer()
{
    // (w4-server-guards) No null test on the list head: retail reads
    // [this + 0xc] and derefs it unconditionally (.text:06cf52f9 `MOV EAX,[EBP
    // + 0xc]` / .text:06cf52fc `MOV ESI,[EAX]`), and both constructors always
    // allocate the sentinel.
    SystemTrackedNode* const Sentinel = Tracked.Head;
    SystemTrackedNode* Node;
    // Pass 1, .text:06cf52f9 .. 06cf532d: every RECORD. .text:06cf52fe `CMP
    // ESI,EAX / JZ` and .text:06cf532a `CMP ESI,[EBP+0xc] / JNZ` -- the walk
    // stops only at the sentinel.
    for (Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        // `delete` runs ~CRemotePhysicsSimulation on the embedded member
        // (.text:06cf5317 LEA ECX,[EDI+0x8] / CALL) and frees the record
        // (.text:06cf5320). Retail's `TEST EDI,EDI / JZ` (.text:06cf5313) is
        // delete's own null test.
        delete Node->Data;
    }
    // Pass 2, .text:06cf532f .. 06cf5353: FUN_06d0f620 = list::erase(node) on
    // every node -- unlink, free, --size.
    Node = Sentinel->Next;
    while (Node != Sentinel) {
        SystemTrackedNode* const Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        --Tracked.Size;
        Node = Next;
    }
    // The members follow: ~PendingDamageIds (.text:06cf535d .. 06cf5385), then
    // ~Tracked (.text:06cf538d .. 06cf53ca).
}

// ---------------------------------------------------------------------------
// FUN_06cf7140 (.text:06cf7140, `RET 0x4`). The only producer the tracked-object
// list has in retail.
// ---------------------------------------------------------------------------
SystemTrackedObject* SystemPlayerView_AddTrackedObject(Observer* View, GameObject* Obj)
{
    // No null test on the view or on its list head: retail reads [this+0xc]
    // unconditionally at .text:06cf71b4, and every view this is called on has
    // been through an Observer constructor, both of which always allocate the
    // sentinel (.text:06cf5208 / .text:06cf5214, 06d0f70c).

    // .text:06cf715e  PUSH 0x2f8 / operator new; .text:06cf7181 constructs the
    // CRemotePhysicsSimulation in place at +0x08; .text:06cf7189 zeroes the
    // object pointer and .text:06cf718f the created-on-client byte. +0x04 is
    // never written (see SystemTrackedObject in server_sysdb.h).
    SystemTrackedObject* const Record = new SystemTrackedObject;
    Record->Object = nullptr;
    Record->bCreatedOnClient = false;

    // .text:06cf71a6  MOV [ESI],EBP -- the real object, stored after the zero.
    Record->Object = Obj;

    // .text:06cf71a8 / .text:06cf71ae -- the flag is seeded from SinglePlayer(),
    // not from false: in single player the client already has everything, so the
    // record counts as created the moment it exists.
    Record->bCreatedOnClient = SinglePlayer();

    // .text:06cf71b4 .. .text:06cf71de -- push_back onto the sentinel list.
    SystemTrackedNode* const Sentinel = View->Tracked.Head;
    SystemTrackedNode* const Node = new SystemTrackedNode;
    Node->Next = Sentinel;
    Node->Prev = Sentinel->Prev;
    Node->Data = Record;
    Sentinel->Prev->Next = Node;
    Sentinel->Prev = Node;

    // .text:06cf71e4  INC dword ptr [EDI + 0x10]
    ++View->Tracked.Size;

    // .text:06cf71e7 .. .text:06cf7237 -- outside single player, prime the
    // simulation with the object's current state. SinglePlayer() is called a
    // SECOND time here (.text:06cf71e7 / TEST AL,AL / JNZ .text:06cf723a), not
    // reused from the flag above.
    //   .text:06cf71f1  MOV EDX,[EBP] / CALL [EDX]       -> EBX: IObjInspect slot 0,
    //                                                       get_position()
    //   .text:06cf71fa  MOV EAX,[EBP] / CALL [EAX+0xc]   -> EBP: IObjInspect slot 3,
    //                                                       get_transform() (ICF-
    //                                                       folded with slot 4,
    //                                                       get_orientation, at
    //                                                       Common 0x062bf1f0; the
    //                                                       pointer is used as the
    //                                                       Matrix at Transform+0)
    //   .text:06cf7204 .. .text:06cf7212  DAT_06d8d7f8, the lazily cached
    //                                     "3DMathEngine" (FUN_06d5ff70 is
    //                                     DACOM_Acquire + QueryInterface)
    //   .text:06cf7220  CALL [ECX+0x50]   I3DMathEngine::MatrixToQuaternion
    //                                     (engine, &Local /*[ESP+0x18]*/, *EBP)
    //   .text:06cf7237  CALL [EAX+0x4]    ??_7CRemotePhysicsSimulation slot 1,
    //                                     RemoteUpdate(*EBX, Local,
    //                                     View->ElapsedTime) on record+0x08.
    // Stack check for the quaternion: the LEA at .text:06cf721a sits one PUSH
    // below the frame (ESP+0x1c -> frame+0x18) and the LEA at .text:06cf722e two
    // PUSHes below it after the callee-cleaned COM call returned (ESP+0x20 ->
    // frame+0x18): the same local. The two PUSHes at .text:06cf7223/06cf722a are
    // View+0x34 then View+0x30 -- one double, ElapsedTime.
    // The engine is DAT_06d8d7f8, Observer.cpp's file-static, which this
    // function shares with the observer update -- Observer_MatrixToQuat
    // (server_observer.cpp) is that lazy fetch through Server.dll's own
    // FUN_06d5ff70. Retail never checks the engine for null and neither does
    // this.
    if (!SinglePlayer())
    {
        Vector const& Position = Obj->get_position();          // slot 0
        Transform const& Xform = Obj->get_transform();         // slot 3
        Quaternion Orientation;
        Observer_MatrixToQuat(Orientation, Xform.Rotation);
        Record->Simulation.RemoteUpdate(Position, Orientation, View->ElapsedTime);
    }

    // .text:06cf723f  MOV EAX,ESI -- retail returns the record.
    return Record;
}

// FUN_06cf7260 (.text:06cf7260), `RET 0x4`.
bool SystemPlayerView_ShouldBeVisible(Observer* View, GameObject* Obj)
{
    // .text:06cf7265 / .text:06cf726a / .text:06cf7278 -- a null object and
    // single player are both unconditionally "visible"; the tracker is a
    // multiplayer-only filter. There is no test on the view: this is a
    // thiscall and EDI = ECX is dereferenced below.
    if (Obj == nullptr)
        return true;
    if (SinglePlayer())
        return true;

    // .text:06cf727e .. .text:06cf7292 -- the inlined CSimple accessor, with
    // retail's own shape: CBase::Object at +0x10, nulled unless
    // (ObjectClass & 3) == 3 (.text:06cf7290 `XOR EAX,EAX`), and then
    // CSimple::OwnerPlayer (+0xb4) read THROUGH the result with no test. Every
    // object a Observer tracks is a CSimple, so retail never reaches
    // the null read; the port does not guard it either.
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;

    // .text:06cf7298 -- your own ship is never filtered.
    if (OwnerPlayer == View->PlayerId)
        return true;

    // .text:06cf72a1 `MOV EAX,[ESI]` / .text:06cf72a5 `CALL [EAX]` -- the
    // wrapper's own vtable slot 0, IObjInspect::get_position, and then FLD off
    // the returned pointer (.text:06cf72a7 / 06cf72b1 / 06cf72b7).
    Vector const& ObjPos = Obj->get_position();

    // .text:06cf72a7 .. .text:06cf72d1 -- the squared distance, computed but
    // not yet compared; retail keeps it on the x87 stack across the group test.
    float const Dx = ObjPos.x - View->Position.x;
    float const Dy = ObjPos.y - View->Position.y;
    float const Dz = ObjPos.z - View->Position.z;
    float const DistSq = Dx * Dx + Dy * Dy + Dz * Dz;

    // .text:06cf72d3 .. .text:06cf72ee -- the owner is re-read through the
    // same inlined accessor; an object nobody owns gets the tightest radius.
    // Every compare is `FCOM/FCOMP` + `TEST AH,0x41` + `JP`, i.e. "visible when
    // DistSq <= radius" (C0 or C3 set), and a NaN (C0 = C2 = C3) is not.
    if (OwnerPlayer == 0)
        return DistSq <= kViewRadiusSqUnowned;           // .text:06cf7354

    // .text:06cf72f0 .. .text:06cf7328 -- the group widening.
    //   .text:06cf72f0  MOV  EDX,[EDI + 0x38]                ; this view's player
    //   .text:06cf72f3  MOV  ECX,[.data:Players]
    //   .text:06cf72f9  IMUL EDX,EDX,0x418
    //   .text:06cf72ff  MOV  EAX,[EDX + ECX*0x1 + -0x14]     ; my group
    //   .text:06cf7303  TEST EAX,EAX / JZ  -> stranger radius
    //   .text:06cf7318  MOV  EDX,[ESI + 0xb4]                ; the owner
    //   .text:06cf731e  IMUL EDX,EDX,0x418
    //   .text:06cf7324  CMP  EAX,[EDX + ECX*0x1 + -0x14]     ; his group
    //   .text:06cf7328  JNZ  -> stranger radius
    //   .text:06cf732a  FCOM [.rdata:06d66af4]               ; 10000^2
    // 0x418 - 0x14 = 0x404 = PlayerData::group_ptr, the CPlayerGroup*. Both
    // slots are indexed raw, with no PlayerDB::is_valid -- PlayerDB::operator[]
    // is the same raw `m_PlayerArray[Id - 1]`. A group mate outside 10000
    // falls through to the stranger test (.text:06cf7335 `JP LAB_06cf7340`),
    // which it then fails as well.
    void const* const MyGroup = Players[View->PlayerId].group_ptr;
    if (MyGroup != nullptr && MyGroup == Players[OwnerPlayer].group_ptr) {
        if (DistSq <= kViewRadiusSqGroupMate)
            return true;
    }
    return DistSq <= kViewRadiusSqStranger;              // .text:06cf7340
}

// Retail's inner-list search, shared by FUN_06cf73d0 (.text:06cf7410) and
// FUN_06cf6820 (.text:06cf6860). The two differ in what they match on:
// FUN_06cf73d0 compares the stored object POINTER against the candidate
// (.text:06cf7413 `CMP [ECX],ESI`), FUN_06cf6820 compares the stored object's
// get_id() against the candidate's (.text:06cf686a). For a live object those
// agree; the id form is what survives an object already being torn down, which
// is why the destroy path uses it.
static SystemTrackedNode* SystemPlayerView_FindByPointer(Observer* View, GameObject* Obj)
{
    // .text:06cf7405 .. .text:06cf741b -- the sentinel is always there (the
    // constructor allocates it) and every node's Data is a live record
    // (AddTrackedObject is the only producer), so retail tests neither:
    // `MOV ECX,[EAX + 0x8]` / `CMP [ECX],ESI`.
    SystemTrackedNode* const Sentinel = View->Tracked.Head;
    for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        if (Node->Data->Object == Obj)
            return Node;
    }
    return nullptr;
}

static SystemTrackedNode* SystemPlayerView_FindById(Observer* View, unsigned int ObjId)
{
    // (w4-server-guards) FUN_06cf6820's inlined search tests neither the
    // sentinel nor the record nor its object: .text:06cf684c `MOV EAX,[EDI+0xc]`
    // / 06cf6853 `MOV ESI,[EAX]`, then per node 06cf6860 `MOV EDX,[ESI+0x8]` /
    // 06cf6863 `MOV ECX,[EDX]` / 06cf6867 `CALL [EAX+0x20]` (get_id).
    SystemTrackedNode* const Sentinel = View->Tracked.Head;
    for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        if (Node->Data->Object->get_id() == ObjId)
            return Node;
    }
    return nullptr;
}

// FUN_06cf73d0 (.text:06cf73d0), `RET 0x4`.
bool SystemPlayerView_IsObjectVisible(Observer* View, GameObject* Obj)
{
    // .text:06cf73da / .text:06cf73e4 -- same two unconditional yeses. No test
    // on the view (thiscall, EDI = ECX).
    if (Obj == nullptr)
        return true;
    if (SinglePlayer())
        return true;

    // .text:06cf73e6 .. .text:06cf7400 -- your own ship again, through the
    // same inlined CSimple accessor as FUN_06cf7260: the `XOR EAX,EAX` null
    // path at .text:06cf73f8 is read through at .text:06cf73fa, unguarded.
    if (Obj->csimple()->OwnerPlayer == View->PlayerId)
        return true;

    // .text:06cf7405 .. .text:06cf7415 -- already tracked? then the answer is
    // the record's own "created on the client" byte, verbatim
    // (.text:06cf742a MOV AL,[ECX+0x2f0]).
    SystemTrackedNode* const Node = SystemPlayerView_FindByPointer(View, Obj);
    if (Node != nullptr)
        return Node->Data->bCreatedOnClient;

    // .text:06cf7420 -- not tracked: fall through to the range test.
    return SystemPlayerView_ShouldBeVisible(View, Obj);
}

// FUN_06cf7370 (.text:06cf7370, `RET 0x4`). PORTED 2026-09-24
// (claude-code:w5-server-06ce), 34 instructions:
//   .text:06cf7376  CALL SinglePlayer / JZ     -- single player: MOV AL,1 / RET
//   .text:06cf7389 .. 06cf7398  FUN_06d006c0(&ObjId, &obj, &sys) -- the lookup
//   .text:06cf73a4 .. 06cf73a9  miss: FUN_06cf73d0(view, 0)
//   .text:06cf73b5 .. 06cf73bc  hit:  FUN_06cf73d0(view, obj)
// FUN_06d006c0 is Server_LookupGameObject in this port (the substitution every
// other caller makes); it answers null on a miss, which is exactly the object
// the miss path hands on.
bool SystemPlayerView_IsObjectIdVisible(Observer* View, unsigned int ObjId)
{
    if (SinglePlayer())
        return true;
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    return SystemPlayerView_IsObjectVisible(View, Obj);
}

// FUN_06cf65b0 (.text:06cf65b0 .. 06cf6627, thiscall on the view, RET 4). PORTED
// 2026-09-24 (claude-code:w5-server-06d0). Only caller StarSystem::sp_obj_update
// (FUN_06d0bbc0, .text:06d0bc8f).
//   .text:06cf65b6 .. 06cf65be  SinglePlayer() -> return
//   .text:06cf65c0 .. 06cf65e3  the tracked list (+0x0c, sentinel re-read each
//                               step): the record whose Object (+0x00) answers
//                               get_id() (vt +0x20) == Rec->ObjectId; none -> return
//   .text:06cf65ec .. 06cf6603  bCreatedOnClient (+0x2f0): IClient +0xd8 (SendCommonUpdateObject)
//                               with (view +0x38, Rec)
//   .text:06cf6609 .. 06cf6620  Simulation (+0x08) vtable slot 1, RemoteUpdate(
//                               &Rec->Position, &Rec->Orientation, (double) the
//                               float at Rec +0x20)
void SystemPlayerView_SendObjectUpdate(Observer* View, FCommonUpdateObjectInfo const* Rec)
{
    if (SinglePlayer())
        return;

    for (SystemTrackedNode* Node = View->Tracked.Head->Next; Node != View->Tracked.Head; Node = Node->Next)
    {
        SystemTrackedObject* const Tracked = Node->Data;
        if (Tracked->Object->get_id() != Rec->ObjectId)
            continue;

        if (Tracked->bCreatedOnClient)
            ::g_pClientInterface->SendCommonUpdateObject(View->PlayerId, *Rec);

        Tracked->Simulation.RemoteUpdate(Rec->Position, Rec->Orientation,
                                         static_cast<double>(Rec->Field_0x20));
        return;
    }
}

// FUN_06cf70c0 (.text:06cf70c0 .. .text:06cf7138), thiscall on the view, (ObjId),
// `RET 0x4`. Only caller FUN_06d0f1c0 (StarSystem_SendGroupChange below). Tells
// this view's client the group id now stamped on a player ship:
//   .text:06cf70d6  FUN_06d006c0(&ObjId, &obj, &owner) -- a miss (AL == 0) or a
//                   null obj (.text:06cf70e6) returns
//   .text:06cf70ed  FUN_06cf73d0(view, obj) -- not visible to this view: return
//   .text:06cf70fa  obj->cobject() (vt +0x150); null returns
//   .text:06cf7104  (ObjectClass & 0x503) != 0x503 (not a CShip) returns
//   .text:06cf7115  the ship's +0x1b4 dword -- the group id CPlayerGroup::AddMember
//                   / DelMember store there (.text:06d5b8a5 / .text:06d5bb1b)
//   .text:06cf712d  IClient +0x1c4 (SendServerMiscObjUpdateObjectGroup)(view->PlayerId
//                   [+0x38], ObjId, group)
// No interface null test. FUN_06d006c0 is Server_LookupGameObject in this port;
// its null on a miss covers both of retail's early-outs.
void SystemPlayerView_SendGroupChange(Observer* View, unsigned int ObjId)
{
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    if (Obj == nullptr)
        return;
    if (!SystemPlayerView_IsObjectVisible(View, Obj))
        return;
    CObject const* const Cobj = Obj->cobject();
    if (Cobj == nullptr)
        return;
    if ((static_cast<unsigned int>(Cobj->ObjectClass) & 0x503u) != 0x503u)
        return;
    unsigned int const GroupId = static_cast<CShip const*>(Cobj)->GroupId;
    g_pClientInterface->SendServerMiscObjUpdateObjectGroup(View->PlayerId, ObjId, GroupId);
}

// FUN_06d0f1c0 (.text:06d0f1c0 .. .text:06d0f1e5), thiscall on the StarSystem,
// (ObjId), `RET 0x4`: FUN_06cf70c0(&node->View, ObjId) for every node of the
// player list at +0x0c, first to last, re-reading the head each round
// (.text:06d0f1dd). Callers CPlayerGroup::AddMember (.text:06d5b8d6) and
// DelMember (.text:06d5bb3e).
void StarSystem_SendGroupChange(StarSystem& System, unsigned int ObjId)
{
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(System.PlayerListHead);
    for (SystemPlayerNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        SystemPlayerView_SendGroupChange(&Node->View, ObjId);
}

// FUN_06cf6730 (.text:06cf6730 .. .text:06cf6818), thiscall on the view, (Obj),
// `RET 0x4`. Ported 2026-09-24 (claude-code:w5-server-06cf):
//   06cf674d .. 06cf676b  walk the tracked list at +0x0C; a record whose +0x00
//                         object is Obj ends the walk (06cf6765), running off the
//                         end returns (06cf676d)
//   06cf6781 .. 06cf678e  the record's +0x2F0 (bCreatedOnClient) clear -> out
//   06cf6790 .. 06cf67a6  a local std::vector<EquipDesc> -- the allocator byte is
//                         the low byte of the argument, _First/_Last/_End zero
//                         (EquipDescVector's own zeroing constructor here)
//   06cf67aa .. 06cf67c9  Obj->cobject() (IObjInspect +0x150); narrowed to CShip
//                         with (ObjectClass & 0x503) == 0x503, else 0
//   06cf67cb .. 06cf67d4  ship->vtable[+0xac](&vector) -- CEqObj::get_equip_desc_list
//                         (CShip's vftable .rdata:0639c02c + 0xac = 0639c0d8). NO null
//                         test on the narrowed pointer: retail calls through it.
//   06cf67da .. 06cf67f4  IClient +0x154 (SendServerRemoteObjectCargoUpdate)(&view->PlayerId [+0x38],
//                         ship->Id [+0xb0], &vector, 1) -- the first argument is the
//                         ADDRESS of the id (rpclocal's REMOTEOBJECTCARGOUPDATE sender
//                         dereferences it, rpc_payload_ch2.cpp)
//   06cf67fa .. 06cf6804  operator delete(vector._First) -- the vector's destructor
void SystemPlayerView_SendRemoteCargoUpdate(Observer* View, GameObject* Obj)
{
    SystemTrackedNode* const Head = View->Tracked.Head;
    SystemTrackedNode* Node = Head->Next;
    for (; Node != Head; Node = Node->Next)
    {
        if (Node->Data->Object == Obj)
            break;
    }
    if (Node == Head)
        return;
    if (!Node->Data->bCreatedOnClient)
        return;

    EquipDescVector Items;
    CObject const* const Cobj = Obj->cobject();
    CShip const* Ship = nullptr;
    if (Cobj != nullptr && (static_cast<unsigned int>(Cobj->ObjectClass) & 0x503u) == 0x503u)
        Ship = static_cast<CShip const*>(Cobj);
    Ship->get_equip_desc_list(Items);
    g_pClientInterface->SendServerRemoteObjectCargoUpdate(View->PlayerId, Ship->Id, Items, 1u);
}

// FUN_06d0b110 (.text:06d0b110 .. .text:06d0b154), thiscall on the StarSystem, (Obj),
// `RET 0x4`. Ported 2026-09-24 (claude-code:w5-server-06cf):
//   06d0b113 .. 06d0b11b  first = [[this+0x0C]]; an empty list returns
//   06d0b122 .. 06d0b134  per node: Obj's CBase::Object (+0x10) under the
//                         (ObjectClass & 3) == 3 guard, else 0 -- csimple()
//   06d0b136 .. 06d0b13f  CSimple::OwnerPlayer [+0xb4], read with no null test,
//                         == the node's PlayerId [node+0x40 = view+0x38] -> skip
//   06d0b141 .. 06d0b145  FUN_06cf6730(&node->View, Obj)
//   06d0b14a .. 06d0b14f  next; the head is re-read each round
void StarSystem_SendRemoteCargoUpdate(StarSystem& System, GameObject* Obj)
{
    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(System.PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(System.PlayerListHead); Node = Node->Next)
    {
        if (Obj->csimple()->OwnerPlayer != Node->View.PlayerId)
            SystemPlayerView_SendRemoteCargoUpdate(&Node->View, Obj);
    }
}

// FUN_06cf7440 (.text:06cf7440), `RET 0x4`. Its only caller is
// FUN_06d0f190 below, i.e. IServerImpl::SubmitChat's local-chat branch.
void SystemPlayerView_CollectCreatedOwners(Observer* View, std::set<unsigned int>* Owners)
{
    // .text:06cf7443 .. .text:06cf7452 -- head = [View+0xc], first = [head];
    // the loop re-reads [View+0xc] as its end every round (.text:06cf7527).
    for (SystemTrackedNode* Node = View->Tracked.Head->Next; Node != View->Tracked.Head;
         Node = Node->Next) {
        SystemTrackedObject* const Record = Node->Data;        // .text:06cf7464
        GameObject* const Obj = Record->Object;                // .text:06cf7467
        if (Obj == nullptr)                                    // .text:06cf746b
            continue;
        // .text:06cf7471 .. .text:06cf7485 -- CBase::Object / (ObjectClass & 3)
        // == 3 narrowing, then CSimple::OwnerPlayer at +0xb4 read with NO null
        // check: retail dereferences 0+0xb4 for a non-CSimple object, so it only
        // ever tracks CSimples here. Not guarded, as retail is not.
        unsigned int const Owner = Obj->csimple()->OwnerPlayer;
        if (Owner == 0)                                        // .text:06cf748b
            continue;
        if (!Record->bCreatedOnClient)                         // .text:06cf7497
            continue;
        // .text:06cf74a5 .. .text:06cf7518 -- the inlined std::set insert
        // (lower-bound walk, then FUN_06cf7770 _Insert unless the key exists).
        Owners->insert(Owner);
    }
}

// FUN_06d0f190 (.text:06d0f190), `RET 0x8`, thiscall on the StarSystem.
void StarSystem_CollectCreatedOwners(StarSystem* System, unsigned int PlayerId,
                                     std::set<unsigned int>* Owners)
{
    // .text:06d0f190 / .text:06d0f193 -- head = [this+0xc], node = [head].
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(System->PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next) {
        // .text:06d0f1a0 -- CMP [node+0x40],id; node+0x40 == View.PlayerId.
        if (Node->View.PlayerId == PlayerId) {
            // .text:06d0f1b3 -- LEA ECX,[node+0x8] / CALL FUN_06cf7440.
            SystemPlayerView_CollectCreatedOwners(&Node->View, Owners);
            return;
        }
    }
}

// FUN_06cf6820 (.text:06cf6820), `RET 0x8`.
//
// Returns nothing in retail; the bool here says whether the record was found
// and dropped, which the callers ignore, exactly as retail does.
bool SystemPlayerView_OnObjectDestroyed(Observer* View, GameObject* Obj, int Kind)
{
    // (w4-server-guards) No null test on View or Obj: thiscall, and
    // .text:06cf6828 `MOV EAX,[EBP]` reads Obj's vtable before anything else.

    // .text:06cf6831 .. .text:06cf684f -- the two-dword block retail passes to
    // IClient::SendServerDestroyObject by address, built BEFORE the search and reused on
    // every exit: { Obj->get_id(), Kind }. It is rpc_iclient_args.h's
    // FServerDestroyObjectInfo (the sender reads the dword and the byte at +0x04); retail
    // stores Kind as a whole dword, so its upper three bytes land in the pad.
    FServerDestroyObjectInfo Wire;
    Wire.ObjectId = Obj->get_id();
    unsigned int const KindDword = (unsigned int)Kind;
    Wire.Field_0x04 = static_cast<unsigned char>(KindDword & 0xffu);
    Wire.Pad_0x05[0] = static_cast<unsigned char>((KindDword >> 8) & 0xffu);
    Wire.Pad_0x05[1] = static_cast<unsigned char>((KindDword >> 16) & 0xffu);
    Wire.Pad_0x05[2] = static_cast<unsigned char>((KindDword >> 24) & 0xffu);

    SystemTrackedNode* const Node = SystemPlayerView_FindById(View, Wire.ObjectId);

    if (Node == nullptr) {
        // .text:06cf6877 .. .text:06cf6894 -- not tracked. Retail still tells
        // the client when the object belonged to this very player...
        // (w4-server-guards) .text:06cf6889 `XOR EAX,EAX` then 06cf688e
        // `CMP [EAX+0xb4],EDX`: retail reads OwnerPlayer through the narrowed
        // pointer with no null test, like SystemPlayerView_ShouldBeVisible.
        unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;
        if (OwnerPlayer == View->PlayerId) {
            // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
            g_pClientInterface->SendServerDestroyObject(View->PlayerId, Wire);
            return false;
        }

        // .text:06cf6900 .. .text:06cf6910 -- ...and otherwise asks the object
        // itself, through vtable +0x154 == GameObject::get_type_bits, and
        // notifies only when the answer has a bit inside 0xfdff set. That mask
        // is "any solar type except WAYPOINT"; see kSolarTypeBits above for the
        // Common.dll table it was read out of. Such objects exist on the client
        // whether or not this player ever tracked them, which is why retail
        // still sends the notification.
        if ((Obj->get_type_bits() & kSolarTypeBits) != 0) {
            // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
            g_pClientInterface->SendServerDestroyObject(View->PlayerId, Wire);
        }
        return false;
    }

    // .text:06cf689d .. .text:06cf68a8 -- tracked. The notification goes out
    // only when the client was ever told the object exists.
    // (w4-server-guards) 06cf689d `MOV ECX,[ESI+0x8]` / 06cf68a0
    // `MOV AL,[ECX+0x2f0]` -- the record is read with no null test here; the
    // only null test is the one before the free below (06cf68c4).
    SystemTrackedObject* const Record = Node->Data;
    if (Record->bCreatedOnClient) {
        // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
        g_pClientInterface->SendServerDestroyObject(View->PlayerId, Wire);
    }

    // .text:06cf68c1 .. .text:06cf68d5 -- destroy the record's
    // CRemotePhysicsSimulation (FUN_06cf6940 is `ADD ECX,8` then a tail jump
    // into COMMON.DLL::CRemotePhysicsSimulation::~CRemotePhysicsSimulation)
    // and free the record.
    if (Record != nullptr) {
        // Record::Simulation is a real CRemotePhysicsSimulation by value now, so
        // `delete` runs its destructor -- which is exactly what FUN_06cf6940
        // (`ADD ECX,0x8` + tail jump into ~CRemotePhysicsSimulation) does before
        // retail frees the block at .text:06cf68d0.
        delete Record;
        Node->Data = nullptr;
    }

    // .text:06cf68d8 .. .text:06cf68f4 -- unlink, free the node, size--.
    Node->Prev->Next = Node->Next;
    Node->Next->Prev = Node->Prev;
    delete Node;
    // (w4-server-guards) 06cf68ed `MOV EAX,[EDI+0x10]` / 06cf68f3 `DEC EAX` /
    // 06cf68f4 store -- unconditional; a found node means size >= 1.
    View->Tracked.Size--;
    return true;
}

// ===========================================================================
// The five "create this object on that player's client" notifiers, and six of
// the seven SystemPlayerView_AddTrackedObject call sites. Declared in
// server_sysdb.h, where the FDUMP-line / IClient-slot cross-check that names
// them lives.
//
// Retail's own source file for all five is
// E:\FL\Scratch\Source\Server\space\Observer.cpp (.rdata:06d669dc, pushed at
// .text:06cf66d7 / .text:06cf6c39 / .text:06cf6cda / .text:06cf6dd9) -- NOT
// StarSys.cpp, which is where add_cargo and the system itself come from. The
// class is retail's per-player Observer (RTTI .?AVObserver@@ at .data:06d88858
// names it too); the free functions keep their SystemPlayerView_ prefix.
// ===========================================================================

static char const kObserverCppPath[] =
    "E:\\FL\\Scratch\\Source\\Server\\space\\Observer.cpp";

// FUN_06cf6630 (.text:06cf6630, `RET 0x8`) -- CREATESHIP, IClient +0x0bc.
//
// The odd one out of the five. It does NOT consult
// SystemPlayerView_ShouldBeVisible and it does NOT set bCreatedOnClient; it
// sends only in SINGLE player (.text:06cf667c CALL SinglePlayer /
// .text:06cf6684 `JZ LAB_06cf66e7`, the silent exit), which is consistent
// because AddTrackedObject already seeds bCreatedOnClient from SinglePlayer().
// In multiplayer it therefore only registers the ship and lets the per-frame
// range check send the create later.
void SystemPlayerView_CreateShipForPlayer(Observer* View, CShipCreateInfo* Info, GameObject* Obj)
{
    // No test of View or Obj: ESI = ECX and EDI = the second stack argument are
    // dereferenced as they come (.text:06cf6632 / .text:06cf6636).
    //
    // .text:06cf6636 .. .text:06cf664c -- the usual `(ObjectClass & 3) == 3`
    // guard on CBase::Object, then CSimple::OwnerPlayer at +0xb4 read through
    // the result with no test (.text:06cf664a `XOR EAX,EAX` falls straight
    // into .text:06cf664c `MOV EDX,[EAX + 0xb4]`). Every object reaching here
    // is a ship, so retail never takes the null read; the port does not guard
    // it either.
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;

    // .text:06cf6652  CMP EDX,[ESI + 0x38] / .text:06cf6655 JZ -- a player is
    // never told to create their own ship.
    if (OwnerPlayer == View->PlayerId)
        return;

    // .text:06cf665b  MOV AL,[ESI + 0x28] / TEST AL,AL / JNZ
    if (View->bShipGone != 0)
        return;

    // .text:06cf666a  CALL [EAX + 0x160] -- is_destroyed.
    if (Obj->is_destroyed()) {
        // .text:06cf66c0 .. .text:06cf66e4 -- level 0x100002, line 0x19b, and
        // the object's own get_id() (vtable +0x20) as the third argument.
        // .text:06cf66cd MOV EAX,[->DACOM.DLL::FDUMP] / .text:06cf66e2 CALL [EAX]
        // -- the import is called with no null test (dacom seeds it with
        // DefaultFdump).
        FDUMP(0x100002, "%s(%d) : *** WARNING: Ship(%d) 'dead' before creation packet sent.",
              kObserverCppPath, 0x19b, Obj->get_id());
        return;
    }

    // .text:06cf6677  CALL FUN_06cf7140 -- the return value is discarded here,
    // unlike in the other four.
    SystemPlayerView_AddTrackedObject(View, Obj);

    if (!SinglePlayer())
        return;

    // .text:06cf6686 .. .text:06cf66a0 -- retail copies Players[PlayerId]+0x318
    // into the payload at +0xd4 before sending. Raw bytes:
    //   .text:06cf6686  8b4e38        MOV  ECX,[ESI + 0x38]        ; PlayerId
    //   .text:06cf6689  8b156002d906  MOV  EDX,[.data:Players]
    //   .text:06cf668f  69c918040000  IMUL ECX,ECX,0x418
    //   .text:06cf6695  8b8c1100ffffff MOV ECX,[ECX + EDX*1 + -0x100]
    //   .text:06cf669c  8b44240c      MOV  EAX,[ESP + 0xc]         ; the PAYLOAD,
    //                                 ; i.e. the FIRST stack argument, not the
    //                                 ; second -- ESP is entry-8 here
    //   .text:06cf66a0  8988d4000000  MOV  [EAX + 0xd4],ECX
    // and 0x418 - 0x100 == 0x318.
    //
    // CORRECTED 2026-09-17 (wave 13). The wave-12 note said "PlayerData+0x318
    // is not a field this port has identified". It is: PlayerData::rep_vibe,
    // documented at -0x100 in common_server_playerdb.h and written through
    // `ADD ECX,0x318` in this very file (see the Reputation::Vibe::GetName
    // call site). The same dword goes into the CREATESOLAR payload at +0xb0
    // through the identical `id*0x418 - 0x100 + Players` expression
    // (.text:06d0a63b region), which is the cross-check.
    //
    // +0xd4 is CShipCreateInfo::VibeOtherGroupId (src/include/server_createinfo.h),
    // the record rpclocal's CREATESHIP marshaller reads through the same
    // declaration. Retail stores unconditionally; the only caller hands in the
    // address of its own stack record, so there is no null case to test. The
    // slot is indexed raw (PlayerDB::operator[] is the same unchecked
    // `m_PlayerArray[Id - 1]`), with no PlayerDB::is_valid.
    Info->VibeOtherGroupId = Players[View->PlayerId].rep_vibe;

    // .text:06cf66a6 .. .text:06cf66b3 -- IClient vtable +0xbc, and the player
    // id goes by VALUE (.text:06cf66af `MOV EAX,[ESI + 0x38]` / `PUSH EAX`).
    // DAT_06d8fb14 is loaded with no test (.text:06cf66a6).
    g_pClientInterface->SendServerCreateShip(View->PlayerId, *Info);
}

// FUN_06cf6bc0 (.text:06cf6bc0, `RET 0x8`) -- CREATECOUNTER, IClient +0x0cc.
void SystemPlayerView_CreateCounterForPlayer(Observer* View, void* Payload, GameObject* Obj)
{
    // (w4-server-guards) No null test on View or Obj: .text:06cf6bc5 reads
    // [EDI + 0x28] first and 06cf6bd0 `MOV EAX,[ESI]` reads Obj's vtable.

    // .text:06cf6bc5  MOV AL,[EDI + 0x28] / TEST AL,AL / JNZ
    if (View->bShipGone != 0)
        return;

    // .text:06cf6bd4  CALL [EAX + 0x160]
    if (Obj->is_destroyed()) {
        // .text:06cf6c22 .. .text:06cf6c46 -- level 0x100002, line 0x23c.
        // 06cf6c2f `MOV EAX,[->DACOM.DLL::FDUMP]` / 06cf6c44 `CALL [EAX]` --
        // the import is called with no null test.
        FDUMP(0x100002, "%s(%d) : *** WARNING: CounterMeasure(%d) 'dead' before creation packet sent.",
              kObserverCppPath, 0x23c, Obj->get_id());
        return;
    }

    // .text:06cf6be1  CALL FUN_06cf7140, and the record is KEPT this time.
    SystemTrackedObject* const Record = SystemPlayerView_AddTrackedObject(View, Obj);

    // .text:06cf6be8 .. .text:06cf6bfc -- outside single player the object has
    // to be in range before the client is told about it at all.
    if (!SinglePlayer() && !SystemPlayerView_ShouldBeVisible(View, Obj))
        return;

    // .text:06cf6c02  MOV byte ptr [EBX + 0x2f0],0x1 -- no test on EBX, the
    // record FUN_06cf7140 just allocated.
    Record->bCreatedOnClient = true;

    // .text:06cf6c09 .. .text:06cf6c16 -- IClient vtable +0xcc. NOTE the first
    // argument: .text:06cf6c12 is `ADD EDI,0x38` and .text:06cf6c15 `PUSH EDI`,
    // so this slot is handed the ADDRESS of View->PlayerId, not its value --
    // unlike the ship / loot / mine slots, which push `[EDI + 0x38]`:
    // IClient::SendServerCreateCounter takes the id as `const unsigned int&`.
    // Payload is the caller's FServerCreateProjectileInfo (server_createinfo.h).
    // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
    g_pClientInterface->SendServerCreateCounter(
        View->PlayerId, *static_cast<const FServerCreateProjectileInfo*>(Payload));
}

// FUN_06cf6d60 (.text:06cf6d60, `RET 0x8`) -- CREATEMINE, IClient +0x0c4.
// Byte-for-byte the countermeasure one with a different FDUMP line, a different
// slot, and the player id passed BY VALUE (.text:06cf6db2 `MOV EAX,[EDI+0x38]`).
void SystemPlayerView_CreateMineForPlayer(Observer* View, void* Payload, GameObject* Obj)
{
    // (w4-server-guards) No null test on View or Obj (.text:06cf6d65 reads
    // [EDI + 0x28], 06cf6d70 `MOV EAX,[ESI]`), none on FDUMP (06cf6dcf .. the
    // import is called through directly), none on the record (06cf6da2).
    if (View->bShipGone != 0)                       // .text:06cf6d65
        return;

    if (Obj->is_destroyed()) {                      // .text:06cf6d74
        // .text:06cf6dc2 .. .text:06cf6de6 -- level 0x100002, line 0x28a.
        FDUMP(0x100002, "%s(%d) : *** WARNING: Mine(%d) 'dead' before creation packet sent.",
              kObserverCppPath, 0x28a, Obj->get_id());
        return;
    }

    SystemTrackedObject* const Record =
        SystemPlayerView_AddTrackedObject(View, Obj);   // .text:06cf6d81

    if (!SinglePlayer() && !SystemPlayerView_ShouldBeVisible(View, Obj))
        return;                                      // .text:06cf6d88 / 06cf6d95

    Record->bCreatedOnClient = true;                 // .text:06cf6da2

    // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
    // Payload is the caller's FServerCreateProjectileInfo (server_createinfo.h).
    g_pClientInterface->SendServerCreateMine(
        View->PlayerId, *static_cast<const FServerCreateProjectileInfo*>(Payload));
}

// FUN_06cf6df0 (.text:06cf6df0, `RET 0x4`) -- EXPLODEASTEROIDMINE, IClient
// +0x0d0. Only caller StarSystem::send_mine_explosion (.text:06d092a9).
//   .text:06cf6df9  id = Rec->MineId (kept in a local)
//   .text:06cf6e01  SinglePlayer() -> straight to the send
//   .text:06cf6e1a  FUN_06d006c0(&id, &obj, &sys); on failure obj = 0
//                   (.text:06cf6e26, redundant after the lookup's own clear)
//   .text:06cf6e35  FUN_06cf73d0(view, obj) == 0 -> return, nothing sent
//   .text:06cf6e3e  DAT_06d8fb14->vtable[+0xd0](view->PlayerId (+0x38), Rec)
// Neither the IClient pointer nor the record is tested.
void SystemPlayerView_SendMineExplosion(Observer* View, FMineExplosion* Rec)
{
    unsigned int const MineId = Rec->MineId;
    if (!SinglePlayer()) {
        // FUN_06d006c0 is the port's registry lookup (server_pub.cpp,
        // Server_LookupGameObject) -- the same substitution every other
        // FUN_06d006c0 caller in this port makes.
        GameObject* const Obj = Server_LookupGameObject(MineId);
        if (!SystemPlayerView_IsObjectVisible(View, Obj))
            return;
    }
    g_pClientInterface->SendServerExplodeAsteroidMine(View->PlayerId, *Rec);
}

// FUN_06cf66f0 (.text:06cf66f0 .. .text:06cf672a), thiscall on the view,
// (Info, Obj), `RET 0x8`. Ported 2026-09-24 (claude-code:w5-server-06cf):
//   06cf66f3 .. 06cf66f8  view+0x28 (bShipGone) set -> return
//   06cf66fa .. 06cf6704  FUN_06cf7260(view, Obj) -- SystemPlayerView_ShouldBeVisible
//   06cf670a .. 06cf6714  not visible: Info+0xac byte clear -> return
//   06cf6716 .. 06cf6723  IClient +0xb8 (SendServerCreateSolar)(view->PlayerId [+0x38], Info)
// Unlike the ship/loot/guided/mine notifiers there is no FUN_06cf7140 here: a
// solar never gets a tracked record. No IClient null test.
void SystemPlayerView_CreateSolarForPlayer(Observer* View, CSolarCreateInfo* Info, GameObject* Obj)
{
    if (View->bShipGone != 0)
        return;
    if (!SystemPlayerView_ShouldBeVisible(View, Obj) && Info->Field_0xac == 0)
        return;
    g_pClientInterface->SendServerCreateSolar(View->PlayerId, *Info);
}

// FUN_06cf6ac0 (.text:06cf6ac0 .. .text:06cf6bb4), thiscall on the view, 21 stack
// dwords (RET 0x54): SenderId, ReceiverId, VoiceId, the 13-dword Costume by value,
// CommType, TextIds, TextCount, Mood, Duration. Ported 2026-09-24
// (claude-code:w5-server-06cf):
//   06cf6ad7 .. 06cf6b0c  SinglePlayer() clear: FUN_06d006c0(&SenderId, &obj, &sys),
//                         a miss zeroes obj; FUN_06cf73d0(view, obj) false -> return
//   06cf6b1a .. 06cf6b4d  SinglePlayer() (called again) clear: the same for ReceiverId
//   06cf6b4f .. 06cf6b64  ReceiverId == Players[view->PlayerId].ship_object_id
//                         [slot-0x34] -> SETZ CL (only the low byte is set; the rest of
//                         ECX is PlayerId*0x418 -- rpclocal's SENDCOMM marshaller reads
//                         A22 & 0xff, so the port passes 0/1)
//   06cf6b67 .. 06cf6ba7  IClient +0x110 (SendServerSendComm)(view->PlayerId, SenderId, ReceiverId,
//                         VoiceId, Costume[0..12], CommType, TextIds, TextCount, Mood,
//                         Duration bits, that flag). No IClient null test.
void SystemPlayerView_SendComm(Observer* View, unsigned int SenderId,
                               unsigned int ReceiverId, unsigned int VoiceId,
                               Costume const& Cost, unsigned int CommType,
                               unsigned int* TextIds, int TextCount, unsigned int Mood,
                               float Duration)
{
    if (!SinglePlayer()) {
        GameObject* SenderObj = nullptr;
        StarSystem* SenderSystem = nullptr;
        if (!Server_FindObject(SenderId, SenderObj, SenderSystem))       // .text:06cf6aee
            SenderObj = nullptr;
        if (!SystemPlayerView_IsObjectVisible(View, SenderObj))          // .text:06cf6b05
            return;
    }
    if (!SinglePlayer()) {
        GameObject* ReceiverObj = nullptr;
        StarSystem* ReceiverSystem = nullptr;
        if (!Server_FindObject(ReceiverId, ReceiverObj, ReceiverSystem)) // .text:06cf6b2f
            ReceiverObj = nullptr;
        if (!SystemPlayerView_IsObjectVisible(View, ReceiverObj))        // .text:06cf6b46
            return;
    }

    bool const bReceiverIsOwnShip =
        ReceiverId == Players[View->PlayerId].ship_object_id;            // .text:06cf6b60
    // The Costume goes by value -- the 13-dword copy is .text:06cf6b97 REP MOVSD.
    static_assert(sizeof(Costume) == 13 * sizeof(unsigned int), "Costume is 13 dwords");
    g_pClientInterface->SendServerSendComm(View->PlayerId, SenderId, ReceiverId, VoiceId,
                                           Cost, CommType, TextIds, TextCount, Mood, Duration,
                                           bReceiverIsOwnShip);         // .text:06cf6ba7
}

// FUN_06cf6c50 (.text:06cf6c50, `RET 0x8`) -- CREATELOOT, IClient +0x0c0.
// The only one of the five that tracks MORE THAN ONE object per call: it walks
// the whole [First, Last) range and sends a single packet afterwards.
void SystemPlayerView_CreateLootForPlayer(Observer* View, FServerCreateLootRequest* Payload,
                                          GameObject* const* First, GameObject* const* Last)
{
    // (w4-server-guards) No null test on View (.text:06cf6c55 reads
    // [EDI + 0x28] first), none on First (the only test is 06cf6c63
    // `CMP ESI,[EBX + 0x8]`; a vector's null begin comes with a null end),
    // none on an element (06cf6c68 `MOV ECX,[ESI]` / 06cf6c6a `MOV EAX,[ECX]`),
    // none on FUN_06cf6c50's FDUMP import (06cf6cd0 .. called through
    // directly) and none on the payload (06cf6c85 stores unconditionally).

    // .text:06cf6c55  MOV AL,[EDI + 0x28] / TEST AL,AL / JNZ
    if (View->bShipGone != 0)
        return;

    // .text:06cf6c60  MOV ESI,[EBX + 0x4] / .text:06cf6c63 CMP ESI,[EBX + 0x8]
    // -- the vector's first/last pair; an empty range skips straight to the
    // send, it does not return.
    for (GameObject* const* It = First; It != Last; ++It) {
        GameObject* const Obj = *It;                 // .text:06cf6c68

        // .text:06cf6c6c  CALL [EAX + 0x160]. Retail ABANDONS the whole call on
        // the first dead entry -- .text:06cf6c74 `JNZ LAB_06cf6cc3` leaves via
        // the FDUMP and never reaches the send. It is not a `continue`.
        if (Obj->is_destroyed()) {
            // .text:06cf6cc3 .. .text:06cf6ce7 -- level 0x100001 (ERROR, not
            // WARNING like the other three), line 0x256.
            FDUMP(0x100001, "%s(%d) : *** ERROR: Loot(%d) 'dead' before creation packet sent.",
                  kObserverCppPath, 0x256, Obj->get_id());
            return;
        }

        // .text:06cf6c76 .. .text:06cf6c85 -- retail asks FUN_06cef450 "is this
        // view's player id in the object's own list at GameObject+0x18?" and
        // stores the answer as a BYTE at Payload+0x8:
        //   .text:06cf6c76  8b4f38    MOV  ECX,[EDI + 0x38]   ; View->PlayerId
        //   .text:06cf6c79  51        PUSH ECX
        //   .text:06cf6c7a  8b0e      MOV  ECX,[ESI]          ; the loot object
        //   .text:06cf6c7c  e8cf87ffff CALL FUN_06cef450
        //   .text:06cf6c81  8b542410  MOV  EDX,[ESP + 0x10]   ; the PAYLOAD
        //   .text:06cf6c85  884208    MOV  byte ptr [EDX + 0x8],AL
        //
        // DECODED 2026-09-17 (wave 13): that list is the object's "marked by"
        // player set -- pub::Player::MarkObj (.text:06d549d5) is its only
        // producer -- and it is modelled now (GameObject::is_marked_by).
        //
        // Note that this store is INSIDE the per-element loop, so the last
        // element of the range wins. That is retail's own behaviour, not a
        // simplification; do not hoist it.
        //
        // +0x08 is FServerCreateLootRequest::bMarkedBy (server_createinfo.h).
        Payload->bMarkedBy = Obj->is_marked_by(View->PlayerId) ? 1u : 0u;

        // .text:06cf6c8d  CALL FUN_06cf7140 -- once per element.
        SystemPlayerView_AddTrackedObject(View, Obj);
    }

    // .text:06cf6c9c .. .text:06cf6ca4 -- and unlike the other four, the loot
    // packet goes out ONLY in single player; there is no ShouldBeVisible arm and
    // bCreatedOnClient is never written here (AddTrackedObject's own seeding is
    // what covers it).
    if (!SinglePlayer())
        return;

    // .text:06cf6caa .. .text:06cf6cb7 -- IClient vtable +0xc0, player id by
    // value (.text:06cf6cb3 `MOV EAX,[EDI + 0x38]`).
    // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
    g_pClientInterface->SendServerCreateLoot(View->PlayerId, *Payload);
}

// FUN_06cf6cf0 (.text:06cf6cf0, `RET 0x8`) -- CREATEGUIDED, IClient +0x0c8.
// Takes the same vector the loot one does but uses ONLY its first element:
// .text:06cf6d00 loads first, .text:06cf6d03 rejects a null one,
// .text:06cf6d07..06cf6d11 computes `(last - first) / 4` and rejects zero, and
// .text:06cf6d13 is `MOV EAX,[ESI]` -- one dereference, no loop.
void SystemPlayerView_CreateGuidedForPlayer(Observer* View, void* Payload,
                                            GameObject* const* First, GameObject* const* Last)
{
    // (w4-server-guards) No null test on View (.text:06cf6cf5 reads
    // [EDI + 0x28] first), none on the element (06cf6d13 `MOV EAX,[ESI]` /
    // 06cf6d15 `PUSH EAX` straight into FUN_06cf7140), none on the record
    // (06cf6d39). The null-first and empty-range exits below are retail's.
    if (View->bShipGone != 0)                        // .text:06cf6cf5
        return;

    if (First == nullptr || First == Last)           // .text:06cf6d03 / 06cf6d11
        return;

    GameObject* const Obj = *First;                  // .text:06cf6d13

    // No is_destroyed() guard and no FDUMP in this one -- the only member of the
    // family without them. Do not add one.
    SystemTrackedObject* const Record =
        SystemPlayerView_AddTrackedObject(View, Obj);    // .text:06cf6d16

    if (!SinglePlayer() && !SystemPlayerView_ShouldBeVisible(View, Obj))
        return;                                      // .text:06cf6d1d / 06cf6d2c

    Record->bCreatedOnClient = true;                 // .text:06cf6d39

    // .text:06cf6d49  ADD EDI,0x38 / .text:06cf6d4c PUSH EDI -- like the
    // countermeasure slot, this one takes the ADDRESS of the player id
    // (IClient::SendServerCreateGuided's `const unsigned int&`). Payload is the caller's
    // FServerCreateGuidedInfo (server_createinfo.h).
    // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
    g_pClientInterface->SendServerCreateGuided(
        View->PlayerId, *static_cast<const FServerCreateGuidedInfo*>(Payload));
}

// ===========================================================================
// FUN_06cf6950 (.text:06cf6950, `RET 0xc`) -- the per-player DAMAGEOBJECT gate,
// and the only writer of the deferred-damage set at View+0x14.
//
// Retail's shape, read off the raw listing rather than off the `.c` (Ghidra
// types param_2 as `int` and then casts it to `int *`, which is the tell):
//
//   .text:06cf6968  CALL FUN_06d006c0(&id, &obj, &flag)   ; id -> GameObject*
//   .text:06cf6988  CALL [EAX + 0x150]                    ; the wrapper's CObject
//   .text:06cf6992  MOV  ECX,[EAX + 0x4c] / AND 0x303 / CMP 0x303
//                                                         ; keep it only if SOLAR
//   .text:06cf69ac  CALL FUN_06cf73d0                     ; IsObjectVisible
//   .text:06cf69b3  JNZ  LAB_06cf6a8c                     ; visible -> send
//   .text:06cf69bd  MOV  EDX,[ESI + 0xe0] / SHR 7 / TEST DL,1
//                                                         ; CSimple::TypeBits bit
//                                                         ; 0x80 == TRADELANE_RING
//   .text:06cf69c9  JNZ  LAB_06cf6a8c                     ; -> send anyway
//   .text:06cf69cf  CALL COMMON.DLL::SinglePlayer / JNZ out
//   .text:06cf69df  JZ   out                              ; not a solar -> drop
//   .text:06cf69e5  MOV  EAX,[ESP + 0x24]                 ; the SECOND record
//   .text:06cf69e9  MOV  ECX,[EAX + 0x8]                  ; its std::list _Size
//   .text:06cf69ee  JNZ  LAB_06cf6a90                     ; non-empty -> send IT
//   .text:06cf69f4 .. .text:06cf6a8a                      ; else insert the id
//   .text:06cf6a90 .. .text:06cf6aa2  CALL [EDX + 0xfc]   ; IClient::SendServerDamageObject
//
// and the send takes THREE stack arguments, `(PlayerId, ObjId, record)`, where
// `record` is param_2 on the visible path (EAX comes from [ESP+0x20] at
// .text:06cf6a8c) and param_3 on the deferred-but-non-empty path (EAX is still
// param_3 from .text:06cf69e5). Two different arguments reaching one PUSH is
// exactly the kind of thing the decompiled `piVar7` hides.
// ===========================================================================
void SystemPlayerView_DamageObject(Observer* View, unsigned int ObjId,
                                   DamageList* PrimaryRecord,
                                   DamageList* DeferredRecord)
{
    // (w4-server-guards) No null test on View: thiscall, EBX = ECX
    // (.text:06cf695b) and nothing tests it before 06cf69ac passes it on.

    // .text:06cf6968 / .text:06cf6972 / .text:06cf697e -- a lookup that fails,
    // and a null result, both leave without touching anything.
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    if (Obj == nullptr)
        return;

    // .text:06cf6988 .. .text:06cf69a7. Retail asks the wrapper's own vtable
    // +0x150 for the CObject and then narrows on (ObjectClass & 0x303) ==
    // 0x303, which is CObject::ClassSolar. GameObject::cobj() already applies
    // the weaker (ObjectClass & 3) == 3, so composing the two is the same test.
    CObject* const Raw = Obj->cobj();
    CSimple* Solar = nullptr;
    if (Raw != nullptr &&
        (static_cast<unsigned>(Raw->ObjectClass) & 0x303u) == 0x303u) {
        Solar = Obj->csimple();
    }

    // .text:06cf69ac .. .text:06cf69c9 -- visible, or a tradelane ring.
    // CSimple::TypeBits is +0xe0 (common_cobject.h has the static_assert) and
    // bit 0x80 is TRADELANE_RING in the OBJECT_TYPE table this file already
    // quotes above kSolarTypeBits. A ring's damage state has to reach the
    // client whether or not the ring is in range.
    bool bSend = SystemPlayerView_IsObjectVisible(View, Obj);
    DamageList* Record = PrimaryRecord;
    if (!bSend && Solar != nullptr && (Solar->TypeBits & 0x80u) != 0)
        bSend = true;

    if (!bSend) {
        // .text:06cf69cf -- in single player there is nothing to defer TO.
        if (SinglePlayer())
            return;
        // .text:06cf69dd -- and a non-solar is dropped outright.
        if (Solar == nullptr)
            return;
        // .text:06cf69e5  MOV EAX,[ESP + 0x24]  ; param_3, the second record
        // .text:06cf69e9  MOV ECX,[EAX + 0x8]   ; its _Size (DamageList::Count)
        if (DeferredRecord->Count != 0) {
            // .text:06cf69ee -- the caller's second record is already populated,
            // so send that one instead of remembering the id.
            Record = DeferredRecord;
            bSend = true;
        } else {
            // .text:06cf69f4 .. .text:06cf6a8a -- `set::insert(ObjId)`. Retail
            // open-codes MSVC6's descent, the uniqueness check and `_Insert`;
            // std::set::insert IS that code.
            View->PendingDamageIds.insert(ObjId);
            return;
        }
    }

    // .text:06cf6a90 .. .text:06cf6aa2 -- IClient vtable +0xfc == SendServerDamageObject,
    // ch2 op 0x05 FLPACKET_SERVER_DAMAGEOBJECT. The player id goes by VALUE
    // (.text:06cf6a9e `MOV EAX,[EBX + 0x38]` / `PUSH EAX`).
    // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
    g_pClientInterface->SendServerDamageObject(View->PlayerId, ObjId, *Record);
}

// ===========================================================================
// FUN_06cf6e60 (.text:06cf6e60, `RET 0x8`) -- "this object has left the
// system". The three things it owns are the tracked-list drop, the
// deferred-damage set CLEAR, and the bShipGone latch.
// ===========================================================================
void SystemPlayerView_OnObjectLeftSystem(Observer* View,
                                         unsigned int const* ObjId,
                                         unsigned int const* Destination)
{
    // (w4-server-guards) No null test on View, ObjId or Destination: thiscall,
    // .text:06cf6e6b `MOV EAX,[EBP + 0xc]` reads the view, 06cf6e8a
    // `CMP EAX,[EBX]` and 06cf6f6f `MOV EDX,[EBX]` read *ObjId, and 06cf6f8d
    // `MOV ECX,[EDX]` reads *Destination -- all unconditionally.

    // .text:06cf6e6b .. .text:06cf6ed4 -- walk the tracked list, match on the
    // wrapper's own get_id (vtable +0x20) at .text:06cf6e87 / .text:06cf6e8a,
    // latch the record's bCreatedOnClient (.text:06cf6e9c reads record+0x2f0,
    // .text:06cf6ea2 spills it), destroy the record (.text:06cf6eaa
    // FUN_06cf6940, .text:06cf6eb0 free), unlink the node (.text:06cf6eb8 ..
    // .text:06cf6ec8) and decrement the size (.text:06cf6ecd .. .text:06cf6ed4).
    // `break` is retail's: .text:06cf6e8c `JZ LAB_06cf6e97` leaves the loop on
    // the first hit and the unlink falls straight through to LAB_06cf6ed7.
    // (w4-server-guards) The walk tests neither the sentinel, nor a node, nor
    // the record, nor its object: 06cf6e6f `MOV ESI,[EAX]` / 06cf6e71
    // `CMP ESI,EAX`, then 06cf6e80 `MOV EAX,[ESI+0x8]` / 06cf6e83
    // `MOV ECX,[EAX]` / 06cf6e87 `CALL [EDX+0x20]`. On the hit, +0x2f0 is read
    // at 06cf6e9c before the `TEST EDI,EDI` at 06cf6e9a takes effect (the JZ is
    // at 06cf6ea6), so only the free is null-tested -- `delete` covers that --
    // and size-- at 06cf6ecd .. 06cf6ed4 is unconditional.
    bool bWasCreatedOnClient = false;
    SystemTrackedNode* const Sentinel = View->Tracked.Head;
    for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        SystemTrackedObject* const Record = Node->Data;
        if (Record->Object->get_id() != *ObjId)
            continue;
        bWasCreatedOnClient = (Record->bCreatedOnClient != false);
        delete Record;                   // .text:06cf6eaa FUN_06cf6940 + free
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;                     // .text:06cf6ec8
        --View->Tracked.Size;             // .text:06cf6ed4
        break;
    }

    // .text:06cf6ed7 .. .text:06cf6f64 -- `_Tree::erase(begin(), end())` on the
    // set at +0x14 (.text:06cf6edf `LEA ESI,[EBP + 0x14]` is its `this`). Retail
    // takes the fast path (.text:06cf6f0f `_Erase` on the whole root, then reset
    // _Size/_Lmost/_Rmost) when the range really is the whole container, and the
    // node-by-node path at .text:06cf6f41 .. .text:06cf6f64 otherwise. Both are
    // the same observable clear().
    View->PendingDamageIds.clear();

    // .text:06cf6f66 .. .text:06cf6f7f -- the latch. `-0x34` off `Players +
    // id*0x418` is PlayerData+0x3E4, the player's own ship object id.
    // (w4-server-guards) No PlayerDB::is_valid here: 06cf6f66 .. 06cf6f79 index
    // the slot raw (`IMUL ECX,ECX,0x418` / `CMP EDX,[ECX + ESI*1 + -0x34]`);
    // Players[] is the same raw `m_PlayerArray[Id - 1]`.
    if (*ObjId == Players[View->PlayerId].ship_object_id)
        View->bShipGone = 1;                 // .text:06cf6f7f

    // .text:06cf6f83 .. .text:06cf6fb0 -- the two-dword stack payload and
    // IClient vtable +0xe4 == SendServerSystemSwitchOut, ch2 op 0x21
    // FLPACKET_SERVER_SYSTEM_SWITCH_OUT. Retail sends it when EITHER the record
    // it just dropped had been created on the client OR bShipGone is now set
    // (.text:06cf6f97 / .text:06cf6fa0). The payload is rpc_iclient_args.h's
    // FServerSystemSwitchOutInfo {ShipId, GateId}.
    if (bWasCreatedOnClient || View->bShipGone != 0) {
        FServerSystemSwitchOutInfo Wire;
        Wire.ShipId = *ObjId;                                         // .text:06cf6f89
        Wire.GateId = *Destination;                                   // .text:06cf6f8f
        // No null test on DAT_06d8fb14 (the IClient pointer): retail loads it and calls through it.
        g_pClientInterface->SendServerSystemSwitchOut(View->PlayerId, Wire);
    }
}

// ===========================================================================
// FUN_06cf6fc0 (.text:06cf6fc0 .. .text:06cf70b1, thiscall on the view,
// `RET 0xc`) -- "this object has landed". The LAND twin of the function
// above: same tracked-list drop, no set clear, no latch, and the packet is
// IClient +0x0ec == SendServerLand, ch2 op 0x0b FLPACKET_SERVER_LAND, with a
// THREE-dword payload (rpc_iclient_args.h's FServerLandInfo {ShipId, DockId, BaseId}).
// ===========================================================================
void SystemPlayerView_OnObjectLanded(Observer* View,
                                     unsigned int const* ObjId,
                                     unsigned int const* Arg2,
                                     unsigned int const* Arg3)
{
    // .text:06cf6fc8 .. .text:06cf702b -- first node whose record's object
    // answers get_id (vtable +0x20, .text:06cf6fda) == *ObjId: BL = the
    // record's +0x2f0 (read at .text:06cf6ff4 BEFORE the null test), record
    // destroyed and freed when non-null (.text:06cf6ffe FUN_06cf6940,
    // .text:06cf7004), node unlinked and freed (.text:06cf700c ..
    // .text:06cf701c), size decremented unguarded (.text:06cf7027). One hit
    // at most: the match jumps out of the walk.
    bool bWasCreatedOnClient = false;
    SystemTrackedNode* const Sentinel = View->Tracked.Head;
    for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        SystemTrackedObject* const Record = Node->Data;
        if (Record->Object->get_id() != *ObjId)
            continue;
        bWasCreatedOnClient = (Record->bCreatedOnClient != false);
        delete Record;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        --View->Tracked.Size;
        break;
    }

    // .text:06cf702e .. .text:06cf7048 -- the payload {*ObjId, *Arg2, *Arg3}
    // at ESP+0xc/+0x10/+0x14, built before either test.
    FServerLandInfo Wire;
    Wire.ShipId = *ObjId;
    Wire.DockId = *Arg2;
    Wire.BaseId = *Arg3;

    // .text:06cf704c .. .text:06cf7061 -- a record that was never created on
    // the client only goes out when the object is this player's own ship
    // (Players + id*0x418 - 0x34 = PlayerData+0x3e4, ship_object_id).
    PlayerData& Owner = Players[View->PlayerId];
    if (!bWasCreatedOnClient && Wire.ShipId != Owner.ship_object_id)
        return;

    // .text:06cf7063 .. .text:06cf708f -- outside single player, the player's
    // own ship landing autosaves the slot (FUN_06d4c430, `LEA ECX,[EAX-0x418]`).
    if (!SinglePlayer() && *ObjId == Owner.ship_object_id)
        PlayerData_AutoSave(Owner);

    // .text:06cf7094 .. .text:06cf70a5 -- IClient +0x0ec (SendServerLand)(player id,
    // &payload). No null test on the interface.
    g_pClientInterface->SendServerLand(View->PlayerId, Wire);
}

// StarSystem::create_med_asteroid / create_med_mine -- retail FUN_06d09bb0 /
// FUN_06d09c80. See the declaration in server_sysdb.h for the map.
//   .text:06d09bc8/06d09c98  operator new(0x68 / 0x88); .text:06d09be2 /
//                            06d09cb5 JZ -> object 0 when the allocation fails
//   .text:06d09bf1 .. 06d09c4b  ListC0.append(object), inlined
//   .text:06d09c56/06d09d29  CALL [EAX + 0x168]  find(*ObjId)
//   .text:06d09c63/06d09d36  PUSH 0 / PUSH ESI / CALL [EDX + 0x170]
//                            enter_system(this, nullptr)
// No test of find()'s answer: the object is entered either way.
// ===========================================================================
SrvAsteroid::MedAsteroid* StarSystem::create_med_asteroid(unsigned int const& ObjId)
{
    SrvAsteroid::MedAsteroid* const Obj = new SrvAsteroid::MedAsteroid();
    ListC0.append(Obj);
    Obj->find(ObjId);
    Obj->enter_system(this, nullptr);
    return Obj;
}

SrvAsteroid::MedMine* StarSystem::create_med_mine(unsigned int const& ObjId)
{
    SrvAsteroid::MedMine* const Obj = new SrvAsteroid::MedMine();
    ListC0.append(Obj);
    Obj->find(ObjId);
    Obj->enter_system(this, nullptr);
    return Obj;
}

// ===========================================================================
// StarSystem::send_mine_explosion -- retail FUN_06d09270 (.text:06d09270).
//   .text:06d0927b .. 06d09297  the four dwords of *Rec copied to a local
//   .text:06d09290 / 06d0929b   sentinel = [this+0xc], node = sentinel->Next;
//                               nothing to do when they are equal
//   .text:06d092a1 .. 06d092a9  FUN_06cf6df0(&node->View, &local)
//   .text:06d092ae / 06d092b0   node = node->Next, compared against a fresh
//                               read of [this+0xc]
// ===========================================================================
void StarSystem::send_mine_explosion(FMineExplosion const& Rec)
{
    FMineExplosion Local = Rec;
    SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
    while (Node != static_cast<SystemPlayerNode*>(PlayerListHead)) {
        SystemPlayerView_SendMineExplosion(&Node->View, &Local);
        Node = Node->Next;
    }
}

// ===========================================================================
// StarSystem::explode -- retail FUN_06d0b260 (.text:06d0b260 .. 06d0b7dd).
// See the declaration in server_sysdb.h for the outline; the trail below is
// per statement. Stack base S = ESP after the four register pushes; the record
// is [S+0x184] (EBX), `this` is kept at [S+0x38].
// ===========================================================================
void StarSystem::explode(ExplosionDamageEvent const& Blast)
{
    // .text:06d0b283 .. 06d0b29c  EBP = [S+0x20] = find_object(Blast +4)
    GameObject* const Inflictor = find_object(Blast.InflictorId);

    // .text:06d0b295 .. 06d0b2b8  ISystem slot 0 (the system id), then
    // FindSphereCollisions(id, &Blast +0xc, Explosion->Radius (+4),
    // parts = S+0x84, 0x3c); the answer is EDI.
    CObject* Parts[0x3c];
    int const Count = PhySys::FindSphereCollisions(SystemInfo->NicknameId, Blast.Position,
                                                   Blast.Explosion->Radius, Parts, 0x3c);

    // .text:06d0b2c2 .. 06d0b2d5  the root vector at S+0x24 {alloc, first,
    // last, end}, all zero.
    // .text:06d0b2d9 / 06d0b2e2  CMP EDI,0 / JBE -- reserve only for a non-zero
    // count; .text:06d0b2e6 JGE clamps a negative count to 0 (06d0b2e8).
    // .text:06d0b2ea .. 06d0b349  reserve(count): new block, copy, free the
    // old one, end = block + count, last = block + size.
    std::vector<CObject*> Roots;
    if (Count != 0)
        Roots.reserve(Count < 0 ? 0u : static_cast<size_t>(Count));

    // .text:06d0b34f JLE / .text:06d0b360 .. 06d0b3b5  for i in [0, count)
    for (int Index = 0; Index < Count; ++Index) {
        // .text:06d0b368  GetCollisionGroup(parts[i])
        char const* const Group = PhySys::GetCollisionGroup(Parts[Index]);
        // .text:06d0b371 .. 06d0b388  both non-null and stricmp == 0 -> skip
        if (Blast.SkipCollisionGroup != nullptr && Group != nullptr &&
            _stricmp(Blast.SkipCollisionGroup, Group) == 0)
            continue;
        // .text:06d0b392 / 06d0b3ad  push_back(GetRoot(parts[i])) (FUN_06d0f460
        // at last, i.e. an insert at end)
        Roots.push_back(GetRoot(Parts[Index]));
    }

    Archetype::Explosion const* const Explosion = Blast.Explosion;

    // .text:06d0b3c4 .. 06d0b3e5  HullDamage (+0xc) > 0.0f goes straight in;
    // otherwise EnergyDamage (+0x10) > 0.0f, else skip to the impulse pass.
    // (FCOMP against DAT_06d65188 = 0.0f, `TEST AH,0x41` -- JZ = greater.)
    if (Explosion->HullDamage > 0.0f || Explosion->EnergyDamage > 0.0f) {
        // .text:06d0b3eb .. 06d0b3f4  a std::vector<unsigned int> at S+0x44
        // (FUN_06d0f1f0) of the ids already damaged.
        std::vector<unsigned int> Damaged;

        // .text:06d0b3f9 .. 06d0b40f / 06d0b5b4 .. 06d0b5c5 -- the walk; the
        // end is re-read every step.
        for (std::vector<CObject*>::iterator It = Roots.begin(); It != Roots.end(); ++It) {
            CObject* const Root = *It;
            // .text:06d0b420 .. 06d0b433  null or (class & 3) != 3 -> next
            if (Root == nullptr || (static_cast<unsigned int>(Root->ObjectClass) & 3u) != 3u)
                continue;
            // .text:06d0b44a / 06d0b452  CSimple id (+0xb0) into [S+0x20]
            unsigned int const Id = static_cast<CSimple*>(Root)->Id;
            // .text:06d0b450 / 06d0b456  the exploding object itself -> next
            if (Id == Blast.ExplodingObjId)
                continue;
            // .text:06d0b45c .. 06d0b473  linear search of Damaged; found -> next
            if (std::find(Damaged.begin(), Damaged.end(), Id) != Damaged.end())
                continue;
            // .text:06d0b479 .. 06d0b4b9  FUN_06d0f210 (find) misses, so
            // FUN_06d0f460 push_back and find again; the {iterator, bool} pair
            // (FUN_06d0f600) is not read afterwards.
            Damaged.push_back(Id);

            // .text:06d0b4d3 .. 06d0b4e5  find_object(id) on [S+0x38]; null -> next
            GameObject* const Obj = find_object(Id);
            if (Obj == nullptr)
                continue;
            // .text:06d0b4eb .. 06d0b4f7  vt +0x160 is_destroyed -> next
            if (Obj->is_destroyed())
                continue;
            // .text:06d0b4fd .. 06d0b525  !DAT_06d88a80 && inflictor &&
            // inflictor->is_player() (vt +0xbc) && Obj->is_player() -> next
            if (GbAllowPvp == 0 && Inflictor != nullptr && Inflictor->is_player() &&
                Obj->is_player())
                continue;

            // .text:06d0b52b .. 06d0b55b  the DamageList at S+0x54, built
            // inline: std::list<DamageEntry> ctor (Common import), bDestroyed
            // (+0xc) = 0, InflictorOwner (+0x18) = 0, Cause (+0x10) = Blast +8,
            // InflictorId (+0x14) = Blast +4.
            DamageList Damage;
            Damage.set_cause(Blast.Cause);
            Damage.set_inflictor_id(Blast.InflictorId);
            // .text:06d0b55f .. 06d0b57b  with an inflictor: its CSimple
            // OwnerPlayer (+0xb4) under the (class & 3) == 3 narrowing, read
            // through the nulled pointer exactly as retail does.
            if (Inflictor != nullptr)
                Damage.set_inflictor_owner_player(Inflictor->csimple()->OwnerPlayer);

            // .text:06d0b57f .. 06d0b589  vt +0x184 explosion_hit(Blast, list)
            Obj->explosion_hit(Blast, Damage);
            // .text:06d0b58f .. 06d0b598  vt +0x188 apply_damage_list(list)
            Obj->apply_damage_list(Damage);
            // .text:06d0b5aa  the list's destructor (Common import)
        }
        // .text:06d0b5cb .. 06d0b5d7  FUN_06d0b7e0 frees Damaged
    }

    // .text:06d0b5de .. 06d0b5ef  Impulse (+8) compared with 0.0f,
    // `TEST AH,0x44 / JNP` -- skipped only when it is exactly equal.
    if (!(Explosion->Impulse == 0.0f)) {
        // .text:06d0b5f5 .. 06d0b609  a second std::vector<unsigned int> at
        // S+0x54, all zero.
        std::vector<unsigned int> Pushed;

        // .text:06d0b60d .. 06d0b61d / 06d0b7a0 .. 06d0b7a9  the walk (EBP)
        for (std::vector<CObject*>::iterator It = Roots.begin(); It != Roots.end(); ++It) {
            CObject* const Root = *It;
            // .text:06d0b623 .. 06d0b637  null or (class & 3) != 3 -> next
            if (Root == nullptr || (static_cast<unsigned int>(Root->ObjectClass) & 3u) != 3u)
                continue;
            CSimple* const Simple = static_cast<CSimple*>(Root);
            // .text:06d0b63d .. 06d0b645  a player's object (+0xb4 != 0) -> next
            if (Simple->OwnerPlayer != 0)
                continue;
            // .text:06d0b64b .. 06d0b6a9  insert-unique of the id into Pushed.
            // Unlike the damage pass the answer is NOT tested: the found arm
            // (.text:06d0b696, pair {it, false}) falls into the same code as
            // the inserted arm (.text:06d0b677, pair {it, true}), so an object
            // whose several parts were in range is pushed once per part.
            if (std::find(Pushed.begin(), Pushed.end(), Simple->Id) == Pushed.end())
                Pushed.push_back(Simple->Id);

            // .text:06d0b6ae .. 06d0b6df  CObject vt +0x68 get_center_of_mass,
            // minus Blast.Position (+0xc), into a Vector at S+0x14
            // (FUN_06ce7c10 is Vector's three-float constructor).
            Vector const Center = Root->get_center_of_mass();
            Vector Dir;
            Dir.x = Center.x - Blast.Position.x;
            Dir.y = Center.y - Blast.Position.y;
            Dir.z = Center.z - Blast.Position.z;

            // .text:06d0b6e4 .. 06d0b6fe  y*y + z*z, then + x*x
            float const LengthSq = (Dir.y * Dir.y + Dir.z * Dir.z) + Dir.x * Dir.x;
            // .text:06d0b700 .. 06d0b70b  FCOM 0.0f / TEST AH,0x44 / JNP
            if (!(LengthSq == 0.0f)) {
                // .text:06d0b70d .. 06d0b733  FSQRT, FDIVR 1.0f (DAT_06d65520),
                // each component scaled
                float const Inverse = 1.0f / sqrtf(LengthSq);
                Dir.x = Dir.x * Inverse;
                Dir.y = Dir.y * Inverse;
                Dir.z = Dir.z * Inverse;
            } else {
                // .text:06d0b737 .. 06d0b757  COMMON.DLL::random_on_unit_sphere
                Dir = random_on_unit_sphere();
            }

            // .text:06d0b75b .. 06d0b784  scaled by Explosion->Impulse (+8)
            float const Impulse = Blast.Explosion->Impulse;
            Dir.x = Dir.x * Impulse;
            Dir.y = Dir.y * Impulse;
            Dir.z = Dir.z * Impulse;

            // .text:06d0b761 / 06d0b786  only with a physics body (+0x54)
            if (Root->BodyPtr != nullptr)
                PhySys::AddImpulseAtPoint(Root, Dir, Blast.Position);   // .text:06d0b78f
        }
        // .text:06d0b7af  Pushed freed
    }
    // .text:06d0b7b8 .. 06d0b7bd  Roots freed
}

// ===========================================================================
// FUN_06d0d500 (.text:06d0d500, thiscall, RET 4) -- StarSystem::munition_collision.
// Declared (with the record's layout) in server_sysdb.h. Traced per statement;
// S = ESP after the four register pushes (EBX, EBP, ESI, EDI).
// ===========================================================================
void StarSystem::munition_collision(SSPMunitionCollisionInfo const& Info)
{
    // .text:06d0d51b .. 06d0d538  EDI = find_object(+0x08) (the object hit),
    // EBP = find_object(+0x04) (the shooter).
    GameObject* const Target = find_object(Info.ObjectId);
    GameObject* const Shooter = find_object(Info.Field_0x04);

    // .text:06d0d53a .. 06d0d56a  DAT_06d88a80 clear and a shooter that is a
    // player (vt +0xbc): a missing target or a player target ends it here.
    if (GbAllowPvp == 0 && Shooter != nullptr && Shooter->is_player())
    {
        if (Target == nullptr)
            return;
        if (Target->is_player())
            return;
    }

    // .text:06d0d570 .. 06d0d584  no target, or a destroyed one (vt +0x160).
    if (Target == nullptr)
        return;
    if (Target->is_destroyed())
        return;

    // .text:06d0d58a .. 06d0d5cc  the FMunitionImpact at S+0x10: shooter id
    // (+0x04), Archetype::GetEquipment(+0x00), sub-object (+0x0c, after a first
    // store of SUBOBJ_ID_NONE), hit position (+0x10..+0x18).
    FMunitionImpact Impact;
    Impact.ShooterId = 0u;
    Impact.Munition = nullptr;
    Impact.SubObjId = SUBOBJ_ID_NONE;
    Archetype::Equipment* const MunitionArch = Archetype::GetEquipment(Info.Field_0x00);
    Impact.Munition = static_cast<Archetype::Munition const*>(MunitionArch);
    Impact.ShooterId = Info.Field_0x04;
    Impact.SubObjId = Info.Field_0x0c;
    std::memcpy(&Impact.HitPos, &Info.Field_0x10, sizeof(Vector));

    // .text:06d0d5d0 .. 06d0d600  the DamageList at S+0x28, built inline
    // (std::list<DamageEntry> head from the Common import, count 0, bDestroyed
    // 0, cause/inflictor/owner 0).
    DamageList Damage;

    // .text:06d0d604 .. 06d0d62e  the cause from the munition's ObjectType
    // (Root +0x10): 0x2000000 -> 2, 0x1000000 -> 3, anything else -> 1.
    unsigned int const ObjectType = MunitionArch->ObjectType;
    if (ObjectType == 0x2000000u)
        Damage.set_cause(static_cast<DamageCause>(2));
    else
        Damage.set_cause(static_cast<DamageCause>((ObjectType == 0x1000000u) ? 3 : 1));

    // .text:06d0d632 .. 06d0d637  inflictor = the shooter's id from the record.
    Damage.set_inflictor_id(Info.Field_0x04);

    // .text:06d0d63b .. 06d0d657  with a shooter: its CSimple OwnerPlayer
    // (+0xb4) under the (class & 3) == 3 narrowing, not null-tested.
    if (Shooter != nullptr)
        Damage.set_inflictor_owner_player(Shooter->csimple()->OwnerPlayer);

    // .text:06d0d65b .. 06d0d669  vt +0x180 munition_hit(Impact, list).
    Target->munition_hit(Impact, Damage);
    // .text:06d0d66f .. 06d0d678  vt +0x188 apply_damage_list(list).
    Target->apply_damage_list(Damage);
    // .text:06d0d67e .. 06d0d6c3  the list's nodes and head are freed.
}

// ===========================================================================
// FUN_06d08e80 (.text:06d08e80, thiscall, RET 0) -- FServerLootDesc's
// constructor, run on every element of create_loot's descriptor block:
//   .text:06d08e84 .. 06d08e95  +0x00 .. +0x18 = 0 (seven dwords)
//   .text:06d08e98  MOV byte [EAX + 0x44],1
//   .text:06d08e9c  MOV byte [EAX + 0x45],CL (0)
//   .text:06d08e9f  MOV byte [EAX + 0x46],CL (0)
// Position, orientation, velocity and +0x47 are left alone.
// ===========================================================================
static void ServerLootDesc_Init(FServerLootDesc* Desc)
{
    Desc->LootId = 0u;
    Desc->ContentsNickname = 0u;
    Desc->ContentsHitPts = 0.0f;
    Desc->Units = 0u;
    Desc->ContainerNickname = 0u;
    Desc->HitPts = 0.0f;
    Desc->NameOverride = 0u;
    Desc->Field_0x44 = 1;
    Desc->bTemporary = 0;
    Desc->bMarkedBy = 0;
}

// ===========================================================================
// StarSystem::create_loot -- retail FUN_06d08a40 (.text:06d08a40 .. 06d08e1c,
// thiscall, RET 0x10). Stack base S = ESP after the four register pushes;
// `this` is kept at [S+0x14], SourceObjId at [S+0x34], Mode at [S+0x38].
// ===========================================================================
void StarSystem::create_loot(unsigned int SourceObjId, int Mode, FLootRecord const* Records, int Count)
{
    // .text:06d08a6c / 06d08a7b  LEA EDX,[EBX+EBX*8] / SHL EDX,3 -- Count * 0x48
    // .text:06d08a9c             operator new, then FUN_06d08e80 on each
    //                            element (.text:06d08ac3 .. 06d08ad6): +0x00..+0x18
    //                            zeroed, byte +0x44 = 1, +0x45 = +0x46 = 0; the
    //                            position/orientation/velocity are left as they are.
    FServerLootDesc* const Items =
        static_cast<FServerLootDesc*>(operator new(static_cast<size_t>(Count) * sizeof(FServerLootDesc)));
    for (int Index = 0; Index < Count; ++Index)
        ServerLootDesc_Init(&Items[Index]);

    // .text:06d08a81 .. 06d08ae2 and .text:06d08aea -- the CREATELOOT request
    // every player view gets at the end: {SourceObjId, Mode, byte 0, byte 0,
    // Items, (byte)Count}. Server-side the layout is FServerCreateLootRequest.
    FServerCreateLootRequest Request = {};
    Request.OwnerId = SourceObjId;                 // .text:06d08a94  [S+0x34]
    Request.Kind = static_cast<unsigned int>(Mode); // .text:06d08a98 [S+0x38]
    Request.bMarkedBy = 0;                         // .text:06d08a81  [S+0x3c]
    Request.bTemporary = 0;                        // .text:06d08a86  [S+0x3d]
    Request.Items = Items;                         // .text:06d08ade  [S+0x40]
    Request.Count = static_cast<unsigned char>(Count); // .text:06d08aea MOV [S+0x44],BL

    // .text:06d08aee .. 06d08afa -- an empty std::vector<GameObject*> at S+0x18;
    // .text:06d08b11 .. 06d08b76 reserve(Count) (operator new(Count*4), the
    // uninitialised copy FUN_06d23e40, the old block freed).
    std::vector<GameObject*> Spawned;

    // .text:06d08afe CMP EBX,EDI / JBE -- an unsigned test: nothing to spawn
    // skips straight to the player walk (which still runs).
    if (static_cast<unsigned int>(Count) != 0u) {
        // .text:06d08b11 MOV EAX,EBX / JGE / XOR EAX,EAX -- the reserve size is
        // Count, or 0 for a negative one.
        Spawned.reserve(static_cast<size_t>(Count >= 0 ? Count : 0));

        FLootRecord const* Record = Records;       // EBP, .text:06d08b80
        FServerLootDesc* Item = Items;             // [S+0x30]
        for (unsigned int Left = static_cast<unsigned int>(Count); Left != 0u; --Left) {
            // .text:06d08b90 operator new(0x70), FUN_06cee360 (GameObject ctor),
            // +0x60/+0x64/+0x68 zeroed, the three Loot vtables (.text:06d08bbc ..
            // 06d08bc9) -- Loot::Loot. No null test on the result reaches the
            // create call below (.text:06d08bd4 XOR EBX,EBX then [EBX]).
            ::Loot* const Obj = new ::Loot();

            // .text:06d08bd6 .. 06d08be6  CALL [EAX + 0x164] (create) with
            // Record->Arch (+0x04); the result is not tested.
            Obj->create(const_cast<Archetype::Equipment*>(Record->Arch));

            // .text:06d08bec .. 06d08c0f  LEA EAX,[EBX+0xc] / [EAX+4]: the
            // wrapper's CBase::Object narrowed with (ObjectClass & 0x803) ==
            // 0x803, kept at [S+0x10] (EDI from .text:06d08c5b on).
            CLoot* const LootObject = Obj->cloot();

            // .text:06d08c13 FUN_06d08e20 -- CLoot::CreateParms defaults: ids 0,
            // HitPts -1.0f, identity orientation, +0x40 .. +0x58 zero. The
            // member initialisers give the CSimple part; Pad58 is stored here.
            CLoot::CreateParms Parms;
            Parms.Pad58 = 0u;
            Parms.Id = Record->Id;                             // .text:06d08c26  +0x00
            Parms.ObjDb = this;                                // .text:06d08c2a  +0x04 = [S+0x14]
            Parms.HitPts = Record->HitPts;                     // .text:06d08c2e  +0x0c
            Parms.Pos = Record->Position;                      // .text:06d08c32 .. 06d08c45  +0x10
            Parms.OwnerPlayer = 0u;                            // .text:06d08c49  +0x08 (EDI == 0)
            Parms.Orient = Record->Orientation;                // .text:06d08c4d .. 06d08c59  REP MOVSD 9
            Parms.Velocity = Record->Velocity;                 // .text:06d08c5f .. 06d08c78  +0x40
            Parms.AngularVelocity = Record->AngularVelocity;   // .text:06d08c7f .. 06d08cac  +0x4c
            Parms.OwnerId = SourceObjId;                       // .text:06d08c91 / 06d08c98  +0x5c
            Parms.Units = static_cast<unsigned int>(Record->Count); // .text:06d08c9f / 06d08cb6  +0x68
            Parms.NameId = static_cast<int>(Record->Field_0x1c);    // .text:06d08ca9 / 06d08cc0  +0x60
            Parms.ContentsArch = Record->ContentsArch;         // .text:06d08cb3 / 06d08cca  +0x64
            Parms.bTemporary = Record->bMission != 0;          // .text:06d08cbd / 06d08cd4  +0x71 <- rec +0x69
            Parms.ContentsHitPts = Record->ContentsHitPts;     // .text:06d08cc7 / 06d08cdb  +0x6c
            Parms.bCanAiTractor = Record->Field_0x68 != 0;     // .text:06d08cd1 / 06d08ce6  +0x70 <- rec +0x68

            // .text:06d08ced .. 06d08cf2  CALL [EDX + 0xa0] -- CLoot::init.
            LootObject->init(Parms);

            // .text:06d08cf8 .. 06d08d07  a player to mark the loot for
            // (FUN_06cef3b0 == GameObject::mark_for_player, mark type 1).
            if ((Record->Field_0x18 & 0x3fffffffu) != 0u)
                Obj->mark_for_player(Record->Field_0x18, 1);

            // .text:06d08d0c .. 06d08d17  CALL [EDX + 0x170] (enter_system)
            // with ESI = [S+0x14] and a null CreateParms.
            Obj->enter_system(this, nullptr);

            // .text:06d08d1d .. 06d08d49 -- MetaList::append inlined on the
            // +0x48 block (FUN_06d11850 node, {Next 0, Prev Tail, Data}).
            Loot.append(Obj);

            // .text:06d08d44 .. 06d08d5b  FUN_06cecbd0 -- vector insert(end, 1,
            // Obj), i.e. push_back.
            Spawned.push_back(Obj);

            // .text:06d08d60 .. 06d08d92 -- controller message 0x2b through
            // FUN_06d43650 (broadcast), the record at S+0x48:
            //   +0x00 the loot id (Parms +0x00, .text:06d08d60 / 06d08d6e)
            //   +0x04 SourceObjId ([S+0xdc], .text:06d08d64 / 06d08d72)
            //   +0x08 Arch2Good(ContentsArch->Nickname) (.text:06d08d6b .. 06d08d8a)
            //   +0x0c Record->Count (+0x14, .text:06d08d80 / 06d08d8e)
            // The field meanings are what the stores say; retail names none.
            struct FControllerMsg2B {
                unsigned int LootId;
                unsigned int SourceObjId;
                unsigned int GoodId;
                int Count;
            } Msg;
            Msg.LootId = Parms.Id;
            Msg.SourceObjId = SourceObjId;
            Msg.GoodId = Arch2Good(Record->ContentsArch->get_id());
            Msg.Count = Record->Count;
            Server_ControllerDispatch(0, pub::Controller::EMessage::LootSpawned, &Msg);

            // .text:06d08d97 .. 06d08d9d  FUN_06d07b90(Item, CLoot).
            ServerLootDesc_Fill(Item, LootObject);

            ++Item;                                // .text:06d08da6 ADD ESI,0x48
            ++Record;                              // .text:06d08dac ADD EBP,0x6c
        }
    }

    // .text:06d08dbe .. 06d08deb -- every player view of this system:
    // FUN_06cf6c50(&Request, &Spawned) on node+8.
    SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
    while (Node != static_cast<SystemPlayerNode*>(PlayerListHead)) {
        GameObject* const* const First = Spawned.empty() ? nullptr : &Spawned[0];
        GameObject* const* const Last = First + Spawned.size();
        SystemPlayerView_CreateLootForPlayer(&Node->View, &Request, First, Last);
        Node = Node->Next;
    }

    // .text:06d08ded .. 06d08dfc -- the descriptor block, then the vector's.
    operator delete(Items);
}

// ===========================================================================
// StarSystem::create_mine -- retail FUN_06d08ef0 (.text:06d08ef0 .. 06d09264,
// thiscall, RET 4). One mine per ProjLaunchInfo, from SrvMineDropper::launch
// (.text:06cedafc). Stack base T = ESP after the four register pushes: this at
// [T+0x14], the CMine::CreateParms at [T+0x70], the CREATEMINE record at
// [T+0x18].
// ===========================================================================
void StarSystem::create_mine(FServerLaunchRecord const& Rec)
{
    // .text:06d08f13 .. 06d08f2b -- the id maker first; none -> FDUMP line 0x3aa.
    CFLIDMaker* const Maker = CFLIDMaker::GetInstance();
    if (Maker == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x3aa,
              "Failed to get instance of global ID maker.\n");
        return;
    }

    // .text:06d08f30 .. 06d08f57 -- operator new(0xa0), Mine::Mine (FUN_06cf3330).
    // The null test on the allocation reaches nothing below it.
    ::Mine* const Obj = new ::Mine();

    Archetype::Equipment* const Arch = const_cast<Archetype::Equipment*>(Rec.Arch);
    Arch->load();                                                   // .text:06d08f60 .. 06d08f70 vt +0xc
    Obj->create(Arch);                                              // .text:06d08f73 .. 06d08f7c vt +0x164
    Arch->Release();                                                // .text:06d08f82 .. 06d08f85

    // .text:06d08f8b .. 06d08fac -- the object under (& 0xf) == 0xf, at [T+0x10].
    CMine* const MineObject = Obj->cmine();

    // .text:06d08fac .. 06d08fb5 -- FUN_06d08e20 (the CSimple half's defaults:
    // ids 0, HitPts -1.0f, identity orientation, +0x40 .. +0x58 zero), then
    // +0x5c = 0; both are the member initialisers here.
    CMine::CreateParms Parms;

    // .text:06d08fbc .. 06d08fcf -- CFLIDMaker::AllocateObjectID inlined: the
    // +0x04 counter post-incremented, masked with 0x3fffffff.
    Parms.Id = Maker->AllocateObjectID();
    if ((Parms.Id & 0x3fffffffu) == 0u)                             // .text:06d08fc7 TEST / JNZ
    {
        // .text:06d08fd5 .. 06d08ff9 -- FDUMP line 0x3c2 and out; the mine
        // wrapper built above is neither entered nor freed.
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x3c2,
              "Failed to allocate valid ID for new mine.\n");
        return;
    }

    // .text:06d08ffe .. 06d090b0 -- the rest of the parms.
    Parms.ObjDb = this;                                             // .text:06d0901a / 06d09020  +0x04
    Parms.OwnerPlayer = 0u;                                         // .text:06d09035  +0x08
    Parms.Pos = Rec.Position;                                       // .text:06d09001 .. 06d0903b  +0x10 <- rec +0x10
    Parms.Orient = Rec.Orientation;                                 // .text:06d09044 .. 06d09055  REP MOVSD 9 <- rec +0x1c
    Parms.Velocity = Rec.Velocity;                                  // .text:06d09059 .. 06d09079  +0x40 <- rec +0x40

    // .text:06d08ffe .. 06d0908a -- random_inside_sphere(|Velocity|) into
    // +0x4c. The length is summed (x*x + y*y) + z*z on the x87 stack (double
    // precision control) and stored as a float argument (FSTP dword).
    double LengthSq = static_cast<double>(Rec.Velocity.x) * static_cast<double>(Rec.Velocity.x) +
                      static_cast<double>(Rec.Velocity.y) * static_cast<double>(Rec.Velocity.y);
    LengthSq += static_cast<double>(Rec.Velocity.z) * static_cast<double>(Rec.Velocity.z);
    float const Speed = static_cast<float>(std::sqrt(LengthSq));
    Parms.AngularVelocity = random_inside_sphere(Speed);            // .text:06d09090 .. 06d090a9  +0x4c
    Parms.OwnerId = Rec.OwnerId;                                    // .text:06d0909c / 06d090b0  +0x5c <- rec +0x04

    MineObject->init(Parms);                                        // .text:06d090b7 .. 06d090c5 vt +0xa0
    Obj->enter_system(this, nullptr);                               // .text:06d090cb .. 06d090d7 vt +0x170
    Obj->arm();                                                     // .text:06d090df FUN_06cf3560

    // .text:06d090e4 .. 06d0910c -- MetaList append on the +0x98 block,
    // inlined (FUN_06d11850).
    List98.append(Obj);

    // .text:06d0910f .. 06d0922c -- the CREATEMINE record. Retail's inlined
    // constructor zeroes +0x00/+0x04/+0x08/+0x10..+0x18/+0x44, puts
    // SUBOBJ_ID_NONE in +0x0c and runs FUN_06ce7c10 (0,0,0) on +0x2c and +0x38;
    // every one of those is then overwritten from the parms and the record
    // except +0x44.
    FServerCreateProjectileInfo Info;
    Info.ObjId = Parms.Id;                                          // .text:06d0915b / 06d09162
    Info.ArchetypeNickname = Arch->get_id();                        // .text:06d0915f / 06d09175 [arch +0x08]
    Info.OwnerId = Rec.OwnerId;                                     // .text:06d0920b / 06d09221
    Info.SubObjId = Rec.EquipId;                                    // .text:06d0921d / 06d0922c
    Info.Pad0e = 0;
    Info.Position = Parms.Pos;                                      // .text:06d09166 .. 06d09199
    Server_MatrixToQuat(Info.Orientation, Parms.Orient);            // .text:06d09183 .. 06d091f1
    Info.Velocity = Parms.Velocity;                                 // .text:06d091cc .. 06d0920e
    Info.Field_0x38 = Parms.AngularVelocity;                        // .text:06d091ea .. 06d09228
    Info.Field_0x44 = 0u;                                           // .text:06d0916d

    // .text:06d09225 .. 06d0924a -- every player view: FUN_06cf6d60.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
        SystemPlayerView_CreateMineForPlayer(&Player->View, &Info, Obj);
}

// ===========================================================================
// StarSystem::create_counter_measure -- retail FUN_06d092c0 (.text:06d092c0 ..
// 06d09634, thiscall, RET 4). Caller SrvCounterMeasureDropper::launch
// (.text:06cedd5c). Instruction for instruction create_mine's body with these
// differences only: the id-maker FDUMP line is 0x401 and the id FDUMP line 0x419
// (the id text is still "... new mine.", .rdata:06d67fb0 at .text:06d093a2);
// operator new(0x60) with FUN_06ce6170; the (& 0x17) narrowing; FUN_06ce6300
// after enter_system; the +0xac block; FUN_06cf6bc0 per player view.
// ===========================================================================
void StarSystem::create_counter_measure(FServerLaunchRecord const& Rec)
{
    CFLIDMaker* const Maker = CFLIDMaker::GetInstance();
    if (Maker == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x401,
              "Failed to get instance of global ID maker.\n");
        return;
    }

    ::CounterMeasure* const Obj = new ::CounterMeasure();          // FUN_06ce6170

    Archetype::Equipment* const Arch = const_cast<Archetype::Equipment*>(Rec.Arch);
    Arch->load();                                                   // vt +0xc
    Obj->create(Arch);                                              // vt +0x164 (.text:06ce6200)
    Arch->Release();

    CCounterMeasure* const CmObject = Obj->ccountermeasure();       // (& 0x17) == 0x17

    CCounterMeasure::CreateParms Parms;                             // FUN_06d08e20 + OwnerId 0

    Parms.Id = Maker->AllocateObjectID();
    if ((Parms.Id & 0x3fffffffu) == 0u)
    {
        // The wrapper built above is neither entered nor freed.
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x419,
              "Failed to allocate valid ID for new mine.\n");
        return;
    }

    Parms.ObjDb = this;
    Parms.OwnerPlayer = 0u;
    Parms.Pos = Rec.Position;
    Parms.Orient = Rec.Orientation;
    Parms.Velocity = Rec.Velocity;

    // random_inside_sphere(|Velocity|): (x*x + y*y) + z*z on the x87 stack, the
    // root stored as a float argument.
    double LengthSq = static_cast<double>(Rec.Velocity.x) * static_cast<double>(Rec.Velocity.x) +
                      static_cast<double>(Rec.Velocity.y) * static_cast<double>(Rec.Velocity.y);
    LengthSq += static_cast<double>(Rec.Velocity.z) * static_cast<double>(Rec.Velocity.z);
    float const Speed = static_cast<float>(std::sqrt(LengthSq));
    Parms.AngularVelocity = random_inside_sphere(Speed);
    Parms.OwnerId = Rec.OwnerId;

    CmObject->init(Parms);                                          // vt +0xa0
    Obj->enter_system(this, nullptr);                               // vt +0x170
    Obj->divert_missiles();                                         // .text:06d094ac FUN_06ce6300

    // MetaList append on the +0xac block, inlined (FUN_06d11850).
    ListAC.append(Obj);

    // The CREATECOUNTER record, built exactly as create_mine's.
    FServerCreateProjectileInfo Info;
    Info.ObjId = Parms.Id;
    Info.ArchetypeNickname = Arch->get_id();
    Info.OwnerId = Rec.OwnerId;
    Info.SubObjId = Rec.EquipId;
    Info.Pad0e = 0;
    Info.Position = Parms.Pos;
    Server_MatrixToQuat(Info.Orientation, Parms.Orient);
    Info.Velocity = Parms.Velocity;
    Info.Field_0x38 = Parms.AngularVelocity;
    Info.Field_0x44 = 0u;

    // Every player view: FUN_06cf6bc0.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
        SystemPlayerView_CreateCounterForPlayer(&Player->View, &Info, Obj);
}

// ===========================================================================
// StarSystem::create_guided -- retail FUN_06d09640 (.text:06d09640 .. 06d099d5,
// thiscall, RET 4). One missile per launch record, from SrvGun::launch_guided
// (.text:06cee1ae). Stack base T = ESP after the three register pushes: the id
// at [T+0x28], the target at [T+0x24], the CGuided::CreateParms at [T+0x78],
// the one-element vector at [T+0x30], the CREATEGUIDED record at [T+0x40].
// Unlike create_mine, the id is allocated and the target looked up BEFORE the
// wrapper is built.
// ===========================================================================
void StarSystem::create_guided(FServerLaunchRecord const& Rec)
{
    // .text:06d09664 .. 06d09694 -- FDUMP line 0x446.
    CFLIDMaker* const Maker = CFLIDMaker::GetInstance();
    if (Maker == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x446,
              "Failed to get instance of global ID maker.\n");
        return;
    }

    // .text:06d09699 .. 06d096d5 -- AllocateObjectID inlined; 0 -> FDUMP line 0x44d.
    unsigned int const Id = Maker->AllocateObjectID();
    if ((Id & 0x3fffffffu) == 0u)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x44d,
              "Failed to allocate valid, dynamic ID for guided object.\n");
        return;
    }

    // .text:06d096db .. 06d096f2 -- the lock target by id (FUN_06d0c840).
    GameObject* const Target = find_object(Rec.TargetId);

    // .text:06d096ed .. 06d09716 -- operator new(0x80), Guided::Guided
    // (FUN_06cf3ce0). The null test on the allocation reaches nothing below it.
    ::Guided* const Obj = new ::Guided();

    Archetype::Equipment* const Arch = const_cast<Archetype::Equipment*>(Rec.Arch);
    Arch->load();                                                   // .text:06d09716 .. 06d09726 vt +0xc
    Obj->create(Arch);                                              // .text:06d09729 .. 06d09732 vt +0x164
    Arch->Release();                                                // .text:06d09738 .. 06d0973b

    // .text:06d09741 .. 06d09767 -- the object under (& 0xa7) == 0xa7, at [T+0x14].
    CGuided* const GuidedObject = Obj->cguided();

    // .text:06d09767 .. 06d097fa -- FUN_06d08e20 defaults, then:
    CGuided::CreateParms Parms;
    Parms.Id = Id;                                                  // .text:06d09782  +0x00
    Parms.ObjDb = this;                                             // .text:06d09786  +0x04
    Parms.OwnerPlayer = 0u;                                         // .text:06d0979b  +0x08
    Parms.Pos = Rec.Position;                                       // .text:06d09770 .. 06d09794  +0x10
    Parms.Orient = Rec.Orientation;                                 // .text:06d097a6 .. 06d097b5  +0x1c (REP MOVSD 9)
    Parms.Velocity = Rec.Velocity;                                  // .text:06d097b7 .. 06d097de  +0x40
    Parms.OwnerId = Rec.OwnerId;                                    // .text:06d097c9 / 06d097d7  +0x5c
    Parms.Target = static_cast<IObjRW*>(Target);                    // .text:06d097e5 / 06d097fa  +0x60
    Parms.SubTarget = Rec.TargetSubId;                              // .text:06d097d3 / 06d097ed  +0x64

    GuidedObject->init(Parms);                                      // .text:06d09801 CGuided::init (import)

    // .text:06d09807 .. 06d09834 -- MetaList append on the +0x70 block, inlined.
    List70.append(Obj);

    Obj->enter_system(this, nullptr);                               // .text:06d09832 .. 06d0983d vt +0x170
    Obj->arm();                                                     // .text:06d09843 .. 06d09848 vt +0x1bc

    // .text:06d0984e .. 06d0987f -- an empty std::vector<GameObject*> and
    // insert(end, 1, Obj) (FUN_06cecbd0).
    std::vector<GameObject*> Spawned;
    Spawned.push_back(Obj);

    // .text:06d09884 .. 06d0991f -- the CREATEGUIDED record.
    FServerCreateGuidedInfo Info;
    Info.ObjId = Parms.Id;                                          // .text:06d09891 / 06d098ab  +0x00
    Info.Position = Parms.Pos;                                      // .text:06d0988a .. 06d098be  +0x04
    Server_MatrixToQuat(Info.Orientation, Parms.Orient);            // .text:06d09899 .. 06d09904  +0x10
    Info.TargetId = Rec.TargetId;                                   // .text:06d09908 / 06d0991b  +0x20
    Info.TargetSubObjId = Rec.TargetSubId;                          // .text:06d09900 / 06d09913  +0x24
    Info.Pad26 = 0;
    Info.ArchetypeNickname = Arch->get_id();                        // .text:06d09884 .. 06d098a0  +0x28
    Info.Field_0x2c = 0u;                                           // .text:06d098b6  +0x2c
    Info.OwnerId = Rec.OwnerId;                                     // .text:06d098f9 / 06d0990b  +0x30
    Info.SubObjId = Rec.EquipId;                                    // .text:06d0990f / 06d0991f  +0x34
    Info.Pad36 = 0;

    // .text:06d09918 .. 06d09947 -- every player view: FUN_06cf6cf0.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        GameObject* const* const First = &Spawned[0];
        SystemPlayerView_CreateGuidedForPlayer(&Player->View, &Info, First, First + Spawned.size());
    }

    // .text:06d09949 .. 06d09984 -- a live target that is a CEqObj ((& 0x103),
    // null tests included) with a behavior interface ...
    if (Target != nullptr && !Target->is_destroyed())
    {
        CObject* const Raw = Target->Object;
        if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x103u) == 0x103u)
        {
            IBehaviorManager* const Behavior = static_cast<CEqObj*>(Raw)->get_behavior_interface();
            if (Behavior != nullptr)
            {
                // .text:06d09986 .. 06d099ae -- block {+0 [EBX+4], +4 the new
                // missile's id ([ESP+0x2c])}, event {6, &block} to
                // IBehaviorManager slot 0 (Common LAB_062d3f90).
                unsigned int MissileBlock[2];
                MissileBlock[0] = Rec.OwnerId;
                MissileBlock[1] = Id;
                FBehaviorManagerEvent MissileEvent;
                MissileEvent.Type = 6;
                MissileEvent.Data = MissileBlock;
                Behavior->Slot00(&MissileEvent);
            }
        }
    }

    // .text:06d099b0 .. 06d099b5 -- the vector's block freed (its destructor).
}

// ===========================================================================
// StarSystem::create_bullet -- retail FUN_06d099e0 (.text:06d099e0 .. 06d09b82,
// thiscall, RET 4). One gun round from SrvGun::launch_bullets (.text:06cee2b6).
// No player view and no controller hears of it here: the client builds its own
// rounds from the FIREWEAPON notify (FUN_06d0e2e0).
// ===========================================================================
void StarSystem::create_bullet(FServerBulletRecord const& Rec)
{
    // .text:06d099fb .. 06d09a36 -- operator new(0x60), FUN_06cee360 (GameObject
    // ctor) and the three Bullet vtables: Bullet::Bullet. The null test on the
    // allocation reaches nothing below it (.text:06d09a38 keeps ESI = 0).
    ::Bullet* const Obj = new ::Bullet();

    Archetype::Munition* const Arch = const_cast<Archetype::Munition*>(Rec.Arch);
    Arch->load();                                                   // .text:06d09a46 .. 06d09a53 vt +0xc
    Obj->create(Arch);                                              // .text:06d09a56 .. 06d09a5e vt +0x164 (FUN_06cf4d40)
    Arch->Release();                                                // .text:06d09a64 .. 06d09a67

    // .text:06d09a6d .. 06d09a88 -- the wrapper's object under (& 0x67) == 0x67.
    CBeam* const Beam = Obj->cbeam();

    // .text:06d09a88 .. 06d09afc -- the CProjectile::CreateParms on the stack
    // (base [ESP+0x14]); every field is written here, the ctor is inlined away.
    CProjectile::CreateParms Parms;
    Parms.Id = 0u;                                                  // .text:06d09aad  +0x00
    Parms.ObjDb = this;                                             // .text:06d09adb  +0x04
    Parms.OwnerPlayer = 0u;                                         // .text:06d09aa5  +0x08
    Parms.HitPts = -1.0f;                                           // .text:06d09abb  +0x0c (0xbf800000)
    Parms.Pos = Rec.Position;                                       // .text:06d09a8f .. 06d09ab1  +0x10 <- rec +0x0c
    Parms.Orient = Rec.Orientation;                                 // .text:06d09ae8 REP MOVSD 9  +0x1c <- rec +0x18
    Parms.Velocity = Rec.Velocity;                                  // .text:06d09ab5 .. 06d09afc  +0x40 <- rec +0x3c
    Parms.AngularVelocity = Vector{0.0f, 0.0f, 0.0f};               // .text:06d09ac3 .. 06d09ad3  +0x4c
    Parms.Pad58 = 0u;                                               // .text:06d09aa9  +0x58
    Parms.OwnerId = Rec.OwnerId;                                    // .text:06d09a88 .. 06d09a8b  +0x5c <- rec +0x00
    // Rec.EquipId (+0x08) is not read.

    Beam->init(Parms);                                              // .text:06d09b00 .. 06d09b05 vt +0xa0
    Obj->enter_system(this, nullptr);                               // .text:06d09b0b .. 06d09b16 vt +0x170

    // .text:06d09b1c .. 06d09b70 -- MetaList append on the +0x84 block, inlined
    // (FreeList +0x94 when bUseHeap +0x90, else operator new(0xc)).
    Beams.append(Obj);
}

// ===========================================================================
// Four StarSystem members GameObject / EqObj reach through OwnerSystem.
// PORTED 2026-09-24 (claude-code:w5-server-06ce); see server_sysdb.h.
// ===========================================================================

// FUN_06d0cfa0 (.text:06d0cfa0, RET 4). Two MetaList walks: ESI = 0 then
// `ESI ? [ESI] : [EBX + 0x38]` (Ships' head) until null, CALL [obj vt + 0x178]
// with the id; then the same over [EBX + 0x60] (Solars' head).
void StarSystem::notify_object_left(unsigned int ObjId)
{
    MetaNode<GameObject>* Node = nullptr;
    while (Ships.traverse(Node, true))
        Node->Data->on_object_left_system(ObjId);           // .text:06d0cfc5
    Node = nullptr;
    while (Solars.traverse(Node, true))
        Node->Data->on_object_left_system(ObjId);           // .text:06d0cfe5
}

// FUN_06d0d730 (.text:06d0d730, RET 8).
void StarSystem::collision_damage(FCollisionDamage const& Rec, GameObject* Target)
{
    // .text:06d0d74d .. 06d0d753  the source, by id (FUN_06d0c840).
    GameObject* const Source = find_object(Rec.SourceId);

    // .text:06d0d75e .. 06d0d78d  with DAT_06d88a80 clear, a player source never
    // damages a player target (IObjInspect +0xbc is_player on both); a null
    // target also ends it here.
    if (GbAllowPvp == 0 && Source != nullptr && Source->is_player())
    {
        if (Target == nullptr)
            return;
        if (Target->is_player())
            return;
    }
    // .text:06d0d793 .. 06d0d7a7
    if (Target == nullptr)
        return;
    if (Target->is_destroyed())
        return;

    // .text:06d0d7ad .. 06d0d7ca  the hit: {source id, the target's sub-object,
    // the damage, byte +0x0c}. The byte is `MOV AL,[ESP + 0x44]` -- the low byte
    // of this function's own first argument slot, i.e. of the Rec POINTER; retail
    // never writes a value of its own there. Reproduced as read.
    FSubObjDamage Hit;
    Hit.InflictorId = Rec.SourceId;
    Hit.SubObjId = Rec.TargetSubObjId;
    Hit.Damage = Rec.Damage;
    Hit.Field_0x0c = static_cast<unsigned char>(reinterpret_cast<uintptr_t>(&Rec) & 0xffu);

    // .text:06d0d7ce .. 06d0d81f  a fresh DamageList: Cause 1, inflictor = the
    // source id, owner player = the source's CSimple::OwnerPlayer (guarded
    // accessor read without a null test) when there is a source, else 0.
    DamageList Damage;
    Damage.Cause = static_cast<DamageCause>(1);
    Damage.InflictorId = Rec.SourceId;
    Damage.InflictorOwner = 0;
    if (Source != nullptr)
        Damage.InflictorOwner = Source->csimple()->OwnerPlayer;

    Target->damage_sub_obj(Hit, Damage);                    // .text:06d0d831 vt +0x17c
    Target->apply_damage_list(Damage);                      // .text:06d0d840 vt +0x188
}

// FUN_06d0d6e0 (.text:06d0d6e0 .. .text:06d0d722, RET 4). PORTED 2026-09-24
// (claude-code:w5-server-06cf), all 28 instructions:
//   06d0d6e6 .. 06d0d6f3  obj = FUN_06d0c840([Info + 4]); null -> out
//   06d0d6f9 .. 06d0d701  obj->vt[+0x160] (is_destroyed) true -> out
//   06d0d707 .. 06d0d70f  obj->vt[+0x19c] (is_invincible) true -> out
//   06d0d711 .. 06d0d71a  obj->vt[+0x158] destroy(1, [Info + 0]); result unused
void StarSystem::badlands_collision(SSPBadLandsObjCollisionInfo const& Info)
{
    GameObject* const Obj = find_object(Info.ObjectId);
    if (Obj == nullptr)
        return;
    if (Obj->is_destroyed())
        return;
    if (Obj->is_invincible())
        return;
    Obj->destroy(1, Info.Field_0x00);
}

// FUN_06d0cd60 (.text:06d0cd60 .. .text:06d0ce40, RET 8). PORTED 2026-09-24
// (claude-code:w5-server-06cf), all 135 instructions:
//   06d0cd70 .. 06d0cd81  MetaList walk of the solar block: EDI = 0 then
//                         `EDI ? [EDI] : [this + 0x60]` until null
//   06d0cd87 .. 06d0cdad  obj = node->Data; the CObject at obj+0xc+4 narrowed
//                         with (ObjectClass & 0x303) == 0x303, else 0 -- there
//                         is no test on the result: [0 + 0x164] is read anyway
//   06d0cdaf .. 06d0cdc3  base = CEqObj+0x164; base != 0: BaseId != 0 and
//                         base != BaseId -> next node
//   06d0cdc5 .. 06d0cddb  Out.insert(end, obj) (FUN_06d05d90) -> next node
//   06d0cde2 .. 06d0cdf2  base == 0: TypeBits (+0xe0) with a bit outside 0xc40
//                         -> next node
//   06d0cdf8 .. 06d0cdfe  BaseId != 0 -> next node
//   06d0ce04 .. 06d0ce3b  the inline push_back (FUN_06cfc340 buynode,
//                         FUN_06d06720 construct, ++size) -> next node
// Both inserts are push_back; the list is the caller's.
void StarSystem::collect_dockable_solars(std::list<GameObject*>& Out, unsigned int BaseId)
{
    MetaNode<GameObject>* Node = nullptr;
    while (Solars.traverse(Node, true))
    {
        GameObject* const Obj = Node->Data;
        CObject* const Cobj = Obj->cobj();
        CEqObj* Eq = nullptr;
        if (Cobj != nullptr && (static_cast<unsigned int>(Cobj->ObjectClass) & 0x303u) == 0x303u)
            Eq = static_cast<CEqObj*>(Cobj);
        // Retail reads +0x164 through the narrowed pointer without testing it.
        unsigned int const Base = Eq->DockingPointBaseId;
        if (Base != 0)
        {
            if (BaseId != 0 && Base != BaseId)
                continue;
            Out.push_back(Obj);
            continue;
        }
        unsigned int const Bits = Eq->TypeBits;
        if ((Bits & 0xc40u) != Bits)
            continue;
        if (BaseId != 0)
            continue;
        Out.push_back(Obj);
    }
}

// StarSystem::send_comm -- FUN_06d0db30 (.text:06d0db30 .. .text:06d0dd2f),
// thiscall, RET 0x58. Ported 2026-09-24 (claude-code:w5-server-06cf):
//   06d0db37 .. 06d0db52  (SenderId & 0x3fffffff) == 0 -> bNoSender = 1 (the sender
//                         position stays unset; it is never read on that path)
//   06d0db54 .. 06d0db7a  else FUN_06d0c840 = find_object(SenderId); null -> return;
//                         its IObjInspect vt[0] get_position() copied
//   06d0db7e .. 06d0db90  every node of the player list (head re-read, 06d0dd1f):
//     06d0db90 .. 06d0dbc1  PlayerDB::is_valid(node PlayerId [+0x40]) inlined, else next
//     06d0dbcd  ShipId = Players[PlayerId].ship_object_id [slot-0x34]
//     bNoSender (06d0dcc7): send when pub::SinglePlayer() (Server's forwarder onto
//               COMMON's SinglePlayer) or ReceiverId == ShipId or bBroadcast
//     ReceiverId == ShipId (06d0dc7e): send, then bBroadcast == 0 -> RETURN
//     otherwise bBroadcast == 0 -> next (06d0dbeb); find_object(ShipId) null -> next;
//               squared distance of its get_position() from the sender's, summed
//               (dz^2 + dx^2) + dy^2 on the x87 stack, FCOMP against .rdata:06d66af4
//               (1e8 = 10000^2), TEST AH,0x41 / JZ -> next when strictly greater
//               (an unordered compare sends); else send
//   every send is FUN_06cf6ac0(&node->View, SenderId, ReceiverId, VoiceId, Costume,
//   CommType, TextIds, TextCount, Mood, Duration) -- SystemPlayerView_SendComm.
// The x87 differences are taken in double here (float operands, so each difference
// is exact; the squares and sums round at 53 bits like MSVC's default x87 precision).
void StarSystem::send_comm(unsigned int SenderId, unsigned int ReceiverId, unsigned int VoiceId,
                           Costume const& Cost, unsigned int CommType, unsigned int* TextIds,
                           int TextCount, unsigned int Mood, float Duration, bool bBroadcast)
{
    bool bNoSender = false;
    Vector SenderPos = {};
    if ((SenderId & 0x3fffffffu) == 0) {
        bNoSender = true;                                                    // .text:06d0db4d
    } else {
        GameObject* const Sender = find_object(SenderId);                    // .text:06d0db57
        if (Sender == nullptr)
            return;
        SenderPos = Sender->get_position();                                  // .text:06d0db68
    }

    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next)
    {
        unsigned int const PlayerId = Node->View.PlayerId;                   // .text:06d0db90
        if (!Players.is_valid(PlayerId))
            continue;
        unsigned int const ShipId = Players[PlayerId].ship_object_id;       // .text:06d0dbcd

        if (bNoSender) {
            if (SinglePlayer() || ReceiverId == ShipId || bBroadcast)        // .text:06d0dcc7
                SystemPlayerView_SendComm(&Node->View, SenderId, ReceiverId, VoiceId, Cost,
                                          CommType, TextIds, TextCount, Mood, Duration);
            continue;
        }

        if (ReceiverId == ShipId) {                                          // .text:06d0dbdd
            SystemPlayerView_SendComm(&Node->View, SenderId, ReceiverId, VoiceId, Cost,
                                      CommType, TextIds, TextCount, Mood, Duration);
            if (!bBroadcast)                                                 // .text:06d0dcc3
                return;
            continue;
        }

        if (!bBroadcast)                                                     // .text:06d0dbeb
            continue;
        GameObject* const Ship = find_object(ShipId);                        // .text:06d0dbf4
        if (Ship == nullptr)
            continue;
        Vector const& Pos = Ship->get_position();                            // .text:06d0dc05
        double const Dx = static_cast<double>(Pos.x) - static_cast<double>(SenderPos.x);
        double const Dy = static_cast<double>(Pos.y) - static_cast<double>(SenderPos.y);
        double const Dz = static_cast<double>(Pos.z) - static_cast<double>(SenderPos.z);
        double const DistSq = (Dz * Dz + Dx * Dx) + Dy * Dy;
        if (DistSq > static_cast<double>(1.0e8f))                            // .text:06d0dc2b
            continue;
        SystemPlayerView_SendComm(&Node->View, SenderId, ReceiverId, VoiceId, Cost,
                                  CommType, TextIds, TextCount, Mood, Duration);
    }
}

// FUN_06d0e750 (.text:06d0e750, RET 4). Every node of the player list (+0x0c):
// FUN_06cf7370(&node->View, Info.ObjectId), and on true IClient +0x15c
// (DAT_06d8fb14) with &node+0x40 -- the view's PlayerId -- and the record.
void StarSystem::broadcast_fuse(FServerBurnFuseInfo const& Info)
{
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, Info.ObjectId))
            continue;
        g_pClientInterface->SendServerBurnFuse(Node->View.PlayerId, Info);   // .text:06d0e77f
    }
}

// ===========================================================================
// The IObjAffect notifiers (2026-09-24, claude-code:w5-server-06d0). Every one
// walks the player list (+0x0c, sentinel at [this+0xc]) the same way retail
// does -- `MOV ESI,[head]` / `CMP ESI,head` / JZ, then `MOV ESI,[ESI]` /
// `CMP ESI,[this+0xc]` at the bottom -- with no null test on the head.
// FUN_06cf7370 is SystemPlayerView_IsObjectIdVisible; DAT_06d8fb14 is
// g_pClientInterface, called unguarded. The four player-skip forms compare the
// view's player (node+0x40) with *SourceClient, the id of the client that
// asked for the change, so it is not told about its own action.
// ===========================================================================

// FUN_06d0e500 (.text:06d0e500, RET 0x10). Only caller EqObj slot_4c
// (.text:06ce71ef). Per node:
//   .text:06d0e520 .. 06d0e529  node+0x40 == *SourceClient -> next
//   .text:06d0e52b .. 06d0e538  FUN_06cf7370(&node->View, *ObjId) == 0 -> next
//   .text:06d0e53a .. 06d0e55a  record {*ObjId, word *EquipId, byte bActivate}
//   .text:06d0e561              IClient +0x04 (SendCommonActivateEquip) (node+0x40, &record)
// The record is XActivateEquip; its pad byte is not written (uninitialised in
// retail, zero here).
void StarSystem::notify_activate_equip(unsigned int const& ObjId, unsigned short const& EquipId,
                                       bool bActivate, int const& SourceClient)
{
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, ObjId))
            continue;
        XActivateEquip Record = {};
        Record.ObjectId = ObjId;
        Record.EquipId = EquipId;
        Record.bActivate = bActivate;
        g_pClientInterface->SendCommonActivateEquip(Node->View.PlayerId, Record);
    }
}

// FUN_06d0e580 (.text:06d0e580, RET 0x10). Only caller Ship slot_50
// (.text:06d024e3). Same walk; record {*ObjId, byte bEngage (+4, param 2),
// byte bArg2 (+5, param 3 kept in BL)} (.text:06d0e5ba .. 06d0e5d6), sent
// through IClient +0x08 (SendCommonActivateCruise) at .text:06d0e5dd. The record is
// XActivateCruise.
void StarSystem::notify_cruise(unsigned int const& ObjId, bool bEngage, bool bArg2,
                               int const& SourceClient)
{
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, ObjId))
            continue;
        XActivateCruise Record = {};
        Record.ObjectId = ObjId;
        Record.bActivate = bEngage;
        Record.Field_0x05 = bArg2 ? 1u : 0u;
        g_pClientInterface->SendCommonActivateCruise(Node->View.PlayerId, Record);
    }
}

// FUN_06d0e600 (.text:06d0e600, RET 0xc). Only caller Ship slot_54
// (.text:06d0264e). Same walk; record {*ObjId, byte bActivate}
// (.text:06d0e63a .. 06d0e64e) through IClient +0x0c (SendCommonActivateThrusters) at
// .text:06d0e655. The record is XActivateThrusters.
void StarSystem::notify_thrusters(unsigned int const& ObjId, bool bActivate, int const& SourceClient)
{
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, ObjId))
            continue;
        XActivateThrusters Record = {};
        Record.ObjectId = ObjId;
        Record.bActivate = bActivate;
        g_pClientInterface->SendCommonActivateThrusters(Node->View.PlayerId, Record);
    }
}

// FUN_06d0e670 (.text:06d0e670, RET 0x14). Callers: Ship set_target
// (.text:06d023f6), EqObj slot_44 (.text:06ce708c), Guided set_target
// (.text:06cf4d2d).
//   .text:06d0e67b .. 06d0e69e  FUN_06d006c0(&ObjId, &obj, &sys); a miss
//                               leaves obj = 0 (.text:06d0e696)
//   per node:
//   .text:06d0e6b4 .. 06d0e6bd  node+0x40 == *SourceClient -> next
//   .text:06d0e6bf              obj == 0 -> next (tested per node, not once)
//   .text:06d0e6c3 .. 06d0e6cc  obj->get_type(type) (IObjInspect +0x88), the
//                               type written over the Target argument's slot
//   .text:06d0e6d2              (type & 0xfdff) != 0 -> send without asking
//   .text:06d0e6dc .. 06d0e6e7  otherwise FUN_06cf73d0(&node->View, obj) must
//                               say visible
//   .text:06d0e6e9 .. 06d0e72d  record {*ObjId, word *SubObjId, Target ?
//                               Target->get_id() : 0, word *SubTarget}
//   .text:06d0e735              IClient +0x10 (SendCommonSetTarget) (node+0x40, &record)
// FUN_06d006c0 is Server_LookupGameObject in this port (see the tracker item
// for 0x06d006c0); it answers null on a miss, which is exactly obj = 0 here.
void StarSystem::notify_set_target(unsigned int const& ObjId, unsigned short const& SubObjId,
                                   IObjRW* Target, unsigned short const& SubTarget,
                                   int const& SourceClient)
{
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (Obj == nullptr)
            continue;
        unsigned int Type = 0u;
        Obj->get_type(Type);
        if ((Type & 0xfdffu) == 0u && !SystemPlayerView_IsObjectVisible(&Node->View, Obj))
            continue;
        XSetTarget Record = {};
        Record.ObjectId = ObjId;
        Record.SubObjId = SubObjId;
        Record.TargetObjectId = (Target != nullptr) ? Target->get_id() : 0u;
        Record.TargetSubObjId = SubTarget;
        g_pClientInterface->SendCommonSetTarget(Node->View.PlayerId, Record);
    }
}

// FUN_06d0f0c0 (.text:06d0f0c0, RET 0xc). Only caller Ship slot_38 (the
// jettison, .text:06d0230c), which pushes (Obj, EquipId, Count).
//   .text:06d0f0c7 .. 06d0f0e2  the owner: CSimple::OwnerPlayer through the
//                               inlined (& 3) == 3 accessor, read unguarded
//   .text:06d0f0eb              ObjId = Obj->get_id()
//   .text:06d0f0ee .. 06d0f12c  owner != 0, owner - 1 < Players.m_MaxPlayers
//                               (DAT_06d90264), slot +0x348 (client_id) != 0 and
//                               slot +0x3e4 (ship_object_id) == ObjId -- the
//                               body of PlayerDB::is_valid_ship_owner, inlined
//   .text:06d0f12e .. 06d0f138  FUN_06d4f100(slot, EquipId, Count, 0), i.e.
//                               PlayerData_RemoveEquipment without the notify
//   .text:06d0f13d .. 06d0f14d  record {ObjId, word EquipId, Count}
//   per node (no player skip here):
//   .text:06d0f157 .. 06d0f166  FUN_06cf7370(&node->View, ObjId)
//   .text:06d0f179              IClient +0x20 (SendCommonJettisonCargo) (node+0x40, &record)
// The record is XJettisonCargo.
void StarSystem::notify_jettison(GameObject* Obj, unsigned short EquipId, int Count)
{
    unsigned int const Owner = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();
    if (Players.is_valid_ship_owner(Owner, ObjId))
        PlayerData_RemoveEquipment(Players[Owner], EquipId, Count, false);

    XJettisonCargo Record = {};
    Record.ObjectId = ObjId;
    Record.ItemId = EquipId;
    Record.Count = static_cast<unsigned int>(Count);

    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (!SystemPlayerView_IsObjectIdVisible(&Node->View, ObjId))
            continue;
        g_pClientInterface->SendCommonJettisonCargo(Node->View.PlayerId, Record);
    }
}

// ===========================================================================
// StarSystem::notify_fire_weapon -- retail FUN_06d0e2e0 (.text:06d0e2e0 ..
// 06d0e4d5, thiscall, RET 0x10). Caller SrvGun::fire_forward (.text:06cedfd6).
// Stack base T = ESP after the three register pushes; the XFireWeaponInfo at
// [T+0x10], its hardpoint vector at [T+0x20].
// ===========================================================================
void StarSystem::notify_fire_weapon(unsigned int const& ObjId, std::vector<unsigned short> const& EquipIds,
                                    Vector const& Target, int SourceClient)
{
    // .text:06d0e2fb .. 06d0e326 -- the record's inlined constructor: id 0,
    // target (FLT_MAX, FLT_MAX, FLT_MAX), the vector built empty by its
    // allocator constructor (import .rdata:06d646fc) -- XFireWeaponInfo().
    // .text:06d0e32c .. 06d0e35c -- then id and target from the arguments.
    XFireWeaponInfo Info;
    Info.ObjectId = ObjId;
    Info.TargetPos = Target;

    // .text:06d0e34a .. 06d0e474 -- VC6 vector<unsigned short>::operator=
    // inlined, from an empty destination: a non-empty source exceeds the zero
    // capacity (.text:06d0e3d5 JA), so the destination gets a new block of
    // exactly size() elements (operator new(size * 2), .text:06d0e451) that the
    // source is copied into (import .rdata:06d646ec), with _Last = _End =
    // block + size (.text:06d0e470 / 06d0e474). An empty source takes the copy
    // path (.text:06d0e386 .. 06d0e3ba) and leaves all three pointers null.
    // std::vector's operator= into an empty vector does the same.
    Info.Hardpoints = EquipIds;

    // .text:06d0e478 .. 06d0e4b5 -- every player view but SourceClient's
    // (node +0x40) that can see the shooter (FUN_06cf7370): IClient slot 0
    // (FLPACKET_COMMON_FIREWEAPON) with the player id and the record.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (Player->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Player->View, ObjId))
            continue;
        g_pClientInterface->SendCommonFireWeapon(Player->View.PlayerId, Info);
    }

    // .text:06d0e4b7 .. 06d0e4bc -- the vector's destructor: its block freed
    // (~XFireWeaponInfo at scope exit; LAB_06d616d8 is the same on unwind).
}

// FUN_06d0c970 (.text:06d0c970, RET 8). See server_sysdb.h for the map.
void StarSystem::complete_dock(GameObject* ShipObj, GameObject* Dock)
{
    ::Ship* const DockingShip = static_cast< ::Ship*>(ShipObj);
    if (DockingShip->DeathSequenceObjId != 0)                   // .text:06d0c978 .. 06d0c985
        return;

    // .text:06d0c993 .. 06d0ca43 -- the ship block (head +0x38), `node ?
    // node->Next : Head`. An owned ship (the inlined CSimple accessor, its
    // +0xb4 read unguarded) is skipped; for the rest the 0x503-narrowed object's
    // CEqObj::get_behavior_interface is asked, with no test on the narrowing.
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->csimple()->OwnerPlayer != 0u)
            continue;
        CObject* const Raw = (Obj != nullptr) ? Obj->Object : nullptr;
        CEqObj* const Other =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CEqObj*>(Raw) : nullptr;
        IBehaviorManager* const Behavior = Other->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        // .text:06d0c9fa .. 06d0ca41 -- block {+0 Dock->get_id() (EDI, vt +0x20),
        // +4 Dock's IObjInspect::get_type out value (vt +0x88), +8 ShipObj->
        // get_id() (EBP, vt +0x20)} (the three calls in retail order: ShipObj id
        // first, then Dock id, then Dock type); event {3, &block} to
        // IBehaviorManager slot 0 (Common LAB_062d3f90).
        unsigned int LeftBlock[3];
        LeftBlock[2] = ShipObj->get_id();
        LeftBlock[0] = Dock->get_id();
        unsigned int DockType;
        Dock->get_type(DockType);
        LeftBlock[1] = DockType;
        FBehaviorManagerEvent LeftEvent;
        LeftEvent.Type = 3;
        LeftEvent.Data = LeftBlock;
        Behavior->Slot00(&LeftEvent);
    }

    // .text:06d0ca48 .. 06d0ca85 -- controller event 0x11.
    unsigned int Msg[3];
    Msg[0] = ShipObj->get_id();
    Msg[1] = Dock->get_id();
    Msg[2] = SystemInfo->NicknameId;
    Server_ControllerDispatch(0, pub::Controller::EMessage::DockComplete, Msg);

    // .text:06d0ca8a .. 06d0cac8 -- every player view hears that the ship left
    // the system for the dock; both ids are fetched afresh per player.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        unsigned int const DockId = Dock->get_id();
        unsigned int const ShipId = ShipObj->get_id();
        SystemPlayerView_OnObjectLeftSystem(&Player->View, &ShipId, &DockId);
    }

    // .text:06d0cacc .. 06d0cad1
    DockingShip->DeathSequenceObjId = Dock->get_id();

    if (ShipObj->csimple()->OwnerPlayer != 0u)                  // .text:06d0cad7 .. 06d0caf5
    {
        // .text:06d0cba2 .. 06d0cbe3 -- a jump gate / jump hole protects the
        // player's ship while it goes through.
        if ((Dock->get_type_bits() & 0x40u) != 0u || (Dock->get_type_bits() & 0x800u) != 0u)
        {
            FInvincibility Cmd;
            Cmd.Type = 2;
            Cmd.bProtect = true;
            Cmd.bFromPlayers = true;
            Cmd.Pad06[0] = 0;
            Cmd.Pad06[1] = 0;
            Cmd.MaxHpLoss = 0.0f;
            ShipObj->set_invincible(Cmd);
        }
        return;
    }

    // The ship's CShip: the raw CBase object under the 0x503 test (null tests
    // included, as retail's inlined accessor has them).
    CObject* const Raw = ShipObj->Object;
    CShip* const Cs =
        (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
            ? static_cast<CShip*>(Raw) : nullptr;

    if (SinglePlayer())                                         // .text:06d0cafb
    {
        Cs->bJumpedOut = 0;                                     // .text:06d0cb27 MOV byte [EAX+0x214],0
        return;
    }

    Cs->enable_collisions_r(false);                             // .text:06d0cb57
    if ((Dock->get_type_bits() & 0x40u) != 0u || (Dock->get_type_bits() & 0x800u) != 0u)
        DockingShip->DeathSequenceTimer = 12.0f;                // .rdata:06d67cc0
    else
        DockingShip->DeathSequenceTimer = 0.0f;                 // .rdata:06d67cc4
}

// FUN_06d0cc00 (.text:06d0cc00, RET 0x10). See server_sysdb.h for the map.
void StarSystem::arrive_ship(GameObject* Obj, GameObject* JumpObj, Vector const& Pos, Matrix const& Orient)
{
    // .text:06d0cc0d .. 06d0cc1c
    unsigned int Bits = 0u;
    if (JumpObj != nullptr)
        Bits = JumpObj->get_type_bits();

    // .text:06d0cc1e .. 06d0cc47 -- the inlined (& 3) CSimple accessor, the
    // +0xb4 read unguarded.
    if (Obj->csimple()->OwnerPlayer == 0u)
    {
        // The 0x503-narrowed object (.text:06d0cc58 .. 06d0cc77 and
        // .text:06d0cc85 .. 06d0cca2), handed on without a null test.
        CObject* const Raw = (Obj != nullptr) ? Obj->Object : nullptr;
        CShip* const Cs =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CShip*>(Raw) : nullptr;

        if ((Bits & 0x40u) != 0u || (Bits & 0x800u) != 0u)          // .text:06d0cc49 / 06d0cc4e
        {
            // .text:06d0cca4 .. 06d0ccac -- PUSH 0x3f000000, EBP (Orient),
            // ESI (Pos), EDI: the jump object argument is Obj itself.
            Cs->jump_in(static_cast<IObjRW*>(Obj), Pos, Orient, 0.5f);
        }
        else if ((Bits & 0x400u) != 0u)                             // .text:06d0cc53
        {
            Cs->airlock_in(Pos, Orient);                            // .text:06d0cc79 .. 06d0cc7d
        }
    }

    // .text:06d0ccb2 .. 06d0cd17 -- the SYSTEM_SWITCH_IN record.
    FPayloadServerSystemSwitchIn Record;
    Record.Field_0x00 = Obj->get_id();                              // .text:06d0ccb6
    Record.Field_0x04 = Bits;                                       // .text:06d0ccd0
    Record.Position = Pos;                                          // .text:06d0ccb9 .. 06d0ccd8
    Server_MatrixToQuat(Record.Orientation, Orient);                // .text:06d0ccc9 .. 06d0ccf1

    // .text:06d0cd10 .. 06d0cd4b -- every player view that can see Obj.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (!SystemPlayerView_IsObjectVisible(&Node->View, Obj))   // FUN_06cf73d0
            continue;
        g_pClientInterface->SendServerSystemSwitchIn(Node->View.PlayerId, Record);   // IClient +0xe8
    }
}

// FUN_06d0d000 (.text:06d0d000, RET 8). See server_sysdb.h for the map.
void StarSystem::remove_ship(GameObject* Obj, PhySys::CreateParms* OutParms)
{
    // .text:06d0d001 .. 06d0d01d -- the inlined (& 3) accessor, read unguarded.
    unsigned int const Owner = Obj->csimple()->OwnerPlayer;

    // .text:06d0d023 .. 06d0d045 -- every view but the owner's.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (Player->View.PlayerId == Owner)
            continue;
        SystemPlayerView_OnObjectDestroyed(&Player->View, Obj, 0);  // FUN_06cf6820
    }

    // .text:06d0d047 .. 06d0d05c
    unsigned int const ObjId = Obj->get_id();
    Server_LookupCacheForget(ObjId);

    // .text:06d0d060 .. 06d0d077 -- `node = node ? node->Next : Head`.
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        if (Node->Data == Obj)
        {
            // .text:06d0d079 .. 06d0d0de -- MetaList::remove, inlined: the
            // head / tail / middle unlink, then FreeList (+0x44) when
            // bUseHeap (+0x40), else operator delete (FUN_06d5ffb0).
            Ships.remove(Node);
            break;
        }
    }

    // .text:06d0d0c4 .. 06d0d0cd / .text:06d0d0e1 .. 06d0d0ea -- both paths.
    Obj->leave_system(OutParms);
}

// FUN_06d0d100 (.text:06d0d100, RET 0x10). See server_sysdb.h for the map.
void StarSystem::add_ship(GameObject* Obj, PhySys::CreateParms* Parms, Vector const& Pos, Matrix const& Orient)
{
    // .text:06d0d11f .. 06d0d138 -- SystemInfo->vtable[0](), the system id.
    Parms->CollisionGroupId = SystemInfo->NicknameId;

    // .text:06d0d13b .. 06d0d158 -- the raw object under the (& 1) test; the
    // writes below do not test the result.
    CObject* const Raw =
        (Obj != nullptr && Obj->Object != nullptr &&
         (static_cast<unsigned int>(Obj->Object->ObjectClass) & 0x1u) == 0x1u)
            ? Obj->Object : nullptr;
    Raw->WorldPosition = Pos;                                       // .text:06d0d158 .. 06d0d176 (+0x2c)
    Raw->Orientation = Orient;                                      // .text:06d0d179 .. 06d0d186 (+0x08)

    // .text:06d0d188 .. 06d0d196 -- IEngine::UpdateInstance(instance, 0, 0).
    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Raw->EngineInstanceId), 0, nullptr);

    Obj->enter_system(this, Parms);                                 // .text:06d0d1a7 vt +0x170

    // .text:06d0d1ad .. 06d0d1d2 -- MetaList append, inlined (FUN_06d11850).
    Ships.append(Obj);

    // .text:06d0d1d5 .. 06d0d1f8 -- the 0x503-narrowed CShip.
    CObject* const ShipRaw = (Obj != nullptr) ? Obj->Object : nullptr;
    CShip* const Cs =
        (ShipRaw != nullptr && (static_cast<unsigned int>(ShipRaw->ObjectClass) & 0x503u) == 0x503u)
            ? static_cast<CShip*>(ShipRaw) : nullptr;

    // .text:06d0d1fa .. 06d0d25b -- CShipCreateInfo (ctor FUN_06cf63f0 plus
    // its inlined tail), filled only when the object is a CShip.
    CShipCreateInfo Info;
    if (Cs != nullptr)
        CShipCreateInfo_Fill(&Info, Cs);                            // FUN_06d07690

    // .text:06d0d25e .. 06d0d28c -- every player view.
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        Info.bMarkedBy = Obj->is_marked_by(Player->View.PlayerId) ? 1 : 0;   // FUN_06cef450
        SystemPlayerView_CreateShipForPlayer(&Player->View, &Info, Obj);    // FUN_06cf6630
    }
    // .text:06d0d290 .. 06d0d360 -- Info's destructor (inlined in retail).
}

// FUN_06d0cee0 (.text:06d0cee0, RET 0xc). See server_sysdb.h for the map.
void StarSystem::launch(GameObject* ShipObj, GameObject* Dock, int LaunchType)
{
    if (ShipObj->csimple()->OwnerPlayer != 0u)                      // .text:06d0cef9 .. 06d0cf01
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",                  // .text:06d0cf70 .. 06d0cf92
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x9f2,
              "StarSystem::launch() called on a player!");
        return;
    }

    // .text:06d0cf03 .. 06d0cf37 -- the dock's CEqObj, or null.
    CObject* const DockRaw = (Dock != nullptr) ? Dock->Object : nullptr;
    CEqObj* const DockEq =
        (DockRaw != nullptr && (static_cast<unsigned int>(DockRaw->ObjectClass) & 0x103u) == 0x103u)
            ? static_cast<CEqObj*>(DockRaw) : nullptr;

    // .text:06d0cf37 .. 06d0cf55 -- the ship's CShip, not tested afterwards.
    CObject* const ShipRaw = (ShipObj != nullptr) ? ShipObj->Object : nullptr;
    CShip* const Cs =
        (ShipRaw != nullptr && (static_cast<unsigned int>(ShipRaw->ObjectClass) & 0x503u) == 0x503u)
            ? static_cast<CShip*>(ShipRaw) : nullptr;

    Cs->launch(static_cast<IObjRW*>(ShipObj), DockEq, LaunchType); // .text:06d0cf5e (PUSH ESI = ShipObj)
    static_cast< ::Ship*>(ShipObj)->apply_launch_invincibility();  // .text:06d0cf66 FUN_06d01110
}

// FUN_06d0df40 (.text:06d0df40, RET 0x14). Callers: IServerImpl::GoTradelane
// (.text:06cf0adf) and Ship go_tradelane (.text:06d02919), both pushing
// (ShipId, EntranceId, ExitId, bFastExit, Speed).
//
// Pass 1, the ship block (+0x34, head +0x38, NULL-terminated; `node ? [node] :
// [this+0x38]` at .text:06d0df60 .. 06d0df6f):
//   * the ship itself (get_id() == ShipId, .text:06d0df7c):
//       .text:06d0df87 .. 06d0dfaf  SkipPlayer = its CSimple::OwnerPlayer
//                                   (the inlined accessor, read unguarded)
//       .text:06d0dfb3 .. 06d0dfce  controller event 0x16 {ShipId, EntranceId,
//                                   ExitId, SystemInfo vt0 = the system id}
//       .text:06d0dfd3 .. 06d0e00a  its CShip (the CBase pointer adjust, then
//                                   Object and (ObjectClass & 0x503) == 0x503):
//                                   CShip::use_tradelane(true)
//     and on to the next node (.text:06d0e010).
//   * every other ship nobody owns (.text:06d0e015 .. 06d0e032) hands its
//     IBehaviorManager (CEqObj::get_behavior_interface on the 0x503-narrowed
//     object, called with no test on the narrowing) a type-4 notification
//     {4, &payload} through its vtable slot 0 (.text:06d0e0af), payload
//       +0x00 byte 1, +0x01 byte 0, +0x02 byte bFastExit, +0x04 float Speed,
//       +0x08 0, +0x0c ShipId, +0x10 EntranceId, +0x14 ExitId
//     (.text:06d0e069 .. 06d0e0a7) -- "this ship entered a lane".
//
// Pass 2 (.text:06d0e0b6 .. 06d0e113): record {ShipId, EntranceId, ExitId,
// byte bFastExit} (XGoTradelane) through IClient +0x18 (SendCommonGoTradeLane) to every
// player except SkipPlayer that FUN_06cf7370 says can see ShipId.
//
// The type-4 notification goes to IBehaviorManager::Slot00 (Common
// LAB_062d3f90, non-virtual in the port) with FBehaviorTradelaneEvent.
void StarSystem::notify_go_tradelane(unsigned int ShipId, unsigned int EntranceId,
                                     unsigned int ExitId, bool bFastExit, float Speed)
{
    unsigned int SkipPlayer = 0u;                               // .text:06d0df55
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->get_id() == ShipId)
        {
            SkipPlayer = Obj->csimple()->OwnerPlayer;
            unsigned int Msg[4];
            Msg[0] = ShipId;
            Msg[1] = EntranceId;
            Msg[2] = ExitId;
            Msg[3] = SystemInfo->NicknameId;                    // .text:06d0dfc1
            Server_ControllerDispatch(0, pub::Controller::EMessage::TradelaneEnter, Msg);
            CObject* const Raw = Obj->Object;
            if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                static_cast<CShip*>(Raw)->use_tradelane(true);
            continue;
        }

        if (Obj->csimple()->OwnerPlayer != 0u)
            continue;
        CObject* const Raw = Obj->Object;
        CEqObj* const Ship =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CEqObj*>(Raw) : nullptr;
        IBehaviorManager* const Behavior = Ship->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        // .text:06d0e069 .. 06d0e0af -- {4, &payload} to IBehaviorManager slot 0
        // (Common LAB_062d3f90); +3 is never written.
        FBehaviorTradelaneEvent Payload;
        Payload.EntranceId = EntranceId;                        // [ESP+0x50]
        Payload.bFastExit = static_cast<unsigned char>(bFastExit); // [ESP+0x42]
        Payload.Speed = Speed;                                  // [ESP+0x44]
        Payload.bExitRequested = 0u;                            // [ESP+0x41] BL
        Payload.bEnteringLane = 1u;                             // [ESP+0x40]
        Payload.Field_0x08 = 0u;                                // [ESP+0x48] EBX
        Payload.ExitId = ExitId;                                // [ESP+0x54] EBP
        Payload.ShipId = ShipId;                                // [ESP+0x4c] EDI
        FBehaviorManagerEvent LaneEvent;
        LaneEvent.Type = 4;
        LaneEvent.Data = &Payload;
        Behavior->Slot00(&LaneEvent);
    }

    XGoTradelane Record = {};
    Record.ObjectId = ShipId;
    Record.TradelaneRing1 = EntranceId;
    Record.TradelaneRing2 = ExitId;
    Record.Field_0x0c = bFastExit;
    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (SkipPlayer == Player->View.PlayerId)
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Player->View, ShipId))
            continue;
        g_pClientInterface->SendCommonGoTradeLane(Player->View.PlayerId, Record);
    }
}

// FUN_06d0e120 (.text:06d0e120, RET 0xc). Callers: IServerImpl::StopTradelane
// (.text:06cf0b3d) and Ship stop_tradelane (.text:06d028b1), pushing
// (ShipId, EntranceId, ExitId).
//
// Pass 1, the ship block, NOT the same shape as FUN_06d0df40's:
//   * the ship itself (.text:06d0e15b): SkipPlayer = its OwnerPlayer,
//     controller event 0x17 {ShipId, EntranceId, ExitId, system id}
//     (.text:06d0e17c .. 06d0e1a6), CShip::use_tradelane(false)
//     (.text:06d0e1d4 PUSH EBX = 0) -- and then it FALLS THROUGH
//     (.text:06d0e1db) into the second test instead of moving on.
//   * every ship (the matched one included) nobody owns and that is not
//     destroyed (GameObject +0x160, .text:06d0e200) hands its IBehaviorManager
//     a type-4 notification {4, &payload} (.text:06d0e285), payload
//       +0x00 byte 0, +0x01 byte 0, +0x02 byte 0, +0x04 0.0f, +0x08 0,
//       +0x0c ShipId, +0x10 EntranceId, +0x14 0
//     (.text:06d0e244 .. 06d0e27d) -- the "left the lane" form.
//
// Pass 2 (.text:06d0e28c .. 06d0e2ca): IClient +0x1c (SendCommonStopTradeLane) (node+0x40,
// ShipId, EntranceId, ExitId) to every player except SkipPlayer that
// FUN_06cf7370 says can see ShipId.
//
// The type-4 notification goes to IBehaviorManager::Slot00 as in
// notify_go_tradelane.
void StarSystem::notify_stop_tradelane(unsigned int ShipId, unsigned int EntranceId,
                                       unsigned int ExitId)
{
    unsigned int SkipPlayer = 0u;                               // .text:06d0e133
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->get_id() == ShipId)
        {
            SkipPlayer = Obj->csimple()->OwnerPlayer;
            unsigned int Msg[4];
            Msg[0] = ShipId;
            Msg[1] = EntranceId;
            Msg[2] = ExitId;
            Msg[3] = SystemInfo->NicknameId;                    // .text:06d0e199
            Server_ControllerDispatch(0, pub::Controller::EMessage::TradelaneExit, Msg);
            CObject* const Raw = Obj->Object;
            if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                static_cast<CShip*>(Raw)->use_tradelane(false);
        }

        if (Obj->csimple()->OwnerPlayer != 0u)                  // .text:06d0e1f2
            continue;
        if (Obj->is_destroyed())                                // .text:06d0e200
            continue;
        CObject* const Raw = Obj->Object;
        CEqObj* const Ship =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CEqObj*>(Raw) : nullptr;
        IBehaviorManager* const Behavior = Ship->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        // .text:06d0e244 .. 06d0e285 -- {4, &payload} to IBehaviorManager slot 0
        // (Common LAB_062d3f90), the "left the lane" form; +3 is never written.
        FBehaviorTradelaneEvent Payload;
        Payload.EntranceId = EntranceId;                        // [ESP+0x40]
        Payload.bFastExit = 0u;                                 // [ESP+0x32] BL
        Payload.Speed = 0.0f;                                   // [ESP+0x34] dword 0
        Payload.ExitId = 0u;                                    // [ESP+0x44] EBX
        Payload.bEnteringLane = 0u;                             // [ESP+0x30] BL
        Payload.bExitRequested = 0u;                            // [ESP+0x31] BL
        Payload.Field_0x08 = 0u;                                // [ESP+0x38] EBX
        Payload.ShipId = ShipId;                                // [ESP+0x3c] EDI
        FBehaviorManagerEvent LaneEvent;
        LaneEvent.Type = 4;
        LaneEvent.Data = &Payload;
        Behavior->Slot00(&LaneEvent);
    }

    SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (SkipPlayer == Player->View.PlayerId)
            continue;
        if (!SystemPlayerView_IsObjectIdVisible(&Player->View, ShipId))
            continue;
        g_pClientInterface->SendCommonStopTradeLane(Player->View.PlayerId, ShipId, EntranceId, ExitId);
    }
}

// FUN_06d0c6d0 (.text:06d0c6d0, RET 4). `TEST EAX,0xfdff / JZ` on
// get_type_bits (.text:06d0c6dc .. 06d0c6e7), then MetaList::append on +0xd4
// inlined (.text:06d0c6e9 .. 06d0c719).
void StarSystem::add_to_list_d4(GameObject* Obj)
{
    if ((Obj->get_type_bits() & 0xfdffu) == 0u)
        return;
    ListD4.append(Obj);
}

// FUN_06d0c730 (.text:06d0c730, RET 4). The search is `node ? [node] : [+0xd8]`
// comparing node+8 with Obj (.text:06d0c74d); the first match is unlinked and
// freed by the inlined MetaList::remove (.text:06d0c752 .. 06d0c7c5). No match:
// nothing.
void StarSystem::remove_from_list_d4(GameObject* Obj)
{
    MetaNode<GameObject>* Node = nullptr;
    while (ListD4.traverse(Node, true))
    {
        if (Node->Data == Obj)
        {
            ListD4.remove(Node);
            return;
        }
    }
}

// ===========================================================================
// StarSystem::find_solar -- retail FUN_06d0c7e0 (.text:06d0c7e0).
// See the declaration in server_sysdb.h for the instruction-level map.
// ===========================================================================
GameObject* StarSystem::find_solar(unsigned int const* NicknameId) const
{
    GameObject* Found = nullptr;                                   // XOR EAX,EAX
    FSystemListNode* Node = nullptr;                               // XOR EDI,EDI
    for (;;) {
        // .text:06d0c7f0 .. .text:06d0c7f9
        Node = (Node == nullptr) ? Solars.Head : Node->Next;    // [this+0x60]
        // .text:06d0c7fb / .text:06d0c7ff
        if (Node == nullptr || Found != nullptr)
            break;
        GameObject* const Obj = Node->Data;
        // .text:06d0c80a .. .text:06d0c827 -- [Data+0x10] is the CBase sub-object's
        // Object (CBase at +0x0c, Object at CBase+0x04), which cobject() returns.
        CObject* const Raw = const_cast<CObject*>(Obj->cobject());
        CSolar* Solar = nullptr;
        if (Raw != nullptr &&
            (static_cast<unsigned int>(Raw->ObjectClass) & 0x303u) == 0x303u)
            Solar = static_cast<CSolar*>(Raw);
        // .text:06d0c82b -- retail reads +0x1a4 even off the zeroed pointer;
        // the solar block never holds anything else.
        if (Solar->Nickname.Id == *NicknameId)
            Found = Obj;                                           // MOV EAX,ESI
    }
    return Found;
}

// ===========================================================================
// StarSystem::on_object_destroyed -- retail FUN_06d0b160 (.text:06d0b160).
// See the declaration in server_sysdb.h for the instruction-level map.
// ===========================================================================
void StarSystem::on_object_destroyed(GameObject* Obj, int Kind)
{
    // (w4-server-guards) No null test on Obj: .text:06d0b16a
    // `MOV EAX,[ESI + 0x10]` is the first read.

    // .text:06d0b16a .. .text:06d0b189 -- the CBase::Object / (ObjectClass & 3)
    // narrowing, then CSimple::OwnerPlayer at +0xb4. Retail reads +0xb4 through
    // the nulled pointer (.text:06d0b182 `XOR EAX,EAX` / 06d0b189
    // `MOV EDI,[EAX+0xb4]`) exactly as StarSystem::remove_player does; the port
    // does not guard it either.
    unsigned int const SessionId = Obj->csimple()->OwnerPlayer;

    // .text:06d0b18f  TEST EDI,EDI / JZ LAB_06d0b237 -- skip the whole player
    // half when the object belongs to nobody.
    if (SessionId != 0)
    {
        // .text:06d0b19f  CALL [EDX + 0x20] -- the object's own id, computed
        // BEFORE the slot lookup and kept in EBX across it.
        unsigned int const ObjId = Obj->get_id();

        // .text:06d0b1a9 .. .text:06d0b1ae -- PlayerDB::is_valid(&Players, &id).
        PlayerSlot* const Slot = PlayerDB_GetPlayer(SessionId);

        // .text:06d0b1c4  CMP dword ptr [ESI + EAX*0x1 + -0x34],EBX
        // -- only when the destroyed object IS the session's current ship.
        if (Slot != nullptr && Slot->ship_object_id == ObjId)
        {
            // .text:06d0b1d1 -- PlayerData::terminate_trade.
            PlayerData_TerminateTrade(*Slot);
            // .text:06d0b1dc  PUSH 0x1 -- the bool argument the decompilation
            // loses as `unaff_BL`; retail asks for the partner's inbound entry
            // to be dropped too.
            PlayerData_CancelTradeRequest(*Slot, /*bDropPartnerEntry*/true);
            // .text:06d0b1ea  CMP EBP,0x1 / JNZ -- only Kind == 1 zeroes the
            // health. PUSH 0 / PUSH 0, i.e. (0.0f, bNotify = false).
            if (Kind == 1)
                PlayerData_SetRelativeHealth(*Slot, 0.0f, /*bNotify*/false);
            // .text:06d0b20a  MOV dword ptr [ESI + EAX*0x1 + -0x34],0x0
            Slot->ship_object_id = 0;
            // .text:06d0b21c/.text:06d0b227 -- a two-dword stack block
            // { slot+0x3E0, sessionId } passed to FUN_06d43650 with id 0x37.
            // +0x3E0 is 0x418 - 0x38, the slot's system id.
            unsigned int Payload[2];
            Payload[0] = Slot->system_id;
            Payload[1] = SessionId;
            Server_ControllerDispatch(0, pub::Controller::EMessage::SpaceExit, Payload);
        }
    }

    // .text:06d0b237 .. .text:06d0b254 -- and then, unconditionally, every
    // player currently in this system is told about it:
    //   MOV  EAX,[EBX + 0xc]      ; the player list sentinel
    //   MOV  ESI,[EAX]            ; first node
    //   LEA  ECX,[ESI + 0x8]      ; the EMBEDDED per-player element
    //   PUSH EBP (Kind) / PUSH Obj / CALL FUN_06cf6820
    //
    // The element, the tracker AND the insert are all real now:
    // StarSystem::add_player (retail FUN_06d07e20) pushes the node, this walk
    // visits it and StarSystem::remove_player takes it off again.
    {
        // (w4-server-guards) No null test on the sentinel or on a node:
        // 06d0b237 `MOV EAX,[EBX+0xc]` / 06d0b23a `MOV ESI,[EAX]` /
        // 06d0b23c `CMP ESI,EAX`, and 06d0b251 `CMP ESI,[EBX+0xc]` -- the walk
        // ends only at the sentinel.
        // The next node is read AFTER the call (06d0b24f `MOV ESI,[ESI]`) and
        // the sentinel is re-read from +0xc on every step (06d0b251).
        for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
             Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next) {
            // .text:06d0b24a  LEA ECX,[ESI+0x8] -- the EMBEDDED element.
            SystemPlayerView_OnObjectDestroyed(&Node->View, Obj, Kind);
        }
    }
}

// ===========================================================================
// StarSystem::add_cargo -- retail FUN_06d0e8d0 (.text:06d0e8d0, RET 0x14).
//
// Stack map, so the argument order below can be checked without re-deriving it:
// after `SUB ESP,0x58` plus PUSH EBX/EBP/ESI/EDI the incoming arguments sit at
// [ESP+0x6c] (Obj), [ESP+0x70] (ArchId), [ESP+0x74] (Count), [ESP+0x78]
// (Health) and [ESP+0x7c] (the bool) -- which is why .text:06d0e937 reads
// [ESP+0x70] to compare against the equip's archetype id and .text:06d0e8f9
// reads [ESP+0x7c] as the byte flag.
// ===========================================================================
// Retail FUN_06ce6870 (.text:06ce6870), the narrowing both cargo paths use:
//   MOV EAX,[ECX + 0x10]                 ; CBase::Object
//   TEST EAX,EAX / JZ null
//   MOV ECX,[EAX + 0x4c] / AND ECX,0x103 / CMP ECX,0x103 / JZ ok
//   null: XOR EAX,EAX
// It is a FREE function on the raw object pointer in retail, which is why
// .text:06d0e903 and .text:06d0ec06 can call it with a plain GameObject* in
// ECX. This port's EqObj::ceqobj() is the same test as a member, so it cannot
// be reached from a GameObject*; this is that member's free-function twin.
static CEqObj* GameObject_ceqobj(GameObject* Obj)
{
    CObject* const Raw = (Obj != nullptr) ? Obj->cobj() : nullptr;
    if (Raw == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Raw->ObjectClass) & 0x103u) != 0x103u)
        return nullptr;
    return static_cast<CEqObj*>(Raw);
}

unsigned short StarSystem::add_cargo(GameObject* Obj, unsigned int ArchId,
                                     unsigned int Count, float Health, bool bMission)
{
    // No null test on Obj: retail's first use is .text:06d0e903 / .text:06d0e953
    // `MOV EAX,[EDI + 0x10]` straight through the argument.
    CECargo* Stack = nullptr;
    bool bCombined = false;

    // .text:06d0e8e8  CALL [->COMMON.DLL::arch_is_combinable] / TEST AL,AL
    // .text:06d0e8f9  MOV AL,[ESP+0x7c] / TEST AL,AL / JNZ -- the flag SUPPRESSES
    // the combine, so an item marked with it always becomes its own entry.
    if (arch_is_combinable(ArchId) && !bMission)
    {
        // .text:06d0e903 FUN_06ce6870, then .text:06d0e923 `LEA EBP,[ESI+0xe4]`
        // with no test of the result in between.
        CEqObj* const EqObj = GameObject_ceqobj(Obj);
        {
            // .text:06d0e908  PUSH 0x10000 -- EquipTypeCargo, nothing else.
            CEquipTraverser Walk(EquipTypeCargo);
            CEquipManager& Mgr = EqObj->EquipManager;  // .text:06d0e923 LEA EBP,[ESI+0xe4]
            for (CEquip* Item = Mgr.Traverse(Walk); Item != nullptr;
                 Item = Mgr.Traverse(Walk))
            {
                // .text:06d0e934  MOV EDX,[ESI+0xc] / CMP [EDX+8],EAX
                // CEquip+0x0c is Arch and Archetype::Root+0x08 is Nickname,
                // whose Id is what get_id() returns.
                // .text:06d0e934 MOV EDX,[ESI+0xc] / .text:06d0e93b CMP [EDX+0x8],EAX
                // -- no test of Arch in between.
                if (Item->Arch->get_id() != ArchId)
                    continue;

                // .text:06d0e9df  MOV EAX,[ESI+0x28] -- CECargo::Count. Retail
                // uses the traverser's own 0x10000 mask as the downcast proof
                // and reads +0x28 with no Cast call at all; the port goes
                // through CECargo::cast, which cannot disagree for that mask.
                CECargo* const Cargo = CECargo::cast(Item);
                if (Cargo != nullptr && Cargo->Count != 0)
                {
                    // .text:06d0e9f2  CECargo::AddToStack(Count, Health)
                    Cargo->AddToStack(Count, Health);
                    Stack = Cargo;
                    bCombined = true;
                }
                else
                {
                    // .text:06d0ea81  CEquipManager::CleanUp, then fall out of
                    // the combine block into the "make a new one" path below.
                    Mgr.CleanUp();
                }
                break;
            }
        }
    }

    // The owning player's slot, resolved the same way on both paths
    // (.text:06d0e953 and .text:06d0e9fc): CBase::Object under the
    // (ObjectClass & 3) == 3 guard, CSimple::OwnerPlayer at +0xb4, the slot's
    // client id at +0x348 non-zero, and the slot's ship object id (+0x3E4)
    // equal to this object's own get_id().
    // .text:06d0e965 XOR EAX,EAX on the failed guard, then .text:06d0e969
    // `MOV ESI,[EAX + 0xb4]` through it -- retail reads OwnerPlayer unguarded.
    unsigned int const SessionId = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();
    PlayerSlot* Slot = (SessionId != 0) ? PlayerDB_GetPlayer(SessionId) : nullptr;
    if (Slot != nullptr && (Slot->client_id == 0 || Slot->ship_object_id != ObjId))
        Slot = nullptr;

    if (bCombined)
    {
        // .text:06d0e9c1 -- FUN_06d4f230(slot, ArchId, Count, Health, 0, 0).
        // That forwarder is PlayerData::add_equipment with the cargo-bay
        // hardpoint baked in: .text:06d4f230 pushes, right to left,
        // (arg1, EquipDesc::CARGO_BAY_HP_NAME, arg2, arg3, 0, arg4, arg5) into
        // FUN_06d4efc0. Note the combine path forces BOTH trailing bytes to
        // zero -- bNoCombine and bNotify alike.
        if (Slot != nullptr)
        {
            PlayerData_AddEquipment(*Slot, ArchId, EquipDesc::CARGO_BAY_HP_NAME,
                                    (int)Count, Health, /*bEquipped*/false,
                                    /*bNoCombine*/false, /*bNotify*/false);
        }
    }
    else
    {
        // .text:06d0e9c1 on the other side of the branch:
        // FUN_06d4f230(slot, ArchId, Count, Health, bMission, 0) -- here the
        // public bool DOES travel on as bNoCombine.
        unsigned short EquipId = SUBOBJ_ID_NONE;
        if (Slot != nullptr)
        {
            EquipId = PlayerData_AddEquipment(*Slot, ArchId,
                                              EquipDesc::CARGO_BAY_HP_NAME,
                                              (int)Count, Health, /*bEquipped*/false,
                                              /*bNoCombine*/bMission, /*bNotify*/false);
        }
        else
        {
            // .text:06d0ea8e  MOV ECX,EDI / .text:06d0ea90 CALL FUN_06d01c30 --
            // Ship::allocate_equip_id straight on the GameObject*, reached from
            // all four slot-lookup misses (.text:06d0e97e/98f/9a9/9bb) with no
            // class test: retail assumes anything reaching here is a Ship.
            EquipId = static_cast<Ship*>(Obj)->allocate_equip_id();
        }

        // .text:06d0ea95 .. .text:06d0eae9 -- the EquipDesc retail hands to
        // CEqObj::add_item, built in retail's own order.
        EquipDesc Desc;
        Desc.set_id(EquipId);
        Desc.set_arch_id(ArchId);
        Desc.set_count((int)Count);
        Desc.set_status(Health);
        Desc.set_equipped(false);
        // .text:06d0eadd  MOV CL,byte ptr [ESP+0x7c]
        // .text:06d0eae1  MOV byte ptr [ESP+0x40],CL
        // [ESP+0x40] is EquipDesc+0x18 (the descriptor sits at [ESP+0x28] in the
        // same frame), and EquipDesc+0x18 is bMission. So the in-space path DOES
        // put pub::Player::AddCargo's fifth argument into EquipDesc::bMission --
        // by a direct byte store, which is why Server.dll imports no setter for
        // it and why an earlier note concluded the field was never written.
        Desc.bMission = bMission;
        // .text:06d0eae9  EquipDesc::make_internal
        Desc.make_internal();

        // .text:06d0eaff  CALL dword ptr [EDX + 0xb0] on the CEqObj.
        // ??_7CEqObj@@6B@ is Common.dll .rdata:0639be5c, so +0xb0 is
        // .rdata:0639bf0c == CEqObj::add_item.
        // .text:06d0eaf1 FUN_06ce6870, .text:06d0eaff CALL [EDX + 0xb0] -- no
        // test of the narrowed pointer, and none again at .text:06d0eb0c.
        CEqObj* const EqObj = GameObject_ceqobj(Obj);
        if (EqObj->add_item(Desc))
        {
            // .text:06d0eb19/.text:06d0eb20 -- FindByID then CECargo::cast.
            Stack = CECargo::cast(EqObj->EquipManager.FindByID(EquipId));
        }
    }

    // .text:06d0eb2d .. .text:06d0ebc2 -- the broadcast. A SECOND, freshly
    // constructed EquipDesc is filled by the cargo equip's own
    // GetEquipDesc (vtable +0x28 on ??_7CECargo@@6B@, Common.dll
    // .rdata:0639cd4c + 0x28 == .text:CECargo::GetEquipDesc), and when either
    // the cargo or that call is missing retail FDUMPs
    //   "StarSys::add_cargo().  Failed to get client cargo description."
    // and sends nothing.
    EquipDesc Wire;
    // .text:06d0eb3d / .text:06d0eb4e -- either miss jumps to the FDUMP at
    // .text:06d0eba0 (level 0x100001, StarSys.cpp line 0xd8e) and sends nothing.
    bool const bHaveDesc = (Stack != nullptr) && Stack->GetEquipDesc(Wire);
    if (bHaveDesc)
    {
        // .text:06d0eb50 .. .text:06d0eb97 -- for every player in this system
        // that can currently see the object, IClient slot 86 (+0x158) with
        // (node+0x40, Obj->get_id(), &Wire). The visibility test is
        // FUN_06cf73d0 on the node's EMBEDDED element at +0x08.
        // StarSystem::add_player fills the list now, so this walk has
        // something to visit.
        // .text:06d0eb54 MOV EAX,[EBX+0xc] / MOV ESI,[EAX] / CMP ESI,EAX, and
        // .text:06d0eb92 MOV ESI,[ESI] / CMP ESI,[EBX+0xc]: head->next until the
        // sentinel, no null tests, and DAT_06d8fb14 is loaded untested
        // (.text:06d0eb6d).
        SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);
        {
            for (SystemPlayerNode* Node = Sentinel->Next;
                 Node != Sentinel; Node = Node->Next) {
                if (!SystemPlayerView_IsObjectVisible(&Node->View, Obj))
                    continue;
                // CITATION CORRECTED 2026-09-17 (wave 13): the CALL is at
                // .text:06d0eb8c, not .text:06d0eb8a -- that address is not an
                // instruction start at all. The whole sequence:
                //   .text:06d0eb7b  52            PUSH EDX            ; the EquipDesc
                //   .text:06d0eb7e  ff5020        CALL [EAX + 0x20]   ; get_id()
                //   .text:06d0eb81  50            PUSH EAX
                //   .text:06d0eb82  8d4e40        LEA  ECX,[ESI + 0x40]
                //   .text:06d0eb85  51            PUSH ECX
                //   .text:06d0eb8c  ff9558010000  CALL [EBP + 0x158]  ; slot 86
                // The first argument really is the ADDRESS of the id word, and
                // node+0x40 is element+0x38.
                g_pClientInterface->SendServerObjectCargoUpdate(Node->View.PlayerId,
                                                                Obj->get_id(), Wire);
            }
        }
    } else {
        // .text:06d0eba0 .. .text:06d0ebc2 -- FDUMP(0x100001, .rdata:06d655d8,
        // .rdata:06d67d10 = StarSys.cpp, 0xd8e, .rdata:06d682cc), through the
        // import with no null test.
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0xd8e,
              "StarSys::add_cargo().  Failed to get client cargo description.");
    }

    // .text:06d0ebc9  MOV AX,word ptr [EBP + 0x8] -- CEquip::Id, or
    // .text:06d0ebd7  *SUBOBJ_ID_NONE when nothing was created.
    return (Stack != nullptr) ? Stack->Id : SUBOBJ_ID_NONE;
}

// ===========================================================================
// StarSystem::remove_cargo -- retail FUN_06d0ebf0 (.text:06d0ebf0, RET 0x0C).
// ===========================================================================
void StarSystem::remove_cargo(GameObject* Obj, unsigned short EquipId, int Count)
{
    // .text:06d0ec03 .. .text:06d0ec1b -- FUN_06ce6870 (the & 0x103 narrowing),
    // then EquipManager (+0xe4).FindByID(id) with no test on the narrowing; only
    // FindByID's result is tested.
    CEqObj* const EqObj = GameObject_ceqobj(Obj);
    CEquip* const Item = EqObj->EquipManager.FindByID(EquipId);
    if (Item == nullptr)
        return;

    // .text:06d0ec22  CECargo::cast -- unlike add_cargo, the remove path really
    // does call it, because FindByID is not filtered by type.
    CECargo* const Cargo = CECargo::cast(Item);
    if (Cargo == nullptr)                                           // .text:06d0ec2f
        return;
    // .text:06d0ec35  MOV EAX,[ESI+0x28] / TEST / JZ -- an empty stack is left
    // alone entirely.
    if (Cargo->Count == 0)
        return;

    // .text:06d0ec40 .. .text:06d0ec53 -- one EquipDesc (EquipDesc::EquipDesc),
    // first filled from the cargo (CECargo vtable +0x28 == GetEquipDesc)...
    EquipDesc Wire;
    Cargo->GetEquipDesc(Wire);

    // .text:06d0ec5a  CECargo::RemoveFromStack(Count). The decompilation passes
    // param_2 here; the raw bytes load [ESP+0x40], the THIRD argument, at
    // .text:06d0ec56.
    Cargo->RemoveFromStack((unsigned)Count);

    // ...and then patched with what is left (.text:06d0ec63 set_count from the
    // live +0x28, .text:06d0ec75 set_status from CECargo vtable +0x44, which
    // ??_7CECargo@@6B@ resolves to CEquip::GetRelativeHealth).
    Wire.set_count((int)Cargo->Count);
    Wire.set_status((float)Cargo->GetRelativeHealth());           // .text:06d0ec7d FSTP float

    // .text:06d0ec86 .. .text:06d0ece6 -- the (& 3) narrowing, not null-tested,
    // for OwnerPlayer (+0xb4); get_id() (vt +0x20) before the test; then
    // PlayerDB::is_valid_ship_owner inlined (owner != 0, owner - 1 below
    // DAT_06d90264, client_id +0x348 set, ship +0x3e4 == id) and
    // FUN_06d4f100(slot, EquipId, Count, 0) == PlayerData_RemoveEquipment with
    // bNotify clear.
    unsigned int const SessionId = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();
    if (SessionId != 0u && Players.is_valid_ship_owner(SessionId, ObjId))
        PlayerData_RemoveEquipment(Players[SessionId], EquipId, Count, /*bNotify*/false);

    // .text:06d0eceb .. .text:06d0ed2e -- every player node (sentinel re-read
    // from +0xc each step), gated on FUN_06cf73d0 (the object visibility test),
    // IClient slot 86 (+0x158).
    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(PlayerListHead); Node = Node->Next) {
        // .text:06d0ecf9  LEA ECX,[ESI+0x8] / CALL FUN_06cf73d0.
        if (!SystemPlayerView_IsObjectVisible(&Node->View, Obj))
            continue;
        // .text:06d0ed12 .. .text:06d0ed23 -- three arguments, pushed across the
        // get_id call: (&element.PlayerId, Obj->get_id(), &Wire). The `PUSH ECX`
        // at .text:06d0ed12 is the THIRD argument going down early, not an
        // argument to get_id; `LEA EAX,[ESI+0x40]` hands over the ADDRESS.
        g_pClientInterface->SendServerObjectCargoUpdate(Node->View.PlayerId, Obj->get_id(), Wire);
    }
}

// ===========================================================================
// StarSystem::unload — retail FUN_06d072c0(this), 0x06d072c0. Run only on the
// single-player path, after event 0x3d, when the last player leaves a system
// (FUN_06d07130 .text:06d07161, FUN_06d087f0 .text:06d0887d, FUN_06d08890
// .text:06d0892d). See the declaration for the whole order; traced against the
// listing 2026-09-24 (claude-code:w3-starsystem-iobjdb).
// ===========================================================================

// Retail's per-object disposal, the same four instructions at every site
// (.text:06d072f7 .. 06d07306 and its siblings):
//   MOV ECX,[EAX + 0x8] / TEST / JZ  -- node->Data, skipped when null
//   MOV EDX,[ECX + 0xc] / ADD ECX,0xc / PUSH 1 / CALL [EDX]
// i.e. the scalar deleting destructor of the wrapper's CBase sub-object at
// +0x0c (IObjInspectImpl's CBase, server_gameobject.h), which destroys and
// frees the whole wrapper.
static void StarSystem_DeleteObject(GameObject* Obj)
{
    delete static_cast<CBase*>(Obj);
}

// The two passes retail runs over one block (for +0x34 at .text:06d072dc ..
// 06d07335; +0x48, +0x70, +0x84, +0x98, +0xac repeat the shape verbatim, and
// FUN_06d07570 / FUN_06d075f0 do the same for +0x5c / +0xc0):
//   pass 1: node = node ? node->Next : Head; delete node->Data when non-null;
//   pass 2: node = Head; while (node) { next = node->Next;
//           FUN_06d0f970(node) (MetaList::free_node); node = next; }
//           then Tail = 0, Head = 0 (.text:06d07332 / 06d07335).
// Pass 1 does not clear Data; pass 2 never looks at it.
static void StarSystem_DeleteBlock(ObjectList& List)
{
    FSystemListNode* Node = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? List.Head : Node->Next;
        if (Node == nullptr)
            break;
        if (Node->Data != nullptr)
            StarSystem_DeleteObject(Node->Data);
    }

    Node = List.Head;
    while (Node != nullptr) {
        FSystemListNode* const Current = Node;
        Node = Node->Next;
        List.free_node(Current);
    }
    List.Tail = nullptr;
    List.Head = nullptr;
}

void StarSystem::unload()
{
    // .text:06d072c7 .. 06d072d7 -- FUN_06d0f390: std::list::erase(begin(),
    // end()) on the player list at +0x08. Per node (.text:06d0f3a2 ..
    // 06d0f3cd): unlink, the view's own destructor through its vtable slot 0
    // with flag 0, operator delete on the node, --size.
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);
    SystemPlayerNode* View = Sentinel->Next;
    while (View != Sentinel) {
        SystemPlayerNode* const Node = View;
        View = View->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        // .text:06d0f3b2 LEA ECX,[ESI+0x8] / MOV EDX,[ECX] / PUSH 0 /
        // .text:06d0f3bc CALL [EDX] -- vptr slot 0, flag 0.
        Observer* const Element = &Node->View;
        Element->~Observer();
        ::operator delete(Node);
        --PlayerListSize;
    }

    // .text:06d072dc .. .text:06d074ff -- six blocks in this order.
    StarSystem_DeleteBlock(Ships);     // +0x34
    StarSystem_DeleteBlock(Loot);      // +0x48
    StarSystem_DeleteBlock(List70);    // +0x70
    StarSystem_DeleteBlock(Beams);     // +0x84
    StarSystem_DeleteBlock(List98);    // +0x98
    StarSystem_DeleteBlock(ListAC);    // +0xac

    // .text:06d074f0 .. .text:06d0753b -- +0xe8: a forward traverse
    // (FUN_06d16120(block, &it, 1)) deleting every non-null Data, then
    // MetaList::free (FUN_06d17170).
    {
        FSystemListNode* Node = nullptr;
        while (Deferred.traverse(Node, true)) {
            if (Node->Data != nullptr)
                StarSystem_DeleteObject(Node->Data);
        }
        Deferred.free();
    }

    // .text:06d07542 -- FUN_06d07570: the solars, then the partition.
    StarSystem_DeleteBlock(Solars);                                // .text:06d07576 .. 06d075c9
    if (Spatial != nullptr) {                                      // .text:06d075d0 / 06d075d5
        FreeSpatial(Spatial);                                      // .text:06d075d8
        Spatial = nullptr;                                         // .text:06d075e1
    }

    // .text:06d07549 -- FUN_06d075f0. SystemInfo->vtable[0]() is the system
    // id (this port's flat ISystem keeps it in NicknameId, common_universe.h).
    unsigned int const SystemId = SystemInfo->NicknameId;
    // .text:06d075fd FUN_06d03410(sysId): drop one reference on this system's
    // SrvAsteroid::SrvAsteroidSystem (the map at DAT_06d8d980, erased at zero).
    // The return value is dropped (no test of EAX before .text:06d0760a).
    SrvAsteroid::SrvAsteroid_UnloadSystem(SystemId);               // .text:06d075fd
    CmnAsteroid::Unload(SystemId);                                 // .text:06d0760d
    StarSystem_DeleteBlock(ListC0);                                // .text:06d07616 .. 06d07680

    // .text:06d0754e / .text:06d07551 -- FUN_06d0f2f0 on +0x24: erase(begin,
    // end) of a vector of 0xC-byte PODs, i.e. _Last = _First.
    VectorEnd = VectorBegin;

    // .text:06d07556 .. .text:06d0755e -- the id again, PhySys::Shutdown(id).
    PhySys::Shutdown(SystemInfo->NicknameId);
}

// ===========================================================================
// StarSystem's constructors, destructor and SysDB init.
// ===========================================================================

// FUN_06d0f3e0(0, 0) (.text:06d0f3e0): operator new(0x50) with both links on
// the node itself -- the player list's sentinel. 0x50 is a full node
// (SystemPlayerNode): MSVC6's _Buynode allocates the value too.
static void* StarSystem_NewPlayerSentinel()
{
    SystemPlayerNode* const Node =
        static_cast<SystemPlayerNode*>(::operator new(sizeof(SystemPlayerNode)));
    Node->Next = Node;
    Node->Prev = Node;
    return Node;
}

// .text:06d06d26 .. 06d06d36 (constructor) and FUN_06d10aa0 (copy): operator
// new(0x20), self-linked -- the dock queue's sentinel.
static void* StarSystem_NewDockSentinel()
{
    SystemDockNode* const Node =
        static_cast<SystemDockNode*>(::operator new(sizeof(SystemDockNode)));
    Node->Next = Node;
    Node->Prev = Node;
    return Node;
}

// Retail's node is 0x50 bytes. The port's Observer embeds a real
// std::set whose size depends on the CRT build (server_sysdb.h), so the port
// allocates sizeof(SystemPlayerNode) rather than asserting 0x50.

// FUN_06d06ce0 (.text:06d06ce0).
//   06d06d08  vptr = IObjDB (the base-class constructor, inlined)
//   06d06d15  +0x08 allocator byte; 06d06d17 FUN_06d0f3e0(0,0) -> +0x0c; +0x10 = 0
//   06d06d2c  +0x14 allocator byte; 06d06d2f operator new(0x20) self-linked
//             -> +0x18; +0x1c = 0
//   06d06d46  +0x20 = 0; 06d06d49 +0x24 allocator byte; +0x28/+0x2c/+0x30 = 0
//   06d06d55 .. 06d06e16  the ten ObjectLists, each bUseHeap = 1 (ObjectList())
//   06d06e20  +0xfc = 0
//   06d06e26  vptr = StarSystem
// The allocator bytes are copies of an uninitialised stack byte (MSVC6's
// empty std::allocator) and carry no value; +0x04 is not written at all --
// FUN_06d06ea0 sets it on the map's copy before anything reads it. The port
// writes zero to all of them rather than copy indeterminate memory.
StarSystem::StarSystem()
    : SystemInfo(nullptr),
      ListAlloc(0),
      PlayerListHead(StarSystem_NewPlayerSentinel()),
      PlayerListSize(0),
      ObjectListAlloc(0),
      ObjectListHead(StarSystem_NewDockSentinel()),
      ObjectListSize(0),
      Spatial(nullptr),
      VectorAlloc(0),
      VectorBegin(nullptr),
      VectorEnd(nullptr),
      VectorCapacity(nullptr),
      bLoaded(0)
{
    Pad1[0] = Pad1[1] = Pad1[2] = 0;
    Pad2[0] = Pad2[1] = Pad2[2] = 0;
    Pad3[0] = Pad3[1] = Pad3[2] = 0;
    Pad4[0] = Pad4[1] = Pad4[2] = 0;
}

// FUN_06d10520 (.text:06d10520, `RET 0x4`); FUN_06d11330 inlines the same body
// for the map node.
//   06d10542  vptr = IObjDB
//   06d10548  +0x04 = Other+0x04
//   06d1054e  +0x08 allocator byte copied; 06d10560 new sentinel; +0x10 = 0;
//   06d10590  FUN_06d109c0: insert(end, Other.begin, Other.end) -- a
//             push_back of a COPY (FUN_06d0f6d0) of every player view
//   06d10595  the same for the dock queue: new 0x20 sentinel (FUN_06d10aa0),
//   06d105d3  FUN_06d10a30: push_back of a copy (FUN_06d0f830) of each entry
//   06d105d8  +0x20 = Other+0x20 (the partition POINTER, shared)
//   06d105de  +0x24 allocator byte; 06d105e6 .. 06d10634 vector copy: n = 0
//             when Other._First is null, else (_Last - _First) / 0xC, clamped
//             at 0; operator new(n * 0xC) -- even for n == 0 --, the elements
//             copied (FUN_06d10ad0), _Last = _End = the end of the copy
//   06d10637 .. 06d1072c  the ten ObjectLists EMPTY, bUseHeap = 0
//             (ObjectList(ObjectList const&) -- nothing of Other's is copied)
//   06d10736  +0xfc = Other+0xfc
//   06d1073d  vptr = StarSystem
StarSystem::StarSystem(StarSystem const& Other)
    : IObjDB(),
      SystemInfo(Other.SystemInfo),
      ListAlloc(Other.ListAlloc),
      PlayerListHead(StarSystem_NewPlayerSentinel()),
      PlayerListSize(0),
      ObjectListAlloc(Other.ObjectListAlloc),
      ObjectListHead(StarSystem_NewDockSentinel()),
      ObjectListSize(0),
      Spatial(Other.Spatial),
      VectorAlloc(Other.VectorAlloc),
      VectorBegin(nullptr),
      VectorEnd(nullptr),
      VectorCapacity(nullptr),
      Ships(Other.Ships),
      Loot(Other.Loot),
      Solars(Other.Solars),
      List70(Other.List70),
      Beams(Other.Beams),
      List98(Other.List98),
      ListAC(Other.ListAC),
      ListC0(Other.ListC0),
      ListD4(Other.ListD4),
      Deferred(Other.Deferred),
      bLoaded(Other.bLoaded)
{
    Pad1[0] = Pad1[1] = Pad1[2] = 0;
    Pad2[0] = Pad2[1] = Pad2[2] = 0;
    Pad3[0] = Pad3[1] = Pad3[2] = 0;
    Pad4[0] = Pad4[1] = Pad4[2] = 0;

    // FUN_06d109c0 (.text:06d109e0 .. 06d10a21): for each source node,
    // operator new(0x50), Next = end, Prev = end->Prev (or itself), end->Prev
    // = node, node->Prev->Next = node, the element copy-constructed at node+8,
    // ++size.
    {
        SystemPlayerNode* const End = static_cast<SystemPlayerNode*>(PlayerListHead);
        SystemPlayerNode* const SrcEnd = static_cast<SystemPlayerNode*>(Other.PlayerListHead);
        for (SystemPlayerNode* Src = SrcEnd->Next; Src != SrcEnd; Src = Src->Next) {
            SystemPlayerNode* const Node =
                static_cast<SystemPlayerNode*>(::operator new(sizeof(SystemPlayerNode)));
            Node->Next = End;
            Node->Prev = (End->Prev != nullptr) ? End->Prev : Node;
            End->Prev = Node;
            Node->Prev->Next = Node;
            ::new (static_cast<void*>(&Node->View)) Observer(Src->View);   // FUN_06d0f6d0
            ++PlayerListSize;
        }
    }

    // FUN_06d10a30 (.text:06d10a50 .. 06d10a91): the same over 0x20-byte nodes
    // with FUN_06d0f830, FSystemDockEntry's copy constructor.
    {
        SystemDockNode* const End = static_cast<SystemDockNode*>(ObjectListHead);
        SystemDockNode* const SrcEnd = static_cast<SystemDockNode*>(Other.ObjectListHead);
        for (SystemDockNode* Src = SrcEnd->Next; Src != SrcEnd; Src = Src->Next) {
            SystemDockNode* const Node =
                static_cast<SystemDockNode*>(::operator new(sizeof(SystemDockNode)));
            Node->Next = End;
            Node->Prev = (End->Prev != nullptr) ? End->Prev : Node;
            End->Prev = Node;
            Node->Prev->Next = Node;
            ::new (static_cast<void*>(&Node->Entry)) FSystemDockEntry(Src->Entry);
            ++ObjectListSize;
        }
    }

    // .text:06d105e6 .. 06d10634 -- the 0xC-byte-element vector.
    int Count = 0;
    if (Other.VectorBegin != nullptr) {
        Count = static_cast<int>(
            (static_cast<char const*>(Other.VectorEnd) -
             static_cast<char const*>(Other.VectorBegin)) / 0xC);
    }
    if (Count < 0)
        Count = 0;
    VectorBegin = ::operator new(static_cast<size_t>(Count) * 0xC);
    size_t const Bytes = static_cast<size_t>(
        static_cast<char const*>(Other.VectorEnd) - static_cast<char const*>(Other.VectorBegin));
    if (Bytes != 0)
        memcpy(VectorBegin, Other.VectorBegin, Bytes);
    VectorEnd = static_cast<char*>(VectorBegin) + Bytes;
    VectorCapacity = VectorEnd;
}

// FUN_06d10030 (.text:06d10030), from `LEA ESI,[ECX+0x4]` on -- the pair's
// destructor is this one inlined. In order:
//   06d10053 .. 06d1017b  +0xe8 down to +0x34: each ObjectList's destructor
//                         (vptr MetaList, MetaList::free, MetaList::free_heap)
//                         -- the members' own destructors, reverse order
//   06d10180 .. 06d1019b  operator delete(+0x28); +0x28/+0x2c/+0x30 = 0
//   06d1019e .. 06d101cf  the dock queue: FUN_06d0f410 erase(begin, end),
//                         operator delete on the sentinel, +0x18 = +0x1c = 0
//   06d101da              FUN_06d0f230: the player list -- every view's
//                         destructor (slot 0, flag 0) and node freed, then the
//                         sentinel, +0x0c = +0x10 = 0
// Nothing deletes the objects the ObjectLists point at, and +0x20 (the
// partition) is not freed here: unload() does both.
StarSystem::~StarSystem()
{
    ::operator delete(VectorBegin);
    VectorBegin = nullptr;
    VectorEnd = nullptr;
    VectorCapacity = nullptr;

    SystemDockNode* const DockHead = static_cast<SystemDockNode*>(ObjectListHead);
    erase_docks(DockHead->Next, DockHead);
    ::operator delete(ObjectListHead);
    ObjectListHead = nullptr;
    ObjectListSize = 0;

    // FUN_06d0f230 (.text:06d0f240 .. 06d0f284).
    SystemPlayerNode* const Sentinel = static_cast<SystemPlayerNode*>(PlayerListHead);
    SystemPlayerNode* Node = Sentinel->Next;
    while (Node != Sentinel) {
        SystemPlayerNode* const Current = Node;
        Node = Node->Next;
        Current->Prev->Next = Current->Next;
        Current->Next->Prev = Current->Prev;
        // .text:06d0f250 LEA ECX,[ESI+0x8] / MOV EDX,[ECX] / PUSH 0 /
        // .text:06d0f25a CALL [EDX] -- vptr slot 0, flag 0.
        Observer* const Element = &Current->View;
        Element->~Observer();
        ::operator delete(Current);
        --PlayerListSize;
    }
    ::operator delete(PlayerListHead);
    PlayerListHead = nullptr;
    PlayerListSize = 0;
    // The ten ObjectLists are destroyed after this body, +0xe8 first. Retail
    // runs them BEFORE the vector/dock/player teardown (.text:06d10053 ..
    // 06d1017b come first). The order is not observable: a block's destructor
    // frees only its own nodes and never touches the objects, the vector or
    // either std::list.
}

// FUN_06d06ea0 (.text:06d06ea0, `RET 0x4`).
//   06d06ea8  +0x04 = COMMON.DLL::Universe::get_system(SystemId)
//   06d06eb4  if (!SinglePlayer())
//   06d06ebf    COMMON.DLL::PhySys::m_pCollisionFilter = LAB_06d06c30
// LAB_06d06c30 (.text:06d06c30 .. 06d06cc9, cdecl, plain RET) -- the
// multiplayer collision filter StarSystem::init installs into
// COMMON.DLL::PhySys::m_pCollisionFilter. Every branch, from the bytes:
//   06d06c32  SinglePlayer() -> TRUE
//   06d06c40  A == null -> TRUE;  06d06c48  B == null -> TRUE
//   06d06c50  ClientA = A->client_data (+0xa4); when it is null and A's +0x1c
//             is not, TRUE (06d06c5a .. 06d06c5f)
//   06d06c61  the same for B (06d06c6b .. 06d06c70)
//   06d06c7d  GetRoot(ClientA) when ClientA != null -- the result is
//             discarded: ESI still holds client_data afterwards
//   06d06c8d  GetRoot(ClientB) when ClientB != null -- likewise discarded;
//             EDI is re-read from B+0xa4 at 06d06c83
//   06d06c98  ClientA kept only when (ObjectClass(+0x4c) & 0x27) == 0x27
//   06d06ca8  the same for ClientB
//   06d06cb5  either survived -> TRUE (06d06cc2); neither -> FALSE (06d06cbe)
static IVP_BOOL __cdecl StarSystem_MpCollisionFilter(IVP_Real_Object* A, IVP_Real_Object* B)
{
    if (SinglePlayer())
        return IVP_TRUE;
    if (A == nullptr)
        return IVP_TRUE;
    if (B == nullptr)
        return IVP_TRUE;

    CObject* ClientA = static_cast<CObject*>(A->client_data);
    if (ClientA == nullptr && A->first_child != nullptr)
        return IVP_TRUE;
    if (B->client_data == nullptr && B->first_child != nullptr)
        return IVP_TRUE;

    if (ClientA != nullptr)
        GetRoot(ClientA);
    CObject* ClientB = static_cast<CObject*>(B->client_data);
    if (ClientB != nullptr)
        GetRoot(ClientB);

    if (ClientA != nullptr && (static_cast<unsigned int>(ClientA->ObjectClass) & 0x27u) != 0x27u)
        ClientA = nullptr;
    if (ClientB != nullptr && (static_cast<unsigned int>(ClientB->ObjectClass) & 0x27u) != 0x27u)
        ClientB = nullptr;

    if (ClientA != nullptr || ClientB != nullptr)
        return IVP_TRUE;
    return IVP_FALSE;
}

void StarSystem::init(unsigned int SystemId)
{
    SystemInfo = const_cast<Universe::ISystem*>(Universe::get_system(SystemId));
    if (!SinglePlayer()) {
        // .text:06d06ebf / 06d06ec5 -- MOV ECX,[->PhySys::m_pCollisionFilter];
        // MOV [ECX],LAB_06d06c30.
        PhySys::m_pCollisionFilter = &StarSystem_MpCollisionFilter;
    }
}

// ===========================================================================
// StarSystem as IObjDB -- the four slots of .rdata:06d67cd0.
// ===========================================================================

// The walk state Common hands to slot 0 ({0, 0, mask} at the start; see
// CAsteroidSystem::update and ScanList::add_in_range): the node last returned,
// the list it came from, and the type mask, which slot 0 re-reads before every
// block (`MOV ECX,[ESI + 0x8]`, .text:06d0dde8 and each later test).
struct FStarSystemObjDbCursor {
    FSystemListNode* Node;             // +0x00
    ObjectList* List;                  // +0x04
    unsigned int Mask;                 // +0x08
};

// FUN_06d0dd50 (.text:06d0dd50, cdecl, five arguments):
//   (Mask, List, &Cursor->Node, &Cursor->List, bFilter)
//   06d0dd50 .. 06d0dd66  a walk already running in ANOTHER list: return null
//                         at once, cursor untouched
//   06d0dd72 .. 06d0dd7f  node = node ? node->Next : List->Head; null -> done
//   06d0dd83  Data = node->Data
//   06d0dd81 / 06d0dd86   only when bFilter (BL, loaded once at 06d0dd6e):
//     06d0dd91  Data->get_type(bits) -- IObjInspect slot 34, +0x88; the out
//               parameter is the bFilter stack slot itself
//     06d0dd97  a non-zero RESULT skips the node
//     06d0dd9f  (bits & Mask) == 0 skips the node
//   06d0dda5  a null Data ends the walk of this list like running off its end
//   06d0dda9 .. 06d0ddbb  hit: cursor = {node, List}, return Data
//   06d0ddbc .. 06d0ddd2  end: cursor = {0, 0}, return null
static GameObject* StarSystem_ObjDbStep(unsigned int Mask, ObjectList* List,
                                        FSystemListNode** CursorNode,
                                        ObjectList** CursorList, bool bFilter)
{
    if (*CursorList != nullptr && *CursorList != List)
        return nullptr;

    FSystemListNode* Node = *CursorNode;
    GameObject* Data = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? List->Head : Node->Next;
        if (Node == nullptr) {
            Data = nullptr;
            break;
        }
        Data = Node->Data;
        if (bFilter) {
            unsigned int Bits = 0;
            if (Data->get_type(Bits) != 0)
                continue;
            if ((Bits & Mask) == 0)
                continue;
        }
        if (Data != nullptr) {
            *CursorNode = Node;
            *CursorList = List;
            return Data;
        }
        break;
    }
    *CursorNode = nullptr;
    *CursorList = nullptr;
    return Data;
}

// Slot 0 -- FUN_06d0dde0 (.text:06d0dde0, `RET 0x4`). Each block is tried only
// when its mask test passes, in this order, and the first object found wins:
//   0x7f0000   -> +0x34, filtered unless the mask covers all of 0x7f0000
//                 (`SETNZ AL` after `CMP EDX,0x7f0000`, .text:06d0ddf7)
//   0xfdff     -> +0x5c, filtered unless it covers all of 0xfdff (.text:06d0de28)
//   0x10000000 -> +0x48 unfiltered      0x1000000  -> +0x70 unfiltered
//   0x2000000  -> +0x84 unfiltered      0x4000000  -> +0x98 unfiltered
//   0x8000000  -> +0xac unfiltered      0x20000000 -> +0xc0 unfiltered
// Null when nothing matched (EAX is zero on every path into .text:06d0df2e
// that did not return an object).
IObjRW* __thiscall StarSystem::GetNext(void* IterState)
{
    FStarSystemObjDbCursor* const Cursor = static_cast<FStarSystemObjDbCursor*>(IterState);
    GameObject* Found = nullptr;

    unsigned int Mask = Cursor->Mask;
    if ((Mask & 0x7f0000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Ships, &Cursor->Node, &Cursor->List,
                                     (Mask & 0x7f0000u) != 0x7f0000u);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0xfdffu) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Solars, &Cursor->Node, &Cursor->List,
                                     (Mask & 0xfdffu) != 0xfdffu);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x10000000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Loot, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x1000000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &List70, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x2000000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Beams, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x4000000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &List98, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x8000000u) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &ListAC, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & 0x20000000u) != 0)
        Found = StarSystem_ObjDbStep(Mask, &ListC0, &Cursor->Node, &Cursor->List, false);
    return Found;
}

// Slot 1 -- LAB_06d0dd40: `MOV EAX,[ESP+4] / MOV EDX,[EAX] / MOV [ESP+4],EDX /
// JMP FUN_06d0c840` -- the id by value into find_object.
IObjRW* __thiscall StarSystem::FindById(unsigned const* ObjectId)
{
    return find_object(*ObjectId);
}

// Slot 2 -- .text:06ce10f0 `MOV EAX,[ECX+0x4]; RET` (the body ICF shares with
// CPlayerGroup::GetID): the Universe::ISystem at +0x04.
Universe::ISystem* __thiscall StarSystem::GetZoneDb()
{
    return SystemInfo;
}

// Slot 3 -- LAB_06d06e90: `MOV EAX,[ECX+0x20]; RET 0x4`. The mask is ignored.
void* __thiscall StarSystem::GetBucket(unsigned TypeMask)
{
    (void)TypeMask;
    return Spatial;
}

// FUN_06d0c840 (.text:06d0c840).
//   06d0c846  TEST EDI,0x3fffffff / JNZ -- otherwise return 0
//   06d0c859 .. 06d0c8bc  ObjectList::find_by_id (FUN_06cf4f00) on +0x34, +0x48,
//             +0x70, +0x98, +0xac, +0xc0, +0x5c, stopping at the first hit
//   06d0c8be  return node->Data (0 when no block had it)
GameObject* StarSystem::find_object(unsigned int Id)
{
    if ((Id & 0x3fffffffu) == 0)
        return nullptr;
    FSystemListNode* Node = Ships.find_by_id(Id);
    if (Node == nullptr)
        Node = Loot.find_by_id(Id);
    if (Node == nullptr)
        Node = List70.find_by_id(Id);
    if (Node == nullptr)
        Node = List98.find_by_id(Id);
    if (Node == nullptr)
        Node = ListAC.find_by_id(Id);
    if (Node == nullptr)
        Node = ListC0.find_by_id(Id);
    if (Node == nullptr)
        Node = Solars.find_by_id(Id);
    return (Node != nullptr) ? Node->Data : nullptr;
}

// FUN_06d0c8d0 (.text:06d0c8d0, `RET 0x8`).
//   06d0c8d4  (Id & 0x3fffffff) == 0 -> 0
//   06d0c8e3  TypeMask == -1 -> find_object(Id) (06d0c8e9)
//   then the FIRST matching test picks one block:
//     0x7f0000 -> +0x34   0x10000000 -> +0x48   0xfdff -> +0x5c
//     0x4000000 -> +0x98  0x1000000 -> +0x70    0x8000000 -> +0xac
//     0x60000000 -> +0xc0   nothing -> 0 (06d0c94b)
//   06d0c953 .. 06d0c95d  find_by_id on it; node->Data or 0
GameObject* StarSystem::find_object(unsigned int Id, unsigned int TypeMask)
{
    if ((Id & 0x3fffffffu) == 0)
        return nullptr;
    if (TypeMask == 0xffffffffu)
        return find_object(Id);

    ObjectList* List = nullptr;
    if ((TypeMask & 0x7f0000u) != 0)
        List = &Ships;
    else if ((TypeMask & 0x10000000u) != 0)
        List = &Loot;
    else if ((TypeMask & 0xfdffu) != 0)
        List = &Solars;
    else if ((TypeMask & 0x4000000u) != 0)
        List = &List98;
    else if ((TypeMask & 0x1000000u) != 0)
        List = &List70;
    else if ((TypeMask & 0x8000000u) != 0)
        List = &ListAC;
    else if ((TypeMask & 0x60000000u) != 0)
        List = &ListC0;
    else
        return nullptr;

    FSystemListNode* const Node = List->find_by_id(Id);
    return (Node != nullptr) ? Node->Data : nullptr;
}


// ===========================================================================
// The dock queue at StarSystem+0x14 (server_sysdb.h, SystemDockNode).
// ===========================================================================

// Retail's `[watcher]` with the `- 8` back to the wrapper and its null test
// (.text:06d0c1f0 `MOV EDI,[EBX+0x8]` / `TEST EDI,EDI` / `ADD EDI,-0x8`, and the
// same at .text:06d0c1fe for the second watcher). The Watchable is a
// non-virtual base of IObjRW, so the downcast is a static one.
GameObject* FSystemDockEntry::WatchedObject(BaseWatcher const& Watcher)
{
    Watchable const* const Target = Watcher.target();
    if (Target == nullptr)
        return nullptr;
    return static_cast<GameObject*>(static_cast<IObjRW*>(const_cast<Watchable*>(Target)));
}

// FUN_06d0d370 -- see the declaration for the instruction map.
void StarSystem::queue_dock(GameObject* Object, GameObject* Target,
                            unsigned int const* BaseId, bool const* bLand)
{
    // .text:06d0d390 .. .text:06d0d41b -- the stack element. The two watchers
    // start zeroed; `Object ? Object + 8 : 0` and `Target ? Target + 8 : 0`
    // are the Watchable sub-objects, set only when they differ from what the
    // watcher holds (BaseWatcher::set is exactly that compare-then-set_pointer).
    FSystemDockEntry Element;
    Element.Object.set(Object != nullptr ? static_cast<Watchable const*>(Object) : nullptr);
    Element.Target.set(Target != nullptr ? static_cast<Watchable const*>(Target) : nullptr);
    Element.BaseId = *BaseId;
    Element.bLand = *bLand;

    // .text:06d0d414 .. .text:06d0d441 -- `ESI = [this+0x18]` (the sentinel),
    // `EDI = ESI->Prev`, operator new(0x20), node->Next = sentinel,
    // node->Prev = EDI (the node itself when EDI is null), sentinel->Prev =
    // node, node->Prev->Next = node: a push_back.
    SystemDockNode* const Sentinel = static_cast<SystemDockNode*>(ObjectListHead);
    SystemDockNode* const Node =
        static_cast<SystemDockNode*>(::operator new(sizeof(SystemDockNode)));
    Node->Next = Sentinel;
    Node->Prev = Sentinel->Prev != nullptr ? Sentinel->Prev : Node;
    Sentinel->Prev = Node;
    Node->Prev->Next = Node;

    // .text:06d0d448 -- FUN_06d0f830 copy-constructs the element into node+8.
    new (&Node->Entry) FSystemDockEntry(Element);

    // .text:06d0d44d .. .text:06d0d454
    ++ObjectListSize;

    // .text:06d0d457 .. .text:06d0d483 -- the stack element's watchers are
    // released on the way out (Element's destructor).
}

// FUN_06d0f410 -- see the declaration.
SystemDockNode* StarSystem::erase_docks(SystemDockNode* First, SystemDockNode* Last)
{
    while (First != Last) {
        SystemDockNode* const Node = First;
        First = First->Next;                         // .text:06d0f429
        Node->Prev->Next = Node->Next;               // .text:06d0f42b
        Node->Next->Prev = Node->Prev;               // .text:06d0f435
        Node->Entry.~FSystemDockEntry();             // .text:06d0f438 FUN_06d0d4a0
        ::operator delete(Node);                     // .text:06d0f43e
        --ObjectListSize;                            // .text:06d0f443 .. 06d0f44c
    }
    return First;                                    // .text:06d0f452
}

// FUN_06d07130 -- see the declaration.
void StarSystem::shutdown()
{
    // .text:06d07134 MOV AL,[ESI+0xfc] / TEST AL,AL / JZ -- nothing when the
    // system is not loaded.
    if (bLoaded == 0)
        return;

    // .text:06d07141 -- the flag is cleared before the event goes out.
    bLoaded = 0;

    // .text:06d0713e .. .text:06d07157 -- `SystemInfo->vtable[0]()` into a
    // stack slot, then FUN_06d43650(0x3d, &slot). This port's
    // Universe::ISystem is a flat record (common_universe.h), so slot 0 is
    // the NicknameId field. Retail calls through SystemInfo unguarded.
    unsigned int SystemId = SystemInfo->NicknameId;
    Server_ControllerDispatch(0u, pub::Controller::EMessage::SystemUnloaded, &SystemId);

    // .text:06d07161 -- FUN_06d072c0.
    unload();
}

// ---------------------------------------------------------------------------
// FUN_06d005d0 (.text:06d005d0, cdecl, 26 instructions). See server_sysdb.h.
//   .text:06d005d0 .. 06d005de  the watched Watchable; null -> return; minus 8
//                               is the wrapper (retail also returns when that
//                               difference is null -- the static_cast below is
//                               the same adjustment and cannot yield null from
//                               a non-null Watchable)
//   .text:06d005e0 .. 06d005fb  CBase::Object under the (& 3) == 3 guard
//   .text:06d005fb .. 06d00607  its CSimple id (+0xb0), read unguarded, != *ObjId
//                               -> return
//   .text:06d00609 .. 06d00610  BaseWatcher::set_pointer(0) on DAT_06d8d8ec
//   .text:06d00616              DAT_06d8d92c = 0
// set(nullptr) is set_pointer(0) here: it only skips the call when nothing is
// watched, and something is.
void Server_LookupCacheForget(unsigned int const& ObjId)
{
    Watchable const* const Watched = GLookupCacheObject.target();
    if (Watched == nullptr)
        return;
    GameObject const* const Obj =
        static_cast<GameObject const*>(static_cast<IObjRW const*>(Watched));
    if (Obj->csimple()->Id != ObjId)
        return;
    GLookupCacheObject.set(nullptr);
    GLookupCacheSystem = nullptr;
}

// FUN_06d08940 (.text:06d08940, RET 4). See server_sysdb.h for the map.
bool StarSystem::remove_solar(GameObject* Obj)
{
    // .text:06d0894a .. 06d08958
    unsigned int const ObjId = Obj->get_id();
    Server_LookupCacheForget(ObjId);

    // .text:06d08960 .. 06d08978 -- `node = node ? node->Next : Head`.
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Solars.Head : Node->Next;
        if (Node == nullptr)
            return false;                                  // .text:06d08a2a
        if (Node->Data == Obj)
            break;
    }

    // .text:06d0897a .. 06d089d1 -- MetaList::remove, inlined: the same
    // head / tail / middle unlink, then FreeList or the node's deleting
    // destructor (FUN_06d0f9a0).
    Solars.remove(Node);

    // .text:06d089d6 .. 06d089eb -- GetBucket (IObjDB vt +0xc) with 0xfdff;
    // the partition's vtable +0xc (remove) with the object. The BSP holds the
    // IObjRW pointers the load pass bulk-added (StarSystem::load).
    void* const Bucket = GetBucket(0xfdffu);
    if (Bucket != nullptr)
    {
        static_cast<ISpatialPartition*>(Bucket)->Remove(static_cast<IObjRW*>(Obj)); // +0x0c
    }

    // .text:06d089ee .. 06d08a20 -- onto the Deferred block (+0xe8).
    Deferred.append(Obj);
    return true;
}

// ---------------------------------------------------------------------------
// The objects between systems and the object lookup by id, retail
// .text:06d002e0 .. 06d00827. See server_gameobject.h for the declarations.
// ---------------------------------------------------------------------------

// LAB_06d002e0 (CRT init: allocator byte DAT_06d8d8d4, head node DAT_06d8d8d8 =
// operator new(0x40) linked to itself, size DAT_06d8d8dc = 0, atexit of
// FUN_06d00320) and FUN_06d00320 (every node unlinked and freed with the size
// counted down, then the head node freed and both dwords zeroed). A global
// std::list is that constructor / atexit-destructor pair.
std::list<FInTransitObject> GInTransitObjects;

// FUN_06d00380 (.text:06d00380, cdecl, 50 instructions).
//   .text:06d00380 .. 06d0039f  the first node whose +0x3c (Obj) is Obj; none
//                               -> return 0
//   .text:06d003a0 .. 06d003ae  REP MOVSD 0xd dwords from node +8 into OutParms
//   .text:06d003b0 .. 06d003d5  unlink, operator delete (FUN_06d5ffb0), --size
//   .text:06d003d5 .. 06d003fd  Watched ? Watched - 8 : 0, == Obj and Watched
//                               != 0 -> BaseWatcher::set_pointer(0) (the
//                               GLookupCacheSystem half is left alone)
//   .text:06d003fd              return 1
bool Server_TakeInTransit(GameObject* Obj, PhySys::CreateParms& OutParms)
{
    std::list<FInTransitObject>::iterator It = GInTransitObjects.begin();
    while (It != GInTransitObjects.end() && It->Obj != Obj)
        ++It;
    if (It == GInTransitObjects.end())
        return false;

    OutParms = It->Parms;
    GInTransitObjects.erase(It);

    // set(nullptr) is set_pointer(0): something is watched on this path.
    Watchable const* const Watched = GLookupCacheObject.target();
    if (Watched != nullptr &&
        static_cast<GameObject const*>(static_cast<IObjRW const*>(Watched)) == Obj)
        GLookupCacheObject.set(nullptr);
    return true;
}

// FUN_06d00410 (.text:06d00410, cdecl, 101 instructions). Only caller:
// pub::SpaceObj::Destroy (.text:06cf864d).
//   .text:06d0042b .. 06d0045c  the first node whose Obj is Obj; none -> 0
//   .text:06d0045d .. 06d0049c  erase(it++) (FUN_06d1b490 is the iterator's
//                               post-increment): unlink, delete, --size
//   .text:06d00491 .. 06d004b5  Watched ? Watched - 8 : 0, == Obj ->
//                               BaseWatcher::set(0) (import 06d64594)
//   .text:06d004bb .. 06d00523  the 0x30-byte controller message 0x39 on the
//                               stack: every field 0 (the vector's allocator
//                               byte is a copy of whatever the slot held), then
//                               +0 = Obj vt +0x20 (get_id); Server_Controller-
//                               Dispatch(0, 0x39, &msg)
//   .text:06d00528 .. 06d00533  Obj's CBase sub-object (+0xc) vtable slot 0
//                               with 1: the scalar deleting destructor
//   .text:06d00535 .. 06d0053a  the message vector's buffer freed (null)
//   return 1
bool Server_DestroyInTransit(GameObject* Obj)
{
    std::list<FInTransitObject>::iterator It = GInTransitObjects.begin();
    while (It != GInTransitObjects.end() && It->Obj != Obj)
        ++It;
    if (It == GInTransitObjects.end())
        return false;

    GInTransitObjects.erase(It);

    Watchable const* const Watched = GLookupCacheObject.target();
    GameObject const* const Cached = (Watched != nullptr)
        ? static_cast<GameObject const*>(static_cast<IObjRW const*>(Watched))
        : nullptr;
    if (Cached == Obj)
        GLookupCacheObject.set(nullptr);

    FObjectDestroyedEvent Event;
    Event.KillerId = 0;
    Event.SystemId = 0;
    Event.OwnerPlayer = 0;
    Event.Kind = 0;
    Event.Position.x = 0.0f;
    Event.Position.y = 0.0f;
    Event.Position.z = 0.0f;
    Event.ObjId = Obj->get_id();                       // .text:06d00505
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDestroyed, &Event); // .text:06d00523

    delete Obj;                                        // .text:06d00533
    return true;
}

// FUN_06d00560 (.text:06d00560, cdecl, 39 instructions). Only caller:
// .text:06cfa4fa (the system-transfer path, when the target system is null).
//   .text:06d00563 .. 06d00580  a 0x38-byte local: 0xd dwords of Parms, then
//                               Obj at +0x34
//   .text:06d00582 .. 06d005b6  operator new(0x40), linked in before the head
//                               node (push_back), REP MOVSD 0xe dwords to +8
//   .text:06d005b8 .. 06d005c0  ++size
void Server_PushInTransit(GameObject* Obj, PhySys::CreateParms const& Parms)
{
    FInTransitObject Entry;
    Entry.Parms = Parms;
    Entry.Obj = Obj;
    GInTransitObjects.push_back(Entry);
}

// FUN_06d006c0 (.text:06d006c0, cdecl, 124 instructions).
//   .text:06d006d0 .. 06d006d6  OutObj = 0, OutSys = 0
//   .text:06d006dc .. 06d0072b  the cache: Watched - 8 (the wrapper), its
//                               CBase::Object under the (& 3) == 3 guard, its
//                               CSimple id (+0xb0, no null test on the guard's
//                               result) == Id -> ++hits, OutObj = wrapper,
//                               OutSys = GLookupCacheSystem, return 1
//   .text:06d0072c              ++misses
//   .text:06d0073c .. 06d00756  cached system set -> OutObj = its
//                               find_object(Id) (FUN_06d0c840), OutSys = it
//   .text:06d00762 .. 06d00794  OutObj still 0 -> every in-transit node whose
//                               Obj's get_id() (vt +0x20) is Id: OutObj = Obj,
//                               OutSys = 0 -- no break, the last match stays
//   .text:06d0079c .. 06d007ec  OutObj still 0 -> SysDB::SysMap in order,
//                               skipping the cached system (&node->second):
//                               find_object(Id); the first hit sets OutSys =
//                               &node->second and ends the walk
//   .text:06d007ee .. 06d007f2  OutObj still 0 -> return 0 (OutSys keeps what
//                               the cached-system step wrote)
//   .text:06d007f8 .. 06d0081b  the watcher onto OutObj + 8 when it differs
//                               (set_pointer), GLookupCacheSystem = OutSys,
//                               return 1
bool Server_FindObject(unsigned int const& Id, GameObject*& OutObj, StarSystem*& OutSys)
{
    OutObj = nullptr;
    OutSys = nullptr;

    Watchable const* const Watched = GLookupCacheObject.target();
    if (Watched != nullptr)
    {
        GameObject* const Cached =
            static_cast<GameObject*>(static_cast<IObjRW*>(const_cast<Watchable*>(Watched)));
        if (Cached->csimple()->Id == Id)
        {
            ++GLookupCacheHits;
            OutObj = Cached;
            OutSys = GLookupCacheSystem;
            return true;
        }
    }
    ++GLookupCacheMisses;

    if (OutObj == nullptr)
    {
        if (GLookupCacheSystem != nullptr)
        {
            OutObj = GLookupCacheSystem->find_object(Id);
            OutSys = GLookupCacheSystem;
        }
        if (OutObj == nullptr)
        {
            for (std::list<FInTransitObject>::iterator It = GInTransitObjects.begin();
                 It != GInTransitObjects.end(); ++It)
            {
                if (It->Obj->get_id() == Id)
                {
                    OutObj = It->Obj;
                    OutSys = nullptr;
                }
            }
            if (OutObj == nullptr)
            {
                for (SysDB::SysMapType::iterator SysIt = SysDB::SysMap.begin();
                     SysIt != SysDB::SysMap.end(); ++SysIt)
                {
                    if (&SysIt->second == GLookupCacheSystem)
                        continue;
                    OutObj = SysIt->second.find_object(Id);
                    if (OutObj != nullptr)
                    {
                        OutSys = &SysIt->second;
                        break;
                    }
                }
                if (OutObj == nullptr)
                    return false;
            }
        }
    }

    GLookupCacheObject.set(static_cast<Watchable const*>(OutObj));
    GLookupCacheSystem = OutSys;
    return true;
}

// FUN_06d00630 (.text:06d00630, cdecl, 23 instructions). The id comes by value;
// its stack slot is what FUN_06d006c0 gets by reference.
//   .text:06d0063a .. 06d0064a  FUN_06d006c0(&Id, OutObj, OutSys); 0 -> below
//   .text:06d0064c .. 06d0065d  OutObj vt +0x160 (is_destroyed) false -> 1
//   .text:06d0065e .. 06d00667  OutObj = 0 (OutSys untouched), return 0
bool Server_FindLiveObject(unsigned int Id, GameObject*& OutObj, StarSystem*& OutSys)
{
    if (Server_FindObject(Id, OutObj, OutSys) && !OutObj->is_destroyed())
        return true;
    OutObj = nullptr;
    return false;
}

// .data:DAT_06d8d924 -- ServerSpace.cpp's own lazily fetched math engine
// (.text:06d00a1e / 06d00b14 / 06d00c67 / 06d00cf6 `MOV EAX,[DAT_06d8d924]` /
// JNZ / CALL FUN_06d5ff70 / store). Retail never tests it for null.
static I3DMathEngine* GServerSpaceMathEngine = nullptr;

// ===========================================================================
// Server_GetLaunchPosition -- retail FUN_06d00830 (.text:06d00830 .. 06d00d49,
// cdecl, 5 args, SEH frame for the local list). S = ESP after the four
// register pushes: PlayerId [S+0x88] (EBX), OutObjId [S+0x8c] (EBP), OutKind
// [S+0x90] (EDI), OutPos [S+0x94], OutOrient [S+0x98].
//   .text:06d00866 .. 06d0086d  *OutObjId = 0, *OutKind = -1
//   .text:06d00873 .. 06d00880  Players[PlayerId].exited_base_id (+0x410) &
//                               0x3fffffff == 0 -> the no-base path (06d00baa)
// In a base:
//   .text:06d00886 .. 06d008a4  std::list<GameObject*> at [S+0x1c] (FUN_06cfc340)
//   .text:06d008a8 .. 06d008f2  system = SysMap lookup of +0x3e0 (FUN_06d00280
//                               inlined: & 0x3fffffff == 0 -> null)
//   .text:06d008f5 .. 06d00907  system->collect_dockable_solars(list, base id)
//   empty list (.text:06d0090c):
//     .text:06d00918 .. 06d0093d  FDUMP 0x100002 "No docking rings for base %X"
//                                 (ServerSpace.cpp line 0x1a3)
//     .text:06d0093f .. 06d00972  base = system->find_object(base id) (FUN_06d0c840),
//                                 its Object narrowed (& 3) == 3 -> EDI
//     .text:06d00976 .. 06d009b1  Dir = -Object->WorldPosition (FUN_06cf4990),
//                                 scaled by 1.0f / |Dir| (FUN_06cf4960, FDIVR
//                                 .rdata:06d65520 = 1.0f), each FSTP float
//     .text:06d009b5 .. 06d009b9  radius = vt +0x64 get_physical_radius_r(center)
//     .text:06d009bc .. 06d009de  Off = (Dir * radius) * 2.2f (0x400ccccd), two
//                                 FUN_06cf4a10 calls
//     .text:06d009e3 .. 06d00a0f  *OutPos = WorldPosition + Off (FUN_06cf49b0)
//     .text:06d00a12 .. 06d00a63  LookMatrix(Dir) -> MatrixToQuaternion (vt +0x50)
//                                 -> *OutOrient
//   non-empty (.text:06d00a6b):
//     .text:06d00a6b .. 06d00a7b  Ring = front(); *OutObjId = Ring->get_id()
//     .text:06d00a7e .. 06d00a99  get_type_bits & 0x20 -> *OutKind = 1
//     .text:06d00a9b .. 06d00ad6  else DAT_06d8d900 = -1, controller event 0x32
//                                 {PlayerId, *OutObjId}, *OutKind = the mailbox,
//                                 -1 read back as 0 (SETZ / DEC / AND)
//     .text:06d00ad8 .. 06d00b0e  CEqObj (& 0x103) launch_pos(*OutPos, M, *OutKind)
//     .text:06d00b14 .. 06d00b56  MatrixToQuaternion(M) -> *OutOrient
//   .text:06d00b59 .. 06d00b94  exited_base_id = 0, the list erased and its head freed
// Not in a base (.text:06d00baa):
//   .text:06d00baa .. 06d00bbb  starting_ring (+0x414) ID_String::IsEmpty -> 06d00cbf
//   .text:06d00bc1 .. 06d00c39  system lookup as above, system->find_solar(&ring)
//                               (FUN_06d0c7e0), narrowed (& 0x303) == 0x303
//   .text:06d00c39 .. 06d00c61  *OutObjId = solar +0xb0; *OutKind = byte +0xe0
//                               bit 5; launch_pos(*OutPos, M, that bit)
//   .text:06d00c67 .. 06d00cac  MatrixToQuaternion(M) -> *OutOrient
//   .text:06d00cbf .. 06d00d3f  empty ring: *OutPos = initial_pos (+0x350,
//                               FUN_06d4ebe0), *OutOrient = MatrixToQuaternion(
//                               initial_ornt +0x35c, FUN_06d4ebf0)
// None of the object / system pointers is tested where retail does not test it.
// ===========================================================================
void Server_GetLaunchPosition(unsigned int PlayerId, unsigned int* OutObjId, int* OutKind,
                              Vector* OutPos, Quaternion* OutOrient)
{
    *OutObjId = 0u;
    *OutKind = -1;

    if ((Players[PlayerId].exited_base_id & 0x3fffffffu) != 0u)
    {
        std::list<GameObject*> Rings;
        StarSystem* const System = SysDB_LookupSystem(Players[PlayerId].system_id);
        System->collect_dockable_solars(Rings, Players[PlayerId].exited_base_id);

        if (Rings.size() == 0u)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: No docking rings for base %X",
                  "E:\\FL\\Scratch\\Source\\Server\\space\\ServerSpace.cpp", 0x1a3,
                  Players[PlayerId].exited_base_id);

            GameObject* const Base = System->find_object(Players[PlayerId].exited_base_id);
            CObject* const BaseRaw = (Base != nullptr) ? Base->Object : nullptr;
            CSimple* const BaseSimple =
                (BaseRaw != nullptr && (static_cast<unsigned int>(BaseRaw->ObjectClass) & 3u) == 3u)
                    ? static_cast<CSimple*>(BaseRaw) : nullptr;
            Vector const& BasePos = BaseSimple->WorldPosition;       // EBX = EDI + 0x2c

            // FUN_06cf4990 / FUN_06cf4960 / FDIVR 1.0f, then three FMUL ST1 / FSTP float.
            Vector Dir;
            Dir.x = -BasePos.x;
            Dir.y = -BasePos.y;
            Dir.z = -BasePos.z;
            double const Len = std::sqrt(static_cast<double>(Dir.x) * Dir.x +
                                         static_cast<double>(Dir.y) * Dir.y +
                                         static_cast<double>(Dir.z) * Dir.z);
            double const Inv = 1.0 / Len;
            Dir.x = static_cast<float>(Dir.x * Inv);
            Dir.y = static_cast<float>(Dir.y * Inv);
            Dir.z = static_cast<float>(Dir.z * Inv);

            Vector Center;
            float const Radius = BaseSimple->get_physical_radius_r(Center);   // vt +0x64

            // FUN_06cf4a10(&Scaled, &Dir, Radius), FUN_06cf4a10(&Off, &Scaled, 2.2f):
            // FLD float scale / FMUL float component / FSTP float.
            Vector Scaled;
            Scaled.x = static_cast<float>(static_cast<double>(Radius) * Dir.x);
            Scaled.y = static_cast<float>(static_cast<double>(Radius) * Dir.y);
            Scaled.z = static_cast<float>(static_cast<double>(Radius) * Dir.z);
            Vector Off;
            Off.x = static_cast<float>(static_cast<double>(2.2f) * Scaled.x);
            Off.y = static_cast<float>(static_cast<double>(2.2f) * Scaled.y);
            Off.z = static_cast<float>(static_cast<double>(2.2f) * Scaled.z);

            // FUN_06cf49b0(&Sum, &BasePos, &Off): FLD float a / FADD float b / FSTP float.
            OutPos->x = static_cast<float>(static_cast<double>(BasePos.x) + Off.x);
            OutPos->y = static_cast<float>(static_cast<double>(BasePos.y) + Off.y);
            OutPos->z = static_cast<float>(static_cast<double>(BasePos.z) + Off.z);

            Matrix const Look = LookMatrix(Dir);
            if (GServerSpaceMathEngine == nullptr)
                GServerSpaceMathEngine = Server_Acquire3DMathEngine();
            Quaternion Orient;
            GServerSpaceMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Orient),
                                                       reinterpret_cast<FMatrix const&>(Look));
            *OutOrient = Orient;
        }
        else
        {
            GameObject* const Ring = Rings.front();
            *OutObjId = Ring->get_id();
            if ((Ring->get_type_bits() & 0x20u) != 0u)
            {
                *OutKind = 1;
            }
            else
            {
                GReportedFreeTerminal = -1;
                unsigned int Msg[2];
                Msg[1] = *OutObjId;                                  // [S+0x14]
                Msg[0] = PlayerId;                                   // [S+0x10]
                Server_ControllerDispatch(0, pub::Controller::EMessage::FreeTerminalRequest, Msg);
                int const Reported = GReportedFreeTerminal;
                *OutKind = (Reported == -1) ? 0 : Reported;
            }

            CObject* const RingRaw = Ring->Object;
            CEqObj* const RingEq =
                (RingRaw != nullptr && (static_cast<unsigned int>(RingRaw->ObjectClass) & 0x103u) == 0x103u)
                    ? static_cast<CEqObj*>(RingRaw) : nullptr;
            Matrix LaunchOrient;
            RingEq->launch_pos(*OutPos, LaunchOrient, *OutKind);

            if (GServerSpaceMathEngine == nullptr)
                GServerSpaceMathEngine = Server_Acquire3DMathEngine();
            Quaternion Orient;
            GServerSpaceMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Orient),
                                                       reinterpret_cast<FMatrix const&>(LaunchOrient));
            *OutOrient = Orient;
        }

        Players[PlayerId].exited_base_id = 0u;
        return;                                  // the list's erase + head free (~list)
    }

    // +0x414 is an ID_String in retail (ID_String::IsEmpty is called on it); the
    // port's PlayerData declares it as its one dword.
    if (!reinterpret_cast<ID_String const&>(Players[PlayerId].starting_ring).IsEmpty())
    {
        StarSystem* const System = SysDB_LookupSystem(Players[PlayerId].system_id);
        GameObject* const Found = System->find_solar(&Players[PlayerId].starting_ring);
        CObject* const FoundRaw = (Found != nullptr) ? Found->Object : nullptr;
        CSolar* const Solar =
            (FoundRaw != nullptr && (static_cast<unsigned int>(FoundRaw->ObjectClass) & 0x303u) == 0x303u)
                ? static_cast<CSolar*>(FoundRaw) : nullptr;
        *OutObjId = Solar->Id;
        int const Kind = static_cast<int>(
            (reinterpret_cast<unsigned char const*>(&Solar->TypeBits)[0] >> 5) & 1u);
        *OutKind = Kind;
        Matrix LaunchOrient;
        Solar->launch_pos(*OutPos, LaunchOrient, Kind);

        if (GServerSpaceMathEngine == nullptr)
            GServerSpaceMathEngine = Server_Acquire3DMathEngine();
        Quaternion Orient;
        GServerSpaceMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Orient),
                                                   reinterpret_cast<FMatrix const&>(LaunchOrient));
        *OutOrient = Orient;
        return;
    }

    PlayerData& Slot = Players[PlayerId];
    OutPos->x = Slot.initial_pos[0];
    OutPos->y = Slot.initial_pos[1];
    OutPos->z = Slot.initial_pos[2];
    if (GServerSpaceMathEngine == nullptr)
        GServerSpaceMathEngine = Server_Acquire3DMathEngine();
    Quaternion Orient;
    GServerSpaceMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Orient),
                                               reinterpret_cast<FMatrix const&>(Slot.initial_ornt));
    *OutOrient = Orient;
}
