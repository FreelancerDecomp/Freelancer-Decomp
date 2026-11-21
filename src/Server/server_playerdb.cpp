// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_playerdb.cpp -- PlayerData and PlayerDB (the player table, accounts, characters).
// Split out of server_classes.cpp unchanged; the declarations the split files share are in
// server_classes_internal.h.

#include "common_server.h"
#include "common_net.h"
#include "common_physys.h"
#include "common_ivp_real_object.h" // IVP_Real_Object +0x1c/+0xa4 (StarSystem_MpCollisionFilter)
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
// The object itself is defined in server_classes.cpp, in its "Missing global exports" block,
// which keeps the globals with constructors in their teardown order; it is declared in
// common_server.h.
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
    for (size_t Index = 0; Index < std::size(AccountName); ++Index)
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
            srand(Platform::TimerMs());
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
    Platform::ModulePath(nullptr, ModulePath, sizeof(ModulePath));

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
        Platform::LocalTime(&Now);
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
    static constexpr wchar_t BadName[] = L">>>BAD   NAME<<<";
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

unsigned short AccountNameFileXorKey() {
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
    Platform::ModulePath(nullptr, TemplatePath, 0x104);                     // .text:06d537b2
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
