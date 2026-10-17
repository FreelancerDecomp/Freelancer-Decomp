// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TU38 (MissionObject.h): the mission object's snapshot and save. Split from MissionObject.cpp
// (which holds the rest of TU38) to keep that file workable; the class is one retail class.
//
//   FUN_06f130b0  SnapshotMissionObjects   (messages 4 and 0x37)
//   FUN_06f11ff0  SnapshotVibeInfos        (the tail of FUN_06f130b0)
//   FUN_06f12c10  ApplyVibeInfos           (message 0x36)
//   FUN_06f0ff90  SaveMission              (message 4)
//   FUN_06f11f70  SaveRandEncs             (message 4)
//   FUN_06f106e0  LoadMissionSave          (message 5)
//   FUN_06f11b90  LoadRandEncsSave         ([RandomEncounter] of the save)
//   FUN_06f12e10  LoadVibeInfos            ([MsnVibeInfo] of the save)
//   FUN_06f15790  FindMissionShipGroup
//   FUN_06f22f50  IsShipBeforeInFormation  (FUN_06f22b00 = std::sort with it)
//   FUN_06f68360  GetSpaceObjAttitude      (TU53, only TU38 callers)

#include "MissionObject.h"

#include "ContentPlayerState.h"
#include "Encounters.h"         // FFormation (NPC ship +0x150)
#include "GuidesInternal.h"     // FLineGuide: the save prints the guide's line (guide +0x18)
#include "MissionObjList.h"     // LoadObjList (FUN_06f25260)
#include "NpcShip.h"
#include "ObjListDirective.h"
#include "RandomEncounters.h"
#include "SystemInfo.h"         // FSystemInfo::SystemId (+0x48)
#include "common_ini.h"
#include "common_server.h"
#include "server_cobject.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string.h>

namespace
{
    // The 9 dwords of a rotation copied as one block (REP MOVSD 9).
    void CopyRotation(FMatrix* Dest, Matrix const* Source)
    {
        std::memcpy(Dest, Source, sizeof(FMatrix));
    }

    // The inlined label walk (e.g. 06f131d0-06f1321f): the NPC's +0x17c list holds LabelId.
    bool HasLabelId(FContentNpcShip const* Npc, unsigned int LabelId)
    {
        std::list<unsigned int>::const_iterator It = Npc->Field_0x17c.begin();
        for (; It != Npc->Field_0x17c.end(); ++It)
        {
            if (*It == LabelId)
                return true;
        }
        return false;
    }

    // The inlined hit point read (e.g. 06f13478-06f134c8): both floats start at 0, GetHealth on a
    // copy of the SpaceObj id, the current value through ftol.
    int ReadHitPoints(FContentNpcShip const* Npc)
    {
        float Current = 0.0f;
        float Max = 0.0f;
        unsigned int ObjId = Npc->SpaceObjId;
        pub::SpaceObj::GetHealth(ObjId, Current, Max);
        return static_cast<int>(Current);
    }

    // FUN_06f11ff0's filter (e.g. 06f12063-06f120ca): the NPC's +0x1bc is -1.0f (DAT_06fb8584;
    // TEST AH,0x44 + JNP taken = equal), or it carries "Mission_Random_Encounter".
    bool IsVibeParty(FContentNpcShip const* Npc)
    {
        if (Npc->Field_0x1bc == -1.0f)
            return true;
        return HasLabelId(Npc, GIdMissionRandomEncounter);
    }

    // 06f13aec-06f13b1a / 06f13edd-06f13f0d: the ship takes the NPC's guide (old one Released
    // and cleared unless it is the same, the new one AddRef'ed), then the guide's slot 8
    // (SetShip, +0x20) with null.
    void TakeGuide(FMissionShip* Ship, FContentGuide* Guide)
    {
        if (Ship->Guide.Pointer != Guide)
        {
            if (Ship->Guide.Pointer != nullptr)
            {
                Ship->Guide.Pointer->Release();
                Ship->Guide = nullptr;
            }
            Ship->Guide = Guide;
            if (Guide != nullptr)
                Guide->AddRef();
        }
        Ship->Guide.Pointer->SetShip(nullptr);
    }

    // 06f134eb-06f13578 (and 06f137f1, 06f13b1f, 06f13f12): Target takes a copy of the NPC's
    // directive list (FUN_06f14400, each AddRef'ed): the old elements Released and erased
    // (FUN_06ed2a00), the copy assigned (FUN_06f93490), each AddRef'ed; then the copy Released,
    // erased and freed (its unwind step is FUN_06ecfa90).
    void TakeDirectives(FMissionDirectiveList* Target, FContentNpcShip const* Npc)
    {
        FMissionDirectiveList Copied;
        Copied.Items = Npc->CopyDirectives();
        *Target = Copied;
    }

    // 06f13426-06f13442 / 06f13454-06f13463 (FUN_06eb70f0 / FUN_06eb7130).
    void ReadPlacement(FContentNpcShip const* Npc, FTransform* Out)
    {
        Vector Position;
        Npc->GetPosition(&Position);
        Out->Translation.X = Position.x;
        Out->Translation.Y = Position.y;
        Out->Translation.Z = Position.z;
        Matrix Orientation;
        Npc->GetOrientation(&Orientation);
        CopyRotation(&Out->Rotation, &Orientation);
    }

    // 06f139ec-06f13a31 / 06f13e46-06f13e8b: the record takes the NPC's position and orientation.
    void ReadGroupPlacement(FContentNpcShip const* Npc, FMissionShipGroup* Group)
    {
        Npc->GetPosition(&Group->Position);
        Matrix Orientation;
        Npc->GetOrientation(&Orientation);
        CopyRotation(&Group->Orientation, &Orientation);
    }

    // The nine rotation floats as doubles, in memory order (the FLD / FSTP double runs of
    // 06f101c3-06f10226 and 06f10377-06f103e0).
    void PrintOrientation(FILE* File, FMatrix const* Rotation)
    {
        std::fprintf(File, "orientation = %f,%f,%f, %f,%f,%f, %f,%f,%f\n",
                     static_cast<double>(Rotation->D[0][0]), static_cast<double>(Rotation->D[0][1]),
                     static_cast<double>(Rotation->D[0][2]), static_cast<double>(Rotation->D[1][0]),
                     static_cast<double>(Rotation->D[1][1]), static_cast<double>(Rotation->D[1][2]),
                     static_cast<double>(Rotation->D[2][0]), static_cast<double>(Rotation->D[2][1]),
                     static_cast<double>(Rotation->D[2][2]));
    }

