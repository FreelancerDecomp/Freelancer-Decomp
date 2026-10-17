// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU27 (06eef190-06ef2070): the "DestroyMission" controller (FDestroyMission),
// the random mission (its create parms) and the random-mission loader slots 3, 4, 6 and 7
// with their helpers. Content.md section 7, lane CNT-RM.

#include "RandomMission.h"
#include "RandomMissionObject.h"
#include "ContentMessageRouter.h"
#include "common_misc.h"
#include "common_net.h"

#include "BaseDB.h"
#include "ContentStaticIds.h"   // TU27 static IDs 06eef190..06eef370 (DAT_06fcf24c..DAT_06fcf210)
#include "ContentDefaultId.h"   // TU27 06eef390 (CreateID("default") -> DAT_06fcf20c)
#include "ContentInternal.h"
#include "ContentPlayerState.h"
#include "FactionProps.h"
#include "RandomEncounters.h"
#include "RandomMissionDB.h"
#include "RandomMissionGen.h"
#include "RandomMissionMachine.h"   // FRmRuntimeMachine, CreateRmRuntimeMachine
#include "SystemConnections.h"
#include "SystemInfo.h"
#include "common_goods.h"
#include "common_ini.h"
#include "common_server.h"
#include "common_text.h"
#include "common_weighted_vector.h"
#include "x86math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

// DAT_06fcf1f8 (static init LAB_06ef04d0: FUN_06ef0500 builds the empty vector and stores
// its address in DAT_06fcf258; atexit FUN_06ef04f0 -> FUN_06ef0520 frees the buffer and
// clears DAT_06fcf258). Only TU27 reads it, so its place in the static-init order is free.
std::vector<unsigned int> GRmObjectiveZones;

namespace
{
    // DAT_06fcf250 -- this TU's cached 3DMathEngine (FUN_06fa8fa0 on first use).
    I3DMathEngine* GRmMissionMath = nullptr;

    // .rdata DAT_06fb7608: game units per range unit (300).
    float const GRmRangeScale = 300.0f;

    // Zone enumerator of FUN_06ef1810 (vtable 06fb782c, local at [ESP+0x34], 0x14 bytes):
    // slot 0 LAB_06ef1ac0 (RET 8) = push_back(*ZoneId); the reader is not read.
    struct FRmZoneEnumerator : public pub::System::ZoneEnumerator
    {
        std::vector<unsigned int> Zones;            // +0x04 (alloc byte) / +0x08..+0x10

        void Slot00(unsigned int const* ZoneId, INI_Reader* /*Reader*/) override
        {
            Zones.push_back(*ZoneId);
        }
    };

    // The zone filter inlined in FUN_06ef0e30 / FUN_06ef0f20 / FUN_06ef1240: every zone of
    // the InZones result that System knows and whose mission_type is lawful or unlawful
    // (+0x12 / +0x13) is appended once (the inline find + insert-at-end of FUN_06f95e00).
    void CollectMissionZones(FSystemInfo* System, Vector const& Position, float Radius,
                             std::vector<unsigned int>* OutZones)
    {
        unsigned int ZoneIds[0x100];
        // Retail leaves the count uninitialised; pub::System::InZones always writes it.
        unsigned int Count = 0;
        pub::System::InZones(System->SystemId, Position, Radius, ZoneIds, 0x100, Count);
        for (unsigned int Index = 0; Index < Count; ++Index)
        {
            FSystemZone* Zone = System->FindZone(ZoneIds[Index]);
            if (Zone != nullptr && (Zone->bLawful || Zone->bUnlawful))
                RmInsertUnique(OutZones, ZoneIds[Index]);
        }
    }

    // The population filter inlined twice in FUN_06ef1810 (06ef1891-06ef1905,
    // 06ef1980-06ef19f5): every populating group except 0x62e8 that OfferGroup feels at or
    // below -0.2 (FCOMP DAT_06fb75e0 + TEST AH,0x41 + JP: greater and unordered skip) is
    // appended once (FUN_06f95e00). pub::Zone::GetPopulation's result is not tested.
    void CollectHostileGroups(unsigned int ZoneId, int OfferGroup,
                              std::vector<unsigned int>* OutGroups)
    {
        weighted_vector<unsigned int> const* Population = nullptr;
        pub::Zone::GetPopulation(ZoneId, Population);
        unsigned int const OfferGroupId = static_cast<unsigned int>(OfferGroup);
        for (std::vector<std::pair<unsigned int, float>>::const_iterator It =
                 Population->Items.begin();
             It != Population->Items.end(); ++It)
        {
            // TODO: which reputation group 0x62e8 is (an id constant in the listing).
            if (It->first == 0x62e8)
                continue;
            // Retail keeps the previous stack value when GetReputation fails (-4, unknown
            // group) without writing it; the port starts from 0.
            float Feeling = 0.0f;
            pub::Reputation::GetReputation(OfferGroupId, It->first, Feeling);
            if (Feeling < -0.2f || Feeling == -0.2f)
                RmInsertUnique(OutGroups, It->first);
        }
    }

    // The random pick inlined in FUN_06ef15b0 (06ef1762-06ef17b2) and FUN_06ef1810
    // (06ef1a40-06ef1a92) -- the same expression as FUN_06f4d200: one element -> index 0,
    // else (rand() * n) / 32768 (CDQ / AND 0x7fff / ADD / SAR 0xf = signed division).
    unsigned int PickRandomId(std::vector<unsigned int> const& Ids)
    {
        int const Count = static_cast<int>(Ids.size());
        int Index = 0;
        if (Count - 1 != 0)
            Index = (std::rand() * Count) / 32768;
        return Ids[Index];
    }
}

// =======================================================================================
// FDestroyMission (RandomMissionObject.h)
// =======================================================================================

// DAT_06fcf208: this TU's message type (static init LAB_06eef3b0, GenerateUniqueMessageType
// inlined). Retail never reads it; it only advances the generator.
unsigned int const GDestroyMissionMessageType = StateMachineMessage::GenerateUniqueMessageType();

namespace
{
    // .rdata DAT_06fb75e4 (-1): the "any state" passed to the TU47 body.
    int const GDmAnyState = -1;

