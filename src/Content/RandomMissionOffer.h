// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "ContentRefObject.h"       // FContentRefObject: the ref-counted base, vtable 06fb43f8
#include "Gcs.h"                    // FGcsConversation, FGcsPlayerListLine (TU25)
#include "MissionObject.h"         // FMissionShip, FMissionLoot, FMissionSolar
#include "NpcNames.h"               // FNpcNameParams
#include "NpcShips.h"               // FMsnNpc
#include "RandomMissionRuntime.h"   // FRmParamText
#include "common_id.h"
#include "common_net.h"             // MessageHandler
#include "common_text.h"
#include "x86math.h"

#include <list>
#include <map>
#include <vector>

struct FRmCriticalLoot;             // RandomEncounters.h (TU62)
struct FRmKillableSolar;            // RandomEncounters.h (TU62)
struct FSystemInfo;
class FRandomMissionObject;         // RandomMissionObject.h
class FRmRuntimeMachine;            // RandomMissionMachine.h

// Random-mission offer (docs/re/content.md, TU map, lane CNT-RM): the TU61 objects the offer tree of
// TU63 (FRandomMissionGenerator::BuildOffer) produces and the random mission keeps
// (FRandomMissionData::Offers). Two classes: the base offer FRmOffer (retail 0xb8, ctor
// FUN_06f89400) and the vignette offer FRmVignetteOffer (retail 0x218, ctor FUN_06f88240) that
// both mission-type filters (destroy solars FUN_06f9a700, assassinate FUN_06f9b790) create.
// The vignette offer holds four offer components -- MessageHandlers registered with the
// content message router (DAT_06fcf254) as soon as the offer is built -- whose ship groups,
// solars and objectives the TU61 runtime machine (FUN_06f890f0, not ported) spawns.

// ---------------------------------------------------------------------------------------
// Source of the values of a parametrised text: FRmParamText slot 2 (LAB_06f84740) and
// FRmOfferText slot 2 (FUN_06f993c0) call slot 0 once per parameter id (GRmTextParamNames
// values). The offers' primary vtables have this one slot and no destructor (06fbc908 = base
// offer FUN_06f89550, 06fbc898 = vignette offer LAB_06f896f0).
// ---------------------------------------------------------------------------------------
class IRmTextParamSource
{
public:
    // Appends the value of Param to Out; false when this source has no such value.
    virtual bool GetTextParam(int Param, FmtStr* Out) = 0;

protected:
    ~IRmTextParamSource() = default;
};

// ---------------------------------------------------------------------------------------
// TU61 -- base offer (retail 0xb8 bytes). Primary vtable 06fbc908 ([0] GetTextParam
// FUN_06f89550); the ref-counted FContentRefObject at +0x04 (vtable 06fbc8fc: AddRef
// FUN_06eacd40, Release FUN_06eacd50, deleting dtor FUN_06f884b0 = FUN_06f883e0 + free of
// this - 4), count 1 at +0x08. The dtor FUN_06f883e0 resets the +0x04 vptr to 06fb43f8.
// ---------------------------------------------------------------------------------------
class FRmOffer : public IRmTextParamSource, public FContentRefObject
{
public:
    // FUN_06f89400 (RET 4): +0x0c/+0x14/+0x18/+0x1c/+0x20 = 0, +0x10 = InMachineType,
    // +0x24/+0x28 = -1, the text and both parametrised texts empty, +0x54 = 0.
    explicit FRmOffer(int InMachineType);
    FRmOffer(FRmOffer const&) = delete;
    FRmOffer& operator=(FRmOffer const&) = delete;
    // FUN_06f883e0: FailureText, RewardText, Text (reverse member order).
    ~FRmOffer() override;

    // FUN_06f89550 (RET 8).
    bool GetTextParam(int Param, FmtStr* Out) override;

