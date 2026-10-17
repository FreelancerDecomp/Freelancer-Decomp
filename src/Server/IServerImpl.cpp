// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/server_iserverimpl.h"   // IServerImpl, Server
#include "../include/server_cobject.h"
#include "../include/server_gameobject.h"
#include "../include/common_dataconfig.h" // DataConfig::startup / shutdown
#include "../include/common_timing.h" // Timing::Delta
#include "../include/common_tradeoffer.h" // CPlayerTradeOffer
#include "../include/common_id.h"
#include "../include/common_ini.h"
#include "../include/common_goods.h"
#include "../include/common_universe.h"
#include "../include/common_server_playerdb.h"
#include "../include/server_savegame.h"
#include "../include/server_characterset.h" // CCharacterSet (CharacterInfoReq)
#include "../include/common_misc.h"      // safe_strcpy (CharacterSelect key)
#include "../include/common_server_controller.h"
#include "../include/content.h"   // IDLL, CreateInstance / DestroyInstance (controller DLL entry points)
#include "../include/server_sysdb.h"
#include "../include/server_basedb.h"
#include "../include/server_fuse.h"     // ServerFuseDB (FUN_06cfd4b0 / FUN_06cfd3a0)
#include "../include/rpc_iclient.h"
#include "../include/rpc_iclient_args.h" // FServerSetReputationInfo / FServerCharSelectVerifiedInfo
#include "../include/DALib.h"           // DALib::Data (FUN_06d44ca0)
#include "../include/readfile.h"        // IFileSystem::GetFileSysPath (+0x84)
#include "../include/common_physys.h"
#include "../include/common_casteroid.h"   // CmnAsteroid::Update (.text:06d0c136)
#include "../include/common_animdb.h"     // AnimDB::Update (FUN_06d10320)
#include "../include/server_createinfo.h" // FShipCreateRequest, Server_Acquire3DMathEngine (PlayerLaunch)
#include "../include/x86math.h"           // I3DMathEngine (PlayerLaunch)
#include <windows.h>
#include <mmsystem.h>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <list>
#include <stdexcept>
#include <cstring>
#include <cstddef>

#pragma comment(lib, "winmm.lib")

// Server_ControllerDispatch (common_server_controller.h), pub::Controller::_SendMessage's
// dispatch core, is defined further down this file. Retail's FUN_06d43650 (exported by the
// port from Common.dll as Server_SendMessage) is only `_SendMessage(0 /* broadcast */,
// MsgId, MsgData)`, so every call site that used to go through the empty Common.dll
// dispatcher now calls it directly.
extern "C" int __cdecl Server_ControllerSetHeartbeat(unsigned int Flid, float Interval);
extern "C" int __cdecl Server_ControllerSetTimer(unsigned int Flid, unsigned int Param1,
                                                 unsigned int Param2, float Timeout);
// IServerImpl::SubmitChat's body (retail FLChatServer.cpp), src/Server/server_chat.cpp.
void Server_SubmitChat(CHAT_ID Sender, unsigned long Size, const void* Data, CHAT_ID Target, int SystemId);

// IClient is the real 126-slot RPC client interface (src/include/rpc_iclient.h).
// A fabricated 125-slot copy used to live here, with invented method names and a
// virtual destructor occupying slot 0 (retail slot 0 is the FLPACKET_COMMON_FIREWEAPON
// sender). That was an ODR duplicate of the shared header; it is gone.
//
// Retail exports IClient's default ctor, copy ctor and operator= (rpclocal ships
// ??0IClient@@QAE@XZ / ??4IClient@@QAEAAV0@ABV0@@Z beside ??_7IClient@@6B@, and our
// Server.dll re-exports all three from src/Server/server_classes.cpp). They are
// declared in rpc_iclient.h and defined here; the class is abstract and has no data,
// so every one of them is trivial.
IClient::IClient() {}
IClient::IClient(const IClient& Other) { (void)Other; }
IClient& IClient::operator=(const IClient& Other) { (void)Other; return *this; }

// Retail keeps TWO pairs of client-interface pointers:
//   .data:06d8f618  HMODULE of the client DLL          (FUN_06d43340 / FUN_06d43390)
//   .data:06d8f620  IClient*     as loaded             (W 06d43371, R 06d43380 / 06d433d0,
//                                                       zeroed 06d433aa)
//   .data:06d8f624  IChatClient* as loaded             (W 06d4337a, R 06d433c0,
//                                                       zeroed 06d433b4)
//   .data:06d8fb14  IClient*     live, every caller    (W 06d44beb Startup,
//                                                       zeroed 06d45203 Shutdown)
//   .data:06d8fb18  IChatClient* live, every caller    (W 06d44bf5 Startup, never zeroed)
// Startup copies the loaded pair into the live pair through FUN_06d433d0 /
// FUN_06d433c0; FreeClientDll clears only the loaded pair.
static HMODULE g_hClientDll = nullptr;
static IClient* GLoadedClientInterface = nullptr;          // .data:06d8f620
static IChatClient* GLoadedChatClientInterface = nullptr;  // .data:06d8f624
// .data:06d8fb14 -- accessed by SendGroupID/SendSystemID in server_classes.cpp
IClient* g_pClientInterface = nullptr;
// Retail .data:DAT_06d8fb18 (set from FUN_06d433c0 at 0x06d44bf0). It is the
// IChatClient half of rpclocal's Client object; CPlayerGroup's group/chat
// senders call its single slot (see server_classes.cpp). Nothing in retail
// ever writes it again, so it outlives Shutdown with its stale value.
IChatClient* g_pChatClientInterface = nullptr;
// Retail's server clock is TWO doubles plus the Delta at .data:06d8f970:
// FUN_06d447b0 returns DAT_06d8fb28 + DAT_06d8fb20 (.text:06d447b0 FLD / FADD).
// DAT_06d8fb28 is the running sum ElapseTime adds read_dt() into (.text:06d4518e)
// and nothing ever resets; DAT_06d8fb20 is the BASE a loaded game or world file
// sets (.text:06d44896 FUN_06d44810, .text:06d44db1 FUN_06d44ca0) and Startup
// zeroes (.text:06d44b76). g_ElapsedTimeAccum is DAT_06d8fb28, GServerTimeBase
// is DAT_06d8fb20 (split 2026-09-24, claude:srv-save-2; they used to be one).
static Timing::Delta g_TimingDelta;
double g_ElapsedTimeAccum = 0.0;
double GServerTimeBase = 0.0;

// ---------------------------------------------------------------------------
// Server_LoadSinglePlayerState -- retail FUN_06d44810 (.text:06d44810 ..
// .text:06d448c8), cdecl (INI_Reader*), plain RET. Called only by the
// CharacterSelect worker FUN_06d4a5d0 in single player (.text:06d4ac6f), on the
// reader it opened over the selected character file.
//   06d44827  get_state(&saved)
//   06d44835  time = 0.0 (both dwords)
//   06d44845  read_header loop until is_header("Time") (.rdata:06d6ad78);
//             on a hit: read_value (result ignored), time = get_value_float(0)
//   06d44883  set_state(&saved)
//   06d44896  DAT_06d8fb20 = time ; 06d448a1 Delta(06d8f970).start()
//   06d448a7  Reputation::FreeFeelings() ; 06d448ae LoadFeelings(reader)
//   06d448b7  FUN_06d43650(0x40, reader) -- the broadcast, data = the reader*
// ---------------------------------------------------------------------------
void Server_LoadSinglePlayerState(INI_Reader& Reader) {
    INI_Reader::State Saved;
    Reader.get_state(Saved);
    double Time = 0.0;
    if (Reader.read_header()) {
        do {
            if (Reader.is_header("Time")) {
                Reader.read_value();
                Time = Reader.get_value_float(0);
                break;
            }
        } while (Reader.read_header());
    }
    Reader.set_state(Saved);
    GServerTimeBase = Time;
    g_TimingDelta.start();
    ::Reputation::FreeFeelings();
    ::Reputation::LoadFeelings(Reader);
    Server_ControllerDispatch(0, pub::Controller::EMessage::LoadGame, &Reader);
}

// Retail .data:06d8f998, the 11-dword counter block GetServerStats copies out
// (.text:06d45afc). Startup zero-fills it (.text:06d44c04); Login bumps +0x04 /
// +0x0c, PlayerDB::logout +0x08 (server_classes.cpp), AbortMission +0x1c. It used
// to be three unrelated statics plus a never-written copy inside GetServerStats.
ServerStats GServerStats = {};
// .data:DAT_06d8d758 -- IServerS.cpp's own lazily acquired I3DMathEngine
// (FUN_06d5ff70), used only by PlayerLaunch (.text:06cefcc2 / 06cefceb /
// 06cf00e4), each time in the `MOV EAX,[DAT] / CMP EAX,EBP / JNZ / CALL
// FUN_06d5ff70 / MOV [DAT],EAX` idiom.
static I3DMathEngine* GIServerSMathEngine = nullptr;
// .data:DAT_06d8f9c8 -- [Freelancer] initial_world. Read also by
// PlayerDB::create_restart_file (.text:06d538f8), hence external linkage.
char g_InitialWorldPath[260] = {0};

static IClient* Server_LoadClientDll(const char* module_path);
static void Server_FreeClientDll();
static IChatClient* Server_GetLoadedChatClientInterface();
static IClient* Server_GetLoadedClientInterface();
static void Server_ReadWorldIni(const char* config_path);
static void Server_LoadWorldState();
unsigned int Server_ControllerCreate(const char* dll_path, const char* instance_name, void* parms, unsigned int priority);
static void Server_LoadInitialDlls(const char* IniPath);
static void Server_PlayerSubsystemInit(unsigned int max_players, unsigned int allow_pvp);
static void Server_CleanupPhysics();
static void Server_ControllerCleanup();
static void Server_PhysicsUpdate(float dt);

// FUN_06d0be50 -- one system's update; defined next to Server_PhysicsUpdate.
static unsigned int StarSystem_Update(StarSystem* Sys, float Dt);
static void Server_TimerTick(float dt);
static void Server_TimerFiring(float dt);

static PlayerSlot* GetPlayerBySession(unsigned int session_id) {
    return PlayerDB_GetPlayer(session_id);
}

// ---------------------------------------------------------------------------
// PlayerData member FUN_06d48d20 (.text:06d48d20 .. .text:06d48d42, thiscall on
// the slot, plain RET). Ported 2026-09-24 (claude-code:w3-baseenter):
//   06d48d23  CALL [->COMMON.DLL::SinglePlayer] / TEST AL,AL / JNZ out
//   06d48d2d  ECX = [.data:DAT_06d8fb14]          ; the IClient
//   06d48d33  EDX = [this + 0x348]                ; client_id
//   06d48d3b  PUSH 0 / PUSH EDX / CALL [EAX + 0x24] ; IClient slot 9
// i.e. outside single player, IClient::PushToClient(client_id, NULL packet). No null
// test on the interface. Callers: BaseExit's two refusals (.text:06d1780c,
// .text:06d17823), the inlined LocationExit refusal inside BaseExit
// (.text:06d17903) and LocationExit's own (.text:06d17ab5). What the MP
// transport does with a NULL packet on slot 9 is not recovered, so the name
// only says what the bytes do.
// ---------------------------------------------------------------------------
static void PlayerData_SendClientNullPacket(PlayerData& Slot) {
    if (SinglePlayer()) return;
    ::g_pClientInterface->PushToClient(Slot.client_id, nullptr);
}

// IServerImpl and the exported object `Server` (ordinal 524) are declared in
// src/include/server_iserverimpl.h; `Server` is defined at the end of this file. The
// member bodies below were written in-class until 2026-10-15 and moved out of the class
// verbatim (no logic change) when the declaration was published for FLServer.exe.

    // Retail .text:06d44b70 .. .text:06d44c91 (RET 0x4). Traced term by term
    // 2026-09-24 (claude-code:w5-server-06d4); the port's boot diagnostics
    // (ServerStartupLog to runtime_boot.log / OutputDebugString) were removed --
    // retail writes nothing here.
bool IServerImpl::Startup(const SStartupInfo& info) {
        GServerTimeBase = 0.0;             // .text:06d44b76/.text:06d44b80 -- the BASE; DAT_06d8fb28 is not reset
        g_TimingDelta.start();             // .text:06d44b8a, this = DAT_06d8f970
        srand(timeGetTime());              // .text:06d44b90 .. .text:06d44b97
        RefreshLoadingProgress();          // .text:06d44ba3
        if (CreateID("Freelancer") == 0)   // .text:06d44baa .. .text:06d44bb5
            return false;                  // .text:06d44bb7 XOR AL,AL
        // Retail 0x06d44bd9: MOV ECX,.data:Players / CALL PlayerDB::init --
        // the exported global, not a member of this object.
        Players.init(info.MaxPlayers, info.bAllowNewPlayers != 0);
        RefreshLoadingProgress();
        // Retail 0x64b8f: FUN_06d43340((LPCSTR)param_1) — the MODULE path, i.e. the
        // first 0x104 bytes of SStartupInfo. This is what LoadLibrary+GetClientInterface
        // must use; the INI path at +0x104 is a different string entirely.
        Server_LoadClientDll(info.ModulePath);
        // .text:06d44be6 CALL FUN_06d433d0 / 06d44beb MOV [DAT_06d8fb14],EAX
        g_pClientInterface = Server_GetLoadedClientInterface();
        // .text:06d44bf0 CALL FUN_06d433c0 / 06d44bf5 MOV [DAT_06d8fb18],EAX
        g_pChatClientInterface = Server_GetLoadedChatClientInterface();
        RefreshLoadingProgress();
        memset(&GServerStats, 0, sizeof(GServerStats));   // .text:06d44bff..06d44c09 REP STOSD x11
        // Retail 0x06d44c1c-0x06d44c24: MOV EBX,EAX / NEG EBX / SBB BL,BL / INC BL / JZ.
        // That idiom yields 1 only when DataConfig::startup returned ZERO, and the JZ
        // then SKIPS the whole init block. Success is a 0 return, and the same byte is
        // what Startup gives back. src/Common/common_dataconfig.cpp keeps that convention.
        // Retail 0x64bdd / 0x64bf6: both DataConfig::startup and FUN_06d448d0 take
        // param_1 + 0x104 — the INI path, not the module path.
        const bool bConfigOk = (DataConfig::startup(info.IniPath, nullptr, nullptr) == 0);
        if (bConfigOk) {
            Server_ReadWorldIni(info.IniPath);
            Server_LoadInitialDlls(info.IniPath);   // .text:06d44c2c PUSH EDI (param_1+0x104)
            RefreshLoadingProgress();
            // .text:06d44c37 .. .text:06d44c41 -- MP only: FUN_06d44ca0.
            if (!SinglePlayer())
                Server_LoadWorldState();
            RefreshLoadingProgress();
            Server_SubsystemInit();
            RefreshLoadingProgress();
            Server_PlayerSubsystemInit(info.MaxPlayers, info.bAllowPvp);
            RefreshLoadingProgress();
        }
        // Retail 0x64c5f: (**(code **)(*DAT_06d8fb14 + 0x28))(param_1+0x208, param_1+0x20c)
        // — IClient slot 10, the session-init slot that returns bool. Reached on
        // BOTH paths (LAB_06d44c70 is the JZ target at .text:06d44c24) and called
        // through DAT_06d8fb14 with no null test (.text:06d44c76 .. .text:06d44c86).
        g_pClientInterface->Startup(info.MaxPlayers, info.Port);
        RefreshLoadingProgress();          // .text:06d44c89
        return bConfigOk;                  // .text:06d44c8d MOV AL,BL
    }

void IServerImpl::Shutdown() {
        Players.logout_all();
        Server_CleanupPhysics();
        CObject::ReleaseAll();
        Server_SubsystemShutdown();
        Server_ControllerCleanup();
        DataConfig::shutdown();
        if (g_pClientInterface) {
            // IClient slot 15 (+0x3c): tears the real client interface down.
            g_pClientInterface->Shutdown();
            Server_FreeClientDll();
            g_pClientInterface = nullptr;
        }
        Players.free();
    }

int IServerImpl::Update() {
        // IClient slot 16 (+0x40): the client-side pump.
        if (g_pClientInterface) g_pClientInterface->Update();
        return 1;
    }

void IServerImpl::ElapseTime(float dt) {
        double real_dt = g_TimingDelta.read_dt();
        if (real_dt < 0.0) real_dt = 0.0;
        g_ElapsedTimeAccum += real_dt;
        Server_PhysicsUpdate(dt);
        Server_SubsystemUpdate(dt);                // FUN_06d18860, .text:06d4519b PUSH ESI (the dt argument)
        Server_TimerTick(dt);
        Server_TimerFiring(dt);
    }

    // ---------------------------------------------------------------------------
    // Slots 0..8 -- the object-command handlers (ported 2026-09-24,
    // claude-code:w5-server-06cf). Every one of them opens with FUN_06d006c0
    // (&Id, &Obj, &System), the object lookup by id: AL = found, Obj = the
    // IObjRW, System = its owning StarSystem. FUN_06d006c0 is Server_FindObject
    // (server_gameobject.h); the handlers that only keep the object call its
    // object-only wrapper Server_LookupGameObject, whose null is exactly retail's
    // AL == 0 (a false return always leaves the object out null). The ones that
    // test the SYSTEM out call Server_FindObject itself: for an in-transit object
    // that out is 0, not the object's OwnerSystem. The object's IObjAffect sub-object is
    // retail's `LEA ECX,[obj + 4]`; its slots are declared in common_iface.h.
    // None of these bodies sends a pub::Controller message.
    // ---------------------------------------------------------------------------

    // Slot 0: FireWeapon -- retail .text:06cf0dc0 .. 06cf0e3c, RET 8.
    //   06cf0dd2  FUN_06d006c0(&info.ObjectId, &obj, &sys); AL == 0 -> out
    //   06cf0de4  obj == 0 -> out
    //   06cf0dea  obj->vt[+0x160] (is_destroyed) true -> out
    //   06cf0e0e  FUN_06d50af0 on Players' raw slot (terminate_trade)
    //   06cf0e13  count = Hardpoints.Begin ? (End - Begin) >> 1 : 0
    //   06cf0e35  IObjAffect +0x34 (Hardpoints.Begin, count, &info.TargetPos, player_id)
void IServerImpl::FireWeapon(unsigned int player_id, const XFireWeaponInfo& info) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        PlayerData_TerminateTrade(Players[player_id]);
        // data() is the vector's _First (+0x14) as retail pushes it, size() its
        // `_First ? (_Last - _First) >> 1 : 0`.
        static_cast<IObjAffect*>(Obj)->slot_34(info.Hardpoints.data(),
                                               static_cast<unsigned>(info.Hardpoints.size()),
                                               &info.TargetPos, static_cast<int>(player_id));
    }
    // Slot 1: ActivateEquip -- retail .text:06cf0980 .. 06cf09cf, RET 8.
    //   06cf0992  FUN_06d006c0(&info.ObjectId, ...); miss (06cf099c), obj == 0
    //             (06cf09a4), is_destroyed (06cf09b2) -> out
    //   06cf09c9  IObjAffect +0x4c (&info.EquipId, byte info+6, player_id)
void IServerImpl::ActivateEquip(unsigned int player_id, const XActivateEquip& info) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        static_cast<IObjAffect*>(Obj)->slot_4c(&info.EquipId, info.bActivate,
                                               static_cast<int>(player_id));
    }
    // Slot 2: ActivateCruise -- retail .text:06cf09e0 .. 06cf0a31, RET 8.
    //   06cf09f2  lookup; miss (06cf09fc), obj == 0 (06cf0a04), is_destroyed
    //             (06cf0a12) -> out
    //   06cf0a2b  IObjAffect +0x50 (byte info+4, byte info+5, player_id)
void IServerImpl::ActivateCruise(unsigned int player_id, const XActivateCruise& info) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        static_cast<IObjAffect*>(Obj)->slot_50(info.bActivate, info.Field_0x05 != 0,
                                               static_cast<int>(player_id));
    }
    // Slot 3: ActivateThrusters -- retail .text:06cf0a40 .. 06cf0a8b, RET 8.
    //   06cf0a52  lookup; miss (06cf0a5c), obj == 0 (06cf0a64), is_destroyed
    //             (06cf0a72) -> out
    //   06cf0a85  IObjAffect +0x54 (byte info+4, player_id)
void IServerImpl::ActivateThrusters(unsigned int player_id, const XActivateThrusters& info) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        static_cast<IObjAffect*>(Obj)->slot_54(info.bActivate, static_cast<int>(player_id));
    }
    // Slot 4: SetTarget -- retail .text:06cf0c70 .. 06cf0d4d, RET 8.
    //   06cf0c85  lookup of info.ObjectId; miss (06cf0c8f), obj == 0 (06cf0c9b),
    //             is_destroyed (06cf0cad) -> out
    //   06cf0cb3  info.SubObjId == SUBOBJ_ID_NONE:
    //     06cf0cd3  FUN_06d006c0(&info.TargetObjectId, &target, &sys); a miss
    //               stores 0 into target (06cf0cdf) -- no other test on it
    //     06cf0cfe  IObjAffect +0x48 set_target(target, info.TargetSubObjId, player_id)
    //   else:
    //     06cf0d18  the same lookup of the target (06cf0d24 zeroes it on a miss)
    //     06cf0d44  IObjAffect +0x44 (&info.SubObjId, target, info.TargetSubObjId,
    //               player_id)
void IServerImpl::SetTarget(unsigned int player_id, const XSetTarget& info) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        IObjAffect* const Affect = static_cast<IObjAffect*>(Obj);
        if (info.SubObjId == SUBOBJ_ID_NONE)
        {
            IObjRW* const Target = Server_LookupGameObject(info.TargetObjectId);
            Affect->set_target(Target, info.TargetSubObjId, static_cast<int>(player_id));
            return;
        }
        IObjRW* const Target = Server_LookupGameObject(info.TargetObjectId);
        Affect->slot_44(&info.SubObjId, Target, info.TargetSubObjId,
                        static_cast<int>(player_id));
    }
    // Slot 5: TractorObjects -- retail .text:06cf0b50 .. 06cf0c68, RET 8, with an
    // SEH frame for the local std::vector<IObjRW*>.
    //   06cf0b7a  lookup of info.ObjectId; miss (06cf0b84), obj == 0 (06cf0b92),
    //             is_destroyed (06cf0ba5) -> out
    //   06cf0bb4  the vector: begin / end / capacity zeroed
    //   06cf0bc0  n = TargetIds.Begin ? (End - Begin) >> 2 : 0; the loop runs while
    //             i < n, signed (JLE / JL)
    //   06cf0bf1  per id: FUN_06d006c0(&ids[i], &target, &sys); a miss zeroes
    //             target (06cf0bfd) and skips; a null target skips (06cf0c0d)
    //   06cf0c23  vector<IObjRW*>::_Insert_n(end, 1, &target) -- a push_back
    //   06cf0c46  IObjAffect +0x3c (&info.TractorEquipId, vector, player_id)
    //   06cf0c4e  operator delete on the vector's storage
void IServerImpl::TractorObjects(unsigned int player_id, const XTractorObjects& info) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        std::vector<IObjRW*> Targets;
        int const Count = static_cast<int>(info.TargetIds.size());
        for (int Index = 0; Index < Count; ++Index)
        {
            IObjRW* const Target = Server_LookupGameObject(info.TargetIds[Index]);
            if (Target == nullptr)
                continue;
            Targets.push_back(Target);
        }
        static_cast<IObjAffect*>(Obj)->slot_3c(&info.TractorEquipId, Targets,
                                               static_cast<int>(player_id));
    }
    // Slot 6: GoTradelane -- retail .text:06cf0a90 .. 06cf0ae5, RET 8.
    //   06cf0aa7  info.ObjectId != the raw slot's ship id ([sid*0x418 + Players
    //             - 0x34], no PlayerDB::is_valid) -> out
    //   06cf0ab8  FUN_06d006c0(&info.ObjectId, &obj, &sys); AL == 0 -> out
    //   06cf0aca  sys == 0 -> out (the SYSTEM out is tested here, not the object)
    //   06cf0adf  sys->FUN_06d0df40(info.ObjectId, info+4, info+8, byte info+0xc,
    //             PUSH 0 -- a float 0.0) = StarSystem::notify_go_tradelane
    // The port's controller event 0x16 and its IClient SendCommonGoTradeLane call are gone: both
    // are FUN_06d0df40's own work (server_classes.cpp), not this handler's.
void IServerImpl::GoTradelane(unsigned int player_id, const XGoTradelane& info) {
        if (info.ObjectId != Players[player_id].ship_object_id) return;
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(info.ObjectId, Obj, Sys))
            return;
        if (Sys == nullptr)
            return;
        Sys->notify_go_tradelane(info.ObjectId, info.TradelaneRing1, info.TradelaneRing2,
                                 info.Field_0x0c, 0.0f);
    }
    // Slot 7: StopTradelane -- retail .text:06cf0af0 .. 06cf0b43, RET 0x10.
    //   06cf0b05  arg1 != the raw slot's ship id -> out
    //   06cf0b1a  FUN_06d006c0(&arg1, &obj, &sys); AL == 0 -> out
    //   06cf0b2c  sys == 0 -> out
    //   06cf0b3d  sys->FUN_06d0e120(arg1, arg2, arg3) = StarSystem::
    //             notify_stop_tradelane(ShipId, EntranceId, ExitId)
void IServerImpl::StopTradelane(unsigned int session_id, unsigned int arg1, unsigned int arg2, unsigned int arg3) {
        if (arg1 != Players[session_id].ship_object_id) return;
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(arg1, Obj, Sys))
            return;
        if (Sys == nullptr)
            return;
        Sys->notify_stop_tradelane(arg1, arg2, arg3);
    }
    // Slot 8: JettisonCargo -- retail .text:06cf0d50 .. 06cf0dbc, RET 8.
    //   06cf0d62  lookup; miss (06cf0d6c), obj == 0 (06cf0d74), is_destroyed
    //             (06cf0d82) -> out
    //   06cf0d9e  FUN_06d50af0 on Players' raw slot (terminate_trade)
    //   06cf0db5  IObjAffect +0x38 (info.ItemId, info.Count, player_id)
void IServerImpl::JettisonCargo(unsigned int player_id, const XJettisonCargo& info) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        PlayerData_TerminateTrade(Players[player_id]);
        static_cast<IObjAffect*>(Obj)->slot_38(info.ItemId, static_cast<int>(info.Count),
                                               static_cast<int>(player_id));
    }
    // Slot 13: retail body is the shared bare RET at Server 0x06ce1050 / rpclocal
    // 0x06d8eae0 — a genuine no-op in both modules.
void IServerImpl::SetActiveConnection(EFLConnection /*conn*/) {}
    // Slot 14: rpclocal 0x06d8f3f0 is MOV AL,1 / RET 8. Server.dll's body (0x06d519e0)
    // is likewise a RET n stub.
bool IServerImpl::SwapConnections(EFLConnection /*conn_a*/, EFLConnection /*conn_b*/) { return true; }
void IServerImpl::Connect(const char* server_name, unsigned short* port) {}

    // IServerImpl::DisConnect -- retail .text:06d44fd0 .. .text:06d4507e, in full:
    //   06d44fd5..06d45003  PlayerDB::is_valid inlined (sid != 0, sid-1 < max,
    //                       client_id != 0); nothing else -- no port `active` test
    //   06d4500e  [slot-0x20] & 0x3fffffff -> LocationInfoRequest(room, sid, false),
    //             then LocationExit([slot-0x20] RE-READ, sid)
    //   06d45042  [slot-0x2c] & 0x3fffffff -> BaseInfoRequest(base, sid, false),
    //             then BaseExit([slot-0x2c] RE-READ, sid)
    //   06d45077  PlayerDB::logout(sid)
    // The port used to gate both halves on its own location_active/base_active
    // flags as well; retail only tests the masked ids, and those flags are gone.
    // `type` is accepted and ignored, as in retail.
void IServerImpl::DisConnect(unsigned int session_id, EFLConnection type) {
        (void)type;
        if (!Players.is_valid(session_id)) return;
        // All four calls are `MOV ECX,.data:Server` + a direct CALL (06d4501d,
        // 06d45032, 06d45051, 06d45067) -- the global object, not `this`.
        if ((Players[session_id].current_location & 0x3fffffff) != 0) {
            Server.LocationInfoRequest(Players[session_id].current_location, session_id, false);
            Server.LocationExit(Players[session_id].current_location, session_id);
        }
        if ((Players[session_id].current_base & 0x3fffffff) != 0) {
            Server.BaseInfoRequest(Players[session_id].current_base, session_id, false);
            Server.BaseExit(Players[session_id].current_base, session_id);
        }
        Players.logout(session_id);
    }

    // IServerImpl::OnConnect -- retail .text:06d45090 .. .text:06d450a8, in full:
    //   06d45090  MOV ECX,[DAT_06d8fb14]          ; the live IClient, no null test
    //   06d4509c  PUSH .data:DAT_06d90280         ; &Players + 0x20 == &Players.m_MOTD
    //   06d450a1  PUSH EDX                        ; session_id
    //   06d450a2  CALL [EAX + 0x1cc]              ; IClient slot 115
    //   06d450a8  RET 4
    // Players is .data:06d90260 (MOV ECX,.data:Players at 06d44bd4), and PlayerDB
    // +0x20 is the MOTD std::wstring -- the same (session, &message) pair
    // PlayerDB::SetMOTD sends through this slot to every connected session.
    // m_MOTD is protected; GetMOTD() returns a reference to that very member, so
    // its address is retail's &DAT_06d90280.
void IServerImpl::OnConnect(unsigned int session_id) {
        g_pClientInterface->SendServerMiscObjUpdateMotd(session_id, Players.GetMOTD());
    }

void IServerImpl::Login(const SLoginInfo& info, unsigned int session_id) {
        // Retail 0x06d450b1-0x06d450d8: proceed when SinglePlayer(), or when the slot has
        // no client yet (player[-0xd0] == 0). A slot that already has a client is a
        // duplicate login and is dropped. player[-0xd0] is +0x348, a client id dword,
        // not a pointer -- see PlayerSlot::client_id.
        bool is_sp = SinglePlayer();
        if (!is_sp) {
            PlayerSlot* player = GetPlayerBySession(session_id);
            if (player && player->client_id != 0) return;
        }
        // Retail 0x06d45077-adjacent: MOV ECX,.data:Players / CALL
        // PlayerDB::login(SLoginInfo const&, unsigned int). The port used to
        // route this through Common.dll's PlayerDB_login, which wrote the other
        // table and never reached PlayerDB::load_user_data at all.
        unsigned char result = Players.login(info, session_id);
        if (result == 3) Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLogin, &session_id);
        // IClient slot 12 (+0x30) SendServerLoginResponse — ch2 op 0x02
        // FLPACKET_SERVER_LOGINRESPONSE. Retail 0x06d883a0: MOV DL,byte ptr [Status] — the
        // status byte travels BY REFERENCE, not by value.
        if (g_pClientInterface)
            g_pClientInterface->SendServerLoginResponse(session_id, result);
        // Retail 0x06d45116: CMP byte ptr [ESP+0x3],0x3 reads back the stored LOGIN RESULT
        // byte, not the session id. (Ghidra renders the spill slot as the high byte of
        // param_2, which is what the previous port code copied.)
        if (result == 3) {
            GServerStats.LoginSuccessCount++;              // .text:06d4511d
        } else {
            GServerStats.LoginFailCount++;                 // .text:06d45127
        }
    }