    // The router lookup of a runtime machine id (06eef70b-06eef723, 06ef0492-06ef04ab,
    // 06eefa7f-06eefa98): DAT_06fcf254 slot 3, the MessageHandler part - 0x10; a missing id
    // gives null, which retail then calls through.
    FRmRuntimeMachine* DmLookupMachine(unsigned int Id)
    {
        MessageHandler* const Handler = GMessageRouter->LookupHandlerById(Id);
        if (Handler == nullptr)
            return nullptr;
        return static_cast<FRmRuntimeMachine*>(Handler);
    }

    // The recurring test of LAB_06eef740 (06eef7bc-06eef7d3 and its copies): a message for
    // any state (delivery state -1) or for the machine's current state.
    bool DmIsForCurrentState(StateMachine const* Machine, StateMachineMessage const* Message)
    {
        return Message->DeliveryState == -1 || Machine->get_state() == Message->DeliveryState;
    }

    // The inline find + shift-down erase of 06eefe35-06eefe8b / 06eefef0-06eeff32: the first
    // element equal to Id, if any.
    void DmEraseFirst(std::vector<unsigned int>* Ids, unsigned int Id)
    {
        std::vector<unsigned int>::iterator It = std::find(Ids->begin(), Ids->end(), Id);
        if (It != Ids->end())
            Ids->erase(It);
    }

    // FUN_06eb5f30 + FUN_06eb5770: appended when absent (the second find is not read).
    void DmAppendUnique(std::vector<unsigned int>* Ids, unsigned int Id)
    {
        if (std::find(Ids->begin(), Ids->end(), Id) == Ids->end())
            Ids->insert(Ids->end(), Id);
    }
}

// FUN_06eef3d0 (06eef3d0-06eef50f, RET 8). The TU47 base (FUN_06f4b0d0), +0x240 = the parms
// (AddRef when non-null), +0x244 = FindSystemInfo(&DestSys), the +0x248 objective list
// (FUN_06f1ca70, vtable 06fb478c) with +0x258 = 0, the three id vectors, the vtables, +0x80 =
// the first offer's type byte (no empty-list test), then the group's process (the import,
// called directly) with a stack SMEventData of type 1.
FDestroyMission::FDestroyMission(unsigned int const* InControllerId, FRandomMissionData* InData)
    : FRandomMissionObject(InControllerId, InData)
    , MissionData(InData)
{
    DestSystem = FindSystemInfo(&MissionData->DestSys);
    Field_0x80 = MissionData->Offers.front()->Type;
    SMEventData const Enter(static_cast<STATE_MACHINE_EVENT_TYPE>(1));
    StateMachineGroup::process(&Enter);
}

// FUN_06eef550 (06eef550-06eef6d4). Every +0x290 id is removed from the group
// (remove_state_machine(uint), the import), then remove_dying_machines; the three vectors,
// the objective list (vtable 06fb479c, FUN_06f1cb40, node erase), the parms holder (+0x240,
// FUN_06fa0330 inlined) and the TU47 dtor FUN_06f4b460. The port's members follow the body
// in reverse declaration order: the three vectors, the list, then the holder.
FDestroyMission::~FDestroyMission()
{
    for (int Index = 0; Index < static_cast<int>(RunningMachines.size()); ++Index)
        StateMachineGroup::remove_state_machine(RunningMachines[Index]);
    remove_dying_machines();
}

// LAB_06eef6e0: with a running machine, its slot 8 (tail call through the router lookup of
// +0x290[0], -0x10); otherwise the system id of +0x244.
int FDestroyMission::Slot08()
{
    if (!RunningMachines.empty())
        return static_cast<int>(DmLookupMachine(RunningMachines[0])->GetSystemId());
    return static_cast<int>(DestSystem->SystemId);
}

// LAB_06eef730: +0x240 +0x50.
int FDestroyMission::Slot09()
{
    return MissionData->JumpPref;
}

// LAB_06ea1480: XOR AL,AL; RET.
bool FDestroyMission::Slot12()
{
    return false;
}

// FUN_06ef0480 (06ef0480-06ef04c1): slot 7 of every +0x290 machine (router lookup, -0x10),
// then JMP thunk_FUN_06f0fc40 (the TU38 body).
void FDestroyMission::EndMission()
{
    for (int Index = 0; Index < static_cast<int>(RunningMachines.size()); ++Index)
        DmLookupMachine(RunningMachines[Index])->Shutdown();
    FMissionObject::EndMission();
}

// LAB_06ef0460: +0x10 (the reward the TU38 body splits) = +0x240 +0x3c, then JMP FUN_06f4c9c0.
void FDestroyMission::OnMissionSucceed()
{
    Reward = static_cast<unsigned int>(MissionData->Reward);
    FRandomMissionObject::OnMissionSucceed();
}

// FUN_06ef02a0 (06ef02a0-06ef0381, RET 4): a local FmtStr (inline zeroing) opened with slot 45
// of the reason (begin_mad_lib only) goes to the first player as mission message 2, broadcast;
// then the TU47 body FUN_06f4c6f0.
void FDestroyMission::FailMission(int Reason)
{
    FmtStr Text;
    Text.begin_mad_lib(GetReasonIds(Reason));
    if (!PlayerStates.empty())
    {
        unsigned int const Target = PlayerStates[0]->PlayerId;
        pub::Player::DisplayMissionMessage(Target, Text, static_cast<MissionMessageType>(2), true);
    }
    FRandomMissionObject::FailMission(Reason);
}

// FUN_06ef0390 (06ef0390-06ef0451, RET 8): the reason text to the leaving player (message 2,
// not broadcast), then the TU47 body FUN_06f4c830.
void FDestroyMission::RemoveMissionPlayer(unsigned int InPlayerId, int Reason)
{
    FmtStr Text;
    Text.begin_mad_lib(GetReasonIds(Reason));
    pub::Player::DisplayMissionMessage(InPlayerId, Text, static_cast<MissionMessageType>(2), false);
    FRandomMissionObject::RemoveMissionPlayer(InPlayerId, Reason);
}

// LAB_06ef01d0 (RET 4): jump table 06ef0258 over 0..14 (11 and 13 share 0x3325), else 1.
unsigned int FDestroyMission::GetReasonIds(int Reason)
{
    switch (Reason)
    {
    case 0: return 0x331d;
    case 1: return 0x331e;
    case 2: return 0x331f;
    case 3: return 0x3320;
    case 4: return 0x3321;
    case 5: return 0x3322;
    case 6: return 0x3328;
    case 7: return 0x3329;
    case 8: return 0x332a;
    case 9: return 0x3323;
    case 10: return 0x3324;
    case 11: return 0x3325;
    case 12: return 0x3326;
    case 13: return 0x3325;
    case 14: return 0x3327;
    default: return 1;
    }
}

