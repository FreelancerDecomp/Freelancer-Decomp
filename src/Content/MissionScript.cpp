// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MissionScript.h"
#include "ContentStaticIds.h"   // TU21's 16 static-init thunks 06eb7360-06eb7540
#include "ContentDefaultId.h"   // TU21's "default" thunk 06eb7560
#include "BaseDB.h"
#include "ContentInternal.h"
#include "ContentPlayerState.h"
#include "Encounters.h"
#include "MissionAct.h"
#include "MissionCnd.h"
#include "MissionDialogs.h"
#include "MissionObjList.h"     // CreateObjListDirective, FObjListAvoidance (FUN_06eb8d10)
#include "RefHandle.h"          // TRefHandle
#include "BehaviorCmds.h"       // FContentShipBase::SetDirectives
#include "DockQueues.h"
#include "NpcShips.h"
#include "PilotPersonalities.h" // GetPilotPersonality (FUN_06f3e020)
#include "Populators.h"
#include "RandomMissionDB.h"
#include "RtcScripts.h"
#include "SystemInfo.h"
#include "common_goods.h"
#include "common_id.h"
#include "common_iface.h"
#include "common_ini.h"
#include "common_server.h"
#include "common_server_controller.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string.h>
#include <windows.h>

I3DMathEngine* GTriggerMathEngine = nullptr;

// DAT_06fd1c79 (see MissionScript.h).
unsigned char GMissionScanClamp = 0;

// DAT_06fcf018: stored 0 by the TU21 ctor (06eb7799); the listing has no reader (XREF[1] W).
unsigned int GMissionScriptDat06fcf018 = 0;

namespace
{
    int const MissionStateFail = G_MissionStateFail;
    int const MissionStateSucceed = G_MissionStateSucceed;
    int const MissionStateNoState = G_MissionStateNoState;
}

// =======================================================================================
// Retail trigger engine (TU21). See MissionScript.h.
// =======================================================================================

namespace
{
    template <class T>
    Cnd* NewTriggerCondition(FMissionScript* Mission)
    {
        return new T(Mission);
    }

    template <class T>
    Act* NewTriggerAction(FMissionScript* Mission)
    {
        return new T(Mission);
    }

    // One Cnd_* key of FUN_06ebb850: the class and the message ids pushed into trigger +0x18
    // after Parse (MOV [ESP+0x10],id before the shared tails LAB_06ebcbbc / LAB_06ebbbdf /
    // LAB_06ebc059 / LAB_06ebceb2, or the inline FUN_06f937c0 / FUN_06eec320 pushes).
    // The controller message ids (common_server_controller.h), used throughout this file.
    using pub::Controller::EMessage;

    struct FCndFactoryEntry
    {
        char const* Name;
        Cnd* (*Create)(FMissionScript* Mission);
        int EventCount;
        EMessage EventIds[3];   // past EventCount: unused (value-initialized)
    };

    // In the order of the is_value chain (06ebb890-06ebce72).
    FCndFactoryEntry const GCndFactory[] = {
        {"Cnd_BaseEnter", &NewTriggerCondition<CndBaseEnter>, 1, {EMessage::BaseEnter}},
        {"Cnd_BaseExit", &NewTriggerCondition<CndBaseExit>, 1, {EMessage::BaseExit}},
        {"Cnd_CargoScanned", &NewTriggerCondition<CndCargoScanned>, 1, {EMessage::CargoScanned}},
        {"Cnd_CharSelect", &NewTriggerCondition<CndCharSelect>, 1, {EMessage::GFObjSelect}},
        {"Cnd_CommComplete", &NewTriggerCondition<CndCommComplete>, 1, {EMessage::CommComplete}},
        {"Cnd_CmpToPlane", &NewTriggerCondition<CndCmpToPlane>, 1, {EMessage::Heartbeat}},
        {"Cnd_Destroyed", &NewTriggerCondition<CndDestroyed>, 1, {EMessage::ObjectDestroyed}},
        {"Cnd_DistCircle", &NewTriggerCondition<CndDistCircle>, 1, {EMessage::Heartbeat}},
        {"Cnd_DistShip", &NewTriggerCondition<CndDistShip>, 1, {EMessage::Heartbeat}},
        {"Cnd_DistVec", &NewTriggerCondition<CndDistVec>, 1, {EMessage::Heartbeat}},
        {"Cnd_DistVecLbl", &NewTriggerCondition<CndDistVecLbl>, 1, {EMessage::Heartbeat}},
        {"Cnd_EncLaunched", &NewTriggerCondition<CndEncLaunched>, 1, {EMessage::EncounterLaunched}},
        {"Cnd_HasMsn", &NewTriggerCondition<CndHasMsn>, 1, {EMessage::Heartbeat}},
        {"Cnd_HealthDec", &NewTriggerCondition<CndHealthDec>, 1, {EMessage::ObjectDamaged}},
        {"Cnd_InSpace", &NewTriggerCondition<CndInSpace>, 1, {EMessage::Heartbeat}},
        {"Cnd_InTradelane", &NewTriggerCondition<CndInTradelane>, 1, {EMessage::Heartbeat}},
        {"Cnd_InZone", &NewTriggerCondition<CndInZone>, 1, {EMessage::Heartbeat}},
        {"Cnd_JumpgateAct", &NewTriggerCondition<CndJumpgateAct>, 1, {EMessage::JumpgateAct}},
        {"Cnd_JumpInComplete", &NewTriggerCondition<CndJumpInComplete>, 1, {EMessage::JumpInComplete}},
        {"Cnd_LaunchComplete", &NewTriggerCondition<CndLaunchComplete>, 1, {EMessage::LaunchComplete}},
        {"Cnd_LocEnter", &NewTriggerCondition<CndLocEnter>, 1, {EMessage::LocationEnter}},
        {"Cnd_LocExit", &NewTriggerCondition<CndLocExit>, 1, {EMessage::LocationExit}},
        {"Cnd_LootAcquired", &NewTriggerCondition<CndLootAcquired>, 1, {EMessage::LootAcquired}},
        {"Cnd_MsnResponse", &NewTriggerCondition<CndMsnResponse>, 1, {EMessage::MissionResponse}},
        {"Cnd_NPCSystemEnter", &NewTriggerCondition<CndNpcSystemEnter>, 3,
         {EMessage::NpcSystemEnter, EMessage::Heartbeat, EMessage::NpcShipCreated}},
        {"Cnd_NPCSystemExit", &NewTriggerCondition<CndNpcSystemExit>, 1, {EMessage::NpcSystemExit}},
        {"Cnd_PlayerLaunch", &NewTriggerCondition<CndPlayerLaunch>, 1, {EMessage::PlayerLaunch}},
        {"Cnd_PlayerManeuver", &NewTriggerCondition<CndPlayerManeuver>, 1, {EMessage::PlayerManeuver}},
        {"Cnd_PopUpDialog", &NewTriggerCondition<CndPopUpDialog>, 1, {EMessage::PopUpDialog}},
        {"Cnd_ProjHit", &NewTriggerCondition<CndProjHit>, 1, {EMessage::ObjectDamaged}},
        {"Cnd_ProjHitShipToLbl", &NewTriggerCondition<CndProjHitShipToLbl>, 1, {EMessage::ObjectDamaged}},
        {"Cnd_RTCDone", &NewTriggerCondition<CndRtcDone>, 1, {EMessage::RtcDone}},
        {"Cnd_RumorHeard", &NewTriggerCondition<CndRumorHeard>, 1, {EMessage::RumorHeard}},
        {"Cnd_SpaceEnter", &NewTriggerCondition<CndSpaceEnter>, 1, {EMessage::SpaceEnter}},
        {"Cnd_SpaceExit", &NewTriggerCondition<CndSpaceExit>, 1, {EMessage::SpaceExit}},
        {"Cnd_SystemEnter", &NewTriggerCondition<CndSystemEnter>, 1, {EMessage::SystemEnter}},
        {"Cnd_SystemExit", &NewTriggerCondition<CndSystemExit>, 1, {EMessage::SystemExit}},
        {"Cnd_TetherBroke", &NewTriggerCondition<CndTetherBroke>, 1, {EMessage::TetherObject}},
        {"Cnd_Timer", &NewTriggerCondition<CndTimer>, 1, {EMessage::Heartbeat}},
        {"Cnd_TLEntered", &NewTriggerCondition<CndTlEntered>, 1, {EMessage::TradelaneEnter}},
        {"Cnd_TLExited", &NewTriggerCondition<CndTlExited>, 1, {EMessage::TradelaneExit}},
        {"Cnd_True", &NewTriggerCondition<CndTrue>, 1, {EMessage::Heartbeat}},
        {"Cnd_WatchTrigger", &NewTriggerCondition<CndWatchTrigger>, 1, {EMessage::Heartbeat}},
        {"Cnd_WatchVibe", &NewTriggerCondition<CndWatchVibe>, 1, {EMessage::Heartbeat}},
    };

    // One Act_* key of FUN_06ebb850 (new, ctor, Parse, push_back into trigger +0x08 with
    // FUN_06ecfc30, FUN_06ebf700 with trigger +0x2c).
    struct FActFactoryEntry
    {
        char const* Name;
        Act* (*Create)(FMissionScript* Mission);
    };

