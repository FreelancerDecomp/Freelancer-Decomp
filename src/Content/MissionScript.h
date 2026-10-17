// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "content.h"
#include "MissionObject.h"
#include "common_reputation.h"  // Costume
#include "x86math.h"

#include <cstdio>
#include <list>
#include <map>
#include <vector>

class Cnd;
class Act;
class FMissionNag;
class INI_Reader;
struct FMissionScript;

// ---------------------------------------------------------------------------------------
// TU21 (06eb7360-06ed3d90): the script mission (ctor FUN_06eb75a0, vtable 06fb4710, 31 slots)
// over the TU38 mission object, and its trigger engine. GenericMission and Mission_01a..13
// (StoryMissions.cpp) derive from it.
// ---------------------------------------------------------------------------------------

// DAT_06fcf014: TU21's math engine, created on first use (FUN_06fa8fa0) by
// Cnd_DistCircle::OnActivate and the Act_* classes that transform vectors.
extern I3DMathEngine* GTriggerMathEngine;

// DAT_06fd1c79 ("scan_clamp"): 0 by slot 22 (06eb8188), 1 by the offer acceptance
// (06eba191), loaded by FUN_06ebae10 (06ebb1a3), saved by FUN_06ebabb0 (06ebac14), read by
// TU37 (06f0c6be).
extern unsigned char GMissionScanClamp;

int const G_TriggerStateActive = 0;   // "ACTIVE" (FUN_06ebb850 06ebf635)
int const G_TriggerStateInactive = 1; // "INACTIVE" (06ebf64c); default (06eb85a5)
int const G_TriggerStateComplete = 2; // "COMPLETE" (06ebf667)

// Mission states (+0x2a8 / +0x2ac, LAB_06eb8260 names).
int const G_MissionStateFail = 0;
int const G_MissionStateSucceed = 1;
int const G_MissionStateNoState = 2;

// One [Trigger] section: the value of the mission+0x1b0 map (node +0x10), built on the stack
// in FUN_06eb84c0 (06eb85a5-06eb85e6), filled by FUN_06ebb850 and copied into the map
// (FUN_06ed0030). The copy shares Condition/Actions pointers; FUN_06ebb7f0 deletes them.
struct FTrigger
{
    int State;                 // +0x00, default INACTIVE
    Cnd* Condition;            // +0x04
    std::list<Act*> Actions;   // +0x08
    FMissionScript* Mission;   // +0x14
    std::vector<pub::Controller::EMessage> EventIds; // +0x18: message ids the condition listens to
    unsigned char bRepeatable; // +0x28 "repeatable"
    unsigned int Id;           // +0x2c CreateID(nickname)
};

typedef std::map<unsigned int, FTrigger> FTriggerMap;

// mission+0x1f8 list node value (+0x08 remaining seconds, +0x0c action), pushed by delayed
// actions (e.g. Act_StartDialog 06ece903-06ece94b) and run by FUN_06eb9c60.
struct FMissionTimer
{
    float Remaining;
    Act* Action;
};

// The 0x54-byte object FUN_06eb8a20 news for a started RTC script (TU23, RtcScripts.h).
struct FCharacterEncounter;

// The value of the mission's +0x2f0 and +0x310 lists (0x90 bytes at node+8, node 0x98,
// FUN_06ed0f20): +0 path length, +4 char[0x80] path, +0x84 handle (-1), +0x88 object,
// +0x8c/+0x8d flags. Built on the stack by Mission_01b (06ea2343-06ea23a7) and copied in
// (0x24 dwords). Field names are node offsets (value offset + 8).
struct FStoryMissionListNode_0x2f4
{
    unsigned int PathLength;
    char Path[0x80];
    int Field_0x8c;                     // handle: RTC location slot / ambient script (-1 none)
    FCharacterEncounter* Field_0x90;    // +0x90 the started script (0x2f0 list) / room (0x310)
    unsigned char Field_0x94;           // +0x94 finished (0x2f0 list)
    unsigned char Field_0x95;           // +0x95 "rtc = %s, true/false" (FUN_06ebabb0)