void IServerImpl::CharacterInfoReq(unsigned int session_id, bool full_info) {
        // Retail IServerImpl::CharacterInfoReq, .text:06d45220 .. .text:06d45341
        // (RET 8).
        // .text:06d4523e .. .text:06d4526c -- sid != 0, sid-1 < MaxPlayers,
        // [slot+0x348] != 0: PlayerDB::is_valid.
        if (!Players.is_valid(session_id)) return;

        // .text:06d45273 .. .text:06d45290 -- the CCharacterSet local at
        // ESP+0xc: OwnerList<CCharacterSet::CharacterInfo> vptr
        // (.rdata:06d6ada8), Head / Tail / bUseHeap / FreeList zeroed.
        CCharacterSet Set;

        // .text:06d4528c .. .text:06d452aa -- only when the bool is false does the
        // slot fill it (FUN_06d499c0). With the bool set, the list goes out empty.
        if (!full_info)
            PlayerData_BuildCharacterSet(Players[session_id], Set);

        // .text:06d452af .. .text:06d452bd -- IClient +0x34 (slot 13, ch2 0x03
        // FLPACKET_SERVER_CHARACTERINFO) with (sid, &set); no null test on the
        // interface.
        g_pClientInterface->SendServerCharacterInfo(session_id, Set);

        // .text:06d452c0 .. .text:06d45341 -- the set's destructor: OwnerList::free
        // (each record destroyed and freed, then the nodes), then ~MetaList.
    }

void IServerImpl::CharacterSelect(const CHARACTER_ID& char_id, unsigned int session_id) {
        // Retail IServerImpl::CharacterSelect, .text:06d453f0 .. .text:06d4566a
        // (RET 8). Frame: S = ESP after the EBX/EBP/EDI pushes; S' = S - 4 once
        // ESI is pushed at .text:06d4551b.

        // .text:06d45414 .. .text:06d45443 -- sid != 0, sid-1 < MaxPlayers,
        // [slot+0x348] != 0: PlayerDB::is_valid.
        if (!Players.is_valid(session_id)) return;
        PlayerData& Slot = Players[session_id];

        // .text:06d45449 .. .text:06d45468 -- the worker FUN_06d4a5d0 on the slot
        // (server_classes.cpp); a miss or a failed load ends here.
        if (!PlayerData_SelectCharacter(Slot, char_id, session_id)) return;

        // .text:06d4546e .. .text:06d45518 -- the SETREPUTATION argument at S+0x44:
        // FmtStr ctors (FUN_06d55bc0), +0x10 = 0, byte +0x14 = 0, word +0x48 = 0,
        // +0x00 = [slot+0x318], +0x10 = operator new(group_count() * 8), then
        // Reputation::Vibe::Get and the wide name wcscpy'd in (or +0x48 = 0).
        // +0x04 is never written by retail (stack residue); zeroed here.
        FServerSetReputationInfo Info;
        Info.Attitude = 0.0f;
        Info.Field_0x08 = 0;
        Info.Field_0x0c = 0;
        Info.Relations = nullptr;
        Info.RelationCount = 0;
        Info.Pad_0x15[0] = 0;
        Info.Pad_0x15[1] = 0;
        Info.Pad_0x15[2] = 0;
        Info.DisplayName[0] = L'\0';
        Info.VibeHandle = Slot.rep_vibe;                                 // .text:06d454af
        Info.Relations = static_cast< ::Reputation::Relation*>(
            ::operator new(::Reputation::group_count() * 8));            // .text:06d454bd
        wchar_t const* VibeName = nullptr;                               // S+0xc, .text:06d454ea
        ::Reputation::Vibe::Get(Info.VibeHandle, Info.Field_0x08, Info.Field_0x0c,
                                Info.RelationCount, Info.Relations, Info.NameA,
                                Info.NameB, VibeName);                   // .text:06d454ee
        if (VibeName == nullptr) {
            Info.DisplayName[0] = L'\0';                                 // .text:06d454ff
        } else {
            wcscpy(Info.DisplayName, VibeName);                          // .text:06d45512, unbounded
        }

        // .text:06d4551b .. .text:06d4555f -- PlayerDB::traverse_active over every
        // active slot: IClient +0x108 (SendServerSetReputation)([slot_i+0x348], &Info).
        for (PlayerData* Active = Players.traverse_active(nullptr); Active != nullptr;
             Active = Players.traverse_active(Active)) {
            g_pClientInterface->SendServerSetReputation(Active->client_id, Info);
        }

        // .text:06d45561 .. .text:06d455a6 -- FUN_06d43650(0x3b, S'+0x1c): seven
        // dwords, {[slot+0x3e0], 0, sid, 0, 0, 0, 0}.
        unsigned int EnterEvent[7];
        EnterEvent[0] = Slot.system_id;
        EnterEvent[1] = 0;
        EnterEvent[2] = session_id;
        EnterEvent[3] = 0;
        EnterEvent[4] = 0;
        EnterEvent[5] = 0;
        EnterEvent[6] = 0;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemEnter, EnterEvent);

        // .text:06d455ab .. .text:06d455d4 -- IClient +0x38 (SendServerCharSelectVerified)(sid, S'+0x38):
        // {Info.VibeHandle, (unwritten), FUN_06d447b0 = DAT_06d8fb28 + DAT_06d8fb20}.
        FServerCharSelectVerifiedInfo Verified;
        Verified.VibeHandle = Info.VibeHandle;
        Verified.Unused_0x04 = 0;
        Verified.ServerTime = g_ElapsedTimeAccum + GServerTimeBase;
        g_pClientInterface->SendServerCharSelectVerified(session_id, Verified);

        // .text:06d455d7 .. .text:06d455f9 -- GetAffiliation(copy of [slot+0x318],
        // out = 0xffffffff, false); the result code is not tested.
        int AffiliationVibe = Slot.rep_vibe;
        unsigned int Affiliation = 0xffffffffu;
        ::Reputation::Vibe::GetAffiliation(AffiliationVibe, Affiliation, false);

        // .text:06d455ff .. .text:06d4561b -- IClient +0x1d0
        // (SendServerMiscObjUpdateAffiliation)(sid, [slot+0x3e4] ship, affiliation).
        g_pClientInterface->SendServerMiscObjUpdateAffiliation(session_id, Slot.ship_object_id, Affiliation);

        // .text:06d45621 .. .text:06d45650 -- operator delete of the relation
        // array (FUN_06d5ffb0); the two FmtStr destructors run with Info.
        ::operator delete(Info.Relations);
    }

    // Slot 21: retail body is the shared RET stub (Server 0x06ce1050); rpclocal
    // 0x06d8cdc0 sends ch3 op 0x38 FLPACKET_CLIENT_REQUESTNEWCHARINFO.
void IServerImpl::NewCharacterInfoReq(unsigned int /*session_id*/) {}

void IServerImpl::CreateNewCharacter(const SCreateCharacterInfo& info, unsigned int session_id) {
        // Retail .text:06d45670 .. .text:06d456a5 (RET 8), no guards:
        // PlayerDB::create_new_character(info, sid), then this->vtable +0x4c --
        // CharacterInfoReq -- (sid, false) on success (.text:06d45693) and
        // (sid, true) on failure (.text:06d456a0).
        if (Players.create_new_character(info, session_id)) {
            CharacterInfoReq(session_id, false);
        } else {
            CharacterInfoReq(session_id, true);
        }
    }

void IServerImpl::DestroyCharacter(const CHARACTER_ID& char_id, unsigned int session_id) {
        // Retail 0x656b0: validate, FUN_06d49f60 delete char, vtable[19](session_id, 0)
        // .text:06d456b5 .. .text:06d456de -- sid != 0, sid-1 < MaxPlayers,
        // [slot+0x348] != 0: exactly PlayerDB::is_valid.
        if (!Players.is_valid(session_id)) return;
        PlayerSlot* player = &Players[session_id];

        // .text:06d456e0 .. .text:06d456f5 -- slot->FUN_06d49f60(char_id, sid),
        // result ignored: deselect if selected, find the record, refuse
        // Restart.fl, drop it from the account maps, unlink the file, erase the
        // record (PlayerData_DestroyCharacter, server_classes.cpp). REPLACED
        // 2026-09-24 (claude:srv-save-2): this used to delete by a path built
        // from the character NAME under the accounts root and then wipe the
        // live slot (active = false, session_id = 0, cash, ship...), none of
        // which retail does.
        PlayerData_DestroyCharacter(*player, char_id, session_id);

        // .text:06d456fa .. .text:06d45701 -- this->vtable +0x4c(sid, 0),
        // i.e. CharacterInfoReq(sid, false).
        CharacterInfoReq(session_id, false);
    }

void IServerImpl::CharacterSkipAutosave(unsigned int session_id) {
        // Retail .text:06d45380..06d453c0: PlayerDB::is_valid inlined (06d45386 sid
        // != 0, 06d45393 sid-1 < max, 06d453ac client_id != 0) -- that is `!p` --
        // then FUN_06d4c5f0(slot). No `active` test (w4-server-guards).
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        PlayerData_SkipNextAutosave(*p);   // FUN_06d4c5f0, .text:06d453bb
    }
    // ---------------------------------------------------------------------------
    // The in-base request family, .text:06d17300 .. .text:06d1761b. Rewritten
    // 2026-09-24 (claude-code:w3-baseenter). Every member but ReqHullStatus is the
    // same eighteen-instruction shape:
    //   CALL [->COMMON.DLL::SinglePlayer] / TEST AL,AL / JNZ work
    //   MOV  reg,[sid*0x418 + Players - 0x24] / TEST reg,reg / JZ out   ; +0x3F4
    //   work: LEA ECX,[sid*0x418 + Players - 0x418] / CALL <PlayerData member>
    // i.e. `SinglePlayer() || entered_base_id != 0` (an UNMASKED dword test), on
    // a slot indexed RAW -- no PlayerDB::is_valid, no client-id test, no port
    // `active` test. The +0x3F4 dword is the base BaseEnter wrote at
    // .text:06d1774e; the port used to read it as `bool bAuthorized`, which
    // nothing ever set, so in MP all of these returned at once.
    // ---------------------------------------------------------------------------

    // ReqShipArch -- retail .text:06d17300 .. .text:06d1733d (RET 0x8): gate at
    // .text:06d1731d, then .text:06d17338 CALL FUN_06d4ec10(arch) on the slot --
    // PlayerData_SetShipArch (GetShip validation, collision-group refill,
    // IClient slot 21, message 0x35 with &client_id). The port used to store the
    // archetype itself and send 0x35 with an invented {session, arch} payload.
void IServerImpl::ReqShipArch(unsigned int arch_id, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        PlayerData_SetShipArch(Players[session_id], arch_id);
    }
    // ReqHullStatus -- retail .text:06d17340 .. .text:06d173d9 (RET 0x8), the one
    // member of the family that validates the session first:
    //   06d17341..06d1736f  PlayerDB::is_valid inlined (sid != 0, sid-1 < max,
    //                       [slot+0x348] != 0)
    //   06d17371..06d1738e  SinglePlayer() || [slot-0x24] (+0x3F4) != 0
    //   06d1739d            [slot+0x3ec] & 0x3fffffff != 0 (current_base)
    //   06d173a9..06d173ba  FLD hull / FCOMP double [.rdata:06d65538] (= 0.0, read
    //                       from the PE) / FNSTSW / TEST AH,0x5 / JP keep /
    //                       hull := 0 -- i.e. `if (hull < 0.0) hull = 0` (C0 set
    //                       alone gives odd parity; equal, greater and unordered
    //                       all give even parity and keep the value, NaN included)
    //   06d173c7..06d173d3  [ESP+8] := 1, tail JMP FUN_06d4ed10(hull, true)
    // FUN_06d4ed10 is PlayerData_SetRelativeHealth (+0x268, IClient slot 22).
    // The port used to write `hull_hp` and skip both the base test and the
    // notification.
void IServerImpl::ReqHullStatus(float hull_status, unsigned int session_id) {
        if (!Players.is_valid(session_id)) return;
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        if ((Players[session_id].current_base & 0x3fffffff) == 0) return;
        if (hull_status < 0.0f) hull_status = 0.0f;
        PlayerData_SetRelativeHealth(Players[session_id], hull_status, true);
    }
    // ReqCollisionGroups -- retail .text:06d173e0 .. .text:06d1741d (RET 0x8):
    // gate at .text:06d173fd, then .text:06d17418 CALL FUN_06d4f320(groups) on the
    // slot -- PlayerData_SetCollisionGroups (+0x26C, IClient slot 23).
void IServerImpl::ReqCollisionGroups(const std::list<CollisionGroupDesc>& groups, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        PlayerData_SetCollisionGroups(Players[session_id], groups);
    }
    // ReqEquipment -- retail .text:06d17420 .. .text:06d1745d (RET 0x8): gate at
    // .text:06d1743d, then .text:06d17458 CALL FUN_06d4eeb0(equipment) on the
    // slot -- PlayerData_LoadEquipment (list replace, EquipIdMaker ids for
    // SUBOBJ_ID_NONE entries, IClient slot 24, message 0 with &client_id). The
    // port used to drop the list and send message 0 with &session_id.
void IServerImpl::ReqEquipment(const EquipDescList& equipment, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        PlayerData_LoadEquipment(Players[session_id], equipment);
    }

    // Slot 29 — the real slot 29, not the "Connect2" duplicate that used to sit here.
    //
    // THERE IS NO MERGE RULE. Read off the listing 2026-09-17 (wave 11): retail's
    // ?ReqCargo@IServerImpl@@UAEXABVEquipDescList@@I@Z is exported at RVA 0x48880
    // as ordinal 366, and that address holds exactly one instruction:
    //   .text:06d28880  c20800  RET  0x8
    // The linker ICF-folded it with ?Connect@IServerImpl@@UAEXPBDPAG@Z (ordinal
    // 83) -- the listing prints both mangled names over the same byte -- because
    // both take eight bytes of stack arguments and both do nothing. The IServer
    // vtable confirms the slot: ??_7IServerImpl@@6BIServer@@@ is at RVA 0x85bcc
    // (.rdata:06d65bcc) and .rdata:06d65c40, i.e. base + 0x74 == slot 29, is the
    // one that points here.
    //
    // So this handler must NOT touch the slot's EquipDescList, and must NOT
    // broadcast: the previous port's Server_ControllerDispatch(0, 0, &session_id)
    // was a message retail never sends. The whole packet is dead on the server
    // side; the client's cargo state arrives through ReqAddItem / ReqRemoveItem
    // and ReqEquipment instead.
void IServerImpl::ReqCargo(const EquipDescList& cargo, unsigned int session_id) {
        (void)cargo;
        (void)session_id;
    }
    // ReqAddItem -- retail .text:06d17460 .. .text:06d174cf (RET 0x18).
    //   .text:06d1747d  MOV  EDX,[sid*0x418 + Players - 0x24] ; +0x3F4 entered_base_id
    //                   TEST EDX,EDX / JZ out                 ; unless SinglePlayer()
    //   .text:06d1748c  StringAlloc(hardpoint, false), result kept in a 1-dword
    //                   stack slot that is then passed BY ADDRESS -- i.e. a
    //                   CacheString built on the stack
    //   .text:06d174c9  FUN_06d4efc0(slot, arch, &hpCacheString, count, health,
    //                                b_mounted, /*bNoCombine*/0, /*bNotify*/1)
    // Slot indexed raw; hardpoint handed to StringAlloc unchecked (the port's
    // StringAlloc maps null to its empty string itself).
void IServerImpl::ReqAddItem(unsigned int arch_id, const char* hardpoint, int count, float health,
                    unsigned char b_mounted, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        CacheString Hp;
        Hp.Value = StringAlloc(hardpoint, false);
        PlayerData_AddEquipment(Players[session_id], arch_id, Hp, count, health, b_mounted,
                                /*bNoCombine*/false, /*bNotify*/true);
    }
    // ReqRemoveItem -- retail .text:06d174e0 .. .text:06d17528 (RET 0xc).
    //   .text:06d174fd  +0x3F4 gate, as above
    //   .text:06d17510  MOV dword ptr [ESP + param_3],0x0   ; bNotify := 0
    //   .text:06d17523  JMP FUN_06d4f100                    ; tail call
    // The zeroed third stack slot is the notify flag, so this path deliberately
    // does NOT send IClient slot 27 -- the client asked for the removal and
    // already knows.
void IServerImpl::ReqRemoveItem(unsigned short equip_id, int count, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        PlayerData_RemoveEquipment(Players[session_id], equip_id, count, /*bNotify*/false);
    }
    // ReqModifyItem -- retail .text:06d17530 .. .text:06d1759d (RET 0x18).
    //   .text:06d1754d  +0x3F4 gate, as above
    //   .text:06d17597  FUN_06d4f170(slot, equip_id, &hpCacheString, count,
    //                                health, b_mounted, 0)
    // FUN_06d4f170 RETs 0x18 (six stack dwords) but only reads five; the sixth,
    // the literal 0 pushed at .text:06d17573, is dead in retail too, so the port
    // signature drops it.
void IServerImpl::ReqModifyItem(unsigned short equip_id, const char* hardpoint, int count, float health,
                       unsigned char b_mounted, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        CacheString Hp;
        Hp.Value = StringAlloc(hardpoint, false);
        PlayerData_ModifyEquipment(Players[session_id], equip_id, Hp, count, health, b_mounted);
    }

void IServerImpl::ReqSetCash(int cash, unsigned int session_id) {
        // Retail .text:06d175a0 .. .text:06d175dd, in full: outside single player
        // the session's [slot - 0x24] (+0x3F4, entered_base_id) must be non-zero,
        // then FUN_06d4f480(cash) on the raw-indexed slot. No other check.
        // FUN_06d4f480 is PlayerData_SetCash: MP selected-character gate, clamp to
        // [0, 999999999], store, IClient SETCASH with &client_id,
        // FUN_06d43650(0, &client_id).
        // Cash is an INT in retail, not a float -- the mangled name is ?ReqSetCash@...@@UAEXHI@Z.
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        PlayerData_SetCash(Players[session_id], cash);
    }

void IServerImpl::ReqChangeCash(int delta, unsigned int session_id) {
        // Retail .text:06d175e0 .. .text:06d1761b: the same +0x3F4 test as
        // ReqSetCash, then `MOV [ESP + param_2],0` and a tail JMP to
        // FUN_06d4f4e0(delta, false) on the slot -- PlayerData_AdjustCash without
        // the +0x320 total.
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        PlayerData_AdjustCash(Players[session_id], delta, false);
    }

    // IServerImpl::BaseEnter -- retail .text:06d17630 .. .text:06d177a8 (RET 0x8),
    // re-ported line by line 2026-09-24 (claude-code:w3-baseenter). EBX = base id
    // (param_1), EBP = session id (param_2), EDI = slot END (sid*0x418 + Players).
    //   06d17635..06d17669  PlayerDB::is_valid inlined; no port `active` test
    //   06d17677..06d17687  [EDI-0x24] (+0x3F4 entered_base_id) & 0x3fffffff != 0
    //                       -> return (already inside a base)
    //   06d1768d            EAX = [EDI-0x8] (+0x410 exited_base_id), parked in
    //                       param_2's stack slot
    //   06d17694..06d176a4  Universe::get_base(base) == 0 -> return
    //   06d176aa..06d176c0  exited == 0 || base == 0 || exited == base -> skip the
    //                       system change
    //   06d176cc..06d176f4  old = [slot-0x38] (+0x3E0 system_id); new = the base's
    //                       vtable +0x10 (the port's IBase::SystemId);
    //                       FUN_06d43650(0x3a, {old, new, sid})  (ESP+0x1c block)
    //   06d17703            [slot-0x38] = new
    //   06d17707..06d1773a  FUN_06d43650(0x3b, {new, old, sid, 0, 0, 0, 0})
    //                       (the 0x1c-byte block at ESP+0x28; the same seven-dword
    //                       shape CharacterSelect and RequestCreateShip send)
    //   06d17748..06d1774e  [EDI-0x2c] = [EDI-0x28] = [EDI-0x24] = base
    //                       (current_base, last_base_id, entered_base_id)
    //   06d17751..06d17758  FUN_06d14230(BaseDB, sid, base) -- BaseDB::load_base
    //   06d1775d..06d1776c  FUN_06d43650(1, {base, sid})
    //   06d17774..06d1779c  !SinglePlayer(): FUN_06d4f560 then FUN_06d4fc30
    // The port used to test current_base instead of +0x3F4, compare the old system
    // against the new one instead of testing +0x410, send 0x3a/0x3b with one shared
    // invented {sid, old, new} block, and skip last_base_id and +0x3F4 entirely.
void IServerImpl::BaseEnter(unsigned int base_id, unsigned int session_id) {
        if (!Players.is_valid(session_id)) return;
        PlayerData& Slot = Players[session_id];
        if ((Slot.entered_base_id & 0x3fffffff) != 0) return;

        unsigned int const ExitedBase = Slot.exited_base_id;
        Universe::IBase const* Base = Universe::get_base(base_id);
        if (Base == nullptr) return;

        if (ExitedBase != 0 && base_id != 0 && ExitedBase != base_id) {
            unsigned int const OldSystem = Slot.system_id;
            unsigned int const NewSystem = Base->SystemId;   // IBase vtable +0x10
            unsigned int LeaveEvent[3] = { OldSystem, NewSystem, session_id };
            Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, LeaveEvent);
            Slot.system_id = NewSystem;
            unsigned int EnterEvent[7] = { NewSystem, OldSystem, session_id, 0, 0, 0, 0 };
            Server_ControllerDispatch(0, pub::Controller::EMessage::SystemEnter, EnterEvent);
        }

        Slot.current_base = base_id;      // .text:06d17748 [EDI-0x2c]
        Slot.last_base_id = base_id;      // .text:06d1774b [EDI-0x28]
        Slot.entered_base_id = base_id;   // .text:06d1774e [EDI-0x24]

        GetBaseDB()->load_base(session_id, base_id);   // FUN_06d14230

        unsigned int BaseEvent[2] = { base_id, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::BaseEnter, BaseEvent);

        if (!SinglePlayer()) {
            PlayerData_ResetTotalEarned(Players[session_id]);
            PlayerData_SnapshotEquipment(Players[session_id]);
        }
    }

    // IServerImpl::BaseExit -- retail .text:06d177b0 .. .text:06d17961 (RET 0x8),
    // re-ported line by line 2026-09-24 (claude-code:w3-baseenter). param_1 = base
    // id, EBP = session id, ESI = slot base, EDI = sid*0x418.
    //   06d177b5..06d177ea  PlayerDB::is_valid inlined; no port `active` test
    //   06d177f8..06d1780c  ([slot-0x24] & 0x3fffffff) == 0 -> FUN_06d48d20(slot),
    //                       return
    //   06d1781b..06d17823  param_1 != [slot-0x24] (raw compare) -> FUN_06d48d20,
    //                       return
    //   06d17832..06d17841  room = [slot+0x3f8]; (room & 0x3fffffff) == 0 -> skip
    //   06d17847..06d1787a  LocationInfoRequest(room, sid, false) INLINED: the
    //                       is_valid test, FUN_06d15650() != 0, then
    //                       FUN_06d146e0(BaseDB, sid, room, 0)
    //   06d17885..06d17903  LocationExit([slot+0x3f8] re-read, sid) INLINED: the
    //                       is_valid test, current_base / room masks and the
    //                       room == param test, FUN_06d43650(0x2a, {room, sid}),
    //                       FUN_06d143e0(BaseDB, sid, room), [slot-0x20] = 0 --
    //                       else FUN_06d48d20(slot)
    //   06d17908..06d1791b  FUN_06d43650(2, {param_1, sid})
    //   06d17920..06d17930  FUN_06d142c0(BaseDB, sid, param_1) -- BaseDB::unload_base
    //   06d17935..06d17937  FUN_06d4c430(slot), the autosave
    //   06d17942..06d17954  [slot+0x410] = [slot-0x2c]; [slot+0x3ec] = 0;
    //                       [slot+0x3f4] = 0
    // The two inlined bodies are exactly this class's own LocationInfoRequest /
    // LocationExit (same order, same arguments), so the port calls them. The port
    // used to test base_active and a masked current_base, skip the room exit
    // event and FUN_06d142c0, send param_1 from current_base, and never write
    // +0x410 or +0x3F4.
void IServerImpl::BaseExit(unsigned int base_id, unsigned int session_id) {
        if (!Players.is_valid(session_id)) return;
        if ((Players[session_id].entered_base_id & 0x3fffffff) == 0) {
            PlayerData_SendClientNullPacket(Players[session_id]);
            return;
        }
        if (base_id != Players[session_id].entered_base_id) {
            PlayerData_SendClientNullPacket(Players[session_id]);
            return;
        }
        if ((Players[session_id].current_location & 0x3fffffff) != 0) {
            LocationInfoRequest(Players[session_id].current_location, session_id, false);
            LocationExit(Players[session_id].current_location, session_id);
        }

        unsigned int ExitEvent[2] = { base_id, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::BaseExit, ExitEvent);
        GetBaseDB()->unload_base(session_id, base_id);   // FUN_06d142c0
        PlayerData_AutoSave(Players[session_id]);

        Players[session_id].exited_base_id = Players[session_id].current_base;
        Players[session_id].current_base = 0;
        Players[session_id].entered_base_id = 0;
    }

    // IServerImpl::LocationEnter -- retail .text:06d17970 .. .text:06d17a0a (RET 0x8),
    // re-ported line by line 2026-09-24 (claude-code:w5-server-06d1):
    //   06d1797a..06d179a4  PlayerDB::is_valid inlined -> return
    //   06d179a6..06d179b9  (current_location & 0x3fffffff) != 0 -> return
    //   06d179bb..06d179bf  (current_base & 0x3fffffff) == 0 -> return
    //   06d179c1..06d179d4  FUN_06d14170(BaseDB, param_1) == 0 -> return (the
    //                       miss FDUMPs inside find_location)
    //   06d179dd            [slot-0x20] = param_1
    //   06d179e1..06d179e8  FUN_06d14350(BaseDB, sid, param_1)
    //   06d179ed..06d179fc  FUN_06d43650(0x29, {param_1, sid})
void IServerImpl::LocationEnter(unsigned int location_id, unsigned int session_id) {
        if (!Players.is_valid(session_id)) return;
        if ((Players[session_id].current_location & 0x3fffffff) != 0) return;
        if ((Players[session_id].current_base & 0x3fffffff) == 0) return;
        if (GetBaseDB()->find_location(location_id) == nullptr) return;
        Players[session_id].current_location = location_id;
        GetBaseDB()->enter_location(session_id, location_id);
        unsigned int RoomEvent[2] = { location_id, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::LocationEnter, RoomEvent);
    }

    // IServerImpl::LocationExit -- retail .text:06d17a10 .. .text:06d17ac0 (RET 0x8),
    // re-ported line by line 2026-09-24 (claude-code:w3-baseenter):
    //   06d17a16..06d17a48  PlayerDB::is_valid inlined -> plain return on failure
    //   06d17a52..06d17a77  (current_base & 0x3fffffff) == 0, or
    //                       (current_location & 0x3fffffff) == 0, or
    //                       current_location != param_1 (raw compare)
    //                       -> FUN_06d48d20(slot) and return (06d17ab5)
    //   06d17a79..06d17a88  FUN_06d43650(0x2a, {param_1, sid})
    //   06d17a90..06d17a99  FUN_06d143e0(BaseDB, sid, param_1) -- BaseDB::exit_location
    //                       (the NPC-character teardown + handle release; NOT the
    //                       content-callback unsubscribe the port used to do here,
    //                       which is LocationInfoRequest(.., false)'s job)
    //   06d17aa5            [slot-0x20] = 0
    // BaseExit inlines this whole body at .text:06d17885 .. .text:06d17903.
void IServerImpl::LocationExit(unsigned int location_id, unsigned int session_id) {
        if (!Players.is_valid(session_id)) return;
        if ((Players[session_id].current_base & 0x3fffffff) == 0 ||
            (Players[session_id].current_location & 0x3fffffff) == 0 ||
            Players[session_id].current_location != location_id) {
            PlayerData_SendClientNullPacket(Players[session_id]);
            return;
        }
        unsigned int RoomEvent[2] = { location_id, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::LocationExit, RoomEvent);
        GetBaseDB()->exit_location(session_id, location_id);   // FUN_06d143e0
        Players[session_id].current_location = 0;
    }

    // IServerImpl::BaseInfoRequest -- retail .text:06d17ad0..06d17b34, which then
    // calls FUN_06d14570(BaseDB, session_id, base_id, b_full). Both halves were
    // rewritten from the listing 2026-09-24 (claude-code:server-triage-s2): the
    // port ignored b_full, so a b_full == false call SUBSCRIBED the session
    // instead of unsubscribing it, and it skipped the wrapper's "only the base
    // you are in" test.
void IServerImpl::BaseInfoRequest(unsigned int base_id, unsigned int session_id, bool b_full) {
        // .text:06d17ad2..06d17afd: PlayerDB::is_valid inlined -- and nothing more;
        // the port's extra `active` test was removed 2026-09-24
        // (claude-code:w3-baseenter).
        if (!Players.is_valid(session_id)) return;
        PlayerSlot* player = &Players[session_id];
        // .text:06d17b03..06d17b17: a subscribe is honoured only for the base the
        // player is in (raw compare against player[-0x2c], no 0x3fffffff mask).
        if (b_full && base_id != player->current_base) return;

        // .text:06d17b19..06d17b20: the accessor's result is tested (FUN_06d15650
        // returns the static's address, so this never fails), then FUN_06d14570.
        if (GetBaseDB() == nullptr) return;
        GetBaseDB()->base_info(session_id, base_id, b_full);
    }
    // IServerImpl::LocationInfoRequest -- retail .text:06d17b40..06d17ba4, then
    // FUN_06d146e0(BaseDB, session_id, location_id, b_full). Rewritten from the
    // listing 2026-09-24 (claude-code:server-triage-s2): b_full was ignored (see
    // BaseInfoRequest), the wrapper's current-location test was missing, and a
    // room miss fell into an invented "system location" path that dispatched
    // controller event 0x31 -- retail has no such path, it FDUMPs and returns.