    // In the order of the is_value chain (06ebceea-06ebf59c).
    FActFactoryEntry const GActFactory[] = {
        {"Act_ActTrig", &NewTriggerAction<ActActTrig>},
        {"Act_AddAmbient", &NewTriggerAction<ActAddAmbient>},
        {"Act_AddRTC", &NewTriggerAction<ActAddRtc>},
        {"Act_AdjAcct", &NewTriggerAction<ActAdjAcct>},
        {"Act_AdjHealth", &NewTriggerAction<ActAdjHealth>},
        {"Act_CallThorn", &NewTriggerAction<ActCallThorn>},
        {"Act_ChangeState", &NewTriggerAction<ActChangeState>},
        {"Act_Cloak", &NewTriggerAction<ActCloak>},
        {"Act_DeactTrig", &NewTriggerAction<ActDeactTrig>},
        {"Act_DebugMsg", &NewTriggerAction<ActDebugMsg>},
        {"Act_Destroy", &NewTriggerAction<ActDestroy>},
        {"Act_DisableEnc", &NewTriggerAction<ActDisableEnc>},
        {"Act_DisableFriendlyFire", &NewTriggerAction<ActDisableFriendlyFire>},
        {"Act_DisableTradelane", &NewTriggerAction<ActDisableTradelane>},
        {"Act_DockRequest", &NewTriggerAction<ActDockRequest>},
        {"Act_EnableEnc", &NewTriggerAction<ActEnableEnc>},
        {"Act_EnableManeuver", &NewTriggerAction<ActEnableManeuver>},
        {"Act_EtherComm", &NewTriggerAction<ActEtherComm>},
        {"Act_ForceLand", &NewTriggerAction<ActForceLand>},
        {"Act_GCSClamp", &NewTriggerAction<ActGcsClamp>},
        {"Act_GiveMB", &NewTriggerAction<ActGiveMb>},
        {"Act_GiveNNObjs", &NewTriggerAction<ActGiveNnObjs>},
        {"Act_GiveObjList", &NewTriggerAction<ActGiveObjList>},
        {"Act_HostileClamp", &NewTriggerAction<ActHostileClamp>},
        {"Act_Invulnerable", &NewTriggerAction<ActInvulnerable>},
        {"Act_Jumper", &NewTriggerAction<ActJumper>},
        {"Act_LightFuse", &NewTriggerAction<ActLightFuse>},
        {"Act_LockDock", &NewTriggerAction<ActLockDock>},
        {"Act_LockManeuvers", &NewTriggerAction<ActLockManeuvers>},
        {"Act_MarkObj", &NewTriggerAction<ActMarkObj>},
        {"Act_MovePlayer", &NewTriggerAction<ActMovePlayer>},
        {"Act_NagClamp", &NewTriggerAction<ActNagClamp>},
        {"Act_NagDistLeaving", &NewTriggerAction<ActNagDistLeaving>},
        {"Act_NagDistTowards", &NewTriggerAction<ActNagDistTowards>},
        {"Act_NagGreet", &NewTriggerAction<ActNagGreet>},
        {"Act_NagOff", &NewTriggerAction<ActNagOff>},
        {"Act_NNPath", &NewTriggerAction<ActNnPath>},
        {"Act_NNIds", &NewTriggerAction<ActNnIds>},
        {"Act_PlayerCanDock", &NewTriggerAction<ActPlayerCanDock>},
        {"Act_PlayerCanTradelane", &NewTriggerAction<ActPlayerCanTradelane>},
        {"Act_PlayerEnemyClamp", &NewTriggerAction<ActPlayerEnemyClamp>},
        {"Act_PlayerForm", &NewTriggerAction<ActPlayerForm>},
        {"Act_PlayMusic", &NewTriggerAction<ActPlayMusic>},
        {"Act_PlayNN", &NewTriggerAction<ActPlayNn>},
        {"Act_PlaySoundEffect", &NewTriggerAction<ActPlaySoundEffect>},
        {"Act_PilotParams", &NewTriggerAction<ActPilotParams>},
        {"Act_PobjIdle", &NewTriggerAction<ActPobjIdle>},
        {"Act_PopUpDialog", &NewTriggerAction<ActPopUpDialog>},
        {"Act_SetPriority", &NewTriggerAction<ActSetPriority>},
        {"Act_RandomPop", &NewTriggerAction<ActRandomPop>},
        {"Act_RandomPopSphere", &NewTriggerAction<ActRandomPopSphere>},
        {"Act_RelocateForm", &NewTriggerAction<ActRelocateForm>},
        {"Act_RelocateShip", &NewTriggerAction<ActRelocateShip>},
        {"Act_RemoveAmbient", &NewTriggerAction<ActRemoveAmbient>},
        {"Act_RemoveCargo", &NewTriggerAction<ActRemoveCargo>},
        {"Act_RemoveRTC", &NewTriggerAction<ActRemoveRtc>},
        {"Act_RepChangeRequest", &NewTriggerAction<ActRepChangeRequest>},
        {"Act_RevertCam", &NewTriggerAction<ActRevertCam>},
        {"Act_RpopAttClamp", &NewTriggerAction<ActRpopAttClamp>},
        {"Act_RpopTLAttacksEnabled", &NewTriggerAction<ActRpopTlAttacksEnabled>},
        {"Act_Save", &NewTriggerAction<ActSave>},
        {"Act_SendComm", &NewTriggerAction<ActSendComm>},
        {"Act_SetFlee", &NewTriggerAction<ActSetFlee>},
        {"Act_SetInitialPlayerPos", &NewTriggerAction<ActSetInitialPlayerPos>},
        {"Act_SetLifeTime", &NewTriggerAction<ActSetLifeTime>},
        {"Act_SetNNHidden", &NewTriggerAction<ActSetNnHidden>},
        {"Act_SetNNObj", &NewTriggerAction<ActSetNnObj>},
        {"Act_SetNNState", &NewTriggerAction<ActSetNnState>},
        {"Act_SetOffer", &NewTriggerAction<ActSetOffer>},
        {"Act_SetOrient", &NewTriggerAction<ActSetOrient>},
        {"Act_SetRep", &NewTriggerAction<ActSetRep>},
        {"Act_SetShipAndLoadout", &NewTriggerAction<ActSetShipAndLoadout>},
        {"Act_SetTitle", &NewTriggerAction<ActSetTitle>},
        {"Act_SetVibe", &NewTriggerAction<ActSetVibe>},
        {"Act_SetVibeLbl", &NewTriggerAction<ActSetVibeLbl>},
        {"Act_SetVibeLblToShip", &NewTriggerAction<ActSetVibeLblToShip>},
        {"Act_SetVibeShipToLbl", &NewTriggerAction<ActSetVibeShipToLbl>},
        {"Act_SetVibeOfferBaseHack", &NewTriggerAction<ActSetVibeOfferBaseHack>},
        {"Act_SpawnFormation", &NewTriggerAction<ActSpawnFormation>},
        {"Act_SpawnLoot", &NewTriggerAction<ActSpawnLoot>},
        {"Act_SpawnShip", &NewTriggerAction<ActSpawnShip>},
        {"Act_SpawnShipRel", &NewTriggerAction<ActSpawnShipRel>},
        {"Act_SpawnSolar", &NewTriggerAction<ActSpawnSolar>},
        {"Act_StartDialog", &NewTriggerAction<ActStartDialog>},
        {"Act_StaticCam", &NewTriggerAction<ActStaticCam>},
    };
}

// FUN_06ebb850 (thiscall on the trigger, RET 4). Every value of the rest of the [Trigger]
// section: a Cnd_* key replaces +0x04 (the previous condition is not deleted), is parsed,
// pushes its message ids into +0x18 and gets the trigger id (+0x2c) at +0x08; an Act_* key is
// parsed, appended to +0x08 and gets the trigger id; "InitState" = ACTIVE 0 / INACTIVE 1 /
// COMPLETE 2 (stricmp, anything else leaves the state); "repeatable" true sets +0x28 (false
// leaves it); "system" is tested and ignored.
void LoadTrigger(FTrigger* Trigger, INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        bool bMatched = false;
        for (int Index = 0; Index < static_cast<int>(sizeof(GCndFactory) / sizeof(GCndFactory[0]));
             ++Index)
        {
            FCndFactoryEntry const& Entry = GCndFactory[Index];
            if (!Reader->is_value(Entry.Name))
                continue;
            Cnd* const Condition = Entry.Create(Trigger->Mission);
            Trigger->Condition = Condition;
            Condition->Parse(*Reader);
            for (int EventIndex = 0; EventIndex < Entry.EventCount; ++EventIndex)
                Trigger->EventIds.push_back(Entry.EventIds[EventIndex]);
            Trigger->Condition->SetTriggerId(Trigger->Id);
            bMatched = true;
            break;
        }
        if (bMatched)
            continue;
        for (int Index = 0; Index < static_cast<int>(sizeof(GActFactory) / sizeof(GActFactory[0]));
             ++Index)
        {
            FActFactoryEntry const& Entry = GActFactory[Index];
            if (!Reader->is_value(Entry.Name))
                continue;
            Act* const Action = Entry.Create(Trigger->Mission);
            Action->Parse(*Reader);
            Trigger->Actions.push_back(Action);
            Action->SetTriggerId(Trigger->Id);
            bMatched = true;
            break;
        }
        if (bMatched)
            continue;
        if (Reader->is_value("InitState"))
        {
            char const* const StateName = Reader->get_value_string(0);
            if (_stricmp("ACTIVE", StateName) == 0)
                Trigger->State = G_TriggerStateActive;
            else if (_stricmp("INACTIVE", StateName) == 0)
                Trigger->State = G_TriggerStateInactive;
            else if (_stricmp("COMPLETE", StateName) == 0)
                Trigger->State = G_TriggerStateComplete;
            continue;
        }
        if (Reader->is_value("repeatable"))
        {
            if (Reader->get_value_bool(0))
                Trigger->bRepeatable = 1;
            continue;
        }
        // 06ebf692: is_value("system"); the result is not used.
        Reader->is_value("system");
    }
}

// FUN_06eb84c0 [Trigger] branch (06eb8527-06eb86f0), after is_header("Trigger").
void LoadMissionTriggerSection(FMissionScript* Script, INI_Reader* Reader)
{
    // 06eb8527: the local ID_String, cleared.
    unsigned int TriggerId = 0;
    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            TriggerId = CreateID(Reader->get_value_string(0));
            // 06eb8580-06eb858f: FUN_06f7b0f0 (find); a known nickname is skipped.
            if (Script->TriggerMap.find(TriggerId) != Script->TriggerMap.end())
                continue;
            // 06eb8595-06eb85e6: the stack trigger (State INACTIVE, no condition, empty
            // lists, mission = this, repeatable 0, id).
            FTrigger Trigger;
            Trigger.State = G_TriggerStateInactive;
            Trigger.Condition = nullptr;
            Trigger.Mission = Script;
            Trigger.bRepeatable = 0;
            Trigger.Id = TriggerId;
            LoadTrigger(&Trigger, Reader);
            // 06eb85ff-06eb8668: {id, copy} (FUN_06ed0030) inserted into +0x1b0 (FUN_06ed06d0);
            // the copies share the condition and action pointers.
            Script->TriggerMap.insert(FTriggerMap::value_type(TriggerId, Trigger));
            // 06eb866d-06eb8694: an InitState ACTIVE trigger also goes into +0x1e4 as {id, id}.
            if (Trigger.State == G_TriggerStateActive)
                Script->InitiallyActive.insert(
                    std::map<unsigned int, unsigned int>::value_type(TriggerId, TriggerId));
            continue;
        }
        // 06eb86c3-06eb86dd: "State" goes through vtbl+0x68 and the result is dropped.
        if (Reader->is_value("State"))
            static_cast<void>(Script->MissionStateFromName(Reader->get_value_string(0)));
    }
}

// FUN_06ebb830.
void RearmTrigger(FTrigger* Trigger)
{
    Trigger->State = G_TriggerStateActive;
    if (Trigger->Condition != nullptr)
        Trigger->Condition->OnActivate();
}

// FUN_06ebb7f0: vtbl[0](1) on +0x04 when set, then on every non-null list value.
void DeleteTriggerContents(FTrigger* Trigger)
{
    if (Trigger->Condition != nullptr)
        delete Trigger->Condition;
    std::list<Act*>::iterator It = Trigger->Actions.begin();
    for (; It != Trigger->Actions.end(); ++It)
    {
        if (*It != nullptr)
            delete *It;
    }
}

// FUN_06eb82c0.
void ActivateMissionTrigger(FMissionScript* Script, unsigned int const* TriggerId)
{
    FTriggerMap::iterator Found = Script->TriggerMap.find(*TriggerId);
    if (Found == Script->TriggerMap.end())
        return;

    // 06eb82ea-06eb8313: the whole active list is walked (no early exit); any node whose
    // key equals the id means the trigger is already active.
    bool bAlreadyActive = false;
    std::list<FTriggerMap::iterator>::iterator ActiveIt = Script->ActiveTriggers.begin();
    for (; ActiveIt != Script->ActiveTriggers.end(); ++ActiveIt)
    {
        if ((*ActiveIt)->first == *TriggerId)
            bAlreadyActive = true;
    }
    if (bAlreadyActive)
        return;

    FTrigger& Trigger = Found->second;
    Trigger.State = G_TriggerStateActive;
    if (Trigger.Condition != nullptr)
        Trigger.Condition->OnActivate();

    // 06eb8337: re-read State; the condition is called without a null check (-1, 0). -1 is
    // no message id: the activation probe, cast into the message-id type Matches takes.
    if (Trigger.State == G_TriggerStateActive &&
        Trigger.Condition->Matches(static_cast<EMessage>(-1), nullptr))
    {
        Trigger.State = G_TriggerStateComplete;
        std::list<Act*>::iterator ActIt = Trigger.Actions.begin();
        for (; ActIt != Trigger.Actions.end(); ++ActIt)
            (*ActIt)->Execute();
        // 06eb836f-06eb8385: inline copy of FUN_06ebb830.
        if (Trigger.bRepeatable != 0)
        {
            Trigger.State = G_TriggerStateActive;
            if (Trigger.Condition != nullptr)
                Trigger.Condition->OnActivate();
        }
    }

    // 06eb8388-06eb83c5: push_back the map node unless the trigger completed.
    if (Trigger.State != G_TriggerStateComplete)
        Script->ActiveTriggers.push_back(Found);
}

