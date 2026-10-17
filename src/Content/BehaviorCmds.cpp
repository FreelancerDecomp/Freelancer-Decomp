// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU50 (06f56f50-06f57cb0): the ship base class (see BehaviorCmds.h).

#include "BehaviorCmds.h"
#include "DockQueues.h"        // GetShipDockingTarget (FollowDirectivesOf)
#include "MissionObjList.h"    // FObjListDock (FollowDirectivesOf)
#include "ObjListDirective.h"
#include "RefHandle.h"          // TRefHandle (AdvanceDirective's Next)
#include "ContentPlayerState.h"
#include "RandomMissionDB.h"   // FindShipClass (TU51 FUN_06f58080)
#include "ContentStaticIds.h"  // TU50's 16 static-init thunks 06f56f50-06f57130
#include "common_server.h"

// FContentShipParams' copy ctor (FUN_06f73680) and dtor (FUN_06ef4ab0) are implicit: their
// +0x60 work is FMissionDirectiveList's (ObjListDirective.h), see BehaviorCmds.h.

// FUN_06f571a0 (06f571a0-06f572e6). Order: TU20 base; secondary vptr (abstract);
// bytes +0x11c/+0x11d = 0; +0x120/+0x124 = 0; +0x128 = copy of Params+0x50; +0x138 =
// Params+0x74; +0x13c empty vector; +0x14c = Params+0x48; +0x150 = Params+0x44; +0x154 =
// Params+0x4c; +0x158 empty list (head node new(0xc)), size 0; +0x168 = Params+0x70;
// +0x16c = 0; +0x170 = Params+0x6c; final vptrs 06fbba08 / 06fbba04. +0x164 is not written.
FContentShipBase::FContentShipBase(FContentShipParams const* Params)
    : FContentSpaceObj(Params)
    , ClassIds(Params->ClassIds)
    , ShipArchId(Params->ShipArchId)
    , FormationSlot(Params->Field_0x48)
    , Formation(Params->Formation)
    , FormationId(Params->Field_0x4c)
    , Priority(Params->Priority)
    , ControllerId(Params->ControllerId)
{
    CurrentDirective = Directives.Items.end();
}

// FUN_06f33d20 (06f33d20-06f33e2c). The body releases and clears ActiveDirective (EH state 3
// of FuncInfo 06fc3b30 covers it); then the member teardown: every queued directive released
// (no null check, 06f33d86-06f33d9d) and the list freed -- the FMissionDirectiveList dtor of
// +0x158, the unwind action FUN_06ecfa90 --, the +0x13c and +0x128 vectors freed, and the TU20
// dtor FUN_06eb7230 runs.
FContentShipBase::~FContentShipBase()
{
    if (ActiveDirective != nullptr)
    {
        ActiveDirective->Release();
        ActiveDirective = nullptr;
    }
}

// 06ea1480: XOR AL,AL; RET.
bool FContentShipBase::Slot04()
{
    return false;
}

// 06f33cf0: MOV AL,[ECX+0x11c]; RET.
bool FContentShipBase::Slot08()
{
    return bInCombat != 0;
}

// 06f57c30: LEA EAX,[ECX+0x16c]; RET.
FObjListDirective** FContentShipBase::GetActiveDirectiveSlot()
{
    return &ActiveDirective;
}

// 06f57c40 (06f57c40-06f57c5d).
void FContentShipBase::ReleaseActiveDirective()
{
    if (ActiveDirective != nullptr)
    {
        ActiveDirective->Release();
        ActiveDirective = nullptr;
    }
}

// FUN_06f57820 (06f57820-06f57832), DirectiveCallback slot 0: [sub+0x48] = +0x160 list
// size; both arguments unused (RET 8).
void FContentShipBase::OnDirectiveComplete(int ResultWord11c, int ResultWord118)
{
    (void)ResultWord11c;
    (void)ResultWord118;
    if (!Directives.Items.empty())
        AdvanceDirective();
}

// FUN_06f33e40 (06f33e40-06f33e6b): local rep = FUN_06f33f80(+8) (no null check);
// GetRank(&rep, &rank) (status ignored, rank uninitialised if it fails); FLD rank; JMP ftol.
int FContentShipBase::GetReputationRank() const
{
    int Rep = ReputationHandle->GetReputation();
    float Rank;
    pub::Reputation::GetRank(Rep, Rank);
    return static_cast<int>(Rank);
}

// FUN_06f57150 (06f57150-06f5716a): GetCargoHoldSize(+0x138, local); return local.
// Retail's local is the slot pushed from ECX (uninitialised); the Server export writes it.
unsigned int FContentShipBase::GetCargoHoldSize() const
{
    unsigned int Size = 0;
    pub::GetCargoHoldSize(ShipArchId, Size);
    return Size;
}

