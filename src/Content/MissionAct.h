// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Mission trigger actions (TU21, Content.dll 06eb7360-06ed3d90).
// Every Act_* key in a [Trigger] section is one retail class created by the opcode factory
// FUN_06ebb850, pushed onto the trigger's action list (trigger+0x08, FUN_06ecfc30) and
// executed in list order when the trigger's condition matches (FUN_06eb9af0 / FUN_06eb82c0).

#include "ContentPlayerState.h"
#include "common_csys.h"
#include "common_goods.h"     // EquipDescVector (Act_SetShipAndLoadout)
#include "common_ini.h"
#include "common_reputation.h" // Costume
#include "common_server.h"
#include "common_tstring.h"
#include "x86math.h"

#include <list>
#include <vector>

class INI_Reader;
struct FFormation;
struct FMissionScript;

// FUN_06ec9530 (non-virtual mission method, the mission pointer is unused): REP_* name ->
// reputation value (DAT_06fb4658..06fb4670: -1, -0.6, -0.2, 0, 0.2, 0.6, 1); any other name
// gives -2 (the 0xc0000000 default). The name is passed by value (RET 0x44).
float GetRepValueFromName(FMissionScript* Script, TString<0x40> Name);

// Base action. Vtable 06fb5a90: +0x00 LAB_06ec47c0 (scalar deleting dtor), +0x04/+0x08
// purecall. Fields: +0x04 owning mission (trigger+0x14), +0x08 ID_String cleared by the ctor
// and set to the owning trigger's id by FUN_06ebf700 right after Parse.
class Act
{
public:
    explicit Act(FMissionScript* InMission);
    virtual ~Act();

    // +0x04: reads the rest of the Act_* line.
    virtual void Parse(INI_Reader& Reader) = 0;
    // +0x08: runs the action. Called with no arguments by the trigger engine and by the
    // mission timer list (FUN_06eb9c60). Implementations return AL=1.
    virtual bool Execute() = 0;

    // FUN_06ebf700 (shared with Cnd by /OPT:ICF): +0x08 = id.
    void SetTriggerId(unsigned int Id);

    FMissionScript* Mission; // +0x04
    unsigned int TriggerId;  // +0x08 ID_String
};