// FUN_06eb83e0.
void DeactivateMissionTrigger(FMissionScript* Script, unsigned int const* TriggerId)
{
    // 06eb83f8-06eb843d: first active node with this key whose trigger is not COMPLETE:
    // State = INACTIVE, node erased (--size).
    std::list<FTriggerMap::iterator>::iterator ActiveIt = Script->ActiveTriggers.begin();
    for (; ActiveIt != Script->ActiveTriggers.end(); ++ActiveIt)
    {
        FTrigger& Trigger = (*ActiveIt)->second;
        if ((*ActiveIt)->first == *TriggerId && Trigger.State != G_TriggerStateComplete)
        {
            Trigger.State = G_TriggerStateInactive;
            Script->ActiveTriggers.erase(ActiveIt);
            break;
        }
    }

    // 06eb843d-06eb84b7: erase every pending activation keyed by this id.
    std::map<unsigned int, unsigned int>::iterator PendingIt = Script->PendingActivations.begin();
    while (PendingIt != Script->PendingActivations.end())
    {
        if (PendingIt->first == *TriggerId)
            PendingIt = Script->PendingActivations.erase(PendingIt);
        else
            ++PendingIt;
    }
}

// FUN_06eb9af0. *EventId is the message id the caller (FUN_06eb8f50) stored in its local.
void DispatchMissionTriggerEvent(FMissionScript* Script, EMessage const* EventId,
                                 void const* EventParms)
{
    std::list<FTriggerMap::iterator>::iterator ActiveIt = Script->ActiveTriggers.begin();
    while (ActiveIt != Script->ActiveTriggers.end())
    {
        FTrigger& Trigger = (*ActiveIt)->second;

        // 06eb9b0e-06eb9b2d: linear scan of the trigger's event vector.
        bool bListens = false;
        std::vector<EMessage>::iterator EventIt = Trigger.EventIds.begin();
        for (; EventIt != Trigger.EventIds.end(); ++EventIt)
        {
            if (*EventIt == *EventId)
            {
                bListens = true;
                break;
            }
        }

        // 06eb9bb6: only ACTIVE triggers are evaluated; others are skipped, never erased.
        if (bListens && Trigger.State == G_TriggerStateActive)
        {
            if (Trigger.Condition->Matches(*EventId, EventParms))
            {
                Trigger.State = G_TriggerStateComplete;
                std::list<Act*>::iterator ActIt = Trigger.Actions.begin();
                for (; ActIt != Trigger.Actions.end(); ++ActIt)
                    (*ActIt)->Execute();
                if (Trigger.bRepeatable != 0)
                    RearmTrigger(&Trigger);
            }
            // 06eb9c1f-06eb9c53: a completed trigger leaves the active list.
            if (Trigger.State == G_TriggerStateComplete)
            {
                ActiveIt = Script->ActiveTriggers.erase(ActiveIt);
                continue;
            }
        }
        ++ActiveIt;
    }

    // 06eb9b39-06eb9b90: drain the pending map front to back. Retail also looks the
    // source id (node+0x10) up in the trigger map (FUN_06f7b0f0 @ 06eb9b63) and discards the
    // result; that lookup has no side effect and is not reproduced.
    while (!Script->PendingActivations.empty())
    {
        std::map<unsigned int, unsigned int>::iterator First = Script->PendingActivations.begin();
        unsigned int TargetId = First->first;
        Script->PendingActivations.erase(First);
        ActivateMissionTrigger(Script, &TargetId);
    }
    // 06eb9b92-06eb9ba7: FUN_06ed0e20 erase(begin, end).
    Script->PendingActivations.clear();
}

// FUN_06eb9c60.
void TickMissionTimers(FMissionScript* Script, float DeltaSeconds)
{
    std::list<FMissionTimer>::iterator It = Script->Timers.begin();
    while (It != Script->Timers.end())
    {
        It->Remaining = It->Remaining - DeltaSeconds;
        // FCOMP DAT_06fb444c (0.0) / TEST AH,0x41 / JP skip: runs when Remaining <= 0.
        if (It->Remaining <= 0.0f)
        {
            if (It->Action != nullptr)
                It->Action->Execute();
            It = Script->Timers.erase(It);
            continue;
        }
        ++It;
    }
}

// LAB_06eb8260: stricmp against DAT_06fb47c0 "FAIL", "SUCCEED", "NO_STATE"; miss -> EBX = -1.
int FMissionScript::MissionStateFromName(char const* Name)
{
    if (_stricmp(Name, "FAIL") == 0)
        return MissionStateFail;
    if (_stricmp(Name, "SUCCEED") == 0)
        return MissionStateSucceed;
    if (_stricmp(Name, "NO_STATE") == 0)
        return MissionStateNoState;
    return -1;
}

// LAB_06eb9730: CMP byte [+0x2b0],1 / JNZ store; TEST State / JZ skip.
void FMissionScript::SetDesiredMissionState(int State, unsigned int MadLibId)
{
    if (Field_0x2b0 == 1 && State == MissionStateFail)
        return;
    MissionState = State;
    Field_0x2e8 = MadLibId;
}

// FUN_06eb8f50 case 0x1000 (06eb94ec-06eb9541): in-order walk of mission+0x1e4, activating
// each key.
void ActivateInitialMissionTriggers(FMissionScript* Script)
{
    std::map<unsigned int, unsigned int>::iterator It = Script->InitiallyActive.begin();
    for (; It != Script->InitiallyActive.end(); ++It)
        ActivateMissionTrigger(Script, &It->first);
}

namespace
{
    // The script's +0x10 (Roles._First) passed as FindCharacter's role: the first role's
    // TString at +0x00; retail passes the raw pointer, null for an empty vector.
    TString<32> const* GetFirstRtcRole(FCharacterEncounter const* Script)
    {
        if (Script->Roles.empty())
            return nullptr;
        return &Script->Roles.front().Role;
    }
}

// FUN_06eba430.
void RemoveMissionRtc(FMissionScript* Script, TString<0x80> const* Path)
{
    std::list<FStoryMissionListNode_0x2f4>::iterator It = Script->List_0x2f4.begin();
    for (; It != Script->List_0x2f4.end(); ++It)
    {
        // 06eba458-06eba494: the inlined TString == (node path against Path->Data).
        if (strcmp(It->Path, Path->Data) != 0)
            continue;
        if (FindContentPlayerState(Script->PlayerId) == nullptr)
            continue;
        // 06eba4a6-06eba503: the player's base (FUN_06f47980), the room state of the
        // mission's location (FUN_06eac110 on +0x308), the script (+0x90), the player gate
        // again, then FBaseRoomState::FindCharacter(script +0x10, player) and
        // FBarCharacter::SetBehavior(-1). Any miss skips to the +0x94 store.
        FContentBase* const Base = Script->PlayerState->GetBaseRecord();
        if (Base != nullptr)
        {
            FBaseRoomState* const Room = Base->FindRoomState(&Script->CurrentLocationId);
            if (Room != nullptr && It->Field_0x90 != nullptr &&
                FindContentPlayerState(Script->PlayerId) != nullptr)
            {
                FBarCharacterList::iterator Character;
                if (Room->FindCharacter(GetFirstRtcRole(It->Field_0x90), Script->PlayerId,
                                        &Character))
                {
                    Character->SetBehavior(static_cast<unsigned long>(-1));
                }
            }
        }
        It->Field_0x94 = 1;
    }
}

// FUN_06ebb540.
void MoveMissionPlayer(FMissionScript* Script, Vector const* Position, float RadiusSq)
{
    if (FindContentPlayerState(Script->PlayerId) == nullptr)
        return;
    FContentPlayerShip* const Ship = Script->PlayerState->ShipObject;
    if (Ship == nullptr)
        return;
    // 06ebb56e: FUN_06eb70f0 (FContentSpaceObj::GetPosition); each delta is stored as a float.
    Vector ShipPosition;
    Ship->GetPosition(&ShipPosition);
    float const DeltaX = ShipPosition.x - Position->x;
    float const DeltaY = ShipPosition.y - Position->y;
    float const DeltaZ = ShipPosition.z - Position->z;
    // 06ebb59e-06ebb5c3: (y*y + z*z) + x*x on the FPU, FCOMP RadiusSq, TEST AH,0x41 / JNZ
    // skip: only a distance strictly beyond the radius (and not unordered) moves the ship.
    double const DistanceSq = static_cast<double>(DeltaY) * DeltaY
                              + static_cast<double>(DeltaZ) * DeltaZ
                              + static_cast<double>(DeltaX) * DeltaX;
    if (!(DistanceSq > static_cast<double>(RadiusSq)))
        return;
    // 06ebb5c5-06ebb613: the ship's orientation (GetLocation again), the player's system
    // (FUN_06f478c0 +0x48, no null check), then FUN_06f67740(&ship, &system, Position,
    // &orientation).
    Vector CurrentPosition;
    Matrix CurrentOrientation;
    pub::SpaceObj::GetLocation(Ship->SpaceObjId, CurrentPosition, CurrentOrientation);
    unsigned int const SystemId = Script->PlayerState->GetSystemRecord()->SystemId;
    unsigned int const ShipId = Script->PlayerState->ShipObject->SpaceObjId;
    RelocateShip(&ShipId, &SystemId, Position, &CurrentOrientation);
}

// =======================================================================================
// FMissionScript (TU21 script mission, vtable 06fb4710)
// =======================================================================================

// FUN_06eb75a0 (06eb75a0-06eb780a). The TU38 base ctor, the containers (06eb75d9-06eb7788),
// then 06eb778d-06eb77f1: vtable, +0x2b0 = 0, DAT_06fcf018 = 0, +0x2bc = 0, +0x2a8 = +0x2ac = 2,
// +0x2c8 = +0x2ec = 0, FUN_06f1ca50(parms+4) (the slot 18 body, called directly),
// +0x2a4 = parms+8, FUN_06f1b6d0(+0xc) (the slot 2 body, called directly), +0x30c = 0,
// +0x2e8 = 1. +0x2b4/+0x2b8/+0x2c0/+0x2c4 and +0x308 are left to the mission ctors.
FMissionScript::FMissionScript(unsigned int const* InControllerId,
                               FStoryMissionCreateParms const* Parms)
    : FMissionObject(InControllerId, Parms)
    , StoryEvents(nullptr)
    , PreviousMissionState(G_MissionStateNoState)
    , MissionState(G_MissionStateNoState)
    , Field_0x2b0(0)
    , CampaignSlotCurrent(0)
    , CampaignSlotSucceed(0)
    , StoryCue(0)
    , Field_0x2c0(0)
    , Field_0x2c4(0)
    , Field_0x2c8(0)
    , bRepPopUpShown(0)
    , bRepPopUpArmed(0)
    , RepPopUpSystemId(0)
    , Field_0x2e8(1)
    , Field_0x2ec(0)
{
    GMissionScriptDat06fcf018 = 0;
    FMissionObject::SetPlayer(Parms->PlayerId);
    StoryEvents = Parms->StoryEvents;
    FMissionObject::GetOrCreatePlayer(PlayerId);
    ElapsedTime = 0.0f;
}

// FUN_06eb7d30 (the members after the explicit loops are destroyed by their own dtors, then
// the TU38 base FUN_06f0ed00).
FMissionScript::~FMissionScript()
{
    // 06eb7d58-06eb7dbc: the active list, then every trigger's condition and actions.
    ActiveTriggers.clear();
    FTriggerMap::iterator TriggerIt = TriggerMap.begin();
    while (TriggerIt != TriggerMap.end())
    {
        DeleteTriggerContents(&TriggerIt->second);
        TriggerIt = TriggerMap.erase(TriggerIt);
    }
    TriggerMap.clear();
    // 06eb7dc3: FUN_06eb8b70.
    ReleaseRtcScripts();
    // 06eb7dc8-06eb7e0c: vtbl[0](1) on every non-null +0x204 value, erase (FUN_06ecfc80).
    std::map<unsigned int, FMissionNag*>::iterator NagIt = Nags.begin();
    while (NagIt != Nags.end())
    {
        if (NagIt->second != nullptr)
            delete NagIt->second;
        NagIt = Nags.erase(NagIt);
    }
    // 06eb7e0e-06eb7e31: +0x2fc erase(begin, end).
    FriendlyFireOffLabels.clear();
    // 06eb7e36-06eb7e88: every ambient scene with a handle is destroyed, then erased.
    std::list<FMissionAmbientScene>::iterator SceneIt = AmbientScenes.begin();
    for (; SceneIt != AmbientScenes.end(); ++SceneIt)
    {
        if (SceneIt->Handle != -1)
        {
            unsigned long Handle = static_cast<unsigned long>(SceneIt->Handle);
            pub::GF::AmbientScriptDestroy(&Handle);
            SceneIt->Handle = -1;
        }
    }
    AmbientScenes.clear();
}

