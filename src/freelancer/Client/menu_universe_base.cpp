// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "menu_universe_base.h"

#include "client_base_market.h"
#include "mirror_property_bucket.h" // FLocationBaseRuntime::MissionOffers (+0x00)
#include "menu_sound_arch.h"
#include "sound_config.h"
#include "character_select_equip.h" // SetPlayerBaseIdRetail / SetPlayerFactionScratchRetail
#include "gameflow.h"               // SetGameflowLocationBase (FUN_0043d590)
#include "login_mgr.h"              // WaitForServerResponse (FUN_005a89b0), G_bServerDisconnected
#include "main_command_handler.h"   // SetClientQuitRequested (FUN_005b2c50)
#include "win3d.h"                  // Win3DFindByName (FUN_0059da10)
#include "wincontrol_base.h"        // WinControl::SetProperty (slot 42)
#include "nav_bar.h"                // NavBarDestroy (FUN_00442130)
#include "nn_arrival_text.h"        // NnArrivalTextDestroy (FUN_0045b380)
#include "nn_story_star.h"          // StoryStarForgetLastObjective (FUN_004b9b30)
#include "player_spawn.h"           // GetPlayerConnectionId (DAT_00673344)
#include "room_control.h"
#include "server_connect.h"         // GetLocalServerInterface (DAT_0067ecd0)
#include "scene_render_registry.h"
#include "win3d_renderables.h"

#include "common_misc.h"

#include "DALib.h"
#include "render/rendcomp.h"
#include "common_archetype.h"
#include "client_mesh_load.h" // ArchetypeLoadFromPath (exe-side port helper)
#include "common_csys.h"
#include "common_goods.h"
#include "common_id.h"
#include "common_iface.h"           // ScriptBehavior (the room state's +0x18 bucket)
#include "common_ini.h"
#include "common_server.h"          // IServer
#include "common_universe.h"
#include "dacom.h"

#include <list>
#include <map>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

namespace
{
    // this+0x58 (DAT_00668760) -- raised by the base enter FUN_005588a0 (0x0055896f); while it
    // is set FUN_0043bab0 keeps a music / ambience whose loop flag is clear, and it clears the
    // byte itself (0x0043bb7d). No other writer.
    static bool G_bLocationKeepAudio = false;

    // ------------------------------------------------------------------
    // LocationController registry — FUN_004393b0.
    //
    // Retail keeps a list of per-base records at this+0x04, each carrying the BaseData* and a
    // std::list of four-dword per-room records `{RoomData*, 0, 0, 0}`. Two maps index them:
    // this+0x20 by BaseData->+0x00 (BaseId) and this+0x0c by RoomData->+0x04, which
    // `BaseData::read_Room_block` fills with `CreateID("%X_%s", BaseId, nickname)` — the
    // location id `FUN_0043bab0` is called with. The three spare dwords are per-visit state the
    // rest of the controller writes; nothing in the ported path reads them yet.
    //
    // Rooms live in a std::list, not a vector: the room map stores pointers into it.
    // ------------------------------------------------------------------
    // FUN_004397a0 allocates the base runtime with `operator new(0x4c)` and three sub-object
    // ctors (FUN_0043c0d0 / FUN_0043c310 / FUN_0042abc0), then writes +0x44 = -1 and +0x48 = 0.
    // The two bytes at +0x14 and +0x2c are MirrorDatabase "synchronised" flags: the IClient
    // completion slots set them (FUN_0043aaf0 / FUN_0043a860) while FUN_004397a0 waits on each
    // through FUN_005a89b0; one still clear after the wait ends the base load (quit in single
    // player, disconnect otherwise).
    struct FLocationBaseRuntime
    {
        // +0x00..+0x18 — the MissionVendorProperties cache (FUN_0043c0d0, torn down at
        // 0x00439cda). Its +0x14 byte (bComplete) is what FUN_0043aaf0 sets (Runtime+0x14).
        // Filled by the MirrorDatabase listener LAB_004378f0 -> FUN_0043a960 (upsert) /
        // LAB_004378d0 -> FUN_0043a9f0 (erase): HOLE, the listener is not ported (W4-D), so the
        // list stays empty. Read by NN_MissionVendor (FUN_0048b6e0 / FUN_0048c770).
        TMirrorPropertyBucket<MissionVendorProperties> MissionOffers; // +0x00
        // +0x18..+0x30 — the NewsBroadcastProperties cache (FUN_0043c310; MetaList vftable
        // 0x005cb02c). Its +0x14 byte (Runtime+0x2c) is what FUN_0043a860 sets. Same listener
        // HOLE as above, so the list stays empty. Read by NN_News (FUN_004a4fd0 through
        // FUN_0043a630).
        TMirrorPropertyBucket<NewsBroadcastProperties> News;          // +0x18
        FClientMarket Market;     // +0x30 — FUN_0042abc0, filled by FUN_00437de0 (client_base_market.h)
        unsigned NewsId;          // +0x44 — the randomly picked news item, 0xffffffff = none
        unsigned StartLocationId; // +0x48 — where the base drops the player
    };

    // Retail room record, from FUN_00439d80: +0x00 RoomData*, +0x04 a 0x48-byte state object
    // (three MirrorDatabase flags at +0x14 / +0x2c / +0x44), +0x08 the 0x13ac-byte room runtime
    // FUN_00449260 builds — whose +0x500 is the camera FUN_00438c90 hands to the sound mixer —
    // and +0x0c the visit refcount.
    // The room's MirrorDatabase state (0x48 bytes, operator new at 0x00439e5d): three buckets
    // the server fills through IClient slots 30..37, each with its completion byte. Destroyed by
    // FUN_0043a140 (the three bucket teardowns, last to first) + operator delete.
    struct FLocationRoomState
    {
        TMirrorPropertyBucket<CharacterProperties> Characters;          // +0x00 FUN_0043c550
        TMirrorPropertyBucket<ScriptBehavior> ScriptBehaviors;          // +0x18 FUN_0043c790
        TMirrorPropertyBucket<AmbientScriptProperties> AmbientScripts;  // +0x30 FUN_0043c9d0
    };

    struct FLocationRoomEntry
    {
        RoomData* Room;         // +0x00
        FLocationRoomState* State; // +0x04 FUN_00439d80 (0x00439e9a)
        FRoomRuntime* Runtime;  // +0x08 — FUN_00449260 (Camera* at Runtime+0x500)
        int RefCount;           // +0x0C
    };

    // Retail base record: +0x00 BaseData*, +0x04 runtime, +0x08 refcount, +0x10 the room list.
    struct FLocationBaseEntry
    {
        BaseData* Base;                       // +0x00
        FLocationBaseRuntime* Runtime;        // +0x04
        int RefCount;                         // +0x08
        std::list<FLocationRoomEntry> Rooms;  // +0x10
    };

    static std::list<FLocationBaseEntry> G_LocationBases;              // this+0x04
    static std::map<unsigned, FLocationRoomEntry*> G_LocationRooms;    // this+0x0c
    static std::map<unsigned, FLocationBaseEntry*> G_LocationBaseById; // this+0x20

    // Forward declaration: defined below alongside LoadLocationBase. FUN_00439260 (the
    // logout/dtor teardown) unloads every still-referenced base before dropping the index;
    // ClearClientLocationMaps mirrors that half of it (see below).
    void UnloadLocationBase(unsigned BaseId);
    FLocationBaseEntry* FindLocationBaseEntry(unsigned BaseId);
    FLocationRoomEntry* FindLocationRoomEntry(unsigned LocationId);
    void EnsureRoomRuntime(FLocationRoomEntry* Entry, unsigned LocationId);
    IServer* GetLocationServer();