// FUN_06ef0100 (06ef0100-06ef01cb): for every offer of the mission (+0x240 +0x80): offer +0xc =
// this, FUN_06f890f0(offer, &machine) builds the runtime machine (0xb4, FUN_06f89a00) for offer
// types 1..3, its MessageHandler id (get_id on machine+0x10, no null test) is taken, the group
// adds it (slot 4, add_state_machine), and the id is appended to +0x26c when absent.
void FDestroyMission::CreateRuntimeMachines()
{
    for (std::list<FRmOffer*>::iterator It = MissionData->Offers.begin();
         It != MissionData->Offers.end(); ++It)
    {
        FRmOffer* const Offer = *It;
        FRmRuntimeMachine* Machine = nullptr;
        Offer->Mission = this;
        CreateRmRuntimeMachine(Offer, &Machine);
        // No null test: an offer type outside 1..3 leaves Machine null and retail reads
        // get_id of null + 0x10.
        unsigned int const Id = static_cast<MessageHandler*>(Machine)->get_id();
        add_state_machine(Machine);
        if (std::find(PendingMachines.begin(), PendingMachines.end(), Id) ==
            PendingMachines.end())
            PendingMachines.push_back(Id);
    }
}

// LAB_06eef740 (06eef740-06ef00bf, RET 8). "Any state" (*State < 0):
//   controller 0x20: slot 37, then the TU47 body with -1;
//   message DAT_06fd3bb8 (machine started; delivery state -1 or current): no running machine
//     yet -> state 2; the sender leaves +0x27c and joins +0x28c (once);
//   message DAT_06fd3bac: +0x25c = 1 and slot 37;
//   controller 0x39: the TU47 body only while no machine runs; 0x12 / 0x25 / 0x27: the TU47
//     body (0x12 / 0x25 first scan the players for a ship, the result is not used);
//   controller 9: FUN_06f86e40(player) sent (MessageHandler slot 2) to every running machine,
//     then the TU47 body; controller 4 (save): single player and the mission's player ->
//     "[RandomMission]", "type = DestroyMission" and the parms' slot 1, unless the first
//     running machine's slot 12 says no; then the TU47 body; any other controller message: the
//     TU47 body. Every handled message answers true, anything else false.
// State 0: enter -> +0x50 = mad lib 0x32fa + offer type, +0x68 = the mission text, SMM_TIMEOUT
//   to itself after 1.1 s (StateMachineMessageHandler::send_delayed_message_to_me, scope 1);
//   SMM_TIMEOUT -> state 1.
// State 1: enter -> FUN_06ef0100; with a pending machine, its id moves to +0x27c (once) and
//   leaves +0x26c (FUN_06ef1ef0), and DAT_06fd3bbc goes to it after 0.01 s
//   (MessageHandler::send_delayed_message).
// State 2: DAT_06fd3bb0 (failed) -> the sender leaves +0x28c and the group (slot 5), +0x23c =
//   the message's +0x1c, state 4. DAT_06fd3bb4 (succeeded) -> the sender leaves +0x28c and the
//   group; with nothing running or starting: nothing pending -> state 3, else the next pending
//   machine is started as in state 1.
// State 3: enter -> slot 23. State 4: enter -> slot 35 with +0x23c.
bool FDestroyMission::Slot2(SMEventData const* Event, int const& State)
{
    if (State < 0)
    {
        using pub::Controller::EMessage;
        // A type 4 event's int +0x08 (SMControllerEvent::ControllerId) is the controller message
        // id (FRandomMissionObject::HandleMissionMessage), cast back after the type test.
        SMControllerEvent const* Controller = static_cast<SMControllerEvent const*>(Event);
        SMMessage const* Wrapped = static_cast<SMMessage const*>(Event);
        if (Event->get_event_type() == 4 &&
            static_cast<EMessage>(Controller->ControllerId) == EMessage::Heartbeat)
        {
            RefreshAllObjectives();
            FRandomMissionObject::Slot2(Event, GDmAnyState);
            return true;
        }
        if (Event->get_event_type() == 3 && DmIsForCurrentState(this, Wrapped->Message) &&
            Wrapped->Message->MessageType == GRmMsgMachineStarted)
        {
            if (RunningMachines.empty())
                set_state(2);
            unsigned int const Sender = Wrapped->Message->Sender;
            DmEraseFirst(&StartingMachines, Sender);
            DmAppendUnique(&RunningMachines, Sender);
            return true;
        }
        if (Event->get_event_type() == 3 && DmIsForCurrentState(this, Wrapped->Message) &&
            Wrapped->Message->MessageType == GRmMsgRefreshObjectives)
        {
            Field_0x25c = 1;
            RefreshAllObjectives();
            return true;
        }
        if (Event->get_event_type() == 4 &&
            static_cast<EMessage>(Controller->ControllerId) == EMessage::ObjectDestroyed)
        {
            if (RunningMachines.empty())
                FRandomMissionObject::Slot2(Event, GDmAnyState);
            return true;
        }
        if (Event->get_event_type() == 4 &&
            (static_cast<EMessage>(Controller->ControllerId) == EMessage::DockRequest ||
             static_cast<EMessage>(Controller->ControllerId) == EMessage::Msg0x25))
        {
            // 06eef912-06eef934 / 06eef95b-06eef984: the players are scanned until one has a
            // ship (FUN_06ef00d0); the result is never read.
            for (int Index = 0; Index < static_cast<int>(PlayerStates.size()); ++Index)
            {
                if (RmPlayerHasShip(PlayerStates[Index]))
                    break;
            }
            FRandomMissionObject::Slot2(Event, GDmAnyState);
            return true;
        }
        if (Event->get_event_type() == 4 &&
            static_cast<EMessage>(Controller->ControllerId) == EMessage::Msg0x27)
        {
            FRandomMissionObject::Slot2(Event, GDmAnyState);
            return true;
        }
        if (Event->get_event_type() == 4 &&
            static_cast<EMessage>(Controller->ControllerId) == EMessage::GroupMemberLeft)
        {
            FRmPlayerLeftMessage Left(*static_cast<unsigned int const*>(Controller->DataPtr));
            for (int Index = 0; Index < static_cast<int>(RunningMachines.size()); ++Index)
                send_immediate_message(&Left, RunningMachines[Index]);
            FRandomMissionObject::Slot2(Event, GDmAnyState);
            return true;
        }
        if (Event->get_event_type() == 4 &&
            static_cast<EMessage>(Controller->ControllerId) == EMessage::PlayerSave)
        {
            unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
            if (pub::SinglePlayer() && Parms[0] == PlayerState->PlayerId)
            {
                // 06eefa64-06eefa9f: with a running machine, only when the first one's slot
                // 12 (CanSave, FUN_06f8a590) agrees.
                if (RunningMachines.empty() || DmLookupMachine(RunningMachines[0])->CanSave())
                {
                    FILE* File = reinterpret_cast<FILE*>(static_cast<uintptr_t>(Parms[1]));
                    fprintf(File, "[RandomMission]\n");
                    fprintf(File, "type = %s\n", "DestroyMission");
                    MissionData->Save(File);
                }
            }
            FRandomMissionObject::Slot2(Event, GDmAnyState);
            return true;
        }
        if (Event->get_event_type() == 4)
        {
            FRandomMissionObject::Slot2(Event, GDmAnyState);
            return true;
        }
        return false;
    }

    if (State == 0)
    {
        if (Event->get_event_type() < 4 && Event->get_event_type() == 1)
        {
            Field_0x50.destroy();
            Field_0x50 = FmtStr();
            Field_0x68.destroy();
            Field_0x68 = FmtStr();
            Field_0x50.begin_mad_lib(0x32fa + MissionData->Offers.front()->Type);
            Field_0x50.end_mad_lib();
            Field_0x68 = MissionData->Description;
            StateMachineMessage Timeout(SMM_TIMEOUT);
            send_delayed_message_to_me(&Timeout, 1.1f, SMMS_Self, 0);
            return true;
        }
        SMMessage const* Wrapped = static_cast<SMMessage const*>(Event);
        if (Event->get_event_type() == 3 && DmIsForCurrentState(this, Wrapped->Message) &&
            Wrapped->Message->MessageType == SMM_TIMEOUT)
        {
            set_state(1);
            return true;
        }
        return false;
    }

    if (State == 1)
    {
        if (Event->get_event_type() >= 4 || Event->get_event_type() != 1)
            return false;
        CreateRuntimeMachines();
        if (!PendingMachines.empty())
        {
            unsigned int const Id = PendingMachines[0];
            DmAppendUnique(&StartingMachines, Id);
            DmEraseFirst(&PendingMachines, Id);
            StateMachineMessage Start(GRmMsgStartMachine);
            send_delayed_message(&Start, 0.01f, Id);
        }
        return true;
    }

    if (State == 3)
    {
        if (Event->get_event_type() >= 4 || Event->get_event_type() != 1)
            return false;
        OnMissionSucceed();
        return true;
    }

    if (State == 4)
    {
        if (Event->get_event_type() >= 4 || Event->get_event_type() != 1)
            return false;
        FailMission(FailReason);
        return true;
    }

    if (State == 2)
    {
        if (Event->get_event_type() != 3)
            return false;
        SMMessage const* Wrapped = static_cast<SMMessage const*>(Event);
        StateMachineMessage const* Message = Wrapped->Message;
        if (DmIsForCurrentState(this, Message) && Message->MessageType == GRmMsgMachineFailed)
        {
            DmEraseFirst(&RunningMachines, Message->Sender);
            remove_state_machine(Message->Sender);
            FailReason = static_cast<FRmMachineFailedMessage const*>(Message)->Reason;
            set_state(4);
            return true;
        }
        if (Event->get_event_type() != 3)
            return false;
        if (!DmIsForCurrentState(this, Message) || Message->MessageType != GRmMsgMachineSucceeded)
            return false;
        DmEraseFirst(&RunningMachines, Message->Sender);
        remove_state_machine(Message->Sender);
        if (!RunningMachines.empty() || !StartingMachines.empty())
            return true;
        if (PendingMachines.empty())
        {
            set_state(3);
            return true;
        }
        unsigned int const Id = PendingMachines[0];
        DmAppendUnique(&StartingMachines, Id);
        DmEraseFirst(&PendingMachines, Id);
        StateMachineMessage Start(GRmMsgStartMachine);
        send_delayed_message(&Start, 0.01f, Id);
        return true;
    }
    return false;
}