    FStoryMissionListNode_0x2f4()
        : PathLength(0)
        , Field_0x8c(-1)
        , Field_0x90(nullptr)
        , Field_0x94(0)
        , Field_0x95(0)
    {
        Path[0] = '\0';
    }
};

// The value of the mission's +0x310 list (0x90 bytes; its node allocator is ICF-folded with
// the +0x2f0 list's, the fields are not): +0 path length, +4 char[0x80] ambient script path,
// +0x84 pub::GF ambient script handle (-1 none), +0x88 room id, +0x8c base id.
// Created by Act_AddAmbient, started on base enter (FUN_06eb9dc0), stopped on base exit
// (FUN_06eb9ef0), saved as "ambi_scene = %s, %u, %u" (FUN_06ebabb0).
struct FMissionAmbientScene
{
    TString<0x80> Path;                 // +0x00
    int Handle;                         // +0x84
    unsigned int RoomId;                // +0x88
    unsigned int BaseId;                // +0x8c
};

// The TU21 script mission (see the file comment). Offsets are retail object offsets.
struct FMissionScript : FMissionObject
{
    // FUN_06eb75a0 (RET 8).
    FMissionScript(unsigned int const* InControllerId, FStoryMissionCreateParms const* Parms);
    // Slot 1, FUN_06eb79d0 -> FUN_06eb7d30 (member teardown) -> operator delete.
    ~FMissionScript() override;

    // Slot 19, LAB_06eba010: the TU38 objective timer only while the NN path is idle.
    void UpdateObjectiveTimer(pub::Controller::EMessage MessageId, void const* MessageParms) override;
    // Slot 21, FUN_06eb8f50: the TU21 message dispatcher (calls the TU38 body directly).
    void HandleMissionMessage(pub::Controller::EMessage MessageId, void const* MessageParms) override;
    // Slot 22, LAB_06eb8070: tear the script down, then the TU38 body FUN_06f0fc40.
    void EndMission() override;
    // Slot 23, FUN_06eb9760: SUCCEED display; Mission_13 overrides it.
    void OnMissionSucceed() override;
    // Slot 24, LAB_06eb9870: FAIL display.
    void OnMissionFail() override;
    // Slot 25, FUN_06eb84c0 (RET 4): FUN_06f172f0 (the TU38 body), then [Trigger] / [NPC] /
    // [Dialog].
    void LoadMission(INI_Reader* Reader) override;
    // Slot 26, LAB_06eb8260: "FAIL" -> 0, "SUCCEED" -> 1, "NO_STATE" -> 2, else -1 (stricmp).
    int MissionStateFromName(char const* Name) override;
    // Slot 28, FUN_06ebab90 (RET 4): message 0x100f with the argument as the parms.
    void Slot28(void const* Parms) override;
    // Slot 29 (vtbl+0x74), LAB_06eb9730 (RET 8): unless (+0x2b0 == 1 && State == FAIL):
    // +0x2ac = State, +0x2e8 = MadLibId.
    virtual void SetDesiredMissionState(int State, unsigned int MadLibId);
    // Slot 30 (vtbl+0x78), FUN_06eb9aa0: apply +0x2ac when it differs from +0x2a8.
    virtual void ApplyDesiredMissionState();

    // The per-mission ctor stores (+0x2b4, +0x2b8, +0x2c0, +0x2c4), e.g. 06ea27b2-06ea27d0.
    void SetChapter(int SlotCurrent, int SlotSucceed, unsigned int Value2c0, unsigned int Value2c4);
    // The tail of every mission ctor (e.g. 06ea27da-06ea280a): a stack INI_Reader,
    // FUN_06f24280 (open the DATA-relative ini; the result is not tested), FUN_06eb84c0
    // (LoadMission), INI_Reader::close.
    void LoadMissionIni(char const* IniPath);