    // +0x0c: the mission running the offer; FDestroyMission::CreateRuntimeMachines
    // (FUN_06ef0100 06ef0141) writes it right before the factory, the runtime machine copies
    // it to its +0x88 (FUN_06f89a00 06f89a5d).
    FRandomMissionObject* Mission = nullptr;
    // +0x10: the runtime machine FUN_06f890f0 builds (1..3; anything else builds none):
    // destroy solars 1, assassinate 2 or 3 (FUN_06f9b790 06f9b7cf: NEG/SBB/ADD 3).
    int MachineType;
    FSystemInfo* System = nullptr;              // +0x14 the mission system (FUN_06f9e9b0)
    // +0x18: the offer group's FFactionProps::McCostume (FUN_06f9e9b0 06f9ebee-06f9ebfb).
    int McCostume = 0;
    unsigned int VoiceId = 0;                   // +0x1c pub::GetVoiceID("mc_leg_m01")
    unsigned int OfferBase = 0;                 // +0x20 OFFER_BASE
    int OfferGroup = -1;                        // +0x24 OFFER_GROUP
    unsigned int HostileGroup = 0xffffffffu;    // +0x28 HOSTILE_GROUP
    // +0x2c..+0x38 are not written by the ctor (heap garbage in retail); BuildOfferFromPath
    // writes all four before the offer is handed out. The port starts them at 0.
    float Difficulty = 0.0f;                    // +0x2c MISSION_DIFFICULTY (remapped rank)
    int Reward = 0;                             // +0x30 REWARD_MONEY
    float RepHit = 0.0f;                        // +0x34 FACTION_HIT (x 100)
    float RepReward = 0.0f;                     // +0x38 FACTION_REWARD (x 100)
    FmtStr Text;                                // +0x3c the offer text (FUN_06f9e1f0)
    // +0x54: the mission type shown to the player (text 0x32fa + Type, rm_* statistics):
    // destroy solars 1; assassinate 0, 2, 3, 4 or 5.
    unsigned char Type = 0;
    FRmParamText RewardText;                    // +0x58 node reward_text (FUN_06f9e1f0)
    FRmParamText FailureText;                   // +0x88 node failure_text (FUN_06f9e1f0)
};

// ---------------------------------------------------------------------------------------
// Objectives of an offer component, keyed by objective id (retail 0x18 bytes, vtable
// 06fbc6cc: [0] scalar deleting dtor FUN_06f89210; ctor FUN_06f891a0, dtor FUN_06f89230).
// The values are owned: the dtor deletes each through its vtable slot 0 (06f89270-06f8927b)
// before the tree is freed. Filled by FUN_06f9dee0 (FRandomMissionGenerator, insert
// FUN_06f21210, signed keys) with the TU61 objectives FRmObjectiveType2 (0x5c, ctor
// FUN_06f855f0) and FRmObjectiveType3 (0x70, ctor FUN_06f858c0), RandomMissionRuntime.h.
// ---------------------------------------------------------------------------------------
class FRmObjectiveMap
{
public:
    FRmObjectiveMap() = default;                // FUN_06f891a0
    FRmObjectiveMap(FRmObjectiveMap const&) = delete;
    FRmObjectiveMap& operator=(FRmObjectiveMap const&) = delete;
    virtual ~FRmObjectiveMap();                 // FUN_06f89230

    std::map<int, FRmParamText*> Objectives;    // +0x04
};

// ---------------------------------------------------------------------------------------
// Comm sequences of an offer (VignetteParams.ini "comm_sequence", FRmCommSequence): one
// conversation per sequence, one line per {speaker, text} pair. FUN_06f9e770 builds them into
// the vignette offer's General component; the component (FUN_06f84f10) sends a clone in
// message GRmMsgType06fd3b74 and the TU61 runtime machine hands it to the GCS conversation
// manager (FUN_06ee9140, which clones it once more through slot 5).
// ---------------------------------------------------------------------------------------

// The line bases are TU25 classes declared in Gcs.h (lane GCS): FGcsCloneableLine (vtable
// 06fb6268, FGcsLine + [5] Clone purecall) and FGcsPlayerListLine (vtable 06fb623c, 0x40
// bytes, PlayerIds at +0x34; ctor FUN_06ed6180). FGcsPlayerListLine's dtor FUN_06f847e0 and
// scalar deleting dtor FUN_06f847c0 are emitted in TU61 and ICF-shared with FRmLine (it adds
// nothing to destroy).

// TU61 -- one line of a comm sequence (retail 0x90 bytes, vtable 06fbc648: [0] scalar deleting
// dtor FUN_06f847c0, [1] CanPlay FUN_06f849c0, [2]-[4] FGcsPlayerListLine's Play LAB_06ed6200,
// IsDone FUN_06ed6560, GetDuration LAB_06ed69c0, [5] Clone FUN_06f84820). ctor FUN_06f84790
// (caller FUN_06f9e770).
class FRmLine : public FGcsPlayerListLine
{
public:
    // FUN_06f84790: FGcsPlayerListLine(null) (FUN_06ed6180 with 0), the kinds 0, the text
    // empty, +0x8c = 0.
    FRmLine();
    // FUN_06f849c0: FGcsPlayerListLine::CanPlay (FUN_06ed6710, a direct call) and +0x8c.
    bool CanPlay() override;
    // FUN_06f84820: new 0x90 copy.
    FGcsCloneableLine* Clone() override;