    // 06f10047-06f100ac / 06f102e8-06f1033e: a ship with a guide writes the guide's
    // [MetaBehavior] line - retail passes the chars at guide +0x18 (FLineGuide::Line, only the
    // goto / dock guides a [MetaBehavior] gives have one) to fprintf as the format; a ship
    // without one writes its directives under [MObjective] (slot 6, +0x18, of each).
    void SaveGuideOrDirectives(FILE* File, FContentGuide* Guide, FMissionDirectiveList const* Directives)
    {
        if (Guide != nullptr)
        {
            std::fprintf(File, "\n[MetaBehavior]\n");
            std::fprintf(File, static_cast<FLineGuide*>(Guide)->Line.Data);
            return;
        }
        if (Directives->Items.empty())
            return;
        std::fprintf(File, "\n[MObjective]\n");
        std::list<FObjListDirective*>::const_iterator It = Directives->Items.begin();
        for (; It != Directives->Items.end(); ++It)
            (*It)->Save(File);
    }
}

// FUN_06f68360 (06f68360-06f683a0, cdecl). Retail leaves both reputations unset before GetRep
// (which writes its out-parameter on every path); the port starts them at 0.
bool __cdecl GetSpaceObjAttitude(unsigned int FromId, unsigned int ToId, float* OutAttitude)
{
    int FromRep = 0;
    int ToRep = 0;
    pub::SpaceObj::GetRep(FromId, FromRep);
    pub::SpaceObj::GetRep(ToId, ToRep);
    return pub::Reputation::GetAttitude(FromRep, ToRep, *OutAttitude) == 0;
}

// 06f135ac-06f13618.
FMissionRandEncRespawn::FMissionRandEncRespawn()
    : FormationRef(0xffffffffu)
    , Count(0)
    , Guide(nullptr)
    , Priority(0)
    , HitPoints(-1)
{
    EncounterId.clear();
    // FUN_06f143d0: identity rotation, zero translation.
    std::memset(&Placement, 0, sizeof(Placement));
    Placement.Rotation.D[0][0] = 1.0f;
    Placement.Rotation.D[1][1] = 1.0f;
    Placement.Rotation.D[2][2] = 1.0f;
}

// FUN_06f15790 (06f15790-06f1583d, RET 4): per +0xb8 record in list order, first its nickname
// (stricmp, node +0xc), then each ship's (stricmp, element +8).
FMissionShipGroup* FMissionObject::FindMissionShipGroup(TString<0x30> const* Nickname)
{
    std::list<FMissionShipGroup>::iterator It = MissionShips.begin();
    for (; It != MissionShips.end(); ++It)
    {
        if (_stricmp(It->Nickname.Data, Nickname->Data) == 0)
            return &*It;
        std::vector<FMissionShip>::iterator ShipIt = It->Ships.begin();
        for (; ShipIt != It->Ships.end(); ++ShipIt)
        {
            if (_stricmp(ShipIt->Nickname.Data, Nickname->Data) == 0)
                return &*It;
        }
    }
    return nullptr;
}

// FUN_06f22f50 (06f22f50-06f22f9a, RET 8).
bool FMissionObject::IsShipBeforeInFormation(FMissionShip const* A, FMissionShip const* B)
{
    FContentNpcShip* NpcA = FindNpcByName(&A->Nickname);
    FContentNpcShip* NpcB = FindNpcByName(&B->Nickname);
    if (NpcA == nullptr || NpcB == nullptr)
        return false;
    return static_cast<int>(NpcA->FormationSlot) < static_cast<int>(NpcB->FormationSlot);
}

