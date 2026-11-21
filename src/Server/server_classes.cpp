// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

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

#pragma comment(lib, "winmm.lib")

// The rest of the old server_classes.cpp is split by class into server_playerdb.cpp,
// server_playergroup.cpp, server_account.cpp, server_asteroid.cpp, server_starsystem.cpp,
// server_objectlist.cpp, server_playerview.cpp and server_space.cpp; the declarations they
// share are in server_classes_internal.h. This file keeps the namespace-scope objects that
// have constructors and destructors -- CPlayerGroup::s_GroupIDToGroupPtrMap,
// SrvAsteroid::GSrvAsteroidSystems, Players, SysDB::SysMap and GInTransitObjects -- in their
// old order, so they are still built in that order and torn down in reverse (SysMap holds
// StarSystems by value, and their teardown reaches the others).

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

// Retail .data:06d90400, the protected static CPlayerGroup::s_GroupIDToGroupPtrMap
// (export ordinal 508; see common_server.h for the declared type).
CPlayerGroup::GroupMapType CPlayerGroup::s_GroupIDToGroupPtrMap;

// SrvAsteroid's system map, kept with this module's other globals (see the top of the file).
namespace SrvAsteroid {
// DAT_06d8d980. Built by the static initializer LAB_06d03270 (FUN_06ce3730,
// the map constructor) and destroyed by FUN_06d032a0, which that initializer
// registers with atexit (.text:06d03285) -- a namespace-scope object's own
// construction and destruction.
FSrvAsteroidSystemMap GSrvAsteroidSystems;
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

// The in-transit list (server_space.cpp). It is defined here only so that the globals keep the
// construction order they had before the split; being declared after SysDB::SysMap, it is
// destroyed first, and ~StarSystem does not touch it.
// LAB_06d002e0 (CRT init: allocator byte DAT_06d8d8d4, head node DAT_06d8d8d8 =
// operator new(0x40) linked to itself, size DAT_06d8d8dc = 0, atexit of
// FUN_06d00320) and FUN_06d00320 (every node unlinked and freed with the size
// counted down, then the head node freed and both dwords zeroed). A global
// std::list is that constructor / atexit-destructor pair.
std::list<FInTransitObject> GInTransitObjects;
