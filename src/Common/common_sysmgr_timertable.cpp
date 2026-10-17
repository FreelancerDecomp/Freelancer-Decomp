// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- SystemManagerTimerTable (common_systemmanager.h), the AI target list / threat
// timer table at SystemManager+0x34 (retail run 0x062e6af0-0x062e8970).
//
// CMN-SPLIT stage 1 (2026-10-14): the byte getters FUN_062e8810..8860 and the checked
// object lookup FUN_062e74a0, until now `extern "C"` free functions over a raw `int` / `void**`
// receiver in common_stubs.cpp / common_behavior.cpp, are members here; every caller passes
// `&Sys->Timers` (SystemManager+0x34). The missile-reaction tests moved here from
// common_behavior.cpp unchanged. CMN-SPLIT phase B (2026-10-14): the table's remaining
// functions (construct / destroy, the watcher list, Arm, the personality thresholds, the
// candidate scan and the per-tick update) moved here from common_systemmanager.cpp unchanged,
// below the getters.

#include "common_systemmanager.h"
#include "common_behavior.h" // TimerTableMissileWithinReaction00/04/08 declarations
#include "common_iface.h"   // IObjRW, IObjInspect::get_id
#include "common_cobjdb.h"  // IObjDB::FindById
#include "common_aisteer.h" // SustainedThresholdTimer
#include "common_misc.h"
#include "common_cobject.h"
#include "dacom.h"

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <new>

// The six SustainedThresholdTimer Active bytes (Timers at +0x28, stride 0x10). Each retail
// body is `MOV AL,byte ptr [ECX+disp]; RET`.

// formerly FUN_062e8810 (.text:062e8810, [ECX+0x28]); was in common_behavior.cpp.
bool SystemManagerTimerTable::IsTimer0Active() const
{
    return Timers[0].Active != 0;
}

// formerly FUN_062e8820 (Common.dll.c 0x62e8820, [ECX+0x38]).
bool SystemManagerTimerTable::IsTimer1Active() const
{
    return Timers[1].Active != 0;
}

// formerly FUN_062e8860 (.text:062e8860, [ECX+0x48]); was in common_behavior.cpp.
bool SystemManagerTimerTable::IsTimer2Active() const
{
    return Timers[2].Active != 0;
}

// formerly FUN_062e8830 (Common.dll.c 0x62e8830, [ECX+0x58]).
bool SystemManagerTimerTable::IsTimer3Active() const
{
    return Timers[3].Active != 0;
}

// formerly FUN_062e8850 (Common.dll.txt:245558-245563, item 1813, [ECX+0x68]; callers
// 062c677b, 062d6395, 062d832d).
bool SystemManagerTimerTable::IsTimer4Active() const
{
    return Timers[4].Active != 0;
}

// formerly FUN_062e8840 (Common.dll.c 0x62e8840, [ECX+0x78]).
bool SystemManagerTimerTable::IsTimer5Active() const
{
    return Timers[5].Active != 0;
}

// formerly FUN_062e74a0 (item 1798; Common.dll.c 117753-117769 / Common.dll.txt
// 243726-243762, 27 insns, ECX = the table, EDX unused, one stack argument). Guard order as
// retail, all four failures falling into the shared zero return LAB_062e74cf: `*Id == 0`
// first (243738-243739), then ObjectDb (+0x00) == 0 (243741-243742), then IObjDB::FindById
// (slot +0x04) == 0 (243747-243748), then the found object's IObjInspect::get_id() (+0x20)
// != *Id (243752-243753). The old comment called both receivers "unidentified"; +0x00 is
// this table's ObjectDb and FindById returns an IObjRW (IObjInspect at offset 0), so the two
// raw vtable dispatches are the typed calls below. 18 callers in the behaviours.
IObjRW* SystemManagerTimerTable::FindVerifiedObject(unsigned const* Id) const
{
    if (*Id == 0)
        return nullptr;

    IObjDB* const Db = ObjectDb;
    if (Db == nullptr)
        return nullptr;

    IObjRW* const Found = Db->FindById(Id);
    if (Found == nullptr)
        return nullptr;

    if (Found->get_id() != *Id)
        return nullptr;

    return Found;
}


// FUN_062e8910 / 062e8940 / 062e8970 -- see common_behavior.h (moved here from
// common_behavior.cpp, CMN-SPLIT A2). Each: `FLD [ECX+0xf4];
// FCOMP 0.0f; TEST AH,0x41; JNZ false` (needs Best > 0), then `MOV EAX,[ECX+4]; FLD
// [ECX+0xf4]; FCOMP [EAX+disp]; TEST AH,0x41; JP false` (PF=1 for AH&0x41 in {0x00
// greater, 0x41 unordered}, so true on less or equal).
bool TimerTableMissileWithinReaction00(SystemManagerTimerTable const* Self)
{
    float const Best = Self->BestValueSoFar;
    if (!(Best > 0.0f))
        return false;
    return Best <= Self->Owner->PersonalityData.MissileReaction.EvadeBreakMissileReactionTime;   // +0x17b4
}

