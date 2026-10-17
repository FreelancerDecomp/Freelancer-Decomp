// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "character_select_equip.h"

#include "server_characterset.h"
#include "common_id.h"
#include "common_newchar.h"
#include "common_reputation.h" // Reputation::Vibe::SetClientID
#include "common_server.h"     // IServer::SetVisitedState
#include "common_misc.h"       // SinglePlayer
#include "common_universe.h"
#include "dacom.h"       // FDUMP
#include "gf_object_ship.h" // GfObjectShip (FUN_00446be0) and its equipment entry points
#include "globals.h"       // SeedClientAccumulatedTime (DAT_0067ecd8)
#include "login_mgr.h"   // G_LoginMgr (DAT_0067e7b8)
#include "menu_ui_init.h" // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "player_spawn.h" // GetPlayerConnectionId (DAT_00673344)

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include <map>

namespace
{
    // DAT_00672960 — client EquipDescList singleton (a std::list<EquipDesc>, as in retail).
    EquipDescList G_PlayerEquipDescList;

    // DAT_0067296c — collision-group desc list (retail std::list at EquipDescList+0xc area).
    std::list<CollisionGroupDesc> G_PlayerCollisionGroups;

    // DAT_0067337c — selected ship archetype id.
    unsigned G_PlayerShipArchetypeId = 0;

    // DAT_00673380 — relative health scale (1.0 = full). Retail .bss (zero at load); written only
    // by FUN_004c4a80 (0x004c4a94, = 0) and FUN_004c4f80 (0x004c4f84), XREF[3] with the getter.
    float G_PlayerHealthScale = 0.0f;

    // DAT_00673364 — credits.
    unsigned G_PlayerCredits = 0;

    // DAT_00673368 / DAT_0067336c / DAT_00673370 -- the kill, mission-success and
    // mission-failure counts IClient slots 78..80 store through FUN_004c4d70 / 80 / 90. The
    // listing has no reader of any of the three (each XREF[1] is that setter's own write);
    // uninitialised .data, so zero at load.
    unsigned G_PlayerNumKills = 0;
    unsigned G_PlayerMissionSuccesses = 0;
    unsigned G_PlayerMissionFailures = 0;

    // DAT_00673360 — scratch scalar (FUN_004c4650/4660); meaning TBD, spawn copies it.
    unsigned G_PlayerSpawnScratchScalar = 0;

    // DAT_0067338c — the player's rank level (FUN_004c55d0 / FUN_004c55e0); -1 while a
    // RequestRankLevel answer is pending (FUN_004c55f0).
    int G_PlayerRankLevel = 0;

    // DAT_0061226c — spawn system id written by FUN_004c4680 / read by FUN_004c4670.
    int G_PlayerSpawnSystemId = -1;

    // DAT_00673354 / DAT_00673358 — star system / base (FUN_004c4840/4910).
    unsigned G_PlayerStarSystemId = 0;
    unsigned G_PlayerBaseId = 0;

    // DAT_0067335c — faction / slot scratch (FUN_004c4a10).
    unsigned G_PlayerFactionScratch = 0;

    // DAT_006732e4..ec / DAT_006732c0..e0 -- the player ship's last position and orientation,
    // written by the space frame (0x0054b192 / 0x0054b1b0) through FUN_004c4a30 / FUN_004c4a60.
    float G_PlayerLastPosition[3] = {};
    float G_PlayerLastOrientation[9] = {};

    // DAT_006728e4 cluster — 4-dword header + count @ +0x30 + up to 8 rank dwords.
    // Sibling DAT_00672918 cluster (FUN_004c3d20) for the other rank buffer.
    static const int kMaxRepScratchTail = 8;
    unsigned G_RepVibeHeader[4] = {};
    int G_RepVibeCount = 0;
    unsigned G_RepVibeTail[kMaxRepScratchTail] = {};

    unsigned G_RepRankHeader[4] = {};
    int G_RepRankCount = 0;
    unsigned G_RepRankTail[kMaxRepScratchTail] = {};

    // DAT_00673384 / DAT_00673388 — generation counters bumped by equip/collision sync.
    unsigned G_EquipListGeneration = 0;
    unsigned G_CollisionListGeneration = 0;

    // DAT_006732f0 -- 0x30 bytes up to DAT_00673320: the selected character's name.
    wchar_t G_PlayerCharacterName[24] = {};