    // FUN_06eb8f50 case 1 -> FUN_06eb9dc0 (RET 4): base enter.
    void OnBaseEnter(void const* MessageParms);
    // FUN_06eb8f50 case 2 -> FUN_06eb9ef0 (RET 4): base exit.
    void OnBaseExit(void const* MessageParms);
    // FUN_06eb8f50 case 0x1b -> FUN_06eba350 (RET 4): character selected in a bar.
    void OnCharacterSelect(void const* MessageParms);
    // FUN_06eb8f50 case 0x2a -> FUN_06eba530 (RET 4): location exit.
    void OnLocationExit(void const* MessageParms);
    // FUN_06eb8f50 case 0x2c -> FUN_06eba030 (RET 4): the player's answer to the offer.
    void OnOfferResponse(void const* MessageParms);
    // FUN_06eb8f50 case 0x36 -> FUN_06eba720 (RET 4): space enter (dialog costume hints).
    void OnSpaceEnter(void const* MessageParms);
    // FUN_06eb8f50 case 0x39 -> FUN_06ebaae0 (RET 4): object destroyed.
    void OnObjectDestroyed(void const* MessageParms);
    // FUN_06eb8f50 case 0x3b -> FUN_06eba860 (RET 4): system enter.
    void OnSystemEnter(void const* MessageParms);
    // FUN_06eb8f50 case 0x3b -> FUN_06eba1f0 (RET 4): the reputation offer pop-up.
    void OnSystemEnterPopUp(void const* MessageParms);
    // FUN_06eb8f50 case 0x24 -> FUN_06eb9cd0 (RET 4).
    void OnMessage24(void const* MessageParms);
    // FUN_06eb8f50 case 0x1005 -> FUN_06eb99a0 (RET 4): attitude of a killer towards a victim.
    void OnMessage1005(void const* MessageParms);
    // FUN_06eb8f50 case 0x100b -> FUN_06eba8f0 (RET 4).
    void OnMessage100b(void const* MessageParms);
    // FUN_06ebabb0 (RET 4): the [MissionState] body of the save (message 4).
    void SaveMissionState(FILE* File);
    // FUN_06ebae10 (RET 4): the save load (message 5).
    void LoadMissionState(INI_Reader* Reader);
    // FUN_06ebb620 (stdcall, RET 8, no this): the NPC's job personality (Mode 0 / 1).
    static void SubmitNpcPersonality(FContentNpcShip* Npc, int Mode);
    // FUN_06eb8b70: stop and free every started RTC script, then empty +0x2f0 (dtor, slot 22).
    void ReleaseRtcScripts();
    // FUN_06eb8a20 (RET 4): load Rtc->Path as a TU23 script; when the player is docked at the
    // script's base, start its bar scene (Rtc->Field_0x8c) and keep it in Rtc->Field_0x90,
    // otherwise free it. Act_AddRTC, Mission_01b, OnBaseEnter, OnMessage100b.
    void StartRtcScript(FStoryMissionListNode_0x2f4* Rtc);
    // FUN_06eb8d10 (plain RET): undo Act_StaticCam -- +0x2c8 = 0, the player ship vulnerable
    // again, NagClamp = 0, every NPC's +0x1b1 cleared, the ship handed one Avoidance(true)
    // directive, the GCS clamp off. Act_RevertCam.
    void RevertStaticCam();
    // FUN_06ec9b30 (RET 0x10): the attitude of ObjA towards ObjB (FUN_06f682c0, TU53
    // SetObjectAttitude) and of the rest of ObjA's formation when ObjA is an NPC leading it or
    // flying alone; the reputation of each non-mission solar involved is kept once in +0x174.
    // Returns ObjA's attitude. Act_SetVibe / SetVibeLbl / SetVibeLblToShip / SetVibeShipToLbl.
    float SetObjectVibe(unsigned int ObjA, unsigned int ObjB, float Lo, float Hi);