bool TimerTableMissileWithinReaction04(SystemManagerTimerTable const* Self)
{
    float const Best = Self->BestValueSoFar;
    if (!(Best > 0.0f))
        return false;
    return Best <= Self->Owner->PersonalityData.MissileReaction.EvadeSlideMissileReactionTime;   // +0x17b8
}

bool TimerTableMissileWithinReaction08(SystemManagerTimerTable const* Self)
{
    float const Best = Self->BestValueSoFar;
    if (!(Best > 0.0f))
        return false;
    return Best <= Self->Owner->PersonalityData.MissileReaction.EvadeAfterburnMissileReactionTime;   // +0x17bc
}

// ============================================================================
// SystemManagerTimerTable — Common.dll.c 0x062e6af0 (FUN_062e6af0), item 1792.
// ============================================================================
void ConstructSystemManagerTimerTable(SystemManagerTimerTable* Self)
{
    std::memset(Self, 0, sizeof(*Self));

    SustainedThresholdTimer* const Timers = Self->Timers;
    for (int i = 0; i < 11; ++i)
    {
        Timers[i].Active = 0;
        Timers[i].TriggerThreshold = 100.0f;
        Timers[i].Duration = 1.0f;
        Timers[i].Accumulator = 0.0f;
    }

    Self->UnknownScale_0xD8 = 0.5f;

    // Retail: operator_new(0x10) then Node[0]=Node; Node[4]=Node; -- an empty,
    // self-referential 2-pointer circular-list sentinel head.
    void** Node = static_cast<void**>(::operator new(0x10));
    Node[0] = Node;
    Node[1] = Node;
    Self->ListHeadNode = Node;
    Self->ListCount = 0;

    Self->BestValueSoFar = 3.402823466e38f; // 0x7f7fffff == FLT_MAX
}

namespace
{
    // FUN_062d4c90, applied by IBehaviorManager's dtor to Timers+0xDC (.text:062d2631-062d263c)
    // -- a std::list whose node is {Next, Prev, BaseWatcher}: every node is unlinked, its
    // watcher destroyed (BaseWatcher::~BaseWatcher at .text:062d4cb6), the node freed and the
    // count decremented; then the head sentinel is freed and head/count are zeroed.
    void DestroySystemManagerTimerList(SystemManagerTimerTable* Self)
    {
        void** const Head = static_cast<void**>(Self->ListHeadNode);
        void** Node = static_cast<void**>(Head[0]);
        while (Node != Head)
        {
            void** const Next = static_cast<void**>(Node[0]);
            void** const Prev = static_cast<void**>(Node[1]);
            Prev[0] = Next;                                  // .text:062d4ca9
            Next[1] = Prev;                                  // .text:062d4cb3
            reinterpret_cast<BaseWatcher*>(Node + 2)->~BaseWatcher();
            ::operator delete(Node);
            --Self->ListCount;
            Node = Next;
        }
        ::operator delete(Head);                             // .text:062d4cd4
        Self->ListHeadNode = nullptr;
        Self->ListCount = 0;
    }
}

// FUN_062d2700 (item 1544). Common.dll.txt 062d2700-062d27ad: +0xE8 watcher set_pointer(0)
// when set; FUN_062d4d20 on every list node, the sentinel freed, +0xE0 / +0xE4 zeroed;
// the Targets buffer (+0x10) freed and +0x10 / +0x14 / +0x18 zeroed.
SystemManagerTimerTable::~SystemManagerTimerTable()
{
    reinterpret_cast<BaseWatcher*>(BestCandidateWatcherBytes)->set(nullptr);
    DestroySystemManagerTimerList(this);
    ::operator delete(Targets.Begin);
    Targets.Begin = nullptr;
    Targets.End = nullptr;
    Targets.Cap = nullptr;
}

// ============================================================================
// 2026-09-11 d250:common-port -- revisit pass over the 0x062e6260-0x062ea450
// blocked-item cluster now that SystemManager/SystemManagerTimerTable/
// SystemManagerGunGroupManager all have real layouts (item 1779's follow-up).
// Each function below was independently cross-checked against the raw
// disassembly (Common.dll.txt), not just the Ghidra .c, per this pass's
// instructions -- see each comment for the specific evidence.
// ============================================================================

namespace
{
    // Doubly-linked circular list node used by AppendSystemManagerTimerListEntry:
    // {next, prev, embedded BaseWatcher payload} -- 0x10 bytes total, matching
    // both the sentinel's own operator_new(0x10) in ConstructSystemManagerTimerTable
    // and this function's own operator_new(0x10) (Common.dll.txt:245432-245434).
    struct SystemManagerTimerListNode
    {
        SystemManagerTimerListNode* Next;  // +0x00
        SystemManagerTimerListNode* Prev;  // +0x04
        unsigned char Watcher[8];          // +0x08, BaseWatcher-sized payload
    };
}