void IServerImpl::LocationInfoRequest(unsigned int location_id, unsigned int session_id, bool b_full) {
        // .text:06d17b42..06d17b6d: PlayerDB::is_valid inlined -- and nothing more;
        // the port's extra `active` test was removed 2026-09-24
        // (claude-code:w3-baseenter).
        if (!Players.is_valid(session_id)) return;
        PlayerSlot* player = &Players[session_id];
        // .text:06d17b73..06d17b87: compare against player[+0x3f8], unmasked.
        if (b_full && location_id != player->current_location) return;

        // .text:06d17b89..06d17b90: the same accessor test, then FUN_06d146e0.
        if (GetBaseDB() == nullptr) return;
        GetBaseDB()->location_info(session_id, location_id, b_full);
    }
    // IServerImpl::GFObjSelect -- retail .text:06d45710 .. .text:06d4575e (RET 0x8),
    // re-read 2026-09-24 (claude-code:w3-baseenter):
    //   06d45726  EAX = [sid*0x418 + Players - 0x2c]   ; current_base, slot indexed RAW
    //   06d4572a  TEST EAX,0x3fffffff / JZ out
    //   06d45732  Universe::get_base(current_base) == 0 -> out
    //   06d45743..06d45752  FUN_06d43650(0x1b, {param_1, sid})
    // No session validation at all. The port used to test its own `base_id`
    // mirror (really +0x3F4's twin) plus `active`, and skipped get_base.
void IServerImpl::GFObjSelect(unsigned int object_id, unsigned int session_id) {
        unsigned int const CurrentBase = Players[session_id].current_base;
        if ((CurrentBase & 0x3fffffff) == 0) return;
        if (Universe::get_base(CurrentBase) == nullptr) return;
        unsigned int SelectEvent[2] = { object_id, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::GFObjSelect, SelectEvent);
    }
    // GFGoodVaporized — retail 0x37c30: packs info struct + session_id, sends message 0x1e
    // Interface limitation: retail takes (SGFGoodVaporizedInfo const&, session_id)
void IServerImpl::GFGoodVaporized(const SGFGoodVaporizedInfo& info, unsigned int session_id) {
        // .text:06d17c30..06d17c62 has no branch at all -- no session test; the
        // port's `!p || !p->active` early-out is gone (w4-server-guards).
        // Retail 0x06d17c30 copies the struct's three dwords onto the stack and calls
        // FUN_06d43650(0x1e, &frame). The field semantics are still unidentified.
        unsigned int msg_data[4] = { info.Field_0x00, info.Field_0x04, info.Field_0x08, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::GFGoodVaporized, msg_data);
    }
// Retail 06d45770..06d457a4 writes a BYTE at +8; bytes +9..+b stay unwritten.
void IServerImpl::MissionResponse(unsigned int Arg0, unsigned long MissionId,
                                 unsigned char bAccepted, unsigned int SessionId) {
    struct FMissionResponseMessage {
        unsigned int Field_0x00; // TODO: forwarded first argument's meaning.
        unsigned long MissionId;
        unsigned char bAccepted;
        unsigned char Padding[3];
        unsigned int SessionId;
    };
    static_assert(sizeof(FMissionResponseMessage) == 0x10, "MissionResponse message size");
    FMissionResponseMessage Message;
    Message.MissionId = MissionId;
    Message.Field_0x00 = Arg0;
    Message.bAccepted = bAccepted;
    Message.SessionId = SessionId;
    Server_ControllerDispatch(0, pub::Controller::EMessage::MissionResponse, &Message);
}
    // TradeResponse -- retail .text:06d45840 .. .text:06d45977, RET 0xc. No branch
    // at all (no session test, no size test). Frame, off ESP after the prologue:
    //   06d4585c  MarshalBuffer::MarshalBuffer() on S+0x70
    //   06d4587a  MarshalBuffer::use_read_buffer(data, size)
    //   06d45884  TradeResponseInfo::TradeResponseInfo() on S+0x3c
    //   06d4589e  TradeResponseInfo::unmarshal(&buffer)
    //   06d458b0 / 06d458c9  MetaList<GoodDesc>(false) + the GoodDescList vptr on
    //             S+0x10 and S+0x24 (= GoodDescList() inlined)
    //   06d458d5..06d45900  S+0x04 = info+0x00, S+0x08 = info+0x04, byte S+0x0c =
    //             info+0x08 (the pad bytes S+0x0d..0x0f are never written)
    //   06d45904 / 06d4590f  GoodDescList::operator= (appending) from info+0x0c and
    //             info+0x20 into S+0x10 / S+0x24
    //   06d4591f  S+0x38 = session_id
    //   06d45923  FUN_06d43650(0x3f, S+0x04) -- the broadcast controller message
    //   then ~OwnerList<GoodDesc> on S+0x24 and S+0x10, ~TradeResponseInfo.
    // So message 0x3f carries {Field00, Field04, bFlag08, Offered, Requested,
    // session_id}: a TradeResponseInfo's members followed by the session id.
void IServerImpl::TradeResponse(const unsigned char* data, int size, unsigned int session_id) {
        MarshalBuffer Buffer;                                         // .text:06d45860
        Buffer.use_read_buffer(data, size);                           // .text:06d4587a
        TradeResponseInfo Info;                                       // .text:06d45884
        Info.unmarshal(&Buffer);                                      // .text:06d4589e

        struct FTradeResponseMessage {
            int Field00;                  // +0x00
            int Field04;                  // +0x04
            bool bFlag08;                 // +0x08
            GoodDescList OfferedGoods;    // +0x0c
            GoodDescList RequestedGoods;  // +0x20
            unsigned int SessionId;       // +0x34
        };
        FTradeResponseMessage Message;                                // .text:06d458b0 / 06d458c9
        Message.Field00 = Info.Field00;                               // .text:06d458e7
        Message.bFlag08 = Info.bFlag08;                               // .text:06d458ef
        Message.Field04 = Info.Field04;                               // .text:06d45900
        Message.OfferedGoods = Info.OfferedGoods;                     // .text:06d45904
        Message.RequestedGoods = Info.RequestedGoods;                 // .text:06d4590f
        Message.SessionId = session_id;                               // .text:06d4591f
        Server_ControllerDispatch(0, pub::Controller::EMessage::TradeResponse, &Message); // .text:06d45923
    }

    // GFGoodBuy — retail packs struct fields + session_id, sends message 0x1c
void IServerImpl::GFGoodBuy(const SGFGoodBuyInfo& info, unsigned int session_id) {
        // .text:06d17bb0..06d17be9 has no branch -- no session test; the port's
        // `!p || !p->active` early-out is gone (w4-server-guards).
        // Retail 0x06d17bb0 copies all four dwords onto the stack, then FUN_06d43650(0x1c, &frame).
        unsigned int msg_data[5] = { info.Field_0x00, info.Field_0x04, info.Field_0x08,
                                     info.Field_0x0c, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::GFGoodBuy, msg_data);
    }
    // GFGoodSell — retail sends message 0x1d
void IServerImpl::GFGoodSell(const SGFGoodSellInfo& info, unsigned int session_id) {
        // .text:06d17bf0..06d17c22 has no branch -- no session test; the port's
        // `!p || !p->active` early-out is gone (w4-server-guards).
        // Retail 0x06d17bf0 copies three dwords onto the stack, then FUN_06d43650(0x1d, &frame).
        unsigned int msg_data[4] = { info.Field_0x00, info.Field_0x04, info.Field_0x08, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::GFGoodSell, msg_data);
    }

    // SystemSwitchOutComplete -- retail .text:06cef600 .. 06cef9d6 (RET 8): the
    // player's ship has finished the jump-out animation at a gate/hole; move it
    // into the destination system at the arrival object.
void IServerImpl::SystemSwitchOutComplete(unsigned int ship_id, unsigned int session_id) {
        // .text:06cef609 .. 06cef63f -- PlayerDB::is_valid inlined: sid != 0 (JBE),
        // sid - 1 < max (JNC), client_id (+0x348) != 0 -- PlayerDB_GetPlayer's null.
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        // .text:06cef64d .. 06cef666 -- not in a base, in a ship.
        if ((p->current_base & 0x3fffffff) != 0) return;
        if ((p->ship_object_id & 0x3fffffff) == 0) return;

        // .text:06cef66c .. 06cef694 -- FUN_06d006c0(&ship_id, &Obj, &Sys); a miss
        // or a null system (in transit) ends it. Obj is not null-tested.
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(ship_id, Obj, Sys)) return;
        if (Sys == nullptr) return;

        // .text:06cef69a .. 06cef6b8 -- the object's CSimple (the (& 3) accessor,
        // null otherwise) must be owned by this session. Retail reads +0xb4 off the
        // accessor's result without a null test.
        CObject* const ShipRaw = Obj->Object;
        CSimple* const ShipSimple = (ShipRaw != nullptr &&
                                     (static_cast<unsigned int>(ShipRaw->ObjectClass) & 3u) == 3u)
                                        ? static_cast<CSimple*>(ShipRaw) : nullptr;
        if (ShipSimple->OwnerPlayer != session_id) return;

        // .text:06cef6be .. 06cef702 -- the gate is the ship's +0x90 (read off the
        // wrapper with no class test), found in this system under type mask 0xc40
        // (FUN_06d0c8d0); its CObject must be a CSolar (0x303). Ship+0x90 is the
        // field the port calls DeathSequenceObjId; its writer (.text:06d0cad1,
        // right after controller event 0x11) stores the jump object's id, so the
        // name is a misnomer -- here it is the gate the ship jumped out through.
        unsigned int* const GateIdField = &static_cast<Ship*>(Obj)->DeathSequenceObjId;   // [ESP+0x48]
        GameObject* const Gate = Sys->find_object(*GateIdField, 0xc40);
        if (Gate == nullptr) return;
        CObject* const GateRaw = Gate->Object;
        if (GateRaw == nullptr) return;
        if ((static_cast<unsigned int>(GateRaw->ObjectClass) & 0x303u) != 0x303u) return;
        CSolar* const GateSolar = static_cast<CSolar*>(GateRaw);

        // .text:06cef708 .. 06cef70c -- get_type_bits (vt +0x154); the result is
        // dropped (EAX is overwritten at .text:06cef718).
        (void)Gate->get_type_bits();

        // .text:06cef712 .. 06cef71f -- destination system (+0x1b4) and the
        // arrival object's nickname (+0x1b8, kept at [ESP+0x14]).
        unsigned int const DestSystemId = GateSolar->DestSystem;
        ID_String DestGate = GateSolar->DestGate;

        // .text:06cef723 .. 06cef765 -- FUN_06d00280; a miss FDUMPs (the gate's
        // nickname is pushed though the format has no slot for it) and returns.
        StarSystem* const DestSys = SysDB_LookupSystem(DestSystemId);
        if (DestSys == nullptr) {
            FDUMP(0x100001, "%s(%d) : *** ERROR: FATAL: Couldn't find gate destination system!",
                  "E:\\FL\\Scratch\\Source\\Server\\space\\IServerS.cpp", 0x6b,
                  GateSolar->Nickname.get_string());
            return;
        }

        // .text:06cef768 .. 06cef791 -- controller event 0x25 {ship, gate, the
        // system being left (FUN_06d06cd0)}.
        unsigned int Msg25[3];
        Msg25[0] = ship_id;
        Msg25[1] = *GateIdField;
        Msg25[2] = Sys->SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::Msg0x25, Msg25);

        // .text:06cef796 .. 06cef82e -- the transit CreateParms at [ESP+0x7c]. The
        // bytes retail never writes (+0x1f .. +0x25, +0x27) are stack contents
        // there; the port zero-initialises them.
        PhySys::CreateParms Parms = {};
        Parms.Mass = 1.0f;                                                 // 0x3f800000
        Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
        Parms.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
        Parms.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
        Parms.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
        Parms.CollisionGroupId = 0;                                        // +0x14
        Parms.Type = 2;                                                    // +0x18
        Parms.bFlag1c = 0;
        Parms.bEnableCollisions = 1;
        Parms.Name[0] = '\0';                                              // +0x1e
        Parms.bHasInertia = 0;                                             // +0x26
        Parms.Inertia.x = 0.0f;
        Parms.Inertia.y = 0.0f;
        Parms.Inertia.z = 0.0f;

        // .text:06cef836 -- FUN_06d0d000: out of the old system.
        Sys->remove_ship(Obj, &Parms);
        // .text:06cef83b .. 06cef848 -- FUN_06d087f0(sid, DestSystemId).
        Sys->player_switched_out(session_id, DestSystemId);
        // .text:06cef84d .. 06cef850 -- FUN_06d07e20(sid) on the destination.
        DestSys->add_player(session_id);

        // .text:06cef855 .. 06cef90c -- the arrival object (FUN_06d0c7e0) picks
        // the spot (Solar +0x250); without one, an FDUMP, the origin and the
        // identity orientation.
        Vector Pos;
        Matrix Orient;
        GameObject* const Arrival = DestSys->find_solar(&DestGate.Id);
        if (Arrival != nullptr) {
            static_cast<Solar*>(Arrival)->ChooseSystemArrivalLocation(Obj, Pos, Orient);
        } else {
            FDUMP(0x100001, "%s(%d) : *** ERROR: Couldn't find arrival gate %s.",
                  "E:\\FL\\Scratch\\Source\\Server\\space\\IServerS.cpp", 0x8d,
                  DestGate.get_string());
            Pos.x = 0.0f;
            Pos.y = 0.0f;
            Pos.z = 0.0f;
            Orient = Matrix(1.0f, 0.0f, 0.0f,
                            0.0f, 1.0f, 0.0f,
                            0.0f, 0.0f, 1.0f);
        }

        // .text:06cef90c .. 06cef934 -- FUN_06d0d100 (add_ship) and FUN_06d0cc00
        // (arrive_ship, handed the possibly-null arrival object).
        DestSys->add_ship(Obj, &Parms, Pos, Orient);
        DestSys->arrive_ship(Obj, Arrival, Pos, Orient);

        // .text:06cef939 .. 06cef945 -- the slot's system is the destination.
        p->system_id = DestSystemId;

        // .text:06cef943 .. 06cef9b2 -- controller event 0x3b, 7 dwords: {dest
        // system, old system, sid, arrival object id, Pos}. Retail calls the
        // arrival object's get_id (vt +0x20) with no null test -- after the
        // "Couldn't find arrival gate" FDUMP it faults here, and so does the port.
        unsigned int Msg3b[7];
        Msg3b[0] = DestSys->SystemInfo->NicknameId;
        Msg3b[2] = session_id;
        Msg3b[1] = Sys->SystemInfo->NicknameId;
        Msg3b[3] = Arrival->get_id();
        std::memcpy(&Msg3b[4], &Pos.x, sizeof(float));
        std::memcpy(&Msg3b[5], &Pos.y, sizeof(float));
        std::memcpy(&Msg3b[6], &Pos.z, sizeof(float));
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemEnter, Msg3b);

        // .text:06cef9b7 .. 06cef9c7 -- FUN_06d4c430 on the slot: the autosave.
        PlayerData_AutoSave(*p);
    }

    // PlayerLaunch — retail 0xfb20: complex launch sequence (~160 lines decompiled)
void IServerImpl::PlayerLaunch(unsigned int ship_id, unsigned int session_id) {
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        // .text:06cefb74: client_id (+0x348) != 0 -- PlayerDB_GetPlayer's null. No
        // `active` test in retail (removed, w4-server-guards).
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        // Retail validation: not in ship ([-0x34] & 0x3fffffff == 0, .text:06cefb82),
        // a ship reserved by RequestCreateShip ([slot+0x3e8] & 0x3fffffff != 0,
        // .text:06cefb98) and param_1 == that same +0x3E8 (.text:06cefba9), then
        // the system object via FUN_06d00280(player[-0x38]). The port used to test
        // its `base_id` mirror (+0x3F4's twin) and compare against ship_object_id,
        // which the preceding test had just required to be zero.
        if ((p->ship_object_id & 0x3fffffff) != 0) return; // Already in ship
        if ((p->created_ship_id & 0x3fffffff) == 0) return; // No ship reserved
        if (ship_id != p->created_ship_id) return; // Ship ID mismatch

        // .text:06cefbb6 .. 06cefbcb -- FUN_06d00280 on the slot's system (+0x3e0);
        // no system, no launch. Kept at [ESP+0x34].
        StarSystem* const Sys = SysDB_LookupSystem(p->system_id);
        if (Sys == nullptr) return;

        // .text:06cefbd1 .. 06cefbd3 -- a thiscall on the slot to .text:06ce1090,
        // which is a bare RET (the ICF fold Ghidra labels
        // pub::AI::ContentCallback::combat_drift_event). Nothing to run.

        // .text:06cefbd8 .. 06cefbdb -- FUN_06d07e20: the session joins the system.
        Sys->add_player(session_id);

        // .text:06cefbe0 .. 06cefbf9 -- controller event 6 {sid, [slot-0x8]}:
        // +0x410, exited_base_id (the base the session just left).
        unsigned int msg6[2] = { session_id, p->exited_base_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLaunch, msg6);

        // .text:06cefc01 .. 06cefc3e -- the request at [ESP+0x88]: FUN_06cf0330 (the
        // CShipCreateInfo constructor), the EquipIdMaker at +0xe0 (import
        // .rdata:06d643c4), +0xe4 = 0, +0xe8 = 0.
        FShipCreateRequest Req;
        Req.EquipStateData = nullptr;
        Req.MarkPlayerId = 0;

        // .text:06cefc43 .. 06cefc54 -- FUN_06d4ed90: `MOV EAX,[ECX+0x264]`.
        Req.ArchetypeNickname = p->ship_archetype;
        // .text:06cefc4e .. 06cefc68 -- PlayerDB::GetGroupID(sid) -> +0xd8.
        Req.Field_0xd8 = Players.GetGroupID(session_id);

        // .text:06cefc6f .. 06cefc7e -- Archetype::GetShip, then its load (vt +0xc).
        Archetype::Ship* const ShipArch = Archetype::GetShip(Req.ArchetypeNickname);
        ShipArch->load();

        // .text:06cefc81 .. 06cefc90 -- HardpointPositionA(Root+0x40 engine
        // index, "HpMount", &HpPos at [ESP+0x3c], NULL); the result is not
        // tested. Retail leaves HpPos as the stack held it when the call fails;
        // the port starts it at zero.
        Vector HpPos;
        HpPos.x = 0.0f;
        HpPos.y = 0.0f;
        HpPos.z = 0.0f;
        HardpointPositionA(ShipArch->EngineIndex, "HpMount", &HpPos, nullptr);

        // .text:06cefc96 .. 06cefcbd -- FUN_06d00830 with ObjId = 0 and Kind = -1
        // preset; position and orientation land in the request (+0x4c / +0x58).
        unsigned int LaunchObjId = 0;
        int LaunchKind = -1;
        Server_GetLaunchPosition(session_id, &LaunchObjId, &LaunchKind, &Req.Position,
                                 &Req.Orientation);

        // .text:06cefcc2 .. 06cefd10 -- the file's math engine (DAT_06d8d758):
        // QuaternionToMatrix (vt +0x54) into [ESP+0x48], then TransformMatrix
        // (vt +0x2c) of HpPos by it into [ESP+0x14].
        if (GIServerSMathEngine == nullptr)
            GIServerSMathEngine = Server_Acquire3DMathEngine();
        Matrix LaunchOrient;
        GIServerSMathEngine->QuaternionToMatrix(reinterpret_cast<FMatrix&>(LaunchOrient),
                                                reinterpret_cast<FQuaternion const&>(Req.Orientation));
        if (GIServerSMathEngine == nullptr)
            GIServerSMathEngine = Server_Acquire3DMathEngine();
        Vector HpOffset;
        GIServerSMathEngine->TransformMatrix(reinterpret_cast<FVector&>(HpOffset),
                                             reinterpret_cast<FMatrix const&>(LaunchOrient),
                                             reinterpret_cast<FVector const&>(HpPos));

        // .text:06cefd13 .. 06cefd83 -- the ship's origin sits HpMount behind the
        // launch point: FLD / FSUB / FSTP float per component.
        Req.Position.x = Req.Position.x - HpOffset.x;
        Req.Position.y = Req.Position.y - HpOffset.y;
        Req.Position.z = Req.Position.z - HpOffset.z;

        // .text:06cefd1e .. 06cefdd7 -- IClient +0xf0 (SendServerLaunch) FLPACKET_SERVER_LAUNCH
        // with the record at [ESP+0x48].
        FServerLaunchInfo Launch;
        Launch.Field_0x00 = ship_id;
        Launch.Field_0x04 = LaunchObjId;
        Launch.Field_0x08 = static_cast<unsigned int>(LaunchKind);
        Launch.Orientation = Req.Orientation;
        Launch.Position = Req.Position;
        g_pClientInterface->SendServerLaunch(session_id, Launch);

        // .text:06cefddd .. 06cefe43 -- outside single player: IClient +0x1c4
        // (SendServerMiscObjUpdateObjectGroup)(sid, ship, group id), then
        // GetAffiliation(copy of [slot+0x318], out = -1, false) and IClient +0x1d0
        // (SendServerMiscObjUpdateAffiliation)(sid, ship, affiliation).
        if (!SinglePlayer()) {
            g_pClientInterface->SendServerMiscObjUpdateObjectGroup(session_id, ship_id, Req.Field_0xd8);
            unsigned int Affiliation = 0xffffffffu;
            int AffiliationVibe = p->rep_vibe;
            ::Reputation::Vibe::GetAffiliation(AffiliationVibe, Affiliation, false);
            g_pClientInterface->SendServerMiscObjUpdateAffiliation(session_id, ship_id, Affiliation);
        }

        // .text:06cefe49 .. 06cefeca -- velocity and angular velocity zero (the
        // request already holds zeros; retail stores them again), ObjId, owner.
        Req.Velocity.x = 0.0f;
        Req.Velocity.y = 0.0f;
        Req.Velocity.z = 0.0f;
        Req.ObjId = ship_id;                                               // +0x04
        Req.OwnerPlayer = session_id;                                      // +0xac
        Req.AngularVelocity.x = 0.0f;
        Req.AngularVelocity.y = 0.0f;
        Req.AngularVelocity.z = 0.0f;

        // .text:06cefece .. 06ceff5b -- vibe (+0x318) and the com_ costume
        // (+0x28c): the four parts, the count, then `count` accessory dwords
        // (signed JLE / JL, no clamp).
        Req.VibeGroupId = p->rep_vibe;                                     // +0x10
        Req.CostumeHead = p->com_costume.PartIds[0];                       // +0x14
        Req.CostumeBody = p->com_costume.PartIds[1];                       // +0x18
        Req.CostumeLeftHand = p->com_costume.PartIds[2];                   // +0x1c
        Req.CostumeRightHand = p->com_costume.PartIds[3];                  // +0x20
        Req.CostumeAccessoryCount = static_cast<int>(p->com_costume.AccessoryCount);   // +0x44
        for (int Index = 0; Index < Req.CostumeAccessoryCount; ++Index)
            Req.CostumeAccessory[Index] = p->com_costume.AccessoryIds[Index];

        // .text:06ceff5f .. 06ceff7d -- CreateID(voice, +0x2c4) -> +0x48.
        Req.VoiceId = CreateID(p->voice);
        // .text:06ceff84 .. 06ceff9e -- FUN_06d4ed00 (FLD [slot+0x268]) FMUL the
        // archetype's hit_pts (+0x1c), _ftol -> +0x68.
        Req.HitPts = static_cast<int>(static_cast<double>(p->relative_health) *
                                      static_cast<double>(ShipArch->HitPts));

        // .text:06ceffa5 .. 06cefff6 -- a temporary EquipDescVector built from the
        // slot's EquipDescList (+0x278, FUN_06d4efb0), assigned to +0x70, then
        // destroyed.
        Req.Equipment = EquipDescVector(p->equipment);
        // .text:06cefff9 .. 06cf0077 -- the slot's collision groups (+0x26c,
        // FUN_06d4f420) assigned to +0x80 (skipped for self-assignment).
        Req.CollisionGroups = p->collision_groups;

        // .text:06cf0079 .. 06cf0096 -- GetRank(+0x10, &Rank). Retail's Rank
        // ([ESP+0x38]) is not initialised before the call; the port starts it at 0.
        unsigned int Rank = 0;
        ::Reputation::Vibe::GetRank(Req.VibeGroupId, Rank);
        Req.Throttle = 0.2f;                                               // DAT_06d65bb8 = 0x3e4ccccd
        Req.ReputationRank = Rank;                                         // +0xb4
        Req.MarkPlayerId = 0;                                              // +0xe8 (.text:06cf00b3)

        // .text:06cf00a7 .. 06cf00c1 -- FUN_06d0a880.
        ::Ship* const NewShip = Sys->create_ship(Req);
        // .text:06cf00bf .. 06cf00c5 -- Archetype::Root::Release.
        ShipArch->Release();

        // .text:06cf00cb .. 06cf00d5 -- launched from an object: FUN_06d01110 on
        // the new wrapper (not null-tested).
        if (LaunchObjId != 0)
            NewShip->apply_launch_invincibility();

        // .text:06cf00da .. 06cf013c -- single player: the orientation matrix again
        // (vt +0x54, into [ESP+0x48]) and CObject::beam_object (vt +0x7c) of the
        // wrapper's raw object under the (& 1) test -- called whatever that test
        // yields, as in retail.
        if (SinglePlayer()) {
            if (GIServerSMathEngine == nullptr)
                GIServerSMathEngine = Server_Acquire3DMathEngine();
            GIServerSMathEngine->QuaternionToMatrix(reinterpret_cast<FMatrix&>(LaunchOrient),
                                                    reinterpret_cast<FQuaternion const&>(Req.Orientation));
            CObject* Raw = nullptr;
            if (NewShip != nullptr) {
                CObject* const Obj = NewShip->Object;
                if (Obj != nullptr && (static_cast<unsigned int>(Obj->ObjectClass) & 1u) == 1u)
                    Raw = Obj;
            }
            Raw->beam_object(Req.Position, LaunchOrient, false);
        }

        // .text:06cf013f .. 06cf014c -- the slot's ship (+0x3e4) = the request's ObjId.
        p->ship_object_id = Req.ObjId;

        // .text:06cf0150 .. 06cf017e -- controller event 0x36 {system (FUN_06d06cd0),
        // sid, launch object, launch_pos index}.
        unsigned int msg36[4];
        msg36[0] = Sys->SystemInfo->NicknameId;
        msg36[1] = session_id;
        msg36[2] = LaunchObjId;
        msg36[3] = static_cast<unsigned int>(LaunchKind);
        Server_ControllerDispatch(0, pub::Controller::EMessage::SpaceEnter, msg36);

        // .text:06cf0186 .. 06cf0236 -- the request's destructor, inlined (base
        // vftable restamp, the +0x8c and +0x80 lists, the +0x70 vector): Req's
        // own destructor here.
    }

void IServerImpl::LaunchComplete(unsigned int arg0, unsigned int object_id) {
        // Retail .text:06cefaa0 .. 06cefad6 (RET 8). rpclocal serializes BOTH
        // arguments (ch3 op 0x3e), so neither is an implicit session id; arg0 is
        // passed through untouched (Ship::slot_70 sends it as the first dword of
        // controller event 0x28). No pub::Controller message of its own -- the old
        // id 0x47 was invented.
        //   06cefaa3 .. 06cefab2  FUN_06d006c0(&object_id, &obj, &sys)
        //   06cefaba / 06cefac4   AL == 0 or obj == 0 -> out
        //   06cefaca .. 06cefad0  (obj + 4)->vtable[+0x70](arg0) -- IObjAffect slot 28
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(object_id, Obj, Sys))
            return;
        if (Obj == nullptr)
            return;
        static_cast<IObjAffect*>(Obj)->slot_70(arg0);
    }

void IServerImpl::JumpInComplete(unsigned int arg0, unsigned int object_id) {
        // Retail .text:06cefae0 .. 06cefb16 (RET 8), the same shape as
        // LaunchComplete with IObjAffect slot 29 (+0x74, Ship::slot_74 sends arg0
        // in controller event 0x26). Both arguments are serialized (ch3 op 0x43).
        // The old message id 0x48 was invented.
        //   06cefae3 .. 06cefaf2  FUN_06d006c0(&object_id, &obj, &sys)
        //   06cefafa / 06cefb04   AL == 0 or obj == 0 -> out
        //   06cefb0a .. 06cefb10  (obj + 4)->vtable[+0x74](arg0)
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(object_id, Obj, Sys))
            return;
        if (Obj == nullptr)
            return;
        static_cast<IObjAffect*>(Obj)->slot_74(arg0);
    }

    // Hail -- retail .text:06cf0800 .. 06cf0848, RET 0xc. Ported 2026-09-24
    // (claude-code:w5-server-06cf):
    //   06cf0812  FUN_06d006c0(&object_id, &obj, &sys); AL == 0 -> out. Only the
    //             found flag is tested -- the object and system are discarded.
    //   06cf081e  payload {object_id, arg1, arg2} on the stack
    //   06cf083d  FUN_06d43650(0x1f, &payload)
    // FUN_06d006c0 is Server_LookupGameObject in this port; its null is AL == 0.
    // All three arguments are serialized by rpclocal (ch3 op 0x3f) -- no session id.
void IServerImpl::Hail(unsigned int object_id, unsigned int arg1, unsigned int arg2) {
        if (Server_LookupGameObject(object_id) == nullptr)
            return;
        unsigned int msg_data[3] = { object_id, arg1, arg2 };
        Server_ControllerDispatch(0, pub::Controller::EMessage::Hail, msg_data);
    }
    // SPObjUpdate -- retail .text:06cf0620 .. .text:06cf06ae (RET 8).
    // Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
    //   06cf0624 .. 06cf062c  SinglePlayer() -> return
    //   06cf062e .. 06cf0647  FUN_06d006c0(&info.ObjectId, &obj, &sys) (the sys out is the
    //                         param_1 stack slot); false -> return
    //   06cf0649 .. 06cf064f  sys == 0 -> return
    //   06cf0651 .. 06cf067b  PlayerDB::is_valid inlined (sid != 0, sid-1 < max,
    //                         client_id +0x348 != 0); failing it jumps to 06cf06a1, i.e.
    //                         skips ONLY the bound check below
    //   06cf067d .. 06cf069f  FUN_06d498f0 (PlayerData_CheckPositionBound) on the slot with
    //                         (&info.Position, info+0x20, info+0x24); false -> return
    //   06cf06a1 .. 06cf06a5  sys->FUN_06d0bbc0(info, session_id) (StarSystem::sp_obj_update)