// FUN_06ef00d0 (06ef00d0-06ef00da): SETNZ of +0x10.
bool RmPlayerHasShip(FContentPlayerState const* State)
{
    return State->ShipObject != nullptr;
}

// FUN_06eeec50 06eeed07-06eeed28: operator new(0x29c); on success FUN_06eef3d0(&ControllerId,
// Parms). The parms are the TU27 mission (CreateParms at +0).
IDLL* CreateDestroyMission(char const* /*Name*/, unsigned int ControllerId, void const* Parms)
{
    pub::Controller::CreateParms* Base =
        const_cast<pub::Controller::CreateParms*>(static_cast<pub::Controller::CreateParms const*>(Parms));
    return new (std::nothrow) FDestroyMission(&ControllerId, static_cast<FRandomMissionData*>(Base));
}

// =======================================================================================
// FRandomMissionData
// =======================================================================================

// FUN_06ef0600 (06ef0600-06ef06c4, cdecl): operator new(0x88); on success the TU47 base
// ctor, ObjectivePos / ObjectiveZone 0, the tree path (06ef064c-06ef0658), the empty offer
// list (06ef066a-06ef0681) and the two vptrs; *OutMission = it (0 on failure). AL = 1.
bool FRandomMissionData::Create(FRandomMissionData** OutMission)
{
    *OutMission = new (std::nothrow) FRandomMissionData();
    return true;
}

// FUN_06ef06d0 (06ef06d0-06ef0800), deleting dtor FUN_06ef0550 / thunk LAB_06ef2060: a
// non-zero ObjectiveZone is erased from GRmObjectiveZones (first match; FUN_06f6a640 moves
// the tail down), every listed offer gets (Offer + 4)->Release, then the list
// (FUN_06ecfec0 / FUN_06f15350), the tree path and the base (~FmtStr +0x24) are destroyed
// -- the member order of the port.
FRandomMissionData::~FRandomMissionData()
{
    if (ObjectiveZone != 0)
    {
        std::vector<unsigned int>::iterator It =
            std::find(GRmObjectiveZones.begin(), GRmObjectiveZones.end(), ObjectiveZone);
        if (It != GRmObjectiveZones.end())
            GRmObjectiveZones.erase(It);
    }
    for (std::list<FRmOffer*>::iterator It = Offers.begin(); It != Offers.end(); ++It)
        (*It)->Release();
}