    // DAT_00673320 -- the player's visit map: system (or object) id -> visit flags (retail VC6
    // std::map<unsigned, unsigned char>: head DAT_00673324, size DAT_00673330, node key +0xc,
    // value +0x10). Built by the static initialiser LAB_004c39d0 (FUN_004c5a10) and destroyed by
    // its atexit entry LAB_004c3a00; a namespace-scope std::map has the same lifetime.
    std::map<unsigned, unsigned char> G_PlayerVisitMap;

    // The body FUN_004c3d20 and FUN_004c3d80 share (0x004c3d20..0x004c3d74 /
    // 0x004c3d80..0x004c3dd4): the costume's four part dwords, its accessory count (src+0x30,
    // stored as it is), then that many accessory dwords from src+0x10 -- the loop bound re-read
    // from the stored count on every pass (JLE skips it for a count <= 0). No clamp, no null
    // test, and the rest of the tail keeps its old values.
    void CopyRepBuffer(unsigned* OutHeader4, int* OutCount, unsigned* OutTail,
                       unsigned const* SourceHeader4, int Count, unsigned const* TailDwords)
    {
        OutHeader4[0] = SourceHeader4[0];
        OutHeader4[1] = SourceHeader4[1];
        OutHeader4[2] = SourceHeader4[2];
        OutHeader4[3] = SourceHeader4[3];
        *OutCount = Count;
        for (int Index = 0; Index < *OutCount; ++Index)
            OutTail[Index] = TailDwords[Index];
    }

    // The three members FUN_005aa9c0 writes live on the connect context itself,
    // G_LoginMgr (DAT_0067e7b8, login_mgr.h): Characters (+0x1b4), SaveFailureCount
    // (+0x1cc) and bCharacterInfoArrived (+0x1d4).
}

unsigned GetPlayerShipArchetypeIdRetail()
{
    return G_PlayerShipArchetypeId;
}

EquipDescList& GetPlayerEquipDescListRetail()
{
    return G_PlayerEquipDescList;
}

unsigned GetPlayerEquipRevisionRetail()
{
    // FUN_004c4f90: MOV EAX,[DAT_00673384] / RET.
    return G_EquipListGeneration;
}

unsigned GetPlayerCollisionRevisionRetail()
{
    // FUN_004c4fa0: MOV EAX,[DAT_00673388] / RET.
    return G_CollisionListGeneration;
}

bool ModifyPlayerEquipRetail(unsigned short Id, CacheString const* Hardpoint, int Count,
                             float Health, unsigned char bEquipped)
{
    // FUN_004c43d0 (cdecl). Added by the NN_Inventory lane (its first ported caller is
    // FUN_004c4540, nn_inventory.cpp).
    EquipDesc* const Item = G_PlayerEquipDescList.find_equipment_item(Id);
    if (Item == nullptr)
        return false;
    ++G_EquipListGeneration; // DAT_00673384

    // 004c4404..004c440b, 004c4507..004c4528: the same hardpoint (pointer compare) -- update
    // the item where it is.
    if (Item->Hardpoint == Hardpoint->Value)
    {
        Item->set_count(Count);
        Item->set_status(Health);
        Item->set_equipped(bEquipped);
        return true;
    }

    // 004c4411..004c44b5: another hardpoint -- copy the item, remove it whole, and add the
    // copy back with the new values (combined when its archetype is combinable).
    EquipDesc Moved(*Item);
    int const Removed = G_PlayerEquipDescList.remove_equipment_item(Id, -1);
    Moved.set_hardpoint(*Hardpoint);
    Moved.set_count(Count);
    Moved.set_status(Health);
    Moved.set_equipped(bEquipped);
    int const Added = G_PlayerEquipDescList.add_equipment_item(Moved, arch_is_combinable(Moved.ArchId));

    // 004c44b9..004c44f8: when the GF ObjectShip singleton (FUN_00446be0, DAT_00668a9c) exists,
    // FUN_00447850(Removed, Moved.ArchId, Moved.Id, &Moved.Hardpoint, Moved.Count) and
    // FUN_004477d0(Added, Moved.ArchId, Moved.Id, Hardpoint, Count, Health) tell it about the
    // move (the copy sits at ESP+0x10: +0x12 Id, +0x14 ArchId, +0x18 Hardpoint, +0x24 Count).
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
    {
        Ship->UnmountEquipment(Removed, Moved.ArchId, Moved.Id,
                               reinterpret_cast<CacheString const*>(&Moved.Hardpoint),
                               static_cast<unsigned>(Moved.Count));
        Ship->MountEquipment(Added, Moved.ArchId, Moved.Id, Hardpoint, Count, Health);
    }
    return true;
}

