// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU29 (06ef3630-06ef8d40): the encounter parameters of a system, read from the
// files named by [EncounterParameters] sections of the system ini (SystemInfo 06f74c2d),
// plus their copies that the zone "encounter = id, level, chance" key keeps (TU30).
// Lane CNT-POP (docs/re/content.md, TU map).

#include "common_id.h"                  // ID_String
#include "common_weighted_vector.h"     // weighted_vector<unsigned int>

#include <list>
#include <vector>

class INI_Reader;
struct FContentNpcShipParams;   // NpcShip.h (TU43)
struct FDensityRestriction;     // SystemInfo.h (TU30 zone "density_restriction")
struct FGuideDesc;              // Guides.h (TU37)
struct FNpcShipArch;            // NpcShips.h (TU42)
struct FSpaceObjPlacement;      // NpcBase.h (orientation + position)
struct FSpawnPlan;              // Guides.h (FGuideDesc's +0x10 block; the spawn placement plan)
struct FSpawnRequest;           // Populators.h (the SpacePop population request)
struct FSystemZone;             // SystemInfo.h (TU64)

// One formation reference of a permutation, retail 8 bytes: [Creation] "permutation =
// 1 + 2* 3" gives {1, false}, {2, true}, {3, false}.
struct FEncounterPermutationEntry
{
    int FormationIndex;         // +0x00 ParseNumber + ftol
    bool bStar;                 // +0x04 a '*' followed the number
};

// EncounterParameters +0x18 element, retail 0x14 bytes (allocator byte, 8-byte vector,
// weight); copy FUN_06efa740, dtor = the vector.
struct FEncounterPermutation
{
    std::vector<FEncounterPermutationEntry> Entries;   // +0x04
    float Weight;                                      // +0x10 (float) value 1
};

// One ship line of an [EncounterFormation], retail 0x38 bytes (dtor FUN_06ef6330: the
// vectors at +0x24 and +0x00).
struct FEncounterShip
{
    // +0x00 ship_by_class value 2, then the difficulty tags "diff%d" (FUN_06ef39a0); each id
    // once (linear find before the insert).
    std::vector<unsigned int> ShipClasses;
    ID_String NpcArch;                  // +0x10 ship_by_npc_arch value 2
    // +0x14: 0 in both loaders; the spawn (FUN_06ef54e0 06ef5754) resolves the line's arch
    // here -- NpcArch in GNpcShipArchs, else a pick for the formation's faction -- and
    // FUN_06ef4780 builds the blocks from it.
    FNpcShipArch* Arch;
    ID_String PilotJob;                 // +0x18 pilot_job (last ship line)
    int MinCount;                       // +0x1c value 0 (1 by default)
    int MaxCount;                       // +0x20 value 1 (1 by default)
    std::vector<unsigned int> MakeClasses;   // +0x24 make_class (last ship line), unique
    int DifficultyOffset;               // +0x34 ship_by_class value 3 (0 by default)

    FEncounterShip();
};

// EncounterParameters +0x2c element: one [EncounterFormation] section, retail 0x80 bytes.
// Ctor FUN_06ef5b50 (defaults + ParseSection), dtor FUN_06ef3e30, copy FUN_06efa7c0 (all
// member-wise).
struct FEncounterFormation
{
    // +0x00: 1 once feeling_to_formation was read (FUN_06ef3cf0); while it is 0 the four
    // feeling_* / explicit_group keys go to FUN_06ef3cf0.
    int bFeelingToFormation;
    int RerollFrequency;                // +0x04 once 0, synch 1, every 2 (default 2)
    unsigned int FeelingMask;           // +0x08 friendly 1, hostile 4, same 8, any 7 (default 7)
    int Field_0x0c;                     // +0x0c -1; TODO: reader not ported
    int FeelingValue;                   // +0x10 feeling_to_formation value 1 (-1 default)
    // +0x14: the faction choices of the formation (FUN_06ef4180 builds them, FUN_06ef44b0
    // picks from them).
    weighted_vector<unsigned int> Factions;
    unsigned int Faction;               // +0x24 the faction picked by FUN_06ef44b0 (-1)
    std::vector<FEncounterShip> Ships;  // +0x28 ship_by_class / ship_by_npc_arch
    ID_String Formation;                // +0x38 "formation" (set only while both are empty)
    ID_String FormationByClass;         // +0x3c "formation_by_class" (same rule)
    int Behavior;                       // +0x40 "behavior" -> ParseGuideBehavior (0)
    // +0x44: arrival flags, always 9 bytes (1 by default; "arrival" clears them first):
    // [1] tradelane, [2] (all only), [3] object_docking_ring, [4] object_jump_gate,
    // [5] object_station, [6] object_capital, [7] cruise, [8] buzz; [0] is never written.
    std::vector<unsigned char> Arrival;
    double ZoneCreationDistance;        // +0x58 (double) value 0 (1500.0)
    // +0x60: the space object ids created for this formation; FUN_06ef52d0 drops the ones
    // SystemInfo no longer knows (FUN_06f73930).
    std::vector<unsigned int> CreatedObjects;
    int TimesToCreate;                  // +0x70 -1 = "infinite"
    bool bAllowSimultaneousCreation;    // +0x74 yes/true 1, no/false 0 (1)
    int Field_0x78;                     // +0x78 0; FUN_06ef52d0 zeroes it on a reroll
    int Longevity;                      // +0x7c value 0 (24)

