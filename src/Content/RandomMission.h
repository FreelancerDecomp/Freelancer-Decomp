// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "BaseDB.h"
#include "ContentRefObject.h"       // FContentRefObject
#include "RandomMissionGen.h"
#include "RandomMissionOffer.h"     // FRmOffer (the +0x7c offers, TRmRef<FRmOffer>)
#include "common_server_controller.h"
#include "common_text.h"

#include <cstdio>
#include <list>
#include <vector>

class INI_Reader;
struct FmtStr;
struct FSystemInfo;
struct MissionVendorOfferDescription;

// Random mission (docs/re/content.md, TU map, lane CNT-RM): the mission data of TU47 (base) and
// TU27 (06eef190-06ef2070, DestroyMission.cpp), and the TU47 loader. Shared with the
// GameSupport lane (bar mission offers FUN_06ea8370, TU35 FUN_06f041c0, player state load
// FUN_06f483d0).

// ---------------------------------------------------------------------------------------
// TU47 -- random mission data base (retail 0x54 bytes, ctor FUN_06f4d2e0, dtor FUN_06ef0570,
// deleting dtor FUN_06f4d370). pub::Controller::CreateParms at +0 (vtable 06fbb388:
// [0] deleting dtor, [1] Save FUN_06f4d390, [2] LoadValue FUN_06f4d420) and the ref-counted
// interface at +4 (vtable 06fbb37c: AddRef FUN_06eacd40, Release FUN_06eacd50, this-4 thunk
// FUN_06f4ed50 to the deleting dtor), count 1 at +8. Bodies in RandomMissionDB.cpp.
// ---------------------------------------------------------------------------------------
class FRmMissionDataBase : public pub::Controller::CreateParms, public FContentRefObject
{
public:
    unsigned int PlayerId = 0;              // +0x0c
    unsigned int DestSys = 0;               // +0x10 "dest_sys = %u"
    int OfferGroup = -1;                    // +0x14 "offer_group = %d"
    unsigned int EnemyGroup = 0xffffffffu;  // +0x18 "enemy_group = %u" (saved by TU27)
    unsigned int OfferBase = 0;             // +0x1c "offer_base = %u"
    int OfferIds = 0;                       // +0x20 "offer_ids = %d"
    FmtStr Description;                     // +0x24 inline default ctor 06f4d30d-06f4d33a
    int Reward = 0;                         // +0x3c "reward = %d"
    float RepReward = 0.0f;                 // +0x40 "rep_reward = %g"
    float RepHit = 0.0f;                    // +0x44 "rep_hit = %g"
    // +0x48 DAT_06fbb238 (360.0f): seconds during which the mission object keeps the players'
    // +0xd0 system flags up to date (FUN_06f4bf30 messages 0x20 / 0x36 / 0x3b). Not saved.
    float FlagTime = 360.0f;
    float Difficulty = 0.0f;                // +0x4c "difficulty = %g" (DAT_06fbb224)
    int JumpPref = 0;                       // +0x50 "jump_pref = %d"

    FRmMissionDataBase() = default;         // FUN_06f4d2e0
    ~FRmMissionDataBase() override;         // FUN_06ef0570
    // Slot 1 (FUN_06f4d390, RET 4).
    virtual void Save(FILE* File);
    // Slot 2 (FUN_06f4d420, RET 4): one key of a [RandomMission] section, first match wins.
    virtual void LoadValue(INI_Reader* Reader);
};

// ---------------------------------------------------------------------------------------
// TU27 -- the random mission (retail 0x88 bytes, vtable 06fb7744: [0] deleting dtor
// FUN_06ef0550 over FUN_06ef06d0, [1] Save FUN_06ef0810, [2] LoadValue LAB_06ef0890;
// secondary vtable 06fb7738 with the this-4 thunk LAB_06ef2060). Built only by Create
// (FUN_06ef0600, the ctor is inlined there). It is the create parms of the
// "DestroyMission" controller (FUN_06eef3d0 AddRefs it).
// ---------------------------------------------------------------------------------------
struct FRandomMissionData final : public FRmMissionDataBase
{
public:
    Vector ObjectivePos = {};               // +0x54 "objective_pos = %g,%g,%g"
    unsigned int ObjectiveZone = 0;         // +0x60 "objective_zone = %u"
    // +0x64 "random_seed = %u". Retail's ctor leaves it as heap garbage (CreateMission
    // writes it, a save without the key keeps the garbage); the port starts it at 0.
    unsigned int RandomSeed = 0;
    FRmTreePath TreePath;                   // +0x68 vtable 06fb7750, written by BuildOffer
    // +0x7c: the offers built for this mission (FUN_06f1e390 push_back); each holds one
    // reference taken by FinalizeMission and dropped by the dtor. The DestroyMission ctor
    // and loader slot 6 read the first one.
    std::list<FRmOffer*> Offers;

    // FUN_06ef0600 (cdecl): new 0x88; *OutMission = it (0 when new fails). Always 1.
    static bool Create(FRandomMissionData** OutMission);

    ~FRandomMissionData() override;         // FUN_06ef06d0
    void Save(FILE* File) override;         // FUN_06ef0810
    void LoadValue(INI_Reader* Reader) override; // LAB_06ef0890

private:
    FRandomMissionData() = default;
};

// TRmRef (RandomMissionDB.h) interfaces: the random mission and the TU61 offer keep their
// ref-counted interface at +4 (FUN_06fa0330 releases (p + 4)).
inline void RmRefAddRef(FRmMissionDataBase* Object)
{
    static_cast<FContentRefObject*>(Object)->AddRef();
}

inline void RmRefRelease(FRmMissionDataBase* Object)
{
    static_cast<FContentRefObject*>(Object)->Release();
}