    // LocationController scalars. this+0x34 is the base the player is in, this+0x38 the
    // location; this+0x59 is raised for the length of a room transition (FUN_0043b6b0).
    static unsigned G_LocationCurrentBase = 0;  // this+0x34
    static unsigned G_LocationCurrentRoom = 0;  // this+0x38
    // this+0x3c / +0x40 -- the base and room records of the current location, set by
    // FUN_0043b6b0 (0x0043b8d1 / 0x0043b919) and cleared with +0x38 whenever the location is
    // left. FUN_0043bbc0 draws through +0x40's runtime.
    static FLocationBaseEntry* G_LocationCurrentBaseEntry = nullptr; // this+0x3c
    static FLocationRoomEntry* G_LocationCurrentRoomEntry = nullptr; // this+0x40
    // this+0x44..+0x50 -- a queued location change (FUN_0043ba90): the location, whether its
    // FUN_0043bab0 audio runs too, the time waited and the time to wait. FUN_00439540 counts
    // +0x4c up and makes the change once it reaches +0x50.
    static unsigned G_LocationPendingRoom = 0;      // this+0x44
    static bool G_bLocationPendingAudio = false;    // this+0x48
    static float G_LocationPendingElapsed = 0.0f;   // this+0x4c
    static float G_LocationPendingDelay = 0.0f;     // this+0x50
    // this+0x54 -- the loaded base's solar object id (CreateID of the nickname of the system
    // object whose "base =" is it), written by FUN_004397a0 (0x00439ae0), read through
    // FUN_0043bc90.
    static unsigned G_LocationBaseSolarId = 0;      // this+0x54
    static bool G_bLocationInTransition = false; // this+0x59 (DAT_00668761)
    // this+0x5a -- zeroed by the LocationController ctor (0x00438ed0), set by IntroFocus::Init
    // at 0x00573919, cleared when the intro base is left. The controller reads it through
    // `this+0x5a` (e.g. 0x004398a4), which the global-address XREF list does not show.
    static bool G_bLocationNoServerNotify = false;  // this+0x5a

    // The controller's destructor FUN_00438fd0, run by the atexit entry LAB_00438de0 that the
    // static initializer LAB_00438dc0 registers after constructing DAT_00668708: FUN_00439260
    // (ClearClientLocationMaps) first, then the base map (+0x20), the room map (+0x0c) and the
    // base list (+0x04) freed. The port's containers above are those three; declared before
    // this object, their destructors run after its own, in the same order as retail's tail.
    struct FLocationControllerTeardown
    {
        ~FLocationControllerTeardown()
        {
            ClearClientLocationMaps(); // 0x00438ffa
        }
    };
    static FLocationControllerTeardown G_LocationControllerTeardown;

    void ApplyLocationAudio(unsigned LocationId)
    {
        // FUN_0043bab0 (thiscall on DAT_00668708, RET 4).
        // 0x0043babc..0x0043bb07: the room map (this+0x0c, FUN_00432240) -- FUN_00439610's
        // lookup inlined: a miss FDUMPs ("Could not find location id", line 0x110) and the
        // record is read through null, as FindLocationRoomEntry's callers do.
        RoomData* const Room = FindLocationRoomEntry(LocationId)->Room;

        // 0x0043bb09..0x0043bb48: the music (+0xcc) and ambience (+0xd0) ID_Strings copied;
        // with this+0x58 clear, one whose loop flag (+0xd4 / +0xd5) is clear is ID_String::clear.
        unsigned MusicId = Room->Music.Id;
        unsigned AmbientId = Room->Ambient.Id;
        if (!G_bLocationKeepAudio)
        {
            if (!Room->bMusicLoop)
                MusicId = 0;
            if (!Room->bAmbientLoop)
                AmbientId = 0;
        }

        // 0x0043bb4a..0x0043bb7a: FUN_004288e0(&music, +0xd4, -1.0), FUN_00428a40(&ambience,
        // +0xd5, -1.0).
        SetClientBackgroundMusic(MusicId, Room->bMusicLoop ? 1u : 0u, -1.f);
        SetClientBackgroundAmbient(AmbientId, Room->bAmbientLoop ? 1u : 0u, -1.f);
        // 0x0043bb7d: this+0x58 = 0.
        G_bLocationKeepAudio = false;

        // 0x0043bb81..0x0043bba5: FUN_00456990 (music_fade_time), then on DAT_0066d2d0
        // FUN_004569b0(-96.0) and FUN_004569d0(0, music_fade_time).
        float FadeSeconds = 0.f;
        GetClientDuckingConfig(nullptr, nullptr, &FadeSeconds);
        SnapClientAudioFade(-96.f);
        SetClientAudioFade(0.f, FadeSeconds);
    }
}

void SetClientLocationNoServerNotify(bool bDisabled)
{
    G_bLocationNoServerNotify = bDisabled;
}

void ApplyClientLocationAudio(unsigned LocationId)
{
    // FUN_0043bab0 (thiscall on DAT_00668708, RET 4) for callers outside the controller.
    ApplyLocationAudio(LocationId);
}

void SetClientLocationKeepAudio()
{
    // FUN_005588a0 0x0055896f: DAT_00668760 (the controller's this+0x58) = 1.
    G_bLocationKeepAudio = true;
}

void BuildClientLocationMaps()
{
    // FUN_004393b0 (thiscall on DAT_00668708; only caller FUN_00558850, paired with the
    // teardown FUN_00439260 in FUN_00558870 -- this function does not clear first).
    // 0x004393d2..0x0043949a: BaseDataList_get() (not null-tested) and its base list (+0x04):
    // per BaseData a record {BaseData*, 0, 0, room list} is built on the stack (FUN_0043bca0),
    // a {RoomData*, 0, 0, 0} record per entry of BaseData +0x28 appended (FUN_0043bd30), and
    // the record copied onto the end of this+0x04 (FUN_0043bd90). No entry is skipped.
    BaseDataList* const BaseList = BaseDataList_get();
    std::list<BaseData*>* const BaseDataItems = BaseList->get_base_data_list();
    for (std::list<BaseData*>::iterator BaseIt = BaseDataItems->begin();
         BaseIt != BaseDataItems->end(); ++BaseIt)
    {
        BaseData* const Base = *BaseIt;
        G_LocationBases.push_back(FLocationBaseEntry());
        FLocationBaseEntry& Entry = G_LocationBases.back();
        Entry.Base = Base;
        Entry.Runtime = nullptr;
        Entry.RefCount = 0;

        for (std::list<RoomData*>::iterator RoomIt = Base->Rooms.begin();
             RoomIt != Base->Rooms.end(); ++RoomIt)
        {
            FLocationRoomEntry RoomEntry;
            RoomEntry.Room = *RoomIt;
            RoomEntry.State = nullptr;
            RoomEntry.Runtime = nullptr;
            RoomEntry.RefCount = 0;
            Entry.Rooms.push_back(RoomEntry);
        }
    }

    // 0x0043949e..0x0043952b: every record of this+0x04 -- {BaseData +0x00, &record} inserted
    // into the base map (this+0x20, FUN_0046d6d0: a unique-key insert, an existing key is
    // kept), then {RoomData +0x04, &room record} for each of its rooms into the room map
    // (this+0x0c, same insert).
    for (std::list<FLocationBaseEntry>::iterator It = G_LocationBases.begin();
         It != G_LocationBases.end(); ++It)
    {
        G_LocationBaseById.insert(
            std::map<unsigned, FLocationBaseEntry*>::value_type(It->Base->BaseId, &(*It)));
        for (std::list<FLocationRoomEntry>::iterator RoomIt = It->Rooms.begin();
             RoomIt != It->Rooms.end(); ++RoomIt)
        {
            G_LocationRooms.insert(std::map<unsigned, FLocationRoomEntry*>::value_type(
                RoomIt->Room->RoomId, &(*RoomIt)));
        }
    }
}