    // Trigger engine members (ctor 06eb75d9-06eb766f).
    FTriggerMap TriggerMap;                                   // +0x1b0 all triggers by id
    std::list<FTriggerMap::iterator> ActiveTriggers;          // +0x1c4 active (map nodes)
    std::map<unsigned int, unsigned int> PendingActivations;  // +0x1d0 target -> source id
    std::map<unsigned int, unsigned int> InitiallyActive;     // +0x1e4 InitState ACTIVE ids
    std::list<FMissionTimer> Timers;                          // +0x1f8 delayed actions
    // +0x204: map<id, nag object> (ctor FUN_06f1f4f0, unique keys; Act_NagGreet /
    // Act_NagDistTowards / Act_NagDistLeaving insert, Act_NagOff erases, FUN_06ecef10 gets the
    // messages, slot 22 / the dtor delete every value). MissionAct.h.
    std::map<unsigned int, FMissionNag*> Nags;
    unsigned char NagClamp = 0;                               // +0x218 (ctor 0 @ 06eb768f)
    FMissionNnPath NnPath;                                    // +0x21c (0x88 bytes)
    IStoryMissionEvents* StoryEvents;                         // +0x2a4 = CreateParms +8
    int PreviousMissionState;                                 // +0x2a8 (ctor 2)
    int MissionState;                                         // +0x2ac desired (ctor 2)
    // +0x2b0 (ctor 0): when 1, SetDesiredMissionState drops FAIL requests.
    unsigned char Field_0x2b0;
    int CampaignSlotCurrent;                                  // +0x2b4 (mission ctor)
    int CampaignSlotSucceed;                                  // +0x2b8 (mission ctor)
    int StoryCue;                                             // +0x2bc "story_cue" (ctor 0)
    unsigned int Field_0x2c0;                                 // +0x2c0 TODO: meaning (mission ctor)
    unsigned int Field_0x2c4;                                 // +0x2c4 objective ids of the accepted offer (06eba19e)
    unsigned char Field_0x2c8;                                // +0x2c8 TODO: meaning (ctor 0)
    // +0x2cc: the reputation pop-up is up (FUN_06eba1f0 sets it, message 0x30 answers it).
    unsigned char bRepPopUpShown;
    // +0x2cd: the reputation pop-up is armed (cleared by the answer and the acceptance).
    unsigned char bRepPopUpArmed;
    unsigned int RepPopUpSystemId;                            // +0x2d0 system the pop-up waits for
    std::vector<unsigned int> RepPopUpGroups;                 // +0x2d4 groups set to 0.6 on "yes"
    // +0x2e4 ID_String (ctor clear @ 06eb771c): trigger id of the last Act_Save (06ec484b);
    // written by the trigger save as "trigger = %u" (06ebade7).
    unsigned int SaveTriggerId = 0;
    // +0x2e8 (ctor 1): mad-lib id shown with the state change (Act_ChangeState +0x10, message
    // 0x1006, FUN_06eb99a0).
    unsigned int Field_0x2e8;
    // +0x2ec (ctor 0): objectives need a renumber (base enter / space enter clear it).
    unsigned char Field_0x2ec;
    // +0x2f0 (head +0x2f4, size +0x2f8): started RTC scripts. Pushed by Mission_01b (06ea23a7)
    // and FUN_06eb8a20's callers; walked by FUN_06eb8b70, FUN_06eb9ef0 and Cnd_RTCDone.
    std::list<FStoryMissionListNode_0x2f4> List_0x2f4;
    // +0x2fc (ctor FUN_06ed0440, node 0x3c; size +0x304): labels named by Act_DisableFriendlyFire
    // (LAB_06ec8270 appends each one not yet listed).
    std::list<TString<0x30>> FriendlyFireOffLabels;
    // +0x308: location id of the player's last location enter on a base (FUN_06eb8f50 case
    // 0x29, 06eb92dd-06eb9308). Not written by the ctor. Read by Cnd_CharSelect /
    // Cnd_RumorHeard.
    unsigned int CurrentLocationId = 0;
    // +0x30c: mission time in seconds (ctor 0; FUN_06eb8f50 case 0x20 adds the frame time,
    // 06eb8fb1). Read by Cnd_NPCSystemEnter and Cnd_TetherBroke.
    float ElapsedTime = 0.0f;
    // +0x310 (head +0x314, size +0x318): ambient scenes.
    std::list<FMissionAmbientScene> AmbientScenes;
    // +0x31c: costumes of the [Dialog] "line = ..., Player" speakers (FUN_06eb84c0, unique by
    // FUN_06eb8ef0), sent as CostumeHint on space enter (FUN_06eba720).
    std::vector<Costume> DialogCostumes;
};