    int SpeakerKind;                            // +0x40 FRmCommLine::Speaker
    int TargetKind;                             // +0x44 FRmCommSequence::Target
    TString<0x40> Text;                         // +0x48 FRmCommLine::Text
    // +0x8c: TODO: meaning; CanPlay needs it non-zero, the ctor and FUN_06f9e770 leave it 0
    // (writer not found yet -- the runtime machine side).
    unsigned char Field_0x8c;
};

// TU61 -- conversation base with a copy (vtable 06fbc690: FGcsConversation's five slots with
// [2] IsValid still purecall, [5] Clone = purecall LAB_06fa9030, [6] CloneLines
// FUN_06f84a20). Stored only by the copy ctors (06f84ccf, 06fa3e7f) before the derived vptr.
// The GCS conversation manager's FUN_06ee9140 takes this type (slots 2, 1 and 5).
class FRmConversationBase : public FGcsConversation
{
public:
    // Slot 5 (+0x14): a heap copy whose lines are copies too.
    virtual FRmConversationBase* Clone() = 0;
    // Slot 6 (+0x18), FUN_06f84a20: Lines is replaced by a vector of Clone() of each line
    // (FUN_06f937c0 appends); the old pointers are not deleted (a fresh copy shares them).
    virtual void CloneLines();
};

// TU61 -- a comm sequence conversation (retail 0x24 bytes, vtable 06fbc660: [0] scalar
// deleting dtor FUN_06f84be0 (FGcsConversation's dtor thunk_FUN_06ee2b70 = 06f84d20), [1]
// Start LAB_06ee1f40, [2] IsValid FUN_06f84c00, [3] OnMessage FUN_06ee1f60, [4] IsFinished
// LAB_06ed6da0, [5] Clone FUN_06f84c40, [6] CloneLines FUN_06f84a20, [7] SetTrigger
// FUN_06f71080 (ICF), [8] GetTrigger LAB_06f84d30, [9] MarkReady LAB_06f84d40, [10] GetDelay
// LAB_06f84d50, [11] SetDelay LAB_06f84d60). ctor FUN_06f849f0 (RET 4; its byte argument
// is the vector's allocator byte at +0x04). Kept by value in the offer components' Conversations
// list; the copy into the list (FUN_06fa3df0) shares the line pointers, as retail.
class FRmConversation : public FRmConversationBase
{
public:
    FRmConversation() = default;                // FUN_06f849f0
    // FUN_06f84c00: MarkReady was called and every line CanPlay (slot 1).
    bool IsValid() override;
    // FUN_06f84c40: new 0x24 member copy, then its CloneLines (slot 6).
    FRmConversationBase* Clone() override;
    virtual void SetTrigger(int InTrigger);     // FUN_06f71080: +0x20
    virtual int GetTrigger();                   // LAB_06f84d30: +0x20
    virtual void MarkReady();                   // LAB_06f84d40: +0x18 = 1
    virtual float GetDelay();                   // LAB_06f84d50: +0x1c
    virtual void SetDelay(float InDelay);       // LAB_06f84d60: +0x1c

    bool bReady = false;                        // +0x18
    float Delay = 0.0f;                         // +0x1c seconds (FUN_06f84f10 sends after it)
    int Trigger = 0;                            // +0x20 FRmCommSequence::Trigger
};

// ---------------------------------------------------------------------------------------
// TU61 -- offer component (retail 0x34 bytes, vtable 06fbc6ac: [0] scalar deleting dtor
// FUN_06f84e40 / dtor FUN_06f84e60, [1] receive_message FUN_06ebf6f0 (RET 4, empty), [2]-[4]
// the MessageHandler send_* imports, [5] FUN_06f84f10, [6] LAB_06f85020, [7] LAB_06f852b0).
// ctor FUN_06f84d70: MessageHandler(DAT_06fcf254), +0x0c = 0, the list, the objective map.
// Slots 5-7 are called by the runtime machine (FRmVignetteMachine, FUN_06f8c460).
// ---------------------------------------------------------------------------------------
struct FContentPlayerState;         // ContentPlayerState.h