// FUN_062e8720, item 1808. Raw disasm Common.dll.txt 245394-245473, thiscall
// (`this` = SystemManager+0x34), `RET 4`. Sole caller: IBehaviorManager::Slot00
// case 6 (.text:062d4287-062d4291), with the event's Data -- the {OwnerId,
// missile id} id block Server's StarSystem::create_guided sends
// (Server.dll .text:06d09986-06d099ae). Data is NOT an object: the missile is
// looked up by id through the IObjDB this table holds at +0x00.
// Retail wraps this in a manual SEH frame (state 0 -> 1 -> 2 -> -1) purely so
// the scratch BaseWatcher is torn down on an exception from operator new or
// the node's watcher ctor; C++ RAII on Scratch gives the same guarantee.
void AppendSystemManagerTimerListEntry(SystemManagerTimerTable* Self, void* Source)
{
    // .text:062e872e-062e8741: param tested for 0 -> return.
    if (Source == nullptr)
        return;

    // .text:062e8747-062e874f: `MOV ECX,[EBX]; MOV EDX,[ECX]; ADD EAX,4;
    // PUSH EAX; CALL [EDX+4]` -- IObjDB::FindById(&Data[1]) on the IObjDB stored
    // at Timers+0x00 by SetSystemManagerSharedListHead (FUN_062e68e0). Not
    // null-tested, as in retail.
    unsigned const* const IdBlock = static_cast<unsigned const*>(Source);
    IObjRW* const Candidate = Self->ObjectDb->FindById(IdBlock + 1);

    // .text:062e8752-062e877a: the scratch watcher (zeroed inline), then
    // set_pointer(Candidate+8) when Candidate and Candidate+8 are both non-0.
    BaseWatcher Scratch;
    if (Candidate != nullptr)
    {
        Watchable* const Target =
            reinterpret_cast<Watchable*>(reinterpret_cast<unsigned char*>(Candidate) + 8);
        if (Target != nullptr)
            Scratch.set(Target);
    }

    // .text:062e8781-062e87ad: std::list _Buynode(Head, Head->Prev) -- a null
    // Prev means the new node itself (.text:062e879c-062e87a2); then
    // Head->Prev = node, node->Prev->Next = node.
    SystemManagerTimerListNode* const Head = static_cast<SystemManagerTimerListNode*>(Self->ListHeadNode);
    SystemManagerTimerListNode* Prev = Head->Prev;

    SystemManagerTimerListNode* const NewNode =
        static_cast<SystemManagerTimerListNode*>(::operator new(sizeof(SystemManagerTimerListNode)));
    NewNode->Next = Head;
    if (Prev == nullptr)
        Prev = NewNode;
    NewNode->Prev = Prev;
    Head->Prev = NewNode;
    NewNode->Prev->Next = NewNode;

    // .text:062e87af-062e87ca: FUN_062ed070(node+8, &Scratch) -- the watcher
    // copy-ctor overload, reading Scratch's first dword (its watched pointer).
    Watchable* ScratchTarget = const_cast<Watchable*>(Scratch.target());
    new (NewNode->Watcher) BaseWatcher(&ScratchTarget);

    ++Self->ListCount;                                       // .text:062e87cf-062e87dc

    // .text:062e87d5-062e87f2: Scratch released (set_pointer(0) when watching).
    Scratch.set(nullptr);
}

// FUN_062e86d0, item 1807. Common.dll.c 118521-118534 / raw disasm
// Common.dll.txt (adjacent to 1808's listing). "this" = &Timers (Owner is
// Timers' own back-pointer). Candidate is an unidentified per-object record
// accessed via raw offsets, consistent with this cluster's established
// "smart-handle-minus-8 / GetId-at-vtable+0x20" idiom.
void AccumulateSystemManagerTimerLevel(SystemManagerTimerTable* Self, void* Candidate)
{
    // .text:062e86d1-062e86da: only the param is tested. Owner ([EDI+4]) and
    // its +0x1580 ExternalIface are dereferenced blind (.text:062e86dc-062e86ee).
    if (Candidate == nullptr)
        return;

    int const CurrentId = static_cast<int>(Self->Owner->SelfTarget.Iface->get_id());

    int CandidateId;
    std::memcpy(&CandidateId, static_cast<unsigned char*>(Candidate) + 0x08, sizeof(int));
    int CandidateState;
    std::memcpy(&CandidateState, static_cast<unsigned char*>(Candidate) + 0x14, sizeof(int));

    if (CandidateId == CurrentId && 1 < CandidateState && CandidateState < 8)
    {
        float Value;
        std::memcpy(&Value, static_cast<unsigned char*>(Candidate) + 0x18, sizeof(float));
        Self->Level += Value;
    }
}

