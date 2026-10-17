// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// TU38 (06f0e000-06f24080): the mission object base class. Retail ctor FUN_06f0e3e0,
// vtable 06fb85d8 (29 slots, 0..28), object ends at +0x1b0. Derived in retail by the TU21
// script mission (FMissionScript, ctor FUN_06eb75a0, vtable 06fb4710) -> GenericMission /
// Mission_01a..13 (StoryMissions.cpp), and by DestroyMission (TU27, ctor FUN_06eef3d0).

#include "content.h"
#include "Guides.h"             // FContentGuideHandle (mission ship slot +0x68)
#include "NpcShip.h"           // FContentNpcShip (TU43): the mission's NPC ships
#include "NpcShips.h"           // FNpcShipArchMap (TU42): the mission's npc_ship_file table
#include "ObjListDirective.h"   // FMissionDirectiveList (ref-owning directive list)
#include "RandomEncounters.h"   // FRandEncShipChoice (TU31): the +0x8c record's ship choices
#include "RefHandle.h"          // TRefHandle ([MsnRandEnc] Params / Populator)
#include "server_cobject.h"     // pub::SpaceObj::LootInfo ([MsnLoot] record)
#include "common_id.h"
#include "common_server.h"
#include "common_text.h"
#include "common_tstring.h"
#include "x86math.h"

#include <cstdio>
#include <list>
#include <map>
#include <vector>

class INI_Reader;
class FContentGuide;            // Guides.h (TU37): the refcounted guide of a mission ship slot
class FObjListDirective;        // ObjListDirective.h (TU40)
class FMetaBehavior;            // Guides.h (TU37 [MetaBehavior] object)
class FRandEncParams;           // RandomEncounters.h (TU31 random encounter parameters)
class FRandEncPopulator;        // RandomEncounters.h (TU31 random encounter populator)
struct FContentPlayerState;
struct FSystemInfo;

// ---------------------------------------------------------------------------------------
// [Msn*] records, filled by FMissionObject::LoadMission (FUN_06f172f0, slot 25).
// ---------------------------------------------------------------------------------------

// FMissionDirectiveList (the ref-owning [ObjList] directive list of the mission ship slot +0x58
// and the [ObjList] map +0x100) lives in ObjListDirective.h.

// A [MetaBehavior] list holding one reference on each object: the value of the +0x114 map. The
// reference handling is the same inlined code as FMissionDirectiveList's (the copy FUN_06f20230
// and the release loops are ICF-shared: slot 0 AddRef / slot 1 Release on each element); the map
// node value dtor is part of FUN_06f1e120.
class FMissionMetaBehaviorList
{
public:
    FMissionMetaBehaviorList() = default;
    // FUN_06f20230: the elements copied, each AddRef'ed.
    FMissionMetaBehaviorList(FMissionMetaBehaviorList const& Other);
    FMissionMetaBehaviorList& operator=(FMissionMetaBehaviorList const&) = delete;
    // Release every element, then erase them (06f18560-06f1859b).
    ~FMissionMetaBehaviorList();

    std::list<FMetaBehavior*> Items;
};

// "cargo = good, count" of a mission ship: the 8-byte element of the slot's +0x6c vector
// (FUN_06f18eb0 06f1921f-06f1926b, appended by FUN_06f96770).
struct FMissionShipCargo
{
    unsigned int GoodId;                              // +0x00 pub::GetGoodID of value 0
    int Count;                                        // +0x04 ftol(get_value_float(1))
};

// One ship of a [MsnShip]/[MsnFormation] record: the 0x88-byte element of the record's +0xe8
// vector. Vtable 06fb60a8 (one slot: the deleting dtor FUN_06ed2c00 -> FUN_06ed2c20). No RTTI in
// Content; the class name is the port's.
class FMissionShip
{
public:
    // The template of FUN_06f18eb0 (06f18ed5-06f18f93): empty nickname, +0x38 ID_String::clear,
    // +0x3c list, +0x48/+0x49/+0x50 = 0, +0x4c = 0, +0x54 clear, +0x58 list (FUN_06f1d4c0),
    // +0x64 clear, +0x68 = null (FUN_06f1d740(0)), +0x6c vector (FUN_06ead6a0), +0x7c/+0x80 = -1,
    // +0x84 = 7.0f (0x40e00000), vtable 06fb60a8.
    FMissionShip();
    // FUN_06ed2a60 (placement copy, cdecl (dest, src)): member-wise, the label list and the cargo
    // copied, the directives AddRef'ed (FUN_06f20230), the guide AddRef'ed (slot 0).
    FMissionShip(FMissionShip const& Other);
    // FUN_06ed34e0 (RET 4): the same members assigned; the old directives and guide released.
    FMissionShip& operator=(FMissionShip const& Other);
    // FUN_06ed2c20: the body releases the guide (slot 1) and clears it; then the members: the
    // cargo buffer freed, the guide handle (already null), the directives released, the label
    // list freed.
    virtual ~FMissionShip();

    TString<0x30> Nickname;                           // +0x04 the record's "nickname"
    ID_String NpcId;                                  // +0x38 "npc" ([NPC] block, map +0x184)
    std::list<unsigned int> LabelIds;                 // +0x3c CreateID of the nickname and labels
    unsigned char bJumper;                            // +0x48 "jumper" (FUN_06f0f150 msg 0x1010)
    // +0x49: "jumper" as well; the save writes it as "original_jump_flag" (FUN_06f0ff90) and
    // message 0x39 copies it back into bJumper (06f0f68e).
    unsigned char bOriginalJumpFlag;
    // +0x4c: the ship's [ObjList] priority (template 0): the snapshot FUN_06f130b0 stores the
    // NPC's priority (FUN_06f57c80), the save writes "ol_priority", FUN_06f15e20 hands it to the
    // NPC ship parameters (+0x70).
    int Priority;
    unsigned char bRandomName;                        // +0x50 "random_name"
    ID_String InitObjectives;                         // +0x54 "init_objectives" (when +0x64 is empty)
    FMissionDirectiveList Directives;                 // +0x58 TODO: writer not found in FUN_06f18eb0
    ID_String InitMetaBehavior;                       // +0x64 "init_metabehavior" (when +0x54 is empty)
    // +0x68: the refcounted guide, a ref-holding member (TRefHandle): the copy ctor FUN_06ed2a60
    // unwinds it with FUN_06f28e10 (state 4 of FuncInfo 06fbf610) and the dtor runs it after its
    // own body; the template's FUN_06f1d740(0) is the empty handle. 06f13ae2-06f13b0e (TU38)
    // copies an NPC ship's +0x178 guide here with Release/AddRef.
    FContentGuideHandle Guide;
    std::vector<FMissionShipCargo> Cargo;             // +0x6c "cargo"
    int HitPoints;                                    // +0x7c "hit_points" (-1)
    int Field_0x80;                                   // +0x80 "hit_points" as well (-1). TODO: meaning
    float Radius;                                     // +0x84 "radius" (7.0f); "arrival_obj" zeroes it
};

// One [MsnShip] or [MsnFormation] section: the value of a mission +0xb8 list node (0xf8 bytes,
// ctor FUN_06f0e2d0, appended by FUN_06f1e750, filled by FUN_06f18eb0). A [MsnShip] keeps its
// one ship slot; a [MsnFormation] drops its own slot ("formation" / "ship" keys) and takes a copy
// of the first slot of each [MsnShip] named by "ship", whose record then leaves the list.
struct FMissionShipGroup
{
    // FUN_06f0e2d0 (06f0e2d0-06f0e3d6): empty TStrings, identity matrix at +0x38, position and
    // +0x68 zero, +0xa0 = 2500.0f, +0xa4/+0xa5 = 0, +0x34 = 0, +0xdc = 0, +0xe0 = -1,
    // +0xe4 = CreateID("transport_liberty"), the +0xe8 vector empty.
    FMissionShipGroup();