// FUN_06ef0810 (RET 4): the base's eight lines, then these six; the tree path is not saved
// here.
void FRandomMissionData::Save(FILE* File)
{
    FRmMissionDataBase::Save(File);
    fprintf(File, "enemy_group = %u\n", EnemyGroup);
    fprintf(File, "dest_sys = %u\n", DestSys);
    fprintf(File, "objective_pos = %g,%g,%g\n", static_cast<double>(ObjectivePos.x),
            static_cast<double>(ObjectivePos.y), static_cast<double>(ObjectivePos.z));
    fprintf(File, "objective_zone = %u\n", ObjectiveZone);
    fprintf(File, "random_seed = %u\n", RandomSeed);
    fprintf(File, "\n");
}

// LAB_06ef0890 (RET 4): the base's keys first (its result is not read), then these. The
// position values are read as integers (FILD). A loaded objective zone is appended to
// GRmObjectiveZones when absent (FUN_06f937c0; the find that follows has no effect).
void FRandomMissionData::LoadValue(INI_Reader* Reader)
{
    FRmMissionDataBase::LoadValue(Reader);
    if (Reader->is_value("enemy_group"))
    {
        EnemyGroup = static_cast<unsigned int>(Reader->get_value_int(0));
        return;
    }
    if (Reader->is_value("dest_sys"))
    {
        DestSys = static_cast<unsigned int>(Reader->get_value_int(0));
        return;
    }
    if (Reader->is_value("objective_pos"))
    {
        ObjectivePos.x = static_cast<float>(Reader->get_value_int(0));
        ObjectivePos.y = static_cast<float>(Reader->get_value_int(1));
        ObjectivePos.z = static_cast<float>(Reader->get_value_int(2));
        return;
    }
    if (Reader->is_value("objective_zone"))
    {
        ObjectiveZone = static_cast<unsigned int>(Reader->get_value_int(0));
        if (std::find(GRmObjectiveZones.begin(), GRmObjectiveZones.end(), ObjectiveZone) ==
            GRmObjectiveZones.end())
            GRmObjectiveZones.insert(GRmObjectiveZones.end(), ObjectiveZone);
        return;
    }
    if (Reader->is_value("random_seed"))
        RandomSeed = static_cast<unsigned int>(Reader->get_value_int(0));
}

// =======================================================================================
// FRandomMissionLoader -- TU27 slots and helpers
// =======================================================================================

// FUN_06ef09c0 (RET 4, the argument is not read; callers Load 06ef0b9c with 1, CreateMission
// 06ef1ea7 with 0). The offer query (FUN_06f9cac0 defaults) takes the mission's values;
// srand(RandomSeed) makes the tree walk repeat the saved mission. A built offer is appended
// to the list and AddRef'd, and its text becomes the mission's Description. 2 when the list
// is still empty, else 0; the local offer reference is released either way.
int FRandomMissionLoader::FinalizeMission(int /*Unused*/)
{
    int Result = 0;
    FRmOfferQuery Query;
    Query.OfferGroup = Mission->OfferGroup;
    Query.EnemyGroup = Mission->EnemyGroup;
    Query.Reward = Mission->Reward;
    Query.RepReward = Mission->RepReward;
    Query.RepHit = Mission->RepHit;
    Query.OfferBase = Mission->OfferBase;
    Query.Difficulty = Mission->Difficulty;
    Query.ObjectiveZone = Mission->ObjectiveZone;
    Query.ObjectivePos = Mission->ObjectivePos;
    Query.PlayerId = Mission->PlayerId;
    Query.DestSys = Mission->DestSys;

    // [ESP+0x8] (EBP-0x4c): the offer holder, released by FUN_06fa0330 on unwind and inline
    // at 06ef0acf-06ef0ae5.
    TRmRef<FRmOffer> Offer;
    std::srand(Mission->RandomSeed);
    // DAT_06fcf25c->vtable[+0xc](&Query, &Offer, &Mission->TreePath).
    if (GRandomMissionGenerator->BuildOffer(&Query, Offer.GetAddress(), &Mission->TreePath))
    {
        Mission->Offers.push_back(Offer.Get());           // FUN_06f1e390 at the end
        Offer->AddRef();
        Mission->Description = Offer->Text;                 // FmtStr::operator= (offer +0x3c)
    }

    if (Mission->Offers.empty())
        Result = 2;
    return Result;
}

// LAB_06ef0b00 (RET 0xc). The reader's state is saved and the reader reset; the old
// mission is released through its interface (not deleted) and a new one created (no null
// test); every [RandomMission] section feeds its values to LoadValue and then
// FinalizeMission, whose 0 becomes the result (NEG / SBB / INC). *OutMission = the mission
// without an AddRef.
bool FRandomMissionLoader::Load(INI_Reader* Reader, FRandomMissionData** OutMission,
                                unsigned int PlayerId)
{
    bool bLoaded = false;
    INI_Reader::State SavedState;
    Reader->get_state(SavedState);
    Reader->reset();

    if (Mission != nullptr)
    {
        Mission->Release();
        Mission = nullptr;
    }
    FRandomMissionData::Create(&Mission);
    Mission->PlayerId = PlayerId;

    while (Reader->read_header())
    {
        if (!Reader->is_header("RandomMission"))
            continue;
        while (Reader->read_value())
            Mission->LoadValue(Reader);
        bLoaded = FinalizeMission(1) == 0;
    }

    Reader->set_state(SavedState);
    *OutMission = Mission;
    return bLoaded;
}

// FUN_06ef0be0 (RET 8). BaseId = the offer base; MissionClass = the first offer's type byte
// (+0x54) + 1; Title = mad lib 0x734 with the LOADER's mission's system (06ef0c16 reads
// [this + 8], not the argument); Description = mad lib 0x72a with the offering group's
// name; Briefing = the mission's text; +0x58 = the reward, +0x5c = the player. TargetId and
// SystemId are not written.
void FRandomMissionLoader::GetOfferDescription(FRandomMissionData const* InMission,
                                               MissionVendorOfferDescription* OutDescription)
{
    OutDescription->BaseId = InMission->OfferBase;
    OutDescription->MissionClass =
        static_cast<unsigned int>(InMission->Offers.front()->Type) + 1;

    OutDescription->Title.begin_mad_lib(0x734);
    OutDescription->Title.append_system(Mission->DestSys);
    OutDescription->Title.end_mad_lib();

    OutDescription->Description.begin_mad_lib(0x72a);
    // Retail reuses the argument slot for the name id; GetGroupName always writes it.
    unsigned int GroupNameId = 0;
    pub::Reputation::GetGroupName(static_cast<unsigned int>(InMission->OfferGroup), GroupNameId);
    OutDescription->Description.append_string(GroupNameId);
    OutDescription->Description.end_mad_lib();

    OutDescription->Briefing = InMission->Description;
    OutDescription->IconTextureId = static_cast<unsigned int>(InMission->Reward);
    OutDescription->Filter = InMission->PlayerId;
}