EquipDesc* FindPlayerNonMissionEquipRetail(unsigned ArchId)
{
    // FUN_004c45d0: traverse_equipment_type(ArchId, null), then (ArchId, previous) until an
    // entry whose byte +0x18 (bMission) is zero -- that entry is returned, null at the end.
    EquipDesc* Desc = G_PlayerEquipDescList.traverse_equipment_type(ArchId, nullptr);
    while (Desc != nullptr)
    {
        if (!Desc->bMission)
        {
            return Desc;
        }
        Desc = G_PlayerEquipDescList.traverse_equipment_type(ArchId, Desc);
    }
    return nullptr;
}

void AddPlayerEquipRetail(unsigned char UnusedFlag, EquipDesc const* Desc)
{
    // FUN_004c4170 (cdecl, plain RET). The first argument is never read (0x004c4177 reads only
    // Stack[0x8]).
    (void)UnusedFlag;
    // 0x004c4171: DAT_00673384++ before anything else.
    ++G_EquipListGeneration;
    // 0x004c417b..0x004c4190: add_equipment_item(desc, arch_is_combinable(desc.ArchId)).
    int const Added = G_PlayerEquipDescList.add_equipment_item(*Desc, arch_is_combinable(Desc->ArchId));
    // 0x004c4198..0x004c41be: with the GF ObjectShip singleton (FUN_00446be0),
    // FUN_004477d0(Added, ArchId +4, Id +2, &Hardpoint +8, Count +0x14, Health +0x10).
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
    {
        Ship->MountEquipment(Added, Desc->ArchId, Desc->Id,
                             reinterpret_cast<CacheString const*>(&Desc->Hardpoint), Desc->Count,
                             Desc->Health);
    }
}

bool RemovePlayerEquipRetail(unsigned char UnusedFlag, unsigned short Id, int Count)
{
    // FUN_004c4320 (cdecl). The first argument is never read (only Stack[0x8] / Stack[0xc]).
    (void)UnusedFlag;
    // 004c432a..004c433a: find_equipment_item(Id); none -> false.
    EquipDesc* const Item = G_PlayerEquipDescList.find_equipment_item(Id);
    if (Item == nullptr)
        return false;
    // 004c4340..004c437f: the item copied field by field before it goes; DAT_00673384 bumped.
    EquipDesc const Removed(*Item);
    ++G_EquipListGeneration;
    // 004c438a..004c439a: remove_equipment_item(Id, Count); true from here on.
    int const RemovedIndex = G_PlayerEquipDescList.remove_equipment_item(Id, Count);
    // 004c43a2..004c43be: when the GF ObjectShip singleton (FUN_00446be0, DAT_00668a9c) exists,
    // FUN_00447850(RemovedIndex, Removed.ArchId, Removed.Id, &Removed.Hardpoint, Count) tells
    // it (the copy sits at ESP+0xc: +0xe Id, +0x10 ArchId, +0x14 Hardpoint; Count is EDI).
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
    {
        Ship->UnmountEquipment(RemovedIndex, Removed.ArchId, Removed.Id,
                               reinterpret_cast<CacheString const*>(&Removed.Hardpoint),
                               static_cast<unsigned>(Count));
    }
    return true;
}

std::list<CollisionGroupDesc>& GetPlayerCollisionGroupListRetail()
{
    return G_PlayerCollisionGroups;
}

unsigned const* GetPlayerRepVibeScratchRetail()
{
    return G_RepVibeHeader;
}

int GetPlayerRepVibeScratchCountRetail()
{
    return G_RepVibeCount;
}

unsigned const* GetPlayerRepVibeScratchTailRetail()
{
    return G_RepVibeTail;
}

unsigned GetPlayerSpawnScratchScalarRetail()
{
    return G_PlayerSpawnScratchScalar;
}

int GetPlayerSpawnSystemIdRetail()
{
    return G_PlayerSpawnSystemId;
}