void ClearClientLocationMaps()
{
    // FUN_00439260 (thiscall on DAT_00668708). Per base record (this+0x04 list), in order:
    for (std::list<FLocationBaseEntry>::iterator BaseIt = G_LocationBases.begin();
         BaseIt != G_LocationBases.end(); ++BaseIt)
    {
        // 0x00439280..0x004392c0: every room of the base -- FUN_00439610 on RoomData+0x04 (the
        // location id); a room still referenced (RefCount > 0, JLE) is unloaded
        // (FUN_0043a080) and its count zeroed.
        for (std::list<FLocationRoomEntry>::iterator RoomIt = BaseIt->Rooms.begin();
             RoomIt != BaseIt->Rooms.end(); ++RoomIt)
        {
            unsigned const LocationId = RoomIt->Room->RoomId;
            FLocationRoomEntry* const RoomEntry = FindLocationRoomEntry(LocationId);
            if (RoomEntry->RefCount > 0)
            {
                UnloadClientLocationRoomState(LocationId);
                RoomEntry->RefCount = 0;
            }
        }

        // 0x004392c4..0x00439333: the base itself -- BaseData+0x00 through the base map
        // (inline FUN_00439590, FDUMP at 0x100 on a miss); still referenced -> FUN_00439b50 and
        // the count zeroed.
        unsigned const BaseId = BaseIt->Base->BaseId;
        FLocationBaseEntry* const BaseEntry = FindLocationBaseEntry(BaseId);
        if (BaseEntry->RefCount > 0)
        {
            UnloadLocationBase(BaseId);
            BaseEntry->RefCount = 0;
        }
    }

    // 0x0043934a..0x0043939f: every base record erased (FUN_0043bf50), then the room map
    // (FUN_0043bdf0 on +0x0c) and the base map (FUN_0043bea0 on +0x20) emptied.
    G_LocationBases.clear();
    G_LocationRooms.clear();
    G_LocationBaseById.clear();

    // 0x004393aa: JMP FUN_00442130 -- the "NavBar" window torn down.
    NavBarDestroy();
}

RoomData* LookupClientLocationRoom(unsigned LocationId)
{
    // FUN_0043bab0 head: FUN_00432240 find on this+0xc, then record[0].
    std::map<unsigned, FLocationRoomEntry*>::const_iterator It = G_LocationRooms.find(LocationId);
    if (It == G_LocationRooms.end() || It->second == nullptr)
        return nullptr;
    return It->second->Room;
}

void* LookupClientLocationBase(unsigned BaseId)
{
    // FUN_0043a2b0 (thiscall on DAT_00668708, RET 4): FUN_0052c7a0 lower_bound on the base map
    // (+0x20), the entry's BaseData (node value +0x10, then its +0x00). A miss FDUMPs "Could not
    // find base id" at LocationController.cpp 0x100 (0x0043a306..0x0043a324) and reads through
    // null (0x0043a327..0x0043a329) -- the same inlined body as FUN_00439590.
    return FindLocationBaseEntry(BaseId)->Base;
}

unsigned GetClientLocationBaseCount()
{
    return static_cast<unsigned>(G_LocationBaseById.size());
}

unsigned GetClientLocationRoomCount()
{
    return static_cast<unsigned>(G_LocationRooms.size());
}

// ----------------------------------------------------------------------------------------
// LocationController: base entry (FUN_0043b290 / FUN_004397a0 / FUN_00439590 / FUN_0043b3e0).
// ----------------------------------------------------------------------------------------

namespace
{
    char const* const kLocationControllerCpp =
        "E:\\FL\\Scratch\\Source\\Client\\Gf\\LocationController.cpp";

    // FUN_00439590 — lower_bound on the base map, FDUMP at LocationController.cpp:0x100 on miss.
    FLocationBaseEntry* FindLocationBaseEntry(unsigned BaseId)
    {
        std::map<unsigned, FLocationBaseEntry*>::const_iterator It =
            G_LocationBaseById.find(BaseId);
        // 0x004395c8..0x004395d9: a found node returns its value (+0x10) as it is.
        if (It != G_LocationBaseById.end())
            return It->second;
        FDUMP(0x100001, "%s(%d) : *** ERROR: Could not find base id %d\n",
              kLocationControllerCpp, 0x100, BaseId);
        return nullptr;
    }

    // FUN_00439610 — same shape on the room map, FDUMP at LocationController.cpp:0x110.
    FLocationRoomEntry* FindLocationRoomEntry(unsigned LocationId)
    {
        std::map<unsigned, FLocationRoomEntry*>::const_iterator It =
            G_LocationRooms.find(LocationId);
        // 0x00439648..0x00439659: a found node returns its value (+0x10) as it is.
        if (It != G_LocationRooms.end())
            return It->second;
        FDUMP(0x100001, "%s(%d) : *** ERROR: Could not find location id %d\n",
              kLocationControllerCpp, 0x110, LocationId);
        return nullptr;
    }