// FUN_06f130b0 (06f130b0-06f143ce). EH (FuncInfo 06fc26d8, 21 states): the local multimap
// (FUN_06f14510) and list (FUN_06f15350), the ship class copies (FUN_06eec8d0), the directive
// copies (FUN_06ecfa90), the new +0x8c record (FUN_06f11a40 / its members), the new +0x128
// record (FUN_06ec6960) and the local ship vector (FUN_06f1d760) - all RAII locals below.
void FMissionObject::SnapshotMissionObjects()
{
    // 06f130d0-06f13147: the three lists emptied.
    RespawnShips.clear();
    RandEncRespawns.clear();
    RespawnSolars.clear();

    // 06f1314c-06f13191: formation ref -> NPC (FUN_06f1f4f0 multi) and the refs in first-seen
    // order.
    std::multimap<unsigned int, FContentNpcShip*> FormationNpcs;
    std::list<unsigned int> FormationRefs;
    // 06f13185-06f131a9: the player's system record, null without a player state.
    FSystemInfo* PlayerSystem = nullptr;
    if (FindContentPlayerState(PlayerId) != nullptr)
        PlayerSystem = PlayerState->GetSystemRecord();

    // 06f131ad-06f13bc9: every live mission NPC.
    std::map<unsigned int, FContentNpcShip*>::iterator NpcIt = NpcObjects.begin();
    for (; NpcIt != NpcObjects.end(); ++NpcIt)
    {
        FContentNpcShip* Npc = NpcIt->second;
        if (!HasLabelId(Npc, GIdMissionRandomEncounter))
        {
            // 06f13221-06f13239: only NPCs whose +0x1bc is -1.0f (TEST AH,0x44 + JP taken when
            // not equal).
            if (!(Npc->Field_0x1bc == -1.0f))
                continue;
            unsigned int const FormationRef = Npc->FormationId;
            if (FormationRef != 0xffffffffu)
            {
                // 06f13254-06f132b6: remembered for the formation pass.
                FormationNpcs.insert(std::make_pair(FormationRef, Npc));
                if (std::find(FormationRefs.begin(), FormationRefs.end(), FormationRef) ==
                    FormationRefs.end())
                    FormationRefs.push_back(FormationRef);
                continue;
            }

            // 06f1390c-06f13bab: a single mission ship - a copy of its [MsnShip] record.
            FMissionShipGroup* Group = FindMissionShipGroup(&Npc->Nickname);
            if (Group == nullptr)
                continue;
            RespawnShips.push_back(*Group);
            FMissionShipGroup& Copy = RespawnShips.back();
            FSystemInfo* NpcSystem = Npc->GetSystem();
            if (Npc->Formation != nullptr)
                Copy.Formation = Npc->Formation->Nickname;
            // 06f13970-06f139d7: retail does not test the ship for null.
            FMissionShip* Ship = FindMissionShip(&Npc->Nickname);
            Ship->Priority = Npc->GetPriority();
            Ship->HitPoints = ReadHitPoints(Npc);
            if (NpcSystem != nullptr)
            {
                if (PlayerSystem != nullptr && NpcSystem->SystemId == PlayerSystem->SystemId)
                    ReadGroupPlacement(Npc, &Copy);
            }
            else if (Ship->bJumper != 0)
            {
                // 06f13a35-06f13a3f: a jumper out of every system arrives through the gate.
                Copy.ArrivalObjId = JumpGateId;
            }
            // 06f13a45-06f13ac4: the copy keeps only this ship.
            Copy.Ships.clear();
            FMissionShip* Again = FindMissionShip(&Npc->Nickname);
            if (Again != nullptr)
            {
                Again->HitPoints = ReadHitPoints(Npc);
                Copy.Ships.push_back(*Again);
            }
            if (Copy.Ships.empty())
                continue;
            FMissionShip& First = Copy.Ships.front();
            if (Npc->Guide != nullptr)
            {
                TakeGuide(&First, Npc->Guide);
            }
            else
            {
                First.InitObjectives.Id = GIdLocalObjective;
                TakeDirectives(&First.Directives, Npc);
            }
            continue;
        }

        // 06f132bb-06f13331: a random-encounter ship - the first [MsnRandEnc] whose id it
        // carries.
        std::list<FMissionRandEnc>::iterator EncIt = RandEncs.begin();
        for (; EncIt != RandEncs.end(); ++EncIt)
        {
            if (HasLabelId(Npc, EncIt->NicknameId.Id))
                break;
        }
        if (EncIt == RandEncs.end())
            continue;

        // 06f13336-06f135a6: every record of the ship's formation group counts it.
        bool bFound = false;
        std::list<FMissionRandEncRespawn>::iterator RecordIt = RandEncRespawns.begin();
        for (; RecordIt != RandEncRespawns.end(); ++RecordIt)
        {
            if (RecordIt->FormationRef != Npc->FormationId)
                continue;
            ++RecordIt->Count;
            bFound = true;
            // 06f13370-06f133de: a copy of the ship's class ids [ESP+0x194] (EH state 2, alive to
            // the end of the iteration, 06f13584), then a temporary choice [ESP+0x16c] = {copy of
            // it, weight 1.0f} (state 3) pushed and destroyed at once.
            std::vector<unsigned int> Classes(Npc->ClassIds);
            {
                FRandEncShipChoice const Choice{Classes, 1.0f};
                RecordIt->Choices.push_back(Choice);
            }
            RecordIt->Names.push_back(Npc->Nickname);
            if (Npc->FormationSlot != 0 && Npc->FormationId != 0xffffffffu)
                continue;
            // 06f13422-06f13578: the leader gives the place, priority, hit points and guide
            // (raw) or directives.
            ReadPlacement(Npc, &RecordIt->Placement);
            RecordIt->Priority = Npc->GetPriority();
            RecordIt->HitPoints = ReadHitPoints(Npc);
            if (Npc->Guide != nullptr)
            {
                RecordIt->Guide = Npc->Guide;
                Npc->Guide->SetShip(nullptr);
            }
            else
            {
                TakeDirectives(&RecordIt->Directives, Npc);
            }
        }
        if (bFound)
            continue;

        // 06f135ac-06f13902: a new record for the group.
        FMissionRandEncRespawn Record;
        Record.EncounterId = EncIt->NicknameId;
        Record.FormationRef = Npc->FormationId;
        Record.Priority = Npc->GetPriority();
        Record.Count = 1;
        // 06f13670-06f136d9: a copy of the ship's class ids [ESP+0x1a4] (EH state 9, destroyed
        // after the record is pushed, 06f138be-06f138d2), then a temporary choice [ESP+0x160]
        // = {copy of it, weight 1.0f} (state 0xa) pushed and destroyed at once.
        std::vector<unsigned int> Classes(Npc->ClassIds);
        {
            FRandEncShipChoice const Choice{Classes, 1.0f};
            Record.Choices.push_back(Choice);
        }
        Record.Names.push_back(Npc->Nickname);
        if (Npc->FormationSlot == 0 || Npc->FormationId == 0xffffffffu)
        {
            ReadPlacement(Npc, &Record.Placement);
            Record.HitPoints = ReadHitPoints(Npc);
            if (Npc->Guide != nullptr)
            {
                Record.Guide = Npc->Guide;
                Npc->Guide->SetShip(nullptr);
            }
            else
            {
                TakeDirectives(&Record.Directives, Npc);
            }
        }
        RandEncRespawns.push_back(Record);
    }

    // 06f13bcf-06f14231: one +0x128 record per formation group, in first-seen order.
    std::list<unsigned int>::iterator RefIt = FormationRefs.begin();
    for (; RefIt != FormationRefs.end(); ++RefIt)
    {
        RespawnShips.push_back(FMissionShipGroup());
        FMissionShipGroup& Dest = RespawnShips.back();
        std::vector<FMissionShip> Ships;
        FMissionShipGroup* Group = nullptr;
        bool bAbandon = false;
        std::pair<std::multimap<unsigned int, FContentNpcShip*>::iterator,
                  std::multimap<unsigned int, FContentNpcShip*>::iterator> Range =
            FormationNpcs.equal_range(*RefIt);
        std::multimap<unsigned int, FContentNpcShip*>::iterator It = Range.first;
        for (; It != Range.second; ++It)
        {
            FContentNpcShip* Npc = It->second;
            // 06f13c98-06f13ca1: a random-encounter ship ends the walk (to 06f14071).
            if (HasLabelId(Npc, GIdMissionRandomEncounter))
                break;
            FMissionShip* Ship = FindMissionShip(&Npc->Nickname);
            if (Ship == nullptr)
                continue;
            Ship->Priority = Npc->GetPriority();
            Ship->HitPoints = ReadHitPoints(Npc);
            if (Npc->FormationSlot != 0 && Npc->FormationId != 0xffffffffu)
            {
                // 06f1403a-06f14046: a wingman is appended.
                Ships.push_back(*Ship);
                continue;
            }
            // 06f13d5a-06f13e08: the leader's [MsnShip] record is copied over the new one
            // (FUN_06f15790); none -> the record stays empty (to 06f141c3).
            Group = FindMissionShipGroup(&Npc->Nickname);
            if (Group == nullptr)
            {
                bAbandon = true;
                break;
            }
            Dest = *Group;
            if (Npc->Formation != nullptr)
                Dest.Formation = Npc->Formation->Nickname;
            FSystemInfo* NpcSystem = Npc->GetSystem();
            if (NpcSystem != nullptr)
            {
                if (PlayerSystem != nullptr && NpcSystem->SystemId == PlayerSystem->SystemId)
                    ReadGroupPlacement(Npc, &Dest);
            }
            else if (Ship->bJumper != 0)
            {
                Dest.ArrivalObjId = JumpGateId;
            }
            // 06f13ea7-06f14038: appended; the vector's first ship takes the guide or the
            // directives.
            Ships.push_back(*Ship);
            FMissionShip& First = Ships.front();
            if (Npc->Guide != nullptr)
            {
                TakeGuide(&First, Npc->Guide);
            }
            else
            {
                First.InitObjectives.Id = GIdLocalObjective;
                TakeDirectives(&First.Directives, Npc);
            }
        }
        // 06f14071-06f141c0: the record takes the collected ships when a leader record was
        // found (the vector assignment inlined).
        if (!bAbandon && Group != nullptr)
            Dest.Ships = Ships;
        // 06f141c3-06f141d9: FUN_06f22b00 = std::sort by formation slot (FUN_06f22f50). The
        // slots of one group's live ships are distinct, so the order does not depend on VC6's
        // introsort (threshold 16, FUN_06f236c0 insertion pass).
        std::sort(Dest.Ships.begin(), Dest.Ships.end(),
                  [this](FMissionShip const& A, FMissionShip const& B)
                  {
                      return IsShipBeforeInFormation(&A, &B);
                  });
    }

    // 06f14237-06f142bf: the spawned [MsnSolar]s of the player's system, by name.
    std::list<FMissionSolar>::iterator SolarIt = MissionSolars.begin();
    for (; SolarIt != MissionSolars.end(); ++SolarIt)
    {
        if (PlayerSystem == nullptr)
            continue;
        if (PlayerSystem->SystemId != SolarIt->SystemId)
            continue;
        if (SolarIt->bSpawned != 1)
            continue;
        RespawnSolars.push_back(SolarIt->Nickname);
    }

    // 06f142c1-06f142c3.
    SnapshotVibeInfos();
}