class FRmOfferComponent : public MessageHandler
{
public:
    FRmOfferComponent();                        // FUN_06f84d70
    FRmOfferComponent(FRmOfferComponent const&) = delete;
    FRmOfferComponent& operator=(FRmOfferComponent const&) = delete;
    ~FRmOfferComponent() override;              // FUN_06f84e60

    void receive_message(StateMachineMessage* Msg) override;    // FUN_06ebf6f0

    // Slot 5, FUN_06f84f10 (RET 0xc): every conversation of Trigger is prepared (slot 6) and a
    // clone of it sent to Machine in FRmConversationMessage after its delay.
    virtual void SendConversations(int Trigger, FRmRuntimeMachine* Machine,
                                   FContentPlayerState* Player);
    // Slot 6, LAB_06f85020 (RET 0xc): MarkReady; per line the texts and players cleared, the
    // listeners and the speaker resolved (slot 7), the voice line name, +0x28 = 0x4a39 and
    // Field_0x8c = 1.
    virtual void PrepareConversation(FRmConversation* Conversation, FRmRuntimeMachine* Machine,
                                     FContentPlayerState* Player);
    // Slot 7, LAB_06f852b0 (RET 0x10): Machine's ResolveLine (its slot 11).
    virtual bool ResolveLine(bool bSpeaker, FRmLine* Line, FRmRuntimeMachine* Machine,
                             FContentPlayerState* Player);

    bool bActive = false;                       // +0x0c set by the filter that fills it
    // +0x10: the comm sequence conversations (FUN_06f9e770 appends to the vignette offer's
    // General component); the dtor runs each element's dtor (FUN_06f84e60 06f84eb8).
    std::list<FRmConversation> Conversations;
    FRmObjectiveMap Objectives;                 // +0x1c
};

// One loot object of a vignette (retail 0x108 bytes: an FMissionLoot and three dwords; the
// dtor FUN_06ec7820 is the FMissionLoot one -- only the +0xec label list needs freeing).
// Built by FUN_06f97580 (06f97869-06f97894: FUN_06f93d80 / FUN_06f936d0).
struct FRmLootSpawn : public FMissionLoot
{
    // +0xfc: 2 marks the critical loot (FUN_06f89650 and LAB_06f896f0 count its +0x94 units
    // and take its +0x104). TODO: the other values.
    int Kind = 0;
    int Field_0x100 = 0;                        // +0x100 TODO: meaning
    // +0x104: the critical loot record (LAB_06f896f0 appends its +0x08 Name as a loot).
    FRmCriticalLoot const* CriticalLoot = nullptr;
};

// One ship of a vignette ship group (retail 0xf0 bytes, vtable 06fbc7a0: [0] scalar deleting
// dtor FUN_06f86400 / dtor FUN_06f86420). ctor FUN_06f862e0: the FMissionShip part (vtable
// 06fb60a8 first), the [NPC] block (FUN_06f31430), +0xd4 = 0, +0xd8 = 2, +0xdc = 1, the loot
// vector empty. The dtor frees the loot, then the FMissionShip members.
class FRmShipSpawn : public FMissionShip
{
public:
    FRmShipSpawn();                             // FUN_06f862e0
    ~FRmShipSpawn() override;                   // FUN_06f86420

    FMsnNpc Npc;                                // +0x88
    // +0xd4: the vignette offer's TargetName (+0x1e0) on the target ship, else null. Writer
    // FUN_06f97580 06f97852-06f97860 (first ship of a block with +0xdc == 2), whose argument is
    // &offer+0x1e0 only on the FUN_06f9c100 path (06f9c3d2). FUN_06f88940 keeps the name
    // reserved while one ship carries it.
    FNpcNameParams* TargetName = nullptr;
    int Field_0xd8 = 2;                         // +0xd8 TODO: meaning
    int Field_0xdc = 1;                         // +0xdc TODO: meaning
    std::vector<FRmLootSpawn> Loot;             // +0xe0 (alloc byte) / +0xe4..+0xec
};

// One ship group of a vignette (retail 0x98 bytes, ctor FUN_06f865e0, dtor FUN_06f94b20 =
// the ship vector). Filled by FUN_06f979b0 from a ship block and FUN_06f97580.
struct FRmShipGroup
{
    FRmShipGroup();                             // FUN_06f865e0