// FUN_06eb8b70.
void FMissionScript::ReleaseRtcScripts()
{
    // 06eb8b75-06eb8b94: the player's base, null without a live player state.
    FContentBase* Base = nullptr;
    if (FindContentPlayerState(PlayerId) != nullptr)
        Base = PlayerState->GetBaseRecord();
    std::list<FStoryMissionListNode_0x2f4>::iterator It = List_0x2f4.begin();
    for (; It != List_0x2f4.end(); ++It)
    {
        if (It->Field_0x90 == nullptr)
            continue;
        if (Base != nullptr && It->Field_0x8c != -1)
        {
            // 06eb8bb9-06eb8bc3: FUN_06ea9fc0(Base, Field_0x8c, 0).
            Base->RemoveBarScene(It->Field_0x8c, nullptr);
            It->Field_0x8c = -1;
        }
        // 06eb8bcd-06eb8be7: FUN_06eb8c40 (the member-wise teardown) + free of the script.
        delete It->Field_0x90;
        It->Field_0x90 = nullptr;
    }
    // 06eb8bfb-06eb8c32: unlink and free every node.
    List_0x2f4.clear();
}

// FUN_06eb8a20 (06eb8a20-06eb8b6d, thiscall, RET 4).
void FMissionScript::StartRtcScript(FStoryMissionListNode_0x2f4* Rtc)
{
    // 06eb8a3e-06eb8a63: operator new 0x54 + FUN_06ed4420.
    FCharacterEncounter* Script = new FCharacterEncounter();
    // 06eb8a67-06eb8a8f: the player state (FUN_06f49b00 on +0xc) and its base (FUN_06f47980).
    if (FindContentPlayerState(PlayerId) != nullptr)
    {
        FContentBase* Base = PlayerState->GetBaseRecord();
        // 06eb8a91-06eb8aaa: FUN_06ed44a0(Rtc path), then the script's base against the
        // player's (MBase +0x48).
        if (Base != nullptr && Script->Load(Rtc->Path) && Base->BaseId == Script->BaseId)
        {
            // 06eb8aac-06eb8ac9: a mission text and no live mission (FUN_06f487c0 & 0x3fffffff
            // zero) -> +0x2c = +0x34.
            if (Script->MissionTextId != 0 && (PlayerState->GetLiveMission() & 0x3fffffff) == 0)
                Script->MissionTextId = Script->LoadedMissionTextId;
            // 06eb8acc-06eb8ad5: +0x40 = FUN_06efeb00().
            Script->SequenceId = AllocateFormationId();
            // 06eb8ad1-06eb8af1: Rtc +0x84 = FUN_06ea9640(Base, Script, +0xc, +0x4), then
            // Rtc +0x88 = Script.
            Rtc->Field_0x8c = Base->CreateBarScene(Script, PlayerId, ControllerId);
            Rtc->Field_0x90 = Script;
            return;
        }
    }
    // 06eb8afb-06eb8b58: FUN_06eb8c40 inlined + free.
    delete Script;
}

// FUN_06eb8d10 (06eb8d10-06eb8eeb, thiscall, plain RET).
void FMissionScript::RevertStaticCam()
{
    // 06eb8d2b-06eb8d39: no live player -> nothing.
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    // 06eb8d44-06eb8d62: +0x2c8 = 0, pub::SpaceObj::SetInvincible(ship id, 0, 1, 0) (the id is
    // not masked here), +0x218 = 0.
    Field_0x2c8 = 0;
    pub::SpaceObj::SetInvincible(PlayerState->GetShipId(), false, true, 0.0f);
    NagClamp = 0;
    // 06eb8d68-06eb8d93: FUN_06f33fd0(npc, 0) over the +0xa4 NPC map, in order.
    std::map<unsigned int, FContentNpcShip*>::iterator NpcIt = NpcObjects.begin();
    for (; NpcIt != NpcObjects.end(); ++NpcIt)
        NpcIt->second->SetField1b1(false);
    // 06eb8d95-06eb8ecd: with a player ship object (+0x10): a local directive list holding one
    // new Avoidance directive (FUN_06f24870 type 0, FUN_06f26020(1), pushed and AddRef'ed),
    // FUN_06f57840(&list, 0); then the local reference is released and the list's elements
    // released and erased.
    FContentPlayerShip* const Ship = PlayerState->ShipObject;
    if (Ship != nullptr)
    {
        FMissionDirectiveList Directives;
        TRefHandle<FObjListDirective> Directive;
        CreateObjListDirective(0, &Directive.Pointer);
        static_cast<FObjListAvoidance*>(Directive.Pointer)->SetAvoidance(true);
        Directives.Items.push_back(Directive.Pointer);
        Directive.Pointer->AddRef();
        Ship->SetDirectives(&Directives.Items, 0);
    }
    // 06eb8ed1-06eb8ed2: FUN_06f5cf60(0).
    SpacePopSetGcsClampEnabled(0);
}

void FMissionScript::SetChapter(int SlotCurrent, int SlotSucceed, unsigned int Value2c0,
                                unsigned int Value2c4)
{
    CampaignSlotCurrent = SlotCurrent;
    CampaignSlotSucceed = SlotSucceed;
    Field_0x2c0 = Value2c0;
    Field_0x2c4 = Value2c4;
}

void FMissionScript::LoadMissionIni(char const* IniPath)
{
    INI_Reader Reader;
    OpenContentDataIni(IniPath, &Reader);
    LoadMission(&Reader);
    Reader.close();
}

// LAB_06eba010: CMP byte [+0x290] (NnPath.bActive); zero -> JMP FUN_06f1b4f0.
void FMissionScript::UpdateObjectiveTimer(EMessage MessageId, void const* MessageParms)
{
    if (NnPath.bActive != 0)
        return;
    FMissionObject::UpdateObjectiveTimer(MessageId, MessageParms);
}

// FUN_06ebab90: FUN_06eb9af0(&0x100f, Arg).
void FMissionScript::Slot28(void const* Parms)
{
    EMessage EventId = EMessage::NpcSystemEnter;
    DispatchMissionTriggerEvent(this, &EventId, Parms);
}

// FUN_06eb9aa0 (06eb9aa0-06eb9aeb). Desired (+0x2ac) equal to Previous (+0x2a8) -> nothing;
// else Previous = Desired; 0 (FAIL) -> tail-jump vtbl[+0x60]; 1 (SUCCEED) -> vtbl[+0x5c], then
// StoryEvents(+0x2a4)->vtbl[0](3, +0x2b8, +4) if the facet is set (TEST ECX / JZ 06eb9ad5).
void FMissionScript::ApplyDesiredMissionState()
{
    int Desired = MissionState;
    if (Desired == PreviousMissionState)
        return;
    PreviousMissionState = Desired;
    if (Desired == G_MissionStateFail)
    {
        OnMissionFail();
        return;
    }
    if (Desired != G_MissionStateSucceed)
        return;
    OnMissionSucceed();
    if (StoryEvents != nullptr)
        StoryEvents->OnMissionEvent(3, static_cast<unsigned int>(CampaignSlotSucceed), ControllerId);
}

// FUN_06eb9760 (06eb9760-06eb9861): with a madlib id (+0x2e8 != 0) and a live player state:
// 1 -> 0x4cf, FmtStr madlib, DisplayMissionMessage(PlayerId, fmt, 1, true), then
// FUN_06f49090(+8, 7) and FUN_06f171a0(this); always FUN_06f0fda0(this) last.
void FMissionScript::OnMissionSucceed()
{
    if (Field_0x2e8 != 0 && FindContentPlayerState(PlayerId) != nullptr)
    {
        if (Field_0x2e8 == 1)
            Field_0x2e8 = 0x4cf;
        FmtStr MissionMsg;
        MissionMsg.begin_mad_lib(Field_0x2e8);
        MissionMsg.end_mad_lib();
        pub::Player::DisplayMissionMessage(PlayerId, MissionMsg, MISSION_MSG_TYPE_1, true);
        // 06eb9823-06eb982b: FUN_06f49090 on the cached player state (+0x08) with type 7.
        PlayerState->CountRandomMissionCompleted(7);
        DestroyMissionObjects();
    }
    FMissionObject::OnMissionSucceed();
}

// LAB_06eb9870 (06eb9870-06eb999d).
void FMissionScript::OnMissionFail()
{
    // 06eb988d-06eb9896: madlib 1 -> 0x412.
    if (Field_0x2e8 == 1)
        Field_0x2e8 = 0x412;
    // 06eb98a0-06eb990d: DisplayMissionMessage(&PlayerId, madlib, type 0, true).
    FmtStr MissionMsg;
    MissionMsg.begin_mad_lib(Field_0x2e8);
    MissionMsg.end_mad_lib();
    pub::Player::DisplayMissionMessage(PlayerId, MissionMsg, MISSION_MSG_TYPE_0, true);
    // 06eb9913-06eb996a: in-order walk of the +0xa4 NPC map; FUN_06f33fd0(npc, 1).
    std::map<unsigned int, FContentNpcShip*>::iterator It = NpcObjects.begin();
    for (; It != NpcObjects.end(); ++It)
        It->second->SetField1b1(true);
    // 06eb996e: FUN_06f171a0.
    DestroyMissionObjects();
    FMissionObject::OnMissionFail();
}

// LAB_06eb8070.
void FMissionScript::EndMission()
{
    // 06eb8078-06eb80bc: vtbl[0](1) on every non-null +0x204 value, erase (FUN_06f20e00).
    std::map<unsigned int, FMissionNag*>::iterator NagIt = Nags.begin();
    while (NagIt != Nags.end())
    {
        if (NagIt->second != nullptr)
            delete NagIt->second;
        NagIt = Nags.erase(NagIt);
    }
    // 06eb80be-06eb80d3: +0x1f8 erase(begin, end) (the actions stay owned by their triggers).
    Timers.clear();
    // 06eb80d8-06eb80f8: a player without a ship gets its maneuvers unlocked (the ship id is
    // re-read and passed, TEST EAX,0x3fffffff / JNZ skip).
    if ((PlayerState->GetShipId() & 0x3fffffff) == 0)
        pub::AI::lock_maneuvers(PlayerState->GetShipId(), false);
    // 06eb80fb-06eb811a: every active trigger's condition gets slot 4 (+0x10).
    std::list<FTriggerMap::iterator>::iterator ActiveIt = ActiveTriggers.begin();
    for (; ActiveIt != ActiveTriggers.end(); ++ActiveIt)
        (*ActiveIt)->second.Condition->Slot10();
    // 06eb811c: FUN_06eb8b70.
    ReleaseRtcScripts();
    // 06eb8123-06eb8138: +0x2fc erase(begin, end).
    FriendlyFireOffLabels.clear();
    // 06eb813d-06eb8152: +0x98 (the random encounters) erase(begin, end).
    RandEncs.clear();
    // 06eb8157-06eb817a: erase(begin, end) of the global dialog map DAT_06fcf260 (TU28).
    GMissionDialogs.erase(GMissionDialogs.begin(), GMissionDialogs.end());
    // 06eb817f-06eb81a7: the SpacePop clamps back to their defaults.
    SpacePopSetAttackClamp(0);
    GMissionScanClamp = 0;
    SpacePopSetEncounterEnabled(1);
    SpacePopSetPopulationEnabled(1);
    SpacePopClearExclusionSpheres();
    SpacePopSetHostileClamp(1);
    // 06eb81a7-06eb81d8: a live player regains docking and trade lanes, the music stops.
    if (FindContentPlayerState(PlayerId) != nullptr)
    {
        PlayerState->SetDockAccess(1, nullptr);
        PlayerState->SetTradelaneAccess(1, nullptr);
        pub::Audio::CancelMusic(PlayerId);
    }
    // 06eb81db-06eb8245: +0x14c, +0x158 and +0x140 emptied.
    Invulnerables.clear();
    DecloakedNames.clear();
    MarkObjNames.clear();
    // 06eb824c: FUN_06f0fc40, the TU38 slot 22 body.
    FMissionObject::EndMission();
}