// FUN_06ef0c90 (RET 0xc). Same system -> false; any id 0 -> true. Otherwise the route is
// walked one connection at a time (TU41 FUN_06f29c10 with the mission's jump preference,
// then FUN_06f2a240 to the far system): no next connection, or one of the gates locked for
// the player or for everybody (TU53 FUN_06f6d2b0), blocks it.
bool FRandomMissionLoader::IsRouteBlocked(unsigned int FromSystem, unsigned int ToSystem,
                                          unsigned int PlayerId)
{
    if (FromSystem == ToSystem)
        return false;
    if (FromSystem == 0 || ToSystem == 0 || PlayerId == 0)
        return true;

    bool bBlocked = false;
    std::vector<unsigned int> LockedGates;
    CollectContentGateLocks(PlayerId, LockedGates);
    unsigned int Current = FromSystem;
    while (Current != ToSystem)
    {
        unsigned int const Connection =
            GetNextHopConnectionObject(Current, ToSystem, Mission->JumpPref);
        if (Connection == 0)
        {
            bBlocked = true;
            break;
        }
        if (std::find(LockedGates.begin(), LockedGates.end(), Connection) != LockedGates.end())
        {
            bBlocked = true;
            break;
        }
        GetConnectionGotoSystem(Current, Connection, &Current);
    }
    return bBlocked;
}

// LAB_06ef0dc0 (RET 0xc). The offer base's system (pub::GetSystem) against the mission's:
// a different system whose route is blocked gives mad lib 0x740, code 2 and false.
bool FRandomMissionLoader::CanAccept(FRandomMissionData const* /*Unused*/, FmtStr* OutReason,
                                     int* OutCode)
{
    bool bBlocked = false;
    // Retail's slot holds the pushed ECX until pub::GetSystem writes it.
    unsigned int OfferSystem = 0;
    pub::GetSystem(OfferSystem, Mission->OfferBase);
    if (OfferSystem != Mission->DestSys)
    {
        bBlocked = IsRouteBlocked(OfferSystem, Mission->DestSys, Mission->PlayerId);
        if (bBlocked)
        {
            OutReason->begin_mad_lib(0x740);
            OutReason->end_mad_lib();
            *OutCode = 2;
        }
    }
    return !bBlocked;
}

// FUN_06ef0e30 (RET 0x14). Range > 0 (FCOMP DAT_06fb444c + TEST AH,0x41 + JNZ returns on
// <= and unordered): the mission zones within Range * 300 of Position.
void FRandomMissionLoader::CollectZonesInRange(unsigned int /*PlayerId*/, FSystemInfo* System,
                                               Vector const* Position, float Range,
                                               std::vector<unsigned int>* OutZones)
{
    if (!(Range > 0.0f))
        return;
    CollectMissionZones(System, *Position, Range * GRmRangeScale, OutZones);
}

// FUN_06ef0f20 (RET 0x14). Range > 10 (DAT_06fb7614). For every trade lane passing within
// Range * 300 (FUN_06f767d0) the nearer end is where the lane is entered and the other end
// where it leaves: the distance to the entry (3DMathEngine::Sqrt of the float squared
// distance) must be > 0 and leave Range - D / 300 - 10 > 0; 10 less again is the range left
// at the exit. When that is > 0 the zones around the exit are collected; the exit is then
// searched along trade lanes and jumps with the range left (also when it is <= 0 -- those
// return at once).
void FRandomMissionLoader::CollectZonesAlongTradeLanes(unsigned int PlayerId,
                                                       FSystemInfo* System,
                                                       Vector const* Position, float Range,
                                                       std::vector<unsigned int>* OutZones)
{
    if (!(Range > 10.0f))
        return;

    std::vector<FTradeLane*> Lanes;
    System->FindTradeLanesNear(Position, Range * GRmRangeScale, &Lanes);
    for (std::vector<FTradeLane*>::iterator LaneIt = Lanes.begin(); LaneIt != Lanes.end();
         ++LaneIt)
    {
        FTradeLane const* Lane = *LaneIt;
        Vector const& Start = Lane->StartPosition;
        Vector const& End = Lane->EndPosition;
        // 06ef0fbc-06ef0fe4: Start - End is built (FUN_06eb7810) and never read.

        // Each difference is stored as a float; the sums of squares (z, y, x order) are
        // stored as floats too.
        float const StartDx = Start.x - Position->x;
        float const StartDy = Start.y - Position->y;
        float const StartDz = Start.z - Position->z;
        float const StartDistSq = static_cast<float>(
            static_cast<double>(StartDz) * StartDz + static_cast<double>(StartDy) * StartDy +
            static_cast<double>(StartDx) * StartDx);
        float const EndDx = End.x - Position->x;
        float const EndDy = End.y - Position->y;
        float const EndDz = End.z - Position->z;
        float const EndDistSq = static_cast<float>(
            static_cast<double>(EndDz) * EndDz + static_cast<double>(EndDy) * EndDy +
            static_cast<double>(EndDx) * EndDx);

        if (GRmMissionMath == nullptr)
            GRmMissionMath = ContentAcquire3DMathEngine();

        // FCOMP + TEST AH,0x5 + JP: the start only when strictly nearer (unordered takes
        // the end branch).
        float Distance = 0.0f;
        Vector Exit;
        if (StartDistSq < EndDistSq)
        {
            GRmMissionMath->Sqrt(Distance, StartDistSq);
            Exit = End;
        }
        else
        {
            GRmMissionMath->Sqrt(Distance, EndDistSq);
            Exit = Start;
        }

        if (!(Distance > 0.0f))
            continue;
        double const Remaining = static_cast<double>(Range) -
                                 static_cast<double>(Distance) / GRmRangeScale - 10.0;
        if (!(Remaining > 0.0))
            continue;

        // DAT_06fb7610: FST float for the recursion, the compare on the unrounded value.
        double const ExitRangeExact = Remaining - 10.0;
        float const ExitRange = static_cast<float>(ExitRangeExact);
        if (ExitRangeExact > 0.0)
            CollectMissionZones(System, Exit, ExitRange * GRmRangeScale, OutZones);

        CollectZonesAlongTradeLanes(PlayerId, System, &Exit, ExitRange, OutZones);
        CollectZonesThroughJumps(PlayerId, System, &Exit, ExitRange, OutZones);
    }
}