float GetPlayerHealthScaleRetail()
{
    return G_PlayerHealthScale;
}

int GetPlayerRankLevelRetail()
{
    // FUN_004c55d0: MOV EAX,[DAT_0067338c].
    return G_PlayerRankLevel;
}

void SetPlayerRankLevelRetail(int Level)
{
    // FUN_004c55e0 (cdecl): [DAT_0067338c] = Level. Caller 0x0053c467.
    G_PlayerRankLevel = Level;
}

unsigned GetPlayerCreditsRetail()
{
    return G_PlayerCredits;
}

unsigned GetPlayerStarSystemIdRetail()
{
    return G_PlayerStarSystemId;
}

unsigned GetPlayerBaseIdRetail()
{
    return G_PlayerBaseId;
}

unsigned GetPlayerFactionScratchRetail()
{
    return G_PlayerFactionScratch;
}

void SetPlayerShipArchetypeIdRetail(unsigned ShipArchId)
{
    // FUN_004c3e20 (cdecl). 0x004c3e20..0x004c3e2d: unchanged -> nothing.
    if (G_PlayerShipArchetypeId == ShipArchId)
        return;

    // 0x004c3e3a: DAT_0067337c = ShipArchId; 0x004c3e40..0x004c3e71: every node of the
    // collision-group list DAT_0067296c unlinked and freed (the inlined erase loop). No
    // generation counter here (DAT_00673388 is bumped by FUN_004c3f30 and FUN_004c4000 only).
    G_PlayerShipArchetypeId = ShipArchId;
    G_PlayerCollisionGroups.clear();

    // 0x004c3e73..0x004c3ebd: a real id -> Archetype::GetShip; found ->
    // get_undamaged_collision_group_list(&DAT_0067296c); not found -> FDUMP.
    if ((ShipArchId & 0x3fffffffU) != 0)
    {
        Archetype::Ship* ShipArch = Archetype::GetShip(ShipArchId);
        if (ShipArch != nullptr)
        {
            ShipArch->get_undamaged_collision_group_list(G_PlayerCollisionGroups);
        }
        else
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: Error retrieving ship type %u",
                  "E:\\FL\\Scratch\\Source\\Client\\nn\\Player.cpp", 0xae, ShipArchId);
        }
    }

    // 0x004c3ec0..0x004c3ee9: with the GF ObjectShip up, FUN_00448540(&DAT_00672960),
    // FUN_00448680(DAT_0067337c), then tail-JMP FUN_00447210.
    FObjectShip* const Ship = GfObjectShip();
    if (Ship == nullptr)
        return;
    Ship->SetEquipment(&G_PlayerEquipDescList);
    Ship->SetShipArchetype(G_PlayerShipArchetypeId); // FUN_00448680(DAT_0067337c)
    Ship->RebuildModel();
}

void SyncPlayerEquipDescListRetail(EquipDescList const& Source)
{
    // FUN_004c4070 — the singleton list is emptied first, unconditionally (inlined node
    // erase loop, .text:004c4071-004c40ac), and only then list::operator= from Source runs
    // when Source is not the singleton itself (.text:004c40b8). Retail has no
    // EquipDescList::clear export; this is the inlined container clear.
    G_PlayerEquipDescList.Items.clear();
    if (&Source != &G_PlayerEquipDescList)
        G_PlayerEquipDescList.Items = Source.Items;
    ++G_EquipListGeneration;
}

void SyncPlayerCollisionGroupListRetail(std::list<CollisionGroupDesc> const& Source)
{
    // FUN_004c3f30 (cdecl, RET). 0x004c3f34: Source == &DAT_0067296c skips straight to the
    // counter. 0x004c3f44..0x004c3fe1 is VC6 std::list::operator= inlined: overwrite the
    // existing nodes value by value (8 bytes each), erase the surplus (FUN_005b7e1d, size--),
    // append the rest (operator new(0x10), size++) -- the same result as this operator=.
    if (&Source != &G_PlayerCollisionGroups)
        G_PlayerCollisionGroups = Source;
    // 0x004c3fe3: DAT_00673388++ on both paths.
    ++G_CollisionListGeneration;
    // 0x004c3fe9..0x004c3ff8: when the GF ObjectShip singleton exists (FUN_00446be0 ->
    // DAT_00668a9c), tail-JMP thunk_FUN_00447210 on it (RebuildModel).
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
        Ship->RebuildModel();
}