// FUN_06eb8ef0 (thiscall on A, RET 4).
bool IsSameCostume(Costume const* A, Costume const* B)
{
    if (A->PartIds[0] != B->PartIds[0] || A->PartIds[1] != B->PartIds[1] ||
        A->PartIds[2] != B->PartIds[2] || A->PartIds[3] != B->PartIds[3])
        return false;
    if (A->AccessoryCount != B->AccessoryCount)
        return false;
    // 06eb8f21-06eb8f3d: signed count (JLE), accessories compared up to it.
    int const Count = static_cast<int>(A->AccessoryCount);
    for (int Index = 0; Index < Count; ++Index)
    {
        if (A->AccessoryIds[Index] != B->AccessoryIds[Index])
            return false;
    }
    return true;
}

// The unique push_back into mission +0x31c inlined by FUN_06eb84c0 (06eb8836-06eb88f4) and
// the Act_* comm senders: FUN_06eb8ef0 over the vector, FUN_06ed1d30 at the end when absent.
void AddDialogCostume(FMissionScript* Script, Costume const* SpeakerCostume)
{
    std::vector<Costume>::iterator It = Script->DialogCostumes.begin();
    for (; It != Script->DialogCostumes.end(); ++It)
    {
        if (IsSameCostume(&*It, SpeakerCostume))
            return;
    }
    Script->DialogCostumes.push_back(*SpeakerCostume);
}

// FUN_06eb84c0 (06eb84c0-06eb8931).
void FMissionScript::LoadMission(INI_Reader* Reader)
{
    // 06eb84eb: FUN_06f172f0(Reader), the TU38 slot 25 body.
    FMissionObject::LoadMission(Reader);
    // 06eb84f0-06eb84f2.
    Reader->reset();
    while (Reader->read_header())
    {
        if (Reader->is_header("Trigger"))
        {
            LoadMissionTriggerSection(this, Reader);
            continue;
        }
        // DAT_06fb43a0 = "NPC".
        if (Reader->is_header("NPC"))
        {
            // 06eb8702-06eb8764: FUN_06f31430 on a stack FMsnNpc, a cleared ID_String,
            // FUN_06f31470(&id, &npc, Reader), then {id, npc (0x13 dwords)} into the +0x184 map
            // (FUN_06ed04c0, unique insert).
            FMsnNpc Npc;
            ID_String Nickname;
            Nickname.clear();
            LoadMsnNpc(&Nickname, &Npc, Reader);
            MissionNpcs.insert(std::map<unsigned int, FMsnNpc>::value_type(Nickname.Id, Npc));
            continue;
        }
        if (Reader->is_header("Dialog"))
        {
            // 06eb877f-06eb8903: every "line" value whose second string is "Player" (stricmp):
            // its first string (TString<0x30>, cut to 0x2f) -> FUN_06f155f0 (the costume of that
            // mission NPC or solar) -> unique push_back into +0x31c (FUN_06eb8ef0 compare,
            // FUN_06ed1d30 insert).
            while (Reader->read_value())
            {
                if (!Reader->is_value("line"))
                    continue;
                if (_stricmp(Reader->get_value_string(1), "Player") != 0)
                    continue;
                TString<0x30> Speaker;
                Speaker.Length = 0;
                Speaker.Data[0] = '\0';
                Speaker = Reader->get_value_string(0);
                Costume* const SpeakerCostume = GetMissionCostume(&Speaker);
                if (SpeakerCostume == nullptr)
                    continue;
                AddDialogCostume(this, SpeakerCostume);
            }
        }
    }
}

// FUN_06eb8f50 (06eb8f50-06eb9628). The switch tables: 06eb9630 (byte index 06eb96ac,
// messages 1..0x3c) and 06eb96e8 (byte index 06eb9714, messages 0x1000..0x1011). Every case
// ends in either LAB_06eb90e6 (the TU38 body FUN_06f0f150, then the tail) or caseD_7 (the tail
// only); the tail (06eb90ef-06eb9144) runs the NN path and the nag objects unless the mission
// is ending, then always vtbl[+0x78].
void FMissionScript::HandleMissionMessage(EMessage MessageId, void const* MessageParms)
{
    // 06eb8f5a-06eb8f68: nothing at all without a live player state.
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;

    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    // The case's message id (local_c1c) handed to FUN_06eb9af0.
    EMessage EventId = MessageId;
    bool bBaseBody = true;

    switch (MessageId)
    {
    case EMessage::BaseEnter:
        // 06eb90ad: parms {base, player}.
        if (Parms[1] == PlayerId)
            OnBaseEnter(MessageParms);
        break;
    case EMessage::BaseExit:
        // 06eb90bf.
        if (Parms[1] == PlayerId)
            OnBaseExit(MessageParms);
        break;
    case EMessage::CargoScanned:
    case EMessage::CommComplete:
    case EMessage::ObjectDamaged:
    case EMessage::TradelaneEnter:
    case EMessage::TradelaneExit:
    case EMessage::RtcDone:
    case EMessage::JumpInComplete:
    case EMessage::LaunchComplete:
    case EMessage::PlayerManeuver:
    case EMessage::JumpgateAct:
    case EMessage::NpcSystemExit:
    case EMessage::EncounterLaunched:
    case EMessage::RumorHeard:
    case EMessage::TetherObject:
        // 06eb92b2 / 06eb94af / 06eb90d1 / 06eb9167 / 06eb917a / 06eb9286 / 06eb9154 /
        // 06eb9187 / 06eb9353 / 06eb9546 / 06eb956c / 06eb95eb / 06eb94d9 / 06eb9559.
        DispatchMissionTriggerEvent(this, &EventId, MessageParms);
        break;
    case EMessage::PlayerSave:
    {
        // 06eb9384-06eb93d7: the save {player, FILE*}; skips the TU38 body when the gate fails.
        bBaseBody = false;
        if (Parms[0] != PlayerId || !pub::SinglePlayer())
            break;
        FILE* File = reinterpret_cast<FILE*>(static_cast<uintptr_t>(Parms[1]));
        fprintf(File, "\n[MissionState]\n");
        SaveMissionState(File);
        FMissionObject::HandleMissionMessage(MessageId, MessageParms);
        fprintf(File, "\n[BStoryMissionDone]\n");
        bTriggerSavePending = 0;
        break;
    }
    case EMessage::PlayerLoad:
    {
        // 06eb93dc-06eb9443: the load {player, INI_Reader*}.
        PlayerState = FindContentPlayerState(PlayerId);
        if (PlayerState == nullptr)
        {
            bPendingDestroy = 1;
            SetPlayer(0);
        }
        else if (Parms[0] == PlayerId)
        {
            INI_Reader* Reader = reinterpret_cast<INI_Reader*>(static_cast<uintptr_t>(Parms[1]));
            INI_Reader::State SavedState;
            Reader->get_state(SavedState);
            Reader->reset();
            LoadMissionState(Reader);
            Reader->set_state(SavedState);
        }
        // 06eb9432-06eb9443: the TU38 body only while a player state is live.
        if (FindContentPlayerState(PlayerId) == nullptr)
            bBaseBody = false;
        break;
    }
    case EMessage::PlayerLaunch:
        // 06eb92bf.
        if (Parms[0] == PlayerId)
            DispatchMissionTriggerEvent(this, &EventId, MessageParms);
        break;
    case EMessage::PlayerLogin:
    case EMessage::SystemLoaded:
    case EMessage::LoadGame:
        bBaseBody = false;
        break;
    case EMessage::PlayerLogout:
    {
        // 06eb8fef-06eb90ab: drop every trigger.
        ActiveTriggers.clear();
        FTriggerMap::iterator TriggerIt = TriggerMap.begin();
        for (; TriggerIt != TriggerMap.end(); ++TriggerIt)
            DeleteTriggerContents(&TriggerIt->second);
        TriggerMap.clear();
        PlayerState = FindContentPlayerState(PlayerId);
        if (PlayerState == nullptr)
        {
            bPendingDestroy = 1;
            SetPlayer(0);
            bBaseBody = false;
        }
        break;
    }
    case EMessage::LootAcquired:
    {
        // 06eb9251-06eb9281: the trigger event, then word parms+8 into the loot record's
        // +0xf8 (06eb927a).
        DispatchMissionTriggerEvent(this, &EventId, MessageParms);
        FMissionLoot* Found = FindLootById(Parms);
        if (Found != nullptr)
            Found->SubObjId = *reinterpret_cast<unsigned short const*>(
                static_cast<unsigned char const*>(MessageParms) + 8);
        break;
    }
    case EMessage::GFObjSelect:
        // 06eb933a.
        if (PlayerId == Parms[1])
            OnCharacterSelect(MessageParms);
        break;
    case EMessage::Heartbeat:
    {
        // 06eb8fa7-06eb8fea: +0x30c += dt, the delayed actions, event 0x20 with a local copy
        // of dt, event 0x1011 with no parms, then the TU38 body with the real parms.
        float DeltaSeconds = *static_cast<float const*>(MessageParms);
        ElapsedTime = DeltaSeconds + ElapsedTime;
        TickMissionTimers(this, DeltaSeconds);
        DispatchMissionTriggerEvent(this, &EventId, &DeltaSeconds);
        EventId = EMessage::TetherObject;
        DispatchMissionTriggerEvent(this, &EventId, nullptr);
        break;
    }
    case EMessage::AbortMission:
        // 06eb9499-06eb94aa: the TU38 body first, then FUN_06eb9cd0; tail only.
        FMissionObject::HandleMissionMessage(MessageId, MessageParms);
        OnMessage24(MessageParms);
        bBaseBody = false;
        break;
    case EMessage::LocationEnter:
        // 06eb92dd-06eb931c: the location enter on the player's base is remembered.
        if (PlayerId == Parms[1] && FindContentPlayerState(PlayerId) != nullptr)
        {
            if (PlayerState->GetBaseRecord() != nullptr)
                CurrentLocationId = Parms[0];
            DispatchMissionTriggerEvent(this, &EventId, MessageParms);
        }
        break;
    case EMessage::LocationExit:
        // 06eb9321.
        if (PlayerId == Parms[1])
            OnLocationExit(MessageParms);
        break;
    case EMessage::MissionResponse:
        // 06eb919a-06eb91b9.
        if (Parms[3] == PlayerId && Parms[0] == ControllerId)
            OnOfferResponse(MessageParms);
        break;
    case EMessage::PopUpDialog:
        // 06eb91be-06eb923f: the answer to the reputation pop-up.
        if (Parms[0] == PlayerId)
        {
            if (bRepPopUpShown != 0)
            {
                if ((Parms[1] & 1) != 0)
                {
                    int PlayerRep = -1;
                    pub::Player::GetRep(PlayerId, PlayerRep);
                    std::vector<unsigned int>::iterator GroupIt = RepPopUpGroups.begin();
                    for (; GroupIt != RepPopUpGroups.end(); ++GroupIt)
                        pub::Reputation::SetReputation(PlayerRep, *GroupIt, 0.6f);
                    bRepPopUpArmed = 0;
                }
                bRepPopUpShown = 0;
            }
            DispatchMissionTriggerEvent(this, &EventId, MessageParms);
        }
        break;
    case EMessage::SpaceEnter:
        // 06eb9448-06eb9465: the TU38 body first; tail only.
        FMissionObject::HandleMissionMessage(MessageId, MessageParms);
        if (Parms[1] == PlayerId)
            OnSpaceEnter(MessageParms);
        bBaseBody = false;
        break;
    case EMessage::SpaceExit:
        // 06eb946a-06eb9494: the TU38 body first; tail only.
        FMissionObject::HandleMissionMessage(MessageId, MessageParms);
        if (Parms[1] == PlayerId)
            DispatchMissionTriggerEvent(this, &EventId, MessageParms);
        bBaseBody = false;
        break;
    case EMessage::ObjectDestroyed:
        // 06eb9244.
        OnObjectDestroyed(MessageParms);
        break;
    case EMessage::SystemExit:
        // 06eb9293.
        if (Parms[2] == PlayerId)
            DispatchMissionTriggerEvent(this, &EventId, MessageParms);
        break;
    case EMessage::SystemEnter:
        // 06eb9366-06eb937f: the TU38 body first; tail only.
        FMissionObject::HandleMissionMessage(MessageId, MessageParms);
        OnSystemEnter(MessageParms);
        OnSystemEnterPopUp(MessageParms);
        bBaseBody = false;
        break;
    case EMessage::MissionStart:
        // 06eb94ec-06eb9541; tail only.
        ActivateInitialMissionTriggers(this);
        bBaseBody = false;
        break;
    case EMessage::DamageReputation:
        // 06eb95fe-06eb9628; tail only.
        if (FindContentPlayerState(PlayerId) != nullptr
            && PlayerState->GetLiveMission() == ControllerId)
            OnMessage1005(MessageParms);
        bBaseBody = false;
        break;
    case EMessage::Msg0x1006:
        // 06eb95be-06eb95e6.
        if (Parms[1] == 0)
        {
            if (Parms[2] == 1)
                Field_0x2e8 = 0x5462;
            else
                Field_0x2e8 = Parms[2];
        }
        break;
    case EMessage::PlayerMissionChanged:
        // 06eb9579.
        OnMessage100b(MessageParms);
        break;
    case EMessage::NpcShipCreated:
    {
        // 06eb9586-06eb95b9: an NPC of this mission was created.
        FContentNpcShip* Npc = *static_cast<FContentNpcShip* const*>(MessageParms);
        if (Npc->ControllerId == ControllerId)
        {
            RegisterNpc(MessageParms);
            SubmitNpcPersonality(Npc, 0);
            DispatchMissionTriggerEvent(this, &EventId, MessageParms);
        }
        break;
    }
    default:
        // caseD_9 (0x9, 0xa, 0xc, 0xe..0x15, 0x19..0x1f, 0x21, 0x23, 0x25, 0x27, 0x2b, 0x2d,
        // 0x2e, 0x31..0x35, 0x38, 0x1001..0x1004, 0x1008, 0x1009, 0x100e, 0x100f, and
        // everything outside the two tables).
        break;
    }

    if (bBaseBody)
        FMissionObject::HandleMissionMessage(MessageId, MessageParms);

    // 06eb90ef-06eb913b: the tail.
    if (bPendingDestroy == 0)
    {
        if (Field_0x2ec == 0 && NnPath.bActive != 0
            && (PlayerState->GetLiveMission() == ControllerId
                || (PlayerState->GetLiveMission() & 0x3fffffff) == 0))
            NnPath.HandleMessage(MessageId, MessageParms);
        // 06eb9133-06eb913b: FUN_06ecef10(+0x204, msg, parms).
        DispatchNagMessage(this, MessageId, MessageParms);
    }
    // 06eb9140-06eb9144: vtbl[+0x78].
    ApplyDesiredMissionState();
}