    TString<0x30> Nickname;                           // +0x00 "nickname"
    FSystemInfo* System;                              // +0x34 "system" (FUN_06f75f30, only when found)
    FMatrix Orientation;                              // +0x38 "orientation"
    Vector Position;                                  // +0x5c "position"
    float RelPosHeading;                              // +0x68 "rel_pos" value 0 in radians
    TString<0x30> RelPosObj;                          // +0x6c "rel_pos" value 1
    float RelPosRange;                                // +0xa0 "rel_pos" value 2 (2500.0f)
    unsigned char bRelPos;                            // +0xa4 set by "rel_pos"
    unsigned char bOrientation;                       // +0xa5 set by "orientation"
    TString<0x30> ArrivalObj;                         // +0xa8 "arrival_obj" value 0
    // +0xdc (ctor 0): the SpaceObj the ships launch from - FUN_06f15e20 hands it to
    // CreateNpcShips as the dock target; the snapshot FUN_06f130b0 stores the jump gate of a
    // jumper that is out of every system; the save writes it as "arrival_obj = %u" in place of
    // the position and orientation when (id & 0x3fffffff) != 0.
    unsigned int ArrivalObjId;
    int Field_0xe0;                                   // +0xe0 "arrival_obj" value 1 (-1). TODO: meaning
    ID_String Formation;                              // +0xe4 "formation" (transport_liberty)
    std::vector<FMissionShip> Ships;                  // +0xe8
};

// One [MsnLoot] section: the value of a mission +0xd0 list node (0xfc bytes). The template is
// built on the stack at 06f17379-06f17403, appended by FUN_06f1e910 and the keys then write the
// new last node in place.
struct FMissionLoot
{
    // 06f17379-06f17403: TString lengths/first chars 0, +0x34/+0x38 = 0, LootInfo ctor
    // FUN_06f18c10, +0xd8..+0xe8 = 0, the +0xec list ctor FUN_06f93460, +0xf8 = SUBOBJ_ID_NONE,
    // +0xfa = 0.
    FMissionLoot();

    // FUN_06f19ca0 (06f19ca0-06f19cc7, RET 4): *LabelId is in LabelIds (+0xec).
    bool HasLabel(unsigned int const* LabelId) const;

    TString<0x30> Nickname;                           // +0x00 "nickname" (FUN_06ed0160)
    unsigned int SpaceObjId;                          // +0x34 the spawned loot, 0 until then
    unsigned int bSpawned;                            // +0x38 1 once created (FUN_06f16fa0 06f170df), 2 after msg 0x39
    // +0x3c: archetype -> EquipArchID (+0x40), string_id -> NameId (+0x44), velocity (+0x4c),
    // position (+0x58), orientation (+0x64), angular_velocity (+0x88), equip_amount -> Units
    // (+0x94), health -> HitPtsScale (+0x98).
    pub::SpaceObj::LootInfo Info;
    TString<0x30> RelPosObj;                          // +0xa4 "rel_pos_obj"
    Vector RelPosOffset;                              // +0xd8 "rel_pos_offset"
    unsigned char bRelPos;                            // +0xe4 set by "rel_pos_obj"
    unsigned int Field_0xe8;                          // +0xe8 TODO: meaning (template 0)
    std::list<unsigned int> LabelIds;                 // +0xec CreateID of the nickname and labels
    // +0xf8: SUBOBJ_ID_NONE; message 0x18 writes word parms+8 (FUN_06eb8f50 06eb9276-06eb927a).
    unsigned short SubObjId;
    // +0xfa: "can_jettison" stored inverted (get_value_bool; NEG/SBB/INC at 06f1785c).
    unsigned char bCannotJettison;
};

// One [MsnSolar] section: the value of a mission +0xc4 list node (0x4c8 bytes). Template
// 06f178b6-06f17984, appended by FUN_06f1e830; the keys write the new last node in place.
struct FMissionSolar
{
    // 06f178b6-06f17984: +0/+4 = 0, the Personality ctor, +0x3e0 = 0, Radius 150.0f, empty
    // nickname, identity matrix (FUN_06efc7a0), position and ids 0, +0x458/+0x45c = -1, the
    // label list, the costume (FUN_06eac5b0), the FmtStr (FUN_06eac680), +0x4c4 = 0.
    FMissionSolar();

    // FUN_06f19c70 (06f19c70-06f19c97, RET 4): *LabelId is in LabelIds (+0x46c).
    bool HasLabel(unsigned int const* LabelId) const;

    unsigned int SpaceObjId;                          // +0x000 the spawned solar, 0 until then
    // +0x004: 1 once created (FUN_06f169a0 06f16da8), 0 again when the mission's objects are
    // destroyed (FUN_06f171a0), 2 when the solar left space (message 0x39, 06f0f7ce).
    unsigned int bSpawned;
    pub::AI::Personality Personality;                 // +0x008 "pilot" (FUN_06f3e020)
    unsigned char bPilot;                             // +0x3e0 1 when the pilot was found
    float Radius;                                     // +0x3e4 "radius" (default 150.0f)
    TString<0x30> Nickname;                           // +0x3e8 "nickname"
    FMatrix Orientation;                              // +0x41c "orientation"
    Vector Position;                                  // +0x440 "position"
    unsigned int ArchetypeId;                         // +0x44c "archetype" (pub::GetNicknameId)
    unsigned int SystemId;                            // +0x450 "system" (pub::GetSystemID)
    unsigned int LoadoutId;                           // +0x454 "loadout" (pub::GetLoadoutID)
    int Field_0x458;                                  // +0x458 TODO: meaning (template -1)
    unsigned int Faction;                             // +0x45c "faction" reputation group (-1)
    unsigned int BaseId;                              // +0x460 "base" (pub::GetBaseID)
    unsigned int VoiceId;                             // +0x464 "voice" (pub::GetVoiceID)
    unsigned int Field_0x468;                         // +0x468 TODO: meaning (template 0)
    std::list<unsigned int> LabelIds;                 // +0x46c CreateID of the nickname and labels
    Costume SpaceCostume;                             // +0x478 "costume" (FUN_06f31390)
    FmtStr Name;                                      // +0x4ac "string_id" (begin/end_mad_lib)
    unsigned char Visit;                              // +0x4c4 "visit = unknowable" -> 0x80
};

// One formation arm of a [MsnRandEnc]: the value of a record +0x30 list node (node 0x20, value
// copied by FUN_06f21b90). Built by FUN_06f14e80 (06f14f45-06f1501b), read by FUN_06f15490.
struct FMissionRandEncArm
{
    ID_String Name;                                   // +0x00 CreateID("%s_frm%d")
    unsigned int FormationRef;                        // +0x04 the NPC ship's +0x154
    unsigned int Id;                                  // +0x08 the same CreateID, pushed on npc +0x17c
    std::list<unsigned int> MemberIds;                // +0x0c SpaceObj ids of the arm's ships
};

// One [MsnRandEnc] section: the value of a mission +0x98 list node (0x48 bytes; stack template
// 06f17ee9-06f17f55, copied into the list by FUN_06f21c50). The record holds one reference on
// each of its two TU31 objects.
struct FMissionRandEnc
{
    // 06f17ee9-06f17f55: empty nickname, bEnabled 0, the id cleared, counter 0, empty arms,
    // no objects, NpcCount 0.
    FMissionRandEnc();
    // FUN_06f21c50 (placement copy, cdecl (dest, src)): member-wise, the arms copied
    // (FUN_06f1e5e0 / FUN_06f21b90), Params and Populator AddRef'ed (slot 0) when set by their
    // handle copies. Unwind map 06fc2d1c: state 1 frees the arms (06fadde1 -> FUN_06ed37d0),
    // state 2 releases the Params handle (06faddec -> FUN_06f28e10) while Populator is copied.
    FMissionRandEnc(FMissionRandEnc const& Other) = default;
    FMissionRandEnc& operator=(FMissionRandEnc const&) = delete;
    // FUN_06ed3710 (the +0x98 node value dtor, implicit): the member handles release Populator
    // (+0x40) then Params (+0x3c) (slot 1), then the arms are freed. Unwind map 06fbf6c0:
    // state 1 releases Params (06fab1ab -> FUN_06f28e10), state 0 frees the arms (06fab1a0).
    // Retail's inlined handle dtor also nulls the field; the port's TRefHandle does not (the
    // record is dead at that point).
    ~FMissionRandEnc() = default;

    TString<0x20> Nickname;                           // +0x00 "nickname" (FUN_06ea9f40, max 0x20)
    unsigned char bEnabled;                           // +0x24 ctor 0; Act_EnableEnc / Act_DisableEnc
    ID_String NicknameId;                             // +0x28 CreateID of "nickname"
    unsigned int FormationIndex;                      // +0x2c "%s_frm%d" counter (FUN_06f14e80)
    std::list<FMissionRandEncArm> Arms;               // +0x30 (ctor FUN_06f1e640)
    TRefHandle<FRandEncParams> Params;                // +0x3c "encounter_type" (FUN_06efb830)
    TRefHandle<FRandEncPopulator> Populator;          // +0x40 "encounter_type" (FUN_06efb980)
    // +0x44: ships of the encounter created so far (FUN_06f14e80); zeroed when the encounter
    // triggers (FUN_06f148f0) and when all of them exist (06f152d7).
    unsigned int NpcCount;
};