// FUN_06f11ff0 (06f11ff0-06f12c0f). The rows are built on the stack and appended (FUN_06f1ede0,
// FUN_06f202f0 with 1, and the inlined push_back of 06f12a3d-06f12bf9). The label rows leave
// RepA / RepB unset in retail (only read for an empty name); the port zeroes them.
void FMissionObject::SnapshotVibeInfos()
{
    VibeInfos.clear();
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    FSystemInfo* PlayerSystem = PlayerState->GetSystemRecord();
    if (PlayerSystem == nullptr)
        return;

    // 06f12043-06f124ee: every live mission NPC.
    std::map<unsigned int, FContentNpcShip*>::iterator NpcIt = NpcObjects.begin();
    for (; NpcIt != NpcObjects.end(); ++NpcIt)
    {
        FContentNpcShip* Npc = NpcIt->second;
        if (!IsVibeParty(Npc))
            continue;

        // 06f120cf-06f12182: towards the player.
        if (FindContentPlayerState(PlayerId) != nullptr)
        {
            int NpcRep = 0;
            int PlayerRep = 0;
            float Attitude = 0.0f;
            pub::SpaceObj::GetRep(NpcIt->first, NpcRep);
            pub::Player::GetRep(PlayerId, PlayerRep);
            pub::Reputation::GetAttitude(NpcRep, PlayerRep, Attitude);
            FMissionVibeInfo Row = {};
            Row.ObjectA = Npc->Nickname;
            Row.ObjectB = GPlayerLabel;
            Row.Attitude = Attitude;
            VibeInfos.push_back(Row);
        }

        // 06f12187-06f122c6: towards every NPC of the same filter (itself included).
        std::map<unsigned int, FContentNpcShip*>::iterator OtherIt = NpcObjects.begin();
        for (; OtherIt != NpcObjects.end(); ++OtherIt)
        {
            FContentNpcShip* Other = OtherIt->second;
            if (!IsVibeParty(Other))
                continue;
            float Attitude = 0.0f;
            if (!GetSpaceObjAttitude(NpcIt->first, OtherIt->first, &Attitude))
                continue;
            FMissionVibeInfo Row = {};
            Row.ObjectA = Npc->Nickname;
            Row.ObjectB = Other->Nickname;
            Row.Attitude = Attitude;
            VibeInfos.push_back(Row);
        }

        // 06f122cc-06f12390: towards every spawned solar of the player's system.
        std::list<FMissionSolar>::iterator SolarIt = MissionSolars.begin();
        for (; SolarIt != MissionSolars.end(); ++SolarIt)
        {
            if (PlayerSystem->SystemId != SolarIt->SystemId)
                continue;
            if (SolarIt->bSpawned != 1)
                continue;
            float Attitude = 0.0f;
            if (!GetSpaceObjAttitude(NpcIt->first, SolarIt->SpaceObjId, &Attitude))
                continue;
            FMissionVibeInfo Row = {};
            Row.ObjectA = Npc->Nickname;
            Row.ObjectB = SolarIt->Nickname;
            Row.Attitude = Attitude;
            VibeInfos.push_back(Row);
        }

        // 06f12396-06f124d1: both ways with every +0x174 reputation.
        int NpcRep = -1;
        pub::SpaceObj::GetRep(NpcIt->first, NpcRep);
        std::vector<int>::iterator RepIt = SolarReps.begin();
        for (; RepIt != SolarReps.end(); ++RepIt)
        {
            // 06f123d0-06f1243b: {"", npc, rep, -1} - the reputation towards the NPC.
            FMissionVibeInfo Towards = {};
            Towards.RepA = *RepIt;
            Towards.RepB = -1;
            Towards.ObjectB = Npc->Nickname.Data;
            pub::Reputation::GetAttitude(*RepIt, NpcRep, Towards.Attitude);
            VibeInfos.push_back(Towards);

            // 06f12440-06f124bd: {npc, "", -1, rep} - the NPC towards the reputation.
            FMissionVibeInfo From = {};
            From.RepA = -1;
            From.RepB = *RepIt;
            From.ObjectA = Npc->Nickname.Data;
            pub::Reputation::GetAttitude(NpcRep, *RepIt, From.Attitude);
            VibeInfos.push_back(From);
        }
    }

    // 06f124f4-06f12978: every spawned solar of the player's system.
    std::list<FMissionSolar>::iterator SolarIt = MissionSolars.begin();
    for (; SolarIt != MissionSolars.end(); ++SolarIt)
    {
        if (PlayerSystem->SystemId != SolarIt->SystemId)
            continue;
        if (SolarIt->bSpawned != 1)
            continue;

        // 06f12533-06f125ca: towards the player.
        if (FindContentPlayerState(PlayerId) != nullptr)
        {
            int SolarRep = 0;
            int PlayerRep = 0;
            float Attitude = 0.0f;
            pub::SpaceObj::GetRep(SolarIt->SpaceObjId, SolarRep);
            pub::Player::GetRep(PlayerId, PlayerRep);
            pub::Reputation::GetAttitude(SolarRep, PlayerRep, Attitude);
            FMissionVibeInfo Row = {};
            Row.ObjectA = SolarIt->Nickname;
            Row.ObjectB = GPlayerLabel;
            Row.Attitude = Attitude;
            VibeInfos.push_back(Row);
        }

        // 06f125cf-06f126df: towards every NPC of the filter.
        std::map<unsigned int, FContentNpcShip*>::iterator NpcIt2 = NpcObjects.begin();
        for (; NpcIt2 != NpcObjects.end(); ++NpcIt2)
        {
            FContentNpcShip* Npc = NpcIt2->second;
            if (!IsVibeParty(Npc))
                continue;
            float Attitude = 0.0f;
            if (!GetSpaceObjAttitude(SolarIt->SpaceObjId, NpcIt2->first, &Attitude))
                continue;
            FMissionVibeInfo Row = {};
            Row.ObjectA = SolarIt->Nickname;
            Row.ObjectB = Npc->Nickname;
            Row.Attitude = Attitude;
            VibeInfos.push_back(Row);
        }

        // 06f126e5-06f12792: towards every spawned solar of the player's system (itself
        // included).
        std::list<FMissionSolar>::iterator OtherIt = MissionSolars.begin();
        for (; OtherIt != MissionSolars.end(); ++OtherIt)
        {
            if (PlayerSystem->SystemId != OtherIt->SystemId)
                continue;
            if (OtherIt->bSpawned != 1)
                continue;
            float Attitude = 0.0f;
            if (!GetSpaceObjAttitude(SolarIt->SpaceObjId, OtherIt->SpaceObjId, &Attitude))
                continue;
            FMissionVibeInfo Row = {};
            Row.ObjectA = SolarIt->Nickname;
            Row.ObjectB = OtherIt->Nickname;
            Row.Attitude = Attitude;
            VibeInfos.push_back(Row);
        }

        // 06f12798-06f1295e: both ways with every +0x174 reputation.
        int SolarRep = -1;
        pub::SpaceObj::GetRep(SolarIt->SpaceObjId, SolarRep);
        std::vector<int>::iterator RepIt = SolarReps.begin();
        for (; RepIt != SolarReps.end(); ++RepIt)
        {
            // 06f127d0-06f12881: {"", solar, rep, -1} (the name assignment inlined).
            FMissionVibeInfo Towards = {};
            Towards.RepA = *RepIt;
            Towards.RepB = -1;
            Towards.ObjectB = SolarIt->Nickname.Data;
            pub::Reputation::GetAttitude(*RepIt, SolarRep, Towards.Attitude);
            VibeInfos.push_back(Towards);

            // 06f12886-06f1294a: {solar, "", -1, rep}.
            FMissionVibeInfo From = {};
            From.RepA = -1;
            From.RepB = *RepIt;
            From.ObjectA = SolarIt->Nickname.Data;
            pub::Reputation::GetAttitude(SolarRep, *RepIt, From.Attitude);
            VibeInfos.push_back(From);
        }
    }

    // 06f1297e-06f12bff: every +0x174 reputation towards the player.
    int PlayerRep = -1;
    pub::Player::GetRep(PlayerId, PlayerRep);
    std::vector<int>::iterator RepIt = SolarReps.begin();
    for (; RepIt != SolarReps.end(); ++RepIt)
    {
        FMissionVibeInfo Row = {};
        Row.RepA = *RepIt;
        Row.RepB = -1;
        Row.ObjectB = GPlayerLabel.Data;
        pub::Reputation::GetAttitude(*RepIt, PlayerRep, Row.Attitude);
        VibeInfos.push_back(Row);
    }
}