// FUN_062e8870, item 1815. Common.dll.txt 062e8870-062e8903. Before each of the three
// compares retail re-tests [this+0xf4] against 0.0f (DAT_06399408): FCOMP; TEST AH,0x41;
// JNZ -> not greater (less, equal or unordered) -> false. Each compare is
// `FLD [this+0xf4]; FCOMP [Owner+0x17b4/8/c]; TEST AH,0x41`: PF=0 for AH&0x41 in
// {0x01 less, 0x40 equal}, so JNP (1st/2nd) returns true and JP (3rd) returns false
// exactly when Best <= Field is false (greater or unordered). Owner ([this+4]) is read
// without a check. The repeated > 0 tests always give the same answer, so one suffices.
bool SystemManagerTimerTableHasMissileReactionTarget(SystemManagerTimerTable const* Self)
{
    float const Best = Self->BestValueSoFar;
    if (!(Best > 0.0f))
        return false;

    pub::AI::Personality::MissileReactionStruct const& Reaction = Self->Owner->PersonalityData.MissileReaction;
    return Best <= Reaction.EvadeBreakMissileReactionTime || Best <= Reaction.EvadeSlideMissileReactionTime ||
           Best <= Reaction.EvadeAfterburnMissileReactionTime;
}

// FUN_062e6c60, item 1793. Raw disasm Common.dll.txt 242934-243021 (74 insns), `RET 4`.
// "this" = &Timers -- item 1785's own call site is `PUSH ESI; LEA ECX,[ESI+0x34];
// CALL FUN_062e6c60` (.text:062e64cf-062e64d5), ESI == the SystemManager.
//
// Stack after `SUB ESP,0xc; PUSH ESI`: three float locals A=[ESP+4], B=[ESP+8], C=[ESP+0xc],
// and the incoming argument slot [ESP+0x14] reused as a bool.
//   .text:062e6c67-062e6c82  Owner == 0, Owner->Watcher target == 0 or == 8  -> return false
//   .text:062e6c88           Self->Owner = Owner
//   .text:062e6ca6           Obj->find_equipment(&ScannerEquipId, 1, 0x80000) != 0 -> false
//   .text:062e6cb0-062e6cd7  A = 0; C = 0; Obj->get_status(A, C)        (slot 0x3C)
//   .text:062e6ce0-062e6d0b  B = 0; flag = 0; Obj->get_shield_status(A, B, flag)  (slot 0x44)
//   .text:062e6d0e-062e6d20  DecayRate = B + C; bBuilt = 0; bKeepCache = 0; return true
// So DecayRate is MaxShield + MaxHitPts. The previous port skipped the slot-0x44 call and
// summed get_status's two out-params (HitPts + MaxHitPts).
bool ArmSystemManagerTimerTable(SystemManagerTimerTable* Self, SystemManager* Owner)
{
    if (Owner == nullptr)
        return false;

    Watchable const* const WatcherTarget = Owner->Watcher.target();
    if (WatcherTarget == nullptr || reinterpret_cast<std::uintptr_t>(WatcherTarget) == 8)
        return false;

    Self->Owner = Owner;

    // .text:062e6c8b-062e6c99 re-reads the watcher; the -8 is the Watchable->IObjRW downcast.
    IObjRW* const Object = ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()));
    // Timers-relative +0x08 -- the scanner equipment id the AI target list rebuild is gated
    // on. See SystemManagerTimerTable's MERGE NOTE.
    if (static_cast<IObjInspect*>(Object)->find_equipment(&Self->ScannerEquipId, 1, 0x80000) != 0)
        return false;

    float HitPts = 0.0f;
    float MaxHitPts = 0.0f;
    (void)static_cast<IObjInspect*>(Object)->get_status(HitPts, MaxHitPts);

    float MaxShield = 0.0f;
    bool bShieldActive = false;
    (void)static_cast<IObjInspect*>(Object)->get_shield_status(HitPts, MaxShield, bShieldActive);

    Self->bBuilt = 0;                       // Timers-relative +0x1C
    Self->bKeepCache = 0;                   // Timers-relative +0x1D
    Self->DecayRate = MaxShield + MaxHitPts; // Timers-relative +0x20

    return true;
}