// One entry of the mission's jumper list (+0x1c, node 0x10: value {npc, state}). A mission ship
// with "jumper" set follows the player through jump gates / holes: message 0x1010 appends
// {npc, 0} (06f0fa1f-06f0fa45), Act_Jumper appends / unlinks it. State (+4): 0 in the mission's
// system; 2 the player jumped and the NPC waits for the jump timer (FUN_06f149f0, the NPC taken
// out of its system); 1 the NPC jumped on its own (FUN_06f149f0 NPC branch). FUN_06f14b40 brings
// state-2 NPCs in through the gate (state 0), FUN_06f14c10 places state-1 NPCs next to the
// launching object (state 0). The mission dtor deletes an NPC whose state is not 0 (06f0edb1-
// 06f0ede1: the NPC is out of every system then, so no system owns it).
struct FMissionJumper
{
    FContentNpcShip* Npc;                             // +0x00
    int State;                                        // +0x04
};

// One formation of a random encounter to create again after a load / player respawn: the value
// of a mission +0x8c list node (0x74 bytes, value 0x6c; copy FUN_06f21a60, dtor FUN_06f229c0 ->
// FUN_06f11a40). Written by the mission snapshot FUN_06f130b0 (one record per formation group
// of live encounter ships) and the save load FUN_06f106e0 ([MsnRandEncSave]); saved by
// FUN_06f0ff90; read by FUN_06f145d0, which hands it to the encounter's TU31
// FRandEncParams::SpawnFormation (FUN_06efba30).
struct FMissionRandEncRespawn
{
    // The template of FUN_06f130b0 (06f135ac-06f13618): the id cleared, FormationRef -1, the
    // placement identity (FUN_06f143d0), Count 0, the three lists empty (FUN_06f11a10,
    // FUN_06f1dc40, FUN_06f1d4c0), Guide null, HitPoints -1. Retail leaves Priority unset (every
    // writer stores it); the port starts it at 0.
    FMissionRandEncRespawn();

    ID_String EncounterId;                            // +0x00 the [MsnRandEnc] NicknameId (+0x28)
    // +0x04: the NPC ships' +0x154 (the formation group they were created in); ships of one
    // group add to the same record (FUN_06f130b0 06f13350-06f13360).
    unsigned int FormationRef;
    FTransform Placement;                             // +0x08 SpawnFormation's placement
    int Count;                                        // +0x38 SpawnFormation's ship count
    // +0x3c: SpawnFormation's per-ship classes: one {ship's ClassIds, 1.0f} per ship (the
    // element dtor FUN_06f1f980 frees the vector at its head).
    std::list<FRandEncShipChoice> Choices;
    std::list<TString<0x30>> Names;                   // +0x48 SpawnFormation's ship names
    FMissionDirectiveList Directives;                 // +0x54 released by the dtor (06f11a68-06f11af8)
    // +0x60: the leader's guide, a raw pointer - the snapshot stores it without AddRef
    // (06f134dc), the copy FUN_06f21a60 copies the dword and the dtor FUN_06f11a40 never
    // releases it. The save writes its [MetaBehavior] line (guide +0x18).
    FContentGuide* Guide;
    int Priority;                                     // +0x64 SpawnFormation's priority ("ol_priority")
    int HitPoints;                                    // +0x68 the leader's hit points ("hit_pts")
};

class FMissionObject;

// ---------------------------------------------------------------------------------------
// Mission objective base (vtable 06fb8ccc, 7 slots; base ctor FUN_06f19f90, dtor FUN_06f1a5c0).
// Slots 1-5 are purecall (LAB_06fa9030) in the base. Eight derived classes, one per type id
// (slot 4), are built by the factory FUN_06f19cd0; the INI [NNObjective] loader
// (FUN_06f1a630) names types 1-4 "base", "navmarker", "ids", "rep_inst". Retail has no RTTI for
// them; the port names 1-4 after those keys and 5-8 by their type id.
// ---------------------------------------------------------------------------------------
class FMissionObjective
{
public:
    // FUN_06f19f90 (base part, RET 4): +4 = 0, +8 ID_String::clear, +0x14 = mission, +0xc = 0,
    // +0x10 = -1, +0x18 = 0, the +0x1c MissionObjective (Type 1, FmtStr zeroed), then vtable
    // 06fb8ccc, Type = 4, and +0x18 = mission +8 when the mission is non-null (06f19fed-06f1a007).
    explicit FMissionObjective(FMissionObject* InMission);
    // The base part of every slot 5 clone (e.g. 06f1a044-06f1a07b): +4..+0x1c copied dword by
    // dword, FmtStr copy ctor at +0x20.
    FMissionObjective(FMissionObjective const& Other);
    // Slot 0, 06f1a520 -> FUN_06f1a5c0 (vtable reset, ~FmtStr at +0x20) -> operator delete.
    virtual ~FMissionObjective();
    // Slot 1 (+0x04, RET 4): read the rest of the "type" key (values 1..).
    virtual void Parse(INI_Reader* Reader) = 0;
    // Slot 2 (+0x08, RET 4): write the "type" values after "type = %d, " (FUN_06f1a8e0).
    virtual void Save(FILE* File) = 0;
    // Slot 3 (+0x0c): rebuild the FmtStr at +0x20 from the fields; every override returns 1.
    virtual bool BuildDescription() = 0;
    // Slot 4 (+0x10): the type id 1..8.
    virtual int GetType() = 0;
    // Slot 5 (+0x14): new copy (FUN_06f1d110 keeps it in the player's +0x28 list).
    virtual FMissionObjective* Clone() = 0;
    // Slot 6, FUN_06f1a980 (RET 4): Other->GetType() == GetType() and FmtStr +0x20 equal.
    virtual bool IsSame(FMissionObjective* Other);

    unsigned char bHidden;                                // +0x04 ("hidden"/"state = HIDDEN"; skipped by FUN_06f1d110)
    ID_String Nickname;                                   // +0x08 ("nickname", CreateID or number)
    int Ids;                                              // +0x0c mad-lib string id (1 -> Type = (Type & 0x80) | 9)
    int Index;                                            // +0x10 index sent to the client, -1 = none
    FMissionObject* Mission;                              // +0x14
    FContentPlayerState* PlayerState;                     // +0x18 (mission +8 at construction)
    pub::Player::MissionObjective Objective;              // +0x1c Type ("state"), +0x20 FmtStr
};

// Type 1, vtable 06fb8d20, 0x3c bytes: "base, <base nickname or id>, <ids>".
class FNNObjectiveBase : public FMissionObjective
{
public:
    // FUN_06f19cd0 case 1: FUN_06f19f90 + vtable; +0x38 left unset.
    explicit FNNObjectiveBase(FMissionObject* InMission);
    void Parse(INI_Reader* Reader) override;              // LAB_06f1a9c0
    void Save(FILE* File) override;                       // LAB_06f1aa80
    bool BuildDescription() override;                     // FUN_06f1aab0
    int GetType() override;                               // LAB_06ee9e10: 1
    FMissionObjective* Clone() override;                  // FUN_06f1a010

    unsigned int BaseId;                                  // +0x38
};

// Type 2, vtable 06fb8d04, 0x4c bytes: "navmarker, <system>, <ids>, <int>, x, y, z".
class FNNObjectiveNavMarker : public FMissionObjective
{
public:
    // FUN_06f19cd0 case 2: FUN_06f19f90 + vtable; +0x38..+0x4b left unset.
    explicit FNNObjectiveNavMarker(FMissionObject* InMission);
    void Parse(INI_Reader* Reader) override;              // LAB_06f1ab00
    void Save(FILE* File) override;                       // LAB_06f1abf0
    bool BuildDescription() override;                     // LAB_06f1ac50
    int GetType() override;                               // LAB_06ee9660: 2
    FMissionObjective* Clone() override;                  // FUN_06f1a0a0

    FmtStr::NavMarker Marker;                             // +0x38 system, +0x3c int, +0x40 position
};

// Type 3, vtable 06fb8ce8, 0x38 bytes: "ids, <ids>".
class FNNObjectiveIds : public FMissionObjective
{
public:
    explicit FNNObjectiveIds(FMissionObject* InMission);  // FUN_06f19cd0 case 3
    void Parse(INI_Reader* Reader) override;              // FUN_06f1aca0
    void Save(FILE* File) override;                       // LAB_06f1acc0
    bool BuildDescription() override;                     // LAB_06f1ace0
    int GetType() override;                               // LAB_06f5e540: 3
    FMissionObjective* Clone() override;                  // FUN_06f1a140
};