// FUN_06f12c10 (06f12c10-06f12e01). A named party's reputation is GetRep of its label's object
// (FUN_06f15b90 inlined twice: 06f12c54-06f12ccd with FUN_06ed01c0 for the loot names,
// 06f12cf0-06f12dad with an inline strcmp), an unnamed one is the row's own.
void FMissionObject::ApplyVibeInfos()
{
    std::vector<FMissionVibeInfo>::iterator It = VibeInfos.begin();
    for (; It != VibeInfos.end(); ++It)
    {
        int RepA = -1;
        int RepB = -1;
        if (It->ObjectA.Length != 0)
            pub::SpaceObj::GetRep(GetLabelObjectId(&It->ObjectA), RepA);
        else
            RepA = It->RepA;
        if (It->ObjectB.Length != 0)
            pub::SpaceObj::GetRep(GetLabelObjectId(&It->ObjectB), RepB);
        else
            RepB = It->RepB;
        pub::Reputation::SetAttitude(RepA, RepB, It->Attitude);
    }
}

// FUN_06f0ff90 (06f0ff90-06f106d5, RET 4).
void FMissionObject::SaveMission(FILE* File)
{
    // 06f0ffb1-06f102c6: every +0x128 record.
    std::list<FMissionShipGroup>::iterator GroupIt = RespawnShips.begin();
    for (; GroupIt != RespawnShips.end(); ++GroupIt)
    {
        // 06f0ffd5-06f0fff2: the ship names, written after the record (FUN_06ed0440 list).
        std::list<TString<0x30>> Names;
        std::vector<FMissionShip>::iterator ShipIt = GroupIt->Ships.begin();
        for (; ShipIt != GroupIt->Ships.end(); ++ShipIt)
        {
            TString<0x30> Name;
            Name.Length = 0;
            Name.Data[0] = '\0';
            Name = ShipIt->Nickname.Data;
            Names.push_back(Name);

            SaveGuideOrDirectives(File, ShipIt->Guide.Pointer, &ShipIt->Directives);
            std::fprintf(File, "\n[MsnShipSave]\n");
            std::fprintf(File, "nickname = %s\n", ShipIt->Nickname.Data);
            std::fprintf(File, "npc = %u\n", ShipIt->NpcId.Id);
            std::fprintf(File, "random_name = %d\n", ShipIt->bRandomName != 0 ? 1 : 0);
            std::fprintf(File, "ol_priority = %d\n", ShipIt->Priority);
            std::fprintf(File, "jumper = %d\n", static_cast<int>(ShipIt->bJumper));
            std::fprintf(File, "original_jump_flag = %d\n", static_cast<int>(ShipIt->bOriginalJumpFlag));
            std::fprintf(File, "hit_pts = %d\n", ShipIt->HitPoints);
            std::fprintf(File, "radius = %f\n", static_cast<double>(ShipIt->Radius));
            std::list<unsigned int>::iterator LabelIt = ShipIt->LabelIds.begin();
            for (; LabelIt != ShipIt->LabelIds.end(); ++LabelIt)
                std::fprintf(File, "label = %u\n", *LabelIt);
        }

        // 06f10176-06f1027e.
        std::fprintf(File, "\n[MsnWingSave]\n");
        std::fprintf(File, "nickname = %s\n", GroupIt->Nickname.Data);
        if ((GroupIt->ArrivalObjId & 0x3fffffff) == 0)
        {
            std::fprintf(File, "position = %f, %f, %f\n", static_cast<double>(GroupIt->Position.x),
                         static_cast<double>(GroupIt->Position.y),
                         static_cast<double>(GroupIt->Position.z));
            PrintOrientation(File, &GroupIt->Orientation);
        }
        else
        {
            std::fprintf(File, "arrival_obj = %u\n", GroupIt->ArrivalObjId);
        }
        std::fprintf(File, "formation = %u\n", GroupIt->Formation.Id);
        std::list<TString<0x30>>::iterator NameIt = Names.begin();
        for (; NameIt != Names.end(); ++NameIt)
            std::fprintf(File, "ship = %s\n", NameIt->Data);
    }

    // 06f102ce-06f104cd: every +0x8c record.
    std::list<FMissionRandEncRespawn>::iterator RecordIt = RandEncRespawns.begin();
    for (; RecordIt != RandEncRespawns.end(); ++RecordIt)
    {
        SaveGuideOrDirectives(File, RecordIt->Guide, &RecordIt->Directives);
        std::fprintf(File, "\n[MsnRandEncSave]\n");
        std::fprintf(File, "rand_enc_owner = %u\n", RecordIt->EncounterId.Id);
        std::fprintf(File, "position = %f, %f, %f\n",
                     static_cast<double>(RecordIt->Placement.Translation.X),
                     static_cast<double>(RecordIt->Placement.Translation.Y),
                     static_cast<double>(RecordIt->Placement.Translation.Z));
        PrintOrientation(File, &RecordIt->Placement.Rotation);
        std::fprintf(File, "num_ships = %d\n", RecordIt->Count);
        std::fprintf(File, "ol_priority = %d\n", RecordIt->Priority);
        std::fprintf(File, "hit_pts = %d\n", RecordIt->HitPoints);
        std::list<FRandEncShipChoice>::iterator ChoiceIt = RecordIt->Choices.begin();
        for (; ChoiceIt != RecordIt->Choices.end(); ++ChoiceIt)
        {
            std::fprintf(File, "ship_type = ");
            std::vector<unsigned int>::iterator ClassIt = ChoiceIt->ShipClasses.begin();
            for (; ClassIt != ChoiceIt->ShipClasses.end(); ++ClassIt)
                std::fprintf(File, "%u, ", *ClassIt);
            std::fprintf(File, "END\n");
        }
        std::fprintf(File, "names = ");
        std::list<TString<0x30>>::iterator NameIt = RecordIt->Names.begin();
        for (; NameIt != RecordIt->Names.end(); ++NameIt)
            std::fprintf(File, "%s, ", NameIt->Data);
        std::fprintf(File, "END\n");
    }

    // 06f104d3-06f1051d.
    if (!RespawnSolars.empty())
        std::fprintf(File, "\n[MsnSolarSave]\n");
    std::list<TString<0x30>>::iterator SolarIt = RespawnSolars.begin();
    for (; SolarIt != RespawnSolars.end(); ++SolarIt)
        std::fprintf(File, "solar_name = %s\n", SolarIt->Data);

    // 06f10521-06f10563.
    if (!MarkObjNames.empty())
        std::fprintf(File, "\n[MarkObj]\n");
    std::list<TString<0x30>>::iterator MarkIt = MarkObjNames.begin();
    for (; MarkIt != MarkObjNames.end(); ++MarkIt)
        std::fprintf(File, "name = %s\n", MarkIt->Data);

    // 06f10567-06f105c1.
    if (!Invulnerables.empty())
        std::fprintf(File, "\n[Invulnerables]\n");
    std::list<FMissionInvulnerable>::iterator InvIt = Invulnerables.begin();
    for (; InvIt != Invulnerables.end(); ++InvIt)
        std::fprintf(File, "invulnerable = %s, %f, %d\n", InvIt->Nickname.Data,
                     static_cast<double>(InvIt->Duration), InvIt->bParam3 != 0 ? 1 : 0);

    // 06f105c5-06f1060d.
    if (!DecloakedNames.empty())
        std::fprintf(File, "\n[Decloaked]\n");
    std::list<TString<0x30>>::iterator DecloakIt = DecloakedNames.begin();
    for (; DecloakIt != DecloakedNames.end(); ++DecloakIt)
        std::fprintf(File, "decloaked= %s\n", DecloakIt->Data);

    // 06f1060f-06f106a9: an empty name is written "empty".
    if (!VibeInfos.empty())
    {
        std::fprintf(File, "\n[MsnVibeInfo]\n");
        std::vector<FMissionVibeInfo>::iterator VibeIt = VibeInfos.begin();
        for (; VibeIt != VibeInfos.end(); ++VibeIt)
        {
            char const* NameB = VibeIt->ObjectB.Length != 0 ? VibeIt->ObjectB.Data : "empty";
            char const* NameA = VibeIt->ObjectA.Length != 0 ? VibeIt->ObjectA.Data : "empty";
            std::fprintf(File, "vibe = %s, %s, %u, %u, %f\n", NameA, NameB,
                         static_cast<unsigned int>(VibeIt->RepA),
                         static_cast<unsigned int>(VibeIt->RepB),
                         static_cast<double>(VibeIt->Attitude));
        }
    }

    // 06f106af-06f106b9: the first player's objectives (slot 6, +8; retail does not test the
    // record for null).
    SaveNNObjectives(File, &GetFirstPlayer()->Objectives);
}