    // FUN_00439d80 (thiscall, RET 4) -- load a room: its MirrorDatabase state, the server's
    // location info, the 0x13ac room runtime (FUN_00449260 + FUN_004496b0; Camera* at +0x500,
    // FUN_00556be0), the sync of the state's three buckets, the scene scripts and the NavBar.
    // Callers hand the room record (FUN_00439610's lookup, 0x00439da0..0x00439df7, done by
    // them) and call it only at a zero count, when +0x04 / +0x08 are clear: retail allocates
    // both unconditionally.
    void EnsureRoomRuntime(FLocationRoomEntry* Entry, unsigned LocationId)
    {
        // 0x00439df9..0x00439e5a: the room's base record through the base map, keyed by
        // RoomData +0x00 (FDUMP at 0x100 and a read through null on a miss); its BaseData's id
        // is what FUN_004496b0 gets -- the port's CreateRoomRuntime reads the same RoomData
        // +0x00, so only the lookup (and its miss) is made here.
        unsigned const RoomBaseId = FindLocationBaseEntry(Entry->Room->BaseId)->Base->BaseId;
        (void)RoomBaseId;
        // 0x00439e5d..0x00439eb1: operator new(0x48), the three buckets constructed
        // (FUN_0043c550 / FUN_0043c790 / FUN_0043c9d0), +0x04 = it, and the three completion
        // bytes (+0x14 / +0x2c / +0x44) cleared.
        Entry->State = new FLocationRoomState();
        Entry->State->Characters.bComplete = 0;
        Entry->State->ScriptBehaviors.bComplete = 0;
        Entry->State->AmbientScripts.bComplete = 0;
        // 0x00439eb5..0x00439edd: IServer slot 40 LocationInfoRequest(Location, session, true).
        IServer* const Server = GetLocationServer();
        if (Server != nullptr)
            Server->LocationInfoRequest(LocationId, GetPlayerConnectionId(), true);
        Entry->Runtime = CreateRoomRuntime(Entry->Room);
        // 0x00439f1f..0x0043a014: with +0x5a clear, the three buckets of the room state are
        // synchronized in turn (+0x14, +0x2c, +0x44): FUN_005a89b0(&flag, 5.0f, 60.0f); a result
        // other than 1 FDUMPs "Synchronization failed" (line 0x108) and clears the flag. A flag
        // still clear -> 0x0043a016: single player -> FUN_005b2c50 (DAT_0067ecbd = 1), otherwise
        // DAT_0067ecbe = 1; either way the function returns without the scenes. A set flag stays
        // set (the IClient completion slots wrote it).
        if (!G_bLocationNoServerNotify)
        {
            unsigned char* const SyncFlags[3] = {
                &Entry->State->Characters.bComplete,
                &Entry->State->ScriptBehaviors.bComplete,
                &Entry->State->AmbientScripts.bComplete,
            };
            int SyncIndex = 0;
            for (SyncIndex = 0; SyncIndex < 3; ++SyncIndex)
            {
                unsigned char* const Flag = SyncFlags[SyncIndex];
                if (WaitForServerResponse(Flag, 5.0f, 60.0f) != 1)
                {
                    FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kLocationControllerCpp, 0x108,
                          "Synchronization failed\n");
                    *Flag = 0;
                }
                if (*Flag == 0)
                {
                    if (SinglePlayer())
                        SetClientQuitRequested(true); // FUN_005b2c50
                    else
                        G_bServerDisconnected = true; // DAT_0067ecbe
                    return;
                }
            }
        }
        // 0x0043a055: FUN_0044aca0 -- the room's scene scripts. 0x0043a05a: FUN_00442060 -- the
        // "NavBar" window found or created and told the room was entered.
        RoomControlPlayScenes(Entry->Runtime);
        NavBarEnsure();
    }

    // FUN_004397a0 (thiscall, RET 4) — load a base: its runtime, RoomData::load on every room,
    // and (with +0x5a clear) the server's base info, the MirrorDatabase sync of the mission and
    // news buckets, the random news item, the market, the base's solar object id (+0x54) and
    // its visit flags.
    void LoadLocationBase(FLocationBaseEntry* Entry, unsigned BaseId)
    {
        // 0x004397c8..0x00439823: the base map lookup (FDUMP at 0x100, then a read through
        // null) -- the callers' FindLocationBaseEntry, whose result they hand in.
        // 0x00439825..0x00439882: operator new(0x4c), the three sub-object ctors, +0x04 = it
        // (unconditionally: the callers load only at a zero count, after FUN_00439b50 cleared
        // +0x04), +0x44 = -1, +0x48 = 0.
        Entry->Runtime = new FLocationBaseRuntime(); // buckets' ctors clear +0x14 / +0x2c
        Entry->Runtime->NewsId = 0xffffffffu;  // puVar3[0x11] = 0xffffffff
        Entry->Runtime->StartLocationId = 0;   // runtime+0x48 = 0

        // 0x00439889..0x004398a2: RoomData::load on every room of the base.
        for (std::list<FLocationRoomEntry>::iterator It = Entry->Rooms.begin();
             It != Entry->Rooms.end(); ++It)
        {
            It->Room->load();
        }

        // 0x004398a4..0x004398a9: +0x5a set -> return (nothing below runs).
        if (G_bLocationNoServerNotify)
            return;

        // 0x004398af..0x004398d9: both completion bytes cleared, then IServer slot 39
        // BaseInfoRequest(Base, session, true) when there is a server.
        Entry->Runtime->MissionOffers.bComplete = 0;
        Entry->Runtime->News.bComplete = 0;
        IServer* const Server = static_cast<IServer*>(GetLocalServerInterface());
        if (Server != nullptr)
            Server->BaseInfoRequest(BaseId, GetPlayerConnectionId(), true);

        // 0x004398df..0x00439993: the two buckets synchronized in turn (+0x14, then +0x2c):
        // FUN_005a89b0(&flag, 5.0f, 60.0f); a result other than 1 FDUMPs "Synchronization
        // failed" (line 0x108) and clears the flag; a flag still clear -> single player:
        // FUN_005b2c50 (DAT_0067ecbd = 1), otherwise DAT_0067ecbe = 1; return.
        unsigned char* const SyncFlags[2] = {
            &Entry->Runtime->MissionOffers.bComplete,
            &Entry->Runtime->News.bComplete,
        };
        int SyncIndex = 0;
        for (SyncIndex = 0; SyncIndex < 2; ++SyncIndex)
        {
            unsigned char* const Flag = SyncFlags[SyncIndex];
            if (WaitForServerResponse(Flag, 5.0f, 60.0f) != 1)
            {
                FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kLocationControllerCpp, 0x108,
                      "Synchronization failed\n");
                *Flag = 0;
            }
            if (*Flag == 0)
            {
                if (SinglePlayer())
                    SetClientQuitRequested(true); // FUN_005b2c50
                else
                    G_bServerDisconnected = true; // DAT_0067ecbe
                return;
            }
        }

        // 0x00439998..0x00439a61: +0x44 = the key (element +0x00) of a random entry of the news
        // bucket: the entries counted with FUN_0043ce90 (forward traverse), Pick =
        // rand() * Count / 32768 (IMUL, CDQ / AND 0x7fff / SAR 15: a truncating signed divide),
        // then a fresh walk to entry Pick. No entries -> -1.
        TMirrorPropertyBucket<NewsBroadcastProperties>& News = Entry->Runtime->News;
        MetaNode<NewsBroadcastProperties>* Node = nullptr;
        int NewsCount = 0;
        while (News.List.traverse(Node, true))
            ++NewsCount;
        if (NewsCount > 0)
        {
            int const Pick = (rand() * NewsCount) / 32768;
            MetaNode<NewsBroadcastProperties>* Picked = nullptr;
            int Index = 0;
            Node = nullptr;
            while (News.List.traverse(Node, true))
            {
                if (Index == Pick)
                {
                    Picked = Node;
                    break;
                }
                ++Index;
            }
            // Retail reads the element through the (never null: Pick < Count) pick.
            Entry->Runtime->NewsId = static_cast<unsigned>(Picked->Data->Key);
        }
        else
        {
            Entry->Runtime->NewsId = 0xffffffffu;
        }

        // 0x00439a68..0x00439a7b: the runtime's market (+0x30) from the base's BaseData, with
        // its price variance (BaseData +0x08) and no explicit base (the current one,
        // FUN_0043b630). FUN_00437de0, client_base_market.cpp.
        ClientMarketFill(&Entry->Runtime->Market, Entry->Base->PriceVariance, nullptr);

        // 0x00439a80..0x00439ad8 / 0x00439b31: with a server, the player's system
        // (Universe::get_system(FUN_004c4830()), not null-tested): the first object (slot 15
        // count, slot 16 fetch into a FUN_0043bc60-initialized record) whose base (+0x88) is
        // this one -> CreateID(its nickname); none (or no server) -> 0.
        unsigned SolarId = 0;
        if (static_cast<IServer*>(GetLocalServerInterface()) != nullptr)
        {
            Universe::ISystem const* const System =
                Universe::get_system(GetPlayerStarSystemIdRetail());
            Universe::FSystemObject Object;
            Object.Nickname.Length = 0;
            Object.Nickname.Data[0] = '\0';
            Object.Reputation.Length = 0;
            Object.Reputation.Data[0] = '\0';
            Object.NextRing.Length = 0;
            Object.NextRing.Data[0] = '\0';
            Object.PrevRing.Length = 0;
            Object.PrevRing.Data[0] = '\0';
            unsigned const ObjectCount = (unsigned)System->GetObjectCount();
            unsigned ObjectIndex = 0;
            for (ObjectIndex = 0; ObjectIndex < ObjectCount; ++ObjectIndex)
            {
                if (!System->GetSystemObject(ObjectIndex, &Object))
                    continue;
                if (Object.BaseId == BaseId)
                {
                    SolarId = CreateID(Object.Nickname.Data);
                    break;
                }
            }
        }
        // 0x00439ada..0x00439b0e: +0x54 = it; non-zero -> its visit flags (FUN_004c4da0 into a
        // zeroed byte, the result ignored) | 0x1e stored back with the server told
        // (FUN_004c4e00(id, flags, 1)).
        G_LocationBaseSolarId = SolarId;
        if (SolarId != 0)
        {
            unsigned char VisitFlags = 0;
            GetPlayerVisitFlagsRetail(SolarId, &VisitFlags, 0, 0); // 0x00439af4
            VisitFlags = static_cast<unsigned char>(VisitFlags | 0x1e);
            SetPlayerVisitFlagsRetail(SolarId, VisitFlags, true);
        }

        // 0x00439b11: FUN_004b9b30 -- the story star forgets its last objective.
        StoryStarForgetLastObjective();
    }

    // DAT_0067ecd0, the IServer every LocationController notification goes to. The
    // notifications are gated on this+0x5a being clear and the server existing (every site:
    // `CMP [ESI+0x5a],0 / JNZ; MOV ECX,[DAT_0067ecd0] / TEST / JZ`); the session id is
    // DAT_00673344.
    IServer* GetLocationServer()
    {
        if (G_bLocationNoServerNotify)
            return nullptr;
        return static_cast<IServer*>(GetLocalServerInterface());
    }

    // FUN_00439b50 (thiscall, RET 4): the base entry looked up again (inline FUN_00439590,
    // FDUMP at 0x100 on a miss, then read through null as retail does); the server told the
    // base's info is no longer wanted (IServer slot 39 BaseInfoRequest(Base, session, false));
    // RoomData::unload on every room of the base (the room runtimes are the rooms' own
    // business -- FUN_0043a080 / FUN_00439690 drop them when their count reaches 0); the base
    // runtime destroyed (FUN_00439c10 + operator delete) and +0x04 cleared.
    void UnloadLocationBase(unsigned BaseId)
    {
        FLocationBaseEntry* const Found = FindLocationBaseEntry(BaseId);
        IServer* const Server = GetLocationServer();
        if (Server != nullptr)
            Server->BaseInfoRequest(BaseId, GetPlayerConnectionId(), false);
        for (std::list<FLocationRoomEntry>::iterator It = Found->Rooms.begin();
             It != Found->Rooms.end(); ++It)
        {
            It->Room->unload();
        }
        // 0x00439be3..0x00439bfa: a non-null runtime -- FUN_00439c10 + operator delete; +0x04 = 0. FUN_00439c10 is the runtime's
        // destructor: the market map (+0x30, FUN_00437b70 = std::map dtor), then the news bucket
        // (+0x18) and the mission-offer bucket (+0x00), each the inlined
        // ~TMirrorPropertyBucket -- the implicit member destructors in reverse order.
        delete Found->Runtime;
        Found->Runtime = nullptr;
    }
}