inline void RmRefAddRef(FRmOffer* Object)
{
    Object->AddRef();
}

inline void RmRefRelease(FRmOffer* Object)
{
    Object->Release();
}

// TU27 -- objective zones of the live random missions (DAT_06fcf1f8, a vector<uint>;
// DAT_06fcf258 holds its address: static init LAB_06ef04d0 / FUN_06ef0500, atexit
// FUN_06ef04f0 / FUN_06ef0520). A zone is excluded from new missions while listed.
extern std::vector<unsigned int> GRmObjectiveZones;

// ---------------------------------------------------------------------------------------
// Request of FRandomMissionLoader::CreateMission (4 dwords read at 06ef1ca9-06ef1cc7).
// ---------------------------------------------------------------------------------------
struct FRmMissionRequest
{
    unsigned int PlayerId;                      // [0] -> mission +0x0c
    unsigned int OfferBase;                     // [1] -> mission +0x1c
    int OfferGroup;                             // [2] -> mission +0x14
    float Difficulty;                           // [3] -> mission +0x4c
};

// ---------------------------------------------------------------------------------------
// Random-mission loader (retail 0x0c bytes, vtable 06fbb35c, built by FUN_06f4d220; the
// dtor FUN_06f4d280 resets the vptr to the ref-counted base 06fb43f8 = FContentRefObject).
// Slots: [0] AddRef FUN_06eacd40, [1] Release FUN_06eacd50, [2] deleting dtor FUN_06f4d260,
// [3] CreateMission FUN_06ef1c70, [4] Load LAB_06ef0b00, [5] FUN_06ebf6f0 (RET 4, empty),
// [6] FUN_06ef0be0 (RET 8), [7] LAB_06ef0dc0 (RET 0xc). Slots 3/4/6/7 and their helpers are
// in DestroyMission.cpp (TU27), the ctor / dtor in RandomMissionDB.cpp (TU47).
// ---------------------------------------------------------------------------------------
class FRandomMissionLoader : public FContentRefObject
{
public:
    FRandomMissionData* Mission = nullptr;      // +0x08 holds one reference

    ~FRandomMissionLoader() override;           // FUN_06f4d280

    // Slot 3 (RET 8). Builds a new random mission from Request into Mission and hands it
    // out through OutMission (no AddRef). Result codes seen by the callers (06f04286):
    // 0 success, 1 no enemy group, 2 no offer (FinalizeMission), 3 no objective zone.
    virtual int CreateMission(FRmMissionRequest const* Request,
                              FRandomMissionData** OutMission);
    // Slot 4 (RET 0xc): [RandomMission] of a saved player (caller FUN_06f483d0).
    virtual bool Load(INI_Reader* Reader, FRandomMissionData** OutMission,
                      unsigned int PlayerId);
    // Slot 5 (FUN_06ebf6f0: RET 4, no body). Both callers pass the mission CreateMission
    // handed out: the failed bar offer (FUN_06ea8370 06ea8785-06ea8790) and the mission
    // offer dtor (FUN_06ea7b90 06ea7bc2-06ea7bc8). TODO: name unknown.
    virtual void Slot05(FRandomMissionData* Mission);
    // Slot 6 (FUN_06ef0be0, RET 8): the mission vendor offer of Mission (caller TU35
    // 06f04986, then pub::GF::MissionVendorOfferCreate).
    virtual void GetOfferDescription(FRandomMissionData const* InMission,
                                     MissionVendorOfferDescription* OutDescription);
    // Slot 7 (LAB_06ef0dc0, RET 0xc): false (with a reason text and code 2) when the route
    // from the offer base to the mission's system is blocked. The first argument is not
    // read. Caller BaseSupport 06eb5163.
    virtual bool CanAccept(FRandomMissionData const* Unused, FmtStr* OutReason, int* OutCode);

private:
    // FUN_06ef09c0 (RET 4, the argument is not read): the offer of Mission; 0 or 2.
    int FinalizeMission(int Unused);
    // FUN_06ef0c90 (RET 0xc): true when no route leads from FromSystem to ToSystem for
    // PlayerId, or it passes a gate locked for him.
    bool IsRouteBlocked(unsigned int FromSystem, unsigned int ToSystem, unsigned int PlayerId);
    // FUN_06ef15b0 (RET 0x10): a random objective zone within Range of Position, 0 if none.
    unsigned int PickObjectiveZone(unsigned int PlayerId, FSystemInfo* System,
                                   Vector const* Position, float Range);
    // FUN_06ef0e30 / FUN_06ef0f20 / FUN_06ef1240 (RET 0x14): candidate zones of System,
    // along its trade lanes and through its jumps.
    void CollectZonesInRange(unsigned int PlayerId, FSystemInfo* System,
                             Vector const* Position, float Range,
                             std::vector<unsigned int>* OutZones);
    void CollectZonesAlongTradeLanes(unsigned int PlayerId, FSystemInfo* System,
                                     Vector const* Position, float Range,
                                     std::vector<unsigned int>* OutZones);
    void CollectZonesThroughJumps(unsigned int PlayerId, FSystemInfo* System,
                                  Vector const* Position, float Range,
                                  std::vector<unsigned int>* OutZones);
    // FUN_06ef1810 (RET 0xc): a random group hostile to OfferGroup populating System, or -1.
    unsigned int PickEnemyGroup(FSystemInfo* System, Vector const* Position, int OfferGroup);
};

// FUN_06f4d220: new loader (count 1, no mission) into *OutLoader; the type string is not
// read; always returns 1 (a failed new stores 0 and still returns 1).
bool __cdecl CreateRandomMissionLoader(char const* MissionType, FRandomMissionLoader** OutLoader);