// FUN_06f11f70 (06f11f70-06f11fe2, RET 4).
void FMissionObject::SaveRandEncs(FILE* File)
{
    std::list<FMissionRandEnc>::iterator It = RandEncs.begin();
    for (; It != RandEncs.end(); ++It)
    {
        std::fprintf(File, "[RandomEncounter]\n");
        std::fprintf(File, "nickname = %s\n", It->Nickname.Data);
        std::fprintf(File, "enabled = %d, %d\n", It->bEnabled != 0 ? 1 : 0, It->NpcCount);
        It->Params->Dump(File);
        std::fprintf(File, "\n");
    }
}

namespace
{
    // The nine "orientation" floats (values 0..8) in memory order (06f10cd2-06f10d7c /
    // 06f11210-06f112ba: read into a stack matrix, then copied as 9 dwords).
    void ReadOrientation(INI_Reader* Reader, FMatrix* Out)
    {
        FMatrix Read;
        Read.D[0][0] = Reader->get_value_float(0);
        Read.D[0][1] = Reader->get_value_float(1);
        Read.D[0][2] = Reader->get_value_float(2);
        Read.D[1][0] = Reader->get_value_float(3);
        Read.D[1][1] = Reader->get_value_float(4);
        Read.D[1][2] = Reader->get_value_float(5);
        Read.D[2][0] = Reader->get_value_float(6);
        Read.D[2][1] = Reader->get_value_float(7);
        Read.D[2][2] = Reader->get_value_float(8);
        *Out = Read;
    }

    // The value is one of the "END"-terminated lists of the save (a TString<0x40> of it
    // compared as 4 bytes with "END\0", 06f10e39-06f10e53).
    bool IsEndMarker(char const* Value)
    {
        TString<0x40> Text;
        Text = Value;
        return Text == "END";
    }

    // FUN_06f12e10's inlined unique insert into +0x174 (find, else insert at the end; the
    // returned pair is not used).
    void AddUniqueRep(std::vector<int>* Reps, int Rep)
    {
        if (std::find(Reps->begin(), Reps->end(), Rep) == Reps->end())
            Reps->push_back(Rep);
    }
}