// ----------------------------------------------------------------------------------------
// LocationController: small field accessors (FUN_0043a460/a510/a5a0/a630/a860/a8d0/aaf0/ab60).
// Retail resolves the base/room record through the same this+0x10 / this+0x24 map lookups as
// FindLocationBaseEntry/FindLocationRoomEntry above (COMDAT-folded onto the same map-find
// helper), then reads or writes one field of the record. The four that touch
// FLocationBaseRuntime's already-modeled fields (StartLocationId +0x48, the two buckets'
// bComplete bytes +0x14 / +0x2c, the buckets themselves +0x00 / +0x18, Market +0x30) and the
// two raw Runtime/State pointer getters port faithfully; retail getters that reach into
// offsets the port never allocated (the room State struct's own 0x48-byte body) are left to the
// simplified functions documented
// where they're declared unprocessed in decomp-tracker.
// ----------------------------------------------------------------------------------------

void* GetClientLocationBaseRuntime(unsigned BaseId)
{
    // FUN_0043a8d0 (thiscall, RET 4) -- the base map (FUN_0052c7a0 lower_bound + key test), the
    // record's +0x04 runtime. A miss FDUMPs (line 0x100) and reads +0x04 through null
    // (0x0043a948..0x0043a94a) -- the same read is made here. Opaque because
    // FLocationBaseRuntime is a port-local type.
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    return static_cast<void*>(Entry->Runtime);
}

TMirrorPropertyBucket<MissionVendorProperties>* GetClientLocationBaseMissionOffers(unsigned BaseId)
{
    // FUN_0043a8d0 as NN_MissionVendor uses it: the runtime pointer is the bucket (its +0x00).
    // Retail's miss path FDUMPs (LocationController.cpp line 0x100) and reads through null.
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    return &Entry->Runtime->MissionOffers;
}

TMirrorPropertyBucket<NewsBroadcastProperties>* GetClientLocationBaseNews(unsigned BaseId)
{
    // FUN_0043a630 (thiscall, RET 4): the base map (this+0x20, FUN_0052c7a0 lower_bound + key
    // test), record +0x04 (the runtime), +0x18. A miss FDUMPs (LocationController.cpp line
    // 0x100) and reads through null (0x0043a6ae..0x0043a6b0) -- the same read is made here.
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    return &Entry->Runtime->News;
}

FClientMarket* GetClientLocationBaseMarket(unsigned BaseId)
{
    // FUN_0043a510 (thiscall, RET 4): the base map (FUN_0052c7a0 lower_bound + key test, FDUMP
    // at LocationController.cpp:0x100 on a miss, then a read through the null entry at
    // 0x0043a58e..0x0043a590 -- the same read is made here), record +0x04 (the runtime), +0x30.
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    return &Entry->Runtime->Market;
}

BaseData* GetClientCurrentLocationBaseData()
{
    // FUN_0043b630 (thiscall): the same lookup on this+0x34 (FDUMP at 0x100 and a read through
    // null on a miss, 0x0043b6a4..0x0043b6a6), record +0x00.
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(G_LocationCurrentBase);
    return Entry->Base;
}

void SetClientLocationBaseStartLocation(unsigned BaseId, unsigned LocationId)
{
    // FUN_0043a5a0 (thiscall, RET 8) -- the base map (miss: FDUMP line 0x100, then +0x04 read
    // through null, 0x0043a611..0x0043a613); a non-null runtime gets +0x48 = LocationId, a null
    // one is skipped (0x0043a616..0x0043a61e).
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    if (Entry->Runtime != nullptr)
        Entry->Runtime->StartLocationId = LocationId;
}

void SetClientLocationBaseMissionListComplete(unsigned BaseId)
{
    // FUN_0043aaf0 (RET 4) -- Runtime+0x14 = 1, the MissionVendorProperties bucket's bComplete.
    // Caller: IClient slot 40 (LAB_00437990, ch2 0x10 GFCOMPLETEMISSIONCOMPUTERLIST).
    // 0x0043ab18..0x0043ab4e: a miss FDUMPs (FUN_00439590's message, line 0x100) and reads the
    // entry through null (EAX = 0 at 0x0043ab45) -- no guard; only a null runtime is skipped.
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    if (Entry->Runtime != nullptr)
        Entry->Runtime->MissionOffers.bComplete = 1;
}

void UpsertClientLocationBaseMissionOffer(MissionVendorProperties const& Offer)
{
    // FUN_0043a960 (RET 4) -- the base of Offer +0x04 (FUN_0052c7a0 lower_bound on this+0x20;
    // a miss FDUMPs line 0x100 and reads through null, 0x0043a9b0..0x0043a9d3); with a runtime,
    // its +0x00 bucket's FUN_0043c1b0. Caller: IClient slot 39 (FUN_004378f0).
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(Offer.BaseId);
    if (Entry->Runtime != nullptr)
        Entry->Runtime->MissionOffers.Upsert(Offer);
}

void EraseClientLocationBaseMissionOffer(unsigned BaseId, unsigned long Key)
{
    // FUN_0043a9f0 (RET 8) -- the base (FUN_00432240, same miss path); with a runtime, the
    // +0x00 bucket's element of that key erased (inlined MirrorDatabase.h 0xf6). Caller: IClient
    // slot 38 (LAB_004378d0).
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    if (Entry->Runtime != nullptr)
        Entry->Runtime->MissionOffers.Erase(Key);
}

void UpsertClientLocationBaseNews(NewsBroadcastProperties const& News)
{
    // FUN_0043a6c0 (RET 4) -- as FUN_0043a960 on the +0x18 bucket (FUN_0043c3f0). Caller:
    // IClient slot 44 (FUN_00437a70).
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(News.BaseId);
    if (Entry->Runtime != nullptr)
        Entry->Runtime->News.Upsert(News);
}

void EraseClientLocationBaseNews(unsigned BaseId, unsigned long Key)
{
    // FUN_0043a750 (RET 8) -- as FUN_0043a9f0 on the +0x18 bucket. Caller: IClient slot 43
    // (LAB_00437a50).
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    if (Entry->Runtime != nullptr)
        Entry->Runtime->News.Erase(Key);
}

void SetClientLocationBaseNewsListComplete(unsigned BaseId)
{
    // FUN_0043a860 (RET 4) -- Runtime+0x2c = 1, the NewsBroadcastProperties bucket's bComplete.
    // Caller: IClient slot 45 (LAB_00437b10, ch2 0x0e GFCOMPLETENEWSBROADCASTLIST). Same shape
    // as FUN_0043aaf0 (0x0043a888..0x0043a8b7).
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    if (Entry->Runtime != nullptr)
        Entry->Runtime->News.bComplete = 1;
}

void* GetClientLocationRoomState(unsigned LocationId)
{
    // FUN_0043ab60 (RET 4) — room entry->State (+0x04). A miss FDUMPs line 0x110 and reads
    // through null (0x0043abd8..0x0043abda) -- no guard.
    FLocationRoomEntry* const Entry = FindLocationRoomEntry(LocationId);
    return static_cast<void*>(Entry->State);
}

namespace
{
    // FUN_0043ab60's shape with the typed state: the room entry (FUN_00432240 / FUN_0052c7a0
    // on +0x0c; FDUMP at 0x110 and a read through null on a miss), its +0x04.
    FLocationRoomState* FindClientLocationRoomState(unsigned LocationId)
    {
        FLocationRoomEntry* const Entry = FindLocationRoomEntry(LocationId);
        return Entry->State;
    }
}