    unsigned char Field_0x00 = 1;               // +0x00 ship block +0x00. TODO: meaning
    unsigned char Field_0x01 = 0;               // +0x01 TODO: meaning (read by FUN_06f88940)
    unsigned char Field_0x02 = 0;               // +0x02 ship block +0x03. TODO: meaning
    int Field_0x04 = 0;                         // +0x04 ship block +0x18. TODO: meaning
    int Field_0x08 = -1;                        // +0x08 ship block +0x1c. TODO: meaning
    FSystemInfo* System = nullptr;              // +0x0c FindSystemInfo(query DestSys)
    FMatrix Orientation;                        // +0x10 identity
    Vector Position;                            // +0x34 the objective position
    float Field_0x40 = -2.0f;                   // +0x40 DAT_06fbc2e8; 0.9 / -0.9. TODO: meaning
    TString<0x30> Name;                         // +0x44 (FUN_06f85020 builds it)
    int Field_0x78 = 0;                         // +0x78 TODO: meaning
    int Field_0x7c = -1;                        // +0x7c TODO: meaning
    ID_String Formation;                        // +0x80 CreateID("transport_liberty")
    std::vector<FRmShipSpawn> Ships;            // +0x84 (alloc byte) / +0x88..+0x90
    unsigned int Serial;                        // +0x94 ++DAT_06fd3c08
};

// TU61 -- DAT_06fd3c08: the serial of the last ship group built (FUN_06f865e0).
extern unsigned int GRmShipGroupSerial;

// One solar of a vignette (retail 0x500 bytes, ctor FUN_06f852e0: the FMissionSolar part as
// the [MsnSolar] template, then the fields below). Built by FUN_06f9af40. +0x4f8..+0x4ff are
// never written.
struct FRmSolarSpawn : public FMissionSolar
{
    FRmSolarSpawn() = default;                  // FUN_06f852e0

    // +0x4c8: the solar's attitude towards the players (DAT_06fbc2e8 -2.0 = none; FUN_06f9af40
    // -0.9). FUN_06f926b0 sets it with pub::Reputation::SetAttitude and keeps it in the
    // record's Attitude.
    float Field_0x4c8 = -2.0f;
    FRmKillableSolar const* Arch = nullptr;     // +0x4cc KillableSolars entry
    int SolarType = -1;                         // +0x4d0 GRmSolarTypes value
    unsigned int DestSys = 0;                   // +0x4d4 the mission system
    // +0x4d8: 1 by default; FUN_06f9af40 writes 2 for a solar the player must kill ("pk_"
    // flags), 0 otherwise. FUN_06f926b0: bit 0 makes the solar invincible; the value becomes
    // the record's Flags. TODO: exact meaning of 2.
    int ModeFlag = 1;
    // +0x4dc: SetInvincible's duration when ModeFlag bit 0 is set (FUN_06f926b0 06f92c3f, a
    // float pushed as a dword).
    float InvincibleDuration = 0.0f;
    // +0x4e0: SetInvincible's third argument (FUN_06f926b0 06f92c4e). TODO: meaning.
    unsigned char Field_0x4e0 = 0;
    int Hitpoints = -1;                         // +0x4e4
    std::vector<FRmLootSpawn> Loot;             // +0x4e8 (alloc byte) / +0x4ec..+0x4f4
};

// TU61 -- ship component (retail 0x50 bytes, vtable 06fbc89c: [0] FUN_06f884e0 / dtor
// FUN_06f88500, [7] LAB_06f88df0, the rest the component's). The vignette offer's +0x190:
// the hostile and friendly ship groups (FUN_06f979b0).
class FRmShipComponent : public FRmOfferComponent
{
public:
    FRmShipComponent() = default;               // inline at 06f88363-06f88386
    ~FRmShipComponent() override;               // FUN_06f88500

    // Slot 7, LAB_06f88df0 (RET 0x10): listeners PLAYERS_IN_RANGE (8) / PLAYERS_OUT_OF_RANGE (9)
    // around Player's ship; everything else is the base slot's machine call.
    bool ResolveLine(bool bSpeaker, FRmLine* Line, FRmRuntimeMachine* Machine,
                     FContentPlayerState* Player) override;

    std::vector<FRmShipGroup> Groups;          // +0x34 (alloc byte) / +0x38..+0x40
    // +0x44: the objective position (06f9a7b0). Neither inline ctor writes it (06f882d1-06f88303,
    // 06f88363-06f88386: heap garbage in retail); the port starts it at 0.
    Vector Position = {};
};