// FUN_06ef1240 (RET 0x14). Range > 45 (DAT_06fb7618). The faction's jump preference
// (TU48 record +0x58) limits the jump type. For every jump of System at a distance D > 0
// (FSQRT) with Range - D / 300 - 45 > 0 whose gate is not locked for the player (TU53
// FUN_06f6ce90), the far system is loaded (FUN_06f4d5e0) and its jump back (ObjectId ==
// GotoObject) is the arrival point (skipped when missing or when a coordinate is still
// 1e10). 10 less is the range left there: zones around the arrival when > 0, then trade
// lanes and jumps from it. An empty local std::list (FUN_06f93790, 06ef1282-06ef1295) is
// built right after the range test and destroyed at the end without use (EH state 0 for the
// whole body, funclet 06fac040 FUN_06f15350).
void FRandomMissionLoader::CollectZonesThroughJumps(unsigned int PlayerId, FSystemInfo* System,
                                                    Vector const* Position, float Range,
                                                    std::vector<unsigned int>* OutZones)
{
    if (!(Range > 45.0f))
        return;

    // 06ef1282-06ef1295: [ESP+0x3c] (EBP-0x430), head node FUN_06f93790 (0xc bytes, so a
    // 4-byte element). Never read or written afterwards; the element type is unknowable,
    // unsigned int stands in for it.
    std::list<unsigned int> UnusedList;

    // 06ef1299-06ef12c7: TU48 FUN_06f50650 (no null test) +0x58, 0 = any jump type.
    unsigned int OfferGroupId = static_cast<unsigned int>(Mission->OfferGroup);
    int const JumpPreference = GetFactionProps(&OfferGroupId)->JumpPreference;

    for (unsigned int JumpIndex = 0; JumpIndex < System->Jumps.size(); ++JumpIndex)
    {
        FSystemJump const Jump = System->Jumps[JumpIndex];

        float const Dx = Position->x - Jump.Position.x;
        float const Dy = Position->y - Jump.Position.y;
        float const Dz = Position->z - Jump.Position.z;
        double const DistanceExact =
            std::sqrt(static_cast<double>(Dz) * Dz + static_cast<double>(Dy) * Dy +
                      static_cast<double>(Dx) * Dx);
        float const Distance = static_cast<float>(DistanceExact);
        if (!(DistanceExact > 0.0))
            continue;
        if (JumpPreference != 0 && JumpPreference != Jump.Type)
            continue;

        double const RemainingExact = static_cast<double>(Range) -
                                      static_cast<double>(Distance) / GRmRangeScale - 45.0;
        float const Remaining = static_cast<float>(RemainingExact);
        if (!(RemainingExact > 0.0))
            continue;

        // TU53 FUN_06f6ce90 (06ef138c): the gate locked for this player or for everybody.
        if (IsContentGateLocked(&PlayerId, &Jump.GotoObject))
            continue;

        FSystemInfo* Target = FindSystemInfo(&Jump.GotoSystem);
        RegisterRmDestSystem(Target->SystemId);

        bool bFound = false;
        Vector Arrival;
        Arrival.x = 1e10f;                          // 0x501502f9
        Arrival.y = 1e10f;
        Arrival.z = 1e10f;
        for (unsigned int BackIndex = 0; BackIndex < Target->Jumps.size(); ++BackIndex)
        {
            if (Target->Jumps[BackIndex].ObjectId == Jump.GotoObject)
            {
                Arrival = Target->Jumps[BackIndex].Position;
                bFound = true;
                break;
            }
        }
        if (!bFound)
            continue;
        // FCOMP DAT_06fb781c + TEST AH,0x44 + JNP: equal skips, unordered goes on.
        if (Arrival.x == 1e10f || Arrival.y == 1e10f || Arrival.z == 1e10f)
            continue;

        double const ArrivalRangeExact = static_cast<double>(Remaining) - 10.0;
        float const ArrivalRange = static_cast<float>(ArrivalRangeExact);
        if (ArrivalRangeExact > 0.0)
            CollectMissionZones(Target, Arrival, ArrivalRange * GRmRangeScale, OutZones);

        CollectZonesAlongTradeLanes(PlayerId, Target, &Arrival, ArrivalRange, OutZones);
        CollectZonesThroughJumps(PlayerId, Target, &Arrival, ArrivalRange, OutZones);
    }
}

// FUN_06ef15b0 (RET 0x10). Candidates from Range - 10 (stored as a float) around Position,
// along trade lanes and through jumps; a candidate survives when it is not already an
// objective and the offering faction's legality (TU48 record +0x94: 1 lawful, 2 unlawful)
// matches the zone's mission_type. One survivor is picked at random; 0 when its route from
// System is blocked, or when none survived.
unsigned int FRandomMissionLoader::PickObjectiveZone(unsigned int PlayerId, FSystemInfo* System,
                                                     Vector const* Position, float Range)
{
    RegisterRmDestSystem(System->SystemId);
    std::vector<unsigned int> Zones;
    unsigned int Picked = 0;

    float const SearchRange = Range - 10.0f;
    CollectZonesInRange(PlayerId, System, Position, SearchRange, &Zones);
    CollectZonesAlongTradeLanes(PlayerId, System, Position, SearchRange, &Zones);
    CollectZonesThroughJumps(PlayerId, System, Position, SearchRange, &Zones);

    std::vector<unsigned int>::iterator It = Zones.begin();
    while (It != Zones.end())
    {
        unsigned int ZoneSystemId = pub::Zone::GetSystem(*It);
        FSystemZone* Zone = FindSystemInfo(&ZoneSystemId)->FindZone(*It);
        bool bKeep = false;
        if (std::find(GRmObjectiveZones.begin(), GRmObjectiveZones.end(), *It) ==
            GRmObjectiveZones.end())
        {
            // 06ef16a3-06ef16f3: TU48 FUN_06f50650(&OfferGroup) (no null test), read twice.
            unsigned int OfferGroupId = static_cast<unsigned int>(Mission->OfferGroup);
            if (GetFactionProps(&OfferGroupId)->Legality == FactionLegalityLawful &&
                Zone->bLawful)
                bKeep = true;
            else if (GetFactionProps(&OfferGroupId)->Legality == FactionLegalityUnlawful &&
                     Zone->bUnlawful)
                bKeep = true;
        }
        if (bKeep)
            ++It;
        else
            It = Zones.erase(It);
    }

    if (!Zones.empty())
    {
        Picked = PickRandomId(Zones);
        if (IsRouteBlocked(System->SystemId, pub::Zone::GetSystem(Picked), PlayerId))
            Picked = 0;
    }
    return Picked;
}