void UpsertClientLocationRoomCharacter(CharacterProperties const& Character)
{
    // FUN_0043abf0 (RET 4). The room of Character +0x08: with a state, its +0x00 bucket's
    // FUN_0043c630 (upsert). Then the room again (FUN_0041bd10, same miss path): with a room
    // runtime (+0x08), RoomControl FUN_00449e60(Character).
    FLocationRoomState* const State = FindClientLocationRoomState(Character.RoomId);
    if (State != nullptr)
        State->Characters.Upsert(Character);
    FLocationRoomEntry* const Entry = FindLocationRoomEntry(Character.RoomId);
    if (Entry->Runtime != nullptr)
        RoomControlUpdateCharacter(Entry->Runtime, Character); // 0x0043accd
}

void SetClientLocationRoomCharactersComplete(unsigned LocationId)
{
    // FUN_0043ace0 (RET 4) -- state +0x14 = 1 when there is a state.
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    if (State != nullptr)
        State->Characters.bComplete = 1;
}

void UpsertClientLocationRoomScriptBehavior(ScriptBehavior const& Behavior)
{
    // FUN_0043ade0 (RET 4) -- the room of Behavior +0x04; with a state, its +0x18 bucket's
    // FUN_0043c870 (upsert).
    FLocationRoomState* const State = FindClientLocationRoomState(Behavior.Field04);
    if (State != nullptr)
        State->ScriptBehaviors.Upsert(Behavior);
}

void EraseClientLocationRoomScriptBehavior(unsigned LocationId, unsigned long Key)
{
    // FUN_0043ae70 (RET 8) -- with a state, the +0x18 bucket's element of that key erased
    // (inlined MirrorDatabase.h 0xf6).
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    if (State != nullptr)
        State->ScriptBehaviors.Erase(Key);
}

void SetClientLocationRoomScriptBehaviorsComplete(unsigned LocationId)
{
    // FUN_0043af80 (RET 4) -- state +0x2c = 1 when there is a state.
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    if (State != nullptr)
        State->ScriptBehaviors.bComplete = 1;
}

void UpsertClientLocationRoomAmbientScript(AmbientScriptProperties const& Script)
{
    // FUN_0043b080 (RET 4) -- the room of Script +0x04; with a state, its +0x30 bucket's
    // FUN_0043c9f0 (upsert).
    FLocationRoomState* const State = FindClientLocationRoomState(Script.RoomId);
    if (State != nullptr)
        State->AmbientScripts.Upsert(Script);
}

void EraseClientLocationRoomAmbientScript(unsigned LocationId, unsigned long Key)
{
    // FUN_0043b110 (RET 8) -- with a state, the +0x30 bucket's element of that key erased.
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    if (State != nullptr)
        State->AmbientScripts.Erase(Key);
}

void SetClientLocationRoomAmbientScriptsComplete(unsigned LocationId)
{
    // FUN_0043b220 (RET 4) -- state +0x44 = 1 when there is a state.
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    if (State != nullptr)
        State->AmbientScripts.bComplete = 1;
}

TMirrorPropertyBucket<CharacterProperties>* GetClientLocationRoomCharacters(unsigned LocationId)
{
    // FUN_0043ab60 again, typed: the state's +0x00 bucket is the state itself. Caller:
    // RoomControl FUN_00449f40 (0x00449f68).
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    return &State->Characters;
}

TMirrorPropertyBucket<ScriptBehavior>* GetClientLocationRoomScriptBehaviors(unsigned LocationId)
{
    // FUN_0043ad50 (RET 4) -- FUN_0043aff0's body with + 0x18: the room entry (FDUMP at 0x110
    // and a read through null on a miss), its state + 0x18. No state check. Caller: RoomControl
    // FUN_00449f40 (0x00449f7c).
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    return &State->ScriptBehaviors;
}

TMirrorPropertyBucket<AmbientScriptProperties>* GetClientLocationRoomAmbientScripts(
    unsigned LocationId)
{
    // FUN_0043aff0 (RET 4) -- the room entry (FUN_0052c7a0 on +0x0c; FDUMP at 0x110 and a read
    // through null on a miss, 0x0043b050..0x0043b07c), then its state (+0x04) + 0x30. No state
    // check. Caller: RoomControl FUN_0044a540 (0x0044a66d).
    FLocationRoomState* const State = FindClientLocationRoomState(LocationId);
    return &State->AmbientScripts;
}

bool IsClientLocationRoomLoaded(unsigned LocationId)
{
    // FUN_0043a340 (RET 4) -- the room entry (FUN_0052c7a0 on +0x0c; FDUMP at 0x110 and a read
    // through the end node on a miss, as FindLocationRoomEntry's callers do) and its RoomData's
    // +0x08 byte (RoomData::load's bLoaded, Common .text:06277c53).
    FLocationRoomEntry* const Entry = FindLocationRoomEntry(LocationId);
    return Entry->Room->bLoaded;
}

void* GetClientCurrentLocationRuntime()
{
    // FUN_0043a460 (thiscall): this+0x38 with its low 30 bits clear -> 0 (0x0043a46b..0x0043a471).
    // Otherwise the room map is searched with the whole +0x38 value (FUN_0052c7a0 lower_bound +
    // key test, 0x0043a474..0x0043a4af) and the record's +0x08 runtime returned; a miss FDUMPs
    // (line 0x110) and reads +0x08 through null (0x0043a4e1..0x0043a4e3).
    if ((G_LocationCurrentRoom & 0x3fffffffu) == 0)
        return nullptr;
    FLocationRoomEntry* const Entry = FindLocationRoomEntry(G_LocationCurrentRoom);
    return static_cast<void*>(Entry->Runtime);
}

void UnloadClientLocationRoomState(unsigned LocationId)
{
    // FUN_0043a080 (thiscall, RET 4) -- per-location unload. The room entry (inline
    // FUN_00439610, FDUMP at 0x110 on a miss, then read through null as retail does); the server
    // told the location's info is no longer wanted (IServer slot 40
    // LocationInfoRequest(Location, session, false)); the 0x13ac runtime at +0x08 released
    // through its slot 0 (deleting dtor) and +0x08 cleared; the 0x48-byte state at +0x04
    // destroyed (FUN_0043a140 + operator delete) and +0x04 cleared.
    FLocationRoomEntry* Entry = FindLocationRoomEntry(LocationId);
    IServer* const Server = GetLocationServer();
    if (Server != nullptr)
        Server->LocationInfoRequest(LocationId, GetPlayerConnectionId(), false);

    if (Entry->Runtime != nullptr)
        DestroyRoomRuntime(Entry->Runtime);
    Entry->Runtime = nullptr;
    // 0x0043a105..0x0043a124: a non-null state -- FUN_0043a140 and operator delete.
    delete Entry->State;
    Entry->State = nullptr;
}

void* GetClientRoomCamera()
{
    // FUN_00438c90 (cdecl on DAT_00668708's fields): +0x38 (DAT_00668740) with its low 30 bits
    // clear -> 0. Otherwise the room map (DAT_00668714) is searched with the whole value
    // (FUN_00432240; a miss FDUMPs line 0x110 and reads +0x08 through null,
    // 0x00438cc8..0x00438cee); a null runtime -> 0, else the runtime's +0x500 (the camera).
    if ((G_LocationCurrentRoom & 0x3fffffffu) == 0)
        return nullptr;
    FLocationRoomEntry* const Entry = FindLocationRoomEntry(G_LocationCurrentRoom);
    if (Entry->Runtime == nullptr)
        return nullptr;
    return GetRoomRuntimeCamera(Entry->Runtime);
}

namespace
{
    // FUN_00439220 (and the same code inline at 0x0043b496 / 0x0043ba4b): FUN_0059da10
    // (Win3DFindByName) "NN_Inventory"; found and its slot 2 (+0x08, AsWinControl) non-null ->
    // slot 2 again and slot 42 (+0xa8, SetProperty) of that answer with (0x4b, 0, 0). NN_Inventory
    // handles 0x4b in its default case (byte table 0x0047acdc[0x38] = 0x0b = caseD_14): the
    // ship-dealer reselect block of FUN_00479d10.
    void NotifyInventoryLocationLeft()
    {
        Win3D* const Inventory = Win3DFindByName("NN_Inventory");
        if (Inventory == nullptr)
            return;
        if (Inventory->AsWinControl() == nullptr)
            return;
        Inventory->AsWinControl()->SetProperty(0x4b, 0, 0);
    }