// FUN_06eb9dc0.
void FMissionScript::OnBaseEnter(void const* MessageParms)
{
    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    // 06eb9ddd-06eb9e0c: every RTC script not yet started is started.
    std::list<FStoryMissionListNode_0x2f4>::iterator ScriptIt = List_0x2f4.begin();
    for (; ScriptIt != List_0x2f4.end(); ++ScriptIt)
    {
        if (ScriptIt->Field_0x8c == -1)
            StartRtcScript(&*ScriptIt);                                 // 06eb9dff
    }
    // 06eb9e0e-06eb9e79: every ambient scene of this base is created.
    std::list<FMissionAmbientScene>::iterator SceneIt = AmbientScenes.begin();
    for (; SceneIt != AmbientScenes.end(); ++SceneIt)
    {
        if (SceneIt->BaseId != Parms[0])
            continue;
        AmbientScriptDescription Desc;
        Desc.RoomId = SceneIt->RoomId;
        Desc.ScriptId = 1;
        Desc.Flags = 0;
        Desc.Name = SceneIt->Path.Data;
        Desc.Filter = PlayerId;
        SceneIt->Handle = static_cast<int>(pub::GF::AmbientScriptCreate(Desc));
    }
    // 06eb9e7c-06eb9eb1.
    if (Field_0x2ec != 0)
    {
        RenumberAllObjectives();
        Field_0x2ec = 0;
    }
    else if (NnPath.bActive != 0)
    {
        NnPath.Rebuild();
        NnPath.SendObjectives(false, false);
    }
    // 06eb9eb7-06eb9ecb.
    EMessage EventId = EMessage::BaseEnter;
    DispatchMissionTriggerEvent(this, &EventId, MessageParms);
}

// FUN_06eb9ef0 (06eb9ef0-06eba005).
void FMissionScript::OnBaseExit(void const* MessageParms)
{
    // 06eb9ef9-06eb9f08: the base record (FUN_06eac0c0 on *parms); the scripts are only walked
    // with one.
    FContentBase* Base = FindContentBaseInfo(static_cast<unsigned int const*>(MessageParms));
    if (Base != nullptr)
    {
        std::list<FStoryMissionListNode_0x2f4>::iterator It = List_0x2f4.begin();
        while (It != List_0x2f4.end())
        {
            if (It->Field_0x90 != nullptr)
            {
                // 06eb9f2a-06eb9f42: FUN_06ea9fc0(Base, Field_0x8c, 0), -1 not tested.
                Base->RemoveBarScene(It->Field_0x8c, nullptr);
                It->Field_0x8c = -1;
                // 06eb9f3a-06eb9f5e: FUN_06eb8c40 + free of the script.
                delete It->Field_0x90;
                It->Field_0x90 = nullptr;
            }
            // 06eb9f68-06eb9f99: a finished script's node is unlinked.
            if (It->Field_0x94 != 0)
                It = List_0x2f4.erase(It);
            else
                ++It;
        }
    }
    // 06eb9fa9-06eb9fe5: every ambient scene's handle is destroyed.
    std::list<FMissionAmbientScene>::iterator SceneIt = AmbientScenes.begin();
    for (; SceneIt != AmbientScenes.end(); ++SceneIt)
    {
        if (SceneIt->Handle != -1)
        {
            unsigned long Handle = static_cast<unsigned long>(SceneIt->Handle);
            pub::GF::AmbientScriptDestroy(&Handle);
            SceneIt->Handle = -1;
        }
    }
    EMessage EventId = EMessage::BaseExit;
    DispatchMissionTriggerEvent(this, &EventId, MessageParms);
}

// FUN_06eba350 (06eba350-06eba41f).
void FMissionScript::OnCharacterSelect(void const* MessageParms)
{
    std::list<FStoryMissionListNode_0x2f4>::iterator It = List_0x2f4.begin();
    for (; It != List_0x2f4.end(); ++It)
    {
        if (It->Field_0x94 != 0 || It->Field_0x95 != 0)
            continue;
        if (FindContentPlayerState(PlayerId) == nullptr)
            continue;
        if (It->Field_0x90 == nullptr)
            continue;
        // 06eba398-06eba3ed: the player's base, its room state of +0x308,
        // FindCharacter(script +0x10 role, PlayerId); a character whose id (+0x0c) equals
        // *parms gets SetBehavior(-1) and the node +0x94 = 1.
        FContentBase* const Base = PlayerState->GetBaseRecord();
        if (Base == nullptr)
            continue;
        FBaseRoomState* const Room = Base->FindRoomState(&CurrentLocationId);
        if (Room == nullptr)
            continue;
        FBarCharacterList::iterator Character;
        if (!Room->FindCharacter(GetFirstRtcRole(It->Field_0x90), PlayerId, &Character))
            continue;
        if (Character->CharacterId != *static_cast<unsigned int const*>(MessageParms))
            continue;
        Character->SetBehavior(static_cast<unsigned long>(-1));
        It->Field_0x94 = 1;
    }
    EMessage EventId = EMessage::GFObjSelect;
    DispatchMissionTriggerEvent(this, &EventId, MessageParms);
}

// FUN_06eba530 (06eba530-06eba719).
void FMissionScript::OnLocationExit(void const* MessageParms)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    FContentBase* Base = PlayerState->GetBaseRecord();
    if (Base != nullptr)
    {
        // 06eba575-06eba5ed: the roles (script +0x10 vector<0x3c>) of the unfinished scripts
        // of this location (script +0 == *parms), appended one by one (FUN_06ed1040).
        unsigned int const LocationId = *static_cast<unsigned int const*>(MessageParms);
        std::vector<FRtcScriptRole> KeepRoles;
        std::list<FStoryMissionListNode_0x2f4>::iterator It = List_0x2f4.begin();
        for (; It != List_0x2f4.end(); ++It)
        {
            if (It->Field_0x94 != 0 || It->Field_0x90 == nullptr)
                continue;
            if (It->Field_0x90->LocationId != LocationId)
                continue;
            std::vector<FRtcScriptRole>::const_iterator Role = It->Field_0x90->Roles.begin();
            for (; Role != It->Field_0x90->Roles.end(); ++Role)
                KeepRoles.push_back(*Role);
        }
        // 06eba5ef-06eba6b3: every finished script (+0x94 == 1) with an object is released
        // through FUN_06ea9fc0(Base, +0x8c, &KeepRoles), freed and its node unlinked.
        It = List_0x2f4.begin();
        while (It != List_0x2f4.end())
        {
            if (It->Field_0x94 != 1 || It->Field_0x90 == nullptr)
            {
                ++It;
                continue;
            }
            Base->RemoveBarScene(It->Field_0x8c, &KeepRoles);
            It->Field_0x8c = -1;
            delete It->Field_0x90;
            It->Field_0x90 = nullptr;
            It = List_0x2f4.erase(It);
        }
        // 06eba6b9-06eba6ec: KeepRoles destroyed.
    }
    EMessage EventId = EMessage::LocationExit;
    DispatchMissionTriggerEvent(this, &EventId, MessageParms);
}

// FUN_06eba030 (06eba030-06eba1e2).
void FMissionScript::OnOfferResponse(void const* MessageParms)
{
    EMessage EventId = EMessage::MissionResponse;
    unsigned char const* ParmsBytes = static_cast<unsigned char const*>(MessageParms);
    if (ParmsBytes[8] == 0 || FindContentPlayerState(PlayerId) == nullptr)
    {
        // 06eba1bb-06eba1cb.
        DispatchMissionTriggerEvent(this, &EventId, MessageParms);
        return;
    }
    // 06eba073-06eba0d6: an empty FmtStr shown with type 10, broadcast.
    {
        FmtStr EmptyMsg;
        pub::Player::DisplayMissionMessage(PlayerId, EmptyMsg,
                                           static_cast<MissionMessageType>(0xa), true);
    }
    DispatchMissionTriggerEvent(this, &EventId, MessageParms);
    // 06eba0f1-06eba10a.
    if (StoryEvents != nullptr)
        StoryEvents->OnMissionEvent(1, static_cast<unsigned int>(CampaignSlotSucceed), ControllerId);
    // 06eba10c-06eba135: another live mission of the player is destroyed and left.
    unsigned int const LiveMission = PlayerState->GetLiveMission();
    if ((LiveMission & 0x3fffffff) != 0 && LiveMission != ControllerId)
    {
        pub::Controller::Destroy(LiveMission);
        PlayerState->EndLiveMission(LiveMission, 4, true);
    }
    // 06eba13a-06eba17a.
    bRepPopUpShown = 0;
    bRepPopUpArmed = 0;
    RepPopUpSystemId = 0;
    RepPopUpGroups.clear();
    // 06eba17e-06eba191.
    SpacePopSetAttackClamp(1);
    SpacePopSetEncounterEnabled(0);
    GMissionScanClamp = 1;
    // 06eba18d-06eba1b4: a cleared ID_String, FUN_06f488b0(PlayerState; +4, EBX = 0, &id, 1,
    // +0x2c4) -- the group joins this mission. Arguments in retail's positional order.
    ID_String NoVnpc;
    NoVnpc.clear();
    PlayerState->JoinMissionAsGroup(ControllerId, 0, &NoVnpc, true, Field_0x2c4);
}