// FUN_06ef1810 (RET 0xc). The hostile groups populating the zones within 7500
// (DAT_06fb761c) of Position; when there are none, those of every zone of the system
// (pub::System::EnumerateZones). One is picked at random, -1 when there is none.
unsigned int FRandomMissionLoader::PickEnemyGroup(FSystemInfo* System, Vector const* Position,
                                                  int OfferGroup)
{
    RegisterRmDestSystem(System->SystemId);
    std::vector<unsigned int> Groups;

    unsigned int ZoneIds[0x100];
    unsigned int Count = 0;
    pub::System::InZones(System->SystemId, *Position, 7500.0f, ZoneIds, 0x100, Count);
    for (unsigned int Index = 0; Index < Count; ++Index)
        CollectHostileGroups(ZoneIds[Index], OfferGroup, &Groups);

    if (Groups.empty())
    {
        FRmZoneEnumerator Enumerator;
        unsigned int SystemId = System->SystemId;
        pub::System::EnumerateZones(SystemId, Enumerator);
        for (std::vector<unsigned int>::iterator It = Enumerator.Zones.begin();
             It != Enumerator.Zones.end(); ++It)
        {
            CollectHostileGroups(*It, OfferGroup, &Groups);
        }
    }

    if (Groups.empty())
        return 0xffffffffu;
    return PickRandomId(Groups);
}

// LAB_06ef1c70 (RET 8). Result starts at 3. The held mission is released (FUN_06fa0330) and
// a new one created (no null test) with a fresh seed rand() (also passed to srand), the
// request's player / base / group / difficulty, the reward for the difficulty (FUN_06f98380)
// and rep_reward / rep_hit +/-0.15 (0x3e19999a / 0xbe19999a). The search starts at the
// base's dock object (FUN_06eac0c0 -> FUN_06eac4d0, no null test; the position stays
// unset when the base has none) in the player's system (FUN_06f49b00 -> FUN_06f478c0)
// with range 90, in multiplayer 90 + 30 per other player in that system, at most 300
// (FCOM + TEST AH,0x41 + JNZ keeps <= and unordered). No zone -> 3. Otherwise the zone
// becomes an objective, the mission takes its system and position, and the enemy group is
// picked there -- else around the origin of the base's system (a missing SystemInfo keeps
// -1). A group -> FinalizeMission (0 / 2), none -> 1. *OutMission = the mission in every
// case (no AddRef).
int FRandomMissionLoader::CreateMission(FRmMissionRequest const* Request,
                                        FRandomMissionData** OutMission)
{
    int const NoObjective = 3;
    if (Mission != nullptr)
    {
        Mission->Release();
        Mission = nullptr;
    }
    FRandomMissionData::Create(&Mission);
    Mission->RandomSeed = static_cast<unsigned int>(std::rand());
    std::srand(Mission->RandomSeed);
    Mission->PlayerId = Request->PlayerId;
    Mission->OfferBase = Request->OfferBase;
    Mission->OfferGroup = Request->OfferGroup;
    Mission->Difficulty = Request->Difficulty;
    Mission->Reward = static_cast<int>(RmDifficultyToMoney(Mission->Difficulty));
    Mission->RepReward = 0.15f;
    Mission->RepHit = -0.15f;

    // Retail's local is uninitialised when the base has no dock object.
    Vector DockPosition = {};
    FindContentBaseInfo(&Mission->OfferBase)->FindDockObject(&DockPosition);

    FSystemInfo* System = FindContentPlayerState(Mission->PlayerId)->GetSystemRecord();
    RegisterRmDestSystem(System->SystemId);

    float Range = 90.0f;                                            // 0x42b40000
    if (!pub::SinglePlayer())
    {
        // FUN_06f78ee0 = &SystemInfo +0xb8 (the players); a null begin counts 0.
        int const OtherPlayers = static_cast<int>(
            FindContentPlayerState(Mission->PlayerId)->GetSystemRecord()->Players.size()) - 1;
        double Scaled = static_cast<double>(OtherPlayers) * 30.0 + 90.0; // DAT_06fb7828/7824
        if (Scaled > 300.0)                                             // DAT_06fb7820
            Scaled = 300.0;
        Range = static_cast<float>(Scaled);
    }

    unsigned int const Zone = PickObjectiveZone(Mission->PlayerId, System, &DockPosition, Range);
    if (Zone == 0)
    {
        *OutMission = Mission;
        return NoObjective;
    }

    RmInsertUnique(&GRmObjectiveZones, Zone);
    Mission->DestSys = pub::Zone::GetSystem(Zone);
    FSystemInfo* DestSystem = FindSystemInfo(&Mission->DestSys);
    Mission->ObjectiveZone = Zone;
    Mission->ObjectivePos = pub::Zone::GetPos(Zone);
    Mission->EnemyGroup = PickEnemyGroup(DestSystem, &Mission->ObjectivePos, Mission->OfferGroup);
    if (Mission->EnemyGroup == 0xffffffffu)
    {
        unsigned int BaseSystemId = 0;
        pub::GetSystem(BaseSystemId, Mission->OfferBase);
        FSystemInfo* BaseSystem = FindSystemInfo(&BaseSystemId);
        if (BaseSystem == nullptr)
        {
            Mission->EnemyGroup = 0xffffffffu;
        }
        else
        {
            Vector Origin;
            Origin.x = 0.0f;
            Origin.y = 0.0f;
            Origin.z = 0.0f;
            Mission->EnemyGroup = PickEnemyGroup(BaseSystem, &Origin, Mission->OfferGroup);
        }
    }

    if (Mission->EnemyGroup != 0xffffffffu)
    {
        int const Result = FinalizeMission(0);
        *OutMission = Mission;
        return Result;
    }
    *OutMission = Mission;
    return 1;
}