// FUN_06f57170 (06f57170-06f57199): local id = +4, out = 0, GetCargoSpaceOccupied.
unsigned int FContentShipBase::GetCargoSpaceOccupied() const
{
    unsigned int Occupied = 0;
    unsigned int Id = SpaceObjId;
    pub::SpaceObj::GetCargoSpaceOccupied(Id, Occupied);
    return Occupied;
}

// FUN_06f57310 (06f57310-06f574fd). Ids with (Id & 0x3fffffff) == 0 are ignored. An
// existing entry gets Amount added (FLD Amount; FADD [e+4]; FSTP [e+4]); otherwise
// {Id, Amount} is inserted at the end (vector insert(end, 1, value)).
void FContentShipBase::AddDamage(unsigned int const* AttackerId, float Amount)
{
    unsigned int Id = *AttackerId;
    if ((Id & 0x3fffffff) == 0)
        return;
    for (std::vector<std::pair<unsigned int, float>>::iterator It = Damage.begin(); It != Damage.end(); ++It)
    {
        if (It->first == Id)
        {
            It->second = Amount + It->second;
            return;
        }
    }
    Damage.push_back(std::pair<unsigned int, float>(Id, Amount));
}

// FUN_06f57500 (06f57500-06f57596). Both maxima start at 0.0f; `FCOMP [max]; TEST AH,1;
// JNZ skip` skips when the entry is below the maximum (or unordered), so the last entry
// with damage >= the current maximum wins. Player entries: FUN_06f49bc0 != null.
void FContentShipBase::GetTopDamagers(unsigned int* OutTopPlayer, unsigned int* OutTopAny) const
{
    unsigned int TopAny = 0;
    unsigned int TopPlayer = 0;
    float MaxAny = 0.0f;
    float MaxPlayer = 0.0f;
    for (std::vector<std::pair<unsigned int, float>>::const_iterator It = Damage.begin(); It != Damage.end(); ++It)
    {
        if (FindContentPlayerStateByShip(&It->first) != nullptr && It->second >= MaxPlayer)
        {
            MaxPlayer = It->second;
            TopPlayer = It->first;
        }
        if (It->second >= MaxAny)
        {
            MaxAny = It->second;
            TopAny = It->first;
        }
    }
    *OutTopPlayer = TopPlayer;
    *OutTopAny = TopAny;
}

// FUN_06f575a0 (06f575a0-06f575c8): linear find of *ClassId in +0x128.
bool FContentShipBase::HasClassId(unsigned int const* ClassId) const
{
    for (std::vector<unsigned int>::const_iterator It = ClassIds.begin(); It != ClassIds.end(); ++It)
    {
        if (*It == *ClassId)
            return true;
    }
    return false;
}

// FUN_06f575d0 (06f575d0-06f57632): FUN_06f58080(ShipClassId); null or empty -> false;
// else true on the first member found in ClassIds (inlined FUN_06f575a0 loop).
bool FContentShipBase::IsInShipClass(unsigned int const* ShipClassId) const
{
    std::vector<unsigned int>* Members = FindShipClass(ShipClassId);
    if (Members == nullptr)
        return false;
    for (std::vector<unsigned int>::const_iterator It = Members->begin(); It != Members->end(); ++It)
    {
        if (HasClassId(&*It))
            return true;
    }
    return false;
}

// FUN_06f57640 (06f57640-06f576b5; added by CNT-GCS4 for the GCS combat conversation
// 06ee2ebb / 06ee353c / 06ee3b9f / 06ee403f / 06ee4b17): a wingman (+0x14c != 0 in a
// formation) gets the first member of its formation (FUN_06f66220 into a local vector,
// begin[0] read without a size test, the buffer freed by the unwind 06faf778); anyone
// else is its own leader.
FContentShipBase* FContentShipBase::GetFormationLeader()
{
    if (FormationSlot == 0 || FormationId == 0xffffffffu)
        return this;
    std::vector<FContentShipBase*> Members;
    CollectFormationMembers(&Members, FormationId);
    return Members.front();
}

// FUN_06f576c0 (06f576c0-06f576de): +0x154 = arg1, +0x14c = arg2, +0x150 = arg3.
void FContentShipBase::SetFormation(unsigned int InFormationId, unsigned int InFormationSlot,
                                    FFormation* InFormation)
{
    FormationId = InFormationId;
    FormationSlot = InFormationSlot;
    Formation = InFormation;
}

// FUN_06f576f0 (06f576f0-06f57708).
void FContentShipBase::SetField120(unsigned int const* Value120, unsigned int const* Value124)
{
    TradeLaneEntryRing = *Value120;
    TradeLaneEndRing = *Value124;
}