// FUN_06eba720 (06eba720-06eba859).
void FMissionScript::OnSpaceEnter(void const* MessageParms)
{
    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    if (Parms[1] != PlayerId)
        return;
    // 06eba74b-06eba7ef: a CostumeHint per dialog costume, the costume assigned through
    // Costume's inline operator= (part ids, count, then `count` accessory ids).
    std::vector<Costume>::iterator It = DialogCostumes.begin();
    for (; It != DialogCostumes.end(); ++It)
    {
        CostumeHint Hint;
        Hint.Cost.PartIds[0] = It->PartIds[0];
        Hint.Cost.PartIds[1] = It->PartIds[1];
        Hint.Cost.PartIds[2] = It->PartIds[2];
        Hint.Cost.PartIds[3] = It->PartIds[3];
        Hint.Cost.AccessoryCount = It->AccessoryCount;
        for (int Index = 0; Index < static_cast<int>(It->AccessoryCount); ++Index)
            Hint.Cost.AccessoryIds[Index] = It->AccessoryIds[Index];
        pub::Player::LoadHint(PlayerId, &Hint);
    }
    EMessage EventId = EMessage::SpaceEnter;
    DispatchMissionTriggerEvent(this, &EventId, MessageParms);
    // 06eba810-06eba842.
    if (NnPath.bActive == 0)
    {
        RenumberAllObjectives();
    }
    else
    {
        NnPath.Rebuild();
        HideAllObjectives();
        NnPath.SendObjectives(false, false);
    }
    Field_0x2ec = 0;
}

// FUN_06ebaae0 (06ebaae0-06ebab81).
void FMissionScript::OnObjectDestroyed(void const* MessageParms)
{
    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    EMessage EventId = EMessage::ObjectDestroyed;
    DispatchMissionTriggerEvent(this, &EventId, MessageParms);
    // 06ebaafd-06ebab4a: the first friendly-fire label naming the destroyed object is dropped.
    std::list<TString<0x30>>::iterator It = FriendlyFireOffLabels.begin();
    for (; It != FriendlyFireOffLabels.end(); ++It)
    {
        if (GetLabelObjectId(&*It) == Parms[0])
        {
            FriendlyFireOffLabels.erase(It);
            break;
        }
    }
    // 06ebab50-06ebab79: FUN_06f15df0(PlayerId) false, a live player state and no ship ->
    // +0x19 = 1.
    if (!IsPlayerDead(PlayerId) && FindContentPlayerState(PlayerId) != nullptr
        && PlayerState->ShipObject == nullptr)
        bPendingDestroy = 1;
}

// FUN_06eba860 (06eba860-06eba8e9).
void FMissionScript::OnSystemEnter(void const* MessageParms)
{
    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    if (Parms[2] != PlayerId || FindContentPlayerState(PlayerId) == nullptr)
        return;
    EMessage EventId = EMessage::SystemEnter;
    DispatchMissionTriggerEvent(this, &EventId, MessageParms);
    FContentPlayerShip* const Ship = PlayerState->ShipObject;
    if (Ship == nullptr)
        return;
    // 06eba8a4-06eba8df: event 0x100f with {ship id, parms[0], parms[3] or 0}.
    unsigned int NpcEnterParms[3];
    NpcEnterParms[0] = Ship->SpaceObjId;
    NpcEnterParms[1] = Parms[0];
    NpcEnterParms[2] = (Parms[1] & 0x3fffffff) != 0 ? Parms[3] : 0;
    EventId = EMessage::NpcSystemEnter;
    DispatchMissionTriggerEvent(this, &EventId, NpcEnterParms);
}

// FUN_06eba1f0 (06eba1f0-06eba343).
void FMissionScript::OnSystemEnterPopUp(void const* MessageParms)
{
    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    if (bRepPopUpArmed == 0 || Parms[0] != RepPopUpSystemId || Parms[2] != PlayerId)
        return;
    FmtStr Title;
    Title.begin_mad_lib(0x54c4);
    Title.end_mad_lib();
    FmtStr Text;
    Text.begin_mad_lib(0x54c5);
    Text.end_mad_lib();
    pub::Player::PopUpDialog(PlayerId, Title, Text, 5);
    bRepPopUpShown = 1;
}

// FUN_06eb9cd0 (06eb9cd0-06eb9dba).
void FMissionScript::OnMessage24(void const* MessageParms)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    if (static_cast<unsigned int const*>(MessageParms)[0] != PlayerId)
        return;
    if (PlayerState->GetLiveMission() != ControllerId)
        return;
    FmtStr MissionMsg;
    MissionMsg.begin_mad_lib(Field_0x2e8);
    MissionMsg.end_mad_lib();
    pub::Player::DisplayMissionMessage(PlayerId, MissionMsg, MISSION_MSG_TYPE_0, true);
    bPendingDestroy = 1;
}

// FUN_06eb99a0 (06eb99a0-06eb9a97): parms {kind, ship, victim}.
void FMissionScript::OnMessage1005(void const* MessageParms)
{
    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    FContentPlayerShip* const Ship = PlayerState->ShipObject;
    if (Ship == nullptr)
        return;
    if (Parms[1] != Ship->SpaceObjId || Parms[0] != 1)
        return;
    // 06eb99e3-06eb9a0c: a victim named by a friendly-fire label is excused.
    std::list<TString<0x30>>::iterator It = FriendlyFireOffLabels.begin();
    for (; It != FriendlyFireOffLabels.end(); ++It)
    {
        if (GetLabelObjectId(&*It) == Parms[2])
            return;
    }
    int VictimRep = 0;
    int ShipRep;
    float Attitude = 0.0f;
    pub::SpaceObj::GetRep(Parms[2], VictimRep);
    pub::SpaceObj::GetRep(Parms[1], ShipRep);
    pub::Reputation::GetAttitude(VictimRep, ShipRep, Attitude);
    // 06eb9a47-06eb9a59: FCOMP DAT_06fb4668 (0.2f), TEST AH,0x41 / JNZ: fall-through (fail
    // with 0x5460) only when Attitude > 0.2.
    if (Attitude > 0.2f)
    {
        SetDesiredMissionState(G_MissionStateFail, 0x5460);
        return;
    }
    // 06eb9a72-06eb9a81: FCOMP DAT_06fb4660 (-0.2f), TEST AH,0x1 / JNZ skip: fail with 0x5463
    // when Attitude >= -0.2 (not below, not unordered).
    if (Attitude >= -0.2f)
        SetDesiredMissionState(G_MissionStateFail, 0x5463);
}

// FUN_06eba8f0 (06eba8f0-06ebaacf).
void FMissionScript::OnMessage100b(void const* MessageParms)
{
    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    if (FindContentPlayerState(PlayerId) != nullptr && PlayerState->ShipObject != nullptr
        && Parms[0] != ControllerId)
    {
        // 06eba930-06eba957: a local record {"", handle -1, no script, 0, 0}.
        FStoryMissionListNode_0x2f4 Restart;
        // 06eba968-06eba997: the first node with a script whose mission text (+0x2c) is set,
        // not finished, while the player has a base.
        std::list<FStoryMissionListNode_0x2f4>::iterator It = List_0x2f4.begin();
        for (; It != List_0x2f4.end(); ++It)
        {
            if (It->Field_0x90 == nullptr || It->Field_0x90->MissionTextId == 0 ||
                It->Field_0x94 != 0)
            {
                continue;
            }
            FContentBase* const Base = PlayerState->GetBaseRecord();
            if (Base == nullptr)
                continue;
            // 06eba99c-06eba9ca: its path (FUN_06ea9f40, max 0x80) and +0x95.
            size_t PathLength = strlen(It->Path);
            if (PathLength >= 0x80)
                PathLength = 0x7f;
            memcpy(Restart.Path, It->Path, PathLength);
            Restart.Path[PathLength] = '\0';
            Restart.PathLength = static_cast<unsigned int>(PathLength);
            Restart.Field_0x95 = It->Field_0x95;
            // 06eba9d1-06ebaa29: FUN_06ea9fc0(Base, +0x8c, 0), FUN_06eb8c40 + free, unlink.
            Base->RemoveBarScene(It->Field_0x8c, nullptr);
            It->Field_0x8c = -1;
            delete It->Field_0x90;
            It->Field_0x90 = nullptr;
            List_0x2f4.erase(It);
            break;
        }
        // 06ebaa2f-06ebaa76: a copied path restarts the script and appends the record.
        if (Restart.PathLength != 0)
        {
            StartRtcScript(&Restart);
            List_0x2f4.push_back(Restart);
        }
    }
    // 06ebaa79-06ebaac2: with *parms == 0 the player's first objective is resent with its
    // type or-ed with 0x30, the record flagged (slot 1) around the send.
    if (Parms[0] != 0)
        return;
    FMissionPlayer* Player = FindPlayer(PlayerId);
    if (Player == nullptr)
        return;
    FMissionObjective* First = Player->Objectives.GetFront();
    unsigned int const SavedType = First->Objective.Type;
    First->Objective.Type = SavedType | 0x30;
    Player->SetField25(true);
    Player->SendObjectives();
    Player->SetField25(false);
    First->Objective.Type = SavedType;
}

// FUN_06ebabb0 (06ebabb0-06ebae00).
void FMissionScript::SaveMissionState(FILE* File)
{
    // 06ebabbc-06ebabdc: NEG/SBB/INC of (live mission - this id) -> 1 when equal.
    fprintf(File, "mission_accepted = %d\n", PlayerState->GetLiveMission() == ControllerId ? 1 : 0);
    // 06ebabde: DAT_06fd1c78 (GSpacePopAttackClamp) CMP AL,1.
    fprintf(File, GSpacePopAttackClamp == 1 ? "att_clamp = true\n" : "att_clamp = false\n");
    fprintf(File, SpacePopGetEncounterEnabled() == 1 ? "tradelane_attacks = true\n"
                                                      : "tradelane_attacks = false\n");
    fprintf(File, GMissionScanClamp == 1 ? "scan_clamp = true\n" : "scan_clamp = false\n");
    fprintf(File, ContentIsGcsClampEnabled() ? "gcs_clamp = true\n" : "gcs_clamp = false\n");
    fprintf(File, SpacePopGetHostileClamp() == 1 ? "hostile_clamp = true\n"
                                                  : "hostile_clamp = false \n");
    fprintf(File, SpacePopGetPopulationEnabled() != 0 ? "random_pop = true\n"
                                                       : "random_pop = false\n");
    // 06ebac80-06ebac99: the madlib ids of the TU38 FmtStrs +0x68 and +0x50.
    fprintf(File, "msn_offer = %d\n", Field_0x68.MadLibId);
    fprintf(File, "msn_title = %d\n", Field_0x50.MadLibId);
    if (NnPath.bActive != 0)
        NnPath.Save(File);
    if (bTriggerSavePending != 0)
    {
        // 06ebaddf-06ebadf6.
        fprintf(File, "\n[TriggerSave]\n");
        fprintf(File, "trigger = %u\n", SaveTriggerId);
        return;
    }
    // 06ebacbf-06ebad21: every unfinished script.
    std::list<FStoryMissionListNode_0x2f4>::iterator ScriptIt = List_0x2f4.begin();
    for (; ScriptIt != List_0x2f4.end(); ++ScriptIt)
    {
        if (ScriptIt->Field_0x94 != 0)
            continue;
        fprintf(File, "rtc = %s, %s\n", ScriptIt->Path, ScriptIt->Field_0x95 != 0 ? "true" : "false");
    }
    std::list<FMissionAmbientScene>::iterator SceneIt = AmbientScenes.begin();
    for (; SceneIt != AmbientScenes.end(); ++SceneIt)
        fprintf(File, "ambi_scene = %s, %u, %u\n", SceneIt->Path.Data, SceneIt->RoomId,
                SceneIt->BaseId);
    std::list<TString<0x30>>::iterator LabelIt = FriendlyFireOffLabels.begin();
    for (; LabelIt != FriendlyFireOffLabels.end(); ++LabelIt)
        fprintf(File, "friendly_fire_excused = %s\n", LabelIt->Data);
    fprintf(File, "story_cue = %d\n", StoryCue);
    // 06ebad8f-06ebadd3: a [TriggerSave] per active trigger, its condition's slot 5 (+0x14).
    std::list<FTriggerMap::iterator>::iterator ActiveIt = ActiveTriggers.begin();
    for (; ActiveIt != ActiveTriggers.end(); ++ActiveIt)
    {
        fprintf(File, "\n[TriggerSave]\n");
        fprintf(File, "trigger = %u\n", (*ActiveIt)->first);
        (*ActiveIt)->second.Condition->Save(File);
    }
}