// ============================================================================
// FUN_062e72d0, item 1796 -- "rebuild the timer thresholds from Personality".
// `this` == &SystemManager::Timers, NOT the ThrustManager: the very first
// instruction is `MOV EAX,[ECX+4]` followed by a read at EAX+0x1770, and
// SystemManagerTimerTable::Owner is the only SystemManager* at +0x04 in this
// cluster (ThrustManager+0x04 is a float). See common_systemmanager.h.
//
// Every one of the eleven slots is written as
//     Timers[i].TriggerThreshold = <personality float> * DecayRate
//     Timers[i].Duration         = <personality float or 1.0f>
// and the slot index follows directly from the write offset: slot i's base is
// Timers-relative 0x28 + 0x10*i, TriggerThreshold at +0x04 and Duration at
// +0x08, so the writes at 0x2c/0x30, 0x3c/0x40, ... 0xcc/0xd0 enumerate slots
// 0..10 in order with no gaps.
//
// x87 note for the eight two-source slots: retail emits
//     FLD <high>          ; st0 = high
//     FLD <low>           ; st0 = low, st1 = high
//     FMUL [ECX+0x20]     ; st0 = low * DecayRate
//     FSTP TriggerThreshold ; pops the scaled LOW word
//     FSTP Duration         ; pops the UNSCALED HIGH word
// so the SECOND-loaded (numerically lower) Personality field is the scaled
// threshold and the first-loaded one goes to Duration unscaled. Slots 0-2 use
// the single-source form and take their Duration from the immediate
// 0x3f800000 (1.0f) staged in EAX at .text:062e72e1.
// ============================================================================
bool RefreshSystemManagerTimerThresholdsFromPersonality(SystemManagerTimerTable* Self)
{
    SystemManager* const Sys = Self->Owner;
    if (Sys == nullptr)
        return true;                       // .text:062e72d5 JZ -> `MOV AL,1; RET`

    pub::AI::Personality const& P = Sys->PersonalityData;
    pub::AI::Personality::DamageReactionStruct const& D = P.DamageReaction;
    pub::AI::Personality::FormationUseStruct const& F = P.FormationParams;

    float const Scale = Self->DecayRate;   // [ECX+0x20]

    SustainedThresholdTimer* const Timers = Self->Timers;

    // Slot 0 -- .text:062e72db..062e72ec   (Personality +0x1770)
    Timers[0].TriggerThreshold = D.EvadeBreakDamageTriggerPercent * Scale;
    Timers[0].Duration = 1.0f;

    // Slot 1 -- .text:062e72ef..062e72fe   (Personality +0x1774)
    Timers[1].TriggerThreshold = D.EvadeDodgeMoreDamageTriggerPercent * Scale;
    Timers[1].Duration = 1.0f;

    // Slot 2 -- .text:062e7301..062e7310   (Personality +0x1798)
    Timers[2].TriggerThreshold = D.BrakeReverseDamageTriggerPercent * Scale;
    Timers[2].Duration = 1.0f;

    // Slot 3 -- .text:062e7313..062e7328   (FLD +0x1784, FLD +0x1780)
    Timers[3].TriggerThreshold = D.EngineKillFaceDamageTriggerPercent * Scale;
    Timers[3].Duration = D.EngineKillFaceDamageTriggerTime;

    // Slot 4 -- .text:062e732b..062e7340   (FLD +0x1794, FLD +0x1790)
    Timers[4].TriggerThreshold = D.AfterburnerDamageTriggerPercent * Scale;
    Timers[4].Duration = D.AfterburnerDamageTriggerTime;

    // Slot 5 -- .text:062e7343..062e7358   (FLD +0x178c, FLD +0x1788)
    Timers[5].TriggerThreshold = D.RollDamageTriggerPercent * Scale;
    Timers[5].Duration = D.RollDamageTriggerTime;

    // Slot 6 -- .text:062e735e..062e7376   (FLD +0x17a8, FLD +0x17a4)
    Timers[6].TriggerThreshold = D.FireGunsDamageTriggerPercent * Scale;
    Timers[6].Duration = D.FireGunsDamageTriggerTime;

    // Slot 7 -- .text:062e737c..062e7394   (FLD +0x177c, FLD +0x1778)
    Timers[7].TriggerThreshold = D.DropMinesDamageTriggerPercent * Scale;
    Timers[7].Duration = D.DropMinesDamageTriggerTime;

    // Slot 8 -- .text:062e739a..062e73b2   (FLD +0x17a0, FLD +0x179c)
    Timers[8].TriggerThreshold = D.FireMissilesDamageTriggerPercent * Scale;
    Timers[8].Duration = D.FireMissilesDamageTriggerTime;

    // Slot 9 -- .text:062e73b8..062e73d0   (FLD +0x17d8, FLD +0x17d4)
    // 0x17d4 - 0x1584 (Personality) - 0x248 (FormationParams) == 0x08.
    Timers[9].TriggerThreshold = F.BreakFormationDamageTriggerPercent * Scale;
    Timers[9].Duration = F.BreakFormationDamageTriggerTime;

    // Slot 10 -- .text:062e73d6..062e73ee  (FLD +0x17e0, FLD +0x17dc)
    Timers[10].TriggerThreshold = F.BreakApartFormationDamageTriggerPercent * Scale;
    Timers[10].Duration = F.BreakApartFormationDamageTriggerTime;

    return true;                           // .text:062e73f4  MOV AL,1
}