// Type 4, vtable 06fb8cb0, 0x80 bytes:
// "rep_inst, <system>, <ids>, <int>, x, y, z, <mission label>".
class FNNObjectiveRepInst : public FMissionObjective
{
public:
    // FUN_06f19cd0 case 4 (base ctor inlined 06f19dc8-06f19e08): +0x38 = 0, +0x3c byte = 0.
    explicit FNNObjectiveRepInst(FMissionObject* InMission);
    void Parse(INI_Reader* Reader) override;              // LAB_06f1ad30
    void Save(FILE* File) override;                       // LAB_06f1ae70
    bool BuildDescription() override;                     // LAB_06f1aee0
    int GetType() override;                               // LAB_06f25080: 4
    FMissionObjective* Clone() override;                  // FUN_06f1a1d0

    TString<0x30> Label;                                  // +0x38 (len), +0x3c chars
    FmtStr::NavMarker Marker;                             // +0x6c system, +0x70 int, +0x74 position
};

// Types 5 and 6 (vtables 06fb8c94 / 06fb8c78, 0x54 bytes): same slot 1-3 bodies, created by
// code (FUN_06f19cd0 with 5 from the slot 11 path builder LAB_06f1b9f0). Both derive straight
// from the base: the type 6 clone FUN_06f1a330 stores 06fb8ccc then 06fb8c78 with no 06fb8c94
// in between (retail keeps even the dead base vtable stores), so there is no 5 -> 6 base.
class FNNObjectiveType5 : public FMissionObjective
{
public:
    // FUN_06f19cd0 case 5/6: FUN_06f19f90, +0x38 = -1, +0x3c = 0, vtable.
    explicit FNNObjectiveType5(FMissionObject* InMission);
    void Parse(INI_Reader* Reader) override;              // LAB_06f1b030
    void Save(FILE* File) override;                       // LAB_06f1b0d0
    bool BuildDescription() override;                     // LAB_06f1b150
    int GetType() override;                               // LAB_06ee99a0: 5
    FMissionObjective* Clone() override;                  // FUN_06f1a280

    int RepInstance;                                      // +0x38 (-1 = none)
    unsigned int SystemId;                                // +0x3c
    FmtStr::NavMarker Marker;                             // +0x40
};

class FNNObjectiveType6 : public FMissionObjective
{
public:
    explicit FNNObjectiveType6(FMissionObject* InMission);
    void Parse(INI_Reader* Reader) override;              // LAB_06f1b030
    void Save(FILE* File) override;                       // LAB_06f1b0d0
    bool BuildDescription() override;                     // LAB_06f1b150
    int GetType() override;                               // LAB_06f250c0: 6
    FMissionObjective* Clone() override;                  // FUN_06f1a330

    int RepInstance;                                      // +0x38 (-1 = none)
    unsigned int SystemId;                                // +0x3c
    FmtStr::NavMarker Marker;                             // +0x40
};

// Type 7, vtable 06fb8c5c, 0x50 bytes: a ready FmtStr at +0x38; slots 1/2 are FUN_06ebf6f0
// (RET 4). Destructor FUN_06f1a560 (deleting 06f1a540) destroys +0x38 then +0x20.
class FNNObjectiveType7 : public FMissionObjective
{
public:
    // FUN_06f19cd0 case 7 (base ctor inlined), FmtStr inline ctor FUN_06eac680 at +0x38.
    explicit FNNObjectiveType7(FMissionObject* InMission);
    ~FNNObjectiveType7() override;
    void Parse(INI_Reader* Reader) override;              // FUN_06ebf6f0
    void Save(FILE* File) override;                       // FUN_06ebf6f0
    bool BuildDescription() override;                     // LAB_06f1b1e0
    int GetType() override;                               // LAB_06f250d0: 7
    FMissionObjective* Clone() override;                  // FUN_06f1a3e0

    FmtStr Text;                                          // +0x38
};

// Type 8, vtable 06fb8c40, 0x3c bytes: one int appended to the mad lib; slots 1/2 FUN_06ebf6f0.
class FNNObjectiveType8 : public FMissionObjective
{
public:
    explicit FNNObjectiveType8(FMissionObject* InMission); // FUN_06f19cd0 case 8
    void Parse(INI_Reader* Reader) override;              // FUN_06ebf6f0
    void Save(FILE* File) override;                       // FUN_06ebf6f0
    bool BuildDescription() override;                     // LAB_06f1b210
    int GetType() override;                               // LAB_06f1a480: 8
    FMissionObjective* Clone() override;                  // FUN_06f1a490

    unsigned int Value;                                   // +0x38
};

// FUN_06f19cd0 (cdecl): new objective of Type (1..8) into *Out, true when *Out is non-null.
// Any other type leaves *Out untouched.
bool __cdecl CreateMissionObjective(int Type, FMissionObject* Mission, FMissionObjective** Out);
// FUN_06f1d430 (cdecl): Text is non-null, non-empty and starts with a digit or '-'. The TU38
// parsers inline the same test on their TString copies.
bool __cdecl IsNumberText(char const* Text);
// FUN_06f1a610 (cdecl): delete *Objective (vslot 0 with 1) when set, clear it, return 1.
bool __cdecl DeleteMissionObjective(FMissionObjective** Objective);

class FMissionObjectiveList;
// FUN_06f1a630 (cdecl): one [NNObjective] section -> a new objective appended to List.
void __cdecl LoadNNObjective(INI_Reader* Reader, FMissionObject* Mission, FMissionObjectiveList* List);
// FUN_06f1a8e0 (cdecl): an [NNObjective] section per objective of List.
void __cdecl SaveNNObjectives(FILE* File, FMissionObjectiveList* List);

// Owning list of objectives, vtable 06fb479c (0x10 bytes: vtable, list +4). Slots: 06eb78e0
// deleting dtor (dtor 06eb7900: FUN_06f1cb40, then the nodes and the head freed), 06ebf720 RET,
// 06eb7890 erase range, 06eb7850 erase one. Ctor FUN_06f1ca70. The derived class
// FMissionOwnedObjectiveList (vtable 06fb478c) reuses every slot body.
class FMissionObjectiveList
{
public:
    FMissionObjectiveList();
    ~FMissionObjectiveList();
    // Inlined at 06f1cf09-06f1cf23 (FUN_06f1cee0): FUN_06f1ca70, then FUN_06f1caa0(Other).
    FMissionObjectiveList(FMissionObjectiveList const& Other);
    // FUN_06f1caa0 (RET 4): delete own elements, then append a slot 5 clone of each of Other's.
    FMissionObjectiveList& operator=(FMissionObjectiveList const& Other);
    // FUN_06f1cb40: delete every element and erase its node (vslot 3 per node).
    void Clear();
    // FUN_06f1cb80 (RET 8): delete and erase [First, Last). Retail's null Last means the node
    // after First; callers pass std::next(First) for that.
    void EraseRange(std::list<FMissionObjective*>::iterator First,
                    std::list<FMissionObjective*>::iterator Last);
    // FUN_06f1cbc0 (RET 0xc): insert before Where a clone of each of Source's objectives, each
    // clone's mission (+0x14) set to InMission.
    void InsertClones(std::list<FMissionObjective*>::iterator Where,
                      FMissionObjectiveList const* Source, FMissionObject* InMission);
    // FUN_06f1cc40 (RET 8): factory FUN_06f19cd0, push_back when created; returns the object.
    FMissionObjective* Add(int Type, FMissionObject* InMission);
    // FUN_06f1ccb0: delete and unlink the first element, if any.
    void PopFront();
    // FUN_06f1cd00: the first element's slot 4, 0 when empty.
    int GetFrontType();
    // FUN_06f1cd20: the first element, null when empty.
    FMissionObjective* GetFront();

    std::list<FMissionObjective*> Objectives;             // +0x04 (alloc byte, head +0x08, size +0x0c)
};

class FMissionPlayer;