void IServerImpl::SPObjUpdate(const SSPObjUpdateInfo& info, unsigned int session_id) {
        if (SinglePlayer()) return;                                          // .text:06cf062c
        GameObject* Obj = nullptr;
        StarSystem* System = nullptr;
        if (!Server_FindObject(info.ObjectId, Obj, System)) return;          // .text:06cf0647
        if (System == nullptr) return;                                       // .text:06cf064f
        if (Players.is_valid(session_id)) {                                  // .text:06cf0657 .. 06cf067b
            // FUN_06d498f0 reads only its first stack argument; the port's helper takes
            // just that one (common_server_playerdb.h).
            if (!PlayerData_CheckPositionBound(Players[session_id], &info.Position)) // .text:06cf0698
                return;                                                      // .text:06cf069f
        }
        System->sp_obj_update(info, session_id);                             // .text:06cf06a5
    }

    // SPMunitionCollision -- retail .text:06cf06c0 .. 06cf0744, RET 8. Ported
    // 2026-09-24 (claude-code:w5-server-06cf):
    //   06cf06c4 .. 06cf06eb  PlayerDB::is_valid inlined: sid != 0, sid-1 < max,
    //                         client_id (+0x348) != 0 -- else out
    //   06cf06f3 .. 06cf0703  [slot-0x34] & 0x3fffffff == 0 (not in a ship) -> out
    //   06cf0705 .. 06cf0715  FUN_06d4ed00(slot) = FLD [slot+0x268], the relative
    //                         health; FCOMP double [06d65190] (0.0001), TEST AH,0x41
    //                         / JNP out -- i.e. out when health < or == 0.0001
    //                         (exactly one of C0/C3 set); unordered continues
    //   06cf0729  FUN_06d006c0(&info.ObjectId [info+8], &obj, &sys); AL == 0 -> out
    //   06cf073b  sys == 0 -> out
    //   06cf073e  sys->FUN_06d0d500(info) = StarSystem::munition_collision
void IServerImpl::SPMunitionCollision(const SSPMunitionCollisionInfo& info, unsigned int session_id) {
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        if ((p->ship_object_id & 0x3fffffff) == 0) return;        // .text:06cf0703
        double const Health = static_cast<double>(p->relative_health);   // FUN_06d4ed00
        if (Health < 0.0001 || Health == 0.0001) return;          // .text:06cf0715
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(info.ObjectId, Obj, Sys))
            return;
        if (Sys == nullptr)
            return;
        Sys->munition_collision(info);
    }

    // SPBadLandsObjCollision -- retail .text:06cf0790 .. 06cf07c0, RET 8. Ported
    // 2026-09-24 (claude-code:w5-server-06cf). No session test at all:
    //   06cf07a4  FUN_06d006c0(&info.ObjectId [info+4], &obj, &sys); AL == 0 -> out
    //   06cf07b6  sys == 0 -> out
    //   06cf07b9  sys->FUN_06d0d6e0(info) = StarSystem::badlands_collision
    // session_id is not read.
void IServerImpl::SPBadLandsObjCollision(const SSPBadLandsObjCollisionInfo& info, unsigned int session_id) {
        (void)session_id;
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(info.ObjectId, Obj, Sys))
            return;
        if (Sys == nullptr)
            return;
        Sys->badlands_collision(info);
    }

    // SPObjCollision -- retail .text:06cf0750 .. 06cf0785, RET 8. Ported
    // 2026-09-24 (claude-code:w5-server-06cf). No session test at all:
    //   06cf0764  FUN_06d006c0(&info.ObjectId [info+8], &obj, &sys); AL == 0 -> out
    //   06cf0776  sys == 0 -> out
    //   06cf077e  sys->FUN_06d0d730(info, obj) = StarSystem::collision_damage
    // Retail hands the 0x14-byte record straight on: SSPObjCollisionInfo IS the
    // FCollisionDamage record GameObject::on_collision builds (+0 source id,
    // +4 source sub-object, +8 target id -- the object looked up here -- +0xc the
    // target's sub-object, +0x10 the damage). The static_asserts below pin the
    // two declarations to the same offsets; the pointer is passed unchanged, as
    // retail passes ESI. session_id is not read.
void IServerImpl::SPObjCollision(const SSPObjCollisionInfo& info, unsigned int session_id) {
        static_assert(sizeof(SSPObjCollisionInfo) == sizeof(FCollisionDamage),
                      "SSPObjCollisionInfo is the FCollisionDamage record");
        static_assert(offsetof(SSPObjCollisionInfo, ObjectId) == offsetof(FCollisionDamage, TargetId),
                      "+8 is the target FUN_06d006c0 resolves");
        static_assert(offsetof(SSPObjCollisionInfo, SubObjId) == offsetof(FCollisionDamage, TargetSubObjId),
                      "+0xc is the target's sub-object");
        static_assert(offsetof(SSPObjCollisionInfo, Field_0x10) == offsetof(FCollisionDamage, Damage),
                      "+0x10 is the damage");
        (void)session_id;
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(info.ObjectId, Obj, Sys))
            return;
        if (Sys == nullptr)
            return;
        Sys->collision_damage(reinterpret_cast<FCollisionDamage const&>(info), Obj);
    }

    // SPRequestUseItem -- retail .text:06cf1310 .. 06cf137c, RET 8. Ported
    // 2026-09-24 (claude-code:w5-server-06cf). No session test at all:
    //   06cf1322  FUN_06d006c0(&info.ObjectId, &obj, &sys); AL == 0 -> out
    //   06cf1334  obj == 0 -> out
    //   06cf1342  obj->vt[+0x160] (is_destroyed) true -> out
    //   06cf135b  IObjAffect +0x58 (word info+4 ItemId, MOVZX word info+6 Count,
    //             session_id); a non-zero result -> out (06cf1360)
    //   06cf1374  FUN_06d50af0 on Players' raw slot (terminate_trade)
void IServerImpl::SPRequestUseItem(const SSPUseItem& info, unsigned int session_id) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        if (static_cast<IObjAffect*>(Obj)->slot_58(info.ItemId, info.Count,
                                                   static_cast<int>(session_id)) != 0)
            return;
        PlayerData_TerminateTrade(Players[session_id]);
    }
    // SPRequestInvincibility -- retail .text:06cf1380 .. 06cf153c, RET 0x10, with an
    // SEH frame for the local list. Ported 2026-09-24 (claude-code:w5-server-06cf):
    //   06cf139c .. 06cf13cc  PlayerDB::is_valid inlined -> out
    //   06cf13de  RAW compare [slot-0x34] == ship_id (no mask) -> else out
    //   06cf13f4 .. 06cf13fc  the FInvincibility record on the stack: +0 reason,
    //             +4 and +5 both b_invincible, +8 below
    //   06cf1400  !b_invincible: +8 = 1.0f (06cf142d), straight to the apply
    //   else +8 = 0.0f (06cf1407), and by reason:
    //     2  (06cf143a): sys = FUN_06d00280(slot system id [slot-0x38]); null
    //        -> out. A local std::list; sys->FUN_06d0cd60(&list, 0) collects the
    //        dockable solars (06cf1484); the first whose FUN_06ce7c50(ship_id)
    //        (the ship is in its dock queue) answers true sets the flag
    //        (06cf14b0); the list is freed (FUN_06d0f620 per node, 06cf14d1,
    //        then the head); no hit -> out (06cf14ea)
    //     5  (06cf1414): straight to the apply
    //     any other: return (06cf141a)
    //   06cf14fb  FUN_06d006c0(&ship_id, &obj, &sys); AL == 0 / obj == 0 /
    //             is_destroyed -> out
    //   06cf1526  obj->vt[+0x194] set_invincible(record)
    // The record's two pad bytes (+6/+7) are never written by retail; the port
    // zeroes them (deviations.md, zeroed indeterminate stack bytes).
void IServerImpl::SPRequestInvincibility(unsigned int ship_id, unsigned char b_invincible,
                                InvincibilityReason reason, unsigned int session_id) {
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        if (p->ship_object_id != ship_id) return;

        FInvincibility Cmd = {};
        Cmd.Type = static_cast<unsigned int>(reason);
        Cmd.bFromPlayers = b_invincible;
        Cmd.bProtect = b_invincible;
        if (!b_invincible)
        {
            Cmd.MaxHpLoss = 1.0f;
        }
        else
        {
            Cmd.MaxHpLoss = 0.0f;
            int const Reason = static_cast<int>(reason);
            if (Reason == 2)
            {
                StarSystem* const System = SysDB_LookupSystem(p->system_id);
                if (System == nullptr)
                    return;
                bool bQueued = false;
                std::list<GameObject*> Solars;
                System->collect_dockable_solars(Solars, 0);
                for (std::list<GameObject*>::const_iterator It = Solars.begin(); It != Solars.end(); ++It)
                {
                    if (static_cast<EqObj*>(*It)->is_ship_queued(ship_id))
                    {
                        bQueued = true;
                        break;
                    }
                }
                if (!bQueued)
                    return;
            }
            else if (Reason != 5)
            {
                return;
            }
        }

        GameObject* const Obj = Server_LookupGameObject(ship_id);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        Obj->set_invincible(Cmd);
    }
void IServerImpl::SaveGame(const CHARACTER_ID& char_id, const wchar_t* description,
                  unsigned int session_id) {
        // Retail 0x659d0 (.text:06d459d0 .. .text:06d45a2d): SP only, a live
        // session, and NOT in a ship ([slot-0x34] & 0x3fffffff == 0 at
        // .text:06d45a11 -- there is no in-base exception), then a tail JMP to
        // FUN_06d4c800(slot, char_id, description, 0) with the third stack
        // slot overwritten to 0 (.text:06d45a1a).
        // REWIRED 2026-09-24 (claude:srv-save): this used to call the
        // serializer directly and accept a ship-borne save while docked.
        // .text:06d459de..06d45a06 is PlayerDB::is_valid inlined -- `!p`; there
        // is no `active` test (removed, w4-server-guards).
        if (!SinglePlayer()) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        if ((p->ship_object_id & 0x3fffffff) != 0) return;
        PlayerData_SaveGame(*p, char_id.Buffer, description, 0);
    }
    // MissionSaveB -- retail 0x65a30 (.text:06d45a30 .. .text:06d45a7f): SP
    // only, a live session, then FUN_06d4c600(slot, mission_id) -- the mission
    // id IS the save's descrip_strid.
void IServerImpl::MissionSaveB(unsigned int session_id, unsigned long mission_id) {
        if (!SinglePlayer()) return;
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        // .text:06d45a66: client_id (+0x348) != 0 -- PlayerDB_GetPlayer's null. No
        // `active` test in retail (removed, w4-server-guards).
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        PlayerData_MissionSave(*p, static_cast<unsigned int>(mission_id));
    }
    // RequestEvent -- retail .text:06cf08e0 .. 06cf0927, RET 0x18 (six stack
    // arguments). Ported 2026-09-24 (claude-code:w5-server-06cf):
    //   06cf08f2  FUN_06d006c0(&p1, &obj, &sys); AL == 0 -> out
    //   06cf0904  obj == 0 -> out
    //   06cf0921  IObjAffect +0x5c (event_type, &p2, &p3, p4, PUSH 0 -- no
    //             RequestCB)
    // p5 is never read. No controller message: the port's id 0x21 was invented.
void IServerImpl::RequestEvent(int event_type, unsigned int p1, unsigned int p2, unsigned int p3, unsigned long p4, unsigned int p5) {
        (void)p5;
        GameObject* const Obj = Server_LookupGameObject(p1);
        if (Obj == nullptr)
            return;
        static_cast<IObjAffect*>(Obj)->slot_5c(event_type, &p2, &p3, static_cast<int>(p4),
                                               nullptr);
    }
    // RequestCancel -- retail .text:06cf0930 .. 06cf0970, RET 0x14 (five stack
    // arguments). Ported 2026-09-24 (claude-code:w5-server-06cf):
    //   06cf0942  FUN_06d006c0(&p1, &obj, &sys); AL == 0 -> out
    //   06cf0954  obj == 0 -> out
    //   06cf096a  IObjAffect +0x60 (event_type, &p2, p3)
    // p4 is never read. No controller message: the port's id 0x22 was invented.
void IServerImpl::RequestCancel(int event_type, unsigned int p1, unsigned int p2, unsigned long p3, unsigned int p4) {
        (void)p4;
        GameObject* const Obj = Server_LookupGameObject(p1);
        if (Obj == nullptr)
            return;
        static_cast<IObjAffect*>(Obj)->slot_60(event_type, &p2, static_cast<int>(p3));
    }
    // MineAsteroid -- retail .text:06cf07d0 .. 06cf07f8, RET 0x18. Ported
    // 2026-09-24 (claude-code:w5-server-06d0):
    //   06cf07d0 .. 06cf07d5  FUN_06d00280(system_id), the SysDB lookup; the
    //                         result is not null-tested
    //   06cf07da .. 06cf07f3  FUN_06d0d9b0 (StarSystem::spawn_mined_loot) with
    //                         (&position, arg3, arg4, arg5); the sixth argument
    //                         is never read. No controller message: the port's
    //                         id 0x23 was invented.
void IServerImpl::MineAsteroid(unsigned int system_id, const Vector& position, unsigned int loot_id,
                      unsigned int secondary_loot, unsigned int quantity, unsigned int session_id) {
        (void)session_id;
        StarSystem* const System = SysDB_LookupSystem(system_id);
        System->spawn_mined_loot(position, loot_id, secondary_loot, static_cast<int>(quantity));
    }
    // CommComplete -- retail .text:06cf0850 .. 06cf08d1, RET 0x10. Ported
    // 2026-09-24 (claude-code:w5-server-06cf):
    //   06cf0862  FUN_06d006c0(&arg1, &obj, &sys); AL != 0 and sys != 0 -> 06cf0899
    //   06cf0885  otherwise FUN_06d006c0(&arg0, &obj, &sys); AL == 0 or sys == 0
    //             skips the conversion (06cf088f / 06cf0897)
    //   06cf0899  FUN_06d06cd0(sys) -- the system's nickname id -- into payload+4
    //   06cf08a2  payload {arg1, system id, arg0, arg2, result} (five dwords)
    //   06cf08c9  FUN_06d43650(0xb, &payload)
    // When neither id resolves, retail never writes payload+4 and sends whatever
    // the stack held; the port sends 0 there (deviations.md, zeroed indeterminate
    // stack bytes). FUN_06d006c0 = Server_FindObject.
void IServerImpl::CommComplete(unsigned int arg0, unsigned int arg1, unsigned int arg2, CommResult result) {
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        unsigned int SystemRef = 0;
        if (Server_FindObject(arg1, Obj, Sys) && Sys != nullptr)
            SystemRef = Sys->SystemInfo->NicknameId;
        else if (Server_FindObject(arg0, Obj, Sys) && Sys != nullptr)
            SystemRef = Sys->SystemInfo->NicknameId;
        unsigned int msg_data[5] = { arg1, SystemRef, arg0, arg2, (unsigned int)result };
        Server_ControllerDispatch(0, pub::Controller::EMessage::CommComplete, msg_data);
    }
    // RequestCreateShip — retail 0xf9e0: validates player, gets/creates FLID,
    // then IClient +0xf4 (SendServerRequestCreateShipResp) directly — not a
    // controller message. Listing: .text:06cfa3xx `CALL [DAT_06d8fb14+0xf4]`.
    //
    // CORRECTED 2026-09-24 (claude-code:w3-baseenter): the field is [slot-0x30] =
    // +0x3E8 (created_ship_id), read at .text:06cefa1e and written at
    // .text:06cefa77 -- NOT ship_object_id (+0x3E4 = -0x34), which the port used
    // to read and write here. PlayerLaunch checks its argument against the same
    // +0x3E8 (.text:06cefb98). The `g_pClientInterface != nullptr` tests are gone
    // too: .text:06cefa29 / .text:06cefa7b load DAT_06d8fb14 and call through it.
    // Retail's null-CFLIDMaker path stores `sid & 0xffffff00` (the argument slot
    // with its low byte cleared, .text:06cefa68/06cefa6d) as both flid and flag;
    // for every session id below 0x100 that is the 0 / false the port passes.
void IServerImpl::RequestCreateShip(unsigned int session_id) {
        if (!Players.is_valid(session_id)) return;
        PlayerSlot* p = &Players[session_id];
        if ((p->created_ship_id & 0x3fffffff) != 0) {
            // .text:06cefa35 -- SendServerRequestCreateShipResp(session, true, existing)
            g_pClientInterface->SendServerRequestCreateShipResp(session_id, true, p->created_ship_id);
            return;
        }
        // Retail 0xfa3d: CFLIDMaker::GetInstance(), then post-increment the dynamic-id
        // counter at maker+4 and mask the pre-increment value to 30 bits. A null maker
        // yields flid 0 with the success flag cleared.
        unsigned int flid = 0;
        unsigned int flag = 0;
        CFLIDMaker* Maker = CFLIDMaker::GetInstance();
        if (Maker != nullptr) {
            flid = Maker->AllocateObjectID();
            flag = (flid != 0) ? 1u : 0u;
        }
        p->created_ship_id = flid;   // .text:06cefa77 [slot-0x30]
        // .text:06cefa8a  CALL [EAX+0xf4](session, flag, flid)
        g_pClientInterface->SendServerRequestCreateShipResp(session_id, flag != 0, flid);
    }
    // SPScanCargo — retail 0x112e0: sends message 3
void IServerImpl::SPScanCargo(const unsigned int& scanner_id, const unsigned int& target_id,
                     unsigned int session_id) {
        // Retail .text:06cf12e0..06cf1306 has no branch and never reads the session:
        //   06cf12e7 ECX = *scanner_id -> [ESP]    (local_8)
        //   06cf12ed EAX = *target_id  -> [ESP+4]  (local_4, stored at 06cf12fa)
        //   06cf12fe FUN_06d43650(3, &local_8)
        // i.e. a TWO-dword payload. The port's `!p || !p->active` early-out and the
        // trailing session dword were additions and are gone (w4-server-guards).
        (void)session_id;
        // Retail 0x112e0 sends message 3; rpclocal serializes both ids (ch3 op 0x2b).
        unsigned int msg_data[2] = { scanner_id, target_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::CargoScanned, msg_data);
    }
    // SetManeuver -- retail .text:06cf1540 .. .text:06cf15de (RET 8).
    // Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
    //   06cf1549 .. 06cf1561  FUN_06d006c0(&info.ObjectId, &obj, &sys) (the obj out is
    //                         the param_2 stack slot); false -> return
    //   06cf1563 .. 06cf1577  obj == 0 -> return; its CBase::Object (+0x10) == 0 -> return
    //   06cf1579 .. 06cf1588  (ObjectClass & 0x103) != 0x103 -> return
    //   06cf158a .. 06cf15a9  CEqObj::get_behavior_interface(); non-null -> its slot 0
    //                         (Common LAB_062d3f90) with the event {2, &info}
    //   06cf15ab .. 06cf15d1  SinglePlayer() -> FUN_06d43650(0x2f, {session_id,
    //                         info.ObjectId (06cf15be), info.Field_0x08 (06cf15c9)})
    // Every early out skips the single-player frame as well.
void IServerImpl::SetManeuver(unsigned int session_id, const XSetManeuver& info) {
        GameObject* Obj = nullptr;
        StarSystem* System = nullptr;
        if (!Server_FindObject(info.ObjectId, Obj, System)) return;          // .text:06cf1561
        if (Obj == nullptr) return;                                          // .text:06cf1569
        CObject* const Raw = Obj->Object;                                    // .text:06cf1572
        if (Raw == nullptr) return;                                          // .text:06cf1577
        if ((static_cast<unsigned int>(Raw->ObjectClass) & 0x103u) != 0x103u) return; // .text:06cf1588
        IBehaviorManager* const Manager =
            static_cast<CEqObj*>(Raw)->get_behavior_interface();             // .text:06cf158a
        if (Manager != nullptr) {                                            // .text:06cf1592
            FBehaviorManagerEvent Event;
            Event.Type = 2;                                                  // .text:06cf159d
            Event.Data = const_cast<XSetManeuver*>(&info);                   // .text:06cf15a5
            Manager->Slot00(&Event);                                         // .text:06cf15a9
        }
        if (SinglePlayer()) {                                                // .text:06cf15b3
            unsigned int msg_data[3];
            msg_data[0] = session_id;                                        // .text:06cf15cd
            msg_data[1] = info.ObjectId;                                     // .text:06cf15be
            msg_data[2] = info.Field_0x08;                                   // .text:06cf15c9
            Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerManeuver, msg_data); // .text:06cf15d1
        }
    }
    // InterfaceItemUsed — retail 0x657b0: sends message 0x21
void IServerImpl::InterfaceItemUsed(unsigned int session_id, unsigned int item_id) {
        // .text:06d457b0..06d457d2 has no branch: {param_1, param_2} ->
        // FUN_06d43650(0x21, &frame). The port's `!p || !p->active` early-out was
        // an addition and is gone (w4-server-guards).
        unsigned int msg_data[2] = { session_id, item_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::InterfaceItemUsed, msg_data);
    }
void IServerImpl::AbortMission(unsigned int session_id, unsigned int mission_id) {
        // Retail 0x65810: send message 0x24, increment abort counter
        unsigned int msg_data[2] = { session_id, mission_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::AbortMission, msg_data);
        // .text:06d4582f INC [.data:06d8f9b4] == GServerStats + 0x1c.
        GServerStats.AbortMissionCount++;
    }
    // RTCDone — retail: sends message 0x22 via pub::Controller::_SendMessage
void IServerImpl::RTCDone(unsigned int session_id, unsigned int rtc_id) {
        unsigned int msg_data[2] = { session_id, rtc_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::RtcDone, msg_data);
    }
    // SetWeaponGroup — retail 0x10e40: validates player connected, calls FUN_06d504f0(player, data)
    // FUN_06d504f0: copies null-terminated string into player+0x380 (weapon group name)
void IServerImpl::SetWeaponGroup(unsigned int session_id, unsigned char* data, int size) {
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        // .text:06cf0e6c: client_id (+0x348) != 0 -- PlayerDB_GetPlayer's null. No
        // `active` test in retail (removed, w4-server-guards).
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        // Retail FUN_06d504f0 (0x06d504f0..0x06d5059a) is the VC6 inline
        // basic_string::assign(const char*) on player+0x380: strlen of the whole
        // NUL-terminated buffer (06d50505 REPNE SCASB), no bound from `size` and no
        // null check on `data` -- the caller at 06cf0e40 has neither guard either.
        (void)size;
        p->weapon_group_name = reinterpret_cast<const char*>(data);
    }
    // SetVisitedState — retail 0x10e90: iterates buffer entries, calls FUN_06d508a0 per entry
    // FUN_06d508a0: finds or inserts {system_id, visit_flags} in player+0x3A0 sorted list,
    //               ORs new flags with existing if entry already present
    // Retail .text:06cf0e90 .. .text:06cf0efb (RET 0xc), re-checked 2026-09-24
    // (claude-code:w3-baseenter): PlayerDB::is_valid inlined (06cf0e94..06cf0ebc),
    // then count = *(int*)data (06cf0ec2, signed JLE at 06cf0ec9) and the 5-byte
    // record loop into FUN_06d508a0. There is no `data` null test, no data_len
    // test (param_3 is never read) and no port `active` test -- all three were
    // port additions and are gone.
void IServerImpl::SetVisitedState(unsigned int session_id, unsigned char* data, int data_len) {
        (void)data_len;
        if (!Players.is_valid(session_id)) return;
        PlayerSlot* p = &Players[session_id];

        int count = *(int*)data;
        unsigned char* ptr = data + 4;
        for (int i = 0; i < count; i++) {
            unsigned int system_id = *(unsigned int*)ptr;
            unsigned char visit_flags = ptr[4];
            ptr += 5;
            // Retail FUN_06d508a0 (0x06d508a0): lower_bound on the player's visited
            // MAP at +0x3a0, OR the incoming flags into an existing entry, otherwise
            // insert a fresh {system_id, flags} node at the hint position.
            //
            // That is PlayerData::visit_map, NOT the pending queue at +0x3b8 this
            // used to write. The two are different containers (see the field note
            // in common_server_playerdb.h): +0x3b8 is appended by FUN_06d4b270 and
            // drained into the map by FUN_06d4b450 when the player leaves a system.
            std::map<unsigned int, unsigned char>::iterator Existing =
                p->visit_map.find(system_id);
            if (Existing != p->visit_map.end()) {
                Existing->second = (unsigned char)(Existing->second | visit_flags);
            } else {
                p->visit_map[system_id] = visit_flags;
            }
        }
    }
    // RequestBestPath — retail 0x10f00: packs 9 fields from buffer, sends message 0x31
void IServerImpl::RequestBestPath(unsigned int session_id, unsigned char* data, int data_len) {
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        // .text:06cf0f33: client_id (+0x348) != 0 -- PlayerDB_GetPlayer's null.
        // Retail has no `active` test and no test on `data` / `data_len` (param_3
        // is never read); both were port additions and are gone (w4-server-guards).
        (void)data_len;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;

        // Retail stack layout (0x10f00), ascending from the message base:
        // session, +0x00, +0x1c, +0x0c, +0x10, +0x14, +0x20, +0x24, +0x28, +0x30.
        // (local_4 holds data+0x30 and sits LAST, at message offset +0x24.)
        unsigned int msg_data[10];
        msg_data[0] = session_id;
        msg_data[1] = *(unsigned int*)(data + 0x00);
        msg_data[2] = *(unsigned int*)(data + 0x1c);
        msg_data[3] = *(unsigned int*)(data + 0x0c);
        msg_data[4] = *(unsigned int*)(data + 0x10);
        msg_data[5] = *(unsigned int*)(data + 0x14);
        msg_data[6] = *(unsigned int*)(data + 0x20);
        msg_data[7] = *(unsigned int*)(data + 0x24);
        msg_data[8] = *(unsigned int*)(data + 0x28);
        msg_data[9] = *(unsigned int*)(data + 0x30);
        Server_ControllerDispatch(0, pub::Controller::EMessage::RequestBestPath, msg_data);
    }
    // RequestPlayerStats — retail 0x10fa0: validates player, sends message 0x33
void IServerImpl::RequestPlayerStats(unsigned int session_id, unsigned char* data, int data_len) {
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        // .text:06cf0fcc: client_id (+0x348) != 0 -- PlayerDB_GetPlayer's null. No
        // `active` test in retail (removed, w4-server-guards).
        (void)data; (void)data_len;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        Server_ControllerDispatch(0, pub::Controller::EMessage::RequestPlayerStats, &session_id);
    }
    // PopUpDialog — retail 0x10ff0: sends message 0x30
void IServerImpl::PopUpDialog(unsigned int session_id, unsigned int arg1) {
        // Retail 0x10ff0 is unconditional: it just sends {session_id, arg1} as message 0x30.
        unsigned int msg_data[2] = { session_id, arg1 };
        Server_ControllerDispatch(0, pub::Controller::EMessage::PopUpDialog, msg_data);
    }
    // ---------------------------------------------------------------------
    // RequestGroupPositions — retail 0x11020 (0x06cf1020), ordinal 380
    // Guard: session in range and Players[session][+0x348] (the connection id)
    // non-zero (0x06cf104d). Then group = Players[session][-0x14]; a null group
    // takes the short arm at 0x06cf1194 -- operator new(4), store 0, and send it
    // with size 4.
    //
    // Populated arm:
    //   Count  = group->GetMemberCount()                     (0x06cf1077)
    //   Others = Count > 0 ? Count - 1 : Count                (0x06cf1082/0x06cf1084)
    //   Buf    = operator new(Others * 0x18 + 4)              (0x06cf108a/0x06cf1096)
    //   *(uint*)Buf = Others                                  (0x06cf10a4)
    // then one 0x18-byte entry per member EXCEPT the requester himself
    // (0x06cf10bd CMP against param_1):
    //   +0x00 float Pos[3]
    //   +0x0c uint  BaseId   -- only on the in-base arm (0x06cf115e), from [-0x2c]
    //   +0x10 uint  SystemId -- [-0x38]  (0x06cf10d7)
    //   +0x14 uint  Vibe     -- [-0x100] (0x06cf10e7)
    // When the member has a ship ([-0x34] non-zero) BaseId is zeroed (0x06cf10fc)
    // and the position comes from FUN_06d00280(SystemId) -> FUN_06d0c8d0(ship,
    // 0x7f0000) -> obj->vtable[0](); when that lookup fails retail writes three
    // zeros (0x06cf1132-0x06cf113b). Finally IClient vtable +0x180 == slot 96
    // (FLPACKET_COMMON_REQUEST_GROUP_POSITIONS) with (session, Buf, BufSize)
    // at 0x06cf1184. Retail never frees Buf on either arm -- the RPC layer owns it.
    //
    // The incoming request payload (data/size) is not parsed -- retail does not
    // read it either.
    // ---------------------------------------------------------------------
void IServerImpl::RequestGroupPositions(unsigned int session_id, unsigned char* data, int size) {
        (void)data; (void)size;
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;   // 06cf1028 / 06cf1039
        // 06cf104d: Players[session]+0x348 (client_id) != 0 -- PlayerDB_GetPlayer's null.
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;

        CPlayerGroup* Group = (CPlayerGroup*)p->group_ptr;               // 06cf1064
        if (!Group) {
            // 06cf1194 .. 06cf11b0: a bare operator new(4) holding 0. The block is
            // handed over: rpclocal's sender (0x06d8b7a0) sets the packet's
            // owns-buffer flag and the packet destructor frees it.
            unsigned int* Empty = static_cast<unsigned int*>(::operator new(4));
            *Empty = 0;
            g_pClientInterface->SendCommonRequestGroupPositions(
                session_id, reinterpret_cast<unsigned char*>(Empty), 4);
            return;
        }

        unsigned int Count = Group->GetMemberCount();
        // 06cf107e .. 06cf1084: TEST EBP,EBP / JLE -- a signed test.
        unsigned int Others = (static_cast<int>(Count) > 0) ? (Count - 1) : Count;
        unsigned int BufSize = Others * 0x18 + 4;
        // 06cf1096: bare operator new, no initialisation -- handed over as above.
        unsigned char* Buf = static_cast<unsigned char*>(::operator new(BufSize));
        *(unsigned int*)Buf = Others;                                      // 06cf10a4

        unsigned char* Entry = Buf + 4;
        for (int i = 0; i < static_cast<int>(Count); ++i) {             // 06cf1169 JL, signed
            unsigned int MemberId = Group->GetMember(i);
            if (MemberId == session_id) continue;                          // 06cf10c1: no advance
            // Retail indexes Players[member] directly, no logged-in test (06cf10c7).
            PlayerSlot* Member = &Players[MemberId];
            *(unsigned int*)(Entry + 0x10) = Member->system_id;            // 06cf10d7
            *(unsigned int*)(Entry + 0x14) = (unsigned int)Member->rep_vibe; // 06cf10e7
            if (Member->ship_object_id != 0) {
                *(unsigned int*)(Entry + 0x0c) = 0;                        // 06cf10fc
                // 06cf1103 .. 06cf1113: FUN_06d00280(SystemId) (no null test on
                // the result) -> FUN_06d0c8d0(sys, ship, 0x7f0000).
                StarSystem* const MemberSystem =
                    SysDB_LookupSystem(*(unsigned int*)(Entry + 0x10));
                GameObject* const MemberShip =
                    MemberSystem->find_object(Member->ship_object_id, 0x7f0000u);
                if (MemberShip == nullptr) {
                    // 06cf111c .. 06cf113b: three zero dwords.
                    *(unsigned int*)(Entry + 0x00) = 0;
                    *(unsigned int*)(Entry + 0x04) = 0;
                    *(unsigned int*)(Entry + 0x08) = 0;
                } else {
                    // 06cf1140 .. 06cf1155: IObjInspect slot 0 (get_position),
                    // its three dwords copied.
                    Vector const& Pos = MemberShip->get_position();
                    std::memcpy(Entry, &Pos, 0x0c);
                }
            } else {
                // 06cf115a: only BaseId is written; Pos stays as operator new left it.
                *(unsigned int*)(Entry + 0x0c) = Member->current_base;
            }
            Entry += 0x18;                                                 // 06cf1165
        }

        g_pClientInterface->SendCommonRequestGroupPositions(session_id, Buf, BufSize); // 06cf1184
    }
    // SetMissionLog — retail 0x111c0: validates player, calls FUN_06d505a0(player, data, size)
    // FUN_06d505a0: case 0 = clear log (free all nodes), case 1 = add entries from buffer