// ============================================================================
// FUN_062e6d40, item 1794 -- the missile-reaction candidate scan.
// Raw disasm Common.dll.txt 243032-243272 (223 insns), `this` == &Timers.
//
// THE "x87 TAIL" THIS ITEM WAS BLOCKED ON IS FULLY RECOVERABLE. Simulated slot
// by slot from the raw bytes below. Stack frame first, since every operand is a
// stack slot: `SUB ESP,0x3c; PUSH EBX; PUSH EBP` puts ESP at entry_esp-0x44
// (call this M); the two extra `PUSH ESI; PUSH EDI` at .text:062e6d7f live only
// for the duration of the loop and are popped at .text:062e6f03/04, so the tail
// runs with ESP == M exactly. Named slots: M+0x0c == the winning candidate's
// distance (the loop's running minimum, seeded 0x7f7fffff at .text:062e6d61 and
// stored at .text:062e6e6f), M+0x10 == the dot product computed just below,
// M+0x14..M+0x1c == the OWNER's velocity (`Vector get_velocity()` returns by
// value, so its hidden out-param buffer is this slot), M+0x20..M+0x28 == the
// winning CANDIDATE's velocity (copied there at .text:062e6e7f-062e6e93),
// M+0x2c and M+0x38 == the two normalize scratch buffers.
//
//   062e6f22  CALL [vt+0x04]        ; OwnerVel = ExternalIface->get_velocity()   -> M+0x14
//   062e6f2f  VecNormalize3         ; M+0x38 = normalize(M+0x14)   (cdecl Out,Src)
//   062e6f3e  VecNormalize3         ; M+0x2c = normalize(M+0x20)
//   062e6f4d  ClampedDot3           ; ST0 = dot(M+0x38, M+0x2c)
//   062e6f52  FSTP [M+0x10]         ;   -> Dot
//   062e6f70  CALL [vt+0x04]        ; re-reads the SAME OwnerVel into M+0x14 --
//                                   ; redundant, not a different value; the
//                                   ; compiler simply did not CSE it across the
//                                   ; three intervening __cdecl calls. This is
//                                   ; the "overlapping stack regions" the prior
//                                   ; pass read as ambiguity; it is not.
//   062e6f73  FLD [M+0x24]; FMUL [M+0x24]      ; ST0 = Cy^2
//   062e6f7b  FLD [M+0x28]; FMUL [M+0x28]      ; ST0 = Cz^2, ST1 = Cy^2
//   062e6f83  FADDP                            ; ST0 = Cz^2+Cy^2
//   062e6f85  FLD [M+0x20]; FMUL [M+0x20]      ; ST0 = Cx^2, ST1 = ...
//   062e6f8d  FADDP                            ; ST0 = |C|^2
//   062e6f8f  FSQRT                            ; ST0 = |CandidateVel|
//   062e6f91  FLD [M+0x1c]; FMUL [M+0x1c]      ; ST0 = Oz^2, ST1 = |C|
//   062e6f99  FLD [M+0x18]; FMUL [M+0x18]      ; ST0 = Oy^2, ST1 = Oz^2, ST2 = |C|
//   062e6fa1  FADDP                            ; ST0 = Oy^2+Oz^2, ST1 = |C|
//   062e6fa3  FLD [M+0x14]; FMUL [M+0x14]      ; ST0 = Ox^2, ...
//   062e6fab  FADDP                            ; ST0 = |O|^2, ST1 = |C|
//   062e6fad  FSQRT                            ; ST0 = |OwnerVel|, ST1 = |CandidateVel|
//   062e6faf  FXCH / 062e6fb1 FXCH             ; two FXCHs == a no-op pair; stack
//                                              ; is back to ST0=|O|, ST1=|C|
//   062e6fb3  FMUL [M+0x10]                    ; ST0 = |O| * Dot
//   062e6fb7  FSUBP                            ; ST(1) -= ST(0), pop
//                                              ;   => ST0 = |C| - |O|*Dot
//   062e6fb9  FDIVR [M+0x0c]                   ; ST0 = BestDistance / that
//   062e6fbd  FCOM [EBX+0xf4] / TEST AH,5 / JP ; continue only if ST0 < BestValueSoFar
//   062e6fca  FCOM 0.0f       / TEST AH,0x41 / JNZ ; continue only if ST0 > 0
//   062e6fd8  FSTP [EBX+0xf4]                  ; commit
//
// The two `TEST AH,n` branch idioms decoded (AH from FNSTSW: C0=0x01, C2=0x04,
// C3=0x40):
//   TEST AH,0x05 + JP  -- jumps on even parity of {C0,C2}: 0 bits (ST0>mem) or
//                         2 bits (unordered). One bit (C0 alone) means ST0<mem.
//                         Equality sets only C3, which is masked out, so it also
//                         jumps. Net: fall through iff ST0 < mem, strictly.
//   TEST AH,0x41 + JNZ -- jumps iff C3 or C0 set, i.e. ST0 <= mem. Net: fall
//                         through iff ST0 > mem, strictly.
// So the result is committed only when it is strictly less than the previous
// BestValueSoFar AND strictly greater than zero -- which also makes the
// FLT_MAX seeding meaningful. Nothing here is guessed.
//
// The whole expression is a closing-rate estimate: distance divided by
// (|target velocity| - |own velocity| * cos(angle between the two velocity
// directions)), which is why SystemManagerTimerTableHasMissileReactionTarget
// (item 1815, already ported) compares BestValueSoFar against three
// MissileReaction time thresholds.
// ============================================================================
void UpdateSystemManagerCandidateScan(SystemManagerTimerTable* Self, float /*UnusedArg*/)
{
    BaseWatcher* const BestWatcher =
        reinterpret_cast<BaseWatcher*>(Self->BestCandidateWatcherBytes);

    BestWatcher->set(nullptr);                       // .text:062e6d4f

    float const kNoCandidate = 3.402823466e38f;      // 0x7f7fffff == FLT_MAX

    // .text:062e6d54-062e6d69 tests only ListCount; Owner ([EBX+4]) is
    // dereferenced unguarded inside the loop (.text:062e6da7).
    if (Self->ListCount == 0)
    {
        Self->BestValueSoFar = kNoCandidate;         // LAB_062e6fe5
        return;
    }

    SystemManagerTimerListNode* const Head =
        static_cast<SystemManagerTimerListNode*>(Self->ListHeadNode);
    SystemManagerTimerListNode* Cur = Head->Next;
    if (Cur == Head)
    {
        Self->BestValueSoFar = kNoCandidate;
        return;
    }

    bool bFound = false;                             // retail's local_39
    float BestDistance = kNoCandidate;               // retail's local_38 (M+0x0c)
    Vector BestCandidateVelocity = { 0.0f, 0.0f, 0.0f };  // M+0x20..M+0x28

    while (true)
    {
        BaseWatcher* const NodeWatcher = reinterpret_cast<BaseWatcher*>(Cur->Watcher);
        Watchable const* const Watched = NodeWatcher->target();

        // .text:062e6d84/062e6d92: null OR the literal 8 sentinel (`EAX-8 == 0`)
        // means this node's target is gone -- unlink, destroy, free, and keep
        // walking. This is the list's garbage collector.
        if (Watched == nullptr || reinterpret_cast<std::uintptr_t>(Watched) == 8)
        {
            SystemManagerTimerListNode* const Next = Cur->Next;   // LAB_062e6eb7
            SystemManagerTimerListNode* const Prev = Cur->Prev;
            Prev->Next = Next;                       // 062e6ebd
            Next->Prev = Prev;                       // 062e6ec7
            NodeWatcher->~BaseWatcher();             // 062e6eca FUN_062b57e0 with flag 0
            // .text:062e6ed0 FUN_06391d5a: Common's statically linked
            // operator delete (`PUSH [ESP+4]; CALL free; POP ECX; RET`), the
            // pair of the operator new in AppendSystemManagerTimerListEntry.
            ::operator delete(Cur);
            --Self->ListCount;
            Cur = Next;
        }
        else
        {
            IObjRW* const Candidate =
                ObjRwFromWatchable(const_cast<Watchable*>(Watched));

            SystemManager* const Owner = Self->Owner;

            // .text:062e6daa-062e6dce: refresh the owner's cached physical
            // centre/radius once per tick, gated on Flags1578[2] (+0x157A) only;
            // ExternalIface (+0x1580) is not null-tested.
            if (Owner->SelfTarget.bPosValid == 0)
            {
                Owner->SelfTarget.bPosValid = 1;
                (void)Owner->SelfTarget.Iface->get_physical_radius(
                    Owner->SelfTarget.CachedRadius, Owner->SelfTarget.CachedPos);
            }

            // .text:062e6dd1-062e6dfa: Delta = CachedCentre - Candidate position.
            Vector const& CandidatePos = ObjInspectOf(Candidate)->get_position();
            Vector Delta;
            Vec3Assign(&Delta,
                Owner->SelfTarget.CachedPos.x - CandidatePos.x,
                Owner->SelfTarget.CachedPos.y - CandidatePos.y,
                Owner->SelfTarget.CachedPos.z - CandidatePos.z);

            // .text:062e6dff-062e6e2e: keep only candidates whose velocity has a
            // strictly positive component along Delta (closing on the owner).
            // The dot stays on the x87 stack: (Dz*Vz + Dy*Vy) + Dx*Vx, then `FCOMP 0.0f;
            // TEST AH,0x41; JNZ` skips on less, equal or unordered.
            Vector const CandidateVelocity = ObjInspectOf(Candidate)->get_velocity();
            double const Along = ((double)Delta.z * CandidateVelocity.z +
                                  (double)Delta.y * CandidateVelocity.y) +
                                 (double)Delta.x * CandidateVelocity.x;

            if (Along > 0.0)
            {
                // .text:062e6e34-062e6e53: (Dz^2 + Dy^2) + Dx^2, FSQRT, kept on the stack for
                // both compares (TEST AH,5 / JP: continue only on strictly less).
                double const Distance = std::sqrt(
                    ((double)Delta.z * Delta.z + (double)Delta.y * Delta.y) +
                    (double)Delta.x * Delta.x);

                // .text:062e6e55: Personality+0x23C == MissileReaction.EvadeMissileDistance
                // (0x17c0 - 0x1584 == 0x23C; MissileReaction starts at 0x230).
                float const MaxRange = Owner->PersonalityData.MissileReaction.EvadeMissileDistance;

                if (Distance < MaxRange && Distance < BestDistance)
                {
                    BestDistance = (float)Distance;               // .text:062e6e6f FSTP float
                    BestCandidateVelocity = ObjInspectOf(Candidate)->get_velocity();
                    BestWatcher->set(Watched);                    // .text:062e6e97-062e6ea1
                    bFound = true;                                // .text:062e6ea9
                }
            }

            Cur = Cur->Next;                                      // LAB_062e6ea6 / LAB_062e6eb2
        }

        if (Self->ListCount == 0 || Cur == Head)                  // LAB_062e6ee7
            break;
    }

    // .text:062e6efd-062e6f05: only local_39 (bFound) gates the tail; Owner and
    // its ExternalIface are dereferenced blind at .text:062e6f0b-062e6f13.
    if (!bFound)
    {
        Self->BestValueSoFar = kNoCandidate;
        return;
    }

    // ---- the x87 tail, decoded in this function's header comment ----
    Vector OwnerVelocity = Self->Owner->SelfTarget.Iface->get_velocity();

    Vector OwnerDirection;
    Vector CandidateDirection;
    (void)VecNormalize3(&OwnerVelocity, &OwnerDirection);
    (void)VecNormalize3(&BestCandidateVelocity, &CandidateDirection);
    float const Dot = ClampedDot3(OwnerDirection, CandidateDirection);

    // .text:062e6f56-062e6f70: retail calls get_velocity() a second time into the same slot.
    OwnerVelocity = Self->Owner->SelfTarget.Iface->get_velocity();

    // .text:062e6f73-062e6fb9: everything below stays on the x87 stack until the final FSTP.
    // |C|^2 = (Cy^2 + Cz^2) + Cx^2, |O|^2 = (Oz^2 + Oy^2) + Ox^2.
    double const CandidateSpeed = std::sqrt(
        ((double)BestCandidateVelocity.y * BestCandidateVelocity.y +
         (double)BestCandidateVelocity.z * BestCandidateVelocity.z) +
        (double)BestCandidateVelocity.x * BestCandidateVelocity.x);
    double const OwnerSpeed = std::sqrt(
        ((double)OwnerVelocity.z * OwnerVelocity.z +
         (double)OwnerVelocity.y * OwnerVelocity.y) +
        (double)OwnerVelocity.x * OwnerVelocity.x);

    double const ClosingRate = CandidateSpeed - OwnerSpeed * Dot;   // FMUL Dot; FSUBP
    double const Estimate = BestDistance / ClosingRate;             // FDIVR [BestDistance]

    // 062e6fbd `FCOM [+0xf4]; TEST AH,5; JP` -> continue only on strictly less;
    // 062e6fca `FCOM 0.0f; TEST AH,0x41; JNZ` -> continue only on strictly greater.
    if (Estimate < Self->BestValueSoFar && Estimate > 0.0)
        Self->BestValueSoFar = (float)Estimate;                     // 062e6fd8 FSTP float
}