// Act_ActTrig: new(0x10), ctor FUN_06ec7880, vtable 06fb5ba4.
// +0x0c id of the trigger to activate.
class ActActTrig : public Act
{
public:
    explicit ActActTrig(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec7900
    bool Execute() override;                 // LAB_06ec7920

    unsigned int TargetTriggerId; // +0x0c ID_String
};

// Act_ChangeState: new(0x14), ctor FUN_06ec55c0, vtable 06fb5ad0.
// +0x0c desired mission state (mission vtbl +0x68 of s0), +0x10 id (ctor 1).
class ActChangeState : public Act
{
public:
    explicit ActChangeState(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec55f0
    bool Execute() override;                 // LAB_06ec5640

    int State;              // +0x0c (ctor -1)
    unsigned int Field_0x10; // +0x10 (ctor 1); passed to mission +0x2e8. TODO: name.
};

// Act_GCSClamp: new(0x10), ctor FUN_06ec8b00, vtable 06fb5c58. +0x0c byte (ctor 0).
class ActGcsClamp : public Act
{
public:
    explicit ActGcsClamp(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecdb30
    bool Execute() override;                 // LAB_06ec8b30

    unsigned char bClamp; // +0x0c get_value_bool(0)
};

// Act_RandomPop: new(0x10), ctor FUN_06ecdb10 (+0x0c not set), vtable 06fb5fa8.
class ActRandomPop : public Act
{
public:
    explicit ActRandomPop(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecdb30
    bool Execute() override;                 // LAB_06ecdb50

    unsigned char bEnabled; // +0x0c get_value_bool(0)
};

// Act_RandomPopSphere: new(0x20), ctor FUN_06ecdb70 (fields not set), vtable 06fb5fb4.
class ActRandomPopSphere : public Act
{
public:
    explicit ActRandomPopSphere(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ecdb90
    bool Execute() override;                 // LAB_06ecdc80

    float CenterX; // +0x0c f0
    float CenterY; // +0x10 f1
    float CenterZ; // +0x14 f2
    float Radius;  // +0x18 f3
    int Mode;      // +0x1c s4: "on" 0 (remove the exclusion sphere), "off" 1 (add it)
};

// Act_RpopAttClamp: new(0x10), ctor FUN_06ece780 (+0x0c not set), vtable 06fb6024.
class ActRpopAttClamp : public Act
{
public:
    explicit ActRpopAttClamp(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecdb30
    bool Execute() override;                 // LAB_06ece7a0

    unsigned char bClamp; // +0x0c get_value_bool(0)
};

// Act_RpopTLAttacksEnabled: new(0x10), ctor FUN_06ece7b0 (+0x0c not set), vtable 06fb6030.
class ActRpopTlAttacksEnabled : public Act
{
public:
    explicit ActRpopTlAttacksEnabled(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecdb30
    bool Execute() override;                 // LAB_06ece7d0

    unsigned char bEnabled; // +0x0c get_value_bool(0)
};

// Act_Save: new(0x14), ctor FUN_06ec4750, vtable 06fb5a84.
class ActSave : public Act
{
public:
    explicit ActSave(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec47e0
    bool Execute() override;                 // FUN_06ec4820

    unsigned int SaveTriggerId; // +0x0c ID_String CreateID(s0)
    unsigned int SaveType;      // +0x10 (ctor 1) optional i1
};

// Act_SetVibeOfferBaseHack: new(0x10), ctor FUN_06ecab80, vtable 06fb5de8.
class ActSetVibeOfferBaseHack : public Act
{
public:
    explicit ActSetVibeOfferBaseHack(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecabc0
    bool Execute() override;                 // FUN_06ecabe0

    unsigned int BaseId; // +0x0c (ctor 0) pub::GetNicknameId(s0)
};

// Act_AdjAcct: new(0x10), ctor FUN_06ec7ec0, vtable 06fb5be0. +0x0c = 0.
class ActAdjAcct : public Act
{
public:
    explicit ActAdjAcct(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec9ac0
    bool Execute() override;                 // FUN_06ec7ef0

    int Amount; // +0x0c get_value_int(0)
};

// Act_ForceLand: new(0x10), ctor FUN_06ec8a30, vtable 06fb5c4c. +0x0c = 0.
class ActForceLand : public Act
{
public:
    explicit ActForceLand(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec8a60
    bool Execute() override;                 // LAB_06ec8ae0

    unsigned int BaseId; // +0x0c pub::GetBaseID(s0 as TString<0x20>)
};

// Act_GiveNNObjs: new(0xc), ctor FUN_06ecad60, vtable 06fb5e00; Parse is the base no-op.
class ActGiveNnObjs : public Act
{
public:
    explicit ActGiveNnObjs(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ebf6f0
    bool Execute() override;                 // LAB_06ecad80
};

// Act_PlayNN: new(0x10), ctor FUN_06ecd340, vtable 06fb5f24. +0x0c = 0.
class ActPlayNn : public Act
{
public:
    explicit ActPlayNn(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecabc0
    bool Execute() override;                 // LAB_06ecd370

    unsigned int MessageId; // +0x0c pub::GetNicknameId(s0)
};

// Act_PlaySoundEffect: new(0x10), ctor FUN_06ecd390, vtable 06fb5f30. +0x0c = 0.
class ActPlaySoundEffect : public Act
{
public:
    explicit ActPlaySoundEffect(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecabc0
    bool Execute() override;                 // LAB_06ecd3c0

    unsigned int SoundId; // +0x0c pub::GetNicknameId(s0)
};

// Act_PobjIdle: new(0xc), ctor FUN_06ecd4f0, vtable 06fb5f48; Parse is the base no-op.
class ActPobjIdle : public Act
{
public:
    explicit ActPobjIdle(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ebf6f0
    bool Execute() override;                 // LAB_06ecd510
};

// Act_SetOffer: new(0x10), ctor FUN_06ec9300, vtable 06fb5cec. +0x0c = 0.
class ActSetOffer : public Act
{
public:
    explicit ActSetOffer(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec9ac0
    bool Execute() override;                 // FUN_06ec9330

    unsigned int OfferId; // +0x0c get_value_int(0): mad-lib id of mission+0x68
};

// Act_SetTitle: new(0x10), ctor FUN_06ec9a90, vtable 06fb5dac. +0x0c = 0.
class ActSetTitle : public Act
{
public:
    explicit ActSetTitle(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec9ac0
    bool Execute() override;                 // FUN_06ec9ae0

    unsigned int TitleId; // +0x0c get_value_int(0): mad-lib id of mission+0x50
};

// Act_RevertCam: new(0xc), ctor FUN_06ece730, vtable 06fb6018; Parse is the base no-op.
class ActRevertCam : public Act
{
public:
    explicit ActRevertCam(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ebf6f0
    bool Execute() override;                 // FUN_06ece750
};

// Act_DebugMsg: new(0x90), ctor FUN_06ec7a80, vtable 06fb5bbc. +0x0c TString<0x80> "".
class ActDebugMsg : public Act
{
public:
    explicit ActDebugMsg(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec7af0
    bool Execute() override;                 // LAB_06ea1460 (MOV AL,1; RET)

    TString<0x80> Text; // +0x0c s0
};

// Act_DisableEnc / Act_EnableEnc: new(0x30), ctors FUN_06ec8370 / FUN_06ec85b0, vtables
// 06fb5c04 / 06fb5c28, shared Parse FUN_06ec83e0 (+0x0c TString<0x20> = s0).
class ActDisableEnc : public Act
{
public:
    explicit ActDisableEnc(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec83e0
    bool Execute() override;                 // LAB_06ec8440

    TString<0x20> Encounter; // +0x0c [MsnRandEnc] nickname
};

class ActEnableEnc : public Act
{
public:
    explicit ActEnableEnc(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec83e0
    bool Execute() override;                 // LAB_06ec8620

    TString<0x20> Encounter; // +0x0c
};

// Act_DisableTradelane: new(0x10), ctor FUN_06ec84c0, vtable 06fb5c10. +0x0c = 0.
class ActDisableTradelane : public Act
{
public:
    explicit ActDisableTradelane(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecabc0
    bool Execute() override;                 // LAB_06ec84f0

    unsigned int RingId; // +0x0c pub::GetNicknameId(s0)
};

// Act_DockRequest: new(0x14), ctor FUN_06ec8500, vtable 06fb5c1c. +0x0c = 0; +0x10 is an
// ID_String cleared by the ctor and never written or read again.
class ActDockRequest : public Act
{
public:
    explicit ActDockRequest(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecabc0
    bool Execute() override;                 // LAB_06ec8570

    unsigned int TargetId;   // +0x0c pub::GetNicknameId(s0)
    unsigned int Field_0x10; // +0x10 ID_String (ctor clear). TODO: no reader found.
};

// Act_EnableManeuver: new(0x14), ctor FUN_06ec86a0, vtable 06fb5c34.
class ActEnableManeuver : public Act
{
public:
    explicit ActEnableManeuver(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec86d0
    bool Execute() override;                 // LAB_06ec8710

    int ManeuverId;         // +0x0c (ctor -1) BehaviorTypes::get_behavior_id(s0)
    unsigned char bEnable;  // +0x10 (ctor 1) get_value_bool(1)
};

// Act_LockManeuvers: new(0x10), ctor FUN_06ec5f70, vtable 06fb5b28. +0x0c = 0.
class ActLockManeuvers : public Act
{
public:
    explicit ActLockManeuvers(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecdb30
    bool Execute() override;                 // LAB_06ec5fa0

    unsigned char bLock; // +0x0c get_value_bool(0)
};

// Act_HostileClamp: new(0x10), ctor FUN_06ecaf90, vtable 06fb5e18. +0x0c = 1.
class ActHostileClamp : public Act
{
public:
    explicit ActHostileClamp(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecafc0
    bool Execute() override;                 // LAB_06ecafe0

    unsigned char bClamp; // +0x0c !get_value_bool(0)
};

// Act_NagClamp: new(0x10), ctor FUN_06ecb3f0, vtable 06fb5e3c. +0x0c = 0, +0x0d = +0x0e = 1.
class ActNagClamp : public Act
{
public:
    explicit ActNagClamp(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecdb30
    bool Execute() override;                 // FUN_06ecb420

    unsigned char bClamp;     // +0x0c get_value_bool(0)
    unsigned char Field_0x0d; // +0x0d (ctor 1). TODO: no reader found.
    unsigned char Field_0x0e; // +0x0e (ctor 1). TODO: no reader found.
};

// Act_PlayerEnemyClamp: new(0x14), ctor FUN_06eccb30, vtable 06fb5ef4. +0x0c = +0x10 = 2.
class ActPlayerEnemyClamp : public Act
{
public:
    explicit ActPlayerEnemyClamp(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06eccb60
    bool Execute() override;                 // FUN_06eccb90

    int MinClamp; // +0x0c get_value_int(0)
    int MaxClamp; // +0x10 get_value_int(1)
};

// Act_MovePlayer: new(0x1c), ctor FUN_06ecb350 (fields not set), vtable 06fb5e30.
class ActMovePlayer : public Act
{
public:
    explicit ActMovePlayer(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ecb370
    bool Execute() override;                 // LAB_06ecb3d0

    Vector Position; // +0x0c f0..f2
    float RadiusSq;  // +0x18 f3 * f3
};

// Act_RemoveRTC: new(0x90), ctor FUN_06ece170, vtable 06fb5fe0. +0x0c TString<0x80> "".
class ActRemoveRtc : public Act
{
public:
    explicit ActRemoveRtc(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec1be0 (shared with Cnd_RTCDone)
    bool Execute() override;                 // LAB_06ece1e0

    TString<0x80> RtcFile; // +0x0c s0
};

// Act_SetNNHidden: new(0x14), ctor FUN_06ec8f20, vtable 06fb5c94. +0x0c = 0, +0x10 =
// CreateID("").
class ActSetNnHidden : public Act
{
public:
    explicit ActSetNnHidden(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec8fa0
    bool Execute() override;                 // FUN_06ec8fe0

    unsigned char bVisible;  // +0x0c !get_value_bool(1)
    unsigned int ObjectiveId; // +0x10 ID_String CreateID(s0)
};

// Act_SetNNObj: new(0x14), ctor FUN_06ec9030, vtable 06fb5ca4. +0x0c = CreateID(""),
// +0x10 = 0x3a.
class ActSetNnObj : public Act
{
public:
    explicit ActSetNnObj(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec90b0
    bool Execute() override;                 // FUN_06ec9150

    unsigned int ObjectiveId; // +0x0c ID_String CreateID(s0)
    int State;                // +0x10 objective state (HISTORY 0x12a, OBJECTIVE 0x11a,
                              // OBJECTIVE_HISTORY 0x13a)
};

// Act_SetNNState: new(0x14), ctor FUN_06ec91a0, vtable 06fb5cd8. +0x0c = CreateID(""),
// +0x10 = 4.
class ActSetNnState : public Act
{
public:
    explicit ActSetNnState(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec9220
    bool Execute() override;                 // LAB_06ec92e0

    unsigned int ObjectiveId; // +0x0c ID_String CreateID(s0)
    int State;                // +0x10 ACTIVE 1, INACTIVE 4, COMPLETE 3, FAILED 2
};

// Act_NNIds: new(0x14), ctor FUN_06ecc720, vtable 06fb5edc. +0x0c = 1, +0x10 = 9.
class ActNnIds : public Act
{
public:
    explicit ActNnIds(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecc750
    bool Execute() override;                 // FUN_06ecc7f0

    int TextId;      // +0x0c get_value_int(0)
    int MessageType; // +0x10 OBJECTIVE 7, HISTORY 8, OBJECTIVE_HISTORY 9
};

// Act_PlayMusic: new(0x28), ctor FUN_06ecd120, vtable 06fb5f18. +0x0c the pub::Audio
// track (4 ids 0, byte 0, fade -1.0f), +0x24 cancel flag 0.
class ActPlayMusic : public Act
{
public:
    explicit ActPlayMusic(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ecd160
    bool Execute() override;                 // LAB_06ecd310

    pub::Audio::Tryptich Music; // +0x0c ids s0..s3, +0x1c b5, +0x20 fade f4 (float bits)
    unsigned char bCancel;      // +0x24 no values / "no_params"
};

// Act_PlayerCanDock: new(0x20), ctor FUN_06ecc8b0, vtable 06fb5ee8 (own dtor FUN_06ecc8f0,
// the vector at +0x10). +0x0c is not set by the ctor.
class ActPlayerCanDock : public Act
{
public:
    explicit ActPlayerCanDock(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ecc910
    bool Execute() override;                 // FUN_06eccb00

    unsigned char bCanDock;                   // +0x0c get_value_bool(0)
    std::vector<unsigned int> Exceptions;     // +0x10 GetNicknameId(s1..)
};

// Act_PlayerCanTradelane: new(0x20), ctor FUN_06eccbe0, vtable 06fb5f00 (dtor FUN_06ecc8f0).
class ActPlayerCanTradelane : public Act
{
public:
    explicit ActPlayerCanTradelane(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06eccc50
    bool Execute() override;                 // FUN_06ecce90

    unsigned char bCanTradelane;              // +0x0c (ctor 1) get_value_bool(0)
    std::vector<FTlrException> Exceptions;    // +0x10 {GetNicknameId(s_i), GetNicknameId(s_i+1)}
};

// Act_DisableFriendlyFire: new(0x18), ctor FUN_06ec8080, vtable 06fb5bf8 (own dtor
// FUN_06ec8100, the list at +0x0c).
class ActDisableFriendlyFire : public Act
{
public:
    explicit ActDisableFriendlyFire(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec81a0
    bool Execute() override;                 // LAB_06ec8270

    std::list<TString<0x30>> Labels;          // +0x0c s0.. (node 0x3c)
};

// Act_SetInitialPlayerPos: new(0x3c), ctor FUN_06ecd990, vtable 06fb5f9c. +0x0c identity
// matrix, +0x30 position 0.
class ActSetInitialPlayerPos : public Act
{
public:
    explicit ActSetInitialPlayerPos(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ecd9e0
    bool Execute() override;                 // FUN_06ecdad0

    Matrix Orientation; // +0x0c quaternion (W=f3, X=f4, Y=f5, Z=f6) through the math engine
    Vector Position;    // +0x30 f0..f2
};

// Act_DeactTrig: new(0x10), ctor FUN_06ec79e0, vtable 06fb5bb0.
// +0x0c id of the trigger to deactivate.
class ActDeactTrig : public Act
{
public:
    explicit ActDeactTrig(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec7900
    bool Execute() override;                 // LAB_06ec7a60

    unsigned int TargetTriggerId; // +0x0c ID_String
};

// ---------------------------------------------------------------------------------------
// Nag objects: the values of the mission's +0x204 map (id -> object), created by
// Act_NagGreet / Act_NagDistTowards / Act_NagDistLeaving, erased by Act_NagOff, fed by the
// dispatcher tail (FUN_06ecef10) and deleted by slot 22 and the dtor.
// ---------------------------------------------------------------------------------------

// Base nag. Vtable 06fb5eb4 (set by the dtor body FUN_06ecc2e0): +0x00 FUN_06ecf4f0 (scalar
// deleting dtor), +0x04 purecall, +0x08 FUN_06ecf190. No ctor of its own (inlined).
class FMissionNag
{
public:
    FMissionNag();
    virtual ~FMissionNag();

