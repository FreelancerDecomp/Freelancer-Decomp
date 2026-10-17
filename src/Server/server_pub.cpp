// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/common_pathfind.h" // PathfindManager (pub::AI::SubmitState)
#include "../include/common_id.h"
#include "../include/common_goods.h"
#include "../include/common_universe.h"
#include "../include/common_archetype.h"
#include "../include/common_reputation.h"
#include "../include/common_iface.h"
#include "../include/common_cobjdb.h"
#include "../include/common_csys.h"
#include "../include/common_cobject.h"
#include "../include/common_server_playerdb.h"
#include "../include/server_exports.h"
#include "../include/server_basedb.h"
#include "../include/server_cobject.h"
#include "../include/common_physys.h"
#include "../include/server_gameobject.h"
#include "../include/server_cship.h"
#include "../include/server_sysdb.h"
#include "../include/common_misc.h"
#include "../include/common_net.h"      // IsMPServer (AdjustCash's gate)
#include "../include/common_newchar.h"   // RtcSlider::get_slider (pub::GF::GetRtcPerformanceSlider)
#include "../include/DALib.h"            // DALib::Engine (pub::FindHardpoint releases the archetype handle)
#include "../include/readfile.h"         // IFileSystem::GetFileSysPath (+0x84, pub::BuildBaseReader)
#include "../include/x86math.h"      // I3DMathEngine::MatrixToQuaternion (pub::Player::SetCamera)
#include "../include/server_createinfo.h" // Server_Acquire3DMathEngine (FUN_06d5ff70)
#include "../include/rpc_iclient.h"
#include "../include/rpc_iclient_args.h" // FPayloadServerSetDirective (pub::AI::SubmitDirective)
#include "../include/server_savefile.h"   // SaveGame_Writer / ISave / Server_FLS1Transform
#include "../include/server_savegame.h"   // pub::Save -> PlayerData_AutoSave / PlayerData_MissionSave
#include "../include/common_casteroid.h"
#include <windows.h>
#include <cstdint>
#include <new>
#include <cmath>
#include <cstdio>
#include <cstring>     // std::memcpy (SpaceObj::Create EquipIdMaker seed)
#include <direct.h>
#include <io.h>        // _unlink (the closeout at .text:06d4dfc7 / .text:06d4e16d)
#include <map>
#include <set>
#include <stdexcept>   // std::out_of_range (Server_VibeID_Free, FUN_06d15f90)
#include <vector>
#include <list>
#include <string>      // Server_AccountDirName's lowercased wide copy
#include <cwchar>      // wcslen / wcstombs / swprintf
#include <cwctype>     // towlower (retail _wcslwr)
#include <cstdlib>     // rand, wcstombs
#include <ctime>       // _strdate_s / _strtime_s (retail _strdate / _strtime)

namespace GlobalReputation = ::Reputation;

// The IClient the server talks back to. Defined in IServerImpl.cpp and set by
// IServerImpl::Startup; retail keeps it in .data:DAT_06d8fb14. Several pub:: exports
// dispatch straight into its vtable, so it has to be reachable from this file too.
extern IClient* g_pClientInterface;

// ---------------------------------------------------------------------------
// The object lookup. Retail FUN_06d006c0 (Server_FindObject, server_classes.cpp)
// searches where the objects really live: its one-entry cache, the cached
// system's blocks, the in-transit list, then every system in SysDB::SysMap. The
// port's former FLID -> wrapper std::map (g_ObjectRegistry, filled by the
// create paths and read here) is gone (2026-09-24, claude-code:w5-server-06d0):
// an object is found only once it is in a StarSystem block, as in retail.
// The lookup hands back the GameObject wrapper: pub::SpaceObj::GetCenterOfMass
// at .text:06cf9069 does `ADD EAX,0xc / MOV ECX,[EAX+4]` on the result, i.e.
// it walks into the CBase sub-object at +0x0c and reads CBase::Object.
// ---------------------------------------------------------------------------
// See server_gameobject.h: the callers that keep only the object.
GameObject* Server_LookupGameObject(unsigned int ObjId) {
    GameObject* Obj = nullptr;
    StarSystem* Sys = nullptr;
    Server_FindObject(ObjId, Obj, Sys);
    return Obj;
}

void* Server_LookupObject(unsigned int obj_id) {
    return Server_LookupGameObject(obj_id);
}

// A second, separate table for the handles that are NOT space objects: pub::GF
// character handles and pub::Phantom zones. Retail keeps neither in the object map
// (characters live in BaseDB's per-room list, phantoms in the phantom subsystem);
// the previous pass put them in the same map as ships and solars, which was already
// wrong and is now also a type error, since that map holds IObjRW wrappers.
// TODO: replace this table with the two real subsystems.
static std::map<unsigned int, void*> g_PlaceholderHandles;

static void Server_RegisterPlaceholder(unsigned int id, void* obj) {
    if (id != 0 && obj != nullptr)
        g_PlaceholderHandles[id] = obj;
}

static void Server_UnregisterPlaceholder(unsigned int id) {
    g_PlaceholderHandles.erase(id);
}

// (Server_LookupPlaceholder was removed 2026-09-17: pub::GF::CharacterDestroy
// was its only reader, and characters now live in BaseDB's real per-room NPC
// list. What is left in this table is pub::Phantom's zone handles.)

// Player validation helper -- the guard every pub::Player export opens with, e.g.
// SetMsnID .text:06d56461 .. .text:06d5648f:
//   TEST ESI,ESI / JBE fail                        ; id == 0
//   LEA EAX,[ESI-1] / CMP EAX,[DAT_06d90264] / JNC ; id-1 >= Players.MaxPlayers
//   IMUL EAX,EAX,0x418 / MOV EDX,[EAX+ECX+0x348]   ; Players[id-1].client_id
//   TEST EDX,EDX / JZ fail
// Retail has no "active" flag in PlayerData: occupancy is the client id at +0x348
// (2026-09-24, claude-code:w5-server-06d4 -- the port's invented `active` bool is
// gone from PlayerData).
static bool IsValidPlayer(unsigned int player_id) {
    if (player_id == 0 || (player_id - 1) >= PlayerDB_GetMaxPlayers()) return false;
    return Players[player_id].client_id != 0;
}

// Forward declarations for functions defined in IServerImpl.cpp
unsigned int Server_ControllerCreate(const char* dll_path, const char* instance_name, void* parms, unsigned int priority);
void Server_ControllerDestroy(unsigned int flid);

// Game time accumulator defined in IServerImpl.cpp (updated by ElapseTime)
extern double g_ElapsedTimeAccum;   // DAT_06d8fb28, IServerImpl.cpp
extern double GServerTimeBase;      // DAT_06d8fb20, IServerImpl.cpp
extern char GDllPath[0x104];        // DAT_06d8f654, IServerImpl.cpp (pub::DLL_LoadLibrary)

// ===========================================================================
// pub global functions -- .text:06d5e440..06d5f033 (0x7e440-0x7f033); per-function
// listing traces are in each function's comment and in its decomp-tracker item
// ===========================================================================
namespace pub {

// pub::IsCommodity -- retail 0x7e440 (ordinal 311), .text:06d5e440..06d5e471.
// GoodList::find_by_id(*GoodId); a miss returns -4 WITHOUT touching OutCommodity.
// Found: OutCommodity = (GoodInfo+0x4c == 0), i.e. Category == GoodCategoryCommodity.
SERVER_API int __cdecl IsCommodity(const unsigned int& GoodId, bool& OutCommodity) {
    GoodInfo const* Info = GoodList::find_by_id(GoodId);
    if (Info == nullptr)
        return -4;
    OutCommodity = (Info->Category == GoodCategoryCommodity);
    return 0;
}

// pub::GetCargoHoldSize -- retail 0x7e480 (ordinal 161), .text:06d5e480..06d5e4c5.
// OutSize = 0 first (06d5e489), then the usual id checks: masked id 0 -> -4,
// Archetype::GetShip miss -> -2; else OutSize = ftol(Ship+0x100 hold_size).
SERVER_API int __cdecl GetCargoHoldSize(const unsigned int& ShipArchId, unsigned int& OutSize) {
    OutSize = 0;
    if ((ShipArchId & 0x3fffffff) == 0)
        return -4;
    Archetype::Ship* ShipArch = Archetype::GetShip(ShipArchId);
    if (ShipArch == nullptr)
        return -2;
    OutSize = static_cast<unsigned int>(static_cast<long long>(ShipArch->HoldSize));
    return 0;
}

// pub::GetLoadoutName -- retail 0x7e4d0 (ordinal 203), .text:06d5e4d0..06d5e518.
// Retail tests the OUTPUT buffer (null, or first byte already 0 -> -2), then
// Loadout::Get(*LoadoutId) (miss -> -2), then ID_String::get_string on the Map
// itself (its first dword is the nickname id; null -> -2), and finally a plain
// strncpy(OutBuffer, Str, MaxLen) -- no forced terminator -- returning 0.
SERVER_API int __cdecl GetLoadoutName(const unsigned int& LoadoutId, char* OutBuffer, int MaxLen) {
    if (OutBuffer == nullptr || OutBuffer[0] == '\0')
        return -2;
    Loadout::Map const* LoadoutMap = Loadout::Get(LoadoutId);
    if (LoadoutMap == nullptr)
        return -2;
    ID_String Name;
    Name.Id = LoadoutMap->NicknameId;
    char const* Str = Name.get_string();
    if (Str == nullptr)
        return -2;
    strncpy(OutBuffer, Str, static_cast<size_t>(MaxLen));
    return 0;
}

// pub::GetLoadoutID -- retail 0x7e520 (ordinal 202), .text:06d5e520..06d5e551.
// Null/empty name -> -4; Loadout::GetID(name) == 0 -> -2 with OutId untouched;
// else OutId = id, 0.
SERVER_API int __cdecl GetLoadoutID(unsigned int& OutId, const char* Nickname) {
    if (Nickname == nullptr || Nickname[0] == '\0')
        return -4;
    unsigned int Id = Loadout::GetID(Nickname);
    if (Id == 0)
        return -2;
    OutId = Id;
    return 0;
}

// pub::GetEquipmentID -- retail 0x7e740 (ordinal 181), .text:06d5e740..06d5e781.
// Null/empty name -> -4; CreateID into an ID_String, Archetype::GetEquipmentID(it)
// -> OutId unconditionally; return (id & 0x3fffffff) ? 0 : -2.
SERVER_API int __cdecl GetEquipmentID(unsigned int& OutId, const char* Nickname) {
    if (Nickname == nullptr || Nickname[0] == '\0')
        return -4;
    ID_String Name;
    Name.Id = CreateID(Nickname);
    unsigned int Id = Archetype::GetEquipmentID(Name);
    OutId = Id;
    return ((Id & 0x3fffffff) != 0) ? 0 : -2;
}

// pub::GetGoodID -- retail 0x7e790 (ordinal 185), .text:06d5e790..06d5e7cf.
// Null/empty name -> -4; GoodList::find_by_nickname miss -> -2 (OutId untouched);
// else OutId = GoodInfo+0x48 (Id) and return (id & 0x3fffffff) ? 0 : -2.
SERVER_API int __cdecl GetGoodID(unsigned int& OutId, const char* Nickname) {
    if (Nickname == nullptr || Nickname[0] == '\0')
        return -4;
    GoodInfo const* Info = GoodList::find_by_nickname(Nickname);
    if (Info == nullptr)
        return -2;
    unsigned int Id = Info->Id;
    OutId = Id;
    return ((Id & 0x3fffffff) != 0) ? 0 : -2;
}

// pub::TranslateArchToGood -- retail 0x7e7d0 (ordinal 465), .text:06d5e7d0..06d5e7ff.
// *ArchId == 0 (unmasked) -> -4; OutGoodId = Arch2Good(*ArchId) unconditionally;
// return (good & 0x3fffffff) ? 0 : -2.
SERVER_API int __cdecl TranslateArchToGood(const unsigned int& ArchId, unsigned int& OutGoodId) {
    if (ArchId == 0)
        return -4;
    unsigned int GoodId = Arch2Good(ArchId);
    OutGoodId = GoodId;
    return ((GoodId & 0x3fffffff) != 0) ? 0 : -2;
}

// pub::GetVoiceID -- retail 0x7e800 (ordinal 296), .text:06d5e800..06d5e829.
// Null/empty name -> -4; else OutId = CreateID(name), 0.
SERVER_API int __cdecl GetVoiceID(unsigned int& OutId, const char* Nickname) {
    if (Nickname == nullptr || Nickname[0] == '\0')
        return -4;
    OutId = CreateID(Nickname);
    return 0;
}

// pub::GetCostumeID -- retail 0x7e830 (ordinal 175), .text:06d5e830..06d5e870.
// Null/empty name -> -4; GetCostumeDescriptions()->find_costume(name); OutId =
// costume ? costume->Id (+0x00) : 0, written in both cases; return
// ((OutId != 0) - 1) & ~1, i.e. 0 when found and -2 otherwise.
SERVER_API int __cdecl GetCostumeID(int& OutId, const char* Nickname) {
    if (Nickname == nullptr || Nickname[0] == '\0')
        return -4;
    costume const* Found = GetCostumeDescriptions()->find_costume(Nickname);
    int Id = 0;
    if (Found != nullptr)
        Id = static_cast<int>(Found->Id);
    OutId = Id;
    return (Id != 0) ? 0 : -2;
}

// pub::GetDataPath -- retail 0x7e880 (ordinal 177): MOV EAX,[DALib::Data] / MOV EAX,[EAX].
SERVER_API IFileSystem* __cdecl GetDataPath() {
    return DALib::Data;
}

SERVER_API int __cdecl GetTime(double& OutTime) {
    // Retail 0x7e890: raw_time = FUN_06d447b0() (_DAT_06d8fb28 + _DAT_06d8fb20)
    // OutTime = raw_time * DaysPerSecond
    OutTime = (g_ElapsedTimeAccum + GServerTimeBase) * DaysPerSecond;
    return 0;
}

SERVER_API int __cdecl GetRwTime(double& OutTime) {
    // Retail 0x7e8b0: OutTime = FUN_06d447b0() (raw seconds, no DaysPerSecond multiply)
    OutTime = g_ElapsedTimeAccum + GServerTimeBase;
    return 0;
}

// pub::NextSystemID -- retail 0x7e8c0 (ordinal 346), .text:06d5e8c0..06d5e90c.
// Retail does not resume Common's iterator: it restarts at GetFirstSystem and, when
// InOutId is non-zero, walks forward until it has stepped PAST the system whose id
// (ISystem vslot 0) equals InOutId. The system it lands on (or the first one, for
// InOutId == 0) is reported; running off the end returns false with InOutId intact.
SERVER_API bool __cdecl NextSystemID(unsigned int& InOutId) {
    Universe::ISystem const* Sys = Universe::GetFirstSystem();
    if (InOutId != 0) {
        while (Sys != nullptr) {
            bool const bMatch = (Sys->NicknameId == InOutId);
            Sys = Universe::GetNextSystem();
            if (bMatch)
                break;
        }
    }
    if (Sys == nullptr)
        return false;
    InOutId = Sys->NicknameId;
    return true;
}

// pub::NextBaseID -- retail 0x7e910 (ordinal 345), .text:06d5e910..06d5e95c. The
// NextSystemID walk over GetFirstBase / GetNextBase and IBase vslot 0.
SERVER_API bool __cdecl NextBaseID(unsigned int& InOutId) {
    Universe::IBase const* Base = Universe::GetFirstBase();
    if (InOutId != 0) {
        while (Base != nullptr) {
            bool const bMatch = (Base->NicknameId == InOutId);
            Base = Universe::GetNextBase();
            if (bMatch)
                break;
        }
    }
    if (Base == nullptr)
        return false;
    InOutId = Base->NicknameId;
    return true;
}

// pub::GetSystemID -- retail 0x7e960 (ordinal 287), .text:06d5e960..06d5e99a.
// Null/empty name -> -4; Universe::get_system_id(name) with a zero low-30-bit id
// -> -2 and OutId untouched; else OutId = id, 0.
SERVER_API int __cdecl GetSystemID(unsigned int& OutId, const char* Nickname) {
    if (Nickname == nullptr || Nickname[0] == '\0')
        return -4;
    unsigned int Id = Universe::get_system_id(Nickname);
    if ((Id & 0x3fffffff) == 0)
        return -2;
    OutId = Id;
    return 0;
}

SERVER_API int __cdecl GetSystemGateConnection(const unsigned int& GateId, unsigned int& OutConnectedSystemId) {
    // Retail 0x7e9a0: Universe::get_gate_system(GateId), check low 30 bits
    unsigned int SysId = Universe::get_gate_system(GateId);
    OutConnectedSystemId = SysId;
    if ((SysId & 0x3fffffff) != 0) {
        return 0;
    }
    return -2;
}

// pub::GetBaseID -- retail 0x7e9d0 (ordinal 153), .text:06d5e9d0..06d5ea0d.
// OutId = 0 first (06d5e9e1); null/empty name and an unknown base BOTH return -4
// (EDI = 0xfffffffc is the only failure code); else OutId = id, 0.
SERVER_API int __cdecl GetBaseID(unsigned int& OutId, const char* Nickname) {
    OutId = 0;
    if (Nickname == nullptr || Nickname[0] == '\0')
        return -4;
    unsigned int Id = Universe::get_base_id(Nickname);
    if ((Id & 0x3fffffff) == 0)
        return -4;
    OutId = Id;
    return 0;
}

// pub::BuildSystemReader -- retail 0x7ea10 (ordinal 68), .text:06d5ea10..06d5eaa5.
// Reader.close(); get_system miss -> -2; Universe::get_filename(path, ISystem vslot 5
// = the system's "file"); Reader.open(path, false) -> 0, else FDUMP the missing file
// (pubMisc.cpp line 0x2b3) and -2.
SERVER_API int __cdecl BuildSystemReader(INI_Reader& Reader, const unsigned int& SystemId) {
    Reader.close();
    Universe::ISystem const* Sys = Universe::get_system(SystemId);
    if (Sys == nullptr)
        return -2;
    char Path[0x104];
    Universe::get_filename(Path, Sys->File);
    if (Reader.open(Path, false))
        return 0;
    FDUMP(0x100001, "%s(%d) : *** ERROR: missing System file '%s'",
          "E:\\FL\\Scratch\\Source\\Server\\pubMisc.cpp", 0x2b3, Path);
    return -2;
}

// pub::BuildBaseReader -- retail 0x7eab0 (ordinal 66), .text:06d5eab0..06d5eb50.
// Reader.close(); get_base miss -> -2; (*DALib::Data)->GetFileSysPath(dir, 0x104)
// (IFileSystem +0x84); _makepath(path, NULL, dir, IBase vslot 3 = the base's "file",
// NULL); Reader.open(path, false) ? 0 : -2 (no FDUMP on this one).
SERVER_API int __cdecl BuildBaseReader(INI_Reader& Reader, const unsigned int& BaseId) {
    Reader.close();
    Universe::IBase const* Base = Universe::get_base(BaseId);
    if (Base == nullptr)
        return -2;
    char DataDir[0x104];
    DALib::Data->GetFileSysPath(DataDir, 0x104);
    char Path[0x104];
    _makepath(Path, nullptr, DataDir, Base->File, nullptr);
    if (Reader.open(Path, false))
        return 0;
    return -2;
}

// pub::GetSystemNickname -- retail 0x7eb60 (ordinal 288), .text:06d5eb60..06d5ebb0.
// get_system miss -> 0. Needed = strlen(nickname (ISystem vslot 2)) + 1; MaxLen <
// Needed -> return Needed; else strcpy and 0.
SERVER_API unsigned int __cdecl GetSystemNickname(char* OutBuffer, unsigned int MaxLen, const unsigned int& SystemId) {
    Universe::ISystem const* Sys = Universe::get_system(SystemId);
    if (Sys == nullptr)
        return 0;
    unsigned int Needed = static_cast<unsigned int>(strlen(Sys->Nickname)) + 1;
    if (MaxLen < Needed)
        return Needed;
    memcpy(OutBuffer, Sys->Nickname, Needed);
    return 0;
}

// pub::GetBaseNickname -- retail 0x7ebc0 (ordinal 154), .text:06d5ebc0..06d5ec13.
// The same over get_base and IBase vslot 1 (called twice by retail, same string).
SERVER_API unsigned int __cdecl GetBaseNickname(char* OutBuffer, unsigned int MaxLen, const unsigned int& BaseId) {
    Universe::IBase const* Base = Universe::get_base(BaseId);
    if (Base == nullptr)
        return 0;
    unsigned int Needed = static_cast<unsigned int>(strlen(Base->Nickname)) + 1;
    if (Needed > MaxLen)
        return Needed;
    memcpy(OutBuffer, Base->Nickname, Needed);
    return 0;
}

// pub::GetBaseStridName -- retail 0x7ec20 (ordinal 156): get_base miss -> -2 (OutStrid
// untouched); else OutStrid = IBase vslot 2 (strid_name), 0.
SERVER_API int __cdecl GetBaseStridName(unsigned int& OutStrid, const unsigned int& BaseId) {
    Universe::IBase const* base = Universe::get_base(BaseId);
    if (base) {
        OutStrid = base->StridName;
        return 0;
    }
    return -2;
}

// pub::GetLocations -- retail 0x7ec50 (ordinal 207), .text:06d5ec50..06d5ed08.
// BaseDataList_get()->get_base_data(*BaseId) miss -> OutCount = 0, return 0. Else
// all seven fixed room names are tried (the loop never stops early): MakeLocationID,
// and if BaseDataList_get()->get_room_data(room) exists AND there is still room in the
// output, append it. OutCount = appended, return 0.
SERVER_API int __cdecl GetLocations(const unsigned int& BaseId, unsigned int* const OutLocations, unsigned int MaxCount, unsigned int& OutCount) {
    static char const* const RoomNames[7] = {
        "Bar", "Deck", "Trader", "Equipment", "Cityscape", "ShipDealer", "Planetside"
    };
    unsigned int Count = 0;
    if (BaseDataList_get()->get_base_data(BaseId) == nullptr) {
        OutCount = Count;
        return 0;
    }
    for (unsigned int Index = 0; Index < 7; ++Index) {
        unsigned int RoomId = MakeLocationID(BaseId, RoomNames[Index]);
        if (BaseDataList_get()->get_room_data(RoomId) != nullptr && Count < MaxCount) {
            OutLocations[Count] = RoomId;
            ++Count;
        }
    }
    OutCount = Count;
    return 0;
}

// pub::GetLocationNickname -- retail 0x7ed10 (ordinal 206), .text:06d5ed10..06d5ed5f.
// get_room_data(*RoomId) is dereferenced WITHOUT a null test (06d5ed29 reads the
// nickname at RoomData+0xc straight away). Needed = strlen + 1; Needed > MaxLen ->
// return Needed; else strcpy and 0.
SERVER_API unsigned int __cdecl GetLocationNickname(char* OutBuffer, unsigned int MaxLen, const unsigned int& RoomId) {
    RoomData* Room = BaseDataList_get()->get_room_data(RoomId);
    unsigned int Needed = static_cast<unsigned int>(strlen(Room->Nickname)) + 1;
    if (Needed > MaxLen)
        return Needed;
    memcpy(OutBuffer, Room->Nickname, Needed);
    return 0;
}

// pub::GetBases -- retail 0x7ed60 (ordinal 157), .text:06d5ed60..06d5edbe.
// Walks EVERY base (GetFirstBase / GetNextBase to the end, leaving Common's iterator
// exhausted); a base whose system (IBase vslot 4) matches is appended while Count <
// MaxCount. OutCount = Count, return 0.
SERVER_API int __cdecl GetBases(const unsigned int& SystemId, unsigned int* const OutBases, unsigned int MaxCount, unsigned int& OutCount) {
    unsigned int Count = 0;
    Universe::IBase const* Base = Universe::GetFirstBase();
    while (Base != nullptr) {
        if (Base->SystemId == SystemId && Count < MaxCount) {
            OutBases[Count] = Base->NicknameId;
            ++Count;
        }
        Base = Universe::GetNextBase();
    }
    OutCount = Count;
    return 0;
}

SERVER_API int __cdecl GetSystem(unsigned int& OutSystemId, const unsigned int& BaseId) {
    OutSystemId = 0;
    Universe::IBase const* base = Universe::get_base(BaseId);
    if (base) {
        OutSystemId = base->SystemId;
        return 0;
    }
    return -2;
}

SERVER_API unsigned int __cdecl GetNicknameId(const char* Nickname) {
    return CreateID(Nickname);
}

SERVER_API unsigned short __cdecl MakeId(const char* Nickname) {
    return ::MakeId(Nickname);
}

// pub::DebugPrint -- retail 0x7ef40 (ordinal 94), .text:06d5ef40..06d5ef95.
// Level 1 -> FDUMP warning (pubMisc.cpp line 0x3c5), level 2 -> FDUMP error (line
// 0x3c6), any other level prints nothing.
SERVER_API void __cdecl DebugPrint(const char* Msg, int Level) {
    if (Level == 1) {
        FDUMP(0x100002, "%s(%d) : *** WARNING: pub: %s",
              "E:\\FL\\Scratch\\Source\\Server\\pubMisc.cpp", 0x3c5, Msg);
    } else if (Level == 2) {
        FDUMP(0x100001, "%s(%d) : *** ERROR: pub: %s",
              "E:\\FL\\Scratch\\Source\\Server\\pubMisc.cpp", 0x3c6, Msg);
    }
}

// pub::GetNavMapScale -- retail 0x7f000 (ordinal 226), .text:06d5f000..06d5f033.
// OutScale = 0 first (06d5f00e: MOV dword [ESI],0); get_system miss -> -1; else
// OutScale = ISystem vslot 20 (+0x50, the navmapscale float), 0.
SERVER_API int __cdecl GetNavMapScale(unsigned int SystemId, float& OutScale) {
    OutScale = 0.0f;
    Universe::ISystem const* Sys = Universe::get_system(SystemId);
    if (Sys == nullptr)
        return -1;
    OutScale = Sys->NavMapScale;
    return 0;
}

// DAT_06d8d900 (.data, zero-initialised) -- the free-terminal mailbox.
// FUN_06d00830 (the launch-position helper) writes -1 here (.text:06d00a9f),
// dispatches controller event 0x32 through FUN_06d43650 (.text:06d00ab7) so a
// mission script can answer synchronously with pub::ReportFreeTerminal, and
// reads the answer back at .text:06d00abc (`CMP EAX,-1`). FUN_06d00830 is
// Server_GetLaunchPosition (server_classes.cpp; extern in server_gameobject.h).
// Defined at global scope (not pub::) to match that extern declaration.
} // namespace pub
int GReportedFreeTerminal = 0;
namespace pub {

// pub::ReportFreeTerminal -- retail .text:06cffd10 (RVA 0x1fd10, ordinal 363),
// four instructions: store the SECOND argument in DAT_06d8d900, return 0. The
// first argument is never read. (Corrected 2026-09-24: the port dropped the
// store.)
SERVER_API int __cdecl ReportFreeTerminal(unsigned int P1, int P2) {
    GReportedFreeTerminal = P2;   // .text:06cffd14
    return 0;
}

// pub::DLL_LoadLibrary -- retail 0x06d43c40 (ordinal 93), cdecl, 0x208 bytes of locals.
//   06d43c44  if (DllPath[0] == '\\' || DllPath[1] == ':')  byte-copy DllPath as is
//   06d43c57  else _makepath(tmp, NULL, &DAT_06d8f654, DllPath, NULL);
//                  _fullpath(full, tmp, 0x104)
//             LoadLibraryA(full); on NULL FDUMP(0x100002, "...failed to load mission
//             DLL '%s'", dlls.cpp, 0x1de, full); return the module.
// DAT_06d8f654 is the [Initial * DLLs] `path` (GDllPath, IServerImpl.cpp).

SERVER_API HINSTANCE __cdecl DLL_LoadLibrary(const char* DllPath) {
    char FullPath[0x104];
    if (DllPath[0] == '\\' || DllPath[1] == ':') {
        strcpy(FullPath, DllPath);
    } else {
        char Joined[0x104];
        _makepath(Joined, nullptr, GDllPath, DllPath, nullptr);
        _fullpath(FullPath, Joined, 0x104);
    }
    HMODULE Module = LoadLibraryA(FullPath);
    if (Module == nullptr) {
        FDUMP(0x100002, "%s(%d) : *** WARNING: failed to load mission DLL '%s'\n",
              "E:\\FL\\Scratch\\Source\\Server\\dlls.cpp", 0x1de, FullPath);
    }
    return Module;
}

// ===========================================================================
// pub:: utility functions — line-by-line verified against Server.dll.c
// ===========================================================================

// pub::GetFullHealth — retail 0x7e1f0 (ordinal 182)
// Assembly: Archetype::GetShip → FLD [EAX+0x1c] (Root::HitPts) → ftol → store
// Returns: 0 success, -2 ship archetype not found, -4 invalid obj_id
SERVER_API int __cdecl GetFullHealth(const unsigned int& ObjId, unsigned int& OutHealth) {
    int Result = -4;
    if ((ObjId & 0x3fffffff) != 0) {
        Archetype::Ship* ShipArch = Archetype::GetShip(ObjId);
        if (ShipArch != nullptr) {
            // .text:06d5e216: FLD [EAX+0x1c] / CALL ftol / MOV [EDX],EAX -- ftol truncates
            // to 64 bits and the low dword is stored.
            OutHealth = static_cast<unsigned int>(static_cast<long long>(ShipArch->HitPts));
            return 0;
        }
        Result = -2;
    }
    return Result;
}

// pub::GetShipArchSTRID — retail 0x7e230 (ordinal 269)
// Assembly: Archetype::GetShip → MOV EAX,[EAX+0x14] (Root::IdsName) → conditional return
// Return formula: (-(uint)((strid & 0x3fffffff) != 0) & 2) - 2
//   strid valid → 0, strid zero/invalid → -2, obj_id bad → -4
SERVER_API int __cdecl GetShipArchSTRID(const unsigned int& ObjId, unsigned int& OutStrid) {
    if ((ObjId & 0x3fffffff) != 0) {
        Archetype::Ship* ShipArch = Archetype::GetShip(ObjId);
        unsigned int StridValue = (unsigned int)ShipArch->IdsName;
        OutStrid = StridValue;
        // Retail branchless: NEG/SBB/AND/ADD sequence
        return (-(int)((StridValue & 0x3fffffff) != 0) & 2) - 2;
    }
    return -4;
}

// pub::GetType — retail 0x7e330 (ordinal 294), .text:06d5e330..06d5e364, all 18 insns:
//   MOV ECX,[ESP+4] / MOV ECX,[ECX] / TEST ECX,0x3fffffff / MOV EAX,-4 / JZ ret
//   PUSH ECX / CALL [COMMON.DLL::Archetype::GetShip] / ADD ESP,4
//   TEST EAX,EAX / MOV ECX,-2 / JZ ret_ecx
//   MOV EDX,[EAX+0x10] (Root::ObjectType) / MOV EAX,[ESP+8] / MOV [EAX],EDX / XOR EAX,EAX / RET
// GetSolarType below is the same body over Archetype::GetSolar.
SERVER_API int __cdecl GetType(const unsigned int& ObjId, unsigned int& OutType) {
    int Result = -4;
    if ((ObjId & 0x3fffffff) != 0) {
        Archetype::Ship* ShipArch = Archetype::GetShip(ObjId);
        if (ShipArch != nullptr) {
            OutType = ShipArch->ObjectType;
            return 0;
        }
        Result = -2;
    }
    return Result;
}

// pub::GetSolarType — retail 0x7e370 (ordinal 279)
// Assembly: Archetype::GetSolar → MOV EDX,[EAX+0x10] (Root::ObjectType) → store
// Returns: 0 success, -2 solar archetype not found, -4 invalid obj_id
SERVER_API int __cdecl GetSolarType(const unsigned int& ObjId, unsigned int& OutType) {
    int Result = -4;
    if ((ObjId & 0x3fffffff) != 0) {
        Archetype::Solar* SolarArch = Archetype::GetSolar(ObjId);
        if (SolarArch != nullptr) {
            OutType = SolarArch->ObjectType;
            return 0;
        }
        Result = -2;
    }
    return Result;
}

// pub::GetMaxHitPoints — retail 0x7e3b0 (ordinal 211)
// Assembly: Archetype::GetSolar → FLD [EAX+0x1c] (Root::HitPts) → ftol → store
// Returns: 0 success, -2 solar archetype not found, -4 invalid obj_id
SERVER_API int __cdecl GetMaxHitPoints(const unsigned int& ObjId, int& OutHP) {
    int Result = -4;
    if ((ObjId & 0x3fffffff) != 0) {
        Archetype::Solar* SolarArch = Archetype::GetSolar(ObjId);
        if (SolarArch != nullptr) {
            // .text:06d5e3d6: FLD [EAX+0x1c] / CALL ftol -- 64-bit truncation, low dword stored.
            OutHP = static_cast<int>(static_cast<long long>(SolarArch->HitPts));
            return 0;
        }
        Result = -2;
    }
    return Result;
}

// pub::GetGoodProperties — retail 0x7e3f0 (ordinal 187)
// Assembly: Good2Arch → Archetype::GetEquipment →
//   MOV EDX,[EAX+0x5c] (Equipment::Volume -> first float&)
//   MOV EDX,[EAX+0x20] (Root::Mass -> second float&)
// Returns: 0 success, -2 equipment archetype not found, -4 invalid good_id
SERVER_API int __cdecl GetGoodProperties(const unsigned int& GoodId, float& OutVolume, float& OutMass) {
    int Result = -4;
    if ((GoodId & 0x3fffffff) != 0) {
        unsigned int ArchId = Good2Arch(GoodId);
        Archetype::Equipment* Equip = Archetype::GetEquipment(ArchId);
        if (Equip != nullptr) {
            OutVolume = Equip->Volume;
            OutMass = Equip->Mass;
            return 0;
        }
        Result = -2;
    }
    return Result;
}

// pub::TranslateSystemToMsgIdPrefix — retail RVA 0x7E160, ordinal 468
// .text:06d5e160: result -4; Universe::get_system; null -> -4. Then ISystem slot 1
// (GetMsgIdPrefix, SYSTEM's CommReferrable half) and TString<64>::operator=(char const*)
// inlined on its Data (.text:06d5e17f-06d5e1d4), as the Good and Ship siblings below.
// Returns: 0 = success (including empty prefix), -4 = system not found.
SERVER_API int __cdecl TranslateSystemToMsgIdPrefix(unsigned int SystemId, TString<64>& OutPrefix) {
    Universe::ISystem const* Sys = Universe::get_system(SystemId);
    if (Sys == nullptr) {
        return -4;
    }
    OutPrefix = Sys->GetMsgIdPrefix().Data;
    return 0;
}

// pub::SinglePlayer — retail 0x7e1e0 (ordinal 451)
// Retail: delegates to global ::SinglePlayer() (Common.dll export, reads GbSinglePlayer)
SERVER_API bool __cdecl SinglePlayer() {
    return ::SinglePlayer();
}

// pub::GetLoadout — retail 0x7e560 (ordinal 201)
// Retail: Loadout::Get → vector<EquipDesc> copy with capacity reuse / realloc
// EquipDescVector is VC6-style (AllocPad, Begin, End, Cap); sizeof(EquipDesc)=0x20
// Three branches: (1) srcCount<=dstSize: copy+shrink, (2) srcCount<=dstCap: copy+extend,
// (3) reallocate. Retail also has a self-assignment test (.text:06d5e58d: param_1 ==
// &Map->Items -> return 0); in the port Map::Items is a std::vector<EquipDesc> owned by
// Common's loadout table, never an EquipDescVector, so the two cannot alias.
// Returns: 0 success, -2 loadout not found, -4 invalid loadout_id
SERVER_API int __cdecl GetLoadout(EquipDescVector& OutLoadout, const unsigned int& LoadoutId) {
    if (LoadoutId == 0) {
        return -4;
    }
    Loadout::Map const* LoadoutMap = Loadout::Get(LoadoutId);
    if (LoadoutMap == nullptr) {
        return -2;
    }
    // Source items from Loadout::Map's std::vector<EquipDesc>
    EquipDesc* SrcBegin = const_cast<EquipDesc*>(LoadoutMap->Items.data());
    unsigned int SrcCount = (unsigned int)LoadoutMap->Items.size();
    // Destination EquipDescVector state
    EquipDesc* DstBegin = OutLoadout.Begin;
    unsigned int DstSize = 0;
    unsigned int DstCap = 0;
    if (DstBegin != nullptr) {
        DstSize = (unsigned int)(OutLoadout.End - DstBegin);
        DstCap = (unsigned int)(OutLoadout.Cap - DstBegin);
    }
    if (SrcCount <= DstSize) {
        // Branch 1: source fits within current destination size — copy and shrink
        for (unsigned int I = 0; I < SrcCount; I++) {
            DstBegin[I] = SrcBegin[I];
        }
        if (SrcCount > 0) {
            OutLoadout.End = DstBegin + SrcCount;
        } else {
            OutLoadout.End = DstBegin;
        }
        return 0;
    }
    if (SrcCount <= DstCap) {
        // Branch 2: source fits within destination capacity — copy existing, construct new
        for (unsigned int I = 0; I < DstSize; I++) {
            DstBegin[I] = SrcBegin[I];
        }
        // Placement-new construct the additional items beyond old size
        for (unsigned int I = DstSize; I < SrcCount; I++) {
            new (&DstBegin[I]) EquipDesc(SrcBegin[I]);
        }
        OutLoadout.End = DstBegin + SrcCount;
        return 0;
    }
    // Branch 3 (.text:06d5e6d5..06d5e71a): need more capacity. FUN_06d5ffb0 (operator
    // delete) on the old Begin with no null test, operator new(SrcCount * 0x20), Begin =
    // the new block, then vector<EquipDesc>::_Ucopy of the source into it and End = Cap =
    // its result. SrcCount > DstCap >= 0 here, so the block is never empty. ::operator
    // new / ::operator delete: the same pair every other EquipDescVector buffer uses,
    // since ~EquipDescVector (Common 0x06269110) releases Begin with ::operator delete.
    ::operator delete(DstBegin);
    EquipDesc* NewBuf = static_cast<EquipDesc*>(::operator new(SrcCount * sizeof(EquipDesc)));
    OutLoadout.Begin = NewBuf;
    for (unsigned int I = 0; I < SrcCount; I++) {
        new (&NewBuf[I]) EquipDesc(SrcBegin[I]);
    }
    OutLoadout.End = NewBuf + SrcCount;
    OutLoadout.Cap = NewBuf + SrcCount;
    return 0;
}

} // namespace pub -- the save-file family below is not in pub::

// ===========================================================================
// The save-file writer family: retail FUN_06d460f0, FUN_06d46370, FUN_06d46930
// and FUN_06d4ccd0, all read off EXE\Disasm\Server.dll.txt (there is no Ghidra
// decompilation of FUN_06d4ccd0 at all -- 2123 instructions, complexity "huge").
//
// REWRITTEN 2026-09-17 (wave 17). The previous port of FUN_06d4ccd0 was built
// from a `.c`-era reading and named four keys retail does not write while
// missing eight that it does. Every key name, every format string and every
// source field below is quoted from the listing at the address in its comment.
// ===========================================================================

// ---------------------------------------------------------------------------
// Server_GetAccountsRoot -- retail FUN_06d460f0 (.text:06d460f0 .. .text:06d46144).
//
//   .text:06d460fe  CALL [->COMMON.DLL::GetUserDataPath]     ; into a 0x104 local
//   .text:06d46107  CALL [->COMMON.DLL::IsMPServer]
//   .text:06d46113  PUSH .data:s_Accts\MultiPlayer_06d8d308  ; = "Accts\\MultiPlayer"
//   .text:06d4611f  PUSH .data:s_Accts_06d8d200              ; = "Accts"
//   .text:06d46130  CALL [->MSVCRT.DLL::_makepath]
//   .text:06d46139  MOV  EAX,.data:DAT_06d90158              ; returns the GLOBAL buffer
//
// Retail really does hand back a pointer to one static 0x104 buffer, which is
// why the serializer can use the result as _makepath's `dir` and then overwrite
// the local it built the user-data path in. Kept as-is.
// ---------------------------------------------------------------------------
static char GAccountsRoot[260];

// Retail DAT_06d902f8, see server_savefile.h.
char GServerLastSavePath[0x104];

// De-staticed 2026-09-24 (claude:srv-save): the save wrappers in
// server_savegame.cpp use it too (declared in server_savefile.h).
char const* Server_GetAccountsRoot() {
    char UserDataPath[260];
    GetUserDataPath(UserDataPath);                                          // .text:06d460fe
    _makepath(GAccountsRoot, nullptr, UserDataPath,
              IsMPServer() ? "Accts\\MultiPlayer" : "Accts", nullptr);
    return GAccountsRoot;
}

// ---------------------------------------------------------------------------
// Server_AccountDirName -- retail FUN_06d46370 (.text:06d46370 .. .text:06d464ee),
// the MP account-directory name: `sprintf(out, "%02x-%08x", wcslen(name), crc)`
// (.rdata:s_%02x-%08x_06d6aec0, pushed at .text:06d464d3).
//
// The CRC is the same Rocksoft-model engine Common.dll's CFLIDMaker uses, but a
// SECOND instance with its own parameters, configured by LAB_06d46250:
//   .text:06d4625c  MOV dword [.data:DAT_06d90138],0x20      ; register width  = 32
//   .text:06d46266  MOV dword [.data:DAT_06d9013c],ESI       ; ESI = 0x1f, crc width = 31
//   .text:06d4626c  MOV dword [.data:DAT_06d90140],0x8005    ; polynomial
//   .text:06d46276  MOV dword [.data:DAT_06d90144],EBP       ; EBP = 0, XorIn  = 0
//   .text:06d4627c  MOV dword [.data:DAT_06d90148],EBP       ;          XorOut = 0
//   .text:06d46282  MOV byte  [.data:DAT_06d9014c],0x1       ; reflect-in
//   .text:06d46289  MOV byte  [.data:DAT_06d9014d],0x1       ; table path
// The table build at .text:06d462a2 .. .text:06d46356 is the textbook reflected
// MSB-first table for that width/poly, and the per-byte step the hash loop takes
// with both flags set is the single branch at .text:06d4640b:
//   ECX = (crc & 0xff) ^ byte ; ECX = Table[ECX] ; crc = (crc >> 8) ^ ECX
//
// The FINALIZE at .text:06d4645a is where this differs from CreateID and must not
// be copied from it. With reflect-in set (.text:06d46469 JNZ) retail runs
//   .text:06d4648a  EBX = 0x20 (register width)
//   .text:06d46490  EAX = 1 << (0x1f - 1)          ; 0x40000000
//   .text:06d4649f  ESI = (EAX + EAX - 1) | 1      ; 0x7fffffff, the width mask
//   .text:06d464a8  ESI &= (XorOut ^ crc)
//   .text:06d464b0  loop: EDI |= (ESI & 0xff) << (EBX - EDX); EDX += 8; ESI >>= 8
// and then prints EDI **unchanged** -- no `>> (32 - width)` fold-back and no
// 0x80000000 top bit, both of which CFLIDMaker::FinalizeStaticId does apply. So
// the account hash is byteswap32(crc & 0x7fffffff), not a CreateID value.
//
// The input is the LOWERCASED WIDE string, hashed two bytes per code unit:
// .text:06d4637a _wcsdup, .text:06d46383 _wcslwr, .text:06d4639c
// `LEA ECX,[EAX + EAX*0x1]` = 2 * wcslen as the byte count.
// ---------------------------------------------------------------------------
namespace {
    const unsigned int AccountCrcWidth = 31;
    const unsigned int AccountCrcPoly = 0x8005;
    const unsigned int AccountCrcRegisterWidth = 32;

    unsigned int GAccountCrcTable[256];
    bool bAccountCrcTableReady = false;

    // .text:06d462b4 and .text:06d46317 -- the same bit-overlay reflect
    // CFLIDMaker uses, spelled out again here because this engine is a separate
    // instance in Server.dll and must not reach into Common.dll's private copy.
    unsigned int AccountCrcReflect(unsigned int Value, unsigned int BitCount) {
        unsigned int Result = Value;
        unsigned int Source = Value;
        for (unsigned int BitIndex = 0; BitIndex < BitCount; ++BitIndex) {
            unsigned int Mask = 1u << ((BitCount - BitIndex) - 1u);
            if ((Source & 1u) == 0)
                Result &= ~Mask;
            else
                Result |= Mask;
            Source >>= 1;
        }
        return Result;
    }

    void AccountCrcBuildTable() {
        if (bAccountCrcTableReady)
            return;
        unsigned int const TopBit = 1u << (AccountCrcWidth - 1u);
        unsigned int const Mask = ((TopBit - 1u) * 2u + 1u) | 1u;
        for (unsigned int Index = 0; Index < 256; ++Index) {
            // .text:06d462ac -- reflect-in reflects the table index byte first.
            unsigned int Work = AccountCrcReflect(Index, 8) & 0xffu;
            // .text:06d462e3  SHL EAX,CL with CL = width - 8.
            Work <<= (AccountCrcWidth - 8u);
            for (int Bit = 0; Bit < 8; ++Bit) {
                if ((Work & TopBit) != 0)
                    Work = (Work << 1) ^ AccountCrcPoly;
                else
                    Work <<= 1;
            }
            // .text:06d4630f -- reflect-out folds the entry back.
            Work = AccountCrcReflect(Work, AccountCrcWidth);
            GAccountCrcTable[Index] = Work & Mask;
        }
        bAccountCrcTableReady = true;
    }
}

// Server_AccountCrc -- the reset/update/finalize sequence every user of the
// DAT_06d8fd38 engine runs: `DAT_06d90150 = DAT_06d90144` inline (XorIn = 0),
// FUN_06d46850 (update over a byte block; FUN_06d466f0 is the same step for one
// byte) and FUN_06d467b0 (finalize). Only the reflect-in + table branch of those
// methods is live: DAT_06d9014c / DAT_06d9014d are written solely by the static
// initializer at .text:06d46282 / .text:06d46289 (both 1), so the other three
// per-byte branches and the non-reflected finalize are unreachable in retail.
// Declared in server_savefile.h.
unsigned int Server_AccountCrc(unsigned char const* Bytes, unsigned int ByteCount) {
    AccountCrcBuildTable();

    unsigned int Crc = 0; // XorIn, .text:06d46276
    for (unsigned int I = 0; I < ByteCount; ++I)
        Crc = GAccountCrcTable[(Crc & 0xffu) ^ Bytes[I]] ^ (Crc >> 8);

    // FUN_06d467b0 reflect path (= .text:06d4649f / .text:06d464a8 inlined in
    // FUN_06d46370): mask with XorOut (0), then the byte-gather loop.
    unsigned int const TopBit = 1u << (AccountCrcWidth - 1u);
    unsigned int const Mask = ((TopBit - 1u) * 2u + 1u) | 1u;
    unsigned int Remain = Crc & Mask;
    unsigned int Folded = 0;
    for (unsigned int Shift = 8; Shift <= AccountCrcRegisterWidth; Shift += 8) {
        Folded |= (Remain & 0xffu) << (AccountCrcRegisterWidth - Shift);
        Remain >>= 8;
    }
    return Folded;
}

// De-staticed 2026-09-24 (claude:srv-save), declared in server_savefile.h.
void Server_AccountDirName(char* Out, wchar_t const* AccountName) {
    // .text:06d4637a -- no null check in retail; _wcsdup/wcslen take the name as is.
    unsigned int const Length = static_cast<unsigned int>(wcslen(AccountName));

    // .text:06d4637a / .text:06d46383 -- _wcsdup then _wcslwr. towlower on a
    // local copy is the same transform without the CRT allocation.
    std::wstring Lowered;
    Lowered.reserve(Length);
    for (unsigned int I = 0; I < Length; ++I)
        Lowered.push_back(static_cast<wchar_t>(towlower(AccountName[I])));

    // .text:06d4639c -- 2 * wcslen bytes of the lowercased copy.
    unsigned int const Folded = Server_AccountCrc(
        reinterpret_cast<unsigned char const*>(Lowered.c_str()), Length * 2u);

    sprintf(Out, "%02x-%08x", Length, Folded);
}

// ---------------------------------------------------------------------------
// Server_WriteCostume -- retail FUN_06d46930 (.text:06d46930 .. .text:06d46aab).
//
// Four identical blocks, one per body part, each `find_bodypart(TypeId, PartId)`
// then `bodypart::get_name()` then a "<prefix><part>" key:
//   part ids   .text:06d46949  MOV ESI,[EAX + 0x4]   -> PartIds[1], TypeId 0, "%sbody"
//              .text:06d469a1  MOV EDI,[EAX]         -> PartIds[0], TypeId 1, "%shead"
//              .text:06d469f8  MOV EDI,[EAX + 0x8]   -> PartIds[2], TypeId 2, "%slefthand"
//              .text:06d46a50  MOV EDI,[EAX + 0xc]   -> PartIds[3], TypeId 3, "%srighthand"
//   formats    .rdata:s_%sbody_06d6aef0, s_%shead_06d6aee8,
//              s_%slefthand_06d6aedc, s_%srighthand_06d6aed0
// A zero part id, a missed find_bodypart or a null name skips the line entirely
// (.text:06d4694f / .text:06d4695d / .text:06d4696b and the three copies of it).
//
// ARGUMENTS. `RET 0xc` is retail's exit, so three dword stack arguments and no
// thiscall `this` -- .text:06d46930 `MOV EAX,dword ptr [ESP + 0xc]` takes the
// THIRD one as the Costume. The first is not a FILE*: .text:06d4d3ef
// `LEA EAX,[ESP + 0x40]` resolves to the serializer's own writer object, and
// .text:06d4698c `MOV ECX,[ESP + 0x120]` / `MOV EAX,[ECX]` / .text:06d46997
// `ff5014 CALL dword ptr [EAX + 0x14]` dispatches slot +0x14 on it, which is
// WriteString. FIXED 2026-09-17 (wave 18): that object is CIniFileSave now
// (renamed SaveGame_Writer 2026-09-25 after its RTTI, .?AUSaveGame_Writer@@).
//
// THE PREFIX. There is no "com_body" string anywhere in Server.dll -- searching
// the mapped image for it returns nothing. The keys are built at run time from
// .rdata:s_%sbody_06d6aef0 and its three siblings with a prefix argument, which
// is "" (.rdata:DAT_06d66ee4, four zero bytes) for the worn costume and "com_"
// (.rdata:06d6b12c) for the comm costume.
// ---------------------------------------------------------------------------
static void Server_WriteCostume(ISave& Save, char const* Prefix, Costume const& Cost) {
    CostumeDescriptions const* Descriptions = GetCostumeDescriptions();
    if (Descriptions == nullptr)
        return;

    struct FCostumePart {
        int TypeId;
        int PartSlot;
        char const* Format;
    };
    static const FCostumePart Parts[4] = {
        { 0, 1, "%sbody" },
        { 1, 0, "%shead" },
        { 2, 2, "%slefthand" },
        { 3, 3, "%srighthand" },
    };

    for (int I = 0; I < 4; ++I) {
        unsigned int const PartId = Cost.PartIds[Parts[I].PartSlot];
        if (PartId == 0)
            continue;
        char const* PartName = Descriptions->get_part_name(Parts[I].TypeId, PartId);
        if (PartName == nullptr)
            continue;
        char Key[128];
        sprintf_s(Key, Parts[I].Format, Prefix);
        Save.WriteString(Key, PartName);
    }
}

// ---------------------------------------------------------------------------
// Server_FormatMissionLogEntry -- retail FUN_06d4cc20
// (.text:06d4cc20 .. .text:06d4ccc0).
//
// ARGUMENTS. `RET 0x8` at .text:06d4ccc0 -- two dword stack arguments, plus an
// ECX that is saved at .text:06d4cc2f and only ever handed back to the recursive
// call at .text:06d4cca5. It is never dereferenced, so this port drops it.
//   param_1 [ESP+0x14]  .text:06d4cc3f -- the destination char buffer
//   param_2 [ESP+0x18]  .text:06d4cc24 -- the record
//
// WHAT IT IS. An earlier wave filed this function as the cheat-report gate. It
// is nothing of the kind: it touches no PlayerData slot at all, and it is a
// recursive list formatter for one FmtStr.
//
//   .text:06d4cc28  MOV  EAX,dword ptr [ESI]        ; FmtStr::Head
//   .text:06d4cc35  MOV  EAX,dword ptr [EAX]        ; Val::Next -- count the chain
//   .text:06d4cc3c  MOV  EAX,dword ptr [ESI + 0x4]  ; FmtStr::MadLibId
//   .text:06d4cc4b  PUSH .rdata:s_%u,_%u_06d6b628   ; "%u, %u"      <- id, count
//   .text:06d4cc60  MOVZX ECX,byte ptr [ESI + 0xa]  ; Val::Flags
//   .text:06d4cc64  MOVZX EDX,word ptr [ESI + 0x8]  ; Val::TypeTag
//   .text:06d4cc68  MOV  EAX,dword ptr [ESI + 0x4]  ; Val::Data
//   .text:06d4cc7c  PUSH .rdata:s_,_%u,_%u,_%u_06d6b618  ; ", %u, %u, %u"
//   .text:06d4cc87  CMP  word ptr [ESI + 0x8],0x21  ; nested FmtStr?
//   .text:06d4cc9c  MOV  byte ptr [ECX],0x2c        ; a BARE ',' -- no space
//   .text:06d4cca9  CALL FUN_06d4cc20               ; recurse on Val::Data
//   .text:06d4ccb2  MOV  ESI,dword ptr [ESI]        ; Val::Next
//
// The strlen before each append is the inlined `EDI = buf; SCASB.REPNE; NOT ECX;
// DEC ECX; ADD ECX,EBX` at .text:06d4cc6e .. .text:06d4cc7a and again at
// .text:06d4cc8e .. .text:06d4cc9a.
//
// VERIFIED ON DATA, not just on the listing. The fifty saved character files on
// this machine hold 3234 `log =` lines. 3231 of them match
// `id, count, {data, tag, flags} x count` exactly. The other three match only if
// the 0x21 rule is applied -- and in those three the separator before the nested
// record really is a bare comma with no space, exactly as .text:06d4cc9c writes
// it (`..., 33, 0,327682, 2, ...`).
//
// THE BUFFER. The caller's destination is the 0x1000-byte stack buffer of
// FUN_06d4ccd0 at [ESP + 0x6b8] (.text:06d4de58 `LEA EDX,[ESP + 0x6bc]` after one
// push); the next local the function addresses is [ESP + 0x16b8]. Retail writes
// it with plain sprintf at the current end and checks nothing -- neither the
// buffer size, nor a null Val::Data on a 0x21 node, nor the chain against the
// count it took first. The port does the same.
// ---------------------------------------------------------------------------
static void Server_FormatMissionLogEntry(char* Out, FmtStr const& Entry) {
    // .text:06d4cc28 .. .text:06d4cc3a -- count the chain into EDI.
    int Count = 0;
    for (FmtStr::Val const* Node = Entry.Head; Node != nullptr; Node = Node->Next)
        ++Count;

    // .text:06d4cc3c .. .text:06d4cc51 -- sprintf(Out, "%u, %u", MadLibId, Count),
    // written at Out itself.
    sprintf(Out, "%u, %u", Entry.MadLibId, Count);

    // .text:06d4cc53 ESI = Head; .text:06d4cc58 TEST EDI,EDI / JLE -- the loop runs
    // Count times (06d4ccb4 DEC / 06d4ccb9 JNZ), following Val::Next unchecked.
    FmtStr::Val const* Node = Entry.Head;
    for (int Remaining = Count; Remaining > 0; --Remaining) {
        // .text:06d4cc60 .. .text:06d4cc82 -- appended at strlen(Out).
        sprintf(Out + strlen(Out), ", %u, %u, %u",
                Node->Value,   // the raw payload dword, whatever the tag
                (unsigned int)Node->TypeTag,
                (unsigned int)Node->Flags);

        // .text:06d4cc87 -- 0x21 is the tag common_text.h already documents as
        // "nested FmtStr", so Val::Subtree is live and the record recurses:
        // .text:06d4cc9c writes a bare ',' at the end, .text:06d4cca2 recurses on
        // the byte after it.
        if (Node->TypeTag == 0x21) {
            char* const Tail = Out + strlen(Out);
            *Tail = ',';
            Server_FormatMissionLogEntry(Tail + 1, *Node->Subtree);
        }
        Node = Node->Next;
    }
}

// ---------------------------------------------------------------------------
// Server_SaveSinglePlayerState -- retail FUN_06d447c0 (.text:06d447c0 ..
// .text:06d44809), cdecl (ISave*), plain RET. The writer half of FUN_06d44810
// (Server_LoadSinglePlayerState, IServerImpl.cpp); its only caller is the
// serializer below, in single player (.text:06d4def8).
//   06d447ce  Writer->vtbl+0x04 (WriteSection)("Time")        .rdata:06d6ad78
//   06d447d1  FLD DAT_06d8fb28 / FADD DAT_06d8fb20 / FSTP dword -- the clock
//             (FUN_06d447b0's sum) narrowed to float
//   06d447ea  Writer->vtbl+0x08 (WriteFloat)("seconds", that) .rdata:06d6ad70
//   06d447ee  Reputation::Save(Writer)
//   06d447fb  Writer->vtbl+0x00 (GetFile)() ; FUN_06d43650(0x41, FILE*)
// ---------------------------------------------------------------------------
static void Server_SaveSinglePlayerState(ISave* Writer) {
    Writer->WriteSection("Time");
    Writer->WriteFloat("seconds", static_cast<float>(g_ElapsedTimeAccum + GServerTimeBase));
    ::Reputation::Save(Writer);
    Server_ControllerDispatch(0, pub::Controller::EMessage::SaveGame, Writer->GetFile());
}

// Server_CopyCostumeCounted -- the Costume copy the MP record refresh in
// FUN_06d4ccd0 inlines twice (.text:06d4e63c .. 06d4e68e and .text:06d4e6f6 ..
// 06d4e73e): the four part ids, the accessory count, then only `count`
// accessory ids under a SIGNED loop (TEST/JLE, INC/CMP/JL). Slots past the
// count keep what the destination held. Added 2026-09-24
// (claude-code:w5-server-06d4).
static void Server_CopyCostumeCounted(Costume& Dst, Costume const& Src) {
    Dst.PartIds[0] = Src.PartIds[0];
    Dst.PartIds[1] = Src.PartIds[1];
    Dst.PartIds[2] = Src.PartIds[2];
    Dst.PartIds[3] = Src.PartIds[3];
    Dst.AccessoryCount = Src.AccessoryCount;
    int const Count = static_cast<int>(Src.AccessoryCount);
    for (int I = 0; I < Count; ++I)
        Dst.AccessoryIds[I] = Src.AccessoryIds[I];
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// PlayerData_SerializeToFile -- retail FUN_06d4ccd0
// (.text:06d4ccd0 .. .text:06d4ea4f, listing lines 190145-192429).
//
// SIGNATURE. Ghidra's 15-parameter prototype is fiction. Every exit is `RET 0xc`
// (.text:06d4cd2a, .text:06d4cdc4, .text:06d4d013), so there are exactly THREE
// dword stack arguments plus the thiscall `this`, and the return is AL. The
// 0x2044 at .text:06d4ccec is __chkstk's frame size, not an argument. The three
// arguments are pinned by their uses:
//   param_1 [EBP+0x08] .text:06d4cede -- _makepath's `fname`, so a char* BASE
//                                        FILE NAME, not a path and not a FILE*
//   param_2 [EBP+0x0c] .text:06d4d03d -- WideToHex'd into the `description` key,
//                                        so a wchar_t*
//   param_3 [EBP+0x10] .text:06d4d074 -- printed with "%u" as `descrip_strid`
// and the autosave call site agrees: .text:06d4c534 pushes 0 (the strid), then
// the wide "<date> <time>" swprintf'd at .text:06d4c52b (the description), then
// a char buffer at EBP+0x20c (the file name).
//
// WHAT IT IS. A save that REFUSES TO RUN when the cheat check fires -- the gate
// and the writer are one function in retail, and the port had them as two that
// never called each other.
//
// STILL DEVIATING, every one an open TODO and not a decision:
//  (1) CLOSED 2026-09-17 (wave 18). The ini-writer object is SaveGame_Writer in
//      server_savefile.cpp now, the error latch at .text:06d4df04 is
//      SaveGame_Writer::HadWriteError, and the container below is Close().
//  (2) CLOSED 2026-09-17 (wave 18), AND THE OLD TEXT WAS WRONG ON ONE POINT.
//      The "FLS1" container is not multiplayer-only. Retail encodes on every
//      path: .text:06d4e0cf .. .text:06d4e14c runs FUN_06d48960 / FUN_06d48b60,
//      fopen "wb" (.rdata:06d6b4e4 = `77 62 00`), a bare fprintf of the four
//      bytes at .rdata:06d6b30c (`46 4c 53 31`), fwrite and fclose, with no
//      SinglePlayer test between the latch and it. What IS multiplayer-only is
//      the destination: outside single player the text goes to `<accts>\temp.fl`
//      (.rdata:s_%s\temp.fl_06d6b6f4, sprintf'd at .text:06d4ced2, chosen at
//      .text:06d4cf16) and .text:06d4e165 .. .text:06d4e183 unlinks the real
//      path and renames the temp onto it. The cipher is
//      out[i] ^= (("Gene"[i & 3] + (unsigned char)i) | 0x80), keyed on the first
//      four bytes of .rdata:06d6b318; it decodes forty-nine of the fifty saved
//      character files on this machine, all of them single-player.
//  (3) CLOSED 2026-09-24 (claude:srv-save-2). The MP base-record copy at
//      .text:06d4d4dd .. .text:06d4d617 and the base_hull_status /
//      base_collision_group / base_cargo / base_equip keys at .text:06d4dbb1 /
//      .text:06d4dc23 / .text:06d4dcf7 / .text:06d4dd79 are ported below. The
//      "+0x468 / +0x46c / +0x478" offsets are NODE offsets: the record starts at
//      node + 0x20c, so they are FSavedCharacter's BaseHullStatus (+0x25c),
//      BaseEquipment (+0x260) and BaseCollisionGroups (+0x26c), which the
//      parser already filled. The key is +0x64 alone (FUN_06d57da0 pairs it
//      with a default record from FUN_06d46ab0 for the map's unique insert).
//  (4) CLOSED 2026-09-17 (wave 18). The chain is a FmtStr's Val list and
//      PlayerData::mission_log is a std::list<FmtStr> now, so the key round
//      trips. See Server_FormatMissionLogEntry above.
//  (5) CLOSED 2026-09-24 (claude:srv-save-2): FUN_06d43650(4, ...) at
//      .text:06d4dee1 and FUN_06d447c0 at .text:06d4def8 are called below.
//  (6) CLOSED 2026-09-24 (claude:srv-charinfo): the slots retail dispatches
//      (ISystem vtable+0x8 at .text:06d4d455, IBase vtable+0x4 at .text:06d4d4ab /
//      .text:06d4db7f) are plain nickname getters, and the port reads that field.
//  (7) CLOSED 2026-09-24 (claude-code:w6-rpc): the MP position source is the
//      CRemotePhysicsController __RTDynamicCast at .text:06d4d672, ported below
//      against the class Common now really builds (src/include/common_server.h).
// ---------------------------------------------------------------------------
bool __cdecl PlayerData_SerializeToFile(PlayerSlot& Player, char const* FileName,
                                       wchar_t const* Description,
                                       unsigned int DescripStrId) {
    // No null test on FileName: the prologue (.text:06d4ccd0 .. 06d4cd01) goes
    // straight to the cheat gate, and the name only reaches _makepath
    // (.text:06d4cf00). Guard removed 2026-09-24 (claude-code:w5-server-06d4).

    // -----------------------------------------------------------------------
    // .text:06d4cd01 .. .text:06d4cdc4 -- THE CHEAT GATE. Retail runs it first
    // and returns AL = 0 (save refused) from inside it. PlayerData_CheckForCheats
    // in server_classes.cpp is the 1:1 port of this block, including the
    // already-reported latch at +0x34C; it was never called from here until now.
    // -----------------------------------------------------------------------
    if (PlayerData_CheckForCheats(Player))
        return false;

    // -----------------------------------------------------------------------
    // .text:06d4cdc7 .. .text:06d4cde2 -- a dead MP ship is put back to the
    // last-base loadout before it is written. SinglePlayer() skips it
    // (.text:06d4cdcb); otherwise FLD the hull (+0x268), FCOMP double
    // .rdata:DAT_06d65538 (PE bytes: eight zeros = 0.0), TEST AH,0x41 / JP skips
    // when C0 and C3 agree -- both clear (hull > 0) or both set (unordered) --
    // so the call runs for hull < 0 or hull == 0 only. Added 2026-09-24
    // (claude-code:w5-server-06d4).
    // -----------------------------------------------------------------------
    if (!SinglePlayer() && Player.relative_health <= 0.0f)
        PlayerData_ReloadShipData(Player);                                  // .text:06d4cde2

    // -----------------------------------------------------------------------
    // .text:06d4cde7 .. .text:06d4ce26 -- the pending-visit flush. Retail walks
    // the queue at +0x3B8 (count from FUN_06d57150, entries from [EBX + 0x3b8],
    // stride 8) and feeds each one to FUN_06d508a0 with its flags ORed with 8,
    // then empties the queue with FUN_06d57170. PlayerData_FlushPendingVisits is
    // that function; the `visit` key below reads the COMMITTED map at +0x3A0, so
    // the flush has to happen before it or the newest systems are lost.
    // -----------------------------------------------------------------------
    PlayerData_FlushPendingVisits(Player);

    // -----------------------------------------------------------------------
    // .text:06d4ce2b .. .text:06d4cf1d -- the destination, which retail builds
    // itself. This used to be the caller's invention.
    //   name    IsMPServer() ? FUN_06d46370(buf, slot)      .text:06d4ce40
    //                        : wcstombs(buf, slot, 0x104)   .text:06d4ce51
    //           -- the wide account name really does live at PlayerData+0x00.
    //   dir     _makepath(tmp, 0, FUN_06d460f0(), name, 0)  .text:06d4ce7a
    //   final   _makepath(tmp, 0, tmp, param_1,             .text:06d4cf00
    //                     IsMPServer() ? 0 : "fl")
    //           -- the extension operand at .text:06d4ceee is `AND EAX,imm32`
    //           (opcode 0x25), i.e. the ADDRESS .rdata:06d6aea4, which holds the
    //           two bytes "fl". In MP param_1 already carries its own extension.
    //   target  SinglePlayer() ? that path : "<accts>\temp.fl"  .text:06d4cf05
    // -----------------------------------------------------------------------
    char AccountDirName[260];                                               // Stack[0x1f4], no pre-init
    if (IsMPServer()) {
        Server_AccountDirName(AccountDirName, Player.AccountName);
    } else {
        // .text:06d4ce4a .. .text:06d4ce51 -- wcstombs(buf, slot, 0x104) and
        // nothing else: no return-value test and no explicit terminator. The
        // account name is a short NUL-terminated wide string, so wcstombs copies
        // its terminator itself. KNOWN retail UB: a 260+ character name, or one
        // that does not convert (return -1), leaves the buffer unterminated.
        wcstombs(AccountDirName, Player.AccountName, 0x104);
    }

    char AccountDir[260];
    _makepath(AccountDir, nullptr, Server_GetAccountsRoot(), AccountDirName, nullptr);

    char SavePath[260];
    _makepath(SavePath, nullptr, AccountDir, FileName, IsMPServer() ? nullptr : "fl");

    char TempPath[260];
    sprintf_s(TempPath, "%s\\temp.fl", Server_GetAccountsRoot());

    char const* const WritePath = SinglePlayer() ? SavePath : TempPath;

    // -----------------------------------------------------------------------
    // .text:06d4cf1d .. .text:06d4cf86 -- THE WRITER OBJECT. Retail builds it on
    // its own stack at ESP+0x24: FUN_06d4eac0 zero-initialises the {FILE*, buffer}
    // pair, .text:06d4cf41 stamps the abstract base's vtable (06d6b5c0, seven
    // purecall slots) and .text:06d4cf4f overwrites it with the concrete one
    // (06d6b5a0), then the path string is assigned and fopen runs.
    //
    // CORRECTED 2026-09-17 (wave 18): the mode is "w+t", not "w". The previous
    // pass read .rdata:DAT_06d6b598 off the listing, which prints `;= 77h  w`
    // because it shows only the first byte of the operand. The real bytes in
    // EXE\Server.dll at that address are `77 2b 74 00`. The "+" is what lets
    // Close() read the file back for encoding, and the "t" is why every real
    // save this machine holds is LF-only.
    // -----------------------------------------------------------------------
    SaveGame_Writer Writer;
    if (!Writer.Open(WritePath)) {
        // .text:06d4cf96 .. .text:06d4cfb1 -- FDUMP(0x100002, .rdata:06d6b6c0,
        // .rdata:06d6b280 = PlayerDB.cpp, 0xad4, path), then AL = 0. (Was an
        // OutputDebugString with invented text until 2026-09-24.)
        FDUMP(0x100002, "%s(%d) : *** WARNING: Failed to open %s for writing",
              "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0xad4, WritePath);
        return false;
    }

    // .text:06d4d016 -- safe_strcpy(DAT_06d902f8, 0x104, target): retail keeps
    // the path it is writing in a global; FUN_06d4c600 copies that file to
    // AutoSave.fl afterwards (.text:06d4c7e4, PlayerData_MissionSave).
    safe_strcpy(GServerLastSavePath, 0x104, WritePath);

    // Retail writes most keys straight to the FILE* (`MOV EDX,[ESP + 0x30]`
    // reads the writer's +0x04 and hands it to fprintf) and only the costume
    // blocks and the single-player hook go through the vtable. This port keeps
    // that split rather than routing everything through the object, because
    // routing everything would silently reorder nothing but would stop the line
    // numbers here matching the listing.
    FILE* const Out = Writer.File;                                  // [writer+4], e.g. .text:06d4d026

    // .text:06d4d030 -- fprintf(f, "\n[%s]\n", "Player") (.rdata:06d6b590).
    fprintf(Out, "\n[%s]\n", "Player");

    // .text:06d4d03d .. .text:06d4d071 -- `description`, only when param_2 is
    // non-null, hex-encoded with COMMON.DLL::WideToHex.
    if (Description != nullptr) {
        char HexBuf[2048];
        WideToHex(Description, HexBuf);
        fprintf(Out, "%s = %s\n", "description", HexBuf);
    }

    // .text:06d4d074 .. .text:06d4d08d -- `descrip_strid`, only when param_3 is
    // non-zero, "%s = %u\n" (.rdata:06d6b5e8).
    if (DescripStrId != 0)
        fprintf(Out, "%s = %u\n", "descrip_strid", DescripStrId);

    // .text:06d4d094 -- a bare newline (.rdata:DAT_06d6af50 is the single byte
    // 0x0a, passed as fprintf's whole format string).
    fprintf(Out, "\n");

    // -----------------------------------------------------------------------
    // .text:06d4d09f .. .text:06d4d0fd -- `tstamp`.
    //   GetLocalTime(&st) / SystemTimeToFileTime(&st, &ft)
    //   sprintf(buf, "%u,%u", ft.dwHighDateTime, ft.dwLowDateTime)
    // CORRECTED: the port had the two halves the other way round. The pushes are
    //   .text:06d4d0c3  MOV EAX,dword ptr [ESP + 0xcc]   ; ft.dwLowDateTime
    //   .text:06d4d0ca  MOV ECX,dword ptr [ESP + 0xd0]   ; ft.dwHighDateTime
    //   .text:06d4d0d1  PUSH EAX                          ; pushed FIRST  -> LAST arg
    //   .text:06d4d0d2  PUSH ECX                          ; pushed SECOND -> first %u
    // so cdecl's reverse order makes the HIGH half the first conversion.
    // No space after the comma (.rdata:s_%u,%u_06d6b6b8).
    // -----------------------------------------------------------------------
    {
        SYSTEMTIME SysTime;
        FILETIME FileTime;
        GetLocalTime(&SysTime);
        SystemTimeToFileTime(&SysTime, &FileTime);
        char TstampBuf[64];
        sprintf_s(TstampBuf, "%u,%u", FileTime.dwHighDateTime, FileTime.dwLowDateTime);
        fprintf(Out, "%s = %s\n", "tstamp", TstampBuf);
    }

    // -----------------------------------------------------------------------
    // .text:06d4d0ff .. .text:06d4d35e -- the reputation block: ONE call to
    // COMMON.DLL::Reputation::Vibe::Get and four keys off its out-parameters.
    //
    //   .text:06d4d0ff  CALL [->COMMON.DLL::Reputation::group_count]
    //   .text:06d4d105  SHL EAX,0x3 / operator new          ; Relation[count], stride 8
    //   .text:06d4d234  CALL [->COMMON.DLL::Reputation::Vibe::Get]
    // with the eight arguments recovered from the LEA/PUSH pairs at
    // .text:06d4d1f7 .. .text:06d4d233 (base P = ESP at .text:06d4d1f7):
    //   GroupId     &[EBX + 0x318]   = PlayerData::rep_vibe
    //   Affiliation  P+0xe8          Rank        P+0x9c
    //   Count        P+0x43 (byte)   Relations   the new'd block
    //   Name1        P+0xb4          Name2       P+0xcc
    //   DisplayName  P+0xec
    //
    // FOUR CORRECTIONS against the old port, all of them fabrication:
    //   * `name` is WideToHex of Vibe::Get's DISPLAY NAME out-parameter
    //     (.text:06d4d23a reads P+0xec, .text:06d4d24d WideToHex,
    //     .text:06d4d25b key .rdata:06d6b2e8 = "name"). The port wrote the
    //     character name there instead.
    //   * there is no `vibe_name` key anywhere in this function.
    //   * there is no `affiliation` key and no `affiliation_name` key. What
    //     .text:06d4d288 writes is .rdata:06d6b164 = "rank", as "%s = %u\n",
    //     from Vibe::Get's Rank -- overridden in SINGLE PLAYER from
    //     PlayerData+0x284 (.text:06d4d275 `MOV EDX,[EBX + 0x284]`), which is
    //     PlayerData::rank_level. (This wave's brief put that override at +0x142
    //     and called it an affiliation; the bytes say +0x284 and `rank`.)
    //   * the PER-GROUP loop key is `house` (.rdata:06d6b13c, .text:06d4d2f9),
    //     not `rep_group`. `rep_group` (.rdata:06d6b144, .text:06d4d351) is
    //     written exactly ONCE, and only when Affiliation != 0xffffffff
    //     (.text:06d4d31e `CMP dword ptr [ESP + 0xc4],-0x1`), carrying the
    //     affiliation group's own nickname.
    // -----------------------------------------------------------------------
    // .text:06d4d105 / .text:06d4d109 -- operator new(group_count() * 8), kept
    // in [ESP+0x8c] until .text:06d4e92a frees it: the MP record refresh at the
    // end of this function reads the array, its byte count ([ESP+0x1f]) and
    // the rank ([ESP+0x78]) again, so all three live at function scope.
    // (claude-code:w5-server-06d4, 2026-09-24: the port used to delete the array
    // at the end of this block and guard Vibe::Get on a non-null array and a
    // non-negative vibe; retail calls it unconditionally at .text:06d4d234.)
    unsigned int const GroupCount = GlobalReputation::group_count();          // .text:06d4d0ff
    GlobalReputation::Relation* const Relations = static_cast<GlobalReputation::Relation*>(
        operator new(GroupCount * sizeof(GlobalReputation::Relation)));    // .text:06d4d109
    unsigned int Affiliation = 0xffffffffu;
    unsigned int Rank = 0;
    unsigned char RelationCount = 0;
    FmtStr Name1;                                                          // .text:06d4d117..
    FmtStr Name2;                                                          // .text:06d4d187..
    {
        wchar_t const* DisplayName = nullptr;

        GlobalReputation::Vibe::Get(Player.rep_vibe, Affiliation, Rank, RelationCount,
                                    Relations, Name1, Name2, DisplayName); // .text:06d4d234

        // .text:06d4d23a .. .text:06d4d268 -- `name`, unconditionally:
        //   .text:06d4d23a  MOV  EDX,[ESP + 0x10c]   ; the P+0xec out-parameter
        //   .text:06d4d24d  CALL WideToHex(EDX, buf)
        //   .text:06d4d266  fprintf(f, "%s = %s\n", "name", buf)
        // There is no test anywhere between the call and the fprintf.
        // KNOWN retail UB: nothing in this function writes P+0xec before the call
        // (.text:06d4d10e .. 06d4d1f0 initialise only the two FmtStr locals at
        // P+0xb4 and P+0xcc), and Reputation::Vibe::Get writes its out-parameters
        // only on a hit -- its miss path (.text:06320c29 JZ -> 06320c9e) returns
        // -2 with all of them untouched. On a miss retail hands an uninitialised
        // stack word to WideToHex; the port hands it the nullptr it starts with,
        // and WideToHex faults on it the same way. Not guarded: the path is
        // retail's. (The same miss also leaves Affiliation, Rank and the count
        // byte uninitialised in retail; the port starts them at 0xffffffff, 0, 0.)
        {
            char HexBuf[2048];
            WideToHex(DisplayName, HexBuf);
            fprintf(Out, "%s = %s\n", "name", HexBuf);
        }

        // .text:06d4d26b .. .text:06d4d293 -- `rank`, with the SP override.
        if (SinglePlayer())
            Rank = Player.rank_level;
        fprintf(Out, "%s = %u\n", "rank", Rank);

        // .text:06d4d2a6 .. .text:06d4d31c -- one `house` line per Relation.
        //   .text:06d4d2c3  CALL [->COMMON.DLL::Reputation::get_nickname]
        //   .text:06d4d2c9  FLD  float ptr [ESI + 0x4]              ; Relation::Feeling
        //   .text:06d4d2e1  sprintf(buf, "%g, %s", feeling, nick)   (.rdata:06d6b6b0)
        //   .text:06d4d2f9  fprintf(f, "%s = %s\n", "house", buf)
        // The loop bound is the BYTE counter Vibe::Get filled, re-read every
        // iteration at .text:06d4d30a.
        for (unsigned int I = 0; I < static_cast<unsigned int>(RelationCount); ++I) {
            TString<16> GroupNick;
            GroupNick.Length = 0;
            GroupNick.Data[0] = '\0';
            GlobalReputation::get_nickname(GroupNick, Relations[I].GroupId);
            char HouseBuf[256];
            sprintf_s(HouseBuf, "%g, %s", static_cast<double>(Relations[I].Feeling),
                      GroupNick.Data);
            fprintf(Out, "%s = %s\n", "house", HouseBuf);
        }

        // .text:06d4d31e .. .text:06d4d35e -- the single `rep_group` line.
        if (Affiliation != 0xffffffffu) {
            TString<16> AffilNick;
            AffilNick.Length = 0;
            AffilNick.Data[0] = '\0';
            GlobalReputation::get_nickname(AffilNick, Affiliation);
            fprintf(Out, "%s = %s\n", "rep_group", AffilNick.Data);
        }
    }

    // -----------------------------------------------------------------------
    // .text:06d4d361 .. .text:06d4d3bf -- four integer keys, in RETAIL's order
    // (`money` first; the port had it last).
    //   money              [EBX + 0x31c]  .text:06d4d361
    //   num_kills          [EBX + 0x334]  .text:06d4d379
    //   num_misn_successes [EBX + 0x338]  .text:06d4d391
    //   num_misn_failures  [EBX + 0x33c]  .text:06d4d3a9
    // all through "%s = %d\n" (.rdata:06d6b5dc).
    // -----------------------------------------------------------------------
    fprintf(Out, "%s = %d\n", "money", Player.cash);
    fprintf(Out, "%s = %d\n", "num_kills", Player.num_kills);
    fprintf(Out, "%s = %d\n", "num_misn_successes", Player.num_mission_successes);
    fprintf(Out, "%s = %d\n", "num_misn_failures", Player.num_mission_failures);

    // .text:06d4d3c8 -- bare newline.
    fprintf(Out, "\n");

    // .text:06d4d3d4 -- `voice`, the ADDRESS of the inline buffer at +0x2C4.
    fprintf(Out, "%s = %s\n", "voice", Player.voice);

    // .text:06d4d3e8 / .text:06d4d3fe -- the two costume blocks. The prefixes are
    // .rdata:DAT_06d6b12c = "com_" and .rdata:DAT_06d66ee4 = "" (four zero bytes).
    Server_WriteCostume(Writer, "com_", Player.com_costume);
    Server_WriteCostume(Writer, "", Player.costume);

    // .text:06d4d418 -- bare newline.
    fprintf(Out, "\n");

    // -----------------------------------------------------------------------
    // .text:06d4d420 .. .text:06d4d468 -- `system`, from Universe::get_system of
    // PlayerData+0x3E0. Retail does NOT null-check the result: .text:06d4d44e
    // dereferences it straight into the vtable, and neither does this port.
    // [EDX + 0x8] is Common's SYSTEM vtable (.rdata:063a5144, COL DAT_063aac30)
    // slot 2 = .text:063390f0 `MOV EAX,[ECX + 0x50]; RET`, and +0x50 is the
    // StringAlloc'd `nickname` (.text:0633748a) -- ISystem::Nickname, handed to
    // fprintf unguarded as retail does.
    // -----------------------------------------------------------------------
    {
        Universe::ISystem const* Sys = Universe::get_system(Player.system_id);
        fprintf(Out, "%s = %s\n", "system", Sys->Nickname);
    }

    // -----------------------------------------------------------------------
    // .text:06d4d46a .. .text:06d4d830 -- the base / position branch.
    //
    // .text:06d4d46a  MOV  EAX,[EBX + 0x3e4]                ; ship object id
    // .text:06d4d477  TEST dword ptr [EBX + 0x3ec],0x3fffffff
    // .text:06d4d483  MOV  ECX,[EBX + 0x3f0] / MOV [EBX + 0x3ec],ECX
    //   -- when there is no live ship object AND current_base holds no object
    //      bits, current_base is refilled from last_base_id first. The port used
    //      Player.base_id raw and never did this.
    // .text:06d4d496  CALL [->COMMON.DLL::Universe::get_base]
    // .text:06d4d4ab  CALL [EDX + 0x4]                      ; the IBase nickname
    // .text:06d4d4b3  key .rdata:DAT_06d6b228 = "base"
    // -----------------------------------------------------------------------
    // [ESP+0x58..0x60] and [ESP+0xdc..0xff]: written only by the `pos` /
    // `rotate` arm below, read again by the MP record refresh at the end
    // (.text:06d4e535 .. 06d4e561) whether or not that arm ran.
    //   position     retail ZEROES it unconditionally before `system`:
    //                .text:06d4d427 / 06d4d432 / 06d4d43d store 0 to
    //                [ESP+0x94/0x98/0x9c] with 0x3c bytes of pushes outstanding,
    //                i.e. frame+0x58/0x5c/0x60. So {0,0,0} is retail, not a guard.
    //   orientation  nothing writes the nine dwords outside the `rotate` arm.
    //                KNOWN retail UB: with a base, or with no live ship, the MP
    //                refresh copies nine uninitialised stack dwords into the
    //                record; the port copies zeros instead.
    float SavedPos[3] = { 0.0f, 0.0f, 0.0f };
    float SavedOrient[9] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    {
        if (Player.ship_object_id == 0 && (Player.current_base & 0x3fffffffu) == 0)
            Player.current_base = Player.last_base_id;

        Universe::IBase* Base = Universe::get_base(Player.current_base);
        if (Base != nullptr) {
            // [EDX + 0x4] is Common's BASE vtable (.rdata:063a4d5c) slot 1 =
            // .text:062a66a0 `MOV EAX,[ECX + 0x8]; RET`; +0x8 is the StringAlloc'd
            // `nickname` (.text:063387d8). Retail does not test it for null.
            fprintf(Out, "%s = %s\n", "base", Base->Nickname);
            fprintf(Out, "\n");
            // .text:06d4d4cf -- single player jumps straight to `location`.
            // .text:06d4d4dd .. .text:06d4d617 -- outside it, the docked state
            // goes into the selected character's record at +0x50: FUN_06d57da0
            // pairs +0x64 with a fresh record (FUN_06d46ab0), FUN_06d575f0 is
            // the map's unique insert (an existing record is kept), and the node
            // + 0x20c record gets relative_health (+0x268 -> record+0x25c,
            // .text:06d4d549), the collision-group list (+0x26c -> +0x26c,
            // inlined list::operator= .text:06d4d55b..06d4d5fa) and the
            // equipment list (+0x278 -> +0x260, list::operator= .text:06d4d611).
            if (!SinglePlayer()) {
                // insert(pair(key, default)).first == operator[] (find or
                // default-insert, never overwrite).
                PlayerData::FSavedCharacter& BaseRecord =
                    Player.saved_characters[Player.selected_character];
                BaseRecord.BaseHullStatus = Player.relative_health;
                BaseRecord.BaseCollisionGroups = Player.collision_groups;
                BaseRecord.BaseEquipment.Items = Player.equipment.Items;
            }
        } else {
            // -----------------------------------------------------------------
            // .text:06d4d61c .. .text:06d4d82b -- no base: `pos` and `rotate`.
            // Re-traced 2026-09-24 (claude-code:w5-server-06d4).
            //   .text:06d4d623  FUN_06d00670(+0x3e4) = Server_LookupLiveObject
            //                   (the lookup plus the is_destroyed reject); null
            //                   skips both keys and the newline (.text:06d4d62f).
            //                   The port used Server_LookupGameObject plus a
            //                   cobj() test retail does not have.
            //   position        SinglePlayer(): IObjInspect slot 0 (.text:06d4d698).
            //                   Otherwise .text:06d4d643 .. 06d4d6aa: cobject()
            //                   (vt+0x150), zero unless (class & 0x503) == 0x503,
            //                   then __RTDynamicCast(that+0x60, IVP_Controller ->
            //                   CRemotePhysicsController) (.text:06d4d672); on a hit
            //                   with controller+0x388 set the position is
            //                   controller+0xe0, with it clear slot 0; on a miss the
            //                   locals are printed as they are.
            //   orientation     IObjInspect slot 3 (+0xc) -- get_transform, whose
            //                   Rotation is its first 0x24 bytes -- copied nine
            //                   dwords to [ESP+0xdc] (.text:06d4d6ff .. 06d4d714).
            //   angles          FUN_06d4ea60 / ea80 / eaa0 are column extractors
            //                   (flat M[0,3,6] / M[1,4,7] / M[2,5,8]). H =
            //                   sqrt(M0*M0 + M3*M3), FST to a float, compared with
            //                   .rdata:DAT_06d6b6a0 (PE bytes 00 00 00 36 =
            //                   1.9073486e-06f) by TEST AH,0x41:
            //                     H > eps : A = atan2(M7, M8), B = atan2(-M6, H),
            //                               C = atan2(M3, M0) (stored as a float)
            //                     else    : A = atan2(-M5, M4), B = atan2(-M6, H),
            //                               C = 0
            //                   each multiplied by .rdata:DAT_06d6b69c (PE bytes
            //                   e0 2e 65 42 = 57.2957763671875f) and printed
            //                   "%g,%g,%g" as A, B, C (.text:06d4d7bf .. 06d4d7fa).
            // FIXED: the port wrote RADIANS and in the order (B, C, A). Retail
            // writes DEGREES in the order (A, B, C) -- and the loader feeds the
            // three values to EulerMatrix, which takes degrees.
            // -----------------------------------------------------------------
            GameObject* const LiveObj = Server_LookupLiveObject(Player.ship_object_id);
            if (LiveObj != nullptr) {
                // The position source, .text:06d4d635 .. .text:06d4d6aa (deviations.md
                // "Server.dll wave 17" item 7, closed 2026-09-24 by claude-code:w6-rpc):
                //   single player  -> IObjInspect slot 0, get_position (.text:06d4d698).
                //   otherwise      -> cobject() (vt+0x150, .text:06d4d643), kept only when
                //                     (ObjectClass & 0x503) == 0x503 (a CShip), else 0
                //                     (.text:06d4d649 .. 06d4d65e); then
                //                     __RTDynamicCast(ship+0x60, 0, IVP_Controller,
                //                     CRemotePhysicsController, 0) (.text:06d4d672).
                //                     Hit: sample count (+0x388) non-zero -> the latest
                //                     sample at +0xe0 (.text:06d4d688); zero -> slot 0 as in
                //                     single player (.text:06d4d694). Miss: the locals are
                //                     NOT written (.text:06d4d67c JZ 06d4d6ae) and `pos` prints
                //                     the zeros stored at .text:06d4d427 .. 06d4d43d.
                // KNOWN retail UB, reproduced as a path, not guarded: when cobject() is null
                // or not a CShip, EAX is 0 at .text:06d4d660 and `MOV EDX,[EAX + 0x60]` reads
                // address 0x60 -- retail faults there, and so does this port.
                if (SinglePlayer()) {                                     // .text:06d4d635
                    Vector const& LivePos = LiveObj->get_position();     // .text:06d4d698
                    SavedPos[0] = LivePos.x;
                    SavedPos[1] = LivePos.y;
                    SavedPos[2] = LivePos.z;
                } else {
                    CObject const* const Cobj = LiveObj->cobject();       // .text:06d4d643
                    CObject const* const Ship =
                        (Cobj != nullptr &&
                         (static_cast<unsigned int>(Cobj->ObjectClass) & 0x503u) == 0x503u)
                            ? Cobj
                            : nullptr;
                    IVP_Controller* const Rc =
                        static_cast<IVP_Controller*>(Ship->GolemController); // .text:06d4d660
                    CRemotePhysicsController* const Remote =
                        dynamic_cast<CRemotePhysicsController*>(Rc);      // .text:06d4d672
                    if (Remote != nullptr) {
                        if (Remote->GetSampleCount() != 0) {              // .text:06d4d67e
                            Vector const Latest = Remote->GetLatestUpdate(); // .text:06d4d688
                            SavedPos[0] = Latest.x;
                            SavedPos[1] = Latest.y;
                            SavedPos[2] = Latest.z;
                        } else {
                            Vector const& LivePos = LiveObj->get_position(); // .text:06d4d698
                            SavedPos[0] = LivePos.x;
                            SavedPos[1] = LivePos.y;
                            SavedPos[2] = LivePos.z;
                        }
                    }
                }
                char PosBuf[128];
                sprintf(PosBuf, "%g,%g,%g", static_cast<double>(SavedPos[0]),
                        static_cast<double>(SavedPos[1]),
                        static_cast<double>(SavedPos[2]));               // .text:06d4d6d5
                fprintf(Out, "%s = %s\n", "pos", PosBuf);                 // .text:06d4d6f2

                ::Matrix const& LiveOrient = LiveObj->get_transform().Rotation; // .text:06d4d6ff
                for (int I = 0; I < 9; ++I)
                    SavedOrient[I] = LiveOrient.E[I / 3][I % 3];          // .text:06d4d714

                float const* const M = SavedOrient;
                double const DegPerRad = static_cast<double>(57.2957763671875f);
                double const HypotExact = sqrt(static_cast<double>(M[0]) * M[0] +
                                               static_cast<double>(M[3]) * M[3]);
                float const Hypot = static_cast<float>(HypotExact);      // .text:06d4d75f FST
                double AngleA = 0.0;
                double AngleB = 0.0;
                float AngleC = 0.0f;
                if (HypotExact > static_cast<double>(1.9073486328125e-06f)) {
                    AngleA = atan2(static_cast<double>(M[7]), static_cast<double>(M[8]));
                    AngleB = atan2(-static_cast<double>(M[6]), static_cast<double>(Hypot));
                    AngleC = static_cast<float>(atan2(static_cast<double>(M[3]),
                                                      static_cast<double>(M[0])));
                } else {
                    AngleA = atan2(-static_cast<double>(M[5]), static_cast<double>(M[4]));
                    AngleB = atan2(-static_cast<double>(M[6]), static_cast<double>(Hypot));
                    AngleC = 0.0f;                                        // .text:06d4d7b7
                }
                float const DegA = static_cast<float>(AngleA * DegPerRad); // .text:06d4d7d1 FSTP float
                double const DegB = AngleB * DegPerRad;
                double const DegC = static_cast<double>(AngleC) * DegPerRad;

                char RotBuf[128];
                sprintf(RotBuf, "%g,%g,%g", static_cast<double>(DegA), DegB, DegC); // .text:06d4d7fa
                fprintf(Out, "%s = %s\n", "rotate", RotBuf);             // .text:06d4d81d
                fprintf(Out, "\n");                                      // .text:06d4d829
            }
        }
    }

    // .text:06d4d830 .. .text:06d4d84c -- `location`, only when +0x3F8 is non-zero.
    // This key was missing from the port entirely.
    if (Player.current_location != 0)
        fprintf(Out, "%s = %u\n", "location", Player.current_location);

    // -----------------------------------------------------------------------
    // .text:06d4d84f .. .text:06d4d957 -- ship_archetype / hull_status /
    // collision_group, all three gated on the archetype resolving.
    // (Unchanged from the previous wave, which read these off the listing.)
    // -----------------------------------------------------------------------
    if (Player.ship_archetype != 0) {
        Archetype::Ship* ShipArch = Archetype::GetShip(Player.ship_archetype);
        if (ShipArch != nullptr) {
            // .text:06d4d873 `MOV EAX,[EAX + 0x8]` -- the RESOLVED archetype's
            // Root::Nickname id, through sprintf "%u" (.rdata:06d6b53c).
            char ArchBuf[32];
            sprintf_s(ArchBuf, "%u", ShipArch->get_id());
            fprintf(Out, "%s = %s\n", "ship_archetype", ArchBuf);

            // .text:06d4d8a3 FLD [EBX + 0x268] / FCOMP 1.0f -- strictly below 1.
            if (Player.relative_health < 1.0f) {
                fprintf(Out, "%s = %g\n", "hull_status",
                        static_cast<double>(Player.relative_health));
            }

            // .text:06d4d8d9 .. .text:06d4d957 -- the PLAYER's own list at +0x26C.
            for (std::list<CollisionGroupDesc>::const_iterator It =
                     Player.collision_groups.begin();
                 It != Player.collision_groups.end(); ++It) {
                if (!(It->Health < 1.0f))
                    continue;
                Archetype::CollisionGroup const* Group = ShipArch->get_group_by_id(It->Id);
                if (Group == nullptr)
                    continue;
                char GroupBuf[256];
                sprintf_s(GroupBuf, "%s, %g", Group->ObjName, static_cast<double>(It->Health));
                fprintf(Out, "%s = %s\n", "collision_group", GroupBuf);
            }
        }
    }

    // .text:06d4d95d -- bare newline on the join point of both archetype skips.
    fprintf(Out, "\n");

    // -----------------------------------------------------------------------
    // .text:06d4d968 .. .text:06d4dabb -- the equipment list, two keys.
    // (Unchanged from the previous wave, which read these off the listing.)
    // -----------------------------------------------------------------------
    for (std::list<EquipDesc>::const_iterator CI = Player.equipment.Items.begin();
         CI != Player.equipment.Items.end(); ++CI) {
        EquipDesc const& Item = *CI;

        // .text:06d4d980 -- outside single player, mission items are skipped.
        if (!::SinglePlayer() && Item.bMission)
            continue;

        Archetype::Equipment* Equip = Archetype::GetEquipment(Item.ArchId);
        if (Equip == nullptr) continue;
        unsigned int const NicknameId = Equip->Nickname.Id;

        char Line[512];
        if (!Item.bEquipped) {
            if (Item.Health == 1.0f) {
                sprintf(Line, "%u, %d, , , %d", NicknameId, Item.Count, Item.bMission ? 1 : 0);
            } else {
                sprintf(Line, "%u, %d, , %g, %d", NicknameId, Item.Count,
                        static_cast<double>(Item.Health), Item.bMission ? 1 : 0);
            }
            fprintf(Out, "%s = %s\n", "cargo", Line);
        } else {
            if (Item.is_internal()) {
                sprintf(Line, "%u, , %g", NicknameId, static_cast<double>(Item.Health));
            } else {
                sprintf(Line, "%u, %s, %g", NicknameId, Item.Hardpoint,
                        static_cast<double>(Item.Health));
            }
            fprintf(Out, "%s = %s\n", "equip", Line);
        }
    }

    // .text:06d4dac5 -- bare newline after the equipment list.
    fprintf(Out, "\n");

    // -----------------------------------------------------------------------
    // .text:06d4dacd .. .text:06d4dda5 -- `last_base` and the base_* keys.
    //
    // SETTLED: `last_base` is NOT the base written above. Retail resolves a
    // SECOND base object from PlayerData+0x3F0 --
    //   .text:06d4dacd  MOV  EAX,dword ptr [EBX + 0x3f0]
    //   .text:06d4dad4  CALL [->COMMON.DLL::Universe::get_base]
    //   .text:06d4dadd  MOV  dword ptr [ESP + 0xc],EAX       ; stashed
    //   .text:06d4db79  MOV  ECX,dword ptr [ESP + 0xc]       ; read back
    //   .text:06d4db7f  CALL dword ptr [EDX + 0x4]           ; its nickname
    //   .text:06d4db87  PUSH .rdata:s_last_base_06d6b1cc
    // and the whole block is gated twice: .text:06d4dae9 skips it in single
    // player, .text:06d4daf5 skips it when that second base is null.
    //
    // The four base_* keys that follow it read the selected character's record
    // in the container at PlayerData+0x50, fetched at .text:06d4dafb ..
    // .text:06d4db49 exactly like the copy at .text:06d4d4dd (FUN_06d57da0 key,
    // FUN_06d59360 = the map's unique insert, FUN_06d0f600 copies the returned
    // pair; the node + 0x20c record is ESI). Retail does that lookup BEFORE it
    // writes `last_base`.
    // -----------------------------------------------------------------------
    if (!SinglePlayer()) {
        Universe::IBase* LastBase = Universe::get_base(Player.last_base_id);
        if (LastBase != nullptr) {
            PlayerData::FSavedCharacter const& BaseRecord =
                Player.saved_characters[Player.selected_character];

            // .text:06d4db7f -- the same BASE slot 1 nickname getter, unguarded.
            fprintf(Out, "%s = %s\n", "last_base", LastBase->Nickname);
            fprintf(Out, "\n");                       // .text:06d4db98

            // .text:06d4dba0 .. .text:06d4dbbc -- record+0x25c, "%s = %g\n",
            // unconditionally (no `< 1` test, unlike hull_status).
            fprintf(Out, "%s = %g\n", "base_hull_status",
                    static_cast<double>(BaseRecord.BaseHullStatus));

            // .text:06d4dbbe .. .text:06d4dc39 -- GetShip(+0x264) with NO null
            // test, then every node of record+0x26c (no health filter):
            // get_group_by_id(Id), and on a hit "%s, %g" (group+0x08, Health)
            // under `base_collision_group`.
            Archetype::Ship* BaseShip = Archetype::GetShip(Player.ship_archetype);
            for (std::list<CollisionGroupDesc>::const_iterator It =
                     BaseRecord.BaseCollisionGroups.begin();
                 It != BaseRecord.BaseCollisionGroups.end(); ++It) {
                Archetype::CollisionGroup const* Group = BaseShip->get_group_by_id(It->Id);
                if (Group == nullptr)
                    continue;
                char GroupBuf[256];
                sprintf(GroupBuf, "%s, %g", Group->ObjName, static_cast<double>(It->Health));
                fprintf(Out, "%s = %s\n", "base_collision_group", GroupBuf);
            }
            fprintf(Out, "\n");                       // .text:06d4dc3f

            // .text:06d4dc47 .. .text:06d4dd93 -- record+0x260. A mission item
            // (+0x18) is skipped outright; an unresolved archetype is skipped.
            // Not mounted (+0x0c == 0): `base_cargo`, "%u, %d, , , %d" when
            // Health == 1.0f (.rdata:06d65520), else "%u, %d, , %g, %d".
            // Mounted: `base_equip`, "%u, , %g" for is_internal, else
            // "%u, %s, %g" with the hardpoint. %u is Equipment+0x08.
            for (std::list<EquipDesc>::const_iterator It =
                     BaseRecord.BaseEquipment.Items.begin();
                 It != BaseRecord.BaseEquipment.Items.end(); ++It) {
                EquipDesc const& Item = *It;
                if (Item.bMission)
                    continue;
                Archetype::Equipment* Equip = Archetype::GetEquipment(Item.ArchId);
                if (Equip == nullptr)
                    continue;
                unsigned int const NicknameId = Equip->Nickname.Id;

                char Line[512];
                if (!Item.bEquipped) {
                    if (Item.Health == 1.0f) {
                        sprintf(Line, "%u, %d, , , %d", NicknameId, Item.Count,
                                Item.bMission ? 1 : 0);
                    } else {
                        sprintf(Line, "%u, %d, , %g, %d", NicknameId, Item.Count,
                                static_cast<double>(Item.Health), Item.bMission ? 1 : 0);
                    }
                    fprintf(Out, "%s = %s\n", "base_cargo", Line);
                } else {
                    if (Item.is_internal()) {
                        sprintf(Line, "%u, , %g", NicknameId, static_cast<double>(Item.Health));
                    } else {
                        sprintf(Line, "%u, %s, %g", NicknameId, Item.Hardpoint,
                                static_cast<double>(Item.Health));
                    }
                    fprintf(Out, "%s = %s\n", "base_equip", Line);
                }
            }
            fprintf(Out, "\n");                       // .text:06d4dd9d
        }
    }

    // -----------------------------------------------------------------------
    // .text:06d4dda8 .. .text:06d4ddc3 -- the weapon-group block, dumped
    // VERBATIM: retail passes the std::string's own data pointer at +0x384 as
    // fprintf's whole FORMAT string, falling back to MSVCP60's static empty
    // string when it is null. So it is a pre-rendered block of ini lines, not a
    // single value. This block was missing from the port entirely.
    //   .text:06d4dda8  MOV  EAX,[EBX + 0x384]    ; _Ptr, or _Nullstr when null
    //   .text:06d4ddbb  PUSH EAX / PUSH FILE* / CALL fprintf   -- two arguments
    // An empty string reaches fprintf as an empty format and prints nothing,
    // which is what c_str() of an empty std::string does here as well.
    // KNOWN retail UB: the stored text is used as the FORMAT, so a '%' in it
    // is interpreted by fprintf with no arguments behind it. Reproduced as a
    // path, not escaped (was `fprintf(Out, "%s", ...)` until 2026-09-24).
    // -----------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4774 6387)  // non-literal format string: retail's own call shape
    fprintf(Out, Player.weapon_group_name.c_str());                        // .text:06d4ddc1
#pragma warning(pop)

    // .text:06d4ddca -- bare newline.
    fprintf(Out, "\n");

    // -----------------------------------------------------------------------
    // .text:06d4ddd2 .. .text:06d4de37 -- `visit`, one line per entry of the
    // COMMITTED map at +0x3A0 (node+0xc key, node+0x10 byte), through
    // sprintf "%u, %u" (.rdata:06d6b628) and then "%s = %s\n".
    // CORRECTED: the port wrote the key as `visited`. The string at
    // .rdata:06d6af28, pushed at .text:06d4de14, is "visit".
    // -----------------------------------------------------------------------
    for (std::map<unsigned int, unsigned char>::const_iterator VI = Player.visit_map.begin();
         VI != Player.visit_map.end(); ++VI) {
        char VisitBuf[64];
        sprintf_s(VisitBuf, "%u, %u", VI->first, static_cast<unsigned int>(VI->second));
        fprintf(Out, "%s = %s\n", "visit", VisitBuf);
    }

    // .text:06d4de3d -- bare newline.
    fprintf(Out, "\n");

    // -----------------------------------------------------------------------
    // .text:06d4de45 .. .text:06d4de8d -- the `log` loop. CLOSED (wave 18).
    //
    //   MOV  EAX,dword ptr [EBX + 0x394]   ; the list's _Head
    //   MOV  ESI,dword ptr [EAX]           ; first node
    //   CMP  ESI,EAX                       ; circular sentinel -> empty
    //   LEA  EAX,[ESI + 0x8]               ; the FmtStr payload, past _Next/_Prev
    //   CALL FUN_06d4cc20
    //   PUSH .rdata:DAT_06d6af18           ; "log"
    //   PUSH .rdata:s_%s_=_%s_06d6b578     ; "%s = %s\n"
    //   MOV  ESI,dword ptr [ESI]           ; advance
    // -----------------------------------------------------------------------
    for (std::list<FmtStr>::const_iterator LI = Player.mission_log.begin();
         LI != Player.mission_log.end(); ++LI) {
        // [ESP + 0x6b8] .. [ESP + 0x16b8): FUN_06d4ccd0's 0x1000-byte scratch,
        // formatted by FUN_06d4cc20 (.text:06d4de62) and printed at .text:06d4de7e.
        char LogText[0x1000];
        Server_FormatMissionLogEntry(LogText, *LI);
        fprintf(Out, "%s = %s\n", "log", LogText);
    }

    // .text:06d4de93 -- bare newline.
    fprintf(Out, "\n");

    // .text:06d4de9b .. .text:06d4deb1 -- `interface`, "%s = %u\n" from +0x39C.
    // This key was missing from the port entirely.
    fprintf(Out, "%s = %u\n", "interface", Player.interface_state);

    // .text:06d4deb7 -- bare newline.
    fprintf(Out, "\n");

    // .text:06d4debf .. .text:06d4dee1 -- FUN_06d43650(4, &{+0x348, FILE*}).
    // Two cdecl arguments of the preceding fprintf are still on the stack, so
    // [ESP + 0x48] is the writer's FILE* at frame+0x28, not frame+0x30: the
    // record is {client_id, the open save file} at frame+0xd4.
    struct FSaveWrittenEvent {
        unsigned int ClientId;                  // PlayerData+0x348
        FILE* File;                             // the writer's +0x04
    };
    FSaveWrittenEvent SaveEvent;
    SaveEvent.ClientId = Player.client_id;
    SaveEvent.File = Out;
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerSave, &SaveEvent);

    // .text:06d4dee9 .. .text:06d4def8 -- single player: FUN_06d447c0(&writer),
    // the [Time] section, the feelings and event 0x41, still inside [Player]'s
    // file before the error latch and the encode.
    if (SinglePlayer())
        Server_SaveSinglePlayerState(&Writer);

    // -----------------------------------------------------------------------
    // .text:06d4df00 .. .text:06d4df08 -- THE WRITE-ERROR LATCH.
    //   MOV  EAX,dword ptr [ESP + 0x28]    ; the writer's FILE*
    //   TEST byte ptr [EAX + 0xc],0x20     ; _flag & _IOERR -- an inlined ferror
    //   JZ   LAB_06d4e0cf
    // When it is set retail FDUMPs "*** WARNING: Error writing ..." from
    // PlayerData.cpp line 0xc6d (.text:06d4df19 pushes 0xc6d), still runs the
    // whole encode-and-rewrite below, and then unlinks what it just wrote
    // (.text:06d4dfc7) so the broken file can never be loaded back. Both arms
    // encode; the only difference is the unlink.
    // -----------------------------------------------------------------------
    bool const WriteFailed = Writer.HadWriteError();
    if (WriteFailed) {
        // .text:06d4df0e .. .text:06d4df2e -- FDUMP(0x100002, .rdata:06d6b630,
        // PlayerDB.cpp (.rdata:06d6b280), 0xc6d, target path [ESP + 0x18]).
        FDUMP(0x100002, "%s(%d) : *** WARNING: Error writing %s\n",
              "E:\\FL\\Scratch\\Source\\Server\\PlayerDB.cpp", 0xc6d, WritePath);
    }

    // -----------------------------------------------------------------------
    // .text:06d4e0cf .. .text:06d4e14c -- THE "FLS1" CONTAINER.
    //
    // CORRECTED 2026-09-17 (wave 18): this is NOT multiplayer-only, which is
    // what deviation (2) used to say. The encode runs on the fall-through with
    // no mode test anywhere between the latch and it -- .text:06d4e0cf through
    // .text:06d4e14c is FUN_06d48960 / FUN_06d48b60 / fclose / fopen "wb" /
    // fprintf "FLS1" / fwrite / fclose and contains no call at all to
    // COMMON.DLL::SinglePlayer. The first SinglePlayer call after the latch is
    // .text:06d4e15f, and .text:06d4e163 `JNZ LAB_06d4e18c` makes it guard only
    // the two lines below it. Checked against the data as well: of the fifty
    // character files this machine holds, forty-nine begin with the bytes
    // `46 4c 53 31` and decode with this cipher, and every one of those is a
    // SinglePlayer save. The single exception is EXE\mpnewcharacter.fl, which is
    // a hand-authored template that was never encoded.
    //
    // Close() is retail's FUN_06d4eb00 and does the whole sequence.
    // -----------------------------------------------------------------------
    Writer.Close();

    if (WriteFailed) {
        // .text:06d4dfc2 .. .text:06d4dfcd -- _unlink(the path just written).
        _unlink(WritePath);
        // .text:06d4dfcd .. .text:06d4e0cc tears down the two FmtStrs and the
        // writer and returns AL = 0. It does NOT free the relation array
        // allocated at .text:06d4d109 -- only the success path reaches the free
        // at .text:06d4e92a -- so neither does the port.
        return false;
    }

    // -----------------------------------------------------------------------
    // .text:06d4e15f .. .text:06d4e189 -- THE MULTIPLAYER SWAP, and the only
    // part of the closeout that really is mode-dependent.
    //   CALL SinglePlayer / TEST AL,AL / JNZ LAB_06d4e18c   <- single player: skip
    //   _unlink(<the real path>)
    //   rename(<accts>\temp.fl, <the real path>)
    // Outside single player everything above was written to temp.fl (chosen at
    // .text:06d4cf16), so the finished, already-encoded temp file replaces the
    // real one here. That makes the save atomic: a crash mid-write leaves the
    // previous character file intact.
    // -----------------------------------------------------------------------
    if (!SinglePlayer()) {
        _unlink(SavePath);
        rename(TempPath, SavePath);
    }

    // -----------------------------------------------------------------------
    // .text:06d4e18c .. .text:06d4e91c -- THE MP RECORD REFRESH. Added
    // 2026-09-24 (claude-code:w5-server-06d4). Outside single player
    // (.text:06d4e190 JNZ LAB_06d4e922) the selected character's record in
    // PlayerData+0x50 is brought up to what was just written, so the in-memory
    // record reflects the last SAVE and not the last load. The port used to
    // return right after the rename.
    //   .text:06d4e196 .. 06d4e213  record = insert(pair(+0x64, FUN_06d46ab0
    //                   default)).first (FUN_06d58460 / FUN_06d59360) --
    //                   map operator[]; node offsets = record + 0x20c
    //   node+0x44c  HullStatus      = +0x268                 .text:06d4e21e
    //   node+0x450  Equipment       = +0x278 (list operator=) .text:06d4e224..06d4e2cc
    //   node+0x56c  EquipIdMaker    = +0x3c8 (a WORD)        .text:06d4e2d9
    //   node+0x45c  CollisionGroups = +0x26c (list operator=) .text:06d4e2e0..06d4e382
    //   node+0x43c..0x448  Money / NumKills / NumMissionSuccesses /
    //               NumMissionFailures = +0x31c/+0x334/+0x338/+0x33c
    //   node+0x4c8  Rank            = Vibe::Get's rank ([ESP+0x78])  .text:06d4e3ca
    //   node+0x590  Visits          = +0x3a0 (map operator=, FUN_06d57540 /
    //                                 FUN_06d59e10)          .text:06d4e3bc..06d4e43f
    //   node+0x570  WeaponGroups    = +0x380 (string assign) .text:06d4e443..06d4e513
    //   node+0x484 / 0x488  BaseId / LastBaseId = +0x3ec / +0x3f0
    //   node+0x494  Position        = the `pos` locals       .text:06d4e535..06d4e54c
    //   node+0x4a0  Orientation     = the nine matrix dwords .text:06d4e54f..06d4e561
    //   node+0x438 / 0x48c / 0x490  ShipArchetype / LocationId / SystemId =
    //               +0x264 / +0x3f8 / +0x3e0
    //   node+0x580  MissionLog      = +0x390 (list operator= with FmtStr::
    //               operator=, FUN_06d58e10 erase, FUN_06d57420 insert)
    //   node+0x4c4  StartingRing    = +0x414                 .text:06d4e636
    //   node+0x4e0  ComCostume      = +0x28c: four part ids, the count, then
    //               only `count` accessory ids (signed loop)  .text:06d4e63c..06d4e68e
    //   node+0x514  Voice           = +0x2c4 (strlen, capped at 0x1f, NUL,
    //               length)                                  .text:06d4e690..06d4e6ee
    //   node+0x538  BaseCostume     = +0x2e4, same partial copy  .text:06d4e6f6..06d4e73e
    //   node+0x58c  Interface       = +0x39c                 .text:06d4e74a
    //   node+0x4cc  Relations: erase(begin, end) (.text:06d4e750..06d4e77c), then
    //               one push_back per Vibe::Get relation, bounded by the BYTE
    //               count (.text:06d4e783..06d4e91c)
    // then the relation array is freed (.text:06d4e92a) on both the SP and the
    // MP path.
    // -----------------------------------------------------------------------
    if (!SinglePlayer()) {                                                 // .text:06d4e18c
        PlayerData::FSavedCharacter& Record =
            Player.saved_characters[Player.selected_character];            // .text:06d4e1e3
        Record.HullStatus = Player.relative_health;                        // .text:06d4e21e
        Record.Equipment.Items = Player.equipment.Items;                   // .text:06d4e224
        Record.EquipIdMaker = Player.equip_id_maker;                       // .text:06d4e2d9
        Record.CollisionGroups = Player.collision_groups;                  // .text:06d4e2e0
        Record.Money = Player.cash;                                        // .text:06d4e38e
        Record.NumKills = Player.num_kills;                                // .text:06d4e39a
        Record.NumMissionSuccesses = Player.num_mission_successes;         // .text:06d4e3a6
        Record.NumMissionFailures = Player.num_mission_failures;           // .text:06d4e3b2
        Record.Rank = Rank;                                                // .text:06d4e3ca
        Record.Visits = Player.visit_map;                                  // .text:06d4e3bc
        Record.WeaponGroups = Player.weapon_group_name;                    // .text:06d4e443
        Record.BaseId = Player.current_base;                               // .text:06d4e523
        Record.LastBaseId = Player.last_base_id;                           // .text:06d4e52f
        Record.Position[0] = SavedPos[0];                                  // .text:06d4e53f
        Record.Position[1] = SavedPos[1];
        Record.Position[2] = SavedPos[2];
        for (int I = 0; I < 9; ++I)
            Record.Orientation[I / 3][I % 3] = SavedOrient[I];             // .text:06d4e561
        Record.ShipArchetype = Player.ship_archetype;                      // .text:06d4e569
        Record.LocationId = Player.current_location;                       // .text:06d4e575
        Record.SystemId = Player.system_id;                                // .text:06d4e581
        Record.MissionLog = Player.mission_log;                            // .text:06d4e587
        Record.StartingRing = Player.starting_ring;                        // .text:06d4e636

        Server_CopyCostumeCounted(Record.ComCostume, Player.com_costume);  // .text:06d4e63c
        {
            // TString<32>::operator=(char const*) inlined. The null arm at
            // .text:06d4e698 cannot fire: the source is the inline buffer's
            // own address.
            size_t VoiceLen = strlen(Player.voice);                        // .text:06d4e6b5
            if (VoiceLen >= 0x20)
                VoiceLen = 0x1f;                                           // .text:06d4e6c5
            memcpy(Record.Voice.Data, Player.voice, VoiceLen);             // .text:06d4e6d9
            Record.Voice.Data[VoiceLen] = '\0';                           // .text:06d4e6ea
            Record.Voice.Length = static_cast<unsigned>(VoiceLen);         // .text:06d4e6ee
        }
        Server_CopyCostumeCounted(Record.BaseCostume, Player.costume);     // .text:06d4e6f6
        Record.Interface = Player.interface_state;                         // .text:06d4e74a

        Record.Relations.clear();                                          // .text:06d4e77c
        for (unsigned int I = 0; I < static_cast<unsigned int>(RelationCount); ++I)
            Record.Relations.push_back(Relations[I]);                      // .text:06d4e7a0..06d4e91c
    }

    operator delete(Relations);                                            // .text:06d4e92a
    return true;
}

namespace pub {

// pub::Save -- retail 0x7efa0 (ordinal 397), .text:06d5efa0 .. .text:06d5eff0.
// Single player only. SaveType == 1 is the autosave FUN_06d4c430; anything
// else goes to FUN_06d4c600 with SaveType itself as the descrip_strid
// (.text:06d5efdc PUSH EAX). The slot is `Players + id*0x418 - 0x418` with no
// validation, i.e. PlayerDB::operator[]. Always returns 0.
//
// REWRITTEN 2026-09-24 (claude:srv-save): this used to carry its own
// approximations of both wrappers (a "SaveXXXX" search with an unsigned-short
// format and an SP-only path, an autosave with no skip latch). Both wrappers
// are ported now in server_savegame.cpp and this is the retail dispatch.
SERVER_API int __cdecl Save(unsigned int PlayerId, unsigned int SaveType) {
    if (::SinglePlayer()) {
        PlayerData& Slot = ::Players[PlayerId];
        if (SaveType == 1)
            PlayerData_AutoSave(Slot);
        else
            PlayerData_MissionSave(Slot, SaveType);
    }
    return 0;
}

} // namespace pub
// Server_ControllerDispatch, pub::Controller::_SendMessage's dispatch core, is declared in
// common_server_controller.h (defined in IServerImpl.cpp).
extern "C" int __cdecl Server_ControllerSetHeartbeat(unsigned int Flid, float Interval);
extern "C" int __cdecl Server_ControllerSetTimer(unsigned int Flid, unsigned int Param1,
                                                 unsigned int Param2, float Timeout);
// FUN_06d43820 (IServerImpl.cpp): is this FLID a live or pending controller.
extern "C" bool __cdecl Server_ControllerExists(const unsigned int& Flid);

// Retail FUN_06d00670 (.text:06d00670), __cdecl, one BY-VALUE argument `unsigned int Flid`
// (the listing hands the ADDRESS of its own stack slot to FUN_06d006c0; every call site,
// e.g. DockRequest at .text:06cfa6b4, does `MOV ECX,[EAX]` / `PUSH ECX` — it pushes the
// dereferenced FLID, not the pointer):
//
//   LEA EAX,[ESP+8] / LEA ECX,[ESP+8] / LEA EDX,[ESP+0x18] / CALL FUN_06d006c0
//   TEST AL,AL / JNZ .. -> ESI = out object, else ESI = 0
//   TEST ESI,ESI / JZ  -> return ESI (null)
//   MOV EAX,[ESI] / CALL [EAX+0x160] / TEST AL,AL
//   JZ  -> return ESI        (slot +0x160 == 0 -> object still alive)
//   XOR EAX,EAX / RET        (slot +0x160 != 0 -> object is dying, return null)
//
// FUN_06d006c0(const unsigned int* pFlid, IObjRW** ppObj, int* pOwner) is the object
// registry lookup itself: it compares *pFlid against the cached object's FLID field at
// +0xb0, falls back to FUN_06d0c840 and then to a walk of the global object list.
//
// So FUN_06d00670 is "look an object up BY FLID and reject it if it is dying" — it has
// nothing to do with the local player. The previous port bound it to a fabricated
// `Server_LookupCurrentPlayer()` that read a `g_CurrentPlayerFLID` global nobody ever
// set, which made every caller (Dock, InstantDock, DockRequest, SendComm, GetGoodID,
// GetJumpTarget, GetTerminalInfo, FormationResponse, LaneResponse) operate on the wrong
// object or bail out with -2.
//
// The liveness test is slot +0x160 of the wrapper's OWN vtable, not an IObjInspect slot:
// IObjInspect ends at +0x150, and +0x160 is .text:06cee6d0 `MOV AL,[ECX+0x40]; RET` in all
// thirteen Server.dll vtables -- GameObject::is_destroyed (server_gameobject.h). Corrected
// 2026-09-24 (claude:srv-ai, full 20-instruction trace of .text:06d00670..06d006ba): the
// port used to test IObjInspect slot 14 (+0x38) is_dying, and additionally returned null
// when CBase::Object failed the (Class & 3) == 3 guard. Retail has no such test here;
// callers that need the CObject apply the guard themselves.
// Declared in server_gameobject.h (2026-09-24, claude-code:w5-server-06d4) so the
// other translation units that call FUN_06d00670 (PlayerData_CheckPositionBound,
// FUN_06d498f0) use this one body instead of a copy.
GameObject* Server_LookupLiveObject(unsigned int Flid) {
    GameObject* Obj = Server_LookupGameObject(Flid);
    if (Obj == nullptr) return nullptr;
    if (Obj->is_destroyed()) return nullptr;
    return Obj;
}

// NOTE: retail's Dock / InstantDock / Launch / FormationResponse / LaneResponse all
// follow the lookup with __RTDynamicCast to Ship; the wrapper hierarchy makes that a
// plain `dynamic_cast< ::Ship*>` at each of those call sites.

// The two file-static trees of SpaceObj.cpp, written by pub::SpaceObj::Create and
// erased by pub::SpaceObj::Destroy, and referenced nowhere else in Server.dll (the
// complete xref lists of .data:DAT_06d8d834 and DAT_06d8d820 are those two functions
// plus their own static constructor / atexit pairs):
//   DAT_06d8d834 -- std::set<unsigned int> of the ship ids Create made. Constructed by
//     the initializer .text:06cf7b50 (FUN_06cfc2d0, the VC6 _Tree ctor) and destroyed
//     through the atexit thunk .text:06cf7b80 (JMP FUN_06ce1420, the _Tree dtor).
//     Create inserts *out_id (.text:06cf7fb8, FUN_06cfc370).
//   DAT_06d8d820 -- std::map<unsigned int, unsigned int>, ship id -> the ShipInfo's
//     ControllerId. Initializer .text:06cf7b90 (FUN_06d050a0 ctor: head and nil nodes
//     of 0x18 bytes, size 0), atexit thunk .text:06cf7bc0 -> .text:06cf7bd0 (the
//     tree's clear + head/nil release). Create inserts {id, 0} (.text:06cf7fdc,
//     FUN_06d5c600) and then stores info.ControllerId into the node's mapped dword
//     (.text:06cf7fe7, node +0x10).
// Node layout 0x18 bytes (left/parent/right, key +0xc, mapped +0x10, color +0x14,
// isnil +0x15) is the VC6 _Tree's; the port uses the current std containers (no
// retail ABI), whose static construction and destruction replace the four thunks.
static std::set<unsigned int> GSpaceObjCreatedShips;                    // .data:DAT_06d8d834
static std::map<unsigned int, unsigned int> GSpaceObjShipControllers;   // .data:DAT_06d8d820

// ===========================================================================
// pub::SpaceObj exports
// ===========================================================================

// ===========================================================================
// pub::Phantom exports and the Server-side phantom record.
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf).
//
// Retail allocates one 0x28-byte record per phantom (operator new(0x28) at .text:06cfbea5
// / 06cfc07d) whose vtable .rdata:06d66b8c has exactly two slots, LAB_06cffed0 and
// LAB_06cfff30: it is a PhySys::CObjectCB, handed to PhySys::CreatePhantom as the
// PhantomParms+0x40 listener. The pub::Phantom handle IS that record. Layout:
//   +0x00 vtable          +0x04 std::list<unsigned> of ids that entered (alloc byte,
//   +0x08 head, +0x0c size)  +0x10 the same for ids that left (+0x14 head, +0x18 size)
//   +0x1c TypeMask (tested against CSimple::TypeBits)  +0x20 the PhySys phantom handle
//   +0x24 the system id.
// Every live record sits on the global std::list at .data:DAT_06d8d8f8 (size
// DAT_06d8d8fc), built by the static initializer LAB_06cffd20 and torn down by the atexit
// FUN_06cffd60 -- here a plain static std::list. .data:DAT_06d8d928 is set while
// FUN_06cffdc0 (Server_PumpPhantomEvents) dispatches; every pub::Phantom export returns
// -5 while it is set.
// ===========================================================================
namespace {

class ServerPhantom : public PhySys::CObjectCB
{
public:
    // LAB_06cffed0 (vtable +0x00, RET 4): a CSimple (class & 3) whose TypeBits (+0xe0)
    // share a bit with TypeMask has its id (+0xb0) appended to Entered.
    void added(CObject* Obj) override;
    // LAB_06cfff30 (vtable +0x04, RET 4, left undisassembled by Ghidra -- its bytes are
    // LAB_06cffed0's with +0x14 / +0x18 in place of +0x08 / +0x0c): the same test, onto Left.
    void deleted(CObject* Obj) override;

    std::list<unsigned int> Entered;   // +0x04 .. +0x0f
    std::list<unsigned int> Left;      // +0x10 .. +0x1b
    unsigned int TypeMask = 0;         // +0x1c
    void* PhyHandle = nullptr;         // +0x20
    unsigned int SystemId = 0;         // +0x24
    // FUN_06cfbd60 is the destructor: it empties Left (FUN_06d0f620 per node) and frees
    // its head, then does the same for Entered -- the reverse-declaration member
    // destruction order of the two lists above.
};

void ServerPhantom::added(CObject* Obj)
{
    if (Obj == nullptr) return;                                          // .text:06cffeda
    if ((static_cast<unsigned int>(Obj->ObjectClass) & 3u) != 3u) return; // .text:06cffee4
    if ((static_cast<CSimple*>(Obj)->TypeBits & TypeMask) == 0) return;  // .text:06cffeef
    Entered.push_back(static_cast<CSimple*>(Obj)->Id);                   // .text:06cffef6 .. 06cfff27
}

void ServerPhantom::deleted(CObject* Obj)
{
    if (Obj == nullptr) return;                                          // .text:06cfff3a
    if ((static_cast<unsigned int>(Obj->ObjectClass) & 3u) != 3u) return; // .text:06cfff44
    if ((static_cast<CSimple*>(Obj)->TypeBits & TypeMask) == 0) return;  // .text:06cfff4f
    Left.push_back(static_cast<CSimple*>(Obj)->Id);                      // .text:06cfff52 .. 06cfff87
}

std::list<ServerPhantom*> GPhantoms;   // .data:DAT_06d8d8f8 / DAT_06d8d8fc
bool GbPhantomPumpBusy = false;        // .data:DAT_06d8d928

// The linear search every export opens with (e.g. .text:06cfbce1 .. 06cfbd03): the first
// node whose value equals the handle, or end().
std::list<ServerPhantom*>::iterator FindPhantom(void* Handle)
{
    std::list<ServerPhantom*>::iterator It = GPhantoms.begin();
    while (It != GPhantoms.end() && *It != Handle)
        ++It;
    return It;
}

} // namespace

// FUN_06cffdc0 (.text:06cffdc0 .. 06cffec3), called once per server physics tick from
// FUN_06d001d0 (.text:06d001e2). With DAT_06d8d928 latched, for every phantom: each
// entered id -> FUN_06d43650(0x2d, {id, phantom, phantom->SystemId}), then
// FUN_06cf7720 (list::erase(begin, end)) on Entered; each left id -> the same with 0x2e,
// then Left is emptied node by node. The latch is cleared at the end.
void Server_PumpPhantomEvents()
{
    GbPhantomPumpBusy = true;                                            // .text:06cffdc9
    for (std::list<ServerPhantom*>::iterator It = GPhantoms.begin(); It != GPhantoms.end(); ++It) {
        ServerPhantom* const Ph = *It;                                   // .text:06cffde0
        for (std::list<unsigned int>::iterator Id = Ph->Entered.begin();
             Id != Ph->Entered.end(); ++Id) {
            unsigned int Event[3];
            Event[0] = *Id;                                              // .text:06cffdf7
            Event[1] = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Ph)); // .text:06cffdf3
            Event[2] = Ph->SystemId;                                     // .text:06cffe05
            Server_ControllerDispatch(0, pub::Controller::EMessage::PhantomEnter, Event); // .text:06cffe09
        }
        Ph->Entered.clear();                                             // .text:06cffe2f
        for (std::list<unsigned int>::iterator Id = Ph->Left.begin();
             Id != Ph->Left.end(); ++Id) {
            unsigned int Event[3];
            Event[0] = *Id;                                              // .text:06cffe47
            Event[1] = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Ph)); // .text:06cffe43
            Event[2] = Ph->SystemId;                                     // .text:06cffe55
            Server_ControllerDispatch(0, pub::Controller::EMessage::PhantomExit, Event); // .text:06cffe59
        }
        Ph->Left.clear();                                                // .text:06cffe6d .. 06cffea5
    }
    GbPhantomPumpBusy = false;                                           // .text:06cffeb8
}

namespace pub {
namespace SpaceObj {

// Retail SpaceObj::Dock calls SpaceObj::ActivateEquipment, which is defined further
// down this file; forward-declare it so the call order matches retail.
SERVER_API EQUIPMENT_RTYPE __cdecl ActivateEquipment(const unsigned int& obj_id_ref, EQInfo* eq_info);

// .data:DAT_06d8d868 -- the SpaceObj translation unit's own lazily filled
// I3DMathEngine (FUN_06d5ff70, Server_Acquire3DMathEngine). Three users in
// retail: pub::SpaceObj::Create (.text:06cf7e28 test, .text:06cf7e44 store),
// CreateSolar (.text:06cf8217 / .text:06cf822d) and GetEmptyPos
// (.text:06cf9e0c / .text:06cf9e24). Create and CreateSolar call slot +0x50,
// MatrixToQuaternion, on the caller's matrix (.text:06cf7e55, .text:06cf823e);
// no null test on the engine. GetEmptyPos calls slot +0x28, TransformXform
// (.text:06cf9e3d).
static I3DMathEngine* GSpaceObjMathEngine = nullptr;   // .data:DAT_06d8d868

static void SpaceObj_MatrixToQuat(Quaternion& Out, Matrix const& In)
{
    if (GSpaceObjMathEngine == nullptr)
        GSpaceObjMathEngine = Server_Acquire3DMathEngine();
    GSpaceObjMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Out),
                                            reinterpret_cast<FMatrix const&>(In));
}

// ---------------------------------------------------------------------------
// SpaceObj_BuildTransportLoadout -- retail FUN_06d5f990 (.text:06d5f990 ..
// .text:06d5fce0, cdecl, plain RET), sole caller pub::SpaceObj::Create
// .text:06cf7f56 with (ship archetype, &EquipIdMaker, &ShipInfo+0x5c, &request
// +0x70). Ported 1:1 2026-09-24 (claude-code:w5-server-06d4); the name comes
// from its own FDUMP text ("Transport loadout aborted").
//   .text:06d5f997 .. 06d5fa38  reserve(size + cargo node count): when the
//                   capacity (unsigned JNC) is short, operator new(max(need,0)
//                   * 0x20), _Ucopy, _Destroy, FUN_06d5ffb0 on the old buffer,
//                   Cap = new + need, End = new + old size, Begin = new
//   .text:06d5fa3b  hps = Arch->get_legal_hps(0xe = HpTypeCargoPod); null,
//                   no buffer or size 0 -> the plain branch at .text:06d5fc51
//   pod branch, at most the FIRST TWO cargo nodes (.text:06d5fc0f JG 1):
//     good = GoodList::find_by_id(GoodId); arch = good+0x50;
//     eq = Archetype::GetEquipment(arch); pod = eq vslot 8 (+0x20)
//     pod null -> FDUMP(0x100002, .rdata:06d6bb40, SrvMisc.cpp, 0x3a,
//                 eq->Nickname.get_string()) and return (no further nodes)
//     per = ftol((unsigned)Count / (int)hp count)
//     per hp: append {CreateEquipID, pod id (+8), count 1, status 1.0, hp,
//             equipped}; append {CreateEquipID, arch, status 1.0, hp, not
//             equipped, count per}; left -= per
//     the last element's count = left + per
//   plain branch, every node: append {CreateEquipID, Good2Arch(GoodId),
//     Count, status 1.0, make_internal(), not equipped}
// Each append is vector<EquipDesc>::insert(end, 1, EquipDesc()) followed by
// the setters on the new last element; here a default EquipDesc is appended
// through add_equipment_item(.., false), which never folds, and the setters
// then run on End[-1] in retail's order.
// ---------------------------------------------------------------------------
static void SpaceObj_AppendDefaultEquip(EquipDescVector& Out) {
    EquipDesc const Blank;                                          // EquipDesc::EquipDesc
    Out.add_equipment_item(Blank, false);                           // insert(end, 1, Blank)
}

static void SpaceObj_BuildTransportLoadout(Archetype::Ship const* Arch,
                                           SubObjectID::EquipIdMaker* IdMaker,
                                           OwnerList<CargoDesc> const* Cargo,
                                           EquipDescVector* Out) {
    // .text:06d5f997 .. .text:06d5f9d9 -- size, node count, capacity.
    unsigned int Need = (Out->Begin == nullptr) ? 0u
        : static_cast<unsigned int>(Out->End - Out->Begin);
    for (MetaNode<CargoDesc> const* Node = Cargo->get_head(); Node != nullptr; Node = Node->Next)
        ++Need;
    unsigned int const Capacity = (Out->Begin == nullptr) ? 0u
        : static_cast<unsigned int>(Out->Cap - Out->Begin);
    if (Capacity < Need) {                                          // .text:06d5f9db JNC
        int const AllocCount = static_cast<int>(Need) >= 0 ? static_cast<int>(Need) : 0; // JGE
        EquipDesc* const NewBuf = static_cast<EquipDesc*>(
            ::operator new(static_cast<size_t>(AllocCount) * sizeof(EquipDesc)));   // .text:06d5f9e9
        EquipDesc* Dst = NewBuf;
        for (EquipDesc* Src = Out->Begin; Src != Out->End; ++Src, ++Dst)
            new (Dst) EquipDesc(*Src);                              // _Ucopy .text:06d5f9fe
        // _Destroy (.text:06d5fa0e): EquipDesc's destructor is trivial.
        unsigned int const OldSize = (Out->Begin == nullptr) ? 0u
            : static_cast<unsigned int>(Out->End - Out->Begin);     // size() .text:06d5fa2a
        ::operator delete(Out->Begin);                              // FUN_06d5ffb0 .text:06d5fa18
        Out->Cap = NewBuf + Need;                                   // .text:06d5fa27
        Out->End = NewBuf + OldSize;                                // .text:06d5fa35
        Out->Begin = NewBuf;                                        // .text:06d5fa38
    }

    std::vector<CacheString> const* const Hps = Arch->get_legal_hps(HpTypeCargoPod); // .text:06d5fa41
    if (Hps != nullptr && !Hps->empty()) {                          // .text:06d5fa49 .. 06d5fa6a
        int const HpCount = static_cast<int>(Hps->size());          // [ESP+0x18]
        int Index = 0;                                              // [ESP+0x14]
        for (MetaNode<CargoDesc> const* Node = Cargo->get_head(); Node != nullptr;
             Node = Node->Next) {                                   // .text:06d5fa80 .. 06d5fa9b
            CargoDesc const* const Desc = Node->Data;
            GoodInfo const* const Good = GoodList::find_by_id(Desc->GoodId); // .text:06d5faa7
            unsigned int const ArchId = Good->ArchId;               // .text:06d5faad +0x50
            Archetype::Equipment* const Eq = Archetype::GetEquipment(ArchId); // .text:06d5fab5
            Archetype::CargoPod* const Pod = Eq->get_pod_appearance(); // .text:06d5fac4 vslot 8
            if (Pod == nullptr) {                                   // .text:06d5facd
                FDUMP(0x100002,
                      "%s(%d) : *** WARNING: Equipment(%s) has no pod_appearance.  Transport loadout aborted.",
                      "E:\\FL\\Scratch\\Source\\Server\\SrvMisc.cpp", 0x3a,
                      Eq->Nickname.get_string());                   // .text:06d5fc22 .. 06d5fc44
                return;
            }
            // .text:06d5fad3 .. 06d5faed: FILD of the count zero-extended to a
            // qword, FIDIV by the hp count, ftol. Both operands fit a double
            // exactly and ftol truncates, so this is the same integer.
            int const PerHp = static_cast<int>(
                static_cast<double>(Desc->Count) / static_cast<double>(HpCount));
            int Left = static_cast<int>(Desc->Count);               // [ESP+0x7c]
            for (std::vector<CacheString>::const_iterator Hp = Hps->begin();
                 Hp != Hps->end(); ++Hp) {                          // .text:06d5fafe .. 06d5fbf0
                SpaceObj_AppendDefaultEquip(*Out);                  // .text:06d5fb06 .. 06d5fb19
                EquipDesc* Last = Out->End - 1;                     // .text:06d5fb26
                Last->set_id(IdMaker->CreateEquipID());             // .text:06d5fb29 / 06d5fb32
                Last->set_arch_id(Pod->Nickname.Id);                // .text:06d5fb42 pod+8
                Last->set_count(1);                                 // .text:06d5fb4c
                Last->set_status(1.0f);                             // .text:06d5fb59
                Last->set_hardpoint(*Hp);                           // .text:06d5fb62
                Last->set_equipped(true);                           // .text:06d5fb6c

                SpaceObj_AppendDefaultEquip(*Out);                  // .text:06d5fb72 .. 06d5fb85
                Last = Out->End - 1;                                // .text:06d5fb92
                Last->set_id(IdMaker->CreateEquipID());             // .text:06d5fb95 / 06d5fb9e
                Last->set_arch_id(ArchId);                          // .text:06d5fbab
                Last->set_status(1.0f);                             // .text:06d5fbb8
                Last->set_hardpoint(*Hp);                           // .text:06d5fbc1
                Last->set_equipped(false);                          // .text:06d5fbcb
                Last->set_count(PerHp);                             // .text:06d5fbd4
                Left -= PerHp;                                      // .text:06d5fbe5
            }
            (Out->End - 1)->set_count(Left + PerHp);                // .text:06d5fbf8 .. 06d5fc05
            if (++Index > 1)                                        // .text:06d5fc0f .. 06d5fc17
                return;
        }
        return;
    }

    // .text:06d5fc51 .. .text:06d5fce0 -- no cargo-pod hardpoint: every node.
    for (MetaNode<CargoDesc> const* Node = Cargo->get_head(); Node != nullptr; Node = Node->Next) {
        CargoDesc const* const Desc = Node->Data;
        SpaceObj_AppendDefaultEquip(*Out);                          // .text:06d5fc6a .. 06d5fc7d
        EquipDesc* const Last = Out->End - 1;                       // .text:06d5fc83
        Last->set_id(IdMaker->CreateEquipID());                     // .text:06d5fc8b / 06d5fc94
        Last->set_arch_id(Good2Arch(Desc->GoodId));                 // .text:06d5fca0 / 06d5fcac
        Last->set_count(static_cast<int>(Desc->Count));             // .text:06d5fcbb
        Last->set_status(1.0f);                                     // .text:06d5fcc8
        Last->make_internal();                                      // .text:06d5fcd0
        Last->set_equipped(false);                                  // .text:06d5fcda
    }
}

// SpaceObj::Create -- retail .text:06cf7c90 .. 06cf80ca (ordinal 0x17c90, cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf). B = the ESP after the
// system lookup's argument pop; the result lives at [B+0xc] (seeded -1 at
// .text:06cf7d06), the FShipCreateRequest at [B+0x34], a local EquipIdMaker at
// [B+0x18], the CFLIDMaker pointer at [B+0x1c].
//   06cf7cac .. 06cf7cf5  CFLIDMaker::GetInstance null -> FDUMP(0x100001,
//                         "...pubSpace.cpp", 0x3f, "Failed to grab instance of global
//                         ID maker.\n"), return -1
//   06cf7cfe .. 06cf7d18  FUN_06d00280(info.SystemID) null -> return -1
//   06cf7d1f .. 06cf7d82  the request: FUN_06cf63f0, then CShipCreateInfo's inline
//                         part (+0xac/+0xb0/+0xb4/+0xd0/+0xd8 = 0, +0xd4 = -1,
//                         +0xdc = 0, vftable 06cf0410), EquipIdMaker ctor on +0xe0,
//                         +0xe4 = 0, +0xe8 = 0
//   06cf7d89 .. 06cf7ddf  the costume: +0x14 = info+0x70, +0x18 = +0x74, +0x1c =
//                         +0x78, +0x20 = +0x7c, +0x44 = count (info+0xa0, stored
//                         before the JLE), then `count` dwords info+0x80 -> +0x24
//   06cf7de1 .. 06cf7e36  +0xb4 = info+0xbc, +0x48 = info+0xa8, +0xb0 = info+0x54,
//                         +0x10 = info+0xa4, +0x08 = info+0x08, +0x4c = info+0x0c
//   06cf7e28 .. 06cf7e7d  DAT_06d8d868 (lazily FUN_06d5ff70) +0x50
//                         MatrixToQuaternion(&q, info+0x30); +0x58..+0x64 = q
//   06cf7e84 .. 06cf7f01  +0xb8 = info+0x24 (velocity), +0xc4 = info+0x18 (angular),
//                         +0x0c = info+0xac, +0x6c = info+0x58, +0xd0 = 0, +0xac = 0,
//                         +0x68 = info+0xb0, +0xe8 = info+0xb8
//   06cf7f08 .. 06cf7f1d  Archetype::GetShip(info+0x08)->
//                         get_undamaged_collision_group_list(&request+0x80); no null
//                         test on the archetype
//   06cf7f23 .. 06cf7f3f  local EquipIdMaker ctor; info+0x58 != 0 -> Loadout::Get's
//                         +0x14 word into it (no null test on the loadout)
//   06cf7f44 .. 06cf7f61  FUN_06d5f990(arch, &maker, &info+0x5c, &request+0x70);
//                         request+0xe0 = the maker's word
//   06cf7f60 .. 06cf7f73  FUN_06d43820(&info) false -> result -4 (06cf7fec)
//   06cf7f75 .. 06cf7f94  the maker's +4 counter, masked and post-incremented
//                         (CFLIDMaker::AllocateObjectID inlined) -> request+0x04;
//                         FUN_06d0a880 (StarSystem::create_ship)(&request), its
//                         return value unused
//   06cf7f99 .. 06cf7fa2  request+0x04 & 0x3fffffff == 0 -> the result stays -1
//   06cf7fa4 .. 06cf7fb8  *out_id = id; FUN_06cfc370 set<uint>::insert(*out_id) on
//                         DAT_06d8d834
//   06cf7fbd .. 06cf7fe7  result = 0; FUN_06d5c600 map<uint,uint>::insert({id, 0})
//                         on DAT_06d8d820, then the returned node's value = info+0x00
//   06cf7ff4 .. 06cf80a9  ~FShipCreateRequest inlined; 06cf80af return the result
SERVER_API int __cdecl Create(unsigned int& out_id, const SpaceObj::ShipInfo& info) {
    CFLIDMaker* const IdMaker = CFLIDMaker::GetInstance();              // .text:06cf7cac
    if (IdMaker == nullptr) {                                            // .text:06cf7cba
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\pubSpace.cpp", 0x3f,
              "Failed to grab instance of global ID maker.\n");          // .text:06cf7cbc .. 06cf7cd8
        return -1;                                                       // .text:06cf7cdd
    }
    int Result = -1;                                                     // .text:06cf7d06
    FStarSystem* const System = SysDB_LookupSystem(info.SystemID);       // .text:06cf7d0a
    if (System == nullptr) return Result;                                // .text:06cf7d18

    FShipCreateRequest Request;                                          // .text:06cf7d23 .. 06cf7d79
    Request.EquipStateData = nullptr;                                    // .text:06cf7d7b
    Request.MarkPlayerId = 0u;                                           // .text:06cf7d82
    Request.CostumeBody = info.CostumeBody;                              // .text:06cf7d92
    Request.CostumeHead = info.CostumeHead;                              // .text:06cf7d99
    Request.CostumeRightHand = info.CostumeRightHand;                    // .text:06cf7da0
    Request.CostumeLeftHand = info.CostumeLeftHand;                      // .text:06cf7da7
    Request.CostumeAccessoryCount = info.CostumeAccessoryCount;          // .text:06cf7dba
    for (int Index = 0; Index < Request.CostumeAccessoryCount; ++Index)  // .text:06cf7dbe / 06cf7ddf
        Request.CostumeAccessory[Index] = info.CostumeAccessory[Index];  // .text:06cf7dd3
    Request.ReputationRank = info.ReputationRank;                        // .text:06cf7df3
    Request.VoiceId = info.VoiceId;                                      // .text:06cf7dfa
    Request.VibeGroupId = info.VibeGroupId;                              // .text:06cf7e04
    Request.Field_0xb0 = info.Field_0x54;                                // .text:06cf7e0e
    Request.ArchetypeNickname = info.ShipArchID;                         // .text:06cf7e17
    Request.Position = info.Position;                                    // .text:06cf7e21 .. 06cf7e36
    Quaternion ShipOrient;
    SpaceObj_MatrixToQuat(ShipOrient, info.Orientation);                 // .text:06cf7e28 .. 06cf7e55
    Request.Orientation = ShipOrient;                                    // .text:06cf7e64 .. 06cf7e7d
    Request.Velocity = info.Velocity;                                    // .text:06cf7e8f .. 06cf7ea2
    Request.AngularVelocity = info.AngularVelocity;                      // .text:06cf7eaf .. 06cf7ecd
    Request.DockingBaseId = info.DockingBaseId;                          // .text:06cf7ec6
    Request.Field_0x6c = info.LoadoutID;                                 // .text:06cf7eda
    Request.Throttle = 0.0f;                                             // .text:06cf7ee8
    Request.OwnerPlayer = 0u;                                            // .text:06cf7ef3
    Request.HitPts = info.HitPts;                                        // .text:06cf7efa
    Request.MarkPlayerId = info.Field_0xb8;                              // .text:06cf7f01
    Archetype::Ship* const ShipArch = Archetype::GetShip(info.ShipArchID); // .text:06cf7f08
    ShipArch->get_undamaged_collision_group_list(Request.CollisionGroups); // .text:06cf7f1d

    SubObjectID::EquipIdMaker EquipIds;                                  // .text:06cf7f27
    if (info.LoadoutID != 0u) {                                          // .text:06cf7f2f
        Loadout::Map const* const LoadoutMap = Loadout::Get(info.LoadoutID); // .text:06cf7f32
        EquipIds = LoadoutMap->IdMaker;                                  // .text:06cf7f38 / 06cf7f3f word store
    }
    SpaceObj_BuildTransportLoadout(ShipArch, &EquipIds, &info.Cargo, &Request.Equipment); // .text:06cf7f56
    Request.EquipIds = EquipIds;                                         // .text:06cf7f5b / 06cf7f61

    if (!Server_ControllerExists(info.ControllerId)) {                   // .text:06cf7f69 / 06cf7f73
        Result = -4;                                                     // .text:06cf7fec
    } else {
        Request.ObjId = IdMaker->AllocateObjectID();                     // .text:06cf7f75 .. 06cf7f90
        System->create_ship(Request);                                    // .text:06cf7f94
        unsigned int const Id = Request.ObjId;                           // .text:06cf7f99
        if ((Id & 0x3fffffffu) != 0) {                                   // .text:06cf7f9d / 06cf7fa2
            out_id = Id;                                                 // .text:06cf7fb0
            GSpaceObjCreatedShips.insert(out_id);                        // .text:06cf7fb8
            Result = 0;                                                  // .text:06cf7fd0
            std::pair<std::map<unsigned int, unsigned int>::iterator, bool> const Inserted =
                GSpaceObjShipControllers.insert(
                    std::pair<unsigned int const, unsigned int>(Id, 0u)); // .text:06cf7fdc
            Inserted.first->second = info.ControllerId;                  // .text:06cf7fe1 .. 06cf7fe7
        }
    }
    return Result;                                                       // .text:06cf80b6
}

// SpaceObj::CreateLoot -- retail .text:06cf84a0 (cdecl, RET 0). One 0x6c-byte FLootRecord
// built on the stack from the LootInfo, handed to StarSystem::create_loot (FUN_06d08a40) in
// mode 4 with LootInfo+0x0c as the source object.
SERVER_API int __cdecl CreateLoot(unsigned int& out_id, const SpaceObj::LootInfo& loot_info) {
    // .text:06cf84a5-06cf84bd: FUN_06d00280 (the system map lookup); none -> -4.
    StarSystem* const Sys = SysDB_LookupSystem(loot_info.SystemID);
    if (Sys == nullptr) return -4;

    // .text:06cf84c5-06cf84ea: the record's first eight dwords zero (so no player to mark
    // and no name id until they are written below), +0x68 = 1, +0x69 = 0.
    FLootRecord Record = {};
    Record.Field_0x68 = 1;
    Record.bMission = 0;

    // .text:06cf84ee-06cf8510: the id maker's dynamic counter, post-incremented and masked
    // to 30 bits; the caller's out_id gets it before anything else is read. No null test on
    // the maker.
    unsigned int const LootId = CFLIDMaker::GetInstance()->AllocateObjectID();
    out_id = LootId;
    Record.Id = LootId;

    // .text:06cf850c-06cf8528: the contents (Archetype::GetEquipment, dereferenced without a
    // null test), their hit points (Root+0x1c) and the unit count (LootInfo+0x58).
    Archetype::Equipment* const Contents = Archetype::GetEquipment(loot_info.EquipArchID);
    Record.ContentsArch = Contents;
    Record.ContentsHitPts = Contents->HitPts;
    Record.Count = static_cast<int>(loot_info.Units);

    // .text:06cf852c-06cf854c: the crate is the contents' loot appearance (Equipment
    // vt+0x1c); its hit points (Root+0x1c) times LootInfo+0x5c, FMUL then FSTP float.
    Archetype::Equipment* const Appearance = Contents->get_loot_appearance();
    Record.Arch = Appearance;
    Record.HitPts = static_cast<float>(static_cast<double>(Appearance->HitPts) *
                                       static_cast<double>(loot_info.HitPtsScale));

    // .text:06cf853d-06cf85c6: the remaining fields, slot for slot.
    Record.Field_0x1c = loot_info.NameId;            // .text:06cf8543  record +0x1c
    Record.Velocity = loot_info.Velocity;            // .text:06cf8547  record +0x20
    Record.Position = loot_info.Position;            // .text:06cf8562  record +0x2c
    Record.Orientation = loot_info.Orientation;      // .text:06cf8579  REP MOVSD 9, record +0x38
    Record.AngularVelocity = loot_info.AngularVelocity; // .text:06cf8587 record +0x5c
    Record.Field_0x68 = loot_info.bCanAiTractor;     // .text:06cf8596  record +0x68
    Record.bMission = loot_info.bMission;            // .text:06cf85a3  record +0x69
    Record.Field_0x18 = loot_info.MarkForPlayer;     // .text:06cf85c0  record +0x18

    // .text:06cf859d-06cf85ca: create_loot(LootInfo+0x0c, 4, &Record, 1); always 0.
    Sys->create_loot(loot_info.OwnerObjId, 4, &Record, 1);
    return 0;
}

// SpaceObj::Destroy -- retail .text:06cf85f0 .. .text:06cf86f4 (ordinal 0x185f0, cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
//   06cf85f7 .. 06cf8603  (obj_id & 0x3fffffff) == 0 -> -4
//   06cf8609 .. 06cf8623  ESI = -2; FUN_06d00630(obj_id, &obj, &sys) false -> -2
//   06cf8629 .. 06cf8646  sys != 0: obj vtable +0x158 destroy(destroy_type, 0); ESI = 0
//   06cf8648 .. 06cf8652  sys == 0 (an in-transit object): FUN_06d00410
//                         (Server_DestroyInTransit(obj)); ESI stays -2
//   06cf8655 .. 06cf86a4  DAT_06d8d834 lower_bound(obj_id) (FUN_06d5c810), and when
//                         it is not end() and !(obj_id < key) erase it (FUN_06ce3ea0)
//   06cf86a4 .. 06cf86e9  the same on DAT_06d8d820 (FUN_06d59ea0 / FUN_06d58e50)
//   06cf86ee              return ESI
// Both tree erases run on both paths, keyed by obj_id, as std::set/map::erase(key).
SERVER_API int __cdecl Destroy(unsigned int obj_id, DestroyType destroy_type) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;                         // .text:06cf8603
    int Result = -2;                                                     // .text:06cf8614
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, System))                     // .text:06cf8619
        return Result;

    if (System != nullptr) {                                             // .text:06cf862d
        Obj->destroy(static_cast<int>(destroy_type), 0);                 // .text:06cf863e
        Result = 0;                                                      // .text:06cf8644
    } else {
        Server_DestroyInTransit(Obj);                                    // .text:06cf864d
    }

    GSpaceObjCreatedShips.erase(obj_id);                                 // .text:06cf8664 / 06cf869f
    GSpaceObjShipControllers.erase(obj_id);                              // .text:06cf86ae / 06cf86e9
    return Result;                                                       // .text:06cf86ee
}

// SpaceObj::DrainShields — drains shields on an object
// Retail (0x18930, Server.dll.c:18665-18728): mask check, FUN_06d00670(obj_id),
// type & 0x103 == 0x103 (else -1), build a stack DamageList whose +0x10 dword (the
// DamageCause) is 0x17, then call EqObj slot +0x1d8 (.text:06ce91e0: for every shield,
// CEquipTraverser(0x100), add a -1.0f shield entry via slot +0x214) followed by slot
// +0x188 (.text:06ceb330 EqObj / .text:06d01a10 Ship: apply the damage list -- the
// FDUMP at .text:06ceb330 names EqObj.cpp "Tried to apply damage ... within another
// damage callback") on the object itself, then destroy the list. 0x17 is a damage
// cause, not a message id — an older port passed it to an invented message 0x25.
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf), .text:06cf8930 .. 06cf8a73:
//   06cf894c .. 06cf8959  (obj_id & 0x3fffffff) == 0 -> -4
//   06cf895f .. 06cf896e  FUN_06d00670 (Server_LookupLiveObject); null -> -2
//   06cf8974 .. 06cf8997  CBase::Object null or (ObjectClass & 0x103) != 0x103 -> -1
//   06cf899d .. 06cf89d6  the stack DamageList, its ctor inlined (list head, count 0,
//                         bDestroyed 0, +0x14/+0x18 0) with +0x10 Cause = 0x17
//   06cf89de              wrapper vtable +0x1d8 (EqObj::drain_shields) on the object
//                         the lookup returned -- not re-narrowed after the class test
//   06cf89ed              wrapper vtable +0x188 (apply_damage_list)
//   06cf89f3 .. 06cf8a33  ~DamageList inlined; return 0
// 0x17 has no DamageCause enumerator in common_misc.h, so it is cast from the value.
SERVER_API int __cdecl DrainShields(unsigned int obj_id) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;                         // .text:06cf8959
    GameObject* const Obj = Server_LookupLiveObject(obj_id);             // .text:06cf8960
    if (Obj == nullptr) return -2;                                       // .text:06cf896e
    CObject* const Cobj = Obj->Object;                                   // .text:06cf897f
    if (Cobj == nullptr ||
        (static_cast<unsigned int>(Cobj->ObjectClass) & 0x103u) != 0x103u) return -1; // .text:06cf8997
    DamageList Damage;
    Damage.Cause = static_cast<DamageCause>(0x17);                       // .text:06cf89d6
    static_cast< ::EqObj*>(Obj)->drain_shields(Damage);                 // .text:06cf89de
    Obj->apply_damage_list(Damage);                                      // .text:06cf89ed
    return 0;                                                            // .text:06cf8a38
}

// SpaceObj::EnumerateCargo — iterates cargo on an object
// Retail: CEquipTraverser with type 0x10000, Arch2Good conversion, callback per item
SERVER_API int __cdecl EnumerateCargo(const unsigned int& obj_id, CargoEnumerator& enumerator) {
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id);
    if (!obj) return -2;
    ::EqObj* EqRw = dynamic_cast< ::EqObj*>(obj);
    CEqObj* Ceq = (EqRw != nullptr) ? EqRw->ceqobj() : nullptr;
    if (Ceq == nullptr) return 0; // nothing that can hold cargo

    // Retail (.text:06cf9680 .. .text:06cf96b9): CEquipTraverser(0x10000) over the
    // object's CEquipManager, Arch2Good on each item's archetype, and
    // `enumerator.notify_cargo(goodId, count, subObjId)` through the enumerator's
    // OWN vtable slot 0 -- not a function pointer out of the struct. Retail skips
    // an item outright when Arch2Good returns 0 (.text:06cf9692) and NEVER breaks
    // the walk on the callback's answer: the slot returns void and the loop runs
    // to the end of the traverser.
    CEquipTraverser Walk(EQUIP_TYPE_CARGO);
    CEquip* Item = Ceq->EquipManager.Traverse(Walk);
    while (Item != nullptr) {
        EquipDesc Desc;
        if (Item->GetEquipDesc(Desc)) {
            unsigned int const good_id = Arch2Good(Desc.ArchId);
            if (good_id != 0) {
                enumerator.notify_cargo(good_id, Desc.Count, Desc.Id);
            }
        }
        Item = Ceq->EquipManager.Traverse(Walk);
    }
    return 0;
}

// SpaceObj::FormationResponse -- retail .text:06cfa9f0 .. .text:06cfaa44 (cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
//   06cfa9f6 .. 06cfaa02  (*obj_id & 0x3fffffff) == 0 -> -4
//   06cfaa04 .. 06cfaa14  FUN_06d00670 (Server_LookupLiveObject); null -> -2
//   06cfaa16 .. 06cfaa25  __RTDynamicCast to Ship, not tested
//   06cfaa31 .. 06cfaa38  FUN_06d01e80 = Ship::request_returned(1, response_type, 0)
//   06cfaa3d              return 0
SERVER_API int __cdecl FormationResponse(const unsigned int& obj_id, FORMATION_RTYPE response_type) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;                         // .text:06cfaa02
    GameObject* const Obj = Server_LookupLiveObject(obj_id);             // .text:06cfaa0a
    if (Obj == nullptr) return -2;                                       // .text:06cfaa14
    ::Ship* const ShipRw = dynamic_cast< ::Ship*>(Obj);                 // .text:06cfaa25
    ShipRw->request_returned(1, static_cast<int>(response_type), 0);     // .text:06cfaa38
    return 0;
}

// SpaceObj::LaneResponse -- retail .text:06cfaa50 .. .text:06cfaaa4 (cdecl), the
// same body as FormationResponse with request type 2:
//   06cfaa56 .. 06cfaa62  mask -> -4; 06cfaa6a .. 06cfaa74  FUN_06d00670 null -> -2
//   06cfaa76 .. 06cfaa85  __RTDynamicCast to Ship, not tested
//   06cfaa91 .. 06cfaa98  Ship::request_returned(2, lane_param, 0); return 0
SERVER_API int __cdecl LaneResponse(const unsigned int& obj_id, int lane_param) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;                         // .text:06cfaa62
    GameObject* const Obj = Server_LookupLiveObject(obj_id);             // .text:06cfaa6a
    if (Obj == nullptr) return -2;                                       // .text:06cfaa74
    ::Ship* const ShipRw = dynamic_cast< ::Ship*>(Obj);                 // .text:06cfaa85
    ShipRw->request_returned(2, lane_param, 0);                          // .text:06cfaa98
    return 0;
}

// SpaceObj::DockRequest — initiates dock request with a target
// Retail (0x1a6b0) is four instructions plus a tail call — it does NOT mask-check
// obj_id and it does NOT return -4/-2:
//   piVar1 = FUN_06d00670(&obj_id);
//   if (piVar1 == 0) return 0;
//   return (**(code **)(piVar1[1] + 0x5c))(0, &target_id, &DAT_06d66b04, 1, 0);
// piVar1[1] is the object's SECOND vtable (IObjAffect at +0x04), slot +0x5c.
SERVER_API int __cdecl DockRequest(const unsigned int& obj_id, const unsigned int& target_id) {
    GameObject* Obj = Server_LookupLiveObject(obj_id);
    if (!Obj) return 0;
    // .text:06cfa6c3-06cfa6d9: IObjAffect slot 23 (common_iface.h), tail-called with
    // (0, &target_id, &<zero dword>, 1, nullptr). The third argument is the address of
    // .rdata:06d66b04, four zero bytes.
    static unsigned const ZeroSecondId = 0;   // .rdata:06d66b04
    return Obj->slot_5c(0, &target_id, &ZeroSecondId, 1, nullptr);
}

// SpaceObj::Dock -- retail .text:06cfa6e0 .. .text:06cfa8cb (ordinal 0x1a6e0, cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
//   06cfa6e7 .. 06cfa6f8  (*ship_id & 0x3fffffff) == 0 -> -4
//   06cfa6fe .. 06cfa717  result = -2; obj = FUN_06d00670(*ship_id)
//                         (Server_LookupLiveObject); null -> straight to 06cfa854
//   06cfa71d .. 06cfa74d  __RTDynamicCast to Ship, not tested; its byte +0x98 set ->
//                         FUN_06d01e80 (Ship::request_returned(0, response, dock_port)),
//                         result = 0
//   06cfa755 .. 06cfa780  EQInfo {Type 3, byte +4 = (response == 4 || response == 3)};
//                         ActivateEquipment(ship_id, &info), its result ignored
//   06cfa783              response == 3 -> return result
//   06cfa78c .. 06cfa7af  the object's CBase::Object (+0x10) under (& 0x503) == 0x503
//                         (a CShip), else to 06cfa854
//   06cfa7b5 .. 06cfa84a  each 8-byte entry of CShip [+0x1f8, +0x1fc) (the end re-read
//                         every pass): entry non-null and entry-8 non-null ->
//                         GetBehaviorManager(entry-8) non-null -> target =
//                         FUN_06d00670(*target_id) non-null -> block {*target_id,
//                         target get_type (+0x88), *ship_id, byte (dock_port != -1)},
//                         event {5, &block} -> the manager's slot 0 (LAB_062d3f90)
//   06cfa854 .. 06cfa859  response == 3 -> return result (reached from the null-obj
//                         and non-CShip exits too)
//   06cfa85b .. 06cfa86c  FUN_06d00670(*target_id) null -> -3
//   06cfa86e .. 06cfa882  __RTDynamicCast to EqObj, not tested
//   06cfa887 .. 06cfa8b2  dock_port >= 0 -> FUN_06ce7880 (EqObj::queue_dock_request(
//                         dock_port, *ship_id)), else FUN_06ce78d0
//                         (EqObj::dequeue_dock_request(dock_port, *ship_id), its bool
//                         ignored); return 0 either way
// EQInfo: retail writes only the dword at +0 and the byte at +4; the port zeroes the
// rest (deviations.md, zeroed indeterminate stack bytes).
SERVER_API int __cdecl Dock(const unsigned int& ship_id, const unsigned int& target_id,
                             int dock_port, DOCK_HOST_RESPONSE dock_host_response) {
    if ((ship_id & 0x3fffffffu) == 0) return -4;                        // .text:06cfa6f8
    int Result = -2;                                                     // .text:06cfa6ff
    GameObject* const Obj = Server_LookupLiveObject(ship_id);           // .text:06cfa707
    if (Obj != nullptr) {                                                // .text:06cfa717
        ::Ship* const ShipRw = dynamic_cast< ::Ship*>(Obj);             // .text:06cfa72c
        if (ShipRw->Field_0x98 != 0) {                                   // .text:06cfa73c
            ShipRw->request_returned(0, static_cast<int>(dock_host_response), dock_port); // .text:06cfa748
            Result = 0;                                                  // .text:06cfa74d
        }

        EQInfo DockEquip;
        memset(&DockEquip, 0, sizeof(DockEquip));
        DockEquip.Type = 3;                                              // .text:06cfa75c
        DockEquip.EquipId = (dock_host_response == 4 || dock_host_response == 3) ? 1 : 0; // .text:06cfa769 / 06cfa770 (byte)
        ActivateEquipment(ship_id, &DockEquip);                          // .text:06cfa77b

        if (dock_host_response == 3) return Result;                      // .text:06cfa786

        CObject* const Raw = Obj->Object;                                // .text:06cfa797
        if (Raw != nullptr &&
            (static_cast<unsigned int>(Raw->ObjectClass) & 0x503u) == 0x503u) { // .text:06cfa7af
            CShip* const Cship = static_cast<CShip*>(Raw);
            for (std::vector<FObjRWWatcher>::const_iterator Entry = Cship->FollowFollowers.begin();
                 Entry != Cship->FollowFollowers.end(); ++Entry) {      // .text:06cfa7bb / 06cfa848
                IObjRW* const Follower = Entry->get();                   // .text:06cfa7cb..06cfa7cd
                if (Follower == nullptr) continue;                       // .text:06cfa7d2
                IBehaviorManager* const Manager = GetBehaviorManager(Follower); // .text:06cfa7d5
                if (Manager == nullptr) continue;                        // .text:06cfa7e2
                GameObject* const Target = Server_LookupLiveObject(target_id); // .text:06cfa7eb
                if (Target == nullptr) continue;                         // .text:06cfa7f5

                // The block at [ESP+0x28] and the event at [ESP+0x14]; LAB_062d3f90's
                // case 5 reads [+0] / [+4] & 0x120 / [+8] / byte [+0xc].
                struct {
                    unsigned int TargetId;   // +0x00  .text:06cfa80b
                    unsigned int TargetType; // +0x04  .text:06cfa814 (get_type out)
                    unsigned int ShipId;     // +0x08  .text:06cfa801
                    unsigned char bHasPort;  // +0x0C  .text:06cfa82c
                    unsigned char Pad0d[3];
                } Block;
                memset(&Block, 0, sizeof(Block));
                Block.ShipId = ship_id;
                Block.TargetId = target_id;
                Target->get_type(Block.TargetType);                      // .text:06cfa814
                Block.bHasPort = (dock_port != -1) ? 1 : 0;              // .text:06cfa821
                FBehaviorManagerEvent Event;
                Event.Type = 5;                                          // .text:06cfa835
                Event.Data = &Block;                                     // .text:06cfa824
                Manager->Slot00(&Event);                                 // .text:06cfa83d
            }
        }
    }
    if (dock_host_response == 3) return Result;                          // .text:06cfa859

    GameObject* const Host = Server_LookupLiveObject(target_id);         // .text:06cfa862
    if (Host == nullptr) return -3;                                      // .text:06cfa86c
    ::EqObj* const HostEq = dynamic_cast< ::EqObj*>(Host);              // .text:06cfa87d
    if (dock_port >= 0)                                                  // .text:06cfa88b
        HostEq->queue_dock_request(dock_port, ship_id);                  // .text:06cfa891
    else
        HostEq->dequeue_dock_request(dock_port, ship_id);                // .text:06cfa8a4
    return 0;                                                            // .text:06cfa897 / 06cfa8aa
}

// SpaceObj::InstantDock -- retail .text:06cfa8d0 .. .text:06cfa9e5 (ordinal 0x1a8d0, cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
//   06cfa8d1 .. 06cfa8f5  (*ship_id & 0x3fffffff) == 0 or (*target_id & 0x3fffffff) == 0
//                         -> -4; from here EBP = -2 is the only result
//   06cfa8fb .. 06cfa90b  FUN_06d00670(*ship_id) (Server_LookupLiveObject); null -> -2
//   06cfa911 .. 06cfa920  __RTDynamicCast to Ship (EDI), not tested
//   06cfa925 .. 06cfa944  host = 0, owner = 0, FUN_06d00630(*target_id, &host, &owner)
//                         (Server_FindLiveObject); its bool result is ignored
//   06cfa94d .. 06cfa968  __RTDynamicCast host -> EqObj (ESI); null -> -2
//   06cfa96a .. 06cfa97e  flags = its vtable +0x154 (get_type_bits);
//                         flags == (flags & 0xc40) ->
//   06cfa980 .. 06cfa991    owner->complete_dock(ship, eqobj) (FUN_06d0c970), -2
//   06cfa992 .. 06cfa9c9  FUN_06ce6870 = EqObj::ceqobj (CBase::Object under 0x103);
//                         its archetype (+0x88) docking sphere [dock_port] (vector at
//                         +0x88, 0x10-byte spheres, type +0xc, unchecked index):
//                         bLand = !(type == 3 || 4 || 5) (moor_small/medium/large)
//   06cfa9c9 .. 06cfa9da  owner->queue_dock(ship, eqobj, &ceq->DockingPointBaseId
//                         (+0x164), &bLand) (FUN_06d0d370)
//   06cfa9df              return -2 (always)
// The owner is not null-tested on either call; FUN_06d006c0 leaves it null for an
// in-transit host, which retail would dereference the same way.
SERVER_API int __cdecl InstantDock(const unsigned int& ship_id, const unsigned int& target_id,
                                    int dock_port) {
    if ((ship_id & 0x3fffffffu) == 0 || (target_id & 0x3fffffffu) == 0) return -4; // .text:06cfa8f5
    GameObject* const Obj = Server_LookupLiveObject(ship_id);           // .text:06cfa901
    if (Obj == nullptr) return -2;                                       // .text:06cfa90b

    ::Ship* const ShipRw = dynamic_cast< ::Ship*>(Obj);                 // .text:06cfa920
    GameObject* Host = nullptr;                                          // .text:06cfa93c
    StarSystem* Owner = nullptr;                                         // .text:06cfa934
    Server_FindLiveObject(target_id, Host, Owner);                       // .text:06cfa944
    ::EqObj* const HostEq = dynamic_cast< ::EqObj*>(Host);              // .text:06cfa95c
    if (HostEq == nullptr) return -2;                                    // .text:06cfa968

    unsigned int const Flags = HostEq->get_type_bits();                  // .text:06cfa96e
    if (Flags == (Flags & 0xc40u)) {                                     // .text:06cfa97e
        Owner->complete_dock(ShipRw, HostEq);                            // .text:06cfa986
        return -2;
    }

    CEqObj* const HostCeq = HostEq->ceqobj();                            // .text:06cfa994
    Archetype::EqObj const* const Arch =
        static_cast<Archetype::EqObj const*>(HostCeq->Archetype);        // .text:06cfa999
    unsigned int const Kind = Arch->DockingSpheres[dock_port].Type;      // .text:06cfa9ac
    bool const bLand = !(Kind == 3 || Kind == 4 || Kind == 5);           // .text:06cfa9b0 .. 06cfa9c4
    Owner->queue_dock(ShipRw, HostEq, &HostCeq->DockingPointBaseId, &bLand); // .text:06cfa9da
    return -2;                                                           // .text:06cfa9df
}

// The PhySys::CreateParms that pub::SpaceObj::JumpIn (.text:06cfa276 .. 06cfa2d7),
// Relocate (.text:06cfa3c3 .. 06cfa428) and Launch (.text:06cfa5c1 .. 06cfa61a) each
// build inline on their stack before a system transfer: Mass 1.0f, the two COMMON
// damping defaults, +0x14 = 0, Type 2, +0x1c = 0, bEnableCollisions 1, Name[0] = 0,
// bHasInertia 0, Inertia = 0. Retail leaves Name[1..7] and Pad27 unwritten; the port
// zeroes the whole block first (deviations.md, zeroed indeterminate stack bytes).
static void SpaceObj_InitTransferParms(PhySys::CreateParms& Parms)
{
    memset(&Parms, 0, sizeof(Parms));
    Parms.Mass = 1.0f;                                                   // 0x3f800000
    Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
    Parms.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
    Parms.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
    Parms.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
    Parms.CollisionGroupId = 0;
    Parms.Type = 2;
    Parms.bFlag1c = 0;
    Parms.bEnableCollisions = 1;
    Parms.Name[0] = 0;
    Parms.bHasInertia = 0;
    Parms.Inertia.x = 0.0f;
    Parms.Inertia.y = 0.0f;
    Parms.Inertia.z = 0.0f;
}

// SpaceObj::Launch -- retail .text:06cfa520 .. .text:06cfa6ab (ordinal 0x1a520, cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
//   06cfa529 .. 06cfa534  (*launch_obj_id & 0x3fffffff) == 0 -> -4
//   06cfa53a .. 06cfa54f  FUN_06d00630(*launch_obj_id, &obj, &sys) false -> -2
//   06cfa555 .. 06cfa570  FUN_06d00630(*dock_obj_id, &dock, &docksys) false -> -3
//                         (retail reuses the two parameter slots as the outs)
//   06cfa57f .. 06cfa5a4  __RTDynamicCast(dock -> EqObj) into EDI and
//                         __RTDynamicCast(obj -> Ship) into ESI; neither is tested
//   06cfa5b9 .. 06cfa5bb  docksys == sys -> straight to the launch
//   06cfa5c1 .. 06cfa61e  the default transfer parms; sys->remove_ship(ship, &parms)
//                         (FUN_06d0d000)
//   06cfa623 .. 06cfa676  pos = 0, orient = identity;
//                         docksys->add_ship(ship, &parms, pos, orient) (FUN_06d0d100)
//   06cfa67b .. 06cfa68c  docksys->launch(ship, eqobj, launch_type) (FUN_06d0cee0)
//   06cfa693              return 0
SERVER_API int __cdecl Launch(const unsigned int& launch_obj_id, const unsigned int& dock_obj_id,
                               int launch_type) {
    if ((launch_obj_id & 0x3fffffffu) == 0) return -4;                  // .text:06cfa534
    GameObject* LaunchObj = nullptr;
    StarSystem* LaunchSystem = nullptr;
    if (!Server_FindLiveObject(launch_obj_id, LaunchObj, LaunchSystem))  // .text:06cfa545
        return -2;
    GameObject* DockObj = nullptr;
    StarSystem* DockSystem = nullptr;
    if (!Server_FindLiveObject(dock_obj_id, DockObj, DockSystem))        // .text:06cfa566
        return -3;

    ::EqObj* const DockEq = dynamic_cast< ::EqObj*>(DockObj);           // .text:06cfa58c
    ::Ship* const ShipRw = dynamic_cast< ::Ship*>(LaunchObj);           // .text:06cfa5a4

    if (DockSystem != LaunchSystem) {                                    // .text:06cfa5b9
        PhySys::CreateParms Parms;
        SpaceObj_InitTransferParms(Parms);
        LaunchSystem->remove_ship(ShipRw, &Parms);                       // .text:06cfa61e
        Vector Pos;
        Pos.x = 0.0f;                                                    // .text:06cfa63a
        Pos.y = 0.0f;
        Pos.z = 0.0f;
        Matrix const Orient(1.0f, 0.0f, 0.0f,                            // .text:06cfa646 ..
                            0.0f, 1.0f, 0.0f,                            // .text:06cfa672
                            0.0f, 0.0f, 1.0f);
        DockSystem->add_ship(ShipRw, &Parms, Pos, Orient);               // .text:06cfa676
    }
    DockSystem->launch(ShipRw, DockEq, launch_type);                     // .text:06cfa68c
    return 0;
}

// SpaceObj::JumpIn -- retail .text:06cfa220 .. .text:06cfa369 (ordinal 0x1a220, cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
//   06cfa229 .. 06cfa234  (*ship_id & 0x3fffffff) == 0 -> -4
//   06cfa23a .. 06cfa24f  FUN_06d00630(*ship_id, &ship, &sys) false -> -2
//   06cfa255 .. 06cfa270  FUN_06d00630(*gate_id, &gate, &gatesys) false -> -3
//   06cfa276 .. 06cfa2d7  the default transfer parms
//   06cfa2db .. 06cfa2fb  sys == 0 -> FUN_06d00380 (Server_TakeInTransit(ship, parms));
//                         else gatesys != sys -> sys->remove_ship(ship, &parms)
//   06cfa2fe .. 06cfa30f  gate vtable +0x250 (Solar::ChooseSystemArrivalLocation)
//                         (ship, &pos, &orient) -- called on whatever the gate is
//   06cfa315 .. 06cfa32f  gatesys != sys -> gatesys->add_ship(ship, &parms, pos, orient)
//   06cfa334 .. 06cfa34b  gatesys->arrive_ship(ship, gate, pos, orient) (FUN_06d0cc00)
//   06cfa351              return 0
SERVER_API int __cdecl JumpIn(const unsigned int& ship_id, const unsigned int& gate_id) {
    if ((ship_id & 0x3fffffffu) == 0) return -4;                        // .text:06cfa234
    GameObject* ShipObj = nullptr;
    StarSystem* ShipSystem = nullptr;
    if (!Server_FindLiveObject(ship_id, ShipObj, ShipSystem))            // .text:06cfa245
        return -2;
    GameObject* GateObj = nullptr;
    StarSystem* GateSystem = nullptr;
    if (!Server_FindLiveObject(gate_id, GateObj, GateSystem))            // .text:06cfa266
        return -3;

    PhySys::CreateParms Parms;
    SpaceObj_InitTransferParms(Parms);
    if (ShipSystem == nullptr)                                           // .text:06cfa2db
        Server_TakeInTransit(ShipObj, Parms);                            // .text:06cfa2f6
    else if (GateSystem != ShipSystem)                                   // .text:06cfa2dd
        ShipSystem->remove_ship(ShipObj, &Parms);                        // .text:06cfa2e9

    Vector Pos;
    Matrix Orient;
    static_cast<Solar*>(GateObj)->ChooseSystemArrivalLocation(ShipObj, Pos, Orient); // .text:06cfa30f
    if (GateSystem != ShipSystem)                                        // .text:06cfa319
        GateSystem->add_ship(ShipObj, &Parms, Pos, Orient);             // .text:06cfa32f
    GateSystem->arrive_ship(ShipObj, GateObj, Pos, Orient);             // .text:06cfa34b
    return 0;
}

// SpaceObj::Relocate -- retail .text:06cfa370 .. .text:06cfa512 (ordinal 0x1a370, cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
//   06cfa376 .. 06cfa384  (*obj_id & 0x3fffffff) == 0 -> -4
//   06cfa38a .. 06cfa39f  FUN_06d00630(*obj_id, &obj, &sys) false -> -2
//   06cfa3a5 .. 06cfa3c1  target = (*system_id & 0x3fffffff) ? FUN_06d00280(*system_id)
//                         (SysDB_LookupSystem) : 0
//   06cfa3c3 .. 06cfa428  the default transfer parms
//   06cfa42c .. 06cfa455  sys == 0 -> FUN_06d00380 (Server_TakeInTransit(obj, parms));
//                         else target != sys -> sys->remove_ship(obj, &parms)
//   06cfa459 / 06cfa4f0   target == 0 -> FUN_06d00560 (Server_PushInTransit(obj,
//                         parms)), return 0
//   06cfa461 / 06cfa4b6   target == sys -> the object's raw CObject (CBase::Object
//                         under the (& 1) test, else null, NOT null-tested after)
//                         vtable +0x7c beam_object(pos, orient, false), return 0
//   06cfa465 .. 06cfa47b  else target->add_ship(obj, &parms, pos, orient)
//   06cfa480 .. 06cfa4a8  the 0x503-narrowed CShip, when there is one:
//                         CEqObj::set_control_exclusion(0); return 0
SERVER_API int __cdecl Relocate(const unsigned int& obj_id, const unsigned int& system_id,
                                 const Vector& pos, const Matrix& orient) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;                         // .text:06cfa384
    GameObject* Obj = nullptr;
    StarSystem* OwnSystem = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, OwnSystem))                  // .text:06cfa395
        return -2;

    StarSystem* TargetSystem = nullptr;
    if ((system_id & 0x3fffffffu) != 0)                                  // .text:06cfa3b1
        TargetSystem = SysDB_LookupSystem(system_id);                    // .text:06cfa3b9

    PhySys::CreateParms Parms;
    SpaceObj_InitTransferParms(Parms);
    if (OwnSystem == nullptr)                                            // .text:06cfa42c
        Server_TakeInTransit(Obj, Parms);                                // .text:06cfa44d
    else if (TargetSystem != OwnSystem)                                  // .text:06cfa42e
        OwnSystem->remove_ship(Obj, &Parms);                             // .text:06cfa43c

    if (TargetSystem == nullptr) {                                       // .text:06cfa459
        Server_PushInTransit(Obj, Parms);                                // .text:06cfa4fa
        return 0;
    }
    if (TargetSystem == OwnSystem) {                                     // .text:06cfa461
        CObject* Raw = Obj->Object;                 // .text:06cfa4c5
        if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 1u) != 1u)
            Raw = nullptr;                                               // .text:06cfa4d6
        Raw->beam_object(pos, orient, false);                            // .text:06cfa4e5
        return 0;
    }
    TargetSystem->add_ship(Obj, &Parms, pos, orient);                    // .text:06cfa47b
    CObject* const ShipRaw = Obj->Object;           // .text:06cfa48f
    if (ShipRaw != nullptr &&
        (static_cast<unsigned int>(ShipRaw->ObjectClass) & 0x503u) == 0x503u)
        static_cast<CShip*>(ShipRaw)->set_control_exclusion(0);          // .text:06cfa4a8
    return 0;
}

// SpaceObj::SendComm -- retail .text:06cf9230 .. .text:06cf9382 (ordinal 401, cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
//   06cf9230 .. 06cf9248  text_ids == 0 or text_count <= 0 (signed JLE) -> -4
//   06cf924e .. 06cf9286  (target_id & 0x3fffffff) != 0: FUN_06d006c0(&target_id,
//                         &obj, &sys); a miss zeroes sys. It is the SYSTEM that is
//                         kept and tested, not the object.
//   06cf9288 .. 06cf92bf  no system yet: (sender_id & 0x3fffffff) == 0 -> -4, else
//                         the same lookup on sender_id; no system -> -4
//   06cf92c5 .. 06cf9320  (sender_id & 0x3fffffff) != 0: FUN_06d00670(sender_id)
//                         (Server_LookupLiveObject); its CBase::Object (+0x10) when
//                         (ObjectClass & 0x103) == 0x103 (a CEqObj): voice_id == 0 ->
//                         CEqObj+0x13c, costume == 0 -> &CEqObj+0x108 (13 dwords), and
//                         comm_type is ZEROED (06cf9320 XOR EDI,EDI) whenever that
//                         branch is taken
//   06cf9322 .. 06cf9331  no costume -> -2
//   06cf9332 .. 06cf9367  sys->FUN_06d0db30(sender_id, target_id, voice, *costume (13
//                         dwords by value), comm_type, text_ids, text_count, mood,
//                         duration, interrupt) -- StarSystem::send_comm
//   06cf936c              return 0
SERVER_API int __cdecl SendComm(unsigned int sender_id, unsigned int target_id,
                                 unsigned int voice_id, const Costume* costume,
                                 unsigned int comm_type, unsigned int* text_ids,
                                 int text_count, unsigned int mood,
                                 float duration, bool interrupt) {
    if (text_ids == nullptr) return -4;                                   // .text:06cf923c
    if (text_count <= 0) return -4;                                       // .text:06cf9248

    GameObject* LookupObj = nullptr;
    StarSystem* System = nullptr;
    if ((target_id & 0x3fffffffu) != 0) {
        if (!Server_FindObject(target_id, LookupObj, System))             // .text:06cf926c
            System = nullptr;
    }
    if (System == nullptr) {
        if ((sender_id & 0x3fffffffu) == 0) return -4;                    // .text:06cf9290
        if (!Server_FindObject(sender_id, LookupObj, System))             // .text:06cf92a5
            System = nullptr;
        if (System == nullptr) return -4;                                 // .text:06cf92bf
    }

    unsigned int Voice = voice_id;                                        // .text:06cf92ce EBX
    const Costume* Cost = costume;                                        // .text:06cf92d2 ESI
    unsigned int CommType = comm_type;                                    // .text:06cf92d6 EDI
    if ((sender_id & 0x3fffffffu) != 0) {
        CBase* const SenderBase = Server_LookupLiveObject(sender_id);    // .text:06cf92dd
        if (SenderBase != nullptr) {
            CObject* const Cobj = SenderBase->Object;                      // .text:06cf92f0
            if (Cobj != nullptr &&
                (static_cast<unsigned int>(Cobj->ObjectClass) & 0x103u) == 0x103u) {
                CEqObj* const Eq = static_cast<CEqObj*>(Cobj);
                if (Voice == 0)
                    Voice = Eq->VoiceId;                                  // .text:06cf930c
                if (costume == nullptr)                                   // .text:06cf9316
                    Cost = &Eq->SpaceCostume;                             // .text:06cf931a (+0x108)
                CommType = 0;                                             // .text:06cf9320
            }
        }
    }
    if (Cost == nullptr) return -2;                                       // .text:06cf9326

    System->send_comm(sender_id, target_id, Voice, *Cost, CommType, text_ids, text_count,
                      mood, duration, interrupt);                         // .text:06cf9367
    return 0;
}

// SpaceObj::JettisonEquipment — jettisons equipment/cargo from ship
// Retail (0x19390): FUN_06d006c0 resolve object, vtable[1]+0x38 dispatch
//   Return value mapping: (result != -2) - 1 & 0xFFFFFFFD
SERVER_API int __cdecl JettisonEquipment(const unsigned int& obj_id,
                                          const unsigned short& equip_id, const int& quantity) {
    // Retail has NO `obj_id == 0` guard here: it goes straight to
    // FUN_06d006c0(&obj_id, &obj, &owner) and returns -2 when that fails.
    GameObject* obj = Server_LookupGameObject(obj_id);
    if (!obj) return -2;
    // .text:06cf93ba-06cf93dc: IObjAffect slot 14 (common_iface.h) with
    // (MOVZX *equip_id, *quantity, 0), then `(result != -2) - 1 & 0xfffffffd` -- 0 on
    // success, -3 when the handler reported -2. The previous port invented message id 0x1E.
    int const Result = obj->slot_38(equip_id, quantity, 0);
    return (Result != -2) ? 0 : -3;
}

// SpaceObj::GetArchetypeID — gets archetype ID from good ID
// Retail: simply calls Good2Arch on the input value
SERVER_API int __cdecl GetArchetypeID(const unsigned int& good_id, unsigned int& out_arch_id) {
    // Retail is three statements with no entry guard, and it writes the out-param on
    // the failure path too (Server.dll.c:19691-19702):
    //   uVar1 = Good2Arch(*param_1); *param_2 = uVar1;
    //   return (uVar1 != 0) - 1 & 0xfffffffe;      // 0 on success, -2 on failure
    unsigned int arch = Good2Arch(good_id);
    out_arch_id = arch;
    return (arch != 0) ? 0 : -2;
}

// SpaceObj::GetAtmosphereRange — gets atmosphere range for planets/suns
// Retail disassembly at 0x19800: checks type flags & 0x303, reads +0x1ac and +0x38
SERVER_API int __cdecl GetAtmosphereRange(const unsigned int& obj_id, float& out_range) {
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id);
    if (!obj) return -2;
    ::Solar* SolarRw = dynamic_cast< ::Solar*>(obj);
    CSolar* Cs = (SolarRw != nullptr) ? SolarRw->csolar() : nullptr;
    // Retail check: ObjectClass & 0x303 must equal 0x303 (planet or sun).
    if (Cs == nullptr) {
        out_range = 0.0f;
        return 0;
    }
    float atmo = Cs->AtmosphereRange;   // CSolar+0x1ac
    out_range = atmo;
    // Retail: if atmosphere > 0, subtract the object's radius (EngineObject+0x38).
    if (atmo > 0.0f) {
        out_range = atmo - Cs->Radius;
    }
    return 0;
}

// SpaceObj::GetScannerRange — gets scanner ranges from equipped scanner
// Retail: finds scanner equip (type 0x80000), checks active, reads radar/cargo scan ranges
SERVER_API int __cdecl GetScannerRange(unsigned int obj_id, int& radar_range, int& cargo_scan_range) {
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id);
    if (!obj) return -2;
    // Retail: (ObjectClass & 0x503) == 0x503, i.e. ships only.
    ::Ship* ShipRw = dynamic_cast< ::Ship*>(obj);
    CShip* Cs = (ShipRw != nullptr) ? ShipRw->cship() : nullptr;
    if (Cs == nullptr) return -1;
    // Retail: CEquipManager::FindFirst(cobj + 0xe4, 0x80000).
    CEquip const* Found = Cs->EquipManager.FindFirst(EQUIP_TYPE_SCANNER);
    CEScanner const* Scanner = dynamic_cast<CEScanner const*>(Found);
    if (Scanner == nullptr) {
        radar_range = 0;
        cargo_scan_range = 0;
        return -3;
    }
    // Retail gates -3 on the scanner's own vtable +0x08 predicate (CEquip slot 2,
    // IsFunctioning) being TRUE -- yes, TRUE means refuse.
    if (Scanner->IsFunctioning()) {
        radar_range = 0;
        cargo_scan_range = 0;
        return -3;
    }
    radar_range = (int)(long)Scanner->GetRadarRange();
    cargo_scan_range = (int)(long)Scanner->GetCargoScanRange();
    return 0;
}

// SpaceObj::GetType — gets object type flags
// Retail disassembly at 0x18da0: checks flags & 3 == 3, reads +0xe0. out_type = 0 comes
// first (.text:06cf8db5, before the id test), so every failure path returns it zeroed.
SERVER_API int __cdecl GetType(unsigned int obj_id, unsigned int& out_type) {
    out_type = 0;
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id);
    if (!obj) return -2;
    // Retail: (ObjectClass & 3) == 3, which is exactly GameObject::cobj()'s guard,
    // then read CSimple::TypeBits at +0xe0.
    CSimple* Cs = obj->csimple();
    if (Cs == nullptr) return 0;
    out_type = Cs->get_type();
    return 0;
}

// SpaceObj::Activate -- retail .text:06cf9ac0 .. .text:06cf9b3f (cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
//   06cf9ac9 .. 06cf9ad4  (*obj_id & 0x3fffffff) == 0 -> -4
//   06cf9ad6 .. 06cf9aeb  FUN_06d00630(*obj_id, &obj, &owner) (Server_FindLiveObject;
//                         obj in the reused param_1 slot) false -> -2
//   06cf9aed .. 06cf9af8  IObjInspect +0x88 get_type(&flags) on the object
//   06cf9afe .. 06cf9b14  activate clear and (flags & 0x400) -> 0
//   06cf9b15 .. 06cf9b24  owner->activate_object(obj, activate, param) (FUN_06d0d8a0)
//   06cf9b29 .. 06cf9b30  NEG/SBB/AND 3/ADD -3: true -> 0, false -> -3
// The owner system is used without a null test, as retail does.
SERVER_API int __cdecl Activate(const unsigned int& obj_id, bool activate, int param) {
    if ((obj_id & 0x3fffffffu) == 0) return -4;                         // .text:06cf9ad4
    GameObject* Obj = nullptr;
    StarSystem* Owner = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, Owner)) return -2;           // .text:06cf9ae1
    unsigned int Flags;
    Obj->get_type(Flags);                                                // .text:06cf9af8
    if (!activate && (Flags & 0x400u) != 0) return 0;                    // .text:06cf9b04 / 06cf9b0d
    return Owner->activate_object(Obj, activate, static_cast<unsigned int>(param)) ? 0 : -3; // .text:06cf9b24
}

// SpaceObj::ActivateEquipment -- retail .text:06cf93f0 .. .text:06cf95d4 (ordinal 0x193f0,
// cdecl); switch table at .text:06cf95d8.
// ?ActivateEquipment@SpaceObj@pub@@YA?AW4EQUIPMENT_RTYPE@@ABIPAUEQInfo@12@@Z
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf). EBP carries the result:
//   06cf9403 .. 06cf9408  EBP = -5; (*obj_id & 0x3fffffff) == 0 -> -5
//   06cf940e .. 06cf9432  EBP = -1; FUN_06d006c0(obj_id, &obj, &sys) false or obj == 0
//                         -> -1 (the system out is not used)
//   06cf9438 .. 06cf9447  info->Type > 3 (unsigned) -> -1
//   case 0  06cf944e      Count != 1 -> -1; else IObjAffect +0x50 (slot_50)(1, 1, 0);
//                         NEG/SBB/AND 0xfffffffb: non-zero -> -5, zero -> 0
//   case 1  06cf9476      Field_0x0c == 0 -> -1; FUN_06d00670(Field_0x0c) null -> -5;
//                         else the object's IObjInspect +0x120 (scan_cargo)(other,
//                         &CargoOut); non-zero -> -5, zero -> 0
//   case 2  06cf94b9      CBase::Object null or (class & 0x103) != 0x103 -> -1;
//                         CEquipManager::FindFirst(0x1000) null -> -2; else
//                         IObjAffect +0x4c (slot_4c)(&its SubObjId, byte +4, 0) -> 0
//   case 3  06cf9535      the same CEqObj test -> -1; EBP = 0; CEquipTraverser(1); for
//                         each CEquipManager::Traverse hit whose CELightEquip::LightArch()
//                         bDockingLight (+0x71) is set: slot_4c(&SubObjId, byte +4, 0);
//                         return 0
// The EQUIPMENT_RTYPE names are the port's; the values (-5/-2/-1/0) are retail's.
SERVER_API EQUIPMENT_RTYPE __cdecl ActivateEquipment(const unsigned int& obj_id_ref, EQInfo* eq_info) {
    if ((obj_id_ref & 0x3fffffffu) == 0) return EQUIPMENT_RTYPE_BAD_TYPE; // .text:06cf9408
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindObject(obj_id_ref, Obj, System))                     // .text:06cf941c
        return EQUIPMENT_RTYPE_INVALID;                         // .text:06cf9426
    if (Obj == nullptr) return EQUIPMENT_RTYPE_INVALID;          // .text:06cf9432

    switch (eq_info->Type) {                                             // .text:06cf9441 (JA -> -1)
    case 0:
        if (eq_info->Count != 1) return EQUIPMENT_RTYPE_INVALID; // .text:06cf9452
        return Obj->slot_50(true, true, 0) != 0 ? EQUIPMENT_RTYPE_BAD_TYPE
                                                 : EQUIPMENT_RTYPE_OK;                  // .text:06cf9464 .. 06cf946e
    case 1: {
        if (eq_info->Field_0x0c == 0) return EQUIPMENT_RTYPE_INVALID; // .text:06cf947b
        GameObject* const Other = Server_LookupLiveObject(eq_info->Field_0x0c); // .text:06cf9482
        if (Other == nullptr) return EQUIPMENT_RTYPE_BAD_TYPE;   // .text:06cf948c / 06cf94af
        EquipDescVector& Cargo = *reinterpret_cast<EquipDescVector*>(eq_info->CargoOut); // .text:06cf9490
        return Obj->scan_cargo(Other, Cargo) != 0 ? EQUIPMENT_RTYPE_BAD_TYPE
                                                   : EQUIPMENT_RTYPE_OK;                // .text:06cf9497 .. 06cf94a4
    }
    case 2: {
        CObject* const Raw = Obj->Object;                                // .text:06cf94c0
        if (Raw == nullptr ||
            (static_cast<unsigned int>(Raw->ObjectClass) & 0x103u) != 0x103u)
            return EQUIPMENT_RTYPE_INVALID;                     // .text:06cf94d8
        CEqObj* const Ceq = static_cast<CEqObj*>(Raw);
        CEquip const* const Launcher = Ceq->EquipManager.FindFirst(0x1000u); // .text:06cf94f0
        if (Launcher == nullptr) return EQUIPMENT_RTYPE_NOT_FOUND;  // .text:06cf94fb
        unsigned short const SubObjId = Launcher->Id;                    // .text:06cf950b
        Obj->slot_4c(&SubObjId, static_cast<unsigned char>(eq_info->EquipId) != 0, 0); // .text:06cf9526
        return EQUIPMENT_RTYPE_OK;                          // .text:06cf952a
    }
    case 3: {
        CObject* const Raw = Obj->Object;                                // .text:06cf953c
        if (Raw == nullptr ||
            (static_cast<unsigned int>(Raw->ObjectClass) & 0x103u) != 0x103u)
            return EQUIPMENT_RTYPE_INVALID;                     // .text:06cf9552
        CEqObj* const Ceq = static_cast<CEqObj*>(Raw);
        CEquipTraverser Walk(1);                                         // .text:06cf9567
        CEquip* Light = Ceq->EquipManager.Traverse(Walk);                // .text:06cf957a
        while (Light != nullptr) {                                       // .text:06cf9584 / 06cf95c9
            if (static_cast<CELightEquip*>(Light)->LightArch()->bDockingLight) { // .text:06cf9588 .. 06cf9593
                unsigned short const SubObjId = Light->Id;               // .text:06cf959a
                Obj->slot_4c(&SubObjId, static_cast<unsigned char>(eq_info->EquipId) != 0, 0); // .text:06cf95b5
            }
            Light = Ceq->EquipManager.Traverse(Walk);                    // .text:06cf95bf
        }
        return EQUIPMENT_RTYPE_OK;                          // .text:06cf95cd (EBP = 0)
    }
    default:
        return EQUIPMENT_RTYPE_INVALID;                         // .text:06cf95cb
    }
}

// SpaceObj::SetInvincible -- retail .text:06cf8700 .. .text:06cf87e0 (ordinal 419, cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf). ESI = -4 up front; both branches
// test obj_id against 0 as a whole dword (not the 0x3fffffff mask), set ESI = -2,
// FUN_06d00630 (Server_FindLiveObject) into the reused parameter slots, and return -2
// when the lookup fails or the object's StarSystem is null. Then the object's vtable
// +0x194 (GameObject::set_invincible) gets a 0x0c-byte FInvincibility on the stack:
//   invincible (06cf8749 .. 06cf8776): { Type 3, bProtect 1, bFromPlayers = !param3
//                                        (TEST/SETZ), MaxHpLoss = duration }
//   otherwise  (06cf87ad .. 06cf87d2): { Type 3, bProtect 0, bFromPlayers 0,
//                                        MaxHpLoss = 1.0f }
// and the result is 0. The two pad bytes (+6/+7) are never written in retail; the port
// zeroes them (deviations.md, zeroed indeterminate stack bytes).
SERVER_API int __cdecl SetInvincible(unsigned int obj_id, bool invincible, bool param3, float duration) {
    if (obj_id == 0) return -4;                                          // .text:06cf8717 / 06cf8787
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, System)) return -2;          // .text:06cf872d / 06cf8799
    if (System == nullptr) return -2;                                    // .text:06cf8743 / 06cf87ab

    FInvincibility Cmd;
    Cmd.Type = 3;                                                        // .text:06cf8766 / 06cf87b5
    Cmd.Pad06[0] = 0;
    Cmd.Pad06[1] = 0;
    if (invincible) {
        Cmd.bProtect = true;                                             // .text:06cf876e
        Cmd.bFromPlayers = !param3;                                      // .text:06cf8753 / 06cf8756
        Cmd.MaxHpLoss = duration;                                        // .text:06cf875a
    } else {
        Cmd.bProtect = false;                                            // .text:06cf87c2
        Cmd.bFromPlayers = false;                                        // .text:06cf87bd
        Cmd.MaxHpLoss = 1.0f;                                            // .text:06cf87c7
    }
    Obj->set_invincible(Cmd);                                            // .text:06cf8776 / 06cf87d2
    return 0;
}

// SpaceObj::AddImpulse — adds physics impulse to an object
// Retail (0x190b0, Server.dll.c:19163-19190): mask check, FUN_06d00670(*obj_id),
// type & 3 == 3, and then — only when the physics body at CObject+0x54 is non-null —
// PhySys::AddImpulseAtPoint(cobj, impulse, point). Returns 0 either way.
// The previous port invented message id 0x28 for this.
SERVER_API int __cdecl AddImpulse(const unsigned int& obj_id_ref, const Vector& impulse, const Vector& point) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    // Listing .text:06cf9069: ADD EAX,0xc / MOV EAX,[EAX+4] reaches CBase::Object,
    // [cobj+0x4c] & 3 == 3 zeroes it otherwise, then MOV ECX,[cobj+0x54] -- the physics
    // body -- gates the PhySys call.
    CObject* Cobj = obj->cobj();
    if (Cobj == nullptr) return 0;
    if (Cobj->BodyPtr != nullptr) {
        PhySys::AddImpulseAtPoint(Cobj, impulse, point);
    }
    return 0;
}

// SpaceObj::GetLocation — gets position and orientation of an object
// Retail (0x18732): FUN_06d00670 lookup, vtable[0] get_position, vtable[3] get_orientation
SERVER_API int __cdecl GetLocation(unsigned int obj_id, Vector& out_pos, Matrix& out_orient) {
    out_pos.x = 0.0f; out_pos.y = 0.0f; out_pos.z = 0.0f;
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id);
    if (!obj) return -2;
    // Retail: IObjInspect slot 0 (+0x00) get_position for the three dwords, then
    // slot 3 (+0x0c) get_transform and a nine-dword REP MOVSD off the result -- i.e.
    // the Transform's leading Matrix (Transform is Matrix Rotation + Vector
    // Translation, see common_csys.h). Slots 3 and 4 are ICF-folded in retail, so
    // that is the orientation.
    out_pos = obj->get_position();
    out_orient = obj->get_transform().Rotation;
    return 0;
}

// SpaceObj::GetSystem -- retail .text:06cf8e00 .. .text:06cf8e6c (cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
//   06cf8e15              *out = 0 first
//   06cf8e04 .. 06cf8e1b  (obj_id & 0x3fffffff) == 0 -> -4
//   06cf8e1d .. 06cf8e47  obj = sys = 0; FUN_06d00630(obj_id, &obj, &sys)
//                         (Server_FindLiveObject) false -> -2
//   06cf8e49 .. 06cf8e56  sys: *out = FUN_06d06cd0(sys) (SystemInfo vtable slot 0, the
//                         system's id -- ISystem::NicknameId in this port), return 0
//   06cf8e5d .. 06cf8e64  no sys (an in-transit object): *out = 0, return 0
SERVER_API int __cdecl GetSystem(unsigned int obj_id, unsigned int& out_system_id) {
    out_system_id = 0;                                                   // .text:06cf8e15
    if ((obj_id & 0x3fffffffu) == 0) return -4;                         // .text:06cf8e1b
    GameObject* Obj = nullptr;                                           // .text:06cf8e2d
    StarSystem* System = nullptr;                                        // .text:06cf8e35
    if (!Server_FindLiveObject(obj_id, Obj, System)) return -2;          // .text:06cf8e3d
    if (System == nullptr) {                                             // .text:06cf8e4f
        out_system_id = 0;                                               // .text:06cf8e5e
        return 0;
    }
    out_system_id = System->SystemInfo->NicknameId;                      // .text:06cf8e51 FUN_06d06cd0
    return 0;
}

// SpaceObj::GetHealth — gets current and max health of an object
// Retail (0x20596): FUN_06d00670 lookup, vtable[15] (+0x3C) get_health(current, max)
SERVER_API int __cdecl GetHealth(const unsigned int& obj_id_ref, float& out_current, float& out_max) {
    out_current = 0.0f; out_max = 0.0f;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id_ref);
    if (!obj) return -2;
    // Retail: ONE call to IObjInspect slot 15 (+0x3c), get_status(HitPts, MaxHitPts),
    // which fills both out-params.
    obj->get_status(out_current, out_max);
    return 0;
}

// SpaceObj::GetRelativeHealth — gets normalized health ratio (0.0-1.0)
// Retail (0x20551): FUN_06d00670 lookup, vtable[16] (+0x40) get_relative_health
// NOTE: retail only checks != 0, NOT the 0x3fffffff mask (unique among SpaceObj functions)
SERVER_API int __cdecl GetRelativeHealth(const unsigned int& obj_id_ref, float& out_ratio) {
    out_ratio = 0.0f;
    if (obj_id_ref == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id_ref);
    if (!obj) return -2;
    // Retail: IObjInspect slot 16 (+0x40), get_status(Health) -- the single-argument
    // overload, which is the relative one.
    obj->get_status(out_ratio);
    return 0;
}

// SpaceObj::ExistsAndAlive — checks if object exists and is alive
// Retail (0x18612): FUN_06d00670 lookup (includes alive check), returns 0 or -2
SERVER_API int __cdecl ExistsAndAlive(unsigned int obj_id) {
    if ((obj_id & 0x3fffffff) == 0) return -2;
    // The whole point of this export is the liveness half of FUN_06d00670 — a plain
    // registry hit is not enough.
    GameObject* obj = Server_LookupLiveObject(obj_id);
    if (!obj) return -2;
    return 0;
}

// SpaceObj::GetTarget — gets the target ID of a ship object
// Retail (0x19194): FUN_06d00670 lookup, type check (& 0x503 == 0x503), CShip::get_target
SERVER_API int __cdecl GetTarget(const unsigned int& obj_id_ref, unsigned int& out_target_id) {
    // Retail zeroes the out-param first and never inspects its incoming value; the
    // previous `if (!out_target_id) return -4;` read an uninitialised reference.
    out_target_id = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id_ref);
    if (!obj) return -2;
    // Retail: (ObjectClass & 0x503) == 0x503 then CShip::get_target. IObjInspect slot
    // 29 (+0x74) get_target hands back the target's IObjRW, whose id is slot 8.
    ::Ship* ShipRw = dynamic_cast< ::Ship*>(obj);
    if (ShipRw == nullptr || ShipRw->cship() == nullptr) return -1;
    IObjRW* Target = nullptr;
    obj->get_target(Target);
    if (Target != nullptr) {
        out_target_id = Target->get_id();
    }
    return 0;
}

// SpaceObj::CreateSolar -- retail .text:06cf80d0 .. 06cf83bb (ordinal 0x180d0, cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf). The request is a
// CSolarCreateInfo at [ESP+0x20]; FUN_06d0a200 (StarSystem::create_solar) builds the
// Solar from it.
//   06cf80f4 .. 06cf8107  FUN_06d00280(info.SystemID) (SysDB_LookupSystem) null -> -4
//                         (06cf839f, before the request exists)
//   06cf810d .. 06cf812f  the request: CEqObjCreateInfo ctor FUN_06cf63f0, then the
//                         inlined CSolarCreateInfo part (+0xac = 0, +0xad = 0,
//                         +0xb0 = -1, vftable .rdata:06d66b7c)
//   06cf814d / 06cf8153   ID_String::clear on +0xb8; +0xbc = 0
//   06cf815a .. 06cf8173  info.Nickname[0] == 0 -> result -4 (06cf82cf)
//   06cf8179 .. 06cf8187  id = CreateID(info.Nickname); *out_id = id
//   06cf8189 .. 06cf81cc  +0xb8 = id, +0x04 = id, +0x68 = HitPts, +0x0c = DockingBaseId,
//                         +0x08 = SolarArchID, +0x14/+0x18/+0x1c/+0x20 = the costume head
//   06cf81c9 .. 06cf81f7  +0x44 = accessory count (stored before the JLE); then `count`
//                         dwords into +0x24, the count re-read from +0x44 each pass
//   06cf81fa .. 06cf8222  +0x48 = VoiceId, +0x10 = VibeGroupId, +0x4c = Position
//   06cf8217 .. 06cf823e  DAT_06d8d868 (lazily FUN_06d5ff70) +0x50 MatrixToQuaternion(
//                         &q, &info.Orientation); +0x58..+0x64 = q
//   06cf8263 / 06cf826e   +0x6c = LoadoutID
//   06cf8266 .. 06cf8288  Archetype::GetSolar(SolarArchID)->
//                         get_undamaged_collision_group_list(&request+0x80) -- no null
//                         test on the archetype
//   06cf828e .. 06cf82b7  +0xbc = info+0xcc, +0xb4 = info+0x80 (byte), +0xac = 1,
//                         +0xad = 1
//   06cf82bf .. 06cf82cc  FUN_06d0a200(system, &request): result 0 when it returns a
//                         Solar, -1 when it returns null (NEG/SBB/NEG/DEC)
//   06cf82d4 .. 06cf8398  ~CSolarCreateInfo inlined; return the result
SERVER_API int __cdecl CreateSolar(unsigned int& out_id, const SpaceObj::SolarInfo& solar_info) {
    FStarSystem* const System = SysDB_LookupSystem(solar_info.SystemID); // .text:06cf80f9
    if (System == nullptr) return -4;                                    // .text:06cf8107

    CSolarCreateInfo Request;                                            // .text:06cf8111 .. 06cf812f
    Request.Field_0xb8.clear();                                          // .text:06cf814d
    Request.Field_0xbc = 0u;                                             // .text:06cf8153
    int Result;
    if (solar_info.Nickname[0] == '\0') {                               // .text:06cf8173
        Result = -4;                                                     // .text:06cf82cf
    } else {
        unsigned int const Id = CreateID(solar_info.Nickname);           // .text:06cf817a
        out_id = Id;                                                     // .text:06cf8187
        Request.Field_0xb8.Id = Id;                                      // .text:06cf818f
        Request.ObjId = Id;                                              // .text:06cf8196
        Request.HitPts = solar_info.HitPts;                              // .text:06cf81a0
        Request.DockingBaseId = solar_info.DockingBaseId;                // .text:06cf81aa
        Request.ArchetypeNickname = solar_info.SolarArchID;              // .text:06cf81b1
        Request.CostumeBody = solar_info.CostumeBody;                    // .text:06cf81b7
        Request.CostumeHead = solar_info.CostumeHead;                    // .text:06cf81be
        Request.CostumeRightHand = solar_info.CostumeRightHand;          // .text:06cf81c5
        Request.CostumeLeftHand = solar_info.CostumeLeftHand;            // .text:06cf81cc
        Request.CostumeAccessoryCount = solar_info.CostumeAccessoryCount; // .text:06cf81d7
        for (int Index = 0; Index < Request.CostumeAccessoryCount; ++Index) // .text:06cf81db / 06cf81f7
            Request.CostumeAccessory[Index] = solar_info.CostumeAccessory[Index]; // .text:06cf81e8
        Request.VoiceId = solar_info.VoiceId;                            // .text:06cf8203
        Request.VibeGroupId = solar_info.VibeGroupId;                    // .text:06cf8209
        Request.Position = solar_info.Position;                          // .text:06cf8213 .. 06cf8222
        Quaternion SolarOrient;
        SpaceObj_MatrixToQuat(SolarOrient, solar_info.Orientation);      // .text:06cf8217 .. 06cf823e
        Request.Orientation = SolarOrient;                               // .text:06cf8241 .. 06cf8267
        Request.Field_0x6c = solar_info.LoadoutID;                       // .text:06cf826e
        Archetype::Solar* const SolarArch = Archetype::GetSolar(solar_info.SolarArchID); // .text:06cf8275
        SolarArch->get_undamaged_collision_group_list(Request.CollisionGroups); // .text:06cf8288
        Request.Field_0xbc = solar_info.Field_0xcc;                      // .text:06cf829e
        Request.Field_0xb4 = solar_info.Field_0x80;                      // .text:06cf82a8
        Request.Field_0xac = 1u;                                         // .text:06cf82af
        Request.Field_0xad = 1u;                                         // .text:06cf82b7
        ::Solar* const Created = System->create_solar(Request);         // .text:06cf82bf
        Result = (Created != nullptr) ? 0 : -1;                          // .text:06cf82c4 .. 06cf82cc
    }
    return Result;                                                       // .text:06cf839b
}

// SpaceObj::GetMotion — gets linear and angular velocity of an object
// Retail (0x18b20): FUN_06d00670 lookup, vtable[1] (+0x04) get_linear_velocity, vtable[2] (+0x08) get_angular_velocity
SERVER_API int __cdecl GetMotion(unsigned int obj_id, Vector& out_linear, Vector& out_angular) {

    out_linear.x = 0; out_linear.y = 0; out_linear.z = 0;
    out_angular.x = 0; out_angular.y = 0; out_angular.z = 0;
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id);
    if (!obj) return -2;
    // Retail: IObjInspect slot 1 (+0x04) get_velocity and slot 2 (+0x08)
    // get_angular_velocity.
    out_linear = obj->get_velocity();
    out_angular = obj->get_angular_velocity();
    return 0;
}

// SpaceObj::GetRep — gets reputation vibe handle for a game object
// Retail (0x18ba0): FUN_06d00670 lookup, type check & 0x103 == 0x103, read +0x104.
// out_rep is written only on success (.text:06cf8bed); every failure path (-4 06cf8bb0,
// -2 06cf8bf8, -1 06cf8bf3) leaves it untouched -- Content callers preset it (often -1)
// and rely on that.
SERVER_API int __cdecl GetRep(unsigned int obj_id, int& out_rep) {
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id);
    if (!obj) return -2;
    // Retail: (ObjectClass & 0x103) == 0x103 then read CEqObj+0x104.
    ::EqObj* EqRw = dynamic_cast< ::EqObj*>(obj);
    CEqObj* Ceq = (EqRw != nullptr) ? EqRw->ceqobj() : nullptr;
    if (Ceq == nullptr) return -1;
    out_rep = (int)Ceq->VibeId;
    return 0;
}

// SpaceObj::GetSolarRep — gets solar reputation by verifying vibe handle
// Retail (0x18c00): Reputation::Vibe::Verify(param_1), if valid sets *param_2 = param_1
SERVER_API int __cdecl GetSolarRep(unsigned int vibe_id, int& out_rep) {
    bool valid = GlobalReputation::Vibe::Verify(vibe_id);
    if (valid) {
        out_rep = (int)vibe_id;
        return 0;
    }
    return -2;
}

// SpaceObj::GetSolarArchetypeID — gets archetype hash ID from a solar object
// Retail (0x18c30): FUN_06d00670 lookup, type check & 0x103, read *(*(obj+0x88) + 8)
SERVER_API int __cdecl GetSolarArchetypeID(unsigned int obj_id, unsigned int& out_arch_id) {
    out_arch_id = 0;
    if ((obj_id & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id);
    if (!obj) return -2;
    ::EqObj* EqRw = dynamic_cast< ::EqObj*>(obj);
    CEqObj* Ceq = (EqRw != nullptr) ? EqRw->ceqobj() : nullptr;
    if (Ceq == nullptr) return -1;
    // Retail reads the archetype hash at *(CObject+0x88) + 8, i.e. Root::Nickname.Id.
    if (Ceq->Archetype != nullptr) {
        out_arch_id = Ceq->Archetype->get_id();
    }
    return 0;
}

// SpaceObj::GetSolarArchetypeNickname — gets nickname string for a solar archetype
// Retail (0x18ca0): Archetype::GetSolar(arch_id), ID_String::get_string at solar+8, safe_strcpy
SERVER_API int __cdecl GetSolarArchetypeNickname(char* out_name, int max_len, unsigned int arch_id) {
    if (!out_name || max_len <= 0) return -4;
    // Look up solar archetype via Common.dll
    Archetype::Solar* solar = Archetype::GetSolar(arch_id);
    if (!solar) return -2;
    // Get nickname string from ID_String at solar+8 (Root::Nickname)
    const char* nick = solar->Nickname.get_string();
    if (!nick) return -2;
    // Safe string copy with length limit
    int len = 0;
    while (nick[len] != '\0') len++;
    if (len < max_len) {
        for (int i = 0; i <= len; i++) out_name[i] = nick[i];
        return 0;
    }
    return -1;
}

// SpaceObj::GetSolarParent — gets parent solar's FLID for a solar object
// Retail (0x18d00): FUN_06d00670 lookup, vtable+0x150 get_solar, check & 0x303,
//   CSolar::GetParentNickname, second lookup to resolve parent FLID
SERVER_API int __cdecl GetSolarParent(const unsigned int& obj_id_ref, unsigned int& out_parent_id) {
    // Retail zeroes the out-param first (Server.dll.c:20 `*param_2 = 0;`) and never
    // inspects its incoming value.
    out_parent_id = 0;
    if (obj_id_ref == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id_ref);
    if (!obj) return -2;
    // Retail: IObjInspect slot 84 (+0x150) cobject() to reach the CObject, check
    // (ObjectClass & 0x303) == 0x303, then CSolar::GetParentNickname.
    ::Solar* SolarRw = dynamic_cast< ::Solar*>(obj);
    CSolar* Cs = (SolarRw != nullptr) ? SolarRw->csolar() : nullptr;
    if (Cs == nullptr) return -1;
    ID_String const& Parent = Cs->GetParentNickname();
    if (Parent.IsEmpty()) return -3;
    // Retail then re-resolves the parent through a second FUN_06d00670 and takes its
    // IObjInspect slot 8 (+0x20) get_id. The nickname id IS the registry key here, so
    // the lookup doubles as the existence check.
    GameObject* ParentObj = Server_LookupGameObject(Parent.Id);
    if (ParentObj == nullptr) return -3;
    out_parent_id = ParentObj->get_id();
    return 0;
}

// SpaceObj::GetHardpoint — gets hardpoint position/orientation on an object
// Retail (0x18e70): FUN_06d00670 lookup, vtable+0x1c get_inner_object, HardpointPosition()
SERVER_API int __cdecl GetHardpoint(const unsigned int& obj_id_ref, const char* hp_name,
                                     Vector* out_pos, Matrix* out_orient) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id_ref);
    if (!obj) return -2;
    // Listing .text:06cf8ea9: CALL [vtable + 0x1c] -- IObjInspect slot 7, get_index,
    // which is the engine instance handle -- then COMMON.DLL::HardpointPosition with
    // it, and `return (-(bool)result & 3) - 3`, i.e. 0 on success and -3 on failure.
    bool bOk = HardpointPosition(obj->get_index(), hp_name, out_pos, out_orient);
    return bOk ? 0 : -3;
}

// SpaceObj::GetRadius — gets bounding radius and center of an object
// Retail (0x18ed0): FUN_06d00670 lookup, vtable+0x2c (+0xB0) get_bounding_sphere(radius, center)
SERVER_API int __cdecl GetRadius(const unsigned int& obj_id_ref, float& out_radius, Vector& out_center) {
    out_radius = 0.0f;
    out_center.x = 0; out_center.y = 0; out_center.z = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id_ref);
    if (!obj) return -2;
    // Retail: ONE call to IObjInspect slot 11 (+0x2c), get_physical_radius, which
    // fills the radius AND the sphere centre.
    obj->get_physical_radius(out_radius, out_center);
    return 0;
}

// SpaceObj::GetBurnRadius — gets engine burn radius and position
// Retail (0x18f60): FUN_06d00670 lookup, vtable[0] get_position, vtable+0x140 get_burn_radius
SERVER_API int __cdecl GetBurnRadius(const unsigned int& obj_id_ref, float& out_radius, Vector& out_center) {
    out_radius = 0.0f;
    out_center.x = 0; out_center.y = 0; out_center.z = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id_ref);
    if (!obj) return -2;
    // Listing .text:06cf8fce / 06cf8fe5: IObjInspect slot 0 (get_position) for the
    // centre, then slot 80 (+0x140) with the out float& -- which is
    // get_atmosphere_range. "Burn radius" and "atmosphere range" are the same slot.
    out_center = obj->get_position();
    obj->get_atmosphere_range(out_radius);
    return 0;
}

// SpaceObj::GetMass — gets mass of an object
// Retail (0x19000): FUN_06d00670 lookup, vtable+0x30 (+0xC0) get_mass
SERVER_API int __cdecl GetMass(const unsigned int& obj_id_ref, float& out_mass) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id_ref);
    if (!obj) return -2;
    // Retail: *param_2 = (float)vtable+0x30(), IObjInspect slot 12 get_mass.
    out_mass = obj->get_mass();
    return 0;
}

// SpaceObj::GetCenterOfMass — gets center of mass in world space
// Retail (0x19040): FUN_06d00670 lookup, type check & 3 == 3, vtable+0x68 get_center_of_mass
SERVER_API int __cdecl GetCenterOfMass(const unsigned int& obj_id_ref, Vector& out_center) {

    out_center.x = 0; out_center.y = 0; out_center.z = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id_ref);
    if (!obj) return -2;
    // CORRECTION (listing .text:06cf9040-06cf90a2): the +0x68 slot retail dispatches
    // here is on the CObject's OWN vtable, not IObjInspect's -- the code does
    // ADD EAX,0xc / MOV ECX,[EAX+4] to reach CBase::Object first, checks
    // [cobj+0x4c] & 3 == 3, and only then CALL [cobj_vtable + 0x68], which is
    // CObject::get_center_of_mass. (IObjInspect's own +0x68 is get_attitude_towards,
    // which takes two arguments and would make no sense here.) Retail then calls
    // through the pointer even when the type check zeroed it -- a genuine retail
    // null-deref path the port declines to reproduce.
    CObject* Cobj = obj->cobj();
    if (Cobj == nullptr) return -2;
    out_center = Cobj->get_center_of_mass();
    return 0;
}

// SpaceObj::GetVoiceID — gets voice ID for comm messages
// Retail (0x191a0): FUN_06d006c0 lookup (4-tier), FUN_06d00670 alive check,
//   type check & 0x103 == 0x103, read +0x13c
SERVER_API int __cdecl GetVoiceID(const unsigned int& obj_id_ref, unsigned int& out_voice_id) {
    // Retail's only entry guard is `if (*param_1 == 0) return -4;` — it never reads the
    // out-param, and it does NOT mask with 0x3fffffff here.
    if (obj_id_ref == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id_ref);
    if (!obj) return -2;
    ::EqObj* EqRw = dynamic_cast< ::EqObj*>(obj);
    CEqObj* Ceq = (EqRw != nullptr) ? EqRw->ceqobj() : nullptr;
    if (Ceq == nullptr) return -3;
    out_voice_id = Ceq->VoiceId;    // CEqObj+0x13c
    return 0;
}

// SpaceObj::GetInvincible — gets invincibility state of an object
// Retail06cf88a0..8922: live lookup and three sequential raw output stores.
SERVER_API int __cdecl GetInvincible(unsigned int ObjId, unsigned char& OutFromPlayers,
                                     unsigned char& OutProtect, float& OutMaxHpLoss) {
    if (ObjId == 0) return -4;
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindLiveObject(ObjId, Obj, System)) return -2;
    if (System == nullptr) return -2;
    OutFromPlayers = Obj->get_invincible_flag();  // +0x198 -> raw byte +0x39
    OutProtect = Obj->is_invincible();            // +0x19c -> raw byte +0x38
    OutMaxHpLoss = Obj->get_invincible_timer();   // +0x1a0 -> float +0x3c
    return 0;
}

// SpaceObj::GetCargoSpaceOccupied — gets cargo hold usage for a ship
// Retail .text:06cf96d0 (0x196d0, 39 instructions):
//   06cf96da  *out = 0, before anything else
//   06cf96e2  id & 0x3fffffff == 0 -> -4
//   06cf96f5  FUN_06d00670 (Server_LookupLiveObject); null -> -2
//   06cf9701 .. 06cf9720  [obj+0x10] narrowed on (ObjectClass & 0x503) == 0x503, else 0
//   06cf9722  MOV ESI,[ECX+0x88]  -- CObject::Archetype, NO null test on the narrowing
//   06cf9728  CALL CShip::get_cargo_hold_remaining
//   06cf972e  FSUBR float [ESI+0x100]  -- Archetype::Ship::HoldSize - remaining, unrounded
//   06cf9734  ftol -> *out; return 0
// The decompile hides the FSUBR; occupied = capacity - remaining, as the name says.
SERVER_API int __cdecl GetCargoSpaceOccupied(const unsigned int& obj_id_ref, unsigned int& out_occupied) {
    out_occupied = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);
    if (Obj == nullptr) return -2;
    CObject* const Raw = Obj->cobj();
    CShip* const ShipObj =
        (Raw != nullptr && ((unsigned int)Raw->ObjectClass & 0x503u) == 0x503u) ? (CShip*)Raw : nullptr;
    Archetype::Ship const* const Arch = (Archetype::Ship const*)ShipObj->Archetype;
    double const Remaining = ShipObj->get_cargo_hold_remaining();
    out_occupied = (unsigned int)(long long)(Arch->HoldSize - Remaining);
    return 0;
}

// SpaceObj::GetDockingTarget — gets the docking target FLID for an object
// Retail (0x19880): FUN_06d00670 lookup, type check & 0x103 == 0x103,
//   read +0x164 first, fallback to +0x160 if secondary is invalid FLID
SERVER_API int __cdecl GetDockingTarget(const unsigned int& obj_id_ref, unsigned int& out_target) {
    // Retail zeroes the out-param first and never inspects its incoming value.
    out_target = 0;
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    void* obj_raw = Server_LookupObject(obj_id_ref);
    if (!obj_raw) return -2;
    ::EqObj* EqRw = dynamic_cast< ::EqObj*>(Server_LookupGameObject(obj_id_ref));
    CEqObj* Ceq = (EqRw != nullptr) ? EqRw->ceqobj() : nullptr;
    if (Ceq == nullptr) return 0;
    // Retail prefers CEqObj+0x164 (DockingPointBaseId), falls back to +0x160
    // (DockingBaseId).
    unsigned int secondary = Ceq->DockingPointBaseId;
    if ((secondary & 0x3fffffff) != 0) {
        out_target = secondary;
    } else {
        out_target = Ceq->DockingBaseId;
    }
    return 0;
}

// SpaceObj::SetRelativeHealth -- retail .text:06cfaaf0 .. .text:06cfab4e (cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
//   06cfaaf0 .. 06cfab05  FCOMP ratio against .rdata:06d65188 (0.0f), TEST AH,0x1 / JNZ:
//                         -4 when ratio < 0 or unordered (C0 is set for NaN too)
//   06cfab07 .. 06cfab16  FCOMP against .rdata:06d65520 (1.0f), TEST AH,0x41 / JP: -4
//                         when C0 == C3, i.e. ratio > 1 or unordered
//   06cfab18 .. 06cfab20  *obj_id == 0 as a whole dword (no 0x3fffffff mask) -> -4
//   06cfab22 .. 06cfab32  FUN_06d00670 (Server_LookupLiveObject); null -> -2
//   06cfab34 .. 06cfab3d  the wrapper's vtable +0x1ac set_relative_health(ratio)
//   06cfab43              return 0
SERVER_API int __cdecl SetRelativeHealth(const unsigned int& obj_id_ref, float ratio) {
    if (!(ratio >= 0.0f)) return -4;                                     // .text:06cfab05
    if (!(ratio <= 1.0f)) return -4;                                     // .text:06cfab16
    if (obj_id_ref == 0) return -4;                                      // .text:06cfab20
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);         // .text:06cfab28
    if (Obj == nullptr) return -2;                                       // .text:06cfab32
    Obj->set_relative_health(ratio);                                     // .text:06cfab3d
    return 0;
}

// SpaceObj::GetShieldHealth — gets shield current/max health and active state
// Retail (0x1abb0): FUN_06d00670 lookup, vtable+0x44 (+0x110) get_shield_health(cur, max, active)
SERVER_API int __cdecl GetShieldHealth(const unsigned int& obj_id_ref, float& out_current,
                                        float& out_max, bool& out_active) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupGameObject(obj_id_ref);
    if (!obj) return -2;
    // Retail dispatches ONE slot, vtable +0x44 (IObjInspect slot 17,
    // get_shield_status(cur, max, active)), which fills all three out-params.
    obj->get_shield_status(out_current, out_max, out_active);
    return 0;
}

// SpaceObj::GetTradelaneNextAndPrev — gets next/prev tradelane ring IDs for a tradelane object
// Retail (0x1ac00): FUN_06d00670 lookup, type check & 0x303 == 0x303, read +0x1dc/+0x1d8
SERVER_API int __cdecl GetTradelaneNextAndPrev(const unsigned int& obj_id_ref,
                                                unsigned int& out_next, unsigned int& out_prev) {

    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    void* obj_raw = Server_LookupObject(obj_id_ref);
    if (!obj_raw) return -2;
    ::Solar* SolarRw = dynamic_cast< ::Solar*>(Server_LookupGameObject(obj_id_ref));
    CSolar* Cs = (SolarRw != nullptr) ? SolarRw->csolar() : nullptr;
    if (Cs == nullptr) return -1;
    out_next = Cs->NextRing;   // CSolar+0x1dc
    out_prev = Cs->PrevRing;   // CSolar+0x1d8
    return 0;
}

// SpaceObj::LightFuse -- retail .text:06cfac70 .. .text:06cfacdf (cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
//   06cfac74 .. 06cfac7f  *obj_id == 0 as a whole dword (no mask) -> -4
//   06cfac81 .. 06cfac93  FUN_06d00670 (Server_LookupLiveObject); null -> -2
//   06cfac95 .. 06cfacb5  ID_String { CreateID(fuse_name) } in the reused param_1 slot
//   06cfacad .. 06cfacc8  wrapper vtable +0x1e4 light_fuse(0, id, SUBOBJ_ID_NONE,
//                         delay, -1.0f) on the looked-up object (EqObj's slot; the
//                         wrapper is not re-narrowed)
//   06cfacce .. 06cfacd7  NEG/SBB/AND 4/ADD -4: found 0, else -4
SERVER_API int __cdecl LightFuse(const unsigned int& obj_id_ref, const char* fuse_name, float delay) {
    if (obj_id_ref == 0) return -4;                                      // .text:06cfac7f
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);         // .text:06cfac87
    if (Obj == nullptr) return -2;                                       // .text:06cfac93
    ID_String FuseId;
    FuseId.Id = CreateID(fuse_name);                                     // .text:06cfac9a / 06cfacb5
    bool const bLit = static_cast< ::EqObj*>(Obj)->light_fuse(0, FuseId, SUBOBJ_ID_NONE,
                                                              delay, -1.0f); // .text:06cfacc8
    return bLit ? 0 : -4;                                                // .text:06cfacce .. 06cfacd7
}

// SpaceObj::GetTerminalInfo -- retail .text:06cf9900 .. .text:06cf999d (cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
//   06cf9906 .. 06cf9911  (*obj_id & 0x3fffffff) == 0 -> -2 (ESI = -2 is the only
//                         failure value before the archetype)
//   06cf9917 .. 06cf9922  FUN_06d00670 (Server_LookupLiveObject); null -> -2
//   06cf9924 .. 06cf9941  CBase::Object null or (ObjectClass & 0x103) != 0x103 -> -2
//   06cf9943 .. 06cf9964  its archetype (+0x88, not null-tested) is an Archetype::EqObj;
//                         the docking-sphere vector (+0x88 begin / +0x8c end, 0x10-byte
//                         elements): null begin -> -3, index >= size (unsigned) -> -3.
//                         A null begin only occurs for an empty vector, so both tests
//                         are the port's single size test.
//   06cf9966 .. 06cf998b  safe_strcpy(out.Name, 0x20, sphere+4) -- the sphere's anim
//                         name (Common EqObj::read value(1), .text:062f18e5); out.Value
//                         (+0x20) = sphere+0xc, its type; return 0
SERVER_API int __cdecl GetTerminalInfo(const unsigned int& obj_id_ref, int index, TerminalInfo& out_info) {
    if ((obj_id_ref & 0x3fffffffu) == 0) return -2;                     // .text:06cf9911
    GameObject* const Obj = Server_LookupLiveObject(obj_id_ref);         // .text:06cf9918
    if (Obj == nullptr) return -2;                                       // .text:06cf9922
    CObject* const Cobj = Obj->Object;                                   // .text:06cf992b
    if (Cobj == nullptr ||
        (static_cast<unsigned int>(Cobj->ObjectClass) & 0x103u) != 0x103u) return -2; // .text:06cf9941
    Archetype::EqObj const* const Arch =
        static_cast<Archetype::EqObj const*>(Cobj->Archetype);           // .text:06cf9943
    if (static_cast<unsigned int>(index) >= Arch->DockingSpheres.size()) return -3; // .text:06cf9951 / 06cf9964
    Archetype::EqObj::DockingSphere const& Sphere = Arch->DockingSpheres[index];
    safe_strcpy(out_info.Name, 0x20, Sphere.HardpointName.c_str());     // .text:06cf9978..06cf997f (record +0x04)
    out_info.Value = Sphere.Type;                                        // .text:06cf998b
    return 0;
}

// SpaceObj::GetGoodID — gets good ID from a loot container's cargo archetype
// Retail (0x199a0): FUN_06d00670 lookup, type & 0x803 == 0x803 (loot),
//   reads *(obj+0xec) → inner ptr → *(ptr+8) archetype hash → Arch2Good
SERVER_API int __cdecl GetGoodID(const unsigned int& obj_id_ref, unsigned int& out_good_id) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -2;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    // Retail: (ObjectClass & 0x803) == 0x803, then CObject+0xec -- which is
    // CLoot::ContentsArch, the pre-resolved Archetype::Equipment pointer -- and the
    // archetype hash at +8 on it (Root::Nickname.Id).
    ::Loot* LootRw = dynamic_cast< ::Loot*>(obj);
    CLoot* Cl = (LootRw != nullptr) ? LootRw->cloot() : nullptr;
    if (Cl == nullptr) return -1;
    if (Cl->ContentsArch == nullptr) return -1;
    out_good_id = Arch2Good(Cl->ContentsArch->get_id());
    return 0;
}

// SpaceObj::GetJumpTarget — gets jump target system/gate IDs from a planet/sun
// Retail (0x19a40): FUN_06d00670 lookup, type & 0x303 == 0x303 (planet/sun),
//   ObjectType check (& 0xc40), read +0x1b4/+0x1b8
SERVER_API int __cdecl GetJumpTarget(const unsigned int& obj_id_ref, unsigned int& out_target1, unsigned int& out_target2) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    GameObject* obj = Server_LookupLiveObject(obj_id_ref);
    if (!obj) return -2;
    ::Solar* SolarRw = dynamic_cast< ::Solar*>(obj);
    CSolar* Cs = (SolarRw != nullptr) ? SolarRw->csolar() : nullptr;
    if (Cs == nullptr) return -1;
    // Check the object's type bits (CSimple::TypeBits, CObject+0xe0): only bits inside
    // 0xc40 may be set.
    unsigned int otype = Cs->get_type();
    if ((otype & 0xc40) != otype) return -1;
    out_target1 = Cs->DestSystem;    // CSolar+0x1b4
    out_target2 = Cs->DestGate.Id;   // CSolar+0x1b8 (ID_String)
    return 0;
}

// SpaceObj::SetInvincible2 -- retail .text:06cf87f0 .. .text:06cf8863 (ordinal 418, cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
//   06cf87f7 .. 06cf87ff  obj_id == 0 (whole dword) -> -4
//   06cf8801 .. 06cf8823  FUN_06d00630(obj_id, &obj, &sys) false, or sys == 0 -> -2
//   06cf8825 .. 06cf8850  FInvincibility { Type 3, bProtect = invincible, bFromPlayers =
//                         param3, MaxHpLoss = duration } -> obj vtable +0x194
//                         (GameObject::set_invincible); return 0
// Unlike SetInvincible there is no branch and param3 is not inverted. Pad bytes zeroed
// by the port (never written in retail).
SERVER_API int __cdecl SetInvincible2(unsigned int obj_id, unsigned char invincible, unsigned char param3, float duration) {
    if (obj_id == 0) return -4;                                          // .text:06cf87ff
    GameObject* Obj = nullptr;
    StarSystem* System = nullptr;
    if (!Server_FindLiveObject(obj_id, Obj, System)) return -2;          // .text:06cf8811
    if (System == nullptr) return -2;                                    // .text:06cf8823

    FInvincibility Cmd;
    Cmd.Type = 3;                                                        // .text:06cf8841
    Cmd.bProtect = invincible;                                           // .text:06cf8831
    Cmd.bFromPlayers = param3;                                           // .text:06cf8849
    Cmd.Pad06[0] = 0;
    Cmd.Pad06[1] = 0;
    Cmd.MaxHpLoss = duration;                                            // .text:06cf8839
    Obj->set_invincible(Cmd);                                            // .text:06cf8850
    return 0;
}

// SpaceObj::RequestSpaceScript — requests a space script execution at a position
// Retail (0x19b40): copies Vector + int + string to stack, dispatches via DAT_06d8fb14 vtable+0xf8
SERVER_API int __cdecl RequestSpaceScript(const unsigned int& obj_id_ref, const Vector& pos,
                                          const int& priority, unsigned int client_id, const char* script_name) {
    if ((obj_id_ref & 0x3fffffff) == 0) return -4;
    // Retail packs one 0x118-byte record on the stack and hands it to
    // IClient::SendServerRequestSpaceScript (+0xf8, FLPACKET_SERVER_REQUESTSPACESCRIPT) as
    // (client_id, &record) -- the fourth argument is the ClientId the packet goes to (it was
    // misnamed `script_type` until 2026-10-16; Content's Act_CallThorn passes the mission's
    // player id there). The layout is
    // FServerRequestSpaceScriptInfo (rpc_iclient_args.h): +0x000 id (06cf9b62), +0x108..
    // +0x110 the Vector (06cf9b6c / 06cf9b89 / 06cf9b7a), +0x114 = priority (06cf9b9c),
    // +0x004 safe_strcpy(.., 0x104, script_name) (06cf9ba3). No test of the client
    // interface or of the name (06cf9ba9 MOV ECX,[DAT_06d8fb14] / 06cf9bc1 CALL [EDX+0xf8]).
    // (Until 2026-09-26 the port used {id, Vector, int, name} here, which put the name where
    // rpclocal's sender 0x06d8aa40 reads the position.)
    FServerRequestSpaceScriptInfo Payload;
    Payload.Field_0x000 = obj_id_ref;
    memcpy(&Payload.Field_0x108, &pos.x, sizeof(float));
    memcpy(&Payload.Field_0x10c, &pos.y, sizeof(float));
    memcpy(&Payload.Field_0x110, &pos.z, sizeof(float));
    Payload.Field_0x114 = static_cast<unsigned int>(priority);
    safe_strcpy(Payload.Name, static_cast<int>(kRequestSpaceScriptNameSize), script_name);
    ::g_pClientInterface->SendServerRequestSpaceScript(client_id, Payload);
    return 0;
}

// SpaceObj::GetEmptyPos -- retail .text:06cf9bd0 .. .text:06cfa04d (ordinal 0x19bd0, cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf). Frame 0x109c bytes, 8-aligned; the
// 0x400-dword id buffer sits at [ESP1+0xa8].
//   06cf9be0 .. 06cf9c11  *start_radius < 0 or *clearance < 0 (FCOMP 0.0f, TEST AH,1 --
//                         NaN counts as below) -> -4
//   06cf9c17              R = *start_radius
//   06cf9c20 .. 06cf9c9d  OUTER: Count = sys->scan_objects(buf, 0x400, &xform.Translation,
//                         R, 0xffffffff) when SysDB_LookupSystem(*system_id) is non-null;
//                         Count == 0 (or no system) -> a second lookup and, when it
//                         succeeds, Count = scan_objects(buf, 0x400, &xform.Translation,
//                         -1.0f, 2); otherwise Count = 0
//   06cf9ca1              Attempts = 0
//   06cf9ca5 .. 06cf9cb3  INNER: *selection_type > 3 (unsigned) -> return result
//                         (-4 on the first pass)
//     case 0  06cf9cba    a = rand() * 2^-15 * 6.2831854820251465; offset (R cos a,
//                         R sin a, 0)
//     case 1  06cf9d03    (0, R cos a, R sin a)
//     case 2  06cf9d4c    (R cos a, 0, R sin a)
//     case 3  06cf9d92    u1 = rand()*2^-15, u2 = rand()*2^-15 (doubles), then
//                         (rand()*2^-15*R, R*u2, R*u1)
//   06cf9e08 .. 06cf9e5f  DAT_06d8d868 (lazily FUN_06d5ff70) ->TransformXform (+0x28)
//                         (&cand, &xform, &offset); *out_pos = cand
//   06cf9e62 .. 06cf9e6f  bEmpty = 1, result = 0; Count == 0 -> the tail
//   06cf9e75 .. 06cf9f9a  each id (unsigned index < Count): Bits = the live object's
//                         CSimple::TypeBits (+0xe0) when its CBase::Object is a CSimple
//                         (& 3), else 0; R_obj = 0, V = 0, then (only for a live id)
//                         Bits & 2 -> V = get_position(), get_atmosphere_range(R_obj);
//                         else get_physical_radius(R_obj, V) (+0x2c).
//                         (sqrt((dz*dz + dy*dy) + dx*dx) - R_obj) - *clearance
//                         compared with 0.0f: < or == -> collision
//   06cfa00e .. 06cfa027  collision: Attempts++, bEmpty = 0, result = -1;
//                         Attempts < 10 -> INNER
//   06cfa02d .. 06cfa03d  R = R + R; !bEmpty -> OUTER
//   06cfa043              return result
// x87 arithmetic is reproduced in double (the process's 53-bit precision control).
SERVER_API int __cdecl GetEmptyPos(const unsigned int& system_id_ref, const Transform& base_transform,
                                   const float& start_radius, const float& clearance,
                                   const PosSelectionType& selection_type, Vector& out_pos) {
    int Result = -4;                                                     // .text:06cf9bee
    if (!(start_radius >= 0.0f)) return Result;                          // .text:06cf9bfb
    if (!(clearance >= 0.0f)) return Result;                             // .text:06cf9c11
    float Radius = start_radius;                                         // .text:06cf9c19
    unsigned int Ids[0x400];                                             // [ESP1+0xa8]
    bool bEmpty = false;
    do {
        // OUTER (.text:06cf9c20).
        unsigned int Count = 0;
        StarSystem* System = SysDB_LookupSystem(system_id_ref);          // .text:06cf9c26
        if (System != nullptr)
            Count = System->scan_objects(Ids, 0x400, &base_transform.Translation, Radius,
                                         0xffffffffu);                   // .text:06cf9c51
        if (Count == 0) {                                                // .text:06cf9c5c
            float const NoRadius = -1.0f;                             // .text:06cf9c67 (.rdata:06d66b14)
            Count = 0;                                                   // .text:06cf9c6b
            System = SysDB_LookupSystem(system_id_ref);                  // .text:06cf9c6f
            if (System != nullptr)
                Count = System->scan_objects(Ids, 0x400, &base_transform.Translation,
                                             NoRadius, 2);            // .text:06cf9c98
        }
        int Attempts = 0;                                                // .text:06cf9ca1
        for (;;) {
            // INNER (.text:06cf9ca5).
            unsigned int const Selection = static_cast<unsigned int>(selection_type);
            if (Selection > 3u) return Result;                           // .text:06cf9cad
            Vector Offset;
            double const Step = 3.0517578125e-05;                        // .rdata:06d656b8 (float 2^-15)
            double const TwoPi = 6.2831854820251465;                     // .rdata:06d66b80
            if (Selection == 3u) {
                double const U1 = static_cast<double>(rand()) * Step;    // .text:06cf9da8
                double const U2 = static_cast<double>(rand()) * Step;    // .text:06cf9dbf
                Offset.x = static_cast<float>(static_cast<double>(rand()) * Step *
                                              static_cast<double>(Radius)); // .text:06cf9dda
                Offset.y = static_cast<float>(static_cast<double>(Radius) * U2); // .text:06cf9ded
                Offset.z = static_cast<float>(static_cast<double>(Radius) * U1); // .text:06cf9e00
            } else {
                double const Angle = static_cast<double>(rand()) * Step * TwoPi; // .text:06cf9cd4 / 06cf9cda
                float const C = static_cast<float>(std::cos(Angle) * static_cast<double>(Radius)); // FCOS
                float const S = static_cast<float>(std::sin(Angle) * static_cast<double>(Radius)); // FSIN
                if (Selection == 0u) {                                   // .text:06cf9cba
                    Offset.x = C;
                    Offset.y = S;
                    Offset.z = 0.0f;
                } else if (Selection == 1u) {                            // .text:06cf9d03
                    Offset.x = 0.0f;
                    Offset.y = C;
                    Offset.z = S;
                } else {                                                 // .text:06cf9d4c
                    Offset.x = C;
                    Offset.y = 0.0f;
                    Offset.z = S;
                }
            }

            if (GSpaceObjMathEngine == nullptr)                          // .text:06cf9e1d
                GSpaceObjMathEngine = Server_Acquire3DMathEngine();      // .text:06cf9e1f
            Vector Candidate;
            GSpaceObjMathEngine->TransformXform(reinterpret_cast<FVector&>(Candidate),
                                                reinterpret_cast<FTransform const&>(base_transform),
                                                reinterpret_cast<FVector const&>(Offset)); // .text:06cf9e3d
            out_pos = Candidate;                                         // .text:06cf9e53 .. 06cf9e5f

            bEmpty = true;                                               // .text:06cf9e66
            Result = 0;                                                  // .text:06cf9e6b
            bool bCollided = false;
            for (unsigned int Index = 0; Index < Count; ++Index) {       // .text:06cf9e6f / 06cf9f9a
                unsigned int const Id = Ids[Index];                      // .text:06cf9e75
                unsigned int Bits = 0;                                   // .text:06cf9e7c
                if ((Id & 0x3fffffffu) != 0) {
                    GameObject* const Probe = Server_LookupLiveObject(Id); // .text:06cf9e86
                    if (Probe != nullptr) {
                        CObject* const Raw = Probe->Object;              // .text:06cf9e99
                        if (Raw != nullptr &&
                            (static_cast<unsigned int>(Raw->ObjectClass) & 3u) == 3u)
                            Bits = static_cast<CSimple*>(Raw)->TypeBits; // .text:06cf9eab
                    }
                }
                float OtherRadius = 0.0f;                                // .text:06cf9eb4
                Vector Other;
                Other.x = 0.0f;
                Other.y = 0.0f;
                Other.z = 0.0f;
                if ((Id & 0x3fffffffu) != 0) {                           // .text:06cf9f10 / 06cf9fe1
                    GameObject* const Obj = Server_LookupLiveObject(Id); // .text:06cf9f13 / 06cf9fe8
                    if (Obj != nullptr) {
                        if ((Bits & 2u) != 0) {                          // .text:06cf9ebc
                            Other = Obj->get_position();                 // .text:06cf9f25
                            Obj->get_atmosphere_range(OtherRadius);      // .text:06cf9f44
                        } else {
                            Obj->get_physical_radius(OtherRadius, Other); // .text:06cfa006
                        }
                    }
                }
                double const Dx = static_cast<double>(out_pos.x) - Other.x; // .text:06cf9f52
                double const Dy = static_cast<double>(out_pos.y) - Other.y; // .text:06cf9f59
                double const Dz = static_cast<double>(out_pos.z) - Other.z; // .text:06cf9f60
                double const Dist = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx); // .text:06cf9f64 .. 06cf9f74
                double const Gap = (Dist - static_cast<double>(OtherRadius))
                                   - static_cast<double>(clearance);      // .text:06cf9f80 / 06cf9f82
                if (Gap <= 0.0) {                                        // .text:06cf9f84 .. 06cf9f91
                    bCollided = true;
                    break;
                }
            }
            if (!bCollided)
                break;                                                   // .text:06cf9fa0 -> 06cfa02d
            ++Attempts;                                                  // .text:06cfa012
            bEmpty = false;                                              // .text:06cfa016
            Result = -1;                                                 // .text:06cfa01b
            if (Attempts >= 10)                                          // .text:06cfa027
                break;
        }
        Radius = static_cast<float>(static_cast<double>(Radius) + static_cast<double>(Radius)); // .text:06cfa037
    } while (!bEmpty);                                                   // .text:06cfa03d
    return Result;                                                       // .text:06cfa043
}

// SpaceObj::IsPosEmpty -- retail .text:06cfa060 .. .text:06cfa21d (ordinal 0x1a060, cdecl).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf). Frame: 0x1024 bytes (__chkstk
// FUN_06d60160), a 0x400-dword id buffer at [ESP0+0x24].
//   06cfa082              *out_empty = true
//   06cfa085 .. 06cfa0a2  R = *radius; result = -1; FUN_06d00280(*system_id)
//                         (SysDB_LookupSystem) null -> *out_empty = true, return -1
//   06cfa0a8 .. 06cfa0cf  Count1 = sys->scan_objects(buf, 0x400, &pos, R, 0xffffffff)
//                         (FUN_06d0bcd0); non-zero -> not empty
//   06cfa0d5 .. 06cfa0eb  R = -1.0f (.rdata:06d66b14); the SAME system is looked up
//                         again; null -> the exit with Count2 = 0 (empty)
//   06cfa0f1 .. 06cfa110  Count2 = sys->scan_objects(buf, 0x400, &pos, -1.0f, 2)
//   06cfa116 .. 06cfa1ce  for each id (unsigned index < Count2): V = (0,0,0), R = 0 each
//                         pass; a live object (id & 0x3fffffff, FUN_06d00670) gives
//                         V = get_position() (slot 0) and R via get_atmosphere_range
//                         (+0x140, return ignored); sqrt((dz*dz + dy*dy) + dx*dx) - R
//                         compared with 0.0f (.rdata:06d65188): < or == -> hit, stop
//   06cfa1db .. 06cfa21d  Count1 != 0 or a hit -> *out_empty = false; otherwise
//                         *out_empty = true; return 0
// x87 arithmetic: retail runs at the process's 53-bit precision control, which the
// double arithmetic below reproduces step for step (the same treatment as AcceptTrade /
// send_comm).
SERVER_API int __cdecl IsPosEmpty(const unsigned int& system_id_ref, const Vector& pos,
                                  const float& radius, bool& out_empty) {
    out_empty = true;                                                    // .text:06cfa082
    float SearchRadius = radius;                                         // .text:06cfa094
    int Result = -1;                                                     // .text:06cfa08a
    bool bHit = false;                                                   // .text:06cfa08f
    unsigned int Count2 = 0;                                             // .text:06cfa08d
    StarSystem* System = SysDB_LookupSystem(system_id_ref);              // .text:06cfa098
    if (System == nullptr) {                                             // .text:06cfa0a2
        out_empty = true;                                                // .text:06cfa1fd
        return Result;
    }
    unsigned int Ids[0x400];                                             // [ESP0+0x24]
    unsigned int const Count1 =
        System->scan_objects(Ids, 0x400, &pos, SearchRadius, 0xffffffffu); // .text:06cfa0c4
    if (Count1 == 0) {                                                   // .text:06cfa0cf
        SearchRadius = -1.0f;                                            // .text:06cfa0dd
        System = SysDB_LookupSystem(system_id_ref);                      // .text:06cfa0e1
        if (System != nullptr) {                                         // .text:06cfa0eb
            Count2 = System->scan_objects(Ids, 0x400, &pos, SearchRadius, 2); // .text:06cfa105
            for (unsigned int Index = 0; Index < Count2; ++Index) {      // .text:06cfa110 / 06cfa1ce
                Vector Other;
                Other.x = 0.0f;                                          // .text:06cfa141 (from 06cfa116)
                Other.y = 0.0f;                                          // .text:06cfa145
                Other.z = 0.0f;                                          // .text:06cfa12c
                float OtherRadius = 0.0f;                                // .text:06cfa139
                unsigned int const Id = Ids[Index];                      // .text:06cfa130
                if ((Id & 0x3fffffffu) != 0) {                           // .text:06cfa149
                    GameObject* const Obj = Server_LookupLiveObject(Id); // .text:06cfa14c
                    if (Obj != nullptr) {                                // .text:06cfa158
                        Other = Obj->get_position();                     // .text:06cfa15e
                        Obj->get_atmosphere_range(OtherRadius);          // .text:06cfa17d
                    }
                }
                double const Dx = static_cast<double>(pos.x) - Other.x;  // .text:06cfa18c
                double const Dy = static_cast<double>(pos.y) - Other.y;  // .text:06cfa193
                double const Dz = static_cast<double>(pos.z) - Other.z;  // .text:06cfa19a
                double const Dist = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx); // .text:06cfa19e .. 06cfa1ae
                double const Gap = Dist - static_cast<double>(OtherRadius); // .text:06cfa1ba
                if (Gap <= 0.0) {                                        // .text:06cfa1bc .. 06cfa1c9
                    bHit = true;                                         // .text:06cfa1d6
                    break;
                }
            }
        }
    }
    Result = 0;                                                          // .text:06cfa1df
    if (Count1 != 0 || (Count2 != 0 && bHit)) {                         // .text:06cfa1e4 .. 06cfa1f0
        out_empty = false;                                               // .text:06cfa213
        return Result;
    }
    out_empty = true;                                                    // .text:06cfa1fd
    return Result;
}

} // namespace SpaceObj

// ===========================================================================
// pub::Player exports — all line-by-line verified against Server.dll.c
// Retail pattern: validate player_id != 0 && (id-1) < max && active → field access
// ===========================================================================
namespace Player {

// Player::MarkObj — marks an object for a player (map marker / HUD highlight)
// Retail (0x74970): validate player, FUN_06d006c0 lookup on obj_id, FUN_06cef3b0 mark, vtable+300 notify
SERVER_API int __cdecl MarkObj(unsigned int player_id, unsigned int obj_id, int mark_type) {
    if (!IsValidPlayer(player_id)) return -2;
    GameObject* obj = Server_LookupGameObject(obj_id);
    if (!obj) return -3;
    // Retail (.text:06d549d5) calls FUN_06cef3b0(obj, player_id, mark_type) to record the
    // mark on the object, then (.text:06d549ed) dispatches
    //   CALL dword ptr [EDX + 0x12c]      with (&player_id, obj_id, mark_type)
    // on DAT_06d8fb14 — that is IClient::SendServerMarkObj (+0x12c), ch2 op 0x46,
    // FLPACKET_SERVER_MARKOBJ. The previous port invented message id 0x48 instead.
    //
    // FUN_06cef3b0 is GameObject::mark_for_player now (wave 13): retail's list
    // of marking player ids lives at GameObject+0x14..+0x1F and the CREATELOOT /
    // CREATESHIP / CREATESOLAR payloads all carry the answer as a byte.
    obj->mark_for_player(player_id, mark_type);
    // .text:06d549e8 LEA EAX,[ESP+0x18] -- the ADDRESS of param_1, and rpclocal's
    // sender (0x06d8ace0, SendPacketClientRef) dereferences it. No null test on
    // DAT_06d8fb14 (.text:06d549de).
    ::g_pClientInterface->SendServerMarkObj(player_id, obj_id, mark_type);
    return 0;
}

// Player::CfgInterfaceNotification — configures interface notifications
// Retail (0x74a10): validate player, server interface vtable+0x130 dispatch
SERVER_API int __cdecl CfgInterfaceNotification(unsigned int player_id, unsigned int notif_type,
                                      bool enable, int param) {
    if (!IsValidPlayer(player_id)) return -2;
    // Retail (.text:06d54a6f) dispatches CALL dword ptr [EAX + 0x130] on DAT_06d8fb14
    // with (&player_id, &payload), where payload is the 12-byte stack block
    // { uint notif_type; int param; bool enable; } built at .text:06d54a43..06d54a66.
    // That is IClient::SendServerCfgInterfaceNotification (+0x130), ch2 op 0x47,
    // FLPACKET_SERVER_CFGINTERFACENOTIFICATION. The previous port dispatched nothing.
    // The block is rpc_iclient_args.h's FPayloadServerCfgInterfaceNotification (+0x00
    // notif_type, +0x04 param, byte +0x08 enable); retail leaves the three pad bytes
    // unwritten, and so does this.
    FPayloadServerCfgInterfaceNotification Payload;
    Payload.Field_0x00 = notif_type;
    Payload.Field_0x04 = static_cast<unsigned int>(param);
    Payload.Field_0x08 = enable ? 1u : 0u;
    // .text:06d54a62 LEA EDX,[ESP+0x18] -- &param_1 (rpclocal's sender 0x06d8ad30
    // dereferences it); DAT_06d8fb14 is not null-tested.
    ::g_pClientInterface->SendServerCfgInterfaceNotification(player_id, Payload);
    return 0;
}

// Player::GetSystem — gets player's current system ID
// Retail (0x74a80): _Players[id * 0x418 - 0x38]
SERVER_API int __cdecl GetSystem(const unsigned int& player_id, unsigned int& out_system_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_system_id = p->system_id;
    return 0;
}

// Player::GetShip — gets player's current ship FLID
// Retail (0x74ad0): _Players[id * 0x418 - 0x34]
SERVER_API int __cdecl GetShip(const unsigned int& player_id, unsigned int& out_ship_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_ship_id = p->ship_object_id;
    return 0;
}

// Player::GetShipID — gets player's ship archetype ID
// Retail (0x74b20): _Players[id * 0x418 - 0x1b4]
SERVER_API int __cdecl GetShipID(const unsigned int& player_id, unsigned int& out_ship_arch) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_ship_arch = p->ship_archetype;
    return 0;
}

// Player::GetRep — gets player reputation vibe handle
// Retail (0x74b70): _Players[id * 0x418 - 0x100], default -1
SERVER_API int __cdecl GetRep(const unsigned int& player_id, int& out_rep) {
    out_rep = -1;
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_rep = p->rep_vibe;
    return 0;
}

// Player::GetBase — gets player's current base ID
// Retail (0x74bd0): _Players[id * 0x418 - 0x2c]
SERVER_API int __cdecl GetBase(const unsigned int& player_id, unsigned int& out_base_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_base_id = p->current_base;
    return 0;
}

// Player::GetLocation — gets player's current location ID
// Retail (0x74c20): _Players[id * 0x418 - 0x20]
SERVER_API int __cdecl GetLocation(const unsigned int& player_id, unsigned int& out_location_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_location_id = p->current_location;
    return 0;
}

// Player::GetCharacter -- retail .text:06d54c70: the standard id guard (-2),
// then *out = [Players + id*0x418 - 0x1c] = slot +0x3FC, PlayerData::Field_0x3FC
// (the port used to read a separate `character_flid` copy that nothing wrote).
SERVER_API int __cdecl GetCharacter(const unsigned int& player_id, unsigned int& out_char_id) {
    if (!IsValidPlayer(player_id)) return -2;
    out_char_id = Players[player_id].Field_0x3FC;
    return 0;
}

// Player::GetRelativeHealth — gets player's normalized hull health
// Retail (0x74cc0): _Players[id * 0x418 - 0x1b0], default -1.0
SERVER_API int __cdecl GetRelativeHealth(const unsigned int& player_id, float& out_health) {
    out_health = -1.0f;
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_health = p->relative_health;
    return 0;
}

// Player::EnumerateCargo — iterates the player's cargo-bay contents.
// Retail 0x74d20 (.text:06d54d20), ported 1:1.
//   .text:06d54d5b  LEA ECX,[ECX + ESI*0x1 + 0xfffffe60]   ; &slot->equipment (+0x278)
//   .text:06d54d63  MOV EBX,[ECX + 0x4] / MOV ESI,[EBX]    ; walk the list
//   .text:06d54d77  MOV AL,[ESI + 0x14]  ; EquipDesc::bEquipped -- SKIP when set
//   .text:06d54d7e  MOV EAX,[ESI + 0xc]  ; EquipDesc::ArchId  -> Arch2Good
//   .text:06d54d84  MOV ECX,[ESI + 0x1c] ; EquipDesc::Count
//   .text:06d54d8f  MOV AX,[ESI + 0xa]   ; EquipDesc::Id
//   .text:06d54d9f  CALL [enumerator vtable + 0]
// The old port body did nothing at all. Note the equipped filter: this is the
// CARGO enumerator, so mounted equipment is deliberately invisible to it.
// Retail does NOT test Arch2Good's answer here (unlike SpaceObj's version).
SERVER_API int __cdecl EnumerateCargo(const unsigned int& player_id, CargoEnumerator& enumerator) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;

    for (std::list<EquipDesc>::const_iterator It = p->equipment.Items.begin();
         It != p->equipment.Items.end(); ++It) {
        EquipDesc const& Item = *It;
        if (Item.bEquipped)
            continue;
        unsigned int const GoodId = Arch2Good(Item.ArchId);
        enumerator.notify_cargo(GoodId, Item.Count, Item.Id);
    }
    return 0;
}

// Player::GetRemainingHoldSize — gets remaining cargo hold capacity
// Retail (0x74db0): get ship archetype from -0x1b4, GetCargoHoldSize, subtract occupied from -0x1a0
SERVER_API int __cdecl GetRemainingHoldSize(const unsigned int& player_id, float& out_remaining) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;

    // .text:06d54e00  MOV EAX,[EAX + EDX*0x1 + 0xfffffe4c]   ; -0x1b4 == +0x264
    // .text:06d54e15  CALL pub::GetCargoHoldSize(&shipArch, &holdSize)
    // .text:06d54e55  LEA  ECX,[EAX + EDX*0x1 + 0xfffffe60]   ; -0x1a0 == +0x278
    // .text:06d54e5c  CALL [->COMMON.DLL::EquipDescList::get_cargo_space_occupied]
    // .text:06d54e78  FILD qword / FSUB ST0,ST1 / FSTP [out]
    // Retail goes through pub::GetCargoHoldSize -- whose out-parameter is an
    // UNSIGNED dword, zero-extended by the FILD qword -- not straight at
    // Archetype::Ship::HoldSize, and subtracts the EquipDescList own occupied
    // volume rather than a bare item count.
    unsigned int HoldSize = 0;
    pub::GetCargoHoldSize(p->ship_archetype, HoldSize);
    out_remaining = (float)HoldSize - p->equipment.get_cargo_space_occupied();
    return 0;
}

// Player::AddCargo -- adds cargo to the player ship, or to the cargo bay when
// the player has no ship object (i.e. is docked).
// Retail 0x74e90 (.text:06d54e90).
//   .text:06d54ed3  FLD health / FCOMP .rdata:06d65188 (0x00000000 = 0.0f)
//                   TEST AH,0x1 / JNZ -> return -4          ; health < 0
//   .text:06d54ee8  FLD health / FCOMP .rdata:06d65520 (0x3f800000 = 1.0f)
//                   TEST AH,0x41 / JP -> return -4          ; health > 1
//   .text:06d54f06  MOV EDX,[EAX + -0x34]                   ; +0x3e4 ship object id
//   .text:06d54f1b  FUN_06d006c0(&shipObjId, &param_1, &obj) ; false -> return -2
//   .text:06d54f50  FUN_06d0e8d0(obj, &playerId, Good2Arch(good), qty, health, flag)
//   .text:06d54f69  LEA ESI,[EAX + 0xfffffbe8]              ; back to the slot base
//   .text:06d54f95  FUN_06d4efc0(slot, arch, CARGO_BAY_HP_NAME, qty, health,
//                                /*bEquipped*/0, flag, /*bNotify*/1)
//
// CORRECTION on the last argument, itself CORRECTED 2026-09-17 (wave 11).
// On THIS path -- the docked one -- it really is FUN_06d4efc0's +0x18 byte, the
// "do not combine with an existing stack" flag, and nothing else. The earlier
// note went one step too far and concluded the bool never reaches
// EquipDesc::bMission anywhere "because Server.dll imports no EquipDesc setter
// for that field": true about the import table, false about the field. The
// IN-SPACE path writes it with a direct byte store instead --
//   .text:06d0eadd  8a4c247c  MOV  CL,byte ptr [ESP + 0x7c]   ; the bool
//   .text:06d0eae1  884c2440  MOV  byte ptr [ESP + 0x40],CL
// and [ESP+0x40] is EquipDesc+0x18 (the descriptor lives at [ESP+0x28] in that
// frame), i.e. EquipDesc::bMission. So in space the flag both suppresses the
// merge and marks the item as mission cargo; docked, only the former.
SERVER_API int __cdecl AddCargo(const unsigned int& player_id, const unsigned int& good_id,
                     unsigned int quantity, float health, bool mission_flag) {
    if (!IsValidPlayer(player_id)) return -2;
    // .text:06d54ed3 FCOMP 0.0f / TEST AH,0x1 / JNZ: C0 is set for "below" AND for
    // unordered, so a NaN health is rejected too -- hence the negated compares.
    if (!(health >= 0.0f)) return -4;
    // .text:06d54ee8 FCOMP 1.0f / TEST AH,0x41 / JP: jumps for "above" and unordered.
    if (!(health <= 1.0f)) return -4;
    PlayerSlot* p = &Players[player_id];

    if (p->ship_object_id != 0) {
        // .text:06d54f10 .. .text:06d54f1b -- FUN_06d006c0(&ship_object_id,
        // &objOut, &ownerOut). It writes the resolved object back over the
        // stack slot that held param_1 (`LEA EAX,[ESP+0x10]`) and the owning
        // CONTAINER into the separate local at [ESP+0x4]; that container is
        // then the `this` of the FUN_06d0e8d0 call at .text:06d54f50
        // (.text:06d54f43 `MOV ECX,[ESP+0x14]` reloads it). A false return is
        // -2 (.text:06d54f5a).
        //
        // The system is FUN_06d006c0's OutSys (Server_FindObject, 2026-09-24
        // claude-code:w5-server-06d4; the GameObject::OwnerSystem stand-in is
        // gone). The lookup failing is the only -2 here (.text:06d54f25 JZ ->
        // 06d54f5a); the system it hands back is used untested, null for an
        // in-transit ship exactly as in retail.
        GameObject* ShipObj = nullptr;
        StarSystem* ShipSys = nullptr;
        if (!Server_FindObject(p->ship_object_id, ShipObj, ShipSys))  // .text:06d54f1b
            return -2;
        // .text:06d54f3d -- Good2Arch only after the lookup succeeded.
        ShipSys->add_cargo(ShipObj, Good2Arch(good_id), quantity, health,
                           mission_flag);                            // .text:06d54f50
        return 0;
    }

    // .text:06d54f62 .. .text:06d54f95 -- the docked path, ported 1:1.
    unsigned int const ArchId = Good2Arch(good_id);   // .text:06d54f6f
    PlayerData_AddEquipment(*p, ArchId, EquipDesc::CARGO_BAY_HP_NAME,
                            (int)quantity, health, /*bEquipped*/false,
                            /*bNoCombine*/mission_flag, /*bNotify*/true);
    return 0;
}

// Player::RemoveCargo -- removes cargo from the player.
// Retail 0x74fb0 (.text:06d54fb0).
//   .text:06d54ffa  MOV ECX,[ESI + -0x34]                   ; +0x3e4 ship object id
//   .text:06d5500f  FUN_06d006c0(...)                       ; false -> return -2
//   .text:06d5502e  FUN_06d0ebf0(obj, &playerId, id, count)
//   .text:06d55042  the no-ship path: the MP gate on [ESI-0x3b4] (= +0x64),
//                   EquipDescList::remove_equipment_item at [ESI-0x1a0]
//                   (= +0x278), IClient slot 27 (+0x6c) with
//                   (client_id, result, id, count), then
//                   FUN_06d43650(0, &client_id).
// That tail is byte-for-byte PlayerData::remove_equipment (FUN_06d4f100)
// inlined, so the port calls the shared helper.
SERVER_API int __cdecl RemoveCargo(const unsigned int& player_id, unsigned short cargo_slot, unsigned int quantity) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = &Players[player_id];

    if (p->ship_object_id != 0) {
        // .text:06d5500f -- the same FUN_06d006c0 resolve, then
        // .text:06d5502e  FUN_06d0ebf0(owner, obj, cargo_slot, quantity).
        // The third argument is real: FUN_06d0ebf0 ends in `RET 0xc` and reads
        // it at .text:06d0ec56 (`MOV EBX,[ESP+0x40]`), which is entry+0x0C.
        // `this` is FUN_06d006c0's OutSys (.text:06d5502a MOV ECX,[local_4]), as in
        // AddCargo; the lookup failing is the only -2 (.text:06d55019 JZ ->
        // 06d55039), the system is used untested.
        GameObject* ShipObj = nullptr;
        StarSystem* ShipSys = nullptr;
        if (!Server_FindObject(p->ship_object_id, ShipObj, ShipSys))  // .text:06d5500f
            return -2;
        ShipSys->remove_cargo(ShipObj, cargo_slot, (int)quantity);   // .text:06d5502e
        return 0;
    }

    PlayerData_RemoveEquipment(*p, cargo_slot, (int)quantity, /*bNotify*/true);
    return 0;
}

// Player::SetShipAndLoadout -- sets the player hull and replaces the whole
// equipment list.
// Retail 0x750a0 (.text:06d550a0).
//   .text:06d5510c  FUN_06d4ec10(slot, shipArch)              ; PlayerData::set_ship_arch
//   .text:06d5511f  a local std::list<EquipDesc> is built, one node per element
//                   of the incoming EquipDescVector (stride 0x20, walked from
//                   [param_3+0x4] to [param_3+0x8]), and EVERY copy has its id
//                   overwritten with SUBOBJ_ID_NONE
//                   (.text:06d55195, set_id(*SUBOBJ_ID_NONE))
//   .text:06d551bf  FUN_06d4eeb0(slot, &thatList)             ; PlayerData::load_equipment
//   .text:06d551e0  the local list is erased and freed
// The id wipe is what makes load_equipment mint a fresh EquipIdMaker id for
// every item, which is the whole point of the round trip. The previous port
// assigned ship_archetype directly and dropped the loadout on the floor.
SERVER_API int __cdecl SetShipAndLoadout(const unsigned int& player_id, unsigned int ship_arch,
                                          const EquipDescVector& equip_desc_vector) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;

    PlayerData_SetShipArch(*p, ship_arch);

    EquipDescList Fresh;
    for (EquipDesc const* It = equip_desc_vector.Begin; It != equip_desc_vector.End; ++It) {
        EquipDesc Copy(*It);
        Copy.set_id(SUBOBJ_ID_NONE);
        Fresh.add_equipment_item(Copy, false);
    }
    PlayerData_LoadEquipment(*p, Fresh);
    return 0;
}

// Player::GetRank — gets player rank level
// Retail (0x75220): _Players[id * 0x418 - 0x194]
SERVER_API int __cdecl GetRank(const unsigned int& player_id, int& out_rank) {
    out_rank = 0;
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_rank = (int)p->rank_level;
    return 0;
}

// Player::GetMoneyNeededToNextRank — gets cash needed for next rank promotion
// Retail (0x75280): _Players[id * 0x418 - 0x190]
SERVER_API int __cdecl GetMoneyNeededToNextRank(const unsigned int& player_id, int& out_money) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    out_money = p->money_needed_to_next_rank;
    return 0;
}

// Player::GetAssetValue — calculates total player asset value
// Retail (0x752d0): GoodList::find_by_ship_arch for ship value * 0.8,
//   iterate EquipDescList at -0x1a0, commodity qty * price * 0.3 or equip full price
SERVER_API int __cdecl GetAssetValue(const unsigned int& player_id, float& out_value) {
    out_value = 0.0f;
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;

    // .text:06d55300  MOV ESI,[EAX + ECX*0x1 + 0xfffffe4c]  ; -0x1b4 == +0x264
    unsigned int const ship_arch = p->ship_archetype;
    if ((ship_arch & 0x3fffffffu) == 0) return -2;

    // .text:06d55314  GoodList_get()
    // .text:06d5531c  GoodInfoList::find_by_ship_arch(ship_arch)  <- NOT
    //                 find_by_archetype, which is what the port used to call.
    GoodInfoList* good_list = GoodList_get();
    if (!good_list) return -2;

    const GoodInfo* ship_good = good_list->find_by_ship_arch(ship_arch);
    if (!ship_good) return -2;

    // .text:06d5532a  FLD .rdata:06d6ae78 / FMUL [EAX + 0x58] / FSTP [EDI]
    //                 .rdata:06d6ae78 is cd cc 4c 3f == 0x3f4ccccd == 0.8f.
    out_value = 0.8f * ship_good->Price;

    // .text:06d55342  LEA EBX,[EDX + EAX*0x1 + 0xfffffe60]   ; -0x1a0 == +0x278
    // .text:06d55360 .. .text:06d553a9 -- one pass over the EquipDescList.
    //   node+0x0c == EquipDesc::ArchId, node+0x18 == Health, node+0x1c == Count.
    //   .text:06d5536b  GoodList::find_by_archetype(ArchId)
    //   .text:06d55374  MOV CL,[good + 0x70] / TEST CL,CL
    //     set   -> FILD Count / FMUL [good + 0x58]                (full price)
    //     clear -> FLD Health / FMUL [good + 0x58] / ftol
    //              IMUL by Count / FILD / FMUL .rdata:06d6ae7c
    //              (9a 99 99 3e == 0x3e99999a == 0.3f)
    // GoodInfo+0x70 is the COMBINABLE flag, not a category: FUN_06d4faf0 splits
    // on exactly the same two branches using COMMON.DLL::arch_is_combinable
    // (.text:06d4fb60) and applies the identical 0.3f to the non-combinable
    // side. The previous port tested Category == GoodCategoryCommodity, had the
    // two branches the wrong way round, and multiplied by a hard 1.0f instead of
    // the item Count.
    for (std::list<EquipDesc>::const_iterator It = p->equipment.Items.begin();
         It != p->equipment.Items.end(); ++It) {
        EquipDesc const& Item = *It;
        if ((Item.ArchId & 0x3fffffffu) == 0) continue;
        const GoodInfo* item_good = GoodList::find_by_archetype(Item.ArchId);
        if (!item_good) continue;

        if (item_good->bCombinable) {
            out_value += (float)Item.Count * item_good->Price;
        } else {
            int const Unit = (int)(Item.Health * item_good->Price);
            out_value += (float)(Unit * Item.Count) * 0.3f;
        }
    }
    return 0;
}

// Player::AdjustCash (0x06d553c0) -- PlayerData::AdjustCash (0x06d4f4e0)
// inlined with its bool argument folded to true, so +0x320 always accumulates.
//
// The previous port kept only the clamp. Four things were missing, and three of
// them could not even be expressed until PlayerSlot's two layouts were unified:
//   * the running total at +0x320 (`ADD [EDI+0x320],EAX`, 0x06d5540e);
//   * the slot-occupied gate `MOV EDI,[EDX+ESI+0x348] / TEST EDI,EDI / JZ`
//     (0x06d553f2) -- retail returns -2 when the client id is zero, which is a
//     stricter test than the index-range check alone;
//   * the `IsMPServer() && player+0x64 == 0` gate (0x06d5541c), which skips the
//     clamp, the store AND the notification, yet still returns 0;
//   * the two notifications: IClient vtable+0x134 == slot 77, taking the client
//     id BY ADDRESS (`ADD EDI,0x348 / PUSH EDI`, 0x06d55456), then
//     Server_SendMessage(0, &client_id) (0x06d55466).
SERVER_API int __cdecl AdjustCash(const unsigned int& player_id, int amount) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    // 0x06d553f2: the client id doubles as retail's "slot occupied" flag.
    if (p->client_id == 0) return -2;

    p->total_earned += amount;
    int new_cash = p->cash + amount;

    // 0x06d55424 jumps into the body when IsMPServer() is false; 0x06d55430
    // jumps PAST it when [player+0x64] is zero. +0x64 is the session's SELECTED
    // CHARACTER_ID (see common_server_playerdb.h -- proved on the raw bytes at
    // 0x06d4a7d0), so the body runs on
    // `!IsMPServer() || selected_character[0] != 0`: an MP server only adjusts
    // the cash of a session that has actually picked a character. Still returns
    // 0, not an error.
    if (IsMPServer() && !p->selected_character.is_valid()) return 0;

    if (new_cash < 0) new_cash = 0;
    else if (new_cash > 999999999) new_cash = 999999999;
    p->cash = new_cash;

    // 0x06d5544d..0x06d5545d: DAT_06d8fb14 is loaded and called through with no
    // null test (the port's `if (g_pClientInterface)` was an addition, removed
    // 2026-09-24 by claude-code:w3-baseenter).
    ::g_pClientInterface->SendServerSetCash(p->client_id, new_cash);
    // 0x06d55463-0x06d55466: FUN_06d43650(0, &client_id) == _SendMessage(&0u, 0, &client_id),
    // a broadcast of message 0 to every controller.
    Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerWorthChanged, &p->client_id);
    return 0;
}

// Player::InspectCash (0x06d55480) -- reads +0x31c and clamps it on the way out
// without writing it back. Same slot-occupied gate as AdjustCash: 0x06d554b5
// loads [+0x348] and 0x06d554bf returns -2 on zero. out_cash is zeroed first
// (0x06d5548a), so the -2 paths still leave a defined value behind.
SERVER_API int __cdecl InspectCash(const unsigned int& player_id, int& out_cash) {
    out_cash = 0;
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    if (p->client_id == 0) return -2;
    int cash = p->cash;
    if (cash < 0) cash = 0;
    if (cash > 999999999) cash = 999999999;
    out_cash = cash;
    return 0;
}

// Player::SetInitialPos — sets initial spawn position for player
// Retail (0x75500): copy Vector to _Players[id * 0x418 - 0xc8] (3 dwords)
SERVER_API int __cdecl SetInitialPos(const unsigned int& player_id, const Vector& pos) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->initial_pos[0] = pos.x;
    p->initial_pos[1] = pos.y;
    p->initial_pos[2] = pos.z;
    return 0;
}

// Player::SetInitialOrnt — sets initial spawn orientation for player
// Retail (0x75560): copy Matrix to _Players[id * 0x418 - 0xbc] (9 dwords loop)
SERVER_API int __cdecl SetInitialOrnt(const unsigned int& player_id, const Matrix& orient) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    for (int i = 0; i < 9; i++) {
        ((float*)p->initial_ornt)[i] = ((const float*)&orient)[i];
    }
    return 0;
}

// Player::GetNumKills — gets player kill count
// Retail (0x755c0): _Players[id * 0x418 - 0xe4], default -1 on error
SERVER_API int __cdecl GetNumKills(const unsigned int& player_id, int& out_kills) {
    if (!IsValidPlayer(player_id)) { out_kills = -1; return -2; }
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) { out_kills = -1; return -2; }
    out_kills = p->num_kills;
    return 0;
}

// Player::SetNumKills — retail 0x06d55620, 30 instructions.
//   .text:06d55667  MOV  [EAX + 0x334],EDX        ; the store
//   .text:06d55675  PUSH EDX                      ; kills
//   .text:06d55676  ADD  EAX,0x348 / PUSH EAX     ; &slot->client_id, BY ADDRESS
//   .text:06d5567c  CALL [IClient + 0x138]        ; FLPACKET_SERVER_SETNUMKILLS
// The notification was missing; without it the client never learns the new
// count. IClient +0x138 is SendServerSetNumKills and rpc_iclient.h's own packet-registry
// derivation names it SETNUMKILLS, which is the independent confirmation.
// The by-address first argument is the convention rpclocal already established
// for this packet family (RPC_CH2_DELIVER_DWORD_BYREF in rpc_payload_ch2.cpp).
SERVER_API int __cdecl SetNumKills(const unsigned int& player_id, int kills) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->num_kills = kills;
    if (::g_pClientInterface) {
        ::g_pClientInterface->SendServerSetNumKills(p->client_id, kills);
    }
    return 0;
}

// Player::GetNumMissionSuccesses — gets mission success count
// Retail (0x75690): _Players[id * 0x418 - 0xe0], default -1 on error
SERVER_API int __cdecl GetNumMissionSuccesses(const unsigned int& player_id, int& out_count) {
    if (!IsValidPlayer(player_id)) { out_count = -1; return -2; }
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) { out_count = -1; return -2; }
    out_count = p->num_mission_successes;
    return 0;
}

// Player::SetNumMissionSuccesses — retail 0x06d556f0, the same shape as
// SetNumKills above: `MOV [EAX+0x338],EDX` at .text:06d55737, then
// `CALL [IClient + 0x13c]` at .text:06d5574c == SendServerSetMissionSuccesses,
// FLPACKET_SERVER_SETMISSIONSUCCESSES. The notification was missing.
SERVER_API int __cdecl SetNumMissionSuccesses(const unsigned int& player_id, int count) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->num_mission_successes = count;
    if (::g_pClientInterface) {
        ::g_pClientInterface->SendServerSetMissionSuccesses(p->client_id, count);
    }
    return 0;
}

// Player::GetNumMissionFailures — gets mission failure count
// Retail (0x75760): _Players[id * 0x418 - 0xdc], default -1 on error
SERVER_API int __cdecl GetNumMissionFailures(const unsigned int& player_id, int& out_count) {
    if (!IsValidPlayer(player_id)) { out_count = -1; return -2; }
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) { out_count = -1; return -2; }
    out_count = p->num_mission_failures;
    return 0;
}

// Player::SetNumMissionFailures — retail 0x06d557c0, same shape again:
// `MOV [EAX+0x33c],EDX` at .text:06d55807, then `CALL [IClient + 0x140]` at
// .text:06d5581c == SendServerSetMissionFailures, FLPACKET_SERVER_SETMISSIONFAILURES.
SERVER_API int __cdecl SetNumMissionFailures(const unsigned int& player_id, int count) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->num_mission_failures = count;
    if (::g_pClientInterface) {
        ::g_pClientInterface->SendServerSetMissionFailures(p->client_id, count);
    }
    return 0;
}

// Player::DisplayMissionMessage -- retail 0x75830 (.text:06d55830..06d5595d), ordinal
// 109. Traced 2026-09-24 (claude-code:w5-server-06d4):
//   guard (id / MaxPlayers / Players[id].+0x348) -> -2 (EAX preset at .text:06d55854)
//   .text:06d55887..06d558c3  CSetMissionMessage record on the stack, FmtStr zeroed inline
//   .text:06d558d4            record.Text = fmt_str (FmtStr::operator=)
//   .text:06d558e0            record.Type = msg_type
//   .text:06d558e4..06d558fb  msg_type == 0 -> Players[id].+0x340 = 1 (bSkipNextAutosave)
//   .text:06d55902..06d55923  broadcast && Players[id].+0x404 (group) != 0
//                               -> CPlayerGroup::SetMissionMessage(&record)
//   .text:06d5592a..06d55933  otherwise IClient slot 70 (+0x118)(&player_id, &record) --
//                             the default path, not a fallback; DAT_06d8fb14 untested
//   .text:06d55945            ~FmtStr; return 0
SERVER_API int __cdecl DisplayMissionMessage(const unsigned int& player_id, const FmtStr& fmt_str,
                                               MissionMessageType msg_type, bool broadcast) {
    if (!IsValidPlayer(player_id)) return -2;
    CSetMissionMessage Record;
    Record.Text = fmt_str;
    Record.Type = static_cast<unsigned int>(msg_type);
    if (msg_type == 0) {
        Players[player_id].bSkipNextAutosave = 1;
    }
    CPlayerGroup* const Group = static_cast<CPlayerGroup*>(Players[player_id].group_ptr);
    if (broadcast && Group != nullptr) {
        Group->SetMissionMessage(Record);
    } else {
        ::g_pClientInterface->SendServerSetMissionMessage(player_id, Record);
    }
    return 0;
}

// Player::SetStoryCue — sets story cue ID for player
// Retail (0x75aa0): validate player, store cue_id, notify server interface
// Retail 0x75d90 (.text:06d55d90). Validates the slot, copies cue_id into a
// stack local, and fires IClient slot 90 (+0x168), ch2 op 0x43
// FLPACKET_SERVER_SET_STORY_CUE, with BOTH arguments by address:
//   .text:06d55dce  LEA  ESI,[ESP + 0x8]   ; &cueCopy
//   .text:06d55dd2  PUSH ESI
//   .text:06d55dd9  PUSH EDX               ; param_1, already a uint const*
//   .text:06d55dda  CALL dword ptr [EAX + 0x168]
// Nothing is stored on the player slot at all -- the old port comment said
// "store cue_id", and no such store exists.
SERVER_API int __cdecl SetStoryCue(const unsigned int& player_id, unsigned int cue_id) {
    if (!IsValidPlayer(player_id)) return -2;
    if (::g_pClientInterface) {
        ::g_pClientInterface->SendServerSetStoryCue(player_id, cue_id);
    }
    return 0;
}

// Player::PopUpDialog — retail 0x06d55df0, THIRTEEN instructions and not one of
// them looks at Players:
//   .text:06d55df4  MOV  ECX,[.data:DAT_06d8fb14] ; the IClient global
//   .text:06d55dfc  PUSH param_4 / param_3 / param_2 / param_1
//   .text:06d55e0c  CALL [EAX + 0x17c]            ; SendCommonPopUpDialog, FLPACKET_COMMON_POP_UP_DIALOG
//   .text:06d55e12  XOR  EAX,EAX / RET            ; ALWAYS 0
// The port's `if (!IsValidPlayer(...)) return -2;` was invented -- retail has no
// such guard here -- and the actual call was missing, so this was a no-op that
// could also refuse for a reason retail does not have.
SERVER_API int __cdecl PopUpDialog(unsigned int player_id, const FmtStr& title, const FmtStr& text, unsigned int param) {
    if (::g_pClientInterface) {
        ::g_pClientInterface->SendCommonPopUpDialog(player_id, title, text, param);
    }
    return 0;
}

// Player::ReturnBestPath — retail 0x06d55e20, eleven instructions:
// `CALL [IClient + 0x174]` (SendCommonRequestBestPath, FLPACKET_COMMON_REQUEST_BEST_PATH) with
// (param_1, param_2, param_3), then XOR EAX,EAX. No validation, always 0.
SERVER_API int __cdecl ReturnBestPath(unsigned int player_id, unsigned char* data, int data_len) {
    if (::g_pClientInterface) {
        ::g_pClientInterface->SendCommonRequestBestPath(player_id, data, (unsigned int)data_len);
    }
    return 0;
}

// Player::ReturnPlayerStats — retail 0x06d55e40, the same eleven instructions
// through `CALL [IClient + 0x178]` (SendCommonRequestPlayerStats,
// FLPACKET_COMMON_REQUEST_PLAYER_STATS). No validation, always 0.
SERVER_API int __cdecl ReturnPlayerStats(unsigned int player_id, unsigned char* data, int data_len) {
    if (::g_pClientInterface) {
        ::g_pClientInterface->SendCommonRequestPlayerStats(player_id, data, (unsigned int)data_len);
    }
    return 0;
}

// Player::SetCamera -- retail 0x75e60 (.text:06d55e60). Validates the slot,
// assembles a 0x24-byte block and fires IClient slot 88 (+0x160), ch2 op 0x4b
// FLPACKET_SERVER_SET_CAMERA.
//
// THE SINGLETON IS NOT UNIDENTIFIED. The previous note called DAT_06d902f0 /
// FUN_06d5ff70 "an unidentified Server.dll singleton" and left the dispatch
// out. FUN_06d5ff70 is four instructions and names itself:
//   .text:06d5ff73  MOV  dword ptr [ESP + 0x8],.data:s_3DMathEngine
//   .text:06d5ff83  MOV  dword ptr [ESP + 0x4],0xc
//   .text:06d5ff8b  CALL dword ptr [->DACOM.DLL::DACOM_Acquire]
//   .text:06d5ff9e  CALL dword ptr [ECX + 0xc]
// -- a DACOM_Acquire of "3DMathEngine" with a 0xC-byte descriptor, the same
// body Common.dll compiled into its own FUN_06391cf0. Server.dll has its own
// copy (21 call sites, XREF header of .text:06d5ff70), and DAT_06d902f0 is this
// function's file-static cache of the result, filled lazily at .text:06d55ec3.
//
// So slot +0x50 is I3DMathEngine::MatrixToQuaternion(FQuaternion&, FMatrix const&)
// (src/include/x86math.h), and the block retail builds is:
//   .text:06d55ea3  LEA EAX,[transform + 0x24]  -- Transform::Translation
//   .text:06d55eb9/ebd/eae  the three dwords -> [E-0x24], [E-0x20], [E-0x1c]
//   .text:06d55ecf  PUSH ESI (&transform, which IS &transform.Rotation)
//   .text:06d55ed0  LEA EDX,[ESP+0x8] (the 4-dword output at [E-0x34])
//   .text:06d55ed6  CALL [ECX + 0x50]
//   .text:06d55ed9..f14  those four dwords -> [E-0x18] .. [E-0x0c],
//                        then fov -> [E-0x08] and aspect -> [E-0x04]
//   .text:06d55f0f  PUSH EDX (= E-0x24)      ; the block, second argument
//   .text:06d55f1a  PUSH EDX (= E+0x04)      ; &player_id, FIRST argument
//   .text:06d55f1b  CALL [EAX + 0x160]
// i.e. one contiguous 0x24-byte { Vector Pos; Quaternion Ornt; float A, B; },
// passed by address, with the player id also by address.
//
// The engine is this function's own lazily filled file-static, .data:DAT_06d902f0
// (.text:06d55eb2 test, .text:06d55ec3 CALL FUN_06d5ff70, .text:06d55ec8 store),
// fetched through Server.dll's FUN_06d5ff70 (Server_Acquire3DMathEngine,
// server_createinfo.h) -- not through Common.dll's cache.
static I3DMathEngine* GSetCameraMathEngine = nullptr;   // .data:DAT_06d902f0

SERVER_API int __cdecl SetCamera(unsigned int player_id, const Transform& transform, float fov, float aspect) {
    if (!IsValidPlayer(player_id)) return -2;

    // The exact block retail puts on the wire: rpc_iclient_args.h's
    // FPayloadServerSetCamera {Vector Position +0x00, Quaternion Orient +0x0c (W,X,Y,Z),
    // float Fov +0x1c, float Aspect +0x20} -- the local FSetCameraBlock it replaced on
    // 2026-10-16 had this very layout (and gave the shared struct its field names).
    FPayloadServerSetCamera Block;
    Block.Position = transform.Translation;

    // .text:06d55eb2 .. .text:06d55ed6 -- no null test on the engine and no
    // seed for the quaternion: the call always writes all four dwords.
    if (GSetCameraMathEngine == nullptr)
        GSetCameraMathEngine = Server_Acquire3DMathEngine();
    // Retail pushes &transform where a `FMatrix const&` is expected, which
    // works because Transform::Rotation is at +0x00. Spelled out here. Quaternion
    // and FQuaternion are the same four floats in the same (W,X,Y,Z) order.
    GSetCameraMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Block.Orient),
                                             reinterpret_cast<FMatrix const&>(transform.Rotation));

    Block.Fov = fov;
    Block.Aspect = aspect;

    // .text:06d55f09 / .text:06d55f1b -- DAT_06d8fb14 is called through with
    // no test.
    ::g_pClientInterface->SendServerSetCamera(player_id, Block);
    return 0;
}

// Player::RevertCamera — reverts camera to default
// Retail (0x75b80): validate player, dispatch revert via server interface
// Retail 0x75f30 (.text:06d55f30). Validates the slot, then:
//   .text:06d55f69  LEA  EDX,[ESP + 0x4]   ; &player_id -- BY ADDRESS
//   .text:06d55f6d  PUSH EDX
//   .text:06d55f6e  CALL dword ptr [EAX + 0x164]
// IClient::SendServerRevertCamera, ch2 op 0x4c FLPACKET_SERVER_REVERT_CAMERA.
SERVER_API int __cdecl RevertCamera(unsigned int player_id) {
    if (!IsValidPlayer(player_id)) return -2;
    if (::g_pClientInterface) {
        ::g_pClientInterface->SendServerRevertCamera(player_id);
    }
    return 0;
}

// Player::LoadHint — retail 0x06d55f80, NINE instructions:
//   .text:06d55f80  MOV  EDX,[ESP + 0x8]          ; param_2, the hint
//   .text:06d55f8c  PUSH EDX
//   .text:06d55f8d  LEA  EDX,[ESP + 0x8] / PUSH   ; &param_1 -- the id BY ADDRESS
//   .text:06d55f92  CALL [IClient + 0x144]        ; SendServerLoadHint, FLPACKET_SERVER_LOADHINT
//   .text:06d55f98  XOR  EAX,EAX / RET            ; ALWAYS 0
// No validation here either: neither the interface global (06d55f84..06d55f8a load
// DAT_06d8fb14 and its vtable with no test) nor the hint is checked. The by-address
// first argument matches the same BYREF convention rpclocal uses for this packet.
SERVER_API int __cdecl LoadHint(unsigned int player_id, BaseHint* hint) {
    // The hint goes on untested (.text:06d55f8c); the sender calls through it.
    ::g_pClientInterface->SendServerLoadHint(player_id, hint);
    return 0;
}

// Player::GetGroupSize — retail 0x75fa0 (0x06d55fa0), ordinal 192
// ?GetGroupSize@Player@pub@@YAHIAAI@Z. Eleven instructions, and NOT one of
// them validates the player: it reads Players[id][-0x14], and
//   group != 0  -> *out = group->GetMemberCount()   (0x06d55fb8)
//   group == 0  -> *out = 1                         (0x06d55fca)
// always returning 0. The port's -2 guard and its `out_size = 1` placeholder
// were both invented; the 1 belongs on the NO-group branch, where it means
// "a party of one".
SERVER_API int __cdecl GetGroupSize(unsigned int player_id, unsigned int& out_size) {
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    CPlayerGroup* grp = p ? (CPlayerGroup*)p->group_ptr : nullptr;
    if (grp) {
        out_size = grp->GetMemberCount();
    } else {
        out_size = 1;
    }
    return 0;
}

// Player::GetName — retail 0x75fe0 (ordinal 222), .text:06d55fe0..06d56166.
// ?GetName@Player@pub@@YAHIAAV?$basic_string@GU?$char_traits@G@std@@V?$allocator@G@std@@@std@@@Z
// -- VC6's basic_string<unsigned short> (its wchar_t); the modern equivalent is std::wstring.
// EVERY path returns 0 (XOR EAX,EAX at 06d560b1 / 06d56102 / 06d56116 / 06d56164):
//   06d55fe4..06d56017  id == 0, id-1 >= DAT_06d90264, slot+0x348 == 0 -> return 0,
//                       out-string untouched
//   06d5601d..06d56064  the inlined PlayerDB::GetActiveCharacterName (same guard again,
//                       the IsMPServer()/slot+0x64 gate, FUN_06d4eda0)
//   06d56066..06d560b4  non-null name -> assign it (wcslen + _Grow + copy + terminate)
//   06d560b5..06d5615c  null name     -> assign the literal u"<bad name>" (.rdata:06d6b994)
SERVER_API int __cdecl GetName(unsigned int player_id, std::wstring& out_name) {
    if (!IsValidPlayer(player_id)) return 0;
    const wchar_t* Name = Players.GetActiveCharacterName(player_id);
    if (Name != nullptr) {
        out_name.assign(Name);
    } else {
        out_name.assign(L"<bad name>");
    }
    return 0;
}

// Player::IsGroupMember — retail 0x76170 (0x06d56170), ordinal 314
// Same shape, no validation: equal ids answer true outright (0x06d56180),
// otherwise it asks PLAYER1's group whether player2 is in it
// (0x06d5619d CALL CPlayerGroup::IsMember), and answers false when player1
// has no group (0x06d561af). Comparing the two group_ptrs, as the port did,
// gives the same answer only because membership is symmetric -- but it also
// answered true when both players were grouped-with-nobody (two null
// pointers), which retail's `TEST EAX,EAX / JZ` rules out.
SERVER_API int __cdecl IsGroupMember(unsigned int player1_id, unsigned int player2_id, bool& out_result) {
    if (player1_id == player2_id) { out_result = true; return 0; }
    PlayerSlot* p1 = PlayerDB_GetPlayer(player1_id);
    CPlayerGroup* grp = p1 ? (CPlayerGroup*)p1->group_ptr : nullptr;
    out_result = grp ? grp->IsMember(player2_id) : false;
    return 0;
}

// Player::RemoveFromGroup — retail 0x761c0 (0x06d561c0), ordinal 361
// group = Players[id][-0x14]; when it is null the function returns
// 0xfffffffd == -3 (0x06d561e3), NOT -2, and when it is not it calls
// CPlayerGroup::DelMember (0x06d561db) -- which is what actually clears the
// player's group_ptr, unregisters him and notifies the rest. The port wrote
// group_ptr = nullptr by hand and left the group holding a stale member.
SERVER_API int __cdecl RemoveFromGroup(unsigned int player_id) {
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    CPlayerGroup* grp = p ? (CPlayerGroup*)p->group_ptr : nullptr;
    if (!grp) return -3;
    grp->DelMember(player_id);
    return 0;
}

// Player::RewardGroup — retail 0x763a0 (0x06d563a0), ordinal 388
// group != 0 -> CPlayerGroup::RewardMembers(amount) (0x06d563c3), return 0.
// group == 0 -> the solo path at 0x06d563cd: bounds-check the id against
// DAT_06d90264 (the player count), require a live object at pd+0x348, then
// PlayerData::AdjustCash(amount, true) INLINED -- pd+0x320 accumulates the
// award (0x06d563f5) and pd+0x31c is the clamped cash (0x06d563fb).
// Either way the return value is 0; the port's -2 was invented.
//
// Re-traced 2026-09-24 (claude-code:w5-server-06d4), .text:06d563a0..06d5645c:
//   .text:06d563a4..06d563bc  EDI = Players + id*0x418; group = [EDI-0x14] -- the slot
//                             is indexed RAW, before any bounds or occupancy test
//   .text:06d563c3            group -> CPlayerGroup::RewardMembers(amount); return 0
//   .text:06d563cd..06d563ef  else the usual guard (id / MaxPlayers / +0x348)
//   .text:06d563f5..06d5644d  PlayerData::AdjustCash(amount, true) inlined:
//                             +0x320 += amount; new = +0x31c + amount; the
//                             `!IsMPServer() || +0x64` gate; clamp [0, 999999999];
//                             +0x31c = new; IClient slot 77(&client_id, new);
//                             FUN_06d43650(0, &client_id)
//   .text:06d56459            return 0 on every path
// PlayerData_AdjustCash (FUN_06d4f4e0) is that same body term for term.
SERVER_API int __cdecl RewardGroup(unsigned int player_id, int amount) {
    CPlayerGroup* const Group = static_cast<CPlayerGroup*>(Players[player_id].group_ptr);
    if (Group != nullptr) {
        Group->RewardMembers(amount);
        return 0;
    }
    if (IsValidPlayer(player_id)) {
        PlayerData_AdjustCash(Players[player_id], amount, true);
    }
    return 0;
}

// Player::SetMsnID -- retail 0x76460 (.text:06d56460..06d564f2), ordinal 431.
// Ported 2026-09-24 (claude-code:w5-server-06d4). EVERY path returns 0 -- the guard
// failures jump straight to .text:06d564ef XOR EAX,EAX:
//   .text:06d564a3  Players[id].+0x408 = msn_id   (group_mission_id)
//   .text:06d564ad  Players[id].+0x40c = sub_id   (group_mission_set_by)
//   .text:06d564b1  flag -> IClient slot 74 (+0x128)(Players[id].+0x348 BY VALUE, param)
//                   (.text:06d564cd MOV EAX,[EAX+EBP-0xd0]; ch2 0x5b MISSIONSAVEA)
//   .text:06d564e7  IClient slot 114 (+0x1c8)(id, msn_id, sub_id), unconditional
// DAT_06d8fb14 is not null-tested. The port's invented `active_mission` member is gone.
SERVER_API int __cdecl SetMsnID(unsigned int player_id, unsigned int msn_id, unsigned int sub_id,
                                  bool flag, unsigned int param) {
    if (!IsValidPlayer(player_id)) return 0;
    PlayerData& Slot = Players[player_id];
    Slot.group_mission_id = msn_id;
    Slot.group_mission_set_by = sub_id;
    if (flag) {
        ::g_pClientInterface->SendServerMissionSaveA(Slot.client_id, param);
    }
    ::g_pClientInterface->SendServerMiscObjUpdateMission(player_id, msn_id, sub_id);
    return 0;
}

// Player::GetMsnID -- retail 0x76500 (.text:06d56500..06d56540), ordinal 221.
// Ported 2026-09-24 (claude-code:w5-server-06d4): on a valid id, out = Players[id].
// +0x408 (.text:06d56534 MOV EAX,[ECX+EDX-0x10]); out is untouched otherwise, and the
// return is 0 on every path (.text:06d5653e).
SERVER_API int __cdecl GetMsnID(unsigned int player_id, unsigned int& out_msn_id) {
    if (IsValidPlayer(player_id)) {
        out_msn_id = Players[player_id].group_mission_id;
    }
    return 0;
}

// Player::SetRank — sets player rank level
// Retail (0x75df0): validate player, store at +0x330 equivalent, notify vtable
// Retail 0x76550 (.text:06d56550): stores at slot[-0x194] (+0x284, rank_level)
// and then fires IClient slot 117 (+0x1d4):
//   .text:06d56590  PUSH EAX   ; rank
//   .text:06d565a0  PUSH EDX   ; player id
//   .text:06d565a1  PUSH EDX   ; player id AGAIN
//   .text:06d565a2  CALL dword ptr [ESI + 0x1d4]
// Pushes are right-to-left, so the slot takes (id, id, rank) -- retail really
// does pass the same id twice, by value both times. The port used to do the
// store and drop the notification.
SERVER_API int __cdecl SetRank(unsigned int player_id, int rank) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->rank_level = (unsigned int)rank;
    if (::g_pClientInterface) {
        ::g_pClientInterface->SendServerMiscObjUpdateRank(player_id, player_id, rank);
    }
    return 0;
}

// Player::SetMoneyNeededToNextRank — sets cash threshold for next rank
// Retail (0x75e20): validate player, store at player data
SERVER_API int __cdecl SetMoneyNeededToNextRank(unsigned int player_id, int money) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->money_needed_to_next_rank = money;
    return 0;
}

// Player::SetMonkey / SetRobot / SetTrent -- retail 0x76600 / 0x76650 / 0x766c0.
// All three write the session Costume at PlayerData+0x2E4 and send NOTHING.
// The old comments ("set flag, notify vtable+0x144/+0x148/+0x14c", and the
// addresses 0x75e50/0x75e80/0x75eb0) were wrong on both counts: there is no
// flag, there is no notification, and those three addresses are inside
// SetCamera/RevertCamera.
//
//   SetMonkey  .text:06d5663a  MOV dword ptr [slot + 0x2e4],0x88db0044
//   SetRobot   .text:06d56691  MOV dword ptr [slot + 0x2e4],0x0
//              .text:06d5669b  MOV dword ptr [slot + 0x2e8],0x8dffc147
//              .text:06d566a5  MOV dword ptr [slot + 0x2ec],0x9dc4c883
//              .text:06d566af  MOV dword ptr [slot + 0x2f0],0x93582d4f
//   SetTrent   .text:06d56701  ADD EAX,0x2e4 / PUSH EAX / PUSH 0xb8249f07
//              .text:06d5670c  CALL [->COMMON.DLL::GetCostumeDescriptions]
//              .text:06d56714  CALL [->COMMON.DLL::CostumeDescriptions::get_costume]
// The four literals are nickname ids (CreateID hashes); SetMonkey touching
// only PartIds[0] and leaving the rest alone is what retail does.
SERVER_API int __cdecl SetMonkey(unsigned int player_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->costume.PartIds[0] = 0x88db0044u;
    return 0;
}

SERVER_API int __cdecl SetRobot(unsigned int player_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    p->costume.PartIds[0] = 0u;
    p->costume.PartIds[1] = 0x8dffc147u;
    p->costume.PartIds[2] = 0x9dc4c883u;
    p->costume.PartIds[3] = 0x93582d4fu;
    return 0;
}

SERVER_API int __cdecl SetTrent(unsigned int player_id) {
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (!p) return -2;
    CostumeDescriptions const* Costumes = GetCostumeDescriptions();
    if (Costumes != nullptr) {
        Costumes->get_costume((int)0xb8249f07u, p->costume);
    }
    return 0;
}

// Player::SendNNMessage -- retail 0x76720 (.text:06d56720).
//   .text:06d56725  TEST ESI,ESI / JZ -> return -4     ; msg_id == 0 is -4
//   .text:06d56733  the usual index / slot-occupied validation -> -4 as well
//                   (EAX is seeded with 0xfffffffc, not 0xfffffffe, at
//                    .text:06d56728, so EVERY failure here returns -4)
//   .text:06d56765  PUSH ESI (msg_id) / PUSH EDX (player id)
//   .text:06d56767  CALL dword ptr [EAX + 0x114]       ; IClient slot 69
// Note the -4: the port returned -2 for an invalid player, which no path in
// this function can produce.
SERVER_API int __cdecl SendNNMessage(unsigned int player_id, unsigned int msg_id) {
    if (msg_id == 0) return -4;
    if (!IsValidPlayer(player_id)) return -4;
    if (::g_pClientInterface) {
        ::g_pClientInterface->SendServerSendNNMessage(player_id, msg_id);
    }
    return 0;
}

// Player::ForceLand — forces player to dock at a base
// Retail 0x76780 (?ForceLand@Player@pub@@YAHII@Z) is a two-line function:
//     IServerImpl::Dock((IServerImpl *)&Server, &param_2, &param_1);
//     return 0;
// i.e. Dock(BaseId, PlayerId) — base first, session second, both by reference.
// This confirms the IServer slot 91 signature recovered from the IServer card in decomp-tracker.
// Retail does NOT validate the player here; the validation lives inside Dock.
// `Server` is a static object (MOV ECX,.data:Server at 06d5678a), so there is
// nothing to null-test; GetServerInterface() returns that same object.
SERVER_API int __cdecl ForceLand(unsigned int player_id, unsigned int base_id) {
    ::GetServerInterface()->Dock(base_id, player_id);
    return 0;
}

// Player::SetCostume — sets player costume
// Retail: EMPTY function body (just returns 0)
// Retail 0x767c0 is literally two instructions -- `XOR EAX,EAX / RET`
// (.text:06d567c0). It is a shipped no-op, not a port stub: nothing is stored,
// nothing is sent, and the player id is never even loaded. Do not "finish" it.
SERVER_API int __cdecl SetCostume(const unsigned int& player_id, int costume_id) {
    (void)player_id; (void)costume_id;
    return 0;
}

// Player::GetGender — retail 0x767a0 (ordinal 183) is two instructions:
//   *param_2 = 1; return 0;
// There is no player validation at all — it always succeeds.
SERVER_API int __cdecl GetGender(const unsigned int& player_id, int& out_gender) {
    out_gender = 1;
    return 0;
}

// Player::GetBody — retail 0x767b0 (ordinal 158) is likewise two instructions:
//   *param_2 = 0; return 0;
// No validation, no player-slot access. It was missing from the port entirely.
SERVER_API int __cdecl GetBody(const unsigned int& player_id, unsigned int& out_body) {
    out_body = 0;
    return 0;
}

// Player::GetGroupMembers — retrieves all member IDs in player's group
// Retail (0x761f0): delegates to CPlayerGroup::StoreMemberList if in group
SERVER_API int __cdecl GetGroupMembers(unsigned int player_id, std::vector<unsigned int>& out_members) {
    out_members.clear();
    if (!IsValidPlayer(player_id)) return -2;
    PlayerSlot* p = PlayerDB_GetPlayer(player_id);
    if (p && p->group_ptr) {
        CPlayerGroup* grp = (CPlayerGroup*)p->group_ptr;
        grp->StoreMemberList(out_members);
    } else {
        out_members.push_back(player_id);
    }
    return 0;
}

// Player::SetMissionObjectives -- retail .text:06d55960 .. .text:06d55b60,
// ?SetMissionObjectives@Player@pub@@YAHABI0PBUMissionObjective@12@IABUFmtStr@@E2@Z.
// Rewritten 2026-09-24 (claude-code:w5-server-06d4) from the listing. Retail touches
// NO server-side mission log: it builds one FServerSetMissionObjectivesInfo on the
// stack and hands it to IClient slot 72 (+0x120, ch2 op 0x35).
//   06d55979..06d559b1  *player_id: 0 or > MaxPlayers or Players[id].client_id == 0
//                       -> return -2
//   06d559b8..06d55a44  two FmtStr() (the inlined zeroing) at +0x0c / +0x24
//   06d55a4c            +0x00 = *mission_id
//   06d55a59            +0x08 = count
//   06d55a5d            +0x0c = title        (FmtStr::operator=)
//   06d55a6c            +0x3c = type
//   06d55a70            +0x24 = desc         (FmtStr::operator=)
//   06d55a72..06d55aac  count != 0: operator new(count*0x1c + 4); on success the
//                       count goes into the first dword and FUN_06d601a8 (eh vector
//                       ctor, FUN_06d55bc0 = FmtStr(), ~FmtStr) builds the elements
//                       from +4; a failed new leaves the array pointer 0
//   06d55ab6            +0x04 = that array (0 when count == 0, 06d55aea)
//   06d55ac5..06d55ae5  for i < count: e[i].Text = objs[i].Description (operator=),
//                       e[i].State = objs[i].Type -- no test of the array pointer
//   06d55aee..06d55b00  IClient slot 72 (&player_id, &record) -- the reference is
//                       passed as is, rpclocal dereferences it (.text:06d8b1f7); no
//                       interface null test
//   06d55b06..06d55b29  array != 0: eh vector dtor over the cookie count
//                       (FUN_06d6001e) and operator delete of array-4 (FUN_06d5ffb0)
//   06d55b4e            return 0
SERVER_API int __cdecl SetMissionObjectives(const unsigned int& player_id, const unsigned int& mission_id, const MissionObjective* objs, unsigned int count, const FmtStr& title, unsigned char type, const FmtStr& desc) {
    if (!IsValidPlayer(player_id)) return -2;

    FServerSetMissionObjectivesInfo Payload;
    Payload.Field_0x00 = mission_id;                                      // .text:06d55a4c
    Payload.Count = count;                                                // .text:06d55a59
    Payload.TextA = title;                                                // .text:06d55a5d
    Payload.Field_0x3c = type;                                            // .text:06d55a6c
    Payload.TextB = desc;                                                 // .text:06d55a70

    FServerMissionObjectiveEntry* Entries = nullptr;
    if (count != 0) {
        unsigned char* const Block = static_cast<unsigned char*>(
            operator new(count * sizeof(FServerMissionObjectiveEntry) + 4)); // .text:06d55a7f
        if (Block != nullptr) {
            *reinterpret_cast<unsigned int*>(Block) = count;              // .text:06d55aa5
            Entries = reinterpret_cast<FServerMissionObjectiveEntry*>(Block + 4);
            for (unsigned int I = 0; I < count; ++I)
                new (&Entries[I]) FServerMissionObjectiveEntry;           // .text:06d55aa7
        }
        for (unsigned int I = 0; I < count; ++I) {                        // .text:06d55ac5
            Entries[I].Text = objs[I].Description;                        // .text:06d55ad0
            Entries[I].State = objs[I].Type;                              // .text:06d55add
        }
    }
    Payload.Entries = Entries;                                            // .text:06d55ab6

    ::g_pClientInterface->SendServerSetMissionObjectives(player_id, Payload);   // .text:06d55b00

    if (Entries != nullptr) {
        unsigned char* const Block = reinterpret_cast<unsigned char*>(Entries) - 4;
        unsigned int const Cookie = *reinterpret_cast<unsigned int*>(Block); // .text:06d55b0f
        for (unsigned int I = Cookie; I != 0; --I)
            Entries[I - 1].~FServerMissionObjectiveEntry();               // .text:06d55b1e
        operator delete(Block);                                           // .text:06d55b24
    }
    return 0;
}

// Player::ReplaceMissionObjective — updates specific objective in list
// Retail (0x75c10): validates player, replaces objective at index
// CHANGED 2026-09-17 (wave 18): this used to do
// `p->mission_log[index].timestamp = obj.Type`, writing a MissionObjective's type
// into a mission-log entry. That was a fabrication twice over -- retail's 0x75c10
// never touches PlayerData+0x390 at all, and the `timestamp` field it wrote into
// did not exist in retail either (the list holds FmtStr; see
// PlayerData::mission_log in common_server_playerdb.h). The bogus write is gone.
//
// Ported 2026-09-24 (claude-code:w5-server-06d4) off .text:06d55c10..06d55d0f:
//   guard -> -2 (EAX preset at .text:06d55c35)
//   .text:06d55c68..06d55ca4  FmtStr of the record zeroed inline
//   .text:06d55cb9            record+0x00 = *mission_id
//   .text:06d55cc6            record+0x20 = (byte)index
//   .text:06d55cca            record+0x04 = obj.Description (FmtStr::operator=)
//   .text:06d55cd2            record+0x1c = obj.Type
//   .text:06d55ce4            IClient slot 71 (+0x11c)(&player_id, &record), untested
//   .text:06d55cf6            ~FmtStr; return 0
// Nothing is written to the player slot.
SERVER_API int __cdecl ReplaceMissionObjective(const unsigned int& player_id, const unsigned int& mission_id, unsigned int index, const MissionObjective& obj) {
    if (!IsValidPlayer(player_id)) return -2;
    FServerReplaceMissionObjectiveInfo Record;
    Record.Field_0x00 = mission_id;
    Record.State = static_cast<unsigned char>(index);
    Record.Text = obj.Description;
    Record.ObjectiveType = obj.Type;
    ::g_pClientInterface->SendServerReplaceMissionObjective(player_id, Record);
    return 0;
}

// Player::SetMissionObjectiveState -- retail 0x75d20 (.text:06d55d20..06d55d8b),
// ordinal 426. Ported 2026-09-24 (claude-code:w5-server-06d4):
//   guard -> -2; record {+0x00 = *mission_id (.text:06d55d61), byte +0x04 = (byte)index
//   (.text:06d55d78), +0x08 = state (.text:06d55d69)}; IClient slot 73 (+0x124)
//   (&player_id, &record) with DAT_06d8fb14 untested (.text:06d55d7f); return 0.
SERVER_API int __cdecl SetMissionObjectiveState(const unsigned int& player_id, const unsigned int& mission_id, int index, unsigned int state) {
    if (!IsValidPlayer(player_id)) return -2;
    FServerMissionObjectiveStateInfo Record;
    Record.Field_0x00 = mission_id;
    Record.Field_0x04 = static_cast<unsigned char>(index);
    Record.Field_0x08 = state;
    ::g_pClientInterface->SendServerSetMissionObjectiveState(player_id, Record);
    return 0;
}

} // namespace Player

// ===========================================================================
// pub::GF exports (Game Framework) -- gf/pubGF.cpp. Every object the family
// creates lives in the BaseDB (FUN_06d15650 = GetBaseDB(), src/Server/server_basedb.cpp).
// ===========================================================================
namespace GF {

// ---- FindBase / FindLocation / GetSpaceflightLocation ----

// pub::GF::FindBase — retail 0x37ca0: CreateID(name), returns hash
// FUN_06d141f0 simply calls CreateID
SERVER_API unsigned int __cdecl FindBase(const char* name) {
    // No null test on name in retail (.text:06d17ca0..06d17cb1 -> FUN_06d141f0,
    // whose body is a bare CreateID call); the port's guard was removed 2026-09-24.
    return CreateID(name);
}

// pub::GF::FindLocation — retail 0x37cc0: MakeLocationID(system_id, name)
// FUN_06d14210 simply calls MakeLocationID
SERVER_API unsigned int __cdecl FindLocation(unsigned int base_id, const char* name) {
    // Retail 0x06d17cca/0x06d17cd1 -> FUN_06d14210, whose entire body (0x06d14210-
    // 0x06d14223) is `PUSH param_2; PUSH param_1; CALL [COMMON.DLL::MakeLocationID]`.
    // The port used to call CreateID(name) and DISCARD the first argument entirely, so
    // every base produced the same location id for a given room name. Fixed.
    // The `!name` guard (absent from both retail bodies) was removed 2026-09-24.
    return MakeLocationID(base_id, name);
}

// pub::GF::GetSpaceflightLocation — retail 0x37ce0
SERVER_API unsigned int __cdecl GetSpaceflightLocation() {
    // Retail 0x06d17ce0-0x06d17ce7 is two instructions: load the IAT slot
    // PTR_SpaceflightLocationID_06d647b0 (COMMON.DLL data export
    // ?SpaceflightLocationID@@3UID_String@@A) and dereference it. The port had a private
    // `static unsigned int g_SpaceflightLocationID = 0;` that nothing ever wrote, so this
    // always returned 0. The real global is already ported: common_misc.cpp's
    // SpaceflightLocationID = CreateID("Spaceflight"). Fixed.
    return ::SpaceflightLocationID.Id;
}

/// ---- GetBasePosition ----

// pub::GF::GetBasePosition -- retail .text:06d17d20 .. .text:06d17e6b. Single-entry memo cache
// DAT_06d8e578 (base id) + DAT_06d8e57c/580/584 (the Vector), reset by LAB_06d17cf0.
static unsigned int GCachedBasePosId = 0;
static float GCachedBasePos[3] = { 0, 0, 0 };

// Retail's exported name is ?GetBasePosition@GF@pub@@YAHABI0AAVVector@@@Z: "ABI0" means
// BOTH id parameters are `unsigned int const&` (the `0` back-references arg 1's type).
// Ported 1:1 2026-09-24 (claude-code:w6-sys):
//   06d17d26 .. 06d17d53  base_id & 0x3fffffff == 0 -> -2, then system_id & 0x3fffffff == 0
//                         -> -2 (base first, system second)
//   06d17d59 .. 06d17d91  base_id == cached id -> *out_pos = cached Vector, return 0
//   06d17d93              Universe::get_system(system_id) -- no null test after it
//   06d17d99 .. 06d17dc2  clear the four TString fields of the stack object record
//   06d17dd0 .. 06d17ddb  count = ISystem slot 15 (vt+0x3c); unsigned JBE 0 -> -2
//   06d17de0 .. 06d17e0a  for index < count (unsigned): slot 16 (vt+0x40)(index, &record);
//                         true and record +0x80 (dock_with) == base_id -> match
//   06d17e1d .. 06d17e50  cache = {base_id, record +0x44 pos}; *out_pos = pos; return 0
//   06d17e0c              no match -> -2
SERVER_API int __cdecl GetBasePosition(const unsigned int& system_id, const unsigned int& base_id, Vector& out_pos) {
    if ((base_id & 0x3fffffff) == 0) return -2;
    if ((system_id & 0x3fffffff) == 0) return -2;

    if (base_id == GCachedBasePosId) {
        out_pos.x = GCachedBasePos[0];
        out_pos.y = GCachedBasePos[1];
        out_pos.z = GCachedBasePos[2];
        return 0;
    }

    Universe::ISystem const* Sys = Universe::get_system(system_id);
    Universe::FSystemObject Record;
    Record.Nickname.Length = 0;
    Record.Nickname.Data[0] = '\0';
    Record.Reputation.Length = 0;
    Record.Reputation.Data[0] = '\0';
    Record.NextRing.Length = 0;
    Record.NextRing.Data[0] = '\0';
    Record.PrevRing.Length = 0;
    Record.PrevRing.Data[0] = '\0';

    unsigned int const Count = (unsigned int)Sys->GetObjectCount();
    for (unsigned int Index = 0; Index < Count; ++Index) {
        if (!Sys->GetSystemObject(Index, &Record)) continue;
        if (Record.DockWith != base_id) continue;
        GCachedBasePosId = base_id;
        GCachedBasePos[0] = Record.Pos.x;
        GCachedBasePos[1] = Record.Pos.y;
        GCachedBasePos[2] = Record.Pos.z;
        out_pos.x = Record.Pos.x;
        out_pos.y = Record.Pos.y;
        out_pos.z = Record.Pos.z;
        return 0;
    }
    return -2;
}

// ---- Costume / BodyPart / Accessory ----

// pub::GF::GetCostumeByID -- retail .text:06d17e70 .. .text:06d17e88:
// GetCostumeDescriptions()->get_costume(param_1, param_2), no null test.
SERVER_API void __cdecl GetCostumeByID(int id, Costume& out_costume) {
    GetCostumeDescriptions()->get_costume(id, out_costume);
}

// pub::GF::GetBodyPart -- retail .text:06d17e90 .. .text:06d17ed8:
// GetCostumeDescriptions()->find_bodypart(param_2, param_1); a hit returns the
// record's first dword, a miss FDUMPs "Cannot find bodypart %s" (pubGF.cpp line
// 0x78) with the name and returns 0. No null tests.
SERVER_API int __cdecl GetBodyPart(const char* name, int type_id) {
    bodypart const* bp = GetCostumeDescriptions()->find_bodypart(type_id, name);
    if (bp) return *(int const*)bp;
    FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot find bodypart %s\n",
          "E:\\FL\\Scratch\\Source\\Server\\gf\\pubGF.cpp", 0x78, name);
    return 0;
}

// pub::GF::GetAccessory -- retail .text:06d17ee0 .. .text:06d17f24: the same
// with find_accessory(param_1), "Cannot find accessory %s", line 0x87.
SERVER_API int __cdecl GetAccessory(const char* name) {
    accessory const* acc = GetCostumeDescriptions()->find_accessory(name);
    if (acc) return *(int const*)acc;
    FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot find accessory %s\n",
          "E:\\FL\\Scratch\\Source\\Server\\gf\\pubGF.cpp", 0x87, name);
    return 0;
}

// pub::GF::GetCostumeSkeletonGender -- retail .text:06d180c0 .. .text:06d180db:
// *param_2 = GetCostumeDescriptions()->get_costume_gender(param_1); return 0.
// No null test.
SERVER_API int __cdecl GetCostumeSkeletonGender(Costume const& desc, int& out_gender) {
    out_gender = GetCostumeDescriptions()->get_costume_gender(desc);
    return 0;
}

// ---- Character Create/Destroy/SetBehavior ----

// pub::GF::CharacterCreate -- retail .text:06d17f30 .. .text:06d17fa4: a local
// CharacterProperties, init(description), FUN_06d14870(BaseDB, &props,
// description+0x50 = the session filter), the destructor, the handle.
SERVER_API unsigned int __cdecl CharacterCreate(const CharacterDescription& description) {
    CharacterProperties Props;
    Props.init(description);
    return (unsigned int)GetBaseDB()->create_character(Props, description.Filter);
}

// pub::GF::CharacterDestroy -- retail .text:06d17fb0 .. .text:06d17fd2:
// `TEST [param_1],0x3fffffff / JZ out`, FUN_06d149c0(BaseDB, *param_1),
// *param_1 = 0. No null test of the pointer.
SERVER_API void __cdecl CharacterDestroy(unsigned int* handle) {
    if ((*handle & 0x3fffffff) != 0) {
        GetBaseDB()->destroy_character(*handle);
        *handle = 0;
    }
}

// pub::GF::CharacterSetBehavior -- retail .text:06d17fe0 .. .text:06d17ff6: an
// unconditional FUN_06d14ac0(BaseDB, param_1, param_2).
SERVER_API void __cdecl CharacterSetBehavior(unsigned int character_id, unsigned long behavior_handle) {
    GetBaseDB()->set_character_behavior(character_id, behavior_handle);
}

// ---- CharacterBehavior Create/Destroy ----

// pub::GF::CharacterBehaviorCreate -- retail .text:06d18000 .. .text:06d18089: a
// local ScriptBehavior, init(description), FUN_06d14b70(BaseDB, &sb,
// description+0x25c = the session filter), the destructor, the handle.
SERVER_API unsigned long __cdecl CharacterBehaviorCreate(const CharacterBehaviorDescription& description) {
    ScriptBehavior Behavior;
    Behavior.init(description);
    return GetBaseDB()->create_behavior(Behavior, description.Filter);
}

// pub::GF::CharacterBehaviorDestroy -- retail .text:06d18090 .. .text:06d180b0:
// `CMP [param_1],-1 / JZ out`, FUN_06d14cc0(BaseDB, *param_1), *param_1 = -1.
SERVER_API void __cdecl CharacterBehaviorDestroy(unsigned long* handle) {
    if (*handle != 0xFFFFFFFF) {
        GetBaseDB()->destroy_behavior(*handle);
        *handle = 0xFFFFFFFF;
    }
}

// ---- Character Placement functions ----
// All use FUN_06d14170(BaseDB, location_id) → RoomData*, then FUN_06d127XX on RoomData

// pub::GF::GetNumCharacterPlacements — retail 0x380e0
// FUN_06d12720: reads RoomData+0x178/0x17C (placement array begin/end), returns count
SERVER_API int __cdecl GetNumCharacterPlacements(unsigned int location_id) {
    // FUN_06d14170 (find_location; a miss FDUMPs 'Location %u is invalid'),
    // null -> return; the FUN_06d127xx helper then reads [room+0x8] unchecked.
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return 0;
    RoomData* room = Entry->Data;
    return (int)room->CharacterPlacements.size();
}

// pub::GF::GetCharacterPlacementByName — retail 0x38100
// FUN_06d12740: iterates placements comparing names via stricmp, returns index or -1
SERVER_API int __cdecl GetCharacterPlacementByName(unsigned int location_id, const char* name, int& out_index) {
    // Retail 0x06d18100-0x06d1813e has no guard on param_3. The port opened with
    // `if (!name || !out_index) return -2;` -- out_index being an `int&`, `!out_index`
    // tested the caller's current VALUE, so the ordinary `int idx = 0; ...(loc, n, idx);`
    // call shape always bailed out with -2 before searching anything. Removed.
    // 2026-09-24 (claude-code:server-triage-s2): the `!name` early -2 and the
    // `cp &&` element test went too. FUN_06d12740 (.text:06d12740..06d127a8)
    // reads each record's first dword through the element pointer with no null
    // test and hands `name` straight to stricmp; only the record's name pointer
    // is tested for null.
    // FUN_06d14170 (find_location; a miss FDUMPs 'Location %u is invalid'),
    // null -> return; the FUN_06d127xx helper then reads [room+0x8] unchecked.
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return -2;
    RoomData* room = Entry->Data;
    for (unsigned int i = 0; i < room->CharacterPlacements.size(); i++) {
        CharPlaceInfo* cp = room->CharacterPlacements[i];
        if (cp->Name && _stricmp(cp->Name, name) == 0) {
            out_index = (int)i;
            return 0;
        }
    }
    out_index = -1;
    return -3;
}

// pub::GF::GetCharacterPlacementName — retail 0x38140
// FUN_06d127b0: returns placement[index]->name (first dword of placement struct)
SERVER_API const char* __cdecl GetCharacterPlacementName(unsigned int location_id, int index) {
    // FUN_06d127b0 (.text:06d127b0..06d127c0) is a bare `placements[index]`: no
    // bounds test, no element null test; .text:06d18161 then returns the record's
    // first dword as-is, a null name included. The bounds/null guards the port had
    // here were removed 2026-09-24 (claude-code:server-triage-s2).
    // FUN_06d14170 (find_location; a miss FDUMPs 'Location %u is invalid'),
    // null -> return; the FUN_06d127xx helper then reads [room+0x8] unchecked.
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return "";
    RoomData* room = Entry->Data;
    return room->CharacterPlacements[index]->Name;
}

// pub::GF::GetCharacterPlacementOccupancy — retail 0x38170
// FUN_06d127b0 + offset 0x34: reads occupancy from placement struct
// CharPlaceInfo+0x34 is CharType (-1=none, 0=npc, 1=pc, 2=rpc; see common_goods.h and
// RoomData::read_set_script's Setpoints->CharacterPlacements derivation in common_roomdata.cpp).
SERVER_API unsigned long __cdecl GetCharacterPlacementOccupancy(unsigned int location_id, int index) {
    // .text:06d1818c FUN_06d127b0 (unchecked `placements[index]`) then
    // .text:06d18191 `MOV EAX,[EAX+0x34]`. Bounds/null guards removed 2026-09-24.
    // FUN_06d14170 (find_location; a miss FDUMPs 'Location %u is invalid'),
    // null -> return; the FUN_06d127xx helper then reads [room+0x8] unchecked.
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return 0;
    RoomData* room = Entry->Data;
    return (unsigned long)room->CharacterPlacements[index]->CharType;
}

// pub::GF::GetCharacterPlacementPosture — retail 0x381a0
// FUN_06d127b0 + offset 0x38: reads posture from placement struct
// CharPlaceInfo+0x38 is Posture (0=stand, 1=sitlow, 2=sithigh, 3=special, -1=unset).
SERVER_API int __cdecl GetCharacterPlacementPosture(unsigned int location_id, int index, unsigned long& out_posture) {
    // Retail 0x06d181a0-0x06d181d5 has no guard on param_3; the port's
    // `if (!out_posture) return -2;` tested the REFERENCE'S VALUE (same defect as
    // GetCharacterPlacementByName above) and short-circuited every zero-initialised
    // caller. Removed.
    // .text:06d181c4 FUN_06d127b0 (unchecked `placements[index]`) then
    // .text:06d181c9 `MOV EAX,[EAX+0x38]`. Bounds/null guards removed 2026-09-24.
    // FUN_06d14170 (find_location; a miss FDUMPs 'Location %u is invalid'),
    // null -> return; the FUN_06d127xx helper then reads [room+0x8] unchecked.
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return -2;
    RoomData* room = Entry->Data;
    out_posture = (unsigned long)room->CharacterPlacements[index]->Posture;
    return 0;
}

// pub::GF::IsCharacterPlacementNormal — retail 0x381e0
// FUN_06d127b0 + offset 0x3C: checks flag == 0
// CharPlaceInfo+0x3C is bZoneScript (0="zg/" zone-group prefix = normal, 1="zs/" zone-script prefix = special).
SERVER_API bool __cdecl IsCharacterPlacementNormal(unsigned int location_id, int index) {
    // .text:06d181ff FUN_06d127b0 (unchecked) then .text:06d18204
    // `MOV BL,[EAX+0x3c]`. Bounds/null guards removed 2026-09-24.
    // FUN_06d14170 (find_location; a miss FDUMPs 'Location %u is invalid'),
    // null -> return; the FUN_06d127xx helper then reads [room+0x8] unchecked.
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return true;
    RoomData* room = Entry->Data;
    return room->CharacterPlacements[index]->bZoneScript == 0;
}

// pub::GF::IsCharacterPlacementSpecial — retail 0x38210
// FUN_06d127b0 + offset 0x3C: checks flag == 1
SERVER_API bool __cdecl IsCharacterPlacementSpecial(unsigned int location_id, int index) {
    // .text:06d1822f FUN_06d127b0 (unchecked) then `MOV BL,[EAX+0x3c]` /
    // `CMP BL,0x1`. Bounds/null guards removed 2026-09-24.
    // FUN_06d14170 (find_location; a miss FDUMPs 'Location %u is invalid'),
    // null -> return; the FUN_06d127xx helper then reads [room+0x8] unchecked.
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return false;
    RoomData* room = Entry->Data;
    return room->CharacterPlacements[index]->bZoneScript == 1;
}

// pub::GF::EnumerateCharacterPlacementIni — retail 0x38250
// FUN_06d12850: delegates to RoomData::enumerate_CharacterPlacement_ini
// ?EnumerateCharacterPlacementIni@GF@pub@@YAXIP6AXHPAVINI_Reader@@PAX@Z1@Z -- the
// trailing `1` repeats PAX, so the context is a void*. Retail (.text:06d18250..06d18276)
// has no callback null test: GetBaseDB -> FUN_06d14170(id), null room -> RET, else
// FUN_06d12850(room, callback, context).
SERVER_API void __cdecl EnumerateCharacterPlacementIni(unsigned int location_id,
    void (__cdecl *callback)(int, INI_Reader*, void*), void* context) {
    // FUN_06d14170 (find_location; a miss FDUMPs 'Location %u is invalid'),
    // null -> return; the FUN_06d127xx helper then reads [room+0x8] unchecked.
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return;
    RoomData* room = Entry->Data;
    room->enumerate_CharacterPlacement_ini(callback, context);
}

// pub::GF::GetCharacterOnPlacement -- retail .text:06d18280 .. .text:06d182a9:
// FUN_06d14170(BaseDB, param_1); 0 -> return 0; else FUN_06d127d0(room,
// param_2 = the session filter, param_3 = the placement index).
SERVER_API unsigned int __cdecl GetCharacterOnPlacement(unsigned int location_id, unsigned int filter_id, int placement_index) {
    FRoomEntry* room = GetBaseDB()->find_location(location_id);
    if (!room) return 0;
    return room->get_character_on_placement(filter_id, placement_index);
}

// pub::GF::GetCharacterPlacementProperties — retail 0x382b0
// Gets placement name, then SetpointProperties::extract_from_name
SERVER_API bool __cdecl GetCharacterPlacementProperties(unsigned int location_id, int index, SetpointProperties* out_props) {
    // .text:06d182b0..06d182f0: no test on param_3; FUN_06d127b0 (unchecked
    // `placements[index]`), `MOV EAX,[EAX]`, and only the NAME is null-tested
    // (.text:06d182d6). The out_props / bounds / element guards were removed
    // 2026-09-24 (claude-code:server-triage-s2).
    // FUN_06d14170 (find_location; a miss FDUMPs 'Location %u is invalid'),
    // null -> return; the FUN_06d127xx helper then reads [room+0x8] unchecked.
    FRoomEntry* Entry = GetBaseDB()->find_location(location_id);
    if (!Entry) return false;
    RoomData* room = Entry->Data;
    CharPlaceInfo* cp = room->CharacterPlacements[index];
    if (!cp->Name) return false;
    out_props->extract_from_name(cp->Name);
    return out_props->bValid != 0;
}

// pub::GF::FormCharacterPlacementName -- retail .text:06d18300 .. .text:06d18304:
// `MOV ECX,[ESP+4] / JMP SetpointProperties::combine_to_name`, no null test.
SERVER_API const char* __cdecl FormCharacterPlacementName(SetpointProperties const* props) {
    return props->combine_to_name();
}

// ---- MissionVendor ----

// pub::GF::MissionVendorOfferCreate -- retail .text:06d18310 .. .text:06d18384:
// local MissionVendorProperties, init(description), FUN_06d14dc0(BaseDB, &props,
// description+0x5c = the session filter), the destructor, the handle.
SERVER_API unsigned long __cdecl MissionVendorOfferCreate(const MissionVendorOfferDescription& description) {
    MissionVendorProperties Props;
    Props.init(description);
    return GetBaseDB()->create_mission_vendor(Props, description.Filter);
}

// pub::GF::MissionVendorOfferDestroy -- retail .text:06d18390: `CMP [p],-1`,
// FUN_06d14f10(BaseDB, *p), *p = -1.
SERVER_API void __cdecl MissionVendorOfferDestroy(unsigned long* handle) {
    if (*handle != 0xFFFFFFFF) {
        GetBaseDB()->destroy_mission_vendor(*handle);
        *handle = 0xFFFFFFFF;
    }
}

// pub::GF::GetMissionVendorOfferCount -- retail .text:06d183c0 .. .text:06d183d6:
// FUN_06d15010(BaseDB, param_1, param_2) returned as-is.
SERVER_API int __cdecl GetMissionVendorOfferCount(unsigned int base_id, unsigned int vendor_type) {
    return GetBaseDB()->count_mission_vendors(base_id, vendor_type);
}

// pub::GF::MissionVendorAcceptance -- retail .text:06d183e0 .. .text:06d18400: an
// unconditional FUN_06d150a0(BaseDB, param_1, param_2, param_3, param_4).
SERVER_API void __cdecl MissionVendorAcceptance(unsigned long handle, bool accepted, const FmtStr& reason_fmtstr, unsigned int session_id) {
    GetBaseDB()->mission_vendor_acceptance(handle, accepted, reason_fmtstr, session_id);
}

// pub::GF::ReportWhyMissionVendorEmpty — retail 0x38410
// ?ReportWhyMissionVendorEmpty@GF@pub@@YAHIW4MVEmptyReason@@@Z -- a global enum.
SERVER_API int __cdecl ReportWhyMissionVendorEmpty(unsigned int client_id, MVEmptyReason reason) {
    // Retail 0x06d18410-0x06d1842a, all nine instructions: load DAT_06d8fb14, load its
    // vtable, PUSH param_2 then PUSH param_1, CALL [vtable+0xa8], XOR EAX,EAX.
    // DAT_06d8fb14 is the IClient pointer (docs/re/server.md), and +0xa8 is
    // IClient::SendServerGFMissionVendorWhyEmpty = FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY,
    // channel 2 opcode 0x5a, arity 2 (docs/re/rpclocal.md). So this is a real packet
    // send, not the "debug/diagnostic, stub is acceptable" the port had assumed --
    // another vtable offset mistaken for something else. Push order gives (client_id,
    // reason): the first argument is the ClientId the packet goes to (it was misnamed
    // `base_id` until 2026-10-16; Content's callers pass the mission state's PlayerId).
    // DAT_06d8fb14 is called through with no null test (.text:06d18414 .. 06d18422).
    ::g_pClientInterface->SendServerGFMissionVendorWhyEmpty(client_id, (unsigned int)reason);
    return 0;
}

// ---- NewsBroadcast ----

// pub::GF::NewsBroadcastCreate -- retail .text:06d18430: local
// NewsBroadcastProperties, init(description), FUN_06d15140(BaseDB, &props,
// description+0x5c = the session filter), the destructor, the handle.
SERVER_API unsigned long __cdecl NewsBroadcastCreate(const NewsBroadcastDescription& description) {
    NewsBroadcastProperties Props;
    Props.init(description);
    return GetBaseDB()->create_news_broadcast(Props, description.Filter);
}

// pub::GF::NewsBroadcastDestroy -- retail .text:06d184b0: `CMP [p],-1`,
// FUN_06d15290(BaseDB, *p), *p = -1.
SERVER_API void __cdecl NewsBroadcastDestroy(unsigned long* handle) {
    if (*handle != 0xFFFFFFFF) {
        GetBaseDB()->destroy_news_broadcast(*handle);
        *handle = 0xFFFFFFFF;
    }
}

// ---- AmbientScript ----

// pub::GF::AmbientScriptCreate -- retail .text:06d184e0 .. .text:06d18554: local
// AmbientScriptProperties, init(description), FUN_06d15390(BaseDB, &props,
// description+0x10 = the session filter), the destructor, the handle.
SERVER_API unsigned long __cdecl AmbientScriptCreate(const AmbientScriptDescription& description) {
    AmbientScriptProperties Props;
    Props.init(description);
    return GetBaseDB()->create_ambient_script(Props, description.Filter);
}

// pub::GF::AmbientScriptDestroy -- retail .text:06d18560 .. .text:06d18580:
// `CMP [p],-1`, FUN_06d15510(BaseDB, *p), *p = -1.
SERVER_API void __cdecl AmbientScriptDestroy(unsigned long* handle) {
    if (*handle != 0xFFFFFFFF) {
        GetBaseDB()->destroy_ambient_script(*handle);
        *handle = 0xFFFFFFFF;
    }
}

// ---- GetRtcPerformanceSlider ----

// pub::GF::GetRtcPerformanceSlider — retail 0x38590: if !IsMPServer(), RtcSlider::get_slider(); else 1.0
SERVER_API float __cdecl GetRtcPerformanceSlider() {
    // Retail 0x06d18590-0x06d185b1: stage 1.0f in a stack local, CALL
    // [COMMON.DLL::IsMPServer]; if it returns false, tail-JMP
    // [COMMON.DLL::RtcSlider::get_slider]; otherwise FLD that 1.0f.
    // The port used SinglePlayer() (a different predicate) and returned a hard-coded
    // 1.0f on BOTH branches, so the real slider was never read. RtcSlider::get_slider is
    // already ported (src/include/common_newchar.h, src/Common/common_rtcslider.cpp).
    if (!::IsMPServer()) {
        return ::RtcSlider::get_slider();
    }
    return 1.0f;
}

} // namespace GF

// ===========================================================================
// pub::Reputation exports
// ===========================================================================

} // namespace pub -- the vibe-id bitmap is file-level Server state, not pub::

// Internal: vibe ID bitmap, retail .data:06d8f768 (0x1000 bits, a
// std::bitset<0x1000> -- FUN_06d15f90 is its out_of_range throw) and the last
// allocated index .data:06d8f968. Both are .bss, so both start at 0.
static unsigned int g_VibeBitmap[128] = {};
static unsigned int g_VibeBitmapHint = 0;

// ---------------------------------------------------------------------------
// Server_VibeID_Alloc -- retail FUN_06d45b60 (.text:06d45b60 .. .text:06d45c18),
// cdecl (unsigned int* Out), plain RET. Callers: FUN_06d4a5d0's record copy
// (.text:06d4ace3, PlayerData_ApplySavedCharacter, declared in
// server_savegame.h) and pub::Reputation below. CORRECTED 2026-09-24
// (claude:srv-save-2): it used to be defined inside `namespace pub`, so the
// global declaration in server_savegame.h never linked (LNK2019), and its loop
// also tried the hint slot itself last, which retail never does.
//   06d45b67  i = hint + 1; BL = 1 (first lap)
//   06d45b72  i >= 0x1000 -> i = 0 (the bitset range checks at 06d45b76 /
//             06d45bc9 cannot fire)
//   06d45ba3  bit i free: BL ? take it : fail (06d45bc5)
//             bit i set:  BL ? next : fail (06d45bac)
//   06d45bb0  next: ++i; i >= 0x1000 -> i = 0 WITHOUT the hint test (06d45bb7
//             -> 06d45b74); else i == hint -> BL = 0 (06d45bc1), so arriving
//             back at the hint fails whatever its bit says
//   06d45bf5  take: set bit i, hint = i, *Out = i (the `JL` at 06d45c01 on a
//             value below 0x1000 cannot fire)
//   06d45c0c  fail: *Out = 0xffffffff
// ---------------------------------------------------------------------------
void Server_VibeID_Alloc(unsigned int* OutId)
{
    unsigned int Index = g_VibeBitmapHint + 1;
    bool bFirstLap = true;
    if (Index >= 0x1000)
        Index = 0;
    for (;;) {
        unsigned int const Mask = 1u << (Index & 0x1f);
        bool const bTaken = (g_VibeBitmap[Index >> 5] & Mask) != 0;
        if (!bFirstLap)
            break;
        if (!bTaken) {
            g_VibeBitmap[Index >> 5] |= Mask;
            g_VibeBitmapHint = Index;
            *OutId = Index;
            return;
        }
        ++Index;
        if (Index >= 0x1000)
            Index = 0;
        else if (Index == g_VibeBitmapHint)
            bFirstLap = false;
    }
    *OutId = 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// Server_VibeID_Free -- retail FUN_06d45c20 (.text:06d45c20 .. .text:06d45c5a),
// cdecl (unsigned int const* Id), plain RET. Only caller pub::Reputation::Free
// (.text:06d5f3f0), which pushes the `int const&` it received -- the address,
// not the value (.text:06d5f3ed PUSH ESI, ESI = [ESP+param_1]).
//   06d45c25  i = *Id
//   06d45c27  i >= 0x1000 -> FUN_06d15f90, std::bitset's _Xran: throws
//             std::out_of_range("invalid bitset<N> position") and never returns
//   06d45c39  bit i cleared (reset(i))
// The earlier port took the id by value and silently skipped an out-of-range
// id; retail throws.
// ---------------------------------------------------------------------------
static void Server_VibeID_Free(unsigned int const* Id)
{
    unsigned int const Index = *Id;
    if (Index >= 0x1000)
        throw std::out_of_range("invalid bitset<N> position");
    g_VibeBitmap[Index >> 5] &= ~(1u << (Index & 0x1f));
}

namespace pub {

// ---------------------------------------------------------------------------
// Server_NotifyRepChange -- retail FUN_06d5f130 (.text:06d5f130 .. .text:06d5f3dc),
// cdecl (int const* Vibe, unsigned int SessionId), plain RET. Callers
// pub::Reputation::Alloc (.text:06d5f120), SetAffiliation (.text:06d5f454),
// SetReputation (.text:06d5f495), SetAttitude (.text:06d5f559) and the
// group-pair SetReputation (.text:06d5f7f8). Both branches send ONE IClient
// call, +0x108 (SendServerSetReputation, ch2 op 0x29 FLPACKET_SERVER_SETREPUTATION), with an
// FServerSetReputationInfo (rpc_iclient_args.h) built on the stack. The old
// port posted an invented controller message 0x40 instead.
//
// SinglePlayer() (.text:06d5f14d):
//   .text:06d5f15a/06d5f168  FUN_06d55bc0 (FmtStr ctor) on +0x18 and +0x30
//   .text:06d5f17b..06d5f189 +0x10 = 0, byte +0x14 = 0, word +0x48 = 0
//   .text:06d5f1a5           +0x00 = *Vibe
//   .text:06d5f1ac..06d5f1d1 +0x10 = operator new(Reputation::group_count() * 8)
//   .text:06d5f1fc           Reputation::Vibe::Get(+0x00, +0x08, +0x0c, +0x14,
//                            +0x10, +0x18, +0x30, &Name) with Name = 0
//   .text:06d5f209..06d5f221 Name ? wcscpy(+0x48, Name) : word +0x48 = 0
//   .text:06d5f229..06d5f26c PlayerDB::traverse_active: SendServerSetReputation([p+0x348], &Info)
//   .text:06d5f26f..06d5f277 operator delete(+0x10) (FUN_06d5ffb0)
//   then ~FmtStr on +0x30 and +0x18. +0x04 is never written.
// MP (.text:06d5f2a5):
//   SessionId != 0, SessionId-1 < DAT_06d90264, Players[SessionId].+0x348 != 0,
//   else return (.text:06d5f2b0 / 06d5f2c1 / 06d5f2da -- no other check);
//   .text:06d5f2e0..06d5f33c +0x18 zeroed inline (the FmtStr ctor's stores),
//                            FmtStr ctor on +0x30, +0x10 = 0, byte +0x14 = 0,
//                            word +0x48 = 0
//   .text:06d5f358/06d5f363  +0x00 = *Vibe, a temp = Players[SessionId].+0x318
//   .text:06d5f381           Reputation::Vibe::AttitudeTowards(+0x00, &temp, +0x04)
//   .text:06d5f398           SendServerSetReputation(SessionId, &Info) -- the session id itself,
//                            not the slot's +0x348
//   then ~FmtStr on +0x30 and +0x18.
//   +0x08 / +0x0c are left as stack residue in the MP branch (rpclocal's sender
//   serializes +0x08); the port zeroes them -- no retail value exists to copy.
// ---------------------------------------------------------------------------
static void Server_NotifyRepChange(int const* vibe_handle, unsigned int session_id)
{
    if (SinglePlayer()) {
        FServerSetReputationInfo Info;
        Info.Attitude = 0.0f;               // never written by retail (stack residue)
        Info.Field_0x08 = 0;
        Info.Field_0x0c = 0;
        Info.Relations = nullptr;           // .text:06d5f17b
        Info.RelationCount = 0;             // .text:06d5f182
        Info.Pad_0x15[0] = 0;
        Info.Pad_0x15[1] = 0;
        Info.Pad_0x15[2] = 0;
        Info.DisplayName[0] = L'\0';        // .text:06d5f189
        Info.VibeHandle = *vibe_handle;     // .text:06d5f1a5
        Info.Relations = static_cast< ::Reputation::Relation*>(
            ::operator new(GlobalReputation::group_count() * 8));      // .text:06d5f1b6
        wchar_t const* VibeName = nullptr;                              // .text:06d5f1f8
        GlobalReputation::Vibe::Get(Info.VibeHandle, Info.Field_0x08, Info.Field_0x0c,
                                    Info.RelationCount, Info.Relations, Info.NameA,
                                    Info.NameB, VibeName);             // .text:06d5f1fc
        if (VibeName != nullptr) {
            wcscpy(Info.DisplayName, VibeName);                         // .text:06d5f216, unbounded
        } else {
            Info.DisplayName[0] = L'\0';                                // .text:06d5f221
        }
        for (PlayerData* Active = Players.traverse_active(nullptr); Active != nullptr;
             Active = Players.traverse_active(Active)) {
            g_pClientInterface->SendServerSetReputation(Active->client_id, Info); // .text:06d5f257
        }
        ::operator delete(Info.Relations);                              // .text:06d5f277
        return;
    }

    if (session_id == 0 || (session_id - 1) >= PlayerDB_GetMaxPlayers()) return;
    if (Players[session_id].client_id == 0) return;

    FServerSetReputationInfo Info;
    Info.Field_0x08 = 0;                    // stack residue in retail
    Info.Field_0x0c = 0;
    Info.Relations = nullptr;               // .text:06d5f334
    Info.RelationCount = 0;                 // .text:06d5f338
    Info.Pad_0x15[0] = 0;
    Info.Pad_0x15[1] = 0;
    Info.Pad_0x15[2] = 0;
    Info.DisplayName[0] = L'\0';            // .text:06d5f33c
    Info.VibeHandle = *vibe_handle;         // .text:06d5f358
    int const PlayerVibe = Players[session_id].rep_vibe;               // .text:06d5f363
    Info.Attitude = 0.0f;
    GlobalReputation::Vibe::AttitudeTowards(Info.VibeHandle, PlayerVibe,
                                            Info.Attitude);             // .text:06d5f381
    g_pClientInterface->SendServerSetReputation(session_id, Info);     // .text:06d5f398
}

namespace Reputation {

// pubGlobalReputation::Alloc — retail 0x7f0f0
// FUN_06d45b60 → Vibe::Alloc → Vibe::SetName → FUN_06d5f130
SERVER_API int __cdecl Alloc(int& vibe_handle, const FmtStr& name_fmtstr, const FmtStr& nickname_fmtstr) {
    // Line 89886: FUN_06d45b60(param_1) � allocate vibe ID from bitmap
    unsigned int vibe_id = (unsigned int)vibe_handle;
    Server_VibeID_Alloc(&vibe_id);
    vibe_handle = (int)vibe_id;
    // .text:06d5f0fd: Vibe::Alloc(param_1); a non-zero result skips SetName and the notify.
    int result = GlobalReputation::Vibe::Alloc(vibe_handle);
    if (result == 0) {
        // Line 89889: Vibe::SetName(param_1, param_2, param_3, nullptr)
        GlobalReputation::Vibe::SetName(vibe_handle, name_fmtstr, nickname_fmtstr, nullptr);
        // Line 89890: FUN_06d5f130(param_1, 0) � notify SP players
        int vibe_tmp = vibe_handle;
        Server_NotifyRepChange(&vibe_tmp, 0);
    }
    return result;
}

// pubGlobalReputation::Free — retail 0x7f3e0
// Vibe::Free → FUN_06d45c20
SERVER_API int __cdecl Free(const int& vibe_handle) {
    // Retail 0x06d5f3e0 is thirteen instructions with NO guard at all: it calls
    // Vibe::Free, keeps the result in EDI across FUN_06d45c20 and returns it.
    // The port's `if (vibe_handle == 0) return -1;` was invented.
    // Line 90036: Vibe::Free(param_1)
    int result = GlobalReputation::Vibe::Free(vibe_handle);
    // .text:06d5f3ed PUSH ESI / .text:06d5f3f0 FUN_06d45c20: the same reference,
    // passed on as a pointer -- clear the bitmap bit (throws when >= 0x1000).
    Server_VibeID_Free(reinterpret_cast<unsigned int const*>(&vibe_handle));
    return result;
}

// pubGlobalReputation::GetAffiliation — retail 0x7f400
// Vibe::GetAffiliation(handle, out, true)
SERVER_API int __cdecl GetAffiliation(const int& vibe_handle, unsigned int& out_affiliation) {
    // Line 90051: Vibe::GetAffiliation(param_1, param_2, true)
    int result = GlobalReputation::Vibe::GetAffiliation(vibe_handle, out_affiliation, true);
    if (result == 0) return 0;
    // Lines 90055-90056: on error, set 0xFFFFFFFF, return -2
    out_affiliation = 0xFFFFFFFF;
    return -2;
}

// pubGlobalReputation::SetAffiliation — retail 0x7f430
// Vibe::SetAffiliation → FUN_06d5f130
SERVER_API int __cdecl SetAffiliation(const int& vibe_handle, const unsigned int& affiliation) {
    // Retail 0x06d5f430 has no guard: it loads *param_2 at 0x06d5f434 and goes
    // straight into Vibe::SetAffiliation. The two-way zero check was invented.
    // Line 90069: Vibe::SetAffiliation(param_1, *param_2, false)
    int result = GlobalReputation::Vibe::SetAffiliation(vibe_handle, affiliation, false);
    if (result == 0) {
        // Line 90071: FUN_06d5f130(param_1, 0) — notify
        Server_NotifyRepChange(&vibe_handle, 0);
        return 0;
    }
    return -2;
}

// pubGlobalReputation::SetReputation (vibe overload) — retail 0x7f470
// Vibe::SetGroupFeelingsTowards → FUN_06d5f130
SERVER_API int __cdecl SetReputation(const int& vibe_handle, const unsigned int& group_id, float value) {
    // Line 90087: Vibe::SetGroupFeelingsTowards(param_1, param_2, param_3)
    int result = GlobalReputation::Vibe::SetGroupFeelingsTowards(vibe_handle, group_id, value);
    if (result == 0) {
        // Line 90089: FUN_06d5f130(param_1, 0)
        int vibe_tmp = vibe_handle;
        Server_NotifyRepChange(&vibe_tmp, 0);
        return 0;
    }
    return -2;
}

// pubGlobalReputation::GetReputation (vibe overload) — retail 0x7f4b0
// Vibe::GetGroupFeelingsTowards
SERVER_API int __cdecl GetReputation(const int& vibe_handle, const unsigned int& group_id, float& out_value) {
    // Line 90105: *param_3 = 0.0
    out_value = 0.0f;
    // Line 90106: Vibe::GetGroupFeelingsTowards(param_1, param_2, param_3)
    int result = GlobalReputation::Vibe::GetGroupFeelingsTowards(vibe_handle, group_id, out_value);
    if (result == 0) return 0;
    return -2;
}

// pubGlobalReputation::GetAttitude — retail 0x7f4e0
// Vibe::AttitudeTowards with special return code handling
SERVER_API int __cdecl GetAttitude(const int& vibe1, const int& vibe2, float& out_attitude) {
    // Line 90124: *param_3 = 0.0
    out_attitude = 0.0f;
    // Line 90125: Vibe::AttitudeTowards(param_1, param_2, param_3)
    int result = GlobalReputation::Vibe::AttitudeTowards(vibe1, vibe2, out_attitude);
    // Lines 90126-90136: complex return mapping
    if (result == 0) return 0;
    if (result == -1) return -2;
    // result == -2 → return pointer-as-int (retail quirk: returns param_3 ptr cast)
    // For our port, just return the attitude value as int or -3
    if (result == -2) return -3;
    return result;
}

// pubGlobalReputation::SetAttitude — retail 0x7f520
// Vibe::AdjustAttitude → SP check → GetClientID → FUN_06d5f130
SERVER_API int __cdecl SetAttitude(const int& vibe1, const int& vibe2, float attitude) {
    // Line 90152: Vibe::AdjustAttitude(param_1, param_2, param_3)
    int result = GlobalReputation::Vibe::AdjustAttitude(vibe1, vibe2, attitude);
    if (result == 0) {
        // Lines 90154-90159: if !SP, get target's ClientID, notify that session
        if (!SinglePlayer()) {
            unsigned int client_id = GlobalReputation::Vibe::GetClientID(vibe2);
            if (client_id != 0) {
                int vibe_tmp = vibe1;
                Server_NotifyRepChange(&vibe_tmp, client_id);
            }
        }
        return 0;
    }
    if (result == -1) return -2;
    // result == -2 → return attitude as int (retail quirk)
    if (result == -2) return -3;
    return result;
}

// pubGlobalReputation::GetRank — retail 0x7f590
// Vibe::GetRank
SERVER_API int __cdecl GetRank(const int& vibe_handle, float& out_rank) {
    // Retail 0x06d5f590 has no guard. On success it does FILD on a QWORD whose
    // high dword is the zeroed EAX (0x06d5f5bc / 0x06d5f5c0), i.e. the rank is
    // converted as an UNSIGNED value, which is why the temp below is unsigned.
    // Line 90183: Vibe::GetRank(param_1, (uint*)&param_1) — reuses param_1 as temp
    unsigned rank_val = 0;
    int result = GlobalReputation::Vibe::GetRank(vibe_handle, rank_val);
    if (result == 0) {
        // Line 90185: *param_2 = (float)param_1 (the rank value)
        out_rank = (float)rank_val;
        return 0;
    }
    // Lines 90188-90189: *param_2 = 0.0, return -2
    out_rank = 0.0f;
    return -2;
}

// pubGlobalReputation::SetRank — retail 0x7f5e0
// SP check → GetClientID → GetRank(old) → ftol → SetRank → notify if changed
SERVER_API int __cdecl SetRank(const int& vibe_handle, float rank) {
    // Lines 90208-90217: if !SP, get ClientID, save old rank
    unsigned int client_id = 0;
    // Retail seeds the old-rank slot with 0xffffffff, not 0 (0x06d5f5ec
    // MOV dword ptr [ESP+0x10],0xffffffff), so a rank of 0 still counts as a
    // change when GetRank below never runs.
    unsigned int old_rank = 0xffffffff;
    if (!SinglePlayer()) {
        client_id = GlobalReputation::Vibe::GetClientID(vibe_handle);
        if ((client_id & 0x3fffffff) != 0) {
            int cid = (int)client_id;
            GlobalReputation::Vibe::GetRank(cid, old_rank);
        }
    }
    // Line 90218: uVar3 = ftol(param_2) — convert float rank to unsigned
    unsigned new_rank = (unsigned)(int)rank;
    // Line 90219: Vibe::SetRank(param_1, uVar3)
    int result = GlobalReputation::Vibe::SetRank(vibe_handle, new_rank);
    if (result != 0) return -2;
    // Lines 90223-90227: if client_id valid && rank changed, notify all active players
    // Retail 0x06d5f648-0x06d5f68e: the 0x3fffffff mask IS real here (TEST EDI,
    // 0x3fffffff at 0x06d5f60f and again at 0x06d5f648 -- unlike the two places
    // where it was copied in by hand), the rank must actually have changed
    // (0x06d5f650 CMP EBX,[ESP+0x10]), and then it walks PlayerDB::traverse_active
    // calling IClient(DAT_06d8fb14)->[+0x1d4] with (pd[+0x348], client_id,
    // new_rank). 0x1d4 is a vtable BYTE offset: 0x1d4/4 = slot 117
    // (IClient::SendServerMiscObjUpdateRank), three
    // arguments, matching docs/re/rpclocal.md. The port used to fire an invented
    // controller message id 0x41 instead.
    // No interface null test (.text:06d5f668 loads DAT_06d8fb14 and calls).
    if ((client_id & 0x3fffffff) != 0 && new_rank != old_rank) {
        for (PlayerData* Active = Players.traverse_active(nullptr); Active != nullptr;
             Active = Players.traverse_active(Active)) {
            // .text:06d5f66e -- pd[+0x348], the slot's client id.
            g_pClientInterface->SendServerMiscObjUpdateRank(Active->client_id, client_id,
                                                            static_cast<int>(new_rank));
        }
    }
    return 0;
}

// pubGlobalReputation::GetReputationGroup — retail 0x7f6a0
// Copy name to TString<16>, call get_id
SERVER_API int __cdecl GetReputationGroup(unsigned int& out_id, const char* name) {
    // Lines 90246-90276: copy name to local TString<16> buffer (max 15 chars + null)
    char local_name[16] = {};
    if (name != nullptr) {
        unsigned int len = 0;
        while (len < 15 && name[len] != '\0') {
            local_name[len] = name[len];
            len++;
        }
        local_name[len] = '\0';
    }
    // Line 90277: get_id(TString<16>). Retail builds the TString in place --
    // local_14 (the length dword at +0) and local_10 (the 16 chars at +4) are
    // adjacent and it passes &local_14 at 0x06d5f6f9. The length is written at
    // 0x06d5f6f4 for a real name and zeroed at 0x06d5f6b9 for a null one; the
    // port used to memcpy only the characters and leave Length uninitialised.
    TString<16> tname;
    tname.Length = 0;
    while (tname.Length < 15 && local_name[tname.Length] != '\0') {
        ++tname.Length;
    }
    memcpy(tname.Data, local_name, 16);
    unsigned int id = GlobalReputation::get_id(tname);
    if (id != 0xFFFFFFFF) {
        out_id = id;
        return 0;
    }
    out_id = 0xFFFFFFFF;
    return -2;
}

// pubGlobalReputation::GetName — retail 0x7f730
// Vibe::GetName
SERVER_API int __cdecl GetName(const int& vibe_handle, FmtStr& out_name_fmta, FmtStr& out_name_fmtb) {
    // Line 90296: Vibe::GetName(param_1, param_2, param_3, (ushort**)&param_3)
    wchar_t const* wide_name = nullptr;
    int result = GlobalReputation::Vibe::GetName(vibe_handle, out_name_fmta, out_name_fmtb, wide_name);
    if (result == 0) return 0;
    return -2;
}

// pubGlobalReputation::GetReputation (uint,uint overload) — retail 0x7f760
// get_feelings_towards with -4 default for invalid IDs
// ?GetReputation@Reputation@pub@@YAHABI0AAM@Z -- an OVERLOAD of GetReputation; the
// port used to call it GetReputationByID, which exported an invented name.
SERVER_API int __cdecl GetReputation(const unsigned int& group_a, const unsigned int& group_b, float& out_value) {
    // Lines 90313-90321: check both != 0xFFFFFFFF
    if (group_a == 0xFFFFFFFF || group_b == 0xFFFFFFFF) return -4;
    int result = GlobalReputation::get_feelings_towards(group_a, group_b, out_value);
    if (result == 0) return 0;
    return -2;
}

// pubGlobalReputation::SetReputation (uint,uint overload) — retail 0x7f7a0
// get_feelings_towards → affect_relation → FUN_06d5f130
// ?SetReputation@Reputation@pub@@YAHABI0M@Z -- overload, formerly SetReputationByID.
SERVER_API int __cdecl SetReputation(const unsigned int& group_a, const unsigned int& group_b, float value) {
    // Lines 90339-90351: check both != 0xFFFFFFFF
    if (group_a == 0xFFFFFFFF || group_b == 0xFFFFFFFF) return -4;
    float current = 0.0f;
    int result = GlobalReputation::get_feelings_towards(group_a, group_b, current);
    if (result == 0) {
        // Line 90343: affect_relation(group_a, group_b, value - current)
        float delta = value - current;
        GlobalReputation::affect_relation(group_a, group_b, delta);
        // Line 90345: FUN_06d5f130(&group_a_copy, 0)
        int tmp = (int)group_a;
        Server_NotifyRepChange(&tmp, 0);
        return 0;
    }
    return -2;
}

// pubGlobalReputation::GetGroupName — retail 0x7f810
// get_name(group_id)
SERVER_API int __cdecl GetGroupName(const unsigned int& group_id, unsigned int& out_name_id) {
    // Line 90365: get_name(*param_1)
    unsigned int name_id = GlobalReputation::get_name(group_id);
    out_name_id = name_id;
    // Lines 90367-90371: return 0 if valid, -2 if 0xFFFFFFFF
    if (name_id != 0xFFFFFFFF) return 0;
    return -2;
}

// pubGlobalReputation::GetShortGroupName — retail 0x7f840
// get_short_name(group_id)
SERVER_API int __cdecl GetShortGroupName(const unsigned int& group_id, unsigned int& out_name_id) {
    // Line 90385: get_short_name(*param_1)
    unsigned int name_id = GlobalReputation::get_short_name(group_id);
    out_name_id = name_id;
    // Lines 90387-90391: return 0 if valid, -2 if 0xFFFFFFFF
    if (name_id != 0xFFFFFFFF) return 0;
    return -2;
}

// pubGlobalReputation::GetGroupFeelingsTowards — retail 0x7f870
// Vibe::GetGroupFeelingsTowards
SERVER_API int __cdecl GetGroupFeelingsTowards(const int& vibe_handle, const unsigned int& group_id, float& out_feeling) {
    // Line 90405: Vibe::GetGroupFeelingsTowards(param_1, param_2, param_3)
    int result = GlobalReputation::Vibe::GetGroupFeelingsTowards(vibe_handle, group_id, out_feeling);
    // Lines 90406-90410: return 0 unless result == -1 → -2
    if (result != -1) return 0;
    return -2;
}

// pubGlobalReputation::GetReputation (char* overload) — retail 0x7f8a0
// CreateID → SolarReps::FindSolarRep
// The export is ?GetReputation@Reputation@pub@@YAHAAHPBD@Z -- AAH is `int&`,
// not `int*`, so the out parameter is a reference. Retail never null-checks it:
// it writes through it unconditionally at 0x06d5f8ca.
SERVER_API int __cdecl GetReputation(int& out_rep, const char* name) {
    // Line 90423: CreateID(param_2)
    // Retail keeps the id in a stack ID_String (the param_2 slot, 06d5f8b1) and passes
    // its address (06d5f8ac).
    ID_String Key;
    Key.Id = CreateID(name);
    // Line 90424: SolarReps::FindSolarRep
    int rep = ::SolarReps::FindSolarRep(Key);
    out_rep = rep;
    // Lines 90426-90429: return 0 if found, -2 if -1
    if (rep != -1) return 0;
    return -2;
}

// pubGlobalReputation::GetReputation (ID_String overload) — retail 0x7f8e0
// SolarReps::FindSolarRep
// ?GetReputation@Reputation@pub@@YAHAAHABUID_String@@@Z -- `int&` plus
// `ID_String const&`, not (int*, unsigned int). Retail pushes param_2 straight
// through to SolarReps::FindSolarRep at 0x06d5f8eb, i.e. as an address, and
// writes the result through the reference at 0x06d5f8fb without a null check.
SERVER_API int __cdecl GetReputation(int& out_rep, const ID_String& id) {
    // Line 90442: SolarReps::FindSolarRep(param_2) -- ID_String const&, as Common exports it.
    int rep = ::SolarReps::FindSolarRep(id);
    out_rep = rep;
    // Lines 90444-90447: return 0 if found, -2 if -1
    if (rep != -1) return 0;
    return -2;
}

// pub::Reputation::EnumerateGroups — retail 0x7f910 (0x06d5f910), ordinal 120
// Ten instructions: build an 8-byte stack adapter { vtable = 0x06d5f940, the
// Enumerator& } at 0x06d5f91c/0x06d5f924, hand it to COMMON.DLL's
// Reputation::enumerate at 0x06d5f928, then XOR EAX,EAX -- it ALWAYS returns 0.
// The adapter's one virtual, the thunk at 0x06d5f940, dereferences the
// `unsigned const*` Common.dll passes and tail-jumps into the Enumerator's own
// slot 0 with the group id by value.
//
// The old port walked g_VibeBitmap -- the VIBE allocation bitmap -- and called
// a fabricated `bool(*)(int, void*)` out of an equally fabricated
// {CallbackFn, Context} Enumerator. Reputation groups are not vibes, and the
// real Enumerator is an abstract struct; both are fixed now.
SERVER_API int __cdecl EnumerateGroups(Enumerator& enumerator) {
    struct FRepGroupAdapter : GlobalReputation::RepGroupCB {
        Enumerator* Sink;
        virtual char Execute(unsigned const* GroupId) { return Sink->Execute(*GroupId); }
    };
    FRepGroupAdapter Adapter;
    Adapter.Sink = &enumerator;
    GlobalReputation::enumerate(&Adapter);
    return 0;
}

} // namespace Reputation

// ===========================================================================
// pub::Controller exports
// ===========================================================================
namespace Controller {

SERVER_API unsigned int __cdecl Create(const char* dll_path, const char* instance_name,
                            const CreateParms* create_parms, PRIORITY priority) {
    return ::Server_ControllerCreate(dll_path, instance_name, (void*)create_parms, (unsigned int)priority);
}

SERVER_API void __cdecl Destroy(unsigned int flid) {
    ::Server_ControllerDestroy(flid);
}

// pub::Controller::SetHeartbeatInterval — retail 0x64030 (0x06d44030). Read
// instruction by instruction: FUN_06d43600(&Flid) finds the controller node, and the
// whole body is then
//     node[+0x10] = Interval;
//     node[+0x14] = (float)rand() * (1/32768) * Interval;   // FMUL DAT_06d656b8 = 0x38000000
//     return 0;            // -2 when the node is not found
// There is NO pub::Controller message: the 0x3d the port used to send here was invented
// (0x3d is a real id, but it is fired from FUN_06d07151 / FUN_06d08871, not from here).
SERVER_API int __cdecl SetHeartbeatInterval(const unsigned int& Flid, float Interval) {
    return Server_ControllerSetHeartbeat(Flid, Interval);
}

// pub::Controller::_SendMessage — retail 0x64080 (0x06d44080). The Flid==0 case is not
// an error: it is the BROADCAST arm, and it is the one FUN_06d43650 (every bare
// "send message N" in Server.dll) uses. The previous port returned -2 for it, which
// silently swallowed every one of those sends.
SERVER_API int __cdecl _SendMessage(const unsigned int& Flid, EMessage MsgId, const void* MsgData) {
    return Server_ControllerDispatch(Flid, MsgId, MsgData);
}

} // namespace Controller

// pub::SetTimer — retail 0x7ee00 (0x06d5ee00). Retail checks ONLY `Timeout < 0` (-4);
// it never validates the Flid. The body then allocates a 0x18-byte node
// {next, prev, Flid, Timer.Param1, Timer.Param2, Timeout}, links it into the timer list
// at DAT_06d90468 and bumps DAT_06d9046c. No message is sent at insert time: message
// 0x3e is what the tick (0x06d5df60) sends when the timer EXPIRES, and it sends it to
// that one controller, not broadcast. The port's insert-time 0x3e broadcast was wrong
// on both counts.
SERVER_API int __cdecl SetTimer(const unsigned int& Flid, const Controller::TimerExpired& Timer, float Timeout) {
    return Server_ControllerSetTimer(Flid, Timer.Param1, Timer.Param2, Timeout);
}

// ===========================================================================
// pub::Zone exports — line-by-line verified against Server.dll.c
// ===========================================================================
namespace Zone {

// pub::Zone::VerifyId — retail 0x1b920
// Universe::get_zone(id) != nullptr
SERVER_API bool __cdecl VerifyId(unsigned int zone_id) {
    // Line 21372: get_zone(param_1)
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    // Line 21373: return pIVar1 != 0
    return zone != nullptr;
}

// pub::Zone::GetId — retail 0x1b940
// get_zone(sys_id, name) → vtable+4 → NicknameId
SERVER_API unsigned int __cdecl GetId(unsigned int system_id, const char* name) {
    // Line 21387: get_zone(param_1, param_2)
    Universe::IZone const* zone = Universe::get_zone(system_id, name);
    if (zone != nullptr) {
        // Line 21391: vtable+4 → returns NicknameId
        return zone->NicknameId;
    }
    // Line 21394: return 0
    return 0;
}

// pub::Zone::GetSystem — retail 0x1b970
// get_zone(id) → vtable+8 → SystemId
SERVER_API unsigned int __cdecl GetSystem(unsigned int zone_id) {
    // Line 21408: get_zone(param_1)
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        // Line 21412: vtable+8 → returns SystemId
        return zone->SystemId;
    }
    // Line 21415: return 0
    return 0;
}

// pub::Zone::GetPopulation — retail 0x1b990
// get_zone(id) → vtable+0x6c → weighted_vector ptr
// ?GetPopulation@Zone@pub@@YAHIAAPBV?$weighted_vector@I@@@Z -- a REFERENCE to a
// `weighted_vector<unsigned> const*` (AAPBV), not a pointer to one.
SERVER_API int __cdecl GetPopulation(unsigned int zone_id, const weighted_vector<unsigned int>*& out_population) {

    // Line 21429: get_zone(param_1)
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        // Retail 0x06cfb9ac: CALL [vtable+0x6c] then `MOV ECX,[ESP+0xc]; MOV [ECX],EAX`,
        // return 0. Slot +0x6c is Common LAB_0633ae40, `LEA EAX,[ECX+0x8c]`: the address
        // of the zone's faction population (Universe::IZone::Population).
        out_population = &zone->Population;
        return 0;
    }
    // Lines 21435-21436: *param_2 = nullptr, return -2
    out_population = nullptr;
    return -2;
}

// pub::Zone::GetPos — retail 0x1b9d0
// get_zone(id) → vtable+0x10 → copy Vector (3 dwords)
SERVER_API Vector __cdecl GetPos(unsigned int zone_id) {
    // Retail 0x06cfb9d0 is exactly `get_zone(id)`, then either copy 3 dwords out of
    // vtable+0x10's Vector or zero the hidden return slot. There is NO id-mask guard --
    // an earlier port carried an invented `(zone_id & 0x3fffffff) == 0` early-out that
    // has no counterpart in the listing; removed.
    Vector result = {};
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (!zone) return result;
    // Retail: vtable+0x10 returns Vector position
    result = zone->Pos;
    return result;
}

// pub::Zone::GetOrientation — retail 0x1ba20
// get_zone(id) → vtable+0x14 → copy Matrix (9 dwords loop)
SERVER_API int __cdecl GetOrientation(const unsigned int& zone_id, Matrix& out_matrix) {
    // Line 21477: get_zone(param_1)
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        // .text:06cfba3a-06cfba4d: vtable+0x14 (Common 062d1ae0, LEA +0x0c) → copy 9 dwords.
        out_matrix = zone->GetOrientation();
        return 0;
    }
    // Line 21487: return -2
    return -2;
}

// pub::Zone::GetRadius — retail 0x1ba60 (.text:06cfba60 .. 06cfba7f)
// get_zone(id); null -> FLD [.rdata:06d65188] = 0.0f; else tail-JMP [vtable+0x30]
// (Common LAB_06339100 = ZONE::GetRadius, the cached per-shape radius).
SERVER_API float __cdecl GetRadius(unsigned int zone_id) {
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr)
        return zone->GetRadius();
    return 0.0f;
}

// pub::Zone::GetSize — retail 0x1ba80
// get_zone(id) → vtable+0x28 → copy Vector (3 dwords)
SERVER_API int __cdecl GetSize(unsigned int zone_id, Vector& out_size) {
    // Line 21523: get_zone(param_1)
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        // Lines 21525-21528: vtable+0x28 → copy 3 dwords to *param_2
        out_size = zone->Size;
        return 0;
    }
    // Line 21531: return -2
    return -2;
}

// pub::Zone::GetShape — retail 0x1bad0
// get_zone(id) → vtable+0x18 → ZoneShape enum
SERVER_API int __cdecl GetShape(unsigned int zone_id, ZoneShape& out_shape) {
    // Line 21545: get_zone(param_1)
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        // Line 21547: vtable+0x18 → ZoneShape
        out_shape = (ZoneShape)zone->Shape;
        return 0;
    }
    // Lines 21551-21552: *param_2 = 0, return -2
    out_shape = (ZoneShape)0;
    return -2;
}

// pub::Zone::ClassifyPoint — retail 0x1bb10
// get_zone(id) → vtable+0x48(point) → float
SERVER_API float __cdecl ClassifyPoint(unsigned int zone_id, const Vector& point) {
    // Line 21566: get_zone(param_1)
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        // .text:06cfbb22-06cfbb2e: PUSH point; CALL [vtable+0x48] (Common LAB_0633a6e0,
        // ZONE::ClassifyPoint); the result is returned as it comes.
        return zone->ClassifyPoint(point);
    }
    // Line 21571: return 1.0 (outside)
    return 1.0f;
}

// pub::Zone::InZone — retail 0x1bb40
// get_zone(id) → vtable+0x34(point, float) → bool
SERVER_API bool __cdecl InZone(unsigned int zone_id, const Vector& point, float margin) {
    // Line 21585: get_zone(param_1)
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        // .text:06cfbb52-06cfbb63: PUSH margin; PUSH point; CALL [vtable+0x34] (Common
        // LAB_06339220, ZONE::InZone).
        return zone->InZone(point, margin);
    }
    // Line 21590: return false
    return false;
}

// pub::Zone::GetDistance — retail 0x1bb70
// get_zone(id) → vtable+0x3c(point) → float
SERVER_API float __cdecl GetDistance(unsigned int zone_id, const Vector& point) {
    // Line 21604: get_zone(param_1)
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        // .text:06cfbb8b-06cfbb98: PUSH point; CALL [vtable+0x3c] (Common LAB_06339b00,
        // ZONE::GetDistance) -- a signed distance, negative inside; retail does not clamp it.
        return zone->GetDistance(point);
    }
    // Line 21609: return 0.0
    return 0.0f;
}

// pub::Zone::Intersect — retail 0x1bba0
// get_zone(id) → vtable+0x4c(start, end, hit_point) → bool
SERVER_API bool __cdecl Intersect(unsigned int zone_id, const Vector& start, const Vector& end, Vector& hit_point) {
    // Line 21624: get_zone(param_1)
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        // .text:06cfbbb2-06cfbbc8: PUSH hit; PUSH end; PUSH start; CALL [vtable+0x4c]
        // (Common LAB_0633aa40, ZONE::Intersect), then TEST AL / JZ -> 0, else AL = 1.
        return zone->Intersect(start, end, hit_point) ? true : false;
    }
    // Line 21631: return false
    return false;
}

// pub::Zone::GetProperties — retail 0x1bbe0
// get_zone(id) → vtable+0x1c → ulong flags
SERVER_API int __cdecl GetProperties(unsigned int zone_id, unsigned long& out_props) {
    // Retail 0x06cfbbe0 has no guard on the out-param at all; an earlier port carried
    // `if (!out_props) return -2;`, which -- out_props being a REFERENCE -- tested the
    // caller's current *value* and bailed out whenever it happened to be 0. Removed.
    // Line 21645: get_zone(param_1)
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        // Line 21647: vtable+0x1c → PropertyFlags
        out_props = (unsigned long)zone->PropertyFlags;
        return 0;
    }
    // Line 21651: return -2
    return -2;
}

// pub::Zone::GetLootableInfo — retail 0x1bc10
// get_zone(id) → vtable+0x7c → loot info struct
SERVER_API int __cdecl GetLootableInfo(unsigned int zone_id, ID_String& out_archetype, ID_String& out_loot_table,
                                        int& out_min_level, int& out_max_level, int& field6) {
    // Retail 0x06cfbc20/0x06cfbc28 call COMMON.DLL::ID_String::clear on both string
    // out-params (not memset), then zero the three int out-params.
    out_archetype.clear();
    out_loot_table.clear();
    field6 = 0;
    out_max_level = 0;
    out_min_level = 0;
    // Retail 0x06cfbc4e: get_zone(param_1)
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        // Retail 0x06cfbc5f: CALL [vtable+0x7c] (ZONE::GetLootableInfo, Common .text:06262dd0,
        // ICF-folded with File::tell, `MOV EAX,[this+0x84]; RET`): the owned loot record the
        // +0x8c setter (ZONE::Slot8C, FUN_0633aea0) installs -- IZone::OwnedLootEntry, a
        // CmnAsteroid::LootableZoneEntry (common_casteroidfield.cpp "[LootableZone]"). When
        // non-null (.text:06cfbc64): out_archetype <- [rec+0x08], out_loot_table <-
        // [rec+0x0c], out_min_level <- [rec+0x20], out_max_level <- [rec+0x10], field6 <-
        // [rec+0x14] (.text:06cfbc66 .. 06cfbc82). A null record falls through to the same
        // `return 0`.
        CmnAsteroid::LootableZoneEntry const* const Loot = zone->GetLootableInfo();
        if (Loot != nullptr) {
            out_archetype = Loot->AsteroidLootCommodityId;   // +0x08
            out_loot_table = Loot->DynamicLootCommodityId;   // +0x0c
            out_min_level = Loot->AsteroidLootDifficulty;    // +0x20
            out_max_level = Loot->AsteroidLootCountMin;      // +0x10
            field6 = Loot->AsteroidLootCountMax;             // +0x14
        }
        return 0;
    }
    // Line 21686: return -2
    return -2;
}

// pub::Zone::GetName — retail 0x1bca0
// get_zone(id) → vtable+0x84 → name id
SERVER_API int __cdecl GetName(unsigned int zone_id, unsigned int& out_name_id) {
    // Line 21700: get_zone(param_1)
    Universe::IZone const* zone = Universe::get_zone(zone_id);
    if (zone != nullptr) {
        // Line 21702: vtable+0x84 → IdsName
        out_name_id = (unsigned int)zone->IdsName;
        return 0;
    }
    // Line 21706: return -2
    return -2;
}

} // namespace Zone

// ===========================================================================
// pub::System exports — line-by-line verified against Server.dll.c
// ===========================================================================
namespace System {

// ===========================================================================
// FUN_06d07e20 — System activation / object spawning
// Retail: StarSys.cpp — activates a star system, loads objects, notifies clients
//
// When param_1 == 0 (from LoadSystem): only initializes the system if bLoaded == 0
// When param_1 != 0 (from player connect): also sends all objects to that player
// ===========================================================================

// FSystemListNode moved to server_sysdb.h (2026-09-17, wave 8) so that
// StarSystem::remove_player and StarSystem::unload in server_classes.cpp can
// walk the same nodes retail's .text:06d08790 / .text:06d072e4 loops do.

// (FSystemObjectEntry and StarSystem_LoadObjects, the port's rewrite of the
// populate half of FUN_06d06ed0, were removed 2026-09-24: StarSystem::load now
// calls the retail functions, StarSystem::load_solars (FUN_06d07170) and
// StarSystem::load_object (FUN_06d09d60), in src/Server/server_classes.cpp.)

// ---------------------------------------------------------------------------
// pub::System::LoadSystem — retail .text:06cface0, RVA 0x1ace0, ordinal 327,
// ?LoadSystem@System@pub@@YAHABI@Z. Eight instructions, and all of them:
//   .text:06cface8  83ceff        OR   ESI,0xffffffff      ; the failure value
//   .text:06cfaceb  CALL FUN_06d00280                      ; SysDB lookup
//   .text:06cfacf3  TEST EAX,EAX / JZ  -> .text:06cfad04 `MOV EAX,ESI` = -1
//   .text:06cfacf7  6a00          PUSH 0x0
//   .text:06cfacfb  CALL FUN_06d07e20                      ; add_player(0)
//   .text:06cfad00  33c0          XOR  EAX,EAX             ; 0
//
// CORRECTED 2026-09-17 (wave 12). The port had three things wrong here, all of
// them checklist class 6/7 (an invented condition, an invented constant, and a
// guard retail does not have):
//   * there is NO `(system_id & 0x3fffffff)` test and therefore no -4 exit --
//     the mask guard lives inside add_player, not here;
//   * the miss returns -1, not -2;
//   * and, the one that mattered, it wrote `bLoaded = true` instead of calling
//     add_player. That marked the system loaded WITHOUT loading it, so the
//     StarSystem::load call this wave added to add_player would have been
//     skipped for every system LoadSystem had already touched.
// add_player(0) is exactly right: it loads the system and then falls out on its
// own `TEST EBP,0x3fffffff` because the session id is zero.
// ---------------------------------------------------------------------------
SERVER_API int __cdecl LoadSystem(const unsigned int& system_id) {
    FStarSystem* sys = SysDB_LookupSystem(system_id);
    if (!sys) {
        return -1;
    }
    sys->add_player(0);
    return 0;
}

// pub::System::Find -- retail .text:06cfb260 (RVA 0x1b260, ordinal 124).
//   .text:06cfb267..06cfb281  -4 when name is null, name[0] == 0, or
//                             (system_id & 0x3fffffff) == 0
//   .text:06cfb289            FUN_06d00280 (SysDB lookup); miss -> -2
//   .text:06cfb29d            key = CreateID(name)
//   .text:06cfb2b1            StarSystem::find_solar(&key); miss -> -3
//   .text:06cfb2c0            out_id = found->get_id() (IObjInspect +0x20), 0
// out_id is written only on success. (Corrected 2026-09-24: the port returned
// CreateID(name) without looking the solar up, never returned -3, accepted an
// empty name and zeroed out_id up front.)
SERVER_API int __cdecl Find(const unsigned int& system_id, const char* name, unsigned int& out_id) {
    if (name == nullptr || name[0] == '\0') return -4;
    if ((system_id & 0x3fffffff) == 0) return -4;
    FStarSystem* sys = SysDB_LookupSystem(system_id);
    if (!sys) return -2;
    unsigned int Key = CreateID(name);
    GameObject* Solar = sys->find_solar(&Key);
    if (Solar == nullptr) return -3;
    out_id = Solar->get_id();
    return 0;
}

// pub::System::GetName (0x1b8e0)
// Retail: Universe::get_system(id), vtable+0xc → StridName
SERVER_API int __cdecl GetName(unsigned int system_id, unsigned int& out_name_id) {
    out_name_id = 0;
    Universe::ISystem const* sys = Universe::get_system(system_id);
    if (!sys) return -2;
    out_name_id = sys->StridName;
    return 0;
}

// pub::System::GetNestedProperties -- retail .text:06cfb880 .. .text:06cfb8dc (RVA 0x1b880).
// Ported 1:1 2026-09-24 (claude-code:w6-sys):
//   06cfb88b .. 06cfb89b  sys = Universe::get_system(system_id); null -> -2
//   06cfb89d .. 06cfb8be  a 16-byte stack sphere {position, radius 0}
//   06cfb8c6 .. 06cfb8cd  out_props = ISystem slot 9 (vt+0x24)(&sphere); return 0
SERVER_API int __cdecl GetNestedProperties(const unsigned int& system_id, const Vector& position, unsigned long& out_props) {
    Universe::ISystem const* sys = Universe::get_system(system_id);
    if (!sys) return -2;
    Geometry::Sphere Query;
    Query.Center = position;
    Query.Radius = 0.0f;
    out_props = sys->GetNestedProperties(&Query);
    return 0;
}

// pub::System::EnumerateConnections -- retail .text:06cfb170 .. .text:06cfb255 (RVA 0x1b170).
// Ported 1:1 2026-09-24 (claude-code:w6-sys):
//   06cfb17c .. 06cfb18e  sys = Universe::get_system(system_id); null -> -2
//   06cfb199 .. 06cfb1a4  count = ISystem slot 13 (vt+0x34); signed JLE 0 -> 0
//   06cfb1b3 .. 06cfb1c7  per index: ID_String::clear on record +4, slot 14 (vt+0x38)
//                         (index, &record); its (always false) result is not read
//   06cfb1ca .. 06cfb1e3  type 0 takes every record, 1 only GateType 1, 2 only GateType 2,
//                         any other type none
//   06cfb1e5 .. 06cfb231  append to the caller's struct at [Count], no bound check
//   06cfb237 .. 06cfb23e  signed loop; return 0
SERVER_API int __cdecl EnumerateConnections(const unsigned int& system_id, ConnectionEnumerator& enumerator, ConnectionType connection_type) {
    Universe::ISystem const* sys = Universe::get_system(system_id);
    if (!sys) return -2;

    int const Count = sys->GetConnectionCount();
    for (int Index = 0; Index < Count; ++Index) {
        Universe::FSystemConnection Record;
        Record.Object.clear();
        sys->GetConnection(static_cast<unsigned int>(Index), &Record);
        if (connection_type == 1) {
            if (Record.GateType != 1) continue;
        } else if (connection_type == 2) {
            if (Record.GateType != 2) continue;
        } else if (connection_type != 0) {
            continue;
        }
        unsigned int const Slot = enumerator.Count;
        enumerator.GotoSystemId[Slot] = Record.GotoSystemId;
        enumerator.Object[Slot] = Record.Object;
        enumerator.ObjectId[Slot] = Record.ObjectId;
        enumerator.Pos[Slot] = Record.Pos;
        ++enumerator.Count;
    }
    return 0;
}

// pub::System::EnumerateObjects -- retail .text:06cfb2e0 .. .text:06cfb63e (RVA 0x1b2e0).
// Ported 1:1 2026-09-24 (claude-code:w6-sys):
//   06cfb30d .. 06cfb338  result = -2; sys = Universe::get_system(system_id); an INI_Reader
//                         is constructed and destroyed but never opened; null -> -2
//   06cfb342 .. 06cfb355  result = 0; count = ISystem slot 15 (vt+0x3c); signed JLE 0
//   06cfb362 .. 06cfb3ad  per index: clear the four TString fields of the stack record, then
//                         slot 16 (vt+0x40)(index, &record); its result is not read
//   06cfb3b0 .. 06cfb43f  build the 0x64-byte FSysObj (two strncpy of 0x20, no terminator
//                         forced), SystemId = *system_id
//   06cfb443 .. 06cfb44a  enumerator slot 0 (&object); false -> stop
//   06cfb44c .. 06cfb45b  signed loop; return result
SERVER_API int __cdecl EnumerateObjects(const unsigned int& system_id, SysObjEnumerator& enumerator) {
    int Result = -2;
    Universe::ISystem const* sys = Universe::get_system(system_id);
    INI_Reader Reader;
    if (!sys) return Result;

    Result = 0;
    int const Count = sys->GetObjectCount();
    for (int Index = 0; Index < Count; ++Index) {
        Universe::FSystemObject Record;
        Record.Nickname.Length = 0;
        Record.Nickname.Data[0] = '\0';
        Record.Reputation.Length = 0;
        Record.Reputation.Data[0] = '\0';
        Record.NextRing.Length = 0;
        Record.NextRing.Data[0] = '\0';
        Record.PrevRing.Length = 0;
        Record.PrevRing.Data[0] = '\0';
        sys->GetSystemObject((unsigned int)Index, &Record);

        FSysObj Object;
        strncpy(Object.Nickname, Record.Nickname.Data, 0x20);
        Object.Pos = Record.Pos;
        Object.ArchetypeId = Record.ArchetypeId;
        Object.IdsName = Record.IdsName;
        Object.IdsInfo = Record.IdsInfo;
        strncpy(Object.Reputation, Record.Reputation.Data, 0x20);
        Object.DockWith = Record.DockWith;
        Object.Goto = Record.Goto;
        Object.SystemId = system_id;
        if (!enumerator.Slot00(&Object)) break;
    }
    return Result;
}

// pub::System::EnumerateZones -- retail .text:06cfad60 .. .text:06cfaf6x (RVA 0x1ad60).
// Ported 1:1 2026-09-24 (claude-code:w6-sys):
//   06cfad8d .. 06cfadb8  result = -2; sys = Universe::get_system(system_id); INI_Reader
//                         constructed; null -> -2
//   06cfadbe .. 06cfade9  Universe::get_filename(path, ISystem slot 5 (vt+0x14) = file);
//                         reader.open(path, false); failure -> -2
//   06cfadeb .. 06cfae3c  for every entry of ISystem slot 6 (vt+0x18, the zone list):
//                         reader.set_state(entry state); id = zone slot 1 (the zone id);
//                         enumerator slot 0 (&id, &reader)
//   06cfae3e              result = 0
SERVER_API int __cdecl EnumerateZones(const unsigned int& system_id, ZoneEnumerator& enumerator) {
    int Result = -2;
    Universe::ISystem const* sys = Universe::get_system(system_id);
    INI_Reader Reader;
    if (!sys) return Result;

    char Path[0x104];
    Universe::get_filename(Path, sys->GetFile());
    if (!Reader.open(Path, false)) return Result;

    std::list<Universe::FSystemZone> const* Zones = sys->GetZones();
    std::list<Universe::FSystemZone>::const_iterator It = Zones->begin();
    std::list<Universe::FSystemZone>::const_iterator End = Zones->end();
    for (; It != End; ++It) {
        Reader.set_state(It->State);
        unsigned int ZoneId = It->Zone->GetId();      // zone vt+0x04
        enumerator.Slot00(&ZoneId, &Reader);
    }
    Result = 0;
    return Result;
}

// pub::System::ScanObjects -- retail .text:06cfad10 .. .text:06cfad5f (ordinal 0x1ad10, cdecl).
// ?ScanObjects@System@pub@@YAHABIQAIIABVVector@@MIAAI@Z -- QAI: the array parameter is
// `unsigned int* const`; MSVC mangles that top-level const.
// Ported 1:1 2026-09-24 (claude-code:w5-server-06cf):
//   06cfad15 .. 06cfad19  *out_count = 0
//   06cfad1f .. 06cfad32  FUN_06d00280(*system_id) (SysDB_LookupSystem, no id mask test);
//                         null -> -2
//   06cfad34 .. 06cfad4f  sys->FUN_06d0bcd0(buffer, param_3, &position, radius, param_6)
//                         (StarSystem::scan_objects). The third parameter is the buffer
//                         capacity and the sixth the type mask -- the port had the two
//                         names swapped.
//   06cfad54 .. 06cfad57  *out_count = the count; return 0
SERVER_API int __cdecl ScanObjects(const unsigned int& system_id, unsigned int* const buffer,
                                    unsigned int max_results, const Vector& position, float radius,
                                    unsigned int type_mask, unsigned int& out_count) {
    out_count = 0;                                                       // .text:06cfad19
    StarSystem* const System = SysDB_LookupSystem(system_id);            // .text:06cfad28
    if (System == nullptr) return -2;                                    // .text:06cfad32
    out_count = System->scan_objects(buffer, max_results, &position, radius, type_mask); // .text:06cfad4f
    return 0;                                                            // .text:06cfad57
}

// pub::System::InZones -- Vector overload, retail .text:06cfb640 .. .text:06cfb75b (RVA 0x1b640).
// ?InZones@System@pub@@YAHIABVVector@@MQAIIAAI@Z -- array parameter `unsigned int* const` (QAI).
// Ported 1:1 2026-09-24 (claude-code:w6-sys):
//   06cfb64a .. 06cfb66e  out_count = 0; radius < 0.0f (DAT_06d65188) -> -4
//   06cfb674 .. 06cfb68e  sys = Universe::get_system(system_id); null -> -2
//   06cfb694 .. 06cfb6da  tree = ISystem slot 7 (vt+0x1c); sphere {position, radius};
//                         tree vt+0x1c (&sphere, &{0x800, 0, items})
//   06cfb6dd .. 06cfb743  for index < hit count while out_count < max_zones (tested first):
//                         !SphereCull(sphere, zone) -> out_zones[out_count++] = zone id
//   06cfb745              return 0
SERVER_API int __cdecl InZones(unsigned int system_id, const Vector& position, float radius,
                                       unsigned int* const out_zones, unsigned int max_zones,
                                       unsigned int& out_count) {
    out_count = 0;
    if (radius < 0.0f) return -4;
    Universe::ISystem const* sys = Universe::get_system(system_id);
    if (!sys) return -2;

    ISpatialPartition* Tree = sys->GetZoneTree();
    Geometry::Sphere Query;
    Query.Center = position;
    Query.Radius = radius;
    FSpatialQueryResult Hits;
    Hits.Capacity = 0x800;
    Hits.Count = 0;
    SpatialFill(Tree, &Query, &Hits);

    for (unsigned int Index = 0; Index < Hits.Count; ++Index) {
        if (out_count >= max_zones) break;
        Universe::IZone const* Zone = static_cast<Universe::IZone const*>(Hits.Items[Index]);
        if (SphereCull(Query, Zone)) continue;
        out_zones[out_count] = Zone->GetId();       // zone vt+0x04
        ++out_count;
    }
    return 0;
}

// pub::System::InZones -- Transform overload, retail .text:06cfb760 .. .text:06cfb870
// (RVA 0x1b760). ?InZones@System@pub@@YAHIABVTransform@@MMMQAIIAAI@Z.
// The three floats are the frustum's AngleX / AngleY / Distance (.text:06cfb7b8-06cfb7d7
// store them at frustum +0x30 / +0x34 / +0x38); the port used to call them radius_x/y/z.
// Ported 1:1 2026-09-24 (claude-code:w6-sys):
//   06cfb77b .. 06cfb791  out_count = 0; sys = Universe::get_system(system_id); null -> -2
//                         (no radius test in this overload)
//   06cfb797 .. 06cfb7f0  tree = ISystem slot 7; frustum {transform (0x30 bytes), angle_x,
//                         angle_y, distance}; tree vt+0x18 (&frustum, &{0x800, 0, items})
//   06cfb7f3 .. 06cfb858  as the Vector overload, with FrustumCull
//   06cfb85a              return 0
SERVER_API int __cdecl InZones(unsigned int system_id, const Transform& transform,
                                          float angle_x, float angle_y, float distance,
                                          unsigned int* const out_zones, unsigned int max_zones,
                                          unsigned int& out_count) {
    out_count = 0;
    Universe::ISystem const* sys = Universe::get_system(system_id);
    if (!sys) return -2;

    ISpatialPartition* Tree = sys->GetZoneTree();
    Geometry::Frustum Query;
    Query.Xform = transform;
    Query.AngleX = angle_x;
    Query.AngleY = angle_y;
    Query.Distance = distance;
    FSpatialQueryResult Hits;
    Hits.Capacity = 0x800;
    Hits.Count = 0;
    SpatialFillFrustum(Tree, &Query, &Hits);

    for (unsigned int Index = 0; Index < Hits.Count; ++Index) {
        if (out_count >= max_zones) break;
        Universe::IZone const* Zone = static_cast<Universe::IZone const*>(Hits.Items[Index]);
        if (FrustumCull(Query, Zone)) continue;
        out_zones[out_count] = Zone->GetId();       // zone vt+0x04
        ++out_count;
    }
    return 0;
}

} // namespace System

// The pub::pub_GetSystemID / pub_GetSystemGateConnection / pub_GetSystemNickname /
// pub_BuildSystemReader / pub_BuildBaseReader / pub_GetBaseID / pub_GetBaseNickname /
// pub_GetSystemFromBase block that stood here was a second, pointer-taking copy of the
// real exports (pub::GetSystemID etc., declared in common_server.h with retail's
// reference signatures). Nothing called them and retail exports none of those names.

// ===========================================================================
// pub::AI exports
// ===========================================================================
namespace AI {

// ---------------------------------------------------------------------------
// OP_RTYPE values. Retail exports no enumerator names for OP_RTYPE; the names in
// common_pub.h are the port's, derived from the return sites below (1 = accepted, which
// the header once called OpRTypeFailed). Every pub::AI function below returns the literal
// retail value, cited at its instruction, instead of an enumerator:
//   0  SubmitState: personality applied / zone cleared
//   1  SubmitDirective: accepted (sent to the player's client, or set on the AI)
//   2  SubmitDirective: the AI already has a current directive
//   4  bad id (both), and every SubmitState zone / flag / unknown-op exit
//   5  op failed validate(), no object, not a CSimple, or no behavior for this op
// (IBehaviorManager::set_directive_priority in Common adds 1 and 3.)
// ---------------------------------------------------------------------------

// IDirectiveInfo is three dwords no module names (common_iface.h keeps it as bytes).
// SubmitDirective builds it at S+0x10: word 0 = kind (0 cancel, 1 behavior, 2 player
// maneuver), word 1 = BaseOp::Priority, word 2 = the behavior id (-1 until a branch sets
// it). Common's own builders use the same three words (e.g. common_launchaction.cpp).
static IDirectiveInfo Server_MakeDirectiveInfo(unsigned int Kind, unsigned int OpField08,
                                               unsigned int BehaviorId) {
    IDirectiveInfo Dir;
    unsigned int const Words[3] = { Kind, OpField08, BehaviorId };
    std::memcpy(Dir.Data, Words, sizeof(Words));
    return Dir;
}

// pub::AI::SubmitState — retail 0x7c8d0, .text:06d5c8d0..06d5cba5, all 207 instructions
// traced from the PE bytes (frame: SUB ESP,0xa0 + three pushes).
//
//   06d5c8da  (Flid & 0x3fffffff) == 0            -> return 4
//   06d5c8e5  FUN_06d00670(Flid) == null          -> return 4
//   06d5c8f2  bm = GetBehaviorManager(obj)        (no null test: retail would fault)
//   06d5c901  switch (Op->OpType)                 other -> return 4
//     0 SetPersonalityParams  06d5cb57..06d5cba5:
//         bm->set_state_graph(Op+0x0c, false)                (PUSH 0 / PUSH [ESI+0xc])
//         Op+0x14 != -1 -> bm->set_directive_callback(Op+0x14)
//         Op+0x10 != -1 -> bm->set_content_callback(Op+0x10)
//         byte Op+0x18   -> bm->set_personality(Op+0x1c)
//         return 0
//     1 SetZoneBehaviorParams 06d5c957..06d5cb56: builds a PathfindManager::UserZone at
//         S+0x54 and hands it to the PathfindManager at [bm+0xc] (fields: common_pathfind.h).
//     2 SetFlagParams         06d5c916..06d5c956:
//         FlagId 0 -> byte [[bm+0xc] + 0x245] = bValue; FlagId 1 -> byte [[bm+8] + 0x11c0]
//         = bValue; anything else untouched. return 4 on all three.
SERVER_API OP_RTYPE __cdecl SubmitState(unsigned int Flid, BaseOp* Op) {
    if ((Flid & 0x3fffffff) == 0) return static_cast<OP_RTYPE>(4);
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return static_cast<OP_RTYPE>(4);
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);

    if (Op->OpType == OpType_SetPersonalityParams) {
        SetPersonalityParams* Params = static_cast<SetPersonalityParams*>(Op);
        Behavior->set_state_graph(Params->StateGraphId, false);
        // .text:06d5cb65 / 06d5cb76: the two -1 sentinels ("leave the callback as it is").
        if (Params->DirectiveCallbackObject != (DirectiveCallback*)-1) {
            Behavior->set_directive_callback(Params->DirectiveCallbackObject);
        }
        if (Params->ContentCallbackObject != (ContentCallback*)-1) {
            Behavior->set_content_callback(Params->ContentCallbackObject);
        }
        if (Params->bSetPersonality != 0) {
            Behavior->set_personality(Params->Value);
        }
        return static_cast<OP_RTYPE>(0);
    }

    if (Op->OpType == OpType_SetZoneBehaviorParams) {
        SetZoneBehaviorParams* Params = static_cast<SetZoneBehaviorParams*>(Op);
        // .text:06d5c957 MOV EBP,[EDI+0xc]: IBehaviorManager+0x0c is the PathfindManager*
        // (every call below is a PathfindManager export with ECX = EBP).
        PathfindManager* Pathfind = Behavior->Pathfind;

        // 06d5c95e FUN_06d0a850(S+0x6c) + 06d5c969..06d5c97f: UserZone's inline ctor --
        // BoxRotation = identity, BoxCenter = 0 (common_pathfind.h).
        PathfindManager::UserZone Zone;
        // Retail writes only the fields each zone kind uses and submits the rest as stack
        // residue (+0x08/+0x14/+0x54 for a box, +0x08 for kind 2, +0x00 for an unknown
        // kind, +0x04 for an unknown mode). Residue is not reproducible; the port zeroes
        // those fields first.
        Zone.Mode = 0;
        Zone.Invert = 0;
        Zone.TargetPoint.x = 0.0f;
        Zone.TargetPoint.y = 0.0f;
        Zone.TargetPoint.z = 0.0f;
        Zone.Radius = 0.0f;
        Zone.BoxHalfExtent.x = 0.0f;
        Zone.BoxHalfExtent.y = 0.0f;
        Zone.BoxHalfExtent.z = 0.0f;
        Zone.TargetKey = 0;

        // 06d5c963..06d5c9b9: mode.
        if (Params->InvertMode == 0) {
            Zone.Invert = 0;                                    // 06d5c9b1
        } else if (Params->InvertMode == 1) {
            Zone.Invert = 1;                                    // 06d5c9ab (ECX = 1)
        } else if (Params->InvertMode == 2) {
            Pathfind->clear_user_zone();                        // 06d5c999
            return static_cast<OP_RTYPE>(0);
        }

        // BoxMin = Params+0x24..+0x2c, BoxMax = Params+0x30..+0x38.
        if (Params->Mode == 0) {
            // 06d5cb15..06d5cb37
            Zone.Mode = 0;
            Zone.TargetPoint.x = Params->TargetPoint.x;
            Zone.TargetPoint.y = Params->TargetPoint.y;
            Zone.TargetPoint.z = Params->TargetPoint.z;
            Zone.Radius = Params->Radius;
        } else if (Params->Mode == 1) {
            // 06d5ca04..06d5cafe. Center = (max + min) * 0.5 (DAT_06d6b9cc == 0.5f), each
            // component stored to a float before use; extents = |min - center| per axis. The
            // Matrix at +0x18 is re-copied from a second identity built at S+0x24 (REP MOVSD
            // of 12 dwords, 06d5cad4) -- the same values.
            Zone.Mode = 1;
            float const CenterX = (Params->BoxMax.x + Params->BoxMin.x) * 0.5f;
            float const CenterY = (Params->BoxMax.y + Params->BoxMin.y) * 0.5f;
            float const CenterZ = (Params->BoxMax.z + Params->BoxMin.z) * 0.5f;
            Zone.BoxCenter.x = CenterX;
            Zone.BoxCenter.y = CenterY;
            Zone.BoxCenter.z = CenterZ;
            Zone.BoxHalfExtent.x = std::fabs(Params->BoxMin.x - CenterX);
            Zone.BoxHalfExtent.y = std::fabs(Params->BoxMin.y - CenterY);
            Zone.BoxHalfExtent.z = std::fabs(Params->BoxMin.z - CenterZ);
        } else if (Params->Mode == 2) {
            // 06d5c9cf..06d5c9ef
            Zone.Mode = 2;
            Zone.Radius = Params->Radius;
            Zone.TargetKey = Params->TargetId;
        }
        // 06d5c9ef / 06d5cb00 / 06d5cb42: every kind, known or not, is submitted.
        Pathfind->submit_user_zone(Zone);
        return static_cast<OP_RTYPE>(4);
    }

    if (Op->OpType == OpType_SetFlagParams) {
        SetFlagParams* Params = static_cast<SetFlagParams*>(Op);
        if (Params->FlagId == 0) {
            // 06d5c93c: byte PathfindManager+0x245 (common_pathfind.h Flag245).
            Behavior->Pathfind->Flag245 = Params->bValue;
        } else if (Params->FlagId == 1) {
            // 06d5c921: byte SystemManager+0x11c0 == GunGroups (+0x12c) + 0x1094, the
            // "fire control active" byte common_aifirecontrol.cpp tests.
            Behavior->Sys->GunGroups.bEnabled = Params->bValue;
        }
        return static_cast<OP_RTYPE>(4);
    }

    return static_cast<OP_RTYPE>(4);
}

// OpType -> behavior id pairs of SubmitDirective's AI branch, in retail's test order
// (.text:06d5d48b..06d5d6d2: thirteen independent `CMP [ESI+4],n / JNE` blocks, each
// setting IDirectiveInfo word 0 = 1 and word 2 = the id before set_current_directive).
struct FSubmitDirectiveBehavior {
    int OpType;
    unsigned int BehaviorId;
};
static const FSubmitDirectiveBehavior GSubmitDirectiveBehaviors[] = {
    { OpType_DirectiveGotoOp,              0x02 },   // 06d5d48b
    { OpType_DirectiveFollowOp,            0x11 },   // 06d5d4c9
    { OpType_DirectiveTrailOp,             0x03 },   // 06d5d4fa (EBP == 3)
    { OpType_DirectiveDockOp,              0x07 },   // 06d5d522
    { OpType_DirectiveDelayOp,             0x14 },   // 06d5d54e
    { OpType_DirectiveIdleOp,              0x06 },   // 06d5d57a
    { OpType_DirectiveLaunchOp,            0x08 },   // 06d5d5a6
    { OpType_DirectiveInstantTradelaneOp,  0x09 },   // 06d5d5d2
    { OpType_DirectiveGuideOp,             0x0e },   // 06d5d5fe
    { OpType_DirectiveFaceOp,              0x0f },   // 06d5d62a
    { OpType_DirectiveEvadeOp,             0x05 },   // 06d5d656
    { OpType_DirectiveDrasticEvadeOp,      0x12 },   // 06d5d682
    { OpType_DirectiveTractorBeamOp,       0x10 },   // 06d5d6ae
};

// pub::AI::SubmitDirective — retail 0x7cbb0, .text:06d5cbb0..06d5d764, all 702 instructions
// traced from the PE bytes, ESP tracked through every PUSH and every callee-cleaned
// FUN_06ce7c10 (RET 0xc). Frame: SUB ESP,0xa8 + four pushes; the SETDIRECTIVE struct is
// S+0x24..S+0xb7, the owner player id S+0x20, the IDirectiveInfo S+0x10..S+0x1b, the result
// S+0x1c. Jump tables at .text:06d5d768 (7 dwords) and .text:06d5d784 (19 bytes, indexed by
// OpType - 3) read from the PE.
//
//   06d5cbc1  (Flid & 0x3fffffff) == 0             -> return 4
//   06d5cbd8  !Op->validate()                      -> return 5    (vtable slot 0)
//   06d5cbef  FUN_06d00670(Flid) == null           -> return 5
//   06d5cc13  obj->cobject() (IObjInspect +0x150) null, not (Class & 3) == 3, or
//             CSimple::ObjectDb (+0xac) null       -> return 5
//   06d5cc40  owner = CSimple(obj+0x10)->OwnerPlayer (+0xb4)
//   06d5cc59..06d5cca0  the inlined PlayerDB::is_valid_ship_owner(owner, Flid): a live player
//             slot whose ship (+0x3e4) is this object. Then the directive goes to that
//             player's client, not to an AI -- the player branch below.
//   06d5d43b  otherwise the AI branch.
SERVER_API OP_RTYPE __cdecl SubmitDirective(unsigned int Flid, BaseOp* Op) {
    if ((Flid & 0x3fffffff) == 0) return static_cast<OP_RTYPE>(4);
    if (!Op->validate()) return static_cast<OP_RTYPE>(5);

    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return static_cast<OP_RTYPE>(5);

    CObject const* Inspected = Obj->cobject();
    if (Inspected == nullptr) return static_cast<OP_RTYPE>(5);
    if ((static_cast<unsigned int>(Inspected->ObjectClass) & 3u) != 3u) return static_cast<OP_RTYPE>(5);
    if (static_cast<CSimple const*>(Inspected)->ObjectDb == nullptr) return static_cast<OP_RTYPE>(5);

    // 06d5cc40..06d5cc53: CBase::Object with the (Class & 3) == 3 guard, no null test on
    // the result (retail would fault reading +0xb4 of null).
    unsigned int OwnerPlayer = Obj->csimple()->OwnerPlayer;

    if (::Players.is_valid_ship_owner(OwnerPlayer, Flid)) {
        // Player branch, 06d5cca6..06d5d43a. Each of the six supported kinds default-
        // constructs the struct inline (+0x18 = -1, +0x40 = 3, the rest 0; the three
        // FUN_06ce7c10(0,0,0) calls are the 3-dword blocks at +0x60/+0x6c/+0x78), fills its
        // own fields, stores the kind at +0x00 and calls IClient slot 100 (+0x190) with
        // (&OwnerPlayer, &Info); every such send returns 1. The kinds other than goto also
        // zero +0x84..+0x90 right before the send (06d5d1dc..06d5d20e, inline for cancel at
        // 06d5cd94..06d5cdc6) -- already zero here. Any other op returns 5 with no send.
        // Pad_0x09 / Pad_0x91 are left as stack residue by retail; zero here (rpclocal's
        // marshal zeroes them again).
        FPayloadServerSetDirective Info = {};
        Info.Field_0x18 = -1;
        Info.Field_0x40 = 3;

        if (Op->OpType == OpType_DirectiveCancelOp) {
            // caseD_15, 06d5ccc3..06d5cdd1
            Info.Field_0x00 = 0;
        } else if (Op->OpType == OpType_DirectiveDelayOp) {
            // caseD_c, 06d5cdd6..06d5ceb6
            DirectiveDelayOp* Delay = static_cast<DirectiveDelayOp*>(Op);
            Info.Field_0x0c = Delay->DelaySeconds;
            Info.Field_0x00 = 1;
        } else if (Op->OpType == OpType_DirectiveDockOp) {
            // caseD_d, 06d5cebb..06d5cfc5
            DirectiveDockOp* Dock = static_cast<DirectiveDockOp*>(Op);
            Info.Field_0x10 = Dock->DockObjectId;
            Info.Field_0x14 = Dock->Field_10;
            Info.Field_0x08 = Dock->Field_14;
            Info.Field_0x18 = Dock->DockPortIndex;
            Info.Field_0x1c = static_cast<unsigned int>(Dock->Field_1C);
            Info.Field_0x20 = Dock->Field_20;
            Info.Field_0x24 = Dock->Field_24;
            Info.Field_0x00 = 2;
        } else if (Op->OpType == OpType_DirectiveFollowOp) {
            // caseD_4, 06d5cfca..06d5d0dd
            DirectiveFollowOp* Follow = static_cast<DirectiveFollowOp*>(Op);
            Info.Field_0x28 = Follow->TargetId;
            Info.Field_0x2c = Follow->FollowDistance;
            Info.Field_0x30.x = Follow->Field_14;
            Info.Field_0x30.y = Follow->Field_18;
            Info.Field_0x30.z = Follow->Field_1C;
            Info.Field_0x3c = Follow->Field_20;
            Info.Field_0x00 = 3;
        } else if (Op->OpType == OpType_DirectiveIdleOp) {
            // caseD_14, 06d5d0e2..06d5d1c5
            Info.Field_0x00 = 5;
        } else if (Op->OpType == OpType_DirectiveGotoOp) {
            // caseD_3, 06d5d22e..06d5d40c. The only kind that fills +0x84..+0x90, and the
            // only one that jumps straight to the send at 06d5d413.
            DirectiveGotoOp* Goto = static_cast<DirectiveGotoOp*>(Op);
            Info.Field_0x40 = Goto->Field_0C;
            Info.Field_0x44 = Goto->Destination;
            Info.Field_0x50 = Goto->Field_1C;
            Info.Block_0x54.Field_0x00 = Goto->Field_20;
            Info.Block_0x54.Field_0x04 = Goto->Field_24;
            Info.Block_0x54.Field_0x08 = Goto->Field_28;
            Info.Block_0x60.Field_0x00 = Goto->Field_2C;
            Info.Block_0x60.Field_0x04 = Goto->Field_30;
            Info.Block_0x60.Field_0x08 = Goto->Field_34;
            Info.Block_0x6c.Field_0x00 = Goto->Field_38;
            Info.Block_0x6c.Field_0x04 = Goto->Field_3C;
            Info.Block_0x6c.Field_0x08 = Goto->Field_40;
            Info.Block_0x78.Field_0x00 = Goto->Field_44;
            Info.Block_0x78.Field_0x04 = Goto->Field_48;
            Info.Block_0x78.Field_0x08 = Goto->Field_4C;
            Info.Field_0x84 = Goto->Field_50;
            Info.Field_0x88 = Goto->Field_54;
            Info.Field_0x8c = Goto->Field_58;
            Info.Field_0x8d = Goto->Field_59;
            Info.Field_0x8e = Goto->Field_5A;
            Info.Field_0x8f = Goto->Field_5B;
            Info.Field_0x1c = Goto->Field_5C;
            Info.Field_0x20 = Goto->Field_60;
            Info.Field_0x24 = Goto->Field_64;
            Info.Field_0x90 = Goto->Field_6C;
            Info.Field_0x00 = 4;
        } else {
            // caseD_5, 06d5d723: byte-table entry 6 (Trail, Strafe, Face, Ram, TractorBeam,
            // Evade, DrasticEvade, Launch, InstantTradelane, Formation, Guide, WaitForShip,
            // WaitForPlayerManeuver) and the Set*Params ops (OpType - 3 > 0x12, `JA`).
            return static_cast<OP_RTYPE>(5);
        }

        g_pClientInterface->SendServerSetDirective(OwnerPlayer, Info);
        return static_cast<OP_RTYPE>(1);
    }

    // AI branch, 06d5d43b..06d5d754. GetBehaviorManager is not null-tested (retail would
    // fault on the first member call).
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);

    if (Op->OpType == OpType_DirectiveWaitForPlayerManeuverOp) {
        // 06d5d709: word 0 = 2, word 2 = BehaviorTypes::get_behavior_id(name); -1 -> 5.
        DirectiveWaitForPlayerManeuverOp* Wait = static_cast<DirectiveWaitForPlayerManeuverOp*>(Op);
        int const BehaviorId = BehaviorTypes::get_behavior_id(Wait->BehaviorName);
        if (BehaviorId == -1) return static_cast<OP_RTYPE>(5);
        Behavior->set_current_directive(
            Server_MakeDirectiveInfo(2u, Op->Priority, static_cast<unsigned int>(BehaviorId)), Op);
        return static_cast<OP_RTYPE>(1);
    }

    if (Op->OpType == OpType_DirectiveCancelOp) {
        // 06d5d6e9: word 0 = EBX = 0, word 2 still -1; the callee's result is ignored.
        Behavior->set_current_directive(Server_MakeDirectiveInfo(0u, Op->Priority, 0xffffffffu), Op);
        return static_cast<OP_RTYPE>(1);
    }

    // 06d5d471: a current directive whose word 0 is non-zero blocks every other op.
    unsigned int CurrentKind = 0;
    std::memcpy(&CurrentKind, Behavior->get_current_directive()->Data, sizeof(CurrentKind));
    if (CurrentKind != 0) return static_cast<OP_RTYPE>(2);

    unsigned int Result = 5;                                    // 06d5d494 [S+0x1c] = 5
    size_t const PairCount = sizeof(GSubmitDirectiveBehaviors) / sizeof(GSubmitDirectiveBehaviors[0]);
    for (size_t Index = 0; Index < PairCount; ++Index) {
        if (Op->OpType != GSubmitDirectiveBehaviors[Index].OpType) continue;
        if (Behavior->set_current_directive(
                Server_MakeDirectiveInfo(1u, Op->Priority, GSubmitDirectiveBehaviors[Index].BehaviorId), Op)) {
            Result = 1;
        }
    }
    return static_cast<OP_RTYPE>(Result);
}

// pub::AI::set_directive_priority — retail 0x7db70, .text:06d5db70..06d5dba8, 20 insns.
// No object or no behavior -> 5; otherwise IBehaviorManager::set_directive_priority's own
// result (06d5dba2). No Flid mask test.
SERVER_API OP_RTYPE __cdecl set_directive_priority(unsigned int Flid, DirectivePriority Priority) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return static_cast<OP_RTYPE>(5);
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return static_cast<OP_RTYPE>(5);
    return Behavior->set_directive_priority(Priority);
}

// pub::AI::get_behavior_id — retail 0x7dbb0, .text:06d5dbb0..06d5dbdd, 17 insns.
// -1 (OR EAX,-1) on no object / no behavior, else IBehaviorManager+0xc0.
SERVER_API int __cdecl get_behavior_id(unsigned int Flid) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return -1;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return -1;
    return Behavior->CurrentBehaviorIndex;
}

// pub::AI::get_state_graph_id — retail 0x7dbe0, .text:06d5dbe0..06d5dc14, 22 insns.
// -1 on no object / no behavior / no state graph (IBehaviorManager+0x04), else the graph's
// first dword, IStateGraph::Id.
SERVER_API int __cdecl get_state_graph_id(unsigned int Flid) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return -1;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return -1;
    IStateGraph const* Graph = Behavior->StateGraph;
    if (Graph == nullptr) return -1;
    return Graph->Id;
}

// pub::AI::get_personality — retail 0x7dc20, .text:06d5dc20..06d5dc51, all 19 insns:
//   PUSH [ESP+4] / CALL FUN_06d00670 (live-object lookup) / TEST EAX,EAX / JZ fail
//   PUSH EAX / CALL [COMMON.DLL::GetBehaviorManager] / TEST EAX,EAX / JZ fail
//   PUSH [ESP+8] / MOV ECX,EAX / CALL [COMMON.DLL::IBehaviorManager::get_personality]
//   MOV AL,1 / RET            fail: XOR AL,AL / RET
// No Flid mask test (the port's was invented).
// ?get_personality@AI@pub@@YA_NIAAVPersonality@12@@Z -- Personality is a class.
SERVER_API bool __cdecl get_personality(unsigned int Flid, Personality& OutPersonality) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return false;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return false;
    Behavior->get_personality(OutPersonality);
    return true;
}

// pub::AI::enable_maneuver — retail 0x7dc60, .text:06d5dc60..06d5dc96, 21 insns.
// IBehaviorManager::enable_maneuver's own bool is dropped (06d5dc91 MOV AL,1): true
// whenever the behavior exists, false otherwise.
SERVER_API bool __cdecl enable_maneuver(unsigned int Flid, int ManeuverId, bool bEnable) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return false;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return false;
    Behavior->enable_maneuver(ManeuverId, bEnable);
    return true;
}

// pub::AI::enable_all_maneuvers — retail 0x7dca0, .text:06d5dca0..06d5dcc9, 15 insns.
// Tail-jumps into IBehaviorManager::enable_all_maneuvers (06d5dcc1), so ITS result is
// returned; false when there is no object / behavior.
SERVER_API bool __cdecl enable_all_maneuvers(unsigned int Flid) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return false;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return false;
    return Behavior->enable_all_maneuvers();
}

// pub::AI::lock_maneuvers — retail 0x7dcd0, .text:06d5dcd0..06d5dd01, 19 insns.
// Same shape as enable_maneuver: the callee's bool is dropped, true on success.
SERVER_API bool __cdecl lock_maneuvers(unsigned int Flid, bool bLock) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return false;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return false;
    Behavior->lock_maneuvers(bLock);
    return true;
}

// pub::AI::get_scan_response — retail 0x7dd10, .text:06d5dd10..06d5ddbc, 67 insns.
// The object looked up is the SECOND argument (06d5dd13 MOV EDI,[ESP+0x14] after three
// pushes); the first goes to IBehaviorManager::get_scan_response and to the client, the
// third only to the client.
//   no object                        -> 0 (the null EAX itself, 06d5dd24)
//   owner = CSimple(obj+0x10)->OwnerPlayer (no null test on the guarded pointer)
//   inlined is_valid_ship_owner(owner, TargetFlid)
//                                    -> IClient::SendServerScanNotify (+0x1b4, FLPACKET_SERVER_SCANNOTIFY)
//                                       (owner, ScannerFlid, GoodId), return 3
//   no behavior                      -> 0
//   otherwise                        -> IBehaviorManager::get_scan_response(ScannerFlid)
SERVER_API ScanResponse __cdecl get_scan_response(unsigned int ScannerFlid, unsigned int TargetFlid, unsigned int GoodId) {
    GameObject* Obj = Server_LookupLiveObject(TargetFlid);
    if (Obj == nullptr) return static_cast<ScanResponse>(0);
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;
    if (::Players.is_valid_ship_owner(OwnerPlayer, TargetFlid)) {
        g_pClientInterface->SendServerScanNotify(OwnerPlayer, ScannerFlid, GoodId);
        return static_cast<ScanResponse>(3);
    }
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return static_cast<ScanResponse>(0);
    return Behavior->get_scan_response(ScannerFlid);
}

// pub::AI::submit_forced_target — retail 0x7ddc0, .text:06d5ddc0..06d5ddfa, 22 insns.
// No message and no Flid mask:
//     obj = FUN_06d00670(ship_id);                 null -> return 0 (EAX)
//     bm  = COMMON.DLL::GetBehaviorManager(obj);   null -> return 0
//     sys = bm[+0x8] (SystemManager*);             null -> return 0
//     return COMMON.DLL::TargetManager::submit_forced_target(sys + 0x142c, target_id);
// TargetManager (Common 0x062eec10: append to the 8-entry id array at this+0x1c, count at
// this+0x18; 1 appended, 0 full or already present) is the class at SystemManager+0x142C
// (RTTI .?AVTargetManager@@) -- SystemManager::Targeting in common_systemmanager.h.
SERVER_API int __cdecl submit_forced_target(unsigned int ship_id, unsigned int target_id) {
    GameObject* Obj = Server_LookupLiveObject(ship_id);
    if (Obj == nullptr) return 0;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return 0;
    if (Behavior->Sys == nullptr) return 0;
    return Behavior->Sys->Targeting.submit_forced_target(target_id);
}

// pub::AI::remove_forced_target — retail 0x7de00, .text:06d5de00..06d5de3a, 22 insns.
// Identical chain ending in COMMON.DLL::TargetManager::remove_forced_target(sys + 0x142c,
// target_id) (Common 0x062eec50: remove the id and close the gap; 1 removed, 0 not found),
// whose result is returned.
SERVER_API int __cdecl remove_forced_target(unsigned int ship_id, unsigned int target_id) {
    GameObject* Obj = Server_LookupLiveObject(ship_id);
    if (Obj == nullptr) return 0;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return 0;
    if (Behavior->Sys == nullptr) return 0;
    return Behavior->Sys->Targeting.remove_forced_target(target_id);
}

// pub::AI::set_player_enemy_clamp — retail 0x7de40, .text:06d5de40..06d5de87, 28 insns.
// The same chain, each failed step returning false, then
// COMMON.DLL::TargetManager::set_player_enemy_clamp(sys + 0x142c, min_rep, max_rep) (Common
// 0x062ee5e0: stores the pair in DAT_063ec754 / DAT_063ec758) and `return true`. No message
// (0x41 is fired from 0x06d447fe, never from here).
SERVER_API bool __cdecl set_player_enemy_clamp(unsigned int player_id, int min_rep, int max_rep) {
    GameObject* Obj = Server_LookupLiveObject(player_id);
    if (Obj == nullptr) return false;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return false;
    if (Behavior->Sys == nullptr) return false;
    Behavior->Sys->Targeting.set_player_enemy_clamp(min_rep, max_rep);
    return true;
}

// pub::AI::update_formation_state — retail 0x7d7a0, .text:06d5d7a0..06d5db33, 297 insns,
// read in full from the PE bytes (SEH frame + SUB ESP,0x54). ?update_formation_state@AI@pub@@
// YA?AW4FORMATION_RTYPE@@IIABVVector@@@Z -- FORMATION_RTYPE is the GLOBAL enum (common_pub.h).
// Argument 1 is the ship that changes formation, argument 2 its NEW leader (may be 0),
// argument 3 its offset. Only two return values exist: -3 when ship 1 does not resolve, else 0.
//
//   ship = FUN_06d00670(ShipFlid)                       null -> return -3 (06d5d7d2)
//   ship->get_formation_leader(OldLeader)               (IObjInspect +0xb0)
//   NewLeader = FUN_06d00670(LeaderFlid)
//   if (!SinglePlayer() && NewLeader == OldLeader) return 0;     (06d5d80d..06d5d81a)
//   if (!SinglePlayer() && OldLeader) (06d5d821..06d5d98f): a stack std::vector<unsigned>;
//       count = OldLeader IObjAffect +0x20; OldLeader +0x14c (array, 0x10); OldLeader's
//       get_id pushed ONLY when count != 0 (06d5d86f); then every follower's id except
//       the ship's own (compared by get_id); then IClient slot 120 (+0x1e0, DAT_06d8fb14)
//       (owner, &vector) to every follower and then to OldLeader whose cobject() (+0x150)
//       is a CShip ((Class & 0x503) == 0x503) with byte +0x2bd set, owner = CSimple+0xb4;
//       FUN_06d5ffb0 frees the buffer. The ship itself is still a follower here, so it
//       is among the receivers; only its id is left out.
//   if (OldLeader && OldLeader id != ship id) OldLeader IObjAffect +0x1c (ship)     (06d5d9b6)
//       (in multiplayer a null OldLeader jumps straight past this, 06d5d834)
//   ship IObjAffect +0x10 (NewLeader), then +0x14 (Offset)                          (06d5d9c2/ce)
//   if (NewLeader && NewLeader != ship) NewLeader IObjAffect +0x18 (ship)            (06d5d9e2)
//   if (!SinglePlayer() && NewLeader) (06d5d9e5..06d5db17): the same broadcast for
//       NewLeader's formation, except that NewLeader's id is pushed unconditionally
//       and no follower is excluded.
//   return 0.
// Ported 1:1 2026-09-24 (claude-code:w5-server-06d4): the two broadcasts were the last
// TODO. Retail reads Followers[i] for i < count with no bound against the 16-entry array;
// so does the port. The old `(LeaderFlid & 0x3fffffff) == 0` test was invented.

// The receiver step retail inlines four times (06d5d8e7..06d5d92c, 06d5d935..06d5d974,
// 06d5da85..06d5dac4, 06d5dacf..06d5db0c).
static void Formation_SendIdsToOwner(IObjRW const* Obj, std::vector<unsigned int> const& Ids) {
    CObject const* const Cobj = Obj->cobject();                    // +0x150
    if (Cobj == nullptr)
        return;
    if ((static_cast<unsigned int>(Cobj->ObjectClass) & 0x503u) != 0x503u)  // +0x4c
        return;
    CShip const* const ShipCobj = static_cast<CShip const*>(Cobj);
    if (ShipCobj->bIsPlayer == 0)                                  // +0x2bd
        return;
    ::g_pClientInterface->SendServerMiscObjUpdateFormation(ShipCobj->OwnerPlayer, Ids);   // +0xb4, IClient +0x1e0
}

SERVER_API FORMATION_RTYPE __cdecl update_formation_state(unsigned int ShipFlid, unsigned int LeaderFlid, const Vector& Offset) {
    GameObject* Ship = Server_LookupLiveObject(ShipFlid);
    if (Ship == nullptr) return FORMATION_RTYPE_NOT_FOUND;       // 06d5d7d2: -3

    // 06d5d7e7-06d5d7f4: the out-slot is zeroed first, then get_formation_leader (+0xb0).
    IObjRW* OldLeader = nullptr;
    Ship->get_formation_leader(OldLeader);
    IObjRW* const NewLeader = Server_LookupLiveObject(LeaderFlid);   // 06d5d7ff
    if (!SinglePlayer() && NewLeader == OldLeader)                  // 06d5d80d-06d5d81a
        return FORMATION_RTYPE_OK;

    if (!SinglePlayer() && OldLeader != nullptr) {                  // 06d5d821-06d5d834
        std::vector<unsigned int> Ids;                              // ESP+0x14
        int const Count = OldLeader->get_formation_follower_count(); // 06d5d857 IObjAffect +0x20
        IObjRW* Followers[0x10];                                    // ESP+0x24
        OldLeader->get_formation_followers(Followers, 0x10);        // 06d5d869 +0x14c
        if (Count != 0)                                             // 06d5d86f
            Ids.push_back(OldLeader->get_id());                     // 06d5d879 / 06d5d890
        for (int I = 0; I < Count; ++I) {                           // 06d5d895..06d5d8db
            if (Followers[I]->get_id() != Ship->get_id())           // 06d5d8a6 / 06d5d8af
                Ids.push_back(Followers[I]->get_id());              // 06d5d8bc / 06d5d8d3
        }
        for (int I = 0; I < Count; ++I)                             // 06d5d8e1..06d5d933
            Formation_SendIdsToOwner(Followers[I], Ids);
        Formation_SendIdsToOwner(OldLeader, Ids);                   // 06d5d935..06d5d974
    }                                                               // 06d5d987 FUN_06d5ffb0

    // 06d5d991-06d5d9b6: OldLeader's IObjAffect +0x1c, remove_formation_follower(ship),
    // unless OldLeader is the ship itself (compared by get_id()).
    if (OldLeader != nullptr && OldLeader->get_id() != Ship->get_id())
        (void)OldLeader->remove_formation_follower(Ship);
    // 06d5d9b9-06d5d9ce: the ship's own +0x10 set_formation_leader and +0x14
    // set_formation_offset.
    (void)Ship->set_formation_leader(NewLeader);
    (void)Ship->set_formation_offset(Offset);
    // 06d5d9d1-06d5d9e2: NewLeader's +0x18 add_formation_follower(ship), unless it is the
    // ship (compared by pointer here).
    if (NewLeader != nullptr && NewLeader != Ship)
        (void)NewLeader->add_formation_follower(Ship);

    if (!SinglePlayer() && NewLeader != nullptr) {                  // 06d5d9e5-06d5d9f4
        std::vector<unsigned int> Ids;                              // ESP+0x14
        int const Count = NewLeader->get_formation_follower_count(); // 06d5da1c IObjAffect +0x20
        IObjRW* Followers[0x10];                                    // ESP+0x24
        NewLeader->get_formation_followers(Followers, 0x10);        // 06d5da2c +0x14c
        Ids.push_back(NewLeader->get_id());                         // 06d5da36 / 06d5da4d
        for (int I = 0; I < Count; ++I)                             // 06d5da52..06d5da7d
            Ids.push_back(Followers[I]->get_id());
        for (int I = 0; I < Count; ++I)                             // 06d5da7f..06d5dacd
            Formation_SendIdsToOwner(Followers[I], Ids);
        Formation_SendIdsToOwner(NewLeader, Ids);                   // 06d5dacf..06d5db0c
    }                                                               // 06d5db17 FUN_06d5ffb0
    return FORMATION_RTYPE_OK;                                      // 06d5db26
}

// pub::AI::refresh_state_graph — retail 0x7db40, .text:06d5db40..06d5db67, 14 insns.
// Lookup, GetBehaviorManager, then a tail JMP into IBehaviorManager::refresh_state_graph
// (06d5db61); either failed step just returns.
SERVER_API void __cdecl refresh_state_graph(unsigned int Flid) {
    GameObject* Obj = Server_LookupLiveObject(Flid);
    if (Obj == nullptr) return;
    IBehaviorManager* Behavior = GetBehaviorManager(Obj);
    if (Behavior == nullptr) return;
    Behavior->refresh_state_graph();
}

} // namespace AI

// pub::Phantom exports -- the record, the global list and the event pump are above
// namespace pub::SpaceObj (Server_PumpPhantomEvents).
namespace Phantom {

// pub::Phantom::Create (box) -- retail .text:06cfbe20 .. 06cfbfce.
//   06cfbe3e .. 06cfbe46  pump latch -> -5
//   06cfbe4c .. 06cfbe8a  any of size.x/y/z not > 0 (FCOMP 0.0f, TEST AH,0x41; NaN
//                         included) -> -4
//   06cfbe90 .. 06cfbe9a  (system_id & 0x3fffffff) == 0 -> -4
//   06cfbea3 .. 06cfbefd  the record (new 0x28, both lists empty)
//   06cfbf01 .. 06cfbf5c  PhantomParms {system_id, +4 = 1, +5 = 0, name[0] = 0,
//                         Position = pos, Orientation = orient, Listener = record};
//                         record->TypeMask = type_mask, record->SystemId = system_id
//   06cfbf5f              PhySys::CreatePhantom(parms, size, record->PhyHandle); its
//                         result is not tested
//   06cfbf65 .. 06cfbfab  out = record; push_back onto the global list; return 0
// PhantomParms bytes retail leaves unwritten (+0x07 .. +0x0f) are zeroed here
// (deviations.md, zeroed indeterminate stack bytes).
SERVER_API int __cdecl Create(unsigned int SystemId, const Vector& Size, const Vector& Pos,
                              const Matrix& Orient, unsigned int TypeMask, void*& OutHandle) {
    if (GbPhantomPumpBusy) return -5;                                    // .text:06cfbe46
    if (!(Size.x > 0.0f)) return -4;                                     // .text:06cfbe62
    if (!(Size.y > 0.0f)) return -4;                                     // .text:06cfbe76
    if (!(Size.z > 0.0f)) return -4;                                     // .text:06cfbe8a
    if ((SystemId & 0x3fffffffu) == 0) return -4;                        // .text:06cfbe9a

    ServerPhantom* const Ph = new ServerPhantom();                       // .text:06cfbea5
    PhySys::PhantomParms Parms;
    memset(&Parms, 0, sizeof(Parms));
    Parms.SystemId = SystemId;                                           // .text:06cfbf2f
    Parms.Flag04 = 1;                                                    // .text:06cfbf33
    Parms.Flag05 = 0;                                                    // .text:06cfbf25
    Parms.Name[0] = 0;                                                   // .text:06cfbf2a
    Parms.Position = Pos;                                                // .text:06cfbf11 .. 06cfbf1d
    Parms.Orientation = Orient;                                          // .text:06cfbf41 (9 dwords)
    Parms.Listener = Ph;                                                 // .text:06cfbf46
    Ph->TypeMask = TypeMask;                                             // .text:06cfbf4b
    Ph->SystemId = SystemId;                                             // .text:06cfbf5c
    PhySys::CreatePhantom(Parms, Size, Ph->PhyHandle);                   // .text:06cfbf5f
    OutHandle = Ph;                                                      // .text:06cfbf6c
    GPhantoms.push_back(Ph);                                             // .text:06cfbf6e .. 06cfbfa6
    return 0;
}

// pub::Phantom::Create (sphere) -- retail .text:06cfc020 .. 06cfc1db.
//   06cfc03e .. 06cfc049  pump latch -> -5
//   06cfc04f .. 06cfc063  radius not > 0 (NaN included) -> -4
//   06cfc069 .. 06cfc073  (system_id & 0x3fffffff) == 0 -> -4
//   06cfc07b .. 06cfc0d2  the record
//   06cfc0d6 .. 06cfc168  PhantomParms {system_id, 1, 0, name[0] = 0, Position = center,
//                         Orientation = identity, Listener = record}; TypeMask, SystemId
//   06cfc16b              PhySys::CreatePhantom(parms, radius, record->PhyHandle)
//   06cfc171 .. 06cfc1b7  out = record; push_back; return 0
SERVER_API int __cdecl Create(unsigned int SystemId, float Radius, const Vector& Center,
                              unsigned int TypeMask, void*& OutHandle) {
    if (GbPhantomPumpBusy) return -5;                                    // .text:06cfc049
    if (!(Radius > 0.0f)) return -4;                                     // .text:06cfc063
    if ((SystemId & 0x3fffffffu) == 0) return -4;                        // .text:06cfc073

    ServerPhantom* const Ph = new ServerPhantom();                       // .text:06cfc07d
    PhySys::PhantomParms Parms;
    memset(&Parms, 0, sizeof(Parms));
    Parms.SystemId = SystemId;                                           // .text:06cfc101
    Parms.Flag04 = 1;                                                    // .text:06cfc105
    Parms.Flag05 = 0;                                                    // .text:06cfc0f9
    Parms.Name[0] = 0;                                                   // .text:06cfc0fd
    Parms.Position = Center;                                             // .text:06cfc0e2 .. 06cfc0ee
    Parms.Orientation = Matrix(1.0f, 0.0f, 0.0f,                          // .text:06cfc10a .. 06cfc14a
                               0.0f, 1.0f, 0.0f,
                               0.0f, 0.0f, 1.0f);
    Parms.Listener = Ph;                                                 // .text:06cfc152
    Ph->TypeMask = TypeMask;                                             // .text:06cfc157
    Ph->SystemId = SystemId;                                             // .text:06cfc168
    PhySys::CreatePhantom(Parms, Radius, Ph->PhyHandle);                 // .text:06cfc16b
    OutHandle = Ph;                                                      // .text:06cfc178
    GPhantoms.push_back(Ph);                                             // .text:06cfc17a .. 06cfc1b2
    return 0;
}

// pub::Phantom::Destroy -- retail .text:06cfbcd0 .. 06cfbd53.
//   latch -> -5; handle not on the global list -> -2;
//   PhySys::DestroyPhantom(record->PhyHandle) (06cfbd0c); the record's destructor
//   (FUN_06cfbd60) and operator delete (06cfbd1e / 06cfbd24); the list node is unlinked
//   and freed, the count decremented (06cfbd2c .. 06cfbd4a); return 0.
SERVER_API int __cdecl Destroy(void* Handle) {
    if (GbPhantomPumpBusy) return -5;                                    // .text:06cfbcdf
    std::list<ServerPhantom*>::iterator It = FindPhantom(Handle);
    if (It == GPhantoms.end()) return -2;                                // .text:06cfbcf0 / 06cfbd03
    PhySys::DestroyPhantom((*It)->PhyHandle);                            // .text:06cfbd0c
    delete *It;                                                          // .text:06cfbd1e / 06cfbd24
    GPhantoms.erase(It);                                                 // .text:06cfbd2c .. 06cfbd4a
    return 0;
}

// pub::Phantom::SetActive -- retail .text:06cfbfd0 .. 06cfc01c.
//   latch -> -5; not found -> -2;
//   PhySys::SetCollisionStatePhantom(record->PhyHandle, active) (06cfc011); return 0.
SERVER_API int __cdecl SetActive(void* Handle, bool bActive) {
    if (GbPhantomPumpBusy) return -5;                                    // .text:06cfbfdd
    std::list<ServerPhantom*>::iterator It = FindPhantom(Handle);
    if (It == GPhantoms.end()) return -2;                                // .text:06cfc003
    PhySys::SetCollisionStatePhantom((*It)->PhyHandle, bActive);         // .text:06cfc011
    return 0;
}

// pub::Phantom::Attach -- retail .text:06cfc1e0 .. 06cfc26e.
//   latch -> -5; FUN_06d00670(*obj_id) null -> -2; handle not found -> -3;
//   the object's CBase::Object when (class & 1) == 1, else null (06cfc22f .. 06cfc24b);
//   PhySys::AttachPhantom(cobject, record->PhyHandle) non-zero -> -1, else 0.
SERVER_API int __cdecl Attach(const unsigned int& ObjId, void* Handle) {
    if (GbPhantomPumpBusy) return -5;                                    // .text:06cfc1ef
    GameObject* const Obj = Server_LookupLiveObject(ObjId);              // .text:06cfc1fd
    if (Obj == nullptr) return -2;                                       // .text:06cfc207
    std::list<ServerPhantom*>::iterator It = FindPhantom(Handle);
    if (It == GPhantoms.end()) return -3;                                // .text:06cfc22d
    CObject* Raw = Obj->Object;                                          // .text:06cfc239
    if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & 1u) != 1u)
        Raw = nullptr;                                                   // .text:06cfc249 / 06cfc24b
    if (PhySys::AttachPhantom(Raw, (*It)->PhyHandle) != 0) return -1;    // .text:06cfc255 .. 06cfc260
    return 0;
}

// pub::Phantom::Detach -- retail .text:06cfc270 .. 06cfc2c0.
//   latch -> -5; not found -> -2; PhySys::DetachPhantom(record->PhyHandle) non-zero ->
//   -1, else 0.
SERVER_API int __cdecl Detach(void* Handle) {
    if (GbPhantomPumpBusy) return -5;                                    // .text:06cfc27e
    std::list<ServerPhantom*>::iterator It = FindPhantom(Handle);
    if (It == GPhantoms.end()) return -2;                                // .text:06cfc2a2
    if (PhySys::DetachPhantom((*It)->PhyHandle) != 0) return -1;         // .text:06cfc2ae .. 06cfc2b9
    return 0;
}

} // namespace Phantom

// ===========================================================================
// pub::Audio exports
// ===========================================================================
namespace Audio {

// pub::Audio::SetMusic — retail 0x7eec0
// Retail 0x7eec0 (0x06d5eec0): no validation at all, just one IClient call and
// `return 0`. 0x1e8 / 4 = slot 122; the seven arguments are the player id followed by
// the Tryptich unpacked field by field (+0x00, +0x04, +0x08, +0x0c, the BYTE at +0x10
// zero-extended, and +0x14). The id 0x44 the port used is not a message id at all.
SERVER_API int __cdecl SetMusic(unsigned int PlayerId, const Tryptich& Track) {
    if (!g_pClientInterface) return 0;
    // Field14 holds the fade's float bits (retail pushes the dword; the sender FLDs it).
    float Fade = 0.0f;
    memcpy(&Fade, &Track.Field14, sizeof(Fade));
    g_pClientInterface->SendServerSetMusic(PlayerId, Track.Field0, Track.Field4, Track.Field8,
                                           Track.FieldC, Track.Field10 != 0, Fade);
    return 0;
}

// pub::Audio::CancelMusic — retail 0x7ef00
// Retail 0x7ef00 (0x06d5ef00): IClient::SendServerCancelMusic(PlayerId) (+0x1ec / 4 = 123),
// return 0.
SERVER_API int __cdecl CancelMusic(unsigned int PlayerId) {
    if (!g_pClientInterface) return 0;
    g_pClientInterface->SendServerCancelMusic(PlayerId);
    return 0;
}

// pub::Audio::PlaySoundEffect — retail 0x7ef20
// Retail 0x7ef20 (0x06d5ef20): IClient::SendServerPlaySoundEffect(PlayerId, SoundId)
// (+0x1f0 / 4 = 124), return 0.
SERVER_API int __cdecl PlaySoundEffect(unsigned int PlayerId, unsigned int SoundId) {
    if (!g_pClientInterface) return 0;
    g_pClientInterface->SendServerPlaySoundEffect(PlayerId, SoundId);
    return 0;
}

} // namespace Audio

// ===========================================================================
// pub::Market exports — line-by-line verified against Server.dll.c 0x385c0 - 0x387d0
// ===========================================================================
namespace Market {

// ---------------------------------------------------------------------------
// Rewritten 2026-09-17 against the raw listing, not the .c. Every one of these
// eight exports funnels through the same three calls --
//   FUN_06d15650()             the base database singleton
//   FUN_06d140f0(this, BaseId) the FBaseEntry lookup
//   FUN_06d12xxx(this, ...)    a helper whose `this` carries the BaseData* at
//                              this+0x04 and the market map at BaseData+0x10
//                              (`MOV ESI,[this+4]` / `ADD ESI,0x10`, e.g.
//                              .text:06d12cd2 / .text:06d12ce3)
// -- and every one of them returns -4 on failure, never -1: the constant is
// `MOV reg,0xfffffffc` at .text:06d185cb, 06d1861b, 06d1866a, 06d18696,
// 06d186d0, 06d1871f, 06d1875a, 06d1879f and 06d187da. The port returned -1
// from six of the eight.
//
// Which of them pre-clear their out-parameter before the bad-id check is also
// per-function in retail, and the port had guessed uniformly. Retail clears in
// IsGoodInDemand (.text:06d186d5), GetNumCommoditiesForSale (.text:06d18724)
// and GetNumCommoditiesInDemand (.text:06d187a4), and in nothing else.
// ---------------------------------------------------------------------------

// pub::Market::GetMinInventory - retail 0x385c0 (.text:06d185c0).
// Body is FUN_06d12c70: map::find(GoodId) over BaseData+0x10, and on a hit
// return the mapped value's +0x08 (.text:06d12cbe MOV EAX,[EAX+0x8], EAX being
// node+0x10). On a miss return 0 (.text:06d12cc7 XOR EAX,EAX). That +0x08 slot
// is what names MarketGoodInfo::MinInventory.
//
// The port used to return MarketGoodInfo::Quantity, which was the struct's
// fifth dword -- retail's TransactionType -- so this reported a transaction
// code where a stock level was asked for.
SERVER_API int __cdecl GetMinInventory(unsigned int BaseId, unsigned int GoodId, int& OutInv) {
    if ((BaseId & 0x3fffffff) == 0 || (GoodId & 0x3fffffff) == 0) return -4;
    // FUN_06d15650 + FUN_06d140f0 (BaseDB::find_base; a miss FDUMPs "Base %u is
    // invalid", line 0x659, and returns 0 -> -4); the helper reads [entry+4].
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.find(GoodId);
    OutInv = (it != base->Market.end()) ? it->second.MinInventory : 0;
    return 0;
}

// pub::Market::GetPrice - retail 0x38610 (.text:06d18610).
// Body is FUN_06d12cd0: map::find(GoodId), and on a hit
// `.text:06d12d02  FLD [EAX+0x4]` where EAX = node+0x10 -- the STORED price.
// On a miss it falls back to GoodList::find_by_id(GoodId)->Price
// (.text:06d12d0c / .text:06d12d12 FLD [EAX+0x58]).
//
// Nothing here recomputes anything. Common.dll computes the price once, at
// load time, and truncates it through ftol (.text:06265c2f); see
// MarketGoodInfo in common_goods.h. The port multiplied nominal by a
// PriceMult field on every call and never truncated, so it disagreed with
// retail by a fraction on every good with a multiplier -- and PriceMult was
// not even the field it was reading, since the struct was rotated by a slot.
SERVER_API int __cdecl GetPrice(unsigned int BaseId, unsigned int GoodId, float& OutPrice) {
    if ((BaseId & 0x3fffffff) == 0 || (GoodId & 0x3fffffff) == 0) return -4;
    // FUN_06d15650 + FUN_06d140f0 (BaseDB::find_base; a miss FDUMPs "Base %u is
    // invalid", line 0x659, and returns 0 -> -4); the helper reads [entry+4].
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.find(GoodId);
    if (it != base->Market.end()) {
        OutPrice = it->second.Price;
        return 0;
    }
    // .text:06d12d0b .. .text:06d12d12 -- the miss falls back to
    // GoodList::find_by_id(GoodId) and reads +0x58 with no null test.
    OutPrice = GoodList::find_by_id(GoodId)->Price;
    return 0;
}

// pub::Market::GetNominalPrice - retail 0x38660 (.text:06d18660).
// GoodInfo+0x58 straight out of GoodList::find_by_id (.text:06d1867f), -4 on
// a bad id or a miss. No pre-clear of the out-parameter (.text:06d18686 is the
// only write).
SERVER_API int __cdecl GetNominalPrice(unsigned int GoodId, float& OutPrice) {
    if ((GoodId & 0x3fffffff) == 0) return -4;
    GoodInfo const* good = GoodList::find_by_id(GoodId);
    if (!good) return -4;
    OutPrice = good->Price;
    return 0;
}

// pub::Market::GetGoodJumpDist - retail 0x38690 (.text:06d18690).
// GoodInfo+0x6c (.text:06d186a8). Note retail does NOT screen the good id here
// the way its neighbours do: it calls find_by_id unconditionally
// (.text:06d1869b) and only checks the result. Failure is -4
// (.text:06d18696 MOV ESI,0xfffffffc), not the -1 the port returned.
SERVER_API int __cdecl GetGoodJumpDist(unsigned int GoodId, unsigned int& OutDist) {
    GoodInfo const* good = GoodList::find_by_id(GoodId);
    if (!good) return -4;
    OutDist = (unsigned int)good->JumpDist;
    return 0;
}

// pub::Market::IsGoodInDemand - retail 0x386c0 (.text:06d186c0).
// Clears the bool first (.text:06d186d5 MOV byte ptr [ESI],0x0), then
// FUN_06d12d20: map::find(GoodId) and
//   .text:06d12d70  MOV  EAX,[EAX+0x10]   ; mapped value +0x10 = TransactionType
//   .text:06d12d73  TEST EAX,EAX / JZ true
//   .text:06d12d77  CMP  EAX,0x1 / JNZ false
// so the answer is `Type == 0 || Type == 1`. The port tested `RepMax >= 0`,
// which was the struct's fifth dword read under a name from a different slot
// and a comparison retail does not make anywhere.
SERVER_API int __cdecl IsGoodInDemand(unsigned int BaseId, unsigned int GoodId, bool& OutDemand) {
    OutDemand = false;
    if ((BaseId & 0x3fffffff) == 0 || (GoodId & 0x3fffffff) == 0) return -4;
    // FUN_06d15650 + FUN_06d140f0 (BaseDB::find_base; a miss FDUMPs "Base %u is
    // invalid", line 0x659, and returns 0 -> -4); the helper reads [entry+4].
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.find(GoodId);
    if (it != base->Market.end()) {
        OutDemand = (it->second.Type == TransactionBuyAndSell ||
                     it->second.Type == TransactionDemandOnly);
    }
    return 0;
}

// The two commodity filters, both read off retail's node walk rather than a
// std::map iterator, so the offsets there are node-relative:
//   node+0x10 = mapped+0x00 = GoodId  (.text:06d12eb0 / .text:06d12e10)
//   node+0x20 = mapped+0x10 = Type    (.text:06d12ec4 / .text:06d12e24)
// Retail feeds GoodList::find_by_id the id out of the VALUE, not the map key,
// and requires GoodInfo+0x4c == 0 (.text:06d12ebd / .text:06d12e1d), the
// commodity category.
static bool MarketGoodIsCommodity(MarketGoodInfo const& Entry) {
    GoodInfo const* info = GoodList::find_by_id(Entry.GoodId);
    return info != nullptr && info->Category == (unsigned int)GoodCategoryCommodity;
}

// for sale: .text:06d12ec7 TEST EAX,EAX / JZ accept / .text:06d12ecb CMP EAX,0x2
static bool MarketGoodForSale(MarketGoodInfo const& Entry) {
    return MarketGoodIsCommodity(Entry) &&
           (Entry.Type == TransactionBuyAndSell || Entry.Type == TransactionSaleOnly);
}

// in demand: .text:06d12e27 TEST EAX,EAX / JZ accept / .text:06d12e2b CMP EAX,0x1
static bool MarketGoodInDemand(MarketGoodInfo const& Entry) {
    return MarketGoodIsCommodity(Entry) &&
           (Entry.Type == TransactionBuyAndSell || Entry.Type == TransactionDemandOnly);
}

// pub::Market::GetNumCommoditiesForSale - retail 0x38710, helper FUN_06d12d90.
SERVER_API int __cdecl GetNumCommoditiesForSale(unsigned int BaseId, int* OutCount) {
    *OutCount = 0;   // .text:06d18724, before the id test and with no pointer test
    if ((BaseId & 0x3fffffff) == 0) return -4;
    // FUN_06d15650 + FUN_06d140f0 (BaseDB::find_base; a miss FDUMPs "Base %u is
    // invalid", line 0x659, and returns 0 -> -4); the helper reads [entry+4].
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    int count = 0;
    for (std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.begin();
         it != base->Market.end(); ++it) {
        if (MarketGoodForSale(it->second))
            count++;
    }
    *OutCount = count;
    return 0;
}

// pub::Market::GetCommoditiesForSale - retail 0x38750, helper FUN_06d12e80.
// The helper reads the cap out of *InOutCount once, up front
// (.text:06d12e85 MOV EAX,[EDX]), tests it with `CMP EBX,cap / JGE skip`
// (.text:06d12ed0) so an over-cap good is skipped but the walk continues, and
// writes the final count back at .text:06d12f36. No pre-clear on the failure
// paths.
SERVER_API int __cdecl GetCommoditiesForSale(unsigned int BaseId, unsigned int* const OutGoods, int* InOutCount) {
    // No pointer tests: .text:06d18772/06d18776 hand both straight to FUN_06d12e80.
    if ((BaseId & 0x3fffffff) == 0) return -4;
    // FUN_06d15650 + FUN_06d140f0 (BaseDB::find_base; a miss FDUMPs "Base %u is
    // invalid", line 0x659, and returns 0 -> -4); the helper reads [entry+4].
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    int const max_count = *InOutCount;
    int count = 0;
    for (std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.begin();
         it != base->Market.end(); ++it) {
        if (!MarketGoodForSale(it->second))
            continue;
        if (count >= max_count)
            continue;
        OutGoods[count++] = it->second.GoodId;
    }
    *InOutCount = count;
    return 0;
}

// pub::Market::GetNumCommoditiesInDemand - retail 0x38790, helper FUN_06d12df0.
SERVER_API int __cdecl GetNumCommoditiesInDemand(unsigned int BaseId, int* OutCount) {
    *OutCount = 0;   // .text:06d187a4, before the id test and with no pointer test
    if ((BaseId & 0x3fffffff) == 0) return -4;
    // FUN_06d15650 + FUN_06d140f0 (BaseDB::find_base; a miss FDUMPs "Base %u is
    // invalid", line 0x659, and returns 0 -> -4); the helper reads [entry+4].
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    int count = 0;
    for (std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.begin();
         it != base->Market.end(); ++it) {
        if (MarketGoodInDemand(it->second))
            count++;
    }
    *OutCount = count;
    return 0;
}

// pub::Market::GetCommoditiesInDemand - retail 0x387d0, helper FUN_06d12f40.
SERVER_API int __cdecl GetCommoditiesInDemand(unsigned int BaseId, unsigned int* const OutGoods, int* InOutCount) {
    // No pointer tests: .text:06d187f2/06d187f6 hand both straight to FUN_06d12f40.
    if ((BaseId & 0x3fffffff) == 0) return -4;
    // FUN_06d15650 + FUN_06d140f0 (BaseDB::find_base; a miss FDUMPs "Base %u is
    // invalid", line 0x659, and returns 0 -> -4); the helper reads [entry+4].
    FBaseEntry* Entry = GetBaseDB()->find_base(BaseId);
    if (!Entry) return -4;
    BaseData* base = Entry->Data;
    int const max_count = *InOutCount;
    int count = 0;
    for (std::map<unsigned int, MarketGoodInfo>::const_iterator it = base->Market.begin();
         it != base->Market.end(); ++it) {
        if (!MarketGoodInDemand(it->second))
            continue;
        if (count >= max_count)
            continue;
        OutGoods[count++] = it->second.GoodId;
    }
    *InOutCount = count;
    return 0;
}

} // namespace Market

// ===========================================================================
// pub:: utility batch 2 — line-by-line verified against Server.dll.c
// ===========================================================================

// pub::FindHardpoint — retail RVA 0x7E270, slot 130
// ?FindHardpoint@pub@@YAHPBDIAAVVector@@AAVMatrix@@@Z
// Flow: GetShip → Archetype::Load (mesh) → HardpointPositionA (lookup) → release handle.
// Returns: 0 = found, -2 = load failed, -3 = hardpoint not found.
// .text:06d5e270..06d5e2d8, traced 2026-09-24 (claude-code:w5-server-06d4):
// GetShip's result is dereferenced straight away (06d5e27b MOV EAX,[EAX+0xc]) --
// retail has no null test, so none here. The engine index is written into
// param_2's own stack slot (06d5e280 LEA ECX,[ESP+0x10]), so its initial value
// is the ship archetype id, not -1.
SERVER_API int __cdecl FindHardpoint(char const* Name, unsigned int ShipArchId,
                                     Vector& OutPos, Matrix& OutRot)
{
    Archetype::Ship* ShipArch = Archetype::GetShip(ShipArchId);
    char const* DaPath = ShipArch->DaArchetype.Value;
    long EngineIndex = static_cast<long>(ShipArchId);
    bool bLoaded = Archetype::Load(DaPath, &EngineIndex, nullptr);
    if (!bLoaded)
        return -2;

    bool bFound = HardpointPositionA(EngineIndex, Name, &OutPos, &OutRot);
    // 0x06d5e2b7-0x06d5e2cc: IEngine::ReleaseHandle (slot 15, +0x3C) straight on
    // DALIB.DLL::DALib::Engine -- no Common.dll wrapper, no null check.
    DALib::Engine->ReleaseHandle(reinterpret_cast<void*>(EngineIndex));
    return bFound ? 0 : -3;
}

// pub::GetShipID — retail RVA 0x7E2E0, slot 271
// ?GetShipID@pub@@YAHAAIPBD@Z
// Flow: validate name → CreateID → Archetype::GetShipID (returns large ID).
// Return code: 0 = found (non-zero ID), -2 = not found, -4 = null/empty name.
SERVER_API int __cdecl GetShipID(unsigned int& OutShipId, char const* ShipName)
{
    if (ShipName != nullptr && ShipName[0] != '\0')
    {
        unsigned int NicknameId = CreateID(ShipName);
        ID_String Name;
        Name.Id = NicknameId;
        unsigned int ShipId = Archetype::GetShipID(Name);
        OutShipId = ShipId;
        if ((ShipId & 0x3fffffff) != 0)
            return 0;
        return -2;
    }
    return -4;
}

// pub::TranslateGoodToMsgIdPrefix — retail RVA 0x7E050, slot 466
// ?TranslateGoodToMsgIdPrefix@pub@@YAHIAAU?$TString@$0EA@@@@Z
// .text:06d5e050: GoodList::find_by_id, then CommReferrable slot 1 (GetMsgIdPrefix) on the
// GoodInfo, and TString<64>::operator=(char const*) inlined on its Data.
// Returns: 0 = success (including empty prefix), -4 = good not found.
SERVER_API int __cdecl TranslateGoodToMsgIdPrefix(unsigned int GoodId,
                                                   TString<64>& OutPrefix)
{
    GoodInfo const* Info = GoodList::find_by_id(GoodId);
    if (Info == nullptr)
        return -4;

    OutPrefix = Info->GetMsgIdPrefix().Data;
    return 0;
}

// pub::TranslateShipToMsgIdPrefix — retail RVA 0x7E0D0, slot 467
// ?TranslateShipToMsgIdPrefix@pub@@YAHIAAU?$TString@$0EA@@@@Z
// .text:06d5e0d0: Archetype::GetShip, then slot 1 (GetMsgIdPrefix) of the CommReferrable
// subobject at +0x94, and TString<64>::operator=(char const*) inlined on its Data.
// Returns: 0 = success (including empty prefix), -4 = ship not found.
SERVER_API int __cdecl TranslateShipToMsgIdPrefix(unsigned int ShipArchId,
                                                   TString<64>& OutPrefix)
{
    Archetype::Ship* ShipArch = Archetype::GetShip(ShipArchId);
    if (ShipArch == nullptr)
        return -4;

    OutPrefix = ShipArch->GetMsgIdPrefix().Data;
    return 0;
}

} // namespace pub