    // FUN_0043b970 (thiscall, RET 4) -- leave the current location while the base changes.
    // Location is the caller's +0x38 (0x0043b7a5); the body re-reads +0x38 itself.
    void LeaveClientLocationRoom(unsigned Location)
    {
        // 0x0043b978: FUN_0045b380 -- destroy the NN_ArrivalText window (DAT_0066d398).
        NnArrivalTextDestroy();
        unsigned const Current = G_LocationCurrentRoom;
        // 0x0043b980..0x0043b9ce: the room entry, inline FUN_00439610 (FDUMP at 0x110).
        FLocationRoomEntry* const Entry = FindLocationRoomEntry(Current);
        Entry->RefCount -= 1;
        if (Entry->RefCount == 0)
        {
            // 0x0043b9d3..0x0043ba24: FUN_00439610 again, IServer slot 40
            // LocationInfoRequest(Current, session, false), the runtime's deleting dtor, the
            // state's FUN_0043a140 + delete.
            FLocationRoomEntry* const Again = FindLocationRoomEntry(Current);
            IServer* const Server = GetLocationServer();
            if (Server != nullptr)
                Server->LocationInfoRequest(Current, GetPlayerConnectionId(), false);
            if (Again->Runtime != nullptr)
                DestroyRoomRuntime(Again->Runtime);
            Again->Runtime = nullptr;
            // FUN_0043a140 + operator delete on a non-null state.
            delete Again->State;
            Again->State = nullptr;
        }
        // 0x0043ba28..0x0043ba45: IServer slot 38 LocationExit(Location, session) -- the
        // argument, not +0x38.
        IServer* const Server = GetLocationServer();
        if (Server != nullptr)
            Server->LocationExit(Location, GetPlayerConnectionId());
        G_LocationCurrentRoom = 0;
        G_LocationCurrentBaseEntry = nullptr;
        G_LocationCurrentRoomEntry = nullptr;
        NotifyInventoryLocationLeft();
    }

    // FUN_00439690 (thiscall, RET 4) -- drop one reference to Location; at 0 the server is told
    // (IServer slot 40 LocationInfoRequest(Location, session, false)) and the runtime and state
    // are released, as FUN_0043a080 does.
    void ReleaseClientLocationRoom(unsigned Location)
    {
        FLocationRoomEntry* const Entry = FindLocationRoomEntry(Location);
        Entry->RefCount -= 1;
        if (Entry->RefCount != 0)
            return;
        FLocationRoomEntry* const Again = FindLocationRoomEntry(Location);
        IServer* const Server = GetLocationServer();
        if (Server != nullptr)
            Server->LocationInfoRequest(Location, GetPlayerConnectionId(), false);
        if (Again->Runtime != nullptr)
            DestroyRoomRuntime(Again->Runtime);
        Again->Runtime = nullptr;
        // FUN_0043a140 + operator delete on a non-null state.
        delete Again->State;
        Again->State = nullptr;
    }

    // The base id of the base Location belongs to, through Common's unloaded room data
    // (BaseDataList::get_unloaded_room_data, RoomData+0x00 = base id) and the base map
    // (FUN_00439590, BaseData+0x00): the pattern FUN_0043b6b0 inlines at 0x0043b6c5..0x0043b6e8
    // and 0x0043b6f9..0x0043b77d. 0 when Location is 0; an unknown room FDUMPs ("Could not
    // find location %d", line 0x120) and answers 0.
    unsigned LocationBaseOf(unsigned Location)
    {
        if (Location == 0)
            return 0;
        RoomData* const Room = BaseDataList_get()->get_unloaded_room_data(Location);
        if (Room == nullptr)
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: Could not find location %d\n",
                  kLocationControllerCpp, 0x120, Location);
            return 0;
        }
        FLocationBaseEntry* const Entry = FindLocationBaseEntry(Room->BaseId);
        if (Entry == nullptr)
            return 0;
        return Entry->Base->BaseId;
    }
}

void ResetClientLocationController()
{
    // FUN_00439200: +0x34..+0x44 = 0, byte +0x48 = 0, +0x4c = +0x50 = 0, byte +0x59 = 0.
    G_LocationCurrentBase = 0;
    G_LocationCurrentRoom = 0;
    G_LocationCurrentBaseEntry = nullptr;
    G_LocationCurrentRoomEntry = nullptr;
    G_LocationPendingRoom = 0;
    G_bLocationPendingAudio = false;
    G_LocationPendingElapsed = 0.0f;
    G_LocationPendingDelay = 0.0f;
    G_bLocationInTransition = false;
}

void TickClientLocationController(float DeltaSeconds)
{
    // FUN_00439540 (thiscall, RET 4).
    unsigned const Pending = G_LocationPendingRoom;
    if ((Pending & 0x3fffffffu) == 0)
        return;
    // 0x0043954e..0x00439560: +0x4c += Dt (FST keeps it), FCOMP +0x50, TEST AH,0x01 / JNZ --
    // C0 set means +0x4c < +0x50, which returns; equal, greater (and unordered, C0 set too,
    // returns) fall through.
    G_LocationPendingElapsed += DeltaSeconds;
    if (G_LocationPendingElapsed < G_LocationPendingDelay ||
        G_LocationPendingElapsed != G_LocationPendingElapsed ||
        G_LocationPendingDelay != G_LocationPendingDelay)
    {
        return;
    }
    ChangeClientLocation(Pending);
    // 0x0043956a..0x00439577: +0x48 set -> FUN_0043bab0(+0x44), re-read after the change.
    if (G_bLocationPendingAudio)
        ApplyLocationAudio(G_LocationPendingRoom);
    G_LocationPendingRoom = 0;
    G_bLocationPendingAudio = false;
}

void QueueClientLocationChange(unsigned Location, float Delay, bool bWithAudio)
{
    // FUN_0043ba90 (thiscall, RET 0xc): +0x44 = Location, +0x48 = bWithAudio, +0x4c = 0,
    // +0x50 = Delay. Callers: 0x0044d3dc, 0x0045551b, 0x0045552f (not ported).
    G_LocationPendingRoom = Location;
    G_bLocationPendingAudio = bWithAudio;
    G_LocationPendingElapsed = 0.0f;
    G_LocationPendingDelay = Delay;
}

void EnterClientLocationBase(unsigned BaseId)
{
    // FUN_0043b290 (thiscall, RET 4).
    G_LocationCurrentBase = BaseId;
    IServer* const Server = GetLocationServer();
    if (Server != nullptr)
        Server->BaseEnter(BaseId, GetPlayerConnectionId());

    // 0x0043b2c1..0x0043b2da: FUN_00439590 (FDUMP at 0x100), load at a zero count, count + 1.
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    if (Entry->RefCount == 0)
        LoadLocationBase(Entry, BaseId);
    Entry->RefCount += 1;

    // 0x0043b2dd..0x0043b33a: the base map again (inline lookup, FDUMP at 0x100 and a read
    // through null on a miss) -> runtime (+0x04) -> +0x48, the start location the server set.
    unsigned StartLocation = FindLocationBaseEntry(BaseId)->Runtime->StartLocationId;
    if (StartLocation == 0)
    {
        // 0x0043b341..0x0043b36b: with a server, the WARNING at line 0x405.
        if (static_cast<IServer*>(GetLocalServerInterface()) != nullptr)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kLocationControllerCpp, 0x405,
                  "Failed to get start location\n");
        }
        // 0x0043b36e..0x0043b3ba: BaseData's own start room (BaseData+0x04).
        StartLocation = FindLocationBaseEntry(BaseId)->Base->StartRoomId;
    }

    // 0x0043b3bd..0x0043b3c8: FUN_0043bab0 then FUN_0043b6b0 on the start location.
    ApplyLocationAudio(StartLocation);
    ChangeClientLocation(StartLocation);
}

void SetClientLocationBase(unsigned BaseId)
{
    // FUN_0043b570 (thiscall, RET 4): the base half of FUN_0043b290 without the start location
    // -- +0x34, IServer slot 35 BaseEnter, the base map lookup (lower_bound, FDUMP at 0x100 and
    // a read through null on a miss), load at a zero count, count + 1.
    G_LocationCurrentBase = BaseId;
    IServer* const Server = GetLocationServer();
    if (Server != nullptr)
        Server->BaseEnter(BaseId, GetPlayerConnectionId());
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(BaseId);
    if (Entry->RefCount == 0)
        LoadLocationBase(Entry, BaseId);
    Entry->RefCount += 1;
}