// The derived objective list, vtable 06fb478c (0x14 bytes): the base plus +0x10. Ctor
// FUN_06eb7830 (FUN_06f1ca70, vtable, +0x10 = 0); built inline by FUN_06f1b6d0 (player +8 and
// +0x28), the DestroyMission ctor (06eef442, +0x248) and FUN_06f1cee0. Copy construction and
// assignment are the implicit ones (base part, then +0x10): 06f1cf28-06f1cf34 and
// 06f1cfa7-06f1cfff / 06f1d009-06f1d066 in FUN_06f1cee0. No user destructor (slot 0 is the
// base's 06eb78e0).
class FMissionOwnedObjectiveList : public FMissionObjectiveList
{
public:
    // +0x10: 0 from the ctor; the player ctor stores the player itself in its +8 list
    // (06f1b7d8). Retail never reads it except to copy it (FUN_06f1cee0).
    FMissionPlayer* Owner = nullptr;
};

// One leg of the NN path (+0x5c list value, 0x24 bytes; node 0x2c, list ctor FUN_06eb79a0).
// Written by the TU53 path builder FUN_06f5ea40; read by mission slot 11 (FUN_06f1b9f0): a
// kind 0 leg gives two type 5 objectives (at PointA with ObjectA's reputation, then at PointB
// with ObjectB's), a kind 1 leg one objective at PointB with the path target's reputation.
struct FNnPathLeg
{
    int Kind;                                             // +0x00
    Vector PointA;                                        // +0x04
    Vector PointB;                                        // +0x10
    unsigned int ObjectA;                                 // +0x1c
    unsigned int ObjectB;                                 // +0x20
};

// The NN path objective state (0x88 bytes), held by the TU21 script mission at +0x21c. Built
// by the mission ctor (06eb7692-06eb76e9: FUN_06eb7830 = the objective list with vtable
// 06fb478c plus +0x10 = 0, FUN_06eb7810(0,0,0) on +0x14, +0x20..+0x2c zero, FUN_06eb79a0 list on
// +0x5c, +0x68/+0x6c = 1, the rest zero), filled by Act_NNPath (LAB_06ecc610) or the trigger
// load (FUN_06f1c430), driven by the mission's messages (FUN_06f1be20 from FUN_06eb8f50).
// Its code is TU38 (06f1be20-06f1ca49); Rebuild builds the legs with TU53 BuildNnPath
// (FUN_06f5ea40, SpacePaths.h).
struct FMissionNnPath
{
    // FUN_06f1be20 (RET 8): returns State.
    int HandleMessage(pub::Controller::EMessage MessageId, void const* MessageParms);
    // FUN_06f1c3f0 (RET 4): "nnpath = ..." line of the trigger save.
    void Save(FILE* File);
    // FUN_06f1c430 (RET 8): the "nnpath" key of the trigger load; starts the path.
    void Load(INI_Reader* Reader, FMissionObject* InMission);
    // FUN_06f1c530: resolve the destination, rebuild the legs (FUN_06f5ea40) and the type 5
    // objectives (mission slot 11).
    void Rebuild();
    // FUN_06f1c7c0 (RET 8): send the path objectives (all but the first with bSkipFirst) to the
    // mission's player; bLaunched picks the first objective's type when the path is running.
    void SendObjectives(bool bSkipFirst, bool bLaunched);
    // FUN_06f1c990 (06f1c990-06f1ca49).
    void Reset();

    FMissionOwnedObjectiveList Objectives;                // +0x00 (+0x10 Owner stays 0; the reset does not touch it)
    Vector Position = {0.0f, 0.0f, 0.0f};                 // +0x14 Act_NNPath f2..f4 or the target's position
    unsigned int TargetObjectId = 0;                      // +0x20 resolved from Label
    float TargetRadius = 0.0f;                            // +0x24 pub::SpaceObj::GetRadius of the target
    TString<0x30> Label = {0, {0}};                       // +0x28 Act_NNPath s2 (object label)
    std::list<FNnPathLeg> Legs;                           // +0x5c
    int TravelIds = 1;                                    // +0x68 Act_NNPath i0: ids of the path objectives
    int TargetIds = 1;                                    // +0x6c Act_NNPath i1: ids once the target is one leg away
    unsigned int SystemId = 0;                            // +0x70 Act_NNPath s3/s5 (else the player's system)
    unsigned char bActive = 0;                            // +0x74 Act_NNPath / Load 1, reset 0
    // +0x78: 1 started (Act_NNPath / Load), 2 first heartbeat seen, 3 timer running, 4 docked
    // (msg 2 / 0x16), 5 arrived (Rebuild: one leg left in the target's system).
    int State = 0;
    unsigned int TimerId = 0;                             // +0x7c pub::SetTimer id (DAT_06fce76c)
    FMissionObject* Mission = nullptr;                    // +0x80 Act_NNPath writes the mission
    unsigned char bArrived = 0;                           // +0x84 set once with State 5
};

// Mission player record held in +0x3c (0x3c bytes, vtable 06fb85b8: 0 06f0e2b0 deleting dtor,
// 1 FUN_06f1d3d0, 2 LAB_06f1d420). Constructed inline by slot 2 FUN_06f1b6d0.
class FMissionPlayer
{
public:
    // 06f1b77f-06f1b7d8: +4 = id, the +8 list (its Owner 0), +0x1c = mission, +0x20 =
    // FUN_06f49b00(id), +0x24 = +0x25 = 0, the +0x28 list (its Owner 0), vtable, then the +8
    // list's Owner = this.
    FMissionPlayer(unsigned int InPlayerId, FMissionObject* InMission);
    // 06f0e2b0 -> FUN_06f1cd40: the body deletes the +0x28 objectives (06f1cd69-06f1cd9f),
    // then the members: +0x28, +8.
    virtual ~FMissionPlayer();
    // Slot 1, FUN_06f1d3d0 (RET 4): +0x25 = b; a false value deletes the +0x28 objectives.
    virtual void SetField25(bool bValue);
    // Slot 2, LAB_06f1d420: return +0x25.
    virtual bool GetField25();

    // FUN_06f1d110: send the player's objective list (pub::Player::SetMissionObjectives).
    void SendObjectives();
    // FUN_06f1cea0: delete the +8 objectives (FUN_06f1cb40's loop inlined).
    void ClearObjectives();
    // FUN_06f1cee0: send a list holding only a new type 7 objective (Type 0x89) in place of
    // the current objectives, then restore clones of them.
    void SendPlaceholderObjective();

    unsigned int PlayerId;                                // +0x04
    FMissionOwnedObjectiveList Objectives;                // +0x08 (Owner +0x18 = this)
    FMissionObject* Mission;                              // +0x1c
    FContentPlayerState* PlayerState;                     // +0x20
    unsigned char Field_0x24;                             // +0x24 TODO: meaning (ctor 0)
    unsigned char Field_0x25;                             // +0x25 TODO: meaning (slots 1/2)
    FMissionOwnedObjectiveList SentObjectives;            // +0x28 (clones kept by FUN_06f1d110; Owner +0x38 = 0)
};

// [Invulnerables] row (list +0x14c, 0x44-byte nodes from FUN_06f1ed80, inserted at
// 06f116bf): nickname, then float at value+0x34 and bool at value+0x38.
struct FMissionInvulnerable
{
    TString<0x30> Nickname;
    float Duration;
    unsigned char bParam3;
};

// One attitude between two mission parties (the element of the mission +0x164 vector, 0x74
// bytes, trivially destroyed: the dtor only frees the buffer). The snapshot FUN_06f11ff0 takes
// the attitude of A towards B for every pair of live mission NPCs / spawned solars / the
// player / the +0x174 solar reputations; FUN_06f12c10 (message 0x36) sets them again after the
// player respawns. A party is named by its mission label (resolved with FUN_06f15b90), or, with
// an empty name, given by its reputation. The save writes "vibe = %s, %s, %u, %u, %f"
// ("empty" for an empty name).
struct FMissionVibeInfo
{
    TString<0x30> ObjectA;                                // +0x00
    TString<0x30> ObjectB;                                // +0x34
    int RepA;                                             // +0x68 used when ObjectA is empty
    int RepB;                                             // +0x6c used when ObjectB is empty
    float Attitude;                                       // +0x70 A towards B
};

// FUN_06f683b0 (TU53): pub::SpaceObj::GetType(Id) == 0 and (Type & 0xfdff) != 0. The second
// argument is pushed by FUN_06f159f0 but never read.
bool __cdecl IsSpaceObjTypeSet(unsigned int ObjId, FSystemInfo* Unused);
// FUN_06f68360 (TU53, cdecl): pub::Reputation::GetAttitude of FromId's reputation towards
// ToId's (pub::SpaceObj::GetRep of each) into *OutAttitude; true when GetAttitude returned 0.
bool __cdecl GetSpaceObjAttitude(unsigned int FromId, unsigned int ToId, float* OutAttitude);