void IServerImpl::SetMissionLog(unsigned int session_id, unsigned char* data, int data_len) {
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        // .text:06cf11ec: client_id (+0x348) != 0 -- PlayerDB_GetPlayer's null. No
        // `active` test in retail (removed, w4-server-guards).
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;

        // Retail 0x06cf11c0 is ONLY the player validation plus
        //   FUN_06d505a0(player_record, data, data_len)      (0x06cf1205)
        // — no pub::Controller message of any id, and no check on data/data_len.
        //
        // FUN_06d505a0 (PlayerData method, RET 0x8) on the std::list<FmtStr> at +0x390
        // (head +0x394, size +0x398):
        //   op = data[0]                                      06d505c0
        //   op 0: erase every node, --size per node           06d505ca .. 06d5062d
        //   op 1: data_len -= 8; count = data[1]; cursor = data + 8   06d5063d .. 06d5064a
        //         while count-- > 0 (signed, JLE at 06d5064e):
        //           FmtStr tmp (inline zeroing of 0x18 bytes) 06d50658 .. 06d50694
        //           ExtractMissionLogData(&tmp, cursor, data_len)      06d506ab
        //           insert a copy BEFORE begin() -- the new node's _Next is
        //           [_Head] (06d506b7), so this is push_front, not push_back
        //                                                     06d506b1 .. 06d506f8
        //   any other op: nothing.
        int* const Ops = reinterpret_cast<int*>(data);
        if (Ops[0] == 0) {
            p->mission_log.clear();
        } else if (Ops[0] == 1) {
            data_len -= 8;
            int Count = Ops[1];
            unsigned char* Cursor = data + 8;
            for (; Count > 0; --Count) {
                FmtStr Entry;
                ExtractMissionLogData(&Entry, Cursor, data_len);
                p->mission_log.push_front(Entry);
            }
        }
    }
    // SetInterfaceState — retail 0x11210: validates player, calls FUN_06d50730(player, data)
    // FUN_06d50730: trivially stores *data into player+0x39C (interface state flag)
    // Retail .text:06cf1210 .. .text:06cf125a (RET 0xc), re-checked 2026-09-24
    // (claude-code:w3-baseenter): PlayerDB::is_valid inlined, then
    // FUN_06d50730(slot, data, data_len), whose whole body is
    // `MOV EAX,[ESP+4] / MOV EDX,[EAX] / MOV [this+0x39c],EDX / RET 8` -- data_len
    // is pushed but never read, and nothing tests `data`. The port's
    // `!data || data_len < 4` and `active` tests were additions and are gone.
void IServerImpl::SetInterfaceState(unsigned int session_id, unsigned char* data, int data_len) {
        (void)data_len;
        if (!Players.is_valid(session_id)) return;
        Players[session_id].interface_state = *(unsigned int*)data;
    }
    // RequestRankLevel — retail 0x11260: SP check, reads player+0x284, sends via controller vtable+0x18c
    // FUN_06d4ec00: returns *(uint*)(player + 0x284) — the rank level
void IServerImpl::RequestRankLevel(unsigned int session_id, unsigned char* data, int data_len) {
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        // 06cf1282: Players[session]+0x348 (client_id) != 0 -- PlayerDB_GetPlayer's null.
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        // 0x18c is a vtable BYTE offset, not a message id: 0x18c / 4 = IClient slot 99
        // (FLPACKET_COMMON_REQUEST_RANK_LEVEL). Retail allocates the 4-byte block FIRST
        // (06cf128e operator new(4)), then fills it: SinglePlayer() ? FUN_06d4ec00(player)
        // (= [player+0x284], rank_level) : 0xffffffff (06cf129a .. 06cf12c1), and sends
        // SendCommonRequestRankLevel(session_id, block, 4) at 06cf12d3. The block is handed over and never
        // freed here: rpclocal's sender (0x06d8b890) sets the packet's owns-buffer flag
        // (06d8b8c3) and the packet destructor frees it.
        unsigned int* const RankBlock = static_cast<unsigned int*>(::operator new(4));
        if (SinglePlayer()) {
            *RankBlock = p->rank_level;
        } else {
            *RankBlock = 0xFFFFFFFF;
        }
        g_pClientInterface->SendCommonRequestRankLevel(
            session_id, reinterpret_cast<unsigned char*>(RankBlock), 4);
    }
    // Trade system. The offers are Common.dll's own CPlayerTradeOffer (common_tradeoffer.h,
    // retail sizeof 0x24): {vfptr, +4 SourcePlayerId, +8 TargetPlayerId, +0xc TargetOffer,
    // +0x10 EquipDescList, +0x1c MoneyOffered, +0x20 Accepted}. Server.dll imports its
    // ctor/dtor, SetSourcePlayerID, SetTargetPlayerID, SetAccepted, SetMoneyOffered
    // (IAT .rdata:06d643e0 .. 06d64410) and reads +8 / +0xc / +0x10 / +0x1c / +0x20
    // inline; the port reaches the same fields through the exported accessors. The
    // port's own FTradeOffer struct that stood in for it until 2026-09-24 is gone.

    // Slot 79: InitiateTrade -- retail .text:06cf1780 .. 06cf1963, RET 8, with an SEH
    // frame around the two allocations. Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
    //   06cf179e .. 06cf17ca  PlayerDB::is_valid inlined on the TARGET only; failure is
    //                         the self-call TerminateTrade(source, 2) (06cf17d5)
    //   06cf17ec  Players[target].trade_request_partner [slot-0x48] != source ->
    //             TerminateTrade(source, 2) (06cf17fb)
    //   06cf1806  source == target -> TerminateTrade(source, 2) (06cf180f)
    //   06cf181a .. 06cf1839  the SOURCE slot, raw: a live offer -> TerminateTrade(
    //             source, 4) and FALL THROUGH (Players reloaded at 06cf1839)
    //   06cf183f .. 06cf1858  the target has a live offer -> IClient +0x198
    //             (SendCommonPlayerTradeTerminateTrade)(source, 6), return
    //   06cf185d .. 06cf18bf  two operator new(0x24) + CPlayerTradeOffer::
    //             CPlayerTradeOffer (IAT 06d643e8); the vfptr is then set to Server's
    //             own copy of the vtable (.rdata:06d65f80, sole slot = the deleting dtor
    //             FUN_06cf1970 = ~CPlayerTradeOffer + operator delete) -- `new` here
    //   06cf18c1 .. 06cf18f4  A = source's: SetSourcePlayerID(source), SetTargetPlayerID(
    //             target), A+0xc = B; B = target's: SetSourcePlayerID(target),
    //             SetTargetPlayerID(source), B+0xc = A
    //   06cf18f7 .. 06cf190e  Players[source].trade_offer = A, Players[target].trade_offer = B
    //   06cf1915  self-call IServer +0x158 StopTradeRequest(target)
    //   06cf192e  SendCommonPlayerTradeInitiateTrade(source, Players[target].ship_object_id [slot-0x34])
    //   06cf194b  SendCommonPlayerTradeInitiateTrade(target, Players[source].ship_object_id)
    // Retail writes neither side's trade_request_partner here; the port's two
    // `= 0` stores (which also stopped StopTradeRequest from ever sending
    // SendCommonPlayerTradeStopTradeRequest) are
    // gone. No null test on DAT_06d8fb14 (the IClient pointer) anywhere in the family.
void IServerImpl::InitiateTrade(unsigned int session_id, unsigned int target_id) {
        PlayerSlot* const target = GetPlayerBySession(target_id);
        if (!target) {
            TerminateTrade(session_id, 2);                     // .text:06cf17d5
            return;
        }
        if (target->trade_request_partner != session_id) {
            TerminateTrade(session_id, 2);                     // .text:06cf17fb
            return;
        }
        if (session_id == target_id) {
            TerminateTrade(session_id, 2);                     // .text:06cf180f
            return;
        }

        PlayerSlot* const p = &Players[session_id];            // .text:06cf181a, raw
        if (p->trade_offer != nullptr)
            TerminateTrade(session_id, 4);                     // .text:06cf1833, falls through

        if (target->trade_offer != nullptr) {
            g_pClientInterface->SendCommonPlayerTradeTerminateTrade(session_id, 6);   // .text:06cf1852
            return;
        }

        CPlayerTradeOffer* const SourceOffer = new CPlayerTradeOffer();   // .text:06cf185f
        CPlayerTradeOffer* const TargetOffer = new CPlayerTradeOffer();   // .text:06cf1895

        SourceOffer->SetSourcePlayerID(session_id);            // .text:06cf18cc
        SourceOffer->SetTargetPlayerID(target_id);             // .text:06cf18d5
        SourceOffer->SetTargetOffer(TargetOffer);              // .text:06cf18de
        TargetOffer->SetSourcePlayerID(target_id);             // .text:06cf18e1
        TargetOffer->SetTargetPlayerID(session_id);            // .text:06cf18ea
        TargetOffer->SetTargetOffer(SourceOffer);              // .text:06cf18f4

        Players[session_id].trade_offer = SourceOffer;         // .text:06cf18fc
        Players[target_id].trade_offer = TargetOffer;          // .text:06cf190e

        StopTradeRequest(target_id);                           // .text:06cf1915

        g_pClientInterface->SendCommonPlayerTradeInitiateTrade(session_id, Players[target_id].ship_object_id);   // .text:06cf192e
        g_pClientInterface->SendCommonPlayerTradeInitiateTrade(target_id, Players[session_id].ship_object_id);   // .text:06cf194b
    }

    // Slot 80: TerminateTrade -- retail .text:06cf1990 .. 06cf1a39, RET 8. Ported 1:1:
    //   06cf1991 .. 06cf19c3  PlayerDB::is_valid inlined -> out
    //   06cf19d2 .. 06cf19db  offer = [slot-0x4c]; null -> straight to the last
    //                         SendCommonPlayerTradeTerminateTrade
    //   06cf19de .. 06cf19e3  partner id = offer+8, then the slot's offer is cleared
    //   06cf19e1 / 06cf19f0   partner id == 0 or offer+0xc == 0 -> skip the partner arm
    //   06cf19ff  Players[partner].trade_offer = 0 on a RAW slot (no is_valid)
    //   06cf1a0b  (offer+0xc)->vtable[0](1) -- delete the partner's offer
    //   06cf1a17  SendCommonPlayerTradeTerminateTrade(partner, reason)
    //   06cf1a23  offer->vtable[0](1) -- delete the own offer
    //   06cf1a30  SendCommonPlayerTradeTerminateTrade(session, reason), outside the offer branch
void IServerImpl::TerminateTrade(unsigned int session_id, int reason) {
        PlayerSlot* const p = GetPlayerBySession(session_id);
        if (!p) return;

        CPlayerTradeOffer* const Offer = p->trade_offer;
        if (Offer != nullptr) {
            unsigned int const PartnerId = Offer->GetTargetPlayerID();
            p->trade_offer = nullptr;
            CPlayerTradeOffer* const PartnerOffer = Offer->GetTargetOffer();
            if (PartnerId != 0 && PartnerOffer != nullptr) {
                Players[PartnerId].trade_offer = nullptr;
                delete PartnerOffer;
                g_pClientInterface->SendCommonPlayerTradeTerminateTrade(PartnerId, reason);
            }
            delete Offer;
        }

        g_pClientInterface->SendCommonPlayerTradeTerminateTrade(session_id, reason);
    }

    // Slot 81: AcceptTrade -- retail .text:06cf1a40 .. 06cf2003, RET 8, SEH frame for the
    // two local cargo lists. Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
    //   06cf1a5a .. 06cf1a92  PlayerDB::is_valid inlined -> out
    //   06cf1a9e .. 06cf1aa8  own offer [slot-0x4c] == 0 -> out
    //   06cf1ab5  own->SetAccepted(b_accepted)
    //   06cf1abb .. 06cf1aeb  IClient +0x19c (SendCommonPlayerTradeAcceptTrade)(offer+8 = partner id,
    //             own ship_object_id [+0x3e4], b_accepted). The partner record is
    //             indexed RAW from offer+8 (06cf1ada).
    //   06cf1af1 .. 06cf1b0a  partner record's own offer [+0x3cc], NO null test:
    //             its Accepted (+0x20) != 1 -> out; own Accepted != 1 -> out
    //   06cf1b10 .. 06cf1b34  own offer's money > FUN_06d4f460(own record) (unsigned
    //             JA) -> TerminateTrade(session, 3) (06cf1feb); the same for the partner
    //   06cf1b3a .. 06cf1b86  FUN_06d006c0 on each ship id (+0x3e4); a miss zeroes
    //             that object pointer
    //   06cf1b8a .. 06cf1be0  each object's CBase::Object (+0x10), narrowed to CShip by
    //             (ObjectClass & 0x503) == 0x503, else 0
    //   06cf1be4 .. 06cf1c45  a copy of the own record's cargo list (FUN_06d4efb0 =
    //             &record+0x278, std::list copy ctor); for every item of the own offer:
    //             find_matching_cargo(ArchId, Count, Health) on the COPY, miss ->
    //             TerminateTrade(session, 3) (06cf1cc2) and out, hit ->
    //             remove_equipment_item(hit->Id, Count) on the copy
    //   06cf1c47 .. 06cf1cb1  the same for the partner's offer against a copy of the
    //             partner's list; miss -> TerminateTrade(session, 3) (06cf1cda)
    //   06cf1d06 .. 06cf1dca  free hold, per side, all through ftol:
    //             (int)((float)(int)Archetype::GetShip(record+0x264)->hold_size [+0x100]
    //                   - record's cargo get_cargo_space_occupied())
    //             - (int)incoming offer volume + (int)outgoing offer volume;
    //             either < 0 (signed) -> TerminateTrade(session, 5) (06cf1fb5)
    //   06cf1dd0 .. 06cf1df7  FUN_06d4f4e0(own, partner money - own money, 1), then
    //             FUN_06d4f4e0(partner, own money - partner money, 1) -- unconditional
    //   06cf1dfc .. 06cf1e4b  own offer's items: FUN_06d4f260(own, Arch, Count, Health,
    //             1) remove, FUN_06d4f230(partner, Arch, Count, Health, 0, 1) add
    //   06cf1e4d .. 06cf1e9b  partner offer's items: the mirror image
    //   06cf1e9d .. 06cf1ebf  CEqObj::sync_cargo(record cargo) on both narrowed ships,
    //             no null test
    //   06cf1ec1 .. 06cf1ef9  FUN_06d006c0(&own ship->Id [+0xb0]) for the SYSTEM (a
    //             miss makes it 0, used unguarded), then FUN_06d0b110(system, own
    //             object) and FUN_06d0b110(system, partner object)
    //   06cf1f0b  TerminateTrade(session, 1), then the two local lists are freed
    // FUN_06d4f460 = PlayerData_GetCash, FUN_06d4f4e0 = PlayerData_AdjustCash,
    // FUN_06d4f260 = PlayerData_RemoveCargo, FUN_06d4f230 = PlayerData_AddEquipment with
    // the cargo-bay hardpoint (see server_classes.cpp's add-cargo site), FUN_06d0b110 =
    // StarSystem_SendRemoteCargoUpdate.
void IServerImpl::AcceptTrade(unsigned int session_id, bool b_accepted) {
        PlayerSlot* const p = GetPlayerBySession(session_id);
        if (!p) return;
        CPlayerTradeOffer* const Offer = p->trade_offer;
        if (Offer == nullptr) return;

        Offer->SetAccepted(b_accepted);                                          // .text:06cf1ab5

        unsigned int const PartnerId = Offer->GetTargetPlayerID();                // .text:06cf1abb
        PlayerSlot& Own = Players[session_id];
        PlayerSlot& Partner = Players[PartnerId];                                 // raw, .text:06cf1ada
        g_pClientInterface->SendCommonPlayerTradeAcceptTrade(PartnerId, Own.ship_object_id, b_accepted);  // .text:06cf1aeb

        CPlayerTradeOffer* const PartnerOffer = Partner.trade_offer;              // .text:06cf1af1
        if (!PartnerOffer->GetAccepted()) return;                                 // .text:06cf1b00
        if (!Offer->GetAccepted()) return;                                        // .text:06cf1b0a

        if (Offer->GetMoneyOffered() > static_cast<unsigned long>(PlayerData_GetCash(Own))) {
            TerminateTrade(session_id, 3);                                        // .text:06cf1b22
            return;
        }
        if (PartnerOffer->GetMoneyOffered() > static_cast<unsigned long>(PlayerData_GetCash(Partner))) {
            TerminateTrade(session_id, 3);                                        // .text:06cf1b34
            return;
        }

        GameObject* OwnObj = nullptr;
        GameObject* PartnerObj = nullptr;
        StarSystem* LookupSystem = nullptr;
        if (!Server_FindObject(Own.ship_object_id, OwnObj, LookupSystem))         // .text:06cf1b4b
            OwnObj = nullptr;
        if (!Server_FindObject(Partner.ship_object_id, PartnerObj, LookupSystem)) // .text:06cf1b70
            PartnerObj = nullptr;

        CShip* OwnShip = nullptr;                                                 // .text:06cf1b8a
        CBase* const OwnBase = OwnObj;
        if (OwnBase != nullptr) {
            CObject* const Cobj = OwnBase->Object;
            if (Cobj != nullptr && (static_cast<unsigned int>(Cobj->ObjectClass) & 0x503u) == 0x503u)
                OwnShip = static_cast<CShip*>(Cobj);
        }
        CShip* PartnerShip = nullptr;                                             // .text:06cf1bb9
        CBase* const PartnerBase = PartnerObj;
        if (PartnerBase != nullptr) {
            CObject* const Cobj = PartnerBase->Object;
            if (Cobj != nullptr && (static_cast<unsigned int>(Cobj->ObjectClass) & 0x503u) == 0x503u)
                PartnerShip = static_cast<CShip*>(Cobj);
        }

        std::list<EquipDesc> const& OwnItems = Offer->GetList().Items;
        std::list<EquipDesc> const& PartnerItems = PartnerOffer->GetList().Items;
        std::list<EquipDesc>::const_iterator It;

        EquipDescList OwnCargo(Own.equipment);                                    // .text:06cf1bf0
        for (It = OwnItems.begin(); It != OwnItems.end(); ++It) {
            EquipDesc const* const Hit = OwnCargo.find_matching_cargo(It->ArchId, It->Count, It->Health);
            if (Hit == nullptr) {
                TerminateTrade(session_id, 3);                                    // .text:06cf1cc2
                return;
            }
            OwnCargo.remove_equipment_item(Hit->Id, It->Count);                   // .text:06cf1c3b
        }

        EquipDescList PartnerCargo(Partner.equipment);                            // .text:06cf1c53
        for (It = PartnerItems.begin(); It != PartnerItems.end(); ++It) {
            EquipDesc const* const Hit = PartnerCargo.find_matching_cargo(It->ArchId, It->Count, It->Health);
            if (Hit == nullptr) {
                TerminateTrade(session_id, 3);                                    // .text:06cf1cda
                return;
            }
            PartnerCargo.remove_equipment_item(Hit->Id, It->Count);               // .text:06cf1ca7
        }

        // .text:06cf1d06 .. 06cf1dc2. The x87 difference is taken in double: both
        // operands are floats (the hold via FILD/FSTP float, the occupied volume as
        // returned), so the subtraction is exact before ftol truncates it.
        int const OwnOfferVolume = static_cast<int>(Offer->GetList().get_cargo_space_occupied());
        int const PartnerOfferVolume = static_cast<int>(PartnerOffer->GetList().get_cargo_space_occupied());
        float const OwnHold =
            static_cast<float>(static_cast<int>(Archetype::GetShip(Own.ship_archetype)->HoldSize));
        int const OwnFree = static_cast<int>(static_cast<double>(OwnHold) -
                                             static_cast<double>(Own.equipment.get_cargo_space_occupied()))
                            - PartnerOfferVolume + OwnOfferVolume;
        float const PartnerHold =
            static_cast<float>(static_cast<int>(Archetype::GetShip(Partner.ship_archetype)->HoldSize));
        int const PartnerFree = static_cast<int>(static_cast<double>(PartnerHold) -
                                                 static_cast<double>(Partner.equipment.get_cargo_space_occupied()))
                                - OwnOfferVolume + PartnerOfferVolume;
        if (OwnFree < 0 || PartnerFree < 0) {
            TerminateTrade(session_id, 5);                                        // .text:06cf1fb5
            return;
        }

        PlayerData_AdjustCash(Own, static_cast<int>(PartnerOffer->GetMoneyOffered() - Offer->GetMoneyOffered()),
                              true);                                              // .text:06cf1de5
        PlayerData_AdjustCash(Partner, static_cast<int>(Offer->GetMoneyOffered() - PartnerOffer->GetMoneyOffered()),
                              true);                                              // .text:06cf1df7

        for (It = OwnItems.begin(); It != OwnItems.end(); ++It) {
            PlayerData_RemoveCargo(Own, It->ArchId, It->Count, It->Health, true);            // .text:06cf1e28
            PlayerData_AddEquipment(Partner, It->ArchId, EquipDesc::CARGO_BAY_HP_NAME, It->Count,
                                    It->Health, /*bEquipped*/false, /*bNoCombine*/false,
                                    /*bNotify*/true);                                        // .text:06cf1e42
        }
        for (It = PartnerItems.begin(); It != PartnerItems.end(); ++It) {
            PlayerData_RemoveCargo(Partner, It->ArchId, It->Count, It->Health, true);        // .text:06cf1e78
            PlayerData_AddEquipment(Own, It->ArchId, EquipDesc::CARGO_BAY_HP_NAME, It->Count,
                                    It->Health, /*bEquipped*/false, /*bNoCombine*/false,
                                    /*bNotify*/true);                                        // .text:06cf1e92
        }

        OwnShip->sync_cargo(Own.equipment);                                       // .text:06cf1eb1
        PartnerShip->sync_cargo(Partner.equipment);                               // .text:06cf1ebf

        GameObject* ScratchObj = nullptr;
        StarSystem* ShipSystem = nullptr;
        if (!Server_FindObject(OwnShip->Id, ScratchObj, ShipSystem))              // .text:06cf1ed2
            ShipSystem = nullptr;
        StarSystem_SendRemoteCargoUpdate(*ShipSystem, OwnObj);                    // .text:06cf1eed
        StarSystem_SendRemoteCargoUpdate(*ShipSystem, PartnerObj);                // .text:06cf1ef9

        TerminateTrade(session_id, 1);                                            // .text:06cf1f0b
    }

    // Slot 82: SetTradeMoney -- retail .text:06cf2010 .. 06cf20d0, RET 8. Ported 1:1:
    //   06cf2011 .. 06cf2045  PlayerDB::is_valid inlined -> out
    //   06cf2053 .. 06cf2059  offer [slot-0x4c] == 0 -> TerminateTrade(session, 2)
    //   06cf205b .. 06cf2064  offer+0xc != 0 -> (offer+0xc)->SetAccepted(false)
    //   06cf2072  SetMoneyOffered(money); 06cf207c SetAccepted(false)
    //   06cf2097  IClient +0x1a0 (SendCommonPlayerTradeSetTradeMoney)(session, own ship_object_id, money)
    //   06cf20b5  SendCommonPlayerTradeSetTradeMoney(offer+8, own ship_object_id, money)
void IServerImpl::SetTradeMoney(unsigned int session_id, unsigned long money) {
        PlayerSlot* const p = GetPlayerBySession(session_id);
        if (!p) return;
        CPlayerTradeOffer* const Offer = p->trade_offer;
        if (Offer == nullptr) {
            TerminateTrade(session_id, 2);                                        // .text:06cf20c7
            return;
        }
        CPlayerTradeOffer* const PartnerOffer = Offer->GetTargetOffer();
        if (PartnerOffer != nullptr)
            PartnerOffer->SetAccepted(false);                                     // .text:06cf2064
        Offer->SetMoneyOffered(money);                                            // .text:06cf2072
        Offer->SetAccepted(false);                                                // .text:06cf207c
        g_pClientInterface->SendCommonPlayerTradeSetTradeMoney(
            session_id, Players[session_id].ship_object_id, money);              // .text:06cf2097
        g_pClientInterface->SendCommonPlayerTradeSetTradeMoney(
            Offer->GetTargetPlayerID(), Players[session_id].ship_object_id, money); // .text:06cf20b5
    }

    // Slot 83: AddTradeEquip -- retail .text:06cf20e0 .. 06cf21b4, RET 8. Ported 1:1:
    //   06cf20e0 .. 06cf2115  PlayerDB::is_valid inlined -> out
    //   06cf2123 .. 06cf2129  offer == 0 -> tail JMP TerminateTrade(session, 2) (06cf21b4)
    //   06cf212b .. 06cf2134  offer+0xc != 0 -> (offer+0xc)->SetAccepted(false)
    //   06cf213a .. 06cf2150  (offer+0x10).add_equipment_item(equip,
    //             arch_is_combinable(equip.ArchId)); the result is dropped
    //   06cf215a  SetAccepted(false)
    //   06cf2179  IClient +0x1a4 (SendCommonPlayerTradeAddTradeEquip)(session, own ship_object_id, &equip)
    //   06cf2197  SendCommonPlayerTradeAddTradeEquip(offer+8, own ship_object_id, &equip)
void IServerImpl::AddTradeEquip(unsigned int session_id, const EquipDesc& equip) {
        PlayerSlot* const p = GetPlayerBySession(session_id);
        if (!p) return;
        CPlayerTradeOffer* const Offer = p->trade_offer;
        if (Offer == nullptr) {
            TerminateTrade(session_id, 2);                                        // .text:06cf21b4
            return;
        }
        CPlayerTradeOffer* const PartnerOffer = Offer->GetTargetOffer();
        if (PartnerOffer != nullptr)
            PartnerOffer->SetAccepted(false);                                     // .text:06cf2134
        Offer->GetList().add_equipment_item(equip, arch_is_combinable(equip.ArchId));   // .text:06cf2150
        Offer->SetAccepted(false);                                                // .text:06cf215a
        g_pClientInterface->SendCommonPlayerTradeAddTradeEquip(
            session_id, Players[session_id].ship_object_id, equip);              // .text:06cf2179
        g_pClientInterface->SendCommonPlayerTradeAddTradeEquip(
            Offer->GetTargetPlayerID(), Players[session_id].ship_object_id, equip); // .text:06cf2197
    }

    // Slot 84: DelTradeEquip -- retail .text:06cf21c0 .. 06cf22b8, RET 8. Ported 1:1:
    //   06cf21c0 .. 06cf21f5  PlayerDB::is_valid inlined -> out
    //   06cf2203 .. 06cf2209  offer == 0 -> tail JMP TerminateTrade(session, 2) (06cf22b8)
    //   06cf220f .. 06cf2218  offer+0xc != 0 -> (offer+0xc)->SetAccepted(false)
    //   06cf221e .. 06cf223c  (offer+0x10).find_matching_cargo(equip.ArchId, equip.Count,
    //             equip.Health)
    //   06cf2244 .. 06cf2253  a hit -> remove_equipment_item(hit->Id, equip.Count)
    //   06cf225d  SetAccepted(false)
    //   06cf227c  IClient +0x1a8 (SendCommonPlayerTradeDelTradeEquip)(session, own ship_object_id, &equip)
    //   06cf229a  SendCommonPlayerTradeDelTradeEquip(offer+8, own ship_object_id, &equip)
void IServerImpl::DelTradeEquip(unsigned int session_id, const EquipDesc& equip) {
        PlayerSlot* const p = GetPlayerBySession(session_id);
        if (!p) return;
        CPlayerTradeOffer* const Offer = p->trade_offer;
        if (Offer == nullptr) {
            TerminateTrade(session_id, 2);                                        // .text:06cf22b8
            return;
        }
        CPlayerTradeOffer* const PartnerOffer = Offer->GetTargetOffer();
        if (PartnerOffer != nullptr)
            PartnerOffer->SetAccepted(false);                                     // .text:06cf2218
        EquipDescList& List = Offer->GetList();
        EquipDesc const* const Hit = List.find_matching_cargo(equip.ArchId, equip.Count, equip.Health);
        if (Hit != nullptr)
            List.remove_equipment_item(Hit->Id, equip.Count);                     // .text:06cf2253
        Offer->SetAccepted(false);                                                // .text:06cf225d
        g_pClientInterface->SendCommonPlayerTradeDelTradeEquip(
            session_id, Players[session_id].ship_object_id, equip);              // .text:06cf227c
        g_pClientInterface->SendCommonPlayerTradeDelTradeEquip(
            Offer->GetTargetPlayerID(), Players[session_id].ship_object_id, equip); // .text:06cf229a
    }

    // Slot 85: RequestTrade -- retail .text:06cf22c0 .. 06cf234d, RET 8. Ported 1:1:
    //   06cf22c2 .. 06cf22ed  PlayerDB::is_valid inlined on the source -> out
    //   06cf22ef .. 06cf230d  the same on the target -> out
    //   06cf230f .. 06cf231f  Players[source].trade_offer != 0 -> out
    //   06cf2329 .. 06cf2333  Players[target].trade_request_partner == source ->
    //             self-call IServer +0x13c InitiateTrade(source, target), return
    //   06cf233f .. 06cf2345  otherwise FUN_06d50920(source record, target) --
    //             PlayerData_SetTradeRequest
    // Retail's own body makes no IClient call.
void IServerImpl::RequestTrade(unsigned int session_id, unsigned int target_id) {
        PlayerSlot* const p = GetPlayerBySession(session_id);
        if (!p) return;
        PlayerSlot* const target = GetPlayerBySession(target_id);
        if (!target) return;
        if (Players[session_id].trade_offer != nullptr) return;                   // .text:06cf231f
        if (Players[target_id].trade_request_partner == session_id) {
            InitiateTrade(session_id, target_id);                                 // .text:06cf2333
            return;
        }
        PlayerData_SetTradeRequest(Players[session_id], target_id);               // .text:06cf2345
    }

    // Slot 86: StopTradeRequest -- retail .text:06cf2350 .. 06cf23a8, RET 4. Ported 1:1:
    //   06cf2351 .. 06cf237b  PlayerDB::is_valid inlined -> out
    //   06cf2388  IClient +0x198 (SendCommonPlayerTradeTerminateTrade)(session, 0)
    //   06cf23a2  FUN_06d50a40(record, 1) -- PlayerData_CancelTradeRequest with the
    //             partner-list unlink on (the port used to inline half of it)