// FUN_06f57710 (06f57710-06f5771d): +0x120 != 0.
bool FContentShipBase::HasField120() const
{
    return TradeLaneEntryRing != 0;
}

// FUN_06f57720 (06f57720-06f57738).
void FContentShipBase::GetField120(unsigned int* Out120, unsigned int* Out124) const
{
    *Out120 = TradeLaneEntryRing;
    *Out124 = TradeLaneEndRing;
}

// FUN_06f57740 (06f57740-06f577cc): SubmitDirective(+4, DirectiveCancelOp) (result
// ignored); Release every queued directive (no null check); erase every node
// (FUN_06ecfe80); CurrentDirective = end().
void FContentShipBase::CancelDirectives()
{
    pub::AI::DirectiveCancelOp Op;
    pub::AI::SubmitDirective(SpaceObjId, &Op);
    std::list<FObjListDirective*>& Items = Directives.Items;
    for (std::list<FObjListDirective*>::iterator It = Items.begin(); It != Items.end(); ++It)
        (*It)->Release();
    std::list<FObjListDirective*>::iterator Node = Items.begin();
    while (Node != Items.end())
        Node = Items.erase(Node);
    CurrentDirective = Items.end();
}

// FUN_06f577d0 (06f577d0-06f57815): node inserted before the head (push_back), size++,
// then Directive->AddRef() (no null check).
void FContentShipBase::AddDirective(FObjListDirective* Directive)
{
    Directives.Items.push_back(Directive);
    Directive->AddRef();
}

// FUN_06f57840 (06f57840-06f579bd). +0x168 = Priority (no set_directive_priority);
// SubmitDirective(+4, DirectiveCancelOp); Release every queued directive (no null check,
// postfix ++ FUN_06f68e30) and clear (FUN_06ecfec0); CurrentDirective = end(); a second
// release + clear pass over the now empty list (06f578c3-06f578f5, kept as in retail);
// list assignment from NewList unless it is this list (06f578fe); AddRef every directive
// (slot 0); CurrentDirective = end(); AdvanceDirective().
void FContentShipBase::SetDirectives(std::list<FObjListDirective*> const* NewList, int InPriority)
{
    Priority = InPriority;
    pub::AI::DirectiveCancelOp Op;
    pub::AI::SubmitDirective(SpaceObjId, &Op);

    std::list<FObjListDirective*>& Items = Directives.Items;
    for (std::list<FObjListDirective*>::iterator It = Items.begin(); It != Items.end(); ++It)
        (*It)->Release();
    Items.clear();
    CurrentDirective = Items.end();
    for (std::list<FObjListDirective*>::iterator It = Items.begin(); It != Items.end(); ++It)
        (*It)->Release();
    Items.clear();

    if (NewList != &Items)
        Items = *NewList;

    for (std::list<FObjListDirective*>::iterator It = Items.begin(); It != Items.end(); ++It)
        (*It)->AddRef();
    CurrentDirective = Items.end();
    AdvanceDirective();
}

// FUN_06f57b00 (06f57b00-06f57bf8). Next is a ref-holding local (retail EBX / [esp+0xc],
// EH state 0 of FuncInfo LAB_06faf798; released on every exit at 06f57bd4 and by the
// unwind step on the exception path):
//   current != end: Release its directive (if any), erase the node (FUN_06ecfe80), current =
//     the next node; end or null directive -> done; else Next = it, AddRef.
//   current == end: empty list (+0x160 == 0) -> done; current = begin(); null directive ->
//     done; else Next = it, AddRef.
//   Then: ActiveDirective released and cleared if set; ActiveDirective = Next, AddRef;
//   Next->Execute(this) (slot 3) false -> AdvanceDirective() again.
void FContentShipBase::AdvanceDirective()
{
    TRefHandle<FObjListDirective> Next;
    std::list<FObjListDirective*>& Items = Directives.Items;
    if (CurrentDirective != Items.end())
    {
        if (*CurrentDirective != nullptr)
            (*CurrentDirective)->Release();
        CurrentDirective = Items.erase(CurrentDirective);
        if (CurrentDirective == Items.end() || *CurrentDirective == nullptr)
            return;
        Next = *CurrentDirective;
        Next->AddRef();
    }
    else
    {
        if (Items.empty())
            return;
        CurrentDirective = Items.begin();
        if (*CurrentDirective == nullptr)
            return;
        Next = *CurrentDirective;
        Next->AddRef();
    }

    if (ActiveDirective != nullptr)
    {
        ActiveDirective->Release();
        ActiveDirective = nullptr;
    }
    ActiveDirective = Next;
    Next->AddRef();
    if (!Next->Execute(this))
        AdvanceDirective();
}