// TU38 static initializers read outside MissionObject.cpp (defined there).
extern unsigned int const GIdLocalObjective;           // DAT_06fd06c4 CreateID("LOCAL_OBJECTIVE")
extern TString<0x30> const GPlayerLabel;               // DAT_06fd0690 "Player"
extern unsigned int const GIdMissionRandomEncounter;   // DAT_06fd068c CreateID("Mission_Random_Encounter")

// Retail TU38 base of every mission controller (see file comment).
// Declared slots: 0-28.
class FMissionObject : public IDLL
{
public:
    // FUN_06f0e3e0 (RET 8). Parms is only read as the allocator-proxy byte of the containers.
    FMissionObject(unsigned int const* InControllerId, void const* Parms);
    // Slot 1, FUN_06f0e770 -> FUN_06f0ed00 (member teardown) -> operator delete.
    ~FMissionObject() override;

    // Slot 0, FUN_06f0f100.
    void HandleMessage(pub::Controller::EMessage MessageId, void const* MessageParms) override;

    // Slot 2, FUN_06f1b6d0 (RET 4): the +0x3c record of the player, created on first use.
    virtual FMissionPlayer* GetOrCreatePlayer(unsigned int InPlayerId);
    // Slot 3, FUN_06f1b830 (RET 4): the +0x3c record of the player, or null.
    virtual FMissionPlayer* FindPlayer(unsigned int InPlayerId);
    // Slot 4, FUN_06f1b8c0 (RET 4): drop the record (objectives, resend, erase, delete).
    virtual void RemovePlayer(FMissionPlayer* Player);
    // Slot 5, FUN_06f1b8a0 (RET 4): slot 4 of slot 3.
    virtual void RemovePlayerById(unsigned int InPlayerId);
    // Slot 6, LAB_06f1b890: the first +0x3c record in key order, or null.
    virtual FMissionPlayer* GetFirstPlayer();

    // Slots 7-10: LAB_06ea1450 (XOR EAX,EAX; RET).
    virtual int Slot07();
    virtual int Slot08();
    virtual int Slot09();
    virtual int Slot10();
    // Slot 11, LAB_06f1b9f0 (RET 0x2c): replace the type 5 run of List (the first type 5 node and
    // the type 5 nodes after it) by two objectives per kind 0 leg / one per kind 1 leg of Legs,
    // inserted before *Where (List's begin when null). Empty Legs just drop the run; an unchanged
    // first leg point (every axis within 1.0 squared) keeps the list and returns false.
    virtual bool BuildPathObjectives(FSystemInfo* System, std::list<FNnPathLeg>* Legs,
                                     FMissionObjectiveList* List, int FirstIds, int LegIds,
                                     int TargetIds, unsigned int TargetObjectId, int FirstType,
                                     int LegType, int TargetType,
                                     std::list<FMissionObjective*>::iterator const* Where);
    // Slots 12..17:LAB_06ea1460 (MOV AL,1; RET), LAB_06ea1470 (XOR AL,AL; RET 4),
    // LAB_06ea1480 (XOR AL,AL; RET).
    virtual bool Slot12();
    virtual bool Slot13(unsigned int Arg);
    virtual bool Slot14();
    virtual bool Slot15();
    virtual bool Slot16(unsigned int Arg);
    virtual bool Slot17();
    // Slot 18, FUN_06f1ca50: +0xc = PlayerId, +8 = FUN_06f49b00(&PlayerId).
    virtual void SetPlayer(unsigned int InPlayerId);
    // Slot 19, FUN_06f1b4f0 (RET 8): objective resend timer (messages 2 / 0x20 / 0x36; TU21
    // override FUN_06eba010 calls it).
    virtual void UpdateObjectiveTimer(pub::Controller::EMessage MessageId, void const* MessageParms);
    // Slot 20, FUN_06ea1490 (RET 8).
    virtual void Slot20(unsigned int Arg0, unsigned int Arg1);
    // Slot 21, FUN_06f0f150: the TU38 message body (TU21 overrides with FUN_06eb8f50,
    // which calls this one directly).
    virtual void HandleMissionMessage(pub::Controller::EMessage MessageId, void const* MessageParms);
    // Slot 22, FUN_06f0fc40: remove every player, clear the mission tables, +0x19 = 1.
    virtual void EndMission();
    // Slot 23, FUN_06f0fda0 (TU21 override FUN_06eb9760 calls it last).
    virtual void OnMissionSucceed();
    // Slot 24, FUN_06f0fea0 (TU21 override LAB_06eb9870 calls it last).
    virtual void OnMissionFail();
    // Slot 25, FUN_06f172f0 (RET 4): read the mission ini's object sections ([MsnLoot],
    // [MsnSolar], [MsnShip]/[MsnFormation], [MsnRandEnc], [ObjList], [MetaBehavior], [Dialog],
    // [Mission], [NNObjective]); the TU21 override FUN_06eb84c0 calls it first.
    virtual void LoadMission(INI_Reader* Reader);
    // Slot 26, FUN_06eef520: -1 (TU21 override LAB_06eb8260 maps FAIL/SUCCEED/NO_STATE).
    virtual int MissionStateFromName(char const* Name);
    // Slot 27, LAB_06ea14a0: "NOT_IMPLEMENTED".
    virtual char const* MissionStateToName(int State);
    // Slot 28, FUN_06ebf6f0 (RET 4, empty; TU21 override FUN_06ebab90). The argument is a
    // message parms pointer: FUN_06f14b40 06f14bc8 passes &{npc SpaceObj id, system id} of a
    // jumper that arrived.
    virtual void Slot28(void const* Parms);