void IServerImpl::StopTradeRequest(unsigned int session_id) {
        PlayerSlot* const p = GetPlayerBySession(session_id);
        if (!p) return;
        g_pClientInterface->SendCommonPlayerTradeTerminateTrade(session_id, 0);  // .text:06cf2388
        PlayerData_CancelTradeRequest(Players[session_id], true);                 // .text:06cf23a2
    }
    // ReqDifficultyScale — retail 0x65a90: SP only, validates player, calls FUN_06d4ed40
    // FUN_06d4ed40: clamps param_1 to [0.0, 2.0], stores at player+0x3C4
void IServerImpl::ReqDifficultyScale(float scale, unsigned int session_id) {
        if (!SinglePlayer()) return;
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;   // .text:06d45ac6 client_id != 0; retail has no `active` test (w4-server-guards)
        // Retail FUN_06d4ed40 clamps to [0.0, 2.0] and stores at player+0x3C4.
        // The x87 tests are written as negations so an unordered (NaN) input goes
        // the same way as in retail: .text:06d4ed4c TEST AH,0x41 / JNZ takes the
        // 0.0f (.rdata:06d65188) arm for below, equal AND unordered; .text:06d4ed6d
        // TEST AH,0x5 / JP takes the 2.0f (.rdata:06d6b734 = 0x40000000) arm for
        // not-below and unordered.
        if (!(scale > 0.0f)) scale = 0.0f;
        if (!(scale < 2.0f)) scale = 2.0f;
        p->difficulty_scale = scale;
    }
    // Retail 0x06d453d0 is two instructions and ignores `this` entirely:
    //     06d453d0  a16802d906  MOV EAX,[.data:DAT_06d90268]
    //     06d453d5  c3          RET
    // 0x06d90268 is &Players + 0x8, i.e. PlayerDB::m_ServerID. The sibling
    // IServerImpl::GetServerSig at 0x06d453e0 has the same shape:
    //     06d453e0  b97002d906  MOV ECX,.data:DAT_06d90270       ; &Players+0x10
    //     06d453e5  ff257046d6  JMP [COMMON.DLL::CPlayerAccount::GetAccountNameSigChar]
    // An immediate global address, never `LEA ECX,[this + N]` -- which is the
    // proof that retail's IServerImpl embeds no PlayerDB.
unsigned int IServerImpl::GetServerID() { return Players.GetServerID(); }
    // Same story, 0x06d453e0 above: retail loads &Players + 0x10 and tail-jumps
    // into CPlayerAccount::GetAccountNameSigChar. The literal "Freelancer" the
    // port returned here was invented.
const char* IServerImpl::GetServerSig() { return Players.GetServerSig(); }
    // DumpPacketStats — retail 0x123b0: debug function, calls controller vtable+500
void IServerImpl::DumpPacketStats(const char* name) {
        // Retail 0x123b0: if the IClient global is set, forwards to its slot 125 (+0x1f4)
        // -- the one IClient slot whose signature retail spells out, and literally the same
        // code as ?DumpPacketStats@IServerImpl@@UAEXPBD@Z in rpclocal.
        if (g_pClientInterface) g_pClientInterface->DumpPacketStats(name);
    }
    // Slot 91: Dock -- retail .text:06cf15f0 .. .text:06cf177d,
    // ?Dock@IServerImpl@@UAEXABI0@Z, RET 8. Both arguments are references;
    // rpclocal serializes *BaseId only (ch3 op 0x24), so SessionId is the second
    // one (.text:06cf15f6 reads param_2 as the session, .text:06cf1626 param_1 as
    // the base). pub::Player::ForceLand (.text:06d5678f) is the other caller.
    //
    // REWRITTEN 2026-09-24 from the listing. The previous body was a stand-in:
    // it checked session/ship/base guards retail does not have, sent invented
    // controller events 0x39 and 1, and set the slot's base fields itself --
    // retail does none of that here; the base is entered later, by the client's
    // own BaseEnter after FLPACKET_SERVER_LAND.
void IServerImpl::Dock(const unsigned int& base_id, const unsigned int& session_id) {
        // .text:06cf15f6 .. .text:06cf1620 -- FUN_06d006c0 (Server_FindObject) on
        // the slot's ship_object_id (`Players + sid*0x418 - 0x34`, no validation of
        // sid), out-parameters the object (ESP+0x10) and its StarSystem (ESP+0xc);
        // return when it fails. The system is that third out-parameter, used below
        // without a null test (an in-transit ship leaves it 0, as in retail).
        PlayerData& Slot = Players[session_id];
        GameObject* ShipObj = nullptr;
        StarSystem* System = nullptr;
        if (!Server_FindObject(Slot.ship_object_id, ShipObj, System)) return; // .text:06cf1616

        // .text:06cf1626 .. .text:06cf163a -- Universe::get_base(*BaseId).
        Universe::IBase const* const Base = Universe::get_base(base_id);
        if (Base == nullptr) return;

        // .text:06cf1640 .. .text:06cf1656 -- FUN_06d0d370 on that system:
        // (Ship, no target, &*BaseId, &land = 1). The byte is the caller's own
        // param_2 slot, overwritten with 1 at .text:06cf1651.
        bool const bLand = true;
        System->queue_dock(ShipObj, nullptr, &base_id, &bLand);

        // .text:06cf165b .. .text:06cf167c -- when the base is in the system the
        // player is already in, that is all: the next regular system update
        // lands the ship. IBase slot +0x10 is the base's system id.
        if (Base->SystemId == Slot.system_id) return;

        // .text:06cf1682 .. .text:06cf168b -- otherwise run FUN_06d0be50 on the
        // ship's system right now, with 0.2f (PUSH 0x3e4ccccd): the dock queue
        // is processed, the LAND goes out and the player leaves that system.
        StarSystem_Update(System, 0.2f);

        // .text:06cf1690 .. .text:06cf16a5 -- FUN_06d4b450 on the slot.
        PlayerData_FlushPendingVisits(Players[session_id]);

        // .text:06cf16aa .. .text:06cf16cf -- FUN_06d43650(0x3a, {system id
        // (FUN_06d06cd0: SystemInfo vtable slot 0), base's system, sid}).
        unsigned int LeaveEvent[3];
        LeaveEvent[0] = System->SystemInfo->NicknameId;
        LeaveEvent[1] = Base->SystemId;
        LeaveEvent[2] = session_id;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, LeaveEvent);

        // .text:06cf16d4 .. .text:06cf16e6 -- the slot's system_id (+0x3e0) is
        // the base's system from here on.
        Players[session_id].system_id = LeaveEvent[1];

        // .text:06cf16ea .. .text:06cf176f -- FUN_06d43650(0x3b, seven dwords
        // {base's system, the old system id (FUN_06d06cd0 again), sid, 0, 0, 0,
        // 0}), the same event CharacterSelect sends with a zero second dword.
        unsigned int EnterEvent[7];
        EnterEvent[0] = LeaveEvent[1];
        EnterEvent[1] = System->SystemInfo->NicknameId;
        EnterEvent[2] = session_id;
        EnterEvent[3] = 0;
        EnterEvent[4] = 0;
        EnterEvent[5] = 0;
        EnterEvent[6] = 0;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemEnter, EnterEvent);
    }

    // Slot 92 (+0x170, .rdata:06d65d3c): PushToServer — Server.dll's body is the shared
    // RET 4 stub at 0x06ce1050. IServerImpl's OWN virtual, not an IServer override:
    // IServer's retail table (.rdata:06d65d4c) stops at slot 91, so this, the only virtual
    // IServerImpl adds, lands right after it. The packet queue lives on the rpclocal side
    // (IServerImpl::PushToServer 0x06d8f460 appends to the CDAConnection), not here.
void IServerImpl::PushToServer(CDAPacket* /*packet*/) {}

    // ?GetServerStats@IServerImpl@@QAEXAAUServerStats@@@Z is NOT virtual — see
    // GetServerStats below, which is the real (non-virtual) implementation.
    // The old vtable entries "GetServerStats", "GetServerStats2", "TradeAbort" and
    // "UnknownSlot93" were fabrications and are gone.

    // =================================================================
    // IChatServer::SubmitChat -- retail 0x06d191b0 (RVA 0x391B0), `RET 0x14`.
    //
    // The whole of retail's FLChatServer.cpp -- command table, group verbs,
    // "<name>: " header, the private / universe / system / local / group
    // routing -- lives in src/Server/server_chat.cpp. Retail's body is a
    // member only because it is reached through the IChatServer vtable; it
    // touches no member of IServerImpl, so the forward loses nothing.
    // =================================================================
void IServerImpl::SubmitChat(CHAT_ID Sender, unsigned long Size, const void* Data, CHAT_ID Target, int SystemId) {
        Server_SubmitChat(Sender, Size, Data, Target, SystemId);
    }

    // =================================================================
    // GetServerStats(ServerStats&) -- non-virtual export
    // ?GetServerStats@IServerImpl@@QAEXAAUServerStats@@@Z, .text:06d45af0..06d45b36.
    // =================================================================
void IServerImpl::GetServerStats(ServerStats& OutStats) {
        OutStats = GServerStats;                           // MOV ECX,0xb / REP MOVSD
        unsigned int PlayerCount = 0;
        PlayerData* Slot = Players.traverse_active(nullptr);
        while (Slot != nullptr) {                          // 06d45b18..06d45b26
            ++PlayerCount;
            Slot = Players.traverse_active(Slot);
        }
        OutStats.Reserved3 = 0;                            // [EBX+0x28]
        OutStats.Reserved2 = 0;                            // [EBX+0x24]
        OutStats.Reserved1 = 0;                            // [EBX+0x20]
        OutStats.PlayerCount = PlayerCount;                // [EBX]
    }

    // Exported constructors and operator= for Server.dll retail ABI.
    //
    // Retail IServerImpl is TWELVE BYTES and holds no player storage at all.
    // The default ctor, 0x06cef560, is six instructions end to end:
    //     06cef560  8bc1          MOV   EAX,ECX
    //     06cef562  c74004445...  MOV   dword ptr [EAX + 0x4],06d5fff0
    //     06cef569  c74008000...  MOV   dword ptr [EAX + 0x8],0x0
    //     06cef570  c700cc5bd606  MOV   dword ptr [EAX],IServerImpl::`vftable'
    //     06cef576  c74004c45...  MOV   dword ptr [EAX + 0x4],06d191b0
    //     06cef57d  c3            RET
    // -- IServer vptr at +0, IChatServer vptr at +4, one dword at +8. The copy
    // ctor (0x06cef580) and operator= (0x06cef5b0) copy exactly that one dword.
    // The global instance confirms the size: `Server`, ordinal 524, sits at
    // .data:06d8facc and the next symbol is at 06d8fad8, twelve bytes later.
    //
    // The port's `char m_Players[4096]` was therefore an invention twice over:
    // retail has no such member, and 4096 is not derived from anything. It was
    // memset by the ctor and then `reinterpret_cast<PlayerDB*>`-ed and called
    // through, running container code over zeroed std::wstring/std::map state.
    // The real table is the exported global `Players` (see common_server.h).

// Field_0x8 (retail +0x8, the single data member) is declared in server_iserverimpl.h.

// FUN_06d43340 (.text:06d43340 .. 06d43385). Retail keeps the module in
// DAT_06d8f618 even when LoadLibraryA fails, and on success calls BOTH
// GetProcAddress results straight away -- `CALL ESI` / `CALL EAX` at
// .text:06d43361/06d43363 and .text:06d43376/06d43378 -- with no null test on
// either export. The results land in DAT_06d8f620 (IClient) and DAT_06d8f624
// (IChatClient); Startup copies them on through FUN_06d433d0/FUN_06d433c0 into
// the live pair DAT_06d8fb14/DAT_06d8fb18. The function returns DAT_06d8f620
// (.text:06d43380, reached on both paths); Startup ignores it.
static IClient* Server_LoadClientDll(const char* module_path) {
    g_hClientDll = LoadLibraryA(module_path);
    if (g_hClientDll) {
        typedef IClient* (__cdecl *GetClientInterfaceFn)();
        typedef IChatClient* (__cdecl *GetChatClientInterfaceFn)();
        GetClientInterfaceFn pfn = (GetClientInterfaceFn)GetProcAddress(g_hClientDll, "GetClientInterface");
        GLoadedClientInterface = pfn();
        GetChatClientInterfaceFn pfn2 = (GetChatClientInterfaceFn)GetProcAddress(g_hClientDll, "GetChatClientInterface");
        GLoadedChatClientInterface = pfn2();
    }
    return GLoadedClientInterface;
}

// FUN_06d43390 (.text:06d43390 .. 06d433be): `if (DAT_06d8f618) { FreeLibrary;
// DAT_06d8f618 = 0; DAT_06d8f620 = 0; DAT_06d8f624 = 0; }` -- all three stores
// sit inside the null test, and only the LOADED pair is cleared. The live IClient
// copy DAT_06d8fb14 is zeroed by the caller (Shutdown, .text:06d45203); the live
// IChatClient copy DAT_06d8fb18 is never zeroed in retail. Shutdown pushes
// DAT_06d8fb14 before the call (.text:06d451fa) and pops it after (06d45200),
// but the body never reads a stack argument, so the port takes none.
static void Server_FreeClientDll() {
    if (g_hClientDll) {
        FreeLibrary(g_hClientDll);
        g_hClientDll = nullptr;
        GLoadedClientInterface = nullptr;
        GLoadedChatClientInterface = nullptr;
    }
}

// FUN_06d433c0 (.text:06d433c0): MOV EAX,[DAT_06d8f624] / RET. Sole caller
// Startup .text:06d44bf0, which stores the result into DAT_06d8fb18.
static IChatClient* Server_GetLoadedChatClientInterface() {
    return GLoadedChatClientInterface;
}

// FUN_06d433d0 (.text:06d433d0): MOV EAX,[DAT_06d8f620] / RET. Sole caller
// Startup .text:06d44be6, which stores the result into DAT_06d8fb14.
static IClient* Server_GetLoadedClientInterface() {
    return GLoadedClientInterface;
}