// FUN_06f106e0 (06f106e0-06f11a06, RET 4; message 5). Locals in retail order: the pending ship
// vector (+0x1c), the [MObjective] directive list (+0x48, state 1) and the [MetaBehavior] list
// (+0x2c, state 2); the end releases the two lists and frees the ships (06f118dd-06f119e7).
// The [MetaBehavior] list is never emptied inside the loop: every later [MsnShipSave] gets a guide
// from its first object.
void FMissionObject::LoadMissionSave(INI_Reader* Reader)
{
    Reader->reset();
    std::vector<FMissionShip> PendingShips;
    FMissionDirectiveList ObjectiveList;
    FMissionMetaBehaviorList MetaList;
    while (Reader->read_header())
    {
        if (Reader->is_header("RandomEncounter"))
        {
            // 06f10771-06f1078c.
            LoadRandEncsSave(Reader);
        }
        else if (Reader->is_header("MObjective"))
        {
            // 06f10791-06f107bf: FUN_06f25260 appends to the list; the nickname is dropped.
            ID_String Nickname;
            Nickname.clear();
            LoadObjList(Reader, &ObjectiveList.Items, &Nickname.Id);
        }
        else if (Reader->is_header("MetaBehavior"))
        {
            // 06f107c4-06f107f2: FUN_06f065e0 appends to the list; the nickname is dropped.
            ID_String Nickname;
            Nickname.clear();
            LoadMetaBehavior(Reader, &MetaList.Items, &Nickname.Id);
        }
        else if (Reader->is_header("MsnShipSave"))
        {
            // 06f107f7-06f10b70: a ship slot (the FUN_06f18eb0 template) from the values.
            FMissionShip Ship;
            while (Reader->read_value())
            {
                if (Reader->is_value("nickname"))
                    Ship.Nickname = Reader->get_value_string(0);
                else if (Reader->is_value("npc"))
                    Ship.NpcId.Id = static_cast<unsigned int>(Reader->get_value_int(0));
                else if (Reader->is_value("random_name"))
                    Ship.bRandomName = Reader->get_value_bool(0) ? 1 : 0;
                else if (Reader->is_value("jumper"))
                    Ship.bJumper = Reader->get_value_bool(0) ? 1 : 0;
                else if (Reader->is_value("original_jump_flag"))
                    Ship.bOriginalJumpFlag = Reader->get_value_bool(0) ? 1 : 0;
                else if (Reader->is_value("label"))
                    Ship.LabelIds.push_back(static_cast<unsigned int>(Reader->get_value_int(0)));
                else if (Reader->is_value("ol_priority"))
                    Ship.Priority = Reader->get_value_int(0);
                else if (Reader->is_value("hit_pts"))
                    Ship.HitPoints = Reader->get_value_int(0);
                else if (Reader->is_value("radius"))
                    Ship.Radius = Reader->get_value_float(0);
            }
            // 06f10a3a-06f10a9c: the pending [MObjective] directives (AddRef'ed).
            Ship.InitObjectives.Id = GIdLocalObjective;
            Ship.Directives = ObjectiveList;
            // 06f10a9e-06f10ad4: with a [MetaBehavior] loaded, the guide of its first object instead.
            if (!MetaList.Items.empty())
            {
                Ship.Directives.Clear();
                Ship.InitMetaBehavior.Id = GIdLocalObjective;
                CreateGuide(MetaList.Items.front(), &Ship.Guide.Pointer);
            }
            // 06f10ad7-06f10af1: the slot waits for its [MsnWingSave]; the directives are dropped.
            PendingShips.push_back(Ship);
            ObjectiveList.Clear();
        }
        else if (Reader->is_header("MsnRandEncSave"))
        {
            // 06f10b75-06f11132: a +0x8c record (template 06f10b8a-06f10c15).
            FMissionRandEncRespawn Record;
            while (Reader->read_value())
            {
                if (Reader->is_value("rand_enc_owner"))
                {
                    Record.EncounterId.Id = static_cast<unsigned int>(Reader->get_value_int(0));
                }
                else if (Reader->is_value("position"))
                {
                    Record.Placement.Translation.X = Reader->get_value_float(0);
                    Record.Placement.Translation.Y = Reader->get_value_float(1);
                    Record.Placement.Translation.Z = Reader->get_value_float(2);
                }
                else if (Reader->is_value("ol_priority"))
                {
                    Record.Priority = Reader->get_value_int(0);
                }
                else if (Reader->is_value("orientation"))
                {
                    ReadOrientation(Reader, &Record.Placement.Rotation);
                }
                else if (Reader->is_value("hit_pts"))
                {
                    Record.HitPoints = Reader->get_value_int(0);
                }
                else if (Reader->is_value("num_ships"))
                {
                    Record.Count = Reader->get_value_int(0);
                }
                else if (Reader->is_value("ship_type"))
                {
                    // 06f10dd4-06f10f41: up to 0x20 class ids until "END", each once, into a
                    // local vector [ESP+0x5c] (EH state 0xe); then a temporary choice [ESP+0x1d0]
                    // = {copy of the vector (FUN_06eb35b0), weight 1.0f} (state 0xf) is pushed
                    // (FUN_06f1e3e0) and destroyed before the vector.
                    std::vector<unsigned int> Classes;
                    for (unsigned int Index = 0; Index < 0x20; ++Index)
                    {
                        if (IsEndMarker(Reader->get_value_string(Index)))
                            break;
                        unsigned int ClassId = static_cast<unsigned int>(Reader->get_value_int(Index));
                        if (std::find(Classes.begin(), Classes.end(), ClassId) == Classes.end())
                            Classes.push_back(ClassId);
                    }
                    FRandEncShipChoice const Choice{Classes, 1.0f};
                    Record.Choices.push_back(Choice);
                }
                else if (Reader->is_value("names"))
                {
                    // 06f10f4b-06f11036: up to 0x20 names until "END".
                    for (unsigned int Index = 0; Index < 0x20; ++Index)
                    {
                        if (IsEndMarker(Reader->get_value_string(Index)))
                            break;
                        TString<0x30> Name;
                        Name = Reader->get_value_string(Index);
                        Record.Names.push_back(Name);
                    }
                }
            }
            // 06f1104c-06f110ca: the pending directives, then the record into +0x8c.
            Record.Directives = ObjectiveList;
            RandEncRespawns.push_back(Record);
            ObjectiveList.Clear();
        }
        else if (Reader->is_header("MsnWingSave"))
        {
            // 06f11137-06f11511: a +0x128 record (FUN_06f0e2d0) that takes the pending ships.
            FMissionShipGroup Group;
            while (Reader->read_value())
            {
                if (Reader->is_value("nickname"))
                {
                    Group.Nickname = Reader->get_value_string(0);
                }
                else if (Reader->is_value("position"))
                {
                    Group.Position.x = Reader->get_value_float(0);
                    Group.Position.y = Reader->get_value_float(1);
                    Group.Position.z = Reader->get_value_float(2);
                }
                else if (Reader->is_value("orientation"))
                {
                    ReadOrientation(Reader, &Group.Orientation);
                }
                else if (Reader->is_value("arrival_obj"))
                {
                    Group.ArrivalObjId = static_cast<unsigned int>(Reader->get_value_int(0));
                }
                else if (Reader->is_value("formation"))
                {
                    Group.Formation.Id = static_cast<unsigned int>(Reader->get_value_int(0));
                }
            }
            // 06f11308-06f114b9: the vector assignment inlined, the pending vector emptied, then
            // the record appended (FUN_06f1e750).
            Group.Ships = PendingShips;
            PendingShips.clear();
            RespawnShips.push_back(Group);
        }
        else if (Reader->is_header("MsnSolarSave"))
        {
            // 06f11516-06f115e9.
            while (Reader->read_value())
            {
                if (!Reader->is_value("solar_name"))
                    continue;
                TString<0x30> Name;
                Name = Reader->get_value_string(0);
                RespawnSolars.push_back(Name);
            }
        }
        else if (Reader->is_header("MarkObj"))
        {
            // 06f115ee-06f116ba.
            while (Reader->read_value())
            {
                if (!Reader->is_value("name"))
                    continue;
                TString<0x30> Name;
                Name = Reader->get_value_string(0);
                MarkObjNames.push_back(Name);
            }
        }
        else if (Reader->is_header("Invulnerables"))
        {
            // 06f116bf-06f117a2: values read in the order 2, 1, 0 (get_indexed_value for the name).
            while (Reader->read_value())
            {
                if (!Reader->is_value("invulnerable"))
                    continue;
                bool const bParam3 = Reader->get_value_bool(2);
                float const Duration = Reader->get_value_float(1);
                FMissionInvulnerable Row;
                Row.Nickname = Reader->get_indexed_value(0);
                Row.Duration = Duration;
                Row.bParam3 = bParam3 ? 1 : 0;
                Invulnerables.push_back(Row);
            }
        }
        else if (Reader->is_header("Decloaked"))
        {
            // 06f117a7-06f118b1.
            while (Reader->read_value())
            {
                if (!Reader->is_value("decloaked"))
                    continue;
                TString<0x30> Name;
                Name = Reader->get_value_string(0);
                DecloakedNames.push_back(Name);
            }
        }
        else if (Reader->is_header("MsnVibeInfo"))
        {
            // 06f118b3-06f118c7.
            LoadVibeInfos(Reader);
        }
    }
}