    // FUN_06f15940: SpaceObj id of a mission label ("Player" = the player's ship), or 0.
    unsigned int GetLabelShipId(TString<0x30> const* Label);
    // FUN_06f15710: live NPC object by nickname, else FUN_06f15840.
    FContentNpcShip* FindNpcByName(TString<0x30> const* Label);
    // FUN_06f15840: first usable NPC of the +0xb8 record named Label, else FUN_06f15490.
    FContentNpcShip* FindFormationNpc(TString<0x30> const* Label);
    // FUN_06f15490: first usable NPC of the random-encounter formation named Label.
    FContentNpcShip* FindRandEncFormationNpc(TString<0x30> const* Label);
    // FUN_06f15b90: ship (FUN_06f15940), else loot (+0xd0), else solar (FUN_06f159f0).
    unsigned int GetLabelObjectId(TString<0x30> const* Label);
    // FUN_06f15b10: spawned loot id by nickname (case-sensitive), or 0.
    unsigned int GetLootId(TString<0x30> const* Label);
    // FUN_06f159f0: spawned solar id by nickname, else the universe nickname id if it
    // names a live object.
    unsigned int GetSolarId(TString<0x30> const* Label);
    // FUN_06f156b0: the +0xa4 NPC whose SpaceObj id is *Id, or null.
    FContentNpcShip* FindNpcById(unsigned int const* Id);
    // FUN_06f15ae0: +0xd0 loot record whose spawned id equals *SpaceObjId, or null.
    FMissionLoot* FindLootById(unsigned int const* SpaceObjId);
    // FUN_06f15560 (RET 4): the first ship slot of any +0xb8 record whose nickname matches
    // (stricmp), or null.
    FMissionShip* FindMissionShip(TString<0x30> const* Nickname);
    // FUN_06f155f0 (RET 4): the costume of a mission ship (its "npc" block in +0x184) or, when no
    // ship has that nickname, of the [MsnSolar] named so (record +0x478); null when neither.
    Costume* GetMissionCostume(TString<0x30> const* Nickname);
    // FUN_06f159c0 (RET 4): the +0xc4 [MsnSolar] record whose SpaceObjId is *ObjId, or null.
    FMissionSolar* FindSolarById(unsigned int const* ObjId);
    // FUN_06f15c70 (RET 8, hidden return TString): the nickname of the mission object with
    // SpaceObj id *ObjId - an NPC (+0xa4, npc +0xd8), a solar (+0xc4), "Player" for the player's
    // ship, a loot (+0xd0); empty when none.
    TString<0x30> GetMissionObjectName(unsigned int const* ObjId);
    // FUN_06f199f0 (RET 0x3c, the name by value): pub::Player::MarkObj(player, ObjId, Mode); Mode 0
    // drops Name from the [MarkObj] list (+0x140), Mode 1 adds it when absent, any other mode
    // returns false. Returns false when MarkObj failed, true otherwise (and without a player).
    bool MarkMissionObject(unsigned int ObjId, int Mode, TString<0x30> Name);
    // FUN_06f14980 (RET 4): the +0x1c jumper entry whose NPC nickname equals Nickname (strcmp),
    // or null.
    FMissionJumper* FindJumperNpc(TString<0x30> const* Nickname);
    // FUN_06f149f0 (RET 4; message 0x25, parms = the jumping ship's id): the player's ship ->
    // its jump target into JumpSystemId / JumpGateId and every state-0 jumper leaves its system
    // (state 2); a mission NPC -> its jumper entry (by nickname) leaves its system (state 1).
    void OnShipJump(unsigned int const* ShipId);
    // FUN_06f14b40 (RET 4; message 0x20 with the frame time): while a jump is pending and the
    // timer has not passed JumpDelay, add DeltaTime; else bring every state-2 jumper in through
    // JumpGateId in JumpSystemId (slot 28 with {npc id, system id}) and reset the wait.
    void UpdateJumpers(float DeltaTime);
    // FUN_06f145d0 (RET 4; message 0x36): spawn the +0x128 records again (hit points reset
    // unless bKeepHitPoints) and the +0x8c formations (the matching [MsnRandEnc]'s
    // SpawnFormation), give the first ship of each record its guide or directives, then empty
    // both lists.
    void RespawnMissionObjects(bool bKeepHitPoints);
    // FUN_06f130b0 (messages 4 and 0x37; MissionSave.cpp): rebuild the respawn lists from the
    // live objects - +0x128 one [MsnShip] record per live mission ship / formation (its ships'
    // hit points, priority, guide or directives, the leader's place when it is in the player's
    // system), +0x8c one record per live random-encounter formation group, +0x134 the spawned
    // [MsnSolar]s of the player's system - then the attitudes (FUN_06f11ff0).
    void SnapshotMissionObjects();
    // FUN_06f11ff0 (MissionSave.cpp): +0x164 rebuilt with the attitudes between the live mission
    // NPCs, the spawned solars of the player's system, the player and the +0x174 reputations.
    void SnapshotVibeInfos();
    // FUN_06f12c10 (message 0x36; MissionSave.cpp): pub::Reputation::SetAttitude for every
    // +0x164 row.
    void ApplyVibeInfos();
    // FUN_06f0ff90 (RET 4; message 4; MissionSave.cpp): the [MsnShipSave] / [MsnWingSave] /
    // [MsnRandEncSave] / [MsnSolarSave] / [MarkObj] / [Invulnerables] / [Decloaked] /
    // [MsnVibeInfo] sections and the first player's [NNObjective]s.
    void SaveMission(FILE* File);
    // FUN_06f11f70 (RET 4; message 4; MissionSave.cpp): one [RandomEncounter] section per +0x98
    // record (its Params' Dump).
    void SaveRandEncs(FILE* File);
    // FUN_06f106e0 (RET 4; message 5; MissionSave.cpp): the save's sections back into +0x128 /
    // +0x8c / +0x98 / +0x134 / +0x140 / +0x14c / +0x158 / +0x164 (the reader reset first).
    void LoadMissionSave(INI_Reader* Reader);
    // FUN_06f11b90 (RET 4; MissionSave.cpp): the values of one [RandomEncounter] section.
    void LoadRandEncsSave(INI_Reader* Reader);
    // FUN_06f12e10 (RET 4; MissionSave.cpp): the "vibe" rows of [MsnVibeInfo].
    void LoadVibeInfos(INI_Reader* Reader);
    // FUN_06f15790 (RET 4; MissionSave.cpp): the first +0xb8 record whose nickname or one of whose
    // ships' nickname is Nickname (stricmp), or null.
    FMissionShipGroup* FindMissionShipGroup(TString<0x30> const* Nickname);
    // FUN_06f22f50 (RET 8; the comparator object is the mission): both ships' NPCs exist
    // (FUN_06f15710) and A's formation slot (+0x14c) is below B's (signed).
    bool IsShipBeforeInFormation(FMissionShip const* A, FMissionShip const* B);
    // FUN_06f14c10 (RET 4; message 0x3b, parms[0] a system id, parms[3] the object the player
    // launched from): every state-1 jumper is placed next to that object (a jump gate / hole:
    // 700 above it; a dock (0x800): at it; else at the player's ship, GetEmptyPos selection 3),
    // relocated into the system, loses its directives, slot 28 gets {npc id, system id, object
    // id}, state 0; a state-2 jumper sets bJumpPending.
    void PlaceJumpersAtLaunch(unsigned int const* Parms);
    // FUN_06f153a0 (RET 4; message 0x39): per [MsnRandEnc], the first arm holding *ObjId loses
    // it (an arm left empty is erased); a record without arms resets its arm counter.
    void RemoveRandEncMember(unsigned int const* ObjId);
    // FUN_06f14e30 (RET 4): unlink the first +0x1c jumper entry whose NPC SpaceObj id is *ObjId
    // (the entry is freed, the NPC is not).
    void RemoveJumperNpc(unsigned int const* ObjId);
    // FUN_06f148f0: offer the message to every enabled random encounter.
    void NotifyRandEncs(pub::Controller::EMessage MessageId, void const* MessageParms);
    // FUN_06f14e80: attach a newly created NPC to its random-encounter formation.
    void AttachRandEncNpc(void const* MessageParms);
    // FUN_06f14780 (prefix): register a newly created NPC (map +0xa4, invulnerable and
    // decloaked lists).
    void RegisterNpc(void const* MessageParms);
    // FUN_06f19b80: the mission object with SpaceObj id ObjId (NPC +0xa4, solar +0xc4, loot
    // +0xd0) carries the label *LabelId.
    bool HasObjectLabel(unsigned int ObjId, unsigned int const* LabelId);

    // FUN_06f1b260 (RET 8): in every player's list, the first objective named *NicknameId gets
    // hidden = !bVisible. True when any player had one.
    bool SetObjectiveVisible(unsigned int const* NicknameId, bool bVisible);
    // FUN_06f1b310 (RET 8): every objective named *NicknameId gets Type = State; the sent ones
    // (index > -1) also go to pub::Player::SetMissionObjectiveState.
    void SetObjectiveState(unsigned int const* NicknameId, int State);
    // FUN_06f1b3d0: delete every player's objectives and resend the empty lists.
    void ClearObjectives();
    // FUN_06f1b480: every objective of every player hidden.
    void HideAllObjectives();
    // FUN_06f1b610: FUN_06f1b670 for every player id.
    void RenumberAllObjectives();
    // FUN_06f1b670 (RET 4): renumber the visible objectives of the player (all but the first get
    // Type 10), the hidden ones index -1, then resend.
    void RenumberObjectives(unsigned int InPlayerId);
    // FUN_06f15df0 (RET 4): PlayerId is in DeadPlayerIds (+0xdc).
    bool IsPlayerDead(unsigned int InPlayerId);
    // FUN_06f15e20 (RET 4; callers Act_SpawnShip / SpawnShipRel / SpawnFormation 06ec68ff,
    // 06ec6f30, 06ec7374, 06ec73c1, TU38 06f1461a): one NPC ship parameter block per ship slot
    // of Group (its [NPC] block: reputation from a generated name or the individual_name,
    // npc_ship_arch, costume / voice, placement, labels, cargo, init_objectives /
    // init_metabehavior), laid out by the record's formation, created in the record's system or
    // the player's (TU58 CreateNpcShips) with the arrival_obj solar as dock target.
    void SpawnMissionShipGroup(FMissionShipGroup* Group);
    // FUN_06f169a0 (RET 4; caller FUN_06f171e0 06f17293): create a [MsnSolar] record's solar in
    // the player's system (id = pub::GetNicknameId of its nickname; moved to an empty spot when
    // its place is taken; reputation from its name and faction; the faction's costume / voice
    // when it has none), then its pilot state, labels, [Invulnerables], [Decloaked], [MarkObj]
    // and the system record's mission objects. False without a player / system or when
    // CreateSolar fails.
    bool SpawnMissionSolarRecord(FMissionSolar* Solar);
    // FUN_06f16fa0 (RET 4; callers Act_SpawnLoot 06ec7797 / 06ec77ff): create a [MsnLoot] loot in
    // the player's system (at rel_pos_obj + rel_pos_offset when set), then its labels and
    // [Invulnerables].
    void SpawnMissionLoot(FMissionLoot* Loot);
    // FUN_06f171e0 (RET 8; callers Act_SpawnSolar 06ec7116, FUN_06f0f150 06f0f3fd with null):
    // SpawnMissionSolarRecord for every [MsnSolar] named Name (stricmp), at Placement when given
    // (the record's own transform restored afterwards); the last result.
    bool SpawnMissionSolar(TString<0x30> const* Name, FTransform const* Placement);
    // FUN_06f171a0: every +0xc4 solar record's +4 = 0, then the player's system record destroys
    // its mission objects (TU58 FUN_06f746f0, tail jump). TU21 callers 06eb9832, 06eb996e.
    void DestroyMissionObjects();