void SetPlayerRepRankBufferRetail(unsigned const* SourceHeader4, int Count,
                                  unsigned const* TailDwords)
{
    // FUN_004c3d20
    CopyRepBuffer(G_RepRankHeader, &G_RepRankCount, G_RepRankTail, SourceHeader4, Count, TailDwords);
}

void SetPlayerRepVibeScratchRetail(unsigned const* SourceHeader4, int Count,
                                   unsigned const* TailDwords)
{
    // FUN_004c3d80
    CopyRepBuffer(G_RepVibeHeader, &G_RepVibeCount, G_RepVibeTail, SourceHeader4, Count, TailDwords);
}

void SetPlayerSpawnScratchScalarRetail(unsigned Value)
{
    G_PlayerSpawnScratchScalar = Value;
}

void SetPlayerSpawnSystemIdRetail(int SystemId)
{
    G_PlayerSpawnSystemId = SystemId;
}

void SetPlayerHealthScaleRetail(float Scale)
{
    G_PlayerHealthScale = Scale;
}

void SetPlayerCreditsRetail(unsigned Credits, int bFromCharacterSelect)
{
    // FUN_004c4cf0 (cdecl). With the byte param_2 set (character select, 0x004c4d17) a plain
    // store. Otherwise (IClient SETCASH, 0x005a5f70) 0x004c4cfd `CMP ESI,[DAT_00673364] / JLE`
    // -- a signed compare -- plays UI sound slot 0x26 through FUN_005645c0 only when the new
    // amount is GREATER than the old one, then stores it.
    if (bFromCharacterSelect == 0
        && static_cast<int>(Credits) > static_cast<int>(G_PlayerCredits))
        PlayOrRewindUiSoundSlot(0x26);
    G_PlayerCredits = Credits;
}

void SetPlayerNumKillsRetail(unsigned Kills)
{
    // FUN_004c4d70: MOV EAX,[ESP+4] / MOV [DAT_00673368],EAX / RET.
    G_PlayerNumKills = Kills;
}

void SetPlayerMissionSuccessesRetail(unsigned Successes)
{
    // FUN_004c4d80: MOV EAX,[ESP+4] / MOV [DAT_0067336c],EAX / RET.
    G_PlayerMissionSuccesses = Successes;
}

void SetPlayerMissionFailuresRetail(unsigned Failures)
{
    // FUN_004c4d90: MOV EAX,[ESP+4] / MOV [DAT_00673370],EAX / RET.
    G_PlayerMissionFailures = Failures;
}

namespace
{
    // The tail FUN_004c4840 (004c484f..004c48f1) and FUN_004c4910 (004c4943..004c49e5) share
    // byte for byte: the system's own visit byte (ISystem vt+0x48); with neither 0x80 nor 0x01
    // set, bit 0 is added and the result goes through the body of FUN_004c4e00 with the server
    // notification on.
    void MarkPlayerSystemVisited(unsigned SystemId)
    {
        Universe::ISystem const* const System = Universe::get_system(SystemId);
        if (System == nullptr)
            return;
        unsigned char Flags = System->GetVisit();
        if ((Flags & 0x81u) != 0)
            return;
        Flags = static_cast<unsigned char>(Flags | 1u);
        SetPlayerVisitFlagsRetail(G_PlayerStarSystemId, Flags, true);
    }
}

void SetPlayerStarSystemIdRetail(unsigned SystemId)
{
    // FUN_004c4840: DAT_00673354 = SystemId, then the visit tail on that system.
    G_PlayerStarSystemId = SystemId;
    MarkPlayerSystemVisited(SystemId);
}

void SetPlayerBaseIdRetail(unsigned BaseId)
{
    // FUN_004c4910 (retail takes the id through a pointer). DAT_00673358 = BaseId; a base id
    // with any of the low 30 bits set resolves its system -- Universe::get_base, then the base's
    // vt+0x10 (the port's IBase::SystemId), with no null test on either -- into DAT_00673354,
    // and the visit tail runs on it.
    G_PlayerBaseId = BaseId;
    if ((BaseId & 0x3fffffffU) == 0)
        return;
    Universe::IBase* const Base = Universe::get_base(BaseId);
    G_PlayerStarSystemId = Base->SystemId;
    MarkPlayerSystemVisited(G_PlayerStarSystemId);
}