    // FUN_06ef5b50 (RET 4).
    explicit FEncounterFormation(INI_Reader* Reader);

    // FUN_06ef5ca0 (RET 4): the section's values; true when it has a ship line.
    bool ParseSection(INI_Reader* Reader);
    // FUN_06ef3cf0 (RET 4): group_reroll_frequency / feeling_to_formation.
    void ParseFeeling(INI_Reader* Reader);
    // FUN_06ef6360 (RET 4): "arrival = all, -tradelane, ...".
    void ParseArrival(INI_Reader* Reader);
};

// The EncounterParameters object, retail 0x4c bytes. The SystemInfo loader owns one per
// [EncounterParameters] section while the zones are read (SystemInfo +0x120); the zone
// "encounter" key copies it (FUN_06f78ef0) into the zone's list.
// Dtor FUN_06ef93c0 and copy ctor FUN_06ef9e50 are member-wise.
struct FEncounterParameters
{
    ID_String Nickname;                 // +0x00 the [EncounterParameters] nickname id
    unsigned int SystemId;              // +0x04
    unsigned int ZoneId;                // +0x08 0; the zone "encounter" key writes it
    float RerollInterval;               // +0x0c (float) value 2 of a "permutation" line (0)
    float LastRerollTime;               // +0x10 -1.0f until FUN_06ef52d0 runs
    int Difficulty;                     // +0x14 -1 until FUN_06ef39a0
    std::vector<FEncounterPermutation> Permutations;   // +0x18 [Creation] "permutation"
    // +0x28: the permutation picked by FUN_06ef52d0 (read by FUN_06ef54e0). The ctor sets
    // it to the (null) end of the empty +0x18; the copy ctor copies the pointer as is.
    FEncounterPermutation* CurrentPermutation;
    std::vector<FEncounterFormation> Formations;       // +0x2c [EncounterFormation]
    // +0x3c: faction weights; filled from the zone's "faction = name, weight" lines that
    // follow its "encounter" line (TU30 06ef90d4).
    weighted_vector<unsigned int> Factions;

    // FUN_06ef3950: the zone key's local; +0x04..+0x14 and +0x28 are left unset in retail
    // (the port zeroes them) and FUN_06f78ef0 overwrites every field before use.
    FEncounterParameters();
    // FUN_06ef3830 (RET 0xc): defaults, then Load(Reader).
    FEncounterParameters(INI_Reader* Reader, unsigned int const* Id, unsigned int const* InSystemId);

    // FUN_06ef3ab0 (RET 4): [EncounterFormation] / [Creation] sections; a missing
    // permutation list becomes one permutation of every formation (weight 1). True when
    // the first formation has a ship line.
    bool Load(INI_Reader* Reader);
    // FUN_06ef3ef0 (RET 4): the [Creation] section.
    void ParseCreation(INI_Reader* Reader);
    // FUN_06ef39a0 (RET 4): the first call sets Difficulty and adds "diff<n>" to every ship
    // line with ship classes (n = Difficulty + its offset, at least 1).
    void ApplyDifficulty(int Level);

