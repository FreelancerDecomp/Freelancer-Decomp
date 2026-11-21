// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_space.cpp -- the objects between systems and the object lookup by id (retail
// ServerSpace, .text:06d002e0 .. 06d00827).
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


// ---------------------------------------------------------------------------
// The objects between systems and the object lookup by id, retail
// .text:06d002e0 .. 06d00827. See server_gameobject.h for the declarations.
// ---------------------------------------------------------------------------


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

// PORT(perf-index): true when every block StarSystem::find_object
// searches is empty. SysDB::SysMap holds every system of the universe, loaded or
// not, and Server_FindObject's miss path asks each one in turn. For such a system
// find_object(Id) returns null and does nothing else: it is non-virtual, writes
// no global and no counter, and each find_by_id walk starts at a null Head, so no
// get_id is called and nothing can fault. (The port's id index may record a
// lookup on the block; that only steers when the index is rebuilt.)
static bool StarSystem_FindObjectBlocksEmpty(StarSystem const& System)
{
    return System.Ships.Head == nullptr && System.Loot.Head == nullptr
        && System.List70.Head == nullptr && System.List98.Head == nullptr
        && System.ListAC.Head == nullptr && System.ListC0.Head == nullptr
        && System.Solars.Head == nullptr;
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
                    // PORT(perf-index): a system with nothing in find_object's blocks
                    // can only answer null (see StarSystem_FindObjectBlocksEmpty).
                    if (StarSystem_FindObjectBlocksEmpty(SysIt->second))
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