// FUN_06f11b90 (06f11b90-06f11f60, RET 4): the values of one [RandomEncounter] section of the
// save. The nickname buffer is strncpy'd (0x20) by "nickname" and is garbage in retail until then;
// the port zeroes it. "enabled" updates every +0x98 record of that name (not for
// "RandomMission"); "encounter_type" creates a new record only for the name "RandomMission".
void FMissionObject::LoadRandEncsSave(INI_Reader* Reader)
{
    char Nickname[0x20] = {};
    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            std::strncpy(Nickname, Reader->get_value_string(0), 0x20);
        }
        else if (Reader->is_value("enabled"))
        {
            // 06f11c02-06f11c94.
            if (_stricmp(Nickname, "RandomMission") == 0)
                continue;
            std::list<FMissionRandEnc>::iterator It = RandEncs.begin();
            for (; It != RandEncs.end(); ++It)
            {
                if (_stricmp(It->Nickname.Data, Nickname) != 0)
                    continue;
                It->bEnabled = Reader->get_value_bool(0) ? 1 : 0;
                if (It->bEnabled != 0)
                    It->NpcCount = 0;
                else if (!Reader->is_value_empty(1))
                    It->NpcCount = static_cast<unsigned int>(Reader->get_value_int(1));
            }
        }
        else if (Reader->is_value("encounter_type"))
        {
            // 06f11c99-06f11f31.
            if (_stricmp(Nickname, "RandomMission") != 0)
                continue;
            char const* Type = Reader->get_value_string(0);
            FMissionRandEnc Record;
            Record.Nickname = Nickname;
            Record.bEnabled = 0;
            // 06f11d37-06f11e26: the type compared as 10 bytes (REPE CMPSB) = strcmp.
            char const* ParamsName = nullptr;
            char const* PopulatorName = nullptr;
            if (std::strcmp(Type, "TL_Attack") == 0)
            {
                ParamsName = "TLParams";
                PopulatorName = "TLPzr";
            }
            else if (std::strcmp(Type, "SP_Attack") == 0)
            {
                ParamsName = "SPParams";
                PopulatorName = "SPPzr";
            }
            if (ParamsName != nullptr)
            {
                if (Record.Params != nullptr)
                {
                    Record.Params->Release();
                    Record.Params = nullptr;
                }
                CreateRandEncParams(ParamsName, &Record.Params.Pointer);   // FUN_06efb830
                if (Record.Populator != nullptr)
                {
                    Record.Populator->Release();
                    Record.Populator = nullptr;
                }
                CreateRandEncPopulator(PopulatorName, &Record.Populator.Pointer);  // FUN_06efb980
                Record.Populator->Parse(Reader, Record.Params);
            }
            // 06f11e29-06f11e44: the mission's controller and player state (no null test, as in
            // retail), then the copy appended (FUN_06f228c0, AddRef of both objects).
            Record.Params->ControllerId = ControllerId;
            Record.Params->Player = PlayerState;
            RandEncs.push_back(Record);
        }
    }
}

// FUN_06f12e10 (06f12e10-06f130a4, RET 4): one row per "vibe" value; a name "empty" (stricmp)
// leaves the party unnamed and adds its reputation (value 2 / 3) to +0x174 once.
void FMissionObject::LoadVibeInfos(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (!Reader->is_value("vibe"))
            continue;
        FMissionVibeInfo Row = {};
        Row.RepA = -1;
        Row.RepB = -1;
        Row.Attitude = 0.0f;
        if (_stricmp(Reader->get_value_string(0), "empty") != 0)
            Row.ObjectA = Reader->get_value_string(0);
        else
            AddUniqueRep(&SolarReps, Reader->get_value_int(2));
        if (_stricmp(Reader->get_value_string(1), "empty") != 0)
            Row.ObjectB = Reader->get_value_string(1);
        else
            AddUniqueRep(&SolarReps, Reader->get_value_int(3));
        Row.RepA = Reader->get_value_int(2);
        Row.RepB = Reader->get_value_int(3);
        Row.Attitude = Reader->get_value_float(4);
        VibeInfos.push_back(Row);
    }
}