// FUN_06d448d0 — Read [Freelancer] initial_world from freelancer.ini
static void Server_ReadWorldIni(const char* config_path) {
    INI_Reader reader;
    if (!reader.open(config_path, false)) return;
    while (reader.read_header()) {
        if (reader.is_header("Freelancer")) {
            while (reader.read_value()) {
                if (reader.is_value("initial_world")) {
                    // .text:06d4497b..06d4498a: get_value_string, then a byte copy
                    // into DAT_06d8f9c8 up to and including the NUL -- no null test
                    // on the returned pointer (the port's guard was removed
                    // 2026-09-24, s2). Retail stops reading once found (BL flag),
                    // which the early return mirrors.
                    const char* val = reader.get_value_string();
                    strncpy_s(g_InitialWorldPath, sizeof(g_InitialWorldPath), val, _TRUNCATE);
                    return;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Server_LoadWorldState -- retail FUN_06d44ca0 (.text:06d44ca0 .. .text:06d44fc2),
// cdecl, no arguments; called only from Startup in multiplayer (.text:06d44c41).
// The MP twin of Server_LoadSinglePlayerState: the same [Time] / feelings / event
// 0x40 sequence, over the initial_world file instead of a character file.
//   06d44cbf .. 06d44cd6  (*DALib::Data)->GetFileSysPath(buf, 0x104)  (IFileSystem +0x84)
//   06d44cdc .. 06d44cef  _makepath(buf, NULL, buf, DAT_06d8f9c8, NULL)
//                         (DAT_06d8f9c8 = initial_world, FUN_06d448d0)
//   06d44cf8 .. 06d44d19  INI_Reader ctor ; open(buf, false) -- result not tested
//   06d44d1f .. 06d44d9e  get_state ; time = 0.0 ; read_header loop until
//                         is_header("Time") -> read_value, time = get_value_float(0)
//                         ; set_state
//   06d44da4 .. 06d44dbc  DAT_06d8fb20 = time ; Delta(06d8f970).start()
//   06d44dc2 .. 06d44dd0  Reputation::FreeFeelings() ; LoadFeelings(reader)
//   06d44dd6 .. 06d44de0  FUN_06d43650(0x40, &reader)
// ---------------------------------------------------------------------------
static void Server_LoadWorldState() {
    // Retail builds the path in place (same buffer as dir and output); the
    // modern CRT's _makepath may pre-fill its output in debug builds, so the
    // dir half gets its own buffer. The resulting string is the same.
    char DataPath[0x104];
    DALib::Data->GetFileSysPath(DataPath, 0x104);
    char WorldPath[0x104];
    _makepath(WorldPath, nullptr, DataPath, g_InitialWorldPath, nullptr);

    INI_Reader Reader;
    Reader.open(WorldPath, false);

    INI_Reader::State Saved;
    Reader.get_state(Saved);
    double Time = 0.0;
    if (Reader.read_header()) {
        do {
            if (Reader.is_header("Time")) {
                Reader.read_value();
                Time = Reader.get_value_float(0);
                break;
            }
        } while (Reader.read_header());
    }
    Reader.set_state(Saved);
    GServerTimeBase = Time;
    g_TimingDelta.start();
    ::Reputation::FreeFeelings();
    ::Reputation::LoadFeelings(Reader);
    Server_ControllerDispatch(0, pub::Controller::EMessage::LoadGame, &Reader);
}

// Retail .data:06d8f650 / .data:06d8f654 -- a TString<0x104> {length, chars}: the
// `path` value of the [Initial SP/MP DLLs] section. The static initializer
// 0x06d43410 seeds it empty (XOR EAX,EAX / MOV [06d8f650],EAX / MOV [06d8f654],AL);
// FUN_06d43840 writes it; pub::DLL_LoadLibrary (0x06d43c40, server_pub.cpp) reads
// the chars as the directory for every relative controller DLL name, so it keeps
// its value for the rest of the session, not just for the ini walk.
unsigned int GDllPathLength = 0;
char GDllPath[0x104] = {0};

// FUN_06d43840 (.text:06d43840 .. .text:06d43bf7), cdecl (const char* IniPath).
// Startup passes the same ini path it gave DataConfig::startup and FUN_06d448d0
// (.text:06d44c26 / .text:06d44c2c, both PUSH EDI = param_1+0x104).
//   06d43888  INI_Reader::open(IniPath, false); read_header; on failure skip to cleanup
//   06d438b0  per header: RefreshLoadingProgress()
//             (is_header("Initial SP DLLs") && SinglePlayer()) ||
//             (is_header("Initial MP DLLs") && !SinglePlayer()), then the read_value loop
//   06d43917  "path": get_value_string(); null -> {0, ""} (06d4393a), else
//             len = FUN_06d03cf0(GDllPath, 0x104, s) (06d43956)
//   06d43968  "DLL":  get_value_string(0/1/2) each copied the same way into a
//             TString<0x104> name, TString<0x104> interface, TString<0x20> priority
//   06d43a33  the priority word is looked up (_stricmp) in the 5-row table at
//             .data:06d8d080 (0x14-byte rows: char[16] name, int value):
//             HIGHEST 0, ABOVE_NORMAL 1, NORMAL 2, BELOW_NORMAL 3, LOWEST 4
//   06d43a83  pub::Controller::Create(name, interface, NULL, priority)
// No match leaves the priority slot ([ESP+0x10]) holding whatever it held before:
// the previous row's priority, or uninitialised stack for the first row. The port
// keeps one function-scope variable for the same carry-over and starts it at 0
// (HIGHEST) where retail's first value is indeterminate.
static void Server_LoadInitialDlls(const char* IniPath) {
    struct FPriorityRow { const char* Name; unsigned int Value; };
    static const FPriorityRow PriorityTable[5] = {
        { "HIGHEST", 0 }, { "ABOVE_NORMAL", 1 }, { "NORMAL", 2 },
        { "BELOW_NORMAL", 3 }, { "LOWEST", 4 },
    };
    // FUN_06d03cf0: Src null -> 0 and Dst untouched; else copy min(strlen, Size-1)
    // chars, terminate, return the copied length. The null arm is inlined by every
    // caller here as {len = 0, chars[0] = 0}.
    struct FBounded {
        static unsigned int Copy(char* Dst, unsigned int Size, const char* Src) {
            unsigned int Len = static_cast<unsigned int>(strlen(Src));
            if (!(Len < Size)) Len = Size - 1;
            memcpy(Dst, Src, Len);
            Dst[Len] = '\0';
            return Len;
        }
    };

    INI_Reader Reader;
    unsigned int Priority = 0;
    if (Reader.open(IniPath, false) && Reader.read_header()) {
        do {
            RefreshLoadingProgress();
            if (((Reader.is_header("Initial SP DLLs") && SinglePlayer()) ||
                 (Reader.is_header("Initial MP DLLs") && !SinglePlayer())) &&
                Reader.read_value()) {
                do {
                    if (Reader.is_value("path")) {
                        const char* PathValue = Reader.get_value_string();
                        if (PathValue == nullptr) {
                            GDllPathLength = 0;
                            GDllPath[0] = '\0';
                        } else {
                            GDllPathLength = FBounded::Copy(GDllPath, sizeof(GDllPath), PathValue);
                        }
                    } else if (Reader.is_value("DLL")) {
                        char DllName[0x104];
                        char InterfaceName[0x104];
                        char PriorityName[0x20];
                        const char* Value = Reader.get_value_string(0);
                        DllName[0] = '\0';
                        if (Value != nullptr) FBounded::Copy(DllName, sizeof(DllName), Value);
                        Value = Reader.get_value_string(1);
                        InterfaceName[0] = '\0';
                        if (Value != nullptr) FBounded::Copy(InterfaceName, sizeof(InterfaceName), Value);
                        Value = Reader.get_value_string(2);
                        PriorityName[0] = '\0';
                        if (Value != nullptr) FBounded::Copy(PriorityName, sizeof(PriorityName), Value);
                        for (unsigned int Row = 0; Row < 5; ++Row) {
                            if (_stricmp(PriorityTable[Row].Name, PriorityName) == 0) {
                                Priority = PriorityTable[Row].Value;
                                break;
                            }
                        }
                        Server_ControllerCreate(DllName, InterfaceName, nullptr, Priority);
                    }
                } while (Reader.read_value());
            }
        } while (Reader.read_header());
    }
}

// ===========================================================================
// Controller / Timer / Physics Infrastructure
// Matches retail Server.dll internal data structures
// ===========================================================================

// Controller node — 32 bytes (0x20), matches retail layout
struct FControllerNode {
    FControllerNode* Prev;        // +0x00
    FControllerNode* Next;        // +0x04
    IDLL* InterfacePtr;           // +0x08  CreateInstance result (Content.dll's IDLL)
    HMODULE ModuleHandle;         // +0x0C
    float HeartbeatAccum;         // +0x10  accumulator (stored as float bits in int)
    float HeartbeatTimer;         // +0x14  period (stored as float bits in int)
    unsigned int FLID;            // +0x18  unique controller ID
    unsigned int Priority;        // +0x1C  sort order
};

// Timer node for FUN_06d5df60 timer firing system
struct FTimerNode {
    FTimerNode* Prev;             // +0x00
    FTimerNode* Next;             // +0x04
    unsigned int TargetController; // +0x08  target controller FLID
    // +0x0c/+0x10 are pub::Controller::TimerExpired's two dwords, copied verbatim by
    // pub::SetTimer (retail 0x06d5ee78..0x06d5ee84) and handed back to the controller
    // as the message payload when the timer fires (retail 0x06d5e00a-0x06d5e014).
    unsigned int Param1;          // +0x0C
    unsigned int Param2;          // +0x10
    float RemainingTime;          // +0x14  countdown
};

// Global controller lists (match retail addresses)
static FControllerNode g_ControllerListSentinel = { &g_ControllerListSentinel, &g_ControllerListSentinel, nullptr, nullptr, 0, 0, 0, 0 };
static FControllerNode g_DeferredListSentinel = { &g_DeferredListSentinel, &g_DeferredListSentinel, nullptr, nullptr, 0, 0, 0, 0 };
// Retail DAT_06d8f628/62c/630: a std::list of POINTERS to active-list nodes (0xc-byte
// list nodes, ctor 0x06d43560 / dtor 0x06d435a0). pub::Controller::Destroy only queues
// here; the heartbeat walk (FUN_06d43680) does the real teardown next tick.
static std::list<FControllerNode*> g_DestroyPending;
static unsigned int g_ControllerCount = 0;       // DAT_06d8f648
static unsigned int g_DeferredCount = 0;         // DAT_06d8f63c
static unsigned int g_ReentrancyGuard = 0;       // DAT_06d8f764

// Controller DLL entry points as the retail call sites use them:
// CreateInstance(Flid, Name, Parms) at .text:06d43dd4 (three pushes, cdecl) and
// DestroyInstance(Interface) at .text:06d436b4 / 06d43df1 / 06d43f21 (one push).
// Retail resolves both with GetProcAddress, never through the import table; the only
// controller DLL [Initial SP/MP DLLs] and Content's own Controller::Create calls name is
// Content.dll, which exports both plus IDLL (Content.def @1..@7). The pointer types are
// therefore taken from content.h's declarations rather than restated here.
typedef decltype(&::CreateInstance) FControllerCreateInstanceFn;
typedef decltype(&::DestroyInstance) FControllerDestroyInstanceFn;

// Timer list for FUN_06d5df60
static FTimerNode g_TimerListSentinel = { &g_TimerListSentinel, &g_TimerListSentinel, 0, 0, 0, 0.0f };
static unsigned int g_TimerCount = 0;            // DAT_06d9046c

// Retail 0x06d5df00 -- the timer list's destructor, registered through _onexit by the
// list's static initializer 0x06d5dec0. Walks from the first node, and for each one
// unlinks it, frees it (0x06d5df24) and decrements DAT_06d9046c; then frees the head node
// and zeroes the head pointer and the count (0x06d5df43..0x06d5df53). The port's head is
// the static sentinel above rather than a 0x18-byte heap node, so instead of being freed
// and nulled it is relinked to itself; nodes are freed with the allocator
// Server_ControllerSetTimer uses.
struct FTimerListTeardown {
    ~FTimerListTeardown() {
        FTimerNode* Cur = g_TimerListSentinel.Next;
        while (Cur != &g_TimerListSentinel) {
            FTimerNode* Node = Cur;
            Cur = Cur->Next;
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            free(Node);
            g_TimerCount--;
        }
        g_TimerListSentinel.Prev = &g_TimerListSentinel;
        g_TimerListSentinel.Next = &g_TimerListSentinel;
        g_TimerCount = 0;
    }
};
static FTimerListTeardown g_TimerListTeardown;


// Linked list helpers
static void ListInsertBefore(FControllerNode* sentinel, FControllerNode* node) {
    node->Next = sentinel;
    node->Prev = sentinel->Prev;
    sentinel->Prev->Next = node;
    sentinel->Prev = node;
}

static void ListRemove(FControllerNode* node) {
    node->Prev->Next = node->Next;
    node->Next->Prev = node->Prev;
    node->Prev = node;
    node->Next = node;
}

static void TimerListInsertBefore(FTimerNode* sentinel, FTimerNode* node) {
    node->Next = sentinel;
    node->Prev = sentinel->Prev;
    sentinel->Prev->Next = node;
    sentinel->Prev = node;
}

static void TimerListRemove(FTimerNode* node) {
    node->Prev->Next = node->Next;
    node->Next->Prev = node->Prev;
    node->Prev = node;
    node->Next = node;
}

// ---------------------------------------------------------------------------
// pub::Controller::_SendMessage's dispatch core — retail 0x06d44080.
//
// This is what makes every `Server_SendMessage(id, data)` call in Server.dll mean
// something. Retail's FUN_06d43650 (which the port exported from Common.dll as
// Server_SendMessage) is only a two-line forwarder:
//     FUN_06d43650(id, data) { uint zero = 0; pub::Controller::_SendMessage(zero, id, data); }
// and _SendMessage itself (0x06d44080) does:
//     if ((Flid & 0x3fffffff) == 0)   // broadcast
//         for (node : controller list)  node->InterfacePtr->vtable[0](MsgId, MsgData);
//     else { node = lookup(Flid); if (!node) return -2;
//            node->InterfacePtr->vtable[0](MsgId, MsgData); }
// The broadcast arm bumps the re-entrancy guard (DAT_06d8f764) around the walk and,
// when it drops back to zero, drains the deferred-create queue — exactly what
// Server_ControllerHeartbeat already does here.
//
// The record's interface is what the controller DLL's CreateInstance returned:
// Content.dll's IDLL (src/include/content.h; Content exports ??_7IDLL@@6B@). Slot 0,
// IDLL::HandleMessage (thiscall, int-sized pub::Controller::EMessage MsgId, const void*
// MsgData, RET 8), is the only
// entry point Server.dll calls on it; teardown goes through DestroyInstance, never
// through slot 1 (the destructor).

static void Server_ControllerDrainDeferred();

// FUN_06d43600 (.text:06d43600 .. .text:06d4364b), cdecl (const uint* Flid): walk the
// ACTIVE list (DAT_06d8f644) for a node whose +0x18 equals *Flid and return &node+8
// (the record); if the active list has none, walk the DEFERRED-create list
// (DAT_06d8f638) the same way; NULL when neither has it. A controller created while
// a broadcast is in flight is therefore already addressable before the drain moves
// it over. Callers: _SendMessage's targeted arm (.text:06d44149),
// SetHeartbeatInterval (.text:06d4403c) and FUN_06d43820 (.text:06d43825).
// The port's node pointer stands in for retail's &node+8.
static FControllerNode* Server_ControllerFind(unsigned int Flid) {
    FControllerNode* Node = g_ControllerListSentinel.Next;
    while (Node != &g_ControllerListSentinel) {
        if (Node->FLID == Flid) return Node;
        Node = Node->Next;
    }
    Node = g_DeferredListSentinel.Next;
    while (Node != &g_DeferredListSentinel) {
        if (Node->FLID == Flid) return Node;
        Node = Node->Next;
    }
    return nullptr;
}

// FUN_06d43820 (.text:06d43820 .. .text:06d43833), cdecl (const uint* Flid):
// `return FUN_06d43600(Flid) != NULL;` -- is this Flid a live or pending controller.
// Sole caller: pub::SpaceObj::Create (.text:06cf7f69), passing &ShipInfo, whose
// first dword is therefore the controller Flid.
extern "C" bool __cdecl Server_ControllerExists(const unsigned int& Flid) {
    return Server_ControllerFind(Flid) != nullptr;
}

// Retail 0x06d44080: RefreshLoadingProgress() first (.text:06d44086), on both arms.
// Neither arm tests the record's interface pointer before `CALL [EDX]`
// (.text:06d440b8..06d440bf broadcast, .text:06d44155..06d44163 targeted).
extern "C" int __cdecl Server_ControllerDispatch(unsigned int Flid, pub::Controller::EMessage MsgId,
                                                 const void* MsgData) {
    RefreshLoadingProgress();
    if ((Flid & 0x3fffffff) == 0) {
        // Broadcast arm (retail 0x06d4409c-0x06d440cd).
        g_ReentrancyGuard++;
        FControllerNode* cur = g_ControllerListSentinel.Next;
        while (cur != &g_ControllerListSentinel) {
            cur->InterfacePtr->HandleMessage(MsgId, MsgData);
            cur = cur->Next;
        }
        g_ReentrancyGuard--;
        if (g_ReentrancyGuard == 0)
            Server_ControllerDrainDeferred();
        return 0;
    }
    // Targeted arm (retail 0x06d44148-0x06d4416c).
    FControllerNode* node = Server_ControllerFind(Flid);
    if (node == nullptr) return -2;
    node->InterfacePtr->HandleMessage(MsgId, MsgData);
    return 0;
}

// pub::Controller::SetHeartbeatInterval's body — retail 0x06d44030. FUN_06d43600(&Flid)
// finds the node (payload pointer = node+8) and the whole body is then
//     payload[+0x8] = Interval;                       // node+0x10 -- the period/gate
//     payload[+0xc] = rand() * 0x38000000f * Interval; // node+0x14 -- initial accumulator
//     return 0;                                        // -2 when not found
// 0x38000000f is 1/32768; the product is a random fraction of one period, so freshly
// configured controllers do not all fire on the same frame. The heartbeat walk
// (FUN_06d43680) gates on node+0x10 >= 0.0 (FLD [ESI+0x10] / FCOMP 0.0 at 0x06d43735).
// NOTE: this port's FControllerNode names the two the other way round -- HeartbeatTimer
// (+0x14) is what Server_ControllerHeartbeat uses as the period/gate and HeartbeatAccum
// (+0x10) as the accumulator. The node is private to Server.dll, so the swap is
// harmless, but the assignment below has to follow the PORT's convention, not retail's
// offsets, or the existing tick would never fire.
extern "C" int __cdecl Server_ControllerSetHeartbeat(unsigned int Flid, float Interval) {
    FControllerNode* node = Server_ControllerFind(Flid);   // .text:06d4403c FUN_06d43600
    if (node == nullptr) return -2;
    node->HeartbeatTimer = Interval;
    node->HeartbeatAccum = (float)rand() * (1.0f / 32768.0f) * Interval;
    return 0;
}

// pub::SetTimer's body — retail 0x06d5ee00. Allocates a 0x18-byte node, links it at the
// END of the timer list (list::push_back: node->next = head, node->prev = head->prev,
// 0x06d5ee59..0x06d5ee67) and bumps the count. Retail checks ONLY Timeout < 0 (-4); it
// does not validate the Flid, and it does not check the allocation either (operator new
// at 0x06d5ee4f is followed straight by the store [EAX],EBX at 0x06d5ee59).
extern "C" int __cdecl Server_ControllerSetTimer(unsigned int Flid, unsigned int Param1,
                                                 unsigned int Param2, float Timeout) {
    if (Timeout < 0.0f) return -4;
    FTimerNode* node = (FTimerNode*)malloc(sizeof(FTimerNode));
    node->TargetController = Flid;
    node->Param1 = Param1;
    node->Param2 = Param2;
    node->RemainingTime = Timeout;
    TimerListInsertBefore(&g_TimerListSentinel, node);
    g_TimerCount++;
    return 0;
}

// pub::Controller::Create -- retail 0x06d43ce0 (ordinal 85), cdecl
// (DllPath, InstanceName, Parms, Priority).
//   06d43cea  Flid = CFLIDMaker::GetInstance()->next id (inlined: [EAX+4] & 0x3fffffff,
//             [EAX+4]++); Flid == 0 -> return 0
//   record    {Interface, Module, +0x10 = -1.0f, +0x14 = 0, Flid, Priority}
//   06d43d3f / 06d43e41  Module = pub::DLL_LoadLibrary(DllPath); NULL -> FDUMP
//             0x100001 "failed create DLL instance" (dlls.cpp, 0xa7) and carry on
//   guard DAT_06d8f764 != 0: append the record to the DEFERRED list (06d43d85);
//   guard == 0: insert it into the ACTIVE list before the first node whose priority
//             is >= Priority (signed JGE, 06d43e89) (06d43eb7)
//   then RefreshLoadingProgress, GetModuleFileNameA (result unused),
//   Interface = GetProcAddress(Module,"CreateInstance")(Flid, Name, Parms);
//   non-NULL -> return Flid.  NULL -> GetProcAddress(Module,"DestroyInstance")(Interface),
//   GetModuleFileNameA, FreeLibrary(Module), erase the record from the list it went
//   into (06d43e24 / 06d43f4f), return 0.
// Neither GetProcAddress result is tested before the call.
unsigned int Server_ControllerCreate(const char* dll_path, const char* instance_name,
                                             void* parms, unsigned int priority) {
    unsigned int flid = CFLIDMaker::GetInstance()->AllocateObjectID();
    if (flid == 0) return 0;

    HMODULE hMod = pub::DLL_LoadLibrary(dll_path);
    if (hMod == nullptr) {
        FDUMP(0x100001, "%s(%d) : *** ERROR: failed create DLL instance '%s' (make sure file exists)\n",
              "E:\\FL\\Scratch\\Source\\Server\\dlls.cpp", 0xa7, dll_path);
    }

    FControllerNode* node = (FControllerNode*)malloc(sizeof(FControllerNode));
    node->InterfacePtr = nullptr;
    node->ModuleHandle = hMod;
    node->HeartbeatAccum = 0.0f;
    node->HeartbeatTimer = -1.0f;
    node->FLID = flid;
    node->Priority = priority;

    const bool bDeferred = (g_ReentrancyGuard != 0);
    if (bDeferred) {
        ListInsertBefore(&g_DeferredListSentinel, node);
        g_DeferredCount++;
    } else {
        FControllerNode* cur = g_ControllerListSentinel.Next;
        while (cur != &g_ControllerListSentinel) {
            if (static_cast<int>(cur->Priority) >= static_cast<int>(priority)) break;
            cur = cur->Next;
        }
        ListInsertBefore(cur, node);
        g_ControllerCount++;
    }

    RefreshLoadingProgress();
    char ModuleName[0x104];
    GetModuleFileNameA(node->ModuleHandle, ModuleName, 0x104);
    FControllerCreateInstanceFn CreateInstance =
        (FControllerCreateInstanceFn)GetProcAddress(node->ModuleHandle, "CreateInstance");
    node->InterfacePtr = CreateInstance(flid, instance_name, parms);
    if (node->InterfacePtr != nullptr) return flid;

    FControllerDestroyInstanceFn DestroyInstance =
        (FControllerDestroyInstanceFn)GetProcAddress(node->ModuleHandle, "DestroyInstance");
    DestroyInstance(node->InterfacePtr);
    GetModuleFileNameA(node->ModuleHandle, ModuleName, 0x104);
    FreeLibrary(node->ModuleHandle);
    ListRemove(node);
    if (bDeferred) g_DeferredCount--;
    else g_ControllerCount--;
    free(node);
    return 0;
}

// pub::Controller::Destroy -- retail 0x06d43f70 (ordinal 103), cdecl (Flid).
//   06d43f70  if the Flid is already queued (DAT_06d8f62c walk, entry->node+0x18), return
//   06d43f9a  walk the ACTIVE list; for every node with that Flid: append the node
//             pointer to the destroy queue (FUN_06cfc340 / FUN_06d06720), bump
//             DAT_06d8f630, then _SendMessage(0 = broadcast, 0x0c, &Flid copy)
// Nothing is unlinked or unloaded here -- that happens at the top of the next
// heartbeat walk (FUN_06d43680), so a broadcast in progress never sees a freed node.
// The deferred-create list is not searched.
void Server_ControllerDestroy(unsigned int flid) {
    for (std::list<FControllerNode*>::iterator It = g_DestroyPending.begin();
         It != g_DestroyPending.end(); ++It) {
        if ((*It)->FLID == flid) return;
    }
    FControllerNode* cur = g_ControllerListSentinel.Next;
    while (cur != &g_ControllerListSentinel) {
        if (cur->FLID == flid) {
            g_DestroyPending.push_back(cur);
            unsigned int DestroyedFlid = cur->FLID;
            Server_ControllerDispatch(0, pub::Controller::EMessage::ControllerDestroyed, &DestroyedFlid);
        }
        cur = cur->Next;
    }
}

// SysDB — per-system state storage
// Retail: std::map<unsigned int, StarSystem> SysDB::SysMap (red-black tree)
// FUN_06d0fb40 iterates Universe systems, constructs FStarSystem for each
// FUN_06d00280 looks up by system_id via map::lower_bound
// The table itself is the exported SysDB::SysMap (server_sysdb.h / server_classes.cpp).
static_assert(sizeof(FStarSystem) == 0x100, "FStarSystem must stay 0x100 — SysDB memset/map");
// No "already initialised" latch: retail FUN_06d0fb40 has none, and its only
// caller FUN_06cfffd0 (Server_PlayerSubsystemInit) runs once per server startup.

// FUN_06d00280 — lookup system in SysMap by ID
FStarSystem* SysDB_LookupSystem(unsigned int system_id) {
    if ((system_id & 0x3fffffff) == 0) return nullptr;
    SysDB::SysMapType::iterator it = SysDB::SysMap.find(system_id);
    if (it != SysDB::SysMap.end()) return &it->second;
    return nullptr;
}

// FUN_06d0fb40 (.text:06d0fb40) -- SysDB initialization. For every Universe
// system (Universe::GetFirstSystem / GetNextSystem):
//   id = sys->vtable[0]()                         (NicknameId in this port)
//   StarSystem Temp;                              FUN_06d06ce0, .text:06d0fb8e
//   pair<unsigned const, StarSystem> P(id, Temp); FUN_06d10520, .text:06d0fba9
//   It = SysMap.insert(P).first;                  FUN_06d10780 (node: FUN_06d11330)
//   ~P, ~Temp
//   It->second.init(sys->vtable[0]())             FUN_06d06ea0, .text:06d0fe0a
// The map holds the COPY, so every entry's ObjectLists have bUseHeap = 0
// (server_sysdb.h, ObjectList).
void SysDB_Initialize() {

    Universe::ISystem* sys = Universe::GetFirstSystem();
    while (sys != nullptr) {
        unsigned int const SysId = sys->NicknameId;
        StarSystem Temp;
        std::pair<SysDB::SysMapType::iterator, bool> const Inserted =
            SysDB::SysMap.insert(SysDB::SysMapType::value_type(SysId, Temp));
        Inserted.first->second.init(sys->NicknameId);

        sys = Universe::GetNextSystem();
    }
}

// FUN_06d10200 (.text:06d10200 .. 06d10318) -- SysDB teardown, called only by
// FUN_06cffff0 (.text:06cffff5, this port's Server_CleanupPhysics). (FUN_06d0fa60,
// the name this used to carry, is SysMap's atexit destructor, not this.)
//   06d10210  in-order walk of SysMap: every StarSystem whose +0xfc (bLoaded) is set
//             gets StarSystem::shutdown (FUN_06d07130, CALL at .text:06d1021e)
//   06d10263  then SysMap.clear() -- ~StarSystem (FUN_06d10030) per node.
// ~StarSystem is traced against FUN_06d10030 (server_classes.cpp).
// 06d10265..06d10318 is std::map::erase(begin(), end()) inlined: the whole-tree
// fast path frees every node through FUN_06d10f60 / FUN_06d11570 (the pair's
// scalar deleting dtor, flags 0 -> ~pair -> ~StarSystem) and resets the head;
// the other path erases node by node (FUN_06d10b10). SysMap.clear() is that.
// No logging in retail (the port's OutputDebugString was removed 2026-09-24).
void SysDB_Shutdown() {
    for (SysDB::SysMapType::iterator It = SysDB::SysMap.begin();
         It != SysDB::SysMap.end(); ++It) {
        if (It->second.bLoaded != 0)
            It->second.shutdown();
    }
    SysDB::SysMap.clear();
}

// Mark system as loaded/active
void SysDB_SetSystemLoaded(unsigned int system_id, bool loaded) {
    FStarSystem* sys = SysDB_LookupSystem(system_id);
    if (sys) sys->bLoaded = loaded ? 1 : 0;
}

// DAT_06d88a80 -- see server_sysdb.h. Initial value 1 (.data:06d88a80).
unsigned char GbAllowPvp = 1;

// FUN_06d006c0's one-entry lookup cache -- see server_sysdb.h.
// LAB_06cfff90 / FUN_06cfffb0 are this watcher's CRT constructor and atexit
// destructor: FUN_06d00d50(0) zeroes both dwords (= BaseWatcher()), and
// FUN_06cfffc0 is `if (Watched) set_pointer(0)` (= ~BaseWatcher, whose
// set(nullptr) is a no-op while nothing is watched).
BaseWatcher GLookupCacheObject;           // DAT_06d8d8ec
StarSystem* GLookupCacheSystem = nullptr; // DAT_06d8d92c
unsigned int GLookupCacheHits = 0;        // DAT_06d8d930
unsigned int GLookupCacheMisses = 0;      // DAT_06d8d934

// FUN_06cfffd0 (.text:06cfffd0, cdecl, called only from IServerImpl::Startup at
// .text:06d44c66 with (info +0x208, (byte)info +0x210)):
//   .text:06cfffd9  DAT_06d88a80 = (byte)param_2
//   .text:06cfffde  CALL FUN_06d0fb40(param_1)  -- SysDB init; the pushed
//                   argument is never read by the callee (SysDB_Initialize)
//   .text:06cfffe6  JMP FUN_06cfd4b0            -- Server fuse DB load
static void Server_PlayerSubsystemInit(unsigned int max_players, unsigned int allow_pvp) {
    (void)max_players;
    GbAllowPvp = static_cast<unsigned char>(allow_pvp);
    SysDB_Initialize();
    ServerFuseDB::LoadAll();
}

// FUN_06cffff0 (.text:06cffff0, called only by IServerImpl::Shutdown). Retail:
//   .text:06cffff0  CALL FUN_06cfd3a0   -- Server fuse DB teardown
//   .text:06cffff5  CALL FUN_06d10200   -- SysDB teardown (SysDB_Shutdown)
//   .text:06cffffa  DAT_06d8d92c = 0    -- the StarSystem half of FUN_06d006c0's
//                   one-entry object-lookup cache (GLookupCacheSystem; the
//                   watcher half, DAT_06d8d8ec, is left as it is)
// Corrected 2026-09-24: the port also cleared two flags retail does not touch
// here (a vestigial init flag and DAT_06d8d928, the phantom-update latch); both
// were otherwise unused and are gone. The OutputDebugString the port used to
// emit here has no retail counterpart and is gone too.
static void Server_CleanupPhysics() {
    ServerFuseDB::Clear();
    SysDB_Shutdown();
    GLookupCacheSystem = nullptr;
}

// FUN_06d0be50's payload for controller event 0x27, built on the stack at
// .text:06d0c29e .. .text:06d0c2b7 (land) and .text:06d0c31f .. .text:06d0c332
// (dock without landing): the object's get_id(), the entry's base id
// (`[EBX+0x18]`, node+0x18 == FSystemDockEntry::BaseId), the target id computed
// at .text:06d0c245 (or 0) and one byte -- 1 for land, 0 otherwise. The three
// bytes after the flag are never written.
struct FDockEventPayload {
    unsigned int ObjectId;    // +0x00
    unsigned int BaseId;      // +0x04
    unsigned int TargetId;    // +0x08
    bool bLand;               // +0x0C
};

// FUN_06d0be50's tail, .text:06d0c4f1 .. .text:06d0c6b0 (after the per-player
// view update): consume Common's changed-affiliation map. Retail:
//   Map = Reputation::GetChangedAffiliationClientMap();   // null while empty
//   if (!Map) return;
//   for (Client, OldAffiliation) in *Map:       // OldAffiliation is never read
//     Ship = Players[Client].ship_object_id               (.text:06d0c523, slot+0x3e4)
//     Aff = 0xffffffff;
//     Reputation::Vibe::GetAffiliation(Players[Client].rep_vibe, Aff, false)
//                                                          (.text:06d0c550, slot+0x318)
//     if (Ship != 0):
//       Obj = FUN_06d006c0(&Ship) ? object : 0             (.text:06d0c570 .. 06d0c57c)
//       for each player view of THIS system:              (.text:06d0c584 .. 06d0c5c2)
//         if (FUN_06cf73d0(view, Obj))                     // SystemPlayerView_IsObjectVisible
//           IClient +0x1d0 (SendServerMiscObjUpdateAffiliation)(view->PlayerId, Ship, Aff)
//     else:
//       IClient +0x1d0 (SendServerMiscObjUpdateAffiliation)(Client, 0, Aff)
//                                                          (.text:06d0c5ca .. 06d0c5da)
//   Map->erase(begin, end)                                 (.text:06d0c62a .. 06d0c68a)
// The map is Common's single global, so the first system updated in a frame
// consumes it and the others see null, exactly as in retail.
static void StarSystem_SendChangedAffiliations(StarSystem* Sys)
{
    std::map<unsigned int, unsigned int>* const Changed =
        ::Reputation::GetChangedAffiliationClientMap();
    if (Changed == nullptr)
        return;

    std::map<unsigned int, unsigned int>::const_iterator It = Changed->begin();
    for (; It != Changed->end(); ++It)
    {
        unsigned int const Client = It->first;
        PlayerData& Slot = Players[Client];
        unsigned int const Ship = Slot.ship_object_id;
        int const Vibe = Slot.rep_vibe;
        unsigned int Affiliation = 0xffffffffu;
        ::Reputation::Vibe::GetAffiliation(Vibe, Affiliation, false);

        if (Ship != 0)
        {
            GameObject* const Obj = Server_LookupGameObject(Ship);
            SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(Sys->PlayerListHead);
            for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
            {
                if (SystemPlayerView_IsObjectVisible(&Node->View, Obj))
                    g_pClientInterface->SendServerMiscObjUpdateAffiliation(Node->View.PlayerId, Ship, Affiliation);
            }
        }
        else
        {
            g_pClientInterface->SendServerMiscObjUpdateAffiliation(Client, 0, Affiliation);
        }
    }

    Changed->clear();
}

// FUN_06d0be50's ship-proximity pass, .text:06d0bf02 .. .text:06d0c0f4, run
// once per step between the +0x70 tick and the +0x48 tick:
//   06d0bf02  Bucket = this->vtable[3](0xfdff)       -- StarSystem::GetBucket
//   06d0bf10  null -> skip the whole pass (.text:06d0c0f6)
//   06d0bf1d  a stack scan list {Capacity 0x800 at +0x94, Count at +0x98,
//             Items[0x800] at +0x9c}
//   06d0bf2f .. 06d0bfca  for every player view (the end test re-reads
//             [this+0xc] each pass):
//     ship = Players[view.PlayerId].ship_object_id    (node+0x40, slot+0x3e4)
//     FUN_06d006c0(&ship, &obj, &x); a failed lookup or a null obj -> next
//     sphere = {obj->vtable[0]() (get_position), DAT_06d88af0 = 10000.0f}
//     Bucket->vtable[+0x1c](&sphere, &list)          -- the BSP fill, thiscall
//   06d0bfd7 .. 06d0c018  then every non-null Data of the block at +0xd4
//             (head [this+0xd8]) is appended while Count < Capacity
//   06d0c01a .. 06d0c09c  Count > 1 (unsigned): de-duplicate in place -- for
//             each non-null item, the FIRST later duplicate is zeroed, and the
//             item is compacted down; zeros are dropped; Count = kept
//   06d0c0a3 .. 06d0c0f4  for each kept item: __RTDynamicCast(item, IObjRW ->
//             GameObject); non-null -> vtable +0x16c (update) with the step;
//             the result is ignored
// FUN_06d006c0 is Server_LookupGameObject in this port (the same stand-in
// server_observer.cpp uses for the same slot read).
static void StarSystem_UpdateNearShips(StarSystem* Sys, float Step)
{
    void* const Bucket = Sys->GetBucket(0xfdffu);
    if (Bucket == nullptr)
        return;

    struct FScanList {
        unsigned int Capacity;          // ESP+0x94
        unsigned int Count;             // ESP+0x98
        void* Items[0x800];             // ESP+0x9c
    };
    struct FScanSphere {
        Vector Center;                  // ESP+0x68
        float Radius;                   // ESP+0x74
    };
    FScanList Scan;                     // retail's own 0x20a0-byte frame holds it
    Scan.Capacity = 0x800;
    Scan.Count = 0;

    // ISpatialPartition +0x1c (sphere query); FScanSphere / FScanList have the layouts of
    // Geometry::Sphere and FSpatialQueryResult.
    ISpatialPartition* const Partition = static_cast<ISpatialPartition*>(Bucket);

    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(Sys->PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(Sys->PlayerListHead);
         Node = Node->Next)
    {
        unsigned int const ShipId = Players[Node->View.PlayerId].ship_object_id;
        GameObject* const Ship = Server_LookupGameObject(ShipId);
        if (Ship == nullptr)
            continue;
        FScanSphere Sphere;
        Sphere.Radius = 10000.0f;       // DAT_06d88af0 = 0x461c4000
        Sphere.Center = Ship->get_position();
        Partition->QuerySphere(reinterpret_cast<Geometry::Sphere const*>(&Sphere),
                               reinterpret_cast<FSpatialQueryResult*>(&Scan));
    }

    FSystemListNode* It = nullptr;
    while (Sys->ListD4.traverse(It, true))
    {
        if (It->Data == nullptr)
            continue;
        if (Scan.Count < Scan.Capacity)
        {
            Scan.Items[Scan.Count] = static_cast<IObjRW*>(It->Data);
            ++Scan.Count;
        }
    }

    if (Scan.Count > 1)
    {
        int const Count = static_cast<int>(Scan.Count);
        int Kept = 0;
        for (int Index = 0; Index < Count; ++Index)
        {
            void* const Item = Scan.Items[Index];
            if (Item == nullptr)
                continue;
            for (int Later = Index + 1; Later < Count; ++Later)
            {
                if (Scan.Items[Later] == Item)
                {
                    Scan.Items[Later] = nullptr;
                    break;
                }
            }
            Scan.Items[Kept] = Scan.Items[Index];
            ++Kept;
        }
        Scan.Count = static_cast<unsigned int>(Kept);
    }

    for (unsigned int Index = 0; Index < Scan.Count; ++Index)
    {
        GameObject* const Obj =
            dynamic_cast<GameObject*>(static_cast<IObjRW*>(Scan.Items[Index]));
        if (Obj != nullptr)
            Obj->update(Step);
    }
}

// ---------------------------------------------------------------------------
// FUN_06ce3360 (.text:06ce3360, thiscall on the StarSystem, `RET 0x10`; the
// name is the port's). One side of a PhySys collision -> the GameObject wrapper
// and the sub-object it was hit on. Only caller: StarSystem_ProcessCollisions
// below (.text:06ce356c side A, .text:06ce3591 side B).
// ---------------------------------------------------------------------------
static void StarSystem_ResolveCollisionSide(StarSystem* Sys, CObject* Part, unsigned int PartId,
                                            GameObject** OutObj, unsigned short* OutSubObjId)
{
    *OutObj = nullptr;                                              // .text:06ce3366
    *OutSubObjId = SUBOBJ_ID_NONE;                                  // .text:06ce3373 .. 06ce3382

    // .text:06ce3387 .. 06ce33e5 -- the wrapper of the part's root object.
    GameObject* Found = nullptr;
    CObject* const Root = GetRoot(Part);
    if (Root != nullptr)
    {
        unsigned int const Class = static_cast<unsigned int>(Root->ObjectClass);
        if ((Class & 3u) == 3u)                                     // .text:06ce3397 .. 06ce339f
        {
            if ((Class & 0x67u) != 0x67u)                           // .text:06ce33a1 .. 06ce33a7
            {
                // .text:06ce33a9 .. 06ce33b7 -- FUN_06d0c840 on the CSimple id.
                Found = Sys->find_object(static_cast<CSimple*>(Root)->Id);
            }
            else
            {
                // .text:06ce33bb .. 06ce33e5 -- a beam: the block at +0x84 (head
                // +0x88), node->Data read without a null test, the first whose
                // cobject() (vt +0x150) is the collided part itself (not the root).
                FSystemListNode* Node = nullptr;
                while (Sys->Beams.traverse(Node, true))
                {
                    GameObject* const Data = Node->Data;
                    if (Data->cobject() == Part)
                    {
                        Found = Data;
                        break;
                    }
                }
            }
        }
    }

    *OutObj = Found;                                                // .text:06ce33f1
    if (Found == nullptr)
        return;                                                     // .text:06ce33f3

    // .text:06ce33f9 .. 06ce341e -- the wrapper's CObject (CBase +0x10) under the
    // CEqObj mask; anything else was hit on its root.
    CObject* const Obj = Found->Object;
    if (Obj == nullptr || (static_cast<unsigned int>(Obj->ObjectClass) & 0x103u) != 0x103u)
    {
        *OutSubObjId = SUBOBJ_ID_ROOT;                              // .text:06ce34b2 .. 06ce34bf
        return;
    }

    // .text:06ce3424 .. 06ce3455 -- CObject::part_to_inst inlined on the PART,
    // with -1 (no map entry, or an entry of -1) falling back to its own engine
    // instance (+0x04).
    long Inst = Part->part_to_inst(PartId);
    if (Inst == -1)
        Inst = Part->EngineInstanceId;

    // .text:06ce3458 .. 06ce3467 -- CEqObj::inst_to_subobj_id (vt +0xf4).
    CEqObj* const Eq = static_cast<CEqObj*>(Obj);
    unsigned short const SubObjId = Eq->inst_to_subobj_id(Inst);
    *OutSubObjId = SubObjId;

    // .text:06ce346a .. 06ce3476 -- already a shield: done.
    if (SubObjectID::IsShieldEquipID(SubObjId))
        return;

    // .text:06ce3478 .. 06ce34a8 -- a functioning shield (IsFunctioning, CEquip
    // vt +0xc) takes the hit instead: its id (+0x08).
    CEShield* const Shield = CEShield::cast(Eq->EquipManager.FindFirst(0x100));
    if (Shield == nullptr)
        return;
    if (!Shield->IsFunctioning())
        return;
    *OutSubObjId = Shield->Id;
}

// ---------------------------------------------------------------------------
// FUN_06ce34d0 (.text:06ce34d0, thiscall on the StarSystem, plain RET; the name
// is the port's). The system's collision pass: PhySys::GenerateCollisions once
// per node of the block at +0x84, then every event PhySys::GetCollisions
// returns for this system is resolved on both sides and delivered to both
// wrappers' on_collision (vt +0x1a4). Only caller: the multiplayer tail of
// StarSystem_Update (.text:06d0c48b).
// ---------------------------------------------------------------------------
static void StarSystem_ProcessCollisions(StarSystem* Sys)
{
    // .text:06ce34e5 .. 06ce3521 -- the beam block; a node whose Data, CObject
    // or class (0x67) does not make a CBeam passes null.
    FSystemListNode* Node = nullptr;
    while (Sys->Beams.traverse(Node, true))
    {
        CBeam* Beam = nullptr;
        GameObject* const Data = Node->Data;
        if (Data != nullptr)
        {
            CObject* const BeamObject = Data->Object;
            if (BeamObject != nullptr &&
                (static_cast<unsigned int>(BeamObject->ObjectClass) & 0x67u) == 0x67u)
                Beam = static_cast<CBeam*>(BeamObject);
        }
        PhySys::GenerateCollisions(Beam);                           // .text:06ce351c
    }

    // .text:06ce3523 .. 06ce3535 -- FUN_06d06cd0 is the system's nickname id.
    PhySys::CollisionEvent const* Begin = nullptr;
    PhySys::CollisionEvent const* End = nullptr;
    PhySys::GetCollisions(Sys->SystemInfo->NicknameId, Begin, End);

    // .text:06ce3546 .. 06ce36de -- the next element is taken (+0x34) before
    // the dispatch, the end compared after it.
    for (PhySys::CollisionEvent const* Event = Begin; Event != End; ++Event)
    {
        GameObject* ObjA = nullptr;
        unsigned short SubA = 0;
        StarSystem_ResolveCollisionSide(Sys, Event->ObjectA, Event->PartIdA, &ObjA, &SubA);
        if (ObjA == nullptr)
            continue;                                               // .text:06ce3577

        GameObject* ObjB = nullptr;
        unsigned short SubB = 0;
        StarSystem_ResolveCollisionSide(Sys, Event->ObjectB, Event->PartIdB, &ObjB, &SubB);
        if (ObjB == nullptr)
            continue;                                               // .text:06ce359c

        // .text:06ce35a2 .. 06ce3610 -- side A's record at [ESP+0x40]. The two
        // pad words are never written by retail (stack contents); the port
        // stores 0.
        FCollisionEvent HitA;
        HitA.SubObjId = SubA;
        HitA.Pad02 = 0;
        HitA.Other = ObjB;
        HitA.OtherSubObjId = SubB;
        HitA.Pad0a = 0;
        HitA.Point = Event->Point;                                   // event +0x10
        HitA.Field_0x18 = Event->SurfaceNormal;                         // event +0x1c
        HitA.Field_0x24 = Event->RelativeVelocity;                         // event +0x28
        ObjA->on_collision(HitA);                                    // .text:06ce3610

        // .text:06ce3616 .. 06ce36cc -- side B's record at [ESP+0x70]: the
        // partners swapped, the point shared, both vectors negated (FLD / FCHS
        // / FSTP dword per component).
        FCollisionEvent HitB;
        HitB.SubObjId = SubB;
        HitB.Pad02 = 0;
        HitB.Other = ObjA;
        HitB.OtherSubObjId = SubA;
        HitB.Pad0a = 0;
        HitB.Point = Event->Point;
        HitB.Field_0x18.x = -Event->SurfaceNormal.x;
        HitB.Field_0x18.y = -Event->SurfaceNormal.y;
        HitB.Field_0x18.z = -Event->SurfaceNormal.z;
        HitB.Field_0x24.x = -Event->RelativeVelocity.x;
        HitB.Field_0x24.y = -Event->RelativeVelocity.y;
        HitB.Field_0x24.z = -Event->RelativeVelocity.z;
        ObjB->on_collision(HitB);                                    // .text:06ce36cc
    }
}

// ---------------------------------------------------------------------------
// FUN_06d0be50 -- StarSystem's update, thiscall with one float (RET 4). Retail
// has no name for it; the port calls it StarSystem_Update and keeps it a
// file-scope function because everything it calls lives here.
//
// Outer loop (.text:06d0bed2 .. .text:06d0c4c2):
//   Target = (double)dt (.text:06d0be6f / 06d0be77, before the clamp);
//   Step = dt; MP only: if ((double)dt > 0.2 (DAT_06d68298, double
//   0x3fc999999999999a; FCOMP / TEST AH,0x41)) Step = 0.2f (0x3e4ccccd, written
//   back into the argument slot);
//   Accum = 0; if (!(0.0 < Target)) skip (DAT_06d65538 = 0.0, TEST AH,5 / JP --
//   NaN skips too);
//   do {
//     Accum = (double)Step + Accum;     // .text:06d0bed2 FLD float, 06d0bee0
//                                       // FADD double ptr, 06d0beef FSTP double
//     …work with Step…
//     if (Target - Accum < Step) Step = (float)(Target - Accum);   // 06d0c490..b1
//   } while (Accum < Target);                                     // 06d0c4b5..c2
//
// Why retail needs no iter_cap / zero_step guard (listing + client clamp):
//   - freelancer.exe DAT_00610a08 = 1.0f caps ElapseTime dt at 1s (never rewritten).
//   - SP: Step == dt == Target → exactly one body iteration when Target > 0.
//   - MP: Step ≤ 0.2 → at most ceil(1.0/0.2) = 5 full steps. When Accum is just
//     short of Target, Target - Accum is at least one ulp of Accum, and its float
//     rounding is still more than half an ulp, so the next double add always
//     moves Accum forward -- the loop cannot stall.
// A port that sees iters>>5 or a hang has an upstream bad dt/Step, not a missing cap.
//
// Callers: FUN_06d10320 (the per-frame walk, Server_PhysicsUpdate below) and
// IServerImpl::Dock (.text:06cf168b, with 0.2f).
//
// Returns the number of step iterations. That is the port's own diagnostic
// for the first-frame log line; retail returns nothing.
//
// FUN_06d03480 (.text:06d0c13d, the SrvAsteroid field update) is called, and
// the server asteroid objects it spawns (SrvAsteroid::MedAsteroid/MedMine,
// server_classes.cpp) exist. The ship-proximity pass (.text:06d0bf02 ..
// .text:06d0c0f4, StarSystem_UpdateNearShips), CmnAsteroid::Update
// (.text:06d0c136), the multiplayer beam-move pass (.text:06d0c41c ..
// .text:06d0c475) and the collision pass FUN_06ce34d0 (.text:06d0c48b,
// StarSystem_ProcessCollisions) are ported.
// The Reputation walk after the loop (.text:06d0c4f1 on) is
// StarSystem_SendChangedAffiliations.
// ---------------------------------------------------------------------------
static unsigned int StarSystem_Update(StarSystem* Sys, float Dt)
{
    // .text:06d0be94 .. 06d0beb2
    float Step = Dt;
    if (!SinglePlayer() && static_cast<double>(Step) > 0.2)
        Step = 0.2f;

    // Retail: param_9 = (double)original_dt; loop gated by 0.0 < param_9.
    const double Target = static_cast<double>(Dt);
    unsigned int Iterations = 0;

    if (Target > 0.0)
    {
        double Accum = 0.0;
        float CurStep = Step;
        do
        {
            // .text:06d0bed2 FLD float [step] / .text:06d0bee0 FADD double
            // ptr [Accum] / .text:06d0beef FSTP double -- a double add.
            Accum = static_cast<double>(CurStep) + Accum;
            ++Iterations;

            // Retail FUN_06d0be50 step body, .text:06d0bed2 .. .text:06d0c14d.
            // Every FUN_06cf4f40 call pushes the block at +0xe8 as the deferred
            // list (ESI, .text:06d0bee4 / .text:06d0c0fd) and the step (EDI).
            // SP never calls PhySys::Update here (.text:06d0c415 gate).
            ObjectList* const Deferred = &Sys->Deferred;
            StarSystem_TickObjectList(&Sys->Ships, CurStep, Deferred);    // .text:06d0bef3
            StarSystem_TickObjectList(&Sys->List70, CurStep, Deferred);   // .text:06d0befd

            // .text:06d0bf02 .. .text:06d0c0f4 -- the ship-proximity pass.
            StarSystem_UpdateNearShips(Sys, CurStep);

            StarSystem_TickObjectList(&Sys->Loot, CurStep, Deferred);     // .text:06d0c108
            StarSystem_TickObjectList(&Sys->Beams, CurStep, Deferred);    // .text:06d0c115
            StarSystem_TickObjectList(&Sys->List98, CurStep, Deferred);   // .text:06d0c122
            StarSystem_TickObjectList(&Sys->ListAC, CurStep, Deferred);   // .text:06d0c12f

            // .text:06d0c134 .. .text:06d0c136 -- COMMON.DLL::CmnAsteroid::Update
            // (import .rdata:06d646d8), cdecl (this, step): StarSystem is the
            // IObjDB it walks.
            CmnAsteroid::Update(Sys, CurStep);

            // .text:06d0c13c .. .text:06d0c13d FUN_06d03480(this) -- looks this
            // system's id up (IObjDB slot 2, then the ISystem's vtable[0]) in
            // the SrvAsteroid::SrvAsteroidSystem map at DAT_06d8d980 and runs
            // FUN_06d04480 on each of its fields (src/Server/server_classes.cpp).
            SrvAsteroid::SrvAsteroid_UpdateSystem(Sys);

            StarSystem_TickObjectList(&Sys->ListC0, CurStep, Deferred);   // .text:06d0c14d

            // (a) .text:06d0c152 .. .text:06d0c1ac -- drain the block at +0xe8,
            // which the passes above fill with the objects whose update()
            // answered false. Retail:
            //   it = 0;
            //   while (FUN_06d16120(block, &it, true)) {    // MetaList::traverse
            //     if (it->Data) it->Data's CBase sub-object, vtable slot 0, flag 1;
            //     it->Data = 0;                             // .text:06d0c190
            //     FUN_06d17090(block, &it);                 // MetaList::remove
            //   }
            // The node removed is always the head, so `it` comes back null and
            // the next traverse starts from the head again.
            {
                FSystemListNode* It = nullptr;
                while (Deferred->traverse(It, true))
                {
                    GameObject* const Data = It->Data;
                    if (Data != nullptr)
                    {
                        // `MOV EDX,[EAX+0xc] / LEA ECX,[EAX+0xc] / PUSH 1 /
                        // CALL [EDX]` -- the deleting destructor through the
                        // CBase sub-object.
                        delete static_cast<CBase*>(Data);
                    }
                    It->Data = nullptr;
                    Deferred->remove(It);
                }
            }

            // (b) .text:06d0c1ae .. .text:06d0c3f3 -- the dock queue at +0x14
            // (server_sysdb.h, SystemDockNode). Skipped outright while it is
            // empty (.text:06d0c1ae `MOV EAX,[EBX+0x1c]` / `JZ LAB_06d0c40d`).
            if (Sys->ObjectListSize != 0)
            {
                // .text:06d0c1bb .. .text:06d0c1d2 -- a local std::list of
                // player ids (FUN_06cfc340 builds its sentinel). The players
                // whose ships land this step are collected into it.
                std::list<unsigned int> Landed;

                // .text:06d0c1d6 .. .text:06d0c1e4 -- walk from the sentinel's
                // Next; the end test re-reads `[this+0x18]` every pass
                // (.text:06d0c33f .. .text:06d0c348).
                SystemDockNode* Node = static_cast<SystemDockNode*>(Sys->ObjectListHead)->Next;
                while (Node != static_cast<SystemDockNode*>(Sys->ObjectListHead))
                {
                    // .text:06d0c1f0 .. .text:06d0c20c -- both watchers back to
                    // their wrappers.
                    GameObject* const Obj = FSystemDockEntry::WatchedObject(Node->Entry.Object);
                    GameObject* const DockTarget = FSystemDockEntry::WatchedObject(Node->Entry.Target);

                    // .text:06d0c20e .. .text:06d0c248 -- the target's id, but
                    // only when its CEqObj has no DockingBaseId (+0x160). The
                    // CEqObj comes from CBase::Object under the 0x103 class
                    // mask; retail reads +0x160 through it without a null test
                    // (.text:06d0c237 XOR EAX,EAX falls straight into
                    // .text:06d0c239 `MOV EDX,[EAX+0x160]`). Every producer
                    // passes a CEqObj-backed wrapper or no target at all.
                    unsigned int TargetId = 0;
                    if (DockTarget != nullptr)
                    {
                        CObject* const TargetObject = DockTarget->Object;
                        CEqObj* TargetEq = nullptr;
                        if (TargetObject != nullptr &&
                            (static_cast<unsigned int>(TargetObject->ObjectClass) & 0x103u) == 0x103u)
                            TargetEq = static_cast<CEqObj*>(TargetObject);
                        if (TargetEq->DockingBaseId == 0)
                            TargetId = DockTarget->get_id();
                    }

                    // .text:06d0c24c MOV AL,[EBX+0x1c] -- the land byte.
                    if (Node->Entry.bLand)
                    {
                        // .text:06d0c257 .. .text:06d0c28e -- every player in
                        // the system is told (FLPACKET_SERVER_LAND, and the
                        // multiplayer autosave of the owner's slot). ObjId is
                        // re-read through vtable +0x20 for each view
                        // (.text:06d0c26b); Arg3 is `LEA EBP,[EBX+0x18]`, the
                        // entry's BaseId, by address.
                        SystemPlayerNode* View = static_cast<SystemPlayerNode*>(Sys->PlayerListHead)->Next;
                        while (View != static_cast<SystemPlayerNode*>(Sys->PlayerListHead))
                        {
                            unsigned int ObjId = Obj->get_id();
                            SystemPlayerView_OnObjectLanded(&View->View, &ObjId, &TargetId,
                                                            &Node->Entry.BaseId);
                            View = View->Next;
                        }

                        // .text:06d0c290 .. .text:06d0c2bf -- event 0x27, land = 1.
                        FDockEventPayload Event;
                        Event.ObjectId = Obj->get_id();
                        Event.BaseId = Node->Entry.BaseId;
                        Event.TargetId = TargetId;
                        Event.bLand = true;
                        Server_ControllerDispatch(0, pub::Controller::EMessage::Msg0x27, &Event);

                        // .text:06d0c2c4 .. .text:06d0c2e7 -- CSimple::OwnerPlayer
                        // (+0xb4) under the (ObjectClass & 3) == 3 guard, read
                        // without a null test.
                        unsigned int const Owner = Obj->csimple()->OwnerPlayer;
                        if (Owner == 0)
                        {
                            // .text:06d0c2e9 .. .text:06d0c2ef -- vtable +0x158
                            // with (0, 0): nobody owns it, it just goes.
                            Obj->destroy(0, 0);
                        }
                        else
                        {
                            // .text:06d0c2f7 .. .text:06d0c30a -- FUN_06d05d90,
                            // std::list<unsigned int>::insert at the sentinel.
                            Landed.push_back(Owner);
                        }
                    }
                    else
                    {
                        // .text:06d0c311 .. .text:06d0c337 -- event 0x27, land = 0.
                        FDockEventPayload Event;
                        Event.ObjectId = Obj->get_id();
                        Event.BaseId = Node->Entry.BaseId;
                        Event.TargetId = TargetId;
                        Event.bLand = false;
                        Server_ControllerDispatch(0, pub::Controller::EMessage::Msg0x27, &Event);
                    }

                    Node = Node->Next;
                }

                // .text:06d0c352 .. .text:06d0c39a -- every collected player
                // leaves the system: StarSystem::remove_player (FUN_06d08740),
                // the pending-visit flush on Players[id] (FUN_06d4b450, ECX =
                // `Players + id*0x418 - 0x418`) and, in single player only,
                // StarSystem::shutdown (FUN_06d07130).
                for (std::list<unsigned int>::iterator It = Landed.begin(); It != Landed.end(); ++It)
                {
                    unsigned int const PlayerId = *It;
                    Sys->remove_player(PlayerId);
                    PlayerData_FlushPendingVisits(Players[PlayerId]);
                    if (SinglePlayer())
                        Sys->shutdown();
                }

                // .text:06d0c39c .. .text:06d0c3ac -- FUN_06cf7720 empties the
                // local list; .text:06d0c3b1 .. .text:06d0c3c3 -- FUN_06d0f410
                // empties the dock queue, [begin, end).
                Landed.clear();
                SystemDockNode* const DockHead = static_cast<SystemDockNode*>(Sys->ObjectListHead);
                Sys->erase_docks(DockHead->Next, DockHead);
                // .text:06d0c3e4 / .text:06d0c3ee -- the second (empty) erase
                // and the sentinel's operator delete: Landed's destructor.
            }

            // .text:06d0c40d .. .text:06d0c48b -- multiplayer only, and only
            // while the system has players (06d0c417 `CMP [EBX+0x10],ESI`,
            // ESI = 0, the player list's size).
            // SinglePlayer() is called again here (.text:06d0c40d), not reused
            // from the clamp.
            if (!SinglePlayer() && Sys->PlayerListSize != 0)
            {
                // .text:06d0c41c .. .text:06d0c475 -- every node of the block at
                // +0x84 (Beams), walked with FUN_06d16120(block, &it, 1):
                // it = it ? it->Next : block head (+0x88). A null Data
                // (06d0c43f), a null CObject (06d0c44f) or a class outside the
                // 0x67 mask (06d0c457) all zero ECX, and CBeam::move is STILL
                // called (06d0c45e / 06d0c45f) -- retail has no test on the
                // result, so every object on this block is a beam there.
                FSystemListNode* BeamNode = nullptr;
                while (Sys->Beams.traverse(BeamNode, true))
                {
                    GameObject* const BeamWrapper = BeamNode->Data;
                    CObject* const BeamObject =
                        (BeamWrapper != nullptr) ? BeamWrapper->Object : nullptr;
                    CBeam* Beam = nullptr;
                    if (BeamObject != nullptr &&
                        (static_cast<unsigned int>(BeamObject->ObjectClass) & 0x67u) == 0x67u)
                        Beam = static_cast<CBeam*>(BeamObject);
                    Beam->move(CurStep);                                   // 06d0c45f
                }

                // .text:06d0c477 .. .text:06d0c480
                PhySys::Update(Sys->SystemInfo->NicknameId, CurStep);

                // .text:06d0c489 .. 06d0c48b -- FUN_06ce34d0(this), the
                // collision pass.
                StarSystem_ProcessCollisions(Sys);
            }

            if (Target - Accum < static_cast<double>(CurStep))
                CurStep = static_cast<float>(Target - Accum);
        } while (Accum < Target);
    }

    // .text:06d0c4c8 .. 06d0c4ef -- after the step loop (or straight from the
    // 06d0becc skip): when the player list is non-empty ([this+0x10]), every
    // view gets FUN_06cf53e0 with the original dt as a double (EDI:EBP =
    // [ESP+0x48]:[ESP+0x44]); the end test re-reads the head [this+0xc].
    if (Sys->PlayerListSize != 0)
    {
        for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(Sys->PlayerListHead)->Next;
             Node != static_cast<SystemPlayerNode*>(Sys->PlayerListHead); Node = Node->Next)
        {
            SystemPlayerView_Update(&Node->View, Target);
        }
    }

    StarSystem_SendChangedAffiliations(Sys);                       // .text:06d0c4f1

    return Iterations;
}

// FUN_06d10320 (.text:06d10320, __cdecl, one float -- `PUSH EAX` of FUN_06d001d0's
// own argument at .text:06d001d9, `ADD ESP,0x4` after) -- the per-frame walk.
// Traced term by term 2026-09-24 (claude-code:server-triage-s2):
//   .text:06d10323 .. 06d10338  outside single player, AnimDB::Update(dt) first;
//   .text:06d1033b .. 06d10365  a function-local static std::vector<unsigned int>
//                               of the systems already stepped this frame
//                               (DAT_06d8da18: guard bit 0 of DAT_06d8da28, ctor
//                               FUN_06d10480, atexit dtor FUN_06d10470);
//   .text:06d10368 .. 06d1044a  for every active player (PlayerDB::traverse_active):
//     - its system id (PlayerData+0x3e0) already in the vector -> skip
//       (the inline find at .text:06d10380 .. 06d103a4);
//     - SysDB::SysMap lower_bound + key compare (.text:06d103bb .. 06d103e6),
//       i.e. find(); when present, FUN_06d0be50(dt) on the mapped StarSystem
//       (node+0x10, .text:06d103e9 .. 06d103ec). No loaded test here -- retail
//       steps whatever the map holds;
//     - find again and, when still absent, insert at end (FUN_06d0f460 with
//       the vector's _Last, .text:06d10413 .. 06d1041a). The third find at
//       .text:06d1041f .. 06d10439 has no effect on the result;
//   .text:06d10450 / .text:06d1045f  _Last = _First -- the vector is emptied,
//                               its storage kept, on both exits.
// No cap on the number of systems and no loaded guard: retail has neither.
static void Server_StepSystems(float dt) {
    if (!SinglePlayer())
        AnimDB::Update(dt);

    static std::vector<unsigned int> SteppedSystems;

    PlayerData* Player = Players.traverse_active(nullptr);
    while (Player != nullptr)
    {
        const unsigned int SysId = Player->system_id;

        bool bAlready = false;
        for (size_t i = 0; i < SteppedSystems.size(); ++i)
        {
            if (SteppedSystems[i] == SysId)
            {
                bAlready = true;
                break;
            }
        }

        if (!bAlready)
        {
            SysDB::SysMapType::iterator It = SysDB::SysMap.find(SysId);
            if (It != SysDB::SysMap.end())
            {
                StarSystem_Update(&It->second, dt);
            }

            bool bPresent = false;
            for (size_t i = 0; i < SteppedSystems.size(); ++i)
            {
                if (SteppedSystems[i] == SysId)
                {
                    bPresent = true;
                    break;
                }
            }
            if (!bPresent)
                SteppedSystems.push_back(SysId);
        }

        Player = Players.traverse_active(Player);
    }

    SteppedSystems.clear();
}

// FUN_06d001d0 (.text:06d001d0 .. 06d00272, cdecl, one float; only caller
// .text:06d45196). Every instruction:
//   06d001d9 / 06d001da  FUN_06d10320(dt)            -- Server_StepSystems
//   06d001e2             FUN_06cffdc0()              -- the phantom event pump
//   06d001e7 .. 06d00232 for every node of the dying-ship list (head re-read
//                        from .data:DAT_06d8d8e4 each lap):
//                          [ESP+0x10] = FUN_06d06cd0(ship->OwnerSystem)
//                                       (SystemInfo vtable slot 0)
//                          [ESP+0x08] = ship->get_id()     (vtable +0x20)
//                          [ESP+0x0c] = ship->DeathSequenceObjId (+0x90)
//                          FUN_06d43650(0x25, &[ESP+0x08])
//   06d00234 .. 06d0026b clear the list: unlink, free, --size, per node.
// The system id is read BEFORE get_id (06d001f8 vs 06d00206).
static void Server_PhysicsUpdate(float dt) {
    Server_StepSystems(dt);

    Server_PumpPhantomEvents();                                  // .text:06d001e2

    for (std::list<Ship*>::iterator It = GDyingShips.begin(); It != GDyingShips.end(); ++It) {
        Ship* const Dying = *It;
        unsigned int Event[3];
        Event[2] = Dying->OwnerSystem->SystemInfo->NicknameId;   // .text:06d001f8 / 06d00200
        Event[0] = Dying->get_id();                              // .text:06d00206 / 06d00209
        Event[1] = Dying->DeathSequenceObjId;                    // .text:06d00210 / 06d0021d
        Server_ControllerDispatch(0, pub::Controller::EMessage::Msg0x25, Event); // .text:06d00221
    }
    GDyingShips.clear();                                         // .text:06d00234 .. 06d0026b
}

// FUN_06d43c00 (.text:06d43c00 .. .text:06d43c3a), called by Shutdown (.text:06d451da).
//   FUN_06d441c0(&active)   -- std::list::sort, ascending by record+0x10 = node+0x18
//                              (the Flid), unsigned compare, stable merge sort
//   walk from the LAST node back to the first: pub::Controller::Destroy(node->Flid)
//                              (re-reading the list head's first node each time)
//   FUN_06d43680(0.0)        -- the heartbeat walk, whose first phase tears down
//                              everything Destroy just queued
// The deferred-create list is left alone.
static void Server_ControllerCleanup() {
    // FUN_06d441c0: stable ascending sort by Flid. A stable insertion into a fresh
    // chain gives the same order as the retail merge sort.
    if (g_ControllerListSentinel.Next != &g_ControllerListSentinel) {
        FControllerNode* Pending = g_ControllerListSentinel.Next;
        g_ControllerListSentinel.Prev->Next = nullptr;
        g_ControllerListSentinel.Next = &g_ControllerListSentinel;
        g_ControllerListSentinel.Prev = &g_ControllerListSentinel;
        while (Pending != nullptr) {
            FControllerNode* NextPending = Pending->Next;
            FControllerNode* Before = g_ControllerListSentinel.Prev;
            while (Before != &g_ControllerListSentinel && Pending->FLID < Before->FLID)
                Before = Before->Prev;
            ListInsertBefore(Before->Next, Pending);
            Pending = NextPending;
        }
    }

    FControllerNode* cur = &g_ControllerListSentinel;
    if (cur != cur->Next) {
        do {
            Server_ControllerDestroy(cur->Prev->FLID);
            cur = cur->Prev;
        } while (cur != g_ControllerListSentinel.Next);
    }
    Server_TimerTick(0.0f);
}

// FUN_06d43680 (.text:06d43680 .. .text:06d4381f), cdecl (float dt): the controller
// heartbeat walk, called every ElapseTime (.text:06d451a2) and once by the cleanup.
//   1. for each queued Destroy (DAT_06d8f62c): GetProcAddress(Module,"DestroyInstance")
//      (Interface); GetModuleFileNameA (result unused); FreeLibrary(Module); unlink the
//      node from the active list, free it, DAT_06d8f648--. Then empty the queue.
//   2. ++DAT_06d8f764
//   3. for each active node with period (+0x10) >= 0: accum (+0x14) += dt; if
//      accum >= period: WrapTime = (float)(floor(accum / period) * period);
//      Interface->vtable[0](0x20, &WrapTime) (thiscall, .text:06d4377d);
//      accum = accum (re-read) - WrapTime
//   4. --DAT_06d8f764; at zero drain the deferred-create list (as _SendMessage does)
// Port field names: HeartbeatTimer is retail +0x10 (period), HeartbeatAccum +0x14.
static void Server_TimerTick(float dt) {
    for (std::list<FControllerNode*>::iterator It = g_DestroyPending.begin();
         It != g_DestroyPending.end(); ++It) {
        FControllerNode* Node = *It;
        FControllerDestroyInstanceFn DestroyInstance =
            (FControllerDestroyInstanceFn)GetProcAddress(Node->ModuleHandle, "DestroyInstance");
        DestroyInstance(Node->InterfacePtr);
        char ModuleName[0x104];
        GetModuleFileNameA(Node->ModuleHandle, ModuleName, 0x104);
        FreeLibrary(Node->ModuleHandle);
        ListRemove(Node);
        free(Node);
        g_ControllerCount--;
    }
    g_DestroyPending.clear();

    g_ReentrancyGuard++;
    FControllerNode* cur = g_ControllerListSentinel.Next;
    while (cur != &g_ControllerListSentinel) {
        if (cur->HeartbeatTimer >= 0.0f) {
            // FLD dt / FADD [+0x14] / FCOM [+0x10] / FST [+0x14]: the compare and the
            // FDIV use the unrounded x87 sum still in ST0, only the store is rounded.
            const double Sum = static_cast<double>(dt) + static_cast<double>(cur->HeartbeatAccum);
            cur->HeartbeatAccum = static_cast<float>(Sum);
            const double Period = static_cast<double>(cur->HeartbeatTimer);
            if (Sum >= Period) {
                float WrapTime = static_cast<float>(floor(Sum / Period) * Period);
                cur->InterfacePtr->HandleMessage(pub::Controller::EMessage::Heartbeat, &WrapTime);
                cur->HeartbeatAccum = cur->HeartbeatAccum - WrapTime;
            }
        }
        cur = cur->Next;
    }
    g_ReentrancyGuard--;
    if (g_ReentrancyGuard == 0)
        Server_ControllerDrainDeferred();
}

// Deferred-create drain, shared by the heartbeat walk and by the broadcast arm of
// Server_ControllerDispatch (retail 0x06d440d5-0x06d4413d does it in both places).
static void Server_ControllerDrainDeferred() {
    FControllerNode* cur = g_DeferredListSentinel.Next;
    while (cur != &g_DeferredListSentinel) {
        FControllerNode* next = cur->Next;
        ListRemove(cur);
        g_DeferredCount--;
        // Insert into active list sorted by priority
        FControllerNode* insert_cur = g_ControllerListSentinel.Next;
        while (insert_cur != &g_ControllerListSentinel) {
            if (cur->Priority <= insert_cur->Priority) break;
            insert_cur = insert_cur->Next;
        }
        cur->Next = insert_cur;
        cur->Prev = insert_cur->Prev;
        insert_cur->Prev->Next = cur;
        insert_cur->Prev = cur;
        g_ControllerCount++;
        cur = next;
    }
}

// ---------------------------------------------------------------------------
// Server_FormatTString256 -- retail FUN_06d5f040 (.text:06d5f040 .. .text:06d5f0b1),
// cdecl varargs (TString<256>* Out, char const* Fmt, ...), plain RET. Only caller:
// the timer tick below (.text:06d5dfcd).
//   06d5f04e..06d5f05c  vsprintf(char Local[0x100], Fmt, &varargs) -- unbounded
//   06d5f062..06d5f0aa  the inlined TString<256>::operator=(Local): strlen, a
//                       length >= 0x100 becomes 0xff, MOVSD/MOVSB copy into
//                       Out+4, Out->Data[len] = 0, Out->Length = len. No null test
//                       (the source is always the local buffer).
// ---------------------------------------------------------------------------
static void Server_FormatTString256(TString<256>* Out, char const* Fmt, ...) {
    char Local[0x100];
    va_list Args;
    va_start(Args, Fmt);
    vsprintf(Local, Fmt, Args);
    va_end(Args);
    unsigned int Len = static_cast<unsigned int>(strlen(Local));
    if (Len >= 0x100)
        Len = 0xff;
    memcpy(Out->Data, Local, Len);
    Out->Data[Len] = '\0';
    Out->Length = Len;
}

// FUN_06d5df60 — Timer firing system
static void Server_TimerFiring(float dt) {
    FTimerNode* expired[128];
    unsigned int expired_count = 0;

    // Phase 1 (.text:06d5df6e .. .text:06d5dffb): every timer loses dt; while
    // fewer than 0x80 are collected (CMP EBP,0x80 / JNC after the FSTP), one whose
    // remaining time is below 0.0 (FCOMP double 0.0, TEST AH,5 / JP) is collected.
    FTimerNode* cur = g_TimerListSentinel.Next;
    while (cur != &g_TimerListSentinel) {
        cur->RemainingTime -= dt;
        if (expired_count < 128 && cur->RemainingTime < 0.0f) {
            expired[expired_count] = cur;
            expired_count++;
            // .text:06d5dfad .. .text:06d5dff0 -- the collection that fills the
            // 128th entry warns once: FUN_06d5f040 formats the pubMisc.cpp message
            // with the timer count DAT_06d9046c into a TString<256> (Length and
            // Data[0] zeroed first, .text:06d5dfc5/06d5dfc9) and FDUMP(0x100002,
            // "%s(%d) : *** WARNING: %s", pubMisc.cpp, 0x3c, Text.Data).
            if (expired_count == 128) {
                TString<256> Text;
                Text.Length = 0;
                Text.Data[0] = '\0';
                Server_FormatTString256(&Text,
                    "%x timers are trying to fire on the same frame.  Only 64 will fire now.  "
                    "The next ones will fire on the next frame.",
                    g_TimerCount);
                FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
                      "E:\\FL\\Scratch\\Source\\Server\\pubMisc.cpp", 0x3c, Text.Data);
            }
        }
        cur = cur->Next;
    }

    // Phase 2: Fire all expired timers
    for (unsigned int i = 0; i < expired_count; i++) {
        FTimerNode* timer = expired[i];
        // Retail 0x06d5e00a-0x06d5e014:
        //   _SendMessage(node+0x08 /* the timer's OWN Flid, by reference */, 0x3e,
        //                node+0x0c /* the two TimerExpired dwords */)
        // i.e. the expiry is delivered to that one controller, not broadcast. The port
        // used to route it through the empty Common.dll dispatcher instead.
        Server_ControllerDispatch(timer->TargetController, pub::Controller::EMessage::TimerExpired,
                                  &timer->Param1);
        // Unlink and free
        TimerListRemove(timer);
        g_TimerCount--;
        free(timer);
    }
}

// `Server` -- retail export ordinal 524, RVA 0xAFACC (.data:06d8facc), UNDECORATED
// (dumpbin -exports EXE\server.dll: `524 20B 000AFACC Server`), so it is an
// extern "C" data symbol and cannot carry the G prefix. It is the one IServerImpl
// object, twelve bytes: the dynamic initializer LAB_06d44740 (.data:06d882bc in
// the __xc table) is the inlined default ctor --
//   06d44740  MOV [06d8fad4],0                        ; Field_0x8
//   06d4474a  MOV [06d8facc],??_7IServerImpl@@6BIServer@@@      (06cf0dc0 table)
//   06d44754  MOV [06d8fad0],??_7IServerImpl@@6BIChatServer@@@  (06d191b0 table)
// FLServer.exe and RemoteClient.dll import it by that name and call IServerImpl
// members on it directly. Inside Server.dll every user is `MOV ECX,.data:Server`
// plus a direct CALL (DisConnect 06d4501d/32/51/67, TerminateTrade at 06d49a6d,
// 06d4a524, 06d4a6f9, 06d50b03, 06d543a6, 06d54465, ForceLand's Dock at
// 06d5678a) or `MOV ECX,.data:DAT_06d8fad0` for SubmitChat (06d4f8e1). The other
// translation units of this port reach the same object through
// GetServerInterface()/GetChatServerInterface(); the dynamic type is IServerImpl, so
// the call lands on the same member retail calls. The class and this object are
// declared in src/include/server_iserverimpl.h.
extern "C" SERVER_API IServerImpl Server;
IServerImpl Server;

extern "C" {
    // 0x06d44760: MOV EAX,.data:Server / RET.
    SERVER_API IServer* __cdecl GetServerInterface() { return &Server; }
    // 0x06d44770: MOV EAX,.data:DAT_06d8fad0 / RET -- Server + 4, the IChatServer
    // subobject (second base).
    SERVER_API void* __cdecl GetChatServerInterface() {
        return static_cast<IChatServer*>(&Server);
    }
}

static HMODULE g_hInstance = nullptr;

BOOL WINAPI DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) { g_hInstance = hModule; DisableThreadLibraryCalls(hModule); }
    return TRUE;
}