// ============================================================================
// FUN_062e7010, item 1795. Raw disasm Common.dll.txt 243289-243552 (225 insns),
// `this` == &Timers, one float argument (DeltaTime) passed straight through to
// the candidate scan above.
//
// Slot bases follow directly from the write offsets: slot i sits at
// Timers-relative 0x28 + 0x10*i, so the eight inlined blocks at 0x28/0x38/0x48/
// 0x58/0x68/0x78/0x88/0x98 are slots 0-7 and the three explicit CALLs at
// 0xa8/0xb8/0xc8 are slots 8-10 -- eleven in all, exactly the array size
// ConstructSystemManagerTimerTable builds.
//
// Closing decay (.text:062e728e-062e72be), operand order read off the raw
// bytes: `FLD [+0xd8]; FMUL [+0x20]; FMUL DeltaTime; FSUBR [+0x24]` computes
// Level - UnknownScale_0xD8 * DecayRate * DeltaTime (FSUBR subtracts ST0 FROM
// the memory operand), stores it back with FST, then `FCOMP 0.0f; TEST AH,5;
// JP` -- which by the parity rule documented on the previous function falls
// through only when the new Level is strictly negative, at which point it is
// clamped to zero. The whole decay is skipped unless the OLD Level was
// strictly positive.
// ============================================================================
void UpdateSystemManagerTimerTable(SystemManagerTimerTable* Self, float DeltaTime)
{
    // .text:062e7016-062e7019: `PUSH EDI(DeltaTime); CALL FUN_062e6d40` with
    // ECX == this. The argument is unread by the callee (see its comment).
    UpdateSystemManagerCandidateScan(Self, DeltaTime);

    // FUN_062e6a20 (Common.dll.txt 242728-242758) applied eleven times over --
    // eight expanded in place (slots 0-7) and three as real CALLs (slots 8-10,
    // .text:062e7269/7279/7289) -- and the inlined form is instruction-for-
    // instruction identical to the out-of-line one, so one loop covers both.
    for (int Slot = 0; Slot < 11; ++Slot)
        (void)Self->Timers[Slot].Update(DeltaTime, Self->Level);

    // 062e728e-062e72be: the product and the difference stay on the x87 stack; FST stores the
    // float, and `FCOMP 0.0f; TEST AH,5; JP` tests the UNROUNDED value (JP skips on greater,
    // equal or unordered, so only strictly negative clamps to 0).
    if (Self->Level > 0.0f)
    {
        double const NewLevel = Self->Level -
            (double)Self->UnknownScale_0xD8 * Self->DecayRate * DeltaTime;
        Self->Level = (float)NewLevel;
        if (NewLevel < 0.0)
            Self->Level = 0.0f;
    }
}