    // +0x04 (RET 8): one message of MessageIds.
    virtual bool HandleMessage(pub::Controller::EMessage MessageId, void const* MessageParms) = 0;
    // +0x08 FUN_06ecf190 (RET 4): send the nag line Name to the player.
    virtual void Say(char const* Name);

    unsigned int SpeakerId;           // +0x04 SpaceObj id of the speaker label (0 = none)
    unsigned int PlayerId;            // +0x08
    unsigned int SystemId;            // +0x0c the player's system (message 0x3b updates it)
    Costume const* SpeakerCostume;    // +0x10 &[NPC] space_costume of the speaker, or null
    unsigned int VoiceId;             // +0x14 [NPC] voice, or 0
    unsigned int LineId;              // +0x18 GetNicknameId of the last line
    unsigned int SpeakerNameId;       // +0x1c [NPC] individual_name, or 1
    unsigned char bDone;              // +0x20 set -> FUN_06ecef10 unlinks the object
    std::vector<pub::Controller::EMessage> MessageIds; // +0x24 messages HandleMessage gets
};

// Greet nag: new(0x34), ctor inline in LAB_06ecc080, vtable 06fb5ea8; messages {0x28}.
class FMissionNagGreet : public FMissionNag
{
public:
    FMissionNagGreet();
    bool HandleMessage(pub::Controller::EMessage MessageId, void const* MessageParms) override; // FUN_06ecf310
};

// Distance nag: new(0x70), ctor FUN_06ecf3a0, vtable 06fb6098; messages
// {0x20, 0xb, 0x3e, 0x16, 0x11, 0x27}.
class FMissionNagDist : public FMissionNag
{
public:
    FMissionNagDist();
    bool HandleMessage(pub::Controller::EMessage MessageId, void const* MessageParms) override; // LAB_06ecf5d0
    void Say(char const* Name) override;                                  // FUN_06ecf510
    // +0x0c LAB_06ecf5b0: one nag spent; the last one fails the mission with FailMadLibId.
    virtual bool CountDown();