// FUN_06f57c00 (06f57c00-06f57c0c): current = end(); JMP FUN_06f57b00.
void FContentShipBase::RestartDirectives()
{
    CurrentDirective = Directives.Items.end();
    AdvanceDirective();
}

// FUN_06f579c0 (06f579c0-06f57af1, RET 4). SubmitDirective(+4, DirectiveCancelOp); the list
// released and cleared (FUN_06ed2a00); CurrentDirective = end(); a second release + clear pass
// over the empty list (FUN_06ecfec0); list = Leader's (FUN_06f93490), every directive
// AddRef'ed. A non-empty list whose first directive is type 0x13 (slot +0x14):
// GetShipDockingTarget(&own id, &directive +0x118, &index, null) true -> +0x120 = 1, +0x124 =
// index -- the directive object is shared with the leader. CurrentDirective = end();
// AdvanceDirective (FUN_06f57b00).
// Type 0x13 is answered by two classes (LAB_06f250a0 sits in both the Dock vtable 06fb8ff0 and
// the InstantTradelane vtable 06fb8fd4, factory cases 0x13 / 0x14), and retail writes the raw
// offsets: for a Dock directive they are Op.DockObjectId / Field_14 / DockPortIndex; for an
// InstantTradelane one +0x118 is Op.TradeLaneId, +0x120 the low byte of Op.Field_14 and +0x124
// lies past the 0x124-byte object (a heap write the port does not reproduce).
void FContentShipBase::FollowDirectivesOf(FContentShipBase* Leader)
{
    pub::AI::DirectiveCancelOp Op;
    pub::AI::SubmitDirective(SpaceObjId, &Op);

    std::list<FObjListDirective*>& Items = Directives.Items;
    for (std::list<FObjListDirective*>::iterator It = Items.begin(); It != Items.end(); ++It)
        (*It)->Release();
    Items.clear();                                                            // FUN_06ed2a00
    CurrentDirective = Items.end();
    for (std::list<FObjListDirective*>::iterator It = Items.begin(); It != Items.end(); ++It)
        (*It)->Release();
    Items.clear();                                                            // FUN_06ecfec0

    Items = Leader->Directives.Items;                                         // FUN_06f93490
    for (std::list<FObjListDirective*>::iterator It = Items.begin(); It != Items.end(); ++It)
        (*It)->AddRef();

    if (!Items.empty() && Items.front()->GetType() == 0x13)
    {
        unsigned int OwnId = SpaceObjId;
        int DockIndex = 0;
        FObjListDock* Dock = dynamic_cast<FObjListDock*>(Items.front());
        if (Dock != nullptr)
        {
            if (GetShipDockingTarget(&OwnId, &Dock->Op.DockObjectId, &DockIndex, nullptr))
            {
                Dock->Op.Field_14 = 1;
                Dock->Op.DockPortIndex = DockIndex;
            }
        }
        else
        {
            FObjListInstantTradelane* Lane =
                static_cast<FObjListInstantTradelane*>(Items.front());
            if (GetShipDockingTarget(&OwnId, &Lane->Op.TradeLaneId, &DockIndex, nullptr))
                reinterpret_cast<unsigned char*>(&Lane->Op.Field_14)[0] = 1;
        }
    }

    CurrentDirective = Items.end();
    AdvanceDirective();
}

// FUN_06f14400 (06f14400-06f14502, RET 4): the out list is built empty, assigned from +0x158
// (FUN_06fa8370 / FUN_06ed0230 element-wise copy), then every element AddRef'ed (slot 0).
std::list<FObjListDirective*> FContentShipBase::CopyDirectives() const
{
    std::list<FObjListDirective*> Copy = Directives.Items;
    for (std::list<FObjListDirective*>::iterator It = Copy.begin(); It != Copy.end(); ++It)
        (*It)->AddRef();
    return Copy;
}

// FUN_06f57c20 (06f57c20-06f57c2d): +0x160 == 0.
bool FContentShipBase::HasNoDirectives() const
{
    return Directives.Items.empty();
}

// FUN_06f57c60 (06f57c60-06f57c78): `DEC EAX; NEG EAX; SBB EAX,EAX; INC EAX` = (r == 1).
bool FContentShipBase::SubmitDirective(pub::AI::BaseOp* Op)
{
    return pub::AI::SubmitDirective(SpaceObjId, Op) == 1;
}

// FUN_06f57c80 (06f57c80-06f57c86).
int FContentShipBase::GetPriority() const
{
    return Priority;
}

// FUN_06f57c90 (06f57c90-06f57cac): +0x168 = *p; set_directive_priority(+4, *p).
void FContentShipBase::SetPriority(int const* InPriority)
{
    Priority = *InPriority;
    pub::AI::set_directive_priority(SpaceObjId, static_cast<pub::AI::DirectivePriority>(*InPriority));
}