bool GetPlayerVisitFlagsRetail(unsigned Id, unsigned char* OutFlags, unsigned Arg3,
                               unsigned Arg4)
{
    // FUN_004c4da0: lower_bound (FUN_0052c7a0), then the key test -- a find. A hit writes the
    // node's flags (+0x10) and returns 1; a miss returns 0 and leaves *OutFlags alone. The
    // third and fourth arguments are never read (0x004c4da0..0x004c4df1 touch only
    // [ESP+0xc] and [ESP+0x10] of the frame, i.e. arguments 1 and 2).
    (void)Arg3;
    (void)Arg4;
    std::map<unsigned, unsigned char>::const_iterator const It = G_PlayerVisitMap.find(Id);
    if (It == G_PlayerVisitMap.end())
        return false;
    *OutFlags = It->second;
    return true;
}

void SetPlayerVisitFlagsRetail(unsigned Id, unsigned char Flags, bool bNotifyServer)
{
    // FUN_004c4e00.
    // 004c4e1c..004c4e38: find (FUN_00432240); an entry that already has every bit of Flags
    // is left as it is.
    std::map<unsigned, unsigned char>::iterator const Found = G_PlayerVisitMap.find(Id);
    if (Found != G_PlayerVisitMap.end() &&
        static_cast<unsigned char>(Found->second & Flags) == Flags)
        return;

    // 004c4e3a..004c4e61: insert {Id, 0} (FUN_0046d6d0) and store Flags over the node's value
    // -- a plain store, not an OR with what was there.
    std::pair<std::map<unsigned, unsigned char>::iterator, bool> const Inserted =
        G_PlayerVisitMap.insert(std::make_pair(Id, static_cast<unsigned char>(0)));
    Inserted.first->second = Flags;

    if (!bNotifyServer)
        return;
    // 004c4e66..004c4e8b: IServer vt+0x11c SetVisitedState(DAT_00673344, record, 9); the
    // record is one entry: count 1, the id, the flags byte.
    unsigned char Record[9];
    int const Count = 1;
    memcpy(Record, &Count, 4);
    memcpy(Record + 4, &Id, 4);
    Record[8] = Flags;
    G_Server->SetVisitedState(GetPlayerConnectionId(), Record, 9);
}

void ClearPlayerVisitMapRetail()
{
    // The map clear FUN_004c4a80 (004c4b16..004c4ba1) and FUN_004c4bc0 (004c4c27..004c4ca2)
    // inline: the whole tree freed, or node-by-node erase.
    G_PlayerVisitMap.clear();
}

void ClearPlayerShipArchAndHealthRetail()
{
    // 0x004c4a8a / 0x004c4a94 (FUN_004c4a80 only): two plain stores, not the setters.
    G_PlayerShipArchetypeId = 0;   // DAT_0067337c
    G_PlayerHealthScale = 0.0f;    // DAT_00673380
}

void SetPlayerCollisionGroupHealthRetail(unsigned short Id, float Health)
{
    // FUN_004c4000 (cdecl): the first node of DAT_0067296c whose id (+0x08) is Id gets the
    // health (+0x0c); DAT_00673388 is bumped whether or not one matched (0x004c401d /
    // 0x004c402b).
    for (std::list<CollisionGroupDesc>::iterator It = G_PlayerCollisionGroups.begin();
         It != G_PlayerCollisionGroups.end(); ++It)
    {
        if (It->Id == Id)
        {
            It->Health = Health;
            break;
        }
    }
    ++G_CollisionListGeneration;
}

void SetPlayerFactionScratchRetail(unsigned Value)
{
    G_PlayerFactionScratch = Value;
}

float const* GetPlayerLastPositionRetail()
{
    // FUN_004c4a20: MOV EAX, DAT_006732e4 / RET.
    return G_PlayerLastPosition;
}

void SetPlayerLastPositionRetail(float const* Position)
{
    // FUN_004c4a30: three dword copies from the argument.
    G_PlayerLastPosition[0] = Position[0];
    G_PlayerLastPosition[1] = Position[1];
    G_PlayerLastPosition[2] = Position[2];
}

float const* GetPlayerLastOrientationRetail()
{
    // FUN_004c4a50: MOV EAX, DAT_006732c0 / RET.
    return G_PlayerLastOrientation;
}

