// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_ini.h"
#include "common_server_controller.h" // pub::Controller::EMessage (Matches)
#include "common_tstring.h"

#include <cstdio>
#include <list>

// Mission trigger conditions (TU21, Content.dll 06eb7360-06ed3d90).
// Every Cnd_* key in a [Trigger] section is one retail class created by the opcode
// factory FUN_06ebb850 and stored at trigger+0x04 (one condition per trigger). The factory
// also pushes the message id(s) the condition listens to into trigger+0x18; the id is
// noted on each class below ("event").
//
// Recurring retail idioms:
// - ID_String fields (ID_String::clear in the ctor) are held as unsigned int.
// - "len dword + char[N]" fields are TString<N> (Common): the ctor writes 0/0 and then
//   assigns "" (FUN_06ed0160 for N = 0x30, FUN_06ead6c0 for N = 0x20); every Parse copy
//   is the same clamp-to-N-1 assignment with a null source giving the empty string.
// - FUN_06f49b00(&mission+0x0c) followed by a read of the mission's cached player state
//   (+0x08): the port looks the state up with FindContentPlayerState(PlayerId) (see
//   MissionCnd.cpp GetMissionPlayerState).

class INI_Reader;
struct FMissionScript;

// Base condition. Vtable 06fb565c: +0x00 LAB_06ebf6d0 (scalar deleting dtor),
// +0x04/+0x08 purecall, +0x0c/+0x10 FUN_06ebf720 (RET), +0x14/+0x18 FUN_06ebf6f0 (RET 4).
// Fields: +0x04 owning mission (trigger+0x14), +0x08 ID_String cleared by the ctor and set
// to the owning trigger's id by the factory (trigger+0x2c).
// Derived classes use the dtor FUN_06ec44a0 (FUN_06ec44c0 restores the base vptr).
class Cnd
{
public:
    explicit Cnd(FMissionScript* InMission);
    virtual ~Cnd();

    // +0x04: reads the rest of the Cnd_* line.
    virtual void Parse(INI_Reader& Reader) = 0;
    // +0x08: FUN_06eb9af0 passes (event id, message parms); FUN_06eb82c0 passes (-1, null)
    // when the trigger is activated.
    virtual bool Matches(pub::Controller::EMessage EventId, void const* EventParms) = 0;
    // +0x0c: called by FUN_06eb82c0 on activation and by FUN_06ebb830 on re-arm.
    virtual void OnActivate();
    // +0x10: base FUN_06ebf720 (RET). Name unknown.
    virtual void Slot10();
    // +0x14: writes the condition state into the save file (Cnd_Timer LAB_06ec16c0 fprintf).
    virtual void Save(FILE* File);
    // +0x18: reads it back (Cnd_Timer FUN_06ec16e0 walks the current section).
    virtual void Load(INI_Reader* Reader);

    // FUN_06ebf700 (shared with Act by /OPT:ICF): +0x08 = id.
    void SetTriggerId(unsigned int Id);

    FMissionScript* Mission; // +0x04
    unsigned int TriggerId;  // +0x08 ID_String
};