    // FUN_06ef52d0 (RET 4): drop the formations' dead objects, re-pick the factions of the
    // formations whose reroll_frequency asks for it, re-pick CurrentPermutation when the
    // reroll interval has passed; LastRerollTime = Time. False when a faction pick fails.
    // Callers: the zone validation FUN_06ef9dd0 (Time 0) and the TU30 spawn path.
    bool Refresh(float Time);
    // FUN_06ef4180 (RET 4): Formation->Factions = the pairs of Factions whose faction may
    // fly every ship line of the formation (TU44 FUN_06f35540 / FUN_06f35310). True when
    // any remain.
    bool BuildFormationFactions(FEncounterFormation* Formation);
    // FUN_06ef44b0 (RET 4): Formation->Faction = a weighted pick from Formation->Factions;
    // with feeling_to_formation only among the factions whose feeling towards the faction
    // of formation FeelingValue matches FeelingMask ("same": that very faction).
    bool PickFaction(FEncounterFormation* Formation);

    // FUN_06ef54e0 (RET 0xc; callers TU30 FUN_06ef95d0 / FUN_06ef9a60): walks
    // CurrentPermutation and spawns the formations whose '*' flag equals *bStarred and that
    // the player's relation, the zone's battle size / relief time, simultaneous creation,
    // times_to_create, the faction, the ship archs and (unstarred) the density restrictions
    // allow. Returns the block count of the last formation spawned; unstarred, the first
    // spawn returns at once.
    int Spawn(FSpawnRequest const* Request, bool const* bStarred,
              std::list<FDensityRestriction> const* Restrictions);
    // FUN_06ef5a20 (RET 0xc): every restriction whose class occurs in the formation (ship
    // classes, make_class or the line's arch classes) must still have room: the NPC ships of
    // that class within the density radius / cone of Placement plus the formation's count of
    // it may not exceed MaxCount.
    bool CheckDensityRestrictions(FSpaceObjPlacement const* Placement,
                                  std::list<FDensityRestriction> const* Restrictions,
                                  FEncounterFormation const* Formation);
    // FUN_06ef4eb0 (RET 0x10): the blocks of one formation, placed (TU53), named, the
    // leader's guide given the plan and, for a trade guide, cargo for the freighters /
    // transports; then laid out in its formation and clamped against SpacePop. Blocks is
    // emptied when TU53 finds no placement.
    void SpawnFormation(FSpawnRequest const* Request, FSpawnPlan* Plan,
                        FEncounterFormation* Formation,
                        std::list<FContentNpcShipParams>* Blocks);
};

// FUN_06ef4780 (thiscall with an unused this, RET 0x10; caller 06ef4f00): one parameter block
// per ship of every ship line of Formation (count = min..max by rand), appended to Blocks.
// The first ship of the first line carries a guide description of the formation's behavior
// with Zone as its wander zone; longevity goes to +0x104 / +0x108 (x2).
void BuildFormationBlocks(FEncounterFormation const* Formation,
                          std::list<FContentNpcShipParams>* Blocks, FSystemZone* Zone,
                          unsigned int ControllerId);
// FUN_06ef4cf0 (cdecl; caller 06ef4f7b): every block gets a name and reputation from the
// formation's faction (TU42 name generation); the first block's name fixes the formation
// designation for the others.
void __cdecl NameFormationBlocks(std::list<FContentNpcShipParams>* Blocks,
                                 FEncounterFormation const* Formation);

// FSystemZone population +0x20 element, retail 0x50 bytes: the parameters copied by the
// zone's "encounter" key, with its chance at +0x4c. Dtor FUN_06ef94e0 (member-wise, the
// same body as FUN_06ef93c0), copy FUN_06efa340.
struct FZoneEncounter : public FEncounterParameters
{
    float Chance;                       // +0x4c "encounter" value 2

    // TU30 06ef91ae-06ef91c3: the base copy ctor FUN_06ef9e50, then +0x4c = the chance.
    FZoneEncounter(FEncounterParameters const& Parameters, float InChance)
        : FEncounterParameters(Parameters)
        , Chance(InChance)
    {
    }
};