void SetPlayerLastOrientationRetail(float const* Orientation)
{
    // FUN_004c4a60: REP MOVSD of nine dwords from the argument.
    for (int Index = 0; Index < 9; ++Index)
        G_PlayerLastOrientation[Index] = Orientation[Index];
}

void InitCreateParmsScratchRetail(void* ParmsBytes)
{
    // FUN_0052e0a0 (thiscall, returns this) -- the exe's compiled CSimple::CreateParms default
    // constructor: +0x00..+0x58 zero except HitPts +0x0c = -1.0f and the matrix diagonal
    // +0x1c / +0x2c / +0x3c = 1.0f. No null test.
    unsigned* Words = static_cast<unsigned*>(ParmsBytes);
    for (int Index = 0; Index < 0x17; ++Index)
        Words[Index] = 0;
    Words[3] = 0xbf800000;  // -1.0f HitPts sentinel style
    Words[7] = 0x3f800000;
    Words[0xb] = 0x3f800000;
    Words[0xf] = 0x3f800000;
}

bool ApplyCharacterSelectEquipFields(CCharacterSet::CharacterInfo const* CharInfo)
{
    // FUN_005ab250 extract tail (.text:005ab29c .. .text:005ab36d, after the
    // IServer::CharacterSelect wait succeeds). Every read below is a named member
    // of the shared record (src/include/server_characterset.h); the retail
    // offset is on each line.
    if (CharInfo == nullptr)
        return false;

    SetPlayerSpawnScratchScalarRetail(CharInfo->VoiceId);          // +0x26c FUN_004c4660
    SetPlayerHealthScaleRetail(CharInfo->HullStatus);              // +0x244 FUN_004c4f80
    SetPlayerShipArchetypeIdRetail(CharInfo->ShipArchetype);       // +0x230 FUN_004c3e20

    // +0x254 FUN_004c3f30, then +0x248 FUN_004c4070 (.text:005ab2cc LEA ECX,[ESI+0x248];
    // PUSH ECX): the record's own EquipDescList (server_characterset.h) is handed over and
    // list-assigned into the client singleton.
    SyncPlayerCollisionGroupListRetail(CharInfo->CollisionGroups);
    SyncPlayerEquipDescListRetail(CharInfo->Equipment);

    SetPlayerCreditsRetail(static_cast<unsigned>(CharInfo->Money), 1);    // +0x234 FUN_004c4cf0

    // +0x2a8 FUN_004c3d20 and +0x274 FUN_004c3d80: the base and the comm Costume
    // (four parts, the accessory count at +0x30, the accessory ids).
    Costume const& BaseCostume = CharInfo->BaseCostume;
    SetPlayerRepRankBufferRetail(BaseCostume.PartIds, static_cast<int>(BaseCostume.AccessoryCount),
                                 BaseCostume.AccessoryIds);
    Costume const& ComCostume = CharInfo->ComCostume;
    SetPlayerRepVibeScratchRetail(ComCostume.PartIds, static_cast<int>(ComCostume.AccessoryCount),
                                  ComCostume.AccessoryIds);

    unsigned BaseId = CharInfo->BaseId;                            // +0x264
    if ((BaseId & 0x3fffffffU) != 0)
    {
        SetPlayerStarSystemIdRetail(0);
        SetPlayerBaseIdRetail(BaseId);
    }
    else
    {
        SetPlayerBaseIdRetail(0);
        SetPlayerStarSystemIdRetail(CharInfo->SystemId);           // +0x260
    }

    SetPlayerFactionScratchRetail(CharInfo->LocationId);           // +0x268 FUN_004c4a10

    // .text:005ab34b .. .text:005ab364 -- wcscpy(DAT_006732f0, CharInfo->Name); an empty
    // name is the _Nullstr, i.e. L"". Unbounded, as retail (the buffer is 24 wchars).
    wcscpy(G_PlayerCharacterName, CharInfo->Name.c_str());

    return true;
}