// Cnd_BaseEnter: new(0x10), ctor inline in FUN_06ebb850 (06ebb8c0), vtable 06fb5770,
// event 1 (inline push at 06ebb8f4). Cnd_BaseExit: same class shape, vtable 06fb5744,
// event 2. Parse FUN_06ebf770, Matches LAB_06ebf750 (both shared).
class CndBaseEnter : public Cnd
{
public:
    explicit CndBaseEnter(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ebf770
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ebf750

    unsigned int BaseId; // +0x0c (ctor 0; "ANY" leaves 0 = any base)
};

class CndBaseExit : public Cnd
{
public:
    explicit CndBaseExit(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ebf770
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ebf750

    unsigned int BaseId; // +0x0c
};

// Cnd_CargoScanned: new(0x7c), ctor inline (06ebb9c2), vtable 06fb5710, event 3.
class CndCargoScanned : public Cnd
{
public:
    explicit CndCargoScanned(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ebf810
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ebf940
    void OnActivate() override;                                 // LAB_06ebf8f0

    TString<0x30> ScannerLabel; // +0x0c
    TString<0x30> ScannedLabel; // +0x40
    unsigned int Field_0x74;    // +0x74 (ctor 0) TODO: resolved id, written by OnActivate
    unsigned int Field_0x78;    // +0x78 (ctor 0) TODO: resolved id, written by OnActivate
};

// Cnd_CharSelect: new(0x38), ctor inline (06ebba73), vtable 06fb56e4, event 0x1b.
// Cnd_RumorHeard shares the Parse (FUN_06ebfa20) and the layout (vtable 06fb51dc).
class CndCharSelect : public Cnd
{
public:
    explicit CndCharSelect(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ebfa20
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ebfac0

    unsigned int LocationId;  // +0x0c pub::GF::FindLocation(base, s1)
    TString<0x20> Character;  // +0x10 s0
    unsigned int BaseId;      // +0x34 pub::GetBaseID(s2)
};

// Cnd_CommComplete: new(0x48), ctor inline (06ebbb0e), vtable 06fb56b4, event 0xb.
class CndCommComplete : public Cnd
{
public:
    explicit CndCommComplete(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec0ee0
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec0f90
    void OnActivate() override;                                 // FUN_06ec0f70

    TString<0x30> SourceLabel;  // +0x0c optional s1
    unsigned int CommId;        // +0x40 pub::GetNicknameId(s0)
    unsigned int SourceObjId;   // +0x44 FUN_06f15940(+0x0c)
};

// Cnd_CmpToPlane: new(0x58), ctor inline (06ebbb9f), vtable 06fb5688, event 0x20.
class CndCmpToPlane : public Cnd
{
public:
    explicit CndCmpToPlane(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec0ff0
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec10e0
    void OnActivate() override;                                 // FUN_06ec10c0

    TString<0x30> ObjectLabel; // +0x0c s1
    float PlaneA;              // +0x40 f2 (not set by the ctor)
    float PlaneB;              // +0x44 f3
    float PlaneC;              // +0x48 f4
    float PlaneD;              // +0x4c f5
    int Side;                  // +0x50 "inside" 0 / "outside" 1 (not set by the ctor)
    unsigned int ObjectId;     // +0x54 (ctor 0) FUN_06f15940(+0x0c)
};

// Cnd_Destroyed: new(0x54), ctor inline (06ebbc33), vtable 06fb5640, event 0x39.
class CndDestroyed : public Cnd
{
public:
    explicit CndDestroyed(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec0b90
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec0cf0
    void OnActivate() override;                                 // FUN_06ec0cc0
    void Save(FILE* File) override;                             // LAB_06ec0e70
    void Load(INI_Reader* Reader) override;                     // FUN_06ec0e90

    int Count;                 // +0x0c destroyed so far (OnActivate 0; not set by the ctor)
    int Needed;                // +0x10 (ctor -1) i1 when >= -1
    unsigned int Label;        // +0x14 ID_String CreateID(s0)
    TString<0x30> KillerLabel; // +0x18 optional s3
    unsigned int KillerObjId;  // +0x4c (ctor 0) FUN_06f15940(+0x18)
    int Mode;                  // +0x50 (ctor 0x3e6) EXPLODE 1, SILENT 0, ALL 0x3e7, ALL_IGNORE_LANDING 0x3e6
};

// Cnd_DistCircle: new(0xa4), ctor inline (06ebbce4), vtable 06fb5614, event 0x20.
class CndDistCircle : public Cnd
{
public:
    explicit CndDistCircle(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec0360
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec0530
    void OnActivate() override;                                 // FUN_06ec0410

    TString<0x30> ObjectLabel; // +0x0c s0
    TString<0x30> CircleObj;   // +0x40 s1
    unsigned int CircleObjId;  // +0x74 (ctor 0) pub::GetNicknameId(+0x44)
    // +0x78..+0xa3 are not set by the ctor; OnActivate fills them.
    Vector LastPosition;       // +0x78 object position of the previous tick
    Vector CircleCenter;       // +0x84 circle object location, then GetRadius's centre
    Vector Normal;             // +0x90 circle axis: orientation * (0,0,-1), normalised
    float Radius;              // +0x9c pub::SpaceObj::GetRadius
    float PlaneOffset;         // +0xa0 -dot(Normal, location)
};

// Cnd_DistShip: new(0x98), ctor inline (06ebbd4b), vtable 06fb55e8, event 0x20.
class CndDistShip : public Cnd
{
public:
    explicit CndDistShip(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec0720
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec0930
    void OnActivate() override;                                 // FUN_06ec08e0
    void Save(FILE* File) override;                             // LAB_06ec0b00
    void Load(INI_Reader* Reader) override;                     // FUN_06ec0b30

    TString<0x30> ObjectA;      // +0x0c s1
    TString<0x30> ObjectB;      // +0x40 s2
    float DistanceSq;           // +0x74 f3, squared when > 0 (not set by the ctor)
    unsigned char bInside;      // +0x78 (ctor 0) "inside" 1 / "outside" 0
    float TickTimer;            // +0x7c (OnActivate = +0x80)
    float TickInterval;         // +0x80 (ctor 0) f4 when > 0
    int TickMode;               // +0x84 (ctor 0) TICK_ALWAYS 0, TICK_TOWARDS 1, TICK_AWAY 2
    float LastDistanceSq;       // +0x88 (OnActivate 0) squared distance of the last tick
    unsigned int ObjectAId;     // +0x8c (ctor 0) FUN_06f15b90(+0x0c)
    unsigned int ObjectBId;     // +0x90 (ctor 0) FUN_06f15b90(+0x40)
};

// Cnd_DistVec: new(0x6c), ctor inline (06ebbdf2), vtable 06fb55c0, event 0x20.
class CndDistVec : public Cnd
{
public:
    explicit CndDistVec(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ebfb60
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ebfd20
    void OnActivate() override;                                 // FUN_06ebfcf0
    void Save(FILE* File) override;                             // LAB_06ebfe60
    void Load(INI_Reader* Reader) override;                     // FUN_06ebfe90

    float PositionX;           // +0x0c f2
    float PositionY;           // +0x10 f3
    float PositionZ;           // +0x14 f4
    TString<0x30> ObjectLabel; // +0x18 s1
    float DistanceSq;          // +0x4c f5, squared when not <= 0
    unsigned char bInside;     // +0x50 (ctor 0)
    float TickTimer;           // +0x54 (OnActivate = +0x58)
    float TickInterval;        // +0x58 (ctor 0) f6 when >= 0
    int TickMode;              // +0x5c (ctor 0)
    float LastDistanceSq;      // +0x60 (OnActivate 0) squared distance of the last tick
    unsigned int ObjectId;     // +0x64 (ctor 0) FUN_06f15940(+0x18)
};

// Cnd_DistVecLbl: new(0x30), ctor inline (06ebbe6e), vtable 06fb5594, event 0x20 (inline
// push at 06ebbecb).
class CndDistVecLbl : public Cnd
{
public:
    explicit CndDistVecLbl(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ebfef0
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec0280
    void OnActivate() override;                                 // LAB_06ebfff0
    void Save(FILE* File) override;                             // LAB_06ec02f0
    void Load(INI_Reader* Reader) override;                     // FUN_06ec0310

    // FUN_06ec0000 ("any"): ticks while any labelled object is in range.
    bool MatchesAny(float DeltaSeconds);
    // FUN_06ec0110: ticks only while every labelled object is in range.
    bool MatchesAll(float DeltaSeconds);

    float PositionX;       // +0x0c f3
    float PositionY;       // +0x10 f4
    float PositionZ;       // +0x14 f5
    unsigned int Label;    // +0x18 ID_String (ctor DAT_06fcefc8 = CreateID(null)) CreateID(s2)
    float DistanceSq;      // +0x1c f6, squared when not <= 0
    unsigned char bInside; // +0x20 (ctor 0)
    unsigned char bAny;    // +0x21 s1 == "any"
    float TickTimer;       // +0x24 (OnActivate = +0x28)
    float TickInterval;    // +0x28 (ctor 0) f7 when >= 0
};

// Cnd_EncLaunched: new(0x10), ctor inline (06ebbf24), vtable 06fb5568, event 0x100c.
// The message parms are a std::list of launched encounter ids (retail +0x04 = list head,
// node value at +0x08).
class CndEncLaunched : public Cnd
{
public:
    explicit CndEncLaunched(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec7900
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec25d0

    unsigned int EncounterId; // +0x0c ID_String CreateID(s0)
};

// Cnd_HasMsn: new(0x10), ctor inline (06ebbfaa), vtable 06fb5540, event 0x20.
class CndHasMsn : public Cnd
{
public:
    explicit CndHasMsn(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec2600
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec26b0

    int Wanted; // +0x0c "yes" 0, "no" 1 (not set by the ctor)
};

// Cnd_HealthDec: new(0x80), ctor inline (06ebbffc), vtable 06fb5514, event 0xd.
class CndHealthDec : public Cnd
{
public:
    explicit CndHealthDec(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec2a90
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec2ba0
    void OnActivate() override;                                 // FUN_06ec2b60

    TString<0x30> ObjectLabel;   // +0x0c s0
    TString<0x30> AttackerLabel; // +0x40 optional s2
    float Health;                // +0x74 f1 (not set by the ctor)
    unsigned int ObjectId;       // +0x78 (ctor 0) FUN_06f15b90(+0x0c)
    unsigned int AttackerId;     // +0x7c (ctor 0) FUN_06f15b90(+0x40)
};

// Cnd_InSpace: new(0x10), ctor inline (06ebc0ad), vtable 06fb54ec, event 0x20.
class CndInSpace : public Cnd
{
public:
    explicit CndInSpace(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ecdb30
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec2700

    unsigned char bInSpace; // +0x0c (ctor 1) b0
};

// Cnd_InTradelane: new(0x44), ctor inline (06ebc100), vtable 06fb54c0, event 0x20.
class CndInTradelane : public Cnd
{
public:
    explicit CndInTradelane(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec2750
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec27c0

    TString<0x30> ObjectLabel; // +0x0c s1
    unsigned char bInLane;     // +0x40 (ctor 1) b0
};

// Cnd_InZone: new(0x4c), ctor inline (06ebc15e), vtable 06fb5498, event 0x20.
class CndInZone : public Cnd
{
public:
    explicit CndInZone(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec2850
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec2980
    void OnActivate() override;                                 // FUN_06ec2960

    unsigned char bInZone;     // +0x0c (ctor 1) b0
    unsigned int ZoneFlags;    // +0x10 (ctor 0) OBJECT_ZONE 0x7fc0, EXCLUSION_ZONE 0x30000,
                               //       NEBULA_ZONE 0x8000, DUST_ZONE 0x200000
    TString<0x30> ObjectLabel; // +0x14 s1
    unsigned int ObjectId;     // +0x48 (ctor 0) FUN_06f15b90(+0x14)
};

// Cnd_JumpgateAct: new(0x44), ctor inline (06ebc1bb), vtable 06fb546c, event 0x1007.
class CndJumpgateAct : public Cnd
{
public:
    explicit CndJumpgateAct(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec2cf0
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec2d60

    TString<0x30> Jumpgate; // +0x0c s0
    unsigned int GateId;    // +0x40 (ctor 0) pub::GetNicknameId(+0x10)
};

// Cnd_JumpInComplete: new(0x10), ctor inline (06ebc24b), vtable 06fb543c, event 0x26.
class CndJumpInComplete : public Cnd
{
public:
    explicit CndJumpInComplete(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec2d80
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec2dd0

    unsigned int SystemId; // +0x0c (ctor 0; "ANY" leaves 0)
};

// Cnd_LaunchComplete: new(0x7c), ctor inline (06ebc2bc), vtable 06fb540c, event 0x28.
class CndLaunchComplete : public Cnd
{
public:
    explicit CndLaunchComplete(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec2e20
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec2f10
    void OnActivate() override;                                 // FUN_06ec2ef0

    TString<0x30> BaseNickname; // +0x0c optional s1
    TString<0x30> ShipLabel;    // +0x40 s0
    unsigned int BaseId;        // +0x74 (ctor 0) pub::GetNicknameId(+0x10)
    unsigned int ShipId;        // +0x78 (ctor 0) FUN_06f15940(+0x40)
};

// Cnd_LocEnter: new(0x14), ctor inline (06ebc33a), vtable 06fb53e0, event 0x29.
// Cnd_LocExit: vtable 06fb53b8, event 0x2a. Parse FUN_06ec2f70 and Matches FUN_06ec2fc0 shared.
class CndLocEnter : public Cnd
{
public:
    explicit CndLocEnter(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec2f70
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec2fc0

    unsigned int LocationId; // +0x0c (ctor 0) pub::GF::FindLocation(base, s0)
    unsigned int BaseId;     // +0x10 (ctor 0) pub::GetBaseID(s1)
};

class CndLocExit : public Cnd
{
public:
    explicit CndLocExit(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec2f70
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec2fc0

    unsigned int LocationId; // +0x0c
    unsigned int BaseId;     // +0x10
};

// Cnd_LootAcquired: new(0x4c), ctor inline (06ebc422), vtable 06fb5388, event 0x18 (inline
// push at 06ebc486).
class CndLootAcquired : public Cnd
{
public:
    explicit CndLootAcquired(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec3010
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec30c0
    void OnActivate() override;                                 // LAB_06ec30b0
    void Save(FILE* File) override;                             // LAB_06ec31e0
    void Load(INI_Reader* Reader) override;                     // FUN_06ec3200

    unsigned int LootLabel;    // +0x0c ID_String (ctor DAT_06fcefc8) CreateID(s0)
    TString<0x30> ShipLabel;   // +0x10 s1
    int Count;                 // +0x44 (OnActivate 0; not set by the ctor)
    int Needed;                // +0x48 (ctor -1) optional i2
};

// Cnd_MsnResponse: new(0x10), ctor inline (06ebc4df), vtable 06fb535c, event 0x2c.
class CndMsnResponse : public Cnd
{
public:
    explicit CndMsnResponse(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec3250
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec32b0

    int Response; // +0x0c "accept" 0, "reject" 1 (not set by the ctor)
};

// One entry of the Cnd_NPCSystemEnter / Cnd_NPCSystemExit lists (list node 0x40, value
// 0x38): +0x00 ship label, +0x34 system id the ship was last seen entering / leaving
// (Matches 06ec36a1 / 06ec3f0d, saved as "%s,%u").
struct FNpcSystemEntry
{
    TString<0x30> Label;
    unsigned int SystemId;
};

// Cnd_NPCSystemEnter: new(0x48), ctor FUN_06ec32e0, vtable 06fb59e4, events 0x100f, 0x20,
// 0x1010 (06ebc56c-06ebc5b5).
class CndNpcSystemEnter : public Cnd
{
public:
    explicit CndNpcSystemEnter(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // LAB_06ec33b0
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec3640
    void OnActivate() override;                                 // LAB_06ec3530
    void Save(FILE* File) override;                             // FUN_06ec3920
    void Load(INI_Reader* Reader) override;                     // LAB_06ec3970

    std::list<FNpcSystemEntry> Ships;       // +0x0c parsed labels ("Player" appended)
    // +0x18: copy of +0x24 made by Load; while non-empty, labels without a ship are kept
    // waiting, and message 0x1010 (NPC created) strikes them off by nickname.
    std::list<FNpcSystemEntry> Awaited;
    std::list<FNpcSystemEntry> Pending;     // +0x24 working copy (OnActivate = +0x0c)
    unsigned int SystemId;                  // +0x30 (ctor 0) pub::GetSystemID(s0)
    unsigned char bAnySystem;               // +0x34 (ctor 0) s0 == "ANY"
    unsigned char bDelayRunning;            // +0x35 (ctor 0) set by Matches 0x100f / 0x1010
    float DelayStart;                       // +0x38 (ctor 0) mission time (+0x30c) at start
    float DelayElapsed;                     // +0x3c (ctor 0) mission time - start (msg 0x20)
    float Delay;                            // +0x40 (ctor 0) 10.0 (0x100f) or 2.0 (0x1010)
    unsigned char bLoadedSameSystem;        // +0x44 (ctor 0) Load: every saved system id equal
};

// Cnd_NPCSystemExit: new(0x38), ctor inline (06ebc5ee, list ctors FUN_06ecffc0), vtable
// 06fb5318, event 0x100a. Dtor FUN_06ec3bc0 and OnActivate LAB_06ec3530 are the same
// bodies as Cnd_NPCSystemEnter's (the first 0x38 bytes have the same layout).
class CndNpcSystemExit : public Cnd
{
public:
    explicit CndNpcSystemExit(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec3cf0
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec3e20
    void OnActivate() override;                                 // LAB_06ec3530
    void Save(FILE* File) override;                             // FUN_06ec3f80
    void Load(INI_Reader* Reader) override;                     // LAB_06ec3fd0

    std::list<FNpcSystemEntry> Ships;       // +0x0c
    std::list<FNpcSystemEntry> Awaited;     // +0x18 copy of +0x24 made by Load
    std::list<FNpcSystemEntry> Pending;     // +0x24
    unsigned int SystemId;                  // +0x30 (ctor 0)
    unsigned char bAnySystem;               // +0x34 (ctor 0)
};

// Cnd_PlayerLaunch: new(0xc), ctor inline (06ebc6a9), vtable 06fb52e8, event 6.
// Parse is the base no-op FUN_06ebf6f0.
class CndPlayerLaunch : public Cnd
{
public:
    explicit CndPlayerLaunch(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ebf6f0
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec1f30
};

// Cnd_PlayerManeuver: new(0x4c), ctor inline (06ebc717), vtable 06fb52b8, event 0x2f.
class CndPlayerManeuver : public Cnd
{
public:
    explicit CndPlayerManeuver(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec1f50
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec2010
    void OnActivate() override;                                 // FUN_06ec1ff0

    int BehaviorId;            // +0x0c BehaviorTypes::get_behavior_id(s0) (not set by the ctor)
    TString<0x30> TargetLabel; // +0x10 optional s1
    unsigned char bInverse;    // +0x44 (ctor 0) s1 contains "inverse"
    unsigned int TargetId;     // +0x48 (ctor 0) FUN_06f15940(+0x10)
};

// Cnd_PopUpDialog: new(0x10), ctor inline (06ebc7ab), vtable 06fb528c, event 0x30.
class CndPopUpDialog : public Cnd
{
public:
    explicit CndPopUpDialog(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec20d0
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec21d0

    unsigned int ButtonMask; // +0x0c (ctor 0) YES 1, NO 2, LATER 4, CLOSE 8
};

// Cnd_ProjHit: new(0x84), ctor inline (06ebc827), vtable 06fb5264, event 0xd.
class CndProjHit : public Cnd
{
public:
    explicit CndProjHit(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec21f0
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec22c0
    void OnActivate() override;                                 // FUN_06ec2350
    void Save(FILE* File) override;                             // LAB_06ec2390
    void Load(INI_Reader* Reader) override;                     // FUN_06ec23b0

    TString<0x30> TargetLabel;   // +0x0c s0
    TString<0x30> ShooterLabel;  // +0x40 optional s2
    int Needed;                  // +0x74 (ctor 1) i1
    unsigned int TargetId;       // +0x78 (ctor 0) FUN_06f15b90(+0x0c)
    unsigned int ShooterId;      // +0x7c (ctor 0) FUN_06f15b90(+0x40)
    int Count;                   // +0x80 (ctor 0, OnActivate 0)
};

// Cnd_ProjHitShipToLbl: new(0x50), ctor inline (06ebc8c6), vtable 06fb5230, event 0xd.
class CndProjHitShipToLbl : public Cnd
{
public:
    explicit CndProjHitShipToLbl(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec2400
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec24a0
    void OnActivate() override;                                 // FUN_06ec2530
    void Save(FILE* File) override;                             // LAB_06ec2560
    void Load(INI_Reader* Reader) override;                     // FUN_06ec2580

    unsigned int TargetLabel;   // +0x0c ID_String CreateID(s0)
    TString<0x30> ShooterLabel; // +0x10 optional s2
    int Needed;                 // +0x44 (ctor 1) i1
    unsigned int ShooterId;     // +0x48 (ctor 0) FUN_06f15b90(+0x10)
    int Count;                  // +0x4c (ctor 0, OnActivate 0)
};

// Cnd_RTCDone: new(0x90), ctor inline (06ebc941), vtable 06fb5208, event 0x22.
class CndRtcDone : public Cnd
{
public:
    explicit CndRtcDone(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec1be0
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec1c50

    TString<0x80> RtcFile; // +0x0c s0 (clamped to 0x7f)
};

// Cnd_RumorHeard: new(0x38), ctor inline (06ebc9b6), vtable 06fb51dc, event 0x100d.
class CndRumorHeard : public Cnd
{
public:
    explicit CndRumorHeard(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ebfa20
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec1cc0

    unsigned int LocationId; // +0x0c (ctor 0)
    TString<0x20> Rumor;     // +0x10
    unsigned int BaseId;     // +0x34 (ctor 0)
};

// Cnd_SpaceEnter: new(0x10), ctor inline (06ebca2e), vtable 06fb51b0, event 0x36.
// Cnd_SpaceExit: vtable 06fb5184, event 0x37. Parse FUN_06ec1d00, Matches LAB_06ebf750.
class CndSpaceEnter : public Cnd
{
public:
    explicit CndSpaceEnter(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec1d00
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ebf750

    unsigned int SystemId; // +0x0c (ctor 0)
};

class CndSpaceExit : public Cnd
{
public:
    explicit CndSpaceExit(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec1d00
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ebf750

    unsigned int SystemId; // +0x0c
};

// Cnd_SystemEnter: new(0x14), ctor inline (06ebcb10), vtable 06fb5158, event 0x3b.
class CndSystemEnter : public Cnd
{
public:
    explicit CndSystemEnter(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec1db0
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec1e30

    unsigned int FromSystemId; // +0x0c (ctor 0) optional s1
    unsigned int SystemId;     // +0x10 (ctor 0) s0
};

// Cnd_SystemExit: new(0x14), ctor inline (06ebcb7f), vtable 06fb512c, event 0x3a.
class CndSystemExit : public Cnd
{
public:
    explicit CndSystemExit(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec1e80
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec1f00

    unsigned int SystemId;   // +0x0c (ctor 0) s0
    unsigned int ToSystemId; // +0x10 (ctor 0) optional s1
};

// Cnd_TetherBroke: new(0x98), ctor inline (06ebcc14), vtable 06fb5100, event 0x1011.
class CndTetherBroke : public Cnd
{
public:
    explicit CndTetherBroke(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec11c0
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec1330
    void OnActivate() override;                                 // LAB_06ec12f0
    void Save(FILE* File) override;                             // LAB_06ec14d0
    void Load(INI_Reader* Reader) override;                     // FUN_06ec1500

    TString<0x30> ShipA;      // +0x0c s0
    TString<0x30> ShipB;      // +0x40 s1
    unsigned int ShipAId;     // +0x74 (ctor 0) FUN_06f15940(+0x0c)
    unsigned int ShipBId;     // +0x78 (ctor 0) FUN_06f15940(+0x40)
    unsigned char Field_0x7c; // +0x7c (ctor 0) tethered: event parms (A, B, .., +0xc byte 0)
    float DistanceSq;         // +0x80 (ctor 0) f2, squared when > 0: broken beyond this
    float Field_0x84;         // +0x84 (ctor 0) optional f4, squared when not < 0: inside it
                              //       only a growing distance counts
    float Field_0x88;         // +0x88 (OnActivate 0) squared distance of the last check
    float Field_0x8c;         // +0x8c (ctor 0) f3: seconds beyond the tether that break it
    float Field_0x90;         // +0x90 (ctor 0, OnActivate 0) seconds beyond so far
    float Field_0x94;         // +0x94 (OnActivate 0) mission time (+0x30c) of the last check
};

// FUN_06f242e0 (TU39; callers Cnd_TetherBroke 06ec13aa and TU60 06f82ae0): squared distance
// of the two objects' locations, -1.0 when either GetLocation fails. The sum stays on the
// x87 stack (returned as double).
double __cdecl GetObjectDistanceSq(unsigned int const* IdA, unsigned int const* IdB);

// Cnd_Timer: new(0x18), ctor FUN_06ec1560, vtable 06fb5884, event 0x20.
// +0x0c elapsed seconds, +0x10 duration, +0x14 byte "count while docked" (ctor 1).
class CndTimer : public Cnd
{
public:
    explicit CndTimer(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                           // LAB_06ec1590
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override;        // FUN_06ec1660
    void OnActivate() override;                                        // LAB_06ec1650
    void Save(FILE* File) override;                                    // LAB_06ec16c0
    void Load(INI_Reader* Reader) override;                            // FUN_06ec16e0

    float Elapsed;           // +0x0c
    float Duration;          // +0x10
    unsigned char bRunOnBase; // +0x14
};

// Cnd_TLEntered: new(0xb4), ctor FUN_06ec1730, vtable 06fb58d0, event 0x16.
class CndTlEntered : public Cnd
{
public:
    explicit CndTlEntered(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // FUN_06ec1830
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec1900
    void OnActivate() override;                                 // FUN_06ec1b20

    TString<0x30> ShipLabel; // +0x0c s0
    TString<0x30> RingA;     // +0x40 s1
    TString<0x30> RingB;     // +0x74 s2
    unsigned int ShipId;     // +0xa8 (ctor 0) FUN_06f15940(+0x0c)
    unsigned int RingAId;    // +0xac (ctor 0) pub::GetNicknameId(+0x44)
    unsigned int RingBId;    // +0xb0 (ctor 0) pub::GetNicknameId(+0x78)
};

// Cnd_TLExited: new(0xb4), ctor FUN_06ec1950, vtable 06fb58ec, event 0x17.
// OnActivate FUN_06ec1b20 is Cnd_TLEntered's.
class CndTlExited : public Cnd
{
public:
    explicit CndTlExited(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // LAB_06ec1a20
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // FUN_06ec1b40
    void OnActivate() override;                                 // FUN_06ec1b20

    TString<0x30> ShipLabel; // +0x0c s0
    TString<0x30> RingA;     // +0x40 s1
    TString<0x30> RingB;     // +0x74 optional s2 (the ctor only zeroes it)
    unsigned int ShipId;     // +0xa8 (ctor 0)
    unsigned int RingBId;    // +0xac (ctor 0) pub::GetNicknameId(+0x78)
    unsigned int RingAId;    // +0xb0 (ctor 0) pub::GetNicknameId(+0x44) unless "ANY"
};

// Cnd_True: new(0xc), ctor FUN_06ec1bb0, vtable 06fb5908, event 0x20.
// Parse FUN_06ebf6f0 (RET 4), Matches LAB_06ec1bd0 (MOV AL,1 / RET 8), other slots base.
class CndTrue : public Cnd
{
public:
    explicit CndTrue(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override;
};

// Cnd_WatchTrigger: new(0x14), ctor FUN_06ec42a0, vtable 06fb5a40, event 0x20.
class CndWatchTrigger : public Cnd
{
public:
    explicit CndWatchTrigger(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // LAB_06ec4310
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec4350

    unsigned int WatchedTriggerId; // +0x0c ID_String CreateID(s0)
    unsigned int WantedState;      // +0x10 ID_String CreateID(s1): ACTIVE/INACTIVE/COMPLETE/ON/OFF
};

// Cnd_WatchVibe: new(0x84), ctor FUN_06ec43e0, vtable 06fb5a5c, event 0x20.
class CndWatchVibe : public Cnd
{
public:
    explicit CndWatchVibe(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override;                    // LAB_06ec44d0
    bool Matches(pub::Controller::EMessage EventId, void const* EventParms) override; // LAB_06ec4670
    void OnActivate() override;                                 // FUN_06ec4640

    int Compare;               // +0x0c (ctor 0) "lt" 0, "gt" 1, "eq" 2
    TString<0x30> ObjectA;     // +0x10 s0
    TString<0x30> ObjectB;     // +0x44 s1
    unsigned int ObjectAId;    // +0x78 (ctor 0) FUN_06f15b90(+0x10)
    unsigned int ObjectBId;    // +0x7c (ctor 0) FUN_06f15b90(+0x44)
    float Value;               // +0x80 (ctor 0) f2 or FUN_06ec9530(REP_* name)
};