void ChangeClientLocation(unsigned Location)
{
    // FUN_0043b6b0 (thiscall, RET 4).
    G_bLocationInTransition = true;

    // 0x0043b6b8..0x0043b77d: the base the current and the new location belong to. The current
    // one is tested whole (TEST EDI,EDI), not masked.
    unsigned const OldBase = LocationBaseOf(G_LocationCurrentRoom);
    unsigned const NewBase = LocationBaseOf(Location);

    if (OldBase != 0 && NewBase != 0 && OldBase != NewBase)
    {
        // 0x0043b79b..0x0043b855: a base change.
        if ((G_LocationCurrentRoom & 0x3fffffffu) != 0)
            LeaveClientLocationRoom(G_LocationCurrentRoom);
        FLocationBaseEntry* const OldEntry = FindLocationBaseEntry(OldBase);
        OldEntry->RefCount -= 1;
        if (OldEntry->RefCount == 0)
            UnloadLocationBase(OldBase);
        IServer* Server = GetLocationServer();
        if (Server != nullptr)
            Server->BaseExit(OldBase, GetPlayerConnectionId());
        G_LocationCurrentBase = 0;
        NavBarDestroy(); // 0x0043b7e9: FUN_00442130
        G_LocationCurrentBase = NewBase;
        Server = GetLocationServer();
        if (Server != nullptr)
            Server->BaseEnter(NewBase, GetPlayerConnectionId());
        FLocationBaseEntry* const NewEntry = FindLocationBaseEntry(NewBase);
        if (NewEntry->RefCount == 0)
            LoadLocationBase(NewEntry, NewBase);
        NewEntry->RefCount += 1;
        // 0x0043b834..0x0043b84d: FUN_004c4910(&NewBase), FUN_004c4a10(Location),
        // FUN_0043d590(NewBase).
        SetPlayerBaseIdRetail(NewBase);
        SetPlayerFactionScratchRetail(Location);
        SetGameflowLocationBase(NewBase);
    }
    else if ((G_LocationCurrentRoom & 0x3fffffffu) != 0)
    {
        // 0x0043b857..0x0043b8a0: the same base -- leave the current location.
        unsigned const Current = G_LocationCurrentRoom;
        NnArrivalTextDestroy(); // FUN_0045b380
        ReleaseClientLocationRoom(G_LocationCurrentRoom);
        IServer* const Server = GetLocationServer();
        if (Server != nullptr)
            Server->LocationExit(Current, GetPlayerConnectionId());
        G_LocationCurrentRoom = 0;
        G_LocationCurrentBaseEntry = nullptr;
        G_LocationCurrentRoomEntry = nullptr;
        NotifyInventoryLocationLeft(); // FUN_00439220
    }

    // 0x0043b8a5..0x0043b919: the new location and its two records.
    G_LocationCurrentRoom = Location;
    if (Location != 0)
    {
        RoomData* const Room = BaseDataList_get()->get_unloaded_room_data(Location);
        if (Room != nullptr)
        {
            G_LocationCurrentBaseEntry = FindLocationBaseEntry(Room->BaseId);
        }
        else
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: Could not find location %d\n",
                  kLocationControllerCpp, 0x120, Location);
            G_LocationCurrentBaseEntry = nullptr;
        }
        G_LocationCurrentRoomEntry = FindLocationRoomEntry(G_LocationCurrentRoom);
    }
    else
    {
        G_LocationCurrentBaseEntry = nullptr;
        G_LocationCurrentRoomEntry = nullptr;
    }
    // 0x0043b91c..0x0043b923: +0x4c = +0x50 = 0 (the queued change's timer, not its target).
    G_LocationPendingElapsed = 0.0f;
    G_LocationPendingDelay = 0.0f;

    IServer* const Server = GetLocationServer();
    if (Server != nullptr)
        Server->LocationEnter(Location, GetPlayerConnectionId());

    // 0x0043b942..0x0043b95f: FUN_00439610 on +0x38 (FDUMP at 0x110 and a read through null
    // on a miss); at a zero count FUN_00439d80 loads the room; count + 1.
    FLocationRoomEntry* const RoomEntry = FindLocationRoomEntry(G_LocationCurrentRoom);
    if (RoomEntry->RefCount == 0)
        EnsureRoomRuntime(RoomEntry, G_LocationCurrentRoom);
    RoomEntry->RefCount += 1;
    G_bLocationInTransition = false;
}

void LeaveClientLocationBase()
{
    // FUN_0043b3e0 (thiscall).
    unsigned const Base = G_LocationCurrentBase;
    if ((Base & 0x3fffffffu) == 0)
        return;

    unsigned const Current = G_LocationCurrentRoom;
    if ((Current & 0x3fffffffu) != 0)
    {
        // 0x0043b40d..0x0043b4d2.
        NnArrivalTextDestroy(); // FUN_0045b380
        FLocationRoomEntry* const RoomEntry = FindLocationRoomEntry(G_LocationCurrentRoom);
        RoomEntry->RefCount -= 1;
        if (RoomEntry->RefCount == 0)
            UnloadClientLocationRoomState(G_LocationCurrentRoom); // FUN_0043a080
        IServer* const Server = GetLocationServer();
        if (Server != nullptr)
            Server->LocationExit(Current, GetPlayerConnectionId());
        G_LocationCurrentRoom = 0;
        G_LocationCurrentBaseEntry = nullptr;
        G_LocationCurrentRoomEntry = nullptr;
        NotifyInventoryLocationLeft();
    }

    // 0x0043b4d6..0x0043b551: the base (inline lookup, FDUMP at 0x100, read through null on a
    // miss); at a zero count FUN_00439b50; IServer slot 36 BaseExit.
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(Base);
    Entry->RefCount -= 1;
    if (Entry->RefCount == 0)
        UnloadLocationBase(Base);
    IServer* const Server = GetLocationServer();
    if (Server != nullptr)
        Server->BaseExit(Base, GetPlayerConnectionId());
    G_LocationCurrentBase = 0;
    // 0x0043b55b: JMP FUN_00442130 -- the "NavBar" window torn down.
    NavBarDestroy();
}

void DrawClientLocation()
{
    // FUN_0043bbc0 (thiscall): +0x40 non-null and its runtime (+0x08) non-null -> JMP
    // FUN_0044b570 on the runtime.
    if (G_LocationCurrentRoomEntry == nullptr)
        return;
    FRoomRuntime* const Runtime = G_LocationCurrentRoomEntry->Runtime;
    if (Runtime == nullptr)
        return;
    RoomControlDraw(Runtime);
}

unsigned GetClientLocationCurrentBase()
{
    return G_LocationCurrentBase;
}

unsigned GetClientLocationBaseSolarId()
{
    // FUN_0043bc90: MOV EAX,[ECX+0x54]; RET.
    return G_LocationBaseSolarId;
}

int GetClientCurrentBaseStridName()
{
    // FUN_0043bbe0. 0x0043bbe6..0x0043bc35: FUN_00439590's lookup inlined on this+0x34 (the
    // FDUMP at LocationController.cpp:0x100 on a miss, after which retail reads through a null
    // entry at 0x0043bc37 -- the same read is made here).
    FLocationBaseEntry* const Entry = FindLocationBaseEntry(G_LocationCurrentBase);
    BaseData* const Base = Entry->Base;
    if (Base == nullptr)
        return 0;
    // 0x0043bc41..0x0043bc59: Universe::get_base on the BaseData's first dword (its id); a null
    // answer returns 0, otherwise the record's slot 2 (+0x08) -- [+0x0c], strid_name.
    Universe::IBase const* const Record = Universe::get_base(Base->get_base_id());
    if (Record == nullptr)
        return 0;
    return Record->StridName;
}

unsigned GetClientLocationCurrentRoom()
{
    return G_LocationCurrentRoom;
}

bool IsClientLocationInTransition()
{
    return G_bLocationInTransition;
}

void SetClientLocationInTransition()
{
    // FUN_005588a0 0x005588bc: DAT_00668761 (this+0x59) = 1.
    G_bLocationInTransition = true;
}