void CharacterSelect_ReceiveCharacterInfo(CCharacterSet const& Set)
{
    // FUN_005aa9c0 (.text:005aa9c0 .. .text:005aac98, thiscall on DAT_0067e7b8,
    // `RET 4`), reached from IClient slot 13 (LAB_005a8220).
    // .text:005aa9e2 -- +0x1d4 = 1 before anything else.
    G_LoginMgr.bCharacterInfoArrived = 1;

    // .text:005aa9f0 .. .text:005aac70 -- the traverse idiom over the set's list.
    // Each node's record is copied into a fresh record (operator new(0x2dc) and the
    // inlined constructor, .text:005aaa08 .. .text:005aab00) and appended to +0x1b4
    // (FUN_005ae700). The node's Data is used without a null test (.text:005aab0a).
    MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
    while (Set.Characters.traverse(Node, true))
    {
        CCharacterSet::CharacterInfo const* Source = Node->Data;
        CCharacterSet::CharacterInfo* Copy = new CCharacterSet::CharacterInfo();
        Copy->Id = Source->Id;                                     // .text:005aab18
        Copy->bIsRestart = Source->bIsRestart;                     // .text:005aab24
        Copy->bIsAutoSave = Source->bIsAutoSave;                   // .text:005aab30
        Copy->Name = Source->Name;                                 // .text:005aab4c
        Copy->Description = Source->Description;                   // .text:005aab68
        Copy->DescripStrId = Source->DescripStrId;                 // .text:005aab74
        Copy->TimestampLow = Source->TimestampLow;                 // .text:005aab80
        Copy->TimestampHigh = Source->TimestampHigh;               // .text:005aab8c
        Copy->ShipArchetype = Source->ShipArchetype;               // .text:005aab98
        Copy->Money = Source->Money;                               // .text:005aaba4
        Copy->NumKills = Source->NumKills;                         // .text:005aabb0
        Copy->NumMissionSuccesses = Source->NumMissionSuccesses;   // .text:005aabbc
        Copy->NumMissionFailures = Source->NumMissionFailures;     // .text:005aabc8
        Copy->HullStatus = Source->HullStatus;                     // .text:005aabe1
        Copy->Equipment = Source->Equipment;                       // .text:005aabe7
        Copy->CollisionGroups = Source->CollisionGroups;           // .text:005aabfa
        Copy->SystemId = Source->SystemId;                         // .text:005aac06
        Copy->BaseId = Source->BaseId;                             // .text:005aac12
        Copy->LocationId = Source->LocationId;                     // .text:005aac1e
        Copy->VoiceId = Source->VoiceId;                           // .text:005aac30
        Copy->Rank = Source->Rank;                                 // .text:005aac43
        CharacterInfoCopyCostume(Copy->ComCostume, Source->ComCostume);   // .text:005aac49 FUN_0053c250
        CharacterInfoCopyCostume(Copy->BaseCostume, Source->BaseCostume); // .text:005aac5b FUN_0053c250
        G_LoginMgr.Characters.append(Copy);                        // .text:005aac6b FUN_005ae700
    }

    // .text:005aac75 .. .text:005aac82 -- +0x1cc = the set's +0x14.
    G_LoginMgr.SaveFailureCount = Set.SaveFailureCount;
}

wchar_t* GetPlayerCharacterNameRetail()
{
    return G_PlayerCharacterName;
}

OwnerList<CCharacterSet::CharacterInfo>& CharacterSelect_GetConnectCharacters()
{
    return G_LoginMgr.Characters;
}

void NotifyCharacterSelectComplete(unsigned const Response4[4])
{
    // FUN_005ab380 after its first store (.text:005ab385, CLoginMgr+0x21c = 1, done by
    // CLoginMgr::OnCharacterSelectResponse). The response is read without a null test.
    SetPlayerSpawnSystemIdRetail(static_cast<int>(Response4[0])); // FUN_004c4680 (005ab38f)

    if (!SinglePlayer())
    {
        // .text:005ab3a1..005ab3ab: Reputation::Vibe::SetClientID(Response[0], DAT_00673344).
        Reputation::Vibe::SetClientID(static_cast<int>(Response4[0]), GetPlayerConnectionId());
    }

    // .text:005ab3b1..005ab3bf / 005ab3c9..005ab3d5: both paths store +8 / +0xc into
    // DAT_0067ecd8 / DAT_0067ecdc -- the low and high dwords of the client clock double that
    // FUN_005b2360 advances every frame (client_timing.cpp), copied bit for bit.
    double ClientTime = 0.0;
    memcpy(&ClientTime, &Response4[2], sizeof(ClientTime));
    SeedClientAccumulatedTime(ClientTime);
}