// FUN_06eb8ef0 (thiscall on A, RET 4): the four parts, the accessory count and the first
// count accessories are equal.
bool IsSameCostume(Costume const* A, Costume const* B);

// The unique push_back of a speaker costume into mission +0x31c (FUN_06eb8ef0 test,
// FUN_06ed1d30 insert), inlined by FUN_06eb84c0 [Dialog] and the comm Acts.
void AddDialogCostume(FMissionScript* Script, Costume const* SpeakerCostume);

// FUN_06ebb850 (thiscall on the trigger, RET 4): reads the rest of the [Trigger] section
// into Trigger (Cnd_* / Act_* objects, InitState, repeatable).
void LoadTrigger(FTrigger* Trigger, INI_Reader* Reader);

// FUN_06eb84c0 [Trigger] branch (06eb8527-06eb86f0): the values of one [Trigger] section
// ("nickname" builds and inserts a trigger through LoadTrigger, "State" is read and dropped).
void LoadMissionTriggerSection(FMissionScript* Script, INI_Reader* Reader);

// FUN_06ebb830: State = ACTIVE, then Condition->OnActivate() when set.
void RearmTrigger(FTrigger* Trigger);

// FUN_06ebb7f0: deletes Condition and every action (scalar deleting dtor, flag 1).
void DeleteTriggerContents(FTrigger* Trigger);

// FUN_06eb82c0: activate the trigger with *TriggerId (no-op when unknown or already listed).
void ActivateMissionTrigger(FMissionScript* Script, unsigned int const* TriggerId);

// FUN_06eb83e0: drop *TriggerId from the active list (State = INACTIVE) and from the
// pending-activation map.
void DeactivateMissionTrigger(FMissionScript* Script, unsigned int const* TriggerId);

// FUN_06eb9af0: run message *EventId through the active triggers, then drain pending
// activations queued by Act_ActTrig.
void DispatchMissionTriggerEvent(FMissionScript* Script, pub::Controller::EMessage const* EventId,
                                 void const* EventParms);

// FUN_06eba430 (thiscall, RET 4): every started script of +0x2f4 whose path equals Path->Data
// is marked finished (+0x94 of the node); when the player stands in the mission's location
// (+0x308) of their base, the bar character named by the script's role gets its behavior
// cleared (-1). Act_RemoveRTC, Mission_01b.
void RemoveMissionRtc(FMissionScript* Script, TString<0x80> const* Path);

// FUN_06ebb540 (thiscall, RET 8): when the player's ship is farther than sqrt(RadiusSq) from
// *Position, relocate it there (FUN_06f67740). Act_MovePlayer.
void MoveMissionPlayer(FMissionScript* Script, Vector const* Position, float RadiusSq);

// FUN_06eb9c60: count the delayed-action list down by DeltaSeconds and run expired ones.
void TickMissionTimers(FMissionScript* Script, float DeltaSeconds);

// FUN_06eb8f50 case 0x1000 (06eb94ec-06eb9541): activate every InitState ACTIVE trigger.
void ActivateInitialMissionTriggers(FMissionScript* Script);