// TU61 -- target ship component (retail 0x58 bytes, vtable 06fbc8dc with every slot of
// 06fbc89c, the dtor FUN_06f88500 included). Derives from the ship component: the layout is
// the same up to 0x50 and TU63 FUN_06f979b0 takes an FRmShipComponent* for it (FUN_06f9c100
// 06f9c1fd / 06f9c2c8 / 06f9c3e5 pass &offer+0xec); the inline ctor 06f882d1-06f88303 elides
// the intermediate 06fbc89c store. The vignette offer's +0xec: the assassination target's
// group (FUN_06f9c100).
class FRmTargetShipComponent : public FRmShipComponent
{
public:
    FRmTargetShipComponent() = default;         // inline at 06f882d1-06f88303
    ~FRmTargetShipComponent() override;         // FUN_06f88500 (ICF)

    int Field_0x50 = 0;                         // +0x50 TODO: meaning
    int Field_0x54 = -1;                        // +0x54 TODO: meaning
};

// TU61 -- solar component (retail 0x4c bytes, vtable 06fbc8bc: [0] FUN_06f88ad0 / dtor
// FUN_06f88620, [7] LAB_06f88af0). The vignette offer's +0x144 (FUN_06f9ac80).
class FRmSolarComponent : public FRmOfferComponent
{
public:
    FRmSolarComponent() = default;              // inline at 06f88306-06f8835d
    ~FRmSolarComponent() override;              // FUN_06f88620

    // Slot 7, LAB_06f88af0 (RET 0x10): the ship component's slot with two differences: range
    // 8 keeps squared distance <= 1e8 (not <), and without Player (range 8 or 9) the machine
    // is asked for the listeners (bSpeaker false; the ship slot passes true).
    bool ResolveLine(bool bSpeaker, FRmLine* Line, FRmRuntimeMachine* Machine,
                     FContentPlayerState* Player) override;

    // +0x34: the objective position. The inline ctor (06f88306-06f8835d) does not write it
    // (heap garbage in retail); the port starts it at 0.
    Vector Position = {};
    std::list<FRmSolarSpawn> Solars;           // +0x40 (alloc byte) / +0x44 head, +0x48 size
};

// ---------------------------------------------------------------------------------------
// TU61 -- vignette offer (retail 0x218 bytes). Primary vtable 06fbc898 ([0] GetTextParam
// LAB_06f896f0); the ref-counted interface at +0x04 has vtable 06fbc88c (AddRef, Release,
// deleting dtor FUN_06f885f0 = FUN_06f88710 + free of this - 4).
// ---------------------------------------------------------------------------------------
class FRmVignetteOffer : public FRmOffer
{
public:
    // FUN_06f88240 (RET 4): the base offer, the four components, the target name.
    explicit FRmVignetteOffer(int InMachineType);
    // FUN_06f88710: ReleaseTargetName (FUN_06f88940), then the members in reverse order.
    ~FRmVignetteOffer() override;

    // LAB_06f896f0 (RET 8): the base offer's parameters first (FUN_06f89550), then
    // BIG_SOLAR, OTHER_SOLAR, CRITICAL_LOOT, TARGET_FULL_NAME and TARGET_ZONE.
    bool GetTextParam(int Param, FmtStr* Out) override;

    // +0xb8: the comm sequences of the path (FUN_06f9e770 fills Conversations; FUN_06f9a700
    // activates it). TODO: any other role.
    FRmOfferComponent General;
    FRmTargetShipComponent TargetShips;         // +0xec
    FRmSolarComponent Solars;                   // +0x144
    FRmShipComponent Ships;                     // +0x190
    FNpcNameParams TargetName;                  // +0x1e0 (FUN_06f9b790: the target's name)

private:
    // FUN_06f88940: the target's name goes back to its faction's name pools unless a ship
    // still carries it.
    void ReleaseTargetName();
    // FUN_06f89650 (thiscall, RET 0xc; this is not read): over Groups' ships, each critical
    // loot (Kind 2) sets *OutLoot and adds its units to *InOutCount; a ship's loot scan stops
    // once the count exceeds 1.
    void CountCriticalLoot(std::vector<FRmShipGroup> const* Groups,
                           FRmCriticalLoot const** OutLoot, int* InOutCount) const;
};