// FUN_06ebae10 (06ebae10-06ebb52f). Four local flags: [MissionState] seen (the [NNObjective]
// gate), the RTC list released, the active triggers dropped, the objectives cleared. The
// header loop ends at [BStoryMissionDone]; any other header is skipped.
void FMissionScript::LoadMissionState(INI_Reader* Reader)
{
    bool bMissionStateSeen = false;
    bool bRtcReleased = false;
    bool bTriggersDropped = false;
    bool bObjectivesCleared = false;
    while (Reader->read_header())
    {
        if (Reader->is_header("MissionState"))
        {
            // 06ebae79-06ebae80.
            Field_0x2ec = 1;
            bMissionStateSeen = true;
            while (Reader->read_value())
            {
                if (Reader->is_value("rtc"))
                {
                    // 06ebaeaa-06ebaf5c: the first "rtc" releases the started scripts; each one
                    // is appended (FUN_06ed0ed0) with handle -1, no script, not finished,
                    // +0x95 = b1.
                    if (!bRtcReleased)
                        ReleaseRtcScripts();
                    FStoryMissionListNode_0x2f4 Script;
                    TString<0x80> Path;
                    Path = Reader->get_value_string(0);
                    Script.PathLength = Path.Length;
                    memcpy(Script.Path, Path.Data, Path.Length + 1);
                    Script.Field_0x95 = Reader->get_value_bool(1) ? 1 : 0;
                    List_0x2f4.push_back(Script);
                    bRtcReleased = true;
                    continue;
                }
                if (Reader->is_value("ambi_scene"))
                {
                    // 06ebaf74-06ebb052: path s0, room i1, base i2, handle -1, appended to
                    // +0x310. The AmbientScriptDescription the listing fills on the stack
                    // (06ebaff7-06ebb03d) is never passed anywhere.
                    FMissionAmbientScene Scene;
                    Scene.Path.Length = 0;
                    Scene.Path.Data[0] = '\0';
                    Scene.Handle = -1;
                    Scene.RoomId = 0;
                    Scene.BaseId = 0;
                    Scene.Path = Reader->get_value_string(0);
                    Scene.RoomId = static_cast<unsigned int>(Reader->get_value_int(1));
                    Scene.BaseId = static_cast<unsigned int>(Reader->get_value_int(2));
                    AmbientScenes.push_back(Scene);
                    continue;
                }
                if (Reader->is_value("friendly_fire_excused"))
                {
                    // 06ebb064-06ebb0d1: s0 as TString<0x30>, appended to +0x2fc.
                    TString<0x30> Label;
                    Label = Reader->get_value_string(0);
                    FriendlyFireOffLabels.push_back(Label);
                    continue;
                }
                if (Reader->is_value("mission_accepted"))
                {
                    // 06ebb0e3-06ebb147.
                    if (Reader->get_value_bool(0) != true)
                        continue;
                    if (FindContentPlayerState(PlayerId) == nullptr)
                        continue;
                    ID_String ObjectiveIds;
                    ObjectiveIds.clear();
                    // 06ebb111-06ebb123: FUN_06f488b0(PlayerState; +4, 0, &id, 0, 0), in
                    // retail's positional order.
                    PlayerState->JoinMissionAsGroup(ControllerId, 0, &ObjectiveIds, false, 0);
                    if (StoryEvents != nullptr)
                        StoryEvents->OnMissionEvent(1, static_cast<unsigned int>(CampaignSlotSucceed),
                                                    ControllerId);
                    continue;
                }
                if (Reader->is_value("att_clamp"))
                {
                    // 06ebb159-06ebb161: DAT_06fd1c78 in place.
                    GSpacePopAttackClamp = Reader->get_value_bool(0) ? 1 : 0;
                    continue;
                }
                if (Reader->is_value("tradelane_attacks"))
                {
                    SpacePopSetEncounterEnabled(Reader->get_value_bool(0) ? 1 : 0); // FUN_06f5a820
                    continue;
                }
                if (Reader->is_value("scan_clamp"))
                {
                    GMissionScanClamp = Reader->get_value_bool(0) ? 1 : 0; // 06ebb1a3
                    continue;
                }
                if (Reader->is_value("gcs_clamp"))
                {
                    SpacePopSetGcsClampEnabled(Reader->get_value_bool(0) ? 1 : 0); // FUN_06f5cf60
                    continue;
                }
                if (Reader->is_value("random_pop"))
                {
                    SpacePopSetPopulationEnabled(Reader->get_value_bool(0) ? 1 : 0); // FUN_06f5a800
                    continue;
                }
                if (Reader->is_value("hostile_clamp"))
                {
                    SpacePopSetHostileClamp(Reader->get_value_bool(0) ? 1 : 0); // FUN_06f5a7b0
                    continue;
                }
                // 06ebb223-06ebb27d: a non-zero id rebuilds +0x68 / +0x50 (FmtStr::destroy, the
                // 6 dwords zeroed, begin_mad_lib(i0 read again), end_mad_lib).
                FmtStr* MadLib = nullptr;
                if (Reader->is_value("msn_offer"))
                {
                    if (Reader->get_value_int(0) != 0)
                        MadLib = &Field_0x68;
                }
                else if (Reader->is_value("msn_title"))
                {
                    if (Reader->get_value_int(0) != 0)
                        MadLib = &Field_0x50;
                }
                else if (Reader->is_value("nnpath"))
                {
                    // 06ebb28a-06ebb299: FUN_06f1b480, then FUN_06f1c430(+0x21c, Reader, this).
                    HideAllObjectives();
                    NnPath.Load(Reader, this);
                    continue;
                }
                if (MadLib != nullptr)
                {
                    MadLib->destroy();
                    MadLib->Head = nullptr;
                    MadLib->MadLibId = 0;
                    for (int Index = 0; Index < FmtStr::KindCount; ++Index)
                        MadLib->ValueIndex[Index] = 0;
                    MadLib->begin_mad_lib(static_cast<unsigned int>(Reader->get_value_int(0)));
                    MadLib->end_mad_lib();
                }
            }
            continue;
        }
        if (Reader->is_header("TriggerSave"))
        {
            while (Reader->read_value())
            {
                if (!Reader->is_value("trigger"))
                    continue;
                // 06ebb2e7-06ebb348: the first "trigger" drops the active list (FUN_06ecfe80 per
                // node, the triggers' states untouched), +0x1e4 and +0x1d0.
                if (!bTriggersDropped)
                {
                    ActiveTriggers.clear();
                    InitiallyActive.clear();
                    PendingActivations.clear();
                }
                // 06ebb34d-06ebb45a: {i0, i0} unique into +0x1e4 (activated by message 0x1000).
                // The key is the second read, the value the first.
                unsigned int const FirstRead = static_cast<unsigned int>(Reader->get_value_int(0));
                unsigned int const SecondRead = static_cast<unsigned int>(Reader->get_value_int(0));
                InitiallyActive.insert(
                    std::map<unsigned int, unsigned int>::value_type(SecondRead, FirstRead));
                // 06ebb45f-06ebb4a3: every trigger with id i0 (read a third time): its condition's
                // slot 6 (+0x18, Load) on the reader.
                unsigned int const TriggerId = static_cast<unsigned int>(Reader->get_value_int(0));
                FTriggerMap::iterator It = TriggerMap.begin();
                for (; It != TriggerMap.end(); ++It)
                {
                    if (It->first == TriggerId)
                        It->second.Condition->Load(Reader);
                }
                bTriggersDropped = true;
            }
            continue;
        }
        if (Reader->is_header("NNObjective") && bMissionStateSeen)
        {
            // 06ebb4cf-06ebb4f5: the first one clears every objective (FUN_06f1b3d0); each is
            // loaded into slot 6's (+0x18) objective list (+8) by FUN_06f1a630.
            if (!bObjectivesCleared)
            {
                ClearObjectives();
                bObjectivesCleared = true;
            }
            LoadNNObjective(Reader, this, &GetFirstPlayer()->Objectives);
            continue;
        }
        if (Reader->is_header("BStoryMissionDone"))
            return;
    }
}

// FUN_06ebb620 (06ebb620-06ebb7ed). stdcall (npc, mode), no this; case 0x1010 passes
// (*parms, 0).
void FMissionScript::SubmitNpcPersonality(FContentNpcShip* Npc, int Mode)
{
    // 06ebb62f-06ebb634: CMP EBP,0 / JZ return.
    if (Npc == nullptr)
        return;
    unsigned int const Flid = Npc->SpaceObjId;
    pub::AI::Personality LocalPersonality;
    if (!pub::AI::get_personality(Flid, LocalPersonality))
        return;
    if (Mode == 0)
    {
        // 06ebb741-06ebb774.
        LocalPersonality.JobParams.Field_00 = 0;
        LocalPersonality.JobParams.SceneToughnessThreshold = 4;
        LocalPersonality.JobParams.LootPreference = 0;
        LocalPersonality.JobParams.FleeSceneThreatStyle = 4;
        LocalPersonality.JobParams.FleeWhenHullDamagedPercent = 0;
        LocalPersonality.JobParams.bFleeWhenLeaderFleesStyle = 0;
        LocalPersonality.JobParams.bFleeNoWeaponsStyle = 0;
    }
    else if (Mode == 1)
    {
        // 06ebb676-06ebb73f: a second Personality from the ship's own pilot (npc +0x1cc) or,
        // when that is empty, from the pilot of its npcships entry (FUN_06f324d0 on
        // ship arch +0x138, value +0xc); on success its job fields replace the live ones.
        pub::AI::Personality PilotPersonality;
        ID_String PilotId = Npc->Field_0x1cc;
        bool bLoaded = false;
        if (PilotId.IsEmpty())
        {
            FNpcShipArch* const Arch = FindNpcShipArchByShip(Npc->ShipArchId, &GNpcShipArchs);
            if (Arch != nullptr)
                bLoaded = GetPilotPersonality(&Arch->Pilot.Id, &PilotPersonality);
        }
        else
        {
            bLoaded = GetPilotPersonality(&PilotId.Id, &PilotPersonality);
        }
        if (bLoaded)
        {
            // 06ebb6dd-06ebb738.
            LocalPersonality.JobParams.FleeSceneThreatStyle = PilotPersonality.JobParams.FleeSceneThreatStyle;
            LocalPersonality.JobParams.FleeWhenHullDamagedPercent = PilotPersonality.JobParams.FleeWhenHullDamagedPercent;
            LocalPersonality.JobParams.bFleeWhenLeaderFleesStyle = PilotPersonality.JobParams.bFleeWhenLeaderFleesStyle;
            LocalPersonality.JobParams.bFleeNoWeaponsStyle = PilotPersonality.JobParams.bFleeNoWeaponsStyle;
            LocalPersonality.JobParams.SceneToughnessThreshold = PilotPersonality.JobParams.SceneToughnessThreshold;
            LocalPersonality.JobParams.Field_00 = PilotPersonality.JobParams.Field_00;
            LocalPersonality.JobParams.LootPreference = PilotPersonality.JobParams.LootPreference;
        }
    }
    // 06ebb77b-06ebb7e0: SetPersonalityParams + SubmitState.
    pub::AI::SetPersonalityParams Params;
    Params.bSetPersonality = 1;
    Params.Value = LocalPersonality;
    Params.StateGraphId = pub::AI::get_state_graph_id(Flid);
    // npc +0x174 (ContentCallback) and +0x118 (DirectiveCallback).
    Params.ContentCallbackObject = static_cast<pub::AI::ContentCallback*>(Npc);
    Params.DirectiveCallbackObject = static_cast<pub::AI::DirectiveCallback*>(Npc);
    pub::AI::SubmitState(Flid, &Params);
}