    FMissionScript* Mission;          // +0x34
    unsigned int TargetId;            // +0x38 SpaceObj id of the target label (0 = position)
    float Distance;                   // +0x3c (ctor 2000.0)
    unsigned int FailMadLibId;        // +0x40 (ctor 1)
    Vector Position;                  // +0x44 the target's position
    Vector LastPosition;              // +0x50 the player's position at the last check
    float Threshold;                  // +0x5c (900.0 per timer start)
    unsigned char bPositionMode;      // +0x60 Say speaks the position lines (ctor 0)
    unsigned char bNagInCombat;       // +0x61 (ctor 0)
    unsigned char bNagOutOfCombat;    // +0x62 (ctor 1)
    unsigned char bLeavingArea;       // +0x63 (ctor 0) "nag_leaving_mission_area"
    unsigned int TimerId;             // +0x64 pub::SetTimer id (DAT_06fce76c)
    unsigned char bArmed;             // +0x68 (ctor 0)
    int NagsLeft;                     // +0x6c (ctor 4)
};

// FUN_06ecef10 (thiscall on the mission's +0x204 map, RET 8): message 0x3b moves the nags of
// the entering player to its system; unless NagClamp (+0x218) is set, every nag listening to
// MessageId gets it, and a done one is unlinked (its object is not freed).
void DispatchNagMessage(FMissionScript* Script, pub::Controller::EMessage MessageId,
                        void const* MessageParms);

// Act_NagGreet: new(0x44), ctor FUN_06ecbf90, vtable 06fb5e9c.
class ActNagGreet : public Act
{
public:
    explicit ActNagGreet(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecc000
    bool Execute() override;                 // LAB_06ecc080

    unsigned int NagId;      // +0x0c ID_String CreateID(s0)
    TString<0x30> Speaker;   // +0x10 s1
};

// Act_NagDistLeaving: new(0x94), ctor FUN_06ecb4a0, vtable 06fb5e48.
class ActNagDistLeaving : public Act
{
public:
    explicit ActNagDistLeaving(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecb550
    bool Execute() override;                 // LAB_06ecb800

    unsigned int NagId;             // +0x0c ID_String CreateID(s0)
    TString<0x30> Speaker;          // +0x10 s1
    TString<0x30> Target;           // +0x44 s2 (label, or "position")
    Vector Position;                // +0x78 (ctor 0) f3..f5 with "position"
    unsigned char bHasPosition;     // +0x84 (ctor 0)
    float Distance;                 // +0x88 (ctor 4000.0)
    unsigned int FailMadLibId;      // +0x8c (ctor 1)
    unsigned char bNagInCombat;     // +0x90 (ctor 0)
    unsigned char bNagOutOfCombat;  // +0x91 (ctor 1)
};

// Act_NagDistTowards: new(0x90), ctor FUN_06ecb9e0, vtable 06fb5e88.
class ActNagDistTowards : public Act
{
public:
    explicit ActNagDistTowards(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ecba90
    bool Execute() override;                 // LAB_06ecbda0

    unsigned int NagId;             // +0x0c ID_String CreateID(s1)
    TString<0x30> Speaker;          // +0x10 s2
    TString<0x30> Target;           // +0x44 s3 ("OBJ" form)
    float Distance;                 // +0x78 (ctor 4000.0)
    unsigned int FailMadLibId;      // +0x7c (ctor 1)
    Vector Position;                // +0x80 (ctor 0) f3..f5 ("POS" form)
    unsigned char bNagInCombat;     // +0x8c (ctor 0)
    unsigned char bNagOutOfCombat;  // +0x8d (ctor 1)
};

// Act_NagOff: new(0x10), ctor FUN_06ecc310, vtable 06fb5ec0.
class ActNagOff : public Act
{
public:
    explicit ActNagOff(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec7900
    bool Execute() override;                 // LAB_06ecc370

    unsigned int NagId; // +0x0c ID_String CreateID(s0)
};

// ---------------------------------------------------------------------------------------
// Objects, formations and scripts
// ---------------------------------------------------------------------------------------

// Act_AdjHealth: new(0x14), ctor FUN_06ec4e50, vtable 06fb5ab8.
class ActAdjHealth : public Act
{
public:
    explicit ActAdjHealth(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec4ec0
    bool Execute() override;                 // FUN_06ec4f00

    unsigned int FormationId; // +0x0c ID_String CreateID(s0): key of the mission +0xec multimap
    float Delta;              // +0x10 (ctor 0) f1
};

// Act_CallThorn: new(0xd4), ctor FUN_06ec51f0, vtable 06fb5ac4.
class ActCallThorn : public Act
{
public:
    explicit ActCallThorn(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec52e0
    bool Execute() override;                 // LAB_06ec54b0

    TString<0x30> Target;    // +0x0c "" / s1 / "Player"
    TString<0x80> Script;    // +0x40 s0
    Vector Position;         // +0xc4 (ctor 0) (i1, i2, i3) as floats
    int Mode;                // +0xd0 (ctor 0) 1 object, 2 position
};

// Act_Cloak: new(0x44), ctor FUN_06ec5660, vtable 06fb5adc.
class ActCloak : public Act
{
public:
    explicit ActCloak(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec5690
    bool Execute() override;                 // LAB_06ec5700

    TString<0x30> Formation; // +0x0c s0 (CreateID -> the +0xec multimap key)
    unsigned char bCloak;    // +0x40 (ctor 0) b1
};

// Act_LockDock: new(0x78), ctor FUN_06ec5cb0, vtable 06fb5b00.
class ActLockDock : public Act
{
public:
    explicit ActLockDock(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec5d60
    bool Execute() override;                 // LAB_06ec5e60

    TString<0x30> Target; // +0x0c s0: "Player" or "Everyone"
    TString<0x30> Gate;   // +0x40 s1: gate nickname (pub::GetNicknameId)
    // +0x74 s2: "lock" -> 1, "unlock" -> 0, anything else leaves it. Retail's ctor never writes
    // it (raw operator new memory); the port starts it at -1, a value Execute ignores like any
    // other that is neither 0 nor 1.
    int Mode;
};

// Act_Destroy: new(0x44), ctor FUN_06ec7f20, vtable 06fb5bec.
class ActDestroy : public Act
{
public:
    explicit ActDestroy(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec7f90
    bool Execute() override;                 // FUN_06ec8050

    TString<0x30> Label;     // +0x0c s0
    int DestroyMode;         // +0x40 (ctor 1) "EXPLODE" 1 / "SILENT" 0
};

// Act_LightFuse: new(0x88), ctor FUN_06ec58a0, vtable 06fb5ae8.
class ActLightFuse : public Act
{
public:
    explicit ActLightFuse(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec5960
    bool Execute() override;                 // LAB_06ec5a40

    TString<0x40> Fuse;      // +0x0c s1
    TString<0x30> Label;     // +0x50 s0
    float Delay;             // +0x84 (ctor 0) optional f2
};

// Act_AddAmbient: new(0x9c), ctor FUN_06ec7b50, vtable 06fb5bc8. +0x0c is the 0x90-byte
// ambient scene record appended to mission +0x310.
class ActAddAmbient : public Act
{
public:
    explicit ActAddAmbient(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec7b90
    bool Execute() override;                 // LAB_06ec7c30

    TString<0x80> Path;      // +0x0c s0
    int Handle;              // +0x90 (ctor -1)
    unsigned int RoomId;     // +0x94 (ctor 0) pub::GF::FindLocation(base, s1)
    unsigned int BaseId;     // +0x98 (ctor 0) pub::GetBaseID(s2)
};

// Act_RemoveAmbient: new(0x90), ctor FUN_06ece1f0, vtable 06fb5fec.
class ActRemoveAmbient : public Act
{
public:
    explicit ActRemoveAmbient(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec1be0
    bool Execute() override;                 // FUN_06ece220

    TString<0x80> Path;      // +0x0c s0
};

// Act_AddRTC: new(0x94), ctor FUN_06ec7cd0, vtable 06fb5bd4.
class ActAddRtc : public Act
{
public:
    explicit ActAddRtc(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec7d40
    bool Execute() override;                 // LAB_06ec7de0

    TString<0x80> Path;       // +0x0c s0
    unsigned char bRepeatable; // +0x90 (ctor 0) s1 == "repeatable"
};

// Act_PilotParams: new(0x44), ctor FUN_06ecd3e0, vtable 06fb5f3c.
class ActPilotParams : public Act
{
public:
    explicit ActPilotParams(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecd410
    bool Execute() override;                 // FUN_06ecd4c0

    int FleeMode;            // +0x0c (not set by the ctor) DAT_06fce300 value of s1
    TString<0x30> Label;     // +0x10 s0
};

// Act_PopUpDialog: new(0x18), ctor FUN_06ecd560, vtable 06fb5f54.
class ActPopUpDialog : public Act
{
public:
    explicit ActPopUpDialog(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ecd5a0
    bool Execute() override;                 // FUN_06ecd6a0

    unsigned int TitleId;    // +0x0c (ctor 1) i0
    unsigned int TextId;     // +0x10 (ctor 1) i1
    unsigned int Buttons;    // +0x14 (ctor 0) YES_NO 3, YES_NO_LATER 7, CLOSE 8
};

// Act_SetPriority: new(0x14), ctor FUN_06ecd7d0, vtable 06fb5f78.
class ActSetPriority : public Act
{
public:
    explicit ActSetPriority(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecd840
    bool Execute() override;                 // LAB_06ecd8b0

    unsigned int FormationId; // +0x0c ID_String CreateID(s0)
    int Priority;             // +0x10 (ctor 0) NORMAL 1, ALWAYS_EXECUTE 0
};

// Act_SetFlee: new(0x8c), ctor FUN_06ec8b50, vtable 06fb5c64.
class ActSetFlee : public Act
{
public:
    explicit ActSetFlee(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec8b90
    bool Execute() override;                 // LAB_06ec8d40

    TString<0x30> Npc;        // +0x0c s1
    int Mode;                 // +0x40 (ctor 2) SET_POS 0, SET_OBJ 1, CLEAR 2
    Vector Position;          // +0x44 SET_POS f2..f4 (not set by the ctor)
    TString<0x30> Target;     // +0x50 SET_OBJ s2
    float Distance;           // +0x84 f5 / f3 (not set by the ctor)
    unsigned char bFlag;      // +0x88 b6 / b4 (not set by the ctor); SetFleeObject's third
};

// Act_SetLifeTime: new(0x44), ctor FUN_06ec8dd0, vtable 06fb5c88.
class ActSetLifeTime : public Act
{
public:
    explicit ActSetLifeTime(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec8e00
    bool Execute() override;                 // LAB_06ec8e70

    TString<0x30> Label;      // +0x0c s0 (an NPC, else a label of the +0xa4 NPCs)
    float LifeTime;           // +0x40 (ctor 1.0f) f1
};

// Act_GiveMB / Act_GiveObjList: new(0x44), ctors FUN_06ecac70 / FUN_06ecad90, vtables
// 06fb5df4 / 06fb5e0c, shared Parse FUN_06ecae40.
class ActGiveMb : public Act
{
public:
    explicit ActGiveMb(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecae40
    bool Execute() override;                 // LAB_06ecace0

    TString<0x30> Npc;        // +0x0c s0
    unsigned int ListId;      // +0x40 ID_String CreateID(s1): [MetaBehavior] nickname
};

class ActGiveObjList : public Act
{
public:
    explicit ActGiveObjList(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecae40
    bool Execute() override;                 // LAB_06ecaec0

    TString<0x30> Npc;        // +0x0c s0 (an NPC, or "Player")
    unsigned int ListId;      // +0x40 ID_String CreateID(s1): [ObjList] nickname
};

// Act_Invulnerable: new(0x48), ctor FUN_06ecb000, vtable 06fb5e24.
class ActInvulnerable : public Act
{
public:
    explicit ActInvulnerable(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecb030
    bool Execute() override;                 // LAB_06ecb0d0

    TString<0x30> Label;         // +0x0c s0: "Player", a formation (+0xec key) or a label
    unsigned char bInvulnerable; // +0x40 (ctor 0) b1
    unsigned char bParam3;       // +0x41 (ctor 0) optional b2: SetInvincible's third argument
    float Duration;              // +0x44 (ctor 0) optional f3
};

// Act_MarkObj: new(0x44), ctor FUN_06ec5fc0, vtable 06fb5b34.
class ActMarkObj : public Act
{
public:
    explicit ActMarkObj(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec6050
    bool Execute() override;                 // LAB_06ec60c0

    TString<0x30> Label;     // +0x0c s0: a formation (+0xec key) or a label
    int Mode;                // +0x40 (ctor 0) i1: pub::Player::MarkObj mode (0 unmark, 1 mark)
};

// Act_Jumper: new(0x14), ctor FUN_06ec5a90, vtable 06fb5af4.
class ActJumper : public Act
{
public:
    explicit ActJumper(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec5b00
    bool Execute() override;                 // LAB_06ec5b40

    unsigned int LabelId;    // +0x0c ID_String CreateID(s0): a label on the NPCs (+0x17c)
    unsigned char bJumper;   // +0x10 (ctor 0) b1
};

// Act_SendComm: new(0xbc), ctor FUN_06ec4910, vtable 06fb5a9c.
class ActSendComm : public Act
{
public:
    explicit ActSendComm(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec49f0
    bool Execute() override;                 // LAB_06ec4cf0

    TString<0x30> Sender;    // +0x0c s0 (label)
    TString<0x30> Receiver;  // +0x40 s1 (label, "Player" or "anyone")
    unsigned int TextId;     // +0x74 (ctor 0) pub::GetNicknameId(s2)
    // +0x78 (ctor: the four parts and the count 0, the accessories not written) s6 head
    // ("NO_HEAD" 0), s7 body, s8.. accessories.
    Costume SpeakerCostume;
    unsigned int VoiceId;    // +0xac (ctor 0) pub::GetVoiceID(s4)
    unsigned int CommType;   // +0xb0 (ctor 0) optional i5
    int Duration;            // +0xb4 (ctor -1) optional i3; kept only for -1 and 1..10
};

// Act_EtherComm: new(0x84), ctor FUN_06ec8730, vtable 06fb5c40.
class ActEtherComm : public Act
{
public:
    explicit ActEtherComm(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ec87c0
    bool Execute() override;                 // LAB_06ec8990

    TString<0x30> Receiver;  // +0x0c s2 (label)
    // +0x40 (ctor: the four parts and the count 0, the accessories not written) s5 head
    // ("NO_HEAD" 0), s6 body, s7.. accessories.
    Costume SpeakerCostume;
    unsigned int VoiceId;    // +0x74 (ctor 0) pub::GetVoiceID(s0)
    unsigned int CommType;   // +0x78 (ctor 0) i1
    unsigned int TextId;     // +0x7c (ctor 0) pub::GetNicknameId(s3)
    int Duration;            // +0x80 (ctor -1) i4; kept only for -1 and 1..10
};

// Act_SetOrient: new(0x50), ctor FUN_06ec93a0, vtable 06fb5cf8.
class ActSetOrient : public Act
{
public:
    explicit ActSetOrient(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec93d0
    bool Execute() override;                 // LAB_06ec9470

    TString<0x30> Label;       // +0x0c s0
    FQuaternion Orientation;   // +0x40 f1..f4 (W, X, Y, Z; not normalized; not set by the ctor)
};

// Act_RelocateForm / Act_RelocateShip: new(0x70), ctors FUN_06ecdce0 / FUN_06ece060, vtables
// 06fb5fc8 / 06fb5fd4, shared Parse FUN_06ecdd30.
class ActRelocateForm : public Act
{
public:
    explicit ActRelocateForm(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecdd30
    bool Execute() override;                 // FUN_06ecdea0

    TString<0x30> Label;     // +0x0c s0: the formation's NPC
    FTransform Placement;    // +0x40 rotation (identity) from the normalized quaternion
                             // f4..f7, +0x64 position f1..f3
};

class ActRelocateShip : public Act
{
public:
    explicit ActRelocateShip(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ecdd30
    bool Execute() override;                 // LAB_06ece100

    TString<0x30> Label;     // +0x0c s0: a label
    FTransform Placement;    // +0x40 / +0x64 as Act_RelocateForm
};

// Act_RemoveCargo: new(0x44), ctor FUN_06ece2a0, vtable 06fb5ff8.
class ActRemoveCargo : public Act
{
public:
    explicit ActRemoveCargo(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ece2d0
    bool Execute() override;                 // LAB_06ece350

    TString<0x30> Loot;      // +0x0c s0: a [MsnLoot] nickname
    int Count;               // +0x40 (ctor 1) optional i1
};

// Act_RepChangeRequest: new(0x24), ctor FUN_06ece430, vtable 06fb600c (own dtor FUN_06ece470,
// the vector at +0x14).
class ActRepChangeRequest : public Act
{
public:
    explicit ActRepChangeRequest(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ece4c0
    bool Execute() override;                 // LAB_06ece590

    unsigned char bShown;               // +0x0c (ctor 0; Parse leaves it) -> mission +0x2cc
    unsigned char bArmed;               // +0x0d (ctor 0; Parse sets 1) -> mission +0x2cd
    unsigned int SystemId;              // +0x10 (ctor 0) pub::GetSystemID(s0) -> mission +0x2d0
    std::vector<unsigned int> Groups;   // +0x14 GetReputationGroup(s1..s49), unique -> +0x2d4
};

// Act_StaticCam: new(0x44), ctor FUN_06ece960, vtable 06fb6048.
class ActStaticCam : public Act
{
public:
    explicit ActStaticCam(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ece9b0
    bool Execute() override;                 // FUN_06eceae0

    Transform Camera;        // +0x0c rotation (identity) from the normalized quaternion f3..f6,
                             // +0x30 position f0..f2
    float Fov;               // +0x3c (ctor 0) optional f7
    float Aspect;            // +0x40 (ctor 0) optional f8
};

// Act_NNPath: new(0x58), ctor FUN_06ecc3f0, vtable 06fb5ecc.
class ActNnPath : public Act
{
public:
    explicit ActNnPath(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // LAB_06ecc420
    bool Execute() override;                 // LAB_06ecc610

    Vector Position;         // +0x0c f2..f4, or the named system object's position (not set by
                             // the ctor)
    TString<0x30> Label;     // +0x18 s2 (label form)
    unsigned int SystemId;   // +0x4c (ctor 0) pub::GetSystemID(s5 / s3)
    int TravelIds;           // +0x50 i0 (not set by the ctor)
    int TargetIds;           // +0x54 i1 (not set by the ctor)
};

// Act_PlayerForm: new(0x44), ctor FUN_06eccec0, vtable 06fb5f0c.
class ActPlayerForm : public Act
{
public:
    explicit ActPlayerForm(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06eccf30
    bool Execute() override;                 // LAB_06eccfc0

    TString<0x30> Npc;       // +0x0c s1
    FFormation* Formation;   // +0x40 FUN_06efd1a0(CreateID(s0)) (not set by the ctor)
};

// Act_SetRep: new(0x7c), ctor FUN_06ec9620, vtable 06fb5d94.
class ActSetRep : public Act
{
public:
    explicit ActSetRep(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec96a0
    bool Execute() override;                 // LAB_06ec97e0

    TString<0x30> Label;     // +0x0c s0 (a label, or "Player")
    TString<0x30> Faction;   // +0x40 s1
    unsigned int GroupId;    // +0x74 (ctor 0) pub::Reputation::GetReputationGroup(s1)
    float Value;             // +0x78 (ctor 0) f2, or the REP_* name of value 2
};

// Act_SetShipAndLoadout: new(0x68), ctor FUN_06ec98b0, vtable 06fb5da0 (own dtor FUN_06ec98f0,
// the EquipDescVector at +0x54).
class ActSetShipAndLoadout : public Act
{
public:
    explicit ActSetShipAndLoadout(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec9940
    bool Execute() override;                 // LAB_06ec9a70

    TString<0x20> LoadoutName; // +0x0c with value 1 present: value 0 again (as retail)
    TString<0x20> ShipName;    // +0x30 s0
    EquipDescVector Loadout;   // +0x54 pub::GetLoadout of GetLoadoutID(s1) unless "none"
    unsigned int ShipArchId;   // +0x64 (ctor 0) pub::GetShipID(s0) unless "none"
};

// Act_SetVibe: new(0x7c), ctor FUN_06ec9df0, vtable 06fb5db8.
class ActSetVibe : public Act
{
public:
    explicit ActSetVibe(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec9ea0
    bool Execute() override;                 // LAB_06eca050

    TString<0x30> LabelA;    // +0x0c s0
    TString<0x30> LabelB;    // +0x40 s1
    float Lo;                // +0x74 (ctor 0) value 2
    float Hi;                // +0x78 (ctor 0) value 3, else value 2
};

// Act_SetVibeLbl: new(0x1c), ctor FUN_06eca0a0, vtable 06fb5dc4.
class ActSetVibeLbl : public Act
{
public:
    explicit ActSetVibeLbl(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06eca130
    bool Execute() override;                 // FUN_06eca2b0

    unsigned int LabelA;     // +0x0c ID_String CreateID(s0) (ctor CreateID(null) = 0)
    unsigned int LabelB;     // +0x10 ID_String CreateID(s1) (ctor CreateID(null) = 0)
    float Lo;                // +0x14 (ctor 0) value 2
    float Hi;                // +0x18 (ctor 0) value 3, else value 2
};

// Act_SetVibeLblToShip: new(0x4c), ctor FUN_06eca550, vtable 06fb5dd0.
class ActSetVibeLblToShip : public Act
{
public:
    explicit ActSetVibeLblToShip(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06eca610
    bool Execute() override;                 // FUN_06eca7a0

    unsigned int FormationId; // +0x0c ID_String CreateID(s0): the +0xec multimap key
    TString<0x30> Ship;       // +0x10 s1 (label)
    float Lo;                 // +0x44 value 2 (not set by the ctor)
    float Hi;                 // +0x48 value 3, else value 2 (not set by the ctor)
};

// Act_SetVibeShipToLbl: new(0x4c), ctor FUN_06eca860, vtable 06fb5ddc.
class ActSetVibeShipToLbl : public Act
{
public:
    explicit ActSetVibeShipToLbl(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06eca920
    bool Execute() override;                 // FUN_06ecaab0

    TString<0x30> Ship;       // +0x0c s0 (label)
    unsigned int FormationId; // +0x40 ID_String CreateID(s1): the +0xec multimap key
    float Lo;                 // +0x44 value 2 (not set by the ctor)
    float Hi;                 // +0x48 value 3, else value 2 (not set by the ctor)
};

// ---------------------------------------------------------------------------------------
// Spawners: a copy of a mission object record handed to the TU38 creators
// (FMissionObject::SpawnMissionShipGroup / SpawnMissionSolar / SpawnMissionLoot).
// ---------------------------------------------------------------------------------------

// Act_SpawnShip: new(0xac), ctor FUN_06ec62b0, vtable 06fb5b40.
class ActSpawnShip : public Act
{
public:
    explicit ActSpawnShip(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec63f0
    bool Execute() override;                 // FUN_06ec6580

    TString<0x30> Name;        // +0x0c s0: [MsnShip] / [MsnFormation] nickname
    TString<0x30> NewNickname; // +0x40 (ctor "") the first slot's nickname with +0xa6
    FTransform Placement;      // +0x74 rotation (identity) from the normalized quaternion f5..f8,
                               // +0x98 position f2..f4
    unsigned char bPosition;   // +0xa4 (ctor 0) value 2 present
    unsigned char bOrientation; // +0xa5 (ctor 0) value 5 present
    unsigned char bRename;     // +0xa6 (ctor 0) no writer in the listing
    unsigned int ObjList;      // +0xa8 ID_String CreateID(s1): the first slot's init_objectives
};

// Act_SpawnShipRel: new(0xe8), ctor FUN_06ec69c0, vtable 06fb5b54.
class ActSpawnShipRel : public Act
{
public:
    explicit ActSpawnShipRel(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec6aa0
    bool Execute() override;                 // FUN_06ec6c40

    TString<0x30> Name;        // +0x0c s0: a ship slot or [MsnShip] / [MsnFormation] nickname
    TString<0x30> RelObj;      // +0x40 s2: label of the reference object
    TString<0x30> NewNickname; // +0x74 s8 (with +0xdd)
    FTransform Offset;         // +0xa8 rotation identity (never parsed), +0xcc offset f4..f6
    // +0xd8 s3: "POINT" 0, "DIRECTIONAL" 1, "RELATIVE" 2, else unchanged. Retail's ctor never
    // writes it; the port starts it at -1, a mode FUN_06ecebc0 treats like any other that is not
    // 0..2 (rotation only).
    int Mode;
    unsigned char Field_0xdc;  // +0xdc (ctor 0) TODO: no reader or writer found
    unsigned char bRename;     // +0xdd (ctor 0) value 8 present
    unsigned int ObjList;      // +0xe0 ID_String CreateID(s1)
    float Heading;             // +0xe4 (ctor -1.0f) optional f7, degrees about Y (< 0: none)
};

// Act_SpawnSolar / Act_SpawnFormation / Act_SpawnLoot: new(0x74), ctors FUN_06ec6fc0 /
// FUN_06ec7130 / FUN_06ec73f0, vtables 06fb5b80 / 06fb5b8c / 06fb5b98, shared Parse
// FUN_06ec7450.
class ActSpawnSolar : public Act
{
public:
    explicit ActSpawnSolar(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec7450
    bool Execute() override;                 // FUN_06ec7060

    TString<0x30> Name;         // +0x0c s0: [MsnSolar] nickname
    FTransform Placement;       // +0x40 rotation (identity) from the normalized quaternion
                                // f4..f7, +0x64 position f1..f3
    unsigned char bPosition;    // +0x70 (ctor 0) value 1 present
    unsigned char bOrientation; // +0x71 (ctor 0) value 4 present
};

class ActSpawnFormation : public Act
{
public:
    explicit ActSpawnFormation(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec7450
    bool Execute() override;                 // FUN_06ec71d0

    TString<0x30> Name;         // +0x0c s0: [MsnShip] / [MsnFormation] nickname
    FTransform Placement;       // +0x40 / +0x64 as Act_SpawnSolar
    unsigned char bPosition;    // +0x70
    unsigned char bOrientation; // +0x71
};

class ActSpawnLoot : public Act
{
public:
    explicit ActSpawnLoot(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ec7450
    bool Execute() override;                 // FUN_06ec75d0

    TString<0x30> Name;         // +0x0c s0: [MsnLoot] nickname
    FTransform Placement;       // +0x40 / +0x64 as Act_SpawnSolar
    unsigned char bPosition;    // +0x70
    unsigned char bOrientation; // +0x71
};

// Act_StartDialog: new(0x18), ctor FUN_06ece7f0, vtable 06fb603c.
class ActStartDialog : public Act
{
public:
    explicit ActStartDialog(FMissionScript* InMission);
    void Parse(INI_Reader& Reader) override; // FUN_06ece860
    bool Execute() override;                 // FUN_06ece8c0

    unsigned int DialogId;      // +0x0c ID_String: CreateID(s0), a [Dialog] nickname
    int DelaySeconds;           // +0x10 i1 (ctor -1): 1..10 (and -1) arm the delay
    // +0x14 (ctor 0): the next Execute queues the action on the mission timers (+0x1f8) for
    // DelaySeconds instead of starting the dialog.
    unsigned char bDelayPending;
};
