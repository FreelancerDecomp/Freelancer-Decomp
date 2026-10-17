// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_buzzbehavior.h"
#include "common_misc.h"  // RandomVector
#include "common_aisteer.h"  // FAiTarget + IsTargetCoAlignedAndAhead / GetInterceptClearance /
                              // IsWithinInterceptRange -- Update()'s three intercept probes.
#include "common_csys.h"   // Matrix / Transform -- FetchNavCandidateBasis's real result type
#include "common_iface.h"  // IObjInspect -- what NavHelper[+0x14fc][+0x84] actually is

#include <cstdlib>  // rand()

// BuzzBehavior -- see common_buzzbehavior.h for the full RTTI/layout writeup
// this pass relied on. Ported from EXE/Disasm/Common.dll.txt (raw disassembly
// is authoritative in this module over Ghidra's Common.dll.c decompilation --
// see this marathon's "ground truth precedence" rule; Update() below has NO
// Ghidra decompile at all, raw disasm is its ONLY source).

// ----------------------------------------------------------------------------
// FUN_062c5af0 (Common.dll.txt 191534-191594, Common.dll.c 101708-101747) --
// constructor. Fully decompiled and clean (unlike Update()); this pass
// verified it 1:1 against the raw disassembly. Calls the shared TractorArm-
// family base-init helper FUN_062d2020 (item 1529 -- it writes +0x04..+0x29, see Behavior in common_behavior.h)
// then default-constructs five still-unidentified sub-objects (FULLPORT-
// DEFERRED, see the private Construct* absence below -- kept as inline no-ops
// via the member-initializer list, matching DockBehavior/IdleBehavior's own
// convention), then zeroes/initializes every field retail explicitly writes.
// ----------------------------------------------------------------------------
BuzzBehavior::BuzzBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , OwnerContext(nullptr)
    , ObjectDb(nullptr)
    , DeltaTime(0.0f)
    , BehaviorMgrPtr(nullptr)
    , NavHelper(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
    , WatcherA()
    , WatcherB()
    , WatcherC()
    , WatcherD()
    , WatcherE()
    , Field2f4{}
    , Field300(0.0f)
    , Field304(0)
    , Pad305{}
    , Field308(0.0f)
    , Field30c(0.0f)
    , Field310(0.0f)
    , Field314(1.5f)
    , BuzzState(0)
    , HeadTowardStyle(0)
    , PassByStyle(0)
    , MinDistanceToHeadToward(500.0f)
    , Field328(1.5f)
    , DistanceToPassBy(100.0f)
    , Field330(5.0f)
    , PassByTime(3.0f)
    , PassByElapsed(0.0f)
    , MaxTimeToHeadAway(6.0f)
    , HeadAwayElapsed(0.0f)
    , Field344(0.0f)
    , PassByRollThrottle(0.0f)
    , HeadTowardEngineThrottle(1.0f)
    , HeadTowardTurnThrottle(1.0f)
{
    ConstructBase(InTypeId);   // FUN_062d2020 -- Behavior's own field initialisation
    // ALL FIVE sub-objects are real-constructed now (2026-09-17,
    // claude:w19-buzz-retype), via MoveIdea::MoveIdea() / FaceIdea::FaceIdea() /
    // AfterburnerIdea::AfterburnerIdea() / DodgeIdea::DodgeIdea() /
    // BreakAwayIdea::BreakAwayIdea() (all common_idea.h) -- matching retail's own
    // FUN_062d4e60(&WatcherA) / FUN_062d6750(&WatcherB) / FUN_062da500(&WatcherC)
    // / FUN_062d77d0(&WatcherD) / FUN_062d8970(&WatcherE) calls exactly
    // (Common.dll.txt 191558/191561/191564 and .text:062c5b46/062c5b5c, inside
    // FUN_062c5af0). The "FULLPORT-DEFERRED: retail additionally
    // default-constructs TWO still-unidentified sub-objects here ... this port
    // leaves those two zero-initialized instead" note that stood here is deleted:
    // both classes are published and both members are typed for real, so the
    // documented behavioural gap versus retail is gone -- WatcherD's and
    // WatcherE's ctor-default field values (DodgeIdea's 5.0f/1.0471377f/150.0f/
    // 0.2f/1.0f/30.0f/{0.1,0,1}, BreakAwayIdea's 350.0f/4.0f/100.0f/6.0f/
    // 100000.0f) are present now instead of zeros.

    // Retail's own explicit zeroing of Field2f4/Field300/Field304/Field308/
    // Field30c/Field310/BuzzState/PassByElapsed/HeadAwayElapsed is duplicated by
    // SetContext()/RecomputeApproach() at runtime anyway (both re-zero large
    // parts of this same region) -- nothing further to do here beyond the
    // member-initializer list above.
}

// FUN_062c5c40 (Common.dll.txt 191656-191711, Common.dll.c 101774-101805) --
// the real (non-thunk) destructor; FUN_062c5c00 (item 1301, MSVC's scalar-
// deleting-destructor boilerplate) just calls this then conditionally frees
// the block.
BuzzBehavior::~BuzzBehavior()
{
    // Retail clears four BaseWatcher-shaped pointer fields via
    // BaseWatcher::set_pointer(nullptr), IN THIS ORDER (Common.dll.txt
    // 191672-191702): +0x280 (WatcherE own +0x28), +0x1a0 (WatcherD own
    // +0x28), +0xb8 (WatcherB own +0x14), +0x40 (WatcherA own +0x14).
    // WatcherB/A's own +0x14 are now real, confirmed as exactly
    // FaceIdea::Watcher / MoveIdea::Watcher (common_idea.h) -- same
    // cross-class corroboration pattern found in LaunchBehavior/LootBehavior/
    // FaceBehavior's own destructors this pass.
    //
    // WatcherC's ABSENCE from this list used to be called "the odd one out";
    // as of 2026-09-17 it is positive evidence instead. WatcherC is an
    // AfterburnerIdea, which adds no fields of its own beyond BaseIdea -- and
    // BaseIdea has no BaseWatcher member at all (MoveIdea and FaceIdea each
    // add their own at +0x14). So there is nothing here for retail TO clear,
    // which is exactly what the disassembly shows.
    //
    // WatcherE's and WatcherD's clears are REAL as of 2026-09-17
    // (claude:w19-buzz-retype), in retail's own order. +0x280 is
    // WatcherE(+0x258) + 0x28 and +0x1a0 is WatcherD(+0x178) + 0x28, and on both
    // classes +0x28 is FDirectionPicker::Watcher (the picker sits at +0x14 and its
    // own Watcher at rel +0x14). Retail's guard is `if (Watched != nullptr)
    // set_pointer(nullptr)`, which is exactly what BaseWatcher::set(nullptr)
    // does, so the four calls below are the same four calls retail makes.
    WatcherE.Picker.Watcher.set(nullptr);
    WatcherD.Picker.Watcher.set(nullptr);
    WatcherB.Watcher.set(nullptr);
    WatcherA.Watcher.set(nullptr);

    // Retail then calls FUN_062d2080 (~Behavior, a vptr reset); the port's base destructor
    // runs implicitly after this body.
}

// ----------------------------------------------------------------------------
// Own vtable slot 1 (offset 0x04). Common.dll.txt 191719-191797
// (LAB_062c5cf0), 77 instructions.
// ----------------------------------------------------------------------------
bool BuzzBehavior::SetContext(void* Param1, void* Param2, void* Param3)
{
    // retail: `CALL FUN_062d2090(this, Param1, Param2, Param3)` -- Behavior::BindContext
    // (owner / sys null -> false; else +0x14 owner, +0x08 owner id, +0x24 sys, +0x20 manager).
    if (!BindContext(Param1, Param2, Param3))
        return false;

    // retail: `MOV [this+0x300],0x43960000(300.0f); MOV byte[this+0x304],0`.
    Field300 = 300.0f;
    Field304 = 0;

    // retail (Common.dll.txt 191735-191750): `WatcherA->vtbl[0](OwnerContext,
    // NavHelper, Param2)`. Push order read off the raw bytes -- ECX = OwnerContext
    // (+0x14), EAX = NavHelper (+0x24), EBP = Param2 -- and they are pushed
    // OwnerContext last, so the argument order is (Object, Handle, Context),
    // matching BaseIdea::SetContext's own signature.
    //
    // REAL as of 2026-09-17: this is MoveIdea's own vtable slot 0
    // (MoveIdea::SetContext, LAB_062d4f20), which common_idea.h now ports in
    // full. The old "common_idea.h itself leaves this FULLPORT-DEFERRED --
    // not safe to call through yet" note on this line was stale and is deleted.
    WatcherA.SetContext(OwnerContext, NavHelper, Param2);

    // The next four calls are ONE MoveIdea configuration block: four
    // consecutive /OPT:ICF-folded one-line setters on the same &WatcherA
    // receiver (Common.dll.txt 191751-191761). See the header's fold table --
    // each address was disassembled this pass and each is a single `MOV
    // [ECX+disp],EAX; RET 4`, so on a MoveIdea receiver the effects are exact.
    //
    // retail: `FUN_062d88b0(&WatcherA, 2)` -- `MOV [ECX+0x44],EAX`, i.e.
    // MoveIdea::SetField44. The literal 2 matches common_idea.h's own note
    // that the only observed arguments to this setter are the small ints 0
    // and 2. (The old comment called this a "SetAlertLevel-shaped shared
    // helper"; that was an invented name for a one-instruction store.)
    WatcherA.SetField44(2);

    // retail: `TextRenderContext::set_origin_h(&WatcherA, 0x40490fdb)` --
    // .text:063275f0, `MOV [ECX+0x38],EAX`, i.e. MoveIdea::SetAngle38. The
    // argument is the raw bit pattern for pi; common_idea.h documents that
    // +0x38 is ctor-defaulted to exactly this value, so the call restores the
    // member's own default. The old comment's claim that the argument was
    // "not matched to any of MoveIdea's confirmed own fields" is disproved.
    WatcherA.SetAngle38(3.14159274f);  // 0x40490fdb

    // retail: `FUN_062d5980(&WatcherA, 0)` -- MoveIdea::SetMode(0), the 0/1/2
    // transition machine. common_idea.h notes this address is shared with
    // MatchThrottle2Idea's identical member and that the listing genuinely
    // cannot say whether the linker folded two sources or retail wrote one;
    // either way the effect on a MoveIdea receiver is MoveIdea::SetMode.
    WatcherA.SetMode(0);

    // retail: `DeviceTRC::bind_to_dc(&WatcherA, 0)` -- .text:062a66b0, `MOV
    // [ECX+0x3c],EAX`, i.e. MoveIdea::SetRange3c. The pushed dword is the raw
    // literal 0, which as a float bit pattern is 0.0f (common_idea.h warns
    // this setter takes a dword straight through, so callers must reinterpret
    // rather than convert -- 0 reinterprets to 0.0f either way).
    WatcherA.SetRange3c(0.0f);

    // retail (191762-191772): `WatcherB->vtbl[0](OwnerContext, NavHelper,
    // Param2)`. FaceIdea's own slot 0 is LAB_062d6820; common_idea.h routes it
    // to BaseIdea::SetContext, which is what this call now reaches.
    WatcherB.SetContext(OwnerContext, NavHelper, Param2);

    // retail (191773-191783): `WatcherC->vtbl[0](OwnerContext, NavHelper,
    // Param2)`. Retail dereferences WatcherC's leading dword as a vtable
    // pointer with no null check, which an earlier pass flagged as a puzzle
    // versus DockBehavior's structurally similar GotoDirectiveHandle. It is
    // not a puzzle: WatcherC really is an embedded polymorphic object, an
    // AfterburnerIdea (see the header's "WatcherC IDENTITY, RESOLVED" note),
    // and AfterburnerIdea does not override slot 0 -- it inherits BaseIdea's
    // shared default, LAB_062d7920.
    WatcherC.SetContext(OwnerContext, NavHelper, Param2);

    // retail (191784-191805, .text:062c5d8f-062c5dbb): `WatcherD->vtbl[0](...)`
    // then `WatcherE->vtbl[0](...)`, same three arguments in the same order --
    // `LEA ECX,[ESI+0x178]` / `CALL dword ptr [EDX]` and `LEA ECX,[ESI+0x258]` /
    // `CALL dword ptr [EDX]`, with EDX loaded from each object's own leading
    // vtable dword. REAL as of 2026-09-17: both members are typed now, and both
    // DodgeIdea and BreakAwayIdea inherit BaseIdea's shared slot 0
    // (LAB_062d7920) unchanged, so this is exactly what retail dispatches to.
    WatcherD.SetContext(OwnerContext, NavHelper, Param2);
    WatcherE.SetContext(OwnerContext, NavHelper, Param2);

    return true;
}

// ----------------------------------------------------------------------------
// Own vtable slot 3 (offset 0x0c). Common.dll.txt 191808-191999
// (LAB_062c5dd0), 186 instructions.
// ----------------------------------------------------------------------------
void BuzzBehavior::RecomputeApproach()
{
    // .text:062c5dd8: `CALL FUN_062d20d0` -- Behavior's own slot 3 body.
    Behavior::Slot3();

    // .text:062c5ddd-062c5de3: FUN_062d21d0(this, 1, 3) -- Behavior::SetCruiseLock.
    SetCruiseLock(1, 3);

    // retail: `Field344 = 300.0f; Field308 = 0; Field304 = 0;` then
    // `Field2f4 = RandomVector(1.0f)` (retail copies the returned Vector's
    // three floats field-by-field into +0x2f4/+0x2f8/+0x2fc).
    Field344 = 300.0f;
    Field308 = 0.0f;
    Field304 = 0;
    Field2f4 = RandomVector(1.0f);

    // retail: `Field30c = 5.0f; Field310 = 0; BuzzState = 0; PassByElapsed = 0;
    // HeadAwayElapsed = 0;` -- BuzzState being reset here means RecomputeApproach()
    // restarts BuzzBehavior's own state machine at "head away" (state 0)
    // every time it runs, consistent with it being the mode this class'
    // vtable slot 3 (the family's usual "(re)compute the maneuver, entering
    // or re-entering the approach state" slot) is expected to perform.
    Field30c = 5.0f;
    Field310 = 0.0f;
    BuzzState = 0;
    PassByElapsed = 0.0f;
    HeadAwayElapsed = 0.0f;

    // NavHelper+0x1584 is the SystemManager's pub::AI::Personality: +0x1618..+0x1677 is its
    // BuzzHeadParams (Personality+0x94), +0x1678..+0x16ab its BuzzPassParams (+0xF4).
    pub::AI::Personality::BuzzHeadTowardUseStruct const& Head = NavHelper->PersonalityData.BuzzHeadParams;
    pub::AI::Personality::BuzzPassByUseStruct const& Pass = NavHelper->PersonalityData.BuzzPassParams;

    // .text:062c5e50-062c5eb6: a weighted pick of HeadTowardStyle. Sum = Slide [0x1620] + Waggle
    // [0x1624] + StraightTo [0x161c] on the FPU, FSTP'd to a float local; R = (double)rand()
    // * Sum * (1/32768) (DAT_063997d0 == 0x38000000), kept on the FPU stack. `FCOM [0x1620];
    // TEST AH,0x5; JP`: the fall-through (odd parity) is ST0 < src only; greater, equal and
    // unordered take the jump. So R < Slide -> 1. At LAB_062c5e96 the second test is
    // `FLD [0x1624]; FADD [0x1620]; FXCH; FCOMPP` with the same mask: R < (Waggle + Slide)
    // -> 2, otherwise 0.
    {
        float const Sum = (float)((double)Head.BuzzHeadTowardStyleWeightSlide +
                                  Head.BuzzHeadTowardStyleWeightWaggle +
                                  Head.BuzzHeadTowardStyleWeightStraightTo);
        double const Pick = (double)std::rand() * Sum * 3.0517578125e-05;
        if (Pick < Head.BuzzHeadTowardStyleWeightSlide)
            HeadTowardStyle = 1;
        else if (Pick < (double)Head.BuzzHeadTowardStyleWeightWaggle + Head.BuzzHeadTowardStyleWeightSlide)
            HeadTowardStyle = 2;
        else
            HeadTowardStyle = 0;
    }

    // .text:062c5ebc-062c5f1b: the same shape for PassByStyle over StraightBy [0x1678] /
    // EngineKill [0x1680] / BreakAway [0x167c]; EBX is still 0 from 062c5dec when the second
    // JP is taken.
    {
        float const Sum = (float)((double)Pass.BuzzPassByStyleWeightStraightBy +
                                  Pass.BuzzPassByStyleWeightEngineKill +
                                  Pass.BuzzPassByStyleWeightBreakAway);
        double const Pick = (double)std::rand() * Sum * 3.0517578125e-05;
        if (Pick < Pass.BuzzPassByStyleWeightStraightBy)
            PassByStyle = 1;
        else if (Pick < (double)Pass.BuzzPassByStyleWeightEngineKill + Pass.BuzzPassByStyleWeightStraightBy)
            PassByStyle = 2;
        else
            PassByStyle = 0;
    }

    // .text:062c5f21-062c5f60: MinDistanceToHeadToward = raw dword [0x1628]; then on the FPU
    // R = rand() * (1/32768); R += R; R -= 1.0f (DAT_0639a1d0); R *= [0x162c]; R += 1.0f;
    // R *= MinDistanceToHeadToward; FSTP float.
    MinDistanceToHeadToward = Head.BuzzMinDistanceToHeadToward;
    {
        double const Rand = (double)std::rand() * 3.0517578125e-05;
        MinDistanceToHeadToward = (float)(((Rand + Rand - 1.0) * Head.BuzzMinDistanceToHeadTowardVariancePercent + 1.0) *
                           MinDistanceToHeadToward);
    }

    // Plain dword copies: [0x1618] -> MaxTimeToHeadAway, [0x1684] -> DistanceToPassBy, [0x1688] -> PassByTime,
    // [0x1630] -> HeadTowardEngineThrottle, [0x1634] -> HeadTowardTurnThrottle.
    MaxTimeToHeadAway = Head.BuzzMaxTimeToHeadAway;
    DistanceToPassBy = Pass.BuzzDistanceToPassBy;
    PassByTime = Pass.BuzzPassByTime;
    HeadTowardEngineThrottle = Head.BuzzHeadTowardEngineThrottle;
    HeadTowardTurnThrottle = Head.BuzzHeadTowardTurnThrottle;

    // ------------------------------------------------------------------
    // Tail, Common.dll.txt 191926-191999. PORTED FOR REAL 2026-09-17
    // (claude:common-buzzbehavior), replacing a FULLPORT-DEFERRED comment and
    // the decomp-tracker note (item 1305) that called this "a GENUINE EXTERNAL
    // BLOCKER ... there is no safe C++ body to write for any of these ~11
    // calls without first identifying those classes."
    //
    // That conclusion was wrong, and specifically so: identifying WatcherD's
    // and WatcherE's classes is NOT a prerequisite here, because not one of
    // these eleven calls dispatches through a vtable. Every one is a direct
    // CALL to a fixed address, and each of those addresses was disassembled
    // this pass and is a bare store at a fixed offset -- `MOV [ECX+disp],EAX;
    // RET 4` for nine of them, `MOV [ECX+0xd8],AL; RET 4` for one, and four
    // consecutive dword stores (RET 0x10) for FUN_062d76b0. Several carry
    // borrowed Ghidra names ("DeviceTRC::bind_to_dc", "CAsteroidCube::
    // set_state", "CAsteroidField::set_fill_dist") from /OPT:ICF folds, which
    // is exactly why the earlier reading treated them as opaque -- but a fold
    // hides the OWNER's name, not the instruction, and the instruction is all
    // that is needed to reproduce the store.
    //
    // EDI is &WatcherD throughout (LEA at .text:062c5f3a); the two
    // FUN_062d76b0 calls take &WatcherD+0x14 (ESI+0x18c) and &WatcherE+0x14
    // (ESI+0x26c), which are exactly each object's own FDirectionPicker.
    //
    // ROUTED THROUGH NAMED FIELDS 2026-09-17 (claude:w19-buzz-retype), now that
    // WatcherD/WatcherE are a real DodgeIdea/BreakAwayIdea. Each target offset
    // was re-derived from the setter's own raw body (see the header's fourteen-
    // address table) and then matched against common_idea.h's field map -- the
    // offsets in the old comments were NOT taken on trust. Retail copies every
    // source as a raw dword (`MOV EDX,[EAX+disp]; PUSH EDX`), never through the
    // x87 stack, so wherever the destination field is a float the assignment
    // below reads the NavHelper slot as a float too, which is the same bits.
    WatcherD.Throttle = Head.BuzzHeadTowardEngineThrottle;               // [0x1630] FUN_062d9490, +0x94
    WatcherD.ConeAngle = Head.BuzzDodgeConeAngle;                        // [0x1654] .text:062a66b0, +0x3c
    WatcherD.ConeAngleVariancePercent = Head.BuzzDodgeConeAngleVariancePercent;  // [0x1658] .text:062a33e0, +0x40
    WatcherD.WaggleAxisConeAngle = Head.BuzzDodgeWaggleAxisConeAngle;    // [0x165c] FUN_062d88b0,  +0x44
    WatcherD.RollAngle = Head.BuzzDodgeRollAngle;                        // [0x1660] .text:062a33c0, +0x48
    WatcherD.TurnThrottle = Head.BuzzDodgeTurnThrottle;                  // [0x1650] FUN_062d8910, +0x90
    // FUN_062d76b0's receiver is <Idea>+0x14 and it writes that pointer's
    // +0x4/+0x8/+0xc/+0x10 -- i.e. FDirectionPicker::WeightLeft/Right/Up/Down. The LAST
    // push is the FIRST argument, so 0x1640 lands on WeightLeft (pushes at
    // 062c6010-062c601f are 0x164c, 0x1648, 0x1644, 0x1640 in that order).
    WatcherD.Picker.WeightLeft = Head.BuzzDodgeDirectionWeightLeft;     // [0x1640]
    WatcherD.Picker.WeightRight = Head.BuzzDodgeDirectionWeightRight;   // [0x1644]
    WatcherD.Picker.WeightUp = Head.BuzzDodgeDirectionWeightUp;         // [0x1648]
    WatcherD.Picker.WeightDown = Head.BuzzDodgeDirectionWeightDown;     // [0x164c]
    // FUN_062d8950 writes +0xd4, which is VectorCc.z (VectorCc spans 0xcc..0xd8,
    // so 0xcc + 8 == 0xd4) -- NOT a field of its own.
    WatcherD.VectorCc.z = Head.BuzzHeadTowardRollThrottle;              // [0x1638] FUN_062d8950, +0xd4
    // The only non-dword member of the chain: retail loads a single BYTE
    // (`MOV CL,byte ptr [EAX+0x163c]` after `XOR ECX,ECX`) and FUN_062d8960
    // stores it as a byte into +0xd8 == DodgeIdea::bRollFlipDirection.
    WatcherD.bRollFlipDirection = Head.bBuzzHeadTowardRollFlipDirection; // [0x163c] FUN_062d8960, +0xd8
    WatcherD.SlideThrottle = Head.BuzzSlideThrottle;                    // [0x166c] FUN_062d8930, +0xc8
    // The second FUN_062d76b0, on WatcherE's own picker (ESI+0x26c == 0x258+0x14).
    WatcherE.Picker.WeightLeft = Pass.BuzzBreakDirectionWeightLeft;     // [0x1690]
    WatcherE.Picker.WeightRight = Pass.BuzzBreakDirectionWeightRight;   // [0x1694]
    WatcherE.Picker.WeightUp = Pass.BuzzBreakDirectionWeightUp;         // [0x1698]
    WatcherE.Picker.WeightDown = Pass.BuzzBreakDirectionWeightDown;     // [0x169c]

    PassByRollThrottle = Pass.BuzzPassByRollThrottle;                             // [0x16a8]
}

// ----------------------------------------------------------------------------
// Own vtable slot 4 (offset 0x10). Common.dll.txt 192001-192024
// (FUN_062c60a0), 20 instructions.
// ----------------------------------------------------------------------------
void BuzzBehavior::Disengage()
{
    // retail: calls each of WatcherB/A/C/D/E's own vtable slot 0x10 in THIS
    // order (Common.dll.txt 192007-192021: +0xa4=WatcherB, +0x2c=WatcherA,
    // +0x164=WatcherC, +0x178=WatcherD, +0x258=WatcherE) -- BaseIdea's own
    // slot 4/0x10 IS Commit() (common_idea.h), so WatcherB/A/C's calls are all
    // wired for real below. WatcherC joined them 2026-09-17 with its
    // AfterburnerIdea identification. MoveIdea::Commit() has a real body;
    // FaceIdea::Commit() and AfterburnerIdea::Commit() are confirmed-empty
    // stubs (common_faceidea.cpp and common_idea.cpp) -- a documented gap owned by those files, not one
    // introduced here. WatcherD/WatcherE joined the list 2026-09-17
    // (claude:w19-buzz-retype): both are real objects now, and DodgeIdea::Commit
    // (LAB_062d8830) and BreakAwayIdea::Commit (LAB_062d93e0) both have real
    // bodies, so all five of retail's slot-0x10 calls are reproduced.
    WatcherB.Commit();
    WatcherA.Commit();
    WatcherC.Commit();
    WatcherD.Commit();
    WatcherE.Commit();

    // .text:062c60e8-062c60eb: `MOV ECX,ESI; JMP FUN_062d20f0` -- Behavior's own slot 4.
    Behavior::Slot4();
}

// ----------------------------------------------------------------------------
// Own vtable slot 5 (offset 0x14). Common.dll.txt 192026-192072
// (FUN_062c60f0), 40 instructions.
// ----------------------------------------------------------------------------
int BuzzBehavior::EvaluateReadiness()
{
    // .text:062c60f3-062c610a: FLD HeadAwayElapsed; FCOMP MaxTimeToHeadAway; TEST AH,0x1; JNZ. C0 is set
    // for less AND for unordered, so the jump (the checks) is taken unless
    // HeadAwayElapsed >= MaxTimeToHeadAway; the fall-through zeroes HeadAwayElapsed.
    int Mode = 2;                                                    // EDI = 2
    if (HeadAwayElapsed >= MaxTimeToHeadAway)
    {
        HeadAwayElapsed = 0.0f;                                             // 062c610c
    }
    else
    {
        // .text:062c6118-062c614c: FUN_062ecb20 on NavHelper+0x1364; when it is false,
        // TargetManager::GetTarget (FUN_062ed5a0, NavHelper+0x142c) and, for a non-null
        // target, FUN_062e6200 (GetObjectTypeOrZero) & 0x30000 -> Mode 1.
        if (!NavHelper->GunConvergenceStateBytes.FollowerDirectiveGateA())
        {
            IObjRW* const Target = NavHelper->Targeting.GetTarget();
            if (Target != nullptr && (GetObjectTypeOrZero(Target) & 0x30000) != 0)
                Mode = 1;
        }
    }

    if (Flag29 != 0)
    {
        ModeSelector0xc = 3;
        return 3;
    }
    ModeSelector0xc = Mode;
    return Mode;
}

// ----------------------------------------------------------------------------
// Internal helper (not a distinct vtable slot). Common.dll.txt 192075-192181
// (FUN_062c6170), 100 instructions. Called twice from Update() (Common.dll.txt
// 192588, 192831).
// ----------------------------------------------------------------------------
void BuzzBehavior::AcquireLockEffects()
{
    SystemManager* const Sys = NavHelper;

    // 062c6177-062c6181: TargetManager::GetTarget (FUN_062ed5a0) on NavHelper+0x142c;
    // retail uses the result unchecked.
    IObjRW* const Target = Sys->Targeting.GetTarget();

    // 062c6188-062c61ae: WatcherB (FaceIdea, +0xa4) publishes the target's get_id()
    // (FUN_062d4e50), takes HeadTowardTurnThrottle as its +0x94 scale (FUN_062d9490) and watches the
    // target (FUN_062d7510).
    WatcherB.PublishTargetId(Target->get_id());
    WatcherB.SetScale94(HeadTowardTurnThrottle);
    WatcherB.SetWatchObject(Target);

    // 062c61b3-062c61e1: FUN_062e8840(NavHelper+0x34) gates FUN_062d7640 with {0, 0, 1}.
    if (CheckNavHelperFlagA(NavHelper))
    {
        Vector const WorldUp{ 0.0f, 0.0f, 1.0f };
        WatcherB.SetUpVector(WorldUp);
    }

    // 062c61e6-062c61ee: WatcherB vtbl[4](DeltaTime).
    WatcherB.Update(DeltaTime);

    // 062c61f1-062c622f: FUN_062e5190(NavHelper+0x14fc, Target, 0.0f). True: WatcherC
    // (AfterburnerIdea, +0x164) ticks and WatcherA's +0x40 scale is 1.0f; false:
    // WatcherC commits (vtbl+0x10) and the scale is HeadTowardEngineThrottle. The scale goes through
    // the .text:062a33e0 fold, MoveIdea::SetScale40, on WatcherA (+0x2c).
    float Scale40;
    if (IsAlignedAndOutsideHeadingCone(&Sys->SelfTarget, Target, 0.0f))
    {
        WatcherC.Update(DeltaTime);
        Scale40 = 1.0f;
    }
    else
    {
        WatcherC.Commit();
        Scale40 = HeadTowardEngineThrottle;
    }
    WatcherA.SetScale40(Scale40);

    // 062c6234-062c623e: WatcherA publishes the target's get_id() again (re-read).
    WatcherA.PublishTargetId(Target->get_id());

    // 062c6243-062c6284: the negated z column of the own ship's get_transform()
    // (ExternalIface, NavHelper+0x1580; reads +0x8/+0x14/+0x20), FUN_062d5890 =
    // MoveIdea::SetDirection.
    Matrix const& Rotation = Sys->SelfTarget.Iface->get_transform().Rotation;
    Vector Dir;
    Dir.x = -Rotation.E[0][2];
    Dir.y = -Rotation.E[1][2];
    Dir.z = -Rotation.E[2][2];
    WatcherA.SetDirection(Dir);

    // 062c6289-062c6291: WatcherA vtbl[4](DeltaTime).
    WatcherA.Update(DeltaTime);
}

// ----------------------------------------------------------------------------
// LAB_062c66cb + LAB_062c66f2, Common.dll.txt 192491-192504. Retail's shared
// "commit a fate to WatcherD" epilogue, reached from FIVE call sites across
// both HeadTowardStyle switches (062c6537 with fate 0, 062c6579 with fate 2,
// 062c66c9 with fate 3, 062c6a69 with fate 2, 062c6b0f with fate 3). Factored
// out here because retail itself shares the code -- the only per-site
// variation is the fate literal and WHICH contiguous NavHelper float pair is
// copied (+0x1664/+0x1668 BuzzDodgeIntervalTime[VariancePercent] for fates 0 and 3,
// +0x1670/+0x1674 BuzzSlideIntervalTime[VariancePercent] for fate 2).
//
// All three stores are /OPT:ICF folds resolved this pass (see the header's
// fold table): +0xb4 via .text:062d8920 ("CSimple::SetOwnerPlayer"), +0x34 via
// .text:063275d0 ("CAttachedEquip::SetFate"), +0x38 via .text:063275f0
// ("TextRenderContext::set_origin_h"). Each is a single `MOV [ECX+disp],EAX;
// RET 4`, so reproducing the store is exact -- there is no hidden side effect to
// miss and no unrelated class to corrupt. As of 2026-09-17 all three land on
// declared DodgeIdea fields (DodgeStyle/IntervalTime/IntervalTimeVariancePercent).
// The "fate" is the DodgeIdea style: 0 Waggle, 2 Slide, 3 Corkscrew (common_idea.h).
// ----------------------------------------------------------------------------
void BuzzBehavior::ApplySteeringFate(int Fate, float Interval, float IntervalVariance)
{
    // On a DodgeIdea receiver +0xb4/+0x34/+0x38 are DodgeStyle/IntervalTime/-VariancePercent.
    WatcherD.DodgeStyle = Fate;                                           // .text:062d8920, +0xb4
    WatcherD.IntervalTime = Interval;                                     // .text:063275d0, +0x34
    WatcherD.IntervalTimeVariancePercent = IntervalVariance;              // .text:063275f0, +0x38

    // retail: `WatcherD->vtbl[4](DeltaTime)`. WatcherD is a DodgeIdea, whose
    // slot-1 override is DodgeIdea::Update (LAB_062d7950, common_dodgeidea.cpp).
    // REAL as of 2026-09-17: the object is genuinely constructed now, so this is
    // no longer the inert call the old note described (its "WatcherD's own
    // constructor is unported, so the buffer stays zeroed and that function's
    // entry guard (Self+0x4 == nullptr) returns immediately" reason is gone).
    WatcherD.Update(DeltaTime);
}

// ----------------------------------------------------------------------------
// Own vtable slot 7 (offset 0x1c), Update/dispatch entry point. Common.dll.txt
// 192187-192974 (LAB_062c62a0), 741 instructions, NO Ghidra decompile -- raw
// disassembly is the ONLY source. Re-traced end to end 2026-09-17
// (claude:common-buzzbehavior); see the header's Update() comment for the four
// control-flow errors that re-trace found in the previous port and for the
// list of what is still genuinely open.
//
// Every branch, gate polarity, own-field arithmetic step, and per-case body is
// real compiled code, and as of 2026-09-17 (claude:common-buzz-largeship) so is
// every call. The list that stood here -- "the only remaining FULLPORT-DEFERRED
// sites are the NavHelper-nested-handle probes (each a named stub below) and
// three writes that land inside spans common_idea.h still marks Opaque" -- is
// spent: the "nested-handle chain" was NavHelper+0x14fc read as an FAiTarget
// plus a stack FAiTarget retail builds at [ESP+0x2c], all four probes are the
// already-ported items 1891/1745/1750/1753, and the last Opaque write became
// FaceIdea::SetAxis (item 1610).
//
// THE LAST TWO OPEN CALLS ARE CLOSED (2026-09-17, claude:w19-buzz-retype). What
// stood here said both were "on WatcherE and both for the same reason -- this
// file's member is still `unsigned char WatcherE[0xbc]` rather than the
// BreakAwayIdea common_idea.h now publishes". The member is a BreakAwayIdea now:
//   * FUN_062d9450(&WatcherE, Record.Iface) is BreakAwayIdea::SetWatchObject,
//     declared and defined for real in common_idea.h/.cpp this pass.
//   * WatcherE->vtbl[4](DeltaTime) at .text:062c6cbc is BreakAwayIdea::Update.
//     Its old reason ("retail LAB_062d8a50, which has no C++ body anywhere in
//     this port yet") was STALE: tracker item 1629 ported all 634 instructions
//     of LAB_062d8a50 on 2026-09-17 and is ready_unverified.
// Every call in this function is now a real call on a real object.
// ----------------------------------------------------------------------------
int BuzzBehavior::Update()
{
    // ---- Entry guard, Common.dll.txt 192188-192206 ----
    // retail: `Handle = FUN_062ed5a0(NavHelper+0x142c);` then
    // `if (OwnerContext == nullptr || Handle == nullptr) { UpdateResult10 = 3; return 3; }`
    // (the two JZs at 062c62eb/062c62f3 share LAB_062c6cfe, whose whole body is
    // `MOV EAX,3; MOV [ESI+0x10],EAX; RET` -- note it pops only EBX/ESI because
    // EBP/EDI are not pushed until after the guard).
    //
    // THE GAP IS CLOSED (2026-09-17, claude:common-buzz-largeship). What stood
    // here said "FetchNavHandle() is a stub, so this port can only check the
    // OwnerContext half ... this port proceeds as if Handle were non-null --
    // i.e. it runs MORE of the state machine than retail would". FUN_062ed5a0
    // is item 1891, TargetManager::GetTarget (common_targetmanager.cpp), so
    // both halves of the guard are real and the deviation is gone.
    IObjRW* const Handle = NavHelper->Targeting.GetTarget();

    // ---- The stack record at [ESP+0x2c], Common.dll.txt 192195-192203 ----
    // Retail INLINES FUN_062d09b0 (item 1470) here: `[ESP+0xac] = OwnerContext;
    // [ESP+0xb0] = Handle; [ESP+0xa8..0xab] = 0`. Resolved against the entry
    // ESP (0xc4 of locals plus the EBX/ESI pushes) those are +0x80, +0x84 and
    // +0x7C..+0x7F of a record based at [ESP+0x2c] -- i.e. an ordinary
    // FAiTarget (common_aisteer.h), the SAME 0x88-byte AI-target cache record
    // LargeShipBehavior::Update builds and BuildAiTargetList fills. Every later
    // use in this function lands inside it: the three intercept probes take
    // `&Record` as their second argument, and the candidate position/refresh
    // flag the previous port modelled as three loose locals are its CachedPos
    // (+0x0C), CachedRadius (+0x18) and bPosValid (+0x7E).
    //
    // The previous port called these "the NavHelper-nested-handle chain ...
    // dispatching through NavHelper[+0x14fc]'s own +0x84 sub-object, whose
    // class is not identified anywhere in this port ... real multi-hundred-byte
    // functions". NavHelper+0x14fc is itself an FAiTarget (item 1745 proved
    // SystemManager+0x14FC is exactly this record, +0x84 == SystemManager's own
    // ExternalIface at +0x1580), and all three "multi-hundred-byte" probes have
    // been ported since: items 1745/1750/1753.
    //
    // Like retail, only Init's six stores happen (+0x7C..+0x80, +0x84): every
    // other slice of the record is behind one of the four cache guards Init
    // clears, so nothing reads it before its own refresh writes it.
    FAiTarget Record;
    Record.Init(ObjInspectOf(OwnerContext), Handle);

    // retail: the two JZs at 062c62eb/062c62f3 share LAB_062c6cfe. Note retail
    // writes the record's two interface pointers BEFORE testing either of them,
    // which is reproduced above.
    if (OwnerContext == nullptr || Handle == nullptr)
    {
        UpdateResult10 = 3;
        return 3;
    }

    // NavHelper's own FAiTarget, the first argument to all four intercept
    // probes below (retail spells it `[ESI+0x24] + 0x14fc` at every site).
    FAiTarget* const NavRecord = &NavHelper->SelfTarget;

    // NavHelper+0x1584, the personality: BuzzHeadParams at +0x1618, BuzzPassParams at
    // +0x1678, FormationParams at +0x17cc.
    pub::AI::Personality const& Pers = NavHelper->PersonalityData;

    int Result = 1;  // EDI throughout retail (seeded at 062c6302); becomes UpdateResult10.

    // Aliases onto the record, so the derivations below keep reading the way
    // the raw trace named them. [ESP+0x40] is Record.CachedPos (the position
    // out-parameter, copied to [ESP+0x28] at LAB_062c6451 and read from there
    // later) and [ESP+0x4c] is Record.CachedRadius -- the FIRST out-parameter
    // of the same get_physical_radius call, which this function never reads.
    // RE-TYPED this pass: the previous port modelled that one as a `Vector`;
    // retail's own slot is four bytes (FAiTarget+0x18 is a float).
    Vector& CandidatePos = Record.CachedPos;

    // ------------------------------------------------------------------
    // "Immediate direction" fast path, Common.dll.txt 192213-192297.
    // Entered only when BuzzState != 2 (062c62f9/062c6307). CORRECTED
    // 2026-09-17: this is NOT an early return that skips the rest of the
    // function -- both of its exits (062c6415, 062c6425) jump to
    // LAB_062c6cda, the shared tail, so the NavHelper timeout check below
    // still runs. Only the middle of the function is skipped.
    // ------------------------------------------------------------------
    bool bFastPathTaken = false;
    if (BuzzState != 2)
    {
        // retail: `FUN_062e4f60(NavHelper+0x14fc, &Record, 0)` then, if true,
        // `FUN_062e4d00(NavHelper+0x14fc, &Record)` FCOMP'd against Field344.
        // The `TEST AH,0x5; JP` at 062c634b is "jump unless less", so the path
        // continues only while the lead distance is below Field344.
        //
        // BOTH ARE REAL CALLS NOW (2026-09-17). FUN_062e4f60 is item 1753,
        // IsTargetCoAlignedAndAhead, and FUN_062e4d00 is item 1750,
        // GetInterceptClearance -- both ready_unverified in common_aisteer.h,
        // and both operating on exactly the pair of FAiTarget records this
        // function already holds. The arguments are passed POSITIONALLY as
        // retail pushes them (arg1 = NavHelper's record, arg2 = this tick's
        // handle record, arg3 = the EBX that 062c62bb zeroed, i.e. 0.0f); no
        // attempt is made to re-derive which side item 1753 calls "Target" and
        // which "Reference", because the push order settles it either way.
        if (IsTargetCoAlignedAndAhead(NavRecord, &Record, 0.0f) &&
            GetInterceptClearance(NavRecord, &Record) < Field344)
        {
            // retail: `Basis = Handle->vtbl[0xc]()` (062c6354-062c6360). The
            // result is a 3x3 basis: FUN_062d75c0 block-copies nine dwords
            // from it, and MoveIdea::SetDirectionFrom reads its +0x8/+0x14/
            // +0x20, i.e. the .z of three consecutive Vectors.
            Matrix const& Basis = FetchHandleBasis(Record.Iface);
            {
                // retail: `FUN_062d75c0(&WatcherB, Basis)` -- `MOV
                // [ECX+0xa0],4` then `REP MOVSD` of 9 dwords into ECX+0x34:
                // FaceIdea::SetBasis(Matrix const&), nine dwords == sizeof(Matrix).
                WatcherB.SetBasis(Basis);

                // retail: `FUN_062d93c0(&WatcherB, 0x3ec90fdb)` -- `MOV
                // [ECX+0x8c],EAX`, i.e. FaceIdea::SetAngleWeight8c. 0x3ec90fdb
                // is pi/8; note common_idea.h records +0x8c's own ctor
                // default as 0x3dc90fdb (pi/32), so this genuinely widens it.
                WatcherB.SetAngleWeight8c(0.3926991f);  // 0x3ec90fdb

                // retail: `FUN_062d9490(&WatcherB, 0x3f800000)` --
                // FaceIdea::SetScale94(1.0f).
                WatcherB.SetScale94(1.0f);

                // retail: `WatcherB->vtbl[4](DeltaTime)` at 062c6390, and
                // unusually its RETURN VALUE is used (`CMP EAX,1`). FaceIdea's
                // Update() is LAB_062d68b0 (common_faceidea.cpp), so
                // this is a real value now rather than something to fabricate.
                int const FaceTick = WatcherB.Update(DeltaTime);
                if (FaceTick == 1)
                {
                    // `TEST AH,0x41; JNZ` at 062c63a6 skips the tick for
                    // "less or equal", so the tick fires only when strictly
                    // greater.
                    if (Field310 > Field314)
                    {
                        WatcherC.Update(DeltaTime);
                    }
                    Field310 += DeltaTime;
                }
                else
                {
                    Field310 = 0.0f;
                }

                // retail: `FUN_062d58f0(&WatcherA, Basis)` --
                // MoveIdea::SetDirectionFrom, which is exactly "Direction =
                // -(Src+0x08, Src+0x14, Src+0x20), renormalized".
                WatcherA.SetDirectionFrom(&Basis);

                // retail: `CmnAsteroid::CAsteroidCube::set_state(&WatcherA,
                // 0x3f800000)` -- .text:062a33e0, `MOV [ECX+0x40],EAX`, i.e.
                // MoveIdea::SetScale40(1.0f).
                WatcherA.SetScale40(1.0f);

                // retail: `WatcherA->vtbl[4](DeltaTime)`, return value discarded.
                WatcherA.Update(DeltaTime);
            }

            // retail (192281-192297, own fields only): `Field308 += DeltaTime;
            // Result = (Field308 > Field30c) ? 0 : (Field308 = 0, 1);`. The
            // `TEST AH,0x41; JNZ LAB_062c641a` at 062c640e routes "less or
            // equal" (and unordered) to the reset-and-1 arm. 062c6400 is FST,
            // not FSTP: the compare is on the unrounded x87 sum.
            double const Sum308 = (double)(Field308) + (double)(DeltaTime);
            Field308 = (float)(Sum308);
            if (Sum308 > (double)(Field30c))
            {
                Result = 0;
            }
            else
            {
                Field308 = 0.0f;
                Result = 1;
            }
            bFastPathTaken = true;
        }
    }

    if (!bFastPathTaken)
    {
        // retail: LAB_062c642a / LAB_062c643a, Common.dll.txt 192299-192317.
        // A shared landing pad: the fast path's two "not taken" exits reach it
        // by fallthrough and the BuzzState == 2 entry jumps straight to it
        // (062c6307), skipping the `CMP [Record.bPosValid],0` that the fall-through
        // path tests (the probes above take &Record and may have refreshed it).
        // On the jump path the flag is still Init's 0, so the guarded
        // FAiTarget::RefreshPos below covers both entries exactly.
        Record.RefreshPos();

        // ------------------------------------------------------------------
        // LAB_062c6451: BuzzState == 0, the head-away stage.
        // ------------------------------------------------------------------
        if (BuzzState == 0)
        {
            // retail (062c6475-062c647e): `EAX = OwnerContext; ECX = EAX+4;
            // CALL [[EAX+4]+0x20]`. NEWLY WIRED 2026-09-17, previously not
            // coded at all: OwnerContext+4 is the IObjAffect sub-object of an
            // IObjRW (common_iface.h proves the offset from IObjRW's own
            // CompleteObjectLocator), and byte offset 0x20 is IObjAffect slot
            // 8 -- get_formation_follower_count() (named 2026-09-24: Server.dll's
            // Ship body .text:06d02a70 is `JMP CShip::get_formation_follower_count`,
            // a non-const CShip method). Nothing is pushed and the result in EAX
            // is dropped (062c6481 overwrites EAX), which the port mirrors.
            (void)ObjAffectOf(OwnerContext)->get_formation_follower_count();

            // retail: `FUN_062e4350(NavHelper+0x14fc, &Out, MinDistanceToHeadToward)` -- the
            // same opaque probe used at all three of its call sites in this
            // function. True routes into the first HeadTowardStyle switch; false
            // commits the transition to the head-toward stage.
            if (IsWithinInterceptRange(NavRecord, &Record, MinDistanceToHeadToward))
            {
                // LAB_062c6510, Common.dll.txt 192510-192705. Dispatch is a
                // `SUB/DEC/DEC/JNZ` chain: 0 -> LAB_062c657e, 1 ->
                // LAB_062c653d, 2 -> fallthrough at 062c6524, anything else ->
                // LAB_062c6705 (straight to the timer). No BuzzBehavior-own
                // field is written anywhere in this switch (re-confirmed).
                switch (HeadTowardStyle)
                {
                case 0:
                {
                    // LAB_062c657e. The DeltaTime argument is pushed at
                    // 062c65a1, BEFORE the branch, and both arms then issue
                    // the same `CALL [EDX+4]` on WatcherC -- so the tick is
                    // unconditional and is hoisted here.
                    WatcherC.Update(DeltaTime);

                    // CORRECTED 2026-09-17. The gate at 062c658d-062c65a2 is
                    // `FLD NavHelper[0x1638]; FCOMP 0.0f; TEST AH,0x44; JP`.
                    // `TEST AH,0x44` is MSVC's EQUALITY idiom: of the four
                    // FCOMP outcomes only "equal" (C3 set alone) leaves a
                    // single bit in the mask and therefore clears PF, so JP
                    // is taken on "not equal or unordered". The prior port
                    // read this as `< 0.0f`, which inverted the branch for
                    // every positive value. Cross-checked against the second
                    // HeadTowardStyle switch, which gates the SAME field with the
                    // opposite mnemonic (JNP at 062c6b07) and must therefore
                    // reach the same fate-3 body on the same condition.
                    if (Pers.BuzzHeadParams.BuzzHeadTowardRollThrottle != 0.0f)   // [0x1638]
                    {
                        // LAB_062c66c6 -> the shared fate epilogue, fate 3.
                        ApplySteeringFate(3, Pers.BuzzHeadParams.BuzzDodgeIntervalTime,
                                          Pers.BuzzHeadParams.BuzzDodgeIntervalTimeVariancePercent);
                    }
                    else
                    {
                        // retail 062c65ae: MoveIdea::SetScale40(1.0f) on
                        // WatcherA (.text:062a33e0 fold).
                        WatcherA.SetScale40(1.0f);

                        // retail 062c65ba-062c660d: fetch the candidate basis
                        // through NavHelper[+0x14fc][+0x84]->vtbl[0xc](), then
                        // build `{-Basis[0].z, -Basis[1].z, -Basis[2].z}` on
                        // the stack with three FCHS/FSTP pairs and pass it to
                        // FUN_062d5890 = MoveIdea::SetDirection. (That is the
                        // same vector MoveIdea::SetDirectionFrom would build
                        // from the same pointer -- retail simply inlined it
                        // here. The x87 juggling was re-derived slot by slot,
                        // including the mid-sequence PUSH that shifts every
                        // subsequent [ESP+disp] by four.)
                        Matrix const& Basis = FetchNavCandidateBasis(NavHelper);
                        {
                            Vector Dir;
                            Dir.x = -Basis.E[0][2];
                            Dir.y = -Basis.E[1][2];
                            Dir.z = -Basis.E[2][2];
                            WatcherA.SetDirection(Dir);
                        }

                        // retail 062c6612-062c661a: `WatcherA->vtbl[4](dt)`.
                        WatcherA.Update(DeltaTime);

                        // retail 062c661d-062c6644: FAiTarget::RefreshPos
                        // inlined on NavHelper+0x14fc -- skipped while its
                        // guard byte (NavHelper+0x157a) is already set.
                        NavRecord->RefreshPos();

                        // retail LAB_062c6647 (192447-192459): builds
                        // `NavHelper[0x1508..0x1510] - CandidatePos` (+0x1508 is the
                        // own SelfTarget.CachedPos, 0x14fc + 0xc) and hands it to
                        // FUN_062d7590 = FaceIdea::SetDirection (`MOV [ECX+0xa0],3`
                        // plus a 3-dword copy into ECX+0x28).
                        Vector const LeadMinusCandidate{
                            NavRecord->CachedPos.x - CandidatePos.x,
                            NavRecord->CachedPos.y - CandidatePos.y,
                            NavRecord->CachedPos.z - CandidatePos.z };
                        WatcherB.SetDirection(LeadMinusCandidate);

                        // retail 062c667a: FUN_062d9490 == FaceIdea::SetScale94.
                        WatcherB.SetScale94(1.0f);

                        // retail 062c6686-062c66b4: `if (FUN_062e8840(
                        // NavHelper+0x34)) FUN_062d7640(&WatcherB, &{0,0,1});`
                        // FUN_062e8840 is a one-instruction byte getter
                        // (`MOV AL,[ECX+0x78]; RET`), already ported in
                        // CheckNavHelperFlagA below, so the GATE is exact now. WIRED
                        // 2026-09-17 (claude:common-idea-internals): the guarded
                        // call is FaceIdea::SetUpVector, and the +0xad flag and
                        // +0xb0 Vector it writes are named fields in
                        // common_idea.h now instead of bytes inside `OpaqueAc`.
                        // With {0,0,1} the setter's three equality gates put
                        // bUpValid = 1 and UpB0 = {0,0,1}.
                        if (CheckNavHelperFlagA(NavHelper))
                        {
                            Vector const WorldUp{ 0.0f, 0.0f, 1.0f };
                            WatcherB.SetUpVector(WorldUp);
                        }

                        // retail 062c66b9-062c66c1: `WatcherB->vtbl[4](dt)`.
                        WatcherB.Update(DeltaTime);
                    }
                    break;
                }
                case 1:
                    // LAB_062c653d, Common.dll.txt 192369-192393. WatcherC
                    // tick, then the shared fate epilogue with fate 2 and the
                    // +0x1670/+0x1674 NavHelper pair (062c6579 jumps into the
                    // epilogue at LAB_062c66f2, i.e. it skips the +0xb4/+0x34
                    // stores' own PUSH sequence but performs the same three
                    // writes -- re-verified instruction by instruction).
                    WatcherC.Update(DeltaTime);
                    ApplySteeringFate(2, Pers.BuzzHeadParams.BuzzSlideIntervalTime,
                                      Pers.BuzzHeadParams.BuzzSlideIntervalTimeVariancePercent);
                    break;
                case 2:
                    // 062c6524-062c6538. WatcherC tick, then fate 0 with the
                    // +0x1664/+0x1668 pair (`PUSH EBX`, and EBX is the zero
                    // register throughout this function).
                    WatcherC.Update(DeltaTime);
                    ApplySteeringFate(0, Pers.BuzzHeadParams.BuzzDodgeIntervalTime,
                                      Pers.BuzzHeadParams.BuzzDodgeIntervalTimeVariancePercent);
                    break;
                default:
                    // 062c651e: HeadTowardStyle outside {0,1,2} jumps straight to
                    // LAB_062c6705, skipping every case body.
                    break;
                }

                // ----------------------------------------------------------
                // TIMER, LAB_062c6705, Common.dll.txt 192705-192726.
                // CORRECTED 2026-09-17: this block is NOT unconditional. It
                // is reached ONLY by falling out of the switch above -- the
                // `JNZ LAB_062c672b` at 062c646f (BuzzState != 0) and the
                // `JZ`/`JMP LAB_062c672b` pair at 062c64fa/062c650b (end of
                // the Engaged commit) both jump past it. The prior port ran
                // it every tick, advancing HeadAwayElapsed on ticks retail leaves it
                // alone and so flipping Result to 2 early.
                //
                // Note the `MOV EDI,1` at 062c6708: retail re-seeds Result
                // here because EDI was reused as a scratch pointer inside the
                // case bodies. Reproduced so the re-seed is explicit.
                // `TEST AH,0x5; JNP` is "not less" -> Result = 2.
                // ----------------------------------------------------------
                // 062c6713 is FST, not FSTP: the compare is on the unrounded sum.
                Result = 1;
                double const Sum340 = (double)(DeltaTime) + (double)(HeadAwayElapsed);
                HeadAwayElapsed = (float)(Sum340);
                if (!(Sum340 < (double)(MaxTimeToHeadAway)))
                {
                    Result = 2;
                }
            }
            else
            {
                // retail 062c64a3-062c650b: the head-toward commit. Slot 0x10 is
                // Commit() on all three, in the order WatcherC, WatcherB,
                // WatcherA (062c64a9 / 062c64b8 / 062c64c4). WatcherC's call
                // is real as of 2026-09-17 (AfterburnerIdea).
                WatcherC.Commit();
                WatcherB.Commit();
                WatcherA.Commit();
                BuzzState = 1;

                // 062c64d7-062c6506: two Personality-flag-gated formation-leader
                // notifications on the NavHelper+0x1364 record (FUN_062ecc60 /
                // FUN_062ecce0, FGunConvergenceStateBlock in common_systemmanager.h).
                FGunConvergenceStateBlock& LeaderRec = NavHelper->GunConvergenceStateBytes;
                if (Pers.FormationParams.bBreakApartFormationOnBuzzHeadToward != 0)     // [0x17ec]
                {
                    LeaderRec.NotifyFormationLeader1(Pers.FormationParams.BreakFormationOnBuzzHeadTowardTime);
                }
                if (Pers.FormationParams.bRegroupFormationOnBuzzHeadToward != 0)        // [0x17f4]
                {
                    LeaderRec.NotifyFormationLeader2();
                }
            }
        }

        // ------------------------------------------------------------------
        // LAB_062c672b: BuzzState == 1, the head-toward stage. Note this runs in
        // the SAME tick as the head-toward commit above when that fired -- retail
        // falls into this test right after setting BuzzState = 1.
        // ------------------------------------------------------------------
        if (BuzzState == 1)
        {
            // retail 062c6738-062c6757: same probe, radius DistanceToPassBy.
            if (IsWithinInterceptRange(NavRecord, &Record, DistanceToPassBy))
            {
                // retail 062c675d-062c67f3. Commit WatcherB then WatcherA;
                // then a NavHelper-flag-gated choice between ticking WatcherC
                // and committing it; then the transition to pass-by. This
                // path jumps directly to LAB_062c6b50 WITHOUT passing through
                // the shared `Result = 0` merge point -- Result keeps whatever
                // the caller-side value was, which is the entry seed of 1
                // (the timer above cannot have run on this path).
                WatcherB.Commit();
                WatcherA.Commit();

                // FUN_062e8850 is `MOV AL,[ECX+0x68]; RET`, already ported in
                // CheckNavHelperFlagB below -- exact, not a guess.
                if (CheckNavHelperFlagB(NavHelper))
                {
                    WatcherC.Update(DeltaTime);
                }
                else
                {
                    WatcherC.Commit();
                }
                BuzzState = 2;

                // 062c67b1-062c67be FUN_062ecc60 ([0x17f5] / [0x17f8]).
                if (Pers.FormationParams.bBreakApartFormationOnBuzzPassBy != 0)
                {
                    NavHelper->GunConvergenceStateBytes.NotifyFormationLeader1(
                        Pers.FormationParams.BreakFormationOnBuzzPassByTime);
                }
                // 062c67ce-062c67d4 FUN_062ecce0 ([0x17fc]).
                if (Pers.FormationParams.bRegroupFormationOnBuzzPassBy != 0)
                {
                    NavHelper->GunConvergenceStateBytes.NotifyFormationLeader2();
                }
                // `CMP byte [EAX+0x168c],BL; JZ`: BuzzPassParams.bBuzzDropBombOnPassBy.
                if (Pers.BuzzPassParams.bBuzzDropBombOnPassBy != 0)
                {
                    // 062c67d9-062c67f3: FUN_062eb550 = GunGroups.DropAllMines (item 1845), the
                    // receiver `LEA ECX,[EAX+0x12c]` with EAX == NavHelper (.text:062c67e8).
                    NavHelper->GunGroups.DropAllMines();
                }
            }
            else
            {
                // retail LAB_062c67f8, 062c67f8-062c6818: same probe again,
                // MinDistanceToHeadToward radius (the same argument as the very first call).
                if (IsWithinInterceptRange(NavRecord, &Record, MinDistanceToHeadToward))
                {
                    // LAB_062c6852, Common.dll.txt 192852-192943 -- a SECOND,
                    // independent HeadTowardStyle 3-way switch with the same
                    // SUB/DEC/DEC/JNZ shape: 0 -> LAB_062c6a6e, 1 ->
                    // LAB_062c69b6, 2 -> fallthrough at 062c686e, else ->
                    // LAB_062c6b4e. Every case opens with the same two
                    // prologue steps (refresh the candidate position if this
                    // tick has not yet, then recompute NavHelper's cached lead
                    // point) and then compares the candidate-relative angle
                    // against its own bit-exact .rdata threshold. All three
                    // thresholds were read from raw .rdata bytes this pass:
                    // DAT_0639f5d0 = 0x3EC90FDB (pi/8), DAT_0639f534 =
                    // 0x3E490FDB (pi/16), DAT_0639f5d4 = 0x3FC90FDB (pi/2).
                    // `TEST AH,0x5; JP` at each site is "jump unless less".
                    switch (HeadTowardStyle)
                    {
                    case 0:
                    {
                        // LAB_062c6a6e, Common.dll.txt 192759-192828.
                        Record.RefreshPos();
                        NavRecord->RefreshPos();

                        // Two gates in series. The second (062c6af6-062c6b07)
                        // tests the SAME NavHelper[0x1638] the first switch's
                        // case 0 tests, but with `JNP` instead of `JP`: JNP is
                        // taken on "equal", which routes to
                        // AcquireLockEffects(). So the fate-3 body is reached
                        // on "not equal" -- identical to the first switch,
                        // which is the cross-check that settles that gate's
                        // polarity for both sites.
                        bool const bWithinAngle =
                            ReadCandidateMetric(NavHelper, CandidatePos) < 0.3926991f;  // DAT_0639f5d0
                        if (bWithinAngle && Pers.BuzzHeadParams.BuzzHeadTowardRollThrottle != 0.0f)
                        {
                            ApplySteeringFate(3, Pers.BuzzHeadParams.BuzzDodgeIntervalTime,
                                              Pers.BuzzHeadParams.BuzzDodgeIntervalTimeVariancePercent);
                        }
                        else
                        {
                            AcquireLockEffects();  // LAB_062c6b47, SITE #2
                        }
                        break;
                    }
                    case 1:
                        // LAB_062c69b6, Common.dll.txt 192703-192758.
                        Record.RefreshPos();
                        NavRecord->RefreshPos();
                        if (ReadCandidateMetric(NavHelper, CandidatePos) < 0.19634955f)  // DAT_0639f534
                        {
                            // 062c6a3f-062c6a69, joining the shared epilogue
                            // at LAB_062c6b33. NEWLY WIRED: the prior port
                            // only had the trailing WatcherD tick here.
                            ApplySteeringFate(2, Pers.BuzzHeadParams.BuzzSlideIntervalTime,
                                              Pers.BuzzHeadParams.BuzzSlideIntervalTimeVariancePercent);
                        }
                        else
                        {
                            AcquireLockEffects();  // SITE #2
                        }
                        break;
                    case 2:
                    {
                        // 062c686e-062c69b1. This case does NOT use the shared
                        // fate epilogue: it interleaves a guide-vector write
                        // between the three fate stores and the WatcherD tick,
                        // so it is spelled out.
                        Record.RefreshPos();
                        NavRecord->RefreshPos();
                        if (ReadCandidateMetric(NavHelper, CandidatePos) < 1.5707964f)  // DAT_0639f5d4
                        {
                            // 062c68f7-062c6922. Receiver is EBP, and
                            // `LEA EBP,[ESI+0x178]` at 062c68f7 proves it is
                            // &WatcherD -- the same three fields the shared
                            // epilogue writes, with fate 0 (`PUSH EBX`, the zero
                            // register) and the +0x1664/+0x1668 NavHelper pair.
                            WatcherD.DodgeStyle = 0;                                 // .text:062d8920
                            WatcherD.IntervalTime = Pers.BuzzHeadParams.BuzzDodgeIntervalTime;                // [0x1664] .text:063275d0
                            WatcherD.IntervalTimeVariancePercent = Pers.BuzzHeadParams.BuzzDodgeIntervalTimeVariancePercent; // [0x1668] .text:063275f0

                            // 062c6927-062c6975: retail repeats BOTH prologue
                            // refreshes here, each still flag-gated. Both
                            // flags are set by now, so the repeats are no-ops
                            // -- reproduced anyway since retail's flag state
                            // is what makes them no-ops, not an assumption.
                            NavRecord->RefreshPos();
                            Record.RefreshPos();

                            // LAB_062c6978: `CandidatePos - NavHelper's lead
                            // point`, i.e. the NEGATION of the vector the
                            // first switch's case 0 feeds WatcherB -- the
                            // operand order is genuinely reversed here
                            // (`FLD [ESP+0x48]; FSUB [EDI+0x14]` versus
                            // `FLD [EDI+0x14]; FSUB [ESP+0x30]`), not a
                            // transcription slip.
                            Vector Guide;
                            Guide.x = CandidatePos.x - NavRecord->CachedPos.x;
                            Guide.y = CandidatePos.y - NavRecord->CachedPos.y;
                            Guide.z = CandidatePos.z - NavRecord->CachedPos.z;
                            // FUN_062d88c0 (.text:062c69a0, receiver EBP ==
                            // &WatcherD): DodgeIdea::SetField58.
                            WatcherD.SetField58(Guide);

                            // 062c69a5-062c69ae: `WatcherD->vtbl[4](DeltaTime)`,
                            // i.e. DodgeIdea::Update.
                            WatcherD.Update(DeltaTime);
                        }
                        else
                        {
                            AcquireLockEffects();  // SITE #2
                        }
                        break;
                    }
                    default:
                        // 062c6868: HeadTowardStyle outside {0,1,2} jumps straight to
                        // LAB_062c6b4e, calling nothing at all -- unlike the
                        // three cases' own "not less" fallback.
                        break;
                    }
                }
                else
                {
                    // retail 062c681a-062c684d, "lost lock": reset the two
                    // maneuvering-intensity fields to 1.0f, call
                    // AcquireLockEffects() (SITE #1), then restore them from
                    // NavHelper's own tunables. All own fields, all real.
                    HeadTowardEngineThrottle = 1.0f;
                    HeadTowardTurnThrottle = 1.0f;
                    AcquireLockEffects();
                    HeadTowardEngineThrottle = Pers.BuzzHeadParams.BuzzHeadTowardEngineThrottle;   // [0x1630]
                    HeadTowardTurnThrottle = Pers.BuzzHeadParams.BuzzHeadTowardTurnThrottle;     // [0x1634]
                }

                // LAB_062c6b4e (`XOR EDI,EDI`): the shared merge point for
                // every switch-#2 exit AND the lost-lock path. Correctly NOT
                // on the bMaintain == true path above, which jumps past it.
                Result = 0;
            }
        }

        // ------------------------------------------------------------------
        // LAB_062c6b50: BuzzState == 2, the pass-by stage's per-tick
        // resolution. Also runs in the same tick as the transition above.
        // ------------------------------------------------------------------
        if (BuzzState == 2)
        {
            // 062c6b5d-062c6b6c: FLD/FADD/FST/FCOMP -- the compare is on the
            // unrounded sum. `TEST AH,0x5; JP LAB_062c6cc3` -- jump unless
            // strictly less, so the switch runs only while the sum < PassByTime.
            double const Sum338 = (double)(PassByElapsed) + (double)(DeltaTime);
            PassByElapsed = (float)(Sum338);
            if (Sum338 < (double)(PassByTime))
            {
                switch (PassByStyle)
                {
                case 0:
                    // LAB_062c6c85, Common.dll.txt 192924-192942. Four
                    // consecutive calls on ONE receiver, EDI, which
                    // `LEA EDI,[ESI+0x258]` at .text:062c6c8e proves is
                    // &WatcherE -- a BreakAwayIdea. ALL FOUR ARE REAL as of
                    // 2026-09-17 (claude:w19-buzz-retype).
                    //
                    // 062c6c97: FUN_062d9490, `MOV [ECX+0x94],EAX`. On a
                    // BreakAwayIdea +0x94 is TurnThrottle (a float, ctor default
                    // 1.0f): BuzzPassParams.BuzzBreakTurnThrottle ([0x16a4]).
                    WatcherE.TurnThrottle = Pers.BuzzPassParams.BuzzBreakTurnThrottle;
                    // 062c6c9c-062c6ca0: `PUSH 0x1` then the .text:063275d0
                    // fold, `MOV [ECX+0x34],EAX` -- BreakAwayIdea::BreakStyle, the
                    // int its own Update() switches on; 1 is Sideways.
                    WatcherE.BreakStyle = 1;
                    // 062c6ca5-062c6caf: `MOV EAX,[ESP+0xb8]` (the stack
                    // FAiTarget's own Iface slot, Record+0x84) then
                    // FUN_062d9450. That function is BreakAwayIdea's own
                    // non-virtual watch-object setter -- common_idea.h declares
                    // it now, with the receiver proof and the full 16-instruction
                    // decode. It does the +8 IObjRW -> Watchable adjustment, the
                    // dedup-guarded BaseWatcher::set_pointer on Picker.Watcher
                    // (+0x28) and clears Picker.Flag1c (+0x30).
                    WatcherE.SetWatchObject(Record.Iface);
                    // 062c6cb4-062c6cbc: `WatcherE->vtbl[4](DeltaTime)` -- note
                    // the `+4` is a BYTE offset into the vtable, i.e. slot 1,
                    // which on BreakAwayIdea is Update (LAB_062d8a50).
                    WatcherE.Update(DeltaTime);
                    break;
                case 1:
                    // LAB_062c6bd4, Common.dll.txt 192870-192921.
                    WatcherB.PublishTargetId(0);   // 062c6bdd FUN_062d4e50
                    // retail: `FUN_062d75e0(&WatcherB, &Vector{0, 0, PassByRollThrottle})`
                    // -- `MOV [ECX+0xa0],5` plus a 3-float copy into ECX+0x58.
                    // NO LONGER DEFERRED (2026-09-17): the reason recorded here
                    // was that +0x58 "is inside common_idea.h's Opaque58 span",
                    // and that span became `Vector Vector58` with a real setter,
                    // FaceIdea::SetAxis (item 1610), in the same pass that
                    // closed this file's other three idea-field handoffs.
                    // The vector is {0, 0, PassByRollThrottle}, not {PassByRollThrottle, 0, 0}: the
                    // FSTP at 062c6bed lands on the temporary's THIRD float and
                    // the two preceding dword stores zero the first two.
                    {
                        Vector Axis;
                        Axis.x = 0.0f;
                        Axis.y = 0.0f;
                        Axis.z = PassByRollThrottle;
                        WatcherB.SetAxis(Axis);
                    }
                    WatcherB.Update(DeltaTime);

                    WatcherA.PublishTargetId(0);   // 062c6c19 FUN_062d4e50
                    // .text:062a33e0 fold -> MoveIdea::SetScale40.
                    WatcherA.SetScale40(1.0f);
                    {
                        // 062c6c2a-062c6c71: the same inlined
                        // "negated basis z column -> MoveIdea::SetDirection"
                        // sequence as the first switch's case 0.
                        Matrix const& Basis = FetchNavCandidateBasis(NavHelper);
                        Vector Dir;
                        Dir.x = -Basis.E[0][2];
                        Dir.y = -Basis.E[1][2];
                        Dir.z = -Basis.E[2][2];
                        WatcherA.SetDirection(Dir);
                    }
                    WatcherA.Update(DeltaTime);
                    break;
                default:
                    // LAB_062c6b95 is the PassByStyle == 2 fallthrough of the
                    // SUB/DEC/DEC/JNZ chain, not a genuine "any other value"
                    // arm: values outside {0,1,2} take the JNZ at 062c6b8f to
                    // LAB_062c6cbf, which only zeroes Result. Kept gated so
                    // this port does not run the body for those values.
                    if (PassByStyle == 2)
                    {
                        // FUN_062e91d0 on [this+0x24]+0x18.
                        NavHelper->Thrust.SetEngineActive(false);

                        // retail: `FUN_062d7510(&WatcherB, Handle)` --
                        // FaceIdea's own watch-object setter: FieldA0 = 1 and
                        // Watcher.set(Handle + 8) when Handle is non-null,
                        // else FieldA0 = 0 and Watcher.set(nullptr). The guard
                        // that used to wrap this ("because this port's
                        // FetchNavHandle() is a stub") is gone: Handle is a
                        // real value now and the entry guard has already
                        // proved it non-null, exactly as in retail, so the
                        // setter's own null arm is unreachable here too.
                        WatcherB.SetWatchObject(Handle);

                        // FUN_062d9490 -> FaceIdea::SetScale94.
                        WatcherB.SetScale94(1.0f);
                        WatcherB.Update(DeltaTime);
                    }
                    break;
                }
                // Every arm converges on `XOR EDI,EDI` (LAB_062c6cbf, or an
                // inlined copy at 062c6bcd/062c6c81).
                Result = 0;
            }
            else
            {
                // LAB_062c6cc3, own fields only: give up the committed
                // maneuver and restart the state machine at head away.
                PassByElapsed = 0.0f;
                HeadAwayElapsed = 0.0f;
                BuzzState = 0;
                Result = 2;
            }
        }
    }

    // ------------------------------------------------------------------
    // LAB_062c6cda, Common.dll.txt 192953-192966 -- the shared tail EVERY
    // path converges on, the fast path included (see the correction note at
    // the top of this function). FUN_062e8810 turned out to be a single
    // instruction, `MOV AL,byte ptr [ECX+0x28]; RET`, so with the call site's
    // own `NavHelper+0x34` this is exactly "read NavHelper's byte at +0x5c" --
    // no longer an opaque probe at all, and ported as such below.
    // ------------------------------------------------------------------
    if (CheckNavHelperTimeout(NavHelper))
    {
        Result = 2;
    }

    UpdateResult10 = Result;
    return Result;
}

// Handle->vtbl[0xc]() at .text:062c635d, the receiver reloaded from the stack record's own
// Iface slot (`MOV ECX,[ESP+0xb8]` at 062c6354, Record+0x84): get_transform(), whose
// Transform begins with its 3x3 Matrix, returned as three consecutive Vectors -- what
// FaceIdea::SetBasis (nine dwords) and MoveIdea::SetDirectionFrom (+0x8/+0x14/+0x20) read.
Matrix const& BuzzBehavior::FetchHandleBasis(IObjInspect const* Handle)
{
    // .text:062c6354-062c6360: `MOV ECX,[ESP+0xb8]; MOV EAX,[ECX]; CALL [EAX+0xc]`, no test.
    return Handle->get_transform().Rotation;
}

// NavHelper[+0x14fc][+0x84]->vtbl[0xc](): the own ship's (SelfTarget.Iface) orientation,
// same shape as FetchHandleBasis. Every retail site (062c65ba, 062c6c2a, the metric sites)
// calls through it with no test.
Matrix const& BuzzBehavior::FetchNavCandidateBasis(SystemManager* NavHelper)
{
    return NavHelper->SelfTarget.Iface->get_transform().Rotation;
}

// The three NavHelper+0x34 byte probes (the timer table's getters).
bool BuzzBehavior::CheckNavHelperTimeout(SystemManager* NavHelper)
{
    return NavHelper->Timers.IsTimer0Active();   // FUN_062e8810
}

bool BuzzBehavior::CheckNavHelperFlagA(SystemManager* NavHelper)
{
    return NavHelper->Timers.IsTimer5Active();   // FUN_062e8840
}

bool BuzzBehavior::CheckNavHelperFlagB(SystemManager* NavHelper)
{
    return NavHelper->Timers.IsTimer4Active();   // FUN_062e8850
}

// FUN_062e1e20(Basis, LeadPoint, CandidatePos) = AngleToBasisZColumn3 (common_ini.h), a
// __cdecl 3-argument call (`ADD ESP,0xc` at 062c68e9). LeadPoint is the own
// SelfTarget.CachedPos (NavHelper+0x1508), passed by address.
double BuzzBehavior::ReadCandidateMetric(SystemManager* NavHelper, Vector const& CandidatePos)
{
    // .text:062c68bf-062c68de: PUSH &CandidatePos, PUSH NavRecord+0xc (the
    // record's CachedPos itself, not a copy), then Iface->get_transform(), PUSH EAX.
    // AngleToBasisZColumn3 takes the basis as three consecutive Vectors (common_ini.h).
    Vector const* const Basis = reinterpret_cast<Vector const*>(&FetchNavCandidateBasis(NavHelper));
    return AngleToBasisZColumn3(Basis, &NavHelper->SelfTarget.CachedPos, &CandidatePos);
}