    unsigned int ControllerId;                        // +0x04
    FContentPlayerState* PlayerState;                 // +0x08 (FUN_06f49b00 cache)
    unsigned int PlayerId;                            // +0x0c
    unsigned int Reward;                              // +0x10 cash split over the group by slot 23 (ctor 0)
    float HeartbeatInterval;                          // +0x14
    // +0x18: "trigger save pending". Writers (every byte write to +0x18 in TU21/TU38/Act code):
    // ctor 06f0e427 = 0; Act_Save::Execute 06ec483e = 1 (with +0x2e4 = the trigger id); TU21
    // message 4 06eb93d3 = 0 after the save. Readers: TU21 save 06ebabb0 writes [TriggerSave]
    // instead of the scripts, TU38 message 4 (06f0f150) skips the object save while set.
    unsigned char bTriggerSavePending;                // +0x18
    unsigned char bPendingDestroy;                    // +0x19
    std::list<FMissionJumper> Jumpers;                // +0x1c (FUN_06f1e440 insert, msg 0x1010)
    // +0x28 / +0x2c: pub::SpaceObj::GetJumpTarget of the gate the player's ship jumped through
    // (FUN_06f149f0): the destination system and the exit gate. Retail's ctor leaves them unset;
    // the port starts them at 0.
    unsigned int JumpSystemId = 0;                    // +0x28
    unsigned int JumpGateId = 0;                      // +0x2c
    // +0x30: a state-2 jumper waits (set by FUN_06f14c10, message 0x3b); +0x34 the seconds since
    // message 0x26 started the wait, -1.0f when idle (FUN_06f14b40 brings the jumpers in once it
    // passes JumpDelay).
    unsigned char bJumpPending;                       // +0x30 (ctor 0)
    float JumpTimer;                                  // +0x34 (ctor -1.0f)
    ID_String Field_0x38;                             // +0x38 TODO: meaning (ctor clear)
    std::map<unsigned int, FMissionPlayer*> Players;  // +0x3c
    // +0x50 / +0x68: FmtStr, ctor inlined (06f0e471-06f0e4ce zero Head, MadLibId and the
    // 14 counters, as Common's inline FmtStr ctor does); ~FmtStr at 06f0f0b4/06f0f0be.
    FmtStr Field_0x50;                                // +0x50 [Mission] "mission_title"
    FmtStr Field_0x68;                                // +0x68 [Mission] "mission_offer"
    // +0x80 (ctor 7): the mission type byte - the Type argument of
    // pub::Player::SetMissionObjectives (06f1d38d), the random mission's offer type
    // (DestroyMission ctor, RandomMissionDB completion counters). Not renamed yet: the RM lane's
    // files use the name.
    unsigned char Field_0x80;
    unsigned char bDestroyed;                         // +0x81 (FUN_06f0f100)
    float Field_0x84;                                 // +0x84 slot 19 accumulates dt (FADD)
    unsigned int Field_0x88;                          // +0x88 TODO: meaning (ctor 0)
    // +0x8c list (ctor FUN_06f1e5b0): random-encounter formations to create again, consumed by
    // FUN_06f145d0 (message 0x36).
    std::list<FMissionRandEncRespawn> RandEncRespawns;
    std::list<FMissionRandEnc> RandEncs;             // +0x98 [MsnRandEnc] (not loaded, see the type)
    // +0xa4 live NPC objects by SpaceObj id (FUN_06f14780 insert).
    std::map<unsigned int, FContentNpcShip*> NpcObjects;
    std::list<FMissionShipGroup> MissionShips;        // +0xb8 [MsnShip] / [MsnFormation]
    std::list<FMissionSolar> MissionSolars;           // +0xc4 [MsnSolar]
    std::list<FMissionLoot> MissionLoots;             // +0xd0 [MsnLoot]
    // +0xdc vector<uint> (ctor 06f0e59b-06f0e5ad): FUN_06f0f150 message 0x27 appends the player
    // whose ship is *parms (FUN_06f49bc0 -> +0xc, FUN_06f937c0), message 0x36 (space enter)
    // erases parms[1], messages 8/9 erase parms[0]. The name follows that use (a player whose
    // ship was lost and who has not entered space again) - a reading of the messages, not a
    // retail name.
    std::vector<unsigned int> DeadPlayerIds;
    // +0xec multimap (ctor FUN_06f1f4f0 with the multi flag 1): mission label id -> SpaceObj id
    // of each object carrying it. Filled by message 0x1010 (06f0fa50-06f0fa9a, FUN_06f003e0:
    // the NPC's +0x17c ids, which the spawn FUN_06f15e20 fills from the ship slot's labels),
    // the loot spawn FUN_06f16fa0 and the solar spawn FUN_06f169a0 (their records' labels); read
    // by FUN_06f14e80 (equal_range 06f150c1-06f1514c; the random-encounter arm ids) and the
    // Cnd_/Act_ label queries. The member name predates that reading.
    std::multimap<unsigned int, unsigned int> FormationMembers;
    // +0x100 map (ctor FUN_06f2d2f0, multi flag 0): [ObjList] nickname id -> its directives
    // (FUN_06f172f0 06f18245-06f183ea, insert FUN_06f1e9f0).
    std::map<unsigned int, FMissionDirectiveList> ObjLists;
    // +0x114 map (ctor FUN_06f2e290, multi flag 0): [MetaBehavior] nickname id -> its objects
    // (FUN_06f172f0 06f18404-06f185a0: LoadMetaBehavior FUN_06f065e0, insert FUN_06f1e9f0); the
    // mission ship spawn FUN_06f15e20 hands the first object of "init_metabehavior" to the NPC.
    std::map<unsigned int, FMissionMetaBehaviorList> MetaBehaviors;
    // +0x128 list (ctor FUN_06f1e800, value dtor FUN_06f227e0): [MsnShip] records to spawn again,
    // consumed by FUN_06f145d0 (message 0x36; SpawnMissionShipGroup on each).
    std::list<FMissionShipGroup> RespawnShips;
    // +0x134 list (ctor FUN_06ed0440): [MsnSolar] names to spawn again; message 6 spawns each
    // (FUN_06f171e0 with no placement, 06f0f3e9-06f0f40a) and empties the list.
    std::list<TString<0x30>> RespawnSolars;
    std::list<TString<0x30>> MarkObjNames;           // +0x140 [MarkObj] (06f115ee insert)
    std::list<FMissionInvulnerable> Invulnerables;    // +0x14c [Invulnerables] (06f116bf)
    std::list<TString<0x30>> DecloakedNames;          // +0x158 [Decloaked] (06f117a7 insert)
    // +0x164 vector (ctor 06f0e6ae-06f0e6c0, dtor 06f0ee13-06f0ee42): the attitudes taken by the
    // snapshot FUN_06f11ff0 / read by FUN_06f12e10, set again by FUN_06f12c10 (message 0x36),
    // saved as [MsnVibeInfo] (06f1060f-06f106a9).
    std::vector<FMissionVibeInfo> VibeInfos;
    // +0x174 vector<int> (ctor 06f0e6ca-06f0e6dc, dtor FUN_06eec8d0 06f0ee03): solar reputations
    // - TU21 06ec9b30 appends pub::SpaceObj::GetSolarRep(obj) once each (unique push_back).
    std::vector<int> SolarReps;
    // +0x184 map<uint, FMsnNpc> (ctor FUN_06f1f0c0): the mission ini's [NPC] blocks by nickname
    // id, inserted by FUN_06eb84c0 (06eb8764, FUN_06ed04c0); FUN_06f155f0 reads the costume.
    std::map<unsigned int, FMsnNpc> MissionNpcs;
    // +0x198 (ctor FUN_06f1d4f0): the [Mission] npc_ship_file table (FUN_06f32510 at 06f18910).
    FNpcShipArchMap NpcShipArchs;
    // +0x1ac (ctor 3): seconds the state-2 jumpers wait (FILD against JumpTimer, FUN_06f14b40).
    int JumpDelay;
};
